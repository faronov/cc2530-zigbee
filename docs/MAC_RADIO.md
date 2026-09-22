# Co-owned live MAC clock and radio

The #80 `mac_radio` composition has one foreground owner of the real
`timebase`, `clock`, `mac_time`, `mac_epoch` and `radio_autoack` services.
It supplies live fractional time during the existing bounded radio phases,
not a MAC event adapter or a Zigbee connection.

## Explicit ownership profile

Only separately compiled `CC2530_MAC_RADIO` objects provide
`mac_time_read_radio`. It shares the genuine bounded latch/whole-FF discard
and atomic publication path, permitting known co-owned RX/calibration/TX
activity while retaining clock, IRQ, DMA, CSP, Timer2, reserved-bit and RF-error
checks. `mac_time_init` and `mac_time_read_live` still require radio quiescence;
a prior co-owned read does not relax either entrypoint.

This is a history contract: all services have one serialized foreground owner
since full SoC reset. Register samples cannot authorize adopting someone
else's running timer or radio. There is no independent lower-service access,
interrupt handler, DMA/CSP work, sleep or debugger intervention during the
epoch. Legacy standalone and board links do not select this profile; their
CODE remains byte-identical. Their changed raw debug identities account only
for added conditional source lines.

Initialization validates and copies the fixed channel11..26/raw-power05
configuration, selects undivided XOSC32 through the genuine clock service,
initializes untouched Timer2, binds its containing period to symbol0, acquires
the genuine receiver and samples again with RX active. Initial **AUTOACK can
transmit before initialization returns**. This is not passive receive.

Every public operation has a positive Sleep Timer timeout below800000 and a
positive poll limit **per underlying service call**, not a claimed aggregate
transaction deadline. Clock selection retains its existing bounded rollback.
The first operational fault retains its raw lower-service cause and ownership.
All later calls return it without MMIO; there is no implicit stop, retry,
reset, cleanup or recovery. Faults may leave RF and Timer2 active.

## API and publication

`mac_radio_now` returns an exact6-byte target `(symbols32, fine16)` live
coordinate. Raw periods wrap atFFFFFF, not1000000; fine remains0..511.
The caller guarantees uninterrupted ownership and strictly less thanFFFFFF00
real fine increments between accepted samples. Missing complete wraps cannot
be detected numerically.

Receive, stop, send and resume first accept a live sample, then invoke the
real radio operation. Sampling before radio work preserves caller-frame
atomicity: a later sampling failure cannot report an error after changing a
published frame. The14-byte diagnostic's `last_live` is the last accepted
sample, **not the instant of RX publication or physical TX/RX end**.

| Operation | Required phase and result |
| --- | --- |
| Receive | RX or DRAINING; FRAME, BAD_CRC or EMPTY; preserve original bytes and inactive tail. |
| Stop | RX or DRAINING; DRAIN requires explicit receive calls, STOPPED establishes OFF/empty. |
| Send | OFF/empty; one ordinary hardware-gated attempt; TX_DONE or CCA_BUSY leaves RX requested in the existing no-AUTOACK response phase. |
| Resume | OFF/empty; restore the original filtering/AUTOACK phase without reset or profile replacement. |
| Now | RX, DRAINING or OFF; does not invent an event or alter the radio phase. |

Invalid arguments, state or caller storage leave state, diagnostics and outputs
unchanged. Operational errors retain their cause but leave caller outputs
unchanged. These raw outcomes are not packet delivery, security acceptance,
ACK matching or membership.

## Linked storage boundaries

Objects link in this order:

```text
timebase -> clock -> mac_time -> radio_autoack -> mac_epoch -> mac_radio -> caller
```

The actual SDCC composition proves:

| Allocation | Ordinary XDATA bytes |
| --- | --- |
| Complete lower-service prefix | 0..437 |
| Shared-end marker | 438 |
| Wrapper raw/epoch/live/config staging and private/compiler state | 439..583 |
| Wrapper reserved-end marker | 584 |
| Caller objects and arguments | 585..745 |
| Complete memcpy/memset/generic-store runtime suffix | 746..757 |

Both lower services reject buffers through the shared marker; the wrapper
rejects buffers through its own final marker. All reject intersecting runtime
scratch and status/alias/out-of-range extents. The source declaration order
alone is not evidence: the proof binds all allocation records, actual marker
addresses and the complete runtime suffix. Upper layers contain no peripheral
instructions.

Current linked image: **15040/16384 CODE**, **758 ordinary XDATA +64 reserved
=822/1024**. Stack begins3C, initial3B; sampled/full-run peaks are53/55,
below the unchanged7C cap. These are separate composition resources, not
whole-stack/ZCL/interrupt-nesting headroom.

## Offline evidence

`make test-mac-radio` is in `test-common` once per board. It uses strict native
compilation, nonrecovering ASan/UBSan, the genuine linked image and alias-aware
uCsim replay with the unchanged15-second per-process deadline. Seven relocated
listings are snapshotted immediately after link. No board image, hardware
operator, dependency or automatic RF step is added.

Prepared evidence is **host-tested, image-checked and simulated**:
62,516 native calls (also with nonrecovering ASan/UBSan),266 genuine target
calls/79,308 MMIO events,48,143 artifact negatives and one missing-alias
negative. The CI matrix preserves its28 board/image jobs and adds two
dedicated MAC clock/radio jobs; the existing debug-fixture jobs run
`test-common-core`. The exact union remains `test-common`, with no duplicate
or omitted component and no raised15-minute job deadline.

The52 native/exported scenarios exercise initialization ordering, live time
with active RF and through stop/drain/send/rearm, CRC-good/bad/empty receive,
CCA busy, ordinary TX, clock/timer/radio faults, low-FF discards, work/time
exhaustion, counter/range errors, exact half-range/ambiguous epoch progress,
invalid storage and retained first faults. The synthetic radio model is reused
from the original owner corpus; the combined Timer2 model uses independent
64-bit counter arithmetic and real selector/latch/period behavior. Production
services are linked, never replaced by success mocks.

Four large accepted advances in the exported wrap scenario cross actual raw
wraps. The longer520-advance software-coordinate wrap and exhaustive native
private-prefix/runtime/status/alias address sweeps are **native-only** here;
the existing independent epoch corpus separately exercises software-coordinate
wrap on the target. Native and SDCC struct padding are not assumed identical:
diagnostics and live coordinates are serialized field by field.

The proof pins every CODE/runtime/constant byte, raw CDB before decoding,
complete map, all seven objects, ordered instructions and full
storage/entry/helper inventories. Replay checks actual MMIO PCs, dynamic DPTR
operands, destructive FIFO access and the four genuine CCA NOPs, return values,
complete diagnostics, caller inputs/outputs, unused XDATA/status tail, GPIO,
IRAM alias and stack unwind/high-water. Full acceptance is GitHub Actions,
not the act of preparing artifacts locally.

**Never flash `mac_radio_test.ihx`.** Evidence is host/image/simulator only;
there is no new hardware observation. Captured edge selection,
freshness/overwrite, event correlation, PHY offsets and event-time quantization
remain #40. Full continuous AUTOACK/POLL is still #50. The optional controlled
post-TX reply experiment #79 remains separate. Nothing here feeds live time
into `mac_tx` as captured TX/ACK end or claims Zigbee interoperability.

The subsequent [delayed raw-sample projection](MAC_STAMP.md) handles bounded
temporal placement only. It is deliberately not wired into this owner until
coherent captured-event identity is established; accepting a number inside a
window is not permission to call it the timestamp of a received frame.
