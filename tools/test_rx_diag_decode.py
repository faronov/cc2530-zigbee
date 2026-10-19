#!/usr/bin/env python3
"""Offline tests for tools/rx_diag_decode.py (synthetic blocks/ELFs; no device, no image)."""
import contextlib
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import rx_diag_decode as rd  # noqa: E402

try:
    import elftools  # noqa: F401
    HAVE_ELFTOOLS = True
except ImportError:
    HAVE_ELFTOOLS = False


def block(**values):
    """A valid all-zero-after-reset v1 block with selected fields set."""
    data = bytearray(rd.SIZE)
    data[0:6] = b"RXDG\x01\x50"
    for name, fmt, offset in rd.FIELDS:
        if name in values:
            struct.pack_into("<" + fmt, data, offset, values[name])
    return bytes(data)


def elf32(value=0x1E40, size=80, section=".mcs51_rx_diag", nobits=True, copies=1):
    """Minimal little-endian ELF32 with one section holding cc2530_rx_diag symbol(s)."""
    shstr = b"\0" + section.encode() + b"\0.symtab\0.strtab\0.shstrtab\0"
    name_sec, name_sym = 1, len(section) + 2
    name_str, name_shs = name_sym + 8, name_sym + 16
    strtab = b"\0cc2530_rx_diag\0"
    symtab = bytes(16) + b"".join(struct.pack("<IIIBBH", 1, value, size, 0x11, 0, 1) for _ in range(copies))
    body = symtab + strtab + shstr
    sym_off, str_off, shs_off = 52, 52 + len(symtab), 52 + len(symtab) + len(strtab)
    shoff = (52 + len(body) + 3) & ~3
    sections = [bytes(40),
                struct.pack("<10I", name_sec, 8 if nobits else 1, 3, value, shoff, size, 0, 0, 1, 0),
                struct.pack("<10I", name_sym, 2, 0, 0, sym_off, len(symtab), 3, 1, 4, 16),
                struct.pack("<10I", name_str, 3, 0, 0, str_off, len(strtab), 0, 0, 1, 0),
                struct.pack("<10I", name_shs, 3, 0, 0, shs_off, len(shstr), 0, 0, 1, 0)]
    header = (b"\x7fELF\x01\x01\x01" + bytes(9) +
              struct.pack("<HHIIIIIHHHHHH", 2, 165, 1, 0, 0, shoff, 0, 52, 0, 0, 40, len(sections), 4))
    data = header + body
    return data + bytes(shoff - len(data)) + b"".join(sections)


class Layout(unittest.TestCase):
    def test_fields_match_c_header(self):
        offsets, const = rd.header_layout()
        self.assertEqual({n: o for n, _, o in rd.FIELDS}, offsets)
        self.assertEqual(const, {"ADDRESS": rd.ADDRESS, "SIZE": rd.SIZE, "VERSION": rd.VERSION,
                                 "LIMIT": rd.LIMIT})

    def test_fields_tile_block_naturally_aligned(self):
        end = 0
        for item in rd.layout():
            self.assertEqual(item["offset"], end, item["field"])
            if item["size"] in (2, 4):
                self.assertEqual(item["offset"] % item["size"], 0, item["field"])
            end += item["size"]
        self.assertEqual(end, rd.SIZE)
        self.assertLessEqual(rd.ADDRESS + rd.SIZE, rd.LIMIT)


