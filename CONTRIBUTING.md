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

With SDCC 4.2.0, `s51`, Python 3.9 or newer, Tcl 8.6 (`tclsh8.6`), GNU Make and
a host C compiler on `PATH`:

```sh
make test-local
python3 tools/check_repository.py
git diff --check
```

`test-local` runs the complete Python tool suite once, all standalone
host/image/simulator component corpora once **for each board definition**,
and the board-specific host/image/simulator checks for all twenty-six board/image
combinations. Components have no `IMAGE`-dependent inputs. This removes
duplicate runs, not cases: every component still runs with both
`CC2530_BOARD` definitions, and every board fixture retains its own checks.
Separate directories under `build/local/` prevent cross-image relocated
listing collisions. Submakes use `-j1`, stop at the first failure, and retain
the 15-second per-simulator deadline; extra CPU cores do not justify
concurrent links into a shared directory or relaxed timeouts.

The existing `make BOARD=... IMAGE=... all test` command remains a full
single-configuration check. The twenty-six-job CI matrix runs its identical
Python tool suite once in generic/bringup, and `all test-common test-board`
is split into `all test-board` in every job plus `test-common` in the two
debug-fixture jobs, once per board definition. The tool suite itself includes both-board image profiles;
no component, board-image, simulator or artifact check is omitted.
Linked clock/TX metadata tests build their own fresh temporary artifacts when
SDCC is available; do not prepopulate development directories to activate them.
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

The [bounded parent selector](docs/NWK_PARENT.md) has focused
`make BOARD=... BUILD=... test-nwk-parent test-nwk-parent-sanitize` checks.
Its real collector/codec composition is included once per board in
`test-common`; preserve all five immediate listing snapshots, complete raw
CDB/map/CODE identities and half-range ambiguity/error cases. It has no board
image linkage or hardware operator; never flash `nwk_parent_test.ihx`.

