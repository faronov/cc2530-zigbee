# Boot-disarmed raw IRND board fixture (#67)

`IMAGE=radio_noise_fixture` is a **distinct genuine board image**, not the
standalone `radio_noise_test.ihx`. It links the unchanged #66 collector,
#65 health core, real init-time clock/timebase services, SDCC CRT, M0 status
and selected board startup. It contains no synthetic MMIO model, test main,
injected events, USB code or normal-radio owner.

The public implementation is **host-tested, image-checked and simulated** for
`generic` and `lg_esl29_rev03`, including native ASan/UBSan. Physical acceptance
remains pending. These tests do not qualify an entropy source, a secure
sampling interval, a seed, conditioning or a DRBG. Diagnostic RCT/APT
cutoffs **21/589 are not accepted entropy parameters**. A periodic balanced
synthetic stream passes deliberately.

## Public offline commands

```sh
make BOARD=generic IMAGE=radio_noise_fixture all test-radio-noise-fixture
make BOARD=lg_esl29_rev03 IMAGE=radio_noise_fixture all test-radio-noise-fixture
make BOARD=generic IMAGE=radio_noise_fixture test-radio-noise-fixture-sanitize
make BOARD=lg_esl29_rev03 IMAGE=radio_noise_fixture test-radio-noise-fixture-sanitize
PYTHONPATH=tools python3 -B -m unittest test_radio_noise_fixture test_local_checks test_m0_artifacts -q
```

`test-board`, `test-local` and the two added CI matrix configurations include
the fixture's native/sanitizer/image/simulator checks. The older24 board/image
combinations and all standalone component targets remain. All24 older BINs
were independently rebuilt from exported `d022127` sources, image-checked,
and compared byte-for-byte with the current tree: unchanged. CI still publishes
only the seven existing artifact paths, never raw captures or a host test
transcript. No Make/CI target opens hardware, flashes, refreshes a display,
transmits RF or runs a physical operator.
The strict new `DebugImage` preflight also requires the nine local per-image
listing snapshots produced by `make all`; a downloaded seven-file CI bundle
alone does not contain those listings. Rebuild the corresponding public
sources offline rather than bypassing that check.

**Never flash `radio_noise_test.ihx` or `noise_health_test.ihx`.** Selection of
this new board image is not authorization to program or execute it on equipment.

## One full-reset epoch

1. Genuine startup disables IEN0/1/2 first. The existing LG Rev0.3 board code
   establishes display-off/reset/control latches before output directions;
   generic has no added GPIO policy. Real `bringup_initialize()` records the
   original32-byte M0 ABI at `1E00`, heartbeat0.
2. The fixture initializes DISARMED, clears command/capture/health/clock
   storage, and installs the fixed request. Repeated `initialize()` is inert
   within the same epoch. No clock selection, Sleep Timer read or radio access
   occurs before a valid ARM packet. Empty input remains DISARMED (or ADMITTED)
   at the next WAIT indefinitely, without peripheral effects.
3. ARM is valid only in DISARMED. Its clock precondition is exact CMD/STA `C9`.
   The real `clock_select_init(XOSC32, 1024, 4096, &clock)` must succeed.
   It preserves LF/TICKSPD and confirms CMD/STA `88`; ARM never starts RX.
   Failure retains the original clock result and the real service's rollback
   diagnostics, then enters FAULT. The fixture adds no restoration operation.
4. RUN is valid only in ADMITTED, with CMD/STA still `88`. The attempt is
   consumed **before** the sole real collector call. Request: channel26,
   1024 bits, interval1 raw tick, timeout100000 raw ticks, limit10000 polls.
   The packet cannot select another profile or change these bounds.
5. After return, every captured raw bit, including a partial failing prefix,
   is fed in order to one real health context. Acquisition result and health
   result remain separate. Acquisition OK reaches END even on RCT/APT failure;
   its heartbeat increments once. Acquisition/clock/invariant failure reaches
   FAULT with heartbeat0 and preserved diagnostics/capture. No fixture
   stop, flush, retry, cleanup or reset follows an error. **RX may remain on.**
6. END/FAULT are terminal. Neither another command, another poll/initialize
   call nor continued execution can resume radio. Ownership is not released
   to normal RX/TX/AUTOACK. A separately established full hardware reset,
   including peripheral reset and fresh admission, is a **new epoch**, not
   a normal-radio handoff.

