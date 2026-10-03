# Multi-pool XDATA productionization

1. **Productionization status: NOT READY** — hardware commissioning failed; the final CI retry is in progress.
2. **Overlay default: OFF.**
3. **Primary final l_XSEG: 7123 / 7680 bytes**, with 557 bytes free.
4. **XDATA saving: 553 bytes** against application baseline `6ba00392d1a5ebd297fd0de809f2ccaa92571682`.
5. **Clean build: PASS.**
6. **Incremental/rebuild tests: PASS:** clean, no-op, touch, parallel, interrupted/recovery and real stale-artifact tests pass, including the completed compiler-change rebuild.
7. **Full repository CI: PARTIAL.** The first full run passed the overlay release gate and 113/118 existing workers; five workers required the fixes described below. The full retry is running.
8. **Complete MCU successful join: PASS**, in the new productionized CI release gate, not merely the earlier experiment.
9. **Directed pool runtime coverage:** 624 actual linked calls, 16 repetitions, 15 owners across five additional pools, on both baseline and overlay images; plus the existing 18-call, two-entry NWK BTR scenario.
10. **Hardware/RF: FAIL.** Guarded programming, physical readback and boot/admission pass. The real fresh-identity trial associated, but timed out waiting for the network key before READY.
11. **Feature-off artifacts unchanged: YES**, all nine original immutable identities.
12. **Unexplained regressions: 0 confirmed; 1 unresolved hardware failure.** The network-key timeout is not attributed to overlay, baseline behavior or RF loss without comparative evidence.
13. **Ready to become default-on: NO.**

These are current acceptance results, not a completed-release claim. The
compiler algorithm, eligibility and primary application remain frozen.
Original v1, compiler-metadata and multi-pool research artifacts are preserved.

## Build integration

The normal contributor workflow is in [XDATA_OVERLAY.md](XDATA_OVERLAY.md).
The interface follows existing explicit `prepare-join-smoke-*` stages rather
than changing every firmware target through a global flag.

```sh
# Existing, feature-off application resource preparation:
make BOARD=lg_esl29_rev03 JOIN_SMOKE_KEY_MODE=default-tc \
  BUILD=build/ordinary prepare-join-smoke-stack

# Set these to explicitly prepared external or checkout-local tools:
OWNERSHIP_SDCC="$PWD/build/xdata-compiler/sdcc-4.2.0+dfsg/bin/sdcc"
JOIN_S51="$PWD/build/join-simulator/sdcc-4.2.0+dfsg/sim/ucsim/s51.src/s51"

make BOARD=lg_esl29_rev03 JOIN_SMOKE_KEY_MODE=default-tc BUILD=build/overlay \
  SDCC="$OWNERSHIP_SDCC" prepare-join-smoke-overlay
make BOARD=lg_esl29_rev03 JOIN_SMOKE_KEY_MODE=default-tc BUILD=build/overlay \
  SDCC="$OWNERSHIP_SDCC" verify-join-smoke-overlay
make BOARD=lg_esl29_rev03 JOIN_SMOKE_KEY_MODE=default-tc BUILD=build/overlay \
  SDCC="$OWNERSHIP_SDCC" S51="$JOIN_S51" test-join-smoke-overlay
```

`make ... all` continues to build the ordinary non-networking bootstrap.
Only `lg_esl29_rev03/default-tc` has overlay admission. Neither generic nor
install-code is silently substituted or claimed supported. The install-code
inventory remains analysis evidence, not a physically admitted product.

## Artifact pipeline

`tools/xdata_build.py` performs:

```text
source/toolchain fingerprint
  -> compile with --xdata-ownership
  -> preliminary link and existing DATA/stack analysis
  -> immutable input/sidecar admission
  -> frozen same-region allocator
  -> relocation and final link in ordinary XSEG
  -> independent image/lifetime/region/DATA/stack/resource proof
  -> verified native observation header and output receipt
  -> atomic publication
```

Format 4, allocator algorithm 1, platform contract 1 and build receipt 1 are
explicitly versioned. `tools/xdata_build_pins.json` binds the separate
experimental input/final catalogs; production pin catalogs are not replaced.
The source-build helper reproduces the existing four compiler patches without
installing SDCC or changing compiler semantics.

