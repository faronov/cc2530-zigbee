# SPDX-License-Identifier: BSD-3-Clause
"""Offline decoder/runner/artifact tests. No physical backend is loaded."""
from contextlib import redirect_stderr, redirect_stdout
from dataclasses import replace
import io
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import check_radio_link_hardware as runner
import radio_link_fixture as fixture
from fixture_test_artifacts import linked_fixture
from test_radio_tx_fixture import DIGEST, PROGRAM, Synthetic as TxSynthetic
from verify_firmware import parse_ihex, parse_symbols

PROOF = dict(checkpoints=[256, 258, 261],
             **{name: address for name, (address, _) in fixture.OBJECTS.items()})
IMAGE = SimpleNamespace(
    image_name="radio_link_fixture", board="generic", sha256=DIGEST, radio_link_proof=PROOF,
    metrics=dict(image_extent_bytes=len(PROGRAM), iram_stack_start=101),
    symbol=lambda name: SimpleNamespace(address=PROOF["checkpoints"][fixture.CHECKPOINTS.index(name)]),
)


def wire(phase=1, outcome=1, remaining=None, late=False):
    raw = bytearray(b"M3LK\x01\x24"+bytes(30))
    raw[6] = phase; raw[13:16] = b"\x08\xff\xff"
    raw[18:21] = b"\x1a\x05\x0d"; raw[29:31] = b"\x69\x96"
    raw[27:29] = (256 if phase < 3 else 0).to_bytes(2, "little")
    if remaining is not None:
        raw[27:29] = remaining.to_bytes(2, "little")
    if phase == 5:
        raw[8:18] = bytes((7, 1, 1, 1, outcome, 0, 18 if outcome == 4 else 17, 5,
                          int(outcome in (1, 2) or late), 0))
    if phase == 6:
        raw[7:16] = bytes((7, 4, 1, 1, 0, 0, 0, 12, 12))
    return bytes(raw)


def diagnostics(raw):
    clock, radio, frames = bytearray(19), bytearray(26), bytearray(256)
    if raw[6] >= 5:
        clock[14:19] = b"\xc9\x88\x88\x88\x08"
        radio[8:10] = b"\x07\x05"; radio[12] = 1; radio[22] = 4
    if raw[16]:
        frames[:6] = bytes((3, 0x90, 0x40 if raw[12] == 2 else 0xc0, 2, 0, 0x5a))
    return bytes(clock), bytes(radio), bytes(frames)


class Synthetic(TxSynthetic):
    def __init__(self, outcome=1, late=False, fault=False, **kwargs):
        super().__init__(**kwargs)
        self.records = [wire(), wire(remaining=255), wire(2), wire(3),
                        wire(6 if fault else 5, outcome, late=late)]

    def resume(self):
        self.call("resume"); self.cursor += 1
        if self.cursor in (2, 3):
            assert self.mailbox == fixture.packet("arm" if self.cursor == 2 else "run")
        self.mailbox = bytes(8)
        phase = self.records[self.cursor][6]
        self.registers = replace(self.registers, pc=258 if phase == 5 else 261 if phase == 6 else 256)

    def write_xdata(self, address, data):
        self.call("write")
        assert address == PROOF["mailbox"] and self.cursor in (1, 2) and len(data) == 8
        self.mailbox = data

    def read_xdata(self, address, size):
        self.call("sram")
        raw = self.records[self.cursor]; clock, radio, frames = diagnostics(raw)
        if self.bad_admission and self.cursor == 3:
            clock = b"\1"+clock[1:]
        if self.bad_clear and self.cursor == 4:
            radio = radio[:13]+b"\1"+radio[14:]
        boot = b"M0CC\x01\x20\x02\0"+bytes((raw[11],))+bytes(15)+b"\xc9\xc9"+bytes(6)
        values = {PROOF["state"]: raw, PROOF["mailbox"]: self.mailbox, PROOF["clock"]: clock,
                  PROOF["radio"]: radio, PROOF["frames"]: frames, 0x1e00: boot}
        value = values[address]; assert len(value) == size
        return value


