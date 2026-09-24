# Fixed-parent key, replay and Trust Center owner (#23)

Original BSD-3-Clause code. This is an actual serialized foreground composition
of `ed_wire_crypt`, CCM*, AES, install-code/MMO/keyed-hash, outgoing counters,
the journal and flash services. There are no successful security/NV substitutes.
It is **host-tested and SDCC object-compiled, not target-image accepted,
simulated, hardware-observed or application-interoperability evidence**.
The genuine combined target link currently fails the unchanged limits below.
The owner is not linked into a board image and does not change existing
production services. The parent separately owns Makefile/workflow integration.

## Deliberately bounded profile

One receiver-on ED is directly associated with the centralized coordinator,
NWK short address `0000`, which is also its fixed parent, Trust Center and
network manager. One trusted provisioning config binds own IEEE, TC IEEE,
Extended PAN ID, PAN ID, own short address, channel and NWK Update ID.
IEEE octets are in wire order. Neither zero/all-FF IEEE identities nor
distributed-security identities are accepted. The short address can be
`FFFF` **only before association**, and cannot be zero or `FFF8..FFFE`.
Channel is 11..26; PAN ID cannot be `FFFF`.

Only one unique 16-octet install code plus its two CRC octets bootstraps the
TC link. No global/default key, legacy install-code lengths, other parent,
neighbor eviction, router forwarding, child admission, Trust Center server,
network formation or application link-key provisioning is implemented.
Even ordinary incoming traffic must originate at short `0000`, not just be
forwarded by it. Outgoing unicast is to that TC. Incoming broadcasts are
restricted to `FFFF` and awake-device `FFFD`. Outgoing APS broadcast Data
additionally permits `FFFC` (routers/coordinator), including the final BDB
Permit Joining request. This does **not** admit router-only `FFFC` on ED RX,
unicast-APS Data to `FFFC`, ACK-to-`FFFC`, or APS protection of broadcasts.
Other networking is explicitly
unsupported, not evidence of a general ED implementation.

This restriction is what permits **two NWK replay records for one sender**
and one operational APS replay record to fit the existing 112-byte opaque
counter payload. More neighbors cannot be added without a reviewed schema
and durable owner change.

## Public contract and state

`include/security_keys.h` is the API; `ccm_star_limits_t` supplies real
per-block timeout/poll limits and each mutation supplies real NV poll bounds.
Caller objects must be complete, disjoint, stable and ordinary storage,
excluding private/lower/libc/MMIO/status/IRAM-alias storage. Generic-pointer
inputs are not permission to use CODE/MMIO as writable output. Foreground
only: no callbacks, ISR entry, recursion, concurrent observers or nested use.

* `open()` really opens the outgoing-counter owner, reads its payload and
  validates the exact schema and phase invariants. EMPTY only establishes an
  unprovisioned state. Damaged/degraded/unknown/incoherent storage fails closed.
  No migration, automatic repair, creation, rollback bypass or reset exists.
* `provision(config, install_code18, nwk_floor, aps_floor, limits, nv_polls)`
  requires the caller's independent trusted authorization/history. It derives
  the actual install-code key and calls `security_counter_create`. Neither
  outgoing floor may be `FFFFFFFF`. An erased device is not automatically
  eligible, and BDB must not invoke this as EMPTY recovery.
* `associate(short_address, nv_polls)` binds genuine MAC-association results
  only in PROVISIONED. It implies **no Zigbee authentication**. If provisioning
  already supplied a short address, association must agree. Initial association
  explicitly clears parent information and Timeout Request context.
* `receive(raw_npdu, length, output, event, limits, nv_polls)` selects actual
  keys internally, verifies required MICs and identities/selectors, checks
  replay, and durably saves all admitted incoming floors and transitions
  **before** returning a packet/event. Initial unsecured NWK is accepted only
  for the APS-protected initial Network Transport-Key. All later traffic
  requires NWK security. No authenticated boolean is accepted from callers.
  The output event's low seven bits are the semantic event
  (`SECURITY_KEYS_EVENT_MASK=7F`). Bit `SECURITY_KEYS_EVENT_APS_SECURED=80`
  is set only when the admitted frame actually passed APS authentication,
  after durable save succeeds. The normalized packet's APS security bit stays
  clear. The upper transaction owner uses the event bit to select ACK
  protection; it is not an input authentication assertion.
