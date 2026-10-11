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

The explicit `parent-recovery` build variant prepares a 144-byte K3 payload
inside the existing journal, without implementing alternate-parent admission.
The ordinary image remains byte-identical to the relocation image:
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
