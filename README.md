# cc2530-zigbee

[![SDCC CI](https://github.com/faronov/cc2530-zigbee/actions/workflows/ci.yml/badge.svg)](https://github.com/faronov/cc2530-zigbee/actions/workflows/ci.yml)
[![License: BSD-3-Clause](https://img.shields.io/badge/license-BSD--3--Clause-blue.svg)](LICENSE)

An experimental, open C/SDCC project aiming to implement a small Zigbee end
device and, subsequently, a sleepy end device on the TI CC2530.

**Current status: bootstrap, guarded hardware-debugging tools, bounded passive RX
and single-frame TX fixtures, and offline protocol components.
This is not yet a working Zigbee stack.** Default `bringup` does not enable radio.
Separately selected fixtures have [hardware-observed TX/RX results](docs/DEBUGGING.md#2026-09-21-lg-tx-and-passive-rx-demonstration);
they do not implement network join, a sensor application or display refresh.
The project does not require IAR or proprietary TI stack libraries.

The selected specification baseline is **Core R22 + PRO BDB 3.0.1**
(`16-02828-012`). The first centralized-network ED target is a subset, not
full BDB support. Base-text development may proceed with unreviewed BDB errata
recorded as a [conformance risk](docs/CONFORMANCE.md); implementation and
security acceptance are not waived.

[Русский обзор](README.ru.md)

## Start here

| Resource | Purpose |
| --- | --- |
| [Development plan](docs/PLAN.md) | Milestones, dependencies, acceptance gates and risks |
| [Architecture](docs/ARCHITECTURE.md) | Layers, memory ownership and proposed interfaces |
| [Bring-up](docs/BRINGUP.md) | Current build commands, board behavior and status ABI |
| [Debugging](docs/DEBUGGING.md) | M1 fixture, guarded host transport, offline tools and remaining gates |
| [Validation](docs/VALIDATION.md) | Host, simulator and real-hardware evidence requirements |
| [Conformance ledger](docs/CONFORMANCE.md) | Required behaviors, specification gates and implementation status |
| [Sources and licensing](docs/PROVENANCE.md) | Permitted inputs and reference provenance |
| [Contributing](CONTRIBUTING.md) | How to add a small, reviewable change |

## Build the bootstrap

From a Git checkout, the baseline is **SDCC 4.2.0**, Python 3.9 or newer,
GNU Make, a host C compiler and the `s51` simulator. On Ubuntu 24.04:

```sh
sudo apt-get update
sudo apt-get install --no-install-recommends \
  build-essential python3 sdcc=4.2.0+dfsg-1 sdcc-ucsim=4.2.0+dfsg-1

make BOARD=generic all test
make BOARD=lg_esl29_rev03 all test
python3 -m unittest discover -s tools -p 'test_*.py'
python3 tools/check_repository.py
```

On macOS, place an installed SDCC 4.2.0 toolchain, including `s51`, on `PATH`,
then use the same `make` commands. No IAR SDK is needed. Linux is the hosted CI
platform; additional toolchain/OS versions are not implied to be tested.

Outputs are under `build/<board>/`, including `bringup.hex`, `bringup.bin`,
linker/debug information and `build-info.json`. **These are bootstrap images,
not Zigbee firmware releases.** Builds and CI never flash a device.

The generic board performs no application-specific pin control. The
`lg_esl29_rev03` board keeps its known display controls off. A board name is not
permission to flash an unknown device: preserve and verify recovery backups
and confirm the hardware first.

## M1 debugger fixture

The first target-side M1 component is a separate, deterministic non-RF image:

```sh
make BOARD=generic IMAGE=debug_fixture all test
make BOARD=lg_esl29_rev03 IMAGE=debug_fixture all test
```

Outputs are `build/<board>/debug_fixture/debug_fixture.*` and `build-info.json`.
It reuses the bootstrap's board/startup policy and status ABI, adding four
named code locations, nested calls, a 16-byte state block and a known-register
NOP/RET probe. The [fixture contract](docs/DEBUGGING.md#implemented-target-fixture)
describes exact expectations and limits.

Both fixture builds are **host-tested, image-checked and alias-aware simulated**.
On 2026-09-16, the unchanged 626-byte LG fixture was also **hardware-observed**
on one LG Rev0.3: all four breakpoint slots, 257 physical cycles including wrap,
register-preserving access, NOP stepping, reset/reinitialization, RAM aliasing,
USB timeout/stall handling and recovery at a halted erased-image boundary.
The [dated record](docs/DEBUGGING.md#2026-09-16-lg-fixture-hardware-record)
identifies the image, adapter, pinned-backend run and limitations.
The generic board remains hardware-unobserved; neither standalone `bringup`
image was flashed. At M1, shared startup was exercised physically only through
the LG debug fixture; the later timebase acceptance below is separate.

The [host transport](docs/DEBUGGING.md#implemented-host-transport) provides
explicit USB selection, status/config, PC/bank/register and bounded SFR/XDATA/CODE
reads, verified SRAM writes and unbanked hardware breakpoints. PC/register/memory
inspection requires an awake, halted CPU. CPU control, reset, memory access,
memory writes and breakpoints have separate permission gates. The public writer
is limited to XDATA `0x0000..0x1DFF`; it cannot write status, the IRAM alias,
MMIO or flash. Register restoration is attempted only after successful
exchanges and is verified; errors fault the session without retries,
automatic restoration, resume or endpoint recovery.

Destructive `attach-reset` has its own explicit confirmation; `reset-halt`
requires an already prepared session. Neither is a non-reset attach.
Opening/closing never resets or resumes. The
[manual acceptance runner and recovery helper](docs/DEBUGGING.md#manual-hardware-acceptance-and-recovery)
are not CI/build actions and never authorize hardware access implicitly.
PyUSB is optional; ordinary tests use synthetic backends and never enumerate USB.

The [offline image tool](docs/DEBUGGING.md#offline-image-symbol-and-snapshot-tools)
performs global symbol lookup, exact CDB source-line lookup, strict status
decoding and breakpoint-parameter preparation against verified image files
and hashes, with **no USB access**. Source lookup does not infer the nearest
line or resolve source-file paths.

**M1 is complete for the bounded LG/unbanked baseline.** A full fixture check
passed after the first confirmed cable reconnection; the later final held-handle
unplug test proved fault latching, denied resume and visible cleanup errors.
After the last replug, PyUSB enumeration and a full one-cycle fixture check in
an explicitly selected new session also passed. That final run left the
fixture halted at `0x0173`. Bank discrimination is required when banked CODE
is introduced, not for this baseline. Completion does not validate generic hardware,
sleeping/MMIO/full-SFR/flash-writer/GDB support or any M2/RF/network service.

## First M2 slice: awake-only timebase

The standalone [timebase](docs/ARCHITECTURE.md#awake-only-timebase-first-m2-slice)
reads the 24-bit Sleep Timer in latched ST0/ST1/ST2 order and implements
bounded modular deadlines with explicit invalid/ambiguous results. It requires
one foreground reader and awake operation, with no clock selection,
millisecond conversion, interrupts, compare or wake handling.
`make test-timebase` provides **host, linked-image and alias-aware simulator**
coverage for the C implementation. A separate
[hardware reference](docs/VALIDATION.md#independent-sleep-timer-hardware-reference-2026-09-16)
observed raw counter progression and a natural rollover through the M1
debugger, not execution of this C driver or calibrated timing. The isolated
test executable must never be flashed. The module is still excluded from
`bringup` and `debug_fixture`: their firmware bytes and M1 evidence remain unchanged.

A subsequent, distinct [board timebase fixture](docs/DEBUGGING.md#awake-only-timebase-board-fixture)
now exercises the compiled C reader/deadline helpers with a 128-raw-tick delay,
a 1,024-poll limit and latched faults:

```sh
make BOARD=generic IMAGE=timebase_fixture all test
make BOARD=lg_esl29_rev03 IMAGE=timebase_fixture all test
```

Outputs are under `build/<board>/timebase_fixture/`. This third board image
shares the original startup/board policy and M0 ABI. Both boards have **host,
linked-image and simulator** coverage. On 2026-09-16, the verified 1,847-byte
LG image also passed [compiled-C hardware acceptance](docs/DEBUGGING.md#2026-09-16-lg-compiled-c-timebase-acceptance):
257 cycles, elapsed 129..130 raw ticks for requested 128, exactly 37 polls
per cycle, byte-counter wrap and preserved CPU/M0 state. That run left the LG
timebase fixture halted at READY `0x016A`; later clock work is separate below.
The manual runner independently verifies all physical CODE before
resume and never flashes.

Generic hardware and physical timer-fault injection remain unobserved. Stopped,
backward and ambiguous C paths retain host/simulator coverage; the C run is
not calibration or a natural 24-bit timer-wrap claim. CI covers both boards
and all seven `IMAGE` variants without uploading standalone test executables.
**M2 #4 remains open** for clock measurement/calibration, IRQ/compare/wake and
the other platform gates.

The next isolated [init-time system clock selector](docs/ARCHITECTURE.md#init-time-system-clock-selector-isolated-m2-slice)
requests RC16/XOSC32 through real CMD/STA registers, with raw-time/poll limits
and a single bounded rollback that never hides the original failure.
`make test-clock` provides **host, linked-image and alias-aware simulator**
coverage; separate bounded LG compiled-C hardware acceptance is recorded below.
All six older board BINs remain unchanged; the selector is not linked into them. Its standalone
`clock_test.ihx` must never be flashed or uploaded as board firmware.
There is no LF source switching, calibration service or new sleep/IRQ support.

The subsequent [clock board fixture](docs/DEBUGGING.md#init-time-clock-board-fixture)
links the real driver separately as `IMAGE=clock_fixture`:

```sh
make BOARD=generic IMAGE=clock_fixture all test
make BOARD=lg_esl29_rev03 IMAGE=clock_fixture all test
```

It repeats RC16 idempotence, XOSC32 and RC16 with bounded calls, a 56-byte
serialized result and terminal faults. Its explicitly authorized manual runner
verifies all physical CODE before resume; optional timeout testing uses a
strictly checked deadline-return breakpoint, not memory/code injection.
The [first LG clock experiment](docs/DEBUGGING.md#2026-09-16-lg-clock-cancellation-failure)
passed `C9 -> 88 -> C9` normally but **failed rollback acceptance**: old STA briefly
matched before a delayed XOSC transition. The fix requires observing requested
source then return; a never-observed departure yields bounded
`CLOCK_ROLLBACK_UNCONFIRMED=9`, not success.

The corrected 3,798-byte LG image passed
[compiled-C hardware acceptance on 2026-09-17 (UTC+03)](docs/DEBUGGING.md#2026-09-17-lg-compiled-c-clock-acceptance):
the original pending-cancel test passed unchanged, with confirmed rollback
only after 64 raw ticks / 15 polls, and the separate late-source timeout test
also passed. Both preserved the original TIMEOUT and terminal FAULT. A
separate explicit reset/recovery run then passed 257 full sequences
(771 actual C calls), including counter/heartbeat wraps and CPU/M0/clock-field
invariants. That clock run left the LG fixture halted at READY `0x016A` on
RC16; the subsequent IRQ acceptance below changed the installed image.
Generic remains **host/image/simulator-only**.
This is not frequency/calibration, physical oscillator-failure/stopped-clock
or never-departed cancellation confirmation. Prior M1/timebase evidence is
historical and unchanged; M2 stays open.

## M2 interrupt ownership foundation

The isolated [EA primitives](docs/ARCHITECTURE.md#interrupt-ownership-foundation-isolated-m2-slice)
save the previous global interrupt-enable bit, disable it atomically, and
restore exactly a caller-owned 0/1 token. Proper LIFO nesting keeps an outer
section disabled; invalid byte tokens return an explicit error without
interrupt/peripheral access. Valid-shaped forged or out-of-order tokens
cannot be detected.

`make test-irq` supplies **host, linked-image and alias-aware simulator**
coverage, including genuine generic C52 interrupt entry, higher-priority
nesting and RETI context preservation. These tiny SDCC reentrant leaves are
not a peripheral dispatcher or physical CC2530 interrupt acceptance.
The foundation preserved all eight older board BINs; the separate IRQ fixture
below now links the primitives intentionally. `irq_test.ihx` is **test-only: never flash
or upload it as board firmware**. Its synthetic coverage is separate from the
bounded LG Timer1 hardware acceptance below; M2 #4 remains open.

## M2 Timer1 IRQ board fixture

`IMAGE=irq_fixture` is a separate non-RF board image for both boards:

```sh
make BOARD=generic IMAGE=irq_fixture all test
make BOARD=lg_esl29_rev03 IMAGE=irq_fixture all test
```

It exercises the real EA primitives, an initially disabled section, nested
outer/inner ownership, Timer1 overflow pending while EA=0, and one ISR/RETI
per successful arm. The counter is stopped before reenabling EA; the ISR
masks its source before acknowledging overflow. No pins are routed to the
timer, and no general timer/dispatcher service is added.
The [manual runner and exact ABI](docs/DEBUGGING.md#timer1-irq-board-fixture)
verify all physical CODE before runner resume and inspect actual ISR context.
Normal repeated cycles and a separate pre-start timeout mode require explicit
hardware authorization. The unchanged 3,269-byte LG image passed
[compiled-C Timer1/IRQ acceptance on 2026-09-17 (UTC+03)](docs/DEBUGGING.md#2026-09-17-lg-compiled-c-irq-acceptance):
three initial cycles, a separate pre-start TIMEOUT retained at FAULT, then a
separately reset 257-cycle run with real ISR services and byte-counter wraps.
Every normal interrupt returned to `0x0C9D`, **restore+12 with DPL=OK (0)**,
not the synthetic restore+9/live-token-1 case. Full CPU/active-IRAM context
and actual RETI restoration passed. That run left the IRQ fixture halted at
READY `0x01BB`, EA/T1IE disabled and Timer1 stopped; later FIFO work is separate.
Generic remains **host/image/simulator-only**. This does not establish
calibrated time/latency, hardware one-shot or exact overflow counts,
higher-priority hardware nesting, other interrupts or platform services.
All eight older BINs are byte-identical. This IRQ slice introduced ten board/image jobs with
the same exact board-only artifact whitelist. Historical evidence is unchanged,
past acceptance grants no new hardware authorization, and M2 #4 remains open.

## M2 quiescent radio FIFO foundation

The isolated [FIFO API](docs/ARCHITECTURE.md#quiescent-radio-fifo-foundation)
verifies explicit RX/TX FIFO clears and bounded TX preloads through real RFST/
RFD instructions. It requires caller-established quiescence and stable XOSC32,
does not enable RF, and rejects controller errors without erasing interrupt
history. Preload accepts 1..125 body bytes, writes the PHY length plus body,
and verifies each FIFO count/pointer advance; it does not transmit or validate MAC.
`make test-radio-fifo` provides **host, linked-image and synthetic FIFO/CSP
simulation** evidence only. `radio_fifo_test.ihx` must never be flashed or
published as board firmware.

The separate [`IMAGE=radio_fifo_fixture`](docs/DEBUGGING.md#quiescent-radio-fifo-board-fixture)
now selects/observes XOSC32, verifies empty FIFO state, preloads small XDATA and
maximum CODE payloads, checks their accepted TX RAM bytes and explicitly clears
TX between frames. Its guarded manual runner is not a build/CI action.
The 8,979-byte LG image also passed
[bounded hardware acceptance on 2026-09-17 (UTC+03)](docs/DEBUGGING.md#2026-09-17-lg-compiled-c-fifo-acceptance):
257 separately reset recovery cycles, 33,410 written/verified/readback-checked
bytes and 514 explicit TX clears. A separate timeout retained one written but
unverified byte at terminal FAULT, without implicit recovery. Generic remains
**host/image/synthetic-simulator-only**. RX flush, received frames, FCS and
on-air operation are not validated. That FIFO run left its fixture
halted at READY `0x016A`, XOSC32 selected, IRQs disabled and both FIFOs empty.
All ten older BINs and historical evidence remain unchanged. M2 #4 stays open.

## Isolated DMA prerequisite

The [single-owner channel-0 DMA copy](docs/ARCHITECTURE.md#isolated-channel-0-dma-copy)
supports bounded 1..16-byte XDATA RAM copies with terminal fault/lifetime
protection. `make test-dma` supplies **host, linked-image and synthetic DMA
evidence only**; never flash `dma_test.ihx`. This RAM-only slice does not
validate AES/peripheral DMA; the [AES CPU sequencing gap](docs/PROVENANCE.md#m2-aes-cpu-transfer-prerequisite)
and remaining radio gates are not closed.
The separate [debug-config gate](docs/DEBUGGING.md#guarded-dma-enable-after-reset)
has bounded LG hardware evidence; it does not exercise or validate DMA copies.

The separate [`IMAGE=dma_fixture`](docs/DEBUGGING.md#channel-0-dma-board-fixture)
has host/image/synthetic checks for both boards and
[bounded LG hardware acceptance on 2026-09-17](docs/DEBUGGING.md#2026-09-17-lg-compiled-c-dma-acceptance):
257 separately reset recovery cycles, 514 copies / 6,289 verified bytes across
RC16/XOSC32 and both RAM routes. A separate real timeout retained an unacknowledged
completion and terminal FAULT without payload inspection or reuse.
Only these two images link the RAM-copy DMA driver. Generic remains hardware-unobserved.
All twelve older BINs remain byte-identical; that slice brought CI to fourteen jobs with the
same seven-file whitelist and `hardware_tested=false`. That DMA run ended
on the 8,890-byte DMA image at READY `016A`, debug config22, RC16, IRQs disabled
and no armed channel, pending request or DMA completion flag.

## Isolated AES-128 DMA foundation

[`aes128_encrypt_block`](docs/ARCHITECTURE.md#isolated-aes-128-dma-block)
encrypts one 16-byte block through two AES-triggered DMA channels, with private
staging, a single bounded deadline/poll cap and retained terminal faults.
`make test-aes` checks public NIST vectors, an independent host-only reference,
real SDCC CODE/XDATA callers and synthetic descriptor/alias transfers.
These automated checks are **host/image/synthetic evidence**, separate from
the bounded LG hardware observations below.
Never flash `aes_test.ihx`; all fourteen earlier board BINs and the seven-file
artifact whitelist remain unchanged. There is no messaging ECB,
CCM/authentication, key management or production software fallback.
The [remaining physical AES coverage](docs/VALIDATION.md#m2-isolated-aes-dma-block-coverage)
and CPU-only sequencing remain open.

The separate [`IMAGE=aes_fixture`](docs/DEBUGGING.md#aes-dma-board-fixture)
has host/image/synthetic coverage for both boards. Compiled C calls the hardware
driver for 21 public cases, all CODE/XDATA input combinations, both clocks and
257 same-reset cycles, with bounded LG hardware acceptance below.
Guarded manual tooling performs genuine pre-key and late-final timeout
experiments without inspecting private payload after failure.
That AES addition brought CI to sixteen full jobs, the same seven artifacts and
`hardware_tested=false`.
The [first physical LG KEY load](docs/DEBUGGING.md#2026-09-17-first-lg-aes-key-load-failure)
exposed a completion-flag bug. The unchanged corrected **12,765-byte LG image**
subsequently passed [short normal operation, both exact negatives and reset recovery](docs/DEBUGGING.md#2026-09-17-corrected-lg-aes-bounded-acceptance).
Normal3 accepted six blocks on both clocks, vectors0/1/2, space combinations0/1
and 18 confirmed ENC ACKs. Fresh KEY **and IV** flags and per-phase ACK checks are now
hardware-observed; both genuine timeouts preserved caller output.
Separately reset recovery accepted 514 blocks over 257 cycles, with all 168
vector/space/clock combinations independently checked and 1,542 confirmed ENC ACKs.
That AES recovery ended at **READY016A/config22/RC16**,
completed/heartbeat1 and fault latch0; this is historical after the PRNG
programming below. Generic AES hardware remains unobserved;
this is not calibrated timing, general DMA/security acceptance or closure of M2 #4.

## Isolated deterministic PRNG foundation

[`prng_seed_explicit` / `prng_next16`](docs/ARCHITECTURE.md#isolated-deterministic-prng)
provide an **explicitly seeded deterministic hardware LFSR, not entropy or a
cryptographic RNG**. A valid caller seed is loaded high-byte first; one bounded
command advances 13 feedback shifts and returns the full 16-bit state.
The two forbidden fixed points are `0000` and `8003`; exhaustive host analysis
finds two other cycles of 32,767 states, not a 65,535-state period.

The separate [binary noise health-test core](docs/ARCHITECTURE.md#binary-raw-noise-health-test-foundation)
provides host/image/simulator-checked RCT/APT with explicit diagnostic cutoffs.
It neither collects RF samples nor qualifies entropy; even a periodic balanced
stream can pass. `make test-noise-health` is a separate offline-only harness.

The [isolated raw IRND collector](docs/ARCHITECTURE.md#isolated-raw-irnd-acquisition)
adds one bounded, reset-exclusive1..1024-bit no-sync receive capture with
explicit partial-failure/timing metadata. `make test-radio-noise` exercises
real driver/health-core code, not a physical source. Both boards are
host-tested, image-checked and simulated, without an entropy claim.
The distinct [boot-disarmed board fixture](docs/RADIO_NOISE_FIXTURE.md),
`IMAGE=radio_noise_fixture`, now links real startup/clock/collector/health,
requires exact ARM then RUN, and retains one1024-bit channel26 capture.
Both-board native/sanitizer and linked/alias-aware checks pass; hardware
acceptance remains pending. **Never flash `radio_noise_test.ihx`.**

`make test-prng` checks the real driver against independent host mathematics
and an isolated SDCC/alias-aware synthetic executable. **Never flash
`prng_test.ihx`**. The separate
[`IMAGE=prng_fixture`](docs/DEBUGGING.md#deterministic-prng-board-fixture) now
links the unchanged driver for both boards. Its 32-word batches include real
31-word final tails: four full 32,767-word periods, both disjoint cycles on
RC16/XOSC32, plus short seed/reseed cases. The host checks every actual word,
not just a count/hash. A separately selected genuine stopped-RCTRL probe tests
terminal rejection; holding the CPU is **not** a PRNG poll-timeout experiment.
That PRNG addition preserved all sixteen older BINs and existing drivers.
The passive RX addition brought CI to twenty jobs; the flash and boot-disarmed
TX fixtures brought it to twenty-four; the raw-IRND and same-owner TX/RX
board fixtures bring it to twenty-eight, with the
same seven artifacts and `hardware_tested=false`.
The original **7,224-byte wirev1 LG PRNG image** passed
[short hardware acceptance on 2026-09-17](docs/DEBUGGING.md#2026-09-17-lg-prng-short-acceptance):
two seed1234 loads and eight RC16 words, repeating `8D94 E5AC CBBE 1731`,
with real benign-error, non-advancing readback, guard and tail checks.
Its [first long run was interrupted by a fixture flag-policy bug](docs/DEBUGGING.md#2026-09-17-lg-prng-long-run-flag-policy-interruption):
the default Sleep Timer compare latched STIF after C's snapshot. This was not
a PRNG driver/output failure; the last32-word batch was not host-accepted.
Corrected wirev2 permits only sticky STIF0->1, with ordered C/live history;
all other flag bits remain strict and no flag is cleared.
The unchanged **7,289-byte wirev2 LG image** then passed
[corrected short hardware acceptance](docs/DEBUGGING.md#2026-09-17-corrected-lg-prng-short-acceptance):
eight RC16 words and six raw flag observations, with **no STIF transition**.
The same image then passed
[full-stopped hardware acceptance](docs/DEBUGGING.md#2026-09-17-corrected-lg-prng-full-stopped-acceptance):
131,084 individually checked words, four32,767 periods/all65,534 valid states
per clock, the actual C0/live80 STIF race and2,145 later preserved observations,
plus the genuine RCTRL11 probe with retained6/6/6 errors and unchanged caller data.
The same image passed a
[separate full-reset recovery](docs/DEBUGGING.md#2026-09-17-corrected-lg-prng-full-reset-recovery-acceptance):
the entire corpus again, with a distinct `c-snapshot` STIF transition and
2,253 later preserved observations, stopping before the probe.
**The bounded LG short/stopped/reset-recovery hardware gate is complete.**
The final LG state is halted ENDREADY016A/config26/RC16, fault0 and C/live
IRCON80; the earlier stopped FAULT is historical. Generic/EOC1/poll-fault and
broader entropy/RF/security/sleep gates remain open. No hosted-CI result is claimed.
[ADC/CSP ownership, shared-register semantics and remaining physical gates](docs/VALIDATION.md#m2-deterministic-prng-coverage)
are explicit. RF/noise seeding, ADC conversion, sleep and security randomness
remain unimplemented; M2 #4 stays open.

## Bounded passive RX

The [bounded passive receiver](docs/RADIO_RX.md) configures channels 11..26,
receives one CRC-checked body with raw RSSI/correlation, and verifies soft
shutdown/flush without TX, automatic ACK, DMA or interrupt enable.
`make test-radio-rx` is **host-tested, image-checked and synthetically
simulated only**; its standalone test executable must never be flashed.
The separate `IMAGE=radio_rx_fixture` now links the actual startup, board,
clock and RX services for both boards. It selects channel15 and permits
at most16 bounded attempts, with READY checkpoints and terminal END/FAULT.
Unlike the nine older non-RF images, this image **can enable RF reception**.
Only successful publication increments heartbeat; BAD_CRC publishes nothing.
A fault may leave RX active and requires separately authorized full-reset
recovery, not resume/retry. A
[parent-observed LG failure/probe](docs/DEBUGGING.md#2026-09-18-lg-rx-fscal1-failure-and-probe)
identified an FSCAL1 reserved-bit guard error. The corrected image passed
[bounded LG hardware acceptance on 2026-09-18](docs/DEBUGGING.md#2026-09-18-lg-bounded-passive-rx-acceptance):
one initial reception, a retained pre-RF timeout, then a separately reset
16-attempt run. All14 published bodies in that run matched the concurrent
Nordic capture; two BAD_CRC attempts published nothing. The16-attempt cap
reached retained END with RX disabled and empty FIFOs.

The [2026-09-21 demonstration](docs/DEBUGGING.md#2026-09-21-lg-tx-and-passive-rx-demonstration)
also exercised the current9,183-byte LG image: one10-byte CRC_OK body matched
exactly one concurrent Nordic record. The image remains halted at READY016A,
with RX disabled and empty FIFOs. This was passive ambient reception, not a
response to the separate channel26 TX.

```sh
make BOARD=generic IMAGE=radio_rx_fixture test-radio-rx-fixture
make BOARD=lg_esl29_rev03 IMAGE=radio_rx_fixture test-radio-rx-fixture
```

Run those simulator jobs serially. Full hashes,96-byte wire ABI,128-byte
caller frame, scoped memory budget and limitations are in
[RADIO_RX.md](docs/RADIO_RX.md#bounded-passive-rx-board-fixture).
The [manual runner](docs/DEBUGGING.md#parent-only-passive-rx-acceptance) is
separately authorized, never automatic, verifies physical CODE before execution
and requires a new private capture outside the repository. Raw frames/identities
must not go to stdout or CI. Generic hardware, calibrated metadata, independent
on-air FCS verification and broader RF fault recovery remain unobserved.
This is not continuous/lossless reception or MAC/security/network support.

The separate [bounded radio queues](docs/RADIO_QUEUE.md) now provide two RX
copies, one pending TX candidate and four ISR-safe request cookies. One
foreground service calls the actual passive receiver; full queues reject
new work and preserve owned buffers. TX dequeue is only a memory operation,
not transmission. `make test-radio-queue` has host, exact-image and
alias-aware/generic-C52 preemption evidence, **not CC2530 IRQ/RF acceptance**.
It is not linked into board images; never flash its standalone test.

## Isolated TX and CCA primitives

The [bounded init-time TX/CCA service](docs/RADIO_TX.md) now implements direct
TX, controller-gated TX-on-CCA and CCA-only sampling. It composes the actual
FIFO/timebase services, supports channels 11..26 with explicit raw power `05`,
and requires fresh TXDONE plus verified idle for PHY completion. Busy CCA
permits caller-directed reuse only after verified shutdown; faults retain
their cause without retry or implicit cleanup.

`make test-radio-tx` is **host-tested, image-checked and simulated**, not
hardware-observed. Its standalone executable must never be flashed.
The separate [boot-disarmed board fixture](docs/RADIO_TX_FIXTURE.md), selected
with `IMAGE=radio_tx_fixture`, now links the actual clock/FIFO/TX services.
It requires distinct ARM/RUN admissions and an inspectable ADMITTED stop before
one conditional-clear attempt on channel26/raw power05 with public `TXF1`
bytes, no ACK request and no real identity. Failures retain evidence without
implicit cleanup or retry. `make ... IMAGE=radio_tx_fixture test-board` stays
offline; its manual RF runner is never invoked by builds/CI.

The separately authorized [LG hardware run](docs/DEBUGGING.md#2026-09-19-lg-single-attempt-tx-acceptance)
returned PHY_DONE for one attempt on the previous **channel15** image; an
independent nRF52840 capture contained exactly one byte-identical public body.
That image was left halted at END. A later separately authorized
[channel26 run](docs/DEBUGGING.md#2026-09-19-lg-channel26-isolated-tx-acceptance)
also returned PHY_DONE once; its completed requested90-second capture contained
exactly one record with the public body. That run left the channel26 image
halted at END and the sniffer sleep-commanded on26. The coordinator was not changed.
Neither run establishes permanent channel exclusivity, independent FCS or
calibrated power/timing.

The [2026-09-21 demonstration](docs/DEBUGGING.md#2026-09-21-lg-tx-and-passive-rx-demonstration)
repeated one independently matched channel26 transmission, then installed the
separate passive RX image and received one matching ambient channel15 body.
The current installed image is RX, not TX. These are two separate experiments,
not same-reset bidirectional operation or controlled ping-pong.

The clock prerequisite reduces permanent DATA45 to12 bytes, with ten extra
XDATA bytes, rather than enlarging memory limits. The TX test composition uses
11,291/11,331 CODE bytes (generic/LG),347 ordinary XDATA +64 reserved and
MMIO-sampled SP`0x74`/full-run simulator high-water`0x77`.
These are measured image sizes and exercised paths, not full-stack fit.
Same-reset mixing with legacy RX, queue/controller integration, physical
ACK/retries, busy-channel/fault recovery and broader RF acceptance remain open.
Neither this observation nor old image-specific records establish MAC/networking.

## Isolated filtered receiver and AUTOACK

The [receiver/AUTOACK owner](docs/RADIO_AUTOACK.md) configures and verifies
caller PAN/short/IEEE addresses, version-0 hardware filtering, CRC metadata
and unslotted hardware ACK with Pending0. It maintains an RX request, copies
complete queued frames through RFD, and explicitly stops/drains without
aborting an in-flight reception or ACK. CRC-bad bodies remain explicitly
classified; operational faults retain ownership without hidden cleanup.
Explicit same-owner rearm can now start another RX episode after complete
stop/drain, preserving configuration and FIFO history. It does not recover
a fault, transfer ownership or hide the reception gap.
The same owner also supports one hardware-gated CCA/TX attempt from stopped,
drained idle, retaining RX for subsequent response bodies. This explicit phase
disables filtering/AUTOACK; another stop/drain and resume restores them.
`TX_DONE` is PHY completion, not ACK receipt, delivery or a timed MAC window.

`make test-radio-autoack` is **host-tested, image-checked and simulated only**,
with CI binding both boards to7007 CODE bytes,450 ordinary XDATA +64 reserved
and whole-run SP`0x34`. Its synthetic executable runs once per board in
`test-common` and is never linked into board firmware.
**Never flash `radio_autoack_test.ihx`.** AUTOACK is RF transmission, not passive
reception; no hardware ACK/timing observation or finite over-air ACK-count
guarantee is supplied. Broadcast-AR and ignored ACK-FCF compatibility remain
explicit limits. Physical stop/drain is not IFS completion, a continuous
MAC/POLL window closure, ordinary-TX handoff or security acceptance.

The separate [boot-disarmed same-owner TX/RX fixture](docs/RADIO_LINK_FIXTURE.md)
now links the genuine owner with board/startup/clock services. Explicit ARM/RUN
admission precedes initial RX/AUTOACK, stop/drain, one channel26/raw05 CCA/TX
attempt, a bounded raw receive interval and final stop/drain. Two complete
CRC-good/bad bodies are retained, with pre-TX drainage distinguished; late
shutdown frames do not turn a receive-window timeout into success.
Only this new board image is an AUTOACK-linkage exception.
The [2026-09-22 LG run](docs/DEBUGGING.md#2026-09-22-lg-same-owner-txrx-sequence)
then physically completed one ordinary TX, an empty raw receive interval and
confirmed stop; the unchanged Nordic sniffer captured the exact public `LNK1`
body once. **No reply was received**: this is not bidirectional exchange,
captured ACK timing, delivery, retries or a Zigbee network connection.
Initial AUTOACK can transmit independently of the ordinary one-attempt limit.

**A controlled active Nordic test node is an allowed laboratory role** in
addition to passive sniffing. Preparation is separate from permission to
replace firmware or transmit, and does not block unrelated CC2530 work.

The [Nordic laboratory stimulus](tools/nrf_stimulus/README.md) is a
**source-built offline prototype**, not an installed image: boot-disarmed
ARM/RUN, one fixed channel26/-20dBm public AR frame, then bounded ordinary
promiscuous reception. Its source-built driver/SL and guarded PHYEND patch
distinguish transmission completion from actually received ACK/data bodies.
It is not a CC2530 board image or an installed sniffer replacement. Native
tests and an external target build/static audit establish no RF/timing or
restoration evidence. The [recovery-artifact checker](docs/NRF_RECOVERY.md)
likewise cannot replace fresh physical backup and restoration verification.
An explicitly gated [manual readback operator](docs/NRF_RECOVERY.md#explicit-read-only-acquisition-operator)
prepares two full flash/UICR reads through a separately reviewed programmer
mode. Its default and ordinary tests are offline; no device backup or
restoration is claimed from those tests.
A [separately scoped manual readback](docs/NRF_RECOVERY.md#2026-09-20-corrected-runtime-manual-readback)
preserved two matching full-extent flash/UICR reads privately, but did not
capture ACL state. A [later manual v2 read](docs/NRF_RECOVERY.md#2026-09-20-acl-qualified-manual-readback)
matched those bytes with three stable ACL-qualified snapshots. Atomicity
and recovery usability remain unverified; neither result establishes
restored firmware or authorizes installing the helper.
The [offline overlay planner](docs/NRF_RECOVERY.md#offline-page-overlay-report)
checks preservation of non-image bytes and UICR using private artifacts,
emitting only a hash/count report, never a programming payload or permission.
An [opt-in SRAM-only helper](tools/nrf_stimulus/README.md#optional-sram-only-profile)
now passes host/image checks with 73,220 bytes in RAM and no flash loads,
avoiding flash replacement in a future design. It has not been loaded or
executed. [Checked MEM-AP handoff primitives](tools/nrf_stimulus/README.md#checked-volatile-handoff-preparation)
are now host-tested, but no live operator or accepted restart of the original
sniffer exists; a halted reset-vector result is not restored service.
The [opt-in v3 read-only collector](docs/NRF_RECOVERY.md#opt-in-v3-startup-source-binding)
now records the missing startup selectors and core/watchdog/NVMC facts.
Its conditional source comparison does not authorize execution or remove
the remaining silicon/board/reset and restoration gates.
A [separate manual v3 read](docs/NRF_RECOVERY.md#2026-09-20-v3-startup-binding-manual-read)
now has stable snapshots and full flash/UICR bytes matching the retained
baseline. No SRAM helper was loaded or executed.

## Isolated MAC Timer foundation

The [awake MAC Timer service](docs/MAC_TIME.md) configures positive periods
512/`FFFFFF`, confirms asynchronous RUN through STATE and reads coherent raw
fine/coarse tuples, with the TI low-byte-FF erratum workaround. Independent
deadline/work limits and retained faults prevent unbounded retries or partial
publication. `make test-mac-time` is **host-tested, image-checked and simulated**.
It requires reset-exclusive timer ownership and a quiescent radio; it is not
captured TX/ACK end, a calibrated symbol clock or a real MAC adapter.
No board image links it; never flash `mac_time_test.ihx`.

The separate [fractional epoch arithmetic](docs/MAC_EPOCH.md) extends those
raw coordinates across the actual `FFFFFF` coarse wrap, preserving fine
phase and faulting ambiguous progress. Its independent wide-integer oracle
and target proof do not establish captured TX/ACK ends or a radio adapter.
No board image links it; never flash `mac_epoch_test.ihx`.

The [co-owned live clock/radio composition](docs/MAC_RADIO.md) (#80) now
serializes the real clock, Timer2, fractional epoch and RX/TX owner under the
explicit `CC2530_MAC_RADIO` profile. Live time remains available during its
RX/AUTOACK and stop/drain/send/rearm phases; original quiescent-only entrypoints
remain strict. This is **not captured PHY-event timing**, ACK/retry scheduling
or Zigbee membership. No board image links it; never flash `mac_radio_test.ihx`.

The [delayed raw-sample projection](docs/MAC_STAMP.md) (#81) maps a known
coherent sample into a closed epoch window without rewinding live time,
preserving fine phase and rejecting ambiguous/outside values. It is arithmetic
only: temporal membership does not prove hardware capture freshness or frame
identity. No board image links it; never flash `mac_stamp_test.ihx`.

The [bounded interval radio attempt](docs/MAC_ATTEMPT.md) (#84) now separates
idle FIFO preparation from one hardware-qualified CCA1/TX and post-TX receive
operation. Its explicit profile uses a source-backed minimum packet duration
and positive completion observations to bound time, preserving fine phase.
It returns original frame/CRC metadata with atomic publication and retained
faults, not guessed captured timestamps or ACK/NO_ACK confirmations.
Response reception is unfiltered/AUTOACK-off, not a complete POLL lease.
This is host-tested, image-checked and simulated; hardware timing, calibrated
CCA and an interval-aware protocol adapter remain separate.
No board image links it; never flash `mac_attempt_test.ihx`.

## Offline MAC transmission state

The [bounded MAC scheduler](docs/MAC_TX.md) adds unslotted CSMA-CA, legacy
ACK/DSN matching and retransmission state for one copied unsecured DATA
frame or selected Beacon/Association/Data Request command,
using the real MAC codec. Association Requests use capability `88`/`8C`
(receiver-on ED, address allocation and truthful caller power metadata);
their ACK means MAC receipt, not successful association or membership.
Data Requests require both addresses and a nonbroadcast compressed PAN;
their ACK/Pending does not implement polling or response retrieval.
Request transmission alone does not scan a channel or receive Beacons.
Backoff, five CCAs per attempt, up to four
transmission attempts, transaction/work limits and confirmed-quiescence
cleanup are explicit. Retries reuse the frame and DSN; no ACK does not prove
that the peer received nothing.

`make test-mac-tx` is **host-tested, image-checked and simulated**. Its clock
is abstract 32-bit symbols, not the raw Sleep Timer. There is no real radio
adapter with captured TX/ACK timing. The new same-owner raw TX/RX phase does
not yet fulfill that contract; old reset-exclusive services cannot simply
be chained. The isolated image's
measured IRAM headroom is not full-stack or ISR-nesting acceptance.
No board image links this scheduler; never flash `mac_tx_test.ihx`.

The separate [Beacon candidate collector](docs/NWK_CANDIDATES.md) keeps four
copied preliminary records using the real MAC/NWK Beacon decoders. It requires
explicit channel/CRC metadata, PRO profile2, BO15, Association Permit and ED
Capacity; valid changes update or withdraw a record, while malformed input
and a full table cannot overwrite unrelated entries. `make test-nwk-candidates`
is **host-tested, image-checked and simulated**. This is not active scanning,
ranked parent selection, freshness, association or authenticated membership.
Its standalone executable is excluded from board firmware and uploads.

The separate [bounded parent selector](docs/NWK_PARENT.md) now chooses from
that immutable table using an explicit target network, supplied link costs
and eligibility, and conservative wrap-aware Update ID ordering. Ambiguous
or cyclic IDs fail instead of selecting by arrival order; profile2 depth is
not ranked. `make test-nwk-parent` is **host-tested, image-checked and simulated**,
not a join procedure or calibrated link-quality source.

The [offline active-scan controller](docs/MAC_SCAN.md) adds ascending channel
walking, real Beacon Request/DSN/CCA state, confirmed receive-window handling
and copied candidate collection. It preserves separate sent/unscanned masks
and overflow diagnostics; release requires confirmed restoration of the saved
PAN/channel/filter/RX state. `make test-mac-scan` is **host-tested,
image-checked and simulated**, not an operating scanner on a board.
Its six-module test image leaves 3201 bytes below the 32-KiB CODE limit:
this is not complete-stack fit. A real adapter, captured timing (#40), full
parent selection and association remain open; never flash `mac_scan_test.ihx`.

The [Association Response context](docs/MAC_ASSOCIATION.md) adds bounded,
one-shot metadata matching through the real codec. It checks the selected
PAN/channel/local IEEE and a known coordinator IEEE; a source learned after
short-address selection is explicitly **unbound**, not authenticated.
An explicit R22 receive profile additionally recognizes uncompressed Responses
and permits a broadcast destination PAN only with the selected source PAN
actually present. Default decoding and the legacy POLL path remain unchanged.
`make test-mac-association` is **host-tested, image-checked and simulated**:
15,086 CODE bytes and696 ordinary XDATA +64 reserved in its separate test
image. These are measured test-image sizes, not total device usage or
full-stack fit. The context does not transmit retrieval requests, deliver the
required receiver ACK, restore radio state or complete association.
Never flash `mac_association_test.ihx`.

The separate [legacy Data Request extraction controller](docs/MAC_POLL.md)
leases that same transmitter for one conditional POLL transaction. It retains
the accepted ACK's captured end, handles Pending0/1 and a caller-valid configured
frame-wait PIB, and copies DATA or commands without conflating command delivery
with POLL SUCCESS. Commands require independent caller dispatch; the genuine
test forwards Association Responses to the real response context, with its
separate lifetime. `make test-mac-poll` is
**host-tested, image-checked and simulated**, with 56 shared scenarios,
including all52 original cases. An explicit per-call R22 receive path now
forwards supported uncompressed Responses without broadening DATA filtering
or changing default legacy admission.
Loss-free closure, continuous RX, immediate receiver ACK and applicable IFS
are explicit adapter obligations, not implemented radio services. This is not
repeated polling, complete association, membership or full-stack fit.
Never flash `mac_poll_test.ihx`.

The [staged association controller](docs/MAC_JOIN.md) (#83) now executes
Request -> accepted ACK -> decision wait -> Data Request -> contextual Response
through those real components and one shared DSN/generation/IFS owner.
A Response is retained even when generic POLL reports NO_DATA/COMMAND or later
cleanup fails. Whole-attempt limits produce local aborts, not a guessed IEEE
association timeout; receiver ACK and restoration still require truthful
adapter confirmations. This is host-tested, image-checked and simulated,
not complete MLME, address/parent installation, membership or authenticated
Zigbee joining. Its 22 test images reach 32763/32768 CODE and SP7C/7C;
that five-byte CODE margin is not complete-stack headroom. It does not convert
raw interval observations into captured events or link a radio adapter.
No board image links it; never flash `mac_join_<n>_test.ihx`.

## Isolated reserved flash reader

The [first flash slice](docs/ARCHITECTURE.md#reserved-flash-read-foundation)
reads 1..32 bytes from reserved pages125/126 after strict chip, controller,
mapping and buffer checks. It restores the original bank before publication
and retains failures without publishing partial data. Lock/configuration and
information pages are excluded. Evidence is **host-tested, image-checked and
simulated only**; `make test-flash` never accesses a device.
This reader has no write entry points; it is unchanged by the writer below.
Never flash the isolated `flash_test.ihx`.

The separate [internal RAM command executor](docs/ARCHITECTURE.md#internal-ram-flash-command-executor)
now has host, exact-image and alias-aware simulator evidence
(`make test-flash-exec`). It executes copied instructions, returning only
after controller quiescence or retaining a RAM-only fail-stop. Controller-idle
is not verified flash contents. Never flash its standalone `flash_exec_test.ihx`.

The [public reserved-page writer](docs/ARCHITECTURE.md#verified-reserved-page-erase-and-program)
now issues erase/program commands through that engine and verifies the
result with the real reader. Programming requires a fresh verified erase
in the current runtime epoch and permits only one attempt per word, even
for allFF data; reset does not make erased-looking words safe to reuse.
`make test-flash-write` is **host-tested, image-checked and simulated only**.
There is still no hardware write/erase acceptance or security-counter
persistence. The separate generic journal below has offline evidence only.
Never flash the
standalone `flash_write_test.ihx`.

The [generic two-page snapshot journal](docs/NV_RECORDS.md) now stores one
1..128-byte opaque record using a version, nonwrapping generation, full-page
CRC and a separate commit-last word. Replacement erases only the inactive
page through the real flash services; damaged-page fallback requires visible,
explicit recovery. `make test-nv-record` is **host-tested, image-checked and
simulated**, including command cuts/reset recovery, not physical durability.
Its bounded runtime erase accounting does not establish lifetime wear.
It is not a security-counter/key/membership service or a board image.

### Boot-disarmed flash board fixture — offline only

`IMAGE=flash_fixture` is now a separately selected **non-RF**, boot-disarmed
fixture using the real services above. Two distinct mailbox packets, each
within256 foreground polls, select one reserved page; success performs one
erase and two verified word programs, with explicit UNKNOWN/USED history
checks and terminal END/FAULT. Default boot/resume/reset cannot erase.
There is no automatic hardware runner or fault retry.

```sh
make BOARD=generic IMAGE=flash_fixture test-flash-fixture
make BOARD=lg_esl29_rev03 IMAGE=flash_fixture test-flash-fixture
```

Both variants are **host-tested, image-checked and simulated only** (4168/4208
CODE bytes,500 XDATA reserved, unchanged512-byte budget).
[Scratch/recovery, byte ABI and debugger visibility blockers](docs/FLASH_FIXTURE.md)
are explicit. Physical execution remains blocked pending new board/scratch/
backup/destructive-scope authorization and independent full excluded-region
verification. No backups, devices or identities were accessed; #8 stays open.

## Intended scope

Independent offline work also includes a [bounded legacy MAC codec](docs/MAC.md):
DATA/ACK plus association request/response, disassociation, data request and
beacon request payloads/frames, plus version-0 Beacons without GTS descriptors
(pending-address lists and opaque upper-layer payloads, not network discovery).
The separate [R22 NWK Beacon decoder](docs/NWK.md) parses the 15-byte Zigbee
metadata within that payload, without accepting a profile, network or parent.
The independent [NWK Data codec](docs/NWK.md#nwk-data-frame-codec) handles
bounded unsecured unicast/broadcast frames with optional IEEE addresses;
security and extended routing layouts fail explicitly.
The independent [APS Data codec](docs/APS.md) adds normal-unicast headers,
endpoint/profile/cluster metadata and opaque payload, with a real offline
MAC/NWK/APS composition test. APS security, broadcast/group delivery and
extended headers fail explicitly; ACK request does not implement transactions.
The [ZCL Revision 8 wire codecs](docs/ZCL.md) add global/cluster-specific
headers and 38 wire-value types, including 8..64-bit byte representations
and short strings. A separate [read-only attribute model and unicast Read
Attributes handler](docs/ZCL.md#read-only-attributes-and-read-attributes)
adds bounded lookup, access/space-error records and response construction.
The [one-cluster unicast dispatcher](docs/ZCL.md#discover-attributes-and-unicast-dispatch)
adds sorted Discover Attributes pages, routes Read requests, reports received
Default Responses and builds unsupported-command errors without response loops.
It adds no registered cluster, reporting or network dispatcher.
Unreviewed ZCL errata remains a conformance risk, not a development stop.
APS/ZCL composition and the read handler are target-tested; the complete
MAC/NWK/APS/ZCL Discover-then-Read request/response chain is host-tested and
also runs in a single SDCC resource image.
The separate [read-only Basic provider](docs/ZCL_LAB.md) supplies six
primary-backed attributes through those handlers, with caller-owned copied
strings and synthetic lab vectors. Its both-board host/image/simulator
evidence is not an advertised endpoint: profile/device selection and
networked reporting remain open.
The independent [Identify procedure](docs/ZCL_IDENTIFY.md) now supplies a
caller-owned logical-time countdown, unicast Identify/Query and real
Read/Discover handling. It has no physical indication, client/group/broadcast
handling, network send or authenticated endpoint. The bounded
[foundation write family](docs/ZCL_WRITE.md) now returns proper Basic
read-only/type/missing-attribute errors and actually updates RW IdentifyTime,
including Undivided rollback and No Response silence. Unsupported value
extents and complete application/cluster conformance remain explicit.
The separate [synthetic Temperature Measurement model](docs/ZCL_TEMPERATURE.md)
adds caller-fed values, Read/Discover/read-only Write handling, Configure/Read
Reporting Configuration and bounded interval/change-threshold reporting.
Preparing Report Attributes only leases bytes; the baseline advances after
the caller confirms their issuance, not merely after serialization.
No physical sensor, destination resolution, transport, persistence or
advertised endpoint is supplied. Three isolated SDCC compositions retain the
complete wire/configuration/reporting corpus without enlarging older budgets.
This foundation is host-tested, image-checked and simulated, not linked into
board firmware. The separate passive receiver does not provide a transmit
driver, functioning MAC, association or Zigbee join.

`make test-protocol-budget` checks that integrated image and generates a
[per-subsystem resource ledger](docs/ARCHITECTURE.md#integrated-protocol-resource-budget):
23,541 CODE bytes and1,639 ordinary XDATA bytes plus64 reserved.
The [production-only ZCL refactoring](docs/ZCL.md#production-code-headroom)
saves1,034 linked CODE bytes, including861 in production objects, with every
caller/vector retained and no raised budget.
This is a measured baseline for the implemented protocol subset, **not a
claim that radio, security/NV, ZDO and the final application already fit**.

- C99 and SDCC, initially CC2530F256.
- One logical end-device implementation: receiver-on first, sleeping later.
- Centralized Trust Center networks as the first interoperability target.
- Zigbee Core R22 / Zigbee-3.0-era behavior as the engineering baseline.
- No routing, child management or Trust Center server implementation.
- Static bounded storage, hardware AES support and durable security counters.
- Later: a low-RAM e-paper/sensor example on the LG Innotek 2.9-inch ESL board.

An end device still needs MAC, NWK, APS, security, ZDO and application behavior.
There is no claim of Zigbee certification or interoperability until the
corresponding validation gates are met.

## What has already informed this project

In earlier local work, a separate SDCC prototype was visually confirmed on
a monochrome display attached to an LG Innotek ESL board, including a streamed
page of 593 visible ASCII glyphs using
144 bytes of nonaliased XDATA. Those prototype numbers are **not measurements
of the future Zigbee stack**, and that display firmware is not part of this
bootstrap. This is background context, not independently reproducible hardware
evidence shipped in this repository; private photos/dumps are not imported.

That work also confirmed an important CC2530 constraint: XDATA
`0x1F00..0x1FFF` aliases the 256-byte internal RAM. It is not another 256 bytes
of free storage. See the [memory contract](docs/ARCHITECTURE.md#memory-contract).

## License and boundaries

Unless explicitly marked otherwise, original project code and documentation
are BSD-3-Clause; see [LICENSE](LICENSE).
The separate [OpenOCD patch and linked driver probe](tools/nrf_openocd/PROVENANCE.md)
are GPL-2.0-or-later host-programmer integration, not CC2530 firmware.
Third-party code requires per-file provenance and preserved notices before
it is imported. The BSD-licensed Contiki radio reference has now had a
[separate upstream build and reuse evaluation](docs/PROVENANCE.md#contiki-cc2530-reference-evaluation),
but no code was imported. Linking after two compatibility edits did not
establish a working port; retained defects and integration limits are recorded.
**Contiki is not a Zigbee stack**.

Do not contribute OEM flash dumps, private packet captures, network keys,
install codes, device-unique factory records or proprietary SDK binaries.
The [source policy](docs/PROVENANCE.md) is part of the project design.
