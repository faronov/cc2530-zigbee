#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Execute the standalone R22 APS Data codec tests without radio or USB."""

import argparse
from pathlib import Path

from boot_image import (
    ALIAS, check_pc, section, simulate, snapshot, snapshot_commands, verify_component_layout,
)
from verify_firmware import STATUS_ADDRESS, parse_ihex, parse_symbols, require


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--simulator", default="s51")
    args = parser.parse_args()
    path = args.output / "aps_frame_test.ihx"
    image = parse_ihex(path.read_text(encoding="ascii"))
    symbols = parse_symbols(path.with_suffix(".map").read_text())
    allocated = verify_component_layout(
        image, symbols, path.with_suffix(".cdb").read_text(), path.with_suffix(".mem").read_text(),
        "aps_frame_test_result", ("aps_frame.c", "test_aps_frame.c"),
    )
    stop = symbols["_aps_frame_test_done"]
    require(image.get(stop) == 0
            and all(symbols.get(name) in image for name in ("_aps_frame_decode", "_aps_frame_encode")),
            "APS frame codec/checkpoint is absent or changed")
    text = simulate(args.simulator, [
        ALIAS, "fill xram 0 0x1eff 0xa5",
        f"run 0 {symbols['_main']:#x}", "fill iram 0x80 0xff 0xc7",
        f"run {symbols['_main']:#x} {stop:#x}",
    ] + snapshot_commands(1), path)
    check_pc(section(text, 1), stop)
    ram, iram, sfr = snapshot(text, 1)
    require(ram[STATUS_ADDRESS:STATUS_ADDRESS + 6] == b"APF1\x01\x08", "APS frame result ABI mismatch")
    failure = int.from_bytes(ram[STATUS_ADDRESS + 6:STATUS_ADDRESS + 8], "little")
    require(failure == 0, f"SDCC APS frame test failed at C source line {failure}")
    require(all(value == 0xa5 for address, value in enumerate(ram) if address not in allocated),
            "APS frame test wrote outside allocated XDATA")
    require(sfr[1] == symbols["s_SSEG"] - 1 and iram[128:] == b"\xc7" * 128,
            "APS frame test crossed the upper IRAM guard or failed to unwind")
    print(f"APS frame: {len(image)} CODE bytes; strict linked layout, actual codec vectors "
          "and alias/XDATA/stack guards PASS (simulation only).")


if __name__ == "__main__":
    main()
