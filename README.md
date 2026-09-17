# cc2530-zigbee

[![SDCC CI](https://github.com/faronov/cc2530-zigbee/actions/workflows/ci.yml/badge.svg)](https://github.com/faronov/cc2530-zigbee/actions/workflows/ci.yml)
[![License: BSD-3-Clause](https://img.shields.io/badge/license-BSD--3--Clause-blue.svg)](LICENSE)

An experimental, open C/SDCC project aiming to implement a small Zigbee end
device and, subsequently, a sleepy end device on the TI CC2530.

**Current status: bootstrap, guarded hardware-debugging tools and offline components.
This is not yet a working Zigbee stack.** The included firmware does not join a network,
transmit radio packets, read a sensor or refresh a display. It does not require
IAR or proprietary TI stack libraries.

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
and all six board images without uploading standalone test executables.
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
on-air operation are not validated. The latest LG run left the FIFO fixture
halted at READY `0x016A`, XOSC32 selected, IRQs disabled and both FIFOs empty.
All ten older BINs and historical evidence remain unchanged; CI has twelve
board/image jobs with the same artifact whitelist. M2 #4 stays open.

## Intended scope

Independent offline work also includes a [bounded legacy MAC codec](docs/MAC.md):
DATA/ACK plus association request/response, disassociation, data request and
beacon request payloads/frames.
It is host-tested, image-checked and simulated in an isolated test executable,
not linked into board firmware. There is still no on-air radio driver, functioning
MAC, association or Zigbee join.

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

Original project code and documentation are BSD-3-Clause; see [LICENSE](LICENSE).
Third-party code requires per-file provenance and preserved notices before
it is imported. The planned Contiki radio reference is BSD-licensed, but
**Contiki is not a Zigbee stack**.

Do not contribute OEM flash dumps, private packet captures, network keys,
install codes, device-unique factory records or proprietary SDK binaries.
The [source policy](docs/PROVENANCE.md) is part of the project design.
