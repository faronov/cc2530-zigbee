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

## Local validation performance

`make test-local` now covers all twenty-two board/image checks while running
the Python tool suite once and each standalone component corpus once per
board definition instead of once per image. It uses isolated component and
board-image directories and serial, fail-fast submakes. The existing full
`make ... all test` and twenty-two-job CI coverage remain available.
The measurements below predate the flash fixture and retain their original
twenty-image scope; they are not timings for the expanded matrix.
Dry-run regressions account for every component, board/image, fixture host
test and tool-suite invocation; synthetic submake failures prove that later
suites are not run after a failure. The AES board test declares its host
reference executable directly: it must not depend on a preceding standalone
AES run to populate its directory. The first aggregate run exposed this
previously implicit dependency; a separate regression checks both boards'
standalone `test-board` command ordering and reference path.

The 2026-09-18 measurements on the same Xeon E5-2697 v2 host compared the
`3c3e133` baseline with the optimized checkers:

| Corpus | Before | After | Measurement |
| --- | ---: | ---: | --- |
| Complete Python discovery | 205.63 s | 79.58 s | `unittest` elapsed time without a profiler; 460/472 tests, 19 explicit skips each |
| Complete Python discovery | 270.69 s | 118.45 s | Wall time with `cProfile` on both runs; 460/469 tests, 19 explicit skips each |
| All 188 linked AES scenarios and image rejections | 85.71 s | 58.99 s | Serial wall time without a profiler, identical linked image |

The unprofiled Python measurement includes twelve added regression tests;
the earlier profiled run preceded the three Make orchestration tests. The
subsequent AES dependency regression is additional. Profiling overhead is
significant; the profiled tool-suite times are not normal invocation latency,
nor is any row a measured speedup of the entire twenty-image matrix.

The savings do not come from shortened PRNG cycles, sampled CODE mutations
or skipped I/O-failure boundaries. A one-entry cache holds only immutable
**synthetic test-backend** register bytes; the real runner still reads and
validates every observation. Fake wire-byte updates are compared against the
decoded reference for all 4,112 records, and changed/invalid inputs cannot
reuse a cached result. Diagnostic strings are formatted only on failure.
Exact CODE serialization caches only expected address tuples, never input
images or validation results, and accepts reordered mappings while rejecting
holes, extra bytes and all original byte mutations. Accepted images still
receive the complete independent layout, ABI and MMIO proofs.

Simulator output remains complete, with command-error, nonzero-exit and
partial-output/timeout regression coverage. No 15-second deadline, alias,
upper-IRAM, stack, poll-limit or retained-fault check is relaxed. These changes
provide **host-tested, image-checked and simulated** evidence only; they
perform no USB/RF operation or new hardware acceptance.

## Reserved flash reader coverage

`make test-flash` exercises only the new reader. The host corpus checks
130,880 calls: both reserved pages and every in-page start, all chip-ID/
information/controller bytes, all XBANK/cache combinations on both clocks,
the full 16-bit caller-address/range domain, ignored mapping writes and
faults at every controller observation. Output guards/tails and retained
faults are checked without enlarging the bounded host-MMIO logs.

The 1,020-byte SDCC image uses 93 bytes of ordinary XDATA plus the unchanged
64-byte status reservation; observed peak SP is `18`, below upper IRAM.
Its 37 linked cases verify the exact emitted SFR accesses and read-only
peripheral loads, typed ABI and entire private allocation prefix. Every CODE
byte mutation is rejected; source/page bounds, actual MOVX addresses with
XBANK7, successful restoration, late errors/nonpublication, faulted re-entry,
unallocated XDATA and alias/stack guards are exercised. Board-image checks reject the test harness from every `IMAGE` and permit
the real services only in the separately selected `flash_fixture`.

This is **host-tested, image-checked and simulated** evidence, not hardware
flash-read, erase/program, endurance, electrical interruption or persistent
record evidence. Synthetic BUSY rejection does not prove a real CPU can
execute from flash while its controller is busy.

## Internal RAM flash executor coverage

`make test-flash-exec` is focused offline coverage for the internal command
engine, not the future public writer. The native corpus exercises 131,815
calls: every reserved word address, both erase pages, clock/cache/XBANK
combinations, full 16-bit argument/source-boundary domains, identity and
ownership states, command rejection/ABORT/controller errors, mapping/address
readback failures, all four ownership observations and retained failures.
Its callback is explicitly a C controller/assembly model, not production
execution or successful flash emulation; modeled fail-stop uses a nonlocal
test escape rather than returning to the wrapper.

The 1,451-byte linked image has SHA-256
`1565921ea42f0222f67a9bf9bc72865d1dfe98a112ab4616a4c2668677163926`,
168 ordinary XDATA bytes plus64 reserved, and observed peak SP `2B`.
The verifier checks every CODE byte, matching relocated instructions,
typed ABI, compiler-private allocation, exact peripheral access sites,
physical copy/readback and all relative RAM branch destinations.
The 123-byte template has no calls, absolute jumps or CODE loads.
The four FWDATA stores are actual consecutive register/MOVX pairs.
The 42-clock instruction-table count is **best-case**, not hardware timing.

The 181 linked scenarios execute genuine copied instructions at
`8009..8083` through synthetic CODE-to-RAM decoders, introduced only after
the real MEMCTR write. Ignored-map rejection never gets that decoder.
They inspect actual command/address/ordered data writes, the genuine common
return PC `04B3`, idle-before-RET and restored mapping. First/last permitted
completion, rejected commands with active status, late ownership changes,
ABORT, FULL/cache mismatches and failed mapping restoration are covered.
Every copied RAM byte is individually corrupted before C's physical
readback and must prevent command execution.

Stuck BUSY and WRITE-without-BUSY exercise limits1/255/256/257/65,535.
A simulator FCTL-read breakpoint counts the exact requested polls and
observes one remaining count before the final decrement. The exhausted
engine preserves result7, final controller status, XMAP and stack frame in
its RAM self-loop, including after a synthetic later-idle event. Faulted
re-entry must reach no peripheral access. Unallocated/status XDATA,
information/reserved-page windows, upper IRAM and alias guards remain strict.
The Make inventory and all-board exclusion checks cover the new corpus.

This is **host-tested, image-checked and simulated**, not hardware-observed.
The emulator supplies status transitions, not a physical bank mux or flash
write/erase effects. No verified NV operation, write-history policy, wear,
power-cut recovery, board fixture or completed M2 gate is claimed.

## Reserved-page writer coverage

`make test-flash-write` checks the genuine C policy, existing reader and RAM
executor together. The native controller/window model exercises145,481 calls:
all1,024 word slots, repeated and allFF attempts, all128 history bytes,
separate page epochs, startup with erased-looking but unknown history,
full16-bit invalid offset/caller-boundary domains, every byte of each page
as an erase-verification mismatch, partial/missing program effects,
ignored/aborted/stuck commands and retained pre/post-read failures.
No synthetic model result substitutes for the real C policy or reader.

The exact3,345-byte image has SHA-256
`278d332847ba181a345c261f5fc0451ef17a3f2705ba578741c7a772c6af711a`.
Both board definitions produce identical bytes. It uses417 ordinary XDATA
plus64 reserved, with peak SP `36`. The entire404-byte service/compiler
prefix is below caller input and the512-byte reservation budget is unchanged.
Every CODE mutation, ABI/allocation mutation and relocated-listing mismatch
is rejected. The link snapshots all three component listings separately,
so later standalone/fixture links cannot silently substitute a different
relocation. The policy module has no direct peripheral access or runtime
helper fallback; its actual calls enter the reader and executor.

The38 linked sequences cover all128 bitmap-byte positions and all eight
bit positions, the final word of both pages, used/allFF words, per-page erase
isolation and genuine startup reinitialization with unchanged flash-window
contents. One sequence traces every actual MOVX source address across the
whole2-KiB erased page and subsequent programmed words. Before RAM command
entry, the emitted code must already have invalidated erase history or
consumed the word-attempt bit. Actual FCTL/FADDR/FWDATA writes, staged inputs,
common-C return frame and idle-before-RET are checked again in the composed
call chain, rather than replacing the executor with a success callback.

Partial/missing effects must fail verification; ignored erasure of an
already-allFF page must not establish history. Faulted re-entry reaches no
MMIO. Stuck program and erase preserve PENDING/COMMAND, history changes,
XMAP and the RAM fail-stop even after a later synthetic idle event.
Unallocated/status XDATA, upper IRAM, information-page and neighboring flash
guards remain intact. Native model extraction also preserves all131,815
executor checks; the reader's1,020-byte/37-case and executor's1,451-byte/
181-case standalone checks remain unchanged.

The full MOVX transcript is indexed once before its existing strict section
checks; no bytes, cases,15-second per-process bound or diagnostics are
discarded. The38 linked sequences completed in20.466 seconds locally.
This is **host-tested, image-checked and simulated**, not hardware-observed
flash behavior, lifetime wear or a durable/atomic NV record. Hardware #8
still requires separate destructive-operation authorization and recovery.

## Boot-disarmed flash fixture coverage

