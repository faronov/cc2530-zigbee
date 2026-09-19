# SPDX-License-Identifier: BSD-3-Clause
"""Original synthetic clock ABI records and offline CLI tests."""

from contextlib import redirect_stderr, redirect_stdout
import hashlib
from io import StringIO
import json
from pathlib import Path
import re
import shutil
import tempfile
import unittest
from unittest.mock import patch

import debug_image
import clock_fixture
from debug_image import decode_clock_fixture
from verify_firmware import ARTIFACT_EXTENSIONS, IMAGES, ROOT, cdb_address, parse_ihex, parse_symbols


class ClockDeclarationTests(unittest.TestCase):
    def test_all_return_declarations_and_public_address_duplicates(self):
        for result in ("SC:U", "SL:U", "SV:S"):
            name = "synthetic_clock"
            records = [f"F:G${name}$0_0$0({{2}}DF,{result}),Z,0,0,0,0,0",
                       f"S:G${name}$0_0$0({{2}}DF,{result}),C,0,0",
                       f"L:G${name}$0$0:100", f"L:XG${name}$0$0:120"]
            text = "\n".join(records)+"\n"
            for debug in (text, text+text):
                clock_fixture.public_declarations(debug, name, result)
                clock_fixture.public_bounds(debug, {"_"+name: 0x100}, name, 0x100, 0x120)
            for record in records:
                wrong = (record.rsplit(":", 1)[0]+":101" if record.startswith("L:") else
                         record.replace("DF,"+result, "DF,SI:U"))
                for debug in (text.replace(record, wrong), text+wrong+"\n"):
                    with self.subTest(record=record), self.assertRaises(ValueError):
                        clock_fixture.public_declarations(debug, name, result)
                        clock_fixture.public_bounds(debug, {"_"+name: 0x100}, name, 0x100, 0x120)
            with self.assertRaises(ValueError):
                clock_fixture.public_bounds(text, {"_"+name: 0x101}, name, 0x100, 0x120)
            for sep in ("\r", "\v", "\f", "\x1c", "\x1d", "\x1e", "\x85", "\u2028", "\u2029"):
                hostile = text+sep+records[0].replace("DF,"+result, "DF,SI:U")+"\n"
                with self.subTest(separator=repr(sep)):
                    with self.assertRaisesRegex(ValueError, "non-LF line separator"):
                        clock_fixture.public_declarations(hostile, name, result)
                    with self.assertRaisesRegex(ValueError, "non-LF line separator"):
                        clock_fixture.public_bounds(hostile, {"_"+name: 0x100}, name, 0x100, 0x120)


