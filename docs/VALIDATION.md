# Validation and evidence

## Evidence levels

Use these terms consistently in documentation and pull requests:

| Level | Meaning |
| --- | --- |
| Host-tested | C/Python behavior checked on the development computer |
| Image-checked | Actual linked firmware parsed for checksums, bounds, symbols and memory layout |
| Simulated | Linked 8051 instructions executed in a specified simulator/model |
| Hardware-observed | A specified board/firmware produced an observed physical result |
| Interoperability-tested | A documented coordinator configuration passed a defined protocol scenario |

None of these silently implies the next level. A screenshot is not a network
test, and an interview is not proof of reliable SED behavior.

## M0 coverage

The current build must cover:

- Strict host compilation and status/board tests.
- Intel HEX integrity, image equivalence and memory bounds.
- Ordinary/status/IRAM-alias allocation checks, including negative cases.
- Bounded execution of the linked image with the CC2530 IRAM alias modeled.
- Explicit reporting of the simulator's limitations.

The generic simulator does not emulate the RF subsystem, analog behavior,
physical supply rails or a display. M0 CI is not a silicon test.
Neither standalone `bringup` image has been flashed. Physical evidence for
shared startup/status is limited to the LG M1 and timebase fixtures recorded
below, not a generic-board or standalone-M0 acceptance claim.

## M1 fixture automated coverage

`IMAGE=debug_fixture` adds a separate target image for both boards, covered by
the same HEX/BIN, memory/alias and board-startup checks as M0. It also checks:

- The versioned 16-byte state within ordinary XDATA, without expanding status.
- All 256 seed values, counter wrap, reinitialization and no extra host MMIO.
- Exact probe instruction bytes and distinct linked code locations, including
  rejection of missing symbols, changed operands and a stop label on RET.
- Actual nested calls, known registers and a NOP moving PC exactly one byte
  without changing the captured memory/registers.
- 257 linked-image cycles through real returns, without stack leakage,
  writes outside allocated XDATA or changes to immutable M0 status fields.

