# SPDX-License-Identifier: BSD-3-Clause
"""Offline manual-operator ordering/failure checks; never import a USB backend."""
from contextlib import redirect_stderr, redirect_stdout
from dataclasses import dataclass, replace
import hashlib
import io
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import check_radio_noise_hardware as runner
from cc_debugger import Access, Debugger, DebuggerError, State, TransportError, UsbAddress
from test_dma_fixture import ControllerBackend
from test_m1_access import CoreBackend


PROGRAM = bytes(256) + b"\0\x22\0\x80\xfd\0\x80\xfd"
DIGEST = hashlib.sha256(PROGRAM).hexdigest()
PROOF = dict(checkpoints=[256, 258, 261], state=0x400, command=0x480,
             capture=0x500, health=0x600, clock=0x640)
IMAGE = SimpleNamespace(
    image_name="radio_noise_fixture", board="generic", sha256=DIGEST, radio_noise_proof=PROOF,
    metrics=dict(image_extent_bytes=len(PROGRAM), iram_stack_start=0x50),
    symbol=lambda name: SimpleNamespace(address=PROOF["checkpoints"][runner.CHECKPOINTS.index(name)]),
)


@dataclass(frozen=True)
class Registers:
    pc: int = 0
    sp: int = 0x51
    dps: int = 0
    psw: int = 0
    bank: int = 1
    a: int = 0x69
    b: int = 0x96
    dptr0: int = 0x1234
    dptr1: int = 0x5678
    r: tuple = tuple(range(8))


class Synthetic:
    def __init__(self, *, fail_at=None, fault=False, health_failure=False, bad_code=False):
        self.calls = []
        self.registers = Registers()
        self.index = -1
        self.command = bytes(len(runner.ARM))
        self.fail_at, self.fault = fail_at, fault
        self.health_failure, self.bad_code = health_failure, bad_code
        bits = [int(v) for v in ("110" * 153 + "10" * 282 + "1")] if health_failure else [0, 1] * 512
        self.data = bytes(sum(bits[i + j] << j for j in range(8)) for i in range(0, 1024, 8))

    def call(self, name):
        self.calls.append(name)
        if len(self.calls) == self.fail_at:
            raise OSError("synthetic transport failure")

    def read_adapter_state(self):
        self.call("adapter")
        return SimpleNamespace(target_id=0x2530)

    def attach_reset(self): self.call("reset")
    def read_pc(self): self.call("pc"); return self.registers.pc
    def read_debug_config(self): self.call("config"); return 0x26
    def set_breakpoint(self, *args): self.call("breakpoint")
    def read_registers(self): self.call("registers"); return self.registers

    def read_code(self, address, size):
        self.call("code")
        data = PROGRAM[address:address + size]
        return bytes(v ^ 1 for v in data) if self.bad_code else data

    def step(self):
        self.call("step")
        self.registers = replace(self.registers, pc=self.registers.pc + 1)
        return SimpleNamespace(accumulator=self.registers.a)

    def resume(self):
        self.call("resume")
        self.index += 1
        if self.index >= 2:
            assert self.command == (runner.ARM if self.index == 2 else runner.RUN)
        self.command = bytes(len(runner.ARM))
        pc = 261 if self.index == 3 and self.fault else 258 if self.index == 3 else 256
        self.registers = replace(self.registers, pc=pc)

    def write_xdata(self, address, data):
        self.call("write")
        assert address == PROOF["command"] and self.index in (1, 2)
        self.command = data

    def read_xdata(self, address, size):
        self.call("sram")
        ended = self.index == 3
        boot = b"M0CC\x01\x20\x02\0" + bytes((int(ended and not self.fault),)) + bytes(15) + b"\xc9\xc9" + bytes(6)
        health = bytearray(runner.HEALTH_SIZE)
        if ended:
            values = (21, 589, 1, 589, 1024, 0) if self.health_failure else (21, 589, 1, 0, 0, 0)
            health[:] = b"".join(v.to_bytes(2, "little") for v in values)
            health += bytes((1, 1, 4)) if self.health_failure else bytes((1, 0, 2))
        clock = bytes(19) if self.index < 2 else b"\1\0\0\0\1\0\0" + bytes(7) + b"\xc9\x88\x88\x88\x08"
        values = {
            PROOF["state"]: bytes((self.index,)) + bytes(runner.SIZE - 1),
            PROOF["capture"]: bytes((self.index,)) + bytes(runner.CAPTURE_SIZE - 129) + self.data,
            PROOF["command"]: self.command,
            PROOF["health"]: bytes(health),
            PROOF["clock"]: clock,
            0x1e00: boot,
        }
        # The service output is untouched until RUN; only this synthetic
        # runner-level decoder uses the checkpoint index as a sentinel.
        if not ended:
            values[PROOF["capture"]] = bytes(runner.CAPTURE_SIZE)
        data = values[address]
        assert len(data) == size
        return data

    def decode(self, raw):
        index = raw[0]
        ended = index == 3
        return dict(phase="FAULT" if ended and self.fault else
                    ("DISARMED", "DISARMED", "ADMITTED", "END")[index],
                    attempts=int(ended), result=8 if ended and self.fault else 0 if ended else 255,
                    health_result=4 if ended and self.health_failure else 0,
                    first_failure=1024 if ended and self.health_failure else 0)

    def acquisition(self, raw):
        ended = self.index == 3
        return dict(samples=1024 if ended else 0, timed_samples=1024 if ended else 0,
                    phase=5 if ended else 0, actions=3 if ended else 0, rx_enable=0, errors=0,
                    elapsed_ticks=4096 if ended else 0, polls=2070 if ended else 0,
                    first_before=20 if ended else 0, last_after=4090 if ended else 0, last_raw=1 if ended else 0,
                    min_gap=1 if ended else 0, max_gap=2 if ended else 0, max_span=2 if ended else 0)

    def live(self, debugger):
        self.call("live")
        values = dict.fromkeys(runner.LIVE, 0)
        values[0xc6] = values[0x9e] = 0xc9 if self.index < 2 else 0x88
        values[0xbe] = 4
        return bytes(values[a] for a in runner.LIVE)


