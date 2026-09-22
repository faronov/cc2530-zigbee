# Boot-disarmed same-owner TX/RX fixture

`IMAGE=radio_link_fixture` is the real board composition for #77. It links
existing startup/status/board code, real clock/timebase and the
[same-owner radio service](RADIO_AUTOACK.md). It does not link the native
controller, standalone harness, legacy reset-exclusive RX/TX/FIFO owners,
MAC scheduler or MAC Timer. **Never flash `radio_autoack_test.ihx`.**

Evidence is **host-tested, image-checked and alias-aware simulated**. The
manual operator is prepared, but this document records **no hardware run**.
It does not establish delivery, an ACK/response match, retries, a timed MAC
window, continuous receiver-on POLL closure, security or network membership.
#40's captured-edge/freshness/timing gate and full #50 remain open.

## Admission and real sequence

Boot clears the mailbox and exposes a36-byte `M3LK` version1 status at WAIT.
It performs ordinary board initialization but no clock selection or RF work.
ARM and RUN each have an independent256-poll admission budget. Empty calls
consume that budget; partial, corrupt, reversed or replayed packets fault.
Terminal END/FAULT and reinitialization cannot clear history.

Write all eight bytes only while halted at WAIT in the indicated phase:

| Phase | Exact mailbox bytes | Next state |
| --- | --- | --- |
| DISARMED | `A9 56 1A E5 36 C9 4C B3` | ARMED |
| ARMED | `56 A9 1A E5 C9 36 4C B3` | ADMITTED |

RUN consumption returns at WAIT without touching clock/radio. The separate
continuous continuation consumes the experiment before any service call:

1. Select real undivided XOSC32 and acquire the filtered receiver.
2. Soft-stop and explicitly drain complete CRC-good/bad preparation frames.
3. Refuse TX if preparation consumed both frame slots. Otherwise make exactly
   one ordinary hardware-gated CCA/TX attempt.
4. After PHY completion, service raw RX with AUTOACK/filtering disabled.
   CCA_BUSY skips this foreground interval; it is not a retry.
5. Soft-stop/drain, then publish END only after actual stopped/empty confirmation.

**Initial acquisition may transmit hardware AUTOACK without CPU intervention.**
The one-attempt limit applies to ordinary TX, not all over-air packets.
This is neither passive reception nor a finite automatic-ACK-count guarantee.
There is no automatic hardware execution, mid-sequence debugger pause, retry,
RX flush, unconditional TX, manual ACK or final TXFIFO flush.

The immutable public profile uses channel26, raw TXPOWER05, IEEE10..17,
PAN1234 and short5678. Those addresses are synthetic, not board identity.
The immutable ordinary13-byte body is:

```text
41 88 5A FF FF FF FF 34 12 4C 4E 4B 31
```

