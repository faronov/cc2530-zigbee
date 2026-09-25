# Offline bounded MAC-TX scheduler (#13)

Original BSD-3-Clause protocol preparation, **not a radio adapter, functioning
MAC, PHY-timing implementation, network, association or Zigbee join**.
The scheduler and dedicated tests/proof reuse the unchanged MAC codec.
`mac_tx_test.ihx` is a synthetic standalone executable: **never flash it,
select it as a board IMAGE or upload it as a firmware artifact**.

## Primary evidence and scope

The actual **IEEE Std 802.15.4-2006**, revision of 802.15.4-2003, approved
7 June 2006, published 8 September 2006, was read directly from the repository's
previously cited [UBC-hosted primary PDF](https://people.ece.ubc.ca/~edc/7860/data/802.15.4-2006.pdf).
Retrieved 2026-09-18: 3,784,305 bytes, SHA256
`d245c8bb208f6cdb585fb753cefa67e367d30eebd55b9ed9957c8fd152d6a055`.
This is a mirror of the IEEE document, not UBC-authored protocol advice.
The [IEEE Xplore publication](https://ieeexplore.ieee.org/document/1700009)
returned HTTP 202/WAF challenge; it did not supply an independent copy.

References below use **printed** page numbers. Functional facts are paraphrased;
the specification and third-party implementations are not vendored.

| Decision | Primary source | Applied rule |
| --- | --- | --- |
| Selected PHY | 6.1.1 Table 1 p.28; 6.5.2.2 p.47 | Legacy 2450-MHz O-QPSK, 62.5 ksymbols/s, 4 bits/symbol: one abstract symbol is exactly 16 microseconds; two symbols/octet |
| PPDU overhead | 6.3 pp.43-45; Tables 19/20 p.44 | Four-octet preamble + one-octet SFD = ten SHR symbols; one PHR octet = two symbols |
| Turnaround | 6.4.1 Table 22 p.45; 6.9.1-2 pp.63-64 | `aTurnaroundTime=12` symbols; physical RX/TX turnaround remains the adapter's responsibility |
| CCA | 6.9.9 p.66 | CCA detection takes eight symbols; this module does not select/measure a threshold or implement the PHY |
| Backoff | 7.5.1.4 pp.170-172, Figure 69; Table 85 p.159 | NB initially zero, BE initially macMinBE; draw `0..2^BE-1` complete 20-symbol periods; unslotted CCA immediately thereafter; idle leads immediately to TX; busy increments NB and saturates BE |
| Fixed parameters | 7.4.2 Table 86 pp.163-164 | Selected defaults minBE=3, maxBE=5, maxCSMABackoffs=4, maxFrameRetries=3; no run-time PIB/configuration API |
| Busy termination | 7.5.1.4 p.171; 7.5.6.1 pp.185-186 | Failure when NB becomes greater than 4: at most five CCAs per transmission attempt; direct channel-access failure ends the transaction, not another frame retry |
| ACK window | 7.4.2 Equation (13), Table 86 p.160 | `20 + 12 + 10 + 6*2 = 54` symbols, including complete ACK reception; **not 54 Sleep Timer ticks** |
| ACK body | 7.2 pp.137-138; 7.2.2.3.1 p.147, Figure 53 | Little-endian FCF, one DSN, then two FCS bytes on air; caller supplies precisely three FCS-free body bytes; no addresses/payload |
| ACK TX versus RX | 7.2.2.3.1 p.147 | ACK type is 2, Pending is retained; other FCF subfields are zero on TX and ignored on RX. The wrapper normalizes ignored bits before the **actual** strict `mac_frame_decode` call |
| DSN | 7.5.6.1 p.185 | One device-wide sequence, initialized from a caller-supplied random octet; allocate/increment for each admitted frame; eight-bit wrap is intentional |
| TX addressing | 7.5.6.1 p.185 | Equal source/destination PANs require compression; different PANs must not compress. The scheduler rejects noncanonical TX accepted by the syntax-only codec |
| ACK/retries | 7.5.6.4.1-3 pp.189-190 | No ACK-request means no retries; matching DSN inside the ACK window completes the logical exchange; wrong DSN fails that attempt; direct retransmissions reuse exactly the original DSN/body, at most three retries |
| ACK timing at recipient | 7.5.6.4.2 p.189 | In the selected nonbeacon-enabled PAN, ACK transmission starts 12 symbols after received frame end; **not implemented or measured here** |
| Interframe spacing | 6.1.3 Table 3 p.30; 7.5.1.3 pp.169-170; Table 85 p.159 | At least 12 symbols for MPDU length <=18, otherwise 40. Our body excludes two FCS octets, hence the body threshold is 16 |
| Beacon Request admission | 7.3.7 p.156, Figure 62; 7.5.1.1 pp.167-168 | Unsecured version-0 command `07`, short destination/PAN `FFFF`, no source/compression/Pending/ACK request: eight body bytes. It uses the selected unslotted channel-access policy, never an ACK retry |
| Association Request admission | 7.2.2.4.1 p.148; Table 82 p.149; 7.3.1.1 p.150, Figure 55 | Command `01`, current device DSN, extended source with source PAN `FFFF`, coordinator destination mode/address as in the referenced Beacon, target PAN, ACK requested, Pending zero on TX (ignored on RX) |
| Capability encoding | 7.3.1.2 pp.150-151, Figure 56 | One capability octet: alternate coordinator bit0, FFD bit1, mains power bit2, receiver-on-idle bit3, reserved bits4-5, MAC security capability bit6, allocate-short-address request bit7 |
| Data Request admission | 7.3.4 pp.153-154, Figure 59 | Command `04`, ACK requested, Pending zero on TX; addressed requests compress the common PAN. Following an Association Request ACK, source addressing is extended; procedure timing/response retrieval is not implemented |
| Not an active scan | 7.5.2.1.2 pp.173-174 | An actual scan also saves/restores PAN filtering, changes channels, receives bounded Beacon windows and reports descriptors/unscanned channels. None follows merely from transmitting a request |

No timing/procedure facts were substituted from vendor SDKs, Contiki, a
generated catalog or a newer IEEE revision. The reviewed secondary reference's
pinned index (`6e575bdc8c1a68880ef7552d6190a0c6bc80b3a6`) was consulted only
for orientation; its R23/BDB material is not normative for this work.
There is no application cluster lookup or imported implementation.

Local `pdftotext` validation initially failed because the tool was absent.
Ubuntu `poppler-utils` and its missing `libpoppler134`/`libopenjp2-7` libraries
were downloaded/extracted only under ignored `build/mac-tx-dev/research` to
read the primary PDF. They retain their upstream licenses and are not
project/build/CI/runtime dependencies, copied code or repository artifacts.

## API, storage and ownership

`include/mac_tx.h` and `src/mac_tx.c` implement one caller-owned `mac_tx_t`,
one copied 125-byte candidate, and a bounded foreground state machine.
Use **one context/DSN owner per device**. A second context is not another
per-peer sequence generator. All codec and scheduler calls are non-reentrant;
serialize them across the whole foreground, including multiple test contexts.
Do not call them from ISR context or run a codec concurrently.

| API | Effect |
| --- | --- |
| `mac_tx_init(tx, random_dsn, now)` | Return `MAC_TX_INVALID` for NULL without mutation; otherwise initialize fresh-epoch storage and return `MAC_TX_OK`. All old actions/events must already be purged; no radio reset, entropy generation or recovery is performed |
| `mac_tx_submit(tx, body, length, now, lifetime, work_limit)` | Validate with the real codec, admit direct unsecured DATA v0/v1 or a canonical v0 Beacon/Association/Data Request command, copy bytes, replace DSN, reserve the sole slot. DATA requires both addresses; short/extended and unacknowledged short broadcast are supported |
| `mac_tx_copy(tx, body, capacity, length)` | Copy immutable body for an adapter. No private pool pointer escapes; exact capacity succeeds, shortage leaves bytes/length unchanged. Copy is not a send |
| `mac_tx_step(tx, now, event, action)` | One finite foreground step; NULL event polls. Produces a one-shot action or NONE plus current diagnostics. API OK means a step was processed, **not delivery** |
| `mac_tx_release(tx)` | Release only a DONE slot; preserves DSN, generation, last time and remaining IFS. FAULT cannot be released |

Arguments must describe actual disjoint storage, not overlapping context,
input, output, event or action objects. Context and caller output live in
ordinary writable storage, never compiler scratch, CODE, MMIO, status or the
IRAM alias. Read-only body/ACK inputs may be valid CODE or RAM objects.
SDCC uses three-byte generic pointers and its normal bank0/DPS0 large-model
ABI. No board GPIO, registers, USB, clock reader, heap, function callback,
hidden queue, persistent state or successful hardware substitute is present.

All public entries, including the memory-only initializer, return
`mac_tx_result_t`. The initializer's result does not verify or establish an
adapter epoch: fresh-epoch ownership remains the caller's precondition.
Its valid-storage behavior still zeroes the context, sets the supplied DSN,
and initializes `last`/`ready_at` to `now`.

Invalid initialization/admission/copy/release/step arguments leave state and outputs
unchanged. A full slot stays full through DRAW, radio operation, ACK wait,
cleanup **and DONE** until explicitly released. No overflow eviction or
implicit replacement occurs. All valid active steps consume finite work or
cleanup budget even when input is ignored. Repeated invalid calls are caller
bugs, not a progress mechanism.

Beacon Request admission reuses the codec's complete command/header checks.
Its body is `03 08 DSN FF FF FF FF 07`; the codec's receive-only allowance for
command Pending is explicitly rejected before transmission admission.
Requests and DATA share the same device-wide DSN sequence, owned slot,
CCA/backoff limits and IFS. Requests do not ask for ACKs or retransmit after
confirmed PHY completion. Channel-access failure is explicit; it is not a
successful scan or proof that no coordinator exists.

The separate [offline scan controller](MAC_SCAN.md) now leases this existing
transmitter across its channel/window procedure, without resetting DSN or
adding a nested `mac_tx_step`. Its explicit foreground pump grants and
confirmed restoration do not implement a radio adapter or association.

Unsupported: Beacon frames, commands other than Beacon/Association/Data Request
(including Association Response and Disassociation), destination-less Data Request,
the polling/response-retrieval procedure,
the association procedure itself, indirect transmission, slotted CSMA/GTS, MAC security,
enhanced ACKs/IEs, outgoing Frame Pending, DATA broadcast PANs and short
`FFFE` allocation sentinels. The existing
codec additionally rejects security/reserved/unsupported TX layouts and
ACK-requested short broadcast. The payload is opaque; parsing it is not
NWK/APS security or application acceptance.

The **both-addresses-present DATA** admission claim follows the existing
[MAC codec contract](MAC.md#supported-subset), not an unstated scheduler
assumption. `src/mac_frame.c::header_shape()` rejects DATA if either address
mode is `MAC_ADDRESS_NONE`; `mac_frame_decode()` applies that check, and
`mac_tx_submit()` rejects any non-OK decode before admitting a candidate.
The source-less Beacon Request exception does not relax DATA admission.

## Explicit interval profile

`CC2530_MAC_INTERVAL` adds the separate `mac_tx_interval_*` API in
`include/mac_tx_interval.h`. This is an interval-aware transmitter state
machine, **not a completed #13 radio adapter**. With the flag absent, the
original API, context, CODE and private ABI are unchanged. Its dedicated
legacy image still passes the existing immutable image/ABI checks.

The new context contains the existing immutable frame/DSN/control engine and
two six-byte target TX bounds. Only the new entry points may operate that
context; passing its embedded engine to `mac_tx_step`, `mac_poll`, `mac_join`
or the current BDB controller is unsupported. `engine.tx_end` stays unused:
no bound is installed there as a purported physical end. Admission, frame
copying, DSN allocation, backoff, busy-CCA limits, retries, work/lifetime and
confirmed cleanup reuse the existing implementation and strict codec.

Every fractional point is `(uint32 symbols, fine0..511)` in the same
continuous epoch. Comparisons require true intervals below the half range;
numeric order cannot establish that history. SENT/ACK report time is distinct
from the physical interval and must be at a symbol boundary no earlier than
the upward-rounded upper bound. Thus a delayed report can carry an earlier
physical interval without pretending that foreground processing was PHY end.
The driver must additionally establish action identity, unique own TX,
early RX arm, post-TX receive causality and valid CRC. Arithmetic cannot
establish any of those facts.

For TX end in `[L,U]` and causally post-TX ACK end in `[A,B]`:

| Evidence | Result |
| --- | --- |
| `B <= L+54`, exact three-byte ACK and matching DSN | ACKED, with original Pending retained |
| Same proven window, wrong DSN | Failed attempt, with the original retry/cleanup policy |
| `A > U+54` | Definitely late; no delivery or failed-attempt assertion from that frame |
| Neither timing proof holds | Local `TIMING_UNCERTAIN`, no invented ACKED/NO_ACK and no automatic retry |
| Time passes without an accepted ACK | Continue waiting for coverage, subject to real work/lifetime bounds |
| Loss-free RX_CLOSED watermark through `U+54`, inclusive | NO_ACK and bounded retry, still requiring confirmed quiescence |

All8192 legacy ACK FCF combinations with type2 retain the old ignored-bit
behavior through the actual canonical codec; the hardware filter is not
allowed to silently narrow it. Bounds incompatible with causal post-TX
reception fail locally. A too-early closure is an adapter fault. EMPTY,
physical STOPPED, or a missing foreground event is not a closure certificate.
IFS is at least12/40 symbols after the upward-rounded ACK/TX upper bound;
retry spacing covers the latest possible ACK deadline. This may wait longer
than an exact-capture implementation, never shorter. QUIESCE and its original
1024-symbol/16-step limit remain mandatory.

`make test-mac-tx-interval` includes native/nonrecovering-sanitizer boundary
tests and a separate SDCC image with52 independent fresh-reset case groups:
all fractional phases and symbol wrap, all ignored ACK FCF bits, exact and
one-fine-tick-outside deadlines, delayed reports, short/long/max-body IFS,
DSN wrap, complete retries, closure, stale identities, atomic invalid input,
work/time exhaustion and retained cleanup faults. No case writes a private
controller field to establish protocol progress.
The new composition reserves28672 CODE and1536 total XDATA bytes, without
changing any old budget. Both-board compiler artifacts agree at26797 CODE,
1285 ordinary XDATA plus64 reserved status bytes, initialSP4E and peakSP67
under7C in every case group. The checker pins the complete image, raw CDB
(including file-scope helpers), parsed linked allocations and all instruction
records; it executes every group with physical IRAM aliases and the unchanged
15-second subprocess deadline. Commit `8f5a314` passed
[full Actions36149393912](https://github.com/faronov/cc2530-zigbee/actions/runs/36149393912):
102/102 jobs, preserving all98 prior workers and adding the two interval
workers. Each new worker passed all52 groups and18807 artifact negatives;
their complete jobs took2m19s/2m23s. This profile is **host-tested,
image-checked and simulated**, not hardware-observed.

The same target also runs a **native-only composed receipt consumer** with
the genuine clock/Timer2/epoch/radio/attempt C services and the existing MMIO
model. Seven cases consume real returned intervals/bytes: timely ACK,
uncertain timing, EMPTY, bad CRC, wrong DSN, busy CCA after actual stop, and
retained late-arm failure. It does not substitute return values or populate
the MAC context. Its synthetic caller schedules invocation using the model;
that is not a production prepared-state clock API, a proof of exact hardware
CCA start, or a combined8051 radio/scheduler image.

Remaining #13/#14 work includes that actual action/clock binding, safe live
RX/AUTOACK handoff, continuous response ACK service, frame preservation and
loss-aware closure, then explicit interval-aware scan/POLL/association
contracts and the complete banked composition. The current raw attempt's
AUTOACK-off receive phase cannot be relabeled as any of these.
The physical, entropy/NV and #45 full-MLME conformance gates remain separate.
Never flash or publish `mac_tx_interval_test.ihx` as board firmware.

## Bounded Association Request admission (#14 prerequisite)

This addition is **transmit admission only**, not an MLME-ASSOCIATE primitive.
IEEE2006 7.3.1 requires an unassociated sender and a coordinator permitting
association, identified by scanning. These remain **caller/procedure
preconditions**: the scheduler has no selected parent, Beacon identity,
association state or IEEE-address registry. The caller must supply its actual
`aExtendedAddress`, the referenced coordinator's PAN/address/address mode,
the selected nonbeacon-enabled channel and truthful receiver/power capability.
All prior scan leases must already have completed confirmed restoration and
release. Neither a copied preliminary candidate nor this syntax check
establishes normative parent eligibility, security or compatibility.

The explicit project subset is unsecured version0, receiver-on end device,
requesting short-address allocation, capability **88 or8C only**. The power
bit is caller-supplied (other supply/mains respectively), not inferred from
the board. Alternate-coordinator, FFD/router, reserved and MAC-security
capability bits are zero. This is not a claim that Zigbee security is present.
Sleepy devices and other capability policies remain unsupported here.

The selected ED mapping was checked directly against **Zigbee Core R22,
05-3474-22, April19,2017, 3.6.1.4.1/Table3-62 pp.336-337**. That table uses
device-type0 for ED, caller power and receiver-on bits, security-capability0
(overriding the IEEE default meaning), and allocate-address1 except its
separate secure self-address/rejoin case. No join/rejoin procedure is
implemented here. The primary
[pinned R22 mirror](https://github.com/pvginkel/ZigBeeHomeAutomation/blob/fc30145012eacd3a5af170b8ae8e0d4c848c2525/Documents/docs-05-3474-22-0csg-zigbee-specification.pdf)
was re-read from the public-source cache; PDF SHA256:
`991dd02b7e5764ac349c47d5c4cf0fbe01529ff6594df03e8dad3d3d4df9e274`.
No SDK, secondary generated rule, private material or additional dependency
was used. BDB3.0.1 errata/commissioning/security gates remain open.

Exact FCS-free layouts (all multioctet fields least-significant octet first,
IEEE2006 7.2 pp.137-138):

```
short coordinator:    23 C8 DSN PAN[2] COORD[2] FF FF LOCAL_IEEE[8] 01 CAP
extended coordinator: 23 CC DSN PAN[2] COORD[8] FF FF LOCAL_IEEE[8] 01 CAP
```

These are exactly19/25 bytes. The codec enforces type/version/reserved bits,
no security/compression, extended source, source PANFFFF, destination present,
ACK request, exact command length and capability reserved bits. Short
coordinator FFFE/FFFF is rejected. Admission additionally requires Pending0,
destination PAN other thanFFFF and CAP88/8C. Destination PANFFFF is excluded
by this canonical target-PAN policy, not mistaken for a selected network.
Extended identities are copied wire bytes: parser acceptance does not verify
their allocation, ownership or match to a real permitting Beacon.

Both lengths require **40-symbol IFS** with the two on-air FCS octets counted.
The existing device-wide copied slot/DSN, CCA/backoff, ACK parser/matching,
identical retry bytes, four-attempt bound, work/lifetime and confirmed cleanup
are unchanged. An ACK is **only MAC receipt of this request**, never successful
association, address assignment or membership. ACK Pending remains diagnostic
metadata; it does not trigger polling or Association Response acceptance.

## Bounded Data Request admission (#14 prerequisite)

IEEE2006 7.3.4 pp.153-154/Figure59 was read directly from the same pinned
primary copy. The selected subset is an unsecured version0 request addressed
to a coordinator in a nonbeacon-enabled PAN, with short/extended source and
destination, common PAN other thanFFFF, compression, ACK request and Pending0.
The real codec already enforces the one-octet command payload, both address
sizes and short-address sentinel rejection. The scheduler additionally rejects
the codec's valid destination-less form and a broadcast PAN. No second decoder
or new scheduler state is introduced.

Exact FCS-free forms, with multioctet fields least-significant octet first:

```
short/short:       63 88 DSN PAN[2] COORD[2] LOCAL[2] 04
extended/short:    63 8C DSN PAN[2] COORD[8] LOCAL[2] 04
short/extended:    63 C8 DSN PAN[2] COORD[2] LOCAL_IEEE[8] 04
extended/extended: 63 CC DSN PAN[2] COORD[8] LOCAL_IEEE[8] 04
```

The labels are destination/source modes. Lengths10/16/16/22 require12/12/12/40
symbols of IFS, respectively, counting the two on-air FCS octets. Selection of
the actual coordinator, current PAN and valid allocated short address or actual
IEEE identity remains the caller's responsibility. **Association-response
retrieval requires extended source**, as specified in7.3.4; admission of a
short-source request does not establish that the device has an assigned address.
The caller must already own a released transmitter outside any scan lease.

This adds transmission admission only: no MLME-POLL primitive, wait interval,
automatic request after Association Request ACK, downlink receive window,
Association Response acceptance or membership transition. A Data Request ACK,
with or without Pending, is only receipt metadata. Every request reuses the
existing owned bytes, device-wide DSN, CCA/backoff, ACK parsing/retries,
lifetime/work bounds and confirmed cleanup.

The separate [conditional POLL controller](MAC_POLL.md) now leases this
unchanged transmitter for one legacy extraction. It witnesses the real
accepted ACK and preserves device-wide DSN/IFS and prompt TX retirement;
it does not add automatic polling to this scheduler or weaken QUIESCED.
Its continuous RX/ACK lease and captured-time preconditions still require a
real adapter.

## Explicit command whitelist and bounded storage reset

Admission caches the decoded payload's first octet in a `uint8_t` command
identifier and applies a positive whitelist: **Beacon Request**,
**Association Request with nonbroadcast target PAN and CAP88/8C**, or
**addressed Data Request with nonbroadcast PAN**. All other
commands remain unsupported even if the codec later adds them. Only after
explicitly identifying Association Request is its final octet read as CAP.
The unchanged real codec supplies the already-proven command-specific
addressing, ACK and exact-length guarantees; no redundant parser is added.
Successful decode bounds length to3..125 before narrowing `length-1` to an
unsigned octet. DATA admission is textually unchanged.

The maintainability follow-up used only two compiler/layout trials:

| Generic trial | MAC-TX CODE | Scan CODE | Result |
| --- | ---: | ---: | --- |
| Cached-ID whitelist, original individual resets |28,433|32,778|Scan exceeds8000 by10 bytes|
| Same whitelist, compact six-field reset |28,306|32,651|Both fit; before new target boundary assertions|

The retained reset uses `memset` on the enclosing context's byte
representation, from `offsetof(retries)` up to but **excluding**
`offsetof(stop_steps)`. This is exactly six contiguous `uint8_t` fields:
`retries`, `outcome` (NONE=0), `transmissions`, `uncertain`, `pending`,
`retry_pending`. It replaces only their original zero assignments.
It neither resets the whole context nor changes `stop_steps`, inactive
timestamps, frame tails, DSN/IFS handling or any other field's prior behavior.
Rejections still precede every public-state mutation. No new abstraction,
feature flag, public layout or cap was introduced to make the whitelist fit.

The prior validated final-octet classification is no longer used. Existing
header/capability/identifier tests remain;4,096 new whole-context native
comparisons independently apply the original individual assignments across
all256 background bytes, four admitted layouts, both IFS branches and time
wrap. They check positive results and unchanged dirty-state admission errors.
Genuine target assertions additionally preserve a nonzero `stop_steps` sentinel
and the actual cleanup counter across release/readmission. No old target case,
codec call or real return was removed. The assertions add36 caller CODE bytes;
the final measurements below include them.

## Actions and real-adapter requirements

Actions are emitted **once**, with `(generation, retry, nb)` identifying the
current operation. The nonzero 32-bit generation advances per admission;
`UINT32_MAX` is the last generation, then further admission rejects instead
of wrapping. A real reset/epoch change must purge all old completions before
initialization. Generation tags are local correlation, not bytes on air.

1. **RANDOM:** caller returns an independent uniformly distributed byte.
   Low BE bits select `0..2^BE-1`. Deterministic draws support CSMA/tests;
   neither this API nor the platform LFSR is cryptographic entropy.
2. **ATTEMPT:** copy the body; schedule exactly one eight-symbol CCA at `at`,
   after the selected complete backoff and any outstanding IFS. Transmit
   immediately iff this CCA is clear. Do not run a CCA-only call, wait, then
   use its old result to authorize unconditional TX. `until` is the absolute
   transaction deadline; no new CCA/TX is authorized at or after it.
3. **BUSY event:** confirms busy CCA, **no transmission for this attempt**,
   no outstanding buffer use, and quiescent radio. This allows another draw
   or terminal channel-access failure.
4. **SENT event:** requires confirmed physical transmission completion,
   a captured trailing-PPDU-end timestamp and RX **already armed** when ACK
   is requested. FIFO preload, queue removal, SFD alone, assumed delay or
   TX-call entry cannot produce this event. The scheduler checks only an
   impossible-early lower bound (`8 + 10 + 2 + 2*(body+2)` symbols from CCA
   start), not CCA/transmit/turnaround accuracy.
5. **ACK event:** caller has independently established a complete CRC-valid
   legacy frame in the selected PHY/channel/epoch and stripped PHR/FCS/radio
   metadata. Supply the actual trailing ACK-end timestamp and original body.
   The real codec decodes the canonicalized three-byte copy. No CRC/MIC,
   source authorization or replay protection is implemented here.
6. **QUIESCE:** cancel any not-yet-issued action and end radio/buffer use,
   then return **QUIESCED only after confirmed quiescence**. Logical ACK,
   no-ACK, unacknowledged completion, timeout and cancellation while radio is
   owned all go through this step. No slot is freed on a request alone.
   `at` is the request time; `until` is its independent cleanup deadline.
7. **FAILURE:** a confirmed adapter error latches FAULT, retaining ownership.
   There is no automatic TX retry or reset after an uncertain physical error.

The existing `radio_tx` is **init-time reset-exclusive** and cannot share an
epoch with `radio_rx_receive_init` or queue RX. It also does not establish
the continuous post-TX ACK reception/timestamp contract above. Consequently
there is **no implementation of this adapter** and no composition with these
drivers in the linked image. A future unified bidirectional owner needs its
own primary hardware review, resource/ABI proofs and authorized acceptance.
Queue TX dequeue from #11 remains copied memory ownership, never an event of
physical TX completion. #12's remaining physical acceptance is tracked by #15.

## Time, retries, completion and cancellation

All times are `uint32_t` counters in abstract **16-microsecond symbol units**,
modulo `2^32`. Lifetimes are `1..0x7fffffff` symbols; work limits are
`1..65535` active calls. Signed casts and implicit timer conversions are not
used. Every observation/comparison must have true temporal separation strictly
less than `2^31` symbols (about 9.54 hours); equality is ambiguous. The clock
must be continuous, without reset, missed full wrap or PM loss. The API
detects backward/exact-half-range progress, not violated epoch assumptions.

`now` is the current observation time. Events must be processed in physical
timestamp order **before advancing the foreground observation watermark**:
`last <= stamp <= now` under those half-range preconditions. Drain queued
captured events before polling at a later time; an event behind the watermark
is stale and ignored even if its DSN matches. The adapter must order TX-end
before ACK-end. No unbounded delayed-event archive is provided.
In particular, passing a late wall-clock `now` with SENT advances the watermark:
an ACK already captured before that `now` cannot subsequently be delivered as
fresh. Arbitrary batched/delayed callback delivery is unsupported; the adapter
must meet this ordering/latency contract or report failure, not relabel old
timestamps. Timely capture alone does not establish timely event processing.

Sleep Timer raw32768-Hz/RC ticks are **not** symbols. There is no conversion,
calibration, busy-wait, timer register or delay loop here. A real adapter
must provide a verified scheduling/capture time domain and resolve sub-symbol
phase, rounding, delivery latency and clock error without silently moving the
deadline or inventing exact physical times. If it cannot satisfy this integer
abstraction, the time API requires a separately reviewed revision. Simulator
CPU clocks do not validate it. Missed scheduled starts must not become SENT.

The 54-symbol ACK deadline is measured from captured TX end, not dequeue,
strobe, function return or foreground receipt of SENT. An eligible ACK's
completion stamp is greater than TX end and <= TX end+54. At equality an
already-delivered ACK is handled before that call's ACK timeout. A prior poll
at the boundary closes the attempt; later input cannot reopen it. Transaction
lifetime equality, in contrast, expires before processing a completion.
Malformed/wrong-type/length, stale identity and out-of-window ACKs do not
match. A well-formed wrong-DSN ACK immediately fails that attempt per the
selected 2006 text.

Each retry resets NB=0/BE=3 and uses the same copied body/DSN. A no-ACK attempt
always waits through the original ACK window plus IFS before another attempt,
including wrong-DSN early failure. IFS is measured after ACK for acknowledged
success and TX end for unacknowledged success. It persists across release
and next admission. Cancellation while waiting for ACK also preserves the
whole ACK window/IFS. Cancellation after issuing ATTEMPT but before confirmed
SENT is conservatively uncertain: after QUIESCED, preserve an additional ACK
window when requested and then IFS, measured from that confirmation.

There are at most four PHY attempts and five CCAs per attempt, further bounded
by one absolute transaction lifetime and one work budget (neither refreshed by
retry). Scheduling a backoff at/after expiry ends without a radio action.
Stopping has its own **project-policy** 1024-symbol/16-step budget, not an IEEE
timer. Lost cleanup confirmation, clock discontinuity or adapter failure
latches FAULT; CPU progress and valid foreground calls are required. The
module cannot stop a hung CPU or physically stop RF by itself.
If a retry is pending during cleanup, lifetime/work exhaustion or cancellation
prevents that retry; cleanup still must finish. A logical completion that
occurred before expiry may finish bounded cleanup afterward.

| Diagnostic | Meaning |
| --- | --- |
| `ACKED`, DONE | Matching eligible ACK **and** adapter quiesced; not peer identity/authentication or application delivery |
| `UNACKNOWLEDGED`, DONE | Confirmed local PHY completion without AR, then quiescence; **no peer delivery evidence**, no frame retry |
| `CHANNEL_ACCESS` | Busy limit exceeded; previous retries may already have transmitted |
| `NO_ACK` | Four attempts exhausted without a match; loss of ACK does not prove loss of data |
| `CANCELLED` / `LIFETIME` / `WORK_LIMIT` | Explicit local termination, not successful delivery; slot waits for cleanup when necessary |
| `transmissions` | Count of confirmed SENT events, 0..4 |
| `uncertain` | ATTEMPT may have transmitted without confirmed SENT; conservative even when later quiescence is confirmed |
| FAULT | Retained ownership; no release/retry/reinitialization as a recovery shortcut |

An outstanding ATTEMPT itself denotes possible effects even before
`uncertain` is latched on cancellation/fault. An ACK outcome shown while
STOPPING is provisional: later cleanup failure yields FAULT, not DONE.
Pending is raw ACK metadata and triggers no polling or parent procedure.

Eight-bit DSN matching cannot distinguish a forged/in-window old ACK with the
same DSN after reuse, or a previous attempt's same-DSN ACK with an indistinguishable
physical arrival. Epoch/timestamps/tags reject stale **software events**;
they add no over-the-air identity or security guarantee.

## Genuine linked evidence and budgets

The detailed ledger below records the earlier control-staging baseline.
With #63's explicit receive profile in the shared codec, this unchanged
transmitter/corpus measured **25515 CODE,1114+64 XDATA, SP5E**, still within
all original limits. Default decoding and TX admission are unchanged.
The [current shared refresh](VALIDATION.md#r22-response-profile-and-shared-proof-refresh-63)
distinguishes this from the historical hashes/addresses below.
The later complete-join decoder lowering gives25472 CODE, unchanged1114+64
XDATA, stack start39 and observed SP55 on both boards. The complete original
corpus and125 metadata negatives remain; only emitted pointer/length-copy
lifetimes change, not TX admission or wire rules.

The dedicated composition links **mac_frame, mac_tx, then test caller** with
unchanged strict native/SDCC flags. SDCC 4.2.0 model-large, both board definitions:

| Object | CODE | Ordinary XDATA (including scratch/parameters) | Persistent IRAM | Overlay IRAM |
| --- | ---: | ---: | ---: | ---: |
| Existing MAC codec, including unused command/Beacon code | 7,009 | 207 | 15 | 10 |
| MAC-TX scheduler | 5,650 | 191 | 8 | 0 |
| Synthetic caller/constants | 12,094 | 683 | 14 | 0 |
| Shared CRT/runtime | 635 | 24 | See linked accounting | Shared |
| **Total** | **25,388** | **1,105** | **37** | **10 shared, not summed** |

This Linux toolchain's unchanged codec size is measured here rather than
substituting an older platform's published object byte count.
Production context is **168 bytes**, including one 125-byte frame. Event is
17 bytes, action 22 bytes; generic pointer fields are three bytes.
These native C layouts are not serialized wire formats.

The test composition gets a separate **28-KiB CODE /1280-byte XDATA reservation
budget**, including all 64 reserved status bytes. This accommodates two context
copies for atomic-failure tests, two 125-byte test buffers, event/action copies
and compiler/runtime storage. It does not expand the MAC-codec, platform,
board or seven-module protocol-resource budgets, nor allocate a second
production queue. Actual reservation use is **1169/1280**.

Exact ordinary private prefix: codec `0000..00CE`, scheduler `00CF..018D`.
The scheduler's control mirror occupies `00CF..00F9`, scalar input staging
`00FA..0105`; neither is a second frame slot.
Caller context `018E..0235`, saved context `0236..02DD`, event `02DE..02EE`,
action `02EF..0304`, saved action `0305..031A`, body `031B..0397`,
copy `0398..0414`, ACK `0415..0418`; caller/compiler/runtime storage continues
through `0450`. The test-only request index/size/generic-pointer/expected-time
occupy `041F..0427` (1/1/3/4 bytes), with explicit caller-ABI checks and
pointer-width/storage negatives. Generic-store scratch is `0444`.
All ordinary allocations are
below `1E00`; status `1E00..1E3F` is reserved, only its first eight bytes used.
`1F00..1FFF` remains an IRAM alias, never extra RAM.

IRAM: 37 persistent +10 overlay +8 bank0 +1 bit-storage byte +1 unused packing
byte precede stack `39..FF` (199 bytes), initial/final SP38.
Observed compiled-test peak is **SP5A**, 34 stack bytes and **37 unused bytes**
below the unchanged upper-IRAM `80..FF` guard. This is limited foreground
vector evidence, not worst-case call-graph, interrupt-nesting or full-stack
fit evidence. No ISR is linked or simulated for this scheduler.

Beacon Request decoding exercises a deeper genuine command-codec call path.
The first extension exceeded the existing SP7C component cap; that cap and
the `80..FF` guard were **not raised**. DATA admission now reuses the decoder's
compressed-PAN equality guarantee, and dispatch snapshots its entry phase
instead of repeatedly loading it through a generic pointer. The portable
case list runs directly in target `main`, removing a test-only wrapper frame;
no case, codec call, real return or failure check is skipped. Host and target
still share the same case functions/list. This changes harness depth and
compiler scratch, not a claim of universal caller-stack headroom.

The `480087f` whitelist/reset follow-up saved89 production CODE bytes compared with
the reviewed final-octet admission and adds36 target assertion bytes: net53
bytes saved at that revision. It left330 CODE bytes below the
unchanged28KiB cap. Relative to the published Beacon Request baseline, that
production module was64 bytes smaller and caller/constants were1083 bytes larger.
That follow-up retained all preceding target cases, XDATA,
public context/parameter/field ABI and private allocation.
Data Request adds19 production CODE bytes and257 caller/constant bytes, with
all earlier cases retained and four additional genuine request layouts.
That pre-staging composition left **54 CODE bytes** below28KiB.
Development layouts exceeded CODE by18/21 bytes; the latter reached
SP85. Staging only test input/expected-time calculations in nine bytes of
ordinary test XDATA restored SP7B and CODE fit. The actual request/copy/retry/
ACK/cleanup calls and every assertion remain; no production or test-image
feature switch, larger cap or skipped case was used.
The later control-staging refactor saves **3230 CODE /25 persistent DATA /
5 overlay bytes**, with a net increase of43 ordinary XDATA bytes. It leaves
**3284 CODE bytes** within this image's unchanged28KiB cap. The coupled scan
image is now29440 CODE,1492+64 XDATA and SP62; all28 scenarios and217 artifact
negatives plus the alias negative pass. See [its ledger](MAC_SCAN.md).

Only the public context's control suffix is copied into private ordinary
XDATA during serialized foreground calls. The public generic-pointer API,
168-byte context and original125-byte frame are unchanged. Compile-time checks
pin every mirrored field's offset/size and both enclosing extents, including
native layouts. Unsigned-character object-representation copies stop after
`stop_steps`: native frame-alignment/tail padding and inactive members are
preserved. Invalid calls do not publish staged control. Successful calls
publish before returning; there are no callbacks or reentrant/ISR users.
The12-byte scalar staging record and cached copy length reduce compiler
spills without changing admission, ACK, DSN, IFS or cleanup semantics.

Contiguous unbanked CODE `0000..632B`, SHA256:
`f658150863b450952fdb2e70a466f4c92691a117b40d03f2fe356a581bb51ef6`.
Both generated IHX files also match byte-for-byte, file SHA256:
`1de50373b956465c7895d3024f46689a7f07f4ca5ce05023edaafc27c4909c95`.
Sorted complete production-private F/S/L/T record SHA256:
`7863fd66009ea481c458789f3b3a540319fff645c75c083456097186dcff0125`.
The reviewed357 private records include file-scope staging, helper
declarations/entries/ends, locals and complete type records. This is a new
inventory, not a claim that the earlier357 records retained their identities.
All253 caller records and181 unique public records are also pinned. Private
duplicate multiplicity is exact; legitimate identical public declarations
remain accepted. The raw-byte CDB loader rejects non-LF controls/separators
before parsing, including prefixed conflicting duplicates. The123 added
metadata negatives cover these boundaries through the actual loader.
The proof pins the whole image (including runtime/constants), private CDB
declarations/addresses, field/pointer ABI, caller storage, generic-store
scratch and **complete ordered** per-image relocated instruction records.
For each module it checks record count, a pinned normalized digest, exact
nonoverlapping byte coverage and each instruction against the linked image.
Normalization is one lowercase `six-hex-address:byte-hex` line per record,
including a final newline; it excludes comments and source/assembler line
numbers, not records or their order.

| Module | Instruction records | Covered instruction bytes | Exact coverage (end exclusive) |
| --- | ---: | ---: | --- |
| `mac_frame` | 4,168 | 7,009 | `0062..1BC3` |
| `mac_tx` | 3,729 | 5,650 | `1BC3..31D5` |
| `mac_tx_test` | 6,998 | 11,934 | `0000..0006`, `005F..0062`, `31D5..606A` |

The test record count includes its nine startup instruction bytes; its
160 constant bytes and the 635-byte linked CRT/runtime remainder are covered
by the whole-image hash, not attributed to these instruction records.
The normalized SHA256 values, in table order, are:

- `6f42dd5789a73ec3024332b64ec3a61a5a29a4e5d541dbb8564aa35c0e6eb423`
- `b9923d41551ab54b15d2079d0312b73514e0466041cb0d13a344abe156f85e0c`
- `1c3686695fbb7421ecf5cb5d19e8e22d120b731a8ed1c91a19cdaa5ef96bec30`

Public MAC-TX entry addresses are pinned and cross-checked against map,
CDB and actual listing labels/instruction boundaries: init `1C87`, submit
`1D55`, copy `21AF`, step `23FC`, release `3180`. The proof also checks the
five reused codec API entries. The target result ABI is an unsigned-byte
return in DPL (`SC:U` in CDB), now including init.
`_main=5F45` is checked against map/CDB/listing and the startup LJMP at `0003`;
`_mac_tx_done=6066` is checked against map/listing and exact `00 80 FE 22`
checkpoint/loop/return bytes. A different location containing a zero byte
cannot substitute for the checkpoint.

The proof executes real compiled instructions without patched returns.
Negative controls reject changed CODE/private/field data, the old void
initializer ABI, altered instruction bytes, **dropped, duplicated and
reordered records in each module**, changed public/checkpoint map entries,
changed public CDB entries, a changed checkpoint label, and missing alias.
Transcripts are indexed once; every s51 process retains the existing
**15-second timeout**. Upper-IRAM, unused XDATA/status, disabled interrupts
and final stack unwind are checked.

Native tests cover the portable state corpus, all 8,192 legacy ACK type-2 FCF
patterns (including RX-ignored bits), 256 DSNs/draws, 32 DATA header combinations,
exact heap-backed input/output/ACK sizes, truncation/trailing data, full slots,
unchanged failures, stale/wrong/delayed/future ACKs, backoff saturation/busy
exhaustion, four-attempt no-ACK exhaustion, wrap/half-range/backward time,
work/cleanup bounds, cancellation and faults after possible TX. The portable
golden/state corpus runs under SDCC, including CODE-pointer admission.
Both native and SDCC tests check the NULL initializer result without public
state mutation, exact initialization from nonzero storage, and every normal
initializer call's successful result.
The Beacon Request extension adds 2,048 byte/value mutations, exact-sized
lengths 0..126, command-Pending rejection, valid unsupported destination-less Data Request
rejection, busy-CCA exhaustion, genuine CODE input/copy, unacknowledged
completion, and shared request-to-DATA DSN wrap/IFS. Existing DATA/ACK cases
remain enabled.
Association Request adds16,384 command-FCF patterns,11,264 byte/value variants
(including every capability byte for both layouts),262,144 command-ID/final-byte
pairs over four valid MHR/payload shapes, and512 one-octet command-ID variants.
These compare with an explicit ID-based oracle; all failed admissions preserve
the whole context/DSN and input bytes. Heap-backed exact sizes0..126 test both
request layouts, truncation/trailing data and copy capacity/tails. Additional
native cases check broadcast/sentinel rejection, unsupported valid commands,
CCA/no-ACK exhaustion, Pending ACK receipt without polling, and cancellation.
The genuine linked addition admits short-coordinator/CAP88 from CODE, checks
exact copied bytes and three identical retries before a real parsed matching
ACK, confirmed cleanup,40-symbol IFS, DSN wrap into extended-coordinator/CAP8C
from XDATA, and retained uncertain adapter failure. It does not substitute
an unconditional-success codec/radio or remove any prior target case.
Data Request adds32,768 command-FCF patterns,16,384 byte/value variants and
1,024 command-ID cases over all four addressed layouts, plus exact input/copy
sizes0..126, broadcast/sentinel rejection and the same five native
CCA/no-ACK/Pending/cancellation outcomes as Association Request.
Whole-context reset/admission-preservation comparisons now total8,192, keeping
all prior4,096 cases. The linked shared request corpus retains both original
Association forms and adds all four Data Request CODE forms: exact copy,
three identical retries, parsed ACK, confirmed cleanup, both IFS lengths,
shared DSN wrap into the original XDATA Association Request and retained fault.
All source events are **synthetic**, not captures or hardware observations.

## Commands and integration

Canonical focused checks, run serially from the repository root:

```sh
make -j1 BOARD=generic BUILD=build/mac-tx-control/generic \
  test-mac-tx test-mac-scan
make -j1 BOARD=lg_esl29_rev03 BUILD=build/mac-tx-control/lg_esl29_rev03 \
  test-mac-tx test-mac-scan
```

Both commands passed from fresh output directories after control staging.
Outputs remain isolated in `build/mac-tx-control/<board>`. The MAC-TX target uses the
unchanged strict native/SDCC flags and link bounds, links
`mac_frame` -> `mac_tx` -> `mac_tx_test`, snapshots all three relocated
listings immediately after linking, then runs the native corpus and
`tests/boot_mac_tx.py`. The scan link then uses `mac_frame -> mac_tx ->
nwk_beacon -> nwk_candidates -> mac_scan -> mac_scan_test` and immediately
snapshots its own six listings before its proof. Always use these per-image
snapshots: the shared unsuffixed listings are overwritten by the second link.
The targets are already canonically integrated; no Make/header dependency,
board IMAGE, upload or new builder integration is required from this slice.
MAC-TX is included once in `test-common`; no board IMAGE
or artifact upload is added. The redundant prototype builder has been removed.

Separate unchanged codec regression recipe (run for the initial slice, not
repeated for this admission-only delta), for each board:

```sh
make BOARD=generic BUILD=build/mac-tx-dev/generic/codec \
  build/mac-tx-dev/generic/codec/host-mac-frame-tests \
  build/mac-tx-dev/generic/codec/mac_frame_test.ihx
build/mac-tx-dev/generic/codec/host-mac-frame-tests
python3 -B tests/boot_mac_frame.py --output build/mac-tx-dev/generic/codec
# Repeat with BOARD=lg_esl29_rev03 and that board's directory.
```

Additional native ASan/UBSan checks, after the canonical builds:

```sh
export ASAN_OPTIONS=abort_on_error=1:detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1
for board in generic lg_esl29_rev03; do
  out=build/mac-tx-control-sanitizers/$board
  make -j1 BOARD="$board" BUILD="$out" \
    HOST_CC='cc -fsanitize=address,undefined -fno-omit-frame-pointer' \
    "$out/host-mac-tx-tests" "$out/host-mac-scan-tests" || exit
  "$out/host-mac-tx-tests" && "$out/host-mac-scan-tests" || exit
done
```

No new dependency is required. Repository and whitespace checks are separate.
An unchanged full local firmware matrix is not claimed/repeated for this
isolated addition.

**2026-09-18 results:** the initial DATA-only composition in `30f0603` passed
both dedicated board checks, native ASan/UBSan and the unchanged coupled
codec host/linked corpus for each board.
`python3 -B tools/check_repository.py`, `git diff --check` and explicit
`git diff --no-index --check /dev/null <new-file>` checks passed.
The Beacon Request extension passed new native and genuine linked checks
with both definitions, all strengthened proof negatives and both sanitizer
builds. Its two images are byte-identical with the new hashes and resource
measurements recorded at that revision. Canonical Make compilation/link/snapshots are retained.
The initial shared integration additionally passed the
48 focused Make/orchestration and artifact-layout tests. These enforce the
per-board component inventory, correct link/snapshot/proof order and exclusion
of scheduler symbols/sources from every board image. Repository/whitespace
checks passed; the CI board-artifact whitelist is unchanged.
These are host-tested, image-checked and alias-aware simulated results only;
there is no hardware-observed result for this slice.

**Association Request and whitelist follow-ups:** both canonical compositions, both board
definitions and all four native ASan/UBSan runs pass. MAC-TX remains SP7B,
scan SP7A, both below the unmodified7C cap. All prior proof guards remain;
scan retains20 public entries, all prior539 private records plus the one
cached-command declaration, and76 artifact+1 alias
negatives. No scanner production/header/corpus, codec, Make/shared verifier
or board firmware was edited. Resource/ABI changes above are measured, not
an estimate of full-stack fit. Private hash changes cover relocated addresses
and exactly that added declaration; caller changes are relocated addresses
only. Both complete private matchers and the parent's helper negatives remain.

**Data Request follow-up:** canonical `test-mac-tx test-mac-scan` runs pass
for `BOARD=generic BUILD=build/mac-data-request-dev` and
`BOARD=lg_esl29_rev03 BUILD=build/mac-data-request-lg-check`.
Both new native request corpora and both coupled scan corpora pass ASan/UBSan.
The earlier357/540 production-private records retain the exact same names,
types and scopes; only CODE addresses relocate. No scanner source/header/
scenario, codec, shared verifier, Make target or board image changed.
All59 focused metadata/Make/artifact regressions pass, including the eight
scan-parser tests. Hardware, response retrieval and association remain open.

**Control-staging follow-up:** both canonical shared-build compositions and
all four native sanitizer executables pass with the current resources above.
The unchanged native TX suite retains8192 complete-context preservation cases;
a separate native old/new comparison additionally passed300,000 API pairs,
including full object representations. The64 focused Make/artifact/parser
regressions and repository/whitespace checks pass. Existing public headers,
native corpora and all limits remain unchanged. This is host-tested,
image-checked and alias-aware simulated evidence only; a composed POLL image
or hardware MAC is not established by these transmitter/scan results.

Before real use, implement/review the unified adapter and exact time domain;
separately authorize a boot-disarmed fixture, board/channel/power/attempt
limits, recovery and independent captures. Observe CCA/backoff, turnaround,
ACK/retry timing and failure cleanup physically. None was done here, and
#15's physical acceptance and #13/M3's adapter/timing gates remain open.
The independent #20 NV journal adds no persistence/security/commissioning
behavior to this scheduler and does not waive BDB errata/test-plan gates.

No devices were enumerated, opened, reset, flashed or used for RF; no private
data, SDKs or third-party implementation sources were imported.
