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
    [--case0-budget SEC] [--resume] [--plan-only]
```

`--plan-only` prints the exact selection without running anything.

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

**FULL: 23 steps.** FULL is the FAST steps plus `vectors`, every admitted scenario (0, 10, 8, 6,
1, 5, 9, 4, 2, 3) run to its expected terminal, and `sim-summary`, which checks every terminal
and that stack ≤ 128.

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
| Firmware simulator cases 0–10 (not 7) | `tools/llvm/llvm_join_replay.py` | See the table above | SMOKE subset; FULL all |
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

`--case0-budget SEC` lets case 0 snapshot every 10 steps and stop with SUSPENDED after the
budget. `--resume` continues the run, but only if all of these are unchanged:
- the same identity and selection;
- the same ELF, vectors and simulator as at admission;
- the same snapshot chain.

Completed steps carry over.

Each change below was refused:
- a flipped ELF byte;
- a modified vector file;
- a source change (another identity);
- another simulator binary;
- a fresh run into a non-empty output.

The case-0 budget and `--jobs` are deliberately not part of the selection.

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
- The jobs `full-1`…`full-4` run FULL segments chained by SUSPENDED/`--resume` within the 6 h
  job limit. On a GitHub runner the whole FULL fits in `full-1` (≈2.2 h); later segments are a
  safety margin.
- `full-verdict` passes only if the last segment's receipt is PASS.

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
  The missing fields, and a ≤48-byte flag-gated diagnostic ABI to capture them, are specified
  in the report.
- No functional code change is justified. No hardware trial is justified until the diagnostic
  image passes FAST, SMOKE and FULL and is separately authorized. Do not use H1.

Known pre-existing gap: the SDCC raw-CDB digest pins used by the SDCC boot checks of
radio_autoack, mac_radio and mac_adapter predate the `llvm-dev` compatibility commits. Those
checks, and the link-direct/join-smoke host builds that depend on them, fail on "raw CDB
identity changed" on this branch. The LLVM tiers are the gate on `llvm-dev`.
