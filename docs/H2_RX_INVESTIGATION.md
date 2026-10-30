# H2 receive-path investigation (offline)

Status: **offline investigation complete. The first cause is not determinable from preserved
evidence.** The missing evidence is identified precisely below, together with a diagnostic ABI
design that would capture it. This investigation changes no firmware, compiler or production
default. No hardware or RF operation was performed.

Scope: the intermittent early-join failures on the H2 board. SDCC had 2/4 authenticated joins
and LLVM had 4/6 (study §23). H1 is damaged and was not used.

## 1. Source and image identities

| Role | Identity |
|---|---|
| SDCC trial image (SH2, S1–S3) | HEX `518c8aa4ef6ab472…`, firmware source `7b0eb693` |
| LLVM trial image (LH2–LH4, L1–L3) | HEX `1b865401c616aab6…`, firmware source `b27f950` (frozen hardware compiler `a3c9a84a4`) |
| Current development baseline | llvm `mcs51-dev` `1a01afb` (codegen = `2feda4a48`), firmware `llvm-dev` `3b84bbc`/`4dc5a26`, HEX `a1f47b14b960762f…` (FULL PASS, run 37996810558) |

`7b0eb693` is an ancestor of `b27f950`. Between them, the radio/MAC sources differ only by
compatibility macros (`volatile`→`SDCC_VOLATILE`, includes). **The receive logic is identical in
both trial images.** The current `llvm-dev` differs from `b27f950` only in `src/flash_exec_mcs51.s`.

## 2. Trials considered

Ten preserved H2 trials (all read-only, under `~/.local/state`): SH2, LH2, LH3, LH4 and the
interleaved series S1, L1, S2, L2, S3, L3. Each has a terminal snapshot (`terminal.json`), a
journal (`run.jsonl`: before-RF, resume, heartbeats, final/halt only) and an 8 MB HA DEBUG
coordinator capture. LH2 and LH3 keep theirs in `permit-*`/`coordinator-final*.txt`.

Normalized terminal state:

| Trial | Result | BDB phase/result | WS | Att | Sec phase | Frames | Lossy | Driver fault | Elapsed |
|---|---|---|---|---|---|---|---|---|---|
| SH2 | SDCC PASS | 13 READY | 3 | – | 8 VERIFIED | 6829 | 0 | 0 | 410 s |
| LH2 | LLVM FAIL | 17 / 11 `TRANSMIT_FAILED` | 3 | 1 | 9 LEFT | 98 | 0 | 0 | 15.6 s |
| LH3 | LLVM PASS* | 13 READY | 3 | 1 | 8 | 1092 | 0 | 0 | 77.8 s |
| LH4 | LLVM FAIL | 17 / 10 `KEY_TIMEOUT` | 3 | 1 | 9 LEFT | 231 | **1** | 0 | 15.4 s |
| S1 | SDCC FAIL | 2 ASSOCIATING | 2 | 2 | 1 | 36 | **1** | 9 `COVERAGE` | 5.1 s |
| L1 | LLVM PASS | 13 | 3 | – | 8 | 1935 | 0 | 0 | 135 s |
| S2 | SDCC PASS | 13 | 3 | – | 8 | 2149 | 0 | 0 | 134 s |
| L2 | LLVM PASS | 13 | 3 | – | 8 | 907 | 0 | 0 | 65 s |
| S3 | SDCC FAIL | 2 ASSOCIATING | 2 | 1 | 1 | 18 | **1** | 9 `COVERAGE` | 4.2 s |
| L3 | LLVM PASS | 13 | 3 | – | 8 | 1702 | **1** | 0 | 66.6 s |

\* LH3 firmware was READY; the run was aborted by a host HTTP 400 (§23).

`RFERRF` was 0 and `radio_work.overflow` was 0 at every terminal. Radio phase 7 / result 5 is
STOPPED, a benign radio-off state, so it is not a loss signature (see the §23 correction below).

## 3. Confirmed RX overflow events

`MAC_RADIO_RX_LOST` has exactly one source: `radio_autoack.c:436`, reached after a latched
`RFERRF.RXOVERF` together with the FIFOP-without-FIFO signature. `mac_adapter` turns it into
`lossy = 1`, which stays set until the next prepare/open. Therefore:

- **Confirmed hardware RX FIFO overflow** (≥1 in the final adapter window): S1, S3, LH4, L3.
- SH2, LH2, LH3, L1, S2, L2: no overflow in the **final** window. Earlier windows are not
  observable, because lossy is cleared on open and no counter exists.
- The number of overflows, their times and the frames involved are **not recorded**.

## 4. Earliest known failure checkpoint

Coordinator times are seconds after the firmware RF resume. They come from a different host
clock, so there is an unknown skew of ±tens of ms to seconds.

| Trial | Earliest firmware checkpoint | Coordinator |
|---|---|---|
| S3 | Association workspace, attempt 1: overflow → fail-closed `COVERAGE`; halt ≈4.25 s | UNSECURED join 4.186; no CHILDJOIN |
| S1 | Association workspace, **attempt 2**: overflow → `COVERAGE`; halt ≈5.1 s. Attempt 1 failed **without** a recorded overflow | UNSECURED join 4.311 **and** 5.063; no CHILDJOIN |
| LH4 | Associated; WAIT_KEY in the runtime workspace; overflow tolerated (lossy=1); no network key within the 10 s `BDB_JOIN_SECURITY_WAIT` (625000 ticks) → `KEY_TIMEOUT` | UNSECURED join 4.441; ZDO unicasts at 4.71/6.47/14.63 unanswered; no CHILDJOIN; **no DEVICE_LEFT** |
| LH2 | Network key received; device announce, then the Node-Descriptor exchange succeeded (BDB reached REQUESTING); the APS Request-Key transaction completed with an error → `TRANSMIT_FAILED`; final window not lossy | UNSECURED join 7.837; CHILDJOIN 10.346 plus the device's secured ZDO frame 10.353; **no key-request response**; DEVICE_LEFT 15.60 |

CHILDJOIN is the coordinator's first callback triggered by a network-key-secured frame from
the device; the secured ZDO frame follows within 7 ms. In passing runs it arrives 2.5–2.7 s
after the UNSECURED join.

LH4's lack of a DEVICE_LEFT is consistent with the device never holding the network key: it
could not send a secured leave. LH2, which held the key, produced DEVICE_LEFT.

**Two distinct failure signatures exist:**
- **A — post-association RX loss before the network key:** S1, S3 (SDCC) and LH4 (LLVM).
- **B — uplink failure after the network key:** LH2 (LLVM). There is no RX-overflow evidence.
  The local MAC outcome (NO_ACK or CCA) is not recorded.

## 5. FIFO and service observations

Driver structure (`src/radio_autoack.c`):
- **There is no RX ISR.** `observe()` requires `IEN0/1/2 == 0`, and reception is
  foreground-polled through `radio_autoack_receive`.
- The 128-byte RX FIFO is the only producer and has no software concurrency, so there is no
  ISR/consumer publication race.
- Each `consume()` byte performs `progress()` (timebase read), checks `RFERRF`, and pins
  `RXFIFOCNT`/`RXFIRST`.
- Each poll (`observe()`) re-verifies about 25 owned radio registers.

Overflow handling:
- Each receive call salvages one complete head (`complete_head`: PHR ≥ 5 and count > PHR).
  Every AUTOACKed complete frame is therefore delivered before the flush.
- Only an incomplete tail is discarded. The order is: `ISFLUSHRX` once → `RFERRF = 0xfb` →
  wait for `RX_ACTIVE` (live) or idle+RFIDLE with an empty FIFO (drain) → `RX_LOST`.
- L3 and LH4 continued receiving after their overflow without any FIFO or controller fault
  (LH4 for about 10 s, L3 to READY). **The recovery path works on hardware.**

The driver contract (`include/radio_autoack.h`) documents deliberate **reception gaps**:
- stop → OFF → `configure`/`resume`, where frames sent during the gap are not received;
- a TX attempt enters `RX_NOACK` with filtering and AUTOACK disabled.

The device advertises `RxOnWhenIdle` (association capability `0x88`/`0x8c`, `mac_join.c:335`),
so the parent sends direct, not indirect, frames after association.

ISR-to-consumer timing does not apply, because there is no ISR. Foreground service latency
(the largest gap between consecutive FIFO services while RX is enabled) has **never been
measured** on hardware, and the reference simulator replays recorded peripheral vectors. The
simulator therefore cannot reproduce physical overflow timing.

## 6. Coordinator-side evidence