A writer holds a per-output `flock`. It creates an unpublished
`.generations/image-*` directory and publishes only after all checks succeed,
by replacing `join-smoke-layout` with a relative symlink. Generations are never
renamed after linking because NoICE LOAD records bind their path spelling.
No build or CI target touches equipment.

The receipt hashes relevant source/header/tool/test inputs, compiler binary
and version, actual compiler subprocesses, installed headers and runtime
libraries, every preliminary/final artifact and the explicit catalogs.
Sidecar filenames alone are never admission. Input changes during generation
or acceptance reject publication. Cached builds hash both inputs and outputs.
Independent existing-image verification additionally reconstructs the proofs.

This is stale-artifact protection, not a signed supply-chain attestation.
Generated logs and progress files do not constitute acceptance.

## Dependency / stale-artifact behavior

[Clean concurrent reproduction](../experiments/xdata/productionization-clean-parallel.json)
used a clean detached checkout at `0019fac` and an explicitly supplied,
freshly reproduced ownership compiler. Two independent Make processes targeted
the same fresh `BUILD=build/parallel`: one generated and independently verified
the image; the other waited and then reused that exact generation. There was
one generation and all nine immutable image identities matched the experiment.

| Scenario | Evidence / result |
| --- | --- |
| Completely clean build | Real compile/link/proof/publication PASS |
| No-op second build | Real input/output validation; same generation PASS |
| Touch relevant C/header | Real Make builds; unchanged contents reuse the same generation PASS |
| Contract version changed | Real driver rejects before generation and removes publication |
| Allocator version changed | Real driver rejects before generation and removes publication |
| Sidecar deleted | Real generated-artifact hash-set check rejects and removes publication |
| Foreign sidecar | Real complete hash check rejects and removes publication |
| Old sidecar with changed object bytes | Real object/sidecar generation mismatch rejects and removes publication |
| Stock compiler substituted | Real capability check rejects; no feature-off fallback |
| default-tc to install-code | Unsupported profile rejects and removes publication |
| Return to default-tc | Restored valid test fixture is accepted; no install-code layout is reused |
| Actual ownership compiler binary changed | Real 987.418-second rebuild; distinct verified generation bound to the new binary PASS |
| Parallel Make | Two real processes, one verified generation PASS |
| Interrupted generation and recovery | Real first build stopped unpublished; subsequent complete build published PASS |
| Source/header/contract/allocator/toolchain contents changed | Transaction tests require new generation; mid-build changes reject publication |
| Terminated allocator process | Process-level regression requires no public output and successful later recovery |

The real mutation trials use a dedicated non-hardware output. Between
independent rejection cases they restore exact original bytes, recheck the
complete original receipt and source/toolchain hashes, and restore the original
test publication link. That fixture reset is **not** claimed as an automatic
rebuild. Final recovery separately performs a real compiler-change build.
Source/contract mutations are temporary and restored; allocator semantics are
not changed by this project.

The earlier real no-op/C-touch/header-touch checks took 4.074/4.390/4.313 seconds.
The later mutation trials also exercise the public command-line driver rather
than replacing its hash or policy checks with mocks. Synthetic transaction
tests remain explicitly distinct from MCU execution evidence.

[Completed real incremental evidence](../experiments/xdata/productionization-incremental.json)
records all 13 passing scenarios. The final compiler switch produced exactly
one additional generation and its subsequent no-op passed in 2.872 seconds.
Both binaries report the same SDCC version; their different executable hashes
are nevertheless detected and bound to separate receipts.

## CI baseline triage

Reproduction preceded each baseline repair. Detailed commands, return codes,
diagnostics and artifact hashes are retained in:

- [Initial paired triage](../experiments/xdata/productionization-triage.json).
- [Eight additional fixture families](../experiments/xdata/productionization-ci-baseline.json).
- [ZDO failure revealed by broader CI](../experiments/xdata/productionization-zdo-baseline.json).
- [Complete clock relocation comparison](../experiments/xdata/productionization-clock-relocations.json).

