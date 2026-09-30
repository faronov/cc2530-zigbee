#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Link a resource probe, NOT an accepted executable or programming image."""
import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

from banked_image import physical_address
from verify_firmware import parse_ihex, parse_symbols, require


RUNTIME_OBJECTS = (
    ("libsdcc", "__memcpy"), ("libsdcc", "_memset"), ("libsdcc", "_gptrput"),
    ("libint", "_mulint"), ("liblong", "_mullong"), ("libsdcc", "_memcmp"),
)


def runtime_prefix(root, sdcc):
    """Link the unchanged toolchain scratch before every private buffer fence."""
    search = subprocess.check_output(
        [sdcc, "-mmcs51", "--model-large", "--print-search-dirs"], text=True)
    match = re.search(r"(?ms)^libdir:\n(.*?)^libpath:", search)
    require(match is not None, "Missing SDCC large-model library search path")
    directories = [Path(line) for line in match[1].splitlines() if line]
    destination = root / "runtime"
    destination.mkdir(exist_ok=True)
    result = {}
    for library, module in RUNTIME_OBJECTS:
        candidates = [d / (library + ".lib") for d in directories]
        archive = next((p for p in candidates if p.is_file()), None)
        require(archive is not None, "Missing SDCC runtime archive: " + library)
        raw = subprocess.check_output(["sdar", "-p", str(archive), module + ".rel"])
        require(raw.startswith(b"XH3\n")
                and re.findall(rb"^M (\w+)$", raw, re.M) == [module.encode()],
                "Wrong runtime archive member: " + module)
        (destination / (module + ".rel")).write_bytes(raw)
        # Archive members contain no source listing. ASlink -r still requires
        # a listing file for explicit objects; runtime proof decodes linked CODE.
        (destination / (module + ".lst")).write_text(
            "; Unchanged SDCC archive member; verify object and actual CODE.\n", encoding="ascii")
        result[module] = hashlib.sha256(raw).hexdigest()
    return result


def link_symbols(noice, map_text, target):
    """ASlink truncates map names to 32 columns, even with -w; NoICE does not."""
    definitions, loads = {}, []
    for line in noice.splitlines():
        if line.startswith("LOAD "):
            loads.append(line[5:])
            continue
        match = re.fullmatch(r"DEF (\S+) 0x([0-9A-Fa-f]+)", line)
        require(match is not None, "Unrecognized NoICE symbol record")
        name, value = match[1], int(match[2], 16)
        require(name not in definitions or definitions[name] == value,
                f"Conflicting complete linker symbol: {name}")
        definitions[name] = value
    require(loads == [str(target)], "NoICE names a different linked image")
    complete = {name: value for name, value in definitions.items()
                if re.fullmatch(r"[A-Za-z_]\w*", name)}
    displayed = {(name, int(value, 16)) for value, name in re.findall(
        r"^\s*(?:[CD]:\s*)?([0-9A-Fa-f]{8})\s+([A-Za-z_]\w*)(?:\s+\S+)?\s*$",
        map_text, re.M)}
    require(displayed == {(name[:32], value) for name, value in complete.items()},
            "Map and complete NoICE symbol records differ")
    return parse_symbols("\n".join(f"{value:08X} {name}" for name, value in complete.items()))


def place(objects):
    """Pack near-call groups; DATA placements are deliberately unverified."""
    require(objects, "No link objects")
    areas = defaultdict(int)
    for module, text in objects.items():
        matches = re.findall(r"^A (\w+) size ([0-9A-F]+) flags [0-9A-F]+ addr [0-9A-F]+$",
                             text, re.M)
        require(matches and len({name for name, _ in matches}) == len(matches),
                f"Missing or duplicate object areas: {module}")
        for name, size in matches:
            areas[name] += int(size, 16)
    groups = sorted(((size, name) for name, size in areas.items()
                     if name.startswith("JS_") and size), reverse=True)
    capacity = [0x8000] * 6 + [0x6800]
    assigned = [[] for _ in capacity]

    def pack(index):
        if index == len(groups):
            return True
        size, name = groups[index]
        seen = set()
        for bank in sorted(range(7), key=lambda b: capacity[b]):
            if capacity[bank] < size or capacity[bank] in seen:
                continue
            seen.add(capacity[bank])
            capacity[bank] -= size
            assigned[bank].append((name, size))
            if pack(index + 1):
                return True
            assigned[bank].pop()
            capacity[bank] += size
        return False

    require(pack(0), "Actual CODE groups cannot fit below the NV partition")
    placements = {}
    for bank, group in enumerate(assigned, 1):
        address = (bank << 16) | 0x8000
        for name, size in group:
            physical_address(address)
            physical_address(address + size - 1)
            placements[name] = address
            address += size
    for name, size in areas.items():
        if name.startswith("JD_"):
            require(size == 0, "Unsplit compiler DATA")
        if name.startswith("JF_"):
            require(0 < size <= 27, f"Function frame too wide for reserved DATA: {name}")
            placements[name] = 8 if size <= 22 else 0x2b
    return {"object_areas": dict(areas), "placements": placements,
            "banks": assigned, "remaining_bank_bytes": capacity,
            "DATA_liveness_verified": False, "stack_verified": False}


