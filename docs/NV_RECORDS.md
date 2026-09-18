# Generic two-page snapshot journal

`nv_record` stores **one opaque 1..128-byte snapshot**, not a key/value database
or a Zigbee security record. It composes the real reserved-page
[reader/writer and RAM executor](ARCHITECTURE.md#verified-reserved-page-erase-and-program).
This is **host-tested, image-checked and simulated**, not physical power-loss,
endurance, counter-safety, key-storage or network-resume acceptance.
No board image links it. **Never flash or upload `nv_record_test.ihx`.**

## Format and selection

Each physical page 125/126 holds at most one record. Multi-byte integers are
little-endian; C structure layout is never stored.

| Page offset | Contents |
| --- | --- |
| `0..3` | Family magic `NVR1` |
| `4` | Format version `1` |
| `5` | Reserved zero |
| `6..7` | Length, 1..128 |
| `8..11` | Generation, 1..FFFFFFFF |
| `12..12+length-1` | Opaque payload |
| Remaining bytes through `2039` | Erased `FF`, including word padding |
| `2040..2043` | CRC32 of bytes `0..2039` |
| `2044..2047` | Separate commit word `CMT1`, programmed last |

CRC32 uses reflected polynomial `EDB88320`, initial/final XOR `FFFFFFFF`;
`123456789` has check value `CBF43926`. It covers header, payload and the
otherwise erased part of the page. It is accidental-corruption detection,
**not authentication or a collision-free guarantee against arbitrary faults**.
The independent native oracle uses bit-reversed, non-reflected polynomial
division; the linked oracle uses Python's standard-library `zlib.crc32`.

The scanner reads every byte of both pages through the existing reader.
All-FF is EMPTY, not evidence of known program history. Missing/torn commit,
bad format/padding and bad CRC have distinct page reasons. A committed
unknown version blocks selection and writes; it is not silently downgraded.
When both records are valid, their generations must differ by exactly one.
Ties or gaps return CONFLICT without choosing a page. One valid record plus
an empty page is normal; one valid record plus an incomplete/damaged page
returns **RECOVERED**, visibly distinguishing recovery from normal selection.
No valid record and nonempty media is CORRUPT, never automatic initialization.

Generation starts at 1 and **does not wrap**. Reading FFFFFFFF is permitted;
replacement rejects GENERATION_EXHAUSTED before erasure. Generation is an
ordering field, not an erase counter, trusted security frame counter or epoch
that can safely be reset during factory reset.

## Public operations and ownership

See [nv_record.h](../include/nv_record.h):

- `nv_record_load(output, capacity)` selects, then rescans the entire selected
  page while staging its payload. Only a still-valid matching generation and
  length may be copied out. OK and RECOVERED publish data; all other results
  preserve caller output. Bytes beyond the payload remain unchanged.
  Insufficient capacity returns NO_SPACE without partial publication.
- `nv_record_replace(data, length, poll_limit, allow_recovery)` rescans both
  pages, stages the complete caller payload, and replaces the inactive page.
  `allow_recovery=0` rejects degraded selection with RECOVERY_REQUIRED before
  erasure. Exactly `1` explicitly permits discarding that damaged inactive
  page; the completed call still reports RECOVERED. It cannot bypass unknown
  versions, conflicts, corrupt-only media or generation exhaustion.
- `nv_record_status()` exposes a read-only 15-byte diagnostic, not payload
  storage or a mutable recovery handle. Phase 0 means no admitted operation
  since reset. `selected=FF` means no selected record; service results start
  `FF` until invocation. Result PENDING and writer PENDING are established
  before entering destructive operations.

All calls are foreground/non-reentrant. The exclusive flash-controller,
mapping, clock, IRQ/DMA and genuine-reset contracts of the underlying services
remain mandatory. No other writer may modify these pages between calls.
Caller objects must be stable, complete ordinary XDATA after the entire
combined private/compiler prefix, below `1E00`; no private lease escapes.
Logical publication occurs only on success, but CPU byte copies are not
interrupt-atomic and permit no concurrent observer.

The diagnostic records selected generation/length, result, phase, both page
states, last reader/writer results, explicit recovery and per-page runtime
erase attempts. Phases are scan 1, load recheck 2, erase 3, body 4, integrity 5,
commit 6, post-commit verification 7 and done 8. Partial diagnostic fields do
not establish a committed replacement. Detailed flash controller causes remain
available in the existing writer diagnostic.

## Commit, garbage collection and recovery

Replacement first performs a **real erase and complete FF verification** of
the inactive page, even if it looks erased. Header and padded payload are
programmed in aligned four-byte words, followed by CRC and finally the
distinct commit word. Each goes through the existing one-attempt-per-word
policy and readback. A full-page format/CRC/generation check follows commit;
only then does the public diagnostic select the new generation.

The old selected page is never erased or invalidated during replacement.
It becomes the inactive garbage-collection candidate only after a complete
new record exists. Thus this simple two-slot design needs no separate
invalidate word, imported write history, append-to-blank inference or copied
record compaction. It trades page utilization for a small, explicit state
machine: at most one erase and 37 program commands per replacement.

An interrupted program may leave the old or new record selected after a
separately established full reset, depending on actual commit/integrity
bytes. The native and linked models exercise that boundary; they do not
prove how real cells behave during electrical interruption. A returned
error can also follow a physically committed record, for example if later
readback fails. **Error is not proof that no persistent change occurred**;
there is no automatic retry or transaction-side-effect rollback.

Reader/writer or post-commit verification faults retain the first failure;
later load/replace calls return it without further MMIO or diagnostic
replacement. No software fault/history-reset API exists. Active-controller
exhaustion stays inside the unchanged RAM fail-stop. The journal remains
PENDING in erase/body/integrity/commit, with writer PENDING and no caller
return, even if the synthetic controller subsequently becomes idle.

This does not authorize security-counter recovery from an older generation.
Future counter reservation, key lifecycle, schema and authenticated membership
must supply their own policy and satisfy the BDB/security and physical gates.
There is no factory reset, key erasure, default-key or membership-success API.

## Wear limits and uncertainty

Successful replacements alternate pages. Before each erase call, an attempt
is consumed from a **32-attempt per-page runtime budget**; rejected/failed
commands are not refunded. This upper-bounds admitted erases in that runtime
epoch and prevents an unbounded wear loop. Word-program history still comes
only from freshly verified erase, never from the record format.

These counters are deliberately **volatile**. Reset loses their history;
pre-reset or interrupted erase attempts and lifetime remaining endurance are
**UNKNOWN**, not reconstructed from generation, CRC, FF contents or successful
write counts. This implementation has no trustworthy persistent lifetime
erase counter. An application needing lifetime-wear limits needs additional
provisioning/accounting and physical evidence; it must not treat resetting
these counters as proof of renewed endurance.

## Allocation and offline evidence

Link executor, reader, writer, journal, then caller. The three existing flash
modules retain **byte-identical published linked instructions**, including
RAM copy/readback, active-controller fail-stop and common-C return. They are
not replaced by callbacks. The journal itself has no peripheral instruction;
its only external calls are the real reader, erase and program APIs.

| Item | Measured/checkable value |
| --- | --- |
| CODE | 7,046 bytes, unbanked |
| Ordinary XDATA + status reservation | 797 +64 =861, within a separate 1,024-byte budget |
| Complete private/compiler prefix | `0000..0294` |
| Diagnostic / staging / reader chunk / program word | `0195` / `01A4` / `0224` / `0244` |
| Caller buffer | `0295..0314`, 128 bytes |
| Stack | Starts `38`; observed peak `57`; upper IRAM remains guarded |
| CODE SHA-256 | `7214d763451bc53d74ca14f9a29fe06e8eb1524cf6088c5db41b97754ffc6d12` |

The 69,623 counted native checks cover every payload length and every byte
of both pages as corruption, independent full-record CRC/format comparison,
all 38 erase/program/commit boundaries before and after completion, all
33 prefix-bit tear positions for every program word and representative
partial-page erases. They also exercise full runtime-budget exhaustion,
conflicting/exhausted generations, caller bounds, late load recheck failure,
post-commit corruption and pending RAM stops in all four write phases.

The 61 linked sequences execute **17,149 actual flash API calls and 829
copied-RAM commands**. Every journal-level page/chunk, program source/offset
and erase admission is traced through its actual compiled call site; inherited
reader/backend instructions are pinned to their published complete proofs.
Each RAM command additionally verifies actual FCTL/FADDR/FWDATA, ordered
bytes, consumed word history, genuine return frame and idle-before-RET.
All 38 completed-command cut points restart through real C startup with
preserved synthetic flash, not cleared history variables or patched returns.

Whole CODE/ABI/allocation/listing mutation, private-prefix and caller guards,
upper IRAM, unallocated/status XDATA, excluded information/neighbor regions
and retained fail-stop are checked. Long transcripts are indexed once and
every simulator retains its 15-second deadline. Both board definitions
produce identical CODE and pass native/layout proofs. The ordinary limits
and all earlier component budgets remain unchanged; full-stack fit is unproven.

```sh
make BUILD=build/nv-record-check test-nv-record
```

`test-common` includes this standalone target; all board images reject the
module/harness. No hardware runner or CI upload path is added. The backing
flash/controller and interrupted-cell patterns are synthetic; real erase,
power-failure, endurance and recovery remain the separately authorized #8
and later persistence acceptance activities.

## Provenance

The format, CRC implementation, selection policy and tests are original
BSD-3-Clause work, not an imported filesystem, vendor journal or SDK algorithm.
Hardware facts and ownership are inherited from the
[reviewed flash sources](PROVENANCE.md#m2-reserved-page-write-policy-sources).
No private record, identity, capture, key, recovery dump or external firmware
is used as a fixture or published artifact.
