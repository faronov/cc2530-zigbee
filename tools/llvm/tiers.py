#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""FAST / SMOKE / FULL validation tiers for the experimental LLVM-MCS51 CC2530 image.

  fast   compiler lit (or identity-bound lit evidence), semantic simulator matrices,
         upstream runtime verifiers, bank-call ABI matrix, 45-TU firmware build+link,
         thunk/stack-argument/static-stack/fixed-address/generic-pointer proofs and
         resource admission. No firmware join simulation.
  smoke  fast + short firmware scenarios (smoke_cases) + a bounded case-0 prefix.
         The prefix is a regression check and never claims commissioning.
  full   fast + every admitted scenario run to completion, including complete
         case 0 through authenticated READY/serving. Case 0 cannot be deselected.

Every run writes OUTPUT/receipt.json (self-hashed) with the exact test selection,
per-step status (PASS/FAIL/TIMEOUT/CANCELLED/NOT-RUN/SUSPENDED), durations and the
content identity of every input. See docs/LLVM_DEVELOPMENT.md.
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import signal
import subprocess
import sys
import threading
import time

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
PROFILE_PATH = HERE / "profile.json"
SCHEMA = "cc2530-llvm-tier-receipt/1"
TIERS = ("fast", "smoke", "full")
# Firmware build variants (build.sh FIRMWARE_VARIANT). rx-diag is the opt-in
# CC2530_RX_DIAG diagnostic image: a distinct identity, never production.
VARIANTS = ("production", "rx-diag", "parent-recovery")
# Content that can never change generated code or acceptance: excluded from the
# firmware identity so documentation edits do not invalidate evidence.
NON_EVIDENCE = re.compile(r"(^docs/|\.md$|^\.github/|^LICENSES?/|^\.gitignore$)")
EXIT = {"PASS": 0, "FAIL": 1, "TIMEOUT": 124, "CANCELLED": 130, "SUSPENDED": 75}
LAYOUT = HERE / "ref/join_smoke_layout.h"


def sha_bytes(raw):
    return hashlib.sha256(raw).hexdigest()


def sha_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def digest(obj):
    return sha_bytes(json.dumps(obj, sort_keys=True, separators=(",", ":")).encode())


def git(cwd, *args):
    return subprocess.run(["git", *args], cwd=cwd, capture_output=True, text=True, check=True).stdout


def first_line(cmd):
    try:
        return subprocess.run(cmd, capture_output=True, text=True, timeout=30).stdout.splitlines()[0].strip()
    except (OSError, IndexError, subprocess.SubprocessError):
        return None


# ---------------------------------------------------------------- identity
def tree_identity(repo):
    files = {}
    for name in git(repo, "ls-files", "-z").split("\0"):
        if not name or NON_EVIDENCE.search(name):
            continue
        path = repo / name
        files[name] = ("link:" + os.readlink(path)) if path.is_symlink() else (
            sha_file(path) if path.is_file() else "missing")
    # The tier/proof logic itself, tracked or not yet committed.
    for path in sorted(HERE.rglob("*")):
        if path.is_file() and "__pycache__" not in path.parts:
            files["tools/llvm/" + str(path.relative_to(HERE))] = sha_file(path)
    return files


def firmware_identity(repo):
    files = tree_identity(repo)
    return {"commit": git(repo, "rev-parse", "HEAD").strip(),
            "dirty": bool(git(repo, "status", "--porcelain", "--untracked-files=no").strip()),
            "files": len(files), "tree": digest(files)}


def compiler_identity(tools, llvm_src, profile):
    tools, llvm_src = Path(tools), Path(llvm_src)
    sources = {s: sha_file(llvm_src / s) for s in profile["compiler_sources"]}
    for path in sorted((llvm_src / "llvm/test/CodeGen/8051/Inputs").glob("*-runtime.c")):
        sources[str(path.relative_to(llvm_src))] = sha_file(path)
    dirty = git(llvm_src, "status", "--porcelain", "--untracked-files=no").strip()
    return {"binaries": {t: sha_file(tools / t) for t in profile["compiler_tools"]},
            "version": first_line([str(tools / "clang"), "--version"]),
            "llvm_commit": git(llvm_src, "rev-parse", "HEAD").strip(),
            "llvm_diff": sha_bytes(git(llvm_src, "diff", "HEAD").encode()) if dirty else None,
            "sources": sources}


def simulator_identity(s51):
    s51 = Path(s51).resolve()
    ident = {"binary": sha_file(s51), "build": None}
    for parent in list(s51.parents)[:6]:
        if (parent / "build.json").is_file():
            build = json.loads((parent / "build.json").read_text())
            ident["build"] = {k: build.get(k) for k in ("archive", "profile", "source_changes", "builder")}
            break
    return ident


