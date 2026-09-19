# SPDX-License-Identifier: BSD-3-Clause
"""Offline synthetic runner/USB/ABI/artifact checks. No physical USB loading."""
from contextlib import redirect_stderr, redirect_stdout
from dataclasses import dataclass, replace
import hashlib
import io
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import check_radio_tx_hardware as runner
import clock_fixture
import radio_tx_fixture
from cc_debugger import Access, Debugger, DebuggerError, UsbAddress
from debug_image import DebugImage
from radio_tx_fixture import CHECKPOINTS, OBJECTS, check_end, decode, load_image, packet
from test_m1_access import CoreBackend
from verify_firmware import ARTIFACT_EXTENSIONS, ROOT, parse_ihex, parse_symbols

PROGRAM = b"\0"*256+b"\0\x22\0\x80\xfd\0\x80\xfd"
DIGEST = hashlib.sha256(PROGRAM).hexdigest()
PROOF = dict(checkpoints=[256, 258, 261], state=238, mailbox=262, clock=270, fifo=289, tx=310)
IMAGE = SimpleNamespace(
    image_name="radio_tx_fixture", board="generic", sha256=DIGEST, radio_tx_proof=PROOF,
    metrics=dict(image_extent_bytes=len(PROGRAM), iram_stack_start=101),
    symbol=lambda name: SimpleNamespace(address=PROOF["checkpoints"][CHECKPOINTS.index(name)]),
)