* `request(nwk_seq, aps_counter, out, capacity, written, limits, nv_polls)`
  emits a real Request-Key (`08 04`), APS-encrypted with the operational old
  TC link key and NWK-encrypted with the active network key. REQUESTED is
  durable before output. Requests/retries are allowed in RECEIVED, REQUESTED
  or VERIFIED; no replacement of an outstanding provisional key.
* `verify(...)` emits `0F 04 || ownIEEE8 || HMAC03(pendingTC)16`, without APS
  encryption but inside a real NWK envelope. WAIT_CONFIRM is durable before
  output; retries in PROVISIONAL/WAIT_CONFIRM do not reset any counter.
  Both Request and Verify select AR=0: the normalized APS command header is
  `01, callerAPS_counter`. Retries reconstruct fresh frames with the supplied
  unchanged APS counter/new NWK sequence. Request consumes fresh APS and NWK
  security counters; Verify consumes a fresh NWK counter and has no APS
  security counter because it has no APS protection.
* `send(packet, aps_secure, ...)` always applies actual NWK security; optional
  APS protection of Data or full/short ACK uses only key-id 0 and the
  operational TC key. Arbitrary
  caller APS commands/transport descriptors are forbidden. Before initial
  verification, only endpoint-0/profile-0 Node Descriptor request/response,
  Device Announce and ACK traffic is allowed, plus the selected NWK management.
* `leave(nwk_seq, out, capacity, written, limits, nv_polls)` is an explicit
  local failed-join abandonment, not an input-authentication setter. With a
  stored network key it constructs and genuinely encrypts NWK Leave `04 00`
  using that active key and a newly consumed outgoing counter: destination
  `FFFD`, SourceIEEE present, DestinationIEEE absent, radius1, caller NWK
  sequence. The required capacity is exactly36 bytes. It then commits LEFT
  through `security_counter_save` before publishing the frame. Request,
  RemoveChildren and Rejoin are all zero.
  Without any network key, it commits LEFT without performing crypto or
  constructing a frame; success sets `written=0` and leaves `out` untouched.
  This is **quiet abandonment, never a claim that a packet was sent**.
  `out` and `written` are still required; capacity0 is valid for this quiet
  case. COLD/EMPTY, failed storage and invalid arguments never yield a
  successful abandonment. Neither path provisions, resets counters or
  clears durable identity/key/replay history.
* `status(&metadata)` copies config, phase, active sequence, valid-slot bits,
  newer-pending flag, `parent_information` (recognized low3 bits),
  `timeout_pending` (durable request intent), and a nonsecret result.
  Neither parent field is proof of delivery, a live deadline or BDB readiness.
  It does not export key material,
  incoming floors, lower secret caches or an authentication capability.

The key progression is:

```
PROVISIONED -> ASSOCIATED -> RECEIVED -> REQUESTED
 -> PROVISIONAL -> WAIT_CONFIRM -> VERIFIED
```

Transporting key B does **not** verify it. Key A remains operational until
a successful `10 00 04 || ownIEEE8` Confirm has actually decrypted under B
in the outstanding WAIT_CONFIRM phase. B is usable for **only that command**
before promotion, so it needs no independent persistent replay floor.
Its conceptual incoming floor starts at zero; successful Confirm consumes
its counter and promotion stores `received+1`. A's old incoming floor is
discarded only then. A becomes a retained reuse tombstone, never a fallback
decryption key. The old floor still protects all A-based derived-key traffic
while B is provisional.

All Transport-Key payload bytes are suppressed on successful receive:
`output.length=0`, all 82 payload bytes zero. Safe normalized headers and the
event (including its authenticated APS-security bit) remain for ACK/transaction handling. Other supported APS security
commands are also suppressed. No secret descriptor or hash is returned to ZDO.
All failures preserve packet, event, output bytes and `written`; consumed
outgoing counters remain consumed. Capacity failure can therefore burn counters
but cannot publish a partially encrypted frame or commit a local lifecycle
transition. Request needs exactly 47 bytes; Verify needs exactly 54.
With minimum NWK/APS Data headers, the 116-byte NPDU bound permits at most
82 data octets with NWK protection alone, or 65 with both layers protected.
Optional IEEE headers reduce those capacities. APS Data delivery and NWK
broadcast addressing must agree; ACKs cannot be sent to a broadcast address.
The selected extended-nonce auxiliary form is mandatory at both layers.
Protected ACKs have no plaintext payload. With the minimum NWK header,
full ACK needs exactly 51 NPDU bytes and short/command-format ACK exactly 45.
They retain caller-supplied APS correlation fields and consume fresh outgoing
security counters on every call. An initial Transport-Key can consequently be
ACKed using the old operational TC data key before TC verification, without
exposing its transported key or using its transport-derived key for the ACK.

