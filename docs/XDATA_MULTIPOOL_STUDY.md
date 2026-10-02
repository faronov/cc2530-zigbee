1. Production l_XSEG: **7676**.
2. V1 l_XSEG: **7637**.
3. Multi-pool l_XSEG: **7123**.
4. Total measured saving: **553 bytes**.
5. Saving beyond v1: **514 bytes**.
6. Number of pools: **36**.
7. Selected homes: **434**.
8. Selected functions: **326**.
9. Ownership source: **frozen compiler-emitted version-1 XDATA sidecars**.
10. Source/name ownership heuristics used: **NO**.
11. CODE changed: **YES, relocation operands only; populated size and instruction sequences unchanged**.
12. ABI changed: **NO**.
13. Complete MCU successful join replay: **NOT RUN to completion; the final-image replay is running**.
14. Full application CI: **PARTIAL**.

# Compiler-owned, region-aware physical XDATA reuse

This is an isolated experiment on branch `xdata-multipool-study`, based on
the compiler study commit `98b7c4b`. The application remains the production
`6ba00392d1a5ebd297fd0de809f2ccaa92571682` configuration:
`BOARD=lg_esl29_rev03`, `JOIN_SMOKE_KEY_MODE=default-tc`, SDCC 4.2.0 #13081.
The compiler implementation is frozen. Production C, headers, Makefile,
protocol limits, memory ceilings, production admission catalogs and the
preserved v1 image were not changed.

**The physical and independently admitted result exceeds the 150-byte
stretch target. Runtime/CI completion is not inferred from that result.**
Ordinary headroom is now **557 bytes**, versus 4 initially.
Dead CODE/XDATA saving included = **0**.

The earlier, separate v2 study is committed as `aaeca6e` on
`xdata-lifetime-study`. Its complete synthetic join passed 1142 steps /
559709 peripheral stops with READY and continuing service, using its
7612-byte image. That result is **not** runtime evidence for this 7123-byte
multi-pool image.

## Compiler metadata consumed

The compiler sidecar schema is unchanged: version, module and per-object
symbol, name, owner, owner symbol, class, area, CDB key, width,
absolute/address facts, address-taken flag and owner ISR/reentrancy flags.
All 48 sidecars and the preliminary image's nine identities are bound by
[`input-identities.json`](../experiments/xdata/multipool/input-identities.json).
The frozen compiler binary SHA256 remains
`273315806c1316c6aaf88207ca27db307ddda9f764f9b8d6f44ebbf1688e0a99`;
its existing regressions were rerun, without modifying or replacing it.

Allowed activation-home classes are `LOCAL`, `FIRST_ARGUMENT_HOME`,
`REGISTER_ARGUMENT_HOME`, `COMPILER_TEMP` and `INLINE_RETURN_HOME`.
`GLOBAL`, `FILE_STATIC`, `STATIC_LOCAL`, `PARAM_CALLER_WRITTEN`, `UNKNOWN`,
ISR and reentrant storage cannot enter a pool.

The actual selected population uses 290 `FIRST_ARGUMENT_HOME` objects
(691 bytes), 141 `LOCAL` objects (203 bytes), and three `INLINE_RETURN_HOME`
objects (three bytes). Register-argument and compiler-temporary classes are
supported conservatively but contribute no selected homes in this image.

The CDB and assembler bind the compiler's explicit owner/class claims to
real allocation keys, symbols, widths, function entries and references.
They do not replace the compiler with parameter-name or `_PARM_` guesses.
A forged but structurally plausible class cannot be diagnosed from a
variable spelling; it is rejected against the frozen sidecar identity.
Structural mismatches are also checked independently of those identities.

`parameter_names`, `legacy_scopes`, `legacy_classification` and
`legacy_caller_written` remain only for the original v1 regression path.
The complete new analysis/admission path ran with all four mocked to raise.
The generic allocator itself imports none of the application, source
parser, compiler-name or CC2530 rules.

## Physical region model

The preliminary linked `.rel` area sizes, relocated `.rst` declarations,
CDB locations and retained compiler classes reconstruct:

* 43 nonempty module regions, in actual link order;
* 48 home runs separated by 121 retained-object anchors;
* 337 region/anchor/platform constraints in the model;
* the fixed 26-byte runtime prefix and the ordinary XSEG boundary.

The explicit platform contract is
[`xdata_platform_contract.json`](../tools/xdata_platform_contract.json).
The complete linked preliminary region/run/anchor model is persisted in
[`regions.json`](../experiments/xdata/multipool/regions.json). Its embedded
header describes the preliminary image; the regenerated final header is
recorded separately by the final proof.
It identifies the naked banker and XMAP flash engine exclusions, their
special activation/return transfers, external roots, fixed status,
ordinary limit, forbidden alias and empty-area obligations. These are
platform assumptions, not application-function admission lists.

