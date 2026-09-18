# SPDX-License-Identifier: BSD-3-Clause
"""Synthetic runner/path/ABI checks only. Never enumerate or open USB."""
from contextlib import redirect_stderr, redirect_stdout
from dataclasses import dataclass, replace
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import check_radio_rx_hardware as runner
from debug_image import DebugImage
from radio_rx_fixture import CHECKPOINTS, LENGTHS, check_frame, decode, instructions, verify_driver_listing
from verify_firmware import IMAGES, ROOT, cdb_address, parse_ihex, verify_layout
import test_m0_artifacts


PROGRAM = b"\0"*0x100+b"\0\x22\0\x22\0\x80\xfd\0\x80\xfd"
PROOF = dict(checkpoints=[0x100, 0x102, 0x104, 0x107], state=0x300,
             frame=0x400, diagnostics=0x500, clock=0x600)
IMAGE = SimpleNamespace(
    image_name="radio_rx_fixture", board="generic", radio_rx_proof=PROOF,
    metrics={"image_extent_bytes": len(PROGRAM), "iram_stack_start": 0x61},
    sha256=hashlib.sha256(PROGRAM).hexdigest(),
    symbol=lambda n: SimpleNamespace(address=PROOF["checkpoints"][CHECKPOINTS.index(n)]),
)


def wire(attempt=0, *, before=False, crc=True, fault=False):
    data = bytearray(96)
    data[:6] = b"M2RX\x01\x60"
    data[6:16] = bytes((1 if before else 4 if fault else 3, 4 if fault else 0, int(not before),
                       attempt, attempt if crc and not fault else 0,
                       10 if fault else 255 if not attempt else 0 if crc else 15,
                       10 if fault else 0, 8 if before else 0, 15, 16))
    data[16:22] = b"\0\0\x01\0\xff\xff"
    data[40] = 8
    if not before:
        data[22:28] = b"\x01\0\0\0\x01\0"
        data[36:40] = b"\xc9\x88\x88\x88"
    if attempt:
        data[41:47] = b"\x21\0\0\0\x20\0"
        data[48:53] = bytes((3 if fault else 7 if crc else 6, 10, 10, 1 if fault else 7, 1))
        data[53] = 128 if fault else 0
        data[64:72] = bytes((0xe6, 4, 1, 0 if fault else 4, 0 if fault else 3,
                             0 if fault else 0x81, 0 if fault else 0xff if crc else 0x7f, 0))
    data[72:79] = bytes((0xc9 if before else 0x88, 0xc9 if before else 0x88, 4, 0, 0, 0, 4))
    data[93:] = b"\x69\x96\xc7"
    return bytes(data)


def frame(raw):
    return b"\x01\x81\x7f\x68"+b"\xa5"*124 if raw[11] == 0 else b"\xa5"*128


@dataclass(frozen=True)
class Registers:
    pc: int = 0
    sp: int = 0x62
    dps: int = 0
    a: int = 0x69
    dptr0: int = 0x1234
    dptr1: int = 0x5678
    b: int = 0x96
    psw: int = 0x18
    r: tuple = tuple(range(8))


class Synthetic:
    """Runner-level fake only; real C/MMIO success is tested separately."""
    def __init__(self, records, fail_at=None):
        self.records = records
        self.cursor = -1
        self.registers = Registers()
        self.calls = []
        self.fail_at = fail_at

    def call(self, name):
        self.calls.append(name)
        if len(self.calls) == self.fail_at: raise OSError("synthetic terminal transport failure")

    def read_adapter_state(self): self.call("adapter"); return None
    def attach_reset(self): self.call("reset")
    def read_pc(self): self.call("pc"); return self.registers.pc
    def read_debug_config(self): self.call("config"); return 0x26
    def read_code(self, start, size): self.call("code"); return PROGRAM[start:start+size]
    def set_breakpoint(self, *args): self.call("breakpoint")
    def read_registers(self): self.call("registers"); return self.registers
    def step(self):
        self.call("step")
        self.registers = replace(self.registers, pc=self.registers.pc+1)
        return SimpleNamespace(accumulator=self.registers.a)
    def resume(self):
        self.call("resume"); self.cursor += 1
        self.registers = replace(self.registers, pc=0x100 if self.cursor == 0 else
                                 0x104 if self.records[self.cursor][6] == 4 else 0x102)
    def read_xdata(self, address, size):
        self.call("sram")
        raw = self.records[self.cursor]
        values = {0x300: raw, 0x400: frame(raw), 0x500: raw[41:72], 0x600: raw[22:41],
                  0x1e00: b"M0CC\x01\x20\x02\0"+bytes((raw[10],))+bytes(15)+b"\xc9\xc9"+bytes(6)}
        value = values[address]
        assert len(value) == size
        return value


