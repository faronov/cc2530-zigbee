# Initial conformance ledger

This ledger is created at M0 and updated with each protocol milestone.
**The bootstrap implements no networking.** Standalone offline components are
marked separately and do not establish MAC or Zigbee operation.
M9 audits the ledger; it does not postpone specification decisions until release.

## Specification gate

| Decision | Status | Gate |
| --- | --- | --- |
| Zigbee Core R22, document 05-3474-22 | Selected engineering baseline | Used for the requirements below |
| PRO BDB v3.0.1, document 16-02828-012, September 28, 2021 | Selected engineering baseline with Core R22; [primary sources](PROVENANCE.md#bdb-301-baseline-sources) | Bounded ED requirements below; no implementation or certification claim |
| Applicable BDB v3.0.1 errata, document 21-65431 | **Open follow-up risk: primary text/revision not reviewed** | Base-text M4/M5 development may proceed; review and resolve applicable corrections before conformance claims, not as a blanket development stop |
| BDB v3.0.1 Test Plan, document 16-02826 | Open: primary text/revision not reviewed | Required before claiming BDB conformance; project tests do not substitute for it |
| ZCL Revision 8, document 07-5123-08, release December 2019; Foundation 14-0126-17 | Selected for bounded offline header/value codecs and read-only foundation; [primary source](PROVENANCE.md#zcl-revision-8-wire-sources) | Base-text evidence only, not full or errata-aware ZCL conformance |
| Approved ZCL R8 errata, document 19-2019 | **Open follow-up risk: primary text/revision not reviewed** | Base-text development may proceed with revision risk recorded; review applicable corrections before conformance claims, not as a blanket development stop |
| Application-profile revision and exact lab/physical device definitions | Open | Pin before device-specific cluster/profile implementation and advertisement; independent generic foundation work may proceed |
| Legacy DATA/ACK, five-command and no-GTS Beacon wire subsets, IEEE 802.15.4-2006-compatible formats | Selected for the standalone [codec](MAC.md), not full MAC conformance | M3 radio/procedure requirements remain open |
| R23 / BDB 3.1 | Not the initial baseline | Separate future scope decision |

Until these decisions and their implementation evidence are complete, the
project must not claim full Zigbee 3.0 conformance or certification.

On 2026-09-23 the maintainer explicitly deferred the BDB errata development
prerequisite. This changes scheduling, not evidence: the primary text and
revision remain unknown, and no claim is made that it contains no critical
corrections. Base-text security and commissioning code still needs real
authentication, negative/replay tests, safe persistent state and explicit
unsupported/failure behavior. No successful security stub or hardware
authorization follows from this decision. Issue #16 remains open for review.

## M2 platform acceptance boundary

Offline #9 audit against [M2 deliverables/exit gates](PLAN.md#m2---minimal-cc2530-platform-services)
and [issue #4](https://github.com/faronov/cc2530-zigbee/issues/4), following the
offline flash fixture in `4a01970`. This is a platform evidence ledger, **not
new hardware observation or protocol conformance**. All rows retain separate
host, genuine linked-image and alias-aware synthetic checks. Physical results
refer only to the dated parent-observed LG records, never generic hardware.

| Contract | Existing evidence / accepted limit | Remaining boundary |
| --- | --- | --- |
| [Timebase/clock](ARCHITECTURE.md#awake-only-timebase-first-m2-slice) | Awake raw24-bit modular deadlines; bounded RC16/XOSC32 selection/rollback. [Compiled-C timer](VALIDATION.md#compiled-c-sleep-timer-hardware-acceptance-2026-09-16), [separate natural rollover](VALIDATION.md#independent-sleep-timer-hardware-reference-2026-09-16) and [clock cases](VALIDATION.md#2026-09-17-bounded-lg-clock-hardware-evidence) observed on LG | No calibrated wall time/frequency, multi-wrap epoch, physical stopped-clock/oscillator-failure or PM wake continuity |
| [IRQ ownership](ARCHITECTURE.md#interrupt-ownership-foundation-isolated-m2-slice) | Reentrant EA token leaves and [real Timer1 ISR/RETI](VALIDATION.md#2026-09-17-bounded-lg-timer1-irq-hardware-evidence) | Not a general dispatcher/peripheral lock; other sources and higher-priority physical nesting unobserved |
| [Quiescent FIFO](ARCHITECTURE.md#quiescent-radio-fifo-foundation) | Bounded flush/preload; [LG TXFIFO readback and terminal partial-effect timeout](VALIDATION.md#2026-09-17-bounded-lg-fifo-hardware-evidence) | FIFO acceptance is not transmission, radio reset, RF-error recovery or continuous RX |
| [DMA](ARCHITECTURE.md#isolated-channel-0-dma-copy) | Channel0/TRIG0,1..16-byte RAM copy; [LG copies/timeout/separate reset](VALIDATION.md#2026-09-17-bounded-lg-dma-hardware-evidence) | Exclusive reset/TRIG0 history and clear debug DMA_PAUSE required; no general channel/trigger framework or fault-time buffer release |
| [AES](ARCHITECTURE.md#isolated-aes-128-dma-block) | One encrypt block via private DMA backend; [LG public vectors, negatives and reset recovery](VALIDATION.md#2026-09-17-corrected-lg-aes-bounded-hardware-evidence) | Not CCM*, authentication, key management/erasure or CPU-only transfer acceptance |
| [PRNG](ARCHITECTURE.md#isolated-deterministic-prng) | Explicitly seeded deterministic LFSR; [LG full-period/stopped/reset records](DEBUGGING.md#2026-09-17-corrected-lg-prng-full-reset-recovery-acceptance) | No entropy or security RNG; [#10 primary review](ARCHITECTURE.md#rf-noise-entropy-qualification-boundary) selects only a raw IRND characterization candidate, not an assessed source or implementation |
| [Binary noise health tests](ARCHITECTURE.md#binary-raw-noise-health-test-foundation) | Hardware-independent RCT/APT, startup accounting, explicit diagnostic cutoffs and retained faults; both-board host/sanitizer/image/simulator evidence | No sampler, qualified entropy estimate/cutoffs, conditioner, DRBG or hardware observation; passing tests do not establish unpredictability |
| [Raw IRND acquisition](ARCHITECTURE.md#isolated-raw-irnd-acquisition) | One reset-exclusive1..1024-bit no-sync RX capture, actual timer/work bounds, exact packing and partial-failure metadata; real health-core composition and both-board software proofs; separate [boot-disarmed fixture/manual operator](RADIO_NOISE_FIXTURE.md) | Hardware acceptance pending; no normal-radio handoff, independence guarantee, qualified entropy or security RNG |
| [Passive RX](RADIO_RX.md#channel-reset-and-metadata-interpretation) | API channels11..26, reset-exclusive polling, raw signed RSSI/seven-bit correlation; [LG channel15 only](RADIO_RX.md#physical-evidence-and-remaining-gates) | Other-channel RF, calibrated frequency/timing/RSSI/LQI, independent FCS and controller-fault recovery remain unobserved; no TX/ACK/ISR/MAC claim |
| [Boot-disarmed TX fixture](RADIO_TX_FIXTURE.md) | Both-board offline proofs; separate one-attempt LG/raw05 PHY_DONE and independent body matches on [channel15](DEBUGGING.md#2026-09-19-lg-single-attempt-tx-acceptance) and [channel26](DEBUGGING.md#2026-09-19-lg-channel26-isolated-tx-acceptance) | Channel exclusivity, physical busy-channel, independent FCS, calibration and fault recovery remain open; no ACK, retry, same-reset RX adapter or MAC/network claim |
| [Flash](ARCHITECTURE.md#verified-reserved-page-erase-and-program) | #6/#7 real RAM executor, bounds/history/readback and retained fail-stop have offline evidence; [#8 fixture](FLASH_FIXTURE.md) is boot-disarmed and offline-only for both boards | Physical program/erase, full excluded-region preservation, mapped-RAM inspection and reset/interruption recovery remain open |
| [Board/memory ownership](ARCHITECTURE.md#memory-contract) | Board startup owns GPIO; code/NV/lock partitions, status reservation, IRAM alias and ISR ownership are documented and checked | Sleep remains disabled. No generic-board physical evidence or arbitrary cross-service ownership handoff is established |

These are isolated, ownership-constrained foundations, not an integrated
concurrent platform runtime. Full-reset/exclusive-history requirements cannot
be satisfied merely by composing APIs or observing idle registers.
The bounded LG AES/vector and raw timer/rollover exit evidence exists, but
issue #4's calibrated timing/radio-metadata and physical-flash items remain
open. The plan's broader reset/dispatch/calibrated-metadata deliverables need
implementation/evidence or an explicit accepted scope decision, not silent
substitution of FIFO flush, EA ownership or raw samples.

#9's offline audit/interpretation does not complete its dependent #8 or its
hardware/scoping acceptance item, and cannot close #4. Further physical work
requires separate board/image/operation/recovery authorization. Sleep,
generic-board support and cryptographic entropy must not be inferred from the
historical LG baseline; durable NV/security remain later, separate gates.

## Core ED requirements

Page numbers refer to the printed pages of R22.

| ID | Behavior / decision | Reference | Implementation / evidence | Gate |
| --- | --- | --- | --- | --- |
| SEC-COUNTER-01 | Durable exclusive range before allocation; restart skips old ranges, no FFFFFFFF emission or reset on opaque-state clearing | R22 4.3.1/4.4.1/4.5.3 pp412-420,457-458; BDB3.0.1 9/9.3-5 pp69-71; [contract](SECURITY_COUNTERS.md) | Actual journal/flash/RAM composition; host-tested, image-checked, simulated with cuts and retained faults | #21 bounded owner; no automatic recovery/provisioning, physical durability or independent cold anti-rollback anchor; key lifecycle/commissioning integration remains required |
| SEC-MMO-01 | Bounded0..32-byte AES-MMO, zero hash IV and short-message bit-length padding; selected16+2-byte install code, reflected CRC, hash including CRC | R22 B.1/B.6 pp490,493-494; BDB3.0.1 10.1.1-2 pp73-74; [contract](INSTALL_CODE.md) | Real AES driver, primary KAT, independent CRC/MMO oracles, host-tested, image-checked, simulated | #22 derivation only; no legacy lengths, entropy/generation/provisioning, HMAC, TC verification or membership |
| SEC-CCM-01 | AES-128 CCM, nonce13/L2/M4/8/16, bounded AAD/message, MIC before plaintext publication | R22 Annex A pp486-489 and Annex C pp501-504; [contract](ZIGBEE_SECURITY.md) | Real AES/timebase composition, independent native oracles and genuine linked DMA/MMIO replay; host-tested, image-checked, simulated | #19 bounded primitive only; no new physical CCM, full erasure or production entropy |
| SEC-WIRE-01 | NWK Data and APS unicast Data/Command envelopes; six authenticated levels, normalized security-control, source/key selectors and forbidden FFFFFFFF counter | R22 4.3.1/4.4.1/4.5 pp412-421,455-458; [sources](PROVENANCE.md#r22-ccm-and-security-envelope-sources) | Actual bare codecs/AES/CCM; host/image/simulator exact wire and failure evidence, no board linkage | Caller-selected effective keys; separate #21 counter/#22 derivation foundations do not supply #23 replay/key/TC state or #24/#26 admission/join |
| MAC-WIRE-01 | Bounded legacy DATA v0/v1 with both short/extended addresses, ACK v0, explicit unsupported security/layout errors | [Codec contract and sources](MAC.md) | Standalone codec / host-tested, image-checked, simulated; not in bootstrap | M3 |
| MAC-WIRE-02 | Unsecured v0 association request/response, disassociation, data request and beacon request payloads and static header rules | IEEE 802.15.4-2006 sections 7.3.1-4, 7.3.7; [contract](MAC.md#fixed-format-commands) | Standalone codec / host-tested, image-checked, simulated; no procedures or RF | M3 |
| MAC-WIRE-03 | Unsecured v0 short/extended-source Beacons without GTS descriptors; raw superframe fields, at most seven pending addresses, opaque payload up to 52 bytes | IEEE 802.15.4-2006 section 7.2.2.1, Table 85; [contract](MAC.md#no-gts-beacon-subset) | Standalone codec / host-tested, image-checked, simulated; not schedule validation, scanning or Zigbee discovery | M3 |
| MAC-TX-01 | One copied unsecured DATA, canonical Beacon Request, selected Association Request (CAP88/8C) or addressed Data Request; unslotted NB/BE backoff, bounded CCA/retries, legacy ACK/DSN matching and explicit lifetime/quiescence ownership | IEEE 802.15.4-2006 selected O-QPSK/command subset, including7.3.4; R22 Table3-62; [primary derivations and adapter contract](MAC_TX.md) | Offline state machine using the real codec / host-tested, image-checked, simulated; abstract symbol time, no polling/association procedure, radio adapter or board linkage | M3; unified radio ownership, physical ACK/timing and full-stack resources remain open |
| MAC-TIME-01 | Reset-exclusive awake Timer2 init and coherent raw live tuples; independent deadline/work bounds, low-FF discard and retained faults | TI SWRU191F chapter22; SWRZ031 section1.2; [capture discrepancy and ownership](MAC_TIME.md) | Actual timebase/MMIO composition / host-tested, image-checked, simulated; no calibrated time, captured-end API, epoch extension or board linkage | M3 prerequisite; unified radio owner, capture freshness/phase and physical timing remain open |
| RADIO-AUTOACK-01 | Reset-exclusive filtered receiver, verified address/profile, complete-frame servicing, non-aborting stop/drain and explicit same-owner rearm after STOPPED | TI SWRU191F23.9.1-2,23.9.5,23.9.8,23.10, Fig23-20 and RXENABLE/RXMASKSET p260; [primary limits and ownership](RADIO_AUTOACK.md) | Actual timebase/owner/caller composition / host-tested, image-checked, simulated; old125 scenarios unchanged,32 added rearm sequences; no board image or silicon ACK evidence | Preparatory M3/#48/#72; rearm has an RX gap, not a continuous window; broadcast and ACK-FCF compatibility, #40 capture, ordinary-TX/RX ownership, IFS and MAC/POLL closure remain open |
| MAC-SCAN-01 | Bounded offline channel walk, real Request/DSN/CCA state, confirmed window/candidate handling, partial masks and saved-state restoration | IEEE 802.15.4-2006 sections7.1.11.1-2,7.3.7,7.5.2.1-1.2; [normative facts and project policy](MAC_SCAN.md) | Real six-module composition / host-tested, image-checked, simulated; not MLME-SCAN descriptors, a hardware scanner, full parent selection or association | Preparatory M3; #13/#40 adapter/timing, association and full-stack fit remain open |
| MAC-ASSOC-RX-01 | Bounded contextual Association Response metadata, known/unbound IEEE source, finite epoch/generation/time/work and one-shot result | IEEE2006 7.3.2,7.5.3.1,7.5.6.2-4; R22 3.6.1.4.1; [primary decisions and API](MAC_ASSOCIATION.md) | Actual codec/context composition / host-tested, image-checked, simulated; no immediate ACK, extraction procedure, radio restoration or membership | Preparatory M3; decision-wait/extraction reconciliation, #13/#40 adapter/timing and BDB/security gates remain open |
| MAC-ASSOC-RX-02 | Explicit receive-only uncompressed extended/extended Response and selected-source/broadcast-destination PAN alternative; legacy defaults and encoding unchanged | R22 Annex D.3/Table D-3, printed p.514; [exact profile](MAC_ASSOCIATION.md#explicit-r22-response-receive-profile-63) | Actual codec/context / host-tested, image-checked, simulated; no inferred PAN, identity, ACK or membership | #63 recognition and #64 explicit POLL forwarding; Request/Data Request alternatives, IEEE2015 reconciliation and #45 total timing remain open |
| MAC-POLL-01 | One conditional legacy Data Request extraction; Pending0/1, captured ACK-end window, configured PIB F, copied DATA/command delivery and independent cleanup | IEEE2006 7.1.16,7.4.2,7.5.6.2-4; [primary derivations and ownership contract](MAC_POLL.md) | Real codec/TX/Association/controller composition / host-tested, image-checked, simulated;52 scenarios, no hardware adapter or repeat extraction | Preparatory M3; #40 capture/ownership, configured-F deployment, #45 whole Association timing, IEEE2015 headers and receiver ACK/IFS remain open |
| MAC-POLL-RX-02 | Explicit per-call R22 Response recognition and copied forwarding; selected source PAN, command-only broadcast-destination exception and unchanged legacy defaults | R22 Annex D.3/Table D-3; [receive contract and actual resource boundary](MAC_POLL.md#explicit-r22-response-reception-64) | Real five-module composition / host-tested, image-checked, simulated;56 cases include all52 original scenarios; no context-layout or timing change | #64 bounded integration only; same #40/#45/#50 and BDB/security gates, no radio/ACK service or complete association |
| MAC-JOIN-01 | Staged Request/ACK, decision wait R, one real extended-source extraction, contextual Response and confirmed restoration; shared DSN/generation/IFS owner, local lifetime/work abort | IEEE2006 7.5.3.1/7.5.6.3; R22 Annex D.3; [precise supported sequence and limits](MAC_JOIN.md) | Real six-module composition / host-tested, image-checked, simulated;22 sequences, retained Response through POLL NO_DATA and cleanup failure; no adapter, PIB/member installation or full MLME | Preparatory M3/#83; #40/#45/#50, F=1..65534 restriction, full-stack resources and BDB/security gates remain open |
| NWK-WIRE-01 | Exact 15-byte legacy Beacon metadata, Protocol ID 0/version 2, strict reserved bits and advertised Extended PAN ID bounds; other fields retained as metadata | R22 section 3.6.7, Table 3-71, Figure 3-54 pp.389-391; Table 3-57 p.322; [contract](NWK.md) | Standalone decoder / host-tested, image-checked, simulated; not profile acceptance, network discovery or join | Preparatory M3; M4/M5 gates unchanged |
| NWK-CAND-01 | Four copied preliminary Beacon candidates; explicit CRC/channel, profile2/BO15/permit/ED-capacity filter, duplicate replacement and withdrawal without overflow eviction | IEEE 802.15.4-2006 Beacon rules; R22 sections3.6.1.3-4,3.6.7 and Annex D; [exact policy boundary](NWK_CANDIDATES.md) | Real three-decoder composition / host-tested, image-checked, simulated; no radio/scan, ranking, freshness, complete parent selection or membership | Preparatory M3; adapter/association, BDB and security gates unchanged |
| NWK-PARENT-01 | Copied parent choice from one immutable candidate table, selected network, supplied link costs/eligibility and explicit conservative Update ID ordering; profile2 depth excluded | R22 3.6.1.4.1 pp336/338,3.6.3.1 p367; [primary facts versus project policy](NWK_PARENT.md) | Real collector/codecs/selector, host-tested, image-checked, simulated; ambiguous/cyclic order rejected, no hardware link quality/NIB mutation/association | Preparatory M3/#69; caller metadata/watermark provenance and #14/#40/#45/#50 plus BDB/security gates remain open |
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
| ZDO-01 | Unsupported unicast response with response cluster/TSN/NOT_SUPPORTED; unsupported broadcast drop | section 2.4.4.1 p.137 | [Offline ED dispatcher](ZDO_SRV.md): host/image/simulator proof and real unicast NWK/APS composition; broadcast policy uses typed context only | No endpoint registration; integrate authenticated admission and remaining mandatory handlers before first endpoint-0 exposure |
| ZDO-02 | Full Mgmt Leave processing for the ED | section 2.4.4.4.5 p.188 | Planned / none | M6 |
| ZDO-03 | Full Mgmt Bind table response when the device owns source bindings | section 2.5.4.8.1 p.222 | Planned / none | M6 |
| ZDO-04 | Omitted Mgmt LQI returns status-only NOT_SUPPORTED; check this configuration against BDB minimum services/test requirements | section 2.4.4.4.2 p.181; BDB 3.0.1 section 6.6 p.36 | Planned / none; BDB acceptance unresolved | M6 |
| ZDO-WIRE-01 | TSN-bearing Node Descriptor request/success/addressed errors/generic unsupported, bounded trailing-field tolerance and raw Stack Compliance Revision | sections1.2.3/1.2.5 p.3;2.3.2.3 pp.69-73;2.4.2.7-8 pp.83-84;2.4.3.1.3 pp.88-89;2.4.4.1 p.137;2.4.4.2.3 pp.142-143;2.4.5 pp.198-199; [contract](ZDO_NODE.md) | Host-tested, image-checked, simulated with genuine APS; explicit reserved-field subset, no board linkage | Preparatory M5; no endpoint dispatch, advertisement, matching or authentication; ZDO-01 remains open |
| ZDO-SRV-01 | Caller-configured ED Node Descriptor response; INV_REQUESTTYPE for another queried address; Parent_annce drop and no response/notification loops | sections2.4.2.7-8 pp.83-84;2.4.3.1.3 p.88;Parent_annce effect p.98;2.4.4.1-2 p.137;2.4.4.2.3 p.143;2.4.4.4.12 pp.197-198 | [Bounded server](ZDO_SRV.md) with genuine codecs and original synthetic configuration; explicit malformed/local/unsupported failures | No installed member address, self advertisement, notification state, authenticated service or full ZDO claim |
| SEC-01 | Outgoing NWK counter survives reboot, factory reset and NWK Leave without rollback | section 4.3.4 p.416 | Planned / none | M4 |
| SEC-02 | Two network keys | section 4.3.4.1 p.417 | Planned / none | M4 |
| SEC-03 | Link keys and applicable Trust Center verification procedures | sections 4.4.7-8 and 4.7.2; BDB 3.0.1 sections 6.3-4,10.1-2 | Planned / none; base-text development permitted with unreviewed errata risk | M4-M5 |
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
| ZCL-READ-01 | Generic read-only table with reserved standard-ID rejection; selected-side/namespace unicast Read Attributes, ordered status/value records including unknown-ID echo, space errors and explicit prefix counts; malformed-command Default Response | Sections 2.3.4.4 p.2-6;2.4.1 pp.2-8..2-9;2.5.1-2 pp.2-11..2-14;2.5.12 pp.2-28..2-29;2.6.1.4 Table 2-8 p.2-44;2.6.3 Table 2-12 pp.2-55..2-56; [bounded contract](ZCL.md#read-only-attributes-and-read-attributes) | Host-tested, image-checked, simulated, including full MAC/NWK/APS request/response and atomic invalid-table rejection in the integrated resource image. No device/cluster advertisement, write/reporting or board linkage | Preparatory M6; errata remains a conformance risk, application selection and M4/M5 gates unchanged |
| ZCL-DISC-01 | One-cluster unicast Read/Discover dispatch, ascending ID/type pages with completion, unsupported-command errors, received Default Response metadata and explicit no-reply outcomes | Sections 2.3.2 p.2-4;2.5 p.2-10;2.5.6.3 p.2-18;2.5.12-14 pp.2-28..2-31;2.6.3 pp.2-55..2-57; [contract](ZCL.md#discover-attributes-and-unicast-dispatch) | Host-tested, image-checked, simulated, including actual full-chain Discover-then-Read and a checked resource ledger; write-family extension is ZCL-WR-01. No endpoint registry, transactions, reporting, board/RF or full-cluster claim | Preparatory M6; same errata/application/security gates |
| ZCL-BASIC-01 | Read-only standard Basic model: ZCLVersion8, bounded copied manufacturer/model/software strings, valid PowerSource and ClusterRevision3; no optional reset success | Sections 3.2.1-3, Tables3-7/8/16 pp.3-6..3-17;2.3.4.5 pp.2-6..2-7; [exact lab contract](ZCL_LAB.md) | Both-board host/sanitizer, complete-image/ABI and alias-aware simulation; synthetic configuration only. Write-family errors now covered by ZCL-WR-01 | No board or endpoint; physical Identify/client, application/reporting/persistence, primary device/profile selection and M3-M5 gates remain open; not full Basic or errata-aware conformance |
| ZCL-ID-01 | Caller-owned logical-time Identify countdown; unicast start/restart/stop, active Query Response/idle silence and actual Read/Discover of IdentifyTime/ClusterRevision2; atomic local failures | R8 3.5, Tables3-31..35 pp.3-30..3-34;2.5.12 pp.2-28..2-29; [time policy, wire rules and evidence](ZCL_IDENTIFY.md) | Both-board native/nonrecovering sanitizer, complete-image/ABI and alias-aware execution; abstract caller clock only. RW IdentifyTime updates now covered by ZCL-WR-01 | No physical indication, client/group/broadcast, send or authenticated endpoint. Application/profile, errata and M3-M5 gates unchanged |
| ZCL-WR-01 | Write/Undivided/No Response for Basic and real IdentifyTime; ordered existence/type/read-only errors, partial ordinary writes, atomic Undivided and local serialization failures, no-reply processing | R8 2.5.3–6/Figures2-10..14 pp.2-14..18; Table2-12 pp.2-55..56;3.5.2.2.1 p.3-31; [bounded contract/evidence](ZCL_WRITE.md) | Existing real codecs/handlers, exact native/sanitizer vectors and full linked/ABI/alias guards in both-board CI; no new board image or raised budget | Unknown/unsupported value extents are explicit local failures; only one full-range uint16 writable intent, no generalized permission framework, reporting, persistence or authenticated application; same errata/security gates |

## BDB 3.0.1 requirements

These are paraphrases of the selected base document, not implementations or
a complete conformance checklist. BDB page numbers below are printed pages
and match PDF page numbers. Base-text development follows the recorded errata
risk above; do not reuse section numbers or rules from BDB 1.0, or claim
errata-aware conformance before reviewing the corrections.

| ID | Behavior / decision | BDB 3.0.1 reference | Implementation / evidence | Gate |
| --- | --- | --- | --- | --- |
| BDB-01 | An ED must be able to join both centralized and distributed security networks; the first project configuration covers only centralized networks | sections 6.1-2 p.32; 10.2.1 p.74 | Centralized path planned; distributed unsupported, not an optional-role exemption | Full BDB conformance remains out of scope |
| BDB-02 | Preserve `bdbNodeIsOnANetwork`; restore persistent state and attempt secure NWK rejoin for a previously joined ED, announcing only on successful rejoin | sections 6.9 p.38; 7.1 pp.39-40 | Planned / none | M4-M5 |
| BDB-03 | Provide unjoined network steering: primary/secondary channel discovery, suitable permit-joining networks, MAC association, bounded authentication/network-key wait and explicit failure | sections 5.1,5.3.8-10; 6.5; 8.2 pp.43-47 | Planned / none | M5 |
| BDB-04 | Fresh-join completion includes a `Mgmt_Permit_Joining_req` broadcast, duration at least 180 seconds and `TC_Significance = 1`; this is not local child admission | section 5.1.2 p.22; section 8.2 steps 14-16 pp.46-47 | Planned / none | M5 |
| BDB-05 | For the R22 TC path, request a new unique TC link key, reject the wrong key type or unchanged key, then verify/confirm before accepting key exchange | section 10.2.5 pp.75-79; R22 section 4.4.8.2.3 pp.445-446 | Planned / none | M4-M5 |
| BDB-06 | ED link-key provisioning includes the default TC key, distributed key and install-code-derived key; install codes use 16 random bytes plus a two-byte CRC and AES-MMO derivation including that CRC | Table 8 p.33; sections 6.4 p.34,10.1 pp.72-74 | #22 CRC/derivation host-tested, image-checked and simulated; no private or production keys imported | M4; generation/provisioning and verified TC state still required; distributed path deferred |
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
