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

The separate [offline MAC transmission scheduler](MAC_TX.md) composes that
real codec with one caller-owned context (168 bytes on SDCC) and one copied
DATA or selected Beacon/Association/Data Request body.
Association admission fixes capability88/8C for the receiver-on ED subset
and relies on caller-supplied identity/coordinator/power facts. Request ACK
is only MAC receipt, not Association Response acceptance or membership.
Addressed Data Requests use the same copied slot and ACK/retry state, with
compressed nonbroadcast PAN and caller-selected short/extended identities.
Association-response retrieval requires extended source; there is no automatic
poll, response window or procedure transition after either request's ACK.
Request transmission is not a scan; channel changes, Beacon receive windows
and the association procedure remain separate.
It owns the device-wide DSN sequence, unslotted NB/BE backoff, finite retry
state and ACK matching. Every external action/event is correlated by a
nonwrapping generation, retry and backoff attempt; retransmission preserves
the admitted bytes and DSN. A completed slot remains occupied until explicit
release, while uncertain adapter/clock/cleanup faults retain ownership.

Serialized foreground calls stage only the43-byte control suffix in private
ordinary XDATA, not a second125-byte frame. Per-field offset/size assertions
retain the public generic-pointer ABI and native layouts; object-representation
copies preserve inactive members and native padding. This reduces the SDCC
module to5650 CODE/8 permanent DATA/191 XDATA bytes, with no reentrancy,
callback, ISR or full-stack-fit claim.

Time is an abstract 32-bit symbol epoch with half-range constraints and an
ordered event watermark, not a conversion of raw Sleep Timer ticks.
Transaction lifetime, foreground work and cleanup each have finite bounds.
The 54-symbol ACK window starts at captured TX end; no-ACK retries preserve
that window plus IFS, including after an early wrong-DSN ACK. Logical results
do not release active radio ownership without a QUIESCED event.
There is no CC2530 adapter with captured TX/ACK timing; the separate same-owner
raw TX/RX phase does not yet provide that contract. No MMIO, board GPIO, security or network membership
is introduced; the isolated SDCC resource proof does not establish complete
stack fit or IRQ nesting.

The isolated [MAC Timer foundation](MAC_TIME.md) owns untouched reset-state
Timer2 under stable undivided XOSC32, disabled interrupts and quiescent
radio/CSP/DMA. It disables timer event outputs, sets positive periods
512/`FFFFFF`, starts asynchronously and confirms STATE, not RUN echo.
Common latching and bounded rejection of fine-low FF produce a coherent
raw tuple: fine0..511, periods0..FFFFFE. One raw Sleep Timer deadline and
a positive work cap bound each operation. A first operational fault is
retained without cleanup/recovery; error outputs are unchanged.
This service does not rewrite counters, acknowledge flags, select clocks,
start RF or expose a capture API. Its ordinary-XDATA private prefix/output
checks and 512-byte reservation budget remain isolated. A live tuple is
neither an extended `mac_tx` symbol epoch nor a captured PHY end; the
quiescent-radio requirement prohibits silently chaining it with RX/TX.
Primary capture selection/freshness, fine phase, delivery order, unified
ownership and physical timing remain separate gates.

The separate [fractional epoch extension](MAC_EPOCH.md) performs arithmetic on
coherent raw Timer2 tuples without accessing hardware. Its11-byte context
extends coarse periods modulo2^32 while retaining fine0..511 in a6-byte
result. The actual raw modulus isFFFFFF periods, with a half-range boundary
of7FFFFF periods plus256 fine increments. Ambiguous progress retains a fault;
missed wraps/reset remain caller continuity obligations. No integer event-time
rounding, captured-edge identity, radio handoff or original driver relaxation
is inferred from this arithmetic. Its separate resource proof is not included
in the integrated protocol budget or board firmware.

The explicit [co-owned clock/radio profile](MAC_RADIO.md) composes those real
services without adopting an unknown running timer or foreign receiver.
`mac_radio_init` orders clock selection, reset-state Timer2 initialization,
epoch binding and radio acquisition. Its operations sample live time before
radio work, so a time failure cannot turn an already-published frame into an
error return. One shared lower-service prefix, a separate enclosing private
prefix and the complete linked libc scratch suffix are excluded from borrowed
caller objects. Operational faults retain ownership and the first cause;
there is no recovery, implicit stop or successful placeholder. This profile's
new active-radio reader is distinct from the unchanged quiescent-only reader.
It supplies neither physical event timestamps nor a `mac_tx` event adapter.

The separate [delayed-sample projection](MAC_STAMP.md) uses three real epoch
advances on private temporary state to validate a closed window and place an
independently coherent raw sample within it. It preserves the live epoch,
fine phase and software wrap, publishing only after all bounds succeed.
It supplies no capture-valid/overrun flag, freshness proof, frame identity,
PHY offset or integer event-time rounding. It is not linked into the radio
owner or board images before those hardware/event contracts are established.

The explicit [interval attempt owner](MAC_ATTEMPT.md) adds a separate
`CC2530_MAC_ATTEMPT` composition over the genuine clock/Timer2/epoch/radio
services. Idle prepare owns immutable TXFIFO before timing-critical work.
RX_MODE11 and explicit raw CCA1 exclude new frames before admission; normal
unfiltered/AUTOACK-off RX must be restored before the earliest possible TX end.
The fixed serialized PPDU duration supplies a lower bound; positive own
completion and complete-head predicates precede upper-bound latches.
Scoped provisional timer reads are bracketed by complete validation.
Only then are chronological epoch projections and original frame/CRC
metadata published in a 164-byte target record. Complete lower, wrapper,
top-level and libc scratch regions remain excluded from caller buffers.
The real eight-module proof uses 24621 CODE, 1475+64 XDATA and peak SP79,
within new 32768/2048/SP7C limits without changing earlier budgets or CODE.
It is not hardware timing/calibration, normative CCA scheduling, an exact-event
`mac_tx` adapter or a continuous POLL receive/ACK lease.

The separate `nwk_beacon` module decodes only the 15-byte R22 NWK information
inside the returned upper-layer Beacon Payload. It has no dependency on MAC
headers or platform code. The [NWK codec contract](NWK.md) preserves raw
profile/capacity/depth/update metadata without compatibility or parent
acceptance. It adds no firmware caller or network state machine.

The separate [NWK candidate collector](NWK_CANDIDATES.md) composes the actual
MAC frame/Beacon and NWK Beacon decoders into a four-entry copied table.
Its 150-byte SDCC caller context applies only a preliminary PRO2/BO15/
Association-Permit/ED-Capacity filter, with explicit channel-mask and truthful
CRC preconditions. Eligible duplicates replace metadata; a valid changed
advertisement can withdraw/compact an entry even while full. Malformed input
and new identities at capacity preserve existing records. Short-address tails
and unused slots are zeroed; outputs borrow no frame storage.
This is not a MAC PAN-descriptor list, channel scan, ranked/fresh parent choice,
association or membership state. No radio, timer, key or board dependency is
introduced. Different Extended PAN identities remain distinct, without aging.

The separate [parent selector](NWK_PARENT.md) consumes one immutable table
snapshot plus a15-byte caller policy. It filters the selected network,
supplied1..7 link costs and potential-parent mask, then requires one Update ID
to dominate every eligible ID under an explicit modulo256 half-range policy.
Known selected-network watermarks are optional explicit inputs, not inferred
NIB state. Ambiguous/cyclic order returns an error with output unchanged;
equal newest IDs use cost/index, never profile2 depth. Success copies a37-byte
choice. No neighbor/NIB update, scan freshness, radio, association, security or
membership is supplied. Table compaction requires rebinding caller metadata.

The [offline active-scan controller](MAC_SCAN.md) leases the caller's existing
device-wide transmitter and owns a 212-byte SDCC context, including the copied
candidate table. It walks requested page0 channels11-26 in ascending order,
submits real canonical Beacon Requests and grants one serialized foreground
`mac_tx_step` at a time; it never resets the DSN or nests another transmitter
implementation. Confirmed receive windows feed the actual collector/decoders.
Working lifetime/steps and separate restoration bounds are finite. A channel
leaves `unscanned` only after confirmed full-window closure; `sent`, candidate
count and sticky capacity loss remain distinct. DONE requires restoration of
the saved logical PAN/channel/filter/RX state; unresolved faults retain the
lease. The action/event interface is not a CC2530 adapter, a full MLME-SCAN
descriptor service, complete parent selection or association.
Its six-module test image uses29440 CODE and1492 ordinary XDATA bytes plus
the64-byte status reservation; it is not complete-stack resource evidence.

The [Association Response context](MAC_ASSOCIATION.md) separately owns73 bytes
and composes the real frame/command decoders. Its copied selection, nonzero
epoch, nonwrapping generation, half-open symbol-time lifetime and finite work
bound correlate foreground reports, not authenticated on-air transactions.
Only a known coordinator IEEE is compared; a source learned after a short
selection is explicitly unbound. Extended PAN ID is never a substitute.
Terminal Response/refusal/address metadata is copied once; cancellation,
expiry and exhaustion cannot invent radio cleanup. Captured trailing-end time,
CRC and timely receiver ACK remain truthful independent adapter obligations.
This is not the decision-wait/retrieval procedure, NWK address admission,
parent installation or membership. The16-KiB CODE/1-KiB XDATA reservation
budget belongs to its standalone test image, not the complete stack.
`mac_frame_decode_profile` and `mac_association_step_rx` select a per-call
R22 Association Response receive policy without adding fields to the73-byte
context. Explicit source PAN permits selected/broadcast destination PAN;
compressed broadcast PAN still cannot establish the selected source PAN.
Legacy entry points always select IEEE2006. Default POLL admission remains
legacy; its explicit `mac_poll_step_rx` path can now forward those R22
Responses. This is not full IEEE2015 header support.

