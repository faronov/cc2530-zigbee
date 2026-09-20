# Bounded legacy Data Request extraction (#46)

Original BSD-3-Clause. **Host-tested, linked-image-checked and alias-aware
simulated for generic and LG; not hardware-observed.** This is one conditional
offline legacy extraction transaction, not a complete MAC, MLME-ASSOCIATE
procedure, radio adapter, repeated-poll service or Zigbee membership.

`mac_poll` uses the actual MAC encoder/decoder and the existing device-wide
`mac_tx` slot. Its genuine caller additionally forwards Association Responses
through the real `mac_association` context. Source events are explicitly
synthetic; neither API success nor the simulator supplies physical evidence.
There is no hardware, USB, RF, GPIO, private-material or SDK dependency.

## Primary basis and selected contract

The reviewed public primary sources are:

- IEEE Std 802.15.4-2006, published 8 September 2006, public UBC mirror:
  <https://people.ece.ubc.ca/~edc/7860/data/802.15.4-2006.pdf>.
  SHA256 `d245c8bb208f6cdb585fb753cefa67e367d30eebd55b9ed9957c8fd152d6a055`.
- Zigbee Core R22, 05-3474-22, 19 April 2017, public mirror
  `pvginkel/ZigBeeHomeAutomation` at
  `fc30145012eacd3a5af170b8ae8e0d4c848c2525`,
  `Documents/docs-05-3474-22-0csg-zigbee-specification.pdf`.
  SHA256 `991dd02b7e5764ac349c47d5c4cf0fbe01529ff6594df03e8dad3d3d4df9e274`.

Pages below are printed pages. No specification text or implementation is
imported. The selected policy is page-0, receiver-on, nonbeacon, unsecured
legacy operation. Channels 11..26 are caller inputs; there is no RF default.

| Decision | Primary evidence and boundary |
| --- | --- |
| Canonical Request | IEEE 7.3.4, pp.153-154: command04, AR1, TX Pending0, common compressed PAN; association extraction uses extended source |
| ACK matching | IEEE 7.2.2.3.1, p.147; 7.5.6.4.3, p.190: receiver-ignored ACK fields, exact DSN and sender window; actual `mac_tx` supplies parsing, retries and IFS |
| Pending0/1 and results | IEEE 7.1.16.1.3-7.1.16.3, pp.133-135, Figure40; 7.5.6.3, pp.187-188: Pending0 NO_DATA, Pending1 reception, nonempty DATA SUCCESS, empty DATA or command NO_DATA |
| Complete receive boundary | IEEE 6.2.1.3, p.34; 7.5.6.2, pp.186-187: complete PSDU and CRC precede reception processing; a frame start before the deadline is insufficient |
| Configured PIB F | IEEE 7.4.2, p.160, Table86 p.164: higher-layer-set `macMaxFrameTotalWaitTime`, measured in symbols for nonbeacon operation; no inferred default or repair of printed Eq.(14) |
| Physical close | IEEE 6.2.2.7.3, pp.39-40: normal TRX_OFF may finish an in-progress PPDU; this does not extend the logical extraction deadline |
| Receiver ACK | IEEE 7.5.6.2, p.187; 7.5.6.4.2, p.189: valid addressed unicast DATA/command AR1 requires an ACK echoing its DSN, starting 12 symbols after complete reception |
| Spacing | IEEE 7.5.1.3, pp.169-170: applicable IFS remains a lower-MAC obligation, including locally generated ACK activity |
| Further extraction | IEEE 7.5.6.3, p.188: received DATA Pending can lead to another Request; this module retains the flag but never starts that optional transaction |
| Indirect retry | IEEE 7.5.6.4.3, p.190: another Request is required; the response DSN can repeat, so no invented on-air replay suppression |
| Unknown coordinator IEEE | IEEE 7.5.3.1, p.181: short selection may learn a Response IEEE; #43 retains SOURCE_UNBOUND, not an authenticated short/IEEE binding |
| R22 receive gate | R22 Annex D.1/D.3, pp.513-514: IEEE2015 and additional header forms remain outside this explicit legacy slice |

`frame_wait` is a caller-valid, currently configured PIB F, not an arbitrary
application timeout. Its deployment derivation remains gated. Request
work/lifetime and cleanup limits are separate project policy, not IEEE
primitive enumeration values or a guessed total Association confirmation
timer. The IEEE2006 p.180 decision-wait/extraction wording and #45 remain open.

