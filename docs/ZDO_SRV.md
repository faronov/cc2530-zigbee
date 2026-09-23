# Offline ED ZDO request dispatch

The original #86 `zdo_srv` module adds a stateless request handler to the
[Node Descriptor codec](ZDO_NODE.md). It handles a bounded, caller-admitted
endpoint-zero context and is exercised with the real NWK/APS codecs.
**This is not a registered endpoint, admitted network peer, transport
transaction or authenticated join.** It has no board caller or hardware I/O.

## Context and response policy

`zdo_srv_handle(local, rx, body, length, response, capacity, info)` accepts a
caller-supplied ED descriptor and network address, typed APS header metadata
and a TSN-bearing ZDP payload. The selected context is Data, profile0,
destination endpoint0, source endpoint00..FE, and normal unicast or broadcast
delivery. Group delivery and security/extended/ACK-format header flags are
unsupported. The only admitted flag is ACK-request, not with APS broadcast.
This checks the supported syntax/context; it does not acknowledge the request.

`rx.broadcast` is a strict0/1 indication of **any lower-layer broadcast
addressing**, even if the typed APS header says unicast. The caller must
derive it from the admitted transport, not assume that APS normal-unicast
proves that every lower-layer destination was unicast. APS broadcast delivery
also independently suppresses replies.

The caller must already have selected the intended local NWK destination,
validated optional identities and the sender, performed authentication where
required, and removed duplicates. Those responsibilities cannot be satisfied
by this function's return value or by supplying a boolean. No authenticator,
admission flag, replay check or security-success stub is added.

| Input in the supported context | Outcome |
| --- | --- |
| Unicast `Node_Desc_req`, queried address equals local address | `REPLY`: cluster8002, echoed TSN/address, SUCCESS and caller's descriptor |
| Unicast `Node_Desc_req`, another queried address | `REPLY`: cluster8002, echoed TSN/address and INV_REQUESTTYPE, no descriptor |
| Other low-bit unicast request except the special cases below | `REPLY`: request cluster with bit15 set, echoed TSN and NOT_SUPPORTED only |
| Broadcast-addressed request | `DROP_BROADCAST`: no serialized reply |
| `Parent_annce`001F, unicast or broadcast | `DROP_PARENT`: no further processing by this ED |
| Any response cluster (bit15 set) | `NOT_REQUEST` error; do not feed a response back into server fallback |
| `Device_annce`0013 or unsolicited enhanced update notification003B | `NOTIFICATION` error; no reply and no claim that notification state was processed |

The other-address Node Descriptor error is specifically the ED rule, not a
router/discovery-cache lookup. `Parent_annce` is a role-specific exception to
the generic unsupported fallback. Known notifications must not trigger
response loops; unimplemented announcement/address updates remain visible
errors, not successful no-ops. Response frames belong to a future client
transaction owner.

Node Descriptor requests are specified as unicast. The handler's broadcast
drop for that command is an explicit unsupported-delivery policy, not an
implementation of broadcast discovery. Generic fallback for an unimplemented
mandatory service is not permission to omit that service in a conforming
device. No endpoint is exposed until the remaining #24/#25 admission,
transactions, mandatory handlers and notification processing are implemented.

## Descriptor, bounds and ownership

The supplied local address is1..FFF7, the existing NWK unicast domain excluding
coordinator address0; descriptor logical type must be2. The complete descriptor
is validated by the genuine `zdo_node_rsp_encode` on every call, including
no-reply paths. Other descriptor domains use that codec's existing conservative
subset. There is no installed address, membership flag or invented local
manufacturer/capability/resource default. Test descriptors are synthetic.
The caller remains responsible for truthful capabilities if later advertising
any configured descriptor.

Payload size is at most100 bytes. At least the TSN is required; a processed
Node Descriptor request requires all three prefix bytes. Its trailing bytes
are ignored per R22 section1.2.5, with `consumed=3`. Generic fallback recognizes
only TSN (`consumed=1`); it does not guess an unknown command's mandatory
layout. Drop outcomes also consume only TSN, not a decoded announcement or
validated command body. Above-bound inputs are rejected.

`ZDO_SRV_OK` requires inspecting `info.kind`. A reply gives cluster, destination
endpoint, TSN, remote status, payload length and consumed prefix. Its destination
endpoint is the requester's **source endpoint**, which need not be0: R22
permits application objects to issue device-profile requests. Profile0 and
server source endpoint0 are supplied by the outgoing APS owner. The handler
never echoes the incoming APS counter, allocates a new one, or transmits.

