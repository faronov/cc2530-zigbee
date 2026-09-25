# Offline Association Response context (#43)

Original BSD-3-Clause, based on published protocol baseline `65dda70`.
This is a bounded **response metadata context/filter**, not an association
procedure, MLME-ASSOCIATE implementation, radio adapter or Zigbee membership.
It uses the actual frame and command decoders, not another wire parser or a
successful hardware substitute. The original #43 slice left the shared
components unchanged; #63 adds the explicit receive profile below and
revalidates dependent compositions without changing their limits.

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
match. The legacy entry requires the exact selected nonbroadcast destination
PAN; the explicit R22 alternative below still requires the actual selected
source PAN. Neither substitutes the general receive filter for this context. Short
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

### Explicit R22 Response receive profile (#63)

The same pinned **R22 05-3474-22**, Annex D.3/Table D-3, printed p.514,
was rechecked as an actual rendered table, not a flattened text row.
`mac_frame_decode_profile(body,length,result,profile)` and
`mac_association_step_rx(ctx,now,event,observation,profile)` add a per-call
`MAC_RX_R22_ASSOCIATION_RESPONSE` selection. `MAC_RX_IEEE2006` and both
original entry points retain legacy behavior; the context ABI/version stays1.

| Received Response | Raw R22 decoding | Selected-PAN context |
| --- | --- | --- |
| Compressed, destination PAN selected | Accepted | Accepted with all other checks |
| Uncompressed, both PANs selected | Accepted | Accepted with all other checks |
| Uncompressed, destination FFFF/source selected | Accepted | Accepted with all other checks |
| Compressed, destination FFFF | Accepted; implied source FFFF | MISMATCH; no selected PAN is invented |
| Explicit source PAN different from selected | Raw fields retained | MISMATCH even with selected destination |

The uncompressed body has27 bytes: `23 CC DSN`, destination PAN, local IEEE,
explicit source PAN, source IEEE, then the same four-byte command payload.
`33 CC` preserves ignored RX Pending. Extended/extended addressing, AR1,
version0, no security/IE, exact length and status/address consistency remain
mandatory. No other command or encoder rule is broadened. This is bounded
Response-header recognition, not a claim to reconcile every IEEE2015 frame
rule or every combination of Annex D alternatives.

CRC, actual channel, known/unbound IEEE, correlation, time/work, terminal
atomicity and independent receiver ACK obligations are unchanged. Unknown
profile bytes return INVALID/INVALID_ARGUMENT without mutating outputs or
context. The shorter `step_rx` name keeps SDCC's parameter symbols distinct
in its bounded-width map output. Volatile **parameter copies**, not caller
objects, prevent extra persistent IRAM spills; no heap or hidden receive mode
is added. The default path never inherits a previous call's profile.