## API, memory and states

| API | Contract |
| --- | --- |
| `mac_poll_init` | Fresh ordinary memory only; not recovery of an active/faulted radio epoch |
| `mac_poll_start` | Copy configuration, encode canonical Data Request, lease an idle existing transmitter; no DSN/generation/IFS reset |
| `mac_poll_step` | Consume one ordered event, bounded work and time; issue one-shot PREPARE, TX-step grant or CLOSE |
| `mac_poll_take` | One-shot copied result, available before cleanup completes |
| `mac_poll_release` | Require DONE+taken, release the actual transmitter slot without resetting DSN/generation/IFS |

Caller storage is disjoint ordinary RAM, never MMIO, compiler scratch,
reserved status or the IRAM alias. Inputs remain valid throughout their call;
frame/ACK byte spans may be CODE or RAM and are not retained. There is no heap.
All calls are serialized and nonreentrant with the codec, TX and Association
modules. API argument/state errors leave caller state, outputs and TX unchanged.

The SDCC context is **266 bytes**: `mac_poll_control_t control` is 123 bytes,
followed by the 143-byte `mac_poll_record_t record`, including its 125-byte
body. Request/event/action sizes are 35/48/25 bytes. Diagnostics are read-only
`poll.control.<field>` and `poll.record`. The persistent owner pointer names
caller-owned transmitter storage, not a borrowed payload.

Private staging uses the SAME control type, with compile-time size/offset
checks, not an alias cast or an assumed host layout. Start copies the request
before clearing the remaining control representation. Each operation reloads
control; step commits only control, never a stale receipt copy. Transient
event/action/time scratch is used only during that foreground call. Native
cases cover inactive bytes and rejected calls contaminating private scratch.

States are IDLE -> ARM -> REQUEST -> RECEIVE -> DRAIN -> DONE, with branches
directly to DRAIN and retained FAULT. Pending0 does not enter RECEIVE.
`MAC_POLL_MAX_WORK=4096`, `MAX_TIME=0x10000000`, local cleanup bounds
4096 symbols/64 steps and TX work512 bound the operation. These do not replace
the transmitter's unchanged 1024-symbol/16-step retirement bounds.
Generation and receive serial correlations do not wrap; bounded action tokens
cannot exhaust 16 bits within the work/cleanup bounds. Eight-bit DSN wrap is
the real transmitter's normal behavior, not proof of freshness.

Time is a continuous uint32 abstract **16-us symbol** epoch. Every true
compared interval/gap must be below 2^31, without hidden wraps. Raw Sleep Timer
ticks and live MAC Timer samples are not captured PHY-end timestamps.

## Actions, receipt and ownership

PREPARE/PREPARED establishes an independently confirmed continuous poll RX/ACK
lease. The real TX action must still be retired under its existing QUIESCED
contract: no future radio/TX/buffer access for that action. No RX gap,
unretired buffer or permission is relabeled quiescent. Current reset-exclusive
platform services cannot implement this handoff.

Each ACTION_TX grants exactly one foreground **real `mac_tx_step`**. The caller
executes it once and reports its return value, original input and foreground
call time. Report generation/token/retry/NB must match the grant. Only the
real ACK_WAIT -> STOPPING/ACKED transition can supply A, the original accepted
ACK's captured trailing end. The witness retains exact bytes/length, CRC truth,
DSN, Pending and time correlation. Actual `mac_tx` owns ACK parsing and
receiver-ignored-bit handling. Failed CRC must never be fed to it as a valid
ACK; DONE, `ready_at` and processing time cannot substitute for A.

Completed pump reports precede later-time polls, frames or cancellation.
An interrupted report can be delivered once after same-time cancellation;
retimestamping or repeating the call is forbidden. An older completed report
after a later cancellation remains stale and cannot release its outstanding
rights.
Fresh frame ends must not precede the previous foreground watermark. The
adapter must drain ordered captures before advancing that watermark; delayed
delivery behind it is an explicit order fault, not silently accepted history.