Every global, file-static, static-local or unknown object is retained as
an anchor, regardless of its name. Ownerless compiler storage is also an
anchor, even if it is classified as a compiler temporary. Its width, type
and order survive.
Its first/last relation to its module survives. Retained UPPER/CHILD
workspaces, union backing, radio/AES/flash storage and `_reserved_end`
fences therefore stay intact. No pool crosses a module or a retained
anchor. A group spanning two home runs is rejected, not split across
ownership regions.

Unselected compiler homes and all caller-written parameters remain in
their original order. The final ledger accounts for their shifted
addresses explicitly. A pool is inserted at its first selected declaration;
later selected declarations become relocatable equates. No instruction is
edited and no custom RAM area is introduced.

The final proof checks the complete `.ds` ledger against object XSEG sizes,
all CDB locations and actual pool ranges. It requires exact coverage
`[0, l_XSEG)`, with no holes, duplicate ownership or concealed allocation.
It also checks `s_XSEG=0`, `s_XISEG=l_XSEG`, empty XABS/XISEG/XINIT/PSEG,
the unchanged fixed status at `0x1e00`, and exclusion of `0x1f00..0x1fff`.
Stock CRT clearing thus covers the real physical allocation.

The existing CC2530 private-range proof is a prerequisite, not removed or
replaced by this model. Final caller observation headers are regenerated
from the relocated CDB/symbols. In particular, the AES DMA addresses now
move; native trace executables were rebuilt rather than reused from v1/v2.

## Lifetime / escape model

Each owner has one whole-activation lifetime. Every selected home of that
owner receives a contiguous subrange of its group, and a group belongs to
only one pool. No intra-function lifetime shortening or caller-written
parameter overlay is attempted.

The actual linked call graph supplies transitive ancestry. An ancestor and
descendant can occupy the same bounding reservation **only at disjoint
offsets**; they can never share a physical byte. Unrelated activations may
share bytes. This distinguishes a reservation from a flat overlay where
all owners start at offset zero.

The existing DPTR state proof begins every home uninitialized. It follows
all owner CFG paths, tracks individual initialized bytes, rejects any read
before definition, and rejects partial/out-of-bounds addressing, address
transfer through DPL/DPH, live home addresses at calls, implicit CODE
consumers and address returns. Every reference must belong to the exact
compiler-reported owner. The proof is rerun at the final relocated address.

Unknown indirect transfers, interrupt returns and recursive activation
fail closed. The reviewed XMAP transfer has its explicit activation edge.
Interrupt-enable writes must be clearing operations; the observed final
IE writes are the startup zero write and the banker stop's EA clear.
There is no admitted asynchronous ISR path.

These are closed-image, valid-C-object assumptions. Forged integer
pointers, out-of-bounds accesses, externally enabling interrupts, new
reentrant use or a new computed target do not inherit this admission.

## Allocator algorithm

Three deterministic algorithms run on the same region-partitioned input:

| Method | Candidate states/layouts examined | Pools | Saving |
|---|---:|---:|---:|
| Weighted multi-seed greedy flat pools | 521 | 70 | 528 |
| Weighted ancestor-chain intervals | 45 | 36 | **553** |
| Bounded best-flat-pool search with greedy fallback | 4083 | 70 | 528 |

The bounded search visits at most 20000 nodes per region and uses an
operation count, not a nondeterministic time cutoff. None of the current
regions reaches that cap. Its objective is the best flat independent pool,
compared against the multi-pool greedy fallback; it is not claimed to be
an exact solver for every possible flat partition.

The selected algorithm places a group after the maximum finish of its
eligible ancestors in the same region. Its extent equals the weight of
that region's heaviest ancestor chain. Such a chain is simultaneously live,
so it is also a lower bound on required storage. The construction attains
the bound independently in every region: **553 is the exact optimum for
this conservative whole-group, same-region model**, not a general XDATA
optimum.

Leaving a group unselected still consumes its original bytes and cannot
improve that bound. The 25-byte gap between greedy and the chain result is
proved, not estimated. The result uses one pool in every region with a
positive saving. Selection does not minimize the number of participating
functions as a separate objective.

Constraint validation is quadratic in group count. The chain computation
uses memoized ancestry; the comparison algorithms are bounded/deterministic.
No unbounded exhaustive subset search is used in the physical pipeline.
Small exhaustive DAG tests check the achieved weighted-clique bound, along
with diamonds, chains, permutations, regions, malformed relations, cycles
and deterministic branch-cap fallback.

## Candidate population

The original conservative filter is preserved: **492 homes / 1000 bytes /
355 activation groups**. Compiler ownership reconciliation has zero owner
mismatches across 1316 function-scoped objects / 2849 bytes.

