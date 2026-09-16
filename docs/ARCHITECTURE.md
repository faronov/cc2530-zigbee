# Architecture

This is the intended stack architecture. At M0 only board/platform bootstrap,
status storage and validation infrastructure exist. The layer names below
are boundaries to implement, not a list of working APIs.

The M1 target addition is a separate non-RF debugger fixture, not a protocol
layer. Its deterministic pattern logic is host-testable; its SDCC register
probe is confined to the target example. It shares existing startup/board
code rather than introducing a second GPIO policy.

The host-only `tools/cc_debugger.py` keeps USB access behind a narrow backend
interface. Session policy, exact diagnostic packets, deadlines and failure
states can be tested without importing PyUSB or enumerating devices. The
optional backend does not own board GPIO or decide reset/attach policy.

CPU-control permission is separate from permission to read an existing
debug session; state checks and the complete command exchange share one
deadline. Reset into halt has a third, separate permission, is limited to an
already prepared debug session and is never triggered by open/close or an
error. The separate `RESET_DEBUG_SESSION` access policy permits only explicit
reset-based initial attach: target operations remain denied until preparation,
reset and postchecks finish. It does not claim a non-reset attach.

Live PC/bank, register and bounded memory inspection requires a halted,
awake target. Memory access, memory writes and breakpoint configuration are
separate permissions, independent of CPU control/reset. Core SFR reads use a
fixed passive whitelist; XDATA reads are limited to `0x0000..0x1FFF`, CODE to
the lower unbanked `0x0000..0x7FFF`, and writes to ordinary XDATA
`0x0000..0x1DFF`. A memory transfer is 1..256 bytes without crossing its bound.
The public API has no flash/MMIO/status/IRAM-alias writer or banked breakpoint
interface; breakpoint slots 0..3 use bank parameter zero.

Register snapshots save A before reading PSW, account for accumulator parity,
and verify restoration. Memory operations save the full snapshot, select
DPS 0 and use DPTR0, then restore DPL0/DPH0, DPS, A and PSW on normal completion.
The verification includes both DPTRs, B/SP/MPAGE, the eight active-bank
registers and PC/FMAP bank. DPTR1 is preserved, not silently replaced by
whichever pointer DPS originally selected. Failed or late transfers stop
without a restoration attempt, retry, implicit resume/reset or stall clearing;
only explicit resource cleanup remains available on the faulted session.

`tools/cc2530_debug.py` contains target command/status facts, distinct
from USB framing. `tools/debug_image.py` handles only offline artifacts and
snapshots, reusing strict image checks and never importing the USB transport.
Its source lookup exposes exact linked CDB records, including multiple records
at one address; it does not guess source ranges or read compiler-named files.

