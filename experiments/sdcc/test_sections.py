#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Measure area retention versus archive-member extraction with stock ASxxxx."""
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from verify_firmware import parse_ihex


def run(*args, cwd):
    subprocess.run(args, cwd=cwd, check=True)


def main():
    directory = ROOT / "build/sdcc-study/sections-test"
    directory.mkdir(parents=True, exist_ok=True)
    sources = {
        "root": ".globl _live\n.area HOME (CODE)\n_entry:\n lcall _live\n sjmp _entry\n",
        "live": ".globl _live\n.area CF_live (CODE)\n_live:\n mov dpl,#7\n ret\n",
        "dead": ".globl _dead\n.area CF_dead (CODE)\n_dead:\n mov dpl,#9\n ret\n",
    }
    sources["together"] = sources["live"] + sources["dead"]
    for name, text in sources.items():
        (directory / f"{name}.asm").write_text(text)
        run("sdas8051", "-plosgff", f"{name}.rel", f"{name}.asm", cwd=directory)
    # The members are regenerated above; remove only this test's stale archive.
    (directory / "functions.lib").unlink(missing_ok=True)
    run("sdar", "-rc", "functions.lib", "live.rel", "dead.rel", cwd=directory)
    run("sdld", "-n", "-i", "-m", "areas", "root.rel", "together.rel", cwd=directory)
    run("sdld", "-n", "-i", "-m", "archive", "root.rel",
        "-k", ".", "-l", "functions.lib", cwd=directory)
    results = {name: len(parse_ihex((directory / f"{name}.ihx").read_text()))
               for name in ("areas", "archive")}
    if results != {"areas": 13, "archive": 9}:
        raise AssertionError(f"Unexpected linker extraction semantics: {results}")
    (directory / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    print(json.dumps(results))


if __name__ == "__main__":
    main()
