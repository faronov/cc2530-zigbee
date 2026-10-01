# Explicit one-frame real-MAC laboratory fixture

This is **not normal firmware startup, a join test, an ACK/retry test, or a
hardware operator**. The explicit build and all tests are offline. Hardware
discovery, backup, selection, programming, capture and recovery are separate
operator responsibilities. No build/import/test enumerates or opens equipment.
Generic simulation supplies synthetic peripheral observations; it does not
establish CC2530 timing, RF reception, power, electrical or USB compatibility.

## Deliberately narrow behavior

The real board `_sdcc_external_startup` disables interrupts and, on LG Rev0.3,
executes the unchanged display-off GPIO policy before CRT initialization.
The owner requires a genuine full-SoC-reset epoch after programming and
exclusive foreground ownership. Merely loading PC0 or clearing C state is
not a reset and does not establish that history.
The fixture initializes the existing32-byte M0 record inside its unchanged
64-byte reservation. It boots **DISARMED**, with no clock/radio service.
There is no runtime flash writer, NV/key service, random generator, display
operation, join, or hidden parameter/private-state command.

Two complete8-byte mailbox commands are accepted, only at common-CODE WAIT:

| Current phase | Bytes, hexadecimal | Result |
|---|---|---|
| DISARMED1 | `A9 56 1A E5 36 C9 4D B2` | ARMED2; no MMIO |
| ARMED2 | `56 A9 1A E5 C9 36 4D B2` | ADMITTED3; no MMIO |

Each command is consumed/cleared. Zero mailboxes consume one of256 admission
polls per phase; exhaustion and malformed/out-of-order commands are terminal.
RUN admission returns to WAIT. With the operator's live WAIT breakpoint,
**one further separately admitted continuation**
enters RUNNING4 and completes without any mid-sequence debugger intervention.
This separation is a debugger admission convention, not a hardware interlock:
a free-running CPU proceeds on the next poll after a valid RUN mailbox.
Only the mailbox is writable operator input; status, owner contexts, compiler
homes and lower-service objects are never command parameters.

The uninterrupted path calls the unchanged genuine `mac_adapter` owner:
clock selection → Timer2/epoch/radio init → independent close/drain →
MAC interval owner init/submit → observed scheduler RANDOM/ATTEMPT →
real unslotted CSMA/CA → actual stop/drain/retirement → public MAC slot release.
STOP_RX is selected. Real adapter observations, not invented completion
events, drive the scheduler. Invalid/fault outcomes never become success.

**Initial adapter init enables RX/AUTOACK.** An eligible received frame can
therefore cause an automatic ACK before the close. This is explicitly not a
bound on *all* RF packets. The selected ordinary DATA has no Ack Request, so
the scheduler makes at most one ordinary transmission and no ordinary retry.
Five CCA-busy attempts can terminate with zero ordinary TX. Initial/drain RX
observations are consumed through the public owner API and counted, not
interpreted as reception of our transmitted frame.

### Public body and laboratory draws

FCS-free body,13 bytes:

```text
41 88 5A 34 12 FF FF 78 56 4D 41 43 31
```

This is DATA, no security/no ACK request, PAN compression, PAN1234,
short destinationFFFF, short source5678, marker `MAC1`. The real scheduler
assigns DSN5A; the immutable CODE template has placeholder DSN00. PANffff DATA
is intentionally **not** used: the existing real MAC rejects that policy.
The TX FIFO PHR is15 (body plus hardware FCS); the radio generates the FCS.
The receiver must compare the public bytes, not treat local SENT as delivery.

CODE draws are `{7,11,19,23,29}`. They are fixed public laboratory backoff
inputs, **not entropy or cryptographic readiness**. Each RANDOM action receives
one matching generation/retry/nb event with the actual live epoch stamp.
The tested clear path uses1 draw,1 ATTEMPT,1 QUIESCE (3 actions total).
The five-busy path uses5 draws and5 ATTEMPTs (10 actions total).
No successful RANDOM service stub or PRNG dependency is linked.

### Deadlines and terminal ownership

Each synchronous service uses timeout1024 raw awake ticks and poll limit256.
The MAC submit lifetime is62500 symbols and work budget4096. The foreground
also permits at most4096 outer progress steps and checks a65536 raw-tick age
before each step, with24-bit subtraction and half-range ambiguity rejection.
These are clock-domain units, **not calibrated wall-clock assertions**.
The outer bound is observed between synchronous operations: one last bounded
service can overrun it. CPU progress and live time are preconditions; this is
not a hardware watchdog or a proof against halted/failed clocks.