class ClockMetadataTests(unittest.TestCase):
    """Small CDB-only controls: no artifact cache, compiler or USB required."""
    def setUp(self):
        self.private = [
            "F:Fclock$observe$0_0$0({2}DF,SV:S),C,0,0,0,0,0",
            "S:Fclock$observe$0_0$0({2}DF,SV:S),C,0,0",
            "L:Fclock$observe$0$0:80", "L:XFclock$observe$0$0:90",
            "S:Fclock$output$0_0$0({3}DG,ST__00000001:S),F,0,0",
            "L:Fclock$output$0_0$0:19",
            "T:Fclock$__00000001[({0}S:S$request$0_0$0({7}ST__00000000:S),Z,0,0)]",
        ]
        for n, shape in (("source", "{1}SC:U"), ("timeout_ticks", "{4}SL:U"),
                         ("poll_limit", "{2}SI:U"), ("diagnostics", "{3}DG,ST__00000001:S")):
            name = f"Lclock.clock_select_init${n}$1_0$25"
            self.private += [f"S:{name}({shape}),F,0,0", f"L:{name}:20"]
        # The private inventory is a multiset even when addresses are equal.
        self.private.append(self.private[-1])
        self.public = [
            "F:G$clock_select_init$0_0$0({2}DF,SC:U),Z,0,0,0,0,0",
            "S:G$clock_select_init$0_0$0({2}DF,SC:U),C,0,0",
            "L:G$clock_select_init$0$0:100", "L:XG$clock_select_init$0$0:120",
        ]
        self.debug = "\n".join(self.private+self.public)+"\n"
        self.digest = hashlib.sha256("\n".join(sorted(self.private)).encode()).hexdigest()
        self.symbols = {"_clock_select_init": 0x100}

    def verify(self, text):
        clock_fixture.verify_clock_metadata(text, self.symbols, self.digest)

    def test_complete_multiset_and_identical_public_duplicates(self):
        self.assertEqual(clock_fixture.private_records(self.debug), "\n".join(sorted(self.private)))
        self.verify(self.debug)
        self.verify(self.debug+"\n".join(self.public)+"\n")
        self.verify(self.debug+"L:G$clock_select_init$0$0:0100\n")
        self.assertEqual(radio_tx_fixture.public_records(self.debug),
                         radio_tx_fixture.public_records(self.debug+"\n".join(self.public)+"\n"))

    def test_helper_declarations_entries_ends_objects_and_fields_fail_closed(self):
        for line in self.private:
            changed = (line.rsplit(":", 1)[0]+":BAD" if line.startswith("L:")
                       else line.replace("({", "({9", 1))
            self.assertNotEqual(changed, line)
            for text in (self.debug.replace(line+"\n", "", 1),
                         self.debug.replace(line, changed, 1), self.debug+changed+"\n",
                         self.debug+line+"\n"):
                with self.subTest(record=line), self.assertRaises(ValueError):
                    self.verify(text)

    def test_raw_helper_and_public_suffixes_are_not_normalized(self):
        for record in self.private+self.public:
            inventory = (radio_tx_fixture.private_records if record in self.private
                         else radio_tx_fixture.public_records)
            original = inventory(self.debug)
            for suffix in ("\r", "\v", "\f", "\x1c", "\x1d", "\x1e", "\x85", "\u2028", "\u2029"):
                for text in (self.debug.replace(record+"\n", record+suffix+"\n", 1),
                             self.debug+record+suffix+"\n"):
                    with self.subTest(record=record, suffix=repr(suffix)):
                        with self.assertRaisesRegex(ValueError, "non-LF line separator"): inventory(text)
                        with self.assertRaisesRegex(ValueError, "non-LF line separator"): self.verify(text)
            self.assertEqual(inventory(self.debug), original)

    def test_prefixed_hostile_duplicates_and_all_non_lf_separators_reject(self):
        for record in self.private+self.public:
            hostile = (record.rsplit(":", 1)[0]+":7FFF" if record.startswith("L:")
                       else record.replace("({", "({9", 1))
            self.assertNotEqual(record, hostile)
            for sep in ("\r", "\v", "\f", "\x1c", "\x1d", "\x1e", "\x85", "\u2028", "\u2029"):
                for text in (self.debug+sep+hostile+"\n",
                             self.debug+"\n"+sep+record+"\n",
                             self.debug+"\nignored"+sep+hostile+"\n"):
                    with self.subTest(record=record, separator=repr(sep)):
                        for inventory in (clock_fixture.private_records, radio_tx_fixture.private_records,
                                          radio_tx_fixture.public_records):
                            with self.assertRaisesRegex(ValueError, "non-LF line separator"): inventory(text)
                        with self.assertRaisesRegex(ValueError, "non-LF line separator"): self.verify(text)

    def test_conflicting_public_returns_and_parameters_cannot_hide(self):
        for line in self.public:
            changed = (line.rsplit(":", 1)[0]+":not-hex" if line.startswith("L:")
                       else line.replace("DF,SC:U", "DF,SV:S"))
            for text in (self.debug.replace(line, changed), self.debug+changed+"\n"):
                with self.subTest(record=line), self.assertRaises(ValueError):
                    self.verify(text)
            self.assertNotEqual(radio_tx_fixture.public_records(self.debug),
                                radio_tx_fixture.public_records(self.debug+changed+"\n"))
        for line in self.private:
            if line.startswith("S:Lclock.clock_select_init$"):
                with self.subTest(parameter=line), self.assertRaises(ValueError):
                    self.verify(self.debug+line.replace("({", "({9", 1)+"\n")

    def test_tx_all_service_board_and_caller_private_record_kinds(self):
        for module in ("clock", "timebase", "radio_tx", "radio_fifo", "startup", "status",
                       "generic", "lg_esl29_rev03", "radio_tx_fixture", "radio_tx_fixture_state"):
            records = [s.replace("clock", module) for s in self.private]
            records += [s for s in records if s.startswith("L:XF")]
            with self.subTest(module=module):
                self.assertEqual(radio_tx_fixture.private_records("\n".join(records)),
                                 "\n".join(sorted(records)))

    def test_genuine_clock_metadata_for_every_coupled_profile(self):
        from fixture_test_artifacts import linked_fixture
        paths = [linked_fixture(board, name) for board in ("generic", "lg_esl29_rev03")
                 for name in ("clock_test", "clock_fixture", "radio_tx_fixture", "radio_fifo_fixture",
                              "dma_fixture", "aes_fixture", "prng_fixture", "radio_rx_fixture")]
        for path in paths:
            image = parse_ihex(path.with_suffix(".ihx").read_text())
            symbols = parse_symbols(path.with_suffix(".map").read_text())
            debug = path.with_suffix(".cdb").read_bytes().decode("utf-8")
            clock_fixture.verify_clock_code(image, symbols, debug)
            for record in clock_fixture.private_records(debug).split("\n"):
                changed = (record.rsplit(":", 1)[0]+":"+f"{int(record.rsplit(':', 1)[1], 16)+1:X}"
                           if record.startswith("L:") else
                           re.sub(r"\(\{(\d+)\}", lambda m: "({"+str(int(m[1])+1)+"}", record, count=1))
                self.assertNotEqual(changed, record)
                for mutation in (debug.replace(record+"\n", "", 1), debug.replace(record, changed, 1),
                                 debug+"\n"+changed+"\n"):
                    with self.subTest(path=path, record=record), self.assertRaises(ValueError):
                        clock_fixture.verify_clock_code(image, symbols, mutation)
            public = [l for l in debug.split("\n") if re.match(r"^[FSL]:(?:X?G)\$clock_select_init\$", l)]
            clock_fixture.verify_clock_code(image, symbols, debug+"\n"+"\n".join(public)+"\n")
            entry = next(l for l in public if l.startswith("L:G$"))
            wrong_entry = entry.rsplit(":", 1)[0]+":"+f"{symbols['_clock_select_init']+1:X}"
            with self.subTest(path=path), self.assertRaises(ValueError):
                clock_fixture.verify_clock_code(image, symbols | {"_clock_select_init": symbols["_clock_select_init"]+1},
                                                debug.replace(entry, wrong_entry))
            for record in public:
                changed = (record.rsplit(":", 1)[0]+":"+f"{int(record.rsplit(':', 1)[1], 16)+1:X}"
                           if record.startswith("L:") else record.replace("DF,SC:U", "DF,SV:S"))
                self.assertNotEqual(changed, record)
                for mutation in (debug.replace(record, changed), debug+"\n"+changed+"\n"):
                    with self.subTest(path=path, record=record), self.assertRaises(ValueError):
                        clock_fixture.verify_clock_code(image, symbols, mutation)

    def test_genuine_clock_board_conflicting_field_and_object_declarations(self):
        from fixture_test_artifacts import linked_fixture
        paths = [linked_fixture(board, "clock_fixture") for board in ("generic", "lg_esl29_rev03")]
        for path in paths:
            image = parse_ihex(path.with_suffix(".ihx").read_text())
            symbols = parse_symbols(path.with_suffix(".map").read_text())
            debug = path.with_suffix(".cdb").read_bytes().decode("utf-8")
            records = [l for l in debug.split("\n") if re.match(
                r"^(?:T:Fclock_fixture_state\$__00000004|S:G\$clock_fixture_state\$)", l)]
            clock_fixture.verify_clock_board(image, symbols, debug+"\n"+"\n".join(records)+"\n")
            for record in records:
                changed = (record.replace("SC:U", "SC:S", 1) if record.startswith("T:") else
                           record.replace("ST__00000004", "ST__00000005"))
                self.assertNotEqual(changed, record)
                for mutation in (debug.replace(record, changed), debug+"\n"+changed+"\n"):
                    with self.subTest(path=path, record=record), self.assertRaises(ValueError):
                        clock_fixture.verify_clock_board(image, symbols, mutation)


