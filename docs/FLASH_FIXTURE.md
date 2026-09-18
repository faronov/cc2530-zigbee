# Boot-disarmed flash fixture — offline preparation for #8

**No physical execution is authorized or recorded.** Both `generic` and
`lg_esl29_rev03` are host-tested, image-checked and simulated only.
#8 remains open. Earlier RX/M1 experiments do not authorize this destructive
experiment. There is no hardware runner, programmer integration or USB import.
Never flash `flash_test`, `flash_exec_test` or `flash_write_test` executables.
Only the separately selected `IMAGE=flash_fixture` is a candidate for a future,
explicitly authorized board task; being a board artifact is not permission.

## Firmware and deliberately bounded handshake

The real executor, reader and writer link **before every caller**, retaining
their published #7 CODE/ABI/private-prefix identities. GPIO belongs solely to
the existing board startup: generic changes no pins; LG Rev0.3 preserves its
existing display-off policy. No panel type/voltage is inferred. The fixture
does not select clocks, enable RF, IRQs or DMA, or implement flash algorithms.
It requires the services' awake undivided RC16/XOSC32, bank0/DPS0 and exclusive
mapping/controller ownership. The reset-default RC16 path is the intended
future first experiment; electrical/timing behavior is not simulated evidence.

CRT and `flash_fixture_initialize()` clear all eight mailbox bytes, publish
DISARMED and give the first phase 256 foreground polls. Initialization is a
boot entry, **not** a callable fault/history-reset operation. Every all-zero
observation decrements a private 16-bit budget once. The 256th empty poll
enters terminal TIMEOUT; the 255th leaves one opportunity to submit a packet.
This is an instruction-progress budget, not wall time; halting pauses it.
There are no timer dependencies, wraparound deadlines or unbounded busy waits.

At `flash_fixture_wait` **only while halted**, write one complete eight-byte
packet using separately authorized, verified ordinary-SRAM access. Do not
stream bytes while the CPU runs; this protocol is not atomic with an external
writer. Check the entire packet readback and CPU preservation before resume.
The mailbox is XDATA `01A4..01AB`; do not write the status, history, engine,
source word or unrelated RAM.

| State/transition | Bytes (hex; `p` is relative page0 or1) |
| --- | --- |
| DISARMED → ARMED | `A6 59 p (p XOR FF) 3C C3 69 96` |
| ARMED → RUNNING | `59 A6 p (p XOR FF) C3 3C 69 96` |

Observe ARMED with an empty mailbox and a **new** 256-poll budget before
submitting RUN for the same page. Both accepted packets clear the mailbox.
RUN admission itself performs no flash access and returns to WAIT with step0.
Any nonzero malformed/partial packet, premature RUN, replayed ARM, out-of-range
page or changed scope enters terminal PACKET fault without a command.
The protocol prevents accidental default execution; fixed public tokens are
**not authentication, unpredictable challenges or destructive-operation consent**.
After END/FAULT, further writes/resumes cannot start another sequence.
Reset discards authorization and history. No automatic retry or recovery exists.

One RUN performs these five foreground steps, separated by common-C WAITs:

1. Program offset0 must return `HISTORY_UNKNOWN=4`, without MMIO.
2. Erase only the selected 2-KiB page, using `flash_nv_erase`; the actual
   accepted command and all2,048 FF readbacks must succeed.
3. Program `12 34 56 78` at offset0, with real four-byte verification.
4. Repeat offset0 must return `WORD_USED=5`, without MMIO.
5. Program the same public pattern at offset2044, verify and enter END.

Thus there are exactly **one erase and two program commands** on success,
not an endless wear loop. The unselected reserved page remains untouched.
The controller poll limit is65,535 per admitted command, not an elapsed-time
guarantee. Unexpected history results give HISTORY fault; real service errors
give SERVICE fault, retaining the actual returned result and service diagnostic.
No fixture code clears or imports write history.

## Image, ABI and allocation

SDCC4.2.0 model-large, lower unbanked CODE only (`0000..7FFF`):

| Board | CODE bytes | SHA-256 | WAIT / END / FAULT |
| --- | ---: | --- | --- |
| generic | 4168 | `56206c0393eb59b11676b870a15979b87bb8614b64afd75f789de1d3181d7a9a` | `0D1D / 0D1F / 0D22` |
| LG Rev0.3 | 4208 | `b32325c16f63cc571078289b59c4cc081535b2e12fb981cbd5dc666824b70707` | `0D45 / 0D47 / 0D4A` |

These are **offline image identities, not actually observed boards/images**.
WAIT is NOP/RET; END and FAULT are NOP/relative self-loops. Revalidate the
actual artifacts/symbols, not copied addresses, before any later board task.

