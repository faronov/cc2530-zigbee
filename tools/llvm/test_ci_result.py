# SPDX-License-Identifier: BSD-3-Clause
"""Production Actions exit/status, artifact failure and chained verdict tests."""
import copy
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import ci_result


def needs(passed=3):
    return {"toolchain": {"result": "success"}} | {
        f"full-{i}": {"result": "success" if i <= passed else "skipped",
                     "outputs": {"status": "SUSPENDED" if i < passed else "PASS" if i == passed else ""}}
        for i in range(1, 7)}


class CI(unittest.TestCase):
    def test_multiple_segments_end_in_pass_only_after_completion(self):
        for passed in (1, 2, 3, 6):
            self.assertEqual(ci_result.chain_segment(needs(passed)), passed)

    def test_upload_failure_cannot_be_pass_or_valid_continuation(self):
        for segment in (1, 3):
            broken = needs()
            broken[f"full-{segment}"]["result"] = "failure"
            with self.assertRaisesRegex(ValueError, "upload"):
                ci_result.chain_segment(broken)

    def test_fail_timeout_cancel_and_incomplete_are_not_pass(self):
        for status in ("FAIL", "TIMEOUT", "CANCELLED", "", "SUSPENDED"):
            broken = needs(6)
            broken["full-6"]["outputs"]["status"] = status
            with self.assertRaises(ValueError):
                ci_result.chain_segment(broken)
        for result in ("failure", "cancelled", "skipped"):
            broken = needs()
            broken["full-2"]["result"] = result
            with self.assertRaises(ValueError):
                ci_result.chain_segment(broken)

    def test_stale_receipt_and_exit_mismatch_are_rejected(self):
        for status, rc in ci_result.tiers.EXIT.items():
            r = dict(tier="full", status=status, segment=2)
            self.assertEqual(ci_result.segment_status(r, rc, 2), status)
            for wrong in (r | {"segment": 1}, r | {"tier": "smoke"}):
                with self.assertRaises(ValueError):
                    ci_result.segment_status(wrong, rc, 2)
            with self.assertRaises(ValueError):
                ci_result.segment_status(r, rc+1, 2)

    def test_later_execution_cannot_hide_an_earlier_missing_segment(self):
        broken = needs()
        broken["full-1"]["outputs"] = {}
        with self.assertRaises(ValueError):
            ci_result.chain_segment(broken)

    def test_final_acceptance_rechecks_all_nineteen_reports_and_frozen_sources(self):
        profile = ci_result.tiers.PROFILE
        cases = profile["simulator"]["full_cases"]+profile["simulator"]["variant_cases"]["parent-recovery-policy-c"]
        steps = {f"sim-case{c}": {"status": "PASS", "details": {}} for c in cases}
        steps |= {f"unit-fixture-proof{i}": {"status": "PASS"} for i in range(11)}
        image_files = {"fw/link/hw1.elf": b"unit fixture, not real firmware",
                       "fw/audit/hw1.hex": b"unit fixture, not a real HEX"}
        hashes = {p: ci_result.tiers.sha_bytes(data) for p, data in image_files.items()}
        steps["admission"] = {"status": "PASS", "details": {"measured": {
            "elf_sha256": hashes["fw/link/hw1.elf"], "hex_sha256": hashes["fw/audit/hw1.hex"]}}}
        r = dict(schema=ci_result.tiers.SCHEMA, tier="full", status="PASS", profile=profile,
                 selection=[{"name": n} for n in steps], steps=steps, artifacts=hashes,
                 identity={"variant": "parent-recovery-policy-c",
                           "firmware": {"commit": "frozen", "dirty": False},
                           "tooling": {"commit": "tooling", "dirty": False},
                           "compiler": {"llvm_commit": "compiler", "llvm_diff": None}})
        with tempfile.TemporaryDirectory() as d, patch.object(
                ci_result.tiers, "sim_check", return_value=lambda: (True, {})) as checks:
            out = Path(d)
            for p, data in image_files.items():
                (out/p).parent.mkdir(parents=True, exist_ok=True)
                (out/p).write_bytes(data)
            out.joinpath("receipt.json").write_text(json.dumps(ci_result.tiers.seal(r)))
            ci_result.accept_full(out, "frozen", "compiler")
            self.assertEqual(checks.call_count, 19)
            checks.return_value = lambda: (False, {})
            with self.assertRaisesRegex(ValueError, "case"):
                ci_result.accept_full(out, "frozen", "compiler")
            checks.return_value = lambda: (True, {})
            for field, value in (("status", "SUSPENDED"), ("profile", {}), ("steps", {})):
                wrong = copy.deepcopy(r)
                wrong[field] = value
                out.joinpath("receipt.json").write_text(json.dumps(ci_result.tiers.seal(wrong)))
                with self.subTest(field=field), self.assertRaises((KeyError, ValueError)):
                    ci_result.accept_full(out, "frozen", "compiler")
            out.joinpath("receipt.json").write_text(json.dumps(ci_result.tiers.seal(r)))
            for firmware, compiler in (("wrong", "compiler"), ("frozen", "wrong")):
                with self.assertRaises(ValueError):
                    ci_result.accept_full(out, firmware, compiler)
            (out/"fw/link/hw1.elf").write_bytes(b"corrupted")
            with self.assertRaisesRegex(ValueError, "published image"):
                ci_result.accept_full(out, "frozen", "compiler")


if __name__ == "__main__":
    unittest.main()