END5 requires genuine adapter OFF, no held observation/ready delivery/goal,
actual radio OFF/OFF_NOACK without errors, scheduler DONE/no retry, then
successful public slot release. Its outcome is SENT1 (one confirmed local
ordinary completion) or CCA_BUSY2 (five busy observations, zero TX).
SENT is not a sniffer/coordinator reception claim. RXENABLE clearing,
physical idle and empty RX FIFO come from the unchanged lower stop/drain
contract, not from rewriting diagnostic state.

FAULT6 preserves the first fixture reason and lower owned state. Work/time/
controller/clock/protocol uncertainty does **not** trigger automatic close,
flush, retry, reset or recovery. RF may remain active on a fault. Even END
does not transfer the clock/radio epoch to another API. Reinitialization and
terminal repeats cannot clear history or transmit again. END/FAULT are common
terminal loops, not sleep/power-management services. A separate bounded bench
operator must halt at the checkpoint and manage the authorized recovery;
there is no assertion of indefinitely safe unattended power or RF behavior.

## Byte ABI

`mac_smoke_status_t` is64 bytes and contains only bytes/byte arrays.
Numbers wider than a byte are little-endian arrays.

| Offset | Field |
|---:|---|
|0..3|`MAC1`|
|4,5|version1, size64|
|6,7|phase, reason|
|8..11|stage, consumed, completed, fixture outcome|
|12..16|last adapter result, MAC result, adapter's lower result, adapter phase, MAC phase|
|17..23|draws, actions, ATTEMPT actions, busy events, sent events, retired events, RX events|
|24..27|channel26, raw power05, body length13, DSN5A|
|28..29|remaining admission polls|
|30..31|outer progress steps|
|32..34|elapsed raw awake ticks|
|35..38|last adapter live symbols|
|39..45|transmissions, held, ready, MAC pending bit, adapter goal, normal_rx, released|
|46..50|radio phase, last radio result, errors, flags0, flags1|
|51..52|guards69/96|
|53|actual MAC outcome|
|54..63|reserved zero|

`pending` is the MAC's Pending bit, **not** a fabricated slot-allocation flag.
The released bit and actual final IDLE phase establish public slot release.
Reason values are defined in `include/mac_smoke.h`: NONE0, PACKET1,
ADMISSION_EXPIRED2, INVARIANT3, ADAPTER4, MAC5, TIME6, WORK7, DRAW8, TERMINAL9.

Fresh SDCC4.2.0#13081 exact layouts:

| Object/checkpoint | LG Rev0.3 | generic |
|---|---:|---:|
|status XDATA|086B|086B|
|mailbox XDATA|08AB|08AB|
|WAIT common CODE|787C|7854|
|END common CODE|787E|7856|
|FAULT common CODE|7881|7859|

The unchanged banker has a separate common fail-stop at0066, with its
error byte in real DATA1F. Reaching it is an uncertain terminal failure,
not END; the fixture status may still say RUNNING. It must never be hidden
as a successful status or followed by automatic resume/reset/retry.

These addresses are admitted only together with the complete image/metadata
pins. They are not a reason to bypass image identity or to use virtual IHX
addresses as physical flash offsets.

## Build, placement and provenance

```sh
make BOARD=lg_esl29_rev03 BUILD=build/lg-smoke mac-smoke
make BOARD=lg_esl29_rev03 BUILD=build/lg-smoke test-mac-smoke
make BOARD=generic BUILD=build/generic-smoke test-mac-smoke
```

Output directory is `BUILD/mac-smoke`. `mac_smoke.ihx` contains ASlink virtual
addresses; **do not program it as a linear image**.
`mac_smoke.physical.hex` is an independently round-tripped sparse physical
HEX made by the existing `tools/banked_image.py::pack/ihex` only after the
new exact image verifier succeeds. Highest physical byte is11A50, with holes
left absent. No output reaches reserved physical3E800..3FFFF (NV pages125/126
and lock/configuration page127); the information page is never a destination.
This is not permission to erase those pages or to program without preservation.

The shared native model lexically includes the existing adapter trace-layout
header even without trace mode. Its two host recipes therefore depend on the
genuine checked adapter header in the separate `BUILD/mac-adapter` directory.
That dependency neither links the simulator caller into the smoke image nor
makes the old adapter image safe to program. CI adds two separate offline
workers, retains all prior workers and uploads neither new banked image.
The ten-process native corpus is selected explicitly with
`test-mac-smoke-native`; the generic fast selector does not execute this
argument-driven shell loop and cannot claim it passed.

