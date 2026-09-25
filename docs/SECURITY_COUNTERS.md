# Durable outgoing counter ranges

`security_counter` is the bounded #21 owner of the actual two-page
[`nv_record` journal](NV_RECORDS.md), reader, writer and RAM command executor.
It commits an exclusive ceiling before returning any counter in the new range.
It is not linked into board firmware and does not implement key validation,
replay reception, commissioning or physical flash acceptance.

## Allocation and restart

The [API](../include/security_counter.h) has two conservative domains:
one NWK/network-key domain across network keys and networks, and one APS
link-key domain across all link keys and their derived transport/load keys.
The selected key family, not merely the sending layer, determines the domain.
Any future APS/MAC use of the network key must share the NWK domain.
Sharing the APS domain across distinct link keys is a project policy that
avoids separate per-peer range state; it is not a new normative requirement.

The persistent `until` value is an **exclusive reserved ceiling**. The
volatile `next` value tracks allocation within that range, not successful RF
transmission. `security_counter_take()` increments it before publishing the
returned value. A counter is consumed even if encryption, queuing or sending
later fails. There is no refund or rewind operation.

At an empty range, the service commits up to256 more counters through the
real journal. No counter from that range is published until the journal
returns fully verified success. FFFFFFFF is an exhausted ceiling, never a
returned counter. The final range is shortened without wrapping.
On opening after a genuine reset, `next` starts at the saved `until`: every
possibly allocated pre-reset counter is skipped, including unused reservations.
Neither generation nor a count of completed transmissions is used to guess
how many counters were actually consumed.

## Record and opaque state

This record is the journal's payload. All integers are little-endian bytes;
the public C struct is not a storage format.

| Offset | Field |
| --- | --- |
|0..3|Family magic `CTR1`|
|4|Schema version1|
|5|Opaque payload length0..112|
|6..7|Reserved zero|
|8..11|NWK exclusive ceiling|
|12..15|APS exclusive ceiling|
|16 onward|Exactly the declared opaque payload|

The journal stores exactly16+payload_length bytes, with its unchanged
generation, whole-page CRC and commit-last semantics. Schema magic/version,
reserved fields and exact length are checked before accepting state.
An unknown counter schema fails VERSION; an unknown journal format remains
an explicit NV failure with the underlying UNSUPPORTED reason. No implicit
migration, truncation or default initialization occurs.

Opaque payload is for a future upper-layer security snapshot, not an
authenticated key or membership record by itself. `security_counter_read()`
copies it to caller storage; no private pointer/lease escapes.
`security_counter_save()` replaces it atomically while preserving both
ceilings and abandoning both remaining volatile ranges. Saving a zero-length
payload is the counter-preserving foundation for leave/factory-reset state
clearing, **not an implementation of those network procedures**.
The older physical record may retain old payload bytes until later erase;
this is not secure key erasure.

## Failure and recovery policy

`security_counter_open()` returning EMPTY is **not evidence of a virgin
device**. `security_counter_create()` is an explicit provisioning operation,
permitted only after EMPTY. The caller must independently establish unused
identity/key history or trusted nondecreasing starting floors. Never call it
automatically after missing state, a leave or factory reset. Those procedures
must retain the counter record and use the counter-preserving save path.

Before an admitted replacement, the service reloads and compares the complete
current snapshot and generation to its runtime copy. Unexpected generation
or bytes fail ROLLBACK. Generic-journal RECOVERED selection always fails
RECOVERY: **an older valid record must not authorize counter rollback**.
The counter layer never sets `allow_recovery=1`.

During destructive work, counter state/result and the underlying NV result
are explicitly BUSY/PENDING. A storage error may follow a physical commit;
it is never interpreted as proof that nothing changed. All storage,
degraded-selection, schema and rollback failures retain a failure and stop
both domains. A later take/save/read cannot clear it or make further MMIO
accesses. Invalid arguments/ownership preserve diagnostics and caller data.
Every failing take/read preserves caller output. Range exhaustion remains
an explicit per-domain result; it does not reset the other domain.

A separate, genuine full reset may recover a transient hardware-service
fault **only if** a fresh scan accepts a complete clean journal. It then
starts at the persisted ceilings, not the old volatile cursor.
Persistent degraded/corrupt/unsupported state remains blocked.
External recovery requires trustworthy history/floors or retirement of
potentially reused identity/key material; there is no software recovery
bypass that guesses zero or silently selects an older record.

