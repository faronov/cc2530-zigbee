# Isolated passive RX foundation

`include/radio_rx.h` / `src/radio_rx.c` implements one bounded foreground
receive operation, separately from the quiescent FIFO service and board GPIO.
It is **host-tested, image-checked and alias-aware simulated only**.
No board image calls it yet. `radio_rx_test.ihx` is an isolated synthetic
executable: **NEVER flash it or publish it as board firmware**.

## Contract

`radio_rx_receive_init(channel, timeout, limit, output, diagnostics)` accepts
channels 11..26, a deadline of 1..7FFFFF raw 24-bit ticks and a positive
16-bit poll cap. Both output objects must be complete, disjoint persistent
XDATA objects below `1E00`, after the linked driver/timebase private prefix
and outside generic-store helper scratch. Link timebase, RX, then caller
objects. The service is not reentrant or ISR-safe.

The caller must establish a genuine full-reset/exclusive-ownership history:
awake undivided XOSC32, no active or scheduled DMA/CSP/RF work, all interrupt
enables and RF masks zero, standard modem configuration, reset-empty FIFOs
and no controller error. Readbacks cannot prove the absence of scheduled work
or replace knowledge of that history. This polling service never accesses
DMA registers; it does not grant debug-DMA permissions.

The receiver writes/readbacks ten fixed settings: normal RX/TX framing with
AUTOCRC on and AUTOACK off, filtering/source matching disabled, FIFOP
threshold 127, the default 192-us RX-to-RX interval, TI's AGCCTRL1,
TXFILTCFG and FSCAL1 recommendations, and
`FREQCTRL = 11 + 5 * (channel - 11)`. Both sides of every configuration write
must still observe an idle/reset-empty radio.

`RFST=E3` enables/calibrates reception. The service waits for RXENABLE80,
PLL lock, RSSI validity and then a complete-frame FIFOP indication.
`RXMASKCLR=80` requests **soft** shutdown, allowing a current reception to
finish. Only after idle is confirmed does it read RFD: one PHR byte and
exactly PHR further bytes, checking the FIFO count decrement after every
read. PHR must be 3..127. With AUTOCRC, the last two bytes are substituted
RSSI and CRC/correlation metadata, not the original FCS.

An `ED` flush after verified idle explicitly discards any additional queued
bytes and must confirm reset-empty FIFO state. This is **not continuous,
lossless reception**. No TX/ACK strobe, TXFIFO write, hard RF-off, flag clear,
DMA, GPIO or address/source-match RAM access is implemented.

`OK` publishes one 1..125-byte FCS-free body, raw RSSI and seven-bit
correlation. The remaining body tail stays unchanged. Raw RSSI is two's
complement, **not calibrated dBm**; correlation is **not IEEE LQI**.
CRC_OK is required but is neither authentication nor MAC syntax validation.
`BAD_CRC` completes shutdown/flush, preserves output and permits another call.
No network membership, security admission or transmission permission follows
from either result.

## Failure and diagnostic ABI

Invalid argument/range/ownership results preserve both output objects and
perform no MMIO. Other errors preserve the frame and retain the **original**
result in a fault latch. Further calls return that result without MMIO or
diagnostic writes, even with otherwise invalid arguments. Only a genuinely
established full reset recovers a fault; clearing a C variable is not recovery.

A failure may leave configuration partial or unconfirmed and reception
**still active**. No guaranteed-quiescent error return or automatic cleanup
is claimed. The one deadline/poll cap covers configuration, reception,
soft-stop, drain and flush. No write/read action is started without a
remaining confirmation poll. A last available successful decision poll is
permitted; publication then uses a bounded CPU copy and is not atomic.
The existing timebase half-range/no-reset/no-missed-wrap and executing-CPU
assumptions apply; ticks and loop counts are not measured elapsed seconds.

