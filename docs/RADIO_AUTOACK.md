# Filtered receiver/AUTOACK owner

**Original offline hardware-service foundation for #48, not a MAC adapter or
hardware acceptance record.** [radio_autoack.h](../include/radio_autoack.h) and
[radio_autoack.c](../src/radio_autoack.c) establish one reset-exclusive filtered
receiver with hardware-generated acknowledgments, bounded foreground FIFO
servicing, and explicit non-aborting stop/drain.

AUTOACK **transmits RF without CPU intervention**. Actual use requires separately
authorized RF-transmitting ownership and CPU progress. This change adds no board
image, hardware runner, RF authorization, ordinary TX submission, TXFIFO reuse,
CSP program, DMA/ISR, GPIO policy, MAC Timer composition, MAC/security acceptance
or membership. `radio_autoack_test.ihx` is a standalone synthetic executable:
**never flash it or distribute it as a board firmware artifact**.

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
standard modem control and an idle CSP. The owner changes no CCA setting and
offers no CCA decision. It writes no source-match table, TXFIFO, RFST or GPIO.
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
| Stop -> `STOPPED` | Physical idle and empty hardware FIFO after explicit servicing, with consistent stopped ring pointers. Enter terminal OFF; no reacquisition. |
| Invalid argument/range/storage/state | No MMIO, output or diagnostic mutation. |
| Operational error | Enter terminal FAULT and retain the first error and ownership. Later acquire/receive/stop calls return it with no MMIO, output or diagnostic mutation, even with invalid arguments. |

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
bit with `RXMASKCLR=01`. AUTOACK stays enabled throughout reception/ACK
completion. Require fresh RFIDLE, RXENABLE zero, calibration inactive, PLL
unlocked, SFD low and RX_ACTIVE/TX_ACTIVE both low. Once stopped, also require
`(RXFIRST_PTR+RXFIFOCNT) mod 128 == RXLAST_PTR`; a full 128-byte FIFO is distinct
from empty despite identical pointers. Complete frames remain available.
An unexplained partial FIFO, pointer/count contradiction or RF error is a
fault, never drained success. There is no abort, SRXON, flush, hidden discard,
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
completion, ordinary-TX permission, captured time or release to another
init-time API. Filter-rejected traffic is not delivered. Any later software
queue loss also prevents a future adapter from claiming loss-free closure.

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
soft stop, drain and off. `sample_valid` marks a complete observation;
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
| radio_autoack | 3430 | 234 | 3 | 2 |
| test caller | 272 | 156 | 2 | 0 |

Whole image: **4410/24576 CODE**, **427 ordinary XDATA + the entire 64-byte
status reservation = 491/1536 bytes**. Runtime/startup adds 304 CODE and 12
XDATA beyond those object totals. The separate 24 KiB/1536-byte budget covers
the real timebase, one staged frame, copied configuration, diagnostics,
compiler parameters, caller buffers and runtime; it changes no old service
budget and establishes no full-stack/POLL fit.

The entire private prefix is `0000..0102`, caller allocation `0103..019E`,
and libc scratch `019F..01AA`. The latter includes memcpy parameters **and
its private temporary**, memset parameters and generic-store scratch. Both
input and output ranges exclude that whole suffix; only excluding
`__gptrput_PARM_2` would be insufficient. The proof binds its actual map,
allocation and emitted runtime layout. A different composition needs its own
complete ownership/layout proof, not an assumed library ordering.

Stack starts at `21`, initial SP `20`; caller checkpoints unwind to SP `22`.
The genuine whole-run maximum is **SP `33`** (19 bytes above initial SP),
separate from MMIO-sampled maximum **`2D`**. The selected bound remains `7C`;
upper IRAM `80..FF` stays intact. No separate RAM is allocated at the
`1F00..1FFF` IRAM alias. All 56 unused status-tail bytes and other unallocated
RAM are guarded.

Whole emitted CODE SHA-256:

```text
e814c6d33bb20b74d826fec630d906858dfeaf346fa7e46e6212db9aa7e9ba6b
```

**Host-tested:** 155490 API calls per board, both strict native and ASan/UBSan.
Coverage includes all bounded lengths/CRC bytes, address/profile bits,
every value of each caller address byte, copied-configuration independence,
configuration/output ownership including every libc-scratch overlap, native
object preservation, all FSCAL1 upper-bit patterns, and faults after each
possible destructive read. Synthetic bodies include Security Enabled,
Ack Request, incoming Pending and broadcast destinations; the model does not
implement over-air filtering, FCS calculation or ACK generation. Lengths
6..8 deliberately overapproximate the configured filter's possible frames
to exercise byte bounds, not claim those are eligible over-air packets.

**Image-checked/simulated:** 125 persistent sequences, 759 genuine API calls,
549 exactly-once RFD reads, and 6248 artifact negatives plus one genuine
missing-alias negative per board. Coverage includes delayed readiness/
calibration, active receive/ACK soft stop, 21 queued frames, max-length circular
FIFO, concurrent arrival, CRC classification, stale flags, partial/count/
head/tail/overflow/underflow faults, equality/work boundaries and terminal
no-MMIO/error-output preservation.

The proof binds all CODE/constants/runtime bytes, complete public/private/
helper/caller/field F/S/L/T record multisets with original duplicate
multiplicity, source/storage associations, every map symbol, all-area
allocations, entry/end/storage labels, and all three ordered instruction
inventories. Missing, conflicting, malformed and duplicate records reject.
CDB is read as raw UTF-8 bytes without newline normalization; control/
non-LF-separator negatives also pass through the real file loader. An
independent FSCAL1 instruction check rejects masking changes without relying
on the whole-image hash.

Replay executes genuine linked instructions and real timebase calls, including
the diagnostic pointer return. It supplies synthetic external peripheral values,
never patched successful returns, skipped branches or private-driver state.
Every dynamic MMIO operand and destructive read is checked. Alias, unused RAM,
status tail, upper IRAM, GPIO/IRQ/clock guards, stack unwind and simulator
whole-run high-water remain enforced. Each simulator process retains its
**15-second** limit.

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
  BUILD=build/radio-autoack-dev/generic test-radio-autoack
make --no-print-directory -j1 BOARD=lg_esl29_rev03 \
  BUILD=build/radio-autoack-dev/lg_esl29_rev03 test-radio-autoack
```

The complete native corpus under both sanitizers:

```sh
(
  set -e
  for board in generic lg_esl29_rev03; do
    case "$board" in generic) id=0;; lg_esl29_rev03) id=1;; esac
    cc -std=c99 -O1 -g -Wall -Wextra -Werror -pedantic \
      -fsanitize=address,undefined -fno-omit-frame-pointer \
      -DCC2530_HOST_TEST -DCC2530_BOARD="$id" -Iinclude -Itests \
      tests/test_radio_autoack.c src/timebase.c src/radio_autoack.c \
      tests/host_mmio.c \
      -o "build/radio-autoack-dev/$board/host-radio-autoack-sanitized"
    ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
      "build/radio-autoack-dev/$board/host-radio-autoack-sanitized"
  done
)
```

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