def wire(phase=1, *, busy=False, remaining=None):
    r = bytearray(b"M3TX\x01\x18"+bytes(18))
    r[6] = phase; r[11:17] = b"\x08\xff\xff\x1a\x05\x0d"; r[19:21] = b"\x69\x96"
    r[17:19] = (256 if phase < 3 else 0).to_bytes(2, "little")
    if remaining is not None: r[17:19] = remaining.to_bytes(2, "little")
    if phase == 5: r[8:14] = bytes((6, 1, 1, 0, 0, 2 if busy else 0))
    if phase == 6: r[7:14] = bytes((7, 4, 1, 0, 0, 0, 10))
    return bytes(r)


def diagnostics(raw):
    c, f, t = bytearray(19), bytearray(21), bytearray(29)
    if raw[6] in (5, 6):
        c[0] = c[4] = 1; c[14:19] = b"\xc9\x88\x88\x88\x08"
        f[7:9] = b"\x02\x02"; f[20] = 1
        t[6:9] = b"\x07\x0a\x0a"; t[10] = 1; t[12] = int(raw[13] == 0); t[13] = 1
    return bytes(c), bytes(f), bytes(t)


@dataclass(frozen=True)
class Registers:
    pc: int = 0
    sp: int = 102
    dps: int = 0
    psw: int = 0
    a: int = 0x69
    b: int = 0x96
    bank: int = 0
    dptr0: int = 0x1234
    dptr1: int = 0x5678
    mpage: int = 0
    r: tuple = tuple(range(8))


