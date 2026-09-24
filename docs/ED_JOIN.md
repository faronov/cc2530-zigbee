# Bounded authenticated end-device integration

This is original C for a **host-exercised R22/BDB3.0.1 join**, not a networking
CC2530 firmware image. The synthetic coordinator and PHY adapter supply
explicit public identities, frames, CRC results and captured times. They do
not replace the production MAC controllers, key owner, CCM, HMAC, AES, counter,
journal or flash services with successful stubs.

## Selected configuration and boundaries

One awake end device associates directly with the centralized coordinator
and Trust Center (NWK address0). Provisioning explicitly binds the TC IEEE,
own IEEE, Extended PAN, PAN, channel and update ID, derives a unique
16+2-byte install-code key and supplies trusted initial counter floors.
EMPTY, degraded storage and persisted membership never automatically create
keys or authorize application traffic.

The coordinator's selected Beacon must have its actual extended MAC source;
Extended PAN is not substituted for its IEEE address. The supplied link cost
is an adapter fact1..3, not an invented RSSI conversion. This is one selected
network, not arbitrary network selection, router forwarding or trust-center
migration. A TC with Stack Compliance Revision below21 is not silently
accepted through BDB's legacy exception.
The descriptor's system-server discovery flags do not establish TC trust and
are not an extra BDB acceptance prerequisite; the revision selects the modern
exchange, and the actual bound-key proof must still complete (BDB10.2.5 p78).

The initial parent must advertise End Device Timeout Request keepalive
(parent-information bit1). Timeout enumeration1 requests two minutes;
the awake owner repeats the genuine secured request after30 seconds.
A MAC-poll-only parent is an explicit unsupported/failing configuration.
Persisted restart returns `BDB_JOIN_RECOVERY_REQUIRED`, not READY. Complete
secure-rejoin/recovery orchestration belongs to #27.

## Ownership and successful sequence

`ed_wire` extends, rather than weakens, the original codecs. It provides
bounded NWK Data/Command, APS unicast/broadcast Data, Command and full/short
ACK syntax plus real level5 ENC-MIC32 protection. Unsupported group,
fragmentation, source-route and extended layouts fail explicitly. Normalized
security-level bits follow the same pinned R22 rule as the original envelope.

[`security_keys`](SECURITY_KEYS.md) owns actual key selection, durable incoming
floors, outgoing allocations, two NWK slots and old/pending TC key lifecycle.
Every received NPDU crosses that owner; no caller-supplied authentication
boolean substitutes for crypto or persistence.

`bdb_join` runs this sequence:

1. Real `mac_scan`, candidate parsing/selection and confirmed restoration.
2. Real `mac_join` Request, extraction, Response and confirmed cleanup, all
   using the same `mac_tx` DSN/generation/IFS owner without reinitialization.
3. Persist the genuinely allocated short address; require a token-correlated
   adapter confirmation of installed PAN/channel/IEEE/short filters and awake RX.
4. Authenticate initial network-key Transport-Key before membership or announce.
5. Transmit Device_annce, then match the TC Node Descriptor response.
6. Send encrypted Request-Key under key A; authenticate Transport-Key B;
   send the actual selector03 HMAC in Verify-Key; authenticate successful
   Confirm-Key under B. Receipt of B alone never verifies it.
7. Negotiate ED Timeout and the selected parent keepalive method.
8. Actually transmit and quiesce the final `Mgmt_Permit_Joining_req` to FFFC,
   duration180 and TC_Significance1, then expose application readiness.

Step5 precedes the updated TC exchange, as BDB8.2 requires. The final broadcast
does not enable local child admission or claim receipt by every network device.
Logical completion, MAC ACK, APS ACK and physical quiescence remain distinct.
An ED sends its NWK broadcasts to the parent's MAC short address with MAC ACK
disabled, not to MAC FFFF as a router would (R22 3.6.5 pp383-384).
Failed commissioning demotes readiness and requests a real protected Leave
when possible. Keyless abandonment explicitly produces no frame. A physical
cleanup failure retains the lower owner in FAULT, never resets it to fake IDLE.

## Bounded transport

`nwk_aps` has one outbound transaction, one priority ACK, one receive record
and eight non-evictable live duplicate entries. FULL is backpressure, not
successful delivery. A full receive/ACK slot rejects new input before a key
owner transition; consumers must drain it.
Admitted durable control transitions use that reserved receive slot rather
than being lost to a full ordinary APS duplicate table. Key-command replay
and outstanding-phase decisions remain with the real key owner.

Eight separate broadcast records hold source/NWK-sequence identities for both
local and received broadcasts. The caller supplies the established network
broadcast-delivery bound in symbols, not an assumed universal timeout; the
synthetic one-hop fixture supplies16 seconds. Cached duplicates and full-table
candidates can be discarded before crypto, without creating records or claiming
authentication. New received records are created only after actual admission.

