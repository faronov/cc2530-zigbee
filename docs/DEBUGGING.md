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

**This repository does not yet contain that hardware-debugger frontend.**
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
