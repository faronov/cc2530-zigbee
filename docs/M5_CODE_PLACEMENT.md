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
(3.6.1.4.2), and BDB v3.0.1 `16-02828-012`, pp.39-40 (7.1).
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
