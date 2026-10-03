# SPDX-License-Identifier: BSD-3-Clause
"""Transaction/dependency failure controls; synthetic builds are not MCU proof."""
from contextlib import redirect_stdout
import io
import json
import multiprocessing
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import xdata_build as build
from xdata_multipool import verify


class BuildTransactionTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.output = self.root / "build/overlay"
        (self.root / "catalog.json").write_text('{"overlay": {}}')
        self.state = {"sources": {"src/unit.c": "original"}, "toolchain": "original",
                      "board": "lg_esl29_rev03", "key_mode": "default-tc"}
        self.counter = 0
        self.failure = None
        self.patchers = [
            patch.object(build, "ROOT", self.root),
            patch.object(build, "inputs", side_effect=lambda *args: (
                Path("/compiler"), json.loads(json.dumps(self.state)))),
            patch.object(build, "policy", return_value=({}, {"image_catalog": "catalog.json"})),
            patch.object(build, "generate", side_effect=self.generate),
            patch.object(build, "identities", return_value={}),
        ]
        for patcher in self.patchers:
            patcher.start()
            self.addCleanup(patcher.stop)
        self.cwd = Path.cwd()
        # Generation paths deliberately remain relative, as in real ASlink inputs.
        import os
        os.chdir(self.root)
        self.addCleanup(os.chdir, self.cwd)

    def generate(self, generation, *args):
        self.counter += 1
        for relative in ("preliminary/unit.rel", "preliminary/unit.xdata.json",
                         "join-smoke-layout/firmware.ihx", "join-smoke-layout/join_smoke_layout.h",
                         "input-identities.json"):
            path = generation / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("synthetic output\n")
        if self.failure:
            raise self.failure

    def run_build(self, **options):
        with redirect_stdout(io.StringIO()):
            return build.build(self.output, "compiler", "lg_esl29_rev03", "default-tc", **options)

    def test_clean_and_noop_retain_generation_without_compiling(self):
        first = self.run_build()
        self.assertEqual(self.run_build(), first)
        self.assertEqual(self.counter, 1)
        self.assertTrue((self.output / "join-smoke-layout").is_symlink())
        build.checked_receipt(self.output / "join-smoke-layout")

    def test_changed_source_header_contract_allocator_and_toolchain_rebuild(self):
        previous = self.run_build()
        for key in ("src/unit.c", "include/unit.h", "tools/xdata_platform_contract.json",
                    "tools/xdata_pool_allocator.py"):
            self.state["sources"][key] = "changed"
            current = self.run_build()
            self.assertNotEqual(current, previous)
            previous = current
        self.state["toolchain"] = "changed"
        self.assertNotEqual(self.run_build(), previous)
        self.assertEqual(self.counter, 6)

    def test_missing_stale_or_foreign_output_fails_without_reusing_publication(self):
        for relative in ("preliminary/unit.xdata.json", "preliminary/unit.rel",
                         "join-smoke-layout/firmware.ihx", "join-smoke-layout/join_smoke_layout.h"):
            for mode in ("delete", "stale"):
                with self.subTest(relative=relative, mode=mode):
                    generation = self.run_build(rebuild=True).parent
                    path = generation / relative
                    if mode == "delete":
                        path.unlink()
                    else:
                        path.write_text("old build\n")
                    with self.assertRaisesRegex(ValueError, "Generated artifact"):
                        self.run_build()
                    self.assertFalse((self.output / "join-smoke-layout").exists())

    def test_verification_rejects_receipt_from_another_compiler(self):
        root = self.run_build()
        self.state["toolchain"] = "another compiler"
        with patch.object(build, "source_inputs", return_value=self.state["sources"]), \
                self.assertRaisesRegex(ValueError, "Compiler/toolchain"):
            build.admit(root, "lg_esl29_rev03", "default-tc", sdcc="compiler")

    def test_failed_or_interrupted_generation_never_publishes_and_next_build_recovers(self):
        for failure in (ValueError("proof rejected"), subprocess.CalledProcessError(1, "compiler"),
                        KeyboardInterrupt()):
            self.run_build()
            self.failure = failure
            with self.assertRaises(type(failure)):
                self.run_build(rebuild=True)
            self.assertFalse((self.output / "join-smoke-layout").exists())
            self.failure = None
            self.run_build()
            self.assertTrue((self.output / "join-smoke-layout").exists())

    def test_inputs_changing_during_compile_fail_closed(self):
        original = self.generate

        def changed(generation, *args):
            original(generation, *args)
            self.state["sources"]["src/unit.c"] = "changed during compile"

        with patch.object(build, "generate", side_effect=changed), \
                self.assertRaisesRegex(ValueError, "changed during overlay build"):
            self.run_build()
        self.assertFalse((self.output / "join-smoke-layout").exists())

    def test_wrong_profile_and_missing_catalog_remove_old_publication(self):
        for error in ("profile not admitted", "Missing immutable image catalog"):
            self.run_build()
            with patch.object(build, "policy", side_effect=ValueError(error)), \
                    self.assertRaisesRegex(ValueError, error):
                self.run_build()
            self.assertFalse((self.output / "join-smoke-layout").exists())

    def test_arbitrary_output_directory_is_not_deleted(self):
        public = self.output / "join-smoke-layout"
        public.mkdir(parents=True)
        (public / "user-file").write_text("keep")
        with self.assertRaisesRegex(ValueError, "owned generation"):
            self.run_build()
        self.assertEqual((public / "user-file").read_text(), "keep")

    def test_receipt_version_and_manifest_algorithm_version_fail_closed(self):
        generation = self.run_build().parent
        path = generation / "receipt.json"
        receipt = json.loads(path.read_bytes())
        path.write_text(json.dumps(receipt | {"version": 99}))
        with self.assertRaisesRegex(ValueError, "receipt"):
            self.run_build()
        for value in (None, True, "1", 2):
            with patch.object(Path, "read_bytes", return_value=json.dumps(
                    {"version": 4, "algorithm_version": value}).encode()), \
                    self.assertRaisesRegex(ValueError, "algorithm version"):
                verify(Path("unused"))

    def test_boolean_receipt_version_is_not_an_integer_schema_version(self):
        generation = self.run_build().parent
        path = generation / "receipt.json"
        receipt = json.loads(path.read_bytes())
        path.write_text(json.dumps(receipt | {"version": True}))
        with self.assertRaisesRegex(ValueError, "receipt"):
            self.run_build()

    def test_parallel_processes_publish_only_one_generation(self):
        context = multiprocessing.get_context("fork")
        results = context.Queue()

        def worker():
            try:
                results.put(("ok", str(self.run_build())))
            except (OSError, ValueError, KeyError) as error:
                results.put(("error", str(error)))

        workers = [context.Process(target=worker) for _ in range(2)]
        try:
            for process in workers:
                process.start()
            found = [results.get(timeout=20) for _ in workers]
            for process in workers:
                process.join(timeout=20)
                self.assertEqual(process.exitcode, 0)
            self.assertEqual(found[0], found[1])
            self.assertEqual(found[0][0], "ok")
            self.assertEqual(len(list((self.output / ".generations").iterdir())), 1)
            build.checked_receipt(self.output / "join-smoke-layout")
        finally:
            for process in workers:
                if process.is_alive():
                    process.terminate()
                process.join(timeout=20)
            results.close()

    def test_terminated_allocator_leaves_no_public_image_and_rebuild_recovers(self):
        context = multiprocessing.get_context("fork")
        started = context.Event()
        hold = context.Event()
        original = self.generate

        def interrupted(generation, *args):
            original(generation, *args)
            started.set()
            hold.wait(30)

        with patch.object(build, "generate", side_effect=interrupted):
            process = context.Process(target=self.run_build)
            try:
                process.start()
                self.assertTrue(started.wait(20))
                self.assertFalse((self.output / "join-smoke-layout").exists())
                process.terminate()
                process.join(timeout=20)
                self.assertIsNotNone(process.exitcode)
            finally:
                if process.is_alive():
                    process.terminate()
                process.join(timeout=20)
        self.run_build()
        build.checked_receipt(self.output / "join-smoke-layout")


