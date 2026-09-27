# SPDX-License-Identifier: BSD-3-Clause
"""Synthetic operator transactions only; no USB, serial, RF or board image execution."""
from contextlib import contextmanager, redirect_stderr, redirect_stdout
from dataclasses import replace
import hashlib
import io
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from banked_image import ihex
from cc_debugger import RegisterSnapshot
import check_mac_smoke_hardware as runner


PHYSICAL = {address: (address * 19 + (address >> 15)) & 255
            for start, end in ((0, 0x79F8), (0x8000, 0xCEB4), (0x10000, 0x11A51))
            for address in range(start, end)}
PHYSICAL.update(zip(range(0x7854, 0x785C), b"\0\x22\0\x80\xfd\0\x80\xfd"))
HEX = ihex(PHYSICAL).encode("ascii")
DIGEST = hashlib.sha256(HEX).hexdigest()
PROGRAM = bytes(PHYSICAL.get(address, 255) for address in range(0x11A51))
IMAGE = runner.SmokeImage("generic", HEX, PROGRAM, len(PHYSICAL), 0x86B, 0x8AB,
                         0x8C1, 243, 0x7854, 0x7856, 0x7859, 0x66, 0x57)


def wire(phase=1, remaining=256, busy=0, outcome=1):
    raw = bytearray(runner.INITIAL)
    raw[6] = phase
    raw[28:30] = remaining.to_bytes(2, "little")
    if phase == 5:
        attempts = busy + 1 if outcome == 1 else 5
        sent = int(outcome == 1)
        raw[8:24] = bytes((6, 1, 1, outcome, 0, 0, 0, 2, 0, attempts,
                          2 * attempts + sent, attempts, attempts - sent, sent, sent, 2))
        raw[28:30] = bytes(2)
        raw[30:32] = (100).to_bytes(2, "little")
        raw[32:35] = (200).to_bytes(3, "little")
        raw[35:39] = (300).to_bytes(4, "little")
        raw[39:51] = bytes((sent, 0, 0, 0, 0, 0, 1, 7, 5, 0, 0x40, 0x20))
        raw[53] = 2 if sent else 3
    elif phase == 6:
        raw[7], raw[8], raw[9], raw[12], raw[14] = 4, 4, 1, 8, 15
    return bytes(raw)


