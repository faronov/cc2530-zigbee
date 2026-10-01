# Bounded authenticated end-device integration

This is original C for a **host-exercised and banked8051-simulated
R22/BDB3.0.1 join**, not a networking firmware release. A separately authorized
LG board trial now exercises real discovery, but has not joined HA.
The synthetic coordinator and PHY adapter supply
explicit public identities, frames, CRC results and captured times. They do
not replace the production MAC controllers, key owner, CCM, HMAC, AES, counter,
journal or flash services with successful stubs.

## Ordinary default-key discovery

The explicit `JOIN_SMOKE_KEY_MODE=default-tc` profile enables
`CC2530_DEFAULT_TC_KEY`. The default build remains `install-code`; changing
the mode refreshes a board/mode dependency before compiling the caller,
host references and image. The two profiles have separate immutable pins.
This is an explicit commissioning choice, never fallback from a failed
install code, failed authentication or existing NV.

The caller opens an empty key owner, then performs a real active scan.
The supplied channel mask can cover all channels11-26; it does not need
the target channel. The initial OFF channel is only a restoration snapshot.
Channel/PAN/Extended PAN/Update ID and coordinator addressing come from the
actual eligible beacon. The current bounded policy selects direct coordinator
beacons only, including short address0000. Distinct eligible open networks,
collector overflow or unscanned channels fail rather than silently choosing
another network. Link cost remains explicit uncalibrated caller policy;
this is not full primary/secondary steering or router-parent support.
Neither an open beacon nor the publicly shared initial key proves that the
network belongs to the intended HA installation.

After contextual Association Response and genuine take/release, the key owner
is provisioned with the discovered network and the observed source IEEE.
A short-selected coordinator still reports `SOURCE_UNBOUND`: learning an
address is not authentication. Initial protection uses the16 ASCII bytes of
the public `ZigBeeAlliance09` key. The Network Key must arrive in a genuinely
authenticated/decrypted Transport Key, followed by the existing durable
counters, Device_annce, updated TC key/Verify/Confirm and parent negotiation.
No host or debugger supplies a Network Key or successful security state.
Existing persisted state is refused; secure restart/rejoin is still separate.

The LG native/sanitizer and shallow/deep caller runs pass the original ten
scenarios plus all16-channel discovery, two open networks, closed admission,
candidate overflow and an Association Response IEEE inconsistent with the
later protected Transport Key. The all-channel success performs24 actual
modeled transmissions and2645 polls with the one-latch sampler, starting on idle channel11 rather than
the peer's channel15; rejected discovery writes no NV.
The first LG linked discovery candidate used7640/7680 ordinary XDATA and243770 populated CODE;
the independent DATA and static-stack checks retain SP7C and bank depth8.
That exact default-profile image also completed601 actual MCU observations,
156100 peripheral stops and authenticated READY at observedSP7B/7C.
Its external peer now saves/restores the idle DUT's AES owner, descriptors,
buffers, peripheral model, registers and accounting around peer cryptography.
Otherwise constructing the first Transport Key incorrectly changed a cold
DUT's ENCCS from08 to48 before its first command. The caller regression checks
the unchanged AES owner/control/accounting around every peer advance; no
private target state is injected to hide the discrepancy.
This is **host-tested, image-checked and simulated**, not a physical HA join
or acceptance of subsequent radio corrections. Physical timing, flash/AES,
RF delivery and full ZHA interview remain separate; Active Endpoints and
Simple Descriptor replies are not supplied by this endpoint-zero caller.

### 2026-09-28 LG physical discovery trials

The operator explicitly authorized erase/program/run and ordinary HA admission,
without preserving the old main flash. Each image was privately packed from
its immutable linked identity, programmed with the reviewed no-run guard,
externally verified, then independently read back through the guarded debugger,
including every FF gap. A genuine full reset, DMA-enable transition26->22,
boot-disarmed checkpoint and public ARM/RUN admission preceded uninterrupted RF
execution. Inputs contained the factory own IEEE and OS-generated backoff draws,
but no target channel/PAN/Extended PAN/TC identity, install code or Network Key.
The initial restoration channel was11 and the scan mask covered11-26.
The one-latch-sampler trials below continued on2026-09-29 UTC.

