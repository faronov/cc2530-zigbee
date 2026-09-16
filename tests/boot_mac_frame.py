#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Run the isolated codec test image; never access a physical radio or debugger."""

import argparse
from pathlib import Path
import re

from boot_image import ALIAS, check_pc, marker, memory_dump, section, simulate
from verify_firmware import CODE_LIMIT, STATUS_ADDRESS, parse_ihex, parse_symbols, require, xdata_ranges


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--simulator", default="s51")
    args = parser.parse_args()
    image_path = args.output / "mac_frame_test.ihx"
    image = parse_ihex(image_path.read_text(encoding="ascii"))
    symbols = parse_symbols((args.output / "mac_frame_test.map").read_text(encoding="utf-8"))
    require(image and min(image) == 0 and max(image) < CODE_LIMIT, "Codec test CODE is not unbanked")
    require(image[0] == 2, "Codec test reset vector is not LJMP")
    require(symbols["_mac_test_result"] == STATUS_ADDRESS, "Codec result ABI address changed")
    debug = (args.output / "mac_frame_test.cdb").read_text(encoding="utf-8")
    sizes = re.findall(r"^S:G\$mac_test_result\$[^(\n]+\(\{(\d+)\}", debug, re.MULTILINE)
    require(sizes and all(int(size) == 8 for size in sizes), "Codec result debug ABI size changed")
    require(symbols["__XPAGE"] == 0x93, "Codec test must use CC2530 MPAGE")
    require(symbols["l_PSEG"] == symbols["l_XISEG"] == symbols["l_XABS"] == 0,
            "Codec simulation does not support paged storage/CRT copies or extra absolute areas")
    stop = symbols["_mac_codec_done"]
    require(image[stop] == 0, "Codec test stop is not a NOP")
    allocations = xdata_ranges(symbols)
    require(all(0 <= start <= end <= STATUS_ADDRESS for start, end in allocations),
            "Codec test allocation overlaps reserved status or the IRAM alias")
    allocated = set(range(STATUS_ADDRESS, STATUS_ADDRESS + 8))
    for start, end in allocations:
        require(not allocated.intersection(range(start, end)), "Overlapping codec test XDATA areas")
        allocated.update(range(start, end))
    require(8 <= symbols["s_SSEG"] < 0x80, "Codec test lacks a bounded lower IRAM stack")
    text = simulate(args.simulator, [
        ALIAS, "fill xram 0 0x1eff 0xa5",
        f"run 0 {symbols['_main']:#x}", "fill iram 0x80 0xff 0xc7",
        f"run {symbols['_main']:#x} {stop:#x}", marker(1), "state", "dump /h xram 0 0x1eff",
        marker(2), "dump /h iram 0 0xff", marker(3), "dump /h sfr 0x81 0x81", marker(4),
    ], image_path)
    check_pc(section(text, 1), stop)
    ram = memory_dump(section(text, 1), 0, 0x1F00)
    require(ram[STATUS_ADDRESS:STATUS_ADDRESS + 6] == b"MAC1\x01\x08", "Codec test result ABI mismatch")
    failure = int.from_bytes(ram[STATUS_ADDRESS + 6:STATUS_ADDRESS + 8], "little")
    require(failure == 0, f"SDCC codec self-test failed at C source line {failure}")
    require(all(value == 0xA5 for address, value in enumerate(ram) if address not in allocated),
            "Codec test wrote outside allocated nonaliased XDATA")
    iram = memory_dump(section(text, 2), 0, 256)
    sp = memory_dump(section(text, 3), 0x81, 1)[0]
    require(sp == symbols["s_SSEG"] - 1 and iram[128:] == b"\xC7" * 128,
            "Codec test crossed upper stack guard or failed to unwind")
    print("MAC codec: SDCC-linked golden/boundary vectors and alias/XDATA/stack guards PASS (simulation only).")


if __name__ == "__main__":
    main()
