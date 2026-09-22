# Synthetic Temperature Measurement and reporting preparation

This is a caller-driven, volatile server model for standard cluster `0402`,
revision **3**, not a physical thermometer or an advertised/authenticated endpoint.
It composes the real frame/value/attribute/Write/dispatcher implementation.
It has no clock reads, GPIO, destination selection, transmission, persistence,
client role, received-report timeout engine or cluster-specific commands.
No device, profile, endpoint or manufacturer identifier is assigned.

## Primary basis

The reviewed primary is [ZCL R8, 07-5123-08, December 2019](https://csa-iot.org/wp-content/uploads/2022/01/07-5123-08-Zigbee-Cluster-Library-1.pdf),
SHA-256 `ad536e1d95a40ca27532e360b124cd76a1b18d96398c1c19fd32fbf7dcdd1aa0`.
Foundation is 14-0126-17; Measurement/Sensing is 14-0128-12.
Page pairs below are printed page / one-based PDF page.

| Decision | Primary location |
| --- | --- |
| Cluster `0402`, revision 3, application type and no server dependencies | 4.4.1, 4-10 / 324 |
| Mandatory attributes, types, read/report access and ranges | 4.4.2.2, Table 4-13, 4-11 / 325 |
| Mandatory global ClusterRevision | 2.3.4.4-5, Table 2-1, 2-6..2-7 / 58..59 |
| Known min/max ordering and measurement bounds | 4.1.3.1, 4-4 / 318 |
| Optional Tolerance and physical-error interpretation | 4.1.3.2, 4-4 / 318 |
| No server-specific commands; mandatory MeasuredValue reporting | 4.4.2.3-5, 4-12 / 326 |
| Configure Reporting layout, special intervals, threshold sign and receipt precedence | 2.5.7-8, Table 2-4, 2-18..2-22 / 70..74 |
| Read Reporting Configuration records, statuses and complete-record prefixes | 2.5.9-10, 2-23..2-25 / 75..77 |
| Report layout, bindings, minimum/maximum timing and analog baseline | 2.5.11, 2-26..2-28 / 78..80 |
| Response direction, echoed TSN and disable-default-response bit | 2.4.1, 2-8..2-9 / 60..61 |
| Analog/discrete/composite classification and extents | 2.6.2, Table 2-11, 2-46..2-48 / 98..100 |
| Signed int16 non-value and status meanings | 2.6.2.8, 2-49 / 101; Table 2-12, 2-55..2-56 / 107..108 |

The malformed secondary temperature rows described in [ZCL_LAB](ZCL_LAB.md)
are not used. Base-text development does not resolve errata 19-2019 or claim
errata-aware conformance. Core R22, BDB 3.0.1, BDB errata/test-plan, application
selection and security gates remain unchanged.

## Values and foundation commands

| ID | Attribute | Type | Implemented access |
| --- | --- | --- | --- |
| `0000` | MeasuredValue | int16 | Read and bounded reporting |
| `0001` | MinMeasuredValue | int16 | Read |
| `0002` | MaxMeasuredValue | int16 | Read |
| `FFFD` | ClusterRevision = 3 | uint16 | Read |

Values are caller-supplied **synthetic hundredths of Celsius**. `8000` means
unknown for measurement and bounds. Known minimum is -27315..32766; known
maximum is -27314..32767. Both known bounds must be strictly ordered.
A known measurement must be -27315..32767 and within every known bound.
`FFFF` is -1, i.e. -0.01 Celsius, not unknown; Table 4-13's footnote 81
example is inconsistent with its controlling factor-of-100 definition.
Initial MeasuredValue is unknown. Tolerance is omitted, not a fabricated
physical uncertainty.
Optional global AttributeReportingStatus is also absent, not a fabricated
claim that reporting or delivery is complete.

Read and Discover expose exactly these four attributes. Existing Write,
Write Undivided and Write No Response run the actual shared parser:
missing attributes are `86`, known wrong types `8D`, and known matching
read-only attributes `88`, with the existing syntax/type/permission precedence.
No write changes the synthetic input or bounds. Write No Response remains
silent rather than fabricating a success reply. Existing standard trailing,
manufacturer-namespace, frame-direction and default-response contracts are
preserved; see [ZCL_WRITE](ZCL_WRITE.md) and [ZCL](ZCL.md).

## Public API and clock contract

`zcl_temp_init(ctx, now, minimum, maximum, defaults)` validates and installs
explicit caller defaults. No default profile is guessed. `zcl_temp_sample`
supplies a new synthetic value. `zcl_temp_rx` handles a caller-selected,
authorized, client-to-server unicast request and constructs response bytes;
it neither selects a cluster nor sends anything.

All calls are serialized foreground operations. Initialize before use.
Storage sizes must be truthful and objects nonoverlapping; context fields
may be observed but not edited. There are no retained or self-referencing
pointers. A context may be relocated between calls, but callers must not
fork a pending lease into independent transport owners. SDCC context/config/
lease sizes are **37/6/4 bytes**; native layout may differ. Internal temporal
staging is only a shared two-byte age, not a full copied model per API.

`now` is a caller-owned modular uint32 **seconds** epoch. Equal timestamps
are allowed. True forward separation between calls must be less than `2^31`;
the caller must maintain continuity even across API errors and uint32 wrap.
This excludes undetectable multi-wrap gaps and ambiguous/backward clocks.
Subsecond timing is deliberately outside this domain: callers must use a
consistent logical second boundary, not independently rounded durations.
Logical age saturates at 65535, sufficient for every supported interval.
No MAC Timer or physical/RF timestamp is substituted.

Ordinary local failures preserve the whole context and caller outputs.
The explicit exceptions are detected stale/ambiguous time or a pending lease
older than **65535 seconds**, which retain `fault=1`, and token exhaustion,
which retains `fault=2`. Only that fault byte changes on those failures;
later temporal operations reject the fault. An encoded protocol-error response
can successfully advance natural logical time without changing configuration.

Initialization abandons volatile state, not persistent state or a network.
Before reinitialization, the caller must discard old prepared bytes and all
old completion callbacks; tokens are scoped to one context initialization.
It is not transport cancellation or factory reset.

## Reporting configuration

Standard global commands `06` and `08` are implemented for this server.
Specific responses are `07` and `09`, reverse direction, echo TSN and set
disable-default-response. Empty or incomplete record lists, including
reserved record directions, produce Default Response `80`; standard reserved
frame-control bits are ignored by the real decoder. Complete appended records
are additional records, not a fixed-format extension. Partial trailing records
invalidate the entire list: no accepted prefix is applied.

For Configure direction 0, receipt precedence is absent ID `86`, composite
type `8C`, known nonreportable attribute `8C`, incorrect type `8D`, invalid
interval/value `87`, then success. Only MeasuredValue is reportable.
Direction 1 timeout configuration is explicitly unsupported (`8C`).
Known primary analog widths, including unsupported scalar float/wide types,
are consumed exactly; known discrete/composite types have no change field.
An unknown/reserved type has no guessed extent: local
`ZCL_CODEC_UNSUPPORTED_DATA_TYPE`, no mutation or output.

Mixed semantic records are processed independently. Successful records apply
despite other semantic errors; only failed records appear in the response,
in request order. All success is one `00` byte. **Project duplicate policy:**
the last successful MeasuredValue record wins. Staging and response capacity
are resolved before any configuration mutation.

Minimum zero means no minimum restriction. Maximum zero means change-only
reporting, except `minimum=FFFF, maximum=0000`, which restores the caller's
defaults. Maximum `FFFF` disables reporting and forgets active configuration.
Analog change bytes are still required on special requests but ignored on
receipt. Normal nonzero maximum must be at least minimum. The sign of an
int16 change is ignored for comparisons; its signed representation is retained
for reading. `8000` is a non-value, not a numeric threshold, and is rejected
on ordinary configurations. Defaults cannot recursively request restoration.
Disabled defaults are allowed locally but do not establish BDB-compliant
mandatory reporting defaults.

Read Reporting Configuration returns `86` for an absent attribute, `8C` for
a known nonreportable one, `8B` for disabled/unconfigured MeasuredValue, or its
complete direction-0 configuration. This component has an empty receive-side
attribute catalog, so direction-1 reads return `86`; this is not an assertion
about a remote peer's attribute set. No received-report timeout configuration
is invented. Only a complete requested prefix fitting the response is returned.
If no record fits, the local capacity failure leaves outputs/context unchanged;
the entire request is still syntax-checked even beyond the fitting prefix.

## Due state, preparation and completion

Each accepted configuration installs the current measurement as baseline,
as required by R8 2.5.11.2.3, and resets age. That initial periodic phase is
an explicit **logical lab policy**: R8 2.5.11.2.1 does not specify the first-report
time after configuration. Thereafter a
report becomes due at the maximum interval or on a sufficiently large value
change, always subject to minimum. Changes are measured against the configured
baseline, then the last successfully issued prepared value, using wide signed
arithmetic. Zero threshold still requires an actual change.

Unknown-to-known and known-to-unknown transitions count as changes after
minimum (**lab policy**, not numeric subtraction involving NaS). An unchanged
unknown value can still be periodically reported as `8000`.

`zcl_temp_prepare(ctx, now, sequence, routes, response, capacity, report)`
requires a truthful `routes` value of 0 or 1. One means the caller has resolved
nonempty binding destinations; the Configure requester is not implicitly a
destination. No routes or no due report returns `OK` with ready/length/token
zero and leaves response bytes untouched. This is not send success.

A ready report is the real eight-byte standard frame
`18 TSN 0A 00 00 29 valueLo valueHi`. The caller supplies TSN; disabling default
response on generated reports is explicit project policy. Serialization leases
one snapshot but does **not** advance its reported baseline. Tokens are 1..FFFF,
never reused within an initialization; failed capacity consumes no token.
While pending, samples and reads remain available; another preparation or
Configure Reporting returns a local unsupported-context result. The caller
must finish/cancel before processing a new configuration.

`zcl_temp_finish(ctx, now, token, sent, issued_at)` checks the current lease.
`sent=0, issued_at=0` cancels without advancing the baseline. A retry can then
prepare the latest sample under a new token. `sent=1` truthfully confirms that
the **prepared snapshot** was issued to the resolved destinations at
`issued_at`, between preparation and completion in the same epoch. Age then
becomes `now-issued_at`, not zero at delayed completion. Concurrently supplied
new samples do not replace that snapshot. Duplicate/stale tokens and invalid
issuance times fail atomically.

The caller owns binding changes, destination correlation, transport failure,
fan-out/retry policy, logical scheduling and authorization. Partial delivery
must not be called successful completion of the resolved destination set.
An issuance assertion is not peer reception or authentication. Missed caller
scheduling cannot be repaired by claiming a report was sent at its deadline.

## Reproducible evidence and limits

Canonical `test-zcl-temperature` runs `host-zcl-temperature-tests`,
`host-zcl-temperature-tests-sanitize`, and the proof below. Sanitizer flags
include ASan/UBSan, nonrecovering errors, frame pointers and non-PIE, as for
the existing providers. Full both-board acceptance belongs in Actions.

The one C corpus is compiled without a partition macro for native tests.
SDCC uses `ZCL_TEMP_PART=1/2/3` for the wire/config/report callers. Each stem is
`zcl_temperature_PART_test`; each link order is:

```text
zcl_temperature.rel zcl_dispatch.rel zcl_write.rel zcl_attributes.rel
zcl_frame.rel zcl_value.rel zcl_temperature_PART_test.rel
```

Immediately after each link, all seven `MODULE.rst` files are copied to
`zcl_temperature_PART_test.MODULE.rst`, before any other link can overwrite
relocations. `python3 -B tests/boot_zcl_temperature.py --output BUILD` checks
all three compositions. Production compilation/link flags are unchanged.
None is a board image or permitted flashing input.

| Partition | CODE / 24576 | Ordinary + reserved XDATA / 1536 | Full SP peak / cap | Checkpoint SP | Shared case groups |
| --- | --- | --- | --- | --- | --- |
| wire | 21513 | 1223 + 64 | 7C / 7C | 57 | 36 |
| config | 22033 | 1215 + 64 | 75 / 7C | 57 | 128 |
| report | 24278 | 1235 + 64 | 7A / 7C | 59 | 2 |

Each group retains its full assertions/loops; the two report groups contain
the interval, threshold, lease, transport and clock corpus, not two isolated
vectors. Native exact-sized allocations add 3708 Configure and 41612 Read
Reporting Configuration length/capacity/control cases, ten report capacities,
all 65536 int16 inputs, all 65535 real lease tokens, and every second before
the maximum active interval across wrap.

The Temperature object is 8152 CODE/361 XDATA; the six production objects
total **16905 CODE/864 XDATA**, already beyond the preferred 16-KiB composition
size before a caller or runtime. Hence three complete compositions and a
separate 24576/1536 budget, not shortened cases or omitted dependencies.
No existing component/integrated-image budget is expanded. Wire reaches the
unchanged stack cap; report has only 298 CODE bytes of composition headroom.

The proof pins all CODE, raw CDB bytes **before decoding** (all F/S/L/T and
private/helper records), complete parsed maps, all object areas, all seven
complete immediate listings and their ordered instructions. It checks public,
field, pointer, return and helper ABI; real calls; the two-byte private age;
production/caller/compiler/libc scratch boundaries; CPU-only direct SFR
operands and unchanged non-CPU SFR state. Artifact/metadata/listing, MMIO,
alias, allocation, result, protected-tail and peak negative controls are real.

Guarded execution uses the `1F00..1FFF` IRAM alias, ordinary XDATA below `1E00`,
untouched `1E08..1E3F`, and the SP7C guard. A separate uninterrupted run measures
peak SP, never substitutes checkpoint SP. Every simulator process retains
the 15-second deadline. Initial focused evidence is **native/sanitizer,
image-checked and generic simulated**; it is not physical sensing, RF,
interoperability, complete BDB or errata-aware conformance.
