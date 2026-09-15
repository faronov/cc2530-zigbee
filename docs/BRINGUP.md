# M0 bootstrap contract

This is a non-networking C/SDCC execution fixture. **This new image has not
been flashed or tested on hardware.** The earlier external display prototype
does not validate this image.

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

## Execution and board policy

SDCC sets up its initial stack, then calls `_sdcc_external_startup`. This hook
disables all three interrupt-enable registers before board policy and C data
initialization. There is no interrupt handler, RF/USART initialization, sensor
read, EPD/SPI command, sleep entry, crystal switch or network operation.

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