Reachability excludes 18 otherwise-eligible owners / 25 homes / 60 bytes
from the new allocation, leaving **467 homes / 940 bytes / 337 groups**.
Their 4609 conflict edges are derived from linked activation ancestry.
There are 45 regions with eligible groups. Address-taken CODE relocations
are additional roots; banked-call operands are not mistaken for such roots.

The final selection is **434 homes / 326 functions / 897 original bytes**,
backed by **344 physical bytes**. The other 33 live eligible homes occupy
43 unchanged bytes. None of the previously identified unreachable code
or data was stripped or counted toward the saving.

## Physical pools

Ranges below are inclusive hexadecimal ordinary-XSEG addresses. The full
owner list, exact CDB keys, group offsets and home offsets for **every row**
are recorded in [`pools.json`](../experiments/xdata/multipool/pools.json);
[`proof.json`](../experiments/xdata/multipool/proof.json) independently
reconstructs the final physical ranges and supplies concrete home witnesses.

| Pool | Owning home run | Physical range | Owners | Before | Physical | Saved |
|---|---|---|---:|---:|---:|---:|
| pool_0 | aes:10 | `083f..0854` | 9 | 40 | 22 | 18 |
| pool_1 | aps_frame:28 | `0f72..0f77` | 4 | 12 | 6 | 6 |
| pool_2 | bdb_join:44 | `13b3..13be` | 14 | 37 | 12 | 25 |
| pool_3 | bdb_join_init:45 | `13f8..13fb` | 5 | 10 | 4 | 6 |
| pool_4 | ccm_star:11 | `086d..0877` | 5 | 14 | 11 | 3 |
| pool_5 | ed_wire:29 | `0f9d..0fa8` | 10 | 27 | 12 | 15 |
| pool_6 | flash_write:7 | `06b9..06bc` | 3 | 5 | 4 | 1 |
| pool_7 | mac_adapter:26 | `0ef1..0efc` | 19 | 54 | 12 | 42 |
| pool_8 | mac_association:31 | `10b1..10b4` | 5 | 9 | 4 | 5 |
| pool_9 | mac_attempt:22 | `0c44..0c4b` | 11 | 34 | 8 | 26 |
| pool_10 | mac_epoch:18 | `0a84..0a8d` | 3 | 12 | 10 | 2 |
| pool_11 | mac_frame:23 | `0c7f..0c8b` | 12 | 31 | 13 | 18 |
| pool_12 | mac_join:33 | `1130..1137` | 8 | 17 | 8 | 9 |
| pool_13 | mac_link_child_workspace:2 | `0514..051c` | 10 | 25 | 9 | 16 |
| pool_14 | mac_link_driver:46 | `1412..141f` | 10 | 28 | 14 | 14 |
| pool_15 | mac_link_workspace:1 | `02aa..02b5` | 12 | 27 | 12 | 15 |
| pool_16 | mac_poll:32 | `10de..10ea` | 10 | 25 | 13 | 12 |
| pool_17 | mac_radio:19 | `0ae0..0aea` | 9 | 27 | 11 | 16 |
| pool_18 | mac_radio:20 | `0b20..0b24` | 3 | 7 | 5 | 2 |
| pool_19 | mac_radio:21 | `0b4c..0b4f` | 3 | 10 | 4 | 6 |
| pool_20 | mac_scan:39 | `11f7..1206` | 11 | 38 | 16 | 22 |
| pool_21 | mac_time:14 | `0900..090a` | 4 | 15 | 11 | 4 |
| pool_22 | mac_time:15 | `092e..0932` | 3 | 13 | 5 | 8 |
| pool_23 | mac_tx:24 | `0d20..0d27` | 21 | 54 | 8 | 46 |
| pool_24 | nv_record:8 | `06d9..06e7` | 10 | 24 | 15 | 9 |
| pool_25 | nwk_aps:40 | `1255..1268` | 15 | 68 | 20 | 48 |
| pool_26 | nwk_candidates:37 | `11bf..11c8` | 6 | 17 | 10 | 7 |
| pool_27 | nwk_frame:27 | `0f39..0f3f` | 4 | 14 | 7 | 7 |
| pool_28 | radio_autoack:16 | `0a0d..0a14` | 13 | 25 | 8 | 17 |
| pool_29 | radio_autoack:17 | `0a5c..0a61` | 6 | 15 | 6 | 9 |
| pool_30 | security_counter:9 | `0794..079b` | 10 | 23 | 8 | 15 |
| pool_31 | security_keys:30 | `1009..1012` | 26 | 56 | 10 | 46 |
| pool_32 | timebase:4 | `05c8..05cb` | 3 | 11 | 4 | 7 |
| pool_33 | zcl_sensor:43 | `136e..1370` | 9 | 11 | 3 | 8 |
| pool_34 | zdo_node:34 | `1168..116d` | 7 | 19 | 6 | 13 |
| pool_35 | zdo_runtime:42 | `1303..130f` | 13 | 43 | 13 | 30 |
| **Total** | | | **326** | **897** | **344** | **553** |

