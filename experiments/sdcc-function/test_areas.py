#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Measure ordinary ASxxxx XDATA areas, relocation, accounting and CRT clearing."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from verify_firmware import parse_ihex


def run(command, cwd):
    start = time.perf_counter()
    result = subprocess.run(command, cwd=cwd, check=True, text=True, capture_output=True)
    return result.stdout, time.perf_counter() - start


def probe(root, source, count, simulator):
    root.mkdir(parents=True, exist_ok=False)
    run(["sdcc", "-mmcs51", "--model-large", "--debug", "-c",
         str(source), "-o", "main.rel"], root)
    names = [("foo", 3), ("bar", 4), ("baz", 2)]
    names += [(f"extra{i}", 1) for i in range(count - 3)]
    text = ".module frames\n"
    for name, size in names:
        text += (f".area XF_{name} (XDATA)\n"
                 f"G${name}_home$0_0$0 == .\n"
                 f"_{name}_home::\n.ds {size}\n")
    (root / "frames.asm").write_text(text)
    _, assembly_seconds = run(["sdas8051", "-plosgff", "frames.asm"], root)
    _, link_seconds = run(["sdcc", "-mmcs51", "--model-large", "--debug",
                          "--xram-loc", "0x100", "--xram-size", "0x2000", "-Wl-r", "-Wl-j",
                          "-o", "probe.ihx", "main.rel", "frames.rel"], root)
    noi = (root / "probe.noi").read_text()
    symbols = {k: int(v, 16) for k, v in re.findall(r"^DEF (\S+) 0x([0-9A-Fa-f]+)$", noi, re.M)}
    intervals = [(symbols["_"+name+"_home"], size) for name, size in names]
    assert all(b == a+s for (a, s), (b, _) in zip(intervals, intervals[1:]))
    assert symbols["l_XSEG"] == 1
    cdb = (root / "probe.cdb").read_text()
    rst = (root / "main.rst").read_text()
    image = parse_ihex((root / "probe.ihx").read_text())
    for name in ("foo", "bar", "baz"):
        address = symbols["_"+name+"_home"]
        assert symbols["G$"+name+"_home$0_0$0"] == address
        assert f"L:G${name}_home$0_0$0:{address:X}" in cdb
        operands = re.findall(r"^\s*([0-9A-F]{6}) 90 ([0-9A-F]{2}) ([0-9A-F]{2}).*"
                              r"mov\s+dptr,#_" + name + r"_home$", rst, re.M)
        assert len(operands) == 1
        pc, hi, lo = [int(v, 16) for v in operands[0]]
        assert (hi << 8) | lo == address
        assert bytes(image[pc+i] for i in range(3)) == bytes((0x90, hi, lo))
    memory = (root / "probe.mem").read_text()
    accounted = re.search(r"EXTERNAL RAM\s+0x[0-9a-f]+\s+0x[0-9a-f]+\s+(\d+)", memory)
    assert accounted and int(accounted[1]) == 1 + sum(s for _, s in names)
    commands = (f'file "{(root / "probe.ihx").resolve()}"\n'
                "fill xram 0 0xffff 0xa5\n"
                f"run 0 {symbols['_main']:#x}\n"
                f"dump /h xram {symbols['_ordinary']:#x} {symbols['_ordinary']:#x}\n"
                f"dump /h xram {intervals[0][0]:#x} {intervals[0][0]+2:#x}\nquit\n")
    result = subprocess.run([simulator, "-t", "C52", "-q", "-c", "-"], input=commands,
                            text=True, capture_output=True, timeout=15, check=True)
    (root / "startup.txt").write_text(result.stdout)
    lines = result.stdout.splitlines()
    ordinary = [l for l in lines if re.match(rf"^0x{symbols['_ordinary']:04x}\s", l)]
    custom = [l for l in lines if re.match(rf"^0x{intervals[0][0]:04x}\s", l)]
    assert len(ordinary) == len(custom) == 1, result.stdout
    assert ordinary[0].split()[1] == "00", ordinary
    assert custom[0].split()[1:4] == ["a5"] * 3, custom
    return {
        "custom_areas": count,
        "custom_bytes": sum(s for _, s in names),
        "first_custom_address": intervals[0][0],
        "last_custom_end": sum(intervals[-1]),
        "l_XSEG": symbols["l_XSEG"],
        "ordinary_address": symbols["_ordinary"],
        "relocated_pointer_operands": {
            n: symbols["_"+n+"_home"] for n in ("foo", "bar", "baz")
        },
        "assembly_seconds": assembly_seconds,
        "link_seconds": link_seconds,
        "sizes": {ext: (root / ("frames."+ext)).stat().st_size for ext in ("asm", "rel", "rst")},
        "memory_report": (root / "probe.mem").read_text(),
        "crt_clears_ordinary": True,
        "crt_clears_custom": False,
        "linked_operands_and_debug_verified": True,
        "memory_accounting_verified": True,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--simulator", required=True)
    args = parser.parse_args()
    source = Path(__file__).with_name("areas.c").resolve()
    args.output.mkdir(parents=True, exist_ok=False)
    result = [probe(args.output / str(count), source, count, args.simulator)
              for count in (3, 300, 1000, 2000)]
    (args.output / "results.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps([{k: v for k, v in r.items() if k != "memory_report"} for r in result], indent=2))


if __name__ == "__main__":
    main()
