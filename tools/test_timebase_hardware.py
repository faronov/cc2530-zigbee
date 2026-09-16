# SPDX-License-Identifier: BSD-3-Clause
"""Manual timebase acceptance orchestration tests; no physical USB access."""

from contextlib import contextmanager, redirect_stdout, redirect_stderr
from dataclasses import replace
import hashlib
from io import StringIO
import json
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import check_timebase_hardware as runner
from cc2530_debug import READ_STATUS
from cc_debugger import Access, AdapterState, Debugger, DebuggerError, RegisterSnapshot, State, TransportError, UsbAddress
from test_m1_transport import Clock, FakeBackend
from test_timebase_fixture import timebase_record
from verify_firmware import TIMEBASE_CHECKPOINTS


PROGRAM = bytes(range(256)) + b"original synthetic CODE fixture"
BEFORE, READY, FAULT = 0x100, 0x102, 0x104


def image_fixture():
    symbols = dict(zip(TIMEBASE_CHECKPOINTS, (BEFORE, READY, FAULT)), _timebase_fixture_state=0x40)
    return SimpleNamespace(
        image_name="timebase_fixture", board="generic", sha256=hashlib.sha256(PROGRAM).hexdigest(),
        metrics={"image_extent_bytes": len(PROGRAM), "iram_stack_start": 0x21},
        symbol=lambda name: SimpleNamespace(address=symbols[name]),
    )


class FixtureDebugger:
    """High-level synthetic stops/records; not execution of firmware or USB."""

    def __init__(self):
        self.pc, self.cycle = 0, 0
        self.events = []
        self.fail_at = None
        self.fault = False
        self.stale_cycle = False
        self.bad_pc = None
        self.corrupt_code = False
        self.corrupt_record = None
        self.corrupt_boot = None
        self.corrupt_cpu = False
        self.bad_step = False
        self.deadline = object()
        self.statuses = []
        self.registers = RegisterSnapshot(pc=0, bank=0, a=0x69, psw=0x80, b=0x96, sp=0x22,
                                         dptr0=0x5678, dptr1=0x1234, dps=1, mpage=0x5a,
                                         r=tuple(range(8)))

    def event(self, name, *args):
        self.events.append((name, *args))
        if len(self.events) == self.fail_at:
            raise TransportError("synthetic operation failure")

    def read_adapter_state(self):
        self.event("adapter")
        return AdapterState(0x2530, 0x28, 1)

    def attach_reset(self):
        self.event("attach-reset")
        self.pc = 0

    def read_pc(self):
        self.event("pc")
        return self.pc

    def read_debug_config(self):
        self.event("config")
        return 0x26

    def read_code(self, address, length):
        self.event("code", address, length)
        data = PROGRAM[address:address + length]
        return b"\xff" * length if self.corrupt_code else data

    def set_breakpoint(self, slot, address, enabled=True):
        self.event("breakpoint", slot, address, enabled)

    def resume(self):
        self.event("resume")
        if self.pc == 0:
            self.pc = BEFORE
        else:
            self.pc = FAULT if self.fault else READY
            self.cycle += 1
        if self.bad_pc is not None:
            self.pc = self.bad_pc

    @contextmanager
    def _target_operation(self):
        self.event("wait")
        yield self.deadline

    def _exchange_byte(self, command, deadline):
        self.event("status")
        assert command == READ_STATUS and deadline is self.deadline
        if self.statuses:
            result = self.statuses.pop(0)
            if isinstance(result, Exception):
                raise result
            return result
        return 0x2b

    _check_status = staticmethod(Debugger._check_status)

    def _pc(self, deadline):
        assert deadline is self.deadline
        return self.read_pc()

    def read_registers(self):
        self.event("registers")
        return replace(self.registers, pc=self.pc)

    def read_xdata(self, address, length):
        self.event("xdata", address, length)
        assert length == 32
        if address == 0x40:
            if self.pc == BEFORE:
                data = timebase_record(phase=1)
            elif self.fault:
                data = timebase_record(phase=4, start=0, end=0, polls=1024, cycles=0, reason=4)
            else:
                data = timebase_record(cycles=0 if self.stale_cycle else self.cycle & 255)
            if self.pc != BEFORE and self.corrupt_record:
                data = self.corrupt_record(data)
        else:
            assert address == 0x1e00
            data = b"M0CC\x01\x20\x02\x00" + bytes([self.cycle & 255]) + b"\0" * 15 + b"\xc9\xc9" + b"\0" * 6
            if self.pc != BEFORE and self.corrupt_boot:
                data = self.corrupt_boot(data)
        if self.corrupt_cpu:
            self.registers = replace(self.registers, b=(self.registers.b + 1) & 255)
        return data

    def step(self):
        self.event("step")
        self.pc += 2 if self.bad_step else 1
        return SimpleNamespace(accumulator=self.registers.a)


