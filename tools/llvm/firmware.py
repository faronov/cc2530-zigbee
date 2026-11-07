#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""LLVM-only standard build, using the existing firmware link and admission."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

import tiers

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]


def run(argv, **kwargs):
    subprocess.run([str(x) for x in argv], check=True, **kwargs)


def fixture_flags(board):
    return ["-target", "mcs51", "-mcpu=cc2530", "-std=c99", "-Os", "-fpack-struct=1",
            "-fno-function-sections", "-fno-data-sections", "-Itoolchain/clang/include",
            "-Iinclude", "-DCC2530_BOARD=" + ("0" if board == "generic" else "1")]


def fixture(args, out):
    flags = fixture_flags(args.board)
    modules = ["status", "board", "example"]
    sources = ["src/status.c", f"boards/{args.board}.c", f"examples/{args.image}.c"]
    if args.image == "timebase_fixture":
        modules += ["timebase_fixture_state", "timebase"]
        sources += ["src/timebase_fixture_state.c", "src/timebase.c"]
    clang = args.tools / "clang"
    for module, source in zip(modules, sources):
        run([clang, *flags, "-c", source, "-o", out / (module + ".o")], cwd=REPO)
    run([clang, "-target", "mcs51", "-mcpu=cc2530", "-c",
         HERE / "inputs/fixture_status_mcs51.s", "-o", out / "fixture_status.o"])
    ld = out / "cc2530-zigbee.ld"
    ld.write_text(f'INCLUDE "{args.llvm_src}/llvm/lib/Target/MCS51/cc2530.ld"\n'
                  'SECTIONS { .mcs51_xdata_fixed 0x1e00 (NOLOAD) : '
                  '{ KEEP(*(.mcs51.xdata.fixed.m0_status)) } > XRAM }\n'
                  'ASSERT(SIZEOF(.mcs51_xdata_fixed) == 32, "M0 status object size")\n'
                  'ASSERT(ADDR(.data) + SIZEOF(.data) <= 0x1e00, "data overlaps status")\n'
                  'ASSERT(ADDR(.bss) + SIZEOF(.bss) <= 0x1e00, "bss overlaps status")\n')
    elf, hexfile = out / "firmware.elf", out / "firmware.hex"
    run([clang, *flags, "-Wl,--no-check-sections", out / "fixture_status.o",
         "-Wl,--gc-sections", "-Wl,-T," + str(ld), *(out / (m + ".o") for m in modules),
         "-o", elf], cwd=REPO)
    run([args.tools / "llvm-objcopy", "--output-target=ihex", elf, hexfile])
    return elf, hexfile, {"scope": "bootstrap fixture; not full-join stack/admission qualification"}


