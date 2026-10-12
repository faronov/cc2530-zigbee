# M5 Policy C: experimental known-router profile

Approved on 2026-10-12 for the receiver-on experimental profile, following the
[architecture review](M5_REPLAY_PERSISTENCE_DECISION.md). The strict
`parent-recovery` variant and qualified `e4c41c8` remain comparison references.
`parent-recovery-policy-c` defines `CC2530_PARENT_RECOVERY` and
`CC2530_REPLAY_POLICY_C`; ordinary production and strict variants do not.

## Checkpoint-trigger matrix

This matrix is the implementation contract, not a generic Zigbee mandate.
All key-owner saves carry live TC and router floors for both unchanged key
identities. Counter-only reservations retain the last durable floors and never
replace the RAM histories. K3 version 3, 144-byte payload, the two-page journal
and all outgoing NWK/APS reservations/ceilings are unchanged.

| Operation | Checkpoint / justification |
| --- | --- |
| Ordinary NWK-only DATA / APS ACK | No direct flash: authenticated admitted floor advances in RAM |
| Network Status / contextual ED Timeout Response | No direct flash; existing volatile management/context policy |
| Any accepted APS-secured input | Yes: unchanged APS replay protection, with live NWK floors |
| Network/TC Transport-Key, pending-key/Confirm transition | Yes: atomic key identity, history and consumed floors |
| Switch-Key, including idempotent fresh-counter switch | Yes, even NWK-only: active slot and replay state |
| Automatic newer-key-slot adoption | Yes: active slot and consumed floor |
| Parent schema upgrade / trusted registration / selection | Existing save points; no new identity or lower live floor permitted |
| Secure Rejoin Request phase / admitted or refused Response | Yes: phase/parent/admission and consumed history; refusal is not silence |
| Network Update, including idempotent fresh-counter update | Yes: durable PAN/update state and replay protection |
| Explicit/received Leave | Yes: LEFT/rejoin intent and history; no key/counter erasure |
| Outbound key request / verify phase | Existing durable phase before output |
| Outgoing reservation refill | Unchanged counter-owner save; not an incoming checkpoint cadence |
| Unsupported security command / factory-new operation | Still unsupported; no permissive reset path added |

A genuinely replaced key initializes only its own TC/router slot histories;
switching existing slots, changing parents or retrying cannot clear histories.
`load`, cold `open`, schema upgrade/registration and every successful save
synchronize both lifetime caches. They are not shared transient-workspace data.

## Explicit residual risk

For unchanged sender/key, durable floor D and pre-reset RAM floor L, previously
accepted ordinary frames with D <= counter < L may be accepted again after
unexpected reset when context permits. A recorded increasing suffix, not just
one frame, can replay. The interval has no configured time or volume cap:
duration can be arbitrarily long between security checkpoints, and volume is
bounded only by the finite 32-bit counter/key lifetime, not a small replay
window. Resets can repeat exposure. High recorded counters can suppress later
lower legitimate traffic. Secure rejoin has no fresh echoed challenge and does
not refresh all sender/key histories.

Accepted security-sensitive commands and APS-secured inputs remain durably
admitted before effects/publication. Plain APS DATA may carry ZCL writes,
Bind/Unbind and reporting configuration: those effects are inside the explicitly
accepted ordinary-NWK reset exposure, not cryptographic TC authority. Use
stronger application freshness for actuator/security-sensitive products.
This policy is suitable only when the experimental sensor profile accepts
these risks; it is not generic Zigbee security conformance or hardware readiness.

No arbitrary periodic checkpoint is introduced. A cadence would require an
approved maximum replay window, actual workload and wear budget; none has been
specified. Remaining APS/control writes and outgoing reservations still use
the unchanged erase guard and finite physical endurance.

## Independent conformance findings

Broadcast NWK-only key updates remain unsupported; Confirm-Key retains the
existing counter + 1 policy; NWK admission still waits for supported upper
semantics; Core/BDB command-protection text remains unresolved. Policy C changes
none of those receive/protection checks or command save points. It does not
make accepted security commands volatile, reenable retired material, or add
an insecure commissioning fallback. Their independent fixes require separate
reviewable changes, not simulator exceptions.

