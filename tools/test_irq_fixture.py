# SPDX-License-Identifier: BSD-3-Clause
"""Original byte records and manual-runner synthetic backends; no USB devices."""

from contextlib import redirect_stdout, redirect_stderr
from dataclasses import replace
import hashlib
from io import StringIO
import json
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import check_irq_hardware as runner
import debug_image
from cc_debugger import Access, DebuggerError, TransportError
from irq_fixture import OFFSETS, FIELDS, decode_irq_fixture
from test_timebase_hardware import FixtureDebugger
from verify_firmware import IRQ_CHECKPOINTS, IRQ_RESTORE_BYTES


BEFORE, ARMED, PENDING, INNER, READY, FAULT = range(0x100, 0x10c, 2)
ENTRY, RETI, RESTORE, CALL = 0x180, 0x1b4, 0x240, 0x200
PROGRAM = bytearray(bytes(range(256)) * 3)
PROGRAM[0x4b:0x4e] = b"\x02" + ENTRY.to_bytes(2, "big")
PROGRAM[RETI] = 0x32
PROGRAM[RESTORE:RESTORE + 23] = IRQ_RESTORE_BYTES
PROGRAM[CALL:CALL + 3] = b"\x12" + RESTORE.to_bytes(2, "big")
PROGRAM = bytes(PROGRAM)


def irq_record(phase=3, stage=5, cycle=1, **changes):
    values = dict(version=1, size=64, phase=phase, reason=0, stage=stage, completed=cycle & 255,
                  isr_count=cycle & 255, isr_before=(cycle - 1) & 255,
                  outer_token=1, invalid_result=1, timeout=1024, poll_limit=4096,
                  pending_polls=2, pending_elapsed=4, delivery_polls=1, delivery_elapsed=1,
                  isr_source=0x20, initial_command=0xc9, initial_status=0xc9, initial_sleep=4,
                  initial_timif=0x40, timif=0x40, command=0xc9, status=0xc9, sleep=4, counter=0x9669)
    if phase == 1:
        values.update(stage=0, completed=0, isr_count=0, isr_before=0, outer_token=0, invalid_result=0,
                      pending_polls=0, pending_elapsed=0, delivery_polls=0, delivery_elapsed=0,
                      isr_source=0, counter=0)
    if phase == 2:
        values.update(completed=(cycle - 1) & 255, isr_count=(cycle - 1) & 255,
                      outer_result=255, source=0x20, cpu=2, ien1=2, delivery_polls=0, delivery_elapsed=0)
    values.update(changes)
    data = bytearray(64)
    data[:4], data[62:] = b"M2IQ", b"\x69\x96"
    for name, size in FIELDS:
        if name in values:
            data[OFFSETS[name]:OFFSETS[name] + size] = values[name].to_bytes(size, "little")
    return bytes(data)


def image_fixture():
    symbols = dict(zip(IRQ_CHECKPOINTS, (BEFORE, ARMED, PENDING, INNER, READY, FAULT)), _irq_fixture_state=0x40)
    return SimpleNamespace(
        image_name="irq_fixture", board="generic", sha256=hashlib.sha256(PROGRAM).hexdigest(),
        metrics={"image_extent_bytes": len(PROGRAM), "iram_stack_start": 0x21},
        symbol=lambda name: SimpleNamespace(address=symbols[name]),
        irq_proof={"isr_size": 53, "isr_entry": ENTRY, "isr_reti": RETI, "restore": RESTORE,
                   "restore_call": CALL, "restore_return": CALL + 3,
                   "resume_addresses": [RESTORE + 9, RESTORE + 12, CALL + 3],
                   "start_address": 0x80, "deadline_address": 0x88},
    )


