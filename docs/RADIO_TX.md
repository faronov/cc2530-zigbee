# Bounded init-time TX and CCA

**Offline foundation, not an on-air acceptance record.** `radio_tx` adds polling
CC2530 PHY/controller primitives. It does not add a board `IMAGE`, boot-time RF,
an RF runner, ACK matching, CSMA/backoff/retries, association, network joining or
authentication. `radio_tx_test.ihx` is a **standalone synthetic executable:
never flash it or upload it as a board artifact**.

## API and ownership

The contracts are in [radio_tx.h](../include/radio_tx.h), implemented originally
in [radio_tx.c](../src/radio_tx.c). This is a deliberately separate **init-time
exclusive radio owner**, not a bidirectional MAC:

- Start from a separately established full SoC reset, awake stable undivided
  XOSC32, all three IEN bytes and RF masks zero, no DMA/CSP/scheduled work,
  standard reset modem settings and exclusive clock/Sleep Timer ownership.
  Entry samples cannot prove that history or absence of future DMA work.
- Permitted same-reset history consists of this service's fully completed
  calls, the existing **quiescent** FIFO service and verified clock/timebase
  preparation. There is no reset, IRQ masking or clock-selection operation here.
- **Do not mix with `radio_rx_receive_init` or the queue's RX service in the
  same reset epoch.** Their historical ownership contracts remain unchanged.
  A future unified RX/TX owner is a separate change, not implied by the fact
  that register snapshots might look compatible.
- The queue's `radio_queue_tx_submit` and `radio_queue_tx_read` transfer copied
  **memory ownership only**. A caller can dequeue into its persistent ordinary
  XDATA frame, then use the FIFO and TX primitives below, provided it has not
  used the legacy RX service. Dequeue is never TX success. No pool pointer
  escapes, and this driver does not cancel/requeue/retry on failure.
  The copying queue is not linked into this TX test image; that larger
  composition still needs its own layout/ownership proof.
- Keep board GPIO/startup policy unchanged. No address/source-match RAM or GPIO
  is read or written by this service. Caller-supplied frame bytes contain all
  MAC addresses; the driver neither invents nor validates them.

Explicit foreground sequence, with separately checked results:

1. Establish the above ownership and an empty RX/TX FIFO state.
2. Use real `radio_fifo_preload_init` to load 1..125 FCS-free body bytes. Its
   generic input pointer permits valid CODE or ordinary XDATA objects, and its
   successful result means **FIFO acceptance only**.
3. Call `radio_tx_send_init(mode, channel, RADIO_TX_POWER_05, body_length,
   timeout, limit, diagnostic)`. `RADIO_TX_DIRECT` issues one `ISTXON`;
   `RADIO_TX_IF_CLEAR` calibrates RX, waits for CCA validity, then issues one
   `ISTXONCCA`. There is no retry or fallback to unconditional TX.
4. After a fully completed result only, use a separate
   `radio_fifo_clear_init` if further work is explicitly wanted. TXFIFO
   contents persist after transmission; CCA can accumulate RX bytes. Clear
   verifies any necessary RX flush before the TX flush. Neither FIFO service
   is a radio reset or recovery from a retained failure.

`radio_tx_cca_init(channel, timeout, limit, diagnostic)` instead requires
reset-empty TXFIFO, issues `ISRXON` and `ISSAMPLECCA`, and returns
`CCA_CLEAR`/`CCA_BUSY` only after soft shutdown and verified idle. It consumes
no RX frame, produces no RSSI/correlation measurement and issues no TX command.
The sample describes a past instant; **it does not reserve subsequent airtime**.
`IF_CLEAR` uses the controller's decision at its own TX strobe, not a prior
software CCA result as permission for unconditional transmission.

### Fixed supported profile