class WireTests(unittest.TestCase):
    def test_wire_and_nonpublication(self):
        for raw in (wire(before=True), wire(), wire(1), wire(1, crc=False), wire(1, fault=True)):
            check_frame(decode(raw), frame(raw))
        raw = wire(1)
        for index, value in ((0, 0), (4, 2), (5, 95), (6, 2), (7, 1), (8, 2), (9, 17),
                             (10, 2), (11, 4), (12, 4), (13, 8), (14, 16), (15, 17),
                             (16, 1), (20, 0), (28, 1), (40, 0), (48, 6), (49, 9),
                             (51, 3), (52, 0), (53, 128), (57, 1), (65, 1), (67, 5),
                             (68, 2), (70, 0), (72, 0xc9), (75, 1), (86, 1), (92, 1), (95, 0)):
            mutated = bytearray(raw); mutated[index] = value
            with self.subTest(index=index), self.assertRaises(ValueError): decode(bytes(mutated))
        for bad in (b"", raw+b"\0", bytearray(raw)):
            with self.assertRaises(ValueError): decode(bad)
        for r, f in ((wire(1), b"\0"*128), (wire(1), frame(raw)[:-1]+b"\0"),
                     (wire(1, crc=False), frame(raw)), (wire(), b"\xa5"*127)):
            with self.assertRaises(ValueError): check_frame(decode(r), f)

    def test_stif_history(self):
        previous = decode(wire())
        current = bytearray(wire(1)); current[92] = 128
        record = decode(bytes(current)); boot = bytes(32)
        runner.progression(record, previous, boot, boot)
        after = decode(wire(2))
        with self.assertRaisesRegex(ValueError, "STIF"): runner.progression(after, record, boot, boot)

    def test_standalone_sources_always_banned_and_scoped_budget(self):
        base = test_m0_artifacts.LayoutTests(); base.setUp()
        for image in IMAGES:
            for source in ("test_radio_rx.c", "test_radio_rx_fixture.c"):
                with self.assertRaisesRegex(ValueError, "isolated passive RX"):
                    verify_layout(base.symbols, base.memory, base.debug+f"\nC${source}$1", image)
            with self.assertRaisesRegex(ValueError, "isolated passive RX"):
                verify_layout(base.symbols | {"_radio_rx_test_frame": 0}, base.memory, base.debug, image)
        debug = base.debug.replace("bringup.c", "radio_rx_fixture.c")
        symbols = base.symbols | {"l_XSEG": 960}
        self.assertEqual(verify_layout(symbols, base.memory, debug, "radio_rx_fixture")
                         ["nonaliased_xdata_reserved_bytes"], 1024)
        with self.assertRaisesRegex(ValueError, "budget"):
            verify_layout(symbols | {"l_XSEG": 961}, base.memory, debug, "radio_rx_fixture")
        with self.assertRaisesRegex(ValueError, "budget"):
            verify_layout(symbols, base.memory, base.debug)


