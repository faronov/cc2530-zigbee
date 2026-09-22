# Fractional MAC Timer epoch extension

The original [mac_epoch.c](../src/mac_epoch.c) supplies the arithmetic part of
#40, under #75. It extends coherent tuples from the fixed
[MAC Timer profile](MAC_TIME.md), retaining their fine phase. It performs
**no MMIO**, reads no timer and identifies no physical frame event. This is
not a CC2530 MAC adapter or permission to feed live samples to `mac_tx` as
captured TX/ACK ends.

## Units and continuity

The primary basis is unchanged: TI SWRU191F sections22.1.3/.8 and
T2IRQF pp198-199,204 replace a would-be period value by zero. With the
existing positive periods, fine is0..511 and coarse is0..FFFFFE. Thus:

```text
raw modulus = 0xFFFFFF * 512 = 0x1FFFFFE00 fine increments
half range  = raw modulus / 2 = 0xFFFFFF00 fine increments
            = 0x7FFFFF periods + 256 fine increments
```

At nominal undivided32 MHz, one fine increment is31.25 ns, one period16 us,
and the strict continuity interval is below134.21772 seconds. Those are
nominal ratios, not measured rates or PHY-event phase.

Consecutive accepted samples must have truthful forward elapsed time
**strictly below** that half range. The implementation subtracts coarse
coordinates moduloFFFFFF, borrows a period for decreasing fine, and compares
the resulting whole/fractional distance with the split half range. It does
not use a `&0xFFFFFF` mask, assume a2^24 modulus, round away fine, or need
64-bit target arithmetic.

Equality and numerically backward/ambiguous progress fail. Repeated identical
tuples are valid but do not establish a running clock. Missed complete wraps,
reset or a writer that recreates valid-looking progress cannot always be
detected: no arithmetic can infer that missing history. The caller must
establish continuity, coherent input and clock ownership independently.
Accepting numeric fine255/511 does not waive `mac_time`'s low-FF read discard;
this module cannot prove how any input was sampled.

## API and phase

[`mac_epoch.h`](../include/mac_epoch.h) defines an11-byte target context and
a6-byte target result. Objects are disjoint, complete, caller-owned persistent
ordinary XDATA outside compiler/runtime scratch. Public pointers are genuine
two-byte XDATA pointers; struct layout is not a wire representation. One
foreground caller owns the immutable-between-calls context. No pointers are
retained, so contexts may be moved or independently interleaved.

`mac_epoch_start(ctx, raw, symbols)` binds the **period containing** `raw` to
the caller's32-bit software coordinate. It preserves `raw.fine`; the sample
does not become fractional zero. Start initializes a new caller epoch and
does not start, stop or recover hardware. Every old user/event must be purged
before restarting, including after a time fault.

`mac_epoch_step(ctx, raw, output)` validates and advances that coordinate,
wrapping software symbols modulo2^32 and returning fine unchanged. For
example, raw`(FFFFFE,511) -> (000000,0)` crosses one coarse boundary but only
one fine increment. Output symbols increase by one; the fine field changes
from511 to0. Keeping only the integer field would misrepresent that interval.

| Outcome | Mutation |
| --- | --- |
| OK | Update the context and publish both output fields. |
| Invalid pointer/numeric range | Preserve the entire context and output. |
| Invalid context state | Preserve the entire context and output. |
| Time error | Change only state to FAULT; retain the last accepted coordinates and leave output unchanged. |
| Retained FAULT | Return TIME_ERROR without further mutation, even with NULL raw/output; ctx itself must be non-NULL. |

The output is an extended **fractional counter coordinate**, not a captured
PHY end or a ready-made integer `mac_tx` stamp. Capture selection,
freshness/overwrite, event correlation, ordered delivery and a justified
event-time quantization policy remain #40 requirements. Radio ownership,
physical timing and full #50 continuous AUTOACK/POLL behavior are unchanged.
Neither the existing `mac_time` quiescence contract nor any radio API is relaxed.

## Offline evidence and resources

`make test-mac-epoch` runs the strict native corpus, nonrecovering ASan/UBSan,
and genuine SDCC/alias-aware proof once per board through `test-common`.
GitHub Actions supplies full acceptance; local work is artifact preparation
or narrow failure reproduction, not a duplicate full matrix.

The native oracle independently constructs complete64-bit raw coordinates
and subtracts modulo the full raw modulus. It covers all512x512 fine pairs
at six coarse distances (1,572,864 cases), exact half-range/borrow boundaries,
4,096 continuous samples across many raw/software wraps, every invalid
16-bit fine value, and exact-sized heap allocations. The common target corpus
executes5,766 checks, including all512 phases at raw/software wrap, independent
contexts, moved contexts, input preservation, invalid calls and retained faults.

Canonical SDCC4.2.0 flags and unchanged unbanked layout:

| Object | CODE including constants/startup | Ordinary XDATA | DATA | OSEG |
| --- | ---: | ---: | ---: | ---: |
| mac_epoch | 954 | 22 | 14 | 0 |
| caller | 3961 | 77 | 0 | 0 |
| linked runtime | 527 | 20 | as linked | as linked |

Whole image: **5442/8192 CODE**, **119 ordinary XDATA +64 reserved =183/512**.
The context and result allocations are included in the caller, not hidden
from those totals. Stack starts at21, initial/checkpoint SP20; the pinned
whole-run peak is2F under the unchanged7C cap. Upper IRAM and the1F00 alias,
unused XDATA/status tail, GPIO/clock/timer/RF guards and unwind are checked.
This separate composition is not linked into the integrated ZCL image and
does not claim whole-stack memory or interrupt-nesting headroom.

CODE SHA-256:
`f3b736e20ab790097034cb4e0676dc37279033c18d2bda0a5444a0a1f58e58f5`.
Raw CDB SHA-256:
`3ccacc99c08c9a3780a249573ae829c2957b2ec592fb9c8ec15962c6249ab4ad`.

The proof binds every CODE/runtime/constant byte, raw CDB before decoding
(including all public/private F/S/L/T records and multiplicities), every map
entry, both complete relocatable objects, all storage/entry/helper labels and
ordered relocated instructions. Both listings are snapshotted immediately
after link. It checks actual calls to both production APIs and excludes
peripheral instructions, not just high-level MMIO macros. There are15,821
artifact, seven snapshot and one genuine missing-alias negative controls.
The simulator retains its15-second process deadline.

**Host/image/simulator evidence only. Never flash `mac_epoch_test.ihx`.**
No board IMAGE, hardware observation, entropy, security or network membership
is introduced. A real TX/RX board fixture is a separate next activity; ordinary
bidirectional radio testing need not claim that the unresolved capture
semantics have been established.