def host_identity(python):
    elftools = subprocess.run([python, "-c", "import elftools;print(elftools.__version__)"],
                              capture_output=True, text=True).stdout.strip() or None
    return {"python": first_line([python, "--version"]), "elftools": elftools,
            "host_cc": first_line([os.environ.get("HOST_CC", "cc"), "--version"]),
            "machine": platform.machine()}


def identity(args, profile):
    ident = {"profile": digest(profile),
             "flags": {"abi": profile["abi_flags"], "sf": profile["sf_flags"]},
             **({"variant": args.variant} if args.variant != "production" else {}),
             "compiler": compiler_identity(args.tools, args.llvm_src, profile),
             "firmware": firmware_identity(REPO),
             "simulator": simulator_identity(args.simulator),
             "host": host_identity(args.python)}
    ident["digest"] = digest({k: v for k, v in ident.items() if k != "firmware"} |
                             {"firmware": ident["firmware"]["tree"]})
    return ident


# ---------------------------------------------------------------- steps
class Step:
    def __init__(self, name, proves, cmd=None, func=None, deps=(), timeout=900, env=None, cwd=None,
                 check=None, suspendable=False):
        self.name, self.proves, self.cmd, self.func = name, proves, cmd, func
        self.deps, self.timeout, self.env, self.cwd = tuple(deps), timeout, env, cwd
        self.check, self.suspendable = check, suspendable

    def describe(self):
        return {"name": self.name, "proves": self.proves, "deps": list(self.deps), "timeout": self.timeout,
                "command": [str(c) for c in self.cmd] if self.cmd else None,
                "internal": None if self.cmd else self.func.__name__}


def jsonl_all_pass(path, expected):
    def check():
        rows = [json.loads(line) for line in Path(path).read_text().splitlines() if line.startswith("{")]
        ok = len(rows) == expected and all(r["verdict"] == "PASS" for r in rows)
        return ok, {"cases": len(rows), "passed": sum(r["verdict"] == "PASS" for r in rows), "expected": expected}
    return check


def verifiers_pass(path, expected):
    def check():
        rows = [line for line in Path(path).read_text().splitlines() if line.strip()]
        good = [r for r in rows if " rc=0 " in r]
        return len(rows) == expected and len(good) == expected, {"runs": len(rows), "passed": len(good)}
    return check


def matrix_pass(log, expected):
    def check():
        found = re.search(r"== \S+: (\d+)/(\d+) pass", Path(log).read_text())
        ok = bool(found) and int(found[1]) == int(found[2]) == expected
        return ok, {"passed": int(found[1]) if found else 0, "expected": expected}
    return check


def lit_pass(log, profile):
    def check():
        text = Path(log).read_text()
        passed = re.search(r"Passed\s*:\s*(\d+)", text)
        failed = re.search(r"Failed\s*:\s*(\d+)", text)
        excluded = re.search(r"Excluded\s*:\s*(\d+)", text)
        ok = bool(passed) and not failed and "Unresolved" not in text
        return ok, {"passed": int(passed[1]) if passed else 0, "failed": int(failed[1]) if failed else 0,
                    "excluded_tests": int(excluded[1]) if excluded else 0,
                    "known_failures_excluded": profile["lit"]["known_failures"]}
    return check


def admission(out, profile):
    def admit():
        a = Path(out) / "fw/audit"
        load = lambda name: json.loads((a / name).read_text())
        thunks, stackarg, stack, proofs = load("thunks.json"), load("stackarg.json"), load("stack.json"), load("proofs.json")
        measure, hexj = load("measure.json"), load("hex.json")
        gp = json.loads((a / "gp-sites.txt").read_text().strip().splitlines()[-1])
        objects = len(list((Path(out) / "fw/obj").glob("*.o")))
        rules = {
            "incorrect_bank_thunks == 0": thunks["bad"] == [],
            "stack_argument_entry_violations == 0": stackarg["summary"]["violations"] == 0,
            "static_stack_proof PASS": stack["verdict"] == "PASS",
            f"stack_demand <= {profile['admission']['stack_capacity']}":
                stack["demand"] <= min(stack["capacity"], profile["admission"]["stack_capacity"]),
            "startup/runtime/memory/fixed-address/SFR proofs PASS": proofs["verdict"] == "PASS" and not proofs["failures"],
            "generic_pointer_sites all OK": gp["sites"] > 0 and gp["ok"] == gp["sites"],
            "no image bytes in reserved NV/lock region": hexj["bytes_at_or_above_reserved"] == 0,
            f"translation_units == {profile['admission']['object_count']}": objects == profile["admission"]["object_count"],
        }
        ref = profile["reference"]
        details = {"rules": rules,
                   "measured": {"code": measure["code"], "common": measure["common"], "xdata_bss": measure["bss"],
                                "xdata_data": measure["data"], "pdata_bss": measure["mcs51_pdata_bss"],
                                "bank_free": measure["bank_free"], "stack_demand": stack["demand"],
                                "stack_capacity": stack["capacity"], "thunks": len(thunks["thunks"]),
                                "gp_sites": gp["sites"], "elf_sha256": hexj["elf_sha256"],
                                "hex_sha256": hexj["hex_sha256"]},
                   "delta_vs_reference": {"code": measure["code"] - ref["code"], "common": measure["common"] - ref["common"],
                                          "stack_demand": stack["demand"] - ref["stack_demand"]}}
        return all(rules.values()), details
    return admit