class ListingTests(unittest.TestCase):
    def test_exact_ordered_instructions_required(self):
        code = {0x100: b"\x75\x82\0", 0x103: b"\x22"}
        lines = ("      000100 75 82 00       [24] 1 mov dpl,#0\n",
                 "      000103 22             [24] 2 ret\n")
        listing = "".join(lines)
        verify_driver_listing(code, listing)
        for bad in ("", lines[0], listing.replace("75 82", "75 83"),
                    listing.replace("000100", "000101"), listing+lines[1],
                    "".join(reversed(lines))):
            with self.subTest(listing=bad), self.assertRaisesRegex(ValueError, "listing"):
                verify_driver_listing(code, bad)

    @unittest.skipUnless(all(shutil.which(tool) for tool in ("make", "sdcc", "packihx", "makebin")),
                         "SDCC build tools required for real shared-link regression")
    def test_image_listings_survive_both_link_orders(self):
        with tempfile.TemporaryDirectory() as directory:
            for board in ("generic", "lg_esl29_rev03"):
                with self.subTest(board=board):
                    output = Path(directory)/board
                    command = ["make", "--no-print-directory", "--jobs=1", "-s", f"BOARD={board}",
                               "IMAGE=radio_rx_fixture", f"BUILD={output}"]

                    def build(*targets):
                        result = subprocess.run(command+list(targets), cwd=ROOT, capture_output=True,
                                                text=True, timeout=120)
                        self.assertEqual(result.returncode, 0, result.stdout+result.stderr)

                    def checked_listing(name):
                        image = parse_ihex((output/f"{name}.ihx").read_text())
                        debug = (output/f"{name}.cdb").read_text()
                        start = cdb_address(debug, "L:Fradio_rx$ordinary$0$0")
                        stop = cdb_address(debug, "L:XG$radio_rx_receive_init$0$0")+1
                        code = instructions(image, start, stop, LENGTHS)
                        listing = (output/f"{name}.radio_rx.rst").read_text()
                        verify_driver_listing(code, listing)
                        return code, listing

                    build("all", str(output/"radio_rx_test.ihx"))
                    board_code, board_listing = checked_listing("radio_rx_fixture")
                    test_code, test_listing = checked_listing("radio_rx_test")
                    self.assertNotEqual(board_code, test_code)
                    self.assertEqual((output/"radio_rx.rst").read_text(), test_listing)
                    with self.assertRaisesRegex(ValueError, "listing"):
                        verify_driver_listing(board_code, test_listing)
                    build("all")
                    self.assertEqual(checked_listing("radio_rx_fixture"), (board_code, board_listing))
                    self.assertEqual(checked_listing("radio_rx_test"), (test_code, test_listing))
                    self.assertEqual((output/"radio_rx.rst").read_text(), board_listing)
                    with self.assertRaisesRegex(ValueError, "listing"):
                        verify_driver_listing(test_code, board_listing)


class PathTests(unittest.TestCase):
    def test_private_create_no_overwrite_or_symlinks(self):
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory)/"capture"
            with runner.private_capture(p) as out:
                out.write("synthetic only\n")
            self.assertEqual(stat.S_IMODE(p.stat().st_mode), 0o600)
            with self.assertRaises(OSError): runner.private_capture(p)
            link = Path(directory)/"link"; link.symlink_to(p)
            with self.assertRaises(OSError): runner.private_capture(link)
            child = Path(directory)/"child"; child.mkdir(mode=0o700)
            link.unlink(); link.symlink_to(child, target_is_directory=True)
            with self.assertRaises(OSError): runner.private_capture(link/"capture")
            os.chmod(child, 0o755)
            with self.assertRaises(ValueError): runner.private_capture(child/"capture")
            with patch.object(runner.os, "getuid", return_value=os.getuid()+1):
                with self.assertRaises(ValueError): runner.private_capture(Path(directory)/"other")
        for p in ("relative", ROOT/"capture", ROOT/".."/"capture"):
            with self.assertRaises(ValueError): runner.private_capture(p)


