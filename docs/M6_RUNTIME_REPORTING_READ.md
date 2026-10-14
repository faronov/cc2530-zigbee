# M6 runtime Read Reporting Configuration

Feature branch: `m6-read-reporting-config`, based on exact accepted firmware
`d0d5355196f667f89261666ac3dbd52260f5b69e`. Compiler remains
`1a01afb49d99ec754e62ab0f65f182de87b4cee3`.
The M5 baseline's FULL38293766529 and frozen images/evidence are unchanged.
Accepted firmware: `28bbafb352cd511e54e369296a02be897c448637`.
[FULL38460876079](https://github.com/faronov/cc2530-zigbee/actions/runs/38460876079)
passes30/30 gates and18/18 complete MCU scenarios in two bound segments.
This is experimental simulator qualification, not hardware validation,
whole-stack certification or production release.

The firmware branch remains at the exact accepted hash. Post-acceptance report
updates are documentation-only on `m6-read-reporting-config-acceptance`.

| Commit | Independent scope |
| --- | --- |
| `ce04f448ebafe37a688c2fbd0682116ba0bd237b` | Reproduce unsupported08 on unchanged accepted runtime, including nonrecovering sanitizers |
| `d61fd04b60258100fc6aea061e1c814a3141f90f` | Minimal live-record runtime handler and primary-backed wire contract |
| `9286c9a391823c6392687f9fe15906ef63fee0f0` | Genuine case23, target layout/state observations and fail-closed FULL/resume gates |
| `28bbafb352cd511e54e369296a02be897c448637` | Documentation-only qualification freeze; exact firmware identity of FULL |

The separate [Confirm-Key normative decision](https://github.com/faronov/cc2530-zigbee/blob/c31e99bb3a7a0f967de30aff5b6ccaf59e0640c3/docs/M5_CONFIRM_KEY_FRESHNESS.md#normative-decision-gate)
remains C - UNRESOLVED. This slice does not change security/key/recovery owners,
production defaults, LLVM, HWLab, hardware, clusters or device IDs.

## Primary wire contract

Pinned ZCL R8,07-5123-08, December2019, Foundation14-0126-17:
SHA256 `ad536e1d95a40ca27532e360b124cd76a1b18d96398c1c19fd32fbf7dcdd1aa0`.
References are printed pages, not PDF indexes:

| Requirement | Primary basis |
| --- | --- |
| Global commands08/09 and request records | Table2-3;2.5.9/Figures2-19..20, pp2-22..23 |
| Response status/direction/ID and optional fields | 2.5.10.1/Figures2-21..22, pp2-24..25 |
| Type-width reportable change | 2.5.10.1.7;2.6.2/Table2-11 |
| Complete fitting response prefix; retry omitted trailing records | 2.5.10.2-3, p2-25 |
| Disabled reporting/default restoration | 2.5.7; existing [runtime](ED_JOIN.md#synthetic-temperature-and-humidity-demo) and [standalone](ZCL_TEMPERATURE.md#reporting-configuration) contracts |
| Error Default Responses and transaction/header rules | 2.3.1-2;2.4.1;2.5.12 |

The request is a global client-to-server frame with at least one complete
three-byte `direction, attributeID_LE16` record. Direction0 reads local
transmit reporting; direction1 refers to received-report attributes.
Reserved directions, empty/truncated bodies and malformed trailing records
produce Default Response `0B, originalCommand08, MALFORMED_COMMAND80`.
Validation covers the entire request before serializing a fitting prefix.

Successful direction0 records contain
`00, direction, ID_LE16, type, min_LE16, max_LE16, change_of_attribute_width`.
Errors contain only `status, direction, ID_LE16`:

| Runtime configuration | Result |
| --- | --- |
| Known reportable attribute with active/default configuration | SUCCESS00 with actual current fields |
| Reporting disabled by maxFFFF | NOT_FOUND8B; no success fields |
| Known nonreportable local attribute | UNREPORTABLE_ATTRIBUTE8C |
| Unknown local attribute | UNSUPPORTED_ATTRIBUTE86 |
| Direction1 | UNSUPPORTED_ATTRIBUTE86: no receive-side attribute catalog/configuration is invented |

There is one existing `ST.report[]`, not another reporting table:

| Cluster / attribute | Type | Change bytes | Existing default min/max/change |
| --- | --- | ---: | --- |
|0402 /0000 Temperature MeasuredValue|29 int16|2|30 /300 /10|
|0405 /0000 Humidity MeasuredValue|21 uint16|2|30 /300 /25|
|0001 /0020 BatteryVoltage|20 uint8|1|60 /3600 /1|
|0001 /0021 BatteryPercentageRemaining|20 uint8|1|60 /3600 /2|

The runtime's real application payload cap is82 bytes, not the standalone
codec's100-byte maximum. Capacity is checked before each whole record;
once a record does not fit, later records are not substituted for it.
No truncated success record or empty valid-request response is emitted.
The current minimum success/error record always fits an empty runtime slot.

Responses keep the incoming transaction sequence, use global server-to-client
direction and Disable Default Response, and address the admitted APS source
endpoint from the actual local endpoint/profile. Request Disable Default
Response never suppresses the explicit09 response. Manufacturer-specific
namespace handling stays unsupported81 with the existing echoed manufacturer
code/header. Wrong frame direction preserves the existing runtime context
errorC3. Received global09 responses are discarded, preventing new
response-to-response loops. Wrong profile/endpoint or oversized packet
metadata is discarded; a busy response slot remains untouched and reports
the existing FULL result.

## Observation and regression boundary

The handler only reads live min/max/change fields. It never changes age, last
reported value, binding destinations, default configuration, pending reports
or application sequence; it has no Flash/NV/security call. Normal owner time
progress and already accepted report completions still occur in the enclosing
serve poll, exactly as before. Equal-time reads introduce no semantic mutation.

Regression commit precedes the correction: native and nonrecovering ASan/UBSan
both reproduce the frozen runtime's unsupported response
`18 80 0B 08 81`, length5, instead of the required default Temperature09
response, length14. Tests use the genuine application overlay and independently
compare overlapping wire behavior against `zcl_temperature.c`.

Coverage includes all four defaults/configure/read/disable/restore paths,
known/unknown/direction1 statuses, mixed records, every request length through
the82-byte cap, every reserved record direction, an exact82-byte response,
prefix clipping, malformed records beyond a full prefix, manufacturer/header
handling, wrong direction/profile/endpoint, oversized metadata, busy-slot
preservation, repeated reads and semantic-state snapshots. Existing
Read/Write/Identify/Bind/scheduler assertions remain present.

The corrected handler passes35077 checks in each native and nonrecovering
ASan/UBSan run. Initial pinned-compiler FAST passes10/10 selected gates
(no MCU scenario is selected by FAST): CODE223628, delta+808; common18944,
BSS4959, DATA44, physical static-frame pool217 and static stack106/128
are unchanged. Banks1..5 are exactly32768 bytes each, bank6 is28342/32768,
bank7 is12502/26624. Minimum local headroom is still zero; bank6/7 free
space is4426/14122. Existing boundary/terminator/thunk/memory proofs pass
without a relocation or a relaxed limit. Final FULL independently reproduces
these measurements and the exact development ELF/HEX identities.

## Integrated application owner

Case23 starts from fresh unprovisioned storage and completes the existing real
authenticated TC commissioning exchange before READY. The external peer injects
eight actual NWK-protected ZCL requests via the radio/AES/transport model:
Temperature configuration7/20/50, reads of all four attributes, mixed
nonreportable/unknown/direction1 records, malformed input, a repeated read and
the existing unsupported manufacturer namespace. The peer only checks actual
DUT TX frames; it does not supply synthetic application responses.

The peer verifies exact command07/09/0B bytes, type widths, TSNs, Disable
Default Response behavior, profile0104 and reversed endpoints1 ->9.
Unsolicited reports still use the original endpoint1 binding and APS ACK flow.
The existing invalid application request/discard and runtime draw/serving
assertions remain present. Original scenarios and case21/22 assertions are
unchanged; case23 has its own modified reporting schedule expectations.

Native and nonrecovering ASan/UBSan produce byte-identical case23 transcripts:
1556 caller steps,8 requests/8 checked replies,7 ordinary reports, and960
READY-state observations. Each observation contains all40 bytes of reporting
records (including ages and last values),3 binding bytes and4 scheduling flags.
The target audit compiles a constant-only layout probe with the exact firmware
compiler/defines/ABI flags, verifies the linked owner's size and bounds, and
binds observation regions to the ELF hash. The probe is never linked into the
image. MCU replay compares actual XDATA with those observations at each caller
return, as well as the original radio/AES/NV/replay-floor/stack invariants.
No native struct offset is assumed, and no observed application state is
injected into DUT memory.

FULL now requires case23's explicit serving terminal, exact complete step count,
bounded stack and reporting/state metrics. Negative gate tests reject missing
scenario/metrics/observations, incomplete replies, wrong configuration, absent
report continuation and invalid count types. All17 prior FULL cases remain
selected; the final Policy C suite contains18 scenarios.
Case23 shares the existing segmented FULL deadline with cases0/21. Bound
simulator snapshots retain the count of already validated application
observations and bind the target layout artifact as well as the image/vector/
simulator/replay identities. Resume cannot silently discard observation history.
This is host evidence state, not a firmware NV field or schema change.

## Development qualification and freeze boundary

All18 genuine host scenarios pass with byte-identical nonrecovering ASan/UBSan
transcripts, including all17 original commissioning/rejoin/security/Policy C
scenarios and case23. Final integrated FAST passes10/10 selected gates;
development SMOKE passes16/16, including complete fault cases2/3/4/9 and
the explicitly limited three-call case0 prefix. SMOKE is not complete
commissioning or AES/flash-path acceptance. The29 tier unit tests and779-file
repository/link guard pass.
CI FAST passes11/11. The actual selection difference is the additional
`compiler-tree` gate for the configured CI compiler build: no development FAST
gate is missing, and its10 selected gates all pass. Counts are not treated
as interchangeable evidence.

The unchanged M5/Policy C accepted branch hashes and original accepted-manifest
hashes were reverified. No compiler diff, hardware operation, HWLab change,
security implementation change or production merge is part of this slice.

Accepted image identities, independently matched to the admitted machine image
preserved from FULL segment1 and the final sealed receipt:

| Artifact | SHA256 |
| --- | --- |
| ELF | `33152d75e120cfbcafe07e481f719b49c3254b0cb4ca1079c87b058cd9b5ad62` |
| HEX | `30e9f9880ee91c3a91dcc39ed2e0f853e2451b880a6af54127f8e0b046cf4a8a` |

377 bank thunks remain correct, with zero incorrect thunks. All fixed-address,
startup, stack-entry, generic-pointer and reserved-NV/lock-region proofs pass.
Static stack remains106/128. The actual maximum across all18 complete MCU
scenarios is101/128, SP0xE4, independently matching the baseline measurement.

## Final acceptance evidence

Exactly one new FULL was dispatched, for source28bbafb and compiler1a01afb;
both identities are exact and clean in the verified sealed receipt. Every
selected gate is PASS. Segment1 suspended only long cases21/23; segment2
resumed their source/image/simulator/vector/layout-bound states and reached
PASS. No partial prefix or SUSPENDED result was accepted as a complete case.

| Evidence | Verified outcome |
| --- | --- |
| All18 MCU cases | Complete steps equal expected steps, explicit profile terminal and bounded actual stack;18/18 PASS |
| Case23 reporting runtime |1556/1556 steps,705133 peripheral stops,8/8 exact replies,7 regular reports,960 actual XDATA state observations; READY/serving;101/128 stack |
| Case21 sustained Policy C |4070/4070 steps;600084 ms;120 protected frames,12 replay drops,23 reports;0 flash commands/erases/programs/checkpoints;101/128 stack |
| Case22 security completion |471/471 steps; READY/serving;101/128 stack |
| Independent standalone MCUcase23 | Complete1556/1556 replay of the identical frozen ELF/HEX, with the same replies/reports/state observations/stack |
| Compiler binding | Full LLVM commit1a01afb49d99ec754e62ab0f65f182de87b4cee3; no compiler diff; all tool hashes equal the pinned development and CI FAST inputs |
| Sealed final FULL receipt | `8fafc49b8191cbc0cd6a36115045a211c2912c8f0d157adebd9ca8aa57eb1fde` |
| Preserved103-file accepted evidence manifest | `92792d1cb9ca0751d6112a6b2e10e85e22e9f5fd3b542b0d9c28710da9ae744b` |

The separate accepted evidence set includes final receipts/reports/logs,
original segment1 state archive, exact compiler/simulator/lit input archive,
admitted ELF/HEX, final-machine-code audits and all18 reference vectors.
CI reference vectors byte-match the native/nonrecovering sanitizer vectors.
Original M5/Policy C accepted manifests and firmware/review branch references
were reverified unchanged; they are not overwritten or relabeled as M6 evidence.

| Resource | Accepted M5 baseline | Accepted M6 | Delta |
| --- | ---: | ---: | ---: |
| Executable CODE |222820|223628|+808|
| Common CODE |18944|18944|0|
| XDATA BSS / initialized DATA |4959 /44|4959 /44|0|
| Physical static-frame pool |217|217|0|
| Raw XDATA including pool |5220|5220|0|
| Static-frame symbols / logical summed bytes |223 /1761|223 /1761|0|
| Static stack demand / capacity |106 /128|106 /128|0|
| Actual MCU runtime peak / capacity |101 /128|101 /128|0|
| Correct / incorrect bank thunks |377 /0|377 /0|0|

| Physical bank | M6 used / capacity | M6 free | M5 free |
| --- | --- | ---: | ---: |
| Common0 |18988 /32768, including44-byte DATA load image|13780|13780|
|1|32768 /32768|0|1|
|2|32768 /32768|0|0|
|3|32768 /32768|0|0|
|4|32768 /32768|0|0|
|5|32768 /32768|0|0|
|6|28342 /32768|4426|5233|
|7|12502 /26624|14122|14122|

Minimum physical bank headroom remains zero; all zero-margin boundary/
terminator proofs remain mandatory and pass. Aggregate bank slack is18548,
down808; it is not permission to overfill any particular bank. The original
linker packing/profile,45-object limit, IRAM/XDATA alias ownership, fixed
addresses and reserved NV/lock region are unchanged. No LTO, dynamic allocation,
memory-ceiling expansion, special relocation, new application state table or
new firmware NV field was introduced. K2/K3 storage/migration and normal
incoming/outgoing security reservations retain their accepted owners.

Direction1 receive-side configurations, manufacturer-specific namespaces,
persistent reporting/bindings, profile selection, real sensing and live
ZHA/Zigbee2MQTT acceptance remain separate gaps. Recommended next bounded ZDO
slice: read-only Mgmt_Bind from actual live RAM bindings with correct entry
count/pagination/status, without adding persistent application storage.
Confirm-Key and broader security-command/broadcast-key-update work remain
behind their independent normative gates.
