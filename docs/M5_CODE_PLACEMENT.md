# M5 CODE placement and recovery follow-up

## Baseline and physical limits

The feature baseline is `6f0ad787e4c91c3795a692723cc4be897373be59`.
FULL run 38087499539 tested firmware `2366e48`, HEX SHA256
`c19f9ed67efc75c79f29e229235123c96547776902681bca2072286d5bc0160f`.
The subsequent summary-only commit did not change that firmware.

The selected compiler is `1a01afb49d99ec754e62ab0f65f182de87b4cee3`.
The local compiler identifies as `2feda4a4`; its compiler/runtime/lld sources
are identical to the pin, whose additional commit changes CI only. Final CI
must use the exact requested commit, not substitute the local identity.

Each switchable bank has a 32 KiB execution window at `0x8000..0xffff`.
Banks have distinct physical load addresses. Bank 7 has only 26,624 usable
bytes: physical CODE ends at `0x3e800`, before the unchanged NV/lock partition.
Common CODE, startup, flash-RAM operations, constant data and initial XDATA
load images use physical bank 0. Writable XDATA and overlaid static frames
are not moved with CODE.

## Why banks 1-6 were full

The emitted objects contain individual `.text.<function>` sections, not
indivisible BDB/MAC/NWK groups. LLD's
`distributeMCS51AutoBankSections` sorts eligible sections largest-first and
fills the first bank with space. All six full banks contain exactly 32,768
input/function bytes: **no padding or hidden free space**.

These are real occupied bytes but not fixed-bank constraints. The last 26
small functions occupy only 368 bytes of bank 7. Additional movable functions
already spill there without changing the linker. The upstream distributor
assumes a full 32 KiB bank 7; the existing project assertion and independent
image proofs enforce its smaller physical limit.

| Bank | Input functions | Occupied bytes | Largest functions (bytes) |
| --- | ---: | ---: | --- |
| 1 | 6 | 32768 | `mac_join_step` 8225; private `step` 7789; `mac_link_driver_step` 6909; `bdb_join_step` 5952 |
| 2 | 9 | 32768 | `security_keys_receive` 5190; BDB `runtime` 4416; `mac_tx_interval_step` 4313; `zcl_sensor_serve` 3927 |
| 3 | 17 | 32768 | `operate` 2676; `mac_adapter_step` 2631; `ed_wire_crypt` 2616; `observe` 2426 |
| 4 | 31 | 32768 | `mac_tx_submit` 1489; private `operate` 1483/1441; `mac_tx_interval_submit_staged` 1437 |
| 5 | 65 | 32768 | `zdo_node_rsp_encode` 762; private `inspect` 752; `select_record` 744 |
| 6 | 213 | 32768 | `read_nwk` 334; private `commit` 332; `security_counter_save` 331 |
| 7 | 26 | 368 | `radio_autoack_acquire` 28; `load_control`/`save_control` 27 |

Static/private names above are distinguished by their input object in the
linker map. Root-pinned functions and constants are not relocation candidates.
Function-pointer/thunk targets must retain their compiler-generated entry
protocol; changing a linker output address alone cannot establish that proof.

## Small relocation

Two existing BDB functions, `bdb_join_step` and private `runtime`, use an
explicit Clang `.bank7.text.bdb` section. SDCC and host compilation are
unchanged. The compiler emits the normal bank-call entries and LLD resolves
the actual targets. No runtime algorithm, protocol value, memory ceiling,
bank-switch routine or compiler source changes.

This is a headroom distribution, **not** a 10 KiB code-size saving. LLD
re-packs the remaining functions; merely moving bytes does not increase
aggregate physical flash capacity.