## Concrete proofs

Each following pair belongs to its table row's module; neither owner is a
transitive ancestor of the other. The shown physical intersection is real,
not merely a shared reservation name.

| Pool | Owner A / owner B | Shared physical bytes |
|---|---|---|
| pool_0 | `input_descriptor` / `pointer_location` | `0847..0848` |
| pool_1 | `aps_frame_decode` / `aps_frame_encode` | `0f72..0f74` |
| pool_2 | `bdb_join_advance` / `install` | `13b9..13ba` |
| pool_3 | `bdb_join_discovered_config` / `bdb_join_runtime_init` | `13fa..13fb` |
| pool_4 | `authenticate` / `counter` | `0873..0873` |
| pool_5 | `counter_value` / `header` | `0fa4..0fa5` |
| pool_6 | `flash_nv_erase` / `flash_nv_program` | `06b9..06b9` |
| pool_7 | `bounds` / `clock` | `0ef9..0efa` |
| pool_8 | `mac_association_init` / `mac_association_start` | `10b1..10b2` |
| pool_9 | `bounds` / `storage` | `0c48..0c49` |
| pool_10 | `mac_epoch_start` / `mac_epoch_step` | `0a84..0a85` |
| pool_11 | `address_policy` / `beacon_fields` | `0c83..0c83` |
| pool_12 | `abort_attempt` / `issue` | `1132..1132` |
| pool_13 | `child_work_address` / `child_work_disjoint` | `0514..0514` |
| pool_14 | `consumer` / `observe` | `1417..1418` |
| pool_15 | `held` / `link_work_external` | `02af..02af` |
| pool_16 | `accept_ack` / `link_ram_disjoint` | `10e5..10e6` |
| pool_17 | `bounds` / `sample` | `0ae6..0ae9` |
| pool_18 | `mac_radio_attempt` / `mac_radio_prepare` | `0b20..0b21` |
| pool_19 | `mac_radio_attempt_now` / `mac_radio_configure` | `0b4c..0b4d` |
| pool_20 | `fault` / `next_channel` | `11ff..1200` |
| pool_21 | `mac_time_init` / `mac_time_read_radio` | `0900..0903` |
| pool_22 | `mac_time_attempt_begin` / `mac_time_attempt_end` | `092e..0931` |
| pool_23 | `fault` / `interval_no_ack` | `0d22..0d22` |
| pool_24 | `caller` / `encode` | `06de..06df` |
| pool_25 | `acknowledgment` / `acknowledgment_matches` | `125f..1264` |
| pool_26 | `nwk_candidates_consider` / `nwk_candidates_get` | `11bf..11bf` |
| pool_27 | `emit_frame` / `header_shape` | `0f3d..0f3f` |
| pool_28 | `consume` / `observe` | `0a13..0a13` |
| pool_29 | `radio_autoack_attempt` / `radio_autoack_configure` | `0a5c..0a5d` |
| pool_30 | `caller` / `finish` | `079a..079a` |
| pool_31 | `adopt` / `open_aps` | `1009..1009` |
| pool_32 | `timebase_deadline_after` / `timebase_expired` | `05c8..05cb` |
| pool_33 | `configure` / `reply` | `136e..136e` |
| pool_34 | `descriptor_valid` / `read_descriptor` | `1168..116a` |
| pool_35 | `address_request` / `advance` | `1307..1308` |

All participating homes pass owner-only address use and initialized-before-
read checks on both images. `proof.json` includes one concrete pair of
**homes** per pool, with exact compiler keys/classes/widths, final addresses
and the actual linked `(instruction PC, byte offset)` read/write sites.
These witnesses are separately recomputed; their chosen pair need not be
the first group pair above.

For example, `Laes.input_descriptor$source$1_0$71` and
`Laes.pointer_location$p$1_0$52` overlap at `0x0847..0x0848`.
The former writes its bytes at linked PCs 258376/258379 before reads at
258383/258386. The latter writes at 256524/256527/256530 before reads at
256534/256537/256540. CFG analysis checks this per path, not just by sorting
PCs. Both addresses are materialized only in their owner DPTR uses and
never escape across a call or return.

## Resource comparison

[`measurements.json`](../experiments/xdata/multipool/measurements.json)
records independently loaded image resources.

