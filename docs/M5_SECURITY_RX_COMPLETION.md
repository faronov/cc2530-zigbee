# M5 security receive completion

Development branch: `m5-security-rx-completion`, from the unchanged
FULL-qualified experimental Policy C candidate
`4e4e941aa316b5bd84af4176a3bd8dfd49a890ce`. Compiler pin:
`1a01afb49d99ec754e62ab0f65f182de87b4cee3`.

## Baseline regressions

`test_security_receive_completion.c` uses the real key, counter, journal,
flash and AES services, plus existing public synthetic commissioning/router
fixtures. Before any production-source change, the three independently
selected reproductions failed against the accepted source:

| Case | Observed baseline defect | Required completion |
| --- | --- | --- |
| `nwk` | Repeating an authenticated unsupported command at counter400 returns UNSUPPORTED again | REPLAY during uptime; ordinary Policy C floor remains RAM-only |
| `aps` | Authenticated current-key unsupported APS command at counter400 leaves durable AF22 | Durable AF401, rejecting the captured APDU under fresh NWK envelopes |
| `adopt` | Valid newer-slot NWK frame with unsupported semantics leaves active sequence0 | Durable active sequence1 and consumed sender/slot floor |

Every negative receive checks that packet and event outputs remain unchanged.
These are host composition reproductions, not hardware observations.

Core R22 4.3.1.2 steps6-7, printed pp.413-414, completes the authenticated
sender/slot NWK count and adopts an already installed newer entry regardless
of unrelated upper semantics. Core 4.4.1.2 step8, pp.419-420, completes the
operational APS count after successful authentication.

The existing pending B key is **only a confirmation proof**, not operational
APS material. Its sole permitted use is a valid outstanding Confirm, whose
promotion and consumed count are committed together. Unsupported pending-key
commands cannot promote it or gain operational admission. Retired material
is not a decryption fallback. This task preserves that documented restriction
and the existing Confirm counter+1 rule; it does not add another APS domain.

Broadcast key updates, Confirm-Key initialization and Core/BDB protection
ambiguity remain separate workstreams. No command acceptance expansion,
journal/schema change, hardware operation or compiler change is authorized
by this development slice.

## Completion transaction

The implementation uses existing private `secured`, `aps_secured`, `pending`
and `event` bytes; no new lifetime cache, workspace field or NV journal.

| Boundary | Security-state outcome | Delivery |
| --- | --- | --- |
| NWK identity/sequence/counter/MIC failure | No NWK floor or adoption | None |
| Valid NWK authentication and replay admission | Sender/slot floor completed, even if APS later fails | Not yet permitted |
| Valid operational current-key APS authentication | AF advances and checkpoints with live NWK floors | Not yet permitted |
| Pending B authentication trial | No operational AF, key promotion or membership change | Valid outstanding Confirm only |
| Valid installed newer-slot NWK authentication | Active slot, consumed floor and both histories checkpoint together | Independent of unrelated upper semantics |
| Upper semantic rejection | Original negative result retained after required completion; packet/event untouched | None |
| Required checkpoint fails or is ambiguous | STORAGE overrides semantic error; owner fails closed | None |
| All security and semantic checks succeed | Existing RAM/durable policy completes first | Safe normalized output only; transport/ZDO still apply their own admission |

Bad APS MIC does not advance AF, but cannot undo its already authenticated
outer NWK floor or authorized NWK adoption. Likewise an APS replay rewrapped
under a fresh valid NWK counter still consumes only that newly admitted outer
counter. NWK replay/failed NWK MIC never authorizes a transition.

All rejecting guards in `transport`, `aps_command` and `nwk_command` precede
semantic mutation. The rejected tail may therefore preserve authenticated
floors/adoption, never partial unsupported command side effects. This
failure-atomic validator invariant must remain true in later command work.
`nwk_aps_receive` discards/zeros rejected input and does not set receive_ready;
`bdb_join_receive` cannot pass it to ZDO/application consumers.

Core R22 4.3.1.2 step7 authorizes adoption of an **already installed newer**
entry after its NWK authentication, not installation of unknown material,
acceptance of a failed MIC or promotion of an APS pending key. The existing
installed-slot/sequence/identity checks remain before this boundary.

## Persistence and compatibility

Ordinary authenticated rejected NWK input, like ordinary accepted input, is
RAM-only under Policy C. There is no added cadence. After an unexpected reset,
the ordinary above-checkpoint suffix can be replayed again; its duration and
volume remain uncapped. Secure rejoin is not a freshness proof. Current-key
APS completion and authorized adoption are durable even on upper rejection.
All existing transition saves include live TC/router floors for both slots.
Outgoing reservations, exclusive ceilings, exhaustion and counter-only saves
are untouched.

K2/K3 versions, offsets, payload lengths112/144 and two-page journal stay
unchanged. A coupled initial-commissioning invariant needed correction:
authenticated bootstrap APS can now consume AF before Network Transport-Key
is semantically accepted. The old pre-network validator required
PK+NF+AF to be zero, making that newly committed state unreadable. It now
requires PK+NF zero while permitting the existing AF field. No network key,
trusted membership or semantic acceptance is thereby granted.

