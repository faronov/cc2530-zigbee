# Isolated passive RX foundation

`include/radio_rx.h` / `src/radio_rx.c` implements one bounded foreground
receive operation, separately from the quiescent FIFO service and board GPIO.
The corrected implementation is **host-tested, image-checked and alias-aware
simulated**, with separate
[bounded LG hardware acceptance](DEBUGGING.md#2026-09-18-lg-bounded-passive-rx-acceptance).
A
[parent-observed failure/probe on the previous LG image](DEBUGGING.md#2026-09-18-lg-rx-fscal1-failure-and-probe)
identified the FSCAL1 guard correction below; it was not successful reception.
The separate `IMAGE=radio_rx_fixture` now calls it on both boards, as described
below. `radio_rx_test.ihx` remains an isolated synthetic executable:
**NEVER flash it or publish it as board firmware**.

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

FSCAL1 (`61AE`) is still written as the whole byte `00`, per **SWRU191F,
revised April2014, section23.15.1/Table23-6**. Its p.267 register table
defines VCO_CURR bits1:0 as R/W and reserved bits7:2 as **R/W0**, reset
`001010`, not R0. Section23.15.2/Table23-7 distinguishes write-zero-only from
read-as-zero. Readback therefore checks `(FSCAL1 & 03) == 00`, during
configuration and every later observation. All other nine settings remain
full-byte comparisons. Nonzero low bits still latch STATE_CHANGED without
publication. This is not FSCAL2 (`61AF`), the separate capacitor-calibration
result, and adds no retry, acknowledgment, cleanup or calibration algorithm.

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

## Channel, reset and metadata interpretation

This is the supported **bounded contract**, not calibrated PHY acceptance.
Primary references below are [TI SWRU191F](https://www.ti.com/lit/ug/swru191f/swru191f.pdf),
April2009/revised April2014, and the CC2530-specific
[SWRS081B data sheet](https://www.ti.com/lit/ds/swrs081b/swrs081b.pdf),
April2009/revised February2011; no other CC253x part's offset is substituted.

- **Channel selection:** SWRU191F section23.5 p.215 and FREQCTRL p.261
  specify nominal `2394 + FREQ[6:0]` MHz. The API restricts this to channels
  11..26, nominal2405..2480MHz in5-MHz steps, using the formula above.
  Channel15 therefore selects FREQ`1F`, nominal2425MHz; that is not a measured
  carrier frequency. Register changes take effect at the next recalibration.
  Each call configures while idle and then issues E3, rather than retuning an
  active reception. Only channel15 on the recorded LG image has physical
  evidence; other channels have argument/configuration tests, not RF acceptance.
- **Reset versus calibration:** section23.9.1 pp.222-223 describes SRXON's
  transition to RX calibration and soft disable's completion of the current
  reception. The service uses this hardware calibration path and Table23-6's
  recommended settings, then checks PLL lock and RSSI validity. These are
  readiness checks, not board frequency/RSSI calibration. `receive_init` does
  not perform a full reset. E3, soft-off and ED FIFO flush do not establish
  the required reset/exclusive-history precondition or cancel unknown scheduled
  work. Only completed OK/BAD_CRC calls permit normal reuse; a latched fault
  requires separately established full-reset recovery, not another strobe or
  a cleared C latch. No in-place radio reset/recovery API is implemented.
- **RSSI:** section23.9.7/Figure23-15 p.230 specifies that the appended value
  measures the first eight symbols **after SFD**, not the entire frame or a
  contemporaneous host sample. Sections23.10.3-4 pp.233-234 and RSSI/RSSISTAT
  p.264 define signed two's-complement, logarithmic1-dB steps. The API preserves
  the byte in `uint8_t`; interpret values128..255 as value minus256 for a signed
  raw reading, not as positive power. Startup RSSI_VALID is a readiness flag,
  not a precision estimate. SWRS081B p.9 gives RSSI/CCA characteristics under
  stated TI EM reference-design conditions; neither its nominal offset nor
  the manual's conversion example is a calibration of either repository board.
  No offset, dBm conversion, uncertainty bound or energy-scan result is supplied
  by this API or established by the recorded run.
- **Correlation and CRC:** the same footer carries CRC_OK in bit7 and unsigned
  correlation in bits6:0, averaged over those first eight post-SFD symbols.
  Section23.10.4 p.234 explicitly says hardware does not directly supply IEEE
  LQI; conversion needs an empirically justified mapping based on packet-error
  measurements. No such mapping, PER calibration or LQI0..255 API exists here.
  Merely doubling/clamping correlation or combining it with raw RSSI does not
  establish LQI. CRC_OK remains hardware FCS status, not signal calibration,
  independent on-air FCS verification, valid MAC syntax or authentication.

Tests of every raw-byte value establish preservation/decoding, not physical
accuracy or a new footer validity filter. The
[CC2530 errata SWRZ031](https://www.ti.com/lit/er/swrz031/swrz031.pdf),
April2009, issues1/2 concern DMA variable length and Timer2 read latching.
This polling RX uses neither; those errata provide no missing metadata
calibration or broader hardware-acceptance evidence.

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
driver and real timebase run in **151,177 strict host cases**, including
all channels, all legal lengths, every RSSI/correlation byte, CRC rejection,
backlog discard, unchanged tails, both object-address sweeps, readback and
entry-state failures, exact limits/deadlines, rollover, helper ambiguity,
delayed phases and retained-fault/BAD_CRC reuse. The host model consumes the
shared 32-entry logs; every write must match the passive-only whitelist.
The FSCAL1 additions cover all64 reserved-bit patterns at configuration,
after E3 and with genuine successful-call reuse. All three nonzero low-bit
values across all64 upper-bit patterns fail both configuration readback and
post-E3 observation, with unchanged output and retained fault. Each bit of
the other nine configuration bytes is also corrupted and rejected.

**33 linked scenarios** replay deterministic host-model peripheral events
through the actual SDCC instructions, not a replacement service or patched
CODE. Native assertions independently constrain publication, FIFO order and
permitted writes; replay additionally checks instruction order, every actual
MOVX address/value, explicitly serialized diagnostics, output/tails,
retained faults, unrelated peripheral state and memory guards.
This is a shared synthetic model, not an independent silicon model.

The complete 5,214-byte linked image, including runtime, caller and CODE
configuration tables, is pinned; every CODE byte is mutation-tested.
SFR operations, the two single-read RFD sites, fixed and table-indexed XREG
sites, CDB layouts, pointer ABI, listing and ownership are checked separately.
SDCC 4.2.0 duplicated a destructive RFD read for a draft chained assignment;
the final source uses a single destination and the linked check rejects an
extra read even when host behavior would appear correct.
The FSCAL1 instruction check separately verifies index8/mask03, maskFF for
other indices, bank0 `ANL AR0,A` (opcode52, not an SFR access), expected
CODE/frequency values and the full comparison. Its bytes are mutation-tested
independently of the whole-image hash. Explicit traces require confirmed
FSCAL1 `00` before E3, then readback `30` or `FC` and successful synthetic
stop/drain/flush, while `31/32/33` cause terminal nonpublication. These expected
values/results are checked separately from shared host-trace replay.

The corrected standalone image SHA256 is
`a69606bca743a8e660d824d920211dfdf9b3467f175f251e117c5c01539dd41e`.
The correction adds25 CODE bytes and no allocated RAM or C/wire ABI changes.

The isolated image uses 375 ordinary XDATA bytes plus the existing 64-byte
status reservation, within the unchanged 512-byte component budget.
Stack starts at `34`; the observed MMIO-stop peak is `46`, not a worst-case
stack proof. The full upper-IRAM `80..FF` sentinel, XDATA/IRAM alias,
unallocated XDATA and final unwind are checked. The result at `1E00` is the
eight-byte synthetic ABI `RXO1 01 08 00 00`.
Each simulator process retains the existing **15-second** timeout.

Before this correction, the generic and LG full offline suites passed.
Focused correction checks are described here; no repeated full matrix or
new hosted-CI pass is claimed. All 18 older board images
were rebuilt and image-checked with unchanged complete BIN sizes/SHA256.
The board verifier explicitly rejects the RX driver in those older images
and the standalone harness in **every** board image, including source-record
and symbol injection tests.

## Bounded passive RX board fixture

`IMAGE=radio_rx_fixture` uses the actual startup, board policy, clock selector
and real passive driver. It is **RF-capable RX-only**, unlike the nine
older non-RF images. The corrected LG image has the bounded physical evidence
below; generic remains host/image/simulator-only. The earlier LG failure/probe
is recorded separately, not relabeled as success.
No successful service substitute or physical peripheral model is
linked. The LG display remains off under the existing board policy.

After bootstrap the BEFORE checkpoint exposes INIT. The next step calls
`clock_select_init(XOSC32,1024,4096,...)` and requires confirmed `88/88`,
awake state and IRQ ownership before CLOCK READY. Each subsequent step
fills the persistent frame object with `A5`, increments attempt and calls
`radio_rx_receive_init(15,65536,65535,...)`. There are at most **16 attempts**,
each separated by READY. `OK`, with intact CPU ownership, increments completed
and M0 heartbeat; `BAD_CRC` leaves the full frame `A5` and permits the next
attempt. The step after the sixteenth READY enters terminal END without RX.
These are raw ticks/polls, not calibrated seconds or a continuous receiver.

Every other driver result is terminal FAULT with the original result/latch
retained. No fixture cleanup, RF-off, retry, flag clear, reset or continuation
follows a failure. The clock selector retains its existing bounded rollback
contract on clock failure; that is not RX recovery. A timeout can leave RX
**active even while the CPU is halted**. Recovery requires a separately
authorized full-reset/full-CODE-verified invocation.

### Wire v1 and linked ABI

All multi-byte wire fields are little-endian. Native C diagnostics are
explicitly serialized, never copied using host structure padding.

| Offset | Size | Meaning |
| --- | --- | --- |
| 0 | 6 | `M2RX`, version1, size96 |
| 6 | 3 | phase, reason, stage |
| 9 | 5 | attempt, completed, RX result, fault latch, clock result |
| 14 | 8 | channel15, maximum16, timeout32=65536, limit16=65535 |
| 22 | 19 | existing clock diagnostic wire layout |
| 41 | 31 | RX diagnostics in the layout above |
| 72 | 7 | CMD, STA, SLEEP, IEN0/1/2, initial SLEEP |
| 79 | 7 | initial IP0, IP1, TCON, S0CON, S1CON, IRCON2, IRCON |
| 86 | 7 | current flags in that order |
| 93 | 3 | guards `69 96 C7` |

Phases: INIT1/RUNNING2/READY3/FAULT4/END5; the decoder rejects RUNNING.
Stages: CLOCK0/RX1/END2. Reasons: none0/entry1/phase2/clock3/RX4/invariant5.
RX result255 means unattempted; clock result8 means unattempted. C snapshots
are sequential, not atomic. Only IRCON.STIF `0->1` growth is accepted across
snapshots; `1->0` and other CPU flag/control changes are rejected. RFIRQF0/1
belong to the driver's raw diagnostic rules, not a byte-identical flag rule.

| Allocation | Address / bytes (both boards) |
| --- | --- |
| Ordinary XDATA | `0000..0219`, 538 |
| Driver fault / private-prefix last byte | `0045` / `00FA` |
| `radio_rx_fixture_state` | `00FB`, 96 |
| `radio_rx_fixture_frame` | `015B`, 128 (length/RSSI/correlation/body125) |
| `radio_rx_fixture_diagnostics` | `01DB`, 31 |
| `radio_rx_fixture_clock` | `01FA`, 19 |
| Generic-store scratch | `0219`, excluded from caller objects |
| M0 used / reservation | `1E00`, 32 / 64 |
| IRAM stack start / reserved | `61` / 159 (SP initial `60`) |
| Observed MMIO-stop peak / checkpoint SP | `71` / `62` |

The actual `.mem` occupancy below stack is8 register-bank bytes,66 DATA bytes,
one bit-storage byte (five bits used),19 overlay bytes and3 unused bytes.
Do not sum overlapping linker DSEG declarations as additional physical RAM.

Timebase, clock and RX objects are linked **before** the caller objects.
Total nonaliased reservation is602 bytes. Only this fixture gets an enforced
1024-byte budget; every older image and standalone RX retains512. CODE stays
below `8000`; XDATA stays below `1E00`. `1F00..1FFF` remains an IRAM alias,
not additional storage. The entire upper IRAM `80..FF` stays `C7` in simulation;
the observed peak is not a worst-case stack proof.

| Board | CODE/BIN | BEFORE / READY / FAULT / END |
| --- | --- | --- |
| generic | 9120 | `0140 / 0142 / 0144 / 0147` |
| lg_esl29_rev03 | 9160 | `0168 / 016A / 016C / 016F` |

Complete BIN SHA256:

```text
generic         473758bc8bf8e9bf1b503117e254a907ac23918a34bf006bbb62c4e3da63a9e7
lg_esl29_rev03   0e31578a708d9c8d4556caec33f062fd82baf7498ebef1af3b708f868ab6c8d2
```

SDCC emits both an unused out-of-line step and the live inlined main copy.
The live copy avoids an extra return frame. Every complete CODE byte is
pinned and mutation-tested; independent instruction/MMIO/CDB/listing checks
cover the real calls, CODE tables, two destructive RFD read sites, fixed and
indexed MMIO addresses, XDATA pointer ABI and caller/helper exclusion.
SDCC overwrites the shared module's `radio_rx.rst` on every link. Each RX
link therefore immediately saves its own `<image>.radio_rx.rst`; the strict
checkers consume only that image-specific listing, including when `all test`
links both images in one directory. Missing, wrong-image, reordered or
duplicated instruction listings fail. These test-only files do not expand
the seven-artifact upload whitelist.

Reviewed instruction sites in the corrected images (hex CODE addresses):

| Site | Standalone test | Generic fixture | LG fixture |
| --- | --- | --- | --- |
| Indexed configuration MOVX read | `03A1` | `0BA9` | `0BD1` |
| Index8 mask selection / ANL AR0,A | `03A4 / 03C5` | `0BAC / 0BCD` | `0BD4 / 0BF5` |
| Table comparison / STATE_CHANGED return | `03DB / 03E0` | `0BE3 / 0BE8` | `0C0B / 0C10` |
| Indexed configuration MOVX write | `0C0B` | `1413` | `143B` |
| E3 / ED | `0D07 / 120E` | `150F / 1A16` | `1537 / 1A3E` |
| Single PHR RFD read / body-loop RFD read | `0F46 / 1098` | `174E / 18A0` | `1776 / 18C8` |

The old physical probe at `0BF7` belongs only to the previous LG hash.
It is not the failure-return site in this corrected image.

### Focused offline checks

Run the two simulator jobs **serially**:

```sh
make BOARD=generic IMAGE=radio_rx_fixture test-radio-rx-fixture
make BOARD=lg_esl29_rev03 IMAGE=radio_rx_fixture test-radio-rx-fixture
PYTHONPATH=tools python3 -B -m unittest test_radio_rx_fixture test_m0_artifacts test_timebase_fixture -q
```

Host tests run real bootstrap/clock/RX, reusing the original strict synthetic
RX backend without changing its32-entry log bounds.
Both boards pass1,006 host checkpoints and67 linked checkpoint traces.
Coverage includes min/max bodies and raw footer/tails, BAD_CRC/reuse,
16-attempt/END bound, exact timeout and full65535 stopped-counter cap,
entry/readback/controller/overflow errors, clock failure, CPU flag rejection
and retained terminal state. Linked scenarios replay that **shared host
model**, not an independent RF oracle, through actual board instructions
with explicit peripheral effects and no ROM/driver-return patch.
Both board host models also exhaust all64 FSCAL1 upper-bit patterns with all
four low-bit values, and retain each failure without publication.
The long full-cap case uses33 segments of genuine CPU/RAM continuation, never
changed poll arguments/counters, including synthetic FSCAL1 `30` after E3.
Each s51 process retains15 seconds.
The C52-only SBUF at99 stays at its verified zero reset byte during restore:
writing it would start a fictitious UART transfer. Every restored SFR byte,
including99, is compared before continuation. This is not CC2530 UART/RF
behavior. Alias, unallocated RAM, upper-IRAM and stack-unwind checks remain.

`tools/radio_rx_fixture.py` is read-only and returns no raw body.
`debug_image.py radio-rx-state` decodes the96-byte snapshot;
`radio-rx-checkpoints` verifies artifacts and reports linked proof.
The separate [manual runner](DEBUGGING.md#parent-only-passive-rx-acceptance)
is never invoked by a build/test/import/CI. The RX addition brought CI to20
jobs; the [current matrix](../.github/workflows/ci.yml) retains the seven
board-artifact paths and `hardware_tested=false`.
No host executable, synthetic vector, capture or standalone image is uploaded.

## Physical evidence and remaining gates

The [2026-09-18 LG record](DEBUGGING.md#2026-09-18-lg-bounded-passive-rx-acceptance)
uses the exact9,160-byte board image above, with full physical readback before
execution. One initial69-byte body matched the concurrent Nordic capture.
A separate pre-configuration deadline hold produced retained TIMEOUT10,
without configuration/RF actions or publication. Explicit reset/CODE-verified
recovery then completed16 attempts:14 CRC_OK bodies each matched one reference
record, while attempts4/16 returned BAD_CRC with the entire output still `A5`.
Subsequent success after attempt4 establishes bounded BAD_CRC reuse, not an
automatic reset. The next step reached retained END016F without a17th receive;
RX was disabled, both FIFOs empty and heartbeat14.

The [2026-09-19 connected-board revalidation](DEBUGGING.md#2026-09-19-connected-lg-rx-revalidation)
rechecked every byte of the same halted image, then separately reset and ran16
attempts:15 CRC_OK/one BAD_CRC. The cap and a terminal-loop pass retained
END016F with attempt16/completed15/heartbeat15 and unchanged frame/bootstrap/
CPU context. This later run did not repeat independent sniffer comparison.
The subsequent [read-only backup activity](DEBUGGING.md#2026-09-19-guarded-private-backup-and-sniffer-preparation)
separately reset and rechecked the unchanged firmware, leaving it halted at
PC0000/config26 without another application resume. END016F above is the
historical endpoint of the RX run, not the latest physical state.

These are finite channel15 observations on one LG board. The reference tool
omits the final two serial-frame octets, which are not independently established
as literal FCS; body equality is not independent on-air CRC verification.
Generic hardware, other channels, calibrated timing/RSSI/LQI, controller
overflow recovery and stopped-clock/poll-cap behavior remain unobserved on
silicon. No lossless/continuous reception, MAC syntax/security/network
acceptance, TX, autoACK, RF ISR or DMA is established. Physical CODE checking,
private captures, explicit recovery and the M2/M3 gates remain required.
No hardware is accessed by tests/CI; their artifacts remain
`hardware_tested=false` with the same seven-file whitelist.