class Synthetic:
    """Runner-level model only. Genuine compiled-C/controller proof is separate."""
    def __init__(self, *, busy=False, fault=False, fail_at=None, bad_code=False,
                 bad_admission=False, bad_clear=False):
        self.records = [wire(), wire(remaining=255), wire(2), wire(3), wire(6 if fault else 5, busy=busy)]
        self.calls = []; self.cursor = -1; self.registers = Registers(); self.mailbox = bytes(8)
        self.fail_at = fail_at; self.bad_code = bad_code
        self.bad_admission, self.bad_clear = bad_admission, bad_clear

    def call(self, name):
        self.calls.append(name)
        if len(self.calls) == self.fail_at: raise OSError("synthetic terminal transport failure")

    def read_adapter_state(self): self.call("adapter"); return None
    def attach_reset(self): self.call("reset")
    def read_pc(self): self.call("pc"); return self.registers.pc
    def read_debug_config(self): self.call("config"); return 0x26
    def read_code(self, a, n):
        self.call("code"); data = PROGRAM[a:a+n]
        return bytes(v ^ 1 for v in data) if self.bad_code else data
    def set_breakpoint(self, *args): self.call("breakpoint")
    def read_registers(self): self.call("registers"); return self.registers
    def step(self):
        self.call("step"); self.registers = replace(self.registers, pc=self.registers.pc+1)
        return SimpleNamespace(accumulator=self.registers.a)
    def resume(self):
        self.call("resume"); self.cursor += 1
        if self.cursor in (2, 3):
            assert self.mailbox == packet("arm" if self.cursor == 2 else "run")
        self.mailbox = bytes(8)
        phase = self.records[self.cursor][6]
        self.registers = replace(self.registers, pc=258 if phase == 5 else 261 if phase == 6 else 256)
    def write_xdata(self, a, data):
        self.call("write"); assert a == 262 and self.cursor in (1, 2) and len(data) == 8
        self.mailbox = data
    def read_xdata(self, a, n):
        self.call("sram"); raw = self.records[self.cursor]
        c, f, t = diagnostics(raw)
        if self.bad_admission and self.cursor == 3: c = b"\1"+c[1:]
        if self.bad_clear and self.cursor == 4: f = f[:13]+b"\x01"+f[14:]
        boot = b"M0CC\x01\x20\x02\0"+bytes((raw[10],))+bytes(15)+b"\xc9\xc9"+bytes(6)
        value = {238: raw, 262: self.mailbox, 270: c, 289: f, 310: t, 0x1e00: boot}[a]
        assert len(value) == n
        return value