| Resource | Before | After | Delta |
| --- | ---: | ---: | ---: |
| CODE | 215742 | 215731 | -11 |
| Common CODE | 18766 | 18755 | -11 |
| Bank 1 | 32768 | 32768 | 0 |
| Bank 2 | 32768 | 32768 | 0 |
| Bank 3 | 32768 | 32767 | -1 |
| Bank 4 | 32768 | 32766 | -2 |
| Bank 5 | 32768 | 32768 | 0 |
| Bank 6 | 32768 | 22771 | -9997 |
| Bank 7 | 368 | 10368 | +10000 |
| Bank 6 free | 0 | 9997 | +9997 |
| Bank 7 free | 26256 | 16256 | -10000 |
| Aggregate bank free | 26256 | 26256 | 0 |
| XDATA bss/data | 4843/35 | 4843/35 | 0 |
| Static-frame pool | 203 | 203 | 0 |
| Static stack | 106/128 | 106/128 | 0 |

FAST passed 11/11 and SMOKE passed 17/17 on this relocation. The existing
audits checked actual bank-call targets/returns, stack arguments, fixed
addresses/SFRs, static-frame ownership, generic pointers, and physical
CODE/NV limits. No proof was relaxed. FULL is reserved for the final
integration, including every existing recovery scenario and the current
summary script.

## Protocol review before alternate parents

Primary evidence: Core R22 `05-3474-22`, printed pp.341-344
(3.6.1.4.2), pp.306-308 (3.4.6-7), and BDB v3.0.1 `16-02828-012`,
pp.39-40 (7.1).
Downloaded documents are evidence only and are not committed.

- A secure rejoin can use a router parent without migrating the Trust Center.
  The parent IEEE/short address and NWK replay history are not the TC identity
  or its APS link key/replay history.
- Active scan must match the saved Extended PAN and suitable capacity/update
  information. Association Permit is not required for secure rejoin.
- The stochastic-addressing rejoin capability must clear Allocate Address
  for the retained valid address; initial association capabilities need not
  change.
- Parent information is cleared before the request. ED Timeout then supplies
  the new parent's information; stale EDI state must not leak into rejoin.
- `aResponseWaitTime` is measured after successful MAC transmission. The
  existing 30720-symbol constant uses the IEEE2006 value selected by this
  project; response delivery is not assumed instantaneous.
- An authenticated refusal is distinct from silence and removes that
  potential parent rather than sending three requests to it.
- R22 checks the most recently requested parent's IEEE identity, including
  delayed responses. Same-parent delayed protected responses need not be
  rejected solely because one attempt's wait expired.
- BDB requires an announcement after successful rejoin, not fresh initial
  commissioning or a permit-join broadcast.

The previous coordinator-only subset correctly protected the narrow
identity but did not implement scan, alternate-parent selection or refusal
semantics. Its 112-byte key payload fills the existing 128-byte journal
snapshot. A versioned, counter-preserving durable extension for one router
parent was explicitly approved for this follow-up. It must preserve both
TC NWK key-slot floors, APS history and retired credentials; it must not
overwrite `tc_ieee` with a discovered router or share/reset replay floors.

The same-parent follow-up implements the capability/parent-information,
MAC-success response-wait and authenticated-refusal corrections above.
Statuses1/2 with assigned addressFFFF produce a durably replay-protected
REFUSED event, not CONTEXT/silence. The controller stops and reports the
appended `BDB_JOIN_REJOIN_REFUSED` result without another request to that
parent. Existing numeric diagnostic values and silent-parent case17 are
preserved. Focused native/sanitized BDB tests cover both refusals, malformed
status/address, late response, reset, counter exhaustion and all456 flash
cut positions. The key-owner tests also prove refusal replays are rejected
across another reset.

There is a specification/identity-resolution issue to resolve before the
unknown-router slice: R22 3.4.6.2 p307 qualifies the destination IEEE by
"if known", whereas 3.6.1.4.2 p342 requires both IEEE fields and validation
against the most recently selected parent's IEEE. The ordinary15-byte
NWK beacon carries Extended PAN, depth/capacity/update, **not router IEEE**;
a short-source MAC beacon therefore does not supply both addresses.
The current candidate table stores one MAC source mode/address, and its
ZDO address client is fixed to TC0. Extended PAN is never a substitute for
parent IEEE. A known, independently bound router roster and unknown-parent
address-resolution/admission are different scopes, not interchangeable
synthetic beacon fixtures. No unknown identity or replay floor is invented
by this follow-up.

