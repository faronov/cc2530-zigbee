# Offline NWK codecs

`include/nwk_beacon.h` and `src/nwk_beacon.c` decode the **15-byte NWK
information carried in a legacy MAC Beacon Payload**, using Zigbee Core
R22, document 05-3474-22. This is an independent byte decoder, not a NWK
stack, network discovery, parent selection, association, authentication or join.
It is not linked into any board firmware.

## Wire subset

Offsets below start at the upper-layer Beacon Payload, **after** the MAC
header, Superframe Specification, GTS fields and pending-address list.

| Offset | Field | Treatment |
| --- | --- | --- |
| 0 | Protocol ID | Only `00` (Zigbee) |
| 1, low nibble | Stack profile | Preserve all 0..15 values as metadata; not a supported-profile decision |
| 1, high nibble | NWK protocol version | Only `2`, the R22 `nwkcProtocolVersion` |
| 2, bits 0..1 | Reserved | Reject nonzero values in this strict subset |
| 2, bit 2 | Router capacity | Return 0/1 |
| 2, bits 3..6 | Device depth | Preserve 0..15; do not rank/select parents |
| 2, bit 7 | End-device capacity | Return 0/1 |
| 3..10 | Extended PAN ID | Copy eight octets in wire order, least significant first; reject all-zero/all-`FF` values |
| 11..13 | Tx Offset | Little-endian 24-bit value, zero-extended into `uint32_t` |
| 14 | NWK Update ID | Preserve the byte; no freshness or wrap comparison |

The Beacon-specific Extended PAN ID range is `1..FFFFFFFFFFFFFFFE`.
The NIB's separate zero/unknown state is not a valid advertised Beacon
identity. This range check does not establish uniqueness, ownership or trust.
Tx Offset is a symbol-time offset between a device's and its parent's Beacon
transmissions; `FFFFFF` is the specified default for beaconless networks.
The decoder preserves the value without timing conversion or cross-checking
MAC superframe metadata.

Protocol version `2` does not identify the specification revision or prove
R22/BDB compatibility. Likewise, capacity flags are unauthenticated
advertisements, not permission to join, and are separate from MAC Association
Permit. Incompatible stack profiles, permit policy, received CRC/LQI/RSSI,
security and network/parent acceptance still need their future procedures.
Enhanced Beacons, information elements and other payload versions/layouts
are not added by this work.

The separate [four-entry candidate collector](NWK_CANDIDATES.md) now applies
an explicitly preliminary profile2/BO15/permit/ED-capacity policy through the
real MAC and NWK decoders. It adds bounded copied records, duplicate updates,
withdrawal and pressure handling, not active scanning, full parent selection,
freshness or network acceptance. This decoder's raw metadata contract is
unchanged.

The separate [offline scan controller](MAC_SCAN.md) uses that collector within
bounded channel/window and restoration state. Its synthetic action/event
evidence adds neither a hardware scanner nor full parent selection.

## API and ownership

`nwk_beacon_decode(payload, length, result)` returns `nwk_beacon_result_t`.
`NWK_BEACON_OK` means only that the supported wire subset was decoded.
The caller owns truthful input/output storage; the two regions must not
overlap. The returned `nwk_beacon_t` copies all metadata and contains no
borrowed pointers. Its native struct layout is not a wire format.

Null pointers return `NWK_BEACON_INVALID_ARGUMENT`; lengths below/above 15
return `NWK_BEACON_TRUNCATED`/`NWK_BEACON_TOO_LONG`. An unknown Protocol ID
or protocol version returns its explicit `UNSUPPORTED_*` result; reserved
bits and prohibited Extended PAN IDs return `NWK_BEACON_INVALID_FIELDS`.
No error modifies the output. Error precedence for simultaneous faults is
not an ABI. No encoder, fallback layout, allocation, hardware service or
network state is introduced. SDCC large-model parameter/local storage is
non-reentrant: use this API only from the foreground.

With the [MAC codec](MAC.md), the caller must first successfully decode the
frame and require `frame.header.type == MAC_FRAME_BEACON`, then decode its
MAC payload with `mac_beacon_decode()`. The final slice is:

```c
nwk_beacon_decode(body + frame.payload_offset + beacon.payload_offset,
                  beacon.payload_length, &network);
```

