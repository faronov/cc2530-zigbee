# SPDX-License-Identifier: BSD-3-Clause
"""Independent wire/runner doubles; no encryption model or physical USB."""
from contextlib import contextmanager, redirect_stderr, redirect_stdout
from dataclasses import replace
import hashlib
from io import StringIO
import json
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import check_aes_hardware as runner
import debug_image
from aes_fixture import AES_FIELDS, CONTROLLER, FLAGS, KATS, PROJECTED, decode, expected_buffers
from cc_debugger import Access, Debugger, DebuggerError, State, TransportError, UsbAddress
from test_dma_fixture import ControllerBackend, DmaDebugger
from test_m1_transport import Clock

BEFORE, READY, FAULT = 0x140, 0x142, 0x144
VECTORS = tuple(b"".join(bytes.fromhex(s) for s in KATS[i % 5])+b"\0" for i in range(21))
INITIAL_FLAGS = bytes.fromhex("31 0e 03 03 55 aa 12 37")
P = dict(state=0xfb, key=0x13b, input=0x14b, output=0x15b, work=0x16d, vectors=0x3000, stack_start=0x6e,
         descriptor0=0x45, descriptor1=0x4d, checkpoints=[BEFORE, READY, FAULT],
         arm_input=0xbf1, arm_output=0xbfe, arm_ret=0xbfd, expiry=0x2da, expiry_call=0x103f,
         reader=0x147, pre_latch=0x14a, reader_call=0xfa1, final_gate=0x1da3,
         armed_sample_call=0x1955, final_sample_call=0x1dc9, fixture_return=0x2ac4, wait=0xaf,
         private=dict(key=0xf6, input=0xe9, output=0xec, timeout=0xee, limit=0xf2, d=0xf4),
         sample=dict(d=0xe4, final=0xe3, expired=0xe6), helper=dict(now=21, deadline=14, expired=18))
program = bytearray(bytes(range(256))*64)
program[BEFORE:BEFORE+7] = b"\0\x22\0\x22\0\x80\xfd"
for name, bit in (("arm_input", 1), ("arm_output", 2)):
    program[P[name]:P[name]+13] = b"\x75\xd6"+bytes([bit])+bytes(9)+b"\x22"
program[P["expiry"]-3:P["expiry"]+1] = b"\x75\x82\0\x22"
program[P["expiry_call"]:P["expiry_call"]+3] = b"\x12\x02\x33"
program[P["reader"]:P["pre_latch"]+2] = b"\x90\0\0\xe5\x95"
program[P["reader_call"]:P["reader_call"]+3] = b"\x12\x01\x47"
program[P["final_gate"]-2:P["final_gate"]] = b"\xf5\x98"
program[P["vectors"]:P["vectors"]+1029] = b"".join(VECTORS)
PROGRAM = bytes(program)


def image_fixture():
    return SimpleNamespace(image_name="aes_fixture", board="generic", aes_proof=P,
                           sha256=hashlib.sha256(PROGRAM).hexdigest(),
                           metrics=dict(image_extent_bytes=len(PROGRAM), iram_stack_start=0x6e))


def diagnostic(phase=3, mode="none"):
    d = dict.fromkeys(AES_FIELDS, 0)
    d.update(polls=17, phase=4, submitted=7, input_complete=7, output_drained=1, published=1,
             configured=3, arms=4, ack_issued=3, dma_acked=7, enc_ack_issued=3, enc_acked=7,
             ircon=0xa0, control=0x48, enc_flags=0xa4, cfg0_low=0x45, cfg1_low=0x4d, sample_valid=3)
    if phase in (2, 4):
        d["published"] = 0; d["enc_acked"] = 3
        if mode == "pre-key":
            d.update(polls=4 if phase == 2 else 5, phase=1, submitted=int(phase == 4),
                     input_complete=int(phase == 4), output_drained=0, arms=2, ack_issued=0,
                     dma_acked=0, enc_ack_issued=0, enc_acked=0, arm=3 if phase == 2 else 2,
                     irq=int(phase == 4), control=0x48 if phase == 2 else 0x4c)
        elif phase == 2:
            d["sample_valid"] = 1
        if phase == 4: d["elapsed_ticks"] = 65536
    return d


