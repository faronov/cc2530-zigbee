# Radio-backed MCU join execution plan

## Objective and fixed boundaries

Implement one complete receiver-on end-device join through the real
`join_smoke` caller, link driver, interval MAC, radio adapter, BDB, security
and durable NV services. The first acceptance is **MCU execution with
synthetic peripherals and an independent synthetic coordinator**, not a
physical join or complete Home Assistant interview.

The 2026-09-27 execution order below supersedes arithmetic RAM estimates as
the next #13/#14 integration gate. It does not reopen completed #26, bypass
the #45 conformance decision, or close secure restart/rejoin #27.

- Ordinary XDATA, including caller, banker and libc, ends before `0x1E00`
  (7680 bytes). The 48-byte JSN1 diagnostic block uses the existing reserved
  status region, not additional ordinary allocation.
- `0x1F00..0x1FFF` aliases IRAM; it is never an independent memory pool.
  Real DATA reservations, bit storage and OSEG remain accounted for.
- Initial SP is `0x4F`; maximum SP remains `0x7C`: at most 45 added bytes,
  including return addresses, bank transitions and register saves.
- Every populated physical CODE address is below `0x3E800`; the NV pages,
  lock/configuration page and information page remain excluded.
- No successful service stubs, injected protocol state, unchecked DATA
  overlays, discarded RX frames, weaker ownership checks or automatic
  counter/key initialization are admissible.
- The later 1024-byte application reserve is not a prerequisite for this
  first bounded join. No RF, flashing, HA mutation, private capture/key
  material or new proprietary dependency is included in this work.

## Ordered work and exit criteria

| Step | Work and owned surfaces | Required exit evidence |
| --- | --- | --- |
| J1 | Integrate DIRECT staging/shared status and the shallow driver/guard candidate with the existing caller. Production files, headers, host regressions and resource ledger. | Reproducible opt-in recipes; genuine caller reaches authenticated READY on the host model; admission, exhaustion, existing-NV refusal and retained failures still work. Previous profiles and their acceptance cases remain present. |
| J2 | Build all real banked services plus startup, board, caller, banker and libc. Preserve compiler originals and snapshot relocated listings immediately after link. | Actual linked XDATA/CODE/IRAM report, exact input set and hashes, correct physical bank limits. A resource-fitting link alone is explicitly **not** executable-image acceptance. |
| J3 | Shorten every excessive emitted stack path, starting with the shared workspace checks and MAC decode, then counter/NV/flash, key reception and CCM. | Bounds from actual calls, bank transitions and PUSH/POP behavior are at most 45 above SP4F; no recursive/unclassified path is ignored. New static storage is charged to J2. |
| J4 | Solve physical function-spill placement and verify the linked ABI. Extend existing instruction-transfer and byte-liveness machinery rather than trusting area names. | Complete raw CDB/map/object/listing/CODE identities; live-across-call DATA, OSEG and libc scratch cannot collide; pointer, near/far and alias invariants reject mutations. Any J3 source change invalidates and reruns this proof. |
| J5 | Execute the actual board caller from reset with public synthetic provisioning and bounded random input. Reuse the independent peer and real peripheral models. | Authenticated BDB READY through actual radio FIFO traffic, key verification and NV operations; observed SP <= 7C and intact guards. Include admission refusal, no parent, exhausted randomness, missing key, radio failure and uncertain flash-busy retention without fabricated cleanup. No private progress/state injection. |
| J6 | Wire the final target and negative controls into Make/CI; update support claims and issue evidence. | Full GitHub Actions acceptance for the exact revision, preserving every previous worker/case/deadline. Distinguish host-tested, image-checked, simulated and hardware-observed results. |

J1 and J2 establish the shared baseline. Bounded J3 implementation may run
in an isolated worktree while J2 integration proceeds, but its result must
be rebuilt into the same image before J4/J5. Function-area splitting is
only preparation: provisional placements must never be described as a
proved overlay or used to authorize flashing.

The stop condition for an optimization is a measured improvement with
unchanged semantics and a reproducible regression. If local call/register
changes cannot meet the stack budget, make the offending operation an
explicit returning phase of the foreground state machine, preserving its
owned inputs, durable-error semantics and RX coverage. Do not keep shaving
unrelated buffers or raise a memory ceiling to declare success.

## Compiler comparison: supporting work, not a dependency

