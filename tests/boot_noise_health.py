#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Execute genuine binary health tests; no RF, sampler or entropy claim."""

import argparse
import hashlib
import json
import unittest
from pathlib import Path

from boot_image import (
    ALIAS, check_alias, check_pc, section, simulate, snapshot, snapshot_commands, verify_component_layout,
)
from verify_firmware import STATUS_ADDRESS, code_bytes, parse_ihex, parse_symbols, require


CODE_SHA = "9b6efe82ba9cf14bee408b0374b1e572cf10039e727db1985ddd4b471d93fc51"
CDB_SHA = "bbcac39afe0891f4d75d66c2f9319d523371997280d0a21795f747300d729505"
MAP_SHA = "ed55dac0b441e321f8d02a33e90b37eaba9b12fc794942a2002aec0cb64c20d0"


def verify(image, symbols, debug, memory):
    allocated = verify_component_layout(
        image, symbols, debug, memory,
        "noise_health_test_result", ("noise_health.c", "test_noise_health.c"),
    )
    require(len(image) <= 8192, "Noise health exceeds 8-KiB CODE budget")
    require(hashlib.sha256(code_bytes(image, 4880)).hexdigest() == CODE_SHA,
            "Noise health CODE identity changed")
    require(hashlib.sha256(debug.encode("ascii")).hexdigest() == CDB_SHA,
            "Noise health complete CDB identity changed")
    canonical = json.dumps(symbols, sort_keys=True, separators=(",", ":")).encode("ascii")
    require(hashlib.sha256(canonical).hexdigest() == MAP_SHA,
            "Noise health complete symbol identity changed")
    return allocated


def check_result(ram, iram, sfr, allocated, symbols):
    require(ram[STATUS_ADDRESS:STATUS_ADDRESS + 6] == b"NOH1\x01\x08",
            "Noise health result ABI mismatch")
    failure = int.from_bytes(ram[STATUS_ADDRESS + 6:STATUS_ADDRESS + 8], "little")
    require(failure == 0, f"SDCC noise health failed at C line {failure}")
    require(all(value == 0xa5 for address, value in enumerate(ram) if address not in allocated),
            "Noise health wrote outside allocated XDATA")
    require(sfr[1] == symbols["s_SSEG"] - 1 and iram[0x7d:] == b"\xc7" * 131,
            "Noise health crossed SP7C/alias guard or failed to unwind")


def artifact_negatives(image, symbols, debug, memory):
    case = unittest.TestCase()
    count = 0
    for name in ("_noise_health_start", "_noise_health_push", "_main",
                 "_noise_health_test_done", "__gptrput"):
        bad = dict(image)
        bad[symbols[name]] ^= 1
        with case.assertRaises(ValueError):
            verify(bad, symbols, debug, memory)
        count += 1
    for address in (0, len(image) - 1):
        bad = dict(image)
        del bad[address]
        with case.assertRaises(ValueError):
            verify(bad, symbols, debug, memory)
        count += 1
    for name in ("_noise_health_start", "_noise_health_push", "s_SSEG",
                 "l_XSEG", "_noise_health_test_result"):
        bad = dict(symbols)
        bad[name] += 1
        with case.assertRaises(ValueError):
            verify(image, bad, debug, memory)
        count += 1
    for prefix in ("F:", "S:", "L:", "T:"):
        line = next(line for line in debug.splitlines() if line.startswith(prefix))
        with case.assertRaises(ValueError):
            verify(image, symbols, debug.replace(line + "\n", "", 1), memory)
        count += 1
    with case.assertRaises(ValueError):
        verify(image, symbols, debug, memory.replace("223 bytes available", "222 bytes available"))
    return count + 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--simulator", default="s51")
    args = parser.parse_args()
    path = args.output / "noise_health_test.ihx"
    image = parse_ihex(path.read_text(encoding="ascii"))
    symbols = parse_symbols(path.with_suffix(".map").read_text())
    debug = path.with_suffix(".cdb").read_text()
    memory = path.with_suffix(".mem").read_text()
    allocated = verify(image, symbols, debug, memory)
    negatives = artifact_negatives(image, symbols, debug, memory)
    check_alias(args.simulator)
    with unittest.TestCase().assertRaises(ValueError):
        check_alias(args.simulator, False)
    stop = symbols["_noise_health_test_done"]
    text = simulate(args.simulator, [
        ALIAS, "fill xram 0 0x1eff 0xa5",
        f"run 0 {symbols['_main']:#x}", "fill iram 0x7d 0xff 0xc7",
        f"run {symbols['_main']:#x} {stop:#x}",
    ] + snapshot_commands(1), path)
    check_pc(section(text, 1), stop)
    ram, iram, sfr = snapshot(text, 1)
    check_result(ram, iram, sfr, allocated, symbols)
    for region, address in ((0, STATUS_ADDRESS), (0, STATUS_ADDRESS + 6),
                            (0, 0x1dff), (1, 0x7d), (2, 1)):
        bad = [bytearray(ram), bytearray(iram), bytearray(sfr)]
        bad[region][address] ^= 1
        with unittest.TestCase().assertRaises(ValueError):
            check_result(*bad, allocated, symbols)
    print(f"Noise health: {len(image)} CODE bytes; strict linked layout and actual RCT/APT "
          f"calls, {negatives} artifact + 5 snapshot + 1 alias negatives, "
          "XDATA/alias/SP7C guards PASS (simulation only; no entropy claim).")


if __name__ == "__main__":
    main()