| Item | Contract |
| --- | --- |
| Channel | Exactly 11..26; `FREQCTRL=11+5*(channel-11)`, nominal 2405..2480 MHz, applied on subsequent calibration. Not a measured frequency. |
| Power | Caller must explicitly pass **raw `TXPOWER=05`**; all other requested values reject without MMIO. Require `TXCTRL=69`. TI characterizes this combination as typical −22 dBm on its CC2530 EM at 25°C, 3 V, 2440 MHz. No generic/LG measured power, EIRP, calibration or regulatory authorization is claimed. |
| Framing | Require `FRMCTRL0=40`: AUTOCRC on, AUTOACK/test modes off. Hardware supplies preamble/SFD/FCS. No MAC syntax, ACK or peer validation. Initial count/pointers and PHR at **TXFIFO `6080`**, not RXFIFO `6000`, must match the separately preloaded length. |
| Filter/address | Set `FRMFILT0=0C`, `SRCMATCH=00`: filtering/source matching off. PAN/short/extended addresses and source tables at `6100..617F` are untouched; their reset contents are unknown. Addressed RX/automatic ACK modes are unsupported. |
| CCA | `CCACTRL0=F8` (signed raw threshold −8), `CCACTRL1=1A` (mode3, hysteresis2): clear requires low energy **and** not receiving a frame. Hysteresis preserves the distinction between becoming clear and becoming busy. No conversion of this setting into a board-specific dBm threshold. |
| Recommended RF settings | `AGCCTRL1=15`, `TXFILTCFG=09`, `FSCAL1=00`. Compare only FSCAL1's writable low two bits; do not misread its reserved R/W0 upper bits as read-as-zero. Other programmed bytes use full readback. |
| Modem/tuning admission | Require `MDMCTRL0/1=85/14`, `MDMTEST0/1=75/08`, `FREQTUNE=0F`. These registers are not rewritten; custom preamble/modulation/test/sniffer/tuning profiles reject. |
| RX-after-TX | Temporarily set `FRMCTRL1=00`, preserving underflow detection and avoiding the legacy automatic RXENABLE bit6. CCA owns only RXENABLE bit7. Soft-clear bit7 without aborting a current frame, confirm idle, then restore/read back `FRMCTRL1=01` for the existing FIFO API. |

