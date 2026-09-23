# Bounded R22 CCM* and secured NWK/APS frames

`ccm_star` and `zigbee_security` implement the authenticated cryptographic
and wire subset of #19, using the actual `aes128_encrypt_block` service.
They are isolated foreground services, not a security manager, trusted
endpoint, replay filter, transport or authenticated join. Neither is linked
into board firmware. No software AES implementation is linked into the target.

## Primitive and wire contract

The [CCM API](../include/ccm_star.h) implements AES-128 with a 13-byte nonce,
L=2 and MIC lengths4/8/16. It accepts up to132 AAD bytes and116 message bytes;
the complete ciphertext/MIC is at most132 bytes. Empty AAD and message are
supported. M=0 is deliberately unsupported: no unauthenticated encryption or
plaintext release is advertised.

`ccm_star_crypt(open, ...)` seals plaintext to ciphertext/MIC when `open=0`,
or verifies that representation before returning plaintext when `open=1`.
CBC-MAC and counter blocks follow R22 Annex A. Their lengths/counters are
big-endian; this must not be confused with the little-endian Zigbee frame
counter. The entire MIC is compared before publishing plaintext. The bounded
implementation is not a general side-channel or constant-time certification.

The [frame API](../include/zigbee_security.h) supports:

| Layer/layout | Key identifier | Extended nonce | Effective level |
| --- | --- | --- | --- |
| Existing NWK Data, including supported optional IEEE headers |1 network|1|1/2/3 or5/6/7|
| Existing normal-unicast APS Data |0 data/link|0 or1|1/2/3 or5/6/7|
| Two-byte normal-unicast APS Command header, optional ACK request |0 data,2 transport,3 load|1|1/2/3 or5/6/7|

This is header syntax, not command authorization or key selection.
APS Commands require a nonempty payload, whose first byte is the command
identifier. No command semantics, APS ACK, broadcast/group/fragmentation
layout or NWK Command/routing extension is implemented. Existing bare
`nwk_frame` and `aps_frame` codecs are unchanged and still reject secured
input. The wrapper reuses their actual parsers for supported Data headers.

The auxiliary header is control, counter, optional source IEEE and, only for
key-id1, key sequence. The nonce is source IEEE8 || counter4 || effective
control1. Reserved control bits6/7 fail. CounterFFFFFFFF fails in both
directions. NWK/APS packet caps remain116/108 bytes, including auxiliary
header and MIC; these are not service-level payload promises.

For levels5/6/7, AAD is outer header plus auxiliary header and the payload
is encrypted. For levels1/2/3, the payload is appended to AAD and the CCM
message is empty; the MIC is still masked with AES(Key,A0). Transmitted
security-level bits are zeroed **after** cryptography. Received low three
bits are replaced by the trusted configured level **before** authenticating,
not required to be zero or trusted as configuration. Levels0/4 fail.

`zigbee_security_inspect()` returns syntax-only, untrusted metadata. It does
not authenticate or admit a packet. Absent source/sequence fields are zeroed;
presence must be determined from the header, not inferred from those zeroes.
`zigbee_security_crypt()` adds protection on seal, or verifies and removes
auxiliary/MIC fields on open. Successful open clears the outer security flag
in a normalized copy consumable by the bare codecs. Retain the successful
security context separately: that normalized flag does not mean the received
packet was unsecured.

## Key, counter and admission ownership

The caller supplies the **effective AES key**, not necessarily a raw link
key. For identifiers2/3 it must already have performed the required
transport/load-key derivation. This module does not derive keys or determine
which command is allowed to use them. The caller binds source IEEE,
identifier, sequence, configured level and extended-nonce policy to its
selected key material.

On RX, an included source must match the supplied source. Without a source
field, the caller supplies the previously mapped IEEE identity. Identifier,
extended-nonce policy and applicable key sequence must match. The RX call
ignores the context's outgoing counter. None of these comparisons establishes
unique peer identity when the key is shared by a group.

