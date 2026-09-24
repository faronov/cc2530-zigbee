#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Fresh GCC host branch evidence for the real wire/key/join corpus; no uploads."""
import argparse
import gzip
import json
import os
from pathlib import Path
import subprocess
import tempfile

from ci_plan import environment
from verify_firmware import BOARDS, ROOT


PROFILES = ("ed-wire", "security-keys", "bdb-join")
CORE = ("ed_wire", "security_keys", "nwk_aps", "zdo_runtime", "bdb_join")


def merge(reports):
    files = {}
    for report in reports:
        for item in report["files"]:
            source = Path(item["file"])
            if not source.is_absolute():
                source = Path(report["current_working_directory"]) / source
            source = source.resolve()
            if source.parent != ROOT / "src":
                continue
            name = source.relative_to(ROOT).as_posix()
            lines = files.setdefault(name, {})
            for entry in item["lines"]:
                number, count = entry["line_number"], entry["count"]
                branches = entry.get("branches", [])
                counts = [branch["count"] for branch in branches]
                if type(number) is not int or number <= 0 or any(
                        type(n) is not int or n < 0 for n in [count, *counts]):
                    raise ValueError("Invalid gcov line/branch counts")
                shape = [(b["fallthrough"], b["throw"]) for b in branches]
                if number not in lines:
                    lines[number] = {"count": count, "branches": counts, "shape": shape}
                else:
                    old = lines[number]
                    if old["shape"] != shape:
                        raise ValueError(f"Incompatible branch layouts for {name}:{number}")
                    old["count"] += count
                    old["branches"] = [a + b for a, b in zip(old["branches"], counts)]
    result = {}
    for name, lines in sorted(files.items()):
        result[name] = {
            "lines": len(lines), "lines_hit": sum(line["count"] > 0 for line in lines.values()),
            "branches": sum(len(line["branches"]) for line in lines.values()),
            "branches_hit": sum(n > 0 for line in lines.values() for n in line["branches"]),
            "uncovered_lines": sorted(n for n, line in lines.items() if not line["count"]),
            "uncovered_branches": {str(n): [i for i, count in enumerate(line["branches"]) if not count]
                                   for n, line in sorted(lines.items()) if 0 in line["branches"]},
        }
    for name in CORE:
        metrics = result.get(f"src/{name}.c", {})
        if not metrics.get("lines_hit") or not metrics.get("branches_hit"):
            raise ValueError(f"No genuine coverage for {name}")
    return result


def run(output, board):
    output.mkdir(parents=True, exist_ok=True)
    env = environment()
    for key in ("GCOV_PREFIX", "GCOV_PREFIX_STRIP", "GCOV_ERROR_FILE", "GCOV_EXIT_AT_ERROR"):
        env.pop(key, None)
    compiler = subprocess.run(["gcc", "-dumpfullversion"], text=True, capture_output=True,
                              check=True).stdout.strip()
    with tempfile.TemporaryDirectory(prefix="gcov-", dir=output.resolve()) as directory:
        build = Path(directory)
        targets = [str(build / f"host-{p}-tests") for p in PROFILES]
        flags = ("-std=c99 -O0 -g --coverage -fprofile-abs-path -Wall -Wextra -Werror -pedantic "
                 "-DCC2530_HOST_TEST $(DEFINES) -Itests")
        subprocess.run(["make", "--no-print-directory", "-j1", "HOST_CC=gcc", f"BOARD={board}",
                        f"BUILD={build}", f"HOST_FLAGS={flags}", *targets],
                       cwd=ROOT, env=env, check=True)
        for target in targets:
            subprocess.run([target], cwd=ROOT, env=env, check=True, timeout=60)
        objects = sorted(build.glob("*.gcno"))
        if not objects:
            raise ValueError("Compiler produced no coverage objects")
        subprocess.run(["gcov", "--json-format", "--branch-probabilities", "--branch-counts",
                        *map(str, objects)], cwd=build, check=True, capture_output=True, text=True)
        reports = [json.loads(gzip.decompress(path.read_bytes()))
                   for path in sorted(build.glob("*.gcov.json.gz"))]
        if len(reports) != len(objects) or any(r.get("gcc_version") != compiler for r in reports):
            raise ValueError("Missing coverage reports or mismatched GCC/gcov")
        result = {"compiler": compiler, "board": board, "profiles": PROFILES, "files": merge(reports)}
    (output / "summary.json").write_text(json.dumps(result, indent=2) + "\n")
    text = ["## Host branch coverage", "",
            f"GCC {compiler}, {board}, real wire/key/join tests. Not path,8051 or hardware proof.",
            "", "| Production file | Lines hit/total | Branches hit/total |",
            "| --- | ---: | ---: |"]
    for name in CORE:
        item = result["files"][f"src/{name}.c"]
        text.append(f"| {name}.c | {item['lines_hit']}/{item['lines']} | "
                    f"{item['branches_hit']}/{item['branches']} |")
    rendered = "\n".join(text) + "\n"
    print(rendered)
    if "GITHUB_STEP_SUMMARY" in os.environ:
        with Path(os.environ["GITHUB_STEP_SUMMARY"]).open("a") as summary:
            summary.write(rendered)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--board", choices=BOARDS, default="generic")
    args = parser.parse_args()
    run(args.output, args.board)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        detail = error.stderr if isinstance(error, subprocess.CalledProcessError) else ""
        raise SystemExit(f"Host coverage failed: {error}\n{detail or ''}") from error