Both builds use436 ordinary XDATA bytes:404 bytes of complete service/compiler
prefix (`0000..0193`),16-byte fixture state (`0194..01A3`),8-byte mailbox,
4-byte source word (`01AC..01AF`),2-byte private remaining budget and2 bytes
of compiler scratch (`01B0..01B3`). All are linker-accounted, below`1E00`.
The fixture's separately measured allocation budget is **512 bytes including
the64-byte M0 reservation**:500 reserved /468 actually used with32-byte M0
status. No larger pool is needed; the existing reader/executor/writer budgets
are unchanged. XDATA`1F00..1FFF` still aliases IRAM and is never extra storage.

IRAM has register bank0 at`00..07`,17 compiler bytes at`08..18`, and the BIT
backing byte at`20`; the seven-byte gap is not a pool. Stack reservation is
`21..FF` (223 bytes), initial SP`20`, observed simulated peak SP`36` (22 bytes
above initial SP). Upper IRAM`80..FF` remains guarded in tests.
No new ISR/reentrancy, generic-pointer helper, heap or CODE banking is added.
The 123-byte copied engine remains XDATA`0009..0083` / mapped CODE`8009..8083`.

The16-byte fixture state is all byte fields, not a native enum/struct cast:

| Offset | Meaning |
| ---: | --- |
| 0..5 | `M2FL`, version1, size16 |
| 6 | DISARMED1 / ARMED2 / RUNNING3 / END4 / FAULT5 |
| 7 | NONE0 / TIMEOUT1 / PACKET2 / HISTORY3 / SERVICE4 |
| 8 | selected relative page0/1, or FF before ARM |
| 9 | next/in-flight step0..4, completed5 at END |
| 10 | last writer result, FF initially, PENDING10 before a call |
| 11 | checks: bit0 UNKNOWN rejection, bit1 USED rejection |
| 12..13 | remaining handshake polls, little endian |
| 14..15 | guards`69 96` |

`tools/flash_fixture.py` is an offline strict decoder/packet codec/image proof,
not a hardware tool. The live writer diagnostic is at`00D1..00D8`.
The nine-byte executor work area at`0000..0008` contains command,4 word bytes,
limitLE,result,lastFCTL. Do not conflate fixture result, writer result and
private engine result: RAM exhaustion leaves RUNNING/PENDING10, writer
PENDING10/COMMAND with executor-return fieldFF, and private RAM_STOP7. No
END/FAULT checkpoint is reached in that case.

## Debugger visibility: precise unresolved blockers

The existing transport is unchanged. It permits unbanked CODE reads and
breakpoints only below`8000`; it cannot read/disassemble mapped CODE`8009..8083`
or set a breakpoint at the RAM stop (`8063`). GET_BM reports FMAP, **not XMAP**.
MEMCTR`C7` and FCTL`6270` are outside its safe-SFR/ordinary-XDATA read policies.
There is **no explicit XMAP guard** in its register-preserving access path:
acceptance of an API call is not validation of this flash/RAM execution state.

Ordinary XDATA reads could address the RAM buffer/work bytes, but require
supplied DEBUG_INSTRs and register preservation that have not been validated
while this engine is running or the controller may be busy. Do not bypass
the transport, broaden its whitelist, inject instructions, step/resume RAM,
alter SP/PC, clear XMAP, fake an idle return or attempt memory inspection there.
Status/config and halted GET_PC do not establish FCTL-idle, XMAP, verified
contents or recovery. Any future passive observation itself requires authority,
a deadline and review of its busy-controller/debug safety.

After authority and independent full verification are established, the
intended safe ordinary-memory observation points are boot DISARMED WAIT,
ARMED WAIT, admitted RUNNING/step0 WAIT, each **successfully returned** step
WAIT, and END (mapping restored). Admission TIMEOUT/PACKET faults have made
no flash access. These common-C points are not RAM timing observations.
A SERVICE/HISTORY FAULT may retain XMAP or XBANK after an underlying mapping/
executor error: a common-C PC alone does **not** prove mapping restoration.
Treat unexpected/service faults as blocked inspection states until a separately
reviewed observation procedure proves safety.

For RAM fail-stop, missing expected checkpoint, USB failure or ambiguous
state, stop the experiment and report the uncertainty; no automatic reattach,
resume, retry, reset or kernel-driver detach. A **new explicitly authorized
reset-halt/recovery action** is required. Do not reset a potentially busy flash
operation on the assumption that a timeout made it safe: review supply,
controller interruption and recovery conditions first. Reset loses private
diagnostics/volatile history; a later erased-looking word is still UNKNOWN.
Physical RAM visibility and reset/interrupted-command recovery remain #8 gates.

The services read DMAARM/DMAREQ. SWRU191F Table3-2 prohibits DMA-register
access with DMA_PAUSE set, so a future debug task must independently authorize
and verify the existing own-reset `26 → 22` gate **before execution**. This
does not authorize DMA activity or make mapped-RAM debug access safe.

