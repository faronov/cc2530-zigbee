# SPDX-License-Identifier: BSD-3-Clause
"""Original synthetic clock ABI records and offline CLI tests."""

from contextlib import redirect_stderr, redirect_stdout
from io import StringIO
import json
import unittest
from unittest.mock import patch

import debug_image
from debug_image import decode_clock_fixture
from verify_firmware import IMAGES, cdb_address


def clock_record(*, phase=3, stage=1, steps=2, result=0, rollback=8, reason=0):
    data = bytearray(56)
    data[:6] = b"M2CK\x01\x38"
    data[6:12] = bytes((phase, reason, stage, int(stage == 1), steps, result))
    data[12:17] = b"\0\x04\0\0\x10"
    data[36] = data[42] = 4
    data[54:] = b"\x69\x96"
    command = 0x88 if stage == 1 else 0xc9
    saved = 0x88 if stage == 2 else 0xc9
    data[31:36] = bytes((saved, command, command, command, rollback))
    data[40:42] = bytes((command, command))
    if stage:
        data[17:24] = b"\x06\0\0\0\x02\0\0"
    if phase in (1, 2):
        data[11] = 8
        data[17:36] = b"\0" * 18 + b"\x08"
        data[40:42] = b"\xc9\xc9"
    if phase == 4 and reason == 1:
        data[17:24] = b"\0\x08\0\0\x01\0\0"
        data[24:31] = b"\x01\0\0\0\x01\0\0"
        data[33:36] = bytes((0xc9, 0xc9, rollback))
        data[40:42] = b"\xc9\xc9"
    return bytes(data)


