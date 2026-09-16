# Offline legacy MAC body codec

`include/mac_frame.h` and `src/mac_frame.c` implement a bounded, standalone
byte codec. This is preparatory M3 work permitted alongside earlier hardware
gates, **not a radio driver, functioning MAC, association or Zigbee join**.
It is not linked into either board firmware image.

## Supported subset

| Frame | Supported fields |
| --- | --- |
| DATA | Frame version 0 or 1; both destination and source addresses present; each short (2 bytes) or extended (8 bytes); explicit PAN compression |
| ACK | Frame version 0, sequence number and optional Frame Pending; no addresses or payload |
| COMMAND | Frame version 0; the five fixed-format commands and command-specific addressing below |

Beacon frames, other command identifiers, COMMAND version 1, absent-address DATA layouts, reserved address modes,
version 2/3, sequence suppression, information elements and MAC security
processing are unsupported. This is deliberately narrower than all legal
IEEE 802.15.4 layouts. Unsupported layouts return an error; they are not
parsed using a different version's assumptions.

The body starts with the two FCF bytes and a sequence byte. It **excludes**
the PHY length byte, two-byte FCS and any transceiver RSSI/LQI/status bytes.
The legacy 127-byte PSDU limit therefore leaves at most **125 body bytes**.
The largest supported header is 23 bytes. Neither function calculates,
checks or supplies an FCS. The future radio caller must separately check CRC
and strip framing/metadata; extra trailing bytes in a DATA input cannot be
distinguished from payload by this codec.

FCF, PAN IDs and short addresses use little-endian wire order. Extended
addresses are arrays in wire order too: byte 0 is the least-significant octet.
Only the first two array bytes matter for short addresses; the decoder zeros
the unused bytes. Absent address/PAN storage is ignored by the encoder and
zeroed by the decoder, including ACKs and single-address commands.
This representation differs from APIs that keep
extended addresses in display order.

Compression is never chosen automatically. If requested, both PAN IDs in
the encoder input must match; the source PAN ID is omitted on wire and
reconstructed by the decoder. Equal PAN IDs without compression are allowed.
Commands additionally enforce the compression choices below.
Broadcast short source addresses and ACK requests to short destination
`FFFF` are rejected. Other address ownership, destination filtering and
association policy belong to a future MAC, not this codec.

## Fixed-format commands

Payload lengths include the one-byte command identifier, but not the MHR.
All command frames in this subset are unsecured and use version 0.

| Command | ID / payload bytes | Addressing and fixed fields |
| --- | --- | --- |
| Association request | `01` / 2 | Short/extended destination, extended source; no compression; source PAN `FFFF`; ACK requested |
| Association response | `02` / 4 | Both addresses extended, compressed PAN; ACK requested |
| Disassociation notification | `03` / 2 | Short/extended destination, extended source, compressed PAN; ACK requested |
| Data request | `04` / 1 | Short/extended source; destination absent/short/extended; compression iff destination present; ACK requested |
| Beacon request | `07` / 1 | Short destination `FFFF`, destination PAN `FFFF`, no source/compression/ACK request |

Request capability bits 4/5 are rejected as reserved in this strict subset.
The remaining capability bits are caller data, including the security-capable
bit; accepting that bit does not implement or advertise a security service.
Association response carries a little-endian short address and status:
`00` succeeds with `0000..FFFE`; `FFFE` specifically means no short address
was allocated, so extended addressing is needed. Status `01` (PAN at capacity)
or `02` (access denied) requires short address `FFFF`. Other statuses are
rejected. Disassociation reason `01` means the coordinator requests departure;
`02` means the device wishes to leave. Other reasons are rejected.

The short-address sentinel `FFFE` is not accepted as a command header's
source or destination; use extended addressing. `FFFF` is accepted there
only for the beacon request's broadcast destination.

Command Frame Pending must be zero for encoding. Decoding accepts either
value and preserves the raw flag, but it has no command semantics: the standard
says to ignore it on reception. Therefore a received command with Pending set
is not directly re-encodable; the caller must deliberately clear it when
constructing a new transmission. This does not change DATA/ACK flag behavior.
Other reserved FCF/capability bits remain errors under this explicit strict
subset, not a claim to implement every permissive receiver rule.

These checks establish static format rules, not procedure context. In
particular, an absent-destination data request requires an appropriate
PAN-coordinator beacon context that this stateless codec cannot verify.
PAN/peer selection, admission, ACK exchanges, timers, polling, departure,
security and all association/join state remain unimplemented.