| Resource | Production | v1 | Multi-pool |
|---|---:|---:|---:|
| l_XSEG | 7676 | 7637 | **7123** |
| Free ordinary XDATA | 4 | 43 | **557** |
| Total XDATA saving | 0 | 39 | **553** |
| Physical pools | 0 | 1 | 36 |
| Conservative eligible homes | 492 | 492 | 492 |
| Selected homes | 0 | 15 | 434 |
| Selected functions | 0 | 10 | 326 |
| Original selected bytes | 0 | 45 | 897 |
| Physical pool bytes | 0 | 6 | 344 |
| Populated CODE | 253445 | 253445 | 253445 |
| Common CODE | 32714 | 32714 | 32714 |
| Physical DATA backing | 51 | 51 | 51 |
| OSEG | 10 | 10 | 10 |
| BSEG bits | 88 | 88 | 88 |
| Static stack | 45 | 45 | 45 |
| Maximum bank depth | 8 | 8 | 8 |
| Smallest free CODE bank | 13 | 13 | 13 |
| Largest free CODE bank | 1418 | 1418 | 1418 |

Multi-pool minus v1: **XSEG -514, headroom +514, all CODE/DATA/OSEG/BSEG/
stack/bank-depth deltas zero**. The `.mem` report independently prints
7123/7680 external bytes and 253445 populated CODE bytes.
The raw `l_DSEG` span is 125 in all three images; it is not the 51-byte
physical DATA backing allocation and is recorded separately.

There are 18851 changed CODE bytes, all legitimate relocation operands.
CODE addresses, decoded instruction structure, branches, calls, entries,
runtime objects and populated byte count are unchanged. There is no
peephole, Local-RET, LTO, dead stripping or compiler allocator change.

## Independent verification

`tools/xdata_multipool.py --verify` does not call the placement solver to
decide whether a plan is safe. It reconstructs bound compiler objects,
eligibility, the actual graph, retained anchors and home runs, then checks
each manifest claim against them.

It reconstructs the exact assembly transformation, checks unchanged ABI
declarations, independently reassembles every affected module and compares
actual object bytes. The relocation auditor reconstructs all **26213
relocations / 253445 emitted bytes / 60 objects**. Non-relocation CODE
changes fail, as do graph or function-entry changes.

Actual CDB addresses and `.ds` ranges, not claimed manifest addresses,
establish pool bases, offsets, nonoverlap, downstream shifts, private
fences, runtime prefix and final byte coverage. The final relocated
nonescape/initialization proof is independent of the original eligibility.
The unchanged DATA-liveness and stack analyzers run through experimental
admission: 496 DATA functions / 2832 checked pairs and a 45-byte deepest
stack at bank depth 8.

Only the separate
[`multipool/identities.json`](../experiments/xdata/multipool/identities.json)
admits this version-3 image. Production and v1 catalogs remain unchanged.
Missing or unsupported profiles are rejected, not automatically repinned.

## Negative tests

[`negative-tests.json`](../experiments/xdata/multipool/negative-tests.json)
records **28 passing rejection cases**, including every requested category:
false owner/class, stale/missing/duplicate sidecar, wrong width,
owner/callgraph mismatch, ancestor/descendant byte overlap, caller-written
parameter, ISR/reentrant owner, escaped/uninitialized home, private-fence
crossing, foreign module, out-of-region storage, duplicate assignment,
false group offsets/saving, unchanged XSEG, broken CRT coverage, altered
fence, overlapping physical pools, incomplete ledger, wrong CDB address,
unknown computed edge and instruction mutation in both linked and
assembler forms.

The physical mutation tests cache only identical lifetime inputs proved
earlier in that process. That cache is test-only; the real verifier
reconstructs its evidence every time. The original v1 39-byte result and
12 negative cases still pass. The focused solver/relocation/admission/
DATA-stack/replay helper suite has **47 passing tests**, including explicit
rejection of unknown overlay schemas instead of legacy fallback.

## Runtime validation

Commands below run from the isolated multi-pool worktree. Existing compiler
study outputs are preserved under their original relative paths:
`build/sdcc-function/final-image` and
`build/sdcc-function/metadata-overlay`. The separate output is
`build/multipool/join-smoke-layout`.

