#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Execute the isolated ZCL Read Attributes tests without RF."""

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
    path = args.output / "zcl_attributes_test.ihx"
    image = parse_ihex(path.read_text(encoding="ascii"))
    symbols = parse_symbols(path.with_suffix(".map").read_text())
    allocated = verify_component_layout(
        image, symbols, path.with_suffix(".cdb").read_text(), path.with_suffix(".mem").read_text(),
        "zcl_attributes_test_result", ("zcl_attributes.c", "zcl_frame.c", "zcl_value.c", "test_zcl_attributes.c"),
        xdata_budget=1024,
    )
    stop = symbols["_zcl_attributes_test_done"]
    require(image.get(stop) == 0 and all(symbols.get(name) in image for name in (
        "_zcl_read_attrs_unicast", "_zcl_frame_encode", "_zcl_frame_decode", "_zcl_value_encode",
    )), "ZCL attribute handler/codecs/checkpoint are absent or changed")
    text = simulate(args.simulator, [
        ALIAS, "fill xram 0 0x1eff 0xa5",
        f"run 0 {symbols['_main']:#x}", "fill iram 0x80 0xff 0xc7",
        f"run {symbols['_main']:#x} {stop:#x}",
    ] + snapshot_commands(1), path)
    check_pc(section(text, 1), stop)
    ram, iram, sfr = snapshot(text, 1)
    require(ram[STATUS_ADDRESS:STATUS_ADDRESS + 6] == b"ZCA1\x01\x08", "ZCL attribute result ABI mismatch")
    failure = int.from_bytes(ram[STATUS_ADDRESS + 6:STATUS_ADDRESS + 8], "little")
    require(failure == 0, f"SDCC ZCL attribute test failed at C source line {failure}")
    require(all(value == 0xa5 for address, value in enumerate(ram) if address not in allocated),
            "ZCL attribute test wrote outside allocated XDATA")
    require(sfr[1] == symbols["s_SSEG"] - 1 and iram[128:] == b"\xc7" * 128,
            "ZCL attribute test crossed the upper IRAM guard or failed to unwind")
    print(f"ZCL Read Attributes: {len(image)} CODE bytes; strict linked layout, actual vectors "
          "and alias/XDATA/stack guards PASS (simulation only).")


if __name__ == "__main__":
    main()
