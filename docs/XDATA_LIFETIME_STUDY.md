# Physical XDATA lifetime study

1. **Current breakdown:** 7676 ordinary bytes = 4801 persistent/global bytes,
   2849 function-scoped bytes, and 26 runtime bytes. The status block remains
   separate at `0x1e00`; `0x1f00..0x1fff` is still the IRAM alias.
2. **Proven activation-local subset:** 1000 bytes / 492 homes pass the
   closed-image relocation, initialization and nonescape filter. This is
   **not** 1000 reclaimable bytes.
3. **Theoretical minimum:** activation-only model A is 7480 ordinary bytes;
   model B is 6746. These are attained mathematical placements in the stated
   conflict model, **not admitted firmware placements**: cross-module
   private fences/pointer-admission rules need additional constraints.
4. **Practical prototype:** a completely relinked image has **7637 bytes
   `l_XSEG`, down 39**, and 43 free bytes instead of 4. No instructions,
   protocol limits, buffers, production C, compiler flags or memory ceilings
   were changed. Ten functions share six physically allocated XSEG bytes.
5. **Validation:** native/sanitized shallow/deep join/ZCL cases, DATA liveness,
   static stack, immutable experimental image admission, complete MCU
   rejection case2, bounded case0 prefix, and a complete targeted MCU BTR
   scenario passed. Full successful-join MCU replay, full CI, hardware and
   RF were **not** run for this experiment.

## Scope and reproducibility

Production C/header/Makefile inputs are those of
`6ba00392d1a5ebd297fd0de809f2ccaa92571682`, board `lg_esl29_rev03`,
`default-tc`, SDCC 4.2.0 #13081. The isolated `xdata-lifetime-study` worktree
starts from the previous study branch, including its two documented
test-only prerequisites (host unsigned-bound warning and JSN1 version2).
No local-return peephole, `--peep-return`, DPTR optimization or LTO is used.
No hardware access occurs.

The baseline in this commit has stale production join pins. The experiment
uses **a separate exact nine-identity catalog**,
`experiments/xdata/identities.json`, through explicit `--xdata-study`.
It neither replaces the production pin catalog nor accepts arbitrary
manifests in place of immutable identity checks. Default admission/replay
continues to use the production catalog.

## XDATA inventory

The complete machine-readable inventory is
[`experiments/xdata/inventory.json`](../experiments/xdata/inventory.json).
Each row includes module, symbol, CDB key, exact linked address/size, owner,
category, address/escape state, references and eligibility/rejection reason.
The 1448 rows cover every ordinary byte exactly once before overlay.
The 48-byte absolute status reservation is deliberately not added to `l_XSEG`.

| Disjoint allocation category | Bytes | Objects |
|---|---:|---:|
| Persistent globals / file-static state / existing shared unions | 4801 | 121 |
| Function-local arrays | 7 | 2 |
| Function-local structs | 41 | 10 |
| Parameter homes, including first register-argument homes | 2389 | 999 |
| Function-local scalar/pointer homes | 398 | 291 |
| Compiler temporaries | 14 | 14 |
| Dedicated ABI return homes | 0 | 0 |
| Runtime shared homes | 26 | 11 |
| Unknown allocation size / unclassified storage | 0 | 0 |
| **Total ordinary XSEG** | **7676** | **1448** |
| **Function-scoped subtotal** | **2849** | **1316** |

Global scope further separates into 4053 bytes / 58 global declarations and
748 bytes / 63 file-static declarations. Existing unions/workspaces are
single retained allocations, never counted once per union member.

Address-taken and non-address-taken are **orthogonal properties**, not
additional rows to sum into this table. The filter proves 1000 bytes /
492 function homes nonescaping; the other 1849 function bytes remain
unknown/unsafe for this experiment. Unknown is treated as escaped, not
optimistically admitted. A pointer *stored in a home* is not the *address
of the home*.

