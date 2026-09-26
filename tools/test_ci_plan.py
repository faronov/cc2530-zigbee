# SPDX-License-Identifier: BSD-3-Clause
"""Selection policy and real Make consumers; no compiler, simulator or hardware."""
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import ci_plan as plan


class SelectionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.index = plan.source_index()

    def select(self, *paths):
        return plan.select(paths, self.index)

    def names(self, selected):
        return {(row["board"], row["directory"]) for row in selected["matrix"]["include"]}

    def test_full_preserves_106_workers_and_adds_two_interval_consumer_workers(self):
        selected = plan.full_plan("test")
        rows = selected["matrix"]["include"]
        self.assertEqual(len(rows), 108)
        self.assertEqual(len({r["name"] for r in rows}), 108)
        self.assertEqual(sum(row["directory"] == "mac-tx-interval" for row in rows), 2)
        self.assertEqual(sum(row["directory"] == "mac-handoff" for row in rows), 2)
        self.assertEqual(sum(row["directory"] == "mac-adapter" for row in rows), 2)
        self.assertEqual(sum(row["directory"] == "mac-reconfig" for row in rows), 2)
        self.assertEqual(sum(row["directory"] == "mac-link" for row in rows), 2)
        self.assertEqual(sum(row["directory"].startswith("banked-join-") for row in rows), 44)
        self.assertEqual({(r["board"], r["image"]) for r in rows if r["image"]},
                         {(b, i) for b in plan.BOARDS for i in plan.IMAGES})
        self.assertEqual({(r["board"], r["directory"]) for r in rows if not r["image"]},
                         {(b, c) for b in plan.BOARDS for c in plan.COMPONENTS})
        self.assertEqual([(r["board"], r["image"]) for r in rows if r["tools"]],
                         [("generic", "bringup")])
        self.assertEqual([(r["board"], r["directory"]) for r in rows if r["coverage"]],
                         [("generic", "ed-bdb-join")])
        for row in rows:
            self.assertEqual("test-common-core" in row["targets"].split(),
                             row["image"] == "debug_fixture")
        self.assertEqual(selected["campaign"], "full")

    def test_docs_only_never_need_make_or_firmware(self):
        with patch.object(plan, "source_index", side_effect=AssertionError("must not run")):
            for paths in ([], ["README.md"], ["docs/PLAN.md", ".github/agents/zigbee-stack.agent.md"]):
                selected = plan.select(paths)
                self.assertEqual(selected["tier"], "docs")
                self.assertEqual(selected["matrix"]["include"], [])

    def test_shared_and_unknown_inputs_fail_safe_to_full(self):
        for path in ("Makefile", ".github/workflows/ci.yml", "include/bdb_join.h",
                     "tests/host_mmio.h", "tests/boot_banked_security.py", "tools/ci_plan.py",
                     "boards/generic.c", "src/banked.c", "requirements-debug.txt",
                     "src/unowned.c", "strange\nfile"):
            with self.subTest(path=path):
                selected = self.select("README.md", path)
                self.assertEqual(selected["tier"], "full")
                self.assertEqual(len(selected["matrix"]["include"]), 108)
                self.assertEqual(selected["campaign"], "full")

    def test_failed_dependency_derivation_is_explicit_full_not_an_empty_pass(self):
        with patch.object(plan, "source_index", side_effect=ValueError("unsupported include")):
            selected = plan.select(["src/bdb_join.c"])
        self.assertEqual(selected["tier"], "full")
        self.assertIn("unsupported include", selected["reason"])

    def test_actual_shared_c_consumers_not_filename_matching(self):
        cases = {
            "src/bdb_join.c": {"ed-bdb-join"} | {name for name in plan.COMPONENTS if name.startswith("banked-join-")},
            "tests/bdb_join_layout.c": {"ed-bdb-join"},
            "src/security_keys.c": {"ed-security-keys", "ed-bdb-join", "banked-security"} |
                                  {name for name in plan.COMPONENTS if name.startswith("banked-join-")},
            "tests/banked_join_edges.c": {name for name in plan.COMPONENTS if name.startswith("banked-join-")},
            "src/zcl_temperature.c": {"compositions"},
            "tests/test_mac_tx_interval.c": {"mac-tx-interval", "mac-adapter"},
            "tests/test_mac_handoff.c": {"mac-handoff", "mac-adapter", "mac-reconfig"},
            "src/mac_adapter.c": {"mac-adapter", "mac-reconfig"},
            "tests/test_mac_observed.c": {"mac-adapter"},
            "tests/test_mac_adapter.c": {"mac-adapter", "mac-reconfig"},
            "tests/test_mac_reconfig.c": {"mac-reconfig"},
            "tests/test_mac_link_scan.c": {"mac-link"},
            "tests/test_mac_link_join.c": {"mac-link"},
            "tests/mac_adapter_fixture.c": {"mac-adapter", "mac-reconfig"},
            "examples/radio_tx_fixture.c": {"radio_tx_fixture"},
        }
        for path, units in cases.items():
            with self.subTest(path=path):
                selected = self.select(path)
                self.assertEqual(selected["tier"], "affected")
                self.assertEqual(self.names(selected), {(b, u) for b in plan.BOARDS for u in units})
                self.assertEqual(selected["campaign"], "deferred")

    def test_included_c_helpers_reach_all_compositions_and_fixtures(self):
        for board in plan.BOARDS:
            for unit in ("core", "mac-attempt", "mac-tx-interval", "mac-handoff", "mac-adapter", "mac-reconfig",
                         "compositions", "radio_link_fixture"):
                self.assertIn("tests/test_radio_autoack.c", self.index[board, unit])
            for unit in ("core", "counters", "resident-counter", "flash_fixture"):
                self.assertIn("tests/test_flash_write.c", self.index[board, unit])
        selected = self.select("tests/test_radio_autoack.c")
        self.assertTrue({(b, u) for b in plan.BOARDS
                         for u in ("debug_fixture", "mac-attempt", "mac-tx-interval", "mac-handoff", "mac-adapter", "compositions", "radio_link_fixture")}
                        <= self.names(selected))

    def test_source_union_and_core_are_preserved_without_running_tools(self):
        selected = self.select("src/flash_write.c", "src/zcl_temperature.c")
        for board in plan.BOARDS:
            self.assertIn((board, "debug_fixture"), self.names(selected))
            self.assertIn((board, "compositions"), self.names(selected))
            self.assertIn((board, "banked-security"), self.names(selected))
        self.assertFalse(any(row["tools"] for row in selected["matrix"]["include"]))
        for row in selected["matrix"]["include"]:
            if row["image"] == "debug_fixture":
                self.assertIn("test-common-core", row["targets"].split())

    def test_every_tracked_c_file_is_owned_or_explicit_full(self):
        names = plan.command(["git", "ls-files", "src/*.c", "tests/*.c", "boards/*.c", "examples/*.c"])
        for name in names.splitlines():
            selected = self.select(name)
            self.assertTrue(selected["matrix"]["include"], name)
            if selected["tier"] != "full":
                expected = {key for key, inputs in self.index.items() if name in inputs}
                actual = self.names(selected)
                for board, unit in expected:
                    self.assertIn((board, "debug_fixture" if unit == "core" else unit), actual, name)

    def test_nonliteral_and_outside_includes_cannot_underselect(self):
        with tempfile.TemporaryDirectory(prefix="include-policy-") as temp, \
                patch.object(plan, "ROOT", Path(temp)):
            source = Path(temp) / "input.c"
            for content in ('#include GENERATED_FILE\n', '#/**/include "other.c"\n',
                            '#include "../../../../../outside.h"\n', '??=include "other.c"\n'):
                source.write_text(content)
                with self.subTest(content=content), self.assertRaises(ValueError):
                    plan.include_closure([source.relative_to(plan.ROOT).as_posix()], {plan.ROOT})

    def test_only_an_explicit_make_generated_header_can_be_absent(self):
        with tempfile.TemporaryDirectory(prefix="generated-include-") as temp, \
                patch.object(plan, "ROOT", Path(temp)):
            root = Path(temp)
            source, generated = root/"input.c", root/"layout.h"
            source.write_text('#include "layout.h"\n')
            with self.assertRaisesRegex(ValueError, "Missing project include"):
                plan.include_closure(["input.c"], {root})
            self.assertEqual(plan.include_closure(["input.c"], {root}, {generated}), {"input.c"})
            generated.write_text('#include "stale-cache.h"\n')
            self.assertEqual(plan.include_closure(["input.c"], {root}, {generated}), {"input.c"})
            source.write_text('#include "unowned.h"\n')
            with self.assertRaisesRegex(ValueError, "Missing project include"):
                plan.include_closure(["input.c"], {root}, {generated})

    def test_target_coupled_join_recorder_cannot_silently_run_as_a_fast_host_test(self):
        row = next(row for row in plan.full_plan("test")["matrix"]["include"]
                   if row["board"] == "generic" and row["directory"] == "banked-join-success")
        planned = plan.recipe("generic", "banked-join-success", "build/fast/generic/banked-join-success")
        with patch.object(plan, "recipe", return_value=planned), \
                patch.object(plan.subprocess, "run") as run, patch("builtins.print") as output:
            with self.assertRaisesRegex(ValueError, "No host tests executed"):
                plan.run_fast({"matrix": {"include": [row]}})
            run.assert_called_once_with([plan.sys.executable, "-B", "tools/check_repository.py"],
                                        cwd=plan.ROOT, check=True)
            self.assertIn("No direct host test", output.call_args.args[0])

    def test_nightly_manual_and_release_are_always_full(self):
        with patch.object(plan, "accepted_base", side_effect=AssertionError("not needed")):
            for event in ("schedule", "workflow_dispatch", "release"):
                self.assertEqual(plan.github_plan(event, {}, "a" * 40, "o/r")["tier"], "full")

    def test_cancelled_failed_or_unaccepted_predecessor_cannot_hide_in_docs_push(self):
        with patch.object(plan, "accepted_base", return_value=False), \
                patch.object(plan, "changed_paths", side_effect=AssertionError("cannot trust baseline")):
            for event, data in (("push", {"before": "a" * 40}),
                                ("pull_request", {"pull_request": {"base": {"sha": "a" * 40}}}),
                                ("push", {"before": "0" * 40}), ("push", {})):
                self.assertEqual(plan.github_plan(event, data, "b" * 40, "o/r")["tier"], "full")

    def test_accepted_exact_baseline_allows_docs_or_affected(self):
        with patch.object(plan, "accepted_base", return_value=True), \
                patch.object(plan, "changed_paths", return_value={"README.md"}) as changed:
            selected = plan.github_plan("push", {"before": "a" * 40}, "b" * 40, "o/r")
        self.assertEqual(selected["tier"], "docs")
        changed.assert_called_once_with("a" * 40, "b" * 40)

    def test_baseline_approval_requires_exact_main_success_not_a_pr_merge(self):
        baseline = dict(headSha="a" * 40, headBranch="main", event="push", conclusion="success")
        for changes, accepted in (({}, True), ({"headSha": "b" * 40}, False),
                                  ({"headBranch": "topic"}, False), ({"event": "pull_request"}, False),
                                  ({"conclusion": "failure"}, False)):
            result = subprocess.CompletedProcess([], 0, json.dumps([baseline | changes]), "")
            with patch.object(plan.subprocess, "run", return_value=result):
                self.assertEqual(plan.accepted_base("o/r", "a" * 40), accepted)

    def test_unavailable_approval_is_explicit_not_permission_to_narrow(self):
        with patch.object(plan.subprocess, "run", side_effect=subprocess.TimeoutExpired("gh", 60)), \
                patch.object(plan.sys, "stderr") as errors:
            self.assertFalse(plan.accepted_base("o/r", "a" * 40))
            self.assertTrue(errors.write.called)
        failure = subprocess.CompletedProcess([], 1, "", "API unavailable")
        with patch.object(plan.subprocess, "run", return_value=failure), \
                patch.object(plan.sys, "stderr") as errors:
            self.assertFalse(plan.accepted_base("o/r", "a" * 40))
            self.assertTrue(errors.write.called)

    def test_required_gate_rejects_failures_cancellation_and_wrong_skips(self):
        plan.check_result("success", "success", 54)
        plan.check_result("success", "skipped", 0)
        for args in (("failure", "skipped", 0), ("cancelled", "skipped", 0),
                     ("success", "skipped", 2), ("success", "cancelled", 2),
                     ("success", "failure", 54), ("success", "success", 0),
                     ("success", "success", -1), ("success", "success", True)):
            with self.subTest(args=args), self.assertRaises(ValueError):
                plan.check_result(*args)

    def test_workflow_full_triggers_readonly_permissions_and_mandatory_gate(self):
        workflow = (plan.ROOT / ".github/workflows/ci.yml").read_text()
        for text in ("workflow_dispatch:", "schedule:", "release:", "types: [published]",
                     "fetch-depth: 0", "actions: read", "contents: read",
                     "matrix: ${{ fromJSON(needs.plan.outputs.matrix) }}",
                     "if: always()", "needs: [plan, checks]", "--check-result",
                     'ARTIFACT_CAMPAIGN="$ARTIFACT_CAMPAIGN"', "timeout-minutes: 15"):
            self.assertIn(text, workflow)
        self.assertNotIn("pull_request_target", workflow)
        self.assertNotIn("continue-on-error", workflow)
        self.assertNotIn("contents: write", workflow)


if __name__ == "__main__":
    unittest.main()
