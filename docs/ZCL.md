# Offline ZCL foundation

`include/zcl_wire.h`, `src/zcl_frame.c` and `src/zcl_value.c` implement
independent header and wire-value codecs against **ZCL Revision 8**,
document **07-5123-08**, released December 2019. The exact official PDF and
foundation chapter revision are pinned in [PROVENANCE.md](PROVENANCE.md#zcl-revision-8-wire-sources).
Its referenced approved errata **19-2019** has not been obtained/reviewed.
The selected base text is not an errata-aware conformance claim.
Unreviewed errata is an open follow-up risk, not a blanket development stop:
base-text implementation may proceed, with possible later corrections.

This is preparatory M6 work alongside Core R22/BDB 3.0.1, not a claim that
R22 universally mandates this ZCL revision. Application/device definitions,
profile requirements, complete clusters, reporting and persistence
are not implemented or advertised. A bounded generic read-only attribute
model, Read/Discover Attributes handlers and one-cluster unicast command
dispatcher are implemented below.
No module is linked into board firmware.

The separate [read-only Basic provider](ZCL_LAB.md) now constructs six
primary-backed attributes in caller-owned storage and uses these same
Read/Discover handlers. Its synthetic lab configuration assigns no
manufacturer code, endpoint, profile or device ID. Basic reset,
measurement/reporting and authenticated application integration
remain unsupported; a Basic table is not full cluster conformance.

The separate [Identify procedure](ZCL_IDENTIFY.md) adds a caller-owned
logical-time countdown, unicast Identify/Query and fresh IdentifyTime/
ClusterRevision views through these same Read/Discover handlers. It provides
explicit response/silence and atomic local failures, not physical indication,
client/group/broadcast handling, network sends or an authenticated endpoint.
IdentifyTime is specified RW. The bounded [shared write path](ZCL_WRITE.md)
now applies its writes and distinguishes Basic read-only, incorrect-type and
missing-attribute errors. Unknown value extents remain explicitly unsupported.

The separate [synthetic Temperature model](ZCL_TEMPERATURE.md) adds four
read-only attributes through the same handlers, caller-fed measurements and
one bounded reportable value. Configure/Read Reporting Configuration and
logical interval/change state produce real Report Attributes bytes.
A pending snapshot is committed only on caller-confirmed issuance; preparing
or cancelling it does not count as a send. Caller defaults, binding resolution,
transport and authorization are explicit boundaries. No physical sensor,
report reception engine, persistence, profile/endpoint or conformance is added.

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
policy belong to handlers, not the framer.

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
header, not a legal command to execute/transmit. A dispatcher must
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

## Read-only attributes and Read Attributes

`include/zcl_attributes.h` / `src/zcl_attributes.c` add
`zcl_read_attrs_unicast(set, request, request_length, response, capacity, info)`.
This is a **response builder for an already selected unicast cluster instance**,
not a network dispatcher. The caller must first establish permitted unicast
delivery to one endpoint, profile/cluster selection and any required
authentication. Broadcast, multicast, all-endpoints delivery, endpoint
registration and general unsupported-command responses are outside this API.
No authentication-success flag substitutes for those missing services.

A caller-owned `zcl_attribute_set_t` describes one cluster side and one
standard/manufacturer namespace, with **0..16** attribute descriptors.
`side` is `ZCL_ATTRIBUTE_SERVER` (`0`) or `ZCL_ATTRIBUTE_CLIENT` (`1`);
`manufacturer_specific` and `readable` are exact `0/1` selectors. Attribute
IDs must be unique within the set. Standard declarations allow `0000..4FFF`
and global IDs `F000..FFFE`; `5000..EFFF` and `FFFF` are reserved by R8
section 2.6.1.4, Table 2-8 (p.2-44). Manufacturer-specific sets retain the
full 16-bit ID range. Empty sets may have a null table pointer.
Duplicate IDs, reserved standard IDs, oversized tables and invalid selectors
return `ZCL_CODEC_INVALID_TABLE` before lookup or output publication, even
if the bad entry was not requested or is non-readable.
Standard and manufacturer namespaces may use separate sets with the same IDs.
There is no automatic fallback from a manufacturer request to standard data.

Each descriptor supplies an ID, a static read-access gate and a `zcl_value_t`
using the same 38 wire types. Descriptor/value storage may be in CODE or XDATA.
The application owns the backing values and must keep them stable throughout
the call; later calls observe later values. The library does not retain
pointers, allocate a registry, call getters, write attributes, validate
cluster-specific ranges or create mandatory global/device attributes.

Only global command `00` is handled. Incoming direction must select the
configured cluster side; the manufacturer flag and, when present, code must
match exactly. Other commands and context mismatches return explicit local
`UNSUPPORTED_COMMAND` / `UNSUPPORTED_CONTEXT` errors without an output frame.
The separate dispatcher below supplies a bounded command-selection and
unsupported-command policy; the direct Read API retains these local errors.
Unknown-cluster/delivery policy remains outside both APIs; neither is a
complete network-facing ZCL endpoint.

A request needs one or more complete little-endian 16-bit IDs. An empty list
or odd trailing byte generates unicast **Default Response `0B`,
MALFORMED_COMMAND `80`**, without reading any attribute values. Otherwise,
Read Attributes Response `01` retains request order, including duplicate
requested IDs. Each record starts with ID and status:

| Condition | Response record |
| --- | --- |
| Readable, supported value fits | `SUCCESS 00`, then type and encoded current value |
| ID absent from the selected set | `UNSUPPORTED_ATTRIBUTE 86`, no type/value |
| Entry has `readable = 0` | `NOT_AUTHORIZED 7E`, no type/value; value descriptor/data are not inspected |
| Valid value record does not fit | `INSUFFICIENT_SPACE 89`, no type/value |

The declaration restriction does not reject received unknown/reserved IDs:
an absent ID is echoed unchanged with `86`, including `5000..EFFF` and `FFFF`.
General wire codecs continue to carry full 16-bit IDs.

R8 deprecates `WRITE_ONLY 8F` in favor of `NOT_AUTHORIZED`; deprecated status
values must not be transmitted. The static gate is not an authorization
engine. An invalid/unsupported **readable value reached during processing**
is a local codec error, not a fabricated successful or unsupported-attribute
record. Malformed requests, denied entries and trailing records beyond the
space cutoff never inspect those values.

`capacity` is the caller's **complete response budget**, capped at 100 bytes,
not a promise that 100 bytes fit authenticated APS/NWK/MAC transport.
It must reflect actual lower-layer overhead. Values that do not fit become
three-byte space-error records; if even the next three-byte record cannot
fit, processing stops at that request prefix as prescribed by R8
2.5.1.3 / 2.5.2.2. The caller receives explicit `requested_count` and
`returned_count`, not a silently complete-looking list. No fragmentation,
automatic follow-up request or retained continuation exists.
Budgets unable to fit a header plus one status record return local
`BUFFER_TOO_SMALL`, rather than a zero-progress Read Attributes response.

Both response kinds copy the transaction sequence/manufacturer context,
reverse direction, set Disable Default Response and clear standard reserved
bits. Request Disable Default Response does not suppress the specific read
response or a malformed-command error response. `info` reports the emitted
`length`, `command_id` and counts; both counts are zero for a malformed
request's Default Response. `ZCL_CODEC_OK` means **a response was built**,
not that all attributes were read or any packet was sent.

Responses are assembled in private bounded storage and published only after
all reached records succeed locally. Any local error leaves the entire
response buffer and `info` unchanged, including after earlier valid records.
Outputs must not overlap each other or input storage. Calls remain
foreground-only and non-reentrant; there is no heap, MMIO, security/NV stub,
registered device, mandatory-cluster claim or RF operation.

## Discover Attributes and unicast dispatch

`include/zcl_dispatch.h` / `src/zcl_dispatch.c` add
`zcl_dispatch_unicast(set, request, request_length, response, capacity, info)`.
The caller selects **one existing cluster side on one unicast endpoint**,
applies profile/delivery/authentication requirements, and supplies its one
attribute namespace. This is not an APS/endpoint registry, broadcast handler,
unknown-cluster service, transaction engine or permission to transmit.

| Incoming command | Bounded result |
| --- | --- |
| Global Read Attributes `00`, matching namespace | Existing Read handler, unchanged semantics |
| Global Discover Attributes `0C`, matching namespace | Sorted ID/type page in Discover Attributes Response `0D` |
| Global Default Response `0B`, matching namespace | Received command/status notification; **no reply** |
| Global Write `02` / Undivided `03`, matching namespace | Shared record parser and Write Response `04`; this generic table is read-only |
| Global Write Attributes No Response `05`, matching namespace | Process records; `SILENT` on supported processing, **no reply** even for errors; [local failure contract](ZCL_WRITE.md) |
| Other global or cluster-specific command | Default Response `0B` with original command ID and `UNSUP_COMMAND 81` |
| Standard/manufacturer namespace or manufacturer-code mismatch | No handler execution; `UNSUP_COMMAND 81`, except Default Response and No Response (the latter returns `UNSUPPORTED_NO_RESPONSE`) |

Direction must match the selected side; a mismatch is local
`UNSUPPORTED_CONTEXT`, not table reselection. An unrecognized Default
Response namespace also returns that error without an output frame.
Cluster-specific IDs `05`/`0B` are not the global no-response commands.
Unsupported response commands are not silently accepted. Error Default
Responses are not suppressed by Disable Default Response. All generated
responses use global type, reverse direction, echo the transaction and
manufacturer context, set Disable Default Response and zero reserved bits.

Discover requests contain a little-endian start ID and one-byte maximum.
Fewer than three payload bytes generate `MALFORMED_COMMAND 80`.
R8 2.3.2 requires ignoring additional octets in standard fixed-format
commands: Discover and received Default Response ignore trailing bytes
within the 100-byte frame bound. Manufacturer extensions are not guessed:
extra bytes in those two manufacturer commands return local
`UNSUPPORTED_LAYOUT`; reserved manufacturer FCF bits retain the framer's
existing rejection. Read's variable ID-list contract is unchanged.

Discovery lists every declared attribute, **including non-readable entries**,
in increasing numeric ID order, starting at the first ID at least the
requested start. It reads ID/type metadata, never backing value bytes,
lengths or non-value selectors. Tables may be unsorted; they are not changed.
The existing 16-entry/unique-ID and standard declaration-range checks are
shared via `zcl_attr_set_check`, before any Read/Discover output is published.
Emitted types must belong to the existing 38-type subset, checked through
`zcl_value_type_supported`; an unsupported emitted type is a local error,
not a skipped record or invented type. Later, unreturned entries are not
type-validated. A denied Read descriptor may therefore be acceptable to
Read while requiring a real supported type before discovery can expose it.

The response payload is a one-byte **Discovery Complete** followed by
three-byte `(ID, type)` records. Page length is bounded by the requested
maximum, the table and the caller's total response budget (at most 100).
Complete is `1` only when no eligible attributes remain after the page.
It remains `0` at a request-count or capacity cutoff. Empty/beyond-end
discovery is complete; a manufacturer-specific declared ID `FFFF` terminates
without wrapping to zero. A standard request starting at `FFFF` is an empty,
complete page, not a malformed request.
The base request field does not prohibit maximum `0`: this implementation
returns an empty page, complete only if no eligible attributes exist.
For a positive maximum and nonempty eligible set, a budget unable to fit
one record returns `BUFFER_TOO_SMALL`, avoiding accidental zero progress.
The caller can request another page starting at the last ID plus one;
there is no retained cursor, automatic request or fragmentation.

`zcl_dispatch_info_t` makes no-transmission outcomes explicit:
`kind = ZCL_DISPATCH_RESPONSE` means `response[0..length)` was constructed;
`kind = ZCL_DISPATCH_DEFAULT_RECEIVED` means only `info` was published,
`length = 0`, and the entire response buffer is untouched (capacity may be
zero, but the pointer remains required). The latter is a notification of
the received `sequence`, `default_command`, `default_raw_status` and
normalized `default_status`, **not successful transaction matching**.
`kind = ZCL_DISPATCH_SILENT` is supported Write No Response processing;
only kind/sequence are populated, not an assertion that every record wrote.
Deprecated statuses are mapped as required by R8 Table 2-12:
`82..84 -> 81`, `8A/C4 -> 00`, `8F -> 7E`,
`90/91/93/C0/C1 -> 01`. Other status bytes are retained as metadata,
including unknown/reserved ones, never coerced to success.
Malformed Default Responses return local errors and never trigger a reply.

For constructed replies, `command_id` is the emitted command, `sequence`
is echoed, and read/discovery counts retain their respective meanings.
Discovery's `requested_count` is the request maximum, not a total table size.
`discovery_complete` is meaningful only for `0D`; default fields are
meaningful for `0B`. Unused metadata is zeroed. Every local error, including
namespace-mismatched no-response writes and invalid metadata after a valid prefix,
leaves both outputs unchanged. No successful write/security/NV stubs exist.
All prior stable-storage, non-overlap and foreground-only contracts apply.

This bounded dispatcher is **not a conforming complete cluster**: R8 2.5
requires complete applicable foundation behavior. Bounded writes now exist,
but unsupported value extents, device/profile selection and errata review
remain necessary before conformance claims.

## Offline evidence

```sh
make test-zcl-frame
make test-zcl-value
make test-zcl-attributes
make test-zcl-dispatch
make test-zcl-basic
make test-zcl-identify
make test-zcl-temperature
make test-protocol-frame
```

All are included in `make ... all test`. The frame and value images,
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

The separate `zcl_attributes_test.ihx` exercises the real model, value codec,
framer and read handler, with a **1,024-byte test-harness reservation budget**.
With the unchanged declaration-range regression cases and the refactoring
below it has 13,082 CODE bytes and
666 ordinary XDATA bytes (730 including the
64-byte status reservation). Existing 512-byte images and the shared
15-second timeout are unchanged; CODE/source/result, alias, unused-XDATA,
upper-IRAM and unwind checks remain mandatory. Its eight-byte `ZCA1` result
uses version 1, size 8 and a little-endian failure line. It is not a board
`IMAGE`, flash target or CI artifact.

## Production CODE headroom

The #76 refactoring changes only production `zcl_value`, `zcl_attributes`
and `zcl_dispatch` C. It adds no public API, type, command or behavior.
Against accepted #74/main `2a7299a`, the real SDCC 4.2.0 integrated link saves
**1,034 CODE bytes**, including **861 bytes in production ZCL objects**.
The synthetic caller, vectors and all other production objects are unchanged:
their relocatable contents match the baseline after the build-directory
`;!FILE` annotation, not merely their sizes.

| Object | CODE before / after | Ordinary XDATA before / after | Persistent IRAM before / after |
| --- | ---: | ---: | ---: |
| ZCL frame | 1,173 / 1,173 | 35 / 35 | 6 / 6 |
| ZCL value | 1,710 / 1,345 | 46 / 40 | 15 / 10 |
| ZCL attributes | 2,086 / 1,958 | 148 / 150 | 6 / 7 |
| ZCL dispatch | 3,093 / 2,725 | 136 / 134 | 15 / 11 |
| ZCL write | 1,552 / 1,552 | 144 / 144 | 0 / 0 |
| Other production: MAC/NWK/APS | 11,056 / 11,056 | 381 / 381 | 35 / 35 |
| Synthetic caller/constants | 3,205 / 3,205 | 735 / 735 | 0 / 0 |
| Shared runtime remainder | 700 / 527 | 29 / 20 | Shared |
| **Integrated total** | **24,575 / 23,541** | **1,654 / 1,639** | **77 / 69** |

Object CODE includes constants, not only listed instructions. Shared overlay
remains15 bytes; bit-addressable backing remains one byte. Integrated stack
start moves from66 to5E (hex). The whole image uses1,703 XDATA bytes including
the64-byte status reservation, below the unchanged2,048 cap. CODE headroom
below the unchanged24,576 budget grows from1 to**1,035 bytes**.
An additional ledger guard enforces **CODE <=23,551** and **production ZCL
savings >=512 bytes** against the9,614-byte baseline; synthetic tests reject
both one-byte threshold failures and caller-only savings. Older module,
whole-image, SP, alias and15-second limits are unchanged.

The implementation uses a byte-valued internal kind/width classifier instead
of two generic-pointer outputs, one bounded non-value-pattern loop, cached
stable value fields and pointer-walking attribute lookup. Discover subtracts
three bytes per emitted record instead of dividing the response budget,
preserving zero maximum, insufficient-space and completion rules. Dispatcher
successes share one final metadata publication; local errors still publish
nothing. Removing production division/multiplication references also removes
173 runtime CODE bytes from the integrated link. Basic/dispatcher callers
still need that runtime; their smaller total saving is not hidden.

A frame-copy `memcpy` trial was rejected: it saved only30 CODE bytes and
grew that object's persistent IRAM from6 to23, exceeding its existing cap.
The original frame implementation is retained. No compiler flag, assembly,
table permission, response precedence, write transaction or Identify phase
policy changes are used to meet the target.

| Isolated composition | CODE before / after | Ordinary XDATA before / after |
| --- | ---: | ---: |
| Basic | 21,098 / 20,237 | 1,142 / 1,136 |
| Identify | 24,340 / 23,338 | 1,006 / 993 |
| Dispatcher | 18,596 / 17,735 | 945 / 939 |

These are compiled-image measurements; actual execution and full-run SP are
checked in GitHub Actions, not inferred from allocation. Separate uninterrupted
SP measurements in both-board CI observed72 for the integrated image
(previously7A),5C for Basic (previously5E) and6A for Identify (previously6E),
distinct from their5D/3F/4D checkpoint values. All original guards remain unchanged.
Canonical targets and link order are unchanged. All earlier native/sanitizer and SDCC corpora,
including370 Basic and102 Identify common cases, are retained without C test
edits. Complete raw-CDB-before-decode, map, CODE, immediate ordered listings,
field/pointer/return ABI, caller/runtime boundaries and negative controls
remain mandatory. The compact private value helpers additionally have explicit
ABI/actual-call and metadata-mutation checks. The standalone value image is
6,459 CODE/371 ordinary XDATA and retains all five simulator phases.

This remains offline component evidence, not hardware timing, RF, full-stack
fit or increased ZCL/application conformance. Write-family limitations and
the existing primary/errata/security gates are unchanged.

Shared read tests cover independent golden records, CODE/XDATA tables and
values, changed backing values, both namespace/header layouts and directions,
duplicate request IDs, duplicate table rejection, denied and missing IDs,
all data type IDs, local failures after a valid prefix, malformed requests,
No-data and short-string non-values, large valid values, exact space-error
versus prefix-cutoff behavior and unchanged errors. Host matrices additionally
cover every 16-bit attribute/manufacturer ID, command/sequence/control byte,
all 0..16 table sizes with exact allocations, request/capacity boundaries
and every uint16 length/capacity under ASan/UBSan. The protocol host suite
feeds actual decoded MAC/NWK/APS requests to the handler and encodes/decodes
the complete response, with an independent whole-frame golden vector and
both manufacturer layouts. Its reverse-hop metadata is synthetic, not
routing, counter allocation or a network transaction implementation.

Shared declaration cases check `4FFF/5000/EFFF/F000/FFFE/FFFF` in both
namespaces, including unrequested/denied invalid entries and unchanged
outputs. Unknown request IDs retain their negative-response echo.

The separate `zcl_dispatch_test.ihx` links the actual dispatcher, Read handler,
frame and value codecs: **16,428 CODE, 800 ordinary XDATA, 864 including status
reservation**, under its explicit 1,024-byte harness budget. It checks the
same CODE/CDB/alias/unused-XDATA/upper-IRAM/unwind rules and 15-second timeout.
The eight-byte result is `ZCD1`, version 1, size 8, failure line LE16.
The original component budgets remain unchanged; the value image now uses
6,824 CODE and 377 ordinary XDATA bytes, within its 512-byte reservation.
Discovery uses a bounded next-ID scan to shorten generic-pointer spill
lifetimes on SDCC; test-only volatile loop temporaries avoid excess permanent
IRAM spills. No memory/timeout guard was relaxed to make the image fit.

Shared dispatch cases cover CODE/XDATA and unsorted tables, inclusive starts,
multi-page discovery, manufacturer-specific `FFFF` completion, zero maximum,
count/capacity cutoffs, empty sets, unreadable metadata without value access,
unsupported types and atomic failures, Read dispatch, malformed/extended
requests and no-response rules.
Host matrices additionally cover all 65,536 declared IDs in both namespaces
through table validation and Read/Discover dispatch, every 16-bit
start/manufacturer ID, every maximum for all 0..16 table sizes and capacities
0..102, all FCF/command pairs,
all Default Response command/status pairs, every type ID, and uint16
lengths/budgets. Exact allocations are checked under ASan/UBSan.
The host protocol chain now performs actual Discover dispatch, decodes the
returned ID, and uses it in a subsequent Read transaction through
MAC/NWK/APS/ZCL. Both manufacturer layouts and independent complete golden
discovery/read response frames are checked. The target protocol image
remains the three-layer MAC/NWK/APS chain; all new images remain excluded
from board selections and CI artifacts.

`protocol_budget_test.ihx` is a new, separate **full-chain SDCC image**:
MAC/NWK Data/APS/ZCL/value/Read/Discover are linked and executed together.
It checks Discover-then-Read for both manufacturer layouts, independent
standard whole-frame golden responses, maximum-size string success,
space errors, negative Read echo for absent `FFFF`, unchanged no-response
output and atomic Read/Discover rejection of a CODE table declaring standard
`FFFF`. Its generated
[resource ledger](ARCHITECTURE.md#integrated-protocol-resource-budget)
records 22,829 CODE, 1,500 ordinary XDATA, stack start `0x66` and observed
peak SP `0x7A`. Its own 2,048-byte reservation does not change prior image
limits. The five-byte observed upper-IRAM headroom is not an ISR or
complete-stack guarantee. Leaf emitters and selected volatile pointer
copies reduce persistent IRAM spills without changing codec semantics.
The affected Read, dispatch and integrated figures were reproduced on both
board selections with SDCC 4.2.0 #13081 (Mac OS X x86_64); limits are unchanged.

Evidence is **host-tested, image-checked and simulated**, not hardware
observation, authenticated traffic, working clusters, interview or
interoperability. [Errata and application gates](CONFORMANCE.md) remain open.