Pending1 immediately opens **`A < frame_end <= D`, where `D=A+F`**, even while
TX is STOPPING. A complete frame at exact D is processed before that call's
F timeout while the transaction remains active; independent cancellation or
project work/lifetime exhaustion can instead abort it without a protocol
confirmation. A frame ending at D+1 is not timely. Old TX rights must be retired
promptly while the independent poll RX lease continues, not held until F.
Timeout is provisional until CLOSED confirms loss-free ordered drainage
through D. A fresh in-window frame delivered after an earlier timeout poll
is an ordering fault, not permission to reopen or claim absence.

The real MAC codec classifies the received legacy subset. A frame must have
the requested nonbroadcast PAN, exact local destination mode/address and
corresponding coordinator source. No short/IEEE alias is invented. An
Association Response to an extended local address may carry an unbound
coordinator IEEE after short selection. Extended PAN ID is never that IEEE.

| Established logical result | Independent delivery |
| --- | --- |
| Pending0 ACK: NO_DATA | No received body |
| Nonempty DATA: SUCCESS | Copied original body and payload offsets |
| Empty DATA: NO_DATA | Copied empty-DATA frame |
| Supported matching command: NO_DATA | Copied command; still dispatch it |
| Pending1 timeout, loss-free CLOSED | NO_DATA, no body |
| Real terminal retry/CCA exhaustion | NO_ACK or CHANNEL_ACCESS, distinct from local aborts |

Protocol/cause constants are module enums, not IEEE wire enumeration bytes.
CRC failure, malformed, foreign, duplicate report, stale epoch/generation,
late and unsupported observations remain distinct. Unsupported codec forms
cause explicit local aborts. Cancellation, work/lifetime exhaustion,
capture loss, clock/order/adapter faults do not fabricate protocol failures.
Uncertain cleanup faults retain ownership.

Original command Pending and body bytes are retained. Source relation is
UNBOUND0/MATCHED1, aligned with #43. Taking COMMAND/NO_DATA requires prompt
independent dispatch; the genuine harness calls `mac_association_step/take`
with the original copied bytes, epoch and physical end. At exact D the poll
can retain a command while the separately configured, half-open Association
context returns EXPIRED. Both facts are preserved; its lifetime is not F.
An established receipt survives a later cleanup failure, including after take.

General MAC delivery and receiver ACK obligations remain independent of this
filter: duplicate, late, refused or addressed-foreign input can still require
lower-MAC service. Empty DATA does not suppress a received AR bit. CLOSE/CLOSED
must confirm required local ACK activity, applicable IFS, complete ordered
drainage and safe receiver-on handoff before releasing the shared transmitter.
The controller does not mutate read-only `ready_at`, queue immediate ACKs as
ordinary CSMA frames, or supply a successful hardware stub.

## Genuine resources and proof

### Current shared-code refresh (#63)

The explicit R22 Response profile in the shared codec/context does **not**
change this controller's legacy admission. All52 original cases still use
legacy decoding and dispatch. Current linked totals are **31401 CODE,
1898 ordinary XDATA +64 reserved**, stack start4C/unwind4B/observed peak79.
The original CODE8000, XDATA2048 and SP7C caps remain unchanged: this is
foreground composition evidence, not IRQ headroom or full-stack fit.

The proof pins22 public entries and21648 complete non-public F/S/L/T records
(40/542/21015/51), all ordered instructions/data and genuine wrapper calls.
The independent floor now runs108 Association cases, including the preserved
legacy corpus and explicit R22 variants: **20844 CODE,891+64 XDATA,
46/45/71 stack start/unwind/peak**,17 public entries and14005 metadata rows.
Both retain complete continuation/alias/storage/stack checks; the floor is
still not POLL acceptance.

