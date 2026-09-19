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
| API only: `enable_dma_after_reset()` | Separate DMA-enable and reset permissions; fresh own-reset eligibility | Verified fixed debug-config transition `26 -> 22`; no DMA-register access |
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
are returned. Table 3-1 p.54 identifies these as **FMAP.MAP**, not the bank
containing the current PC: FMAP=1 at unbanked PC=0 is valid. This is not a flat
flash address or proof of banked-code support. Config reads check status before/after and reject
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

It does not prepare a new adapter session, issue a debug-configuration write, poll
until the oscillator stabilizes, retry reset, issue a fallback HALT or send
index `0` (reset into normal execution). A failed or late postcheck faults the
session even if reset has already occurred. Closing only releases USB
resources; it never undoes a halt or starts the firmware.
Reset itself can restore the reset configuration; the separately observed
`22 -> 26` recovery below is not a restore-on-close operation.

`command_sent=true` plus halted status is not independent proof of reset PC,
memory initialization or matching flash contents. The live PC/CODE APIs and
manual fixture runner make those separate comparisons; the dated hardware
record identifies the image for which they passed.

### Guarded DMA enable after reset

`Debugger(..., allow_dma_enable=True, allow_target_reset=True).enable_dma_after_reset() -> int`
is an **API-only prerequisite for reviewed manual DMA fixtures**, not a CLI
option or arbitrary configuration writer. The explicit boolean DMA permission
is separate from CPU/memory/reset permissions and requires reset permission.
Only this session's successful `attach_reset()` or `reset_halt()` with normal
reset status `22` grants one reset-history eligibility; an existing halted
session alone never does. Resume/step, a HALT observation of a running CPU,
contradictory target/status/config/PC observations, or an enable attempt consume
eligibility. Healthy passive CODE/core inspection may precede enable; a manual
DMA runner must verify **every physical CODE byte of the intended image before
enable**. The API does not perform or claim that image verification.

One operation deadline covers target-family checks, status `22`, PC=0,
saved FMAP.MAP and config `26`, the exact write, explicit config `22` readback,
and PC/FMAP/status postchecks. FMAP is preserved, **not pinned to zero**.
Success returns integer `0x22`. The transition clears only DMA_PAUSE bit2,
preserving SOFT_POWER_MODE and TIMER_SUSPEND. SWRU191F Table 3-2 p.55 prohibits
DMA-register access while DMA_PAUSE is set, including while the CPU runs in
debug mode; this is not merely a halt-time suspension.

The only configuration packet is **bulk OUT `04`, three bytes `4C 1D 22`**.
It has no paired USB read. Target WR_CONFIG is `00011XXX`; `1D` is a documented
don't-care-low-bit encoding. Table 3-1 p.53 specifies one target input byte
and one STATUS output byte, but this individually observed adapter form exposes
no unsolicited USB response. Do not substitute guessed `4F 19` framing or
`_exchange(..., 1)`, or infer a general USB grammar. Verification uses a
separate RD_CONFIG.

