# M6 runtime Read Reporting Configuration

Feature branch: `m6-read-reporting-config`, based on exact accepted firmware
`d0d5355196f667f89261666ac3dbd52260f5b69e`. Compiler remains
`1a01afb49d99ec754e62ab0f65f182de87b4cee3`.
The M5 baseline's FULL38293766529 and frozen images/evidence are unchanged.
This is an experimental development candidate; new FULL acceptance is pending.

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
without a relocation or a relaxed limit. These are development-image
measurements, not final integrated acceptance.

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

MCU acceptance, SMOKE and one frozen-candidate FULL remain required;
host-only success is not MCU acceptance. No additional memory ceiling or
relaxed proof is permitted.
Persistent reporting/bindings, Mgmt_Bind, profile selection, extra ZDO services,
real sensing and live ZHA/Zigbee2MQTT acceptance remain separate milestones.
