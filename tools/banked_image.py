#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Pack SDCC virtual bank addresses into sparse CC2530F256 physical flash."""

import argparse
import hashlib
import json
from pathlib import Path

from verify_firmware import parse_ihex, require


BANK_SIZE = 0x8000
CODE_END = 0x3E800  # NV pages 125/126 and the entire lock/config page stay absent.


def physical_address(virtual):
    require(type(virtual) is int and 0 <= virtual < 0x80000, "Invalid virtual CODE address")
    bank, logical = virtual >> 16, virtual & 0xFFFF
    require(logical < BANK_SIZE if bank == 0 else logical >= BANK_SIZE,
            "Noncanonical common/banked CODE address")
    physical = logical if bank == 0 else bank * BANK_SIZE + logical - BANK_SIZE
    require(physical < CODE_END, "CODE overlaps reserved NV/lock flash")
    return physical


def identity(virtual):
    """Never confuse the debugger's logical PC with a unique flash address."""
    physical = physical_address(virtual)
    return {"virtual": virtual, "bank": virtual >> 16, "logical": virtual & 0xFFFF,
            "physical": physical}


def pack(image):
    require(bool(image), "Empty banked image")
    physical = {}
    for address, byte in image.items():
        target = physical_address(address)
        require(type(byte) is int and 0 <= byte <= 255, "Invalid CODE byte")
        require(target not in physical, "Overlapping physical CODE")
        physical[target] = byte
    return dict(sorted(physical.items()))


def record(address, kind, data):
    raw = bytes((len(data), address >> 8, address & 255, kind)) + data
    return ":" + (raw + bytes((-sum(raw) & 255,))).hex().upper()


def ihex(image):
    """Emit only present bytes; never pad across an absent/reserved flash page."""
    lines = []
    addresses = sorted(image)
    index, upper = 0, None
    while index < len(addresses):
        start = addresses[index]
        if start >> 16 != upper:
            upper = start >> 16
            lines.append(record(0, 4, upper.to_bytes(2, "big")))
        data = bytearray((image[start],))
        index += 1
        while (index < len(addresses) and len(data) < 16
               and addresses[index] == start + len(data) and addresses[index] >> 16 == upper):
            data.append(image[addresses[index]])
            index += 1
        lines.append(record(start & 0xFFFF, 0, data))
    return "\n".join(lines + [record(0, 1, b""), ""])


def manifest(image):
    packed = pack(image)
    banks = []
    for bank in range(8):
        items = {a: b for a, b in image.items() if a >> 16 == bank}
        if not items:
            continue
        start, end = min(items), max(items)
        # Address/byte pairs pin holes and identities, not just concatenated bytes.
        digest = hashlib.sha256(b"".join(a.to_bytes(4, "big") + bytes((items[a],))
                                        for a in sorted(items))).hexdigest()
        banks.append({"bank": bank, "first": identity(start), "last": identity(end),
                      "bytes": len(items), "addressed_sha256": digest})
    return {"format": "cc2530-banked-v1", "capability": "offline-banked-abi-fixture",
            "physical_code_end_exclusive": CODE_END, "bytes": len(packed), "banks": banks}


def verify_files(output):
    linked = parse_ihex((output / "banked.ihx").read_text(encoding="ascii"))
    require(parse_ihex((output / "banked.hex").read_text(encoding="ascii")) == pack(linked),
            "Packed physical HEX differs from linked banked image")
    require(json.loads((output / "banked-layout.json").read_text(encoding="ascii")) == manifest(linked),
            "Banked layout identity differs from linked image")
    return linked


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    linked = parse_ihex((args.output / "banked.ihx").read_text(encoding="ascii"))
    physical = pack(linked)
    (args.output / "banked.hex").write_text(ihex(physical), encoding="ascii")
    (args.output / "banked-layout.json").write_text(
        json.dumps(manifest(linked), indent=2) + "\n", encoding="ascii")
    verify_files(args.output)


if __name__ == "__main__":
    main()