Corrected same-parent candidate: FAST 11/11, SMOKE 17/17; native and
sanitized BDB 127356 checks, ordinary key owner 7388 checks, optional storage
8334 checks. Fresh host transcripts retain case0's1142 steps and cold-reset
cases16/17; generating those transcripts is not MCU recovery acceptance.
No new FULL has been claimed. Compared with the relocation commit:

| Resource | Corrected candidate | Delta |
| --- | ---: | ---: |
| Populated/common CODE | 215815/18766 | +84/+11 |
| Bank 6 used/free | 22819/9949 | +48/-48 |
| Bank 7 used/free | 10391/16233 | +23/-23 |
| XDATA bss/data | 4843/35 | 0/0 |
| Static-frame pool | 203 | 0 |
| Static stack | 106/128 | 0 |

The candidate HEX SHA256 is
`6c8a0e79cce373cd8cb78b65315a8474aed69a61978729023dc384990b9acc45`
in both FAST and SMOKE. All linked bank/relocation/fixed/SFR/NV, frame
ownership and stack proofs remain strict. The SDCC production composition
was already infeasible at the earlier baseline (+725 bytes in its selected
layout); the Clang-only relocation and LLVM acceptance do not qualify SDCC.
There is no hardware authorization, frozen-artifact change or HWLab change.

## Durable-storage prerequisite evidence

At prerequisite commit `d05e938`, the explicit `parent-recovery` build variant
prepared a 144-byte K3 payload inside the existing journal, without alternate-parent
admission. That commit's ordinary image remained byte-identical to the relocation image:
HEX SHA256 `3423626d1591da04467202ce5b962aa8df91e7b10090a9a64f8a2459436cd82e`.

| Resource | Relocation/ordinary | Extended storage | Delta |
| --- | ---: | ---: | ---: |
| Populated CODE | 215731 | 215848 | +117 |
| Common CODE | 18755 | 18755 | 0 |
| XDATA bss/data | 4843/35 | 4907/35 | +64/0 |
| Bank 6 used/free | 22771/9997 | 22886/9882 | +115/-115 |
| Bank 7 used/free | 10368/16256 | 10368/16256 | 0 |
| Static-frame pool | 203 | 203 | 0 |
| Static stack | 106/128 | 106/128 | 0 |

Extended-storage FAST passed 11/11 and SMOKE passed 17/17. The refreshed
FAST and SMOKE firmware HEX hashes both equal
`c84542b0d78dbe20e1359b030af7e135a1fd662c14e30c73758cdb6ad658482a`;
the receipts bind different source snapshots, not a reusable FULL result.
Migration itself is covered natively/sanitized: 8304 checks, including 138
flash-command cuts (134 explicit fail-closed journal outcomes), VERIFIED
and REJOINING resets, no LEFT revival and retained snapshot length on later
crypto saves. Its target function is linked and image-checked but is not
yet called by the board recovery lifecycle or proved by a new MCU scenario.
There is no fresh FULL or hardware authorization at this prerequisite.

## Integrated known-router candidate

The following evidence supersedes the prerequisite-only scope above. The
optional profile implements the explicitly selected **known-router-first**
slice. One independently trusted router IEEE/short/key-slot replay history
is registered through the real key/NV owner, without changing TC identity
or admitting network membership. The ordinary profile keeps K2 and its
existing public admission/status layout.

