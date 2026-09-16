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
image was flashed. Shared startup was exercised physically only through the LG fixture.

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

## Intended scope

Independent offline work also includes a [bounded legacy MAC codec](docs/MAC.md):
DATA/ACK plus association request/response, disassociation, data request and
beacon request payloads/frames.
It is host-tested, image-checked and simulated in an isolated test executable,
not linked into board firmware. There is still no radio driver, functioning
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