Default POLL admission stays legacy. The explicit
[#64 POLL receive path](MAC_POLL.md#explicit-r22-response-reception-64) now
forwards these supported uncompressed headers to `mac_association_step_rx`.
Request/Data Request alternatives, whole-procedure timing (#45), radio/ACK
and BDB gates remain open.

**Current #63 evidence:** all34 original shared scenarios remain and run in
legacy, R22-compressed and R22-uncompressed forms. Two additional scenarios
per form distinguish wrong explicit source PAN and legacy rejection:
**108 genuine target cases**, including CODE/RAM inputs and explicit invalid
profile atomicity. Native tests additionally cover24,576 command FCFs,
6,400 byte variants, three complete65,536-address and256-status sweeps,
exact allocations0..126, all254 unknown profiles, raw/context distinction
and unchanged encoder/Pending rejection. ASan/UBSan exercise exact spans.

The following ledger records the #63 compiler baseline. Subsequent
complete-join decoder lowering gives15043 CODE, unchanged696+64 XDATA,
stack start3C/unwind3B/peak5E on both boards. `mac_frame` becomes7093
CODE/216 XDATA/12 DATA; all108 cases and110+1 negatives are retained.
The current complete identities are pinned in `tests/boot_mac_association.py`.

| Earlier #63 genuine composition | Value |
| --- | --- |
| CODE / ordinary XDATA / reserved status |15086 /696 /64 bytes |
| Production object CODE/XSEG/DSEG | mac_frame7136/216/15; mac_association2839/76/27 |
| Context/request/event/record |73/30/20/27 bytes, unchanged |
| Stack start / unwind / observed peak / cap |46/45/71/7C |
| Public entries / complete private/caller/field records |12 /314/38/20 |
| Ordered instructions / ordered table and CODE data |8694 /122 bytes,35 switch targets |
| Artifact negatives / missing-alias negative |110 /1 |

Whole CODE SHA256:
`52f5c012c144f21368e7d5201de45aa9a3aaf7fc5eb6f10bc271f1641e28a4ca`.
The original16-KiB CODE and1024-byte XDATA-reservation budgets are unchanged.
Full relocated-byte, parameter/helper/public ABI, real wrapper/decoder call,
alias/unallocated/status/upper-IRAM and bounded simulator checks remain.
This is host-tested, image-checked and simulated evidence, **not hardware**.
See the [shared composition refresh](VALIDATION.md#r22-response-profile-and-shared-proof-refresh-63).

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

### Staged timing and confirmation gate (#45)

Further direct examination of the same primary revisions establishes the
following stages, but **not a complete normative association deadline**:

| Stage | Reference event and meaning |
| --- | --- |
| Association Request | Actual CSMA/transmission/ACK transaction; channel-access and exhausted missing-ACK failures have Association confirmations |
| Decision wait | Successful Request ACK receipt starts `R = macResponseWaitTime * 960` symbols |
| Nonbeacon extraction | Send extended-source Data Request after R; its own CSMA/transmission/retry/ACK time is additional |
| Extraction ACK Pending0 | No queued data; no Pending1 reception window |
| Extraction ACK Pending1 | ACK receipt starts at most `F = macMaxFrameTotalWaitTime` symbols of reception |
| Extraction reception | Corresponding Response, empty DATA or F expiry determines that extraction's result |

IEEE7.1.3.1.3-7.1.3.2.3,pp.79-81 and7.5.6.3,pp.187-188 establish these
primitive/extraction relationships. Pending1 can also mean that the coordinator
could not determine queue state before ACK transmission; it is not a delivery
promise. Figures31/80/81,pp.86/219-220 show the stage order, but7.7,p.215
expressly makes the charts chronological illustrations, not exact timing.
The p.180 terminal "within" wording remains unresolved by these passages.
Neither R,2R nor R+F from Request ACK is established as the complete deadline;
R+F also omits the intervening Data Request transaction.

Equation13,p.160 gives a54-symbol sender ACK bound for the selected PHY.
This is distinct from the receiver's12-symbol ACK-start requirement.
Equation14 on that page is also kept source-faithful: the printed parentheses
enclose only its summation. Its literal grouping is
`sum(k=0..m-1,2^(minBE+k)) + (2^maxBE-1)*(maxCSMABackoffs-m)*20 + phyMaxFrameDuration`,
where `m=min(maxBE-minBE,maxCSMABackoffs)`. IEEE defaults3/5/4 and266-symbol
maximum PHY duration give1530, not1986 obtained by moving the20 multiplier
around the entire sum. This transcription is **not an implemented timer** or
an inferred correction/erratum; a future timing contract must preserve the
exact selected source/revision and resolve interpretation explicitly.

Generic `MLME-POLL.confirm(NO_DATA)` must not be wired straight to association
failure: IEEE7.1.16.1.3,p.133 also returns that POLL result for a retrieved
**MAC command**, while a valid Association Response supplies the separate
Association confirmation. The examined text does not establish a blanket
forwarding rule for extraction-stage POLL failures. Coordinator
`MLME-COMM-STATUS.indication` is separate again. An unacknowledged indirect
Response stays queued for another Data Request and retains its DSN; it does
not autonomously use the direct-retry loop (7.5.6.4.3,p.190). Coordinator-side
transaction persistence begins at queuing, not the child's Request ACK.

R22 3.6.1.4.1/Table3-62 does not mandate that every ED be receiver-on;
our selected awake capability must match actual PIB behavior, whose IEEE
`macRxOnWhenIdle` default is FALSE. R22's receiver-on exemption from polling
in3.6.1.4.2,p.341 applies to **NWK rejoin**, not MAC association.
No association-specific replacement for R/F was found in the examined R22
clauses; AnnexD.6,p.515 recommends minBE5/maxBE8 rather than mandating them
as ED association overrides. `nwkParentInformation=0` before joining is a
separate NIB requirement (3.2.2.13.3,p.260), not a timing PIB.

R22 AnnexD.1 refers to IEEE2015, and AnnexD.3/TablesD-1-D-3,pp.513-514 require
additional association header recognition, including Response destination
PANFFFF. The explicit #63 profile above covers bounded Response alternatives,
not the complete R22 receive contract and not a silent change to the selected
IEEE2006 default. Authoritative deadline
interpretation and revision reconciliation remain open in #45. Context work/
lifetime limits stay explicit project policy, not fabricated IEEE NO_DATA.

The official [IEEE2015 correction sheet](https://standards.ieee.org/wp-content/uploads/import/documents/erratas/802.15.4-2015_errata.pdf)
issued29 July2016 was inspected separately. Its two pages correct only the
standard's title; they do not resolve association timing or the frame-wait
equation. This does not establish that no other correction exists, or replace
review of the complete2015 edition referenced by R22.

The separate [staged controller](MAC_JOIN.md) now implements the supported
Request/decision-wait/extraction/Response sequence. Its overall resource/time
bound remains an explicit local abort, not an invented IEEE NO_DATA deadline.
The extraction ACK supplies context start B; configured F=1..65534 gives
lifetime F+1 so `(B,B+F]` remains inside this context's half-open interval.
It never retimestamps an old Response to force acceptance. A received Response
is classified separately from unresolved failure-confirmation mapping;
receiver ACK, radio restoration, PIB installation and membership are not
invented.

The separate [conditional POLL controller](MAC_POLL.md) now implements one
legacy extraction using caller-valid configured F. Its genuine caller forwards
copied Response bytes/epoch/stamp here even when POLL reports NO_DATA.
At exact F expiry, a timely extraction can coexist with this context's
half-open EXPIRED result. Neither replaces the other deadline or resolves #45;
receiver ACK, continuous RX ownership and full Association confirmation remain
independent gates.

## API and finite memory contract

| Entry | Effect |
| --- | --- |
| `init(ctx,now)` | Explicit result; NULL invalid; otherwise fresh memory context and IDLE. Caller already purged old reports before reinitialization |
| `start(ctx,request,now)` | Validate/copy context; IDLE only; advance nonwrapping generation; open logical wait. Caller supplies truthful selected PAN/channel/local identity and actual confirmed reception/ACK-service preconditions |
| `step(ctx,now,event,observation)` | NULL event polls; frame or correlated cancellation otherwise. API OK means processed, not delivery/association |
| `step_rx(ctx,now,event,observation,profile)` | Same finite contract with explicit per-call receive profile; unknown profiles are atomic API errors |
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

This section retains the **original #43** figures, hashes and entry addresses
as historical evidence. The [current #63 profile](#explicit-r22-response-receive-profile-63)
above supersedes these measurements without weakening their budgets or guards.

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