Both drop outcomes have zero reply length/cluster/endpoint/status and leave
the entire response buffer untouched. Error outcomes preserve **both** buffer
and `info`. Missing pointers, unsupported context, invalid local descriptor,
truncation, oversized payload and insufficient response capacity are explicit
local errors; no malformed-request error response is fabricated. All pointers
are required even for zero-capacity/drop cases. Error precedence is not an ABI.

The response is staged in private storage using the actual Node Descriptor
encoder, which also supplies the common TSN/status-only generic fallback
shape. Publication occurs only after validation and the final capacity check.
No pointers are retained and there is no heap or persistent service state.
Identical calls recompute identical replies; this is **not** duplicate
suppression. Timeouts and late-response matching are not applicable to this
synchronous builder and remain transport/client responsibilities.

Inputs may be generic CODE/RAM; callers own complete disjoint spans and exclude
MMIO, reserved status, the IRAM alias and all compiler/libc scratch. Calls are
serialized foreground-only and nonreentrant. As in the existing ZCL code,
top-level `volatile` pointer copies keep SDCC's long-lived temporaries in
large-model XDATA. Pointed-to data is not volatile; pointer widths and wire
semantics are unchanged. Matching header/definition qualifiers are required.
This removes the initial SP82 overflow instead of raising the SP7C cap.

## Actual composition and remaining transport

Twelve target exchanges use genuine NWK encode/decode, APS encode/decode,
server dispatch and Node Descriptor response decode: all four optional NWK
IEEE-address layouts, with local success, other-address error and unknown
request fallback. Replies reverse the selected NWK peer/local addresses, use
fresh caller-supplied NWK/APS sequences22/AA, preserve TSN5A, and target source
endpoint7. An independent complete33-byte NPDU golden checks the success
reply. No new wire parser or echo-based fake transport replaces these codecs.

This is **unsecured synthetic offline composition**, not permission to send
unsecured joined-network traffic. Existing NWK/APS security and APS broadcast
wire inputs still fail explicitly. Broadcast policy is tested with directly
supplied typed context, not a claimed implemented broadcast codec.
No RX queue, APS ACK/retry engine, peer matching, network state installation,
MAC closure, coordinator or authentication is supplied by the test harness.

## Evidence and resources

```sh
make BOARD=generic test-zdo-srv
make BOARD=lg_esl29_rev03 test-zdo-srv
```

GitHub Actions is the full acceptance gate; local compilation and narrow
stack/proof preparation do not replace both-board acceptance. The shared
native/SDCC corpus has1694 checks; native tests total942,914 checks with exact
allocation/nonrecovering ASan/UBSan coverage. Native matrices cover every16-bit
cluster in both lower-addressing classes, every profile, every queried/local
address, all256 values of each supported context byte, all TSNs and source
endpoints, payload/capacity boundaries, notifications/response suppression,
trailing input, descriptor rejection and untouched outputs. Target vectors
include genuine CODE inputs, explicit no-reply results and real full-chain
response bytes.

| Linked object | CODE including constants/startup | XDATA | DSEG | OSEG | BSEG bits |
| --- | ---: | ---: | ---: | ---: | ---: |
| New `zdo_srv` production | 1534 | 69 | 0 | 0 | 0 |
| Unchanged `zdo_node` | 3133 | 86 | 32 | 0 | 1 |
| Unchanged `aps_frame` | 1626 | 69 | 8 | 7 | 0 |
| Unchanged `nwk_frame` | 2294 | 96 | 12 | 10 | 0 |
| Test caller | 6688 | 494 | 4 | 0 | 1 |
| Full image including runtime | 15802/16384 | 834+64/1024 | - | - | - |

Target local/rx/info objects occupy16/11/8 bytes. Production private XDATA is
`[0,320)`, caller objects `[320,803)`, caller locals `[803,814)` and complete
linked libc scratch `[814,834)`. Stack starts at4B, unwinds to4A, and has
full-run peak65 under the unchanged7C cap. This is not whole-stack fit or
ISR/concurrency headroom.

The proof pins full CODE/runtime/constants, raw CDB before decoding, complete
map and memory accounting, all relocatable objects except their build-path
first lines, and five immediate relocated-listing snapshots. It checks
explicit fields/generic-pointer/entry/return ABI, genuine service and
NWK/APS calls, storage boundaries and the absence of peripheral instructions.
The proof rejects46,446 artifact mutations,21 state-guard mutations,3
peak-metadata mutations and one disabled-alias control.
The actual linked image executes under the existing alias-aware `s51` harness,
with the unchanged15-second deadline, exact completed-check count, untouched
unused/status-tail XDATA, upper IRAM and GPIO/clock/timer/RF SFR guards.

Both-board CI uses the existing composed-service jobs; all prior cases,
budgets, deadlines and board-image exclusions remain. Evidence is
**host-tested, image-checked and simulated**, never hardware-observed or
authenticated network membership.