def packed(d, fields=AES_FIELDS):
    return b"".join(d[k].to_bytes(4 if k == "elapsed_ticks" else 2 if k == "polls" else 1, "little") for k in fields)


def record_bytes(stage=0, completed=0, phase=3, mode="none"):
    r = bytearray(64); target = 0x88 if stage in (2, 3) else 0xc9
    r[:14] = b"M2AE\x02\x40"+bytes((phase, 4 if phase == 4 else 0, stage, completed,
                                   completed % 21, ((completed//21)+(stage == 3))&3,
                                   255 if phase in (1, 2) else 8 if phase == 4 else 0,
                                   0 if phase == 1 else 2 if stage in (1, 3) else 1))
    r[15:17] = b"\xff\xff"; r[20:25] = b"\0\x80\0\0\x10"
    r[44:47] = bytes((target, target, 0x84)); r[50] = int(phase != 4)
    r[51:59] = INITIAL_FLAGS; r[59:] = b"\xa0\xa4\x84\x69\x96"
    if phase in (3, 4):
        if stage in (1, 3):
            r[14] = 50; r[25:44] = packed(diagnostic(phase, mode), PROJECTED)
        else:
            r[29] = int(stage != 0)
            r[39:44] = bytes((0x88 if stage == 4 else 0xc9, target, target, target, 8))
    if phase == 4: r[19] = 8
    return bytes(r)


class AesDebugger(DmaDebugger):
    def __init__(self, mode="none"):
        super().__init__()
        self.mode = mode
        self.registers = replace(self.registers, sp=0x6f)
        self.payload_reads_after_fault = []

    def read_code(self, address, length):
        self.event("code", address, length)
        return b"\xff"*length if self.corrupt_code else PROGRAM[address:address+length]

    def resume(self):
        self.event("resume")
        assert self.config == 0x22
        if self.pc == 0: self.pc = BEFORE
        elif self.steps == 3 and self.breakpoints.get(3) is not None:
            self.pc = self.breakpoints[3]
        else:
            self.steps += 1; self.pc = FAULT if self.held or self.fault else READY
        if self.bad_pc is not None: self.pc = self.bad_pc

    def coordinates(self):
        phase = 1 if self.pc == BEFORE else 4 if self.pc == FAULT else 2 if self.pc in (P["expiry"], P["pre_latch"]) else 3
        stage = 3 if phase == 2 else 0 if self.steps <= 1 else (self.steps-2)%4+1
        completed = 0 if self.stale_cycle else max(0, self.steps-1)//4 & 255
        return stage, completed, phase

    def read_registers(self):
        self.event("registers")
        internal = self.pc in (P["expiry"], P["pre_latch"])
        return replace(self.registers, pc=self.pc, sp=(0x7b if self.mode == "pre-key" else 0x73) if internal else 0x6f,
                       dptr0=0 if internal else 0x5678)

    @contextmanager
    def _stopped_operation(self, *, memory_access):
        assert memory_access
        self.event("stopped"); yield self.deadline

    def _instruction(self, instruction, deadline):
        self.event("instruction", instruction)
        assert instruction[0] == 0xe5 and self.config == 0x22 and deadline is self.deadline
        stage, _, phase = self.coordinates()
        d = diagnostic(phase, self.mode)
        controller = bytes(d[k] for k in ("control", "enc_flags", "arm", "request", "irq", "ircon",
                                          "cfg0_low", "cfg0_high", "cfg1_low", "cfg1_high"))
        if phase == 1 or stage == 0: controller = b"\x08\xa4\0\0\0\xa0"+bytes(4)
        target = 0x88 if stage in (2, 3) else 0xc9
        values = dict(zip(CONTROLLER, controller)) | dict(zip(FLAGS, INITIAL_FLAGS))
        values.update({0xa8: 0, 0xb8: 0, 0x9a: 0, 0xbe: 0x84, 0xc6: target, 0x9e: target})
        reg = instruction[1]
        return values[reg] ^ int(self.bad_context == reg)

    def live_values(self):
        final = self.mode == "final"
        stack = bytes.fromhex("c42acc1da40f" if final else "c42a016d017458190173016d4210")
        return {
            0xf6: b"\0\x30\x80", 0xe9: b"\x4b\x01\0", 0xec: b"\x5c\x01", 0xee: b"\0\x80\0\0",
            0xf2: b"\0\x10", 0xf4: b"\x6d\x01", 0xad: b"\0\x01", 0xe4: b"\x6d\x01", 0xe3: bytes([final]),
            0xaf: (100).to_bytes(4, "little")*2+(32868).to_bytes(4, "little")+b"\0\x10\x88\xa0\xa4\x40\x45\0\x4d\0",
            21: (100).to_bytes(4, "little"), 14: (32868).to_bytes(4, "little"), 18: b"\xe6\0\0", 0xe6: b"\0",
            0x45: bytes((0, 0x8d if final else 0x6d))+b"\x70\xb1\0\x10\x1d\x41\x70\xb2\0\x9d\0\x10\x1e\x11"+bytes(24),
            0x16d: packed(diagnostic(2, self.mode)), 0x1f6e: stack, 0x13b: expected_buffers(VECTORS[0], False),
        }

    def read_xdata(self, address, length):
        self.event("xdata", address, length)
        stage, completed, phase = self.coordinates()
        if phase == 2 and address in self.live_values():
            data = self.live_values()[address]
            if isinstance(self.bad_context, tuple) and self.bad_context[:2] == ("xdata", address):
                i = self.bad_context[2]; data = data[:i]+bytes([data[i]^1])+data[i+1:]
        elif address == P["state"]:
            data = record_bytes(stage, completed, phase, self.mode)
            if phase != 1 and self.corrupt_record: data = self.corrupt_record(data)
        elif address == 0x1e00:
            data = b"M0CC\x01\x20\x02\0"+bytes([completed])+bytes(15)+b"\xc9\xc9"+bytes(6)
            if phase != 1 and self.corrupt_boot: data = self.corrupt_boot(data)
        elif address == P["work"]: data = packed(diagnostic(phase, self.mode))
        else:
            assert address == P["key"] and length == 50
            data = expected_buffers(VECTORS[completed % 21], phase == 3)
        if phase == 4:
            assert address in (P["state"], P["key"], P["work"], 0x1e00)
            self.payload_reads_after_fault.append(address)
        assert len(data) == length, (address, length)
        if self.corrupt_cpu: self.registers = replace(self.registers, b=(self.registers.b+1)&255)
        return data

    def hold(self, seconds):
        self.event("hold", seconds)
        assert self.pc == P["expiry" if self.mode == "pre-key" else "pre_latch"] and self.breakpoints[3] is None
        self.held = True


class AesFixtureTests(unittest.TestCase):
    def exercise(self, d, cycles=1):
        with patch.object(runner.time, "sleep", side_effect=d.hold):
            return runner.exercise(d, image_fixture(), PROGRAM, VECTORS, cycles, d.mode)

    def test_wire_and_257_cycles_code_gate_resume_order(self):
        for stage in range(5):
            r = record_bytes(stage); self.assertEqual(decode(r)["stage"], stage)
            for i in (0, 4, 5, 6, 7, 8, 10, 11, 12, 13, 20, 24, 44, 45, 46, 47, 50, 62, 63):
                with self.subTest(stage=stage, offset=i), self.assertRaises(ValueError):
                    decode(r[:i]+bytes([r[i]^0x80])+r[i+1:])
        d = AesDebugger(); result = self.exercise(d, 257)
        self.assertEqual((len(result["observations"]), result["accepted_blocks"],
                          result["c_confirmed_input_bytes"], result["c_drained_output_bytes"]), (1029, 514, 24672, 8224))
        self.assertEqual((result["command_submissions"], result["arms_issued"], result["dma_acks_issued"],
                          result["dma_phases_acknowledged"], result["enc_acks_issued"], result["enc_acks_confirmed"],
                          result["published_blocks"], result["published_output_bytes"]),
                         (1542, 2056, 1542, 1542, 1542, 1542, 514, 8224))
        gate = d.events.index(("enable-dma",))
        self.assertEqual([e for e in d.events[:gate] if e[0] == "code"],
                         [("code", a, 128) for a in range(0, len(PROGRAM), 128)])
        self.assertLess(gate, d.events.index(("resume",)))
        self.assertFalse(any(e[0] == "instruction" for e in d.events[:gate]))
        self.assertEqual(result["debug_gate"]["preserved_fmap"], 1)
        self.assertEqual({(r["vector"], r["spaces"], r["stage"]) for r in result["observations"] if r["kind"] == 2},
                         {(v, s, c) for v in range(21) for s in range(4) for c in (1, 3)})

    def test_two_exact_negative_contexts_and_safe_caller_only_failure_inspection(self):
        for mode in ("pre-key", "final"):
            d = AesDebugger(mode); r = self.exercise(d)
            self.assertEqual(r["accepted_blocks"], 1)
            self.assertEqual((r["c_confirmed_input_bytes"], r["c_drained_output_bytes"], r["published_output_bytes"]),
                             (64, 16, 16) if mode == "pre-key" else (96, 32, 16))
            self.assertEqual((r["enc_acks_issued"], r["enc_acks_confirmed"]), (3, 3) if mode == "pre-key" else (6, 5))
            self.assertEqual(r["final_pc"], FAULT)
            self.assertEqual(r["deadline_context"]["deadline"], 32868)
            self.assertEqual(r["deadline_context"]["final_now_not_yet_latched"], mode == "final")
            self.assertIn(P["key"], d.payload_reads_after_fault)
            self.assertFalse(r["private_payload_inspected"])
            self.assertEqual(sum(e[0] == "attach-reset" for e in d.events), 1)
            self.assertEqual(sum(e[0] == "enable-dma" for e in d.events), 1)

    def test_every_runner_io_failure_has_no_following_io(self):
        for mode in ("none", "pre-key", "final"):
            reference = AesDebugger(mode); self.exercise(reference)
            for boundary in range(1, len(reference.events)+1):
                d = AesDebugger(mode); d.fail_at = boundary
                with self.subTest(mode=mode, boundary=boundary), self.assertRaises(TransportError): self.exercise(d)
                self.assertEqual(d.events, reference.events[:boundary])

    def test_every_read_context_byte_and_controller_mutation_rejects_before_hold(self):
        for mode in ("pre-key", "final"):
            reference = AesDebugger(mode); self.exercise(reference)
            stop = P["expiry" if mode == "pre-key" else "pre_latch"]
            begin = reference.events.index(("breakpoint", 3, stop, True))
            end = reference.events.index(("hold", 2.0))
            reads = {e[1] for e in reference.events[begin:end] if e[0] == "xdata"}
            for address, data in reference.live_values().items():
                if address not in reads: continue
                for i in range(len(data)):
                    d = AesDebugger(mode); d.bad_context = ("xdata", address, i)
                    try: self.exercise(d)
                    except ValueError:
                        # Final polling is variable. A valid larger live count is
                        # rejected when the later timeout record contradicts it.
                        self.assertEqual(any(e[0] == "hold" for e in d.events),
                                         (mode, address, i) == ("final", 0x16d, 5))
                    else:
                        self.assertIn((address, i), {(0xaf, 4), (0xaf, 0), (21, 0)})
            for reg in runner.OBSERVATIONS:
                d = AesDebugger(mode); d.bad_context = reg
                with self.subTest(mode=mode, reg=reg), self.assertRaises(ValueError): self.exercise(d)
                self.assertFalse(any(e[0] == "hold" for e in d.events))

    def test_entry_configuration_cpu_and_exact_timeout_errors(self):
        for name, value in (("corrupt_code", True), ("bad_pc", 0x700), ("corrupt_cpu", True),
                            ("bad_step", True), ("stale_cycle", True), ("fault", True),
                            ("bad_config", 0x22), ("gate_result", 0x26), ("gate_status", 0x20)):
            d = AesDebugger(); setattr(d, name, value)
            with self.subTest(name=name), self.assertRaises(ValueError): self.exercise(d, 2)
        for mode in ("pre-key", "final"):
            for offset in (7, 12, 19, 25, 29, 32, 33, 35, 36, 37, 38, 39, 40, 41, 42, 43, 50):
                d = AesDebugger(mode)
                d.corrupt_record = lambda b, i=offset: b[:i]+bytes([b[i]^1])+b[i+1:] if b[6] == 4 else b
                with self.subTest(mode=mode, offset=offset), self.assertRaises(ValueError): self.exercise(d)
            d = AesDebugger(mode)
            with patch.object(runner.time, "sleep"), self.assertRaises(ValueError):
                runner.exercise(d, image_fixture(), PROGRAM, VECTORS, 1, mode)
        for config in range(256):
            if config == 0x22: continue
            d = AesDebugger(); original = d.read_debug_config
            d.read_debug_config = lambda: config if d.pc else original()
            with self.subTest(config=config), self.assertRaises(ValueError): self.exercise(d)

    def test_internal_cpu_and_checkpoint_mutations_fail_before_hold(self):
        for mode in ("pre-key", "final"):
            for field, value in (("sp", 0x7f), ("dps", 1), ("dptr0", 1)):
                d = AesDebugger(mode); original = d.read_registers
                def registers():
                    r = original()
                    return replace(r, **{field: value}) if d.pc in (P["expiry"], P["pre_latch"]) else r
                d.read_registers = registers
                with self.subTest(mode=mode, field=field), self.assertRaises(ValueError): self.exercise(d)
                self.assertFalse(any(e[0] == "hold" for e in d.events))
            for stop in (P["arm_ret"] if mode == "pre-key" else P["final_gate"],
                         P["expiry"] if mode == "pre-key" else P["pre_latch"]):
                d = AesDebugger(mode); original_resume = d.resume
                def resume():
                    original_resume()
                    if d.pc == stop: d.pc += 1
                d.resume = resume
                with self.subTest(mode=mode, stop=stop), self.assertRaises(ValueError): self.exercise(d)
                self.assertFalse(any(e[0] == "hold" for e in d.events))

    def test_unattempted_aes_does_not_reinterpret_retained_union_or_caller_priming(self):
        d = AesDebugger(); d.pc = FAULT; d.steps = 4; d.config = 0x22
        raw = bytearray(record_bytes(3, phase=4))
        raw[7] = 5; raw[12] = 255; raw[14] = raw[19] = 0; raw[25:44] = bytes(19)
        d.corrupt_record = lambda _: bytes(raw)
        record, *_ = runner.inspect(d, image_fixture(), FAULT, VECTORS)
        self.assertEqual(record["result"], 255)
        self.assertEqual([e for e in d.events if e[0] == "xdata"],
                         [("xdata", P["state"], 64), ("xdata", 0x1e00, 32)])

    def invoke(self, extra):
        out, err = StringIO(), StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            status = runner.main(["--board", "generic", "--output", "unused", "--bus", "1", "--address", "2"]+extra)
        return status, out.getvalue(), err.getvalue()

    def test_cli_permissions_prevalidation_cleanup_and_offline_decode(self):
        for extra in ([], ["--confirm-aes-test", "--cycles", "0"], ["--confirm-aes-test", "--cycles", "258"],
                      ["--confirm-aes-test", "--cycles", "2", "--negative", "final"], ["--confirm-aes-test", "--bus", "0"]):
            with patch.object(runner.PyUsbBackend, "load") as load:
                self.assertEqual(self.invoke(extra)[:2], (1, "")); load.assert_not_called()
        for cleanup in (None, DebuggerError("release failed")):
            with patch.object(runner, "DebugImage", return_value=image_fixture()), \
                    patch.object(runner.Path, "read_bytes", return_value=PROGRAM), \
                    patch.object(runner, "public_vectors", return_value=VECTORS), \
                    patch.object(runner.PyUsbBackend, "load") as load, patch.object(runner, "Debugger") as ctor, \
                    patch.object(runner, "exercise", return_value={"evidence": "synthetic-test"}):
                ctor.return_value.__exit__.side_effect = cleanup; ctor.return_value.__exit__.return_value = False
                status, out, err = self.invoke(["--confirm-aes-test"])
                ctor.assert_called_once_with(load.return_value, Access.RESET_DEBUG_SESSION, timeout_ms=10_000,
                    allow_cpu_control=True, allow_target_reset=True, allow_memory_access=True,
                    allow_breakpoints=True, allow_dma_enable=True)
                if cleanup is None: self.assertEqual((status, json.loads(out)), (0, {"evidence": "synthetic-test"}))
                else: self.assertEqual((status, out), (1, "")); self.assertIn("release failed", err)
        for image_name in ("aes_test", "dma_fixture", "bringup"):
            image = image_fixture(); image.image_name = image_name; d = AesDebugger()
            with self.assertRaises(ValueError): runner.exercise(d, image, PROGRAM, VECTORS)
            self.assertEqual(d.events, [])
        for body in (b"", PROGRAM[:-1], PROGRAM+b"\0"):
            with self.assertRaises(ValueError): runner.validate_program(image_fixture(), body, VECTORS, 1, "none")
        wrong_reader = bytearray(PROGRAM)
        wrong_reader[P["reader"]:P["pre_latch"]] = b"\x75\x92\0"
        image = image_fixture()
        image.sha256 = hashlib.sha256(wrong_reader).hexdigest()
        with self.assertRaisesRegex(ValueError, "expiry/pre-latch"):
            runner.validate_program(image, bytes(wrong_reader), VECTORS, 1, "final")
        for command in ("aes-state", "aes-checkpoints"):
            out, err = StringIO(), StringIO()
            args = [command, "--board", "generic", "--image", "aes_fixture", "--output", "unused"]
            if command == "aes-state": args += ["--hex", record_bytes().hex()]
            with patch.object(debug_image, "DebugImage", return_value=image_fixture()), redirect_stdout(out), redirect_stderr(err):
                self.assertEqual(debug_image.main(args), 0)
            self.assertEqual(json.loads(out.getvalue())["aes_proof"]["pre_latch"], P["pre_latch"])


class AesControllerTests(unittest.TestCase):
    def session(self, bank=0, dps=0, memory=True):
        clock = Clock(); backend = ControllerBackend(clock, bank, dps)
        backend.sfr.update({a: i for i, a in enumerate(runner.OBSERVATIONS)})
        d = Debugger(backend, Access.EXISTING_DEBUG_SESSION, 10, clock, allow_memory_access=memory)
        d.open(UsbAddress(1, 2)); backend.calls.clear()
        return d, backend

    def test_exact_readonly_surface_and_all_cpu_fmap_preservation(self):
        for bank in range(4):
            for dps in range(2):
                d, b = self.session(bank, dps); before = b.cpu_image(), bytes(b.ram)
                self.assertEqual(runner.live_registers(d), bytes(range(len(runner.OBSERVATIONS))))
                self.assertEqual((b.cpu_image(), bytes(b.ram)), before)
                reads = [p[3] for p in b.packets() if len(p) == 4 and p[:3] == b"\x7f\x56\xe5" and p[3] in runner.OBSERVATIONS]
                self.assertEqual(reads, list(runner.OBSERVATIONS))
                self.assertFalse(any(p[-2:] in (b"\xe5\xb1", b"\xe5\xb2") for p in b.packets()))
                d.close()

    def test_every_wrong_configuration_and_permission_precedes_instruction(self):
        d, b = self.session(memory=False)
        with self.assertRaises(DebuggerError): runner.live_registers(d)
        self.assertEqual(b.calls, []); d.close()
        for config in range(256):
            if config == 0x22: continue
            d, b = self.session(); b.config = config
            with self.assertRaises(ValueError): runner.live_registers(d)
            self.assertFalse(any(p[:2] in (b"\x7f\x56", b"\xaf\x57", b"\x4f\x55") for p in b.packets()))
            d.close()

    def test_every_transport_failure_and_late_effect_is_terminal(self):
        d, b = self.session(); runner.live_registers(d); count = len(b.calls); d.close()
        for late in (False, True):
            for boundary in range(1, count+1):
                d, b = self.session()
                if late: b.late_at = boundary
                else: b.fail_at = boundary
                with self.subTest(late=late, boundary=boundary), self.assertRaises(TransportError): runner.live_registers(d)
                self.assertEqual((d.state, len(b.calls)), (State.FAULTED, boundary))
                with self.assertRaises(DebuggerError): runner.live_registers(d)
                self.assertEqual(len(b.calls), boundary)
                d.close(); self.assertEqual(b.calls[-1], ("close",))
