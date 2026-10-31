# M5 incoming replay persistence: decision for approval

**Architecture review only; recommendation C is not approved or implemented.**
Per-frame nonvolatile storage of ordinary incoming router NWK counters is not
established as a requirement by the reviewed Core R22 / BDB 3.0.1 base texts.
It is a stronger project guarantee than maintaining incoming counters in the
NIB during operation. Removing that guarantee is nevertheless a real security
policy change, not an equivalent storage optimization.

Recommend extending the existing TC volatile-NWK policy to the one trusted
router, while retaining durable lower bounds and all existing APS/security
transition commits. Do not implement the proposed append journal unless the
product requires strict replay exclusion across resets, or the remaining
durable traffic proves to need it. Approval must explicitly accept the
cold-reset exposure described below. If that exposure is unacceptable, retain
policy A and continue the strict-durability storage proof instead.

## 1. Baseline, provenance and evidence boundary

The qualified firmware remains
`e4c41c8e8a89bfe0ef517bf04dc3747b904783d1` on `m5-secure-rejoin`, with
compiler `1a01afb49d99ec754e62ab0f65f182de87b4cee3`.
The actual successful FULL run is
[38140096302](https://github.com/faronov/cc2530-zigbee/actions/runs/38140096302);
the supplied `3814096302` returns 404. The sealed result is 27/27 steps and
15/15 scenarios PASS, static stack 106/128, measured peak 101 and zero incorrect
bank thunks. The [storage audit](M5_DURABLE_REPLAY.md#qualification-gate)
records the unchanged image hashes and receipt identity. That is bounded
known-router acceptance, not sustained-traffic or complete-conformance proof.

Local `m5-durable-replay` was verified at
`edc53eb21fdf659f14cd8bcac7057a01196b36f4`, authored/committed by `faronov`.
It descends from the qualified candidate without rebasing it:

| Commit | Scope |
| --- | --- |
| `bc0c9b2172355c274bfe2022c110eea17c41ed81` | Test-only baseline storage amplification audit |
| `49fce4a459c966a084ae4d271fda87ae34fd5417` | Host-only append/checkpoint reference and fault corpus |
| `edc53eb21fdf659f14cd8bcac7057a01196b36f4` | Documentation-only FULL qualification record |

`edc53eb` changes two documents, not firmware. The complete branch adds tests,
Make targets and documentation; `src/` and `include/` are unchanged from the
qualified candidate. The remote branch ref was unavailable (404); this
provenance establishes the **local branch**, not a published remote branch.
No accepted receipt or frozen artifact was rewritten.

The primary documents were read from the already available PDFs after
rechecking their SHA256:

| Source | Identity / SHA256 |
| --- | --- |
| Core R22 | 05-3474-22, 2017-04-19; `991dd02b7e5764ac349c47d5c4cf0fbe01529ff6594df03e8dad3d3d4df9e274` |
| PRO BDB 3.0.1 | 16-02828-012, 2021-09-28; `16471aa230657818da4c8440671efb530d80c71a975fce7af71c507ca7aa17d3` |

Public source locations and provenance are in [PROVENANCE](PROVENANCE.md).
References below use **printed pages** (Core PDF page = printed page + 25;
BDB printed/PDF pages match). These are paraphrases, not reproduced text.
Applicable errata 21-65431 and certification tests remain unreviewed; neither
this decision nor a synthetic-parent FULL establishes certified conformance.

## 2. Normative requirement matrix

Here, an incoming floor is the **exclusive next acceptable** value: admit wire
counter `c >= floor`, reject wire `0xFFFFFFFF`, then set the floor to `c + 1`.
Stored `0xFFFFFFFF` is a valid exhausted floor, not a value that may be sent.
NIB/AIB storage is not synonymous with one flash transaction per frame.

| Domain | Base-text requirement and precise reference | Persistence conclusion / project distinction |
| --- | --- | --- |
| Outgoing NWK | Core 4.3.1.1 pp.412-413: select key/counter, reject exhaustion, increment/store after security. 4.3.4 p.416: retain across factory reset and network changes; a single shared instance is permitted with a narrowly specified switch/reset exception. 4.3.4.1 p.417 explicitly describes store-ahead to avoid flash wear. | Durable monotonic reservations are justified; **per-TX flash writes are not required**. Preserve the project's existing two-domain reservation service and no arbitrary reset policy. Do not enable the optional reset exception through this change. |
| Outgoing APS | Core 4.4.1.1 pp.418-419: use/increment the counter associated with the selected key. 4.5.2.2 p.457 defines the nonce using sender/counter/security control; Annex A, CCM forward-transformation input, p.486 requires nonce uniqueness within an encryption key. BDB 10.2.5 step 9 p.78 permits outgoing APS reset on a genuinely new TC key. | Reusing the same-key TX counter after reset risks nonce reuse; retain the existing durable APS reservations. No per-frame flash requirement is stated here. The project's lifetime-monotonic APS domain is stronger than allowing reset for genuinely new key material. |
| Incoming NWK: TC sender | Core 4.3.1.2 pp.413-414: match key sequence/material, check sender IEEE floor, authenticate, store sender and `c + 1` in the NIB; respect `nwkAllFresh` capacity failure. Tables 4-2/3/4 pp.415-416 define material sets and per-sender counters. | Maintain independent sender/key history in operation. These clauses do **not specify per-frame nonvolatile writes**. Current TC ordinary NWK handling is already RAM-based with durable lower bounds. |
| Incoming NWK: router sender | The same Core 4.3.1.2 procedure applies to the sender of hop security. Core 4.5.1.3 / 4.5.2.2 p.457 distinguish the security-originating IEEE from logical NWK origin. | No separate router-specific flash rule was found. A forwarding router's counter must not be checked against or merged with the TC's counter. Per-frame durable router floors are the project's additional guarantee. |
| Incoming APS / TC commands | Core 4.4.1.2 pp.419-420: bind key pair/source, check freshness for a **unique** link key, authenticate, store `c + 1`. Core 4.4.11 Table 4-29 p.454 defines per-key-pair incoming/outgoing counters. Core 4.5.3 pp.457-458 associates derived-key counters with the underlying key. | No per-frame nonvolatile requirement found in these clauses; do not infer one from NWK outgoing requirements. Nevertheless **all presently accepted APS-secured inputs and TC command transitions remain durable by project policy and this task's constraints**. Applying freshness checks also in the global initial-key profile is stronger than the conditional unique-key wording; preconfiguration alone does not make a key global. |
| TC command authority / protection | Core 4.4.1.3 Table 4-6 p.421, sending Table 4-7 p.422 and 4.4.1.5 p.423 distinguish command-specific APS protection. BDB 10.2.2 / 10.2.3 p.75 binds established TC identity and requires updated-TC-key encryption of TC APS commands. | Identity/key binding remains mandatory. NWK protection alone is not TC-exclusive cryptographic authority. There is a base-text tension for NWK-only Switch-Key / broadcast Transport-Key; see section 4. Do not resolve it by silently weakening APS rules or adding APS to every command. |
| Network-key rotation | Core 4.6.3.4.1-2 p.467: next sequence modulo 256, two key entries, TC-originated switch, reset counters belonging to a genuinely replaced key. 4.3.1.2 step 7 p.414 adopts a newer **entry**. 4.3.4 p.416 separately permits a shared outgoing instance. | A new key material identity may have new incoming history; switching between existing entries is not permission to erase their histories. Duplicate transport/switch must not reset floors. Keep current shared outgoing reservations unchanged. Sequence equality/wrap alone does not identify fresh material. |
| TC link-key exchange / history | BDB 10.2.5 steps 9-13 pp.78-79 rejects unchanged new keys, initializes the genuine new key's incoming counter, and retains old A until B is successfully verified. Core 4.4.8.2.3 steps 8-9 pp.445-446 marks VERIFIED and resets incoming APS counter at Confirm receipt. | New-key initialization must be isolated from old-key history. Current staged-B promotion/tombstones are retained. Its `Confirm counter + 1` policy differs from the explicit Core reset-to-zero step; stricter does not mean textually conformant. |
| Parent change / secure rejoin | Core 3.4.6/7 pp.306-308, 3.6.1.4.2 pp.341-344, 4.6.3.3.1 pp.464-465 require the appropriate parent/address/security procedure. Secure rejoin requires no extra authorization beyond NWK-security validation. BDB 6.9 p.38 / 7.1 pp.39-40 restore membership, attempt secure rejoin and announce on success. | No instruction to reset all incoming floors or proof that rejoin refreshes every sender/key history. Preserve floors for old and selected owners. A successful authenticated response is **not a universal replay barrier**. |
| Leave / factory reset | Core 3.2.2.19.3 p.267 removes relevant persistent data for Leave with Rejoin FALSE; TRUE recommends preserving application information. Core 3.6.1.10.4 pp.361-362 distinguishes rejoin from clearing network/security NIB state. Core 4.3.4 p.416 and BDB 9 pp.69-71 preserve outgoing NWK across reset mechanisms. BDB 9.1 p.69 distinguishes optional Basic attribute reset from network reset. | Current durable LEFT/history retention is a bounded abandonment state, not a complete factory-new implementation. Replay-policy work must not add key/counter erasure. A future factory-reset design must separately reconcile network-data removal with retained anti-reuse history. |
| Power loss / restart persistence lists | Core 2.2.8.1 p.54 lists minimum APS persistent data; 3.6.8 p.391 lists NWK persistence recommendations/particular required child data, including parent information; 3.6.1.12 p.362 discusses reset and optional restoration of NWK quantities. BDB 6.9 / 7.1 pp.38-40 adds membership and initialization. | Incoming per-frame replay counters are not in those lists. This supports distinguishing RAM NIB state from NVM, but **absence alone is not proof that every RAM/reset policy is conformant**. Preserve durable credentials/membership/lower bounds and the explicit ED recovery gate. |

**Conclusion:** the reviewed text establishes runtime incoming freshness and
outgoing nonce/counter requirements, but does not establish the project's
strict per-router-frame flash contract as normative. Policies B/C can implement
the stated incoming algorithm in RAM; their reset/threat semantics must still
be explicit. This is not a claim that the current firmware meets every R22/BDB
requirement.

## 3. Current implementation behavior

Read-only evidence is the production
[key owner](../src/security_keys.c), [counter owner](../src/security_counter.c),
[journal](../src/nv_record.c), [NWK/APS admission](../src/nwk_aps.c),
[BDB resume](../src/bdb_join_init.c) and [BDB receive](../src/bdb_join.c).
The relevant existing contracts are [keys](SECURITY_KEYS.md) and
[counter reservations](SECURITY_COUNTERS.md).

| State | Actual behavior at `e4c41c8` |
| --- | --- |
| Outgoing NWK and APS | Two independent 256-counter store-ahead domains. After reset, RAM next starts at the durable ceiling. Reservation succeeds before output; failed transmissions may burn counters. Exhaustion fails closed. |
| TC incoming NWK | Two four-byte RAM floors (`floor_cache[8]`). `load()` overlays them on the durable K2/K3 snapshot; plain DATA/MANAGEMENT without APS protection or adoption is volatile. A subsequent **key-owner save** checkpoints them. |
| Router incoming NWK | K3 offsets 122..129, two independent floors for the saved router IEEE and slot identities. `receive()` additionally requires `sender == ROUTER` to trigger a full save, even for ordinary traffic. |
| Incoming APS | Operational TC key and derived keys share the durable four-byte floor at 88..91, not the router NWK domain. Pending B is permitted only in WAIT_CONFIRM for successful Confirm; it is not a general decryption fallback. |
| Security transitions | Transport-Key, Confirm-Key, Switch-Key, PAN Update, Leave, authenticated Rejoin success/refusal and automatic newer-key adoption save before reporting their event. Outbound request/verify/rejoin/Leave phase changes also save before output. |
| Key history | Two resident NWK keys, per-slot sender histories, staged TC B and most-recent retired TC tombstone. Duplicate same-key transport preserves floors; material reuse/sequence conflicts are rejected. This is bounded history, not an unbounded archive of all retired keys. |
| Parent ownership | One TC and one independently trusted router, no arbitrary router learning/eviction. ATTEMPT is separate from admitted ACTIVE. Selection/admission persists identity and state without lowering saved router floors. |
| Reset / READY | VERIFIED or REJOINING credentials resume into RECOVERING, not READY. Current receive gating permits contextual recovery control, not application data during REJOINING. ED Timeout, Device Announce and runtime readiness still have to be restored. |

The snapshot journal remains the sole NV writer and uses commit-last atomic
replacement of the inactive page. A save preserves outgoing ceilings and the
live outgoing ranges below them; only exhaustion or a reset reserves again.
**Outgoing reservation commits do not automatically checkpoint volatile
incoming floors**: the counter owner has the durable snapshot, not the key
owner's live floor cache. Do not promise periodic incoming checkpoints from
outgoing activity.

Security admission advances its staged NWK floor only when the combined
supported NWK/APS semantic operation succeeds. The transport can subsequently
discard a packet (e.g. an APS retry duplicate) without undoing that floor.
Queue occupancy is checked before security admission; no consumer publication
precedes the required save/cache update.

## 4. Conformance differences independent of persistence choice

These are existing base-text findings, not firmware changes in this review:

| Finding | Assessment / consequence |
| --- | --- |
| Core-required authorized broadcast NWK-only Transport-Key | Core 4.4.1.5 p.423 says to accept this update. Current `transport()` requires APS security and unicast to own address. Unsupported behavior conflicts with that base-text requirement; an ordinary-data RAM policy does not fix it. |
| BDB blanket TC APS-encryption rule versus Core command-specific rules | BDB 10.2.3 p.75 requires updated-key encryption of TC APS commands, while Core Tables 4-6/7 allow/require NWK-only Switch-Key and 4.4.1.5 requires the broadcast update above. Current NWK-only Switch follows Core but cannot be claimed reconciled with the BDB sentence. Applicable errata/interpretation must be reviewed separately. |
| Confirm-Key incoming APS reset | Core 4.4.8.2.3 step 9 p.446 explicitly sets the counter to zero; current promotion stores the received Confirm counter + 1. A peer using the prescribed low post-Confirm counter could be rejected. This deliberate replay policy is not proof of exact Core conformance; do not silently change it through the NWK review. |
| NWK-security completion versus upper semantics | Core 4.3.1.2 step 6 p.414 advances the NWK floor after successful NWK CCM, before subsequent APS/command processing. Current staged owner discards that advance if later decode, APS authentication or command context fails. Thus a later lower NWK counter may pass after an authenticated-but-unsupported earlier frame. This layering difference needs its own test/decision; per-frame flash does not correct it. Bad NWK MIC must still never advance a floor. |
| Parent information persistence | Core 3.6.8 p.391 recommends persisting ED parent information. The TC-path Timeout intent/information bits currently stay volatile until another save. Restoring by a new Timeout exchange is a project recovery policy, not literal per-event persistence. |
| Leave / factory reset | LEFT deliberately retains credentials/history and blocks operation. It is not the complete no-rejoin factory-new behavior specified by BDB 9 / Core Leave. Basic attribute reset is distinct; do not claim it clears network security state. |
| Unsupported configurations | Distributed security is not implemented even though BDB requires joining both security models; arbitrary/new router learning, generalized TC migration and TC-rejoin key refresh are outside this bounded profile. Existing synthetic-parent PASS does not waive them. |

The proposed first change preserves these behaviors; it must not be presented
as fixing or certifying them. In particular, do not copy a permissive rule from
another Zigbee revision or invent an erratum to reconcile the two base texts.

## 5. Compare the three policies

Policy A means the **actual** baseline: strict router NWK, already-volatile TC
ordinary NWK, durable APS/security transitions. B means all supported ordinary
incoming NWK floors are RAM-resident above saved initial lower bounds, with
durable identity/key state and a cold-reset recovery gate; security commits
still required by this task cannot be discarded. C adds explicit checkpoints
of all live owner/slot floors at security-sensitive transitions.

| Dimension | A: strict router per frame | B: RAM ordinary NWK + recovery | C: RAM + selective checkpoints |
| --- | --- | --- | --- |
| R22 implications | Stronger router reset guarantee than found in base text; does not repair section 4 gaps | Runtime algorithm can be RAM-based; reset gate alone is not sufficient evidence of full conformance | Same runtime requirements; retains more reset history; still not certification |
| Normal uptime | Independent sender/key floors reject duplicate/older **admitted** frames | Same if RAM floors never reset/evict on parent or slot changes | Same; checkpoints do not replace RAM comparisons |
| Cold reset | Previously admitted router NWK frames remain excluded; TC ordinary traffic already has a checkpoint window | Recorded ordinary frames above durable lower bounds can return after recovery, potentially a large suffix | Same kind of exposure, but checkpoints shrink it; no maximum age/size guaranteed between transitions |
| Parent change | Router history durable; TC retains live RAM in the same boot | Must preserve old and selected IEEE/key histories; a parent switch is not a cache reset | Preserve both and checkpoint at ATTEMPT/rejoin/admission transitions |
| Key rotation | Separate histories for two resident slots; durable adoption/key state | Must bind RAM to key material/owner, not sequence or active-slot bit alone | Checkpoint old/live floors with transition, initialize only a genuinely replaced slot; retain other slot |
| APS/security commands | Durable before event; authority/context checks remain | Those durable paths remain mandatory, even in B | All existing APS and security events durable, carrying live NWK floors as well |
| Erase/program rate | One erase/45 programs for every admitted router frame, plus reservation amplification | Zero direct writes for eligible ordinary RX; durable credentials/APS/events still write | Zero direct writes for eligible ordinary RX; one existing snapshot at each qualifying owner transition |
| RX servicing | Snapshot foreground stalls on ordinary router traffic | Avoids those storage stalls; AES/decode/queue costs remain | Avoids ordinary stalls; selective control and outgoing reservations still stall |
| RAM/XDATA | TC floors already use 8 bytes; router floors come from the durable snapshot | Two router floors require an estimated additional 8 long-lived bytes | Same +8-byte estimate; no second page buffer/log parser proposed |
| Complexity / failures | Existing fail-closed journal; low boot budget and flash wear | Simpler writes, but easy to lose history on lifecycle changes; boot replay risk | Reuses save points; must prove cache/checkpoint/key identity coherence and commit errors; risk remains between checkpoints |

An unrestricted B that also makes APS/security commands volatile is **not an
eligible option** under this task. Once B's mandatory security writes carry
live floors, it largely converges on C; C documents that obligation rather
than leaving it accidental.

## 6. Recommended policy and security-command classification

Use **C**, conditional on product acceptance of ordinary NWK replay exposure
after cold reset. The change is scoped to the supported, one-trusted-router
profile. Do not claim that NWK-only traffic is harmless or that all commands
are equivalent.

Outgoing NWK/APS reservations, durable TC keys and identities, accepted network
state, membership and retained key history stay unchanged. There is no
automatic insecure-commissioning fallback or shared TC/router replay domain.

| Input / transition | Required proposed admission |
| --- | --- |
| Supported NWK-only DATA, including ordinary APS DATA/ACK without APS encryption | Authenticate and advance the correct RAM sender/key floor before exposing output. No per-frame flash. Any application authorization/freshness rules remain separate. |
| Network Status and contextual ED Timeout Response | Same RAM policy, matching the current TC behavior. Preserve request context checks; a recorded response can satisfy a later identical request if above the restored checkpoint. No fresh nonce is added by this policy. |
| Any presently accepted APS-encrypted frame | Preserve current durable APS floor, TC binding and any associated NWK floor. Derived data/transport/load keys share the existing key's APS history; router hop history stays independent. |
| Transport-Key / new TC key / Confirm-Key | Commit key identity, pending/retired state, counter state and affected/live NWK floors before event. Keep existing Confirm behavior pending its separate conformance decision. Never clear old A history merely because B is staged. |
| Switch-Key or authenticated auto-adoption | Durable active/newer-slot change **even without APS encryption**; preserve the other slot. Duplicate fresh-counter switch still checkpoints, not a no-write shortcut. |
| PAN Network Update | Durable network parameters and consumed floor before event, including idempotent updates with a fresh counter. Channel/manager changes remain unsupported. |
| NWK Leave, Rejoin success or Rejoin refusal | Durable membership/parent/phase and consumed floor before event. Refusal is authenticated input, not silence. Preserve keys/anti-reuse history; READY is not implied by a saved VERIFIED phase. |
| Parent registration/selection, outbound request/verify/rejoin/Leave phase transition | Retain existing owner save points, including all live histories for unchanged owners/keys. Selection alone never admits a new parent. |
| Update Device, Remove Device, inbound Request-Key, Tunnel, inbound Verify-Key, unsupported Transport-Key variants | Still explicit unsupported receive paths; do not add permissive handling or classify them as ordinary traffic. If later implemented, apply Core Tables 4-6/7, TC authority and a separate durable security-effect review. |
| APS DATA carrying application/ZDO configuration | Not automatically a security-owner event. Current sensor Bind/Unbind, reporting configuration and bounded writes can modify RAM/application state. NWK-only replay can repeat those effects after reset; deployments needing durable freshness for such actions require explicit APS/authenticated application protection or separately approved checkpoint classification. |

No base-text per-command flash mandate was established for the table's
incoming counters. The stronger commits are **project policy** to retain
security/key/membership effects and their replay lower bounds. Freshness and
authority are separate: a shared NWK key authenticates a key-holding network
member, not uniquely the TC. Source fields are not a signature.

## 7. Threat model and residual replay risks

Assume an attacker can record/reinject valid protected frames, manipulate
delivery/availability and exploit a reset; keys and flash writer remain trusted.
Do not claim protection against key extraction, a malicious network-key
holder forging counters/identities, or coherent rollback of both NV pages.
The current journal has no external monotonic anti-rollback anchor.

For unchanged owner/key, let durable checkpoint be `D` and live RAM floor
immediately before reset be `L`. Under C, an already admitted NWK-only frame
with `D <= c < L` can pass again after cold reset if context/address/key checks
permit it. An attacker can replay a recorded increasing **suffix**, not just
one frame, and may repeat this over successive resets. There is no promised
time bound, frame-count bound or periodic checkpoint. A high recorded counter
may also suppress legitimate lower delayed frames.

The existing 8-bit APS retry/broadcast tables are bounded, volatile duplicate
filters, not durable substitutes for 32-bit security histories. Effects can
include repeated ZCL configuration/Bind actions, stale data, misleading parent
Timeout information or availability loss. These are not limited to harmless
synthetic sensor callbacks.

Secure rejoin helps only within its actual admission:

1. Membership recovery rejects ordinary application traffic until the
   procedure admits it; it does not rekey the network or recover every lost
   incoming floor.
2. A successful response durably raises the **selected sender/response-key
   slot** floor to its counter + 1 and checkpoints other live floors. This
   excludes recordings below that new floor, not all traffic from all owners
   or the other key slot.
3. Request/response formats have no echoed fresh challenge. A captured,
   previously **unadmitted** response above the durable floor may match a later
   request context; rejoin alone does not prove contemporaneous parent liveness.
   Previously admitted responses remain protected by their durable commits
   under C; do not confuse these two cases.
4. Slot replacement by genuinely fresh key material removes old ciphertext's
   validity for that slot; switching to an existing key or changing parent does
   not. Other resident key/sender histories still matter.

Selective durability protects **previously admitted security events** through
reset, assuming the existing atomic writer succeeds. It does not make an
unseen recorded command fresh, supply a missing protocol challenge, or give
ordinary NWK data end-to-end APS authenticity.

## 8. State transitions, reset semantics and invariants

The intended state machine is:

```text
reset -> restore valid K2/K3 + outgoing ceilings
      -> initialize incoming RAM floors from durable lower bounds
      -> RECOVERING (no application READY)
      -> saved/eligible known parent, sealed secure request + durable phase
      -> authenticated response/refusal + durable checkpoint
      -> successful admission -> ED Timeout / Device Announce -> READY

READY ordinary admitted NWK -> advance RAM floor -> publish
READY security transition   -> snapshot live floors + transition -> commit -> publish
storage/auth/context failure -> no successful event/output; existing bounded failure
```

Required invariants for a later implementation:

- Four NWK histories remain separate: TC slots 0/1 and router slots 0/1.
  APS has its existing independent TC-key history; TX domains remain separate.
- RAM is at least the durable floor for the **same IEEE/key material lineage**.
  Never restore zero/lower floors on parent selection, loss, retry, runtime
  reinitialization or same-key switch. `security_keys_open()` remains cold-only.
- Loading a working record overlays live histories without resetting them.
  Every key-owner save carries both owners' current valid-slot floors.
  Counter-only reservation writes preserve durable checkpoints; they must not
  replace or clear the live caches.
- A genuinely new key replaces only that slot's history for both supported
  NWK senders. Preserve the other slot and TC APS history. Do not blindly
  `max()` an old slot's cache into a different new key after its reset-to-zero
  initialization, or overwrite a reset slot with an old cache on the next load.
- Initialize/upgrade K2 to K3 without inventing a trusted router or losing TC
  histories. Register imported router floors before copying them into the
  cache. Reject lower registration floors for the same trusted owner/keys.
- Required durable transition succeeds before publication. On a write/readback
  failure, fail closed; a physically committed record followed by API failure
  must be honored on reset, not undone through an older page.
- No transient union/workspace may own these lifetime caches. Queue refusal
  before authentication must not advance state; accepted authentication followed
  by consumer discard must never lower it or expose uncommitted security effects.
- Existing journal generation exhaustion, corrupt/degraded-page refusal,
  erase/program limits and no-older-page rollback remain unchanged.

An abrupt power cut and a normal CPU reset both lose volatile histories unless
an explicitly successful owner checkpoint preceded the reset. Merely calling
a restart orderly does not make it durable. A future optional shutdown flush
could narrow the window, but is not required for the smallest change and must
not be assumed for watchdog/power loss. Rejoin within the same running boot
preserves RAM history. Existing LEFT/factory-reset behavior is unchanged; no
factory reset or Leave may become an outgoing counter reset shortcut.

## 9. Flash reduction, RX cost and resource expectations

The [real-service audit](M5_DURABLE_REPLAY.md#measured-snapshot-cost) measured one
ordinary admitted router NWK frame:

| Cost | A baseline | C ordinary NWK-only RX, expected |
| --- | ---: | ---: |
| Erases | 1 | 0 |
| Four-byte programs | 45 | 0 |
| Programmed bytes | 180 | 0 |
| Flash read calls / bytes | 539 / 14697 (now 283 / 6505, NV selection reuse) | No flash calls required for this path; cached record access remains |
| Useful modified security data | One 4-byte incoming floor | One 4-byte RAM floor |
| Controller-only nominal storage time | 20.9 ms | 0 storage-command time |

The C column is an architectural prediction, **not a new measurement**.
The baseline 20.9 ms derives from TI's nominal 20 ms erase and 20 us word write,
not observed total RX-off time. Current storage blocks foreground service but
does not explicitly disable RF; AES, CRC, reads, CPU cost, FIFO loss and AUTOACK
behavior require separate measurement. C removes ordinary snapshot stalls,
not all receive/service latency.

If `r` is admitted ordinary router frames/second, A directly consumes
`3600*r` erases and `45*3600*r` program commands/hour before outgoing refills.
C reduces that ordinary-RX component by **100%**, not total system flash wear
by 100%. At 1 frame/5 s the avoided component is 720 erases and 32400 program
commands/hour. At 1/s it is 3600 erases/hour.

Remaining writes are selective security snapshots, all accepted APS-secured
inputs and outgoing reservations. At event rate `s`, direct selective snapshot
cost is `3600*s` erases/hour. Snapshot saves keep live TX ranges and add no
reservation replacements. An otherwise quiet profile with
720 new outbound NWK allocations/hour amortizes to `720/256 = 2.8125`
reservation replacements/hour, plus startup, reset and control costs. Actual
finite-hour totals depend on the starting range. APS security allocation is
not the 8-bit APS sequence/retry counter.

The 32-attempts/page/**boot** guard is retained. It is not physical lifetime
accounting; restarting renews the guard, not flash endurance. The TI endurance
table reviewed in the audit is not evidence of a guaranteed 20k-cycle minimum.
No years-of-service guarantee follows from these estimates. Frequent
APS-secured/control traffic can still exhaust storage under C.

The additional persistent-lifetime RAM estimate is **8 XDATA bytes** for router
slots, conditional on `CC2530_PARENT_RECOVERY`. There is no proposed log parser
or new page buffer. Target CODE/static-frame/stack effects have not been built
or measured. The qualified reference remains CODE 222717, common 18944, raw
XDATA 5212 and stack 106 static / 101 measured; the review itself changes none
of these resources. Do not infer a compiled 5220-byte admission from the cache
estimate, or borrow alias/reserved flash capacity.

## 10. Negative tests, smallest change and append-log decision

Before approving implementation, accept the explicit risk in section 7.
Then the smallest independent firmware change would be:

1. Add two long-lived router floor cache values; extend cold seeding,
   K2/K3 upgrade/registration, load overlay and save/lazy cache synchronization.
2. Remove only the unconditional router save condition for eligible ordinary
   traffic in `security_keys_receive()`. Keep APS protection, security-event and
   key-adoption save conditions and existing control/context behavior.
3. Preserve every parent/key/Leave/rejoin save point; prove genuine key
   replacement refreshes only the correct cache entries. No changes to
   `security_counter` reservation/burn semantics, `nv_record`, flash executor,
   partition, guards or LLVM are necessary for this first slice.
4. Update public contracts, test expectations, scenario/profile checkers and
   evidence claims to distinguish volatile NWK admission from durable events.
   In particular, existing comments saying every packet requires a durable
   save must not survive as false API promises.

K3 remains version 3 and length 144; its two router floors become **durable
checkpoint lower bounds**, not exact last-admitted counters. K2 TC semantics
already have this property. No format migration, append record, new writer,
page partition or retired-key removal is proposed. Old qualified firmware can
read the same K3 bytes and resume strict saving; it cannot reconstruct ordinary
history that policy C never persisted. Format compatibility is not security
equivalence.

Required future deterministic tests:

| Area | Required negative/result proof |
| --- | --- |
| Ordinary receive/reset | No flash commands on eligible router RX; duplicate rejected within uptime. After reset, never admit below the durable floor; deliberately demonstrate permitted replay **above** it and multiple increasing recorded frames, rather than labelling it equivalent strict security. |
| Checkpoint union | TC/router interleaving, both slots and APS: a subsequent security-owner save captures all live unchanged-key floors. Outgoing counter-only refill cannot lower RAM, falsely claim it checkpointed RAM, or mix owners. |
| Parent lifecycle | Same/alternate/silent parent, ATTEMPT versus ACTIVE, live parent switch and recovery retries retain floors; cold reset stays gated from application. Wrong IEEE/EPAN/key/update/response source rejected; exhaustion/refusal bounded. |
| Rejoin freshness | Accepted response replay rejected after reset; previously unadmitted matching response above checkpoint is not assumed fresh. A successful response raises only the correct sender/slot; other-slot/sender residual exposure is tested. |
| Key rotation | Fresh slot initializes exactly its own sender histories; old slot survives switch/auto-adoption; duplicate transport/switch does not reset; sequence wrap/conflict/reused material rejected; reset during staging/promotion. |
| APS/TC security | Derived-key traffic shares one TC APS floor; bad APS MIC produces no event/output; every accepted APS-secured input/Transport/Confirm remains durable through reset; pending/retired key never becomes a generic fallback. Include the existing Confirm-counter conformance difference explicitly. |
| Boundary/context | Wire `FFFFFFFF`, admitted `FFFFFFFE`, exhausted stored floor, wrong nonce sender, bad NWK MIC, malformed/unsupported upper frame, late/duplicate Timeout response, repeated NWK-only configuration. Keep the current staged-floor layering difference separate from new-policy regressions. |
| Real flash cuts | Existing writer, reset before/after every required checkpoint program/commit, torn page, full guard, generation exhaustion and post-physical-commit/pre-success fault. Recover new committed state or fail closed; never select an older security state as success. |
| Migration / reservations | K2 to K3, imported floors, absent slots, old/new firmware reading same format; outgoing NWK/APS near exhaustion and reset reservations remain monotonic independent of incoming RAM. |
| Queue / publication | RX/ACK/backpressure and APS retry duplicates: no early output, no lowering after consumer discard; control events cannot be silently lost. Parent Timeout/Announce and READY restoration still execute after recovery. |
| Sustained service | Actual protected router NPDU traffic plus reports, Timeout, bursts/idle and restart/replay attempts. Predefine workload/duration/rate/cost thresholds and distinguish volatile residual replay from failure. Real linked MCU execution, separate terminal/stack checks; ten-second case 18 is not sustained evidence. |

Use focused host/fault tests and LLVM FAST during that later implementation,
SMOKE after integration, then **one fresh exact-final-candidate FULL** including
all admitted commissioning/recovery cases and any explicitly added sustained
scenario. Preserve old evidence and do not reuse its receipt for changed
runtime semantics. This documentation review runs no firmware acceptance tier.

**Append-only storage is not presently justified merely as a Zigbee
per-frame-persistence obligation.** Its host reference proof is useful and
remains preserved, but integration is deferred pending this policy decision.
It remains justified if the product rejects the reset replay window, needs
strict durable ordinary application actions, or needs sustained APS/security
traffic beyond the unchanged snapshot budget. It would still need real MCU
format/recovery proof, measured service timing and an honest endurance model;
the host log proof alone is not production readiness.

The next bounded M5 slice is the approved cache/checkpoint policy and its
reset/real-traffic proof, **not** a journal rewrite or compiler optimization.
Existing protocol differences in section 4 need separate conformance work.
No firmware, compiler, HWLab, hardware installation, frozen image or production
branch was modified for this review. Implementation stops pending approval.
