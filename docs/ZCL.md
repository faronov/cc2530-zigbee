# Offline ZCL wire codecs

`include/zcl_wire.h`, `src/zcl_frame.c` and `src/zcl_value.c` implement
independent header and wire-value codecs against **ZCL Revision 8**,
document **07-5123-08**, released December 2019. The exact official PDF and
foundation chapter revision are pinned in [PROVENANCE.md](PROVENANCE.md#zcl-revision-8-wire-sources).
Its referenced approved errata **19-2019** has not been obtained/reviewed.
The selected base text is not an errata-aware conformance claim.

This is preparatory M6 work alongside Core R22/BDB 3.0.1, not a claim that
R22 universally mandates this ZCL revision. Application/device definitions,
profile requirements, command handlers, attributes, reporting and persistence
are not implemented or advertised. No module is linked into board firmware.

## Frame header

Offsets start at the ZCL frame, not APS/NWK/MAC:

| Field | Size | Treatment |
| --- | --- | --- |
| Frame Control | 1 | Type bits 0..1: global `0` or cluster-specific `1`; reject types `2/3` |
| Manufacturer code | 0 or 2 | Little-endian, present only with bit 2 |
| Transaction sequence | 1 | Raw byte, no transaction allocation/matching |
| Command ID | 1 | Raw byte, no command dispatch or payload validation |
| Payload | Remaining bytes | Opaque, including empty payload |

Bit 3 records server-to-client versus client-to-server direction. Bit 4
records Disable Default Response; it does not generate or suppress an actual
response. Immediate response rules, command-specific direction and response
policy belong to future handlers.

Reserved control bits **must not inherit the NWK codec's rejection policy**:
ZCL R8 sections 2.3.1-2 require zero on transmission and ignoring reserved
sub-fields on non-manufacturer-specific reception. Decode therefore records
bits 5..7 in `ignored_control_bits` and removes them from the usable header.
Re-encoding that header emits zero reserved bits. For manufacturer-specific
frames, interpretation is manufacturer-defined; this subset returns
`ZCL_CODEC_UNSUPPORTED_LAYOUT` if any such bit is set. Encoding rejects
reserved bits and attempts to inject type bits through `header.flags`.

Manufacturer codes are retained without checking allocation or recognition.
Absent codes decode as zero and are ignored on encode. Command IDs retain
all byte values, including unknown/reserved IDs: this codec identifies the
header, not a legal command to execute/transmit. A future dispatcher must
apply the global-command table, cluster-specific rules and manufacturer
recognition before handling requests. Frame success is not command support.

`zcl_frame_decode(body, length, result)` copies metadata and returns the
payload offset/length. `zcl_frame_encode(header, payload, payload_length,
body, capacity, length)` emits the three- or five-byte header and payload.
The **100-byte raw frame cap** follows the existing APS codec's 108-byte cap
minus its eight-byte header. This leaves at most 97/95 opaque payload bytes.
It is not an APSDE-DATA, secure-network or application-payload guarantee:
actual lower-layer headers, security, service limits and record overhead
reduce the usable budget. Fragmentation is not implemented.

## Typed wire values

The value codec supports these **38 type IDs**:

| Types | IDs | Value bytes, excluding any type byte |
| --- | --- | --- |
| No data | `00` | Zero |
| General data, 8 through 64 bits | `08..0F` | 1..8 |
| Boolean | `10` | One: `00`, `01` or the `FF` non-value; reject `02..FE` |
| Bitmap, 8 through 64 bits | `18..1F` | 1..8 |
| Unsigned integer, 8 through 64 bits | `20..27` | 1..8 |
| Signed integer, 8 through 64 bits | `28..2F` | 1..8 |
| Enumeration, 8/16 bits | `30/31` | 1/2 |
| Short octet/character string | `41/42` | One length byte plus 0..254 data bytes |

All other types, including unknown `FF`, floats, long strings, arrays,
structures, sets/bags, time/identifier/address/key types, return an explicit
unsupported-data-type error. There is no guessed width or successful skip.

Fixed values are octets **in wire order**, not native C integers. This
preserves 24/40/48/56/64-bit values and signed bit patterns without requiring
64-bit arithmetic or unsafe native-struct serialization on SDCC.
Numeric conversion, attribute-specific ranges, enum membership and reserved
bitmap bits are not validated by this layer.

`zcl_value_decode(type, body, available, result)` decodes **one** value;
the type ID is supplied separately. `data_offset`/`data_length` identify
the data span, and `encoded_length` tells the caller how many bytes were
consumed. Trailing bytes are allowed for subsequent records. The available
span can be any uint16 length, but this subset consumes at most 255 bytes.
No-data consumes zero bytes: the enclosing record parser must still advance
past its own identifier/type fields, not loop on `encoded_length` alone.

`zcl_value_encode(value, body, capacity, length)` takes a `zcl_value_t`
with a type, raw data pointer/length and `string_non_value` selector. It emits
no type byte. Fixed types require the exact data width and a zero
`string_non_value`. To send a fixed non-value pattern, supply its actual
wire bytes. Strings take data without their prefix; lengths above 254 fail.
`string_non_value = 1` requires zero data length and emits prefix `FF`.
All other nonzero selector values fail rather than being coerced to true.

Empty strings (`00`) and non-value strings (`FF`) both have no data but
remain distinguishable. For numeric types, `non_value_pattern` only reports
a match to the table's pattern (all ones for unsigned/enum, sign bit alone
for signed). **It does not automatically mean unavailable or invalid**:
R8 permits a field to use the full unsigned range without a non-value.
General data and bitmaps have no non-value pattern. Boolean `FF` and string
prefix `FF` retain their explicit non-value representations.

