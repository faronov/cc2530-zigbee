# SPDX-License-Identifier: BSD-3-Clause
"""Original byte-ABI and manual-runner doubles; no USB enumeration or firmware stubs."""

from contextlib import redirect_stderr, redirect_stdout
from dataclasses import replace
import hashlib
from io import StringIO
import json
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import check_radio_fifo_hardware as runner
import debug_image
from cc_debugger import Access, DebuggerError, TransportError
from radio_fifo_fixture import CHECKPOINTS, decode
from test_timebase_hardware import FixtureDebugger, BEFORE, READY, FAULT


DEADLINE, CALL, WRITE = 0x240, 0x280, 0x300
program = bytearray(bytes(range(256)) * 4)
program[BEFORE:BEFORE + 7] = b"\0\x22\0\x22\0\x80\xfd"
program[DEADLINE - 3:DEADLINE + 1] = b"\x75\x82\0\x22"
program[CALL:CALL + 3] = b"\x12" + (DEADLINE - 147).to_bytes(2, "big")
program[WRITE:WRITE + 2] = b"\x89\xd9"
PROGRAM = bytes(program)


def image_fixture():
    symbols = dict(zip(CHECKPOINTS, (BEFORE, READY, FAULT)), _radio_fifo_fixture_state=0)
    return SimpleNamespace(
        image_name="radio_fifo_fixture", board="generic", sha256=hashlib.sha256(PROGRAM).hexdigest(),
        metrics={"image_extent_bytes": len(PROGRAM), "iram_stack_start": 0x6d},
        symbol=lambda name: SimpleNamespace(address=symbols[name]),
        radio_fifo_proof={
            "function_start": DEADLINE - 147, "function_size": 148, "address": DEADLINE,
            "deadline_call": CALL, "return_address": CALL + 3, "write": WRITE,
            "preload_return": 0x320, "fixture_return": 0x380, "work": 108, "small": 129,
            "checkpoints": [BEFORE, READY, FAULT], "deadline_sp": 0x74,
            "private": dict(zip(("length", "timeout", "limit", "d", "body", "w"),
                               (245, 246, 250, 252, 254, 257))),
            "helper_locals": {"now": 164, "delay": 157, "deadline": 161},
        },
    )


def record_bytes(stage=0, completed=0, phase=3):
    data = bytearray(108)
    data[:12] = b"M2RF\x01\x6c" + bytes((phase, 4 if phase == 4 else 0, stage, completed, 0, 255))
    data[12:17] = b"\0\x04\0\0\x10"
    data[17:36] = b"\0" * 4 + b"\x01\0\0" + b"\0" * 7 + b"\xc9\x88\x88\x88\x08"
    data[58] = 255
    data[61:64] = b"\x88\x88\x84"
    data[67:73] = b"\x40\x01\x17\x3f\0\x18"
    data[81:90] = data[90:99] = b"\x31\x0e\xaa\x35\x03\x03\x53\x21\x55"
    data[99:101] = b"\x01\x84"
    data[106:] = b"\x69\x96"
    count = 4 if stage == 2 else 126 if stage == 4 else 0
    if phase == 1:
        data[10] = 8
        data[17:36] = bytes(18) + b"\x08"
        data[61:63] = b"\xc9\xc9"
        data[67:100] = bytes(33)
    elif phase != 2 and stage:
        data[11] = 1 if stage == 1 else 0
        data[55:57] = b"\x18\x01"
        if stage != 1:
            data[36] = 1
            data[40] = count or 1
        if count:
            data[45] = data[46] = data[49] = data[54] = data[57] = count
            data[74] = data[79] = count
        elif stage != 1:
            data[43] = data[44] = 2
    if phase == 4:
        data[11] = 8
        data[36:40] = (2000).to_bytes(4, "little")
        data[40] = data[45] = data[49] = data[54] = data[74] = data[79] = 1
        data[46] = data[57] = 0
    return bytes(data)


