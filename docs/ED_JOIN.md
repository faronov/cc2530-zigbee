# Bounded authenticated end-device integration

This is original C for a **host-exercised R22/BDB3.0.1 join**, not a networking
CC2530 firmware image. The synthetic coordinator and PHY adapter supply
explicit public identities, frames, CRC results and captured times. They do
not replace the production MAC controllers, key owner, CCM, HMAC, AES, counter,
journal or flash services with successful stubs.

## Selected configuration and boundaries

One awake end device associates directly with the centralized coordinator
and Trust Center (NWK address0). Provisioning explicitly binds the TC IEEE,
own IEEE, Extended PAN, PAN, channel and update ID, derives a unique
16+2-byte install-code key and supplies trusted initial counter floors.
EMPTY, degraded storage and persisted membership never automatically create
keys or authorize application traffic.

The coordinator's selected Beacon must have its actual extended MAC source;
Extended PAN is not substituted for its IEEE address. The supplied link cost
is an adapter fact1..3, not an invented RSSI conversion. This is one selected
network, not arbitrary network selection, router forwarding or trust-center
migration. A TC with Stack Compliance Revision below21 is not silently
accepted through BDB's legacy exception.
The descriptor's system-server discovery flags do not establish TC trust and
are not an extra BDB acceptance prerequisite; the revision selects the modern
exchange, and the actual bound-key proof must still complete (BDB10.2.5 p78).

The initial parent must advertise End Device Timeout Request keepalive
(parent-information bit1). Timeout enumeration1 requests two minutes;
the awake owner repeats the genuine secured request after30 seconds.
A MAC-poll-only parent is an explicit unsupported/failing configuration.
Persisted restart returns `BDB_JOIN_RECOVERY_REQUIRED`, not READY. Complete
secure-rejoin/recovery orchestration belongs to #27.

## Ownership and successful sequence

`ed_wire` extends, rather than weakens, the original codecs. It provides
bounded NWK Data/Command, APS unicast/broadcast Data, Command and full/short
ACK syntax plus real level5 ENC-MIC32 protection. Unsupported group,
fragmentation, source-route and extended layouts fail explicitly. Normalized
security-level bits follow the same pinned R22 rule as the original envelope.

[`security_keys`](SECURITY_KEYS.md) owns actual key selection, durable incoming
floors, outgoing allocations, two NWK slots and old/pending TC key lifecycle.
Every received NPDU crosses that owner; no caller-supplied authentication
boolean substitutes for crypto or persistence.

`bdb_join` runs this sequence:

1. Real `mac_scan`, candidate parsing/selection and confirmed restoration.
2. Real `mac_join` Request, extraction, Response and confirmed cleanup, all
   using the same `mac_tx` DSN/generation/IFS owner without reinitialization.
3. Persist the genuinely allocated short address; require a token-correlated
   adapter confirmation of installed PAN/channel/IEEE/short filters and awake RX.
4. Authenticate initial network-key Transport-Key before membership or announce.
5. Transmit Device_annce, then match the TC Node Descriptor response.
6. Send encrypted Request-Key under key A; authenticate Transport-Key B;
   send the actual selector03 HMAC in Verify-Key; authenticate successful
   Confirm-Key under B. Receipt of B alone never verifies it.
7. Negotiate ED Timeout and the selected parent keepalive method.
8. Actually transmit and quiesce the final `Mgmt_Permit_Joining_req` to FFFC,
   duration180 and TC_Significance1, then expose application readiness.

Step5 precedes the updated TC exchange, as BDB8.2 requires. The final broadcast
does not enable local child admission or claim receipt by every network device.
Logical completion, MAC ACK, APS ACK and physical quiescence remain distinct.
An ED sends its NWK broadcasts to the parent's MAC short address with MAC ACK
disabled, not to MAC FFFF as a router would (R22 3.6.5 pp383-384).
Failed commissioning demotes readiness and requests a real protected Leave
when possible. Keyless abandonment explicitly produces no frame. A physical
cleanup failure retains the lower owner in FAULT, never resets it to fake IDLE.

### Operational failure and foreground work

