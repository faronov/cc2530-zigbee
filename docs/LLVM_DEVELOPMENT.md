# LLVM-MCS51 development: branches, test tiers and evidence

LLVM is an **experimental, opt-in** toolchain for this firmware. SDCC is still the production
reference, and nothing here changes production defaults, `ci.yml` or the SDCC tiers in
[CONTRIBUTING](../CONTRIBUTING.md#development-checks). The study record is
[LLVM_MCS51_PORT_STUDY](LLVM_MCS51_PORT_STUDY.md); see §24 for this infrastructure.

## Which command to run

| Situation | Command | Typical time (24 cores) | Runs complete case 0? |
| --- | --- | --- | --- |
| After an ordinary compiler or firmware edit | `make test-llvm-fast` | ≈21 s | No, never |
| Before opening or updating a PR | `make test-llvm-smoke` | ≈3.5–5 min | No, never; a 3-step prefix only |
| Before an integration/release/hardware candidate | `make test-llvm-full` | ≈4.8 h (case 0 ≈4.8 h) | Yes, mandatory |

All targets take these variables:
- `LLVM_TOOLS`: the compiler `bin/` directory.
- `LLVM_SRC`: the llvm-mcs51 checkout, used for runtime sources and verifiers.
- `LLVM_TREE`: optional. The configured build tree, which enables lit.
- `LLVM_LIT_EVIDENCE`: optional. A receipt to reuse instead of rerunning lit.
- `PYTHON`: a Python with `pyelftools`.

The simulator is built once with `make llvm-simulator`. It is pinned to the Debian SDCC 4.2.0
archive (see `tools/llvm/prepare_simulator.py`), and needs `LLVM_SIM_ARCHIVE`.

```sh
make llvm-simulator LLVM_SIM_ARCHIVE=build/sdcc_4.2.0+dfsg.orig.tar.xz
make test-llvm-fast  PYTHON=.venv/bin/python LLVM_TOOLS=../build-rc/bin LLVM_SRC=../llvm-rc LLVM_TREE=../build-rc
make test-llvm-smoke PYTHON=.venv/bin/python LLVM_TOOLS=../build-rc/bin LLVM_SRC=../llvm-rc LLVM_TREE=../build-rc
# FULL: hours. Suspend after a case-0 budget, then resume against the same identity:
make test-llvm-full ... LLVM_TIER_ARGS="--case0-budget 17400"
make test-llvm-full ... LLVM_TIER_ARGS="--resume --case0-budget 17400"
```

The direct CI entry point is the same script:

```sh
python3 -B tools/llvm/tiers.py --tier {fast,smoke,full} --output DIR \
    --tools BIN --llvm-src SRC --simulator S51 \
    [--llvm-build TREE | --lit-evidence RECEIPT] [--evidence STORE] \
    [--case0-budget SEC] [--resume] [--plan-only] [--variant {production,rx-diag,parent-recovery,parent-recovery-policy-c}]
```

`--plan-only` prints the exact selection without running anything.

`--variant` (Make: `LLVM_VARIANT=rx-diag`) selects the firmware image. The default
`production` image is the admitted profile. `rx-diag` adds `-DCC2530_RX_DIAG` and links the
fixed diagnostic block; see
[RX_DIAG diagnostic variant](#rx_diag-diagnostic-variant-llvm-only). The variant is part of
the receipt identity, so evidence never crosses variants.

`parent-recovery` adds `-DCC2530_PARENT_RECOVERY` for the explicitly versioned
known-router recovery extension described in
[SECURITY_KEYS.md](SECURITY_KEYS.md#optional-known-router-durable-ownership).
It does not enable RX_DIAG or change production defaults. Alternate recovery
requires valid durable previously trusted router history; unknown identities
are unsupported. `make test-parent-storage`, `test-parent-recovery` and
`test-parent-candidates` run the real owner/controller/collector corpus
with this flag, both natively and under sanitizers.

`parent-recovery-policy-c` additionally defines `CC2530_REPLAY_POLICY_C`.
It preserves K3 and outgoing reservations, but uses RAM ordinary NWK floors
with selective durable checkpoints and explicit cold-reset replay exposure.
See the [checkpoint/threat contract](M5_POLICY_C.md).
`test-replay-policy-c` and `test-parent-recovery-policy-c` run native and
nonrecovering-sanitizer tests. Only this opt-in FULL variant adds sustained
case21 (600 modeled seconds, 120 protected frames and 12 duplicate attempts).
Strict/production/RX_DIAG selections are unchanged.

Exit codes:

| Status | Exit code |
| --- | --- |
| PASS | 0 |
| FAIL | 1 |
| TIMEOUT | 124 |
| CANCELLED | 130 |
| SUSPENDED | 75 |

`make` reports any of these as rc 2. Every run writes `OUTPUT/receipt.json`, which contains:
- the tier;
- the exact selected and unselected steps, with reasons;
- per-step status and duration;
- the full input identity;
- a self-hash.

FAST and SMOKE delete only their own output directory. FULL never deletes an earlier FULL
output.

## Tier contents and what each step proves

**FAST: 11 steps. Measured 20.4–20.8 s per tier, 23 s wall time through make.**

| Step | Proves | Time |
| --- | --- | --- |
| `tier-unit-tests` | Tier selection, identity, stale-evidence and resume rules (17 unit tests) | 1.2 s |
| `compiler-tree` | The lit build tree is current and was configured from `LLVM_SRC` | 5.0 s |
| `lit` | 187 MCS51 tests in codegen, MC, lld and clang. The one known upstream failure is excluded visibly in `profile.json` | 3 s |
| `semsim-default`, `semsim-profile` | Generic-pointer/ABI semantic simulation: 13 cases under the default ABI and 13 under the image ABI | 4.8 s each |
| `bankmatrix-default`, `bankmatrix-profile` | Cross-bank call/return ABI matrix in the 8-bank FMAP model | 0.6 s each |
| `runtime-verifiers` | The upstream generic-pointer, arith32 and div64 runtime verifiers at O0/O2/Os/Oz | 17 s |
| `firmware-build` | 45 translation units compile, and the static-frame link succeeds. Fails on an assertion, a compile or link error, or a bank overflow | 6 s |
| `firmware-audit` | Machine-code audits: bank thunks (0 incorrect), stack arguments, the static stack proof, the HEX/bank image, fixed-address/SFR placement, and generic-pointer sites | 14.8 s |
| `admission` | Admission rules: 0 incorrect thunks; 0 stack-argument entry violations; static stack proof PASS with demand ≤ 128; startup, runtime, memory and fixed-address/SFR proofs PASS; every generic-pointer site OK; no image bytes in the reserved NV/lock region; exactly 45 objects. The receipt also records measured CODE, common, XDATA, stack, thunk count and ELF/HEX hashes, plus informational deltas against the reference | <0.1 s |

**SMOKE: 17 steps. Measured 279–294 s with the reference simulator and 208 s with the fast
one.** SMOKE is FAST plus the following:

| Step | Scenario | Coverage | Time |
| --- | --- | --- | --- |
| `vectors` | — | Host reference transcripts built by the Makefile's own `host-join-smoke-vectors` recipe | 28 s |
| `sim-case2` | Malformed mailbox packet rejected | Boot, input path | 12 s |
| `sim-case3` | Admission input rejected with zero qualified draws | Boot, RNG admission | 12 s |
| `sim-case4` | Committed security record, then a `JS_SECURITY` stop | Security/key-record path smoke | 73–84 s |
| `sim-case9` | Retained radio fault, 25 steps and 5926 stops | Banking call/return, MAC RX/TX, fault handling | 250–266 s |
| `sim-case0-prefix` | First 3 of 1142 steps | Boot, XMAP banking, MAC RX/TX start | 135–151 s |

The case-0 prefix is a regression check only. Its report says `complete: false`, and it never
claims READY or commissioning. The AES/flash write path is first reached at case-0 step 395 and
in case 10, so it is FULL-only. The receipt lists it under `unselected`.

**FULL: 25 steps.** FULL is the FAST steps plus `vectors`, every admitted scenario (0, 16, 10,
8, 6, 1, 5, 9, 4, 2, 3, 17) run to its expected terminal, and `sim-summary`, which checks every
terminal and that stack ≤ 128.

Cases 16 and 17 are the M5 secure-recovery scenarios. Each MCU replay starts from the
`nv_initial` flash image, and the actual journal is compared after every step.
- **Case 16.** `nv_initial` is byte-identical to the final NV of complete case 0. The device
  resumes it through a secured same-parent NWK rejoin, ED Timeout and Device Announce to READY,
  and keeps `serving`.
- **Case 17.** The parent never answers. After exactly three durable Rejoin Requests, the device
  stops with `fault` (`JS_BDB`, `NO_PARENT`).

Both reach the flash write path (REJOINING is saved before each request), so they are FULL-only,
like case 10. The receipt records their measured times.

The `parent-recovery` variant adds three FULL-only cases, for28 steps total:
**20** initially joins and stores explicit public trusted-router history through
the actual target key/NV C services; **18** cold-boots from its exact final NV,
discovers that eligible router after the old parent disappears, securely rejoins,
negotiates ED Timeout, announces, reaches READY and confirms a report plus
protected application input during ten seconds of foreground serving;
**19** cold-boots from the same NV and terminates boundedly when discovery has
only a wrong-Extended-PAN candidate. The foreground interval is not a calibrated
wall-clock or sustained-service qualification. Every case retains separate
terminal and measured-stack checks. The current profile drives both FULL
selection and its summary; no new case is added to production or RX_DIAG.
Vector generation forwards the build variant and verifies both cold-NV chains.
The optional public20-byte `join_smoke_parent_history` input is IEEE8, short2 LE,
two exclusive NWK floors4 LE and key sequences2, loaded before ARM. It supplies
synthetic independently trusted history, not keys or successful membership.
All-zero is absent. Target C stores it only after initial authenticated admission.

FULL refuses to start in two cases:
- Lit is unavailable. FULL requires `--llvm-build` or `--lit-evidence`.
- The profile drops case 0, or does not expect case 0 to reach `serving`.

Case 7 is not an admitted LLVM scenario. It is the host-only `reference_e2e_main`.

Measured reference-simulator times for the reconciled profile:

| Case | Time (s) |
| --- | --- |
| 0 | 17366 |
| 10 | 3979 |
| 8 | 2939 |
| 6 | 1788 |
| 1 | 889 |
| 5 | 686 |
| 9 | 256 |
| 4 | 79 |
| 2 | 12 |
| 3 | 12 |

Cases run in parallel (`--jobs`), so the wall time is about the length of case 0, ≈4.8 h.

On these figures FAST is about 830× cheaper than FULL, and SMOKE about 60× cheaper.

## Inventory of pre-existing tests (reused, not reinvented)

| Group | Owner / runner | Cost | Tier |
| --- | --- | --- | --- |
| MCS51 lit (`llvm/test/CodeGen/8051`, `MC/MCS51`, `lld/test/ELF` mcs51, `clang/test/MCS51`) | llvm-lit | ≈3 s on a built tree; a cold toolchain build in CI takes ≈89 min | FAST |
| Upstream runtime verifiers | `llvm/lib/Target/MCS51/verify-*-runtime.sh` | 17 s | FAST |
| Generic-pointer and ABI semantic simulation | `tools/llvm/semsim` | 5 s | FAST |
| Bank-call matrix | `tools/llvm/bankmatrix` | 1 s | FAST |
| Thunk, stack-argument, stack, static-frame, fixed-address and gp proofs | `tools/llvm/audit.sh` (`thunk_audit`, `stackarg_entry_audit`, `stack_proof`, `gp_proof`, `hexbanks`) | 15 s | FAST |
| Firmware simulator cases 0–10 (not 7), 16, 17 | `tools/llvm/llvm_join_replay.py` | See the table above | SMOKE subset; FULL all |
| Bounded case-0 prefix | `llvm_join_replay.py --limit 3` | 135–151 s | SMOKE |
| SDCC host/sanitizer/image/overlay gates | `tools/ci_plan.py`, `ci.yml` | SDCC overlay full join ≈1 h 38 min on GitHub | Unchanged, separate |

The pre-tier LLVM gates were scripts in the study workspace. Their behavior was moved into
`tools/llvm` unchanged.

Removed waste:
- FAST and SMOKE no longer run full scenarios.
- Lit no longer rebuilds an unchanged tree.
- FULL resume does not rerun completed steps.
- Evidence reuse skips a run whose inputs are identical.

## Artifact identity and evidence reuse

The receipt `identity` is content-addressed, never timestamp-based. It covers:
- **compiler:** the SHA-256 of the clang, ld.lld, llvm-objcopy, llvm-ar and llvm-objdump
  binaries; the runtime, startup, linker-script and verifier sources; the `LLVM_SRC` commit
  and its dirty diff; and the ABI and static-frame flags;
- **firmware:** a digest of the working-tree content of every tracked file, except `docs/`,
  `*.md`, `.github/`, `LICENSES/` and `.gitignore`. It also covers every file under
  `tools/llvm`, tracked or not: scripts, proof logic, linker inputs and `profile.json`. A new
  source file must be tracked, and wired into `tools/llvm/build.sh`, before it counts;
- **simulator:** the s51 binary hash, plus its `build.json` (archive hash, profile and patched
  files);
- **host:** the Python, pyelftools and C compiler versions, and the machine type.

The `selection_digest` binds the exact commands of the selected steps.

With `--evidence STORE`, a PASS receipt is sealed as `<tier>-<identity>-<receipt>.json`. A later
run reuses it only if the identity, selection and seal all match exactly. The receipt is then
copied to `OUTPUT/receipt.json` and reported as REUSED. Anything else runs the tier again.

Verified on real FAST receipts:

| Change | Result |
| --- | --- |
| Identical inputs | REUSED |
| `docs/VALIDATION.md` edit | REUSED (documentation never triggers a rerun) |
| Tampered stored receipt | Rejected ("does not match its seal"), then re-run |
| `src/` comment edit | New identity, re-executed |
| Source restored | The original receipt is reused again |

Consequences:
- A compiler patch that changes code changes the binaries and invalidates all firmware and
  simulator evidence.
- A change to the simulator or to acceptance logic changes the simulator or firmware identity.
- No PASS is ever printed from a receipt with a different identity.
- Lit evidence (`--lit-evidence`) is accepted only from a PASS receipt bound to the same
  compiler binaries and source.

### FULL suspend and resume

`--case0-budget SEC` retains its CLI spelling but applies one shared deadline
to **every selected FULL MCU case**, including 0/21/22/23/24. The scheduler
submits at most `--jobs` tasks and does not start new work after that deadline.
Never-started tasks remain PENDING; they are not silently dropped or passed.
Active replay checks the deadline after each validated chunk, including inside
a long caller step, rather than waiting for the periodic 4096-event snapshot.

Snapshots retain the complete alias-aware MCU/media state, caller index,
intra-step event offset/adjacency, validated event count, learned adjacency,
security/application observations and measured stack peak. They are written
through a flushed/fsynced temporary file and atomic replacement, with a payload
checksum and a separate whole-file SHA-256 in the suspension report. SIGTERM
requests suspension only at a validated boundary. An exit code of 75 without a
verified checkpoint is FAIL, not SUSPENDED.

`--resume` requires a sealed SUSPENDED receipt, not editable `progress.json`.
It rehashes the output inventory, including PASS evidence, ELF/HEX, vectors,
layouts and snapshots, before carrying any completed work. Snapshot loading
also checks the exact image, simulator, replay/support code, vectors, layout
and tier input identity (compiler/runtime/firmware/tooling). Missing, corrupt,
incompatible or incomplete state is rejected. Previously validated event
counts are checked against the production `events_for()` transformation, not
against raw stimulus counts.

Firmware and qualification tooling have separate checkouts and identities.
`LLVM_FIRMWARE_ROOT` selects the frozen source for build/vector generation;
it does not change flags, runtime, linker rules or observation semantics.
Workflow dispatch accepts `firmware_ref` independently of the tooling revision.
Reusable callers may set `tooling_ref`; its default preserves the previous
firmware-ref-based tooling selection for existing callers.
Historical receipts from the old runner are not compatible continuation state.

Hosted FULL currently uses two workers, a 14400-second execution budget and a
hard 1200-second checkpoint grace. A worker that cannot reach a valid checkpoint
within that grace is TIMEOUT, never successful suspension. Active non-suspendable
subprocesses cannot exceed the segment deadline. The 355-minute job therefore
reserves 95 minutes for checkout/setup, sealing, packing and uploads; network
failure remains an explicit CI failure, not an accepted continuation.

The budget and worker count are deliberately not part of the selection.
Timeouts, suspendability and replay observation settings are part of it.
The workflow permits six segments and preserves PASS work without reruns.
Final acceptance checks every preceding job and artifact upload, the receipt
seal, all selected gates and all complete scenario reports. Policy C requires
31 gates and 19 complete cases. A final SUSPENDED, FAIL, TIMEOUT, cancellation,
missing receipt or upload failure cannot be promoted to FULL PASS.
Segment evidence also retains the actual ELF/HEX and audit JSON/text reports;
final acceptance rehashes the published images against admission and the seal.

Run `tools/llvm/check_case22_resume.py --help` for the genuine reference-s51
bounded checkpoint experiment. It compares complete MCU state/history hashes,
counters and stack for an uninterrupted three-caller-step case22 prefix versus
caller-boundary plus intra-step resume, and measures 1/2/4-worker contention.
This experiment is **not** whole-case security-completion or FULL acceptance.

Measured against frozen firmware `1dd413b` and compiler `1a01afb`, using the
reference simulator with affinity restricted to two CPUs:

| Concurrent case22 prefixes | Batch wall seconds | Validated peripheral stops/second |
| --- | ---: | ---: |
| 1 | 196.48 | 10.70 |
| 2 | 214.63 | 19.59 |
| 4 | 422.60 | 19.90 |

Four workers gained only about 1.6% aggregate throughput over two while almost
doubling batch latency. This supports the conservative two-worker setting;
it does not claim the same throughput on a four-core hosted runner. Empty
simulator startup median was 18.5 ms, the intra-step atomic snapshot took
16.0 ms, and the largest validated chunk in the uninterrupted prefix took
21.43 seconds. Caller-step and subprocess/chunk measurements remain in each
case's `performance/*.jsonl`.

The unchanged normalized workloads for 21/22/23/24 contain respectively
1514316/640015/705133/783078 peripheral stops. Historical four-worker wall
cost was 22.63 ms per stop across the 15 finished scenarios. Applying that
cost gives approximate active-case times of 9.52/4.02/4.43/4.92 hours and
about 20.21 hours overall with two workers, excluding setup/checkpoint/upload
overhead. These are estimates, not completed-case measurements. Six four-hour
segments provide 24 hours of execution; actual segment receipts determine
completion, and exhausting the chain is an explicit failure.

The cancelled run `38594192887` is incomplete: 15/19 MCU scenarios finished,
21/23/24 suspended, and 22 was interrupted inside a caller step. Its runner
allowed only 0/21/23/24 to suspend and waited for case22 before sealing the
segment; no usable continuation receipt/state was uploaded. It must not be
resumed or described as firmware failure or FULL qualification.

### Negative checks

| Injected fault | Result |
| --- | --- |
| `#error` in `src/aes.c` | FAST failed at `firmware-build` in 1.8 s. Dependent steps were cancelled or not run, and the exit status was nonzero |
| Stack capacity changed to 100 | `admission` failed on `stack_demand <= 100` |

## case-0 cost and the optional fast simulator

Complete case 0 has the following metrics, which are not assumed equal to SDCC's:
- 1142 logical steps;
- 559709 peripheral stops;
- stack 101 B, peak SP 228;
- 10 adjacency retries;
- 17366 s.

The SDCC reference has the same 1142 steps and 559709 stops.

Profiling found:
- Most time goes to s51 (≈80% of wall time) and Python event processing (≈20%).
- Actual MCU instruction execution is only 10–15%.
- Each chunk starts a fresh s51, which reinstalls ≈1100 breakpoints (≈0.7 s) and takes two
  full captures (≈1.3 s).
- A 256 KB flash dump costs 0.43 s, because ucsim writes it one byte per `write()`.

`prepare_simulator.py --profile fast` (`LLVM_SIM_PROFILE=fast`) builds the same ucsim with a
buffered memory dump, and nothing else changes.

Differential proof:
- 27 real chunks, with all 255 dumps byte-identical;
- identical results in the SMOKE tier (case 2/3/4/9 and the prefix are identical except for
  path and duration fields).

Measured speedups:

| Run | Reference | Fast |
| --- | --- | --- |
| case 1 | 888 s | 562 s |
| case 9 | 256 s | 183 s |
| SMOKE | 279–294 s | 208 s |

The **reference** simulator stays the FULL acceptance oracle. The fast simulator is a
development option, and its hash is recorded in every receipt that uses it.

### Simulator optimization roadmap (proposal, not implemented)

Case 0 runs 4194 chunks: ≈4.1 s each locally and ≈1.9 s each on a GitHub runner. Almost all of
that is per-chunk fixed cost, not MCU execution. In priority order:

1. **Persistent s51 across chunk boundaries.** This removes process start, ELF load and full
   state restore from every chunk, which is the largest share of the ≈80% s51 time. Equivalence
   must be proven against the per-chunk restore, because the restore is what makes resume exact.
2. **Install breakpoints once.** The ≈1100 conditional breakpoints (≈0.7 s per chunk) are
   identical for the whole case.
3. **Buffered memory dumps.** Already done as `--profile fast`, saving 25–35%.
4. **Batched peripheral events, less Python IPC.** Process the stops between captures in one
   exchange, and parse dumps in bulk (`memory_dump` parsing is ≈8%).
5. **Do not reserialize immutable state.** Flash rarely changes, so captures and the ≈300 KB
   snapshots taken every 10 steps can carry a hash plus deltas instead of the full image.

Any accelerated simulator is admitted only after a differential proof against the reference:
- chunk-by-chunk identical dumps;
- identical per-case steps, stops and stack;
- identical negative cases.

Until that proof exists, the reference stays the release oracle.

## GitHub Actions

| Event | Repository | Tier |
| --- | --- | --- |
| Push to `llvm-dev` | cc2530-zigbee (`llvm-tiers.yml`) | FAST |
| PR into `llvm-dev` | cc2530-zigbee | FAST + SMOKE |
| Push to `mcs51-dev` or `mcs51/feature-**` | faronov/llvm-project (`mcs51-dev-tiers.yml`) | FAST against firmware `llvm-dev` |
| PR into `mcs51-dev` | faronov/llvm-project | FAST + SMOKE |
| `workflow_dispatch` | Either | Any tier, including FULL |

Documentation-only pushes are ignored.

Job layout:
- The `toolchain` job builds clang/lld (Release, assertions, ccache), the reference simulator
  and FAST/SMOKE.
- FULL packs those exact binaries and the FAST lit receipt as inputs.
- The jobs `full-1` through `full-6` chain validated SUSPENDED/`--resume` state
  within individual 355-minute job limits. Early FULL timings below are historical;
  they do not estimate the larger current 19-scenario selection.
- `full-verdict` rechecks the sealed PASS receipt and complete MCU reports,
  and refuses any failed/cancelled segment or artifact upload.

A FULL result is bound to the exact compiler commit and binaries it was built from.

### First FULL qualification

FULL run 37996810558 qualified llvm `mcs51-dev` 1a01afb + firmware `llvm-dev` 3b84bbc on
2026-10-09/10:
- `workflow_dispatch`, 22:00:41Z → 00:15:31Z, 2 h 15 min wall;
- one segment, no resume needed; 22/22 steps PASS;
- receipt `4f5622a1…`, identity `c3d57d04…`, selection `9817490c…`;
- lit imported from the same run's FAST receipt `e4d50459…` (187 passed, one known exclusion).

| Case | Terminal | Steps | Peripheral stops | Stack B | GH s |
| --- | --- | --- | --- | --- | --- |
| 0 | serving | 1142/1142 | 559709 | 101 | 7800 |
| 10 | flash-ram-stop | 396 | 114986 | 91 | 2666 |
| 8 | fault | 406 | 79017 | 92 | 2095 |
| 6 | fault | 405 | 63872 | 73 | 1297 |
| 1 | fault | 256 | 0 | 12 | 669 |
| 5 | fault | 130 | 23086 | 73 | 464 |
| 9 | fault | 25 | 5926 | 73 | 177 |
| 4 | fault | 3 | 387 | 52 | 43 |
| 2 / 3 | fault | 1 | 0 | 12 | 8 |

Every case result equals the earlier local reference run, apart from path and duration.

Admission and proofs:
- 367 thunks, 0 bad; stack demand 106/128;
- generic-pointer sites 18/18;
- fixed-address/memory proof and runtime verifiers PASS.

Image hashes:
- The HEX `a1f47b14…` is byte-identical to the local build.
- The ELF (`b697ccd9…`) differs only in the non-loaded `.comment`, which embeds the compiler
  commit.

### Excluded lit test

`lld/test/ELF/mcs51-address-space-overlap.s` comes from upstream 576349eb3a6d, which added the
test without its `mcs51-address-space-overlap.ld`, so it fails with "cannot find linker script":
- This is a test-fixture omission, not a defect in codegen, ABI, banking or stack.
- A reconstructed script passes every `OVERLAP` check, so the lld behavior itself works.
- There is no upstream issue; it is noted in PR #7.
- The firmware link and the memory proofs cover this property for the admitted image.

### Scheduling

Policy:
- push → FAST;
- PR → FAST + SMOKE;
- significant ABI/backend integration → FULL before acceptance;
- release or hardware candidate → FULL mandatory.

A weekly FULL is optional, and nightly FULL is not needed.

Nightly FULL is **BLOCKED**:
- GitHub runs `schedule` only from a repository's default branch.
- That is production `main` here, and `tc32-as-thumb-extension` in faronov/llvm-project.
- Neither will be changed for this.
- A weekly schedule has the same restriction. A non-intrusive option is a separate scheduler
  repository whose default branch only dispatches `llvm-tiers.yml` with pinned SHAs. That
  needs a token secret and explicit approval, so it is not set up.

Run FULL manually with
`gh workflow run llvm-tiers.yml --ref llvm-dev -f tier=full` (or from the fork's
`mcs51-dev-tiers.yml`). GitHub lists `workflow_dispatch` only for workflows it already knows,
and these workflows exist only on non-default branches. Dispatch therefore depends on a
push-triggered run having registered the workflow first. Verify it before relying on it.

A PR into `llvm-dev` also triggers the unchanged production `ci.yml`, which runs on every pull
request. This is intended, and the LLVM tiers do not replace it.

When a tier is required:
- Changes with broad ABI or codegen consequences need a FULL PASS before integration.
- An integration or release candidate requires FULL on the exact candidate.
- Unchanged sealed evidence may be reused only when every identity component matches.

## Branches and identities

```
upstream devbis/llvm-mcs51:mcs51 80b61955e
  └─ faronov/llvm-project:mcs51-dev  2feda4a48 (+ CI workflow)  ← ongoing development
       └─ mcs51/feature-*                                       ← one change each
frozen hardware compiler a3c9a84a4 (immutable, never rebased)
  └─ accepted firmware 7b0eb693 / LLVM source b27f950 / llvm-study/hw-candidate-1 3fd666d
       └─ cc2530-zigbee:llvm-dev                               ← firmware LLVM development
```

Policies:
- `mcs51-dev` is upstream `mcs51` plus only the still-needed PR #7 static-frame, CSR and
  wide-argument commits.
- Upstream rewrites are absorbed by merge. Shared branches are never force-pushed.
- Upstream PRs carry only generic backend changes, never CC2530 workarounds.
- Compiler commits live only in the LLVM fork, and firmware commits only here.
- The reconciled profile is a **new experimental image identity** (HEX `a1f47b14…`). It is not
  the hardware-tested frozen image (`1b865401…`), and byte identity across backend commits is
  not required.
- The accepted SDCC image (`518c8aa4…`) can still be rebuilt from `7b0eb693`.

## H2 receive-path investigation (offline, complete)

The full report is [`H2_RX_INVESTIGATION.md`](H2_RX_INVESTIGATION.md). Summary:

- **Signature A** (S1, S3 SDCC; LH4 LLVM): a hardware RX FIFO overflow (`RXOVERF`) after
  association, before the network key was used.
  - In the association workspace, the link driver fails closed with `COVERAGE` (fault 9).
  - In the runtime workspace, the loss is tolerated. LH4 then reached `KEY_TIMEOUT`, while L3
    passed despite the same loss.
- **Signature B** (LH2): the network key was received, then the key-request uplink failed
  with `TRANSMIT_FAILED`. There is no overflow evidence.
- The receive driver is **polled; there is no RX ISR**. No source defect is proven, and no
  compiler causality exists.
- Coordinator EZSP counters are global and do not discriminate passes from failures.
- The first cause cannot be determined from the evidence:
  - service latency;
  - parent burst/retries;
  - channel traffic.
  The report lists the missing fields. The flag-gated diagnostic ABI that captures them is now
  implemented for LLVM only (80 bytes, version 1; see
  [RX_DIAG diagnostic variant](#rx_diag-diagnostic-variant-llvm-only)).
- No functional code change is justified. No hardware trial is justified until the diagnostic
  image passes FAST, SMOKE and FULL and is separately authorized. Do not use H1.

Known pre-existing gap: the SDCC raw-CDB digest pins used by the SDCC boot checks of
radio_autoack, mac_radio and mac_adapter predate the `llvm-dev` compatibility commits. Those
checks, and the link-direct/join-smoke host builds that depend on them, fail on "raw CDB
identity changed" on this branch. The LLVM tiers are the gate on `llvm-dev`.

## RX_DIAG diagnostic variant (LLVM only)

`CC2530_RX_DIAG` is an opt-in, versioned, 80-byte XDATA block. It records RX FIFO overflow
first-loss state, service gaps, the last good MAC header and the first upper-layer failure.
The layout, placement, HWLab read procedure and analysis tools are in
[H2_RX_INVESTIGATION §10](H2_RX_INVESTIGATION.md#10-diagnostic-abi-cc2530_rx_diag-implemented-llvm-image-only).

The variant is a **separate image identity, never production**.

| Command | Purpose |
| --- | --- |
| `make test-rx-diag PYTHON=.venv/bin/python [TSHARK=…/tshark]` | Host diagnostic tests, flag-off/on MMIO trace identity, decoder and capture tests |
| `make test-llvm-fast LLVM_VARIANT=rx-diag LLVM_TIER_OUT=…` | FAST on the diagnostic image |
| `make test-llvm-smoke LLVM_VARIANT=rx-diag LLVM_TIER_OUT=…` | SMOKE; each sim records the final block (`rx_diag` hex in `sims/*.json`) |
| `gh workflow run llvm-tiers.yml --ref llvm-dev -f tier=full -f variant=rx-diag` | FULL on the diagnostic image (required before hardware) |
| `python3 tools/rx_diag_sdcc_build.py --variant production --output DIR` | SDCC feature-OFF identity proof |
| `python3 tools/rx_diag_sdcc_build.py --variant rx-diag --baseline DIR --output DIR2` | SDCC fit check; records INFEASIBLE |

Local FAST/SMOKE evidence (compiler source 2feda4a, which is `mcs51-dev` 1a01afb minus its CI-workflow-only commit):

| Image | HEX SHA-256 | CODE | XDATA bss/data | Stack |
| --- | --- | --- | --- | --- |
| production (unchanged) | `a1f47b14…` | 213132 | 4841 / 35 | 106/128 |
| rx-diag | `59d7bfc1…` | 214612 (+1480) | 4841 / 35, plus the fixed block at 0x1E40 | 106/128 |

Both FAST runs pass. SMOKE `rx-diag` passes 17/17 in 275 s. Its decoded blocks are
self-consistent:
- case 0 prefix: 0 frames;
- case 9: 1 beacon, `max_service_gap` 116 ticks (≈3.5 ms), `max_rx_off_gap` 83 ticks;
- no overflow in either.

**FULL `rx-diag`: PASS.** Run 38062355155 qualified firmware `llvm-dev` 6463309 with llvm
`mcs51-dev` 1a01afb.
- 2026-10-10 15:07:45Z → 17:21:08Z (2 h 13 min). One segment, 22/22 steps PASS.
- Receipt `7852560e…`.
- HEX `59d7bfc1…`, equal to the local build. ELF `ba45110a…`; only `.comment` differs.
- Every case equals the production FULL:
  - case 0: serving, 1142/1142 steps, 559709 peripheral stops, 101 B stack;
  - the other cases match too.
- 370 thunks, 0 incorrect. Stack demand 106/128. Generic-pointer sites 18/18.

Decoded case 0 block (simulator timing, not hardware):
- 28 frames, last a data frame with PHR 56;
- 0 overflows, salvaged, flushes, lost and bad CRC;
- `max_rx_off_gap` 921 ticks;
- no first-loss or failure snapshot;
- every consistency check is OK.

For SDCC, the diagnostic code does not fit the banked join-smoke layout (INFEASIBLE).
- Bank groups grow by +1532 B. Of that, `JS_RADIO` grows by +914 B, but its bank has only 34 B
  of headroom.
- The SDCC feature-OFF proof reproduces `518c8aa4…` with all resource deltas 0.

SDCC trials therefore run the production image with the sniffer only. No SDCC diagnostic image
exists.