class FifoDebugger(FixtureDebugger):
    def __init__(self):
        super().__init__()
        self.breakpoints, self.steps, self.held = {}, 0, False
        self.bad_context = None
        self.bad_dpl = False
        self.registers = replace(self.registers, dps=0, sp=0x6e)
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
        elif self.steps == 2 and self.breakpoints.get(3) == DEADLINE:
            self.pc = DEADLINE
        else:
            self.steps += 1
            self.pc = FAULT if self.fault or self.held else READY
        if self.bad_pc is not None:
            self.pc = self.bad_pc

    def read_registers(self):
        self.event("registers")
        return replace(self.registers, pc=self.pc, sp=0x74 if self.pc == DEADLINE else 0x6e,
                       dptr0=int(self.bad_dpl) if self.pc == DEADLINE else 0x5678)

    def read_xdata(self, address, length):
        self.event("xdata", address, length)
        proof = image_fixture().radio_fifo_proof
        values = {
            "length": b"\x03", "timeout": b"\0\x04\0\0", "limit": b"\0\x10",
            "d": b"\x6c\0", "body": b"\x81\0\0",
            "w": (100).to_bytes(4, "little") * 2 + (1124).to_bytes(4, "little") + b"\0\x10\x88" + bytes(8),
            "now": (100).to_bytes(4, "little"), "delay": b"\0\x04\0\0", "deadline": b"\x09\x01\0",
            "small": b"\x13\x57\xa9", "work": bytes(19) + b"\x18\x01",
            "frame": b"\x80\x03\x20\x03\0\x6c" + (CALL + 3).to_bytes(2, "little"),
        }
        locations = proof["private"] | proof["helper_locals"] | {"small": 129, "work": 108, "frame": 0x1f6d}
        names = [name for name, location in locations.items() if location == address]
        if names:
            name = names[0]
            data = values[name]
            if self.bad_context == name:
                data = bytes((data[0] ^ 1,)) + data[1:]
        elif address == 0:
            if self.pc == BEFORE:
                data = record_bytes(phase=1)
            elif self.pc == DEADLINE:
                data = record_bytes(stage=2, phase=2)
            else:
                stage = 0 if self.steps == 1 else (self.steps - 2) % 5 + 1
                complete = 0 if self.stale_cycle else (self.steps - 1) // 5 & 255
                data = record_bytes(stage, complete, 4 if self.pc == FAULT else 3)
            if self.pc != BEFORE and self.corrupt_record:
                data = self.corrupt_record(data)
        else:
            assert address == 0x1e00
            complete = max(0, self.steps - 1) // 5 & 255
            data = b"M0CC\x01\x20\x02\0" + bytes((complete,)) + bytes(15) + b"\xc9\xc9" + bytes(6)
            if self.pc != BEFORE and self.corrupt_boot:
                data = self.corrupt_boot(data)
        assert len(data) == length
        if self.corrupt_cpu:
            self.registers = replace(self.registers, b=(self.registers.b + 1) & 255)
        return data

    def hold(self, seconds):
        self.event("hold", seconds)
        assert self.pc == DEADLINE and self.breakpoints[3] is None
        self.held = True