Commissioning abandonment is not the response to an ordinary READY failure.
A lost periodic Timeout Response or a quiescent MAC transmission failure
retries the parent query up to three attempts. Exhaustion demotes volatile
membership/application readiness, drains outstanding TX/ACK ownership and
finishes with `BDB_JOIN_PARENT_FAILED`, without constructing Leave or saving
LEFT. Other fatal operational errors likewise preserve durable key history
for explicit recovery; they do not automatically resume READY. Initial
commissioning failures and admitted local/TC address conflicts retain their
real Leave behavior. Authenticated remote Leave remains terminal.

A failed priority APS ACK with confirmed MAC release reports
`NWK_APS_ACK_FAILED` and its actual MAC outcome in `reply_result`. A quiescent
failed ZDO server reply reports `ZDO_RUNTIME_DROPPED` and its NWK/APS cause in
`response_result`. Neither failure destroys membership. Uncertain MAC FAULT,
crypto/storage failure and failed cancellation are not reclassified as these
ordinary losses. `nwk_aps_stop` prevents new admission, cancels ordinary TX
and the priority ACK, and clears `stopping` only after physical retirement.
It cannot turn a pending or faulted radio owner into IDLE.

After association, `BDB_JOIN_STALL_STEPS=4096` bounds consecutive step calls
at the same symbol time. Advancing the coherent symbol epoch resets this
local guard, so normal polling at approximately1 ms does not truncate the
10-second network-key or5-second TC deadlines. The4097th same-time call
latches `BDB_JOIN_WORK_LIMIT`, not a key timeout, and drains without Leave.
An issued, unconfirmed adapter INSTALL instead retains FAULT. Unrelated
events cannot bypass the guard or retirement; callers inspect actions even
when the event returns IGNORED. The100-second commissioning deadline and
the existing explicit scan, association, MAC and cleanup bounds remain.
No CPU frequency or new maximum ordinary polling rate is assumed.

## Bounded transport

`nwk_aps` has one outbound transaction, one priority ACK, one receive record
and eight non-evictable live duplicate entries. FULL is backpressure, not
successful delivery. A full receive/ACK slot rejects new input before a key
owner transition; consumers must drain it.
Admitted durable control transitions use that reserved receive slot rather
than being lost to a full ordinary APS duplicate table. Key-command replay
and outstanding-phase decisions remain with the real key owner.

Eight separate broadcast records hold source/NWK-sequence identities for both
local and received broadcasts. The caller supplies the established network
broadcast-delivery bound in symbols, not an assumed universal timeout; the
synthetic one-hop fixture supplies16 seconds. Cached duplicates and full-table
candidates can be discarded before crypto, without creating records or claiming
authentication. New received records are created only after actual admission.

Application endpoint/profile admission requires completed announcement,
verified TC key and final permit transmission. The BDB facade additionally
owns parent negotiation and only exposes send in its READY state. There is no
ZCL profile/interoperability claim: admitted application packets are delivered
as bounded opaque APS payloads.

APS ACK matching checks peer, endpoints, profile, cluster, format and counter.
An ACK before a genuine MAC SENT cannot complete an operation. An ACK while
cleanup is outstanding can be remembered but cannot replace QUIESCED.
Three APS retries retain transaction identity while rebuilding protection with
fresh durable security counters and a fresh NWK sequence.

The caller supplies `ack_wait` in16-us symbols. Its minimum93750-symbol base
is the selected profile2 maximum-depth term, not a measured cryptographic
latency; the caller must add its proven encryption/decryption allowance.
The synthetic fixture uses100000. The duplicate window is at least16 seconds
and covers four complete MAC lifetimes, cleanup bounds and ACK intervals.
An absolute transaction deadline also bounds delayed foreground service.
APS counter wrap observes a full window of
quarantine. These are finite lifetime assumptions, not proof against arbitrary
unbounded delayed packets after an eight-bit counter has been reused.
NWK commands do not consume an APS counter, so parent keepalive and Leave
remain available during APS wrap quarantine.

## Endpoint zero

`zdo_runtime` takes only packets admitted by the real transport. It supplies
one timed TC client transaction, one deferred server response and one
application delivery. Node Descriptor and single-device NWK/IEEE address
responses match peer, TSN, address, endpoint, profile and outstanding timeout.
Client requests use AR0 and bounded response retries rather than treating
queue admission as a response.