The separately selected `flash_fixture` is checked for both board definitions,
using the [same published image/layout contract](FLASH_FIXTURE.md#image-abi-and-allocation).
Focused commands, not the unrelated full board/component matrix:

```sh
make BOARD=generic IMAGE=flash_fixture test-flash-fixture
make BOARD=lg_esl29_rev03 IMAGE=flash_fixture test-flash-fixture
make BOARD=generic BUILD=build/flash-components-generic test-flash test-flash-exec test-flash-write
make BOARD=lg_esl29_rev03 BUILD=build/flash-components-lg test-flash test-flash-exec test-flash-write
PYTHONPATH=tools python3 -B -m unittest test_flash_fixture test_m0_artifacts test_local_checks test_timebase_fixture test_m1_access test_m1_image -q
```

Each fixture's653 native cases exercise both complete sequences,255/256
admission boundaries, every one-bit packet corruption in both phases, all256
page bytes, premature/replayed/scope-changing packets, reset with unchanged
flash contents, frozen terminal calls, ignored/aborted/stuck commands,
missing/partial program effects, erase residue and explicit history failure.
They reuse the original native controller/window/assembly model, never a
production stub. Existing board startup and strict native warnings remain.

Each board's32 linked scenarios execute genuine SDCC C and the actual
123 copied instructions. They prove default/reset disarming, full256-poll
timeouts, acceptance after255 polls, malformed packet paths with breakpoints
on **all** service IO sites, terminal no-retry, both selected pages and exact
one-erase/two-program command/address/data traces. Each accepted erase traces
all2,048 actual MOVX verification addresses; program/preflight traces remain
strict. Transcript sections are indexed once, not quadratically rescanned.
RAM entry checks physical C-copy bytes,65,535 poll input, consumed history,
genuine common-C return frame, FCTL/FADDR and ordered FWDATA operands.
Idle-before-RET is required; RAM exhaustion leaves PENDING/COMMAND/RAM_STOP,
XMAP and unchanged CPU/RAM even after synthetic late idle.

Whole-CODE mutations, symbols/typed ABI/field offsets/private compiler layout
and saved relocated listings are rejected. Services are byte-identical to #7;
their181 executor/38 writer linked sequences retain the independent finite
poll/ABI/alias proofs. Per-image snapshots now also protect the standalone
reader/executor listings; a genuine serial shared-directory relink regression
proves later component links cannot overwrite fixture evidence.
Full ordinary/unallocated/status XDATA, upper IRAM, neighboring page boundaries,
information/peripheral windows and M0 heartbeat are checked. Both fixture
images reserve500 XDATA bytes within512 and have peak SP`36`.
Every s51 process retains the15-second deadline.

This is **host-tested, image-checked and simulated only**. Synthetic status,
XMAP and flash-window effects are not physical flash physics,20-us timing,
endurance, power interruption, preservation of real excluded regions or
recovery. Both hardware gates and #8 remain open; no device/private data access
was performed. CI has22 board jobs and the unchanged seven-path board-artifact
whitelist, with `hardware_tested=false`. Standalone executables, snapshots,
private recovery files and captures are not uploaded as board artifacts.

## Bounded radio queue composition coverage

`make test-radio-queue` covers the [queue and IRQ ownership contract](RADIO_QUEUE.md)
with 69,108 counted native memory/IRQ operations plus real-RX composition.
The 7,189-byte SDCC image uses 818 ordinary XDATA +64 reserved, within its new
integrated 1-KiB budget; observed SP is `50`, below the preserved upper-IRAM
guard. Existing component/protocol budgets remain unchanged.

The actual linked receiver/queue corpus performs 24 composed operations and
11,031 MMIO events, 676 additional memory-only operations and 181 genuine
generic C52 preemption cases. It checks all request/packet capacities,
cursor wrap, saturation, preserved output tails, copy ownership,
disconnected-consumer pressure, BAD_CRC reuse and retained driver failures.
The producer's only calls are the verified reentrant IRQ leaves; it has no
static compiler scratch or packet/radio/timebase access. Snapshots retain
their pre-ISR capture even when restore admits an interrupt before return.

The whole image, complete compiler-private metadata and matching relocated
listings are pinned; negative CODE/ABI/allocation cases, real RFD/timebase/
FSCAL1 rules, full memory accounting, alias and CPU context/unwind remain
strict. Five stateful vectors come from the existing native receiver model,
not a successful stub for `radio_rx_receive_init`. No CC2530 IRQ delivery,
physical RF, TX/ACK, networking, continuous-lossless reception or complete
stack fit is established. `radio_queue_test.ihx` is never a board image,
flash input or CI artifact.

## Bounded TX/CCA composition coverage

`make test-radio-tx` runs 7,078 native calls and 51 genuine linked sequences:
173 calls and 45,538 MMIO events against the real FIFO/timebase/TX code.
Both board definitions have identical 8,526-byte CODE, 374 ordinary XDATA
+64 reserved, stack start `59` and observed MMIO peak `6C`. The 512-byte
reservation, upper-IRAM/alias guards and 15-second simulator deadline are
unchanged. Per-image snapshots retain all four relocated listings.

Coverage includes every channel/body length, power/mode rejection, stale or
missing TXDONE, busy-at-strobe, calibration/completion/shutdown stalls, all
seven RFERR bits, overflow and PHR mismatch, wrap/backward/ambiguous ticks,
maximum poll cap and retained failures. Every CODE byte mutation rejects;
private ABI/allocation, actual RFD/MOVX operands, publication and status/
unallocated memory are checked. The unchanged FIFO corpus (69,895 native
cases/99 linked scenarios) and timebase checks also passed for both boards.

The model is shared by host and simulator, not independent RF evidence.
No frame capture, calibrated power/timing, TX board fixture, same-reset
legacy RX/TX handoff or queue/controller integration is established.
All board images reject the new module and standalone harness. The
[future physical procedure](RADIO_TX.md#separately-authorized-future-rf-procedure)
remains separately authorized and is never run by Make/CI.

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
shared startup/status is limited to the LG M1, timebase, clock and IRQ
fixtures recorded below, not a generic-board or standalone-M0 acceptance claim.

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
symbols and non-executable breakpoint targets. The M1 runner's host-only mocks
also reject an empty, truncated, extended or same-length replaced BIN after
artifact validation, before USB loading. Direct `exercise` calls reject wrong
image/extent/type/cycle inputs without any debugger call; valid length/cycle
endpoints reach a deliberately failing synthetic first observation. These
checks add no hardware evidence.
Synthetic parameter vectors
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
The foundation preserved the eight older board images and their CI matrix;
the board verifier rejects linkage of either primitive into those images.
The subsequent IRQ board fixture is a separate scope below.

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

The foundation brought the pinned Python suite to **346 tests**, retaining all 345 earlier tests.
The new rejection test enforces separation from board firmware. Full generic
clock-fixture `all test` includes the existing 40 linked clock scenarios,
timebase/codec checks and the new IRQ checks. Eight board rebuilds and
alias-aware board simulations retain the six earlier hashes listed above,
plus generic clock fixture (3,758 bytes)
`13bec2214263cfab52513cb5744fe971a7a254b13e674799410b5d3f7693c462`
and LG clock fixture (3,798 bytes)
`77f7142d1e4ef3a662ce8ffd7ce9803d110a80e867b1a55be5540b98d16867ca`.
No board map contains either new primitive; historical LG evidence is unchanged.

**Hardware gate defined by the foundation:** authorize a dedicated non-RF board fixture
with a documented safe CC2530 source, real vector/priority/pending-clear
semantics, bounded stimulus and recovery conditions. Independently verify
all physical CODE before resume, then observe pending assertion while EA=0,
deferred delivery through nested sections, exact previous-state restoration,
ABI-preserving ISR/RETI and continued service, with unrelated state preserved.
That fixture/runner was not part of the foundation; the next slice below
implements it, with subsequent bounded LG acceptance recorded separately.
Generic C52 IE/IP/TCON
injection cannot establish CC2530 delivery, peripheral flag races or timing.
No hardware operation was performed by those automated checks; M2 #4 remains open.

## M2 Timer1 IRQ board fixture automated coverage

Both `IMAGE=irq_fixture` configurations passed full `all test` with pinned
Python: **355 tests**, retaining all 346 prior regressions, plus host C,
linked-image and alias-aware checks. The 34-byte EA leaves and separate
native C52 interrupt/preemption tests are unchanged. All eight older images
were rebuilt, image-checked and simulated; their complete BIN lengths and
SHA-256 values above remain identical. The new
[footprints, hashes and manual proof](DEBUGGING.md#irq-checkpoints-and-footprint)
do not replace any historical LG hardware record.

Host tests run 257 successful cycles through the real primitives and C ISR
body, verify every read/write log before consumption, model H0/R/W0,
all 31 newly asserted channel-flag combinations at acknowledgment, stopped
time/caps, late/backward/ambiguous samples, lost delivery, entry/phase rejection,
terminal faults and byte/counter wraps. Counter reads model latching/ticking
through 00FF/0100 and FFFF/0000. The fixed FF invalid-token experiment is
separate from pending delivery; exhaustive byte-token semantics stay in the
foundation tests.

The linked checker rejects primitive/vector/ISR/RETI/checkpoint/caller,
SFR/read-order/RW0, field-layout and storage changes. It checks all peripheral
instruction operands and both read-only inactive-channel XREGs, accounts
exactly 63 compiler vector-padding holes, and preserves ordinary allocation,
full M0 reservation, alias and stack guards. No synthetic bytes are inserted
into the board image. EA bit address AF and T1STAT SFR address AF are distinct
`SBIT`/`SFR` symbol spaces, never interchangeable memory access.

s51 executes 257 real-C cycles and six terminal-fault cases per board.
Because C52 has no CC2530 Timer1 controller, the test explicitly supplies
counter/source values, constructs the hardware-style return frame and enters
the actual `004B` vector, then applies documented H0/RW0 effects around
genuine instructions. It checks ISR CPU/IRAM/live-stack preservation, real
RETI/foreground continuation, frozen counter, complete records/M0 heartbeat,
and nonallocated XDATA/upper-stack guards. This is **synthetic entry/source
modeling**, not native CC2530 interrupt delivery or priority-controller
emulation. Generic C52 priority/RETI in-service behavior remains separately
covered by `test-irq`.

The runner tests cover authorization/artifacts before backend loading, every
composed operation boundary in normal/timeout modes, all CODE before resume,
257-cycle wrap, bad context/frame/acknowledgment/RETI, malformed records,
immutable startup observations and cleanup failure. A missed induced timeout
stops at unexpected normal progress instead of running further cycles.
Success JSON follows cleanup only. No test enumerates physical USB.

The separate [dated LG acceptance](DEBUGGING.md#2026-09-17-lg-compiled-c-irq-acceptance)
now establishes bounded real Timer1 flags/vector, acknowledgment, restored EA
and ISR/RETI context on the unchanged LG image. Generic remains
host/image/simulator-only. Neither these automated tests nor that record claim
one-shot hardware, an exact count of timer overflows, calibrated latency,
physical stopped-clock faults, higher-priority hardware nesting, other
peripheral dispatch, DMA, wake, RF, AES or flash services. M2 #4 remains open.

### 2026-09-17 bounded LG Timer1 IRQ hardware evidence

The parent independently passed both IRQ `all test` configurations with
355 Python tests and strict linked/source/vector/ISR/alias/fault checks,
then supplied physical acceptance on **2026-09-17, UTC+03**, without changing
firmware, runner or assertions. Every invocation independently checked all
3,269 CODE bytes, including FF padding, against LG SHA-256
`b9bc83d7254944621f25d312f118ca6a044e808017f85bb6453f28c15cdb0ae1`
before any runner resume.

Three initial normal cycles passed. The separate deadline-hold negative run
retained FAULT `01BD`, phase 4/reason 6/stage 1, zero completed/ISR count,
14,718 raw pending ticks / 1 poll / helper 0, with IEN0/IEN1/T1CTL and
source/CPU flags all zero. That is a real returned timeout and terminal
cleanup, not physical oscillator-stop injection; recovery was not implicit.

The separately reset full run passed **257 actual C cycles/Timer1 services**:
133..134 pending raw ticks / exactly 29 polls, 1..2 delivery ticks / 1 poll,
ISR source `20` and CPU flags `00`. Counter `0B3A` was frozen at PENDING,
INNER and READY. All hardware return PCs were **`0C9D`, restore+12 at RET
with DPL already OK=0**, and `interrupted_restore=true`; full CPU/active-IRAM/
stack preservation and an actual RETI two-byte pop/resume passed each time.
The synthetic +9/live-token-1 case and higher-priority nesting were not
hardware-observed by these runs.

Cycle/ISR/M0-heartbeat bytes wrapped; final completed and ISR counts were
both 1. Immutable M0/initial observations, priorities, unrelated IRCON/TIMIF,
tokens and disable/stop
conditions passed. That IRQ run ended at **READY `01BB`, EA/T1IE off,
Timer1 stopped**; old clock/M1/timebase records remain historical. All eight
older BIN hashes and the published EA/timebase drivers are unchanged.
The full run's 226.77-second duration is host wall time only. See the
[canonical timeline and scope](DEBUGGING.md#2026-09-17-lg-compiled-c-irq-acceptance);
no calibrated time/latency, exact overflow count, true one-shot, other IRQ/
DMA/sleep/RF/AES/flash or new authorization follows. Generic hardware and
broader M2 #4 gates remain open.

## M2 DMA debug gate coverage

The parent's **271-test M1 regression run passed**, including **23 new tests**
for the [API-only reset-scoped DMA-enable gate](DEBUGGING.md#guarded-dma-enable-after-reset).
Synthetic coverage includes both own-reset paths, separate/default boolean
permissions, eligibility consumption and contradictory passive observations,
all 256 pre/post configuration/status values, all eight FMAP values and
change rejection, exact write-only framing with no unsolicited read,
malformed/short/late transfers and faults at every I/O boundary. Lifecycle
and reentrant exclusion prevent post-fault I/O, retries, resets or restoration.
The shared `_write_packet` exact-count/post-write-deadline checks add no
target I/O to existing exchanges.

The [canonical 2026-09-17 LG record](DEBUGGING.md#2026-09-17-lg-dma-enable-gate-acceptance)
supplies separate **hardware-observed gate evidence**: three full-CODE-verified
native cycles, a real accepted write followed by an injected late host return,
independent read-only observation and explicit reset/FIFO recovery.
Accepted effect is not API confirmation. Neither DMA registers/transfers nor
AES were exercised; the negative run was not a physical USB stall or DMA-stuck
test. Raw records remain private and no new hardware authorization follows.

## M2 isolated DMA copy coverage

`make test-dma` runs the original
[channel-0 RAM-copy service](ARCHITECTURE.md#isolated-channel-0-dma-copy)
as a standalone executable. **Never flash `dma_test.ihx` or upload
it as board firmware.** AES CPU sequencing remains unresolved; this is not an
AES API, peripheral-trigger backend, hardware fixture or radio-gate closure.

**Host-tested:** 194,819 cases cover all 16-bit source/destination addresses
at maximum length, diagnostic ranges, protected descriptor/parameter/helper
storage, overlap, all lengths 1..16 and all 256 byte patterns, supported and
unsupported clock fields, precise descriptor/control order, request/arm/IRQ
changes and owned acknowledgment races. The independent reference copy
checks the whole synthetic RAM, including untouched tails. Tests consume and
verify the existing 32-entry MMIO logs rather than expanding or blindly
resetting them. The model enforces the nine-clock fetch interval and
demonstrates a lost early request; ARM alone is not readiness.

Timeout at each phase, completed-but-late, pending request, partial/stuck
transfer, missing fresh completion, ignored configuration/acknowledgment,
half-range ambiguity, backward time and a frozen-timer 65,535-poll cap retain
their original errors. Re-entry cannot replace the descriptor, acknowledge
flags or release buffers. Explicit synthetic transfers after error return
really change destination bytes using the original buffers. There are 257
successful same-epoch reuses; test-only reset between independent scenarios
is not a production reset/recovery API.

**Image-checked and simulated:** 97 scenarios execute genuine SDCC 4.2.0
instructions with the IRAM/XDATA alias enabled. Every executable byte is
mutation-rejected, including reset code, helpers and complete callers.
Separate instruction decoding, CDB/assembler allocation and typed diagnostic
checks pin actual SFR accesses, arguments, nine-NOP interval and private
storage. At `028A`, `MOV DMAARM,#1` is followed by NOPs `028D..0295` and RET
`0296`; the sole call at `0C40` returns through the checked post-arm poll at
`0C6C` before the request at `0C7C`. The timing basis is
[SWRU191F's system-clock contract](PROVENANCE.md#m2-channel-0-dma-sources),
not s51's generic 8051 instruction-clock counts.

The synthetic engine uses the real linked DMA configuration SFRs and actual
eight descriptor bytes to copy each byte between actual modeled XDATA
addresses. It supplies fresh ARM/REQ/IRQ effects, including completion before
disarm, missing completion, post-return effects, late completion and a foreign
flag arriving at acknowledgment. No CODE patch, C success substitution,
unallocated RAM pool or physical peripheral model is used. CPU masks, clock,
GPIO, radio/peripheral memory, source/tails, immutable failure diagnostics,
descriptor persistence, stack unwind and full status/upper-IRAM guards are
checked, including a genuine second caller invocation after faults.

The isolated executable is **5,485 CODE bytes**, including **2,867 driver
bytes**, with **198 ordinary XDATA bytes + 8 result bytes / full 64-byte
reservation**: 262 reserved nonaliased bytes, below 512. The private DMA/timebase
prefix occupies `0000..005B`; the separately protected generic-store helper
scratch is at `00BD`. IRAM before stack is `00..3B`, with 196 stack bytes
reserved at `3C..FF`; the synthetic high-water SP is `4E`, below the `80` guard.

At foundation publication, the twelve existing board configurations retained their complete published
BIN lengths/hashes and host/image/alias checks. Their verifier rejects DMA
symbols or source records for every image; matrix and artifact whitelist
are unchanged. The existing FIFO 69,895 host cases / 99 linked scenarios and
clock/IRQ/timebase/MAC checks remain intact. The contemporaneous offline
Python run passed 389 tests, including separately owned debugger work.
The two FIFO fixture simulations hit their existing 15-second bound during
the twelve-way parallel check; both passed separately with that bound and
the fixture code unchanged.

**Separate hardware evidence:** the subsequent board fixture below, not this
standalone executable or the debug-config gate alone, establishes bounded LG
controller copies and a terminal timeout. It verifies all physical CODE after
its own reset and clears DMA_PAUSE before any DMA-register access, retaining
known reset/TRIG0 history and persistent buffer ownership. Arbitration timing,
peripheral transfers, AES and physical stuck-controller recovery remain open.
Historical FIFO records are unchanged. No automatic reset, abort, resume,
hardware access or recovery is added; M2 #4 remains open.

## M2 DMA board fixture coverage

Both `IMAGE=dma_fixture` layouts are **host-tested, image-checked and
synthetically simulated**; the LG image also has the separate hardware record
below. The [canonical ABI, hashes, memory layout,
checkpoints and manual procedure](DEBUGGING.md#channel-0-dma-board-fixture)
remain separate from DMA-controller hardware acceptance. The published DMA,
clock, timebase and reset-scoped debugger gate implementations are unchanged.

The fixture host model executes the real drivers with checked 32-entry logs
and an independent copy reference. It covers 257 cycles/514 copies/6,289
bytes, all lengths/pattern phases, both clocks/routes, all 36 byte/guard
corruptions, clock timeout/cap and confirmed/unconfirmed rollback, stale or
changed controller/flag/config/CPU ownership, partial/stuck/late completion,
and immutable fault/descriptor/diagnostic/buffer lifetime. Explicit synthetic
completion after C returns does not permit reuse or clear the original fault.

Each linked board executes **42 scenarios**: a 257-cycle normal run and 41
fault cases, including every actual C buffer-readback position, the real
post-arm expiry RET hold, partial/stuck/late effects, clock failure and flag
changes. The model decodes actual DMA configuration/descriptor addresses and
length, copies exactly each requested byte, and verifies all 6,289 destination
addresses plus 514 arm/nine-NOP/request/ack sequences and 514 clock writes.
Simulator event-control variables live outside target address spaces.
Completion/control/clock changes are explicit synthetic events; no CODE
patch or C success replacement is used.

Every board CODE byte and every byte of the independently normalized original
2,867-byte DMA module is mutation-rejected. Exact CDB types/fields, private
prefix, helper exclusion, caller paths, passive SFR boundary and all three
live return frames are checked. Status/unallocated/peripheral/alias/upper-IRAM
guards are unchanged; measured synthetic peak SP is79 on both boards.

Runner tests are original synthetic doubles, not hardware observations. They
check 257 decoded cycles, whole-CODE-before-enable-before-resume ordering,
separate authorization, config26/22 distinction, FMAP1 preservation, every
live context byte, actual controller reads, exact timeout acceptance, no
fault payload inspection, every runner I/O boundary and cleanup failure.
The real debugger with a synthetic USB/CPU backend also checks all register
banks/DPS values, every wrong config byte, and every peripheral-read I/O
failure/late boundary: no restore, retry or following target I/O after fault.

All fourteen `make BOARD=... IMAGE=... PYTHON=.venv/bin/python all test`
combinations passed serially, including **402 Python tests** per combination,
the unchanged standalone DMA **194,819 host / 97 linked** and FIFO
**69,895 host / 99 linked** cases, and existing clock/IRQ/timebase/MAC checks.
Complete BIN lengths and SHA-256 values matched all twelve published baselines.
Repository/local-link checks covered 123 files; whitespace checks passed.

The twelve older images remain DMA-excluded and byte-identical. Only the two
new images link DMA; the fourteen-job matrix retains the exact seven-file
artifact whitelist and `hardware_tested=false`. Generic, other channels/triggers,
physical stuck-DMA/abort recovery, AES, peripheral DMA, RF, calibration and
M2 #4 remain open.

### 2026-09-17 bounded LG DMA hardware evidence

Parent-run `all test` passed for both new images, including 402 Python tests
and the unchanged standalone DMA/FIFO/clock/IRQ/timebase/MAC checks.
The unchanged 8,890-byte LG image passed normal copies and a separate real
pre-request deadline hold: DMA_TIMEOUT8, 27,960 raw ticks/four polls,
actions7, complete1 but verified0, with IRQ1 unacknowledged and no payload
inspection or reuse. This was not a physical stuck-controller injection.

A separately reset 257-cycle run checked **1,029 READY stages, 514 copies,
6,289 transferred bytes and 18,504 source/destination/tail/guard bytes**,
with both clocks/routes, all lengths 1..16 and the real `00FF->0100` RAM
boundary in both directions. CPU/FMAP, M0/counter-wrap and unrelated flags
were preserved. Final state was DMA READY016A/config22, RC16, IRQs off,
ARM/REQ/DMAIRQ zero. The
[canonical record](DEBUGGING.md#2026-09-17-lg-compiled-c-dma-acceptance)
contains the hash, timeline, raw bounds and retained-error observations.
Actual negative SP75/return frames were checked; peak SP79 remains synthetic,
not a measured hardware high-water mark. Raw records remain private.

## M2 isolated AES DMA block coverage

`make test-aes` runs the [single-block contract](ARCHITECTURE.md#isolated-aes-128-dma-block)
without a board image or hardware access. **Never flash `aes_test.ihx` or
upload it as board firmware.** The AES code is excluded from the fourteen
earlier board images; only the separate AES fixture below links it.
Old driver proofs and artifact policy remain intact.

**Host-tested:** the original mathematical reference independently passes
FIPS 197 C.1 and all four SP 800-38A F.1.1 AES-128 blocks. The real AES/timebase
driver model passes **243,481 operations**, including all 16 byte positions
with 256 varied public patterns, CODE/XDATA inputs, in-place input/output,
both clocks, every ordinary-address rejection surface and all generic tags.
It verifies actual staged bytes, SINGLE/LEN16 descriptors and AES aliases,
separate nine-clock readiness, all 48 input/16 output transfers, guards and
fresh completion/acknowledgment. Existing 32-entry logs are checked/consumed,
not enlarged or discarded. Stale RDY/output/flags, ignored writes, all partial
input/output positions, missing DMA/AES flags, bounds/caps/counter faults,
controller/clock/IRQ mutations, all entry control/request/flag values and
post-return private DMA effects retain the original error and forbid reuse.
There are 257 varied-key/input reuses in one completed/drained reset epoch,
a frozen-timer 65,535-poll bound, exact deadline/last-permitted-poll cases,
delayed ENC status after output drain, and mixed ARM/IRQ observations.
The corrected model posts fresh KEY/IV ENC pairs as well as block flags:
per-command missing/partial/delayed/stale flags and ignored/late ACKs cannot
advance to the next command or publish output. Issued ACK counts and confirmed
phase masks distinguish an effective late write from a successful C observation.

**Image-checked and simulated:** **188 linked scenarios** execute the actual
SDCC 4.2.0 caller, generic pointer staging, driver and timebase. Every CODE byte
is mutation-rejected, including constants, runtime and caller; separate
instruction/CDB/assembler checks pin the complete module, typed arguments,
29-byte diagnostics, every private allocation and helper exclusion.
Input arm is `03EC` (`MOV DMAARM,#1`, NOPs `03EF..03F7`, RET `03F8`);
output arm is `03F9` (`#2`, NOPs `03FC..0404`, RET `0405`).
Sole callers `111A` and `0FBF` respectively prove four separate fetch
intervals across the three input phases and one output arm. Every arm RET
checks the real descriptors, including the full 32-byte DMA1 table.
Readiness uses SWRU191F system clocks, not generic s51 cycle timing.

The synthetic model maps only the two documented AES aliases to their SFRs,
in addition to the required IRAM alias. Each input byte travels from the
actual descriptor-selected XDATA address through `70B1`; each output byte
travels from `70B2` into actual private XDATA. All downloaded bytes are checked
against the public inputs used by the independent oracle. Explicit synthetic
AES/control/completion/timer events supply peripheral behavior; no CODE patch,
C return substitution or vector-lookup encryption is used. Cases include all
five KATs in four CODE/XDATA combinations, repeated bytewise rollover,
partial/lost/late/mixed effects, stuck ST and delayed ENC status,
every consequential ignored write, timed/cap and
terminal re-entry paths. Failure never publishes caller output, even when a
late private drain finishes. Full caller/descriptor/status/peripheral/alias
guards, stack unwind and upper IRAM `80..FF` remain checked.

The executable has **6,387 CODE bytes**, including **5,254 driver bytes**,
and **320 ordinary XDATA + 8 result / 64 reserved** (384 reserved nonaliased
bytes, below 512). AES/timebase/private arguments occupy `0000..00CE`;
descriptors are `0019..0020` and `0021..0040`, four staging arrays
`0041..0080`. Caller diagnostics are `00CF..00EB` (29 bytes), key `00EC..00FB`,
input storage `00FC..011B`, guarded output `011C..012D`; actual output is
`011D..012C`. Caller scalars follow; `__gptrput_PARM_2=013F` is excluded
separately. IRAM before stack is `00..40`; stack `41..FF` reserves 191 bytes,
with synthetic peak SP **54**. Complete linked CODE SHA-256 is
`cc1af6a5c38853c0a5453aad68ae2b3720d9fd40211096598c1650628fd28b27`.
Canonical driver SHA-256 is
`b80e064f5fb405c8a5d2c28722e18b51f25c5d99d90dd8aaec6da4332103befd`.

At the original foundation publication, both representative DMA-board `all test`
runs passed **243,355 AES host / 152 linked and 403 Python tests**, plus the
unchanged legacy checks. Twelve other combinations passed focused checks;
all fourteen published BIN lengths/hashes were preserved. That slice retained
the fourteen-job matrix/seven-file whitelist and passed the 130-file guard.
These are historical results, not evidence for the corrected load protocol;
current fixture/regression results are recorded below.

**Physical evidence and remaining gate:** the unchanged corrected LG fixture
passed short normal operation, both exact negatives and separately reset
257-cycle recovery, as
[recorded separately](DEBUGGING.md#2026-09-17-corrected-lg-aes-bounded-acceptance).
The recovery run accepted/published514 blocks and independently covered the
set of all168 combinations of21 fixture vectors, four CODE/XDATA spaces and
both clocks, with fresh KEY/IV pairs and per-phase ACK confirmation.
Both negative calls remained unpublished; generic is not hardware-observed.
Each manual invocation must genuinely reset, verify every physical CODE byte,
then use the existing explicitly permitted `26 -> 22` gate before resume or
DMA-register access; recovery is a separate full-reset invocation, not an
aborting key/IV load or C-state clear. The first physical KEY failure remains
root-cause history, not the source of later IV/block acceptance.
No calibrated timing, key-erasure/retention experiment or physical
stuck-controller recovery is established.
CPU-only AES sequencing, security/CCM/key management and M2 #4 remain open.
That AES recovery ended with the **corrected 12,765-byte fixture at
READY016A/config22/RC16**, completed/heartbeat1 after257 cycles and fault latch0.
This is historical after the later PRNG programming below.
The standalone test remains unflashed.

## M2 AES board fixture offline coverage

Both `IMAGE=aes_fixture` layouts use the
[64-byte ABI, allocation and manual procedure](DEBUGGING.md#aes-fixture-abi-and-linked-proof).
The target calls the corrected hardware driver and compares actual caller
output, not a firmware software cipher or returned vector. The independent
host-only reference checks all five primary KATs and sixteen derived public
cases; it remains unconditionally excluded from all sixteen board images.

Host C exercises **1,388 steps**, including 257 cycles/514 calls, every one
of 21 vectors/four CODE-XDATA combinations/both clocks, all 50 caller
source/output/guard corruptions, every partial input/output position, late
completion, frozen cap, ignored/changed control, lost flags, ownership errors
and clock request/rollback failure. All three interrupt masks, including
ENCIE, are rejected without an AES call. The original 32-entry MMIO logs are
checked and consumed; no capacity increase or silent overflow/reset is used.
Terminal steps perform no MMIO, buffer/descriptor reuse or diagnostic replacement.
Each command has lost/partial ENC-pair and ignored/late-ACK coverage.

Alias-aware s51 executes **102 compiled scenarios**, including the full
1,029-stage/257-cycle sequence, both genuine timeout contexts, 50 precise
byte corruptions, per-KEY/IV/block deadline/cap/ambiguous-helper/counter/control/
request/partial failures, lost input/output flags, per-command lost/partial ENC
pairs and ignored/late acknowledgments, delayed output drain, preserved clock
rollback and terminal late delivery.
The final legal sample succeeds at poll4096; exhaustion at the preceding
non-final block-ACK sample fails without publication. No impossible
post-approved-copy failure or C poll-count/return substitution is injected.
The two genuine negative contexts prove actual stacks/arguments and phase,
not a source-line PC guess or a shared sample mistaken for a unique final path.

Synthetic DMA reads all **24,672 descriptor-selected input bytes through
70B1** and writes all **8,224 output bytes from 70B2** during the full run.
Each descriptor's LEN16/SINGLE/increment/trigger/table shape and every separate
arm readiness/command/ACK sequence are checked, including **1,542 issued and
1,542 confirmed ENC ACKs**. Oracle effects are accepted only with matching
actual downloads; this caught a real SDCC CODE/XDATA
ternary-tag bug in the fixture caller, fixed with separately typed assignments.
There is no CODE patch or replacement of the compiled AES function.
M0/status/alias/peripheral/stack guards remain strict, including upper IRAM
`80..FF`; measured synthetic peak SP is **7F** for both layouts.

Complete board CODE/constants/caller bytes, the relocation-equivalent
5,254-byte corrected driver and private/typed map/CDB allocations are
mutation-rejected. Restricted-runner tests cover full CODE-before-gate-before-
resume, all live argument/descriptor/saved-frame fields, wrong config/PC/
stack/CPU/flags, every high-level I/O boundary and every low-level failed/late
transport exchange, cleanup errors and safe caller-only post-failure reads.
No test enumerates USB. The [source/retention limits](PROVENANCE.md#m2-aes-board-fixture-sources)
and [physical gate](DEBUGGING.md#parent-only-aes-acceptance-procedure) remain
distinct from host/image/synthetic evidence; the bounded corrected LG results
below do not confer hardware evidence on generic or on untested cases.
Parent's actual-reader-prefix fix and all six actual-board/mode
preflights remain checked, including wrong-prefix rejection with a matching hash.

Both agent-run corrected board `all test` runs passed, including the unchanged five-KAT
reference, **243,481 AES host / 188 isolated linked**, **194,819 DMA host /
97 linked**, **69,895 FIFO host / 99 linked**, and existing clock/IRQ/timebase/
MAC checks. The Python suite covers **415 tests** after the final
unattempted-diagnostic regression. The other fourteen combinations passed
focused build, board-host, image and alias checks with every complete
published BIN length/SHA-256 unchanged. Only AES and its directly related
fixture/proofs change; other platform/debug drivers and their proofs remain
unchanged. Heavy s51 runs were serial, with the original per-process 15-second
bound. Repository/local-link checks
cover 138 files. That AES addition selected sixteen full jobs with the unchanged seven-file
whitelist and `hardware_tested=false`; no hosted-CI or hardware result is
inferred from local validation. The parent independently completed both
corrected LG/generic serial `all test` runs:415 Python tests,102 compiled
fixture scenarios per board, all actual-image normal/pre-key/final prevalidation
paths and unchanged MAC guards. The 138-file repository guard passed.
The parent also independently matched all fourteen older BIN sizes and complete
SHA-256 values against published baselines; module and corrected board
identities are unchanged. These are local results, not a hosted-CI pass.

### 2026-09-17 first AES failure and offline correction

The [canonical sanitized record](DEBUGGING.md#2026-09-17-first-lg-aes-key-load-failure)
preserves the original LG 12,783-byte/`97aed900...` image, programming/attempt
times, parent-only physical CODE verification and exact terminal sample.
The first runner attempt stopped before USB because of a guessed instruction
prefix; the parent corrected it without firmware changes. The next attempt
reached KEY DMA completion with ENCCS4C/S0CON03/ARM02/IRQ01 and returned
AES_STATE_CHANGED7 after39 raw ticks/five polls. All50 separate caller bytes
were unchanged; no output was published or owned flags acknowledged.

This disproved the original model's load-flags-clear assumption despite its
passing KAT/host/linked suites. At that stage the corrected per-command
completion/ACK contract had only host/image/synthetic evidence, and IV pair3
was unobserved, not guaranteed by the reviewed block-interrupt wording.
The old run ended at FAULT016C/config22 with output DMA armed02. Later
programming and corrected-image observations are separate below.

### 2026-09-17 corrected LG AES bounded hardware evidence

The unchanged 12,765-byte LG image (`0ee3e094...`) and 5,254-byte module
(`b80e064f...`) passed parent-run normal3, separate pre-key/final negatives
and separately reset 257-cycle recovery
after checked board-HEX programming/readback. Each invocation reset, verified
all physical CODE, and performed the explicit26->22 gate with CPU/FMAP preserved.
The [canonical record](DEBUGGING.md#2026-09-17-corrected-lg-aes-bounded-acceptance)
contains full identities, sanitized times, live contexts and exact results.

Normal3 confirmed six blocks,288 input bytes,96 drained/published bytes and18
issued/confirmed ENC ACKs. It covered vectors0/1/2 on RC16/XOSC32 with spaces0/1;
RC elapsed112..115 raw ticks, XOSC57, all17 polls. Fresh **KEY and IV** pairs
and checked-clear acknowledgments are now hardware-observed through the
unchanged production C gate, not inferred from the earlier KEY failure.

Pre-key used actual RETs0C25->0302, SP7B/all14 frame bytes, cached unexpired
poll4, and returned exact AES_TIMEOUT8 after87,420 raw ticks/five polls:
one submitted/completed input phase, no ACK, drain or publication.
Final used1DCB->0172 before latching final now, SP73/all6 frame bytes, and
returned AES_TIMEOUT8 after84,326 ticks/17 polls despite complete drain.
DMA ACK issued3/mask7 and ENC issued3/mask3 distinguish the unconfirmed final
block ACK. Both calls checked all50 unchanged caller bytes and ended at
FAULT016C/latch8, without private-payload inspection or implicit cleanup.
Their prior successful RC block is separate from the failing call's zero
publication. These are genuine production deadline paths, not stuck-controller
injections; errors and clear live flags still do not prove quiescence.

The separate full-reset recovery invocation at 14:15:30..14:19:49 returned 0:
257 same-reset cycles, 1,029 READY stages, 514 accepted/published blocks,
1,542 KEY/IV/block commands, 24,672 C-confirmed input bytes and 8,224 drained/
published output bytes (32,896 total DMA bytes). It issued 2,056 individually
delayed arms. DMA phase ACKs and ENC ACKs each totaled 1,542 issued and 1,542
confirmed. All 50 caller bytes per block were checked in C and independently
read back: 25,700 checks. The parent independently asserted the **set of all 168
vector/space/clock combinations**, including all 21 vectors and all four
CODE/XDATA combinations on both clocks. Completed/heartbeat wrapped to 1.
All 514 calls used 17 polls; RC elapsed 112..116 and XOSC 56..58 raw ticks.
The 259.4136599 host seconds include preflight/reset/CODE/gate/inspection,
not isolated AES timing or throughput.

The final snapshot of that AES recovery (now historical) is
**READY016A/config22**, wirev2 stage4/clockRC, completed/heartbeat1,
CMD/STA C9/C9, SLEEPCMD04, all IENs0, ENCCS48/S0CON0, ARM/REQ/IRQ/IRCON0,
CFG0/1=0045/004D and fault latch0; IP0/1=0, TCON5, other retained flags0.
The negative FAULT records remain historical. The separate full reset, not
clear DMA flags alone, established recovery; neither negative published caller
output, inspected private payload or performed automatic recovery.

Generic has no physical AES result. Native29/wirev2, all fourteen older BIN
identities, memory/alias guards and the exact seven-file artifact policy remain unchanged;
generated `hardware_tested=false` is not rewritten by this manual evidence.
No calibrated timing, CPU-only AES pacing, CCM/authentication, key management/
erasure, networking or complete M2 acceptance is established.

## M2 deterministic PRNG coverage

`make test-prng` runs the isolated
[explicit-seed/13-shift contract](ARCHITECTURE.md#isolated-deterministic-prng).
**This is deterministic, not entropy or a cryptographic RNG. Never flash
`prng_test.ihx` or upload it as a board artifact.** That foundation added no board IMAGE, hardware
runner, production software replacement, RF/ADC/DMA/security integration or
new permission is introduced.

The original strict host corpus executes **204,699 real-driver calls**.
Independent bit-cell and polynomial models agree on all65,536 states;
exhaustive traversal proves fixed points0000/8003 and two disjoint cycles
of32,767 states each. All65,534 valid seeds are loaded and stepped through
the actual driver. These are derived mathematical facts, not a nontrivial
public known-answer table, entropy assessment or measured on-chip period.
Tests cover all16-bit numeric pointer classes, null/zero limits, legal
host-mapped unaligned/end-of-range caller addresses, guards, all256 ADCCON1 entry values,
all byte values in IRQ/sleep/clock entry checks, every positive poll cap,
last-allowed versus one-too-late completion, ignored seed/command writes,
forbidden/stale results, prior-state changes, observed EOC/ST/reserved/IRQ/
clock mutations and terminal no-MMIO re-entry after late completion.
Existing32-entry logs are checked and consumed per entry, not enlarged or
silently reset. No default MMIO read invents success.

The genuine SDCC4.2.0 executable is **1,282 CODE bytes**, including
**1,043 driver bytes at0062..0474**. Ordinary XDATA is39 bytes: complete
driver state/parameter/scratch prefix0000..001D and caller001E..0026.
The isolated8-byte `PRNG` version1 result occupies1E00..1E07 inside the
unchanged64-byte status reservation: **103 reserved nonaliased bytes**,
47 used, below the512-byte budget. There is one BIT temporary and no generic
pointer runtime helper. Stack starts21/initialSP20,223 bytes reserved;
observed synthetic high-water is2A and upperIRAM80..FF stays guarded.
XDATA1F00..1FFF aliases IRAM, never an extra allocation.

| Reviewed actual CODE extent | Bytes | SHA-256 (ascending physical CODE order) |
| --- | ---: | --- |
| Complete standalone executable0000..0501 | 1282 | `a7292d43d7e965af5d0565a8abb708cd4ac0df9189ab7523ae9eaa9d2723ca90` |
| Driver0062..0474 | 1043 | `df20f1945e7993fcfefab707ba75ebec8474a8f949857ef9bed3608679681954` |

The proof rejects a mutation of **every complete executable byte**, actual
instruction/listing/LCALL/SFR ledger changes, pointer/return/local ABI and
private-allocation/source/status/stack changes. The genuine caller calls
seed01D8 and next1602AC; RNDL writes at0248/024A are high then low,
the sole ADCCON1 command write is0396, and state reads0186/018C are
low then high. There is no RNDH write, ADC conversion, clock/IRQ/RF/DMA write
or production software transition. Board verifiers reject PRNG/model/test
symbols and sources from all sixteen earlier images; the subsequent PRNG
fixture below admits only the real driver, never the host model/test.

Serial alias-aware s51 executes **86 scenarios/505 actual driver calls**,
including257 consecutive updates, both clocks/EOC states, successful explicit
reuse/reseed, invalid arguments/history/control, ignored writes, stale/forbidden
results, delayed/stuck completion and late-effect terminal faults. It stops
at each genuine SFR instruction and verifies exact PC/read/write/order plus
real C results, caller guards, retained state, unrelated SFRs, all unallocated
nonaliased XDATA/status and upperIRAM. The original15-second simulator limit
is unchanged. Only documented PRNG register effects are synthetic; actual
CODE, helper returns and success/failure paths are never replaced.

Both LG/generic AES-board serial `all test` runs passed with **416 Python
tests and102 compiled AES-fixture scenarios each**, unchanged MAC guards,
and the new PRNG counts above. The existing AES corpus remains243,481 host
operations/188 isolated linked scenarios; its complete6,387-byte executable
and5,254-byte module are unchanged. All fourteen older board/image combinations
passed focused build, board-host, linked-image and alias-aware checks without
rerunning identical standalone corpora. **All sixteen complete BIN sizes and
SHA-256 values match published c828bf57c12ead33557180a59afcef1d29f3de56.**
The142-file repository/local-link guard and diff checks passed. That foundation kept
sixteen full jobs with exactly seven artifact paths and
`hardware_tested=false` for every image; no hosted-CI result is claimed here.

**The bounded LG short/stopped/reset-recovery physical gate is complete.** The separate
[LG short hardware run](DEBUGGING.md#2026-09-17-lg-prng-short-acceptance)
observed explicit seed1234 loading, its four-word sequence twice on RC16,
non-advancing CPU readback, benign errors and shared-state/guard preservation.
The corrected7,289-byte LG wirev2 passed its own
[short case](DEBUGGING.md#2026-09-17-corrected-lg-prng-short-acceptance) and
[full-stopped hardware acceptance](DEBUGGING.md#2026-09-17-corrected-lg-prng-full-stopped-acceptance):
all131,084 words, four complete periods/all65,534 valid states per clock,
real after-C/live STIF race and preserved flag history through the genuine
stopped probe. Do not infer these observations from s51 or AES acceptance.
The same image also passed
[separate full-reset recovery](DEBUGGING.md#2026-09-17-corrected-lg-prng-full-reset-recovery-acceptance):
the entire corpus in a new reset epoch, with a distinct C-observed STIF
transition and continued preservation. Final LG is halted ENDREADY016A/
config26/RC16, fault0 and C/live IRCON80, with no probe execution or later
resume. The stopped FAULT snapshot is historical, not the current state.
Ordinary tests never enumerate USB or grant new general debug permission.
Generic/EOC1/physical poll-fault acceptance remains open.
RF/noise entropy seeding, ADC conversion, sleep/retention, ISR/CSP coexistence,
general randomness/security and full M2 #4 remain unimplemented/open.

## M2 PRNG board fixture offline coverage

Both `IMAGE=prng_fixture` layouts use the
[88-byte wire ABI and guarded procedure](DEBUGGING.md#deterministic-prng-board-fixture).
Production PRNG remains1,043 bytes, relocation-equivalent to
`df20f1945e7993fcfefab707ba75ebec8474a8f949857ef9bed3608679681954`;
the standalone1,282-byte test remains
`a7292d43d7e965af5d0565a8abb708cd4ac0df9189ab7523ae9eaa9d2723ca90`.
No software sequence generator is linked into either board.

Original host C runs the complete schedule with EOC0 and EOC1, checking
**262,168 individual words**, both full disjoint cycles per clock, true31-word
tails, all caller guards, actual API statuses, fixed32-entry log consumption,
ignored seed/command writes, busy cap, late completion, ADC/EOC/ST/RCTRL/
clock/IRQ mutations, caller range/private-prefix rejection and foreign-state
history. The fixture's four benign checks and real RCTRL11 probe execute
unchanged driver calls; terminal re-entry retains caller/wire state without MMIO.

For each board, alias-aware s51 executes4,111 READY checkpoints and131,084
actual returned words. The host compares **every word**, proves first return
to each period seed exactly at32,767, rejects all earlier duplicates, and
checks the union is exactly all65,534 valid states per clock. Count/hash alone
is insufficient. It checks16 actual high/low RNDL writes,131,084 sole37
commands, both clock writes,8 seed loads,4,100 batches and heartbeat wrap to4.
All caller bytes, unfilled tails, initial objects, M0/flags, unallocated XDATA,
peripheral XDATA, upperIRAM80..FF and exact stack unwind are checked.
Wirev2 injects STIF **after C's128th READY snapshot and before live inspection**:
raw C IRCON20/liveA0, at run1/index3904/total3912. Ordered history carries
that actual SFR state across the next genuine continuation and through later
XOSC32, ENDREADY, probe and FAULT, with3,985 subsequent preserved observations.
Immutable initial IRCON stays20; no C snapshot is rewritten.

The full sequence runs as **33 bounded genuine checkpoint continuations**.
Each new s51 process restores byte-for-byte RAM/IRAM/SFR state captured at
the preceding real READY, proves that state before resuming its actual NOP,
and resumes the unchanged CODE/stack. No synthetic C return, CODE patch,
counter shortcut or reseed breaks a period. Each process retains15 seconds.
Synthetic register effects implement only documented seed/13-shift/self-clear
behavior and the documented sticky STIF assertion; they are not physical timing evidence.

The full-corpus ENDREADY is followed in the synthetic negative by the actual
fixture3F write and genuine first next16 LCALL, with complete frame/arguments
checked. It reaches expected UNSUPPORTED_STATE6, verifies two retained-fault
re-entries and stable FAULT with unchanged caller batch/sentinel. Nine additional
compiled seed/ignored/stuck/EOC/ST/RCTRL/IRQ/clock fault scenarios stop without
automatic recovery. Wirev2 adds two actual short linked cases (16 words)
for initial STIF1 and arrival during C execution, plus81 flag-fault cases:
all79 other flag bits and observed STIF1->0 with initial0 or1. Raw flags,
immutable initial observations, no PRNG fault/command on fixture rejection
and complete terminal RAM/IRAM/SFR retention remain checked.
For the two deliberate TCON.4/.6 mutations only, C52's classic timer aliases
are configured as external counters without input edges before C execution;
no CC2530 code/counter is patched and no flag/SFR comparison is excluded.
The host C corpus injects assertion during one full run and during the other
run's genuine stopped probe, with unchanged full sequence counts.
Host runner cases cover initial/C/live/probe/final-FAULT races, subsequent
deassertion/stale C rejection, immutable initial flags and all65,536 IRCON
transitions. Host USB tests cover fixed SFR read order, every low-level
failure/late-effect boundary, all wrong configurations, all short-run and
stopped-suffix I/O boundaries, corrupted contexts/caller bytes, and cleanup
failure suppressing success output. All three real-image `validate_program`
modes run before backend loading, not merely against synthetic instruction bytes.

Complete board CODE/constants/caller mutation rejection, original clock/
timebase/PRNG relocation, native pointer/limit/return ABI, private-prefix and
runtime exclusions remain strict. Layout is310 ordinary XDATA plus32-byte
M0 result inside64 reserved (**374 reserved nonaliased bytes**). Stack starts4E,
initialSP4D,178 bytes reserved; synthetic peakSP61.

Both original wirev1 LG/generic serial `all test` runs passed **429 Python tests each**,
the262,168-word host fixture corpus,33-segment complete compiled fixture
corpus, the unchanged standalone PRNG204,699 host/86 linked scenarios and
all existing platform/MAC corpora. The strengthened actual-PC and adjacent
native-clock-object continuation checks also passed for both new boards.
All sixteen older combinations passed focused build, board-host, genuine
image and alias checks; **every complete older BIN size/SHA-256 matches
published cf577b673babdce810b5c42710b7ef96e0b4e84c**. Published PRNG and AES
standalone CODE identities remain unchanged. The150-file repository/local-link
guard and diff checks passed. The tested CI matrix has eighteen full jobs,
the exact seven-artifact whitelist and `hardware_tested=false` for every image;
no hosted-CI pass or hardware observation is asserted by these local results.
The parent independently confirmed those original two-board results,
including the complete corpus/probe/9 faults and all sixteen older identities,
before the physical flag-policy interruption.

**Corrected wirev2 offline validation (implementation runs):** both LG/generic `all test` runs
completed serially with **434 Python tests each**, the262,168-word host C
corpus and the full33-segment/131,084-word linked corpus, plus16 short edge
words,9 retained driver faults and81 flag faults per board. The chosen
C/live STIF race and3,985 subsequent preserved observations include genuine
continuation, XOSC32, END and probe/FAULT; all65,536 ordered IRCON pairs are
host-checked (384 permitted,65,152 rejected). Focused runner coverage has17
tests, including unchanged terminal I/O and actual-program prevalidation.
The per-process simulator timeout remains15 seconds.
All sixteen older combinations passed focused build/board-host/image/alias
checks; every individual complete BIN size and SHA-256 was compared with the
published baseline and matched. Standalone PRNG1282/a7292d43... and
AES6387/cc1af6a5... identities remain unchanged, as do the exact1,043-byte
PRNG module and original clock/timebase proofs. The150-file repository/
local-link guard and diff checks passed.
Corrected CODE grows65 bytes per board to7249/7289, with unchanged310
ordinary XDATA,374 reserved nonaliased bytes,178 reserved stack and peakSP61.
The [exact candidate identities and probe frames](DEBUGGING.md#parent-only-prng-acceptance-procedure)
are recorded separately from the original physical image below.
The parent independently reverified all sixteen published BIN sizes/full
SHA-256 values and both corrected `DebugImage`/three-mode `validate_program`
paths before programming. Both independent LG/generic `all test` runs now
completed with exit0 and434 Python tests each. Each passed33 genuine
continuations,4,111 READY checkpoints,131,084 actual simulated words/four
32,767 periods, STIF C/live race plus3,985 preserved observations,16 edge
words and probe plus9 driver/81 flag faults, with peakSP61 and MAC alias
checks intact. Parent150-file repository/local-link and `git diff --check`
checks passed; generated `hardware_tested=false` was verified.
These completed offline results are separate from the full-stopped and
full-reset recovery hardware records below; neither physical result is
inferred from the offline checks.

### 2026-09-17 LG PRNG short hardware evidence

The [canonical sanitized record](DEBUGGING.md#2026-09-17-lg-prng-short-acceptance)
records the original wirev1 7,224-byte LG image, SHA-256
`aac5793818df6c2f4695d08d33cc2ea5e599945b4472e2b31160caf4e0bda0f7`,
programmed via checked board HEX with erase/write/readback Completed.
The short run performed its own resetPC0/config26, checked all 7,224 physical
CODE bytes and preserved complete CPU/FMAP (FMAP1), without DMA enable or
debug-configuration write. Five READY stages passed real NOT_SEEDED5,
limit0/status2 and invalid0000/8003/status1 checks (benign mask15),
then two explicit seed1234 loads and eight RC16 words:
`8D94 E5AC CBBE 1731` twice. C and independent host math checked output;
two additional RNDL/H read pairs per word, 28 unfilled9669 slots per batch
and all caller guards passed (checked68).

The completed short snapshot was wirev1 READY/stage3/run0, index8/batch2/
total8/seedcalls2, completed/heartbeat2, result/seedresult/fault0,
ADCCON1/initial33, CMD/STA C9/C9, SLEEPCMD04, IENs0,
flags `[0,0,5,0,0,0,0,0,0,0]`, hardware1731 and clock NOT_ATTEMPTED8.
The reported 10.592902 host seconds include reset/preflight/CODE/inspection,
not PRNG performance. That original short case covered only RC16/seed1234/EOC0.

The subsequent wirev1 long invocation was interrupted by the fixture flag
policy and established no full-corpus, stopped-probe or recovery acceptance.
The corrected wirev2 cases below are separate; earlier original-image results
remain valid and no hosted-CI run is claimed.
Generic, EOC1 and physical stuck/late/poll-limit cases remain host/image/
synthetic-only. All sixteen older BIN identities,
memory/layout limits, eighteen-job matrix, seven-artifact whitelist and
`hardware_tested=false` remain unchanged. RF/noise entropy, CRC/ADC/DMA/AES/
flash/sleep/ISR integration and cryptographic randomness remain outside
this slice; M2 #4 remains open.

### First long interruption and wirev2 correction

The [canonical dated interruption record](DEBUGGING.md#2026-09-17-lg-prng-long-run-flag-policy-interruption)
documents the557.307896-host-second original-image run ending with
`PRNG live shared-state differs from C observation`. Default Sleep Timer
compare latched STIF after C's snapshot (SWRU191F p.47/129); no PRNG error
or timeout was reported. The last32 words were rejected before buffer/Sequence
inspection, so C total62535 is not an independently checked corpus.

The historical original7224/aac57938... halt was
READY016A/config26, run2/seed0003/index29760/batch930, C total62535,
completed164, result/seedresult/fault0, C initial/latest IRCON00 and live80.
All other observed shared fields match; a separate read-only observation
changed no CPU state and performed no reset/resume/flag clear.
The parent subsequently programmed wirev2 and completed the separate short,
full-stopped and full-reset recovery cases below.
The18-job/seven-artifact policy,
`hardware_tested=false` and all sixteen older images remain unchanged.

### 2026-09-17 corrected LG PRNG short hardware evidence

The [canonical corrected-short record](DEBUGGING.md#2026-09-17-corrected-lg-prng-short-acceptance)
pins the unchanged7289-byte LG wirev2/b53ecd58... board HEX, successful
erase/write/readback and own resetPC0/config26 with every physical CODE byte
checked and complete CPU/FMAP1 preservation. Five READY stages accepted
eight RC16 words from two explicit seed1234 loads, with benign mask15 and
guard/tail checks68. Six raw initial/C/live flag observations contained
**no STIF transition**; that short case alone did not accept transition handling.

The installed image is now wirev2, not the historical failed wirev1 candidate.
The short READY016A snapshot is historical; the subsequent full-stopped
acceptance is recorded below. These short observations alone did not establish
the later corpus/probe/STIF or separate full-reset recovery results.
No entropy/security randomness or change to generated
`hardware_tested=false` or M2 #4 scope follows.

### 2026-09-17 corrected LG PRNG full-stopped hardware evidence

The [canonical full-stopped record](DEBUGGING.md#2026-09-17-corrected-lg-prng-full-stopped-acceptance)
pins the unchanged7289-byte/b53ecd58... image and own resetPC0/config26,
complete physical CODE and CPU/FMAP1 proof. The run returned0 after4,111 READY
stages and131,084 individually host-checked words: four32,767 periods,
all65,534 valid states per clock,8 seed loads and heartbeat4, without
midperiod reset.

At history observation1969 (RC16 seed3, run2/index29984/total62759),
previous IRCON0, raw C0 and live128 recorded a real `live-after-c` race.
All4,114 observations retained ordered history, including2,145 later
observations through XOSC32, END and probe; final C/live IRCON80 and every
other flag matched. STIF was neither cleared nor written; this is not a
wrap/event counter or frequency measurement.

The genuine probe at16DB/SP4F/frame75 1A used output00FF, limit16,
control3F, RND0003 and fault-before-call0. It reached expected terminal
FAULT016C/config26, reason7/probe6,6,6/fault6, preserving the full caller
batch and probe9669 sentinel, with complete CPU context and no cleanup or
automatic recovery. The retained return-RC diagnostic was2 raw ticks,
1 poll/timebase0, rollback NOT_ATTEMPTED8; earlier XOSC timing is not retained.

The parent closed that invocation before the separate `--mode full`
reset-recovery below. This stopped FAULT is historical, not the final state
of the same installed image; the stopped run alone did not establish recovery.

### 2026-09-17 corrected LG PRNG full-reset recovery hardware evidence

The [canonical recovery record](DEBUGGING.md#2026-09-17-corrected-lg-prng-full-reset-recovery-acceptance)
pins the same7289-byte/b53ecd58... image and a distinct own-resetPC0/config26
epoch, complete physical CODE proof and full CPU/FMAP1 preservation.
The separate `--mode full` invocation returned0 after4,111 READY checkpoints,
131,084 individually checked words, four complete32,767 periods/all65,534
valid states per clock,8 seed loads and heartbeat4. No probe was executed;
`probe_context` was null. Fresh reset, not cleared C state or automatic
continuation, established counter/fault/flag recovery.

At observation1859 (RC16 seed3, run2/index26464/total59239), previous IRCON0,
raw C128 and live128 recorded source `c-snapshot`. This differs from the
stopped run's observation1969/C0/live128 `live-after-c` race. The recovery
history had4,112 observations and2,253 subsequent preserved observations;
initial flags were `[0,0,5,0,0,0,0,0,0,0]`, final C/live flags
`[0,0,5,0,0,0,0,0,0,128]`. No other flag changed; no flag clear or compare
write occurred. Neither record counts timer events/wraps or establishes frequency.

**Final LG: halted ENDREADY016A/config26/RC16**, wirev2 END5/run5,
result/seedresult/fault0, probe0/0/0, hardware0003 and C/live IRCON80.
Return-RC diagnostics were2 raw ticks/1 poll/timebase0/rollback NOT_ATTEMPTED8.
No resume followed ENDREADY. The canonical record retains the complete final
wire/shared state; its reported1170.138642461 host seconds are not PRNG
throughput or calibration.

**This completes the bounded LG short/stopped/separate-reset recovery gate.**
Generic/EOC1/physical stuck/late/poll faults, entropy/RF/security/sleep and
broader M2 #4 remain open. Parent offline434/memory/all sixteen older-image
evidence and the18-job/seven-artifact/`hardware_tested=false` policy are
unchanged; no hosted-CI pass is claimed.

## M2 isolated passive RX automated coverage

`make test-radio-rx` exercises the real RX/timebase C sources on host and in
a strictly pinned, simulator-only SDCC image. The
[contract, detailed cases and resource accounting](RADIO_RX.md#offline-evidence)
record 151,177 host cases and33 linked synthetic traces, including every
actual MMIO event, destructive RFD reads, CODE/ABI mutation rejection,
XDATA/IRAM alias, unallocated-memory and upper-stack guards.
The existing 512-byte component budget and 15-second process timeout remain.
This supplies no silicon RX, calibrated timing/metadata, physical FCS or
on-air evidence. The later RX board fixture below is separate from this
component test; all earlier hardware records retain their original scope.

## Bounded passive RX board fixture offline coverage

The [fixture contract and exact artifacts](RADIO_RX.md#bounded-passive-rx-board-fixture)
separate RF-capable `radio_rx_fixture` from the older non-RF images.
Focused checks execute real startup, clock and RX on both boards:
1,006 strict host checkpoints,67 complete linked checkpoint traces, complete
CODE-byte mutation rejection, independent MMIO/instruction/CDB/listing/ABI
analysis and full65535-poll failure in33 genuine continuation segments.
The shared host trace is not an independent RF oracle. Min/max frame/footer,
BAD_CRC nonpublication/reuse, clock/controller/count/readback/entry failures,
timeout, STIF growth/deassertion,16-attempt END and terminal retention are
covered without successful service stubs or ROM patches.

The upper128 IRAM bytes remain `C7`, unallocated XDATA remains `A5`, aliasing
and stack unwind remain checked. Every s51 process keeps the original15-second
limit and heavy simulator jobs run serially. Only the new fixture has a1024
reservation budget; the standalone foundation and all older512-budget
negative tests remain unchanged. Its observed MMIO-stop peak SP71 is not a
worst-case stack proof. Full physical RX timing, calibration and radio behavior
are not modeled by C52.

The decoder/runner tests use synthetic backends and temporary private paths.
They reject malformed state/frame/ABI/artifacts, bad arguments before USB,
unsafe capture locations/permissions, overwrites/symlinks and every synthetic
transport boundary; failed cleanup cannot print successful JSON.
The host suite also runs under ASan/UBSan. All18 older complete BIN sizes and
SHA256 values were independently compared after rebuilding/image checks;
none changed. The FSCAL1 correction adds25 CODE bytes to each RX image;
allocated RAM, C/wire ABI, exactly-once RFD sites and all other MMIO rules are
preserved. The new complete hashes and independently inspected mask/compare
sites are [recorded with the ABI](RADIO_RX.md#wire-v1-and-linked-abi).
No repeated full20-job local matrix or hosted-CI pass is claimed.

No physical target or USB device is used by these checks. The
[manual gate](DEBUGGING.md#parent-only-passive-rx-acceptance) requires separate
authorization, verified recovery conditions, reset/full physical CODE
comparison and private comparison with an independent channel15 sniffer.
The separate [bounded LG record](DEBUGGING.md#2026-09-18-lg-bounded-passive-rx-acceptance)
now covers reception/body agreement and pre-RF-timeout reset recovery.
Independent on-air FCS, calibrated timing/metadata, generic hardware and
broader controller-fault recovery remain open.

### FSCAL1 correction and parent-observed failure

The [dated processed hardware record](DEBUGGING.md#2026-09-18-lg-rx-fscal1-failure-and-probe)
is a **failure/probe**, not a successful RX observation. The parent identified
old-image table index8/FSCAL1 readback `30` at the exact compiled full-byte
comparison. A subsequent parent-owned separate full reset-halt and complete
CODE comparison of that frozen old image ended at PC0000/status22/config26,
without application resume. This is not corrected-image RX acceptance.
Only FSCAL1 reserved bits7:2 are now excluded from comparison,
as required by SWRU191F p.267; the whole-byte write stays00.

Core host tests add584 cases: all64 upper-bit patterns at configuration and
after E3 with successful-call reuse; all3 nonzero low values across64 upper
patterns rejected both before E3 and after calibration; and all72 bit flips
of the other nine settings rejected. Both fixture host tests also exercise
all256 FSCAL1 upper/low combinations through real startup, clock and RX.
Six new core traces and18 fixture checkpoints include confirmed00 before E3,
postcal30/FC acceptance in the synthetic model, terminal31/32/33 rejection
without RFD reads/publication, and pre-E3 low-bit rejection. Independent
trace predicates constrain those values/results in addition to replay.
The full65,535-poll board fault also observes synthetic post-E3 FSCAL1=30.
Independent instruction mutations reject changes to the index8 condition,
mask03, maskFF, private-IRAM ANL target or comparison, without relying only
on the whole-image hash. All former scenarios, bounded logs, fault/BAD_CRC
semantics, memory guards and15-second process limits remain.
The focused correction run passed the core and both board host/image/serial
simulator targets, core/both-board ASan+UBSan builds,120 relevant Python
tests, repository/local-link checks and `git diff --check`. All18 older
images were rebuilt/image-checked against the authorized complete-BIN
baseline. No hardware was accessed by these automated checks.

### 2026-09-18 bounded LG RX hardware evidence

The parent independently passed126 focused Python checks, both1,006-checkpoint
host fixtures and both complete image proofs before programming. Full optional
environment tooling discovery subsequently passed462 tests,18 explicit skips.
These are host/image checks, separate from the physical observations below.

The exact9,160-byte LG image and complete readback, private reference method,
deadline stimulus and limitations are in the
[dated hardware record](DEBUGGING.md#2026-09-18-lg-bounded-passive-rx-acceptance).
One initial CRC_OK body matched the reference. A checked deadline-RET hold
then produced actual TIMEOUT10 after105,828 raw ticks/one poll, zero
configuration writes/RF actions, unchanged output and retained terminal FAULT.
A separate reset/CODE-verified16-attempt recovery published14 bodies, each
matching exactly one concurrent reference record. Attempts4/16 were BAD_CRC,
preserved the full `A5` output and did not latch a fault; later success after
attempt4 proves bounded reuse. The next step and terminal loop retained
END016F/attempt16/completed14, RX disabled and empty FIFOs.

This does not turn the C52 model into radio evidence, validate generic hardware,
prove independent FCS or calibrated metadata, or close M2/M3. No raw packet,
identity, payload digest, dump or capture was added to repository/CI artifacts.

### RX shared-link listing regression

The first20-job fixture CI run failed only the two RX board simulations:
the standalone RX prerequisite in `all test` relinked the common `radio_rx.rel`,
replacing `radio_rx.rst` with standalone addresses before the board checker
consumed it. Both exact board images still passed their full CODE/ABI proof.
Focused board-only runs had not exercised that link order.

Each RX link now immediately preserves an image-specific relocated listing;
both strict checkers use their own snapshot without a shared-file fallback.
The common comparison rejects missing, changed, reordered and duplicated
instructions. A real-SDCC regression builds both boards in fresh temporary
directories, links board then component, reverses the order, and requires
both preserved listings to keep matching only their respective genuine images.
This changes test artifacts, not firmware, simulator limits or hardware evidence.
The seven-file CI whitelist is unchanged.

## M2 quiescent radio FIFO automated coverage

`make test-radio-fifo` runs strict host C and the isolated
`radio_fifo_test.ihx`; the executable is **never a board image or flash input**.
Both board configurations passed full IRQ-fixture `all test`, retaining
355 Python regressions and the original clock/timebase/IRQ/MAC checks.
An additional rejection test brings the Python suite to 356 tests and enforces
the FIFO driver's exclusion from every then-existing board image.
That isolated foundation added no IMAGE, CI job or board-artifact entry;
the subsequent board fixture is recorded separately below.

Host coverage includes **69,895 modeled cases**, every supported body length
and every invalid uint16 length above 125, all command/status byte pairs,
enable/control/reserved/error bytes, full/empty FIFOs, exact byte order,
delayed counts/pointers, independent RX overflow, each controller-error bit,
partial effects, deadlines/ambiguity/backward time and a stopped-counter
65,535-poll cap. Read/write logs remain 32 entries: every SFR/XREG read and
write is validated before consumption, never silently discarded or enlarged.
There is no host success default for an unmodeled XREG read.

The genuine SDCC 4.2.0 link is **5,154 CODE bytes**, including **3,042 driver
bytes**, with **325 ordinary XDATA**, an 8-byte test result inside the full
64-byte M0 reservation (**389 total reserved nonaliased bytes**), stack start
`40`/initial SP `3F`, and **192 reserved stack bytes**. The diagnostic ABI is
21 bytes (two-byte XDATA output pointer, three-byte generic body pointer).
The driver object itself accounts for 78 XDATA bytes, 31 DATA bytes,
15 overlay bytes and three bit variables; overlays are not extra private RAM.
There is no persistent epoch, allocation or radio-RAM pool.

**99 linked synthetic scenarios** execute the unchanged reader, FIFO driver
and CODE/XDATA caller paths. Checks reject every changed driver instruction
byte, wrong SFR/XREG/strobe operands, linked-listing mismatch, pointer/return/
diagnostic ABI changes and invalid allocation/stack/alias layouts. Every
executed MMIO instruction/address/value, all diagnostic bytes, body/input
preservation, unrelated SFR/config/flag state and radio-RAM guards are checked.
The `1F00..1FFF` IRAM alias is explicitly installed and tested; upper IRAM and
unallocated/status-reservation guards stay strict.

s51 C52 has **no CC2530 radio/FIFO/CSP model**. The test explicitly supplies
synthetic count/pointer/status/time observations around genuine instructions;
it neither patches ROM nor claims physical RF, silicon FIFO behavior or timing.
It separately proves a count alone, late effect or requested strobe cannot
produce success. The mixed-address-space caller uses explicit generic-pointer
assignments; both CODE and XDATA payload bytes are actually executed/checked.

**Separate hardware evidence:** the
[non-RF board fixture](DEBUGGING.md#2026-09-17-lg-compiled-c-fifo-acceptance)
now establishes bounded LG empty/TX-clear/preload and terminal-timeout behavior
after explicit programming and full physical CODE verification. This is not
evidence from the standalone test executable or the old IRQ image.
Error-latch recovery, RX flush, RX/TX enable,
synthesizer/ACK operations, DMA, radio interrupts, MAC and networking remain
outside this slice; earlier LG hardware records do not validate or authorize it.
Full M2 #4 remains open.

All ten board configurations were rebuilt, host-checked, image-checked and
alias-aware board-simulated. Their complete BIN lengths and SHA-256 hashes
remain identical to published `cdbae7686d0f55d724d9ef5f0ad19e65841c18e3`,
including both IRQ fixtures; no board map contains a FIFO-driver symbol.
That foundation left the published timebase, clock and EA source/header files
and then-ten-job CI configuration unchanged. Historical hardware evidence is
not rewritten by the subsequent fixture below.

## M2 quiescent radio FIFO board fixture coverage

The separate `radio_fifo_fixture` composes the unchanged published drivers.
The [ABI/checkpoints/manual procedure](DEBUGGING.md#quiescent-radio-fifo-board-fixture)
centralize its scope and full hashes. Both new images retain real-driver host,
genuine linked-image and alias-aware synthetic evidence. The LG image also has
the separate bounded hardware record below; generic remains hardware-unobserved.
Older records and images are unchanged, and full M2 #4 remains open.

Host tests execute 257 cycles, both payload address spaces, all 126 maximum
readback-corruption positions, retained clock request/rollback failure,
written-but-unverified timeout, controller error, stopped-count poll cap,
ownership/phase errors and immutable terminal failure. Every read/write log
entry is validated before consumption; the 32-entry bounds are unchanged.
The Python suite includes synthetic runner/decoder coverage for byte shapes,
257-cycle wraps, matching-image/CODE/authorization gates, every normal and
negative operation-failure boundary, bad live helper/caller/argument contexts,
misleading hold-only results, CPU/M0 changes and cleanup failure.

Linked checks pin every board instruction/constant byte and reject mutated
state/pointer/layout/source records. They separately decode fixture MMIO,
require passive reads at the exact allowed SFR/XREG addresses, retain the
original clock/timebase contracts and prove relocation-equivalent FIFO code.
The genuine inlined foreground call chain is checked, including its live
deadline RET and three nested return frames. An initial non-inlined candidate
hit IRAM `80..81`; fixture-only inlining removed that frame, without modifying
the published drivers or weakening the stack guard.

s51's C52 model **does not emulate CC2530 radio/FIFO/CSP or oscillator silicon**.
Explicit event hooks supply synthetic STA, FIFO counts/pointers and RAM effects
around actual CLKCONCMD/RFD/RFST instructions. ROM is never patched. Each board
executes 257 real C cycles with all 33,410 RFD values and 33,410 exact accepted
TX RAM read addresses checked, plus terminal deadline, controller, maximum-byte,
poll-cap, flag-change and clock/rollback scenarios. The negative deadline uses
synthetic elapsed ticks, not a physically stopped clock. All unallocated XDATA,
unused M0 reservation, upper IRAM and RX/unknown-TX/address-RAM guards remain
strict; every RFST in normal fixture execution is EE, never ED or an RF enable.
The original standalone FIFO still covers its separate 99 scenarios and
69,895 host cases; standalone MAC/timebase/clock/C52-IRQ regressions remain.

All ten older board images are separately rebuilt, host/image/alias-checked
and required to retain their complete published BIN hashes. FIFO functions
and CDB source remain forbidden in those images. Only the two new board images
are added to CI, with the same seven-file artifact whitelist and no hardware
dependency. RX flush/received data, FCS generation,
controller recovery, calibrated timing and all on-air/MAC/DMA/IRQ/sleep/AES/
flash services remain separate gates.

### FIFO simulator guard regression after RX declarations

The passive-RX foundation `e4590f6` added common `_SOC_RFIRQF0/1`
declarations at `E9/91`, aliasing the FIFO fixture's existing `_RFF_` names.
Neither FIFO BIN changed. The simulator's symbol-derived `_SOC_` guard now
included these bytes, exposing an incorrect expectation in its existing
negative `flags` scenario: the harness itself deliberately injects
`RFIRQF0 AA -> AB`, but the final guard still required the original `AA`.
Both actual board images reproduced exactly this one guarded SFR delta:
`AA` at BEFORE, `AA` at clock READY, `AB` at FAULT. The real C caller
correctly retained invariant reason5 and FIFO result255 (not attempted).

The corrected harness keeps both flag registers guarded. Only the `flags`
scenario expects the exact injected `AB`; before/after-injection snapshots
require unchanged PC, ordinary RAM, all IRAM and every other SFR. `AB` must
remain at terminal FAULT. Missing injection, clearing it, any other RFIRQF0
value, a change in another guarded bit, or using the exception in another
scenario fails. Host-only rejection checks cover all eight bits of every
guarded byte in all seven scenarios and all alternate RFIRQF0 values.
Unexpected changes now report the scenario, address, expected byte and actual
byte. This is explicit synthetic input accounting, not a sticky-flag mask,
flag acknowledgment or relaxation of CPU ownership.

The complete normal scenario also exceeded the unchanged **15-second
per-simulator-process** deadline on the local host. It now executes in
11 serial segments, at most128 genuine READY observations each. All
1,286 observations and257 real C cycles, including counter wrap, remain.
Only the first segment executes startup; the next ten restore its actual
ordinary/status RAM, IRAM, all128 SFR bytes, PC and512-byte synthetic radio
region at READY. A snapshot taken before continued execution must match every
byte and PC exactly; no caller argument, counter, return value or ROM byte is
fabricated.
C52 timers must be stopped. Its reset-zero SBUF (`99`) is not rewritten,
because doing so starts a fictitious C52 UART transfer; that byte is still
compared with all the others. Verified restoration-console echoes are
separated from actual instruction/event output, so they cannot count as RFD
writes or RFST strobes. Rejection tests mutate every restored memory/radio
byte and reject incomplete snapshots, nonzero SBUF and running C52 timers.

Focused offline runs passed all seven original scenarios for **both boards**,
with all33,410 RFD bytes,33,410 read addresses and514 EE strobes checked per
normal run, original terminal-fault retention, and unchanged alias,
unallocated-XDATA, upper-IRAM `C7` and SP-unwind guards. The published generic
8,939-byte and LG8,979-byte BINs and their complete hashes remain unchanged.
This correction changes only the simulator harness and related documentation;
there is no firmware/memory/ABI or new hardware evidence.

### 2026-09-17 bounded LG FIFO hardware evidence

Both new images additionally passed parent-run `all test`, including 365
Python regressions. On the unchanged 8,979-byte LG image, all physical CODE
was verified before each runner resume. Three normal cycles and a separate
TIMEOUT with one written/unverified byte passed; a separately reset 257-cycle
run then checked 1,286 READY stages, all 33,410 written/verified/TX-readback
bytes, 514 explicit TX clears, counter wrap and CPU/M0/clock/flag preservation.
No RX flush or RF-enable strobe was exercised. The
[canonical record](DEBUGGING.md#2026-09-17-lg-compiled-c-fifo-acceptance)
contains the hash, actual bounds, partial-effect observations and current
READY/empty-FIFO state. These finite results are not calibrated timing,
received-frame, FCS, on-air or general radio recovery evidence.

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
The no-GTS Beacon extension adds shared host/SDCC vectors for both source modes,
all 36 pending-count combinations, zero/52-byte upper-layer payload boundaries,
metadata/list truncations and explicit header/payload rejection. Host matrices
exhaust superframe fields, source PAN/short and pending short addresses,
GTS/pending/header bytes and all 8,192 Beacon FCF patterns. Exact-sized input,
payload and output allocations cover the maximum 125-byte body under ASan/UBSan.
This is host-tested, image-checked and simulated serialization only: raw
superframe metadata is not a validated schedule or a discovered Zigbee network.
Its test executable is not a board firmware image or CI upload artifact.

The [R22 NWK Beacon payload decoder](NWK.md) has separate original golden,
length, reserved/protocol/version and Extended PAN ID boundary cases on
host and SDCC. Host matrices vary every payload byte through 0..255,
exercise all uint16 lengths and check exact input/output allocations under
ASan/UBSan. The standalone image reuses strict component layout/source/ABI,
512-byte XDATA reservation and alias/stack checks. The MAC test image also
executes the real two-source-mode, mixed-pending-list pipeline, including a
MAC-valid but non-Zigbee payload rejected at the NWK boundary. These checks
are host/image/simulator evidence only; no scanner, parent selection, join,
radio or authenticated acceptance is implemented.

The independent [NWK Data codec](NWK.md#nwk-data-frame-codec) has shared
host/SDCC golden, optional-IEEE, unicast/broadcast, truncation, payload/capacity
and unchanged-error cases. Host tests exhaust unicast/broadcast FCF spaces,
typed flags, both network addresses, scalar/IEEE/payload byte values and
uint16 lengths, with exact allocations under ASan/UBSan. Its standalone
image retains the 512-byte XDATA reservation, CODE/CDB/result ABI, alias and
upper-IRAM/unwind checks. The full MAC test image composes both real codecs
for all four MAC address-size and four NWK IEEE layouts at the 125-byte body
limit, including MAC-too-long and NWK-unsupported failures.
All existing test scenarios and guards remain enabled; test-only XDATA
temporaries and object ordering avoid lower-IRAM fragmentation/spills.
This is host-tested, image-checked and simulated syntax, not RF or security.

The [APS Data codec](APS.md) adds shared host/SDCC original CODE-header/payload
goldens, every FCF byte, header truncations (including explicit rejection of
short non-Data types), payload/capacity boundaries and unchanged errors.
Host matrices exhaust typed control/endpoint/counter fields, all cluster/profile
identifiers, every value at every maximum-payload position and uint16 lengths/
capacities. Exact allocations are exercised under ASan/UBSan. Its isolated
image retains the original 512-byte component reservation limit.

The new `protocol_frame_test.ihx` independently composes real MAC/NWK/APS
for all four MAC address-size and four NWK IEEE layouts, both APS ACK-request
values and empty/one-byte/maximum payloads. It checks one complete independent
golden chain, exact 125-byte MAC bodies, rejection at each outer size boundary,
truncated APS headers and MAC/NWK-valid but APS-unsupported payloads.
Its shared strict layout checker uses an explicit 1,024-byte XDATA reservation
budget for three scratch frames and linked codec storage; all existing callers
retain the unchanged 512-byte default. CODE/source/result ABI, XDATA ownership,
IRAM alias, untouched upper IRAM and final stack unwind remain mandatory.
The existing MAC scenarios and component reservation limits remain intact.
This is host-tested, image-checked and simulated syntax, not a firmware image,
ACK transaction, endpoint dispatcher, ZDO/ZCL support or hardware observation.

The [ZCL Revision 8 wire codecs](ZCL.md) have separate frame and value
host/SDCC suites, retaining the 512-byte component reservation and unchanged
CODE/CDB/alias/upper-IRAM/unwind guards. Header tests cover every FCF value
and both header layouts, receive normalization versus transmit rejection
of reserved bits, CODE inputs, lengths, capacities and unchanged errors.
They execute real APS/ZCL composition on SDCC. Value tests cover all 38
supported types, every other type ID, Boolean encodings, fixed-width and
non-value patterns and every short-string length. Four independently
initialized target phases and an invalid-selector rejection keep all cases
within the existing 15-second per-run bound.

Host matrices add exhaustive scalar/control/manufacturer fields, every byte
value at scalar/string/payload positions and uint16 spans/capacities, with
exact-sized ASan/UBSan allocations. The host protocol test now composes
actual value/ZCL/APS/NWK/MAC codecs, checking an independent complete golden
vector, both ZCL header sizes, all existing MAC/NWK address layouts, maximum
125-byte MAC bodies and per-layer failures. Its SDCC image remains the
three-layer MAC/NWK/APS chain, not a claimed four-layer target run.
This is base-text, host/image/simulator evidence only. ZCL errata 19-2019,
application selection, complete command/attribute behavior, native numeric/charset
validation and M4/M5 security/networking gates remain separate and open.

The separate Read Attributes suite adds real bounded table lookup, access
denial without touching values, ordered status/value records, namespace/side
guards, malformed-command responses, space errors and explicit prefix counts.
Golden CODE/XDATA vectors, all data type IDs, unchanged local failures,
capacity boundaries and a 16-entry table are executed on SDCC in
`zcl_attributes_test.ihx`: with declaration-range regressions, 13,579 CODE
bytes and 670 ordinary XDATA bytes, 734 with the 64-byte status reservation.
This harness uses an explicit
1,024-byte limit; existing budgets and the 15-second timeout are unchanged.
The exact reservation threshold passes at 734 and rejects 733 and the
512-byte default, without weakening other layout/alias/stack checks.

Host tests add exact table/value/request/response/result allocations,
all 16-bit attribute/manufacturer IDs, command/sequence/control bytes and
uint16 spans/budgets under ASan/UBSan. Full MAC/NWK/APS request decoding,
actual read handling and response encoding/decoding are host-tested with
an independent complete golden response and both manufacturer layouts.
The SDCC protocol image remains the previous three-layer chain. These
synthetic vectors do not implement routing, authentication, counter
allocation, endpoint registration, writes/reporting or a device profile.
Unreviewed ZCL errata remains a conformance risk, not a development stop.

Shared Read/Discover tests check declaration boundaries
`4FFF/5000/EFFF/F000/FFFE/FFFF`, including denied/unrequested invalid entries,
unchanged outputs and negative Read echo of unknown/reserved request IDs.
Host dispatch additionally tests all 65,536 declaration IDs in each namespace:
standard reserved ranges reject atomically, manufacturer-specific IDs remain
valid across the full range.

The isolated discovery/dispatch image exercises the real dispatcher,
Read handler and frame/value codecs with CODE/XDATA tables. It uses
16,428 CODE bytes and 800 ordinary XDATA bytes, 864 including the reserved
status block, within an explicit 1,024-byte harness budget. The exact layout
threshold passes at 864 and rejects 863 and the 512-byte default.
No existing component budget, alias/IRAM/unwind check or 15-second timeout
is relaxed. The small shared type predicate leaves the value image at
6,824 CODE and 377 ordinary XDATA, within its original 512-byte reservation.

Shared cases include sorting without table mutation, inclusive starts and
pagination, zero maximum, manufacturer-specific `FFFF` termination,
unreadable metadata without backing-value access, atomic errors, Read dispatch,
malformed/extended standard and manufacturer payloads, namespace/direction checks and
no reply to Default Response or unsupported Write No Response.
Host tests exhaust 16-bit start/manufacturer IDs, maximum/table-size/budget
matrices, control/command and Default Response command/status pairs, type
IDs, uint16 lengths/capacities and exact allocations under ASan/UBSan.
The full host MAC/NWK/APS/ZCL chain discovers an attribute, decodes the
returned ID and reads it through the dispatcher, with independent complete
golden responses and both manufacturer layouts. The target protocol image
is unchanged and remains three-layer; radio/platform code and board images
are unaffected. This is not network admission, transaction matching or
full-cluster conformance.

The additional `protocol_budget_test.ihx` runs **all seven implemented
protocol modules together**, not only the three-layer target above.
Ten exchanges cover Discover-then-Read using the returned ID, both ZCL
manufacturer layouts, independent standard golden response frames, the
exact 125-byte MAC body, space-error replies and negative Read echo of absent
`FFFF`. Unsupported Write No Response leaves the reply untouched.
Two further full-chain requests check atomic Read/Discover rejection of a
CODE table declaring standard `FFFF`. The shared native harness
also runs under ASan/UBSan, alongside the existing exhaustive MAC/NWK/APS,
Read/Discover and host protocol suites after the IRAM-storage refactor.

The image uses 22,829 CODE and 1,500 ordinary XDATA bytes, 1,564 including
the 64-byte reservation. Its explicit 2,048-byte budget preserves the
unchanged component layout/source/CODE, alias, untouched-XDATA/upper-IRAM,
unwind, disabled-interrupt and 15-second guards. The eight-byte result ABI
is `PBG1`, version 1, size 8, failure line LE16. Stack starts at `0x66`;
the simulator's maximum-SP evidence is `0x7A`, leaving five bytes before
the guard, not a worst-case/ISR guarantee.
These affected Read/dispatch/integrated metrics and guards pass on both
`generic` and `lg_esl29_rev03` with SDCC 4.2.0 #13081 (Mac OS X x86_64).

`tools/protocol_resources.py` checks relocatable module/ABI/area records,
per-module budgets, linked shared-runtime remainder and nonadditive overlay/
bit accounting before the runner writes `protocol-resources.json` with
artifact hashes. Synthetic unit tests exercise malformed/unknown/absolute
areas, wrong modules/ABI, missing/extra modules, resource discrepancies,
each module at its exact limits, whole-image limits at/above the threshold,
invalid stack peaks and removal of stale output when the checker fails.
[The ledger and exclusions](ARCHITECTURE.md#integrated-protocol-resource-budget)
are resource evidence, not on-air or full-stack acceptance. No board
firmware, radio/platform source or CI artifact whitelist is changed.

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
The CI matrix covers nine non-RF images (`bringup`, `debug_fixture`,
`timebase_fixture`, `clock_fixture`, `irq_fixture`, `radio_fifo_fixture`,
`dma_fixture`, `aes_fixture`, `prng_fixture`) plus the RF-capable passive-only
`radio_rx_fixture` on both boards: twenty offline jobs. Artifacts contain only the
explicitly selected board image's generated firmware, symbols and build
metadata. Pull requests must not use privileged `pull_request_target` execution
to build untrusted source.
The artifact-path whitelist is tested; neither standalone component executable
nor host test binaries/logs are uploaded.

CI success means the declared automated checks passed. Experimental releases
must separately list their completed milestones, known limitations, exact
build inputs and hardware/interoperability evidence.
