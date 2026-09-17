# Development plan

## 1. Goal and present state

Build an inspectable C/SDCC Zigbee end-device stack for the CC2530F256,
then add dependable sleeping operation and a small sensor/display application.
The purpose is not to port every feature of a commercial stack or implement
a coordinator.

**Implemented at project creation:** a non-networking bootstrap, build/image
checks, host/simulator tests and CI. The bootstrap has not been validated on
physical hardware merely because a preceding display prototype worked.
The standalone `bringup` images remain unflashed; shared startup was later
exercised on one LG board through the M1 fixture, as recorded below.
Everything from M1 onward below is planned except for the explicitly
implemented components and evidence recorded under each milestone.

The [initial conformance ledger](CONFORMANCE.md) exists at M0. Selection of
the exact R22-compatible BDB revision is an open, blocking prerequisite for
M4/M5 security/commissioning implementation, not work postponed until release.
Update the ledger with each protocol change; M9 audits it rather than first
creating it.

The development sequence is:

```text
M0 repository / reproducible bootstrap
  -> M1 usable hardware debugger
  -> M2 CC2530 platform services
  -> M3 radio and end-device MAC
  -> M4 security and durable state
  -> M5 authenticated receiver-on end device
  -> M6 discovery / application interoperability
  -> M7 sleepy end device
  -> M8 sensor and streamed display
  -> M9 hardening and reproducible releases
```

Host-side codecs, test vectors and documentation can proceed alongside earlier
hardware work. Later features must not bypass their security or recovery gates.

## 2. Scope decisions

| Area | Initial decision | Consequence |
| --- | --- | --- |
| Language/toolchain | C99, SDCC 4.2.0 baseline | No Rust runtime or IAR object dependency |
| Hardware | CC2530F256; generic and LG ESL board descriptions | Other CC253x parts need a separate validation record |
| Role | Receiver-on ED first, SED second | One logical ED role; no forwarding or children |
| Network | Centralized Trust Center network first | Distributed networks are outside the first supported configuration |
| Specification | Core R22 and compatible Zigbee-3.0-era BDB | BDB 3.1/R23 is not silently treated as the R22 baseline |
| Application | Small reporting sensor, local display later | Only implemented clusters are advertised |
| Memory | Static pools and explicit bounds | No heap-dependent protocol or frame-sized display buffer |
| Debugging | CC Debugger plus RAM trace and independent sniffer | An IDE or GDB integration is not assumed to exist |
| License | BSD-3-Clause original code | Every imported file has its own provenance review |

Initially excluded: coordinator/Trust Center server, router, child admission,
mesh routing, Green Power proxy, Touchlink and OTA. Exclusion is not a claim
that every listed capability is optional in every specification or profile.
Unsupported modes are documented and must not be advertised as supported.

## 3. Milestones and acceptance gates

### M0 - Public foundation

Deliver:

- Clean history, license, source policy and contribution instructions.
- SDCC bootstrap for the two named board configurations.
- Versioned status ABI, memory bounds and alias-aware simulation.
- Linux CI with pinned toolchain packages and generated artifacts.
- This plan and a traceable evidence vocabulary.

Exit:

- A clean checkout builds and tests without proprietary downloads or secrets.
- CI produces genuine firmware artifacts and succeeds for both boards.
- The README says explicitly that the image is not a Zigbee device.
- No automatic flashing, radio transmission or display-refresh target exists.

### M1 - Hardware debugging that we can trust

