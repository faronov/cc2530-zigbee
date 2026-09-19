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

The [initial conformance ledger](CONFORMANCE.md) exists at M0. Core R22 is
paired with PRO BDB v3.0.1, document 16-02828-012. The base revision and
bounded ED requirements are recorded; review of applicable BDB errata
21-65431 remains a blocking prerequisite for M4/M5 security/commissioning
implementation, not work postponed until release.
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
| Network | Centralized Trust Center network first | Distributed networks are deferred; this is a BDB conformance gap, not an ED role exemption |
| Specification | Core R22 and PRO BDB v3.0.1, 16-02828-012 | Applicable errata gate remains open; neither BDB 1.0 nor BDB 3.1/R23 substitutes for this baseline |
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

The CPU-only AES-128 encrypt-block prerequisite remains
[blocked on CPU transfer sequencing](PROVENANCE.md#m2-aes-cpu-transfer-prerequisite).
That investigation added no successful placeholder. A separately assigned
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
explicit reset, with 514 copies and 6,289 verified bytes. That DMA run
ended at DMA READY016A/config22 on RC16, with IRQs off and ARM/REQ/DMAIRQ zero.
Generic, other channels/triggers and physical stuck-controller recovery
remain unobserved.
This does not close the remaining radio/RX/channel/calibration gates or M2 #4.

A separate [AES-128 DMA encrypt-one-block foundation](ARCHITECTURE.md#isolated-aes-128-dma-block)
now has host, genuine linked-image and alias-aware synthetic evidence.
It uses only the documented ENC-triggered two-channel interface with private
staging, finite key/IV/block phases and terminal failure ownership. It is not
a board `IMAGE`, production software fallback, messaging/CCM/security service
or CPU-transfer implementation. All fourteen existing board BINs and the
published DMA/timebase/clock/debug implementations remain unchanged.
Hardware evidence comes from the separately scoped fixture below, never from
flashing the standalone test or treating synthetic events as silicon.

The separate `IMAGE=aes_fixture` is available for both boards,
with compiled orchestration, a 64-byte explicit wire record, relocated driver
proof and guarded manual pre-key/final timeout procedures. The 21 public
cases cover every CODE/XDATA combination, both clocks and 257 same-reset cycles.
The [first physical LG KEY load](DEBUGGING.md#2026-09-17-first-lg-aes-key-load-failure)
failed the original flag assumption: input completed and both ENCIF bits set.
The unchanged corrected per-command completion/ACK protocol and wirev2 now have
[bounded LG hardware evidence](DEBUGGING.md#2026-09-17-corrected-lg-aes-bounded-acceptance):
six blocks on both clocks, vectors0/1/2 and spaces0/1, fresh KEY/IV pairs and18
confirmed ENC ACKs, plus both exact unpublished AES_TIMEOUT8 cases.
Separate full-reset/full-CODE/gate recovery passed257 same-reset cycles,
1,029 READY stages and514 accepted/published blocks, with all168 fixture
vector/space/clock combinations independently asserted and1,542 confirmed
DMA phase ACKs and ENC ACKs each. That AES recovery left the corrected
12,765-byte image at READY016A/config22/RC16, completed/heartbeat1 and fault
latch0; this state is historical after the PRNG programming below.
Parent serial `all test` passed for both corrected boards; all fourteen older
BIN sizes/hashes independently match published baselines. No hosted-CI pass
is claimed here.
Generic has no physical AES evidence. All fourteen older BINs and non-AES
platform/debug drivers stayed unchanged; that addition brought CI to sixteen full jobs with the same
seven-file whitelist and `hardware_tested=false`. M2 #4 remains open.

A further isolated [deterministic PRNG foundation](ARCHITECTURE.md#isolated-deterministic-prng)
implements explicit valid seed loading and one bounded 13-shift hardware
command returning the 16-bit LFSR state. This is **not entropy or a
cryptographic RNG**. The two forbidden fixed points and two 32,767-state cycles
are exhaustively host-checked; real SDCC calls have linked/alias-aware synthetic
coverage; board-specific hardware evidence is recorded separately below.
Exclusive ADC/PRNG/CSP history and stable awake ownership are prerequisites,
not inferred from ST=0 alone.
That isolated foundation added no board image or hardware runner. The separate
[PRNG board fixture](DEBUGGING.md#deterministic-prng-board-fixture) now links
the unchanged service on both boards, with explicit wire ABI, guarded manual
short/full/stopped modes and eighteen full CI jobs using the same seven
artifacts. Four full periods cover all65,534 valid states per clock; small
seed/reseed cases and a real fixture-only RCTRL11 precondition violation
exercise benign/terminal outcomes without inventing a CPU-hold timeout.
All sixteen older board BINs and existing drivers remain unchanged.
The original wirev1 7,224-byte LG PRNG image passed
[short hardware acceptance on 2026-09-17](DEBUGGING.md#2026-09-17-lg-prng-short-acceptance):
two seed1234 loads, eight RC16 words, repeated non-advancing readback and
benign mask15 with preserved guards/tails. Its
[first long run exposed a fixture flag-policy bug](DEBUGGING.md#2026-09-17-lg-prng-long-run-flag-policy-interruption):
natural STIF assertion after the C snapshot, not a PRNG failure.
The wirev2 correction permits only sticky STIF0->1 and tracks ordered C/live
observations without flag clearing, compare writes or shorter periods.
The old wirev1 halt is historical; its last32 words were not host-accepted.
The parent then programmed the unchanged7289-byte wirev2 and passed
[corrected short hardware acceptance](DEBUGGING.md#2026-09-17-corrected-lg-prng-short-acceptance):
eight RC16 words, six raw flag observations and no STIF transition.
The same installed wirev2 then passed
[full-stopped hardware acceptance](DEBUGGING.md#2026-09-17-corrected-lg-prng-full-stopped-acceptance):
131,084 individually checked words, four32,767 periods/all65,534 states per
clock, real C/live STIF race with2,145 later preserved observations, and
genuine stopped-probe/re-entry6/6/6 with unchanged caller data.
The same image then passed
[separate full-reset recovery](DEBUGGING.md#2026-09-17-corrected-lg-prng-full-reset-recovery-acceptance),
repeating the entire corpus after its own reset/full-CODE proof. A distinct
`c-snapshot` STIF transition and2,253 later preserved observations ended at
ENDREADY016A/config26/RC16, fault0 and C/live IRCON80, without executing the
probe or resuming afterward. **The bounded LG short/stopped/reset-recovery
hardware gate is complete.** The earlier stopped FAULT is historical;
recovery was a new reset epoch, not cleared C state or automatic continuation.
Parent independently completed both original-image `all test` runs (429 Python,
33 segments/full131,084 synthetic words plus probe/9 faults),150-file guard
and sixteen older hashes before this physical finding.
For wirev2, both parent LG/generic `all test` runs completed with exit0,
434 Python tests each and the full continuation/corpus/STIF/probe/fault
evidence. Parent MAC alias,150-file repository/local-link and diff checks
also passed; `hardware_tested=false` was verified.
Generic/EOC1/physical poll-fault and broader RF/noise entropy, security and
sleep acceptance remain open. M2 #4 stays open; no hosted-CI pass is claimed.

The [isolated passive RX foundation](RADIO_RX.md) now has host, linked-image
and alias-aware synthetic evidence for channel configuration, one-frame
CRC/footer reception and checked soft-stop/flush. The subsequent
[bounded board fixture](RADIO_RX.md#bounded-passive-rx-board-fixture) now links
real startup/board/clock/RX for both boards with a fixed16-attempt cap and
separately authorized private-capture runner. The older18 BINs are preserved
and CI now covers20 offline
board jobs with the same seven artifacts; no fixture result closes M2/M3.
The [2026-09-18 parent-observed failure/probe](DEBUGGING.md#2026-09-18-lg-rx-fscal1-failure-and-probe)
identified an FSCAL1 reserved-bit guard error. Its narrow low-bit-readback
correction passed [bounded LG acceptance](DEBUGGING.md#2026-09-18-lg-bounded-passive-rx-acceptance):
one initial frame, a retained pre-configuration TIMEOUT, then an explicitly
reset16-attempt run with14 published bodies matching a concurrent Nordic
capture and two nonpublishing BAD_CRC results. Reuse after BAD_CRC and the
terminal16-attempt cap were observed; final END016F had RX off/empty FIFOs.
Generic, independent on-air FCS, calibrated timing/metadata and broader RF
fault recovery remain unobserved. This finite RX slice does not complete M2/M3.
Transmission in board firmware, automatic ACK, calibrated metadata,
continuous reception and MAC/networking remain unsupported; M2 #4 stays open.
The separate offline TX/CCA preparation below does not change this record.

The [reserved flash read foundation](ARCHITECTURE.md#reserved-flash-read-foundation)
now reserves physical pages125/126 and provides bounded, staged reads with
strict chip, mapping, controller and output-ownership checks. It is isolated
from all board images except the separately selected flash fixture and has host, exact-image and alias-aware synthetic
evidence only. Page127 and the information page are excluded.
The subsequent [internal RAM executor](ARCHITECTURE.md#internal-ram-flash-command-executor)
implements command/address/data writes from a copied, verified 123-byte RAM
path, with a genuine idle-before-return ABI and retained RAM-only fail-stop.
It has host, exact-image and alias-aware synthetic evidence, not hardware
observations. Controller-idle is not verified flash content.
The [public reserved-page writer](ARCHITECTURE.md#verified-reserved-page-erase-and-program)
now composes these services for actual erase/program commands, complete
readback, verified-erase-only epochs and one attempt per word. History is
unknown after reset, regardless of allFF contents; runtime faults retain
their causes and active-controller exhaustion stays in RAM. This isolated
composition has host/image/simulator evidence. The subsequent
[boot-disarmed non-RF board fixture](FLASH_FIXTURE.md) now implements #8's
offline preparation, linking the unchanged services for both board definitions.
ARM/RUN are separate bounded mailbox transitions; default boot/reset/resume
performs no command. One selected page receives one erase and two verified
programs with explicit history checks and terminal outcomes, without retry.
Private backup/full excluded-region verification and destructive scope require
new authorization. Physical mapped-RAM debugger visibility, flash timing/effects
and reset/interruption recovery remain blocked/unobserved; neither board has
flash-fixture hardware evidence. #8 and M2 stay open; durable NV remains separate.

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

The [bounded radio ownership queues](RADIO_QUEUE.md) now implement two RX
copies, one TX candidate and a four-cookie ISR/foreground request ring.
One foreground service invokes the real bounded passive receiver; full
queues preserve existing ownership, BAD_CRC publishes nothing and other
driver errors retain their cause. Reentrant ISR storage, real generic C52
preemption, complete linked MMIO and combined memory budgets are checked
offline. These are software queues, not TX, ACK, a radio ISR, continuous
reception, association or M3 hardware acceptance.

The separate [bounded TX/CCA primitives](RADIO_TX.md) now implement direct
TX, controller-gated TX-on-CCA and CCA-only sampling, with real FIFO/timebase
composition and fresh completion/idle checks. All operational faults retain
their first result; busy CCA requires verified shutdown before explicit reuse.
Both board definitions have host/image/alias-aware simulator evidence, not
physical RF evidence. These init-time owners cannot be mixed with legacy RX
in the same reset epoch. Queue/controller composition, a boot-disarmed RF
fixture, on-air acceptance, physical ACK/retries and association remain open.

The separate [offline MAC transmission scheduler](MAC_TX.md) now implements
one copied unsecured DATA transaction with unslotted CSMA-CA, legacy ACK/DSN
matching, finite retries and lifetime/work bounds. It reuses the actual codec,
retains the DSN/body across retries and requires confirmed quiescence before
radio ownership can be released. Host, genuine linked-image and alias-aware
simulator evidence cover synthetic symbol-time events, not RF timing.
There is no real adapter or post-TX ACK receiver: the existing reset-exclusive
TX/RX owners cannot fulfill that contract by simple composition. #13's
physical/integration gate remains open, and the standalone image's narrow
IRAM headroom is not complete-stack or interrupt-nesting acceptance.

The [awake MAC Timer foundation](MAC_TIME.md) (#39) now establishes bounded
one-shot asynchronous initialization and coherent raw live tuples, using the
actual Sleep Timer for independent deadlines and the TI low-FF workaround.
Both board definitions have host/image/simulator evidence. Positive periods
512/`FFFFFF` imply a non-power-of-two coarse modulus, not an extended symbol
epoch. Radio quiescence remains mandatory; this is not a same-reset RX/TX
owner. The primary sources' SFD-rise versus TX-end capture discrepancy,
capture freshness/overwrite, epoch/fine-phase conversion and physical timing
remain prerequisites for the real adapter, tracked separately in #40.
Neither a live sample nor raw Sleep Timer ticks may replace captured TX/ACK end.

Initial #14 preparation additionally admits canonical unsecured Beacon
Requests through that same codec/scheduler, DSN owner and CCA/IFS bounds.
They require neither ACK nor frame retries. This does not yet implement
channel walking, bounded Beacon receive windows, candidate selection or
association, and does not waive #13's real-adapter gate.

The independent [four-entry Beacon candidate collector](NWK_CANDIDATES.md)
now composes the real MAC/NWK decoders with explicit CRC/channel inputs,
preliminary profile2/BO15/permit/ED-capacity filters, copied metadata,
duplicate updates, valid withdrawal/compaction and full-table rejection.
Both board definitions have native/sanitizer/linked evidence. This is not
active scan completion or complete normative parent selection: target-network
choice, link cost, freshness/update-ID policy, actual receive windows and
association remain open. The record/table are metadata, never membership.

The [offline active-scan controller](MAC_SCAN.md) now adds bounded ascending
channel walking, real Beacon Request admission and serialized transmitter
pumping, explicitly confirmed receive windows, copied candidate ingestion and
restoration of saved logical radio state. Host/sanitizer, linked-image and
alias-aware checks cover both board definitions, including partial/unscanned
masks, cancellation and retained restoration faults. This is not a physical
scanner or full MAC PAN-descriptor service: #13/#40 adapter/timestamp gates,
full parent policy and association remain open. The composed test's 32,715-byte
CODE leaves only 53 bytes below the unbanked limit; no full-stack fit follows.

**Offline preparatory implementation:** the [legacy body codec](MAC.md)
encodes/decodes a bounded DATA/ACK subset plus five fixed-format commands and
their addressing layouts, with host, linked-image and
alias-aware simulator evidence. It also handles version-0 Beacons without GTS
descriptors, bounded pending-address lists and opaque upper-layer payloads.
This adds no scan, synchronization, scheduling or Zigbee discovery procedure.
The separate [R22 NWK Beacon payload decoder](NWK.md) now decodes the exact
15-byte upper-layer metadata, with independent host/target tests and an
offline MAC-to-NWK slicing check. Profiles, capacities and identifiers remain
unauthenticated metadata, not selection/admission decisions. It does not
satisfy M4/M5 or the remaining BDB specification/implementation gates.
An independent [NWK Data codec](NWK.md#nwk-data-frame-codec) also serializes
bounded unsecured unicast/broadcast NPDUs and optional IEEE fields, with
host/target evidence and maximum-body MAC integration. Security and extended
routing layouts remain explicit errors, not successful substitutes.
These codecs are not linked into board firmware and
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

Independent generic preparation now includes the
[two-page snapshot journal](NV_RECORDS.md): bounded versioned records,
nonwrapping generations, full-page CRC, commit-last replacement, explicit
degraded recovery and runtime erase-attempt limits over the real flash
services. Host/image/simulator checks include command cuts, torn patterns
and genuine reset/retained-RAM paths. No physical durability, lifetime-wear,
security-counter, key or membership acceptance is implied. This generic
foundation does not waive the security/commissioning entry gate below.

Entry gate: the BDB v3.0.1 base revision and security/commissioning requirements
are pinned in the conformance ledger. Obtain/review applicable errata
21-65431 and resolve affected requirements before implementing these procedures.

Deliver:

- Zigbee-specific AES-CCM* nonce/header/MIC handling, using the AES primitive.
- Network/link-key handling, key identifiers/sequences and replay rejection.
- Two network-key slots and the applicable Trust Center key procedures.
- Install-code CRC/AES-MMO derivation and explicit initial/updated TC key
  state; receiving a key or confirmation frame alone is not verification.
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

**Offline preparatory implementation:** the independent [APS Data codec](APS.md)
now serializes a bounded normal-unicast subset with endpoint/profile/cluster
metadata, counter and ACK-request. Standalone and real MAC/NWK/APS composition
have host, linked-image and alias-aware simulator evidence. Unsupported APS
security, broadcast/group delivery and extended headers fail explicitly.
This implements no transaction/ACK state, endpoint dispatch or board caller
and does not close any M4/M5 security, commissioning or interoperability gate.
ZCL revision/device selection and implementation remain separate M6 work.

Deliver:

- Required NWK and APS header handling and bounded transaction/ACK state.
- Centralized-network steering, key transport/verification and join completion.
  Follow BDB 3.0.1 section 8.2, including its final permit-join broadcast;
  this does not enable local child admission. Already-joined steering remains
  optional and is not initially selected.
- Join-critical endpoint-0 services, including Node Descriptor exchange and
  address/Device Announce handling. Introduce the generic ZDO unsupported-service
  fallback as soon as endpoint 0 is exposed, before omitting any optional handler.
- Device Announce, address-conflict handling and required address resolution.
- ED Timeout negotiation, parent information and selected keepalive method.
- On persisted resume, restore BDB state and attempt secure NWK rejoin as
  required by BDB section 7.1, then announce on success. Retain ED Timeout
  negotiation after every successful join/rejoin and prompt keepalive after
  recovery using `nwkParentInformation`; unknown information or an absent
  parent needs bounded failure/recovery, not an indefinitely blocked startup.
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

M5 is an authenticated lab ED milestone, not full BDB conformance or a claim
that all discovery/application-profile requirements have been completed.
M6 finishes those surfaces and their interoperability evidence; distributed
security support remains outside the first configuration.

### M6 - Discovery, ZCL and receiver-on interoperability

**Offline preparatory implementation:** [ZCL Revision 8 wire codecs](ZCL.md)
now handle global/cluster-specific headers and 38 bounded wire-value types.
A separate read-only model and unicast Read Attributes handler add bounded
lookup, read-access checks, protocol status records, explicit partial counts
and atomic response construction, without registering a cluster.
A one-cluster unicast dispatcher adds sorted Discover Attributes pages,
Read selection, unsupported-command responses and explicit no-reply handling
for received Default Responses and unsupported Write No Response.
The primary PDF and Foundation 14-0126-17 are pinned; approved errata 19-2019
remains an open follow-up risk, not a stop on base-text development. Review
applicable corrections before conformance claims. Header/value, APS/ZCL and
read/dispatch target checks are host-tested, image-checked and simulated;
full Discover-then-Read/ZCL/APS/NWK/MAC composition is host-tested and also
image-checked/simulated in the integrated resource harness.
There is no board caller, endpoint/transport dispatcher, native
numeric/charset conversion or advertised cluster. Application/device/profile
selection and all M4/M5 networking/security gates remain open.

Deliver:

- Completion of required ZDO discovery clients/servers, retaining the generic
  unsupported-service fallback introduced with endpoint 0 in M5.
- Small source-binding storage and the management behavior it requires.
- Full `Mgmt_Leave` (`0x0034`), full `Mgmt_Bind` (`0x0033`) when source bindings
  exist, and a tested status-only `NOT_SUPPORTED` response for omitted
  `Mgmt_Lqi` (`0x0031`). Resolve this omitted-service configuration against
  BDB section 6.6 and the selected BDB test requirements before conformance claims.
- Pin the application/device class and its BDB finding/binding, Identify,
  binding/group-capacity and default-reporting requirements. A single endpoint
  does not by itself make finding/binding optional.
- Integrate the generic read-only attribute model with the selected device's
  real types/access/range requirements and transport; writes remain separate.
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

**Measured preparatory integration:** `make test-protocol-budget` now links
and executes the complete implemented MAC/NWK Data/APS/ZCL codec and
read/discovery chain: **22,829 CODE and 1,500 ordinary XDATA bytes**, plus
64 reserved status bytes. Per-object and total budgets are enforced; see
[the resource ledger](ARCHITECTURE.md#integrated-protocol-resource-budget).
The first combined link failed on IRAM, prompting ABI-compatible spill/
leaf-emission changes rather than weakened guards. Persistent protocol IRAM
fell from 154 to 77 bytes; observed SP reaches `0x7A` with stack start `0x66`.
Only five bytes remain before the current upper-IRAM guard on these vectors.
This is not enough evidence to budget interrupt nesting or promise complete
stack fit. Platform/radio queues, security/NV, ZDO and application state are
still outside this integrated image.

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
Update it when measurements or normative review change a decision.
The [ordered issue roadmap #5](https://github.com/faronov/cc2530-zigbee/issues/5)
now splits the remaining work into dependency-linked implementation and
evidence tasks. Mark completed acceptance checkboxes as evidence lands;
do not close a whole milestone for a bounded subtask. This document remains
the canonical dependency and acceptance plan.