class FifoTests(unittest.TestCase):
    arguments = ["--bus", "1", "--address", "2", "--board", "generic", "--output", "unused"]

    def exercise(self, debugger, induced=False, cycles=1):
        with patch("check_clock_hardware.time.sleep", side_effect=debugger.hold):
            return runner.exercise(debugger, image_fixture(), PROGRAM, cycles, induced)

    def invoke(self, extra):
        out, err = StringIO(), StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            status = runner.main(self.arguments + extra)
        return status, out.getvalue(), err.getvalue()

    def test_serialized_shapes_and_corrupt_fields(self):
        for stage in range(6):
            data = record_bytes(stage)
            self.assertEqual(decode(data)["stage"], stage)
            for index in (0, 4, 5, 6, 7, 10, 12, 15, 20, 35, 58, 61, 62, 63, 64, 67, 68, 73, 80, 90, 99, 101, 107):
                changed = bytearray(data)
                changed[index] ^= 0x80
                with self.subTest(stage=stage, index=index), self.assertRaises(ValueError):
                    decode(bytes(changed))
        for data in (b"", record_bytes()[:-1], record_bytes() + b"\0", bytearray(record_bytes()),
                     record_bytes(stage=2, phase=2)):
            with self.assertRaises(ValueError):
                decode(data)
        self.assertEqual(decode(record_bytes(phase=1))["phase"], 1)
        self.assertEqual(decode(record_bytes(stage=2, phase=2), allow_running=True)["phase"], 2)
        runner.check_timeout(decode(record_bytes(stage=2, phase=4)))

    def test_offline_cli_loads_matching_image_before_decoding_state_or_proof(self):
        for command in ("radio-fifo-state", "radio-fifo-checkpoints"):
            for image_name in ("radio_fifo_fixture", "irq_fixture"):
                out, err = StringIO(), StringIO()
                args = [command, "--board", "generic", "--image", image_name, "--output", "unused"]
                if command == "radio-fifo-state":
                    args += ["--hex", record_bytes().hex()]
                with patch.object(debug_image, "DebugImage", return_value=image_fixture()) as load, \
                        redirect_stdout(out), redirect_stderr(err):
                    status = debug_image.main(args)
                load.assert_called_once()
                if image_name == "radio_fifo_fixture":
                    self.assertEqual(status, 0)
                    result = json.loads(out.getvalue())
                    self.assertEqual(result["evidence"], "offline-image-checked")
                    self.assertEqual(result["radio_fifo_proof"]["address"], DEADLINE)
                    if command == "radio-fifo-state":
                        self.assertEqual(result["radio_fifo_state"]["phase"], 3)
                else:
                    self.assertEqual((status, out.getvalue()), (1, ""))
                    self.assertTrue(err.getvalue())

    def test_257_cycles_and_every_code_byte_before_first_resume(self):
        debugger = FifoDebugger()
        result = self.exercise(debugger, cycles=257)
        self.assertEqual(len(result["observations"]), 1286)
        self.assertEqual(result["observations"][1280]["completed"], 0)
        self.assertEqual(result["observations"][-1]["completed"], 1)
        self.assertEqual(result["final_pc"], READY)
        first = next(i for i, event in enumerate(debugger.events) if event[0] == "resume")
        self.assertEqual([e for e in debugger.events[:first] if e[0] == "code"],
                         [("code", i, 128) for i in range(0, len(PROGRAM), 128)])
        self.assertFalse(any(e[0] == "hold" for e in debugger.events))

    def test_deadline_all_arguments_nested_frames_and_unconfirmed_effect(self):
        debugger = FifoDebugger()
        result = self.exercise(debugger, True)
        self.assertEqual(result["final_pc"], FAULT)
        self.assertEqual(result["completed_cycles"], 0)
        self.assertEqual(result["observations"][-1]["fifo"]["bytes_verified"], 0)
        self.assertEqual(result["observations"][-1]["radio"][7], 1)
        self.assertEqual(result["deadline_context"]["deadline"], 1124)
        hold = debugger.events.index(("hold", 0.25))
        self.assertLess(debugger.events.index(("xdata", 0x1f6d, 8)), hold)
        self.assertLess(debugger.events.index(("breakpoint", 3, DEADLINE, False)), hold)
        self.assertEqual(sum(e[0] == "attach-reset" for e in debugger.events), 1)

    def test_every_operation_failure_stops_without_retry_or_cleanup_actions(self):
        for induced in (False, True):
            reference = FifoDebugger()
            self.exercise(reference, induced)
            for boundary in range(1, len(reference.events) + 1):
                debugger = FifoDebugger()
                debugger.fail_at = boundary
                with self.subTest(induced=induced, boundary=boundary), self.assertRaises(TransportError):
                    self.exercise(debugger, induced)
                self.assertEqual(debugger.events, reference.events[:boundary])

    def test_invalid_live_context_never_holds_or_resumes(self):
        for name in ("length", "timeout", "limit", "body", "d", "w", "now", "delay",
                     "deadline", "small", "work", "frame"):
            debugger = FifoDebugger()
            debugger.bad_context = name
            with self.subTest(name=name), self.assertRaises(ValueError):
                self.exercise(debugger, True)
            self.assertEqual(debugger.pc, DEADLINE)
            self.assertFalse(any(e[0] == "hold" for e in debugger.events))
        debugger = FifoDebugger()
        debugger.bad_dpl = True
        with self.assertRaises(ValueError):
            self.exercise(debugger, True)
        debugger = FifoDebugger()
        debugger.corrupt_record = lambda d: d[:10] + b"\x04" + d[11:] if d[6] == 2 else d
        with self.assertRaises(ValueError):
            self.exercise(debugger, True)
        self.assertFalse(any(e[0] == "hold" for e in debugger.events))

    def test_hold_wrong_results_extra_write_flush_and_unrelated_changes_fail(self):
        for offset, value in ((7, 5), (11, 9), (37, 0), (40, 2), (43, 2), (44, 2), (45, 2),
                              (46, 1), (49, 2), (54, 2), (57, 1), (64, 1), (74, 2), (79, 2), (90, 0)):
            debugger = FifoDebugger()
            debugger.corrupt_record = lambda d, i=offset, v=value: (
                d[:i] + bytes((v,)) + d[i + 1:] if d[6] == 4 else d)
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                self.exercise(debugger, True)
        debugger = FifoDebugger()
        with patch("check_clock_hardware.time.sleep"), self.assertRaises(ValueError):
            runner.exercise(debugger, image_fixture(), PROGRAM, 1, True)
        self.assertEqual(debugger.pc, READY)
        for name, value in (("corrupt_code", True), ("bad_pc", 0x700), ("fault", True),
                            ("corrupt_cpu", True), ("stale_cycle", True), ("bad_step", True)):
            debugger = FifoDebugger()
            setattr(debugger, name, value)
            with self.subTest(name=name), self.assertRaises(ValueError):
                self.exercise(debugger)
            if name == "corrupt_code":
                self.assertFalse(any(e[0] == "resume" for e in debugger.events))
        debugger = FifoDebugger()
        debugger.corrupt_boot = lambda d: d[:24] + b"\0" + d[25:]
        with self.assertRaises(ValueError):
            self.exercise(debugger)

    def test_authorization_bounds_artifacts_and_image_gate_backend_loading(self):
        for extra in ([], ["--confirm-radio-fifo-test", "--cycles", "0"],
                      ["--confirm-radio-fifo-test", "--cycles", "258"],
                      ["--confirm-radio-fifo-test", "--cycles", "2", "--induce-timeout"],
                      ["--confirm-radio-fifo-test", "--bus", "0"],
                      ["--confirm-radio-fifo-test", "--address", "128"]):
            with patch.object(runner.PyUsbBackend, "load") as load:
                status, out, err = self.invoke(extra)
                self.assertEqual((status, out), (1, ""))
                self.assertTrue(err)
                load.assert_not_called()
        for body in (b"", PROGRAM[:-1], PROGRAM + b"\0"):
            with patch.object(runner, "DebugImage", return_value=image_fixture()), \
                    patch.object(runner.Path, "read_bytes", return_value=body), \
                    patch.object(runner.PyUsbBackend, "load") as load:
                self.assertEqual(self.invoke(["--confirm-radio-fifo-test"])[:2], (1, ""))
                load.assert_not_called()
        for field, value in (("image_name", "radio_fifo_test"), ("image_name", "irq_fixture")):
            image, debugger = image_fixture(), FifoDebugger()
            setattr(image, field, value)
            with self.assertRaises(ValueError):
                runner.exercise(debugger, image, PROGRAM)
            self.assertEqual(debugger.events, [])
        with patch.object(runner, "DebugImage", side_effect=ValueError("wrong board/artifacts")), \
                patch.object(runner.PyUsbBackend, "load") as load:
            self.assertEqual(self.invoke(["--confirm-radio-fifo-test"])[:2], (1, ""))
            load.assert_not_called()

    def test_minimum_permissions_and_json_only_after_cleanup(self):
        for cleanup in (None, DebuggerError("release failed")):
            with patch.object(runner, "DebugImage", return_value=image_fixture()), \
                    patch.object(runner.Path, "read_bytes", return_value=PROGRAM), \
                    patch.object(runner.PyUsbBackend, "load") as load, \
                    patch.object(runner, "Debugger") as constructor, \
                    patch.object(runner, "exercise", return_value={"evidence": "synthetic-test"}):
                constructor.return_value.__exit__.return_value = False
                constructor.return_value.__exit__.side_effect = cleanup
                status, out, err = self.invoke(["--confirm-radio-fifo-test"])
                constructor.assert_called_once_with(
                    load.return_value, Access.RESET_DEBUG_SESSION, timeout_ms=10_000,
                    allow_cpu_control=True, allow_target_reset=True, allow_memory_access=True,
                    allow_breakpoints=True)
                if cleanup is None:
                    self.assertEqual((status, json.loads(out)), (0, {"evidence": "synthetic-test"}))
                else:
                    self.assertEqual((status, out), (1, ""))
                    self.assertIn("release failed", err)
