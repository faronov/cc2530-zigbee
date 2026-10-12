# SPDX-License-Identifier: BSD-3-Clause
"""Selection, status and stale-evidence rules of tools/llvm/tiers.py (no compiler or simulator needed)."""
import argparse
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import tiers  # noqa: E402

PROFILE = json.loads(tiers.PROFILE_PATH.read_text())
SERVICE = dict(duration_ms=600084, protected_frames=120, replay_drops=12, reports=23,
               erases=0, programs=0, ordinary_rx_commands=0, security_checkpoints=0,
               flash_controller_nominal_us=0)


def args(tier, **extra):
    base = dict(tools=Path("/x/bin"), llvm_src=Path("/x/src"), llvm_build=None, lit_evidence=None,
                simulator=Path("/x/s51"), output=Path("/x/out"), python="python3", jobs=2, sim_timeout=10,
                case_timeout=10, case0_budget=None, variant="production")
    if tier == "full":
        base["lit_evidence"] = Path("/x/lit.json")
    base.update(extra)
    return argparse.Namespace(**base)


def names(tier, **extra):
    steps, unselected = tiers.plan(tier, args(tier, **extra), PROFILE)
    return {s.name: s for s in steps}, unselected


class Selection(unittest.TestCase):
    def test_fast_runs_no_firmware_simulation(self):
        steps, unselected = names("fast")
        self.assertFalse([n for n in steps if n.startswith("sim-") or n == "vectors"])
        self.assertIn("simulator", unselected)
        self.assertIn("admission", steps)

    def test_smoke_has_bounded_prefix_only(self):
        steps, unselected = names("smoke")
        self.assertNotIn("sim-case0", steps)
        prefix = steps["sim-case0-prefix"]
        self.assertIn("--limit", [str(c) for c in prefix.cmd])
        self.assertIn("NOT commissioning", prefix.proves)
        for case in PROFILE["simulator"]["smoke_cases"]:
            self.assertNotIn("--limit", [str(c) for c in steps[f"sim-case{case}"].cmd])
        self.assertIn("complete case 0", unselected)

    def test_full_always_runs_complete_case0_and_every_case(self):
        for extra in ({}, {"case0_budget": 60}):
            steps, unselected = names("full", **extra)
            self.assertNotIn("--limit", [str(c) for c in steps["sim-case0"].cmd])
            for case in PROFILE["simulator"]["full_cases"]:
                self.assertIn(f"sim-case{case}", steps)
            self.assertIn("sim-case0", steps["sim-summary"].deps)
            self.assertEqual(unselected, {})
        self.assertEqual(PROFILE["simulator"]["terminals"]["0"], "serving")
        self.assertIn(0, PROFILE["simulator"]["full_cases"])

    def test_full_requires_compiler_lit(self):
        with self.assertRaises(SystemExit):
            tiers.plan("full", args("full", lit_evidence=None), PROFILE)

    def test_full_refuses_profile_without_complete_case0(self):
        for mutate in (lambda p: p["simulator"]["full_cases"].remove(0),
                       lambda p: p["simulator"]["terminals"].__setitem__("0", "joined")):
            weakened = copy.deepcopy(PROFILE)
            mutate(weakened)
            with self.assertRaises(SystemExit):
                tiers.plan("full", args("full"), weakened)

    def test_variant_reaches_build_and_audit(self):
        for variant in tiers.VARIANTS:
            steps, _ = names("smoke", variant=variant)
            for name in ("firmware-build", "firmware-audit", "sim-case2"):
                self.assertEqual(steps[name].env["FIRMWARE_VARIANT"], variant)
            command = [str(c) for c in steps["vectors"].cmd]
            self.assertEqual(command[command.index("--variant")+1], variant)

    def test_parent_profile_adds_every_new_case_without_removing_old_cases(self):
        parent, _ = names("full", variant="parent-recovery")
        ordinary, _ = names("full")
        for case in PROFILE["simulator"]["full_cases"]:
            self.assertIn(f"sim-case{case}", parent["sim-summary"].deps)
        for case in (18, 19, 20):
            self.assertIn(f"sim-case{case}", parent)
            self.assertIn(f"sim-case{case}", parent["sim-summary"].deps)
            self.assertNotIn(f"sim-case{case}", ordinary)
        self.assertEqual(parent["sim-summary"].cmd[-1], "parent-recovery")

    def test_tier_sets_are_nested(self):
        fast, _ = names("fast")
        smoke, _ = names("smoke")
        full, _ = names("full")
        self.assertTrue(set(fast) - {"lit"} <= set(smoke))
        self.assertTrue(set(fast) - {"lit"} <= set(full))

    def test_policy_c_requires_sustained_case_and_preserves_strict_selection(self):
        strict, _ = names("full", variant="parent-recovery")
        policy, _ = names("full", variant="parent-recovery-policy-c")
        self.assertNotIn("sim-case21", strict)
        self.assertIn("sim-case21", policy["sim-summary"].deps)
        for case in PROFILE["simulator"]["full_cases"] + [18, 19, 20]:
            self.assertIn(f"sim-case{case}", policy["sim-summary"].deps)
        self.assertEqual(PROFILE["simulator"]["terminals"]["21"], "serving")

    def test_long_cases_share_segment_deadline_and_have_separate_snapshots(self):
        steps, _ = names("full", variant="parent-recovery-policy-c", case0_budget=60)
        for case in (0, 21):
            s = steps[f"sim-case{case}"]
            self.assertTrue(s.suspendable)
            self.assertTrue(s.env["SNAP_DIR"].endswith(f"case{case}-snap"))
        self.assertEqual(steps["sim-case0"].env["STOP_AT_TIME"],
                         steps["sim-case21"].env["STOP_AT_TIME"])
        self.assertFalse(steps["sim-case16"].suspendable)


