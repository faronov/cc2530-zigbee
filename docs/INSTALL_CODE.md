# Bounded install-code derivation

`zigbee_mmo` is an isolated, original BSD-3-Clause implementation for #22.
It calls the real `aes128_encrypt_block`/timebase services; there is no
production software AES fallback or successful cryptography mock.
It is **host-tested, image-checked and simulated**, not hardware-observed
install-code processing, provisioning, Trust Center verification or join.
No board image links it.

## Selected format and hash

The baseline is BDB3.0.1, document16-02828-012, sections10.1.1-2 pp73-74,
and Core R22 Annex B.1/B.6 pp490,493-494. See the
[primary source/provenance record](PROVENANCE.md#install-code-and-aes-mmo-sources).

`install_code_derive` accepts exactly18 binary octets:16 install-code octets
and the little-endian16-bit CRC. CRC uses polynomial1021, reflected input/
output, direct initializationFFFF and final XORFFFF; the reflected loop uses
8408. Every other uint16 length is rejected before reading the code.
Legacy6/8/12-octet codes, text/hex parsing, generation and provisioning are
not supported. A valid CRC detects corruption; it does not authenticate a
device or establish that a code was generated randomly.

The CRC is checked before AES. **All18 octets, including the CRC**, are
hashed. The short public BDB known-answer vector is:

```text
code + CRC: 83fed3407a939723a5c639b26916d505c3b5
CRC:        B5C3 (wire C3 B5)
AES-MMO:    66b6900981e1ee3ca4206b6b861c02bb
```

`zigbee_mmo_hash` accepts0..32 binary octets and returns16 octets. The
initial hash is zero; each step is `H = AES(H, block) XOR block`. Padding
appends80, the minimum zero octets and the **big-endian16-bit original bit
length**. A residual14/15-octet tail needs another block. Exact16/32-octet
messages also require padding. At most three real AES calls occur.
R22's different long-message padding is outside this explicit bound.
This primitive is not HMAC or a Verify-Key/Confirm-Key procedure. The
separate [keyed-hash foundation](KEY_HASH.md) composes it for the reviewed
transport/load derivations and initiator hash, still not TC confirmation.

## Ownership and failure

Both APIs are serialized, foreground and nonreentrant. All required objects
must be complete, disjoint and correctly sized; input is immutable.
Only the hash API accepts NULL input, and only at length zero. Output is
a writable16-byte object and diagnostics are required. These are C object
and ownership preconditions, not arbitrary-pointer validation. Exclude
private/compiler/libc storage, reserved status, MMIO and the IRAM alias.

The full [AES ownership/history contract](ARCHITECTURE.md#isolated-aes-128-dma-block)
applies: stable supported clocks, exclusive channels/engine, disabled relevant
IRQs, clear debug DMA_PAUSE and genuine reset/history conditions. Link the
timebase and AES private storage before this module and its callers.
Each attempted AES call retains its positive finite timeout/poll cap; the
three-call maximum bounds total work. There is no retry or recovery API.

Every failure preserves output. Preflight failures also preserve diagnostics;
operational diagnostics report accumulated32-bit polls, attempted blocks and
the last AES status. The private89-byte state is overwritten through volatile
stores on operational exit. Lower AES buffers, hardware, registers, compiler
temporaries and caller-owned copies are **not** securely erased by this.
No complete erasure or side-channel-resistance claim follows.

A short poll-limit failure can leave **output DMA armed**, before input arm
or the KEY command. The lower AES fault remains latched; no success/history
flag is fabricated and buffers/engine ownership are not released. Hash
failure never makes a partial derived key available. Recovery requires the
lower service's actual reset policy, not clearing a software flag.

## Evidence and resources

```sh
make BOARD=generic test-zigbee-mmo
make BOARD=lg_esl29_rev03 test-zigbee-mmo
```

Full acceptance runs in the existing two Zigbee-security CI jobs; do not
duplicate the full matrix locally. No cryptographic artifacts are uploaded.
Native and nonrecovering ASan/UBSan use the real driver and original host-only
mathematical AES/peripheral oracle. The265384-check corpus covers the primary
vector, an independent flat-padding MMO oracle, a normal-polynomial/reflected-
input CRC oracle, exact malloc extents, all lengths0..32,256 synthetic codes,
all144 single-bit code corruptions, all65536 external lengths, each possible
AES-block failure and retained-fault retry. Existing independent AES known
answers remain required.

The linked39-case corpus performs117 shared checks and58 completed AES calls,
including the primary KAT, every supported hash length, empty NULL input,
CRC/length rejection and the actual output-arm/poll-limit failure.
The controller models only explicit AES/DMA behavior: actual C, call/return,
MMIO writes, descriptor contents and all transferred bytes are checked.
It does not inject hash results or replace service returns.

| Resource | Actual | Unchanged proof cap |
| --- | ---: | ---: |
| Complete CODE |9060|10240|
| Ordinary XDATA + reserved status |426+64|512 total|
| Stack allocation / initial checkpoint SP |41 /40 hex|IRAM alias enforced|
| Maximum executed SP |5C hex|7C hex|
| MMO module CODE / XDATA |1322 /127|included above|

Ordinary XDATA partitions are timebase`[0,25)`, AES`[25,207)`,
MMO/private/compiler`[207,334)`, caller/compiler`[334,406)` and complete
linked libc`[406,426)`. Four immediate linked-listing snapshots, complete
CODE/raw-CDB/map/memory/object identities and scalar/generic-pointer/field
ABIs are pinned. All39 exact stack peaks remain checked.
There are27597 artifact,8034 final-snapshot,117 stack and one missing-alias
negative controls. The unchanged per-simulator deadline is15 seconds.
The shared replay retains the original #19 corpus and default behavior.

This is isolated fit, not whole-stack/ISR headroom. Public test inputs are
not real secrets, entropy, provisioned identities or evidence of membership.
Production randomness, provisioning/TC policy, durable key/replay state and
commissioning remain required separately.
