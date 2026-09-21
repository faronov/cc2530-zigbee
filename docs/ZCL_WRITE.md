# Bounded foundation writes: Basic and IdentifyTime

This offline addition implements Write Attributes, Write Attributes Undivided
and Write Attributes No Response over the existing frame/value/attribute path.
Basic retains its six read-only attributes and immutable caller-owned model.
IdentifyTime is **RW**: accepted writes change the actual logical Identify
procedure, not a detached attribute copy. No physical indication, send,
authenticated endpoint, reporting, persistence or application ID is supplied.

## Primary decisions

The primary is [ZCL R8, 07-5123-08, December 2019](https://csa-iot.org/wp-content/uploads/2022/01/07-5123-08-Zigbee-Cluster-Library-1.pdf),
Foundation **14-0126-17**, General **14-0127-21**, 1,213 PDF pages, SHA-256
`ad536e1d95a40ca27532e360b124cd76a1b18d96398c1c19fd32fbf7dcdd1aa0`.
These clauses were read directly; page references are printed / PDF:

| Clause/table and pages | Implemented decision |
| --- | --- |
| 2.5/Table2-3, 2-10 / 62 | Global command IDs: Write `02`, Undivided `03`, Response `04`, No Response `05`. Attribute-bearing clusters require writes; a read-only table was not sufficient. |
| 2.5.3.1/Figures2-10/11, 2-14..15 / 66–67 | A nonempty record list; each record is `attribute ID LE16, type uint8, type-defined value`. There is no record count, terminator, alignment padding or per-record value-length field. |
| 2.5.3.3, 2-15 / 67 | Check each parsed record in order: existence, matching type, read-only permission, current write admission, range, ability to support the value. Failure records retain input order; valid ordinary writes still apply when other records fail. |
| 2.5.4, 2-16 / 68 | Undivided uses the same syntax and response, but **none of the requested writes apply if any record fails**. |
| 2.5.5/Figures2-12/13, 2-16..17 / 68–69 | Response contains only failures as `status, ID LE16`. All-success is exactly one `00` byte, with no ID. Never include success records in a mixed result. |
| 2.5.6, especially2.5.6.3, 2-17..18 / 69–70 | No Response attempts the writes, skips denied/invalid attributes, and sends **no response, error response or Default Response**. It is not an unsupported-success stub. |
| 2.4.1, 2-8..9 / 60–61 | Response echoes TSN/manufacturer namespace, reverses direction, sets Disable Default Response and zeroes reserved bits. Specific Write Response is not suppressed by the request's Disable Default Response bit. |
| 2.3.2, 2-4 / 56;2.5.12, 2-28..29 / 80–81 | Standard reserved RX subfields are ignored. No Response overrides the general default rule. A malformed ordinary/Undivided list gets Default Response `80`, even with Disable Default Response set. |
| Table2-12, 2-55..56 / 107–108 | `00 SUCCESS`, `80 MALFORMED_COMMAND`, `86 UNSUPPORTED_ATTRIBUTE`, `87 INVALID_VALUE`, `88 READ_ONLY`, `8D INVALID_DATA_TYPE`. Malformed commands are not carried out. |
| 3.5.2.2.1/Table3-31, 3-31 / 141 | IdentifyTime `0000` is uint16 RW, full `0000..FFFF`: nonzero enters identification, zero terminates, decrement every second. `FFFF` is valid here. |
| 3.2/Tables3-7/8/16 and2.3.4.5/Table2-1 | The six selected [Basic attributes](ZCL_LAB.md) and ClusterRevision remain read-only; no optional writable Basic attributes are advertised. |

Parsing precedes semantic status selection. A complete Boolean record whose
value is invalid still has a known one-byte extent: a missing attribute gets
`86`, a type mismatch gets `8D`, and a matching read-only attribute gets `88`,
before checking value validity. Denied backing values are never dereferenced.

The variable record list consumes complete additional records, not arbitrary
“extension” bytes. A partial final identifier/type/value is a missing field,
not a distinguishable fixed-command extension; the **whole malformed list**
is rejected before mutation. Existing fixed-length Read/Discover/Identify
receive rules are unchanged.

## Scope, API and atomicity

`zcl_dispatch_unicast` keeps its signature and read/discovery/default behavior.
Its existing table remains read-only: writes receive per-attribute errors
rather than the old `UNSUP_COMMAND`. Nonexistent and reserved *request* IDs
are echoed as missing attributes, not converted into invalid declarations.
Unknown manufacturer namespaces still cannot execute standard handlers.
Wrong direction and malformed frame headers retain existing local failures.

`zcl_wr_handle` is an **internal, prevalidated composition entry point**:
selected table/context, real decoded frame and zeroed private metadata staging.
It shares one parser/serializer, not an endpoint registry or callback framework.
A five-byte `zcl_wr_t` optionally permits one declared, full-range uint16:
`id` input, `value/written` output. NULL means no writable attributes.
Only Identify's internal view supplies this permission for ID0000; its
ClusterRevision stays read-only. Other mutable types/ranges/access policies
are not implemented by this contract.

The cluster stages its next timer before dispatch and commits only after
successful response serialization. All public local failures preserve entire
context/model, output buffer and metadata. No fallible operation follows
publication. No Response can succeed with capacity0 and leaves the response
buffer untouched; storage must nevertheless be nonnull under the existing API.
All objects/regions must be truthful, stable and nonoverlapping; calls remain
foreground-only/non-reentrant, including compiler scratch.

`ZCL_DISPATCH_SILENT=2` extends the generic dispatcher; `ZCL_ID_SILENT` retains
the same value. Silence publishes only kind/sequence, with length0 and other
metadata zero. It does **not** assert that every requested attribute changed.
Write Response metadata counts requested records and serialized status records
(one for all-success). Malformed defaults have zero counts.

The parser reuses the existing 38 supported value types and 100-byte frame cap.
It does not invent extents for unknown/reserved or unsupported complex types:
`UNSUPPORTED_DATA_TYPE` is an explicit local failure, with no prefix mutations
or reply publication. This remains a bounded foundation limitation, **not a
claim of all-type/full ZCL conformance**. Empty/truncated No Response returns
`TRUNCATED` without output or changes. Callers must not synthesize replies to
No Response from these local errors.

## Logical time and transactions

The [existing monotonic-time contract](ZCL_IDENTIFY.md#api-and-exact-time-semantics)
is unchanged: abstract uint32 milliseconds, continuous epoch, true elapsed
less than2^31 between successful calls. No board clock is read.

**Project phase policy:** every accepted IdentifyTime write resets the fractional
phase at that call's `now`, just like Identify, even for unchanged value/TSN.
Records are considered in wire order; the last successful duplicate wins.
Undivided with any error applies no writes. This stages final foreground state;
it does not claim intermediate physical flashes or external callbacks.
Ordinary and No Response keep accepted records despite semantic failures in
other records. Natural timer aging on a successfully processed error response
is independent of rejected writes; it is not rolled back by Undivided.

TSN echo is not duplicate suppression or replay protection. Transport duplicate
filtering, authorization and application routing remain caller responsibilities.
Read/Discover/Query observe the resulting real timer, including exact expiration.

Examples, TSN5A:

```text
Basic:    00 5A 02 00 00 20 08 -> 18 5A 04 88 00 00
Basic:    00 5A 02 00 00 21 08 00 -> 18 5A 04 8D 00 00
Identify: 00 5A 02 00 00 21 07 00 -> 18 5A 04 00; timer=7, phase=0
Identify: 00 5A 05 00 00 21 07 00 -> silence; timer=7, phase=0
Mixed:    00 5A 02 00 00 21 07 00 01 00 00 -> 18 5A 04 86 01 00; timer=7
Undivided changes command02 to03: same error bytes, no requested timer change.
```

## Evidence and resource boundary

Canonical CI paths are `test-zcl-basic`, `test-zcl-identify`,
`test-zcl-dispatch` and `test-protocol-budget`, once per board in `test-common`.
Basic/Identify include nonrecovering ASan/UBSan with exact native allocations.
No duplicate full local matrix is required by the
[CI-first policy](../CONTRIBUTING.md#development-checks).

The original cases remain, with unsupported-write expectations deliberately
replaced. Basic has370 counted common cases; Identify has102, including29
packed independent write vectors. Native tests add exact request/response
capacity/truncation matrices, both reply policies, full failure preservation,
and all65,536 IdentifyTime values with restart, wrap and exact expiry, retaining
the earlier maximum-countdown and20,000-operation clock oracle.

Prepared generic SDCC artifacts use21,098 CODE/1,142 ordinary XDATA (Basic),
24,340/1,006 (Identify),18,596/945 (dispatcher), and24,575/1,654 (integrated).
These are compiled-image measurements; execution is checked separately in CI.
The respective total reserved-XDATA limits remain1,536/1,536/1,024/2,048;
the64-byte status reservation is counted. Basic/Identify and integrated CODE
caps stay24,576; every older module budget is unchanged. The new write module
uses1,552 CODE/144 XDATA/**zero persistent IRAM**, bounded by1,792/160/0.
The integrated image has only **one byte of CODE budget headroom**.

No cases or work moved into an unbudgeted image. Repeated Identify test argument
staging became one real-call helper; equivalent protocol test headers became one
CODE template. Immediate snapshots now cover seven Basic/Identify modules,
six dispatcher modules and nine integrated modules, including `zcl_write`.
Complete CODE/raw-CDB-before-decode/parsed-map and ordered listing identities,
all object allocations, actual write calls/pointer/field/return ABI, compiler
scratch, caller/runtime boundaries and negative controls remain explicit.
Execution/peak evidence is obtained in CI, not inferred from compilation.

No board image may link `zcl_wr_*` or its source/CDB evidence. No firmware
image, artifact upload, hardware test, RF send or complete-stack fit claim is
added. Physical Identify, client/group/broadcast behavior, reporting,
persistence, security, profile/device selection and Core R22/BDB3.0.1 gates
remain open. R8 errata19-2019 remains unreviewed; no errata-aware conformance
claim is made.