def sim_check(report, case, terminal, limit=None):
    def check():
        r = json.loads(Path(report).read_text())
        cap = PROFILE["admission"]["stack_capacity"]
        if limit is None:
            ok = (r.get("complete") is True and r.get("steps") == r.get("of_steps") and r.get("terminal") == terminal
                  and r.get("stack_bytes", 999) <= cap)
            kind = "complete scenario"
        else:
            ok = r.get("complete") is False and r.get("steps") == limit and r.get("stack_bytes", 999) <= cap
            kind = f"bounded prefix ({limit} of {r.get('of_steps')} steps); NOT a commissioning proof"
        keep = ("complete", "steps", "of_steps", "peripheral_stops", "terminal", "stack_bytes", "peak_sp", "seconds")
        return ok, {"case": case, "kind": kind, "expected_terminal": terminal if limit is None else None,
                    **{k: r.get(k) for k in keep}}
    return check


def plan(tier, args, profile):
    """Return (steps, unselected) for a tier. FULL always contains complete case 0."""
    out = Path(args.output)
    py = args.python
    env = {"TOOLS": str(args.tools), "LLVM_SRC": str(args.llvm_src), "S51": str(args.simulator),
           "MCS51_SIM": str(args.simulator), "PYTHON": py, "JOBS": str(args.jobs),
           "ABI_FLAGS": profile["abi_flags"], "SF_FLAGS": profile["sf_flags"],
           "FIRMWARE_VARIANT": args.variant,
           "MCS51_CLANG": str(Path(args.tools) / "clang"), "MCS51_LLD": str(Path(args.tools) / "ld.lld"),
           "MCS51_OBJCOPY": str(Path(args.tools) / "llvm-objcopy")}
    flags = f"{profile['abi_flags']} {profile['sf_flags']}"
    steps, unselected = [], {}
    steps.append(Step("tier-unit-tests", "tier selection, identity and stale-evidence rules",
                      cmd=[py, "-B", "-m", "unittest", "-q", "test_tiers"], cwd=HERE, timeout=300))
    if args.llvm_build:
        steps.append(Step("compiler-tree", "lit build tree is up to date and built from --llvm-src",
                          func=compiler_tree(args), timeout=600))
        known = profile["lit"]["known_failures"]
        lit_src = configured_source(args.llvm_build)
        cmd = [str(Path(args.llvm_build) / "bin/llvm-lit"), "-s", "--filter", profile["lit"]["filter"]]
        if known:
            cmd += ["--filter-out", "|".join(re.escape(k) for k in known)]
        cmd += [str(lit_src / p) for p in profile["lit"]["paths"]]
        steps.append(Step("lit", "MCS51 codegen/MC/lld regression tests", cmd=cmd, deps=["compiler-tree"],
                          timeout=1200, check=lit_pass(out / "logs/lit.log", profile)))
    elif args.lit_evidence:
        steps.append(Step("lit", "lit PASS imported from a receipt bound to this exact compiler",
                          func=lit_evidence(args, profile), timeout=60))
    elif tier == "full":
        raise SystemExit("FULL requires compiler lit: pass --llvm-build or --lit-evidence")
    else:
        unselected["lit"] = "no --llvm-build/--lit-evidence: compiler lit is owned by the compiler CI"
    for label, extra in (("default", ""), ("profile", flags)):
        steps.append(Step(f"semsim-{label}", f"generic-pointer/ABI semantic simulation (13 cases, {label} ABI)",
                          cmd=["bash", HERE / "semsim/matrix.sh"],
                          env=env | {"OUT": str(out / f"semsim-{label}"), "SEM_EXTRA": extra},
                          check=jsonl_all_pass(out / f"semsim-{label}/semsim-receipt.jsonl", 13)))
        steps.append(Step(f"bankmatrix-{label}", f"cross-bank call/return ABI matrix in 8-bank FMAP model ({label} ABI)",
                          cmd=[py, "-B", HERE / "bankmatrix/run.py", args.tools, f"bank-{label}"],
                          env=env | {"MATRIX_OUT": str(out / "bankmatrix"), "MATRIX_FLAGS": extra},
                          timeout=300, check=matrix_pass(out / f"logs/bankmatrix-{label}.log", 14)))
    steps.append(Step("runtime-verifiers", "upstream generic-pointer/arith32/div64 runtime verifiers x O0/O2/Os/Oz",
                      cmd=["bash", HERE / "semsim/run_up_verifiers.sh", args.llvm_src, Path(args.tools).parent,
                           out / "verifiers.txt"], env=env, check=verifiers_pass(out / "verifiers.txt", 12)))
    steps.append(Step("firmware-build", "45-TU firmware compile + static-frame link (fails on assertion/compile/link/bank overflow)",
                      cmd=["bash", HERE / "build.sh", out / "fw"], env=env, timeout=1800))
    steps.append(Step("firmware-audit", "machine-code audits: thunks, stack args, stack proof, HEX, fixed-address, generic pointers",
                      cmd=["bash", HERE / "audit.sh", out / "fw"], env=env, deps=["firmware-build"], timeout=1800))
    steps.append(Step("admission", "resource/ABI/stack/fixed-address admission of the linked image",
                      func=admission(out, profile), deps=["firmware-audit"], timeout=60))
    if tier == "fast":
        unselected["simulator"] = "FAST never runs firmware join scenarios"
        return steps, unselected
    sim = profile["simulator"]
    if tier == "full" and (0 not in sim["full_cases"] or sim["terminals"].get("0") != "serving"):
        raise SystemExit("FULL must contain complete case 0 through 'serving'; refusing a weakened profile")
    order = sim["full_cases"] + sim.get("variant_cases", {}).get(args.variant, []) if tier == "full" else sim["smoke_cases"]
    cases = sorted(set(order if tier == "full" else order + [sim["smoke_prefix"]["case"]]))
    steps.append(Step("vectors", "deterministic host reference transcripts for the selected scenarios",
                      cmd=[py, "-B", HERE / "tiers.py", "vectors", "--output", out / "vectors",
                           "--cases", ",".join(map(str, cases)), "--variant", args.variant], timeout=900))
    elf = out / "fw/link/hw1.elf"
    replay_env = env | {"SIM_TIMEOUT": str(args.sim_timeout), "PYTHONUNBUFFERED": "1",
                        "LLVM_SIM_DIAG": str(out / "diag")}
    for case in order:
        term = sim["terminals"][str(case)]
        cmd = [py, "-B", HERE / "llvm_join_replay.py", "--elf", elf, "--vectors", out / "vectors",
               "--simulator", args.simulator, "--case", str(case), "--report", out / f"sims/case{case}.json"]
        e, suspend = dict(replay_env), False
        if tier == "full" and case == 0 and args.case0_budget:
            snap = out / "case0-snap"
            e |= {"SNAP_DIR": str(snap), "SNAP_EVERY": "10", "STOP_AFTER_SECONDS": str(args.case0_budget)}
            suspend = True
        steps.append(Step(f"sim-case{case}", f"complete MCU replay of scenario {case} to '{term}'"
                          + (" (authenticated READY/serving)" if case == 0 else ""),
                          cmd=cmd, env=e, deps=["admission", "vectors"], timeout=args.case_timeout,
                          check=sim_check(out / f"sims/case{case}.json", case, term), suspendable=suspend))
    if tier == "smoke":
        p = sim["smoke_prefix"]
        steps.append(Step(f"sim-case{p['case']}-prefix", f"bounded case-{p['case']} prefix ({p['limit']} steps): "
                          "boot, XMAP banking, MAC RX/TX start; regression only, NOT commissioning",
                          cmd=[py, "-B", HERE / "llvm_join_replay.py", "--elf", elf, "--vectors", out / "vectors",
                               "--simulator", args.simulator, "--case", str(p["case"]), "--limit", str(p["limit"]),
                               "--report", out / f"sims/case{p['case']}-prefix.json"],
                          env=replay_env, deps=["admission", "vectors"], timeout=1800,
                          check=sim_check(out / f"sims/case{p['case']}-prefix.json", p["case"], None, p["limit"])))
        unselected["complete case 0"] = "SMOKE never runs complete commissioning; use FULL"
        unselected["AES/flash security path"] = "first reached at case-0 step 395 and in case 10; FULL only"
    else:
        steps.append(Step("sim-summary", "all admitted scenarios complete with expected terminal and stack <= 128",
                          cmd=[py, "-B", HERE / "sim_summary.py", out / "sims", out / "sims/summary.json", args.variant],
                          deps=[f"sim-case{c}" for c in order], timeout=60))
    return steps, unselected


