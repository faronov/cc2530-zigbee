# Awake MAC Timer live-counter foundation (#39)

Original BSD-3-Clause, **offline preparation only**. This is Timer 2
initialization and a coherent **live** counter snapshot, not a MAC adapter,
captured TX/ACK end, calibrated time, scan, or a unified radio owner.
There is no board `IMAGE` or hardware runner. **Never flash or upload the
standalone `mac_time_test.ihx` as a board artifact.**

## Primary facts and deliberately fixed profile

The implementation is [mac_time.c](../src/mac_time.c), with its full contract
in [mac_time.h](../include/mac_time.h). The following are functional facts
from the primary documents, not SDK code:

| Primary location | Fact and decision |
| --- | --- |
| TI **SWRU191F**, April2009/revised April2014, section4.4 pp66-69, section4.5 p69 and Ch22 p197 | Timer 2 runs according to the system clock; TICKSPD prescales Timers1/3/4, not Timer2. Require stable **undivided XOSC32**, matching CLKCONCMD/STA and unchanged command fields; do not select a clock. Nominal frequency is not measured/calibrated frequency. |
| Sections22.1.1-3 p198; Table22-1 p203; T2CTRL p206 | Reset leaves the timer idle, counters zero, CTRL02 (SYNC=1). Require untouched reset history and these observable conditions. Do not stop/adopt a running timer or rewrite either counter. |
| Section22.4 pp200-202 | A first synchronous start without a prior synchronous stop loads unpredictable values. Set **SYNC=0** before the first RUN. No sleep compensation, synchronous stop/start, 32-kHz-edge synchronization or 86-cycle restart calculation is implemented. |
| T2CTRL p206 | RUN bit0 reads the request, STATE bit2 reports running. Write CTRL08 for common latching while idle; later write CTRL09 and require STATE, reading **0D**, not just echoed RUN. |
| Sections22.1.3/.8 pp198-199; T2IRQF p204 | A would-be count equal to its programmed period is replaced by zero. Set positive fine period **0200** and overflow period **FFFFFF**; use no assumed special encoding for period zero. |
| Section22.5 pp203-204 | MSEL22 selects both period registers. Write low byte first; overflow writes commit on the high byte. Verify all five period bytes, then restore/read back MSEL00. |
| Section22.3 p200; T2EVTCFG p206 | Timer events can feed radio/CSP/DMA. Before RUN, program and verify **EVTCFG77**, disabling both CC253x event outputs. Do not use CC2541's four-bit event selectors. |
| Section22.2 pp199-200, T2IRQF/IRQM pp204-205 | Six masked flags can still become set. Require IRQM00, preserve flags, accept only the low six bits after start. Flags are neither event counts nor an extended epoch. No IRQ mask/flag write or ISR is introduced. |
| Sections22.1.2/.6 pp198-199; T2M0/1, T2MOVF0/1/2 and CTRL pp203-206 | With live selectors00 and LATCH_MODE=1, **one T2M0 read** supplies fine-low and latches fine-high plus all three overflow bytes together. T2MOVF0 then reads the existing latch; it must not trigger an independent overflow sample. |
| TI **SWRZ031**, April2009/history2009-04-29, section1.2 pp2-3 | At a low-byte FF rollover, upper bytes can latch one clock late. Save T2M0 once; **discard the entire sample when it is FF**, then perform a new bounded attempt. Never combine an old low byte with a second read's upper bytes. LATCH_MODE=1 avoids the separate MOVF0 overflow-latch erratum. |
| TI **CC2530 SWRS081B**, April2009/revised February2011, pp20-21 | Confirms CC2530/F256 CPU/memory and the configurable MAC timer/24-bit overflow counter. Its high-level end-capture claim is not sufficient to define the missing register-level capture API; see below. |

The ten added SFR declarations are CTRL94, MSELC3, M0A2, M1A3,
MOVF0/1/2 A4/A5/A6, IRQFA1, IRQMA7 and EVTCFG9C.
The existing register-list macro supplies native bindings automatically.
No existing register address, MMIO semantics, host-log size or host binding
implementation was changed.

### Raw units, range and rollover

`mac_time_stamp_t` contains `uint16_t fine` and `uint32_t periods`:

- Fine is a system-clock count in **0..511** within a timer period.
- Periods is the zero-extended hardware overflow count in **0..FFFFFE**.
  Each unit accounts for **512** fine increments; the next period wraps to zero.
- Samples with fine-low FF are conservatively discarded, so this reader does
  not publish fine255 or511, even when a particular FF read was not erroneous.
