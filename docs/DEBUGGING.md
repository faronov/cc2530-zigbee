# Debugging strategy

## Hardware capability is not a finished debugger

The CC2530's documented two-wire debug interface uses P2.1/P2.2 plus reset.
It is not ARM SWD. Through a CC Debugger, the hardware supports:

- Halt/resume and execution of supplied debug instructions.
- PC and memory-bank inspection, instruction stepping.
- Four hardware code breakpoints, with bank bits.
- Access to memory and registers through the debug instruction mechanism.

See TI SWRU191F chapter 3, particularly section 3.3.3 for breakpoints.
These capabilities do not depend on compiling with IAR.

**The repository implements guarded live PC/register/memory access, CPU
control, reset-based attach and unbanked hardware breakpoints. M1 is complete
for the bounded LG/unbanked baseline, not a universal debugger acceptance.**
Host/image/simulator coverage and the
[2026-09-16 one-board LG hardware record](#2026-09-16-lg-fixture-hardware-record)
are distinct evidence levels. Non-reset attach remains unsupported.
`cc-tool` is primarily a programmer, and SDCC's `s51`/uCsim checks are
simulation, not a connection to a physical CC Debugger. A ready GDB/OpenOCD
source-level setup is not assumed or claimed.

M0 emits SDCC debug information. M1 must first prove address-level control,
then add symbol/variable lookup and, separately, source-line conveniences.

## Debugger lifecycle contract

Before implementing host commands, make these choices explicit:

1. Which operation resets the target or enters debug mode?
2. Which operations require it to be halted?
3. Which CPU registers/pointers does a memory access temporarily alter?
4. How are they restored before execution resumes?
5. What happens on a short USB transfer, timeout or disconnection?
6. Can an error accidentally resume partially written firmware?

A reset-on-connect tool cannot preserve the original running failure just by
reading registers afterward. Starting under debug control, running to the
failure and then halting is a different operation and must be identified.

Use bounded USB/debug transfers. Never report a successful write from an exit
code alone when the underlying operation can report failure separately.
Read back programmed data. On interrupted flashing, retain recovery material,
keep the target from executing an unverified image where possible, and restore
a completely verified image before normal operation.

TI SWRU191F Table 3-3 describes `STACK_OVERFLOW` as detection of a write to
DATA address `0xFF`. A CRT that clears that address can therefore set the bit
without an overflowing call chain. Inspect the actual startup and stack
behavior; the bit alone is not proof of a call-stack overflow.

## Implemented host transport

`tools/cc_debugger.py` is an original Python implementation of narrow CC
Debugger diagnostic, control and register-preserving access exchanges. Its optional PyUSB backend is
separate from the firmware toolchain. No operation happens on import,
construction or `--help`; USB access requires an explicit command and numeric
bus/address.

Only TI CC Debugger VID/PID `0451:16A2` is accepted. There is no fallback to the
first connected adapter, SmartRF boards or clones with different IDs. Opening
requires USB configuration 1 already active, only interface 0/alternate 0,
and bulk endpoints `0x04` OUT / `0x84` IN. Interface 0 is explicitly claimed;
an active kernel driver is refused rather than detached.

Opening and closing do **not** reset the adapter/target, change configuration
or alternate settings, clear endpoint stalls, send debug-entry sequences,
halt/resume the CPU or flash anything. Closing releases the USB interface and
handle only. Claiming an interface can conflict with another host tool; do
not run two debugger clients against the same adapter.

### Commands and permissions

| Command / Python method | Access policy | Result |
| --- | --- | --- |
| `adapter-state` / `read_adapter_state()` | `ADAPTER_ONLY` (default) | Adapter-reported target ID and firmware version/revision |
| `debug-status` / `read_debug_status()` | `EXISTING_DEBUG_SESSION` | One raw debug status byte |
| `debug-config` / `read_debug_config()` | `EXISTING_DEBUG_SESSION` | One raw debug configuration byte |
| `bank` / `read_bank()` | `EXISTING_DEBUG_SESSION` | Low three bits of GET_BM, with CPU halted before/after |
| `halt` / `halt()` | Existing session plus separate CPU-control permission | Before/after status and whether HALT was sent |
| `resume` / `resume()` | Existing session plus separate CPU-control permission | Before/after status and whether RESUME was sent |
| `step` / `step()` | Existing session plus separate CPU-control permission | Before/after status and the resulting accumulator |
| `reset-halt` / `reset_halt()` | Prepared existing session plus separate target-reset permission | Before/after status and whether the reset request was sent |
| `attach-reset` / `attach_reset()` | Fresh `RESET_DEBUG_SESSION` policy plus separate target-reset permission | Halted post-status and whether reset was sent; no prior-state claim |
| `pc` / `read_pc()` | Prepared existing session, awake and halted; no extra permission | 16-bit PC |
| `registers` / `read_registers()` | Existing session plus memory-access permission, awake and halted | Verified `RegisterSnapshot` |
| `read-sfr` / `read_sfr(address)` | Same memory-access permission and stopped state | One reviewed core SFR byte |
| `read-xdata` / `read_xdata(address, length)` | Same memory-access permission and stopped state | 1..256 SRAM bytes within `0x0000..0x1FFF` |
| `read-code` / `read_code(address, length)` | Same memory-access permission and stopped state | 1..256 lower unbanked CODE bytes within `0x0000..0x7FFF` |
| `write-xdata` / `write_xdata(address, data)` | Both memory-access and memory-write permissions, awake and halted | `None` after per-byte readback and register verification; only `0x0000..0x1DFF` |
| `breakpoint` / `set_breakpoint(slot, address, enabled=True)` | Separate breakpoint permission, awake and halted | `BreakpointResult`; slots 0..3, lower unbanked CODE, bank parameter fixed at 0 |

Ordinary target commands require `--confirm-existing-debug-session` on the CLI.
This is the operator's assertion that an appropriate CC2530 debug session already
exists, **not an attach operation**. Reset-based initial attach is a distinct
operation below; the flag itself cannot independently prove a session.
Before each target operation the tool refreshes adapter state
and refuses non-`0x2530` target IDs without sending a target command.
Adapter metadata may be stale: its target ID is not a silicon identity check,
proof of a halted CPU, or proof that a physical connection is safe.

After a successful operation **and cleanup**, the CLI prints a JSON object:
`target_id`, `firmware_version`, `firmware_revision` for adapter state;
`debug_status`, `debug_config` or `bank` for target reads.
HALT/RESUME/reset-halt return `status_before`, `status_after` and `command_sent`; STEP returns
`status_before`, `status_after` and `accumulator`. Status/data values are integers;
`command_sent` is a boolean. Initial attach returns only integer `status_after`
and boolean `reset_sent`, not an invented pre-reset status. The last two
adapter-state bytes are uninterpreted and are not exposed. A zero target ID
is reported by `adapter-state`, not invented as a successful target
connection. Target bytes are returned without interpreting them as success.
PC returns `{"pc": INTEGER}`. Register JSON contains exactly
`pc`, `bank`, `a`, `psw`, `b`, `sp`, `dptr0`, `dptr1`, `dps`, `mpage`, `r`;
`r` is an eight-element JSON array (a tuple in the Python dataclass).
`bank` is the low three FMAP bank bits, not the PSW-selected register bank.
SFR JSON is `{"address": INTEGER, "value": INTEGER}`. Memory-read JSON is
`{"address": INTEGER, "data": "lowercase hexadecimal bytes"}`. Write JSON is
`{"address": INTEGER, "bytes_written": INTEGER, "readback_verified": true}`.
Breakpoint JSON has `slot`, `address`, boolean `enabled` and `status_after`
(the command's STATUS reply, also checked before a separate fresh postcheck).
There is no automatic dump, serial-number read or payload logging beyond
the explicitly requested JSON. Keep captured target data out of Git/CI artifacts.

CPU-control methods require `allow_cpu_control=True` in Python, or the CLI's
additional `--allow-cpu-control` flag. Existing-session and CPU-control
permission do not grant reset, flash writing, memory access or breakpoint
programming. The operator must have verified the intended target image
separately: the flag is an authorization,
not proof that the bytes on a chip match a local build. **No hardware commands
may be run as part of offline development checks, even with an adapter absent.**

Before HALT/RESUME/STEP and bank reads, a fresh status must indicate an unlocked,
non-erasing chip, stable oscillator and normal power state. RESUME, STEP and
bank reads additionally require `CPU_HALTED`. These are deliberately awake
fixture operations, not support for inspecting arbitrary sleeping firmware.
`PM_ACTIVE=1` means outside normal operation/in a power-mode transition or
state, not that the CPU is normally running. `STACK_OVERFLOW` remains reported,
but does not alone reject control: the CRT DATA-`0xFF` caveat above applies.

HALT is not sent if the CPU is already halted (`command_sent=false`). Otherwise
its reply is consumed but not interpreted: a breakpoint can halt the CPU
between the precheck and the command, making that reply undefined. A new
READ_STATUS must then confirm the halted state.

RESUME checks its status reply and a fresh post-status. The CPU can already
have stopped at a hardware breakpoint again; this is returned as an observed
halted/breakpoint status, not retried or misreported as continuously running.
A still-halted CPU without the breakpoint cause is an error. STEP's reply is
**accumulator data**, including values such as `0xFF`, never a status byte.
STEP then requires a fresh halted post-status. `step()` does not infer a PC
delta from the opcode: use `read_pc()` for the actual address. The manual
fixture runner compares snapshots around the linked NOP; its observed `PC+1`
result is recorded separately from the alias-aware simulator check.

GET_BM's upper five reply bits are unspecified here; only its low three bits
are returned. This is FMAP bank information, not a flat flash address or proof
of banked-code support. Config reads now check status before/after and reject
locked or erasing targets. Raw READ_STATUS remains available for diagnosing
those states; RD_CONFIG is not a permitted command on a locked CC2530.

### Live register and memory access

`allow_memory_access=True` / `--allow-memory-access` authorizes the supplied
instructions needed for register, SFR, XDATA and CODE inspection.
`allow_memory_write=True` / `--allow-memory-write` additionally requires
memory-access permission. `allow_breakpoints=True` / `--allow-breakpoints`
is independent of both memory and CPU-control permissions. All constructor
permission values must be exact booleans; none upgrades `ADAPTER_ONLY`.
These operations require fresh awake, halted, unlocked and non-erasing status
before and after, and share one lock/deadline across all helper exchanges.

The passive core SFR whitelist is exactly:

| Address | Register |
| --- | --- |
| `0x81` | SP |
| `0x82`, `0x83` | DPL0, DPH0 |
| `0x84`, `0x85` | DPL1, DPH1 |
| `0x92`, `0x93` | DPS, MPAGE |
| `0x9F` | FMAP |
| `0xD0`, `0xE0`, `0xF0` | PSW, ACC, B |

This is not permission to sweep peripheral SFRs or MMIO. XDATA reads include
the status reservation and the IRAM alias, but the public writer rejects
**all addresses at or above `0x1E00`**, including unused status space,
`0x1F00..0x1FFF`, MMIO and flash. Reads/writes require exact integer
addresses/lengths, 1..256 bytes and no crossing of the relevant bound.
Python write data must be immutable `bytes`; successful writes read back each
byte, not merely accept the USB OUT count. There is no CODE/flash writer.

Snapshots save A with a supplied NOP, read PSW next, and restore A/PSW,
including the accumulator-dependent parity bit. They capture both DPTRs,
DPS, B, SP, MPAGE, active-bank R0..R7 and PC/FMAP bank, and verify a second
snapshot. Memory helpers save that context, select DPS 0 and use DPTR0.
On normal completion they restore DPL0/DPH0, DPS, A and PSW, then compare the
full context; DPTR1 and the other untouched registers must also match.
**Failed, short or late exchanges stop immediately, with no attempted
restoration or retry.** Register/RAM changes may already have happened:
`FAULTED` is not a claim that the pre-operation context survived.

Exact command-specific CLI operands:

| Command | Required additional permission(s) | Operands |
| --- | --- | --- |
| `pc` | None | None |
| `registers` | `--allow-memory-access` | None |
| `read-sfr` | `--allow-memory-access` | `--memory-address ADDRESS` |
| `read-xdata`, `read-code` | `--allow-memory-access` | `--memory-address ADDRESS --length COUNT` |
| `write-xdata` | `--allow-memory-access --allow-memory-write` | `--memory-address ADDRESS --data-hex 'HEX BYTES'` |
| `breakpoint` | `--allow-breakpoints` | `--slot SLOT --code-address ADDRESS`, optionally `--disable` |

All also need `--bus BUS --address ADDRESS --confirm-existing-debug-session`.
The USB `--address` is not the target memory/CODE address. Target addresses
accept decimal or `0x` notation; lengths and slots are decimal integers.
`--data-hex` accepts hexadecimal pairs and whitespace, decoded to immutable
bytes. Missing, invalid and unrelated operands are rejected before loading
PyUSB. There is no live `--bank` operand; banked breakpoint use is unsupported.

### Reset-based initial attach

The Python `Access.RESET_DEBUG_SESSION` policy requires
`allow_target_reset=True`. Opening only claims the selected adapter; target
reads, memory writes, breakpoint programming, HALT/RESUME/STEP and `reset_halt()` remain denied until an explicit
`attach_reset()` completes. Repeating initial preparation on a ready session
is rejected; use an explicit existing-session reset if that is intended.

The CLI's `attach-reset` requires both `--confirm-reset-attach` and
`--allow-target-reset`. The initial-attach confirmation and
`--confirm-existing-debug-session` are mutually exclusive. This policy cannot
be supplied to turn an ordinary read into an automatic attach.

**This destroys the original execution context.** Confirm the board,
intended verified image, electrical setup and recovery material separately
before any manual invocation. The adapter must already report family `2530`;
zero/unknown targets are refused, not blindly probed or configured.
The tool cannot safely inspect the prior target status before preparing a
new session: in particular, it cannot promise that an unknown ongoing flash
operation will survive attach. Exclusive USB claiming is not proof of a
safe target/image or absence of autonomous peripheral activity.

One deadline covers adapter-family checking, cached descriptor revision,
vendor OUT `C5` preparation, `C8` chip-information data, `C9` reset into debug,
fresh adapter-family checking and halted/awake/unlocked/non-erasing status.
The exact setup fields are recorded in [PROVENANCE.md](PROVENANCE.md).
`DID:` in the adapter metadata is based on `bcdDevice`, not a unique device
identity; no USB string descriptor, serial number or factory record is read.

Only successful completion enables target operations on this Python session;
normal CPU-control permission is still separate. Failure or interruption at
any stage faults the session and may already have disturbed the target.
There is no retry, fallback reset, debug-config write or automatic resume.
Closing after success releases USB resources without issuing a normal-mode
reset. Initial PC/reset and subsequent fixture initialization were observed
on the single adapter/board in the dated record; this is not a compatibility
claim for other firmware, boards, sleeping targets or arbitrary attach timing.

### Explicit reset into halt

`reset_halt()` additionally requires `allow_target_reset=True`; the CLI uses
`reset-halt` with both `--confirm-existing-debug-session` and
`--allow-target-reset`. CPU-control permission cannot substitute for reset
permission, and reset permission does not grant HALT/RESUME/STEP.

This operation **destroys the previous execution context**. Use only with a
separately confirmed board, verified intended image and appropriate recovery
material. It requires an already prepared CC2530 debug session, not merely a
connected adapter. It is not an implementation of first attach or preservation
of the original running failure.

Within one deadline it checks adapter family and unlocked, non-erasing, awake
status, sends vendor OUT `40/C9`, value `0`, index `1`, empty data, then checks
adapter family and fresh halted/awake status again. It requires an integer
zero USB completion count, not a truthy/falsy success guess. A CPU already
halted is still reset when this explicit command is requested.

It does not prepare a new adapter session, change debug configuration, poll
until the oscillator stabilizes, retry reset, issue a fallback HALT or send
index `0` (reset into normal execution). A failed or late postcheck faults the
session even if reset has already occurred. Closing only releases USB
resources; it never undoes a halt or starts the firmware.

`command_sent=true` plus halted status is not independent proof of reset PC,
memory initialization or matching flash contents. The live PC/CODE APIs and
manual fixture runner make those separate comparisons; the dated hardware
record identifies the image for which they passed.

### Lifecycle and failures

The single-owner, synchronous Python API moves `NEW -> OPEN -> CLOSED`.
Operations require `OPEN`; I/O failure, malformed/short reply, incomplete
write, target mismatch, deadline expiry or interruption leaves it `FAULTED`.
Only explicit cleanup is then permitted. A policy denial or invalid argument
before I/O is an error but does not poison an otherwise open session.
There is no implicit retry, reconnect, reset, resume or endpoint recovery.
A new session object requires a new explicit open; it is not proof that a
previous interrupted exchange or its pending reply has been recovered.

A nonblocking per-session guard covers opening, closing and each complete
operation, including access-policy checks and reset/attach preconditions.
Concurrent or reentrant calls fail as busy before changing state or accessing
the backend; they are not queued. In particular, a callback or another thread
cannot close the backend during a read and let that read restore `OPEN`.
Failure/interruption releases the guard so explicit cleanup remains possible.
The backend must still have a single owner; sharing it between separate
`Debugger` objects or calling it directly bypasses this session-level guard.
Closing remains terminal even when cleanup reports an error; a second close
does not retry resource release.

Each diagnostic operation has one monotonic deadline across its control,
bulk-write and bulk-read phases. USB timeouts are positive milliseconds,
rounded up by at most one millisecond; a response at/after the deadline is
still rejected. The allowed operation timeout is 1..60000 ms. Discovery,
USB descriptor/configuration queries during open, interface claim/release and OS cleanup use synchronous
PyUSB/libusb APIs and are **not claimed to have that aggregate deadline**.
Hard cancellation of a stuck OS/backend call remains outside this slice.

CPU prechecks, the control command and postchecks share that same deadline.
There is no oscillator-ready polling loop, implicit halt before a read, or
second STEP/RESUME after a late or ambiguous reply. A failed postcheck leaves
the session faulted even if the command may already have executed.

Partial writes are not continued and short reads are not padded. Cleanup
explicitly releases the interface before PyUSB disposal, because disposal
alone can suppress release errors. Both the original operation error and a
cleanup failure remain visible; no success JSON is printed on either failure.

### Optional installation and hardware boundary

For a separately authorized manual Linux USB session, install the optional
backend in a virtual environment. Firmware builds and ordinary fake-backend
tests do not need it:

```sh
sudo apt-get install --no-install-recommends python3-venv libusb-1.0-0
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements-debug.txt
.venv/bin/python tools/cc_debugger.py --help
```

After identifying the intended adapter, replace `BUS`/`ADDRESS` with its
current numeric USB location; they may change on reconnection:

```text
.venv/bin/python tools/cc_debugger.py adapter-state --bus BUS --address ADDRESS
.venv/bin/python tools/cc_debugger.py debug-status --bus BUS --address ADDRESS --confirm-existing-debug-session
.venv/bin/python tools/cc_debugger.py pc --bus BUS --address ADDRESS --confirm-existing-debug-session
.venv/bin/python tools/cc_debugger.py registers --bus BUS --address ADDRESS --confirm-existing-debug-session --allow-memory-access
.venv/bin/python tools/cc_debugger.py read-sfr --bus BUS --address ADDRESS --confirm-existing-debug-session --allow-memory-access --memory-address 0x81
.venv/bin/python tools/cc_debugger.py read-xdata --bus BUS --address ADDRESS --confirm-existing-debug-session --allow-memory-access --memory-address 0x1E00 --length 32
.venv/bin/python tools/cc_debugger.py read-code --bus BUS --address ADDRESS --confirm-existing-debug-session --allow-memory-access --memory-address 0 --length 16
```

These are manual examples, **not commands invoked by builds or CI**. No udev
rules, permission changes, automatic driver detachment or USB discovery are
installed by this repository. Do not use `sudo` as a substitute for a reviewed
device-access policy or run a debugger operation merely to clear a warning.

Automated transport evidence is **host-tested**: synthetic conversations check exact packets,
selection, permission gates, reply lengths, deadlines, failure latching and
cleanup. Optional tests exercise the actual pinned PyUSB resource manager
with a synthetic driver, never a real libusb device. They are explicitly
skipped when PyUSB is absent; CI also runs them with PyUSB installed.
The macOS erase-observer test uses a compiled synthetic USB library/driver,
not real libusb or `cc-tool`, and skips on other platforms or without a host
compiler. The public wire facts and licenses are pinned in
[PROVENANCE.md](PROVENANCE.md). Physical results, including real timeout/stall
and cable-unplug failures, are limited to the
[dated LG record](#2026-09-16-lg-fixture-hardware-record).

## Offline image, symbol and snapshot tools

`tools/debug_image.py` never imports the USB transport or enumerates devices.
Its artifact commands first run the existing strict image/layout verifier,
then require matching board/image/toolchain metadata and all six artifact
hashes from `build-info.json`. This detects stale or mixed build files; it is
**not signature authentication or verification of a physical target image**.
Do not inspect a build directory concurrently with relinking it.

The reader supports linked SDCC 4.2.0 global functions, CODE labels/data,
XDATA objects, byte SFR symbols and explicitly typed SBIT symbols used by these images. It combines map
addresses with CDB space/type/size and global address records, rejecting
conflicting records. SBIT addresses are bit addresses, not byte-SFR addresses;
in the IRQ image, EA bit AF and T1STAT byte SFR AF are different objects.
This does not expand the hardware SFR-read whitelist.
Function CDB type sizes are not misrepresented as function byte lengths.
Unknown symbols, unsupported spaces, locals, optimized-out variables, source
ranges and banked images are not guessed. Some linker/runtime symbols have no
supported type information and are not advertised.

Examples that use only generated files and synthetic values:

```sh
python3 tools/debug_image.py symbols --output build/generic/debug_fixture --board generic --image debug_fixture --name _debug_fixture_state
python3 tools/debug_image.py breakpoint --output build/generic/debug_fixture --board generic --image debug_fixture --name _debug_fixture_stage0 --slot 0
python3 tools/debug_image.py fixture-state --output build/generic/debug_fixture --board generic --image debug_fixture --hex '4d314442011003015ab16bce9dd46996'
python3 tools/debug_image.py source-lines --output build/generic/debug_fixture --board generic --image debug_fixture --file debug_pattern.c --line 45
python3 tools/debug_image.py decode-status 0x23
python3 tools/debug_image.py decode-config 0x26
```

Omit `--name` from `symbols` to list supported linked symbols. The `breakpoint`
command accepts slots 0..3 and only executable CODE functions/labels. It emits
the **three target parameter bytes**, with `programmed=false`: it does not
produce or send a USB packet. These images are unbanked, so the command uses
bank 0. The lower-level target codec separately tests all 4 slots, 8 bank-bit
values, enable/disable and 16-bit CODE address boundaries; this is not evidence
of silicon bank discrimination.

`source-lines` lists exact linked CDB source records. Filter by `--pc ADDRESS`
or by both `--file NAME --line NUMBER`; with no filter it lists all records.
Each result preserves filename, line, scope, block and CODE address. Multiple
source lines can share one address; one line can have distinct scoped
locations. All matching records are returned, not an arbitrarily chosen line.
Duplicate identical records are collapsed; conflicting records and addresses
outside the linked unbanked image are errors.

There is no nearest-line fallback, range attribution, function-end inference,
source breakpoint programming or source-level stepping. For example, the
fixture's inline assembly has no per-instruction C records, and naked-function
end records may share the following function's address. Missing exact
locations are explicit errors. Filenames are compared literally, never
resolved or opened, and the current working-tree source may differ from the
source used to build the artifacts. The example line number above refers to
the current fixture source, not a stable ABI.

`status` decodes exactly 32 bytes of M0 status; `fixture-state` decodes exactly
16 bytes of M1 state. `timebase-state` decodes exactly 32 bytes of the separate
`timebase_fixture` ABI below. Each accepts `--hex` or `--snapshot PATH`, never both.
Files are read with a size bound; complete RAM dumps are not accepted. M0
requires the correct signature/version/size, ready phase, selected board and
policy, disabled interrupts and zero reserved bytes. Its port fields are
reported samples, not electrical safety measurements.

M1 initialized state must have zero cycle data. Ready state must match the
entire expected seed/checkpoint/result/guard record, including counter wrap.
In-progress/unknown phases and inconsistent records are explicit errors, not
verified cycles. Readers still need an independently coherent, halted snapshot;
a valid record cannot prove how it was captured. Raw debug-config decoding
treats CC2530 bit 0 as reserved, not as another family's flash-info selection.

Artifact-command JSON includes the image BIN hash and
`evidence="offline-image-checked"`. This describes local files, not USB access
or a hardware observation. No error path prints a success JSON object.

## Manual hardware acceptance and recovery

These tools are **manual only**. Confirm the exact board, safe non-RF image,
electrical setup, independent recovery backups and exclusive adapter ownership.
A previous authorization or successful record is not permission to repeat a
destructive test on another device/image. Never run these commands in CI.

### Fixture acceptance runner

After separately approved programming and independent readback, the explicit
manual invocation is:

```text
.venv/bin/python tools/check_debug_hardware.py --bus BUS --address ADDRESS --board lg_esl29_rev03 --output build/lg_esl29_rev03/debug_fixture --cycles 257 --confirm-fixture-test
```

Use the current numeric USB location, not an address saved before reconnect.
`--output` names the matching public build directory, not a dump/output-log
destination. `--cycles` accepts 1..257 and defaults to 257.
`--confirm-fixture-test` is a combined explicit authorization for reset,
CPU control, temporary fixture SRAM/alias writes and all four breakpoint
slots; the runner internally enables the separate Python permissions. It
does not accept the transport CLI's individual `--allow-*` flags.

The runner validates local board/image/compiler/hash metadata, reset-attaches,
requires PC `0x0000` and debug configuration `0x26`, and compares every byte
of physical fixture CODE with the verified BIN **before its first resume**.
It does not flash, verify the entire 256-KiB flash tail or read the factory
page; those are separate programming/readback activities.

It exercises four simultaneous stage breakpoints, known-register snapshots,
complete M1 state/M0 heartbeat, NOP `PC+1`, resumed real calls, explicit HALT,
reset and reinitialization. It temporarily writes/restores 16 SRAM bytes at
`0x1D00`. Its narrowly scoped alias check accesses DATA `0x40` and XDATA
`0x1F40` bidirectionally and restores the original byte, only for the verified
interrupt-free fixture with that scratch byte above the bounded call chain.
This use of private helpers is **not** an expansion of `write_xdata()`:
the public writer still rejects every address at or above `0x1E00`.

Success JSON is emitted only after cleanup and identifies the image hash,
adapter, breakpoint hits, cycle count and final halted probe PC. The runner's
`not_tested_by_this_run` list describes its own scope: it does not perform physical cable
disconnection, interrupted-flash recovery or banked CODE tests. The separate
recovery and cable observations below are therefore not claims that this
runner flashes or automatically disconnects USB. Failures stop without an
automatic resume, retry or restoration.

### Erased-image-boundary interruption

`tools/erase_boundary_fault.c` is an original BSD-3-Clause macOS DYLD observer
for an **external** programmer. It does not implement or initiate erase/write.
Only environment value `CC2530_ERASE_BOUNDARY_FAULT=1` enables it; unset, zero
or any other value passes transfers through and clears its remembered state.
It forwards the original transfer parameters/results and watches for:

1. Exact successful bulk OUT `04`, two bytes `1C 14`, on one handle.
2. A subsequent fresh successful OUT `04`, two bytes `1F 34`, on that handle.
3. A successful IN `84` with requested and completed length exactly one:
   `CHIP_ERASE_BUSY=0`, `DEBUG_LOCKED=0`, `CPU_HALTED=1`.

Here "ready" means erase-busy clear; the observer does not independently
verify CODE, oscillator or power-mode state. The observed status was `0x22`.
Failed, null-count or partial transfers invalidate the pending status reply.
At the accepted boundary it writes a diagnostic and calls `_exit(99)`,
bypassing programmer destructors/normal-reset cleanup and the next programming
transfer. It is not a general USB grammar, power-cut injector or flash writer.

First exercise the fully offline synthetic-library test on the intended host:

```sh
PYTHONPATH=tools python3 -B -m unittest test_erase_boundary_fault -q
```

For a separately approved manual experiment, compile the observer into an
operator-selected recovery directory **outside the repository**:

```text
cc -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic -dynamiclib -Wl,-undefined,dynamic_lookup tools/erase_boundary_fault.c -o /ABSOLUTE/PRIVATE/PATH/erase_boundary_fault.dylib
CC2530_ERASE_BOUNDARY_FAULT=1 DYLD_INSERT_LIBRARIES=/ABSOLUTE/PRIVATE/PATH/erase_boundary_fault.dylib cc-tool --device BUS:ADDRESS --name CC2530 -e -w build/lg_esl29_rev03/debug_fixture/debug_fixture.hex -v r
```

These are not commands to paste without an approved fixture/recovery plan.
The synthetic test proves host interposition, not that a particular external
binary will take the expected path. Require exit 99 and the boundary diagnostic,
then independently confirm halted status and erased CODE. Missing/mismatched
evidence is an unsuccessful or ambiguous experiment, not a recovery pass.
Buffered programmer stdout is not an interruption trigger: an earlier attempt
stopped only during verification and was rejected as insufficient evidence.

**External `cc-tool` cleanup is unsafe to infer from `--reset` or exit status.**
At the reviewed revision, any normal task return calls target close and sends
normal-execution reset (`C9`, index 0), even without `--reset`. A verification
mismatch can merely print failure, still return exit 0 and take that reset
path. An exception before target close skips it; USB destruction only closes
the handle. See the [pinned lifecycle sources](PROVENANCE.md#m1-live-access-and-manual-recovery-sources).
Neither omitting `--reset` nor accepting `-v r`/exit 0 alone proves safe
completion or correct flash contents.

After a confirmed interruption, keep the erased/unverified target halted.
Recovery is a **new explicit complete reprogramming action** using the known
non-RF fixture and a reviewed external-programmer workflow, not a resumed
partial write. Remove the fault-injection environment for that action.
Check programmer readback verification, independently compare full flash with
the intended image plus erased tail, and confirm that the read-only factory
page is unchanged. Account explicitly for the external programmer's possible
normal-execution reset; the ordinary debugger does not issue it. Re-establish
halted control, independently verify physical CODE, then run fixture acceptance.
Do not resume unknown application code to see whether recovery "worked".

### USB failure and cable-disconnect procedure

On an error, preserve the diagnostic, deny further target commands on the
faulted session and close its resources without retrying or restoring registers.
Endpoint recovery, adapter reset and reattachment are separate operator
actions; `Debugger.open()`/`close()` never clears stalls. A new handle alone
does not prove that an ambiguous reply stream or target state is recovered.
After explicit endpoint recovery, check halted PC/state before any resume.

For a separately authorized cable-disconnect check, use the verified fixture
and retain a live handle during a bounded, operator-confirmed physical cable
removal. Require an actual device/transfer failure, `FAULTED`, denied resume and no implicit reset
or retry. Close, physically reconnect, identify the new USB location and use
an explicitly approved reattachment/recovery policy. Recheck status, PC and
physical image before controlled execution, then record the result. A
confirmation timeout, absent adapter at a later instant, or software USB device
reset does not establish this full sequence.

## M1 acceptance boundary before M2

**M1 is complete for the bounded LG/unbanked baseline.** It covers four slots,
stepping, state-preserving registers/memory, real timeout/short-reply/failed-write/
disconnect paths, safe erased-image interruption and verified recovery,
alias/reset behavior and offline symbols. It does not establish generic-board
hardware, a universal debugger or any M2/RF/network service.

The full fresh-reconnect fixture check passed after the first confirmed cable
reconnection; the final held-handle unplug/failure check passed later.
After the last replug, PyUSB enumeration and the full one-cycle fixture check
in an explicitly selected new session also passed, leaving the fixture halted
at `0x0173` at the end of that run. These finite measured scenarios, detailed
in the [dated record](#2026-09-16-lg-fixture-hardware-record), establish the
gates, including successful recovery after the final real unplug.

Bank discrimination is deferred until a banked CODE fixture is introduced,
not a current unbanked M1 blocker. Mid-word electrical power cuts, flash wear,
sleeping targets, non-reset attach and interactive source-level stepping are
not established by this record. The recovery result is specifically the
halted erased-image boundary, not arbitrary interrupted flash programming.

## M1 fixture and acceptance

Build a small known program with:

- Explicit safe board outputs and no radio transmission.
- Named functions, known register/memory values and a bounded call chain.
- A deliberate stop location and a versioned status block.
- Bank-specific stop locations when banking is introduced.

Exercise halt/resume, single-step, PC/bank, each of the four breakpoints,
read/write bounds and state restoration. Test disconnect/error paths and
verify that the fixture still runs correctly after inspection.

Source debugging must describe its limitations: optimized-out variables,
pointer spaces, banked addresses and SDCC debug-format support are not
automatically solved by a breakpoint command.

## Implemented target fixture

Build with `make BOARD=generic IMAGE=debug_fixture all test` or substitute
`BOARD=lg_esl29_rev03` for the confirmed LG board configuration. The default
output directory is `build/<board>/debug_fixture/`, containing
`debug_fixture.{ihx,hex,bin,map,mem,cdb}` and `build-info.json`.
The default `IMAGE=bringup` build remains unchanged.

This image reuses `_sdcc_external_startup`, board GPIO policy and the 32-byte
`M0CC` status at XDATA `0x1E00`. It adds no radio, timer, interrupt, sleep,
display, flash writer or USB operation. It is intended to start from reset
after separately approved and verified programming, not by jumping into its
main function from unknown running firmware. It is not a peripheral shutdown
routine. Builds and tests never access a physical board.

### State ABI v1

Look up `_debug_fixture_state` in the **matching linked map**. This 16-byte
object is allocated in ordinary XDATA below `0x1E00`, not in the unused part
of the M0 status reservation or the IRAM alias. All fields are bytes.

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | Signature `4D 31 44 42` (`M1DB`) |
| 4 | 1 | ABI version `1` |
| 5 | 1 | Active size `16` |
| 6 | 1 | Phase: `1` initialized, `2` cycle in progress, `3` cycle complete |
| 7 | 1 | Completed iterations, modulo 256 |
| 8 | 1 | Current cycle seed |
| 9 | 1 | Result after all nested calls return |
| 10 | 4 | Stage 0..3 checkpoints |
| 14 | 2 | Guards `69 96`, unchanged by normal execution |

Initialization clears iteration, seed, result and checkpoints. A cycle uses
the pre-increment iteration `i` to compute the following byte arithmetic:

```text
seed = i XOR 0x5A
c0 = (seed + 0x11) modulo 256
c1 = c0 XOR 0xA5
c2 = rotate-left-one-bit(c1)
c3 = (c2 + 0x37) modulo 256
result = (((c3 XOR 0x3C) + 3) modulo 256) XOR seed
```

The iteration and M0 heartbeat each advance once per completed cycle. At the
first register-probe stop the exact state is:

```text
4D 31 44 42 01 10 03 01 5A B1 6B CE 9D D4 69 96
```

Halt at a known boundary and exclude concurrent reset when reading either
status block. Phase 3 alone does not make a running read coherent, detect
corruption, or prove that a debugger preserved CPU state. Compare the entire
expected record; the fixture does not repair debugger-induced corruption.

### Code locations and register probe

`_debug_fixture_stage0` through `_debug_fixture_stage3` are four distinct,
nested C function entry addresses for hardware-breakpoint tests. They
are not hardware breakpoint-slot numbers. Their addresses can change on each
link; read the map rather than copying an address from another build.

`_debug_fixture_probe` loads known scratch-register values under SDCC's
foreground calling convention. Its exported assembly label
`_debug_fixture_stop` is exactly a one-byte `NOP`, followed by `RET`:

| At `_debug_fixture_stop` | Expected |
| --- | --- |
| A | `0xA5` |
| B | `0x3C` |
| DPTR (DPH:DPL) | `0x1234` |
| Register bank / R7 | Bank 0 / `0x69` |
| PSW carry | 1; other arithmetic flags are not a fixed-value contract |
| SP | `s_SSEG + 1`, after the nested pattern calls have returned |

The probe changes these call-clobbered registers deliberately. There is no
host substitute pretending to execute it. The image checker verifies its
exact instruction bytes and the stop-label offset.

For a register-preservation check, stop at the NOP, save the CPU
registers, perform the debugger's documented non-destructive XDATA read, then
compare registers before resuming. A single step must move PC to `stop + 1`
without changing registers or RAM. Resuming at that RET must return to the
real caller and produce the next expected cycle. Do not indiscriminately read
all hardware SFRs: some peripheral reads can have side effects.

Current automated evidence is **host-tested, image-checked and simulated**:
all seed values and wrap/reinitialization are host-tested, and the linked
image traverses all four function entries, checks actual nested-call SP
values, steps the NOP and completes 257 cycles with the IRAM alias enabled.
Memory/register snapshots, unchanged status, allocation boundaries and the
upper stack guard are checked. The simulator runs command files after loading
the image so console input echo cannot interleave with parsed memory dumps.

| SDCC 4.2.0 debug fixture | CODE bytes | Nonaliased XDATA used / reserved | IRAM stack reserved |
| --- | --- | --- | --- |
| generic | 586 | 52 / 84 bytes | 248 bytes |
| lg_esl29_rev03 | 626 | 52 / 84 bytes | 248 bytes |

XDATA includes 20 ordinary bytes (state plus compiler parameter storage) and
32 active M0 status bytes / 64 reserved status bytes. Stack reservation is not
a measured hardware high-water mark.

These automated checks are not physical breakpoint tests. The image is
lower-32-KiB unbanked CODE; uCsim address stops are not CC2530 comparator slots.
The separate LG observation below does not extend to the generic board,
standalone `bringup`, banked CODE or interactive source-level stepping.

## Init-time clock board fixture

`IMAGE=clock_fixture` is a separate original non-RF board image, not
`clock_test.ihx`. It links the C clock/timebase drivers and reuses the original
startup/board policy and M0 status ABI. The first LG image passed normal
switching but **failed rollback acceptance**, exposing the race recorded below.
Both revised images are **host-tested, image-checked and alias-aware simulated**.
The corrected LG image additionally passed
[bounded compiled-C hardware acceptance on 2026-09-17 (UTC+03)](#2026-09-17-lg-compiled-c-clock-acceptance),
including both timeout/rollback cases and separate reset/recovery.
Generic remains hardware-unobserved; full M2 #4 stays open.
The six older board BINs and all historical LG evidence remain unchanged.
That clock run ended on the corrected 3,798-byte image described below,
halted at READY `0x016A` on RC16; later fixture records are separate.

```sh
make BOARD=generic IMAGE=clock_fixture all test
make BOARD=lg_esl29_rev03 IMAGE=clock_fixture all test
```

Outputs are under `build/<board>/clock_fixture/`. Initialization requires
stable undivided RC16, MODE=0 and disabled IEN0/1/2. The foreground sequence
is stage 0 RC16 idempotence, stage 1 XOSC32, stage 2 RC16, repeating only after
successful calls. Every `clock_select_init` uses **1,024 raw timeout ticks**
and a **4,096-poll cap per attempt**, not milliseconds. No other fixture
function calls `timebase_deadline_after`. This deliberately isolated exercise
has no application/peripheral clients that depend on an unchanged HF clock.

Each result is serialized explicitly, including all driver diagnostic fields.
READY is published last, after the successful-step counter and M0 heartbeat
advance modulo 256. An original driver error remains an error even after a
successful rollback; it is recorded before terminal FAULT. No fault advances
the counter, retries, or automatically resets/reinitializes. Only a separate
explicit reset/initialization clears it. The C orchestration is host-testable;
NOP/RET and the terminal fault loop exist only in the target example.

The fixture checks unchanged SLEEPCMD/IRQ values and LF/TICKSPD command
fields, and records current CMD/STA separately after every call. M0 offsets
24/25 remain **startup snapshots**, not live clock telemetry. There are no
SLEEPCMD, ST0-2, LF-source, GPIO or IRQ writes beyond the original startup
policy; the driver changes only CMD.OSC/CLKSPD. Automatic RC calibration and
an extra Sleep Timer tick can accompany HF selection (SWRU191F pp.64, 66-69).
Neither source confirmation nor this fixture establishes calibrated frequency,
LF calibration completion, wake/IRQ/compare, RF, AES or flash services.

### Clock fixture byte ABI v1

The 56-byte `clock_fixture_state` is ordinary linker-accounted XDATA, currently
at `0x0000` on both boards, never unused M0 reservation or the IRAM alias.
All multibyte values are little-endian arrays; host C struct padding is not
serialized. Read a complete record at the matching checkpoints.

| Offset | Bytes | Meaning |
| --- | ---: | --- |
| 0 | 4 | Signature `M2CK` |
| 4 / 5 | 1 / 1 | Version 1 / size 56 |
| 6 / 7 | 1 / 1 | Phase: 1 INITIALIZED, 2 RUNNING, 3 READY, 4 FAULT / reason |
| 8 / 9 | 1 / 1 | Stage 0/1/2 / requested source 0 RC16 or 1 XOSC32 |
| 10 / 11 | 1 / 1 | Successful steps modulo 256 (including idempotence) / original clock result |
| 12 / 15 | 3 / 2 | Fixed raw timeout 1,024 / poll cap 4,096 |
| 17 | 7 | Request: elapsed uint32, polls uint16, timebase status byte |
| 24 | 7 | Rollback: elapsed uint32, polls uint16, timebase status byte |
| 31..35 | 5 | Saved CMD, requested CMD, last driver-observed CMD, STA, rollback result |
| 36 / 37 | 1 / 3 | Initial SLEEPCMD / IEN0, IEN1, IEN2 |
| 40 / 41 | 1 / 1 | Current CMD / STA after the call |
| 42 / 43 | 1 / 3 | Current SLEEPCMD / IEN0, IEN1, IEN2 |
| 46 / 54 | 8 / 2 | Reserved zeros / guards `69 96` |

Reasons are 0 none, 1 driver error, 2 invariant violation, 3 invalid phase/stage.
Clock results retain the [driver values](ARCHITECTURE.md#init-time-system-clock-selector-isolated-m2-slice);
8 means NOT_ATTEMPTED; appended rollback-only 9 means bounded cancellation
UNCONFIRMED. Existing values, 19-byte driver diagnostics and this 56-byte ABI
are unchanged. Initial and cleared RUNNING diagnostics are eighteen
zero bytes followed by rollback NOT_ATTEMPTED. On a pre-call phase fault,
the preceding record is retained. Snapshot decoding does not authenticate
arbitrary RAM corruption or invent a reset epoch.

The offline `clock-state` decoder rejects incomplete records, RUNNING, unknown
values, inconsistent source/status/bounds and altered guards. The manual runner
alone permits the cleared RUNNING shape at strictly checked internal
checkpoints; it is not accepted as a completed call.

```text
python3 tools/debug_image.py clock-checkpoint --board lg_esl29_rev03 --image clock_fixture --output build/lg_esl29_rev03/clock_fixture
python3 tools/debug_image.py clock-state --board lg_esl29_rev03 --image clock_fixture --output build/lg_esl29_rev03/clock_fixture --snapshot SNAPSHOT
```

### Clock checkpoints, timeout proof and footprint

| Symbol / checked boundary | generic | LG | Exact meaning |
| --- | --- | --- | --- |
| `_clock_fixture_before_call` | `0x0140` | `0x0168` | NOP; RET, initial or preceding READY record |
| `_clock_fixture_ready_stop` | `0x0142` | `0x016A` | NOP; RET, completed successful call |
| `_clock_fixture_fault_stop` | `0x0144` | `0x016C` | NOP; SJMP back, terminal fault |
| Checked deadline-helper RET | `0x06AC` | `0x06D4` | Shared RET; require live DPL=0, DPS=0 |
| Clock caller return address | `0x0838` | `0x0860` | Actual stack return address at that RET |
| Following CMD write | `0x0852` | `0x087A` | MOV CLKCONCMD,R0 |
| Post-request checkpoint | `0x0854` | `0x087C` | Immediately after that actual write |
| Poll observation call | `0x08B6` | `0x08DE` | Calls C observation of CMD/STA |
| Poll Sleep Timer call | `0x0932` | `0x095A` | C source evidence stored, before timestamp |

These addresses belong only to the matching checked artifacts. Some numerical
NOP addresses coincide with the older timebase fixture; that is **not** image
identity. The verifier resolves the explicit deadline-function CDB start/end,
checks its entire **148-byte relocated instruction body**, and checks the
`MOV DPL,#0; RET` success tail. Its argument-error branch also reaches the RET;
the runtime DPL check is mandatory. Only verified RAM/overlay/runtime-call
operands relocate, with declared allocation and actual linked bytes checked.
There is exactly one direct deadline-helper call in this board image, within
the clock wait function. Its straight-line return continuation must save DPL,
restore the three saved registers, store helper status, reload the owned CMD
and reach the exact CMD write. No next-symbol-minus-one or nearest-line guess,
code patch or injected ROM/RAM is used.

The separate late-source boundary verifies the **127-byte** continuation from
the poll's observation call through the Sleep Timer call: stored STA/source
comparison, conditional evidence store, helper-status branch and saved
registers. Only typed/allocated CDB RAM and verified call/end operands relocate.
The post-request instruction is checked as well. Read-only internal inspection
checks the original requested command at XDATA `0x0082`, computed deadline
at `0x0087`, private source-evidence byte at `0x009A` and the target's 19-byte
diagnostic object at `0x0038`. These are matching-artifact addresses, not a
new public ABI or permission to write RAM. Exact CODE checks also tie those
objects to the real pointer argument stores and the per-call evidence clear;
an allocated but unrelated CDB address is not accepted.

| SDCC 4.2.0 board image | CODE/BIN | Ordinary XDATA | Total used / reserved XDATA | Stack |
| --- | ---: | ---: | ---: | --- |
| generic clock_fixture | 3,758 B | 156 B | 188 / 220 B | `0x4E`, 178 B reserved |
| LG clock_fixture | 3,798 B | 156 B | 188 / 220 B | `0x4E`, 178 B reserved |

IRAM includes eight register-bank bytes, 45 DATA bytes, three overlay bytes
and BIT storage. The upper 128-byte simulator guard remains untouched; this
is not a measured hardware high-water mark. The 512-byte nonaliased reservation
budget, M0 `0x1E00..0x1E3F` reservation, allocator limit below `0x1E00` and
`0x1F00..0x1FFF` IRAM alias remain enforced.

BIN SHA-256:

- generic: `13bec2214263cfab52513cb5744fe971a7a254b13e674799410b5d3f7693c462`
- LG: `77f7142d1e4ef3a662ce8ffd7ce9803d110a80e867b1a55be5540b98d16867ca`

### Manual clock acceptance and induced-timeout mode

**Never run during offline development or CI.** The parent/operator must
separately authorize the board/image, safe setup, exclusive adapter access,
verified private recovery material and programming/readback. Program only
the checked board `clock_fixture`, never synthetic `clock_test.ihx`.
The runner itself grants no flashing, raw SFR access or host memory writes.

```text
.venv/bin/python tools/check_clock_hardware.py --bus BUS --address ADDRESS --board lg_esl29_rev03 --output build/lg_esl29_rev03/clock_fixture --cycles 1 --confirm-clock-test
.venv/bin/python tools/check_clock_hardware.py --bus BUS --address ADDRESS --board lg_esl29_rev03 --output build/lg_esl29_rev03/clock_fixture --cycles 1 --confirm-clock-test --induce-timeout
.venv/bin/python tools/check_clock_hardware.py --bus BUS --address ADDRESS --board lg_esl29_rev03 --output build/lg_esl29_rev03/clock_fixture --cycles 1 --confirm-clock-test --induce-late-timeout
```

Normal `--cycles` is 1..257 complete three-stage sequences, default 1. Current
numeric USB selection, checked board artifacts and explicit confirmation are
required before loading USB. The runner reset-attaches, requires reset PC 0
and config `26`, and compares **all physical CODE in the checked BIN extent
before any resume**. It uses existing read/reset/CPU/breakpoint permissions,
with no memory-write permission or SFR-whitelist expansion. Stage, source,
complete ABI, bounded diagnostics, LF/TICKSPD, MODE/IRQs, counter/heartbeat and
immutable M0 startup fields are checked. Full CPU snapshots bracket inspection;
NOP stepping must advance PC by exactly one without other changes.

`--induce-timeout` requires one partial sequence. After successful RC
idempotence it arms the verified RET only for the first XOSC setup. At the
breakpoint it verifies DPL=OK/DPS=0, the actual two-byte IRAM stack return
address through the existing read-only XDATA alias, the stored 24-bit deadline,
and the cleared RUNNING/XOSC state. It **disables that breakpoint before
resuming**, so rollback gets its own fresh, uninterrupted budget. The CPU is
held for a fixed 0.25 host seconds inside a bounded guarded operation. That
interval is only a stimulus, not proof of elapsed target time or calibration.
Success requires the actual returned `CLOCK_TIMEOUT`, raw request elapsed
strictly greater than 1,024 and below half range, confirmed `CLOCK_OK`
rollback and observed/current CMD/STA restored to the original settings.
Expected-timeout mode ends halted at terminal FAULT with exactly one prior
successful step; it is not a successful full sequence. Normal mode never
accepts FAULT. Recovery requires a **separate explicit normal run/reset**.

The original `--induce-timeout` remains the dangerous pending-cancel test.
It does **not** accept `CLOCK_ROLLBACK_UNCONFIRMED=9`: a never-observed
departure is reported as uncertainty and exits with error, even with old
CMD/STA matches. Confirmed rollback is possible only if the C driver observed
the pending source and its subsequent return. No fixed wait or repeated old
matches are treated as proof of cancellation.

The mutually exclusive `--induce-late-timeout` also requires `--cycles 1`.
After the same verified deadline RET it resumes to the checked post-request
instruction, disables that breakpoint, and holds the halted CPU for 0.25 host
seconds. It then resumes to the checked poll Sleep Timer call. Before allowing
the timestamp, read-only inspection must find actual C-observed requested
CMD/STA, source-evidence byte 1, zero completed polls and untouched rollback
diagnostics. Missing source evidence fails without another resume. That
breakpoint is disabled before the real time sample/rollback proceeds.
Actual request TIMEOUT, elapsed beyond the raw timeout, confirmed rollback
and agreeing driver/fixture snapshots remain mandatory. The hold is a
stimulus only; this mode never injects a clock status or timestamp.

Each debugger operation and breakpoint wait has one 10-second whole-operation
deadline; an excessive host hold fails before resume. Errors, unexpected PC,
wrong helper return/caller, malformed records, unconfirmed rollback and cleanup
failures suppress success JSON, with no retry/reset/reattach or resume after
failure. JSON is emitted only after successful cleanup, identifying the image,
observations and final halted READY or explicitly expected FAULT. No physical
oscillator-failure/stopped-clock injection, frequency measurement, LF
calibration completion, source precision, wake/IRQ/compare or RF is claimed.

### 2026-09-16 LG clock cancellation failure

The parent/operator reports programming and readback verification of the
**original 3,630-byte** LG clock image, SHA-256
`c41dae85c1707bd7315f2f82835b9ee0bd2ca954892f9a2c5756964790fb2395`.
One normal RC16-idempotent/XOSC32/RC16 sequence passed: `C9 -> 88 -> C9`,
11 raw ticks / 3 request polls for XOSC and 2 ticks / 1 poll for RC, with no
rollback. This is evidence for that old image's normal path, not the fix.

At **2026-09-16 23:24 (UTC+03)**, the deterministic pre-request timeout runner
**failed its acceptance check correctly**. The terminal record at FAULT `0x016C` had phase 4, reason 1,
stage 1, completed steps 1 and original `CLOCK_TIMEOUT=3`. Request elapsed
13,490 raw ticks exceeded 1,024, with one poll and helper status 0. Saved CMD
was `C9`, requested CMD `88`. The old driver reported rollback elapsed 3 ticks,
one poll, `CLOCK_OK=0`, and observed `C9/C9`, but the fixture's immediately
subsequent snapshot was **CMD `C9`, STA `89`** (XOSC selected, CLKSPD=1).
A later read-only parent observation found live `C9/C9` again. No reset or
resume occurred before those captures.

This demonstrates a real false-positive rollback confirmation: old STA can
match before a still-pending source change completes. **This old image did not
validate rollback.** The fix requires requested-source observation followed
by restored settings, otherwise bounded uncertainty/error. That experiment
left the old image at terminal FAULT; the corrected-image acceptance below
is a separate later record, not a reinterpretation of this failure.
Raw records remain private; this is only the operator-supplied processed
summary. No frequency/calibration or other M2 service acceptance follows.

### 2026-09-17 LG compiled-C clock acceptance

The parent/operator reports successful bounded acceptance of the **corrected
3,798-byte LG clock fixture**, SHA-256
`77f7142d1e4ef3a662ce8ffd7ce9803d110a80e867b1a55be5540b98d16867ca`.
The setup was the same LG Rev0.3 / CC Debugger / macOS 15.7.9 / Python 3.11.9 /
PyUSB 1.3.1 used in the earlier records. Programming/readback was explicitly
authorized; **all 3,798 physical CODE bytes were independently verified before
CPU resume** in the acceptance runs.

These corrected runs occurred on **2026-09-17 in UTC+03**, not September 16.
The parent verified private log modification times: programming 00:25,
pending-timeout 00:29, late-timeout 00:30, full recovery 00:37. These are
operator-local record timestamps, not clock-frequency measurements. The old
`c41dae85...` image's race at September 16 23:24 remains the separate failure
above; no private log was accessed or imported for this documentation update.

An initial normal sequence passed: RC16 idempotence `C9/C9`, 0 raw ticks /
0 polls; XOSC32 `88/88`, 11 ticks / 3 polls; return to RC16 `C9/C9`, 2 ticks /
1 poll. No rollback was attempted.

Both negative tests then **passed without weakening acceptance**:

| Corrected-image experiment | Original request result | Request raw ticks / polls | Rollback result | Rollback raw ticks / polls | Driver-observed and immediate fixture CMD/STA |
| --- | --- | --- | --- | --- | --- |
| Original `--induce-timeout`, pending cancel | `CLOCK_TIMEOUT=3` | 14,770 / 1, helper 0 | `CLOCK_OK=0` | 64 / 15 | Both `C9/C9` |
| Separate `--induce-late-timeout`, verified real C source evidence before timestamp | `CLOCK_TIMEOUT=3` | 27,850 / 1 | `CLOCK_OK=0` | 3 / 1 | Both `C9/C9` |

Both request elapsed values exceeded the 1,024-raw-tick timeout and remained
below half range. The pending-cancel case no longer accepted the old transient
3-tick / 1-poll match: the corrected C driver returned confirmed rollback
only after 64 ticks / 15 polls, with agreeing driver and immediate fixture
snapshots. The late-source case separately exercised the checked real C
source-evidence checkpoint before its late timestamp. **Both runs ended at
terminal FAULT `0x016C` with the original TIMEOUT**, not normal operation
success or an implicit retry.

A **separate explicit reset and normal recovery run** then completed
**257 full RC16-idempotent / XOSC32 / RC16 sequences = 771 actual C calls**:

| Stage | Occurrences | CMD/STA | Request raw ticks | Request polls |
| --- | ---: | --- | --- | ---: |
| 0, RC16 idempotence | 257 | `C9/C9` | 0 | 0 |
| 1, XOSC32 | 257 | `88/88` | 11..13 | 3 |
| 2, RC16 | 257 | `C9/C9` | 2..3 | 1 |

Every recovery call returned `CLOCK_OK=0` with rollback
`CLOCK_NOT_ATTEMPTED=8`. The runner checked completed-step/M0-heartbeat byte
wraps, immutable M0 startup status, preserved LF/TICKSPD command fields,
unchanged SLEEPCMD/IRQs and CPU-register preservation during inspection.
That run ended with **the corrected clock fixture halted at READY `0x016A`
on RC16**; the later IRQ acceptance below changed the installed image.
All six older board BIN hashes and historical M1/timebase evidence
remain unchanged.

This is **hardware-observed execution of the compiled C clock selector and
fixture on this LG board/image**, not just register-only observation or a
synthetic SFR simulation. It establishes the finite normal, pending-cancel,
late-source and separately reset recovery scenarios above. It does **not**
establish measured frequency, calibrated timing, LF calibration completion,
physical oscillator absence/failure, stopped-clock injection, power-cut
tolerance, natural 24-bit counter rollover, or IRQ/compare/wake, RF, AES or
flash services. Byte-counter wraps are not Sleep Timer rollover evidence.

Never-departed cancellation is still **unconfirmed**: old STA matches alone
cannot prove drainage, and `CLOCK_ROLLBACK_UNCONFIRMED=9` remains required
when its bounded observation cannot establish departure. These successful
physical cases do not validate that synthetic uncertainty/fault path as a
hardware experiment. Generic clock-fixture evidence remains **host/image/
simulator-only**; full **M2 #4 remains open**.

Only the operator-supplied processed summary is recorded here. Factory data,
identities, raw captures/logs and recovery files remain private. Generated
`hardware_tested=false` metadata continues to describe automated build evidence,
separately from this dated manual acceptance. No new hardware authorization
follows from this historical record.

## Timer1 IRQ board fixture

`IMAGE=irq_fixture` is a separate original non-RF image, built for `generic`
and `lg_esl29_rev03`. Both have host/image/simulator coverage; the unchanged
LG image additionally passed [dated compiled-C hardware acceptance](#2026-09-17-lg-compiled-c-irq-acceptance).
Generic hardware remains unobserved. It intentionally
links the published EA primitives and an actual CC2530 Timer1 ISR, not the
standalone generic-C52 `irq_test.ihx`, which must never be flashed.
The eight older BINs and all historical LG M1/timebase/clock evidence remain
unchanged. The subsequent parent-owned hardware run installed the IRQ image
and left the live LG board halted at READY `01BB`, EA/T1IE off and Timer1 stopped.

The [ownership contract](ARCHITECTURE.md#timer1-irq-fixture-ownership) requires
fresh awake reset, C9/C9, inactive channels, GPIO-selected pins and exclusive
ownership of EA/Timer1. Timer1 runs DIV=1/free-running until overflow is
observed with EA=0, then is stopped before PENDING. Restoring the inner token
must leave the event pending; restoring the outer token permits interrupt 9
at CODE `004B`. The ISR masks T1IE before acknowledging only OVFIF and returns
with RETI. There is no public timer/dispatcher API, pin output, DMA, sleep,
LF switching or calibration service.

### IRQ state ABI v1

The 64-byte `irq_fixture_state` is ordinary XDATA (currently `0000` on both
boards); resolve it from matching artifacts. All multibyte arrays are
little-endian. M0 remains its separate immutable startup record except for
the existing heartbeat byte.

| Offset | Size | Meaning |
| --- | ---: | --- |
| 0 | 4 | ASCII `M2IQ` |
| 4 / 5 | 1 each | Version 1 / byte size 64 |
| 6 / 7 / 8 | 1 each | Phase / original fault reason / stage |
| 9 / 10 / 11 | 1 each | Completed cycles / ISR count / count before arm |
| 12..19 | 8 | Disabled token/result, outer token, inner token/result, outer result, invalid-FF result, timebase status |
| 20 / 23 | 3 / 2 | Timeout 1,024 raw ticks / poll cap 4,096 |
| 25 / 27 | 2 / 3 | Pending-wait polls / elapsed raw ticks |
| 30 / 32 | 2 / 3 | Delivery-wait polls / elapsed raw ticks |
| 35 / 36 | 1 each | ISR-observed T1STAT / IRCON before source acknowledgment |
| 37..43 | 7 | Initial CMD, STA, SLEEPCMD, IP0, IP1, TIMIF, unrelated IRCON bits |
| 44..55 | 12 | Sampled IEN0/1/2, T1CTL, T1STAT, IRCON, IP0/1, TIMIF, CMD, STA, SLEEPCMD |
| 56 | 2 | T1CNTL-first latched counter snapshot |
| 58 / 62 | 4 / 2 | Zero reserved bytes / guards `69 96` |

Phases are INITIALIZED=1, RUNNING=2, READY=3, FAULT=4. Stages are IDLE=0,
WAIT_PENDING=1, PENDING=2, INNER=3, DELIVERY=4, DONE=5. Reasons are NONE=0,
ENTRY=1, BAD_PHASE=2, TOKEN=3, TIMEBASE=4, COUNTER_RANGE=5, TIMEOUT=6,
POLL_LIMIT=7, EARLY_IRQ=8, SOURCE=9, INVARIANT=10. The original reason is
latched; cleanup does not turn failure into success.
Outer result FF means not yet returned, not a primitive error code.
Other result fields are meaningful only after their stage executes.
The register group at 44 is a C observation, **not a live SFR read**: at ISR
entry/RETI it still contains the earlier INNER snapshot. The ISR-specific
fields and subsequent READY snapshot supply acknowledgment evidence.

`tools/debug_image.py irq-state` decodes a complete `--hex`/`--snapshot`
record, while `irq-checkpoints` reports the checked CODE/ABI proof. Both
require matching `--board`, `--image irq_fixture` and `--output`, and are
entirely offline.

### IRQ checkpoints and footprint

| Label / point | generic | LG |
| --- | --- | --- |
| `_irq_fixture_before_stop` | `018B` | `01B3` |
| `_irq_fixture_armed_stop` | `018D` | `01B5` |
| `_irq_fixture_pending_stop` | `018F` | `01B7` |
| `_irq_fixture_inner_stop` | `0191` | `01B9` |
| `_irq_fixture_ready_stop` | `0193` | `01BB` |
| `_irq_fixture_fault_stop` | `0195` | `01BD` |
| Actual Timer1 ISR / final RETI | `045A` / `048E` | `0482` / `04B6` |
| `irq_restore` entry | `0C69` | `0C91` |
| Outer restore LCALL / continuation | `07EC` / `07EF` | `0814` / `0817` |
| Actual timer start / source acknowledgment | `0726` / `0476` | `074E` / `049E` |

These addresses belong only to the checked builds below, not a stable API.
The five ordinary stops are exact NOP/RET leaves; FAULT is NOP/SJMP-to-self.
The verifier checks explicit function extents, main's call sequence, all
34 primitive bytes, all 53 ISR bytes and the actual `004B` LJMP. The ISR
push/pop template preserves A, DPL, DPH, bank-0 R7 and PSW and has no calls,
overlay scratch or timebase access. Its source write is literal `75 AF 1F`,
never a flag read/modify/write.

The outer-call proof verifies loading the actual byte token from state+14
into DPL, the real LCALL and result store at state+17. Accepted interrupt
return PCs are instruction boundaries at restore+9 (live DPL=1), restore+12
(DPL=OK), or the checked caller continuation through its result store.
If hardware enters after the leaf returned, the runner records
`interrupted_restore=false`, not invented live-argument evidence. Any return
outside that proved window is an error and requires separate investigation.
An interrupted leaf must retain its actual caller return address beneath
the hardware frame. Full CPU/active-IRAM snapshots must match at ISR entry
and just before RETI; stepping RETI must restore the recorded PC and pop
exactly two bytes.

The LG hardware runs below observed **restore+12, PC `0C9D`, DPL=OK (0)**
in every normal cycle. This is still inside the leaf, at RET after setting
the result, so `interrupted_restore=true`. Restore+9/live-token-1 remains
synthetic evidence, not an observed hardware window in these runs.

The armed stop is after a real pending-wait deadline and before the verified
timer-start write. The checker resolves typed four-byte start/deadline
storage (currently XDATA `0040`/`0048`) and checks the entire 107-byte
deadline-setup function with verified storage/call relocations; the runner additionally checks the
actual stored 24-bit `(start + 1024) & FFFFFF` relation. Delivery gets a
fresh deadline **after** restore's checked continuation, so ISR inspection
does not manufacture a delivery timeout.

| Board | Emitted CODE | BIN extent, including 63 vector-padding bytes | BIN SHA-256 |
| --- | ---: | ---: | --- |
| generic | 3,166 | 3,229 | `1bf7bc5c0c405a671b9065f279b4d2bcd89c04c3bcb71ce6e7e1c53382c76f3d` |
| LG | 3,206 | 3,269 | `b9bc83d7254944621f25d312f118ca6a044e808017f85bb6453f28c15cdb0ae1` |

Both use 121 ordinary XDATA bytes, 153 used / 185 reserved nonaliased bytes
including M0, stack start `21` (initial SP `20`) and 223 reserved stack bytes.
Foreground stop SP is `22`; interrupting the restore leaf gives ISR-entry
SP `26`, before its five context pushes. Upper-128 IRAM, ordinary allocation
below `1E00`, full status reservation and `1F00..1FFF` alias guards remain.
The exact nine unused-vector padding intervals account for 63 reserved,
unemitted CODE bytes; no linked instruction is fabricated to fill them.
BIN padding is FF and is included in physical CODE verification.

### Parent-only IRQ programming and acceptance

**Not a build/CI action; do not run during offline work.**

1. Build the selected board with `make BOARD=lg_esl29_rev03 IMAGE=irq_fixture PYTHON=.venv/bin/python all test`; retain matching BIN/IHX/HEX/map/mem/CDB/metadata and verify the hash above.
2. Separately authorize the exact board, exclusive adapter, safe supply and recovery material. Program only that board BIN with the established external programmer/readback procedure, never `irq_test.ihx`. Follow the [programmer lifecycle and recovery boundary](#manual-hardware-acceptance-and-recovery): external `cc-tool` may reset on normal return even without `--reset`; exit status alone is not readback evidence. This runner is not a flasher or a replacement for that authorization.
3. With the current numeric USB location, run a short normal acceptance, the separate negative experiment if authorized, then a separately reset wrap-covering normal recovery run:

```text
.venv/bin/python tools/check_irq_hardware.py --bus BUS --address ADDRESS --board lg_esl29_rev03 --output build/lg_esl29_rev03/irq_fixture --cycles 3 --confirm-irq-test
.venv/bin/python tools/check_irq_hardware.py --bus BUS --address ADDRESS --board lg_esl29_rev03 --output build/lg_esl29_rev03/irq_fixture --cycles 1 --induce-timeout --confirm-irq-test
.venv/bin/python tools/check_irq_hardware.py --bus BUS --address ADDRESS --board lg_esl29_rev03 --output build/lg_esl29_rev03/irq_fixture --cycles 257 --confirm-irq-test
```

Replace BUS/ADDRESS explicitly; there is no automatic selection or fallback.
Each invocation reset-attaches, requires PC=0/config=26, and verifies **every
physical CODE byte in the BIN extent before any runner resume**. Normal cycles
are 2..257, default 3. Pending/inner checkpoints prove the timer is stopped,
EA remains zero and no ISR has run. The runner reuses four breakpoint slots
for ISR entry/RETI, checks context and actual return, then verifies READY,
frozen counter, acknowledgment, cycle/heartbeat progression and immutable M0/
initial IRQ observations. Inspection and NOP stepping preserve CPU state.

Default debug config 26 suspends Timer1 while halted or debug-instructed;
STEP supplies approximately execution ticks (SWRU191F p.55). No config change
is made. A halted interval is **not timer progress or calibrated time**.
The negative mode holds the armed, still-stopped CPU for 0.25 host seconds
under one guarded deadline. It accepts only actual TIMEOUT=6, one pending
poll, raw elapsed strictly above 1,024 and below half range, zero ISR count,
disabled EA/T1IE and stopped timer at terminal FAULT. Pending/READY safety
breakpoints reject normal progress without running additional cycles.
Host sleep alone, another fault or a poll-cap result is not success.
Recovery is a separate explicit invocation/reset, never an automatic retry.

Each USB operation/checkpoint wait is bounded by one 10-second operation
deadline. Errors, unexpected PCs/frames, malformed records and cleanup errors
suppress success JSON, with no implicit resume/reset/reattach. No generic
SFR whitelist, host RAM/code writer or flash API is added.
Raw run records stay private; publish only a processed scope-specific result.
The bounded LG hardware record below is separate from these requirements;
it does not authorize another run. Higher-priority hardware nesting,
other sources, calibrated latency, an exact count of hardware overflows,
physical clock faults, DMA/wake/RF/AES/flash services remain outside this gate.

### 2026-09-17 LG compiled-C IRQ acceptance

The parent/operator reports that the **unchanged LG IRQ fixture and runner
passed without weakened assertions or a source fix**. The image is
**3,269 BIN bytes: 3,206 emitted CODE plus 63 FF padding bytes**, SHA-256
`b9bc83d7254944621f25d312f118ca6a044e808017f85bb6453f28c15cdb0ae1`.
The setup was the same owned LG Rev0.3 / CC Debugger / macOS 15.7.9 /
Python 3.11.9 / PyUSB 1.3.1 as the historical M1/timebase/clock records.
The parent independently passed both LG and generic IRQ `all test` with
355 Python tests and strict source/vector/ISR/alias/fault checks.

All times below are **2026-09-17, UTC+03**. Before programming, the parent
verified equal private recovery copies against their recorded checksums and
a valid factory-page backup; none was changed. The expected physical adapter
port was selected exclusively. Explicit external `cc-tool -e/-w/-v r`
erase/write/readback completed successfully at **02:55:33**.
**Each acceptance invocation independently read and compared all 3,269
physical CODE bytes, including FF padding, before any runner resume.**
These are operator-supplied processed facts; no private backup, identity,
capture or run log was accessed or imported for this documentation update.

An initial separate **three-cycle normal run completed at 02:55:58**.
Each pending wait was 134 raw ticks / 29 polls; delivery was 1..2 raw ticks /
1 poll. Every cycle used actual Timer1 vector `004B`, ISR `0482` and final
RETI `04B6`, with `isr_source=20` and `isr_cpu=00`, observing the CPU H0 clear.

The hardware interrupt return PC was **`0C9D` in every cycle: `irq_restore`
entry `0C91` +12, at RET after DPL=OK (0) had been set**. The runner verified
the nested caller frame and live DPL=0, compared complete CPU and active-IRAM
context at ISR entry versus just before RETI, then stepped actual RETI,
which popped exactly two bytes and resumed `0C9D`. This is hardware proof
of an **interrupted in-leaf result 0**, not restore+9 with live token 1,
synthetic C52 delivery or higher-priority hardware nesting.

A separate **`--cycles 1 --induce-timeout` run completed at 02:56:43**:

| Observed field | Result |
| --- | --- |
| Terminal PC / phase / original reason / stage | `01BD` / FAULT=4 / TIMEOUT=6 / WAIT_PENDING=1 |
| Completed cycles / ISR count | 0 / 0 |
| Pending elapsed / polls / helper status | 14,718 raw ticks / 1 / 0 |
| IEN0 (including EA) / IEN1 (including T1IE) / T1CTL | 0 / 0 / 0 |
| T1STAT source / IRCON CPU flags | 0 / 0 |

Elapsed exceeded 1,024 and remained below half range. This was the verified
pre-start deadline-hold stimulus followed by the real C failure and terminal
cleanup, not a stopped physical oscillator, calibration or successful normal
cycle. There was **no implicit recovery**.

A **separate explicit reset and normal `--cycles 257` run** started at
**02:57:19** and completed at **03:01:06**, passing **257 actual C cycles and
Timer1 ISR services**. The reported **226.77 seconds is host wall duration
only**, not a timer-frequency or latency measurement.

| Full-run observation | Result in all 257 cycles |
| --- | --- |
| Pending wait | 133..134 raw ticks, exactly 29 polls |
| Delivery wait | 1..2 raw ticks, exactly 1 poll |
| ISR source / CPU flags | `20` / `00` |
| Frozen Timer1 counter at PENDING / INNER / READY | 2,874 (`0B3A`) at all three checkpoints |
| Interrupt return PC / in-leaf result | `0C9D` = restore+12 / DPL=OK (0) |
| Context and return | `interrupted_restore=true`; full CPU/active-IRAM/stack preservation; actual RETI stepped each time |

Completed-cycle, ISR-count and M0-heartbeat bytes wrapped; final completed
and ISR counts were both 1. Immutable M0 and initial clock/sleep/IRQ
observations, priorities, unrelated IRCON/TIMIF, token results and disable/
stop conditions all passed. That run ended on the **IRQ fixture, halted at
READY `01BB`, EA=0, T1IE=0 and Timer1 stopped**. No hardware process remained
active. Later FIFO work is recorded separately below. All eight older BIN
hashes and published EA/timebase drivers are unchanged.

This establishes the finite real-source pending/nested-mask/delivery,
ISR/RETI-context, explicit timeout and separately reset repeat/wrap scenarios
on this LG image. It does **not** establish calibrated time/latency, exact
hardware overflow counts, true hardware one-shot behavior, higher-priority
hardware nesting, other IRQs, DMA, sleep/wake, RF, AES or flash services.
Other injected flag-race/stall/range faults remain host/simulator evidence.
Generic IRQ hardware remains unobserved; full **M2 #4 remains open**.
Generated `hardware_tested=false` metadata describes automated build evidence,
separately from this dated manual record. Past acceptance grants no new
hardware authorization; private records remain outside Git and CI artifacts.

## Quiescent radio FIFO board fixture

`IMAGE=radio_fifo_fixture` is a separately selectable, original non-RF board
fixture. It links the unchanged published clock, timebase and FIFO drivers.
Both boards are host-tested, image-checked and synthetically simulated.
The LG image additionally passed the [dated hardware acceptance below](#2026-09-17-lg-compiled-c-fifo-acceptance);
generic remains hardware-unobserved. Older hardware records remain historical.
This section grants no hardware authorization. Full M2 #4 remains open.

Fresh M0 startup and the selected board GPIO policy precede one bounded
`clock_select_init(XOSC32, 1024, 4096)`. Original request/rollback diagnostics
survive a terminal clock failure. Known reset/foreground ownership, awake
operation, confirmed CMD/STA `88`, disabled interrupt enables, idle radio/CSP,
AUTOCRC and no AUTOACK are prerequisites, not conclusions inferred from a
single matching snapshot. There is no DMA, RX/TX/FS/ACK enable, loopback, CSP
program, sleep, RF-off cleanup or error-flag acknowledgment.

READY stages are: `0` clock; `1` genuine already-empty clear; `2` three-byte
XDATA body `13 57 A9` (PHR `05`); `3` explicit TX clear; `4` 125-byte CODE
body `body[i] = i ^ 69` (PHR `7F`); `5` explicit TX clear and completed/heartbeat
increment. Stages 1..5 repeat. A 257-cycle run observes **1,286 READY stages**,
33,410 RFD writes, 514 intended TX clears and byte-counter wrap to one.
Fresh empty RX produces no ED strobe: EMPTY is not RX-flush/received-frame
evidence. EE is an explicit normal operation, never failure recovery.
The driver retains its 1,024-raw-tick whole-operation deadline and independent
4,096-poll cap, including written versus verified partial effects.

After confirmed preload, C reads only the known accepted bytes at
`6080..6083` or `6080..60FD`, checks every PHR/body byte and resamples
counts/pointers. No RFD read, unknown TX tail, RX RAM, source/address RAM
`6100..617F`, direct RAM writer or debugger MMIO expansion is introduced.
Read-only RAM inspection does not advance FIFO pointers
([primary references](PROVENANCE.md#m2-quiescent-radio-fifo-sources)).
Preloading neither transmits nor generates/verifies FCS or authenticates MAC.

### FIFO fixture byte ABI v1

The 108-byte `M2RF` record is ordinary XDATA, currently at `0000`; all
multibyte fields are explicitly serialized little-endian, not native structs.
The immutable 32-byte M0 startup ABI at `1E00` is unchanged; only its heartbeat
advances at stage 5. The complete 64-byte status reservation remains protected.

| Offset | Field |
| --- | --- |
| 0..5 | `M2RF`, version 1, size 108 |
| 6..11 | Phase, reason, stage, completed byte, original clock result, original FIFO result |
| 12..16 | Timeout 1024 (3 bytes), poll cap 4096 (2 bytes) |
| 17..35 | Original 19-byte clock request/rollback diagnostics |
| 36..56 | Original 21-byte FIFO diagnostics: elapsed/polls/helper, requested/confirmed strobes, written/verified bytes, errors, counts/pointers/signals, sample validity |
| 57..60 | Checked bytes, mismatch index (`FF` when absent), actual, expected |
| 61..66 | Current CMD, STA, SLEEPCMD, IEN0/1/2 |
| 67..80 | FRMCTRL0/1, CSPSTAT, FSMSTAT0, RXENABLE, FSMSTAT1, RX/TX count, RX first/last/packet, TX first/last, RFERRF |
| 81..89, 90..98 | Initial/current IP0, IP1, RFIRQF0, RFIRQF1, S1CON, TCON, RFIRQM0, RFIRQM1, RFERRM |
| 99..100 | Radio snapshot validity, initial SLEEPCMD |
| 101..107 | Five reserved zero bytes, guards `69 96` |

Phases are INIT=1, RUNNING=2, READY=3, FAULT=4. Reasons are NONE=0, ENTRY=1,
PHASE=2, CLOCK_ERROR=3, FIFO_ERROR=4, INVARIANT=5, BYTES=6. FIFO result `FF`
means not attempted, not driver success. A result inconsistent with its stage
is also terminal. On byte mismatch the original driver OK remains recorded.
No further stage/MMIO operation follows FAULT; recovery is a separate explicit
reset invocation. Clock failure may leave `radio_valid=0`: zero-filled radio
fields must not be described as observations.

### FIFO linked checkpoints and footprint

SDCC 4.2.0 emits 8,939 generic / 8,979 LG CODE and BIN bytes, without padding.
Both use 309 ordinary XDATA bytes: 341 including used M0 status, **373 with
the full reservation**. Stack starts at `6D`, initial SP `6C`, with 147 reserved
IRAM bytes. Inlining the stage into foreground `main` removes one return frame;
the deepest exercised chain stays below the unchanged `80..FF` guard.
The retained out-of-line compiler copy is included in these figures.

| Point | Generic | LG |
| --- | --- | --- |
| BEFORE / READY / terminal FAULT | `0140 / 0142 / 0144` | `0168 / 016A / 016C` |
| Deadline helper start / final RET | `0DCF / 0E62` | `0DF7 / 0E8A` |
| FIFO deadline LCALL / return PC | `1E7F / 1E82` | `1EA7 / 1EAA` |
| First/only RFD write instruction | `204B` | `2073` |
| Preload return / foreground caller return | `220C / 0C26` | `2234 / 0C4E` |

| Board | Complete BIN SHA-256 |
| --- | --- |
| generic | `4f7f2691d6d710ea48b5679a4e657a0660b61ece7ce1ec3d4ed377bc680641ac` |
| lg_esl29_rev03 | `caa26c090473b2e9008652d2ee67bb90226b493f392582d978a6ad71aaf37497` |

The verifier checks complete board instructions/constants, actual fixture
MMIO/read-only bounds, typed storage and the original 148-byte deadline helper.
Only reviewed FIFO relocations are normalized before requiring the unchanged
published 3,042-byte module fingerprint; the standalone 99-scenario proof
is retained. At the internal RET, DPL=OK and DPS=0, SP=`74`; the three nested
return frames, generic XDATA body pointer, body bytes, length=3, timeout/cap,
diagnostic pointer, start/previous/deadline and helper arguments must agree.
This is not a guessed next-symbol-minus-one breakpoint.

Offline inspection (no USB) uses:

```sh
.venv/bin/python tools/debug_image.py radio-fifo-checkpoints --board lg_esl29_rev03 --image radio_fifo_fixture --output build/lg_esl29_rev03/radio_fifo_fixture
```

`radio-fifo-state` additionally requires `--hex` or `--snapshot` for exactly
108 bytes. See [automated coverage](VALIDATION.md#m2-quiescent-radio-fifo-board-fixture-coverage)
for simulator limitations and fault evidence.

### Parent-only FIFO programming and acceptance

Confirm the owned board/revision, safe electrical state, independently verified
recovery backups, exact artifacts and exclusive expected adapter/physical port.
Follow the [programmer lifecycle/recovery contract](#manual-hardware-acceptance-and-recovery):
external programmer cleanup can reset/resume, and exit status alone is not
independent readback. Separately authorized external programming uses the
**board** HEX, never `radio_fifo_test.ihx`, for example:

```text
cc-tool -e -w build/lg_esl29_rev03/radio_fifo_fixture/radio_fifo_fixture.hex -v r
```

Only after that separately approved programming/readback activity, use the
current, explicitly selected numeric location in separate manual invocations:

```text
.venv/bin/python tools/check_radio_fifo_hardware.py --board lg_esl29_rev03 --output build/lg_esl29_rev03/radio_fifo_fixture --bus BUS --address ADDRESS --cycles 3 --confirm-radio-fifo-test
.venv/bin/python tools/check_radio_fifo_hardware.py --board lg_esl29_rev03 --output build/lg_esl29_rev03/radio_fifo_fixture --bus BUS --address ADDRESS --cycles 1 --induce-timeout --confirm-radio-fifo-test
.venv/bin/python tools/check_radio_fifo_hardware.py --board lg_esl29_rev03 --output build/lg_esl29_rev03/radio_fifo_fixture --bus BUS --address ADDRESS --cycles 257 --confirm-radio-fifo-test
```

Each invocation independently validates all matching artifacts, explicitly
reset-attaches and compares **every physical CODE byte** before any runner
resume. USB and checkpoint waits are bounded. It checks real C records,
immutable M0/clock/flag observations, counters, stack and CPU-preserving
inspection/NOP stepping. No host RAM/flash/MMIO writer is granted; JSON is
printed only after successful cleanup.

The negative mode first completes stages 0/1, then holds at the fully proved
operation-deadline RET before any FIFO write. The 0.25-second host hold is
stimulus only. Success requires actual terminal stage-2 FAULT, reason 4,
FIFO TIMEOUT=8, one poll, elapsed strictly above 1024 and below half-range,
**one written but zero verified bytes**, no strobe/append/readback, and zero
completed cycles. C's driver and post-call count/pointer observations are
reported: an unconfirmed PHR can already have affected the FIFO. This is not
a stopped oscillator, calibrated duration or error-latch recovery test.
The last command is a **new, separately authorized reset/recovery run**, not
an automatic continuation after failure. Normal completion leaves READY with
confirmed empty TX/RX, IRQs disabled and XOSC32 selected; timeout completion
leaves terminal FAULT and possibly one unconfirmed PHR. Do not infer physical
success, repeat permission or on-air capability from these instructions.

### 2026-09-17 LG compiled-C FIFO acceptance

The unchanged **8,979-byte LG image**, SHA-256
`caa26c090473b2e9008652d2ee67bb90226b493f392582d978a6ad71aaf37497`,
passed the existing runner without firmware changes or weakened assertions.
The setup remained LG Rev0.3 / CC Debugger / macOS 15.7.9 / Python 3.11.9 /
PyUSB 1.3.1. Private text-demo recovery copies and the factory-page backup were
rechecked against their saved checksums without modification. Explicit
programming/readback completed at **05:10:59, 2026-09-17 UTC+03**.
Every acceptance invocation independently compared **all 8,979 physical CODE
bytes before any runner resume**.

Three initial normal cycles completed at 05:11:43. A separate induced-timeout
run completed at 05:12:26. Its verified deadline RET was `0E8A`, with DPL=0,
DPS=0, SP=`74`, the genuine nested caller frames, XDATA body length 3 and
computed deadline consistent with the live helper arguments. After the hold,
the real driver returned **TIMEOUT=8**, fixture reason FIFO_ERROR=4, stage 2,
at terminal FAULT `016C`: 23,636 raw ticks / one poll, helper 0, **one written
but zero verified bytes**, no flush, append or payload readback.
Both driver and post-call observations found TX count/last pointer 1 and first
pointer 0, with no RF error. The unconfirmed write had a visible FIFO effect;
it was not relabeled as a verified byte. IRQs remained disabled, CMD/STA `88`.

A **separate explicit reset/recovery invocation** ran from **05:13:07 to
05:17:19 UTC+03**, completing **257 cycles / 1,286 READY stages**: one initial
clock stage and 257 occurrences of each FIFO stage below.

| FIFO stage | Result | Raw elapsed ticks | Polls | Written / verified / independently C-readback-checked bytes |
| --- | --- | --- | --- | --- |
| Already-empty clear | EMPTY=1 | 0 | 0 | 0 / 0 / 0 |
| Three-byte XDATA body plus PHR | OK=0 | 12..14 | 4 | 4 / 4 / 4 |
| Explicit clear after small body | OK=0 | 2..3 | 1 | 0 / 0 / 0 |
| 125-byte CODE body plus PHR | OK=0 | 439..441 | 126 | 126 / 126 / 126 |
| Explicit clear after maximum body | OK=0 | 2..3 | 1 | 0 / 0 / 0 |

The run checked **33,410 written, verified and readback-compared bytes** and
**514 confirmed TX clears**. The small PHR/body was `05 13 57 A9`; the maximum
PHR was `7F`, followed by the 125-byte CODE pattern `i ^ 69`.
Each preload's count/last pointer matched its accepted byte count and first
pointer remained zero. Direct C readback accessed only these known TX bytes
and did not advance pointers. RX stayed reset-empty: **no ED/RX flush or
received-frame behavior was exercised**. Clear strobes were only EE.

Completed/M0-heartbeat bytes wrapped to 1. CPU-preserving inspection, stack,
immutable M0, original clock diagnostics and unrelated flag/priority/mask
observations passed. The **last reported LG state is the FIFO fixture halted
at READY `016A`, CMD/STA `88`, all IRQ enables zero and both FIFOs empty**,
with inactive radio/CSP and AUTOACK disabled. No hardware process remained
active. The 252.283-second host duration and raw counts are not calibrated
timing or latency measurements.

All ten older BINs and the published platform drivers remain unchanged.
Generic has no physical FIFO evidence. This finite record does not validate
RX flush, received data, FCS generation, authentication/MAC, RF-enable/on-air
behavior, error-latch recovery, physical stopped clocks, DMA, IRQs, sleep,
AES or flash services. Full M2 #4 stays open. Only processed observations are
recorded; raw logs, identities and recovery files remain outside Git and CI.
Past acceptance grants no new hardware authorization.

## Awake-only timebase board fixture

`IMAGE=timebase_fixture` is a **separate non-RF board image**, intentionally
added after the isolated timebase foundation. It calls the actual C Sleep
Timer reader/deadline helpers; it is not `timebase_test.ihx`. That standalone
test executable remains unflashable and is never a board artifact.
The new image reuses original startup, board GPIO policy and the exact M0
status ABI. It neither replaces nor changes any `bringup`/`debug_fixture`
firmware bytes. Both boards are **host-tested, image-checked and alias-aware
simulated**. The [2026-09-16 LG acceptance](#2026-09-16-lg-compiled-c-timebase-acceptance)
additionally establishes hardware execution of the C reader and successful
deadline cycles on one LG Rev0.3; generic hardware remains unobserved.
The independent register-only hardware reference in [VALIDATION.md](VALIDATION.md#independent-sleep-timer-hardware-reference-2026-09-16)
does not change that distinction.

```sh
make BOARD=generic IMAGE=timebase_fixture all test
make BOARD=lg_esl29_rev03 IMAGE=timebase_fixture all test
python3 tools/debug_image.py symbols --output build/lg_esl29_rev03/timebase_fixture --board lg_esl29_rev03 --image timebase_fixture --name _timebase_fixture_state
python3 tools/debug_image.py breakpoint --output build/lg_esl29_rev03/timebase_fixture --board lg_esl29_rev03 --image timebase_fixture --name _timebase_fixture_ready_stop --slot 1
```

These commands are offline. Outputs are `build/<board>/timebase_fixture/`
unless `BUILD` is overridden. CI covers both boards and all four board
images; its explicit artifact whitelist excludes standalone test executables.

### Timebase state ABI v1

`_timebase_fixture_state` is a 32-byte, byte-oriented object in **ordinary
linker-accounted XDATA below `0x1E00`**. Resolve its address from matching
artifacts, never from the M0 reservation, unused SRAM or another build.
All offsets are decimal; multibyte arrays are explicitly little-endian.

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | Signature `M2TM` (`4D 32 54 4D`) |
| 4 | 1 | ABI version `1` |
| 5 | 1 | Size `32` |
| 6 | 1 | Phase: `1` INITIALIZED, `2` RUNNING, `3` READY, `4` FAULT |
| 7 | 1 | Reason: `0` none, `1` deadline helper error, `2` expiry helper error, `3` clock range/backward step, `4` poll limit, `5` invalid phase |
| 8 | 1 | Completed cycles modulo 256; increments only on success |
| 9 | 1 | Last helper status: `0` OK, `1` INVALID_ARGUMENT, `2` AMBIGUOUS; initialized zero does not claim a helper was called |
| 10 | 3 | Start raw ticks |
| 13 | 3 | Last sampled end raw ticks |
| 16 | 3 | Constructed raw deadline |
| 19 | 3 | Masked `end - start` raw elapsed ticks |
| 22 | 2 | Polls in this attempted cycle, `0..1024`; excludes the initial start sample |
| 24 | 2 | Fixed requested delay `128` raw ticks, not milliseconds |
| 26 | 2 | Fixed poll budget `1024` |
| 28 | 2 | Reserved, initialized zero and otherwise untouched |
| 30 | 2 | Guards `69 96`, initialized once and otherwise untouched |

Initialization clears cycle data and publishes phase 1 after filling the
record. `timebase_fixture_begin()` accepts INITIALIZED or READY, publishes
RUNNING, samples once and calls `timebase_deadline_after()`. Each
`timebase_fixture_poll()` samples once and calls `timebase_expired()`.
The foreground example polls only while RUNNING. A stopped Sleep Timer, with
the CPU still executing, reaches FAULT after exactly 1,024 polls rather than
hanging or advancing heartbeat.
A valid expiry on the last permitted poll succeeds.

Helper errors are recorded explicitly. Elapsed time and each forward
observation step must be below `0x800000`; a backward step or out-of-window
elapsed value faults, even if modular expiry alone would appear successful.
Exactly-half deadline ambiguity preserves its helper error. A successful
cycle must have elapsed `>=128` and `<0x800000`. It updates all result bytes,
completed cycles and M0 heartbeat **before publishing READY last**. No failure
increments either counter. Further begin/poll calls leave a latched fault
unchanged; only explicit initialization/reset clears it. The target never
automatically retries or reinitializes after a fault.

Halt at a matching named checkpoint for a complete record. `timebase-state`
rejects RUNNING, unknown phases, wrong sizes/guards, inconsistent
reason/helper combinations, limits and modular results. It can decode a
FAULT for diagnosis; a decoded fault is not a successful cycle.
The record cannot detect missed complete counter wraps, every possible reset,
or memory corruption that happens to form another valid record. It does not
repair guards or provide an authenticated reset epoch.

All code is foreground-owned. There are no ST0/ST1/ST2 writes, clock switches,
calibration, compare/IRQ setup, sleep entry/wake handling or peripheral
services beyond the existing startup/board policy and raw reader.
The hardware facts remain TI SWRU191F sections 11.1/11.4, pp.129-131; the
nominal 32-kHz source is not converted into a precise tick period.

### Timebase checkpoints and footprint

| Linked CODE symbol | Exact instructions / boundary |
| --- | --- |
| `_timebase_fixture_before_sample` | `NOP; RET`; INITIALIZED before the first sample, previous READY before subsequent cycles |
| `_timebase_fixture_ready_stop` | `NOP; RET`; complete successful cycle, after all C helpers returned |
| `_timebase_fixture_fault_stop` | `NOP; SJMP` back to that NOP; complete terminal fault, no calls/retries |

These are distinct naked functions in the target example; ordinary C logic
lives in `src/timebase_fixture_state.c`. Their addresses and source mappings
come from the matching image. SP at each checkpoint is `s_SSEG + 1`, inside
one call from main, after deeper calls have unwound.

| SDCC 4.2.0 timebase fixture | CODE bytes | Ordinary XDATA | M0 used / reserved | IRAM stack reserved |
| --- | --- | --- | --- | --- |
| generic | 1807 | 88 bytes | 32 / 64 bytes | 223 bytes |
| lg_esl29_rev03 | 1847 | 88 bytes | 32 / 64 bytes | 223 bytes |

The 88 ordinary bytes include the 32-byte fixture, current-cycle state and
compiler scratch/parameters. Total nonaliased XDATA is 120 used / 152 reserved,
within the unchanged 512-byte reservation budget. IRAM has eight register-bank
bytes, one DATA byte, three overlay bytes and BIT storage; the stack begins
at `0x21`. The upper 128-byte simulator guard stays untouched. Reserved stack
space is not a measured hardware high-water mark. XDATA `0x1F00..0x1FFF`
remains the IRAM alias, not additional storage.

The image verifier checks the real reader's 88 instruction bytes with only
its six scratch-address operands relocated from exact CDB XDATA records.
Unallocated, overlapping or conflicting scratch records fail closed. This
does not relax the original standalone reader's exact bytes at scratch 0/1/2.

### Manual timebase acceptance runner

**Do not run during offline development or CI.** A separate hardware task
must establish the exact board/image, safe GPIO/display setup, exclusive
adapter ownership, verified private recovery material and separately
authorized programming/readback. Program only the validated board
`timebase_fixture` image, never the standalone `timebase_test.ihx`.
This runner itself never programs or injects ROM/RAM.

After that separate preparation and explicit authorization:

```text
.venv/bin/python tools/check_timebase_hardware.py --bus BUS --address ADDRESS --board lg_esl29_rev03 --output build/lg_esl29_rev03/timebase_fixture --cycles 3 --confirm-timebase-test
```

Supply the current numeric USB location, not an address retained across
reconnection. `--output` selects checked local artifacts, not private dumps
or a log destination. `--cycles` is 1..257, default 3. The confirmation grants
reset-attach, CPU control, read-only memory inspection and breakpoint
permissions; it grants **no host RAM writes or flash operations**. There is
no SFR whitelist expansion or raw Sleep Timer debugger sampling.

The runner validates board/image/compiler/hash metadata and rechecks the
loaded program hash before loading USB. It reset-attaches, checks PC zero and
debug configuration `0x26`, then compares **all physical CODE bytes in the
checked image extent before any resume** using bounded read APIs. It does not
verify the remainder of flash or factory/NV pages. It clears stale breakpoint
slots and arms before-sample, ready and fault checkpoints from matching
symbols. Initial phase/heartbeat are checked at before-sample, then that
breakpoint is disabled so the C cycle runs without an intentional mid-cycle
halt.

Each successful cycle requires the complete READY record, successful helper
status, poll count in `1..1024`, raw elapsed `>=128` and `<0x800000`, matching
deadline/end/start, cycle/heartbeat progression and unchanged M0 fields.
Full CPU snapshots must match before/after inspection. A linked NOP must
advance PC by exactly one with the rest of CPU state preserved before the next
resume. The final successful run leaves the CPU halted at the READY NOP.

Every existing guarded debugger operation has a 10-second whole-operation
deadline. Each breakpoint wait shares one such deadline across all status
polls and the final PC read, not a new timeout per poll. CODE comparisons use
fixed-size bounded reads without retries. FAULT, unexpected PC/state, malformed
records, I/O errors and timeout terminate immediately; no later resume,
reset, reattach or recovery is attempted. Cleanup only releases resources,
and cleanup errors suppress success JSON. On success the JSON identifies
the checked image hash, complete cycle observations, startup clock snapshot
and final halted PC. It does **not** claim calibration, guaranteed natural
rollover, clock switching, wake/IRQ/compare, RF, AES or flash acceptance.

The dated LG run below is distinct from the independent M1 register-only
observation. Raw records/identities remain outside Git and CI artifacts.

### 2026-09-16 LG compiled-C timebase acceptance

The operator explicitly authorized programming and acceptance of the new LG
`timebase_fixture` after rechecking private text-demo recovery copies.
External **cc-tool 0.26** performed programming with readback verification.
The manual runner then independently compared **all 1,847 physical CODE bytes**
against the checked local image **before any resume**. This is actual
execution of `src/timebase.c` and `src/timebase_fixture_state.c`, not supplied
register-read instructions or the standalone `timebase_test.ihx`.

| Item | Observed/tested configuration |
| --- | --- |
| Board/image | One LG ESL Rev0.3, `lg_esl29_rev03`, SDCC 4.2.0 `timebase_fixture`; 1,847-byte BIN/CODE extent |
| BIN SHA256 | `cd64743bc5095a37711885236fa65367dc375a57a509b322f6d5678ffdf954ef` |
| Adapter/host/backend | Same TI CC Debugger and LG setup as the M1 record; macOS 15.7.9, Python 3.11.9, pinned PyUSB 1.3.1 |
| Initial acceptance | Three cycles passed: elapsed 130/129/129 raw ticks for requested 128, 37 polls each |
| Fresh full acceptance | 257 cycles passed; every `helper_status=0`, `reason=0`, elapsed 129..130 raw ticks and exactly 37 polls per cycle, below the 1,024-poll limit |
| Publication/progression | Complete READY records and completed-cycle/M0 heartbeat progression passed, including byte wrap: cycle 256 -> 0, cycle 257 -> 1 |
| Initialization/inspection | Initial reset PC `0x0000`, before-sample checkpoint, NOP `PC+1`, immutable M0 status and full CPU-register preservation during inspection passed |
| Clock snapshot | Startup `CLKCONCMD=CLKCONSTA=C9`, unchanged: 32-kHz RC source / 16-MHz RC system source |
| Final state | `_timebase_fixture_ready_stop`, PC `0x016A` (362), READY, CPU halted; completed-cycle/heartbeat bytes both 1 |

This is **hardware-observed compiled-C awake timebase acceptance on that LG
board/image**. The 8-bit cycle/heartbeat wrap is not a natural 24-bit Sleep
Timer rollover claim. These 257 cycles do not establish calibrated timing,
a precise tick rate, natural counter rollover, physical stopped-clock
injection, source switching, IRQ/compare/wake, RF, AES or a project flash
service. Stopped/backward/ambiguous C paths retain **host and simulator**
coverage only. Generic `timebase_fixture` remains host/image/simulator-only,
and **M2 #4 remains open** for the other platform gates.

That run left the timebase fixture halted at `0x016A`, not the old M1 fixture;
the later clock experiment is recorded separately above. All four pre-existing board BIN hashes remain
unchanged, including the 626-byte LG M1 hash
`e3459339d63a63ae9aa71cdc01a4dd18cb6e2b079f86968da507ac57ff133815`.
The M1 record below remains historical evidence for that earlier image.
The separate [register-only experiment](VALIDATION.md#independent-sleep-timer-hardware-reference-2026-09-16)
retains its own natural-rollover evidence; it is not substituted for this C
execution. Only this processed operator summary is published, not private
run JSONs, recovery copies or identities. Generated `hardware_tested=false`
build metadata still describes automated build evidence, not this physical run.

## 2026-09-16 LG fixture hardware record

The operator explicitly authorized checks, reset and flashing on the owned LG
board on 2026-09-16. This is a sanitized **hardware-observed** record, not a CI
result or evidence imported from a prior display prototype. Flower and the
private GPL-derived probe were not accessed. No private dumps, factory data,
identities, photographs or recovery/session files are published here.

| Item | Observed/tested configuration |
| --- | --- |
| Board | One LG ESL Rev0.3, `lg_esl29_rev03`; no generic-board observation |
| Adapter | TI CC Debugger `0451:16A2`; firmware version `0x05CC`, revision `0x0044`, descriptor `bcdDevice=0x0701` (not a serial number) |
| Host | macOS 15.7.9, Python 3.11.9 |
| Backend | Final 257-cycle run: isolated, pinned PyUSB 1.3.1; preliminary successful runs also used global 1.2.1 and are **not** labeled pinned |
| Related host regression run | 309 passing pinned-dependency tests, including 18 fully offline macOS fault-injector tests; not hardware evidence |
| Fixture | Original, unchanged, non-RF `debug_fixture`, SDCC 4.2.0; 626-byte BIN/CODE extent |
| Build record | Public `build-info.json`: revision `8a26b088e69ad15bf23b0374491639b58ae02901`, clean build inputs |
| BIN SHA256 | `e3459339d63a63ae9aa71cdc01a4dd18cb6e2b079f86968da507ac57ff133815` |

The generated manifest still has `hardware_tested=false`: a build does not
certify a physical run. This dated record associates the observation with its
exact BIN hash; it does not change the generated metadata or firmware.
Neither standalone `bringup` image was flashed. LG shared startup, board
policy/status and alias behavior were exercised only through this fixture.

Before programming, two independent full 262144-byte backups of the user's
**text demo, not an OEM image**, and a 2048-byte read-only factory-page capture
were retained outside Git. External `cc-tool` 0.26 programming with `-v r` was
followed by an independent full 262144-byte readback matching the 626-byte
fixture plus an `FF` tail; the factory page was unchanged. Backup/capture
contents and paths are intentionally absent from this record.

### Observed standalone wire contracts

All bytes are hexadecimal, OUT endpoint `04`, IN endpoint `84`.
`i` denotes supplied instruction bytes; `control AH AL` are the three target
breakpoint parameters. These are individually observed forms, not a general
adapter-bytecode grammar.

| Operation | Complete OUT payload / count | IN payload / count |
| --- | --- | --- |
| GET_PC | `3F 28` / 2 bytes | PC high byte, low byte / 2 bytes |
| DEBUG_INSTR, one supplied byte | `4F 55 i` / 3 bytes | ACC / 1 byte |
| DEBUG_INSTR, two supplied bytes | `7F 56 i i` / 4 bytes | ACC / 1 byte |
| DEBUG_INSTR, three supplied bytes | `AF 57 i i i` / 5 bytes | ACC / 1 byte |
| SET_HW_BRKPNT | `AF 3F control AH AL` / 5 bytes | STATUS / 1 byte |

GET_PC observed reset `0x0000`, reset-vector LJMP destination `0x0006`, and the
linked NOP moving `0x0173 -> 0x0174`. Supplied DEBUG_INSTR executes without PC
increment and returns ACC, including high-bit data; its reply is not STATUS.
The failed hypotheses `2F 28` (one reply byte) and `8F 56 ...` (zero reply
bytes) are not shipped. Primary TI target facts and the public MIT/GPL
reference boundaries are recorded in [PROVENANCE.md](PROVENANCE.md#m1-live-access-and-manual-recovery-sources).

### Fixture execution and preservation

All four slots were configured simultaneously at the linked stage entries;
each hit was checked and that slot then disabled to reach the next stage.
Addresses below identify this exact image, not stable ABI constants:

| Slot / stage | PC | SP |
| --- | --- | --- |
| 0 | `0x021A` | `0x0B` (11) |
| 1 | `0x01FD` | `0x0D` (13) |
| 2 | `0x01E1` | `0x0F` (15) |
| 3 | `0x01CD` | `0x11` (17) |

At stop `0x0173`: A=`0xA5`, B=`0x3C`, DPTR0=`0x1234`, DPS=0,
active bank 0/R7=`0x69`, carry=1 and SP=`0x09`.
The final pinned-backend run completed **257 physical cycles**, including
counter wrap. It checked the complete expected M1 state, M0 heartbeat and
immutable status, unchanged registers/state around NOP `PC+1`, and successful
returns into real calls on resume. Explicit HALT, reset PC zero and fixture
reinitialization also passed.

The 16-byte XDATA scratch at `0x1D00` was written, read back and restored.
DATA `0x40` / XDATA `0x1F40` were checked bidirectionally and the original
byte restored; ordinary public writes at/above `0x1E00` remain forbidden.
A separate physical matrix covered all **eight PSW-register-bank x DPS**
combinations (four banks, DPS 0/1), with nonzero DPTR1=`0x5678`, through
XDATA/CODE/SFR reads. The original context was restored.

### Failure and recovery observations

Real USB tests included an empty-IN 10-ms timeout (`-7`), a zero-length reply,
and an explicitly halted OUT endpoint yielding PIPE (`-9`). Each latched
`FAULTED`, denied resume and performed no retry. Cleanup followed by **explicit**
endpoint recovery left the same halted PC. Ordinary API open/close does not
clear stalls. Software USB device resets preserved PC, but the USB address
changed later; this is not cable-disconnection evidence.

For the controlled interrupted-flash experiment, the original DYLD observer
saw external `1C 14`, then fresh `1F 34`/one-byte status `0x22`, and exited 99
before programming or normal-reset cleanup. While halted, 640 CODE bytes
(`0x0000..0x027F`) were independently checked as `FF`. A subsequent **explicit
complete** fixture reprogram/readback and independent CODE verification
succeeded, followed by the full 257-cycle isolated-PyUSB-1.3.1 pass above.
This establishes recovery at the **halted erased-image boundary only**:
not a mid-word electrical power cut, arbitrary power-loss recovery or flash
wear endurance.

### Physical cable scenarios and last-reported availability

The earlier interactive confirmation timeout/absent-adapter observation was
not counted as a pass. The measured cable scenarios occurred in this order:

1. **First confirmed cable reconnection:** the adapter was explicitly selected
   at its new USB address. The entire one-cycle fixture check passed, including
   physical CODE verification, all four slots, alias checks and reset.
2. **Final held-handle physical unplug:** after the operator removed the cable
   with the selected CC Debugger handle open, production `read_pc()` failed at
   control IN with USBError errno 19 / backend `-4` NO_DEVICE. The session was
   `FAULTED` and resume was denied without any increase in the bulk-write
   counter: no retry or resume I/O occurred. Explicit cleanup surfaced
   release-interface NO_DEVICE instead of swallowing it.
3. **Final explicit recovery after the last replug:** PyUSB enumerated the
   adapter again. After explicit selection at its new address, a full one-cycle
   `check_debug_hardware` run in a new session passed: reset PC `0x0000`, all
   626 physical CODE bytes matching the fixture with the BIN hash recorded
   above, all four breakpoint slots and their SPs, alias/RAM restoration, one
   golden cycle/NOP and reset reinitialization. The last action left the fixture
   halted at `0x0173`. This successful recovery supersedes the interim absence;
   no current NO_DEVICE blocker remains. Numeric locations, device identities
   and private paths are intentionally not published.

**M1 is complete for this bounded LG/unbanked baseline.** The proven
fresh-reconnect fixture check, subsequent physical-disconnect failure check
and final explicit new-session recovery establish the finite gates; none is
inferred from enumeration alone. Generic hardware and banked-code
discrimination remain outside the record; the latter is required only when
banked CODE is introduced. Mid-word power cuts and flash wear belong to future
platform/persistence evidence. There is no universal-debugger, sleeping-target,
MMIO/full-SFR, flash-writer, GDB, M2 or RF/network claim.

## Radio debugging without destroying timing

CPU halts and debug configuration affect timers and sleep. A breakpoint in an
ACK/poll transaction can manufacture a failure that is absent during normal
operation. Conversely, a permanently connected debugger can hide sleep bugs.

Use three complementary evidence sources:

| Tool | Good for | Not sufficient for |
| --- | --- | --- |
| Hardware halt/step/breakpoint | Memory corruption, state and control flow | Real-time MAC timing or final sleep current |
| Bounded RAM event trace | Sequence of events during uninterrupted execution | Proving what was actually transmitted over air |
| Independent 802.15.4 sniffer | Frames, retries, ACKs and peer behavior | Internal memory ownership or power consumption |

Add a logic analyzer/current measurement when testing timing and power.
Trace event IDs, bounded counters and reasons rather than dumping keys or
private payloads. Retrieve traces after the relevant event and sanitize any
material intended for public fixtures.

UART logging can help early bring-up, but P1.6/P1.7 cannot simultaneously be
UART logging and the proposed software-I2C bus. Debugger pins are separate.