The [conditional legacy extraction controller](MAC_POLL.md) leases the same
idle device-wide transmitter without resetting its DSN, generation or IFS.
Its266-byte SDCC context separates123-byte control from a143-byte copied
receipt. Foreground staging uses the same control type; later cleanup never
overwrites a taken or established receipt. One action grants one real
`mac_tx_step`, with the original event reported before advancing time.

Only an accepted matching ACK transition establishes captured end A.
Pending0 gives NO_DATA; Pending1 opens `A < complete_frame_end <= A+F`, where
F is a caller-valid configured PIB, not a guessed timeout. Nonempty DATA gives
SUCCESS; empty DATA and supported commands give NO_DATA with independent
copied delivery. Commands require independent caller dispatch; the genuine
test forwards Association Responses to the real response context using original
bytes/epoch/stamp, preserving its separate half-open lifetime.
Timeout NO_DATA requires loss-free ordered closure, not elapsed foreground time.
Cancellation, capture/order/adapter errors and cleanup faults remain local.

An independently confirmed continuous RX/ACK lease must survive prompt
retirement of the real TX action under its unchanged QUIESCED contract.
CLOSED covers drainage, required receiver ACKs and applicable IFS; FAULT
retains ownership. The existing reset-exclusive platform services cannot
supply that handoff. No automatic repeat extraction, total Association
confirmation, full IEEE2015 header support, radio adapter or membership is added.
Both POLL step entries replace their private argument staging before calling
the same worker. This avoids four additional persistent SDCC IRAM spills;
no profile is installed in the public context or inherited by a later call.
The32641-CODE/1989-byte-reservation test composition reaches SP7B under the
unchanged SP7C cap. Its127-byte CODE margin is not full-stack or IRQ headroom.

The [staged association controller](MAC_JOIN.md) leases that same transmitter
across the complete supported Request/decision-wait/extraction/Response path.
Its 645-byte context contains actual POLL and Association contexts, not layout
casts or a second transmitter. Request actions execute the genuine scheduler
internally; extraction preserves POLL's external one-step grant/report.
The accepted Request ACK starts R; prompt retirement preserves the existing
quiescence bound instead of holding a TX action through the wait.
An extraction ACK opens a contextual lifetime F+1 for configured F=1..65534,
preserving exact `(B,B+F]` reception without changing captured timestamps.
Response metadata is taken before later cleanup can fail. The one-shot
prepare/close/restore tokens require genuine adapter confirmations; FAULT
retains ownership. Local abort reasons, lower-layer records and cleanup errors
remain separate from membership or MLME confirmation.
Five production modules consume30428 CODE/1900 XDATA; full caller images
reach32763 CODE/3117+64 XDATA/SP7C, with no whole-stack headroom claim.
This remains an exact-event offline controller, not an interval-to-point
conversion or a link to the raw radio owner.

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
separately; the resource image below also executes full MAC/NWK/APS/ZCL/value
composition on SDCC.
The independent `zcl_attributes` module adds a caller-owned, bounded read-only
model and a unicast Read Attributes response builder. One table selects a
cluster side and standard/manufacturer namespace; duplicate IDs and reserved
standard declarations (`5000..EFFF`, `FFFF`) are rejected,
non-readable values are not inspected, and partial responses expose their
requested/returned counts. It composes the existing frame/value codecs and
publishes outputs atomically on local success. The caller must already select
permitted unicast endpoint/profile/cluster context and authenticate as required.
No endpoint registry, getter callback, write/reporting service,
device-specific cluster or board linkage is introduced. Its own target image
tests the handler; the resource image below also tests full request/response
composition on SDCC.
Complete ZCL/application support remains planned.

`zcl_dispatch` selects Read or Discover handlers for one caller-selected
cluster side/namespace. Discover scans the bounded, possibly unsorted table
without mutation or value access and emits ascending ID/type pages with an
explicit completion bit. The dispatcher builds unsupported-command errors,
delivers received Default Response metadata without replying, and shares the
[bounded write parser](ZCL_WRITE.md) for read-only per-attribute errors and
silent No Response processing. Namespace-mismatched No Response still fails
explicitly without output or mutation.
It shares structural table validation and supported-type classification,
not network state. Local failures preserve outputs; the explicit result kind
distinguishes a constructed response from a received default notification.
Transport admission, cluster selection, authentication, matching transaction
state and actual sends remain outside this module. Its separate target
image exercises real dispatch/read/discovery; full Discover-then-Read
MAC/NWK/APS composition additionally runs in the integrated resource image.

The separate [Basic attribute provider](ZCL_LAB.md) constructs a standard
server table for ZCLVersion8, caller-supplied manufacturer/model/software
strings, validated PowerSource and ClusterRevision3. It reuses these handlers
without a new dispatcher. The caller owns152 target bytes, including copied
strings; internal pointers require the initialized object to stay at its
original address and remain read-only until explicit reinitialization.
Invalid configuration leaves the complete model unchanged.
Its original #70 six-module proof used17,978 CODE and996 ordinary XDATA plus64
reserved, with peak SP5A; [current write-enabled evidence](ZCL_WRITE.md) is separate.
The Basic model is not added to the integrated
protocol-budget image or board firmware and does not establish whole-stack
fit. There is no endpoint registration, manufacturer assignment, factory
reset, Identify, reporting, persistence, physical sensor or network send.

The separate [Identify procedure](ZCL_IDENTIFY.md) owns an eight-byte caller
context without retained pointers. Explicit monotonic modulo32 millisecond
inputs drive its seconds/fractional-phase countdown; Identify resets the phase,
Query/Read/Discover do not. It stages timer changes until real serialization
succeeds and delegates foundation processing through a transient two-attribute
view. It has no board clock, physical indicator, client/group/broadcast path,
network send or authenticated endpoint. Its internal five-byte write intent
permits full-range uint16 IdentifyTime updates through that shared parser;
serialization failure cannot half-apply the timer. Its isolated proof does not extend
the integrated protocol resource image or establish complete-stack fit.

The separate [synthetic Temperature model](ZCL_TEMPERATURE.md) owns a37-byte
caller context without retained pointers. Caller-supplied signed hundredths
and modular uint32 seconds drive four read-only attributes and one reportable
MeasuredValue. A transient table reuses the genuine dispatcher/write/read
services; bounded Configure/Read Reporting Configuration handlers stage full
request syntax and response construction before changing configuration.
Minimum/maximum intervals, signed change thresholds and unknown transitions
are explicit; defaults are supplied by the caller, not inferred from a profile.
One pending report leases a value snapshot with a non-reused16-bit token.
Only truthful completion at a checked issuance time advances the baseline;
cancellation, later samples and failed serialization cannot masquerade as a send.
Clock ambiguity, excessive pending age and token exhaustion retain faults.
Bindings/destinations, scheduling, transport and authorization remain caller
responsibilities; physical sensing, persistence and endpoint registration are
absent. Three isolated compositions fit separate24576-CODE/1536-reserved-XDATA
limits; the wire caller reaches SP7C and report CODE has298 bytes remaining.
The production modules alone use16905 CODE/864 XDATA. This is not added to the
integrated protocol-budget image and does not prove full-stack fit.

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

## Isolated passive radio reception

