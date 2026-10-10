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

## 10. Proposed minimal diagnostic ABI (design only, not implemented)

This is one shared source for SDCC and LLVM, behind a build flag (`CC2530_RX_DIAG`) so that
the admitted images are unchanged.
- It uses a fixed 48-byte XDATA block, zeroed at reset, and read by the operator from the halt
  dump.
- It has no printf and no payload bytes, and stores no addresses, keys or IEEEs.

```c
typedef struct {                 /* 48 bytes, little-endian, version 1 */
    uint8_t  version;            /* 1 */
    uint8_t  overflows;          /* saturating: overflow latch 0->1 */
    uint8_t  salvaged;           /* saturating: heads delivered while latched */
    uint8_t  flushes;            /* saturating: ISFLUSHRX from the overflow branch */
    uint16_t max_service_gap;    /* saturating ticks between FIFO services with RX on */
    uint16_t max_rx_gap;         /* saturating ticks, RX stop/OFF -> resume READY */
    /* first-loss sticky snapshot (written once, first_valid 0->1) */
    uint8_t  first_valid, site;  /* site: 1 observe(), 2 consume() */
    uint8_t  radio_state, radio_phase;
    uint8_t  count, first, last, phr, signals, rferrf;
    uint32_t stamp;              /* 24-bit timebase at detection */
    uint16_t gap_at_loss;        /* ticks since previous FIFO service */
    uint16_t frames_at_loss;     /* adapter frame serial, low 16 bits */
    uint8_t  last_fcf0, last_fcf1, last_dsn, last_len;   /* last good frame header */
    uint8_t  bdb_phase, workspace, adapter_phase, adapter_goal; /* filled by link driver */
    /* first-failure sticky uplink snapshot (signature B) */
    uint8_t  fail_valid, fail_bdb_phase, fail_result, fail_mac_outcome;
    uint8_t  fail_retries, fail_cca_busy;
    uint8_t  reserved[6];
} cc2530_rx_diag_t;
```

Rules:
- The radio layer writes only counters and the radio-level snapshot.
- `mac_link_driver` writes the upper context once, at the first lossy observation.
- `bdb_join` `fail()` writes the uplink snapshot once.
- No control decision may read the block.

Offline tests required before any hardware use:
1. Host `test_radio_autoack` loss variants: inject overflow at observe/consume, in RX, RX_NOACK
   and DRAIN, and assert the counters and the sticky snapshot (first wins, saturation, no
   payload).
2. Host `test_mac_adapter` / link join: assert the context capture once per first loss, and
   that the flag-off build is bit-identical to the current admitted image.
3. `bdb_join` host test: uplink-failure snapshot for each `TRANSMIT_FAILED` site.
4. Layout proofs: the block lies outside the radio-private range (`PRIVATE_END`) and the
   whole-program stack stays ≤128.
5. LLVM FAST + SMOKE, and a matching SDCC build from the same source.
6. **FULL** on the diagnostic LLVM image before it becomes a hardware candidate.

## 11. Focused test results

See §8.

## 12. Is a code change justified?

**No functional change.** No source defect is proven. Changing the `COVERAGE` policy or
`RxOnWhenIdle` would be a protocol decision without demonstrated cause. A **diagnostic-only**
build (§10) is justified as the next step.

## 13. Is another hardware trial justified?

**Not with the current images.** Repeating them adds pass/fail counts but no first-cause
evidence. A trial is justified only with the diagnostic image after it has passed §10
tests 1–6, and it needs separate operator authorization.

## 14. Next bounded experiment

1. Implement §10 behind `CC2530_RX_DIAG` and run its offline tests (FAST, SMOKE, then FULL for
   the LLVM diagnostic image). Build the SDCC counterpart from the same source.
2. After authorization, run on H2 only (no H1): one bounded interleaved pair, SDCC then LLVM.
   Use the existing single RF resume, the coordinator-only permit and the join+key gate, with
   no added retries.
3. Optionally, add an independent 802.15.4 sniffer on channel 15 for per-frame
   transport-key/ACK truth.
4. Decide from the first-loss snapshot:
   - a large `gap_at_loss` / `max_service_gap` points to (a);
   - several parent-addressed retransmissions in the FIFO point to (b);
   - foreign frames in an `RX_NOACK` interval point to (c).
