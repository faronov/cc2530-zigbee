# Bounded parent choice

The original BSD-3-Clause `nwk_parent_select()` implements #69's **offline
choice over one copied Beacon table**, not a scan, neighbor manager, association
procedure, BDB steering or authenticated join. It composes the existing
`nwk_candidates_get()`; the collector, codecs and scan controller are unchanged.
No board image links this module. Never flash `nwk_parent_test.ihx`.

## Primary facts and project policy

The same pinned **Core R22, 05-3474-22, April19,2017** used by the
[collector](NWK_CANDIDATES.md#primary-sources-and-selected-policy) was read
directly; its SHA256 remains
`991dd02b7e5764ac349c47d5c4cf0fbe01529ff6594df03e8dad3d3d4df9e274`.
No specification text, implementation, private packet or identity is imported.

| Primary location, printed page | Decision |
| --- | --- |
| 3.6.1.4.1,336 | Match the selected Extended PAN, join permission/ED capacity, link cost at most3 and potential-parent eligibility where that field exists. Use the most recent advertised Update ID with wrap accounted for; the selected network's NIB Update ID supplies a lower-bound condition. |
| 3.6.1.4.1,336 | Profile2 must not rank depth. The later general depth sentence does not override that explicit profile restriction. |
| 3.6.1.4.1,338 | Refusal and a new scan affect potential-parent state. This stateless selector does not implement those transitions: its caller must bind current eligibility to the supplied table. |
| 3.6.3.1,367 | Link cost is in1..7 and concerns reception probability. Constant7 is permitted. The selector does not invent an RSSI/correlation-to-cost conversion. |

The primary text requires wrap-aware recency but does not specify this API or
its numeric ambiguity policy. **Strict modulo256 half-range ordering is an
explicit conservative project choice**, not a newly discovered normative
equation: delta0 is equal,1..127 newer,129..255 older, and128 unordered.
It presumes an applicable comparison window; advertisements cannot prove
actual age, missed wraps, network identity or authenticated freshness.

A candidate must be equal/newer than **every** eligible candidate. A running
pairwise maximum would depend on insertion order for cyclic sets such as
`0,100,200`; this implementation rejects them. Among equal newest IDs, lowest
supplied cost wins, then lowest snapshot index. That deterministic tie-break
is project policy, never depth ranking.

## Inputs, output and ownership

`nwk_parent_policy_t` contains a wire-order Extended PAN ID, a four-bit
`potential_mask`, four link costs, `minimum_known` and `minimum_update_id`.
The target ID cannot be all-zero/all-FF. `minimum_known` must be exactly0/1.

The table must be an unchanged output/snapshot of the existing collector.
Its profile2/BO15/permit/ED-capacity admission remains authoritative; header
validation is not detection of arbitrary corruption in caller-edited entries.
Every populated slot needs a cost1..7, including excluded slots. Costs4..7
cannot win. Unused costs are ignored. Mask bit i permits slot i, including
when the caller has no potential-parent field; bits beyond count are errors.

When `minimum_known=1`, eligible candidates older than the supplied watermark
are excluded; an exact128 difference returns `AMBIGUOUS_UPDATE`. When0, the
watermark byte is ignored. **The caller must establish whether a selected-network
watermark is applicable.** Absence is not an automatic fallback when an existing
NIB value is inconvenient or unknown after recovery. This module owns no NIB
and cannot establish that prerequisite for a complete R22 join.

Link/eligibility metadata must describe this exact table snapshot. Collector
withdrawal compacts entries, so callers must rebind metadata afterward, even
if an old index still fits. The function retains no pointer/index; a successful
choice copies the entire candidate plus the index of that snapshot.

| Return | Effect |
| --- | --- |
| `OK` | Copy one selected candidate and its index. No radio/association/security acceptance. |
| `NONE` | No eligible candidate after target/cost/mask/watermark filtering. |
| `AMBIGUOUS_UPDATE` | Half-range watermark ambiguity or no ID dominates the eligible set. |
| `INVALID_ARGUMENT` | Null pointer, target/policy domain, out-of-count mask or invalid populated cost. |
| `INVALID_TABLE` | Collector header/getter validation failed. |

All non-OK returns preserve the complete output; all returns preserve both
inputs. Validation order is pointer/target/boolean, collector header, complete
mask/cost domains, then candidate filtering/ordering. Output publication follows
all failure decisions. Objects must be accessible and disjoint; invalid numeric
pointers/overlap are not diagnosed. Writable objects are caller-owned ordinary
storage, not compiler scratch, CODE, MMIO, reserved status or IRAM alias.
Foreground calls are serialized and non-reentrant on SDCC. Loops cover at most
four entries and sixteen pair comparisons; there is no allocation or retry.

## Evidence and resources

Both board definitions passed **host, image and alias-aware simulator** checks:

- 134,378 counted native cases plus direct pointer/CODE-policy/exact-allocation
  checks; all65,536 ID pairs and all65,536 candidate/watermark pairs; all byte
  values for mask, each cost slot and watermark-presence flag. ASan/UBSan pass.
- An independent linear-window oracle tries each possible256-value origin,
  rather than reusing pairwise dominance. It checks1,728 four-entry cases
  spanning wrap, equal IDs, costs, half-range ambiguity and cyclic sets.
- The genuine linked corpus executes42 shared outcome cases plus direct checks,
  including all six cyclic permutations, irrelevant foreign-network ambiguity,
  real collector withdrawal/rebinding, depth exclusion, copied outputs and errors.
- Complete contiguous CODE, raw-byte CDB, parsed map, all five immediately
  snapshotted ordered instruction inventories and per-object extents are pinned.
  There are53 artifact,8 result/guard and1 missing-alias negative controls.

| Actual SDCC4.2.0 composition | Resources |
| --- | --- |
| `mac_frame -> nwk_beacon -> nwk_candidates -> nwk_parent -> test` | 15,537/16,384 CODE;875 ordinary XDATA +64 reserved within1,024 |
| Production parent object | 1,428 CODE;57 XDATA;0 permanent DATA/overlay |
| Policy / copied choice | 15 /37 target bytes |
| Private modules / test caller / libc | 411 /442 /22 XDATA bytes |
| Stack | Start2F, checkpoint SP2E, full-run observed peak4D; unchanged cap7C |

The current43-byte CODE reduction comes only from complete-join decoder
pointer/length-copy lowering. Both boards retain all42 target cases and
53 artifact/8 result/1 alias negatives; parent-selection policy is unchanged.

The test image contains all real codecs, not successful decoder substitutes.
Both-board IHX/CDB are byte-identical. Reserved status tail, unallocated XDATA,
IRAM alias/upper guard, stack unwind and disabled IRQs are checked. The fixed
15-second simulator deadline remains. These are not full-stack resources,
hardware reception/link-quality evidence or proof of a valid parent on air.

```sh
make -j1 BOARD=generic BUILD=build/parent/generic test-nwk-parent test-nwk-parent-sanitize
make -j1 BOARD=lg_esl29_rev03 BUILD=build/parent/lg test-nwk-parent test-nwk-parent-sanitize
PYTHONPATH=tools python3 -B -m unittest test_local_checks test_m0_artifacts -q
```

`test-common`/`test-local` and the existing CI component jobs include the new
corpus once per board. Board-image exclusions and the existing seven-artifact
upload policy remain. No hardware operator is called by these commands.
#14 still needs actual scan/link-quality integration and the complete procedure;
#40/#45/#50 timing/ownership and BDB/security gates remain open.