On TX, the counter must already be durably reserved and consumed, including
when this call later fails. No counter is allocated, incremented or persisted
here. Receiving a valid MIC does not advance a replay watermark.
[Durable outgoing reservations](SECURITY_COUNTERS.md) (#21), install-code/MMO derivation (#22), key/replay/TC
state (#23), admitted transport (#24) and commissioning (#26) remain separate.
Public deterministic test inputs neither implement nor qualify entropy (#10).

## Failure, memory and hardware-service ownership

Every frame-API error preserves caller output and metadata. CCM errors
preserve output and written length; preflight errors also preserve diagnostic
metadata, while operational results report AES status, polls and block calls.
There is no retry, fallback, implicit reset/recovery or success-shaped failure.

All pointers must identify truthful, complete, disjoint objects that remain
immutable during the call. Null AAD/input is allowed by CCM only with the
corresponding zero length. Caller buffers must exclude linked private and
libc scratch, status, MMIO and the IRAM alias. There are no heap allocations.
Calls are serialized, nonreentrant and foreground-only.

The real AES contract still applies: supported clock, exclusive channel0
history, all IRQs masked, clear debugger DMA_PAUSE and disjoint DMA storage.
Link timebase and AES before higher-level staging/caller objects so that the
lower AES ownership fence excludes all of its own private storage.
Each AES block has a finite supplied raw-time timeout and poll cap. A call
uses at most27 AES blocks; this is a bounded work composition, not a new
global wall-clock deadline or a DMA-fault recovery service.

The CCM267-byte and envelope331-byte private states are overwritten via
volatile byte loops on operational exit. This is **not full secure erasure**:
AES hardware, lower-driver and compiler copies retain the limitations in
[`aes.h`](../include/aes.h). Production code emits no key/plaintext diagnostics.
Only the host test controller can print its fixed public synthetic vectors.

## Offline evidence and resource ledger

```sh
make BOARD=generic BUILD=build/generic/security test-zigbee-security
make BOARD=lg_esl29_rev03 BUILD=build/lg_esl29_rev03/security test-zigbee-security
```

The target links the actual timebase, AES, CCM, NWK, APS, envelope and caller
objects in that order. Seven relocated listings are snapshotted immediately
after linking. The host-only mathematical AES oracle and peripheral controller
are excluded from this SDCC image.

The native/nonrecovering ASan/UBSan corpus has12185 checks. An independently
structured CCM oracle uses a flat padded authentication string; a separate
wire oracle constructs all18 layer/level envelopes and command key selectors.
Coverage includes all AAD lengths0..132 and message lengths0..116 for all
three MIC sizes, exact allocations, corruption/truncation, invalid selectors,
the primary Annex C KAT, per-block failures, retained AES faults and unchanged
caller buffers. The primary KAT and existing five AES KATs anchor the oracles.

The linked replay runs23 scenarios,220 target checks and345 completed real
AES calls. Actual AES MMIO instructions, DMA descriptors and each transferred
byte execute against a synthetic peripheral, not a replacement crypto return
value. Public host traces are completely hash-pinned. Simulator runs retain
the existing15-second deadline and XDATA1F00..1FFF-to-IRAM alias.

| Resource | Measured / limit |
| --- | --- |
| Complete isolated CODE |20250 /24576 bytes|
| Ordinary XDATA plus reserved status |1907+64 /2048 bytes|
| Stack allocation / unwound SP |55 /54 hex|
| Maximum observed full-run SP |7C /7C hex|
| CCM module CODE / XDATA |2103 /314 bytes|
| Envelope module CODE / XDATA |4124 /395 bytes|

This composition reaches the existing stack cap: there is **no demonstrated
headroom for another wrapper or ISR**, and no whole-stack fit claim.
Ordinary XDATA spans are timebase[0,25), AES[25,207), CCM[207,521),
NWK[521,617), APS[617,686), envelope[686,1081), caller[1081,1875),
and complete linked libc scratch[1875,1907). The result occupies1E00..1E07;
unallocated memory, remaining status bytes and upper IRAM remain guarded.

The strict proof pins complete CODE, raw CDB before decoding, map, memory,
all objects, ordered listings and instruction metrics. It checks struct
fields, public ABI/parameter storage, actual call chains, module/private/libc
ownership, final AES/fault/register state, complete caller bytes, private
wipes, unchanged SFRs and exact per-case stack peaks. The61685 artifact negatives
mutate every CODE byte, map symbol and F/S/L/T metadata record, listings,
objects and raw line endings, plus54 snapshot faults,3 peak faults and a
missing physical alias. No existing image, case, deadline or budget is relaxed.

Evidence is **host-tested, image-checked and simulated**, not new
hardware-observed AES/CCM behavior, replay-safe transport, authenticated join,
coordinator interoperability or Zigbee/BDB conformance. Both-board GitHub
Actions jobs are the full acceptance gate; they upload no crypto artifacts.
Primary references and original implementation provenance are recorded in
[PROVENANCE](PROVENANCE.md#r22-ccm-and-security-envelope-sources).
