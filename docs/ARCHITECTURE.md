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

The API-only [DMA-enable debug gate](DEBUGGING.md#guarded-dma-enable-after-reset)
has its own explicit permission, requiring reset permission and one fresh
own-reset eligibility. It permits only verified `26 -> 22`, with PC0 and
unchanged FMAP (not FMAP0); full physical CODE verification remains the manual
caller's responsibility.
Passive inspection may precede it; execution, contradictory observations or
an enable attempt consume eligibility. It grants no DMA/MMIO access and adds
no automatic configuration change to existing runners. Its separate LG
hardware evidence is not DMA-controller or AES acceptance.

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
DATA/ACK body encoding/decoding, five fixed-format command payloads/frames and
version-0 Beacons without GTS descriptors. Beacon views bound and expose
pending-address/upper-layer spans without copying or interpreting Zigbee
discovery data; superframe fields are raw metadata, not a validated schedule.
Command-specific header validation is still stateless serialization, not a
procedure or association state machine. The module has no board/platform dependency or
network state and is not linked into bootstrap/fixture firmware. Its
[contract](MAC.md) separates syntax success from CRC/security/peer acceptance.

The separate `nwk_beacon` module decodes only the 15-byte R22 NWK information
inside the returned upper-layer Beacon Payload. It has no dependency on MAC
headers or platform code. The [NWK codec contract](NWK.md) preserves raw
profile/capacity/depth/update metadata without compatibility or parent
acceptance. It adds no firmware caller or network state machine.

The independent `nwk_frame` module encodes/decodes only the bounded,
unsecured R22 Data NPDU: fixed addressing/radius/sequence fields, optional
IEEE addresses and opaque payload. Unsupported security, multicast and
source-route layouts fail explicitly. It does not decrement radius, assign
sequences, validate identities, process APS or decide network membership.
Its offsets start at the NPDU, not the MAC body. All three codecs are composed
only in offline tests, including each layer's independent size/error checks.

The independent `aps_frame` module handles only unsecured Data with
normal-unicast delivery and an eight-byte header. Endpoints, profile/cluster
IDs, counter and ACK-request are metadata; payload is opaque. Unsupported
types, broadcast/group delivery, security and extended headers fail explicitly.
The [APS contract](APS.md) separates the raw 108-byte APDU cap from future
service/security limits and endpoint/transaction policy. A separate simulator
image composes MAC/NWK/APS without expanding the existing MAC test image.
APS introduces no dispatcher, ZDO handler or board caller.

The separate `zcl_frame` and `zcl_value` modules share the typed
`zcl_wire.h` API but no runtime state. The [ZCL wire contract](ZCL.md)
pins Revision 8 and keeps manufacturer/command/attribute policy outside
header parsing. Standard reserved RX bits are ignored and reported, not
mistaken for an invalid NWK layout. Value views retain raw wire octets and
explicit consumed spans, including non-value patterns; they do not convert
native numbers or validate text. APS/ZCL composition is target-tested
separately; full MAC/NWK/APS/ZCL/value composition is currently host-only.
The independent `zcl_attributes` module adds a caller-owned, bounded read-only
model and a unicast Read Attributes response builder. One table selects a
cluster side and standard/manufacturer namespace; duplicate IDs are rejected,
non-readable values are not inspected, and partial responses expose their
requested/returned counts. It composes the existing frame/value codecs and
publishes outputs atomically on local success. The caller must already select
permitted unicast endpoint/profile/cluster context and authenticate as required.
No endpoint registry, getter callback, write/reporting service,
device-specific cluster or board linkage is introduced. Its own target image
tests the handler; full MAC/NWK/APS request/response composition is host-only.
Complete ZCL/application support remains planned.

`zcl_dispatch` selects Read or Discover handlers for one caller-selected
cluster side/namespace. Discover scans the bounded, possibly unsorted table
without mutation or value access and emits ascending ID/type pages with an
explicit completion bit. The dispatcher builds unsupported-command errors,
delivers received Default Response metadata without replying, and rejects
unsupported Write No Response without an output frame or mutation.
It shares structural table validation and supported-type classification,
not network state. Local failures preserve outputs; the explicit result kind
distinguishes a constructed response from a received default notification.
Transport admission, cluster selection, authentication, matching transaction
state and actual sends remain outside this module. Its separate target
image exercises real dispatch/read/discovery; full Discover-then-Read
MAC/NWK/APS composition is host-only.

The planned BDB commissioning policy uses
[BDB 3.0.1 with Core R22](CONFORMANCE.md#bdb-301-requirements), above the
NWK/APS/security services rather than inside codecs or board code.
MAC association, received network key, BDB membership flag, Device Announce
and authenticated application readiness are separate states. In particular,
BDB announces before TC link-key exchange completes; an announce must not
unlock application traffic or imply verified persistent key state.

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

## Interrupt ownership foundation (isolated M2 slice)

`include/irq.h` / `src/irq.c` provides only two original platform primitives,
excluded from the eight older board images:

| API | Contract |
| --- | --- |
| `irq_state_t irq_save_disable(void)` | Atomically sample/clear EA, returning exactly its previous 0/1 state; no error case or output pointer |
| `irq_result_t irq_restore(irq_state_t token)` | For byte 0/1, restore exactly that EA state and return `IRQ_OK=0`; for 2..255, return `IRQ_INVALID_TOKEN=1` before interrupt/peripheral access |

`irq_state_t` is `uint8_t`. Wider externally decoded values must be validated
before narrowing to this API. Restore does not modify the caller's token.
Each token belongs to its saving context and must be consumed once, in LIFO
order. The inner token of an already-disabled section is zero, so restoring
it cannot enable an outer section. There is no token registry or dynamic
stack: forged valid-shaped, reused or out-of-order tokens are **not detected**.
No independent EA writer may override an active section.

TI **SWRU191F pp.41-46**, IEN0/IEN1/IEN2 and interrupt-processing descriptions,
place EA at IEN0 `0xA8` bit 7, bit address `0xAF`. The target uses `JBC EA`
to sample/clear this control bit, never a hardware pending flag. Restore
uses only `CLR EA` or `SETB EA` after byte validation. There is no whole-IEN0
write or the p.45 `XCH A,IEN0` delayed-disable hazard. All other current IEN0
bits, IEN1/2, priority, pending and peripheral registers are untouched by these
primitives. EA=0 prevents acknowledgment, not flag assertion, timer progress
or DMA; handlers and hardware can still change their own state. This is not
a peripheral lock, acknowledgment service or generalized IRQ framework.

Both target functions are `__reentrant __naked` register-only leaves under the
checked SDCC 4.2.0 ABI: byte argument/return in DPL, 11-byte save and 23-byte
restore, no calls, loops, overlay/static scratch or explicit stack pushes.
Their only normal CPU clobbers are DPL and, for restore, A/PSW parity. Those
ABI effects (including on invalid return) are distinct from the no-MMIO
promise for interrupt/peripheral state. B, DPH, DPS, carry and register banks
are not touched. Ordinary calls still need their two-byte return-address
frames and an adequate caller/ISR stack.

They may be called from foreground or ABI-correct ISRs that preserve the
interrupted CPU context, including DPL/A/PSW and any compiler scratch their
own callees use, and finish with RETI. Enabling EA can admit an interrupt
**before restore returns**; higher-priority handlers may nest. The linked
test exercises that exact window and nested calls through the same primitives.
This does not make arbitrary SDCC callees reentrant. Shared data still needs
appropriate volatile qualifiers and consistent ownership; existing clock and
timebase helpers remain foreground-only. Host MMIO is a sequential model,
not a host-thread atomic implementation.

See [source facts](PROVENANCE.md#m2-interrupt-ownership-sources) and
[validation and the separate hardware gate](VALIDATION.md#m2-interrupt-ownership-automated-coverage).
The primitives themselves provide no dispatch, flag acknowledgment or priority/
source setup. A subsequent board fixture and its bounded LG acceptance are
described below; generic hardware and broader IRQ services remain open with
M2 #4.

## Timer1 IRQ fixture ownership

`src/irq_fixture_state.c` and `examples/irq_fixture.c` are a separate
`IMAGE=irq_fixture`, not a general Timer1 driver or a change to `src/irq.c`.
The fixture requires fresh awake reset, unchanged M0 GPIO policy with all
startup PxSEL bytes zero, C9/C9 undivided RC16 settings, no other enable/writer,
inactive Timer1 channels (`T1CCTL0..4=40`), zero counter/source/T1 CPU flag,
and the reset TIMIF overflow mask set. It observes channels 3/4 through
read-only XREG `62A3/62A4`, not an implicit ordinary-XDATA allocation.
No DMA registers are accessed: debug config `26` includes DMA_PAUSE, for which
SWRU191F p.55 prohibits those accesses. No channel mode/output, routing,
clock, sleep, priority or TIMIF register is written.

Each cycle checks save/restore with EA initially zero and invalid token FF.
Explicit fixture policy then enables EA, obtains outer token 1 and inner
token 0, enables only IEN1.T1IE, clears the counter through T1CNTL and constructs
a raw deadline. Timer1 is started in free-running DIV=1 mode (`T1CTL=01`).
Pending overflow/CPU flags are polled with EA=0; the counter is suspended
(`T1CTL=00`) before the PENDING checkpoint. Inner restore(0) must leave the
event undelivered. Outer restore(1) permits the real vector-9 ISR.

The 53-byte generated ISR saves A/DPL/DPH/bank-0 R7/PSW, masks IEN1.T1IE,
captures T1STAT/IRCON, writes **T1STAT=1F**, increments its byte count, restores
context and executes RETI. IRCON.T1IF is H0 on hardware entry, never written
by this code. R/W0 acknowledgment preserves other channel flags, including
newly asserted flags; if they reassert the CPU flag, the source is already
masked and foreground reports FAULT rather than an interrupt storm.
Other pending/priority fields are observed and preserved; unexpected changes
fault without clearing them. Timer1 is not hardware one-shot: the claim is
at most one service per arm, not exactly one physical counter overflow.

Each foreground wait has its own **1,024 raw tick / 4,096 poll** limits.
Confirmation must precede the deadline; equality times out. Range/backward
and helper errors are explicit. The delivery wait begins after restore's
return, excluding debugger inspection of its interrupted continuation and
ISR/RETI. This is safe only with the stopped timer, source-masking ISR, no
other enabled source and finite verified ISR body; it is not a wall-time
bound for arbitrary handlers. Existing foreground-only timebase ownership
is preserved: the ISR calls no helper or Sleep Timer reader.

Success clears EA under explicit fixture policy after both tokens have been
consumed, advances the cycle/M0 heartbeat and reaches READY. The next cycle
independently rearms the timer. Faults retain their original reason, disable
EA/T1IE and stop an owned timer, abandon outstanding tokens and latch FAULT;
they never restore an enabled token as cleanup or retry. Entry rejection
does not repair an unowned timer. Only separate explicit initialization/reset
clears faults. The [64-byte ABI, proof and manual boundaries](DEBUGGING.md#timer1-irq-board-fixture)
separate sampled register fields from actual ISR context.

The unchanged LG image passed
[compiled-C Timer1/IRQ acceptance on 2026-09-17 (UTC+03)](DEBUGGING.md#2026-09-17-lg-compiled-c-irq-acceptance):
three initial normal cycles, a separate pre-start timeout retaining FAULT,
then 257 independently armed cycles after an explicit reset. Pending waits
were 133..134 raw ticks / 29 polls in the full run; delivery was 1..2 ticks /
1 poll. Counter `0B3A` stayed frozen at PENDING/INNER/READY. Each interrupt
returned to `0C9D`, restore+12 at RET with **DPL already OK=0**; the nested
caller frame, full CPU/active-IRAM context and actual RETI two-byte pop passed.
This is hardware evidence for the interrupted in-leaf result, not the
synthetic +9/live-token-1 case or higher-priority nesting. Byte counters/M0
heartbeat wrapped, and immutable observations and unrelated state passed.
The target now holds IRQ READY `01BB`, EA/T1IE off and Timer1 stopped.
No firmware or runner change or weakened assertion was needed.

Generic hardware, higher-priority hardware nesting, calibrated peripheral
timing and other IRQ services remain unvalidated. Neither the host run
duration nor raw counts establish exact hardware overflow counts or a true
one-shot timer. M2 #4 remains open; this dated acceptance grants no new
hardware authorization.

## Quiescent radio FIFO foundation

`include/radio_fifo.h` / `src/radio_fifo.c` is an isolated, original
initialization-time service, excluded from all ten earlier board images.
Only the subsequent, explicitly selected `radio_fifo_fixture` links it.

| API | Supported operation |
| --- | --- |
| `radio_fifo_clear_init(timeout_ticks, poll_limit, diagnostics)` | Explicitly discard/reset both FIFOs as needed; at most one immediate RX flush ED and one TX flush EE, each verified before proceeding |
| `radio_fifo_preload_init(body, body_length, timeout_ticks, poll_limit, diagnostics)` | Require reset-empty TX; write PHR=`body_length+2`, then 1..125 body bytes through RFD, verifying count/first/last after every byte |

The body excludes PHR and generated FCS: AUTOCRC requires LEN=3..127 and
LEN-2 supplied body bytes (SWRU191F pp.219,221). Total FIFO writes are 2..126,
not 128; no frame parsing, authentication, CRC generation or transmission is
performed here. TX content persists after transmission, so a different frame
requires a separate explicit clear, never an implicit replacement.

**Ownership is a precondition, not inferred from idle samples.** One awake
foreground owner must know that radio, CSP and DMA have no active, scheduled
or pending work and exclude concurrent writers, ISR calls, Sleep Timer readers,
clock changes and sleep/wake discontinuities. The API requires IEN0/1/2=0,
SLEEPCMD.MODE=0 with reserved bit 2 set, stable matching CMD/STA with OSC=0 and
CLKSPD=0, FRMCTRL0=40 and FRMCTRL1=01. Thus AUTOCRC is enabled, AUTOACK and
test/loop modes are disabled, and underflow detection is enabled. It does not
switch clocks or configure those fields. LF/TICKSPD are preserved, not calibrated.

Observations reject nonzero RXENABLE, CSP_RUNNING, CAL_RUNNING, RX_ACTIVE,
TX_ACTIVE, SFD or PLL lock. Reserved status bits required zero are checked.
CSP_PC, the reset-unknown fast FSM state number and CCA/SAMPLED_CCA are
explicitly ignored, not interpreted as ownership or timing evidence.
No DMA register, CSP program, address/source-match RAM or FIFO RAM is accessed.
Only RFST ED/EE and RFD writes are emitted; RFD is never read. No RF-off,
RX/TX/ACK strobe, IRQ/source-mask/flag write, dispatcher or GPIO policy is added.

Clear success requires zero counts, reset FIFO pointers and cleared RX FIFO/
FIFOP signals. If those postconditions already hold, `EMPTY` is a verified
idempotent result with no strobe or time sample. Preload requires TX count and
both TX pointers initially zero; each write must be followed by count/last
equal to the number written and first still zero. It cannot advance on a
desired count alone, and it preserves the observed RX count/pointers/signals.
RFERRF latches, including overflow/underflow, and the independent RX overflow
indication FIFO=0/FIFOP=1 are rejected. Flush is **not** used as an assumed
interrupt acknowledgment; this API does not recover pre-existing error history.

| Result | Meaning |
| --- | --- |
| `OK=0`, `EMPTY=1` | Verified operation, or already reset-empty clear |
| `INVALID_ARGUMENT=2` | Null pointer, length outside 1..125, zero/out-of-half-range timeout, or zero cap; no MMIO, output unchanged |
| `UNSUPPORTED_STATE=3`, `BUSY=4`, `NOT_EMPTY=5` | Rejected configuration/reserved bits, observed activity, or TX needing explicit clear |
| `CONTROLLER_ERROR=6`, `COUNT_ERROR=7` | Raw RFERRF/RX overflow evidence, or impossible count/pointer progress |
| `TIMEOUT=8`, `POLL_LIMIT=9` | Whole-operation raw deadline or independent poll cap exhausted |
| `TIMEBASE_ERROR=10`, `COUNTER_RANGE=11`, `STATE_CHANGED=12` | Helper ambiguity/error, backward/out-of-window time, or changed clock/other FIFO state |

Timeout must be positive and below `800000`; cap is 1..65535. One raw deadline
covers the entire operation, including all bytes/flushes; equality times out.
There are at most `poll_limit` polls and one initial time sample. No second
write is issued after cap exhaustion. CPU execution and true half-range
observation are required; missed wraps/reset and instruction overhead are not
calibrated wall-time bounds.

Diagnostics are a caller-owned **21-byte SDCC XDATA object**, not a serialized
wire ABI; host padding differs. They retain elapsed/polls/helper status, issued
and confirmed strobe masks, written and verified byte counts, raw errors,
counts, pointers, FIFO signals and a complete-sample indicator. A late observed
effect is not counted as verified. Invalid arguments leave the object unchanged;
other calls initialize it, and a zero helper field alone does not imply a call.
Input/output objects must be valid and disjoint; body is a generic pointer,
supporting CODE and ordinary XDATA. The linked caller assigns each address-space
variant separately before conversion: a mixed CODE/XDATA conditional expression
can lose its tag under SDCC 4.2.0.

There is no retained driver state or heap. Compiler scratch is foreground-only,
not ISR-reentrant. A failure may leave cleared or partially loaded FIFOs, or
unconfirmed effects. Stop initialization and explicitly reestablish ownership/
recover before reuse; never retry, append, erase error latches, issue RF-off or
reset implicitly. See [primary facts](PROVENANCE.md#m2-quiescent-radio-fifo-sources)
and [offline evidence/remaining gate](VALIDATION.md#m2-quiescent-radio-fifo-automated-coverage).

The [board fixture contract and ABI](DEBUGGING.md#quiescent-radio-fifo-board-fixture)
compose these unchanged drivers with M0 startup and one bounded XOSC32 selection.
Its foreground stage is inlined into `main` in `src/radio_fifo_fixture_state.c`
to remove a return frame from the combined clock/FIFO call chain; target-only
NOP/RET/fault checkpoints remain in `examples/radio_fifo_fixture.c`.
SDCC's retained out-of-line copy and all scratch are included in the footprint.
Host tests call the same stage body without target checkpoints. There are no
production test callbacks, new public driver APIs or controller recovery.
The fixture alone reads known accepted TX RAM bytes at `6080..60FD`; this does
not expand the FIFO driver's interface or debugger memory permissions.
The [2026-09-17 LG record](DEBUGGING.md#2026-09-17-lg-compiled-c-fifo-acceptance)
executes those unchanged drivers on silicon, including a real partial-effect
timeout and separately reset recovery. It does not validate RX flush, on-air
behavior or error-latch recovery; generic remains host/image/simulator-only.

## Isolated channel-0 DMA copy

[`dma_copy_init(source, destination, length, timeout, limit, diagnostics)`](../include/dma.h)
is a foreground, non-reentrant **1..16-byte RAM copy**, not a DMA framework or
an AES/peripheral interface. Source/destination are numeric 16-bit XDATA
addresses; diagnostics is an XDATA-qualified pointer. The descriptor is eight
persistent volatile XDATA bytes: big-endian source/destination, `00 LEN 20 51`
(fixed length, byte BLOCK, TRIG0, +1/+1, IRQMASK0, assured priority).

Before **any DMA-register access**, the caller must establish awake stable
undivided RC16 or XOSC32, all IEN0/1/2 zero, exclusive DMA ownership and clear
debug DMA_PAUSE (or no debug session). Firmware cannot read that debug setting.
The separately [implemented and LG-observed debug gate](DEBUGGING.md#guarded-dma-enable-after-reset)
can establish `26 -> 22` after an authorized own reset and caller image check;
it does not itself validate or exercise this DMA service.
History must be full SoC reset or this service's exclusive TRIG0 history:
no other armed, in-flight or scheduled channel, ISR/debugger owner, prior
peripheral-trigger accounting or pending unread completion. Idle snapshots
cannot prove that history. No dummy transfer, abort or implicit recovery is used.

Buffers must be explicitly allocated, persistent, caller-owned objects, not
compiler scratch or an implicit free-RAM pool. All ranges must fit below
`1E00`, be pairwise disjoint including diagnostics, and avoid private storage.
The linked `dma_reserved_end` fence protects the entire DMA/timebase XDATA
prefix, including compiler arguments/temporaries; the separate SDCC
`__gptrput_PARM_2` scratch byte is also excluded. Link timebase before DMA and
eligible caller objects after that fence. The isolated checker pins and
accounts these allocations and the actual helper/caller ABI; future image
integration requires the same proof, not guessed addresses. IRAM/stack cannot
be reached through the prohibited `1F00..1FFF` alias.

Entry rejects any armed channel, pending request/completion or DMA CPU flag.
Other-channel configuration and all unrelated flags remain untouched.
Configuration, arm and software request are issued once. A pinned naked leaf
executes `MOV DMAARM,#1`, **nine NOPs**, `RET`; each NOP takes at least one
system clock. ARM readback alone is not readiness. No instruction-delay
assumption is used outside this documented single-channel fetch interval.
Fresh completion requires DMAIRQ0=1, DMAARM0=0 and DMAREQ0=0 with preserved
configuration/clock/masks. The only acknowledgment is `DMAIRQ=1E`, preserving
even newly arriving foreign flags under R/W0 semantics; IRCON is never written.

One positive raw deadline below `800000` covers preparation through checked
acknowledgment, with an independent positive 16-bit poll cap. No next action
is issued when that cap is exhausted. Equality is timeout; late completion
is not success. CPU progress and true half-range observation remain caller
requirements; missed wraps/reset and calibrated wall time are not inferred.

The **19-byte SDCC diagnostic object** is not a wire ABI (host padding differs).
It records elapsed ticks, polls, helper status, issued action bits
CONFIGURED=1/ARMED=2/REQUESTED=4/ACKNOWLEDGED=8, complete/verified indicators,
raw DMA flags/configuration/IRCON and sample validity. `complete` can be true
on a late/error return; `verified` requires timely completion and checked
acknowledgment. Silicon exposes no partial byte count.

Argument/range/ownership errors leave MMIO and diagnostics unchanged. Every
other failure latches its **original result**; all later calls return it before
MMIO or diagnostic/descriptor modification, even if their arguments are invalid.
After ARM, error return is **not quiescence**: DMA may still change destination
after C returns. Descriptor and both buffers remain valid, source immutable
and destination unreused until genuinely established full-reset recovery.
Only success releases buffers. There is no reset/abort/release API; clearing
C state alone does not reset DMA history or pending DMAREQ.

See [primary facts](PROVENANCE.md#m2-channel-0-dma-sources) and
[offline evidence/remaining gate](VALIDATION.md#m2-isolated-dma-copy-coverage).
All twelve earlier board images still exclude this module. Only the two
[DMA board fixtures](DEBUGGING.md#channel-0-dma-board-fixture)
link it: volatile persistent buffers and diagnostics follow the private
prefix, with relocated helper/ABI proof and terminal ownership unchanged.
Their bounded RC16/XOSC32 orchestration and explicit reset/CODE/config22
manual gate are not production integration. The separate
[LG hardware record](DEBUGGING.md#2026-09-17-lg-compiled-c-dma-acceptance)
executes the unchanged driver on both clocks/routes, including a real
unverified timeout and separately reset recovery. Generic, other channels,
AES, peripheral triggers, DMA interrupts and stuck-controller recovery
remain separate gates.

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

Persist network/parent information, required keys and counters, and
`bdbNodeIsOnANetwork`, then add bindings/reporting settings only when their
behavior exists. Corrupt or incomplete records are rejected with a visible
recovery reason. Keep pending TC exchanges separate from committed verified
keys; a restart must not accept an interrupted exchange as successful.

After persisted resume, BDB 3.0.1 section 7.1 requires a secure NWK rejoin
attempt for a previously joined ED and Device Announce on success. The
persisted membership flag alone does not establish current connectivity.
Retain R22 ED Timeout negotiation after each successful join/rejoin and prompt
keepalive after recovery using `nwkParentInformation`. Unknown information
triggers bounded renegotiation/recovery; a missing response must not block
startup forever. Test this separately from a fresh join.

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
