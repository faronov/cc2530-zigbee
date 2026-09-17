# Offline NWK Beacon payload decoder

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
The compatible-BDB gate remains open in [CONFORMANCE.md](CONFORMANCE.md).
