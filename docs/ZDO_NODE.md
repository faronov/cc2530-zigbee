# Offline R22 Node Descriptor codec

`include/zdo_node.h` and `src/zdo_node.c` provide original bounded
`Node_Desc_req`/`Node_Desc_rsp` payload encoding and decoding. This is #85's
join-critical **wire preparation**, not an endpoint-zero server, transaction
owner, descriptor advertisement or authenticated Trust Center decision.
It is not linked into any board image. No hardware was accessed.

The baseline is Core R22, **05-3474-22**, April 19, 2017, not a later Zigbee
revision or a copied vendor implementation. See the
[reviewed primary locations](PROVENANCE.md#r22-node-descriptor-wire-sources).

## Supported payloads

Offsets and lengths start at the ZDP Transaction Sequence Number (TSN),
excluding APS/NWK/MAC headers, security and PHY metadata. Multioctet fields
are little-endian; native struct layout is never a wire format.

| Payload | Canonical bytes | Meaning |
| --- | --- | --- |
| Request, cluster `0002` | 3 | TSN and queried 16-bit NWK address |
| Successful response, cluster `8002` | 17 | TSN, status `00`, queried address, 13-byte descriptor |
| Addressed failure | 4 | TSN, status `80`/`81`/`89`, queried address; no descriptor |
| Generic unsupported response | 2 | TSN and `84`; neither address nor descriptor |

The addressed failures are `INV_REQUESTTYPE`, `DEVICE_NOT_FOUND` and
`NO_DESCRIPTOR`. They differ from the generic `NOT_SUPPORTED` response
specified by R22 section2.4.4.1. Local `ZDO_NODE_OK` means parsing or encoding
succeeded, **not that the remote operation succeeded**. Inspect `status`.
Other remote statuses return `ZDO_NODE_UNSUPPORTED_STATUS` without changing
the output; they are not silently mapped to a known failure.

On receive, mandatory fields must be present. R22 section1.2.5 requires
ignoring additional trailing fields, so the decoder accepts extra bytes
within its100-byte input cap and returns the recognized prefix in `consumed`.
It does not reinterpret an error response's trailing bytes as a descriptor.
This is deliberately not an exact-total-length parser. The encoder emits
only the canonical prefix, ignores RX-only `consumed`/`has_address`, and
does not read inactive descriptor/address fields.

For decoded addressed responses, `has_address=1`; the status-only generic
response has `has_address=0` and address0. Absent descriptor fields are zero,
not a successful empty descriptor. Callers must use status/presence metadata,
not those zero values, to decide which fields exist.

The cap is the existing APS raw-codec payload limit,108 minus8 header bytes,
not a new APS service or secured-payload guarantee. All16 queried-address
bits and all8 TSN bits are preserved as syntax. No address is thereby accepted
as routable, assigned or owned. The caller allocates/matches TSNs; the codec
does not increment a counter or match a response to a request.

## Descriptor fields and supported domains

The13-byte wire descriptor preserves logical type, Complex/User Descriptor
availability, frequency-band bits, MAC capability, manufacturer code, maximum
buffer/incoming/outgoing sizes, Server Mask capabilities/revision and
descriptor capabilities.

The supported subset permits logical types0..2; two availability bits;
frequency bits0/2/3/4 (not only the local2.4GHz band); MAC capability bits
other than reserved4/5; maximum buffer0..127; incoming/outgoing0..32767;
Server Mask capability bits0..6; and two descriptor-capability bits.
APS flags and all reserved descriptor bits must be zero. RX rejects these
unsupported reserved-field forms rather than normalizing them. This is an
explicit conservative codec policy, not a claim that every future descriptor
extension is malformed or that general R22 reserved bits must be rejected.
It is separate from the mandated trailing-field tolerance above.

`available` bits0/1 represent the two descriptor-availability bits.
`server_flags` holds Server Mask bits0..6; `stack_revision` holds bits9..15
as a seven-bit value0..127. Server Mask bits7/8 remain reserved. In particular,
revision22 is not Beacon protocol version2. Revision0 can describe a legacy
peer; the parser does not demand22 or trust any revision as proof of actual
conformance. A Primary Trust Center capability bit is not authenticated TC
identity. The MAC security-capability bit describes IEEE MAC security, not
Zigbee key establishment.

Manufacturer codes and resource/capability fields remain caller/peer metadata.
There is no guessed manufacturer, self descriptor or production default.
Incoming/outgoing transfer sizes may exceed the buffer size with fragmentation;
the codec does not invent a relation between them. Successful parsing does
not enable fragmentation or any advertised feature.

## Ownership and integration

The four APIs are `zdo_node_req_decode`, `zdo_node_req_encode`,
`zdo_node_rsp_decode` and `zdo_node_rsp_encode`. Length/capacity arguments
are16-bit; emitted lengths and consumed prefixes are8-bit.
Null required pointers, truncation, oversized receive bodies, insufficient
output capacity, invalid descriptor fields and unsupported statuses have
distinct local errors. Every error preserves **all output bytes and metadata**.
Error precedence for simultaneous faults is not an ABI.

Callers provide truthful accessible spans and disjoint input/output objects,
including returned lengths. SDCC inputs use generic CODE/RAM pointers.
Caller storage must exclude MMIO, the reserved status area, the IRAM alias
and compiler/libc scratch. Calls are serialized, foreground-only and
nonreentrant under model-large. No pointers are retained; there is no heap,
I/O, board GPIO, radio, key, persistent state or successful service stub.

The caller must select the proper ZDP APS envelope/cluster and unicast
transaction before dispatch. The tests compose the genuine existing APS
codec with synthetic profile0, source/destination endpoint0 and clusters
`0002`/`8002`. They independently distinguish APS counter`A9` from TSN`5A`.
No envelope admission or authenticated transport is implemented by this codec.
An alternate discovery holder can return another node's descriptor, so the
queried address must not automatically be equated with the transport source.
Matching TSN, original query, permitted responder and security context remains
the future transaction owner's responsibility.

Node Descriptor is a mandatory service; the generic status-only encoding is
not permission to omit it from a conforming endpoint. The general unsupported
unicast-response/broadcast-drop dispatcher is still required by #25 **before
endpoint0 is exposed**. The subsequent [offline ED dispatcher](ZDO_SRV.md)
now implements that bounded response/drop policy with this codec, but still
does not register an endpoint or complete the remaining mandatory services.
There is no endpoint exposure here.

## Evidence and resources

Reproduce the isolated composition with:

```sh
make BOARD=generic test-zdo-node
make BOARD=lg_esl29_rev03 test-zdo-node
```

Actions is the full both-board acceptance gate. Local compilation and
proof construction do not substitute for the exact-commit CI result.
The original synthetic corpus has2559 shared native/target checks and
1,358,150 total native checks. Native exact allocations cover every16-bit
queried address, every TSN byte, every status byte, response/request
length/capacity boundaries and all256 values at every descriptor byte.
All256 values of each typed8-bit descriptor field are exercised too.
Shared cases include all128 stack revisions, mask boundaries, explicit
field extrema, CODE/RAM goldens, unknown/reserved fields, truncation,
trailing input, inactive fields, null pointers and atomic failures.
The same native corpus runs under nonrecovering ASan/UBSan.

| Linked accounting | CODE | XDATA | DSEG | OSEG | BSEG bits |
| --- | ---: | ---: | ---: | ---: | ---: |
| Production `zdo_node` | 3133 | 86 | 32 | 0 | 1 |
| Unchanged production `aps_frame` | 1626 | 69 | 8 | 7 | 0 |
| Caller, including constants/startup contributions | 8353 | 399 | 6 | 0 | 3 |
| Full image, including linked runtime | 13639/16384 | 574+64/768 | - | - | - |

Target struct sizes are14 bytes for descriptor metadata,4 for a request,
20 for a response; the wire descriptor is13 bytes. Compiler-private ZDO
storage is `[0,86)`, APS storage `[86,155)`, caller objects `[155,542)`,
caller locals `[542,554)` and linked libc scratch `[554,574)`.
Initial stack allocation starts at`41`; the checkpoint unwinds to SP`40`;
the full-run peak is`5E`, under the unchanged`7C` cap. These are isolated
image measurements, not whole-stack fit or concurrent/ISR headroom.

`boot_zdo_node.py` checks full CODE/runtime/constants, raw CDB **before
decoding**, complete parsed map, complete relocatable objects (excluding
only their build-path first line), and all three immediate relocated-listing
snapshots. It also checks ordered real instructions, explicit public
entries/parameters/return ABI, caller/production field layouts and disjoint
private/caller/libc storage. APS composition must call all six actual APIs.
No MMIO instructions are permitted in the linked component/caller listings.

The proof rejects39,047 artifact mutations,21 snapshot mutations,3
stack-peak mutations and one disabled-alias control. It executes the actual
SDCC image under the existing alias-aware `s51` harness with the unchanged
15-second deadline, complete check count, untouched unused/status-tail
XDATA, upper IRAM, GPIO/clock/timer/RF SFRs and stack unwind/high-water checks.
Both-board CI uses the existing composed-service jobs; no old job, case,
budget or deadline is removed or relaxed, and no component image is uploaded.

Evidence is **host-tested, image-checked and simulated**, never
hardware-observed. This codec does not establish MAC completion, network
membership, key verification, authenticated join or full Zigbee conformance.
