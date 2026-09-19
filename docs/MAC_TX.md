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
| `mac_tx_submit(tx, body, length, now, lifetime, work_limit)` | Validate with the real codec, admit direct unsecured DATA v0/v1 or a canonical v0 Beacon/Association Request command, copy bytes, replace DSN, reserve the sole slot. DATA requires both addresses; short/extended and unacknowledged short broadcast are supported |
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

Unsupported: Beacon frames, commands other than Beacon/Association Request
(including Association Response, Disassociation and Data Request/polling),
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

### Explicit command whitelist and bounded storage reset

Admission caches the decoded payload's first octet in a `uint8_t` command
identifier and applies a positive whitelist: **Beacon Request**, or
**Association Request with nonbroadcast target PAN and CAP88/8C**. All other
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
physical TX completion. #12 remains open for its physical fixture/capture gate.

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

The dedicated composition links **mac_frame, mac_tx, then test caller** with
unchanged strict native/SDCC flags. SDCC 4.2.0 model-large, both board definitions:

| Object | CODE | Ordinary XDATA (including scratch/parameters) | Persistent IRAM | Overlay IRAM |
| --- | ---: | ---: | ---: | ---: |
| Existing MAC codec, including unused command/Beacon code | 7,009 | 207 | 15 | 10 |
| MAC-TX scheduler | 8,861 | 148 | 33 | 5 |
| Synthetic caller/constants | 11,837 | 674 | 14 | 0 |
| Shared CRT/runtime | 635 | 24 | See linked accounting | Shared |
| **Total** | **28,342** | **1,053** | **62** | **10 shared, not summed** |

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
production queue. Actual reservation use is **1117/1280**.

Exact ordinary private prefix: codec `0000..00CE`, scheduler `00CF..0162`.
Caller context `0163..020A`, saved context `020B..02B2`, event `02B3..02C3`,
action `02C4..02D9`, saved action `02DA..02EF`, body `02F0..036C`,
copy `036D..03E9`, ACK `03EA..03ED`; caller/compiler/runtime storage continues
through `041C`. Generic-store scratch is `0410`. All ordinary allocations are
below `1E00`; status `1E00..1E3F` is reserved, only its first eight bytes used.
`1F00..1FFF` remains an IRAM alias, never extra RAM.

IRAM: 62 persistent +10 overlay +8 bank0 +1 bit-storage byte +9 unused packing
bytes precede stack `5A..FF` (166 bytes), initial/final SP59.
Observed compiled-test peak is **SP7B**, 34 stack bytes and **four bytes**
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

The whitelist/reset follow-up saves89 production CODE bytes compared with
the reviewed final-octet admission and adds36 target assertion bytes: net53
bytes saved in this image. The composition has330 CODE bytes below its
unchanged28KiB cap. Relative to the published Beacon Request baseline, the
production module is64 bytes smaller and caller/constants are1083 bytes larger.
All preceding target cases remain. XDATA, public context/parameter/field ABI
and private allocation are unchanged.
The coupled scan composition is now32,651 CODE (117 bytes below8000), still
1449+64 XDATA and SP7A; its28 genuine scenarios and76+1 negatives pass with
the full private matcher and reviewed relocated records. See [its ledger](MAC_SCAN.md).

Contiguous unbanked CODE `0000..6EB5`, SHA256:
`5aef7879004504b02bc8ce2e0d4b649b4aa37e925d039efcd3b04d162993ab37`.
Both generated IHX files also match byte-for-byte, file SHA256:
`081156908af65e8435956f54fbbeaa7d4669b0569fcf85c5980d7e10b53eef22`.
Sorted production-private CDB declaration/address record SHA256:
`1fc0a2e73350c02c66fa3dab6596c8e4352423d275ef6d5c08282e12e5d937af`.
The parent's complete private matcher is unchanged: its356 prior records are
retained with reviewed addresses, plus the cached-command local declaration,
for357 total. It covers file-scope helper declarations and entry/end addresses
as well as local parameters. All three added helper negatives remain. The scan
matcher likewise retains its539 records plus that declaration, for540 total,
and all49 field records. No extra private byte is allocated for the new local.
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
| `mac_tx` | 5,796 | 8,861 | `1BC3..3E60` |
| `mac_tx_test` | 6,898 | 11,765 | `0000..0006`, `005F..0062`, `3E60..6C4C` |