class LinkTests(unittest.TestCase):
    def run_fake(self, fake, capture=None, admit_only=False):
        with tempfile.TemporaryFile(mode="w+") as temporary, \
             patch.object(runner, "HASHES", {"generic": (len(PROGRAM), DIGEST)}), \
             patch.object(runner, "wait_checkpoint", side_effect=lambda d, *args: d.read_pc()):
            return runner.exercise(fake, IMAGE, PROGRAM, DIGEST, capture or temporary, True, admit_only)

    def test_decoder_profile_bounds_and_late_drain(self):
        for stage in ("arm", "run"):
            data = fixture.packet(stage)
            self.assertEqual(len(data), 8)
            self.assertTrue(all(data[i] ^ data[i+1] == 255 for i in (0, 2, 4, 6)))
        for phase in (1, 2, 3, 5, 6):
            for outcome in (1, 2, 3, 4):
                raw = wire(phase, outcome); record = fixture.decode(raw)
                clock, radio, frames = diagnostics(raw)
                fixture.check_end(record, clock, radio)
                fixture.decode_frames(record, frames)
        raw = wire(5, 3, late=True); record = fixture.decode(raw)
        self.assertEqual(record["outcome"], 3)
        self.assertEqual(len(fixture.decode_frames(record, diagnostics(raw)[2])), 1)
        for index in (*range(6), 18, 19, 20, *range(29, 36)):
            raw = bytearray(wire()); raw[index] ^= 1
            with self.subTest(index=index), self.assertRaises(ValueError):
                fixture.decode(bytes(raw))
        for index, value in ((6, 4), (8, 6), (9, 0), (10, 0), (11, 0), (12, 0),
                             (13, 1), (14, 0), (15, 4), (16, 0), (17, 2), (28, 1)):
            raw = bytearray(wire(5)); raw[index] = value
            with self.subTest(index=index), self.assertRaises(ValueError):
                fixture.decode(bytes(raw))
        raw = wire(5)
        for index in (0, 2, 6, 128):
            frames = bytearray(diagnostics(raw)[2]); frames[index] ^= 0x80
            with self.subTest(index=index), self.assertRaises(ValueError):
                fixture.decode_frames(fixture.decode(raw), bytes(frames))
        for value in (b"", wire()+b"\0", bytearray(wire())):
            with self.assertRaises(ValueError):
                fixture.decode(value)

    def test_all_outcomes_admission_and_every_transport_failure(self):
        normal = Synthetic(); result = self.run_fake(normal)
        self.assertEqual(result["outcome"], "RX_CRC_GOOD")
        self.assertNotIn("body_hex", result)
        self.assertEqual(normal.calls.count("reset"), 1)
        self.assertEqual(normal.calls.count("resume"), 5)
        for boundary in range(1, len(normal.calls)+1):
            failed = Synthetic(fail_at=boundary)
            with self.subTest(boundary=boundary), self.assertRaises(OSError):
                self.run_fake(failed)
            self.assertEqual(len(failed.calls), boundary)
        for outcome, name in ((2, "RX_CRC_BAD"), (3, "RX_WINDOW_TIMEOUT"), (4, "CCA_BUSY")):
            self.assertEqual(self.run_fake(Synthetic(outcome=outcome))["outcome"], name)
        late = self.run_fake(Synthetic(outcome=3, late=True))
        self.assertEqual((late["outcome"], late["retained_frames"]), ("RX_WINDOW_TIMEOUT", 1))
        admitted = Synthetic()
        self.assertEqual(self.run_fake(admitted, admit_only=True)["outcome"], "ADMITTED")
        self.assertEqual(admitted.calls.count("resume"), 4)
        for options, text, resumes in ((dict(bad_code=True), "CODE", 0),
                                      (dict(bad_admission=True), "admission", 4),
                                      (dict(bad_clear=True), "stop", 5), (dict(fault=True), "FAULT", 5)):
            fake = Synthetic(**options)
            with self.subTest(options=options), self.assertRaisesRegex(ValueError, text):
                self.run_fake(fake)
            self.assertEqual(fake.calls.count("resume"), resumes)
            self.assertEqual(fake.calls.count("reset"), 1)

    def test_private_records_and_capture_failure_stop_before_continuation(self):
        with tempfile.TemporaryFile(mode="w+") as capture:
            self.run_fake(Synthetic(outcome=2), capture)
            capture.seek(0); records = [json.loads(line) for line in capture]
            self.assertEqual(len(records), 6)
            self.assertEqual(records[-1]["frames_hex"][:12], "03904002005a")
        for boundary in range(1, 7):
            fake = Synthetic()
            with patch.object(runner, "save", side_effect=[None]*(boundary-1)+[OSError("private write failure")]):
                with self.assertRaisesRegex(OSError, "private write"):
                    self.run_fake(fake)
            self.assertEqual(fake.calls.count("resume"), max(0, boundary-1))

    def test_permissions_program_and_cli_preflight_precede_backend(self):
        for permission in (False, None, 1):
            fake = Synthetic()
            with self.assertRaises(ValueError):
                runner.exercise(fake, IMAGE, PROGRAM, DIGEST, None, permission)
            self.assertFalse(fake.calls)
        flags = ["--allow-target-reset", "--allow-cpu-control", "--allow-memory-access",
                 "--allow-memory-write", "--allow-breakpoints", "--confirm-rf-including-autoack"]
        args = ["--bus", "1", "--address", "2", "--board", "generic", "--output", "absent-link-artifacts",
                "--sha256", DIGEST, "--capture", "/absent-private-link/capture.jsonl"]
        with patch.object(runner.PyUsbBackend, "load") as usb, redirect_stderr(io.StringIO()):
            for missing in flags:
                self.assertEqual(runner.main(args+[f for f in flags if f != missing]), 1)
            self.assertEqual(runner.main(args+flags), 1)
            usb.assert_not_called()
        with patch.object(runner, "HASHES", {"generic": (len(PROGRAM), DIGEST)}):
            for program in (b"", PROGRAM[:-1], PROGRAM+b"\0", bytearray(PROGRAM)):
                with self.assertRaises(ValueError):
                    runner.validate_program(IMAGE, program, DIGEST, True)
            with self.assertRaises(ValueError):
                runner.validate_program(IMAGE, PROGRAM, "0"*64, True)
            for name in ("radio_autoack_test", "radio_tx_fixture", "bringup"):
                wrong = SimpleNamespace(**(vars(IMAGE) | {"image_name": name}))
                with self.assertRaises(ValueError):
                    runner.validate_program(wrong, PROGRAM, DIGEST, True)

    def test_cleanup_failure_never_prints_success_or_received_body(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory); (path/"radio_link_fixture.bin").write_bytes(PROGRAM)
            args = ["--bus", "1", "--address", "2", "--board", "generic", "--output", directory,
                    "--sha256", DIGEST, "--capture", str(path/"private.jsonl"),
                    "--allow-target-reset", "--allow-cpu-control", "--allow-memory-access",
                    "--allow-memory-write", "--allow-breakpoints", "--confirm-rf-including-autoack"]
            with patch.object(runner, "load_image", return_value=IMAGE), \
                 patch.object(runner, "HASHES", {"generic": (len(PROGRAM), DIGEST)}), \
                 patch.object(runner.PyUsbBackend, "load"), patch.object(runner, "Debugger") as cls, \
                 patch.object(runner, "exercise", return_value={"outcome": "RX_CRC_GOOD"}), \
                 redirect_stdout(io.StringIO()) as out, redirect_stderr(io.StringIO()):
                cls.return_value.__exit__.side_effect = OSError("synthetic cleanup failure")
                self.assertEqual(runner.main(args), 1)
                self.assertEqual(out.getvalue(), "")
                self.assertNotIn("allow_dma_enable", cls.call_args.kwargs)

    def test_fresh_both_board_artifacts_and_snapshot_rejection(self):
        for board in fixture.HASHES:
            path = linked_fixture(board, "radio_link_fixture")
            image = fixture.load_image(path.parent, board)
            self.assertEqual(image.metrics["nonaliased_xdata_reserved_bytes"], 814)
            code = parse_ihex(path.with_suffix(".ihx").read_text())
            symbols = parse_symbols(path.with_suffix(".map").read_text())
            listings = {m: (path.parent/f"radio_link_fixture.{m}.rst").read_text()
                        for m in fixture.modules(board)}
            for name in fixture.modules(board):
                with self.subTest(board=board, name=name), self.assertRaises(ValueError):
                    fixture.verify_listings(listings | {name: ""}, code, symbols, board)
            raw = path.with_suffix(".cdb").read_bytes().decode("ascii")
            for altered in (raw.replace("\n", "\r\n"), raw+"\0", raw+"\n"):
                with self.assertRaises(ValueError):
                    fixture.verify_fixture(code, symbols, altered, board)


if __name__ == "__main__":
    unittest.main()