Storage failures retain a FAILED state and first failure. Crypto failures are
explicit and inherit the real AES driver's retained failures; there is no
software fallback. The owner's private work record, keys, packet, plaintext
and ciphertext staging are volatile-wiped on every returning operational exit.
This does **not** erase counter/journal caches, flash history, lower AES/CCM
storage, compiler temporaries or hardware. It is not a secure-erasure claim.

### Upper-layer obligations and restart

A persisted VERIFIED key record is **not** evidence of a current association,
successful BDB rejoin, ED Timeout completion, parent keepalive or final
commissioning. The upper transport/BDB owner must continue to gate application
use on restart and commissioning. It owns finite transaction attempts,
timeouts, duplicate APS transactions and cancellation; stopping retries does
not reset or silently complete a key exchange.
In particular, BDB p46 steps11–14 put member=TRUE and broadcast Device Announce
after authenticated network-key receipt, **before** Node Descriptor/TC exchange;
final Permit Joining broadcast and commissioning success remain upper actions.
The owner permits that early announce but does not infer final BDB readiness.
For secured APS retries, each new `send` consumes fresh security counters while
leaving the caller's APS transaction counter unchanged (Core2.2.8.4.4 p59).
Do not replay an old secured NPDU as a fresh APS retry.

The owner permits a fixed-parent **secured** NWK Rejoin Request through
`send`: command `06`, awake ED capability `88` or `8C`, both IEEE addresses,
destination 0, radius 1 and the existing own short address. It persists
REJOINING before returning the encrypted request. Only a genuine secured
Rejoin Response `07 || newShortLE2 || status00` from the bound TC, to the old
short and both correct IEEE addresses, can persist the new short address and
return REJOINED. Failed/stale/unsolicited responses cannot change the binding.
Ordinary traffic is blocked in REJOINING. This does not implement parent
search, unsecured TC rejoin, key recovery or random address allocation.

The owner also handles authenticated NWK Leave and locally generated
secured Leave indications. Source IEEE and radius 1 are required. A request
is addressed to this ED; an indication is broadcast to `FFFD`. Local TX has
Request=0, RemoveChildren=0, reserved bits zero, SourceIEEE present and no
DestinationIEEE. LEFT is durable before publication and preserves key/counter
history, including an abandoned pending TC key as inactive history. Such a
key is not an outstanding Confirm context and is never promoted by Leave.
The generic packet/received leave's rejoin option is retained; the dedicated
failed-join `leave()` always clears it. Fixed-parent secured rejoin from LEFT
requires that option and a previously verified TC key. No automatic
reprovisioning/factory reset follows Leave. Rejoin/leave timing, retries and
radio cleanup remain upper-layer responsibilities.
The parent's initial BDB integration rejects persisted resume as
recovery-required; this local-abandonment API does not implement or authorize
#27 recovery. An abandoned pending exchange also blocks the existing narrow
rejoin path pending that separate review.

Repeated keyed `leave()` calls reconstruct fresh secured frames; no delivery
is inferred. Counter reservation can commit before the separate atomic
LEFT/history save, and every error/interruption preserves caller outputs.
Thus an interrupted attempt can retain the old phase with a burned outgoing
range, or committed LEFT without published bytes, but never publishes an
uncommitted demotion. A quiet LEFT record can have no NWK slots and retain
`FFFF` when association never completed. It cannot be associated or
reprovisioned automatically. Normal journal page turnover remains entirely
inside `security_counter`; no history-erasure path is introduced.