- At *nominal* 32 MHz, a fine increment is 31.25 ns and a period is 16 us.
  The complete configured tuple repeats after 8,589,934,080 increments,
  nominally 268.43544 seconds. This is **not** a power-of-two 24-bit coarse
  modulus, a measured rate, or a retained multiwrap/software epoch.

The tuple belongs to the accepted **T2M0 read instant**, not function return.
The counter continues during subsequent byte reads and validation; its captured
latch does not. There is no rounding, elapsed-time helper, epoch-extension API,
or conversion to [mac_tx](MAC_TX.md)'s abstract modulo-`2^32` symbol domain
inside this driver. The separate [fractional epoch arithmetic](MAC_EPOCH.md)
extends coherent raw tuples without dropping fine; it is not an event capture
or a direct integer-timestamp bridge.
Matching nominal 16-us periods does not align the timer's origin/fine phase
with a PHY event, preserve a multiwrap epoch, or justify dropping fine bits.

## Ownership and API behavior

One foreground owner, non-reentrant, normal SDCC model-large bank0/DPS0 ABI:

- Awake, no PM entry/exit, clock discontinuity, unconfirmed request or prior
  Timer 2 use since a separately established full SoC reset.
- Stable undivided XOSC32 and exclusive clock/Sleep Timer reads throughout;
  both raw-time and CPU-progress assumptions must hold.
  Matching CLKCONCMD/STA does not prove LF-crystal stability, completed RC
  calibration or reset history (section4.4.3 p67); those remain caller preconditions.
  SLEEPCMD's mode/reserved-bit check follows its p64 register definition.
- All IEN bytes and Timer2/RF masks zero. Radio/CSP/DMA must be quiescent,
  **with no pending/scheduled work**, not merely appear idle in one sample.
  Chip/clock/power/enables/DMA/radio/CSP and owned timer fields are checked.
- No other timer selector, counter, period, compare, delta, latch, capture,
  ISR, DMA or debugger access throughout the epoch. The period and latch
  configuration become immutable. A foreign writer that changes/restores
  fields, a reset resembling progress, or missed wraps cannot always be
  detected. Historical ownership remains a real caller precondition.

This foundation deliberately also requires radio quiescence for live reads.
It **cannot** be silently chained with the reset-exclusive RX/TX services into
a real MAC adapter. A unified bidirectional owner and its interrupt/timestamp
contract require a separately reviewed change.

| API | Supported behavior |
| --- | --- |
| `mac_time_init(timeout, poll_limit)` | One-shot, bounded event/latch/period configuration, asynchronous RUN/STATE confirmation and one coherent range-checked read. Does not publish that initialization sample as caller time. |
| `mac_time_read_live(timeout, poll_limit, output)` | Read-only MMIO after successful init. Stage a coherent tuple privately and publish all six target bytes only after state, range and deadline validation. |
| `mac_time_diagnostic()` | Read-only private diagnostic pointer; no MMIO. COLD at startup, PENDING during an operation. Partial observations are not an atomic hardware snapshot or a captured event. |

State errors and argument errors are explicit:

- Before init, live read returns NOT_INITIALIZED without MMIO.
- Repeated successful init returns ALREADY_INITIALIZED without MMIO or restart.
  It is not idempotent hardware verification or success.
- Invalid bounds/storage return before MMIO and preserve output/diagnostics.
  State checks precede argument checks. Outputs are complete persistent
  ordinary XDATA, after the whole private prefix, below1E00 and outside
  generic-store scratch.
- The **first operational fault** is retained. Both later APIs return it,
  without MMIO or diagnostic/output writes, even with invalid arguments.
  There is no stop, retry-after-fault, history-import or recovery API.
  A separately established full reset is required before reuse.

Every error leaves the caller's stamp unchanged. An initializer failure may
leave partial configuration, a period selector active, or the timer running.
There is no hidden selector restoration, flag acknowledgment, stop/reset or
success-shaped fallback. Diagnostics preserve phase and last observed control/
selector/flags, poll/discard counts, raw elapsed ticks and timebase status.
Argument/state misuse does not replace the last operational diagnostic.

### Independent bounds and discontinuities

Each operation requires `0 < timeout < 0x800000` raw Sleep Timer ticks and a
positive 16-bit poll budget. The unchanged real timebase supplies one deadline,
starting after finite entry preflight and before configuration writes/live
attempts. Every write group or live attempt requires a remaining confirmation
poll. **Equality times out**, including a valid tuple observed at equality.
The final allowed timely confirmation may succeed. A stalled Sleep Timer or
repeated FF read still terminates through the work cap.

