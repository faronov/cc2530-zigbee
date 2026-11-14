# M7 low-power end device plan

Staged; each stage needs its own hardware evidence. Status words: **host-tested**
(host/E2E models only), **planned**, **unverified** (taken from documentation
or emulator behaviour, not observed on a board).

The device already associates as an RX-off-when-idle child (MAC capability
bit 3 clear) and polls its parent with MAC Data Requests (`bdb_join.c`
`poll()`): 250 ms while commissioning, recovering, waiting for a reply, for
180 s after READY entry and 10 s after a unicast receipt; otherwise the slow
READY period. Outside the `autonomous-pm2` variant the CPU never sleeps; the
stack requires IEN0/1/2 == 0 outside the guarded RF-vector and PM2 windows
(`mac_time.h`).

## M7a — receiver off when idle (hardware-observed on H2)

`CC2530_RX_OFF_IDLE`, enabled only for the `autonomous` firmware variant
(`tools/llvm/build.sh`, `make firmware LLVM_VARIANT=autonomous`). Default and
parent-recovery images are byte-identical to the pre-M7a build.

- Scope: BDB READY only, capability bit 3 clear. WAIT_KEY..VERIFYING and
  RECOVERING keep RX on: their indirect frames already need polls, but turning
  RX off there gains little and risks the join/rejoin timing.
- In READY the receiver is on only for own TX (prepare/CCA/TX/ACK wait and the
  74-symbol AUTOACK guard) and for a Data Request response window
  (macMaxFrameTotalWaitTime, `extraction.frame_wait` = 1986 symbols) after an
  ACK with Frame Pending or a lost ACK (implicit ACK). Details and evidence:
  [MAC_LINK_DRIVER](MAC_LINK_DRIVER.md#ready-receiver-lifecycle-m7a-cc2530_rx_off_idle).
- Closes use the normal adapter close/drain, so frames already in the FIFO
  are delivered; a frame still on air at the stop is lost.
- Slow READY poll: 7.0 s (was 4 s; the define-less images keep 4 s). The
  parent's macTransactionPersistenceTime is about 7.68 s (0x01F4 × 960
  symbols); `poll_at` counts from poll queueing, so 0.68 s remains for CSMA,
  macMaxFrameRetries and foreground/NV latency. 7.5 s would leave <0.2 s.
  Keepalive (30 s ED timeout request, 2-minute requested timeout, 5 s × 6
  retries) is unchanged.
- ACTIVE window (rxoff builds only): a received unicast still triggers an
  immediate poll (`poll_at = now`, so Frame Pending drains a queued tail), but
  only NWK data frames that deliver an APS payload open the 10 s 250 ms
  window. A NWK command such as the keepalive's ED Timeout Response ends its
  exchange; before this change each keepalive cost about 40 fast polls per
  30 s. An APS ACK for an own report also ends its exchange and no longer
  opens the window.
- Diagnostics: `ready_time`, `rx_on`, `rx_opens` in `mac_link_driver_t`
  (rxoff builds only). Host results: E2E `RX_OFF_IDLE` 0.77% RX-on over a
  modeled minute of slow polling with 84 polls (0.19% and 13 polls after
  the ACTIVE change; 16 polls and 0.236% with an acknowledged application
  send inside the measured minute; the E2E checks both bounds); join-smoke sleepy cases 25-27
  (`JOIN_SMOKE_RX_OFF_IDLE=1`) about 3.7% over 3 modeled hours, because that
  peer model sets Frame Pending in every Data Request ACK and each keepalive
  response restarts 10 s of 250 ms polling.
- Hardware-observed (H2 against EmberZNet, LH22, before the ACTIVE change):
  join, Leave + 180 s hold, rejoin, 5/5 ZHA reads; 5.5-7.4% RX-on in idle
  60 s windows (100% before M7a), dominated by keepalive-triggered fast polls.
- Hardware-observed (H2, LH23, NWK-data-only ACTIVE window): join, Leave +
  180 s hold, rejoin, 5/5 ZHA reads (0.42-0.75 s); idle 60 s windows 0.82,
  0.97, 1.12, 3.76 and 1.21% RX-on, no extra receiver opens. The sniffer
  shows keepalives no longer start fast polling; the 3.76% window held an
  own ZCL report whose APS ACK (a NWK data frame) opened the window, which
  the later APS ACK exclusion addresses (host-tested, not yet on hardware).
- Per-poll receiver time (simulated, MCUEMU, FSMSTAT0 sampled every 50 µs,
  LH22 image): about 22.75 ms per poll without Frame Pending. TX and ACK end
  2.6 ms after RX start; the rest is the adapter retire/close sequence, about
  3 ms per main-loop iteration. At the 7 s slow period that is about 0.33%
  RX-on, small next to the always-awake CPU, so shortening this tail is
  deferred until M7b.

## M7b — PM2 between polls (H2 HIL, debugger-powered)

`CC2530_PM2_IDLE`, enabled only for the `autonomous-pm2` variant
(`make firmware LLVM_VARIANT=autonomous-pm2`, which also defines the M7a
`CC2530_RX_OFF_IDLE`). Default, autonomous and parent-recovery images are
byte-identical to the pre-M7b build. `join_smoke_sleep()` runs after each
`join_smoke_poll()`:

