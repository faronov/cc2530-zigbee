# Initial conformance ledger

This ledger is created at M0 and updated with each protocol milestone.
**The bootstrap implements no networking.** Standalone offline codecs are
marked separately and do not establish MAC or Zigbee operation.
M9 audits the ledger; it does not postpone specification decisions until release.

## Specification gate

| Decision | Status | Gate |
| --- | --- | --- |
| Zigbee Core R22, document 05-3474-22 | Selected engineering baseline | Used for the requirements below |
| Exact R22-compatible Zigbee-3.0-era BDB revision and supported procedures | **Open** | Must be pinned before M4/M5 security/commissioning implementation |
| ZCL/application-profile revision and exact lab/physical device definitions | Open | Must be pinned before M6 attribute/profile implementation |
| Legacy DATA/ACK and five-command wire subset, IEEE 802.15.4-2006-compatible formats | Selected for the standalone [codec](MAC.md), not full MAC conformance | M3 radio/procedure requirements remain open |
| R23 / BDB 3.1 | Not the initial baseline | Separate future scope decision |

Until these decisions and their implementation evidence are complete, the
project must not claim full Zigbee 3.0 conformance or certification.

## Core ED requirements

Page numbers refer to the printed pages of R22.

| ID | Behavior / decision | Reference | Implementation / evidence | Gate |
| --- | --- | --- | --- | --- |
| MAC-WIRE-01 | Bounded legacy DATA v0/v1 with both short/extended addresses, ACK v0, explicit unsupported security/layout errors | [Codec contract and sources](MAC.md) | Standalone codec / host-tested, image-checked, simulated; not in bootstrap | M3 |
| MAC-WIRE-02 | Unsecured v0 association request/response, disassociation, data request and beacon request payloads and static header rules | IEEE 802.15.4-2006 sections 7.3.1-4, 7.3.7; [contract](MAC.md#fixed-format-commands) | Standalone codec / host-tested, image-checked, simulated; no procedures or RF | M3 |
| ED-01 | Discovery and child-side join/rejoin/leave | Table 2-152 pp.224-226; section 3.6.1.4.2 | Planned / none | M5 |
| ED-02 | Node Descriptor request and response | Table 2-44 p.84 | Planned / none | M5-M6 |
| ED-03 | Required address/power/simple/active/match descriptor responses | Tables 2-44, 2-149 | Planned / none | M6 |
| ED-04 | Device Announce emit/process and address conflict handling | pp.212,215-216,355 | Planned / none | M5 |
| ED-05 | ED Timeout Request after every join/rejoin, including same parent | section 3.6.10.2 p.392 | Planned / none | M5 |
| ED-06 | Store parent information and select supported keepalive method | sections 3.6.10.2-3 p.393 | Planned / none | M5 |
| ED-07 | Persisted-resume immediate keepalive; bounded renegotiation if information is unknown | Project robustness gate informed by sections 3.6.10.3,3.6.10.8 pp.393,395 | Planned / none | M5 |
| ED-08 | Required NWK Network Update and wrap-aware update identifier handling | section 3.6.1.13.3 p.363 | Planned / none | M5 |
| ZDO-01 | Unsupported unicast response with response cluster/TSN/NOT_SUPPORTED; unsupported broadcast drop | section 2.4.4.1 p.137 | Planned / none | First endpoint-0 exposure in M5 |
| ZDO-02 | Full Mgmt Leave processing for the ED | section 2.4.4.4.5 p.188 | Planned / none | M6 |
| ZDO-03 | Full Mgmt Bind table response when the device owns source bindings | section 2.5.4.8.1 p.222 | Planned / none | M6 |
| ZDO-04 | Omitted Mgmt LQI returns status-only NOT_SUPPORTED | section 2.4.4.4.2 p.181 | Planned / none | M6 |
| SEC-01 | Outgoing NWK counter survives reboot, factory reset and NWK Leave without rollback | section 4.3.4 p.416 | Planned / none | M4 |
| SEC-02 | Two network keys | section 4.3.4.1 p.417 | Planned / none | M4 |
| SEC-03 | Link keys and applicable Trust Center verification procedures | sections 4.4.7-8 and 4.7.2; pending compatible BDB selection | Planned / none | M4-M5 |
| SED-01 | Parent polling and rejoin-response retrieval while sleepy | p.215; section 3.6.1.4.2 p.342 | Planned / none | M7 |

The reporting application's binding, reporting and attribute requirements are
added with its pinned profile/ZCL revision. Synthetic lab values must be
clearly identified; they are not physical sensor evidence.

## Role exclusions

Routing, accepting/managing children, parent-side indirect queues, network
formation and Trust Center server behavior are outside this ED implementation.
Optional service removal still needs ZDO-01. Full ED leave, source-binding
management and NWK updates must not be removed under a generic "SED is small"
assumption.

This ledger is deliberately incomplete as a full conformance specification.
Each implementation change must add the precise requirement, supported
configuration, test and evidence link it relies on.