class OperatorTests(unittest.TestCase):
    def run_fake(self, fake, *, capture=None, admit_only=False):
        output = capture or io.StringIO()
        with patch.object(runner, "decode", side_effect=fake.decode), \
             patch.object(runner, "decode_capture", side_effect=fake.acquisition), \
             patch.object(runner, "read_live", side_effect=fake.live), \
             patch.object(runner.os, "fsync"), \
             patch.object(runner, "wait_checkpoint", side_effect=lambda d, *args: d.read_pc()):
            if capture is None:
                output.fileno = lambda: 99
            result = runner.exercise(fake, IMAGE, PROGRAM, DIGEST, output, True, admit_only)
        return result, output

    def test_one_capture_and_all_transport_boundaries(self):
        fake = Synthetic()
        result, output = self.run_fake(fake)
        self.assertEqual(result["result"], "RAW_CAPTURE_STOPPED")
        self.assertFalse(result["entropy_qualified"])
        self.assertNotIn("data_hex", result)
        self.assertEqual(fake.calls.count("reset"), 1)
        self.assertEqual(fake.calls.count("resume"), 4)
        self.assertEqual(fake.calls.count("write"), 2)
        self.assertEqual([json.loads(s)["stage"] for s in output.getvalue().splitlines()],
                         ["boot", "empty", "arm", "run"])
        for boundary in range(1, len(fake.calls) + 1):
            failed = Synthetic(fail_at=boundary)
            with self.subTest(boundary=boundary), self.assertRaises(OSError):
                self.run_fake(failed)
            self.assertEqual(len(failed.calls), boundary)

    def test_independent_raw_prefix_health_boundaries(self):
        healthy = runner.diagnostic_health(b"\xaa" * 128)
        self.assertEqual(healthy[:2], (0, 0))
        self.assertEqual(healthy[2][10:12], b"\0\0")
        self.assertEqual(healthy[2][14], 2)
        for value in (0, 255):
            result, first, context = runner.diagnostic_health(bytes((value,)) * 128)
            self.assertEqual((result, first), (3, 21))
            self.assertEqual(int.from_bytes(context[10:12], "little"), 1003)
        data = Synthetic(health_failure=True).data
        result, first, context = runner.diagnostic_health(data)
        self.assertEqual((result, first, context[14]), (4, 1024, 4))
        self.assertEqual(context[10:12], b"\0\0")
        for invalid in (b"", b"\0" * 127, b"\0" * 129, bytearray(128)):
            with self.assertRaises(ValueError):
                runner.diagnostic_health(invalid)

    def test_fault_is_saved_before_rejection_and_health_failure_is_not_acquisition_failure(self):
        fake = Synthetic(fault=True)
        capture = io.StringIO(); capture.fileno = lambda: 99
        with self.assertRaisesRegex(ValueError, "IRND FAULT"):
            self.run_fake(fake, capture=capture)
        self.assertEqual(json.loads(capture.getvalue().splitlines()[-1])["stage"], "run")
        self.assertEqual(fake.calls.count("reset"), 1)
        self.assertEqual(fake.calls.count("resume"), 4)
        result, _ = self.run_fake(Synthetic(health_failure=True))
        self.assertEqual(result["health_result"], 4)
        self.assertEqual(result["first_failure"], 1024)
        admitted = Synthetic()
        result, _ = self.run_fake(admitted, admit_only=True)
        self.assertEqual(result["result"], "ADMITTED")
        self.assertEqual(result["attempts"], 0)
        self.assertEqual(admitted.calls.count("resume"), 3)

    def test_capture_failure_stops_before_next_resume(self):
        for failure in ("write", "flush", "fsync"):
            fake = Synthetic()
            capture = io.StringIO(); capture.fileno = lambda: 99
            if failure in ("write", "flush"):
                setattr(capture, failure, lambda *args: (_ for _ in ()).throw(OSError("capture failed")))
                with self.assertRaises(OSError):
                    self.run_fake(fake, capture=capture)
            else:
                with patch.object(runner, "decode", side_effect=fake.decode), \
                     patch.object(runner, "decode_capture", side_effect=fake.acquisition), \
                     patch.object(runner, "read_live", side_effect=fake.live), \
                     patch.object(runner, "wait_checkpoint", side_effect=lambda d, *args: d.read_pc()), \
                     patch.object(runner.os, "fsync", side_effect=OSError("capture failed")):
                    with self.assertRaises(OSError):
                        runner.exercise(fake, IMAGE, PROGRAM, DIGEST, capture, True)
            self.assertEqual(fake.calls.count("resume"), 1)

    def test_permissions_program_checkpoint_and_context_fail_closed(self):
        for permission in (False, 1, None):
            fake = Synthetic()
            with self.assertRaises(ValueError):
                runner.exercise(fake, IMAGE, PROGRAM, DIGEST, io.StringIO(), permission)
            self.assertFalse(fake.calls)
        for program in (b"", PROGRAM[:-1], PROGRAM + b"\0", bytearray(PROGRAM)):
            with self.assertRaises(ValueError):
                runner.validate_program(IMAGE, program, DIGEST, True)
        for name in ("radio_noise_test", "noise_health_test", "bringup"):
            image = SimpleNamespace(**(vars(IMAGE) | {"image_name": name}))
            with self.assertRaises(ValueError):
                runner.validate_program(image, PROGRAM, DIGEST, True)
        with self.assertRaises(ValueError):
            runner.validate_program(IMAGE, PROGRAM, "0" * 64, True)
        fake = Synthetic(bad_code=True)
        with self.assertRaisesRegex(ValueError, "CODE"):
            self.run_fake(fake)
        self.assertNotIn("resume", fake.calls)
        for field, value in (("sp", 128), ("dps", 1), ("psw", 8)):
            fake = Synthetic()
            fake.registers = replace(fake.registers, **{field: value})
            with self.assertRaisesRegex(ValueError, "PC/bank/DPS/SP"):
                self.run_fake(fake)
            self.assertEqual(fake.calls.count("resume"), 1)
        for damage in ("packet", "context"):
            fake = Synthetic()
            write = fake.write_xdata

            def corrupt(address, data):
                write(address, data)
                if damage == "packet": fake.command = bytes(len(data))
                else: fake.registers = replace(fake.registers, a=0)

            fake.write_xdata = corrupt
            with self.assertRaisesRegex(ValueError, "packet"):
                self.run_fake(fake)
            self.assertEqual(fake.calls.count("resume"), 2)

    def test_live_gpio_flags_and_monotonic_stif(self):
        values = dict.fromkeys(runner.LIVE, 0)
        values.update({0xc6: 0xc9, 0x9e: 0xc9, 0xbe: 4, 0xfd: 0xbc, 0xfe: 2, 0x8f: 0xbc})
        pack = lambda v: bytes(v[a] for a in runner.LIVE)
        initial = pack(values)
        runner.check_live(initial, "lg_esl29_rev03", 0xc9)
        values.update({0xc6: 0x88, 0x9e: 0x88, 0xc0: 0x80})
        active = pack(values)
        runner.check_live(active, "lg_esl29_rev03", 0x88, initial)
        runner.check_live(active, "lg_esl29_rev03", 0x88, active)
        for address in runner.GPIO + runner.FLAGS + (0xa8, 0xb8, 0x9a, 0xd6, 0xd7, 0xd1, 0xc6, 0x9e, 0xbe):
            with self.subTest(address=address), self.assertRaises(ValueError):
                runner.check_live(pack(values | {address: values[address] ^ 1}),
                                  "lg_esl29_rev03", 0x88, active)
        for address, bit in ((0x80, 0x80), (0x90, 2), (0xc0, 0x80)):
            with self.assertRaises(ValueError):
                runner.check_live(pack(values | {address: values[address] ^ bit}),
                                  "lg_esl29_rev03", 0x88, active)
        fake = Synthetic(); live = fake.live
        def changed(debugger):
            data = bytearray(live(debugger))
            if fake.index == 2: data[runner.LIVE.index(0xf1)] = 1
            return bytes(data)
        fake.live = changed
        with self.assertRaisesRegex(ValueError, "GPIO/unrelated"):
            self.run_fake(fake)
        self.assertEqual(fake.calls.count("resume"), 3)

    def test_live_reader_exact_whitelist_and_all_usb_failure_boundaries(self):
        def session(bank=0, dps=0, permission=True, config=0x26):
            backend = ControllerBackend(register_bank=bank, dps=dps)
            backend.config = config
            backend.sfr.update({a: (i * 17 + 3) & 255 for i, a in enumerate(runner.LIVE)})
            debugger = Debugger(backend, Access.EXISTING_DEBUG_SESSION, 10, backend.clock,
                                allow_memory_access=permission)
            debugger.open(UsbAddress(1, 2)); backend.calls.clear()
            return debugger, backend
        for bank in range(4):
            for dps in range(2):
                d, b = session(bank, dps)
                before = b.cpu_image(), bytes(b.ram)
                self.assertEqual(runner.read_live(d), bytes(b.sfr[a] for a in runner.LIVE))
                self.assertEqual((b.cpu_image(), bytes(b.ram)), before)
                reads = [p[3] for p in b.packets() if len(p) == 4 and p[:3] == b"\x7f\x56\xe5"
                         and p[3] in runner.LIVE]
                self.assertEqual(reads, list(runner.LIVE))
                count = len(b.calls); d.close()
        for late in (False, True):
            for boundary in range(1, count + 1):
                d, b = session()
                if late: b.late_at = boundary
                else: b.fail_at = boundary
                with self.subTest(late=late, boundary=boundary), self.assertRaises(TransportError):
                    runner.read_live(d)
                self.assertEqual(d.state, State.FAULTED)
                self.assertEqual(len(b.calls), boundary)
                with self.assertRaises(DebuggerError): runner.read_live(d)
                self.assertEqual(len(b.calls), boundary)
                d.close()
        for config in range(256):
            if config == 0x26: continue
            d, b = session(config=config)
            with self.assertRaises(ValueError): runner.read_live(d)
            self.assertFalse(any(p[:2] in (b"\x7f\x56", b"\xaf\x57", b"\x4f\x55") for p in b.packets()))
            d.close()
        d, b = session(permission=False)
        with self.assertRaises(DebuggerError): runner.read_live(d)
        self.assertEqual(b.calls, [])
        d.close()

    def test_real_guarded_transport_writes_only_ordinary_sram(self):
        backend = CoreBackend()
        with Debugger(backend, Access.EXISTING_DEBUG_SESSION, clock=backend.clock,
                      allow_memory_access=True, allow_memory_write=True) as d:
            d.open(UsbAddress(1, 2))
            before = d.read_registers()
            for data in (runner.ARM, runner.RUN):
                d.write_xdata(0xfb, data)
                self.assertEqual(d.read_xdata(0xfb, len(data)), data)
                self.assertEqual(d.read_registers(), before)
            for address in (0x1e00, 0x1f00, 0x6000):
                count = len(backend.calls)
                with self.assertRaises(ValueError): d.write_xdata(address, runner.RUN)
                self.assertEqual(len(backend.calls), count)
        backend = CoreBackend()
        with Debugger(backend, Access.EXISTING_DEBUG_SESSION, clock=backend.clock,
                      allow_memory_access=True) as d:
            d.open(UsbAddress(1, 2)); count = len(backend.calls)
            with self.assertRaises(DebuggerError): d.write_xdata(0xfb, runner.ARM)
            self.assertEqual(len(backend.calls), count)

    def test_admission_clock_and_terminal_metadata_are_not_success_shaped(self):
        for damaged in ("clock", "cutoffs", "startup", "stop", "samples", "last_raw", "timing"):
            fake = Synthetic()
            read = fake.read_xdata
            acquisition = fake.acquisition

            def corrupt_read(address, size):
                data = bytearray(read(address, size))
                if address == PROOF["clock"] and fake.index == 2 and damaged == "clock":
                    data[16] = 0xc9
                if address == PROOF["health"] and fake.index == 3:
                    if damaged == "cutoffs": data[0] ^= 1
                    if damaged == "startup": data[14] = 1
                return bytes(data)

            def corrupt_acquisition(raw):
                data = acquisition(raw)
                if fake.index == 3 and damaged == "stop": data["rx_enable"] = 0x80
                if fake.index == 3 and damaged == "samples": data["timed_samples"] -= 1
                if fake.index == 3 and damaged == "last_raw": data["last_raw"] ^= 1
                if fake.index == 3 and damaged == "timing": data["last_after"] = 0x1000000
                return data

            fake.read_xdata = corrupt_read
            fake.acquisition = corrupt_acquisition
            with self.subTest(damaged=damaged), self.assertRaises(ValueError):
                self.run_fake(fake)
            self.assertEqual(fake.calls.count("resume"), 3 if damaged == "clock" else 4)

    def test_operator_binds_fresh_genuine_images_for_both_boards(self):
        from fixture_test_artifacts import linked_fixture
        for board in runner.BOARDS:
            output = linked_fixture(board, "radio_noise_fixture").parent
            image = runner.DebugImage(output, board, "radio_noise_fixture")
            program = (output / "radio_noise_fixture.bin").read_bytes()
            runner.validate_program(image, program, image.sha256, True)
            with self.assertRaises(ValueError):
                runner.validate_program(image, program[:-1], image.sha256, True)

    def test_cli_permissions_artifacts_private_path_and_cleanup_prevent_success(self):
        flags = ["--allow-target-reset", "--allow-cpu-control", "--allow-memory-access",
                 "--allow-memory-write", "--allow-breakpoints", "--confirm-one-irnd-capture"]
        args = ["--bus", "1", "--address", "2", "--board", "generic", "--output", "missing-irnd",
                "--sha256", DIGEST, "--capture", "not-an-absolute-private-path"]
        with patch.object(runner.PyUsbBackend, "load") as usb, redirect_stderr(io.StringIO()):
            for omitted in flags:
                self.assertEqual(runner.main(args + [f for f in flags if f != omitted]), 1)
            self.assertEqual(runner.main(args + flags), 1)
            usb.assert_not_called()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "radio_noise_fixture.bin").write_bytes(PROGRAM)
            args[args.index("--output") + 1] = directory
            with patch.object(runner, "DebugImage", return_value=IMAGE), \
                 patch.object(runner.PyUsbBackend, "load") as usb, redirect_stderr(io.StringIO()):
                self.assertEqual(runner.main(args + flags), 1)
                usb.assert_not_called()
            args[args.index("--capture") + 1] = str(root / "new-private.jsonl")
            with patch.object(runner, "DebugImage", return_value=IMAGE), \
                 patch.object(runner.PyUsbBackend, "load"), patch.object(runner, "Debugger") as cls, \
                 patch.object(runner, "exercise", return_value={"result": "RAW_CAPTURE_STOPPED"}), \
                 redirect_stdout(io.StringIO()) as output, redirect_stderr(io.StringIO()):
                cls.return_value.__exit__.side_effect = OSError("cleanup failed")
                self.assertEqual(runner.main(args + flags), 1)
                self.assertEqual(output.getvalue(), "")
                self.assertEqual(cls.call_args.args[1], Access.RESET_DEBUG_SESSION)
                self.assertTrue(all(cls.call_args.kwargs[key] for key in
                                    ("allow_target_reset", "allow_cpu_control", "allow_memory_access",
                                     "allow_memory_write", "allow_breakpoints")))