NWK ED Timeout Request TX (`0B, timeout0..14, configuration00`) and Response
RX (`0C, status0..1, parentInformation`) require both IEEE addresses,
unicast destination and radius 1. The request is sent with `aps_secure=0`
because it is a NWK command, not an APDU; genuine NWK protection is mandatory.
Both protected forms require exactly45 NPDU bytes with those headers.
Before publishing a genuinely encrypted outgoing `0B`, the owner durably sets
one outstanding-request bit. Retries reconstruct a new protected request and
retain that context without resetting incoming floors. A response requires
this outstanding context in addition to the real NWK MIC, source short0,
bound TC auxiliary/header IEEE, own short/IEEE, both IEEE fields and radius1.
An unsolicited response cannot establish parent information.
On status0, the owner stores `parentInformation & 07`; status1 clears the
outstanding bit without changing previously accepted parent information.
The consumed context, successful metadata update and incoming replay floor
are in the same atomic journal payload save before publication.
An admitted response returns the entire three-byte nonsecret payload with
event exactly `SECURITY_KEYS_EVENT_MANAGEMENT` (no APS-secured bit), after
that save succeeds. It does not modify key verification or application readiness.
The reverse directions
are unsupported in this ED-only owner.
Parent-information bits, including reserved bits and MAC-poll-only values,
are passed through unchanged in the packet to the upper procedure, while only
recognized bits0..2 are persisted. Core Table3-55 assigns bit0 to MAC Data Poll
keepalive, bit1 to Timeout Request keepalive and bit2 to power negotiation;
storing a capability bit does not implement power negotiation or MAC polling.
The selected upper BDB procedure requires bit1 (Timeout Request
keepalive support), explicitly rejects MAC-poll-only parents, and matches
its outstanding request, deadline, addresses and status before accepting
the parent information. Repeated Timeout Request sends consume fresh NWK
security counters; the upper procedure owns scheduling and retry bounds.
Persisted request intent is not proof that RF transmission occurred or that
a response arrived in the current timed transaction. Restart remains
recovery-required in the parent integration.

For **every outgoing NPDU**, the owner clears the caller's End Device
Initiator assertion and then sets EDI iff persisted `parent_information != 0`,
before authenticating the header. This includes Timeout Request retries,
ordinary Data/ACK, Request/Verify and Leave. A successful response containing
only reserved bits stores zero and cannot set EDI. Parent information survives
key changes and Leave as durable history; Leave clears outstanding Timeout
context, not that history. Core3.3.1.1.9's EDI condition is a separate wire
requirement from the upper profile's bit1/readiness test.
The owner also admits bounded NWK Network Status bodies as
already-authenticated management; command semantics remain the upper handler.
Keepalive scheduling and endpoint-0 responses remain required upper-layer work.
After TC-key verification, ordinary ZDO/application Data and full/short ACK
pass through within the fixed-peer profile; transport still gates BDB
application readiness. Before verification, the earlier join-critical filter
continues to apply, so final Permit Joining is not prematurely enabled.

### Network-key and identity changes

Two valid NWK slots have explicit chronology: active index and a bit saying
the alternate is newer. Sequence `255 -> 0` is legal. The next transported
sequence must be the immediate successor of the most recently installed
entry, not a guessed half-range ordering. Same-sequence/same-material
transport is idempotent and **never resets replay**. Conflicting selectors,
known reused material and known old keys under new selectors are rejected.
The bounded tombstones detect resident/most recently retired reuse, not
arbitrary historical reuse after multiple replacements.

Normal authenticated network-key transport replaces the alternate. A valid
frame authenticated with that newer entry automatically adopts it; old-slot
traffic can still be received, but never switches the sender back. A TC
Switch-Key must be NWK-protected and broadcast, naming the current or newer
stored entry. Optional APS protection must be key-id 0. This profile requires
APS protection on **all** Network Transport-Key reception, including normal
updates, and currently supports their own-IEEE/unicast descriptor form only.
The Core-permitted NWK-only broadcast transport exception is not implemented.
Switch-Key's explicit Core NWK-protected broadcast rule is retained.

Authenticated NWK Network Update (`0A`) is handled internally, not through a
fabricated-authentication setter. The one-record PAN-ID update requires
13 payload bytes: command, options `01`, ExtendedPAN8, UpdateID, PAN-LE2;
NWK destination `FFFF`, SourceIEEE present, DestinationIEEE absent, origin
short 0 and matching fixed Extended PAN. The existing strict half-range policy
is applied to `(receivedUpdateID - storedUpdateID) mod 256`: 1..127 is newer,
128 is ambiguous and rejected, 129..255 is stale and rejected. Equal ID plus
equal PAN is idempotent; equal ID with a different PAN is rejected. Idempotent
reception still durably advances the fresh incoming security counter, without
resetting any key, floor or phase. Channel migration and network-manager changes need separate reviewed handling;
there is no generic config mutation API.