The manual `tools/check_debug_hardware.py` is outside normal build/test
execution. It verifies physical fixture CODE before resume and has a narrowly
authorized DATA/XDATA-alias experiment; that exception does not expand the
public writer's bounds. The separate macOS `tools/erase_boundary_fault.c`
observes an external programmer's erase/status traffic and exits before
programming/normal-reset cleanup at the authorized boundary. It neither
implements a programmer nor adds implicit recovery to the debugger.
The [dated LG evidence](DEBUGGING.md#2026-09-16-lg-fixture-hardware-record)
is separate from host/image/simulator evidence. M1 is complete for the bounded
LG/unbanked baseline: a fresh-reconnect fixture check passed before the final
held-handle unplug/failure check. After the last replug, PyUSB enumeration and
a full one-cycle fixture check in an explicitly selected new session also
passed; that run ended with the fixture halted at `0x0173`.
Banked CODE remains deferred, and the protocol/platform layer boundaries
below are unchanged. This finite acceptance does not add universal compatibility
or sleeping-target, MMIO, full-SFR, flash-writer or GDB support.

## Layer boundaries

The standalone `mac_frame` module currently supplies only offline legacy
DATA/ACK body encoding/decoding and five fixed-format command payloads/frames.
Command-specific header validation is still stateless serialization, not a
procedure or association state machine. The module has no board/platform dependency or
network state and is not linked into bootstrap/fixture firmware. Its
[contract](MAC.md) separates syntax success from CRC/security/peer acceptance.

```text
sensor / local display application
              |
       attributes / ZCL
              |
            ZDO + APS
              |
     end-device NWK + security
              |
        end-device MAC
              |
   CC2530 radio / AES / time / NV
              |
       board-specific GPIO
```

The host validation build should exercise packet encoding, state machines
and persistence decisions without depending on CC2530 SFR syntax. Hardware
access belongs behind narrow platform interfaces, not in protocol parsers.

Proposed interface responsibilities:

| Boundary | Responsibility | Must not do |
| --- | --- | --- |
| Radio | Frame TX/RX, filtering, CCA, timestamps/errors | Pretend to perform Zigbee join |
| MAC | Association, ACK/retry and parent exchanges | Own application attributes |
| NWK | ED addressing, join/rejoin, parent/update state | Forward traffic or manage children |
| APS | Endpoints, transactions, ACKs and applicable security | Silently discard ownership errors |
| ZDO | Required discovery/management behavior | Advertise unimplemented services |
| ZCL | Typed attributes and implemented foundation commands | Serialize C structs directly as wire format |
| NV | Validated atomic records and monotonic reservations | Return success after a failed flash operation |
| Application | Sensor values and display scheduling | Block protocol progress during refresh |

## Awake-only timebase (first M2 slice)

`include/timebase.h` / `src/timebase.c` is an independent platform component,
not linked into `bringup` or `debug_fixture`. The separate `timebase_fixture`
board image now links it for explicit C-driver acceptance. It owns no board pins, clock
selection, interrupt dispatch, compare channel or sleep policy. M2 remains
open; this is neither an extended monotonic epoch nor calibrated wall time.

| API | Contract |
| --- | --- |
| `uint32_t timebase_read_awake_ticks24(void)` | Read ST0, ST1, ST2 exactly once in separate sequenced MMIO expressions; return the latched 24-bit value zero-extended to 32 bits |
| `timebase_deadline_after(now, delay, &deadline)` | Return `(now + delay) & 0xFFFFFF` for 24-bit `now` and `delay < 0x800000` |
| `timebase_expired(now, deadline, &expired)` | For valid 24-bit inputs, masked `now - deadline` below `0x800000` means expired (including equality); above means pending; exactly half-range is ambiguous |

Both arithmetic functions return `timebase_result_t`: `TIMEBASE_OK`,
`TIMEBASE_INVALID_ARGUMENT` for out-of-range inputs/null output pointers
(including a delay at or above half-range), or `TIMEBASE_AMBIGUOUS` for an
exactly-half-range expiry delta. Every error leaves the output unchanged.
Expiry writes a C99 `bool`; zero delay expires immediately. Inputs are values,
outputs are caller-owned writable objects; SDCC uses its generic-pointer ABI.
There is no heap, retained epoch, millisecond conversion or clock-failure stub.

The caller must maintain **true temporal separation strictly below half a
counter cycle**, in either direction, for every comparison and observe each
deadline within that window. Raw ticks cannot detect an overdue observation
outside that window, missed full wraps, reset or PM3 loss of counter state.
Masking arithmetic does not repair those violations. Discard prior deadlines
when continuity is lost; do not treat the ambiguity result as pending/success.

One foreground owner must serialize all Sleep Timer reads; neither an ISR nor
another reader may interleave ST0 reads and overwrite the latch. All helpers
are foreground-only and not ISR-reentrant under the SDCC model-large ABI.
No interrupt masking is needed under this ownership contract; the current
board baseline disables interrupts and never sleeps.

TI SWRU191F sections 11.1/11.4, pp.129-131, specify that ST0 (`0x95`)
latches the complete counter, while ST1 (`0x96`) and ST2 (`0x97`) return its
latched middle/high bytes. Writes program compare, so this component never
writes those SFRs or any GPIO/clock/IRQ register. The counter starts after
reset, uses the current 32-kHz RC/XOSC source and runs except in PM3, which
loses its value. PM1/PM2 wake requires a positive 32-kHz edge observed through
`SLEEPSTA.CLK32K` before current values are reliable. This awake-only API does
not implement that synchronization, sleep entry/exit, clock switching or a
precise tick rate. These are caller preconditions, not runtime-detected states.
See [sources](PROVENANCE.md#m2-timebase-sources) and the separate
[host/image/simulator evidence](VALIDATION.md#m2-awake-only-timebase-automated-coverage).
The [independent hardware reference](VALIDATION.md#independent-sleep-timer-hardware-reference-2026-09-16)
uses supplied debug instructions on the unchanged M1 fixture; it does not
execute this C module or establish calibrated timing.

The [board fixture](DEBUGGING.md#awake-only-timebase-board-fixture) separates
host-testable orchestration in `src/timebase_fixture_state.c` from target-only
NOP/fault-loop checkpoints in `examples/timebase_fixture.c`. Initialization
reuses M0 startup/status and the selected board policy. Each foreground cycle
samples a start, constructs a fixed 128-raw-tick deadline, then performs at
most 1,024 polls. READY is published only after successful helper status,
bounded elapsed time, completed-cycle increment and M0 heartbeat update.
Invalid/ambiguous helper results, backward/out-of-window observations and
exhaustion publish a latched FAULT, with no implicit retry. Only explicit
initialization/reset clears it. There is no ISR caller, clock selection,
Sleep Timer write, host callback or successful substitute in the board image.

The 32-byte fixture ABI uses ordinary linker-accounted XDATA, not unused M0
status reservation or IRAM alias space. Reader scratch addresses relocate in
this image; the verifier checks their exact CDB declarations/addresses and
each corresponding MOV DPTR operand, while preserving the standalone test's
original 88-byte reader contract. The manual runner uses existing guarded
debugger APIs with read/reset/CPU/breakpoint permissions only. It verifies all
physical image CODE before resume and observes matched checkpoint symbols;
it does not expand the core-SFR whitelist or add a host RAM/flash writer.

The [2026-09-16 LG acceptance](DEBUGGING.md#2026-09-16-lg-compiled-c-timebase-acceptance)
executed this C module and fixture logic on the verified 1,847-byte board
image: 257 successful cycles, elapsed 129..130 raw ticks for requested 128,
37 polls each, with cycle/heartbeat wrap and CPU/M0 preservation. That run left
the timebase fixture halted at READY `0x016A`; later clock-fixture work is
recorded separately below. Generic hardware remains unobserved, and fault paths
remain host/simulator evidence. This does not add calibration, natural
24-bit rollover in the C run, clock switching, IRQ/compare/wake or other M2
services; the earlier register-only experiment remains separate.

## Init-time system clock selector (isolated M2 slice)

`include/clock.h` / `src/clock.c` adds original synchronous
`clock_select_init(source, timeout_ticks, poll_limit, &diagnostics)`, using
the real CLKCONCMD/CLKCONSTA and existing timebase. The initial isolated slice
did not link it into any board image; a subsequent separate `clock_fixture`
now does, without changing any of the six older board BINs.
The original clock board image passed one LG normal sequence but exposed a
[pending-cancellation race](DEBUGGING.md#2026-09-16-lg-clock-cancellation-failure).
The revised driver has host/image/simulator coverage plus separate
[2026-09-17 (UTC+03) bounded LG compiled-C acceptance](DEBUGGING.md#2026-09-17-lg-compiled-c-clock-acceptance):
both induced timeouts confirmed rollback without masking the original error,
and a separately reset recovery run completed 257 sequences / 771 C calls.
Generic remains hardware-unobserved. This does not confirm never-departed
cancellation or establish frequency/calibration and other M2 services.
The earlier LG timebase record remains separate; M2 #4 stays open.

`source` is `CLOCK_RC16` (0) or `CLOCK_XOSC32` (1). The raw timeout must be
less than `0x800000`, the 16-bit poll limit positive, and the diagnostic
pointer non-null and writable. Pure argument errors return before MMIO and
preserve the entire output. For valid arguments, diagnostics are initialized
and entry state is observed without writes. Required conditions are:

- Awake bootstrap/initialization, no prior unsynchronized sleep/wake or lost
  Sleep Timer continuity; one foreground owner of CLKCONCMD and ST0 reads.
- IEN0, IEN1 and IEN2 all zero; SLEEPCMD.MODE zero and its required reserved
  bit 2 set. No interrupt masking, sleep-command repair or oscillator setup.
- A known undivided source: CMD.CLKSPD `001` for RC16 or `000` for XOSC32,
  with actual STA matching all effective fields and no pending clock request.

Current MODE is not evidence of wake history. The caller must guarantee that
history and exclude concurrent writers/readers throughout both attempts;
these are not detectable/repairable ownership conditions. SDCC model-large
helpers and compiler scratch are foreground-only, not ISR-reentrant.
Matching entry CMD/STA alone cannot exclude a hidden pending request.
After a failure, an idempotent call is not a cancellation/recovery test.

The selector preserves CMD.OSC32K and CMD.TICKSPD, changes only OSC/CLKSPD,
and never writes SLEEPCMD, ST0-2, GPIO, IRQ or calibration registers.
RC clamps TICKSPD command `000` to effective STA `001`; entry and confirmation
accept that documented difference, not arbitrary CMD/STA mismatches.
An already selected, genuinely stable source succeeds without any write or
Sleep Timer sample. Otherwise it samples a start, constructs one raw deadline,
then writes the target CMD exactly once. Each poll reads CMD then actual STA,
then samples ST0/ST1/ST2. It never infers stability from CMD or SLEEPSTA.
STA confirmation timestamped exactly at the deadline is accepted; a later
confirmation fails. Pending at the deadline fails, including a zero timeout.
Zero timeout can succeed only with same-tick confirmation. The final permitted
poll can succeed, but an exhausted cap never wraps or reports success.

Every observation must remain within the timebase's true half-range window.
Exactly-half deadline ambiguity is a helper error; elapsed or successive
sample deltas at/above half range are rejected as counter range/backward
movement. Missed full wraps, resets that resemble forward progress, and an
unexecuting CPU cannot be detected/recovered by this synchronous interface.

| Return | Meaning |
| --- | --- |
| `CLOCK_OK` (0) | Stable requested settings confirmed within bounds, or valid idempotent entry |
| `CLOCK_INVALID_ARGUMENT` (1) | No MMIO; output unchanged |
| `CLOCK_UNSUPPORTED_STATE` (2) | Unsupported entry IRQ/power/CMD/STA state; no request/write |
| `CLOCK_TIMEOUT` (3) | Pending at deadline or confirmation observed late |
| `CLOCK_POLL_LIMIT` (4) | Independent finite poll cap exhausted |
| `CLOCK_TIMEBASE_ERROR` (5) | Deadline/expiry helper error; raw helper status retained |
| `CLOCK_COUNTER_RANGE` (6) | Backward/out-of-window elapsed or observation step |
| `CLOCK_COMMAND_CHANGED` (7) | CMD no longer matches the owned request |

After **any post-request failure**, the driver writes the saved known
undivided CMD exactly once, even if rollback deadline construction fails.
Rollback has a fresh start/deadline and the same poll cap. **A matching old
STA is not cancellation confirmation:** the pending requested source can
still appear after that observation. The driver tracks whether STA.OSC ever
reported the originally requested HF source, including observations before
time/error checks or after cancellation. Only that source observation followed
by a subsequent complete saved-settings match within the rollback bounds
permits rollback `CLOCK_OK`. Source-bit evidence deliberately accepts the
intermediate XOSC/CLKSPD=1 status `89`; it is not full restoration.

If the requested source was never observed when the rollback deadline or poll
cap expires, `CLOCK_ROLLBACK_UNCONFIRMED` (**9**, appended after unchanged
`CLOCK_NOT_ATTEMPTED=8`) reports bounded cancellation uncertainty. A truly
canceled request that never transitions, an unseen departure/return between
polls, and a still-pending request cannot be distinguished by old STA alone.
No number of old matches or fixed grace delay substitutes for source evidence.
If departure was observed but return was not confirmed, ordinary timeout/cap
errors apply. Helper, range and command errors retain their specific codes.
All these outcomes are unconfirmed restoration, not permission to continue.

It never retries the original request, resets the chip
or converts a fallback into success: the return remains the original failure.
`diagnostics.rollback_result` is `CLOCK_NOT_ATTEMPTED` (8), `CLOCK_OK` for
the observed departure/return sequence, 9 for the uncertainty above, or its
separate failure code. A failed rollback is
**unconfirmed clock state**, even when its last observed settings look correct;
the caller must stop clock-dependent initialization and report both failures.
There is no implicit retry, continued initialization or recovery policy.

Diagnostics contain two `clock_wait_diagnostics_t` records (`request`,
`rollback`), each with `uint32_t elapsed_ticks`, `uint16_t polls` and
`uint8_t timebase_status`, plus saved/requested/last-observed CMD, last-observed
STA and rollback result bytes. Poll counts exclude the start sample. Initialized
zero elapsed/count/helper status does not mean a helper ran. Final CMD/STA are
the last complete observation, including on failure, not a fabricated desired
state. This is a caller-owned C object, not a portable serialized ABI:
SDCC lays it out in 19 bytes (phase offsets 0/7, command/result offsets 14..18);
host padding may differ. The isolated test checks all target bytes/field offsets.
No heap, retained epoch or persistent driver state is added. One private
per-call source-evidence byte is cleared before a non-idempotent request and
shared by the two attempts; it does not change the 19-byte diagnostic ABI.

With a cap `P`, at most `2*P` polls, `2*(P+1)` Sleep Timer samples and two CMD
writes occur across request and rollback. Each attempt gets its own raw
timeout; setup, observation and polling granularity add bounded instruction
overhead, not a hard calibrated wall-time limit. A stopped Sleep Timer still
terminates through the poll cap while the CPU executes.

TI **SWRU191F pp.64, 66-69** specifies source selection/confirmation, divider
clamping and automatic calibration effects. Preserving TICKSPD does **not**
preserve its effective clamped timer frequency across an HF change.
Source switching aligns with TICKSPD. Selecting XOSC32 automatically calibrates
RC16; when LF RC is selected and calibration is enabled, LF RC calibration
can take up to 2 ms and add one Sleep Timer tick. HF selection confirmation
does not establish completion/precision of LF calibration. This driver
implements no calibration procedure, LF source change, wake synchronization,
timer compare, interrupt dispatch or runtime clock-management service.
See [source facts](PROVENANCE.md#m2-system-clock-sources) and
[offline evidence/manual gates](VALIDATION.md#m2-init-time-system-clock-automated-coverage).

The [clock board fixture](DEBUGGING.md#init-time-clock-board-fixture) separates
56-byte byte-oriented state/serialization and foreground C orchestration from
target-only NOP/RET/fault-loop checkpoints. It repeats RC16 idempotence, XOSC32
and RC16 only on success, with fixed 1,024 raw ticks and 4,096 polls per attempt.
Any original driver failure remains terminal despite successful rollback.
M0 clock values stay immutable startup evidence; current CMD/STA and
SLEEPCMD/IRQ snapshots are separate fixture fields. The later cancellation
fix changes only rollback evidence/error semantics, not the fixture sequence
or an application clock-management service.

The manual runner verifies all physical CODE before resume and uses read-only
inspection plus existing reset/CPU/breakpoint permissions. Its optional timeout
stimulus is a halt at the unchanged deadline helper's strictly verified shared
RET, before the clock request. Complete linked bytes, success-return ABI,
caller continuation and live stack/DPL distinguish that boundary from an
unrelated/error return. It disables the breakpoint before allowing the real
request/rollback, and accepts the negative experiment only from actual TIMEOUT
and confirmed restoration. An uncertain cancellation fails that acceptance
check explicitly, even if CMD/STA presently match. A separate
`--induce-late-timeout` holds after the verified actual CMD write and verifies
C-observed requested source and the private evidence byte before the poll's
Sleep Timer call. It then requires actual late TIMEOUT and confirmed return.
There is no RAM/ROM injection or generic SFR access. On the corrected LG image,
the original pending-cancel test confirmed rollback after 64 raw ticks /
15 polls, not the old false 3-tick / 1-poll match. The late-source case confirmed
return after 3 ticks / 1 poll, with prior real C source evidence. Both retained
TIMEOUT and terminal FAULT; a separate explicit reset then completed recovery.
This finite hardware record does not turn old matches into proof of cancellation:
never-observed departure still requires result 9 or another explicit error.

## Memory contract

| Address space | Meaning |
| --- | --- |
| XDATA `0x0000..0x1EFF` | 7,936 bytes of SRAM distinct from IRAM |
| XDATA `0x1F00..0x1FFF` | Alias of the 256-byte IRAM; not extra storage |
| IRAM | Register banks, compiler data and stack |
| CODE | 64 KiB CPU view with CC2530 FMAP banking for larger flash |

M0 reserves `0x1E00..0x1E3F` for at most 64 status bytes. Ordinary allocation
ends below `0x1E00`; unused space above status is not an implicit allocation
pool. The linker/map checker and alias-aware simulator enforce this.

The M1 fixture retains that exact reservation and M0 status ABI. Its separate
16-byte `debug_fixture_state` lives in ordinary, linker-accounted XDATA below
`0x1E00`; its address is looked up in the matching image's map, not hardcoded.
The existing 512-byte nonaliased-XDATA reservation budget still applies.

Do not clear an XDATA object at `0x1F00`: this can overwrite the very register
holding its loop index and the active return addresses. A generic 8051
simulation with separate IRAM/XDATA will miss that failure.

C rules:

- Use fixed-width integers and explicit byte encoders for wire formats.
- State byte order, length and ownership at every protocol boundary.
- Keep constant tables in CODE; do not copy fonts or descriptors into RAM
  unnecessarily.
- Use bounded static queues/pools with explicit exhaustion behavior.
- Treat SDCC's data models, pointer spaces, register allocation and reentrancy
  as part of the ABI, not as interchangeable desktop-C implementation details.
- Any banking support must handle calls, interrupts, constants and debugger
  addresses together. It is not only a linker flag.

## Scheduling and ownership

Use a cooperative foreground state machine with short, bounded work items.
Interrupt handlers capture minimal state and enqueue work; they must not call
non-reentrant foreground helpers or run ZCL/display processing.

Every queued object has one owner. Timeouts/retries and cancellation need
explicit transitions so a reset, leave or parent change cannot use stale
buffers or complete an operation twice.

The initially awake ED and later SED share protocol behavior. Sleep is permitted
only when radio/APS transactions, timers, NV and application activity agree.
Fast polling during transactions is different from normal background polling.

Display work is scheduled in short slices. Sending a command/row is distinct
from waiting for the controller; BUSY waits become scheduled deadlines rather
than long CPU loops. Errors always lead to a defined power/control state.

## Persistence design

Reserve explicit flash pages outside code, factory/configuration data and lock
locations before introducing any writer. Define record version, generation,
length, integrity checks and commit semantics.

Persist network/parent information, required keys and counters, then add
bindings/reporting settings only when their behavior exists. Corrupt or
incomplete records are rejected with a visible recovery reason.

After persisted resume, select an immediate keepalive from the saved
`nwkParentInformation`. Unknown parent information triggers ED Timeout
renegotiation with bounded recovery; a missing response must not block startup
forever. Test this separately from a fresh join.

For outgoing security counters, reserve a durable future range before using
it. A restart may skip values; it must not reuse transmitted values. Validate
this under interrupted writes and network leave/factory reset, not only under
orderly shutdown.

## First board

The LG ESL board is an application example, not the protocol architecture.
Display pins and their power sequencing stay in board/display code.

P1.6/P1.7 are exposed UART pads and candidates for future software I2C after
UART ownership is disabled. Existing NFC wiring and supply arrangements must
be considered before reusing its bus. P2.1/P2.2 remain available for debugging.

Motherboard straps describe the motherboard configuration. They do not detect
the type of a display that someone has swapped onto the connector. No unknown
panel gets an automatic voltage/LUT/profile fallback.
