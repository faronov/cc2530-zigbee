# SPDX-License-Identifier: BSD-3-Clause
"""Public offline decoder, exact linked binding and orchestration regressions."""
from contextlib import redirect_stdout
import hashlib
import io
import json
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from debug_image import DebugImage
from fixture_test_artifacts import linked_fixture
from radio_noise_fixture import (
    ARM, RUN, SIZE, CAPTURE_SIZE, HEALTH_SIZE, CLOCK_SIZE, CHECKPOINTS, OBJECTS,
    decode, decode_capture, modules, verify_fixture, verify_listings, verify_memory,
)
from verify_firmware import ARTIFACT_EXTENSIONS, ROOT, parse_ihex, parse_symbols, require, verify_artifacts

CDB_SEPARATORS = ("\r\n", "\r", "\v", "\f", "\x1c", "\x1d", "\x1e", "\x85", "\u2028", "\u2029")


def state(phase=1, reason=0):
    raw = bytearray(b"M2RN\x01\x14\x01\0\0\xff\xff\x08\0\0\x1a\x01\0\0\x69\x96")
    raw[6] = phase; raw[7] = reason
    if phase >= 2:
        raw[11] = 0
    if phase == 3:
        raw[8:11] = b"\x01\0\0"
    return raw


class DecoderTests(unittest.TestCase):
    def test_exact_packets_and_separate_fixed_profile(self):
        self.assertEqual(ARM.hex(), "a6591ae500040100a08601102767983c")
        self.assertEqual(RUN.hex(), "59a61ae500040100a0860110276798c3")
        self.assertEqual((len(ARM), len(RUN), OBJECTS["command"][1]), (16, 16, 16))
        self.assertEqual(ARM[2:15], RUN[2:15])
        self.assertEqual((SIZE, CAPTURE_SIZE, HEALTH_SIZE, CLOCK_SIZE), (20, 171, 15, 19))

    def test_every_capture_metadata_field_is_exposed_without_stdout(self):
        expected = {
            "elapsed_ticks": 1000, "first_before": 0xfffff0, "last_after": 0x100,
            "min_gap": 1, "max_gap": 3, "max_span": 2,
            "samples": 9, "timed_samples": 8, "polls": 55,
            "phase": 3, "writes": 10, "verified": 10, "actions": 1,
            "timebase_status": 0, "rx_enable": 0x80, "calibration": 0,
            "signals": 5, "rssi_valid": 1, "errors": 2,
            "flags0": 0x80, "flags1": 4, "last_raw": 4,
        }
        data = b"\x96" + bytes(127)  # Synthetic nine-bit prefix; zero unused tail.
        raw = struct.pack("<6I3H13B128s", *expected.values(), data)
        output = io.StringIO()
        with redirect_stdout(output):
            actual = decode_capture(raw)
        self.assertEqual(output.getvalue(), "")
        self.assertEqual(actual, expected | {"data_hex": data.hex()})

    def test_disarmed_admitted_end_and_separate_health_failure(self):
        for phase, name in ((1, "DISARMED"), (2, "ADMITTED"), (3, "END")):
            record = decode(bytes(state(phase)))
            self.assertEqual(record["phase"], name)
            self.assertEqual(record["attempts"], int(phase == 3))
            self.assertEqual(record["result"], 0 if phase == 3 else 255)
        raw = state(3); raw[10] = 3; raw[12] = 21
        record = decode(bytes(raw))
        self.assertEqual((record["phase"], record["health_result"], record["first_failure"]), ("END", 3, 21))
        raw = state(4, 4); raw[8:11] = b"\x01\x0c\0"
        self.assertEqual(decode(bytes(raw))["fault"], "ACQUISITION")
        raw = state(4, 3); raw[11] = 3
        self.assertEqual(decode(bytes(raw))["fault"], "CLOCK")

    def test_no_live_checkpoint_or_invalid_status(self):
        base = state()
        for index in (*range(6), 6, 7, 8, 9, 10, 11, 12, 13, *range(14, 20)):
            bad = bytearray(base); bad[index] ^= 1
            with self.subTest(index=index), self.assertRaises(ValueError):
                decode(bytes(bad))
        for raw in (bytes(19), bytes(21), None, "x"*20, base):
            with self.assertRaises(ValueError):
                decode(raw)
        live = state(2); live[8] = 1
        with self.assertRaises(ValueError): decode(bytes(live))
        for health, first in ((0, 21), (3, 0), (1, 21), (4, 1025)):
            bad = state(3); bad[10] = health; bad[12:14] = first.to_bytes(2, "little")
            with self.assertRaises(ValueError): decode(bytes(bad))

    def test_private_capture_layout_prefix_bounds_and_reserved_tail(self):
        raw = bytearray(CAPTURE_SIZE)
        raw[24:26] = (17).to_bytes(2, "little"); raw[26:28] = (16).to_bytes(2, "little")
        raw[28:30] = (55).to_bytes(2, "little"); raw[30:34] = b"\x03\x0a\x0a\x01"
        raw[43:46] = b"\x96\x96\x01"
        record = decode_capture(bytes(raw))
        self.assertEqual((record["samples"], record["timed_samples"], record["phase"], record["actions"]), (17, 16, 3, 1))
        self.assertTrue(record["data_hex"].startswith("969601"))
        for start, value in ((24, 1025), (26, 18), (28, 10001)):
            bad = bytearray(raw); bad[start:start+2] = value.to_bytes(2, "little")
            with self.assertRaises(ValueError): decode_capture(bytes(bad))
        for index in (3, 7, 11, 15, 19, 23, 170):
            bad = bytearray(raw); bad[index] = 255
            with self.assertRaises(ValueError): decode_capture(bytes(bad))
        for data in (bytes(170), bytes(172), raw, None):
            with self.assertRaises(ValueError): decode_capture(data)