**A coherent cold rollback of both pages, or complete out-of-band erasure,
cannot be detected by these two pages alone.** Their CRC is not authentication,
and a journal generation is not an independent monotonic anchor. This
implementation supplies no such hardware anchor. Physical tampering, arbitrary
coherent rollback and electrical durability are not established by synthetic
interruption tests. An eventual migration/recovery mechanism must explicitly
retain both counter ceilings and atomic publication, not weaken this boundary.

## Ownership and wear

There is one exclusive NV owner: no parallel/direct journal writer may run.
Link flash executor, reader, writer, journal, counter owner, then caller.
All caller objects must be complete, disjoint stable ordinary XDATA beyond
the entire private/compiler prefix and below1E00, excluding libc scratch,
status/MMIO and IRAM aliases. Calls are serialized foreground-only and inherit
the real flash clock, mapping, IRQ/DMA and genuine-reset/history constraints.
There are no callbacks, heap, automatic retry or successful persistence mocks.

Journal generations never wrap. The underlying32 erase-attempts/page runtime
limit remains unchanged; provisioning and payload changes consume it too.
At most64 replacements are therefore admitted in an epoch, and fewer ranges
are available when other snapshots are saved. Exhaustion fails closed;
resetting volatile accounting does not establish renewed lifetime endurance.
No lifetime-wear estimate, production transmission rate or physical erase/
program acceptance is inferred from the reservation size.

## Offline evidence

```sh
make BOARD=generic BUILD=build/generic/counters test-security-counter
make BOARD=lg_esl29_rev03 BUILD=build/lg_esl29_rev03/counters test-security-counter
```

The native/nonrecovering sanitizer corpus has82372 checks using the actual
counter, journal and flash C, with the existing synthetic controller and
independent record CRC oracle. It covers every opaque length, every byte of
both populated pages as corruption, all invalid caller addresses, exhaustion,
retained faults, changed runtime snapshots, restart/range skipping and
counter-preserving state clearing. All38 full-length erase/program boundaries
are interrupted before/after completion, with all33 prefix-bit tears per
program word and representative partial erases. Provisioning and clearing
also have every command-boundary cut; four active RAM-fail-stop phases remain
PENDING without publishing a counter.

The genuine target links all six objects with immediate relocated-listing
snapshots. The three lower flash modules retain their complete published
instruction identities. The journal is the real production C, with its
relocated constant addresses checked for this composition. Actual NV calls,
RAM commands, FCTL/FADDR/FWDATA instructions and every programmed byte are
checked by the shared journal replay, not substituted API results.
The56 sequences execute36695 traced flash calls and919 actual RAM commands
across162 complete-state segments.

| Resource | Measured / limit |
| --- | --- |
|Complete isolated CODE|9907 /12288 bytes|
|Ordinary XDATA plus status reservation|1125+64 /1280 bytes|
|Counter module CODE / XDATA|2821 /333 bytes|
|Stack allocation / checkpoint SP|42 /43 hex|
|Maximum full-run stack / cap|66 /7C hex|

The journal/private prefix ends at0295; the counter/private compiler fence
is03E2; caller storage is03E3..0464. There is no linked generic-pointer/libc
scratch in this composition. Complete CODE, raw CDB before decoding, map,
memory, objects, listing order/metrics, fields, parameter storage, fences and
real call chains are pinned; every CODE byte and every F/S/L/T record has
negative controls. Caller publication, upper IRAM, status tails, unallocated
XDATA, NV neighbors and unowned peripherals remain guarded.
There are32257 artifact negatives,204 snapshot/continuation negatives and
one missing-IRAM-alias negative; exact sequence/call/segment/peak totals
are required, not merely printed.
The complete-join stack reduction uses volatile domain/end/poll/page copies
to shorten compiler register-save lifetimes, without changing counter floors,
reservation frequency, journal format or flash history. The refreshed
standalone execution passes locally; its larger
[resident composition](SECURITY_RESIDENT.md#execution-and-resource-contract)
still needs combined deadline acceptance.

Longer multi-operation sequences use complete-state continuations at genuine
operation boundaries, following the existing offline proof pattern. Each
simulator still has the same15-second deadline. Continuations preserve and
compare every CPU/SFR, IRAM, nonaliased XDATA, backing-flash and XMAP-ROM byte
before resuming the exact PC; they never reset C state or import guessed
flash/counter history. Only explicitly modeled resets execute real startup.
The original61 journal sequences and their17149 calls/829 RAM commands remain
unchanged and pass through the shared replay's original path.

This is **host-tested, image-checked and simulated** evidence, not physical
power-cut/endurance, cold anti-rollback, key secrecy, a board image or
authenticated membership. Both-board Actions jobs are the full acceptance
gate and upload no counter/NV artifacts.
See [primary facts and provenance](PROVENANCE.md#outgoing-counter-reservation-sources).