| Check | Pristine application baseline | Multi-pool / productionization | Cause and action |
| --- | --- | --- | --- |
| Personal absolute path in compiler report | PASS; report absent | FAIL | Experimental artifact hygiene; remove the personal path |
| Original 1,069,021-byte inventory | PASS; inventory absent | FAIL | Experimental artifact hygiene; one exact path/length/SHA exception, all content checks retained |
| Native recipe inventory | FAIL: 15 versus 13 | Same failure | Baseline added native/sanitized ZCL sensor recipes; preserve 13 original assertions and explicitly cover the two additions |
| Radio-link / dependent radio-TX proof | CODE extent failure | Same failure | Baseline F5 admission added 8 bytes; refresh exact identities and relocated MMIO sites |
| Radio autoack | Raw CDB failure | Same failure | F5 admission; exact CODE/CDB/object/listing and derived mutation inventory refresh |
| MAC radio | Raw CDB failure | Same failure | F5 admission added 13 composed CODE bytes; preserve 54 scenarios and all runtime expectations |
| MAC attempt | Raw CDB failure | Same failure | Same prior source change; preserve 33 scenarios and limits |
| MAC handoff | Raw CDB failure | Same failure | Same prior source change; preserve 76 scenarios and exact runtime inventory |
| MAC adapter | Mutation inventory failure | Same failure | Already pinned 56708-byte image, but old 56671-byte mutation count remained |
| MAC smoke | Raw CDB failure | Same failure | Prior F5/source-line changes; refresh both exact board catalogs |
| CHILD workspace ABI | Raw CDB failure | Same failure | Five-cluster reply grows 19 to 23; removal of `board.h` makes complete board artifact sets coincide |
| Banked join | Complete CODE failure | Same failure | Earlier keepalive retry, descriptor and ZCL-serving relocation changed the fixture and native reference |
| ZDO server | Native line 158, after 1399 checks | Same failure | Production already returned 23-byte/five-cluster descriptor; standalone test still supplied 19 bytes and expected three clusters |

All 20 freshly linked IHX/CDB files across the eight-family paired run were
byte-identical between pristine baseline and productionization. The ZDO
reproduction also produced byte-identical IHX/CDB before its repair.
Production `src/`, `include/`, `boards/` and `examples/` are unchanged from the
application baseline.

**FIXED BASELINE TEST INFRASTRUCTURE:** exact fixture identities, resource
reports and exhaustive mutation inventories were updated for previously
implemented code. All 34 decoded clock LCALL operands, restored by the known
runtime relocation delta, reproduce each old complete 1875-byte clock digest;
no masked hashes or instruction exceptions were added.

The ZDO test now checks the actual five-cluster golden bytes, a 25-byte guarded
output, and all formerly checked tails. Capacity 18 remains rejected and new
capacity 19..22 cases add 16 shared assertions. The real corpus passes 1834 MCU
checks, 943051 native/sanitized checks, 54675 artifact negatives, 21 snapshot
negatives, three peak negatives and the missing-alias control. Limits stay
24576 CODE, 1024 XDATA and SP 7C.

The first repair missed the handoff mutation count. The unchanged exhaustive
campaign measured 87922, not 87888; the follow-up changes only that derived
count, retaining 76 cases and runtime inventory 1164/438789/121/123.

**FIXED MULTIPOOL REGRESSION — test infrastructure only:** adding a separate
CI uploader invalidated the test's old one-uploader assumption. It now
independently requires the original seven board paths and exactly the
synthetic overlay acceptance JSON. Also, after both banked-join board mutation
counts became equal, using the other board's count ceased to be a negative
test; the test now supplies the correct count minus one. No firmware behavior
was changed to satisfy either assertion.

**REMAINING EXTERNAL/BASELINE ISSUE:** none is declared exempt. The complete
retry must actually finish successfully; previous red jobs are not converted
to green by this report.

## CI final results

The full workflow is the repository's canonical acceptance matrix, selected
through `tools/ci_plan.py`, plus the mandatory overlay job. All 118 existing
workers, their 15-minute budgets and 15-second simulator-call deadlines remain.
Only the separate expensive overlay release job has a 360-minute budget.

```sh
gh workflow run ci.yml --repo faronov/cc2530-zigbee --ref xdata-multipool-study
```

