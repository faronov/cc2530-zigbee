# Banked CODE foundation

This is an isolated CC2530F256/SDCC foundation, not a networking firmware
release or evidence that the complete MAC/security/NWK/APS/ZDO/BDB stack
fits. Existing common-CODE images retain their `0x8000` limits. Banking
does not enlarge DATA, the call stack, or ordinary XDATA.

```sh
make BOARD=generic test-banked
make BOARD=lg_esl29_rev03 test-banked
```

Artifacts are under `build/<board>/banked/` (or `$(BUILD)/banked/`).
The [exact ABI contract](BANKED_ABI.md) describes the original trampolines,
typed constants, supported signatures, ISR storage separation and errors.
Only the explicitly measured fixture is admitted, not arbitrary C callbacks
or automatic banking of unchanged protocol modules.

## Address identities and publication

SWRU191F pp27,33-34 specifies a fixed physical bank0 at CPU CODE
`0000..7FFF` and an FMAP-selected 32-KiB bank at `8000..FFFF`.
FMAP (`9F`) resets to1; its low three bits select banks0..7.
MEMCTR (`C7`) independently selects the bank at XDATA `8000..FFFF`.
Its XMAP bit overlays SRAM at CODE `8000..9FFF`, including the physical
IRAM alias at `9F00..9FFF`. CODE `A000..FFFF` still sees selected flash.

The separate linker/packer contract uses three distinct identities:

| Identity | Common | Bank N, N=1..7 |
| --- | --- | --- |
| Linker virtual | `0000..7FFF` | `(N << 16) + 8000..FFFF` |
| CPU logical | `0000..7FFF` | `8000..FFFF` |
| Physical flash | `0000..7FFF` | `N * 8000 + (logical - 8000)` |

`tools/banked_image.py` rejects noncanonical addresses, including the
bank0 upper-window alias and bankN lower-window aliases. It excludes all
physical bytes at or above `3E800`: NV pages125/126 and the entire
lock/config page127. The information page is not part of this image.
The linked `banked.ihx` is **not a physical flashing HEX**.
`banked.hex` is the sparse physical representation; absent bytes remain
absent, not padding that might erase or program another region.
`banked-layout.json` records explicit bank/logical/physical identities
and address-sensitive hashes. No automatic flashing or artifact upload
is added by this profile.

A logical PC alone cannot identify upper-window code. These offline
identities do not expand the existing hardware debugger's unbanked CODE
read/breakpoint contract. Physical bank discrimination, recovery and
flash programming need separately authorized hardware evidence.

## Simulator mapping

The existing 15-second `boot_image.simulate` deadline is retained.
uCsim uses an actual 256-KiB backing chip, a fixed common decoder,
an FMAP-controlled CODE banker and a separate MEMCTR-controlled XDATA
banker. Real 8051 instructions still fetch through the 64-KiB CPU CODE
view; there is no flat 256-KiB CPU or synthetic far-call return.
The XDATA `1F00..1FFF` alias shares the actual simulator IRAM.

XMAP is a modeled hardware effect applied at audited real MEMCTR writes,
not at function entry or by skipping the copied RAM engine. The model
replaces the whole upper-window banker before installing the SRAM
overlay and the remaining `A000..FFFF` flash banker. This is necessary
because the pinned uCsim's partial banker splitting does not safely
preserve its selector. Restoration installs the full FMAP banker again.
Standalone probes exercise all eight CODE and independent XDATA banks,
common invariance, the SRAM/IRAM overlays, FMAP changes during XMAP,
and restoration. Fixed-bank and missing-alias negative controls must fail.

These are synthetic mapping/controller facts. They do not establish
physical oscillator timing, flash endurance, RF behavior or observed
CC2530 interrupt-source timing.

The pinned uCsim also has a **bank-window disassembly label bug**, separate
from instruction execution. Its banker's null direct `memchip` reaches
the variable/label lookup path. The runner removes simulator names and
disables automatic analysis, then attaches `commands analyze` to permanent
bank-window breakpoints before clearing them. Temporary breakpoints do not
run this cleanup before formatting in this version. This changes neither
CODE nor CPU/RAM state; an exact before/after snapshot regression enforces
that. Common-only instruction sweeps retain ordinary simulator names: the
numeric workaround is not a universal disassembler repair. It does not add
a debugger backend or replace/increase the simulator.
The relevant packaged sources are
[mem.cc](https://sources.debian.org/src/sdcc/4.2.0%2Bdfsg-1/sim/ucsim/sim.src/mem.cc/),
[var.cc](https://sources.debian.org/src/sdcc/4.2.0%2Bdfsg-1/sim/ucsim/sim.src/var.cc/),
[uc.cc](https://sources.debian.org/src/sdcc/4.2.0%2Bdfsg-1/sim/ucsim/sim.src/uc.cc/)
and [uc51.cc](https://sources.debian.org/src/sdcc/4.2.0%2Bdfsg-1/sim/ucsim/s51.src/uc51.cc/).

## Bounded acceptance

| Resource | Linked fixture |
| --- | ---: |
| Populated CODE across common/banks1,2,7 | 4,676 bytes |
| Ordinary XDATA | 224 bytes |
| Status reservation | 64 bytes |
| Occupied non-stack IRAM | 26 bytes |
| Explicit SSEG | `22..7c`, 91 bytes |
| Initial SP / normal full-transcript peak | `21` / `3d` |

Host checks exercise every canonical physical CODE address and sparse HEX
round trips, including malformed records and all excluded NV/lock bytes.
The linked proof pins address-sensitive complete CODE, all parsed symbols,
raw CDB before decoding, memory report, six immediate relocated listings
and six compiler objects. It accounts for every emitted CODE area,
instruction boundary, absolute/relative transfer, MOVC site, FMAP writer,
RETI, XDATA prefix, DATA/overlay and stack reservation. The genuine flash
object and 123-byte RAM engine have independent identities.

The current corpus includes23,399 artifact/address/metadata mutations,
26 terminal runtime negatives,16 explicit constant-helper error cases and
600 interrupt-boundary cases. These comprise396 trials across all five
foreground call/return paths, including the bank7-specific admission branch,
and204 trials across mapping/copy/restoration for banks0,1,2,7. Each executes the actual
common ISR, banked IRQ-only leaf and RETI. Normal nested calls, the three-byte
function pointer and constants at the last usable bank7 bytes have exact
independent results.

The complete transcript executes bank7 -> common flash service -> copied
RAM -> common confirmed-idle restoration -> bank7 -> caller. FADDR, command,
four data bytes and copied instruction bytes are checked, not replaced by
a successful service return. Separate transcripts retain an exhausted busy
command in RAM even after synthetic busy clears, and trap an idle-error
return in common CODE with XMAP retained. Every transcript retains ordinary
XDATA, lower-IRAM-gap and upper-IRAM/stack guards. Corrupted-SP entry tests
specifically reject before any push can wrap into register bank0.

**Evidence:** host-tested packing/tool behavior, image-checked linked ABI
and simulated CPU/mapping/controller execution. Both board definitions have
dedicated CI jobs; the profile performs no board GPIO initialization and
is never uploaded or flashed. This is not whole-stack fit, RF join,
CC2530 interrupt-source integration, hardware-observed banking or physical
flash acceptance. CPU register-bank0/DPS0 remain normal C caller
preconditions; a state check inside a compiled C function does not make
arbitrary entry calling conventions safe.

The [banked security composition](BANKED_SECURITY.md) is the next, separate
real-service profile. It does not replace this foundation's IRQ/pointer/
bank7/constant/failure corpus or turn its synthetic fixture into board firmware.