**Complete for the bounded LG/unbanked baseline.** The [target fixture](DEBUGGING.md#implemented-target-fixture)
in `examples/debug_fixture.c` and `src/debug_pattern.c` is host-tested,
image-checked and alias-aware simulated for both boards. It is a separate
build, not a change to the default M0 image.

The [host transport](DEBUGGING.md#implemented-host-transport) in
`tools/cc_debugger.py` provides explicit adapter selection, guarded status/config,
PC/bank/register and safe SFR/XDATA/CODE reads, verified ordinary-SRAM writes,
unbanked breakpoint programming and HALT/RESUME/STEP with an optional PyUSB
backend. CPU control, reset, memory access, memory writes and breakpoints have
separate permissions. `reset-halt` requires a prepared session;
`attach-reset` explicitly prepares/reset-halts under its own access policy.
Open/close never resets or resumes. Failed/late exchanges fault the session
without retries or attempted register restoration. The
[offline tools](DEBUGGING.md#offline-image-symbol-and-snapshot-tools) also
provide verified-image global symbol and exact CDB source-line lookup, strict
M0/M1 snapshot decoding and target breakpoint-parameter encoding; they never
access USB.

Both fixture builds retain host, image and alias-aware simulator coverage.
The [2026-09-16 hardware record](DEBUGGING.md#2026-09-16-lg-fixture-hardware-record)
adds one LG Rev0.3, an unchanged 626-byte fixture and a final 257-cycle run
using isolated PyUSB 1.3.1. It covers all four simultaneous breakpoint slots,
known registers/NOP stepping, real returns and counter wrap, reset, bounded
RAM access, IRAM aliasing, all PSW-register-bank/DPS combinations, real USB
timeout/stall failures and controlled halted erased-image recovery. After the
first confirmed cable reconnection, explicit selection at the new address and
a complete one-cycle fixture check passed, including physical CODE verification,
all four slots, alias checks and reset. The subsequent final held-handle cable
removal produced NO_DEVICE, faulted the session, denied resume without further
bulk writes and exposed cleanup failure. After the last replug, PyUSB enumerated
the adapter and a full one-cycle fixture check in an explicitly selected new
session also passed, including physical CODE verification and reset
reinitialization. That final run left the fixture halted at `0x0173`.

The [acceptance boundary](DEBUGGING.md#m1-acceptance-boundary-before-m2)
distinguishes these passed finite scenarios from universal compatibility.
No current LG/unbanked M1 gate remains open.
Generic hardware is unobserved. Bank discrimination is required when banked
CODE is introduced, not a blocker for the current unbanked image.
Recovery evidence does not establish a mid-word electrical power cut or flash
wear tolerance; those belong to future platform/persistence work. Non-reset
attach, sleeping targets, MMIO/full-SFR access, a flash writer and GDB remain
unsupported. No M2/RF work is validated by these debugger results.

Deliver:

- A host debug transport for the CC Debugger, with explicit reset/attach policy.
- Halt/resume, PC and bank reading, instruction step, code breakpoints,
  register/memory access, and symbol lookup.
- SDCC debug-build handling; source-line support is an additional feature,
  not implied by address-level breakpoints.
- A dedicated fixture that never transmits and keeps known board outputs safe.

Exit:

- Hardware proves all four code breakpoints, including bank discrimination
  once banked code is enabled.
- A known instruction advances PC as expected; register-preserving reads
  do not corrupt resumed execution.
- Failed reads/writes and disconnection leave an explicit error, not a false
  success or execution of an unverified image.
- Breakpoint, reset, RAM-alias and interrupted-flash recovery procedures are
  documented. See [DEBUGGING.md](DEBUGGING.md).

Do not begin diagnosing a complex stack with an unvalidated debugger.

### M2 - Minimal CC2530 platform services

**First bounded implementation; M2 remains open (#4).** The standalone
[awake-only timebase](ARCHITECTURE.md#awake-only-timebase-first-m2-slice)
reads the real 24-bit Sleep Timer and provides bounded modular deadlines.
It is host-tested, image-checked and alias-aware simulated in an isolated
test executable. A subsequent, separate
[board timebase fixture](DEBUGGING.md#awake-only-timebase-board-fixture)
now calls the compiled reader/deadline helpers, with bounded polling and
latched faults. Its explicit manual runner completed
[LG compiled-C hardware acceptance on 2026-09-16](DEBUGGING.md#2026-09-16-lg-compiled-c-timebase-acceptance):
257 verified cycles, elapsed 129..130 raw ticks for requested 128, 37 polls
per cycle, and preserved CPU/M0 state. Generic hardware and physical timer-fault
injection remain unobserved. Existing `bringup` and
`debug_fixture` firmware bytes and M1 evidence are unchanged. A separate
[debugger-register observation](VALIDATION.md#independent-sleep-timer-hardware-reference-2026-09-16)
established raw counter progression and natural rollover on LG, not execution
of the C driver or calibrated timing. The compiled-C run is a separate
experiment and does not establish natural 24-bit rollover in those cycles.
A further isolated [init-time system clock selector](ARCHITECTURE.md#init-time-system-clock-selector-isolated-m2-slice)
now requests RC16/XOSC32 with raw-time and independent poll bounds, strict
entry-state checks, and one bounded restoration attempt on failure. A subsequent separate
[clock board fixture](DEBUGGING.md#init-time-clock-board-fixture) now executes
RC16 idempotence, XOSC32 and RC16, with serialized original/rollback results
and terminal faults. Its manual runner includes a strictly verified pre-request
deadline-RET halt experiment, without code/RAM injection. The
[first LG clock experiment](DEBUGGING.md#2026-09-16-lg-clock-cancellation-failure)
passed a normal sequence but **failed rollback acceptance**: old STA briefly
matched after cancellation, then the still-pending XOSC change appeared.
The root fix requires observed requested-source departure followed by return,
or reports bounded `CLOCK_ROLLBACK_UNCONFIRMED=9`. The corrected LG image passed
[bounded compiled-C clock acceptance on 2026-09-17 (UTC+03)](DEBUGGING.md#2026-09-17-lg-compiled-c-clock-acceptance):
the unchanged pending-cancel test confirmed return after 64 raw ticks /
15 polls, the separate verified late-source timeout test passed, and an
explicit reset/recovery run completed 257 full sequences / 771 C calls.
Both timeout cases retained the original error and terminal FAULT; recovery
was a separate run, not implicit continuation. All six older BINs are unchanged.
Generic and never-departed cancellation remain host/image/simulator evidence
only. Frequency/calibration, physical oscillator-failure/stopped-clock,
interrupts, compare/wake and the other M2 gates remain open. The new clock
record is separate from the earlier LG timebase evidence; full M2 #4 is open.

The next bounded [interrupt-ownership foundation](ARCHITECTURE.md#interrupt-ownership-foundation-isolated-m2-slice)
adds only reentrant EA save-disable/exact restore, with caller-owned LIFO
byte tokens and explicit invalid-token rejection. It is host-tested,
image-checked and alias-aware simulated, including genuine generic C52
preemption/RETI. That standalone evidence does not establish CC2530
peripheral delivery; the separate board acceptance below does so narrowly.
All eight older board BINs remain byte-identical and exclude the primitives.
The foundation did not add a board image or peripheral dispatcher.

The subsequent [Timer1 IRQ board fixture](DEBUGGING.md#timer1-irq-board-fixture)
now links the unchanged primitives separately for both boards. It uses the
documented CC2530 vector 9, stops the counter while overflow is pending with
EA=0, checks nested restoration, and masks/acknowledges only its owned source
before real RETI. The guarded manual runner checks physical CODE and ISR
context, with a separate bounded pre-start timeout experiment. Both images
retain host/image/synthetic-entry simulation coverage. The unchanged LG image
passed [bounded hardware acceptance on 2026-09-17 (UTC+03)](DEBUGGING.md#2026-09-17-lg-compiled-c-irq-acceptance):
three normal cycles, an independent TIMEOUT/terminal FAULT, then a separately
reset 257-cycle run with real Timer1 ISR services, counter wraps and preserved
CPU/IRAM/M0 state. Hardware interrupted the restore leaf at +12 with DPL=OK,
not the synthetic +9/live-token case. That run ended at IRQ READY `01BB`,
with EA/T1IE disabled and Timer1 stopped; prior clock/M1/timebase
records remain historical. Generic hardware remains unobserved.
That IRQ slice added ten board/image jobs with the exact artifact whitelist. Higher-priority
hardware nesting, other sources, measured/calibrated timing and general
dispatch remain deferred; clock/timebase ownership is unchanged and M2 #4
remains open.

The next isolated [quiescent radio FIFO foundation](ARCHITECTURE.md#quiescent-radio-fifo-foundation)
adds verified FIFO clear and bounded RFD preload, not RF enable or a MAC.
It requires known awake radio/CSP/DMA ownership and stable XOSC32; all ten
existing board BINs were unchanged by that isolated slice. Its
[host/image/synthetic FIFO evidence](VALIDATION.md#m2-quiescent-radio-fifo-automated-coverage)
does not establish physical FIFO/CSP behavior. The subsequent, separate
[quiescent FIFO board fixture](DEBUGGING.md#quiescent-radio-fifo-board-fixture)
and guarded manual runner now implement that offline preparation, including
real-driver CODE/XDATA payload checks and a proved pre-write deadline hold.
The FIFO addition preserved all ten older BINs and the exact artifact
whitelist. The unchanged LG image passed
[separate hardware acceptance on 2026-09-17](DEBUGGING.md#2026-09-17-lg-compiled-c-fifo-acceptance):
normal operation, a terminal written-but-unverified timeout, then 257 cycles
after an explicit reset, with 33,410 checked bytes and 514 TX clears.
That FIFO run ended at READY `016A`, XOSC32, IRQs disabled and empty FIFOs.
RX flush, received frames, on-air behavior, error-latch recovery and calibrated
metadata remain open; generic has no physical FIFO evidence. M2 #4 remains open.

The independent AES-128 encrypt-block prerequisite is
[blocked on CPU transfer sequencing](PROVENANCE.md#m2-aes-cpu-transfer-prerequisite).
No AES primitive or successful placeholder is added. A separately assigned
[channel-0 RAM-copy DMA foundation](ARCHITECTURE.md#isolated-channel-0-dma-copy)
now advances that dependency with host, linked-image and synthetic execution
only, without a peripheral trigger, AES operation or hardware claim.
The separate [reset-scoped DMA-enable debug gate](DEBUGGING.md#guarded-dma-enable-after-reset)
now has host and bounded LG hardware evidence, without exercising DMA.
The subsequent [DMA board fixture](DEBUGGING.md#channel-0-dma-board-fixture)
has offline checks for both boards: 257 compiled RC16/XOSC32 cycles,
514 bounded copies, exact relocated ABI/nine-NOP/negative-RET proof and a
guarded manual runner. Only these two images link DMA; all twelve older
BINs remain unchanged. The matrix grows to fourteen jobs with the same
seven-file whitelist and `hardware_tested=false`. The unchanged LG image passed
[bounded hardware acceptance on 2026-09-17](DEBUGGING.md#2026-09-17-lg-compiled-c-dma-acceptance):
normal operation, a terminal unverified timeout, then 257 cycles after an
explicit reset, with 514 copies and 6,289 verified bytes. The last LG run
ended at DMA READY016A/config22 on RC16, with IRQs off and ARM/REQ/DMAIRQ zero.
Generic, other channels/triggers and physical stuck-controller recovery
remain unobserved.
This does not close the remaining radio/RX/channel/calibration gates or M2 #4.

Deliver independent interfaces for:

- Clocks, wrap-safe monotonic time, short deadlines and interrupt dispatch.
- Radio register/FIFO access, reset, channel selection and calibrated metadata.
- Randomness appropriate to its documented purpose; do not call a fixed seed
  or timer value a cryptographic random generator.
- Hardware AES block operations with host-reference vectors.
- Flash page operations and a reserved, nonoverlapping persistence area.
- Board GPIO and low-power hooks, initially with sleep disabled.

Exit:

- Timers are measured on hardware and rollover tests pass.
- AES agrees with public known-answer vectors on host and target.
- Flash operations honor alignment/bounds and expose controller failures.
- Board pins, code/NV partitions and ISR ownership are documented.
- No required platform primitive is represented by a successful no-op.

### M3 - Radio and end-device MAC

**Offline preparatory implementation:** the [legacy body codec](MAC.md)
encodes/decodes a bounded DATA/ACK subset plus five fixed-format commands and
their addressing layouts, with host, linked-image and
alias-aware simulator evidence. It also handles version-0 Beacons without GTS
descriptors, bounded pending-address lists and opaque upper-layer payloads.
This adds no scan, synchronization, scheduling or Zigbee discovery procedure.
It is not linked into board firmware and
does not establish radio, MAC or networking support. Hardware M1/M2 gates
are not bypassed.

Adapt the BSD-licensed Contiki CC2530 RF code selectively, or implement the
documented registers directly. Do not import Contiki's complete OS/netstack.

Deliver:

- Bounded TX/RX queues, FIFO overflow recovery and CRC/error handling.
- Channel/PAN/address filtering, CCA/backoff, ACK handling and retry bounds.
- Beacon discovery, association, coordinator/parent selection and MAC commands.
- Explicit ownership of buffers crossing ISR and foreground code.
- A capture format and counters that do not expose real credentials.

Exit:

- An independent IEEE 802.15.4 sniffer observes correct frames, addresses,
  ACK timing and retry behavior.
- Invalid lengths, CRC failures, queue pressure and disconnected peers
  cannot corrupt memory or block forever.
- A lab-only MAC exchange is identified as MAC bring-up, not Zigbee joining.
- RF tests are opt-in, within the supported channels/power settings, and never
  run in hosted CI.

### M4 - Security and durable state

Entry gate: pin the compatible BDB revision and security/commissioning
requirements in the conformance ledger.

Deliver:

- Zigbee-specific AES-CCM* nonce/header/MIC handling, using the AES primitive.
- Network/link-key handling, key identifiers/sequences and replay rejection.
- Two network-key slots and the applicable Trust Center key procedures.
- Atomic persistence for network identity, parent information, keys,
  bindings/configuration when implemented, and outgoing security counters.
- Counter-range reservation or another demonstrably monotonic power-loss
  strategy; versioned records, validation and wear accounting.

Exit:

- Host and target known-answer tests agree on wire bytes and authentication.
- Corrupted ciphertext/MICs, stale frames and wrong key sequences are rejected.
- Power interruption at every persistence write boundary cannot roll counters
  backward or create a partially accepted key/state record.
- NWK Leave/factory reset cannot accidentally reset outgoing NWK counters.
- Secrets are absent from ordinary diagnostics and public CI artifacts.

Do not trade this milestone away to make the first network demo look complete.

### M5 - Authenticated receiver-on end device

Deliver:

- Required NWK and APS header handling and bounded transaction/ACK state.
- Centralized-network steering, key transport/verification and join completion.
- Join-critical endpoint-0 services, including Node Descriptor exchange and
  address/Device Announce handling. Introduce the generic ZDO unsupported-service
  fallback as soon as endpoint 0 is exposed, before omitting any optional handler.
- Device Announce, address-conflict handling and required address resolution.
- ED Timeout negotiation, parent information and selected keepalive method.
- On persisted resume, immediate keepalive chosen from stored
  `nwkParentInformation`; when unknown, ED Timeout renegotiation with bounded
  failure/recovery rather than an indefinitely blocked startup.
- Child-side rejoin, parent loss, explicit leave and persisted restart.
- NWK Network Update handling, including wrap-aware update identifiers.

Exit:

- A real coordinator accepts an authenticated device, not merely a MAC child.
- Power cycling preserves a usable joined state without counter rollback.
- Parent loss/rejoin and leave are exercised, including same-parent rejoin.
- Restart tests cover known/unknown parent information and an absent parent.
- Failures have bounded recovery and an observable reason.
- The evidence records coordinator software/adapter versions and firmware
  revision, with private network data kept outside the repository.

M5 is an authenticated lab ED milestone, not a claim that all discovery or
application-profile requirements have been completed. M6 finishes those
surfaces and their interoperability evidence.

### M6 - Discovery, ZCL and receiver-on interoperability

Deliver:

- Completion of required ZDO discovery clients/servers, retaining the generic
  unsupported-service fallback introduced with endpoint 0 in M5.
- Small source-binding storage and the management behavior it requires.
- Full `Mgmt_Leave` (`0x0034`), full `Mgmt_Bind` (`0x0033`) when source bindings
  exist, and a tested status-only `NOT_SUPPORTED` response for omitted
  `Mgmt_Lqi` (`0x0031`).
- A real attribute model with types, access checks and bounded serialization.
- A clearly identified **lab-only synthetic measurement application**, with
  Basic/Identify and a deterministic test-controlled measurement source.
  It must identify itself as synthetic and must not be released as physical
  sensor firmware. Real sensor/battery measurement support is added in M8;
  exact cluster requirements are checked against the selected profile.
- Read/configure-reporting/report behavior, errors and supported persistence.

Exit:

- A coordinator completes interview and reads meaningful descriptors/attributes.
- Binding, reporting changes and leave work across restart.
- `Mgmt_Leave`, owned-source `Mgmt_Bind` and omitted `Mgmt_Lqi` behavior are
  exercised directly, not inferred from successful interview.
- Reports are not hardcoded to one coordinator endpoint/address.
- An unsupported unicast ZDO request gets the required status response;
  unsupported broadcasts are handled according to the specification.
- At least one Zigbee2MQTT and one ZHA configuration pass the documented
  receiver-on test set. Adapter diversity is tracked, not assumed.

### M7 - Sleepy end device

Deliver:

- Sleep eligibility rules shared by radio, APS transactions, timers,
  persistence and application work.
- Parent polling, Frame Pending handling, fast/long polling and wake recovery.
- Rejoin-response retrieval while sleepy and keepalive/timeout behavior.
- Hardware low-power entry/exit and measured clock/startup margins.

Exit:

- Pending downlink traffic is received during polling.
- Join, configuration and APS exchanges are not broken by premature sleep.
- Parent changes, loss, restart and key updates work while sleeping.
- Current is measured with the debugger's influence identified and removed
  for the final sleep measurement.
- A continuous run of at least 12 hours, then at least 72 hours, has bounded
  memory use and no unexplained loss of reachability. Record report/poll rates
  and exercise at least 100 controlled restart/rejoin cycles separately.
- Before testing, record per-board numeric current, polling-latency and timing
  limits. Undefined limits cannot produce a passing power/timing claim.

### M8 - Sensor and display application

Deliver:

- An explicitly selected I2C sensor with a licensed driver and honest units,
  calibration/accuracy and error handling.
- Board-specific software-I2C ownership; UART and I2C cannot own the same pins.
- A cooperative streamed display driver: short transfer slices, scheduled
  waits, timeouts and safe power-off.
- Explicit panel configuration, separate from motherboard strap readings.

Exit:

- Sensor reports and local display agree within the declared conversion rules.
- Missing sensors, bus faults and an absent/stuck display do not block radio.
- Display refresh does not starve polling or security/APS deadlines.
- No full 4,736-byte display framebuffer is required.
- The chosen panel, controller/profile evidence and physical result are recorded.

### M9 - Hardening and reproducible experimental releases

Deliver:

- Negative/fuzz tests for packet parsers and state transitions.
- NV power-cut/wear tests, watchdog recovery and long-duration runs.
- An audit of the continuously maintained conformance ledger, with every
  claimed behavior linked to its chosen revision and acceptance evidence.
- Reproducible build inputs, artifact hashes and a compatibility matrix.
- A reviewed release procedure; no automatic production release from a draft PR.

Exit:

- [Validation gates](VALIDATION.md) pass at the claimed support level.
- Known limitations are published with each release.
- Hardware compatibility is supported by evidence, not by a successful host
  build or the presence of a CC2530 chip.
- No certification claim is made without the corresponding independent process.

## 4. End-device requirements that cannot disappear

The concise list below is an implementation checklist, not a substitute for
the specifications:

- Discovery, join/rejoin, leave, Device Announce and address-conflict handling.
- Node Descriptor request **and** response; required address, power, simple,
  active-endpoint and match-descriptor responses.
- ED Timeout Request after every join/rejoin, stored `nwkParentInformation`
  and keepalive selection.
- Explicit restart keepalive/renegotiation and bounded absent-parent recovery,
  as a project robustness gate rather than a newly attributed BDB 3.1 rule.
- Required NWK updates, security, replay protection and persistent counters.
- Two network keys, link-key support and applicable Trust Center verification.
- Real binding/reporting behavior for the implemented reporting sensor.
- Full `Mgmt_Leave`; full `Mgmt_Bind` for owned source bindings; a status-only
  `NOT_SUPPORTED` response is sufficient for an omitted `Mgmt_Lqi` server.

Optional ZDO removal needs the R22 section 2.4.4.1 fallback: unsupported
unicast requests receive the corresponding response/TSN with `NOT_SUPPORTED`;
unsupported broadcasts are dropped. Do not confuse optional ZDP management
network-update handling with mandatory NWK Network Update reception.

Important references: R22 Table 2-44 p.84; section 2.4.4.1 p.137;
section 2.5.4.8.1 p.222; section 2.4.4.4.5 p.188;
section 3.6.10.2 pp.392-393; section 4.3.4 pp.416-417.
See [PROVENANCE.md](PROVENANCE.md).

## 5. Provisional resource budgets

| Resource | Bootstrap contract | Stack design target |
| --- | --- | --- |
| Flash | Unbanked, validated bounds | Try unbanked first; add tested FMAP banking only when needed |
| Ordinary XDATA | Allocator below `0x1E00` | Budget every queue/table; exact limits follow measurements |
| Debug status | At most 64 bytes at `0x1E00` | Versioned debug build feature, not a hidden permanent heap |
| Nonaliased XDATA use | At most 512 bytes for bootstrap | Stay within 7,936 bytes total with explicit headroom |
| IRAM | Separate stack/register accounting | Static review plus measured maximum stack depth |
| Packet storage | No network buffers yet | Small fixed pools; counts justified by concurrent transactions |
| Display | Not implemented in bootstrap | Stream rows/bytes rather than allocate a full plane |

The CC2530 has 256 KiB flash but a banked CODE view, not a flat 256 KiB code
space. The upper 256 bytes of its 8 KiB SRAM are the XDATA alias of IRAM.
Prototype display memory figures do not predict final stack size.

## 6. Risks and responses

| Risk | Response / evidence required |
| --- | --- |
| Debugger corrupts state or loses the original failure on reset | M1 fixtures, explicit lifecycle, register preservation and trace-before-halt |
| SDCC non-reentrant helpers shared with ISR | Keep ISR work small; explicit queues and compiler/listing review |
| Frame-counter rollback or flash wear | M4 atomic records, range reservation and exhaustive write-boundary tests |
| Interoperates with only one coordinator | Record multiple software/adapter combinations; retain failing sanitized cases |
| SED works only with debugger connected | Compare normal/debug operation and measure disconnected sleep current |
| Display blocks radio | Cooperative state machine with bounded work slices and fault injection |
| License contamination | Per-file provenance before copying; no proprietary/GPL code silently relicensed |
| Old stack examples mistaken for modern conformance | Audit against the declared R22/BDB regime; track unsupported features |
| Growing scope consumes all RAM/time | Keep one ED role and one application; defer optional capabilities explicitly |

## 7. Progress and change policy

Use small changes tied to a milestone and an acceptance test. A completed
milestone needs code, documentation and evidence at the relevant level.
Simulation is not hardware validation; a visible image is not Zigbee
interoperability; a successful join is not a complete SED implementation.

The plan deliberately does not promise a delivery date or a final byte count.
Update it when measurements or normative review change a decision. A future
issue tracker may split these milestones, but this document remains the
canonical dependency and acceptance plan.
