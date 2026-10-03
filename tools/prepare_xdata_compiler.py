#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Rebuild the frozen ownership-only SDCC patch series; never install it."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile

from prepare_join_simulator import ARCHIVE_SHA
from verify_firmware import require

ROOT = Path(__file__).resolve().parents[1]
PATCH_HASHES = (
    "2bdfc1afd47f7ac955a6d2f0669bca24c00a92f6d02523b0f9f07d608cfcd238",
    "4c8aecace1e481e491c4a77afa6c5e79d774a48b38e5f134866ba44f2d301586",
    "bf70f8e58f00d17e3dca5e7c921d9300014d257ca226550c5dd62dd577f71405",
    "4deb9cb2cb0a3af09ba55cfee3033bef761ed6e7fda681787c9fb80d24ae3878",
)
PORTS = ("z80", "z180", "r2k", "r2ka", "r3ka", "sm83", "tlcs90", "ez80_z80", "z80n",
         "ds390", "ds400", "pic14", "pic16", "hc08", "s08", "stm8", "pdk13", "pdk14",
         "pdk15", "mos6502")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build(archive, output):
    require(digest(archive) == ARCHIVE_SHA, "Wrong upstream SDCC archive")
    require(not output.exists(), "Use a new compiler output directory; existing toolchains are preserved")
    for name in ("gcc", "g++", "make", "bison", "flex", "m4", "git", "sdcc", "sdas8051"):
        require(shutil.which(name) is not None, f"Missing compiler build dependency: {name}")
    patches = sorted((ROOT / "experiments/sdcc-function/patches").glob("*.patch"))
    require(tuple(map(digest, patches)) == PATCH_HASHES, "Frozen compiler patch series changed")
    output.mkdir(parents=True)
    with tarfile.open(archive, "r:xz") as source:
        members = source.getmembers()
        require(members and all((m.isfile() or m.isdir()) and
                                not Path(m.name).is_absolute() and ".." not in Path(m.name).parts and
                                Path(m.name).parts[0] == "sdcc-4.2.0+dfsg" for m in members),
                "Unexpected SDCC archive entry")
        source.extractall(output, members=members, filter="data")
    root = output / "sdcc-4.2.0+dfsg"
    subprocess.run(["git", "init", "-q", str(root)], check=True)
    configure = ["./configure", "--prefix=/usr", *(f"--disable-{p}-port" for p in PORTS),
                 *(f"--disable-{p}" for p in ("ucsim", "device-lib", "packihx", "sdcpp", "sdcdb",
                                              "sdbinutils", "non-free"))]
    with (output / "build.log").open("w") as log:
        for patch in patches:
            subprocess.run(["git", "apply", str(patch)], cwd=root, stdout=log,
                           stderr=subprocess.STDOUT, check=True)
        subprocess.run(configure, cwd=root, stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run(["make", "-s", "-j2", "-o", "sdcc-sdbinutils", "sdcc-cc"],
                       cwd=root, stdout=log, stderr=subprocess.STDOUT, check=True)
    compiler = root / "bin/sdcc"
    control = Path(shutil.which("sdcc")).resolve()
    with (output / "regressions.txt").open("w") as log:
        subprocess.run([sys.executable, "-B", str(root / "support/regression/test-xdata-ownership.py"),
                        "--sdcc", str(compiler), "--control", str(control)],
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    result = dict(version=1, source_archive=ARCHIVE_SHA, patch_hashes=PATCH_HASHES,
                  binary_sha256=digest(compiler), control_sha256=digest(control),
                  compiler_version=subprocess.check_output([str(compiler), "--version"], text=True).strip(),
                  builder_sha256=digest(Path(__file__)), installed=False,
                  compiler_regressions="PASS", regressions_sha256=digest(output / "regressions.txt"))
    (output / "build.json").write_text(json.dumps(result, indent=2) + "\n", encoding="ascii")
    return compiler


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        print(build(args.archive.resolve(), args.output.resolve()))
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"SDCC ownership compiler preparation failed: {error}; inspect output build.log/regressions.txt",
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