| Image IHX SHA-256 | Hardware-observed result |
| --- | --- |
| `1fed3af96da8e2253f6776411678bd79ebf7d67aedd26fc1a4e1e763979c243b` | Scanned11-15 and retained three beacons belonging to the intended HA network, all router rather than direct coordinator candidates. Channel16 hit CCA_BUSY followed by radio-stop TIMEOUT. |
| `840c1c61ffafb0fc3264cc7949437c737fea4e5fc9e830a08c121783b0a0a138` | Reached channel15, then rejected a queued29-byte frame: RXFIRST=7D and raw RXLAST=9A. The latter's reserved high bit was incorrectly treated as an error instead of using the documented7-bit offset. |
| `f0d1e7164d686f352149eb8428e76a1ad3d424ac9a3396c0f75cf109274da9ac` | With pointer masking, reached channel18. CCA_BUSY still could not soft-stop RSSI-only RX: RXENABLE=0, RX_ACTIVE=1, empty FIFO, RX_MODE11 retained, no fresh RFIDLE, TIMEOUT after1029 raw ticks/80 polls. |
| `b216a4fb8394734804366ebbecbaedc8e11d1890ec59ba8055b61538889ba6c4` | With guarded ISRFOFF, completed all16 channels: sent mask07FFF800, unscanned0, overflow0 and no radio fault. The retained scan result had zero eligible candidates, so BDB stopped with NO_PARENT before association. |
| Same `b216a4fb...` image, fresh reset/readback/admission | Repeated with ordinary whole-network ZHA permit-join rather than coordinator-targeted admission. Again completed11-26 with no unscanned channel, overflow or radio fault. Retained two permitting, capacity-available Pro-profile router candidates on15, sharing one Extended PAN, with depths2/4, nonzero short addresses and PAN-coordinator flags clear. The direct-coordinator-only policy correctly returned NO_PARENT. |
| `449f4915950def09abe919e8e8493bcbfb7b0a3813f52309aadf6c491101c95e` | First D5 run reached channel15, then retained MAC_RADIO_TIMER_ERROR. The timer rejected its entry preflight with MAC_TIME_UNSUPPORTED_STATE, phase1/polls0. The exact failed guard is not retained; this does not demonstrate counter corruption. |
| Same `449f4915...` image, fresh reset/readback/admission | Second D5 run completed all16 channels, retained one eligible direct coordinator and transmitted an Association Request. A CRC-good three-byte ACK matched its DSN, but the conservative interval did not prove timely arrival. MAC_JOIN_TIMING_UNCERTAIN stopped association at the request stage, before Association Response extraction or key provisioning. |
| `3676af5409917a2cfa9dfbecf18d1baab0e38831e7e414b8ca36e48a4c3aeedb` | First one-latch-sampler run completed all16 channels without a radio fault, but candidate overflow forced NO_PARENT. The four retained candidates were one direct coordinator and three permitting routers on15 in one Extended PAN; overflow means this is not the complete discovered set. No association timing result follows from this run. |
| Same `3676af54...` image, fresh reset/readback/admission | With permit-join opened only on the HA coordinator, reached channel15, then retained MAC_RADIO_TIMER_ERROR. MAC_TIME_UNSUPPORTED_STATE occurred during the first preflight poll: phase1/polls1, with previously observed control13/select0/IRQ flags07. No association occurred. The failed guard's actual register/value is not retained. |
| `0150118e145c4d65e5389ff3f644f324857f17725980f92913d1cab2d01e8575` | First guard-diagnostic run completed all16 channels with no radio fault or unscanned channel, but overflowed the four-entry candidate collector and returned NO_PARENT. Timer guard/value remained0/0. The ZHA permit-join helper accepted a coordinator-addressed request without confirming its effective scope; this is not coordinator-only admission evidence. |
| Same `0150118e...` image, fresh reset/readback/admission | With explicit coordinator-only `zha.permit`, reached channel15 and retained MAC_RADIO_TIMER_ERROR. The original failed read is now preserved: MAC_TIME_UNSUPPORTED_STATE, phase1/polls0, guard14/value04, identifying RFERRF.RXOVERF. One candidate had been collected; no association occurred. This proves RX FIFO overflow in this run, not a Timer2 counter/latch failure. |

The third observation disproves the proposed *soft-stop-before-mode-change*
repair as a physical solution. The revised busy path uses ISRFOFF only after
proving its exclusive RX_MODE11/AUTOACK-off profile, failed own CCA, no TX,
SFD or FIFO, and cleared old RFIDLE. It still requires observed fresh idle
before changing mode and restarting RX; it is neither an unconditional abort
nor a fallback after timeout. Normal RX/ACK/frame closure remains soft and
loss-preserving. The fourth run completed the full scan under this revision,
but its terminal snapshot is not an individual-strobe trace proving which
CCA attempts exercised ISRFOFF. The final retained CRC-good43-byte beacon
had protocol ID3 and a26-byte upper payload, not a supported Zigbee beacon;
the real production codecs reject it without exposing its identity.
Zero eligible candidates does not establish absence of RF traffic or prove
that the HA coordinator is out of range.

The fourth image uses244269 populated CODE bytes,7640/7680 ordinary XDATA
and unchanged SP7C/bank-depth8 static limits. Its updated lower LG corpora
passed33 standalone and76 handoff MCU sequences, with79097/87794 artifact
negatives plus alias controls. The banked busy-CCA case passed380 calls,
27433 MMIO events and SP78/7C. These are host/image/simulator checks of the
revised paths, separate from the physical scan and the first image's complete
authenticated simulated join.

The fifth run distinguishes the current parent-policy limitation from the
earlier radio fault: the existing caller cannot join through a router.
No eligible direct-coordinator beacon was retained. This does not prove its
physical absence or RF range. The later D5 run did discover a direct
coordinator; router-parent joining still requires separate TC-identity learning
and security integration, not removing a selection predicate alone.

All five runs used the original raw TXPOWER05 profile, characterized by TI
as typical -22 dBm, not measured LG output. The operator then explicitly
authorized a D5 (+1 dBm reference) trial before further router-parent work.
The new caller selects D5 itself, through checked radio/MAC initialization
and reconfiguration; there is no debugger register override. D5 retains
full-byte readback, power bytes other than05/D5 remain rejected, and it does not
improve receiver sensitivity. The D5 image, IHX
`449f4915950def09abe919e8e8493bcbfb7b0a3813f52309aadf6c491101c95e`,
uses244336 populated CODE and7640 ordinary XDATA with SP7C/bank depth8.
It is host/image-checked with selected lower MCU power paths; the two D5
physical results are recorded above. D5 is a checked firmware configuration,
not a calibrated measurement of output power.

The second D5 run retained pre-strobe, TX-observation and RX-observation live
Timer2 tuples. Relative to the pre-strobe sample, TX was observed at45007
fine ticks and RX at67457; the19-byte request gives a conservative TX lower
bound of27648 ticks. RX upper minus TX lower is39809 ticks, exceeding the
54-symbol ACK window of27648 ticks. This is measurement uncertainty, not
proof of an actually late ACK. The initial TX outcome is TIMING_UNCERTAIN;
the later cleanup_error=MAC_JOIN_TX_ERROR is secondary, not a received
Association Response error. No ACK deadline is extended and a matching DSN
does not bypass timing or security.

