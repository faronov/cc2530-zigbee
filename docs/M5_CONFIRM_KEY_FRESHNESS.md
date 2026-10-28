# Confirm-Key freshness decision

Date: 2026-10-13. Source baseline:
`d0d5355196f667f89261666ac3dbd52260f5b69e`.
Compiler: `1a01afb49d99ec754e62ab0f65f182de87b4cee3`.
Review branch: `m5-confirm-key-review`.

**Decision C: UNRESOLVED. Firmware implementation remains BLOCKED.**
The incoming-counter reset is normative, but no accessible authoritative
interpretation establishes D's Confirm-specific rejection before operational
APS counter completion. The [normative decision gate](#normative-decision-gate)
records the primary evidence, access limits, clarification question and
independent next work. No source-changing candidate or new FULL was created.

This supersedes the earlier assumption that a separate confirmation domain
necessarily needs another persistent counter. The existing durable phase and
key identity can reject a whole class of out-of-transaction Confirm commands
without storing their counters. The unresolved part is authorization for placing
that rejection before operational APS replay completion, not a demonstrated
need for four more NV bytes.

## Accepted evidence restored, not repeated

The existing [security receive completion acceptance](M5_SECURITY_RX_COMPLETION.md#frozen-candidate-acceptance)
was verified from preserved evidence, not rerun: FULL38293766529,29/29 gates,
17/17 complete MCU cases, runtime stack101/128, static106/128,377 correct
thunks and0 incorrect. The shorter ID3829376529 is a transcription error.
The final receipt, exact ELF/HEX and68-file accepted manifest were rehashed;
the prior Policy C137-file manifest and all frozen firmware remain unchanged.

The previous Confirm reproducer and its results also remain available:
2066 observation checks in native and nonrecovering ASan/UBSan, deliberate
zero-initialization regression failure, and114 nonzero-Confirm power cuts
(3 exact old,1 exact new,110 fail-closed). These completed tests were not rerun.

The initial domain review below added observations needed to evaluate
phase/key ownership, very high counters, reset/rejoin and the next
establishment cycle. The subsequent normative decision is documentation-only;
neither those observations nor accepted validation were repeated.

## Normative interpretation

The target is Core R22 / BDB3.0.1, not R23 / BDB3.1. Printed document pages:

| Requirement | Primary reference | Consequence |
| --- | --- | --- |
| Incoming security-counter ownership | Core Table4-29, p454 | Four-byte counter belongs to the TC key-pair descriptor; it is not the eight-bit APS header counter |
| Incoming APS processing | Core4.4.1.2 steps1,3-4,6-8, pp419-420 | Reject exhausted/below-floor input, obtain source/key material, authenticate, then store received counter+1 |
| Replacement B installation | BDB3.0.1 10.2.5 step9, p78 | B must differ from current A; B's incoming counter starts at0; outgoing reset is optional |
| A remains valid during exchange | BDB10.2.5 note after step13, p79 | Never clear A's still-live operational history just because B was transported |
| Successful Confirm | Core4.4.8.2.3 steps1-9, pp445-446 | Validate destination context/status/key type/TC; mark the matching descriptor verified; incoming counter becomes0 |
| Shared derived-key history | Core4.5.3, pp457-458 | Data/key-transport/key-load keys share the link key's associated counters |
| Nonce domain | Core4.5.2.2 Figure4-20, p457 | Nonce contains source IEEE, counter and security control, not pending/verified phase |
| Confirm fields | Core4.4.10.8 Figure4-17, pp452-453 | No independent request challenge or fresh epoch identifier is carried by Confirm |
| APS reception and duplicates | Core2.2.8.4.2, pp57-58 | Secured reception still invokes chapter4 security; duplicate delivery filtering is not durable APS replay completion |

Pinned PDF SHA256:

- Core: `991dd02b7e5764ac349c47d5c4cf0fbe01529ff6594df03e8dad3d3d4df9e274`.
- BDB: `16471aa230657818da4c8440671efb530d80c71a975fce7af71c507ca7aa17d3`.

The two step9 operations are distinct. BDB initializes B upon installation;
Core resets its incoming counter on successful verification. The frozen
`Confirm counter+1` assignment is a confirmed deviation from the latter.

There is a new key epoch relative to A when genuinely fresh B is installed.
There is **not another cryptographic epoch between pending B and operational
B**: the same TC IEEE, B material, key-id0 and captured nonce/ciphertext remain.
An operational zero lower bound cannot by itself remember the accepted proof.
The specification's optional outgoing reset does not require or authorize
weakening this project's lifetime-monotonic outgoing reservations.

## Eight domains and persistence

| Domain | Actual representation / identity | Persistent requirement |
| --- | --- | --- |
| Outstanding transaction | TC IEEE + own IEEE + exact PK material + WAIT_CONFIRM | These already persist together; no independent request nonce is stored or carried |
| Pending-key cryptographic proof | MIC under PK with key-id0 and TC nonce; private trial during WAIT_CONFIRM | Trial plaintext and success marker are volatile; they never establish membership alone |
| One-time acceptance | Valid successful Confirm under the exact pending key, in the outstanding phase | Atomic durable phase/key transition consumes the context; no repeated promotion |
| Key promotion | LK<-PK, retirement of old LK, VERIFIED | Single existing counter-owner save before any event/output |
| Retired-key history | Old key material in PK with RETIRED; no operational fallback | Persist non-use/reuse tombstone; old scalar AF is not a second usable APS domain |
| New operational security state | LK=B, verified identity/context | Must persist; a logical lifecycle change is not a new nonce salt |
| Operational incoming APS floor | AF at88..91, shared by current LK and its derived keys | Durable monotonic completion within that operational state; required initial value0 remains unresolved in the implementation |
| Outgoing APS reservation | Existing counter-owner domain, exclusive durable ceilings | Unchanged across promotion, restart, rejoin and input rejection |

Sources: `src/security_keys.c:43-83,250-354,593-614,657-676,787-824,942-977,1030-1072`;
schema in [SECURITY_KEYS](SECURITY_KEYS.md#durable-112-byte-schema-version-2).

K2 remains112 bytes/version2; K3 remains144 bytes/version3. K3 bytes132..143
are reserved zero and the frozen reader enforces that. Flags/phase bits have
existing meanings; they are not unowned storage. The pending/retired16 bytes
cannot also store proof data while preserving their present material ownership.
Counter ceilings cannot be borrowed as proof counters.

## Phase and key identity: what they prove

The existing durable context already establishes:

- After promotion, B is LK, old A is a tombstone, and WAIT_CONFIRM for B no
  longer exists. Reset restores that closed context, not an outstanding proof.
- A later WAIT_CONFIRM for C does not reopen B's proof: PK=C, while B remains
  current LK. A valid old B Confirm cannot prove C.
- After C promotion, B is retired and no longer a decryption fallback.
- Rejoin and parent/NWK-slot changes do not open a Confirm transaction or
  create a new TC link-key epoch.

Thus a rule admitting Confirm **only from the pending-key trial** can exclude
all Confirm authenticated under the current key without a new persistent field.
It need not remember the old counter, ciphertext hash or eight-bit sequence.
That is sufficient context to prevent semantic re-promotion.

It does **not** change the result of ordinary cryptographic authentication under
LK. In the frozen ordering, current-key authentication advances AF in
`open_aps()` before `aps_command()` checks transaction context. The rejected
completion tail correctly checkpoints that floor. Moving only the later
semantic guard or returning a different error does not fix the poisoning.

## Captured Confirm transition table

| Boundary | Frozen behavior | Required distinction for a correction |
| --- | --- | --- |
| Before first acceptance, genuine matching pending context | Valid proof may complete once | A previously unaccepted proof is not a replay of a committed transition |
| After original acceptance, same NWK envelope | NWK REPLAY, no output | Preserve saved per-sender/key floor |
| After promotion, fresh valid NWK wrapper | A rejects captured APS counter below its proof-derived floor | B with zero permits authentication/accounting unless a separate freshness boundary intervenes |
| After cold reset | Restores saved key/phase/AF | Proof closure must survive reset; do not restore WAIT_CONFIRM from RAM |
| After secure same-parent rejoin | Existing B and AF retained | Rejoin is not an APS freshness proof |
| After known-router rejoin | Separate router NWK history; logical TC APS ownership remains B | A fresh admitted hop cannot authorize another B confirmation |
| Proof APS counter0xFFFFFFFE | Frozen promotion stores AF0xFFFFFFFF | Zero alone avoids the initial floor, but a replay can later set that exhausted floor |
| C pending in the next cycle | Old B proof fails the C trial but can authenticate under current B | Phase-only WAIT_CONFIRM test is insufficient; exact pending-key ownership matters |
| C promoted | Captured B proof fails current C authentication | Never try retired B as an operational fallback |

All authenticated fresh NWK wrappers still complete their own NWK replay
accounting. Preventing unauthorized APS/key changes is not permission to roll
back a legitimately authenticated outer NWK frame or authorized slot adoption.

## Alternatives A/B/C/D

| Design | Zero requirement | Old Confirm / counter poisoning | Atomic/reset behavior | NV and resource impact | Decision |
| --- | --- | --- | --- | --- | --- |
| A: existing received+1 | Fails Core Confirm initialization | Captured accepted proof rejects, but high original proof can start B at an exhausted operational floor | Existing atomic save and tested recovery | Existing K2/K3; measured baseline CODE222820, raw XDATA5220, static106/128 | Preserve reference, not the correction |
| B: direct zero assignment | Matches the reset assignment only | Fresh wrapper can authenticate old proof and checkpoint AF=c+1; no semantic promotion does not make this safe | Could share existing promotion save, but no corrected-image cut proof exists | No field needed; CODE delta not measured | Rejected |
| C: transaction-bound proof plus operational floor | Zero possible with separate freshness decision | Can protect if old proofs are rejected before operational completion; a distinct logical epoch alone cannot change MIC validity | Proof closure and B state must commit together | May use existing context if rejecting the command class; selective per-counter history would need separate representation/migration review | Not established as normative; do not assume a new field solves ordering |
| D: contextual pending-only Confirm gate before AF publication | Zero possible without new NV field | Exact phase+PK trial rejects all current-key Confirm, including B while C pending; generic current APS completion stays intact for other APDUs | Existing durable phase/key transition can close proof across reset/rejoin | Zero new durable bytes required for this class gate; parser/branch CODE/stack cost unmeasured | Smallest security design to review; normative placement unresolved |

No CODE/XDATA/stack cost for unimplemented alternatives is invented. The actual
firmware delta of this review is zero; all baseline physical proofs remain
unchanged. D would need authenticated, bounded command classification, not a
plaintext/counter-only shortcut or broad exception for unsupported commands.
It must distinguish an APS command identifier from application data bytes.

### Exact implementation gate that remains unresolved

For an old Confirm protected by current B, after operational initialization:

1. Its counter c is at or above AF=0 (or above later small application counters).
2. Its original MIC is valid under the unchanged B and TC nonce.
3. Core4.4.1.2 step8 requires received+1 completion after those checks.
4. The project's closed transaction correctly refuses Confirm semantics.
5. The task requires the same frame not to advance operational AF.

The late phase guard solves step4, not the conflict between steps3 and5.
D could solve the security behavior by making a Confirm-specific freshness
rejection before operational accounting. However, the pinned general incoming
procedure does not establish that exception merely from an absent pending
transaction. Core4.4.8.2.3 gives later primitive semantics; it does not state
that a current-key command may bypass step8. The earlier receive-completion
fix cannot be undone or silently specialized to assert such permission.

This is **not a proof that no conforming implementation can exist**. It is the
precise unestablished interpretation blocking this implementation gate:
an authoritative clarification for a transaction-specific freshness rejection
before operational APS completion, including already verified and later
pending-key states. Additional durable proof history is another possible
freshness input, not evidence of such normative authorization by itself.

An eight-bit APS duplicate table is not an equivalent answer: it is a delivery
filter with timing, not a reset-persistent32-bit proof history. Core2.2.8.4.2
still directs secured reception through chapter4. Secure rejoin, an arbitrary
high-counter limit, resetting an existing operational AF, a second save after
publication, or suppressing completion on generic semantic rejection are
also not acceptable substitutes.

## New independent observations on unchanged firmware

`confirm-domain-audit.c` links the genuine frozen security, wire, AES/CCM,
counter, journal and Flash services with public synthetic fixtures.
Native and nonrecovering ASan/UBSan each pass **1120 observation checks**.
This is additional host evidence, not acceptance of a firmware correction.

| Observation | Result |
| --- | --- |
| Pending bad MIC, wrong TC nonce identity | No promotion, no A-floor advance, no output |
| Reserved APS counter0xFFFFFFFF | Rejected as FORMAT by the wire inspector; no promotion or AF advance |
| Current-A Confirm with no outstanding transaction | CONTEXT, no membership change/output, but required current-key completion stores AF401 |
| Valid first B Confirm400 | VERIFIED, matching retired A; AF401 demonstrates unchanged deviation |
| Identical NWK envelope / fresh wrapper under strict rule | REPLAY |
| Zero-state model, initial B application counter0 | DATA accepted; durable AF1; duplicate application counter0 rejected |
| Zero-state model, captured Confirm400 | CONTEXT and no output; durable AF1->401 |
| Zero-state model, captured Confirm0xFFFFFFFE after reset | CONTEXT and no output; durable AF becomes0xFFFFFFFF |
| Same-parent and known-router rejoin followed by that proof | Same exhausted-floor advancement; rejoin is not freshness |
| Old B proof while C is pending | Cannot promote C; nevertheless advances current B's operational floor |
| Old B proof after C promotion | AUTH failure; C history unchanged; retired B not reused |

The zero-state tests modify only AF in a synthetic NV model after genuine
promotion. They are explicitly **final-state behavior models**, not a two-save
promotion implementation or proof of atomic zero initialization. Fresh wrappers
reuse the byte-identical captured APDU; the attacker is not given B.
The successful poisoning requires an admitted valid NWK envelope, not merely
an increasing unauthenticated counter.

Existing power-cut/readback/busy-fail-stop evidence was retained. No new
zero-promotion or D implementation exists, so its every-command cuts,
ambiguous-commit recovery, FAST, SMOKE and final FULL are **not performed and
not claimed**. Once the gate is resolved, those remain mandatory, together
with no-publication checks, all operational replay cases and original recovery
scenarios. Invalid APS MIC may still consume an authenticated outer NWK floor,
as required by the accepted completion model.

The fixture initially attempted to generate a frame at the forbidden maximum
counter; the genuine wire sealer correctly refused. The exhaustion input was
then constructed as an invalid header inside a genuine protected NWK envelope.
The wire rejection is FORMAT, not the fixture's initial guessed REPLAY result.
No production behavior or verifier was relaxed to obtain these observations.

## Atomicity, reuse and next decision

Any authorized implementation must preserve one promotion checkpoint:
trusted identities, B/current ownership, old-key non-use, closed proof context,
new operational AF and all live incoming NWK floors. Outgoing reservations
are unchanged. Before successful commit no output may escape; ambiguous
physical commits fail closed until genuine reopen resolves the readable state.

Current/retired key equality checks prevent immediate reuse. The bounded
one-predecessor tombstone is not a lifetime archive of every key ever used;
neither this report nor D claims freshness if much older identical material is
reintroduced after its history was discarded. Extending that archive would be
generic key management outside this slice.

**No K2/K3 schema change, new journal, source correction or validation dispatch
is approved by this review.** The domain review left D's Confirm-specific
pre-accounting boundary for the normative decision below, not an allocation
of reserved K3 bytes. Other Core/BDB command-protection questions remain
separate review gates.

## Normative decision gate

Decision dated 2026-10-13: **C - UNRESOLVED**.

This concludes the bounded research task, not the firmware implementation
gate. The pinned base text supplies a concrete ordering obstacle to D; no
accessible applicable erratum or authoritative conformance interpretation
resolves it. Missing primary documents are not evidence that an exception
does not exist. Consequently this review does not assert either permission
for D or a definitive standards-wide prohibition.

### Exact evidence and receive ordering

The PDF identities above were rechecked and are unchanged. All pages below
are printed pages; requirements are paraphrased, not replacement spec text.

| Question | Pinned primary evidence | Established result / limit |
| --- | --- | --- |
| APS security before semantic admission | Core4.4.1.2 introduction and steps3-8, pp419-420 | Secured APS reception invokes the procedure. Source/key material selection and counter comparison precede MIC checking; successful checking is followed by storage of received+1 in the selected security material. No Confirm transaction-context exception appears in this procedure. |
| Pending versus operational descriptors | Core4.4.1.2 step3 and Table4-29, p454 | Data-key material comes from the source's key-pair set; the descriptor has key attributes, key material and a32-bit incoming counter. The procedure gives no early-rejection branch based on VERIFIED_KEY or absence of a pending transaction. Project pending/current storage is not itself a normative exception. |
| Confirm transaction scope | Core4.4.10.8 and Figure4-17, pp452-453 | Confirm acknowledges an updated peer link key; its fields identify status, key type and destination. They do not carry a challenge or independent establishment-epoch identifier. This purpose is not explicit authorization to bypass step8. |
| Successful Confirm indication | Core4.4.8.2.2-3, pp445-446 | Reception generates the primitive. Its checks precede VERIFIED_KEY and incoming-zero assignments. These are indication effects, not a stated pre-security transaction-freshness test. |
| B installation and verification | BDB3.0.1 10.2.5 steps9,11-13 and final note, pp78-79 | Incoming zero at B installation is distinct from Core's zero on successful confirmation. A remains usable until the B-protected verification response succeeds. No out-of-transaction Confirm receive-order exception is specified here. |
| Duplicate handling | Core2.2.8.4.2, pp57-58 | Secured reception still invokes chapter4. The timed source/APS-counter duplicate table suppresses repeated upper delivery; it does not establish reset-persistent Confirm freshness or permission to omit operational security-counter completion. |
| Cryptographic epoch and counter domain | Core4.5.2.2-3, pp457-458 | The nonce includes source, security counter and control; derived link keys share counters. Pending-to-verified phase is not a fresh nonce domain. Incoming reset does not require a reset of this implementation's outgoing reservations. |

For a first accepted pending-B proof, the base-text sequence is APS security,
Confirm indication, then incoming-zero initialization of the verified B
descriptor. For a later captured B proof protected by current B, with
`c >= AF` and a valid MIC, the general procedure again reaches step8.
The later semantic refusal is not authority to undo that completion.

D would instead insert a transaction-freshness decision after authenticating
the command, but before step8 publishes/completes the current descriptor's
counter. The exact missing authority is permission for that insertion when
B has no matching pending transaction, including when a different C is pending.
No plaintext-only drop or general semantic-rejection exemption is proposed.
Authenticated outer NWK completion remains independent in either case.

No special normative treatment of a previously accepted Confirm was found in
the reviewed clauses. This statement is limited to the accessible pinned
texts, not a claim about unavailable errata or test cases.

### Errata and conformance interpretation: access limits

| Source / route | Observed evidence | Review status |
| --- | --- | --- |
| BDB3.0.1 sections1.5-6 and2.1, pp15-17 | R6 identifies Test Plan16-02826; R7 identifies Specification Errata21-65431. These are distinct primary documents, not optional implementation examples. | References reviewed; exact applicable revisions and primary contents not obtained. |
| [CSA specification downloads](https://csa-iot.org/developer-resource/specifications-download-request/) | Actual page HTML lists Core, BDB and other public specifications. Its Zigbee list includes BDB3.0.1 and the known Core download, labelled R22.1 by the website. It does not supply the needed errata/test-plan text or Confirm interpretation. | Public catalogue inspected. Website labels/newer R23/BDB3.1 downloads do not replace the hash-pinned project baseline. |
| [Official CSA ZUTH page](https://csa-iot.org/certification/tools/zuth/) | Actual page HTML identifies ZUTH as the official certification tool, lists BDB3.0.1 test coverage, and describes licenses for Alliance members at Adopter level or higher. | Public overview reviewed, not its test scripts/verdicts. No licensed test case or Confirm-specific interpretation was accessible in this task. |
| Official CSA site search, exact document-ID public searches and GitHub code index | CSA search for21-65431 returned no result; exact-ID index searches supplied no primary text. | Search limitations only, not evidence that the documents or an applicable interpretation do not exist. |
| Retrieval failures | AI-assisted search failed with a service error; direct HTTP retrieval of CSA pages returned403. The web-fetch interface subsequently exposed the relevant catalogue/ZUTH HTML. Google/another search endpoint did not expose usable results. | Failed attempts are not successful review. No membership authentication, document-access bypass or external transmission of project data was attempted. |

**Applicable errata review remains incomplete.** No authoritative published
R22 correction or accessible16-02826 interpretation answering this exact
ordering question was obtained. Issues
[#16](https://github.com/faronov/cc2530-zigbee/issues/16) and
[#17](https://github.com/faronov/cc2530-zigbee/issues/17) therefore remain open.
An SDK ordering, successful commissioning trace, extra durable proof field
or passing local regression would not supply the missing normative authority.

No permitted alternative satisfying both zero initialization and the requested
pre-accounting freshness rejection has been established. The current
`Confirm counter+1` protective deviation is retained and explicitly remains
nonconforming to Core's Confirm zero assignment; it is not relabelled as a
conforming alternative.

### Clarification question for standards maintainers

Draft for the CSA Zigbee Core/BDB maintainers, routed through the
[Alliance contact / certification team](https://csa-iot.org/contact-us/)
if authorized. **Not sent.**

> For Core05-3474-22 with BDB16-02828-012: after a successful B-protected
> Confirm, Core4.4.8.2.3 step9 sets B's incoming counter to zero. A captured
> Confirm can still pass B's MIC and counter check inside a fresh admitted
> NWK envelope, although no corresponding pending-B transaction remains.
> May an implementation authenticate it, reject it as out-of-transaction
> before4.4.1.2 step8, and leave operational AF unchanged? Does this also
> apply after reset or while C is pending? If not, which permitted ordering
> preserves zero initialization without old-Confirm counter advancement?
> Please identify the applicable erratum revision/clause and16-02826 test
> interpretation.

### Return to independent M5/M6 work

Confirm development stops at this gate. The following are proposed bounded
slices, not implementation authorization in this documentation-only task.
The live issue roadmap is an index, not evidence that every integration
item remains unimplemented: the accepted runtime already serves synthetic
ZCL traffic, limited binding and sustained reports.

| Priority | Independent slice and source evidence | Bounded next evidence / remaining gate |
| --- | --- | --- |
| 1 | Runtime Read Reporting Configuration. `src/zcl_sensor.c:277-309` dispatches Read/Write/Configure but returns unsupported for command08; the standalone [Temperature owner](ZCL_TEMPERATURE.md) already handles the generic procedure. | Read the actual four reportable runtime records; test success/error record layouts, types, malformed payloads, destination/direction checks, response bounds and no mutation. Reuse existing wire/value rules where appropriate; do not reimplement the standalone reporting engine. This can proceed as generic bounded correctness without choosing a new advertised device ID. |
| 2 | Profile/descriptor truthfulness, [#29](https://github.com/faronov/cc2530-zigbee/issues/29), then missing ZDO services in [#30](https://github.com/faronov/cc2530-zigbee/issues/30). `include/zdo_srv.h:29` still declares deviceFFFF; `src/zdo_srv.c:81-122` builds the limited endpoint descriptors and generic fallback. | Pin an applicable primary application/device definition before changing advertisement. Implement individual missing address/power/match services against actual registered endpoints; retain explicit unsupported outcomes and notification/response-loop suppression. Test echoed transactions, queried-address errors and exact descriptor/cluster consistency. A working interview does not close BDB/test-plan gates. |
| 3 | Persistent application reporting/binding configuration, #30/[#31](https://github.com/faronov/cc2530-zigbee/issues/31). `include/zcl_sensor.h:40-56,78-87`, `src/zcl_sensor.c:314-340` and [runtime documentation](ED_JOIN.md#synthetic-temperature-and-humidity-demo) establish RAM-only configuration and reset-to-default coordinator bindings. | First review an explicit bounded application-storage owner/partition and compatibility policy; do not borrow security K2/K3 fields, reserved bytes or outgoing reservations. Then test old/new-or-fail-closed cut recovery, storage failure, retained unbind/disable/configuration and reset/rejoin restoration before reports resume. No generic security journal is proposed. Capacity/resource cost is not yet measured. |
| 4 | Source-binding management and report transport completion, #30/#31. Existing bindings are limited to one coordinator endpoint per reportable cluster; actual scheduling already exists. | Add required Mgmt_Bind enumeration/pagination once table ownership is defined; cover full/absent/invalid binding cases. Test report preparation versus queue admission versus actual completion, busy queues, failures, configuration changes and rejoin. Audit full Mgmt_Leave through its genuine existing leave/security owners as its own bounded slice; do not silently relax security-command admission. |
| 5 | Offline interoperability corpus, then separately authorized [#32](https://github.com/faronov/cc2530-zigbee/issues/32). | Build public/synthetic ZHA and Zigbee2MQTT interview/read/configure/bind/restart sequences with exact unsupported outcomes. These are regression evidence, not live interoperability. Physical coordinator acceptance #28, software/adapter diversity, #18 errata and fresh hardware/network authorization remain independent gates. |

M5 authentication/replay/recovery remains the preserved prerequisite, not work
to repeat. Core/BDB security-command protection and broadcast NWK key updates
stay behind separate normative gates. Missing errata/test text prevents
conformance claims but does not automatically block unrelated pinned-base
generic ZDO/ZCL work.

All future source slices must measure physical CODE/bank placement, XDATA/DATA
ownership and the existing128-byte stack limits before qualification. The
accepted image has several zero-headroom banks; aggregate free CODE is not a
placement budget. No resource estimate is invented for these unimplemented
slices.

This normative task changes documentation only: **CODE/XDATA/DATA/stack and
NV-schema deltas are zero**. No native/sanitizer replay, FAST, SMOKE, FULL,
compiler/HWLab change, hardware operation or production merge was performed.
Accepted firmware branches, images, receipts and evidence manifests remain
unchanged. The old Confirm implementation remains intentionally frozen until
the missing authoritative interpretation is obtained.
