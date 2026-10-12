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