This is not calibrated wall time or proof of timer frequency/progress from
STATE alone. The executing-CPU and timebase true-half-range assumptions remain
mandatory; no modulo arithmetic recovers missed full wraps or a lost epoch.

PM and debugger halt/step/resume/inspection are outside this epoch contract.
Reading T2M0 or T2MOVF0 is potentially destructive to latches, not harmless
debugger visibility. Do not inspect timer registers during a call or use a
paused run to establish timing. Current debugger protections/permissions are
unchanged. No claim of PM1/PM2 continuity is imported from the radio chapter's
brief description: section22.4 specifies synchronization/compensation that this
awake-only service deliberately does not perform.

## Why there is no capture API

The primary sources leave concrete requirements unresolved:

1. **Edge/selection:** SWRU191F section22.1.10 p199 describes automatic capture when
   radio SFD status goes **high**, with selectors001 exposing the last captured
   timer and overflow values. SWRS081B p21 and SWRU191F's overview p22 also
   claim capture at TX end, but
   the reviewed CC2530 Timer2/RF register path supplies no end-selection
   procedure resolving that broader claim. TI clarification or other applicable
   primary register-level evidence is needed; this is **not** an assertion that
   end capture is physically impossible.
2. **Do not transfer another part's solution:** SWRU191F Table25-13 p304 has
   programmable beginning/end and first-packet TXCAP/RXCAP modes in **CC2541
   proprietary mode**, not CC2530. No such setting is used or inferred here.
3. **Latch/freshness:** the documented common read latch applies to selected
   live counters000. It does not establish a freeze/read handshake for capture
   registers selected001 during another SFD. Timer2's six IRQ flags contain no
   capture-valid/generation/overrun flag. Reset-zero, an old capture and multiple
   events cannot be made fresh by checking for a nonzero value or a sticky flag.
   A future owner must prevent/identify overwrite and correlate RX/TX events.
4. **Physical end and phase:** section23.8.11 p222 distinguishes the SFD interrupt
   from the GPIO SFD signal; section23.9.4/Fig23-11 p224 gives RX SFD timing.
   Section23.8.2 p218 describes TX's 2-us down-ramp/SFD stretch. SFD rise,
   TXDONE observation, assumed frame duration or a foreground live read is not
   automatically an exact captured trailing-PPDU/ACK-end timestamp.

No capture getter, fabricated freshness bit or gratuitous UNSUPPORTED stub
was added. Before a real #13 adapter, resolve those semantics, RF event ownership,
capture overwrite/delivery order, raw epoch extension, fine-phase/rounding
policy and the abstract scheduler's time API. Independently observe physical
timing under a separate authorization. This work closes none of those gates.

### Additional TI support evidence: estimates are not bounds

MaMoe's reply in [TI E2E thread90922, CC2530 Timer 2 Capture Function](https://e2e.ti.com/support/wireless-connectivity/other-wireless-group/other-wireless/f/other-wireless-technologies-forum/90922/cc2530-timer-2-capture-function)
describes estimated TX analog delay below0.5us and RX SFD-detection delay
around3us, varying with temperature, voltage and process. It also describes
clock-domain-crossing jitter, but expressly says worst-case and RMS jitter
have not been evaluated. These are support estimates, **not guaranteed
offsets or worst-case bounds**. They do not justify subtracting3us, assuming
zero delay, rounding away the fine phase or transferring one board's
measurement to every device.

