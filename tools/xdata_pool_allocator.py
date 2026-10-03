#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Pure deterministic region/activation packing; no source, target or symbol rules."""
from collections import defaultdict
import time


ALLOCATOR_ALGORITHM_VERSION = 1


def pool(groups, starts, method):
    return dict(region=groups[0]["region"], owners=sorted(g["owner"] for g in groups),
                group_offsets={g["owner"]: starts[g["owner"]] for g in groups},
                original_bytes=sum(g["bytes"] for g in groups),
                width=max(starts[g["owner"]] + g["bytes"] for g in groups),
                method=method)


def flat(groups, statistics=None):
    remaining, result = list(groups), []
    while remaining:
        choices = []
        for seed in sorted(remaining, key=lambda g: g["owner"]):
            if statistics is not None:
                statistics["candidates"] = statistics.get("candidates", 0)+1
            chosen = [seed]
            for g in sorted(remaining, key=lambda g: (-g["bytes"], len(g["conflicts"]), g["owner"])):
                if g is not seed and all(h["owner"] not in g["conflicts"] for h in chosen):
                    chosen.append(g)
            p = pool(chosen, {g["owner"]: 0 for g in chosen}, "greedy")
            choices.append((p["width"]-p["original_bytes"], len(chosen), tuple(p["owners"]), chosen, p))
        _, _, _, chosen, p = min(choices, key=lambda c: c[:3])
        if p["width"] == p["original_bytes"]:
            break
        result.append(p)
        used = set(p["owners"])
        remaining = [g for g in remaining if g["owner"] not in used]
    return result


def chain(groups):
    by_owner = {g["owner"]: g for g in groups}
    starts, finish, visiting = {}, {}, set()

    def end(owner):
        if owner in finish:
            return finish[owner]
        if owner in visiting:
            raise ValueError("Cyclic activation constraints")
        visiting.add(owner)
        g = by_owner[owner]
        starts[owner] = max((end(p) for p in sorted(g["ancestors"]) if p in by_owner), default=0)
        finish[owner] = starts[owner] + g["bytes"]
        visiting.remove(owner)
        return finish[owner]

    for owner in sorted(by_owner):
        end(owner)
    p = pool(groups, starts, "ancestor-chain")
    return [p] if p["original_bytes"] > p["width"] else []


def bounded_flat(groups, limit=20000):
    """Best flat independent pool; a deterministic node cap, never a time cutoff."""
    ordered = sorted(groups, key=lambda g: (-g["bytes"], g["owner"]))
    best = None
    visited, capped = 0, False
    for maximum in sorted({g["bytes"] for g in groups}):
        pending = [g for g in ordered if g["bytes"] <= maximum]

        def search(todo, selected, weight):
            nonlocal visited, capped, best
            if visited >= limit:
                capped = True
                return
            visited += 1
            saving = weight - maximum
            key = (-saving, len(selected), tuple(sorted(g["owner"] for g in selected)))
            if saving > 0 and (best is None or key < best[0]):
                best = key, list(selected)
            bound = weight + sum(g["bytes"] for g in todo) - maximum
            if not todo or best is not None and bound < -best[0][0]:
                return
            g, rest = todo[0], todo[1:]
            search([h for h in rest if h["owner"] not in g["conflicts"]], selected+[g], weight+g["bytes"])
            search(rest, selected, weight)

        search(pending, [], 0)
        if capped:
            break
    result = [] if best is None else [pool(best[1], {g["owner"]: 0 for g in best[1]}, "bounded-exact-flat")]
    fallback = {}
    baseline = flat(groups, fallback)
    if sum(p["original_bytes"]-p["width"] for p in baseline) > sum(p["original_bytes"]-p["width"] for p in result):
        result = baseline
    return result, dict(nodes=visited, node_limit=limit, capped=capped,
                        greedy_candidates=fallback.get("candidates", 0),
                        fallback="deterministic greedy if it saves more")


def allocate(groups):
    total_started = time.perf_counter()
    if len({g["owner"] for g in groups}) != len(groups):
        raise ValueError("Activation group assigned to multiple regions")
    for a in groups:
        for b in groups:
            related = a["owner"] in b["ancestors"] or b["owner"] in a["ancestors"]
            if (b["owner"] in a["conflicts"]) != related:
                raise ValueError("Conflicts must equal symmetric activation ancestry")
    regions = defaultdict(list)
    for g in groups:
        if type(g["bytes"]) is not int or g["bytes"] <= 0 or g["owner"] in g["ancestors"]:
            raise ValueError("Invalid group weight or recursive activation")
        regions[g["region"]].append(g)
    validation_seconds = time.perf_counter()-total_started
    comparisons = {}
    for method in ("greedy", "ancestor-chain", "bounded-exact-flat"):
        started = time.perf_counter()
        plans, evidence, statistics = [], {}, {}
        for region in sorted(regions):
            members = regions[region]
            if method == "greedy":
                selected = flat(members, statistics)
            elif method == "ancestor-chain":
                selected = chain(members)
            else:
                selected, evidence[region] = bounded_flat(members)
            plans += selected
        comparisons[method] = dict(saving=sum(p["original_bytes"]-p["width"] for p in plans),
                                   pools=len(plans), seconds=time.perf_counter()-started, bounds=evidence,
                                   candidates=(statistics.get("candidates", 0) if method == "greedy" else
                                               len(regions) if method == "ancestor-chain" else
                                               sum(e["nodes"]+e["greedy_candidates"] for e in evidence.values())))
        if method == "ancestor-chain":
            optimal = plans
    bound = comparisons["ancestor-chain"]["saving"]
    if any(r["saving"] > bound for r in comparisons.values()):
        raise ValueError("Placement exceeds weighted-chain region bound")
    return optimal, dict(algorithms=comparisons, groups=len(groups), regions=len(regions),
                         conflict_edges=sum(len(g["conflicts"]) for g in groups)//2,
                         constraint_validation_seconds=validation_seconds,
                         allocation_seconds=time.perf_counter()-total_started,
                         optimum_same_region_saving=bound,
                         proof="Each region needs its heaviest ancestor chain; emitted intervals attain it. "
                               "Nonselected groups stay allocated; selecting fewer cannot improve this bound.")