The source compiler assembly `.ds` declarations, relocated `.rst`
allocations, module-qualified CDB types/locations, `.rel` XSEG offsets and
untruncated NoICE/map symbols must agree. Function parameter scope is
identified from explicit `_PARM_N` declarations; single-argument functions
also use source parameter names. There is no inference from the approximate
distance between source debug labels.

Runtime objects have no CDB. In particular, neither memcpy's nor memcmp's
last parameter occupies five bytes: each has a two-byte count followed by
a separate three-byte first-argument home. Recompiling the installed
`__memcpy.c` and `_memcmp.c` with `sdcc -mmcs51 --model-large -c`
reproduced the linked archive members **byte-for-byte**. Their assembly
declarations and object hashes are recorded in
`experiments/xdata/runtime-homes.json`; no runtime source is imported.
This avoids inventing object widths from gaps between exported symbols.

## Escape analysis

`tools/xdata_relocations.py` decodes every observed ASxxxx XH3 T/R mode,
including area-relative references, global symbols, three-byte low/mid/high
selections and elided relocation bytes. It includes the six explicit
runtime objects and six implicit startup/runtime archive members.
**26213 relocations in 60 objects reconstruct all 253445 emitted CODE
bytes exactly.** Unknown modes, incomplete members, conflicting emitted
bytes or unclassified initialized storage fail closed.

`tools/xdata_lifetime.py` resolves references by actual allocation ranges,
not globally matching potentially colliding static symbol spellings.
For an eligible home:

- Every relocated reference is an owner-local `MOV DPTR,#home+offset`.
- Each byte is initialized before any read on every reachable owner CFG
  path. Initial CRT zeroing is not used as an activation-local definition.
- The home address cannot be copied out of DPL/DPH, passed across a call,
  consumed by `MOVC`/computed transfer, or returned in result registers.
- DPTR increment and MOVX accesses stay within the declared object.
- Calls retain the entire caller home group as live until return.
- Caller-written `_PARM_N`, banker and flash-execution homes are excluded.

This admits 756 bytes of first-argument parameter homes, 226 local
scalar/pointer bytes, 15 local-struct bytes and three compiler-temporary
bytes. No local array passes. Addresses passed to a helper are rejected
even if that helper might in fact be synchronous and harmless.

The proof concerns the closed compiled foreground firmware and valid,
disjoint API objects. It is not a proof against arbitrary forged integer
pointers, pre-existing out-of-bounds C behavior or external debugger writes.
There is no DMA descriptor in the selected set. Existing MMIO/DMA/private
workspace contracts remain mandatory.

## Lifetime/interference analysis

The graph is the existing exact linked call graph, not a source-file
heuristic. It resolves near and banked calls to their real destinations,
pins the banker ABI, rejects unknown computed transfers/ISR returns, and
models the sole known flash XMAP transfer to the pinned copied engine.
The independent stack proof also checks reset interrupt disabling and
rejects later interrupt enabling, recursion and unclassified control flow.
CDB ISR metadata is checked; the absence of dynamic reentry comes from
the actual closed foreground graph, not an invented CDB reentrancy flag.

Two groups interfere whenever either owning function can transitively
call the other. All objects in a group are simultaneously live for the
entire activation. Siblings are reusable after return, but a caller and
its nested callee are not. No additional phase exclusions are asserted.

| Model | Eligible current homes | Minimum pool | Potential saving | Ordinary XSEG in this model |
|---|---:|---:|---:|---:|
| A: only proven nonescaping locals/temporaries | 244 | 48 | 196 | 7480 |
| B: A + callee-initialized first-argument homes | 1000 | 70 | 930 | 6746 |
| C: B + independently proved extra phase exclusions | 1000 | 70 | 930 | 6746 |

The allocator starts each group after the maximum weighted ancestor path.
Every ancestor ends before its descendant starts; unrelated intervals may
overlap. The maximum weighted chain is a matching lower bound, so this
placement attains the optimum **for this conflict model**. Caller-written
parameters are not implicitly included in B: their lifetime begins before
callee entry and needs a separate argument-setup proof.

