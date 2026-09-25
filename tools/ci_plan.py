#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Conservative CI selection from actual Make compiler inputs; no device access."""
import argparse
from functools import lru_cache
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys

from verify_firmware import BOARDS, IMAGES, ROOT


COMPONENTS = {
    "compositions": ("Offline composed services ({board})", (
        "test-mac-radio", "test-mac-stamp", "test-zcl-temperature",
        "test-mac-join", "test-zdo-node", "test-zdo-srv")),
    "mac-attempt": ("Offline MAC interval owner ({board})", ("test-mac-attempt",)),
    "security": ("Offline Zigbee security ({board})",
                 ("test-zigbee-security", "test-zigbee-mmo", "test-zigbee-key-hash")),
    "counters": ("Offline durable counters ({board})", ("test-security-counter",)),
    **{f"resident-{p}": (f"Offline resident crypto NV ({{board}}, {p})",
                        (f"test-security-resident-{p}",))
       for p in ("security", "mmo", "key-hash", "counter")},
    **{f"ed-{p}": (f"Host authenticated ED ({{board}}, {p})", (f"test-{p}",))
       for p in ("ed-wire", "security-keys", "bdb-join")},
    "banking": ("Offline banked CODE ({board})", ("test-banked",)),
    "banked-security": ("Offline banked key lifecycle ({board})", ("test-banked-security",)),
    **{f"banked-join-{case}": (f"Offline complete MCU join ({{board}}, {case})",
                             (f"test-banked-join-{case}",))
       for case in ("success", "missing-key", "radio-fault", "flash-fault",
                    "rx-queues", "wrap-quarantine", "ack-correlation", "ack-deadlines",
                    "zdo-server", "broadcast-table", "address-map", "update-full",
                    "install-timeout", "node-correlation", "node-timeout", "node-status",
                    "tc-key-timeout", "tc-confirm-timeout", "parent-status",
                    "network-key-late", "tc-key-late", "tc-confirm-late")},
}
COMMON_RUNTIME = {"src/startup.c", "src/status.c", "src/banked.c"}
SOURCE = re.compile(r"(?:src|tests|boards|examples)/[A-Za-z0-9_/-]+\.c\Z")


def environment():
    return {k: v for k, v in os.environ.items()
            if k not in ("MAKEFLAGS", "MFLAGS", "MAKELEVEL")}


def command(args, **kwargs):
    return subprocess.run(args, cwd=ROOT, env=environment(), text=True,
                          capture_output=True, check=True, timeout=60, **kwargs).stdout


def units():
    return {**{name: targets for name, (_, targets) in COMPONENTS.items()},
            "core": ("test-common-core",),
            **{image: ("all", "test-board") for image in IMAGES}}


@lru_cache(maxsize=None)
def recipe(board, unit, build="build/ci-plan"):
    image = unit if unit in IMAGES else "debug_fixture" if unit == "core" else "bringup"
    text = command(["make", "--no-print-directory", "-n", "-B", "-j1",
                    "HOST_CC=cc", "SDCC=sdcc", "PYTHON=python3",
                    f"BOARD={board}", f"IMAGE={image}", f"BUILD={build}", *units()[unit]])
    return tuple(tuple(shlex.split(line)) for line in text.replace("\\\n", " ").splitlines() if line.strip())


def source_index():
    index = {}
    for board in BOARDS:
        for unit in units():
            commands = recipe(board, unit)
            compile_commands = [args for args in commands if args[0] in ("cc", "sdcc")]
            inputs = {arg for args in compile_commands for arg in args if SOURCE.fullmatch(arg)}
            if not inputs:
                raise ValueError(f"No compiler inputs found for {board}/{unit}")
            directories = {ROOT, ROOT / "include", ROOT / "tests"}
            for args in compile_commands:
                for i, arg in enumerate(args):
                    if arg.startswith("-I"):
                        directories.add(ROOT / (arg[2:] or args[i + 1]))
            generated = {(ROOT/args[args.index("--emit-header")+1]).resolve()
                         for args in commands
                         if "tests/verify_banked_join.py" in args and "--emit-header" in args}
            index[board, unit] = include_closure(inputs, directories, generated)
    return index


