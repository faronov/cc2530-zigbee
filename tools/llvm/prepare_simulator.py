#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Build the isolated uCsim used by the experimental LLVM image replay.

Profiles (both start from tools/prepare_join_simulator.py: same pinned archive,
same two debugger null guards, never installed):

* reference -- adds the banker use-after-free fix. Re-creating the FMAP banker
  deleted the old cl_banker while its operator stayed chained on SFR 0x9f, so a
  later bank-register write could crash depending on heap layout. This is the
  acceptance oracle for test-llvm-full.
* fast -- reference plus coalesced `dump /b` writes (one write(2) per 4 KiB
  instead of per byte). The read() sequence and the produced byte stream are
  unchanged; equivalence is a differential test, see docs/LLVM_DEVELOPMENT.md.

CPU execution semantics are unchanged in both profiles.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import prepare_join_simulator as base  # noqa: E402
from verify_firmware import require  # noqa: E402

BANKER_UAF = {
    "sim.src/memcl.h": (
        "  virtual void del_operator(class cl_brk *brk);\n",
        "  virtual void del_operator(class cl_brk *brk);\n"
        "  virtual void del_banker_operators(class cl_banker *b);\n",
    ),
    "sim.src/mem.cc": [
        ("void\ncl_memory_cell::del_operator(class cl_brk *brk)\n",
         "void\ncl_memory_cell::del_banker_operators(class cl_banker *b)\n"
         "{\n"
         "  class cl_memory_operator **op_p = &operators;\n"
         "  while (*op_p)\n"
         "    {\n"
         "      if ((*op_p)->get_banker() == b)\n"
         "        {\n"
         "          class cl_memory_operator *old = *op_p;\n"
         "          *op_p = old->next_operator;\n"
         "          delete old;\n"
         "        }\n"
         "      else\n"
         "        op_p = &(*op_p)->next_operator;\n"
         "    }\n"
         "}\n\n"
         "void\ncl_memory_cell::del_operator(class cl_brk *brk)\n"),
        ("cl_banker::~cl_banker()\n{\n  int i;\n",
         "cl_banker::~cl_banker()\n{\n  int i;\n"
         "  // Unlink our bank_switcher operators; otherwise the cell keeps a\n"
         "  // dangling banker pointer and a later bank-register write is a use-after-free.\n"
         "  class cl_address_space *ases[2]= { banker_as, banker2_as };\n"
         "  t_addr addrs[2]= { banker_addr, banker2_addr };\n"
         "  for (int k= 0; k < 2; k++)\n"
         "    {\n"
         "      class cl_memory_cell *c= ases[k] ? ases[k]->get_cell(addrs[k]) : NULL;\n"
         "      if (c)\n"
         "\tc->del_banker_operators(this);\n"
         "    }\n"),
    ],
}

BUFFERED_DUMP = {
    "sim.src/mem.cc": [
        ("t_addr\ncl_memory::dump_b(",
         "static void\ndump_b_flush(class cl_f *f, const char *buf, int n)\n{\n"
         "  while (n > 0)\n    {\n      int w= f->write(buf, n);\n      if (w <= 0)\n\tbreak;\n"
         "      buf+= w;\n      n-= w;\n    }\n}\n\n"
         "t_addr\ncl_memory::dump_b("),
        ("  t_addr a= start;\n  t_mem d= read(a);\n  while ((a <= stop) &&\n\t (a <= hva))\n    {\n"
         "      char c= d;\n      if (a >= lva)\n\t{\n\t  f->write(&c, 1);\n\t}\n      d= read(++a);\n    }\n",
         "  char buf[4096];\n  int n= 0;\n"
         "  t_addr a= start;\n  t_mem d= read(a);\n  while ((a <= stop) &&\n\t (a <= hva))\n    {\n"
         "      char c= d;\n      if (a >= lva)\n\t{\n\t  buf[n++]= c;\n"
         "\t  if (n == (int)sizeof(buf))\n\t    {\n\t      dump_b_flush(f, buf, n);\n\t      n= 0;\n\t    }\n"
         "\t}\n      d= read(++a);\n    }\n  dump_b_flush(f, buf, n);\n"),
    ],
}

PROFILES = {"reference": [base.PATCHES, BANKER_UAF], "fast": [base.PATCHES, BANKER_UAF, BUFFERED_DUMP]}


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def apply(root, patches, changes):
    for name, edits in patches.items():
        path = root / name
        raw = path.read_bytes()
        changed = raw
        for before, after in edits if isinstance(edits, list) else [edits]:
            require(changed.count(before.encode()) == 1, "Changed simulator patch context: " + name)
            changed = changed.replace(before.encode(), after.encode())
        path.write_bytes(changed)
        first = changes.setdefault(name, {"before": sha(raw)})
        first["after"] = sha(changed)


def build(archive, output, profile, jobs):
    require(sha(archive.read_bytes()) == base.ARCHIVE_SHA, "Wrong upstream simulator archive")
    require(not output.exists(), "Use a new isolated simulator output directory")
    for tool in ("g++", "make", "bison", "flex", "m4"):
        require(shutil.which(tool) is not None, "Missing simulator build dependency: " + tool)
    output.mkdir(parents=True)
    with tarfile.open(archive, "r:xz") as source:
        members = [m for m in source.getmembers() if m.name.startswith(str(base.SOURCE) + "/")]
        require(members and all((m.isfile() or m.isdir()) and ".." not in Path(m.name).parts
                                for m in members), "Unexpected upstream archive entry")
        source.extractall(output, members=members, filter="data")
    root = output / base.SOURCE
    changes = {}
    for patches in PROFILES[profile]:
        apply(root, patches, changes)
    with (output / "build.log").open("w") as log:
        subprocess.run(["./configure"], cwd=root, stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run(["make", f"-j{jobs}", "s51.src"], cwd=root, stdout=log, stderr=subprocess.STDOUT, check=True)
    binary = root / "s51.src/s51"
    report = {"archive": base.ARCHIVE_SHA, "profile": profile, "source_changes": changes,
              "binary": sha(binary.read_bytes()), "builder": sha(Path(__file__).read_bytes()),
              "base_builder": sha(Path(base.__file__).read_bytes()),
              "cpu_execution_changes": False, "installed": False}
    (output / "build.json").write_text(json.dumps(report, indent=2) + "\n", encoding="ascii")
    return binary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--profile", choices=sorted(PROFILES), default="reference")
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    print(build(args.archive, args.output, args.profile, args.jobs))


if __name__ == "__main__":
    main()
