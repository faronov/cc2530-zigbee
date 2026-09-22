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

The integrated protocol resource harness, synthetic frames and SDCC resource
ledger are original BSD-3-Clause work. Leaf serialization and pointer-storage
changes refactor this repository's existing codecs, not an imported stack.
Resource facts come from the local SDCC 4.2.0 objects/link output and uCsim
execution; they are not hardware measurements or new normative wire rules.
The #76 [ZCL headroom refactoring](ZCL.md#production-code-headroom) likewise
uses only existing original C and compiler artifacts: compact private value
classification, equivalent bounded lookup/discovery and shared publication.
No external implementation, assembly or new normative behavior is imported.

## Reviewed reference candidates

| Reference | Status and permitted use |
| --- | --- |
| [TI CC2530](https://www.ti.com/product/CC2530) and [SWRU191F](https://www.ti.com/lit/pdf/swru191) | Primary hardware facts; link/cite documentation rather than redistribute whole manuals |
| [Contiki CC2530 RF driver](https://github.com/contiki-os/contiki/blob/32b5b17f674232867c22916bb2e2534c8e9a92ff/cpu/cc253x/dev/cc2530-rf.c) | BSD-3-Clause terms verified; [external reference build evaluated](#contiki-cc2530-reference-evaluation), not imported |
| [Contiki CC253x build](https://github.com/contiki-os/contiki/blob/32b5b17f674232867c22916bb2e2534c8e9a92ff/cpu/cc253x/Makefile.cc253x) | Explicit SDCC, `0x1F00` XDATA limit and banking evidence; not proof of Zigbee support |
| [Legacy open ZBOSS](https://github.com/niclash/zboss) | Reference candidate requiring per-file license/revision review; not a proven ready CC2530/SDCC modern stack |
| [Public LG ESL demo](https://github.com/cddwx525/cc2530_esl_demo/tree/eca70ae29c8f6c4b1bdc58b99c1192646e74d469) | Hardware reference only; no declared repository license was established, so do not copy its implementation/fonts/images |
| [SDCC](https://sdcc.sourceforge.net/) | Build tool, with its own licenses; baseline 4.2.0 |
| [cc-tool](https://sourceforge.net/projects/cctool/) | External GPL programmer; not vendored or relicensed as BSD code |

### Contiki CC2530 reference evaluation

On 2026-09-19, #49 evaluated revision
`32b5b17f674232867c22916bb2e2534c8e9a92ff` in an isolated external public-source
workspace. Earlier original register-based services were **not** the result of
a comparative Contiki port proving it unsuitable. This experiment supplies
an actual reference build and source comparison, not a port or adoption.
Source, compatibility patches, logs and generated binaries remain outside
this repository and its CI artifacts. No equipment or private material was used.

The selected upstream `examples/hello-world` configuration uses Rime instead
of the default IPv6/banked path. The inspected
[CPU settings](https://github.com/contiki-os/contiki/blob/32b5b17f674232867c22916bb2e2534c8e9a92ff/cpu/cc253x/Makefile.cc253x#L17-L96)
and [link/pack rules](https://github.com/contiki-os/contiki/blob/32b5b17f674232867c22916bb2e2534c8e9a92ff/cpu/cc253x/Makefile.customrules-cc253x#L72-L79)
compile, link and pack this target; no upload/serial/firmware execution target
was selected. After an equivalent `-n` dry run, the command was:

```sh
make --no-print-directory -j1 TARGET=cc2530dk CONTIKI_WITH_RIME=1 \
  RELSTR=32b5b17f674232867c22916bb2e2534c8e9a92ff \
  V=1 hello-world.cc2530dk
```

This command does **not** succeed on the unmodified source with SDCC 4.2.0:

| Attempt | Actual outcome |
| --- | --- |
| Unmodified upstream | Exit 2: the CC253x `clock_delay` compatibility macro rewrites the deprecated `unsigned int` declaration into a conflicting `clock_delay_usec` declaration. |
| Compatibility edit 1 | Guard the deprecated declaration at `core/sys/clock.h:141` with `#ifndef clock_delay`. The build advances, then exits 2 on legacy `void putchar(char)` versus the compiler's `int putchar(int)` ABI. |
| Compatibility edits 1 and 2 | Change the declaration in `platform/cc2530dk/debug.h:49` and definition in `platform/cc2530dk/putchar.c` to `int putchar(int)`, convert through `unsigned char` before the existing output, and return the converted byte. The build exits 0, links and packs. |

The second edit preserves actual output, not a successful output stub; it
adds no UART error detection or EOF-on-write-error contract. No third or
broader compatibility repair was attempted. The two retained external patches,
`compat-01-clock-prototype.patch` and `compat-02-putchar-abi.patch`, have SHA-256
`1ec80949308437b75ddfc6b1f0d0d098a3d22dea94d7bd8998decdb7d09f65b1` and
`071ba09228618c369e34c1b553d8c1b5d5d5d492c895f4f097738cc7f9ef3cfe`.

The genuine linked image reports **49,175 CODE bytes**, ending at `C016`,
with **49,088 populated Intel HEX bytes**. The linker-reported XDATA extent
is **2,782 bytes**, through `0ADD`: relocatable XSEG contributes 2,632 bytes
from `0004` and XISEG another 146 from `0A4C`, with the leading four bytes
included in the reported extent rather than that relocatable sum.
Initial SP is `20`; the static 223-byte stack capacity is **not an executed
high-water measurement**. Upstream uses `--stack-auto`, a 64-KiB CODE allowance
and XDATA through `1EFF`, not this project's ABI/status/32-KiB policy.
These example sizes establish no like-for-like footprint advantage.

The packed `hello-world.hex` SHA-256 is
`68993b5a73c49246939bc5c0d0ade64c4f57ca31a4e04e60983a41ab962f4f84`.
Independent parent artifact inspection confirmed the digest, HEX checksums
and extents, map allocation and the return-zero sequence below. This is not
the repository's complete image/ABI proof or alias-aware execution.

**A successful link is not a runnable-example claim.** Two retained defects
qualify this particular source/toolchain/configuration:

- [Platform initialization](https://github.com/contiki-os/contiki/blob/32b5b17f674232867c22916bb2e2534c8e9a92ff/platform/cc2530dk/contiki-main.c#L84-L126)
  uses plain `char i` with `i >= 0`. It is unsigned in this build; emitted code
  has an unconditional backward jump and indexing beyond the eight-byte address
  buffer. No firmware execution was needed or performed to inspect that code.
- [The driver's `receiving_packet()`](https://github.com/contiki-os/contiki/blob/32b5b17f674232867c22916bb2e2534c8e9a92ff/cpu/cc253x/dev/cc2530-rf.c#L586-L596)
  has a precedence error. Its actual linked entry at `2910` emits
  `906193e090000022`: read FSMSTAT1, then return zero.

The bounded reuse comparison is:

| Area and pinned source | Useful reference and remaining difference |
| --- | --- |
| [RF initialization](https://github.com/contiki-os/contiki/blob/32b5b17f674232867c22916bb2e2534c8e9a92ff/cpu/cc253x/dev/cc2530-rf.c#L308-L379) | Channel/power/address helpers and recommended AGC/TX-filter/FSCAL settings are useful. No equivalent cold-history, full readback or ownership admission proof is provided. |
| [RX handling](https://github.com/contiki-os/contiki/blob/32b5b17f674232867c22916bb2e2534c8e9a92ff/cpu/cc253x/dev/cc2530-rf.c#L491-L572) and [platform handoff](https://github.com/contiki-os/contiki/blob/32b5b17f674232867c22916bb2e2534c8e9a92ff/platform/cc2530dk/contiki-main.c#L298-L308) | AUTOCRC/AUTOACK and disabled source matching/AUTOPEND corroborate the hardware foundation. Platform polling feeds `packetbuf`, then RDC/MAC software filtering. Bad CRC is handled but flushes RX; this is not #48's explicit loss-preserving drain. |
| [TX](https://github.com/contiki-os/contiki/blob/32b5b17f674232867c22916bb2e2534c8e9a92ff/cpu/cc253x/dev/cc2530-rf.c#L382-L478) and [CCA/on/off](https://github.com/contiki-os/contiki/blob/32b5b17f674232867c22916bb2e2534c8e9a92ff/cpu/cc253x/dev/cc2530-rf.c#L577-L630) | Unified RX/TX ownership with a persistent RX request is useful precedent. Unbounded waits, separately sampled CCA before `ISTXON`, TX_ACTIVE-based completion, hard strobes and RX flushes do not meet our bounded completion/recovery contracts. |
| [`nullrdc` ACK path](https://github.com/contiki-os/contiki/blob/32b5b17f674232867c22916bb2e2534c8e9a92ff/core/net/mac/nullrdc.c#L110-L201) | Waits after transmit returns and checks three-byte length/DSN, not an independent ACK FCF predicate. Hardware filtering precedes it; compatibility with our ignored-FCF corpus remains unproved. |
| [Object API](https://github.com/contiki-os/contiki/blob/32b5b17f674232867c22916bb2e2534c8e9a92ff/cpu/cc253x/dev/cc2530-rf.c#L739-L778) and [rtimer](https://github.com/contiki-os/contiki/blob/32b5b17f674232867c22916bb2e2534c8e9a92ff/cpu/cc253x/rtimer-arch.c#L54-L98) | This driver exposes extended address, not a captured timestamp object. Its live 16-bit Timer1 at nominal 15625 Hz is not Timer2 RF-edge capture. The compare-programming use of capture mode does not settle #40. |
| [CSMA scheduling](https://github.com/contiki-os/contiki/blob/32b5b17f674232867c22916bb2e2534c8e9a92ff/core/net/mac/csma.c#L145-L195) | Queues, callbacks, backoff and retries belong to MAC/OS, not the radio driver. The selected 128-Hz clock gives a minimum nonzero backoff unit of one tick, not an exact 320-us interval. The inspected path supplies no equivalent explicit 12/40-symbol IFS ledger, bounded QUIESCED retirement or loss-aware POLL CLOSED. |

The driver depends on Contiki configuration, clock/rtimer, packetbuf, netstack,
statistics and optional LEDs/energest. Its `.c/.h` files carry 2011 George
Oikonomou BSD-3-Clause notices. Other reviewed MAC/timer files retain their
own SICS, ADVANSEE/Benoit Thebaudeau or Loughborough University notices;
headerless build/platform files were not assigned invented per-file notices.
The [root license](https://github.com/contiki-os/contiki/blob/32b5b17f674232867c22916bb2e2534c8e9a92ff/LICENSE#L1-L36)
does not remove the review/preservation obligation for a future import.

**Decision:** retain a concretely evaluated reference for selective adaptation,
not a drop-in driver/MAC replacement or a rejected platform. Repairing the
demonstrated defects would not by itself remove OS/global-buffer coupling or
establish our bounded timing/ownership contract. No current original-code
hardware assumption was disproved by this secondary evidence. TI SWRU191F
remains normative; captured freshness, ACK-filter compatibility, IFS and POLL
handoff stay open. No hardware/interoperability or Zigbee support was observed.
The original code's BSD-3-Clause license is unchanged; no Contiki code is
imported by this evaluation.

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

### Linux external-programmer no-run guard sources

`tools/cc_tool_no_run_guard.c` and `tools/test_cc_tool_no_run_guard.py` are
original BSD-3-Clause work. The guard uses Linux ELF `LD_PRELOAD`,
`dlsym(RTLD_NEXT, ...)` and the `libusb_control_transfer` calling convention
to reject a narrow class of control requests before submission. It copies
no GPL programmer implementation, flash algorithm, RAM executor or USB
bytecode encoder. The existing reviewed adapter reset facts are functional
reference facts; **SWRU191F is not an adapter USB protocol specification**.

The parent supplied this separately reviewed external-tool identity on
2026-09-18; the external source archives, unpacked tool and inspection logs
remain session-private and are not imported or CI artifacts:

| Item | Reviewed identifier |
| --- | --- |
| Installed Ubuntu package | `cc-tool 0.27-1build5`; executable banner `0.26` |
| Installed `/usr/bin/cc-tool` SHA256 | `815c42664a30e5efd88d2c62a71fff00e2afa4ec4a3217243749f5f3f55fb1c4` |
| Upstream v0.27 source revision | `51fd9dffc206d132614f30402b50cd712fa9a60c` |
| Official Ubuntu original source archive SHA256 | `1d26be4446c68413a02bf3156e6434d7fe9ce76aa0a169464ca5d7b2a731610d` |
| Debian patch series in the reviewed source | Empty |

The installed binary matched the parent's privately unpacked binary.
At that unpatched revision, `src/application/cc_base.cpp` `execute()` calls
`unit_close()` on normal return; `src/programmer/cc_programmer.cpp`
`unit_close()` always calls `reset(false)`. This includes a read-only task.
`src/application/cc_flasher.cpp` `task_verify()` can merely print a mismatch
and return normally, reaching that cleanup reset. The guard is original
interposition around these functional lifecycle facts, not an adaptation of
those GPL implementations. Package version, banner and content identity must
not be conflated.

The [exact guard boundary](DEBUGGING.md#linux-external-programmer-no-run-guard)
forwards only the reviewed OUT reset-into-debug shape
`40/C9/value0/index1/NULL/length0` among OUT `C9` requests, rejects the rest
before libusb with flush and exit86, and reports helper/log failures with
exit87. Unrelated operations/errors remain unchanged. No environment disable
switch, reset substitution or successful-service stub exists.
It does not block DEBUG RESUME/STEP or the external programmer's RAM helper.
Exit86 is neither programming success nor halted-state proof.

Six strict synthetic tests compile only the original fake USB library/driver
and guard; normal discovery includes them without build integration or USB.
The parent also verified the installed ELF's actual binding using
`LD_BIND_NOW`/`LD_DEBUG` with `--help` only, without USB. That parent-reported
host/linkage evidence does not establish guarded target behavior or hardware
acceptance. Independent physical readback/halt confirmation and separately
authorized recovery conditions remain required. No private backup, device
identity, capture, external source archive or programmer binary is published.

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

The subsequent [FIFO simulator guard correction](VALIDATION.md#fifo-simulator-guard-regression-after-rx-declarations)
is also original repository-only test code. It accounts for the harness's
explicit `RFIRQF0 AA -> AB` input after the common RX declarations exposed the
existing `E9/91` aliases; the register references remain SWRU191F sections2.5
and23.1 cited above. Bounded CPU/RAM continuation uses the repository-local
restore pattern, not an imported simulator or firmware implementation.
C52 SBUF-write effects are emulator bookkeeping, not CC2530 peripheral
evidence. No firmware bytes, hardware acceptance record, external code or
private data were changed or imported by this correction.

### M2 passive RX sources

The [isolated passive RX service](RADIO_RX.md), host model and linked
instruction checks are original BSD-3-Clause code. No Contiki/TI SDK driver,
external firmware, capture or programmer implementation was imported.
Functional facts were read directly from **SWRU191F, revised April2014**:
pp.209-211 (RF flags), p.215 (channel formula), pp.221-224 (receive control),
pp.229-235 (appended CRC/RSSI/correlation, FIFO/overflow and raw metadata),
pp.251-254 (immediate E3/ED strobes), pp.256-268 (recommended settings,
filter/source controls, RXMASKCLR, counts and pointers).
TXFILTCFG is `61FA`; RXP1_PTR is eight bits rather than the seven-bit
RXFIRST_PTR/RXLAST_PTR. Filtering/source matching disabled is configuration,
not a reason to silently clear or misinterpret raw interrupt flags.

[SWRZ031](https://www.ti.com/lit/pdf/swrz031), April2009, was rechecked.
Its DMA variable-length and Timer2 latch errata do not establish RX behavior
or waive a hardware gate; this service uses neither operation.
Primary PDFs remain external. Synthetic events explicitly supply peripheral
effects absent from s51; they are not captures or hardware observations.

The subsequent bounded board fixture, decoder, private-file manual runner and
tests are also original BSD-3-Clause work, reusing only this repository's
startup, clock, RX, debugger and host-model code. Hardware facts retain the
same **SWRU191F (April2014)** references above; CPU snapshots additionally use
the SFR/interrupt descriptions in section2.5 pp.46-48 and Sleep Timer
sections11.1-11.2 p.129: STIF may latch with IRQs disabled and is never cleared
by the fixture. **SWRZ031 (April2009)** adds no applicable DMA/Timer2 operation
to this no-DMA slice. No USB framing, debugger MMIO writer, external driver,
programmer implementation, dependency or private data is imported.
The C52 UART suppression on continuation is solely a simulator bookkeeping
fact, verified against uCsim4.2.0 behavior; it is not a CC2530 hardware claim.

The [2026-09-18 parent-observed LG failure/probe](DEBUGGING.md#2026-09-18-lg-rx-fscal1-failure-and-probe)
identified FSCAL1 `61AE` readback `30` against the previous full-byte `00`
expectation. **SWRU191F (April2014) p.267**, read directly again for this
correction, defines reserved bits7:2 as R/W0 with reset `001010`, and
VCO_CURR bits1:0 as R/W. **Section23.15.1/Table23-6** still recommends writing
FSCAL1 `00`; **section23.15.2/Table23-7** distinguishes W0 from read-as-zero R0.
The adjacent FSCAL2 table describes a separate capacitor-calibration result,
not the FSCAL1 observation. Only the documented stable low bits are compared;
there is no new calibration algorithm or workaround imported from another
part. The newly emitted opcode52 is the two-byte `ANL direct,A` from the
same manual's **Table2-3 p.37**; actual output targets private bank0 AR0,
and the proof rejects other instances/operands. The existing SWRZ031 DMA/Timer2
issues do not alter this register-table
contract. Processed failure/probe facts were supplied by the parent; no
private backup, payload, capture, identity or external implementation was
opened or copied by the offline implementer. Corrected-image automated checks
remain host/image/synthetic evidence. The separate
[2026-09-18 bounded LG acceptance](DEBUGGING.md#2026-09-18-lg-bounded-passive-rx-acceptance)
records parent-performed guarded programming, complete flash/factory readback,
compiled-C reception, a real deadline hold, reset recovery and terminal cap.
Independent reference capture used official Nordic sniffer firmware0.8.0
on the nRF52840 DK, channel15. The externally installed host tool came from
[upstream revision e459feba9730f85b22a78d3559c67c4df6bf876a](https://github.com/nordicsemi/nRF-Sniffer-for-802.15.4/tree/e459feba9730f85b22a78d3559c67c4df6bf876a);
its distribution metadata reports version0.0.0, not the firmware's version.
Its parser omits the final two serial-frame octets;
private comparison checked complete FCS-free bodies, not independent CRC or
authentication. The original standard-library comparison checked PCAP2.4,
DLT283, complete record lengths and TAP channel/metadata structure.
Only processed counts/statuses and this project's public image hash are
recorded here. No capture, frame, address, payload hash, factory identity,
external sniffer implementation or programmer binary is imported or uploaded.

Keeping those two octets would **not by itself establish genuine on-air FCS**.
The host README points to the nRF Connect SDK2.6.0
[sniffer sample](https://github.com/nrfconnect/sdk-nrf/blob/3190fa573ff67bfb745028f203e9d0ea4144a1ce/samples/peripheral/802154_sniffer/src/main.c#L53-L83),
whose configuration enables RAW_MODE and whose
[manifest](https://github.com/nrfconnect/sdk-nrf/blob/3190fa573ff67bfb745028f203e9d0ea4144a1ce/west.yml)
selects Zephyr `v3.5.99-ncs1`. That pinned
[radio driver](https://github.com/nrfconnect/sdk-zephyr/blob/d96769facecaba386b642d2c76c92c7694c81da0/drivers/ieee802154/ieee802154_nrf5.c#L156-L164)
distinguishes LQI/automatic-CRC metadata from literal FCS in the trailing buffer
positions. This is public reference-source inspection, not a source/binary
identity proof for the installed Nordic firmware. Neither its discarded
octets nor a software-recomputed CRC are independent received-FCS evidence.

### Filtered receiver/AUTOACK ownership sources

The original [receiver/AUTOACK owner](RADIO_AUTOACK.md#primary-basis-and-limits)
uses TI SWRU191F (April2009, revised April2014), not an imported radio stack.
Section23.4.3/Table23-1 p214 establishes the unknown-after-reset local-address
RAM; sections23.9.5 pp224-226 and23.9.7-8 pp230-232 establish filtering,
AUTOCRC metadata, automatic-ACK eligibility/DSN and unslotted12-symbol timing.
Sections23.9.1-2 pp222-223, Fig23-20 p235/Table23-3 p236, flags pp210-211
and registers pp257-267 underpin persistent RX, non-aborting mask clear,
fresh idle confirmation and explicit FIFO drainage through RFD.
The register definitions RXMASKSET/RXMASKCLR resolve older prose names.

Disable source matching/AUTOPEND/PENDING_OR; incoming Pending is not echoed.
Broadcast filtering has no express AUTOACK broadcast exclusion. Opaque
security-enabled bodies are not excluded by this register profile, and
compatibility with all ignored ACK-FCF variants accepted by `mac_tx` remains
unproved. Sticky TXACKDONE supplies neither a per-frame ACK count nor time.
RX-to-RX timeout removal is not MAC IFS; physical stop/drain is not receiver-on
POLL CLOSED. These limits are part of the contract, not hidden test restrictions.

SWRS081B (revised February2011), Table2 p24 supplies the reference raw05 power
profile, not calibrated board output. SWRZ031 (April2009), sections1.1-1.2
pp2-3 covers DMA/Timer2 issues; this owner uses neither. All implementation,
synthetic controller/vector and linked proof code is original BSD-3-Clause.
No SDK code, Contiki implementation, capture, identity or hardware observation
is imported; no primary manual is redistributed.

The later [#50 primary review](RADIO_AUTOACK.md#ordinary-tx-admission-under-live-autoack)
separates supported persistent RX/FIFO preparation/soft stop from unresolved
ordinary-TX admission. SWRU191F23.8.1 p218 and Fig23-20 p235 differ in their
stated STXONCCA source states; pp249,253 do not settle pending/active ACK
arbitration. RFIRQF1 pp210-211 does not expressly establish AUTOACK/TXDONE
exclusivity, and Fig23-6 p220's ACK-to-TXFIFO independence does not prove the
reverse TX-flush-to-ACK guarantee. SWRZ031 pp2-3 adds no clarification.
These are explicit evidence gaps, not assertions of unsupported silicon or
permission to invent a successful concurrent model.

The #72 same-owner rearm extension reread SWRU191F23.9.1-2 pp222-223,
Fig23-20/Table23-3 pp235-236 and RXENABLE/RXMASKSET/RXMASKCLR p260 from the
same PDF, SHA-256
`a8fe8e92db33ad79c7f371075b0a464602a6db747614625d9f8d3e6be990b877`.
These explicitly support enabling RX from idle with the mask; no inference
about ordinary-TX/ACK arbitration is needed for this limited operation.
The original stop/drain proof establishes its OFF precondition. The old
RFIDLE bit is retained evidence of that stop, not a newly captured event.
Tests use original synthetic controller inputs; no hardware observation,
SDK implementation, private material or manual redistribution is added.

The #73 same-owner ordinary TX phase additionally uses the same SWRU191F:
section23.8 pp218-222 for STXONCCA, retained TXFIFO contents, RFD preload and
AUTOCRC length; p222 for RSSI_VALID plus four system clocks; pp253-254 for
ISTXONCCA=EA and TX-only ISFLUSHTX=EE; p259 for AUTOACK disable; p260 for the
persistent RX request; pp264-265 for CCACTRL0=F8, mode3/hysteresis2=1A and TX
count/pointers. TXFIRST is the next byte to transmit, not a permanent zero.
After PHY completion the test model advances it and preserves TXFIFO contents.
Disabling AUTOACK/filtering only after verified stop/drain avoids, rather than
settles, #50's three live-AUTOACK unknowns. The four-NOP helper, emitted MMIO,
synthetic TX/response model and bounded fault behavior are original work.
No RF measurement, captured timing, ACK acceptance, SDK code or private data
is introduced; the public manual remains a reference, not a CI dependency.

The [#77 same-owner board fixture](RADIO_LINK_FIXTURE.md) is original
composition of those already-reviewed services, the existing board/startup
policy and the real clock/Sleep Timer. It introduces no external implementation
or new capture/timing interpretation. Public synthetic addresses, the `LNK1`
diagnostic body and original native controller inputs are not device identities
or captured traffic. Fresh SDCC4.2.0 links establish both board identities,
complete raw metadata and allocation/MMIO inventories. The manual operator
reuses the guarded debugger and private-artifact writer; received bytes stay
outside Git and CI. Offline evidence does not establish physical TX/RX,
AUTOACK timing, calibrated power or interoperability.
The later [#78 LG physical record](DEBUGGING.md#2026-09-22-lg-same-owner-txrx-sequence)
uses that unchanged accepted image and the existing unchanged Nordic sniffer:
one exact public-body capture, an empty CC2530 receive interval and confirmed
stop. It introduces no third-party firmware or private capture into the
repository and does not establish positive RX, independent FCS or MAC timing.

### Nordic laboratory stimulus sources

The separate [NS51 source/provenance ledger](../tools/nrf_stimulus/PROVENANCE.md)
records the original application, portable control/UART logic, codecs and
offline tests. The small `phyend-observer.patch` imports reviewed context from
Nordic's BSD-3-Clause `nrf_802154_core.c`, preserving its complete per-file
notice; no sample command processor or full SDK tree is vendored.

The external pins are sdk-nrf `3190fa573ff67bfb745028f203e9d0ea4144a1ce`,
sdk-zephyr `d96769facecaba386b642d2c76c92c7694c81da0`,
sdk-nrfxlib `13cd978b22d192447537a60f7fae5fe092930dc4`,
hal_nordic `5470822384781624efb2fda28cbc6a895a227677` and the locked CMSIS
revision in `dependencies.json`. Radio and SL are source-built; this is not a
claim that all SDK files share the project license or that the compiler was
rebuilt. Zephyr/CMSIS Apache-2.0, Nordic integration LicenseRef-Nordic-5-Clause
and toolchain/runtime terms remain separate. SDK dependencies and generated
firmware stay outside Git/CI.

Vendor-authored source/API contracts establish PHYEND dispatch, direct callback
bookkeeping, ordinary promiscuous RX, buffer return and the actual priority0
interrupt configuration. They are not measured radio timing. The explicit
build audit checks selected source objects, startup bodies/macros and final
ELF/HEX ranges; it does not grant authority to run arbitrary untrusted SDK
build metadata or to program a device. Conditional volatile APPROTECT handling
does not become a promise of debug access. No private identity, capture, backup
or hardware observation is imported by this implementation.

The original BSD [private acquisition operator](NRF_RECOVERY.md#read-path-source-contracts)
uses public OpenOCD command interfaces and vendor register facts, not an
imported Nordic recovery script. Its linked source ledger pins OpenOCD
`9ea7f3d647c8ecf6b0f1424002dfc3f4504a162c` and the same reviewed Nordic MDK
revision. The separately documented host-programmer preservation patch
retains its own OpenOCD license; it does not relicense original project code.
No upstream `nrf52_recover` procedure, firmware, private identity or backup
is incorporated into the operator or its synthetic Tcl backend.

The original BSD-3-Clause [offline overlay planner](NRF_RECOVERY.md#offline-page-overlay-report)
reuses the existing private-file checks and ELF/HEX parser. Its byte/page
arithmetic and synthetic tests import no vendor writer, SDK or device backend.
The 1-MiB/4-KiB artifact geometry is assumed, not detected; strict artifact
comparison does not establish silicon, startup or restoration compatibility.
Actual captured bytes and derived private hashes remain outside Git/CI.

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

### RF-noise and entropy-assessment sources

The #10 [qualification boundary](ARCHITECTURE.md#rf-noise-entropy-qualification-boundary)
is a primary-document review, not a noise sampler or cryptographic RNG.
It imports no implementation, vectors, SDK, actual noise data or equipment
observations. The original BSD-3-Clause documentation summarizes functional
requirements; complete source documents remain external.

The directly retrieved documents have these exact identities:

| Primary document | SHA-256 |
| --- | --- |
| [TI SWRU191F](https://www.ti.com/lit/pdf/swru191), April2009/revised April2014 | `a8fe8e92db33ad79c7f371075b0a464602a6db747614625d9f8d3e6be990b877` |
| [NIST SP800-90B](https://doi.org/10.6028/NIST.SP.800-90B), January2018 | `9b0dd77131ade3617a91cd8457fa09e0dc354c273bb2220a6afeaca16e5defe7` |
| [SP800-90B potential corrections](https://csrc.nist.gov/files/pubs/sp/800/90/b/final/docs/sp800-90b_errata_potential_updates.pdf), updated May29,2025 | `af99ae20161aeed6e8eab88badf650e08c9f5ac455b71cf1095c289c104a5128` |
| [NIST SP800-90C](https://doi.org/10.6028/NIST.SP.800-90C), final September2025 | `22dc2de903b2fe602fa0729c38b9512927ad96ef5669a3da8a0ffb4d22c8e6d8` |

| Primary location (printed pages) | Established fact and limit |
| --- | --- |
| SWRU191F14.2.2 p.144 | CC253x RF IF_ADC bits may seed the separate LFSR, with the receiver on and synchronization avoided; harvesting cannot occur during normal radio work. This is not a cryptographic construction. |
| SWRU191F23.12 pp.236-237 | Receiver settling/RSSI-valid and I/Q raw-bit access; roughly20 million bytes illustrate measurable DC bias. Neither sampling-rate/independence guarantees nor a security min-entropy estimate is supplied by those passages. |
| SWRU191F FRMCTRL0 p.259; RFRND p.272 | RX_MODE10 FIFO looping differs from11 symbol-search disable. The CC253x RFRND address is61A7, with IRND0/QRND1 and reserved upper bits. Do not substitute the CC2541 proprietary-radio register in chapter25. |
| SP800-90B3.1.1-3.1.4 pp.9-14;3.2 pp.18-21 | Raw sequential/restart datasets, justified IID versus non-IID assessment, source/environment/security-boundary documentation and noninterfering acquisition. A local test is not accredited validation. |
| SP800-90B3.1.5 pp.14-17;3.2.3 pp.19-20 | Entropy accounting and vetted/non-vetted conditioning distinction; AES-CMAC is listed, but AES hardware or a hash alone is not a qualified entropy source. |
| SP800-90B4.2-4.4 pp.22-27 | Startup/on-demand withholding, at least1024 startup samples, continuous raw health tests and explicit errors. RCT uses assessed H; APT uses1024-sample binary or512-sample nonbinary windows, not a generic sliding window or a packed-byte reinterpretation. |
| SP800-90B potential corrections, p.1 | Proposed numerical fixes affect5.2.4 p.39 and the6.3.1 example p.42. The notice explicitly says these are not official changes; future estimator/vector selection must account for this status, not silently substitute corrected or erroneous arithmetic. |
| SP800-90C abstract;5/5.3 pp.49-56 | Final RBG constructions combine entropy sources and DRBGs. RBG2 requires reseeding before generation once at least2^17 output bits have been generated since instantiation/reseed; actual rendered p.55/PDF69 confirms the exponent. Construction choice, approved source material and seed/reseed accounting remain open here. |

Neither the TI statistical illustration nor a NIST health-test pass is adopted
as a measured CC2530 entropy rate. The review selects only an IRND
characterization candidate and a fail-closed assessment boundary.
Hardware characterization, independently checked conditioning/DRBG behavior,
resource proofs and the separate BDB security/commissioning gates remain open.

The subsequent #65 [binary health-test core](ARCHITECTURE.md#binary-raw-noise-health-test-foundation)
is original BSD-3-Clause C with original synthetic corpora and a separate
whole-prefix oracle. It implements the functional RCT/APT rules of
SP800-90B4.4.1-2, pp.25-27 and the1024-sample startup accounting of4.3,
without importing NIST code or actual noise data. The illustrative21/589
cutoffs used in some tests correspond to `H=1, alpha=2^-20`, not a CC2530
estimate. Other explicit diagnostic cutoffs exercise boundary/failure
behavior. A periodic balanced passing sequence documents the tests' limited
meaning. The genuine compiler/image evidence establishes software behavior
only; no source qualification, RNG construction or new hardware observation
follows.

The subsequent #66 [isolated IRND collector](ARCHITECTURE.md#isolated-raw-irnd-acquisition)
and its synthetic model/caller/proofs are original BSD-3-Clause code, not
vendor or NIST implementations. SWRU191F FRMCTRL0 p.259 supplies RX_MODE11
and AUTOACK0; RXMASKCLR p.260 supplies the owned-bit80 soft stop; RFRND p.272
supplies IRND0/QRND1/reserved bits. Existing reviewed radio settings and the
[FSCAL1 readback distinction](RADIO_RX.md) are reused without weakening other
register checks. Table2-3 instruction encodings and actual SDCC4.2 relocated
listings establish the extra register ANL/ORL/ADD/ADDC/XRL/XCH, MUL, RL/RRC
and DEC instruction lengths used by the checker. No hardware-model timing,
diagnostic interval, synthetic pattern or software health pass is adopted as
a physical source guarantee. Real raw samples remain outside Git/CI.

The #67 [boot-disarmed board composition](RADIO_NOISE_FIXTURE.md) and its
fixture-specific native model/linked proof are original BSD-3-Clause work.
No #66 production or standalone test implementation was imported or replaced.
Primary TI SWRU191F (April2014) register facts above were checked directly,
along with CLKCONCMD/STA pp.68–69, IRCON.STIF and Sleep Timer compare
section11.2 (reset compareFFFFFF). SWRZ031 (April2009) issues1/2 concern
DMA variable lengths and Timer2 latching, neither used by this fixture.
The real existing board startup/clock/health services and SDCC4.2 runtime
retain their own reviewed provenance. No physical samples, private identities,
new dependency, vendor SDK object or hardware evidence is imported.

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

### Offline MAC transmission scheduler sources

The [scheduler, tests and event contract](MAC_TX.md) are original BSD-3-Clause
work, reusing this repository's MAC codec rather than an external stack.
Primary [IEEE Std 802.15.4-2006](https://people.ece.ubc.ca/~edc/7860/data/802.15.4-2006.pdf)
supplies the selected 2450-MHz O-QPSK symbol/backoff, NB/BE, retry, ACK and IFS
facts. The exact document identity and printed-section/page derivations are
recorded in the dedicated contract. The copy's SHA256 is
`d245c8bb208f6cdb585fb753cefa67e367d30eebd55b9ed9957c8fd152d6a055`.
The PDF and temporary research tools are not vendored or CI artifacts.

The Beacon Request extension uses section 7.3.7 p.156/Figure62 and the
selected nonbeacon-enabled channel-access scope in section 7.5.1.1
pp.167-168. It reuses the actual command/header codec, with TX Pending
rejection; its eight-byte body does not require ACK or frame retransmission.
Section 7.5.2.1.2 pp.173-174 was read directly to separate that command from
the separate PAN-filter/channel/receive-window procedure. That procedure now
has an [offline controller](MAC_SCAN.md), not a hardware adapter.

Association Request admission uses IEEE2006 sections7.2.2.4.1,7.3.1.1-2,
Figures55-56 and the same pinned R22 section3.6.1.4.1/Table3-62 pp.336-337.
The selected capability88/8C maps ED, receiver-on, address allocation and
caller power, without coordinator/FFD/MAC-security advertisement. Identity,
permitting-parent choice and actual power capability remain caller facts.
An explicit positive command-ID whitelist follows the unchanged real codec;
the final capability octet is inspected only for an identified Association
Request. A compact six-field reset is checked against the original individual
assignments, preserving all other context bytes and error behavior. No
association, BDB or security procedure is
implemented by transmitting this request or receiving its ACK.

Addressed Data Request admission uses the same IEEE2006 primary copy,
section7.3.4 pp.153-154/Figure59, read directly. It selects the addressed,
compressed-PAN, ACK-requested form with TX Pending0, excluding destination-less
requests and broadcast PANs. Address identity/PAN/coordinator and the required
extended source when retrieving an Association Response remain caller facts.
The unchanged codec and scheduler supply syntax, DSN/retries/ACK/IFS;
neither an ACK nor Pending creates a polling or response-retrieval procedure.
Original synthetic vectors cover all four selected address layouts; no
implementation, capture or device identity was imported.

The later transmitter control-staging refactor is original work on this
repository's implementation. A private ordinary-XDATA suffix mirror and
per-field compile-time layout assertions preserve the public generic-pointer
ABI, native padding and inactive bytes; no second frame or external algorithm
is introduced. CODE/DATA reductions and stack measurements come from actual
SDCC objects, linked images and alias-aware execution, not hardware.
The unchanged native corpus, native old/new comparison and coupled scan proof
provide behavioral evidence without importing an SDK or reference stack.

The receive-only ACK normalization follows the selected edition's ignored
FCF-subfield rules, then calls the unchanged strict codec; it is not a
loosening of general frame admission. Single-slot capacity, generation tags,
work/lifetime limits, ordered event delivery and the independent cleanup
budget are explicit project policies. Synthetic randomness is not security
entropy. No trace, identity, SDK, driver implementation or hardware
observation is imported. This is not an IEEE conformance or RF-timing claim.

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

The engineering scope is the legacy IEEE 802.15.4-2006-compatible DATA/ACK,
five-command and no-GTS Beacon wire subsets documented in [MAC.md](MAC.md),
not complete standard conformance.
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

The Beacon extension was checked directly against the same IEEE 2006 source:
section 7.2 (pp.137-138) for byte order and the reserved-bit receiver rule;
sections 7.2.2.1.1-8 (pp.143-146), Figures 44-51 for source-only MHR,
superframe/GTS fields, short-before-extended pending lists and the combined
seven-address bound; Table 85 (p.159) for the 52-byte upper-layer payload
limit (`127 - 75`). Section 7.5.1.1 (pp.167-168) was read to distinguish raw
BO/SO metadata from scheduling and the nonbeacon-enabled case. This slice
does not claim to validate those timing/procedure rules.
Zero GTS descriptors, strict reserved-field rejection and rejecting source
PAN `FFFF`/source short `FFFE` are explicit subset choices, not permission to
relax the standard's broader receiver behavior. The pending-list rule
specifically excludes short `FFFF`, without inventing a second sentinel rule.
The implementation and vectors are original. The public PDF and temporary PDF
reader were used only for offline research and are not repository/CI artifacts.

Contiki's RF code depends on Contiki facilities. An adaptation must replace
those interfaces deliberately, preserve the original notices and be tested
against the CC2530 documentation. It does not supply Zigbee NWK/APS/ZDO.

The inspected legacy ZBOSS tree documents TI CC253x use with IAR and an SDCC
simulator configuration. The presence of both words in one repository is not
proof of a tested native CC2530/SDCC port. Its old protocol implementation is
not automatically compliant with the project's intended R22/BDB behavior.

### M2 reserved flash reader sources

The flash reader and its host/linked test models are original BSD-3-Clause
code. Primary functional facts come from TI **SWRU191F**: pp.27-28 distinguish
physical flash, the read-only XBANK window, information page and lock page;
p.34 defines MEMCTR.XBANK/XMAP and FMAP; p.59 defines CHIPID/CHIPINFO0/1;
pp.73-77 define 2-KiB pages, 4-byte programming words, controller status,
flash-fetch stalls and program/erase restrictions.

Pages125/126 are this project's partition choice, not a vendor NV layout.
The read-only implementation does not copy the manual's IAR erase example
or any SDK/programmer routine. No private flash content, identity or external
binary is used as a test vector. The simulator's patterned XDATA window is
synthetic and does not emulate flash physics or prove the hardware bank mux.
No write/erase or recovery claim follows from these tests.

### M2 RAM flash executor sources

The internal executor, naked SDCC template/trampoline, C wrapper and test
models are original BSD-3-Clause code, not the manual's IAR example or an
imported SDK/programmer algorithm. Functional facts are from **SWRU191F**:
pp.25,33-34 define the shared CODE/XDATA bus, CPU/DMA arbitration and XMAP;
pp.34-36 describe bank0/DPS and stack/return state; pp.73-77 specify the
four-byte write, 20-us data window, FCTL flags, FADDR and FWDATA.
Pages125/126 and the retained RAM fail-stop policy are project choices.

Table2-3, pp.37-39, supplies the instruction-cycle values used for the
42-clock staged command/data path. Its p.37 warning explicitly describes
best-case memory access, so that count is not a measured or general
worst-case wall-clock bound. No generic uCsim timing is presented as
CC2530 timing. The model supplies controller events and XMAP address
decoders; the simulator executes the actual C-copied instructions.
Neither model establishes physical flash contents, wear, transfer timing,
electrical interruption or hardware recovery. No external binary, private
device data or manual example is a test vector.

### M2 reserved-page write policy sources

The public writer and its composition tests are original BSD-3-Clause code.
They reuse the reader and RAM executor, not a vendor/library flash routine.
SWRU191F sections6.1/6.2, p.73 establish 2-KiB erase pages, four-byte words,
erase-to-one and programming-zero semantics. Section6.2.2, p.74 limits a zero
bit to two writes, a word to eight writes and a page to1,024 writes per erase.
The project's stricter policy uses one attempted write per word and requires
a new actual verified erase after loss of volatile history; unchanged allFF
readback is not evidence of an unattempted word.

The native backing array models erase-to-FF, bitwise-AND programming,
ignored/aborted/stuck commands and intentionally missing/partial effects.
The linked test instead executes all three genuine modules with explicit
synthetic controller/XMAP/window transitions. Neither proves flash physics,
endurance, data retention or electrical interruption. No factory record,
private recovery dump or external binary is used as input or output.

### M2 boot-disarmed flash fixture sources

The original BSD-3-Clause board fixture, packet codec, strict decoder, image
proof and synthetic tests reuse the published flash services and native model.
They import no IAR sample, SDK code/object, GPL programmer implementation,
dependency, board asset or private recovery material.
The hardware contract is unchanged: **SWRU191F, revised April2014**,
sections6.1/6.2.2/6.3 (pp.73-77: pages, word limits, FCTL/FADDR/FWDATA),
memory mapping p.34 (MEMCTR.XBANK/XMAP), and Tables3-1/3-2 pp.53-55
(target debug commands/configuration; DMA_PAUSE prohibition). The previous
SWRZ031 April2009 review lists DMA variable-length/Timer2 issues, neither
of which supplies flash/debug recovery or timing acceptance for this fixture.
No CC253x/CC254x behavior or new USB packet is inferred.

The two public packet tokens,256-poll bounds, one selected page, terminal
sequence and private-preservation checklist are project policy, not TI
authentication or a vendor recovery algorithm. The
[debugger blockers](FLASH_FIXTURE.md#debugger-visibility-precise-unresolved-blockers)
come from inspection of the actual public transport: lower32KiB CODE/breakpoint
bounds, safe-SFR whitelist and DEBUG_INSTR-based register-preserving access.
SWRU191F is not adapter USB framing documentation; these changes add no
transport operations. Existing external-programmer/no-run-guard references
retain their separate provenance and limited scope.

Both board definitions have only [offline evidence](VALIDATION.md#boot-disarmed-flash-fixture-coverage).
No physical board, backup, capture or identity was accessed. No observed-board
entry or completed #8 gate is invented; the future private backup/full
excluded-region verification and reset/interrupted-command recovery
procedures remain explicitly blocked on new authority and hardware evidence.

### Bounded radio ownership queues

The queue implementation and its tests are original BSD-3-Clause code.
Pool counts of two RX slots, one TX slot and four request cookies,
copy-based ownership and reject-new
overflow are project choices, not a claimed vendor MAC algorithm. IRQ
protection reuses the existing SWRU191F-backed EA token leaves; reception
reuses the unchanged passive RX and timebase code and their primary facts.
No ISR calls a foreground helper, packet codec or Sleep Timer reader.

Stateful synthetic MMIO vectors are emitted by the existing native RX model,
with real successful/BAD_CRC reuse rather than resetting hardware state
between calls. The linked image executes the actual receiver. Generic C52
external-interrupt preemption/RETI is an ABI test, not a CC2530 RF-IRQ or
timing observation. No SDK code, captured packet or device identity is used.

### Bounded init-time TX and CCA sources

The original BSD-3-Clause [TX/CCA code and synthetic model](RADIO_TX.md#primary-sources-and-provenance)
reuse this repository's real FIFO/timebase services. No SDK, IAR sample,
external driver, captured frame or programmer binary is imported. SWRU191F
sections 23.4.2, 23.8, 23.9 and 23.14.9 provide the FIFO/strobe/completion,
CCA-validity/four-clock and soft-shutdown facts. In particular, TXFIFO starts
at `6080`, not RXFIFO `6000`; source/address RAM remains uninspected.
SWRU191F Table 23-6 supplies recommended settings; CC2530 SWRS081B Table 2
supplies the explicit raw `05`/TXCTRL `69` profile and its typical reference-EM
conditions, not measured board power. SWRZ031 applicability was checked.

The native and linked tests share synthetic controller expectations. Their
agreement is not an independent silicon, timing or RF measurement. The
linked proof executes actual strobes and FIFO/timebase instructions and pins
the four genuine NOPs, typed ABI, private prefix and per-link listings.
The separately documented future fixture/capture procedure grants no device,
channel, power or recovery authority. The component checks remain offline-only.

The separate [boot-disarmed board fixture and original synthetic corpus](RADIO_TX_FIXTURE.md)
reuse these unchanged timebase/FIFO/TX implementations and existing board
policy. Its fixed public `TXF1` body contains no observed identity or capture.
ARM/RUN complements/guards, admission budgets and one-attempt policy are
project choices, not hardware authentication or MAC acknowledgement.
The clock staging prerequisite is original BSD-3-Clause work preserving the
SWRU191F pp.68-69 register contract and existing public ABI/rollback semantics;
it imports no SDK or compiler workaround from third-party implementations.
Strict emission checks identify the changed storage, helper declarations and
real instructions; synthetic peripherals remain distinct from physical proof.
The manual runner adds no new debugger USB/MMIO command, programming path or
automatic hardware/CI activity.
The separately authorized
[LG hardware record](DEBUGGING.md#2026-09-19-lg-single-attempt-tx-acceptance)
adds one channel15/raw05 PHY_DONE and an independent exact public-body match.
It retains the Nordic FCS limitation described above; no raw capture, private
factory information, backup hash or third-party implementation is imported.

The fixed channel26 profile uses the same documented channel/
FREQCTRL relationship and unchanged raw-power/CCA settings. Only five linked
channel operands change per board; complete private/public ABI and other
service listings remain identical. Original synthetic regressions reject the
old channel15 admission packets. The processed passive-survey counts are not
CCA silence, a capture of our transmitter or acceptance of the channel26
firmware. The dated channel15 hardware record remains tied to its old CODE;
no raw capture, identity, programming or new RF operation is introduced by
this offline profile update.

The later separately authorized
[channel26 hardware record](DEBUGGING.md#2026-09-19-lg-channel26-isolated-tx-acceptance)
adds guarded programming with independent complete-CODE/factory checks,
one PHY_DONE and one exact public body in a completed one-record Nordic capture.
The private capture helper permits header-only startup for a planned quiet-
channel test but still rejects an empty final capture; it does not reuse
empty-survey success as received-frame evidence. Channel readback and clean
shutdown are separate from physical frequency/FCS/calibration claims.
No private bytes, identities, backup hashes, captures or external tool code
are imported by that processed record.

### Generic two-page NV record sources

The [snapshot journal](NV_RECORDS.md) and its format, selection, commit-last
and test policies are original BSD-3-Clause work. They compose the existing
SWRU191F-backed flash services without changing their instructions, geometry,
runtime history or fail-stop contract. No vendor journal, filesystem, SDK
implementation or private NV image is imported.

CRC32 is implemented directly using the reflected `EDB88320` polynomial and
all-one initial/final XOR. Native test division uses the non-reflected
`04C11DB7` form with explicit bit reversal; linked expected records additionally
use standard-library `zlib.crc32`. CRC agreement is not authentication.
Synthetic cuts and torn-cell patterns are original test inputs, not physical
cell characterizations. Runtime erase-attempt accounting does not recover
unknown lifetime wear or establish security-counter safety.

## Normative target

Engineering references:

- Zigbee Core R22, document **05-3474-22**.
- PRO Base Device Behavior Specification **v3.0.1**, document
  **16-02828-012**, September 28, 2021. The base revision is selected;
  applicable errata and implementation evidence remain separate
  [gates in the conformance ledger](CONFORMANCE.md).
- Zigbee Cluster Library **Revision 8**, document **07-5123-08**, release
  December 2019, for the bounded offline foundation work below. Its approved errata
  **19-2019** and application/device requirements remain unreviewed/unselected
  conformance risks/gates, not a blanket base-text development stop or an
  implied complete ZCL implementation.

BDB 3.1, document 22-65816-030, belongs with R23/PRO 2023. It may inform future
work but must not be cited as an R22 requirement. The project is not certified.

Key R22 references used in the plan: Table 2-44 p.84; section 2.4.4.1 p.137;
section 2.4.4.4.5 p.188; section 2.5.4.8.1 p.222;
section 3.6.10.2 pp.392-393; section 4.3.4 pp.416-417.
Paraphrases in this repository are an engineering aid, not replacement
specification text.

### BDB 3.0.1 baseline sources

The selected primary source is the
[official CSA BDB v3.0.1 PDF](https://csa-iot.org/wp-content/uploads/2022/12/16-02828-012-PRO-BDB-v3.0.1-Specification.pdf),
linked from the
[CSA specification download page](https://csa-iot.org/developer-resource/specifications-download-request/).
The title page identifies **16-02828-012, September 28th, 2021**; the revision
history on p.5 identifies v3.0.1 as derived from 16-02828-011. Neither the
2022 copyright nor the upload-directory date replaces the document date.
The reviewed 86-page PDF has SHA-256
`16471aa230657818da4c8440671efb530d80c71a975fce7af71c507ca7aa17d3`.

[Microchip's reference list](https://onlinedocs.microchip.com/oxy/GUID-1DD68C79-8AC2-497D-A1BB-49D92D3FDAB8-en-US-5/GUID-4EC58238-EB93-489F-B9BC-A237F17AE18E.html)
explicitly lists BDB v3.0.1 alongside Core R22 1.0, 05-3474-22.
The [CSA ZUTH page](https://csa-iot.org/certification/tools/zuth/) independently
names BDB v3.0.1 as a test target. These confirm the version context, not
this implementation's interoperability or a unique mandatory pairing for
every R22 product. BDB section 2 [R1] references the Core document family
05-3474 without pinning a revision; this project explicitly selects R22.

| Primary location (printed/PDF pages) | Facts reviewed |
| --- | --- |
| Sections 1.5-6,2, pp.15-17 | Separate v3.0.1 Test Plan 16-02826 and Specification Errata 21-65431 |
| Sections 5.1,5.3, pp.22-31 | Constants, commissioning capability/status, channel sets and key-exchange attributes |
| Sections 6.1-10, pp.32-38 | ED security models/keys, required versus optional commissioning, ZDO/application minimums, persistence and role-limited Green Power requirement |
| Section 7.1, pp.39-40 | Persisted ED initialization with secure NWK rejoin |
| Sections 8.1-2, pp.41-47 | Already-joined versus unjoined steering, authentication, key exchange, failure and permit-join broadcast |
| Section 9, pp.69-71 | Factory reset, optional Basic reset and outgoing NWK counter preservation |
| Sections 10.1-2, pp.72-79 | Install-code CRC/MMO, TC identity/policies and request/verify/confirm sequencing |

The TC exchange was cross-checked against R22 section 2.3.2.3.10/Table 2-32
p.72 and sections 4.4.7-8 pp.439-446, particularly Confirm-Key.indication
validation in section 4.4.8.2.3. The retained R22 counter requirement is
section 4.3.4 p.416. Exact errata/test-plan revisions and contents have **not**
been reviewed; do not infer that the errata is empty or substitute the older
BDB 1.0 errata 15-02020 or test specification 14-0439.

The separately inspected
[BDB 1.0 PDF](https://csa-iot.org/wp-content/uploads/2019/12/docs-13-0402-13-00zi-Base-Device-Behavior-Specification-2-1.pdf)
identifies **13-0402-13, February 24, 2016**. It is not BDB 3.0/3.0.1 and is
not the selected baseline. For example, the selected v3.0.1 explicitly makes
already-joined steering optional and includes the CRC in its install-code
hash example. Old summaries and section numbers must not replace this text.

This is primary-document review only, not host-tested, image-checked,
simulated or hardware-observed BDB behavior. No implementation, sample code,
keys or test-plan vectors were imported. PDFs and extraction tools remain
temporary research inputs, not Git or CI artifacts; the specifications'
notices and licensing are not replaced by BSD-3-Clause.

### Offline R22 NWK Beacon payload sources

The original decoder was checked directly against **Zigbee Specification
Revision 22 1.0**, document **05-3474-22**, April 19, 2017, using this public
[unaltered-document mirror at a pinned revision](https://github.com/pvginkel/ZigBeeHomeAutomation/blob/fc30145012eacd3a5af170b8ae8e0d4c848c2525/Documents/docs-05-3474-22-0csg-zigbee-specification.pdf).
The title/revision/date were read from the document, not inferred from a
search summary or the filename.

| Primary location (printed pages) | Functional facts used |
| --- | --- |
| Section 3.3, p.288 | Least-significant octet first convention |
| Section 3.5.1, Table 3-57, p.322 | `nwkcProtocolVersion = 2` |
| Section 3.6.7, Table 3-71, pp.389-390 | Protocol ID 0, profile/capacity/depth fields, advertised Extended PAN ID range `1..FFFFFFFFFFFFFFFE`, symbol-time Tx Offset and `FFFFFF` beaconless default, Update ID |
| Figure 3-54, p.391 | Exact 120-bit/15-byte field layout, nibble/flag placement and reserved bits |

Nonzero reserved bits are rejected in the documented strict subset.
Stack-profile values remain raw metadata rather than a supported-profile
decision; version 2 alone does not establish R22/BDB compatibility. The NIB's
separate zero/unknown Extended PAN ID is not substituted for the Beacon
table's valid advertised range. No scheduling, admission, replay/freshness
or commissioning rules are implemented.

No implementation, vectors, protocol capture or SDK object was imported.
Test identities and payloads are original synthetic data. The PDF and
temporary PDF reader are research-only and are not shipped in Git or CI;
the original document's notices and licenses are not replaced by BSD-3-Clause.

### Awake MAC Timer foundation sources

The [Timer2 service and synthetic tests](MAC_TIME.md) are original
BSD-3-Clause work. Functional facts come from TI SWRU191F, April2009/revised
April2014, sections4.4-4.5 and chapter22: system-clock ownership, asynchronous
first start, positive period replacement, RUN versus STATE, CC253x event
selectors, common live latching and masked flags. The exact register/page
derivations are recorded in the dedicated contract. SWRZ031, April2009/history
2009-04-29, section1.2 pp.2-3 supplies the low-byte-FF latch workaround.

SWRS081B, April2009/revised February2011, p.21 and SWRU191F's overview p.22
advertise end capture, whereas the register-level procedure in section22.1.10 p.199
establishes SFD-rising capture. This unresolved distinction, capture-register
overwrite/coherence and freshness prevent adding a captured-end API.
CC2541 proprietary-mode TXCAP/RXCAP controls in Table25-13 p.304 are not
CC2530 controls and are not transferred. No SDK implementation, private
capture, identity or recovery material was imported; manuals remain linked
references, not redistributed artifacts. Offline models do not establish
physical capture semantics, calibration or PM/debugger continuity.

The additional [TI E2E thread90922](https://e2e.ti.com/support/wireless-connectivity/other-wireless-group/other-wireless/f/other-wireless-technologies-forum/90922/cc2530-timer-2-capture-function)
was read directly, including MaMoe's reply316742. Its analog/SFD delays are
estimates; its explicit absence of evaluated worst-case/RMS jitter prevents
treating them as guaranteed timing corrections. The [MAC Timer source
assessment](MAC_TIME.md#additional-ti-support-evidence-estimates-are-not-bounds)
records that limitation and the still-open capture contract. No forum code,
SDK implementation or physical measurement is imported.

The [#75 fractional epoch extension](MAC_EPOCH.md) uses only the already
reviewed positive-period replacement rule in SWRU191F22.1.3/.8 and T2IRQF,
pp198-199,204. ModuloFFFFFF coarse subtraction, fine borrowing and the
7FFFFF-period-plus256-fine half-range threshold are original arithmetic
derivations, not an additional timing guarantee from TI. The independent
host oracle uses complete64-bit coordinates; production uses bounded
32/16-bit operations. No captured-edge selection, offset correction,
freshness claim, foreign implementation, new dependency or hardware access
is introduced. The original timer and radio ownership contracts are unchanged.

The [#80 co-owned live clock/radio composition](MAC_RADIO.md) and combined
synthetic controller are original BSD-3-Clause work reusing these existing
services/models. The explicit active-radio reader uses SWRU191F (April2014),
sections22.1.2/.6 and Timer2 registers pp203-206; RXENABLE/FSMSTAT0/1
pp260,262-263; and SWRZ031 (April2009), section1.2 pp2-3. Known common-owner
history is a caller precondition, not something those register observations
can prove. Ordinary quiescent-only timer APIs remain strict. No foreign
implementation, captured-event guarantee, new dependency or hardware access
is introduced.

The [#81 delayed-sample projection](MAC_STAMP.md) is original BSD-3-Clause
arithmetic using the same already-reviewed positive-period modulus and
half-range rule. It reuses this repository's actual `mac_epoch_step`, with an
independent64-bit host oracle and genuine SDCC execution. The closed-window
membership argument is a mathematical derivation, not a new TI statement or
capture guarantee. No external implementation, new normative revision,
hardware measurement, device access or dependency is introduced.

### Offline ED Beacon candidate sources

The [four-entry collector](NWK_CANDIDATES.md), its copied-record policy and
synthetic corpus are original BSD-3-Clause work using this repository's
unchanged MAC/NWK decoders. IEEE 802.15.4-2006 sections 6.1.2.1,7.2.2.1
and7.5.1.1 provide channel-map, Beacon and BO15/SO-ignore facts.
The same pinned R22 document above supplies ED capacity (section3.6.7),
profile2 identification (Annex D, pp.520-521) and the additional requirements
that distinguish collection from parent selection (sections3.6.1.3-4,
pp.335-336). Exact hashes and derivations are in the dedicated contract.
No Enhanced Beacon implementation is inferred from Annex D's identifier.

Capacity4, the NWK-aware identity key, last-observation replacement, withdrawal
and deterministic full-table policy are explicit project choices. They do not
implement complete discovery, link quality, update freshness, authentication
or BDB procedures. All addresses/payloads are synthetic; no SDK, private capture,
identity, key or third-party implementation is imported or uploaded.

### Offline parent-choice sources

The intervening [bounded parent-choice step](NWK_PARENT.md), #69, is original
BSD-3-Clause code over the unchanged collector/getter. The same raw-byte-pinned
R22 primary PDF was read directly:3.6.1.4.1 printed336/338 for selected network,
cost/capacity/potential-parent/Update ID requirements and profile2 depth exclusion;
3.6.3.1 printed367 for link-cost bounds and the permitted constant7.
The optional explicit watermark, strict modulo256 half-range/ambiguity rule,
all-candidate dominance, and cost/index tie-break are documented project
policies, not asserted normative formulas or authenticated freshness.
Synthetic Beacons and an independent linear-window test oracle are original.
No external implementation, PDF, private RF data, identity or key is imported.

### Offline active-scan controller sources

The [bounded controller and original synthetic corpus](MAC_SCAN.md) compose
the unchanged MAC transmitter and candidate collector, not a vendor scanner.
The pinned IEEE 802.15.4-2006 source above supplies channel order/page0,
duration0-14 and the960-symbol base duration, canonical Beacon Request,
PAN save/FFFF/restore, Beacon-only observation and requested-but-unscanned
semantics: sections6.1.2.1,7.1.11.1-2,7.3.7,7.5.2.1-1.2 and
Tables67/68/82/85. The dedicated contract records printed page locations.
The same R22 sections3.6.1.3-4 and3.6.7 distinguish preliminary collection
from complete discovery/parent selection.

Continuing after four candidate entries, sticky capacity-loss reporting,
saved channel/filter/RX restoration, separate cleanup limits and the serialized
foreground pump are explicit project policies. They do not implement the
IEEE PAN-descriptor-limit/macAutoRequest algorithm or fabricate its status
codes. No association, security, BDB or radio/capture facts are inferred from
successful synthetic events. No SDK implementation or private material was
imported; all new code/tests are original BSD-3-Clause work.

### Offline Association Response context sources

The [context, API and synthetic tests](MAC_ASSOCIATION.md) are original
BSD-3-Clause work using the existing MAC frame/command decoders. The same
pinned IEEE2006 primary text supplies Response layout/status/address rules
(7.3.2,Figure57,Table83,pp.151-152), selection and coordinator-IEEE learning
(7.5.3.1,pp.179-181), receive filtering and independent ACK obligations
(7.5.6.2-4,pp.186-189). R22 3.6.1.4.1/Table3-62,pp.336-340 distinguishes
MAC metadata from parent selection, allocated ED addressing and authentication.

The dedicated contract records the exact copies/hashes, printed pages and
unresolved decision-wait/extraction wording; no guessed total association
timeout is implemented. Half-open context lifetime, finite work, epoch/
generation correlation and one-shot result are project policy, not IEEE
primitive status or authentication. Unknown IEEE sources remain explicitly
unbound to the selected short address; Extended PAN ID is not IEEE identity.
No vendor implementation, SDK, private capture/identity or key was imported.

The #45 follow-up additionally examined IEEE7.1.3,7.1.16,7.4.2/Equations13-14,
7.5.5-6 and the7.7 sequence-chart limitation, plus R22 AnnexD.1/D.3/D.6.
The [staged contract](MAC_ASSOCIATION.md#staged-timing-and-confirmation-gate-45)
records exact printed equation grouping, separate POLL/Association
confirmations, and R22's IEEE2015/additional-header requirements. This is
primary-document review, not new executable, simulated or physical evidence.
It does not silently repair IEEE2006 terminal timing or upgrade the codec's
selected revision.

The #63 receive-profile addition rechecked the exact R22 PDF hash recorded in
[the Response contract](MAC_ASSOCIATION.md), and visually inspected Annex
D.3/Table D-3 on PDF page539/printed p.514 to preserve its alternative-row
layout. It adds original RX-only code and synthetic vectors for explicit
source PAN and broadcast destination PAN, not a copied stack implementation.
The selected PAN must still occur on wire for contextual acceptance; no
IEEE2015-wide procedure, Request/Data Request alternative or total timer is
inferred. The downloaded PDF, extraction and rendering remain outside Git
and generated CI artifacts; no new repository or CI dependency is required.

### Conditional legacy POLL extraction sources

The #64 explicit receive-path extension reuses #63's reviewed R22 Annex
D.3/Table D-3 classifier and synthetic Response layouts. It introduces no new
normative timeout, transmit format or general broadcast-DATA permission.
Its private argument staging and shared worker are original C/SDCC work;
real object/stack measurements rejected SP7F and verified the subsequent
SP7B result under the unchanged SP7C cap. Neither native inputs nor simulator
continuations are hardware observations.

The [controller, API and synthetic corpus](MAC_POLL.md) are original
BSD-3-Clause work composing the repository's real MAC codec, transmitter and
Association context. IEEE2006 7.1.16.1.3-7.1.16.3 pp.133-135 and7.5.6.3
pp.187-188 supply Pending0/1 and the distinction between DATA, empty DATA,
command delivery and POLL confirmation. Sections6.2.1.3 p.34 and7.5.6.2
pp.186-187 require complete reception, including FCS; a frame start is not
the accepted trailing-end timestamp. Section6.2.2.7.3 pp.39-40 separates
physical receiver shutdown from the logical deadline.

The caller-valid configured PIB F follows7.4.2 p.160/Table86 p.164; no
default, Eq.(14) repair or total Association timer is inferred. Sections
7.5.6.2/7.5.6.4.2 and7.5.1.3 retain immediate receiver ACK and IFS obligations,
which this foreground controller does not implement. R22 AnnexD.1/D.3
pp.513-514 retains the IEEE2015/additional-header gate. Exact document hashes
and printed-page derivations are in the dedicated contract.

Finite work/cleanup limits, action grants, copied receipts and loss-free
ordered closure are explicit project policies. The independent no-poll
diagnostic and complete simulator-state continuations are original validation
code, not replacements for real protocol calls. No SDK, third-party stack,
specification body, private capture, identity or physical observation is imported.

### Offline R22 NWK Data frame sources

The original `nwk_frame` codec uses the same
[pinned primary R22 PDF](#offline-r22-nwk-beacon-payload-sources), not a vendor
stack or generated catalog. Its downloaded Git blob was checked as
`c8123d63e30995e4a66941cbdf2529332480a0ff`.

| Primary location (printed pages) | Functional facts used |
| --- | --- |
| Sections 3.3,3.3.1, Figure 3-5, p.288 | Little-endian octets, fixed header order and optional-address order |
| Section 3.3.1.1, Figure 3-6, Tables 3-45/46, p.289 | FCF bit positions, Data type, unicast/broadcast field combinations |
| Sections 3.3.1.1.2-9, Table 3-47, p.290 | Protocol version, route-discovery values, extension/security flags, raw ED Initiator bit |
| Sections 3.3.1.2-7, p.291 | Network addresses, radius/sequence and optional IEEE fields; no destination IEEE on broadcasts |
| Sections 3.3.1.8-9,3.3.2.1, pp.291-293 | Excluded multicast/source-route structures and opaque Data payload |
| Table 3-57, pp.322-323 | Version 2, eight-byte minimum NWK header and 11-byte MAC overhead |
| Section 3.6.5, Table 3-69, pp.382-383 | Four defined broadcast destinations and reserved `FFF8..FFFA`/`FFFE` |

The [contract](NWK.md#nwk-data-frame-codec) selects unsecured Data with
8/16/24-byte headers and a 116-byte NPDU bound, using the existing MAC
125-byte FCS-free body limit and a nine-byte compressed short/short MHR.
Larger MAC headers impose their own smaller limit. Short source addresses
in the reserved/broadcast range and noncanonical FCF combinations are rejected;
IEEE identities, radius, sequence and ED Initiator remain raw metadata.
No routing, duplicate filtering, APS validation or security procedure is
inferred from successful serialization.

All vectors and identities are original synthetic data. No implementation,
capture, key material or SDK object was imported. The PDF and temporary reader
are research-only and are not shipped in Git or CI.

### Offline R22 APS Data frame sources

The original `aps_frame` codec uses the same
[pinned primary R22 PDF](#offline-r22-nwk-beacon-payload-sources), document
05-3474-22, April 19, 2017, with downloaded Git blob
`c8123d63e30995e4a66941cbdf2529332480a0ff`. No vendor implementation or
generated catalog was used.

| Primary location (printed pages) | Functional facts used |
| --- | --- |
| Table 2-2, p.21 | Destination endpoint `00..FF`, source endpoint `00..FE`, profile/cluster ID widths and ASDU service-length distinction |
| Section 2.2.5, Figures 2-2/3, pp.44-45 | Byte order, field order and FCF positions; reserved fields must be rejected |
| Tables 2-20/21, pp.45-46 | Data/command/ACK/Inter-PAN types, unicast/reserved/broadcast/group delivery; excluded group addressing |
| Sections 2.2.5.1.1-7, pp.46-47 | ACK-format, security, ACK-request, extended-header flags; endpoints, identifiers and counter |
| Sections 2.2.5.1.8-9,2.2.5.2.1, Figures 2-4/5/6, pp.47-49 | Excluded extended/fragmentation fields, eight-byte ordinary Data header and opaque payload |
| Table 2-23, p.51 | Separate service constant `apscMinHeaderOverhead = 0x0C`, not the eight-byte raw header size |
| Section 2.2.8.4, pp.57-59 | Network membership, endpoint delivery, duplicate/ACK/retry procedures are distinct from syntax |
| Section 2.3.1.3, p.66 | Endpoint 0/device profile, `FF`/all active endpoints, `F1..FE` restricted to Alliance-approved applications |

The [contract](APS.md) selects only normal-unicast Data, not APS
broadcast/group delivery, command/ACK/Inter-PAN types or security/extended
headers. Destination `FF` remains endpoint metadata, not a network broadcast
mode. No active-endpoint/profile admission, counter allocation, ACK state or
security procedure is inferred. The 108-byte raw APDU bound follows the
existing 116-byte NWK codec bound minus its minimum eight-byte header; outer
options reduce that budget. This does not replace APSDE-DATA service limits
with a claim that a real application can send 100-byte ASDUs.

All code, golden byte vectors and identities are original synthetic work.
No implementation, capture, key, test-plan vector or SDK object was imported.
The PDF and temporary reader remain research-only, outside Git/CI artifacts.

### ZCL Revision 8 wire sources

The selected primary source is the
[official CSA ZCL Revision 8 PDF](https://csa-iot.org/wp-content/uploads/2022/01/07-5123-08-Zigbee-Cluster-Library-1.pdf),
linked directly from the
[CSA specification download page](https://csa-iot.org/developer-resource/specifications-download-request/).
The cover names **Document 07-5123 Revision 8**, release **December 2019**.
The 2020 copyright and 2022 upload directory are not substitute release dates.
The 1,213-page PDF has SHA-256
`ad536e1d95a40ca27532e360b124cd76a1b18d96398c1c19fd32fbf7dcdd1aa0`.
Document Control on p.4 identifies the Foundation chapter as **14-0126-17**
and the separate approved errata as **19-2019**.

The errata's exact revision and primary text have not been obtained/reviewed.
No claim is made that it is empty, unavailable or incorporated into this PDF.
Behavior is checked against the pinned base text only. Unreviewed errata is
an explicit risk permitting continued base-text development, with possible
later corrections; review remains a gate before conformance claims, not
before all command/attribute work. Application/device definitions and profile
requirements also remain separate. Selecting R8 alongside Core R22/BDB 3.0.1 is this project's
engineering baseline, not a claim of a universal mandatory pairing.

| Primary location (printed / PDF pages) | Functional facts used |
| --- | --- |
| Sections 2.3.1-2, 2-3..2-4 / 55-56 | Zero reserved bits on TX; ignore reserved sub-fields for standard RX; manufacturer-defined handling for extensions |
| Sections 2.3.3,2.3.4.4, 2-4..2-6 / 56-58 | Manufacturer context must not execute unrecognized commands; read/write access categories, separate from application authentication |
| Section 2.4.1, Figures 2-2/3/4, 2-8..2-9 / 60-61 | Three/five-byte headers, FCF bits, manufacturer-code order, direction/default-response metadata and transaction/command fields |
| Table 2-3, 2-10..2-11 / 62-63; section 2.5.11.1, 2-26 / 78 | Codec test Report Attributes ID and identifier/type/value record shape; the later synthetic reporting model is reviewed separately below |
| Sections 2.5.1-2, Figures 2-5/6/7, 2-11..2-14 / 63-66 | One or more LE16 request IDs; ordered status records; type/value only on success; insufficient-space records, prefix termination and lack of fragmentation |
| Section 2.3.2, 2-4 / 56; sections 2.5.13-14, Figures 2-26/27/28, 2-29..2-31 / 81-83 | Ignore appended standard-command octets; LE16 inclusive discovery start and byte maximum, ascending ID/type records and completion flag; follow-up at last ID plus one |
| Section 2.5.6.3, 2-18 / 70; section 2.5.12, 2-28..2-29 / 80-81 | Write Attributes No Response forbids all replies including errors; never reply to Default Response; unsupported-command `81` errors and received command/status notification |
| Section 2.4.1, 2-8..2-9 / 60-61; section 2.5.12, 2-28..2-29 / 80-81 | Response transaction echo/direction/default-response flag; unicast Default Response command/status and error-response conditions |
| Section 2.6.3, Table 2-12, 2-55..2-56 / 107-108 | SUCCESS `00`, NOT_AUTHORIZED `7E`, MALFORMED_COMMAND `80`, UNSUPPORTED_ATTRIBUTE `86`, INSUFFICIENT_SPACE `89`; deprecated WRITE_ONLY `8F` must not be transmitted |
| Section 2.6.3, Table 2-12, 2-55..2-57 / 107-109 | Normalize received deprecated statuses: `82..84 -> 81`, `8A/C4 -> 00`, `8F -> 7E`, `90/91/93/C0/C1 -> 01`; transmit nondeprecated `UNSUP_COMMAND 81` rather than `82/83/84` |
| Section 2.5, 2-10 / 62 | Attribute-bearing clusters require more foundation commands, including writes; this partial dispatcher does not establish complete cluster conformance |
| Section 2.6.1.4, Table 2-8, 2-44 / 96 | Standard attribute declarations `0000..4FFF`, global declarations `F000..FFFE`; `5000..EFFF` and `FFFF` reserved. Manufacturer-specific attributes retain the full 16-bit range in manufacturer context |
| Section 2.6.1.5, 2-44 / 96 | Command ID ranges and manufacturer context, left to future dispatch policy |
| Sections 2.6.2.1-2, Tables 2-10/11, 2-45..2-48 / 97-100 | Type IDs, widths, non-value patterns and field-dependent full versus non-value ranges |
| Sections 2.6.2.3-9, 2-48..2-49 / 100-101 | No-data, raw/bitmap/integer widths, Boolean `00/01/FF` and signed non-value patterns |
| Sections 2.6.2.13-14, Figures 2-43/44, 2-50..2-51 / 102-103 | Short string byte counts, empty/non-value prefixes, default UTF-8 and descriptor-dependent encoding |

The [wire contract](ZCL.md) distinguishes raw layout from command/attribute
acceptance, a matching scalar non-value pattern from an actual unavailable
measurement, and character-string bytes from validated text.
Unsupported data types are explicit errors, not guessed zero-width values.
The read handler requires complete nonempty identifier pairs; an empty list
or incomplete last ID uses MALFORMED_COMMAND under the base status definition.
Table 2-8 constrains standard declarations, not received unknown identifiers:
negative Read records still echo absent IDs unchanged with `86`, and generic
wire fields retain all 16 bits. Reserved standard declarations return local
`INVALID_TABLE` before Read/Discover publication, including unused or denied
entries; manufacturer-specific tables are exempt from that numeric restriction.
Its 16-entry table cap, local error API, minimum one-record response budget,
caller-selected unicast context and private atomic scratch storage are
explicit implementation bounds, not new standard requirements. The subsequent
dispatcher is limited to one caller-selected unicast cluster side; it does not
imply a network dispatcher, authenticated reception or complete device.
Discovery's maximum byte has no stated nonzero restriction in the reviewed
base text: zero requests produce an empty page, with completion derived from
whether any eligible attribute remains. Positive nonempty discovery requires
room for a record; page sizes are bounded by maximum and response capacity.
Manufacturer fixed-format trailing extensions remain explicitly unsupported.
Original code and vectors were written from these functional facts, not the
external mixed ZCL/Matter catalog or a vendor stack. No implementation,
cluster table, key, capture, SDK object or test-plan vector was imported.
The PDF/extractor remain temporary research inputs, outside Git/CI artifacts;
their original notices and licenses are not replaced by BSD-3-Clause.

### Lab Basic model and cluster requirements

The [read-only Basic provider and lab requirements](ZCL_LAB.md) use the
same pinned R8 PDF: General chapter14-0127-21 and Measurement and Sensing
chapter14-0128-12. Sections3.2.1-3/Tables3-7/8/16 (printed3-6..3-17)
supply Basic IDs, mandatory/optional access, lengths, valid power-source
values and revision3; section2.3.4.5/Table2-1 supplies mandatory
ClusterRevision. Sections3.5 (3-30..3-34) and4.4/Table4-13 (4-10..4-12)
establish Identify and Temperature requirements; the Basic change implemented neither.
The pinned BDB3.0.1 sections6.5-7 remain binding/group/reporting requirements.
An applicable primary device/profile definition was not established;
no advertisement or ID was inferred from a newer document or SDK.

The secondary catalog's optionality/Identify payload/type discrepancies were
resolved against those primary tables, not imported. The provider, synthetic
test identities and complete SDCC/alias proof are original BSD-3-Clause work.
No manufacturer code, physical reading, external implementation or SDK was
adopted. Errata and full application conformance gates remain unchanged.

### Bounded Identify procedure

The [Identify contract](ZCL_IDENTIFY.md#primary-requirements-and-wire-decisions)
uses the same pinned R8 primary, sections3.5/Tables3-31..35
(printed3-30..3-34 / PDF140-144), plus2.3.2,2.4.1 and2.5.12 for
reception, response framing and idle-Query silence. IdentifyTime is mandatory
RW, not the secondary catalog's optional attribute; Matter IdentifyType is
not imported. Restart's subsecond phase and caller clock continuity are
explicit project policies, not hardware or normative timing measurements.
Original code reuses the repository's real foundation handlers and proof
helpers. No external implementation, identity, SDK or private capture was
imported. Logical command processing does not establish physical indication,
generalized mutable attributes, application advertisement or errata-aware conformance.

### Foundation write-family source review

The [bounded write contract](ZCL_WRITE.md) directly reviews the same pinned
ZCL R8 primary, Foundation14-0126-17 sections2.5.3–6/Figures2-10..14
(printed2-14..18 / PDF66–70),2.4.1 (2-8..9 / PDF60–61),
2.3.2 (2-4 / PDF56),2.5.12 and Table2-12 (2-28..29,2-55..56 /
PDF80–81,107–108). Existence/type/read-only checks precede value validation;
Undivided prohibits all requested mutations after any record error; No Response
forbids every reply. IdentifyTime's actual write effect comes from3.5.2.2.1/
Table3-31 (3-31 / PDF141); subsecond restart remains explicit project policy.
Unknown wire extents are not inferred from the mixed secondary catalog.
Original parser, synthetic vectors and proof adaptations import no external
implementation or private material. No new primary revision is substituted.

### Synthetic Temperature Measurement and reporting

The [temperature/reporting contract](ZCL_TEMPERATURE.md#primary-basis) uses
the same pinned R8 PDF, Measurement/Sensing14-0128-12 sections4.4 and4.1.3.1
(printed4-10..12,4-4 / PDF324..326,318), and Foundation14-0126-17
sections2.5.7-11 (2-18..28 / PDF70..80).
These establish ranges/unknown markers, mandatory MeasuredValue reporting,
record extents and error precedence, interval special values, signed-change
semantics and complete-record Read Reporting Configuration prefixes.
Section2.5.11.2.3 explicitly establishes the analog baseline at configuration,
then uses the previously reported value; the initial periodic phase remains
unspecified by2.5.11.2.1. Logical first-report phase, unknown transitions,
duplicate-record resolution and the finite preparation/completion lease are
documented project policies, not physical timing or delivery observations.
No optional Tolerance, endpoint/profile identity, bindings, persistence or
receive-side reporting is invented. Caller-supplied defaults do not establish
BDB-compliant default reporting. Original C, synthetic vectors and proof
import no external implementation, sensor reading, SDK or private artifact.
Errata19-2019, application selection and conformance gates remain open.

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
The primary PDF confirms that 13-0402-13 is BDB 1.0. Neither secondary label
identifies the selected BDB 3.0.1 document 16-02828-012.

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
