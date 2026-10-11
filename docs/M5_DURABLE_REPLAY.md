# M5 durable router replay: audit and proposed format

This is a storage development record, **not acceptance of new firmware**.
The starting candidate is
`e4c41c8e8a89bfe0ef517bf04dc3747b904783d1`, on `m5-secure-rejoin`.
Development is separate, on `m5-durable-replay`. Production storage is still
unchanged. No compiler, hardware, HWLab, frozen image or flash partition change
is part of this work.

## Qualification gate

The supplied Actions ID `3814096302` returns 404. The actual candidate run is
[38140096302](https://github.com/faronov/cc2530-zigbee/actions/runs/38140096302).
At the audit checkpoint it is **in progress**: the toolchain and exact-pin FAST
job passed, while `full-1` is still executing. There is no accepted FULL receipt
yet. Before production storage edits, preserve its final receipt/results or
classify its failure. Test-only experiments do not replace that gate.

The compiler requested for final acceptance remains
`1a01afb49d99ec754e62ab0f65f182de87b4cee3`. A local build with another source
identity is development evidence, not that exact-pin qualification.

## Measured snapshot cost

`make test-parent-storage-audit` uses the real `security_keys`,
`security_counter`, `nv_record`, reader, writer and RAM-executor services with
the existing combined flash/AES peripheral model. Linker wrappers count APIs;
the controller independently counts physical commands. The input is an
authenticated router NWK frame after genuine key exchange and parent admission.

| Operation | Erases | Programs | Program bytes | Read calls | Read bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| Router NWK-only frame | 1 | 45 | 180 | 539 | 14697 |
| First subsequent NWK allocation | 1 | 45 | 180 | 539 | 14697 |
| First subsequent APS allocation | 1 | 45 | 180 | 539 | 14697 |
| Same router frame after reset | 0 | 0 | 0 | 0 | 0 |

The modified field is one four-byte exclusive incoming replay floor; the chosen
`1 -> 2` example changes just one serialized byte. Three complete reads in
`security_counter.current`, two in journal selection, one erase verification
and one post-commit check account for seven 2048-byte traversals. The additional
361 read bytes are erase preflight plus per-word preflight/readback.

The extra two replacements are real amplification, not required reception
writes: `security_counter_save` abandons both remaining outgoing reservations.
The next allocation in each domain reserves again. A replay-only operation
could retain live cursors **only while both persisted outgoing ceilings and
all non-replay context remain unchanged**. A true reset must still start from
the durable ceilings, never the abandoned volatile cursors.

The baseline audit passes 8673 checks in both native and nonrecovering sanitizer
builds, including 1582 command/reset cuts. Of those cuts, 1574 fail closed, two
recover the fully committed new floor and reject replay, and six recover the
unpublished old state. A post-physical-commit reader failure is also followed
by reset and replay rejection. This is host composition, not electrical proof.

Separately, the unchanged 128-byte journal MCU fixture passes 61 linked
sequences, 17149 actual flash API calls and 829 RAM commands, with exact
CODE/private-ABI/alias guards and peak SP `4C`. This establishes the existing
flash backend, **not execution of the proposed format on the MCU**.

## Primary-source timing and endurance boundaries

Sources:

- [TI SWRU191F](https://www.ti.com/lit/ug/swru191f/swru191f.pdf),
  printed pp.72-74, especially section 6.2.2.
  SHA256 `a8fe8e92db33ad79c7f371075b0a464602a6db747614625d9f8d3e6be990b877`.
- [TI SWRS081B](https://www.ti.com/lit/ds/swrs081b/swrs081b.pdf),
  printed p.5, General Characteristics.
  SHA256 `a1af423a2a658410d6f2bd1b2c96b0f536d1febcd259f4901f9312bbcad103a9`.

SWRU191F specifies 2048-byte pages, 32-bit words, a 20 ms erase and a 20 us
four-byte write. Thus **controller-only nominal** cost is 20.9 ms per snapshot,
excluding reads, CRC, AES, mapping and CPU overhead. This is not a measured
RX-disabled interval: storage does not explicitly turn RF RX off. Foreground
processing is blocked; FIFO/AUTOACK can remain active. Actual complete-service
blackout, FIFO loss and RF-off time still require distinct MCU observations.

Programming clears zero bits and leaves one bits unchanged. TI permits bounded
repeated programming (at most eight word writes, at most two zero writes per
bit, at most 1024 word writes per page between erases). The existing service is
deliberately stricter: **one attempt per word per verified erase epoch**.
The proposal preserves that stricter policy. FF bytes after reset do not prove
that a zero-effect interrupted programming attempt never happened.

The downloaded SWRS081B table places 20k erase cycles in the **MAX** column,
not MIN. It is not evidence of a guaranteed 20k-cycle minimum under the intended
operating conditions. Temperature, voltage, interrupted-cell behavior,
previous wear and persistent lifetime accounting remain unknown.

For one replacement per admitted RX, assuming perfect page alternation:

| Incoming rate | Total erases/hour | Per-page erases/hour | Upper 64-replacement boot budget |
| --- | ---: | ---: | ---: |
| 1 / 30 s | 120 | 60 | 1920 s |
| 1 / 5 s | 720 | 360 | 320 s |
| 1 / s | 3600 | 1800 | 64 s |
| 10 / s | 36000 | 18000 | 6.4 s |

Setup, reservations and control writes shorten those bounds. A reset renews
the volatile boot guard, **not physical endurance**. No guaranteed lifetime
is derived from these rates or the 20k figure.

## Architecture comparison

| Approach | Security | Cost / limitation | Decision |
| --- | --- | --- | --- |
| A. Existing complete snapshot | Strict durable admission; conservative degraded-page refusal | One erase per update, plus abandoned outgoing reservations | Correctness reference |
| B. Commit-last replay bundles plus checkpoint | Can preserve strict admission if identity, key slots and ordering are bound to the checkpoint | More parser/proof work; finite log and physical endurance; fresh erase after reset | Proposed, not production-qualified |
| C. Delay/batch already published frames | Previously admitted frames can replay after power loss | Fewer writes by weakening the contract | Rejected as equivalent |
| D. Bound traffic before admission | Can preserve replay safety by withholding publication | Availability, buffering and control-traffic/conformance limits | Explicit fallback profile only; not silently enabled |

Batching can be strict only if **no frame in the batch is published before its
commit**. The RX FIFO, deadlines, queue ownership and power-cut drop policy then
need a separate proof; it is not the least invasive first change.

## Proposed version 2 page

Use only the existing two reserved pages, physical 125/126,
`0x3E800..0x3F7FF`. No third writer or partition expansion. Multi-byte fields are
little-endian; never write a C structure image.

The checkpoint remains the complete 160-byte CTR1/K3 snapshot. Family magic
`NVR1` is retained, but version is 2 so old firmware fails closed rather than
mistaking log bytes for version-1 padding. K3 and the CTR1 envelope do not migrate
back to K2.

| Page offset | Contents |
| --- | --- |
| 0..11 | Existing family/header shape; version 2, length 160, nonzero base generation |
| 12..171 | Complete CTR1 envelope and K3 payload |
| 172..175 | FF padding |
| 176..179 | Checkpoint CRC32 over 0..175 |
| 180..183 | Separate `CMT2` commit word, last checkpoint program |
| 184..191 | FF |
| 192..2015 | 38 consecutive 48-byte replay bundles |
| 2016..2047 | FF, not another partial slot |

A replay bundle stores **all five distinct incoming floor fields**, not a
shared maximum. This handles router NWK and TC APS protection on the same frame
without falling back to a snapshot every time.

| Bundle offset | Contents |
| --- | --- |
| 0..3 | Logical generation, exactly previous generation + 1 |
| 4..7 | Owning checkpoint's base generation |
| 8 | Changed-floor mask, bits 0..4 only, nonzero |
| 9 | Bundle version 1 |
| 10..11 | Reserved zero |
| 12..31 | TC NWK slot 0, TC NWK slot 1, TC APS, router NWK slot 0, router NWK slot 1 |
| 32..35 | Owning checkpoint CRC |
| 36..39 | Previous logical generation |
| 40..43 | CRC32 over bundle bytes 0..39 |
| 44..47 | Separate `DMT2` commit word, programmed last |

The fixed CTR1-relative floor offsets are 96, 100, 104, 138 and 142. Every
masked field must strictly advance; every unmasked field must equal the
previous folded value. A masked NWK slot must be present in the checkpoint;
a masked router field additionally requires a known router owner. A mask never
grants permission to modify identities,
key bytes/sequences/flags, outgoing ceilings, retired-key history, membership,
parent selection, address or update ID.

Snapshot generation plus snapshot CRC bind each record to the complete
identity/key-slot/domain context. This is accidental-corruption and ownership
association, **not an authenticated anchor against malicious physical flash
replacement or CRC collisions**; the existing snapshot has the same limitation.
Key rotation, owner changes and non-floor state transitions require a complete
checkpoint through the existing exclusive owner.

## Recovery and publication rules

1. Scan and validate both complete pages, including all padding and every log
   slot. Only an intact committed checkpoint with an entirely coherent log is
   usable. A nonempty incomplete/invalid tail fails closed; never silently drop
   it and expose an earlier floor.
2. Version-1 pages remain readable under their original CRC/padding rules.
   Unknown versions block selection. A valid page with a damaged other page
   remains a degraded selection, rejected by the security owner.
3. Two valid pages must form a checkpoint chain: the newer **base** generation
   is exactly the older page's final folded generation + 1. Numeric "largest
   generation wins" alone is insufficient once logs introduce gaps.
4. Opening folds each valid bundle in order into the complete snapshot. A
   committed bundle survives a failed API return. Its frame may have been
   withheld, but its floor remains consumed.
5. Reset invalidates append eligibility unconditionally, even with a completely
   FF tail. Before the first subsequent durable change, checkpoint the folded
   snapshot/change to the inactive page through a **real erase and full FF
   verification**. Only that boot-local freshly erased selected page may append.
6. Stage a new bundle, program eleven aligned body/CRC words, then its twelfth,
   distinct commit word. Recheck durable contents before publication. Never
   reattempt any word after an error. Retain failure until genuine reset.
7. On log exhaustion, non-floor change or fresh-erase requirement, checkpoint
   the complete desired snapshot to the other page, commit last and recheck.
   Keep the old complete checkpoint/log intact until the new checkpoint commits.
   A torn checkpoint does not authorize rollback to the old security state.
8. Logical generation never wraps. At FFFFFFFF, read is permitted but every
   mutation is rejected before any flash command. Retain the existing
   32 erase-attempts/page/boot guard and count attempts before command entry.

A cut leaving all programmed bits FF cannot be detected from media alone.
Rule 5 prevents reuse of that unknown word history. A cut exposing a nonempty
tail is deliberately unavailable rather than replay-unsafe. This proposal does
not promise uninterrupted recovery from every electrical fault.

## Expected cost, not yet measured production reduction

An append is twelve program commands / 48 bytes / nominal 240 us controller
time, with no erase. A checkpoint is 45 programs / 180 bytes and one erase.
One checkpoint followed by 38 bundles represents 39 durable changes per erase,
or a theoretical 97.44% RX erase reduction before resets/control changes.
Steady-state program count is `(45 + 38*12)/39 = 12.846` per change.
Those are format arithmetic, not production measurements.

At one incoming frame per 5 s the replay-only component would be about
18.46 total erases/hour, rather than 720, before control/counter/checkpoint
overhead. Even this consumes the unchanged boot guard eventually. Reset-heavy
or context-change-heavy traffic can approach snapshot cost. The design cannot
provide unbounded traffic/lifetime with finite flash and strict durability.

## Planned sustained-service acceptance

Define the workload before implementation: 30 minutes of genuine protected
router traffic, one incoming frame per 5 s with periodic 10-frame bursts at
2 frames/s, application report every 5 s, normal 30 s keepalive, idle intervals,
one key-slot transition and a power cut/restart at the midpoint. Establish a
fresh trusted parent through actual commissioning/recovery, not callbacks.
Attempt duplicates/replay immediately and after restart.

Admission requires READY during each normal service interval, every published
frame's replay state durable, no replay publication, monotonic outgoing
ceilings, restored ED Timeout/Announce after restart, no STORAGE from the
unchanged boot guard, and at most 24 erases total for that declared workload.
Measure actual erases, programs, RX-off and foreground blackout separately;
check terminal and stack independently. These thresholds are a planned profile,
**not passed results**. Physical lifetime and arbitrary-rate operation remain
outside that claim.

## Remaining gates

The isolated `make test-replay-log-reference` experiment currently passes
594406 checks in both native and nonrecovering-sanitizer builds. All reads,
erases and programs go through the existing real C flash services; only the
peripheral/cell model and the proposed format parser are test code.

| Transition | Command/reset cuts | Fail closed | Old/unpublished state | Committed new floor |
| --- | ---: | ---: | ---: | ---: |
| Append | 420 | 415 | 3 | 2 |
| Checkpoint | 1582 | 1574 | 6 | 2 |
| Full-log compaction | 1582 | 1574 | 6 | 2 |

The experiment also checks each incoming domain independently, a joint router
NWK/TC APS update, forbidden non-floor mutations, absent router ownership,
real K2-to-K3 migration with an older K2 page still present, replay-floor
regression, owner/key-context detachment, every bundle byte, generation
exhaustion, CRC-valid malformed metadata, checkpoint generation ties/gaps,
unknown versions, outgoing ceiling rollback, a gap before a committed bundle,
the unchanged 32/page guard, and reader failure after a physically
committed append **and** checkpoint. The cut model is the existing prefix-bit
program/quarter-page erase model, not all possible electrical cell failures.

The parser deliberately uses a large host buffer and is prohibited from target
compilation. It is a format/recovery experiment, **not the production
implementation or new-format MCU proof**. The input K3 snapshot comes from
genuine production key exchange, key rotation and router admission; format
tests do not establish network READY continuity or live reservation behavior.

An additional unchanged outgoing-counter check passes 82482 native/sanitizer
checks but **fails standalone MCU admission** at the existing immutable raw-CDB
fingerprint. This reproduces on untouched `e4c41c8`: actual CDB SHA256
`bd50c14ce4d9a899c417b448911085fba338f4f2eb333629ff496749aa1fd219`
versus expected
`f0649222d958c071de28e4e72bb8535b615234a474c56f97c44193ecc843b2f3`.
CODE, parsed map and memory fingerprints match; the cause of debug-metadata
drift remains unclassified. No expected hash or verifier was changed, and
this extra check must not be reported as MCU PASS.

Fresh LLVM FAST passes 11/11 on the test-experiment tree using the explicitly
different local compiler identity `2feda4a4872c85594311a87d52178954d9dab5b4`.
The physical HEX SHA256 remains
`c3e356c86d2a14822c1ab21a8d822d409358a0c683def0901e57a64526b7f7e5`,
identical to the original candidate's CI FAST image: production semantics and
resource deltas are zero. This does not convert pending original FULL into PASS.
An initial FAST invocation failed because an explicitly supplied relative
Python path was resolved from the tier-test subdirectory; its failed receipt
is retained separately. Retrying with the launcher's absolute interpreter
passed without changing source or acceptance checks.

Before production integration: prove the proposed format with deterministic
real-flash-service fault injection, including physically committed/API-failed
records, zero-effect tears, compaction, all ownership/key/counter boundaries
and generation/budget exhaustion. Then integrate one writer, preserve static
frames/loans and live outgoing reservations only for verified replay changes.

Require focused host tests, fresh LLVM FAST, integrated SMOKE, actual linked-MCU
new-format execution and one final exact-pin FULL with all existing scenarios
and an explicit sustained-service scenario/checker. No cached receipt can admit
changed storage or acceptance semantics.

Current starting resources remain CODE 222717, common 18944, populated HEX
222906, raw XDATA 5212, static stack 106/128, and zero incorrect thunks.
Banks 6/7 have 5337/14122 bytes free; banks 1..5 remain full.
There are **no production resource deltas or measured new stack peaks yet**.
The SDCC optional candidate still exceeds its selected switchable layout by
7883 bytes, including a 4799-byte JS_BDB bank overflow; there is no admitted SDCC
image. Storage work does not remove that separate compatibility limitation.
