# Interval MAC link driver (#13/#14)

`src/mac_link_driver.c` is an original BSD-3-Clause foreground driver. It
connects the complete BDB join (`bdb_join_*`) under the
[interval consumer profile](MAC_SCAN.md#interval-consumer-profile-cc2530_mac_link)
to the real [MAC radio adapter](MAC_ADAPTER.md) with
[OFF-only reconfiguration](MAC_ADAPTER.md#off-only-reconfiguration-and-reopen).
It requires `CC2530_MAC_LINK`, `CC2530_MAC_ADAPTER` and `CC2530_MAC_RECONFIG`.
No board image enables it, and no MCU image links it yet.

This is the third #13/#14 increment: a **host end-to-end join** through the
real adapter, radio services and host MMIO model, with a synthetic
coordinator/Trust Center. It is not a linked combined MCU image, an MCU
simulation of the driver, a hardware observation or MLME conformance (#45).

## API

| Call | Contract |
| --- | --- |
| `mac_link_driver_init` | Binds zeroed, disjoint caller storage to a started SCANNING BDB context, its single `mac_tx_interval_t` owner and an OFF, drained adapter. The initial radio state must be the truthful installed one: saved PAN/channel, RX off, short `FFFF` and the BDB IEEE address. Arguments are checked atomically. A bound object cannot be rebound. |
| `mac_link_driver_step` | One bounded foreground progress call. It returns `WAIT`, `RANDOM`, `FINISHED`, or a retained fault (`RADIO`, `CONSUMER`, `ORDER`, `COVERAGE`, `STATE`). `FINISHED` means BDB is terminal, not that RX is off or ownership was released. |
| `mac_link_driver_random` | Supplies one caller-qualified backoff byte for the pending draw, correlated by generation, retry and NB. The driver never generates randomness. |

Result values are `OK=0`, `WAIT=1`, `RANDOM=2`, `FINISHED=3`, `ARGUMENT=4`,
`STATE=5`, `RADIO=6`, `CONSUMER=7`, `ORDER=8`, `COVERAGE=9`. A fault is
retained. A fresh init is not recovery, and the bound objects must not be
recycled.

Every consumer `now` is the floor of an actual adapter observation. Original
interval sources are forwarded without retimestamping. RX deliveries are
processed in `rx_serial` order and consumed with their exact token only after
the consumer has used them. CRC is actual radio metadata. Received DATA frames
go to `bdb_join_receive` and its real crypto/NV owners.

## Preparation handshakes

`mac_adapter_prepare` requires the owner in `MAC_TX_DRAW` and
`mac_adapter_unprepare` requires `MAC_TX_DONE`. The exact-profile Association
Request and NWK/APS transport submit and then step in the same call, so under
`CC2530_MAC_LINK` both gain explicit one-shot handshakes:

| Constant | Value | Meaning |
| --- | ---: | --- |
| `MAC_JOIN_ACTION_ARM` | 7 | Owner is in DRAW after submit or a retry; prepare the adapter slot |
| `MAC_JOIN_ACTION_DISARM` | 8 | Owner is DONE; retire any prepared, unattempted slot before release |
| `MAC_JOIN_ARMED` | 9 | Echoes epoch/generation/token after real preparation |
| `MAC_JOIN_DISARMED` | 10 | Echoes the same identity after real unprepare or confirmed OFF/RX |
| `NWK_APS_ACTION_ARM` | 5 | Transport control action (not a `mac_adapter_accept` action) |
| `NWK_APS_ACTION_DISARM` | 6 | Transport control action |

The transport side confirms with `bdb_join_armed`/`bdb_join_disarmed`
(generation, retry, NB). No ordinary owner step runs before ARMED. Cancellation
or lifetime expiry while the handshake is pending draws no random byte and
starts no TX. Wrong-token, wrong-generation or duplicate confirmations return
`STATE` and change nothing. BDB action/event identifiers are unchanged.

A cancel or stop can legitimately arrive after the transport emitted ARM and
before the driver confirms it. Examples are a ZDO query timeout in the same BDB
step, or a Leave or key Update drained during the driver's own preparation.
`nwk_aps_armed` therefore accepts an identity-matched outstanding ARM despite
a pending cancel or stop. The next step injects CANCEL, reaches DONE and emits
DISARM, and the driver actually unprepares the unattempted slot. After a retry,
the transport does not re-ARM an ordinary transmission that is stopping or
cancelled. A priority APS ACK still completes. `mac_join` already ignores
ARMED while stopping and cancels through DISARM.

## Composition policy

| Transmission | Adapter policy | Receive coverage |
| --- | --- | --- |
| Beacon Request | `KEEP_RAW` | Software beacon-only dispatch in the retained raw RX; OPENED is a later live observation of that episode, not a captured edge |
| Association Request | `STOP_RX` | None; the Response is extracted later by POLL |
| Data Request (POLL) | `KEEP_AUTOACK` | From TX end through the guarded first-ACK handoff; handoff failure is a terminal local error |
| Runtime unicast | `KEEP_AUTOACK` | As above |
| Runtime non-ACK frame | `STOP_RX`, then normal reopen | Explicit gap |

Under the link profile, POLL/Association PREPARED confirms installed
PAN/channel/local address with the adapter OFF and drained. It does not assert
RX coverage before the transmission. OFF, configure and prepare intervals
never supply coverage. CLOSED carries the adapter's latest actual pre-stop
watermark. It is refreshed from every adapter delivery, including TX-only
RX_CLOSED and RETIRED, so it never comes from an earlier RX episode and never
claims time after the stop. `gaps` counts driver-initiated RX closures. An INSTALL event echoes
the original epoch/token only after the new configuration is really open.

After a received ACK-requesting frame, the driver delays the next preparation
by a conservative `12 + 22 + 40 = 74`-symbol turnaround, ACK and IFS guard
(IEEE 802.15.4-2006 §§7.5.6.4.2, 7.5.1.3). The guard is not evidence that an
ACK was sent. There is no secondary RX queue. If `bdb_join_receive` returns
`FULL`, for example while a priority APS ACK is blocked, the driver records a
retained `CONSUMER` fault. The original adapter head and the unexecuted grant
are kept, and no frame is silently dropped.

Three consumer checks changed, all only under `CC2530_MAC_LINK`:
- **POLL ACK acceptance.** Report order is now checked separately from the
  physical bounds. The ACK's lower bound may legitimately precede the last
  foreground report, but it must not precede the retained TX lower bound.
- **Scan CLOSED stamp.** The stamp is the saved pre-stop watermark, which may
  be earlier than later drain-report clocks.
- **POLL TX-result closure.** A NO_ACK or CHANNEL_ACCESS decision rests on the
  MAC's own closed-ACK-window or CCA evidence. The adapter is already OFF and
  drained before the later report. Its CLOSED watermark therefore need not
  reach that report stamp. The CLOSED delivery ordering, timeout coverage
  through `receive_end` and frame/PENDING_ZERO coverage checks are unchanged.

## Evidence

`make BOARD=<board> test-mac-link` builds `tests/test_mac_link_e2e.c` with
`tests/mac_link_peer.c` against the complete ED join, crypto/NV models, the real
adapter and radio services, and the host MMIO radio model. It runs the result
natively and under nonrecovering ASan/UBSan. All four runs (boards 0/1) give
**15 cases, 48262 checks plus 943 peer checks, 8997 driver steps and 96
explicit test RANDOM inputs**:

| Case | Verified outcome |
| --- | --- |
| Authenticated join | READY with verified TC key, protected application exchange and a real periodic parent keepalive |
| No beacon | `NO_PARENT` |
| Association refusal | `ASSOCIATION_FAILED`, status 1, after three real attempts |
| Missing Transport Key | `KEY_TIMEOUT` |
| Late Response (ambiguous interval) | `MAC_JOIN_TIMING_UNCERTAIN`; no accepted Response or protocol success |
| RF error during closure | Retained `RADIO` fault in driver and adapter |
| Withheld RANDOM | Real deadline expiry, actual unprepare, zero transmissions |
| RX admission FULL | Retained `CONSUMER` fault, original frame and grant preserved |
| First POLL without ACK | Four unacknowledged Data Requests, ordinary NO_ACK, association retry, READY |
| First POLL CCA busy | Five busy CCAs, ordinary CHANNEL_ACCESS, association retry, READY |
| Cancel after transport ARM | Unattempted slot unprepared through DISARM; transaction CANCELLED; normal FAILED/TRANSMIT_FAILED |
| Stop after transport ARM | As above through the stop path |
| Cancel with an active priority APS ACK | The ACK completes; the cancelled announce ends in normal FAILED |
| Cancel at retry retirement | No re-ARM after RETIRED to DRAW; normal FAILED/TC_FAILED |
| Real ZDO timeout after ARM emission | Retry, then READY |

The NO_ACK and CCA cases also check that the delivered CLOSED watermark equals
the adapter's latest floored watermark, not the scan window's. The direct
cancel/stop cases are labelled as mimicking the existing callers; the last case
uses the real ZDO timeout caller. Cases 0–7 kept their previous outcomes and
counters.

The test PRNG is deterministic and explicitly not a production entropy
source. Twelve driver-path mutations are killed. Eight are killed by the
authenticated-join case:
- consume token + 1;
- retimestamp the source to `now + 1`;
- force CRC valid;
- invent RANDOM 0;
- echo INSTALL token + 1;
- reuse OFF before the closure goal clears;
- discard an outstanding FRAME grant;
- advance the RX serial incorrectly.

Four more revert the review fixes: the POLL TX-result closure exemption and the
watermark refresh (both killed by the NO_ACK case), ARMED acceptance despite
cancel/stop (cancel case) and retry re-ARM suppression (retry-retirement case).

The link corpora now cover ARM/ARMED, retry re-arming, DISARM, cancellation
and lifetime while waiting, wrong identities and FAILURE. POLL/join has
146 cases and 8185 checks, including NO_ACK closure and rejected future
watermark, frame and PENDING_ZERO coverage. The scan corpus adds the delayed CLOSED
watermark.

`test-mac-link` also compiles the driver composition with SDCC 4.2.0
`--Werror`, banked-join flags and the full link+adapter+reconfiguration
defines. Sizes are identical on both boards:

| Module | CODE | XSEG | DATA |
| --- | ---: | ---: | ---: |
| `mac_link_driver` (`BJ_BANK4`) | 16473 | 90 | 34 (+8 OSEG, 3 BSEG bits) |
| `mac_scan` | 8528 (+8 CONST) | 79 | 4 |
| `mac_poll` | 8057 | 319 | 11 |
| `mac_join` | 7784 | 270 | 7 |
| `nwk_aps` | 17476 | 275 | 22 |
| `bdb_join` | 14584 | 186 | 24 |

Measured SDCC context sizes: `mac_link_driver_t` 304 bytes,
`mac_tx_interval_t` 180, `bdb_join_t` 1696 (exact 1676), `mac_join_t` 655
(645) and `nwk_aps_t` 708 (703). These are object measurements, not combined
placement, DATA/stack-fit or alias evidence.

Without the flag, all touched modules compile to identical instructions for
boards 0/1, standalone and banked. All 25 affected exact images
(`mac_scan_test`, `mac_poll_test`, 22 `mac_join_<n>_test` and
`banked_join`) are byte-identical on both boards. So are their memory
reports, parsed maps (excluding line symbols) and listing bytes. Only
source-line records moved or were added, from split exact-profile conditions
in `mac_join.c` and `mac_poll.c`. The POLL F/S/L/T digest, 22 join manifests, join
`WORKSPACE_PINS` (rel/asm/lst) and banked raw-CDB/listing/object pins were
refreshed. The same calculation reproduces every previous pin from the
previous revision.

Evidence level: **host-tested and SDCC compile-checked**. There is no linked
driver image, MCU replay, hardware observation, production entropy,
physical timing or NV measurement, secure restart/rejoin or conformance
result. No hardware was accessed.

## Remaining #13/#14 work

The next increment is one combined banked MCU image containing the adapter,
driver and link consumers, within 7680 ordinary XDATA. It needs DATA/stack/ABI
and alias proofs. A rough object estimate exceeds the budget by about
1.3–1.5 KB, so a measured RAM reduction is required first. After that come the
#45 decision, documentation and closure at the documented offline evidence
level.
