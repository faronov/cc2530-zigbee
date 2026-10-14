# M6 bounded live Mgmt_Bind

Development branch: `m6-mgmt-bind`, from accepted firmware
`28bbafb352cd511e54e369296a02be897c448637`. Compiler remains
`1a01afb49d99ec754e62ab0f65f182de87b4cee3`. The accepted reporting-read FULL
[38460876079](https://github.com/faronov/cc2530-zigbee/actions/runs/38460876079)
and its artifacts are separate, unchanged evidence. This feature is not yet
FULL accepted.

## Identity and ownership

`zcl_sensor_init()` previously copied `record.association.source_ieee`.
That is the actual Association Response source: it identifies the discovered
TC only within the existing direct-TC initial-commissioning profile. Cold
resume has no Association Response record. Known-router rejoin selects a
different MAC parent without changing the logical coordinator/TC.
`config.association.extraction.coordinator` can also hold a short selected
parent address, not a TC IEEE. Neither field is a safe recovery identity.

The caller now refreshes the genuine security owner's existing status snapshot,
requires VERIFIED membership, and supplies its disjoint `config` to the
application initializer. The initializer validates source IEEE against the
admitted application identity and copies the durable TC IEEE into the existing
eight-byte `ST.tc_ieee`. No identity is derived from the Extended PAN ID,
short address or parent/router. Failed initialization preserves state and
stops the caller explicitly. K2/K3 and counter reservations are unchanged.

The sole binding owner remains `ST.bound[3]`: Temperature, Humidity, Power.
Power's two attributes share one cluster binding. A nonzero endpoint denotes
one current extended-address destination at the fixed logical TC, NWK address
zero. READY installs three local demonstration defaults at endpoint one;
these are actual report destinations, **not evidence of external Bind**.
An explicit Bind of the same tuple is idempotent; a different endpoint needs
Unbind first. Zero means absent. There is no origin bit, multi-destination
database, group binding or persistent application table.

`zcl_sensor_binding_page()` is an explicit bounded read-only owner view.
Mgmt_Bind dispatch precedes normal report-completion/timer bookkeeping:
it cannot consume `sent`, reset last/age, or advance an application timer.
The next ordinary poll still performs the existing bookkeeping and catch-up.
ZDO receives `0x0033` through the same admitted application route as Bind/
Unbind; `zdo_srv_handle()` is not given a mutable duplicate table. Its generic
status-only fallback remains appropriate when no application owner is installed.

## Wire and pages

Primary evidence: pinned Core R22, `05-3474-22`, SHA256
`991dd02b7e5764ac349c47d5c4cf0fbe01529ff6594df03e8dad3d3d4df9e274`,
section 2.4.3.3.4/Table 2-83, printed p.125, and section 2.4.4.4.4/
Tables 2-133/134, pp.186-188. Only functional facts are reproduced here.
Section 2.5.3, printed p.205, requires endpoint0 for both ends of ZDP
requests/responses and forbids ZDO fragmentation. The MCU peer uses endpoint0;
nonzero source endpoints are rejected, not treated as a valid ZDO client.

Request: TSN, one-byte StartIndex. Missing fields are discarded; trailing
bytes are ignored, matching the existing mandatory-prefix ZDO server policy.
Response `0x8033`: TSN, SUCCESS, total entries, start, returned count.
Each extended-address record is 21 bytes:
source IEEE[8], source endpoint, cluster LE16, mode03, destination IEEE[8],
destination endpoint. IEEE octets retain the owner's wire order.

Pages compress absent slots but preserve Temperature/Humidity/Power order.
Total is independent from returned count. Only complete entries fit the
82-byte actual application slot; the three-entry page is 68 bytes.
The owner API also tests smaller capacities: below five bytes is a nonmutating
error; five bytes can hold a zero-record prefix. Empty tables return all three
count/start fields zero, including a nonzero requested start.

For a nonempty table, boundary/beyond-end indices return SUCCESS, requested
start, zero records. The pinned management definition gives no INVALID_INDEX
status for this successfully obtained table; no such error is invented.
Unknown/invalid source or destination identity fails closed, never emitting a
fabricated record.

Unicast plain APS Data/profile0/source and destination endpoint0 is required. Replies
reverse NWK destination and APS destination endpoint, retain local endpoint0,
and echo TSN. Broadcast/group traffic, malformed context, received response
clusters and a busy response slot produce no management reply. The occupied
slot stays untouched and FULL remains explicit. Normal transport authentication,
replay completion, reservations and response sending stay with their owners.

## Current evidence

Test-only reproduction commit `0c3b5ef` first failed the actual runtime
Mgmt_Bind request in native/nonrecovering ASan/UBSan. Genuine cold-rejoin
case16 independently failed cached-TC equality against the real security/NV
owners in both builds, before firmware correction.

The bounded handler and initializer passed 77,800 focused checks in native
and nonrecovering ASan/UBSan, covering all live masks, real Bind/Unbind,
repetitions, wrong identities/endpoints/modes/clusters, pages, exact and short
capacities, busy slot, wrong context, malformed/trailing input and read-only
state, including a pending report and elapsed timer. That edge case independently
fails on the preceding implementation in both builds, then passes after moving
enumeration before bookkeeping. A nonzero ZDO source endpoint similarly fails
the new rejection regression before its correction.
Genuine native same-parent/alternate-parent cases16/18 pass identity
checks against VERIFIED durable TC and device identity.

Initial LLVM FAST: 10/10 PASS. Final integrated LLVM SMOKE: 16/16 PASS
(including all ten FAST gates), 272.9 seconds, on clean implementation
`d5bed19a47a4d75dc25d67462ab00456a05f2fb3` using the unchanged reference
simulator. Receipt seal:
`3b055e254687d83c713edcbfa3c778fc68f58fbe43c1fedab9998b45a9fa8641`.
Evidence source-tree digest:
`8fc14054acbee179707eb70d2ef24949f936045ca812ed8a920d90d0f7aa394e`.
Thirty tier/admission tests pass, including
negative completion/count/type/identity/owner-metric cases.
All 19 genuine native/nonrecovering-sanitizer scenario transcripts are byte
identical before the final endpoint-only tightening; affected case24 was then
regenerated and rechecked byte-identically with standard endpoint0 framing.
The full reference schemas and cold-NV lineage are checked.

Case24 adds 18 actual protected management transactions. Its native reference
has 1709 calls, 1113 application/identity observations, 414 pure-owner
no-mutation/no-flash checks, and one subsequent Humidity report to endpoint7.
It checks initial defaults, Unbind, live two-entry table, Bind to endpoint9,
idempotent Bind, compressed pages, boundary/beyond-end, removal, empty table,
and reinstallation of the sole remaining Humidity binding.
The peer inspects genuine TXFIFO/crypto output, never generates DUT responses.
Actual MCU identity/reporting/binding observations are derived from the exact
target compiler and bound to the ELF, including cold-recovery cases16/18/20.
The constant-only layout probe is never linked or used to write MCU state.
These are native reference results until genuine MCU replay completes.

Resources: executable CODE224728 (+1100 vs accepted M6), common19001
(+57), raw XDATA5220 unchanged, physical static-frame pool217 unchanged.
Ordinary XDATA BSS4959 and initialized DATA44 remain unchanged. Static stack
demand106/128; 379 correct thunks, zero incorrect; generic-pointer sites18.
Logical static-frame sum1777 (+16), 224 symbols (+1), with no physical-pool growth.
Bank used bytes1..7: 32768/32767/32768/32768/32768/29386/12502.
Bank free bytes1..7: 0/1/0/0/0/3382/14122; every placement/stack/ABI/
fixed-address admission remains enabled. Common physical HEX occupancy is19190:
19001 executable bytes,145 alignment/padding bytes and44 initialized data-load
bytes, leaving13578 of32768. The symbolic CODE+data sum19045 excludes padding.
Minimum local bank headroom remains zero; aggregate slack is not placement proof.
Development ELF SHA256:
`738fa2d980aed91efbc67399a0e7ebb1253ce92466a8bd96104835809d4d2c7a`.
HEX SHA256:
`798975f4171454fcfe54d56584ba4287806dbb4e9993f1fcb7c5c566fa173e25`.
Whole-MCU/final FULL evidence is pending; host success is not a whole-MCU claim.
The final selection has31 gates and all19 complete MCU scenarios, preserving
the previous18 and adding case24. Final runtime peak is not inferred from the
accepted predecessor's101/128 or from SMOKE prefixes.

Remaining limits are RAM-only fixed-TC destinations, no externally-configured
origin tracking, groups, multi-destination table or unknown-router discovery;
synthetic sensors/device ID and physical interoperability remain unchanged.
The recommended next bounded milestone is reporting TX completion/failure
and response-slot backpressure accounting, separate from persistence, security
commands and broadcast key updates.

Confirm-Key remains C -- UNRESOLVED. No compiler/HWLab/hardware, LTO, NV schema,
production defaults or main merge changes are authorized by this slice.