The global mathematical placements are emitted in the inventory JSON.
They do not yet preserve all module-private exclusion fences, so **930
bytes is not a measured or fully semantic-proof-backed firmware saving**.
The 39-byte prototype is the admitted physical result.

## Best overlay candidates

Large unshared arrays are not present. The largest entire function-home
group is `ccm_star_crypt`, only 36 bytes, including 31 explicit parameter
bytes. Selecting an imaginary 80/96-byte pair would be misleading.

Largest filter-qualified groups, before placement constraints:

| Function | Qualified bytes | Conflicting qualified groups |
|---|---:|---:|
| flash.observe | 10 | 23 |
| nwk_aps.nwk_aps_receive | 10 | 100 |
| mac_link_child_workspace.child_work_disjoint | 9 | 29 |
| mac_time.mac_time_attempt_read | 9 | 10 |
| aes.aes128_encrypt_block | 8 | 42 |
| aes.observe | 8 | 25 |
| mac_epoch.mac_epoch_step | 8 | 36 |
| mac_scan.mac_scan_step | 8 | 40 |
| nwk_parent.nwk_parent_select | 8 | 16 |
| mac_poll.step | 7 | 33 |
| nwk_aps_direct.nwk_aps_transmit | 7 | 120 |
| aes.pointer_location | 6 | 24 |
| aes.sample | 6 | 33 |
| bdb_join.bdb_join_step | 6 | 219 |
| mac_adapter.mac_adapter_step | 6 | 59 |
| mac_link_child_workspace.child_work_inside | 6 | 36 |
| mac_link_workspace.location | 6 | 175 |
| mac_scan.mac_scan_start | 6 | 17 |
| mac_scan.next_channel | 6 | 8 |
| mac_time.operate | 6 | 49 |

The requested **top20 individual objects**, including exact addresses,
sizes, CDB keys and instruction references, are separately stored in
`experiments/xdata/top-objects.json`. All are four-byte scalar/struct or
first-argument homes, not large reclaimable buffers. Their individual
capacity is four bytes; realizable saving depends on a compatible group.
The full inventory has every conflict list and rejection reason.

## Prototype

Files:

- `tools/xdata_lifetime.py`: inventory, conservative DPTR escape/definition
  analysis, activation graph and bounded mathematical allocation models.
- `tools/xdata_relocations.py`: independent object relocation reconstruction.
- `tools/xdata_overlay.py`: selection, assembly transformation and final
  physical ownership verifier.
- `tools/join_smoke_image.py`: explicit experimental catalog/admission
  integration; the default production identity gate is unchanged.
- `tests/test_xdata_lifetime.py`: positive and negative proof regressions.
- `tests/boot_xdata_overlay.py`: targeted real-image MCU BTR scenario.
- `tests/boot_join_smoke.py`: explicit experimental identity selection only.
- `experiments/xdata/`: complete inventory, identities, runtime evidence,
  manifest, resource measurements, validation and dead-function reports.

The selector searches at most ten groups in `nwk_aps`, rejecting every
pair related by the actual transitive call graph. Names select the search
domain, **not a safety whitelist**.

The first prototype deliberately uses one module without a private
reserved-end fence. It reserves `_xdata_overlay_pool: .ds 6` **inside
XSEG**, then substitutes selected declarations with relocatable symbol
equates `home = _xdata_overlay_pool + offset`. Debug aliases remain
relocatable and retain their exact original CDB declarations.

This is the generated-symbol/alias alternative to separate named areas.
It avoids hiding allocations in custom areas outside the `l_XSEG` ledger
or changing CRT clearing. ASxxxx supports these same-area relocatable
equates without a frontend, linker or binary-patching change.
All source instructions, CODE areas and call ABIs remain unchanged.

