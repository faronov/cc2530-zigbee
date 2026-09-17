# SPDX-License-Identifier: BSD-3-Clause
"""Independent wire/runner doubles; real compiled execution is checked separately."""
from contextlib import contextmanager, redirect_stderr, redirect_stdout
from dataclasses import replace
import hashlib
from io import StringIO
import json
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import check_dma_hardware as runner
import debug_image
from cc2530_debug import READ_CONFIG
from cc_debugger import Access, Debugger, DebuggerError, State, TransportError, UsbAddress
from dma_fixture import CHECKPOINTS, decode, expected_buffers
from test_timebase_hardware import FixtureDebugger, BEFORE, READY, FAULT
from test_m1_access import CoreBackend
from test_m1_transport import Clock

ARM, STOP, CALL, ARM_CALL, POLL_CALL, REQUEST = 0x340, 0x300, 0x400, 0x500, 0x580, 0x590
program = bytearray(bytes(range(256))*8)
program[BEFORE:BEFORE+7] = b"\0\x22\0\x22\0\x80\xfd"
program[STOP-3:STOP+1] = b"\x75\x82\0\x22"
program[ARM:ARM+13] = b"\x75\xd6\x01"+bytes(9)+b"\x22"
program[CALL:CALL+3] = b"\x12"+(STOP-167).to_bytes(2, "big")
program[ARM_CALL:ARM_CALL+3] = b"\x12"+ARM.to_bytes(2, "big")
program[REQUEST:REQUEST+3] = b"\x75\xd7\x01"
PROGRAM = bytes(program)
FLAGS = bytes.fromhex("31 0e 03 03 03 55 aa 12 37")


def image_fixture():
    return SimpleNamespace(image_name="dma_fixture", board="generic",
        sha256=hashlib.sha256(PROGRAM).hexdigest(),
        metrics={"image_extent_bytes": len(PROGRAM), "iram_stack_start": 0x69},
        symbol=lambda n: SimpleNamespace(address=dict(zip(CHECKPOINTS, (BEFORE, READY, FAULT)))[n]),
        dma_proof={"state": 0x88, "a": 0xfc, "b": 0x10e, "work": 0x120, "xdata_start": 0x45,
            "checkpoints": [BEFORE, READY, FAULT], "arm": ARM, "arm_ret": ARM+12, "arm_call": ARM_CALL,
            "armed_poll_call": POLL_CALL, "request": REQUEST, "expiry_call": CALL,
            "function_start": STOP-167, "function_size": 168, "address": STOP, "return_address": CALL+3,
            "dma_return": POLL_CALL+3, "fixture_return": 0x610, "deadline_sp": 0x75,
            "poll_expired": 0x65, "helper_locals": {"now": 21, "deadline": 14, "expired": 18},
            "private": {"source": 0x71, "destination": 0x66, "length": 0x68, "timeout": 0x69,
                        "limit": 0x6d, "d": 0x6f, "w": 0x73}})


def record_bytes(stage=0, completed=0, phase=3, *, complete=1):
    data = bytearray(116)
    data[:12] = b"M2DM\x01\x74"+bytes((phase, 4 if phase == 4 else 0, stage, completed, 0, 255))
    data[14:16] = b"\xff\xff"
    data[22:27] = b"\0\x04\0\0\x10"
    target = 0x88 if stage in (2, 3) else 0xc9
    data[31] = int(stage >= 2)
    data[41:46] = bytes((0x88 if stage == 4 else 0xc9, target, target, target, 8))
    data[65:68] = bytes((target, target, 0x84))
    data[74] = data[90] = 0xa0
    data[79] = 1; data[89] = 0x84
    data[93:102] = data[102:111] = FLAGS
    data[111:113] = b"\x69\x96"
    if stage:
        source, destination = (0xfd, 0x10f) if stage in (1, 2) else (0x10f, 0xfd)
        n = (completed&15)+1 if stage in (1, 2) else 16
        data[18:22] = source.to_bytes(2, "little")+destination.to_bytes(2, "little")
        data[75] = 0x45
        data[81:89] = source.to_bytes(2, "big")+destination.to_bytes(2, "big")+bytes((0, n, 0x20, 0x51))
        if stage in (1, 3):
            data[12] = n
            if phase in (3, 4):
                data[11] = 0; data[13] = 36
                data[50] = 5; data[53:56] = b"\x0f\x01\x01"
                data[56:64] = data[71:79]
                data[64] = 1
    if phase == 1:
        data[10] = 8; data[27:46] = bytes(18)+b"\x08"
    if phase == 4:
        data[11] = data[80] = 8; data[13] = 0
        data[46:50] = (2000).to_bytes(4, "little")
        data[50] = 4; data[53:56] = bytes((7, complete, 0))
        data[58] = data[73] = 1
        if not complete: data[56] = 1
    return bytes(data)


