# R22 keyed-hash foundation

`zigbee_key_hash` supplies the bounded cryptographic prerequisite for #23,
not its key lifecycle or Trust Center exchange. It composes the real
[AES-MMO](INSTALL_CODE.md) and AES/timebase services. It is **host-tested,
image-checked and simulated**, not hardware-observed. No board image links
it, and no caller can obtain an authenticated/committed-key state from it.

## Exact purposes

| Purpose byte | Result | Primary R22 location |
| --- | --- | --- |
|00|Key-transport key|4.5.3 p457|
|02|Key-load key|4.5.3 p457|
|03|Initiator Verify-Key hash; **not an encryption/decryption key**|4.4.10.7.4 p452|

All other bytes fail. These are HMAC message bytes, **not** the security
auxiliary-header key identifiers or Transport-Key's StandardKeyType.
Data-key use takes the original link key, not this transformation.
Derived transport/load use must share the associated link-key counters.

The Verify-Key service's cross-reference to4.5.3 does not itself define
selector03. The actual command-field definition at4.4.10.7.4 does; no erratum,
later-revision assumption or implementation-code inference was needed.
The [source record](PROVENANCE.md#r22-keyed-hash-sources) pins this distinction.

For a16-byte input key `K`, one-byte purpose `P`, and R22 AES-MMO `H`:

```text
inner = H((K XOR 36 repeated16 times) || P)       #17 input bytes
result = H((K XOR 5C repeated16 times) || inner) #32 input bytes
```

There are exactly two inner and three outer AES calls on success. The
short-message MMO padding includes the original bit length. This API is not
variable-key/message HMAC, a challenge-response exchange, key selection,
key generation or proof of any key's entropy. Zigbee's MMO instantiation is
not a FIPS validation or approval claim.

## Failure and ownership

The key16/output16/info objects are required, complete, disjoint and valid
for the whole serialized foreground call; the key stays immutable. The
underlying [MMO/AES ownership contract](INSTALL_CODE.md#ownership-and-failure)
applies without relaxation, including private/compiler/libc exclusions,
clock/IRQ/DMA/reset history and finite per-block timeout/poll limits.

Failures preserve output. Preflight also preserves diagnostics; operational
diagnostics accumulate attempted AES blocks/polls across both hashes and
retain the last AES status. No inner hash or incomplete outer hash is
published. The60-byte private keyed-hash state and lower89-byte MMO state
are overwritten on operational return. This does **not** erase lower AES,
hardware, compiler or caller copies.

The lower retained fault is not cleared on retry. Failures may retain output
DMA arm alone or both DMA arms with an unfinished KEY command. No fault path
claims hardware quiescence or releases buffer/engine ownership. Actual lower
reset policy, not a success-shaped hash or software flag, governs recovery.

Receiving a new key, computing its hash, successfully transmitting Verify-Key,
and authenticating/committing Confirm-Key remain distinct operations. This
module performs only the hash. #23's durable slots, replay state, old-key
retention and actual confirmation procedure remain required.

## Independent evidence and resources

```sh
make BOARD=generic test-zigbee-key-hash
make BOARD=lg_esl29_rev03 test-zigbee-key-hash
```

Both existing security CI jobs run this corpus alongside the unchanged
#19/#22 corpora, with nonrecovering ASan/UBSan and no artifact uploads.
The2891 native checks cover all three purposes, all256 selector bytes,
256 public patterned keys with exact malloc extents, failure at every one
of the five AES positions, retained-fault retry and invalid arguments.
The shared original mathematical AES/flat-padding MMO oracle retains all
existing five AES KATs and the original265384-check install-code corpus.

Nine original synthetic known-answer tuples use zero, ascending-byte and the
public BDB install-code-derived key. They were independently cross-checked
using Python's standard-library HMAC construction and local OpenSSL AES-ECB,
with the MMO adapter first reproducing the primary BDB KAT. These are
project-generated vectors, **not** published CSA HMAC KATs. OpenSSL is not a
new build/CI dependency and no external implementation is imported.

The15 genuine linked cases execute45 shared checks and51 completed AES calls.
They include all nine KATs, preflight errors, a short output-arm failure and
stalled third/fifth AES calls after an actual inner hash/outer prefix.
Every call, MMIO write, descriptor and transferred byte is checked; only
the explicitly synthetic peripheral behavior is supplied. Hash results
and firmware returns are never injected.

| Resource | Actual | Proof cap |
| --- | ---: | ---: |
| Complete CODE |10143|12288|
| Ordinary XDATA + reserved status |506+64|640 total|
| Stack allocation / initial SP |4C /4B hex|IRAM alias enforced|
| Maximum executed SP |71 hex|unchanged7C hex|
| Keyed-hash module CODE / XDATA / DATA |729 /77 /3|included above|

Partitions are timebase`[0,25)`, AES`[25,207)`, MMO`[207,334)`,
keyed-hash private/compiler`[334,411)`, caller/compiler`[411,466)` and
**all** linked libc`[466,506)`, including division/modulo temporaries.
Five immediate listing snapshots, complete raw-CDB/CODE/map/memory/object
identities, typed ABI and every exact executed peak are pinned.
There are29883 artifact,3735 snapshot,45 peak and one missing-alias negatives.
The15-second per-simulator deadline and every pre-existing proof cap/case
remain unchanged. This isolated composition is not whole-stack or ISR fit,
provisioning, authenticated TC exchange, entropy or network membership.

The separate [resident profile](SECURITY_RESIDENT.md) also runs this exact
corpus with all thirteen real crypto/NV services present and checked DATA
frame sharing. It leaves this standalone image, ABI, corpus and caps intact.
Resident fit is not mixed key lifecycle or actual TC confirmation.