```sh
PYTHONPATH=tools:tests python3 -B tools/xdata_multipool.py \
  --baseline build/sdcc-function/final-image/join-smoke-layout \
  --metadata build/sdcc-function/final-image \
  --bindings experiments/xdata/multipool/input-identities.json \
  --output build/multipool-evidence/search.json --analysis-only

PYTHONPATH=tools:tests python3 -B tools/xdata_multipool.py \
  --baseline build/sdcc-function/final-image/join-smoke-layout \
  --metadata build/sdcc-function/final-image \
  --bindings experiments/xdata/multipool/input-identities.json \
  --output build/multipool/join-smoke-layout

PYTHONPATH=tools:tests python3 -B tools/xdata_multipool.py \
  --output build/multipool/join-smoke-layout --verify

PYTHONPATH=tools:tests python3 -B tools/join_smoke_image.py \
  --output build/multipool/join-smoke-layout --board lg_esl29_rev03 \
  --key-mode default-tc --xdata-study \
  --header build/multipool/join-smoke-layout/join_smoke_layout.h

PYTHONPATH=tools:tests python3 -B tests/test_xdata_multipool.py \
  --candidate build/multipool/join-smoke-layout \
  --output experiments/xdata/multipool/negative-tests.json

PYTHONPATH=tools:tests python3 -B tests/test_xdata_lifetime.py \
  --baseline build/sdcc-function/final-image/join-smoke-layout \
  --candidate build/sdcc-function/metadata-overlay/join-smoke-layout

PYTHONPATH=tools:tests python3 -B -m unittest \
  test_xdata_pool_allocator test_xdata_relocations test_join_smoke_analysis \
  test_join_smoke_image test_join_smoke_replay
```

All seven commands above **PASS**. Candidate generation rejects an existing
output directory; preserve earlier images rather than overwriting them.
Compiler ownership regressions with the frozen patched/stock executables
also **PASS**. Metadata reconciliation retains 492/1000 eligibility and the
unchanged v1 selection; its 15 bound-metadata rejection cases and four
conservative flag mutations pass. Fourteen structurally malformed cases
are additionally rejected without relying on the identity comparison;
the semantically false caller-parameter class is bound to compiler output
rather than guessed from its spelling.

```sh
S=../sdcc-function-xdata-study/build/sdcc-function/sdcc-4.2.0+dfsg
python3 -B "$S/support/regression/test-xdata-ownership.py" \
  --sdcc "$(realpath "$S/bin/sdcc")" \
  --control "$(realpath ../sdcc-function-xdata-study/build/sdcc-function/stock/bin/sdcc)"
PYTHONPATH=tools:tests python3 -B tests/test_xdata_metadata.py \
  --layout build/sdcc-function/final-image/join-smoke-layout \
  --metadata build/sdcc-function/final-image \
  --output build/multipool-evidence/metadata-regressions.json
```

Both commands **PASS**.

The standalone typed adapter bridge was copied from the preserved v2
build and **reverified**, not accepted merely because a header existed:

```sh
PYTHONPATH=tools:tests python3 -B tests/verify_mac_adapter.py \
  --output build/multipool \
  --emit-header build/multipool/mac-adapter/mac_adapter_layout.h

make -s -j4 -o build/multipool/mac-adapter/mac_adapter_layout.h \
  BOARD=lg_esl29_rev03 JOIN_SMOKE_KEY_MODE=default-tc BUILD=build/multipool \
  test-join-smoke-host

make -s -j4 \
  -o build/multipool/join-smoke-layout/join_smoke_layout.h \
  -o build/multipool/mac-adapter/mac_adapter_layout.h \
  BOARD=lg_esl29_rev03 JOIN_SMOKE_KEY_MODE=default-tc BUILD=build/multipool \
  build/multipool/host-join-smoke-vectors \
  build/multipool/host-join-smoke-vectors-sanitize
```

Result: **PASS**. All 15 native/sanitized shallow/deep join scenarios and
native/sanitized ZCL checks pass. Both trace executables were freshly built
against the new caller header. `-o` preserves already verified headers;
ordinary Make image regeneration must not overwrite experimental layouts.

```sh
S51=../join-simulator/sdcc-4.2.0+dfsg/sim/ucsim/s51.src/s51
python3 -u -B tests/boot_join_smoke.py --output build/multipool \
  --board lg_esl29_rev03 --key-mode default-tc --xdata-study \
  --simulator "$S51" --case 2
python3 -u -B tests/boot_join_smoke.py --output build/multipool \
  --board lg_esl29_rev03 --key-mode default-tc --xdata-study \
  --simulator "$S51" --case 0 --limit 3
PYTHONPATH=tools:tests python3 -B tests/boot_xdata_overlay.py \
  --layout build/multipool/join-smoke-layout --simulator "$S51" \
  --output experiments/xdata/multipool/btr.json
python3 -u -B tests/boot_join_smoke.py --output build/multipool \
  --board lg_esl29_rev03 --key-mode default-tc --xdata-study \
  --simulator "$S51" --case 0
```

Case2: **PASS**, one complete rejection step, zero peripheral stops,
SP `0x53`. Bounded case0: **PASS as a prefix only**, three steps / 1974
peripheral stops, SP `0x71`. BTR: **PASS**, eighteen actual linked calls
covering fill, duplicate, full and expiry behavior at the new addresses.
The complete case0 command is **running**, not a completed result.
Directed owner execution currently covers the two BTR entries; exhaustive
per-owner execution of all 326 owners is **not claimed**.

