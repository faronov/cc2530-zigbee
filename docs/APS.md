# Offline APS Data codec

`include/aps_frame.h` and `src/aps_frame.c` encode/decode a bounded subset
of the R22 APS Data APDU. This is an independent byte codec, not an APS
service, endpoint dispatcher, acknowledged transaction or security service.
It is not linked into board firmware.

## Wire subset

Only Data type `0`, normal unicast delivery mode `0`, with no APS security
or extended header is supported. Offsets start at the APDU, excluding
NWK/MAC headers, PHY length, FCS and radio metadata.

| Offset | Field | Treatment |
| --- | --- | --- |
| 0 | Frame Control | Type bits 0..1 = 0, delivery bits 2..3 = 0; ACK-request bit 6 retained |
| 1 | Destination endpoint | Preserve `00..FF`, including ZDO endpoint 0 and all-active-endpoints selector `FF` |
| 2..3 | Cluster ID | Little-endian 16-bit metadata |
| 4..5 | Profile ID | Little-endian 16-bit metadata |
| 6 | Source endpoint | `00..FE`; reject `FF` |
| 7 | APS counter | Preserve `00..FF`; no sequence allocation or duplicate rejection |
| 8 onward | Payload | Opaque bytes, including zero length; no ZDO/ZCL validation |

Command, Acknowledgement and Inter-PAN frame types return unsupported-type,
including short command/ACK headers. Broadcast delivery `2` and group
delivery `3` return unsupported-delivery; reserved delivery `1` and typed
values above `3` are invalid. No group-address field is guessed or skipped.
ACK-format bit 4 is excluded from this Data subset; security bit 5 and
extended-header bit 7 have explicit unsupported results. Even an extended
header indicating no fragmentation is unsupported, not opaque payload.
`aps_header_t.flags` contains only upper-nibble flags; attempts to inject
type/delivery bits through it fail. Unsupported constants identify rejected
inputs and do not advertise implemented features.

The destination `FF` selector addresses active endpoints on the recipient,
not APS broadcast delivery across devices. Endpoint 0 is metadata, not a ZDO
service. Endpoints `F1..FE` are restricted to Alliance-approved applications
by R22 section 2.3.1.3; retaining received values is not permission to deploy
an application there. Profile/cluster IDs are not filtered against registered
services, and source/destination endpoint/profile compatibility is not checked.
The caller must eventually implement those policies and the appropriate
NWK destination/security checks before dispatch.

ACK request is only a header bit: decoding it neither sends an ACK nor
creates a pending transaction. APS, NWK and MAC counters are independent.
The codec does not allocate counters, retry, recognize duplicates, authenticate
input or report that data was delivered.

## API, limits and ownership

`aps_frame_decode(body, length, result)` copies header metadata and returns
payload offset/length relative to that APDU in `aps_frame_info_t`.
`aps_frame_encode(header, payload, payload_length, body, capacity, length)`
serializes the header and payload. Both return `aps_codec_result_t`.
Native C struct layout is not a wire format.

The complete APDU has a **108-byte raw-codec cap**, derived from the existing
116-byte NPDU cap minus the minimum eight-byte NWK header. Its eight-byte
APS header leaves at most 100 opaque bytes. This is a serialization boundary,
**not an APSDE-DATA service payload guarantee**. In particular, R22 Table 2-23
defines the separate service constant `apscMinHeaderOverhead = 0x0C`;
this implementation does not redefine that constant as eight.
Actual service limits, security overhead and any fragmentation belong to
future APS/NWK procedures.

Each outer encoder still checks its own budget. For the supported unsecured
test chain, a compressed short/short MAC MHR and no optional NWK IEEE
addresses can carry all 108 APDU bytes. One/two extended MAC addresses remove
6/12 bytes, and one/two NWK IEEE fields remove 8/16 bytes. The largest supported
headers leave 80 APDU bytes, hence 72 opaque bytes in these synthetic tests.
APS security and NWK security remain unsupported rather than silently omitted.