## API and failure contract

`mac_frame_encode()` takes a header, opaque payload, output capacity and
output-length pointer. A null payload is allowed only for length zero.
`mac_frame_decode()` returns a copied header plus payload offset/length;
payload bytes remain in the caller's input buffer.

`mac_command_encode()` and `mac_command_decode()` operate on command payloads,
including the identifier, using `mac_command_t`. Only fields belonging to that
identifier are consumed; unused fields decode as zero. Payload-only operations
do not validate a frame header. Complete frame encode/decode validates both
the command payload and its header before changing any public output.
After frame decoding, a caller can decode the returned payload span separately
to obtain the typed command fields.

All return a `mac_codec_result_t`. `MAC_CODEC_OK` means only that the
supported body layout was encoded/decoded. It is **not CRC verification,
authentication, replay protection, peer acceptance or permission to act on
payload contents**. MAC-security-enabled input is rejected explicitly, not
treated as verified plaintext. NWK/APS security is not inspected.

Invalid pointers, truncated or oversized bodies, insufficient output
capacity, invalid headers and unsupported features have explicit result
codes. Unsupported command identifiers return `MAC_CODEC_UNSUPPORTED_COMMAND`;
reserved/inconsistent command fields or trailing payload bytes within the
four-byte maximum return `MAC_CODEC_INVALID_COMMAND`. Larger command payloads
return `MAC_CODEC_TOO_LONG`; incomplete ones return `MAC_CODEC_TRUNCATED`.
Callers must check the result before consuming outputs. Every decoder's
result object and every encoder's output bytes/length are unchanged on failure.
Error precedence between multiple simultaneous faults is not an ABI.

Input and output objects/buffers must not overlap, and lengths/capacities must
describe actual accessible storage. There is no allocation, hidden global
network state, GPIO, USB, radio, interrupt or clock access. The selected SDCC
large model uses non-reentrant parameter/local storage: these functions belong
in the foreground, not concurrent ISR calls. Buffer ownership and queuing
still need their own M3 implementation.

## Evidence and simulator boundary

The ordinary `make ... all test` commands run:

- Strict host compilation, original golden vectors, all 64 DATA layouts,
  exact capacity/truncation boundaries and unchanged-output checks.
- All 65,536 FCF values on the host; exactly 66 combinations are accepted for
  the synthetic non-broadcast address pattern and chosen lengths, and each
  accepted body round-trips byte-for-byte.
- The same portable golden/layout/failure vectors compiled by SDCC and
  executed in an isolated, alias-aware uCsim test image.
- Nine command-payload vectors and twelve complete golden command layouts on
  both host and target, with truncation, capacity, reserved-field and Pending
  acceptance/transmission checks.
- Host-only command/address/flag matrices, all 256 identifiers, capability,
  reason and status values, all 65,536 response addresses under each of the
  three supported statuses, and all 8,192 command FCF patterns for each of
  the twelve golden frames.

Host-only boundary tests use exact-sized allocations, so sanitizer runs can
detect accesses beyond declared input/output storage. No heap is used by
the codec or target-side tests.
The larger exhaustive/mutation matrices stay host-only to keep test-harness
compiler spills out of the guarded upper IRAM; all twelve command layouts and
portable command failure vectors still execute under SDCC. The little-endian
leaf helpers are C99-inline, avoiding extra call/register-save overhead.
The upper-half IRAM guard and exact final stack-pointer check are unchanged.

`build/<board>/[debug_fixture/]mac_frame_test.ihx` is a **simulator-only test
executable**, not an `IMAGE` selection or board firmware to flash. It is not
uploaded by CI. Its eight-byte `MAC1` result record at XDATA `1E00` contains
version 1, size 8 and a little-endian C failure-line number (zero on success).
The test checks actual linked CODE bounds, result symbols/debug ABI, allocation
bounds and IRAM stack guards. It preserves the XDATA `1F00..1FFF` alias.

Test vectors are in CODE. Paged allocation/CRT copies are rejected in this
test image because generic C52 does not implement CC2530 MPAGE behavior.
The test harness has its own linker-accounted storage; its footprint is not
a claim about the eventual MAC or the M0 board image. The strict M0/M1
firmware budget and image checks remain unchanged.

Evidence is **host-tested, image-checked and simulated**. No packet was sent,
received or observed on hardware. Sources are recorded in
[PROVENANCE.md](PROVENANCE.md); physical M1/M2/M3 acceptance remains open.