Keep SDCC 4.2.0 as the integration baseline. The
[existing 4.6 comparison](MAC_LINK_DRIVER.md#sdcc-resource-comparison)
did not establish XDATA or stack savings for a complete working image.
There is no assumed gain from LLVM or a compiler upgrade.

After identifying a concrete expensive sequence, reduce an original public
source reproducer for bank calls, memory-space pointers or register
preservation. Record source, flags, compiler identity and equivalent
observable behavior. Compare populated executable CODE (not HEX/debug file
length), XDATA, simultaneous DATA and maximum stack separately. Include
startup/runtime costs explicitly.

An IAR comparison is conditional on legally available EW8051 and a bounded
ABI/port experiment. No purchase, installation or trial registration is
authorized by this plan. The supplied microbenchmarks are motivation, not
evidence that our real call chains fit or that a complete toolchain port is
the fastest route to join.

## Execution record

At plan creation, the parent draft's actual link was refused at 7811
ordinary XDATA bytes. Four reviewed, isolated candidates have been applied
to the development worktree:

- DIRECT staging plus shared key status:
  `02cc1b92ba1efee492b4deea661ffc77b7785340c25cc0c2aba310fb3623dc7c`.
- Shallow driver and parameter-home reloads:
  `edb767a1f18c5cf4473e7b40838126c40797eda18b11ddde0fd892a12aec337d`.
- Shallow workspace guards and MAC decode:
  `65e14198c5eed0a9d008692834f428f72a8761ae9eead8b78e1a57716fe1ddad`.
- NV/counter/wire/AES parameter-home reloads:
  `dac5dc1b6af10bfcc406a52a84fc3cc2c257a755219146e2845c5001b961ce57`.

J1's integrated caller passes all seven fresh-process native and
nonrecovering-sanitizer cases on both boards: authenticated READY in 599 polls,
9 backoff inputs and 9 peer-observed transmissions, plus all six negative
admission/failure cases. The same seven cases now compare shallow/deep caller
builds within each native/sanitizer mode, including the real NV/wire/AES paths.
The original DIRECT and previous-profile corpora remain in CI.

The first two candidates produced actual7629-byte XDATA links, and the third
reached7633. After the fourth candidate and corrected near-call bank groups,
the current real links are:

| Board | Ordinary XDATA / limit | Free XDATA | Populated CODE | Last physical CODE |
| --- | ---: | ---: | ---: | --- |
| generic | 7641 / 7680 | 39 | 243593 | `0x3E361` |
| lg_esl29_rev03 | 7641 / 7680 | 39 | 243633 | `0x3E361` |

All47 objects,102 function-spill areas and immediately snapshotted listings
are recorded per board. Summed function DATA is503 bytes, not a simultaneous
allocation; it uses the same22+27 physically reserved bytes under the
byte-lifetime proof below. The caller has88 dedicated bits in11 DATA bytes,
plus the same10-byte OSEG and45-byte stack. Resource fit alone remains
**not execution admission**. J1 and J2 are complete at their stated
host/resource evidence levels; full acceptance remains J6.

ASlink's map truncates symbol names to 32 columns even in wide mode. The
new linker recipe also emits complete NoICE `DEF` records; the resource
reader cross-checks their 32-column projection against every map symbol
and keeps full names rather than merging colliding areas/parameter homes.
Raw map, NoICE, CDB, objects, listings and image identities are retained.
NoICE is parsed as data, never executed as debugger commands.

J3's workspace/MAC-decode change reduced the object-level stack
bound from64 to53 above SP4F. It uses
DIRECT-only parameter-home reloads and equivalent shallow guard/parser paths,
not skipped admission or ownership checks. A same-mode shallow/deep host
comparison covers95,789,977 records on each board, independently for native
and nonrecovering-sanitizer builds. Native and sanitizer digests are not
compared with one another because their address/redzone layouts differ.
The fourth candidate reduces53 to45, adding8 XDATA bytes for flash-writer
and AES sample homes while removing16 summed function DATA and363 CODE bytes.
It changes no call edges, flash programming-history rule, fail-stop, crypto
check or ownership admission. The staging-only XSEG ceiling5681 becomes5682;
the complete ordinary-XDATA ceiling remains7680. Other profiles retain their
original emitted artifacts.

An independent **actual linked-instruction stack checker** now verifies both
boards, rather than inheriting the specialist's object estimate. It walks
471/472 functions including actual libc, tracks PUSH/POP and near/far return
costs, and rejects recursion, inconsistent stack merges, unknown SP writes,
ISR returns and interrupt enables. Exact relocated bank trampolines prove
their5-byte peak/3-byte resident call cost and2-byte return cost. The checked
CRT sets SP4F, disables interrupts before calls and adds only the real startup
return address. The copied flash engine and retained busy fail-stop are bound
to their actual123-byte template; this is not yet a RAM-execution observation.

The main bound is **45/45 bytes (SP7C)** on both boards. Startup needs2 bytes
on generic and4 on LG. Maximum bank nesting is **8/8**. There is no spare
stack or bank-depth allowance. J3's static exit criterion is met; alias-aware
MCU stack observation, full image admission and execution remain J4/J5.

J4's linked transfer analysis exposed a real flaw in the earlier resource
packing: near calls could select a different bank, including a call into the
middle of another function. `mac_join`, `mac_poll` and `mac_association` now
share one28901-byte group. `bdb_join`, `bdb_join_init`, `mac_scan`,
`nwk_candidates`, `nwk_beacon` and `nwk_parent` share another30318-byte group.
No extra far call or stack cost was introduced. The checker verifies the
intended module-local/global symbol as well as the decoded target and far
setup. Runtime coverage includes the HOME comparator and the CSEG tail.
The old resource-only layouts must never be executed.

The new function-owned DATA analysis reuses the existing backwards
byte-liveness engine. CDB declarations, listing allocations and instructions
must agree; dedicated bits, real physical backing, OSEG and transitive libc
clobbers are accounted for. Uninitialized/retained scratch, recursion,
cross-function spill use and source indirect IRAM escapes are rejected.
Before the fourth candidate, the provisional placements had57 conflicting
call sites;2449 byte-pair inequalities moved19 function areas. The new
candidate is solved afresh and really relinked, without reusing those
placements as proof. Its independent strict check passes:

| Board | Decoded instructions | Functions / calls | DATA-analyzed functions | Live-byte pairs |
| --- | ---: | ---: | ---: | ---: |
| generic | 153769 | 466 / 4627 | 463 | 2338 |
| lg_esl29_rev03 | 153783 | 467 / 4628 | 464 | 2338 |

These are **linked transfer and compiler-scratch lifetime checks**, not
complete J4/J5 acceptance. Static startup/template checks do not replace
actual execution.
The older banked caller retains its279-function/1262-pair liveness result.

The subsequent immutable admission pins both complete raw images and their
CDB/map/NoICE/objects/relocated listings/runtime members. It independently
accounts for all7641 XDATA bytes and1421 located CDB object/parameter homes.
The unchanged26-byte SDCC runtime scratch is now linked first, below every
closed private fence, rather than relying on incomplete suffix exclusions.
The actual reset replay then found another placement error: `clock`'s
private deadline/expired outputs were below `flash_exec_reserved_end`, so
the real workspace guard correctly refused them. `flash_exec` now precedes
both `timebase` and `clock`; admission checks this required positive path
as well as exclusion boundaries. No pointer guard was relaxed and no
memory, CODE-size, static-stack or bank-depth budget changed.

The generic image has now been **simulated from reset through authenticated
READY**: public ARM/RUN, real provisioning, radio-backed scan/association,
AES/DMA, key exchange and copied flash-RAM/NV execution. All602 caller polls
(including599 running polls) match the independent native/sanitizer peer
reference, including its9 transmissions and157320 selected peripheral
checkpoints. Observed peak is **SP7B/7C**. The actual terminal READY loop is
executed and preserves the retained owners and media.
The successful bound report is
`build/join-prefix/generic/join-replay-0.json`; the full run's output is
`build/join-replay-full.log`. This is the first complete radio-backed MCU
positive join, not physical RF or an HA interview. The LG full replay and
new Actions acceptance remain open; the subsequent failure increment is
recorded below.

### Complete-caller failure corpus

The caller's fresh-process host corpus now has ten cases, each compared
between shallow/deep native builds and separately between shallow/deep
nonrecovering-sanitizer builds on both boards. Existing cases0-6 are retained;
case7 still invokes the legacy host E2E corpus rather than a board-caller
scenario. No production instruction, image pin or memory limit changes.

| Case | External stimulus | Required outcome |
| --- | --- | --- |
| 8 | The peer completes association but withholds Transport Key; the modeled physical clock keeps advancing. | Genuine `BDB_JOIN_KEY_TIMEOUT`, no authenticated READY, no invented key or member state. |
| 9 | RFERRF is asserted while the real adapter is closing after the first scan transmission. | Caller FAULT/driver RADIO, retained adapter fault and original MAC owner, no fabricated retirement. |
| 10 | The first provisioning erase is accepted but FCTL remains busy through all 65,535 real poll reads. | No caller return: the actual copied flash engine remains in its XMAP RAM loop, with writer/journal PENDING and no binding, READY or NV completion. |

The link peer enables the joint model's physical flash timing: an erase
completes after 30,477 polls and a word program after 31, derived from
SWRU191 (20 ms / 20 us at 21 cycles per poll, 32 MHz). With the former
2,000-poll caller budget, case 0 reaches `RAM_STOP`, matching the hardware
stop. That failure is host-tested and hardware-observed.

The native flash model catches its existing nonreturning stop with `setjmp`;
the transcript explicitly distinguishes that observation from a caller
return. The target replay must reach the actual RAM self-loop, never the
ordinary caller FAULT loop. Complete CPU/RAM/flash snapshots must remain
identical while stopped and after the sole external change of FCTL to idle;
later idle cannot authorize return, cleanup, remapping or publication.
Mutation controls reject altered pending owners, RAM code, mapping, status,
media, bank depth and return markers. A diagnostic `--limit` invocation
never publishes completed acceptance, even if its limit includes every step.
The missing-key peer stays silent while the physical clock advances50000
symbols per WAIT_KEY poll (0.8 seconds); other running polls retain the
four-symbol delay. This supplies elapsed time, not a protocol deadline,
captured timestamp or private state. Actual production timing decisions
and all foreground/MMIO calls remain in the replay.

The unchanged admitted generic image has now executed all three new
failures from genuine reset:

| Case | Caller observations | Peripheral checkpoints | Observed peak SP | Actual terminal |
| --- | ---: | ---: | --- | --- |
| 8 | 411 | 53021 | `7B/7C` | Caller FAULT after genuine key timeout and cleanup |
| 9 | 23 | 7623 | `78/7C` | Caller FAULT, retained radio owner |
| 10 | 3 | 3254 | `6E/7C` | Nonreturning flash-RAM stop, retained after later idle |

Reports are `build/join-prefix/generic/join-replay-8.json`,
`join-replay-9.json` and `join-replay-10.json`, with immutable image identities.
The malformed-mailbox
case2 also executes its actual FAULT loop atSP53; an otherwise complete
one-step `--limit 1` rerun correctly publishes no accepted report.
These are **simulated**, not hardware-observed or full Actions acceptance.
The remaining J5/J6 work includes LG whole-caller execution, the rest of the
generic admission/failure MCU corpus and dedicated full Actions acceptance;
the ten host cases alone do not close those gates.

Reproduction commands (from the repository root):

```sh
make BOARD=generic BUILD=build/join-integration/generic test-join-smoke-host
make BOARD=generic BUILD=build/join-integration/generic prepare-join-smoke-layout
make BOARD=generic BUILD=build/join-integration/generic prepare-join-smoke-data
make BOARD=generic BUILD=build/join-integration/generic prepare-join-smoke-stack
make BOARD=generic BUILD=build/join-integration/generic prepare-join-smoke-image
```

Use `BOARD=lg_esl29_rev03` and a separate `BUILD` for the other board.
The second command writes `join-smoke-layout/layout.json`, raw map/CDB/NoICE
and immediately snapshotted listings with provisional DATA placement.
The third first prepares those artifacts, derives an object-bound
`data-placement.json`, relinks, then independently checks the new actual
instructions. Its `analysis.json` binds the result to the exact resource
report; a failed analysis removes any previous analysis report.
Only that separate successful strict analysis sets `DATA_liveness_verified`;
the resource report never grants it. The fourth command retains that DATA
gate and adds `static_stack_verified` with root bounds and a deepest path in
`analysis.json`. `stack_verified`, `accepted_image`, `simulated` and
`hardware_observed` remain false: static bounds do not authorize execution.
`prepare-join-smoke-image` additionally requires the separately pinned
immutable artifacts and XDATA ownership proof, then emits `admission.json`
and the exact150-byte public admission serializer used by the native
transcript. Failed admission removes stale report/header output. The
generated serializer contains no private protocol-progress fields.
`join_smoke_unverified.ihx` remains **not a programming image**. Only the
explicit offline replay consumes an admitted image; no packing, device
access or flashing target consumes it.

The replay needs the isolated debugger repair described in
[provenance](PROVENANCE.md). With Python3.12+, GNU make/g++, bison, flex and
m4 available, fetch the public archive separately, then prepare a fresh
ignored directory (no system installation):

```sh
curl -fL https://deb.debian.org/debian/pool/main/s/sdcc/sdcc_4.2.0+dfsg.orig.tar.xz \
  -o build/sdcc_4.2.0+dfsg.orig.tar.xz
python3 -B tools/prepare_join_simulator.py \
  --archive build/sdcc_4.2.0+dfsg.orig.tar.xz --output build/join-simulator
make BOARD=generic BUILD=build/join-integration/generic \
  S51=build/join-simulator/sdcc-4.2.0+dfsg/sim/ucsim/s51.src/s51 test-join-smoke-mcu
```

This opt-in target is not yet an accepted CI worker. The runner compares
native/nonrecovering-sanitizer peripheral transcripts, starts the genuine
image at reset and restores only previously observed complete MCU snapshots
between bounded processes. Every simulator process retains the15-second deadline;
chunks contain at most256 events, or128 during NV mapping/read loops.
Real DMA descriptors/inputs,
RAM-template bytes/PC/XMAP, FADDR/FCTL/FWDATA, NV media, JSN1 output,
stack/canaries and unowned RAM/flash are observed. Identical logical PCs in
different banks/XMAP share one OR-conditioned breakpoint, rather than
silently losing one breakpoint. Public synthetic peripheral/media inputs
cannot write CPU registers or private progress. `--limit` is diagnostic
only and never writes a complete replay report.
The complete instruction reader also accepts SDCC's adjacent `[24]10687`
cycle/line columns. The older fixture reader omitted such five-digit tail
lines, hiding reconfiguration accesses; the new caller explicitly verifies
the third indexed register-write sequence. The attempt/handoff reader now
also reuses the existing complete listing parser: a longer radio function
otherwise hid both real handoff writes beyond listing line10000.

`test-join-smoke-mcu` selects all ten cases. Use
`test-join-smoke-mcu-8`, `test-join-smoke-mcu-9` or
`test-join-smoke-mcu-10` for an individual new failure, keeping the same
`BOARD`, separate `BUILD` and explicit `S51` settings. Each target retains
immutable admission, native/sanitizer transcript comparison, genuine reset,
alias/stack checks and the15-second per-process deadline. Reports retain
the real terminal kind (`ready`, `fault` or `flash-ram-stop`); a busy flash
case must not be reported as a completed commissioning operation.

### Explicit ordinary-join profile

`JOIN_SMOKE_KEY_MODE=default-tc` is a separate discovery/security build, not
an install-code fallback. It scans before provisioning, accepts a contextual
short-coordinator Association Response, learns network/TC metadata from RF
inputs and then performs the real key exchange. The operator supplies only
the device's own IEEE, scan/policy bounds and qualified backoff draws, not
the target channel/PAN/Extended PAN/TC or Network Key. See the
[exact supported sequence and current evidence](ED_JOIN.md#ordinary-default-key-discovery).
Default-profile case10 now stops inside provisioning after association,
retaining the bound driver at stage4; it must not use the original
install-code case's pre-radio stage1 expectation.

The first LG discovery image completed a strict601-observation MCU replay
with156100 peripheral stops and SP7B after isolating external-peer AES state.
Subsequent physical runs exposed real RSSI-only shutdown and reserved FIFO-bit
defects before association; the [dated record](ED_JOIN.md#2026-09-28-lg-physical-discovery-trials)
distinguishes those failures and later image revisions. At power05, a repeat
with whole-network ZHA admission completed all16 channels and retained two
permitting routers but no eligible direct coordinator. A subsequent D5 run
selected a direct coordinator and received a matching CRC-good Association
Request ACK, but its conservative timing interval remained uncertain and
association correctly failed before response extraction or key provisioning.
Another D5 run retained an unresolved timer-entry preflight error. These observations
do not establish HA membership or close J6/full Actions acceptance.
The subsequent one-latch attempt sampler preserves the54-symbol ACK window
and full owner checks. Its first physical run hit candidate overflow after
finding a coordinator and routers; a coordinator-only-admission repeat stopped
on an unresolved timer preflight rejection on channel15. Neither exercised
association, so the targeted host/MCU improvement is not yet a physical
ACK-timing or authenticated-join result.

The diagnostic image adds only two ordinary-XDATA bytes, retaining7634/7680
and the same SP7C/bank-depth8 limits. Its first physical run again overflowed
the candidate table after a complete scan. A fresh explicitly coordinator-only
run retained the original failed RFERRF read: guard14/value04, phase1/polls0,
on channel15. This establishes RX FIFO overflow for that run, not a Timer2
counter failure or the exact cause of the older unlocalized faults.
RX service latency and explicit loss/recovery handling remain repair work;
clearing the flag alone cannot justify scan coverage or ACK success.
Both runs stopped before association; HA admission was closed and the MCU
fully reset/halted. J6, physical authenticated joining and full Actions
acceptance remain open.