The next software revision reuses the existing active-scope one-latch reader
for attempts in the HANDOFF profile. Full begin/end validation, whole-FF
discard, radio-owned timeout/work checks, conservative live intervals and
retained faults remain. It removes redundant per-sample deadline setup, not
checks on when an ACK arrived. The standalone non-HANDOFF sampler is unchanged.
The caller's finite service work cap is512 rather than256 polls: with less
work per iteration, the original fine-grained synthetic clock exhausted256
polls during a legitimate transmission. The maximum125-byte D5 attempt is
covered under512; the1024-raw-tick service deadline and54-symbol ACK window
are unchanged. All15 default-key caller cases pass native/sanitizer and
shallow/deep comparisons. The two physical runs of this revision stopped
during discovery, so neither establishes improved physical ACK timing.
The new LG default-key IHX is
`3676af5409917a2cfa9dfbecf18d1baab0e38831e7e414b8ca36e48a4c3aeedb`:
244110 populated CODE and7632 ordinary XDATA. Its complete immutable image,
DATA/private-prefix and static SP7C/bank-depth8 checks pass. Generic and LG
install-code images are separately admitted, not aliases of this profile.
Selected lower MCU and banked-adapter ACK/handoff runs remain distinct from
a complete current-image MCU join replay, which has not been rerun.

The ninth trial's timer failure is distinct from a demonstrated counter/latch error:
the initial observation passed and the following preflight observation
rejected an unsupported state before a live Timer2 tuple was published.
The last radio diagnostic had a105-byte FIFO observation and no recorded
RF error, but it predates the failing timer guard and cannot establish that
guard's cause. That image lacked firmware-retained first-failure observations;
the reviewed debugger does not permit arbitrary peripheral reads or banked
breakpoints, and no private-instruction bypass was used.

The diagnostic-only image appends `mac_time.guard/guard_value`,
preserving the original failed read without rereading a peripheral.
LG default-key IHX
`0150118e145c4d65e5389ff3f644f324857f17725980f92913d1cab2d01e8575`
uses244361 populated CODE and7634/7680 ordinary XDATA (46 bytes free).
Actual linked DATA and static SP7C/bank-depth8 bounds are unchanged; the
volatile staging byte does not add another byte beyond the two diagnostic
fields in this emission. No timer/radio rejection, ACK window or recovery
policy changes. All original timer traces keep their MMIO order/values;
102 timer and54 co-owned-radio actual-MCU sequences verify the diagnostic,
including a fault first appearing in the second preflight observation.
All15 default-key caller scenarios and install-code0/10 pass native and
nonrecovering sanitizer checks. Those offline checks establish diagnostic
behavior, not a successful physical join or a physical RX-overflow diagnosis.

The second physical run of this diagnostic image supplies that missing
evidence: the initial preflight read of RFERRF returned exactly04
(RXOVERF), with guard14 and phase1/polls0. The earlier guards, including
FSMSTAT0, passed. Control/select/IRQ diagnostic bytes remained zero because
the failing observation never reached their reads, not because the timer was
observed stopped. The preceding successful radio operation retained FIFO
count58 and errors0; these older values do not contradict a later overflow.
The original RFERRF byte, not a debugger peripheral reread, identifies the
failure. This supports the overflow hypothesis for the earlier unlocalized
failures but does not retrospectively prove their exact failed guards.

RX FIFO overflow and candidate-table overflow are separate failures. The
former loses receive coverage and currently becomes a retained timer error;
the latter reports more candidates than the four-entry collector can retain.
Neither may be cleared or ignored to claim a complete loss-free scan or a
timely ACK. RX service-latency reduction and explicit loss/recovery semantics
remain unimplemented; neither diagnostic trial reached association, so the
one-latch sampler's physical ACK-timing improvement remains unproved.

Only the seventh trial reached Association Request; all eleven
stopped before durable network provisioning or authenticated membership.
HA admission was explicitly closed, private full
RAM/CPU evidence retained, and a genuine full reset/halt observed after each
failure. A fault checkpoint alone does not establish radio-off. Captures,
factory/network identities and programming logs remain outside Git/CI.
The filtered HA core journal contained no matching ZHA join/interview entry;
that filter result is not proof that no frames reached HA.

## Complete banked MCU execution

The complete `banked_join` composition now executes the real MAC scan,
parent selection, association, NWK/APS, ZDO, BDB, key owner, CCM/HMAC/AES,
durable counter, journal and flash-RAM services together. The synthetic PHY
and coordinator supply external events and public packets only; they never
write expected controller state or supply authentication-success flags.

The measured image has145994 populated CODE bytes on generic and145995
on LG:28760/28761 common bytes and four bank windows containing28126,
28906,29755 and30447 bytes. Ordinary XDATA is7546/7680, leaving134 bytes
before the status block. `zdo_runtime` embeds the board's Basic model
string, so each board pins its own six complete artifact identities.
The actual physical DATA reservations are08..1D and26..45, banking state
1E..1F, bit storage20..25, compiler overlay46..4F and stack50..7C.
Compiler spills are split into62 per-function `JF_<module>_<function>`
frames, then placed inside those reservations as described below.
XDATA1F00..1FFF still aliases IRAM; no allocation boundary or SP7C limit
was increased.

The full target caller allocates an actual1290-byte association phase:
645-byte live controller plus645-byte error-atomic shadow. Its statically
linked shadow is `fixture_device+645`, not additional independent RAM or a
borrowed stack buffer. Successful association take/release precedes runtime
reuse; faults retain their live phase. Splitting BDB initialization and
NWK transmission into translation units permits reviewed DATA placement.
Volatile scalar/pointer copies shorten emitted register-save lifetimes in
MAC decoding, CCM, counter reservation and journal replacement; they do not
change wire rules, counter floors, NV schema or persistence frequency.

The linked proof decodes every source/runtime instruction, checks far
entries/returns and analyzes285 functions with1882 live-byte/callee-write
pairs, including OSEG and transitive libc scratch. Raw CDB, complete
NoICE-checked symbols, memory report, ordered relocated listings,
relocatable objects and all CODE addresses/bytes are pinned per board before
execution. The complete image corruption campaign passes729989 (generic)
and729994 (LG) mutations.

