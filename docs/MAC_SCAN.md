# Offline active-scan controller

This is a bounded #14 preparation step over the real `mac_tx` and
`nwk_candidates` implementations. It is **not** an IEEE MLME-SCAN primitive,
radio implementation, MAC association procedure or Zigbee network membership.
The standalone common-check target adds no board IMAGE, callback framework,
heap or hardware access.

## Primary decisions and exclusions

The retained primary documents were read directly:

* [IEEE Std 802.15.4-2006](https://people.ece.ubc.ca/~edc/7860/data/802.15.4-2006.pdf),
  SHA256
  `d245c8bb208f6cdb585fb753cefa67e367d30eebd55b9ed9957c8fd152d6a055`.
* [Zigbee Core R22, 05-3474-22, April 19, 2017](https://github.com/pvginkel/ZigBeeHomeAutomation/blob/fc30145012eacd3a5af170b8ae8e0d4c848c2525/Documents/docs-05-3474-22-0csg-zigbee-specification.pdf),
  SHA256
  `991dd02b7e5764ac349c47d5c4cf0fbe01529ff6594df03e8dad3d3d4df9e274`.

These public PDFs and the temporary PDF reader are research inputs, not
vendored files or CI artifacts. No implementation, SDK, capture or private
identity was imported. All new source is original BSD-3-Clause work.
No secondary summary, R23 or alternative BDB version supplied wire values.

| Decision | Exact primary reference; implemented boundary |
| --- | --- |
| Channel set/order | IEEE sections6.1.2.1 and7.5.2.1, printed pp.29,172-173: channel bitmap and lowest-to-highest order. Only page0 channels11-26 are supported, with a nonempty explicit subset of `07FFF800`. There is no automatic channel selection or alternate page. |
| Dwell | IEEE section7.1.11.1.1/Table67 pp.112-113 and section7.5.2.1.2 pp.173-174: duration `n=0..14`, receiver enabled for at most `aBaseSuperframeDuration * (2^n + 1)` symbols after successful Request transmission. Table85 pp.159-160 gives slot60 and slots16, hence **960**, not a remembered millisecond constant. Endpoints here are1920 and15,729,600 symbols. |
| Request bytes | IEEE section7.3.7/Figure62 p.156, section7.2.2.4 and Table82 pp.147-149: command ID07, short destination address/PAN broadcast, source absent, Pending/ACK/Security zero. Selected v0, uncompressed, FCS-free body is `03 08 DSN FF FF FF FF 07`, eight bytes; FCF/PAN/short address are little-endian. |
| PAN filtering | IEEE section7.5.2.1.2 p.173: save macPANId, use `FFFF` to accept Beacons from any PAN, restore the saved value after scanning. Discard non-Beacons and do not extract pending data even when the device is listed. No Data Request or other response is scheduled here. |
| Unscanned | IEEE section7.1.11.1.3 p.114 and section7.5.2.1.2 p.174: channel-access failure leaves a channel unscanned; a channel not scanned for the full dwell is also unscanned. Table68 pp.115-116 defines requested-but-unscanned bits. This controller clears a bit only after exact, confirmed full-window closure. |
| Descriptor/termination distinction | IEEE sections7.1.11.1.3,7.5.2.1.2 pp.113-114,173-174: MAC PAN descriptors, uniqueness by PAN/source on the current channel, macAutoRequest behavior, notifications and descriptor-limit termination. The four-entry NWK table is **not** that list; see below. |
| Zigbee boundary | R22 sections3.6.1.3/3.6.1.4.1 pp.335-336 require discovery/scan confirms and substantially more parent-selection state; section3.6.7/Table3-71/Figure3-54 pp.389-391 defines the15-byte NWK Beacon payload. The unchanged [collector](NWK_CANDIDATES.md) remains preliminary. |

The actual [MAC transmitter](MAC_TX.md) supplies the single device-wide DSN,
unslotted CCA/backoff, IFS, finite work/lifetime and confirmed TX cleanup.
The controller does not implement a second DSN, bypass CCA, call
`mac_tx_init`, or turn a queue dequeue into a transmission. A Request has no
ACK and no frame retry; real CCA backoff attempts still occur. Its TX
`UNACKNOWLEDGED` outcome is not delivery or evidence that any coordinator heard
it. TX Pending must be zero even though the primary receiver rule ignores it.

The selected candidate policy remains profile2, BO15, Association Permit and
ED Capacity, with SO ignored for BO15, raw update/depth/offset, copied source
addresses and strict inherited syntax exclusions. Its identity additionally
includes channel and Extended PAN ID. Protected/unsupported Beacons are not
unsecured here; there is no PAN descriptor SecurityFailure/LQI/timestamp list
or MLME-BEACON-NOTIFY implementation.

**Project policy:** retain at most four preliminary candidates, continue all
requested channels after filling the table, and set a sticky `overflow` on a
new unique eligible candidate that cannot fit. Duplicates/withdrawals still
work while full. This resembles continuing notification-style collection,
but does not implement or change macAutoRequest. It must not be relabeled
the IEEE descriptor-limit algorithm or its `LIMIT_REACHED`/`NO_BEACON` statuses.
A zero candidate count does not prove that no MAC Beacon was received.

Restoring the original channel, logical filter mode and idle RX mode in
addition to PAN is a **project ownership policy**, not an invented IEEE
channel-restoration requirement. No beacons are transmitted by this ED, so
coordinator beacon-suspension/recommencement behavior is outside the role.

## API, time and owned storage

[mac_scan.h](../include/mac_scan.h) defines five explicit-result entries:

* `mac_scan_init`: NULL is INVALID; successful fresh-storage initialization is
  OK. This is memory initialization only, never recovery of a radio/TX fault.
* `mac_scan_start`: requires an initialized IDLE scan and the initialized,
  IDLE **persistent device-wide** `mac_tx_t`. It copies the request/saved
  state, clears its embedded candidate table and binds the TX object for the
  entire lease. Generation exhaustion, busy/invalid state and unsupported
  request bounds are explicit errors without mutation.
* `mac_scan_step`: consumes a copied-value event or NULL poll and returns a
  caller-owned action record. API errors leave state/output unchanged.
  Successful steps may consume work/time for stale or unrelated input.
  Operational failure is represented by phase/reason, not API OK.
* `mac_scan_get`: copies one candidate after DONE or FAULT, before release.
  Errors, including a bad index, preserve the entire output.
* `mac_scan_release`: only DONE with the same idle transmitter/generation can
  release the scan lease. FAULT cannot be released or restarted.

Request duration is0-14, work is1-4096 successful working steps, and lifetime
is1-`10000000` symbols. A too-short positive lifetime is permitted and yields
an explicit incomplete transaction, not a false full dwell. Each real Request
gets4096 TX symbols and64 TX steps; global scan bounds can cancel it sooner.
Termination starts a separate4096-symbol/64-step cleanup allowance. The
public `deadline` then denotes that cleanup deadline; the original working
deadline is no longer used. Missing closure has a1024-symbol grace for
confirmation delivery, not permission to keep RX enabled beyond its dwell.
No clock progression is necessary to exhaust a positive work allowance.

All clocks are the **same abstract modulo-32-bit symbol epoch** as `mac_tx`,
one symbol being the selected nominal16 us. Every compared interval,
foreground gap and event lag must be less than `80000000`; backward/half-range
time produces a retained fault. Natural wrap within that bound is tested.
The maximum16-channel duration14 dwell sum is251,673,600 symbols, below the
maximum working lifetime; configuration, CCA and foreground overhead still
consume the remaining allowance. No deadline addition is a calibrated
conversion from raw Sleep Timer ticks.

Events must be delivered in order before advancing the foreground watermark.
An event stamp earlier than the previous accepted foreground time, later
than `now`, with a wrong scan generation/token, or in an unrelated phase
cannot confirm an operation. Frame ends use actual captured timestamps, not
processing time. CANCEL uses the caller's current control time. EVENT_TX is
explicitly a **foreground pump-result** time, not a captured radio edge.
A completed EVENT_TX stamped at time `t` must therefore be delivered **before**
any poll or cancellation at a later timestamp. It cannot be delivered after
the watermark has advanced past `t`.

Generation is nonwrapping32-bit. The16-bit action token changes with each
grant. At most4096 working steps,64 cleanup steps and16 extra submission
tokens fit far below token wrap. A new scan cannot recycle a generation at
`FFFFFFFF`; an exhausted MAC generation is not reset by this controller.

All pointers denote actual, accessible, disjoint objects for the declared
sizes. On SDCC, scan/TX/request/event/action/result objects use ordinary XDATA
via the public `MAC_SCAN_RAM` qualifier; Beacon input remains a generic
pointer and can refer to CODE or XDATA. The bound TX object outlives the lease.
Input bodies are consumed synchronously and never retained. No internal pool
pointer is returned. Caller diagnostics are in its own `mac_scan_t`, not in
hidden allocated storage.

Non-NULL and the limited version/phase checks do not validate invented numeric
pointers or arbitrary corruption. Never pass MMIO, compiler-private storage,
reserved status, the IRAM alias, CODE as writable storage, or overlapping
records. Callers must not edit context fields. The code is foreground-only,
non-reentrant and serialized with the transmitter/decoders. It has no board
headers or registers. SDCC qualifiers/volatile temporaries are storage choices,
not locks or ISR safety.

## Action/event protocol and serialized TX bridge

Entry requires the exclusive adapter owner to have **already confirmed** a
quiescent page0 snapshot: PAN, channel11-26, logical filter NORMAL or BEACONS,
and idle RX-on bit0/1. No old commands, TX/RX buffers or competing MAC job may
be pending. The logical filter enum is not a CC2530 register image; an
unrepresentable initial mode is unsupported. The future adapter must preserve
the entire represented state exactly.

Normal flow:

```
CONFIG action / CONFIGURED
  -> real mac_tx_submit(canonical Request)
  -> [TX pump grant / real mac_tx_step / EVENT_TX]*
  -> real mac_tx_release, still under the scan lease
  -> RECEIVE action / OPENED
  -> BEACON observations / CLOSED
  -> next requested channel, or RESTORE action / RESTORED
  -> DONE -> explicit mac_scan_release
```

* CONFIG asks for page0/current channel, PANFFFF, Beacon-only filtering,
  no pending-data extraction/automatic response and RX off during transition.
  CONFIGURED must confirm the whole requested logical state.
* TX is **not** a radio-success request. It grants exactly one foreground
  `mac_tx_step` on the bound real transmitter. The caller may call
  `mac_tx_copy` into its own exact-sized buffer. A real TX action, not this
  grant, authorizes random input, scheduled CCA/immediate TX or quiescence.
* RECEIVE grants one bounded receive window. OPENED confirms its actual start
  and PAN/channel/filter/RX-on state. The adapter must arrange automatic
  closure at `start + dwell`, independently of foreground polling, and deliver
  ordered copied frames followed by CLOSED after all in-window frames.
  CLOSED confirms RX off, drained buffer ownership and exactly that end.
  The accepted frame-end interval is `[start, start+dwell)`.
* RESTORE cancels/drains every outstanding scan configuration/RX operation,
  purges stale events and future state writes, and restores the exact saved
  PAN/channel/filter/idle-RX state. RESTORED confirms all of that, not merely
  register echo. It is accepted only before the cleanup deadline/work limit.
  A failed/wrong/missing restoration retains ownership in FAULT.

The TX bridge must be implemented by the eventual single foreground owner,
not a generic callback or fake radio. For each TX grant:

1. Copy the scan generation/token. Call `mac_tx_step` exactly once, at a
   monotonic foreground time. Supply the next real MAC event or NULL.
   If `tx_cancel` is set, instead supply a CANCEL with the bound transmitter's
   current generation/retry/nb and that foreground timestamp.
2. Preserve/dispatch the actual returned MAC action. RANDOM needs an
   independent uniform byte, not security entropy. ATTEMPT needs the real
   scheduled CCA/TX semantics. QUIESCE needs real confirmation. Returned NONE
   does not repeat an older physical action.
3. Deliver EVENT_TX with the grant's scan generation/token, the actual API
   result and the **time of that call**. A pending physical action can complete
   later. The next MAC event waits for the next grant. The controller reads
   the actual TX phase/generation/last/outcome; fabricated completion records
   are outside the contract.
4. If cancellation interrupts delivery of a completed pump result at the
   **same timestamp**, retain and report that same completed call before any
   later-time poll/cancellation. Target scenario5 exercises this supported
   same-time ordering. A cancellation at `t+1` cannot precede delivery of the
   completed EVENT_TX at `t`: the later cancellation advances the watermark,
   making the older report stale and leaving its grant unresolved. Cleanup
   then reaches retained FAULT under its existing time/work bounds; the lease
   is not released. Do not retimestamp the old report or repeat `mac_tx_step`
   to work around this ordering violation. Do not hide a physical action that
   was returned before cancellation.

The controller itself executes real submit/release; the compiled caller
executes real step/copy. Their linked calls are pinned. No other operation on
the bound transmitter is permitted during the lease. In particular,
`mac_tx_init` is never a scan recovery operation, and an IDLE MAC slot during
RX is not permission for another client to submit DATA. DSN and `ready_at`
survive internal slot release and later scans.

SENT records the confirmed transmission in `sent`, even if later quiescence
fails. The window opens only after confirmed MAC completion/quiescence.
There is no inferred RX state at TX end. The gap before confirmed RX opening
is bounded by the transaction lifetime, not a hardware turnaround guarantee.
Correct immediate reception/timing remains an adapter/physical acceptance
requirement. `mac_tx` FAULT cannot be repaired by a scan RESTORE event: it
retains its slot and the controller retains the lease.

## Results and first-fault retention

Read the caller-owned context diagnostics, and copy candidates before release:

| Field | Meaning |
| --- | --- |
| `phase` | DONE requires positive restoration; FAULT retains ownership. Logical termination alone never permits release. |
| `reason` | First termination cause: FINISHED, CANCELLED, LIFETIME, WORK_LIMIT, CLOCK_ERROR, ADAPTER_ERROR, TX_ERROR or CLOSE_MISSING. FINISHED means the channel walk ended, not that every channel was scanned successfully. |
| `cleanup_error` | Separate first cleanup failure, without overwriting the original reason. FINISHED plus FAULT/cleanup error is not success. |
| `unscanned` | Requested channels without an exact confirmed full dwell, including CCA failure, incomplete/current and not-yet-attempted channels. |
| `sent` | Channels with confirmed Request PHY completion; not delivery, ACK or full dwell. |
| `candidates.count` | 0 means no retained eligible candidate;4 means the preliminary table is full. Neither value is a MAC descriptor result/status. |
| `overflow` | Sticky loss of a new eligible unique candidate at capacity; remains set after withdrawal frees an entry. |
| `uncertain` | Sticky uncertainty diagnostic, not an alternative ownership/release bit. |
| `tx_outcome` | Last observed real MAC outcome, including retained MAC failure detail. |

Thus empty complete windows, all-CCA-failed scans, partly scanned masks,
full/lossy candidate results and cancelled/failed transactions are distinct.
A delayed or malformed frame cannot erase good candidates. Full tables still
accept duplicate metadata and valid permit/capacity withdrawals. There is no
aging, LQI/RSSI calibration, link cost, freshness or ranking policy.

## Evidence and resource ledger

The detailed numbers/hashes below record the earlier control-staging baseline.
The #63 shared-code refresh leaves the scanner and all28 scenarios unchanged:
**29567 CODE,1501+64 XDATA, SP66**, with3201 CODE bytes remaining and the same
budgets/guards. All21 public entries, including the explicit decoder, are
pinned;221 artifact negatives and the missing-alias negative are retained.
See the [current evidence summary](VALIDATION.md#r22-response-profile-and-shared-proof-refresh-63);
these RX-header alternatives do not broaden scan admission.
The subsequent complete-join decoder lowering gives29524 CODE and the same
1501+64 XDATA, with stack start3D and observed SP59 on both boards. All28
scenarios,221 artifact negatives and the alias negative remain unchanged;
the older detailed hashes below are not the refreshed image identities.

Both board definitions passed strict native C99, ASan/UBSan and the genuine
six-module SDCC image. Native additions cover all256 duration values, all32
single mask bits, explicit request/API failures and unchanged output checks,
381 exact-sized input cases for26/44/88-byte layouts, exact-sized output
buffers, all256 CRC flags, stale/future/wrong-channel events and end-exclusive
frame handling. All four owned state fields are independently damaged in
CONFIGURED, OPENED and CLOSED confirmations (12 additional native cases).
One native-only regression completes a real granted MAC poll at `t`, delivers
cancellation at `t+1` and then the stale completed-pump report. With time frozen,
the existing cleanup work bound reaches retained FAULT, preserves CANCELLED
and the unchanged transmitter, and rejects release/restart and an unsolicited
restoration confirmation. It neither retimestamps nor repeats the MAC call.
This does not add target instructions or change the target28-scenario corpus.
Unchanged MAC-TX and candidate native corpora also pass for both definitions.
No external dependency was added.

The linked C corpus runs28 scenarios, including a full16-channel walk:
empty/full/overflow/withdraw/reuse; mixed short/extended pending-list layouts;
CODE and XDATA inputs; all/some CCA failures; DSN wrap and reuse of the same
owner across scans; enforced inherited IFS on an immediate subsequent scan;
cancel during configuration/possible TX/RX/before RX opening; adapter and MAC
faults; wrong/early/missing closure; frozen-clock work exhaustion; working
lifetime, cleanup work/deadline and backward/half-range clock failures; natural
32-bit wrap; duration14; and wrong-token followed by correct confirmation.
Every transmitted Request is copied through `mac_tx_copy` and checked against
its canonical bytes/DSN. These are synthetic public events, not RF observations.

The proof pins whole CODE, all20 public entries plus the exact done checkpoint,
public return types, whole private/caller/field ABI, object extents and all six
complete ordered relocated instruction sets. It checks the real call chain
including serialized `mac_tx_step` in the caller, and forbids a controller
call to `mac_tx_init` or a newly nested `mac_tx_step`.

There are217 damaged-artifact negatives plus a genuine missing-alias negative.
They retain the original52 and an appended conflicting public-return declaration for
each of the20 entries, including main. The complete matching declaration set
must contain only the expected declaration; identical duplicates remain
accepted and are checked by a positive control. Four further negatives cover
file-scope object size/address and private helper declaration/end records.
The control-staging addition covers complete F/S/L/T inventories, raw-control
corruption, conflicting/duplicate records and bounded staging storage.
Every module rejects dropped, duplicate and reordered instruction records.
CODE extent/content, private/field/caller/return ABI, entry/checkpoint, object
extent, stack accounting and alias allocation damage are rejected.
The simulator uses real compiled instructions, no patched returns, the shared
15-second process limit and one indexed transcript. Upper IRAM, unallocated
XDATA, status tail, disabled interrupt registers and final unwind are checked.

Measured with SDCC4.2.0 #13081 and the canonical large-model flags:

| Item | Measured |
| --- | --- |
| Whole CODE | **29,440**, contiguous `0000..72FF`; 3328 bytes remain below8000 |
| Ordinary XDATA | **1,492**, `0000..05D3` |
| Including entire64-byte status reservation | **1,556** |
| Scan context / TX context / embedded candidate table | **212 /168 /150** bytes on SDCC |
| Request / event / action / candidate |16 /23 /23 /36 bytes on SDCC |
| Production compiler-private XDATA |615, `0000..0266` |
| Caller allocation |851, `0267..05B9` |
| Linked runtime XDATA |26, `05BA..05D3`; `__gptrput_PARM_2` at05C5 |
| DSEG / OSEG / bit backing |44 /10 /2 bytes, plus8-byte register bank and1 packing byte |
| Stack start / final SP / measured peak SP |`41 /40 /62` |
| Measured stack use / lower-IRAM remainder |34 /29 bytes; upper80-FF remains guarded |

The separately justified composition budget is **32KiB CODE /2KiB XDATA
including64 reserved bytes**, within the assigned maximum. The image includes
five production modules, a full28-scenario caller, a212-byte saved-context
failure-output check and actual MAC/NWK codecs, not stand-ins. Its1556-byte
reservation leaves492 bytes within this test budget. Recovered CODE space
is emphatically **not full-stack fit**, production firmware headroom,
ISR nesting evidence or permission to increase a previous component budget.
The proof additionally enforces SP <=7C, with measured62; the existing
MAC-TX SP7C guard and every shared verifier remain unchanged.

An initial direct-pump proposal failed the32KiB link. Generic-pointer and
wide-value spills also consumed IRAM. The final design uses explicit ordinary
XDATA records, stored scratch/time values and serialized pumping, without
dropping any real call or scenario. No guard was relaxed to obtain a pass.

| Object | CODE incl. constants/startup | XSEG | DSEG/OSEG | Ordered instructions / bytes |
| --- | ---: | ---: | ---: | ---: |
| mac_frame |7009|207|15/10|4168 /7009|
| mac_tx |5650|191|8/0|3729 /5650|
| nwk_beacon |601|27|9/0|353 /601|
| nwk_candidates |2334|111|4/0|1519 /2334|
| mac_scan |8598|79|4/0|6230 /8590|
| mac_scan_test |4581|851|4/0|2686 /4483|

The remaining667 CODE bytes are CRT/library contributions. All18,685 ordered
instruction records (28,667 bytes) are normalized as
`six-hex-address:lowercase-byte-hex\n`; each per-module digest is pinned in
the proof. Constants and runtime are also covered by the whole CODE digest.
Private/caller/field digests cover563/69/51 complete sorted debug records,
preserving duplicates and a final newline, including two-byte XDATA and
three-byte generic pointer ABIs. Private coverage includes file-scope objects,
helper declarations and helper entry/end addresses, not only local parameters.
The new inventory includes complete F/S/L/T records and the transmitter's
private control/input types; existing public context layouts are unchanged.
All217 unique public records are pinned, allowing identical public duplicates
but rejecting conflicting declarations. Private duplicate multiplicity is
exact. Raw-byte CDB decoding rejects non-LF controls/separators before indexing.

Both boards produced identical hashes:

```
CODE    96468bf60d144c46297f558f54b610fe6755865bc1314a3ae7b03fc4efd8b23d
IHX     30d4c6fdb347ce9d7aa4a6dbb346d2f77e7c436bb12cdcd1ef7e17b5286fa729
private 9635d4b0c073cf26aae32d5655a3c00ed653580f185c274db3e33f721ec15a71
caller  27b97210deac66da2f841934ce4001737b8870dd7b6f9c8882da55ff2c9ddccb
fields  659464f7b5d96ba06e79f413f6a255d76b148b1fcf94f83ef341f6e56e0a6a88
```

Scan public addresses init/start/step/get/release are
`4234/4288/4839/5D4B/5DE0`; main/done are `5EDA/7050`.
All existing codec/TX public addresses and the precise NOP/loop/return
checkpoint are cross-checked with map, CDB, listings and image bytes.

### Coupled Association Request admission revalidation

The bounded [MAC-TX admission extension](MAC_TX.md) now uses an explicit
positive command-ID whitelist and an equivalent six-field reset in submit.
That follow-up saves89 production CODE bytes and68 instruction records
compared with the prior validated final-octet predicate. Only two layout
trials were needed: the whitelist alone exceeded the limit by10 bytes; the
allowed reset saved127 bytes without changing allocation or public state
semantics. The remaining linked modules have unchanged instruction
counts/extents. Scanner source/header/corpus and all28 genuine scenarios are
unchanged. Only mechanically coupled hash/address/object ledger values changed
in this proof. Private declaration coverage adds exactly one cached-command
local to the prior539 records; no prior record was removed. Other private
changes and all caller changes are relocated addresses, not new allocation.
The unchanged full private matcher now covers540 records;49 field records,20 public entries,
complete ordered listings,76 artifact negatives plus the missing-alias
negative,15-second simulator-process deadline and every budget/guard remain.
Both-board genuine runs still measure SP7A and confirmed stack unwind.

Revalidation used canonical `make -B -j1 BOARD=<board>
BUILD=build/mac-association-request-dev/<board> test-mac-tx test-mac-scan`,
for `generic` and `lg_esl29_rev03`, with immediate per-image listing snapshots
after each link. Both strict native corpora and both compositions' ASan/UBSan
executables pass for each definition. Exact sanitizer recipes and current
MAC-TX hashes are in its dedicated ledger. The unmodified full local matrix
was not repeated. These remain offline synthetic/linked/simulated results,
not association or a working radio adapter; that revision's117-byte CODE margin did not
authorize increased limits or a full-stack fit claim.

### Coupled Data Request admission revalidation

The subsequent explicit [Data Request whitelist](MAC_TX.md#bounded-data-request-admission-14-prerequisite)
adds19 CODE bytes/eight instructions to the shared transmitter, without changing
production allocation, fields, APIs, the scanner or its caller. At that revision
the scan image was32,670 CODE with98 bytes remaining. All540 private and54 caller
records remain: their changed hashes reflect relocated addresses only;
all49 field records are unchanged. The six whole ordered listings,20 public
entries, complete CODE,76 artifact negatives, genuine missing-alias negative
and all28 target scenarios retain their existing bounds and checks.
Both board definitions still measure SP7A and preserve upper IRAM/unwind.
Admission of another command in the shared transmitter does not make the
scanner submit it, release its lease early or implement association.

### Coupled control-staging revalidation

The [transmitter's ABI-preserving control staging](MAC_TX.md#genuine-linked-evidence-and-budgets)
reduces its CODE by3230 and persistent DATA by25 bytes, adding43 ordinary XDATA
bytes and removing its five-byte overlay. Scanner code, headers, all28 shared
scenarios and every other linked object remain unchanged. Per-field native/
SDCC layout assertions preserve the original public transmitter context;
there is no additional frame copy, address-space specialization or budget rise.

Both board definitions pass canonical `test-mac-tx` and `test-mac-scan` under
`build/mac-tx-storage/<board>/mac_tx` and
`build/mac-tx-storage/<board>/mac_scan`. All217 artifact negatives plus the
missing-alias negative, exact unwind, upper-IRAM guards and15-second process
limit remain enforced. Both native corpora also pass ASan/UBSan for each board.
The eight existing parser/cache regressions pass. The original scan execution
and manifest were separately reproduced with the original transmitter object;
new hashes were not accepted solely because a rebuilt image produced them.
These are host-tested, image-checked and simulated results, not a working
radio adapter, ISR-nesting proof or full-stack fit.

### Host-side proof parsing

The checker indexes complete CDB address/public-declaration records and listing
labels, retaining every duplicate and conflicting value. Address validation
still uses the shared `cdb_address` rules. Immutable parsed instructions,
ordered-listing metrics and indices use bounded caches keyed by the **complete
exact input text**: four CDB texts and twelve texts per listing cache. No image
or successful verification result is cached. Every call still validates CODE,
layout, ABI, storage, instruction bytes and required real calls; cheap public
map/CDB failures now precede expensive instruction traversal.

Using identical `480087f` linked artifacts and the unchanged76-negative corpus,
one positive check plus the negatives (including their duplicate-declaration
positive control,78 total `verify` calls) took **68.933 ->12.003 seconds** under
`cProfile`, a **5.74x** speedup with initially cold parse caches. This measures
host-side artifact checking only, not simulator or full-matrix latency.
Eight synthetic parser regressions additionally cover exact-text changes,
malformed/conflicting/identical records, label whitespace and duplicates,
ordered instruction mutations, immutability and bounded cache eviction.
At that parser-only revision, both complete board proofs executed all28
genuine scenarios, all76 artifact negatives and the missing-alias negative with unchanged guards and
15-second per-simulator deadline. The51 Make/artifact regressions also pass.
No firmware rebuild, hardware access or new dependency was needed.

## Interval consumer profile (`CC2530_MAC_LINK`)

`CC2530_MAC_LINK` (which requires `CC2530_MAC_OBSERVED`/`CC2530_MAC_INTERVAL`,
see `include/mac_link.h`) binds the same controller to the observed interval
owner `mac_tx_interval_t`. Without the flag the exact profile and its
compiled SDCC instructions and allocations are unchanged: both boards' standalone and
banked-join `.asm` files match the previous revision after removing comments and
source-line debug labels. Only line-number metadata moves.

The profile changes only what the time model requires:

| Item | Exact profile | Interval profile |
| --- | --- | --- |
| Owner/submit/release | `mac_tx_t`, `mac_tx_submit/release` | `mac_tx_interval_t`, `mac_tx_interval_submit/release` |
| ACTION_TX grant | one `mac_tx_step` with captured events | one `mac_tx_observed_step` with interval/BUSY/RETIRED input |
| OPENED stamp | captured receive-on edge | foreground observation after receive-on was confirmed, so the dwell window starts at or after the physical opening |
| BEACON | captured end inside `[start, start+dwell)` | ordered delivery on the scan channel between OPENED and CLOSED |
| CLOSED stamp | exactly `window_end` | loss-free drained watermark at or after `window_end` |

A beacon delivered after the dwell but before confirmed closure was still
received on the tuned channel with the beacon filter. Accepting it lengthens
the listening period and does not create a timing fact. An earlier
closure watermark is an adapter error, and the unchanged 1024-symbol grace still
reports a missing closure. The TX report must still echo the engine's
current foreground time. A Beacon Request never requests an ACK, so its
normal outcome is `UNACKNOWLEDGED` after `SENT_INTERVAL` and `RETIRED`.
Five bounded `BUSY_INTERVAL` results still move to the next channel.

`tests/test_mac_link_scan.c` drives the real observed interval engine, codecs,
beacon parser and candidate table with synthetic interval events. It covers
the ordinary two-channel scan with in-window, post-dwell and wrong-channel
beacons. It also covers an exact-boundary closure, early and missing closure, busy channels, RX
cancellation, lifetime cancellation of a pending attempt, a TX report whose
stamp differs from the engine step and scan reuse with a continuing DSN. Invalid input
is atomic. Every case runs at fine phases 0/1/256/511, both from epoch 0 and
across the 32-bit symbol wrap, natively and under ASan/UBSan. Eight targeted
link-profile mutations were killed: exact-equality closure, closure one symbol later,
unconditional closure, restored dwell filter, any-channel beacons, dropped
engine uncertainty, dropped report identity and a skipped release.
With the banked-join flags, SDCC 4.2.0 compiles the link profile to 8504
`BJ_BANK3` bytes, compared with 8602 for the exact profile. It also uses the same
4-byte data segment.

This is **host-tested and SDCC compile-checked** only. It is not an adapter
driver, a linked MCU image or a hardware observation. The link driver that
maps these actions to `mac_adapter` configure/open/close calls is still
separate #13/#14 work. Code `4b9b747` passed
[full Actions 36226384354](https://github.com/faronov/cc2530-zigbee/actions/runs/36226384354),
110/110 jobs. No hardware was accessed.

## Reproduction and integration

Run the canonical targets from the repository root:

```sh
make -j1 BOARD=generic BUILD=build/mac-scan-check test-mac-scan
make -j1 BOARD=lg_esl29_rev03 BUILD=build/mac-scan-lg-check test-mac-scan
```

They run the native corpus and genuine linked proof, preserving the order
`mac_frame -> mac_tx -> nwk_beacon -> nwk_candidates -> mac_scan -> mac_scan_test`.
Each link immediately snapshots all six listings as
`mac_scan_test.<module>.rst`; later shared-object links must not overwrite
the proof's inputs. Object extent checks additionally consume the six `.rel`
files. The common checks include this composition once per board definition
in `test-local`. All51 focused Make/artifact regressions pass.

The host-only metadata regressions are included in `make test-tools`, or run:

```sh
PYTHONPATH=tools python3 -B -m unittest test_mac_scan_metadata -q
```

For additional native sanitizer coverage:

```sh
for board in generic lg_esl29_rev03; do
    if [ "$board" = generic ]; then id=0; else id=1; fi
    out="build/mac-scan-dev/$board"
    mkdir -p "$out"
    cc -std=c99 -O1 -g -Wall -Wextra -Werror -pedantic \
        -fsanitize=address,undefined -fno-omit-frame-pointer \
        -DCC2530_HOST_TEST -Iinclude -DCC2530_BOARD="$id" -Itests \
        tests/test_mac_scan.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c \
        src/nwk_candidates.c src/mac_scan.c -o "$out/host-mac-scan-sanitized" || exit
    UBSAN_OPTIONS=halt_on_error=1 "$out/host-mac-scan-sanitized" || exit
done
python3 -B tools/check_repository.py
git diff --check
```

All board verifiers reject this controller's symbols/source records; the
test executable is never a board/upload/flash artifact. The unchanged
expensive full local matrix was not repeated.

## Remaining gates

#40 remains the real #13 adapter gate: captured-edge selection, freshness/
overwrite, epoch/fine phase, ordered event delivery, unified ownership and
physical acceptance. The published MAC Timer service is a reset-exclusive
**live counter foundation**, not a captured PHY-end API. Existing RX, TX and
time services cannot be chained in one reset epoch to implement these actions.
No live-counter read, CRC flag supplied by a test, simulated timestamp or
logical completion is physical evidence.

No tuning, RX window, CCA, transmission, PHY timing, FCS hardware behavior or
restoration was observed on devices. No devices/private data were accessed.
The real adapter and its confirmed saved-state/filter semantics remain
unimplemented in board firmware. Active-scan control does not establish a
complete normative PAN descriptor list, full parent selection, association,
membership, security, BDB commissioning or application interoperability.
BDB v3.0.1 errata/test-plan conformance follow-ups #16/#17, required ED join/rejoin/leave, parent keepalive,
ED Timeout, endpoint0 services and durable counters remain unchanged.