This new reader accepts existing accepted/migration images. An older frozen
reader will fail closed on a newly saved pre-network nonzero-AF record; do not
claim downgrade-read compatibility for that newly reachable state. Operational
K3 checkpoints retain the existing representation. A rejected initial
transport, duplicate, cold reopen and subsequent fresh legitimate transport
are explicitly exercised.

Authenticating unsupported current-key APS traffic now incurs its required
checkpoint cost: one erase plus37 K2 or45 K3 programs. This is not an
ordinary-NWK write path. Repeated fresh authenticated APS errors can consume
the inherited32-erases/page/power-epoch budget and fail closed; no rate-limit,
wear-budget override or simulator exemption hides that boundary.

## Regressions and faults

Focused tests cover unsupported NWK/APS commands, malformed APS syntax and
authenticated malformed Transport-Key semantics, fresh NWK wrappers around
old APDUs, duplicates, bad increasing NWK/APS MIC, wrong sender, unknown
sequence, both slots, TC/router separation and exhaustion. Caller tests use
genuine commissioned READY state for wrong endpoint/profile, checking both
replay completion and no transport/ZDO/application publication.

Pending tests distinguish operational A, pending B trials and retired A:
unsupported B commands cannot promote B; B application traffic is not
operationally decryptable; A's authenticated rejected command checkpoints;
valid outstanding Confirm under B still promotes exactly as before.
Confirm initialization remains `received+1`, not the separate R22 zero-count
conformance slice.

Each rejected operational APS checkpoint and rejected newer-slot checkpoint
is interrupted at1582 points: before/after each of46 K3 Flash commands,
all33 bit-completion boundaries of each four-byte program, and five erase
landmarks. Each sweep has1574 explicit fail-closed recoveries and two fully
committed checkpoints. Remaining readable states are the exact old/new
record, never a partial semantic transition. Caller outputs remain unchanged.
Precommit read failure, physical commit followed by ambiguous API readback,
and busy Flash fail-stop are separate negative tests.

The existing Policy C corpus retains10000 protected ordinary router frames
plus10000 duplicates with zero Flash reads/erases/programs,1582 original
checkpoint cuts,138 migration cuts and690 router-transition cuts. BDB tests
retain same-parent/known-router recovery,456 recovery cuts, explicit Leave
and counter exhaustion.

Native and nonrecovering ASan/UBSan runs both pass: focused completion44130
checks, complete Policy C138451 checks, and BDB148248 checks. The new
case20/21/22 reference transcripts also match between native and sanitizer
executions. These are deterministic modeled-service results, not hardware
observations.

## Linked-MCU and resource gates

New case22 adds bounded genuine radio/AES/counter/journal service coverage,
not a fabricated security result. It recovers through the known router,
injects an authenticated unsupported NWK frame, an authenticated unsupported
current-key APS command, legitimate next network-key transport, and an
unsupported frame under the installed newer entry. Three duplicate attempts
must drop. Ordinary rejection costs zero Flash commands; APS rejection,
legitimate transport and rejected adoption each cost one K3 checkpoint.
READY and genuine reporting continue.

The MCU replay compares actual RAM replay-floor symbols and complete physical
journal bytes with the real host reference at every polling boundary. This
specifically exercises the source-owned shared-workspace loan across the new
rejected completion tail, beyond the nonworkspace focused host proof.
The original16 FULL cases and case21 service thresholds remain admitted;
case22 is an additional FULL gate, not a replacement.

Development exact-pin FAST passes10/10 gates. Linked resources against the
accepted Policy C image: CODE222779 ->222820 (+41); common18944 unchanged;
BSS4959/data44/static-frame pool217, raw XDATA5220 unchanged. Static stack
remains106/128. All377 bank thunks are correct. Bank free bytes1..7:
`1,0,0,0,0,5233,14122` (aggregate19356); several banks remain full.
No LTO or compiler change.

At this development commit, integrated frozen-SHA FAST/SMOKE, case22 terminal
acceptance, new FULL verdict/runtime stack/image hashes remain outstanding.
Development FAST is not FULL qualification or hardware evidence. Preserve
accepted `4e4e941` and its137-file receipt/evidence manifest independently.

## Remaining interpretation and next slice

Broadcast NWK-only key transport is still unsupported. Confirm-Key zero-count
initialization is still a separate deviation. Core4.4.1.3 tables4-6/4-7
(pp.421-422), Core4.4.1.5 (p.423) and BDB10.2.3 (p.75) protection-rule
interpretation, including applicable errata21-65431/test plan16-02826,
remain unresolved. No authentication requirement is relaxed here.

The next small conformance slice should resolve/test Confirm-Key initialization
from Core4.4.8.2.3 step9 (pp.445-446) and BDB10.2.5 step9 (p.78), separately
from broadcast support and command-protection expansion. This candidate
remains an experimental bounded sensor-only ED; simulator acceptance cannot
establish generic Zigbee conformance, production readiness or hardware safety.
