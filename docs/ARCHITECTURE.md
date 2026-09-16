# Architecture

This is the intended stack architecture. At M0 only board/platform bootstrap,
status storage and validation infrastructure exist. The layer names below
are boundaries to implement, not a list of working APIs.

The partial M1 addition is a separate non-RF debugger fixture, not a protocol
layer. Its deterministic pattern logic is host-testable; its SDCC register
probe is confined to the target example. It shares existing startup/board
code rather than introducing a second GPIO policy.

The host-only `tools/cc_debugger.py` keeps USB access behind a narrow backend
interface. Session policy, exact diagnostic packets, deadlines and failure
states can be tested without importing PyUSB or enumerating devices. The
optional backend does not own board GPIO or decide reset/attach policy.

CPU-control permission is separate from permission to read an existing
debug session; state checks and the complete command exchange share one
deadline. Reset into halt has a third, separate permission, is limited to an
already prepared debug session and is never triggered by open/close or an
error. The separate `RESET_DEBUG_SESSION` access policy permits only explicit
reset-based initial attach: target operations remain denied until preparation,
reset and postchecks finish. It does not claim a non-reset attach.
`tools/cc2530_debug.py` contains target command/status facts, distinct
from USB framing. `tools/debug_image.py` handles only offline artifacts and
snapshots, reusing strict image checks and never importing the USB transport.
Its source lookup exposes exact linked CDB records, including multiple records
at one address; it does not guess source ranges or read compiler-named files.

## Layer boundaries

The standalone `mac_frame` module currently supplies only offline legacy
DATA/ACK body encoding/decoding and five fixed-format command payloads/frames.
Command-specific header validation is still stateless serialization, not a
procedure or association state machine. The module has no board/platform dependency or
network state and is not linked into bootstrap/fixture firmware. Its
[contract](MAC.md) separates syntax success from CRC/security/peer acceptance.

```text
sensor / local display application
              |
       attributes / ZCL
              |
            ZDO + APS
              |
     end-device NWK + security
              |
        end-device MAC
              |
   CC2530 radio / AES / time / NV
              |
       board-specific GPIO
```

The host validation build should exercise packet encoding, state machines
and persistence decisions without depending on CC2530 SFR syntax. Hardware
access belongs behind narrow platform interfaces, not in protocol parsers.

Proposed interface responsibilities:

| Boundary | Responsibility | Must not do |
| --- | --- | --- |
| Radio | Frame TX/RX, filtering, CCA, timestamps/errors | Pretend to perform Zigbee join |
| MAC | Association, ACK/retry and parent exchanges | Own application attributes |
| NWK | ED addressing, join/rejoin, parent/update state | Forward traffic or manage children |
| APS | Endpoints, transactions, ACKs and applicable security | Silently discard ownership errors |
| ZDO | Required discovery/management behavior | Advertise unimplemented services |
| ZCL | Typed attributes and implemented foundation commands | Serialize C structs directly as wire format |
| NV | Validated atomic records and monotonic reservations | Return success after a failed flash operation |
| Application | Sensor values and display scheduling | Block protocol progress during refresh |

## Memory contract

| Address space | Meaning |
| --- | --- |
| XDATA `0x0000..0x1EFF` | 7,936 bytes of SRAM distinct from IRAM |
| XDATA `0x1F00..0x1FFF` | Alias of the 256-byte IRAM; not extra storage |
| IRAM | Register banks, compiler data and stack |
| CODE | 64 KiB CPU view with CC2530 FMAP banking for larger flash |

M0 reserves `0x1E00..0x1E3F` for at most 64 status bytes. Ordinary allocation
ends below `0x1E00`; unused space above status is not an implicit allocation
pool. The linker/map checker and alias-aware simulator enforce this.

The M1 fixture retains that exact reservation and M0 status ABI. Its separate
16-byte `debug_fixture_state` lives in ordinary, linker-accounted XDATA below
`0x1E00`; its address is looked up in the matching image's map, not hardcoded.
The existing 512-byte nonaliased-XDATA reservation budget still applies.

Do not clear an XDATA object at `0x1F00`: this can overwrite the very register
holding its loop index and the active return addresses. A generic 8051
simulation with separate IRAM/XDATA will miss that failure.

C rules:

- Use fixed-width integers and explicit byte encoders for wire formats.
- State byte order, length and ownership at every protocol boundary.
- Keep constant tables in CODE; do not copy fonts or descriptors into RAM
  unnecessarily.
- Use bounded static queues/pools with explicit exhaustion behavior.
- Treat SDCC's data models, pointer spaces, register allocation and reentrancy
  as part of the ABI, not as interchangeable desktop-C implementation details.
- Any banking support must handle calls, interrupts, constants and debugger
  addresses together. It is not only a linker flag.

## Scheduling and ownership

Use a cooperative foreground state machine with short, bounded work items.
Interrupt handlers capture minimal state and enqueue work; they must not call
non-reentrant foreground helpers or run ZCL/display processing.

Every queued object has one owner. Timeouts/retries and cancellation need
explicit transitions so a reset, leave or parent change cannot use stale
buffers or complete an operation twice.

The initially awake ED and later SED share protocol behavior. Sleep is permitted
only when radio/APS transactions, timers, NV and application activity agree.
Fast polling during transactions is different from normal background polling.

Display work is scheduled in short slices. Sending a command/row is distinct
from waiting for the controller; BUSY waits become scheduled deadlines rather
than long CPU loops. Errors always lead to a defined power/control state.

## Persistence design

Reserve explicit flash pages outside code, factory/configuration data and lock
locations before introducing any writer. Define record version, generation,
length, integrity checks and commit semantics.

Persist network/parent information, required keys and counters, then add
bindings/reporting settings only when their behavior exists. Corrupt or
incomplete records are rejected with a visible recovery reason.

After persisted resume, select an immediate keepalive from the saved
`nwkParentInformation`. Unknown parent information triggers ED Timeout
renegotiation with bounded recovery; a missing response must not block startup
forever. Test this separately from a fresh join.

For outgoing security counters, reserve a durable future range before using
it. A restart may skip values; it must not reuse transmitted values. Validate
this under interrupted writes and network leave/factory reset, not only under
orderly shutdown.

## First board

The LG ESL board is an application example, not the protocol architecture.
Display pins and their power sequencing stay in board/display code.

P1.6/P1.7 are exposed UART pads and candidates for future software I2C after
UART ownership is disabled. Existing NFC wiring and supply arrangements must
be considered before reusing its bus. P2.1/P2.2 remain available for debugging.

Motherboard straps describe the motherboard configuration. They do not detect
the type of a display that someone has swapped onto the connector. No unknown
panel gets an automatic voltage/LUT/profile fallback.