These automated checks are **host-tested, image-checked and simulated**.
They do not by themselves test physical breakpoint comparators, USB/debug
commands or a host frontend's register-preserving reads. The
[fixture contract](DEBUGGING.md#implemented-target-fixture) provides the ABI,
golden state and resource accounting. Separate physical evidence now exists
for one LG fixture; the generic board remains hardware-unobserved.

## M1 host transport automated coverage

Synthetic backends cover explicit USB selection and claiming, permission
denial before I/O, exact diagnostic requests, malformed/short replies, partial
writes, disconnect/errors at each phase, one shared operation deadline and
terminal failure/cleanup states. CLI errors must emit no success JSON.
Unknown targets, unsupported descriptor layouts and active kernel drivers are
refused rather than reset, reconfigured or silently accepted.

`tools/test_m1_transport.py` and the facade tests in `tools/test_m1_usb.py`
require only the standard library. With the pinned optional PyUSB installed,
additional tests exercise its real resource manager with a synthetic driver,
including failed/interrupted claiming and release-error propagation. No test
calls real USB discovery or opens libusb hardware. CI runs both the
dependency-free and optional test paths. This resource-manager coverage is **host-tested** evidence only, not
hardware-observed debugger behavior. It adds no firmware memory consumption.

The CPU-control tests additionally cover separate permissions, locked/erasing,
unstable-clock and power-mode rejection, halted preconditions, invalid
postconditions, undefined HALT replies, breakpoint races, immediate
breakpoints after RESUME, and all 256 STEP accumulator / GET_BM raw values.
Precheck/command/postcheck share one deadline and never retry a possibly
executed command. Config reads check lock state rather than issuing disabled
commands on a locked target.

Reset tests separately cover denied permissions before I/O, exact vendor-OUT
reset-into-debug fields, integer zero-length completion, refreshed adapter
family and halted/awake postconditions. Every transfer boundary, timeout,
interruption and cleanup error is covered without a retry, fallback HALT or
reset into normal execution. Initial attach tests cover the distinct access
policy, blocked target operations before readiness, exact `C5`/`C8`/`C9`
ordering and metadata layout, invalid descriptor revisions, partial writes
and failures at every preparation boundary. Successful attach does not grant
CPU-control permission or trigger resume on close. This does not prove reset
timing or initial attach on physical hardware.

`tools/test_m1_access.py` adds an original small 8051/backend model for the
individually confirmed standalone PC/instruction/breakpoint packets. It checks
all accumulator values/parity, all four PSW register banks and DPS 0/1,
both DPTRs, full-context restoration, safe SFR/range bounds, byte-by-byte write
readback, separate permissions, and one lock/deadline across complete helpers.
Every exchange boundary is exercised with failures, short replies and partial
writes; corrupt restoration, stale/late results and denied resume are errors,
not triggers for recovery or retry. CLI tests check exact JSON, immutable write
bytes, pre-load permission/operand rejection and no success on cleanup failure.
These are host tests of original instruction semantics, not linked-image or
silicon evidence.

`tools/test_erase_boundary_fault.py` compiles the original macOS observer with
a synthetic USB library and driver in a temporary directory, using strict
compiler flags. It covers opt-in/pass-through, exact erase/status association,
handle isolation, status bits, short/failed transfers, stale-reply rejection,
ABI forwarding and exit-before-cleanup. It never loads real libusb or runs
`cc-tool`, and explicitly skips outside macOS or without a host compiler.
This test is distinct from physically interrupting an external programmer.
The 2026-09-16 full pinned-dependency host suite passed **309 tests**, including
all **18 offline macOS observer tests** after the invalid-transfer and requested
IN-length guards were corrected. That host result is not cable or flash
hardware evidence.

Offline image tests cover board/image/compiler/hash mismatches, supported
CODE/XDATA/SFR symbol classification, conflicting map/CDB spaces/addresses, missing
symbols and non-executable breakpoint targets. Synthetic parameter vectors
cover all four breakpoint slots and eight bank-bit values, not real
comparators. M0/M1 decoders reject invalid ABI/phase/board/guard/checkpoint
records and oversized snapshots. The alias-aware image test also exercises
symbol lookup and decodes actual simulated status records; this links the
offline reader to the compiled fixture without creating USB hardware evidence.
Source-location tests preserve multiple mappings, reject malformed/conflicting
records and addresses outside the image, and reject missing exact matches
rather than guessing a nearby line. Both board/image builds check main's
source records; the fixture also checks the actual declaration lines and
linked addresses of all four stage functions. These are offline source
metadata checks, not source stepping through a hardware debugger.

## M1 hardware evidence and acceptance boundary

The canonical [2026-09-16 LG record](DEBUGGING.md#2026-09-16-lg-fixture-hardware-record)
contains sanitized adapter/host/backend versions, the unchanged 626-byte
fixture's BIN hash and observed addresses. It separates preliminary global
PyUSB 1.2.1 runs from the final isolated, pinned PyUSB 1.3.1 **257-cycle physical
pass**. One LG Rev0.3 was observed; the generic board was not.

That record establishes four simultaneous unbanked comparator slots, nested
SP values, known registers, full M1/M0 cycle checks and wrap, NOP `PC+1`,
resumed real calls, explicit HALT and reset/reinitialization. It also records
scratch SRAM write/readback/restore, bidirectional DATA/XDATA aliasing, all
eight PSW-register-bank/DPS combinations with nonzero DPTR1, and real USB
timeout/zero-length/PIPE failures with fault latching and separately authorized
endpoint recovery. Ordinary API cleanup never clears stalls.

The controlled interrupted-flash recovery result is limited to a **halted
erased-image boundary**: observed erase/status traffic and exit 99 before
programming/reset cleanup, independent erased-CODE checking, then explicit
complete fixture reprogram/readback and the final acceptance pass. It does
not establish a mid-word electrical power cut or wear tolerance. External
`cc-tool` can reset normally and exit 0 after a verification mismatch;
independent actual readback is required, not a successful process status alone.

After the **first confirmed cable reconnection**, the adapter was explicitly
selected at its new address and the complete one-cycle fixture check passed:
physical CODE verification, all four slots, alias checks and reset.
The **subsequent final held-handle cable-unplug check also passed**: production
`read_pc()` failed at control IN with errno 19 / backend `-4` NO_DEVICE.
The session was `FAULTED`; resume was denied with an unchanged bulk-write
count, and cleanup exposed release-interface NO_DEVICE.

After the **last replug**, PyUSB enumerated the adapter again. Explicit
selection at the new address and a full one-cycle `check_debug_hardware` run
in a new session passed: reset PC `0x0000`, all 626 physical CODE bytes matching
the hash-verified fixture, all four slots/SPs, alias/RAM restoration, one golden
cycle/NOP and reset reinitialization. That final run left the fixture halted
at `0x0173`, establishing recovery after the final real unplug rather than
enumeration alone. No numeric location or identity is published.

**M1 is complete for the bounded LG/unbanked baseline.** These finite
fresh-reconnect, physical-disconnect and explicit new-session recovery
scenarios establish the gates, not universal compatibility.
Earlier confirmation timeouts and software USB resets are not substituted
for the real removal. Generic hardware, bank discrimination and
sleeping/MMIO/full-SFR/flash-writer/GDB support remain outside this acceptance.
Bank discrimination becomes a requirement when banked CODE is introduced;
mid-word power-cut and flash-wear evidence belongs to future
platform/persistence work.

The [manual runner/procedure](DEBUGGING.md#manual-hardware-acceptance-and-recovery)
is outside CI, never flashes and verifies physical fixture CODE before resume.
Its JSON `not_tested_by_this_run` list describes that runner's scope, not
separate companion checks: it never flashes or automatically disconnects USB.
Generated `hardware_tested=false` metadata is not rewritten by this documentary
record. No M2 service, RF, network or
interoperability support is implied.

## M2 awake-only timebase automated coverage

The [first bounded timebase slice](ARCHITECTURE.md#awake-only-timebase-first-m2-slice)
has **host-tested, image-checked and simulated** standalone coverage.
The later [LG board-fixture acceptance](DEBUGGING.md#2026-09-16-lg-compiled-c-timebase-acceptance)
adds hardware execution of the C implementation, separately from both these
automated checks and the independent register observation below.
`make test-timebase` runs it independently; every existing `make ... all test`
combination also includes it. `timebase_test.ihx` is a standalone component
test, not a board image, flashing target or CI upload artifact.
The existing `bringup` and `debug_fixture` images still do not link the module
or change their firmware behavior/bytes. A later, distinct `timebase_fixture`
board image is covered separately below; it is not this standalone executable.

`tests/test_timebase.c` supplies original shared host/SDCC vectors for zero
delay, byte/counter rollover, maximum permitted delay `0x7FFFFF`, exact expiry,
one tick before/after, both sides of half-range, exactly-half ambiguity in both
directions, invalid upper bits/delays and null pointers. Error checks preserve
the full 32-bit sentinel or each initial boolean value.

Host-only coverage additionally checks every one of the **16,777,216 counter
phases** with zero/max-delay construction and expiry, and every modular
comparison delta at three deadline origins. The MMIO hook models a ticking
counter whose full value latches only on ST0. It advances between byte reads
through `0000FF -> 000100`, `00FFFF -> 010000` and `FFFFFF -> 000000`, checks
all 65,536 low/middle phases at three high-byte values, and exercises stationary
and larger synthetic tick increments. Every sample must return exactly the
zero-extended latched value with exactly three reads in `95,96,97` order,
zero writes and unchanged GPIO/clock/IRQ registers. The pre-existing host
write log and startup/board write contracts remain intact.

`tests/boot_timebase.py` parses the actual IHX/map/CDB/memory files and locks
the reviewed SDCC 4.2.0 reader's 88 instruction bytes, including its three
SFR reads, compiler scratch addresses and return packing. Mutation of every
opcode/operand is rejected, alongside missing bytes, wrong SFR/stop symbols,
CODE bounds, status/alias placement, unaccounted storage, reservation budget,
MPAGE, result ABI and stack violations. This is linked-code inspection, not
source-text inference or a successful host-clock substitute.

Alias-aware s51 execution runs the bounded deadline/error vectors and 17
reader samples through actual calls/returns. It supplies **synthetic prelatched
SFR bytes**, poisons each register after its linked read, checks all four
returned bytes and immutable result/status/guard regions, and rejects stack
leakage or changes to guarded SFRs. The simulator's alias is independently
exercised in both directions, including a failing no-alias control.
Generic C52/s51 does **not** emulate the physical CC2530 Sleep Timer's ticking,
latching, clock rate or wake-edge behavior; the host model and linked SFR
injection are separate forms of synthetic evidence.

With the baseline model-large/code-size flags, `timebase.rel` contains 404
CODE bytes, 25 ordinary XDATA bytes (including three reader scratch bytes),
three overlayable DATA bytes and one BIT, excluding shared runtime and tests.
It has no retained software epoch or heap. The isolated executable accounts
for its own CODE separately, uses 35 ordinary XDATA bytes plus an 8-byte
test result at `0x1E00`, and retains the full 64-byte status reservation:
99 nonaliased XDATA bytes reserved, within the unchanged 512-byte budget.
Its IRAM stack begins at `0x21`, with 223 bytes reserved; the upper 128-byte
guard remains untouched in these executions. This is a bound, not a measured
worst-case hardware stack depth. Ordinary allocation stays below `0x1E00`;
`0x1F00..0x1FFF` remains the IRAM alias, never a second allocation pool.
The checker reports the complete test executable's linked resource counts.

M2 remains open. Physical C-driver execution is now established for the
specific LG fixture below, not generic hardware or all platform timing.
No precise tick period, calibration,
PM1/PM2 synchronization, PM3 continuity, interrupts, compare/wake handling,
radio, AES or flash service is established by this slice. The existing M1
LG/unbanked hardware record and fixture hash are unchanged and do not supply
those missing platform measurements.

## M2 timebase board fixture automated coverage

`IMAGE=timebase_fixture` is a separately selected safe non-RF board image,
not a repurposed `timebase_test.ihx`. Its C reader and deadline helpers run in
the foreground with a fixed 128-raw-tick delay and independent 1,024-poll limit.
Both board builds are **host-tested, image-checked and alias-aware simulated**.
The [dated LG record](DEBUGGING.md#2026-09-16-lg-compiled-c-timebase-acceptance)
adds hardware-executed C-driver acceptance for one LG/image only; generic
hardware and physical timer-fault injection remain unobserved.
The [ABI, checkpoints, resource counts and manual procedure](DEBUGGING.md#awake-only-timebase-board-fixture)
are separate from the existing M1 fixture's hardware record.

Host tests cover initialization/reinitialization, byte/counter wrap, 257
completed cycles and heartbeat wrap, pending/overshoot/max-half-window results,
success on the final permitted poll, stopped clocks, backward observations
(including a backward step still after the initial sample), half-range
ambiguity, invalid phases and unchanged latched faults. Every real-reader
triplet is checked before consuming its host log; no overflow assertion is
removed. Separate host-only link doubles exercise every non-OK helper result,
including invalid arguments that a genuine zero-extended SFR reader cannot
normally produce. Production code never links these doubles.

The common image verifier retains all HEX/BIN, CODE, XDATA/status/alias,
512-byte reservation, MPAGE and IRAM checks. It additionally verifies the
32-byte state, all three exact checkpoint instruction sequences, relocated
88-byte reader operands and matching scratch CDB records. Negative cases
cover each reader/checkpoint byte, missing/conflicting scratch records,
wrong symbols/SFRs, unallocated/overlapping scratch and ABI/budget corruption.
The offline decoder checks complete records, fixed bounds, helper/reason
consistency, modular deadline/elapsed relationships and reserved/guard bytes;
RUNNING is not accepted as a stable snapshot.

The genuine linked board image executes 257 successful cycles with synthetic
prelatched SFR inputs, including rollover and maximum valid elapsed time.
Separate runs exercise the real 1,024-poll fault and backward/half-range
failure branches. Fault-loop execution must leave RAM and CPU state unchanged.
M0 status/heartbeat, GPIO/clock/IRQ preservation, real return-stack depths,
unallocated XDATA and upper IRAM guards remain checked. This does not model
physical Sleep Timer ticking/latching or prove physical comparator behavior.

The manual runner has synthetic orchestration tests for full CODE verification
before any resume, 257 cycles/wrap, complete status decoding, CPU preservation,
FAULT/unexpected-PC/data errors and failure at every composed operation.
Its real guarded wait is also exercised with a synthetic backend and clock:
one decreasing deadline, terminal timeout and denied resume without more I/O.
Authorization, invalid selection/artifacts, minimal permissions and cleanup
failure are checked without loading a physical USB backend. Success JSON is
emitted only after cleanup. These tests are not a hardware run.

The operator separately reran both new board-image `all test` configurations
with pinned Python: all 327 tests and strict simulation passed. The preceding
all-six-configuration validation and unchanged-four-old-BIN-hash checks still
stand. These automated results remain distinct from the physical acceptance.

## Compiled-C Sleep Timer hardware acceptance (2026-09-16)

The [canonical LG record](DEBUGGING.md#2026-09-16-lg-compiled-c-timebase-acceptance)
identifies the new 1,847-byte image/hash, separately authorized programming
and independent complete physical CODE comparison before resume. An initial
three-cycle run and a fresh 257-cycle run passed through the actual C reader
and fixture logic. The full run returned helper/reason zero throughout,
elapsed 129..130 raw ticks for requested 128, and exactly 37 polls per cycle.
Completed-cycle/M0 heartbeat byte wrap, initialization/NOP checkpoints,
immutable M0 status and CPU-register preservation passed. The last reported
board state is the new fixture halted at READY PC `0x016A`, with the startup
clock snapshot still `C9`; the old M1 record is historical, not the live image.

This is **hardware-observed successful awake C-driver execution**, not
calibration or a natural 24-bit counter-wrap observation in these C cycles.
Stopped/backward/ambiguous C paths remain host/simulator-only; no source
switching, IRQ/compare/wake, RF, AES or project flash service is established.
Generic remains host/image/simulator-only and M2 #4 remains open. The earlier
register-only experiment below has separate natural-rollover evidence.
Raw run JSONs remain private; only the processed operator summary is published.

## M2 init-time system clock automated coverage

The [init-time selector](ARCHITECTURE.md#init-time-system-clock-selector-isolated-m2-slice)
has **host-tested, image-checked and simulated** cancellation coverage, plus
separate [bounded LG compiled-C hardware acceptance on 2026-09-17 (UTC+03)](DEBUGGING.md#2026-09-17-lg-compiled-c-clock-acceptance).
The first clock board image passed normal LG switching but
[failed rollback acceptance](DEBUGGING.md#2026-09-16-lg-clock-cancellation-failure).
Neither the earlier raw-register experiment nor the accepted LG C timebase
fixture validates rollback. M2 #4 remains open. `make test-clock` is
offline and is included in every board/image check. That initial slice added
no board image or driver object to existing board firmware. A later separate
clock board fixture is described below; the six earlier BINs remain unchanged.
`clock_test.ihx` is a synthetic standalone executable: **never flash it or
upload it as a board artifact**. The existing CI whitelist excludes it.

Strict host coverage checks all **65,536 CMD/STA entry pairs**, every enabled
bit of IEN0/1/2, MODE/reserved-bit rejection, both HF directions across all
16 LF/TICKSPD combinations, clamping and preservation of other command fields.
Tests cover genuine no-write/no-timer idempotence, invalid arguments with
unchanged output/no MMIO, delayed STA, exact/late/zero/max-timeout boundaries,
byte/24-bit rollover, stopped time, backward-before-start and backward-within-
window observations, exact ambiguity, and changed command readback.
Both success and exhaustion at poll boundaries are checked, including **65,535
polls per attempt** without a 16-bit wrap. Each failure must return its original
cause, restore exactly once, and separately report confirmed/failed rollback;
no original-request retry or unbounded loop is permitted.

The cancellation regression additionally covers 16 command-field combinations
in both directions with **1..32 old matches** before a delayed requested-source
observation and return, including raw rollover. Old matches never confirm
rollback. Never-departed deadline and stopped-counter/65,535-poll cases report
`CLOCK_ROLLBACK_UNCONFIRMED=9`; departure at the final poll without return still
fails. Late source confirmation before cancellation is retained as evidence
for a later saved-source match. No fixed read count or grace interval is used.

Every host read is checked against an original script for address, value and
write count at that exact boundary; the previous read-log entry is validated
before consumption. The shared 32-entry logs and overflow assertions remain
intact. Only the expected one/two CLKCONCMD writes may occur; other registers
and caller diagnostic boundaries remain unchanged. Separate host-only helper
doubles cover defensive invalid deadline/expiry returns in request and rollback,
including writing the cancellation even when its own deadline helper fails.
Those doubles never enter the target executable or any board firmware.

`tests/boot_clock.py` reuses the strict standalone layout checks extracted from
the timebase checker: real IHX/map/CDB/memory accounting, ordinary XDATA below
`0x1E00`, the full 64-byte status reservation, 512-byte nonaliased reservation
budget, MPAGE `0x93`, lower unbanked CODE and at least 128 reserved stack bytes.
The existing isolated timebase's exact 88-byte reader contract is unchanged.
The clock executable checks that same reader, real timebase calls, exact
SFR declarations, all diagnostic field offsets/sizes and matching source records.
The clock module's linked listing must cover every byte of its actual IHX
extent without gaps, overlap or unreviewed instruction lengths. Direct/bit SFR
operands are inspected: six passive read sites and one CLKCONCMD write site,
not fictional SLEEPSTA stability or SLEEPCMD power-down controls. Negative cases
reject changed actual bytes, source/layout/status/alias/budget/stack metadata,
and forbidden writes even when the listing is changed to match the mutated IHX.

Alias-aware s51 executes **40 bounded scenarios** through actual calls/returns,
including both directions, clamping, entry errors, exact/late deadlines,
counter rollover/range failures, independent rollback errors, and two stopped-
counter attempts of 257 polls each. Every expected MMIO event is a linked
instruction breakpoint with a checked PC and read/write value; unexpected
events cannot be skipped silently. Complete 19-byte diagnostics, return status,
the eight-byte result ABI, nonallocated XDATA, upper IRAM and unrelated SFRs
are checked. Alias behavior is independently tested in both directions with a
failing no-alias control. SFR values are **synthetic**: generic C52/s51 does not
model CC2530 analogue oscillator startup, source stability, divider-transition
timing, automatic calibration, or physical Sleep Timer ticking/latching.

SDCC 4.2.0 model-large/code-size accounting:

| Component | CODE | Ordinary XDATA | IRAM |
| --- | ---: | ---: | --- |
| `clock.rel` alone | 1,824 bytes | 44 bytes | 45 DATA bytes; no own BIT/overlay area |
| Existing `timebase.rel`, separate | 404 bytes | 25 bytes | 3 overlay DATA bytes, 1 BIT |
| Complete isolated `clock_test.ihx`, including tests/runtime | 3,351 bytes | 154 bytes | Stack starts `0x4E`, 178 bytes reserved; upper 128-byte guard untouched |

The test additionally uses eight result bytes at `0x1E00`, retaining the whole
64-byte reservation: 162 bytes used / 218 reserved nonaliased XDATA. Its linked
IRAM also accounts for eight bank registers, 45 DATA bytes, five overlay bytes
and BIT storage; linker area extent is not all live DATA. These are linked
allocations and simulated guards, not a measured hardware worst-case stack.
The 19-byte caller-owned target diagnostic object is not persistent driver state.
The fix adds 168 CODE bytes, four ordinary XDATA bytes (one local flag and a
three-byte pointer argument) and eight DATA bytes to each linked clock image.
The public 19-byte diagnostics and 56-byte fixture ABI are unchanged.
No older board firmware bytes, M0 ABI/reservation, board policy or historical
M1/timebase evidence change.

On 2026-09-16, all six `make BOARD=... IMAGE=... PYTHON=.venv/bin/python all test`
configurations passed, including the unchanged **327-test pinned Python suite**
and existing host/image/alias-aware component checks. Repository/local-link
and whitespace guardrails passed. None of the six board maps contains
`_clock_select_init`; exact BIN lengths and SHA-256 values remain:

| Board / image | Bytes | Unchanged SHA-256 |
| --- | ---: | --- |
| generic / bringup | 331 | `fccb1cd684dc15038c0cf24837508f5699083f68be400b98df37dc5df206bd7a` |
| LG / bringup | 371 | `6725a2804ee701962906fe128d69971221340d8aeb3360d4fbe33cceee13693c` |
| generic / debug_fixture | 586 | `f33e6c66770a825ea81a06d2228ba3b08027b958da07693a8afac2a566b45284` |
| LG / debug_fixture | 626 | `e3459339d63a63ae9aa71cdc01a4dd18cb6e2b079f86968da507ac57ff133815` |
| generic / timebase_fixture | 1,807 | `15072f87b5fe06ff56c2e303704aeefb2d6f5c87aff20dd132ea0699d29a478f` |
| LG / timebase_fixture | 1,847 | `cd64743bc5095a37711885236fa65367dc375a57a509b322f6d5678ffdf954ef` |

**Manual evidence and remaining gates:** the dedicated board fixture now has
the bounded LG normal, pending-cancel, late-source and separately reset recovery
record linked above. It verified physical CODE before resume, compiled calls
in both directions, CMD/STA, original failure/rollback results and the documented
invariants. This isolated executable is still not a board image. New hardware
work requires its own board/image/recovery authorization; generic hardware,
frequency/calibration and the other M2 gates remain unobserved.
Source confirmation is not LF calibration completion or tick precision.
Oscillator-failure injection needs its own safe authorized setup; host/simulator
timeouts are not that experiment. Do not access hardware, alter the installed
LG image, reset/flash or run USB/debugger tools through ordinary tests.
No LF switching, calibration service, wake/IRQ/compare, RF, AES or flash-service
acceptance follows from this work.

## M2 clock board fixture automated coverage

The separate `IMAGE=clock_fixture` runs the corrected clock and unchanged
timebase C drivers, with **host-tested, image-checked and simulated** fixture
coverage on both boards. The manual runner has synthetic host coverage and
separate [2026-09-17 (UTC+03) LG compiled-C hardware acceptance](DEBUGGING.md#2026-09-17-lg-compiled-c-clock-acceptance).
The old LG negative test exposed the pending-cancellation bug; it is not
reclassified as success by the corrected-image record. See the
[56-byte ABI, exact checkpoint proof, footprint, hashes and manual procedure](DEBUGGING.md#init-time-clock-board-fixture).
M2 #4 remains open. The six older board BIN hashes in the preceding table and
all historical LG M1/timebase records remain unchanged.

The real-driver host model covers all 16 LF/TICKSPD command combinations
through **771 successful steps each**, including source/stage progression,
byte-counter/heartbeat wrap, explicit serialization and synthetic 24-bit
counter rollover. Every ST0/ST1/ST2 triplet is ordered and latched while the
model ticks between byte reads. Each read/write log entry is checked before
consumption; existing 32-entry capacity and overflow assertions are unchanged.
Stopped, backward and ambiguous counter samples, late timeout, confirmed and
failed rollback, IRQ/MODE/initial-state rejection, post-call invariant errors
and unchanged latched faults are covered. No host callback enters firmware.

Both actual linked board images execute **771 steps** under alias-aware s51,
with original startup/M0/GPIO policy, correct stage results and stack unwind.
Eight terminal driver scenarios cover the verified pre-request deadline halt,
backward/ambiguous observations, 4,096-poll stopped-clock failure and an
independently exhausted 4,096-poll rollback. The simulated deadline halt proves
the stored deadline, live DPL=0/DPS=0, actual caller return address and unchanged
CMD before request execution. Additional cases check `C9 -> 89 -> C9` after
cancellation without accepting the first old match, never-departed uncertainty,
and the separate post-request hold/C-observed-source checkpoint before a late
timestamp. Tick/STA injection is **synthetic**, not analogue
oscillator startup, a calibrated counter or physical failure injection.
Terminal fault-loop CPU/RAM immutability, complete records, counter/heartbeat,
SFR, nonallocated XDATA and upper-stack guards remain checked.

The board image verifier uses actual IHX/map/CDB, not a module listing that a
later standalone test link can overwrite. It shares the clock instruction/ABI
and relocated reader checks without weakening the original standalone
`clock_test.ihx` listing contract or `timebase_test.ihx` 88-byte reader contract.
Negative checks mutate every byte of the complete 148-byte deadline helper,
its caller-to-request continuation, the 127-byte source-observation-to-sample
continuation, post-request instruction, fixture checkpoints and relocated reader;
they also reject extra helper calls, incorrect explicit CDB ends, overlay
bounds, SFRs, source/field layouts and all common image/memory/alias violations.
The helper RET is shared with an error path: opcode/tail checks alone never
replace the live success-return and caller checks.

The clock-fixture slice brought the offline Python suite to **345 tests**, retaining the prior 327
regressions and adding 18 clock record/CLI/runner tests. They cover complete
normal sequences and wrap, full CODE verification before resume, permissions,
authorization/artifact gates, deadline/caller checks, breakpoint disable before
hold, every composed operation failure in all three modes, malformed records,
unexpected PC/FAULT, register/NOP preservation and cleanup failure. A host hold
alone, wrong returned error, late/failed rollback or missing elapsed evidence
must not pass negative acceptance. In particular, the reported driver/fixture
STA mismatch and bounded rollback uncertainty still fail acceptance. A
synthetic backend also verifies the real guarded hold's shared deadline,
terminal oversleep failure and denied resume.
No test loads a physical USB backend.

The two new images preserve the 512-byte reservation budget, ordinary allocation
below `0x1E00`, full M0 reservation and IRAM alias. They do not enable RF,
interrupts, sleep, LF switching or a calibration service. The revised LG physical
normal/timeout/recovery observations are separate from these automated checks.
Measured frequency, calibration, oscillator-failure/stopped-clock and power-cut
experiments, generic hardware and other M2 services remain outside that acceptance.

All **eight local board/image `all test` configurations passed**
after the cancellation fix, each including the 345-test pinned Python suite
and the existing standalone clock/timebase/codec checks. All six earlier BIN lengths and full SHA-256 values
were asserted unchanged; their maps still exclude `_clock_select_init`.
The two new BIN hashes match the linked clock fixture record. Repository/local
links and whitespace guardrails passed. This is offline validation, not a new
CI publication or hardware result.

### 2026-09-17 bounded LG clock hardware evidence

The parent additionally reports successful full LG offline checks and physical
acceptance of corrected SHA-256
`77f7142d1e4ef3a662ce8ffd7ce9803d110a80e867b1a55be5540b98d16867ca`
(3,798 bytes). All physical CODE was independently checked before CPU resume.
The hardware runs are dated **September 17, UTC+03**, distinct from the old
`c41dae85...` image's **September 16 23:24** failure.

The original pending-timeout test passed unchanged: original TIMEOUT 3,
14,770 raw request ticks / 1 poll, helper 0, then confirmed rollback 0 after
64 ticks / 15 polls, with both driver and immediate fixture snapshots `C9/C9`.
The separate late-source test passed at the real C evidence checkpoint:
TIMEOUT 3 after 27,850 ticks / 1 poll, rollback 0 after 3 ticks / 1 poll, both
snapshots `C9/C9`. Both preserved terminal FAULT `0x016C` and the original
failure. A separate explicit reset/recovery run then passed 257 full sequences
/ 771 C calls, all result 0 and rollback NOT_ATTEMPTED 8: RC idempotence
0 ticks / 0 polls, XOSC 11..13 ticks / 3 polls, RC return 2..3 ticks / 1 poll.
Counter/heartbeat wraps and M0/LF/TICKSPD/SLEEPCMD/IRQ/CPU-inspection invariants
passed; final READY was `0x016A`, RC16.

The [full dated record](DEBUGGING.md#2026-09-17-lg-compiled-c-clock-acceptance)
retains the exact scope, operator-local timeline and evidence separation.
No frequency/calibration/LF-calibration-completion, physical oscillator absence
or stopped-clock, power-cut, IRQ/wake/RF/AES/flash or natural 24-bit timer-wrap
claim follows. Never-observed departure remains uncertainty 9, not confirmed
cancellation; that path and other injected faults retain host/simulator evidence.
Generic remains host/image/simulator-only. Full M2 #4 stays open.

## M2 interrupt ownership automated coverage

`make test-irq` checks the isolated
[EA ownership API](ARCHITECTURE.md#interrupt-ownership-foundation-isolated-m2-slice).
Its evidence is **host-tested, image-checked and simulated only**.
`irq_test.ihx` is not a board image and must never be flashed or uploaded.
The existing eight board images and CI matrix are unchanged, and the board
verifier rejects accidental linkage of either primitive.

Host coverage checks all 65,536 IEN0/other-enable-byte combinations with
eight-deep LIFO nesting, all 256 token representations against all 256 IEN0
values, initially enabled/disabled entry and preservation of current lower
enable bits even if the caller changes them inside a section. Invalid byte
tokens preserve input guards and every modeled register with zero MMIO log
entries. All unrelated modeled registers, log addresses, order, counts and
values are checked without relaxing overflow assertions. Reserved IEN0 bit 6
is included as synthetic byte fuzzing, not a realizable CC2530 read value.

The linked verifier rejects changes to every primitive instruction, its EA
operand, DPL byte ABI, explicit naked-function ends, module allocations,
result/control-byte declarations, ISR vectors and complete compiler-generated
context-save/restore/RETI sequences. It checks the actual IHX/map/CDB/REL,
not source-line guesses. Only the exact SDCC vector holes `0x06..0x0A` and
`0x0C..0x12` are allowed; extra/missing CODE bytes are rejected. Existing
clock/timebase tests retain their contiguous-CODE and instruction contracts.

The C self-test covers every enable byte and eight bounded token values.
Thirty leaf instruction-boundary cases execute the actual primitives with
synthetic pending requests, including invalid restore while EA remains set.
Additional tests prove pending assertion during EA=0, no delivery after
inner restore(0), higher-priority preemption versus equal-priority deferral,
and calls through the same primitives in foreground/low/high handlers.
Actual RETI resumes the interrupted restore between SETB EA and its return,
with live DPL preserved. Complete interrupted SFR/IRAM/live-stack context,
counter/results, subsequent interrupt delivery, nonallocated XDATA and upper
128-byte stack guards are checked. Pending-flag acknowledgment is performed
by the C52 CPU model, never by the primitive. A required failing no-alias
control retains the CC2530 `0x1F00..0x1FFF` IRAM-alias contract.

The leaves use **34 CODE bytes and zero private DATA/XDATA/BIT/overlay
scratch**, with no explicit stack pushes. The whole isolated executable has
971 emitted CODE bytes / 983 reserved including twelve vector-padding bytes,
6 ordinary XDATA plus the 8-byte `IRQT` result at `0x1E00`: 14 used / 70
reserved nonaliased XDATA, counting the full 64-byte M0 reservation.
Stack starts at `0x22`, with 222 bytes reserved; ordinary allocation remains
below `0x1E00` and total reservation below 512. The test's own compiler data,
bit-register bank and real ISR frames are not private primitive scratch.

The pinned Python suite has **346 tests**, retaining all 345 earlier tests.
The new rejection test enforces separation from board firmware. Full generic
clock-fixture `all test` includes the existing 40 linked clock scenarios,
timebase/codec checks and the new IRQ checks. Eight board rebuilds and
alias-aware board simulations retain the six earlier hashes listed above,
plus generic clock fixture (3,758 bytes)
`13bec2214263cfab52513cb5744fe971a7a254b13e674799410b5d3f7693c462`
and LG clock fixture (3,798 bytes)
`77f7142d1e4ef3a662ce8ffd7ce9803d110a80e867b1a55be5540b98d16867ca`.
No board map contains either new primitive; historical LG evidence is unchanged.

**Separate next hardware gate:** authorize a dedicated non-RF board fixture
with a documented safe CC2530 source, real vector/priority/pending-clear
semantics, bounded stimulus and recovery conditions. Independently verify
all physical CODE before resume, then observe pending assertion while EA=0,
deferred delivery through nested sections, exact previous-state restoration,
ABI-preserving ISR/RETI and continued service, with unrelated state preserved.
That fixture/runner does not exist in this slice. Generic C52 IE/IP/TCON
injection cannot establish CC2530 delivery, peripheral flag races or timing.
No hardware operation was performed; M2 #4 and physical IRQ acceptance stay open.

## Independent Sleep Timer hardware reference (2026-09-16)

This manual observation used the existing LG Rev0.3 M1 fixture, not
`timebase_test.ihx` and not the new C driver. The fixture remained the verified
626-byte image with SHA-256
`e3459339d63a63ae9aa71cdc01a4dd18cb6e2b079f86968da507ac57ff133815`.
The host used pinned PyUSB 1.3.1 and the same CC Debugger/host setup recorded
in the M1 evidence.

After explicit reset and physical CODE verification, the fixture ran to its
known NOP with iteration/heartbeat 2. While the CPU remained halted, original
supplied debug instructions read ST0, ST1 and ST2 in order, with register
save/restore and the normal transfer deadlines. The complete known fixture
record was checked between samples to detect reset or unexpected execution.
No counter, compare, GPIO or clock-source register was written by sampling.

| Observation | Result |
| --- | --- |
| Short progression check | 8,236 raw ticks between samples; host interval bounded by 252.568..292.371 ms |
| Natural rollover observation | One 24-bit rollover during 555.699 seconds of host observation, across 528 samples |
| Adjacent samples across rollover | Positive modulo-24-bit delta of 31,736 ticks, strictly below half-range |
| Context and reset exclusion | CPU registers and complete M1 iteration-2 canary unchanged |
| Clock identification | Same-epoch M0 startup `CLKCONCMD=CLKCONSTA=C9`: 32-kHz RC source, 16-MHz RC system source |

This establishes **hardware-observed counter progression and natural
rollover for that debug-controlled register sequence**. The observation did
not start exactly at counter zero, and host elapsed time includes USB/debug
overhead: it is not a calibrated counter period, frequency/accuracy claim,
or execution-time measurement of `timebase_read_awake_ticks24()`.
It does not prove C-driver integration, interrupt concurrency, clock switching,
PM wake synchronization or any other M2 service. Raw records remain private;
the evidence here is a processed summary without unique identities.

## Future protocol tests

The [standalone legacy MAC codec](MAC.md) is already covered by original
golden/layout/negative vectors on host and an isolated SDCC/uCsim image,
plus all 65,536 FCF values and exact-sized buffer cases on the host. This is
syntax/serialization evidence, not CRC, authentication, radio or MAC operation.
Five command payload formats and all twelve supported command addressing
layouts have shared host/SDCC golden and boundary checks. Host-only tests
also exhaust command fields, association response addresses/statuses and
command FCF patterns, retaining unchanged-output checks on errors.
Its test executable is not a board firmware image or CI upload artifact.

| Area | Required cases before the corresponding milestone closes |
| --- | --- |
| Encoders/parsers | Golden wire bytes, every boundary length, invalid/truncated/reserved fields, explicit byte order |
| MAC | ACK/no-ACK, retry/backoff, filtering, RX overflow, association failure, queue pressure |
| Security | Public vectors, wrong key/MIC/nonce, replay, key sequence changes, authenticated error handling |
| Persistence | Reset at every write boundary, corrupt records, generation wrap, counter reservation and wear |
| NWK/APS | Join failure/success, rejoin, changed parent, leave, transaction timeout, duplicate handling |
| ZDO/ZCL | Interview, mandatory responses, unsupported-service fallback, full Mgmt Leave/owned-source Mgmt Bind, omitted Mgmt LQI, bind/unbind, read/report/configuration |
| Sleep | Pending downlink, fast polling, expired parent state, restart, key update and wake deadlines |
| Application | Missing sensor, stuck I2C, stuck BUSY, display timeout, concurrent radio/reporting |

The M6 application uses explicitly labeled, deterministic synthetic
measurements to test transport/reporting before a physical sensor is selected.
That evidence must not be described as sensor accuracy or battery measurement.
Physical sources and their errors/calibration enter at M8.

Persisted-resume cases include known and unknown parent information, immediate
keepalive selection/renegotiation, an absent parent and bounded recovery.

Test vectors committed to the repository are synthetic or publicly licensed.
Do not use real network keys, install codes, device factory records or raw
personal captures as convenient fixtures.

## Hardware progression

1. Non-RF debugger fixture, using the exact board profile.
2. Isolated radio/MAC experiments with an independent sniffer.
3. Authenticated receiver-on ED with a test coordinator.
4. Interview/reporting/restart/leave and parent-loss scenarios.
5. SED behavior and measurements with debugger influence separated.
6. Concurrent sensor/display operation.

Before flashing, preserve and verify recovery backups and confirm the exact
target/image. Never reconnect a display flex under power. Firmware must not
guess voltage/LUT settings for an unidentified panel.

A hardware record should name the board revision, panel/sensor if relevant,
firmware commit/hash, toolchain, test scenario, expected/observed result and
limitations. Keep unique identifiers, keys, private captures and raw dumps
outside public Git and Actions artifacts.

## Interoperability matrix

The following are **planned test slots, not claims of support**:

| Environment | Awake ED | SED | Restart/rejoin | Bind/report/leave |
| --- | --- | --- | --- | --- |
| Zigbee2MQTT with a documented adapter/firmware | Not tested | Not tested | Not tested | Not tested |
| ZHA with a documented adapter/firmware | Not tested | Not tested | Not tested | Not tested |
| Additional independent coordinator stack | Not tested | Not tested | Not tested | Not tested |

Track software and adapter versions in actual test records. Testing two user
interfaces with the same coordinator stack is useful, but is not the same as
testing two independent radio/stack implementations.

For M7, record at least 12-hour and 72-hour continuous runs and 100 controlled
restart/rejoin cycles. Record polling/reporting rates, current limits, allowed
latencies and timing margins before the run; do not invent pass limits after
seeing measurements.

## CI and release boundary

Hosted CI builds/tests without physical devices or repository secrets.
The CI matrix covers four non-RF images (`bringup`, `debug_fixture`,
`timebase_fixture`, `clock_fixture`) on both boards: eight jobs. Artifacts contain only the
explicitly selected board image's generated firmware, symbols and build
metadata. Pull requests must not use privileged `pull_request_target` execution
to build untrusted source.
The artifact-path whitelist is tested; neither standalone component executable
nor host test binaries/logs are uploaded.

CI success means the declared automated checks passed. Experimental releases
must separately list their completed milestones, known limitations, exact
build inputs and hardware/interoperability evidence.
