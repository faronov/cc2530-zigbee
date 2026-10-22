# SPDX-License-Identifier: BSD-3-Clause
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import firmware


class LLVMOnlyBuild(unittest.TestCase):
    def test_pin_and_abi(self):
        pin = json.loads((firmware.HERE / "toolchain.json").read_text())
        self.assertEqual(pin["revision"], "ac1712447bf906daddba44f79e586173a045aa1f")
        profile = json.loads((firmware.HERE / "profile.json").read_text())
        self.assertEqual(profile["sf_flags"], "-mllvm -mcs51-static-frames")
        self.assertEqual(profile["abi_flags"], "-mllvm -mcs51-csr=none -mllvm -mcs51-wide-args")

    def test_fixture_status_has_the_existing_typed_xdata_contract(self):
        header = (firmware.REPO / "include/bringup.h").read_text()
        asm = (firmware.HERE / "inputs/fixture_status_mcs51.s").read_text()
        self.assertIn("#define M0_STATUS_ADDRESS 0x1e00UL", header)
        self.assertIn("#define M0_STATUS_SIZE 32u", header)
        self.assertIn("#define M0_STATUS_RESERVED 64u", header)
        self.assertIn('.section .mcs51.xdata.fixed.m0_status,"aw",@nobits', asm)
        self.assertIn(".size m0_status, 32", asm)
        self.assertNotIn("--defsym,m0_status", (firmware.HERE / "firmware.py").read_text())

    def test_standard_make_is_llvm_only(self):
        out = subprocess.check_output(["make", "-n"], cwd=firmware.REPO, text=True)
        self.assertIn("tools/llvm/firmware.py", out)
        for path in ("Makefile", "mk/host-tests.mk"):
            text = (firmware.REPO / path).read_text()
            self.assertNotIn("$(SDCC)", text)
            self.assertNotIn(".ihx", text)
            self.assertNotIn(".rel", text)

    def test_both_native_board_definitions(self):
        for board, number in (("generic", "0"), ("lg_esl29_rev03", "1")):
            out = subprocess.check_output(["make", "-Bn", "BOARD=" + board,
                                           "BUILD=build/native-check", "build/native-check/host-tests_" + board],
                                          cwd=firmware.REPO, text=True)
            self.assertIn("-DCC2530_BOARD=" + number, out)
            self.assertIn("boards/" + board + ".c", out)
            self.assertNotIn("sdcc ", out)

    def test_vector_recipe_still_binds_the_llvm_oracle(self):
        out = subprocess.check_output(["make", "-Bn", "BUILD=build/native-check",
                                       "build/native-check/host-join-smoke-vectors"],
                                      cwd=firmware.REPO, text=True)
        lines = [x for x in out.splitlines() if x.endswith("-o build/native-check/host-join-smoke-vectors")]
        self.assertEqual(len(lines), 1)
        self.assertEqual(lines[0].count("-Ibuild/native-check/join-smoke-layout"), 1)
        self.assertNotIn("sdcc ", out)

    def test_reject_changed_compiler_source(self):
        with patch("sys.argv", ["firmware.py", "--tools", "unused", "--llvm-src", "unused",
                                "--output", "build/unused", "--board", "lg_esl29_rev03"]):
            with patch.object(firmware.tiers, "compiler_identity",
                              return_value={"llvm_commit": "wrong", "llvm_diff": None}):
                with self.assertRaisesRegex(SystemExit, "exact clean pinned"):
                    firmware.main()

    def test_reject_wrong_executable(self):
        pin = json.loads((firmware.HERE / "toolchain.json").read_text())["revision"]
        with patch("sys.argv", ["firmware.py", "--tools", "unused", "--llvm-src", "unused",
                                "--output", "build/unused", "--board", "lg_esl29_rev03"]):
            with patch.object(firmware.tiers, "compiler_identity",
                              return_value={"llvm_commit": pin, "llvm_diff": None, "version": "other compiler"}):
                with self.assertRaisesRegex(SystemExit, "executable version"):
                    firmware.main()

    def test_workflow_defaults_match_the_standard_pin(self):
        pin = json.loads((firmware.HERE / "toolchain.json").read_text())["revision"]
        workflow = (firmware.REPO / ".github/workflows/llvm-tiers.yml").read_text()
        self.assertIn("default: " + pin, workflow)
        self.assertIn("inputs.llvm_ref || '" + pin + "'", workflow)

    def test_reject_floating_compiler_override_before_tool_inspection(self):
        for revision in ("main", "ac17124", "a" * 39, "A" * 40):
            with self.subTest(revision=revision), patch("sys.stderr"), \
                    patch("sys.argv", ["firmware.py", "--tools", "unused", "--llvm-src", "unused",
                                       "--output", "build/unused", "--board", "lg_esl29_rev03",
                                       "--llvm-revision", revision]), \
                    patch.object(firmware.tiers, "compiler_identity") as inspect:
                with self.assertRaises(SystemExit) as error:
                    firmware.main()
                self.assertEqual(error.exception.code, 2)
                inspect.assert_not_called()

    def test_explicit_override_still_checks_source_and_executable(self):
        revision = "a" * 40
        for compiler, message in (
                ({"llvm_commit": revision, "llvm_diff": "dirty", "version": revision}, "exact clean"),
                ({"llvm_commit": "b" * 40, "llvm_diff": None, "version": revision}, "exact clean"),
                ({"llvm_commit": revision, "llvm_diff": None, "version": "wrong"}, "executable version")):
            with self.subTest(compiler=compiler), \
                    patch("sys.argv", ["firmware.py", "--tools", "unused", "--llvm-src", "unused",
                                       "--output", "build/unused", "--board", "lg_esl29_rev03",
                                       "--llvm-revision", revision]), \
                    patch.object(firmware.tiers, "compiler_identity", return_value=compiler):
                with self.assertRaisesRegex(SystemExit, message):
                    firmware.main()

    def test_sealed_generation_rejects_artifact_tampering(self):
        pin = json.loads((firmware.HERE / "toolchain.json").read_text())["revision"]
        for override in (None, "a" * 40):
            with self.subTest(override=override), tempfile.TemporaryDirectory() as directory:
                self.check_sealed_generation(directory, override or pin, override)

    def check_sealed_generation(self, directory, revision, override):
        compiler = {"llvm_commit": revision, "llvm_diff": None, "version": revision}
        with self.subTest(revision=revision):
            output = Path(directory) / "out"
            def build(argv, **kwargs):
                if str(argv[1]).endswith("build.sh"):
                    fw = Path(argv[2])
                    (fw / "link").mkdir(parents=True)
                    (fw / "audit").mkdir()
                    (fw / "link/hw1.elf").write_bytes(b"test ELF")
                    (fw / "audit/hw1.hex").write_bytes(b"test HEX")
            argv = ["firmware.py", "--tools", directory, "--llvm-src", directory,
                    "--output", str(output), "--board", "lg_esl29_rev03"]
            if override:
                argv += ["--llvm-revision", override]
            with patch("sys.argv", argv), patch.object(firmware, "run", side_effect=build), \
                    patch.object(firmware.tiers, "compiler_identity", return_value=compiler), \
                    patch.object(firmware.tiers, "firmware_identity", return_value={"commit": "test"}), \
                    patch.object(firmware.tiers, "admission", return_value=lambda: (True, {"test": True})), \
                    patch("builtins.print"):
                firmware.main()
                firmware.main()
                receipt = json.loads((output / "receipt.json").read_text())
                self.assertEqual(receipt["identity"]["expected_llvm_revision"], revision)
                self.assertEqual(receipt["identity"]["abi"], "-mllvm -mcs51-csr=none -mllvm -mcs51-wide-args")
                Path(receipt["elf"]).write_bytes(b"changed")
                with self.assertRaisesRegex(SystemExit, "identity/seal/artifact"):
                    firmware.main()


if __name__ == "__main__":
    unittest.main()
