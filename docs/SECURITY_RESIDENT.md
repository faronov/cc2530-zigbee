# Resident crypto/NV memory profile

This is a bounded #23 prerequisite, **not key lifecycle or authenticated
join**. Four offline images each contain all thirteen unchanged production
modules: flash executor, reader, writer, journal, outgoing counters,
timebase, AES, CCM, NWK/APS codecs, security envelopes, MMO and keyed hash.
Each has one unchanged original service caller. There are no substitute
crypto, persistence or successful authentication implementations.

The services retain their host/sanitizer evidence. This separate composition
is image-checked and simulated, not hardware-observed or linked into a board
image. It does not establish mixed crypto/NV transactions, key admission,
restart-safe replay state, TC confirmation, ISR headroom or whole-stack fit.

## Physical DATA ownership

The default large-model composition exceeds available DATA/OSEG even though
CODE and XDATA fit. Changing to stack-auto is not an accepted workaround:
the exploratory genuine HMAC path exceeded the unchanged SP7C cap.

The checked profile keeps the large-model ABI and uses SDCC's `--dataseg`
to name complete module DATA frames. After replacing just that area name
with `DSEG`, **every production and caller relocatable object must match its
already accepted original hash**. This covers instructions, relocations,
private/public metadata, argument representation and all storage classes.
Only the assembler output-directory header is excluded as before.

Named areas alone do **not** reserve physical IRAM from SDCC's special
DATA/OSEG/stack allocator. Two real, separately compiled DATA arrays reserve
24 bytes at08 and37 bytes at22. They are reservations, not application
buffers or additional RAM. The bit-addressable bytes20..21 remain separate.

| Module frame | Start, hex | Bytes |
| --- | ---: | ---: |
| flash executor |08|8|
| flash reader |10|4|
| flash writer |14|5|
| journal |22|23|
| outgoing counter owner |39|14|
| timebase / CCM |08|0 each|
| AES |22|32|
| NWK / APS codecs |22|12 /8|
| security envelopes |08|9|
| MMO |08|18|
| keyed hash |42|3|
| original caller |1A|at most6|

The actual reservations are08..1F and22..46; OSEG is47..50, stack allocation
starts51 and initial SP is50. No named frame reaches the physical bits,
OSEG or stack. XDATA1F00..1FFF still aliases IRAM, never extra memory.
Ordinary XDATA remains below1E00 and the64-byte status reservation.

Complete CDB records must show only compiler `sloc` temporaries in reused
DATA; the only file/global DATA objects are the physical reservations.
Every temporary must fit its module frame. Retained state stays in
nonoverlapping XDATA or separately allocated bits. Function attributes,
raw CDB identity before decoding, actual map and memory report are checked.

## Active calls, not just a successful link

The verifier reconstructs transfers from every emitted source instruction:
long and absolute calls/jumps, conditional/relative branches and tail
transfers. It decodes the complete linked libc CODE region, excludes
callbacks into application modules and rejects direct/bit accesses to the
reused DATA pool. Complete linked runtime bytes and scratch allocations are
pinned, including division/modulo and generic-pointer helpers.

The only indirect transfers are the existing fixed flash RAM executor and,
in the counter caller, its checked four-entry switch with a bounded0..3
index. Their genuine byte sequences and targets are checked. Unknown
indirect transfers and RETI fail; reset's fixed startup jump is separate.
Transitive inter-module paths must be acyclic and have disjoint active DATA
frames. Tail transfers are conservatively treated as retaining ancestors.
Same-module allocation and calling conventions remain byte-for-byte those
of the original accepted compiler objects.

For example, counter -> journal -> writer -> executor has disjoint frames,
as does keyed hash -> MMO -> AES -> timebase. The counter and keyed-hash
frames can overlap only because neither occurs on the other's active path.
Likewise, NWK/APS header processing returns before the envelope calls CCM
and AES. Added unsafe call edges, recursion and overlapping active frames
are independently rejected, not merely caught by an artifact hash.

This is a **serialized foreground-only profile for these four compositions**.
It is not permission to add callbacks, interrupts, DMA-backed DATA, retained
local addresses or arbitrary callers. New modules/callers require a fresh
complete ownership, resource and emitted-call proof. There is no ABI flag
change for existing components or board images.

## Execution and resource contract

```sh
make BOARD=generic test-security-resident
make BOARD=lg_esl29_rev03 test-security-resident
```

The existing native reference traces drive actual linked AES/DMA execution;
hashes, MIC results and function returns are never injected. Journal replay
uses the real reader/writer/RAM commands and complete CPU/RAM/flash/XMAP
continuations. Only peripherals/media are explicitly synthetic.

| Resident caller | Complete CODE | Ordinary XDATA | Maximum SP, hex | Original corpus |
| --- | ---: | ---: | ---: | --- |
| Security envelopes |31786|3101|78|23 cases,220 checks,345 AES calls|
| MMO/install code |29421|2367|6C|39 cases,117 checks,58 AES calls|
| Keyed hash |29775|2370|76|15 cases,45 checks,51 AES calls|
| Outgoing counters |28231|2425|79|56 sequences,36695 flash calls,919 RAM commands,162 continuations|

The new profile caps are32768 CODE and3200 total XDATA including64 reserved
status bytes; executed SP remains at or below the original7C cap. These
are new full-resident image budgets, **not relaxed original component caps**.
The largest image leaves982 CODE bytes below8000; this is not demonstrated
space for the remaining stack. Every existing standalone image identity,
case, diagnostic, negative control and15-second simulator deadline remains.

Each image consumes sixteen listings captured immediately after its own
link, before a subsequent link can overwrite relocated listings. All source
storage extents, complete libc boundaries and artifact identities are pinned.
Artifact-negative counts are99456/93116/92761/89734 respectively. The
existing snapshot/continuation negatives remain; exact shifted peak checks
are added for every crypto case. Every call also checks inactive services'
XDATA, with1194/1941/1864/1305 individual inactive-byte mutations.
Missing IRAM aliasing is independently rejected.

Eight additional CI jobs, one per board/caller, run all four resident corpora.
The exact partition is `test-security-resident-{security,mmo,key-hash,counter}`;
splitting the heavier artifact proofs preserves the15-minute job limit.
There are no artifact uploads, hardware connections, automatic flashing or
RF actions. `test-common` remains the exact union including the unchanged
original service suites.

## What still must be implemented

This profile does not persist keys or accept a coordinator. #23 still needs
two network-key slots, old/pending TC link-key states, bounded persistent
replay rejection and authenticated Transport/Verify/Confirm processing.
Actual interleaving, interruptions and restart must be tested with those
owners present. Upper staging must remain outside every lower module and
complete libc scratch region; earlier NV-owned key buffers cannot simply
be passed to AES. `security_counter` remains the only journal writer.
No new entropy, physical durability or cold-rollback protection is claimed.
