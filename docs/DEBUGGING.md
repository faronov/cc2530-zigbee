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

**This repository has a partial host transport, offline image tools and a
target fixture, not a validated hardware debugger.** Guarded CPU control is
host-tested, as are separately authorized reset into halt and reset-based
initial attach. Live PC/memory/register access, breakpoint programming
and non-reset attach remain unimplemented.
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
Debugger diagnostic and CPU-control exchanges. Its optional PyUSB backend is
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
There is no memory dump, serial-number read or payload logging.

CPU-control methods require `allow_cpu_control=True` in Python, or the CLI's
additional `--allow-cpu-control` flag. Existing-session and CPU-control
permission do not grant reset, flash writing, memory access or breakpoint
programming. The operator must have verified the intended target image
separately: the flag is an authorization,
not proof that the bytes on a chip match a local build. **No hardware commands
are being run during offline development, even with an adapter absent.**

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
STEP then requires a fresh halted post-status. Live PC advancement is not
checked by this transport yet; the target fixture's simulated NOP `PC+1` check
is a separate evidence level.

GET_BM's upper five reply bits are unspecified here; only its low three bits
are returned. This is FMAP bank information, not a flat flash address or proof
of banked-code support. Config reads now check status before/after and reject
locked or erasing targets. Raw READ_STATUS remains available for diagnosing
those states; RD_CONFIG is not a permitted command on a locked CC2530.

### Reset-based initial attach

The Python `Access.RESET_DEBUG_SESSION` policy requires
`allow_target_reset=True`. Opening only claims the selected adapter; target
reads, HALT/RESUME/STEP and `reset_halt()` remain denied until an explicit
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
reset. Hardware timing, actual initial PC and compatibility with a particular
adapter firmware remain unobserved.

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
memory initialization or matching flash contents. Those need the remaining
PC/memory transport and physical acceptance checks.

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
```

These are manual examples, **not commands invoked by builds or CI**. No udev
rules, permission changes, automatic driver detachment or USB discovery are
installed by this repository. Do not use `sudo` as a substitute for a reviewed
device-access policy or run a debugger operation merely to clear a warning.

Evidence is **host-tested only**: synthetic conversations check exact packets,
selection, permission gates, reply lengths, deadlines, failure latching and
cleanup. Optional tests exercise the actual pinned PyUSB resource manager
with a synthetic driver, never a real libusb device. They are explicitly
skipped when PyUSB is absent; CI also runs them with PyUSB installed.
The public wire facts and licenses are pinned in [PROVENANCE.md](PROVENANCE.md).
There is no hardware-observed USB/target result, USB firmware compatibility
claim, or validation of real timeout/disconnect behavior yet.

## Offline image, symbol and snapshot tools

`tools/debug_image.py` never imports the USB transport or enumerates devices.
Its artifact commands first run the existing strict image/layout verifier,
then require matching board/image/toolchain metadata and all six artifact
hashes from `build-info.json`. This detects stale or mixed build files; it is
**not signature authentication or verification of a physical target image**.
Do not inspect a build directory concurrently with relinking it.

The reader supports linked SDCC 4.2.0 global functions, CODE labels/data,
XDATA objects and byte SFR symbols used by these images. It combines map
addresses with CDB space/type/size and global address records, rejecting
conflicting records.
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
16 bytes of M1 state. Either accepts `--hex` or `--snapshot PATH`, never both.
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

## Remaining M1 gates before M2

| Remaining work | Boundary / evidence still needed |
| --- | --- |
| USB framing for PC reads, supplied instructions and breakpoint programming | A reviewed multi-byte adapter protocol source or separately authorized, sanitized hardware evidence; the target opcode table alone is insufficient |
| Register-preserving memory/register reads and writes | Implement the verified transport first, then prove restoration and failure behavior on the fixture |
| Physical HALT/RESUME/STEP, bank read and four breakpoint slots | Confirm exact board/image and adapter firmware; compare expected state and known NOP PC movement |
| Physical reset/initial attach | Confirm `C5`/`C8` preparation, reset PC, status, timing and subsequent fixture initialization; host tests alone do not prove any of these |
| Disconnect/timeout and interrupted-flash recovery | Exercise controlled failures without resuming an unverified image; flashing is not implemented by this tool |
| Banked breakpoints | A banked fixture plus bank-discrimination evidence when banking is introduced |

The one-command/one-byte-response USB shape is reused only for the documented
zero-argument target commands. More complex framing is not extrapolated from
it. Host tests validate our packet/state contracts, not the adapter firmware.
M1 therefore remains open: do not describe it as hardware-validated or use it
to bypass the M2 hardware-services gate.

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
nested C function entry addresses for later hardware-breakpoint tests. They
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

For an eventual register-preservation test, stop at the NOP, save the CPU
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

**Not established:** physical breakpoint slots, USB error recovery, CC2530
debug-instruction register preservation, reset/attach behavior, FMAP/banked
breakpoints or interactive source-level host debugging. The image is lower-32-KiB unbanked
code. uCsim address stops are not CC2530 hardware breakpoints. M1 remains open,
and no hardware-observed evidence is claimed.

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