The reply does not supply a CC2530 trailing-edge selection procedure,
capture-register freeze/freshness mechanism or frame-end conversion contract.
It therefore narrows the physical questions for #40 without closing them.
The separate [CC2530-2591 follow-up](https://e2e.ti.com/support/wireless-connectivity/other-wireless-group/other-wireless/f/other-wireless-technologies-forum/159361/cc2530-2591-module-timer-2-capture-function)
asks whether the delay transfers to that front end; the retrieved question
does not establish that it does. No driver, timestamp correction or hardware
observation is added by this source review.

## Offline evidence, ABI and resources

Baseline **SDCC4.2.0** canonical large-model flags and unchanged linker bounds:
IRAM100, ordinary XDATA below1E00, unbanked CODE below8000. Link
**timebase -> mac_time -> test caller**. Snapshot all three relocated `.rst`
listings immediately after this link, before another link can overwrite them.

Both board definitions have identical linked CODE:

```text
2,999 bytes, 0000..0BB6
SHA256 3caf16cb1d4010f02b32c9d448aab19eb816137f4e4d5c9be5088d37bc78b336
```

- **124 ordinary XDATA +64 status reservation =188/512 bytes**, within this
  standalone composition's budget. No earlier budget is changed.
- Whole timebase/service/compiler prefix `0000..0060` (97 bytes);
  private diagnostic `001C..0029` (14 bytes), staging `002A..002F` (6),
  wait state `0030..0040` (17). Public output is a **two-byte XDATA pointer**.
- Caller guard/output `0061..006E`, actual six-byte stamp `0065..006A`;
  generic-store helper scratch `007B` is explicitly excluded.
- IRAM: bank0 eight bytes, DATA3, overlay3, one bit-storage byte (one bit).
  The 18-byte `0E..1F` gap is unused, not an implicit pool.
  Stack `21..FF` reserves223 bytes, initial SP20; observed MMIO peak **SP2E**.
  Upper IRAM80..FF remains guarded and every call unwinds. This is not an
  interrupt-nesting or whole-application stack bound. `1F00..1FFF` remains
  the IRAM alias, never extra RAM.

[Native tests](../tests/test_mac_time.c) execute the real driver/timebase with
an original stateful counter/selector/latch model. Each board passes **8,384
calls**, including every fine count against coarse byte/wrap boundaries,
low-FF erratum, live carries during latched reads, malformed state/arguments,
configuration/readback failures, RUN without STATE, deadline/work exhaustion,
full16-bit cap, nonpublication and retained re-entry.
The same native corpus also passes ASan/UBSan for both board definitions.

[Linked checks](../tests/boot_mac_time.py) run **48 sequences /158 calls /
10,550 MMIO events** per board using genuine SDCC instructions, not patched
returns. They pin whole CODE, public/private/caller ABI, every MMIO site and
actual operand, all allocations, accessor, listing snapshots and guards.
The five adjacent period writes are individually stepped: uCsim `run` must not
skip a breakpoint at the current PC. FF decisions use one saved destructive
read, not a second hidden T2M0 access.

Negative controls reject every changed CODE byte, all six public API map/CDB
entry mutations, changed/conflicting return declarations, representative
layout mutations, missing/duplicate/reordered listing instructions and a
missing actual IRAM alias. Another genuine replay
deliberately re-latches MOVF0 across a carry while retaining the original M0
instant oracle; the resulting **range-valid torn tuple fails** the output
check. This is not a claim that software detects electrically silent hardware
violations of the documented latch. Marker parsing is indexed once, and every
simulator process retains the existing **15-second deadline**.

These levels are **host-tested, image-checked and simulated**, not independent
silicon models or hardware observations. No calibration, physical latch/timing,
PM/debugger continuity, capture, RF or generic/LG hardware acceptance is claimed.

Directly coupled `test-timebase test-radio-tx` passes for each board in separate
`build/mac-time-dev/coupled-<board>` directories: unchanged timebase corpus,
and TX's 7,078 native calls plus51 linked sequences/173 calls/45,538 events.
Normal image verification also passes for both `bringup` and `timebase_fixture`.
Their full `.bin` bytes are identical before/after the ten SFR declarations:
generic331/1807 CODE bytes, LG371/1847 respectively. This checks representative
existing consumers without repeating the unrelated whole image matrix.

### Reproduction and integration

The canonical offline targets are:

```sh
make -j1 BOARD=generic BUILD=build/mac-time-check test-mac-time
make -j1 BOARD=lg_esl29_rev03 BUILD=build/mac-time-lg-check test-mac-time
```

Both targets preserve the strict native/SDCC flags and actual
`timebase.rel -> mac_time.rel -> mac_time_test.rel` link. All three listings
are copied immediately to `mac_time_test.timebase.rst`,
`mac_time_test.mac_time.rst` and `mac_time_test.test_mac_time.rst`, before
native/proof execution or another shared-object link. The common check runs
this corpus once per board definition in `test-local`. The Make inventory/
snapshot regression and all-board symbol/source exclusions cover the new
target; there is no new board `IMAGE`, hardware runner or upload artifact.
All 50 focused Make/artifact regressions and the complete 487-test tool suite
pass (19 explicit optional/platform skips). No earlier budget is enlarged.

No SDK/IAR/programmer implementation, private capture, identity or recovery
material was accessed or imported. Public primary sources:
[SWRU191F](https://www.ti.com/lit/ug/swru191f/swru191f.pdf),
[SWRS081B](https://www.ti.com/lit/ds/swrs081b/swrs081b.pdf),
[SWRZ031](https://www.ti.com/lit/er/swrz031/swrz031.pdf).