Actual controller progression is saved-parent RECOVERING, drained failure
or authenticated refusal, confirmed INSTALL, bounded same-network active
scan, released scan lease, durable ATTEMPT selection, secure router rejoin,
atomic ACTIVE admission, INSTALL, Device Announce, ED Timeout and READY.
The current scan is limited to the saved PAN/channel/Extended PAN and the
known router identity. Association Permit is not required for rejoin;
profile/capacity, non-stale update and rejoin-specific minimum depth still
apply. No unknown IEEE or replay floor is derived from a beacon.

An authenticated late response now retires a queued genuine Rejoin Request
before that retry can reseal VERIFIED state. This correction also changes
the ordinary image: previous ordinary HEX/FULL evidence is not reused.
The radio owner's saved short address changes only after successful INSTALL,
so subsequent scan restoration uses the actual confirmed address.

### Linked resources

Local FAST/SMOKE image measurements use compiler `2feda4a4`, not the exact
requested CI pin. Executable CODE below excludes initial XDATA load bytes;
populated HEX includes them and other image bytes.

| Resource | Follow-up baseline | Current ordinary | Current known-router |
| --- | ---: | ---: | ---: |
| Executable CODE | 215742 | 215977 | 222717 |
| Common CODE | 18766 | 18766 | 18944 |
| Populated HEX | not recorded here | 216157 | 222906 |
| XDATA bss/data | 4843/35 | 4843/35 | 4951/44 |
| Overlaid static-frame pool | 203 | 203 | 217 |
| Static frames / unoverlaid bytes | not recorded here | 218/1707 | 223/1762 |
| Raw XDATA, including frames | not recorded here | 5081 | 5212 |
| Static stack demand | 106/128 | 106/128 | 106/128 |

The optional image adds 6975 executable CODE bytes and 178 common bytes
against the follow-up baseline. Against the current ordinary image it adds
6740 CODE bytes, 108 XDATA bss bytes, 9 data bytes and 14 frame-pool bytes.
No DATA/IDATA globals were added; the existing register/pseudo-register
and hardware-stack ownership remains unchanged.

| Bank | Current ordinary used/free | Known-router used/free | Largest current input sections |
| --- | ---: | ---: | --- |
| 1 | 32768/0 | 32768/0 | `mac_join_step` 8225; `mac_poll` private `step` 7789; `mac_link_driver_step` 6946; `security_keys_receive` 5750 |
| 2 | 32768/0 | 32768/0 | `mac_tx_interval_step` 4313; `mac_tx_step` 3897; `aes128_encrypt_block` 3820; `nwk_aps_receive` 3417 |
| 3 | 32768/0 | 32768/0 | driver `observe` 2426; `mac_join_start` 2321; `mac_poll_start` 2279 |
| 4 | 32766/2 | 32768/0 | `mac_scan_start` 1408; `nwk_parent_select` 1375; `security_keys_remember_parent` 1246 |
| 5 | 32768/0 | 32768/0 | `child_work_address` 681; `bdb_join_resume` 679; `flash_nv_read` 675 |
| 6 | 22982/9786 | 27431/5337 | `mac_tx_copy`/`mac_attempt_prepare` 293 each; `header_shape` 292 |
| 7 | 10391/16233 | 12502/14122 | BDB explicit group: `bdb_join_step` and private `runtime`, 12502 total |

Banks1-5 still pack real section bytes exactly, with no alignment slack.
Bank7's remaining 14122 bytes are physically usable before unchanged NV,
not the fictitious full-32-KiB margin. Aggregate switchable slack is 19459;
the minimum per-bank headroom remains zero. Further automatically banked
functions can spill into banks6/7, but the largest new indivisible section
must fit a real available interval.

Common/root sections remain explicit: `mac_scan_step` 5114,
`zdo_runtime_step` 5457 and flash-RAM command/idle group 1379 bytes, plus
the existing startup, constants and bank/runtime entries. They were not
moved without their separate call/static-frame/fixed-address proofs.
The complete input/output map is generated at `fw/link/hw1.map`; both
FAST and SMOKE retain bank, relocation, runtime, SFR, frame/alias and stack
audits. The current image has zero incorrect thunks and no populated bytes
at or above physical NV `0x3e800`. Status remains `0x1e00..0x1e2f`;
IRAM alias `0x1f00..0x1fff` is excluded. RX_DIAG remains independent.