def configured_source(build):
    cache = (Path(build) / "CMakeCache.txt").read_text()
    found = re.search(r"^LLVM_MAIN_SRC_DIR:\w+=(.*)$", cache, re.M) or \
        re.search(r"^LLVM_SOURCE_DIR:\w+=(.*)$", cache, re.M)
    if not found:
        raise SystemExit("cannot read the configured LLVM source from CMakeCache.txt")
    return Path(found[1]).parent


def compiler_tree(args):
    def check():
        build, src = Path(args.llvm_build), configured_source(args.llvm_build)
        details = {"configured_source": str(src), "llvm_src": str(args.llvm_src)}
        same = git(src, "rev-parse", "HEAD") == git(args.llvm_src, "rev-parse", "HEAD") and \
            git(src, "diff", "HEAD") == git(args.llvm_src, "diff", "HEAD")
        details["same_revision"] = same
        tools_match = all(sha_file(build / "bin" / t) == sha_file(Path(args.tools) / t)
                          for t in PROFILE["compiler_tools"])
        details["tools_are_build_outputs"] = tools_match
        up_to_date = None
        maker = re.search(r"^CMAKE_MAKE_PROGRAM:\w+=(.*)$", (build / "CMakeCache.txt").read_text(), re.M)
        if (build / "build.ninja").is_file():
            out = subprocess.run([maker[1] if maker else "ninja", "-C", str(build), "-n", "clang", "lld", "llvm-objcopy", "llvm-ar"],
                                 capture_output=True, text=True).stdout
            up_to_date = "no work to do" in out
        details["build_up_to_date"] = up_to_date
        return same and tools_match and up_to_date is not False, details
    check.__name__ = "compiler_tree"
    return check