class FixtureTests(unittest.TestCase):
    def run_fake(self, fake, admit_only=False):
        with patch.object(runner, "HASHES", {"generic": (len(PROGRAM), DIGEST)}), \
             patch.object(runner, "wait_checkpoint", side_effect=lambda d, *args: d.read_pc()):
            return runner.exercise(fake, IMAGE, PROGRAM, DIGEST, True, admit_only)

    def test_mailbox_and_strict_decoding(self):
        for name in ("arm", "run"):
            data = packet(name)
            self.assertEqual(len(data), 8)
            self.assertEqual(data[2:4], b"\x1a\xe5")
            self.assertTrue(all(data[i] ^ data[i+1] == 255 for i in (0, 2, 4, 6)))
        for bad in ("", None, "direct"):
            with self.assertRaises(ValueError): packet(bad)
        for phase in (1, 2, 3, 5, 6):
            raw = wire(phase); check_end(decode(raw), *diagnostics(raw))
        for index in (0, 1, 2, 3, 4, 5, 14, 15, 16, 19, 20, 21, 22, 23):
            raw = bytearray(wire()); raw[index] ^= 1
            with self.subTest(index=index), self.assertRaises(ValueError): decode(bytes(raw))
        for raw in (b"", bytearray(wire()), wire()+b"\0", wire(4)):
            with self.assertRaises(ValueError): decode(raw)
        old_profile = bytearray(wire()); old_profile[14] = 15
        with self.assertRaisesRegex(ValueError, "profile"): decode(bytes(old_profile))
        for index, value in ((7, 1), (8, 5), (9, 0), (10, 0), (11, 3), (12, 8), (13, 1), (17, 1)):
            raw = bytearray(wire(5)); raw[index] = value
            with self.subTest(index=index), self.assertRaises(ValueError): decode(bytes(raw))

    def test_normal_busy_admitted_and_every_failure_boundary(self):
        fake = Synthetic(); result = self.run_fake(fake)
        self.assertEqual(result["result"], "PHY_DONE")
        self.assertEqual((result["profile_channel"], result["profile_txpower_raw"]), (26, 5))
        self.assertEqual(fake.calls.count("reset"), 1)
        self.assertEqual(fake.calls.count("write"), 2)
        self.assertEqual(fake.calls.count("resume"), 5)
        for boundary in range(1, len(fake.calls)+1):
            failed = Synthetic(fail_at=boundary)
            with self.subTest(boundary=boundary), self.assertRaises(OSError): self.run_fake(failed)
            self.assertEqual(len(failed.calls), boundary)
        self.assertEqual(self.run_fake(Synthetic(busy=True))["result"], "CCA_BUSY")
        admitted = Synthetic()
        self.assertEqual(self.run_fake(admitted, True)["result"], "ADMITTED")
        self.assertEqual(admitted.calls.count("resume"), 4)
        for options, text, resumes in ((dict(bad_code=True), "CODE", 0),
                                      (dict(bad_admission=True), "admission", 4),
                                      (dict(fault=True), "FAULT", 5),
                                      (dict(bad_clear=True), "FIFO", 5)):
            fake = Synthetic(**options)
            with self.subTest(options=options), self.assertRaisesRegex(ValueError, text): self.run_fake(fake)
            self.assertEqual(fake.calls.count("resume"), resumes)
            self.assertEqual(fake.calls.count("reset"), 1)

    def test_explicit_identity_and_permission_before_target(self):
        for permission in (False, 1, None):
            fake = Synthetic()
            with self.assertRaises(ValueError):
                runner.exercise(fake, IMAGE, PROGRAM, DIGEST, permission)
            self.assertFalse(fake.calls)
        with patch.object(runner, "HASHES", {"generic": (len(PROGRAM), DIGEST)}):
            for program in (b"", PROGRAM[:-1], PROGRAM+b"\0", bytearray(PROGRAM)):
                with self.assertRaises(ValueError): runner.validate_program(IMAGE, program, DIGEST, True)
            for name in ("radio_tx_test", "bringup", "radio_rx_fixture"):
                wrong = SimpleNamespace(**(vars(IMAGE) | {"image_name": name}))
                with self.assertRaises(ValueError): runner.validate_program(wrong, PROGRAM, DIGEST, True)
            with self.assertRaises(ValueError): runner.validate_program(IMAGE, PROGRAM, "0"*64, True)
        fake = Synthetic()
        with patch.object(runner.time, "monotonic", side_effect=(0, 61)):
            budget = runner.Deadline(fake)
            with self.assertRaisesRegex(ValueError, "deadline"): budget.read_pc()
        self.assertFalse(fake.calls)
        with patch.object(runner.time, "monotonic", side_effect=(0, 59, 61)):
            budget = runner.Deadline(fake)
            with self.assertRaisesRegex(ValueError, "late"): budget.read_pc()
        self.assertEqual(fake.calls, ["pc"])

    def test_bad_context_packet_and_checkpoint_never_continue(self):
        for corruption in ("context", "packet"):
            fake = Synthetic(); write = fake.write_xdata

            def corrupt(a, data):
                write(a, data)
                if corruption == "context": fake.registers = replace(fake.registers, a=0x68)
                else: fake.mailbox = b"\0"+fake.mailbox[1:]

            fake.write_xdata = corrupt
            with self.subTest(corruption=corruption), self.assertRaisesRegex(ValueError, "packet"):
                self.run_fake(fake)
            self.assertEqual(fake.calls.count("resume"), 2)
        fake = Synthetic(); resume = fake.resume

        def wrong_stop():
            resume(); fake.registers = replace(fake.registers, pc=257)

        fake.resume = wrong_stop
        with self.assertRaisesRegex(ValueError, "DISARMED"): self.run_fake(fake)
        self.assertEqual(fake.calls.count("resume"), 1)
        for field, value in (("dps", 1), ("psw", 8), ("sp", 0x80)):
            fake = Synthetic(); fake.registers = replace(fake.registers, **{field: value})
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, "PC/bank/DPS/SP"):
                self.run_fake(fake)
            self.assertEqual(fake.calls.count("resume"), 1)

    def test_cli_permissions_invalid_artifacts_and_cleanup(self):
        flags = ["--allow-target-reset", "--allow-cpu-control", "--allow-memory-access",
                 "--allow-memory-write", "--allow-breakpoints", "--confirm-rf-one-attempt"]
        args = ["--bus", "1", "--address", "2", "--board", "generic", "--output", "absent-TX-artifacts",
                "--sha256", DIGEST]
        with patch.object(runner.PyUsbBackend, "load") as usb, redirect_stderr(io.StringIO()):
            for omitted in flags:
                self.assertEqual(runner.main(args+[f for f in flags if f != omitted]), 1)
            self.assertEqual(runner.main(args+flags), 1)
            usb.assert_not_called()
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory); (p/"radio_tx_fixture.bin").write_bytes(PROGRAM)
            args[args.index("--output")+1] = directory
            with patch.object(runner, "load_image", return_value=IMAGE), \
                 patch.object(runner, "HASHES", {"generic": (len(PROGRAM), DIGEST)}), \
                 patch.object(runner.PyUsbBackend, "load"), patch.object(runner, "Debugger") as cls, \
                 patch.object(runner, "exercise", return_value={"result": "PHY_DONE"}), \
                 redirect_stdout(io.StringIO()) as out, redirect_stderr(io.StringIO()):
                cls.return_value.__exit__.side_effect = OSError("synthetic cleanup failure")
                self.assertEqual(runner.main(args+flags), 1)
                self.assertEqual(out.getvalue(), "")
                self.assertEqual(cls.call_args.args[1], Access.RESET_DEBUG_SESSION)
                for key in ("allow_target_reset", "allow_cpu_control", "allow_memory_access",
                            "allow_memory_write", "allow_breakpoints"):
                    self.assertIs(cls.call_args.kwargs[key], True)
                self.assertNotIn("allow_dma_enable", cls.call_args.kwargs)

    def test_actual_transport_mailbox_uses_existing_guarded_writer(self):
        backend = CoreBackend()
        mailbox = OBJECTS["mailbox"][0]  # Actual00FB..0102 crosses a low-byte boundary.
        with Debugger(backend, Access.EXISTING_DEBUG_SESSION, clock=backend.clock,
                      allow_memory_access=True, allow_memory_write=True) as d:
            d.open(UsbAddress(1, 2))
            before = d.read_registers()
            for stage in ("arm", "run"):
                d.write_xdata(mailbox, packet(stage))
                self.assertEqual(d.read_xdata(mailbox, 8), packet(stage))
                self.assertEqual(d.read_registers(), before)
            for a in (0x1e00, 0x1f00, 0x6000):
                count = len(backend.calls)
                with self.assertRaises(ValueError): d.write_xdata(a, packet("run"))
                self.assertEqual(len(backend.calls), count)
        denied = CoreBackend()
        with Debugger(denied, Access.EXISTING_DEBUG_SESSION, clock=denied.clock,
                      allow_memory_access=True) as d:
            d.open(UsbAddress(1, 2)); count = len(denied.calls)
            with self.assertRaises(DebuggerError): d.write_xdata(mailbox, packet("arm"))
            self.assertEqual(len(denied.calls), count)

    def test_full_run_stack_statistic_presence_format_and_upper_bound(self):
        with patch.object(sys, "path", [str(ROOT/"tests"), *sys.path]):
            from boot_radio_tx_fixture import stack_high_water
        line = "Max value of stack pointer= 0x000074, avg= 0x000065\n"
        for peak in (0, 0x74, 0x77, 0x7f):
            self.assertEqual(stack_high_water(line.replace("000074", f"{peak:06x}")), peak)
        for text in ("", line.replace("Max value", "Missing value"), line+line,
                     line.replace("000074", "000080"), line.replace("000074", "0000ff"),
                     line.replace("000074", "000100"), line.replace("000074", "00007q"),
                     line.replace("000074", "-00001"), line.replace(", avg= 0x000065", "")):
            with self.subTest(text=text), self.assertRaises(ValueError):
                stack_high_water(text)

    def test_genuine_artifact_and_listing_rejections(self):
        from fixture_test_artifacts import linked_fixture
        for board in ("generic", "lg_esl29_rev03"):
            source = linked_fixture(board, "radio_tx_fixture").parent; image = load_image(source, board)
            self.assertIsInstance(image, DebugImage)
            self.assertEqual(image.metrics["ordinary_xdata_bytes"], 347)
            self.assertEqual(image.metrics["nonaliased_xdata_reserved_bytes"], 411)
            self.assertEqual(image.metrics["iram_stack_reserved_bytes"], 159)
            program = (source/"radio_tx_fixture.bin").read_bytes()
            runner.validate_program(image, program, image.sha256, True)
            with tempfile.TemporaryDirectory() as directory:
                out = Path(directory)
                for suffix in ARTIFACT_EXTENSIONS:
                    shutil.copyfile(source/f"radio_tx_fixture.{suffix}", out/f"radio_tx_fixture.{suffix}")
                shutil.copyfile(source/"build-info.json", out/"build-info.json")
                for module in ("timebase", "clock", "radio_fifo", "radio_tx", "startup", "status",
                               board, "radio_tx_fixture", "radio_tx_fixture_state"):
                    shutil.copyfile(source/f"radio_tx_fixture.{module}.rst",
                                    out/f"radio_tx_fixture.{module}.rst")
                self.assertIsInstance(load_image(out, board), DebugImage)
                for p in sorted(out.iterdir()):
                    original = p.read_bytes(); p.unlink()
                    with self.subTest(board=board, missing=p.name), self.assertRaises(FileNotFoundError):
                        load_image(out, board)
                    p.write_bytes(original)
                for name in [f"radio_tx_fixture.{m}.rst" for m in
                             ("timebase", "clock", "radio_fifo", "radio_tx", "startup", "status",
                              board, "radio_tx_fixture", "radio_tx_fixture_state")]:
                    p = out/name; original = p.read_bytes(); p.write_bytes(b"")
                    with self.subTest(board=board, artifact=name), self.assertRaises(ValueError):
                        load_image(out, board)
                    p.write_bytes(original)
                for suffix in ARTIFACT_EXTENSIONS:
                    p = out/f"radio_tx_fixture.{suffix}"; original = p.read_bytes(); p.write_bytes(b"")
                    with self.subTest(board=board, artifact=suffix), self.assertRaises((ValueError, KeyError)):
                        load_image(out, board)
                    p.write_bytes(original)
                # A valid but different HEX must fail independently of its
                # metadata hash; equivalent HEX with a stale hash also fails.
                p = out/"radio_tx_fixture.hex"; original = p.read_bytes()
                metadata = out/"build-info.json"; original_info = metadata.read_bytes()
                info = json.loads(original_info)
                p.write_bytes(b":0100000000FF\n:00000001FF\n")
                changed_info = json.loads(original_info)
                changed_info["sha256"][p.name] = hashlib.sha256(p.read_bytes()).hexdigest()
                metadata.write_text(json.dumps(changed_info))
                with self.assertRaisesRegex(ValueError, "IHX/HEX content differs"): load_image(out, board)
                metadata.write_bytes(original_info); p.write_bytes(original+b"\n")
                with self.assertRaisesRegex(ValueError, "hash mismatch.*hex"): load_image(out, board)
                p.write_bytes(original)
                malformed = [
                    None, dict(info, schema=True),
                    dict(info, board="lg_esl29_rev03" if board == "generic" else "generic"),
                    dict(info, image="radio_tx_test"), dict(info, compiler="SDCC 4.3.0"),
                    dict(info, sha256={k: v for k, v in info["sha256"].items()
                                      if not k.endswith(".hex")}),
                    dict(info, sha256={**info["sha256"], "radio_tx_test.bin": "0"*64}),
                    dict(info, sha256={**info["sha256"], "radio_tx_fixture.hex": "0"*64}),
                ]
                for value in malformed:
                    metadata.write_text(json.dumps(value))
                    with self.subTest(board=board, metadata=value), self.assertRaises(ValueError):
                        load_image(out, board)
                metadata.write_bytes(original_info)
                # Preserve the old .mem error/summary guards even when all
                # canonical hashes are honestly updated to the damaged file.
                p = out/"radio_tx_fixture.mem"; original = p.read_bytes()
                for mutation in (original+b"\nERROR: synthetic linker failure\n",
                                 original.replace(b"0x015a     347", b"0x015b     348")):
                    self.assertNotEqual(mutation, original)
                    p.write_bytes(mutation); changed_info = json.loads(original_info)
                    changed_info["sha256"][p.name] = hashlib.sha256(mutation).hexdigest()
                    metadata.write_text(json.dumps(changed_info))
                    with self.assertRaisesRegex(ValueError, "linker accounting/error"):
                        load_image(out, board)
                p.write_bytes(original); metadata.write_bytes(original_info)
                # Even with an updated artifact hash, suffixes and hostile
                # prefixed duplicates must fail in the genuine artifact loader.
                p = out/"radio_tx_fixture.cdb"; original = p.read_bytes()
                helpers = [r for r in original.decode("utf-8").split("\n")
                           if re.match(r"^[FSL]:X?Fclock\$observe\$", r)]
                self.assertEqual(len(helpers), 4)
                public = [r for r in original.decode("utf-8").split("\n")
                          if re.match(r"^[FSL]:(?:X?G)\$clock_select_init\$", r)]
                for record in helpers+sorted(set(public)):
                    hostile = (record.rsplit(":", 1)[0]+":7FFF" if record.startswith("L:")
                               else record.replace("({", "({9", 1))
                    for sep in ("\r", "\v", "\f", "\x1c", "\x1d", "\x1e", "\x85", "\u2028", "\u2029"):
                        for mutation in (original.replace((record+"\n").encode(),
                                                          (record+sep+"\n").encode(), 1),
                                         original+("\n"+sep+hostile+"\n").encode(),
                                         original+("\nignored"+sep+hostile+"\n").encode()):
                            self.assertNotEqual(mutation, original)
                            p.write_bytes(mutation); changed_info = json.loads(original_info)
                            changed_info["sha256"][p.name] = hashlib.sha256(mutation).hexdigest()
                            metadata.write_text(json.dumps(changed_info))
                            with self.subTest(board=board, record=record, separator=repr(sep)), \
                                 self.assertRaisesRegex(ValueError, "non-LF line separator"):
                                load_image(out, board)
                p.write_bytes(original); metadata.write_bytes(original_info)
                p = out/"radio_tx_fixture.clock.rst"; original = p.read_text()
                lines = original.splitlines(keepends=True)
                indexes = [n for n, line in enumerate(lines) if re.match(
                    r"^\s+[0-9A-F]{6} (?:[0-9A-F]{2} ){1,3}\s+\[\s*\d+\]", line)]
                a, b = indexes[:2]
                removed = lines[:a]+lines[a+1:]
                duplicated = lines[:a]+[lines[a]]+lines[a:]
                reordered = list(lines); reordered[a], reordered[b] = lines[b], lines[a]
                for mutation in (removed, duplicated, reordered):
                    p.write_text("".join(mutation))
                    with self.assertRaisesRegex(ValueError, "ordered listing"): load_image(out, board)
                p.write_text(original)

    def test_import_orders_and_shared_boot_entry_point(self):
        # Fresh interpreters exercise actual imports, not sys.modules caching.
        # This performs no fixture execution and never loads the USB backend.
        script = (
            "import importlib, inspect, sys\n"
            "sys.path[:0] = ['tools', 'tests']\n"
            "for name in sys.argv[1:]: importlib.import_module(name)\n"
            "import radio_tx_fixture, boot_radio_tx_fixture\n"
            "assert not hasattr(radio_tx_fixture, 'FixtureImage')\n"
            "assert tuple(inspect.signature(boot_radio_tx_fixture.check_radio_tx_fixture).parameters)"
            " == ('simulator', 'output', 'board', 'symbols')\n"
            "assert 'usb.core' not in sys.modules\n"
        )
        for modules in (("radio_tx_fixture", "debug_image", "boot_radio_tx_fixture"),
                        ("debug_image", "radio_tx_fixture", "boot_radio_tx_fixture"),
                        ("verify_firmware", "boot_radio_tx_fixture", "debug_image")):
            with self.subTest(modules=modules):
                completed = subprocess.run([sys.executable, "-B", "-c", script, *modules],
                                           cwd=ROOT, capture_output=True, text=True, timeout=15)
                self.assertEqual(completed.returncode, 0, completed.stdout+completed.stderr)


if __name__ == "__main__":
    unittest.main()