The native and nonrecovering-sanitizer transcript contains415 public calls
and2791 real modeled AES/flash events. The generic MCU run executes all415:
READY after scan/association, authenticated network-key receipt,
Device_annce, the updated TC exchange/HMAC03/Confirm, parent negotiation,
and final permit broadcast/quiescence; then protected TX with APS retry,
RX/replay rejection, endpoint-zero address and unsupported replies,
authenticated PAN update/reinstallation, operational retirement, and
cold-restart denial of automatic READY. Separate cases cover missing keys
and retained radio fault. Observed peak isSP7B under the unchanged7C cap.
The added nested Device_annce flash-failure case retains the actual RAM
fail-stop even after later idle: authenticated membership is distinct from
READY, and no completed caller status or new NV image is published.

Each continuation preserves and rechecks complete CPU, IRAM/alias, XDATA,
peripheral and physical-flash state. Binary simulator dumps preserve every
observed byte; batching retains an observation after every public call.
The15-second subprocess deadline is unchanged. CI has twenty-two independent
fresh-reset scenarios per board, each under the existing15-minute worker
limit; no key/NV/image artifact from this fixture is uploaded.

Reproduce with `make BOARD=generic test-banked-join`, or a bounded
`test-banked-join-success`, `-missing-key`, `-radio-fault`, `-flash-fault`
or one of the edge targets below. A runner `--limit` is explicitly a development
prefix, never full acceptance. Full CI for the initial and expanded #26
transport/endpoint-zero corpora is accepted below. Real-radio/timing,
entropy, electrical NV durability and secure restart/rejoin remain separate
gates. The earlier allocation sections below record successive historical
steps, not the current complete-image size.