The server retains the original generic unsupported-unicast fallback and
broadcast/Parent_annce drop rules. Address requests add the specified matching
broadcast exception, addressed failures and childless extended response.
For packets admitted by the fixed-peer security owner, Device_annce updates
a four-entry address map, invalidates conflicting short bindings, and fails
closed on conflict with the fixed local/TC binding. Only NWK-source0 with the
bound TC's security identity reaches this path. Relayed third-party
announcements, including a third party claiming the local short address,
are rejected before map mutation; this is not general network-wide address
conflict detection.
It does not manufacture a replacement address or claim complete routing-side
conflict resolution. Optional discovery, binding and application services
remain outside this initial integration.

APS counter quarantine explicitly drops an unsendable server response with
`response_result=NWK_APS_EXHAUSTED`, rather than leaving a response queued
for the full duplicate window. Ordinary TX backpressure retains the one
deferred response, but does not block reception of client responses or
durable control events. A further server request while that response slot
is occupied is explicitly dropped with `NWK_APS_FULL`; it cannot overwrite
the retained response. The application still must drain its single delivery
slot. None of these results claims successful response delivery.

Authenticated Network Update is persisted by the key owner. During READY the
owner stops current traffic and requires a new real adapter-install confirmation
before resuming. It cancels an outstanding parent query without discarding
its physical TX completion, removes deferred old-configuration responses,
and rearms keepalive after successful reinstallation. An old query deadline
cannot make the reinstalled device leave. An update during initial commissioning requires recovery rather
than silently continuing with uninstalled network parameters.

## Evidence and remaining target gate