def step(name, cmd, **kw):
    return tiers.Step(name, "test", cmd=cmd, **kw)


class Runner(unittest.TestCase):
    def run_steps(self, steps, keep_going=False):
        with tempfile.TemporaryDirectory() as out:
            results = tiers.Runner(steps, out, 4, keep_going).run(progress=lambda _: None)
            return results, tiers.overall(results, [])

    def test_failure_is_nonzero_and_blocks_dependents(self):
        results, status = self.run_steps([step("a", ["false"]), step("b", ["true"], deps=["a"])], keep_going=True)
        self.assertEqual((results["a"]["status"], results["b"]["status"], status), ("FAIL", "NOT-RUN", "FAIL"))
        self.assertNotEqual(tiers.EXIT[status], 0)

    def test_timeout_is_distinct(self):
        results, status = self.run_steps([step("slow", ["sleep", "5"], timeout=0.5)])
        self.assertEqual((results["slow"]["status"], status), ("TIMEOUT", "TIMEOUT"))
        self.assertEqual(tiers.EXIT[status], 124)

    def test_fail_fast_cancels_running_steps(self):
        results, status = self.run_steps([step("slow", ["sleep", "30"]),
                                          step("bad", ["sh", "-c", "sleep 0.3; exit 3"])])
        self.assertEqual(results["slow"]["status"], "CANCELLED")
        self.assertEqual(status, "FAIL")

    def test_interrupt_is_cancelled(self):
        self.assertEqual(tiers.overall({"a": {"status": "PASS"}}, [2]), "CANCELLED")
        self.assertEqual(tiers.EXIT["CANCELLED"], 130)

    def test_suspended_case0_is_not_pass(self):
        results, status = self.run_steps([step("c0", ["sh", "-c", "exit 75"], suspendable=True),
                                          step("summary", ["true"], deps=["c0"])])
        self.assertEqual((results["c0"]["status"], results["summary"]["status"], status),
                         ("SUSPENDED", "PENDING", "SUSPENDED"))
        _, status = self.run_steps([step("c0", ["sh", "-c", "exit 75"])])
        self.assertEqual(status, "FAIL")

    def test_failing_proof_check_fails(self):
        results, status = self.run_steps([step("p", ["true"], check=lambda: (False, {}))])
        self.assertEqual(status, "FAIL")
        results, status = self.run_steps([step("p", ["true"], check=lambda: 1 / 0)])
        self.assertEqual(status, "FAIL")