The collector itself still enforces its unchanged reset-exclusive
[ownership/profile contract](ARCHITECTURE.md#isolated-raw-irnd-acquisition).
Software observations cannot establish prior exclusive ownership or raw ADC
freshness. No debugger breakpoint, step, injected timer value or other pause
is allowed between RUN and END/FAULT on hardware. Simulator IO stops below
are only an offline technique for modeling peripherals.

## Stable operator interface

```python
image = DebugImage(output, board, "radio_noise_fixture")
proof = image.radio_noise_proof
```

`tools/radio_noise_fixture.py` exports `SIZE=20`, `COMMAND_SIZE=16`,
`CAPTURE_SIZE=171`, `HEALTH_SIZE=15`, `CLOCK_SIZE=19`, `CHECKPOINTS`,
`ARM`, `RUN`, `decode(raw)` and `decode_capture(raw)`. Import and verification
are offline. `decode_capture` includes **PRIVATE ONLY** `data_hex`; never
include actual raw bytes in public logs, Git or CI artifacts.

All addresses below are the same on both boards except CODE checkpoints:

| Object | Address | Bytes | Meaning |
| --- | ---: | ---: | --- |
| `state` | `009B` |20| Versioned byte-only record |
| `command` | `00AF` |16| Sole permitted operator-written object |
| `request` | `00BF` |11| Actual SDCC raw request, not a serialized replacement |
| `capture` | `00CA` |171| Actual collector capture, including128 packed raw bytes |
| `health` | `0175` |15| Actual health context |
| `clock` | `0184` |19| Actual clock request/rollback diagnostics |

`proof["checkpoints"]` is `[WAIT, END, FAULT]`; `CHECKPOINTS` contains
`_radio_noise_fixture_wait`, `_radio_noise_fixture_end`,
`_radio_noise_fixture_fault`. Generic CODE addresses are `1EB0/1EB2/1EB5`;
LG addresses are `1ED8/1EDA/1EDD`. WAIT is naked `NOP; RET`; each terminal
is naked `NOP; SJMP self`. At all three checkpoints require
**SP=stack_start+1=`30`, DPS=0**, ordinary register-bank0 and unbanked CODE.
`proof["stack_start"]` and `image.metrics["iram_stack_start"]` are `2F`.

Keep WAIT's breakpoint installed: at a verified WAIT only, write/readback
exact ARM or RUN, step the NOP, then resume. ARM must return to WAIT with
ADMITTED and zero command. RUN must reach END or FAULT without an intermediate
WAIT. No packet fragments, other SRAM writes, synthetic event injection,
function-return patch, automatic reset or retry are part of this interface.

Exact16-byte packets:

```text
ARM a6 59 1a e5 00 04 01 00 a0 86 01 10 27 67 98 3c
RUN 59 a6 1a e5 00 04 01 00 a0 86 01 10 27 67 98 c3
```

Fields are opcode/complement, channel/complement, samples16, interval16,
timeout24, limit16, magic/complement, stage token; multibyte integers are
little-endian. Every byte is compared with its exact CODE literal.
Packets are copied to a separate private object then cleared, including
invalid packets. Premature RUN, duplicate ARM and other stale/wrong-profile
input fail terminally. Terminal calls leave any newly written command alone.
Packets provide deliberate sequencing, **not authentication or replay
protection across full resets**.

State offsets:

| Offset | Field |
| --- | --- |
|0..5|`"M2RN"`, version1, size20|
|6|phase:1 DISARMED,2 ADMITTED,3 END,4 FAULT|
|7|reason:0 none,1 PACKET,2 INVARIANT,3 CLOCK,4 ACQUISITION|
|8|attempts:0 or1|
|9|collector result,255 before attempted|
|10|health result,255 before attempted|
|11|clock result,8 NOT_ATTEMPTED initially|
|12..13|first health failure: one-based raw-bit index, or0|
|14..19|channel26, profile1, two reserved zeros, guards69/96|

`decode` returns phase **names**, integer attempts/result/health_result/
first_failure/clock_result/reason, and a fault name or `None`. It rejects a
live acquisition record. The capture decoder exposes samples, timed_samples,
phase, actions, elapsed_ticks, first_before, last_after, min_gap, max_gap,
max_span, polls and raw status fields as well as private data_hex. Bits are
first-sample/low-bit first. `samples` and `timed_samples` may differ by one
on a failed raw read/post-observation. Unused bits must remain zero.

### Explicit manual operator

`tools/check_radio_noise_hardware.py` is separate from Make/CI and **never
programs flash**. It requires an already programmed, fully checked matching
board image and an explicitly selected debugger. Example placeholders:

```sh
python3 tools/check_radio_noise_hardware.py \
  --bus BUS --address ADDRESS --board lg_esl29_rev03 \
  --output CHECKED_BUILD_DIRECTORY --sha256 CHECKED_BIN_SHA256 \
  --capture /absolute/private-0700-directory/new-capture.jsonl \
  --allow-target-reset --allow-cpu-control --allow-memory-access \
  --allow-memory-write --allow-breakpoints --confirm-one-irnd-capture
```

The optional `--admit-only` stops at ADMITTED without starting RX.
The operator requires the exact nonempty BIN identity before opening USB,
then performs its own reset and compares every physical CODE byte before
resuming. It clears all old breakpoints, checks the actual empty-command
continuation, writes/readbacks the exact ARM and RUN packets at WAIT and
preserves full CPU/FMAP context. Only RUN may start acquisition, with no
intermediate debug pause. A60-second experiment bound supplements individual
transport deadlines; a late operation is failure, never permission to retry.

A fixture-only, fixed non-destructive SFR whitelist checks live GPIO
selection/direction/pulls/routing, driven LG output pins, IRQ/DMA state,
clock/sleep and unrelated flags at each stop. The GPIO reads observe pins,
not output latches: the verified reset/startup CODE establishes the latch
writes. Only monotonic IRCON.STIF0->1 is allowed; no global debugger SFR
permission or arbitrary MMIO access is added. The reader does not touch
RFRND, FIFO data or the Sleep Timer latch.

Raw state, command, capture, health, clock, M0 and live observations are
written and synchronized to a new exclusive0600 file in a user-owned0700
directory outside Git **before decoding or continuing**, including faults
and malformed records. File/transport/context/cleanup failures prevent a
success report or another resume; there is no automatic reset or recovery.
The public JSON contains only processed diagnostics, not raw sample bytes.

END acceptance requires a complete1024-bit capture, confirmed soft stop,
consistent raw24-bit brackets and agreement between the final packed bit
and `last_raw`. An independent whole-prefix recount from the actual raw bits
checks the complete15-byte health context, retained result and exact first
failure. A valid RCT/APT failure remains distinct from acquisition failure;
even failure on the final startup sample can have countdown zero.
No outcome qualifies entropy.

Focused offline operator coverage:

```sh
PYTHONPATH=tools python3 -B -m unittest test_radio_noise_hardware -q
```

It includes freshly built genuine images for both boards, every synthetic
transport boundary, private-output failures, GPIO/STIF/configuration guards,
all register-bank/DPS combinations and fail-stop/late USB boundaries of the
actual fixed reader, as well as the existing guarded SRAM writer. No test
loads a real USB backend or accesses equipment.

### Independent physical admission obligations

The parent-owned physical operator must bind the exact board/image, full CODE,
fresh reset/clock history, M0 and safe GPIO policy **before** ARM. Confirm
unchanged GPIO selections/directions/latches/pulls/routing, disabled enables,
no DMA/CSP/other radio owner, no pending clock request and the specified raw
clock baseline. Do not accept a previously running image merely because
CMD/STA match. Do not infer a swapped panel's type or voltage from straps.

Only documented monotonic IRCON.STIF (`C0` bit7) may latch0->1 after reset
admission; it may not clear until a separately authorized full reset.
All other flags and enable/history checks remain strict. STIF is not
permission to weaken fresh-reset admission or enable the Sleep Timer IRQ.
SWRU191F section11.2 gives the reset compare value `FFFFFF`; it can become
pending while waiting at a debugger checkpoint. The firmware never clears
it. CPU context, M0 immutable bytes, command consumption, clock19, capture171
and health15 must be checked separately, not inferred from END alone.

## Image/ABI and evidence

SDCC4.2.0 model-large, unbanked, no `--stack-auto` or banking change:

| Resource | Both-board result |
| --- | --- |
| CODE |generic9426, LG9466, cap16384 (global unbanked bound unchanged32768)|
| Ordinary nonaliased XDATA |440 bytes, range`0000..01B7`|
| Status |32 used at`1E00`, full64-byte reservation unchanged|
| XDATA reservation accounting |440+64=504, cap768|
| Service/compiler prefix |`0000..009A`, including clock/health/timebase and collector|
| Caller/compiler area |`009B..01AB`, separate command/state/request/capture/health/clock|
| Complete libc suffix |`01AC..01B7`; memcpy starts428, memset436, gptrput439|
| IRAM |bank0:8, permanent DATA:30, overlay:7, bit backing:1, one hole below stack|
| Reserved stack |`2F..FF`,209 bytes; checkpoint SP30|
| Observed SP |projected IO3E, cumulative genuine execution high-water40; cap7C|

`1F00..1FFF` still aliases IRAM and is never an extra allocation/pool.
The entire prefix and libc suffix are proved, not only exported objects.
Capture171/request11/health15/clock19 have target size assertions; native
tests serialize fields explicitly rather than assuming desktop padding.
Command literals are CODE, pointers to collector buffers are XDATA, health
and clock calls preserve their generic-pointer ABI. Calls are serialized
foreground/non-reentrant; no ISR is installed.

BIN SHA-256:

* generic: `43e4e89625e5b421dc39e98d406877c9f6fa294cdf6aa54c6aa38999408791a6`
* LG: `c7d9ba504648c166a1368a3dc9500038c05169c9d29dcfcab51a57fa3b0ad9a7`

Complete CODE, raw CDB, parsed map and **all nine immediately snapshotted
relocated listings** are pinned. Genuine shared-directory relinking proves
later standalone links cannot replace the fixture evidence. Neither this
composition nor its new clock relocation expands an older standalone proof.
CDB loaders decode raw bytes without universal-newline translation; CRLF,
bare CR and other non-LF separators remain distinct from the pinned LF bytes.
Both-board file-level regressions reject all ten separator alternatives even
when the artifact manifest is refreshed to match the damaged CDB.

Each board has8177 strict native cases and49 genuine linked scenarios:
empty/disarmed/stale/malformed packets, every command-byte alternative
natively, ARM-only clock setup, invariant/reset-history rejection, full1024
capture, raw wrap/half-range/timeouts, failing partial prefixes, separate
RCT/APT failures, failed clock request/rollback, full work caps, terminal
retention and explicit new-reset disarming. Artifact negatives are16219
(generic) and16293 (LG), including CRLF raw-identity rejection, plus the
missing-alias negative.

The simulator supplies immutable synthetic peripheral changes at actual
reads/writes, checking actual PC/DPTR/operands, real compiler calls, complete
raw/context diagnostics, one-shot ownership, M0 heartbeat, board GPIO,
unallocated XDATA and SP7C/alias guards. An independent bit oracle checks
packing and partial tails. Long **identical** read-only observation cycles
use real breakpoint hit counts and inspect actual poll counters before the
last iteration; changing stimuli and writes are not compressed. No poll
limit, code, return, timer calculation or15-second process deadline is patched.
This establishes software behavior, not CC2530 timing, RF behavior, physical
USB compatibility, source independence or entropy acceptance.

## Primary sources

Original BSD-3-Clause composition and synthetic tests, no imported SDK or
third-party implementation. TI **SWRU191F, April2014**: sections14.2.2
(p.144) and23.12 (pp.236–237), FRMCTRL0 (p.259), RXMASKCLR (p.260),
RFRND.IRND/QRND (p.272); clock CMD/STA (pp.68–69), IRCON.STIF register,
Sleep Timer compare section11.2 and memory/stack chapter2. TI **SWRZ031,
April2009**, issues1/2 concern DMA variable lengths and Timer2 latching;
neither supplies raw-noise qualification and neither mechanism is introduced
here. NIST SP800-90B January2018 sections4.3/4.4 govern the separate health
accounting, not acceptance of the diagnostic cutoffs.
See [source review](PROVENANCE.md#rf-noise-and-entropy-assessment-sources).