## Required private preservation, scratch and recovery procedure

This is a gate checklist, **not executable hardware commands**. No backup,
capture, identity or physical device was accessed in this work.

1. Obtain a new task identifying the actual CC2530F256 board/revision, safe
   supply/wiring, intended image/hash, explicit relative page0 **or**1,
   permitted installation/read/debug/RAM-mailbox/reset/destructive operations,
   independent whole-operation deadlines, stop conditions and recovery owner.
   Select exactly physical125 (`3E800..3EFFF`) or126 (`3F000..3F7FF`);
   motherboard straps do not identify a swapped panel. No RF/display action.
2. Before **any** erase/program, preserve the original entire256-KiB main
   flash (`00000..3FFFF`) and separate2-KiB information page. Keep two private
   recovery copies in operator-controlled storage outside Git/build/CI,
   with restrictive directory/file permissions, sizes, hashes, tool identities
   and the board association recorded privately. Do not publish device hashes
   or identities. Preserve page127 including lock/config; do not assume FF.
3. Independently read back and compare **every byte** of both backups to a
   second physical read with a reviewed reader/path, not merely programmer
   exit0, its own verify message, file-to-file hashes, the linked BIN extent
   or sampled locations. Establish readable lock/information regions and a
   workable recovery method before risking them. A locked/unreadable region,
   incomplete reader, ambiguous data or unavailable recovery is a blocker.
   Current public debugger reads only lower32KiB CODE: it cannot do this job.
4. Review external programmer lifecycle and actual erase/program scope:
   even read-only operations may normally reset into execution; verify
   failures may still exit0. The existing Linux no-run guard is not a
   programmer, halted-state proof or a block on RAM helpers/RESUME/STEP.
   Installation may replace CODE but is a **separate authorized destructive
   scope**. A chip mass erase is not scratch-only; do not silently accept it
   or assume subsequent restoration cancels writes/wear to excluded regions.
   If the available workflow cannot preserve the required exclusions, stop.
5. Independently verify the installed fixture and **full excluded baseline**
   before ARM: every byte outside the selected2-KiB page, including main
   CODE/tail, unused banks, the other reserved page and all page127; separately
   compare the whole information page. Installation changes must match the
   separately approved expected image, not be mistaken for preservation of
   pre-install CODE. Keep both original recovery and installed expected
   baselines privately. Confirm halted control; never resume unknown CODE.
6. Only after those gates and debug-visibility blockers are resolved, separately
   authorize the exact one-page sequence. Clear stale breakpoints, verify
   actual CODE before first resume, then prove boot DISARMED and use the
   two-stage handshake at WAIT. Do not externally program/read with a RAM
   helper, reset, attach, use DMA or otherwise disturb the writer's ownership
   epoch between steps. No program retry or automatic next page/run.
7. On successful terminal END, independently verify the entire selected page:
   only the first/last words are`12 34 56 78`, all other bytes FF. Compare
   **all** excluded main-flash bytes and the whole information page with the
   installed baseline again. Native C readback is necessary, not independent
   physical preservation evidence. An external reader may destroy RAM state:
   preserve allowed diagnostics first and never resume that epoch afterwards.
8. For failure/interruption, retain uncertainty and halt where safely possible.
   Recovery is a new approval: reset-halt only under reviewed interruption
   conditions, restore with known private material and a reviewed bounded
   programmer, then independently verify all main/information/excluded bytes,
   lock configuration and halted control. Do not silently rewrite information/
   lock data, weaken protection, infer success from exit status, or resume an
   OEM/RF image without separate authority. Rebuilt fixture execution starts
   DISARMED/UNKNOWN; another erase/run requires a new authorization.

## Evidence separation and acceptance ledger

| #8 item | Current evidence / remaining gate |
| --- | --- |
| Separate non-RF board fixture and explicit scratch/recovery procedure | Implemented offline for both definitions; procedure above, not performed |
| Independently preserved/verified recovery material and full excluded regions before destruction | **Unfulfilled physically**; no private recovery material accessed |
| Safe mapped-RAM completion/error/fail-stop observation and reset recovery | Offline ABI/points/blockers documented; **physical visibility/recovery unfulfilled** |
| Each actually observed board/image and processed physical evidence | **None for this fixture**, both hardware gates open |

Future records must be separate dated, processed summaries: authorized board
revision/image digest, scope, independent preservation checks, exact observed
checkpoint/result/controller evidence, terminal failure handling, separately
authorized recovery and unresolved gates. Never import raw backups/captures,
identities, keys or per-device content hashes into the repository/CI.
Host cases, exact image proofs, generic8051 events and earlier LG experiments
must not be relabelled as physical flash/timing/endurance/power-cut acceptance.
