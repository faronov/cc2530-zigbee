# Offline Association Response context (#43)

Original BSD-3-Clause, based on published protocol baseline `65dda70`.
This is a bounded **response metadata context/filter**, not an association
procedure, MLME-ASSOCIATE implementation, radio adapter or Zigbee membership.
It uses the actual `mac_frame_decode` and `mac_command_decode`, not another
wire parser or a successful hardware substitute. The existing transmitter,
scanner, collector, codecs and proofs were not changed.

## Primary decisions

Read directly: **IEEE Std 802.15.4-2006**, approved7 June2006, published
8 September2006, from the previously reviewed
[UBC public mirror](https://people.ece.ubc.ca/~edc/7860/data/802.15.4-2006.pdf).
PDF SHA256:
`d245c8bb208f6cdb585fb753cefa67e367d30eebd55b9ed9957c8fd152d6a055`.
The existing public extracted text was used, not a vendor implementation.

Also read directly: **Zigbee Core R22,05-3474-22,April19,2017**, from the
[pinned public mirror](https://github.com/pvginkel/ZigBeeHomeAutomation/blob/fc30145012eacd3a5af170b8ae8e0d4c848c2525/Documents/docs-05-3474-22-0csg-zigbee-specification.pdf).
PDF SHA256:
`991dd02b7e5764ac349c47d5c4cf0fbe01529ff6594df03e8dad3d3d4df9e274`.
The already reviewed public PDF was read through the existing temporary PDF
reader. No new dependency, SDK, private capture/identity or reference catalog
was imported. Sources are not vendored. Pages below are printed pages.

| Decision | Primary location | Applied meaning |
| --- | --- | --- |
| Response command | IEEE7.3.2,Figure57,pp.151-152; Table82,p.149 | ID02; two-byte short address; one-byte status |
| Response MHR | IEEE7.3.2.1,pp.151-152 | Extended source AND destination, destination PAN is coordinator's current PAN, compressed source PAN, AR1; Pending0 on TX but ignored on RX |
| MAC address/status | IEEE7.3.2.2-3,Table83,p.152; Table87,p.181 | Status00 with address0000..FFFD is MAC allocation, FFFE is extended-only MAC association; status01/02 is refusal with FFFF; other statuses and inconsistent combinations are not admitted by the existing codec |
| Chosen context | IEEE7.5.3.1,pp.179-181 | Set current channel/page/PAN and known coordinator address from selection before association; Request ACK is not association |
| Unknown coordinator IEEE | IEEE7.5.3.1,p.181 | If selected Beacon supplied only short coordinator address, the Response MHR supplies its extended address to store on successful association |
| General receive/ACK filtering | IEEE7.5.6.2,pp.186-187 | FCS verification first; extended destination must be local; valid unicast command with AR requires ACK, independently of this higher contextual filter |
| ACK timing | IEEE7.5.6.4.2,p.189;6.4.1/Table22,p.45 | Echo the Response DSN; nonbeacon ACK transmission starts12 symbols after received frame end |
| R22 parent/child procedure | R22 3.6.1.4.1,pp.336-338; Table3-62,p.337 | Parent selection includes network/capacity/link-cost/potential-parent/update-ID requirements; selected ED requests address allocation. Neither a preliminary candidate nor this context completes that procedure |
| Security boundary | R22 3.6.1.4.1,p.340 | Parent can classify a secured-network child as unauthenticated; MAC result does not establish authenticated readiness |

### Identity is not invented

An extended-address selection requires exact Response source equality.
`SOURCE_MATCHED` means only equality to caller-supplied bytes; a Beacon and
an unsecured Response are not authenticated identity evidence.

A short-address selection cannot check an IEEE address that was never
supplied. Its first otherwise matching Response records the source IEEE with
`SOURCE_UNBOUND`. It does **not** verify that IEEE-to-short-address binding,
change the selected address, or assert that a particular coordinator emitted
the frame. The future MAC procedure may learn/store the source as specified
by IEEE7.5.3.1; this slice only copies observed metadata. Refusal responses
also expose their source as unbound, without installing it as a parent.
**Extended PAN ID is a network identifier, not coordinator IEEE**; this API
does not accept an Extended PAN ID field or substitute one.

PAN, actual receive channel, local extended destination and caller epoch must
match. This project deliberately requires the exact selected nonbroadcast PAN,
not merely the general receive filter's broadcast-PAN allowance. Short
selections reject FFFE/FFFF and require zero tails, like existing admission.
No IEEE-allocation registry or source authorization is implemented.

Response DSN belongs to the coordinator; there is no Request-DSN equality
rule or transaction nonce in this command. Generation/epoch reject stale
**local reports**, not a freshly received on-air replay or impersonation.
A forged, CRC-correct frame can meet an unsecured unknown-source context.

### Exact selected layout

FCS-free, unsecured version0 body,25 bytes, little-endian multioctet fields
(IEEE7.2,pp.137-138):

```
offset  0     2    3       5               13              21  22       24
        63 CC DSN PAN[2]  LOCAL_IEEE[8]   SOURCE_IEEE[8]   02  SHORT[2] STATUS
```

`73 CC` is also admitted because command Pending is ignored on reception.
The unchanged codec supplies all addressing, exact-length, compression,
AR, status and short-address consistency checks; its strict reserved/version/
security subset remains explicit. The second real command decode returns the
payload values; this module does not duplicate their byte decoding.

`RESPONSE` means a contextual record, **not association success**.
`ALLOCATED`, `EXTENDED_ONLY` and `REFUSED` describe raw **MAC** address usage.
In particular, FFFE is valid IEEE metadata but not a usable allocated address
for the selected R22 ED/address-allocation roadmap. Even another MAC-allocated
value is not admitted here as a valid/unique Zigbee network address. NWK/PIB
updates, parent installation, BDB and authentication remain absent.

### Wait, retrieval, ACK and restoration remain separate

IEEE7.5.3.1,p.180 specifies indirect Association Response delivery.
Receiver-on operation does not eliminate Data Request extraction.
Table86,p.165 defines `macResponseWaitTime` as2..64 units of
`aBaseSuperframeDuration`, default32. Table85,p.159 gives960 symbols per
base superframe:1920..61440 symbols, default30720, not raw Sleep Timer ticks.
In the selected nonbeacon case, extraction follows that decision wait after
the Request ACK (7.5.3.1,p.180;7.5.6.3,p.188).
Retrieval of an Association Response uses the local extended source in the
Data Request (7.3.4,pp.153-154), even though addressed Data Request admission
also supports short sources for other uses. No automatic retrieval is added.

Data Request ACK Pending0 indicates no queued data; Pending1 requires bounded
reception up to `macMaxFrameTotalWaitTime` in symbols for the nonbeacon case
(7.5.6.3,p.188). That bound is parameter-dependent:7.4.2,Equation14,p.160
uses minBE/maxBE/maxCSMABackoffs,20-symbol backoff units and
`phyMaxFrameDuration`. No guessed fixed retrieval timer is installed here.
The p.180 prose also says failure if extraction does not occur "within"
`macResponseWaitTime`, after describing extraction after that wait.
A future full procedure must explicitly reconcile its decision-wait,
retrieval and NO_DATA-confirm deadlines with the primitive/sequence-chart
requirements; this context does not pretend a caller lifetime resolves that
wording or implement a total association timeout.

Receipt of an Association Response requires an immediate MAC ACK, including
refusal statuses. This foreground filter is **not an ACK eligibility/timing
engine**. A frame rejected here for coordinator-context mismatch may still
require ACK under the lower MAC receive rules. Do not delay ACK until after
this function, derive it from `RESPONSE`, or treat metadata receipt as proof
that an ACK was transmitted. No ACK action or claimed ACK completion exists
in this API. A real independent adapter must fulfill it; #13/#40 remain open.

IEEE7.5.3.1,p.181 requires default PANFFFF after unsuccessful association.
Full procedure cancellation, saved filter/channel/PAN restoration, transmitter
lease/DSN preservation and confirmed physical quiescence are future caller/
adapter obligations. This object owns no radio state and cannot release it.
Existing reset-exclusive RX/TX/time services cannot be chained to implement
those obligations. Memory init is not MLME reset or physical recovery.

## API and finite memory contract

| Entry | Effect |
| --- | --- |
| `init(ctx,now)` | Explicit result; NULL invalid; otherwise fresh memory context and IDLE. Caller already purged old reports before reinitialization |
| `start(ctx,request,now)` | Validate/copy context; IDLE only; advance nonwrapping generation; open logical wait. Caller supplies truthful selected PAN/channel/local identity and actual confirmed reception/ACK-service preconditions |
| `step(ctx,now,event,observation)` | NULL event polls; frame or correlated cancellation otherwise. API OK means processed, not delivery/association |
| `take(ctx,record)` | Copy terminal record exactly once, then IDLE. Preserve generation and time watermark; no radio/transmitter cleanup |

All records are caller-owned ordinary RAM (two-byte XDATA pointers on SDCC);
read-only frame bodies are generic CODE/RAM spans. No body pointer is retained.
Outputs/configuration/context/event/body must be disjoint accessible storage.
Never pass MMIO, status, compiler-private storage, IRAM alias or writable CODE.
The address-space qualifier and version check are not arbitrary-pointer or
corruption validation. Foreground-only, nonreentrant with the actual codecs.

States: IDLE -> WAIT -> DONE -> IDLE on one-shot take. DONE outcomes are
RESPONSE, EXPIRED, EXHAUSTED or CANCELLED; first terminal outcome cannot be
replaced by duplicates or late frames. There is no successful radio stub.

Project bounds: lifetime1..65535 abstract16-us symbols and work1..4096 calls.
These are context bounds, **not IEEE procedure timer parameters**. `now` uses
uint32 modular arithmetic; every consecutive observation must be monotonic
within the half-range and have no hidden wraps. No raw32768-Hz conversion,
counter capture, calibration or timer service is provided.
The receive window is half-open `[opened,opened+lifetime)`. Expiry precedes
input; delivery at the deadline is too late even for an earlier captured
frame. Frame stamps must lie between the previous foreground watermark and
`now`, inclusively. Deliver completed reports before later-time polling/
cancellation; never retimestamp old events or repeat a completed MAC step.

Invalid arguments/object headers/time, occupied start and unavailable take
return explicit API errors with context/output unchanged. Valid ignored
frames are **processed observations**, not atomic API errors: CRC failure,
syntax failure, mismatched identity and stale reports consume time/work
without replacing response metadata. The final allowed step can record or
cancel; otherwise it terminates EXHAUSTED. Invalid caller calls cannot provide
a progress guarantee. Caller must service this foreground object.

Nonzero epoch plus local generation identifies reports. GenerationMAX is the
last usable attempt, then start fails without wrapping. Nothing on air carries
these tags; the caller must truthfully correlate current adapter reports.
Only RESPONSE makes status/address/source fields meaningful; other terminal
records carry outcome, epoch, generation and termination time.

## Evidence and resource ledger

Both board definitions: strict C99 native tests, ASan/UBSan exact spans, and
genuine linked SDCC4.2.0 #13081 instructions with alias-aware s51 execution.
No patched returns, scripted substitute codec, hardware model or RF.

The shared target corpus has34 scenarios: known/unbound sources, CODE/XDATA
inputs, RX Pending, local/PAN/channel/source mismatches, CRC failure, malformed/
unsupported commands/security, short/trailing bodies, both refusal statuses,
FFFE, invalid status/address, stale epoch/generation/stamp, future/stale
delivery, half-open deadline, cancellation, final-step exhaustion/receipt,
time wrap, one-shot take, duplicates and reuse. Native coverage additionally
checks8192 command FCFs,6400 byte variants, every65536 short-address value,
every256 status byte with refusal address, exact allocations0..126, invalid
request/event/object boundaries,4096-call exhaustion and last generation.

The initial target corpus exposed SDCC's mixed CODE/XDATA conditional-pointer
typing in a test comparison. Explicit generic casts on both arms fixed that
comparison without changing production behavior or dropping any case.

| Item | Measured |
| --- | --- |
| Whole CODE |14022, contiguous `0000..36C5` |
| Ordinary XDATA / with64 reserved |675 /739 |
| Context / request / event / record |73 /30 /20 /27 bytes |
| Codec private / context private |207 /65 bytes, combined `0000..010F` |
| Caller / runtime XDATA |383 at`0110..028E` /20 at`028F..02A2` |
| Generic-store scratch |`__gptrput_PARM_2=029A` |
| Persistent DSEG / overlay / bit backing / register bank |40 /10 /1 /8 bytes;9 packing bytes |
| Stack start / final SP / observed peak |`44 /43 /6B`:40 stack bytes;20 bytes below upperIRAM80 |

The separately justified budget is **16KiB CODE /1024-byte XDATA reservation
including64 status bytes**, accommodating both real production modules,
two73-byte contexts, record copies and126-byte malformed-input storage.
It leaves2362 CODE and285 reservation bytes in this test composition; no
previous component budget increases. SP cap7C remains independent of the
upperIRAM80..FF guard. No ISR nesting or full-stack fit is claimed.
Ordinary storage is below1E00, status1E00..1E3F is reserved (first8 used), and
XDATA1F00..1FFF remains an IRAM alias, never extra storage.

| Module | CODE including data/startup | XSEG | DSEG/OSEG | Instructions / covered bytes |
| --- | ---: | ---: | ---: | ---: |
| mac_frame |7009|207|15/10|4168 /7009|
| mac_association |2674|65|25/0|1715 /2674|
| mac_association_test |3812|383|0/0|2166 /3719|

Remaining527 CODE bytes are CRT/library code. The caller has a68-byte
compiler switch table and25-byte CODE vector: all93 `.db` records are checked
in order against the image, with all34 table targets verified as instruction
boundaries. All8049 instruction records have complete ordered digests, exact
nonoverlapping coverage and relocated-byte checks. The proof pins all10
public entry/end/return ABIs, main/reset target and exact NOP/loop/RET done.
Complete private/caller/field matchers cover294/33/20 records, including
file-scope objects, helpers, locals and helper entry/end records.

Both boards produce identical hashes:

```
CODE    18921a1d34eab51efc32993ea0513b32c6e2d454bb238406f9ddf88775e3e115
IHX     f3380c3d1e70140baf7d9bcc1bed796c22c049f1c887e632de81aa78a4def047
private 8a41a37b099aa0ff9c5620e981d7d28568ee0f4d95543d940aeb0c7481a985fc
caller  bf59b3e2ef5acb1578445c855c2acce591bee50d8d599854d16a4d9d01529f5a
fields  75cc77b4721e8ca829854b73fa5b772634cc7a6568f9be75f8a1cfe4df9838f4
```

Context init/start/step/take entries: `1C05/1C79/1F4D/25A5`.
Main/done: `34BB/34F3`; whole per-module listing digests and public endpoints
are pinned in `tests/boot_mac_association.py`.

**98 artifact negatives plus1 genuine missing-alias negative** pass on each
board. They cover CODE extent/content, private/helper/caller/field changes,
dropped/duplicate ABI records, every public map/return/entry/end conflict
(including malformed duplicates and main), all module instruction
drop/duplicate/reorder/mutations, table/vector records, checkpoint labels,
object extents, aliased allocation and stack extent. Identical public
address/return duplicates are positively tested; conflicting ones fail.
Unused XDATA/status tails, disabled interrupts, upperIRAM and stack unwind
are checked. Transcripts are indexed once. **15 seconds per s51 process**
is inherited unchanged from the shared simulator helper.

## Canonical checks and integration

From the repository root:

```sh
for board in generic lg_esl29_rev03; do
    if [ "$board" = generic ]; then number=0; else number=1; fi
    out="build/mac-association-response-dev/$board"
    make -j1 BOARD="$board" BUILD="$out" test-mac-association || exit
    cc -std=c99 -O1 -g -Wall -Wextra -Werror -pedantic \
        -fsanitize=address,undefined -fno-omit-frame-pointer \
        -DCC2530_HOST_TEST -Iinclude -DCC2530_BOARD="$number" -Itests \
        tests/test_mac_association.c src/mac_frame.c src/mac_association.c \
        -o "$out/host-mac-association-sanitized" || exit
    UBSAN_OPTIONS=halt_on_error=1 "$out/host-mac-association-sanitized" || exit
done
python3 -B tools/check_repository.py
git diff --check
```

Link order is **mac_frame -> mac_association -> mac_association_test**.
Immediately snapshot all three relocated listings after each link; never
consume shared unsuffixed listings after another image link. Proof inputs are
IHX/map/CDB/mem, the three `.rel` objects and the three per-image snapshots.

`test-common` includes the canonical target for both board definitions.
Make orchestration checks cover the exact link order and all three immediate
snapshots; board-image guards reject component source/public-symbol leakage.
`mac_association_result` is reserved at1E00: eight bytes `ASR1`, version1,
length8, little-endian failing C line at6..7 (zero on pass).
Keep `_mac_association_done`, the genuine corpus and CODE/XDATA-input coverage.
Do not link, flash, upload or select this standalone executable as a board IMAGE.
No radio fixture or hardware runner is required or supplied by this slice.
The unchanged expensive full matrix was not repeated.

## Remaining gates

Request transmission and addressed Data Request admission exist separately,
but this component does not call or reset `mac_tx`, acquire a second DSN,
schedule extraction, confirm ACK delivery, select parents, restore PHY/PIB
state or implement membership. A real future procedure must use the single
existing transmitter/DSN owner and its confirmed cleanup contract.

#13/#40 unified ownership/captured-edge/freshness/phase and physical acceptance
remain open. Raw live-counter reads are not captured frame ends. BDB3.0.1
errata/security/entropy gates and all join/rejoin/leave, ED Timeout, keepalive,
endpoint0 and durable-counter requirements remain unchanged.
This work is host-tested, image-checked and simulated only. No device, USB,
RF, private capture or physical-state observation was accessed by this task.