def resources(image, symbols, memory):
    require("*** ERROR" not in memory, "Linker reported a memory error")
    require(symbols["s_XSEG"] == 0 and 0 < symbols["l_XSEG"] <= 0x1e00
            and symbols["l_XISEG"] == symbols["l_PSEG"] == 0,
            "Ordinary XDATA does not fit")
    require(symbols["_join_smoke_status"] == 0x1e00, "JSN1 status reservation moved")
    require(symbols["_join_smoke_iram_low"] == 8
            and symbols["_join_smoke_iram_high"] == 0x2b
            and symbols["s_OSEG"] == 0x46 and symbols["l_OSEG"] == 10
            and symbols["s_SSEG"] == 0x50 and symbols["l_SSEG"] == 45,
            "Physical DATA/OSEG/stack reservations changed")
    physical = {physical_address(address) for address in image}
    require(image and len(physical) == len(image), "Physical CODE overlaps or is empty")
    match = re.search(r"EXTERNAL RAM\s+0x0000\s+0x[0-9a-f]+\s+(\d+)\s+7680", memory)
    require(match is not None and int(match[1]) == symbols["l_XSEG"],
            "Map and memory XDATA reports differ")
    return {"ordinary_xdata": symbols["l_XSEG"], "xdata_limit": 7680,
            "xdata_remaining": 7680 - symbols["l_XSEG"],
            "populated_code": len(image), "last_physical_code": max(physical),
            "initial_sp": 0x4f, "stack_limit": 0x7c,
            "resource_link_completed": True, "accepted_image": False,
            "DATA_liveness_verified": False, "stack_verified": False,
            "simulated": False, "hardware_observed": False}


def apply_data_placement(plan, candidate):
    require(candidate["objects"] == plan["objects"], "DATA placement belongs to different objects")
    expected = {name for name in plan["placements"] if name.startswith("JF_")}
    require(candidate["placements"].keys() == expected, "Incomplete or unexpected DATA placement")
    reserved = set(range(8, 0x1e)) | set(range(0x2b, 0x46))
    for name, base in candidate["placements"].items():
        require(type(base) is int and set(range(base, base + plan["object_areas"][name])) <= reserved,
                "Candidate DATA placement escapes physical reservations")
    plan["placements"].update(candidate["placements"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--modules", nargs="+", required=True)
    parser.add_argument("--sdcc", default="sdcc")
    parser.add_argument("--data-placement", type=Path)
    args = parser.parse_args()
    require(len(set(args.modules)) == len(args.modules)
            and all(re.fullmatch(r"\w+", m) for m in args.modules), "Invalid link module set")
    root = args.output
    target = root / "join_smoke_unverified.ihx"
    report = root / "layout.json"
    # A failed fresh preparation must not leave a previous resource result.
    target.unlink(missing_ok=True)
    report.unlink(missing_ok=True)
    require({p.stem for p in root.glob("*.rel")} == set(args.modules),
            "Missing or unexpected object in isolated layout directory")
    objects = {m: (root / f"{m}.rel").read_bytes() for m in args.modules}
    plan = place({m: data.decode("ascii") for m, data in objects.items()})
    plan["objects"] = {m: hashlib.sha256(data).hexdigest() for m, data in objects.items()}
    if args.data_placement:
        raw = args.data_placement.read_bytes()
        apply_data_placement(plan, json.loads(raw))
        plan["data_placement_sha256"] = hashlib.sha256(raw).hexdigest()
    runtime = runtime_prefix(root, args.sdcc)
    (root / "placement.json").write_text(json.dumps(plan, indent=2) + "\n")
    subprocess.run([args.sdcc, "-mmcs51", "--model-large", "--debug", "--Werror",
                    "--iram-size", "0x100", "--xram-loc", "0", "--xram-size", "0x1e00",
                    "--code-size", "0x80000", "--stack-size", "0x2d", "-Wl-r", "-Wl-w", "-Wl-j",
                    *(f"-Wl-b{name}=0x{address:x}" for name, address in plan["placements"].items()),
                    "-o", str(target),
                    *(str(root / "runtime" / f"{m}.rel") for m in runtime),
                    *(str(root / f"{m}.rel") for m in args.modules)],
                   check=True)
    snapshots = {}
    for module in args.modules:
        source = root / f"{module}.rst"
        destination = root / f"join_smoke_unverified.{module}.rst"
        shutil.copyfile(source, destination)
        snapshots[module] = hashlib.sha256(destination.read_bytes()).hexdigest()
    symbols = link_symbols(target.with_suffix(".noi").read_text(),
                           target.with_suffix(".map").read_text(), target)
    for name, address in plan["placements"].items():
        require(symbols.get("s_" + name) == address
                and symbols.get("l_" + name) == plan["object_areas"][name],
                f"Linked area identity or placement differs: {name}")
    result = resources(parse_ihex(target.read_text()), symbols,
                       target.with_suffix(".mem").read_text())
    result["objects"] = plan["objects"]
    result["runtime_objects"] = runtime
    result["listings"] = snapshots
    result["artifacts"] = {suffix: hashlib.sha256(target.with_suffix(suffix).read_bytes()).hexdigest()
                           for suffix in (".ihx", ".map", ".mem", ".cdb", ".noi")}
    result["spill_manifest"] = hashlib.sha256((root / "spill-manifest.json").read_bytes()).hexdigest()
    report.write_text(json.dumps(result, indent=2) + "\n")
    print(f"UNVERIFIED join resource link: {result['ordinary_xdata']}/7680 XDATA, "
          f"{result['populated_code']} CODE; DATA liveness, stack and execution NOT accepted.")


if __name__ == "__main__":
    main()