- Gates (counted in `join_smoke_sleep_status.skips`, last gate in `reason`):
  1 READY with RX off idle and no BDB/transport/ZDO work; 2 MAC driver, TX
  engine and adapter idle (adapter OFF); 3 radio and SoC quiescent (RXENABLE
  0, FSMSTAT0/1 idle, IEN0/1/2 except RFIE zero, SLEEPCMD.MODE 0, no DMA);
  4 deadline: the next poll (`poll_at`) or, unless the parent selected MAC
  data poll keepalive, the ED Timeout keepalive. The span is capped at
  1 875 000 symbols (30 s), converted to 32.768 kHz ticks rounded down and
  reduced by a 33-tick (~1 ms) wake margin; spans under 197 ticks stay awake.
  5 (`CC2530_EPD` only) display: no sleep while a refresh streams or waits
  on a short panel step; during the panel's own refresh the span is limited
  to the expected rest of 5 s, then to 250 ms steps
  ([e-paper display](ED_JOIN.md#optional-e-paper-display)).
- Sleep: `mac_time_pause()` (stops Timer2 through T2CTRL.SYNC and the
  32 kHz edge, `mac_time.h`), Sleep Timer compare written ST2, ST1, ST0 after
  STLOAD.LDRDY and re-checked at least 164 ticks ahead (`late` otherwise),
  IEN2 saved and cleared, SLEEPCMD = 0x06, then common-code `enter_pm2()`:
  `CLR STIF; MOV IEN0,#0xA0; MOV PCON,#1; CLR EA`. The IEN0 write delays any
  interrupt by one instruction, so no ISR runs before PCON. `.p2align 2`
  places PCON at 4k+3, so its successor is not on a 4-byte boundary
  (SWRU191F 4.2). The build does not check this alignment; recheck the
  disassembly after toolchain changes.
- Wake: vector 5 `join_smoke_sleep_isr` clears STIF; IEN0 = 0, SLEEPCMD back
  to 0x04, IEN2 restored; `stray` counts wakes with IRCON.STIF clear. Wait
  for CLKCONSTA == CLKCONCMD (32 MHz XOSC) and a SLEEPSTA.CLK32K rising
  edge, then re-latch the radio idle state (below), `mac_time_resume()`
  (Timer2 restart by SYNC on that edge) and `aes_power_restored()`. Any failure sets `failure` and faults the image
  with `JS_SLEEP`.
- Retention: SWRU191F 4.6 lists only AES, I2C, USB, OBSSEL and TR0 as not
  retained in PM2. `aes_power_restored()` re-applies the key-load mode when
  ENCCS reads the reset value 0x08 and rejects any other state.
- RFIDLE re-latch: on hardware (H2, LH24) RFIRQF1.RFIDLE reads 0 after PM2
  although the RF configuration is retained; the radio owner requires the
  flag while OFF and faulted the first TX after the first wake
  (`STATE_CHANGED`). ISRFOFF alone in idle gives STROBEERR, so each wake
  issues ISRXON then ISRFOFF back to back (RFOFF during calibration, no RX),
  waits for RFIDLE and FSMSTAT0 idle, and fails with `reason` 10 on any
  RFERRF, RFIRQF1 bit 3-7, FSMSTAT1 RX/TX/lock/SFD, RXFIFOCNT or new RFIRQF0
  bit other than RXMASKZERO. `lost` counts wakes that found RFIDLE clear.
  MCUEMU retains the flag (`lost` 0) but exercises the same sequence.
- Stack proof: the ISRs (vector 16 RX, vector 5 Sleep Timer) share the reset
  priority level; `stack_analysis.py` rejects any IP0/IP1 write, so they
  never nest and the maximum ISR peak bounds them. Demand 127/128 bytes.
- Reports may be delayed by up to one sleep (about 7 s in slow READY); the
  device has no wake source except the Sleep Timer.
- Simulated (MCUEMU, Telink sampleGW parent, 45 s): join to READY, then 136
  PM2 sleeps in 33 s of the 250 ms post-READY poll phase, about 82% of the
  time asleep, polls every 250.1 ms, no mac_time/AES fault, no stray or late
  wake. MCUEMU freezes Timer2 in PM1-3, so Timer2 continuity, the
  7 s slow-poll phase and current are not validated there. With the RFIDLE
  re-latch: 116 sleeps in 25 s of READY, no fault.
- LH24 (H2, ZHA/ZBT-1, debugger attached with SOFT_POWER_MODE): FAIL, no
  join: one sleep, then the radio owner fault above 0.6 s after READY. The
  debugger keeps the regulator on, so full PM2 retention loss is not
  exercised by this setup.
- LH25 (re-latch image): no firmware fault, 802 sleeps in 637 s, `lost` 34
  (RFIDLE is lost only on some wakes), reports and ED Timeout exchanged with
  the coordinator, but ZHA never completed the initial interview (not
  captured; not reproduced). LH25C (same image, sniffer and zigpy debug
  logging): PASS — interviewed 63 s after start, leave/hold/rejoin, five
  steady reads (latency up to 7.4 s, one slow poll), five 60 s idle windows
  52-64 s asleep each; 865 sleeps, `lost` 97, no failure or late compare.
  `stray` 5 are debugger HALT wakes. Current is not measured (M7c).

## M7c — measurements (planned)

Pending a current meter: average/peak current per state, poll energy, 12 h and
72 h soak with an EmberZNet parent (no missed indirect frames, no rejoin), and
100 reset/restart cycles.