def receipt(**over):
    ident = {"digest": "d1", "compiler": {"binaries": {"clang": "c1"}}, "firmware": {"tree": "f1"}}
    body = {"schema": tiers.SCHEMA, "tier": "full", "status": "PASS", "identity": ident,
            "selection_digest": "s1", "steps": {"sim-case0": {"status": "PASS", "details": {"terminal": "serving"}}}}
    body.update(over)
    return tiers.seal(body)


class Evidence(unittest.TestCase):
    def test_identical_sealed_pass_is_reusable(self):
        r = receipt()
        self.assertIsNone(tiers.reusable(r, r["identity"], "s1", "full"))

    def test_stale_or_wrong_receipts_are_refused(self):
        r = receipt()
        changed = copy.deepcopy(r["identity"])
        changed["compiler"]["binaries"]["clang"] = "c2"
        changed["digest"] = "d2"
        self.assertIn("compiler", tiers.reusable(r, changed, "s1", "full"))
        self.assertIn("selection", tiers.reusable(r, r["identity"], "s2", "full"))
        self.assertIn("tier", tiers.reusable(r, r["identity"], "s1", "smoke"))
        self.assertIn("FAIL", tiers.reusable(receipt(status="FAIL"), r["identity"], "s1", "full"))
        self.assertIn("SUSPENDED", tiers.reusable(receipt(status="SUSPENDED"), r["identity"], "s1", "full"))
        no_c0 = receipt(steps={"sim-case0": {"status": "PASS", "details": {"terminal": "fault"}}})
        self.assertIn("case 0", tiers.reusable(no_c0, r["identity"], "s1", "full"))

    def test_tampered_receipt_is_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "r.json"
            r = receipt()
            path.write_text(json.dumps(r))
            self.assertEqual(tiers.load_receipt(path)["status"], "PASS")
            r["status"] = "PASS"
            r["steps"]["sim-case0"]["details"]["terminal"] = "serving "
            path.write_text(json.dumps(r))
            with self.assertRaises(ValueError):
                tiers.load_receipt(path)

    def test_resume_all_paused_cases_and_reject_wrong_snapshot_or_identity(self):
        for paused in ((0,), (21,), (0, 21)):
            with self.subTest(paused=paused), tempfile.TemporaryDirectory() as d:
                out = Path(d)
                (out/"fw/link").mkdir(parents=True)
                (out/"vectors").mkdir()
                (out/"sims").mkdir()
                (out/"fw/link/hw1.elf").write_bytes(b"fixture-elf")
                (out/"vectors/v21.json").write_bytes(b"fixture-vector")
                (out/"vectors/vectors.json").write_text(json.dumps({
                    "vectors": {"21": tiers.sha_file(out/"vectors/v21.json")}}))
                statuses = {"admission": {"status": "PASS", "details": {
                    "measured": {"elf_sha256": tiers.sha_file(out/"fw/link/hw1.elf")}}}}
                steps = []
                for case in (0, 21):
                    name = f"sim-case{case}"
                    statuses[name] = dict(status="SUSPENDED" if case in paused else "PASS")
                    snap = out/f"case{case}-snap"/"bound.pkl"
                    snap.parent.mkdir()
                    snap.write_bytes(b"fixture; replay validates snapshot content")
                    (out/f"sims/case{case}.json").write_text(json.dumps({"resume": str(snap)}))
                    steps.append(step(name, ["true"], env={}, suspendable=True))
                prior = dict(identity={"digest": "identity"}, selection_digest="selection",
                             segment=1, steps=statuses)
                (out/"progress.json").write_text(json.dumps(prior))
                carried = tiers.resume_state(out, prior["identity"], "selection", steps)
                for s in steps:
                    case = int(s.name.removeprefix("sim-case"))
                    self.assertEqual("RESUME" in s.env, case in paused)
                    self.assertEqual(s.name in carried, case not in paused)
                for changed, selection in (({"digest": "different"}, "selection"),
                                           (prior["identity"], "different")):
                    with self.assertRaises(SystemExit):
                        tiers.resume_state(out, changed, selection, steps)
                case = paused[0]
                (out/f"sims/case{case}.json").write_text(json.dumps({
                    "resume": str(out/f"case{21 if case == 0 else 0}-snap/bound.pkl")}))
                with self.assertRaises(SystemExit):
                    tiers.resume_state(out, prior["identity"], "selection", steps)


