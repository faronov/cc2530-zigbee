# Delayed raw-sample projection

The original #81 `mac_stamp` module supplies a bounded arithmetic prerequisite
of #40, following [fractional epoch extension](MAC_EPOCH.md) and the
[co-owned live clock/radio composition](MAC_RADIO.md). It projects a delayed
raw sample into a known closed time window **without rewinding the live
epoch**. It neither reads a capture register nor proves that a timestamp
belongs to a frame.

## Contract

`mac_stamp_project(first, last, sample, output)` takes:

- `first`: an immutable READY `mac_epoch_t` snapshot at the beginning.
- `last`: a coherent raw tuple at the end, in the same uninterrupted epoch.
- `sample`: an independently established coherent raw tuple to place in that
  window.
- `output`: the exact extended `(symbols32, fine16)` coordinate on success.

The caller knows the window's true duration is strictly less thanFFFFFF00
fine increments, under the existing positive-period profile. Its complete
raw modulus is`0xFFFFFF * 512 = 0x1FFFFFE00`, **not2^33**. Inclusive endpoints
and zero-duration windows are supported; equal numeric coordinates do not
establish physical clock progress.

Calls are serialized with `mac_epoch`, not reentrant or ISR-safe. Every object
is immutable/disjoint while borrowed, complete persistent ordinary XDATA
outside all linked compiler/private/runtime scratch. No pointer is retained.
All caller inputs are preserved, including the live epoch's state. Every error
preserves all output bytes. A rejected candidate does not poison the live
epoch; the caller may continue legitimate forward live samples.

| Result | Meaning |
| --- | --- |
| OK | Numeric sample lies in the valid closed window; publish both fields without rounding. |
| INVALID_ARGUMENT | NULL argument or invalid raw coordinate, subject to the underlying epoch's fault precedence for non-NULL inputs. |
| INVALID_STATE | The first epoch snapshot is not READY or has invalid stored coordinates. |
| TIME_ERROR | A faulted first snapshot, reversed/ambiguous/half-range window, or sample outside the window. |

**OK is not capture freshness.** A stale capture with the same numeric value,
a missed full wrap or a foreign epoch cannot be detected by this arithmetic.
Generation/ownership, coherent hardware capture, event identity, overwrite
prevention, physical edge selection and offsets remain separate obligations.
Fine255/511 are legitimate arithmetic values; accepting them does not waive
the live reader's low-FF discard or establish capture-register read semantics.

## Algorithm and bounds

The implementation reuses three genuine `mac_epoch_step` calls with private
temporary storage, instead of duplicating its modulo, borrow or half-range
logic:

1. Copy `first` and advance to `last`, validating the whole window.
2. Copy `first` again and advance to `sample`, staging its extended coordinate.
3. Advance that temporary state to `last`, rejecting a sample beyond the end.

Publish only after all three succeed. Each call and copy is finite; there are
no retries, loops over time, MMIO accesses or 64-bit production arithmetic.
Copying temporary arithmetic state is not restarting/adopting a hardware epoch.

Why both sub-arcs suffice: let the raw modulus beM. The whole forward window
has lengthw<M/2. Accepted sub-arcs from first to sample and sample to last each
have length belowM/2, so their sum is belowM and congruent tow moduloM.
Consequently their sum equalsw, and the sample lies within the window.
Checking only first-to-sample would wrongly admit values beyond `last`;
checking only sample-to-last would wrongly admit values before `first`.

For example, first`(periods=FFFFFE,fine=500,symbols=FFFFFFFF)` and
last`(periods=0,fine=10)` bound a22-fine-increment window. A sample at
`(FFFFFE,511)` maps to`(FFFFFFFF,511)` and one at`(0,5)` to`(0,5)`.
Neither raw nor software wrap loses fine phase. An otherwise valid sample at
`(0,11)` is outside the window and leaves output unchanged.

This returns a fractional coordinate, not an integer `mac_tx` event stamp.
No offset correction, PHY-end derivation, rounding policy, event ordering
queue or physical timestamp is invented.

## Evidence and resources

`make test-mac-stamp` runs strict native tests, nonrecovering ASan/UBSan and the
actual linked SDCC/alias-aware target proof. It is part of `test-common` once
per board; CI executes it in the existing two composition jobs, preserving
all28 board/image jobs; the separate interval owner brings the current
partition to 32 jobs. No new timeout, dependency,
board IMAGE, hardware runner or artifact upload is added.

The independent native oracle uses full64-bit coordinates and separately
checks whole-window duration and candidate offset. It covers all512 initial
fine phases, twelve window lengths including exact half-range boundaries,
512 nearby candidates plus both endpoint neighbors,4,096 additional coordinate
cases, every invalid16-bit fine value in all three input positions, exact-sized
allocations and immutable-input/error-output guards. Native and sanitizer
each execute **33,461,984 checks**.

The common genuine target corpus executes **23,769 checks**, including all512
fine phases across raw and32-bit software wraps, both inclusive endpoints,
out-of-window values, zero/reversed/ambiguous windows, invalid inputs/context,
preservation and continued live-epoch advancement after projection rejection.
The large native oracle is not claimed as a target run.

| Linked unit | CODE | Ordinary XDATA | DATA | OSEG |
| --- | ---: | ---: | ---: | ---: |
| Existing mac_epoch | 954 | 22 | 14 | 0 |
| mac_stamp | 397 | 31 | 1 | 0 |
| Target caller | 3291 | 94 | 0 | 0 |
| Linked runtime/startup | 527 | 20 | as linked | as linked |

Whole image: **5169/8192 CODE**, **167 ordinary XDATA +64 reserved =231/256**.
Stack begins21, initial/unwound SP20, full-run peak3A under the unchanged7C
cap. This is not whole-stack or interrupt-nesting headroom.

CODE SHA256:
`b1d280b94831bfb45ea65d9866ee3fb2f9b1e0ce162c60f6f94124dff4c2df5b`.
Raw CDB SHA256:
`11c3cb68b8b1719870455a9ca6d27146cef9dbbff2d2dae0cc32ae7690b21a20`.

All three relocated listings are snapshotted immediately after link. The proof
binds complete CODE/constants/runtime, raw CDB before decoding, every map
symbol, complete objects, ordered instructions and allocation/entry/helper
records. It verifies actual public calls and all three real epoch advances,
excludes peripheral instructions, and checks unused/status-tail/alias XDATA,
GPIO/clock/timer/RF guards, stack unwind and full-run high-water. There are
15,644 artifact negatives, seven snapshot mutations and one missing-alias
negative. The simulator process deadline remains15 seconds.

**Host-tested, image-checked and simulated only. Never flash
`mac_stamp_test.ihx`.** Existing epoch/radio CODE and proofs are unchanged.
The new module is not linked into board firmware or the radio owner: doing
so before establishing capture coherence and event identity would not solve
#40. No new primary hardware evidence, measurement or Zigbee membership is
claimed.
