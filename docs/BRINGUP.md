# M0 bootstrap contract

This is a non-networking C/SDCC execution fixture. **The standalone `bringup`
images have not been flashed or tested on hardware.** The earlier external
display prototype does not validate these images.

## Build and scope

Use the toolchain and commands in the [README](../README.md). `make all test`
builds the actual SDCC image, checks its layout and formats, runs host C tests,
and executes it in an alias-aware uCsim fixture. `BOARD` is `generic` (default)
or `lg_esl29_rev03`; outputs are under `build/<board>/`.

The generated files are `bringup.ihx`, `.hex`, `.bin`, `.map`, `.mem`, `.cdb`
and `build-info.json`. Metadata records toolchain, source revision/dirty state,
hashes and memory accounting. A build before the first commit has a null
revision; it is not presented as a reproducible committed release.

`SDCC`, `HOST_CC`, `PYTHON`, `S51` and `BUILD` are overridable Make variables.
SDCC's `packihx` and `makebin` must also be on `PATH`. Python >=3.9 is required.
There is deliberately no automatic flash target.

`IMAGE=bringup` remains the default. `IMAGE=debug_fixture` selects the separate
[M1 target fixture](DEBUGGING.md#implemented-target-fixture), with outputs in
`build/<board>/debug_fixture/` unless `BUILD` is specified.
`IMAGE=timebase_fixture` selects the separate
[awake-only timebase board fixture](DEBUGGING.md#awake-only-timebase-board-fixture),
under `build/<board>/timebase_fixture/`. `IMAGE=clock_fixture` selects the separate
[init-time clock board fixture](DEBUGGING.md#init-time-clock-board-fixture),
under `build/<board>/clock_fixture/`. `IMAGE=irq_fixture` selects the separate
[Timer1 IRQ board fixture](DEBUGGING.md#timer1-irq-board-fixture),
under `build/<board>/irq_fixture/`. All five images use the same board
policy, M0 status ABI and memory restrictions. The new timebase fixture does
not change the existing `bringup` or `debug_fixture` firmware bytes. The clock
fixture likewise preserves all six older board BINs; the IRQ fixture preserves
all eight older BINs. The IRQ fixture has separate bounded LG hardware
acceptance below; generic remains host/image/simulator-only. Image selection
does not enable any USB, flashing or RF operation. When reusing a custom `BUILD`,
`build-info.json` always describes the last selected image; use separate
directories to retain each metadata record.

Shared startup/status has separate LG hardware evidence through the M1
fixture and the [2026-09-16 compiled-C timebase acceptance](DEBUGGING.md#2026-09-16-lg-compiled-c-timebase-acceptance).
That timebase run left READY `0x016A`. The
[later LG clock experiment](DEBUGGING.md#2026-09-16-lg-clock-cancellation-failure)
passed normal switching but failed pending-cancellation rollback acceptance.
The corrected 3,798-byte LG image subsequently passed
[2026-09-17 (UTC+03) compiled-C clock acceptance](DEBUGGING.md#2026-09-17-lg-compiled-c-clock-acceptance):
both timeout/rollback cases and a separate reset/recovery run of 257 sequences
(771 C calls). That clock run ended halted at READY `0x016A` on RC16.
Neither standalone `bringup` image nor generic hardware was observed.
All six older BIN hashes and historical M1/timebase evidence
remain unchanged. This is not frequency/calibration or physical clock-failure
acceptance, and result 9 still denotes unconfirmed never-departed cancellation.

The unchanged 3,269-byte LG IRQ image subsequently passed
[compiled-C Timer1/IRQ acceptance on 2026-09-17 (UTC+03)](DEBUGGING.md#2026-09-17-lg-compiled-c-irq-acceptance):
three initial normal cycles, an independent pre-start TIMEOUT at FAULT,
then 257 real C cycles/ISR services after a separate explicit reset.
Every acceptance invocation independently verified all physical CODE,
including FF padding, before runner resume. Hardware return was restore+12
with DPL=OK (0), with preserved CPU/active-IRAM context and actual RETI.
The live board is now **IRQ fixture at READY `0x01BB`, EA/T1IE disabled and
Timer1 stopped**, not the historical clock image. All eight earlier BIN
hashes and the published EA/timebase drivers remain unchanged. This finite
result does not establish calibrated time/latency, true one-shot behavior,
exact overflow counts, higher-priority nesting or other platform services.
M2 #4 remains open.

## Execution and board policy

SDCC sets up its initial stack, then calls `_sdcc_external_startup`. This hook
disables all three interrupt-enable registers before board policy and C data
initialization. The bootstrap has no interrupt handler, RF/USART initialization, sensor
read, EPD/SPI command, sleep entry, crystal switch or network operation.
The separate clock fixture performs its explicitly documented HF selections
after this unchanged startup; the default bootstrap does not.
The separate IRQ fixture subsequently enables only its owned Timer1 source
and global EA in bounded stages, with a stopped counter before delivery.
It leaves GPIO selection/routing unchanged and does not change the startup
interrupt/clock evidence stored in M0.

The default board does not change GPIO latches, directions, selections or
pulls. That is not a guarantee of electrically safe reset levels on an unknown
board: external pulls, attached hardware and reset behavior still matter.

The LG Rev0.3 policy first drives display power P0.7 low. It then drives
P0.2-P0.5 and P1.1 low, selecting digital GPIO and establishing the inactive
latches before enabling outputs. CS is low with power/reset held off, not an
active display transaction. Unrelated pins and global routing are preserved.
P1.2 BUSY, P1.3-P1.5 straps, P1.6/P1.7 UART/future I2C, NFC pins P0.0/P0.1 and
P2.1/P2.2 debug pins are not claimed. P1.1 has no configurable internal pull.
This is not a battery-current optimization or a general shutdown routine for
a display already performing a refresh.

Only a confirmed LG board may use that image. No physical board or connected
working display was altered as part of creating this repository.

## Fixed debugger status ABI v1

The active block is **32 bytes at XDATA `0x1E00..0x1E1F`**, within a 64-byte
reservation ending at `0x1E3F`. The unused reservation and `0x1E40..0x1EFF` are
not initialized or allocated by M0. Read the byte-size field, not all 64 bytes
as if they were valid status.

All offsets below are decimal. All fields are bytes, so no struct packing or
multibyte endianness is involved.

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | Signature `4D 30 43 43` (`M0CC`) |
| 4 | 1 | ABI version `1` |
| 5 | 1 | Active size `32` |
| 6 | 1 | Phase: `1` initializing, `2` bootstrap ready |
| 7 | 1 | Board: `0` generic, `1` LG Rev0.3 |
| 8 | 1 | Foreground-loop heartbeat, modulo 256 |
| 9 | 1 | Policy: bit 0 = LG display-off policy applied |
| 10 | 3 | P0/P1/P2 sampled values after early policy |
| 13 | 3 | P0DIR/P1DIR/P2DIR |
| 16 | 3 | P0SEL/P1SEL/P2SEL |
| 19 | 3 | P0INP/P1INP/P2INP |
| 22 | 1 | APCFG |
| 23 | 1 | PERCFG |
| 24 | 1 | CLKCONCMD |
| 25 | 1 | CLKCONSTA |
| 26 | 3 | IEN0/IEN1/IEN2, all disabled |
| 29 | 3 | Reserved, zero |

The signature and ready phase are published after the snapshot is populated.
Readers require the exact signature/version/size and phase 2. Halt the CPU
for a coherent snapshot and exclude a simultaneous reset; this is not a
transactional cross-reset protocol. Port values are sampled pin values, not
a proof of measured rail voltage or the internal output latch.

The heartbeat is not a timer or boot counter: it resets on initialization,
wraps rapidly and has no frequency promise. A ready phase means only that
the bootstrap initialized its snapshot. It never means joined, radio-ready
or that a physical panel is working.

## Memory and evidence limits

The linker places ordinary XDATA strictly below `0x1E00` and bounds CODE to
the fixed lower 32 KiB. It uses CC2530 MPAGE at SFR `0x93` for SDCC's runtime
page register; the generic 8051 default P2 must not be used.

The absolute status is omitted from SDCC's ordinary `.mem` XDATA total.
The checker therefore counts its 32 used bytes and full 64-byte reservation
explicitly. The M0 reservation budget is <=512 nonaliased XDATA bytes; it is
not permission to use the IRAM alias as extra RAM. See the generated metadata
for exact code and stack reservations.

The simulator uses a C52 instruction engine with an explicit memory decoder:
XDATA `0x1F00..0x1FFF` maps to the **same IRAM backing storage**, including
register banks and stack. An original synthetic MOVX fixture must corrupt R7
through `0x1F07`; a reverse alias check also runs. The actual linked image then
boots with this mapping, reaches its real main/tick symbols and must satisfy
the status, bounded heartbeat, unallocated-XDATA and upper-stack guards.
Negative host fixtures cover malformed HEX, wrong status placement/size,
overlapping allocator areas, budget overflow and incorrect page/stack setup.
Copies of the actual artifacts are mutated to check rejection of wrong-board,
BIN/HEX, status, stack and upper-CODE errors. The synthetic alias test must fail
when the decoder is deliberately omitted.

This does not simulate CC2530 GPIO electrical behavior, RF, clock timing,
banked execution, debug USB, power modes or analog circuitry. The upper-stack
guard is a bound observed on this boot path, not a whole-program stack-depth
proof. Hardware acceptance remains a separate task.