`make BOARD=... test-ed-integration` consists of `test-ed-wire`,
`test-security-keys` and `test-bdb-join`: strict native execution,
nonrecovering ASan/UBSan and SDCC compilation of the new production modules.
The combined host peripheral model shares live AES and flash state; only an
explicit modeled CPU power cycle resets services, retaining NV when requested.
The native flash-RAM entry uses the existing MMIO controller bridge; host
execution does not execute8051 opcodes. Genuine RAM-opcode execution remains
in the separate, unchanged target flash proofs.
The coordinator decrypts real transmissions, checks announcement ordering,
verifies HMAC03 before issuing Confirm-Key and checks the final permit payload.
The end-to-end negative corpus includes key and confirmation timeouts, real
Leave/quiet abandonment, unsupported parent keepalive, missing physical
quiescence, wrong-TSN/status/key responses, broadcast duplicate filtering and
PAN-update cancellation/reinstallation before application traffic can resume.
Network-key, TC-key and Confirm responses at the expired deadline cannot
complete commissioning. Exchange timers start with the request, not after
eventual MAC cleanup; a valid MIC does not extend a transaction deadline.
It also exercises all three APS retries, late/wrong/early ACKs, full receive
tables and real transmitted APS-counter wrap. Parent keepalive continues
during the wrap quarantine; counter0 is not reused before its expiry.
Operational-loss regressions include one/all three lost keepalives, transient
and sustained CCA failure, failed priority ACKs, four real MAC NO_ACK attempts
for a server response, AR0/AR1 requests during actual APS wrap, client reception
while a server response is deferred, and PAN update during a pending query.
Modeled resets verify that operational failures retain VERIFIED and permit
the actual protected Rejoin primitive with a larger security counter, whereas
genuine terminal Leave retains LEFT and rejects that primitive without output.
Fast-poll waits cross4096 calls and uint32 time wrap before accepting a valid
network/TC key. Stopped time, ignored events, unconfirmed INSTALL and missing
QUIESCED are separate negative cases. These checks do not implement #27's
full recovery procedure.
The runtime corrections at `4c19bd2` passed
[full Actions acceptance](https://github.com/faronov/cc2530-zigbee/actions/runs/36044641734):
56/56 successful jobs, including the78786-check native and nonrecovering
sanitizer BDB corpus on both boards and the unchanged original target suites.
This accepts the bounded runtime changes, not the still-missing whole-MCU
composition below.

The unchanged synthetic READY fixture begins with17 of64 per-power-epoch
erase attempts consumed. Sixteen successful30-second keepalive cycles fit;
the17th reaches the programmed NV quota at about510 seconds. Cleanup must
not attempt another NV write or Leave, and a modeled reset retains VERIFIED.
This is a software work/wear quota, not measured electrical endurance or
acceptable long-running availability. Persistence-frequency work is still
required; neither increasing quotas nor weakening durable replay/counter
ownership is implied by these fixes.

This evidence is **host-tested and SDCC compile-checked**, not a new linked-image,
alias-aware execution or physical observation. Existing linked-image and
simulator suites remain mandatory and unchanged; the old resident profile
does not prove that these new services fit or execute safely together on8051.
Whole-stack CODE/XDATA/IRAM placement, banked execution if required, complete
new ABI/alias proofs, a truthful real-radio adapter, entropy, electrical
durability and physical interoperability are still gates. #26 now owns the
remaining complete-target acceptance of the consolidated #24/#25/#26 scopes;
host READY alone cannot close it. #23's bounded implementation is accepted
separately through the genuine banked lifecycle below, not from host READY.

The earlier smaller key-owner/crypto/NV composition requested49,576 CODE bytes
against the unchanged32,768-byte unbanked limit and failed allocation of57
contiguous DATA bytes. The separate [banked security image](BANKED_SECURITY.md)
now resolves that bounded placement with real key lifecycle execution and
checked physical DATA ownership. It does not yet include this document's
complete MAC/NWK/APS/ZDO/BDB caller or prove whole-stack resource/stack fit.

### Complete-service allocation baseline

The next placement preparation compiles all28 genuine `ED_JOIN_SRC` modules
with the existing SDCC4.2.0 large-model flags, rather than inferring fit from
the key-only image. At the runtime-fix revision `4c19bd2`, their relocatable
objects contain132049 CSEG bytes,16 CONST bytes and6570 XSEG bytes, before
the caller, libc, startup, banking and placement. Summing module DATA/OSEG
is not a valid simultaneous-frame or linked-IRAM proof.

At `3888eae1871f42b02613456a9e57894bfeecf0fe`, top-level volatile parameter
and local pointer copies shorten compiler
temporary lifetimes, following the existing MAC/key-owner technique. They
do not make caller objects volatile, change pointer memory spaces, move
fields, introduce reentrancy or add an alternate protocol implementation.
SDCC requires the matching parameter qualifiers in declarations and
definitions. Actual individual object allocations change as follows:

| Module | CSEG before / after | XSEG before / after | DATA before / after | OSEG before / after |
| --- | ---: | ---: | ---: | ---: |
| `bdb_join` |13390 /16390|205 /208|102 /44|4 /0|
| `nwk_aps` |14174 /17278|298 /319|138 /35|9 /6|
| `zdo_runtime` |9556 /10575|220 /232|83 /17|6 /0|

This removes227 bytes of summed module DATA allocation, trading7123 CODE
and36 XDATA bytes, but **does not establish complete target fit**. SDCC
`sizeof` gives2743 bytes for `bdb_join_t`,168 for the separate MAC owner,
125 for a BDB event and126 for an action. That revision's production XSEG sum
is6606 bytes; the services plus only the BDB context and MAC owner already
need9517 bytes, exceeding the7680-byte ordinary region below status. Event,
action, caller configuration and libc storage are not included in that lower
bound. The key-only peak SP7B/7C also supplies no additional caller headroom.
That exact baseline revision passed
[Actions36046973052](https://github.com/faronov/cc2530-zigbee/actions/runs/36046973052),
56/56 successful jobs. The following three resource increments were then
published in `c97e32d4bf4126b78b645d5c7fc706a133238e1c` and accepted by
[Actions36059444790](https://github.com/faronov/cc2530-zigbee/actions/runs/36059444790),
56/56 successful jobs after a targeted rerun of the timed-out generic bringup
worker and dependent acceptance gate. No source, deadline or corpus changed
for the rerun. This accepts the resource reductions and existing key-only
image, **not a whole-MCU join**.

#26 therefore still needs explicit phase lifetimes, reduced/shared work
storage with an active-call ownership proof, balanced CODE banks and real
execution under the unchanged stack/alias limits. Merely enlarging a linker
allowance or allocating the IRAM alias as more RAM cannot make this
composition valid. These measurements are **SDCC object/size checks plus
unchanged host regressions**, not a linked-image or simulated MCU join.

### Phase-owned BDB storage

The first bounded #26 resource increment replaces simultaneous caller-owned scan,
association and runtime allocation with a **real C union in ordinary XDATA**.
It is not linker-area renaming, private compiler-scratch sharing or additional
IRAM. The context ABI is version2; public entry-point memory spaces and
foreground/nonreentrant calling conventions are unchanged. Callers inspect
`workspace` before reading `work.scan`, `work.association` or `work.runtime`.
An inactive member is not a readiness flag or a diagnostic record.

| Live storage | Entry and lifetime | Required handoff |
| --- | --- | --- |
| NONE | Fresh context or rejected runtime configuration; bytes are not a live controller | Real runtime initializers validate endpoint/profile, crypto/NV limits, ACK/broadcast bounds and descriptor before scan can acquire a lease |
| SCAN | Real `mac_scan_init/start/step`; a rejected scan start remains IDLE, while FAILED/FAULT retain the actual member | Copy scan outcome and selected parent, then require successful `mac_scan_release` before overwriting scan |
| ASSOCIATION | Initialize once after scan release; all retries reuse the same context and nested generations | Copy the **complete** `mac_join_record_t`, then require successful `mac_join_release` with physical restoration and idle unchanged MAC owner |
| RUNTIME | Initialize NWK/APS, ZDO and packet staging after association release and before durable short-address admission | Retained throughout INSTALL, key exchange, READY, PAN Update, draining, failure and fault; never reinitialized to recover an owner |

Initial runtime validation uses this as-yet-unleased union, admits no packet
or transaction and is discarded before scan starts. It does not leave an
initialized runtime hidden underneath the scan. Neither runtime initializer
resets the MAC owner or touches crypto/counters/NV. There are no callbacks or
ISR users at any handoff; the previous service has returned and its release
has succeeded before the next initializer is called. Configuration, owner
pointer, phase/tag, selected parent, scan summary and complete association
outcome live **outside** the union. The raw candidate table is available only
while SCAN is retained; its copied summary keeps generation, sent/unscanned
masks, reason, cleanup error, overflow, TX outcome and candidate count.
Faulted scan/association members are never replaced by runtime storage.

The original Response timestamp remains in `record.association.stamp`;
delayed restoration/runtime initialization cannot restart the network-key
deadline. Association rejection retries do not reset nested generations,
MAC DSN, generation or IFS. A flash-read failure during subsequent durable
association cannot publish INSTALL, membership or readiness, and a genuine
modeled reboot still sees PROVISIONED. This is not a secure-rejoin procedure.

Measured at the phase-only step with the unchanged SDCC4.2.0 large-model flags
(the subsequent wire reduction is recorded separately below):

| Allocation | `3888eae` | Phase-owned storage |
| --- | ---: | ---: |
| `sizeof(bdb_join_t)` |2743|1904|
| BDB object CSEG / XSEG / DATA / OSEG |16390 /208 /44 /0|17090 /221 /49 /0|
| All28 production objects CSEG / CONST / XSEG |139172 /16 /6606|139872 /16 /6619|
| Services + BDB +168-byte MAC owner, excluding other caller storage |9517|8691|
| Above +93-byte config,125-byte event,126-byte action |9861|9035|

The union is1518 bytes and the retained scan summary17 bytes. Net caller
saving is839 bytes; including the13-byte increase in BDB private compiler
storage saves826 ordinary-XDATA bytes. At that step all27 other production objects match
the baseline after excluding only the assembler's output-directory comment.
`tests/bdb_join_layout.c` compiles actual caller allocation objects and
asserts SDCC sizes and outside-union retained-field offsets in both existing
BDB host workers. It is **not linked or executable**, supplies no substitute
service and is not a banked target fixture.

The existing78786 host checks first passed unchanged apart from moved-field
access. The additional phase tests bring the corpus to79783 checks, passing
native and nonrecovering ASan/UBSan for both board definitions. They cover
both successful handoffs with byte-identical MAC ownership, retained outcome/
timestamp, early API rejection without crypto/NV work, no candidate, missing
scan/association restoration, terminal fault retention, actual refusal packets
across three attempts, refusal followed by successful authenticated join,
durable-association read failure and corrected never-leased start/cancellation.
They never write a production context to manufacture a phase or result.

**Whole-MCU acceptance remains unresolved.** The phase-only8691-byte lower bound
exceeds the7680-byte ordinary region by1011 bytes, before caller I/O, libc,
startup and banker scratch; the explicit caller objects raise the deficit
to1355 bytes. Module DATA sums are not a simultaneous active-frame proof.
No complete image, physical DATA reservation layout, full libc/active-lifetime
proof, new stack high-water, mixed MCU trace or full-join CI worker is claimed.
The key-only image still peaks at SP7B under the unchanged7C cap.
Remaining #26 work must reduce/prove returning scratch separately from
retained security/NV state, solve real banked call depth, place the actual
services and new synthetic caller, then add strict linked identities and
alias-aware AES/flash-RAM execution including failure/nonpublication cases.
No old verifier identity, component budget, case, simulator deadline or CI
worker is changed or removed by the phase-only increment. #27, #40/#45 and hardware
acceptance remain separate.

Local reproduction (isolated paths; no equipment or duplicate full matrix):

```sh
make -j1 BOARD=generic BUILD=build/stage2-phase/generic test-bdb-join
make -j1 BOARD=lg_esl29_rev03 BUILD=build/stage2-phase/lg test-bdb-join
make -j1 BOARD=generic BUILD=build/stage2-phase/generic \
  --eval 'stage2-objects: ; $(MAKE) -j1 $(patsubst src/%.c,$(BUILD)/ed-join/%.rel,$(ED_JOIN_SRC))' \
  stage2-objects
PYTHONPATH=tools python3 -B -m unittest test_ci_plan test_local_checks
```

### Returning wire-work reduction

The next implemented reduction uses actual C unions for sequential
`ed_wire` encode/decode/crypt scratch. Header/frame occupy one116-byte region;
encoded payload/decoded packet/CCM output occupy a separate120-byte region.
Metadata remains independently live. Private nested readers cannot wipe
their caller's buffers, while every public return wipes all three named work
objects. No retained counter, key history or journal state is overlaid.
See the [complete lifetime, ABI and execution proof](BANKED_SECURITY.md#returning-wire-work-ownership).

| Allocation | Phase-only step | Phase + returning wire work |
| --- | ---: | ---: |
| Ordinary `ed_wire` CSEG / XSEG / DATA / OSEG |6837 /797 /9 /4|7181 /466 /9 /4|
| All28 production objects CSEG / CONST / XSEG |139872 /16 /6619|140216 /16 /6288|
| Services +1904-byte BDB +168-byte MAC, excluding other caller storage |8691|8360|
| Above +93-byte config,125-byte event,126-byte action |9035|8704|

Both-board genuine SDCC object measurements agree. All26 modules other than
BDB and wire still match `3888eae` after excluding only the output-directory
comment. Combined ordinary-XDATA saving from that revision is1157 bytes:
826 from phase storage plus331 from wire work. This is **not enough**:
the new lower bound exceeds ordinary RAM by680 bytes, or1024 with explicit
caller I/O/configuration, before libc/startup/banker scratch. No extra pool,
reserved-status overlap or use of the IRAM alias is introduced.

The existing banked key profile changes only as required by the real wire
implementation and has been fully revalidated locally on both boards:
49261 populated CODE bytes,3555 ordinary XDATA, unchanged physical DATA/
bit/OSEG reservations, complete20-byte libc suffix, and actual peak SP7B
under7C. Its original22 operations,128 AES calls,532 flash-RAM commands and
retained busy fail-stop remain. Every extra CODE/unowned/wipe byte is included
in the full246324 artifact/10354 outcome mutations; no old class is dropped.
This is key-only **image-checked and simulated** evidence, not full MCU join.
The native/nonrecovering sanitizer corpora pass on both boards with49119
wire checks (1590 added interleaving/error checks),9490 key checks and79783
BDB checks. No fixture-side success result or production-state overwrite
manufactures these outcomes.

Reproduction, each with a distinct build directory:

```sh
make -j1 BOARD=generic BUILD=build/stage2-wire-host/generic test-ed-integration
make -j1 BOARD=lg_esl29_rev03 BUILD=build/stage2-wire-host/lg test-ed-integration
make -j1 BOARD=generic BUILD=build/stage2-wire/generic test-banked-security
make -j1 BOARD=lg_esl29_rev03 BUILD=build/stage2-wire/lg test-banked-security
make -j1 BOARD=generic BUILD=build/stage2-wire-host/generic \
  --eval 'stage2-objects: ; $(MAKE) -j1 $(patsubst src/%.c,$(BUILD)/ed-join/%.rel,$(ED_JOIN_SRC))' \
  stage2-objects
make -j1 BOARD=lg_esl29_rev03 BUILD=build/stage2-wire-host/lg \
  --eval 'stage2-objects: ; $(MAKE) -j1 $(patsubst src/%.c,$(BUILD)/ed-join/%.rel,$(ED_JOIN_SRC))' \
  stage2-objects
python3 -B tools/host_coverage.py --board generic --output build/stage2-wire-coverage/generic
PYTHONPATH=tools python3 -B -m unittest test_banked_security test_ci_plan test_local_checks
python3 -B tools/check_repository.py
git diff --check
```

The remaining #26 work is concrete: remove the remaining ordinary-XDATA
deficit without discarding retained state; establish physical DATA/libc
ownership for the full active caller graph; solve real key-operation caller
depth without raising SP7C; place/link the complete28-service composition;
then add a separate real-wire synthetic MCU caller, strict whole-image/
failure-state replay and both-board CI consumers. None is supplied by
successful linkage of the smaller key image or native BDB READY. Stages3/4,
#27, #40/#45 and all physical acceptance remain outside this increment.

### Occupancy-owned runtime slots and tagged I/O

The next reduction removes redundant runtime/caller buffers without sharing
retained key/counter/journal state or adding a generic scratch pool.

| Storage | Admission and active lifetime | Publication / retirement |
| --- | --- | --- |
| NWK/APS `incoming` | Reused as receive scratch **only** when neither a packet nor priority ACK is pending; full capacity rejects before key-owner work | Actual authentication and durable admission run first; transport checks then set `receive_ready`. Ignored ACKs/endpoints, duplicates, table exhaustion and failures clear unoccupied scratch. A pending packet is never cleared |
| NWK/APS `mac[125]` | Real key services produce at most116 NPDU bytes at offset9; only an idle, unleased transport enters transmit construction | The real MAC codec emits a zero-payload short/short compressed header into the separate9-byte prefix; its returned size must be9 before submitting all bytes to the real MAC owner |
| ZDO `application` | Receive scratch only while `application_ready` is clear; incoming transport packet remains separately owned until taken | Application reception sets the flag; all other consumed input is cleared before returning, including FORMAT/error/control results. Taking the application copies then clears it |
| ZDO commissioning constructor | An unoccupied application slot constructs Device_annce or the final permit broadcast; `nwk_aps_queue` must really admit/copy it | Returning scratch is cleared on queue success or failure. BDB still owns actual TX confirmation and readiness; a message selector is not an authentication-success input |
| BDB event/action `data` | A real union selected by outer `kind`; only one scan/association/TX/install payload is active | Epoch/token/tag remain outside the union. The synthetic driver finishes the real MAC step before reusing its TX-event bytes as a scan completion |

The MAC prefix is the existing codec's3+4+2-byte layout, not a new framing
rule. No overlapping payload/output is passed to a codec. Header emission
cannot touch the already-produced NPDU, and a compile-time extent check
requires9+116=125. Subsequent priority ACKs cannot overwrite an active MAC
owner: existing `active`, release and QUIESCED rules still govern transmission.
All operations remain synchronous, serialized foreground calls with disjoint
complete caller objects; no ISR/callback/reentrant user can observe or retain
the intermediate scratch span.

ZDO server response storage remains independent of both its application slot
and the transport's queued outgoing packet. A deferred response does not
block a client/control receive, and another request cannot overwrite it.
Before initial BDB readiness, transport admission blocks application delivery,
so the commissioning constructor can use the empty application slot without
discarding a pending application or adding another packet to BDB.
It preserves the original Device_annce/permit contents, TSN allocation,
confirmation owner and NV frequency. Both runtime context versions are now2;
BDB's new version2 ABI also includes the `event.data`/`action.data` unions.
No on-flash schema changes.

Actual SDCC4.2.0 large-model measurements, identical for both board definitions:

| Allocation | Phase + wire step | With occupancy/tagged slots |
| --- | ---: | ---: |
| `sizeof(nwk_aps_t)` |939|703|
| `sizeof(zdo_runtime_t)` |459|339|
| `sizeof(bdb_join_work_t)` / `sizeof(bdb_join_t)` |1518 /1904|1042 /1428|
| BDB event / action |125 /126|55 /51|
| NWK/APS object CSEG / XSEG / DATA / OSEG |17278 /319 /35 /6|17367 /320 /35 /6|
| ZDO runtime object CSEG / XSEG / DATA / OSEG |10575 /232 /17 /0|11473 /246 /20 /0|
| BDB object CSEG / XSEG / DATA / OSEG |17090 /221 /49 /0|16272 /213 /46 /0|
| All28 objects CSEG / CONST / XSEG |140216 /16 /6288|140385 /16 /6295|
| Services + BDB +168-byte MAC |8360|7891|
| Above +93-byte config and event/action |8704|8090|

The additional saving is614 ordinary-XDATA bytes:476 in BDB's live runtime,
145 in caller I/O, minus7 additional compiler bytes. The complete saving from
`3888eae`, with those explicit caller objects, is1771 bytes. All24 production
objects other than BDB, NWK/APS, ZDO runtime and wire still match that baseline
after excluding only their output-directory comment. The compile-only
allocation regression now allocates1795 bytes and checks actual context,
payload-union and outer-tag offsets; it is not a target executable.

The prior79783 BDB checks passed unchanged before adding1939 checks, bringing
the both-board native/nonrecovering sanitizer corpus to81722. New regressions
preserve two simultaneously pending application/transport packets, reject
FULL without crypto/counter consumption and then accept the **same** retried
wire frame, check priority-ACK/duplicate/nonpublication cleanup, reject bad
MICs and wrong endpoints, clear malformed endpoint-zero scratch, preserve
pending client/server bytes, and reject a real durable receive failure.
Invalid commissioning selectors, null arguments and half-range time jumps
are rejected without acquiring transport or consuming a TSN.
The genuine persisted reset boundary still retains VERIFIED, not automatic
READY. Maximum125-byte MAC frames execute with82-byte NWK-only or65-byte
NWK+APS protected application payloads; one extra protected byte fails
without a MAC lease, frame publication or a hidden success result.

Fresh both-board key-image links retain **exactly** the complete immutable
identities checked and replayed in the preceding wire step. No key-image hash,
case, budget, stack limit or simulator deadline is changed by this slot step.
The slot changes themselves are **host-tested and SDCC object-checked**, not
yet part of an executed complete MCU image. The remaining ordinary-RAM
deficit is211 bytes before caller I/O, or410 with configuration/event/action,
before libc/startup/banker scratch. Full active-call DATA/libc ownership and
real complete-caller stack depth under7C are still unresolved.

Local reproduction, without repeating the accepted old full matrix:

```sh
make -j1 BOARD=generic BUILD=build/stage2-slots/generic test-ed-integration
make -j1 BOARD=lg_esl29_rev03 BUILD=build/stage2-slots/lg test-ed-integration
make -j1 BOARD=generic BUILD=build/stage2-slots/generic \
  --eval 'stage2-objects: ; $(MAKE) -j1 $(patsubst src/%.c,$(BUILD)/ed-join/%.rel,$(ED_JOIN_SRC))' \
  stage2-objects
make -j1 BOARD=lg_esl29_rev03 BUILD=build/stage2-slots/lg \
  --eval 'stage2-objects: ; $(MAKE) -j1 $(patsubst src/%.c,$(BUILD)/ed-join/%.rel,$(ED_JOIN_SRC))' \
  stage2-objects
make -j1 BOARD=generic BUILD=build/stage2-slots-image/generic \
  build/stage2-slots-image/generic/banked-security/banked_security.ihx
make -j1 BOARD=lg_esl29_rev03 BUILD=build/stage2-slots-image/lg \
  build/stage2-slots-image/lg/banked-security/banked_security.ihx
PYTHONPATH=tools:tests python3 -B -c 'from pathlib import Path; import verify_banked_security as v; [v.verify(*v.load(Path("build/stage2-slots-image")/b)) for b in ("generic", "lg")]'
python3 -B tools/host_coverage.py --board generic --output build/stage2-slots-coverage/generic
PYTHONPATH=tools python3 -B -m unittest test_banked_security test_banked_image test_host_coverage test_ci_plan test_local_checks
python3 -B tools/check_repository.py
git diff --check
```

All fixture identities and key material are deliberately public synthetic
values. No key-bearing firmware/NV dumps or captures are uploaded by these
CI jobs. No equipment access, flashing or RF tests are performed.