class TimebaseHardwareTests(unittest.TestCase):
    arguments = ["--bus", "1", "--address", "2", "--board", "generic", "--output", "unused"]

    def invoke(self, arguments):
        output, error = StringIO(), StringIO()
        with redirect_stdout(output), redirect_stderr(error):
            result = runner.main(arguments)
        return result, output.getvalue(), error.getvalue()

    def test_no_hardware_loading_without_authorization_valid_inputs_and_matching_artifacts(self):
        for extra in ([], ["--confirm-timebase-test", "--cycles", "0"],
                      ["--confirm-timebase-test", "--cycles", "258"],
                      ["--confirm-timebase-test", "--bus", "0"],
                      ["--confirm-timebase-test", "--address", "128"]):
            with self.subTest(extra=extra), patch.object(runner.PyUsbBackend, "load") as load:
                result, output, error = self.invoke(self.arguments + extra)
                self.assertEqual(result, 1)
                self.assertEqual(output, "")
                self.assertTrue(error)
                load.assert_not_called()
        for program in (PROGRAM[:-1], PROGRAM + b"\0", b""):
            with self.subTest(program=len(program)), patch.object(runner, "DebugImage", return_value=image_fixture()), \
                    patch.object(runner.Path, "read_bytes", return_value=program), \
                    patch.object(runner.PyUsbBackend, "load") as load:
                result, output, error = self.invoke(self.arguments + ["--confirm-timebase-test"])
                self.assertEqual(result, 1)
                self.assertEqual(output, "")
                load.assert_not_called()
        with patch.object(runner, "DebugImage", side_effect=ValueError("artifact rejected")), \
                patch.object(runner.PyUsbBackend, "load") as load:
            result, output, error = self.invoke(self.arguments + ["--confirm-timebase-test"])
            self.assertEqual(result, 1)
            self.assertEqual(output, "")
            self.assertIn("artifact rejected", error)
            load.assert_not_called()

    def test_minimal_permissions_and_success_only_after_cleanup(self):
        for cleanup in (None, DebuggerError("release failed")):
            with self.subTest(cleanup=cleanup), patch.object(runner, "DebugImage", return_value=image_fixture()), \
                    patch.object(runner.Path, "read_bytes", return_value=PROGRAM), \
                    patch.object(runner.PyUsbBackend, "load") as load, \
                    patch.object(runner, "Debugger") as constructor, \
                    patch.object(runner, "exercise", return_value={"evidence": "synthetic-test"}):
                constructor.return_value.__exit__.return_value = False
                constructor.return_value.__exit__.side_effect = cleanup
                result, output, error = self.invoke(self.arguments + ["--confirm-timebase-test"])
                constructor.assert_called_once_with(
                    load.return_value, Access.RESET_DEBUG_SESSION, timeout_ms=10_000,
                    allow_cpu_control=True, allow_target_reset=True, allow_memory_access=True,
                    allow_breakpoints=True)
                if cleanup is None:
                    self.assertEqual(result, 0)
                    self.assertEqual(json.loads(output), {"evidence": "synthetic-test"})
                else:
                    self.assertEqual(result, 1)
                    self.assertEqual(output, "")
                    self.assertIn("release failed", error)

    def test_full_code_before_resume_cycles_wrap_cpu_and_final_halt(self):
        debugger = FixtureDebugger()
        debugger.registers = replace(debugger.registers, bank=3)
        result = runner.exercise(debugger, image_fixture(), PROGRAM, 257)
        self.assertEqual(result["verified_code_bytes"], len(PROGRAM))
        self.assertEqual(result["verified_cycles"], 257)
        self.assertEqual(result["cycles"][255]["completed_cycles"], 0)
        self.assertEqual(result["cycles"][-1]["completed_cycles"], 1)
        self.assertEqual(debugger.pc, READY)
        first_resume = next(index for index, event in enumerate(debugger.events) if event[0] == "resume")
        reads = [event for event in debugger.events if event[0] == "code"]
        self.assertEqual(reads, [("code", offset, min(128, len(PROGRAM) - offset))
                                 for offset in range(0, len(PROGRAM), 128)])
        self.assertTrue(all(debugger.events.index(event) < first_resume for event in reads))
        self.assertNotIn(("step",), debugger.events[-1:])
        self.assertEqual(sum(event[0] == "attach-reset" for event in debugger.events), 1)

    def test_every_operation_failure_stops_without_retry_resume_or_recovery(self):
        baseline = FixtureDebugger()
        runner.exercise(baseline, image_fixture(), PROGRAM, 2)
        for boundary in range(1, len(baseline.events) + 1):
            debugger = FixtureDebugger()
            debugger.fail_at = boundary
            with self.subTest(boundary=boundary), self.assertRaisesRegex(TransportError, "synthetic"):
                runner.exercise(debugger, image_fixture(), PROGRAM, 2)
            self.assertEqual(debugger.events, baseline.events[:boundary])

    def test_code_mismatch_never_resumes(self):
        debugger = FixtureDebugger()
        debugger.corrupt_code = True
        with self.assertRaisesRegex(ValueError, "Physical timebase CODE"):
            runner.exercise(debugger, image_fixture(), PROGRAM, 1)
        self.assertFalse(any(event[0] in ("resume", "step", "breakpoint") for event in debugger.events))

    def test_fault_unexpected_pc_bad_cycle_cpu_and_nop_are_terminal(self):
        for name, value, message in (("fault", True, "FAULT"), ("bad_pc", 0x200, "Unexpected"),
                                     ("stale_cycle", True, "progression"),
                                     ("corrupt_cpu", True, "CPU state"), ("bad_step", True, "NOP")):
            debugger = FixtureDebugger()
            setattr(debugger, name, value)
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, message):
                runner.exercise(debugger, image_fixture(), PROGRAM, 2)
            self.assertNotEqual(debugger.events[-1][0], "resume")

    def test_record_and_boot_corruption_never_produce_success(self):
        for offset, value in ((6, 2), (7, 1), (9, 2), (19, 127), (21, 0x80), (22, 0),
                              (23, 5), (28, 1), (30, 0)):
            debugger = FixtureDebugger()
            debugger.corrupt_record = lambda data: data[:offset] + bytes([value]) + data[offset + 1:]
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                runner.exercise(debugger, image_fixture(), PROGRAM, 1)
        for offset in (7, 8, 10, 26, 29):
            debugger = FixtureDebugger()
            debugger.corrupt_boot = lambda data: data[:offset] + bytes([data[offset] ^ 1]) + data[offset + 1:]
            with self.subTest(boot_offset=offset), self.assertRaises(ValueError):
                runner.exercise(debugger, image_fixture(), PROGRAM, 1)

    def test_wait_uses_one_deadline_and_rejects_invalid_halt_and_timeout(self):
        debugger = FixtureDebugger()
        debugger.pc = READY
        debugger.statuses = [0x03, 0x03, 0x2b]
        self.assertEqual(runner.wait_checkpoint(debugger, READY, FAULT), READY)
        self.assertEqual(debugger.events.count(("wait",)), 1)
        for status in (0x23, 0x26, 0x32, TransportError("deadline exceeded")):
            debugger = FixtureDebugger()
            debugger.statuses = [0x03, status]
            with self.subTest(status=status), self.assertRaises((ValueError, TransportError)):
                runner.wait_checkpoint(debugger, READY, FAULT)
            self.assertEqual(debugger.events, [("wait",), ("status",), ("status",)])

    def test_wrong_image_or_program_is_rejected_before_any_target_operation(self):
        for field, value in (("image_name", "debug_fixture"), ("sha256", "incorrect")):
            image = image_fixture()
            setattr(image, field, value)
            debugger = FixtureDebugger()
            with self.subTest(field=field), self.assertRaises(ValueError):
                runner.exercise(debugger, image, PROGRAM, 1)
            self.assertEqual(debugger.events, [])

    def test_real_guarded_wait_deadline_faults_and_denies_resume_without_more_io(self):
        clock = Clock()
        backend = FakeBackend(clock)
        backend.byte_reply = b"\x03"
        backend.delays["read"] = 1_000_000
        debugger = Debugger(backend, Access.EXISTING_DEBUG_SESSION, timeout_ms=5, clock=clock,
                            allow_cpu_control=True)
        debugger.open(UsbAddress(1, 2))
        with self.assertRaisesRegex(TransportError, "deadline"):
            runner.wait_checkpoint(debugger, READY, FAULT)
        self.assertEqual(debugger.state, State.FAULTED)
        self.assertEqual([event[-1] for event in backend.calls if event[0] == "write"], [5, 4, 3, 2, 1])
        calls = list(backend.calls)
        with self.assertRaises(DebuggerError):
            debugger.resume()
        self.assertEqual(backend.calls, calls)
        debugger.close()


if __name__ == "__main__":
    unittest.main()
