# SPDX-License-Identifier: BSD-3-Clause
"""Synthetic SDCC records and status bytes; no target memory dumps."""

from contextlib import redirect_stderr, redirect_stdout
import hashlib
from io import StringIO
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from debug_image import (
    DebugImage, SourceLocation, decode_bootstrap, decode_fixture, expected_fixture,
    linked_source_locations, linked_symbols, main,
)
from test_m0_artifacts import EOF, record
from verify_firmware import ARTIFACT_EXTENSIONS


MAP = """C: 00000000 _main main
C: 00000001 _stop main
C: 00000002 _table main
D: 00000000 _state main
   00001E00 _m0_status status
   00000080 _SOC_P0 main
   00000008 __start__stack main
"""
CDB = """F:G$main$0_0$0({2}DF,SV:S),Z,0,0,0,0,0
S:G$table$0_0$0({2}STtest:S),D,0,0
S:G$state$0_0$0({16}STtest:S),F,0,0
S:G$m0_status$0_0$0({32}STtest:S),F,0,0
S:G$SOC_P0$0_0$0({1}SC:U),I,0,0
L:G$main$0_0$0:0
L:C$main.c$10$0_0$1:0
L:C$main.c$12$1_0$1:0
L:C$main.c$13$1_0$1:1
"""
GOLDEN_FIXTURE = bytes.fromhex("4d314442011003015ab16bce9dd46996")
INITIAL_FIXTURE = bytes.fromhex("4d314442011001000000000000006996")


class SourceLocationTests(unittest.TestCase):
    def parse(self, text=CDB, image=None):
        return linked_source_locations(text, image if image is not None else dict.fromkeys(range(4), 0))

    def test_exact_records_preserve_multiple_lines_at_one_address(self):
        self.assertEqual(self.parse(), (
            SourceLocation("main.c", 10, "0_0", 1, 0),
            SourceLocation("main.c", 12, "1_0", 1, 0),
            SourceLocation("main.c", 13, "1_0", 1, 1),
        ))

    def test_duplicate_records_are_deduplicated_but_conflicts_are_rejected(self):
        self.assertEqual(self.parse(CDB + "L:C$main.c$10$0_0$1:0\n"), self.parse())
        with self.assertRaisesRegex(ValueError, "Conflicting"):
            self.parse(CDB + "L:C$main.c$10$0_0$1:1\n")

    def test_distinct_scopes_can_map_one_line_to_multiple_addresses(self):
        locations = self.parse(CDB + "L:C$main.c$13$2_0$2:3\n")
        self.assertEqual([item.address for item in locations if item.line == 13], [1, 3])

    def test_malformed_record_is_not_silently_ignored(self):
        for record_text in ("L:C$main.c$13$1_0$1", "L:C$main.c$x$1_0$1:0",
                            "L:C$main.c$13$1$1:0", "L:C$main.c$13$1_0$1:xyz", "L:C"):
            with self.subTest(record=record_text), self.assertRaisesRegex(ValueError, "Malformed"):
                self.parse(record_text)

    def test_invalid_source_file_or_line(self):
        for file, line in (("", 1), ("main.c", 0), ("ma\0in.c", 1), ("ma\tin.c", 1)):
            with self.subTest(file=file, line=line), self.assertRaises(ValueError):
                self.parse(f"L:C${file}${line}$1_0$1:0\n")

    def test_code_holes_and_banked_addresses_are_rejected(self):
        for address in (2, 0x8000, 0x10000):
            with self.subTest(address=address), self.assertRaisesRegex(ValueError, "outside"):
                self.parse(f"L:C$main.c$1$0_0$1:{address:X}\n", {0: 0})

    def test_missing_source_records_do_not_invent_locations(self):
        self.assertEqual(self.parse("M:main\nL:A$main$10:0\n"), ())


