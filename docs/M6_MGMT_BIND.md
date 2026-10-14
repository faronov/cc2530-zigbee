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
ZDO receives `0x0033` through the same admitted application route as Bind/
Unbind; `zdo_srv_handle()` is not given a mutable duplicate table. Its generic
status-only fallback remains appropriate when no application owner is installed.

## Wire and pages

Primary evidence: pinned Core R22, `05-3474-22`, SHA256
`991dd02b7e5764ac349c47d5c4cf0fbe01529ff6594df03e8dad3d3d4df9e274`,
section 2.4.3.3.4/Table 2-83, printed p.125, and section 2.4.4.4.4/
Tables 2-133/134, pp.186-188. Only functional facts are reproduced here.

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

Unicast plain APS Data/profile0/destination endpoint0 is required. Replies
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

The bounded handler and initializer passed 77,785 focused checks in native
and nonrecovering ASan/UBSan, covering all live masks, real Bind/Unbind,
repetitions, wrong identities/endpoints/modes/clusters, pages, exact and short
capacities, busy slot, wrong context, malformed/trailing input and read-only
state. Genuine native same-parent/alternate-parent cases16/18 pass identity
checks against VERIFIED durable TC and device identity.

Initial LLVM FAST: 10/10 PASS, CODE224627 (+999 vs accepted M6), common19001
(+57), raw XDATA5220 unchanged, physical static-frame pool217 unchanged.
Bank free bytes1..7: 0/0/0/0/2/3482/14122; every placement/stack/ABI/
fixed-address admission remains enabled. MCU/SMOKE/final FULL evidence is
pending; host success is not a whole-MCU claim.

Confirm-Key remains C -- UNRESOLVED. No compiler/HWLab/hardware, LTO, NV schema,
production defaults or main merge changes are authorized by this slice.