class LinkedTests(unittest.TestCase):
    def test_public_debug_image_interface_both_boards_and_complete_bindings(self):
        for board in ("generic", "lg_esl29_rev03"):
            path = linked_fixture(board, "radio_noise_fixture")
            image = DebugImage(path.parent, board, "radio_noise_fixture")
            code = parse_ihex(path.with_suffix(".ihx").read_text())
            symbols = parse_symbols(path.with_suffix(".map").read_text())
            debug = path.with_suffix(".cdb").read_bytes().decode("ascii")
            proof = image.radio_noise_proof
            for name, (address, size) in OBJECTS.items():
                self.assertEqual(proof[name], address)
                obj = image.symbol("_radio_noise_fixture_"+name)
                self.assertEqual((obj.address, obj.size, obj.space), (address, size, "XDATA"))
            for slot, checkpoint in enumerate(CHECKPOINTS):
                self.assertEqual(image.breakpoint(checkpoint, slot)["address"], proof["checkpoints"][slot])
            self.assertEqual(image.metrics["ordinary_xdata_bytes"], 440)
            self.assertEqual(image.metrics["iram_stack_start"], 47)
            for text in (debug+"\n", debug.replace("{171}", "{170}"), debug.replace("{15}ST", "{16}ST"),
                         debug.replace("C$radio_noise.c$", "C$test_radio_noise.c$"),
                         debug+"\rmalformed\n",
                         *(debug.replace("\n", separator, 1) for separator in CDB_SEPARATORS)):
                self.assertNotEqual(text, debug)
                with self.assertRaisesRegex(ValueError, "CDB"):
                    verify_fixture(code, symbols, text, board)
            verify_listings(path.parent, code, symbols, board)
            memory = path.with_suffix(".mem").read_text()
            verify_memory(memory)
            for damaged in (memory+"\nERROR: synthetic linker failure\n",
                            memory.replace("440", "441"), memory.replace("209 bytes", "210 bytes")):
                with self.assertRaises(ValueError):
                    verify_memory(damaged)
            self.assertFalse(json.loads((path.parent/"build-info.json").read_text())["hardware_tested"])

    def test_raw_cdb_identity_survives_real_loaders_and_updated_manifest(self):
        for board in ("generic", "lg_esl29_rev03"):
            source = linked_fixture(board, "radio_noise_fixture").parent
            with tempfile.TemporaryDirectory(prefix="irnd-cdb-negative-") as directory:
                output = Path(directory)
                names = ["build-info.json"]
                names += ["radio_noise_fixture."+suffix for suffix in ARTIFACT_EXTENSIONS]
                names += [f"radio_noise_fixture.{module}.rst" for module in modules(board)]
                for name in names:
                    shutil.copyfile(source/name, output/name)
                DebugImage(output, board, "radio_noise_fixture")
                path = output/"radio_noise_fixture.cdb"
                raw = path.read_bytes()
                metadata = output/"build-info.json"
                original_info = metadata.read_bytes()
                for separator in CDB_SEPARATORS:
                    damaged = raw.replace(b"\n", separator.encode("utf-8"), 1)
                    self.assertNotEqual(damaged, raw)
                    # Only the raw separator differs: line normalization would
                    # conceal the damage. An honestly refreshed manifest must
                    # not substitute for the independently pinned CDB identity.
                    self.assertEqual(damaged.decode("utf-8").splitlines(), raw.decode("ascii").splitlines())
                    path.write_bytes(damaged)
                    info = json.loads(original_info)
                    info["sha256"][path.name] = hashlib.sha256(damaged).hexdigest()
                    metadata.write_text(json.dumps(info), encoding="ascii")
                    with self.subTest(board=board, separator=repr(separator)):
                        with self.assertRaisesRegex(ValueError, "CDB"):
                            verify_artifacts(output, board, "radio_noise_fixture")
                        with self.assertRaisesRegex(ValueError, "CDB"):
                            DebugImage(output, board, "radio_noise_fixture")
                path.write_bytes(raw); metadata.write_bytes(original_info)
                DebugImage(output, board, "radio_noise_fixture")

    def test_genuine_shared_directory_relink_cannot_replace_fixture_snapshots(self):
        board = "generic"
        path = linked_fixture(board, "radio_noise_fixture")
        modules = ("timebase", "clock", "noise_health", "radio_noise")
        originals = {m: (path.parent/f"radio_noise_fixture.{m}.rst").read_bytes() for m in modules}
        result = subprocess.run(["make", "-s", "-j1", f"BOARD={board}", "IMAGE=bringup", f"BUILD={path.parent}",
                                 str(path.parent/"radio_noise_test.ihx")],
                                cwd=ROOT, capture_output=True, text=True, timeout=120)
        self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
        self.assertTrue(any((path.parent/f"{m}.rst").read_bytes() != originals[m] for m in ("timebase", "radio_noise")))
        for m, raw in originals.items():
            self.assertEqual((path.parent/f"radio_noise_fixture.{m}.rst").read_bytes(), raw)
        DebugImage(path.parent, board, "radio_noise_fixture")


class CountedWaitTests(unittest.TestCase):
    def test_compression_never_accepts_changing_inputs_writes_or_raw_reads(self):
        # Test-only import exposes no USB or physical backend.
        import sys
        with patch.object(sys, "path", [str(ROOT/"tests")]+sys.path):
            from boot_radio_noise_fixture import repeated_events
        block = [["o", 0x624a, 0xa5, []], ["t", 0x95, 7, []]]
        self.assertEqual(repeated_events(block*10000, 0), (2, 10000))
        self.assertEqual(repeated_events(block*31, 0), (0, 0))
        for kind in ("r", "w"):
            self.assertEqual(repeated_events([[kind, 0x61a7, 1, []], block[1]]*100, 0), (0, 0))
        changed = [[e[0], e[1], e[2], [[0x6199, 1]]] for e in block]
        self.assertEqual(repeated_events(changed*100, 0), (0, 0))
        changing = [e for i in range(100) for e in (block[0], ["t", 0x95, i, []])]
        self.assertEqual(repeated_events(changing, 0), (0, 0))


if __name__ == "__main__":
    unittest.main()