Each result must be checked before consuming the next span. Passing the
whole frame or the whole MAC payload to the NWK decoder is not an automatic
request to strip those layers. MAC decoding remains independent: an opaque
payload belonging to another network protocol may pass MAC validation and
then be explicitly rejected by this decoder.

## Offline evidence

`make test-nwk-beacon` runs strict host tests and the isolated
`nwk_beacon_test.ihx` image under alias-aware s51. The ordinary
`make ... all test` commands include it. The test image is **not an `IMAGE`
selection, never a firmware to flash, and not a CI upload artifact**.

Shared host/SDCC cases cover original golden vectors in CODE, copied XDATA
inputs, exact lengths, unchanged errors, 64 single-bit and 64 single-cleared-bit
Extended PAN ID boundary patterns, and 24-bit offset byte boundaries.
Host-only tests vary every payload byte through all 256 values, exercise
every uint16 length, and use exact-sized allocations under ASan/UBSan.
The target verifier reuses the strict component CODE/CDB/XDATA/stack
accounting, including the 512-byte reservation budget, the IRAM alias and
the untouched upper-IRAM guard.

The existing MAC test image additionally executes the real
MAC-frame -> MAC-Beacon -> NWK-payload path for both source-address sizes
with mixed pending-address lists. A non-Zigbee payload is accepted only at
the MAC layers and rejected at the NWK boundary without changing the result.
All existing MAC tests and guards remain enabled.

Evidence is **host-tested, image-checked and simulated**, not hardware-
observed, a discovered network or interoperability. Primary references and
the public-document mirror revision are in [PROVENANCE.md](PROVENANCE.md).
The selected BDB 3.0.1 base revision and remaining errata/implementation gates
are recorded in [CONFORMANCE.md](CONFORMANCE.md); decoding does not satisfy them.

## NWK Data frame codec

`include/nwk_frame.h` and `src/nwk_frame.c` independently encode/decode a
bounded **NWK Data NPDU**, not Beacon metadata. Input/output excludes the
MAC header, PHY length, FCS and radio metadata. The module has no dependency
on MAC, the Beacon decoder, platform services or a live network.

The supported subset is Data type `0`, protocol version `2`, without NWK
security, multicast control or a source-route subframe. The complete NPDU is
at most **116 bytes**: the 127-byte PHY packet limit minus R22's
`nwkcMACFrameOverhead = 11`. Optional fields still consume that same bound.
This is not a promise that a 116-byte NPDU fits every supported MAC header.

The separate [security envelope](ZIGBEE_SECURITY.md) adds/verifies supported
NWK Data protection through the real AES service, reusing this parser.
Authenticated open returns a normalized unsecured copy; the caller retains
its successful security context and still owns replay, key and network
admission. This bare codec remains unchanged and rejects protected input.

| Offset | Field | Supported treatment |
| --- | --- | --- |
| 0..1 | Little-endian Frame Control | Type 0, version 2; Discover Route 0/1 for unicast, 0 for broadcast |
| 2..3 | Destination network address | Unicast `0000..FFF7`, or defined broadcasts `FFFB`, `FFFC`, `FFFD`, `FFFF` |
| 4..5 | Source network address | `0000..FFF7`; never a broadcast/reserved address |
| 6 | Radius | Preserve all 0..255; no decrement, forwarding or lifetime decision |
| 7 | Sequence | Preserve all 0..255; no increment or duplicate/freshness processing |
| 8 onward | Optional IEEE addresses | Eight destination octets first, then eight source octets, according to flags |
| 8, 16 or 24 onward | Payload | Opaque remainder, including zero length; no APS/ZDO/ZCL validation |

FCF bits 8 (multicast) and 10 (source route) return an unsupported-layout
error; bit 9 (security) returns an unsupported-security error. Bits 11/12
indicate destination/source IEEE address presence. Bit 13, End Device
Initiator, is preserved as metadata; setting it correctly needs actual
source-role and `nwkParentInformation` state, which this codec does not own.
Reserved FCF bits 14/15 and Discover Route values 2/3 are rejected.
`nwk_header_t.flags` contains only the high-octet flags, not type/version/route
bits. Unsupported flag constants exist to identify rejected inputs, not
advertise those features.

