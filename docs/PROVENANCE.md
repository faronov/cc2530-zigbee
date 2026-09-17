# Sources, licensing and public-data policy

## Original work

Original project code and documentation are BSD-3-Clause. Hardware register
addresses, protocol field definitions and other functional facts are not an
excuse to copy an implementation whose license has not been checked.

At M0, no third-party radio or Zigbee stack implementation is vendored.
Standard compiler/runtime dependencies retain their own upstream licenses.
BSD-3-Clause does not relicense SDCC, its runtime, uCsim, or external tools.

The M1 debugger fixture and its test patterns are original BSD-3-Clause work.
Its MOV/NOP/RET probe uses ordinary 8051 instructions and synthetic constants,
not bytes from an OEM image or an external programmer/debugger implementation.
No USB backend, programmer code or additional dependency is imported by it.

## Reviewed reference candidates

| Reference | Status and permitted use |
| --- | --- |
| [TI CC2530](https://www.ti.com/product/CC2530) and [SWRU191F](https://www.ti.com/lit/pdf/swru191) | Primary hardware facts; link/cite documentation rather than redistribute whole manuals |
| [Contiki CC2530 RF driver](https://github.com/contiki-os/contiki/blob/32b5b17f674232867c22916bb2e2534c8e9a92ff/cpu/cc253x/dev/cc2530-rf.c) | BSD-3-Clause terms verified in this file; planned selective adaptation, not yet imported |
| [Contiki CC253x build](https://github.com/contiki-os/contiki/blob/32b5b17f674232867c22916bb2e2534c8e9a92ff/cpu/cc253x/Makefile.cc253x) | Explicit SDCC, `0x1F00` XDATA limit and banking evidence; not proof of Zigbee support |
| [Legacy open ZBOSS](https://github.com/niclash/zboss) | Reference candidate requiring per-file license/revision review; not a proven ready CC2530/SDCC modern stack |
| [Public LG ESL demo](https://github.com/cddwx525/cc2530_esl_demo/tree/eca70ae29c8f6c4b1bdc58b99c1192646e74d469) | Hardware reference only; no declared repository license was established, so do not copy its implementation/fonts/images |
| [SDCC](https://sdcc.sourceforge.net/) | Build tool, with its own licenses; baseline 4.2.0 |
| [cc-tool](https://sourceforge.net/projects/cctool/) | External GPL programmer; not vendored or relicensed as BSD code |

### M1 host transport sources

The original BSD-3-Clause host transport initially used **functional wire facts only**
from the public `dashesy/cc-tool` revision
`0d84df329e343e2ea5a960c04a3d4478ee039aa0`. The inspected files declare GNU
GPL v2. No implementation, instruction macro sequences, fixtures or notices
from that programmer are copied into this project:

| Public reference | Facts used |
| --- | --- |
| [cc_programmer.cpp](https://github.com/dashesy/cc-tool/blob/0d84df329e343e2ea5a960c04a3d4478ee039aa0/src/programmer/cc_programmer.cpp) | CC Debugger USB `0451:16A2`, configuration 1/interface 0, bulk OUT `04` / IN `84`; vendor IN `GET_STATE` request `C0`, value/index zero, 8-byte response with three little-endian 16-bit fields at offsets 0/2/4 |
| [cc_unit_driver.cpp](https://github.com/dashesy/cc-tool/blob/0d84df329e343e2ea5a960c04a3d4478ee039aa0/src/programmer/cc_unit_driver.cpp) and [cc_debug_interface.h](https://github.com/dashesy/cc-tool/blob/0d84df329e343e2ea5a960c04a3d4478ee039aa0/src/programmer/cc_debug_interface.h) | Bulk diagnostic requests `1F 34` (read status), `1F 24` (read config), each followed by one input byte |
| [cc_253x_254x.cpp](https://github.com/dashesy/cc-tool/blob/0d84df329e343e2ea5a960c04a3d4478ee039aa0/src/programmer/cc_253x_254x.cpp) | Adapter target-family ID `2530` for CC2530; not a unique device identity |

These are public implementation references, **not an official TI USB protocol
specification or hardware verification**. No generalized USB debug-bytecode
encoder or programmer sequence is inferred from them. SWRU191F chapter 3
describes the target debug interface; it must not be mistaken for this USB
adapter's framing specification.

For the CPU-control addition, TI **SWRU191F** was checked directly:
Table 3-1 pp.53-54 (HALT/RESUME/STEP_INSTR/GET_BM and their results),
Table 3-2 p.55 (CC2530 debug configuration), Table 3-3 pp.55-56
(status/oscillator requirements), section 3.3.3 pp.56-57 (three breakpoint
parameter bytes) and section 3.4.1 p.57 (commands allowed when debug-locked).
HALT `44`, RESUME `4C`, STEP_INSTR `5C` and GET_BM `64` are zero-argument,
one-response-byte target commands; their three low opcode bits are don't-care
for these commands. The existing `1F` USB shape is reused for this bounded
class. The later live-access addition uses the separately observed standalone
forms described below, not a generalized encoder inferred from this class.
Hardware observations are limited to the exact LG fixture/adapter record.

The same pinned `cc_unit_driver.cpp`, `reset(bool)`, supplies the reset wire
facts: vendor OUT request type `40`, request `C9`, value `0`, index `1` for
debug mode and an empty data stage. Index `0` requests normal execution and
is deliberately not exposed. Our implementation checks explicit reset
permission, a prepared existing session, pre/post status and exact USB
completion; it does not copy the reference's reset-on-close behavior.
The pinned `cc_programmer.cpp`, `enter_debug_mode()` and
`request_device_info()`, additionally supply initial adapter preparation
facts, implemented as a separately authorized operation:

| Vendor OUT request | Value / index | Data |
| --- | --- | --- |
| `C5` | `0` / `0` | Empty |
| `C8` | `1` / `0` | 48 bytes initially filled with ASCII spaces; `CC2530` at offset 0, `DID:` at offset 16, four uppercase hex digits of USB descriptor `bcdDevice` at offset 21 |
| `C9` | `0` / `1` | Empty; request reset into debug mode |

`bcdDevice` is the cached USB device revision, not a serial number, factory
identity, `GET_STATE` firmware version or firmware revision. This M1 preparation
does not copy the reference's automatic configuration changes,
normal-execution reset or programmer algorithms. The later
[DMA-enable gate](#m2-dma-debug-configuration-sources) uses a separately observed
fixed packet, not a general configuration writer. The implementation requires
exact completion counts and fresh halted/awake status, not the reference's
broader programmer workflow. The compositions have synthetic host coverage
and the separately dated one-board hardware observations below.
No generalized multi-byte target-command USB grammar is claimed.

The breakpoint codec itself encodes only the target's documented
slot/enable/bank and CODE address fields, not adapter bytecode. The live
transport now wraps those fields in one individually observed USB form and
restricts it to lower unbanked CODE with bank parameter zero. All committed
test vectors are original synthetic data. The manual is linked, not redistributed.

Offline symbol lookup reads this project's own SDCC 4.2.0 map/CDB output and
build hashes. Its supported record subset is checked against genuine linked
images and original synthetic records, not imported debugger code. Source
lookup uses the linked `L:C$file$line$scope$block:address` records emitted by
this toolchain. Scope/block identifiers are retained, not used to infer
function ranges, instruction lengths or source stepping. No source text is
imported or resolved from CDB filenames. Snapshot decoders use the project's
documented M0/M1 ABIs, not captured device data.

The optional dependency in `requirements-debug.txt` is
[PyUSB 1.3.1](https://github.com/pyusb/pyusb/tree/89ea84d6c9e4cb81bc6e40d7df849d0cbd7dfd47),
BSD-3-Clause, reviewed via its
[license](https://github.com/pyusb/pyusb/blob/89ea84d6c9e4cb81bc6e40d7df849d0cbd7dfd47/LICENSE)
and public API. It is installed externally, not vendored. The backend uses
public methods; optional host tests exercise its pinned resource manager to
check that explicit release errors are not lost during disposal. The system
[libusb runtime](https://github.com/libusb/libusb/blob/v1.0.27/COPYING) retains
its LGPL-2.1 license; it is neither embedded in firmware nor redistributed
by this repository. Synthetic backend responses contain no real device data.

### M1 live access and manual recovery sources

TI **SWRU191F pp.27-40 and 53-57** supplied primary memory/SFR, PSW/register-bank,
dual-DPTR/DPS, debug-command, status and breakpoint facts. GET_PC returns two
bytes; supplied DEBUG_INSTR returns ACC without incrementing PC;
SET_HW_BRKPNT returns STATUS, not an undefined byte. An already-halted HALT
has an undefined reply. These target facts alone do not specify USB framing.

The public MIT `florischabert/ccd` revision
`a7a1e6be6a07bfb93694e693333491984678026c` was reviewed for fixed transfer
layouts and counts in
[target.c](https://github.com/florischabert/ccd/blob/a7a1e6be6a07bfb93694e693333491984678026c/src/target.c#L29-L243),
named constants in
[target.h](https://github.com/florischabert/ccd/blob/a7a1e6be6a07bfb93694e693333491984678026c/src/target.h#L88-L110),
and raw endpoint/count handling in
[usb.c](https://github.com/florischabert/ccd/blob/a7a1e6be6a07bfb93694e693333491984678026c/src/usb.c#L160-L182).
Its [MIT notice](https://github.com/florischabert/ccd/blob/a7a1e6be6a07bfb93694e693333491984678026c/LICENSE#L1-L19)
names **Copyright (c) 2013, Floris Chabert. All rights reserved.**
No implementation, instruction macro sequence or fixture was imported.
The fixed examples do not document a general adapter grammar or establish
standalone GET_PC/breakpoint framing.

The standalone forms actually used by our original code were then
**hardware-observed on the owned, verified non-RF LG fixture**:
`3F 28` returns two high-byte-first PC bytes; `4F 55` plus one instruction byte,
`7F 56` plus two, and `AF 57` plus three each return one ACC byte;
`AF 3F` plus three breakpoint parameters returns one STATUS byte.
The rejected experiments `2F 28` (one reply byte) and `8F 56 ...` (zero reply
bytes) are not shipped. These are bounded functional facts, not copied code,
instruction macros or a claim to reverse-engineer the whole adapter language.
See the [dated evidence and exact image hash](DEBUGGING.md#2026-09-16-lg-fixture-hardware-record).

The original BSD-3-Clause `tools/erase_boundary_fault.c` observes external
programmer traffic only. The same pinned GPL `cc-tool` provides its exact
mass-erase fact:
[erase()](https://github.com/dashesy/cc-tool/blob/0d84df329e343e2ea5a960c04a3d4478ee039aa0/src/programmer/cc_unit_driver.cpp#L127-L135)
sends `1C 14`, followed separately by
[READ_STATUS](https://github.com/dashesy/cc-tool/blob/0d84df329e343e2ea5a960c04a3d4478ee039aa0/src/programmer/cc_unit_driver.cpp#L72-L81)
OUT `1F 34` / one IN byte. The
[CC253x completion check](https://github.com/dashesy/cc-tool/blob/0d84df329e343e2ea5a960c04a3d4478ee039aa0/src/programmer/cc_253x_254x.cpp#L248-L255)
tests only that `CHIP_ERASE_BUSY` is clear. Our opt-in observer additionally
requires a fresh exact-length reply on the same handle with halted/unlocked
bits, then exits 99 before programming/normal-reset cleanup. It neither
initiates flashing nor copies a programmer implementation.

The reviewed external programmer's cleanup is a material safety boundary:
[normal task return](https://github.com/dashesy/cc-tool/blob/0d84df329e343e2ea5a960c04a3d4478ee039aa0/src/application/cc_base.cpp#L227-L253)
unconditionally calls
[unit_close()](https://github.com/dashesy/cc-tool/blob/0d84df329e343e2ea5a960c04a3d4478ee039aa0/src/programmer/cc_programmer.cpp#L256-L258),
which requests reset into normal execution, regardless of CLI `--reset`.
A [readback mismatch](https://github.com/dashesy/cc-tool/blob/0d84df329e343e2ea5a960c04a3d4478ee039aa0/src/programmer/cc_unit_driver.cpp#L301-L324)
can be [printed without propagating failure](https://github.com/dashesy/cc-tool/blob/0d84df329e343e2ea5a960c04a3d4478ee039aa0/src/application/cc_flasher.cpp#L631-L638),
allowing that reset and exit 0. USB/file exceptions before target close skip
the call; the [USB destructor](https://github.com/dashesy/cc-tool/blob/0d84df329e343e2ea5a960c04a3d4478ee039aa0/src/usb/usb_device.cpp#L108-L139)
only closes libusb resources. Our debugger does not adopt this lifecycle.
Independent physical readback, not exit status alone, is required by the
[manual recovery procedure](DEBUGGING.md#manual-hardware-acceptance-and-recovery).

The manual fixture runner and the synthetic 8051/USB and native-DYLD tests
are original BSD-3-Clause work. Native tests compile only an original fake
USB library/driver, never real libusb or `cc-tool`. The final 257-cycle
physical acceptance used isolated PyUSB 1.3.1; earlier global-1.2.1 runs are
not represented as pinned. The user explicitly authorized LG checks/reset/
flash on 2026-09-16. Flower and private GPL-derived probes were not accessed.
Private text-demo backups, factory-page captures and recovery/session files
remain outside Git and CI artifacts. No GPL implementation is imported or
relicensed. The completed bounded LG/unbanked M1 record distinguishes the
successful fresh-reconnect fixture run from the later real held-handle
cable-unplug/NO_DEVICE companion check and the final successful explicit
new-session recovery after the last replug. That full one-cycle fixture run
ended halted at `0x0173`.
This changes evidence status, not licensing or the supported API bounds.

### M2 timebase sources

The awake-only Sleep Timer reader, unsigned modular deadline arithmetic,
host latch/tick model and isolated linked-image checks are original
BSD-3-Clause work. No timer implementation, SDK object, peripheral emulator,
capture or additional dependency was imported.

Primary hardware facts are from TI **SWRU191F sections 11.1 and 11.4,
pp.129-131**: the 24-bit counter starts immediately after reset, uses the
current 32-kHz RC/XOSC source, runs except in PM3 and loses its value in PM3.
ST0 (`0x95`) reads latch all 24 bits; ST1 (`0x96`) and ST2 (`0x97`) read the
latched upper bytes. Writes set compare, not the counter. PM1/PM2 wake needs
a positive 32-kHz edge through `SLEEPSTA.CLK32K` before reliable current reads.
The [awake-only contract](ARCHITECTURE.md#awake-only-timebase-first-m2-slice)
excludes that wake synchronization and does not choose or calibrate a clock.
The manual is linked above, not redistributed. Synthetic host latching and
generic s51 SFR injection do not establish physical CC2530 counter behavior.

The separate authorized hardware reference used original supplied
`MOV A,direct` instructions to read ST0/ST1/ST2 on the unchanged M1 fixture,
with CPU-state preservation. No C timebase code was injected or executed.
The clock-source interpretation uses SWRU191F pp.68-69:
`CLKCONSTA.OSC32K=1` selects the 32-kHz RC source and `OSC=1` the 16-MHz
RC system source. The matching fixture startup snapshot was `C9` for both
clock command/status, and no clock-source writes occurred during observation.
Only the [sanitized summary](VALIDATION.md#independent-sleep-timer-hardware-reference-2026-09-16)
is published, not raw captures, factory information or device identities.

The later board timebase fixture, byte-oriented state ABI, bounded polling,
relocation checks and manual acceptance runner are also original BSD-3-Clause
work. They reuse the existing startup/board policy, C timebase and guarded
debugger APIs; no implementation, new wire framing, dependency or private
capture is imported. The 128-tick delay and 1,024-poll budget are synthetic
fixture choices, not TI timing guarantees. Host fault doubles, counter inputs
and runner records are original synthetic test data. The runner's offline
tests do not execute a physical C driver or supersede the independent
register-only hardware reference.

The [2026-09-16 compiled-C acceptance](DEBUGGING.md#2026-09-16-lg-compiled-c-timebase-acceptance)
is a separate operator-reported physical experiment on the new verified
1,847-byte LG image. External cc-tool 0.26 programming/readback preceded the
runner's independent full CODE comparison and successful 3-/257-cycle runs
of the original C driver/fixture. This does not import programmer code or
establish a project flash service. Only a processed summary is published;
raw run JSONs and rechecked private text-demo recovery copies remain outside
Git/CI and were not accessed for this documentation update. The earlier
register-only reference is preserved separately, and neither experiment
establishes calibrated timing or changes licensing.

### M2 system clock sources

The bounded init-time HF selector, diagnostic records, host clock scripts and
isolated SDCC/simulator checks are original BSD-3-Clause work. No TI SDK,
CC2430/other-part driver, programmer implementation, peripheral emulator or
new dependency is imported. Tests use original synthetic clock/counter
observations; no physical clock-switch capture or private file was accessed.

The supplied primary facts were read directly from **TI SWRU191F pp.64,
66-69**: HF source requests through CLKCONCMD.OSC and confirmation through
CLKCONSTA.OSC only after stability; undivided debug-compatible CLKSPD `001`
for RC16 / `000` for XOSC32; TICKSPD/CLKSPD clamping to the selected source;
source-change alignment with TICKSPD; and automatic RC calibration effects.
The exact registers are CLKCONCMD `0xC6`, CLKCONSTA `0x9E`, SLEEPCMD `0xBE`
and SLEEPSTA `0x9D`. CMD/STA fields are OSC32K bit 7, OSC bit 6, TICKSPD bits
5:3 and CLKSPD bits 2:0.

SLEEPCMD bit 7 is OSC32K_CALDIS, bit 2 is reserved and **must be one**, and
bits 1:0 are MODE. SLEEPSTA bits 6:5 are reserved, bits 4:3 reset reason,
and bit 0 CLK32K. CC2430-style `SLEEPCMD.OSC_PD` / `SLEEPSTA.XOSC_STB`
are **not CC2530 fields** and are not used. No SLEEPCMD write is required or
implemented for HF selection. IEN register declarations and the disabled-IRQ
bootstrap policy are reused unchanged.

Selecting XOSC32 calibrates RC16 automatically; enabled LF RC calibration can
take up to 2 ms and add one Sleep Timer tick. Confirmation of the HF source is
not LF calibration completion or precision evidence. LF source selection,
32-kHz XOSC stability, sleeping/wake behavior and calibration services remain
outside the [selector contract](ARCHITECTURE.md#init-time-system-clock-selector-isolated-m2-slice).
The existing Sleep Timer facts remain separately cited above. The linked
checker reviews the emitted ordinary 8051 instruction subset, genuine SFR
operands and calls; synthetic s51 inputs do not model analogue oscillator startup.

The subsequent clock board fixture, explicit 56-byte serialization, host/simulator
models, decoder and manual runner are original BSD-3-Clause work. That initial
fixture left both drivers unchanged. A later root fix in `clock.c` follows the
operator-supplied [pending-cancellation failure facts](DEBUGGING.md#2026-09-16-lg-clock-cancellation-failure);
the timebase remains unchanged. STA reports actual source, not cancellation
drainage. The fix tracks observed requested-source departure/return and reports
bounded uncertainty when departure was never seen, without invented oscillator
bits or a delay heuristic. No private record was read or imported.
The timeout checkpoint is derived from this
project's actual SDCC 4.2.0 IHX/map/CDB, with the full relocated deadline helper
and clock-call continuation checked, not guessed from another image or copied
from a programmer. SDCC emits an address record, but no type declaration, for
the helper's three-byte overlay scratch; the checker verifies that exact
record and OSEG allocation rather than inventing a declaration.
The live return check uses existing register snapshots and read-only IRAM alias
access. No adapter framing, hardware SFR whitelist, memory writer, dependency,
private recovery file or physical capture is added. The fixed host hold is a
host-timed test stimulus, not a calibrated tick-rate fact or oscillator-failure
experiment. A separate 127-byte observation-to-sample instruction proof
supports the late-source experiment using existing read-only APIs. New reviewed
SDCC instructions are XRL A,direct, JB ACC.6,rel and CJNE R4,#data,rel;
none adds a peripheral access. The operator-supplied
[2026-09-17 (UTC+03) compiled-C clock acceptance](DEBUGGING.md#2026-09-17-lg-compiled-c-clock-acceptance)
records the corrected 3,798-byte LG image, independent physical CODE verification,
both bounded timeout/rollback cases and a separately reset 257-sequence recovery
run. The operator checked private log timestamps; only the processed
facts are documented, with no private logs, captures, identities or factory
data imported. This record is distinct from the old image's
September 16 cancellation failure and from the earlier M1/timebase evidence.
It does not establish frequency/calibration, physical oscillator failure or
never-departed cancellation confirmation; generic remains host/image/simulator-only.

### M2 interrupt ownership sources

The EA save-disable/restore leaves, host vectors and isolated interrupt tests
are original BSD-3-Clause work. Primary functional facts are TI
**SWRU191F pp.41-46**, interrupt processing and IEN0/IEN1/IEN2 descriptions:
IEN0 `0xA8` has EA bit 7, reserved read-zero bit 6 and six source enables;
IEN1 `0xB8` and IEN2 `0x9A` hold independent enables. EA controls acknowledgment,
not flag assertion; only a higher priority can nest an active ISR and RETI
finishes processing. The p.45 warning about `XCH A,IEN0` is avoided. The p.41
R/W0 flag read-modify-write warning and prohibition on using JBC to poll/clear
hardware flags are not acknowledgment recipes: this slice touches no flags.
Its JBC operand is only the EA control bit (`0xAF`).

The register ABI, exclusive naked-function extents, zero-scratch module and
generated ISR push/pop/RETI sequences are checked against this project's
actual SDCC **4.2.0** linked IHX/map/CDB/module output. No SDK or external
critical-section/dispatcher implementation was imported. uCsim **s51 0.6.4**
in generic C52 mode supplies synthetic external-interrupt entry, priorities
and return. Its IE/IP/TCON/vector meanings are deliberately test-only, not
CC2530 peripheral mappings, silicon timing or hardware evidence.
The exact twelve vector-padding holes are accounted separately from emitted
CODE; no fabricated instruction bytes are inserted to satisfy image checks.
No new dependency, hardware access, private data or board image is added.

### M2 Timer1 IRQ fixture sources

The new fixture, byte decoder, linked checks, synthetic source/entry models
and manual runner are original BSD-3-Clause work. The EA implementation is
unchanged. TI **SWRU191F, revised April 2014**, was read directly for:

| Primary location | Facts used |
| --- | --- |
| Sections 2.5.1/2.5.2, Table 2-5 and Figure 2-4, pp.41-45 | CC2530 interrupt 9/vector `004B`, IEN1.T1IE bit 1, masking/flag assertion and RETI; no generic 8051 Timer1 mapping |
| IRCON p.47; priority section 2.5.3 and Tables 2-6/2-7 p.48 | T1IF bit 1 is H0 on entry; reserved bit 6 must not be written 1; T1 belongs to IPG1; priority registers remain unchanged |
| Section 7.3 p.79 | PxSEL selects GPIO versus peripheral routing; reset GPIO selection prevents counter-clear output initialization from driving timer pins |
| Sections 9.1-9.3 p.104 | Free-running terminal count, DIV clock, T1CNTL-first high-byte latch, low-register write clears the whole counter, MODE=00 suspends at current count |
| Sections 9.10-9.12 pp.113-119 | Source flags assert independently of masks; masked sources set CPU flag; clearing one source can reassert CPU flag from another enabled flag; OVFIF is R/W0, inactive channel reset value 40 |
| TIMIF p.127 | T1 overflow mask is bit 6, reset 1; other bits are Timer3/4 R/W0 flags, so this fixture never writes TIMIF |
| Section 3.3.1, Table 3-2 p.55 | Reset config 26: TIMER_SUSPEND freezes timers while halted/debug-instructed; STEP provides approximately execution ticks; DMA_PAUSE prohibits DMA-register access |

The official [CC2530 errata SWRZ031](https://www.ti.com/lit/pdf/SWRZ031),
April 2009 / document history 2009-04-29, was also read directly. Its issues
1 and 2 concern DMA variable transfer length and Timer2 read latching
(pp.2-3), not this non-DMA Timer1 use. No Timer2 workaround is transferred
speculatively to Timer1.

The board simulator explicitly supplies timer/source values, H0 and R/W0
effects and a hardware-style stack/vector entry because C52 lacks this
CC2530 peripheral/controller mapping. It executes unchanged linked
instructions, including the real vector and ISR/RETI, without ROM patches.
It is not a silicon emulator or hardware evidence. The separate native C52
preemption regression is retained. No TI SDK, external implementation,
dependency or private capture is imported, and automated checks add no
hardware access; source documents are cited, not redistributed in firmware
artifacts.

The operator-supplied
[2026-09-17 (UTC+03) compiled-C LG IRQ acceptance](DEBUGGING.md#2026-09-17-lg-compiled-c-irq-acceptance)
records the unchanged 3,269-byte image
`b9bc83d7254944621f25d312f118ca6a044e808017f85bb6453f28c15cdb0ae1`,
authorized external erase/write/readback and independent complete physical
CODE verification before runner resume in each invocation. Three normal
cycles, an independent pre-start TIMEOUT/FAULT and a separately reset
257-cycle run passed without firmware/runner changes or weakened assertions.
Actual return PC `0C9D` was restore+12 with DPL=OK (0), with full CPU/active-IRAM
preservation and real RETI, not the synthetic +9/live-token-1 case.
The parent verified unchanged private recovery material; only these supplied
processed facts are recorded. No private file, capture, identity or backup
content was read or imported for this update. This bounded LG result does not
validate generic hardware, calibrated timing, higher-priority nesting or
other platform services and does not alter licensing or historical records.

### M2 quiescent radio FIFO sources

The FIFO API, host model and linked/simulator checks are original BSD-3-Clause
work. TI **SWRU191F, revised April 2014**, was read directly for these facts:

| Primary location | Use in this slice |
| --- | --- |
| Sections 4.4-4.4.4, pp.66-69; section 23.14.3 p.239 | Stable selected XOSC32 and 32-MHz system clock for correct radio/CSP operation; no CC2533 amplitude-detector bit transferred to CC2530 |
| Sections 23.1.1/23.1.2, pp.209-213 | Independent masks and R/W0 RFERRF BF error latches; no acknowledgment writes; RF-off while already off can raise STROBEERR |
| Sections 23.2/23.4, pp.213-215 | RFD D9 writes TX/reads RX; two 128-byte FIFOs at 6000..60FF; reset-unknown address/source-match area 6100..617F excluded |
| Sections 23.8.3-23.8.10, pp.218-221 | AUTOCRC length/body/FCS bounds, RFD recommendation, persistent TX contents, overflow/underflow and explicit clear |
| Sections 23.10/23.10.2, pp.232-233 | Direct RAM access does not update FIFO pointers; RX reset pointers/counts/signals; active-RX abort hazard; independent FIFO=0/FIFOP=1 overflow indication |
| Section 23.11/Table 23-3, pp.234-236 | Do not drive software sequencing from rapidly changing FSM state numbers |
| Sections 23.14-23.14.9, pp.238-243,253-254 | RFST E1, CSPSTAT 61E1 RUNNING bit 5, immediate ED/EE flushes, undefined/no-op encodings are not substitutes |
| Section 23.15, pp.259-265 | FRMCTRL0/1 6189/618A, RXENABLE 618B, FSMSTAT0/1 6192/6193, full-byte counts 619B/619C and pointer registers 619D..619F/61A1..61A2 |

The official [SWRZ031 errata](https://www.ti.com/lit/pdf/SWRZ031), April 2009,
history 2009-04-29, was also read directly. Issues 1/2 concern DMA variable
length and Timer2 latching, neither used here. No speculative workaround,
SDK/Contiki/GPL implementation or dependency is imported. The manual's p.209
RF-interrupt-12 narrative conflicts with Table 2-5 p.42 (RF16/vector83);
this slice installs no RF ISR and does not use that narrative mapping.

The [evidence](VALIDATION.md#m2-quiescent-radio-fifo-automated-coverage) is
host/image/synthetic FIFO/CSP only. No physical hardware, private identity/
address RAM, capture or recovery file was accessed. Public reference downloads
are temporary research material, not repository or CI artifacts.

The subsequent board fixture, byte decoder, manual runner and synthetic tests
are also original BSD-3-Clause work. SWRU191F sections 23.2/23.4 pp.213-215,
23.8.3-23.8.10 pp.218-221 and 23.10 p.232 were read directly again for RFD
writes and read-only TX RAM inspection without pointer advancement. Inspection
is confined to known synthetic accepted bytes `6080..60FD`, not the unknown
tail, RX RAM or address/source-match area. Sections 2.5 pp.46-48 and 23.1
pp.210-211 supply the read-only IP0/IP1 (`A9/B9`), RFIRQF0/1 (`E9/91`),
S1CON/TCON (`9B/88`) observations; RFIRQM0/1/ERRM are `61A3..61A5`.
These CC2530 meanings are not generic C52 interrupt semantics.
SWRZ031 was rechecked; neither listed erratum is exercised. No new dependency,
external implementation or private data was imported; automated checks never
access hardware. The separate
[2026-09-17 LG FIFO record](DEBUGGING.md#2026-09-17-lg-compiled-c-fifo-acceptance)
contains only processed observations from explicitly authorized programming,
full physical CODE verification and normal/timeout/reset-recovery runs.
Raw logs, identities and recovery backups remain outside Git and CI.

### M2 deterministic PRNG sources

The original BSD-3-Clause [explicitly seeded deterministic PRNG](ARCHITECTURE.md#isolated-deterministic-prng)
uses functional facts read directly from primary **SWRU191F, April2009 /
revised April2014**, including the rendered Figure14-1 and shared ADCCON1 table.
No SDK, private artifact, GPL programmer implementation, other-part PRNG
implementation or dependency is imported. The source PDF is cited, not
redistributed. RF/noise-seeding implementation is not adopted.

| Primary section/page | Adopted fact and boundary |
| --- | --- |
| 14.1-14.2, Figure14-1 p.144 | Sixteen-bit LFSR with polynomial `x^16+x^15+x^2+1`; left shift toward higher bit indices with old bit15 feedback into next bits15/2/0. PRNG in_bit is zero. One RCTRL01 update performs13 feedback shifts, not one shift or one byte. |
| 14.2.1-14.3 pp.144-145 | RCTRL00 CSP reads advance; CPU RND reads do not. RCTRL01 automatically returns00 **when complete**, providing the bounded polling predicate. RCTRL10 is reserved,11 stopped/off; neither is treated as success. No fixed completion-latency claim is needed. |
| 14.2.2-14.3 pp.144-145 | Each RNDL BC write moves old low byte to high, so seed high then low via two RNDL writes. RNDL/RNDH BD read low/high state, resetFF/FF. RNDH writes trigger8-shift CRC and are excluded. Exactly0000 and8003 are documented PRNG lockup seeds. |
| 12.2.3-12.2.4 p.134; 12.2.10 p.136 | ADCCON1 B4: EOC is R/H0, cleared by ADCH read; ST is R/W1/H0, writing1 with STSEL11 starts conversion. Low reserved bits must be11. STSEL11/ST0 and no ADCCON3 single-conversion activity are external prerequisites; ST0 alone cannot establish ADC quiescence. Write37 does not replay ST1 or clear EOC. |
| 4.1/Table4-1 pp.61-62; 4.4.2/4.4.4 pp.66-69; 4.6 p.69 | Active operation supports RC16 or XOSC32. This API conservatively requires stable undivided clocks and excludes sleep/retention transitions. No PRNG-specific retention/recovery exception is invented. ADC's XOSC32 sampling restriction in12.2.7 p.135 is not transferred to this digital, non-converting service. |
| IRCON register p.47; 11.1-11.2 p.129 | STIF is IRCON bit7, R/W. The Sleep Timer starts immediately after reset; the default compare is **FFFFFF**, not zero. Compare asserts the latched flag even with IRQs disabled. The wirev2 fixture permits only0->1 and preserves it; no clear, compare write, ISR, elapsed threshold or event-count interpretation is adopted. |
| [SWRZ031](https://www.ti.com/lit/pdf/swrz031), April2009, complete errata | The listed DMA variable-length and Timer2 latch issues supply no PRNG exception or missing completion guarantee. |

The original host bit-cell model independently interprets Figure14-1.
A separate polynomial-reduction model agrees for all65,536 input states;
complete graph traversal finds two fixed points and two32,767-state cycles.
Those are derived mathematical results, not a published nontrivial KAT table
(none was found), entropy assessment or on-chip evidence. The isolated Python
simulator oracle separately expresses polynomial reduction and supplies only
synthetic register effects while unchanged compiled C executes.
Physical bit/byte order, seed/step/repeat behavior and shared-register
preservation have bounded LG short, full-stopped and separate full-reset
recovery evidence below, including both clocks and full periods.
Generic and broader cases remain [open gates](VALIDATION.md#m2-deterministic-prng-coverage).
No RF receiver/noise entropy, ADC conversion, random-byte security service
or production software replacement is provided.

The subsequent original BSD-3-Clause [PRNG board fixture](DEBUGGING.md#deterministic-prng-board-fixture)
reuses those exact facts and unchanged production driver. Its sole additional
PRNG write is a deliberate **fixture-only** ADCCON1=3F before the planned
unsupported-state call: SWRU191F14.3 p.145 defines RCTRL11/off, and12.2.10
p.136 establishes ST0/no ADC start, preserved STSEL11/reserved11 and read-only
EOC. It is not automatic driver recovery or a physical poll-timeout injection.
The small CODE prefix constants for seeds1234/1/3 are independently derived
and checked by original host bit-cell/polynomial models, not imported KATs.
All bulk sequence mathematics stays host-only and is excluded from board
artifacts. Repeated CPU readback relies on14.2.1/14.3 pp.144-145, not a
guessed latch or CSP behavior. The manual runner reuses existing bounded
reset/CODE/context/breakpoint operations, with a fixed read-only SFR set and
config26; it adds no DMA gate or generic MMIO capability.
No private record, SDK/GPL implementation or dependency is imported by this
fixture. The [2026-09-17 LG short acceptance record](DEBUGGING.md#2026-09-17-lg-prng-short-acceptance)
uses only parent-supplied sanitized facts: checked board HEX programming,
own reset/full CODE/CPU-FMAP proof, real benign errors, two seed1234 loads
and eight RC16 words matching the derived prefix twice. C and host checked
output, repeated non-advancing CPU reads, guards and tails. This is a narrow
on-chip observation, not a new primary specification or entropy assessment.
Raw records, recovery images and adapter identity remain private and are not
copied into Git/CI. That original wirev1 image subsequently
[stopped during its first long run](DEBUGGING.md#2026-09-17-lg-prng-long-run-flag-policy-interruption)
because STIF asserted after C's last snapshot. This is fixture-policy evidence,
not a PRNG driver/output failure or completed corpus. The parent directly
reread SWRU191F p.47/129 for the correction above; no SDK/private/GPL source
or new dependency was used. The isolated PRNG/timebase/clock drivers remain
unchanged; only original fixture C, wire/runner policy and tests are corrected.
C and host retain raw observations and reject every other flag-bit change,
including any observed STIF deassertion. Synthetic C52 TCON.4/.6 rejection
cases put the simulator's classic timer aliases in external-counter mode
without input edges; this is not CC2530 timer configuration or target code.
Complete terminal RAM/IRAM/SFR comparisons remain strict, without exclusions.
The old7224/aac57938... halt is historical after separately authorized
programming of the unchanged7289-byte wirev2 and its
[2026-09-17 corrected short acceptance](DEBUGGING.md#2026-09-17-corrected-lg-prng-short-acceptance).
That record uses only parent-supplied sanitized facts: complete old/new CODE
checks, checked board HEX programming/readback, own-reset short acceptance,
eight RC16 words and six raw flag observations without a STIF transition.
Reviewed source/proofs are unchanged; no private record or identity is copied.
The same unchanged wirev2 then passed
[full-stopped LG hardware acceptance](DEBUGGING.md#2026-09-17-corrected-lg-prng-full-stopped-acceptance):
every131,084-word corpus result was host-checked, including four32,767
periods/all65,534 valid states per clock. The actual C0/live80 STIF race and
2,145 subsequent preserved observations through XOSC/END/probe confirm this
bounded policy on LG; they do not count wraps/events or establish frequency.
The genuine stopped-probe and retained errors preserved caller data, without
STIF writes, cleanup or automatic recovery. All facts are sanitized parent
observations, not imported implementation or private records.
The same image subsequently passed
[separate full-reset/full-corpus recovery](DEBUGGING.md#2026-09-17-corrected-lg-prng-full-reset-recovery-acceptance).
These additional sanitized parent observations establish the complete corpus
in a new reset epoch and a distinct `c-snapshot` STIF transition, with2,253
later preserved observations. They do not infer recovery from cleared C state,
automatic cleanup or simulation. The final LG halt is ENDREADY016A/config26,
fault0 and C/live IRCON80; the stopped FAULT snapshot is historical.
The bounded LG gate is complete, without importing private records, artifacts,
identities or new implementation. Generic/EOC1/poll-fault/entropy/RF/security/
sleep and broader M2 gates remain open.

### M2 AES CPU-transfer prerequisite

The initial independent AES-128 encrypt-block investigation on 2026-09-17 did **not**
add an API, executable, software fallback or hardware-support claim.
The CPU-only transfer contract remains unresolved; this is not a finding that
CPU access is impossible. Sources read directly:

| Primary source | Established facts / limit |
| --- | --- |
| SWRU191F sections 15.1-15.5 p.147 | ECB/CBC sequence requires key load, IV load, then a start for each 128-bit block and complete output consumption before the next block. Key/IV loads abort active processing. CPU data access is expressly discussed, although DMA is preferred. |
| SWRU191F sections 15.8-15.10 pp.150-151 | ENCDI/ENCDO/ENCCS are B1/B2/B3. RDY describes encryption/decryption completion; ST is hardware-cleared. No CPU byte-ready/accepted-count field or key/IV-load completion condition is specified here. |
| SWRU191F sections 2.5.1-2.5.2 pp.41-46 | ENCIE is IEN0 bit 4; block completion sets both S0CON ENCIF bits 1:0, even with CPU interrupts disabled. S0CON lists R/W flags, not a generic R/W0 acknowledgment recipe. |
| SWRU191F section 2.2.5 p.33; [SWRS081B](https://www.ti.com/lit/pdf/swrs081), February 2011, pp.20-21 | CPU/DMA arbitration and single-cycle SFR access do not specify AES input/output pacing or an AES backpressure guarantee. |
| SWRU191F sections 4.4-4.6 pp.66-69; SWRZ031, April 2009 | Clock/status and AES retention boundaries are documented. Neither listed erratum supplies the missing CPU transfer contract. |

The precise missing facts are when CPU key/IV/block writes may begin and
advance, when key/IV loading is confirmed complete before the next aborting
command, and whether all 16 ECB output reads can proceed consecutively.
An old RDY=1 sample, ST readback, a software byte count or inserted delay is
not a substitute for those guarantees. A synthetic AES oracle/known-answer
test would not establish them on silicon.

The concrete documented alternative is two DMA channels driven by ENC_DW=29
and ENC_UP=30 (SWRU191F p.147, section 15.9 p.150 and Table 8-1 p.99), with
explicit descriptors, completion and ownership handling. At that prerequisite
gate it was **not** implemented: it required separate scope. It also needed a reviewed debug
boundary: Table 3-2 p.55 prohibits DMA-register access with DMA_PAUSE set;
the manual runners then required debug configuration 26, which sets that bit.
The later [bounded debug gate](#m2-dma-debug-configuration-sources) addresses
that prerequisite only, not the AES CPU transfer contract.
The [SWRU214A software examples guide](https://www.ti.com/lit/pdf/swru214),
October 2009, pp.21-24 describes higher-level security APIs, not the missing
CPU handshake. Full TI E2E discussions attempted as clarification returned
HTTP 403; no conclusion is attributed to their inaccessible contents.
No SDK implementation, other-part AES behavior, dependency, key material or
private/hardware observation was imported. The separately assigned
[DMA block foundation](#m2-aes-dma-block-sources) below now implements that
alternative offline; it does not resolve or implement the CPU-transfer path.

### M2 channel-0 DMA sources

The original BSD-3-Clause RAM-copy slice uses functional facts read directly
from **SWRU191F, April 2009 / revised April 2014**, full chapter 8 pp.92-102,
and **SWRZ031, April 2009 / history 2009-04-29**. No implementation, SDK,
other-part workaround, dependency, private data or hardware observation was
imported. The [API/lifetime contract](ARCHITECTURE.md#isolated-channel-0-dma-copy)
and [offline evidence](VALIDATION.md#m2-isolated-dma-copy-coverage) are separate
from the unresolved AES CPU path and the separately scoped two-channel
ENC-triggered foundation below.

| Primary location | Applied fact |
| --- | --- |
| SWRU191F 8.1 p.93 | One channel's descriptor fetch takes nine system clocks; early triggers can be lost. ARM is not a ready flag. Prior-trigger missed-event history persists across reconfiguration; DMAREQ is not cleared by disarming. Only reset/exclusive TRIG0 history is supported, without the documented dummy-copy workaround. |
| 8.2-8.3 pp.95-97; Table 8-2 pp.99-100 | Any XDATA descriptor location, eight big-endian bytes; fixed byte BLOCK/TRIG0, +1/+1 and assured priority (at least every second arbitration try), IRQMASK0. No invented alignment requirement. |
| Figure 8-1 p.94; 8.4-8.5 p.98; 8.8 pp.101-102 | Completion sets DMAIRQ despite IRQMASK0, which gates IRCON.DMAIF. DMAREQ clears when transfer starts; ARM clears at non-repeated completion. D1 IRQ is R/W0; D4/D5 select channel 0, D2/D3 the separate channels 1-4 table; D6 ARM and D7 REQ use per-channel write-one controls. Channel bit tables, not the figure's inconsistent `85` channel-number prose, define channel 0. |
| 2.1 p.25; Table 2-3 pp.37,39; 2.2.5 p.33 | NOP is opcode 00, one byte, minimum one CPU/system clock; flash/arbitration stalls only lengthen it. The linked nine-NOP path supplies the documented interval, not generic s51's 12-clock instruction timing. |
| 2.2 pp.26-33; 2.5 pp.41,44,47; 4.4-4.5 pp.66-69 | Ordinary RAM/IRAM alias separation, IEN1.DMAIE bit0 and IRCON.DMAIF bit0, no RMW acknowledgment of source flags; observed stable undivided clocks. |
| Table 3-2 p.55 | DMA_PAUSE pauses all transfers and prohibits DMA-register access while set. Debug configuration 26 is unsuitable; future hardware requires separately authorized, verified 22 before first access. Firmware neither infers nor changes debug configuration. |
| SWRZ031 issue 1 p.2 | Variable-length VLEN001/010 zero/one-length erratum; this slice uses unaffected VLEN000 and positive fixed length. |

### M2 DMA debug configuration sources

The original API uses primary **SWRU191F, revised April 2014**, Table 3-1
pp.53-54 (WR_CONFIG `00011XXX`, target STATUS result, GET_BM returning
FMAP.MAP) and Table 3-2 p.55 (DMA_PAUSE and configuration bits).
The exact **OUT04 `4C 1D 22`, without a paired USB read**, was individually
observed by the parent using authorized external `cc-tool 0.26 --reset --log`;
it was not derived from a target opcode or general adapter grammar.
No programmer implementation was imported. The target STATUS result must not
be reinterpreted as an unsolicited USB reply for this packet.

The [canonical dated record](DEBUGGING.md#2026-09-17-lg-dma-enable-gate-acceptance)
separates external observation, explicit replay, three native API cycles,
late host-return failure, independent read-only effect observation and
explicit reset/FIFO recovery. Only processed facts are published; external
traces, errors and JSON remain private. This establishes the bounded
debug-config gate on that LG setup, not DMA-controller copies, AES,
physical USB stalls or general adapter compatibility.

### M2 DMA board fixture sources

The original BSD-3-Clause [DMA fixture and guarded runner](DEBUGGING.md#channel-0-dma-board-fixture)
reuse the published DMA, clock, Sleep Timer and debug-gate implementations
unchanged. The primary facts are those cited above, including SWRU191F
chapter8, Table2-3, section2.5's SFR/interrupt map, and Tables3-1/3-2;
no new adapter framing or hardware timing is inferred. Only the reviewed
relocation operands of the original 2,867-byte DMA module are normalized;
its published hash is not replaced. Public patterns, host/backend models,
wire serializers and alias-aware synthetic events are original work.
No SDK/GPL implementation, private artifact or dependency was imported.
[Offline evidence](VALIDATION.md#m2-dma-board-fixture-coverage) remains distinct
from the separate [2026-09-17 LG compiled-C record](DEBUGGING.md#2026-09-17-lg-compiled-c-dma-acceptance):
authorized programming/readback, full physical CODE verification, normal,
negative and explicitly reset recovery runs of this original image.
Only processed observations are published; raw logs, identities and recovery
material remain private. Earlier debug-gate/FIFO observations remain historical.

### M2 AES DMA block sources

The original BSD-3-Clause [single-block service](ARCHITECTURE.md#isolated-aes-128-dma-block),
host mathematical reference, register/descriptor models and isolated linked
proof use functional facts only. **SWRU191F, revised April 2014**, complete
chapter 15 and the relevant DMA, memory, bus, IRQ, clock and retention passages
were read directly; **SWRZ031, April 2009**, was checked in full.

| Primary location | Applied fact |
| --- | --- |
| SWRU191F 15.1-15.4 p.147 | Key, IV, then start/data for each 128-bit ECB block; correct mode on IV load; full output consumption before another block. Key/IV commands abort active processing. Two DMA channels must be initialized before start generates their trigger. |
| 15.8-15.10 pp.150-151 | ENC_DW29 requests needed ENCDI input; ENC_UP30 requests ENCDO output. B1/B2/B3 registers; ECB MODE4, encrypt CMD0, key CMD2, IV CMD3; ST is R/W1 H0. RDY bit3 describes encryption/decryption, not load completion. The block-interrupt wording does not state that loads cannot request ENC interrupts. |
| 8.1 p.93; 8.2.3/8.2.7 pp.95,97; 8.3-8.5 pp.97-98 | Nine system clocks **per channel**, sequential configuration fetches; old missed-trigger history survives reconfiguration. Fixed SINGLE moves one byte per trigger and completes/disarms after LEN transfers. DMA1 configuration requires a real 32-byte table. Completion sets DMAIRQ even with IRQMASK0. |
| Tables 8-1/8-2 pp.99-100; 8.8 pp.101-102 | Exact triggers, big-endian descriptors, increments and priority; DMAARM per-channel write-one, DMAIRQ per-channel R/W0, DMAREQ not cleared by disarm. No software request/dummy/abort recovery is adopted. |
| 2.2.2-2.2.3 pp.27-29; 2.2.5 p.33; Table 2-3 pp.37,39 | AES peripheral SFR aliases are `70B1/70B2`, unlike CPU-internal SFR exceptions. RAM/IRAM alias and arbitration rules; one-clock minimum NOP, with stalls only lengthening the interval. |
| 2.5.1 pp.41,44,46; 4.4-4.6 pp.66-69 | ENCIE is IEN0.4; block completion sets both ENCIF bits even with CPU interrupts disabled. S0CON bits1:0 **and reserved7:2 are R/W**. Key/IV clear on reset/PM2/PM3, not a software-RAM erasure guarantee. |
| Table 3-2 p.55; SWRZ031 issue1 p.2 | Independently establish clear DMA_PAUSE before DMA access. Fixed VLEN000/LEN16 is outside the variable-length zero/one erratum; no AES-specific erratum supplies a CPU pacing contract. |

The [first physical LG KEY load](DEBUGGING.md#2026-09-17-first-lg-aes-key-load-failure)
set both ENCIF bits while only input DMA completed. Re-review of complete
chapter15 and S0CON p.46 rejected the old inference that KEY/IV flags stay
clear; SWRZ031 supplies no load-interrupt clarification. The
[corrected bounded LG runs](DEBUGGING.md#2026-09-17-corrected-lg-aes-bounded-acceptance)
subsequently observed fresh pair3 and verified-clear ACKs for **both KEY and
IV** through the unchanged real C gate, alongside finite input completion,
MODE/CMD/ST and retained output ownership. This is physical evidence for those
runs, not an explicit load-pair guarantee added to the primary wording.
The driver still fails boundedly if flags are absent. No guessed RDY delay
establishes delivery. The parent independently reread SWRU191F pp.46/147/150
using system Swift/PDFKit before these runs; no project dependency was added.
Each fresh command's DMA completion is acknowledged/checked before its
ordinary-R/W S0CON ACK is checked; only then may the next descriptor/start
proceed. Block completion additionally requires all output drained.
Short normal operation, both exact negatives and separately reset257-cycle
recovery now have LG evidence. The recovery accepted514 blocks with all168
public vector/space/clock combinations independently asserted. Generic remains
unobserved. Successful reset/recovery does not turn clear flags into proof of
eligible history, make load-pair wording more explicit or resolve CPU-only pacing.
No old trigger is discarded merely by rewriting a descriptor or ARM.

The host-only oracle independently implements the field arithmetic, generated
S-box, round transformations and AES-128 expansion from
[FIPS 197, November 26, 2001](https://nvlpubs.nist.gov/nistpubs/FIPS/NIST.FIPS.197.pdf),
sections 3.4, 4, 5.1-5.2 (printed pp.8-20). Its Appendix C.1 pp.35-36 vector and
[NIST SP 800-38A, December 2001](https://nvlpubs.nist.gov/nistpubs/Legacy/SP/nistspecialpublication800-38a.pdf),
F.1.1 p.24, supply five public AES-128 KAT blocks. The downloaded FIPS edition
carries its May 9, 2023 withdrawal/replacement notice; that update makes
editorial improvements without changing the algorithm. These are public
standard vectors, not keys/captures from a device. No implementation/table,
SDK/GPL code, production software fallback or project dependency was imported.
Temporary public PDF parsing used externally installed BSD-3-Clause pypdf
6.1.1 in the ignored research directory only, not in builds/tests/runtime.

### M2 AES board fixture sources

The original BSD-3-Clause fixture/runner reuse the corrected DMA AES primitive
and the primary sources above; no peripheral timing or CPU-only pacing contract
is added. The read-only manual surface observes ENCCS, S0CON, DMA control/config,
clock/enables and the existing priority/flag set (SWRU191F sections 2.5.1,
4.4-4.6, 8 and 15). It never reads ENCDO or performs CPU data transfers.

The five primary KATs are followed by sixteen original public cases:
for `n=0..15`, byte `i=0..15`, key=`(19*n+31*i)&255` and
input=`((37*n+13*i)&255)^A7`. Expected outputs are generated and independently
rechecked by the existing host-only mathematical reference. The fixture CODE
table stores 21 rows of 48 data bytes plus SDCC's string terminator; it is
comparison data, not an encryption implementation or a returned-result table.
Host math remains excluded from every board image and CI artifact.
Generic simulation supplies explicit DMA/AES events, not silicon evidence.
No SDK/GPL/private implementation, device data or dependency was imported.
The dated first-failure and corrected-acceptance records contain only
parent-supplied sanitized facts;
private raw records and the preserved original image/artifacts were not read
or imported. The parent corrected runner preflight from a guessed instruction
prefix to actual linked `90 00 00 E5 95`; actual-image and wrong-prefix
regressions retain that fix. It did not change the failed firmware bytes.

### Offline MAC codec sources

The standalone codec is original BSD-3-Clause code, not an imported Contiki
module. Generic legacy frame-field facts were cross-checked against
[Microchip's IEEE 802.15.4-2006 FCF description](https://onlinedocs.microchip.com/oxy/GUID-4B3D771E-5647-4191-AD45-C897B0D23071-en-US-3/GUID-46A20735-4885-446D-9C0A-461B67A126BF.html)
and the BSD-3-Clause
[frame802154.h](https://github.com/contiki-os/contiki/blob/32b5b17f674232867c22916bb2e2534c8e9a92ff/core/net/mac/frame802154.h)
and
[frame802154.c](https://github.com/contiki-os/contiki/blob/32b5b17f674232867c22916bb2e2534c8e9a92ff/core/net/mac/frame802154.c).
The revision and licenses were checked before using these references.
Microchip's transceiver registers/timings are not CC2530 implementation facts.

The engineering scope is the legacy IEEE 802.15.4-2006-compatible DATA/ACK
and five-command wire subset documented in [MAC.md](MAC.md), not complete standard conformance.
No Contiki state, security code or frame-processing implementation was copied.
Address arrays use explicitly documented wire order, not Contiki's display
order; PAN compression is explicit rather than automatically selected.
All vectors, addresses, PAN IDs and payloads are original synthetic values.

The command extension was checked directly against
[IEEE Std 802.15.4-2006](https://people.ece.ubc.ca/~edc/7860/data/802.15.4-2006.pdf),
sections 7.2.2.4 and 7.2.3 (printed pp.147-149), sections 7.3.1-4
(pp.150-154), section 7.3.7 (p.156), and Tables 82-84. These supply the command
identifiers, fixed payload formats, capability/status/reason fields and
command-specific addressing/ACK/compression facts. In particular, successful
association without an allocated short address uses `FFFE`; unsuccessful
association uses `FFFF`. Frame Pending is zero on transmission and ignored on
reception. Generated search summaries were not used as authority for those
facts. Version 0 and strict reserved-field rejection are explicit codec subset
choices. The manual is linked, not redistributed; no third-party command
implementation or packet capture is imported.

Contiki's RF code depends on Contiki facilities. An adaptation must replace
those interfaces deliberately, preserve the original notices and be tested
against the CC2530 documentation. It does not supply Zigbee NWK/APS/ZDO.

The inspected legacy ZBOSS tree documents TI CC253x use with IAR and an SDCC
simulator configuration. The presence of both words in one repository is not
proof of a tested native CC2530/SDCC port. Its old protocol implementation is
not automatically compliant with the project's intended R22/BDB behavior.

## Normative target

Engineering references:

- Zigbee Core R22, document **05-3474-22**.
- A Zigbee-3.0-era BDB revision compatible with R22. This is an explicit
  [open gate in the initial conformance ledger](CONFORMANCE.md), to be resolved
  before M4/M5 security/commissioning implementation, not after interoperability
  work. Exact procedures and requirements must be recorded.
- Applicable Zigbee Cluster Library and application-profile requirements for
  the chosen device, pinned when those layers are implemented.

BDB 3.1, document 22-65816-030, belongs with R23/PRO 2023. It may inform future
work but must not be cited as an R22 requirement. The project is not certified.

Key R22 references used in the plan: Table 2-44 p.84; section 2.4.4.1 p.137;
section 2.4.4.4.5 p.188; section 2.5.4.8.1 p.222;
section 3.6.10.2 pp.392-393; section 4.3.4 pp.416-417.
Paraphrases in this repository are an engineering aid, not replacement
specification text.

## Specialist-agent reference

The repository's specialist profiles are original project instructions.
The user-supplied
[faronov/zigbee-docs reference](https://github.com/faronov/zigbee-docs/tree/6e575bdc8c1a68880ef7552d6190a0c6bc80b3a6)
was inspected at immutable revision
`6e575bdc8c1a68880ef7552d6190a0c6bc80b3a6`, including its agent, skill and source
metadata. It is an external secondary lookup aid, not a normative source or
build dependency. No declared license was established for those files, so
neither its prompts, generated catalog nor scripts are copied into this tree.

Its [source index](https://github.com/faronov/zigbee-docs/blob/6e575bdc8c1a68880ef7552d6190a0c6bc80b3a6/docs/README.md)
combines ZCL Revision 8 and Matter 1.5, and identifies Core R23
(`05-3474-23`), not this project's R22 baseline. Its BDB labels are inconsistent:
the index says 2.1 while
[bdb.json](https://github.com/faronov/zigbee-docs/blob/6e575bdc8c1a68880ef7552d6190a0c6bc80b3a6/docs/base-device-behavior/bdb.json)
has `specification.version` 1.0; both name document `13-0402-13`.
These labels do not resolve the compatible-BDB gate.

Agent lookups must use the reviewed revision, inspect field-level source
annotations and verify implementation decisions against the applicable
primary specification. Do not import Matter-only fields into ZCL or treat
R23 requirements as R22 requirements. Fetch only the relevant remote index
entry/file; its `docs/` paths refer to that repository, not this checkout.
Changing the reviewed revision requires rechecking these source constraints.

## Before importing any code or data

Record its upstream URL and immutable revision, license, original notices,
local changes and tests. Check each imported dependency, not merely the
repository's top-level license badge.

- Do not vendor proprietary IAR/TI stack libraries or OEM firmware code.
- Do not assume IAR-built objects can be linked by SDCC.
- Do not copy GPL programmer/debugger implementation into the BSD core.
  A separately licensed tool or an external dependency is a different decision
  and needs explicit documentation.
- Do not import raw device dumps, private traffic captures, real credentials,
  factory identities or personal photographs.
- Use synthetic/publicly licensed test fixtures with a documented origin.
- Review and sanitize a new media/fixture policy before adding binary assets.

The lightweight repository checker is a guardrail, not a complete secret
scanner, legal review or proof of provenance. Human review is still required
before publication.