The test record count includes its nine startup instruction bytes; its
72 constant bytes and the 635-byte linked CRT/runtime remainder are covered
by the whole-image hash, not attributed to these instruction records.
The normalized SHA256 values, in table order, are:

- `bcdb6cdd4b8a011b54ccb3ee26726d4ff013d69fba113b70b51037c50038841a`
- `19bf4007eac51ec46f6afbd9082f227e1ee945b52dfff2af1bfc6c7b781286d8`
- `e657482b19ee550ce689c973d99f7134136780d3ae24d0f2dc5aae66b146e4f2`

Public MAC-TX entry addresses are pinned and cross-checked against map,
CDB and actual listing labels/instruction boundaries: init `1C05`, submit
`1CD3`, copy `2244`, step `2748`, release `3E0B`. The proof also checks the
five reused codec API entries. The target result ABI is an unsigned-byte
return in DPL (`SC:U` in CDB), now including init.
`_main=6B27` is checked against map/CDB/listing and the startup LJMP at `0003`;
`_mac_tx_done=6C48` is checked against map/listing and exact `00 80 FE 22`
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
lengths 0..126, command-Pending rejection, valid unsupported Data Request
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
All source events are **synthetic**, not captures or hardware observations.

## Commands and integration

Canonical focused checks, run serially from the repository root:

```sh
make -B -j1 BOARD=generic BUILD=build/mac-association-request-dev/generic \
  test-mac-tx test-mac-scan
make -B -j1 BOARD=lg_esl29_rev03 BUILD=build/mac-association-request-dev/lg_esl29_rev03 \
  test-mac-tx test-mac-scan
```

These force recompilation of both native executables and linked compositions,
as used for final canonical validation. Outputs remain isolated in
`build/mac-association-request-dev/<board>`. The MAC-TX target uses the
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
# Repeat with number=1 and board=lg_esl29_rev03.
number=0
board=generic
out=build/mac-association-request-dev/$board
cc -std=c99 -O1 -g -Wall -Wextra -Werror -pedantic \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -DCC2530_HOST_TEST -Iinclude -DCC2530_BOARD="$number" -Itests \
  tests/test_mac_tx.c src/mac_tx.c src/mac_frame.c \
  -o "$out/host-mac-tx-sanitized" &&
  UBSAN_OPTIONS=halt_on_error=1 "$out/host-mac-tx-sanitized"
# Same flags for the coupled scan sanitizer:
cc -std=c99 -O1 -g -Wall -Wextra -Werror -pedantic \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -DCC2530_HOST_TEST -Iinclude -DCC2530_BOARD="$number" -Itests \
  tests/test_mac_scan.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c \
  src/nwk_candidates.c src/mac_scan.c -o "$out/host-mac-scan-sanitized" &&
  UBSAN_OPTIONS=halt_on_error=1 "$out/host-mac-scan-sanitized"
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

Before real use, implement/review the unified adapter and exact time domain;
separately authorize a boot-disarmed fixture, board/channel/power/attempt
limits, recovery and independent captures. Observe CCA/backoff, turnaround,
ACK/retry timing and failure cleanup physically. None was done here, and
#12's physical acceptance and #13/M3's adapter/timing gates remain open.
The independent #20 NV journal adds no persistence/security/commissioning
behavior to this scheduler and does not waive BDB errata/test-plan gates.

No devices were enumerated, opened, reset, flashed or used for RF; no private
data, SDKs or third-party implementation sources were imported.