This API performs no DMA SFR/MMIO access, CPU resume, implicit reset/retry or
restore-on-close. Failed or late operations fault the session even if the
hardware accepted the write; only resource cleanup remains. All
pre-DMA hardware runners still require config `26`; none enables DMA automatically.
Only the separately authorized [DMA fixture runner](#channel-0-dma-board-fixture)
and [AES fixture runner](#aes-dma-board-fixture)
use this gate after full CODE verification. The gate itself is not controller acceptance.

#### 2026-09-17 LG DMA-enable gate acceptance

These parent-supplied processed observations apply only to the explicitly
selected LG Rev0.3 / CC Debugger and unchanged **8,979-byte FIFO image**,
SHA-256 `caa26c090473b2e9008652d2ee67bb90226b493f392582d978a6ad71aaf37497`.
All times below are **2026-09-17 UTC+03**. Raw external traces, error records
and JSON remain private, outside Git/CI.

| Separate observation | Result |
| --- | --- |
| External wire observation, 05:50:51 | Authorized `cc-tool 0.26 --reset --log` recorded OUT `04`, exactly `4C 1D 22`, with no corresponding USB read before its next command. No flash/erase operation occurred. |
| Explicit replay, 05:57:15-05:57:25 | Fresh reset/config `26`/PC0 and all 8,979 physical CODE bytes checked first. Replay followed by RD_CONFIG returned `22`; fresh status `22`, PC0 and full CPU/FMAP were preserved. A separate successful explicit reset restored `26`. |
| Native API, 06:07:54-06:08:23 | Three independently full-CODE-verified own-reset cycles passed API `26 -> 22`, PC0/FMAP1 and full CPU preservation. Each second enable was rejected before any backend I/O. A separate final explicit reset restored `26`. |

An early, overly restrictive bank-zero assertion stopped **before any
configuration write**. Primary GET_BM facts and read-only FMAP observation
corrected that precondition: FMAP1 at PC0 is valid, not an execution-bank error.
Preservation checks were retained.

The **separate negative run, 06:09:34-06:09:45**, started after fresh reset and
complete CODE verification. The real three-byte write completed, then the
parent deliberately delayed its **host return by 1,100 ms against a 1,000-ms
operation deadline**. The API raised `USB operation deadline exceeded`,
entered FAULTED and returned no config result. The gate operation's
ten-I/O-boundary trace ended at the write: all five attempted subsequent
operations were rejected, with **zero target I/O after the late write**,
no retry, restore or reset.
After closing, a separately established read-only existing-session observation
found config `22`, PC0/FMAP1 and the full CPU unchanged. The accepted hardware
effect was **not API confirmation**. This was an injected host-completion
delay after a real USB write, not a physical USB timeout/stall or stuck-DMA test.

**Separate recovery, 06:10:14-06:10:26:** the unchanged FIFO runner explicitly
reset, required default config `26`, verified all 8,979 CODE bytes and passed one
full compiled FIFO cycle. That recovery ended at **FIFO READY `016A`, IRQs
disabled, XOSC32 selected and both FIFOs empty**, with no hardware process
remaining. The earlier [257-cycle FIFO record](#2026-09-17-lg-compiled-c-fifo-acceptance)
remains historical and unchanged.

The gate tests introduced no DMA-register access, DMA transfer, flashing or
RF activity. Evidence is **hardware-observed only for this debug-config gate**,
not DMA-controller copies or AES. Those gates remain open; this record grants
no new hardware authorization. See [source boundaries](PROVENANCE.md#m2-dma-debug-configuration-sources)
and [host coverage](VALIDATION.md#m2-dma-debug-gate-coverage).

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

The runner validates local board/image/compiler/hash metadata and binds the
immutable BIN bytes to the checked image's exact extent and SHA-256 before
loading USB. The same guard runs at `exercise` entry, before any adapter
observation or reset. Empty, truncated or replaced BINs are rejected even if
the build directory changes after artifact validation.
It then reset-attaches,
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

### Linux external-programmer no-run guard

`tools/cc_tool_no_run_guard.c` is an original BSD-3-Clause, Linux ELF
**per-process `LD_PRELOAD` fail-stop guard**, not a programmer or a successful
transfer substitute. It addresses the reviewed external `cc-tool` cleanup
hazard above, including read-only tasks and verification mismatches. Loading
the guard announces `cc-tool-no-run-guard: active` on stderr. Once loaded,
there is **no environment disable switch**.

The guard interposes `libusb_control_transfer` with this exact boundary:

- For request `C9` with the direction bit indicating OUT, only
  `bmRequestType=40`, `wValue=0`, `wIndex=1`, `data=NULL`, `wLength=0`
  is forwarded. This is the reviewed reset-into-debug shape, not permission
  to request it during offline work.
- Every other OUT `C9`, including normal-run reset `40/C9/0/0/NULL/0`,
  is blocked **before calling libusb**. The guard emits a blocked diagnostic,
  calls `fflush(NULL)` and then `_exit(86)`, bypassing normal process cleanup.
  It neither reports a successful transfer nor substitutes a debug reset.
- A missing downstream symbol or notice/output-flush failure causes
  `_exit(87)`, not success. Forwarded operations retain their original
  arguments and return values/errors, including unrelated IN `C9` requests.

**Exit86 proves neither programming success nor a halted target.** It only
identifies this process's intercepted request when accompanied by the expected
guard diagnostics. Exit87, absent/mismatched diagnostics or other outcomes
require investigation, not retry or an assumption of safety. Independently
compare physical readback and confirm halted state before accepting a guarded
operation. The guard does **not** intercept DEBUG RESUME/STEP bulk commands
or prevent an external programmer from executing its RAM helper. It therefore
does not prove that the CPU never executed code, that no earlier programming
error occurred, or that an unverified application cannot run through another
path. Full reset and full physical fixture CODE verification before the
runner's first resume remain mandatory.

First run the host-only synthetic test:

```sh
PYTHONPATH=tools python3 -B -m unittest test_cc_tool_no_run_guard -q
```

It compiles the guard and original fake USB library/driver in a temporary
directory, never opens real USB and never invokes the installed programmer.
There is no Makefile programming target, CI hardware hook or firmware/debugger
permission change. Any manually compiled guard/library and programmer logs
belong in a selected private location outside the repository; the guard must
be selected per process, not installed as a global preload. A constructor
notice alone is not proof that the intended external binary binds its calls
to the guard. Binary, loader and linkage changes require renewed checking;
no portable, static-binary or alternative-backend interception is claimed.

**2026-09-18 evidence boundary:** the parent reported six strict synthetic
tests passing, plus actual installed `/usr/bin/cc-tool` ELF symbol binding
checked with `LD_BIND_NOW`/`LD_DEBUG` and `--help` only, **without USB**.
The installed Ubuntu package is `0.27-1build5`, although its banner says
`0.26`; the binary/source identifiers and reviewed lifecycle are recorded in
[provenance](PROVENANCE.md#linux-external-programmer-no-run-guard-sources).
This is host/linkage evidence, not a guarded target experiment, successful
baseline backup, flash/readback, halted-state confirmation or RX acceptance.
The separately planned guarded private backup remains a separately authorized
hardware task; this record establishes none of its results.

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
observations passed. That run ended on the **FIFO fixture halted
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

The later [DMA-enable gate and separately reset one-cycle recovery](#2026-09-17-lg-dma-enable-gate-acceptance)
are separate observations; they do not replace this 257-cycle acceptance.

## Channel-0 DMA board fixture

`IMAGE=dma_fixture` is host-tested, image-checked and synthetically simulated
on both boards, with separate [bounded LG hardware acceptance below](#2026-09-17-lg-compiled-c-dma-acceptance).
Generic remains hardware-unobserved.
It links the published DMA/timebase/clock implementations unchanged; all twelve
older BINs remain byte-identical and exclude DMA. This is not AES, peripheral
DMA, interrupt dispatch, an allocator or a production integration. Never
program the standalone `dma_test.ihx`.

The real compiled-C sequence first selects/observes RC16. Each cycle copies
`(completed & 15)+1` bytes A-to-B at RC16, selects XOSC32 after DMA_OK, copies
16 bytes B-to-A, then selects RC16 and increments completed/M0 heartbeat only
after DMA_OK. Thus 257 cycles give **1,029 READY stages, 514 DMA calls and
6,289 verified transferred bytes**. There are 515 clock calls but only 514
command writes: initial RC16 selection is genuinely idempotent. Timeout is
1,024 raw ticks and the independent poll cap is 4,096; neither is calibrated
wall time.

Two explicitly allocated, persistent **volatile** buffers contain guard `69`,
16 data bytes and guard `96`. Source byte `i` is
`((completed + (31 for RC, 97 for XOSC)) & FF) XOR i` (hex constants).
Destination starts complemented. C verifies all 36 source/destination/tail/
guard bytes after acknowledged success; the runner independently reads them
back at successful copy checkpoints. It never reads payload after DMA error.
Buffers are refilled only after the preceding successful operation releases
them. The [terminal ownership contract](ARCHITECTURE.md#isolated-channel-0-dma-copy)
is unchanged: error is not quiescence, even if later bytes happen to match.
There is no abort, dummy copy, fault-latch clear, retry or error-time clock
switch. Original clock request/rollback diagnostics survive clock failure.

### DMA fixture ABI, allocation and linked checkpoints

The `M2DM` v1 record is **116 explicit wire bytes**, not a padded C diagnostic
structure. Its address comes from matching checked map/CDB symbols, not XDATA0.
Little-endian scalar arrays are serialized field by field.

| Offset | Fields |
| --- | --- |
| 0..9 | `M2DM`, version1, size116, phase (INIT1/RUNNING2/READY3/FAULT4), reason, stage0..4, completed modulo256 |
| 10..17 | Clock result, DMA result (`FF` not attempted), length, checked count, mismatch buffer/index (`FF` unset), actual/expected byte |
| 18..26 | Source16, destination16, timeout24, poll cap16; source/destination retain the last copy on clock-only stages |
| 27..45 | Serialized 19-byte clock request/rollback diagnostics |
| 46..64 | Serialized DMA elapsed32, polls16, helper status, actions, complete, verified, ARM/REQ/IRQ/IRCON/CFG0L/H/CFG1L/H, sample-valid |
| 65..80 | CMD/STA/SLEEPCMD, IEN0/1/2, eight controller bytes in the same order, later-snapshot validity, driver fault latch |
| 81..92 | Persistent descriptor snapshot, initial sleep/IRCON/CFG1L/H |
| 93..110 | Initial/current IP0, IP1, TCON, S0CON, S1CON, RFIRQF0/1, IRCON2, RFERRF |
| 111..115 | Guards `69 96`, three reserved zero bytes |

Reasons are entry1, phase2, clock3, DMA4, invariant5, bytes6. Driver actions
CONFIGURED1/ARMED2/REQUESTED4/ACKNOWLEDGED8, `complete`, `verified`, and both
sample-valid fields are distinct. Unread/stale bytes are not observations.
Later controller snapshots are sequential reads, not atomic DMA snapshots;
completion may occur between reads or after C returns. No partial-byte count
is invented.

Both layouts allocate descriptor `0045..004C`, DMA fault latch `004D`, private
fence `dma_reserved_end=0087`, record `0088..00FB`, buffer A `00FC..010D`,
buffer B `010E..011F`, work union `0120..0132`, and separately excluded
`__gptrput_PARM_2` scratch `0143`. The entire DMA/timebase/clock prefix is
protected. A's data `00FD..010C` naturally crosses `00FF->0100` in both
directions; no padding/pool forced this layout. Other physical boundaries
are not claimed. Ordinary XDATA is **324 bytes**, **356 used / 388 reserved**
including status; the full `1E00..1E3F` reservation and `1F00..1FFF` IRAM
alias are unchanged. Stack reservation is `69..FF` (151 bytes); synthetic
peak SP is `79`, below the unchanged `80..FF` guard.

| Linked location | Generic | LG |
| --- | --- | --- |
| BEFORE / READY / terminal FAULT | `0140 / 0142 / 0144` | `0168 / 016A / 016C` |
| Arm leaf / RET | `0A8F / 0A9B` | `0AB7 / 0AC3` |
| Arm call / third-poll call / DMAREQ write | `1445 / 1471 / 1481` | `146D / 1499 / 14A9` |
| Expiry helper / actual success RET | `0233 / 02DA` | `025B / 0302` |
| Nested returns: fixture / DMA poll / expiry caller | `20A5 / 1474 / 0E43` | `20CD / 149C / 0E6B` |

The 13-byte leaf is exactly `75 D6 01`, nine `00` NOPs, `22`.
[SWRU191F 8.1 p.93 and Table 2-3 p.39](PROVENANCE.md#m2-channel-0-dma-sources)
supply the >=9-system-clock fetch interval; s51 timing is not that proof.
Only reviewed CODE/private-data/parameter/IRAM/bit/split-pointer relocations
are normalized: the **2,867-byte DMA module still hashes to
`d5cc411d99fad73f654803263414deb629f988d95cc21dee9bd0f07ac54d9919`**.
Complete board CODE/constants/callers, clock/timebase helpers, byte ABIs and
allocation guards are independently checked and mutation-rejected.

| Board | CODE / BIN bytes | SHA-256 |
| --- | ---: | --- |
| generic | 8,850 | `f19112019fc6b3ab22d81ad4b8d4d1a5fd310a36ee38493cfcefa8ab2a8974a0` |
| lg_esl29_rev03 | 8,890 | `e18db3859aa23c673e9edb5770f65056efea8040986e560c49fcc9c88061094d` |

Offline lookup: `tools/debug_image.py dma-checkpoints` or `dma-state`, with
`--board`, `--image dma_fixture`, `--output` and, for state, `--hex`/`--snapshot`.

### Parent-only DMA acceptance procedure

After separate authorization, correct board programming and recovery review,
each invocation explicitly resets into halted PC0/config26, verifies **every
physical CODE byte**, then calls the published DMA-enable API with separate
DMA/reset permissions to establish22 **before first firmware resume or DMA
register access**. PC0 does not mean FMAP0; full CPU/FMAP preservation is
checked. The runner records requested/confirmed configuration and observed
STATUS separately. Older runners/helpers still require26. The new runner's
only extra debug-injected peripheral instructions are the eight read-only
controller SFR reads above, under config22 and full CPU preservation; generic
core access is not widened.

```sh
.venv/bin/python tools/check_dma_hardware.py --board lg_esl29_rev03 \
  --output build/lg_esl29_rev03/dma_fixture --bus BUS --address ADDRESS \
  --confirm-dma-test --cycles 257
```

A **separately authorized negative invocation** uses `--cycles 1 --induce-timeout`.
On the first XOSC32 copy it stops at the proved arm RET (nine NOPs executed),
replaces that breakpoint with the genuine 168-byte `timebase_expired` helper's
RET, and resumes. Before holding, it checks DPL/DPS=0, SP=`75`, all three
nested returns and seven saved registers, actual live copy/timeout/cap/
diagnostic arguments, descriptor, ARM1/REQ0/IRQ0 and preserved config/IRCON.
It verifies actual start/previous/deadline/now/helper-output arguments and
the cached **unexpired** boolean. Breakpoint I/O is not assumed fast enough:
already-expired or mismatched context fails closed, without hold or resume.

A bounded 0.25-second host hold changes no helper state. Resuming that real
RET uses its earlier sample and issues the real request once; the next poll
must produce **DMA_TIMEOUT8, polls4, actions7, no ACK/verified success** and
terminal FAULT. Actual elapsed must exceed1,024 and remain below half-range.
Driver complete may be0/1; later controller observations may be mixed/late.
This is a host-induced deadline experiment, not a physical stuck-DMA test.
No payload inspection or reuse follows failure.

After failed/late I/O only close is allowed: no restore/disable-DMA, abort,
reset, retry or clock cleanup. JSON success is emitted only after successful
resource cleanup. Recovery requires a **new, explicitly authorized full-reset
invocation**, never resetting C fields or continuing a halted failed copy.
Full-CODE-before-runner-resume does not establish that an external programmer
never executed its own normal-reset cleanup.

[Automated evidence](VALIDATION.md#m2-dma-board-fixture-coverage) is host,
linked-image and synthetic-only; the manual record below is separate.
The identical numeric READY address in earlier FIFO images is not proof of
image identity. Past acceptance grants no new hardware authorization; AES
and M2 #4 remain open.

### 2026-09-17 LG compiled-C DMA acceptance

The unchanged **8,890-byte LG image**, SHA-256
`e18db3859aa23c673e9edb5770f65056efea8040986e560c49fcc9c88061094d`,
passed the existing runner without firmware changes or weakened assertions.
The setup remained LG Rev0.3 / CC Debugger / macOS 15.7.9 / Python 3.11.9 /
PyUSB 1.3.1. The two private text-demo recovery copies and factory-page backup
were checksum-verified without modification. Explicit programming with actual
readback ran **09:16:47-09:16:55, 2026-09-17 UTC+03**.
Every acceptance invocation independently verified **all 8,890 physical CODE
bytes after its own reset**, then established config `26 -> 22` before any
firmware resume or DMA-register access; full reset CPU/FMAP preservation passed.

Three initial cycles, **09:17:27-09:17:41**, passed six copies and 54 bytes,
including both clocks/routes, source/destination guards and untouched tails.
The **separate negative run, 09:18:40-09:18:53**, stopped after the nine-NOP
arm leaf and at the actual expiry RET `0302`. DPL/DPS=0, SP=`75`, all three
return frames, live arguments and the persistent `010F -> 00FD`, length16
descriptor were checked. The raw start/previous/now/deadline values were
334936 / 334940 / 335038 / 335960: the captured third-poll sample was genuinely
unexpired, with ARM1/REQ0/IRQ0 before the hold.

After resumption, the real request was issued once and the next poll returned
**DMA_TIMEOUT=8**, 27,960 raw ticks / four polls / helper0. Actions were7,
complete1, verified0; both driver and later controller observations found
ARM0/REQ0/IRQ1, CFG0=`0045`, CFG1=`0000`, IRCON0. The hardware-completed
request was **not timely verified or acknowledged**. The fixture retained
fault latch8 and DMA reason4/stage3 at terminal FAULT `016C`, on XOSC32 with
IRQs disabled. No payload readback, refill, retry, acknowledgment or clock
cleanup followed failure. This is a host-induced deadline experiment, not
a physical stuck-DMA test or permission to reuse buffers after error.

A **separate explicit-reset recovery invocation** ran **09:20:00-09:24:03
UTC+03**, completing 257 cycles and 1,029 READY stages. Its observed bounds:

| Stage | Count | Raw elapsed ticks | Polls | Copy length / C-checked buffer bytes |
| --- | ---: | --- | ---: | --- |
| Initial RC16 selection, idempotent | 1 | 0 | 0 | 0 / 0 |
| RC16 A-to-B copy | 257 | 25..26 | 5 | All lengths 1..16 / 36 each |
| Select XOSC32 | 257 | 11..13 | 3 | 0 / 0 |
| XOSC32 B-to-A copy | 257 | 12..13 | 5 | 16 / 36 each |
| Select RC16, advance cycle/heartbeat | 257 | 2..3 | 1 | 0 / 0 |

The run confirmed **514 copies and 6,289 transferred bytes**. Real C checked
**18,504 source/destination/tail/guard bytes**, independently read back by the
runner at successful copy checkpoints. A's `00FD..010C` data crossed
`00FF -> 0100` as both source and destination; no other physical RAM boundary
is claimed. Completed/M0-heartbeat bytes wrapped to1. CPU/FMAP preservation,
immutable M0 and unrelated flag/priority/mask observations passed.

That **DMA run ended halted at READY `016A`,
debug config22, CMD/STA `C9` (RC16), all IRQ enables zero, ARM/REQ/DMAIRQ zero,
CFG0=`0045`, CFG1=`0000`, IRCON0 and fault latch0**. No hardware process
remained active. The 242.309-second host duration and raw whole-operation
counts are not calibrated timing or isolated DMA throughput measurements.
SP=`75` was physically checked at the negative checkpoint; synthetic peak
SP=`79` is not a measured hardware high-water mark.

All twelve older BINs and the published platform/debugger implementations
remain unchanged. Generic DMA hardware, other channels/triggers, variable/
word/repeated transfers, DMA interrupts, physical stuck/abort recovery, AES,
RF, flash and sleep/wake remain separate gates; full M2 #4 stays open.
Only processed observations are published. Raw logs, identities and recovery
material remain outside Git/CI; automated `hardware_tested=false` metadata
is not rewritten by this dated manual acceptance.

## AES/DMA board fixture

`IMAGE=aes_fixture` is a separate board image for both boards, not
`aes_test.ihx`. The corrected LG image has
[bounded hardware evidence](#2026-09-17-corrected-lg-aes-bounded-acceptance)
for short normal operation, both exact negatives and separately reset
257-cycle recovery; generic remains
host/image/synthetic-only. All fourteen earlier BINs and timebase/clock/DMA/
debugger implementations are unchanged. The
[first KEY failure](#2026-09-17-first-lg-aes-key-load-failure) remains root-cause
history. That successful 257-cycle recovery left the corrected **12,765-byte**
image halted at **READY016A/config22/RC16**. This is historical after the
[later PRNG programming and short acceptance](#2026-09-17-lg-prng-short-acceptance).

```sh
make BOARD=generic IMAGE=aes_fixture all test
make BOARD=lg_esl29_rev03 IMAGE=aes_fixture all test
```

Compiled C first selects RC16, then repeats AES_RC, clock_XOSC32, AES_XOSC32,
clock_RC16/heartbeat. After one full reset, 257 cycles give **1,029 READY
stages, 514 actual AES calls, 24,672 input and 8,224 output DMA bytes**,
2,056 individually delayed arms, 1,542 DMA acknowledgments and 1,542 ENC
acknowledgments. There are 515 clock calls, of which 514 switch source.
Only successful, fully drained AES releases ownership for the next operation.

The public CODE table has the five primary KATs and sixteen
[derived cases](PROVENANCE.md#m2-aes-board-fixture-sources). Vector is
`completed % 21`; space bits are `(completed/21)&3` at RC and one greater,
modulo four, at XOSC. Bit0 selects CODE key; bit1 selects CODE input; zero
selects XDATA. All 21 cases/four combinations/both clocks occur before the
byte counter wraps. Separate generic-pointer assignments preserve SDCC space
tags; a mixed CODE/XDATA ternary is not used. There is no target software
cipher, ciphertext-return shortcut or arbitrary-pointer test route.

Each call checks unchanged key/input, output guards `69/96`, and all sixteen
actual output bytes. Before the call, each output byte is the complement of
its expected answer, distinguishing stale/short publication at every position.
The C check and runner independently verify 50 caller bytes per accepted
block, **25,700 per 257-cycle run**. Inputs are public, never live/device keys.
On AES error, C retains the original result, checks only these separate caller
objects for unchanged sentinel/guards, and reaches stable FAULT without any
following clock/AES call, descriptor replacement, C fault clear or wipe.

### AES fixture ABI and linked proof

Both layouts allocate ordinary XDATA `0000..01B4` (437 bytes): **469 used /
501 reserved nonaliased bytes**, within the unchanged 512-byte budget.
The AES/clock/timebase private prefix is `0000..00FA`; DMA0 is `0045..004C`,
the real DMA1..4 table `004D..006C`, private key/IV/input/output
`006D..00AC`, and fault/used `00AD/00AE`. Caller state is `00FB..013A`,
key `013B..014A`, input `014B..015A`, guarded output `015B..016C`
(actual destination `015C..016B`), native union work `016D..0189`.
Remaining caller/runtime storage, including `__gptrput_PARM_2=01AF`, is
linker-accounted and excluded from DMA/caller overlap. No free-RAM pool,
status reservation or `1F00..1FFF` IRAM alias is used as separate storage.
Stack starts `6E`, reserves 146 bytes; synthetic peak SP is `7F`, not a
hardware high-water measurement. The strict upper-IRAM `80..FF` guard remains.
The correction adds two DATA scratch bytes, no XDATA or helper frame.

| Board | CODE bytes | Complete BIN SHA-256 |
| --- | ---: | --- |
| generic | 12,725 | `ce8bf5e85291c93901432612824ab428f0350baaa674d4d2939a6531f3313b92` |
| lg_esl29_rev03 | 12,765 | `0ee3e0946685c6fac10fbdd589ce75a6d2fbc14d4bad07e632faf0cd4f2d4997` |

The unchanged LG image has the bounded hardware evidence below, including
all 168 fixture vector/space/clock combinations and 257-cycle recovery.
Generic is not hardware-observed.
The explicit **64-byte `M2AE`, version2** wire record uses byte fields and
little-endian numeric serializers, never a native host union layout:

| Offsets | Meaning |
| --- | --- |
| 0..5 | Signature, version, size64 |
| 6..13 | Phase, reason, stage, completed, vector, space bits, result, kind |
| 14..19 | Checked count, mismatch buffer/index, actual/expected byte, AES fault latch |
| 20..24 | Timeout24=`32768`, poll limit16=`4096` |
| 25..43 | Full 19-byte clock request/rollback record, or compact AES projection |
| 44..50 | CLKCONCMD/STA, SLEEPCMD, IEN0/1/2, CPU-snapshot validity |
| 51..61 | Initial IP0/IP1/TCON/S1CON/RFIRQF0/RFIRQF1/IRCON2/RFERRF, IRCON, S0CON, SLEEPCMD |
| 62..63 | Record guards `69/96` |

Phase is INIT1/RUNNING2/READY3/FAULT4; kind is init0/clock1/AES2.
Reasons are entry1/phase2/clock3/AES4/invariant5/bytes6. ResultFF means
not attempted; mismatch buffer/indexFF mean unset. AES projection contains
elapsed32, polls16, helper status, phase, submitted/input-complete bitsets,
output-drained, published, arms, ACK-issued, DMA-acked bitset, ENC-ACK-issued,
ENC-acked, sample-valid and configured. Its omitted raw controller fields
remain in the **29-byte native caller-owned diagnostic**, which the runner
cross-checks for attempted AES calls, not resultFF/unattempted union contents.
Version2 changes ENC-ACK-issued to a count0..3 and ENC-acked to confirmed
KEY1/IV2/block4 bits (reachable masks0/1/3/7). Aggregates count set bits, not
the numeric mask: 514 successful blocks confirm 1,542 ENC acknowledgments.
CPU validity and AES sample validity are independent; retained
values are not fresh samples. Live runner register reads are separately
ordered observations, not an atomic controller snapshot or partial-byte count.
Runner totals distinguish command submissions, C-confirmed finite input bytes,
C-drained output bytes, issued/confirmed acknowledgments and actually published
blocks/bytes. A late-final timeout contributes a drained block, not a published
one; a post-error IRQ read does not invent additional C-confirmed byte counts.

`tools/debug_image.py aes-state` / `aes-checkpoints` require matching
`--board --image aes_fixture --output`; state additionally takes
`--hex`/`--snapshot`. Complete CODE/constants/caller rejection and decoded
relocation normalization prove the corrected **5,254-byte AES module**, SHA-256
`b80e064f5fb405c8a5d2c28722e18b51f25c5d99d90dd8aaec6da4332103befd`.
The original timebase reader, deadline/expiry and clock contracts remain
checked independently. Each arm is exactly MOV, nine NOPs, RET (13 bytes);
output and input are individually ready before KEY, and each subsequent
input arm has its own interval before IV/block start. The
[documented completed/drained history](ARCHITECTURE.md#isolated-aes-128-dma-block)
is required; rewriting ARM/descriptors cannot erase old missed triggers.

| Boundary | generic PC | LG PC |
| --- | --- | --- |
| BEFORE / READY / FAULT | `0140 / 0142 / 0144` | `0168 / 016A / 016C` |
| First input-arm RET | `0BFD` | `0C25` |
| Actual expiry RET | `02DA` | `0302` |
| Unique final S0CON-ack successor | `1DA3` | `1DCB` |
| Actual ST0 read, before latch | `014A` | `0172` |

### Parent-only AES acceptance procedure

After separate board/recovery/image review and authorization, program **only
the checked board `aes_fixture.hex` or `aes_fixture.bin`**, never the standalone
executable. The corrected LG programming below used the checked HEX.
Do not resume or rekey a failed image; recovery/programming belongs
to a separate full-reset operation. Review the corrected candidate hash and
version2 metadata, start with a short normal run, and stop on any KEY/IV/block
failure before attempting negatives or 257-cycle recovery.
An external programmer may execute its own normal-reset cleanup before this
runner attaches; the runner cannot retroactively verify CODE before that
cleanup. Every runner invocation itself owns reset PC0/config26, compares
**every physical CODE byte**, invokes the published explicit `26 -> 22` gate,
verifies full CPU/FMAP preservation, and only then resumes. Older runners
retain their own configuration policies; no global MMIO permission changed.

```sh
.venv/bin/python tools/check_aes_hardware.py --board lg_esl29_rev03 \
  --output build/lg_esl29_rev03/aes_fixture --bus BUS --address ADDRESS \
  --confirm-aes-test --cycles 257
```

The locally built `aes-reference` is required for independent public-corpus
verification before USB backend loading; it is not an uploaded artifact.
Choose each negative in a **separate invocation**, `--cycles 1`, with either
`--negative pre-key` or `--negative final`. Both select the first XOSC AES
call, after a successful RC block and clock switch:

- **Pre-key:** stop at the first input-arm RET, immediately move the breakpoint
  to the next actual expiry RET, and reject an already-expired cached sample.
  Validate phase1, CODE key/XDATA input tags, output/diagnostic addresses,
  timeout/cap, start/previous/deadline/now, helper pointers/result, DPL/DPS,
  both descriptors/controller and all **14 stack bytes at `1F6E..1F7B`**
  (SP=`7B`).
  Both nine-NOP paths have completed; configured3/arms2/polls4, no submission
  or acknowledgment. Hold there; the cached unexpired result may issue KEY,
  but the next real poll must return **AES_TIMEOUT8**, phase1/submitted1,
  polls5, no ACK/drain/publication. Input completion can be observed late.
- **Final:** stop at the unique S0CON-ack successor, immediately move to the
  next ST0 read **before latching final now**. Validate phase4, final-mode1,
  both real completions/drain, three confirmed DMA ACKs, three issued ENC ACKs,
  KEY/IV ENC confirmation mask3, cleared owned flags/control, live
  arguments/wait context and all **six stack bytes at `1F6E..1F73`**
  (SP=`73`, at least17 polls). The final sample has not yet read time;
  its prior elapsed value is retained, not a fresh now. Hold then let the
  actual read expire: require exact **AES_TIMEOUT8**, drained but unpublished,
  ENC-ACK-issued3/confirmed-mask3 (block ACK not confirmed). Never hold after an approved publication decision
  and falsely require the following fixed non-failing copy to return an error.

The bounded two-second host hold is only a stimulus, not a raw-tick conversion.
Actual elapsed must reach `32768` while remaining below half range. No assumption
that debugger I/O fits a deadline replaces the live frame/unexpired checks.
All checkpoints require config22. The fixture-only read surface is ENCCS,
S0CON, DMAARM/REQ/IRQ, IRCON, CFG0/1 and the documented clock/enables/flags;
every instruction preserves CPU/FMAP. **Never read ENCDO to inspect output.**
No CPU ENCDI/ENCDO transfer or generic MMIO debugger expansion is permitted.

After failure, private DMA staging may still change. Neither C nor the runner
inspects/reuses/wipes that payload; the caller output is disjoint and never
a DMA destination, so its unchanged sentinel/guards remain safe to check.
No failed/late I/O is followed by reset, abort, rekey, DMA disable, clock
cleanup, retry or further target access; only close is allowed. Errors are
terminal, not evidence of quiescence. Only a separately reset invocation
establishes recovery. Hardware key/IV and software copies/spills are not
securely erased; this is not key management.

**Evidence boundary:** the completed LG cases below establish short normal
operation, both exact timeout records and separately reset 257-cycle recovery:
514 accepted blocks across all 21 fixture vectors, four spaces and both clocks,
with fresh KEY/IV flags and per-phase ACK checks. This is bounded evidence on
one LG board, not generic hardware or arbitrary workloads. Generic s51 events
and the host oracle alone do not establish silicon behavior.
CPU-only AES pacing, calibrated timing,
CCM/authentication, key management/erasure, RF, flash, sleep and complete M2
remain outside this acceptance.

### 2026-09-17 first LG AES KEY-load failure

All times below are parent-reported **UTC+03**. The parent independently
passed both original board `all test` runs (415 Python tests, 87 compiled
fixture scenarios each, 138-file guard), with all fourteen older BINs unchanged.
Those tests modeled the wrong load-flag behavior and did not establish silicon.

Before programming, all 8,890 physical CODE bytes of the accepted DMA image
were verified at READY016A/config22 with CPU preserved; private recovery
backups were reconfirmed. At **13:11:36..13:11:46**, cc-tool0.26 erase/write/
actual readback all completed for the original **12,783-byte** LG AES image,
SHA-256 `97aed900b224a4a937b1b005af90646b5831c21cea853c29445fb0c96bb47d6e`.
Its exact image and seven metadata artifacts were preserved privately before
rebuilding; they are not imported into Git/CI.

The **13:12:16..13:12:17** runner attempt failed before USB: it expected a
guessed `75 92 00 E5 95` reader prefix. The actual linked prefix is
**`90 00 00 E5 95`**, MOV DPTR,#0000 followed by the ST0 latch read.
The parent's runner/synthetic-PROGRAM correction changed no firmware bytes.
Eleven runner tests and all six actual-board/mode preflights passed, including
actual DebugImage validation and rejection of the wrong prefix even with a
matching mutated hash. Those regressions remain required.

At **13:14:28..13:14:44**, normal3 verified every physical CODE byte, performed
the real reset26->22 gate, and passed initialization/initial RC READY.
The **first KEY load failed AES_STATE_CHANGED7**, not timeout or ciphertext
mismatch. The parent independently re-read this sanitized terminal C sample:

| Field | Observed value |
| --- | --- |
| elapsed / polls / timebase | 39 raw ticks / 5 / 0 |
| phase / submitted / input_complete | 1 / 1 / 1 |
| output_drained / published / fault | 0 / 0 / 7 |
| configured / arms / sample_valid | 3 / 2 / 3 |
| DMA ACK issued/confirmed; ENC ACK issued/confirmed | 0/0; 0/0 |
| DMAARM / DMAREQ / DMAIRQ / IRCON | `02 / 00 / 01 / 00` |
| ENCCS / S0CON; CFG0 / CFG1 | **`4C / 03`**; `0045 / 004D` |
| initial S0CON / IRCON / SLEEPCMD | `00 / 00 / 04` |
| CLKCONCMD/STA; IEN0/1/2 | `C9/C9`; all zero |
| initial IP0/IP1/TCON/S1CON/RFIRQF0/RFIRQF1/IRCON2/RFERRF | `00/00/05/00/00/00/00/00` |

C checked all50 caller bytes: key/input, sentinel output and guards remained
unchanged. **KEY completion set both ENCIF bits without output DMA completion**;
the old driver rejected that legitimate observed pair before either ACK.
SWRU191F15.8 describes block interrupts but does not exclude load interrupts.
The [corrected contract](ARCHITECTURE.md#isolated-aes-128-dma-block) requires and
acknowledges each command's fresh pair with finite DMA/control checks.
At this first attempt, IV pair3 remained unobserved; no IV or encrypted-block
success was inferred from KEY. Later corrected-image evidence is separate below.

That run ended at **old AES FAULT016C/config22, output channel1 armed02**,
CPU preserved. No ACK, reset, resume, rekey or private-staging inspection
followed the error within that failed invocation. The parent reconfirmed
FAULT016C/config22 before separately programming the corrected image below.
These processed first-failure facts remain root-cause evidence, **not
corrected-image hardware acceptance**; raw records, identities and recovery
material stay private.

### 2026-09-17 corrected LG AES bounded acceptance

These are parent-supplied sanitized observations, with times in **UTC+03**.
The same reviewed **5,254-byte** AES module,
SHA-256 `b80e064f5fb405c8a5d2c28722e18b51f25c5d99d90dd8aaec6da4332103befd`,
and **12,765-byte** LG board image,
SHA-256 `0ee3e0946685c6fac10fbdd589ce75a6d2fbc14d4bad07e632faf0cd4f2d4997`,
were used without implementation changes. Native diagnostics remain29 bytes,
wire ABI is M2AEv2, and memory/artifact limits are unchanged.

After confirming the old v1 image still at FAULT016C/config22, the parent
separately programmed the checked board **`aes_fixture.hex`** at
**14:10:31..14:10:42**; erase/write/actual readback all completed.
The external programmer's normal-reset caveat above still applies.
Each runner invocation below owned a new reset, compared all **12,765 physical
CODE bytes**, then performed the explicit **26->22** DMA gate with CPU/FMAP
preservation before resume. No physical identity or raw artifact is published.

| Completed case | Time window | Host duration, including preflight/reset/CODE |
| --- | --- | ---: |
| Normal3 | 14:11:02..14:11:21 | 18.963 s |
| Pre-key timeout, separate invocation | 14:12:04..14:12:24 | 19.779 s |
| Final-publication timeout, separate invocation | 14:12:41..14:13:01 | 19.880 s |
| Full-reset 257-cycle recovery | 14:15:30.376377..14:19:49.797773 | 259.4136599 s |

**Normal3:** six actual blocks produced **288 C-confirmed input bytes,
96 drained/published output bytes and 18 issued/confirmed ENC ACKs**.
The first three public vectors0/1/2 ran on both clocks, with RC space0
(XDATA key/input) and XOSC space1 (CODE key/XDATA input). C and the independent
host reference checked actual outputs. RC AES elapsed112..115 raw ticks;
XOSC AES elapsed57; all calls used17 polls. Both **KEY and IV fresh ENC pairs**
and their verified-clear acknowledgments are now hardware-observed through
the unchanged real C gate requiring pair3, finite input completion,
MODE/CMD/ST and retained output-channel ownership. This is not a claim for
untested vectors/spaces, calibrated time or general MCU validation.

**Pre-key negative:** the first XOSC call followed one successful RC block.
The actual input-arm RET `0C25` led to the genuine expiry RET `0302`, SP=`7B`;
all14 frame bytes `ec2a016d017480190173016d6a10` were verified.
Start=`477846`, previous=`477859`, cached now=`477951`, deadline=`510614`;
cached expired was false. At the stop, polls4/phase1/submitted0/input_complete0,
ARM3, control48 and clear flags established the pre-command context.
After the two-second hold, the real C call returned **AES_TIMEOUT8** with
elapsed **87,420 raw ticks**, polls5, phase1/submitted1/input_complete1,
arms2/configured3, all DMA/ENC ACK counts/masks0, drained0/published0.
All50 caller bytes were unchanged, with no mismatch; fault latch8 and
cpu_valid0 were retained at terminal FAULT016C.

**Final-publication negative:** the unique final ACK successor `1DCB` led to
the actual pre-latch ST0 read `0172` (reader prefix `90 00 00 E5 95`), SP=`73`;
all6 frame bytes `ec2af41dcc0f` were verified. Start=`484415`,
previous=`484469`, deadline=`517183`; final now was **not yet latched**.
At the stop, phase4/submitted7/input_complete7/drained1/published0/polls17
proved the late pre-publication context. The hold produced **AES_TIMEOUT8**,
elapsed **84,326 raw ticks**, polls17, arms4, DMA ACK issued3/confirmed-mask7,
ENC ACK issued3/confirmed-mask3: KEY/IV confirmed, block write issued but
unconfirmed. All50 caller bytes stayed unchanged, with terminal
FAULT016C/fault latch8/cpu_valid0. No caller output was published despite
completed output drain; the hold was before the final timing decision, not
after an approved non-failing copy.

| Live post-fault observation | Pre-key | Final publication |
| --- | --- | --- |
| CLKCONCMD/STA | `88/88` | `88/88` |
| ENCCS / S0CON | `4C / 03` | `48 / 00` |
| DMAARM / DMAREQ / DMAIRQ | `02 / 00 / 01` | `00 / 00 / 00` |
| IRCON; CFG0 / CFG1 | `00`; `0045 / 004D` | `00`; `0045 / 004D` |

These live observations are separate from retained diagnostics; cpu_valid0
does not denote a fresh C CPU snapshot. Neither negative performed private
payload inspection, rekey, post-failure ACK, clock cleanup or reset.
Each invocation's prior RC success published16 bytes, excluded from the
failing call's zero publication. The final invocation totals are96 input
bytes/32 drained bytes but only16 published bytes, with6 issued/5 confirmed
ENC ACKs. Error, drained output or clear post-error flags do not establish
quiescence or release private-buffer ownership.

**Separate full-reset recovery:** the final invocation returned 0 after
**257 same-reset-epoch cycles**. It again owned reset PC0/config26, compared
every physical CODE byte of the unchanged 12,765-byte image and performed the
explicit26->22 gate with CPU/FMAP preserved before the first resume.
The 259.4136599-second host duration includes offline preflight, reset,
full-CODE proof, gate and host inspection; it is not isolated AES performance.

| Recovery result | Observed total |
| --- | ---: |
| READY stages | 1,029 |
| Accepted and published blocks | 514 |
| KEY/IV/block commands | 1,542 |
| C-confirmed input DMA bytes | 24,672 |
| Drained and published output bytes | 8,224 |
| Total DMA bytes | 32,896 |
| Individually delayed channel arms | 2,056 |
| DMA phase ACKs, issued / confirmed | 1,542 / 1,542 |
| ENC ACKs, issued / confirmed | 1,542 / 1,542 |
| C caller-byte checks and independent readback | 25,700 |

The parent wrapper independently asserted the **set of all 168 combinations**:
21 public vectors x four CODE/XDATA spaces x two clocks, not merely an
aggregate call count. Each block checked all 50 caller bytes in C and by
readback. All 514 AES calls used 17 polls; RC whole-operation elapsed was
112..116 raw ticks and XOSC 56..58. Completed and M0 heartbeat bytes wrapped
to 1 after 257 cycles. These observations do not establish calibrated time
or AES throughput. The separate full reset established recovery; zero
observed DMA flags alone would not.

The **final snapshot of that AES recovery was the corrected fixture halted at
READY016A**, not either negative's FAULT stop. It is historical after the
[later PRNG programming](#2026-09-17-lg-prng-short-acceptance):

| Final field | Observed value |
| --- | --- |
| Wire / stage / completed / heartbeat | v2 / 4 (clock RC) / 1 / 1 |
| Debug config; CLKCONCMD/STA; SLEEPCMD | `22`; `C9/C9`; `04` |
| IEN0/1/2 | all zero |
| ENCCS / S0CON | `48 / 00` |
| DMAARM / DMAREQ / DMAIRQ / IRCON | all zero |
| CFG0 / CFG1 | `0045 / 004D` |
| IP0 / IP1; TCON; other retained flags | `00 / 00`; `05`; zero |
| AES fault latch | 0 |

The parent independently completed both corrected LG/generic serial `all test`
runs: 415 Python tests, 102 compiled fixture scenarios per board, actual-image
normal/pre-key/final prevalidation and unchanged MAC guards. The 138-file
repository guard passed, and all fourteen earlier BIN sizes and complete
SHA-256 values independently matched published baselines. The 5,254-byte
module and both corrected board identities are unchanged. Agent-run offline
counts remain recorded in
[VALIDATION.md](VALIDATION.md#m2-aes-board-fixture-offline-coverage).
No hosted-CI pass is inferred; the exact seven-file whitelist and automated
`hardware_tested=false` remain untouched.

Generic hardware, CPU-only pacing, key management/erasure, authentication/CCM,
networking, RF, general DMA, flash, sleep and full M2 #4 remain unvalidated/
out of scope. KEY/IV pair3 is observed on this LG; the primary manual has not
become more explicit about load interrupts. Raw records, execution artifacts,
identities and the original failed v1 image remain private and historical.

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

## Boot-disarmed flash board fixture

`IMAGE=flash_fixture` is a separately selected non-RF candidate for #8,
**offline-tested only on both board definitions**. The complete
[fixture/recovery contract](FLASH_FIXTURE.md) documents the boot-disarmed
ARM/RUN handshake, finite poll budgets, exact step/diagnostic ABI and hashes,
private full256-KiB/information backups and independent verification of all
excluded code/lock/config/information bytes **before destruction**.
No automatic destructive runner is supplied. Component test executables
remain forbidden as hardware input. Earlier LG/RX authority and evidence
do not carry over to flash; no flash-fixture physical record exists.

The [precise visibility blockers](FLASH_FIXTURE.md#debugger-visibility-precise-unresolved-blockers)
must not be worked around: mapped CODE/breakpoints exceed the debugger's
unbanked range; MEMCTR/FCTL are not in its permitted observation set, and
the ordinary XDATA/register-preserving DEBUG_INSTR path has not been validated
with XMAP/possibly busy flash. There is no explicit XMAP rejection in that
path, so API availability is not a safety proof. Successful common-C WAIT/END
and admission faults are distinct from mapping/service faults or RAM_STOP.
GET_PC/status alone do not establish safe return, contents or recovery.
No automatic reset/resume/reattach or protection relaxation is added.

Both flash-fixture physical gates remain open. A later task must identify
board/image, scratch page, verified private recovery material, destructive
scope and interruption/reset conditions, then record sanitized physical
observations separately from host, linked-image and simulator evidence.

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

## Deterministic PRNG board fixture

`IMAGE=prng_fixture` is a separate compiled-C acceptance image for both boards,
not the standalone `prng_test.ihx` and not an entropy/cryptographic RNG service.
It links the unchanged timebase/clock and1,043-byte PRNG driver with
foreground-owned ADC/PRNG/CSP history. The original wirev1 LG image has
[short hardware acceptance](#2026-09-17-lg-prng-short-acceptance) for
RC16/seed1234 only.
Its [first long run stopped on the fixture's STIF policy](#2026-09-17-lg-prng-long-run-flag-policy-interruption),
not a PRNG error; its last32 words were not host-accepted. That halt is now
historical. **LG has the unchanged7,289-byte wirev2 installed**, with
[corrected short hardware acceptance](#2026-09-17-corrected-lg-prng-short-acceptance):
eight RC16 words and six raw flag observations without a STIF transition.
The same image then passed
[full-stopped hardware acceptance](#2026-09-17-corrected-lg-prng-full-stopped-acceptance):
the complete both-clock corpus, actual C/live STIF race and preservation,
and genuine RCTRL11 rejection/re-entry. A
[separate full-reset recovery](#2026-09-17-corrected-lg-prng-full-reset-recovery-acceptance)
then repeated the entire corpus and observed a distinct `c-snapshot` STIF
transition. **The bounded LG short/stopped/reset-recovery gate is complete.**
Final LG is halted ENDREADY016A/config26/RC16, fault0 and C/live IRCON80,
with no probe execution or later resume. The stopped FAULT snapshot is
historical, not current. Generic remains hardware-unobserved.

| Board | CODE/BIN bytes | SHA-256 |
| --- | ---: | --- |
| generic | 7249 | `220da5183f9e11670bb094d14f079b06c6f94b40900abf63f83858e7cc45cf50` |
| lg_esl29_rev03 | 7289 | `b53ecd587fdd897287114fd8f4cee369af6f9388f79892cbb4b67d4729602c9f` |

The complete image/constants/caller proof rejects every CODE-byte mutation.
PRNG normalization proves byte-for-byte equivalence to standalone module
`df20f1945e7993fcfefab707ba75ebec8474a8f949857ef9bed3608679681954`,
including seed high/low writes, sole37 command, all reads, private operands
and two-byte XDATA pointer/limit/return ABI. Original timebase/clock proofs
remain in force. No production polynomial/model is linked.
The correction adds65 CODE bytes per board with no new linker-allocated RAM.

### PRNG wire and caller layout

All multibyte wire values below are explicitly serialized little-endian;
the native C clock diagnostic is never interpreted as a wire struct.

| XDATA | Object |
| --- | --- |
| `0000..0044` | Original timebase/clock private state and parameters |
| `0045..0062` | Complete PRNG private state/parameters/fence |
| `0063..00BA` | 88-byte `M2PN` wirev2 record |
| `00BB..00FE` | 68-byte caller: guards69/96,32 uint16 words at00BD, guardsA5/5A |
| `00FF..0100` | Separate caller probe sentinel9669 |
| `0101..0113` | 19-byte native clock diagnostic |
| `0114..0135` | Fixture counters/scratch/runtime allocation; gptrput parameter at0130 |
| `1E00..1E3F` | Unchanged full M0 status reservation,32 bytes used |

There are310 ordinary XDATA bytes,342 used including M0, and374 reserved
nonaliased bytes (limit512). XDATA1F00..1FFF is the IRAM alias, never a pool.
Stack starts4E/initialSP4D,178 bytes reserved; simulated peak61 preserves
upperIRAM80..FF. No caller buffer overlaps the private prefix or runtime scratch.

| Wire offset | Meaning |
| --- | --- |
| 0..5 | `M2PN`, version2, size88 |
| 6..15 | phase, reason, stage, run, modulo256 completed-batch counter, valid words, benign/batch result, normal seed result, benign-check mask, checked caller bytes |
| 16..25 | seed16, run index16, batch16, total words24, successful seed-load count |
| 26..30 | mismatch index (255 none,254 guards), actual16, expected16 |
| 31..39 | observed/initial ADCCON1, clock CMD/STA, SLEEPCMD, three IENs, initial sleep |
| 40..59 | initial and observed IP0/IP1/TCON/S0CON/S1CON/RFIRQF0/RFIRQF1/IRCON2/RFERRF/IRCON |
| 60..66 | repeated CPU-read state16, three probe return values, PRNG fault latch, clock result |
| 67..85 | serialized clock request/rollback diagnostics (19 bytes) |
| 86..87 | wire guards69/96 |

Phases are INIT1/RUNNING2/READY3/FAULT4. Stages are INITIAL0/BENIGN1/SEED2/
BATCH3/CLOCK4/END5/PROBE6. Reasons0..7 are none, entry, phase, clock error,
PRNG error, invariant, bytes, expected stopped rejection. Result255 means
not attempted; benign mask15 proves real NOT_SEEDED, limit0, seed0000 and
seed8003 checks without MMIO effects or publication. `count` is valid only
for its BATCH record; seed READY clears it. No PRNG poll count is invented:
limit16 bounds each call, and private loop scratch is not a batch diagnostic.

**Wirev2 sticky-flag policy:** all initial flags are immutable. C retains its
previous raw IRCON observation before the next snapshot; only bit7/STIF may
rise0->1, never fall1->0. Every other IRCON bit and all nine other flag bytes
remain exact. All IENs stay zero and full-reset/no-ISR/no-other-writer history
is required. STIF is not cleared; ST0/1/2 and compare are not written.
The current decoder/runner intentionally reject wirev1 rather than silently
applying this new contract to old CODE.

The host checks ordered initial -> C -> live -> next-C observations, including
INIT, probe and final FAULT. A same-checkpoint C0/live1 race is allowed; a later
C0 after a prior live1 is rejected even if live is again1. Raw C bytes are
never rewritten to the live value. `flag_history` reports immutable initial,
last C and last live flag arrays, observation count, at most one
`stif_transition` (`c-snapshot` or `live-after-c`, checkpoint/run/index/total
and before/C/live IRCON), and `observations_after_transition`. Initially set
STIF does not imply that its assertion was observed. This bounded record is
not a timer/event/wrap counter, elapsed threshold or calibrated clock result.

Runs0/3 use seed1234 on RC16/XOSC32: four words, explicit reseed, same four
words. Runs1/2 and4/5 use seeds1/3 for **32,767 consecutive calls each**.
Each period has1,023 full32-word batches plus a real31-word tail; unfilled
words remain9669. There is no reseed within a period. C compares every
returned word to two non-advancing RNDL/RNDH read pairs, plus public prefixes,
caller guards and tails. Host checks every word against independent math,
first return exactly at32,767, all other states unique, and all65,534 valid
states covered per clock. This is not a65,535-state period or randomness quality.
Full acceptance has4,111 READY stages,4,100 batches,131,084 words,8 seed
loads (16 RNDL writes),131,084 one-step commands and two clock transitions.
The heartbeat/completed byte ends at4 after repeated wrap. ENDREADY restores
RC16 and precedes the deliberate probe.

### Parent-only PRNG acceptance procedure

These are **manual examples, not development commands or authorization**.
Only the separately authorized hardware operator may select/program a board
or invoke the runner. Establish the exact board/image, independent access/reset
permission and recovery conditions first. Preserve existing private recovery
material outside Git/CI. Program only the checked board HEX or BIN; never
`prng_test.ihx`. An external programmer may normally reset/run the target
before this runner attaches; that is not a checked acceptance epoch.

```sh
python3 tools/check_prng_hardware.py --board lg_esl29_rev03 \
  --output build/lg_esl29_rev03/prng_fixture --bus <bus> --address <address> \
  --mode short --confirm-prng-test
```

Each invocation performs its **own full reset**, checks PC0/config26, compares
every physical CODE byte and proves preserved complete CPU/FMAP before its
first resume. It does not change debug configuration, enable DMA or access
DMA registers. Full-reset/no-other-code history establishes no ADC conversion,
queued single conversion, CRC/RF/CSP consumption or ownership handoff.
ST0 alone cannot prove that history. Live observations are limited to the
fixed non-mutating SFR set; no ADCH read, ADCCON3 write, RNDH write or RF command
is allowed. INIT requires stable RC16, IRQs0 and ADCCON1 STSEL11/ST0/RCTRL00.

`short` stops after five READY stages: both seed1234 loads and8 actual RC16
words. Review this small initial gate before separately invoking `--mode full`.
Full mode verifies the complete finite corpus and stops at ENDREADY; it does
not execute the following probe. CPU halting cannot honestly create a PRNG
poll timeout, so no hold/timeout option exists.

For the separately authorized terminal gate, invoke `--mode stopped`. It
independently resets/verifies CODE and runs the **entire** corpus before
explicitly resuming ENDREADY through fixture-only ADCCON1=3F (RCTRL11/off,
ST0/STSEL11/reserved11, read-only EOC preserved). This is an intentional
precondition violation **before** the driver call, not error-path recovery.

| Exact context | generic | LG |
| --- | --- | --- |
| BEFORE / READY / FAULT | `0140 / 0142 / 0144` | `0168 / 016A / 016C` |
| PRNG module | `09FB..0E0D` | `0A23..0E35` |
| Raw snapshot / previous-IRCON check | `0E5E / 0ECD` | `0E86 / 0EF5` |
| Intentional3F write | `16A7` | `16CF` |
| First genuine next16 LCALL | `16B3` | `16DB` |
| Complete caller frame at IRAM4E..4F | `4D 1A` | `75 1A` |

At that LCALL: SP4F, DPS0, active bank0, DPTR0=00FF, XDATA005C=16,
sentinel9669, live ADCCON1=3F (BF only in an EOC1 synthetic setup), hardware
state0003, no prior PRNG fault, wire RUNNING/PROBE and total131,084.
The runner checks all frame bytes/arguments and preserves CPU context before
executing the actual call. Expected terminal FAULT has reason7, returns6/6/6,
fault latch6 and unchanged sentinel/full caller batch. The first real call
rejects unsupported state; the next next16 and seed entries return the retained
fault without MMIO. No subsequent clock cleanup, reseed, storage clear, reset
or resume occurs. USB failure/late effect is terminal; success JSON is emitted
only after successful session cleanup.

Recovery acceptance is a **new, separate `--mode full` invocation** with its
own full reset/CODE proof, never continuing the stopped or failed invocation.
Generic physical behavior, EOC1 preservation, physical stuck/late/poll-limit
faults remain unobserved; calibrated timing is not established. No entropy, RF/noise seeding,
ADC/CRC/DMA/AES/flash/sleep/ISR service, key management or M2 #4 closure follows.

### 2026-09-17 LG PRNG short acceptance

**Hardware-observed original wirev1 short case; no correction was needed for that case.**
The parent supplied the sanitized facts below. Raw execution records, recovery
images, checksums of private backups and adapter identity remain private and
are not opened or copied into Git/CI.

Before programming, the parent reconfirmed private recovery material: two
equal 262,144-byte recovery images and the 2,048-byte factory page, with all
checksums matching. All 12,765 physical CODE bytes of the prior accepted
AES image (`0ee3e094...`) was independently verified at READY016A/config22,
wirev2 stage4/completed1/fault0, with full CPU preservation. That AES state
is historical, not the currently installed image.

The parent verified the actual LG `DebugImage` and all three
`validate_program` modes before programming. On **2026-09-17, UTC+03**,
cc-tool0.26 programmed the checked **LG `prng_fixture.hex`** at
**16:49:54-16:50:00**: erase, write and actual readback all Completed.
The image is 7,224 bytes, SHA-256
`aac5793818df6c2f4695d08d33cc2ea5e599945b4472e2b31160caf4e0bda0f7`.
At that programming, the generic 7,184-byte image (`75ec8580...`), original
1,043-byte driver (`df20f194...`), standalone test and all sixteen older BIN
identities matched the reviewed originals. The external-programmer
normal-reset caveat still applies; only
the runner's own reset/CODE proof establishes this acceptance epoch.

**Short mode passed first try**, from
`2026-09-17T16:50:09.678904+03:00` to
`2026-09-17T16:50:20.271172+03:00`. The runner reported 10.592902 host
seconds including offline preflight, reset, full CODE proof and inspection;
this is not isolated PRNG performance. It proved own resetPC0/config26,
all 7,224 physical CODE bytes and preserved complete CPU/FMAP (FMAP1) before
first resume. There was no DMA enable or debug-configuration write.

Five READY stages passed actual NOT_SEEDED5, zero-limit/status2 and
invalid seeds0000/8003/status1 checks, with unchanged RND state, control and
sentinel and benign mask15 (`0x0F`). Two explicit seed1234 loads produced eight
actual RC16 words: **`8D94 E5AC CBBE 1731`**, repeated after reseeding.
The unchanged C driver output and two additional non-advancing RNDL/H read
pairs per word agreed with C prefix checks and independent host mathematics.
Each batch had four valid words, 28 unfilled9669 slots, before guards69/96,
after guardsA5/5A and checked68.
Register, seed, word and guard values are hexadecimal; counts/statuses are
decimal. Flags below are IP0/IP1/TCON/S0CON/S1CON/RFIRQF0/RFIRQF1/IRCON2/
RFERRF/IRCON in wire order.

| Completed short snapshot (historical) | Value |
| --- | --- |
| Wire/phase/stage/run | v1 / READY3 / BATCH3 / 0 |
| Seed/index/batch/total/seedcalls | 1234 / 8 / 2 / 8 / 2 |
| Completed / M0 heartbeat | 2 / 2 |
| Result / seed result / fault latch | 0 / 0 / 0 |
| ADCCON1 / initial ADCCON1 | 33 / 33 |
| CLKCMD / CLKSTA / SLEEPCMD | C9 / C9 / 04 |
| IEN0/1/2 | 0 / 0 / 0 |
| Initial and observed flags in wire order | `[0,0,5,0,0,0,0,0,0,0]` |
| Repeated hardware state / clock result | 1731 / NOT_ATTEMPTED8 |

The subsequent separately reset `--mode stopped` invocation was interrupted
as recorded below, before full-corpus/probe acceptance. This short snapshot
is historical, not the current halt. Corrected short acceptance subsequently
passed as recorded below, followed by full-stopped acceptance and a separate
full-reset recovery. Those later results do not expand this short case's scope.

The parent independently completed both **original-image** `all test` runs:
429 Python tests,33 genuine continuation segments/full131,084 synthetic
words, probe and9 faults per board,150-file guard and all sixteen older
BIN identities. Those models lacked STIF arrival. No hosted-CI pass is claimed;
the eighteen-job/seven-artifact policy and `hardware_tested=false` stay unchanged.
Generic and EOC1 remain physically unobserved. This short RC16/seed1234
case establishes no entropy/security randomness, ADC conversion, RF/noise
seeding, CSP coexistence, physical poll-timeout, calibrated timing or full
M2 #4 acceptance.

### 2026-09-17 LG PRNG long-run flag-policy interruption

**Fixture flag-policy failure, not a PRNG driver/output failure or accepted
full corpus.** The parent supplied only these sanitized facts; the old image,
seven debug metadata files, raw records and device identity remain private.

The original LG wirev1 image was still7,224 bytes, SHA-256
`aac5793818df6c2f4695d08d33cc2ea5e599945b4472e2b31160caf4e0bda0f7`.
The separately reset `--mode stopped` invocation began
`2026-09-17T16:52:04.964618+03:00` and ended
`2026-09-17T17:01:22.257924+03:00`, reporting557.307896 host seconds and
exit1: **`PRNG live shared-state differs from C observation`**.
The last32-word batch was **not** accepted: the flag check preceded caller
buffer reading and `Sequence` comparison. C total62535 is not an independently
checked full-corpus count. No full-corpus, stopped-probe or recovery acceptance
followed, and the duration is not PRNG performance or timer calibration.

A separate parent read-only observation preserved complete CPU context and
performed no reset, resume or IRCON clear. This **historical wirev1 halt**
was neither the earlier short READY snapshot nor ENDREADY; subsequent
wirev2 programming below replaced that installed image:

| Field | Observed original-image value |
| --- | --- |
| PC / debug configuration | READY016A / 26 |
| Wire / phase / reason / stage / run | v1 / READY3 / 0 / BATCH3 / 2 (RC16 seed3) |
| Completed / count / checked / benign mask | 164 / 32 / 68 / 15 |
| Seed / index / batch / C total / seedcalls | 0003 / 29760 / 930 / 62535 / 4 |
| Result / seed result / mismatch | 0 / 0 / 255 |
| PRNG fault latch / probe returns | 0 / 0,0,0 |
| Initial and latest C flags, in wire order | `[0,0,5,0,0,0,0,0,0,0]` |
| Live flags | Same except IRCON00->80 |
| ADCCON1 / CLKCMD / CLKSTA / SLEEPCMD / IEN0/1/2 | 33 / C9 / C9 / 04 / 0,0,0 |
| C hardware state / live RNDL,RNDH | F3B8 / B8,F3 |

The parent directly reread **SWRU191F p.47 (IRCON bit7/STIF, R/W) and
11.1-11.2 p.129**: the Sleep Timer starts immediately after reset and its
default compare is **FFFFFF, not zero**. Compare can latch STIF with all
interrupt enables zero. No compare write was performed; the debugger-paced
run naturally crossed that default compare after C's last flag snapshot.
This is not ADC/PRNG entropy, ISR activity, `PRNG_STATE_CHANGED` or timeout.
The sticky flag does not identify an exact event count, wrap count or tick rate.

The bounded wirev2 correction above preserves that assertion and rejects
deassertion/every other flag change, without clearing flags, moving compare,
writing ST0/1/2, enabling interrupts, shortening the corpus or resetting
between period chunks. Exact CODE, relocation, raw-read/previous-snapshot,
ABI and probe-frame proofs are updated; the1,043-byte PRNG and original
clock/timebase modules, all sixteen older board BINs and memory limits are
unchanged. Host/synthetic tests now actually inject the assertion and its
C/live race instead of freezing all flags.

At that interruption, no corrected image had been programmed or accepted.
The parent subsequently programmed the checked wirev2 board HEX and passed
the corrected short, full-stopped and distinct full-reset recovery cases below.
Do not resume or reinterpret the old wirev1 halt with the new runner.
Generic, EOC1, physical poll faults, entropy/RF-noise seeding and full M2 #4 acceptance
remain open. No hardware access, private-record
inspection or automatic recovery is part of this correction.

### 2026-09-17 corrected LG PRNG short acceptance

**Hardware-observed corrected short case only, on unchanged reviewed source
and proofs.** These are parent-supplied sanitized facts from 2026-09-17,
UTC+03; raw logs, recovery backups and identities remain private.

Before programming, the parent reverified recovery backups and all 7,224
physical CODE bytes of the old wirev1 image at READY016A/config26,
C IRCON00/live80 and RND state F3B8. Complete CPU context was preserved,
with no reset, resume or flag clear. That old image/halt is now historical.

cc-tool0.26 programmed the checked LG **`prng_fixture.hex`**, 7,289 bytes,
SHA-256 `b53ecd587fdd897287114fd8f4cee369af6f9388f79892cbb4b67d4729602c9f`,
from `2026-09-17T18:14:16.112468+03:00` to
`2026-09-17T18:14:22.484261+03:00` (reported 6.372 host seconds).
Erase, write and actual readback each reported Completed. The external
programmer's normal-reset caveat is unchanged: only the runner's own
reset/CODE proof establishes the checked acceptance epoch.

**Corrected short mode passed first try**, from
`2026-09-17T18:14:47.964107+03:00` to
`2026-09-17T18:14:58.592625+03:00` (reported 10.628710 host seconds).
It proved own resetPC0/config26, all 7,289 physical CODE bytes and complete
CPU/FMAP1 preservation. Five READY stages accepted eight actual RC16 words:
`8D94 E5AC CBBE 1731` twice after two explicit seed1234 loads.
Benign mask15 and caller guards/unfilled tails passed, checked68.
Neither reported duration is isolated PRNG performance or calibrated time.

| Completed corrected-short snapshot (historical) | Value |
| --- | --- |
| PC / wire / phase / stage / run | READY016A / v2 / READY3 / BATCH3 / 0 |
| Total / index / batch / completed | 8 / 8 / 2 / 2 |
| Seed / hardware state | 4660 (`0x1234`) / `0x1731` |
| Result / seed result / fault latch | 0 / 0 / 0 |
| CLKCMD / CLKSTA / SLEEPCMD | C9 / C9 / 04 |
| Initial/current ADCCON1 / IEN0/1/2 | 33/33 / 0,0,0 |
| Raw initial, C and live flags, in wire order | `[0,0,5,0,0,0,0,0,0,0]` |
| Flag history | Six observations; no STIF transition |

The parent independently reverified all sixteen published BIN sizes/full
SHA-256 values and both v2 `DebugImage` plus all three `validate_program`
modes before programming. **Both independent corrected-v2 LG/generic
`all test` runs completed with exit0.** Each passed434 Python tests
(LG54.932 seconds, generic57.771 seconds for the Python suites, not complete
`all test` duration or hardware timing),33 genuine continuations,4,111 READY
checkpoints,131,084 actual simulated words/four32,767 periods, STIF C/live
race plus3,985 preserved observations,16 edge words and probe plus9 driver/
81 flag faults. PeakSP61, MAC alias and150-file repository/local-link checks
passed, as did the parent's `git diff --check`. Generated
`hardware_tested=false` was verified. Seven exact corrected LG artifacts were
preserved privately. These independent results remain separate from the
earlier implementation runs; no hosted-CI result is claimed.

The short READY016A snapshot is historical. The separately reset
`--mode stopped` invocation, first confirmed active3m11s after its start,
subsequently passed as recorded below; no other USB session was opened during
that run. Distinct full-reset recovery subsequently passed as recorded below.
No entropy/security randomness or wider M2 #4 acceptance follows;
the eighteen-job/seven-artifact
policy and `hardware_tested=false` are unchanged.

### 2026-09-17 corrected LG PRNG full-stopped acceptance

**Hardware-observed complete corpus, real STIF race and genuine stopped
probe.** Recovery was a separate later invocation recorded below. These are
parent-supplied sanitized facts; no private logs, artifacts or identities
are opened or copied here.
The source, proofs and installed LG image were unchanged: 7,289 bytes,
SHA-256 `b53ecd587fdd897287114fd8f4cee369af6f9388f79892cbb4b67d4729602c9f`.

`--mode stopped` ran from `2026-09-17T18:15:37.461786+03:00` to
`2026-09-17T18:34:31.139137+03:00`, returning0 and reporting
**1133.646915744 host seconds**. Its own reset proved PC0/config26;
all 7,289 physical CODE bytes were verified before first resume with complete
CPU/FMAP preservation and preflight FMAP1. The duration includes debugger
pacing and inspection, not isolated PRNG performance or calibrated time.

The run accepted **4,111 READY stages and 131,084 individually host-checked
words**, four complete 32,767 periods and all 65,534 valid states per clock
(RC16 and XOSC32), 8 explicit seed loads and heartbeat4. There was no
midperiod reset or reseed inside a period. This is not a 65,535-state period
or an entropy/security-randomness result.

**Actual after-C/live STIF race:** initial flags, in wire order, were
`[0,0,5,0,0,0,0,0,0,0]`. The retained transition was:

| Transition field | Observed value |
| --- | --- |
| History observation / phase / stage / run | 1969 / READY3 / BATCH3 / 2 |
| Seed / clock / index / total | 0003 / RC16 / 29984 / 62759 |
| Previous IRCON / raw C IRCON / live IRCON | 0 / 0 / 128 (`0x80`) |
| Source | `live-after-c` |

There were 4,114 total observations and **2,145 subsequent preserved
observations**, through XOSC32, END and probe. Final C/live IRCON were both128
and all other flags remained unchanged. The transition record retained raw
C IRCON0 rather than replacing it with live128.
No STIF clear or write occurred. This proves the bounded race handling and
continued preservation, not timer wrap/event counts or frequency.

**Genuine deliberate probe context:** PC5851 (`0x16DB`), SP79 (`0x4F`),
complete frame bytes **`75 1A`**, caller output255 (`0x00FF`), limit16,
control63 (`0x3F`), RND state0003 and fault-before-call0. This was the actual
compiled path after verified idle END, including fixture-only RCTRL11/off
before the real API call, not a forced return or host PC shortcut.

The resulting terminal halt was **FAULT016C/config26**. This is a completed
historical snapshot, not the final state after the separate recovery:
register/word values below are hexadecimal; counts/status codes are decimal.

| Terminal wirev2 field | Observed value |
| --- | --- |
| Phase / reason / stage / run | FAULT4 / expected-stop7 / PROBE6 / 5 |
| Total / index / batch / seed / seedcalls | 131084 / 32767 / 1024 / 0003 / 8 |
| Completed / heartbeat / count / checked / benign | 4 / 4 / 31 / 68 / 15 |
| Mismatch / actual / expected | 255 / 0 / 0 |
| Normal batch result / normal seed result | 0 / 0 |
| Actual probe returns / PRNG fault latch | 6,6,6 / 6 |
| Current / initial ADCCON1 | 3F / 33 |
| CLKCMD / CLKSTA / SLEEPCMD / IEN0/1/2 | C9 / C9 / 04 / 0,0,0 |
| C/live IRCON / repeated hardware state | 80/80 / 0003 |

The full caller batch and distinct probe9669 sentinel remained unchanged
across rejection and retained-fault re-entry; complete CPU context was
preserved. No cleanup or automatic recovery followed the expected error.
The retained return-to-RC clock diagnostic was **2 raw ticks,1 poll,
timebase status0, rollback NOT_ATTEMPTED8**. Earlier XOSC timing was not
retained and is not inferred from this final diagnostic.

The stopped invocation was closed before the parent began the distinct
`--mode full` recovery recorded below. Its own full reset/CODE proof and
complete corpus establish recovery independently; this stopped run alone
did not do so. The installed wirev2 image and its hash were unchanged.
Do not describe the completed stopped FAULT as the final target state.

Generic hardware, EOC1, physical stuck/late/poll-cap faults, entropy/RF/
security/sleep and broader M2 #4 remain open. Both parent434-Python/offline
corpora, memory/alias proofs and sixteen older BIN identities remain as
confirmed above. No code/test/build change or hosted-CI pass is implied;
the eighteen-job/seven-artifact policy and `hardware_tested=false` are unchanged.

### 2026-09-17 corrected LG PRNG full-reset recovery acceptance

**Hardware-observed separate full-reset recovery passed, completing the
bounded LG short/stopped/reset-recovery gate.** These are parent-supplied
sanitized facts, not observations made by the documentation updater.
The source, proofs and installed LG wirev2 image were unchanged: **7,289 bytes**,
SHA-256 `b53ecd587fdd897287114fd8f4cee369af6f9388f79892cbb4b67d4729602c9f`.
Raw logs, seven exact image artifacts, recovery backups and identities remain
private; no private records were opened or copied into Git/CI.

After the stopped invocation had closed, a **separate `--mode full`** ran
from `2026-09-17T18:35:26.479462+03:00` to
`2026-09-17T18:54:56.587890+03:00`, returning0 and reporting
**1170.138642461 host seconds**. Its own full reset proved PC0/config26;
every physical CODE byte was verified, with complete CPU/FMAP1 preservation,
before first resume. The duration includes debugger pacing and inspection,
not isolated PRNG throughput, a tick-rate measurement or calibration.

The new reset epoch again accepted **4,111 READY checkpoints and 131,084
individually host-checked words**: four complete 32,767 periods, all 65,534
valid states per clock, 8 explicit seed loads and heartbeat4. There was no
midperiod reset or reseed inside a period. The run stopped at ENDREADY before
the deliberate RCTRL11 path: `probe_context` was null and no probe executed.
Fault, counter and flag history began afresh through the full reset; this was
not an automatic recovery, cleared C state or continuation of the stopped run.

**Distinct C-observed STIF transition:** initial flags, in wire order, were
`[0,0,5,0,0,0,0,0,0,0]`. The retained transition was:

| Transition field | Observed value |
| --- | --- |
| History observation / phase / stage / run | 1859 / READY3 / BATCH3 / 2 |
| Seed / clock / index / total | 0003 / RC16 / 26464 / 59239 |
| Previous IRCON / raw C IRCON / live IRCON | 0 / 128 (`0x80`) / 128 (`0x80`) |
| Source | `c-snapshot` |

There were **4,112 total observations and 2,253 subsequent preserved
observations**. Final raw C/live flags were both
`[0,0,5,0,0,0,0,0,0,128]`; no other flag changed and no flag clear or
compare write occurred. This C-observed assertion is distinct from the
stopped run's observation1969, raw C0/live128 `live-after-c` race and2,145
subsequent preserved observations. Neither history counts timer events/wraps
or establishes frequency.

**Final physical LG state, at the end of this recovery on 2026-09-17: halted
ENDREADY016A/config26 on the same installed wirev2 image.** No resume followed
ENDREADY. Register/word values below are hexadecimal; counts/statuses are decimal.

| Final recovery field | Observed value |
| --- | --- |
| Wire / phase / reason / stage / run | v2 / READY3 / 0 / END5 / 5 |
| Total / index / batch / seed / seedcalls | 131084 / 32767 / 1024 / 0003 / 8 |
| Completed / heartbeat / count / checked / benign | 4 / 4 / 31 / 68 / 15 |
| Normal result / seed result / PRNG fault latch | 0 / 0 / 0 |
| Probe returns / probe context | 0,0,0 / null (not executed) |
| Current / initial ADCCON1 | 33 / 33 |
| CLKCMD / CLKSTA / SLEEPCMD / IEN0/1/2 | C9 / C9 / 04 / 0,0,0 |
| C/live IRCON / repeated hardware state | 80/80 / 0003 |

The retained return-to-RC clock diagnostic was **2 raw ticks,1 poll,
timebase status0, rollback NOT_ATTEMPTED8**. No earlier XOSC timing is inferred.
The prior short, interrupted wirev1 and stopped FAULT snapshots remain dated
historical evidence, not the final current halt.

No further local hardware work is planned. Moving the board to
Ubuntu24/MacPro6.1 is a handoff, not an acceptance result or permission to
operate hardware there. Generic hardware, EOC1 preservation, physical
stuck/late/poll-cap faults, entropy/RF/security/sleep and broader M2 #4
remain open. Both parent434-Python/offline corpora,33 genuine continuations,
memory/alias proofs,150-file guard and all sixteen older BIN identities
remain as confirmed above. No code/test/build change or hosted-CI pass follows;
the eighteen-job/seven-artifact policy and `hardware_tested=false` are unchanged.

## Bounded passive RX board fixture

The separately implemented passive RX board fixture is RF-capable; it is not
covered by earlier non-RF fixture hardware evidence. See the
[bounded contract](RADIO_RX.md#bounded-passive-rx-board-fixture) for exact
hashes, allocation and checkpoint addresses.

### Parent-only passive RX acceptance

**Bounded LG acceptance is recorded separately
[below](#2026-09-18-lg-bounded-passive-rx-acceptance).** The earlier-image
[failure/probe](#2026-09-18-lg-rx-fscal1-failure-and-probe) is not successful reception.
This procedure is not
authorization to use hardware during offline development. The parent must
separately establish owned CC2530F256 board/revision/image, safe supply/pin
policy, private verified recovery backups, exclusive adapter access and the
authorized operations. External programming remains separate; never flash
`radio_rx_test.ihx`. Programmer cleanup can reset/resume even after a failed
verification: use the separately checked
[Linux external-programmer gate](#linux-external-programmer-no-run-guard)
where applicable, and independently verify complete physical readback and
halted state. Guard exit86 is not success or halt evidence and does not block
RESUME/STEP or programmer RAM-helper execution. Retain the existing lifecycle
safeguards and the runner's own full-reset/full-CODE-before-resume checks.

Only after separately authorized programming, the manual invocation shape is:

```text
python3 tools/check_radio_rx_hardware.py --board lg_esl29_rev03 --output build/lg_esl29_rev03/radio_rx_fixture --bus BUS --address ADDRESS --attempts 16 --capture /PRIVATE-0700-DIRECTORY/new-rx-records --confirm-passive-rx-test
```

Use numeric current bus/address, never automatic device selection.
All arguments and `DebugImage` artifacts are checked before USB loading.
The capture must be an explicitly selected **new** absolute file outside the
repository, in a user-owned0700 directory, created0600 with no overwrite or
symlink traversal. No raw bodies/payloads/addresses or frame hashes are printed
to stdout; only sanitized aggregate status appears after successful cleanup.
The private per-checkpoint records contain full wire state and the persistent
frame, including failure sentinels, for parent-owned comparison with a private
concurrent independent sniffer capture. Do not upload them to Git/CI.

The runner reuses `reset_and_verify_code`, `step_nop` and `wait_checkpoint`.
It requires reset config26, compares every physical CODE byte before resume
and grants only existing reset/CPU/read/breakpoint permissions. **No debug
DMA22 gate, RAM/MMIO/flash writer or new USB packet is added.** It reads only
ordinary caller SRAM/M0 plus the existing full CPU context, checks diagnostic
serialization, frame/tail/nonpublication, heartbeat, immutable M0 and C-observed
flag history, and verifies register preservation. Each transport/checkpoint
wait is bounded. It stops halted at READY after1..16 attempts, or terminal
FAULT, with no retry, automatic reset, reattach or continuation after fault.
The firmware's next step after16 READY would enter terminal END without RX.

There is no RF cleanup guarantee: a timeout/failure may leave RX **active
while halted**. Do not resume the failed invocation. A full reset and complete
CODE verification are required in a **separately authorized recovery run**.
Compare channel15 successful FCS-free bodies/raw RSSI/correlation privately
with the independent sniffer; raw RSSI is not dBm and correlation is not LQI.
Report BAD_CRC, losses/backlog discard, timeouts and recovery honestly.
CRC does not establish MAC syntax, authentication, membership or network
acceptance. The dated LG observations cover only their stated finite cases;
generic simulation does not establish physical FCS/timing or broader recovery.

### 2026-09-18 LG RX FSCAL1 failure and probe

**Parent-observed failure and diagnosis, not RX success.** The offline
implementer performed no target/USB operation. The parent reported a private
256-KiB flash backup and2,048-byte factory backup, guarded programming of the
LG Rev0.3 **9,135-byte** image with SHA256
`1c459ba79eff3c6283ccecf882b0573b1f2eb979490c08d11b57e88be066e82e`,
independent full-flash comparison including the erased `FF` tail and unchanged
factory region, then full reset and physical CODE verification before execution.
Backup contents, identities and captures remain private and are not imported.

The first RX attempt, with a concurrent independent Nordic channel15 capture,
ended in terminal **STATE_CHANGED7**, not publication. RX diagnostics were
phase2, polls21, elapsed90 **raw ticks**, writes10, verified10, actions1
(E3 issued), sample_valid0 and bytes_read0. The full output object remained
`A5`. There is no frame, FCS, calibrated timing or sniffer-agreement result.

In a **separate genuine full-reset/CODE-verified diagnostic epoch**, the parent
used only existing CPU/breakpoint/SRAM-read methods, preserving full CPU
context. No host MMIO access exception or writer was introduced. The exact
old-image comparison at `0BF2` was:

```text
B5 4E 02 80 06 75 82 07 02 0D B0
```

The breakpoint at its failure return `0BF7` showed saved bank0 R1=`08`,
A(expected)=`00`, and IRAM `4E` (read through the `1F4E` alias)=`30`.
Table index8 is **FSCAL1 `61AE`**. Diagnostics still showed phase2,
writes/verified10, actions1 and no consumed bytes; the frame remained `A5`.
The parent let the original failure path finish at FAULT `016C`, with
result=latch7. This identifies the full-byte FSCAL1 comparison as the failing
predicate rather than inferring it from a generic error code.

**SWRU191F, revised April2014, p.267 FSCAL1** marks bits7:2 reserved R/W0
(reset `001010`), not R0; VCO_CURR bits1:0 are R/W. Sections23.15.1/Table23-6
and23.15.2/Table23-7 retain the recommended whole-byte write `00` and
distinguish W0 from R0. FSCAL2 is the separate capacitor calibration result.
The corrected observer therefore masks only FSCAL1 readback with `03`;
nonzero low bits and every other ownership/configuration/error predicate
remain failures. The [current hashes/sites](RADIO_RX.md#wire-v1-and-linked-abi)
are different; do not reuse this historical probe address against them.

FAULT `016C` was the diagnostic epoch's historical endpoint, not the final
reported state. A halt alone did not establish RF-off after E3.

The parent subsequently performed a **separate genuine full reset-halt** using
the previously frozen9,135-byte image/hash above, and compared every physical
CODE byte again. The final reported state for that old-image recovery was **PC `0000`, status `22`,
configuration `26`, with no application resume after reset**. The private
post-failure-reset manifest was retained, not imported. This records
reset-to-halt and CODE verification on the old image, not successful RX or
corrected-image acceptance.

The Nordic capture was stopped and its native port released; channel15
readback was verified. The subsequently authorized corrected-image experiment
is a separate record below, not a reinterpretation of this failure.

### 2026-09-18 LG bounded passive RX acceptance

**Hardware-observed on the owned LG Rev0.3, channel15 only.** The parent
performed these manual operations; no delegated implementer, build or automated
test accessed equipment.
The corrected `radio_rx_fixture` was9,160 bytes, SHA256:

```text
0e31578a708d9c8d4556caec33f062fd82baf7498ebef1af3b708f868ab6c8d2
```

The seven checked artifacts were frozen privately before programming.
The reviewed system programmer plus Linux no-run guard completed erase,
write and readback verification, then intercepted normal-run cleanup.
Independent debugger reads compared every CODE byte while halted. A separate
complete262,144-byte read matched the image plus erased `FF` tail; the2,048-byte
factory page matched the private pre-programming backup. A genuine reset and
another complete CODE comparison left PC0000/status22/config26 without
application execution. Guard exit86 alone was not accepted as evidence.

The independent reference was the Nordic nRF52840 DK running official sniffer
firmware0.8.0, using the separately
[pinned external host tool](PROVENANCE.md#m2-passive-rx-sources), whose package
metadata reports0.0.0. Actual channel15 was read back before/after each capture.
Its process
was alive and receiving packets before LG execution. Captures were new0600
files in an owned0700 directory outside Git; processes were stopped cleanly,
the sniffer put to sleep and the serial port released afterward.
Private parsing checked PCAP2.4/DLT283, complete/nontruncated record lengths,
ordered timestamps, TAP header/TLV layout and channel15. The tool strips
the two FCS octets, so comparison was byte-for-byte **FCS-free body equality**,
not independent FCS validation or authentication. No payload or payload hash
is published.

| Separate experiment | Observed result |
| --- | --- |
| First corrected-image receive | One CRC_OK Data body,69 bytes, matching exactly one of215 reference records;639 raw ticks/126 polls |
| Pre-configuration timeout | Real TIMEOUT10/latch10,105,828 raw ticks/one poll, phase1/sample-valid1; zero writes, actions or consumed bytes, entire frame still `A5` |
| Explicit reset recovery |16 attempts,14 CRC_OK publications and two BAD_CRC returns; each published body matched exactly one of321 concurrent reference records |
| Same-epoch attempt cap | Next step reached END016F with attempt16/completed14/heartbeat14, no17th receive; a further terminal-loop execution retained the complete state |

The timeout stimulus used **only** existing reset/CPU/breakpoint/SRAM-read
permissions, not code, RAM or MMIO injection. The checked148-byte
`timebase_deadline_after` helper starts at01CA and returns at025D; the sole RX
call is12FF, with stacked return1302. After the real clock READY, the parent
stopped at that RET and verified DPL0/DPS0, the actual stack return, allocated
arguments and work object, computed65536-tick deadline, untouched output and
zero configuration/RF actions. A3-second host hold was bounded by the existing
transport operation deadline and preserved CPU context. Resuming the genuine
RET produced the terminal failure above. One NOP/loop execution retained the
original result/latch/state. This tests a **pre-RF** timeout, not a busy-radio
timeout, calibrated duration, stopped timer or controller-error recovery.
The following16-attempt run used its own full reset/full-CODE verification;
no failure clear, implicit retry or continuation from FAULT was used.

In the recovery run, BAD_CRC occurred at attempts4 and16. Both had CRC_OK clear,
preserved every output byte as `A5`, and left the fault latch zero. Successes
after attempt4 establish reuse after clean stop/flush without an intervening
reset. The14 successful bodies were10..66 bytes, with raw frame-type bits
indicating eight Data and six Command frames, not full MAC/protocol acceptance.
The reference contained134 Data,147 ACK and40 Command records. Across all16
attempts, elapsed observations were527..28,173 raw ticks and104..5,855 polls;
reported backlog discard was zero. This does not measure losslessness across
RX-off/READY gaps. Successful raw signed RSSI was -21..7 and correlation86..108;
neither is calibrated dBm/IEEE LQI or equated with the Nordic metadata.

**Final physical state:** halted at END `016F`, phase5/stage2,
attempt16/completed14/heartbeat14, fault latch0. The last result was BAD_CRC15;
its frame stayed `A5`. The cap preserved that frame and the previous diagnostics,
with RXENABLE0, RX/TX counts0 and empty pointers, clock command/status88,
IRQs disabled and checkpoint SP62. A subsequent terminal-loop pass changed
neither state, frame, heartbeat nor full CPU context. The reference was stopped
and its port released. Earlier FAULT/READY endpoints remain historical.

This completes the bounded channel15 LG reception/body-agreement,
BAD_CRC/reuse, pre-RF timeout/reset-recovery and attempt-cap observations.
Generic hardware, other channels, independent on-air FCS, calibrated timing/
metadata, overflow/stopped-clock/poll-cap recovery, continuous queues, TX/ACK,
MAC/security/networking and the remaining M2/M3 gates are not established.
All18 older board BINs are unchanged; CI remains offline with20 jobs,
the seven-artifact whitelist and `hardware_tested=false`.

### 2026-09-19 connected LG RX revalidation

Under renewed explicit permission to check the connected equipment, the parent
first inspected the existing halted session without reset or resume:
PC016F/status2B/config26. Every9,160 CODE bytes matched the published corrected
LG RX fixture above, and the complete CPU register context was preserved.
No programmer, flash write, factory-data read or unknown application execution
was involved.

A separate manual `check_radio_rx_hardware.py --attempts 16` invocation then
performed its own full reset, reset-config26 check and complete physical CODE
comparison before execution. It returned16 attempts, **15 CRC_OK publications
and one BAD_CRC**, with register preservation. The raw records were created in
a new0600 file in a user-owned0700 directory outside the repository.

The parent next advanced the already-verified sixteenth READY to END016F.
The attempt/completion/heartbeat counts stayed16/15/15, the frame and bootstrap
record were unchanged, and a further real terminal-loop pass preserved the
entire serialized state, frame, bootstrap and CPU context. There was no17th
receive. **Final physical state at this record: halted at END016F.**

This is a fresh bounded channel15 **hardware-observed** regression of the same
firmware, not new TX, MAC, association, capture-timing or calibration evidence.
There was no simultaneous independent sniffer comparison in this run; the
earlier body-agreement results remain dated observations, not a claim for
these15 publications. No packet contents, identifiers or payload hashes are
published, and automated tests/CI still perform no hardware operation.
