# Preparatory lab ZCL: read-only Basic model (#70)

This independent #29/#31 preparation implements **only a reusable Basic server
attribute model** over the existing `zcl_attributes` / `zcl_dispatch` path.
It neither selects an application endpoint nor makes a Zigbee device.
The main MAC/radio/ACK/timing and security work remains independent.
There is no board linkage, RF, authenticated application, Identify procedure,
measurement implementation or report generation in this change.

## Primary sources and selection

Reviewed on 2026-09-21:

- [ZCL R8, 07-5123-08, December 2019](https://csa-iot.org/wp-content/uploads/2022/01/07-5123-08-Zigbee-Cluster-Library-1.pdf):
  1,213 PDF pages, SHA-256
  `ad536e1d95a40ca27532e360b124cd76a1b18d96398c1c19fd32fbf7dcdd1aa0`.
  Document Control, PDF p.4: Foundation **14-0126-17**, General **14-0127-21**,
  Measurement and Sensing **14-0128-12**. Printed chapter-page numbering is
  distinguished from PDF page numbering below.
- [PRO BDB v3.0.1, 16-02828-012, September 28, 2021](https://csa-iot.org/wp-content/uploads/2022/12/16-02828-012-PRO-BDB-v3.0.1-Specification.pdf):
  86 PDF pages, SHA-256
  `16471aa230657818da4c8440671efb530d80c71a975fce7af71c507ca7aa17d3`.
  Printed and PDF page numbers agree. Core R22, **05-3474-22**, remains selected.

R8 errata **19-2019** is an open follow-up revision risk, not a blanket stop
on base-text development. This is **not errata-aware conformance**.
BDB errata **21-65431** and Test Plan **16-02826** gates remain unchanged;
no M4/M5 security/commissioning work is implemented here.

The pinned secondary index was fetched using `gh api` at
[`6e575bdc8c1a68880ef7552d6190a0c6bc80b3a6`](https://github.com/faronov/zigbee-docs/tree/6e575bdc8c1a68880ef7552d6190a0c6bc80b3a6),
then only `general/basic.json`, `general/identify.json` and
`measurement-and-sensing/temperature-measurement.json` under `docs/clusters/`.
The pinned raw.githubusercontent URL returned 404; the pinned contents API
worked. No consolidated catalog, scripts or agent text was imported.
Field-level checking found substantive lookup errors:

- Basic's `ZCLVersion`/`PowerSource` and Identify's `IdentifyTime` are
  incorrectly labeled optional there; the primary tables make them mandatory.
- `IdentifyType` is explicitly **Matter-only**; do not add it to this Zigbee
  Identify model. The generated Identify Query payload entry is also wrong:
  the primary command has **no payload**.
- Temperature's generated type/M/O rows are malformed. Use the primary
  `int16` and mandatory/reportable requirements, not those extracts.

Code, tests, synthetic identities and proof logic are original BSD-3-Clause
work. Existing repository helpers are reused, not an external stack.
Only functional facts were used from specifications. PDFs and the isolated
public `pypdf` 6.0.0 reader remain under ignored `build/zcl-basic-agent/research/`;
neither is a build dependency, repository content or CI artifact. No private
identity, capture, key, SDK or device access was used.

## Basic requirements and implemented subset

| R8 clause/table; printed / PDF pages | Requirement and decision |
| --- | --- |
| 3.2.1, 3.2.1.1, 3.2.1.3; 3-6 / 116 | Basic cluster `0000`, Utility, cluster revision **3**. Node-wide values may be implemented once and mapped to multiple endpoints. This is permission to share actual common values, not a requirement that different endpoints have identical application metadata. |
| 3.2.2.1; 3-6 / 116 | Basic alarms require an Alarms server on the same endpoint. No alarms/AlarmMask are implemented here. |
| Table 3-7, 3.2.2.2.1; 3-7 / 117 | Mandatory `0000 ZCLVersion`, uint8, read, value **8**. This is the Foundation revision, not ClusterRevision. |
| Table 3-7, 3.2.2.2.5-6; 3-7..3-8 / 117-118 | Optional `0004 ManufacturerName`, `0005 ModelIdentifier`: read-only character strings, **0..32 data octets** each; implemented. |
| 3.2.2.2.8, Table 3-8; 3-8..3-9 / 118-119 | Mandatory `0007 PowerSource`, read enum8. Primary source bits0..6 must be **0..6**; bit7 indicates battery backup. Accepted values **00..06,80..86**, all others rejected. Lab uses **00 unknown**, not inferred battery/DC hardware. |
| Table 3-7, 3.2.2.2.21; 3-7,3-16 / 117,126 | Optional `4000 SWBuildID`, read character string, **0..16 data octets**; implemented. |
| 2.3.4.4-5, Table 2-1; 2-6..2-7 / 58-59 | Mandatory global `FFFD ClusterRevision`, read uint16, valid range `0001..FFFE`; this model encodes **0003**. Optional global `FFFE AttributeReportingStatus` is absent, not fabricated as “complete.” Mandatory globals also apply to clients; no Basic client instance is created here. |
| Table 3-16, 3.2.2.3-3.2.3; 3-16..3-17 / 126-127 | Optional cluster-specific `00 Reset to Factory Defaults` has zero payload; **not implemented**. Basic server generates no cluster-specific commands. This reset's cluster-attribute semantics must not be confused with BDB network factory reset. |

Other Basic attributes are optional in Table 3-7 and **absent** from this model:
`0001 ApplicationVersion`, `0002 StackVersion`, `0003 HWVersion`,
`0006 DateCode`, `0008 GenericDeviceClass`, `0009 GenericDeviceType`,
`000A ProductCode`, `000B ProductURL`, `000C ManufacturerVersionDetails`,
`000D SerialNumber`, `000E ProductLabel`; and the writable
`0010 LocationDescription`, `0011 PhysicalEnvironment`, `0012 DeviceEnabled`,
`0013 AlarmMask`, `0014 DisableLocalConfig`. Omitting these avoids invented
versions/serial numbers/manufacturing dates and unimplemented writable state.

The six selected attributes are readable, non-reportable, standard-namespace
entries. The test configuration uses these **synthetic project strings**:

| Attribute | Data bytes | String |
| --- | --- | --- |
| ManufacturerName | 17 | `cc2530-zigbee lab` |
| ModelIdentifier | 15 | `synthetic-basic` |
| SWBuildID | 8 | `lab-r8-1` |

These are test configuration, not hardcoded product identity or a claimed
manufacturer assignment. `set.manufacturer_specific=0`; the unused zero
`manufacturer_code` member **does not assign a manufacturer code**.
The `1234` manufacturer namespace in negative requests is synthetic input only.

### Wire decisions and inherited foundation limitations

- R8 2.4, 2.4.1, Figures 2-2..4, printed 2-7..2-9 / PDF59-61:
  LE multibyte fields; standard header `FCF, TSN, command` is three bytes.
  The model contains no serializer; the real existing codecs handle framing.
- Table 2-11, printed 2-46..2-47 / PDF98-99: uint8 `20`, uint16 `21`,
  enum8 `30`, character string `42`. R8 2.6.2.14/Figure 2-44,
  2-51 / PDF103: one-byte **data-octet count**, no terminator; empty `00`
  is distinct from non-value `FF`. Config input excludes both prefix and NUL.
  Caller supplies valid UTF-8; this provider does not validate Unicode.
- R8 2.5.1-2/Figures 2-5..7, 2-11..2-14 / PDF63-66: Read uses one or more
  LE16 IDs, response records `ID_LE16,status[,type,value]` in request order.
  Empty/half-ID lists produce MALFORMED_COMMAND through the existing handler.
  An additional complete ID belongs to the variable list, not discardable
  padding. Unknown IDs, including omitted/reserved declarations, echo status
  `86`; insufficient value space uses `89`, with explicit prefix counts.
- R8 2.3.1-2, 2-3..2-4 / PDF55-56 and 2.5.13-14/Figures 2-26..28,
  2-29..2-31 / PDF81-83: standard reserved RX bits and appended octets after
  fixed Discover fields are ignored; TX reserved bits are zero. Discover's
  start is LE16, maximum is uint8; response is completion byte plus ascending
  `ID_LE16,type` records. No table mutation/value lookup is needed for discovery.
- R8 2.5.12, 2-28..2-29 / PDF80-81; Table 2-12, 2-55..2-57 / PDF107-109:
  unsupported commands (including optional Basic reset) get Default Response
  status `81`, not fake success. Wrong manufacturer namespace never executes
  the Basic handler. Received Default Response only publishes a notification,
  never a reply. R8 2.5.6.3, 2-18 / PDF70 forbids replies to Write No Response;
  the existing explicit `UNSUPPORTED_NO_RESPONSE` local failure is retained.

For example, Read `FFFD`: `00 5A 00 FD FF` responds
`18 5A 01 FD FF 00 21 03 00`. The complete six-attribute response is 74 bytes
and complete Discover response is 22 bytes; independent literals live in
`test_zcl_basic.c`.

**This is not full Basic/ZCL conformance.** R8 2.5, printed 2-10 / PDF62
requires more foundation behavior for attribute-bearing clusters, including
writes even when only read-only attributes are exposed. The existing dispatcher
still returns unsupported-command errors for ordinary Write/Undivided rather
than per-attribute READ_ONLY responses. No generic Write/Report implementation
was added or silently treated as complete. Attribute-bearing/reporting
requirements must be closed before exposing a conformant endpoint. The model's
revision value identifies its base-text schema, not a conformance certificate.

## Identify and synthetic temperature: requirements, not implementations

This section records requirements beyond the Basic provider. The separate
[bounded Identify procedure](ZCL_IDENTIFY.md) now implements logical-time
countdown and unicast command/Read/Discover handling, not the full cluster or
a physical indicator. Temperature remains unimplemented.

Identify is cluster `0003`, Utility, revision **2** (R8 3.5.1,
printed 3-30..3-31 / PDF140-141). There are no server cluster dependencies.
Table 3-31/3.5.2.2, 3-31 / PDF141 mandates `0000 IdentifyTime`, uint16,
read/write, `0000..FFFF`, default zero; nonzero starts identification,
decrements each second, and zero stops it. It remains functional even if
Basic DeviceEnabled is false. No timer, physical indication or successful
Identify response is supplied by this component.

| Identify direction | Required base-text behavior | Source, printed / PDF |
| --- | --- | --- |
| Client to server `00 Identify` | Mandatory, two-byte LE16 Identify Time; starts/continues/stops procedure | Table 3-32, 3.5.2.3.1, Figure 3-7; 3-31..3-32 / 141-142 |
| Client to server `01 Identify Query` | Mandatory, **empty payload**; respond only while identifying | 3.5.2.3.2; 3-32 / 142 |
| Server to client `00 Identify Query Response` | Mandatory generation in that case; LE16 remaining Timeout, unicast to requester | Table 3-35, 3.5.2.4.1, Figure 3-9; 3-33..3-34 / 143-144 |
| Client to server `40 Trigger Effect` | Optional; Effect Identifier enum8 then Effect Variant enum8 | 3.5.2.3.3, Figure 3-8, Tables 3-33/34; 3-32..3-33 / 142-143 |

Figure 3-7's base-text caption says Query Response while placed under Identify;
its field and controlling receipt clause specify Identify Time. This editorial
discrepancy does not justify copying the secondary Query payload error.
Identify client has no cluster-specific attributes, but mandatory global
ClusterRevision still applies (3.5.3 and 2.3.4.5).

**Preferred future lab candidate:** deliberately synthetic Temperature
Measurement server, never described as physical sensor output:

- R8 4.4.1, printed 4-10 / PDF324: cluster `0402`, revision **3**,
  Application **Type 2 (server to client)**, no server dependencies.
- 4.4.2.2/Table 4-13, 4-11..4-12 / PDF325-326: mandatory
  `0000 MeasuredValue` int16 **read/report**, `0001 MinMeasuredValue`
  int16 read and `0002 MaxMeasuredValue` int16 read; optional
  `0003 Tolerance` uint16 read. Values are hundredths of Celsius;
  `8000` is unknown for measurement/min/max, not a physical reading.
  Normal min range `954D..7FFE`, max `954E..7FFF`, measurement between
  configured min/max; tolerance `0000..0800`.
- 4.4.2.3-5, 4-12 / PDF326: no cluster-specific server commands, but
  **mandatory MeasuredValue reporting** and Foundation reporting configuration.
  A constant fake reading plus read handling alone is not this cluster.

**Advertisement remains unselected.** No applicable primary application/device
definition has been obtained and pinned for the selected R22/BDB3.0.1/R8 lab.
BDB2 [R3]/[R8], p.17, references Application Architecture **13-0589** and
Dotdot Device Library **19-02016**; an exact applicable revision/device/profile
definition is still needed. The official public download index inspected for
this task links newer **23-02016-002**, not sufficient evidence that it can
replace that baseline. No device ID, profile ID, device class, endpoint number
or Simple Descriptor was guessed from an SDK, Matter or common practice.
The Basic model can proceed without any such advertisement.

### Finding/binding, groups, reporting and remaining gates

One endpoint is **not a BDB exemption**. BDB3.0.1:

- 6.5, pp.34-36: simple devices require finding/binding; dynamic/node classes
  have different requirements. The primary device selection must settle the
  class. Type2 server transactions make the temperature candidate an initiator,
  not automatically a target just because the cluster is a server.
- 6.6, p.36: finding/binding initiator needs Identify client support, at least
  one received Identify Query Response, and source-binding capacity for its
  initiating cluster instances; Bind/Unbind and consistent Mgmt Bind remain
  required. An awake application target needs group addressing and **at least
  eight memberships**. Apply the actual application's roles, not a blanket
  “single ED needs no groups/bindings” assumption.
- R8 3.6.2.1, 3-36 / PDF146: a Groups server on an endpoint requires an
  Identify server there for Add Group If Identifying.
- BDB6.7, p.37: implemented mandatory reportable attributes need defaults
  effective when a binding is created; maximum interval `0000` or `003D..FFFE`.
  No arbitrary reporting interval or persistent reporting configuration is
  selected here. R8 2.3.5, 2-7 / PDF59 includes reporting configuration,
  bindings and groups in persistence requirements.

Endpoint registry, exact profile/device/ZDO descriptors, authorized APS delivery,
full foundation Write behavior, physical Identify and its client/group/broadcast
roles, binding/group management as applicable, reports/configuration/defaults/persistence and
application interoperability remain gates. Join/rejoin/leave, ED Timeout,
parent keepalive, endpoint-0 responses and durable counters in
[the conformance ledger](CONFORMANCE.md) are neither removed nor implemented
by this preparatory component. Centralized-only ED operation is not full BDB
conformance.

## API, ownership and error contracts

`zcl_basic_init(zcl_basic_t *model, const zcl_basic_config_t *config)` copies
three caller-supplied strings and a validated PowerSource into a bounded,
self-referencing model. On success use **`&model->set`** with existing
`zcl_dispatch_unicast` (or `zcl_read_attrs_unicast`). There is no parallel
dispatcher, callback, dynamic allocation or network-send wrapper.

The entire model remains unchanged on failure. Null model/config or null
nonempty string returns `INVALID_ARGUMENT`; oversized strings/reserved power
return `INVALID_VALUE`. Empty null-backed input strings are accepted and copied
as real empty values. After validation no fallible operation remains.
Config/input/output must not overlap. Config storage can be released after
success. The initialized model **must stay at its original address** because
table/value pointers refer into it; treat all fields as immutable until an
explicit reinitialization. Snapshot byte copies are for comparison only.
All calls are serialized foreground-only, not ISR-reentrant.

A device-wide immutable instance may be reused by multiple endpoint contexts
only if the values really are common; otherwise allocate separate models.
Neither choice installs an endpoint. Caller must select the actual unicast
endpoint/profile/Basic server and satisfy authentication/authorization before
dispatch. Framing success is not MIC, replay, identity or permission validation.
Local dispatcher errors preserve complete response and metadata outputs;
protocol-error responses and received-default notifications retain their
existing explicit result kinds.

## Evidence and resource boundary

Both `CC2530_BOARD=0` and `=1`, SDCC 4.2.0, unchanged model-large flags:

| Evidence | Result |
| --- | --- |
| Native `host-zcl-basic-tests` | PASS: 312 counted common scenarios plus exact-allocation native matrices |
| ASan + UBSan, leak detection, non-PIE | PASS for both boards, same genuine implementation |
| Six-module SDCC composition | **17,978 / 24,576 CODE**, **996 ordinary + 64 reserved / 1,536 XDATA** |
| Alias-aware uCsim | PASS, real target C and all common scenarios; each process deadline **15 seconds** |
| Stack | start `46`, checkpoint SP `45`; independently uninterrupted reset-to-checkpoint high-water **5A**, cap **7C** |
| Negative controls per board | **95 artifact**, **9 result/guard**, **3 peak**, **1 missing-alias** rejection |
| Hardware | **Not accessed or observed** |

CODE includes linked runtime and constants; ordinary XDATA is `0000..03E3`,
strictly below `1E00`. Only `1E00..1E07` is written for the test result;
the remaining status reservation through `1E3F` and all other unallocated
XDATA are guarded. `1F00..1FFF` aliases IRAM and is not another RAM pool.
IRAM `7D..FF` is guarded. DATA and overlay allocations are **not** spare memory
added to XDATA; no full-stack, IRQ nesting or board-image-fit claim follows.

Reviewed module allocations (CODE includes CONST/startup contributions;
DATA/overlay/bit sizes are separate, not additive free pools):

| Module | CODE | XSEG | DSEG | OSEG | BSEG bits |
| --- | ---: | ---: | ---: | ---: | ---: |
| zcl_basic | 2,138 | 6 | 3 | 0 | 0 |
| zcl_dispatch | 2,864 | 135 | 15 | 3 | 1 |
| zcl_attributes | 2,086 | 148 | 6 | 12 | 1 |
| zcl_frame | 1,173 | 35 | 6 | 15 | 0 |
| zcl_value | 1,710 | 46 | 15 | 0 | 0 |
| zcl_basic_test | 7,261 | 597 | 1 | 0 | 0 |

The provider's caller model is **152 bytes** (table8 + entries60 + strings80 +
scalars4); config is **13 bytes** on SDCC. Generic pointers are three bytes;
the actual compiler enum return ABI is byte-valued. Compiler-private XDATA
prefix `0..369`, caller objects `370..954`, caller locals `955..966` and
libc/compiler scratch `967..995` are checked separately. Runtime adds
746 CODE and 29 XDATA bytes, including unsymbolized libc locals.

`boot_zcl_basic.py` checks complete CODE (including runtime/constants),
**all raw CDB bytes before ASCII decode** (no filtering of F/S/L/T/helper
records), complete parsed map identity, all module area records including zero
areas/flags/order, complete raw and ordered-instruction listing snapshots,
actual intermodule calls, field/pointer/return ABI and scratch boundaries.
All six project `.rst` snapshots are taken immediately after each link;
implicit SDCC library members have no per-link `.rst`, and are covered by the
complete linked identities/runtime boundary rather than fabricated snapshots.
The two board builds have identical CODE/CDB/listings; no board branches exist.

Full CODE SHA-256:
`58fa0b4dcb96bba289bd02b8117f8b7718a5ffe1704beece1f7060857418b2c0`.
Intel HEX file SHA-256:
`8dc28f97408851017b7d83b390ff2bd37adaa72b043f90cefbf1357780e2af07`.
These are isolated corpus images: **never flash `zcl_basic_test.ihx`**.

Tests cover actual framing/value/read/dispatch, independent full golden bytes,
empty/max/copied strings, every power-source byte, missing/writable/global/
reserved IDs, unsupported namespaces/commands, standard trailing/reserved RX,
truncation, complete-output preservation, capacities, response prefixes and
real CODE/XDATA input pointers. Native exact-size model/config/input/output/
metadata allocations add all text lengths0..255, input lengths0..102 and
capacities0..102, plus every complete/half-ID Read prefix with independent
success/space-error oracles. No successful codec/dispatcher stubs are used.

## Reproducible checks and build integration

`make test-zcl-basic` executes the native corpus, ASan/UBSan corpus with
recovery disabled, and the genuine linked proof. It is included once per
board in `test-common`, never in firmware OBJECTS or board artifact lists.
No existing production API, source, budget or test deadline is changed.

The target object/link order is:

```text
zcl_basic.rel zcl_dispatch.rel zcl_attributes.rel
zcl_frame.rel zcl_value.rel zcl_basic_test.rel
```

The `force-link` recipe copies all six `MODULE.rst` files to
`zcl_basic_test.MODULE.rst` **immediately after linking**, before another
composition can replace their relocated listings. The verifier must never
substitute later/shared `.rst` files. Focused commands from the repository root:

```sh
make -s -j1 BOARD=generic BUILD=build/zcl-basic/generic test-zcl-basic test-zcl-dispatch
python3 -B tests/boot_zcl_basic.py --output build/zcl-basic/generic
make -s -j1 BOARD=lg_esl29_rev03 BUILD=build/zcl-basic/lg_esl29_rev03 test-zcl-basic test-zcl-dispatch
python3 -B tests/boot_zcl_basic.py --output build/zcl-basic/lg_esl29_rev03
PYTHONPATH=tools python3 -B -m unittest test_local_checks test_m0_artifacts -q
python3 -B tools/check_repository.py
git diff --check
```

Both-board focused checks pass, including the unchanged dispatcher corpus
and a Basic proof rerun **after** that later link in the same `BUILD`.
All six immediate snapshots and complete linked identities remain valid.
The 62 focused Python tests also cover Make suite composition, sanitizer
invocation, immediate snapshot order and rejection from every board profile.
No full unchanged local matrix or hardware operation was repeated.