The manifest binds baseline/candidate artifact identities, every owner,
every CDB object key, pool offset/width and claimed delta. The verifier
does not trust that claim: it reconstructs the transformed assembly,
reassembles the changed module independently, checks all unchanged object
bytes, reconstructs all final CODE bytes, compares actual linked graphs,
rechecks relocated owner CFGs, checks every resulting CDB allocation
address, and requires exact ordinary-byte coverage and `l_XSEG` reduction.
The baseline's existing XDATA, private-fence, DATA and stack proofs remain
prerequisites. Candidate DATA and stack proofs run independently too.
A failed proof makes the preparation/admission command fail.

## Measurements

| Resource | Baseline | Prototype | Delta |
|---|---:|---:|---:|
| **`l_XSEG` / memory-report XDATA** | **7676** | **7637** | **-39** |
| Free ordinary XDATA | 4 | 43 | +39 |
| Populated CODE | 253445 | 253445 | 0 |
| Common CODE | 32714 | 32714 | 0 |
| Physical DATA | 51 | 51 | 0 |
| OSEG | 10 | 10 | 0 |
| BSEG, bits | 88 | 88 | 0 |
| Static stack, bytes | 45 | 45 | 0 |
| Maximum bank depth | 8 | 8 | 0 |
| Smallest free bank | 13 | 13 | 0 |
| Largest free bank | 1418 | 1418 | 0 |

CODE bytes containing XDATA relocations and CRT length constants change,
but the instruction sequence and populated size do not.
The candidate's IHX SHA256 is
`bcd60dea20970a7aa439ed444de773acaf189d8b626bdbfe773e9bd36e327712`.

## Concrete ownership proof

All fifteen selected homes share physical **`0x1403..0x1408`**:

| Owner | Current selected homes | Bytes | Pool offsets |
|---|---|---:|---|
| acknowledgment | ctx, a | 6 | 0, 3 |
| acknowledgment_matches | ctx, sent | 6 | 0, 3 |
| nwk_aps_broadcast_put | ctx, entry | 6 | 0, 3 |
| nwk_aps_broadcast_slot | ctx, entry | 6 | 0, 3 |
| nwk_aps_complete | ctx, p | 6 | 0, 3 |
| advance | ctx | 3 | 0 |
| allocated | ctx | 3 | 0 |
| arm | ctx | 3 | 0 |
| nwk_aps_armed | ctx | 3 | 0 |
| nwk_aps_confirm | ctx | 3 | 0 |
| **Total before / after** | | **45 / 6** | |

For example, owner A `nwk_aps_broadcast_put` and owner B
`nwk_aps_broadcast_slot` each use both three-byte halves. Neither reaches
the other in the linked graph. `nwk_aps_receive` can call them sequentially,
but its own homes are **not** in this pool. It also calls acknowledgment
helpers; those calls return before another pool owner starts.
`advance` and `allocated` are similarly sequential callees, never nested
with another selected owner. Ancestor groups remain allocated elsewhere.

The `ctx` homes are written from DPL/DPH/B in each callee prologue. The
second homes are initialized from that invocation's context before any
read. Their addresses occur only in owner-local direct DPTR accesses;
passing the stored context/entry pointer does not pass the home address.
No caller-written `_PARM_N` is moved.

Keeping the pool within its original module preserves every other
module's contiguous private storage and fence ordering. Modules after
`nwk_aps` shift uniformly by 39 bytes; all linked references and CDB
locations are checked. No shared workspace, status field, DMA buffer,
NV state or application object is placed in the pool.

## Validation and exact limits

Commands below were run from the isolated worktree. `S51` denotes
`<repository-root>/build/join-simulator/sdcc-4.2.0+dfsg/sim/ucsim/s51.src/s51`,
the previously reviewed isolated alias/banking-capable simulator.

