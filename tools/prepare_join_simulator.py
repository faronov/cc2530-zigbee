#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Build an isolated uCsim with two debugger null guards; never install it."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tarfile

from verify_firmware import require

ARCHIVE_SHA = "ebe7bfb0894380cd92798b57fb9de96e6c0b913a02b6854d0a01cd70328c1578"
SOURCE = Path("sdcc-4.2.0+dfsg/sim/ucsim")
PATCHES = {
    "sim.src/uc.cc": ("      if (ad)\n", "      if (ad && ad->memchip)\n"),
    "sim.src/var.cc": (
        "      (ad = ((cl_address_space *)space_mem)->get_decoder_of(space_addr)))\n",
        "      (ad = ((cl_address_space *)space_mem)->get_decoder_of(space_addr)) &&\n"
        "      ad->memchip)\n",
    ),
}


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def build(archive, output):
    require(sha(archive.read_bytes()) == ARCHIVE_SHA, "Wrong upstream simulator archive")
    require(not output.exists(), "Use a new isolated simulator output directory")
    for tool in ("g++", "make", "bison", "flex", "m4"):
        require(shutil.which(tool) is not None, "Missing simulator build dependency: " + tool)
    output.mkdir(parents=True)
    with tarfile.open(archive, "r:xz") as source:
        members = [m for m in source.getmembers() if m.name.startswith(str(SOURCE) + "/")]
        require(members and all((m.isfile() or m.isdir()) and ".." not in Path(m.name).parts
                                for m in members), "Unexpected upstream archive entry")
        source.extractall(output, members=members, filter="data")
    root = output / SOURCE
    changes = {}
    for name, (before, after) in PATCHES.items():
        path = root / name
        raw = path.read_bytes()
        require(raw.count(before.encode()) == 1, "Changed debugger patch context: " + name)
        changed = raw.replace(before.encode(), after.encode())
        path.write_bytes(changed)
        changes[name] = {"before": sha(raw), "after": sha(changed)}
    with (output / "build.log").open("w") as log:
        subprocess.run(["./configure"], cwd=root, stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run(["make", "-j2", "s51.src"], cwd=root,
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    binary = root / "s51.src/s51"
    report = {"archive": ARCHIVE_SHA, "debugger_changes": changes, "binary": sha(binary.read_bytes()),
              "builder": sha(Path(__file__).read_bytes()),
              "cpu_execution_changes": False, "installed": False}
    (output / "build.json").write_text(json.dumps(report, indent=2) + "\n", encoding="ascii")
    return binary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    print(build(args.archive.resolve(), args.output.resolve()))


if __name__ == "__main__":
    main()
