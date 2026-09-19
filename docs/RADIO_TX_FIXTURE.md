# Boot-disarmed, one-attempt TX fixture — offline preparation

**Current fixed channel26 profile: offline-tested on both board definitions,
not hardware-observed.** The dated channel15 TX/RX, private-backup and Nordic
records are separate historical evidence, not acceptance of this new profile.
`IMAGE=radio_tx_fixture` is a separately selected board image; it is not the
default bootstrap and its manual runner is never invoked by Make or CI.
Standalone component executables are **never programming inputs/artifacts**.

## Contract and ownership

The fixture uses unchanged board startup/GPIO, timebase, FIFO and TX services.
The clock service has the ABI-compatible storage refactor described below.
The [TX contract](RADIO_TX.md) remains authoritative: full-reset-exclusive
clock/RF/CSP/DMA/IRQ ownership, no previous legacy RX/queue RX in that epoch.
No Timer2, ACK matching, CSMA retries, MAC adapter, security or network claim
is added.

- Boot/CRT initialization is **DISARMED**, with cleared mailbox and zero
  service diagnostics. No clock/FIFO/RF service runs. Unarmed resumes consume
  a finite admission budget and eventually fault; they never transmit.
- ARM then RUN each require a separate valid eight-byte packet while halted
  at WAIT. Each window has256 polls, not a wall-time/calibrated deadline.
  The last timely packet at remaining1 is accepted. Bytes are snapshotted
  once; accepted and rejected admission packets are cleared. Complements,
  channel, guards and phase are checked.
- Packets are `[opcode, ~opcode, 26, ~26, token, ~token, 69, 96]`:
  ARM=`A6`/token`3C`; RUN=`59`/token`C3` (hexadecimal except channel26).
  The channel pair is `1A E5`; old channel15 packets are rejected before MMIO.
  Write only the mailbox, with the CPU halted; partial writes are not an
  admission transaction. Never resume after a debugger write/readback error.
- **RUN consumption returns ADMITTED with no MMIO.** Only a further deliberate
  resume executes real `clock_select_init(XOSC32)` → explicit FIFO clear →
  CODE-body preload → **one `RADIO_TX_IF_CLEAR`** → explicit FIFO clear after
  `PHY_DONE` or `CCA_BUSY` → verified terminal END. No intermediate RF stop
  is supported. The attempt is consumed before calling TX, even on failure.
- Each service receives1024 raw Sleep Timer ticks and an independent256-poll
  bound. The real clock service retains its original bounded cancellation/
  rollback semantics; any clock error still faults this fixture.
- Every fixture error is terminal FAULT: no implicit flush, reset, retry,
  resume or RF-off operation. A frame may already have transmitted and RF may
  remain active. Recalling initialize cannot clear history. END/FAULT loops
  cannot initiate another attempt; only a separately authorized full reset
  establishes a new epoch.

Fixed profile: channel26 (nominal2480MHz, FREQCTRL`56`), raw TXPOWER`05`,
TXCTRL`69`, CCACTRL0`F8`, CCACTRL1`1A`. Only the channel differs from the
previous fixture profile; the thirteen-byte body and PHR15 do not change. No calibrated
board power, EIRP, RSSI/CCA conversion or regulatory permission is implied.
The public thirteen-byte, FCS-free CODE body is:

```text
41 88 5A FF FF FF FF 34 12 54 58 46 31
```

This is unsecured legacy DATA, **no ACK request**, broadcast destination PAN/
short address, synthetic source1234, sequence5A and payload`TXF1`. There is no
real identity. The unchanged driver supplies AUTOCRC. PHY_DONE requires fresh
TXDONE and confirmed idle; it is **not ACK or delivery**. CCA_BUSY is recoverable
driver busy with shutdown confirmed, not a transmission. END's completed bit/
heartbeat means the entire fixture sequence finished, including final explicit
FIFO clear; either of those two outcomes can reach END.

## Clock IRAM prerequisite, resolved without changing old budgets

The first genuine SDCC composition failed to allocate a21-byte OSEG:
clock45 +FIFO31 +TX22 =98 permanent DATA bytes, plus bank0 eight bytes and
one bit-storage byte. Neither lower-IRAM hole could fit the overlay. Rejected
link outputs were not accepted as firmware.

`src/clock.c` now stages bounded state explicitly in XDATA and uses generic
output-store leaves, instead of carrying generic-pointer spills across
nonleaf timebase calls. Its public header, three-byte generic diagnostics
pointer, byte return ABI, ordered MMIO, deadline equality, source-departure
evidence, rollback and original-error retention are unchanged. Caller fields
are still published at the original observation boundaries, including errors.
Request/rollback share one private count because their lifetimes are sequential;
the caller's two diagnostic records remain distinct. The four initial state
reads are all performed in the same order; only their private rejection
accumulator is combined.

