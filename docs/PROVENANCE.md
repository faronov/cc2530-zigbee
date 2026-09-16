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
identity, `GET_STATE` firmware version or firmware revision. We do not copy
the reference's automatic configuration changes, debug-config writes,
normal-execution reset or programmer algorithms. The implementation requires
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