Current hashes live beside their exact layouts in `tests/boot_mac_poll.py`;
the [shared evidence summary](VALIDATION.md#r22-response-profile-and-shared-proof-refresh-63)
records their scope. **The remainder of this resource section is the original
POLL acceptance ledger**, including its historical addresses and hashes,
not the current shared-code measurement.

### Original acceptance ledger

Both board definitions produce identical IHX, CDB and memory reports.
The new **test composition** budget is CODE <=0x8000, ordinary XDATA plus the
64-byte status reservation <=2048, observed SP <=7C. It contains four real
protocol modules and the full caller corpus. Earlier component budgets,
compiler flags/models and cases are unchanged.

| Module | CODE, including constants/startup | XSEG | DSEG | OSEG | BSEG bits |
| --- | ---: | ---: | ---: | ---: | ---: |
| mac_frame | 7009 | 207 | 15 | 10 | 1 |
| mac_tx | 5650 | 191 | 8 | 0 | 3 |
| mac_association | 2674 | 65 | 25 | 0 | 1 |
| mac_poll | 7332 | 323 | 5 | 0 | 4 |
| mac_poll_test | 7809 | 1068 | 0 | 0 | 1 |
| Compiler/runtime | 635 | 24 | separate linker areas | shared overlay maximum | shared backing |

Linked totals: **31109 CODE**, **1878 ordinary XDATA +64 reserved =1942**.
Private XDATA is0000..0311 (786 bytes); caller objects/helpers occupy
0312..073D (1068); runtime073E..0755 (24). The latter include
`__gptrput_PARM_2` at0749. All ordinary allocation is below1E00; the full
1E00..1E3F status reservation and 1F00..1FFF IRAM alias remain unavailable.

IRAM has DSEG53, overlay10, bit backing2, register bank8 and one packing byte.
Stack starts4A, initial/final SP49, and the **observed peak is73**, below7C.
At inter-case continuation boundaries SP is4B, the genuine caller return
address depth. Upper IRAM80..FF, unallocated XDATA, the status tail, peripheral
XDATA and interrupt guards remain unchanged. This is foreground test evidence,
not full-stack capacity or IRQ-nesting headroom.

The proof pins contiguous CODE, all20 public entries/ends/returns, parameter
ABI, and **21422 raw non-public-entry F/S/L/T records**: F40/S527/L20804/T51.
These include all file-scope/helper/local/caller/field and source-address
records, not just a selected local-variable matcher. The private/helper subset
has507 records and caller subset139. Identical public declaration/address
duplicates are permitted; conflicting ones are rejected. Raw file reading
preserves control separators rather than normalizing them away.

All five immediate per-link relocated snapshots are checked in module and
instruction order, byte-for-byte against CODE. Instruction counts/bytes are
4168/7009, 3729/5650, 1715/2674, 4884/7332 and4580/7682 respectively;
127 ordered switch/fixture data bytes complete the caller object. Actual
codec, TX submit/copy/step/release and Association calls are explicitly checked.
Main is7707/end7749; the real completion checkpoint is7746.

**259 artifact +1 missing-alias +17 continuation negatives pass.** They cover
missing/extra/changed CODE, public map/return/address conflicts, every module's
raw metadata drops/duplicates/conflicts, raw-file control separators, all five
listing drops/duplicates/reorders, module order, CODE data, checkpoint,
allocation/stack reports and complete-state restoration.

All **52 shared scenarios** execute unchanged genuine instructions. A
monolithic run exceeded15 seconds; the proof instead uses **13 four-case
processes**, each still subject to the shared15-second limit. Between processes
it preserves and compares PC, all IRAM, all128 SFR bytes and the full64-KiB
XDATA view, including alias and peripheral regions. Stopped timers/inactive
UART are required; writing synthetic SBUF is avoided while its captured zero
byte is still compared. Only simulator statistics restart; maximum observed
SP is aggregated. No return, instruction, case, caller state or event is
patched to succeed. Each long transcript is indexed once.

Native coverage additionally includes exact-sized receive spans0..126,
all invalid channel bytes (shared cases cover every valid11..26 channel),
API/counter atomicity, copied input mutation and staging preservation.
Both strict native and ASan/UBSan runs pass.

### Independent no-poll floor

The separately named floor is **not poll acceptance**:

```
mac_frame -> mac_tx -> mac_association -> unchanged mac_association_test
```

With the frozen shared core it genuinely passes all34 original cases on both
boards: **19780 CODE, 870+64 XDATA, stack start44/unwind43/peak6B**. Private,
caller and runtime allocations are463/383/24 bytes. The same alias, upper-IRAM,
status/unallocated/peripheral and SP7C guards pass in one bounded process.
It covers all15 public entries and13305 raw metadata records
(F19/S393/L12864/T29),398 private/helper and33 caller records, four complete
ordered listings and93 data bytes. **205 artifact +1 alias negatives pass.**
The unchanged caller's locals are register-only; its source-address records
are exercised instead of inventing nonexistent L:L address fixtures.

Historically the old core's floor executed the same34 cases but peaked8C
and failed the guards. The old full poll demand was40216 CODE. Those were
real failures, not waived checks: the separately authorized shared storage
reduction and owned control/test refactoring resolved them. No old corpus or
limit was weakened. The frozen core source SHA256 is
`8ffcc9d05e8994e90715de5bd369176a5358400923117057003c8f7389d37d6b`.

### Hash ledger

Both-board poll:

```
contiguous CODE SHA256
35c4a8245c271cb6115c5d136887acc00bd27ef6471c5b9433bfd72a423b4277
IHX SHA256
461343379c450a558c6356c56adb5062f5457979dfe308d49ec88a24ef215594
full non-public-entry F/S/L/T SHA256
58d34a5bc45a609e3ae72ff05192377897bff46ded0ce8617e2c8dc788465d39
```

Both-board independent floor:

```
contiguous CODE SHA256
432c4b07bb57eae4c0368ae973cd934c97adbb2553d3c4e9122501e73be3877c
IHX SHA256
502a586d1871b41032b0a80469278a821fe4fa6805f9765efe05e1581079d5b6
full non-public-entry F/S/L/T SHA256
0bbb6ecc06b894e87de2d58d24cda8e049877f6edc1fd3a8e1ec2f285b5e7072
```

## Reproduction and integration

Set `board=generic; number=0` or `board=lg_esl29_rev03; number=1`.
The canonical target compiles/runs the strict native binary
`host-mac-poll-tests`, links `mac_poll_test.ihx` and runs the genuine proof:

```sh
out=build/mac-poll-dev/$board
make --no-print-directory -s -j1 BOARD="$board" BUILD="$out" test-mac-poll
cc -std=c99 -O1 -g -Wall -Wextra -Werror -pedantic \
  -DCC2530_HOST_TEST -DCC2530_BOARD=$number -Iinclude -Itests \
  -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie \
  src/mac_frame.c src/mac_tx.c src/mac_association.c src/mac_poll.c \
  tests/test_mac_poll.c -o "$out/test_mac_poll_san"
ASAN_OPTIONS=detect_leaks=1 "$out/test_mac_poll_san"
```

The exact link order is:

```
mac_frame -> mac_tx -> mac_association -> mac_poll -> mac_poll_test
```

Keep the five immediate `mac_poll_test.<module>.rst` copies before another
link overwrites unprefixed listings. Existing SDCC flags remain
`-mmcs51 --model-large --std-c99 --debug --opt-code-size --Werror`;
link flags remain
`--iram-size 0x100 --xram-loc 0 --xram-size 0x1e00 --code-size 0x8000`.
No image-specific behavior flags or board linkage are permitted.

The independent floor uses the same isolated objects and its own snapshots:

```sh
make --no-print-directory -s -j1 BOARD="$board" BUILD="$out" \
  "$out/mac_association_test.rel"
sdcc -mmcs51 --model-large --std-c99 --debug --opt-code-size --Werror \
  -Iinclude -DCC2530_BOARD=$number \
  --iram-size 0x100 --xram-loc 0 --xram-size 0x1e00 --code-size 0x8000 \
  -o "$out/association_tx_floor.ihx" \
  "$out/mac_frame.rel" "$out/mac_tx.rel" "$out/mac_association.rel" \
  "$out/mac_association_test.rel"
for module in mac_frame mac_tx mac_association mac_association_test; do
  cp "$out/$module.rst" "$out/association_tx_floor.$module.rst"
done
python3 -B tests/boot_mac_poll.py --output "$out" --diagnose-floor
```

The canonical target is included once per board in `test-common`, including
the two CI debug-fixture component jobs and the local component matrix.
Make orchestration checks pin the five-module link/snapshot order and component
inventory. Every board image rejects the controller's symbols/sources; the
seven-path artifact-upload whitelist is unchanged. The independent floor is
never a replacement for `test-mac-poll`. Neither executable may be flashed or
uploaded as a board image.

Deployment retains #40 captured-time/provenance/freshness/phase and unified
ownership, configured-F interpretation, #45 total Association confirmation
timing, R22 IEEE2015/additional-header, physical ACK/IFS/arbitration, restoration,
BDB errata/security and membership gates. A live counter is not a captured
PHY end; #44 fixture observations do not satisfy these adapter preconditions.