The READY scheduling and real wire-delivered unsupported-application
fixture corrections are the reviewed v2 corrections, not relaxed expected
semantics. The DUT must still discard exactly one packet, avoid dropped
frames/faults and continue acknowledged reports.

An initial adapter command used `--header` instead of its documented
`--emit-header` and failed before execution; the corrected command passed.
An initial compiler regression used relative executable paths while its
fixtures change directory; rerunning with absolute executable paths passed.
A diagnostic admission wrapper successfully wrote its proof/header, then
failed to print a misnamed `.mem` file. The official admission command
subsequently passed. The first new negative-test run failed on a
bytes-valued test-cache JSON key; the corrected complete 28-case campaign
passed. None of these repairs changed firmware or semantic expectations.

### CI scope and omissions

`python3 -B tools/ci_plan.py --tier full` **PASS** enumerates the workload;
it does not execute it. `python3 -B tools/check_repository.py` **FAIL**:
the inherited compiler-study report contains a personal absolute path, and
the unchanged v1 `experiments/xdata/inventory.json` exceeds the source-file
size guard. Neither artifact was exempted to produce a green result.

Full GitHub Actions/all-board acceptance, the complete MCU corpus,
exhaustive artifact campaigns, other SDCC ports/memory models, and another
profile's physical overlay/runtime admission have **not run**. This
isolated, unpublished experiment is not full application CI.

The broader command
`python3 -B -m unittest discover -s tools -p 'test_*.py'` was also run here:
**927 tests in 635.808 seconds: 903 passed, 21 skipped, one failure,
two errors**. This is a failed subset run, not a full-CI pass.
[`host-tools.json`](../experiments/xdata/multipool/host-tools.json) records:

* `test_local_checks...test_direct_replaces_transmit_and_adds_only_host_join_caller`:
  unchanged expectation 13 native recipes versus actual 15.
* `test_radio_link_fixture...test_fresh_both_board_artifacts_and_snapshot_rejection`:
  existing generic radio-link `CODE extent changed` pin failure.
* `test_radio_tx_fixture...test_genuine_clock_metadata_for_every_coupled_profile`:
  the same radio-link fixture failure.

The failing Make/helper/verifier surfaces are unchanged from `98b7c4b`.
These same three failures were previously reproduced on pristine `af762a7`
in the v2 investigation. Neither expectations nor production pins were
altered. The focused suite was subsequently rerun with the added schema
routing and ownerless-anchor checks; it passes all 47 tests.

There was no flashing, RF, hardware access or private capture. All MCU
results here are synthetic, alias-aware instruction execution.

## Remaining opportunity

The same-region bound uses 940 eligible live bytes and requires 387 bytes:
344 pooled bytes plus 43 unchanged bytes. Thus **553 realized, zero
unrealized additional bytes under this exact model**. This does not repeat
the old globally packed 196/930 numbers as physically admissible savings.

The unselected live groups are:

[`remaining.json`](../experiments/xdata/multipool/remaining.json) preserves
their exact home keys, classes, conflicting same-region ancestors and
rejection reasons, plus the separately retained unreachable owners.

| Group | Bytes | Compiler classes | Region / blocking relation |
|---|---:|---|---|
| `flash.observe` | 10 | first argument, local | flash:6; descendant of `flash_nv_read` |
| `nwk_parent.nwk_parent_select` | 8 | local | nwk_parent:38; only eligible group |
| `nwk_aps_direct.nwk_aps_transmit` | 7 | local | nwk_aps_direct:41; only eligible group |
| `zigbee_mmo.zigbee_mmo_hash` | 6 | first argument, local | zigbee_mmo:12; only eligible group |
| `mac_tx.mac_tx_interval_stage` | 3 | first argument | mac_tx:25; retained-anchor isolation |
| `zigbee_key_hash.zigbee_key_hash` | 3 | first argument | zigbee_key_hash:13; only eligible group |
| `nwk_beacon.nwk_beacon_decode` | 2 | local | nwk_beacon:36; only eligible group |
| `clock.clock_select_init` | 1 | first argument | clock:5; ancestor of `effective_status` |
| `clock.effective_status` | 1 | first argument | clock:5; descendant of `clock_select_init` |
| `flash.flash_nv_read` | 1 | first argument | flash:6; ancestor of `observe` |
| `join_smoke.fault` | 1 | first argument | join_smoke:47; only eligible group |

Each has **zero incremental saving** without crossing its region/fence or
shortening its activation lifetime. These are activation/region blockers,
not a greedy heuristic gap. The 60 eligible-but-unreachable bytes stay
unchanged by policy. Caller-written parameter lifetimes, escape uncertainty,
retained storage and unknown/ISR/reentrant classes remain exclusions;
no additional safe saving is assigned to them without a new proof.