Broadcasts cannot carry a destination IEEE address or enable route discovery.
`FFF8..FFFA` and `FFFE` are rejected as destinations. Accepting `FFFB`/`FFFC`
metadata does not mean an ED belongs to those broadcast recipient groups.
IEEE arrays retain octets in wire order, including all-zero/all-`FF` values;
there is no identity validity, short-to-IEEE mapping or address-conflict check.
Absent arrays decode as zero and are ignored on encoding. Strict rejection
and the unsecured-only subset are not full R22 reception/transmission policy.

### Data API and ownership

`nwk_frame_decode(body, length, result)` returns a `nwk_frame_info_t` containing
the typed header and payload offset/length relative to the supplied NPDU.
`nwk_frame_encode(header, payload, payload_length, body, capacity, length)`
serializes the typed header and opaque payload. Length/capacity inputs are
uint16; bounded returned lengths/offsets are uint8. Native C struct layout
is not a wire format.

Both return `nwk_codec_result_t`. Null required pointers are invalid; an
encoder payload pointer may be null only when its length is zero. Incomplete
fixed/optional headers, oversized frames, insufficient output capacity,
unsupported type/version/security/layout and invalid static header fields
have explicit error results. NWK commands, Inter-PAN and reserved frame types
are not treated as Data. No decoder result or encoder output byte/length
changes on failure; error precedence for simultaneous faults is not an ABI.

The caller owns truthful buffer spans, which must not overlap the other
input/output objects. There is no heap, retained pointer or network state.
The decoder copies metadata but not payload. CODE and XDATA inputs use the
SDCC generic-pointer ABI; calls are foreground-only and non-reentrant.
`NWK_CODEC_OK` means only supported syntax, **not authenticated or sendable
Zigbee traffic**. In particular, this API is not a security-disabled join path.

After a successful MAC decode and explicit `MAC_FRAME_DATA` type check:

```c
nwk_frame_decode(body + frame.payload_offset, frame.payload_length, &network);
```

Only after that result succeeds may the caller consume the next-layer slice
at `body + frame.payload_offset + network.payload_offset`. No layer guesses
or strips the other header. Encoding likewise checks both results: a larger
MAC MHR can reject an otherwise valid NPDU. The 9-byte compressed short/short
MAC MHR can carry 116 NPDU bytes in a 125-byte FCS-free body; compressed
short/extended or extended/extended MHRs reduce the NPDU budget to 110 or
104 bytes respectively.

### Data offline evidence

`make test-nwk-frame`, also included in `make ... all test`, runs the strict
host suite and `nwk_frame_test.ihx` under alias-aware s51. This is not an
`IMAGE` selection, board firmware or CI upload artifact.

Shared host/SDCC cases cover original CODE golden frames, XDATA inputs,
all IEEE/initiator/route flag combinations with unicast/broadcast policy,
8/16/24-byte header truncations, every payload length up to the 116-byte
bound, capacities, reserved addresses/flags and unchanged errors.
Radius/sequence boundary values and raw zero/all-`FF` IEEE arrays are retained.
Host matrices additionally exhaust both unicast and broadcast FCF spaces
(16 and four accepted patterns respectively), all 65,536 typed flag values,
source/destination addresses, uint16 lengths, and every scalar/IEEE/payload
byte value. Exact-sized input, payload, result and output allocations run
under ASan/UBSan.

The standalone verifier checks linked CODE/source/result ABI, the unchanged
512-byte XDATA reservation budget, all allocation boundaries, the IRAM alias,
untouched upper IRAM and final stack unwind. The full MAC test image also
executes both real codecs for all four MAC address-size combinations and
all four NWK IEEE layouts, reaching the exact 125-byte MAC body boundary.
An oversized NPDU for an extended MAC MHR is rejected without output changes;
MAC-valid security/command payloads are rejected at the NWK boundary.
All prior MAC and Beacon scenarios remain enabled.

Evidence is **host-tested, image-checked and simulated**, never RF,
authentication, routing or interoperability. See the
[primary frame sources](PROVENANCE.md#offline-r22-nwk-data-frame-sources).

The separate [APS Data codec](APS.md) now consumes a successfully decoded
NWK payload in `protocol_frame_test.ihx`. That image composes real MAC/NWK/APS
codecs with independent length and error checks; neither this NWK module nor
the existing MAC test image gains an APS service or firmware caller.