The original [`radio_rx` service](RADIO_RX.md) is a separate foreground
RX-only operation, not an expansion of the quiescent FIFO API or board GPIO.
It requires known reset/exclusive ownership, performs fixed passive channel
configuration, receives one CRC-checked body through RFD, then verifies
soft-stop/flush before publication. Metadata remains raw; failures retain
their original cause and may leave RX active until a separate full reset.
The contract includes both persistent XDATA objects, the private prefix and
generic-store scratch exclusion. Only the separate `radio_rx_fixture` board
image links it, after the real clock selector and before its persistent caller
objects. The [fixture contract](RADIO_RX.md#bounded-passive-rx-board-fixture)
caps channel15 reception at16 attempts with terminal END/FAULT and no recovery.
Its strict host/linked/synthetic checks are not physical radio evidence.
FSCAL1 remains a whole-byte00 write, but only documented stable VCO_CURR
bits1:0 are verified; reserved R/W0 bits7:2 are not read-as-zero. The other
nine settings remain fully compared. The
[parent-observed prior-image failure](DEBUGGING.md#2026-09-18-lg-rx-fscal1-failure-and-probe)
is distinct from the corrected image's
[bounded LG hardware acceptance](DEBUGGING.md#2026-09-18-lg-bounded-passive-rx-acceptance).
The latter covers channel15 body agreement with an independent sniffer,
BAD_CRC nonpublication/reuse, a pre-RF timeout with separate reset recovery
and the16-attempt terminal cap. Generic and broader RF fault recovery remain
unobserved; neither result introduces transmission, continuous queues or MAC.

## Isolated filtered receiver and AUTOACK ownership

The original [AUTOACK owner](RADIO_AUTOACK.md) is a separate reset-exclusive
foreground hardware service, not a composition of the old RX/TX/FIFO/queue
or MAC Timer owners. It verifies all12 local-address RAM bytes and a fixed
version-0 DATA/ACK/command filter, AUTOCRC/AUTOACK, unslotted/Pending0 profile
and reviewed raw-power settings before requesting RX. It excludes source
matching/AUTOPEND, CSP programs, DMA and IRQs.
READY additionally requires completed calibration, actual RX, PLL lock and
RSSI_VALID. Hardware ACK can transmit independently of CPU progress.

One private staged frame makes publication atomic; each destructive RFD read
executes once. Caller configuration/output must be persistent ordinary XDATA
beyond the complete timebase/service prefix and outside all linked libc
scratch. Complete CRC-good and CRC-bad bodies retain raw metadata, with no
security/duplicate/window parsing. Invalid calls preserve diagnostics/output;
the first operational fault retains ownership and forbids subsequent MMIO.

Soft stop clears only this owner's RX mask after separately clearing/verifying
old RFIDLE. It preserves AUTOACK while reception/ACK finishes, confirms fresh
RFIDLE and physical idle, then retains complete FIFO frames for explicit reads.
Partial/inconsistent FIFO or any RF error cannot become drained success.
STOPPED is physical stop/drain, not receiver-on POLL CLOSED, IFS,
ordinary-TX permission or release to a reset-exclusive service.
Explicit `radio_autoack_resume` revalidates the retained profile, physical
idle and empty ring, sets only the owned mask bit and confirms RX/RSSI
readiness. Only this owner's OFF is eligible; pending drainage, foreign RF
history and retained faults cannot be adopted. Nonzero empty cursors remain
valid, and no reset/reconfiguration/flush conceals an RX gap. Acquire,
receive and stop still reject OFF as before.

The #73 `radio_autoack_send` phase admits one ordinary hardware-gated CCA/TX
only after this owner's stop/drain. At verified idle it disables AUTOACK and
filtering, checks mode3 CCA settings, flushes/reloads only TXFIFO and
clears/verifies TXDONE. RSSI readiness plus four real CPU NOPs precede
ISTXONCCA; the owned RX mask stays set through post-TX reception.
TX_DONE is fresh ordinary PHY completion, not delivery; CCA_BUSY performs no
retry. RX_NOACK/DRAIN_NOACK/OFF_NOACK preserve the explicit different profile.
Receive/stop retain existing bounded publication/drain semantics; resume
restores filtering then AUTOACK only while stopped and empty. There is no
concurrent live-AUTOACK admission claim or hidden RX gap.

Both-board host/image/alias-aware CI binds7007 CODE,450+64 reserved
XDATA and whole-run SP34 within separate24576/1536/SP7C limits. No board image
links this synthetic test composition. Broadcast-AR behavior, global ACK-FCF
compatibility, same-clock captured timing, continuous RX/ordinary-TX arbitration
and IFS/loss-aware handoff remain full-adapter gates. No silicon AUTOACK or
finite over-air ACK-count claim follows from bounded CPU calls.

The separate [boot-disarmed link fixture](RADIO_LINK_FIXTURE.md) is the sole
board-image exception for the genuine service, not the synthetic composition.
It keeps board GPIO in existing startup/board code and protocol decisions out
of the raw diagnostic controller. Two explicit mailbox phases return at WAIT
without clock/RF work; a further uninterrupted continuation consumes one
experiment before selecting XOSC32. Acquisition may automatically ACK.
Preparation stops/drains before one ordinary conditional TX; post-TX RX has
AUTOACK/filtering disabled and a raw Sleep Timer deadline plus work cap.
Up to two bodies retain CRC metadata and pre-TX classification. Capacity,
driver or local time/work faults retain ownership without cleanup.
END requires a real stopped/empty owner. Final drainage may preserve a body
after the foreground receive interval has already timed out; this does not
retroactively establish in-window reception, a response or delivery.
Its750 ordinary XDATA plus64 status reservation fit a separate1024-byte
budget; the complete335-byte service prefix and12-byte runtime suffix remain
excluded from all caller pointers. No old owner is mixed into the image.

## External Nordic laboratory companion

The Nordic may be a **controlled stimulus/observer as well as a passive
sniffer**. Preparing that laboratory role does not authorize installation,
RF use or bypass of preservation/recovery gates, and is not a dependency for
unrelated CC2530 work.

The [offline overlay planner](NRF_RECOVERY.md#offline-page-overlay-report)
shares hardware-independent private-file checks with acquisition tooling.
It emits only artifact hashes/counts after checking every non-image byte;
it has no writer/device path and does not evaluate programming authority.

The [NS51 helper](../tools/nrf_stimulus/README.md) targets a separately selected
nRF52840 DK, not in the CC2530 firmware or production stack. Portable bounded
control/serialization and UART queues are separated from the SDK/board/radio
adapter. A default-off BSD patch at the genuine RADIO PHYEND path preserves
the on-air AR bit and enters normal promiscuous RX, with Nordic automatic ACK
disabled. Scheduling, PHY completion and copied CRC-validated receipts remain
distinct records. The latter exclude potentially modified FCS bytes.

One expiring descriptor/nonce-bound grant permits one submission per boot.
Shared foreground/callback state uses saved/restored PRIMASK, not an assumption
that BASEPRI masks priority0. Faults, queue loss and unresolved shutdown remain
visible; no retry, network operation, captured-time API or loss-free window
claim exists. Software/service deadlines and SDK initialization require actual
CPU/clock/driver progress; there is no hard RF-off-on-fault guarantee.

Its target build is explicit and external, using pinned source-built radio/SL.
Ordinary CI exercises only portable native/sanitizer/protocol and synthetic
artifact checks; it does not fetch/build the SDK or upload Nordic firmware.
UICR load-range and actual startup checks do not prove physical restoration
or debug accessibility. The [offline recovery-file checker](NRF_RECOVERY.md)
provides artifact agreement only. Actual device use remains separately gated.

An [explicit SRAM-only build](../tools/nrf_stimulus/README.md#optional-sram-only-profile)
uses the same application/radio source without a flash replacement. Its
128-KiB budget includes code, data and stacks; the flash profile keeps its
original 256-KiB flash/64-KiB SRAM limits. The two artifact profiles reject
each other's loads and are never auto-selected. MPU/stack guards remain;
the SRAM profile omits SoC cache/regulator enable writes. This is a linked
image, not a volatile loader or accepted CPU handoff/restart mechanism.
The [checked handoff preparation](../tools/nrf_stimulus/README.md#checked-volatile-handoff-preparation)
adds original MEM-AP-only transaction primitives with actual DHCSR/DCRSR
handshakes, full SRAM readback and irreversible fault state. It uses fresh
SYSRESETREQ catch rather than changing an active exception's IPSR.
Return ends at a verified **halted** original reset vector, not restored
sniffer service. Its pinned-image admission and halted-state decoder are
offline utilities; no live execution operator or complete physical-policy
approval is present. They do not widen acquisition into CPU control.

The separate [manual acquisition operator](NRF_RECOVERY.md#explicit-read-only-acquisition-operator)
uses an explicitly selected, locally reviewed OpenOCD preservation mode and
only a MEM-AP target. It has no CPU-control or flash-write script, no network
server and no automatic execution. Its private single-use operation contains
two complete flash/UICR read passes with fresh identity/protection binding.
Version 2 includes all eight ACL triplets in each of three snapshots and
rejects read-denial/unknown permission encodings. Version 1 did not cover
ACL debugger read-as-zero and cannot establish unmasked recovery material.
Live sampled ACL agreement is not an atomicity or reset-history proof.
Debug-interface power/configuration and SWD shutdown traffic remain real
side effects; the dummy MEM-AP target state is not physical CPU evidence.
Tool-reported matching reads do not authorize programming or prove restoration.
The [explicit v3 profile](NRF_RECOVERY.md#opt-in-v3-startup-source-binding)
extends each snapshot with seven startup words, while preserving the default
v2 command script. It binds actual MDK selectors, core-family/watchdog/NVMC
facts and complete UICR.APPROTECT to both full UICR captures. Unsupported or
contradictory source conditions have a negative report/exit, not an execution
grant; even a match leaves all CPU/SRAM/RF permissions false. This read-only
source classifier is not named-revision/errata or board/reset approval.
The helper's [actual startup audit](../tools/nrf_stimulus/PROVENANCE.md#offline-startup-and-preservation-prerequisites)
also distinguishes absent linked NV writers from conditional volatile
APPROTECT, cache and regulator effects before application disarming.
Silicon-specific reset/debug recovery must be established separately.

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

## Isolated AES-128 DMA block

[`aes128_encrypt_block(key, input, output, timeout, limit, diagnostics)`](../include/aes.h)
encrypts **one 16-byte block**, using a private two-channel AES backend, not the
RAM-copy API or a general DMA framework. Both layouts are host-tested,
image-checked and synthetically simulated; the corrected LG fixture also has
[bounded normal/negative/reset-recovery hardware evidence](DEBUGGING.md#2026-09-17-corrected-lg-aes-bounded-acceptance).
The fourteen earlier board images exclude it;
only the separate [AES board fixture](DEBUGGING.md#aes-dma-board-fixture)
links it. The LG recovery run accepted514 blocks over257 same-reset cycles,
covering all168 fixture vector/space/clock combinations. Generic hardware
remains unobserved; this is not arbitrary-workload or general DMA acceptance.

The foreground/non-reentrant caller owns AES, all DMA channels, IRQ state,
clock settings and ST0 reads exclusively. Require awake, stable, undivided
RC16/XOSC32, IEN0/1/2 zero (including ENCIE), and independently established
clear debug DMA_PAUSE or no debug session **before any DMA-register access**.
History must be genuine full reset/C initialization or only this API's
successfully completed, acknowledged and fully drained operations. Prior
RAM-copy/peripheral activity is not eligible history. Idle flags, RDY, writing
DMAARM=0 or clearing C fields cannot erase missed-trigger accounting.

Key/input are generic pointers to complete readable 16-byte objects in
unbanked CODE below `8000` or ordinary XDATA below `1E00`; other generic spaces
are rejected. Output and diagnostics are writable XDATA. All XDATA objects
exclude the entire linked AES/timebase private prefix and generic-store helper
scratch. Diagnostics are disjoint from every caller buffer; output cannot
overlap an XDATA key. Input/output overlap is supported: both inputs are
staged before DMA starts. Caller objects are not retained by DMA, even on
error. Link timebase before AES, caller objects after `aes_reserved_end`, and
prove actual map/CDB/assembler allocations; do not assume a free-RAM pool.

Four persistent volatile 16-byte XDATA arrays hold key, zero IV, input and
output. DMA0 has an eight-byte descriptor; DMA1 has its documented **32-byte
channels-1..4 table**, with unused records zero and never armed. Both use
fixed LEN16, byte SINGLE, assured priority and IRQMASK0:

| Phase | ENCCS start | DMA0 source / fixed destination | DMA1 |
| --- | --- | --- | --- |
| Key | `45` | Staged key / ENCDI alias `70B1`, ENC_DW29, +1/0 | Armed once before key start |
| IV | `47` | Zero IV / same input alias and trigger | Remains ready, no output expected |
| Encrypt | `41` | Staged input / same input alias and trigger | ENCDO alias `70B2` / staged output, ENC_UP30, 0/+1 |

Output channel 1 is armed first, then input channel 0; each separate arm has
its own **nine-NOP fetch interval**. Input is reconfigured/rearmed only after
the preceding finite input completion and checked owned acknowledgment.
All required channels are ready before each AES start. No DMAREQ write,
CPU ENCDI/ENCDO transfer, guessed byte delay, simultaneous-arm shortcut or
dummy/abort transfer is used.

The [primary handshake](PROVENANCE.md#m2-aes-dma-block-sources) requests each
needed byte. Each key/IV command requires fresh finite input completion,
matching MODE/CMD, cleared ST and **both fresh ENCIF bits**, while output
DMA remains armed without an output completion. RDY is deliberately ignored
for loads: it describes encryption/decryption, not load completion.
The first LG KEY load [physically set ENCIF=3](DEBUGGING.md#2026-09-17-first-lg-aes-key-load-failure),
disproving the original clear-flags assumption. The primary block-interrupt
wording does not exclude load interrupts or explicitly guarantee load pairs.
Subsequent bounded LG runs observed fresh pair3 for **both KEY and IV**, plus
verified-clear ACKs, through this unchanged C completion gate. That evidence
does not extend to generic hardware or untested workloads. Missing flags wait
only within the deadline/cap; partial pairs fail. No next command consumes
retained flags.
The same input trigger remains selected between phases. Complete finite
loads have no further requested input until the next command; complete block
processing plus all 16 output reads leaves no unread output producer.
These documented boundaries justify descriptor replacement in exclusively
completed history, not recovery from an unknown or failed history.

Block success requires fresh input **and output** DMA completions, both
channels disarmed, no pending request, ENCCS=`48` and both fresh ENCIF bits.
Only then is DMAIRQ acknowledged with `1C`; key/IV use `1E`. These are R/W0
owned-channel masks. Each command's finite DMA completion/acknowledgment and
still-present ENC pair3 are checked before its S0CON acknowledgment. S0CON
is written with its saved high six bits and low bits clear: **all S0CON
bits are ordinary R/W**, not R/W0. A timed post-ACK sample must confirm clear
flags and the expected control/ownership before the next descriptor/command.
The block retains a unique final ACK/sample path before publication.
Unrelated state is never repaired; even a late, effective ACK can leave a
terminal timeout rather than a confirmed/reusable operation.

One positive raw deadline below `800000` covers staging through the final
checked publication decision, following fixed entry preflight. Equality is
timeout; an independent positive 16-bit poll cap also bounds a stopped timer.
Half-range continuity/CPU progress remain caller requirements. The final
16-byte CPU copy has no fallible step: failures never change caller output,
but success is not an atomic 16-byte bus transaction or calibrated deadline.
Arguments/ranges/alias errors additionally preserve diagnostics and MMIO.
Every other error latches its original result before return; re-entry performs
no MMIO, staging/descriptor replacement, acknowledgment or publication.
There is no retry, abort, reset, rekey or wipe on error. Potentially active
DMA retains private buffers until separately established full-reset recovery;
matching bytes or an error return never proves quiescence.

The 29-byte SDCC diagnostic object is not host-struct serialization. It records
phase, command submissions, confirmed input-phase bits, output drain,
issued/confirmed acknowledgments and publication separately, plus raw status
and elapsed/poll/helper values. `sample_valid` bit0 identifies a complete
last SFR observation; bit1 a timed last poll. Unread/stale fields are not
current observations. `enc_ack_issued` counts 0..3 writes; `enc_acked` is a
KEY1/IV2/block4 confirmed-phase mask, not a count. The native size remains29;
the fixture's explicit wire ABI is version2. There is no partial DMA byte
count or secret payload.
Hardware key/IV persist until reload/reset/PM2/PM3; software copies, compiler
spills and CPU registers may retain data beyond those events. No secure
erasure, key management, decryption, messaging ECB, CCM/MIC/authentication,
RNG or Zigbee security is provided. The CPU-only transfer gap remains open.
See [offline proof and precise physical gate](VALIDATION.md#m2-isolated-aes-dma-block-coverage).

## Isolated deterministic PRNG

[`prng_seed_explicit(uint16_t seed)` / `prng_next16(uint16_t __xdata *output, uint8_t limit)`](../include/prng.h)
are an explicitly seeded **deterministic hardware LFSR**, not entropy, a
cryptographic RNG or a random-byte routing service. The original driver is
excluded from all sixteen earlier board images; only the separate PRNG fixture
below links it. Its independent software mathematics
is test-only, never a production fallback. See the
[primary register contract](PROVENANCE.md#m2-deterministic-prng-sources).

One foreground, non-reentrant owner must establish active/awake operation,
stable matching clock CMD/STA at undivided RC16 or XOSC32, IEN0/1/2 zero,
and exclusive ADC/PRNG/CRC/CSP ownership. ADC must already be quiescent:
STSEL11, ST0, no queued single conversion or other ADCCON3 activity.
ST0 cannot prove that history. No concurrent CSP consumption, RF/noise seeding,
RND/ADC writer, interrupt, sleep/reset or ownership handoff is allowed.
Clock settings can change only between successfully idle calls, not within a
call. Observations reject visible violations, but cannot detect every transient
or past violation. This service configures no clock, ADC, IRQ or radio state.

Seed accepts all uint16 values except the documented fixed points `0000` and
`8003`. It writes the high byte and then low byte **to RNDL twice**, reads
RNDL then RNDH separately, and verifies the complete seed without advancing.
Reset's FFFF does not replace an explicit successful seed. A subsequent
explicit seed is allowed only after this driver's successful idle history,
with current hardware state still matching the retained previous state.
RNDH is never written: that would invoke CRC, not PRNG seeding.

`next16` requires a positive limit1..255. It checks retained state and shared
control again before issuing **exactly one RCTRL01 command**, then performs at
most `limit` complete control observations for the documented self-clear.
Completion may precede the first observation: seeing busy first is not required.
The final permitted poll may succeed; a still-pending command returns
`PRNG_POLL_LIMIT`. Reserved RCTRL10/stopped11 are never interpreted as completion.
Separate low/high reads return the full 16-bit state; a final idle/shared-state
check and forbidden/unchanged-state rejection precede publication.
No CPU read advances the LFSR; no undocumented latch or fixed delay is assumed.
There is no exact-transition software computation in the driver, so arbitrary
valid-state corruption cannot all be diagnosed.

ADCCON1 is mixed-command state, not a blind read/modify/write target.
The only command write is37: preserve STSEL11, write ST0 (never start a
conversion), reserved low bits11 and RCTRL01. EOC is read-only here; its
write0 has no clearing effect. No ADCH read clears it. Stable EOC0 and EOC1
are both supported; a change within a call is an error. ADC/clock/IRQ and
RND control observations have fixed bounded work, not a calibrated time bound.

Output is a complete caller-owned writable two-byte XDATA object, after the
entire linker-proved private prefix and ending below1E00. Link the driver
before caller objects; no implicit RAM pool, generic pointer or cast fallback
is provided. Invalid seed/argument/range/ownership and NOT_SEEDED perform no
MMIO or retained-state changes. All other errors terminal-latch the original
result; re-entry performs no MMIO, reseed, command or caller publication.
Compiler parameter scratch remains private. Every failed call preserves valid
caller output; successful two-byte CPU publication is fixed but not atomic.
A late command may still complete after error. No automatic retry, stop,
restore or clock repair occurs, and clearing C state alone is not recovery.
Only independently established full reset begins a new fault-free epoch.

The documented polynomial is `x^16+x^15+x^2+1`; each command performs 13
feedback shifts. Exhaustive original host models establish two fixed points
and two disjoint cycles of32,767 states. A 16-bit output is **not** a claim
of a65,535-state period, entropy, distribution quality or security.
[Offline evidence, bounded LG acceptance and remaining physical gates](VALIDATION.md#m2-deterministic-prng-coverage)
remain separate from the board fixture below.

### RF-noise entropy qualification boundary

The #10 [primary-source review](PROVENANCE.md#rf-noise-and-entropy-assessment-sources)
selects **raw receiver I-channel samples as a characterization candidate**,
not an accepted entropy source. The later binary health-test core and isolated
raw acquisition driver below are implemented; no board sampler, conditioner,
cryptographic DRBG or security-random API is implemented. There is no credited min-entropy,
approved sampling cadence or hardware characterization result. The
deterministic PRNG API above is unchanged.

TI SWRU191F distinguishes two mechanisms:

| Mechanism | Primary fact | Consequence |
| --- | --- | --- |
| RNDL/RNDH and RCTRL | Chapter14's explicitly seeded16-bit LFSR; CPU reads do not advance it | Expanding a seed does not add entropy or make this a cryptographic generator |
| RFRND at `0x61A7` | CC253x register p.272: bit0 IRND, bit1 QRND; upper six bits read zero | A register read is not eight random bits; the initial candidate uses IRND only, with no independent-Q assumption |
| Radio state | Sections14.2.2/23.12: receiver powered/on, no normal radio work during harvesting; wait for RX transients to settle, with RSSI-valid as the suggested indication | Dedicated RF ownership and bounded warm-up are required; RSSI-valid is not an entropy or sample-freshness test |
| RX mode | Section14.2.2 calls for infinite RX to avoid synchronization; p.259 distinguishes `RX_MODE=10` FIFO looping from `11` symbol-search disable | Do not equate FIFO looping with disabled synchronization. A diagnostic profile must explicitly establish the no-sync receive behavior |
| Published statistical illustration | Section23.12 pp.236-237 reports a slight DC component in roughly20 million IRND-derived bytes, with mean127.6518, histogram and FFT | This is TI's illustration, not our measurement, a worst-case min-entropy bound or a guarantee against RF influence |

The reviewed TI passages supply no security-qualified read interval,
independence guarantee between consecutive reads or I/Q channels, or
environment-wide min-entropy bound. A tight loop, apparently balanced
histogram, RSSI value, timestamp, device identity or fixed seed cannot fill
those gaps. The existing passive receiver requires normal `FRMCTRL0=40` and
exclusive reset history; AUTOACK and TX have their own ownership contracts.
None may be silently reconfigured for noise harvesting or used as a
cryptographic fallback.

#### Required assessment and health-test contract

NIST SP800-90B, January2018, is the assessment reference, not a claim of NIST
validation. Its May2025 potential corrections are recorded with their
non-final status in the source ledger. The following requirements constrain
later work; **no measured H or deployed cutoffs are selected here**.

| Area | Required boundary |
| --- | --- |
| Source definition | Fix the raw symbol, exact RF/clock/channel configuration, acquisition cadence, warm-up, restart procedure, security boundary and operating envelope. Explain the physical unpredictability and possible attacker influence; repeated register values alone cannot distinguish legitimate repetition from stale sampling. |
| Raw evidence | Section3.1.1 calls for at least1,000,000 raw sequential samples and a separate1000-restart by1000-sample dataset. Its permitted concatenation uses consecutive segments of at least1000 samples; arbitrary debugger-paused fragments are not one continuous stream. Record all segment boundaries and demonstrate collection does not change the assessed source. |
| Entropy estimate | Use the non-IID track unless both the design argument and prescribed sequential/restart tests support IID. Characterize the supported environment and source-specific failures; neither a Shannon-entropy calculation nor a statistical pass establishes the required min-entropy bound. |
| Startup/on-demand tests | Sections4.2-4.3 require testing before release, with at least1024 consecutive samples for startup and at least equivalent on-demand testing. Successful startup samples may be used or discarded under the standard; for a bounded implementation, discarding them avoids needing a startup-output buffer. |
| Continuous tests | Test raw samples before conditioning. The Repetition Count Test (RCT) and Adaptive Proportion Test (APT) are the baseline, with additional tests for identified failure modes. Passing these tests detects selected failures; it does not qualify a source or establish H. |
| RCT | Section4.4.1 uses `C = 1 + ceil(-log2(alpha) / H)` and fails when the run count reaches C, including the first occurrence. H is assessed min-entropy per raw sample, not per packed byte. Never reset the run at a caller chunk boundary. |
| APT | Section4.4.2 uses nonoverlapping windows of1024 samples for a binary alphabet,512 for a nonbinary alphabet. The first sample defines the reference value and starts its count at1; fail at the selected cutoff. A binary IRND stream remains binary even if stored eight samples per byte. |
| Cutoffs/errors | Cutoffs depend on assessed H and a documented false-positive target; Section4.3 recommends alpha between `2^-20` and `2^-40`, permitting lower values. Do not adopt the illustrative `H=1` or its table cutoffs as a CC2530 measurement. Counter overflow, acquisition gaps and health failures must be explicit errors, not silent restarts or fresh seeds. |
| Conditioning | Sections3.1.5/3.2.3 distinguish vetted conditioning functions from arbitrary mixing. AES-CMAC is a candidate because an AES block service exists, but that service alone implements neither CMAC nor a conditioner. Input entropy accounting, exact function/key/input/output sizes and independent known-answer tests remain required; XOR, CRC, LFSR expansion or a hash cannot create missing entropy. |

No samples are to be suppressed because their values look undesirable:
in particular, the LFSR's forbidden seeds0000/8003 are **not** a raw-noise
filter. Neither conditioning nor health testing may conceal acquisition loss.
The intended error policy is terminal failure with no usable RNG output,
no automatic retry/reseed and no timer/LFSR fallback. Reset or explicit repair
would require fresh source admission and startup tests, not clearing a C flag.
These are proposed service requirements, not a new successful unsupported API.

#### DRBG and separately scoped characterization

SP800-90C is **final, September2025**, not the older draft. Its RBG2 model
illustrates why initialization alone is insufficient: Section5.3 requires
healthy validated source material and reseeding before generation once at
least `2^17` output bits have been generated since instantiation/reseed.
The rendered p.55 was checked for that exponent. This is a requirement of
that construction, not an adopted API limit or a claim that RBG2 has been
implemented. A final DRBG/construction choice must account for source
availability during radio operation, algorithm-specific instantiation/reseed
entropy, request/output bounds, reset/rollback handling and actual CC2530
resources. An external provisioning-only construction is a different design,
not permission to substitute a fixed or device-derived seed.

The next diagnostic must be boot-disarmed, receiver-only with AUTOACK and
TX disabled, and separate from normal MAC ownership. Before any physical
collection it needs a reviewed exact no-sync register profile, finite
warm-up/acquisition/stop deadlines and independent work limits, fixed
ordinary-XDATA buffers below1E00, genuine linked/alias-aware proofs and a
separately authorized board/image/channel/return scope. The first goal is
faithful raw samples and explicit failure reporting, not generated key bytes.
No sample rate or buffer/deadline budget is accepted before that profile is
defined and measured.

Characterization must cover the intended voltage/temperature/clock/RF
envelope, restarts and controlled receiver/clock failures, including possible
external signal influence. Tests must preserve raw samples and timing/gap
metadata, not discard failing batches. Actual captures and board bindings
stay outside Git/CI; public tests use synthetic vectors, including stuck,
biased, periodic-but-balanced and chunk-boundary cases. Exposed diagnostic
samples must never later seed production secrets. Assessments and independent
estimator checks are distinct from the implementation's small online health
tests; a million-sample dataset does not belong in CC2530 RAM.

This source review is **documentation evidence only**. #10 remains open for
the diagnostic, characterization, entropy estimate, conditioning/DRBG
selection and resource/physical acceptance. It neither authorizes equipment
access nor waives the BDB errata or security/commissioning entry gates.

### Binary raw-noise health-test foundation

The separate #65 [`noise_health`](../include/noise_health.h) module implements
the binary RCT and1024-sample APT algorithms above, not an entropy source.
It takes one explicit raw bit per call and touches no MMIO, radio, timer,
PRNG, AES or key state. It is not linked into any board firmware.

`noise_health_start(ctx, rct_cutoff, apt_cutoff)` initializes a new diagnostic
stream. The caller supplies integer cutoffs2..65535 and2..1024 respectively;
these domains only bound the algorithms and **do not approve a source's
H/alpha configuration**. There is no default cutoff, estimator or implicit
source admission. Starting a new C context does not establish physical
recovery or authorize retrying a failed entropy source.

`noise_health_push(ctx, sample)` consumes exactly one0/1 sample. The caller
owns a persistent context and preserves it across chunks; distinct contexts
can be interleaved, but calls are foreground/non-reentrant on SDCC.
RCT includes the first occurrence and continues across APT window boundaries.
APT's first sample selects its reference and starts the count at1; only a
successful complete window clears its count. Both comparisons fail at their
cutoffs. If both fail together, RCT is retained as the first reported cause.
There is no sample buffering, output release, filtering or conditioning.

Startup accounts for1024 samples. Only successful completion sets
`NOISE_HEALTH_MONITORING`; `startup_remaining == 0` alone is not success,
because the last startup sample can fail. Even MONITORING means only that
startup health checks passed, not qualified entropy or usable seed material.
Subsequent valid calls after a health failure return the retained result
without changing context. Invalid arguments and detected invalid state are
atomic errors; invalid arguments do not overwrite a retained health cause.
Range checks prevent counter wrap, but this is not a context-integrity scheme:
the caller must not modify initialized fields.

`make test-noise-health` runs the actual C implementation natively and in an
isolated genuine SDCC image. Shared cases exercise both failures, first-sample
counting, exact cutoff/startup/window boundaries, cross-window runs, context
interleaving, invalid bits and terminal re-entry. A deterministic4096-bit
alternation **passes**, explicitly demonstrating that health-test success
is not unpredictability. Native checks add4096 twelve-bit sequences and32
2049-bit synthetic streams against a separate whole-prefix counting oracle,
all65536 values of each cutoff parameter, malformed contexts and an exact
allocated context. Both board definitions passed ASan/UBSan.

Both linked images and complete CDB files are byte-identical:4880 CODE,
76 ordinary XDATA +64 reserved within the512-byte reservation budget.
The production object is1575 CODE,11 XDATA,4 DATA and7 overlay bytes;
the caller context is15 bytes on SDCC. The stack starts at21 and unwinds
to20; the SP7C/upper-IRAM guard passes. No measured high-water or IRQ-nesting
capacity is inferred from the unused guard. Complete CODE/CDB/parsed-map
identities, including private/helper metadata, are pinned from actual links;
17 artifact,5 snapshot and1 alias negatives retain the15-second simulator
bound. This is **host-tested, image-checked and simulated**, never a physical
noise/entropy result or full-stack fit. Do not flash `noise_health_test.ihx`.

### Isolated raw IRND acquisition

The #66 [`radio_noise_collect`](../include/radio_noise.h) driver takes one
explicit1..1024-bit capture per independently established full-reset epoch.
It is foreground/non-reentrant, receiver-only and separate from normal
RX/TX/AUTOACK; there is no implicit ownership transfer or board-image linkage.
The caller must establish exclusive radio/CSP/DMA/clock/ST0 history, awake
undivided XOSC32, disabled IRQ/RF masks/DMA and empty reset FIFOs.
Register observations cannot prove that history or ADC sample freshness.

The exact no-sync profile is `FRMCTRL0=4C`: RX_MODE11, AUTOACK0, TX_MODE00.
Ten settings are written and read back, with the existing FSCAL1 write00/
low-two-bit check and full-byte comparisons elsewhere. E3 enables RX.
Sampling starts only after owned RX-enable80, calibration idle, lock/RX-active
and RSSI-valid observations. Each sample executes one RFRND61A7 read; only
IRND bit0 is packed, first sample in the low bit of the first byte. QRND is
ignored; nonzero reserved bits are an error, not discarded data. The only
stop action is RXMASKCLR80, followed by confirmed idle and empty FIFOs.
No TX, ACK, FIFO read/flush, PRNG call or automatic retry is present.

An explicit positive timeout below the24-bit half-range and an independent
positive16-bit poll limit cover configuration, warm-up, sampling and stop.
The requested interval1..65535 is a minimum in raw Sleep Timer ticks between
the previous successful post-read observation and the next pre-read observation.
The first gap starts at warm-up completion. `first_before`, `last_after`,
`min_gap`, `max_gap` and `max_span` describe sequential observation brackets,
not captured ADC times, a calibrated sample rate or independent symbols.
A frozen clock terminates by the work bound. Executing CPU and no hidden
half-range/wrap gaps remain caller preconditions; this is not asynchronous
shutdown.

The request is11 and capture171 target bytes, including128 packed-data bytes.
Both must be disjoint ordinary XDATA after the entire service-private prefix
and before the first linked libc scratch object, including memcpy temporaries.
Link timebase, other service-private modules, this driver, then callers, and
prove the complete layout for each composition. Invalid arguments/storage
leave output and MMIO unchanged. After admission, output is zeroed and
`samples` counts every actual raw read, including a failing `last_raw`;
`timed_samples` counts only successful post-observations. Unused bits remain
zero. A partial/error capture is never OK. First operational error is retained;
subsequent calls have no MMIO/publication, and successful re-entry returns
ALREADY_USED. **RX may remain active after failure**; no hidden repair/reset
or normal-radio release is performed.

The genuine test caller feeds raw bits to the real health core in17-bit
chunks with one persistent context and explicit diagnostic cutoffs. A retained
RCT/APT failure is distinct from successful raw acquisition; a failing final
startup sample is not healthy even when its countdown is zero. Balanced
periodic raw input deliberately passes. No output is entropy-qualified.

Both boards are **host-tested, image-checked and simulated**, including
ASan/UBSan; the images/CDBs are identical. The composition is6423 CODE and
325 ordinary XDATA +64 reserved within8-KiB/512-byte budgets. Stack starts28,
unwinds to29 inside the caller checkpoint, and the SP7C/alias guard passes.
SP30 is the maximum observed at projected MMIO stops, **not a full-run
high-water measurement**. See [coverage](VALIDATION.md#m2-raw-irnd-acquisition-software-coverage).
No hardware behavior follows from the synthetic model. The subsequent
[genuine boot-disarmed board fixture](RADIO_NOISE_FIXTURE.md) now composes the
unchanged services with real board startup and clock selection. It admits
exact ARM then RUN for a fixed channel26/1024-bit one-shot, with separate
acquisition/health results and retained terminal ownership. Its own
host/image/simulator proof is not physical acceptance; hardware remains pending.
**Never flash `radio_noise_test.ihx`.**

### Deterministic PRNG board orchestration

The separate [PRNG fixture](DEBUGGING.md#deterministic-prng-board-fixture)
links original timebase/clock **before** the unchanged1,043-byte PRNG module.
The complete private prefix0000..0062 includes all three drivers' state and
parameters. Explicit caller objects follow it; generic runtime scratch is
separately excluded. The88-byte wire record and68-byte guarded caller buffer
remain within the374-byte reserved nonaliased budget, without a RAM pool.

The finite C schedule begins with actual NOT_SEEDED, zero-limit,0000 and8003
rejections, observing unchanged state/control/sentinel. On each clock it
loads1234, captures four public-prefix words, explicitly reloads1234 and
repeats, then runs seed1 and seed3 for32,767 consecutive successful calls each.
There is no reseed inside a period. Each period uses1,023 full32-word batches
and one real31-word tail. Both clocks total131,084 words,8 seed loads and4,100
batches; the byte heartbeat wraps to4. ENDREADY follows a verified return to RC16.

C compares each caller word with two separately sequenced, documented
non-advancing RNDL/RNDH read pairs while idle, checks public prefix constants,
guards and unfilled tails, and publishes actual valid counts/counters/results.
Bulk polynomial evaluation and complete-cycle set comparison exist only on
the host. Neither the wire nor runner invents per-batch poll totals: the
unchanged API exposes no such diagnostic. The fixed per-call limit is16.
Clock transitions retain the original bounded diagnostics and failure contract.

Wirev2 changes only flag policy, not byte layout or the PRNG service. C retains
the previous raw `flags[9]` before taking its next snapshot. All nine other
flags and IRCON's low seven bits must equal the immutable initial values;
STIF may remain unchanged or rise0->1, never fall1->0. SWRU191F p.47 and
11.1-11.2 p.129 establish the hardware-latched compare flag and default
compareFFFFFF while the Sleep Timer runs from reset, even with IRQs disabled.
This foreground/reset-history contract never clears IRCON, writes ST0/1/2,
changes compare or enables interrupts.

The host orders immutable initial flags -> C snapshot -> live reads -> the
next checkpoint's C snapshot. C0/live1 at one checkpoint is legitimate;
after observing live1, a later C0 is not. Initial and observed wire bytes
remain raw, never rewritten to match live state. The same history spans
INIT, READY, probe and final FAULT. Result `flag_history` records at most one
observed STIF transition, its C/live source and subsequent preservation count;
an initially set flag does not prove an observed transition. It is neither
an event/wrap counter nor calibrated timing.

Normal full acceptance stops at ENDREADY **before** a distinct deliberate
fault path. A separate `stopped` invocation proves the entire same-reset
corpus, resumes through fixture-only ADCCON1=3F, and stops at the genuine
first next16 LCALL with its real arguments and complete frame. This write
selects documented RCTRL11/off with ST0/STSEL11/reserved11; EOC is read-only.
Real next16 must return UNSUPPORTED_STATE6 without publishing; two further
real API entries must retain6 without MMIO. The fixture records the expected
terminal fault, performs no cleanup/reseed/write and loops at FAULT.
Separate full-reset acceptance establishes recovery, never cleared software
state or an error return. Holding the CPU cannot cause an honest PRNG poll timeout.

The original wirev1 LG image has
[bounded short hardware evidence](DEBUGGING.md#2026-09-17-lg-prng-short-acceptance):
real benign rejections, two seed1234 loads and eight RC16 words, with the
four-word prefix repeating after reseed, two additional non-advancing CPU
read pairs per word, guards and unfilled tails. This observes only the short
RC16/seed1234/EOC0 case, not XOSC32, complete periods, all valid states,
RCTRL11 rejection or recovery. Its
[long run stopped on the old fixture flag policy](DEBUGGING.md#2026-09-17-lg-prng-long-run-flag-policy-interruption),
not a PRNG error; its final32 words were not host-accepted. That halt is now
historical after the parent programmed the unchanged7289-byte wirev2 and
passed the [corrected short case](DEBUGGING.md#2026-09-17-corrected-lg-prng-short-acceptance).
It observes the same bounded RC16/seed1234 case, with six raw initial/C/live
flag observations and no STIF transition, not physical transition handling.
The same wirev2 subsequently passed
[full-stopped hardware acceptance](DEBUGGING.md#2026-09-17-corrected-lg-prng-full-stopped-acceptance):
all131,084 words/four periods/both clocks, the real after-C/live STIF race
and2,145 subsequent preserved observations through XOSC32, END and probe.
Raw C0/live80 at observation1969 was retained, not normalized; final C/live
IRCON80 and all other flags matched. Genuine RCTRL11 rejection and retained
6/6/6 returns preserved the full caller batch and probe sentinel.
A [separate full-reset recovery](DEBUGGING.md#2026-09-17-corrected-lg-prng-full-reset-recovery-acceptance)
then passed the entire corpus again on the same image. Its fresh reset epoch
established new counter/fault/flag history, not a C-state clear or continuation.
At observation1859, previous IRCON0/raw C80/live80 recorded a distinct
`c-snapshot` transition;2,253 later observations preserved it through END.
The final halt is ENDREADY016A/config26/RC16, fault0, probe returns0/0/0,
C/live IRCON80; no probe execution or resume followed. The stopped FAULT is
historical. This completes the bounded LG short/stopped/reset-recovery gate;
generic/EOC1/physical poll-fault and broader M2 gates remain open.
This is not entropy or timer calibration.

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

The RF-capable `radio_rx_fixture` alone has a1024-byte nonaliased reservation
budget (538 ordinary+64 status reserved used). Its96-byte wire state and128-byte
frame remain ordinary allocated XDATA. No component or older board budget is
expanded; the standalone RX foundation still fits512.

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

## Integrated protocol resource budget

`make test-protocol-budget` links **one** unbanked SDCC image containing
MAC, NWK Data, APS, ZCL frame/value codecs and Read/Discover/write dispatch, plus
a synthetic two-peer caller. It executes Discover, uses the returned ID in
Read, serializes/decodes both replies and checks independent whole-frame
golden vectors. Both manufacturer layouts, a maximum 125-byte MAC body,
space-error responses and malformed no-response writes are exercised.
Reserved standard declarations fail atomically for Read/Discover; absent
received IDs still produce negative Read records with the original ID.
No board image, radio operation or fabricated successful service is added.

The generated `build/<board>/protocol-resources.json` (or selected `BUILD`)
contains actual linked totals, per-object contributions/budgets, artifact
hashes and simulator-observed SP. Run the target successfully before using
the report; its hashes identify the exact artifacts, not an arbitrary later
build. It is excluded from the unchanged CI board-artifact whitelist.

The following table is the historical seven-module measurement with SDCC
4.2.0 #13081 (Mac OS X x86_64), matching both board definitions. The current
eight-module write-enabled image and its before/after objects are recorded in
[production CODE headroom](ZCL.md#production-code-headroom):
23,541 CODE and1,639 ordinary XDATA plus64 reserved, with unchanged caps.
The write module contributes1,552 CODE/144 XDATA/zero persistent IRAM;
the synthetic caller's equivalent header representation was compacted.
Production ZCL saves861 CODE bytes and the integrated link saves1,034,
without changing the caller or vectors. The recovered1,035-byte CODE-budget
margin is not full-stack headroom.

| Object / subsystem | CODE bytes | Ordinary XDATA bytes | Persistent IRAM bytes | Overlay IRAM bytes |
| --- | ---: | ---: | ---: | ---: |
| MAC, including its command/Beacon code | 7,004 | 207 | 15 | 10 |
| NWK Data | 2,292 | 96 | 12 | 10 |
| APS | 1,626 | 69 | 8 | 7 |
| ZCL frame | 1,173 | 35 | 6 | 15 |
| ZCL value | 1,710 | 46 | 15 | 0 |
| ZCL attributes | 2,090 | 148 | 6 | 12 |
| ZCL dispatch | 2,864 | 135 | 15 | 3 |
| Synthetic integration caller/constants | 3,370 | 735 | 0 | 0 |
| Shared runtime / linked remainder | 700 | 29 | Accounted below | Shared |
| **Linked total** | **22,829** | **1,500** | **77 persistent** | **15 shared, not summed** |

Object CODE includes constants/startup contributions where present, and
unused functions retained in those linked objects; it is not the isolated
size of one executed API. XDATA includes parameters and compiler scratch.
The harness has five independent frame buffers totaling 549 bytes, not a
production packet pool. It deliberately does not overlap codec input/output
storage. CRT/libc costs are the linked remainder, not charged repeatedly.

Against the unmodified image rebuilt with this toolchain (22,200 CODE,
1,499 ordinary XDATA), the declaration check adds 178 CODE bytes and three
object-overlay bytes, without increasing shared overlay or persistent IRAM.
Its integration regressions add 451 CODE and one XDATA byte. No budget is
raised; observed stack usage is unchanged.

The checked limits are **24,576 CODE bytes**, **2,048 XDATA bytes including
64 reserved status bytes**, per-object budgets in `tools/protocol_resources.py`,
and stack start no higher than `0x68`. This historical build consumes 1,564 of that
XDATA reservation. The 32-KiB window has 9,939 bytes remaining; ordinary
allocation below `0x1E00` has 6,180 bytes remaining. These are address-space
remainders, **not guaranteed capacity for the complete Zigbee stack**.

IRAM is the tighter constraint: 77 persistent bytes, 15 shared overlay bytes,
8 register-bank bytes, one bit-storage byte and one packing-gap byte precede
the stack at **`0x66`**. Observed peak SP is **`0x7A`**: 21 stack bytes used,
only **five bytes before the preserved `0x80` upper-IRAM guard**. The
simulator also verifies final unwind, untouched upper IRAM/unallocated XDATA
and disabled interrupts. This peak is for the executed foreground vectors,
not a worst-case call-graph proof or an interrupt-nesting allowance.

The initial combined link failed despite ample flash/XDATA: the seven protocol
objects alone required **154 persistent IRAM bytes**. MAC/NWK/APS emission was
separated into leaf helpers so compiler temporaries can use shared OSEG.
Selected MAC/ZCL pointer copies are top-level `volatile`, keeping their
storage in large-model XDATA instead of long-lived internal-RAM spills.
The pointed-to bytes are not volatile; argument representation, wire/API
semantics, stable-storage preconditions and foreground non-reentrancy remain
unchanged. SDCC requires matching qualifiers in declarations and definitions.
This is an IRAM/code/XDATA tradeoff, not a speed optimization or new ABI mode.
All prior component guards/tests remain; the integrated image gets its own
explicit budget rather than inflating existing limits.

Not included: radio/platform services and live queues, the separate NWK
Beacon metadata decoder, stateful MAC/NWK/APS, AES/CCM and durable NV, ZDO,
commissioning, transaction/binding/reporting state, device clusters, sensor/
display, sleep, ISR nesting and banked CODE. These still require explicit
budgets and integration evidence; **complete-stack fit remains unproven**.

## Bounded init-time TX and CCA

The isolated [TX/CCA composition](RADIO_TX.md) links the actual timebase,
quiescent FIFO and new `radio_tx` services in that order. Caller-owned
preload/clear operations have separately checked deadlines. Direct TX and
controller-gated TX-on-CCA require fresh TXDONE and verified idle; a CCA-only
sample reserves no future airtime. Only channels 11..26 and explicitly
requested raw TXPOWER `05` are supported. Address/source-match RAM and board
GPIO remain untouched; filtering/AUTOACK are disabled.

This is a reset-exclusive, foreground-only owner, not a unified radio runtime.
It may follow its own verified completions and the FIFO/clock/timebase
preparation, but **not legacy RX or queue RX service in the same reset epoch**.
Error returns may leave RF active and cannot authorize implicit flush/retry
or reset. A copied queue TX candidate remains only caller-owned bytes, not a
controller transaction; that combined image has not been proved.

The standalone executable uses 8,526 CODE bytes and 374 ordinary XDATA +64
reserved, within its unchanged 512-byte reservation. Stack begins at `59`,
observed MMIO peak is `6C`, and the upper-IRAM guard remains intact. These
host/image/simulator results neither add a board image nor establish complete
stack fit, ACK timing, calibrated output power or on-air acceptance.

The separately selected [TX board fixture](RADIO_TX_FIXTURE.md) links
timebase/FIFO/TX/clock before every startup/board/example/state caller and
preserves nine immediate relocated listings. Its24-byte state and8-byte mailbox
sit after the entire227-byte service/compiler prefix. Total ordinary XDATA347
plus64 reserved fits the existing512-byte fixture budget; MMIO-sampled SP`0x74`
and full-run simulator high-water`0x77` remain below the unchanged upper-IRAM
guard. These are exercised-path measurements, not a universal stack bound.
Two explicit finite ARM/RUN stages return
ADMITTED without MMIO before any real clock/FIFO/RF work. Only one IF_CLEAR
attempt is possible per full reset; successful/busy quiescence and final
explicit FIFO clear precede END. FAULT cannot authorize retry or cleanup.

That composition initially failed genuine lower-DATA/OSEG allocation. The
clock's ABI-compatible XDATA staging and generic-store leaves reduce its
permanent DATA45 to12, with XDATA44 to54 and1875 module CODE bytes. Sequential
request/rollback work reuses one private counter; published diagnostics remain
distinct and retain their existing observation boundaries. This is not extra
IRAM, a new memory model, weaker stack guards or a change to oscillator,
deadline or rollback policy. Every other clock consumer needs its corresponding
new exact image/ABI proof; previous image-specific hardware acceptance is
historical rather than inherited.

## Scheduling and ownership

The isolated [bounded radio queue composition](RADIO_QUEUE.md) now supplies
two copied RX slots, one copied TX candidate and four IRQ-safe request
cookies, with explicit full/empty/cancel/retained-fault behavior. It calls the
real passive receiver for at most one event per foreground service. The
receiver still requires all interrupt enables zero; the cookie producer is
not RF delivery or a peripheral dispatcher. Its separate 1-KiB XDATA/8-KiB
CODE budget does not alter the protocol-resource image or prove full-stack
fit. No TX/ACK, continuous reception, board image or hardware claim follows.

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

## Reserved flash read foundation

`include/flash.h` and `src/flash.c` add an isolated, read-only first M2 slice.
The CC2530F256 partition is fixed before introducing a writer:

| Region | Physical main-flash range | Policy |
| --- | --- | --- |
| Current unbanked CODE | `00000..07FFF` | Existing linker/image bound, unchanged |
| Reserved NV page 125 | `3E800..3EFFF` | Read via relative page index 0 |
| Reserved NV page 126 | `3F000..3F7FF` | Read via relative page index 1 |
| Entire lock/config page 127 | `3F800..3FFFF` | Never exposed |
| Separate information page | XDATA `7800..7FFF` | Never exposed |

`flash_nv_read(page, offset, output, length)` copies 1..32 bytes within one
reserved page. It verifies CC2530/256-KiB-flash/8-KiB-SRAM identity, idle flash
status, IRQ/DMA exclusion and stable awake undivided clock ownership, selects
XBANK7 (`E800..F7FF`), stages the bytes privately, then restores and verifies
the original mapping before publishing. XMAP is rejected, never enabled;
FMAP, FCTL, FADDR and FWDATA are never written. All cache modes are preserved.
The chip-information reserved upper bits are not assumed zero.

Invalid arguments/ranges/private-prefix overlap have no MMIO or fault-latch
effect. Other failures leave caller output unchanged and retain the first
fault; no retry, fault clearing or post-fault mapping restoration is attempted.
Later calls perform no MMIO. Exclusive foreground ownership, unbanked
execution, no concurrent output observer and no other flash writer are
preconditions, not consequences of these observations. A concurrent writer
can stall instruction fetch; a bounded byte count is not a hardware timeout
or busy-flash recovery guarantee.

The separate `flash_test.ihx` is host-tested, image-checked and alias-aware
simulated, never a board image. This reader is unchanged by the separate
executor and write-policy layers below. Interrupted-word history and
write-count limits cannot be inferred from an erased-looking readback.
Hardware acceptance and durable records remain separate gates.

## Internal RAM flash command executor

`include/flash_exec.h` and `src/flash_exec.c` implement the isolated M2 #6
dependency. `flash_exec_command(operation, page, offset, word, poll_limit)`
accepts ERASE=1 or PROGRAM=2, relative page0/1, and a byte offset: zero for
erase, or four-byte aligned within the 2-KiB page for program. It sets the
actual FADDR word address within `FA00..FDFF`; the command path writes FCTL
and, for an accepted program command, four staged bytes to FWDATA. It never
writes the information-page window or accepts page127.

This is **not a public erase/program or persistence API**. `FLASH_EXEC_IDLE`
means command acceptance and subsequent idle controller status, not verified
flash contents, erase history or a durable record. The public writer below
enforces write-count/interrupted-word history and performs actual readback;
even an idle controller may have timed out without programming the word.

The foreground contract requires normal SDCC bank0/DPS0 ABI, common unbanked
callers, CC2530F256 identity, stable awake undivided RC16/XOSC32, IRQs disabled,
DMA unarmed/unrequested, and exclusive controller/mapping/clock ownership.
The program source is four ordinary XDATA bytes after the entire linked
executor/compiler private prefix. Invalid parameters/ownership do no MMIO
and do not latch a fault. Other failures retain their original result, with
no retry, implicit cleanup or subsequent-call MMIO.

Before issuing a command, C copies and physically reads back the volatile
123-byte RAM code buffer, stages word/limit inputs, and verifies FADDR and
MEMCTR.XMAP. The naked entry tail-jumps into mapped RAM using the genuine
common-C return frame. All critical branches are relative, with no CODE
loads, helper calls or interrupts. `RET` is reached only after BUSY, WRITE
and ERASE are all clear. Only a successful idle return restores the original
MEMCTR from common CODE; FMAP and cache mode are preserved.

`poll_limit` permits 1..65,535 completed FCTL polling reads, separate from
the initial acceptance read and preflight observations; it is not elapsed
time. If any active-controller bit remains at exhaustion, RAM stores
`FLASH_EXEC_RAM_STOP=7` and the last FCTL byte, then stays in a RAM-only
self-loop even if the controller later becomes idle. XMAP and the genuine
caller frame remain intact. Recovery requires a separately authorized reset,
not a fabricated safe timeout return to possibly busy flash. The nine-byte
private `flash_exec_work` contains command, four word bytes, little-endian
limit, result at byte7 and last RAM-observed FCTL at byte8.

The exact standalone image has 1,451 CODE bytes, 168 ordinary XDATA bytes
plus the unchanged 64-byte status reservation, and observed peak SP `2B`.
Its copied template is `0062..00DC`, XDATA buffer `0009..0083`, mapped CODE
`8009..8083`; the whole private prefix ends at `009A`. These are verified
test-image allocations, not fixed addresses imposed on future integration.
The static staged command/data sequence is 42 **tabulated best-case**
CC2530 clocks (2.625 us at nominal16MHz); TI explicitly does not make its
instruction table a general worst-case timing guarantee. Physical XMAP,
20-us transfer timing, flash effects and reset/power interruption remain
the separate #8 hardware gate, not simulator evidence.

`make test-flash-exec` is host-tested, exact-image-checked and alias-aware
simulated. **Never flash `flash_exec_test.ihx`.** Board images exclude
the flash harnesses; only the separately selected fixture below links services.
The component adds no automatic device access or completed M2 milestone.

## Verified reserved-page erase and program

`include/flash_write.h` and `src/flash_write.c` compose the unchanged reader
and RAM executor. `flash_nv_erase(page, poll_limit)` issues an actual page
erase and verifies all2,048 bytes as `FF` in32-byte reader chunks.
`flash_nv_program(page, offset, word, poll_limit)` stages four bytes,
checks the target word is blank, issues an actual program command and reads
back all four bytes. Pages are partition-relative0/1, program offsets are
four-byte aligned, and caller input must be beyond the entire linked private
prefix and below the status/IRAM-alias reservation. No partial publication,
automatic retry or successful placeholder is used.

The strict history policy is deliberately narrower than silicon's limits:

- Reset starts with unknown history on both pages, even if readback is allFF.
- Only an accepted, non-aborted erase and complete page verification establish
  a new page epoch. Erase invalidates that page's previous epoch before the
  command; only successful verification clears its bitmap.
- Each512-word page has a64-byte attempted-word bitmap. The bit is consumed
  **before** entering the executor, including allFF data, rejected commands
  and attempts that never return. Repeated programming of that word is
  rejected without MMIO; healthy erasure of one page does not reset the other.
- One attempt per word implies at most512 program attempts per page and at
  most one zero-programming attempt per bit: below TI's8/1,024/two limits.
  This is not lifetime erase-cycle/wear accounting or a persistent journal.

Ownership spans the entire epoch, not just a function call: direct executor,
debugger, DMA or other out-of-band writes are forbidden. The service cannot
infer an invisible previous attempt from unchanged allFF contents. There is
no history import, fault-clear or software-reset-history API. After an error,
separately authorized full-reset recovery starts unknown again; it does not
make existing words writable without another verified erase.

Benign argument/range/ownership/history/used-word errors return explicitly
without MMIO or replacing the last admitted operation's diagnostic. Other
failures retain their public reason and underlying reader/executor result;
all later write/erase calls perform no MMIO. The eight-byte
`flash_write_diagnostic()` record contains result, operation, relative page,
little-endian byte offset, phase, executor result and reader result.
An active-controller RAM fail-stop leaves public result
`FLASH_WRITE_PENDING=10`/COMMAND and executor result field `FF` (no return),
with the executor's separate RAM diagnostic containing RAM_STOP. No caller
can mistake this for verified completion.

The standalone composition links **executor, reader, writer, caller** in that
order. It uses417 ordinary XDATA bytes plus64 reserved (481, below512), with
the complete private prefix through `0193`; observed peak SP is `36`.
Its3,345-byte linked image and actual copied RAM commands have host,
exact-image and alias-aware simulator evidence. **Never flash
`flash_write_test.ihx`.** The simulated peripheral supplies flash effects;
this is not physical program/erase,20-us timing, wear, power-cut recovery,
a durable NV record or authorization for the separately gated #8 fixture.

## Boot-disarmed flash board fixture

The separate `IMAGE=flash_fixture` composes the unchanged executor/reader/writer
with original foreground orchestration and the existing board startup.
The [full contract](FLASH_FIXTURE.md) fixes the8-byte mailbox, two distinct
ARM/RUN packets,256-poll admission windows, one selected relative page and a
five-step history/erase/program sequence. It boots DISARMED, clears stale
commands on reset, terminates on success/fault, and never retries or resets
history. Invalid/default/expired admission performs no service MMIO.
All GPIO remains board policy; no RF, clock switch, ISR or DMA is added.

The entire404-byte service/compiler prefix and emitted service instructions
are identical to the standalone #7 composition. The32-byte fixture/caller
addition gives436 ordinary XDATA plus64 reserved:500 of its separately
accounted512-byte budget. Existing component budgets and unbanked/alias
bounds do not change. IRAM stack is reserved at`21..FF` with simulated
peak`36`; DATA/BIT/typed XDATA ABI and all relocated listing snapshots are
checked. No generic pointers, hidden pool or copied flash algorithm.

Generic/LG images are4168/4208 CODE bytes with host/image/simulator evidence
only. RAM_STOP keeps the genuine command frame and public PENDING state;
it is not a fixture FAULT return. The current debugger cannot read mapped
CODE or address its breakpoints, and its DEBUG_INSTR-based ordinary-memory
inspection has no physical validation for busy-flash/XMAP states.
See the [visibility and recovery blockers](FLASH_FIXTURE.md#debugger-visibility-precise-unresolved-blockers).
Private full-flash/information backup and full excluded-region verification
must precede destruction under new explicit authority. No hardware gate,
NV durability or recovery result is implied; #8 remains open.

## Persistence design

The isolated [generic snapshot journal](NV_RECORDS.md) now implements one
1..128-byte opaque record across the two reserved pages. It composes the real
reader/writer/RAM engine, always erases the inactive page before replacement,
programs a separate commit word last and preserves the selected old record.
Unknown versions, conflicting/nonadjacent generations and corrupt-only
storage never silently initialize; degraded selection requires explicit
permission before replacing its damaged page. Generations do not wrap.

The existing flash instructions are byte-identical. The composition uses
7,046 CODE bytes, 797 ordinary XDATA +64 reserved within a separate 1-KiB
budget, and a private/compiler fence through `0294`. Observed SP is `57`;
IRAM alias and upper-IRAM guards remain intact. Host/model cuts and actual
linked reset/commit/RAM execution are not electrical power-failure evidence.
The 32-attempt/page runtime erase budget is volatile; lifetime wear remains
unknown, not inferred from committed generations or blank contents.
No board image, security-counter/key schema or authenticated resume is added.

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
