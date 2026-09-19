# SPDX-License-Identifier: BSD-3-Clause
"""Manual clock runner tests use synthetic devices only, never physical USB."""

from contextlib import redirect_stdout, redirect_stderr
from dataclasses import replace
import hashlib
from io import StringIO
import json
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import check_clock_hardware as runner
from cc_debugger import Access, Debugger, DebuggerError, State, TransportError, UsbAddress
from test_m1_transport import Clock, FakeBackend
from test_timebase_hardware import FixtureDebugger, BEFORE, READY, FAULT
from test_clock_fixture import clock_record
from verify_firmware import CLOCK_CHECKPOINTS


DEADLINE, CALL, WRITE = 0x180, 0x1a0, 0x1bd
POST, OBSERVE = WRITE + 2, WRITE + 8
SAMPLE = OBSERVE + 40
data = bytearray(bytes(range(256)) * 3)
data[DEADLINE - 3:DEADLINE + 1] = b"\x75\x82\0\x22"
data[CALL:CALL + 3] = b"\x12" + (DEADLINE - 147).to_bytes(2, "big")
data[WRITE:WRITE + 2] = b"\x8f\xc6"
data[POST], data[SAMPLE] = 0x8f, 0x12
PROGRAM = bytes(data)


def image_fixture():
    symbols = dict(zip(CLOCK_CHECKPOINTS, (BEFORE, READY, FAULT)), _clock_fixture_state=0x40)
    return SimpleNamespace(
        image_name="clock_fixture", board="generic", sha256=hashlib.sha256(PROGRAM).hexdigest(),
        metrics={"image_extent_bytes": len(PROGRAM), "iram_stack_start": 0x21},
        symbol=lambda name: SimpleNamespace(address=symbols[name]),
        clock_timeout_checkpoint={"address": DEADLINE, "function_start": DEADLINE - 147, "function_size": 148,
                                  "call_address": CALL, "return_address": CALL + 3,
                                  "command_write_address": WRITE, "deadline_address": 0x90,
                                  "post_request_address": POST, "poll_observe_address": OBSERVE,
                                  "poll_sample_address": SAMPLE, "command_address": 0x94,
                                  "source_seen_address": 0x98, "diagnostics_address": 0x100},
    )


class ClockDebugger(FixtureDebugger):
    def __init__(self):
        super().__init__()
        self.breakpoints = {}
        self.steps = 0
        self.held = False
        self.bad_return = self.bad_dpl = False
        self.bad_source = self.bad_evidence = False
        self.registers = replace(self.registers, dps=0, bank=3)
        self.deadline = SimpleNamespace(remaining_ms=lambda: self.event("remaining") or 1000)

    def read_code(self, address, length):
        self.event("code", address, length)
        return b"\xff" * length if self.corrupt_code else PROGRAM[address:address + length]

    def set_breakpoint(self, slot, address, enabled=True):
        self.event("breakpoint", slot, address, enabled)
        self.breakpoints[slot] = address if enabled else None

    def resume(self):
        self.event("resume")
        if self.pc == 0:
            self.pc = BEFORE
        elif self.breakpoints.get(3) in (DEADLINE, POST, SAMPLE) and self.steps == 1:
            self.pc = self.breakpoints[3]
        else:
            self.steps += 1
            self.pc = FAULT if self.fault or self.held else READY
        if self.bad_pc is not None:
            self.pc = self.bad_pc

    def read_registers(self):
        self.event("registers")
        return replace(self.registers, pc=self.pc, sp=0x50 if self.pc in (DEADLINE, POST, SAMPLE) else 0x22,
                       dptr0=(0x5601 if self.bad_dpl else 0x5600) if self.pc == DEADLINE else 0x5678)

    def read_xdata(self, address, length):
        self.event("xdata", address, length)
        if address == 0x1f4f:
            assert length == 2
            return (CALL + (4 if self.bad_return else 3)).to_bytes(2, "little")
        if address == 0x90:
            assert length == 4
            return (1124).to_bytes(4, "little")
        if address == 0x94:
            assert length == 1
            return b"\x88"
        if address == 0x98:
            assert length == 1
            return bytes((int(self.pc == SAMPLE and not self.bad_evidence),))
        if address == 0x100:
            assert length == 19 and self.pc == SAMPLE
            return b"\0" * 14 + bytes((0xc9, 0x88, 0x88, 0xc9 if self.bad_source else 0x88, 8))
        complete = self.steps - int(self.pc == FAULT)
        if address == 0x40:
            assert length == 56
            if self.pc == BEFORE:
                data = clock_record(phase=1, stage=0, steps=0)
            elif self.pc in (DEADLINE, POST, SAMPLE):
                data = clock_record(phase=2, stage=1, steps=1)
            elif self.pc == FAULT:
                data = clock_record(phase=4, stage=(self.steps - 1) % 3, steps=complete,
                                    result=3, rollback=0, reason=1)
            else:
                data = clock_record(stage=(self.steps - 1) % 3, steps=0 if self.stale_cycle else complete & 255)
            if self.pc != BEFORE and self.corrupt_record:
                data = self.corrupt_record(data)
        else:
            assert address == 0x1e00 and length == 32
            data = b"M0CC\x01\x20\x02\x00" + bytes([complete & 255]) + b"\0" * 15 + b"\xc9\xc9" + b"\0" * 6
            if self.pc != BEFORE and self.corrupt_boot:
                data = self.corrupt_boot(data)
        if self.corrupt_cpu:
            self.registers = replace(self.registers, b=(self.registers.b + 1) & 255)
        return data

    def hold(self, seconds):
        self.event("hold", seconds)
        assert self.pc in (DEADLINE, POST) and self.breakpoints.get(3) is None
        self.held = True


