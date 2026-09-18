# Contributing

This is an experimental bootstrap project. Read the
[plan](docs/PLAN.md), [architecture](docs/ARCHITECTURE.md) and
[source policy](docs/PROVENANCE.md) before adding a subsystem.

## A useful change

- Addresses a bounded milestone requirement.
- States what is implemented and what remains unsupported.
- Includes the smallest relevant host/image/simulator tests.
- Records hardware evidence separately when hardware behavior changes.
- Preserves per-file licenses and contains no private data.

Do not add a success-returning radio, security, persistence or join stub to
make an example look functional. Add an explicit unsupported boundary only
when it has a real caller and a tested failure contract.

## Specialist Copilot agents

Two repository-local [Copilot custom agent profiles](https://docs.github.com/en/copilot/reference/custom-agents-configuration)
are provided; no global installation or fixed model is required.

| Agent | Intended task |
| --- | --- |
| [zigbee-stack](.github/agents/zigbee-stack.agent.md) | Protocol codecs/state machines, specification lookup, end-device conformance and security/commissioning boundaries |
| [cc2530-platform](.github/agents/cc2530-platform.agent.md) | CC2530/SDCC, memory and linker ABI, debugger transport and isolated chip services |

In Copilot CLI opened at this repository, use `/agent` to select a profile,
or name it in a prompt, for example: "Use zigbee-stack to implement the
assigned offline MAC codec extension." A new CLI invocation can select it
with `copilot --agent=cc2530-platform`.

Give a delegated task a bounded objective, file ownership and required
evidence. Coordinate shared interfaces in the main task; do not run two
writers over the same files. The profiles allow read/search/edit/execute/web
tools, omit nested-agent tools, and inherit the configured model. Ordinary CLI
permissions still apply: profile instructions are not an OS sandbox, and
shell access does not constitute authorization to operate hardware.

The Zigbee profile consults the external
[reviewed reference index](docs/PROVENANCE.md#specialist-agent-reference)
on demand. It does not install that repository's agent/skill or copy its
catalog here. Reference retrieval needs network access, but firmware builds
and offline tests do not depend on that repository. Neither profile adds
working firmware features or changes the project's support claims.

## Development checks

With SDCC 4.2.0, `s51`, Python 3.9 or newer, GNU Make and a host C compiler on `PATH`:

```sh
make test-local
python3 tools/check_repository.py
git diff --check
```

`test-local` runs the complete Python tool suite once, all standalone
host/image/simulator component corpora once **for each board definition**,
and the board-specific host/image/simulator checks for all twenty board/image
combinations. Components have no `IMAGE`-dependent inputs. This removes
duplicate runs, not cases: every component still runs with both
`CC2530_BOARD` definitions, and every board fixture retains its own checks.
Separate directories under `build/local/` prevent cross-image relocated
listing collisions. Submakes use `-j1`, stop at the first failure, and retain
the 15-second per-simulator deadline; extra CPU cores do not justify
concurrent links into a shared directory or relaxed timeouts.

The existing `make BOARD=... IMAGE=... all test` command remains a full
single-configuration check and is unchanged in the twenty-job CI matrix.
For focused iteration, the explicit parts are:

```sh
make test-tools
make BOARD=generic test-common
make BOARD=generic IMAGE=radio_rx_fixture test-board
make BOARD=lg_esl29_rev03 IMAGE=radio_rx_fixture test-board
```

`test-common` covers standalone components, not a board fixture;
`test-board` builds and checks the selected board image, not the standalone
components or Python suite. Neither is a replacement for `test-local` when
the full local matrix is required. Run focused targets serially; do not
run simultaneous Make invocations using the same `BUILD`.
See [performance evidence](docs/VALIDATION.md#local-validation-performance)
for measured improvements and preserved coverage.

The build writes to `build/`. Generated firmware, captures and logs must not
be committed. Do not add a dependency solely to avoid a small standard-library
check; new dependencies need a purpose and license review.

The M1 transport tests use synthetic USB backends and must never enumerate
hardware. Ordinary tests need no PyUSB; optional PyUSB resource-manager tests
are explicitly skipped when it is absent. To include those tests without any
USB device access, use a virtual environment:

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements-debug.txt
.venv/bin/python -m unittest discover -s tools -p 'test_m1_*.py' -v
```

Focused offline access/CLI and erase-boundary observer coverage:

```sh
PYTHONPATH=tools python3 -B -m unittest test_m1_access test_erase_boundary_fault -q
```

`test_m1_access` uses an original small 8051/backend model. On macOS,
`test_erase_boundary_fault` compiles an original synthetic USB shared library,
driver and the DYLD interposer in a temporary directory with strict compiler
flags. It never links real libusb or runs `cc-tool`; it clearly skips on other
platforms or without a host compiler. The full discovery command includes it.

The original Linux external-programmer no-run guard has a separate focused
offline test:

```sh
PYTHONPATH=tools python3 -B -m unittest test_cc_tool_no_run_guard -q
```

Its six strict synthetic tests compile the guard and an original fake USB
library/driver in a temporary directory. They exercise blocked reset shapes,
exact forwarding/errors, an unguarded negative control, missing backend and
output-flush failure; they never invoke the installed `cc-tool` or open real
USB. Normal test discovery already includes them, with explicit platform/
compiler skips where unsupported. No Makefile/CI hardware integration is
needed. The parent's separate installed-ELF binding check used `--help` only
and supplies no target or RF evidence. See the
[exact limits](docs/DEBUGGING.md#linux-external-programmer-no-run-guard) and
[source identities](docs/PROVENANCE.md#linux-external-programmer-no-run-guard-sources).

On Ubuntu, creating this environment may require `python3-venv`. Actual
device access is a separate manual activity described in
[DEBUGGING.md](docs/DEBUGGING.md#implemented-host-transport).

During offline M1 work, do not run even USB enumeration or adapter-state
commands against physical devices. `tools/debug_image.py` and all tests above
operate without an adapter. New control commands need explicit permissions,
pre/post-state checks, one bounded operation deadline, and tests proving that
an error cannot trigger a retry or an implicit resume/reset.
PC/register/memory access and breakpoint programming are implemented, but their
presence is not permission to operate hardware during development checks.
Memory reads, writes and breakpoints need their own permissions; writes are
limited to nonaliased ordinary SRAM below `0x1E00`. Preserve and verify the
full register context, including DPS/DPTR1, on successful inspection. Failed or
late exchanges must stop immediately, without attempting register restoration.
`reset-halt` and `attach-reset` are hardware commands despite their host-tested
implementations: never invoke them in these checks. Reset permission is
independent of ordinary CPU-control permission. Initial attach needs its
distinct explicit access policy and must not be substituted for ordinary
open or an existing-session read. Offline source-location tests must not open
paths named by CDB data.

The same `make ... all test` commands also run the standalone MAC codec on
host and in `mac_frame_test.ihx` under alias-aware simulation. That executable
is test-only: do not flash it, add it to firmware support claims or upload it
as a board image. Protocol codecs must reject unsupported security/layouts
explicitly and must not equate syntactic decoding with authenticated input.

The isolated [reserved flash reader](docs/ARCHITECTURE.md#reserved-flash-read-foundation)
has a focused offline target:

```sh
make BUILD=build/flash-dev test-flash
```

It runs the new host corpus and exact linked-image/alias-aware simulator
checks, without repeating unrelated suites. `test-common` includes it for
both board definitions. `flash_test.ihx` is never flashed or uploaded; no
erase/program operation, flash writer, persistence record or board `IMAGE`
is introduced. Keep the 512-byte reservation budget and the entire private
prefix protected from caller output buffers.

The separate [internal RAM command executor](docs/ARCHITECTURE.md#internal-ram-flash-command-executor)
has `make BUILD=build/flash-exec-dev test-flash-exec`, also in `test-common`.
It executes real copied instructions with synthetic controller/XMAP events
and retains a RAM-only fail-stop when active-controller polling exhausts.
Controller-idle is not verified NV success; public history/readback policy
and hardware acceptance are separate tasks. Preserve the exact linked
extent/return ABI, copy-readback and alias/private-prefix proofs.
**Never flash `flash_exec_test.ihx` or upload it as a board artifact.**
No ordinary build/test target may invoke this engine on a physical device.

The independent passive RX foundation has `make test-radio-rx`, also included
in `make ... all test`. Its [contract and synthetic evidence](docs/RADIO_RX.md)
do not grant hardware access. `radio_rx_test.ihx` is never a board image,
flash input or CI upload. Keep the complete linked-code/MMIO proof, original
512-byte reservation, alias/upper-IRAM guards and 15-second simulator timeout.

For the separate RX board integration, use focused checks rather than twenty
redundant local standalone corpora:

```sh
make BOARD=generic IMAGE=radio_rx_fixture test-radio-rx-fixture
make BOARD=lg_esl29_rev03 IMAGE=radio_rx_fixture test-radio-rx-fixture
PYTHONPATH=tools python3 -B -m unittest test_radio_rx_fixture test_m0_artifacts test_timebase_fixture test_m1_access -q
```

Run heavy s51 jobs **serially**, preserving15 seconds per process and exact
CPU/RAM continuations for the full poll-cap test. Rebuild/image-check the18
older board BINs and compare their complete sizes/hashes to the baseline;
the parent performs broader final regressions/publication separately.
The fixture is **RF-capable passive RX**, not another non-RF image. Never
invoke its [manual runner](docs/DEBUGGING.md#parent-only-passive-rx-acceptance)
from tests/CI. It requires a separately authorized board/recovery task and a
new mode0600 capture in a user-owned0700 directory outside the repository.
No raw bodies, addresses, identities or frame hashes belong in stdout/CI.
CI has20 jobs and exactly the existing seven upload paths, with
`hardware_tested=false`; the new1024-byte reservation budget applies only to
this board fixture, not the component or older512-budget tests.
Shared host-MMIO write hooks must consume, not bypass or enlarge, the bounded
logs. Physical RX needs a separately checked board fixture and explicit
manual acceptance.

The separate R22 NWK codecs have focused offline targets:

```sh
make test-nwk-beacon
make test-nwk-frame
```

Both are included in `make ... all test`. `nwk_beacon_test.ihx` and
`nwk_frame_test.ihx` are strictly checked simulator-only component images,
not board images or CI uploads. The MAC test image also exercises the real
MAC-to-Beacon and MAC-to-Data slices, including maximum-body capacity and
layer-specific failures. Keep the codecs independent; decoded metadata is
not successful network/parent acceptance or authentication.

The independent R22 APS Data codec and three-layer composition have focused
offline targets:

```sh
make test-aps-frame
make test-protocol-frame
```

Both are included in `make ... all test`. The standalone APS image retains
the 512-byte XDATA reservation check; the separate MAC/NWK/APS composition
image has a 1,024-byte harness budget with the same strict layout, alias and
upper-IRAM guards. Neither changes the existing MAC test image or adds an
`IMAGE` option/CI upload. See [APS scope and evidence](docs/APS.md).
Normal-unicast syntax and ACK-request metadata are not an APS transaction,
ZDO/ZCL support, authentication or permission to send.

The ZCL Revision 8 wire codecs have focused offline targets:

```sh
make test-zcl-frame
make test-zcl-value
```

Both are included in `make ... all test`, with the unchanged 512-byte
component reservation and alias/upper-IRAM guards. The frame image also
executes real APS/ZCL composition. The value image uses four independently
initialized phases plus an invalid-selector rejection, retaining every
scenario and the shared 15-second per-run timeout. The
[test-only phase/result ABI](docs/ZCL.md#offline-evidence) never grants
hardware access. Neither new executable is a board `IMAGE` or CI artifact.
The host protocol target additionally checks full MAC/NWK/APS/ZCL/value
composition; its SDCC image remains the prior three-layer chain.
Base-text wire evidence is not errata-aware conformance or validated
numeric/text application data.

`make test-zcl-attributes`, also part of `make ... all test`, exercises the
separate generic read-only model and unicast Read Attributes handler.
Its isolated image uses an explicit 1,024-byte harness reservation, retaining
the same source/layout, alias, upper-IRAM, unwind and 15-second guards.
No existing component budget is increased. The host protocol target also
checks a complete MAC/NWK/APS read request/response; its target chain is
unchanged. See [scope and evidence](docs/ZCL.md#read-only-attributes-and-read-attributes):
no board `IMAGE`, artifact upload, registered cluster, network dispatcher, writes or
reporting is added. Unreviewed ZCL errata remains a documented conformance
risk rather than a stop on base-text foundation development.

`make test-zcl-dispatch`, also part of the default test suite, exercises
one-cluster unicast Read/Discover dispatch, unsupported-command errors,
Default Response reception and the no-response exceptions. Its isolated
1,024-byte test harness keeps the same strict layout/alias/upper-IRAM/unwind
and 15-second guards. Host tests cover exact allocations and full-chain
Discover-then-Read using the returned ID. Neither the new test image nor
its outputs become board firmware or CI artifacts. See the
[dispatch contract](docs/ZCL.md#discover-attributes-and-unicast-dispatch).

`make test-protocol-budget`, included in the default test suite, additionally
links **all seven** MAC/NWK Data/APS/ZCL/Read/Discover modules into one compact
SDCC harness. It executes complete Discover-then-Read exchanges and bounded
errors, then writes `build/<board>/protocol-resources.json` (or the chosen
`BUILD`). The ledger checks per-module/linked CODE, XDATA and IRAM budgets,
artifact hashes and actual simulator peak SP; see the
[measured budget and exclusions](docs/ARCHITECTURE.md#integrated-protocol-resource-budget).
Only this new harness uses a 2,048-byte XDATA reservation; existing budgets,
alias/upper-IRAM guards and the 15-second timeout stay unchanged. The checker
removes its old report before validation and writes a new one only on success.
Always require a successful target run and matching artifact hashes before
using a report. Neither this image nor the ledger is a board `IMAGE` or CI
upload. Observed foreground stack headroom is not a worst-case/ISR budget.

The same commands now also run the standalone awake-only timebase. A focused,
entirely offline check is:

```sh
make test-timebase
```

This runs strict host tests, genuine linked-code/layout rejection checks and
alias-aware s51 execution of `timebase_test.ihx`. The host models Sleep Timer
latching/ticking; s51 receives synthetic prelatched SFR values and executes
the actual read/return and deadline instructions. Neither measures a physical
counter or tick rate. Like the codec executable, this is **test-only: never
flash it or upload it as a board artifact**. It is not an `IMAGE` option.
The separate `IMAGE=timebase_fixture` board fixture now links `src/timebase.c`;
the existing `bringup` and `debug_fixture` images still must not link it.
Keep foreground read ownership, explicit half-range ambiguity and unchanged
outputs on errors covered by the [timebase checks](docs/VALIDATION.md#m2-awake-only-timebase-automated-coverage).

The board timebase fixture adds a real-reader host executable and a separate
failure-injection executable for defensive helper-error paths. Neither expands
the production MMIO boundary. Its host read hook explicitly validates and
consumes each three-read log before the next sample, retaining the existing
32-entry overflow assertions rather than silently discarding unverified reads.
Focused offline Python coverage is:

```sh
PYTHONPATH=tools python3 -B -m unittest test_timebase_fixture test_timebase_hardware -v
```

`tools/check_timebase_hardware.py` is a **manual-only** runner, never a build,
test or CI action. Its separate authorization grants reset/CPU/breakpoint and
read permissions, not host RAM writes or flashing. Follow the
[fixture ABI and manual procedure](docs/DEBUGGING.md#awake-only-timebase-board-fixture).
Do not describe its mock tests or the synthetic SFR simulator as C-driver
hardware acceptance; the independent M1 register observation remains separate.
The [2026-09-16 LG compiled-C record](docs/DEBUGGING.md#2026-09-16-lg-compiled-c-timebase-acceptance)
now establishes a successful 257-cycle physical run on the verified new
image, not on generic hardware. Stopped/backward/ambiguous paths remain
host/simulator-tested, with calibration and the other M2 gates still open.
This past authorization is not permission to repeat hardware operations.

The isolated init-time HF selector is also included in every `make ... all test`.
Its focused offline command is:

```sh
make test-clock
```

This runs strict real-MMIO host models, separate host-only helper-error doubles,
linked instruction/diagnostic/layout rejection checks and alias-aware s51
execution. `clock_test.ihx` is **test-only: never flash or upload it**. It is
not a board `IMAGE`; the driver remains excluded from the six older board builds.
The subsequent, separate `IMAGE=clock_fixture` now links it intentionally.
CI's explicit board-artifact whitelist remains unchanged. The clock script
validates/consumes every host read-log entry and retains the 32-entry capacity
and overflow assertions; production code has no test callbacks.
The simulator supplies synthetic STA/timer values at checked linked access
sites, not physical oscillator startup or calibration. Preserve the existing
standalone timebase's exact reader contract when sharing layout checks.
See the [clock contract](docs/ARCHITECTURE.md#init-time-system-clock-selector-isolated-m2-slice)
and [hardware gates](docs/VALIDATION.md#m2-init-time-system-clock-automated-coverage);
the historical LG timebase record grants no clock-switch authorization.

The clock board fixture adds real-reader/clock host models, explicit diagnostic
serialization, strict linked timeout-checkpoint proof and synthetic clock/fault
simulation. The image checker uses its own IHX/CDB, not `clock.rst` that a later
standalone link may overwrite. Focused runner/decoder tests are entirely offline:

```sh
PYTHONPATH=tools .venv/bin/python -B -m unittest test_clock_fixture test_clock_hardware -v
```

`tools/check_clock_hardware.py` is manual-only, never run by Make or CI.
Both normal and `--induce-timeout` modes require separate hardware authorization;
read the [clock fixture procedure](docs/DEBUGGING.md#init-time-clock-board-fixture).
The latter verifies a real linked deadline RET and caller, pauses the CPU before
the real request, and still requires actual TIMEOUT plus confirmed rollback.
`CLOCK_ROLLBACK_UNCONFIRMED=9` explicitly fails hardware acceptance; old STA
matches cannot prove a pending request was drained. The separate
`--induce-late-timeout` mode holds after the checked request write, then verifies
C-observed source evidence before the timed poll and requires confirmed return.
Both modes need a fresh explicitly authorized run. The original LG
pending-cancellation test exposed a real driver bug on September 16.
The corrected image passed separate
[2026-09-17 (UTC+03) compiled-C acceptance](docs/DEBUGGING.md#2026-09-17-lg-compiled-c-clock-acceptance):
both negative modes retained TIMEOUT/terminal FAULT with confirmed rollback,
then an explicitly reset normal run completed 257 full sequences / 771 C calls.
This finite LG record does not confirm never-departed cancellation, generic
hardware, frequency/calibration, physical oscillator/stopped-clock or power-cut
faults, or other M2 services. A host sleep, synthetic SFR input or green runner
test is not physical clock evidence; past acceptance grants no permission to
repeat hardware work.
The clock slice had eight board/image jobs; the IRQ board slice below extends
this to ten while still uploading only selected board artifacts.

The isolated interrupt-ownership foundation is included in `make ... all test`:

```sh
make PYTHON=.venv/bin/python test-irq
```

This checks all host byte-token representations, exact SDCC EA instructions
and register ABI, and genuine interrupt entry/preemption/RETI using s51's
**generic C52 CPU model**, not CC2530 peripheral delivery. Preserve the
instruction-boundary, nested-context, alias/stack and fail-closed layout
checks. The shared layout checker permits only the IRQ executable's exact
twelve compiler-reserved vector-padding holes; existing timebase/clock
executables still require contiguous emitted CODE and their original guards.
`irq_test.ihx` is **test-only: never flash or upload it**. It is not an `IMAGE`
option. Keep IRQ objects out of the eight older board links and retain the exact
board-artifact whitelist. Hardware interrupt work needs a separate
board/source/vector/recovery task; the existing LG clock/timebase acceptance
is not authorization or evidence for it. See the
[API ownership contract](docs/ARCHITECTURE.md#interrupt-ownership-foundation-isolated-m2-slice).

The separate `IMAGE=irq_fixture` links those unchanged primitives and one
fixture-owned CC2530 Timer1 ISR. Its `all test` includes the strict host model,
real linked vector/context/ABI proof, six synthetic fault cases and 257
synthetic-entry cycles. The host-only `IRQ_FIXTURE_HOST_TEST` policy permits
only the three owned timer-write registers with checked IRQ state; default
host guards and all 32-entry log assertions are unchanged. Every new log
entry is verified before consumption. There are no firmware test callbacks.

```sh
PYTHONPATH=tools .venv/bin/python -B -m unittest test_irq_fixture -q
```

The board simulator explicitly models missing CC2530 entry/H0/RW0 behavior;
it does not relabel C52 Timer1 as CC2530. Keep the separate `test-irq` native
C52 interrupt regression. `tools/check_irq_hardware.py` is **manual-only**,
requires explicit reset/CPU/read/breakpoint authorization and verifies every
physical CODE byte before runner resume. It has no RAM/code/SFR writer.
Follow the [IRQ manual procedure](docs/DEBUGGING.md#timer1-irq-board-fixture);
the unchanged LG image passed
[bounded compiled-C hardware acceptance on 2026-09-17 (UTC+03)](docs/DEBUGGING.md#2026-09-17-lg-compiled-c-irq-acceptance):
three normal cycles, a separate pre-start TIMEOUT/FAULT, then 257 cycles after
an explicit reset. Hardware interrupted restore at +12 with DPL=OK (0), not
the synthetic +9/live-token-1 case; actual ISR/RETI CPU/active-IRAM preservation
passed. That IRQ run ended at READY `01BB`, EA/T1IE off, Timer1 stopped.
Generic remains host/image/simulator-only. Higher-priority hardware nesting,
calibrated timing and other M2 services remain outside that acceptance.
The parent also independently passed both full IRQ configurations with 355
Python tests and strict checks. Past acceptance grants no new hardware
authorization, and M2 #4 remains open. Never flash `irq_test.ihx`.

The isolated quiescent radio FIFO has a focused offline target:

```sh
make test-radio-fifo
```

It is also included in `make test`, without linking radio code into any of the
ten earlier board images. Keep `radio_fifo_test.ihx` test-only:
**never flash or upload it as board firmware**. Its XREG hook is host-only,
requires an explicit model and retains checked 32-entry logs. The linked test
executes real RFST/RFD and CODE/XDATA pointer paths with synthetic FIFO/CSP
effects, not physical radio acceptance. Preserve the
[ownership/error contract](docs/ARCHITECTURE.md#quiescent-radio-fifo-foundation)
and [separate hardware gate](docs/VALIDATION.md#m2-quiescent-radio-fifo-automated-coverage);
no automatic USB, RF operation or error-latch recovery belongs in this target.

The separate `IMAGE=radio_fifo_fixture` adds real-driver host orchestration and
alias-aware synthetic board execution, including all accepted TX RAM reads.
Only these two images link the FIFO driver, with the same seven-file artifact
whitelist. Focused decoder/runner tests:

```sh
PYTHONPATH=tools .venv/bin/python -B -m unittest test_radio_fifo_fixture -q
```

`tools/check_radio_fifo_hardware.py` is **manual-only**. Follow its
[programming, checkpoint and acceptance procedure](docs/DEBUGGING.md#quiescent-radio-fifo-board-fixture);
normal, induced timeout and reset/recovery runs need separate explicit
authorization. The [dated LG FIFO record](docs/DEBUGGING.md#2026-09-17-lg-compiled-c-fifo-acceptance)
now covers bounded normal, written-but-unverified timeout and separately reset
257-cycle recovery. Generic, RX flush, received frames and on-air operation
remain unvalidated. Keep that manual evidence separate from automated checks;
past acceptance grants no permission to repeat hardware operations.

The independent channel-0 RAM-copy DMA has a focused offline target:

```sh
make test-dma
```

`make test` includes this standalone executable separately from board artifacts.
**Never flash or publish `dma_test.ihx` as board firmware.** Its
checked host model and alias-aware linked execution are synthetic, not DMA
silicon or AES evidence. Preserve the pinned nine-system-clock arm path,
private/helper allocation guards and
[terminal failure/buffer-lifetime contract](docs/ARCHITECTURE.md#isolated-channel-0-dma-copy).
Debug DMA_PAUSE must be cleared by a separate authorized hardware workflow
before any physical DMA-register access. The
[API-only gate and dated LG evidence](docs/DEBUGGING.md#guarded-dma-enable-after-reset)
cover only the reset-scoped `26 -> 22` transition, not DMA transfers or AES.
The separate `IMAGE=dma_fixture` now prepares both boards offline, preserving
all twelve older BINs and expanding CI to fourteen jobs with the same exact
seven-file whitelist. Its real-driver host and 42 linked synthetic scenarios
cover 257 cycles, both clocks/routes, every buffer/guard byte, terminal lifetime
and the genuine negative RET checkpoint. Run heavy s51 checks serially; do
not relax the existing 15-second bound.

```sh
PYTHONPATH=tools .venv/bin/python -B -m unittest test_dma_fixture -q
```

The [manual DMA procedure](docs/DEBUGGING.md#parent-only-dma-acceptance-procedure)
requires its own reset, full physical CODE verification and explicit gate22
before resume/access. Older runners still require26; no automatic hardware
test or recovery is added. The [dated LG DMA record](docs/DEBUGGING.md#2026-09-17-lg-compiled-c-dma-acceptance)
now covers normal copies, an accepted-but-unverified timeout and separately
reset 257-cycle recovery. Generic, other DMA channels/triggers and stuck-DMA
recovery remain outside that RAM-copy acceptance; AES evidence is separate
below. That DMA run ended on READY016A/config22.
Focused **synthetic-only** gate/lifecycle checks:

```sh
PYTHONPATH=tools .venv/bin/python -B -m unittest test_m1_dma_config test_m1_lifecycle test_m1_transport -q
```

These tests exercise the real API only through synthetic backends; they never
enumerate or access physical USB devices. The recorded late-host-return
failure is not a physical USB stall or DMA-stuck test, and past acceptance
grants no new hardware authorization.

The isolated AES-triggered DMA block foundation is included in `make test`:

```sh
make test-aes
```

It runs five public primary KATs against an independent host-only mathematical
reference, the real C driver with checked 32-entry MMIO logs, complete linked
CODE/ABI/allocation rejection, and serial alias-aware synthetic AES/DMA cases.
**Never flash `aes_test.ihx` or upload it as a board artifact.** The isolated
test is separate from board firmware. The evidence-backed per-command ENC
correction changes AES and its proofs, not the fourteen pre-AES BINs or other
platform/debug drivers.
Preserve the [sequencing/history/lifetime contract](docs/ARCHITECTURE.md#isolated-aes-128-dma-block)
and [distinct physical gate](docs/VALIDATION.md#m2-isolated-aes-dma-block-coverage);
neither public KATs nor RAM-copy acceptance establish AES silicon behavior.
After representative full `all test` runs, cross-board byte-preservation work
may use existing explicit `host-tests_<board>` / matching fixture-host targets,
`all`, and `tests/boot_image.py` for the other board/image combinations instead
of repeating identical standalone mutation corpora. Run s51 serially with the
unchanged 15-second bound; the current CI runs all eighteen full jobs.

The separate AES board fixture adds no dependency or general debugger permission:

```sh
make BOARD=generic IMAGE=aes_fixture all test
make BOARD=lg_esl29_rev03 IMAGE=aes_fixture all test
PYTHONPATH=tools .venv/bin/python -B -m unittest test_aes_fixture -q
```

These commands exercise real compiled orchestration, explicit wire/ABI and
private-memory rejection, the independent public-corpus oracle, both genuine
negative contexts and synthetic transport failures. They never access USB.
`aes-reference` is a locally built host-only executable, not an uploaded artifact.
The [manual procedure](docs/DEBUGGING.md#aes-dma-board-fixture) is a separate
parent-owned hardware task after review, with a new full reset and complete
physical CODE comparison on every invocation. Preserve the parent-corrected
actual `90 00 00 E5 95` reader prefix and real-image preflight regressions.
The [first physical KEY failure](docs/DEBUGGING.md#2026-09-17-first-lg-aes-key-load-failure)
is historical root-cause evidence. The unchanged corrected LG image now has
[short-normal, both exact-negative and full-reset recovery hardware evidence](docs/DEBUGGING.md#2026-09-17-corrected-lg-aes-bounded-acceptance),
including fresh KEY/IV flags and verified per-phase ACKs. Recovery passed257
cycles/514 blocks and independently covered all168 vector/space/clock
combinations; that AES recovery ended at READY016A/config22/RC16,
completed/heartbeat1, fault latch0, before the later PRNG programming below.
Parent serial `all test` passed on both corrected boards with
415 Python tests/102 compiled fixture scenarios each and the 138-file guard;
all fourteen older BIN sizes/hashes independently matched published baselines.
This does not assert a hosted-CI pass.
Programming may use the checked board HEX or BIN, never a standalone test.
Do not resume/rekey a failed invocation or inspect private staging. Generic
remains host/image/synthetic-only; no build or test grants hardware permission.

The isolated deterministic PRNG foundation is included in `make test`:

```sh
make test-prng
```

It exercises explicit valid seeds, all65,536 mathematical states, exact
13-shift/state-period results, finite self-clear observations, shared ADC/CSP
ownership, unchanged caller output on failure and terminal re-entry. Genuine
SDCC calls have complete CODE/ABI/private-prefix/SFR-order rejection and
alias-aware synthetic coverage. **Never flash `prng_test.ihx` or upload it
as board firmware.** This is deterministic, explicitly seeded, **not entropy
or a cryptographic RNG**; the test-only mathematical models never link into
board images. Preserve the [register contract](docs/ARCHITECTURE.md#isolated-deterministic-prng)
and [bounded LG evidence and remaining physical gates](docs/VALIDATION.md#m2-deterministic-prng-coverage).

For this isolated change, run both AES-board `all test` configurations serially,
then focused build, board-host, image and alias checks for the other fourteen
images and compare all sixteen full BIN sizes/SHA-256 values with the published
baseline. Do not run sixteen redundant expensive standalone corpora locally.
Keep s51's15-second operation timeout and run heavy s51 checks serially.
That foundation kept sixteen full jobs and added no hardware runner.
The AES READY state at the end of that foundation is now historical;
the separately programmed PRNG fixture has the bounded evidence below.

The separate PRNG board fixture adds no dependency or general debugger permission:

```sh
make BOARD=generic IMAGE=prng_fixture all test
make BOARD=lg_esl29_rev03 IMAGE=prng_fixture all test
PYTHONPATH=tools python3 -B -m unittest test_prng_fixture test_m0_artifacts -q
```

Run the two new `all test` configurations serially, then focused build,
board-host, image and alias checks for the sixteen older combinations and
compare every complete older BIN size/SHA-256 against the published baseline.
Do not run eighteen redundant local standalone corpora. The full-period s51
proof uses33 bounded segments with byte-exact genuine checkpoint memory/CPU
continuation, not altered CODE, driver returns, period counters or reseeding.
Keep15 seconds per simulator process and run heavy simulations serially.
CI runs eighteen full jobs with exactly the same seven artifact paths and
`hardware_tested=false`. No host oracle/standalone test/log is uploaded.
The [manual short/full/stopped procedure](docs/DEBUGGING.md#parent-only-prng-acceptance-procedure)
requires separate hardware authorization; it is never part of these checks.
Holding a CPU cannot synthesize this API's poll-limit error. Recovery is a
distinct fully reset/full-CODE invocation, not resumption after a failure.
Wirev2 additionally checks ordered initial/C/live/next-C flag history:
only IRCON.STIF0->1 is allowed, never1->0; all other flag bits remain exact.
Do not clear STIF, write ST0/1/2, change compare, enable an ISR or reset between
period chunks to avoid the default compare event. The result's `flag_history`
keeps raw C/live values and a bounded transition record, not a timing estimate.

The original wirev1 LG PRNG image passed the
[dated short hardware case](docs/DEBUGGING.md#2026-09-17-lg-prng-short-acceptance):
five READY stages, two seed1234 loads and eight checked RC16 words, with
benign mask15, guards/tails and non-advancing CPU readback. Its
[first long run stopped on the fixture's STIF policy](docs/DEBUGGING.md#2026-09-17-lg-prng-long-run-flag-policy-interruption),
not a PRNG error; the last32-word batch was not host-accepted.
That old wirev1 halt is historical after the parent programmed the unchanged
7289-byte wirev2 and passed the
[corrected short case](docs/DEBUGGING.md#2026-09-17-corrected-lg-prng-short-acceptance).
The same installed wirev2 then passed
[full-stopped hardware acceptance](docs/DEBUGGING.md#2026-09-17-corrected-lg-prng-full-stopped-acceptance):
the entire131,084-word corpus on both clocks, actual C/live STIF race and
continued preservation, and genuine RCTRL11 rejection/re-entry without caller
changes. After closing that session, the parent passed a
[separate full-reset/full-CODE/full-corpus recovery](docs/DEBUGGING.md#2026-09-17-corrected-lg-prng-full-reset-recovery-acceptance)
on the same image, with a distinct C-observed STIF transition and continued
preservation. This completes the bounded LG short/stopped/reset-recovery
hardware gate. The final halt is ENDREADY016A/config26/RC16, fault0,
C/live IRCON80, with no probe execution or later resume. The stopped FAULT
snapshot is historical; recovery was not an automatic action or C-state clear.
Generic/EOC1/physical poll faults and broader M2 acceptance remain open.
The parent's independent **original-image** two-board `all test` runs completed:
429 Python tests,33 genuine continuation segments/full131,084 words, probe
and9 faults per board,150-file guard and all sixteen older identities matched.
Those passing synthetic tests did not model STIF arrival. No hosted-CI pass is claimed.
The implementer's corrected wirev2 two-board serial runs subsequently passed434 Python
tests each, the full word/continuation corpus, actual STIF arrival/race and
81 flag rejections. All sixteen older focused checks and individual full
BIN size/SHA-256 comparisons passed; the150-file repository/local-link guard
remains green. The parent independently reverified the sixteen published BIN
identities and both v2 images' three-mode prevalidation before programming.
Both parent LG/generic `all test` runs now completed with exit0 and434 Python
tests each, matching the full linked corpus and flag-fault evidence above.
Parent MAC alias,150-file repository/local-link and diff checks passed;
build metadata still has `hardware_tested=false`.
See [the separate offline evidence](docs/VALIDATION.md#m2-prng-board-fixture-offline-coverage);
those results do not establish the pending wider physical gates.

## Code conventions

- C99, fixed-width integers and explicit bounds.
- Four-space C indentation; tabs for Make recipes.
- Keep compiler-specific SFR/pointer/interrupt details in platform code.
- Avoid heap allocation, packed-struct wire serialization and unbounded waits.
- Keep ISR work small and audit SDCC reentrancy/stack implications.
- Errors must carry a reason and a defined recovery/state transition.
- Comment hardware/ABI subtleties, not obvious assignments.

## Hardware and reviews

No CI job or ordinary build target may flash hardware or transmit RF.
Manual hardware work requires a confirmed board/image, recovery backups and
an appropriate test setup. A software-only change is not permission to erase
a connected device.

`tools/check_debug_hardware.py` is a separate manual, non-flashing runner.
Its `--confirm-fixture-test` flag explicitly authorizes destructive reset,
CPU control, temporary RAM/alias writes and all four breakpoint slots, after
checking the local fixture artifacts; it compares physical CODE before resume.
`tools/erase_boundary_fault.c` is an opt-in macOS observer for an external
programmer, not a flashing target or ordinary debugger cleanup path. Follow the
[manual procedure and dated evidence](docs/DEBUGGING.md#manual-hardware-acceptance-and-recovery),
not an exit code alone. Neither tool belongs in CI or automatic hardware hooks.

`tools/cc_tool_no_run_guard.c` is a Linux-only per-process `LD_PRELOAD`
fail-stop guard for the separately reviewed external programmer, not a
programmer or general execution blocker. Once loaded it has no environment
disable switch. It permits the exact OUT reset-into-debug shape
`40/C9/value0/index1/NULL/length0`, blocks all other OUT `C9` before libusb,
flushes output and exits86; helper/log failures exit87. It does not fabricate
a successful transfer or substitute a reset. **Exit86 is not programming
success or halted-state proof**; independent readback/halt confirmation is
required. DEBUG RESUME/STEP and programmer RAM-helper execution are not
blocked. Actual guarded backup/programming is separately authorized hardware
work, never an ordinary development check. Keep compiled preloads, logs,
external GPL tools/source and private backups outside the repository and
generated CI artifacts; neither these host tests nor ELF binding inspection
close a hardware acceptance gate.

The [2026-09-16 LG record](docs/DEBUGGING.md#2026-09-16-lg-fixture-hardware-record)
establishes bounded LG/unbanked M1 completion: a full fixture check after the
first confirmed cable reconnection, followed by the final real unplug/failure
check. After the last replug, PyUSB enumeration and the full one-cycle fixture
check in an explicitly selected new session also passed, leaving the fixture
halted at `0x0173` at the end of that run. The record also includes 309 passing
pinned-dependency host tests (including 18 offline macOS observer tests).
Keep generic-board and standalone
`bringup` hardware claims unobserved; shared startup evidence is limited to
the separately dated LG fixture records in [DEBUGGING.md](docs/DEBUGGING.md).
Bank discrimination is conditional on future banked CODE.
Do not turn bounded M1 completion into a universal debugger or M2/RF/network
claim, or infer current hardware availability from finite acceptance evidence.

For protocol work, identify the specification revision and relevant sections.
For a footprint claim, identify code/XDATA/IRAM separately. For a compatibility
claim, provide the actual scenario and coordinator versions.