The target frame ABI is 128 bytes: length/RSSI/correlation at offsets 0/1/2
and body[125] at 3. Target diagnostics are 31 bytes:

| Offsets | Contents |
| --- | --- |
| 0..5 | Little-endian elapsed ticks (4), polls (2) |
| 6..11 | Timebase result, phase, configuration writes, verified writes, actions, complete-sample indicator |
| 12..21 | RX enable, FSM0, FIFO/state signals, RX/TX counts, RX first/last/packet pointers, TX first/last pointers |
| 22..25 | Raw error flags, RFIRQF0, RFIRQF1, RSSI-valid status |
| 26..30 | Consumed-byte count, PHR, raw RSSI, CRC/correlation byte, discarded queued-byte count |

Phases are entry0, configuration1, RX startup2, frame wait3, soft-stop4,
drain5, flush6 and publication7. Action bits 0/1/2 record E3/RXMASKCLR80/ED
issuance, not successful completion. A diagnostic sample is sequential,
not atomic; `sample_valid=0` means early state rejection prevented a complete
sample. Existing source-match flags may remain diagnostic metadata; the
source-matching configuration itself is disabled and verified. TXDONE,
TXACKDONE and CSP execution/interrupt flags are never accepted.
RXP1_PTR is eight bits; only RXFIRST_PTR/RXLAST_PTR reserve bit7.

## Offline evidence

```sh
make test-radio-rx
```

The target is also included in ordinary `make ... all test`. The same C
driver and real timebase run in **150,593 strict host cases**, including
all channels, all legal lengths, every RSSI/correlation byte, CRC rejection,
backlog discard, unchanged tails, both object-address sweeps, readback and
entry-state failures, exact limits/deadlines, rollover, helper ambiguity,
delayed phases and retained-fault/BAD_CRC reuse. The host model consumes the
shared 32-entry logs; every write must match the passive-only whitelist.

**27 linked scenarios** replay deterministic host-model peripheral events
through the actual SDCC instructions, not a replacement service or patched
CODE. Native assertions independently constrain publication, FIFO order and
permitted writes; replay additionally checks instruction order, every actual
MOVX address/value, explicitly serialized diagnostics, output/tails,
retained faults, unrelated peripheral state and memory guards.
This is a shared synthetic model, not an independent silicon model.

The complete 5,189-byte linked image, including runtime, caller and CODE
configuration tables, is pinned; every CODE byte is mutation-tested.
SFR operations, the two single-read RFD sites, fixed and table-indexed XREG
sites, CDB layouts, pointer ABI, listing and ownership are checked separately.
SDCC 4.2.0 duplicated a destructive RFD read for a draft chained assignment;
the final source uses a single destination and the linked check rejects an
extra read even when host behavior would appear correct.

The isolated image uses 375 ordinary XDATA bytes plus the existing 64-byte
status reservation, within the unchanged 512-byte component budget.
Stack starts at `34`; the observed MMIO-stop peak is `46`, not a worst-case
stack proof. The full upper-IRAM `80..FF` sentinel, XDATA/IRAM alias,
unallocated XDATA and final unwind are checked. The result at `1E00` is the
eight-byte synthetic ABI `RXO1 01 08 00 00`.
Each simulator process retains the existing **15-second** timeout.

The generic and LG full offline suites passed. All 18 existing board images
were rebuilt and image-checked with unchanged complete BIN sizes/SHA256.
The board verifier explicitly rejects the RX driver and standalone harness
in those images, including source-record and symbol injection tests.

## Remaining physical gate

A separately integrated, checked board fixture and explicitly authorized
programming/recovery procedure are still required. Physical CODE must be
verified before execution. Channel-15 reception must then be compared with
an independent sniffer using private captures, with errors and any separate
full-reset recovery reported honestly. No LG RX, calibration, FCS or on-air
acceptance is established by these offline results. Existing board firmware,
debugger permissions, CI artifact whitelist and M2/M3 exit gates are unchanged.