class ClockRecordTests(unittest.TestCase):
    def test_complete_initial_ready_running_and_terminal_timeout(self):
        self.assertEqual(decode_clock_fixture(clock_record(phase=1, stage=0, steps=0))["phase"], 1)
        for stage in range(3):
            record = decode_clock_fixture(clock_record(stage=stage, steps=stage + 1))
            self.assertEqual(record["requested_source"], int(stage == 1))
        running = clock_record(phase=2, stage=1, steps=1)
        with self.assertRaisesRegex(ValueError, "in progress"):
            decode_clock_fixture(running)
        self.assertEqual(decode_clock_fixture(running, allow_running=True)["phase"], 2)
        fault = decode_clock_fixture(clock_record(phase=4, reason=1, result=3, rollback=0, steps=1))
        self.assertEqual(fault["clock_result"], 3)
        self.assertEqual(fault["diagnostics"]["rollback_result"], 0)

    def test_shape_phase_fields_bounds_and_every_guard_byte_fail_closed(self):
        valid = clock_record()
        for data in (None, bytearray(valid), valid[:-1], valid + b"\0"):
            with self.subTest(shape=type(data)), self.assertRaises(ValueError):
                decode_clock_fixture(data)
        mutations = [(0, 0), (4, 2), (5, 55), (6, 0), (6, 2), (7, 1), (8, 3), (9, 0),
                     (11, 3), (12, 1), (15, 1), (20, 1), (22, 17), (23, 1),
                     (24, 1), (31, 0x89), (32, 0x80), (33, 0xc9), (34, 0xc9), (35, 0),
                     (36, 0), (37, 1), (40, 0xc9), (41, 0xc9), (42, 0x84), (43, 1)]
        mutations += [(i, valid[i] ^ 1) for i in range(46, 56)]
        for index, value in mutations:
            data = valid[:index] + bytes([value]) + valid[index + 1:]
            with self.subTest(index=index, value=value), self.assertRaises(ValueError):
                decode_clock_fixture(data)
        for elapsed in (1025, 0x800000):
            data = valid[:17] + elapsed.to_bytes(4, "little") + valid[21:]
            with self.assertRaises(ValueError):
                decode_clock_fixture(data)

    def test_idempotent_stage_cannot_hide_polling_or_rollback(self):
        valid = clock_record(stage=0, steps=1)
        for index in (17, 21, 23, 24, 28, 30):
            data = valid[:index] + b"\x01" + valid[index + 1:]
            with self.subTest(index=index), self.assertRaises(ValueError):
                decode_clock_fixture(data)

    def test_fault_reason_helper_rollback_and_elapsed_consistency(self):
        valid = clock_record(phase=4, reason=1, result=3, rollback=0, steps=1)
        for index, value in ((7, 0), (11, 0), (11, 8), (18, 0), (23, 2), (25, 8),
                             (28, 0), (30, 2), (33, 0x88), (34, 0x88), (35, 8), (35, 1)):
            data = valid[:index] + bytes([value]) + valid[index + 1:]
            with self.subTest(index=index, value=value), self.assertRaises(ValueError):
                decode_clock_fixture(data)
        for cause in (4, 5):
            data = bytearray(valid)
            data[35] = cause
            if cause == 4:
                data[24:31] = b"\0\0\0\0\0\x10\0"
            else:
                data[24:31] = b"\0\x04\x80\0\x01\0\x02"
            self.assertEqual(decode_clock_fixture(bytes(data))["diagnostics"]["rollback_result"], cause)
        invalid = bytearray(clock_record(phase=1, stage=0, steps=0))
        invalid[6], invalid[7], invalid[11] = 4, 1, 1
        self.assertEqual(decode_clock_fixture(bytes(invalid))["clock_result"], 1)

    def test_clock_cli_never_accepts_another_image(self):
        for image_name in IMAGES:
            for command in ("clock-state", "clock-checkpoint"):
                with self.subTest(image=image_name, command=command), patch.object(debug_image, "DebugImage") as cls:
                    image = cls.return_value
                    image.board, image.image_name, image.sha256 = "generic", image_name, "synthetic"
                    image.clock_timeout_checkpoint = {"address": 0x180}
                    output, error = StringIO(), StringIO()
                    args = [command, "--output", "unused", "--board", "generic", "--image", image_name]
                    if command == "clock-state":
                        args += ["--hex", clock_record().hex()]
                    with redirect_stdout(output), redirect_stderr(error):
                        result = debug_image.main(args)
                    self.assertEqual(result, 0 if image_name == "clock_fixture" else 1)
                    if result == 0:
                        self.assertEqual(json.loads(output.getvalue())["timeout_checkpoint"]["address"], 0x180)
                    else:
                        self.assertEqual(output.getvalue(), "")
                        self.assertIn("requires a clock_fixture", error.getvalue())

    def test_unconfirmed_cancellation_is_not_success_and_requires_an_exhausted_bound(self):
        data = bytearray(clock_record(phase=4, reason=1, result=3, rollback=9, steps=1))
        for elapsed, polls in ((1024, 1), (1025, 2), (0, 4096)):
            data[24:30] = elapsed.to_bytes(4, "little") + polls.to_bytes(2, "little")
            self.assertEqual(decode_clock_fixture(bytes(data))["diagnostics"]["rollback_result"], 9)
        for elapsed, polls in ((1023, 4095), (0x800000, 4096), (1024, 0)):
            data[24:30] = elapsed.to_bytes(4, "little") + polls.to_bytes(2, "little")
            with self.assertRaises(ValueError):
                decode_clock_fixture(bytes(data))
        data[24:30] = b"\0\x04\0\0\x01\0"
        for index, value in ((30, 2), (33, 0x88), (34, 0x89), (6, 3), (11, 9)):
            changed = bytes(data[:index] + bytes([value]) + data[index + 1:])
            with self.assertRaises(ValueError):
                decode_clock_fixture(changed)

    def test_cdb_checkpoint_records_are_exact_not_nearest_or_partial(self):
        key = "L:XG$timebase_deadline_after$0$0"
        self.assertEqual(cdb_address(key + ":180\n", key), 0x180)
        for text in ("", key + ":not-an-address\n", key + ":180\n" + key + ":181\n",
                     key + ":180\n" + key + ":invalid\n"):
            with self.assertRaises(ValueError):
                cdb_address(text, key)


if __name__ == "__main__":
    unittest.main()