Character strings are byte-counted spans, not NUL-terminated C strings;
embedded zero bytes remain data. R8 uses UTF-8 by default unless another
encoding is specified by the complex descriptor. This codec preserves bytes
without validating UTF-8 or interpreting that descriptor. Text acceptance,
normalization, display and application limits remain separate.

## Ownership and errors

Both modules use `zcl_codec_result_t`. Required null pointers, truncation,
oversized frames/strings, insufficient output capacity, unsupported formats
and invalid fields have explicit errors. The value decoder requires a
non-null body even for No-data with zero available bytes. Frame payload and
value data pointers may be null only when their supplied lengths are zero.

No decoder result or encoder output byte/length changes on failure. Error
precedence with simultaneous faults is not an ABI. Callers supply truthful,
accessible spans; all input/output objects must be non-overlapping. Returned
views contain offsets, not retained pointers. Native structs are not a wire
format. CODE/XDATA input pointers use the SDCC generic-pointer ABI. Calls are
foreground-only and non-reentrant. There is no heap, MMIO or network state.

## Offline evidence

```sh
make test-zcl-frame
make test-zcl-value
make test-protocol-frame
```

All are included in `make ... all test`. The two new images,
`zcl_frame_test.ihx` and `zcl_value_test.ihx`, are simulator-only: not board
`IMAGE` selections, firmware to flash or CI upload artifacts.
Both retain the existing **512-byte XDATA reservation budget**, strict
CODE/CDB/result accounting, alias, upper-IRAM and unwind guards.

Shared frame tests cover original CODE golden headers, all 256 FCF values
(72 accepted patterns under the receive policy), both header lengths, every
payload length/capacity boundary and unchanged errors. Host matrices exhaust
typed control fields, manufacturer codes, sequence/command bytes, payload
byte values and uint16 lengths/capacities, using exact allocations under
ASan/UBSan. The real APS/ZCL composition is also executed on SDCC, with
maximum APDUs, truncations and layer-specific failures.

Shared value tests cover all 38 supported and all other type IDs, every
Boolean value, fixed-width patterns, all short-string lengths, empty versus
non-value strings, capacities and unchanged errors. Host matrices add every
value of each scalar/string byte, all 16-bit scalar patterns where applicable
and every uint16 available/data-length/capacity value with exact allocations.

The value image runs four independently initialized phases: fixed types,
octet strings, character strings, and rejection/argument cases. The simulator
sets only the verified ordinary-XDATA `zcl_value_test_phase` selector after
CRT startup; the eight-byte result identifies the phase as `ZCV0..ZCV3`,
version 1, size 8, followed by a little-endian failure line. A fifth run
requires an explicit failure for invalid selector 4 (`ZCV4`).
This preserves every case while retaining the shared **15-second per-run
timeout**, rather than relaxing it for one long combined run.

The host protocol test composes actual value/ZCL/APS/NWK/MAC encoders and
decoders, including an independent complete golden byte vector, both ZCL
header lengths, all four MAC and four NWK optional-address layouts, maximum
125-byte MAC bodies and failures at ZCL/value boundaries. It manually builds
synthetic Report Attributes records solely as wire vectors, not a reporting
engine or an advertised cluster. The existing `protocol_frame_test.ihx`
remains a three-layer MAC/NWK/APS image with its unchanged 1,024-byte budget;
it is not claimed as a four-layer target run.

Evidence is **host-tested, image-checked and simulated**, not hardware
observation, authenticated traffic, working clusters, interview or
interoperability. [Errata and application gates](CONFORMANCE.md) remain open.