class IrqDebugger(FixtureDebugger):
    def __init__(self):
        super().__init__()
        self.registers = replace(self.registers, dps=0)
        self.breakpoints = {}
        self.position = "reset"
        self.deadline = SimpleNamespace(remaining_ms=lambda: 10_000)
        self.bad_frame = self.bad_context = self.bad_ack = self.bad_reti = False
        self.missed_timeout = False

    def set_breakpoint(self, slot, address, enabled=True):
        self.event("breakpoint", slot, address, enabled)
        self.breakpoints[slot] = address if enabled else None

    def read_code(self, address, length):
        self.event("code", address, length)
        return b"\xff" * length if self.corrupt_code else PROGRAM[address:address + length]

    def resume(self):
        self.event("resume")
        if self.position == "reset":
            self.position, self.pc = "before", BEFORE
        elif self.position in ("before", "ready"):
            if ARMED in self.breakpoints.values():
                self.position, self.pc = "armed", ARMED
            else:
                self.position, self.pc = "pending", PENDING
        elif self.position == "armed":
            self.position, self.pc = ("pending", PENDING) if self.missed_timeout else ("fault", FAULT)
        elif self.position == "pending":
            self.position, self.pc = "inner", INNER
        elif self.position == "inner":
            self.position, self.pc = "entry", ENTRY
            self.registers = replace(self.registers, sp=0x26, a=1, dptr0=0x5601)
        elif self.position == "entry":
            self.position, self.pc = "reti", RETI
            if self.bad_context:
                self.registers = replace(self.registers, b=self.registers.b ^ 1)
        else:
            assert self.position == "returned"
            self.position, self.pc = "ready", READY
            self.cycle += 1
            self.registers = replace(self.registers, sp=0x22)
        if self.bad_pc is not None:
            self.pc = self.bad_pc
        if self.fault:
            self.position, self.pc = "fault", FAULT

    def read_xdata(self, address, length):
        self.event("xdata", address, length)
        if address in (0x80, 0x88):
            assert length == 4 and self.position == "armed"
            data = (100 if address == 0x80 else 1124).to_bytes(4, "little")
        elif address == 0x40:
            assert length == 64
            if self.position == "before":
                data = irq_record(phase=1)
            elif self.position == "fault":
                data = irq_record(phase=4, stage=1, cycle=0, reason=6, pending_polls=1,
                                  pending_elapsed=2000, isr_count=0, control=0)
            elif self.position == "armed":
                data = irq_record(phase=2, stage=1, cycle=1, source=0, cpu=0, pending_polls=0, counter=0)
            elif self.position in ("pending", "inner", "entry", "reti"):
                stage = {"pending": 2, "inner": 3, "entry": 4, "reti": 4}[self.position]
                extra = {"isr_count": (self.cycle + 1) & 255,
                         "isr_source": 0 if self.bad_ack else 0x20} if self.position == "reti" else {}
                data = irq_record(phase=2, stage=stage, cycle=self.cycle + 1, **extra)
            else:
                data = irq_record(cycle=0 if self.stale_cycle else self.cycle)
            if self.corrupt_record:
                data = self.corrupt_record(data)
        elif address == 0x1e00:
            assert length == 32
            data = b"M0CC\x01\x20\x02\0" + bytes([self.cycle & 255]) + b"\0" * 15 + b"\xc9\xc9" + b"\0" * 6
            if self.corrupt_boot:
                data = self.corrupt_boot(data)
        else:
            assert address == 0x1f00 and length == self.registers.sp + 1
            data = b"\0" * (length - 4) + (CALL + 3).to_bytes(2, "little")
            data += (0x300 if self.bad_frame else RESTORE + 9).to_bytes(2, "little")
        if self.corrupt_cpu:
            self.registers = replace(self.registers, b=self.registers.b ^ 1)
        return data

    def step(self):
        if self.pc != RETI:
            return super().step()
        self.event("step")
        self.pc = RESTORE + (12 if self.bad_reti else 9)
        self.registers = replace(self.registers, sp=self.registers.sp - 2)
        self.position = "returned"
        return SimpleNamespace(accumulator=self.registers.a)