class Synthetic:
    def __init__(self, *, outcome=1, busy=0, fail_at=None, bad_code=None,
                 bad_admission=False, bad_end=False, fault=False, banker=False):
        self.calls = []
        self.fail_at, self.bad_code = fail_at, bad_code
        self.bad_admission, self.bad_end = bad_admission, bad_end
        self.banker = banker
        self.records = [wire(), wire(remaining=255), wire(2), wire(3, 0),
                        wire(4 if banker else 6 if fault else 5, 0, busy, outcome)]
        self.cursor = -1
        self.mailbox = bytes(8)
        self.registers = RegisterSnapshot(pc=0, bank=1, a=0xA5, psw=0, b=0xE9,
                                         sp=7, dptr0=0xC123, dptr1=0xD456, dps=0,
                                         mpage=0, r=tuple(range(8)))

    def call(self, name):
        self.calls.append(name)
        if len(self.calls) == self.fail_at:
            raise OSError("synthetic transport failure")

    def open(self, address):
        self.call("open")

    def read_adapter_state(self):
        self.call("adapter")
        return SimpleNamespace(target_id=0x2530)

    def attach_reset(self):
        self.call("reset")

    def read_pc(self):
        self.call("pc")
        return self.registers.pc

    def read_debug_config(self):
        self.call("config")
        return 0x26

    def read_debug_status(self):
        self.call("status")
        return 0x22

    def read_registers(self):
        self.call("registers")
        return self.registers

    def read_sfr(self, address):
        self.call("fmap")
        assert address == 0x9F
        return 1

    def read_flash_code(self, address, size):
        self.call("physical")
        assert size <= 256 and address // 0x8000 == (address + size - 1) // 0x8000
        raw = bytearray(PROGRAM[address:address + size])
        if self.bad_code is not None and address <= self.bad_code < address + size:
            raw[self.bad_code - address] ^= 1
        return bytes(raw)

    def set_breakpoint(self, slot, address, enabled=True):
        self.call("breakpoint")
        assert (slot, address) in enumerate((IMAGE.wait, IMAGE.end, IMAGE.fault, IMAGE.banker))

    def step(self):
        self.call("step")
        self.registers = replace(self.registers, pc=self.registers.pc + 1)
        return SimpleNamespace(accumulator=self.registers.a)

    def resume(self):
        self.call("resume")
        self.cursor += 1
        assert self.cursor < 5
        if self.cursor in (2, 3):
            assert self.mailbox == runner.PACKETS[self.cursor - 2]
        self.mailbox = bytes(8)
        phase = self.records[self.cursor][6]
        pc = IMAGE.wait if phase < 4 else IMAGE.banker if self.banker else IMAGE.fault if phase == 6 else IMAGE.end
        self.registers = replace(self.registers, pc=pc, sp=0x71 if self.banker and phase == 4 else 0x57,
                                 dps=int(self.banker and phase == 4), bank=7 if self.banker and phase == 4 else 1)

    def write_xdata(self, address, data):
        self.call("write")
        assert address == IMAGE.mailbox and self.cursor in (1, 2) and data in runner.PACKETS
        self.mailbox = data

    def read_xdata(self, address, size):
        self.call("sram")
        raw = self.records[self.cursor]
        if self.bad_end and self.cursor == 4:
            raw = raw[:45] + b"\0" + raw[46:]
        boot = b"M0CC\x01\x20\x02\0" + bytes((raw[10],)) + bytes(15) + b"\xc9\xc9" + bytes(6)
        values = {IMAGE.status: raw, IMAGE.mailbox: self.mailbox, 0x1E00: boot,
                  0x1F1E: b"\x02\x04" if self.banker and self.cursor == 4 else bytes(2)}
        if address in values:
            result = values[address]
            assert len(result) == size
            return result
        assert address + size <= IMAGE.status or IMAGE.contexts <= address < IMAGE.contexts + IMAGE.context_size
        return b"\1" + bytes(size - 1) if self.bad_admission and self.cursor == 3 and address == 0 else bytes(size)