class RunnerTests(unittest.TestCase):
    def run_fake(self, fake, capture, attempts=1):
        with patch.object(runner, "wait_checkpoint", side_effect=lambda d, *a: d.read_pc()):
            return runner.exercise(fake, IMAGE, PROGRAM, capture, attempts)

    def test_normal_crc_fault_and_every_transport_boundary(self):
        with tempfile.TemporaryFile(mode="w+") as out:
            fake = Synthetic([wire(before=True), wire(), wire(1)])
            result = self.run_fake(fake, out)
            self.assertEqual(result["completed"], 1)
            self.assertNotIn("hex", json.dumps(result)); self.assertNotIn("sha256", json.dumps(result))
            out.seek(0); self.assertEqual(len(out.readlines()), 3)
            total = len(fake.calls)
            for boundary in range(1, total+1):
                failed = Synthetic(fake.records, boundary)
                with self.subTest(boundary=boundary), self.assertRaises(OSError):
                    self.run_fake(failed, out)
                self.assertEqual(len(failed.calls), boundary)
            crc = Synthetic([wire(before=True), wire(), wire(1, crc=False)])
            self.assertEqual(self.run_fake(crc, out)["bad_crc"], 1)
            failed = Synthetic([wire(before=True), wire(), wire(1, fault=True)])
            with self.assertRaisesRegex(ValueError, "FAULT"): self.run_fake(failed, out)
            self.assertEqual(failed.calls.count("resume"), 3)
            many = Synthetic([wire(before=True), wire()]+[wire(i) for i in range(1, 17)])
            self.assertEqual(self.run_fake(many, out, 16)["completed"], 16)

    def test_all_preconditions_before_target(self):
        for attempts in (0, 17, True, 1.0):
            with self.assertRaises(ValueError): runner.validate_program(IMAGE, PROGRAM, attempts)
        for program in (b"", PROGRAM[:-1], PROGRAM+b"\0", b"\0"*len(PROGRAM), bytearray(PROGRAM)):
            with self.assertRaises(ValueError): runner.validate_program(IMAGE, program, 1)
        with tempfile.TemporaryFile(mode="w+") as out:
            out.close()
            fake = Synthetic([wire(before=True), wire()])
            with self.assertRaises(ValueError): self.run_fake(fake, out)
            self.assertEqual(fake.calls.count("resume"), 1)

    def test_cli_never_loads_usb_on_invalid_input(self):
        args = ["--board", "generic", "--output", "missing-artifacts", "--bus", "1", "--address", "2",
                "--capture", "/not-a-private-capture"]
        with patch.object(runner.PyUsbBackend, "load") as usb, redirect_stderr(io.StringIO()), \
                redirect_stdout(io.StringIO()) as stdout:
            self.assertEqual(runner.main(args), 1)
            self.assertEqual(runner.main(args+["--confirm-passive-rx-test"]), 1)
            usb.assert_not_called()
            self.assertEqual(stdout.getvalue(), "")

    def test_genuine_preflight(self):
        for board in ("generic", "lg_esl29_rev03"):
            path = ROOT/"build"/board/"radio_rx_fixture"
            if not (path/"build-info.json").exists(): continue
            image = DebugImage(path, board, "radio_rx_fixture")
            program = (path/"radio_rx_fixture.bin").read_bytes()
            runner.validate_program(image, program, 16)
            self.assertEqual(image.symbol("_radio_rx_fixture_frame").size, 128)
            self.assertEqual(image.symbol("_radio_rx_fixture_diagnostics").size, 31)

    def test_cli_private_preflight_and_cleanup_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory)
            (p/"radio_rx_fixture.bin").write_bytes(PROGRAM)
            args = ["--board", "generic", "--output", str(p), "--bus", "1", "--address", "2",
                    "--capture", str(p/"capture"), "--confirm-passive-rx-test"]
            with patch.object(runner, "DebugImage", return_value=IMAGE), \
                    patch.object(runner.PyUsbBackend, "load") as usb, redirect_stderr(io.StringIO()), \
                    redirect_stdout(io.StringIO()) as stdout:
                self.assertEqual(runner.main(args+["--attempts", "17"]), 1)
                usb.assert_not_called()
                os.chmod(p, 0o755)
                self.assertEqual(runner.main(args), 1); usb.assert_not_called()
                os.chmod(p, 0o700)
                with patch.object(runner, "Debugger") as debugger, \
                        patch.object(runner, "exercise", return_value={"completed": 1}):
                    debugger.return_value.__exit__.side_effect = OSError("synthetic cleanup failure")
                    self.assertEqual(runner.main(args), 1)
                    kwargs = debugger.call_args.kwargs
                    self.assertNotIn("allow_dma_enable", kwargs)
                    self.assertNotIn("allow_memory_write", kwargs)
                self.assertEqual(stdout.getvalue(), "")


if __name__ == "__main__":
    unittest.main()