Length/capacity arguments are uint16; bounded returned lengths/offsets are
uint8. Required null pointers, missing header bytes, excessive input/payload,
insufficient capacity, unsupported formats and invalid header fields have
explicit errors. The payload pointer may be null only for empty payload.
No decoder result or encoder byte/output length changes on failure.
Error precedence for simultaneous faults is not an ABI.

Callers own truthful, accessible buffer spans. All input/output objects must
be non-overlapping, including metadata and returned length. Decode copies
metadata, not payload; it retains no pointer. CODE/XDATA inputs use SDCC's
generic-pointer ABI. There is no heap or network state. Calls are foreground
only and non-reentrant under the large-model SDCC storage convention.
`APS_CODEC_OK` means supported syntax, not endpoint/profile acceptance,
authenticated traffic or permission to transmit.

After successful MAC Data and NWK Data decoding, pass only the NWK payload:

```c
aps_frame_decode(body + frame.payload_offset + network.payload_offset,
                 network.payload_length, &transport);
```

Check every layer's return status and frame type before using its offsets.
Only after APS success may the caller consume its opaque payload. In the
other direction, check APS encode, NWK encode and MAC encode independently;
an inner success does not establish outer capacity or protocol acceptance.

## Offline evidence

```sh
make test-aps-frame
make test-protocol-frame
```

Both are included in `make ... all test`. They use separate simulator-only
`aps_frame_test.ihx` and `protocol_frame_test.ihx` executables, never `IMAGE`
selections, firmware to flash or CI upload artifacts.

Shared host/SDCC APS tests cover original CODE golden headers/payloads, XDATA
round trips, every FCF byte, 0/255 endpoint and counter boundaries, all
supported payload lengths, insufficient capacities, truncated headers and
unchanged errors. Host matrices additionally exhaust typed control fields,
endpoint/counter values, all 65,536 cluster/profile identifiers, every value
at each of 100 payload positions and every uint16 length/capacity.
ASan/UBSan runs use exact input/header/result/payload/output allocations.
The standalone image keeps the existing 512-byte XDATA reservation budget,
strict CODE/CDB/result ABI accounting and alias/upper-IRAM/unwind guards.

The separate composition image executes the real MAC/NWK/APS codecs for all
four MAC address-size and four NWK IEEE layouts, both APS ACK-request values
and empty/one-byte/maximum payloads. A complete original golden chain fixes
byte order independently of round trips. It checks the exact 125-byte
FCS-free MAC body boundary, layer-specific overflow, all truncated APS header
lengths, and MAC/NWK-valid payloads rejected by APS without output changes.
Its three scratch frames and linked codec storage have an explicit
1,024-byte XDATA reservation budget; the same strict layout/alias/stack checks
apply. Existing standalone limits and original MAC test scenarios are
unchanged. These are test-harness budgets, not deployed firmware sizes.

Evidence is **host-tested, image-checked and simulated**, not RF,
authentication, ACK handling, working ZDO/ZCL, join or interoperability.
All data is synthetic; see the [primary sources](PROVENANCE.md#offline-r22-aps-data-frame-sources).

The separate [ZCL wire codecs](ZCL.md) now consume an APS payload in offline
tests. `zcl_frame_test.ihx` executes actual APS/ZCL composition; the host
protocol test adds the full MAC/NWK/APS/ZCL/value chain. The existing
`protocol_frame_test.ihx` remains three-layer, with its original memory
budget. APS itself still treats payload as opaque and gains no dispatcher,
ZCL handler or board caller.

The additional `protocol_budget_test.ihx` now also executes the full
MAC/NWK/APS/ZCL Discover-then-Read chain in one SDCC image, with
[per-subsystem resource accounting](ARCHITECTURE.md#integrated-protocol-resource-budget).
Validated serialization moves into leaf helpers to reduce persistent IRAM
spills; APS wire/error behavior and its existing component budget are unchanged.