The hardware-independent [binary noise health tests](docs/ARCHITECTURE.md#binary-raw-noise-health-test-foundation)
have `make BUILD=build/noise-health-check test-noise-health`, also in
`test-common` for both boards. Preserve startup/window/terminal boundaries,
independent native oracles, complete linked identities and alias/stack guards.
Diagnostic cutoffs and passing synthetic streams are not qualified source
parameters or entropy evidence. No raw captures, RF sampler or board-image
linkage belongs in this target; never flash `noise_health_test.ihx`.

The separate [raw IRND collector](docs/ARCHITECTURE.md#isolated-raw-irnd-acquisition)
uses `make BUILD=build/radio-noise-check test-radio-noise`, likewise included
once per board in `test-common`. Preserve its complete linked identities,
all four immediate listings, real driver/health calls, projected MMIO ordering,
partial captures, libc-scratch exclusions and terminal ownership. Synthetic
timer/radio inputs are not source characterization; the15-second simulator
deadline is unchanged. Its standalone harness has no board-image linkage. Never flash
`radio_noise_test.ihx` or put actual raw captures in Git/CI.

The distinct [boot-disarmed IRND board fixture](docs/RADIO_NOISE_FIXTURE.md)
uses `make BOARD=... IMAGE=radio_noise_fixture test-radio-noise-fixture`.
`test-board` includes strict native and ASan/UBSan cases plus the genuine linked
proof for each board. Preserve all nine immediate listings, complete
CODE/CDB/map identities, fixed command/profile ABI and actual full work caps;
never enlarge the16384-CODE/768-reserved-XDATA/SP7C budgets silently.
No physical operator is invoked by those targets, `test-local` or CI.

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

The [Nordic recovery-artifact checker](docs/NRF_RECOVERY.md) has the focused
offline command `PYTHONPATH=tools python3 -B -m unittest test_nrf_recovery -q`.
Its synthetic tests also run in `test-tools`. Matching private files never
establishes physical acquisition, successful restoration or programming
permission. Keep the shared `private_artifacts` helpers hardware-independent;
the passive-RX runner retains its existing `private_capture()` import surface.

The [manual Nordic readback operator](docs/NRF_RECOVERY.md#explicit-read-only-acquisition-operator)
adds `PYTHONPATH=tools python3 -B -m unittest test_nrf_acquire test_nrf_recovery -v`.
Tcl 8.6 executes its real generated script only with an original synthetic
backend; no real OpenOCD, probe or SDK is used by ordinary tests. Never put
`--execute-read`, a private selection file or a device capture in CI.
The separate reviewed external programmer build is not a test dependency.
The opt-in [v3 startup-binding profile](docs/NRF_RECOVERY.md#opt-in-v3-startup-source-binding)
uses the same focused command and preserves default v2 behavior. For the
separate compatibility rerun, select only the reviewed standalone interpreter
with `NRF_ACQUIRE_TEST_INTERPRETER=/reviewed/build/jimtcl/jimsh`.
Do not substitute device-enabled OpenOCD or treat a source-match report as
CPU/SRAM/RF authorization.

The [report-only Nordic overlay planner](docs/NRF_RECOVERY.md#offline-page-overlay-report)
adds `PYTHONPATH=tools python3 -B -m unittest test_nrf_overlay test_nrf_recovery test_nrf_acquire test_nrf_stimulus -q`.
Its synthetic tests are included in `test-tools` and exercise the actual CLI
without SDK, programmer, USB or serial access. Only an exclusive private JSON
report is produced; keep actual captures, staged images and reports outside
Git/CI. An artifact overlay never grants programming or recovery approval.

The [Nordic stimulus helper](tools/nrf_stimulus/README.md) has independent
host-only coverage via `python3 -B -m unittest tools.test_nrf_stimulus -v`,
automatically included in `test-tools`. Keep SDK target builds and generated
ELF/HEX/map/evidence outside this repository and normal CI. The explicit static
build audit is for the reviewed local pinned workspace, not untrusted downloaded
build metadata. No `flash`, `debug`, `recover`, serial or USB action belongs in
these tests. New helper evidence never waives the existing CC2530 or physical
recovery gates.

Its opt-in [SRAM-only profile](tools/nrf_stimulus/README.md#optional-sram-only-profile)
shares those tests; use
`PYTHONPATH=tools python3 -B -m unittest test_nrf_stimulus test_nrf_overlay -q`
for the coupled profile/overlay checks. Both SDK builds remain explicit and
external. `--ram-only` is a static-audit selector, never a loading or execution
option; ordinary CI still builds no Nordic image and accesses no equipment.

The [checked handoff preparation](tools/nrf_stimulus/README.md#checked-volatile-handoff-preparation)
adds `PYTHONPATH=tools python3 -B -m unittest test_nrf_handoff -q`.
It runs the real Tcl state machine with a synthetic MEM-AP backend and compiles
the existing portable C startup/observation ABI. No equipment, programmer or
SDK is required. The documented explicit standalone-Jim rerun checks the
reviewed external interpreter separately; never point its test override at
a device-enabled OpenOCD executable. A halted reset-vector result is not
successful original-firmware restart or RF/restoration acceptance.

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

The isolated [filtered receiver/AUTOACK owner](docs/RADIO_AUTOACK.md) has
`make BUILD=build/radio-autoack-check test-radio-autoack`, also in `test-common`.
Preserve the real timebase/service/caller link, all three immediate snapshots,
complete libc-scratch exclusions, atomic frame publication and exactly-once
destructive RFD reads. Its separate 24-KiB CODE/1536-byte XDATA reservation
and SP7C caps do not enlarge earlier budgets. Soft stop must retain AUTOACK
through in-flight completion and explicitly drain frames, never abort/flush
or hide loss. Do not turn physical STOPPED into MAC/POLL CLOSED, IFS completion,
ordinary-TX permission or a release to another reset-exclusive API.
AUTOACK transmits RF autonomously; these tests and CI authorize no hardware.
**Never flash or upload `radio_autoack_test.ihx`; no board image may link it.**

The isolated [awake MAC Timer foundation](docs/MAC_TIME.md) has
`make BUILD=build/mac-time-check test-mac-time`, also in `test-common`.
Keep the actual timebase/service/caller link order and all three immediate
relocated-listing snapshots, exact MMIO/latch/ABI proof, torn-latch and alias
negative controls, and independent deadline/work bounds. Its positive
512/`FFFFFF` periods do not define a power-of-two coarse modulus, calibrated
clock or captured TX/ACK end. Faults retain ownership; do not add implicit
recovery or silently compose its quiescent-radio contract with RX/TX.
**Never flash or upload `mac_time_test.ihx`; no board image may link it.**

The separate [offline MAC transmission scheduler](docs/MAC_TX.md) has
`make BUILD=build/mac-tx-check test-mac-tx`, also in `test-common`.
It composes the actual MAC codec with one owned frame copy and abstract
symbol-time events/actions, not successful substitutes for radio calls.
Keep its three per-link listing snapshots, whole CODE/private/field ABI,
alias/upper-IRAM and genuine stack-unwind checks. Its own 28-KiB CODE and
1,280-byte XDATA reservation budgets do not enlarge earlier component limits.
The private control-suffix mirror must retain per-field layout assertions,
generic-pointer ABI, inactive bytes and native padding; do not add a second
frame or reentrant/ISR use. Measured headroom is not permission to claim
full-stack fit. A real adapter must establish unified TX/RX ownership,
event timestamps and physical ACK/timing behavior separately.
**Never flash or upload `mac_tx_test.ihx`; no board image may link it.**

The [Beacon candidate collector](docs/NWK_CANDIDATES.md) has
`make BUILD=build/nwk-candidates-check test-nwk-candidates`, also in
`test-common`. Preserve real MAC/MAC-Beacon/NWK decoding, explicit CRC/channel
input, four copied slots, full-table duplicate/withdrawal handling and atomic
error behavior. Its separate 20-KiB CODE/1,280-byte XDATA reservation and four
per-link listing snapshots do not enlarge earlier budgets. Candidate retention
is neither active scan completion nor compatible/authenticated parent selection.
**Never flash or upload `nwk_candidates_test.ihx`; no board image may link it.**

The [offline active-scan controller](docs/MAC_SCAN.md) has
`make BUILD=build/mac-scan-check test-mac-scan`, also in `test-common`.
Preserve the six real modules and immediate per-link listing snapshots,
serialized `mac_tx_step` grants, persistent DSN ownership, actual collector
calls, explicit hardware-confirmation boundaries and confirmed restoration.
Its separate 32-KiB CODE/2-KiB XDATA reservation budget does not enlarge earlier
limits; the current test image leaves 3328 CODE bytes. Keep the SP7C cap,
alias/upper-IRAM guards and genuine complete scenario corpus. Do not replace
missing physical timestamps or adapter behavior with successful stubs.
**Never flash or upload `mac_scan_test.ihx`; no board image may link it.**

The [offline Association Response context](docs/MAC_ASSOCIATION.md) has
`make BUILD=build/mac-association-check test-mac-association`, also in
`test-common`. Keep the real codec/context/caller link order, three immediate
listing snapshots and complete CODE/private/caller/field ABI proof. Its own
16-KiB CODE/1,024-byte XDATA reservation does not enlarge earlier budgets.
An unknown coordinator IEEE remains explicitly unbound; byte equality is not
authentication. Captured time, epoch and immediate receiver ACK are independent
adapter obligations, not services supplied by this foreground metadata filter.
**Never flash or upload `mac_association_test.ihx`; no board image may link it.**

The [conditional legacy extraction controller](docs/MAC_POLL.md) has
`make BUILD=build/mac-poll-check test-mac-poll`, also in `test-common`.
Preserve the real MAC codec/TX/Association/POLL/caller link order and five
immediate listing snapshots. Its separate 32-KiB CODE/2-KiB XDATA reservation
and SP7C cap do not enlarge earlier budgets. Keep all52 scenarios, full raw
metadata, actual Association forwarding, exact-D reception and independent
logical-result/cleanup checks. The13 four-case simulator continuations require
complete CPU/IRAM/SFR/64-KiB-XDATA comparison and the unchanged15-second process
deadline; never reset caller state or patch returns to finish the corpus.
Configured PIB F, captured ACK end, loss-free closure and immediate lower-MAC
ACK/IFS are truthful adapter preconditions, not successful stubs.
**Never flash or upload `mac_poll_test.ihx` or its independent floor diagnostic.**

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
Controller-idle is not verified NV success; the separate writer adds policy
and readback, while hardware acceptance remains gated. Preserve the exact linked
extent/return ABI, copy-readback and alias/private-prefix proofs.
**Never flash `flash_exec_test.ihx` or upload it as a board artifact.**
No ordinary build/test target may invoke this engine on a physical device.

The [public reserved-page writer](docs/ARCHITECTURE.md#verified-reserved-page-erase-and-program)
has `make BUILD=build/flash-write-dev test-flash-write`, also in `test-common`.
It links the real executor, reader and policy in that order, below the
unchanged512-byte reservation budget. Keep one-attempt-per-word history,
unknown-after-reset handling and full erase/program readback. Source inputs
must follow the entire combined private/compiler prefix. The native engine
model is shared with the executor corpus and never enters SDCC firmware.
**Never flash or upload `flash_write_test.ihx`.** #8 is a separate fixture
and explicit physical-acceptance gate; these tests access no device.

The separate [generic two-page snapshot journal](docs/NV_RECORDS.md) has
`make BUILD=build/nv-record-check test-nv-record`, also in `test-common`.
Keep the byte-identical published flash backend and four per-link listing
snapshots, fresh erase before each replacement, commit-last word, explicit
degraded recovery and nonwrapping generations. Its combined 1-KiB XDATA
reservation does not raise any earlier budget. Native cuts/torn-cell models
and genuine linked reset/RAM execution are not electrical power-loss or
security-counter proof. The runtime erase budget is not lifetime endurance.
**Never flash or upload `nv_record_test.ihx`; no board image may link it.**

The separate [boot-disarmed flash fixture](docs/FLASH_FIXTURE.md) has focused
offline checks:

```sh
make BOARD=generic IMAGE=flash_fixture test-flash-fixture
make BOARD=lg_esl29_rev03 IMAGE=flash_fixture test-flash-fixture
PYTHONPATH=tools python3 -B -m unittest test_flash_fixture test_m0_artifacts test_local_checks test_timebase_fixture test_m1_access test_m1_image -q
```

Run heavy s51 checks serially, with the existing15-second per-process deadline.
The fixture links real services before callers, retains whole CODE/ABI/private
prefix proofs and per-linked-image relocated listing snapshots, and measures
500 XDATA bytes including reservation within its own512-byte budget. Do not
enlarge component budgets or convert IRAM alias/gaps to storage. The exact
one-page sequence is terminal, without retry. CI's24 board jobs retain exactly
seven upload paths and `hardware_tested=false`; no standalone flash executable
or private recovery material is a board artifact. Physical arming/running is
blocked on new board/scratch/backup/destructive-scope authority and resolution
of the documented inspection/recovery gates. There is no hardware runner.

The independent passive RX foundation has `make test-radio-rx`, also included
in `make ... all test`. Its [contract and synthetic evidence](docs/RADIO_RX.md)
do not grant hardware access. `radio_rx_test.ihx` is never a board image,
flash input or CI upload. Keep the complete linked-code/MMIO proof, original
512-byte reservation, alias/upper-IRAM guards and 15-second simulator timeout.

The separate [radio ownership queues](docs/RADIO_QUEUE.md) have
`make BUILD=build/radio-queue-dev test-radio-queue`. This composes the actual
RX/timebase/IRQ services with copied pools and a reentrant hint producer;
it does not enable a radio ISR or implement transmission. Preserve its
explicit 1-KiB composed XDATA/8-KiB CODE budgets without increasing earlier
component limits, and keep generic-pointer/compiler scratch out of the ISR
call graph. The native RX model's `--queue-vectors` mode supplies successive
real-driver traces without a peripheral reset. No vectors or
`radio_queue_test.ihx` belong in board artifacts or physical experiments.

The separate [init-time TX/CCA primitives](docs/RADIO_TX.md) have
`make BUILD=build/radio-tx-check test-radio-tx`, also in `test-common`.
Link timebase, FIFO, TX, then callers, and retain all four per-image relocated
listing snapshots. Keep the 512-byte total XDATA reservation and explicit
FIFO preload/clear calls with separately checked deadlines. TXDONE is not an
ACK; CCA busy is recoverable only after verified shutdown. Do not compose
this reset-exclusive owner with the legacy RX service in the same reset
epoch, widen power profiles or add automatic RF. Neither `radio_tx_test.ihx`
nor its synthetic traces is a board artifact or a physical-test input.

The separate [boot-disarmed TX board fixture](docs/RADIO_TX_FIXTURE.md) has:

```sh
make BOARD=generic IMAGE=radio_tx_fixture test-board
make BOARD=lg_esl29_rev03 IMAGE=radio_tx_fixture test-board
PYTHONPATH=tools python3 -B -m unittest test_radio_tx_fixture test_clock_fixture test_local_checks test_m0_artifacts -q
```

Keep timebase/FIFO/TX/clock before every caller, all nine immediate per-image
listing snapshots and the existing512-byte total reservation budget. Boot and
ordinary unarmed continuation must perform no RF; distinct ARM/RUN admissions
are followed by an inspectable ADMITTED return before the one conditional-clear
attempt. Do not add retries, ACK, alternate power modes or implicit fault cleanup.
The manual runner needs its separate reset/CPU/read/write/breakpoint permissions
and RF opt-in; it never flashes and must not run from Make or CI.
Clock staging affects every clock consumer: preserve their full corpora,
exact changed emission proofs and old budgets, not just the new TX image.
Clock standalone and board listings also need immediate distinct snapshots.

For the separate RX board integration, use focused checks rather than
redundant local standalone corpora:

```sh
make BOARD=generic IMAGE=radio_rx_fixture test-radio-rx-fixture
make BOARD=lg_esl29_rev03 IMAGE=radio_rx_fixture test-radio-rx-fixture
PYTHONPATH=tools python3 -B -m unittest test_radio_rx_fixture test_m0_artifacts test_timebase_fixture test_m1_access -q
```

Run heavy s51 jobs **serially**, preserving15 seconds per process and exact
CPU/RAM continuations for the full poll-cap test. Rebuild/image-check affected
board BINs: unchanged consumers retain their complete baseline hashes, while
intentional changes require reviewed updated proofs and separate hardware
evidence. Perform broader final regressions/publication separately.
The fixture is **RF-capable passive RX**, not another non-RF image. Never
invoke its [manual runner](docs/DEBUGGING.md#parent-only-passive-rx-acceptance)
from tests/CI. It requires a separately authorized board/recovery task and a
new mode0600 capture in a user-owned0700 directory outside the repository.
No raw bodies, addresses, identities or frame hashes belong in stdout/CI.
CI now has24 jobs (including flash and boot-disarmed TX fixtures) and exactly the existing seven upload paths, with
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

`make BOARD=... BUILD=... test-zcl-basic`, included once per board in
`test-common`, checks the [read-only Basic provider](docs/ZCL_LAB.md) through
the real existing handlers, including native ASan/UBSan and genuine linked
execution. Preserve all six immediate listing snapshots, complete raw
CDB/map/CODE identities, caller/libc boundaries and alias/stack negatives.
Its24,576-CODE/1,536-reserved-XDATA budget changes no older component limit.
No board image may link it; **never flash `zcl_basic_test.ihx`**.
Identify, writes, reporting and application advertisement remain separate.

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