def lit_evidence(args, profile):
    def check():
        receipt = load_receipt(args.lit_evidence)
        mine = compiler_identity(args.tools, args.llvm_src, profile)
        theirs = receipt["identity"]["compiler"]
        lit = receipt["steps"].get("lit", {})
        same = all(mine[k] == theirs.get(k) for k in ("binaries", "llvm_commit", "llvm_diff"))
        known = receipt["profile"]["lit"]["known_failures"] == profile["lit"]["known_failures"]
        ok = lit.get("status") == "PASS" and same and known
        return ok, {"receipt": str(args.lit_evidence), "receipt_sha256": receipt["receipt_sha256"],
                    "same_compiler": same, "same_known_failures": known, "lit": lit.get("details")}
    check.__name__ = "lit_evidence"
    return check


# ---------------------------------------------------------------- runner
class Runner:
    def __init__(self, steps, out, jobs, keep_going, carried=None):
        self.steps = {s.name: s for s in steps}
        self.out, self.jobs, self.keep_going = Path(out), jobs, keep_going
        self.results = dict(carried or {})
        self.procs, self.lock, self.stop = {}, threading.Lock(), threading.Event()
        self.cancel_reason = None

    def cancel(self, reason):
        with self.lock:
            if not self.stop.is_set():
                self.cancel_reason = reason
                self.stop.set()
            for proc in list(self.procs.values()):
                try:
                    os.killpg(proc.pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass

    def execute(self, step):
        log = self.out / "logs" / f"{step.name}.log"
        start = time.time()
        result = {"status": None, "seconds": None, "log": str(log.relative_to(self.out))}
        try:
            if self.stop.is_set():
                result["status"], result["reason"] = "CANCELLED", self.cancel_reason
                return result
            if step.func:
                ok, details = step.func()
                result["status"], result["details"] = ("PASS" if ok else "FAIL"), details
                log.write_text(json.dumps(details, indent=1, default=str) + "\n")
                return result
            env = os.environ | (step.env or {})
            with log.open("w") as handle:
                proc = subprocess.Popen([str(c) for c in step.cmd], cwd=step.cwd, env=env, stdout=handle,
                                        stderr=subprocess.STDOUT, start_new_session=True)
                with self.lock:
                    self.procs[step.name] = proc
                try:
                    rc = proc.wait(timeout=step.timeout)
                except subprocess.TimeoutExpired:
                    os.killpg(proc.pid, signal.SIGKILL)
                    proc.wait()
                    result["status"], result["reason"] = "TIMEOUT", f"exceeded {step.timeout}s"
                    return result
                finally:
                    with self.lock:
                        self.procs.pop(step.name, None)
            result["returncode"] = rc
            if rc != 0 and self.stop.is_set():
                result["status"], result["reason"] = "CANCELLED", self.cancel_reason
            elif rc == EXIT["SUSPENDED"] and step.suspendable:
                result["status"] = "SUSPENDED"
            elif rc != 0:
                result["status"] = "FAIL"
                result["tail"] = log.read_text(errors="replace")[-1500:]
            elif step.check:
                ok, details = step.check()
                result["status"], result["details"] = ("PASS" if ok else "FAIL"), details
            else:
                result["status"] = "PASS"
            return result
        except Exception as error:  # a broken proof is a failure, never a pass
            result["status"], result["reason"] = "FAIL", f"{type(error).__name__}: {error}"
            with log.open("a") as handle:
                handle.write(result["reason"] + "\n")
            return result
        finally:
            result["seconds"] = round(time.time() - start, 1)

    def run(self, progress=print):
        for sub in ("logs", "sims"):
            (self.out / sub).mkdir(parents=True, exist_ok=True)
        pending = [n for n in self.steps if n not in self.results]
        running = {}
        with concurrent.futures.ThreadPoolExecutor(max_workers=self.jobs) as pool:
            while pending or running:
                scheduled = False
                for name in list(pending):
                    deps = [self.results.get(d, {}).get("status") for d in self.steps[name].deps]
                    if any(d not in self.steps for d in self.steps[name].deps):
                        self.results[name] = {"status": "FAIL", "reason": "unknown dependency"}
                        pending.remove(name)
                    elif any(s in ("SUSPENDED", "PENDING") for s in deps):
                        self.results[name] = {"status": "PENDING", "reason": "waits for a suspended step"}
                        pending.remove(name)
                    elif any(s not in (None, "PASS") for s in deps):
                        self.results[name] = {"status": "NOT-RUN", "reason": "dependency did not pass"}
                        pending.remove(name)
                    elif all(s == "PASS" for s in deps):
                        if self.stop.is_set():
                            self.results[name] = {"status": "CANCELLED", "reason": self.cancel_reason}
                        else:
                            running[pool.submit(self.execute, self.steps[name])] = name
                        pending.remove(name)
                        scheduled = True
                if not running:
                    if pending and not scheduled:
                        raise RuntimeError("unsatisfiable step graph: " + ", ".join(pending))
                    continue
                done, _ = concurrent.futures.wait(running, return_when=concurrent.futures.FIRST_COMPLETED)
                for future in done:
                    name = running.pop(future)
                    self.results[name] = future.result()
                    status = self.results[name]["status"]
                    progress(f"[{status:9}] {name} ({self.results[name]['seconds']}s)")
                    if status in ("FAIL", "TIMEOUT") and not self.keep_going:
                        self.cancel(f"fail-fast after {name} {status}")
        return self.results


def overall(results, interrupted):
    statuses = [r["status"] for r in results.values()]
    if interrupted:
        return "CANCELLED"
    if "FAIL" in statuses:
        return "FAIL"
    if "TIMEOUT" in statuses:
        return "TIMEOUT"
    if "SUSPENDED" in statuses and all(s in ("PASS", "SUSPENDED", "PENDING") for s in statuses):
        return "SUSPENDED"
    if all(s == "PASS" for s in statuses):
        return "PASS"
    return "FAIL"


# ---------------------------------------------------------------- receipts
def seal(receipt):
    body = {k: v for k, v in receipt.items() if k != "receipt_sha256"}
    return receipt | {"receipt_sha256": digest(body)}


def load_receipt(path):
    receipt = json.loads(Path(path).read_text())
    if receipt.get("schema") != SCHEMA:
        raise ValueError("not a tier receipt")
    if seal(receipt)["receipt_sha256"] != receipt.get("receipt_sha256"):
        raise ValueError("receipt content does not match its seal (edited or truncated)")
    return receipt


def reusable(receipt, ident, selection, tier):
    """Why a sealed receipt can (None) or cannot (reason) stand in for a run now."""
    if receipt["tier"] != tier:
        return f"receipt is for tier {receipt['tier']}"
    if receipt["status"] != "PASS":
        return f"receipt status is {receipt['status']}"
    if receipt["identity"]["digest"] != ident["digest"]:
        changed = [k for k in ident if k != "digest" and receipt["identity"].get(k) != ident[k]]
        return "input identity differs: " + ", ".join(changed or ["digest"])
    if receipt["selection_digest"] != selection:
        return "test selection or proof contract differs"
    if tier == "full":
        c0 = receipt["steps"].get("sim-case0", {})
        if c0.get("status") != "PASS" or (c0.get("details") or {}).get("terminal") != "serving":
            return "complete case 0 is not PASS in the receipt"
    return None


def selection_digest(steps, unselected, args):
    def norm(value):
        return str(value).replace(str(Path(args.output)), "$OUT").replace(str(HERE), "$TOOLS_LLVM")
    return digest({"steps": [{"name": s.name, "deps": s.deps, "command": [norm(c) for c in s.cmd] if s.cmd else
                              s.func.__name__, "env": {k: norm(v) for k, v in (s.env or {}).items()
                                                       if k in ("SEM_EXTRA", "MATRIX_FLAGS", "ABI_FLAGS", "SF_FLAGS")}}
                             for s in steps], "unselected": unselected})


def resume_state(out, ident, selection, steps):
    """Carry PASS steps of a SUSPENDED FULL run with the same identity; re-verify its artifacts."""
    prior = json.loads((out / "progress.json").read_text())
    if prior["identity"]["digest"] != ident["digest"] or prior["selection_digest"] != selection:
        raise SystemExit("--resume: output belongs to another identity or selection")
    if prior["steps"].get("sim-case0", {}).get("status") != "SUSPENDED":
        raise SystemExit("--resume: the previous segment did not suspend complete case 0")
    admitted = prior["steps"]["admission"]["details"]["measured"]["elf_sha256"]
    if sha_file(out / "fw/link/hw1.elf") != admitted:
        raise SystemExit("--resume: firmware ELF changed since admission")
    for case, want in json.loads((out / "vectors/vectors.json").read_text())["vectors"].items():
        if sha_file(out / f"vectors/v{case}.json") != want:
            raise SystemExit(f"--resume: vector v{case}.json changed")
    carried = {k: v | {"carried_from_segment": v.get("carried_from_segment", prior["segment"])}
               for k, v in prior["steps"].items() if v["status"] == "PASS"}
    snap = Path(json.loads((out / "sims/case0.json").read_text())["resume"])
    if snap.parent != out / "case0-snap" or not snap.is_file():
        raise SystemExit("--resume: missing case-0 snapshot")
    for s in steps:
        if s.name == "sim-case0":
            s.env = s.env | {"RESUME": str(snap)}
    return carried


# ---------------------------------------------------------------- vectors
def vectors(output, cases, variant="production"):
    """Build the host reference model with the Makefile's own join-smoke recipe."""
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    scratch = output / "build"
    target = scratch / "host-join-smoke-vectors"
    text = subprocess.run(["make", "-s", "-n", "-B", "BOARD=lg_esl29_rev03", "JOIN_SMOKE_KEY_MODE=default-tc",
                           f"JOIN_SMOKE_PARENT_RECOVERY={int(variant == 'parent-recovery')}",
                           f"BUILD={scratch}", str(target)], cwd=REPO, capture_output=True, text=True, check=True).stdout
    lines = text.replace("\\\n", " ").splitlines()
    recipe = [line for line in lines if line.rstrip().endswith(f"-o {target}")]
    if len(recipe) != 1:
        raise SystemExit("cannot find the unique host-join-smoke-vectors recipe in the Makefile")
    cmd = shlex.split(recipe[0])
    layout = f"-I{scratch}/join-smoke-layout"
    if cmd.count(layout) != 1:
        raise SystemExit("unexpected join-smoke layout include")
    cmd[cmd.index(layout)] = f"-I{LAYOUT.parent}"
    scratch.mkdir(exist_ok=True)
    (output / "command.txt").write_text(shlex.join(cmd) + "\n")
    subprocess.run(cmd, cwd=REPO, check=True)
    report = {"layout_header_sha256": sha_file(LAYOUT), "recipe_sha256": sha_bytes(shlex.join(cmd).encode()),
              "variant": variant, "vectors": {}}
    for case in cases:
        raw = subprocess.run([str(target), str(case)], capture_output=True, check=True, timeout=120).stdout
        vector = json.loads(raw)
        if vector.get("case") != case:
            raise SystemExit(f"host model produced the wrong scenario for case {case}")
        (output / f"v{case}.json").write_bytes(raw)
        report["vectors"][case] = sha_bytes(raw)
    # Bind modeled cold boots to flash written by the actual initial caller.
    for initial, recovery in ((0, 16), (0, 17), (20, 18), (20, 19)):
        if initial in cases and recovery in cases:
            first = json.loads((output/f"v{initial}.json").read_bytes())
            second = json.loads((output/f"v{recovery}.json").read_bytes())
            if first["steps"][-1]["nv"] != second["nv_initial"]:
                raise SystemExit(f"case {recovery} cold NV differs from case {initial} durable result")
    (output / "vectors.json").write_text(json.dumps(report, indent=1) + "\n")
    print(json.dumps(report, indent=1))


# ---------------------------------------------------------------- main
def main(argv=None):
    if argv is None:
        argv = sys.argv[1:]
    if argv[:1] == ["vectors"]:
        p = argparse.ArgumentParser(prog="tiers.py vectors")
        p.add_argument("--output", required=True)
        p.add_argument("--cases", required=True)
        p.add_argument("--variant", choices=VARIANTS, default="production")
        a = p.parse_args(argv[1:])
        vectors(a.output, [int(c) for c in a.cases.split(",")], a.variant)
        return 0
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--tier", choices=TIERS, required=True)
    p.add_argument("--tools", type=Path, required=True, help="LLVM-MCS51 bin directory (clang, ld.lld, ...)")
    p.add_argument("--llvm-src", type=Path, required=True, help="LLVM-MCS51 source checkout of that compiler")
    p.add_argument("--llvm-build", type=Path, help="configured build tree: run compiler lit")
    p.add_argument("--lit-evidence", type=Path, help="sealed receipt whose lit PASS is bound to the same compiler")
    p.add_argument("--simulator", type=Path, required=True, help="s51 from tools/llvm/prepare_simulator.py")
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--variant", choices=VARIANTS, default="production",
                   help="firmware variant; rx-diag = diagnostics; parent-recovery = extended durable storage")
    p.add_argument("--python", default=sys.executable)
    p.add_argument("--jobs", type=int, default=min(os.cpu_count() or 4, 16))
    p.add_argument("--keep-going", action="store_true", help="do not cancel remaining steps after a failure")
    p.add_argument("--sim-timeout", type=int, default=6000, help="per s51 process (seconds)")
    p.add_argument("--case-timeout", type=int, default=12 * 3600, help="per complete scenario (seconds)")
    p.add_argument("--case0-budget", type=int, help="FULL: suspend complete case 0 after this many seconds "
                   "at a bound snapshot (exit 75); rerun with --resume on the same --output to continue")
    p.add_argument("--resume", action="store_true", help="FULL: continue a SUSPENDED run in --output "
                   "(same identity and selection only; carried PASS steps are re-verified)")
    p.add_argument("--evidence", type=Path, help="sealed-receipt store: reuse an identical PASS, store new PASS")
    p.add_argument("--plan-only", action="store_true", help="print the selection and identity; run nothing")
    a = p.parse_args(argv)
    a.output = a.output.resolve()
    a.tools, a.llvm_src, a.simulator = a.tools.resolve(), a.llvm_src.resolve(), a.simulator.resolve()
    if a.llvm_build:
        a.llvm_build = a.llvm_build.resolve()
    for label, path in (("--tools", a.tools / "clang"), ("--llvm-src", a.llvm_src), ("--simulator", a.simulator)):
        if not path.exists():
            p.error(f"{label}: {path} does not exist")
    if (a.case0_budget or a.resume) and a.tier != "full":
        p.error("--case0-budget/--resume only apply to FULL")
    profile = json.loads(PROFILE_PATH.read_text())
    steps, unselected = plan(a.tier, a, profile)
    selection = selection_digest(steps, unselected, a)
    ident = identity(a, profile)
    header = {"schema": SCHEMA, "tier": a.tier, "identity": ident, "selection_digest": selection,
              "selection": [s.describe() for s in steps], "unselected": unselected, "profile": profile}
    if a.plan_only:
        print(json.dumps(header, indent=1, default=str))
        return 0
    carried = {}
    if a.evidence:
        for path in sorted(a.evidence.glob(f"{a.tier}-{ident['digest'][:16]}-*.json")):
            try:
                old = load_receipt(path)
            except (ValueError, KeyError, json.JSONDecodeError) as error:
                print(f"evidence {path.name}: rejected ({error})")
                continue
            why = reusable(old, ident, selection, a.tier)
            if why is None:
                a.output.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(path, a.output / "receipt.json")
                print(f"{a.tier.upper()}: REUSED sealed PASS {path} (identity {ident['digest'][:16]}, "
                      f"receipt {old['receipt_sha256'][:16]}); nothing was re-executed")
                return 0
            print(f"evidence {path.name}: not reusable ({why})")
    if a.resume:
        carried = resume_state(a.output, ident, selection, steps)
    if a.output.exists() and any(a.output.iterdir()) and not carried:
        raise SystemExit(f"refusing to reuse non-empty output {a.output}")
    a.output.mkdir(parents=True, exist_ok=True)
    print(f"{a.tier.upper()}: {len(steps)} steps, identity {ident['digest'][:16]}, selection {selection[:16]}")
    for name, why in unselected.items():
        print(f"  not selected: {name} -- {why}")
    runner = Runner(steps, a.output, a.jobs, a.keep_going, carried)
    interrupted = []

    def on_signal(signum, _frame):
        interrupted.append(signum)
        runner.cancel(f"interrupted by signal {signum}")
    for sig in (signal.SIGINT, signal.SIGTERM):
        signal.signal(sig, on_signal)
    start = time.time()
    results = runner.run()
    status = overall(results, interrupted)
    segment = 1 + max((r.get("carried_from_segment", 0) for r in carried.values()), default=0)
    if a.tier == "full":
        (a.output / "progress.json").write_text(json.dumps({"identity": ident, "selection_digest": selection, "segment": segment,
                                     "steps": results}, indent=1, default=str) + "\n")
    receipt = seal(header | {"status": status, "segment": segment, "seconds": round(time.time() - start, 1),
                             "finished_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
                             "steps": results, "counts": {k: sum(r["status"] == k for r in results.values())
                                                          for k in sorted({r["status"] for r in results.values()})}})
    (a.output / "receipt.json").write_text(json.dumps(receipt, indent=1, default=str) + "\n")
    if a.evidence and status == "PASS":
        a.evidence.mkdir(parents=True, exist_ok=True)
        (a.evidence / f"{a.tier}-{ident['digest'][:16]}-{receipt['receipt_sha256'][:16]}.json").write_text(
            json.dumps(receipt, indent=1, default=str) + "\n")
    print(f"{a.tier.upper()}: {status} in {receipt['seconds']}s  {receipt['counts']}  receipt {a.output / 'receipt.json'}")
    return EXIT[status]


PROFILE = json.loads(PROFILE_PATH.read_text())

if __name__ == "__main__":
    sys.exit(main())
