# M7 low-power end device plan

Staged; each stage needs its own hardware evidence. Status words: **host-tested**
(host/E2E models only), **planned**, **unverified** (taken from documentation
or emulator behaviour, not observed on a board).

The device already associates as an RX-off-when-idle child (MAC capability
bit 3 clear) and polls its parent with MAC Data Requests (`bdb_join.c`
`poll()`): 250 ms while commissioning, recovering, waiting for a reply, for
180 s after READY entry and 10 s after a unicast receipt; otherwise the slow
READY period. The CPU never sleeps and the stack requires IEN0/1/2 == 0
outside the guarded RF-vector window (`mac_time.h`).

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
  only NWK data frames open the 10 s 250 ms window. A NWK command such as the
  keepalive's ED Timeout Response ends its exchange; before this change each
  keepalive cost about 40 fast polls per 30 s.
- Diagnostics: `ready_time`, `rx_on`, `rx_opens` in `mac_link_driver_t`
  (rxoff builds only). Host results: E2E `RX_OFF_IDLE` 0.77% RX-on over a
  modeled minute of slow polling with 84 polls (0.19% and 13 polls after
  the ACTIVE change; the E2E checks the bound); join-smoke sleepy cases 25-27
  (`JOIN_SMOKE_RX_OFF_IDLE=1`) about 3.7% over 3 modeled hours, because that
  peer model sets Frame Pending in every Data Request ACK and each keepalive
  response restarts 10 s of 250 ms polling.
- Hardware-observed (H2 against EmberZNet, LH22, before the ACTIVE change):
  join, Leave + 180 s hold, rejoin, 5/5 ZHA reads; 5.5-7.4% RX-on in idle
  60 s windows (100% before M7a), dominated by keepalive-triggered fast polls.
- Per-poll receiver time (simulated, MCUEMU, FSMSTAT0 sampled every 50 µs,
  LH22 image): about 22.75 ms per poll without Frame Pending. TX and ACK end
  2.6 ms after RX start; the rest is the adapter retire/close sequence, about
  3 ms per main-loop iteration. At the 7 s slow period that is about 0.33%
  RX-on, small next to the always-awake CPU, so shortening this tail is
  deferred until M7b.

## M7b — PM2 between polls (planned, all items unverified)

Sleep with the Sleep Timer as the wake source between READY polls. Notes for
the implementation, from SWRU191F and the emulator:

- PCON write: the instruction after `MOV PCON,#1` must not start on a 4-byte
  boundary when the flash cache is enabled (SWRU191F §4.2); align explicitly.
- Wake needs an interrupt: IEN0.STIE with IRCON.STIF, EA set only for the
  sleep window and `CLR EA` immediately after `MOV PCON,#1` (TI example). This
  is a narrow, documented exception to the IEN == 0 invariant and must keep
  the existing guards meaningful.
- Sleep Timer compare at least 5 ticks ahead; write ST2, ST1, then ST0, after
  STLOAD.LDRDY is set.
- After wake: wait for a SLEEPSTA.CLK32K edge before reading ST, and for
  CLKCONSTA.OSC == 0 (32 MHz XOSC) before using the radio.
- AES registers are not retained in PM2 (§4.6): `aes.c` state must be
  re-initialised after every wake.
- Timer2/`mac_time` has no re-initialisation path; PM2 needs a pause and SYNC
  resume contract for the MAC epoch, verified on hardware.
- MCUEMU requires EA = 1 to wake and freezes Timer2/WDT in PM1-3, so emulator
  runs cannot validate Timer2 continuity.

## M7c — measurements (planned)

Pending a current meter: average/peak current per state, poll energy, 12 h and
72 h soak with an EmberZNet parent (no missed indirect frames, no rejoin), and
100 reset/restart cycles.