class Decode(unittest.TestCase):
    def test_reset_block(self):
        r = rd.decode(block())
        self.assertEqual(r["first_overflow"], {"status": "NOT_RECORDED"})
        self.assertIsNone(r["last_good_mac_header"])
        self.assertEqual(r["flags"], [])
        self.assertTrue(all(c["ok"] for c in r["consistency"]))
        for key in ("first_loss_context", "first_bdb_failure", "first_uplink_failure"):
            self.assertEqual(r[key]["status"], "NOT_RECORDED")

    def test_rejects_foreign_or_damaged_dumps(self):
        good = block()
        for bad, why in ((good[:79], "short"), (good + b"\0", "long"), (b"RXDX" + good[4:], "signature"),
                         (good[:4] + b"\x02" + good[5:], "version"), (good[:5] + b"\x40" + good[6:], "size"),
                         (block(pad=1), "pad"), (block(reserved=b"\0\0\0\0\0\0\0\1"), "reserved"),
                         (block(flags=4), "flags"), (bytes(80), "erased"), (b"\xff" * 80, "unwritten")):
            with self.subTest(why), self.assertRaises(rd.DecodeError):
                rd.decode(bad)

    def test_first_overflow_and_headers(self):
        r = rd.decode(block(overflows=2, salvaged=3, flushes=2, rx_lost=1, frames=9, first_valid=1,
                            first_site=2, first_rferrf=4, first_fsmstat1=0x40, first_rxfifocnt=128,
                            first_stamp=0x123456, first_gap=47, first_frames=4, flags=1,
                            last_header=b"\x61\x88\x5a\x1c", first_header=b"\x00\x80\x07\x28"))
        f = r["first_overflow"]
        self.assertEqual((f["status"], f["rxoverf"], f["fifo_fifop"], f["rxfifocnt"]), ("PROVEN", True, "01", 128))
        self.assertEqual(f["service_gap"], {"ticks": 47, "ms_nominal": 1.434, "status": "PROVEN"})
        self.assertIn("observe", f["note"])
        self.assertEqual(f["last_good_before"]["frame_type"], "beacon")
        h = r["last_good_mac_header"]
        self.assertEqual((h["frame_type"], h["ack_request"], h["pan_id_compression"], h["dsn"], h["phr_length"],
                          h["dst_mode"], h["src_mode"]), ("data", True, True, 0x5a, 28, 2, 2))
        self.assertEqual(r["flags"], ["SERVICE_VALID"])
        self.assertTrue(all(c["ok"] for c in r["consistency"]))

    def test_saturation_and_unknown(self):
        r = rd.decode(block(overflows=255, flushes=255, rx_lost=255, frames=0xFFFF, first_valid=1,
                            max_service_gap=0xFFFE, max_rx_off_gap=0xFFFF, last_service_gap=3))
        self.assertEqual(r["counters"]["overflows"]["status"], "PROVEN_LOWER_BOUND")
        self.assertEqual(r["counters"]["frames"]["status"], "PROVEN_LOWER_BOUND")
        self.assertEqual(r["counters"]["salvaged"]["status"], "PROVEN")
        self.assertEqual(r["max_service_gap"]["status"], "PROVEN_LOWER_BOUND")
        self.assertEqual(r["max_rx_off_gap"], {"ticks": None, "status": "UNKNOWN", "note": "no valid previous stamp"})
        self.assertEqual(r["last_service_gap"]["ticks"], 3)
        self.assertTrue(all(c["ok"] for c in r["consistency"]))

    def test_inconsistent_blocks_are_flagged_not_hidden(self):
        cases = {"first_valid iff overflows > 0": block(overflows=1),
                 "flushes <= overflows (one per episode)": block(overflows=1, first_valid=1, flushes=2),
                 "rx_lost <= flushes": block(rx_lost=1),
                 "first_frames <= frames": block(overflows=1, first_valid=1, first_frames=2),
                 "no first-overflow snapshot without first_valid": block(first_rferrf=4),
                 "no tx snapshot without tx_valid": block(tx_outcome=3)}
        for name, data in cases.items():
            with self.subTest(name):
                failed = [c["check"] for c in rd.decode(data)["consistency"] if not c["ok"]]
                self.assertEqual(failed, [name])

    def test_upper_layer_snapshots(self):
        r = rd.decode(block(ctx_valid=1, ctx_bdb_phase=2, fail_valid=1, fail_workspace=3, fail_aps_result=1,
                            fail_aps_retries=3, tx_valid=1, tx_outcome=2, tx_retries=3, tx_special=0x80),
                      names={"tx_outcome": {2: "SOME_NAME"}})
        self.assertEqual(r["first_loss_context"]["bdb_phase"], {"value": 2, "name": None})
        self.assertEqual(r["first_bdb_failure"]["aps"], {"result": 1, "retries": 3, "error": 0})
        self.assertEqual(r["first_uplink_failure"]["outcome"], {"value": 2, "name": "SOME_NAME"})
        self.assertEqual(r["first_uplink_failure"]["kind"], "APS ACK")
        self.assertIsNone(rd.decode(block(fail_valid=1, fail_workspace=1))["first_bdb_failure"]["aps"])

    def test_names_from_firmware_source(self):
        names = rd.source_names(rd.ROOT)
        self.assertEqual(names["first_radio_state"][0], "RADIO_AUTOACK_COLD")
        self.assertEqual(names["fail_bdb_phase"][0], "BDB_JOIN_IDLE")
        self.assertIn("BDB_JOIN_READY", names["ctx_bdb_phase"].values())
        self.assertIn("MAC_TX_OUTCOME_NONE", names["tx_outcome"].values())

    def test_read_command(self):
        cmd = rd.read_command(0x1E40)
        self.assertIn("--space xdata --address 0x1E40 --length 80", cmd)
        self.assertIn("access=existing-debug-session", cmd)
        for intrusive in ("halt", "reset", "resume", "flash", "write"):
            self.assertNotIn(intrusive, cmd)