class BuildPolicyTests(unittest.TestCase):
    def test_explicit_override_cannot_masquerade_as_pinned_release(self):
        with patch.object(build, "executable", return_value=Path("/compiler")), \
                patch.object(build, "prepare_toolchain", return_value=(Path("/compiler"), {"mode": "release"})), \
                patch.object(build, "describe_toolchain", side_effect=lambda compiler, identity: (compiler, identity)):
            self.assertEqual(build.toolchain(None)[1]["mode"], "release")
            self.assertEqual(build.toolchain("/compiler")[1]["mode"], "development")

    def test_only_primary_profile_is_admitted(self):
        build.policy("lg_esl29_rev03", "default-tc")
        for board, key in (("generic", "default-tc"), ("lg_esl29_rev03", "install-code")):
            with self.assertRaisesRegex(ValueError, "not admitted"):
                build.policy(board, key)

    def test_missing_compiler_never_falls_back(self):
        with self.assertRaisesRegex(ValueError, "Missing overlay tool"):
            build.toolchain("deliberately-nonexistent-xdata-compiler")

    def test_stock_compiler_rejected_before_build(self):
        with patch.object(build, "executable", return_value=Path("/compiler")), \
                patch.object(build.subprocess, "check_output",
                             side_effect=["SDCC 4.2.0 #13081", "ordinary options only"]), \
                self.assertRaisesRegex(ValueError, "lacks --xdata-ownership"):
            build.toolchain("sdcc")


if __name__ == "__main__":
    unittest.main()