class IrqFixtureTests(unittest.TestCase):
    def test_byte_records_bounds_and_wrap(self):
        for cycle in (1, 255, 256, 257):
            self.assertEqual(decode_irq_fixture(irq_record(cycle=cycle))["completed"], cycle & 255)
        for phase, stage in ((1, 0), (2, 2), (2, 3), (2, 4)):
            self.assertEqual(decode_irq_fixture(irq_record(phase=phase, stage=stage))["phase"], phase)
        for field, value in (("phase", 0), ("stage", 6), ("reason", 1), ("size", 63),
                             ("pending_polls", 4097), ("delivery_polls", 0), ("timeout", 1025),
                             ("isr_count", 2), ("isr_cpu", 2), ("inner_result", 1), ("outer_result", 1),
                             ("source", 1), ("control", 1), ("ien0", 0x80), ("helper_status", 2),
                             ("pending_elapsed", 1024), ("initial_timif", 0)):
            with self.subTest(field=field), self.assertRaises(ValueError):
                decode_irq_fixture(irq_record(**{field: value}))
        for offset in (0, 4, 58, 61, 62, 63):
            changed = bytearray(irq_record())
            changed[offset] ^= 1
            with self.assertRaises(ValueError):
                decode_irq_fixture(bytes(changed))
        for value in (b"", irq_record()[:-1], irq_record() + b"\0", bytearray(irq_record())):
            with self.assertRaises(ValueError):
                decode_irq_fixture(value)

    def test_real_symbol_bit_and_sfr_spaces_are_distinct(self):
        text = " 000000AF _irq_ea irq\n 000000AF _IRQ_T1STAT irq_fixture_state\nC: 00000100 _main main\n"
        debug = "S:G$irq_ea$0_0$0({1}SX:U),J,0,0\nS:G$IRQ_T1STAT$0_0$0({1}SC:U),I,0,0\n"
        symbols = debug_image.linked_symbols(text, debug, {0x100: 0x22})
        self.assertEqual(symbols["_irq_ea"].space, "SBIT")
        self.assertEqual(symbols["_IRQ_T1STAT"].space, "SFR")

    def test_manual_full_code_context_reti_repeat_and_wrap(self):
        debugger = IrqDebugger()
        result = runner.exercise(debugger, image_fixture(), PROGRAM, 257)
        self.assertEqual(result["verified_cycles"], 257)
        self.assertEqual(result["observations"][255]["record"]["completed"], 0)
        self.assertEqual(result["observations"][-1]["record"]["completed"], 1)
        self.assertTrue(all(item["interrupted_restore"] for item in result["observations"]))
        first_resume = next(i for i, event in enumerate(debugger.events) if event[0] == "resume")
        self.assertEqual([event for event in debugger.events[:first_resume] if event[0] == "code"],
                         [("code", offset, 128) for offset in range(0, len(PROGRAM), 128)])
        self.assertEqual(debugger.pc, READY)
        self.assertEqual(sum(event[0] == "attach-reset" for event in debugger.events), 1)

    def test_induced_timeout_uses_returned_failure_not_host_delay(self):
        with patch("check_clock_hardware.time.sleep"):
            debugger = IrqDebugger()
            result = runner.exercise(debugger, image_fixture(), PROGRAM, 1, True)
            self.assertTrue(result["induced_timeout"])
            self.assertEqual(result["verified_cycles"], 0)
            self.assertEqual(debugger.pc, FAULT)
            self.assertEqual(result["observations"][0]["reason"], 6)
            debugger = IrqDebugger()
            debugger.missed_timeout = True
            with self.assertRaisesRegex(ValueError, "Unexpected"):
                runner.exercise(debugger, image_fixture(), PROGRAM, 1, True)
            self.assertEqual(debugger.pc, PENDING)
            self.assertIn(PENDING, debugger.breakpoints.values())

    def test_immutable_initial_irq_observations_are_compared_across_stops(self):
        baseline = decode_irq_fixture(irq_record(phase=1))
        current = decode_irq_fixture(irq_record())
        boot = b"M0CC\x01\x20\x02\0\x01" + b"\0" * 23
        for key in ("initial_ip0", "initial_ip1", "initial_sleep", "initial_timif", "initial_ircon"):
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, "initial observations"):
                runner.invariant(dict(current, **{key: baseline[key] ^ 1}), boot, boot, 1, baseline)

    def test_every_operation_failure_stops_without_continuation(self):
        for negative in (False, True):
            with patch("check_clock_hardware.time.sleep"):
                baseline = IrqDebugger()
                runner.exercise(baseline, image_fixture(), PROGRAM, 1 if negative else 2, negative)
                for boundary in range(1, len(baseline.events) + 1):
                    debugger = IrqDebugger()
                    debugger.fail_at = boundary
                    with self.subTest(negative=negative, boundary=boundary), self.assertRaises(TransportError):
                        runner.exercise(debugger, image_fixture(), PROGRAM, 1 if negative else 2, negative)
                    self.assertEqual(debugger.events, baseline.events[:boundary])

    def test_fault_bad_code_context_frame_ack_and_reti_are_errors(self):
        for name in ("fault", "corrupt_code", "corrupt_cpu", "bad_step", "stale_cycle",
                     "bad_frame", "bad_context", "bad_ack", "bad_reti"):
            debugger = IrqDebugger()
            setattr(debugger, name, True)
            with self.subTest(name=name), self.assertRaises(ValueError):
                runner.exercise(debugger, image_fixture(), PROGRAM, 2)
        for name in ("corrupt_record", "corrupt_boot"):
            debugger = IrqDebugger()
            setattr(debugger, name, lambda data: b"?" + data[1:])
            with self.assertRaises(ValueError):
                runner.exercise(debugger, image_fixture(), PROGRAM, 2)

    def invoke(self, extra):
        out, err = StringIO(), StringIO()
        args = ["--board", "generic", "--output", "unused", "--bus", "1", "--address", "2"] + extra
        with redirect_stdout(out), redirect_stderr(err):
            result = runner.main(args)
        return result, out.getvalue(), err.getvalue()

    def test_cli_denies_before_backend_load(self):
        for extra in ([], ["--confirm-irq-test", "--cycles", "1"], ["--confirm-irq-test", "--cycles", "258"],
                      ["--confirm-irq-test", "--induce-timeout", "--cycles", "2"],
                      ["--confirm-irq-test", "--bus", "0"]):
            with patch.object(runner.PyUsbBackend, "load") as load:
                result, output, error = self.invoke(extra)
                self.assertEqual(result, 1)
                self.assertEqual(output, "")
                self.assertTrue(error)
                load.assert_not_called()
        with patch.object(runner, "DebugImage", return_value=image_fixture()), \
                patch.object(runner.Path, "read_bytes", return_value=b"bad"), \
                patch.object(runner.PyUsbBackend, "load") as load:
            self.assertEqual(self.invoke(["--confirm-irq-test"])[0], 1)
            load.assert_not_called()

    def test_cli_minimal_permissions_and_cleanup_before_success(self):
        for cleanup in (None, DebuggerError("cleanup failed")):
            with patch.object(runner, "DebugImage", return_value=image_fixture()), \
                    patch.object(runner.Path, "read_bytes", return_value=PROGRAM), \
                    patch.object(runner.PyUsbBackend, "load") as load, \
                    patch.object(runner, "Debugger") as constructor, \
                    patch.object(runner, "exercise", return_value={"evidence": "synthetic-test"}):
                constructor.return_value.__exit__.return_value = False
                constructor.return_value.__exit__.side_effect = cleanup
                result, output, error = self.invoke(["--confirm-irq-test"])
                constructor.assert_called_once_with(
                    load.return_value, Access.RESET_DEBUG_SESSION, timeout_ms=10_000,
                    allow_cpu_control=True, allow_target_reset=True, allow_memory_access=True,
                    allow_breakpoints=True)
                self.assertEqual(result, int(cleanup is not None))
                if cleanup:
                    self.assertEqual(output, "")
                    self.assertIn("cleanup failed", error)
                else:
                    self.assertEqual(json.loads(output), {"evidence": "synthetic-test"})