The same compiler-lifetime changes refresh the original MAC/NWK and
NV/counter/crypto/resident/key-only images, rather than replacing them with
the new complete caller. Both-board artifacts and original corpora remain
required. The lower local executions pass except the final combined
[resident-counter deadline](SECURITY_RESIDENT.md#execution-and-resource-contract):
its full corpus executed atSP75 and its exhaustive artifact proof passed
separately, but the combined run twice exceeded900 seconds. Full Actions
then resolved this component gate without shrinking the corpus or raising
the15-minute deadline: both hosted counter workers passed in
[Actions36122466601](https://github.com/faronov/cc2530-zigbee/actions/runs/36122466601).
All eight complete-join workers also passed, including success in9m05s
on generic and8m05s on LG. The overall run failed only on stale
`protocol_budget` compiler identities in both debug workers. The corrected
revision `08672c5a99265a583cba27618c1395cb9a992d84` then passed
[Actions36124292416](https://github.com/faronov/cc2530-zigbee/actions/runs/36124292416):
all62 workers and both control jobs succeeded, including Required offline
acceptance. This accepts the initial143092-byte complete-image corpus, not
the expanded edge corpus below.

### Whole-target edge corpus

Eighteen additional native/nonrecovering-sanitizer references contain3232
public calls and18703 actual modeled AES/flash events. Their raw stdout
identities, exact call/event counts and scenario names are pinned before
parsing. The original415-call/2791-event transcript remains byte-identical.
Each added target starts from genuine reset/provisioning/scan/association,
not an uploaded READY context, counter, duplicate table or key state.

| `test-banked-join-` suffix | Required complete-MCU behavior |
| --- | --- |
| `rx-queues` | Separate application/RX/priority-ACK ownership, pre-authentication FULL, exact-frame retry after drain, duplicate ACK without second delivery, endpoint/profile rejection |
| `wrap-quarantine` | Public queue/cancel/step/confirm through254; actual TX254/255, quarantine and ZDO reply exhaustion, actual counter0 at exact expiry |
| `ack-correlation`, `ack-deadlines` | Early/wrong/late ACK, peer/endpoints/profile/cluster/counter/format/security, no completion before quiescence, three retries with fresh protection, exact and absolute deadlines |
| `zdo-server` | NWK/IEEE address type/status/identity handling, Node Descriptor, generic unsupported TSN reply, Parent_annce/broadcast no-reply, malformed input, deferred response/full-slot/client independence |
| `broadcast-table`, `address-map` | Eight non-evictable broadcast records, sequence wrap and exact expiry; bounded address map, reassignment, unknown IEEE, own-address conflict and genuine Leave |
| `update-full`, `install-timeout` | Durable PAN update despite full APS duplicate table, pending-traffic cancellation, token/epoch/PAN/address/channel/IEEE install correlation, exact-deadline failure |
| `node-correlation`, `node-timeout`, `node-status` | Real Node query, early/correlation/format rejection, three attempts, old TSN, exact deadline and negative status; no premature READY |
| `tc-key-timeout`, `tc-confirm-timeout`, `parent-status` | Missing updated-key/Confirm responses, bounded retry/exhaustion, unsupported parent method and real commissioning Leave |
| `network-key-late`, `tc-key-late`, `tc-confirm-late` | Authenticated input at the exact commissioning deadline never restores READY; durable state and cleanup remain observable |

The wrap setup consumes sequence numbers through real public cancellations;
it does not claim extra PHY transmissions. All timer inputs use the real
monotonic modular clock, including initial32-bit rollover. Time advancement
expires live tables through production logic rather than clearing them.
Every public-call observation still checks the complete controller/MAC/NV
state, CPU continuation, guarded memory and unchanged SP7C cap.

This work also exposed and fixes a real endpoint-admission mismatch:
ZDO's source endpoint0/profile0 reply may target a nonzero requester endpoint
after READY. The remote endpoint no longer misclassifies that reply as local
application traffic and retires membership. Before READY it remains refused;
wrong local source endpoints/profiles remain refused. The focused genuine
host corpus runs82089 checks per native/sanitized executable. Only `nwk_aps`
and the synthetic caller objects change from the accepted MCU baseline;
ordinary XDATA and the279-function/1262-pair liveness proof are unchanged.
Both board definitions match all six new complete artifact identities.
Exact revision `8348ac11117d206c3174a3afe385ecd0027711c1` passed
[full Actions36141209979](https://github.com/faronov/cc2530-zigbee/actions/runs/36141209979):
**100/100 jobs**, all98 workers and both controls. All44 complete-MCU workers
passed: each board executes the original415 calls plus3232 edge calls,
21494 AES/flash events in total, and the separate retained busy-flash case.
Every complete scenario peaks atSP7B. Both missing-key workers retain the
full716229-mutation campaign. The generic wrap worker takes14m02s and ZDO
server10m35s, below the unchanged15-minute limit; no corpus or guard was
removed to fit. This completes #26's linked-image/simulated transport and
endpoint-zero scope, not #27 recovery or physical #13/#14/#15/#28 acceptance.

The ordinary-join tree keeps NWK floors and keepalive bits volatile between
NV saves and stages provisioning/association until the first key save. Its
refreshed host references keep every public call and AES block but carry
fewer journal writes/erases:415 calls/1651 events originally and3234 edge
calls/11445 events (`update-full` gains the two public calls of its
duplicate-eviction check; `broadcast-table` now requires unchanged flash
after unsecured broadcast data). The fixed `bdb_join_t` grows to1677 bytes.

Its compiler spills no longer fit the former hand-placed module frames
(`bdb_join`, `nwk_aps` and `zdo_runtime` would need26,26 and116 bytes).
The complete image therefore uses the join-smoke method instead of source
changes: modules compile with draft `JD_<module>` DATA (the banker keeps its
DSEG), unchanged `tools/split_link_spills.py` emits per-function frames of
at most24 bytes, and a deliberately unverified draft link exposes the actual
linked call graph. `verify_banked_join.py --solve-data` collects every
live-byte/callee-write pair from that graph and deterministically places
whole frames inside08..1D/26..45. The final relink is accepted only by the
strict per-call backwards liveness check above; the draft link itself is
rejected by that check. Retained DATA, OSEG, bit storage, stack50..7C,
libc scratch, indirect spill users and the copied flash-RAM engine checks
are unchanged, and direct DATA accesses must stay in owned bytes.
Both boards derive the same placement. Offline, all22 targets per board
pass with `ARTIFACT_CAMPAIGN=full`; every scenario matches the host
transcript and peaks atSP7B. This is simulated evidence, not hardware
acceptance.

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

## First physical join gate

The [observed MAC broadcast](MAC_SMOKE.md) is not a join. A physical
commissioning trial still needs a combined, boot-disarmed image containing
the real radio adapter and link driver, with complete bank/DATA/stack/alias
proof. Neither the synthetic-PHY `banked_join` image nor the7644-byte
CHILD object-plus-context floor establishes that image's readiness.

Before admitting a trial, bind the actual coordinator/TC IEEE, current
channel/PAN/Extended PAN/update ID, unique device IEEE and explicit initial
counter floors. Provision a unique16+2-byte install code through the real
key/counter/NV owner; do not inject the coordinator's network key or import a
READY/key-owner snapshot. Supply separately qualified MAC backoff bytes.
The public deterministic smoke draws and an unqualified hardware LFSR are
not substitutes for that input contract.

For a ZHA coordinator, inspect the deployed HA release's admission contract.
For example, HA2026.9.3's
[permit schema and service](https://github.com/home-assistant/core/blob/2026.9.3/homeassistant/components/zha/websocket_api.py)
pair `source_ieee` (the **joining device**, not the coordinator) with
`install_code`; ordinary `ieee`-targeted permit is a different branch.
An interface exposing only `ieee` and duration does not provision the
install-code link key. Do not assume that supplying both IEEE fields limits
the install-code branch to a particular admission router.
Also, that release's
[integration diagnostics](https://github.com/home-assistant/core/blob/2026.9.3/homeassistant/components/zha/diagnostics.py)
actively performs an all-channel energy scan. Fetching those diagnostics is
not a passive metadata-only operation. Cached device/descriptor information
does not establish a newly observed radio response.

The physical scope must explicitly include real provisioning/NV writes,
initial RX/AUTOACK, bounded retries and the final network-wide180-second
Permit Joining request described below. Stopping at a READY checkpoint does
not itself stop RX; retain the owner until verified physical closure.
Use independent observations of association, authenticated key exchange and
the remaining BDB stages, not just a new HA device-list entry.
The current bounded endpoint-zero server does not implement Active Endpoints
or Simple Descriptor responses, so complete ZHA interview and useful
application attribute exchange remain separate work rather than implied
consequences of BDB READY.

### Radio-backed board caller under construction

The active [J1-J6 execution plan](LINK_JOIN_PLAN.md) separates integration,
resource linking, stack reduction, DATA/ABI proof and genuine MCU replay.

`join_smoke` is a **draft, not an accepted or flashable image**. Unlike the
earlier synthetic-PHY caller, it explicitly provisions through the real
key/counter/NV services and drives BDB through `mac_link_driver` and the
radio adapter. Fresh-process native and nonrecovering-sanitizer execution
of this actual caller reaches authenticated BDB READY through the independent
synthetic peer:599 foreground polls,9 backoff inputs and9 transmitted frames.
Nine failure/admission scenarios cover unarmed expiry, malformed mailbox,
missing random-input count, existing durable state, exhausted random tape
and no selected parent, plus withheld network key, retained radio error and
nonreturning busy flash. These are host observations, not MCU or RF results.

The first actual SDCC combined link was refused at7811 ordinary XDATA bytes.
After integration of DIRECT staging/shared status and the shallow driver/
guard and NV/wire/AES candidates, both real resource links now use **7641/7680
XDATA**, including the real caller, banker and linked libc, and243593/243633
populated CODE bytes (generic/LG) below the physical NV boundary. The39-byte remainder
is actual linked space. Function-owned spill placement now passes the linked
DATA/OSEG/libc byte-lifetime checker after an actual relink.
An independent actual linked-instruction check now gives45 bytes aboveSP4F
against45 available, with8/8 nested bank calls. Exact CRT/banker/flash-template
checks support this static bound, not an observed MCU stack peak. The ten
host caller cases also compare shallow/deep NV/wire/AES execution within each
native/sanitizer mode. These **host-tested and statically image-checked**
results alone do not establish MCU execution.
The first trial need not reserve the later1024-byte application budget.
Its required gate is a complete safe image, not a maximal RAM optimization.

The explicit `CC2530_BANKED_LINK` ABI keeps the timing-sensitive lower radio,
MAC timer and attempt implementation together, with banked top-level attempt,
AES and MAC-codec entries and independently banked wire/key services.
`tools/split_link_spills.py` preserves every emitted instruction while
separating compiler-only DATA spill allocations by function. It also retains
shared-header source-line locations under module-qualified debug labels
instead of dropping the colliding records. Splitting alone is not permission
to overlay live storage. The separate placement solver binds every spill to
its owning function, and the post-link checker rejects actual live-across-call
collisions, including OSEG/libc. Near dependencies colocate the complete
association and BDB/scan/candidate groups; expected symbolic destinations
are checked, not merely whether a call hits some valid entry. Final compiler
identities and the unchanged SP7C execution cap still need complete acceptance.
Original compiler assembly and debug records remain available.
`make test-join-smoke-host` runs the real caller's ten scenarios.
`make prepare-join-smoke-layout` creates only a marked unverified resource
probe and report; no execution follows from that resource result alone.
`make prepare-join-smoke-data` additionally solves, relinks and checks the
compiler-scratch placement, saving the bound result in `analysis.json`.
The layout reader preserves complete NoICE symbol identities and verifies
their truncated map projection instead of accepting ambiguous map names.

`make prepare-join-smoke-image` now adds immutable offline admission and
the exact native admission header. It accounts for the full26-byte runtime
prefix and1421 XDATA object/parameter declarations, including valid placement
of `clock`'s private timebase outputs after the closed flash-executor prefix.
The opt-in `test-join-smoke-mcu` consumes only this pinned image, not an
arbitrary resource candidate. Its new debugger-only simulator build and
reproduction commands are described in the [execution plan](LINK_JOIN_PLAN.md).
The generic image now reaches authenticated JS_READY from reset through
actual radio/MAC, AES/DMA and flash-RAM execution:602 caller polls,
157320 peripheral checkpoints, observedSP7B/7C. The independent peer's9
transmissions match actual DUT register/FIFO traffic. The real terminal
READY loop preserves retained ownership and NV. This is **complete positive
MCU simulation**, not a physical join, LG full replay or new CI acceptance;
expanded failure execution is recorded separately in the
[execution plan](LINK_JOIN_PLAN.md#complete-caller-failure-corpus).

The caller uses the reserved status block at1E00 as `JSN1`, rather than
linking or claiming the M0 status ABI. Its admission/configuration storage
is reused for the driver only after successful provisioning and BDB start
have returned; no bound driver is recycled. Random bytes are preloaded once,
consumed without refill and explicitly exhausted, never generated from the
public test tape on hardware. READY and FAULT retain radio ownership.
No halted checkpoint, driver return or READY flag is a claim of RX shutdown.

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

Under `CC2530_MAC_LINK` the transport and BDB owner is the interval MAC owner.
The transport does not step its submitted frame in the same call. It issues
`NWK_APS_ACTION_ARM` (5) and waits for a correlated `bdb_join_armed` after real
adapter preparation. Before releasing a DONE slot it issues
`NWK_APS_ACTION_DISARM` (6) and waits for `bdb_join_disarmed`. A matched
ARMED is accepted even when a cancel or stop arrived after ARM; the transport
then cancels through DONE and DISARM. It does not re-ARM a stopping or
cancelled ordinary transmission after a retry. See the
[link driver](MAC_LINK_DRIVER.md#preparation-handshakes); it is host-tested
end to end and compile-checked, not linked into an MCU image.

The experimental [compact link profile](MAC_LINK_DRIVER.md#experimental-returning-work-profile)
removes the association shadow from the BDB phase union. With SDCC 4.2.0 the
BDB context falls from1696 to1433 bytes because runtime becomes the largest
phase. All translation units must agree on this layout. The complete link
composition still exceeds ordinary XDATA and has no linked MCU proof.
The additional [shared UPPER profile](MAC_LINK_DRIVER.md#shared-upper-workspace)
keeps that public BDB layout while alternating private key/protocol work.
Nested protocol regions and separate key-status outputs remain disjoint;
exact lower-I/O loans do not make retained contexts or queues reusable.
Its8225-byte floor still lacks a complete memory/bank/stack proof.

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

ED Timeout Request/Response keepalives no longer write NV: the request
intent, parent information and NWK incoming floors are volatile (see
SECURITY_KEYS.md, *Volatile floors and keepalive state*). The synthetic READY
fixture now runs seventeen 30-second keepalive cycles with zero flash commands
and stays READY. Outgoing NWK counter reservations and every APS/key event
remain durable, so the erase quota still bounds key exchange and counter
reservation, not idle keepalive time.

This earlier evidence is **host-tested and SDCC compile-checked**, not a new linked-image,
alias-aware execution or physical observation. Existing linked-image and
simulator suites remain mandatory and unchanged; the old resident profile
does not prove that these new services fit or execute safely together on8051.
Whole-stack placement/execution and ABI/alias proof were still gates at that
stage; the [complete MCU corpus above](#whole-target-edge-corpus) now accepts
the consolidated #24/#25/#26 target scope. A truthful real-radio adapter,
entropy, electrical durability and physical interoperability remain separate.
Host READY alone did not close #26. #23's bounded implementation is accepted
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
MAC DSN, generation or IFS.

After association INSTALL is issued **before** durable admission: the parent
sends Transport Key within ~10 ms of the Association Response, while the LG
CC2530F256 NV write path (default-TC provisioning plus short-address
admission) takes ~1.6 s of stalled CPU. Only the confirmed INSTALLED event for
the RAM identity (discovered or provisioned, plus the associated short address)
triggers the NV writes, which then run with AUTOACK RX open on that address, so
an early unicast is hardware-acknowledged and held in the RX FIFO. The stored
configuration must then equal the installed one exactly before WAIT_KEY.
A flash-read failure during that admission cannot publish membership or
readiness, and a genuine modeled reboot still sees PROVISIONED. An INSTALL
stall or fault before admission leaves NV unchanged, so a fresh join may start.
The MAC ACK of a frame the device later loses is not a membership claim: a
lost key ends in KEY_TIMEOUT. This is not a secure-rejoin procedure.
Host-tested (`tests/test_bdb_join.c`, E2E `EARLY_KEY`). Hardware-observed on the
LG board with an HA/EmberZNet coordinator: the third Transport Key transmission
was ACKed once RX opened, and Device Announce and the Node Descriptor exchange
followed.

The network-key, TC-link-key and Confirm Key deadlines judge a key frame when
`bdb_join_receive` accepts it (`timely`), not when the next step runs. The
durable key save inside that receive stalled the CPU for ~5 s on hardware. That
alone turned a TC link key which arrived on time into TC_FAILED. A key that
arrives at or after its deadline still fails. Host-tested (`test_bdb_join.c`
cases 6–11). With both changes the LG board was hardware-observed to reach
BDB READY against the HA/EmberZNet coordinator (~19 s). That covered the
network key, TC link key, Verify/Confirm Key, the parent query and the permit
broadcast. HA did not list the device. The join-smoke image stops at READY,
and the ZDO server answers only Node_Desc_req, so the HA interview
(Active_EP/Simple_Desc/ZCL Basic) cannot complete (superseded below).

### 2026-09-30 LG ordinary join and ZHA interview

The join-smoke image now serves after READY (stage 5, `JOIN_SMOKE_SERVE_TICKS`)
instead of stopping, and reports Basic ModelIdentifier once. The ZDO server
answers Active_EP_req (one endpoint) and Simple_Desc_req (HA profile, device
`FFFF`, input cluster Basic only); the caller serves ZCL Basic reads of
ManufacturerName/ModelIdentifier. The scan RX profile accepts beacons only,
and NWK incoming floors plus ED Timeout keepalive state are volatile (see
SECURITY_KEYS.md), so a READY device no longer spends ~1-2 s radio-deaf in an
NV save per keepalive. A complete, loss-free scan with no eligible direct
coordinator is repeated up to `BDB_JOIN_ATTEMPTS` times; one hardware run lost
the only permitting coordinator beacon and otherwise ended in NO_PARENT.
Host-tested (`test_security_keys.c`, `test_bdb_join.c`, link/join-smoke/E2E
suites) and image-checked (`join_smoke_image.py`, XDATA 7680/7680).

Hardware-observed with an ordinary ZHA permit (no install code, no injected
network/channel/PAN identity) on the LG ESL29 board against a ZBT-1/EmberZNet
coordinator, image IHX pinned in `tools/join_smoke_pins.json`: scan found the
coordinator on channel15, association, Transport Key, Device_annce, TC-link-key
Request/Verify/Confirm, End Device Timeout negotiation and the permit
broadcast reached BDB READY. A private sniffer capture decrypted with the
delivered keys confirms that sequence. zigpy then completed the interview
(Node Descriptor, Active Endpoints `[1]`, Simple Descriptor, Basic read) and
ZHA lists the device as manufacturer `cc2530-zigbee`, model `LG-ESL29`,
EndDevice. In the same capture, each 30 s End Device Timeout keepalive
advanced the outgoing NWK frame counter by exactly one (no 256-step NV
reservation), so keepalives performed no NV save. Earlier attempts only failed to appear because zigpy retained a
stale uninitialized device object; a ZHA integration reload cleared it.
The endpoint exposes no application entities; that is still separate work.
The run is bounded by the serving budget, not a long-running device, and no
sleepy behaviour, rejoin after reset or production NV endurance is claimed.

### Synthetic temperature and humidity demo

**The values are synthetic demonstration data, not measurements.** The LG
board has no temperature or humidity sensor. The endpoint now advertises
input clusters Basic `0000`, Temperature Measurement `0402` and Relative
Humidity Measurement `0405` (device ID still `FFFF`). `zdo_runtime` answers
Read Attributes for MeasuredValue `0000` of both clusters with a sawtooth
derived from runtime time: 21.0-24.9 °C in 0.1 °C steps and 45.00-55.00 %RH
in 0.25 % steps (`ZDO_RUNTIME_TEMPERATURE/HUMIDITY`, sample index from bits
22..29 of the MAC-symbol time). Every other attribute returns
UNSUPPORTED_ATTRIBUTE; MinMeasuredValue/MaxMeasuredValue, Tolerance and
ClusterRevision are not implemented. Configure Reporting and Bind are not
implemented either: the ZCL Default Response is UNSUP_GENERAL_COMMAND
(`0x81`) and ZDO returns NOT_SUPPORTED. They are never acknowledged as
successful.

READY no longer has a serving budget, and the join-smoke caller serves until
reset or a fault (stage 6 and `JOIN_SMOKE_SERVE_TICKS` are removed). It first
sends the Basic ModelIdentifier report. After that, the banked `zcl_sensor`
builds one unsolicited, APS-acknowledged Report Attributes frame to
coordinator endpoint 1 in each 2^21-symbol slot (~33.6 s). Even slots carry
temperature and odd slots humidity, so each cluster is reported about every
67 s. The ZCL sequence number is the slot number. Report FULL and the APS
counter-wrap quarantine are retried within the same slot; any other send
failure is recorded in `announce` as `0x80|result`, and the next slot sends
again. READY CSMA backoff bytes now come from a maximal 8-bit Galois LFSR
seeded by one nonzero qualified draw at READY entry. It is deterministic and
**not entropy**, and it has no other consumer. In READY, the status `steps`
wraps modulo 2^32.

To fit the image, common CODE, XDATA (7680/7680), the DATA reservations and
the 45-byte static stack bound are all unchanged in size. The serve
temporaries reuse the dead admission scratch, and the report builder lives in
its own bank. Host-tested: `test_zdo_srv.c`, `test_bdb_join.c` (reads of both
clusters and an alternating-report peer), and `test_join_smoke.c` in both
key modes (unbounded READY, consecutive slot reports, no further draws).
Image-checked: `prepare-join-smoke-stack` and `join_smoke_image.py` with the
repinned LG default-TC identity.

Hardware-observed on the LG board (channel 15, ordinary join with
coordinator-only `zha.permit`): ZHA interviewed the endpoint with all three
clusters and created temperature and humidity entities. A device previously
interviewed with the one-cluster signature must be removed from ZHA first,
otherwise zigpy keeps the stale signature. The first READY runs faulted after
about six minutes on a busy channel (~40k sniffed frames in 25 min), exposing
two radio_autoack faults, both now fixed and host-tested in
[RADIO_AUTOACK](RADIO_AUTOACK.md): a stopped drain with a complete second
frame but FSMSTAT1.FIFOP low (`FIFO_ERROR`), and an RXOVERF latched between
the FSMSTAT1 and RFERRF reads (`CONTROLLER_ERROR`). With both fixes the board
stayed READY for the whole 25-minute window. HA recorded continuous reports
about every 67 s, 21.1-23.2 °C and 45.00-50.25 %RH, and the final status was
`ready=1`, reason 0, no radio errors and no recorded send failure. This is one
run on one board, not a long-term reliability claim.

NV record CRC-32 is now computed a byte at a time from two
16-entry nibble tables rather than bit by bit; its format and results are
unchanged (`test_nv_record.c` reference CRC).

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

At this historical stage the remaining #26 work was concrete: remove the ordinary-XDATA
deficit without discarding retained state; establish physical DATA/libc
ownership for the full active caller graph; solve real key-operation caller
depth without raising SP7C; place/link the complete28-service composition;
then add a separate real-wire synthetic MCU caller, strict whole-image/
failure-state replay and both-board CI consumers. The complete MCU and edge
sections above record its later completion; linkage of the smaller key image
or native BDB READY did not supply it. #27, #40/#45 and all physical acceptance
remain outside that completed offline scope.

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

### Returning controller views and grouped initialization

The next bounded reduction preserves the retained BDB/NWK/ZDO contexts and
their occupied slots. It reuses only private returning views:

| Owner | Shared work | Required separation |
| --- | --- | --- |
| BDB |57-byte union:15-byte parent policy or simultaneous28-byte MAC and29-byte NWK views|Parent selection returns before runtime reception. Both receive views remain live through destination validation|
| NWK/APS |29-byte union:26-byte MAC header,17-byte cancellation event or29-byte receive hint|An active-MAC step returns before any transmit construction. A receive call is separate; lower services do not retain these views|
| ZDO |37-byte descriptor/node-output pair or35-byte server views|Node response and its encoded bytes remain disjoint. Descriptor reception returns before the server branch uses its three simultaneous arguments|

Each module's key-status snapshot stays separate: it may be needed before
and after lower calls. No key/counter/journal state, pending packet,
association diagnostic or MAC lease is overlaid.

`nwk_aps_init` now accepts a copied `nwk_aps_config_t`, rather than eleven
separate arguments. BDB embeds it at `config.transport`; crypto limits are
`config.transport.limits`. The input is complete and disjoint from the
destination context; no input pointer survives initialization. This reduces
SDCC argument-evaluation pressure without a reentrant/software-stack ABI.
Actual SDCC sizes remain19 bytes for the transport configuration,93 for
the BDB configuration and1428 for the BDB context.

| Module | CSEG before / after | XSEG before / after | DATA before / after |
| --- | ---: | ---: | ---: |
| BDB |16272 /16012|213 /198|46 /34|
| NWK/APS |17367 /17748|320 /264|35 /35|
| ZDO runtime |11473 /11473|246 /211|20 /20|

This controller-only change saves106 ordinary-XDATA bytes and12 compiler
DATA bytes, at121 more CODE bytes. These deltas are independent of the
subsequent MAC-work measurements; do not mistake them for a complete link.
Both-board native/nonrecovering sanitizer callers pass81791 checks.
Added cases cover null/grouped configuration, all ten invalid selector/
limit boundaries, both legal interval extremes, unchanged inputs/owner/
failed outputs, copied input lifetime, and no cryptographic or NV side
effect during initialization. The existing interleaved commissioning,
ACK, application/server and failure corpus remains.

The whole-MCU gate is still open. Placement probes compiled the real services
into separate CODE banks, but have not produced an accepted complete image.
Their function-frame investigation exposed a separate live DATA/call-depth
problem; removing an XDATA deficit alone cannot close #26.
The existing banking ABI still rejects stack-auto/xstack. An isolated
alternative upper-owner experiment was not adopted: SDCC's external-stack
mode still emits hardware-stack `_bp` spill slots, so zero module DATA in
that experiment is **not** a proof of meeting SP7C. No new banking macro,
relaxed guard, larger stack or synthetic successful service is enabled.
The next complete-image work must prove actual active-call lifetimes and
stack use, then execute the authentic join/data/failure transcript.