## Focused development evidence

Native and nonrecovering Address/UndefinedBehaviorSanitizer executions use
the production key, counter, journal, flash writer/reader and AES services with
synthetic peripherals, not a successful snapshot substitute:

- 10,000 authenticated ordinary router frames and 10,000 duplicate attempts:
  zero flash reads, erase attempts or program commands.
- TC/router separation, both key slots, new-slot replacement/adoption,
  APS durability, same/alternate parent admission, explicit Leave and
  `FFFFFFFE` exhaustion retain their distinct histories.
- A Network Update checkpoints all live floors; outgoing-only reservation
  refill retains the old durable floor without clearing RAM.
- 1,582 command/reset cuts: 1,574 explicit fail-closed outcomes, two committed
  checkpoints rejecting replay, and six old-snapshot recoveries with the
  permitted ordinary suffix exposure. A physically committed checkpoint
  followed by failed API readback also rejects replay after cold restart.
- The existing corpus still covers K2-to-K3 migration, retired material and
  lifecycle transitions. Policy-C BDB recovery passes 146,810 checks in each
  mode, including 456 recovery cuts and counter exhaustion.

These are host composition results, not exhaustive MCU power-cut execution or
physical flash endurance qualification.

| Ordinary router RX cost | Strict reference | Policy C |
| --- | ---: | ---: |
| Erase attempts | 1 | 0 |
| Four-byte program commands | 45 | 0 |
| Programmed bytes | 180 | 0 |
| Flash read calls / bytes | 539 / 14,697 | 0 / 0 |
| Modeled controller busy time | 20,900 us | 0 |

The 20 ms erase / 20 us word-program values are synthetic peripheral timings,
not measured radio blackout or manufacturer endurance. A justified checkpoint
still costs the unchanged full replacement and can abandon remaining outgoing
reservation ranges. APS-secured/control traffic can still exhaust the guard.

## Sustained scenario and acceptance boundary

Case21 cold-boots from only the genuine case20 DUT flash and peer state.
It then serves for 600 modeled seconds, admits 120 unique protected
NWK-only router frames (one per five seconds), and retries 12 captured frames
during uptime. Every frame traverses real radio/adapter/BDB/security services;
each duplicate must produce REPLAY without application publication. READY,
transport ownership, zero RX backpressure and ED Timeout progression are
required throughout, with at least 16 peer-decoded/verified application reports.

The defined limits are at most eight service erase attempts, exactly 45
programs per erase, and zero flash commands within every ordinary/replayed RX
drain. Native and sanitized transcripts agree byte-for-byte: 600,084 ms,
120 accepted protected frames, 12 replay drops, 23 reports, zero service
erase/program commands and zero security checkpoints. The measured host
checkpoint count is output-only, never injected into DUT memory.

Consequently no flash-caused RX servicing stall occurs in this particular
service interval: there are no flash operations. This does not measure RF
blackout on hardware, guarantee indefinite zero writes, or extrapolate physical
lifetime. At the specified rate the strict policy would demand 720 ordinary-RX
erase attempts per hour before its per-boot guard stops service; Policy C's
observed service interval has zero. Startup, security checkpoints and outgoing
reservation replenishment remain outside a zero-write guarantee.

The linked-MCU replay must independently check every peripheral operation,
status/NV result and both actual lifetime RAM caches, then reach the serving
terminal and validate service metrics. A prefix, suspension or host transcript
alone does not qualify sustained MCU operation. FULL adds case21 without
removing cases0/16/17/18/19/20 or other original cases.

With exact compiler `1a01afb49d99ec754e62ab0f65f182de87b4cee3`, development
admission records CODE222,779 (+62), common18,944 (+0), raw XDATA5,220 (+8),
static stack106/128 (+0), 377 bank thunks and zero incorrect thunks.
The strict comparison reproduces qualified ELF
`3861ffd04465b6ef72f5c7596f2946ee484be93ae7afeee720078be9a760b3ea`
and HEX `c3e356c86d2a14822c1ab21a8d822d409358a0c683def0901e57a64526b7f7e5`.
Runtime sustained stack and final FULL acceptance remain pending until that
exact frozen candidate completes; the accepted strict FULL evidence is unchanged.