def include_closure(inputs, directories, generated=frozenset()):
    pending = [ROOT / p for p in inputs]
    seen = set()
    while pending:
        source = pending.pop().resolve()
        if not source.is_relative_to(ROOT):
            raise ValueError("Include dependency escapes the repository")
        if source in seen:
            continue
        seen.add(source)
        for line in source.read_text().replace("\\\n", "").splitlines():
            if "??=" in line:
                raise ValueError("Unreviewed trigraph include syntax")
            if not re.search(r"#\s*(?:/\*.*?\*/\s*)*include\b", line):
                continue
            match = re.fullmatch(r'\s*#\s*include\s*([<"])([A-Za-z0-9_./-]+)[>"]'
                                 r'\s*(?://.*|/\*.*\*/\s*)?', line)
            if not match:
                raise ValueError(f"Unreviewed include syntax in {source.relative_to(ROOT)}")
            candidates = {(d / match[2]).resolve() for d in (*directories, source.parent)}
            if any(not p.is_relative_to(ROOT) for p in candidates):
                raise ValueError("Include dependency escapes the repository")
            found = {p for p in candidates if p.is_file() and p not in generated}
            if not found and match[1] == '"' and not candidates & generated:
                raise ValueError(f"Missing project include: {match[2]}")
            pending.extend(found - seen)
    return {p.relative_to(ROOT).as_posix() for p in seen}


def rows(selected, tools=False):
    result = []
    for board in BOARDS:
        for image in IMAGES:
            core = image == "debug_fixture" and (board, "core") in selected
            host_tools = tools and board == "generic" and image == "bringup"
            if (board, image) in selected or core or host_tools:
                result.append(dict(name=f"Offline board fixture ({board}, {image})",
                                   board=board, image=image, directory=image, tools=host_tools,
                                   coverage=False, targets="all test-board" +
                                   (" test-common-core" if core else "")))
        for unit, (label, targets) in COMPONENTS.items():
            if (board, unit) in selected:
                result.append(dict(name=label.format(board=board), board=board, image="",
                                   directory=unit, tools=False,
                                   coverage=board == "generic" and unit == "ed-bdb-join",
                                   targets=" ".join(targets)))
    return result


def full_plan(reason):
    selected = {(b, u) for b in BOARDS for u in units()}
    return dict(tier="full", reason=reason, campaign="full",
                matrix={"include": rows(selected, tools=True)})


def select(paths, index=None):
    paths = set(paths)
    if not paths or all(Path(p).suffix == ".md" for p in paths):
        return dict(tier="docs", reason="Only documentation or no changed files",
                    campaign="deferred", matrix={"include": []})
    for path in sorted(paths):
        if Path(path).suffix == ".md":
            continue
        if not SOURCE.fullmatch(path) or path in COMMON_RUNTIME or path.startswith("boards/"):
            return full_plan(f"Shared/build/header/verifier/runtime or unknown input: {path}")
    if index is None:
        try:
            index = source_index()
        except (ValueError, OSError, subprocess.SubprocessError) as error:
            detail = error.stderr if isinstance(error, subprocess.CalledProcessError) else ""
            return full_plan(f"Dependency evidence unavailable; FULL required: {error} {detail or ''}")
    selected = set()
    for path in sorted(paths):
        if Path(path).suffix == ".md":
            continue
        consumers = {key for key, inputs in index.items() if path in inputs}
        if not consumers:
            return full_plan(f"No verified Make consumers for {path}")
        selected.update(consumers)
    return dict(tier="affected", reason="All actual Make compiler-input consumers",
                campaign="deferred", matrix={"include": rows(selected)})


def changed_paths(base, head=None):
    base = command(["git", "rev-parse", "--verify", "--end-of-options", base + "^{commit}"]).strip()
    args = ["git", "diff", "--name-only", "--no-renames", "-z", base]
    if head is not None:
        head = command(["git", "rev-parse", "--verify", "--end-of-options", head + "^{commit}"]).strip()
        if head != command(["git", "rev-parse", "HEAD"]).strip():
            raise ValueError("Dependency selection requires the checked-out head")
        args.append(head)
    paths = set(filter(None, command(args + ["--"]).split("\0")))
    if head is None:
        paths.update(filter(None, command(
            ["git", "ls-files", "--others", "--exclude-standard", "-z"]).split("\0")))
    return paths


def accepted_base(repository, base):
    try:
        result = subprocess.run(
            ["gh", "run", "list", "--repo", repository, "--workflow", "ci.yml",
             "--commit", base, "--status", "success", "--limit", "20",
             "--json", "headSha,headBranch,event,conclusion"],
            cwd=ROOT, env=environment(), text=True, capture_output=True, timeout=60,
        )
    except subprocess.TimeoutExpired:
        print("Baseline acceptance lookup timed out; selecting full checks", file=sys.stderr)
        return False
    if result.returncode:
        print("Baseline acceptance lookup failed; selecting full checks:\n" + result.stderr,
              file=sys.stderr)
        return False
    return any(run["headSha"] == base and run["headBranch"] == "main" and
               run["conclusion"] == "success" and
               run["event"] in ("push", "workflow_dispatch", "schedule")
               for run in json.loads(result.stdout))