The register's eight-bit `PA_POWER[7:0]` definition and the **CC2530** datasheet
table govern power selection, rather than the manual's adjacent “7-bit” prose.
Similarly, the manual's approximate RSSI-offset prose is not substituted for
the device datasheet or board calibration. See also
[raw metadata limits](RADIO_RX.md#channel-reset-and-metadata-interpretation).

## Completion, deadlines and failures

Fresh TX completion is `RFIRQF1.TXDONE`, **not TXACKDONE, SFD, idle alone,
FIFO consumption, sampled CCA, queue release or an ACK from a peer**. The
service first clears only its owned TXDONE using a direct R/W0 `RFIRQF1=3D`
write (no read/modify/write), then verifies it clear while still idle. It
requires fresh TXDONE and verified final idle before returning `PHY_DONE`.
A lost previously observed TXDONE, stale/unconfirmed flag clear, RFERR,
unexpected modes/counts or configuration changes cannot become success.
No particular final TXFIRST/TXFIFOCNT reset state is assumed.

CCA waits for RXENABLE bit7, PLL lock, completed calibration and RSSI_VALID
(documented as valid after eight symbol periods).
Four genuine NOPs provide **at least** the additional four system clocks
required before using CCA; the surrounding instructions add time. This is
an instruction-level minimum, not a simulated peripheral-timing measurement.
`IF_CLEAR` reports busy from SAMPLED_CCA only after verified soft shutdown;
accepted TX still needs fresh TXDONE. Errors take priority over completion.
No `ISRFOFF`, error acknowledgment, automatic flush or recovery is hidden in
either API.

Each TX/CCA call has one positive 24-bit raw timeout strictly below `800000`
and one positive 16-bit poll cap. The deadline starts after finite entry
preflight, **before any configuration/command write**, and covers configuration,
calibration, sampling/TX, shutdown and restoration. Equality times out. Every
action requires a remaining confirmation poll; a stopped Sleep Timer is still
bounded by the cap. Separate FIFO calls have their own separately checked
deadlines; this is not one convenience call concealing multiple budgets.
Bounds require an executing CPU, exclusive ST0 reads, no missed full wrap,
and the existing [timebase](../include/timebase.h) half-range assumptions.

- Invalid argument/range/diagnostic ownership: no MMIO or diagnostic mutation.
- `PHY_DONE`, `CCA_CLEAR`, `CCA_BUSY`: diagnostic `phase=7`, verified idle and
  restored FIFO framing; an explicit next operation is allowed under the same
  owner. RX backlog still requires explicit FIFO clearing.
- Any other result: retain the **first** fault. Later TX **and** CCA calls
  return it without MMIO or diagnostic writes, even with invalid arguments.
  There is no history-clear API. Stop the caller; a separately established
  full reset is the only recovery.

A timeout may occur **after a frame transmitted**, or with RX/TX still active.
It is never permission to retry, flush, resume or reset automatically.
Diagnostics are partial observations, not an atomic radio snapshot: `actions`
marks issued strobes/writes, `sample_valid` marks a complete observation,
`txdone` records fresh observed completion, and `radio_idle` is published only
after successful shutdown/restoration. Raw elapsed ticks on a fault are not
calibrated time or continuity evidence. Strobe/status behavior relies on the
documented controller semantics; synthetic replay cannot qualify physical CSP
execution or detect every electrically silent hardware failure.

## Memory, ABI and offline evidence

Link **timebase, radio_fifo, radio_tx, then callers**. The entire combined
private/compiler XDATA prefix is fenced, including parameters and wait state.
The persistent, disjoint 29-byte diagnostic must follow that prefix, remain
below `1E00`, and exclude `__gptrput_PARM_2`. Public diagnostic pointers are
two-byte XDATA-qualified pointers; the reused FIFO payload remains a three-byte
generic pointer. Functions require the normal SDCC register-bank0/DPS0 ABI and
are foreground/non-reentrant, not ISR APIs.
Tables remain in CODE. No heap, interrupt owner, additional register bank, GPIO or RF DMA
allocation is introduced. The `1F00..1FFF` IRAM alias is never an extra pool.

The standalone composition uses its own **512-byte total XDATA reservation
budget**, not a relaxation of any component budget. With SDCC4.2.0/model-large:

- **8,526 CODE bytes**, unbanked `0000..214D`.
- **374 ordinary XDATA +64 status reservation =438/512 bytes**.
  Private prefix `0000..00AC`; guard/diagnostic starts `00AD`/`00B1`;
  FIFO diagnostic `00D2`, caller payload `00E7..0163`;
  generic-store scratch `0175`.
- Stack reservation `59..FF` (167 bytes), initial SP58. Linked MMIO sampling
  observes peak SP6C; upper IRAM `80..FF` remains guarded and returns unwind.
  This is not an interrupt-nesting or universal caller-stack bound.
  The linked IRAM allocation is 57 DATA bytes, 21 overlay bytes, one bit-storage
  byte (six bits) and eight bank0 bytes; the two-byte `1E..1F` gap is unused.
- Both board definitions must produce identical CODE:
  `426080e0151dffd7f5b9ee4cf58a4d3962e7d0a4d7561359a8cda9970683a154`.

[Native tests](../tests/test_radio_tx.c) execute the real FIFO/timebase/TX
composition with an original synthetic controller. They cover every supported
channel/body length, unsupported modes/powers, stale completion, busy-at-strobe,
calibration/command/completion/shutdown stalls, all seven defined RFERR bits,
overflow, header/count mismatch, wrap/backward/ambiguous ticks, maximum poll
cap, buffer boundaries and retained faults. No native controller model enters
SDCC firmware.

The [linked proof](../tests/boot_radio_tx.py) pins the whole image, ABI and
private declarations, decodes actual instructions/MMIO, normalizes the real
FIFO module back to its published standalone proof, and compares **per-image
snapshots** of all four relocated listings. It replays genuine C52 instructions,
not patched returns. It checks actual DPTR/operands, exact RFD bytes, unchanged
caller data, unused/status XDATA, alias, upper IRAM, stack and radio-register
guards. Markers are indexed once; every simulator subprocess retains the
15-second bound. A missing alias and changed CODE/ABI/layout/listings reject.

Focused checks passed for **each** of `generic` and `lg_esl29_rev03`:
7,078 native calls; 51 linked sequences comprising 173 calls and 45,538 actual
MMIO events. The directly coupled unchanged timebase tests and FIFO corpus also
passed on both definitions (69,895 native FIFO cases /99 linked scenarios).
The TX build/proof outputs are isolated under `build/radio-tx-dev/<board>`;
coupled checks use `build/radio-tx-dev/coupled-generic` and `coupled-lg`.

Host and simulator use the **same synthetic controller expectations**, not
two independent peripheral measurements. Primary memory-map review caught and
corrected an early shared-model RXFIFO/TXFIFO-address error before completion.
These tests establish neither on-air bits/FCS/timing, actual CCA behavior/power,
physical compatibility, nor a generic/LG hardware observation.

### Integration boundary

`make BUILD=build/radio-tx-check test-radio-tx` runs the native and linked
corpora above. The target is included in the existing per-board common checks,
with normal strict native/SDCC flags and the stated link order. Native inputs
are `tests/host_mmio.c`, the three real services and `tests/test_radio_tx.c`.
Use separate build directories for the two board definitions.

Immediately after linking, Make snapshots the timebase, FIFO, TX and caller
relocated listings as `radio_tx_test.<module>.rst`. A later component link
cannot silently substitute different relocations. The standalone inventory
and all-board exclusion regressions cover this target and its source/symbols.
The standalone target does not itself add an image or hardware operation.
The separate [boot-disarmed board fixture](RADIO_TX_FIXTURE.md) now composes
these services with the actual clock and guarded caller; only that selected
board image is an eventual programming input. Existing component budgets and
the seven-path board-artifact whitelist remain unchanged.

## Separately authorized future RF procedure

This is preparation **only**, not authorization. The
[separate board fixture/manual runner](RADIO_TX_FIXTURE.md) implements the
bounded one-attempt path. The
[separate LG record](DEBUGGING.md#2026-09-19-lg-single-attempt-tx-acceptance)
now supplies one PHY_DONE/independent-body observation, not blanket acceptance
or authorization for another run. Before physical execution:

1. Identify the exact board/revision/CC2530F256, recovery material, separately
   selected **boot-disarmed board fixture**, linked image hash/toolchain and
   return-to-known-state procedure. Complete the still-open physical/scope
   dependencies, including #8/#4 where applicable. Do not use a component
   executable or infer permission from past RX work.
2. Obtain explicit scope for channel, raw power profile, antenna/test
   environment, independent receiver/sniffer, public synthetic frame bytes,
   maximum one attempt per command, total attempts, permitted debugger/reset
   operations and recovery conditions. The low reference-design power setting
   is not a safe universal board power or regulatory clearance.
3. Review the selected finite fixture: default/reset/resume disarmed, deliberate
   bounded admission, unchanged board startup/GPIO, no AUTOACK, automatic retry
   or automatic fault reset, explicit END/FAULT. Do not enable legacy RX and
   this init-time TX owner together. The linked board fixture is separate;
   neither document establishes a shared MAC ownership transfer.
4. Under that separate authorization, first observe default no-command
   behavior. Capture separately admitted direct TX, conditional-clear TX and
   busy CCA. Any controlled busy-channel source needs its own RF permission.
   Require independent captured bytes/channel/frame validity and, if claiming
   FCS correctness, a capture path that actually retains/verifies FCS.
   Queue release, local TXDONE and a receiver that strips FCS are insufficient
   for those independent claims.
5. Observe a completed idle END checkpoint using only separately allowed
   debugger access. **FAULT is not an idle observation point.** Halting or
   stepping during RF changes timing;
   generic C52 or debug-paused execution cannot establish turnaround/CCA timing.
   On fault do not assume RF is off, inspect unsafe/live MMIO, retry or resume.
   Follow only the preauthorized terminal containment/reset/recovery procedure;
   existing debugger protections are not weakened.
6. Keep raw captures, board identities and private recovery backups outside git
   and CI. Publish only processed non-sensitive observations, exact board/image/
   tool versions, actual channel/raw settings, measured power/timing **only if
   measured**, finite attempt counts, completion/busy/fault outcomes and limits.

**Open gates:** the one LG conditional-clear/body observation does not cover
physical busy-channel behavior, independent FCS, fault containment/recovery,
generic hardware or calibration. #12's bounded implementation is complete;
its remaining physical observations are now tracked by #15, with calibration
under #4. #15 also requires its
separate lab fixture, independent captures and unimplemented ACK/retry/
association behavior; this slice closes none of those or M3 as a whole.

## Primary sources and provenance

Original BSD-3-Clause implementation/model/proof; no SDK, IAR sample, programmer
code or binary was imported. Sources were consulted as facts:

- [TI SWRU191F](https://www.ti.com/lit/ug/swru191f/swru191f.pdf), April2009,
  revised April2014: Table2-3 p39 (NOP clocks); §23.1.2 pp210–213
  (R/W0 flags, TXDONE bit1, RFERR and masks); §23.2 and §23.4 pp213–214
  (RFD, **RXFIFO6000/TXFIFO6080**, unknown-after-reset address RAM);
  §23.5/FREQCTRL p261 (channel); §23.8.1–5 pp218–219
  (TX/CCA, 192µs documented turnaround, 2µs down-ramp, persistent TXFIFO,
  underflow/overflow); §23.8.7–13 pp221–222 (AUTOCRC, CCA validity/four-clock
  lag and power); §23.9.1 pp222–223 and RXMASKCLR/FRMCTRL1 p260
  (soft non-aborting shutdown and RX ownership); §23.14 pp238–241 and
  §23.14.9.34/.37–39 pp252–253 (immediate E3/E9/EA/EB strobes);
  Table23-6 p256 (recommended settings), FREQTUNE/TXPOWER/TXCTRL p262,
  FSMSTAT1 p263, CCACTRL0/1 and RSSISTAT p264, FIFO pointers p265,
  modem/FSCAL1 pp266–267 and MDMTEST0/1 p270.
- [TI CC2530 SWRS081B](https://www.ti.com/lit/ds/swrs081b/swrs081b.pdf),
  April2009, revised February2011: Table2 p24 and its measurement conditions
  (the selected `05` setting without the separate `TXCTRL=09` variant);
  p9 RSSI/CCA characteristics. Neither document calibrates these boards.
- [TI SWRZ031](https://www.ti.com/lit/er/swrz031/swrz031.pdf), April2009,
  history2009-04-29: §1.1 DMA variable-length zero/one issue and §1.2 Timer2
  latch issue. This slice uses neither DMA nor Timer2. This is an applicability
  check, not a claim that synthetic testing establishes all silicon behavior.
