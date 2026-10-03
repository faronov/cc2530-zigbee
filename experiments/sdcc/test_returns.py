#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Compile and execute all 256 inputs, including reentrant and call boundaries."""
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / "tools"), str(ROOT / "tests")]
from boot_image import ALIAS, memory_dump, simulate
from verify_firmware import parse_ihex, parse_symbols


def branch(x):
    if x < 7:
        return 13
    if x & 1:
        return x ^ 0x5a
    return (x + 3 if x > 90 else x - 7) & 255


def loop(x):
    y = 0
    while x:
        y = (y + x) & 255
        if x == 7:
            return y
        x -= 1
    return y


def main():
    output = ROOT / "build/sdcc-study/returns-test"
    output.mkdir(parents=True, exist_ok=True)
    oracle = bytes([branch(x) for x in range(256)] +
                   [x ^ 0xa5 for x in range(256)] +
                   [((x + 1) % 256 + (branch(x) if x & 2 else x ^ 0xa5)) % 256
                    for x in range(256)] + [loop(x) for x in range(256)])
    results = {}
    for name, flags in (
            ("baseline", []),
            ("local-returns", ["--peep-file", "experiments/sdcc/local-returns.peep"]),
            ("peep-return", ["--peep-return"]),
            ("omit-frame", ["--fomit-frame-pointer"]),
            ("both", ["--peep-return", "--fomit-frame-pointer"]),
            ("debug-last", ["--peep-return", "--debug"]),
            ("no-debug", [])):
        directory = output / name
        directory.mkdir(exist_ok=True)
        target = directory / "returns.ihx"
        args = ["sdcc", "-mmcs51", "--model-large", "--std-c99", "--opt-code-size",
                "--Werror", "--iram-size", "256", "--xram-size", "7680"]
        if name != "no-debug":
            args += ["--debug"]
        subprocess.run(args + flags + ["experiments/sdcc/returns_test.c", "-o", str(target)],
                       cwd=ROOT, check=True)
        symbols = parse_symbols(target.with_suffix(".map").read_text())
        text = simulate("s51", [ALIAS, f"run 0 {symbols['_done']}",
                               "dump /h xram 0x1000 0x13ff"], target)
        actual = memory_dump(text, 0x1000, 1024)
        if actual != oracle:
            raise AssertionError(f"{name}: target output differs from independent oracle")
        assembly = target.with_suffix(".asm").read_text()
        body = assembly.split("_caller:", 1)[1].split("_nested:", 1)[0]
        calls_leaf = re.search(r"\blcall\s+_leaf\b", body) is not None
        tailcalls_leaf = re.search(r"\bljmp\s+_leaf\b", body) is not None
        tail_enabled = name in ("peep-return", "both", "debug-last", "no-debug")
        if calls_leaf == tail_enabled or tailcalls_leaf != tail_enabled:
            raise AssertionError(f"{name}: unexpected CALL/tail-JMP boundary")
        results[name] = {"code": len(parse_ihex(target.read_text())),
                         "oracle_bytes": len(actual), "call_boundary_retained": calls_leaf}
    if results["baseline"]["code"] - results["local-returns"]["code"] != 4:
        raise AssertionError("Restricted rule did not save the expected four fixture bytes")
    (output / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    print(json.dumps(results))


if __name__ == "__main__":
    main()