Application endpoint/profile admission requires completed announcement,
verified TC key and final permit transmission. The BDB facade additionally
owns parent negotiation and only exposes send in its READY state. There is no
ZCL profile/interoperability claim: admitted application packets are delivered
as bounded opaque APS payloads.

APS ACK matching checks peer, endpoints, profile, cluster, format and counter.
An ACK before a genuine MAC SENT cannot complete an operation. An ACK while
cleanup is outstanding can be remembered but cannot replace QUIESCED.
Three APS retries retain transaction identity while rebuilding protection with
fresh durable security counters and a fresh NWK sequence.

The caller supplies `ack_wait` in16-us symbols. Its minimum93750-symbol base
is the selected profile2 maximum-depth term, not a measured cryptographic
latency; the caller must add its proven encryption/decryption allowance.
The synthetic fixture uses100000. The duplicate window is at least16 seconds
and covers four complete MAC lifetimes, cleanup bounds and ACK intervals.
An absolute transaction deadline also bounds delayed foreground service.
APS counter wrap observes a full window of
quarantine. These are finite lifetime assumptions, not proof against arbitrary
unbounded delayed packets after an eight-bit counter has been reused.
NWK commands do not consume an APS counter, so parent keepalive and Leave
remain available during APS wrap quarantine.

## Endpoint zero

`zdo_runtime` takes only packets admitted by the real transport. It supplies
one timed TC client transaction, one deferred server response and one
application delivery. Node Descriptor and single-device NWK/IEEE address
responses match peer, TSN, address, endpoint, profile and outstanding timeout.
Client requests use AR0 and bounded response retries rather than treating
queue admission as a response.

The server retains the original generic unsupported-unicast fallback and
broadcast/Parent_annce drop rules. Address requests add the specified matching
broadcast exception, addressed failures and childless extended response.
Device_annce updates a four-entry address map, invalidates conflicting short
bindings, and fails closed on conflict with the fixed local/TC binding.
It does not manufacture a replacement address or claim complete routing-side
conflict resolution. Optional discovery, binding and application services
remain outside this initial integration.

Authenticated Network Update is persisted by the key owner. During READY the
owner stops current traffic and requires a new real adapter-install confirmation
before resuming. An update during initial commissioning requires recovery rather
than silently continuing with uninstalled network parameters.

## Evidence and remaining target gate

`make BOARD=... test-ed-integration` consists of `test-ed-wire`,
`test-security-keys` and `test-bdb-join`: strict native execution,
nonrecovering ASan/UBSan and SDCC compilation of the new production modules.
The combined host peripheral model shares live AES and flash state; only an
explicit modeled CPU power cycle resets services, retaining NV when requested.
The native flash-RAM entry uses the existing MMIO controller bridge; host
execution does not execute8051 opcodes. Genuine RAM-opcode execution remains
in the separate, unchanged target flash proofs.
The coordinator decrypts real transmissions, checks announcement ordering,
verifies HMAC03 before issuing Confirm-Key and checks the final permit payload.
The end-to-end negative corpus includes key and confirmation timeouts, real
Leave/quiet abandonment, unsupported parent keepalive, missing physical
quiescence, wrong-TSN/status/key responses, broadcast duplicate filtering and
PAN-update cancellation/reinstallation before application traffic can resume.
Network-key, TC-key and Confirm responses at the expired deadline cannot
complete commissioning. Exchange timers start with the request, not after
eventual MAC cleanup; a valid MIC does not extend a transaction deadline.
It also exercises all three APS retries, late/wrong/early ACKs, full receive
tables and real transmitted APS-counter wrap. Parent keepalive continues
during the wrap quarantine; counter0 is not reused before its expiry.

This evidence is **host-tested and SDCC compile-checked**, not a new linked-image,
alias-aware execution or physical observation. Existing linked-image and
simulator suites remain mandatory and unchanged; the old resident profile
does not prove that these new services fit or execute safely together on8051.
Whole-stack CODE/XDATA/IRAM placement, banked execution if required, complete
new ABI/alias proofs, a truthful real-radio adapter, entropy, electrical
durability and physical interoperability are still gates. #23-#26 must not
be closed merely because this synthetic join reaches READY.

The smaller key-owner/crypto/NV composition already requests49,576 CODE bytes
against the unchanged32,768-byte unbanked limit and fails allocation of57
contiguous DATA bytes. That failed link is not a measurement of a working
whole-stack image, and relaxing the limit would not solve banking/IRAM safety.

All fixture identities and key material are deliberately public synthetic
values. No key-bearing firmware/NV dumps or captures are uploaded by these
CI jobs. No equipment access, flashing or RF tests are performed.