The accepted common/MA_BANK1/MA_BANK2 ABI and per-module compiler DATA bases
are unchanged. Two new real source-owned DATA arrays back08..1D and23..4B;
they are not absolute aliases, pretend allocations or an implicit RAM pool.
Banker bytes1E..1F, physical bit bytes20..22, runtime OSEG4C..55 and
stack56..7C remain separate. XDATA1F00..1FFF is the256-byte IRAM alias,
not extra capacity. M0 remains1E00..1E3F; ordinary XDATA is2539 bytes below1E00.

LG uses58149 populated CODE bytes, generic58109 (including CONST); both have59 common CONST
bytes, bank1=20148, bank2=6737. LG common CODE ends before CONST at79E5;
CONST ends7A20 exclusive. Complete DATA liveness checks157 functions on LG
(156 generic),634 cross-call live-byte combinations, not raw overlapping
module sums. No lower production source, old budget or old verifier pin is
changed. The native model's address mapping is synthetic; the independent
linked verifier establishes genuine source/compiler/libc placement.
After the later committed scan, RX-overflow and time-guard lower-service
changes, the current images use59339 (LG)/59299 (generic) CODE bytes with
bank2=6830, LG CONST7E2E..7E69 exclusive, highest physical byte11AAD,
2537 ordinary XDATA bytes, a172-site lower MMIO inventory and liveness over
156/155 functions with560 combinations.

Raw CDB bytes are pinned before decoding. Complete CODE/address bytes,
source objects, immediately snapshotted relocated listings, memory report
and all MAP metadata are bound. Only existing output-path comments and
ASlink's precisely checked BUILD-path file rows vary; source/CODE metadata
is not normalized. Full CDB identities supplement ASlink's truncated names.
The exact image has no test caller or model linked.

All new source/test/tool/documentation work is original BSD-3-Clause.
SDCC startup/runtime dependencies retain their upstream licenses; no SDK,
programmer implementation, private capture, identity or key is imported.
The unchanged hardware basis was checked against:

* TI **SWRU191F**, April2009/revised April2014, §2.2.2/Figures2-2/2-3 and
  §2.2.5 FMAP/MEMCTR: independent bank window and XDATA mapping.
* Same revision §23.8 pp218–222 and immediate instructions pp253–254:
  CCA-qualified ordinary TX, FIFO/body/PHR semantics; §23.9.5 pp224–226
  and §23.9.8 pp231–232: filtering and AUTOACK eligibility;
  RXENABLE/RXMASKCLR p260, FSMSTAT0/1 pp262–263 and §23.10 pp232–233:
  soft-stop/drain and FIFO errors. Table23-6 p256 and TXPOWER/TXCTRL p262
  support the unchanged channel/power/settings policy.
* TI **SWRZ031**, April2009, §§1.1–1.2 pp2–3: DMA variable-length and
  Timer2 latching errata. No DMA service is added; the accepted Timer2
  reader/erratum handling remains unchanged.

See the existing [radio owner contract](RADIO_AUTOACK.md),
[adapter contract](MAC_ADAPTER.md) and [bank ABI](BANKED_ABI.md).
This fixture establishes neither new silicon behavior nor hardware acceptance.

## Offline validation observed for this image

Both boards pass10 fresh-process native and10 ASan/UBSan scenarios, including
terminal public re-init/re-poll immutability. Ten verifier regression tests
per board reject complete CODE/CDB/CR/NUL/metadata corruption, moved/overlapping
regions, missing/swapped objects/listings and reserved physical destinations.
The output-path-only positive case passes; wrong object basenames do not.
The startup stop must be the actual linked `main` before any boot snapshot
is consumed; an early/failed stop is rejected directly.

The exact installed images, not simulator callers, pass these genuine s51/C52
instruction replays per board with the CC2530 IRAM alias decoder:

| Scenario | MMIO events | Peak SP |
|---|---:|---:|
|one noACK broadcast completion/retirement|8293|78|
|five CCA_BUSY, zero TX|43588|78|
|clock initialization fault|2577|74|
|outer raw lifetime fault|2298|74|
|malformed ARM|0|59|
|256 empty admission polls|0|59|
|RUN before ARM|0|59|
|mailbox nonempty after RUN admission|0|59|
|uncertain TX completion/work-limit fault|10775|78|

Each scenario includes real boot/default/command processing and a1024-
instruction terminal repeat with unchanged machine/owner state. The positive
sequences traverse ARM and RUN separately before the admitted continuation.
Every simulator subprocess retains the existing15-second deadline. Whole
scenario runtime is longer: the conservative admission replay saves/restores
complete machine images for each of256 polls; it is not a fast native check.

