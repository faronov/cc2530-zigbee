# Filtered receiver/AUTOACK owner

**Original offline hardware-service foundation for #48, not a MAC adapter or
hardware acceptance record.** [radio_autoack.h](../include/radio_autoack.h) and
[radio_autoack.c](../src/radio_autoack.c) establish one reset-exclusive filtered
receiver with hardware-generated acknowledgments, bounded foreground FIFO
servicing, explicit non-aborting stop/drain and same-owner rearm (#72).
The #73 extension adds one ordinary hardware-gated CCA/TX attempt from
stopped/drained idle, followed by reception under the same owner.

AUTOACK **transmits RF without CPU intervention**. Actual use requires separately
authorized RF-transmitting ownership and CPU progress. The service foundation adds no
CSP program, DMA/ISR, GPIO policy, MAC Timer composition, MAC/security acceptance
or membership. `radio_autoack_test.ihx` is a standalone synthetic executable:
**never flash it or distribute it as a board firmware artifact**.
The later [#77 board fixture](RADIO_LINK_FIXTURE.md) separately links the real
owner and clock/startup services for boot-disarmed, manually authorized TX/RX.
Its offline proof/operator preparation is not silicon acceptance; no other
board image may link this owner or its synthetic test/model.
The later [#78 LG physical sequence](DEBUGGING.md#2026-09-22-lg-same-owner-txrx-sequence)
adds one ordinary TX with independent body equality and an empty RX interval
followed by stop. It supplies no positive receive, automatic-ACK or timing
acceptance for this service.

The [#80 co-owned clock/radio composition](MAC_RADIO.md) separately selects
`CC2530_MAC_RADIO`: verified reset-state Timer2 initialization may precede
acquisition under that same foreground owner. Its new live reader tolerates
owned RX/AUTOACK activity without fabricating physical event timestamps.
Complete shared-prefix and libc guards replace the isolated prefix in this
profile only. The existing standalone/board CODE and radio phases are unchanged.

## Primary basis and limits

The following are functional facts from TI **SWRU191F, April 2009, revised
April 2014**, using printed pages:
[CC253x/4x User's Guide](https://www.ti.com/lit/pdf/swru191).
No manual illustration, sample implementation, SDK or programmer implementation
is imported.

| Primary location | Implemented decision or unresolved boundary |
| --- | --- |
| Section 23.4.3, Table 23-1, p.214 | Local-address RAM is unknown after reset. Write and verify all eight IEEE bytes, two PAN bytes and two short-address bytes before enabling RX. Multibyte addresses are little-endian. |
| Section 23.9.5, pp.224-226; FRMFILT0/1, p.257 | Enable the documented hardware filter for version 0 DATA, ACK and command frames, non-coordinator mode and unchanged frame-type interpretation. This is filtering of opaque bodies, not MAC/security validation. |
| Destination filtering, p.225; AUTOACK predicate, section 23.9.8, p.231 | Filtering accepts broadcast PAN/short destinations. The listed AUTOACK predicate has no explicit broadcast exclusion. **No arbitrary-input unicast-only ACK guarantee is supportable.** |
| Sections 23.9.7-23.9.8, pp.230-232; FRMCTRL0, p.259 | Automatic ACK requires filter acceptance, Ack Request, correct FCS and a non-Beacon/non-ACK type. ACK DSN comes from the received frame. AUTOCRC selects FIFO metadata; it does not enable/disable ACK generation. |
| Section 23.9.8, Figures 23-17/23-18, p.231; FSMCTRL, p.263 | Select unslotted ACK: TI specifies its preamble 12 symbols after the received frame ends. Manual ACK overrides are reception-only, last-strobe-wins; late strobes cause an error. This owner issues none. These are silicon specifications, not timing measured by these tests. |
| Section 23.9.8, pp.231-232; SRCMATCH, p.258; FRMCTRL1, p.260 | Pending is the OR of its documented sources, not an echo of incoming Pending. Disable source matching, AUTOPEND and PENDING_OR; no SACKPEND strobe is permitted. |
| Sections 23.9.1-23.9.2, pp.222-223; RXENABLE/RXMASKSET/RXMASKCLR, p.260 | Persistent RXENABLE requests return to RX after reception/transmission/ACK activity. Use soft bit set/clear, not aborting SRXON/SRFOFF. The prose's older RXENMASKOR/AND names are resolved by the actual register definitions: writing `01` to RXMASKCLR clears bit 0. |
| Section 23.9.2, p.223; FSMCTRL, p.263 | `RX2RX_TIME_OFF=0` disables the default 12-symbol post-reception SFD-detection timeout. This is not MAC SIFS/LIFS completion or a loss-free receive guarantee. |
| Figure 23-20, p.235; Table 23-3, p.236; FSMSTAT0/1, pp.262-263 | A soft stop allows an in-flight reception and its eligible ACK to finish. TX_ACTIVE covers ACK calibration/delay/TX/shutdown; RX_ACTIVE includes RX calibration. Do not drive control flow from fast numeric FSM state values. |
| RFIRQF0/1 and RFERRF, pp.210-211 | Flags update even while IRQs are masked. RXMASKZERO is not idle. TXACKDONE is sticky, not a per-frame count, correlation token or timestamp. A separately cleared/verified RFIDLE plus physical idle is the stop completion gate. |
| Section 23.10, pp.232-233; FSMSTAT1/FIFOPCTRL, p.263; FIFO registers, pp.264-265 | The 128-byte FIFO can contain multiple frames. RFD advances its read pointer; direct RAM access does not. Use the complete-frame indication with threshold 127, bounded count and head progression. RFIRQF0.FIFOP also documents notification when reading a complete packet leaves another complete packet. No one-frame/one-edge assumption is used. |
| Section 23.10.2, p.233 | Overflow, underflow and abort are errors, not permission to flush and claim drainage. Filtering can overflow before rejecting a frame. |
| Table 23-6, p.256; FSCAL1, p.267; TXPOWER/TXCTRL, p.262 | Apply the recommended AGC/TX filter/VCO settings. Read back only FSCAL1's known VCO_CURR bits; its upper bits are R/W0, not read-as-zero. All other configured bytes require full readback. |
| Section 23.8, pp.218-222; immediate instructions, pp.253-254 | With AUTOACK disabled at idle, replace TXFIFO using only ISFLUSHTX=EE and RFD writes. AUTOCRC PHR includes two FCS bytes; software supplies only the body. Use ISTXONCCA=EA, never unconditional TX. Successful TX retains FIFO contents. |
| CCA, p.222; CCACTRL0/1, p.264 | Require actual RX/RSSI readiness and four additional system clocks before hardware-gated TX; use recommended threshold F8, mode3 and hysteresis2 (1A). |
| TX count/pointers, pp.264-265 | Verify the prepared buffer before admission; TXFIRST is the next byte to transmit, not a constant zero after TX. Post-TX pointer/count need not equal the preloaded tuple. |

TI **SWRS081B, April 2009, revised February 2011**, Table 2 p.24
([CC2530 datasheet](https://www.ti.com/lit/pdf/swrs081)), characterizes raw
TXPOWER `05` with the normal TXCTRL profile as typical -22 dBm on its CC2530 EM
at 25 C, 3 V and 2440 MHz. This is not measured generic/LG output power, EIRP,
calibration or regulatory permission.

TI **SWRZ031, April 2009**, sections 1.1-1.2 pp.2-3
([CC2530 errata](https://www.ti.com/lit/pdf/swrz031)), covers variable-length DMA
and Timer 2 read latching. This owner uses neither DMA nor Timer 2. It does not
claim to resolve those issues for other services.

The AUTOACK predicate supplies no software duplicate/window decision.
Otherwise eligible late or repeated frames may each elicit an ACK, even when
software later rejects their body. The filter does not exclude the Security
Enabled bit; this is **not an unsecured-only receiver**. There is also no
reviewed global-filter ACK exception sufficient to preserve every ignored-FCF
case of the existing `mac_tx` ACK contract. That remains a full-adapter gate;
the old ACK corpus is unchanged.

## API, profile and ownership

Acquisition requires a genuine full SoC reset, foreground nonreentrant execution
in register bank 0/DPS 0, awake stable undivided XOSC32, and exclusive
radio/CSP/DMA/clock/Sleep Timer ownership. Only verified clock/timebase preparation
may precede it. All three IEN bytes, RF masks, DMAARM/DMAREQ and CSP execution
must be inactive, with no scheduled work or competing debugger/peripheral
access. Initial radio/FIFOs must be reset-empty and idle. Entry samples cannot
prove that history or absence of future activity.

Do not compose this lease with the old reset-exclusive `radio_rx`, `radio_tx`,
`radio_fifo`, queue RX service or `mac_time` calls. Their APIs, contracts and
budgets are unchanged. A later register resemblance does not authorize a
handoff to one of those owners.

`radio_autoack_acquire(configuration, timeout, limit)` copies a 14-byte
configuration; it retains no caller pointer. IEEE bytes are supplied least
significant first; PAN/short values are encoded little-endian. All address bit
patterns are raw caller filter configuration, not validated identities.
Channels 11..26 and explicit raw power `RADIO_AUTOACK_POWER_05` are supported.

| Setting | Written value |
| --- | --- |
| EXT_ADDR `616A..6171`, PAN_ID `6172..6173`, SHORT_ADDR `6174..6175` | All 12 caller bytes, individually verified |
| FRMFILT0 / FRMFILT1 | `01 / 70`: filter on, maximum version 0, reserved-FCF mask 0, non-coordinator, DATA/ACK/command |
| SRCMATCH | `00`: source matching and AUTOPEND off |
| FRMCTRL0 / FRMCTRL1 | `60 / 00`: AUTOCRC/AUTOACK, RSSI plus CRC/correlation, normal RX/TX, Pending0, underflow detection, no automatic TX mask bit |
| FIFOPCTRL / FSMCTRL | `7F / 00`: threshold 127, unslotted ACK, RX-to-RX timeout disabled |
| AGCCTRL1 / TXFILTCFG / FSCAL1 | `15 / 09 / 00`; only FSCAL1 readback is masked to bits 1:0 |
| FREQCTRL / TXPOWER / TXCTRL | `11+5*(channel-11) / 05 / 69` |

Require unchanged MDMCTRL0/1 `85/14`, MDMTEST0/1 `75/08`, FREQTUNE `0F`,
standard modem control and an idle CSP. Acquisition changes no CCA setting
and writes no source-match table, TXFIFO, RFST or GPIO.
After configuration/readback it writes `RXMASKSET=01`. `READY` additionally
requires the owned mask, calibration complete, RX_ACTIVE, PLL lock, no
TX_ACTIVE, and RSSI_VALID. A request echo or RX_ACTIVE alone is insufficient.

Every configuration/output object must be caller-owned persistent XDATA below
`1E00`, disjoint from the complete linked private prefix and libc scratch.
There is one private 128-byte staged frame so an operational error cannot
publish a partial body. No heap or software packet queue is introduced.

### Results and state transitions

| Call/result | Meaning |
| --- | --- |
| Acquire -> `READY` | Enter RX ownership; hardware may autonomously transmit eligible ACKs. |
| Receive -> `FRAME` | Publish one complete FCS-free body with CRC_OK set. No MAC/security or per-frame ACK attestation. |
| Receive -> `BAD_CRC` | Also publish that body's raw bytes/metadata, explicitly marked invalid CRC. It is not MAC input acceptance. |
| Receive -> `EMPTY` | No complete head frame observed at the initial check; partial reception may still be active. Not silence, drainage or window closure. |
| Stop -> `DRAIN` | Physical stop is established, but queued bytes remain. Explicitly consume complete frames using receive, then call stop again. |
| Stop -> `STOPPED` | Physical idle and empty hardware FIFO after explicit servicing, with consistent stopped ring pointers. Enter OFF or OFF_NOACK; acquire/receive/stop still reject. |
| Resume -> `READY` | Only from this owner's OFF/OFF_NOACK after STOPPED: recheck the retained profile/idle/empty FIFO, restore the normal profile if needed, then enable and confirm RX. Not continuous reception or ownership transfer. |
| Send -> `TX_DONE` | From OFF/OFF_NOACK only; fresh ordinary PHY completion with TX inactive. Keep the RX request, enter RX_NOACK. Not ACK/delivery or captured timing. |
| Send -> `CCA_BUSY` | One hardware-gated attempt did not transmit. Keep RX enabled in RX_NOACK; no automatic retry. |
| Invalid argument/range/storage/state | No MMIO, output or diagnostic mutation. |
| Operational error | Enter terminal FAULT and retain the first error and ownership. All later operations return it with no MMIO, output or diagnostic mutation, even with invalid arguments. |

Receive masks only the PHR's documented high bit, bounds length 5..127, and
returns `length=PHR-2` with 3..125 body bytes. `rssi_raw` is the uninterpreted
RSSI byte; `crc_correlation` has CRC_OK in bit 7 and raw correlation in bits
6:0, not calibrated LQI. Successful short frames preserve the inactive output
tail. No software header/security/address/DSN/duplicate parser alters the body.

`BAD_CRC` is a documentation-supported physical path even with `FRMFILT0=01`,
not merely conservative synthetic input. SWRU191F section 23.9.5 pp.224-225
filters header/address/type/length conditions, not final FCS correctness.
Section 23.9.7 p.230 defines CRC_OK=0 and makes software responsible for
discarding the frame. An otherwise accepted frame with a payload/FCS error
can therefore remain available with bad-CRC metadata. Section 23.9.8 p.231
separately requires correct FCS for AUTOACK: that frame must not be automatically
acknowledged. This path is documented, not hardware-observed here.

Each RFD access is a single emitted destructive read. Before reading, require
remaining work for confirmation, a valid deadline, no RFERR, sufficient count,
and the expected head pointer. Confirm the actual PHR and final head. Do not
require a live FIFO's total count to decrease by exactly one: another frame
may arrive or be rejected at its tail. Multiple queued frames, full capacity
and circular wrap are supported one explicit receive call at a time.

Stop clears/verifies only old RFIDLE (`RFIRQF1=3B`), then clears the owned mask
bit with `RXMASKCLR=01`. The current profile stays unchanged throughout
reception/ACK completion, including AUTOACK when enabled. Require fresh RFIDLE, RXENABLE zero, calibration inactive, PLL
unlocked, SFD low and RX_ACTIVE/TX_ACTIVE both low. Once stopped, also require
`(RXFIRST_PTR+RXFIFOCNT) mod 128 == RXLAST_PTR`; a full 128-byte FIFO is distinct
from empty despite identical pointers. Complete frames remain available.
An unexplained partial FIFO, pointer/count contradiction or RF error is a
fault, never drained success. There is no abort, SRXON, RX flush, hidden discard,
automatic retry or recovery operation.

The ring identity follows the exact SWRU191F p.265 register definitions:
`RXFIRST_PTR[6:0]` (`619D`) is the RAM offset of the first FIFO byte;
`RXLAST_PTR[6:0]` (`619E`) is the offset of the **last byte +1 byte**.
The latter is a one-past-last producer boundary, not the last occupied byte
or permission to write when full. Page 264 defines RXFIFOCNT as the number of
bytes in the FIFO. Section 23.9.7, Figure 23-15 p.230 shows the leading length
byte and replacement of the two FCS bytes by RSSI/CRC-correlation metadata,
not two additional bytes: a complete length-L frame occupies
`1 + (L-2) + 2 = L+1` FIFO bytes, including PHR and metadata.

Pointer stability is derived only after verified physical idle under exclusive
ownership: section 23.9.1 p.223, Figure 23-20 p.235 and Table 23-3 p.236 allow
reception/eligible ACK completion before idle after soft mask clear. With
RXENABLE zero and no competing RFD access, flush/reset, DMA or CSP activity,
no FIFO producer/consumer remains active during the check. TI does not print
the modulo equation or promise an atomic snapshot/pointer freeze merely upon
the mask write. The implementation waits for completion first.

`STOPPED` is **not** a loss-free/continuous RX lease, MAC/POLL `CLOSED`, IFS
completion, ordinary-TX handoff, captured time or release to another
init-time API. Filter-rejected traffic is not delivered. Any later software
queue loss also prevents a future adapter from claiming loss-free closure.

### Explicit same-owner rearm

`radio_autoack_resume(timeout, limit)` starts another receive episode only
from this owner's OFF or OFF_NOACK state. It does not acquire another owner's stopped
radio, consume pending DRAIN frames or recover a fault. No caller may use
another RF/FIFO API, modify configuration or reset hardware between calls.
The ordinary acquire/receive/stop state errors remain unchanged.

The same complete observation revalidates clock/profile, masks, errors,
physical idle and stopped ring consistency. The previous stop's RFIDLE bit
must still be present; it is **not a new capture or fresh event identity**.
Count and FIFO/FIFOP indications must be empty before enabling. Equal,
nonzero ring cursors are valid; the original configuration and FIFO history
are retained, not reset to resemble cold acquisition.

From ordinary OFF, with remaining confirmation work, resume writes only `RXMASKSET=01`, then
uses the original calibration/RX_ACTIVE/PLL/no-TX_ACTIVE/RSSI_VALID readiness
checks. No configuration, flag acknowledgment, RFST, FIFO or GPIO write is
added. New frames may arrive and eligible AUTOACKs may transmit after the
enable, including before READY returns. Timeout/fault may leave RX active;
there is no rollback or success-shaped cleanup.

This follows SWRU191F23.9.1-2 pp222-223 and RXENABLE/RXMASKSET p260 directly.
It deliberately creates an **RX gap** between episodes, including turnaround.
It establishes neither the safety of ordinary TX during live AUTOACK nor
permission to bridge an active POLL window with stop/resume. Captured timing,
IFS and window continuity remain separate.

### Explicit ordinary TX and response reception

`radio_autoack_send(body, length, timeout, limit)` accepts immutable caller
XDATA containing1..125 FCS-free bytes, without MAC parsing. It requires this
owner's OFF or OFF_NOACK after actual stop/drain; live RX or pending drainage
is rejected without MMIO. This is not a call into the old reset-exclusive TX
driver, a software clear-channel check followed by unconditional TX, or a MAC
backoff/retry scheduler.

Recheck physical idle, empty consistent RXFIFO, the retained RFIDLE indication,
clock and configuration. While still idle, disable AUTOACK (`FRMCTRL0=40`),
then filtering (`FRMFILT0=0C`), verifying each transition. Establish and verify
CCA threshold/mode (`F8/1A`) once; all later observations retain those checks.
No filter change occurs during live reception.

Issue only the TX flush `EE`, verify empty TXFIFO, and load PHR=`length+2`
plus the body through RFD. Each write has bounded confirmation of TX count and
both pointers while idle with empty RXFIFO. Clear and verify old TXDONE
(`RFIRQF1=3D`), preserving other flags. Enable the owned RX mask and wait for
calibration completion, RX_ACTIVE, PLL lock, inactive TX and RSSI_VALID.
Execute four actual NOPs and recheck readiness before issuing `EA`.

SAMPLED_CCA=0 requires no TX activity/TXDONE and an unchanged prepared buffer;
return CCA_BUSY. Otherwise wait within the same deadline/work allowance for
fresh TXDONE and inactive TX, retaining the sampled admission result.
The ordinary TX cannot be confused with a new autonomous ACK in this phase:
AUTOACK was disabled before enabling RX. Nothing is inferred about #50's
live-AUTOACK arbitration.

Both results enter RX_NOACK with RXMASK bit0 still set. Hardware can return
to RX after TX without a software re-enable; TX_DONE does not promise that
post-TX calibration or RSSI readiness has already finished. Receive exposes
complete CRC-good/bad bodies unchanged, including unrelated frames, without
discarding a frame that arrived during CCA preparation. No DSN matching,
ACK receipt, captured TX/ACK-end time or timed response window is supplied.
The existing receive body lower bound remains3 bytes.

Stop/drain enters DRAIN_NOACK or OFF_NOACK without changing this profile.
Another explicit send may replace the retained TXFIFO only at idle.
Resume from OFF_NOACK first restores filtering (`01`), then AUTOACK (`60`),
verifying each at idle before the normal RX enable. Ordinary OFF rearm keeps
its previous one-mask-write behavior. An operational fault never flushes,
aborts, retries or restores the profile; TX or RX may already have occurred.

The RX gap and explicit no-AUTOACK interval are **not continuous MAC/POLL
service**. #40 captured timing, #45 Association deadline, IFS, full #50
arbitration and the MAC scheduler adapter remain open. This step neither
implements join nor grants hardware/RF permission.

### Bounds and diagnostics

Each call uses the real unchanged timebase reader/deadline/expiry functions:
positive raw Sleep Timer timeout below `800000`, positive 16-bit work limit,
wrap-safe half-range comparisons, and expiry at equality. These are raw ticks,
not symbols. Require real clock continuity, no missed wraps and CPU progress.
Every destructive read/configuration/mask action reserves a confirmation
sample. Even a stopped timebase cannot defeat the work cap.

The deadline covers the last checked decision, followed by bounded CPU-only
result/body publication; it is not an exact RF event or return-instant
timestamp. Live RX may continue during publication. A `FRAME` result describes
the consumed frame, not a guarantee against subsequent FIFO overflow.
On timeout/work exhaustion the receiver or ACK transmitter may still be
active. **No RF-off-on-fault or finite over-air ACK-count promise exists.**
Only a separately authorized genuine full reset can recover.

`radio_autoack_diagnostic()` returns read-only private storage. Diagnostics are
partial observations, not an atomic peripheral snapshot. Phase 0 means no
operation yet; phases 1..7 mean configuration, enable, ready, frame service,
soft stop, drain and off; phases8..10 mean rearm preflight, enable and ready.
Phases11..16 mean TX preflight/profile, preload, TXDONE-clear/RX readiness,
CCA attempt, completion wait and response-RX ownership.
`sample_valid` marks a complete observation;
`writes` counts issued configuration/control writes and `verified` counts
confirmed configuration bytes. Polls, consumed bytes and raw elapsed ticks
are per-call. Invalid calls preserve the preceding diagnostic; retained fault
calls preserve the original one. Sticky TXACKDONE/TXDONE observations are never
converted into ACK counts, per-frame receipts or captured timestamps.

## Memory, ABI and offline evidence

Link **timebase -> radio_autoack -> radio_autoack_test**. Immediately snapshot
each relocated listing as `radio_autoack_test.timebase.rst`,
`radio_autoack_test.radio_autoack.rst` and
`radio_autoack_test.radio_autoack_test.rst`; later links must not overwrite
this image's proof inputs.

SDCC 4.2.0, unchanged `--model-large --std-c99 --debug --opt-code-size --Werror`
and unbanked linker flags produce byte-identical generic/LG images:

| Object | CODE, including constants/startup | Ordinary XDATA | Permanent DATA | OSEG |
| --- | ---: | ---: | ---: | ---: |
| timebase | 404 | 25 | 0 | 3 |
| radio_autoack | 5888 | 256 | 4 | 2 |
| test caller | 411 | 157 | 2 | 0 |

Whole image: **7007/24576 CODE**, **450 ordinary XDATA + the entire 64-byte
status reservation = 514/1536 bytes**. Runtime/startup adds 304 CODE and 12
XDATA beyond those object totals. The separate 24 KiB/1536-byte budget covers
the real timebase, one staged frame, copied configuration, diagnostics,
compiler parameters, caller buffers and runtime; it changes no old service
budget and establishes no full-stack/POLL fit.

The entire private prefix is `0000..0118`, caller allocation `0119..01B5`,
and libc scratch `01B6..01C1`. The latter includes memcpy parameters **and
its private temporary**, memset parameters and generic-store scratch. Both
input and output ranges exclude that whole suffix; only excluding
`__gptrput_PARM_2` would be insufficient. The proof binds its actual map,
allocation and emitted runtime layout. A different composition needs its own
complete ownership/layout proof, not an assumed library ordering.

Stack starts at `21`, initial SP `20`; caller checkpoints unwind to SP `22`.
The CI-pinned whole-run maximum is **SP `34`** (20 bytes above initial SP),
separate from MMIO-sampled maximum **`2E`**. The selected bound remains `7C`;
upper IRAM `80..FF` stays intact. No separate RAM is allocated at the
`1F00..1FFF` IRAM alias. All 56 unused status-tail bytes and other unallocated
RAM are guarded.

Whole emitted CODE SHA-256:

```text
a06a14624e638adb49b87a23d7e2fe35711a32a23c53eaf53f74c6569a1bcde4
```

**Host-test corpus:** 158362 API calls per board, both strict native and ASan/UBSan.
Coverage includes all bounded lengths/CRC bytes, address/profile bits,
every value of each caller address byte, copied-configuration independence,
configuration/output ownership including every libc-scratch overlap, native
object preservation, all FSCAL1 upper-bit patterns, and faults after each
possible destructive read. Synthetic bodies include Security Enabled,
Ack Request, incoming Pending and broadcast destinations; the model does not
implement over-air filtering, FCS calculation or ACK generation. Lengths
6..8 deliberately overapproximate the configured filter's possible frames
to exercise byte bounds, not claim those are eligible over-air packets.

**Image/simulator corpus:** 194 persistent sequences, 1732 genuine API calls,
1157 exactly-once RFD reads, and 9017 artifact negatives plus one genuine
missing-alias negative per board. Coverage includes delayed readiness/
calibration, active receive/ACK soft stop, 21 queued frames, max-length circular
FIFO, concurrent arrival, CRC classification, stale flags, partial/count/
head/tail/overflow/underflow faults, equality/work boundaries and terminal
no-MMIO/error-output preservation. Rearm additionally covers repeated episodes,
nonzero/wrapped empty cursors, pending-drain rejection, arrivals during enable,
last-allowed timely confirmation, timeout equality, ignored mask writes,
profile/clock/idle/FIFO/flag faults and retained re-entry. Native checks add
every configured bit during OFF and the full65535 work cap with stalled time.
The37 new sequences include419 TXFIFO writes,22 CCA attempts,29 TX-only
flushes and22 genuine four-NOP settling calls. They cover clear/busy outcomes,
post-TX calibration/ACK-shaped reception, bad CRC, unrelated queued traffic,
explicit profile restoration and repeated TX without reset. Ignored
flush/flag/mask writes, preload/profile/CCA faults, active/stuck/underflow TX,
stale completion, deadline equality, work exhaustion and retained send
re-entry cannot produce successful completion. Native tests additionally
cover every length byte, each preload-byte failure and every call-budget
boundary for the reference11-byte attempt. Full acceptance runs in
GitHub Actions, not a duplicate local matrix; no hardware evidence is implied.

The proof binds all CODE/constants/runtime bytes, complete public/private/
helper/caller/field F/S/L/T record multisets with original duplicate
multiplicity, source/storage associations, every map symbol, all-area
allocations, entry/end/storage labels, and all three ordered instruction
inventories. Missing, conflicting, malformed and duplicate records reject.
CDB is hashed as complete raw bytes **before decoding**, retaining source-line
records as well as the detailed metadata multisets. Raw SHA-256 is
`f36096a63e6868aa7ebdddb4b8b8c5bd58d507183867a7ab71f7fb3d9ec52610`.
Control/non-LF-separator, CRLF and appended-blank-line negatives also pass
through the real file loader. An
independent FSCAL1 instruction check rejects masking changes without relying
on the whole-image hash.

Replay executes genuine linked instructions and real timebase calls, including
the diagnostic pointer return. It supplies synthetic external peripheral values,
never patched successful returns, skipped branches or private-driver state.
Every dynamic MMIO operand, destructive read, TXFIFO write and permitted
strobe is checked; RX flush/unconditional TX/manual ACK strobes remain denied.
The four settling NOPs execute through their genuine call before admission.
Alias, unused RAM,
status tail, upper IRAM, GPIO/IRQ/clock guards, stack unwind and simulator
whole-run high-water remain enforced. Each simulator process retains its
**15-second** limit.

The original125 scenarios are also pinned independently to their pre-rearm
native results, frame/diagnostic/input snapshots and complete ordered MMIO.
Their canonical JSON digest is
`d15ba16889fcb03932468342741390d9807a7ff652aa342aa6b2accdd87d0a32`.
The verifier runs the same fixed synthetic-address corpus and requires exact
identity, not only the same final return values. All157 pre-TX scenarios,
including the32 rearm cases, also retain their complete native identity:
`c19e89572bc7398c799d9a9240f703e1ac5ae92b93f9d397aeefcbf3859e411a`.
New cases never replace old ones. Historical pre-TX simulator-call envelopes
were3.464s (generic) and3.356s (LG), with whole `run_vector` maxima4.591s/4.560s.
Those measured the4692-byte rearm image, not this7007-byte extension; they
are local observations, not portable timing guarantees.

### Historical overlap-partition acceptance before rearm

The following measurements concern the earlier4410-byte image,
SHA-256 `e814c6d33bb20b74d826fec630d906858dfeaf346fa7e46e6212db9aa7e9ba6b`,
not the current rearm composition above.

Case 94 previously combined 25 configuration and 139 output libc-overlap
rejections with its lifecycle, producing a marginal simulator process that
exceeded 15 seconds in fresh parent acceptance. Its non-inventory lifecycle
remains intact; cases 114..124 now cover the same 164 overlaps exactly once in
11 genuine fresh sequences, at most 16 invalid calls per sequence. Each
rejection preserves state/fault, diagnostics and output without MMIO; each
partition also executes real acquisition, EMPTY and STOPPED postconditions.
The other 113 original sequences are unchanged. The 33 additional API calls
are these real setup/postcondition calls, not reduced coverage or continuation
through debugger-restored state. Every original full snapshot and guard remains.

The proof reports wall time around each unchanged shared simulator call
(including command-file I/O and output validation) separately from complete
`run_vector` time. The former conservatively bounds process time; its reported
margin against 15 seconds is a lower bound, not a CPU-time or RF-timing claim.

Both canonical targets and the complete ASan/UBSan corpora passed after this
partition. Comparing generated vectors against the saved pre-partition native
executable established that all 113 other sequences, case 94's remaining
lifecycle, and every one of the 164 moved steps (including complete MMIO and
expected-state/output records) are identical on both boards.

A separate timing observation around the actual `subprocess.run` calls
forwarded their original arguments, `timeout=15` and results unchanged. Each
board executed all 127 simulator processes: 125 sequences and both alias
controls. Local wall intervals, including process launch/capture/reaping:

| Board | Worst process interval | Margin below 15 s | Worst overlap partition | Case 94 lifecycle |
| --- | ---: | ---: | ---: | ---: |
| generic | 3.891894 s, case 105 | 11.108106 s | 2.085531 s, case 120 | 2.400000 s |
| lg_esl29_rev03 | 3.846995 s, case 105 | 11.153005 s | 1.833472 s, case 120 | 2.340519 s |

The corresponding worst complete `run_vector` times were 5.050 s and 4.726 s.
These are local shared-host measurements, not portable worst-case execution
time guarantees. No timeout, snapshot, memory/ABI guard or firmware byte was
relaxed to obtain them.

Independent parent acceptance in fresh `build/radio-autoack-integration/<board>`
directories also passed both canonical targets and sanitizers. Its maximum
simulator-call envelopes were 3.969 s (generic) and 4.085 s (LG), leaving at
least 10.915 s below the unchanged subprocess limit, with the same CODE digest.

### Reproduction

From the repository root, with the existing toolchain:

```sh
make --no-print-directory -j1 BOARD=generic \
  BUILD=build/radio-rearm/generic test-radio-autoack
make --no-print-directory -j1 BOARD=lg_esl29_rev03 \
  BUILD=build/radio-rearm/lg_esl29_rev03 test-radio-autoack
```

The canonical target now includes the complete native corpus under ASan/UBSan
with recovery disabled; no separate manual sanitizer compilation is required.

## Remaining gates

**Hardware-observed: none for this owner, either board.** Generic 8051 execution
does not establish CC2530 RF/filter/ACK timing, peripheral electrical behavior,
power, physical liveness or compatibility. An earlier separate ordinary-TX
observation is not AUTOACK or timing evidence. This work accessed no equipment
or private hardware material and authorizes no subsequent hardware activity.

A full MAC/POLL adapter still needs a separately proved continuous RX/ordinary
TX/FIFO owner, preserved global ACK-filter compatibility, captured/fresh/ordered
receive and ACK timing (#40), phase/epoch/error bounds, IFS accounting,
bounded TX-action retirement under existing MAC limits, and loss-aware
buffer/window handoff. Physical stop/drain here cannot substitute for POLL
`PREPARED`/`CLOSED`, nor resolve Association timing/revision gates (#45).
Those are explicit future work, not successful adapters hidden behind this API.

### Ordinary TX admission under live AUTOACK

The bounded primary review in #50 found insufficient evidence to add an
ordinary-TX submission entry point under live AUTOACK. This is an unresolved
contract, **not a claim that the silicon cannot support a combined owner**.
SWRU191F (April2009, revised April2014), printed pages, leaves three specific
questions:

| Boundary | Verified fact and unresolved inference |
| --- | --- |
| Admission during pending/active ACK | Section23.8.1 p218 says CCA-qualified STXONCCA aborts ongoing transmission or reception. Fig23-20 p235 instead draws STXONCCA with CCA from "any RX state", versus STXON from "all states". Instruction definitions pp249,253 do not resolve ACK calibration/delay/TX. A stale foreground status sample or SAMPLED_CCA alone does not prove unique acceptance. |
| Ordinary completion identity | RFIRQF1 pp210-211 defines sticky TXDONE and TXACKDONE, but does not expressly establish that AUTOACK can never also assert TXDONE. Clearing/verifying a flag proves freshness, not unique ordinary-action attribution. TX_ACTIVE also covers ACK states48..55 (Table23-3 p236). |
| TXFIFO flush during ACK | Fig23-6 p220 states that RX/RXFIFO and ACK activity do not affect TX-buffer state. This supports preparation from known-empty FIFO, not the reverse guarantee that SFLUSHTX cannot disturb an ACK. The generic underflow warning in23.8.3-5 pp218-219 and instruction descriptions pp250,254 do not settle every ACK phase. |

Mode3 CCA and the hardware-gated strobe are not equivalent to a software CCA
sample followed by STXON; CPU interrupt masking cannot stop autonomous radio
transitions. SWRZ031 sections1/Table1 pp2-3 supplies no arbitration clarification.
Do not encode guessed admission/completion behavior in a successful synthetic
model or bypass old reset-exclusive FIFO guards.

Persistent RX after ordinary/ACK TX (RXENABLE p260) and non-aborting soft stop
(section23.9.1 p223, Fig23-20 p235) remain supported foundations. The stop
sequence must forbid new submissions, preserve AUTOACK, confirm fresh RFIDLE
and physical inactivity, then explicitly drain complete frames. None supplies
IFS, timestamps or POLL closure. Further vendor clarification or separately
scoped controlled hardware evidence is needed for the unresolved transitions;
no such hardware experiment was performed by this review.

The later #72 extension implements only the independently documented
same-owner soft RX rearm after STOPPED. It neither exercises nor resolves the
three ordinary-TX transitions above. It is the first reusable ownership
transition toward a combined service, not permission to chain the old
reset-exclusive TX/FIFO APIs into the gap.
