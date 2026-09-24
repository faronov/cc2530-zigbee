# SPDX-License-Identifier: BSD-3-Clause
"""Coverage accounting tests use synthetic reports, never cached observations."""
import copy
import unittest

import host_coverage as coverage


class HostCoverageTests(unittest.TestCase):
    def report(self, count=1):
        return {"current_working_directory": str(coverage.ROOT), "files": [
            {"file": f"src/{name}.c", "lines": [
                {"line_number": 1, "count": count, "branches": [
                    {"count": count, "fallthrough": True, "throw": False},
                    {"count": 0, "fallthrough": False, "throw": False}]},
                {"line_number": 2, "count": 0, "branches": []}]}
            for name in coverage.CORE]}

    def test_unions_lines_and_branches_across_real_test_binaries(self):
        a, b = self.report(), self.report()
        for item in b["files"]:
            item["lines"][0]["branches"][0]["count"] = 0
            item["lines"][0]["branches"][1]["count"] = 1
            item["lines"][1]["count"] = 1
        for metrics in coverage.merge([a, b]).values():
            self.assertEqual((metrics["lines"], metrics["lines_hit"]), (2, 2))
            self.assertEqual((metrics["branches"], metrics["branches_hit"]), (2, 2))
            self.assertEqual(metrics["uncovered_lines"], [])
            self.assertEqual(metrics["uncovered_branches"], {})

    def test_reports_uncovered_edges_not_only_line_hits(self):
        result = coverage.merge([self.report()])
        for metrics in result.values():
            self.assertEqual(metrics["uncovered_lines"], [2])
            self.assertEqual(metrics["uncovered_branches"], {"1": [1]})

    def test_different_branch_shapes_and_invalid_counts_fail(self):
        a = self.report()
        for change in ("shape", "negative", "boolean", "missing"):
            b = copy.deepcopy(a)
            line = b["files"][0]["lines"][0]
            if change == "shape":
                line["branches"].pop()
            elif change == "negative":
                line["count"] = -1
            elif change == "boolean":
                line["count"] = True
            else:
                b["files"].pop()
            with self.subTest(change=change), self.assertRaises(ValueError):
                coverage.merge([a, b] if change == "shape" else [b])

    def test_empty_or_unexecuted_data_is_not_a_coverage_pass(self):
        for reports in ([], [self.report(0)]):
            with self.assertRaises(ValueError):
                coverage.merge(reports)

    def test_never_reports_test_sources_or_absolute_machine_paths(self):
        report = self.report()
        report["files"].append({"file": "tests/test_bdb_join.c", "lines": []})
        result = coverage.merge([report])
        self.assertEqual(set(result), {f"src/{name}.c" for name in coverage.CORE})


if __name__ == "__main__":
    unittest.main()
