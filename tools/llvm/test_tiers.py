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

    def test_tier_sets_are_nested(self):
        fast, _ = names("fast")
        smoke, _ = names("smoke")
        full, _ = names("full")
        self.assertTrue(set(fast) - {"lit"} <= set(smoke))
        self.assertTrue(set(fast) - {"lit"} <= set(full))


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


class Identity(unittest.TestCase):
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


if __name__ == "__main__":
    unittest.main()