def load_app(app_dir, board):
    """External application manifest (docs/EXTERNAL_APP.md) -> build.sh environment."""
    manifest = json.loads((app_dir / "cc2530-app.json").read_text())
    if manifest.get("schema") != "cc2530-app/1":
        raise SystemExit("cc2530-app.json: schema must be cc2530-app/1")
    defines, include, units = manifest.get("defines", []), manifest.get("include", []), manifest.get("units", {})
    if not all(isinstance(d, str) and re.fullmatch(r"-[DU][A-Za-z_][A-Za-z0-9_]*(=[A-Za-z0-9_]+)?", d) for d in defines):
        raise SystemExit("cc2530-app.json: defines must be plain -DNAME[=VALUE] or -UNAME flags")
    if any(re.search(r"CC2530_BOARD\b|CC2530_APP\b", d) for d in defines):
        raise SystemExit("cc2530-app.json: CC2530_BOARD and CC2530_APP come from --board/--app")
    safe = re.compile(r"[A-Za-z0-9_][A-Za-z0-9_./-]*")
    if not units or not all(isinstance(u, str) and isinstance(f, str) and safe.fullmatch(u) and safe.fullmatch(f)
                            and ".." not in f.split("/") for u, f in units.items()):
        raise SystemExit("cc2530-app.json: units must map stack unit names to relative application paths")
    if not all(isinstance(i, str) and safe.fullmatch(i) and ".." not in i.split("/") for i in include):
        raise SystemExit("cc2530-app.json: include entries must be relative application directories")
    flags = [*defines, "-DCC2530_APP", *("-Iapp/" + i for i in include)]
    if board == "external":
        flags += ["-UCC2530_BOARD", "-DCC2530_BOARD=2"]
    env = {"APP_DIR": str(app_dir), "APP_DEFINES": " ".join(flags),
           "APP_UNITS": " ".join(f"{u}={f}" for u, f in sorted(units.items()))}
    return manifest, env


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tools", type=Path, required=True)
    parser.add_argument("--llvm-src", type=Path, required=True)
    parser.add_argument("--llvm-revision", help="explicit full LLVM SHA for compiler CI; default: toolchain.json pin")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--board", choices=("generic", "lg_esl29_rev03", "external"), required=True)
    parser.add_argument("--image", choices=("join", "bringup", "timebase_fixture"), default="join")
    parser.add_argument("--variant", choices=tiers.VARIANTS + ("autonomous", "autonomous-pm2", "autonomous-pm2-sht3x", "app"), default="production")
    parser.add_argument("--app", type=Path, help="external application directory (cc2530-app.json); requires --variant app")
    args = parser.parse_args()
    args.tools, args.llvm_src, args.output = (p.resolve() for p in (args.tools, args.llvm_src, args.output))
    pin = json.loads((HERE / "toolchain.json").read_text())
    profile = json.loads((HERE / "profile.json").read_text())
    revision = pin["revision"] if args.llvm_revision is None else args.llvm_revision
    if not re.fullmatch(r"[0-9a-f]{40}", revision):
        parser.error("LLVM revision must be a full lowercase 40-character commit SHA")
    compiler = tiers.compiler_identity(args.tools, args.llvm_src, profile)
    if compiler["llvm_commit"] != revision or compiler["llvm_diff"] is not None:
        raise SystemExit("standard build requires the exact clean pinned or explicitly requested LLVM source revision")
    if revision not in compiler["version"]:
        raise SystemExit("compiler executable version does not identify the requested source revision")
    if args.image == "join" and args.board == "generic":
        raise SystemExit("the admitted join profile needs a real board; generic supports bringup/timebase_fixture")
    if (args.app is not None) != (args.variant == "app"):
        raise SystemExit("--app and --variant app go together")
    if args.board == "external" and args.app is None:
        raise SystemExit("the external board is supplied by an external application (--app)")
    app_manifest, app_env = load_app(args.app.resolve(), args.board) if args.app else (None, {})
    if args.image != "join" and args.variant != "production":
        raise SystemExit("non-production variants are supported only by the join image")
    identity = {"compiler": compiler, "firmware": tiers.firmware_identity(REPO),
                "expected_llvm_revision": revision,
                "paths": {"tools": str(args.tools), "llvm_src": str(args.llvm_src),
                          **{name: str((args.tools / name).resolve()) for name in profile["compiler_tools"]}},
                "board": args.board, "image": args.image, "variant": args.variant,
                "abi": profile["abi_flags"] if args.image == "join" else "",
                "static_frames": profile["sf_flags"] if args.image == "join" else "",
                "fixture_flags": fixture_flags(args.board) if args.image != "join" else None}
    if args.app:
        identity["app"] = {"manifest": app_manifest, "tree": tiers.firmware_identity(args.app.resolve())}
    generation = args.output / tiers.digest(identity)[:24]
    receipt_path = generation / "receipt.json"
    if generation.exists():
        receipt = json.loads(receipt_path.read_text())
        body = {k: v for k, v in receipt.items() if k != "receipt_sha256"}
        if (tiers.digest(body) != receipt["receipt_sha256"] or receipt["identity"] != identity
                or receipt["status"] != "PASS"
                or any(tiers.sha_file(Path(receipt[k])) != receipt[k + "_sha256"] for k in ("elf", "hex"))):
            raise SystemExit("existing build failed identity/seal/artifact verification; use a fresh BUILD")
    else:
        generation.mkdir(parents=True)
        if args.image == "join":
            env = os.environ | {"TOOLS": str(args.tools), "LLVM_SRC": str(args.llvm_src),
                                "ABI_FLAGS": profile["abi_flags"], "SF_FLAGS": profile["sf_flags"],
                                "FIRMWARE_VARIANT": args.variant, "PYTHON": sys.executable} | app_env
            run(["bash", HERE / "build.sh", generation / "fw"], env=env)
            run(["bash", HERE / "audit.sh", generation / "fw"], env=env)
            ok, admission = tiers.admission(generation, profile)()
            if not ok:
                raise SystemExit("firmware admission failed: " + json.dumps(admission))
            elf, hexfile = generation / "fw/link/hw1.elf", generation / "fw/audit/hw1.hex"
        else:
            elf, hexfile, admission = fixture(args, generation)
        receipt = {"schema": "cc2530-llvm-build/1", "status": "PASS", "identity": identity,
                   "admission": admission, "elf": str(elf), "hex": str(hexfile),
                   "elf_sha256": tiers.sha_file(elf), "hex_sha256": tiers.sha_file(hexfile)}
        receipt["receipt_sha256"] = tiers.digest(receipt)
        receipt_path.write_text(json.dumps(receipt, indent=2) + "\n")
    for name in ("elf", "hex"):
        shutil.copyfile(receipt[name], args.output / ("firmware." + name))
    (args.output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps({"status": "PASS", "receipt": str(args.output / "receipt.json"),
                      "elf_sha256": receipt["elf_sha256"], "hex_sha256": receipt["hex_sha256"]}))


if __name__ == "__main__":
    main()