| Command | Result |
|---|---|
| `make -s -j4 BOARD=lg_esl29_rev03 JOIN_SMOKE_KEY_MODE=default-tc BUILD=build/xdata-study/baseline prepare-join-smoke-stack` | PASS, clean unchanged baseline |
| `python3 -B tools/xdata_lifetime.py build/xdata-study/baseline/join-smoke-layout --output experiments/xdata/inventory.json` | PASS, exact coverage and relocation reconstruction |
| `python3 -B tools/xdata_overlay.py --baseline build/xdata-study/baseline/join-smoke-layout --output build/xdata-study/overlay/join-smoke-layout` | PASS, fresh candidate output, real -39 bytes |
| `python3 -B tools/join_smoke_analysis.py --output build/xdata-study/overlay/join-smoke-layout --check-data --check-stack` | PASS |
| `make -s -j4 BOARD=lg_esl29_rev03 JOIN_SMOKE_KEY_MODE=default-tc BUILD=build/xdata-study/baseline test-join-smoke-host` | PASS, native/sanitized shallow/deep caller cases and ZCL |
| `python3 -B tests/test_xdata_lifetime.py --baseline build/xdata-study/baseline/join-smoke-layout --candidate build/xdata-study/overlay/join-smoke-layout` | PASS, positive resource checks and 12 negative checks |

For **each** `P=baseline` and `P=overlay`, these commands passed:

```sh
python3 -B tools/join_smoke_image.py \
  --output build/xdata-study/$P/join-smoke-layout \
  --board lg_esl29_rev03 --key-mode default-tc --xdata-study \
  --header build/xdata-study/$P/join-smoke-layout/join_smoke_layout.h

python3 -B tests/boot_join_smoke.py --output build/xdata-study/$P \
  --board lg_esl29_rev03 --key-mode default-tc --xdata-study \
  --simulator "$S51" --case 2

python3 -B tests/boot_join_smoke.py --output build/xdata-study/$P \
  --board lg_esl29_rev03 --key-mode default-tc --xdata-study \
  --simulator "$S51" --case 0 --limit 3

python3 -B tests/boot_xdata_overlay.py \
  --layout build/xdata-study/$P/join-smoke-layout --simulator "$S51" \
  --output build/xdata-study/$P-btr.json
```

The native/sanitized vector executables were built using the freshly
admitted baseline header. Baseline/candidate generated headers were
byte-compared equal before reusing those unchanged native oracles.
No semantic expected status, packet, journal or peripheral output changed.
`--xdata-study` changes only which immutable artifact catalog is required.

Case2 completed one step, zero peripheral stops, peak SP `0x53`.
The case0 prefix completed three steps / 1974 peripheral stops, peak
SP `0x71`, preserving alias/stack, unowned XDATA, DMA and flash guards.
It is **not** a completed join, and it does not execute the later NWK pool.

The additional BTR test therefore executes the **actual linked** selected
`broadcast_put`/`broadcast_slot` functions eighteen times, alternating
owners through table fill, duplicate detection, full rejection and
expiry/reuse. An independent byte oracle checks the complete context.
A simulator-only caller occupies already-unused common CODE; no deployed
instruction is patched or replaced. Bank-local near calls use the actual
near ABI, not the public far-call ABI. Both images pass.

Initial development checks rejected parser assumptions about absolute
status symbols, exported CDB function storage letters and macro-renamed
functions. The initial BTR harness also correctly failed because those
two internal APIs use near, not banked, returns. These tooling failures
were corrected; they were not counted as successful firmware runs.

**Not tested:** full successful-join MCU transcript, all ten pool owners
under every protocol path, full repository CI matrix, other boards,
install-code profile, another compiler version, real interrupts,
hardware/RF and long-duration operation.

## Dead unreachable XDATA, separate experiment

`tools/xdata_dead.py` repeats the complete object relocation audit and
adds all non-control CODE address materializations to the root set.
Roots include main/reset-startup, every banker/runtime service including
the exported `banked_code_read`, every flash-execution function,
address-taken functions and the independently closed runtime paths.
Unknown indirect callbacks/ISR paths are rejected by the existing graph.
Exported library APIs are not automatically runtime entry roots: this is
a closed executable, not an externally callable library.