Masked node timelines and EZSP counter deltas were extracted read-only. The coordinator
counters (10 s buckets) are **global** to the whole PAN, not per node.

| Trial | Outcome | `MAC_TX_UNICAST_RETRY` / `FAILED` in the join bucket |
|---|---|---|
| SH2 | PASS | 36 / 11 |
| S1 | FAIL | 34 / 11 |
| S2 | PASS | 15 / 5 |
| S3 | FAIL | 15 / 5 |
| L1 | PASS | 6 / 2 |
| L2 | PASS | 28 / 7 (next bucket 123 / 41) |
| L3 | PASS | 6 / 2 (next bucket 63 / 20) |
| LH2 | FAIL | 7 / 2 |
| LH3 | PASS | 10 / 2 |
| LH4 | FAIL | **3 / 0** |

Pre-join buckets: 1 MAC failure in 20 buckets.

- Join windows raise coordinator MAC unicast failures in 9 of 10 trials, **independent of
  outcome**. LH4, a failure, had none.
- The counters therefore do not discriminate success from failure, and they cannot attribute
  failures to the H2 node.
- `ASH_*` errors were 0 and `PHY_CCA_FAIL` was flat. There is no coordinator-host transport
  fault.
- The coordinator logs contain no per-frame delivery status for ZDO/APS transport-key frames,
  because the NCP sends the transport key itself.

## 7. SDCC versus LLVM

| | SDCC | LLVM |
|---|---|---|
| Signature A (RX loss before key) | S1, S3 | LH4 |
| Signature B (uplink after key) | – | LH2 |
| Overflow tolerated in a passing run | – | L3 |

The **same logical mechanism** (a hardware RXOVERF in the post-association window, within
identical receive source) occurs in both compilers.

The outcome depends on which workspace the overflow lands in:
- in the association workspace, `mac_link_driver.c:103` fails closed with `COVERAGE` (S1, S3);
- in the runtime workspace, it is tolerated: L3 passed, while LH4 never obtained the network key.

The SDCC/LLVM split of where the overflow landed (2/2 vs 0/1) is n=3 and supports no claim.
Instruction-timing differences are not evidence of miscompilation. FULL proves the LLVM image
semantically against the reference simulator.

## 8. Correctness audit: proven source defects

**None proven.** Audited items:

| Item | Finding |
|---|---|
| FIFO draining under burst | Bounded per byte. Throughput relative to the 250 kbit/s air rate has not been measured on hardware (gap). |
| Overflow recovery ordering | Salvage → single `ISFLUSHRX` → clear RXOVERF → re-verify. Recovered on hardware (L3, LH4). Whether TI requires a double flush is **unverified**; no primary source was consulted, so it is not claimed. |
| Interrupt windows / preemption | IEN0/1/2 are verified zero on every poll, so there is no preemption. |
| Volatile register access | Through the MMIO macros. Proven by FULL generic-pointer/SFR checks and by the SDCC trial images. |
| Producer/consumer publication | There is no software producer. The FIFO head is pinned per byte (`RXFIRST` and the remaining count). |
| State after flush | `overflow` goes 2 → 0 only after RX_ACTIVE, or after idle with an empty FIFO. |
| Stale RX events | `lossy` is sticky per window; RX_LOST outside the RX/COLLECT/CLOSING/DRAINING phases is a `RADIO_ERROR`. |
| Partial-frame delivery | Impossible: `complete_head` is required, and CRC_OK is published separately. |
| `COVERAGE` on association loss | Intentional fail-closed policy (no proof covers association-window loss). It converts a recoverable never-ACKed loss into a join abort. This is a **policy decision, not a defect**. |
| RxOnWhenIdle with documented RX gaps | A protocol-level exposure: a parent's direct frame landing in a gap is not ACKed, and the sender's MAC retries may expire. Not proven to be the failure cause, because the gap durations are unmeasured. |

Host tests on the current `llvm-dev` (2026-10-10):

- **PASS (synthetic):**
  - radio_autoack, including the RX-loss variants (8 and 14 flushed overflows, LINK normal-RX salvage);
  - mac_radio (54 cases);
  - mac_adapter (raw-RX loss reporting);
  - mac_link scan (lossy/rescanned closure);
  - mac_link join (146 cases).
- **Blocked:** link-direct and join-smoke host builds, plus the SDCC boot checks of
  radio_autoack, mac_radio and mac_adapter. They pin raw SDCC CDB digests from before the
  `llvm-dev` compatibility commits (2a03445…91d8c6a). This stale pin predates this
  investigation; the gate is the LLVM tiers.