## Durable 112-byte schema (version 2)

Offsets are zero-based within `security_counter`'s opaque payload. Multi-byte
integers are explicit little-endian. Native struct layout is never persisted.

| Offset | Length | Content |
| --- | ---: | --- |
| 0 | 8 | own IEEE |
| 8 | 8 | fixed TC/parent IEEE |
| 16,32 | 16 each | NWK slots 0,1 |
| 48 | 16 | operational old/current TC link key |
| 64 | 16 | pending TC key, most recent retired TC tombstone, or abandoned pending TC history in LEFT |
| 80,84 | 4 each | exclusive next-acceptable incoming NWK floor per slot |
| 88 | 4 | exclusive incoming APS floor for operational TC key and its derived keys |
| 92 | 8 | Extended PAN ID |
| 100 | 2 | PAN ID |
| 102 | 2 | own short address |
| 104,105 | 1 each | channel, NWK Update ID |
| 106,107 | 1 each | NWK slot sequences |
| 108 | 1 | bits: valid0, valid1, active1, alternate-newer, retired-TC, previously-verified, leave-rejoin-allowed; bit7 reserved zero |
| 109 | 1 | bits0..3: persisted phase (public enum); bits4..6: parent-information bits0..2; bit7: outstanding Timeout Request |
| 110,111 | 1 each | magic `4B`, schema version `02` |

Absent slots/key staging and their floors/selectors are canonical zero.
The phase enum fits four bits; compile-time guards check that bound and the
unchanged112-byte payload. Before association/without a NWK key, parent bits
and outstanding context must be zero. LEFT/REJOINING cannot retain an
outstanding Timeout context. The earlier development version1 did not retain
parent information; `open` rejects it with FORMAT rather than guessing or
automatically migrating an authenticated fact. Recovery/migration is a
separate explicit gate, never EMPTY provisioning.
Incompatible phases, selectors, reserved flags, identities and layouts cause
`open` to fail closed rather than auto-migrate. `FFFFFFFF` is forbidden as an incoming wire
counter, but is a valid exhausted **exclusive stored floor** after admitting
`FFFFFFFE`. On receive, strict `< stored_floor` rejects duplicate and stale
frames. No outgoing domain ever resets, including TC promotion, NWK switch,
rejoin, PAN update or Leave. Every save burns unused outgoing reservations.

`security_counter` remains the sole NV writer. Its degraded-journal refusal,
runtime snapshot/generation checks, 32-erases/page/power-epoch bound and
fail-stop RAM executor are unchanged. Saving a floor for every admitted frame
is deliberately conservative and quickly reaches that bound; this is not a
production flash-endurance solution. Two pages do not detect coherent cold
rollback or total erasure without an external trusted history/anchor.

## Primary sources and policy distinctions

Functional facts were checked in the local primary PDFs; no implementation,
catalog or reference-agent text is imported. No R23/BDB 1.0/3.1 substitution.

* Core R22, **05-3474-22**, SHA-256
  `991dd02b7e5764ac349c47d5c4cf0fbe01529ff6594df03e8dad3d3d4df9e274`.
* PRO BDB v3.0.1, **16-02828-012**, SHA-256
  `16471aa230657818da4c8440671efb530d80c71a975fce7af71c507ca7aa17d3`.

Printed-page references:

| Decision | Primary clause/table |
| --- | --- |
| Exclusive incoming floor, MIC before plaintext, newer **entry** auto-adoption | Core 4.3.1.2 pp413–414; 4.4.1.2 p420 |
| APS data/request/confirm key-id00; Network Transport id02/hash00; TC Transport id03/hash02 | Core 4.4.1.1 p418; 4.5.3; existing keyed-hash contract |
| TC command source checks; required versus optional APS encryption | Core 4.4.1.3 Table4-6 p421; 4.4.2.3 p428 |
| Transport type1/type4 descriptors and source/destination binding | Core 4.4.10.1, Figures4-7/8/9 pp447–448 |
| Request `08 04`, no partner field; Switch `09 seq` | Core Table4-27 p447; 4.4.10.4–5 pp450–451 |
| Verify `0F 04 IEEE8 hash16`; purpose03 is not an encryption key | Core 4.4.10.7/Figure4-16 p452; B.1.4 |
| Confirm `10 status type IEEE8`, pending-key authentication | Core 4.4.10.8/Figure4-17 pp452–453; BDB10.2.5 pp75–79 |
| Retain A until genuine decrypted Confirm under B; reject same B/wrong key type; incoming reset for B | BDB10.2.5 steps9–13 pp78–79; no outgoing reset is chosen |
| Two NWK keys, sequential modulo256 update, broadcast protected Switch | Core 4.6.3.4 p467; 4.7.3.10.5–6 p482 |
| Leave options, addressing and radius | Core3.4.4, Figures3-16/17 pp303–304 |
| Secured fixed-parent rejoin/address response | Core3.4.6–7, Figures3-19/20 pp306–308 |
| Network Update exact PAN-ID form | Core3.4.10, Figures3-28/29/30/31 pp312–314 |
| ED Timeout request/response bodies and addressing | Core3.4.11–12 pp314–318, Tables3-52/53/54/55 |
| Parent-information recognized bits0..2 and ED Initiator iff parent information nonzero | Core Table3-55 p318; 3.3.1.1.9 p290 |
| Secured retransmission counter increase | Core2.2.8.4.4 p59 |
| Full/short ACK fields and AR=0; security sub-field owned by security provider | Core2.2.5.1.1 p46, 2.2.5.2.3 p50, 2.2.8.4.3 p58; key-id00 per4.4.1.1 p418 |
| Early Device Announce, then TC exchange, then final permit | BDB8.2 steps11–14 p46 |
| Directional broadcast groups: FFFC routers/coordinator, FFFD awake devices, FFFF all devices | Core3.6.5, Table3-69 pp382–383 |

Fixed identities, strict unicast key transport, strict half-range Update-ID
policy, known-material reuse rejection, persistent per-frame floors and
lifetime shared outgoing APS domain are explicit restrictive project policies,
not claims that all Zigbee devices must use this profile. Existing BDB errata,
test-plan and application-selection gates remain open.

## Focused evidence and remaining target gate

`tests/security_joint_model.c/.h` supplies one genuine host AES-DMA plus
flash controller. It dispatches real register hooks, checks DMA/flash exclusion,
programs actual synthetic 4096-byte NV, implements erase/program attempts and
before/after/torn power cuts using an explicit caller-owned `jmp_buf`.
`security_joint_reset(erase)` is a **modeled cold power cycle**, never a live
crypto/NV handoff. It clears the owner's phase/result and all private staging
through a host-only CPU-initialization helper, including an interrupted
operation's staging; the helper is absent from the public header and target
object. No API call resets the live AES/NV models. The one AES-model change routes generic pointer admission
through its existing hook; its default mapping/controller/vectors remain intact.
The combined synthetic generic address map is for admission checks, not a
target allocation/IRAM-alias proof. Native flash uses the existing executor
behavioral model; target copied-RAM opcodes still need linked execution.

Tests call the actual source services. Three complete encrypted golden NPDUs
(initial Transport, Request and Verify) were independently cross-checked using
Python `cryptography` AESCCM plus a separate MMO/HMAC construction; the tests
embed only the fixed public ciphertexts and add no runtime dependency.
They cover initial key admission,
Request/Transport/Verify/Confirm wire bodies and layering, provisional/pending
reboot, genuine old-key retention, wrong MIC/key/type/source/destination/status,
missing context, replay and exhausted floors, sequence wrap/explicit switch/
newer auto-adoption, suppressed secrets, exact capacities, every truncation
and trailing data, schema errors, updates, Leave and fixed-parent secured
rejoin, half-range Update-ID boundaries and idempotent replay-floor preservation,
early Device Announce, Request/Verify retries with fresh security counters and
unchanged APS counters, authenticated-only event flags, protected full/short
ACKs and exact ACK capacities, all 38 save-command cut points in three interruption modes, AES/NV
faults, RAM fail-stop and the unchanged erase budget. All identities/material
are public synthetic test data; tests do not print keys.
Dedicated failed-join Leave cases additionally check exact36-byte protected
frames, unbound/associated quiet abandonment, retained byte-for-byte durable
material/floors, no auto-create/association after LEFT, fresh-counter retries,
and before/after/torn cuts around both outgoing reservation and final LEFT save.
ED Timeout cases cover exact45-byte requests, fresh-counter retries, required
IEEE fields/radius, reserved request/status values, unchanged key phases,
unmodified parent-information bytes (including MAC-poll-only and reserved
bits), management-only events, replay/reboot and MIC/NV failure atomicity.
They additionally exercise durable outstanding request context, rejection of
unsolicited responses and version1 records, successful versus failed-status
updates, reserved-bit masking, real MIC-verified EDI on normal/key/Leave output,
context/metadata persistence across reboot and key phases, and interrupted
request/response commits with unchanged failure outputs. No deadline/readiness
claim is inferred from these security-owner tests.
Post-verification protected/unprotected APS ZDO/application Data and protected
ACK pass-through are exercised without asserting upper-layer readiness.
The final-permit regression sends a real NWK-protected, EDI-bearing APS Data
broadcast to `FFFC` after authenticated ED Timeout, checks all decrypted
fields and exact37-byte capacity, and proves `FFFC` RX remains rejected
without advancing replay/NV state. It also rejects unicast-APS/ACK/APS-secured
`FFFC` output while preserving `FFFF`/`FFFD` reception.