The stricter exported-runtime roots narrow the earlier estimate to
**28 functions, 5878 CODE bytes, 162 XDATA bytes, 33 summed DATA-frame
bytes, zero BSEG bits**. Physical DATA saving is **zero**: those frames
already share fixed backing reservations.
The earlier 6194/169 estimate also counted `banked_code_read` (316 CODE,
7 XDATA, 6 DATA-frame bytes). It has no ordinary caller in this image,
but retaining the exported runtime entry is the conservative choice.

The complete function list and exact per-function values are in
[`experiments/xdata/dead.json`](../experiments/xdata/dead.json).
It includes `install_code_derive`, `mac_time_attempt_read`,
`mac_command_encode`, `security_keys.seal`, `zdo_node_req_encode`,
unselected public wrappers and out-of-line copies of inlined helpers.
Live relocation sources do not reference their XDATA homes.

**No dead-stripping prototype or measured removal delta is claimed.**
Assembly removal would also need to update CDB entry/end records,
function-area/frame inventories, CODE packing identities and private
fence ownership. That separate proof was not implemented. These 162 bytes
are not mixed into the measured 39-byte lifetime-overlay saving.

## SDCC upstream path

Primary implementation evidence: SDCC 4.2 source `src/SDCCmem.c`,
`canOverlayLocals()`/`doOverlays()` (1252-1320), and `src/SDCCglue.c`,
`emitRegularMap()` (141 onward), XDATA emission (2045), and
`emitOverlay()` (2188 onward). This study reads these sources as evidence;
it does not import their implementation.

| Route | Complexity | Benefit | Upstreamability | ABI impact / risk |
|---|---|---|---|---|
| A. Project-side checked aliases/placement | Low for this bounded subset | Measured 39 bytes now | Mostly project-specific | No call ABI change; lowest bounded risk |
| B. Function-scoped XDATA sections | Moderate compiler-emission work | Enables generic placement, not saving by itself | Plausible opt-in compiler feature | Preserve names/pointer widths; debug/startup accounting needs work |
| C. Extend existing overlay allocator | High | Potentially broader local reuse | Plausible, requires design beyond leaf tests | Same ABI possible; call/escape/parameter correctness risk |
| D. Callgraph-aware linker allocation | High across compiler/linker | Cross-TU reuse without full LTO | Requires emitted lifetime/escape summaries | Preserve ABI; unknown objects must conflict |
| E. New MCS51 allocation pass | Highest | Broader ranges and object classes | Largest maintenance burden | Highest correctness and regression risk |

**Can SDCC emit a separate area per function-local group without changing
the ABI? Yes.** `symbol.localof`, parameter flags and allocation maps already
identify owners. Emission can retain the same public parameter names,
generic-pointer representation, instruction forms and calling convention.
This alone is not an overlay proof. First-argument homes and caller-written
parameter homes need distinct lifetime metadata; reentrant/ISR/address-
escaping objects must remain excluded or conservatively conflicting.
The linker/startup ledger must still account for the physical union.

**Can the existing overlay allocator be extended? Yes, but not by merely
enabling XDATA or removing its call rejection.** The current
`canOverlayLocals()` rejects reentrant/ISR functions, ordinary CALL and
PCALL (apart from builtins), then emits local overlay sets. It is not a
whole-program callgraph allocator. Safe inter-function/cross-TU reuse
needs persistent call/escape/entry metadata, callback/ISR roots, parameter-
setup lifetimes and a link-time closed-world proof or conservative fallback.
No full LTO or register-allocator rewrite is necessary for section emission.

## Recommendation

Next, extend the existing external placement model with **explicit
private-fence/region constraints**, then select a second independently
verified pool to exceed 64 bytes total. Keep caller-written parameters and
dead stripping separate. Retain the measured 39-byte candidate as the
regression baseline and run full CI before proposing production adoption.
