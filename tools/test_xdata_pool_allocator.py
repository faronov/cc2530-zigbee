#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Small exact models and malformed-input checks for target-independent packing."""
import copy
import itertools
import unittest

from xdata_pool_allocator import allocate, bounded_flat


def groups(weights, edges=(), regions=None):
    parents = {str(i): set() for i in range(len(weights))}
    for a, b in edges:
        parents[str(b)].add(str(a))
    for _ in weights:
        for owner in parents:
            parents[owner] |= set().union(*(parents[a] for a in parents[owner]))
    return [dict(owner=str(i), bytes=weight, region=(regions or ["r"]*len(weights))[i],
                 ancestors=sorted(parents[str(i)]),
                 conflicts=sorted(a for a in parents if a in parents[str(i)] or str(i) in parents[a]))
            for i, weight in enumerate(weights)]


class AllocationTests(unittest.TestCase):
    def test_siblings_overlap_ancestors_do_not(self):
        plans, result = allocate(groups([4, 3, 5], [(0, 1), (0, 2)]))
        self.assertEqual(result["optimum_same_region_saving"], 3)
        self.assertEqual(plans[0]["group_offsets"], {"0": 0, "1": 4, "2": 4})
        self.assertEqual(plans[0]["width"], 9)

    def test_diamond_and_all_chain(self):
        plans, _ = allocate(groups([2, 3, 5, 7], [(0, 1), (0, 2), (1, 3), (2, 3)]))
        self.assertEqual(plans[0]["width"], 14)
        self.assertEqual(allocate(groups([2, 3, 5], [(0, 1), (1, 2)]))[0], [])

    def test_regions_are_independent(self):
        plans, report = allocate(groups([2, 3, 5, 6], regions=["a", "a", "b", "b"]))
        self.assertEqual(len(plans), 2)
        self.assertEqual(report["optimum_same_region_saving"], 7)

    def test_input_permutation_is_deterministic(self):
        original = groups([2, 3, 5, 7], [(0, 1), (0, 2), (1, 3), (2, 3)])
        expected = allocate(original)[0]
        for permutation in itertools.permutations(original):
            self.assertEqual(allocate(permutation)[0], expected)

    def test_exhaustive_small_dags_match_weighted_clique_bound(self):
        edges = list(itertools.combinations(range(4), 2))
        for mask in range(1 << len(edges)):
            sample = groups([2, 1, 3, 2], [e for i, e in enumerate(edges) if mask >> i & 1])
            bound = max(sum(g["bytes"] for g in subset)
                        for n in range(1, 5) for subset in itertools.combinations(sample, n)
                        if all(b["owner"] in a["conflicts"] for a, b in itertools.combinations(subset, 2)))
            _, report = allocate(sample)
            self.assertEqual(report["optimum_same_region_saving"], 8-bound)

    def test_node_cap_falls_back_deterministically(self):
        sample = groups([1, 2, 3, 4])
        first = bounded_flat(sample, limit=1)
        self.assertEqual(first, bounded_flat(sample, limit=1))
        self.assertTrue(first[1]["capped"])
        self.assertEqual(first[1]["nodes"], 1)
        self.assertEqual(first[0][0]["width"], 4)

    def test_bad_constraints_fail_closed(self):
        samples = []
        base = groups([1, 2], [(0, 1)])
        samples.append(base + [copy.deepcopy(base[0])])
        for field, value in (("bytes", 0), ("bytes", True), ("ancestors", ["0"]),
                             ("conflicts", [])):
            broken = copy.deepcopy(base)
            broken[0][field] = value
            samples.append(broken)
        samples.append(groups([1, 2], [(0, 1), (1, 0)]))
        for sample in samples:
            with self.subTest(sample=sample), self.assertRaises(ValueError):
                allocate(sample)

    def test_empty_population(self):
        self.assertEqual(allocate([])[0], [])


if __name__ == "__main__":
    unittest.main()