Both boards total67531 replayed MMIO events. SP78 uses35 of39 reserved bytes;
7D..FF remains the131-byte canary, and XDATA1F00..1FFF matches IRAM on every
complete snapshot. The new initializer/poll/snapshot/fault/progress functions
emit **zero PUSH/POP instructions**. Direct service calls retain the accepted
caller envelope; snapshot-only helper nesting does not wrap active service
calls. This is simulated stack evidence for this fixture, not a hardware
observation, a general interruptible-caller budget or a combined join proof.

The linked CRT and board startup run from PC0 with dirty IRAM/XDATA before
initialization. Peripheral inputs, however, are seeded from the synthetic
model's post-startup values, not a complete CC2530 reset model. Startup GPIO
and interrupt-register writes are not part of the compared MMIO transcript.
A genuine whole-SoC reset and exclusive hardware history therefore remain
separate operator gates; this simulation is not proof of them.

## Manual operator and physical evidence boundary

`tools/check_mac_smoke_hardware.py` never discovers USB, flashes firmware,
starts a receiver, enables DMA, or retries a trial. It requires the exact
board's full linked artifacts and sparse physical HEX identity, explicit
bus/address, separate permissions and a new0600 capture in a user-owned0700
directory outside Git. The image must already have been installed under a
separately reviewed preservation/programming plan.

The explicit `--confirm-erased-gaps` policy requires FF in the image's
unaddressed internal holes. This is an installation/preflight requirement,
not a claim that sparse HEX specifies those bytes. After genuine reset/halt,
the operator independently reads all72273 bytes of the physical image extent,
including every hole and both populated upper banks, with a300-second overall
preflight bound and10-second transport-operation cap. It verifies CPU/FMAP
preservation before the first resume. It does not verify the remaining
main-flash suffix, NV/lock or information page; the programming plan must
separately preserve and compare them.

Four common breakpoints cover WAIT, END, FAULT and the banker fail-stop.
The60-second admission/experiment deadline starts only after physical CODE
verification. Initial/default/ARM/RUN inspections require the exact status,
zero mailbox, unchanged M0, zero actual lower/private prefix and zero public
TX/action/random/clock contexts. Read-only IRAM-alias inspection checks banker
depth/fault. No status or private state is written. The only operator writes
are the two complete mailbox packets. After ADMITTED, the RF path runs
uninterrupted to a terminal breakpoint.
The M0 record remains byte-for-byte unchanged even at END: this fixture
never calls `bringup_tick`. Its own `completed=1` is not an M0 heartbeat.

Normal END requires the published retirement/release invariants and bounded
SENT or five-busy accounting. SENT may follow earlier busy CCAs; live time,
work counts, received frames and radio flags are not compared to one synthetic
transcript. Banker failure is rejected before applying ordinary SP/DPS/bank
expectations. Complete raw status, private-prefix/frame storage and fault
material are saved privately before semantic checks. Capture, transport,
timeout or validation failure does not trigger resume/reset/flush/recovery.
An error after RF admission can leave the radio active.

For example, after separately verifying installation and preparing independent
receive-only capture, replace the bus/address and private path:

```sh
python3 -B tools/check_mac_smoke_hardware.py \
  --bus BUS --address ADDRESS --board lg_esl29_rev03 \
  --output build/lg-smoke/mac-smoke \
  --sha256 2a499e591e591b29878d55f977cf6ad91b77e8b95a5c4438f5aac5d506eaa703 \
  --capture /ABSOLUTE/PRIVATE/NEW/mac-smoke.jsonl \
  --allow-target-reset --allow-cpu-control --allow-memory-access \
  --allow-memory-write --allow-breakpoints \
  --confirm-rf-including-autoack --confirm-erased-gaps
```

`--admit-only` stops at ADMITTED without radio startup. The Python entry point
also accepts a `before_rf` callback for a parent-owned, bounded receiver
startup/check after admission; callback failure prevents RF continuation.
There is no CLI plugin, receiver firmware installer, or automatic second run.
Synthetic operator tests cover all transport-call failure boundaries, changed
CODE/gaps, admission/private-state corruption, capture/receiver failure,
banker/ordinary faults, deadlines and no success output after cleanup failure.

The prerequisite physical reader passed full Actions36304569355,116/116 jobs,
at `df1f0c7`. A separately authorized320-byte hardware preflight preserved
CPU/FMAP and matched the full predecessor backup while selecting banks0..7.
All upper banks were erased, so that is control/restoration evidence, not
distinct populated-bank discrimination. The actual MAC/RF trial remains
separate from those prerequisites; the first #88 observation follows below.
Local SENT alone proves neither independent reception nor ACK/retry
interoperability, join or Zigbee conformance.