class SymbolTests(unittest.TestCase):
    def parse(self, map_text=MAP, debug_text=CDB, image=None):
        return linked_symbols(map_text, debug_text, image if image is not None else dict.fromkeys(range(4), 0))

    def test_spaces_kinds_and_function_size_are_not_confused(self):
        symbols = self.parse()
        self.assertEqual((symbols["_main"].space, symbols["_main"].kind, symbols["_main"].size),
                         ("CODE", "function", None))
        self.assertEqual((symbols["_stop"].space, symbols["_stop"].kind), ("CODE", "label"))
        self.assertEqual((symbols["_table"].space, symbols["_table"].kind, symbols["_table"].size),
                         ("CODE", "object", 2))
        self.assertEqual((symbols["_state"].space, symbols["_state"].size), ("XDATA", 16))
        self.assertEqual(symbols["_m0_status"].address, 0x1E00)
        self.assertEqual(symbols["_SOC_P0"].space, "SFR")
        self.assertNotIn("__start__stack", symbols)

    def test_conflicting_declarations(self):
        with self.assertRaisesRegex(ValueError, "Conflicting CDB"):
            self.parse(debug_text=CDB + "S:G$state$0_0$0({17}STother:S),F,0,0\n")

    def test_map_and_cdb_spaces_must_agree(self):
        for map_text in (MAP.replace("C: 00000002", "D: 00000002"),
                         MAP.replace("D: 00000000", "C: 00000000")):
            with self.subTest(map_text=map_text), self.assertRaisesRegex(ValueError, "space mismatch"):
                self.parse(map_text=map_text)

    def test_map_and_linked_cdb_global_addresses_must_agree(self):
        with self.assertRaisesRegex(ValueError, "Map/CDB address mismatch"):
            self.parse(debug_text=CDB.replace("L:G$main$0_0$0:0", "L:G$main$0_0$0:1"))
        with self.assertRaisesRegex(ValueError, "Malformed CDB global"):
            self.parse(debug_text=CDB + "L:G$main$0_0$0:xyz\n")

    def test_conflicting_map_spaces(self):
        with self.assertRaisesRegex(ValueError, "Conflicting map spaces"):
            self.parse(map_text=MAP + "D: 00000000 _main main\n")

    def test_unlinked_declarations_are_not_advertised(self):
        symbols = self.parse(debug_text=CDB + "S:G$unlinked$0_0$0({2}DF,SV:S),C,0,0\n")
        self.assertNotIn("_unlinked", symbols)

    def test_unsupported_global_space_is_explicit(self):
        with self.assertRaisesRegex(ValueError, "Unsupported CDB"):
            self.parse(debug_text=CDB.replace("),F,", "),A,"))

    def test_code_data_must_fit_actual_image(self):
        with self.assertRaisesRegex(ValueError, "outside the linked image"):
            self.parse(image=dict.fromkeys(range(3), 0))

    def test_xdata_cannot_reach_iram_alias(self):
        with self.assertRaisesRegex(ValueError, "IRAM alias"):
            self.parse(map_text=MAP.replace("D: 00000000", "D: 00001EFF"))

    def test_sfr_bounds_and_size(self):
        with self.assertRaisesRegex(ValueError, "Invalid SFR"):
            self.parse(map_text=MAP.replace("00000080", "00000100"))
        with self.assertRaisesRegex(ValueError, "Invalid SFR"):
            self.parse(debug_text=CDB.replace("{1}SC", "{2}SC"))

    def test_zero_sized_objects_are_not_accepted(self):
        with self.assertRaisesRegex(ValueError, "Invalid object size"):
            self.parse(debug_text=CDB.replace("{2}ST", "{0}ST"))


class SnapshotTests(unittest.TestCase):
    def setUp(self):
        self.m0 = b"M0CC\x01\x20\x02\x00\xff\x00" + bytes(range(10, 26)) + b"\0" * 6

    def test_m0_decodes_snapshot_without_inventing_pin_measurements(self):
        result = decode_bootstrap(self.m0, "generic")
        self.assertEqual(result["heartbeat"], 255)
        self.assertEqual(result["ports"], [10, 11, 12])
        self.assertEqual(result["directions"], [13, 14, 15])
        self.assertEqual(result["clock_request"], 24)
        self.assertEqual(result["interrupt_enables"], [0, 0, 0])
        lg = bytearray(self.m0)
        lg[7] = lg[9] = 1
        self.assertEqual(decode_bootstrap(bytes(lg), "lg_esl29_rev03")["board"], "lg_esl29_rev03")

    def test_m0_invalid_identity_phase_policy_interrupts_and_reserved_bytes(self):
        for offset in (*range(8), 9, *range(26, 32)):
            data = bytearray(self.m0)
            data[offset] ^= 1
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                decode_bootstrap(bytes(data), "generic")

    def test_exact_snapshot_lengths(self):
        for source, decode in ((self.m0, lambda data: decode_bootstrap(data, "generic")),
                               (GOLDEN_FIXTURE, decode_fixture)):
            for data in (source[:-1], source + b"\0", b"", None):
                with self.subTest(data=data), self.assertRaises(ValueError):
                    decode(data)

    def test_fixture_golden_state_and_counter_wrap(self):
        self.assertEqual(expected_fixture(0), GOLDEN_FIXTURE)
        self.assertEqual(expected_fixture(255), bytes.fromhex("4d31444201100300a5c1b613265d6996"))
        self.assertEqual(expected_fixture(256), GOLDEN_FIXTURE)
        self.assertEqual(decode_fixture(GOLDEN_FIXTURE)["result"], 0xB1)
        self.assertEqual(decode_fixture(INITIAL_FIXTURE)["phase"], 1)
        for cycle in range(512):
            self.assertEqual(decode_fixture(expected_fixture(cycle))["iteration"], (cycle + 1) & 255)

    def test_fixture_rejects_every_single_byte_mutation(self):
        for source in (GOLDEN_FIXTURE, INITIAL_FIXTURE):
            for offset in range(16):
                data = bytearray(source)
                data[offset] ^= 1
                with self.subTest(phase=source[6], offset=offset), self.assertRaises(ValueError):
                    decode_fixture(bytes(data))

    def test_in_progress_fixture_is_not_a_verified_cycle(self):
        data = bytearray(GOLDEN_FIXTURE)
        data[6] = 2
        with self.assertRaisesRegex(ValueError, "in progress"):
            decode_fixture(bytes(data))

    def test_expected_fixture_rejects_invalid_cycle(self):
        for cycle in (-1, True, 1.0):
            with self.subTest(cycle=cycle), self.assertRaises(ValueError):
                expected_fixture(cycle)


class BundleTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="cc2530-image-reader-")
        self.addCleanup(self.directory.cleanup)
        self.output = Path(self.directory.name)
        contents = {"map": MAP, "cdb": CDB, "ihx": record(data=b"\x02\x00\x00\x00") + "\n" + EOF,
                    "hex": record(data=b"\x02\x00\x00\x00") + "\n" + EOF, "mem": "synthetic"}
        for extension in ARTIFACT_EXTENSIONS:
            data = b"\x02\0\0\0" if extension == "bin" else contents[extension].encode("ascii")
            (self.output / f"bringup.{extension}").write_bytes(data)
        self.info = {
            "schema": 1, "board": "generic", "image": "bringup", "compiler": "SDCC 4.2.0",
            "sha256": {f"bringup.{extension}": hashlib.sha256(
                (self.output / f"bringup.{extension}").read_bytes()).hexdigest() for extension in ARTIFACT_EXTENSIONS},
        }
        self.write_metadata()
        # Unit-isolate the reader; boot_image.py also tests it on genuine verified artifacts.
        patcher = patch("debug_image.verify_artifacts", return_value=({}, {}))
        self.verify = patcher.start()
        self.addCleanup(patcher.stop)

    def write_metadata(self):
        (self.output / "build-info.json").write_text(json.dumps(self.info), encoding="utf-8")

    def load(self):
        return DebugImage(self.output, "generic")

    def run_cli(self, command, *extra):
        stdout, stderr = StringIO(), StringIO()
        with redirect_stdout(stdout), redirect_stderr(stderr):
            code = main([command, "--output", str(self.output), "--board", "generic", *extra])
        return code, stdout.getvalue(), stderr.getvalue()

    def test_load_requires_real_verifier_before_symbol_lookup(self):
        image = self.load()
        self.verify.assert_called_once_with(self.output, "generic", "bringup")
        self.assertEqual(image.symbol("_state").size, 16)
        self.assertEqual(image.sha256, self.info["sha256"]["bringup.bin"])

    def test_image_verification_failure_is_not_hidden(self):
        self.verify.side_effect = ValueError("synthetic image rejection")
        code, out, err = self.run_cli("symbols")
        self.assertEqual((code, out), (1, ""))
        self.assertIn("synthetic image rejection", err)

    def test_each_artifact_hash_is_checked(self):
        for extension in ARTIFACT_EXTENSIONS:
            path = self.output / f"bringup.{extension}"
            data = path.read_bytes()
            path.write_bytes(data + b"x")
            with self.subTest(extension=extension), self.assertRaisesRegex(ValueError, "hash mismatch"):
                self.load()
            path.write_bytes(data)

    def test_metadata_identity_and_toolchain(self):
        for field, value in (("schema", 2), ("schema", True),
                             ("board", "lg_esl29_rev03"), ("image", "debug_fixture"),
                             ("compiler", "SDCC 4.3.0"), ("compiler", None), ("sha256", {})):
            original = self.info[field]
            self.info[field] = value
            self.write_metadata()
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.load()
            self.info[field] = original

    def test_unknown_symbol_is_an_error(self):
        with self.assertRaisesRegex(ValueError, "No supported"):
            self.load().symbol("_missing")

    def test_breakpoint_requires_executable_code_not_data(self):
        image = self.load()
        self.assertEqual(image.breakpoint("_stop", 3), {
            "symbol": "_stop", "slot": 3, "bank": 0, "address": 1,
            "target_parameters_hex": "380001", "programmed": False,
        })
        for name in ("_table", "_state", "_m0_status", "_SOC_P0"):
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, "not a data object"):
                image.breakpoint(name, 0)

    def test_cli_lookup_is_json_with_image_identity(self):
        code, out, err = self.run_cli("symbols", "--name", "_state")
        self.assertEqual((code, err), (0, ""))
        result = json.loads(out)
        self.assertEqual(result["evidence"], "offline-image-checked")
        self.assertEqual(result["symbols"], [{"name": "_state", "address": 0, "space": "XDATA",
                                             "kind": "object", "size": 16}])

    def test_source_lookup_is_exact_in_both_directions(self):
        image = self.load()
        self.assertEqual([item.line for item in image.source_lines(pc=0)], [10, 12])
        self.assertEqual(image.source_lines(file="main.c", line=13)[0].address, 1)
        self.assertEqual(len(image.source_lines()), 3)
        for arguments in ({"pc": 2}, {"file": "main.c", "line": 11},
                          {"file": "/somewhere/main.c", "line": 13}):
            with self.subTest(arguments=arguments), self.assertRaisesRegex(ValueError, "No exact"):
                image.source_lines(**arguments)

    def test_source_lookup_rejects_invalid_or_ambiguous_filters(self):
        image = self.load()
        for arguments in ({"pc": True}, {"pc": -1}, {"pc": 0x8000}, {"pc": 0.0},
                          {"pc": 0, "line": 1}, {"pc": 0, "file": "main.c"},
                          {"file": "main.c"}, {"line": 1}, {"file": "main.c", "line": True},
                          {"file": "main.c", "line": 0}):
            with self.subTest(arguments=arguments), self.assertRaises(ValueError):
                image.source_lines(**arguments)

    def test_source_files_are_never_opened_by_loading_or_lookup(self):
        original_open = Path.open

        def only_bundle(path, *args, **kwargs):
            self.assertEqual(path.parent, self.output, "Source lookup accessed a file outside the bundle")
            return original_open(path, *args, **kwargs)

        with patch.object(Path, "open", autospec=True, side_effect=only_bundle):
            self.assertEqual(self.load().source_lines(file="main.c", line=13)[0].address, 1)

    def test_source_cli_includes_exact_locations_and_image_identity(self):
        code, out, err = self.run_cli("source-lines", "--pc", "0x0")
        self.assertEqual((code, err), (0, ""))
        result = json.loads(out)
        self.assertEqual(result["mapping"], "exact-cdb-records")
        self.assertEqual(result["image_sha256"], self.info["sha256"]["bringup.bin"])
        self.assertEqual(result["source_lines"], [
            {"file": "main.c", "line": 10, "scope": "0_0", "block": 1, "address": 0},
            {"file": "main.c", "line": 12, "scope": "1_0", "block": 1, "address": 0},
        ])
        code, out, err = self.run_cli("source-lines", "--file", "main.c", "--line", "13")
        self.assertEqual((code, err), (0, ""))
        self.assertEqual(json.loads(out)["source_lines"][0]["address"], 1)

    def test_cli_errors_never_print_success(self):
        for command, arguments in (("symbols", ["--name", "_unknown"]),
                                   ("breakpoint", ["--name", "_main", "--slot", "4"]),
                                   ("fixture-state", ["--hex", GOLDEN_FIXTURE.hex()]),
                                   ("source-lines", ["--pc", "2"]),
                                   ("source-lines", ["--file", "main.c"]),
                                   ("source-lines", ["--pc", "0", "--line", "10"]),
                                   ("status", ["--hex", "wrong"])):
            with self.subTest(command=command):
                code, out, err = self.run_cli(command, *arguments)
                self.assertEqual((code, out), (1, ""))
                self.assertTrue(err.startswith("debug-image: "))

    def test_snapshot_file_must_be_exact_size(self):
        source = self.output / "synthetic-snapshot"
        source.write_bytes(b"M0CC\x01\x20\x02\0\0\0" + b"\0" * 22)
        code, out, err = self.run_cli("status", "--snapshot", str(source))
        self.assertEqual((code, err), (0, ""))
        self.assertEqual(json.loads(out)["status"]["byte_size"], 32)
        source.write_bytes(source.read_bytes() + b"\0")
        code, out, err = self.run_cli("status", "--snapshot", str(source))
        self.assertEqual((code, out), (1, ""))
        self.assertIn("exactly 32", err)


class OfflineBoundaryTests(unittest.TestCase):
    def test_decoder_never_imports_usb_or_host_transport(self):
        code = """
import sys
def audit(event, args):
    if event == "import" and args[0].split(".")[0] in ("usb", "cc_debugger"):
        raise AssertionError("Offline tool imported USB transport")
sys.addaudithook(audit)
sys.path.insert(0, "tools")
import debug_image
sys.exit(debug_image.main(["decode-status", "0x23"]))
"""
        result = subprocess.run([sys.executable, "-B", "-c", code], capture_output=True, text=True,
                                cwd=Path(__file__).resolve().parents[1], timeout=5, check=True)
        self.assertTrue(json.loads(result.stdout)["stack_overflow"])
        self.assertEqual(result.stderr, "")


if __name__ == "__main__":
    unittest.main()