class ClockLinkedMetadataTests(unittest.TestCase):
    """Synthetic corruption of real immutable artifacts; never run equipment."""
    @classmethod
    def setUpClass(cls):
        paths = [ROOT/"build/radio-tx-fixture-dev"/b/"clock-board-after/clock_fixture"
                 for b in ("generic", "lg_esl29_rev03")]
        if not all(p.with_suffix(s).exists() for p in paths for s in (".ihx", ".map", ".cdb")):
            raise unittest.SkipTest("Build the isolated both-board clock fixtures first")
        cls.artifacts = [(p, parse_ihex(p.with_suffix(".ihx").read_text()),
                          parse_symbols(p.with_suffix(".map").read_text()),
                          p.with_suffix(".cdb").read_bytes().decode("utf-8"))
                         for p in paths]

    def test_unused_public_map_and_cdb_boundaries_are_independently_bound(self):
        for path, image, symbols, debug in self.artifacts:
            clock_fixture.verify_clock_board(image, symbols, debug)
            for name in ("clock_select_init", "timebase_read_awake_ticks24", "timebase_deadline_after",
                         "timebase_expired", "clock_fixture_initialize", "clock_fixture_cycle"):
                with self.subTest(path=path, missing=name), self.assertRaises(ValueError):
                    clock_fixture.verify_clock_board(image, {k: v for k, v in symbols.items() if k != "_"+name}, debug)
                with self.subTest(path=path, name=name), self.assertRaises(ValueError):
                    clock_fixture.verify_clock_board(image, symbols | {"_"+name: symbols["_"+name]+1}, debug)
                for prefix in ("L:G$", "L:XG$"):
                    records = re.findall(rf"^{re.escape(prefix+name)}\$0\$0:[^\n]*$", debug, re.M)
                    self.assertTrue(records)
                    record = records[0]
                    changed = record.rsplit(":", 1)[0]+":"+f"{int(record.rsplit(':', 1)[1], 16)+1:X}"
                    for mutation in (debug.replace(record+"\n", ""), debug.replace(record, changed),
                                     debug+"\n"+changed+"\n",
                                     debug+"\n"+record.replace("$0$0:", "$1$0:")+"\n"):
                        with self.subTest(path=path, record=record), self.assertRaises(ValueError):
                            clock_fixture.verify_clock_board(image, symbols, mutation)
                    # A coordinated bad map+CDB entry cannot evade the proof.
                    if prefix == "L:G$":
                        with self.assertRaises(ValueError):
                            clock_fixture.verify_clock_board(image, symbols | {"_"+name: symbols["_"+name]+1},
                                                             debug.replace(record, changed))
                    clock_fixture.verify_clock_board(image, symbols, debug+"\n"+record+"\n")

    def test_every_public_return_and_generic_pointer_alternative_rejects(self):
        functions = ("clock_select_init", "clock_fixture_initialize", "clock_fixture_cycle",
                     "timebase_read_awake_ticks24", "timebase_deadline_after", "timebase_expired")
        for path, image, symbols, debug in self.artifacts:
            records = [l for l in debug.split("\n") if
                       re.match(r"^[FS]:G\$(?:"+"|".join(functions)+r")\$", l) or
                       re.match(r"^S:(?:Lclock\.|Fclock\$|Ltimebase\.|Lclock_fixture_state\.).*DG,", l)]
            self.assertTrue(records)
            for record in records:
                wrong = (record.replace("DF,", "DF,DA2d,", 1) if "DF," in record else
                         record.replace("{3}DG,", "{2}DX,", 1))
                self.assertNotEqual(wrong, record)
                for mutation in (debug.replace(record, wrong), debug+"\n"+wrong+"\n"):
                    with self.subTest(path=path, record=record), self.assertRaises(ValueError):
                        clock_fixture.verify_clock_board(image, symbols, mutation)
            for name in symbols:
                if re.match(r"_(?:clock_select_init|timebase_deadline_after|timebase_expired)_PARM_", name):
                    with self.subTest(path=path, name=name), self.assertRaises(ValueError):
                        clock_fixture.verify_clock_board(image, symbols | {name: symbols[name]+1}, debug)
            public = [r for r in records if re.match(r"^[FS]:G\$", r)]
            clock_fixture.verify_clock_board(image, symbols, debug+"\n"+"\n".join(public)+"\n")

    def test_source_and_actual_diagnostic_pointer_storage_associations(self):
        for path, image, symbols, debug in self.artifacts:
            proof = clock_fixture.clock_timeout_checkpoint(image, symbols, debug)
            self.assertEqual((proof["diagnostics_address"], proof["source_seen_address"],
                              proof["deadline_address"], proof["output_pointer_address"]), (56, 144, 139, 111))
            for module in ("clock", "timebase", "clock_fixture", "clock_fixture_state"):
                changed = debug.replace("C$"+module+".c$", "C$missing.c$")
                self.assertNotEqual(changed, debug)
                with self.subTest(path=path, module=module), self.assertRaises(ValueError):
                    clock_fixture.clock_timeout_checkpoint(image, symbols, changed)
            key = "L:Fclock_fixture_state$diagnostics$0_0$0"
            record = re.findall(r"^"+re.escape(key)+r":[^\n]*$", debug, re.M)[0]
            for value in (0, 57, 139, 0x1e00, 0x1f00):
                wrong = key+f":{value:X}"
                for changed in (debug.replace(record, wrong), debug+"\n"+wrong+"\n"):
                    with self.subTest(path=path, address=value), self.assertRaises(ValueError):
                        clock_fixture.clock_timeout_checkpoint(image, symbols, changed)
            clock_fixture.clock_timeout_checkpoint(image, symbols, debug+"\n"+record+"\n")
            for key in ("S:Fclock_fixture_state$diagnostics$", "T:Fclock_fixture_state$__00000002",
                        "T:Fclock_fixture_state$__00000003"):
                record = next(l for l in debug.split("\n") if l.startswith(key))
                wrong = record.replace("({", "({9", 1)
                for changed in (debug.replace(record, wrong), debug+"\n"+wrong+"\n"):
                    with self.subTest(path=path, key=key), self.assertRaises(ValueError):
                        clock_fixture.clock_timeout_checkpoint(image, symbols, changed)

    def test_original_clock_board_metadata_negatives_still_apply(self):
        for path, image, symbols, debug in self.artifacts:
            for old, new in (("L:XG$timebase_deadline_after", "L:XG$missing"),
                             ("$sloc0$0_1$0:14\n", "$sloc0$0_1$0:13\n"),
                             ("{17}S:S$diagnostics", "{18}S:S$diagnostics"),
                             ("{19}DA19d,SC:U", "{18}DA18d,SC:U"),
                             ("{17}S:S$source_seen", "{16}S:S$source_seen"),
                             ("Fclock_fixture_state$diagnostics$0_0$0:38\n",
                              "Fclock_fixture_state$diagnostics$0_0$0:39\n")):
                self.assertIn(old, debug)
                with self.subTest(path=path, original=old), self.assertRaises(ValueError):
                    clock_fixture.verify_clock_board(image, symbols, debug.replace(old, new))
            with self.assertRaises(ValueError):
                clock_fixture.verify_clock_board(image, symbols,
                    debug+"\nL:XG$timebase_deadline_after$0$0:8000\n")

    def test_non_lf_records_reject_directly_and_through_genuine_loader(self):
        for board, (path, image, symbols, debug) in zip(("generic", "lg_esl29_rev03"), self.artifacts):
            records = sorted(set(r for r in debug.split("\n") if re.match(
                r"^[FSL]:(?:X?Fclock\$observe\$|X?G\$clock_select_init\$)", r)))
            self.assertEqual(len(records), 8)
            with tempfile.TemporaryDirectory() as directory:
                out = Path(directory)
                for ext in ARTIFACT_EXTENSIONS:
                    shutil.copyfile(path.with_suffix("."+ext), out/("clock_fixture."+ext))
                for listing in path.parent.glob("clock_fixture.*.rst"):
                    shutil.copyfile(listing, out/listing.name)
                metadata = out/"build-info.json"
                shutil.copyfile(path.parent/metadata.name, metadata)
                original_info = metadata.read_bytes()
                self.assertIsInstance(debug_image.DebugImage(out, board, "clock_fixture"),
                                      debug_image.DebugImage)
                for record in records:
                    hostile = (record.rsplit(":", 1)[0]+":7FFF" if record.startswith("L:")
                               else record.replace("({", "({9", 1))
                    self.assertNotEqual(hostile, record)
                    for sep in ("\r", "\v", "\f", "\x1c", "\x1d", "\x1e", "\x85", "\u2028", "\u2029"):
                        for text in (debug.replace(record+"\n", record+sep+"\n", 1),
                                     debug+"\n"+sep+hostile+"\n",
                                     debug+"\nignored"+sep+hostile+"\n"):
                            with self.subTest(board=board, record=record, separator=repr(sep)):
                                for verify in (clock_fixture.verify_clock_code, clock_fixture.verify_clock_board):
                                    with self.assertRaisesRegex(ValueError, "non-LF line separator"):
                                        verify(image, symbols, text)
                                raw = text.encode("utf-8")
                                (out/"clock_fixture.cdb").write_bytes(raw)
                                info = json.loads(original_info)
                                info["sha256"]["clock_fixture.cdb"] = hashlib.sha256(raw).hexdigest()
                                metadata.write_text(json.dumps(info))
                                with self.assertRaisesRegex(ValueError, "non-LF line separator"):
                                    debug_image.DebugImage(out, board, "clock_fixture")


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