class OperatorTests(unittest.TestCase):
    def run_fake(self, fake, capture=None, **kwargs):
        with tempfile.TemporaryFile(mode="w+") as temporary, \
             patch.object(runner, "HASHES", {"generic": DIGEST}), \
             patch.object(runner, "wait_checkpoint", side_effect=lambda d, allowed: d.read_pc()):
            return runner.exercise(fake, IMAGE, DIGEST, capture or temporary, True, True, **kwargs)

    def test_fixed_status_and_all_admitted_terminal_counts(self):
        self.assertEqual(len(runner.INITIAL), 64)
        self.assertEqual((runner.decode(runner.INITIAL)["remaining"], runner.decode(runner.INITIAL)["adapter_result"]),
                         (256, 255))
        for busy in range(5):
            self.assertEqual(runner.check_end(runner.decode(wire(5, 0, busy))), "LOCAL_SENT")
        self.assertEqual(runner.check_end(runner.decode(wire(5, 0, outcome=2))), "CCA_BUSY")
        for index in (*range(6), *range(24, 28), *range(51, 53), *range(54, 64)):
            raw = bytearray(wire(5))
            raw[index] ^= 1
            with self.subTest(index=index), self.assertRaises(ValueError):
                runner.decode(bytes(raw))
        for index in (*range(6, 23), *range(39, 46), 47, 48, 53):
            raw = bytearray(wire(5))
            raw[index] ^= 0x80
            with self.subTest(index=index), self.assertRaises(ValueError):
                runner.check_end(runner.decode(bytes(raw)))

    def test_program_admission_is_exact_and_pre_io(self):
        with patch.object(runner, "HASHES", {"generic": DIGEST}):
            runner.validate_image(IMAGE, DIGEST, True, True)
            for rf, gaps in ((False, True), (True, False), (1, True), (True, 1), (None, True)):
                fake = Synthetic()
                with self.assertRaises(ValueError):
                    runner.exercise(fake, IMAGE, DIGEST, None, rf, gaps)
                self.assertFalse(fake.calls)
            for field, value in (("board", "unknown"), ("physical_hex", HEX + b"\n"),
                                 ("physical_hex", bytearray(HEX)),
                                 ("program", PROGRAM[:-1]), ("program", bytes(len(PROGRAM))),
                                 ("status", IMAGE.status + 1), ("checkpoint_sp", 0x58),
                                 ("wait", IMAGE.wait + 1), ("populated", 1)):
                with self.subTest(field=field), self.assertRaises(ValueError):
                    runner.validate_image(replace(IMAGE, **{field: value}), DIGEST, True, True)

    def test_one_run_and_receiver_hook_only_after_verified_admission(self):
        for outcome, name in ((1, "LOCAL_SENT"), (2, "CCA_BUSY")):
            fake = Synthetic(outcome=outcome, busy=2)
            seen = []

            def receiver():
                self.assertEqual(fake.cursor, 3)
                self.assertEqual(fake.registers.pc, IMAGE.wait)
                self.assertEqual(fake.calls.count("resume"), 4)
                seen.append(True)

            result = self.run_fake(fake, before_rf=receiver)
            self.assertEqual(result["outcome"], name)
            self.assertFalse(result["independent_reception_proved"])
            self.assertEqual(fake.calls.count("reset"), 1)
            self.assertEqual(fake.calls.count("resume"), 5)
            self.assertEqual(seen, [True])
        fake = Synthetic()
        result = self.run_fake(fake, admit_only=True, before_rf=lambda: self.fail("RF hook during admission-only"))
        self.assertEqual(result["outcome"], "ADMITTED")
        self.assertEqual(fake.calls.count("resume"), 4)

    def test_every_operator_transport_boundary_stops_without_retry(self):
        normal = Synthetic()
        self.run_fake(normal)
        for index in range(1, len(normal.calls) + 1):
            fake = Synthetic(fail_at=index)
            with self.subTest(index=index), self.assertRaises(OSError):
                self.run_fake(fake)
            self.assertEqual(len(fake.calls), index)

    def test_physical_common_banks_and_gaps_all_precede_first_resume(self):
        for address in (0, 0x79F8, 0x8000, 0xCEB4, 0x10000, len(PROGRAM) - 1):
            fake = Synthetic(bad_code=address)
            with self.subTest(address=address), self.assertRaisesRegex(ValueError, "physical CODE/erased gap"):
                self.run_fake(fake)
            self.assertNotIn("resume", fake.calls)
            self.assertEqual(fake.calls.count("reset"), 1)

    def test_admission_fault_and_banker_evidence_are_retained_before_rejection(self):
        for kwargs, message, resumes in (({"bad_admission": True}, "admission/private", 4),
                                         ({"bad_end": True}, "retirement", 5),
                                         ({"fault": True}, "MAC FAULT", 5),
                                         ({"banker": True}, "banker fail-stop", 5)):
            fake = Synthetic(**kwargs)
            with tempfile.TemporaryFile(mode="w+") as capture:
                with self.subTest(kwargs=kwargs), self.assertRaisesRegex(ValueError, message):
                    self.run_fake(fake, capture)
                capture.seek(0)
                records = [json.loads(line) for line in capture]
                self.assertEqual(records[-1]["event"], "checkpoint")
                self.assertIn("prefix_hex", records[-1])
                if kwargs.get("banker"):
                    self.assertEqual(records[-1]["banker_hex"], "0204")
                    self.assertEqual(records[-1]["dps"], 1)
            self.assertEqual(fake.calls.count("resume"), resumes)

    def test_capture_and_receiver_failures_never_resume_the_rf_run(self):
        for record in range(1, 8):
            fake = Synthetic()
            with patch.object(runner, "save", side_effect=[None] * (record - 1) + [OSError("capture failed")]):
                with self.assertRaisesRegex(OSError, "capture failed"):
                    self.run_fake(fake)
            self.assertLessEqual(fake.calls.count("resume"), 4 if record < 7 else 5)
        fake = Synthetic()
        with self.assertRaisesRegex(OSError, "receiver failed"):
            self.run_fake(fake, before_rf=lambda: (_ for _ in ()).throw(OSError("receiver failed")))
        self.assertEqual(fake.calls.count("resume"), 4)

    def test_physical_and_rf_deadlines_are_separate_and_finite(self):
        fake = Synthetic()
        with patch.object(runner.time, "monotonic", side_effect=[0, 301]), \
             tempfile.TemporaryFile(mode="w+") as capture:
            with self.assertRaisesRegex(ValueError, "preflight deadline"):
                runner.verify_physical_code(fake, IMAGE, capture)
        self.assertFalse(fake.calls)
        now = [0]
        fake = Synthetic()
        with patch.object(runner.time, "monotonic", side_effect=lambda: now[0]):
            with self.assertRaisesRegex(ValueError, "experiment deadline"):
                self.run_fake(fake, before_rf=lambda: now.__setitem__(0, 61))
        self.assertEqual(fake.calls.count("resume"), 4)

    def test_all_four_checkpoint_addresses_and_bad_status(self):
        class Stopped:
            @contextmanager
            def _target_operation(self):
                yield None

            def _exchange_byte(self, command, deadline):
                return 0x2A

            _check_status = staticmethod(runner.Debugger._check_status)

            def _pc(self, deadline):
                return self.pc

        target = Stopped()
        stops = (IMAGE.wait, IMAGE.end, IMAGE.fault, IMAGE.banker)
        for pc in stops:
            target.pc = pc
            self.assertEqual(runner.wait_checkpoint(target, stops), pc)
        target.pc = 0x1234
        with self.assertRaisesRegex(ValueError, "Unexpected MAC breakpoint"):
            runner.wait_checkpoint(target, stops)

    def test_cli_permissions_artifacts_and_cleanup_precede_success(self):
        flags = ["--allow-target-reset", "--allow-cpu-control", "--allow-memory-access",
                 "--allow-memory-write", "--allow-breakpoints", "--confirm-rf-including-autoack",
                 "--confirm-erased-gaps"]
        args = ["--bus", "1", "--address", "2", "--board", "generic", "--output", "absent-mac-smoke",
                "--sha256", DIGEST, "--capture", "/absent-private-mac/smoke.jsonl"]
        with patch.object(runner.PyUsbBackend, "load") as usb, redirect_stderr(io.StringIO()):
            for missing in flags:
                self.assertEqual(runner.main(args + [f for f in flags if f != missing]), 1)
            self.assertEqual(runner.main(args + flags), 1)
            usb.assert_not_called()
        with tempfile.TemporaryFile(mode="w+") as capture, \
             patch.object(runner, "load_image", return_value=IMAGE), \
             patch.object(runner, "HASHES", {"generic": DIGEST}), \
             patch.object(runner, "private_capture", return_value=capture), \
             patch.object(runner.PyUsbBackend, "load"), patch.object(runner, "Debugger") as cls, \
             patch.object(runner, "exercise", return_value={"outcome": "LOCAL_SENT"}), \
             redirect_stdout(io.StringIO()) as output, redirect_stderr(io.StringIO()):
            cls.return_value.__exit__.side_effect = OSError("synthetic cleanup failure")
            self.assertEqual(runner.main(args + flags), 1)
            self.assertEqual(output.getvalue(), "")
            self.assertNotIn("allow_dma_enable", cls.call_args.kwargs)


if __name__ == "__main__":
    unittest.main()
