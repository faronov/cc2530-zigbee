# Bounded banked MAC/radio action adapter

`CC2530_MAC_ADAPTER` connects the real interval MAC scheduler to the existing
clock, Timer2, fractional epoch, radio and attempt services. It requires
`CC2530_MAC_RADIO`, `CC2530_MAC_ATTEMPT`, `CC2530_MAC_HANDOFF`,
`CC2530_MAC_INTERVAL` and `CC2530_MAC_OBSERVED`. No board image enables it.
It is an original BSD-3-Clause foreground owner, not a mocked PHY backend.
**Never flash the synthetic `mac-adapter/mac_adapter.ihx` test composition.**
Calling `init` on equipment enables RX/AUTOACK and requires separate RF
permission; offline acceptance grants none.

This is a bounded #13 prerequisite, not completion of #13/#14, a
hardware-observed result or an authenticated real-radio join. The already
accepted [complete banked join](ED_JOIN.md#complete-banked-mcu-execution)
still has synthetic PHY events. Its interval-aware scan/POLL/association
binding and actual combined RAM/call-lifetime placement remain separate.
This owner has fixed initialization-time channel/PAN/address configuration.
The separately gated [OFF-only reconfiguration and reopen](#off-only-reconfiguration-and-reopen)
profile adds real retuning/address installation and new RX episodes without
resetting the device-wide MAC/epoch or bypassing ownership; it is not yet
linked into an MCU composition.

## One owner, real actions

The caller retains a single device-wide `mac_tx_interval_t` and drives
`mac_tx_observed_step`, not `mac_tx_step`, `mac_poll` or `mac_join` on its
embedded engine. Existing exact-event and interval-only entry points keep
their original contracts. Public objects are persistent, disjoint ordinary
XDATA outside the entire linked adapter prefix and **all** linked libc
scratch. The new composition includes four multiplication parameter bytes
after `__gptrput_PARM_2`; that older marker is not its runtime end.

The caller and hardware work have separate call lifetimes:

1. `mac_adapter_init` performs real clock/timer/radio acquisition and starts
   normal RX/AUTOACK. `mac_adapter_now` returns a genuinely observed clock,
   including while a TX slot is prepared.
2. Complete explicit close/drain before TX preparation. Submit through the
   genuine MAC, then `mac_adapter_prepare` copies its immutable body through
   the real interval API and loads the real TXFIFO.
3. Drive the MAC. RANDOM is an explicit caller responsibility requiring an
   independent uniform byte; there is no successful RNG/entropy stub.
   Queue the genuine one-shot ATTEMPT/COLLECT/QUIESCE through `accept`.
4. `step` samples time and performs at most one radio operation. Pass a
   nonzero observation `tx.source.kind` to `mac_tx_observed_step`; otherwise
   pass NULL and still service the MAC's clock/work budget. Source kind zero
   is not a MAC event. Consume the exact delivery token before reuse.
5. MAC completion/release does not close an independent RX lease.
   `KEEP_RAW` can retain raw RX after an unacknowledged transmission;
   `KEEP_AUTOACK` needs a provably timely matching first ACK and the real
   [guarded handoff](MAC_ATTEMPT.md#guarded-live-rx-handoff).
   Explicitly close that lease before another preparation.

Cancellation/work exhaustion before ATTEMPT can finish the MAC while leaving
the physical slot prepared. `unprepare` requires that same completed owner
and actually stops the lower prepared slot; it makes no RX-coverage claim.
Device-wide DSN/generation survives cancellation, retry and resubmission.
Duplicate actions, stale delivery tokens and invalid storage fail explicitly.

ATTEMPT waits until the observed clock reaches the requested `at`. It returns
EXPIRED without starting a run when that observation reaches `until`.
**CPU latency and the eventual hardware CCA strobe are not bounded by that
pre-call test.** Requested schedule is not captured CCA time or calibrated
PHY conformance. #40 and #45 remain open; no interval is relabeled as a
captured edge.

## Honest busy and retirement sources

The gated `mac_tx_observed_step` extends the interval owner with two sources
that the original interval step rejects:

| Source | Required evidence |
| --- | --- |
| `BUSY_INTERVAL` | One actual busy CCA in `[lower,upper]`, no TX, and completed physical stop/drain with no future buffer use for that action |
| `RETIRED` | At observed `upper`, no future ordinary TX/buffer access for that action; an independent RX/receiver-ACK lease can remain active |

BUSY's lower bound is the genuine pre-run sample plus the already reviewed
minimum eight-symbol CCA dwell, not a reported captured decision. The returned
arming observation bounds that actual decision above. Physical cleanup
precedes publication. MAC NB/BE progression, five-busy limit and immutable
DSN/body remain genuine scheduler operations.

RETIRED is not an exact RF-off timestamp. Uncertain TX uses its conservatively
rounded upper bound for IFS. Both sources echo generation/retry/NB, require
report time no earlier than `ceil(upper)`, and preserve the real cleanup
deadline/work limits. Atomic malformed input and stale sources do not
manufacture progress. In particular, `ceil(current fractional sample)` is
not a current symbol clock: the adapter waits until actual time reaches the
next boundary before publishing a fixed fractional upper.

## Frame preservation and loss-aware closure

One delivery remains stable until its exact token is consumed. Every complete
head, including bad CRC, non-ACK and wrong-DSN frames, is retained with original
body/CRC metadata and RX serial. `frame` is meaningful as an incoming head
only for `RX_EVENT`; a pointer on a TX/closure observation is not another
reception. Long backpressure is not permission to overwrite or flush.

For an absent ACK the adapter continues receiving through the real
`tx_upper + 54` boundary. Closure records a live watermark **before the first
soft stop**, while RX is still owned/requested. It then performs the genuine
stop, drains every complete FIFO head, and checks physical idle/empty again.
Only then may RX_CLOSED attest coverage through that saved watermark.
An EMPTY call alone, STOPPED alone or a later post-stop clock sample cannot
establish coverage. Multiple drain passes never move the watermark forward
while RX is off. An independently requested close uses the same rule; a
future requested end is reached by actual reception/clock progress.
An explicit end must be no earlier than the latest observed live point and
less than half the32-bit symbol epoch ahead, including its fine phase.
Past/ambiguous requests are INVALID; use NULL to close immediately instead.

RF overflow, incomplete lower completion, invalid clock, an SFD race, changed
filter readback or a bracket of512 ticks or more retains an explicit fault.
It does not publish successful closure or retirement. Original bytes remain
available through the read-only receipt when a clock fault occurs after
receiving but before publication; `held` and `bound_valid` distinguish this
from a valid timed delivery. No stale time is fed to the MAC as a fabricated
FAILURE event. The fault is out of band and ownership is retained, without
automatic reset/reinitialization/recovery.

The handoff only accepts the original first raw ACK and cannot be revived by
an intervening radio operation. A later ACK is still preserved and subjected
to the real interval classifier, including timing uncertainty; it does not
silently authorize a stop/resume gap or restore first-ACK eligibility.
There is no per-frame receiver-ACK attestation, complete POLL procedure,
unicast-only policy for arbitrary input or full MLME conformance.

## OFF-only reconfiguration and reopen

`CC2530_MAC_RECONFIG` (requires the complete adapter profile) adds
`mac_adapter_configure` and `mac_adapter_open`, layered through
`mac_attempt_configure`, `mac_radio_configure` and `radio_autoack_configure`.
Both calls are accepted only in adapter `OFF` after a consumed closure,
retirement or unprepare: no held frame, pending delivery or closure goal.
The radio must be this owner's `OFF`/`OFF_NOACK` state, `STOPPED`, with an
empty FIFO and RFIDLE set.

Configure rewrites only the differing PAN (`0x6172/73`), short-address
(`0x6174/75`) and FREQCTRL (`0x618F`, `11+5*(channel-11)`) bytes, in that
order, while the receiver mask is clear ([SWRU191F](PROVENANCE.md) pp.214,
256). Each write is followed by a whole-profile readback of all25 acquired
settings, the frame-filter and AUTOACK/CCA state, stopped/idle and FIFO checks.
IEEE address and power are immutable and must equal the initialization values;
an invalid channel, power, IEEE mismatch, storage or state returns
`INVALID`/`STORAGE`/`RANGE`/`STATE` with no MMIO and no diagnostic change.
A readback or controller failure is a retained terminal `RADIO_ERROR` like
every other operation. Configure never restarts RX.

Open starts a **new** normal filtered RX/AUTOACK episode through the existing
resume service. After a raw lease it restores the normal filter/AUTOACK
profile first. Its `opened` output is a live sample taken after the receiver
is confirmed ready: a conservative coverage start, not a captured edge.
Frames sent while `OFF` were not received; the gap is explicit and never
continuous coverage. `RX_EVENT` `tx.lower` is the last transmission's
bound, not a frame lower bound; consumers order frames by `rx_serial` and the
open stamp. A transmission may also be prepared directly from `OFF` on the
new channel. None of this is network membership, PIB validation, a
coordinator realignment or recovery.

Six native sequences over the handoff radio model exercise reopen,
five-byte PAN/short/channel install, identical and partial reconfiguration,
a four-channel raw Beacon-request sweep with per-channel TX/RX FREQCTRL checks,
AUTOACK decisions against the installed address, boundary values
(`0000`, `FFFE`, `FFFF`, channels11/26), a corrupted readback fault and every
invalid/state/storage guard. The same binaries rerun all18 adapter sequences
with the gated profile compiled in. The model does not implement frame
rejection; only its AUTOACK decision reads the installed registers.
Normal and nonrecovering-sanitizer builds pass. The four gated production
objects compile under the banked SDCC flags with `--Werror`, and every existing
profile's objects, relocations and debug records are byte-identical to the
previous code. This increment is **host-tested and compile-checked** only:
there is no linked RECONFIG image, MCU replay or hardware result yet.

## Linked composition and evidence

`make BOARD=generic test-mac-adapter` (or `BOARD=lg_esl29_rev03`) includes the
original interval corpus plus the observed-source boundary corpus, ordinary
native/nonrecovering-sanitizer adapter tests, and two target-derived native
reference executables. A generated header comes from the **actual CDB**,
not native padding. Only known original receipt pointers can be serialized.

The MCU dispatcher executes real public APIs. Inputs are declared caller
configuration/body/timeout/pointer/random/cancel commands and actual peripheral
values. MAC contexts, adapter progress, source observations and success flags
are **never installed from the reference**. All public outputs and retained
receipts are compared after each genuine call.

| Resource | Actual composition |
| --- | ---: |
| Common CODE, including constants | 28557 bytes |
| Bank1: MAC codec and scheduler | 20148 bytes |
| Bank2: action adapter | 6737 bytes |
| Populated CODE total | 55442 bytes |
| Ordinary XDATA, including caller and libc | 2848 bytes |
| Status reservation | 64 bytes, eight used |
| Initial SP / full-run maximum / cap | 55 / 78 / 7C |

The virtual bank encoding is not additional physical flash. Physical DATA
reservations cover08..1D and23..4B; banking owns1E/1F, bits20..22,
OSEG4C..55 and stack56..7C. Actual relocated byte-liveness covers146
functions and634 live-caller-byte/callee-write comparisons, including libc
clobbers and cross-bank lifetimes.
No named `--dataseg` is treated as a physical allocation by itself.
XDATA1F00..1FFF remains the real IRAM alias. None of the original standalone
or complete-join resource budgets is relaxed.

Eighteen fresh-reset sequences execute2410 genuine calls and210988 MMIO
events. They cover ACK/AUTOACK and raw leases, five busy CCAs, all four
no-ACK attempts, wrong DSN/bad CRC, queued pre-stop and in-flight heads,
preserved closure watermark, a maximum frame across FIFO wrap, held clock/
overflow faults, SFD/filter/bracket faults, later-ACK uncertainty, cancellation/
resubmission, expired actions, work exhaustion, raw coarse rollover and
private/libc/status/alias storage rejection. This is not every possible
hardware fault or a run through billions of delivery-counter values.

Every native transcript is raw-byte pinned and must equal its sanitizer
counterpart. The image, raw CDB, map, memory account, all14 immediate listing
snapshots and relocatable objects are fully pinned. Acceptance includes
277229 complete CODE/address/metadata mutations, missing IRAM alias and live
FMAP/XBANK mapping controls, exact timebase/clock/MMIO instructions, unowned
RAM/SFR/XREG/flash guards and actual full-run stack peaks.

Replays use at most64 public calls per simulator subprocess with the unchanged
15-second limit. Continuation restores only a prior **fully observed actual**
CPU/RAM/peripheral/flash state at the common caller boundary, then verifies it
byte for byte before continuing. It never seeds controller progress. Lossless
binary observation reconstruction uses one pass over output rather than a
separate whole-transcript scan per dump; missing or duplicated dump boundaries
still fail. CI has two dedicated15-minute workers and uploads no adapter
image or reference.

This increment is **host-tested, image-checked and simulated**. Code commit
`3cc26b0` passed
[full Actions36187341754](https://github.com/faronov/cc2530-zigbee/actions/runs/36187341754),
**106/106 jobs**, including both complete adapter workers and all102 previous
workers. The new jobs took3m43s (generic) and2m58s (LG), each retaining all18
sequences,2410 calls,210988 MMIO events and277229 artifact mutations plus the
three mapping/alias rejection controls. No limit, older case or upload
exclusion changed. No hardware was accessed or RF test authorized.
The isolated2848-byte composition is not proof of adding these services to
the7512-byte complete join: real combined RAM/ABI integration is still needed.
