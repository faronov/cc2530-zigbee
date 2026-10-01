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

Receive loss (`lossy`) is a `COVERAGE` fault during scan-less association
work only. Once RUNTIME owns the workspace, a normal AUTOACK RX FIFO overflow
is an accepted loss: completed frames ahead of the overflow are salvaged, the
halted partial frame never passed its FCS and so was never acknowledged
(SWRU191F §23.10.2). Upper-layer timeouts and retries handle the loss. The
adapter observation samples FSMSTAT1 before RFERRF, so an overflow can latch
between the two reads; an RXOVERF-only RFERRF without the FIFO=0/FIFOP=1
overflow signature is resampled once, and a persistent mismatch stays a
`CONTROLLER_ERROR` (LG READY observed FSMSTAT1=ED with RXOVERF, then 5C). In raw
(non-AUTOACK) RX during association, a command or data frame after the
Beacon/Association exchange, such as a neighbour's network frame following a
busy-CCA Data Request, is never acknowledged. It is dropped, not delivered,
and does not fault `COVERAGE`. That fault was hardware-observed on LG; the
drop is host-tested (E2E `POLL_BUSY`).

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
| No beacon | `NO_PARENT` after three complete loss-free scans (ambiguous, closed, overflowed or unscanned results fail on the first scan) |
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
result. No hardware was accessed. Code `6f648f5` passed
[full Actions 36237016492](https://github.com/faronov/cc2530-zigbee/actions/runs/36237016492),
**110/110 jobs**.

## SDCC resource comparison

The 2026-09-26 offline experiment compares the unchanged sources at
`0ca84cdf8f074b8c8f699d509580b612f98241b1` using SDCC 4.2.0 #13081 and
the official stable **4.6.0 #16555** (released 2026-06-22). The latter runs
from an isolated directory, not a replacement system installation.
The [upstream Linux amd64 archive](https://sourceforge.net/projects/sdcc/files/sdcc-linux-amd64/4.6.0/sdcc-4.6.0-amd64-unknown-linux2.5.tar.bz2/download)
has SHA-256
`f6b929c62ed3082a26087885e0f1f9bf41878602ef1f57e40b11b4a01bf4f366`;
the published SHA-1 and MD5 were also checked. No proprietary toolchain was
available or evaluated. Compiler/runtime licenses remain upstream's.

The experiment compiles all **38 unique production modules** in
`MAC_LINK_E2E_SRC`, not just the eleven changed-module targets. It uses
`-mmcs51 --model-large --std-c99 --debug --opt-code-size --Werror`, the
existing banked-join/link/adapter/reconfiguration defines and per-module
DATA/CODE area names. These separate-profile area names are not a proposed
combined placement map. Host fixtures, banker and compiler runtime are
excluded from production object sums. Both board definitions agree.

Complete SDCC 4.2.0 object measurements, in bytes except BSEG:

| Profile | Modules | CODE | CONST | XSEG | DATA sum | OSEG sum | BSEG bits |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Exact join production | 30 | 141048 | 16 | 5320 | 327 | 52 | 47 |
| LINK production | 38 | 204166 | 55 | 7264 | 497 | 77 | 66 |
| LINK with `--nogcse` | 38 | 210318 | 55 | 7575 | 327 | 63 | 66 |

XISEG is zero. DATA/OSEG sums are **not concurrent IRAM usage or stack
measurements**. Disabling global common-subexpression elimination trades
170 summed DATA bytes for **311 more XDATA bytes**, not a solution to the
XDATA deficit. The shipped SDCC User Guide section 3.3.4 describes this
spill tradeoff and excludes mcs51 from `--max-allocs-per-node` effects.

There is **no complete 4.6.0 total or accepted new-compiler image**:

- The unchanged banked build stops at the reviewed-version gate in
  `include/banked.h`. This expected rejection is not itself a compiler bug.
- Compiling identical, unedited 4.2-preprocessed input with both compilers
  provides an object-only diagnostic, not a supported 4.6 banked build.
  It exposes banked declaration/definition conflicts and an internal
  compiler error in `ed_wire`. The 4.2 direct/preprocessed control matches.
- A separate direct, nonbanked LINK reference still hits compiler internal
  errors in `ed_wire`, `bdb_join`, `bdb_join_init` and `mac_link_driver`.
  No production source, version macro or acceptance guard was changed.

Only like-for-like successful subsets can be compared:

| Diagnostic | Paired modules | 4.6 minus 4.2 CODE | XDATA |
| --- | ---: | ---: | ---: |
| Identical preprocessed banked LINK input | 27 | -1378 | **0** |
| Direct nonbanked LINK reference | 34 | -2962 | **0** |

Actual SDCC layouts are unchanged across releases and boards: LINK
`bdb_join_t` is 1696 bytes (exact 1676), `mac_tx_interval_t` 180 and
`mac_link_driver_t` 304. Embedded event/action/configuration fields are not
counted again. The current unmodified composition therefore needs at least
**7264 + 1696 + 180 + 304 = 9444 ordinary XDATA bytes**, exceeding 7680 by
**1764 bytes before banker/libc/additional caller storage**. This supersedes
the earlier rough 1.3–1.5 KB shortfall estimate.

A small original control uses two disjoint leaf calls with 64-byte volatile
automatic arrays. Both compilers allocate 131 XSEG bytes, just as with two
persistent arrays; `--nooverlay` does not change that. CODE decreases
128 to 122 bytes without freeing RAM. The small-model control does overlay
the automatic arrays into 64 OSEG bytes, versus 128 DATA for persistent arrays
or `--nooverlay`. That is evidence about a different memory space, not a
proposed production-model switch or an exhaustive scalar-spill test.
Upstream's [planned non-stack spill allocation work](https://sourceforge.net/p/sdcc/wiki/NGI0-Commons-SDCC/#a1-efficient-allocation-of-spilt-local-variables-into-non-stack-memory)
is relevant, but these measurements do not establish that it shipped.

The unchanged **4.2 baseline** passes both-board strict image/ABI checks:
143242 populated CODE and7512 ordinary XDATA. A fresh, selected
`network-key-late` simulation executes 58 calls/228 peripheral events per
board and reaches SP7B under 7C. That is not a new full-suite peak.
**4.6 stack use is unmeasured**, and no combined LINK image was linked or
simulated. No hardware was accessed.

Commands, hashes, per-module CSV/JSON, diagnostics and the original control
are retained in the local `sdcc-resource-comparison` experiment artifacts
(`reproduce.txt` is the entry point), outside Git and CI uploads.
The measured recommendation is to retain 4.2.0 for the RAM-lifetime work:
neither the tested upgrade nor `--nogcse` closes the deficit. Improving SDCC
itself remains a separate compiler-development task, not an observed saving.

## Experimental returning-work profile

`CC2530_MAC_LINK_RAM` is an explicit, nondefault composition profile. It
requires `CC2530_MAC_LINK` and, on SDCC, the banked join/workspace profile.
All translation units and callers must agree: its BDB layout is incompatible
with the ordinary LINK and exact layouts. Do not supply the old
`mac_join_staged` linker binding in this profile.

The first reduction changes only the join/POLL returning work:

- Join uses a typed view of the actual caller context after admission instead
  of a full shadow context.
- POLL uses the caller's actual control. Its candidate Data Request is encoded
  in the existing returning input/output union before the caller is changed.
- Rejected admission preserves caller bytes. An admitted call's failure is
  committed state, not rolled back or wiped.
- Join, nested POLL, TX and Association work remain separate. An occupied or
  faulted context is never scratch for another call.
- Returning work is wiped and borrowed views dropped on public return.
  Span/overlap checks reject incompatible caller aliases and target storage
  outside ordinary XDATA. The existing serialized foreground, no-callback,
  no-ISR and nonreentrant restrictions remain.

Removing the 655-byte association shadow saves only 263 bytes in the BDB
object: the unchanged 1047-byte runtime member becomes the phase union's
largest member. The selected parent, outcome, security state, occupied
queues and adapter frame/grant ownership are not removed.

SDCC 4.2.0 measurements are identical on both boards:

| Allocation | Ordinary LINK | Compact LINK | Delta |
| --- | ---: | ---: | ---: |
| 38 production modules: CODE | 204166 | 211469 | +7303 |
| CONST | 55 | 55 | 0 |
| XSEG | 7264 | 7163 | -101 |
| DATA, raw sum | 497 | 519 | +22 |
| OSEG, raw sum | 77 | 77 | 0 |
| BSEG, bits | 66 | 70 | +4 |
| `mac_poll`: CODE / XSEG / DATA | 8057 /319 /11 | 11341 /203 /18 | +3284 /-116 /+7 |
| `mac_join`: CODE / XSEG / DATA | 7784 /270 /7 | 11803 /285 /22 | +4019 /+15 /+15 |
| `bdb_join_t` | 1696 | 1433 | -263 |
| Separate MAC / driver contexts | 180 /304 | 180 /304 | 0 |
| Production + top-context XDATA floor | 9444 | **9080** | **-364** |

This saves RAM at a significant CODE and DATA cost. It **still exceeds 7680
by 1400 bytes**, before banker/libc/additional caller storage. DATA/OSEG sums
do not prove simultaneous IRAM use or stack fit. The inherited standalone
area assignments are also not a combined bank map: compact CSEG is 54168,
BJ_BANK2 43733 and BJ_BANK4 48424 bytes, each beyond a 32 KiB CODE window.

`make BOARD=<board> test-mac-link-ram` runs the compact E2E/cleanup test and
the unchanged 146-case POLL/join corpus natively and with nonrecovering
ASan/UBSan. The cleanup test includes all 15 original E2E scenarios and adds
18172 checks/9030 public-return cleanup observations, including rejected
admission, overlap, nested returning work and retained faults. Four isolated
mutations are rejected: omitted join wipe, omitted POLL wipe, premature POLL
caller write and omitted join alias rejection.

The target also compiles all 38 unique production objects and the separate
`mac_link_ram_layout` sizeof probe. `tools/link_ram_resources.py` checks the
complete object set, allocation classes and experimental resource ceilings.
It counts the probe's three caller objects once, outside production totals,
and reports the floor explicitly as **object-only**, not MCU-fit acceptance.
The probe is never linked into firmware. Two new CI workers retain the
ordinary LINK workers and every existing target case.

The exact and noncompact LINK profiles retain their emitted instructions and
allocation areas. All 25 affected exact images per board, memory reports,
parsed maps and non-line CDB records match the previous revision. The 22 join
manifests, POLL metadata (still22457 records), join workspace rel/asm/lst and
banked raw-CDB/listing/object pins were refreshed only for source-line changes;
the calculation first reproduced every old pin from the unchanged baseline.
The compact profile is **host-tested and object-checked only**: no compact
linked image, physical DATA layout, stack peak, MCU replay or hardware
observation is established.

The initial364-byte reduction at `00e4086` passed
[full Actions36253122802](https://github.com/faronov/cc2530-zigbee/actions/runs/36253122802),
**112/112 jobs**, including both new compact workers and every existing
worker. This accepts the host/object experiment, not combined MCU placement.

The subsequent [compact attempt projection](MAC_ATTEMPT.md#compact-projection-storage)
removes another128-byte private frame duplicate without dropping the raw
record, public receipt or retained handoff metadata. Its CODE cost is423
bytes, with no added DATA/OSEG. Together these two changes reduce the floor
from9444 to8952 bytes:7035 production XDATA plus1917 caller-context bytes.
Production CODE is211892 plus55 CONST; the other raw sums above are unchanged.
The profile still exceeds7680 by1272 bytes before runtime/additional caller
storage. The sensor/streamed-display design target reserves a further1024
bytes, so at least2296 bytes plus overhead remain to be removed.
Projection code `03bf9bb` passed
[full Actions36255240167](https://github.com/faronov/cc2530-zigbee/actions/runs/36255240167),
**112/112 jobs**. Both compact workers include the new projection corpus;
all previous host/image/ABI/alias/simulator cases remain. This is still
host/object acceptance for the compact profile, not MCU placement.

## Shared upper workspace

`CC2530_MAC_LINK_WORKSPACE` is a separate opt-in experiment requiring the
consistent compact RAM/LINK profile. It does not enable a board image or
change the ordinary compact profile. The source-owned typed union in
`src/mac_link_workspace_internal.h` alternates the complete617-byte key work
with499 bytes of protocol work:

| Simultaneously live protocol region | Bytes |
| --- | ---: |
| Parent: join, scan, NWK or nested ZDO work | 218 |
| POLL | 109 |
| TX plus decoder, or Association decoder | 135 |
| MAC decoder and beacon fields | 37 |

JOIN -> POLL -> TX -> codec retains all four regions. Observed -> interval ->
exact TX borrows the same TX region; only its outer owner may wipe it.
Association parsing follows POLL return. NWK releases its header hint before
key processing; ZDO queries key status before acquiring its protocol work.
The three separate37-byte key-status outputs, persistent contexts, occupied
queues, frame/grant receipts, counters and retained outcomes stay outside.
The complete key work, including post-seal packet intent, remains intact
until its own secret-wiping return.

The manager checks acquisition, nesting, return, exact I/O loans and scoped
child grants. A rejected acquisition or release cannot clear another owner.
Lower services reject UPPER overlap by default. Counter READ requires the
exact KEY-owned record112 and length-byte outputs; CREATE/SAVE require
record112 and TAKE requires counter4, all under the corresponding grant.
Readonly bounded slices exist only for actual nested inputs, including
`ed_wire_encode` -> `aps_frame_encode` reading the key packet payload.
Neither the entire arena nor the entire KEY variant is a caller whitelist.
Child wire/hash/CCM/AES/NV/flash work is not pooled by this change.

**Target placement is still an obligation.** The entire manager XDATA
allocation, including compiler/parameter storage, must precede
`flash_exec_reserved_end` and every other lower private fence. Its arena must
be nonzero ordinary XDATA below1E00. Runtime entry rejects wrong placement;
source declaration order or native synthetic addresses do not prove the
linked order. No linker aliases or absolute-address arena are supplied.
Other lower-service buffers passed between modules must lie above the
manager. Its CODE and CODE-qualified constant tables also require a valid
common-memory placement; `--codeseg` alone does not bank CONST.

Both-board SDCC4.2 measurements after the combined code-generation lowering:

| Allocation | Compact projection | Shared UPPER | Delta |
| --- | ---: | ---: | ---: |
| Production CODE | 211892 | 230576 | +18684 |
| CONST | 55 | 962 | +907 |
| Production XSEG | 7035 | 6308 | -727 |
| Raw DATA sum | 519 | 556 | +37 |
| Raw OSEG sum | 77 | 74 | -3 |
| BSEG bits | 70 | 80 | +10 |
| Top-level caller contexts | 1917 | 1917 | 0 |
| Object-plus-context floor | 8952 | **8225** | **-727** |

The selected1429 bytes become617 payload +31 ownership +54 compiler/
parameter bytes. Gross saving812 therefore becomes727 net. XISEG remains
zero. This is an expensive CODE/DATA tradeoff, not the hoped-for low-cost
overlay. The floor remains545 above7680 and1569 above the6656 application-
reserve target, before banker/libc/additional caller storage. Raw DATA/OSEG
sums are not physical concurrent IRAM or a stack measurement.

The measured combination saves967 CODE, one XDATA and four DATA bytes versus
the initial UPPER implementation. Its packed16-bit return ABI preserves both
full8-bit arguments and their original conversions, with a volatile parameter
home preventing extra SDCC PUSHes. Byte lookup into the same caller-mask
table and typed traversal of the same loan array preserve all907 manager
CONST bytes. No local PUSH envelope or saved bytes at a call edge increase;
the I/O helper's local envelope falls8->5. This is not a physical SP proof.

`make BOARD=<board> test-mac-link-workspace` separately compiles all39
production modules and runs native/nonrecovering-sanitizer workspace and
146-case POLL/join tests. The workspace test embeds all15 E2E scenarios,
18172 compact checks/9030 cleanup observations and1815278 workspace checks,
including the original1656 ownership, alias, fault and canary checks. The
scalar cases cover every frame/result byte pair, single argument evaluation,
grant-held rejection and borrowed TX releases. The real counter/journal/flash admission
regression provisions through38 modeled flash commands, then checks
precise legitimate handoffs and unchanged FREE/JOIN/wrong-size/offset/alias
rejections. Retained NV-read failure, reset recovery and AES-stall secret
wiping are covered. Six compiled mutations are rejected: malformed READ
length, wrong-owner release, inner TX wipe, missing KEY wipe, unloaned arena
I/O and partial output.
The isolated code-generation study also rejects three scalar ABI mutations
and an actual SDCC mutation removing the volatile parameter home, which adds
two PUSHes. An additional1900544-case admission oracle passes.

The sizeof probe additionally allocates copies of the617-byte arena and
31-byte ownership type. These are layout evidence only: the ledger counts
the real manager in production and adds only1917 caller bytes, never those
probe copies again. Two new CI workers preserve the ordinary compact workers
and every previous corpus. The162 old-profile module/board comparisons retain
instructions and allocation areas. Before refreshing113 metadata digests
across both `tests/` and the board-fixture guards in `tools/`,
both boards reproduced the old pins and retained byte-identical IHX/memory,
parsed maps, non-source-line CDB records and emitted instructions for all80
affected Make-linked images plus the documented independent Association/TX
floor. Ordinary/external join object probes retain complete ADB and emitted
instructions too. The final code-generation promotion additionally preserves
raw ADB/ASM/LST/SYM and REL (except its output-path comment) across162 old-profile
module/board comparisons, with no further source-line shifts. These checks
do not establish a UPPER image: UPPER evidence is **host-tested and
object-checked only**. A real bank map, private-fence placement,
DATA/OSEG/libc/SP7C proof, MCU replay and hardware observations remain absent.

Implementation `525172b`, with the tool-side fixture pin refresh in `84c0014`,
passed [full Actions36265427843](https://github.com/faronov/cc2530-zigbee/actions/runs/36265427843):
**114/114 jobs**, including both shared-UPPER workers, both ordinary compact
workers and every previous corpus. This accepts the new host/object profile
and preserves the existing image/simulator proofs; it does not establish a
combined radio-backed MCU join or application-memory headroom.

## Shared child workspace

`CC2530_MAC_LINK_CHILD_WORKSPACE` additionally pools temporary wire, hash and
NV work. It requires the UPPER/compact/LINK profile and remains opt-in.
Retained keys, counters, diagnostics, flash history, DMA descriptors and
executable flash RAM are not pooled. The typed543-byte arena contains these
alternative branches:

| Branch | Simultaneously live storage |
| --- | --- |
| Wire543 | Buffers236 + crypto40 + engine267 |
| Engine within wire | Parser119 (wire78 + NWK29 + APS12), or CCM267 |
| Hash149 | Key-hash60 + nested MMO89 |
| NV394 | Counter check128 + journal198 + writer36 + reader32 |

NV is a struct, not a union of its four nested users. All final wire syntax
reads precede the parser-to-CCM transition, after which parser reuse is
rejected. Exact source-issued grants constrain owner edges and I/O direction;
AES key/input/info/output roles are not interchangeable. The flash executor
may read only the writer's exact word. Readonly slices and reader prefixes
are admitted only where the real call requires them. Both complete managers,
including ownership and compiler homes, remain excluded from general caller
spans and subject to the existing private-fence placement obligations.

A returning operational NV failure pins the NV branch through owner unwind:
work and the first failure cause survive subsequent calls and failed reopen.
Only the existing host full-power-cycle hook resets this experimental owner;
there is no target software poison-clear API. The actual flash RAM fail-stop
remains nonreturning. Generation exhaustion remains a policy rejection, not
a new flash operation. The real joint model binds five relocated arrays to
typed slots with a contiguous synthetic address mapping that preserves native
padding and overlap; target layout is checked separately, never inferred from
host offsets.

Initial staging-only SDCC4.2 object measurements on both boards:

| Allocation | Shared UPPER | UPPER + CHILD | Delta |
| --- | ---: | ---: | ---: |
| Production CODE | 230576 | 236892 | +6316 |
| CONST | 962 | 1507 | +545 |
| Production XSEG | 6308 | 5727 | -581 |
| Raw DATA sum | 556 | 562 | +6 |
| Raw OSEG sum | 74 | 74 | 0 |
| BSEG bits | 80 | 86 | +6 |
| Caller contexts | 1917 | 1917 | 0 |
| Object-plus-context floor | 8225 | **7644** | **-581** |

The1205 selected bytes become543 arena +31 ownership +50 compiler/marker
bytes. The net saving is581, or1800 versus the original9444-byte LINK floor.
The3139-byte layout probe contains1917 caller bytes plus both arena/owner
copies; only the callers are added to production. **The remaining36 bytes
below7680 are before runtime/additional caller storage, not a proven fit.**
The6656 application-reserve target still requires988 more bytes plus overhead.
Raw DATA/OSEG sums do not measure physical simultaneous IRAM.

`make BOARD=<board> test-mac-link-child-workspace` retains all40 production
objects and eight native/nonrecovering-sanitizer executions: focused
ownership/crypto, real NV/fault/retirement, the accepted UPPER/E2E corpus,
and146-case POLL/join. Isolated both-board evidence covers297852 focused
checks,2737 NV checks, all15 E2E scenarios,18172 compact checks,
1815278 UPPER checks and8185 POLL/join checks per corresponding run.
Six focused mutations and four real-NV mutations were killed; unsuccessful
compilation attempts are not counted as kills. The retirement case reaches
FINISHED and explicitly closes RX through the public adapter; FINISHED alone
does not imply RX is off.

The separate four-module `child_abi` image exercises actual SDCC generic
pointers and typed volatile reads of already allocated parameter homes,
including all256 pointer tags and43098 checks. Its11105 CODE/1551 XSEG image
has stack50..7C, observed peak59 and an intact7D..FF canary with the real
1F00..1FFF IRAM alias. It never calls AES, flash or radio; executor entry is a
forbidden breakpoint. This is **image-checked and simulated ABI evidence
only**, not a40-module join, common CODE placement or combined SP proof.
The full profile remains **host-tested and object-checked**. The two new
workers retain every previous corpus.

Integration reproduced all205 production/probe object artifacts per board
(only differing REL output-path comments were excluded). It also preserved
full IHX, memory, parsed maps, complete non-source-line CDB and emitted
instructions/areas for26 affected legacy images per board before refreshing
54 metadata digests in11 verifier files. Both `tests/` and `tools/` were
scanned; no tool-side pin needed changing. The21 applicable existing static
image/fixture guards per board remain intact. Each new CI worker explicitly
runs all18 ABI-verifier regressions, including wrong counts/PC, stack/canary,
malformed raw metadata and missing/swapped artifacts.
The new source-line records add2 security,4 counter and6 resident CDB records,
with no non-source-line changes. Their exhaustive three-mutations-per-record
coverage therefore increases by6,12 and18 respectively; the exact count
guards are refreshed, not relaxed. MMO/key-hash standalone counts, runtime
cases, stack limits and all existing mutation generators remain unchanged.

### Join-chain stack reduction candidate

The draft radio-backed caller needs77 bytes above the initial SP4F under a
fail-closed bound over emitted calls (near call +2, banked call
max(5,3+callee), measured library helpers). The unchanged cap SP7C allows45.
The driver keeps its public API and ordering. The frame consumer and serial
update run inside observation, a close/drain result is returned to one
step-level drain instead of nesting another observation, and one
`mac_adapter_prepare` site serves ARM and TX-draw with the existing policy
selection. Profile-gated AES, MMO, key-hash and timebase guards reload their
existing parameter homes rather than keeping pushed register copies.

Old-profile and non-workspace objects of the four gated files are
byte-identical. On both boards the bound drops77→64, with CODE-747,
XDATA-11 and raw DATA-24 (driver34→22, AES30→26, MMO19→16, key hash3→0,
timebase8→6). The ledgers are compact211786/7018, UPPER230419/6292 and CHILD
236145 CODE/1507 CONST/5716 XDATA (floor7633). The deepest path remains the
shared MAC decode and workspace-guard chain (64), then NV/flash57, key
receive55 and CCM/wire54. This is **host-tested and object/stack-analyzed**
only. Linked DATA placement, simulation and SP7C remain open gates.

Implementation `317e0f7`, with the exact mutation-count correction in
`e286b72`, passed
[full Actions36277911852](https://github.com/faronov/cc2530-zigbee/actions/runs/36277911852):
**116/116 jobs**. This includes both CHILD workers, the retained UPPER and
compact workers, every previous image/simulator corpus and the complete
artifact campaigns. It accepts the experimental profile and narrow ABI
proof, not a combined radio-backed MCU join or application headroom.

## Direct MAC staging

`CC2530_MAC_LINK_DIRECT` requires the CHILD profile and remains opt-in. It
removes the private125-byte MAC build buffer from `nwk_aps_t`. In this
profile only, `src/nwk_aps_direct.c` replaces `src/nwk_aps_transmit.c`:
before any NWK/APS sequence, counter or security mutation it borrows the
idle interval owner's engine frame with `mac_tx_interval_stage()`, builds and
protects the frame there and admits it in place with
`mac_tx_interval_submit_staged()`. The copying `mac_tx_interval_submit()`,
`nwk_aps_transmit.c` and every legacy image remain unchanged.

- A loan is issued only for an arena-admitted IDLE owner and records owner
  plus generation; another stage replaces it. A busy owner yields
  `NWK_APS_STATE` with no NWK/APS/counter change.
- INVALID arguments and FULL (not IDLE) return before the loan is examined
  and keep it. A missing or mismatched loan is STATE. A matching loan is
  consumed whatever the subsequent result, so a rejected frame cannot be
  resubmitted without restaging.
- The ordinary clock and generation checks then apply. The in-place decode
  admits only DATA frames without pending, broadcast PAN or `FFFE` source/
  destination; NWK/APS never emits commands. Rejection leaves owner control
  unchanged. The DSN is inserted only on admission.
- The IDLE engine frame is dead storage: copy-out is STATE while IDLE, the
  adapter copies only at prepare and ACK matching reads the DSN only while
  STOPPING. `nwk_aps_step` transmits only after its own IDLE check.
- Reinitializing an owner with an outstanding loan is a caller error, because
  its generation restarts at zero.
- `mac_tx_interval_stage()` is the join stack's first `__banked` function
  that returns a generic pointer. SDCC returns it in DPL/DPH/B, a subset of
  the32-bit DPL/DPH/B/A return exercised by the linked banked foundation
  fixture ([banked ABI](BANKED_ABI.md#compiler-call-and-return-abi)).
  `_sdcc_banked_ret` saves A/PSW and uses only R0/R3/R4 as scratch. No DIRECT
  image has linked or simulated this call yet.

The same profile also removes two of the three 37-byte
`security_keys_status_t` snapshots. `bdb_join` and `zdo_runtime` name
`nwk_aps_keys` through private macros, so one XDATA definition remains in
`nwk_aps.c`. The snapshot is a copy of status and configuration only, never
key material or a durable counter. Each module calls
`security_keys_status()` in the same activation before reading it and
returns on any failure; a successful call writes all 37 bytes. Among the 22
functions that can transitively refresh the snapshot, none is called
between a reader's own refresh and a later read, and none retains a
pointer. `nwk_aps_complete()` is called in `nwk_aps_transmit()` only just
before returning. The ZDO announce and address-request builders run only
from `received()` after its refresh.

Both-board SDCC4.2 object measurements:

| Allocation | UPPER + CHILD | + DIRECT | Delta |
| --- | ---: | ---: | ---: |
| Production CODE | 236892 | 238278 | +1386 |
| CONST | 1507 | 1507 | 0 |
| Production XSEG | 5727 | 5681 | -46 |
| Raw DATA sum | 562 | 562 | 0 |
| Raw OSEG sum | 74 | 74 | 0 |
| BSEG bits | 86 | 87 | +1 |
| Caller contexts | 1917 | 1792 | -125 |
| Object-plus-context floor | 7644 | **7473** | **-171** |

Staging adds 28 XSEG bytes: the7-byte loan, two3-byte owner-pointer homes
and 12 parameter-home bytes in `mac_tx`, plus the3-byte volatile frame
pointer in `nwk_aps_direct`. Snapshot sharing removes 74 (`bdb_join`
186 to149, `zdo_runtime`174 to137) and changes no CODE, DATA, OSEG or BSEG.
Apart from the removed definitions, those objects' instructions differ only
in the referenced symbol. CODE/CONST reaches239785 bytes. With the23-byte
libc runtime the floor is7496, still before banker/additional caller storage
and not a proven fit. The6656 application target needs817 more plus
overhead.

The reduction stops below1024 because SDCC4.2 large-model parameter/local
homes (2826 of the5681 XSEG bytes) are never overlaid, and stack-auto, xstack
and private overlays remain excluded. The remaining large buffers are
retained or backpressure storage: radio/receipt/attempt frames, key and
counter state, diagnostics/history, DMA/executable RAM, the ZDO response/
application slots and the separate security input/output packets.

`make BOARD=<board> test-mac-link-direct` builds all40 production objects
and the SDCC layout probe, then runs native and nonrecovering-sanitizer
executions of the focused staging test (6 cases,153 checks), the real CHILD
NV corpus (2737 checks), all15 E2E scenarios (1815278 UPPER checks) and the
146-case POLL/join corpus (8185 checks). The focused test compares admitted
state byte for byte with the copying submit. The target then reruns the E2E,
compact and UPPER corpus with `tests/mac_link_direct_poison.c`, linked
through GNU ld `--wrap`. Before each of the11 exported refresher entries,
the harness overwrites the shared snapshot with a changing pattern: 20003
times, with output unchanged. Corrupting the snapshot just after a refresh
fails at the first `bdb_join_start`. Eleven staging mutations were killed. An
offline differential trace showed the complete TX FIFO sequence of the E2E
and NV cases byte-identical to CHILD on both boards. Unchanged CHILD objects
were reproduced exactly; the five layout-dependent DIRECT objects differ
only in numeric field offsets. All legacy compilations of the snapshot
modules, on both boards, remain byte-identical. This is **host-tested and
object-checked** only for this staging-only step: no SP proof, simulation or hardware.

### Shallow call lowering

The integrated DIRECT-only workspace/codec path preserves guard order and
ownership while reducing register saves across nested calls. Volatile static
parameter homes and the private near command parser are used only in this
profile. The deep reference remains host-only, and the other profiles retain
their original code paths. A four-way native/sanitizer, shallow/deep comparison
checks95,789,977 records per executable on each board; comparisons are within
one build mode, not across native/sanitizer address layouts.

Together with the shallow driver/time/crypto changes, the object-level stack
bound fell from77 through64 to53 bytes aboveSP4F. The workspace step costs4
XDATA bytes and removes23 summed function DATA bytes. The subsequent
NV/counter/wire/AES reloads bring the bound to45, at8 additional XDATA bytes,
16 fewer summed function DATA and363 fewer CODE bytes. The current production
ledger is237434 CODE,1507 CONST,5682 XSEG and1792 caller-context bytes
(floor7474); the initial staging-only table above is historical. Its XSEG
ceiling changes5681 to5682, not the complete7680-byte memory limit.

An independent actual linked-instruction check now confirms45/45 stack bytes
on both boards, including bank transitions and actual libc. Bank nesting
also reaches8/8, so neither allowance has margin. The complete ten-case
caller additionally compares shallow/deep NV/wire/AES execution in native
and sanitizer modes. None of these static bounds is an observed MCU peak. The
[execution plan](LINK_JOIN_PLAN.md#execution-record) records the complete
linked caller and its separate compiler-scratch lifetime proof.

### Current object ledger after the ZHA-join series

The tables above are historical measurements. The later scan filter, RX-loss
salvage/lossy scan reporting, ZDO Basic/Active_EP/Simple_Desc serving and the
security-counter/NV/crypto changes move the exact SDCC4.2 object ledgers
(both boards) and the measured contexts to `bdb_join_t` 1438 (was1433: a
32-bit lossy scan count and a timeliness byte) and `mac_link_driver_t` 305
(was304: one drop flag). Caller contexts are therefore1923 bytes (DIRECT
1798):

| Profile | CODE | CONST | XSEG | Raw DATA | OSEG | Bits | Floor |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Compact | 217183 | 205/206 | 7051 | 607 | 81 | 70 | 8974 |
| Shared UPPER | 236052 | 1112/1113 | 6322 | 646 | 74 | 80 | 8245 |
| UPPER + CHILD | 241708 | 1657/1658 | 5746 | 644 | 74 | 86 | 7669 |
| DIRECT | 242997 | 1657/1658 | 5712 | 605 | 74 | 87 | 7510 |

CONST is generic/LG: the Basic model string is one byte longer on LG.
The synthetic sensor demo (zdo_srv three-cluster list +2 XSEG, zdo_runtime
+2 DATA/-1 XSEG) and the radio_autoack stopped-head/RXOVERF fixes (+39 CODE)
are included. The CHILD floor leaves11 bytes below7680 before runtime/additional caller
storage; this remains object arithmetic, not a fit. The raw DATA growth is
mainly compiler spill frames of the new ZDO serving functions, not
simultaneous IRAM. The CHILD pointer-ABI image keeps11105 CODE,1551+64
XDATA,43098 checks and peakSP59/7C; only nine typed-loan size/offset
constants in `mac_link_workspace` changed, following the three added
`zdo_srv_local_t` bytes (profile, endpoint: 16 to19) and the resulting ZDO
server/descriptor union (37 to38). Because `zdo_runtime.h` now includes
`board.h`, that object's SDCC symbol keys and CDB/map debug records differ by
board while its CODE does not; the proof therefore pins one complete raw set
per board, selected by the exact CDB and never mixed.

## Remaining #13/#14 work

The next increment is one combined banked MCU image containing the adapter,
driver and link consumers, within 7680 ordinary XDATA. It needs DATA/stack/ABI
and alias proofs. The returning-work, projection, UPPER, CHILD and DIRECT
experiments lower the floor from9444 to7473 before banker/libc/additional
caller storage. The207-byte arithmetic remainder is not sufficient evidence
of a combined fit. The [J1-J6 execution plan](LINK_JOIN_PLAN.md) now records
the integrated caller's actual both-board resource links:7641 XDATA and
243593/243633 CODE (generic/LG). Linked near/far destinations, function-owned
DATA/OSEG/libc lifetimes and the45-byte static stack bound now pass.
Full image admission and alias-aware MCU execution remain open. The later
1024-byte application reserve does not block the first bounded join.
After that come the #45
decision, documentation and closure at the documented offline evidence level.