Known-router HEX SHA256:
`c3e356c86d2a14822c1ab21a8d822d409358a0c683def0901e57a64526b7f7e5`.
Current ordinary HEX SHA256:
`1bb3925fef33c66eb54130dc7e86f285add0eeda7590e962c84a11da896f236a`.
These identify linked local candidates, not a new FULL receipt.

### Focused evidence and remaining acceptance

Native and sanitizer results agree: optional key owner12500 checks,
BDB146810 checks and candidate selection134383 cases. Ordinary key
owner7388 and BDB127785 checks remain passing. Migration retains138
flash-command cut positions/134 explicit storage failures; router
registration/selection/request/admission adds690/670, while the existing
BDB recovery cuts456/440 remain intact. Wrong network/key/identity, refusal,
NO_ACK/CCA, delayed/duplicate responses, selected/request reset, exhausted
or stopped-clock queues, overall discovery expiry, ED Timeout loss, Leave
and counter exhaustion have bounded fail-closed coverage.

All15 current FULL vectors match byte-for-byte between native and sanitizer
executions. Cold NV equality is checked for case0 ->16/17 and case20 ->18/19.
Case20 registers trusted synthetic history through target C after initial
authenticated join; cases18/19 cold boot only that durable flash. No host
READY, selected parent or final recovery state is injected. The separate
20-byte optional input leaves admission150/status48 layouts unchanged.
FULL now requires every original scenario plus20/18/19 and independent
terminal and measured-stack checks, for28 steps.

The final optional source/acceptance snapshot passed local FAST11/11 in21.2s
and SMOKE17/17 in290.2s, both identity `1b77782e871b1fe7` and the same HEX
above. Receipts are `build/m5-final-admission/{fast,smoke}/receipt.json`.
Ordinary FAST11/11 also passed; its12 current FULL vectors match native/
sanitizer executions. Final exact-pin MCU/FULL and current measured
stack peak are still pending; the previous101-byte peak belongs only to the
previous qualified image. Native transcripts and SMOKE's case0 prefix
are not new recovery MCU qualification.

SDCC compiles the optional and ordinary workspace/key/controller/caller
objects with exact source-owned size assertions. The fresh optional complete
resource composition fails `prepare-join-smoke-layout` before producing an
admitted map/image: switchable CODE groups total231115 against223232 usable
bytes, an7883-byte lower-bound excess. Its indivisible `JS_BDB` group is37567
bytes, also exceeding one32768-byte window by4799. Common CSEG alone is30934
before subsequent runtime/link admission. These are current object
measurements, not the earlier725-byte selected-layout excess. Merely splitting
the group cannot remove the aggregate excess. No artifact pin or resource
proof was relaxed; SDCC full-image compatibility remains blocked.

### Real bounded-service gap

Each admitted router-protected frame durably saves that sender's independent
replay floor before publication. Longer serving reaches the unchanged
32 erase-attempts/page/boot guard and fails closed with STORAGE. Case18
therefore proves a deliberately bounded ten-second foreground recovery/report/
protected-input exercise, not sustainable alternate-router operation.
The original case0/16 serving corpus is unchanged. Raising the guard or
silently making router floors volatile is not a solution.

The next prerequisite is a replay-safe durable wear/traffic policy with
power-cut and reset proofs, followed by sustained router-service acceptance.
Unknown-router IEEE learning, multiple-router history/eviction, PAN/channel
change, newly admitted router loss, TC migration, sleepy operation, IEEE2015
timing reconciliation and BDB errata remain separate gaps. No hardware trial
is admitted by this work, and HWLab/frozen images remain unchanged.