class Identity(unittest.TestCase):
    def test_policy_flag_invalidates_profile_and_requires_parent_flag(self):
        with tempfile.TemporaryDirectory() as d:
            profile = Path(d) / "join-smoke-profile"
            command = ["make", "-s", f"BUILD={d}", "JOIN_SMOKE_KEY_MODE=default-tc",
                       "JOIN_SMOKE_PARENT_RECOVERY=1", str(profile)]
            for policy in ("0", "1", "0"):
                subprocess.run(command + [f"JOIN_SMOKE_REPLAY_POLICY_C={policy}"],
                               cwd=tiers.REPO, check=True, capture_output=True)
                self.assertTrue(profile.read_text().strip().endswith(f"default-tc 1 {policy}"))
            for overrides in (["JOIN_SMOKE_REPLAY_POLICY_C=invalid"],
                              ["JOIN_SMOKE_REPLAY_POLICY_C=1", "JOIN_SMOKE_PARENT_RECOVERY=0"]):
                result = subprocess.run(command + overrides, cwd=tiers.REPO,
                                        capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)

    def test_make_parent_variant_invalidates_shared_objects(self):
        with tempfile.TemporaryDirectory() as d:
            profile = Path(d) / "join-smoke-profile"
            command = ["make", "-s", f"BUILD={d}", "JOIN_SMOKE_KEY_MODE=default-tc", str(profile)]
            for variant in ("0", "1", "0"):
                subprocess.run(command + [f"JOIN_SMOKE_PARENT_RECOVERY={variant}"],
                               cwd=tiers.REPO, check=True, capture_output=True)
                self.assertTrue(profile.read_text().strip().endswith(f"default-tc {variant} 0"))
                before = profile.stat().st_mtime_ns
                subprocess.run(command + [f"JOIN_SMOKE_PARENT_RECOVERY={variant}"],
                               cwd=tiers.REPO, check=True, capture_output=True)
                self.assertEqual(profile.stat().st_mtime_ns, before)
            result = subprocess.run(command + ["JOIN_SMOKE_PARENT_RECOVERY=invalid"],
                                    cwd=tiers.REPO, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("JOIN_SMOKE_PARENT_RECOVERY must be 0 or 1", result.stderr)

    def test_docs_do_not_invalidate_but_code_does(self):
        with tempfile.TemporaryDirectory() as d:
            repo = Path(d)
            subprocess.run(["git", "init", "-q"], cwd=repo, check=True)
            (repo / "docs").mkdir()
            (repo / "src").mkdir()
            (repo / "docs/NOTES.md").write_text("a")
            (repo / "README.md").write_text("a")
            (repo / "src/x.c").write_text("int x;")
            subprocess.run(["git", "add", "."], cwd=repo, check=True)
            base = tiers.digest(tiers.tree_identity(repo))
            (repo / "docs/NOTES.md").write_text("b")
            (repo / "README.md").write_text("b")
            self.assertEqual(base, tiers.digest(tiers.tree_identity(repo)))
            (repo / "src/x.c").write_text("int y;")
            self.assertNotEqual(base, tiers.digest(tiers.tree_identity(repo)))

    def test_variant_is_a_distinct_identity(self):
        saved = (tiers.compiler_identity, tiers.firmware_identity, tiers.simulator_identity, tiers.host_identity)
        tiers.compiler_identity = lambda *a: {"c": 1}
        tiers.firmware_identity = lambda *a: {"tree": "t"}
        tiers.simulator_identity = tiers.host_identity = lambda *a: {}
        try:
            prod = tiers.identity(args("smoke"), PROFILE)
            diag = tiers.identity(args("smoke", variant="rx-diag"), PROFILE)
        finally:
            (tiers.compiler_identity, tiers.firmware_identity, tiers.simulator_identity, tiers.host_identity) = saved
        self.assertNotIn("variant", prod)
        self.assertEqual(diag["variant"], "rx-diag")
        self.assertNotEqual(prod["digest"], diag["digest"])

    def test_simulator_identity_tracks_binary(self):
        with tempfile.TemporaryDirectory() as d:
            s51 = Path(d) / "s51"
            s51.write_bytes(b"a")
            first = tiers.simulator_identity(s51)
            s51.write_bytes(b"b")
            self.assertNotEqual(first, tiers.simulator_identity(s51))


class Summary(unittest.TestCase):
    def summary(self, variant, mutate=None):
        with tempfile.TemporaryDirectory() as d:
            folder = Path(d)
            cases = PROFILE["simulator"]["full_cases"] + PROFILE["simulator"]["variant_cases"].get(variant, [])
            for case in cases:
                row = dict(complete=True, steps=11, of_steps=11, stack_bytes=106, peak_sp=0xe9,
                           terminal=PROFILE["simulator"]["terminals"][str(case)])
                if case == 21:
                    row["service"] = SERVICE.copy()
                if mutate:
                    row = mutate(case, row)
                if row is not None:
                    (folder/f"case{case}.json").write_text(json.dumps(row))
            result = subprocess.run([sys.executable, tiers.HERE/"sim_summary.py", folder,
                                     folder/"summary.json", variant], capture_output=True, text=True)
            return result.returncode, json.loads((folder/"summary.json").read_text())

    def test_all_current_variant_cases_required(self):
        for variant in tiers.VARIANTS:
            code, summary = self.summary(variant)
            self.assertEqual(code, 0)
            self.assertEqual(summary["passed"], summary["total"])
        for case in (0, 16, 17, 18, 19, 20):
            code, summary = self.summary("parent-recovery", lambda c, r: None if c == case else r)
            self.assertNotEqual(code, 0)
            self.assertEqual(summary["verdict"], "FAIL")

    def test_new_case_terminal_completion_and_stack_are_independent(self):
        for change in (dict(terminal="fault"), dict(complete=False), dict(steps=10),
                       dict(stack_bytes=129), dict(stack_bytes=True), dict(stack_bytes=-1)):
            code, summary = self.summary("parent-recovery", lambda c, r: r | change if c == 18 else r)
            self.assertNotEqual(code, 0)
            self.assertEqual(summary["verdict"], "FAIL")

    def test_sustained_completion_and_metrics_are_required_by_both_gates(self):
        changes = [dict(service=None), dict(terminal="fault"), dict(complete=False),
                   dict(steps=10), dict(stack_bytes=129), dict(stack_bytes=True),
                   dict(stack_bytes=-1)]
        for field, value in (("duration_ms", 10000), ("duration_ms", 601000),
                             ("duration_ms", 600000.0), ("protected_frames", 119),
                             ("replay_drops", 11), ("reports", 15), ("erases", 9),
                             ("programs", 1), ("ordinary_rx_commands", 1),
                             ("security_checkpoints", 1), ("flash_controller_nominal_us", 1),
                             ("reports", True), ("reports", -1)):
            changes.append(dict(service=SERVICE | {field: value}))
        changes.append(dict(service=SERVICE | {"unexpected": 0}))
        for change in changes:
            with self.subTest(change=change):
                code, summary = self.summary("parent-recovery-policy-c",
                    lambda c, r: r | change if c == 21 else r)
                self.assertNotEqual(code, 0)
                self.assertEqual(summary["verdict"], "FAIL")
                with tempfile.TemporaryDirectory() as d:
                    report = Path(d) / "case21.json"
                    row = dict(complete=True, steps=11, of_steps=11, terminal="serving",
                               stack_bytes=106, service=SERVICE.copy()) | change
                    report.write_text(json.dumps(row))
                    self.assertFalse(tiers.sim_check(report, 21, "serving")()[0])
        code, summary = self.summary("parent-recovery-policy-c",
                                    lambda c, r: None if c == 21 else r)
        self.assertNotEqual(code, 0)
        self.assertEqual(summary["verdict"], "FAIL")


if __name__ == "__main__":
    unittest.main()
