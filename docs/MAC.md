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

Beacon/command frames, absent-address DATA layouts, reserved address modes,
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
the unused bytes. Unused ACK address/PAN storage is ignored by the encoder
and zeroed by the decoder. This representation differs from APIs that keep
extended addresses in display order.

Compression is never chosen automatically. If requested, both PAN IDs in
the encoder input must match; the source PAN ID is omitted on wire and
reconstructed by the decoder. Equal PAN IDs without compression are allowed.
Broadcast short source addresses and ACK requests to short destination
`FFFF` are rejected. Other address ownership, destination filtering and
association policy belong to a future MAC, not this codec.

## API and failure contract

`mac_frame_encode()` takes a header, opaque payload, output capacity and
output-length pointer. A null payload is allowed only for length zero.
`mac_frame_decode()` returns a copied header plus payload offset/length;
payload bytes remain in the caller's input buffer.

Both return a `mac_codec_result_t`. `MAC_CODEC_OK` means only that the
supported body layout was encoded/decoded. It is **not CRC verification,
authentication, replay protection, peer acceptance or permission to act on
payload contents**. MAC-security-enabled input is rejected explicitly, not
treated as verified plaintext. NWK/APS security is not inspected.

Invalid pointers, truncated or oversized bodies, insufficient output
capacity, invalid headers and unsupported features have explicit result
codes. Callers must check the result before consuming outputs. The decoder's
result object and the encoder's output bytes/length are unchanged on failure.
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

Host-only boundary tests use exact-sized allocations, so sanitizer runs can
detect accesses beyond declared input/output storage. No heap is used by
the codec or target-side tests.

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