@unittest.skipUnless(HAVE_ELFTOOLS, "pyelftools not installed")
class Image(unittest.TestCase):
    def run_cli(self, *argv):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = rd.main(list(argv))
        return code, out.getvalue(), err.getvalue()

    def write(self, directory, name, data):
        path = Path(directory) / name
        path.write_bytes(data)
        return str(path)

    def test_address_from_elf(self):
        with tempfile.TemporaryDirectory() as d:
            where = rd.locate(self.write(d, "hw1.elf", elf32()))
            self.assertEqual((where["address"], where["size"]), (0x1E40, 80))
            for bad, why in ((elf32(value=0x1E30), "address"), (elf32(size=64), "size"),
                             (elf32(section=".xdata"), "section"), (elf32(nobits=False), "progbits"),
                             (elf32(copies=2), "duplicate"), (elf32(copies=0), "missing")):
                with self.subTest(why), self.assertRaises(rd.DecodeError):
                    rd.locate(self.write(d, why + ".elf", bad))

    def test_cli_end_to_end(self):
        with tempfile.TemporaryDirectory() as d:
            elf = self.write(d, "hw1.elf", elf32())
            code, out, _ = self.run_cli("--elf", elf, "--print-read-command")
            self.assertEqual((code, out.strip()), (0, rd.read_command(0x1E40)))
            dump = self.write(d, "rx_diag.bin", block(frames=1, last_header=b"\x00\x80\x01\x1c"))
            report_path = Path(d) / "report.json"
            code, _, _ = self.run_cli("--elf", elf, "--dump", dump, "--output", str(report_path))
            report = json.loads(report_path.read_text())
            self.assertEqual(code, 0)
            self.assertEqual(report["image"]["address"], 0x1E40)
            self.assertIn("CC2530 only", report["clock"]["domain"])
            self.assertEqual(report["last_good_mac_header"]["frame_type"], "beacon")
            code, _, _ = self.run_cli("--elf", elf, "--dump", self.write(d, "bad.bin", block(rx_lost=1)))
            self.assertEqual(code, 1)
            code, _, err = self.run_cli("--elf", elf, "--dump", self.write(d, "short.bin", block()[:40]))
            self.assertEqual(code, 2)
            self.assertIn("40 bytes", err)
            code, _, err = self.run_cli("--dump", dump)
            self.assertEqual(code, 2)
            self.assertIn("--elf required", err)


if __name__ == "__main__":
    unittest.main()
