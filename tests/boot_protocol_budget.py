#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Run the complete offline protocol chain and write its checked resource ledger."""

import argparse
import hashlib
import json
import re
from pathlib import Path

from boot_image import (
    ALIAS, check_pc, section, simulate, snapshot, snapshot_commands, verify_component_layout,
)
from protocol_resources import MODULE_BUDGETS, XDATA_BUDGET, build_report
from verify_firmware import STATUS_ADDRESS, parse_ihex, parse_symbols, require
from zcl_write_proof import load_and_verify


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--simulator", default="s51")
    args = parser.parse_args()
    path = args.output / "protocol_budget_test.ihx"
    report_path = args.output / "protocol-resources.json"
    report_path.unlink(missing_ok=True)
    image = parse_ihex(path.read_text(encoding="ascii"))
    symbols = parse_symbols(path.with_suffix(".map").read_text())
    debug = load_and_verify(args.output, "protocol_budget_test", image, symbols)
    sources = tuple(name + ".c" for name in MODULE_BUDGETS if name != "protocol_budget_test") + ("test_protocol_budget.c",)
    allocated = verify_component_layout(
        image, symbols, debug, path.with_suffix(".mem").read_text(),
        "protocol_budget_result", sources, xdata_budget=XDATA_BUDGET,
    )
    stop = symbols["_protocol_budget_done"]
    require(image.get(stop) == 0 and all(symbols.get(name) in image for name in (
        "_mac_frame_encode", "_mac_frame_decode", "_nwk_frame_encode", "_nwk_frame_decode",
        "_aps_frame_encode", "_aps_frame_decode", "_zcl_frame_encode", "_zcl_frame_decode",
        "_zcl_value_encode", "_zcl_value_decode", "_zcl_read_attrs_unicast", "_zcl_dispatch_unicast",
        "_zcl_wr_handle",
    )), "Integrated protocol symbol/checkpoint absent or changed")
    objects = {name: (args.output / (name + ".rel")).read_text(encoding="ascii") for name in MODULE_BUDGETS}
    text = simulate(args.simulator, [
        ALIAS, "fill xram 0 0x1eff 0xa5",
        f"run 0 {symbols['_main']:#x}", "fill iram 0x80 0xff 0xc7",
        f"run {symbols['_main']:#x} {stop:#x}",
    ] + snapshot_commands(1), path)
    check_pc(section(text, 1), stop)
    ram, iram, sfr = snapshot(text, 1)
    require(ram[STATUS_ADDRESS:STATUS_ADDRESS + 6] == b"PBG1\x01\x08", "Integrated result ABI mismatch")
    failure = int.from_bytes(ram[STATUS_ADDRESS + 6:STATUS_ADDRESS + 8], "little")
    require(failure == 0, f"SDCC integrated protocol test failed at source line {failure}")
    require(all(value == 0xa5 for address, value in enumerate(ram) if address not in allocated),
            "Integrated protocol wrote outside allocated XDATA")
    require(sfr[1] == symbols["s_SSEG"] - 1 and iram[128:] == b"\xc7" * 128,
            "Integrated protocol crossed upper IRAM or failed to unwind")
    require(all(sfr[address - 0x80] == 0 for address in (0xa8, 0xb8, 0x9a)),
            "Integrated protocol enabled interrupts")
    full = simulate(args.simulator, [
        ALIAS, "fill xram 0 0x1eff 0xa5", f"run 0 {stop:#x}",
    ] + snapshot_commands(1), path)
    check_pc(section(full, 1), stop)
    peaks = re.findall(r"Max value of stack pointer=\s*0x([0-9a-f]+)", section(full, 1))
    require(len(peaks) == 1, "Missing or ambiguous simulator stack-peak evidence")
    report = build_report(image, symbols, objects, int(peaks[0], 16))
    report["artifacts_sha256"] = {
        name: hashlib.sha256((args.output / name).read_bytes()).hexdigest()
        for name in ("protocol_budget_test.ihx", "protocol_budget_test.map", "protocol_budget_test.cdb",
                     "protocol_budget_test.mem", *(name + ".rel" for name in MODULE_BUDGETS))
    }
    report_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="ascii")
    totals = report["linked"]
    print(f"Integrated MAC/NWK/APS/ZCL: {totals['code']} CODE, {totals['ordinary_xdata']} ordinary XDATA "
          f"+ {totals['status_reserved']} reserved; stack start={totals['stack_start']:#x}, "
          f"observed peak={totals['observed_peak_sp']:#x}, guard headroom={totals['observed_headroom_to_guard']}. "
          "Golden exchanges, per-module budgets and alias/XDATA/stack guards PASS (simulation only).")
    print(f"Resource ledger: {report_path} (not a board image, CI artifact or full-stack fit claim).")


if __name__ == "__main__":
    main()