[Run 37074346486](https://github.com/faronov/cc2530-zigbee/actions/runs/37074346486),
commit `90c5e87`: overlay release gate PASS, 113/118 ordinary workers PASS,
five ordinary workers FAIL, required final gate FAIL. The five failures were
both handoff workers, both composed workers at ZDO dispatch, and the tool
worker's invalid other-board negative control. All have explicit fixes.
The complete tool command was
`python3 -m unittest discover -s tools -p 'test_*.py' -v`:
951 test methods in 300.538s: 930 passed, 20 skipped and one failed.
One additional class was skipped in `setUpClass`; unittest reports
21 skip events but does not add that class to `testsRun`.

[Run 37103691293](https://github.com/faronov/cc2530-zigbee/actions/runs/37103691293),
commit `028ef04`, is the full retry. It is currently unfinished. Its completed
generic tools worker has already passed the full discovery command:
951 test methods in 301.818s: 931 passed, 20 skipped, zero failures/errors,
plus the one skipped class (`OK (skipped=21)`).
The separate optional PyUSB host tests passed 283 tests in 2.382s.
Both boards' formerly failing handoff and composed-service workers passed.
The 21 discovery skips are 18 macOS-only DYLD interposition tests, one optional
PyUSB class (covered by the separate installed-PyUSB run), and two real Nordic
driver tests requiring an explicit external workspace. They are not silently
counted as executed tests.

Targeted local reproduction after the first repairs passed radio-autoack,
MAC radio, MAC attempt, adapter, smoke, CHILD workspace and banked-join-success;
handoff exposed the separately corrected count. After the follow-up,
`PYTHONPATH=tools:tests python3 -B -m unittest test_mac_handoff test_banked_join -q`
passed 24 tests, and the complete generic ZDO target passed as detailed above.
Full local `make test-local` was deliberately not duplicated before CI.

## Resource result

| Resource | Application production baseline | Frozen multi-pool experiment | Productionized image |
| --- | ---: | ---: | ---: |
| Ordinary XSEG | 7676 | 7123 | 7123 |
| Free ordinary XDATA | 4 | 557 | 557 |
| Physical saving | 0 | 553 | 553 |
| Physical pools | 0 | 36 | 36 |
| Selected homes / functions | - | 434 / 326 | 434 / 326 |
| Selected logical / physical bytes | - | 897 / 344 | 897 / 344 |
| Populated CODE | 253445 | 253445 | 253445 |
| Common CODE | 32714 | 32714 | 32714 |
| Physical DATA backing | 51 | 51 | 51 |
| OSEG | 10 | 10 | 10 |
| BSEG, bits | 88 | 88 | 88 |
| Static stack | 45 | 45 | 45 |
| Maximum bank depth | 8 | 8 | 8 |
| Dead-function stripping contribution | 0 | 0 | 0 |

The early adoption policy requires exact image identities and exact resources,
not merely `l_XSEG < 7680`. A regression toward 7676 is not silently admitted.
Instruction sequences, ABI and memory ceilings are unchanged; differing CODE
bytes are verified relocation operands.

Final virtual-IHX identity:
`df86611e04a22069067a2529c96dff527ec909aab95bb904f0bde04b71eae4e9`.

## Runtime validation

[Productionized release evidence](../experiments/xdata/productionization-release.json)
records a **new** complete CI execution:

| Release phase | Result | Seconds |
| --- | --- | ---: |
| Fresh verified adapter header | PASS | 17.451 |
| Native/sanitized shallow/deep join, ZCL and fresh replay vectors | PASS | 45.799 |
| Repeated baseline directed owners | PASS | 47.616 |
| Repeated overlay directed owners | PASS | 105.686 |
| Existing actual-linked NWK BTR scenario | PASS | 98.734 |
| Complete case2 | PASS | 85.879 |
| Four-step case0 prefix | PASS | 110.905 |
| Complete successful case0 | PASS | 4578.490 |

The last run reached phase 5 READY/serving in 1142 steps and 559709 peripheral
stops, with peak SP 7B below 7C. Those incidental counts were measured, not made
new semantic acceptance conditions. Incomplete, wrong-profile, faulted,
wrong-image and out-of-bound-stack evidence cannot produce `acceptance.json`.
The complete case0 command has no skip flag in the release target.

`tests/boot_xdata_pools.py` adds 624 real linked calls per image over 16 repeated
cycles. Its 15 owners span `mac_tx`, `security_keys`, `mac_adapter`,
`zdo_runtime` and `bdb_join`; the separate BTR scenario covers `nwk_aps`.
Coverage includes near/banked calls, two-byte XDATA and three-byte generic
pointers, 32-bit returns and wrap/half-range comparisons, nested
identity-to-all calls, complete context preservation, MAC initialization and
relocated private adapter bounds. Actual CDB locations reconstruct each pool;
manifest addresses and unexported pool symbols are not accepted as authority.
This is risk-based coverage, not execution coverage of all 326 owners.

## Hardware validation

[Public hardware outcome](../experiments/xdata/productionization-hardware.json)
contains only non-secret observations. Hardware used the clean reproduction
checkout at `0019fac`; its nine final artifact identities are the same frozen
image admitted by the later CI source revisions.

The physical HEX was packed only after independent production overlay
admission in the clean checkout. Its SHA-256 is
`518c8aa4ef6ab47205f2556135c947aedae109251211e306791dae608e6c3f21`;
it contains 253445 populated bytes over a 255759-byte physical extent.
No image byte reaches the reserved NV pages or lock/config page.

The reviewed Linux no-run guard and real installed programmer linkage were
checked before programming. Guarded erase/write/readback returned 86,
meaning cleanup was blocked, **not** proof of success. CPU halt was then
independently confirmed. The hardware runner subsequently reset/halted,
compared every physical CODE byte and FF gap, preserved register/configuration
context, enabled DMA explicitly and observed DISARMED, ARMED and ADMITTED.

An initial laboratory input used higher counter floors. The unchanged
default-TC profile requires floors 1 and correctly rejected it as JS_INPUT
at stage 1, before scan, network provisioning or RF. This is an operator-input
failure, not evidence of overlay corruption.

Reusing floor 1 under an erased device's old IEEE/key history is not safe
counter recovery. The user explicitly authorized a **fresh locally administered
laboratory IEEE**, held only in admission RAM/private evidence; the physical
factory identity and firmware were not changed. A new admission using that
identity and the required floors passed.

The corrected real RF trial **failed before READY**. ZHA logged an unsecured
association with `USE_PRECONFIGURED_KEY`, but no completed device interview
appeared. The terminal firmware state was JS_FAULT/JS_BDB, stage 4,
BDB_JOIN_FAILED/BDB_JOIN_KEY_TIMEOUT, with `saw_ready=0`, member 0 and driver
FINISHED. The independently decoded CDB fields showed association stamp 242682,
key deadline 867682 and final time 869011 symbols: the 10-second key wait
expired by 1329 symbols. One scan candidate and no scan overflow were retained.

The radio diagnostic retained error 0x04 (RXOVERF); the driver recorded two
coverage gaps, 211 receive serials and zero consumer-full drops. These are
observations, **not proof that overflow caused the missing key**, and zero
consumer drops does not imply zero RF loss. Status guards and the expected
terminal stack/register/banker checkpoint passed. No hardware peak-stack
measurement or authenticated membership is claimed.

The CPU stopped at the fault breakpoint. Permit-join was explicitly closed.
The intended 35-minute READY soak, interview, Bind/report configuration,
temperature/humidity/battery visibility, Identify and periodic reports were
not reached. The same key-timeout symptom was observed during earlier
pre-overlay development, but that is not a controlled baseline/overlay RF
comparison and does not exonerate the optimized image.

The retained-NV restart check subsequently **passed its fail-closed contract**.
Without reflashing or erasing NV, a full reset, independent physical readback
and fresh admission with the same laboratory identity were followed by one
bounded continuation. It stopped at JS_FAULT/JS_SECURITY, stage 1:
`security_keys_open()` returned OK rather than EMPTY, with zero driver steps
and no READY. This is deliberate rejection of existing durable state, **not**
successful rejoin. The CPU was left halted at the fault checkpoint
(PC 30986, SP 81, bank 1). The helper did not perform a separate RF-off register
proof; the stage-1 source path returns before adapter initialization.

The manual continuation procedure is:

1. Require the exact admitted physical image, independent readback/halt,
   known NV history and the recorded hardware admission.
2. Finish the full source/toolchain/image proof before opening permit-join.
   The laboratory runner emits a private readiness token and remains halted.
3. Permit only the chosen ZHA coordinator for 254 seconds, then explicitly
   release that token; no Make/CI step can do this.
4. Observe the new node's join, complete five-cluster interview, Bind and
   Configure Reporting, temperature/humidity/battery attributes, actual
   Identify and Identify Query, and repeated reports.
5. Run a defined 35-minute trial, retaining fault breakpoints and terminal
   status. Record READY duration separately from initial commissioning.
6. After any status snapshot, acknowledge the observer-induced halt.
   Distinguish a running demo from a halted terminal checkpoint.
7. A subsequent full-reset trial with retained NV must fail closed, not be
   reported as secure rejoin. Do not erase counters and reuse the identity.

Raw identities, factory data, network traffic, keys, snapshots and programmer
logs remain in user-owned 0700 storage outside Git, using exclusive 0600 files.
The private laboratory runner is not a released automatic flashing interface.
All sensor/battery values are synthetic. Identify/Identify Query are supported;
Trigger Effect and Read Reporting Configuration are not. Bind/report settings
are RAM-only and secure rejoin is still unimplemented.

## Reproducibility

Use a clean checkout and the archive/four-patch preparation commands in
[XDATA_OVERLAY.md](XDATA_OVERLAY.md). The archive is
`sdcc_4.2.0+dfsg.orig.tar.xz`, SHA-256
`ebe7bfb0894380cd92798b57fb9de96e6c0b913a02b6854d0a01cd70328c1578`.
No installation or undeclared worktree source is necessary. Alternatively,
supply an explicit external ownership compiler and repaired simulator.

Two source-built ownership compiler executables have different binary hashes,
despite the same SDCC 4.2.0 #13081 version:
`273315806c1316c6aaf88207ca27db307ddda9f764f9b8d6f44ebbf1688e0a99`
and
`eb674f7f1f75851c51351ff35bd94976688d230d439be6254eb2c7715e962963`.
The clean build reproduces all nine immutable target identities; the receipt
still distinguishes the actual binaries and forces a new generation on change.
Compiler path spelling is not used as a substitute for executable identity.

[Feature-off comparison](../experiments/xdata/productionization-feature-off.json)
records fresh stock-SDCC builds in pristine and productionization checkouts:
all nine identities match each other and the original baseline catalog.
No ownership sidecars or overlay manifests are generated. CI repeats this
comparison and the disabled native/sanitized join/ZCL checks.

## Security/correctness boundaries

The allocator retains the conservative whole-activation, same-region model.
Compiler ownership metadata is a trust root, not a proof generated by an
independent compiler. Final admission still requires actual linked callgraph,
relocation, initialization/nonescape, private-region, exact byte-ledger,
DATA and stack checks.

The accepted image is closed: no unexpected computed calls, recursion,
reentry or ISR enabling. Callers must obey C object bounds and the explicit
CC2530 ownership contract. Ordinary XSEG, the reserved status block, runtime
scratch, private fences and the IRAM alias remain distinct. No allocation is
hidden outside `l_XSEG`.

The build feature does not make the underlying experimental Zigbee application
production-certified, implement secure rejoin, validate arbitrary new profiles,
or recover an erased counter history. The manual identity decision above is
not a replacement for durable production counter management.

## Remaining blockers

The complete CI retry must finish. Real RF commissioning must pass before
interview/reporting/Identify/soak can be assessed; the observed network-key
timeout remains unclassified. Retained-NV reset rejection has passed, but
secure rejoin remains outside the unchanged application's implemented scope.
No unsupported profile or exempt red test is being used to bypass these gates.

## Default-on decision

**NO.** Keep the feature opt-in while those concrete acceptance tasks remain
unfinished. A measured 553-byte saving, a clean image proof and one successful
simulated join are not by themselves permission to enable it globally.