The parent-wired focused target runs strict native, nonrecovering sanitizer
and SDCC object compilation (not a successful target image/simulation):

```sh
make -j1 BOARD=generic BUILD=build/security-keys-fffc test-security-keys
```

Direct host reproduction (run from repository root):

```sh
mkdir -p build/security-keys-dev
cc -std=c99 -O2 -Wall -Wextra -Werror -pedantic \
  -DCC2530_HOST_TEST -DCC2530_BOARD=0 -Iinclude -Itests \
  tests/test_security_keys.c tests/security_joint_model.c \
  tests/security_aes_model.c tests/aes_reference.c tests/host_mmio.c \
  tests/host_flash_engine.c src/flash_exec.c src/flash.c src/flash_write.c \
  src/nv_record.c src/security_counter.c src/timebase.c src/aes.c \
  src/ccm_star.c src/nwk_frame.c src/aps_frame.c src/zigbee_mmo.c \
  src/zigbee_key_hash.c src/ed_wire.c src/security_keys.c \
  -o build/security-keys-dev/test-security-keys
build/security-keys-dev/test-security-keys
```

The nonrecovering sanitizer variant uses `-O1 -g -fsanitize=address,undefined
-fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie`.
The focused corpus passes **9,490 checks**. The unchanged default AES-model
callers also pass: security 12,185 checks; MMO/install-code 265,384; keyed-hash
2,891. Repository guardrails and `git diff --check` pass. No full local matrix
or Actions acceptance is claimed by this focused work.

SDCC object preparation:

```sh
sdcc -mmcs51 --model-large --std-c99 --debug --opt-code-size --Werror \
  -Iinclude -DCC2530_BOARD=0 -c src/security_keys.c \
  -o build/security-keys-dev/security_keys.rel
```

The earlier owner object was 14,755 CODE bytes, 783 XDATA bytes, 46 DATA bytes,
4 overlay bytes and 12 bit variables. Those are compiler object extents, not
a proved safe complete allocation or executed stack high water.

The genuine thirteen-service resident + extended-wire + owner + minimal
caller link **failed**, without changing the old limits: 49,576 requested
CODE bytes versus 32,768, and failure to allocate 57 consecutive DATA bytes.
Its memory report requested 3,909 ordinary XDATA bytes and initial SP `7F`,
also not the accepted resident 3,200-total-XDATA / SP`7C` contract. This
failure is **not an image check pass or a simulated stack measurement**.
That historical failed profile was not repaired by relaxing its limits.
The separate [banked security profile](BANKED_SECURITY.md) now links and
executes the real owner/wire/crypto/NV composition with physical DATA
reservations, full artifact/active-frame checks and mixed peripheral replay.
The current ordinary owner is16835 CODE /841 XDATA /4 DATA bytes;
banked entry/return adds204 CODE bytes. Wipe-owned staging and private
inlining trade XDATA/CODE for genuine stack headroom. Old profiles, schemas
and host corpora remain unchanged. Full MCU MAC/BDB join and hardware gates
remain separate; this synthetic owner fixture is not board firmware.