- **LLVM FAST: PASS 11/11, 21.4 s.** HEX `a1f47b14…`, identical to the FULL-qualified image.
- SMOKE and FULL were not run: there was no runtime change.

## 9. Unresolved evidence gaps

The first cause cannot be decided between three hypotheses:
- (a) foreground service latency during post-association processing;
- (b) a burst from the parent (direct frames plus MAC retransmissions caused by non-ACK
  intervals);
- (c) channel traffic accepted in unfiltered `RX_NOACK` intervals.

The following are missing:

1. Overflow count and time per window (only the final window's boolean survives).
2. The FIFO snapshot at detection: count, first, last, PHR, signals, and the radio state
   (RX, RX_NOACK or DRAIN), plus the operation phase.
3. The header (FCF, DSN, length) of the salvaged heads and of the last good frame before the
   loss. Without it we cannot tell whether the transport key was the frame lost.
4. Maximum foreground service gap while RX is enabled, and RX gap durations (stop → resume)
   around association.
5. BDB phase, workspace and adapter phase/goal at the first loss.
6. For signature B: the BDB phase at failure and the local MAC outcome of the failing uplink
   (NO_ACK, CCA busy, retries).
7. Coordinator per-node MAC delivery status. The EZSP global counters are insufficient; a
   sniffer would be authoritative.
8. Why S1 attempt 1 failed with no recorded overflow (association response missed, or a
   HANDOFF race).
9. Inter-host clock skew between the operator and HA.

## 10. Diagnostic ABI `CC2530_RX_DIAG` (implemented, LLVM image only)

The ABI is implemented behind the build flag `CC2530_RX_DIAG`.
- The authoritative layout is [`include/cc2530_rx_diag.h`](../include/cc2530_rx_diag.h). Every
  offset is pinned by a static assert, and `tools/test_rx_diag_decode.py` checks the decoder
  against the header.
- With the flag off, the admitted images are unchanged (proof below).
- The block holds no payload bytes, keys, IEEE addresses or short addresses.
- It is passive: no control decision reads it, and it performs **no extra MMIO**. The values
  it records are the ones the driver already reads.
- There is no printf.

### Layout, version 1 (80 bytes, little-endian, naturally aligned)

| Off | Field | Meaning |
|---|---|---|
| 0 | `sig[4]` | `"RXDG"` |
| 4 | `version` | 1 |
| 5 | `size` | 80 |
| 6–9 | `overflows`, `salvaged`, `flushes`, `rx_lost` | u8, saturating at 255 |
| 10 | `frames` u16 | good frames delivered (adapter and attempt paths), saturating |
| 12 | `bad_crc` u8 | bad-CRC frames dropped |
| 13 | `first_valid` | 1 once the first-overflow snapshot is latched (first wins) |
| 14 | `max_service_gap` u16 | largest gap between FIFO services with RX on |
| 16 | `max_rx_off_gap` u16 | largest RX stop → RX-on interval |
| 18 | `first_site` | 1 = observe(), 2 = consume() |
| 19–20 | `first_radio_state`, `first_radio_phase` | driver state at the first overflow |
| 21 | `first_rferrf` | RFERRF as read by the driver |
| 22–23 | `first_fsmstat0`, `first_fsmstat1` | FSMSTAT0/1 as read by the driver |
| 24–26 | `first_rxfifocnt`, `first_rxfirst`, `first_rxlast` | FIFO status at the first overflow |
| 27 | `flags` | bit 0 = service stamp valid, bit 1 = off stamp valid |
| 28 | `first_stamp` u32 | sleep-timer tick at the first overflow |
| 32 | `first_gap` u16 | service gap at the first overflow |
| 34 | `first_frames` u16 | `frames` at the first overflow |
| 36 | `last_header[4]` | last good frame: FCF0, FCF1, DSN, PHR |
| 40 | `first_header[4]` | `last_header` at the first overflow |
| 44, 48 | `service_stamp`, `off_stamp` u32 | internal bookkeeping |
| 52–56 | `ctx_valid`, `ctx_bdb_phase`, `ctx_workspace`, `ctx_adapter_phase`, `ctx_adapter_goal` | link-driver context at the first lossy observation |
| 57–63 | `fail_valid`, `fail_bdb_phase`, `fail_result`, `fail_workspace`, `fail_aps_result`, `fail_aps_retries`, `fail_aps_error` | first `bdb_join` failure |
| 64–68 | `tx_valid`, `tx_outcome`, `tx_retries`, `tx_nb`, `tx_special` | MAC outcome of the first failed uplink (0x80 = APS ACK) |
| 69 | pad | 0 |
| 70 | `last_service_gap` u16 | gap at the most recent service |
| 72 | `reserved[8]` | 0 |

### Time values and saturation

- Ticks are 24-bit Sleep Timer counts, nominally 32768 Hz, in the CC2530 clock domain only.
- Gaps saturate at 0xFFFE; 0xFFFF means unknown.
- A gap is measured from the start of the service operation, so it includes polling latency.

### Known limits

- An overflow inside the MAC attempt path, while a request is waiting for its ACK or reply, is
  already a hard `FIFO_ERROR` fault there. The block does not latch it; only `frames`,
  `bad_crc` and `last_header` are recorded on that path.
- The block has no RF loss counter.

### Placement and reading

The block is a NOBITS section `.mcs51_rx_diag` at a fixed XDATA address, outside `.bss`.
- `tools/llvm/inputs/rx_diag_mcs51.s` and `rx_diag.ld` place it.
- `join_smoke_initialize` writes every byte at reset.
- Linker asserts require:
  - exactly 80 bytes at 0x1E40;
  - the block is above the fixed M0 status block;
  - the block is below the IRAM alias at 0x1F00.
- The decoder takes the address from the linked ELF symbol `cc2530_rx_diag` and never
  hard-codes it.
- Halt-state read through the existing HWLab XDATA API. It needs an existing debug session
  with the CPU **already halted**: no halt, reset or resume, and no RF-register access by
  HWLab.

  ```
  hwlab target memory read --plugin ccdebugger --yes --config backend=native \
    --config access=existing-debug-session --config allow_memory_access=true \
    --space xdata --address 0x1E40 --length 80 --path rx_diag.bin
  ```

- Decode the dump with
  `tools/rx_diag_decode.py --elf DIAG_HW1_ELF --dump rx_diag.bin [--output report.json]`.
  The decoder checks the signature, version and size, and flags inconsistent blocks instead of
  hiding them. `--print-read-command` prints the HWLab command with the address taken from the
  ELF.
- Correlate with a capture:
  `tools/rx_diag_capture.py nrf802154-ch15.pcapng --rx-diag report.json [--output capture.json]`.

### Compiler coverage

**LLVM only.** The SDCC image cannot hold the diagnostic code. This was measured at 389b301
with `tools/rx_diag_sdcc_build.py` and the real banked CODE packer; the verdict is INFEASIBLE.
- Bank-group growth is +1532 B:
  - `JS_RADIO` +914;
  - `JS_NWK` +295;
  - `JS_BDB` +162;
  - `JS_mac_link_driver` +161.
- Total bank space still has room: 2501 B free shrinks to 969 B.
- The problem is `JS_RADIO`. It must stay in one bank, and that bank has only 34 B of headroom,
  so no packing fits.

The SDCC feature-OFF proof at the same commit is clean:
- The physical HEX `518c8aa4…` and the final IHX `df86611e…` equal the admitted image.
- All resource deltas are 0. Static stack is 45 and bank depth is 8.
- Only the debug and listing identities differ (line records; the catalog predates the
  `llvm-dev` compat commits).

SDCC trials therefore use the production SDCC image, with the sniffer as the only extra
observer.

### Offline analysis of HWLab sniffer captures

`tools/rx_diag_capture.py` analyses HWLab nRF52840 captures: pcapng with link type 283
(IEEE 802.15.4 TAP), using tshark for dissection.
- It reports frames, MAC ACK pairing and retry candidates. Each conclusion is classed
  PROVEN, INFERRED or UNKNOWN.
- It matches CC2530 `last_header` / `first_header` by content only (FCF, DSN, PHR). It never
  aligns by clock, because the sniffer timestamps are HWLab session time and are a different
  clock domain from the CC2530 sleep timer.
- Limits of the capture:
  - No FCS is delivered, so FCS validity is UNKNOWN. tshark's `fcs_ok` is not evidence.
  - There is no sniffer-side RF loss counter; `isb_ifdrop` counts only host event-sink drops.
  - A missing ACK in the capture is "not observed", not "not sent".
  - The same DSN can be ambiguous.
- 64-bit addresses are reported only as salted hashes.
- Raw captures and private identities stay outside the repository.

### Verification

| Check | Result |
|---|---|
| Host radio tests: reset, gaps, saturation, headers, observe/consume latch, first-wins, salvage/flush/loss, FSMSTAT1/RFERRF race, stop/resume | PASS (plain, adapter and link builds) |
| Host attempt path: frames and headers from attempt replies | PASS (12 replies) |
| Mutation checks (counting, `first_site`, RX-on accounting, `off_stamp`, attempt hook) | all caught |
| Flag-off vs flag-on MMIO trace digest (`tests/rx_diag_trace_identity.py`) | identical: 194 radio and 33 attempt vectors; digests 9b879f71, 5a709085, feedbcf7 |
| SDCC host test images (radio, attempt), flag-off vs a212547 | IHX byte-identical |
| SDCC production feature-OFF (`rx_diag_sdcc_build.py`) | physical HEX `518c8aa4…` reproduced; resource deltas 0 |
| Decoder and capture tests (`make test-rx-diag`) | 22 PASS |
| LLVM FAST, production | PASS; HEX `a1f47b14…` unchanged |
| LLVM FAST, `rx-diag` | PASS; HEX `59d7bfc1…`, CODE +1480, stack 106/128 |
| LLVM SMOKE, `rx-diag` | PASS 17/17; decoded sim blocks are consistent, with no overflow |
| LLVM FULL, `rx-diag` | PASS, run 38062355155: 22/22, case 0 serving 1142/1142, stack 106/128 (peak 101), 0 bad thunks |

## 11. Focused test results

See §8.

## 12. Is a code change justified?

**No functional change.** No source defect is proven. Changing the `COVERAGE` policy or
`RxOnWhenIdle` would be a protocol decision without demonstrated cause. The **diagnostic-only**
build in §10 is now implemented. It is opt-in and LLVM only.

## 13. Is another hardware trial justified?

**Not with the current images.** Repeating them adds pass/fail counts but no first-cause
evidence. A trial is justified only with the LLVM diagnostic image after it has passed FAST,
SMOKE and FULL, and it needs separate operator authorization.

## 14. Next bounded experiment

1. Done: §10 is implemented behind `CC2530_RX_DIAG` for the LLVM image, with offline tests.
   FULL on the `rx-diag` variant is the remaining gate. The SDCC counterpart does not fit (§10).
2. After authorization, run on H2 only (no H1): one bounded interleaved pair.
   - SDCC production image: sniffer only.
   - LLVM `rx-diag` image: sniffer plus the halted RX_DIAG read.
   Use the existing single RF resume, the coordinator-only permit and the join+key gate, with
   no added retries.
3. Run the existing HWLab nRF52840 passive capture on channel 15 across each trial. Arm it
   before commissioning and stop it after the terminal READY or failure. Read RX_DIAG only
   when the CPU is already halted at the terminal checkpoint, never during reception.
4. Decide from the first-loss snapshot:
   - a large `first_gap` / `max_service_gap` points to (a);
   - several parent-addressed retransmissions in the FIFO point to (b);
   - foreign frames in an `RX_NOACK` interval point to (c).

## 15. LH7 first-cause evidence and RX interrupt remedy

LH7 (H2 against the home ZHA network, EZSP, channel 15; PR #102) failed with BDB
`KEY_TIMEOUT` after a successful Association (SUCCESS, short 0x0a7a, AUTOACKed). The
107-byte APS Transport-Key, sent as sequences 62/63/64 over eight attempts, was never ACKed
by H2. In each RX window a 93-byte NWK broadcast relayed by 0x0000 preceded it. The
coordinator's post-join route storm repeated about every 10 ms, and 94 + 108 FIFO bytes
exceed the 128-byte RXFIFO. A 61-byte Association Response at the same −85 dBm was ACKed,
so signal level is not the cause. This selects hypothesis (a): foreground service gaps.

**Remedy (source change, LLVM production image):** `CC2530_RADIO_RX_ISR` adds a leaf RF
ISR. It runs only while IEN0.EA is set between owner operations and copies each complete
FIFO head into a 4-slot XDATA queue. See `docs/RADIO_AUTOACK.md` "RX interrupt" for the
contract, gates and limits. The stack proof now includes the ISR: 106 + 14 = 120 of 128
bytes. Host E2E runs the real ISR against the MMIO model, and MCUEMU checks join function.
A busy-PAN join on H2 (LH8) is the remaining hardware gate. Flash windows with EA cleared
still use the existing RX_LOST path.

## 16. LH8: queued Association Response rejected at POLL closure

LH8 ran the RX ISR image (`2fa4f9cd…`) against the same network and failed with BDB
`ASSOCIATION_FAILED`. The terminal XDATA snapshot shows that the radio path worked. The ISR
had queued two Association Responses (SUCCESS, short 0xcf4d, CRC OK), and the association
record decoded one as a response. The failure came from the POLL layer instead:

- ACK end 45949, receive end 47985, response stamp 48666 (symbols).
- The receive window expired first. The POLL drained, and the adapter took its pre-stop
  watermark.
- The stop then popped the queued response, stamping it at the pop time, 681 symbols after
  `receive_end`.
- The LINK POLL accepts such an AUTOACKed frame (commit `0d8c0e1`). The CLOSED check still
  required `through >= stamp`, so it reported `ADAPTER_ERROR`. That became `CLEANUP_FAILED`,
  `MAC_JOIN_POLL_ERROR` and then `ASSOCIATION_FAILED`.

The check compared an observation upper bound with a coverage lower bound. LINK CLOSED now
requires stamp coverage only for PENDING_ZERO. A delivered frame preceded CLOSED, and the
radio AUTOACKed it while RX was on. `tests/test_mac_link_join.c` reproduces the LH8 order:
the old code fails and the fix passes. A busy-PAN hardware join (LH9) remains the gate.

LH9 (image `35ea6bf6…`) then joined, was interviewed by ZHA and stayed READY through a
600-second soak with 5/5 attribute reads.

## 17. LH10: a frame still in reception read as a finished head

LH10 re-added the LH9 image to ZHA after a fresh removal. It joined at +57 s and was
interviewed, and one read succeeded. About 65 s after the start it faulted with
`RADIO_AUTOACK_FIFO_ERROR`, reported up the stack as adapter FAULT and link driver RADIO.
The terminal snapshot of the radio driver status shows:

- Phase 4 (receive), 2 polls, 0 bytes read. The first `consume()` failed its count/head
  recheck.
- The ISR queue was empty (put = get = 72, no halt).
- FSMSTAT1 = 0xad: FIFO and **SFD** high, FIFOP low. RXFIFOCNT 7 and PHR 6, so the FIFO held
  exactly one 7-byte frame.

The bytes counted in the FIFO belonged to the frame still being received. With SFD high its
FCS/metadata were not final, and filtering could still reject it. This short 6-byte MPDU is
malformed for any addressed frame type. Rejection removes the frame from RXFIFO, so the
recheck found the count gone.

Before the ISR, live RX waited for FIFOP. The ISR rule for heads it leaves behind with FIFOP
low (SWRU191F p.232) accepted any count-complete head, and the vector loop used the same rule.

`finished_head()` and the vector now accept a count-complete head without FIFOP only in two
cases:
- SFD was low when FSMSTAT1 was sampled, which happens before RXFIFOCNT.
- A later frame's byte follows the head.

A head left behind this way is read at the next RXPKTDONE or the next foreground receive.
`tests/test_mac_link_e2e.c` adds an in-flight tail that is later rolled back; the old ISR
and the old foreground rule each read it. It also adds a finished head followed by a frame
in reception, which must still be queued. Image sleepy13 (`911a3529…`) carries the fix.

## 18. LH11: one busy CCA aborted the TC Node Descriptor query

LH11 ran sleepy13 against the same ZHA network. It associated at +43.6 s
(short 0x3a3a), took the network key and broadcast Device_annce at +45.37 s.
About seven routers re-broadcast that announce during the next ~70 ms. The
device's own Node_Desc_req to the TC (NWK frame counter 1) never appeared on
air. Its Node_Desc_rsp to zigpy's request (counter 2) did, at +45.74 s, and a
NWK Leave followed at +46.59 s.

The halt snapshot shows:
- BDB FAILED with `TC_FAILED`, attempts 1, `got_tc` 0.
- ZDO `result` 7 (`ZDO_RUNTIME_TRANSMIT`) and an all-zero TC descriptor.

`zdo_runtime` sets TRANSMIT only when the query's transport confirm is
non-zero. In NODE, `bdb_join` retried only TIMEOUT, so one failed MAC delivery
during the announce re-broadcast burst ended commissioning. PARENT and the
Request-Key/Verify-Key transmissions had the same single-shot rule. READY
keepalives already retried TRANSMIT.

These phases now retry TIMEOUT and TRANSMIT alike within `BDB_JOIN_ATTEMPTS`.
`tests/test_bdb_join.c` (`transient_transmit`) injects five busy CCAs (one MAC
channel-access failure) into NODE, REQUESTING, VERIFYING and PARENT:
- One failure must still reach READY with exactly two attempts.
- Sustained busy must fail after three attempts with TC_FAILED, TRANSMIT_FAILED
  or PARENT_FAILED.

The previous source, and a mutant without the key-exchange retry, both fail.
Image sleepy14 (`8e2c40ec…`) carries the fix.


The retry initially also covered a delivery that `nwk_aps` had *cancelled*:
`zdo_runtime` reported every non-zero confirm as TRANSMIT, so the e2e
RETRY_CANCEL case re-sent a deliberately cancelled Node_Desc_req. A
`NWK_APS_CANCELLED` confirm now reports `ZDO_RUNTIME_CANCELLED`, which BDB
never retries. `host-mac-link-e2e-tests` (1.5 s) now runs in
`test-m5-m6-host`, so CI covers it.

## 19. LH12: one late TX arm after READY

LH12 (sleepy14) reached READY and was interviewed. Later it stopped with
`ATTEMPT_LATE_ARM`: TX_ACTIVE appeared about 20 ms after the STXON strobe,
outside the attempt's arm window. A write-only `attempt_trace` (XDATA 0x15e8)
now records the strobe-to-TX_ACTIVE wait. In LH13 every attempt saw
TX_ACTIVE on the first read (max_wait 0). The LH12 delay is therefore a rare
anomaly whose cause is still unknown; the trace stays in place to capture it.

## 20. LH13: a transient recheck failure behind a foreign ACK head

LH13 (sleepy15) associated as 0x38ef and reached READY. About 90 s after RF
start it faulted with `RADIO_AUTOACK_FIFO_ERROR` in phase 4 (receive):
- 0 bytes read, 2 polls.
- RXFIFOCNT 7, PHR 5, FSMSTAT1 0xad (SFD high, FIFOP low).
- First byte at 0x50, ISR queue empty.

A read-only dump of the halted RXFIFO RAM (0x6000–0x607F) showed the FIFO
full of foreign 5-byte ACK frames. Filtering accepts every ACK regardless of
address. Their order from 0x50 matches the air capture. The head was a
complete foreign ACK (seq 105, +82.918 s). The seventh byte was the first byte
of the next frame: data 0x04d9→0x85fa at +82.938 s, which is not addressed to
us, so filtering rejects it and removes its bytes (SWRU191F p.225).

`finished_head()` correctly judged the head finished (count 7 > PHR+1), and
RXFIRST_PTR still pointed at it after the halt. `consume()`'s first recheck
failed anyway: either the count or the head pointer read wrong once, which
coincided with the rejection. The ISR path, which has no recheck, would have
read the head.

Phase 4 now allows one soft retry:
- **Conditions:** the first `consume()` fails with FIFO_ERROR, nothing has been
  read, there is no overflow, the state is RX/RX_NOACK, and the previous
  receive did not already retry.
- **Effect:** the receive ends EMPTY (`head_retry`), and the next receive reads
  the head. A repeated failure stays fatal.
- **Evidence:** `head_trace` (0x15fc) records the retry count and the failing
  count/RXFIRST_PTR/RXLAST_PTR/FSMSTAT1/re-read count, so the next occurrence
  identifies the comparison that failed.

`tests/test_mac_link_e2e.c` reproduces the LH13 FIFO: an ACK head, one
in-flight byte, SFD high and FIFOP low, with RXFIFOCNT reading 0 once. It
requires EMPTY with no RFD read, then the frame, then EMPTY; in case 0 it also
requires that a second consecutive failure latches FIFO_ERROR. Two mutants
fail it: one without the retry, and one with an unbounded retry. Image
sleepy16 (`19dc4d17…`) carries the fix and reaches READY in MCUEMU.