This is legacy unsecured DATA to broadcast PAN/destination, source1234,
DSN5A and `LNK1`, with **no ACK request**. Hardware supplies the FCS.
No calibrated EIRP, channel exclusivity or legal RF permission follows from
the raw power byte; existing [primary-source limits](RADIO_AUTOACK.md#primary-basis-and-limits)
still apply.

## Bounds, capture and errors

Each service call has a1024-raw-tick deadline and256-poll work cap.
The foreground receive interval has a separate1024-tick deadline and4096-call
cap, measured from foreground PHY-completion confirmation. This is **not
captured TX end or the MAC ACK window**. Half-range continuity and CPU
progress remain prerequisites. Clock ambiguity or work exhaustion faults;
it does not become a timeout/completion success.

Two128-byte slots retain complete bodies and raw RSSI/CRC/correlation metadata.
`before_tx` counts frames copied during preparation, including on a later
preparation capacity fault. A third body is not silently read/dropped:
capacity faults retain ownership and any pending FIFO data.

The first complete foreground CRC-good/bad body ends that receive loop.
It can be unrelated traffic; even an ACK-shaped body is not delivery evidence.
Final stop/drain may additionally copy a late body. In particular,
**RX_WINDOW_TIMEOUT may coexist with stored post-TX bytes**: those bytes do
not retroactively become an in-window receipt. A timeout never proves silence.
There is no per-frame captured timestamp or finer classification of final
drainage in this ABI.

END has one completed experiment/heartbeat and an OFF_NOACK, physically idle,
empty owner. TXFIFO remains owned. Operational/capacity/time/work failures
retain their state without implicit abort/reset/flush. **RF can remain active
at FAULT**, including automatic ACK in initial phases. Separate physical
containment/recovery is required; terminal CPU halt alone is not RF-off.

The status contains phase/reason/stage, consumed experiment, ordinary attempt,
completion, outcome, clock/TX/owner results, frame/pre-TX counts, fixed profile,
LE receive polls/elapsed/admission budget, guards and reserved zeros.
Clock and radio diagnostics remain separate19- and26-byte objects.
The decoder rejects live RUNNING inspection and inconsistent completion.

## Linked proof and coverage

| Resource | Generic | LG Rev0.3 |
| --- | --- | --- |
| Complete populated CODE |10758|10798|
| Ordinary XDATA |750|750|
| Including status reservation |814/1024|814/1024|
| Stack start / initial SP |21 /20|21 /20|
| Corpus sampled / whole-run SP high-water |31 /37|31 /37|

CODE limit is16384 and the exercised stack cap is7C; all values in the stack
rows are hexadecimal. Existing image budgets and15-second simulator limits
are unchanged. This is an isolated diagnostic image, not complete-stack fit
or interrupt-nesting acceptance.

Ownership is proved from all allocation records, not just exported objects:
timebase/clock/radio occupy0000..014E (335 bytes); caller allocation is
014F..02E1; complete libc scratch is02E2..02ED (12 bytes, including memcpy,
memset and generic-store parameters). Ordinary allocation stays below1E00;
1F00..1FFF still aliases IRAM and is never separately allocated.

`tools/radio_link_fixture.py` pins complete CODE, raw CDB bytes and the full
canonical symbol map for both boards. Eight relocated listings are snapped
immediately after each link; their ordered instructions, labels and storage
inventory are bound to the actual bytes. Real XREG addresses6000..63FF are
distinguished from larger CODE-table addresses. Checks bind actual RFD
read/write instructions, indexed addresses, EE/EA-only strobes, one destructive
read leaf/call site, and execution of the four real CCA-settling NOPs.
The new clock relocations retain the existing full code/private/public ABI proof.

The35 native-vector sequences replay1096 genuine polls and54878 MMIO/settling
events through linked 8051 instructions, with genuine startup, full frame and
diagnostic snapshots, terminal preservation, GPIO/SFR guards, alias negative
control and cumulative stack high-water. They cover default/armed exhaustion,
bad/reversed packets, last permitted admission, CRC-good/bad bodies, CCA busy,
empty window, pre-TX capture, clock/acquire/TX/stop failures, capacity and
late final drainage. There are34101/34250 artifact negatives (generic/LG)
plus one alias control per board.
Every CODE byte, map symbol and F/S/L/T CDB record is exercised negatively.
The target corpus does not establish silicon behavior.

Strict native and nonrecovering ASan/UBSan additionally cover every mailbox
bit/partial packet, immutable body/config mutations, terminal reinitialization,
and the full4096-call frozen-time receive cap. **That full receive work-cap
case is native-only**, not represented as executed target coverage.

Focused reproduction:

```sh
make BOARD=generic IMAGE=radio_link_fixture BUILD=build/link-generic test-board
make BOARD=lg_esl29_rev03 IMAGE=radio_link_fixture BUILD=build/link-lg test-board
PYTHONPATH=tools python3 -B -m unittest test_radio_link_fixture -q
```

Follow the [CI-first policy](../CONTRIBUTING.md#development-checks); do not
duplicate the full matrix locally. The28-job matrix retains the existing
seven-path public artifact whitelist and `hardware_tested=false`.
Models, raw received bytes and private reports are never CI artifacts.

## Manual physical operator

`tools/check_radio_link_hardware.py` never flashes, chooses USB automatically,
or runs from Make/CI. It requires exact board/artifacts/hash, explicit USB bus
and address, separate reset/CPU/read/write/breakpoint permissions and
`--confirm-rf-including-autoack`. A freshly scoped external programming
operation must install the genuine image first; consumed old programming/RF
runners are not reusable authorization.

Use a local canonical artifact directory including all eight `.rst` snapshots;
the seven-file CI archive alone is not the full operator preflight input.
Before loading/opening the backend, the operator verifies the exact image and
creates a new private capture in a user-owned0700 directory outside Git.
The capture is0600, exclusively created without symlink traversal/overwrite.

The operator reset-halts and verifies **every physical CODE byte** before
execution. It inspects boot/default poll/ARM/RUN at WAIT, checks full register
preservation and zero service work, disables WAIT, and then resumes once
through the whole RF sequence to END or FAULT. An optional `--admit-only`
stops at ADMITTED without RF; it is not a hardware TX/RX observation.
A60-second experiment deadline complements the independent10-second transport
cap. Errors never cause reset/retry/resume/flush recovery.

Complete raw states, frames and diagnostics are synced only to the private
capture, including fault and CRC-bad records. Stdout contains only non-identifying
profile/results/counts, not received bodies, identities or their hashes.
Capture or debugger cleanup failure suppresses success. Synthetic operator
tests cover every transport-call failure boundary, private-write failures,
all outcomes, retained faults, permission/identity preflight and cleanup.

No physical board identity, private dump/capture or SDK binary is introduced.
The [provenance ledger](PROVENANCE.md#filtered-receiverautoack-ownership-sources)
records the original-code and primary-hardware basis.