class ClockHardwareTests(unittest.TestCase):
    arguments = ["--bus", "1", "--address", "2", "--board", "generic", "--output", "unused"]

    def invoke(self, extra):
        output, error = StringIO(), StringIO()
        with redirect_stdout(output), redirect_stderr(error):
            result = runner.main(self.arguments + extra)
        return result, output.getvalue(), error.getvalue()

    def exercise(self, debugger, induced=False, cycles=1, late=False):
        with patch.object(runner.time, "sleep", side_effect=debugger.hold):
            return runner.exercise(debugger, image_fixture(), PROGRAM, cycles, induced, late)

    def test_normal_sequences_wrap_full_code_first_and_no_memory_write_permission(self):
        debugger = ClockDebugger()
        result = self.exercise(debugger, cycles=257)
        self.assertEqual(len(result["observations"]), 771)
        self.assertEqual(result["observations"][255]["completed_steps"], 0)
        self.assertEqual(result["observations"][256]["completed_steps"], 1)
        self.assertEqual(result["final_pc"], READY)
        first_resume = next(i for i, event in enumerate(debugger.events) if event[0] == "resume")
        reads = [event for event in debugger.events[:first_resume] if event[0] == "code"]
        self.assertEqual(reads, [("code", i, 128) for i in range(0, len(PROGRAM), 128)])
        self.assertFalse(any(event[0] == "hold" for event in debugger.events))

    def test_expected_timeout_requires_verified_context_disable_before_hold_and_terminal_fault(self):
        debugger = ClockDebugger()
        result = self.exercise(debugger, induced=True)
        self.assertTrue(result["induced_timeout"])
        self.assertEqual(result["completed_sequences"], 0)
        self.assertEqual(result["final_pc"], FAULT)
        self.assertEqual(result["observations"][-1]["clock_result"], 3)
        self.assertEqual(result["observations"][-1]["diagnostics"]["rollback_result"], 0)
        events = debugger.events
        hold = events.index(("hold", 0.25))
        self.assertLess(events.index(("xdata", 0x1f4f, 2)), hold)
        self.assertLess(events.index(("breakpoint", 3, DEADLINE, False)), hold)
        self.assertEqual(sum(event[0] == "attach-reset" for event in events), 1)
        self.assertEqual(debugger.pc, FAULT)

    def test_every_normal_and_negative_operation_failure_stops_at_that_boundary(self):
        for induced, late in ((False, False), (True, False), (False, True)):
            baseline = ClockDebugger()
            self.exercise(baseline, induced, late=late)
            for boundary in range(1, len(baseline.events) + 1):
                debugger = ClockDebugger()
                debugger.fail_at = boundary
                with self.subTest(induced=induced, late=late, boundary=boundary), self.assertRaises(TransportError):
                    self.exercise(debugger, induced, late=late)
                self.assertEqual(debugger.events, baseline.events[:boundary])

    def test_bad_return_abi_or_caller_never_holds_or_resumes_again(self):
        for field in ("bad_dpl", "bad_return"):
            debugger = ClockDebugger()
            setattr(debugger, field, True)
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.exercise(debugger, True)
            self.assertFalse(any(event[0] == "hold" for event in debugger.events))
            self.assertEqual(debugger.pc, DEADLINE)

    def test_code_mismatch_unexpected_pc_fault_cpu_and_counter_are_not_success(self):
        for field, value in (("corrupt_code", True), ("bad_pc", 0x700), ("fault", True),
                             ("corrupt_cpu", True), ("stale_cycle", True), ("bad_step", True)):
            debugger = ClockDebugger()
            setattr(debugger, field, value)
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.exercise(debugger)
            if field == "corrupt_code":
                self.assertFalse(any(event[0] == "resume" for event in debugger.events))

    def test_hold_alone_is_not_timeout_or_rollback_evidence(self):
        for mutate in (
            lambda data: data[:11] + b"\x04" + data[12:],
            lambda data: data[:18] + b"\x04" + data[19:],
            lambda data: data[:35] + b"\x04" + data[36:],
            lambda data: data[:40] + b"\x88" + data[41:],
            lambda data: data[:41] + b"\x89" + data[42:],
        ):
            debugger = ClockDebugger()
            debugger.corrupt_record = lambda data: mutate(data) if data[6] == 4 else data
            with self.assertRaises(ValueError):
                self.exercise(debugger, True)
            self.assertEqual(debugger.pc, FAULT)
        debugger = ClockDebugger()
        with patch.object(runner.time, "sleep", return_value=None), self.assertRaisesRegex(ValueError, "actual CLOCK_TIMEOUT"):
            runner.exercise(debugger, image_fixture(), PROGRAM, 1, True)
        self.assertEqual(debugger.pc, READY)

    def test_authorization_selection_cycles_and_artifacts_gate_usb_loading(self):
        for extra in ([], ["--confirm-clock-test", "--cycles", "0"],
                      ["--confirm-clock-test", "--cycles", "258"],
                      ["--confirm-clock-test", "--cycles", "2", "--induce-timeout"],
                      ["--confirm-clock-test", "--cycles", "2", "--induce-late-timeout"],
                      ["--confirm-clock-test", "--induce-timeout", "--induce-late-timeout"],
                      ["--confirm-clock-test", "--bus", "0"], ["--confirm-clock-test", "--address", "128"]):
            with self.subTest(extra=extra), patch.object(runner.PyUsbBackend, "load") as load:
                result, output, error = self.invoke(extra)
                self.assertEqual(result, 1)
                self.assertEqual(output, "")
                self.assertTrue(error)
                load.assert_not_called()
        for program in (b"", PROGRAM[:-1], PROGRAM + b"\0"):
            with patch.object(runner, "DebugImage", return_value=image_fixture()), \
                    patch.object(runner.Path, "read_bytes", return_value=program), \
                    patch.object(runner.PyUsbBackend, "load") as load:
                result, output, error = self.invoke(["--confirm-clock-test"])
                self.assertEqual((result, output), (1, ""))
                load.assert_not_called()
        image = image_fixture()
        image.clock_timeout_checkpoint["address"] -= 1
        debugger = ClockDebugger()
        with self.assertRaises(ValueError):
            runner.exercise(debugger, image, PROGRAM)
        self.assertEqual(debugger.events, [])

    def test_separate_late_source_mode_executes_request_before_hold_and_proves_c_observation(self):
        debugger = ClockDebugger()
        result = self.exercise(debugger, late=True)
        self.assertEqual(result["timeout_mode"], "late-confirmed-source")
        self.assertEqual(result["final_pc"], FAULT)
        events = debugger.events
        hold = events.index(("hold", 0.25))
        self.assertLess(events.index(("breakpoint", 3, POST, False)), hold)
        self.assertGreater(events.index(("xdata", 0x100, 19)), hold)
        self.assertLess(events.index(("breakpoint", 3, SAMPLE, False)),
                        max(i for i, event in enumerate(events) if event[0] == "resume"))
        for field in ("bad_source", "bad_evidence"):
            debugger = ClockDebugger()
            setattr(debugger, field, True)
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.exercise(debugger, late=True)
            self.assertEqual(debugger.pc, SAMPLE)
            self.assertNotIn(("breakpoint", 3, SAMPLE, False), debugger.events)

    def test_staged_instruction_guards_reject_old_emission_despite_matching_hash(self):
        for address, value in ((WRITE, 0x88), (POST, 0x88), (SAMPLE, 0)):
            program = bytearray(PROGRAM)
            program[address] = value
            image = image_fixture()
            image.sha256 = hashlib.sha256(program).hexdigest()
            debugger = ClockDebugger()
            with self.subTest(address=address), self.assertRaises(ValueError):
                runner.exercise(debugger, image, bytes(program))
            self.assertEqual(debugger.events, [])
        image = image_fixture()
        image.clock_timeout_checkpoint["poll_sample_address"] = OBSERVE + 124
        program = bytearray(PROGRAM)
        program[OBSERVE + 124] = 0x12
        image.sha256 = hashlib.sha256(program).hexdigest()
        debugger = ClockDebugger()
        with self.assertRaises(ValueError):
            runner.exercise(debugger, image, bytes(program))
        self.assertEqual(debugger.events, [])

    def test_uncertain_cancellation_never_becomes_hardware_rollback_acceptance(self):
        def unconfirmed(data):
            if data[6] != 4:
                return data
            changed = bytearray(data)
            changed[24:31] = b"\x02\x04\0\0\x56\x01\0"
            changed[35] = 9
            return bytes(changed)
        for late in (False, True):
            debugger = ClockDebugger()
            debugger.corrupt_record = unconfirmed
            with self.assertRaisesRegex(ValueError, "rollback=9"):
                self.exercise(debugger, induced=not late, late=late)
            self.assertEqual(debugger.pc, FAULT)

    def test_minimal_permissions_and_success_json_only_after_successful_cleanup(self):
        for cleanup in (None, DebuggerError("cleanup failed")):
            with patch.object(runner, "DebugImage", return_value=image_fixture()), \
                    patch.object(runner.Path, "read_bytes", return_value=PROGRAM), \
                    patch.object(runner.PyUsbBackend, "load") as load, patch.object(runner, "Debugger") as cls, \
                    patch.object(runner, "exercise", return_value={"evidence": "synthetic-test"}):
                cls.return_value.__exit__.return_value = False
                cls.return_value.__exit__.side_effect = cleanup
                result, output, error = self.invoke(["--confirm-clock-test"])
                cls.assert_called_once_with(load.return_value, Access.RESET_DEBUG_SESSION, timeout_ms=10_000,
                                           allow_cpu_control=True, allow_target_reset=True,
                                           allow_memory_access=True, allow_breakpoints=True)
                if cleanup is None:
                    self.assertEqual(result, 0)
                    self.assertEqual(json.loads(output), {"evidence": "synthetic-test"})
                else:
                    self.assertEqual((result, output), (1, ""))
                    self.assertIn("cleanup failed", error)

    def test_real_guarded_hold_has_one_deadline_and_oversleep_faults_without_resume(self):
        clock = Clock()
        backend = FakeBackend(clock)
        backend.byte_reply = b"\x2b"
        debugger = Debugger(backend, Access.EXISTING_DEBUG_SESSION, timeout_ms=5, clock=clock)
        debugger.open(UsbAddress(1, 2))
        def oversleep(seconds):
            clock.now += 10_000_000
        with patch.object(runner.time, "sleep", side_effect=oversleep), self.assertRaises(TransportError):
            runner.hold_halted(debugger)
        self.assertEqual(debugger.state, State.FAULTED)
        calls = list(backend.calls)
        with self.assertRaises(DebuggerError):
            debugger.resume()
        self.assertEqual(backend.calls, calls)
        debugger.close()


if __name__ == "__main__":
    unittest.main()