Actual final clock allocation: **12 DATA, zero clock overlay,54 XDATA**.
XDATA consists of output pointer3, observed fields5, count7, rollback flag1,
work20, and18 compiler argument/local bytes. Previously it used45 DATA and
44 XDATA. An intermediate65-XDATA staging version was discarded: it would
have exceeded the old AES board's512-byte reservation budget. No budget,
alias guard, stack cap, compiler flag or forced overlap was relaxed.

| Actual linked image | Before CODE | Final CODE | Ordinary XDATA before → final | Stack start before → final |
| --- | ---: | ---: | ---: | --- |
| Standalone clock, either board |3351|3402|154 →164|4E →21|
| Clock board, generic |3758|3781|156 →166|4E →21|
| Clock board, LG |3798|3821|156 →166|4E →21|

Add the existing64-byte status reservation to each ordinary-XDATA count;
the standalone eight-byte result occupies that reservation, not additional
ordinary RAM. Reserved IRAM stack increases178 →223 bytes. The new actual
clock module is1875 CODE bytes. Its SDCC end record precedes `MOV DPL,A; RET`,
so the proved end is the record+3, not record+1. The single write is now
`MOV CLKCONCMD,R7` (`8F C6`), rather than the old `88 C6`; the hardware access
and its order are unchanged.

Final SHA256:

```text
clock_test, either:
82d47ac15fe82617a3ad86052e785cfa57b2e61e028f63113487e468efa128d8
clock_fixture, generic:
260c8b60fc3780e37d7082b44cb71ed8b369987fa7a14d76c35264af6dae5cf2
clock_fixture, lg_esl29_rev03:
0a02809ee84cc76456d316c55fd6e52f7c9baf6d146e5fbe057659626158736c
```