## Performance

Measured on the actual artifacts, with other validation processes active:

| Work | Seconds |
|---|---:|
| Read and parse 48 sidecars | 0.0103 |
| Bind preliminary image/sidecar/contract identities | 0.2607 |
| Preliminary analysis, graph, lifetimes and regions | 113.38 |
| Greedy comparison | 0.0175 |
| Ancestor-chain placement | 0.0026 |
| Bounded flat comparison | 0.0308 |
| Complete allocator including constraint validation | 0.2569 |
| Actual relink subprocess | 2.2720 |
| Independent verifier, including 36 concrete witnesses, first sample | 182.76 |
| Final immutable re-admission's independent verifier | 164.92 |

The relink timing is not represented as total end-to-end preparation time:
analysis, assembly and final proof are separate costs. The existing
artifact/CFG proof dominates; solving the placement is not the bottleneck.
Exact values and all per-region search-node counts are in the measurement
JSON. Timing is observational; placements and rejection decisions do not
depend on it.

## Genericity

The pure allocator consumes only weighted groups, region IDs, ancestry and
conflicts. Its output is independent of board, module names, C variable
spellings and XDATA base addresses.

The adapter is deliberately an explicit CC2530 closed-image experiment:
it consumes this project's linked-image decoder, ABI/IRAM/stack checks,
runtime conventions and platform contract. Another target needs its own
equally strong adapters/contracts, not a claim that these constants apply
to arbitrary MCS51 firmware.

An additional `lg_esl29_rev03 / install-code` build and **analysis-only**
placement passed with the same frozen compiler and unchanged allocator.
Its original XSEG is **7675**, with **490 eligible homes / 996 bytes**.
Reachability leaves **336 groups / 941 bytes / 4608 conflicts**. The
recomputed plan selects **432 homes / 324 owners / 36 pools**, with a
**549-byte theoretical saving**, giving a hypothetical XSEG of **7126**.
Its pool composition differs from default-TC; target addresses/owners were
not forced onto it. These are not physically realized or runtime-admitted
install-code savings.

[`install-analysis.json`](../experiments/xdata/multipool/install-analysis.json)
contains the different plans and bounds; its separate compiler/image
bindings are in `install-input-identities.json`. Compiler preparation,
DATA/stack checking and region/lifetime analysis passed:

```sh
P=$(realpath ../sdcc-function-xdata-study/build/sdcc-function/sdcc-4.2.0+dfsg/bin/sdcc)
make -s -j2 BOARD=lg_esl29_rev03 JOIN_SMOKE_KEY_MODE=install-code \
  BUILD=build/multipool-install SDCC="$P" \
  SDCC_FLAGS='-mmcs51 --model-large --std-c99 --debug --opt-code-size --Werror -Iinclude -DCC2530_BOARD=1 --xdata-ownership' \
  prepare-join-smoke-stack
PYTHONPATH=tools:tests python3 -B - <<'PY'
from pathlib import Path
from xdata_multipool import prepare
root = Path("build/multipool-install")
prepare(root / "join-smoke-layout", root,
        Path("experiments/xdata/multipool/install-input-identities.json"),
        Path("build/multipool-evidence/install-analysis.json"), True)
PY
```

Both operations **PASS**. The install-code allocator took 0.348 seconds,
including constraint validation; its preliminary analysis took 114.60
seconds. No additional profile was physically overlaid or executed.

Current compiler metadata is **sufficient for this bounded generic
placement**, but is not itself a lifetime or region proof. Final linking
still supplies instruction references, actual graph/indirect transfers,
address escape and initialization, retained fences, physical ledger, CRT
coverage and real sizes.

Useful later metadata includes stronger object/build identity binding,
stable semantic schema identifiers, explicit call-site facts and parameter
ABI/lifetime facts. None was required to obtain these 553 bytes, so the
compiler was not extended again.

A separate prelink allocator remains sufficient. Linker integration becomes
useful when it can consume trusted object ownership, final call edges and
explicit region/CRT obligations without a preliminary/relink cycle. Merely
creating extra sections still does not solve stock CRT clearing or ordinary
XSEG accounting. No measured limitation here requires LTO.

## Recommended next step

**A. Productionize the multi-pool allocator**, after completing this
candidate's outstanding runtime/CI acceptance. The existing metadata and
conservative model already realize 553 bytes, exceeding the requested
stretch goal without compiler or protocol changes. Robust build
integration, reproducible admission and validation coverage have higher
measured immediate value than another compiler redesign or speculative
lifetime expansion. This recommendation is not production admission.
