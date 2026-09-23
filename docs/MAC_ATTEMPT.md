# Bounded raw interval attempt

Implementation tracking: [#84](https://github.com/faronov/cc2530-zigbee/issues/84).

`CC2530_MAC_ATTEMPT` is an explicit alternative foreground composition, built
alongside `CC2530_MAC_RADIO`. It adds a real, single-submission prepare/attempt/
collect operation, not another board demo and not a `mac_tx` event adapter.
There is no declaration or successful fallback without the profile. Ordinary
and legacy MAC_RADIO CODE and behavior remain unchanged without the new flag.

The new service is **host-tested, image-checked and simulated**, not
hardware-observed, calibrated, interoperability-tested or PHY/MAC conformant.
No hardware/USB/RF operation is part of its build or tests.

## Ownership and public API

Use only `mac_attempt_init`, `prepare`, `run`, `receive`, `stop`, `resume` and
the read-only diagnostic accessor during this full-reset ownership epoch.
The header is `include/mac_attempt.h`; result values retain the existing
`mac_radio_result_t` meanings. There is no independent lower-service client,
IRQ/DMA/CSP work, sleep, debugger intervention or clock/timer reconfiguration.
Register readback cannot establish this ownership history.

`init(config, timeout, limit)` uses the actual clock -> Timer2 -> epoch ->
radio acquisition chain. Initial normal AUTOACK reception can transmit;
initialization is not passive. Before preparation, explicitly stop and drain
every already admitted frame/ACK until STOPPED. No receive is truncated.
`prepare(body, length, timeout, limit)` requires that stopped/empty state,
accepts 1..125 FCS-free bytes, loads and verifies the genuine TXFIFO while
idle, and reserves an immutable slot. It does not request reception or TX.
The caller body can be reused after successful preparation.

`run(window, timeout, limit, record)` submits that slot exactly once and
collects at most the first complete receive head. `window` is 1..4096 symbols
relative to the conservative TX lower end, not an ACK timeout or scheduled
CCA deadline. A window ending before an observed TX completion does not
fabricate completion: the operation still needs a fresh positive completion
predicate, within its work/time limits.

FRAME, BAD_CRC, EMPTY and CCA_BUSY publish a complete atomic receipt.
All errors preserve the public receipt. The record contains:

| Field | Meaning |
| --- | --- |
| `transmitted`, `tx_lower`, `tx_upper` | Fresh own TX completed; its actual serialized PHY end is inside the closed interval |
| `received`, `rx_upper`, `frame` | Original complete post-TX head, whose actual end is no later than this bound; raw body, RSSI and CRC/correlation are preserved |
| `within_window` | RX upper bound is no later than `tx_lower + window`; false leaves timing uncertain, not an exact late event |
| `armed`, `last` | Live normal-RX-arm and final validated samples, never captured events |
| `slot`, `length` | Owned immutable submission identity and original body size |

Timestamp fields requiring absent `transmitted`/`received` facts are zero.
Inactive received-body bytes are zero. Slot identifiers do not wrap: after
65535 preparations, another preparation is rejected until a new full-reset
epoch. A slot is not a delivered-frame or MAC sequence number.

RX remains requested after any ordinary result, in RX_MODE00 with AUTOCRC,
**filtering and AUTOACK disabled**, preserving the original ACK bytes as in
the existing idle-separated owner. The immutable TX slot and any incomplete,
queued or subsequently arriving RX head remain owned. Explicit receive/drain/
stop operations retire them; resume restores normal filtering/AUTOACK only
after STOPPED. Stopping a prepared but unsubmitted slot cancels it without
an RX request, reset or recovery flush. A later explicit prepare may replace
the idle TXFIFO; an operational fault never authorizes this.

All calls retain the positive Sleep Timer timeout `<0x800000` and positive
16-bit work cap. The attempt/consumer share one radio deadline and poll budget;
they do not reset the budget when copying a frame. Each Timer2 latch also has
a genuine bounded deadline/poll cap. Relative interval arithmetic additionally
rejects separations exceeding 65535 raw periods, using the actual unchanged
`mac_epoch` half-range/continuity checks.

The first operational fault is retained. In particular, invalid clock/profile,
lost submission/completion, FIFO overflow, exhausted work/time and late arming
are failures, not NO_ACK. Radio diagnostics distinguish
`RADIO_AUTOACK_ATTEMPT_TIMER_ERROR` and `RADIO_AUTOACK_ATTEMPT_LATE_ARM`.
RF, the timer and ownership may remain active on failure. There is no retry,
abort, reset, RX flush, recovery or implicit release.

## Critical order and interval proof

1. While truly idle, disable filtering/AUTOACK, select raw CCA1/threshold,
   prepare the immutable FIFO and clear/verify only the old TXDONE flag.
   Set RX_MODE11 before requesting RX.
2. Validate the complete genuine owner/time profile. Wait for RX readiness and
   RSSI_VALID, then establish a separate elapsed interval of at least eight
   symbols using real coherent Timer2 reads. Execute four real pre-strobe NOPs,
   then revalidate readiness, empty RXFIFO and the unchanged loaded TX tuple.
3. Read coherent live tuple `S`, issue one ISTXONCCA, and save SAMPLED_CCA
   before restoring symbol search. The four-NOP rule is **not** a fabricated
   post-strobe sampling delay. For an admitted attempt, require a positive
   own TX_ACTIVE state, not a zero flag or assumed calibration duration.
4. Restore/read back RX_MODE00 and latch `M`. With `L = body_length + 2`,
   define `TX_lower = S + (12 + 2*L)` symbols, retaining `S.fine` exactly.
   Require **`M < TX_lower`**. The positive transmit state and this inequality
   exclude both pre-submission reception and restoration after the earliest
   possible completion. Equality/late arm is terminal.
5. Latch `TX_upper` after the fresh own TXDONE predicate, without waiting for
   a globally idle RF controller. No other TX command or AUTOACK is admitted.
   TXFIFO contents stay immutable; full post-validation rejects RF errors.
6. Before any FIFO read or long copy, require FIFOP without overflow, legal
   PHR, sufficient count, and matching first-byte/first-packet pointers.
   Latch `RX_upper` only after that complete-head predicate. Reconfirm the
   pinned head/PHR, then use the same exact bounded consumer as ordinary RX:
   one destructive RFD read per byte, pinned advancing head, remaining-count
   and error checks, original PHR/body/status, and final full validation.
7. Finish complete radio/Timer2 validation, advance the actual epoch and
   project the staged samples in chronological order. Publish only then.

The serialized duration lower bound uses the fixed four-octet preamble,
one-octet SFD, one-octet PHR, two symbols/octet and generated two-octet FCS.
It does not add an assumed calibration/start delay or subtract a capture
offset. Both radio and Timer2 use the owned undivided XOSC32 clock. The raw
fine period remains 512 and coarse modulus remains **FFFFFF**, not 1000000.
The receive bound has causal post-TX ownership, not an invented tight receive
lower timestamp. RXPKTDONE is never used as FIFO-frame identity.

The scoped internal Timer2 reader checks ordinary storage and performs the
real common latch, whole-FF discard, range and time/work/fault path. Its
intermediate tuples are provisional; complete peripheral validation brackets
the finite hot region. It makes no MMIO writes. Existing init/read_live still
require physical radio quiescence; ordinary read_radio retains its full checks.

## Primary basis and remaining gates

Implementation is original BSD-3-Clause code; no SDK or external driver was
imported. Hardware facts are from TI **SWRU191F**, April 2009/revised April
2014, and **SWRZ031**:

- SWRU191F sections23.6/23.7.1, pp215-217; sections23.8.7-.10, p221;
  MDMCTRL0 p266: fixed PHY serialization, preamble and AUTOCRC.
- Sections23.8.1-.5, pp218-219: TX commands, retained FIFO, errors and
  turnaround; section23.8.11, p222: positive complete-frame TX indication.
- Section23.8.12, p222; section23.14, p238; FSMSTAT1 p263:
  immediate strobe semantics, SAMPLED_CCA updated by the issued strobe,
  RSSI readiness and the four-clock **pre-strobe** CCA-update requirement.
- CCACTRL0/1 and RSSISTAT, p264: CCA1, recommended raw threshold,
  hysteresis, eight-symbol RSSI averaging and validity.
- Section23.9.1, pp222-223; FRMCTRL0 p259; RXENABLE p260:
  soft stop, disabled symbol search, normal RX and AUTOACK selection.
- Section23.10, pp232-233; FIFO/status registers pp263-265:
  complete-head, count and pointer predicates and destructive FIFO access.
- Timer2, pp197-206; SWRZ031 issue1.2: common coherent latch and complete
  low-FF sample discard, not an event-capture or capture-valid mechanism.

References: [SWRU191F](https://www.ti.com/lit/ug/swru191f/swru191f.pdf),
[SWRZ031](https://www.ti.com/lit/er/swrz031/swrz031.pdf),
[SWRS081B](https://www.ti.com/lit/ds/swrs081b/swrs081b.pdf).

The new raw profile explicitly selects **energy-only CCA1**:
CCACTRL1=`0A` (mode1/hysteresis2), CCACTRL0=`F8` (signed threshold -8).
F8 is the manual's recommended raw value; choosing an unqualified lower
threshold could leave CCA permanently busy. It satisfies the register floor
`CCA_HYST - 128`. This is a new mode1 profile, not a relabeling of legacy
CCACTRL1=`1A` (mode3), which also uses F8.
Register selection, the independently observed >=8-symbol dwell,
and a normatively qualified energy threshold are three separate claims.
RSSI offset/sensitivity/board calibration and exact PHY procedure scheduling
remain physical/conformance gates. F8 is **not** asserted to meet a calibrated
ED threshold; RSSI_VALID plus four NOPs is **not** asserted to prove a newly
scheduled normative eight-symbol CCA.

IEEE2006 section6.9.9 permits a PHY implementing at least one of modes1/2/3.
Its threshold, detection and already-receiving-PPDU requirements still need
separate treatment. The owner's prior soft-stop/drain and pre-admission
RX_MODE11 prevent truncating an already admitted frame; they do not establish
complete PHY conformance. Reviewed R22 AnnexD does not add a CCA-mode
restriction, but references IEEE2015; complete revision reconciliation remains
outside this explicitly IEEE2006-compatible candidate.

Nominal turnaround12 + ordinary ACK delay/duration34 leaves eight symbols
inside an illustrative `TX_lower+54` boundary before software observation
uncertainty. This is feasibility arithmetic, **not** measured execution,
status latency, receiver processing delay, oscillator calibration or guaranteed
normal-exchange timing. The model's delays are synthetic stimuli. Runtime
interval inequalities, not those model constants, govern published certainty.

No legacy `captured` field receives a live timestamp. There is no ACK
classification, MAC confirmation/retry/IFS, continuous POLL lease or inferred
NO_DATA. Connecting these intervals to `mac_tx`/`mac_poll` requires a separate
explicit protocol API and proof; those sources are unchanged here.

## Build and evidence boundary

Compile both profile defines, with the normal SDCC4.2.0 large-model flags.
Link, in order:

```text
ma_timebase.rel ma_clock.rel ma_mac_time.rel ma_radio_autoack.rel
ma_mac_epoch.rel ma_mac_radio.rel ma_mac_attempt.rel ma_test_mac_attempt.rel
```

The image is `mac_attempt_test.ihx`. Immediately snapshot every module's
relocated listing as `mac_attempt_test.<module>.rst`. The native executable
is `host-mac-attempt-tests`; its nonrecovering ASan/UBSan variant adds
`-sanitize`. Its source dependencies include `tests/test_radio_autoack.c`,
whose stateful radio/FIFO model it extends. The complete component target is:

```sh
make BOARD=generic BUILD=build/mac-attempt test-mac-attempt
```

This runs the exact `tests/boot_mac_attempt.py` proof after both native
executables. CI schedules it once per board in separate 15-minute jobs,
outside the existing composed-services and debug-fixture jobs; the exact
aggregate remains `test-common`.

The composition excludes the complete lower prefix through
`mac_radio_shared_end`, wrapper prefix through `mac_radio_reserved_end`,
top-level prefix through `mac_attempt_reserved_end`, and the complete linked
memcpy/memset/gptr scratch suffix. Internal staging is genuinely allocated
above the lower boundaries, not a privileged private-pointer exception.
Caller buffers must be complete, disjoint, persistent allocated ordinary-XDATA
objects; unused address space is not an implicit pool.

The linked image uses **24621/32768 CODE**, **1475+64/2048 XDATA**, initial
SP59 and full-run SP79/7C. Ordinary allocation remains below1E00; the eight-byte
test record sits inside the unchanged 64-byte status reservation, and the
1F00..1FFF IRAM alias is never separate storage.

The proof binds complete CODE/raw CDB/map/memory, ordered instructions and
allocations, object identity, public/private ABI, genuine service calls,
actual MMIO/DPTR and unique destructive reads. Its replay checks caller
publication, unallocated/reserved memory, GPIO/SFR/XREG ownership, genuine
full-run stack usage, aliasing and the unchanged 15-second simulator limit.
Negative controls retain every emitted CODE-byte mutation and every relevant
F/S/L/T record mutation, plus map, listing, allocation, object and alias
rejections. The corpus has 28 sequences, 326 genuine linked calls and 148638
MMIO events, with 77713 artifact negatives and a missing-alias negative.
Native/nonrecovering sanitizer runs exercise 5983 real-service calls, including
private/scratch address sweeps. An independent 64-bit model checks containment
of its actual TX/RX events, exact late-arm equality, and coarse-counter wrap;
these synthetic events are not measurements. Listing serialization caches only
immutable input text, never verification results or mutable images.

Fresh legacy CODE remains 2999/7007/15040 for Timer2/radio/MAC_RADIO and
10758/10798 for generic/LG link fixtures, with their prior layouts and budgets.
Only genuinely affected raw debug/object identities are refreshed. Full
repository acceptance remains Actions; this component evidence does not
by itself establish a full repository or hardware acceptance result.