The five coupled FIFO/DMA/AES/PRNG/RX images retain their complete native and
genuine execution corpora on both boards, with reviewed relocation/ABI and
immediate clock/service listing proofs. AES remains447 ordinary+64 reserved
=511/512, stack`0x41`, CODE12776 generic/12816 LG. Its257-cycle run uses nine
bounded continuations with complete CPU/RAM/peripheral preservation, not a
larger simulator timeout. See the [current ledger](VALIDATION.md#boot-disarmed-tx-board-composition-and-clock-staging).

## Final TX image, memory and offline evidence

| Board | CODE bytes | WAIT / END / FAULT | SHA256 |
| --- | ---: | --- | --- |
| generic |11291|2725 /2727 /272A|`f402afdfabcda87d6d13c73768d9f479798bd5adc3f743ee8f0f0f4d165ac20b`|
| lg_esl29_rev03 |11331|274D /274F /2752|`f951f0324e0149fc16ee110cfd975ff61b12749e903b3fcaecdd4dfb809201e6`|

Both have347 ordinary XDATA (`0000..015A`) +64 reserved =**411/512**.
This separately scoped512-byte fixture budget includes the complete real
services, all caller/diagnostic storage and runtime scratch; no old component
budget increases. Layout:

```text
0000..00E2 whole service/compiler private prefix (227 bytes)
00E3..00FA fixture state (24)
00FB..0102 mailbox (8)
0103..0115 clock diagnostic (19)
0116..012A FIFO diagnostic (21)
012B..0147 TX diagnostic (29)
0148       initialized flag
0149..0156 remaining fixture private/caller storage
0157..0159 compiler-generated memset parameter storage
015A       generic-store runtime parameter
1E00..1E3F existing64-byte M0 reservation,32 bytes occupied
```

The TX driver's own prefix still ends00AC; clock storage follows at00AD..00E2.
Every caller object follows **both** prefixes. Ordinary allocation stays below
1E00. XDATA1F00..1FFF is the IRAM alias, **not another pool**.
The byte-only wire record is version1/size24: signature`M3TX` at0, version/
size4/5, phase6, reason7, stage8, attempts9, completed10, clock/FIFO/TX results
11..13, channel/power/length14..16, remaining LE17..18, guards`69 96`19..20,
and zero reserved21..23. Phases1..6 are DISARMED/ARMED/ADMITTED/RUNNING/END/
FAULT; RUNNING is not an allowed debugger observation point.
IRAM:65 DATA (clock12+FIFO31+TX22), overlay21, bank0 eight bytes,
one bit-storage byte/six bits, two-byte packing gap. Stack61..FF is159 bytes;
initial SP`0x60`, checkpoint SP`0x62`, MMIO-sampled peak`0x74` and full-run
simulator high-water **`0x77`**, pinned separately across all36 executed cases.
The cumulative statistic is required from each completed vector and checked
against samples and the unchanged `<0x80` bound. Upper80..FF stays guarded
and calls unwind. This is not a universal worst-case or IRQ stack bound.
Generic body is2C0E..2C1A; LG body2C36..2C42.

Evidence, separately classified:

- **Strict native, both boards:**1,332 fixture polls each; entire unchanged
  clock CMD/STA/source/clamping/bounds/rollback and failure-helper corpora,
  plus the old16 field combinations ×771 clock-board steps/error cases.
  Those four native executables also pass AddressSanitizer and
  UndefinedBehaviorSanitizer together on both board definitions.
- **Image-checked:** complete contiguous CODE/constants/runtime, public/
  private/caller/field ABI, all storage/prefix boundaries, exact instruction
  and MMIO inventories, and all nine ordered relocated listing inventories.
  Negative controls mutate every CODE byte, ABI/allocation metadata, listing
  deletion/duplication/reordering and alias behavior.
- **Genuine linked/alias-aware simulation, both boards:**38 TX cases,
  83 checkpoints,38,674 actual MMIO events;40 standalone clock scenarios and
  the entire unchanged771-step clock-board/timeout/cancellation/late-source
  corpus. Real copied/emitted services execute, not patched success returns.
  All existing15-second per-simulator deadlines and upper-IRAM caps remain.
  Transcript markers are indexed once.
- **Synthetic host runner/USB tests:** independent permission and CODE
  preflight, deliberate admissions, busy/completion/fault, all-call transport
  failures, deadline exhaustion/late completion, context/readback errors,
  denied mailbox writes and cleanup failure. No physical USB is loaded.

Native and linked peripheral models are synthetic and share expectations;
they do not independently establish silicon RF/clock timing or physical USB.
The changed clock images do not inherit old image-specific hardware acceptance.

## Canonical offline checks

From the repository root, run each board serially with SDCC4.2.0 #13081:

```sh
for board in generic lg_esl29_rev03; do
    out="build/radio-tx-fixture-check/$board"
    make -j1 BOARD="$board" IMAGE=radio_tx_fixture BUILD="$out" test-board || exit
    make -j1 BOARD="$board" IMAGE=clock_fixture BUILD="$out/clock" \
        test-clock test-board || exit
done
PYTHONPATH=tools python3 -B -m unittest \
    test_radio_tx_fixture test_clock_fixture test_local_checks test_m0_artifacts -q
python3 tools/check_repository.py
git diff --check
```

The original clock-board negative/execution corpus now uses the real8FC6
write site and new source-evidence field through the common verifier, with no
monkeypatch adapter or fabricated return. Clock standalone/board snapshots
are distinct and their instruction order is checked. The new TX fixture adds
two jobs to the24-job board matrix, retaining exactly seven upload paths,
`hardware_tested=false` and exclusion of all standalone/native/model code.

## Manual runner boundary — not authorization

`tools/check_radio_tx_hardware.py` is manual-only; never add it to ordinary
build, test, CI or automatic programming. Before use the parent/operator must
separately establish exact board/revision, verified programmed board image,
physical RF scope/channel/power/test environment, recovery conditions and any
independent sniffer. No component image may be flashed.

The runner requires explicit bus/address, board, output directory and exact
SHA256, **all five independent** `--allow-target-reset`, `--allow-cpu-control`,
`--allow-memory-access`, `--allow-memory-write`, `--allow-breakpoints`, and
`--confirm-rf-one-attempt`. Invalid local artifacts or missing permission fail
before USB loading. No USB autoselection, flash/configuration programming,
new target/adapter operation, kernel detach or automatic reattach is added.

After the authorized reset it checks PC0/debug configuration26 and reads back
the **entire selected CODE** before any execution. It observes DISARMED, one
real empty-mailbox resume, ARM and ADMITTED; every snapshot/write preserves CPU
context and observes cleared packets. Only after inspecting ADMITTED does it
resume continuously to END/FAULT. There is one60-second whole-experiment
deadline, with independent10-second existing transport operation caps; a late
operation is failure, not fabricated completion.

`--admit-only` stops halted at ADMITTED. **This leaves a live authorization:
another resume would perform the RF sequence.** It is not a disarmed state,
and still requires the RF opt-in. Cancel it only through separately authorized
reset/recovery. A normal successful run remains halted at END.

Inspect only ordinary mailbox/state/diagnostic RAM and M0 at those checkpoints.
Never single-step an active calibration/transmit/shutdown or inject live MMIO.
FAULT/transport/deadline failures cause no automatic retry/reset/resume/flush;
cleanup errors are reported and suppress success output. Radio-off is **not
promised on failure**. The processed result separates PHY_DONE from CCA_BUSY
and lists untested gates; it does not collect ambient payloads, private
identities or captures. Keep any later sensitive capture/recovery material
private and out of git/CI.

## Integration and remaining gates

The selected IMAGE, service-prefix link order, native target, nine immediate
snapshots and genuine boot proof are wired through the standard Make/image/
DebugImage/CI paths. Only the TX board image may link this radio service;
its native tests/models remain forbidden everywhere. FIFO service admission
does not admit the unrelated FIFO board caller. Loaders remain offline-only.

The common clock verifier and original clock-board execution corpus use the
staged implementation directly. Its relocation allowlist intentionally rejects
unreviewed consumers. FIFO/DMA/AES/PRNG/RX board images retain their complete
emission proofs and corresponding native/boot/runner tests. Clock and PRNG
manual preflights now validate the actual staged instruction/operand and
stack layouts; their gates are not bypassed. AES has only one byte of its
unchanged budget margin. No old physical record is reused as new acceptance.

The separately authorized
[2026-09-19 LG acceptance](DEBUGGING.md#2026-09-19-lg-single-attempt-tx-acceptance)
observed the admission boundaries, one PHY_DONE result and exactly one
byte-identical public body in an independent channel15 Nordic capture.
The verified channel15 image was left halted at END274F/status2B/config26.
This is hardware evidence for that one profile, not acceptance of the current
channel26 image. No programming or target execution accompanied this profile change.

#12's physical busy-channel, independent FCS and failure/recovery observations
remain open. #15 lab/capture/calibration gates, same-reset RX/TX ownership/
adapter and complete M3 acceptance remain open. Generic-board RF and all
other channel/power profiles remain unobserved.

## Channel26 profile boundary

This prospective isolated-test profile changes exactly five linked operand
bytes on each board: four channel values `0F ->1A` and the admission complement
`F0 ->E5`. The generic offsets are27FF/284D/2967/296F/2AE9; LG adds28 hex.
Image sizes, all complete private/public ABI records, the other eight ordered
listing inventories, storage and stack limits remain identical to freshly
verified channel15 references. Historical hardware artifacts were not rebuilt
or relabeled.

Both canonical channel26 board proofs pass all38 cases,83 checkpoints and
38674 actual synthetic MMIO events. Two new scenarios reject intact old-profile
ARM/RUN packets without MMIO; the original36 remain. Successful paths check
actual FREQCTRL56/raw TXPOWER05. Both native fixture executables pass ASan/UBSan,
and65 focused runner/artifact/Make tests pass, including strict rejection of
channel15 state records. Manual output names `profile_channel` and
`profile_txpower_raw` explicitly; these are configured profile values, not RF
calibration measurements.

The separate passive Nordic survey observed0 decoded records during a
requested15-second channel26 capture with a valid empty PCAP header, compared
with259 records/requested5 seconds on channel15. Channel readback was checked
before/after; the sniffer was sleep-commanded on26 and its port released.
These counts do not establish CCA/RF silence, absence of Wi-Fi/BLE, or that only
our packets can appear. The coordinator/network was not changed, and the
CC2530 was not reset, resumed or programmed by that survey or these offline
checks. A future channel26 transmission still needs its own explicit
programming/identity/permission preflight and independent capture.

## Primary sources and provenance

Original BSD-3-Clause; no SDK, IAR sample or programmer implementation imported.
The unchanged driver is supported by:

- TI **SWRU191F**, April2009/revised April2014: §23.8 pp218–222,
  immediate TX-on-CCA §23.14.9.38 p253, Table23-6 p256, FREQCTRL p261,
  TXPOWER/TXCTRL p262 and CCACTRL0/1 p264. Clock CLKCONCMD/CLKCONSTA and
  RC source clamping: pp68–69. Full TX flag/shutdown/reset references remain
  in [RADIO_TX.md](RADIO_TX.md).
- TI **CC2530 SWRS081B**, revised February2011, Table2 p24: reference-EM
  characterization for raw05/TXCTRL69, not measured LG/generic board power.
- **SWRZ031**, April2009: this slice uses neither DMA nor Timer2.

Linker/storage claims above come from actual canonical SDCC listings/maps/
memory reports, not an assumption that all nominal RAM is freely allocatable.