## First LG real-MAC hardware observation, 2026-09-27

**One actual MAC no-ACK broadcast and independent reception were observed.**
Only LG Rev0.3 was exercised, using the unchanged fixture code at
`833d6f5e18e76ca735a69467bf827ced99a3d37b`, after
[full Actions36308161366](https://github.com/faronov/cc2530-zigbee/actions/runs/36308161366)
passed118/118 jobs. The physical HEX identity is the LG hash above.
This is the real scheduler/adapter/clock/radio composition, not the earlier
primitive-only `TXF1`/`LNK1` demonstrations.

Fresh262144-byte predecessor and2048-byte information-page comparisons
preceded programming. The one guarded erase/write attempt reached100% write,
then its external read-verification failed at99% with USB I/O error. A
separately approved status-only recovery probe also failed with USB overflow;
both scopes stopped without automatic retry or application resume. After
the user's physical debugger reconnect, a fresh authorized read-only scope
performed reset-to-debug and independently compared all72273 physical CODE/
gap bytes with CPU/full-FMAP preservation. A subsequent complete262144-byte
main read matched the image and all-FF gaps/suffix, including the unchanged
NV/lock-page values; the2048-byte information page matched the retained
original. No second erase or programming attempt occurred. This verifies
content, not that the whole-chip erase spared the erased NV/lock cells.

The RF operator then performed its own reset and full physical CODE/gap
preflight, checked initial/default/ARM/RUN admission and actual zero private
state, and started the unchanged Nordic receiver only at halted ADMITTED.
One uninterrupted RF continuation reached common END. No live time,
peripheral-success flag or private owner state was injected.

| Hardware observation | Result |
| --- | --- |
| Profile | Channel26, raw TXPOWER05, fixed public laboratory addresses/draws |
| Real MAC actions |1 RANDOM using public draw7,1 ATTEMPT,1 QUIESCE; zero busy observations |
| Ordinary TX/accounting |1 transmission,1 SENT event,1 RETIRED event; no ordinary retry |
| Independent capture |1 complete PCAP/TAP record, exactly matching the13-byte public `MAC1` body |
| Local terminal | END787E, stage6/reason0, consumed1/completed1, fixture SENT1/MAC UNACKNOWLEDGED2 |
| Ownership | Adapter OFF2, radio OFF_NOACK7/STOPPED5, public slot released; held/ready/pending/goal/normal_rx all0 |
| Radio errors / received heads |0 /0 |
| Foreground observations |10 progress steps,1255 raw elapsed ticks, last live symbol2264; not calibrated duration |
| Final debugger state | Halted END787E, status2B/config26, checkpoint SP57/DPS0/FMAP1; banker depth/fault0 |
| Final receiver | Sleep commanded, channel26 read back, serial port released |

The first operator invocation falsely rejected the successful END because
it incorrectly equated M0 heartbeat with the separate fixture completion
byte. Real firmware kept M0 unchanged, correctly. The resulting operator
exception stopped the receiver early rather than completing its requested
90-second window. The preserved81-byte PCAP nevertheless has a complete
header and one complete matching record; no truncated tail was accepted.
The heartbeat check and synthetic operator model were corrected, including
a regression rejecting a changed M0 heartbeat. Offline reconciliation of
the original records plus a separate read-only inspection of the same halted
END confirmed the result. **No reset, resume or second RF trial was used
to correct this operator error.** Original failed-scope logs were preserved,
not rewritten as successful execution logs.

The operator/model correction at
`f2f38813e053aded1362af47b9d5625808286b41` passed
[full Actions36314030255](https://github.com/faronov/cc2530-zigbee/actions/runs/36314030255):
all118 jobs succeeded, including Required offline acceptance and every prior
worker. Firmware and the observed physical image remain unchanged from
`833d6f5`; this acceptance required no additional hardware operation.

This is FCS-free body equality, not independent FCS validation. Initial
AUTOACK was possible and not included in the ordinary-TX budget. There is
no hardware stack-high-water measurement, calibrated backoff/CCA/PHY timing,
positive ACK/retry/busy-channel experiment, loss-free RX claim, generic-board
RF result, full MAC conformance, Zigbee association or authenticated join.
The coordinator/Home Assistant network was unchanged. #13/#14 remain open.
Raw frames, backups, USB details and private recovery reports stay outside
Git and generated CI artifacts.