class DmaDebugger(FixtureDebugger):
    def __init__(self):
        super().__init__()
        self.config, self.steps, self.held = 0x26, 0, False
        self.breakpoints = {}
        self.bad_context = self.bad_config = None
        self.gate_result, self.gate_status, self.complete = 0x22, 0x22, 1
        self.registers = replace(self.registers, bank=1, dps=0, sp=0x6a)
        self.deadline = SimpleNamespace(remaining_ms=lambda: self.event("remaining") or 1000)

    def read_code(self, address, length):
        self.event("code", address, length)
        return b"\xff"*length if self.corrupt_code else PROGRAM[address:address+length]

    def enable_dma_after_reset(self):
        self.event("enable-dma")
        assert self.pc == 0 and self.config == 0x26
        self.config = self.gate_result
        return self.config

    def read_debug_config(self):
        self.event("config")
        return self.bad_config if self.bad_config is not None else self.config

    def read_debug_status(self):
        self.event("read-status")
        return self.gate_status if self.pc == 0 else 0x2a

    def set_breakpoint(self, slot, address, enabled=True):
        self.event("breakpoint", slot, address, enabled)
        self.breakpoints[slot] = address if enabled else None

    def resume(self):
        self.event("resume")
        assert self.config == 0x22
        if self.pc == 0: self.pc = BEFORE
        elif self.steps == 3 and self.breakpoints.get(3) in (ARM+12, STOP):
            self.pc = self.breakpoints[3]
        else:
            self.steps += 1
            self.pc = FAULT if self.held or self.fault else READY
        if self.bad_pc is not None: self.pc = self.bad_pc

    def read_registers(self):
        self.event("registers")
        return replace(self.registers, pc=self.pc, sp=0x75 if self.pc == STOP else 0x6a,
                       dptr0=0 if self.pc == STOP else 0x5678)

    @contextmanager
    def _stopped_operation(self, *, memory_access):
        assert memory_access and self.pc == STOP
        self.event("stopped"); yield self.deadline

    @contextmanager
    def _preserve_registers(self, deadline):
        assert deadline is self.deadline
        self.event("preserve"); yield

    def _exchange_byte(self, command, deadline):
        if command == READ_CONFIG:
            self.event("raw-config")
            return self.config
        return super()._exchange_byte(command, deadline)

    def _instruction(self, instruction, deadline):
        self.event("instruction", instruction)
        assert self.config == 0x22 and instruction[0] == 0xe5 and deadline is self.deadline
        values = dict(zip((0xd6, 0xd7, 0xd1, 0xc0, 0xd4, 0xd5, 0xd2, 0xd3), (1, 0, 0, 0xa0, 0x45, 0, 0, 0)))
        return values[instruction[1]] ^ (1 if self.bad_context == instruction[1] else 0)

    def live_values(self):
        values = {
            0x71: b"\x0f\x01", 0x66: b"\xfd\0", 0x68: b"\x10", 0x69: b"\0\x04\0\0",
            0x6d: b"\0\x10", 0x6f: b"\x20\x01",
            0x73: (100).to_bytes(4, "little")*2+(1124).to_bytes(4, "little")+b"\0\x10\x88\x45\0\0\0",
            21: (100).to_bytes(4, "little"), 14: (1124).to_bytes(4, "little"), 18: b"\x65\0\0", 0x65: b"\0",
            0x120: bytes(4)+b"\x03\0\0\x03\0\0\x01\0\0\xa0\x45\0\0\0\x01",
            0x45: b"\x01\x0f\0\xfd\0\x10\x20\x51",
            0x1f69: b"\x10\x06\x83\x05\x27\x01\x01\x26\0\0\x73\x03\x04",
        }
        a, b = expected_buffers(3, 0, 0)
        values[0xfc], values[0x10e] = a, b
        return values

    def read_xdata(self, address, length):
        self.event("xdata", address, length)
        if self.pc == STOP and address in self.live_values():
            data = self.live_values()[address]
            if isinstance(self.bad_context, tuple) and self.bad_context[:2] == ("xdata", address):
                index = self.bad_context[2]
                data = data[:index]+bytes([data[index]^1])+data[index+1:]
        elif address == 0x88:
            stage = 0 if self.steps <= 1 else (self.steps-2)%4+1
            completed = 0 if self.stale_cycle else max(0, self.steps-1)//4 & 255
            phase = 1 if self.pc == BEFORE else 2 if self.pc == STOP else 4 if self.pc == FAULT else 3
            data = record_bytes(3 if phase == 2 else stage, completed, phase, complete=self.complete)
            if self.pc != BEFORE and self.corrupt_record: data = self.corrupt_record(data)
        elif address == 0x1e00:
            data = b"M0CC\x01\x20\x02\0"+bytes((max(0, self.steps-1)//4 & 255,))+bytes(15)+b"\xc9\xc9"+bytes(6)
            if self.pc != BEFORE and self.corrupt_boot: data = self.corrupt_boot(data)
        else:
            assert address in (0xfc, 0x10e) and self.pc != FAULT
            stage, completed = (self.steps-2)%4+1, (self.steps-1)//4 & 255
            data = expected_buffers(stage, completed, (completed&15)+1 if stage == 1 else 16)[address == 0x10e]
        assert len(data) == length, (address, length, data)
        if self.corrupt_cpu: self.registers = replace(self.registers, b=(self.registers.b+1)&255)
        return data

    def hold(self, seconds):
        self.event("hold", seconds)
        assert self.pc == STOP and self.breakpoints[3] is None
        self.held = True


class DmaTests(unittest.TestCase):
    args = ["--board", "generic", "--bus", "1", "--address", "2", "--output", "unused"]

    def exercise(self, d, induced=False, cycles=1):
        with patch("check_clock_hardware.time.sleep", side_effect=d.hold):
            return runner.exercise(d, image_fixture(), PROGRAM, cycles, induced)

    def invoke(self, extra):
        out, err = StringIO(), StringIO()
        with redirect_stdout(out), redirect_stderr(err): result = runner.main(self.args+extra)
        return result, out.getvalue(), err.getvalue()

    def test_wire_decoder_and_every_ready_field_invariant(self):
        for stage in range(5):
            self.assertEqual(decode(record_bytes(stage))["stage"], stage)
            for index in (0, 4, 5, 6, 7, 10, 22, 26, 45, 65, 66, 67, 68, 71, 72, 73, 74, 79, 80, 102, 111, 113):
                data = bytearray(record_bytes(stage)); data[index] ^= 0x80
                with self.subTest(stage=stage, index=index), self.assertRaises(ValueError): decode(bytes(data))
        for data in (b"", record_bytes()[:-1], record_bytes()+b"\0", bytearray(record_bytes()), record_bytes(3, phase=2)):
            with self.assertRaises(ValueError): decode(data)
        self.assertEqual(decode(record_bytes(phase=1))["phase"], 1)
        for index in (14, 15, 16, 17, 18, 20, 67, 68, 71, 72, 73, 74, 75, 76, 77, 79, 80, 81, 89, 90, 93, 102):
            changed = bytearray(record_bytes(phase=1)); changed[index] ^= 1
            with self.subTest(initial=index), self.assertRaises(ValueError): decode(bytes(changed))
        for complete in (0, 1):
            runner.check_timeout(decode(record_bytes(3, phase=4, complete=complete)), image_fixture().dma_proof)

    def test_257_cycles_code_before_dma_gate_then_first_resume(self):
        d = DmaDebugger(); result = self.exercise(d, cycles=257)
        self.assertEqual((len(result["observations"]), result["confirmed_copies"], result["verified_bytes"]), (1029, 514, 6289))
        self.assertEqual([result["observations"][i]["completed"] for i in (1024, 1028)], [0, 1])
        gate = d.events.index(("enable-dma",)); first = d.events.index(("resume",))
        self.assertLess(gate, first)
        self.assertEqual([e for e in d.events[:gate] if e[0] == "code"],
                         [("code", i, min(128, len(PROGRAM)-i)) for i in range(0, len(PROGRAM), 128)])
        self.assertEqual(result["debug_gate"]["preserved_fmap"], 1)
        self.assertEqual(result["debug_gate"]["requested_config"], 0x22)
        self.assertFalse(any(e[0] == "instruction" for e in d.events))

    def test_genuine_arm_then_expiry_context_and_no_fault_payload_inspection(self):
        for complete in (0, 1):
            d = DmaDebugger(); d.complete = complete
            result = self.exercise(d, True)
            self.assertEqual(result["final_pc"], FAULT)
            self.assertEqual((result["confirmed_copies"], result["verified_bytes"]), (1, 1))
            self.assertEqual(result["deadline_context"]["deadline"], 1124)
            hold = d.events.index(("hold", 0.25))
            self.assertLess(d.events.index(("xdata", 0x1f69, 13)), hold)
            self.assertFalse(any(e[0] == "xdata" and e[1] in (0xfc, 0x10e) for e in d.events[hold:]))
            self.assertEqual(sum(e[0] == "enable-dma" for e in d.events), 1)
            self.assertEqual(sum(e[0] == "attach-reset" for e in d.events), 1)

    def test_every_runner_io_fault_is_terminal_without_retry_or_recovery(self):
        for induced in (False, True):
            reference = DmaDebugger(); self.exercise(reference, induced)
            for boundary in range(1, len(reference.events)+1):
                d = DmaDebugger(); d.fail_at = boundary
                with self.subTest(induced=induced, boundary=boundary), self.assertRaises(TransportError):
                    self.exercise(d, induced)
                self.assertEqual(d.events, reference.events[:boundary])

    def test_all_live_context_objects_and_actual_controller_mutations_reject_before_hold(self):
        for address, data in DmaDebugger().live_values().items():
            for index in range(len(data)):
                d = DmaDebugger(); d.bad_context = ("xdata", address, index)
                with self.subTest(address=address, index=index), self.assertRaises(ValueError): self.exercise(d, True)
                self.assertEqual(d.pc, STOP)
                self.assertFalse(any(e[0] == "hold" for e in d.events))
        for reg in (0xd6, 0xd7, 0xd1, 0xc0, 0xd4, 0xd5, 0xd2, 0xd3):
            d = DmaDebugger(); d.bad_context = reg
            with self.subTest(reg=reg), self.assertRaises(ValueError): self.exercise(d, True)
            self.assertFalse(any(e[0] == "hold" for e in d.events))

    def test_exact_timeout_not_arbitrary_fault_or_unreleased_success(self):
        for offset, value in ((7, 5), (11, 9), (12, 15), (13, 1), (47, 0), (50, 5), (53, 15),
                              (55, 1), (64, 0), (65, 0xc9), (68, 1), (74, 0xa1), (80, 9), (102, 0)):
            d = DmaDebugger()
            d.corrupt_record = lambda b, i=offset, v=value: b[:i]+bytes((v,))+b[i+1:] if b[6] == 4 else b
            with self.subTest(offset=offset), self.assertRaises(ValueError): self.exercise(d, True)
        d = DmaDebugger()
        with patch("check_clock_hardware.time.sleep"), self.assertRaises(ValueError):
            runner.exercise(d, image_fixture(), PROGRAM, 1, True)

    def test_configuration_gate_entry_code_cpu_clock_and_heartbeat_errors(self):
        for name, value in (("corrupt_code", True), ("bad_pc", 0x700), ("corrupt_cpu", True),
                            ("bad_step", True), ("stale_cycle", True), ("fault", True),
                            ("bad_config", 0x22), ("gate_result", 0x26), ("gate_status", 0x20)):
            d = DmaDebugger(); setattr(d, name, value)
            with self.subTest(name=name), self.assertRaises(ValueError): self.exercise(d)
            if name in ("corrupt_code", "bad_config", "gate_result", "gate_status"):
                self.assertFalse(any(e[0] == "resume" for e in d.events))
        d = DmaDebugger(); d.corrupt_boot = lambda b: b[:24]+b"\0"+b[25:]
        with self.assertRaises(ValueError): self.exercise(d)
        for config in (0, 0x26, 0x23, 255):
            d = DmaDebugger()
            original = d.read_debug_config
            d.read_debug_config = lambda: config if d.pc else original()
            with self.subTest(config=config), self.assertRaises(ValueError): self.exercise(d)

    def test_cli_authorization_image_and_artifacts_precede_backend_loading(self):
        for extra in ([], ["--confirm-dma-test", "--cycles", "0"], ["--confirm-dma-test", "--cycles", "258"],
                      ["--confirm-dma-test", "--cycles", "2", "--induce-timeout"],
                      ["--confirm-dma-test", "--bus", "0"], ["--confirm-dma-test", "--address", "128"]):
            with patch.object(runner.PyUsbBackend, "load") as load:
                status, out, err = self.invoke(extra)
                self.assertEqual((status, out), (1, "")); self.assertTrue(err); load.assert_not_called()
        for body in (b"", PROGRAM[:-1], PROGRAM+b"\0"):
            with patch.object(runner, "DebugImage", return_value=image_fixture()), \
                    patch.object(runner.Path, "read_bytes", return_value=body), patch.object(runner.PyUsbBackend, "load") as load:
                self.assertEqual(self.invoke(["--confirm-dma-test"])[:2], (1, "")); load.assert_not_called()
        for name in ("dma_test", "bringup", "radio_fifo_fixture"):
            image, d = image_fixture(), DmaDebugger(); image.image_name = name
            with self.assertRaises(ValueError): runner.exercise(d, image, PROGRAM)
            self.assertEqual(d.events, [])
        with patch.object(runner, "DebugImage", side_effect=ValueError("image mismatch")), \
                patch.object(runner.PyUsbBackend, "load") as load:
            self.assertEqual(self.invoke(["--confirm-dma-test"])[:2], (1, "")); load.assert_not_called()

    def test_separate_dma_permission_and_success_only_after_cleanup(self):
        for cleanup in (None, DebuggerError("release failed")):
            with patch.object(runner, "DebugImage", return_value=image_fixture()), \
                    patch.object(runner.Path, "read_bytes", return_value=PROGRAM), \
                    patch.object(runner.PyUsbBackend, "load") as load, patch.object(runner, "Debugger") as ctor, \
                    patch.object(runner, "exercise", return_value={"evidence": "synthetic-test"}):
                ctor.return_value.__exit__.side_effect = cleanup
                ctor.return_value.__exit__.return_value = False
                status, out, err = self.invoke(["--confirm-dma-test"])
                ctor.assert_called_once_with(load.return_value, Access.RESET_DEBUG_SESSION, timeout_ms=10_000,
                    allow_cpu_control=True, allow_target_reset=True, allow_memory_access=True,
                    allow_breakpoints=True, allow_dma_enable=True)
                if cleanup is None: self.assertEqual((status, json.loads(out)), (0, {"evidence": "synthetic-test"}))
                else: self.assertEqual((status, out), (1, "")); self.assertIn("release failed", err)

    def test_offline_decoder_and_proof_require_matching_image(self):
        for command in ("dma-state", "dma-checkpoints"):
            for name in ("dma_fixture", "radio_fifo_fixture"):
                args = [command, "--board", "generic", "--image", name, "--output", "unused"]
                if command == "dma-state": args += ["--hex", record_bytes().hex()]
                out, err = StringIO(), StringIO()
                with patch.object(debug_image, "DebugImage", return_value=image_fixture()), \
                        redirect_stdout(out), redirect_stderr(err): status = debug_image.main(args)
                if name == "dma_fixture":
                    self.assertEqual(status, 0); self.assertEqual(json.loads(out.getvalue())["dma_proof"]["address"], STOP)
                else: self.assertEqual((status, out.getvalue()), (1, "")); self.assertTrue(err.getvalue())


class ControllerBackend(CoreBackend):
    """The existing synthetic CPU plus the published RD_CONFIG packet."""
    config = 0x22

    def bulk_write(self, endpoint, data, timeout_ms):
        if data != b"\x1f\x24":
            return super().bulk_write(endpoint, data, timeout_ms)
        self.record("write", endpoint, data, timeout_ms)
        assert endpoint == 4 and self.pending is None
        if len(self.calls) in self.write_counts: return self.write_counts[len(self.calls)]
        self.pending = bytes((self.config,))
        return len(data)


class ControllerReadTests(unittest.TestCase):
    def session(self, *, bank=0, dps=0, memory=True):
        clock = Clock()
        backend = ControllerBackend(clock, bank, dps)
        backend.sfr.update(dict(zip((0xd6, 0xd7, 0xd1, 0xc0, 0xd4, 0xd5, 0xd2, 0xd3),
                                    (1, 0, 0, 0xa0, 0x45, 0, 0x36, 0x12))))
        debugger = Debugger(backend, Access.EXISTING_DEBUG_SESSION, 10, clock, allow_memory_access=memory)
        debugger.open(UsbAddress(1, 2)); backend.calls.clear()
        return debugger, backend

    def test_exact_read_instructions_preserve_all_cpu_and_memory_context(self):
        for bank in range(4):
            for dps in range(2):
                d, b = self.session(bank=bank, dps=dps)
                before = b.cpu_image(), bytes(b.ram)
                self.assertEqual(runner.live_controller(d), b"\x01\0\0\xa0\x45\0\x36\x12")
                self.assertEqual((b.cpu_image(), bytes(b.ram)), before)
                reads = [p[3] for p in b.packets() if len(p) == 4 and p[:3] == b"\x7f\x56\xe5" and
                         p[3] in (0xd6, 0xd7, 0xd1, 0xc0, 0xd4, 0xd5, 0xd2, 0xd3)]
                self.assertEqual(reads, [0xd6, 0xd7, 0xd1, 0xc0, 0xd4, 0xd5, 0xd2, 0xd3])
                d.close()

    def test_permissions_and_all_wrong_configs_prevent_peripheral_instruction(self):
        d, b = self.session(memory=False)
        with self.assertRaises(DebuggerError): runner.live_controller(d)
        self.assertEqual(b.calls, []); d.close()
        for config in range(256):
            if config == 0x22: continue
            d, b = self.session(); b.config = config
            with self.subTest(config=config), self.assertRaises(ValueError): runner.live_controller(d)
            self.assertFalse(any(p[:2] in (b"\x7f\x56", b"\xaf\x57", b"\x4f\x55") for p in b.packets()))
            d.close()

    def test_every_usb_boundary_failure_or_late_effect_has_no_restore_or_following_io(self):
        d, b = self.session(); runner.live_controller(d); count = len(b.calls); d.close()
        for late in (False, True):
            for boundary in range(1, count+1):
                d, b = self.session()
                if late: b.late_at = boundary
                else: b.fail_at = boundary
                with self.subTest(late=late, boundary=boundary), self.assertRaises(TransportError):
                    runner.live_controller(d)
                self.assertEqual(d.state, State.FAULTED)
                self.assertEqual(len(b.calls), boundary)
                with self.assertRaises(DebuggerError): runner.live_controller(d)
                self.assertEqual(len(b.calls), boundary)
                d.close(); self.assertEqual(b.calls[-1], ("close",))
