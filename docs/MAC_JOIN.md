# Bounded staged association attempt

Implementation tracking: [#83](https://github.com/faronov/cc2530-zigbee/issues/83).

`mac_join` executes a real Association Request, a decision wait, one real
Data Request extraction, and contextual Association Response handling.
It uses `mac_frame`, the caller's existing `mac_tx` owner, `mac_poll` and
`mac_association`. It is **not complete MLME-ASSOCIATE, a physical adapter,
MAC membership or authenticated Zigbee joining**. It installs no address,
parent or PIB value and supplies no security, persistence or board defaults.

## Primary basis and remaining boundary

The primary is **IEEE Std 802.15.4-2006**, published 8 September 2006,
[public UBC mirror](https://people.ece.ubc.ca/~edc/7860/data/802.15.4-2006.pdf),
SHA256 `d245c8bb208f6cdb585fb753cefa67e367d30eebd55b9ed9957c8fd152d6a055`.
The existing public text was rechecked directly. No implementation or
specification body is imported. Page numbers below are printed pages.

| Fact | Primary location |
| --- | --- |
| Canonical Request, extended source, ACK request and capability | 7.3.1, pp.150-151 |
| Request ACK is receipt, not association; indirect Response and decision wait | 7.5.3.1, pp.179-181 |
| Configured response-wait value 2..64; 960-symbol base duration | Table 86, p.165; Table 85, p.159 |
| Extended-source Data Request for association extraction | 7.3.4, pp.153-154 |
| Extraction after Request ACK/decision wait; Pending0/1 and configured F | 7.5.6.3, pp.187-188; 7.4.2, p.160 |
| POLL confirmation versus command delivery | 7.1.16, pp.133-135, Figure 40 |
| Response address/status and raw MAC allocation meanings | 7.3.2, pp.151-152, Table 83 |
| Independent immediate receiver ACK and IFS | 7.5.6.2/4.2, pp.186-189; 7.5.1.3, pp.169-170 |

R22 **05-3474-22**, 19 April 2017, SHA256
`991dd02b7e5764ac349c47d5c4cf0fbe01529ff6594df03e8dad3d3d4df9e274`,
3.6.1.4.1/Table 3-62 and Annex D.3/Table D-3 supply the already-reviewed
ED capability and bounded Response receive profile. See
[MAC_ASSOCIATION](MAC_ASSOCIATION.md) for the pinned public copy, precise
known/unbound identity rules and supported header alternatives.

**#45 remains open.** This component does not resolve the IEEE2006 p.180
decision-wait/extraction wording or establish a total Association NO_DATA
deadline. It does not choose R, 2R or R+F as that deadline. Its
whole-attempt lifetime/work limits are explicit **local abort policy**.
Broader IEEE2015/R22 header reconciliation, physical capture/ACK/ownership
gates #40/#50, complete MLME confirmation mapping, membership and security
are not waived.

The official two-page [IEEE2015 correction sheet](https://standards.ieee.org/wp-content/uploads/import/documents/erratas/802.15.4-2015_errata.pdf),
issued 29 July 2016, SHA256
`f02e15438a488631dcd38a022727faf99c53661836fd8e1c30128baf127657b1`,
corrects only the title. It contains no association or frame-wait correction
and does not resolve #45. The full IEEE2015 text remains unverified for this
component; no new normative interpretation follows from that sheet.

## API and ownership

The public entries are `mac_join_init/start/step/take/release`.
All calls are serialized foreground operations, nonreentrant with their
real dependencies. Caller objects are disjoint ordinary RAM/XDATA, not
compiler scratch, MMIO, reserved status or the IRAM alias. Context fields
are read-only diagnostics. Do not move/clone a leased context or operate
its nested contexts independently.

`init` initializes fresh memory in a fresh, purged adapter epoch. It never
resets the transmitter or restores a radio. `start` requires the already
initialized, idle, **single device-wide `mac_tx_t`** and exclusive caller
ownership: IDLE alone does not authorize stealing another controller's lease.
The lease lasts until successful `mac_join_release`, including waits when
the transmitter itself is idle. DSN, generation and IFS are preserved.
At least two remaining TX generations and nonwrapping controller generations
are required at admission.

The request supplies a truthful unassociated local IEEE address, permitting
selected coordinator, PAN/channel, capability, explicit receive profile,
configured R/F values and confirmed saved logical radio state. Only page 0,
channels 11..26, receiver-on capability 0x88/0x8C, extended local source,
short/extended coordinator and nonbroadcast selected PAN are admitted.
Short coordinator tails must be zero; FFFE/FFFF are rejected.
No scan, coordinator eligibility, IEEE allocation or authorization is inferred.

`MAC_RX_IEEE2006` or `MAC_RX_R22_ASSOCIATION_RESPONSE` is selected explicitly
per attempt and passed to both real receive handlers. Request/Data Request
transmission remains the existing canonical legacy subset. Unknown profiles,
other capabilities/local addressing and unsupported configured F fail
explicitly at admission, without allocating a DSN.

`mac_join_event_t` reuses the genuine POLL event **storage type**. Its event
kind is nevertheless from `MAC_JOIN_*`, not `MAC_POLL_*`; the bridge maps
the namespaces explicitly, including FAILURE and CANCEL.

`step` consumes an ordered event or NULL poll. API argument/state errors
leave caller state, transmitter and output unchanged. A valid processed call
can advance time/work even for stale input; `MAC_JOIN_OK` is not success of
association or delivery. Inspect phase, record and action observation.
`take` copies a terminal record once. `release` requires DONE, taken,
confirmed restoration and the same idle transmitter generation. FAULT never
releases ownership, even if that transmitter happens to be idle.

## Actual sequence and time

Time is a caller's continuous uint32 **16-us symbol** domain. Every compared
true interval/gap is below `2^31`, with no hidden wraps. Equal times and
modular wrap are supported. No clock reader or live sample is substituted
for captured trailing-PPDU-end timestamps. Raw interval bounds are not exact
captured events and must not be supplied in SOURCE/FRAME timestamp fields;
this component does not adapt or link the raw interval owner.

1. One-shot PREPARE requests the selected channel/PAN, normal filter and
   receiver-on state. A matching PREPARED is a truthful adapter confirmation,
   not an echo generated by this module.
2. The real encoder builds the Request. `mac_tx_submit` allocates its DSN
   on the existing owner. Request `mac_tx_step` executes **inside** the
   controller and exposes its real one-shot RADIO actions. Original SOURCE
   events drive it; failed-CRC ACK inputs are never passed as valid ACKs.
3. Only the actual ACK_WAIT -> STOPPING/ACKED transition establishes captured
   Request ACK end A. The decision boundary is `A + response_wait*960`,
   where the caller supplies a valid configured value 2..64.
4. The Request action is retired promptly through the real QUIESCED/DONE/
   release path, under unchanged TX 1024-symbol/16-step bounds. It is not
   held alive through R. The controller then waits until the decision boundary.
5. The real `mac_poll_start/step_rx` executes one extended-source extraction
   using the same DSN owner. Its independent continuous RX/ACK preparation,
   original TX-step witness, Pending1 F interval and loss-free closure remain
   unchanged. No repeated extraction is invented.
6. A copied `POLL NO_DATA/COMMAND` is still dispatched through the actual
   Association context. Terminal Response metadata is taken immediately,
   before subsequent POLL cleanup can fail. All lower-layer results remain
   independent of final restoration.

R is a known staged decision period, not the total attempt lifetime.
`extraction.lifetime/work` independently bound the whole attempt; extraction
receives the remaining local lifetime and the configured work bound.
`frame_wait` is a **caller-valid configured PIB F**, not an arbitrary
application timeout or a guessed default.

### Bounded Response context without retimestamping

This slice admits **F=1..65534**, an explicit product/API restriction imposed
by the existing context's maximum 65535-symbol lifetime. From the real accepted
Data Request ACK end B it starts a context at B with lifetime `F+1`.
The intersection of POLL's `(B,B+F]` with that context's `[B,B+F+1)` preserves
the inclusive complete-frame upper boundary in integral symbols.

The context receives the original copied frame timestamp and actual current
foreground time. A frame ending at D=B+F and dispatched at D can match.
The same frame dispatched at D+1 can be retained by POLL while the context
reports EXPIRED; neither byte stream nor timestamp is altered to force a match.
This is a limited contextual window derived from valid stage facts, **not**
a solution to #45. F=65535 or larger is explicitly unsupported here.
Timestamp zero is valid; inspect outcomes, not a nonzero-timestamp heuristic.

## One-shot adapter obligations

An action contains epoch/generation/token correlation, `until`, the relevant
logical state and, for RADIO, a complete real `mac_tx_action_t`. Diagnostics
and decision/receive boundaries remain in the context; ACTION_NONE is not
completion. The saved state is supplied by the caller, never selected from a
board default.

| Action / event | Obligation |
| --- | --- |
| PREPARE / PREPARED | Confirm exact requested logical configuration and safe initial Request service; save/retain the caller-confirmed original state |
| RADIO / SOURCE | Execute one real Request radio/random/quiesce action; copy immutable TX bytes with `mac_tx_copy`; report actual captured source times and CRC truth |
| TX / TX | POLL grants exactly one external real `mac_tx_step`; echo its original input/result and foreground call time before later-time events; honor `tx_cancel` |
| RECEIVE / PREPARED | Establish the unchanged independent POLL RX/ACK lease, surviving prompt retirement of its TX action |
| CLOSE / CLOSED | Confirm ordered loss-free drainage, required immediate receiver ACKs, applicable IFS and safe receive handoff |
| RESTORE / RESTORED | Retire all old preparation/RX/ACK/buffer permissions and physically restore the exact saved logical state; confirm after the owner's remaining IFS |

RESTORED is a truthful correlated confirmation of those obligations, not a
memory reset or an implemented adapter. It cannot complete while the real
transmitter/POLL remains owned. Cancelling an uncompleted PREPARE still
requires restoration to retire that permission. No retry of a stale token
grants a new action. Local cleanup is bounded by 4096 symbols/64 calls; it does
not extend either component's existing retirement limits.

POLL FAILURE is forwarded as FAILURE, never relabeled CANCEL to bypass
uncertainty. A prior Response survives that failure and a later failed close,
but ownership remains retained. Clock ambiguity, unknown TX generation,
failed retirement or missing restoration similarly cannot be cleared by init
and called restored.

Every eligible received Response, including refusal, has an independent
immediate-ACK obligation. This foreground context does not schedule it through
CSMA, prove it happened or decide general MAC ACK eligibility. Lower-MAC ACK
service can also be required for frames ignored by the contextual filter.

## Result meanings

`POLL_RESULT` contains the actual extraction record, including NO_DATA with
command bytes. `REQUEST_RESULT` retains the actual terminal Request result
such as NO_ACK or channel-access failure. `LOCAL_ABORT` records local policy/
clock/adapter failures, never protocol NO_DATA. API result fields initially
use `MAC_JOIN_UNCALLED=255`, not an invented successful operation.

The first local reason/error stage, later cleanup error, Request outcome,
POLL reason/cleanup and Association observation remain separate. An existing
receipt is not erased by a later fault. A record may therefore contain a
matched Response and failed cleanup simultaneously.

`ALLOCATED`, `REFUSED` and `EXTENDED_ONLY` are the real context's raw MAC
metadata. FFFE is not an installed Zigbee ED address. A known IEEE match is
byte equality, not authentication; a source observed after short selection
remains UNBOUND. Response DSN is the coordinator's DSN, not either Request DSN.
No result implies PIB installation, MAC membership, successful response ACK,
network admission, BDB completion or security.

## Returning-work reduction (2026-09-25)

The returning-work step used **32310..32744/32768 CODE** and
**2884+64/3200 XDATA reservation**. All22 rows below retain their original
events, outcomes and SP peaks; subtract19 CODE bytes from each historical
row. Worst CODE headroom is24 bytes. Stack starts at51, unwinds to50 and
still peaks at7C under the unchanged7C cap.

The later complete-join decoder lowering reduces each image by a further43
CODE bytes: current profiles use32267..32701 CODE, the same2884 ordinary
XDATA and stack start51, with measured peaks65..73. All22 original profiles
and116 artifact/12 guard/3 peak negatives per profile still execute on both
boards. Only compiler pointer/length copies change; command admission and
the error-atomic645-byte controller/shadow contract remain unchanged.

The measured SDCC4.2 large-model object costs are identical for `generic`
and `lg_esl29_rev03`:

| Object | CSEG before → after | XSEG before → after | DSEG | OSEG | BSEG bits before → after |
| --- | ---: | ---: | ---: | ---: | ---: |
| mac_join | 7184 → 7170 | 1067 → 885 | 5 → 5 | 0 → 0 | 1 → 1 |
| mac_poll | 7619 → 7614 | 350 → 299 | 5 → 5 | 0 → 0 | 4 → 3 |

CONST stays zero. The combined saving is **233 ordinary-XDATA and19 CODE
bytes**, not600 bytes or proof of complete MCU fit. The full645-byte
transactional `mac_join_t` staging object is deliberately retained. Public
contexts, requests, events, actions, diagnostics and pointer qualifiers do
not change. Complete composition ownership is now production0..1666,
caller1667..2857 and libc2858..2883 (1667/1191/26 bytes); reserved status and
the entire unallocated region remain guarded. No DATA overlay or new stack
capacity is introduced.

The real165-byte C union contains either97-byte start work or165-byte step
work. Start's43-byte proposed request,26-byte header,2-byte command,25-byte
encoded body and length remain mutually disjoint while the real codec runs.
Every header field is assigned, including zero version/sequence, before
encoding; prior step bytes cannot supply an accidental header field.
During step, input48, output44 and POLL action25 remain distinct throughout.
Only the nested48-byte slot is reused, in this actual returning order:

1. `pr` is consumed by genuine `mac_poll_start` before extraction uses `pe`.
2. `pe` is consumed by genuine `mac_poll_step_rx` before `window` constructs `ar`.
3. `ar` is consumed by genuine `mac_association_start` before `receipt` constructs `ae`.
4. FRAME `ae` is consumed before `finish_window` can reuse it for CANCEL.
   Its borrowed body is in `staged.record.poll`, never in the returning slot.

All nested contexts, records and outgoing bytes stay outside this union;
`pa` remains live across `window/receipt/finish_window`. No returning member
is retained by a lower service. The [POLL unions](MAC_POLL.md#returning-work-ownership)
likewise never overlap this caller's live work.

Both-board native and nonrecovering ASan/UBSan runs retain the original
corpora and repeat all22 sequences with284 interleaved, genuinely
cancelled/restored/taken/released attempts in an independent synthetic device
world. They compare full original context/TX/event/action/receipt bytes and
exercise rejected calls after scratch reuse. No faulted owner is reset.
All22 generic images were independently replayed with116 artifact,
12 guard and3 peak negatives each, plus the original alias/four-MMIO
controls. Both boards pass the complete image/ABI checks and have identical
linked CODE, raw CDB, parsed map, memory report and immediate listing bytes,
and identical complete object-area inventories. Unlinked `.rel` files differ
only in the build-directory `;!FILE` comment. Full Actions acceptance is
separate; this is focused local evidence, not a new hardware observation.

These are original BSD refactorings, with no new wire interpretation,
dependency or peripheral access. Memory facts remain TI **SWRU191F, revised
April2014**, §2.2.2 pp27–28: `1F00..1FFF` is the IRAM alias, not another pool.
**SWRZ031, April2009**, Table1/§1.1/§1.2 DMA and MAC Timer concerns are
unchanged; neither peripheral is touched. #40/#45 and physical gates remain
open. Larger in-place/partial-control experiments failed the old CODE/DATA
constraints and are not production changes.

## Full-profile static shadow binding (compile-only)

`CC2530_JOIN_WORKSPACE` is an explicit full-composition storage opt-in,
**not banking, a new public ABI, or complete-image acceptance**. The ordinary
component retains its private `static mac_join_t MAC_JOIN_RAM staged`.
With the flag, the same source instead declares:

```c
extern mac_join_t MAC_JOIN_RAM mac_join_staged;
```

Every existing `staged` use names that statically addressed symbol. There is
no workspace pointer, additional parameter, shortened snapshot, in-place
mutation, successful service stub or fallback allocation. No `JOIN_FAR`,
banking-header or stack-policy change accompanies this option.

SDCC4.2 large-model measurements, identical on both board definitions:

| mac_join object | CSEG | Ordinary XSEG | DSEG | OSEG | BSEG bits | CONST |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Ordinary | 7170 | 885 | 5 | 0 | 1 | 0 |
| External shadow | 7170 | 240 | 5 | 0 | 1 | 0 |

The645-byte allocation is removed from this **object**, not from the
application's memory requirement. Its `.rel` contains an unresolved
`_mac_join_staged` reference. There are4640 identical emitted instructions
and235 direct-symbol operands: the complete `.asm` is identical after
removing precisely the private645-byte declaration and renaming that symbol.
The complete `.adb` differs only in the full object's static/global linkage
record; all types, parameters, private locals, return ABIs and qualifiers
remain identical. Complete `.rel`, `.asm`, `.lst` and `.adb` identities are
pinned by the separate object check, including assembled bytes/relocations.
Only the `.rel` build-path comment is excluded, never object contents.
Its32 negative controls reject changed bytes, wrong type/size/linkage,
extra shadow allocation, pointer substitution and the ordinary object passed
as the external profile.

### Required integrator proof

The integrator must bind `_mac_join_staged` to the exact address of a real
C-allocated, complete `mac_join_t` field. A proposed association aggregate is
`{ mac_join_t context; mac_join_t staged; }` inside a separately proved phase
union. A linker alias by itself reserves **no memory**. The full linked map,
raw CDB/type/member offset, enclosing allocation and complete simultaneous
lifetimes must prove that binding and disjointness from the live context,
TX owner, input/output/request spans, other returning work and libc scratch.
The complete645-byte extent must remain below1E00; status, MMIO, CODE and
the1F00..1FFF IRAM alias are never possible backing allocations.

The shadow is used by `init`, `start`, **every `step` and its nested helpers**,
and `release`, not merely admission. It must remain exclusively available
until each operation returns. `take` uses the caller's retained record.
The actual full-context copies/publication and all error paths are unchanged.
No association/runtime union reuse is admitted while a call is active or
while the parent still needs the retained association context; failed cleanup
cannot be turned into release. Required outcome/timestamp metadata must be
preserved outside any reused phase storage.

The proposed1290-byte aggregate versus1042-byte runtime member would grow the
phase union by248 while removing645 private bytes: a **potential397-byte net
saving**, or630 with the earlier233-byte reduction. This arithmetic is not
a measured full-profile allocation, DATA/stack proof, linked image or
alias-aware execution result. Those aggregate/binding/phase-lifetime gates
remain the responsibility of the complete composition.

### Focused reproduction and evidence

For the generic board (`CC2530_BOARD=1` selects the corresponding LG object):

```sh
make -s -j1 BOARD=generic BUILD=build/stage2-mac-static/default-generic \
  build/stage2-mac-static/default-generic/mac_join.rel
mkdir -p build/stage2-mac-static/external-generic
sdcc -mmcs51 --model-large --std-c99 --debug --opt-code-size --Werror \
  -Iinclude -DCC2530_BOARD=0 -DCC2530_JOIN_WORKSPACE \
  -c src/mac_join.c -o build/stage2-mac-static/external-generic/mac_join.rel
python3 -B tests/boot_mac_join.py \
  --output build/stage2-mac-static/default-generic \
  --check-workspace-object build/stage2-mac-static/external-generic
```

The explicit object-check mode does not link, bind an address, simulate or
access hardware. Normal `test-mac-join` retains all22 cases and its existing
budgets, negative controls and15-second simulator deadlines. All22 rebuilt
ordinary executables, parsed maps and memory reports match the fully
replayed233-byte-reduction baseline exactly; only source/debug/listing
metadata changes, with complete updated manifest pins.
All22 updated ordinary image/ABI checks pass. Focused ordinary case20 was
also replayed through its real retained-fault/receipt path at unchanged SP7C,
with116 artifact,12 guard,3 peak, one alias and four MMIO negative controls.
No external-shadow target link or simulation was performed.

Native and nonrecovering ASan/UBSan external-shadow runs pass on both boards,
including all22 sequences, the existing exact-span/admission cases and284
interleaved retired attempts. Only those host tests provide a separate real
global shadow definition; it is not a target aggregate or SRAM-fit proof.
This is original BSD storage/linkage work using the same SWRU191F §2.2.2
memory-space basis cited above. No wire, peripheral, physical or #40/#45
acceptance boundary changes.

## Interval consumer profile (`CC2530_MAC_LINK`)

Under [`CC2530_MAC_LINK`](MAC_POLL.md#interval-consumer-profile-cc2530_mac_link),
the join owner is `mac_tx_interval_t` and the `radio` action is a
`mac_tx_interval_action_t`. The internal Association Request runs through
`mac_tx_observed_step` with interval/BUSY/RETIRED input. The TX grant of a POLL is the
observed step described in MAC_POLL.md. `record.request_ack` is the Request
ACK's `ceil(upper)` rather than a captured end. Extraction preparation waits
until at least `request_ack + R`, so the true ACK end plus
macResponseWaitTime has elapsed. A Request `TIMING_UNCERTAIN` or POLL
`MAC_POLL_TIMING_UNCERTAIN` is a local abort,
`MAC_JOIN_TIMING_UNCERTAIN` (10). It is never a fabricated protocol
outcome. Retries, refusal and Response semantics are unchanged.

The Request is not stepped in the same call as its submit, because adapter
preparation needs the owner in DRAW:
- After submit, and after each retry that returns to DRAW, join issues
  `MAC_JOIN_ACTION_ARM` (7). Only a correlated `MAC_JOIN_ARMED` (9) allows the
  first observed step.
- Before releasing a DONE slot it issues `MAC_JOIN_ACTION_DISARM` (8) and needs
  `MAC_JOIN_DISARMED` (10). This includes cancellation before ARMED.
- Cancellation or lifetime expiry while waiting never draws randomness or
  transmits.
- PREPARE/RECEIVE confirm an installed configuration with the adapter OFF and
  drained. They do not confirm receive coverage.

See the [link driver](MAC_LINK_DRIVER.md#preparation-handshakes).

The shared corpus is `tests/test_mac_link_join.c`. With banked-join flags,
the SDCC 4.2.0 link profile is 7784 `BJ_BANK2` CODE bytes (exact 7189),
XSEG 270 (240) and DATA 7 (5). The external workspace grows from 645 to 655
bytes. The exact profile's compiled instructions are unchanged for both boards
and banked flags. All 22 `mac_join_<n>_test.ihx`, parsed maps and memory
reports are byte-identical. The 22 manifest pins and compile-only
`WORKSPACE_PINS` (rel/asm/lst) were refreshed only because source-line records
moved, and again for the ARM handshake (including one split exact-profile
condition). Evidence level: **host-tested and SDCC compile-checked**.

The optional [compact link profile](MAC_LINK_DRIVER.md#experimental-returning-work-profile)
uses the admitted caller context directly instead of an association shadow.
It preserves rejected-start atomicity and clears its private scratch and
borrowed views before returning. This changes the BDB phase layout; all
callers must use the same profile. It is not enabled in these exact images.

## Reproducible evidence and historical resources

The following numeric ledger records the pre-union profile. The current
costs/ownership above supersede its resource numbers; commands, all22 cases,
public layouts, budgets, deadline and acceptance obligations remain intact.

Canonical target: `test-mac-join`, with strict `host-mac-join-tests` and
nonrecovering `host-mac-join-tests-sanitize`. Native runs all 22 sequences
without a case macro. Target compilation uses `MAC_JOIN_CASE=0..21`, each
with stem `mac_join_<n>_test`, unchanged repository compiler/link flags and:

```text
mac_frame.rel mac_tx.rel mac_association.rel mac_poll.rel mac_join.rel
mac_join_<n>_test.rel
```

Immediately after each link, copy all six relocated `MODULE.rst` files to
`mac_join_<n>_test.MODULE.rst`. `boot_mac_join.py --output BUILD --simulator s51`
consumes all 22 images and their own snapshots. None is a board IMAGE,
flashing input or firmware-upload artifact.
The caller object is `mac_join_<n>_test.rel`, not `test_mac_join.rel`, and its
snapshot is `mac_join_<n>_test.mac_join_<n>_test.rst`. Do not interleave links
that share relocated-listing paths. Each stem supplies `.ihx`, `.map`, `.cdb`
and `.mem`; the proof also consumes all six `.rel` object inventories.

Each row compiles the same caller with exactly `-DMAC_JOIN_CASE=N`.
Native and sanitizer executables omit this define and execute every row.

`test-common` includes the complete target; the two existing composed-service
CI jobs run it once per board, outside `test-common-core`. The32-job partition,
all28 board/image checks, artifact whitelist and15-minute job deadlines stay
unchanged.

| N | Image stem | CODE bytes | Peak SP | Sequence |
| ---: | --- | ---: | ---: | --- |
| 0 | `mac_join_0_test` | 32703 | 0x7C | Legacy allocated Response |
| 1 | `mac_join_1_test` | 32704 | 0x7C | Refusal status 1, address FFFF |
| 2 | `mac_join_2_test` | 32704 | 0x7C | Refusal status 2, address FFFF |
| 3 | `mac_join_3_test` | 32704 | 0x7C | Success with FFFE, extended-only metadata |
| 4 | `mac_join_4_test` | 32704 | 0x7C | R22 broadcast destination PAN, short selection/unbound IEEE, wrap, exact D |
| 5 | `mac_join_5_test` | 32347 | 0x76 | Request NO_ACK through real retries |
| 6 | `mac_join_6_test` | 32335 | 0x76 | Cancellation during decision wait |
| 7 | `mac_join_7_test` | 32329 | 0x76 | Local lifetime exhaustion during decision wait |
| 8 | `mac_join_8_test` | 32373 | 0x76 | Request retirement deadline, retained TX fault |
| 9 | `mac_join_9_test` | 32621 | 0x7C | Restoration timeout after Response |
| 10 | `mac_join_10_test` | 32370 | 0x76 | Clock regression during decision wait |
| 11 | `mac_join_11_test` | 32610 | 0x7A | Data Request NO_ACK through real POLL retries |
| 12 | `mac_join_12_test` | 32746 | 0x7C | Bad CRC followed by valid Response |
| 13 | `mac_join_13_test` | 32740 | 0x7A | Response captured at D, processed at D+1: COMMAND plus EXPIRED |
| 14 | `mac_join_14_test` | 32606 | 0x7A | Data Request ACK Pending0 |
| 15 | `mac_join_15_test` | 32699 | 0x7C | Response-wait 64, F=65534, exact D |
| 16 | `mac_join_16_test` | 32348 | 0x76 | Actual Request channel-access exhaustion |
| 17 | `mac_join_17_test` | 32638 | 0x7A | Pending1 timeout and confirmed drainage/CLOSED |
| 18 | `mac_join_18_test` | 32343 | 0x6E | Cancellation before Request admission |
| 19 | `mac_join_19_test` | 32329 | 0x6E | Whole-attempt work exhaustion before admission |
| 20 | `mac_join_20_test` | 32763 | 0x7C | POLL FAILURE after Response, failed cleanup retains metadata and owner |
| 21 | `mac_join_21_test` | 32435 | 0x76 | Stale/duplicate completions, then cancellation |

| Production object | CODE | Ordinary XDATA |
| --- | ---: | ---: |
| mac_frame | 7136 | 216 |
| mac_tx | 5650 | 191 |
| mac_association | 2839 | 76 |
| mac_poll | 7619 | 350 |
| mac_join | 7184 | 1067 |
| **Production total** | **30428** | **1900** |

Each complete image uses **32329..32763 /32768 CODE** and
**3117+64 /3200 XDATA reservation**. Worst CODE headroom is only 5 bytes.
Stack starts at 0x51, checkpoint SP is 0x50, independently measured uninterrupted
peaks are 0x6E..0x7C under the unchanged 0x7C cap. These are standalone composition
costs, not full-stack or interrupt headroom. No old budget or corpus changes.

Current object-area identities, complete artifact-manifest hashes, ABI and
allocation maps are pinned in `boot_mac_join.py` (`OBJECTS`, `PINS`, `PUBLIC`,
`FIELDS`, `CALLER`, `RUNTIME`). Historical ordinary XDATA ownership was production
0..1899, caller 1900..3090 and runtime 3091..3116. The result occupies
0x1E00..0x1E07; the remaining reserved status bytes and all unallocated
ordinary XDATA stay guarded. XDATA 0x1F00..0x1FFF is only the genuine IRAM
alias, never an additional allocation pool.

The context is 645 bytes on SDCC, request 43, event 48 (the real POLL event type),
action 44 and result 194. The 1067-byte private contribution includes one
explicit 645-byte transactional staging object. Whole-context staging trades
real XDATA for CODE/IRAM without inventing an alias pool; nested types are
copied as their actual types, not cast mirrors. Caller TX ownership never
moves.

The 22 genuine sequences cover allocated/refused/FFFE Responses, R22 broadcast
PAN with unbound short selection, wrap and exact D, both Request and extraction
NO_ACK, Pending0, timeout closure, cancellation/work/lifetime/clock bounds,
missing retirement/restoration, CRC rejection, stale/duplicate events and
Response preservation through an actual POLL failure. Native exact-sized
allocations additionally cover F=0/1/65534/65535/FFFFFFFF, every uint8
response-wait value, 254 unsupported profiles, 127 ACK spans and 3429 TX-copy
capacity cases with unchanged failure outputs and no DSN allocation at admission.

Target expectations include independent exact request/query wire bytes and
the entire 194-byte retained record with independently derived timestamps.
The eight-byte target marker records driver completion/failure and case number,
not association success; expected FAULT cases must retain their real owners.
The proof pins all CODE, raw CDB **before decoding**, the complete parsed map,
all object areas and all six complete immediate listings; it checks actual
calls, public/private/helper/field/pointer/return ABI and exact caller/runtime
ownership. Every case has 116 artifact, 12 guard and 3 peak negative controls,
plus the common missing-alias and four MMIO negatives.

Execution uses the genuine `1F00..1FFF` IRAM alias, untouched unallocated
XDATA/status tail, non-CPU SFR guards and a separate uninterrupted peak run.
Every simulator process keeps its 15-second deadline. Focused evidence is
**native/sanitizer, image-checked and generic simulated**; full both-board
acceptance belongs in Actions. No hardware, PHY timing, RF or interoperability
has been observed by these tests.

The complete generic proof, including all 22 images and negative controls,
took **239.870 seconds elapsed** in the measured local run on 23 September
2026. This excludes compilation and native/sanitizer execution; it is not a
CI-runner timing guarantee or a replacement for the per-process deadline.
