# Initial conformance ledger

This ledger is created at M0 and updated with each protocol milestone.
**The bootstrap implements no networking.** Standalone offline codecs are
marked separately and do not establish MAC or Zigbee operation.
M9 audits the ledger; it does not postpone specification decisions until release.

## Specification gate

| Decision | Status | Gate |
| --- | --- | --- |
| Zigbee Core R22, document 05-3474-22 | Selected engineering baseline | Used for the requirements below |
| PRO BDB v3.0.1, document 16-02828-012, September 28, 2021 | Selected engineering baseline with Core R22; [primary sources](PROVENANCE.md#bdb-301-baseline-sources) | Bounded ED requirements below; no implementation or certification claim |
| Applicable BDB v3.0.1 errata, document 21-65431 | **Open: primary text/revision not reviewed** | Review and resolve applicable changes before M4/M5 security/commissioning implementation |
| BDB v3.0.1 Test Plan, document 16-02826 | Open: primary text/revision not reviewed | Required before claiming BDB conformance; project tests do not substitute for it |
| ZCL Revision 8, document 07-5123-08, release December 2019; Foundation 14-0126-17 | Selected for bounded offline header/value codecs and read-only foundation; [primary source](PROVENANCE.md#zcl-revision-8-wire-sources) | Base-text evidence only, not full or errata-aware ZCL conformance |
| Approved ZCL R8 errata, document 19-2019 | **Open follow-up risk: primary text/revision not reviewed** | Base-text development may proceed with revision risk recorded; review applicable corrections before conformance claims, not as a blanket development stop |
| Application-profile revision and exact lab/physical device definitions | Open | Pin before device-specific cluster/profile implementation and advertisement; independent generic foundation work may proceed |
| Legacy DATA/ACK, five-command and no-GTS Beacon wire subsets, IEEE 802.15.4-2006-compatible formats | Selected for the standalone [codec](MAC.md), not full MAC conformance | M3 radio/procedure requirements remain open |
| R23 / BDB 3.1 | Not the initial baseline | Separate future scope decision |

Until these decisions and their implementation evidence are complete, the
project must not claim full Zigbee 3.0 conformance or certification.

## Core ED requirements

Page numbers refer to the printed pages of R22.

| ID | Behavior / decision | Reference | Implementation / evidence | Gate |
| --- | --- | --- | --- | --- |
| MAC-WIRE-01 | Bounded legacy DATA v0/v1 with both short/extended addresses, ACK v0, explicit unsupported security/layout errors | [Codec contract and sources](MAC.md) | Standalone codec / host-tested, image-checked, simulated; not in bootstrap | M3 |
| MAC-WIRE-02 | Unsecured v0 association request/response, disassociation, data request and beacon request payloads and static header rules | IEEE 802.15.4-2006 sections 7.3.1-4, 7.3.7; [contract](MAC.md#fixed-format-commands) | Standalone codec / host-tested, image-checked, simulated; no procedures or RF | M3 |
| MAC-WIRE-03 | Unsecured v0 short/extended-source Beacons without GTS descriptors; raw superframe fields, at most seven pending addresses, opaque payload up to 52 bytes | IEEE 802.15.4-2006 section 7.2.2.1, Table 85; [contract](MAC.md#no-gts-beacon-subset) | Standalone codec / host-tested, image-checked, simulated; not schedule validation, scanning or Zigbee discovery | M3 |
| NWK-WIRE-01 | Exact 15-byte legacy Beacon metadata, Protocol ID 0/version 2, strict reserved bits and advertised Extended PAN ID bounds; other fields retained as metadata | R22 section 3.6.7, Table 3-71, Figure 3-54 pp.389-391; Table 3-57 p.322; [contract](NWK.md) | Standalone decoder / host-tested, image-checked, simulated; not profile acceptance, network discovery or join | Preparatory M3; M4/M5 gates unchanged |
| NWK-WIRE-02 | Unsecured Data/version 2 NPDU, unicast/defined broadcasts, optional IEEE addresses, 8/16/24-byte headers and a 116-byte bound; explicit unsupported security/layout errors | R22 sections 3.3.1-2 pp.288-293; Tables 3-57 pp.322-323,3-69 pp.382-383; [contract](NWK.md#nwk-data-frame-codec) | Standalone codec and real MAC integration / host-tested, image-checked, simulated; no APS validation or network/security state | Preparatory M3; M4/M5 gates unchanged |
| APS-WIRE-01 | Unsecured Data/normal-unicast APDU, eight-byte header, raw endpoint/profile/cluster/counter/ACK-request metadata and 108-byte codec cap; explicit unsupported type/delivery/security/layout errors | R22 sections 2.2.5 pp.44-49,2.2.8.4 pp.57-59; [contract and service-limit distinction](APS.md) | Standalone codec and real MAC/NWK/APS composition / host-tested, image-checked, simulated; not in board firmware, no endpoint dispatch or ACK/security state | Preparatory M5; M4/M5 gates unchanged |
| ED-01 | Discovery and child-side join/rejoin/leave | Table 2-152 pp.224-226; section 3.6.1.4.2 | Planned / none | M5 |
| ED-02 | Node Descriptor request and response | Table 2-44 p.84 | Planned / none | M5-M6 |
| ED-03 | Required address/power/simple/active/match descriptor responses | Tables 2-44, 2-149 | Planned / none | M6 |
| ED-04 | Device Announce emit/process and address conflict handling | pp.212,215-216,355 | Planned / none | M5 |
| ED-05 | ED Timeout Request after every join/rejoin, including same parent | section 3.6.10.2 p.392 | Planned / none | M5 |
| ED-06 | Store parent information and select supported keepalive method | sections 3.6.10.2-3 p.393 | Planned / none | M5 |
| ED-07 | Persisted-resume secure rejoin first; prompt keepalive after recovery, bounded renegotiation if information is unknown | BDB 3.0.1 section 7.1 p.40; project robustness gate informed by R22 sections 3.6.10.3,3.6.10.8 pp.393,395 | Planned / none | M5 |
| ED-08 | Required NWK Network Update and wrap-aware update identifier handling | section 3.6.1.13.3 p.363 | Planned / none | M5 |
| ZDO-01 | Unsupported unicast response with response cluster/TSN/NOT_SUPPORTED; unsupported broadcast drop | section 2.4.4.1 p.137 | Planned / none | First endpoint-0 exposure in M5 |
| ZDO-02 | Full Mgmt Leave processing for the ED | section 2.4.4.4.5 p.188 | Planned / none | M6 |
| ZDO-03 | Full Mgmt Bind table response when the device owns source bindings | section 2.5.4.8.1 p.222 | Planned / none | M6 |
| ZDO-04 | Omitted Mgmt LQI returns status-only NOT_SUPPORTED; check this configuration against BDB minimum services/test requirements | section 2.4.4.4.2 p.181; BDB 3.0.1 section 6.6 p.36 | Planned / none; BDB acceptance unresolved | M6 |
| SEC-01 | Outgoing NWK counter survives reboot, factory reset and NWK Leave without rollback | section 4.3.4 p.416 | Planned / none | M4 |
| SEC-02 | Two network keys | section 4.3.4.1 p.417 | Planned / none | M4 |
| SEC-03 | Link keys and applicable Trust Center verification procedures | sections 4.4.7-8 and 4.7.2; BDB 3.0.1 sections 6.3-4,10.1-2 | Planned / none; applicable errata gate open | M4-M5 |
| SED-01 | Parent polling and rejoin-response retrieval while sleepy | p.215; section 3.6.1.4.2 p.342 | Planned / none | M7 |

The reporting application's binding, reporting and attribute requirements are
added with its pinned profile/ZCL revision. Synthetic lab values must be
clearly identified; they are not physical sensor evidence.

## Preparatory ZCL foundation evidence

These references use the printed chapter-page numbering of ZCL Revision 8,
not the R22 page numbers above.

| ID | Behavior / decision | Reference | Implementation / evidence | Gate |
| --- | --- | --- | --- | --- |
| ZCL-WIRE-01 | Global/cluster-specific 3/5-byte headers, raw metadata/payload, standard reserved-bit RX normalization and zero-reserved TX; manufacturer-defined reserved extensions rejected | Sections 2.3.1-2 pp.2-3..2-4;2.4.1 pp.2-8..2-9; [contract](ZCL.md) | Host-tested, image-checked, simulated, including real APS/ZCL composition; no command execution or board linkage | Preparatory M6; errata/application and M4/M5 gates unchanged |
| ZCL-WIRE-02 | 38 wire-value types: no-data, raw/bitmap/unsigned/signed 8..64, Boolean, enum8/16 and short strings; explicit unsupported types and non-value-pattern metadata | Sections 2.6.2.1-9,2.6.2.13-14 pp.2-45..2-51; [contract](ZCL.md#typed-wire-values) | Host-tested, image-checked, simulated; exact spans and full host protocol composition, not native numeric conversion, UTF-8 or attribute validation | Preparatory M6; same open gates |
| ZCL-READ-01 | Generic read-only table; selected-side/namespace unicast Read Attributes, ordered status/value records, space errors and explicit prefix counts; malformed-command Default Response | Sections 2.3.4.4 p.2-6;2.4.1 pp.2-8..2-9;2.5.1-2 pp.2-11..2-14;2.5.12 pp.2-28..2-29;2.6.3 Table 2-12 pp.2-55..2-56; [bounded contract](ZCL.md#read-only-attributes-and-read-attributes) | Host-tested, image-checked, simulated; full MAC/NWK/APS read request/response is host-only. No dispatcher, device/cluster advertisement, write/reporting or board linkage | Preparatory M6; errata remains a conformance risk, application selection and M4/M5 gates unchanged |

## BDB 3.0.1 requirements

These are paraphrases of the selected base document, not implementations or
a complete conformance checklist. BDB page numbers below are printed pages
and match PDF page numbers. Apply the errata gate above before implementing
the affected procedures; do not reuse section numbers or rules from BDB 1.0.

| ID | Behavior / decision | BDB 3.0.1 reference | Implementation / evidence | Gate |
| --- | --- | --- | --- | --- |
| BDB-01 | An ED must be able to join both centralized and distributed security networks; the first project configuration covers only centralized networks | sections 6.1-2 p.32; 10.2.1 p.74 | Centralized path planned; distributed unsupported, not an optional-role exemption | Full BDB conformance remains out of scope |
| BDB-02 | Preserve `bdbNodeIsOnANetwork`; restore persistent state and attempt secure NWK rejoin for a previously joined ED, announcing only on successful rejoin | sections 6.9 p.38; 7.1 pp.39-40 | Planned / none | M4-M5 |
| BDB-03 | Provide unjoined network steering: primary/secondary channel discovery, suitable permit-joining networks, MAC association, bounded authentication/network-key wait and explicit failure | sections 5.1,5.3.8-10; 6.5; 8.2 pp.43-47 | Planned / none | M5 |
| BDB-04 | Fresh-join completion includes a `Mgmt_Permit_Joining_req` broadcast, duration at least 180 seconds and `TC_Significance = 1`; this is not local child admission | section 5.1.2 p.22; section 8.2 steps 14-16 pp.46-47 | Planned / none | M5 |
| BDB-05 | For the R22 TC path, request a new unique TC link key, reject the wrong key type or unchanged key, then verify/confirm before accepting key exchange | section 10.2.5 pp.75-79; R22 section 4.4.8.2.3 pp.445-446 | Planned / none | M4-M5 |
| BDB-06 | ED link-key provisioning includes the default TC key, distributed key and install-code-derived key; install codes use 16 random bytes plus a two-byte CRC and AES-MMO derivation including that CRC | Table 8 p.33; sections 6.4 p.34,10.1 pp.72-74 | Planned / none; no private or production keys imported | M4; distributed path deferred |
| BDB-07 | Bind security processing to the established TC address; reject mismatched command sources and apply explicit unsolicited-link-key policies | sections 10.2.1-3,10.2.6 pp.74-75,79 | Planned / none | M4-M5 |
| BDB-08 | Process/respond to the listed discovery, binding, Mgmt Bind/LQI and Mgmt Leave requests; reconcile optional-service status responses with the selected BDB tests | section 6.6 p.36 | Planned / none; maps to ED-02/03 and ZDO-01..04 | M5-M6 |
| BDB-09 | Finding/binding is required for a simple device; initiator Identify support and binding capacity, awake-target group capacity, and mandatory reportable-attribute defaults depend on the chosen application | sections 6.5-7 pp.34-37; section 8 p.41 | Planned / none; device/ZCL selection still open | M6 |
| BDB-10 | Provide an installer-accessible factory-reset mechanism; distinguish network reset from optional Basic-cluster attribute reset and preserve the outgoing NWK counter | sections 9,9.1,9.3-5 pp.69-71; R22 section 4.3.4 | Planned / none | M4-M6 |

### Commissioning and recovery boundaries

The first target is a receiver-on ED on a centralized R22 network, using the
APS Request Key method (`bdbTCLinkKeyExchangeMethod = 0`). This is a bounded
development configuration, not a declaration that other mandatory BDB
security models or application requirements have been satisfied.

MAC association, receipt of a network key, `Device_annce`, and completed
commissioning are distinct states. Section 8.2 sets `bdbNodeIsOnANetwork`
and sends the announce **before** TC link-key exchange; neither can be used
as the application's authenticated-ready flag.

Section 8.2 bounds successive same-network attempts at ten, with three
recommended. Network-key reception uses `apsSecurityTimeOutPeriod` in
milliseconds, not the five-second TC-exchange timer. Authentication failure
resets network parameters and advances/retries within the stated bound.
TC exchange failure instead requires leave, network-parameter reset,
`bdbNodeIsOnANetwork = FALSE` and `TCLK_EX_FAILURE`.
Exhausted discovery must not become a successful join or an unbounded loop.

Section 10.2.5 gives separate descriptor, request-key and verify-key retry
stages, using `bdbcTCLinkKeyExchangeTimeout = 5 s` and
`bdbTCLinkKeyExchangeAttemptsMax`. Its R20-or-earlier descriptor exception
is not evidence of verified key exchange with an R22 TC. Core R22
section 2.3.2.3.10, Table 2-32 p.72 carries the stack revision in the Node
Descriptor server mask; Beacon protocol version 2 is not that revision.
Retain key A until key B has been successfully verified by an authenticated
response. Mere receipt of Confirm Key is insufficient: apply R22
section 4.4.8.2.3 status, source, key-type and context checks. Power loss
between receive/store/verify/commit must not mark an unverified key ready
or roll counters backward.

Persisted ED startup follows section 7.1: restore state, request secure NWK
rejoin to the saved Extended PAN ID (`RejoinNetwork = 2`, zero scan-channel
mask/duration, `SecurityEnable = TRUE`), then announce on success. Keep
failed recovery explicit even when the persisted BDB membership flag remains
true. R22 ED Timeout negotiation still follows every successful join/rejoin;
the project's prompt keepalive requirement is not a substitute for rejoin.

Network steering while **already joined** is optional in sections 6.5/8;
it is not initially selected. That does not remove the fresh-join broadcast
in BDB-04. Network formation is excluded for the ED, Touchlink is optional,
and section 6.10 requires Green Power proxy basic for coordinators/routers,
not this ED role.

Before M6, classify the application rather than assuming finding/binding is
optional because there is one endpoint. An initiator needs enough source
bindings for its initiating cluster instances. A non-sleepy application target
needs at least eight group memberships. Section 6.7 requires default reports
for implemented mandatory reportable attributes; exact attributes and device
requirements still depend on the selected ZCL/device specification.
Sleepy operation additionally needs section 6.8 polling behavior and the R22
parent/timeout rules; it is not established by this awake-first selection.

## Role exclusions

Routing, accepting/managing children, parent-side indirect queues, network
formation and Trust Center server behavior are outside this ED implementation.
Optional service removal still needs ZDO-01. Full ED leave, source-binding
management and NWK updates must not be removed under a generic "SED is small"
assumption.

Distributed-network support is a deferred **conformance gap**, not a router
or Trust Center role exclusion. Do not describe centralized-only operation
as full BDB 3.0.1 support.

This ledger is deliberately incomplete as a full conformance specification.
Each implementation change must add the precise requirement, supported
configuration, test and evidence link it relies on.