def github_plan(event_name, event, head, repository):
    if event_name in ("schedule", "workflow_dispatch", "release"):
        return full_plan(f"Explicit full trigger: {event_name}")
    if event_name == "push":
        base = event.get("before", "")
    elif event_name == "pull_request":
        base = event["pull_request"]["base"]["sha"]
    else:
        raise ValueError(f"Unsupported CI event: {event_name}")
    if not re.fullmatch(r"[0-9a-f]{40}", base) or base == "0" * 40:
        return full_plan("No previous commit baseline")
    if not accepted_base(repository, base):
        return full_plan("Previous/base commit is not an accepted main CI baseline")
    return select(changed_paths(base, head))


def check_result(plan_result, worker_result, count):
    if type(count) is not int or not 0 <= count <= 256:
        raise ValueError("Invalid selected worker count")
    if plan_result != "success":
        raise ValueError(f"Selection/policy job did not succeed: {plan_result}")
    expected = "success" if count else "skipped"
    if worker_result != expected:
        raise ValueError(f"Expected worker result {expected}, got {worker_result}")


def run_fast(plan):
    subprocess.run([sys.executable, "-B", "tools/check_repository.py"], cwd=ROOT, check=True)
    total = 0
    for row in plan["matrix"]["include"]:
        unit = row["directory"]
        build = f"build/fast/{row['board']}/{unit}"
        planned = list(recipe(row["board"], unit, build))
        if "test-common-core" in row["targets"].split():
            planned += recipe(row["board"], "core", build)
        native = list(dict.fromkeys(args for args in planned
                                   if args[0].startswith(build + "/host-")))
        if any(len(args) != 1 for args in native):
            raise ValueError("Unreviewed native recipe arguments; run the affected target explicitly")
        if not native:
            print(f"No direct host test for {row['name']}; target acceptance still required")
            continue
        subprocess.run(["make", "--no-print-directory", "-B", "-j1", f"BOARD={row['board']}",
                        f"IMAGE={row['image'] or 'bringup'}", f"BUILD={build}",
                        *(args[0] for args in native)], cwd=ROOT, env=environment(), check=True)
        for args in native:
            subprocess.run(args, cwd=ROOT, env=environment(), check=True, timeout=60)
            total += 1
    if plan["matrix"]["include"] and not total:
        raise ValueError("No host tests executed; use the listed affected target checks")
    print(f"Fast checks: {total} native/sanitizer runs; NOT target-image/simulator acceptance")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", default="HEAD")
    parser.add_argument("--head")
    parser.add_argument("--tier", choices=("fast", "affected", "full"), default="affected")
    parser.add_argument("--github", action="store_true")
    parser.add_argument("--check-result", nargs=3, metavar=("PLAN", "WORKERS", "COUNT"))
    args = parser.parse_args()
    if args.check_result:
        check_result(*args.check_result[:2], int(args.check_result[2]))
        return
    if args.github:
        event = json.loads(Path(os.environ["GITHUB_EVENT_PATH"]).read_text())
        plan = github_plan(os.environ["GITHUB_EVENT_NAME"], event,
                           os.environ["GITHUB_SHA"], os.environ["GITHUB_REPOSITORY"])
    else:
        plan = full_plan("Explicit full selection") if args.tier == "full" else select(
            changed_paths(args.base, args.head))
    entries = plan["matrix"]["include"]
    print(json.dumps(plan, indent=2))
    if args.github:
        with Path(os.environ["GITHUB_OUTPUT"]).open("a") as output:
            for key, value in (("matrix", json.dumps(plan["matrix"], separators=(",", ":"))),
                               ("count", str(len(entries))), ("campaign", plan["campaign"])):
                output.write(f"{key}={value}\n")
        with Path(os.environ["GITHUB_STEP_SUMMARY"]).open("a") as summary:
            summary.write(f"## Check selection\n\nTier: **{plan['tier']}**, "
                          f"**{len(entries)} workers**. {plan['reason']}\n\n")
            summary.write("Banked artifact campaign: " + plan["campaign"] + ".\n")
            for row in entries:
                summary.write(f"- {row['name']}: `{row['targets']}`\n")
    elif args.tier == "fast":
        run_fast(plan)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        detail = error.stderr if isinstance(error, subprocess.CalledProcessError) else ""
        raise SystemExit(f"Check selection failed: {error}\n{detail or ''}") from error
