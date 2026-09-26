#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Exact linked interval owner and alias-aware MMIO replay; never hardware."""
import argparse
from dataclasses import dataclass
from functools import lru_cache
import hashlib
import json
from pathlib import Path
import re
import subprocess

from boot_image import (ALIAS, check_alias, check_pc, marker, memory_dump, simulate,
                        snapshot_commands, verify_component_layout)
from boot_radio_tx_fixture import sections, snap, stack_high_water
from clock_fixture import verify_clock_code
from radio_link_fixture import LABEL, SETTINGS, records
from verify_firmware import (cdb_address, code_bytes, parse_ihex, parse_symbols,
                             peripheral_accesses, require, verify_timebase_reader)

MODULES = ("timebase", "clock", "mac_time", "radio_autoack", "mac_epoch",
           "mac_radio", "mac_attempt", "test_mac_attempt")
CASES = 28
SIZE, XDATA, STACK = 24621, 1475, 0x5a
CODE_SHA = "64acf106f577d774b333cfb063c26f0b4a6f092b2901a1db343e052eb6191cbc"
# Compact-only projection changes source lines, not this image's CODE/ABI.
CDB_SHA = "ad7f4d1a86951e64be41899eb26fd253adae4a23a02ad05326ca151e74f6c399"
MAP_SHA = "822ec1514503e149578cb17132d92e9ddb2a46be5ba4619ee9cbabe98763aba6"
MEM_SHA = "3a9a8cd7acb4a495df6504761e0bef4e015590f03be0ca5e5270288f39914a55"
LIST_SHA = "dbc898ee53bb6f58333751bebc2dc8ad8c28405b445c93f029c8d3fb371fab11"
OBJECT_SHA = "9a1dfc2ccf2278917b14f475e3e85cbc5316a0f9ef21ae153c59079b95df64f7"
INVENTORY = (326, 148638, 119, 121)
NEGATIVES = 77713
CALLER_SIZES = {"config": 14, "frame": 128, "record": 164, "operation": 1,
                "return": 1, "length": 1, "input": 2, "output": 2, "limit": 2,
                "window": 2, "timeout": 4}

@dataclass(frozen=True)
class Profile:
    stem: str
    prefix: str
    native: str
    modules: tuple
    caller_sizes: tuple
    size: int
    xdata: int
    stack: int
    code_sha: str
    cdb_sha: str
    map_sha: str
    mem_sha: str
    list_sha: str
    object_sha: str
    cases: int
    inventory: tuple
    negatives: int
    handoff: bool = False


LEGACY = Profile("mac_attempt_test", "ma", "host-mac-attempt-tests", MODULES,
                 tuple(CALLER_SIZES.items()), SIZE, XDATA, STACK, CODE_SHA, CDB_SHA,
                 MAP_SHA, MEM_SHA, LIST_SHA, OBJECT_SHA, CASES, INVENTORY, NEGATIVES)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def map_identity(symbols):
    return sha(json.dumps(symbols, sort_keys=True, separators=(",", ":")).encode())


@lru_cache(maxsize=16)
def listing_serialized(text):
    # Cache immutable input text, never verification results or mutable images.
    return "".join(f"{a:06x}:{v.hex()}\n" for a, v in records(text)) + \
        "\n".join(line.strip() for line in text.splitlines()
                  if re.search(r"\b_\w+:|\.ds \d+$|\.area ", line)) + "\n"


def listing_identity(listings, profile=LEGACY):
    return sha("".join(m+"\n"+listing_serialized(listings[m]) for m in profile.modules).encode("ascii"))


def object_identity(objects, profile=LEGACY):
    require(all(v.startswith(";!FILE ") for v in objects.values()), "Attempt object header")
    return sha("".join(m+"\n"+objects[m].split("\n", 1)[1] for m in profile.modules).encode("ascii"))


def caller(symbols, profile=LEGACY):
    return {name: (symbols["_mac_attempt_test_"+name], size) for name, size in profile.caller_sizes}


def verify(image, symbols, debug, memory, listings, objects, profile=LEGACY):
    require(profile.size <= 32768 and sha(code_bytes(image, profile.size)) == profile.code_sha,
            "Attempt complete CODE")
    require(sha(debug.encode("ascii")) == profile.cdb_sha, "Attempt complete raw CDB")
    require(map_identity(symbols) == profile.map_sha and sha(memory.encode("ascii")) == profile.mem_sha,
            "Attempt complete map/memory accounting")
    require(set(listings) == set(objects) == set(profile.modules), "Attempt exact composition")
    require(listing_identity(listings, profile) == profile.list_sha
            and object_identity(objects, profile) == profile.object_sha,
            "Attempt ordered listings/allocations/helpers/objects")
    allocated = verify_component_layout(image, symbols, debug, memory, "mac_attempt_test_result",
        tuple(m+".c" for m in profile.modules), xdata_budget=2048)
    require(symbols["l_XSEG"] == profile.xdata and symbols["s_SSEG"] == profile.stack, "Attempt XDATA/IRAM ABI")
    codes, spans, covered = {}, {}, set()
    for module in profile.modules:
        text = listings[module]
        require(f".module {module}" in text, "Attempt wrong relocated snapshot")
        codes[module] = dict(records(text))
        for address, raw in records(text):
            span = set(range(address, address+len(raw)))
            require(not span & covered and all(image.get(address+i) == v for i, v in enumerate(raw)),
                    "Attempt non-linked/overlapping instruction")
            covered.update(span)
        segment = text.split(".area XSEG    (XDATA)", 1)[1].split(".area XABS", 1)[0]
        spans[module] = set()
        for a, size in re.findall(r"^\s+([0-9A-F]{6})\s+\d+\s+\.ds (\d+)$", segment, re.M):
            span = set(range(int(a, 16), int(a, 16)+int(size)))
            require(span and not span & set().union(*spans.values()), "Attempt overlapping allocation")
            spans[module].update(span)
    shared, radio_end, end = (symbols[n] for n in
        ("_mac_radio_shared_end", "_mac_radio_reserved_end", "_mac_attempt_reserved_end"))
    runtime, final = symbols["___memcpy_PARM_2"], symbols["__gptrput_PARM_2"]
    require(set().union(*(spans[m] for m in profile.modules[:5])) == set(range(shared)) and
            spans["mac_radio"] == set(range(shared, radio_end+1)) and
            spans["mac_attempt"] == set(range(radio_end+1, end+1)) and
            spans[profile.modules[-1]] == set(range(end+1, runtime)), "Attempt whole private/caller prefix")
    require(final+1 == profile.xdata and final-runtime == 11 and
            [symbols[n]-runtime for n in ("___memcpy_PARM_2", "___memcpy_PARM_3",
             "_memset_PARM_2", "_memset_PARM_3", "__gptrput_PARM_2")] == [0, 3, 8, 9, 11],
            "Attempt complete linked libc suffix")
    used = set()
    for name, (a, size) in caller(symbols, profile).items():
        region = set(range(a, a+size))
        require(not used & region and region <= spans[profile.modules[-1]], "Attempt caller ABI: "+name)
        used.update(region)
    require(used == spans[profile.modules[-1]], "Attempt unaccounted caller storage")
    for module in profile.modules[4:]:
        require(not peripheral_accesses(codes[module]) and not any(
            raw[0] == 0x90 and 0x6000 <= int.from_bytes(raw[1:], "big") < 0x6400
            for raw in codes[module].values()), "Attempt upper-layer MMIO bypass")
    for module, names in (
        ("mac_attempt", ("mac_radio_init", "mac_radio_prepare", "mac_radio_attempt",
                         "mac_radio_receive", "mac_radio_stop", "mac_radio_resume", "mac_epoch_step")),
        ("mac_radio", ("clock_select_init", "mac_time_init", "mac_time_read_radio", "mac_epoch_start",
                       "mac_epoch_step", "radio_autoack_acquire", "radio_autoack_prepare",
                       "radio_autoack_attempt")),
        ("radio_autoack", ("mac_time_attempt_begin", "mac_time_attempt_read", "mac_time_attempt_end",
                           "mac_epoch_start", "mac_epoch_step")),
        (profile.modules[-1], ("mac_attempt_init", "mac_attempt_prepare", "mac_attempt_run",
                              "mac_attempt_receive", "mac_attempt_stop", "mac_attempt_resume")),
    ):
        for name in names:
            require(b"\x12"+symbols["_"+name].to_bytes(2, "big") in codes[module].values(),
                    "Attempt bypasses genuine API: "+name)
    if profile.handoff:
        for module, names in (
            (profile.modules[-1], ("mac_attempt_now", "mac_attempt_handoff")),
            ("mac_attempt", ("mac_radio_attempt_now", "mac_radio_handoff")),
            ("mac_radio", ("radio_autoack_handoff_eligible", "radio_autoack_handoff")),
            ("radio_autoack", ("mac_time_handoff_read", "mac_time_handoff_value")),
        ):
            for name in names:
                require(b"\x12"+symbols["_"+name].to_bytes(2, "big") in codes[module].values(),
                        "Handoff bypasses genuine API: "+name)
    verify_timebase_reader(image, symbols, debug, 0x1e00, 8)
    verify_clock_code(image, symbols, debug)
    return allocated, mmio_sites(image, debug, listings, handoff=profile.handoff)


def mmio_sites(image, debug, listings, *, handoff=False):
    """Decode the actual common lower-service MMIO, including indexed settings."""
    codes = {m: dict(records(listings[m])) for m in MODULES[:4]}
    sites = {}
    for module in MODULES[:4]:
        code = codes[module]
        for pc, raw, reg in peripheral_accesses(code):
            write = raw[0] in (0x75, 0xf5) or 0x88 <= raw[0] <= 0x8f
            observed = reg if write or raw[0] == 0xb5 else (
                raw[2] if raw[0] == 0x85 else raw[0]-0xa8 if 0xa8 <= raw[0] <= 0xaf else 0xe0)
            sites[pc] = ("w" if write else "r", reg, observed)
        for pc, raw in code.items():
            if raw[0] != 0x90 or not 0x6000 <= int.from_bytes(raw[1:], "big") < 0x6400:
                continue
            address = int.from_bytes(raw[1:], "big")
            if code.get(pc+3) == b"\x93":
                require(module == "radio_autoack" and
                        address == cdb_address(debug, "L:Fradio_autoack$values$0_0$0"),
                        "Attempt unreviewed CODE table in numeric XREG range")
                continue
            if code.get(pc+3) == b"\xe0": sites[pc+3] = ("r", address, 0xe0)
            else:
                require(len(code.get(pc+3, b"")) == 2 and code[pc+3][0] == 0x74 and
                        code.get(pc+5) == b"\xf0", "Attempt unreviewed constant MOVX")
                sites[pc+5] = ("w", address, address)
    radio = codes["radio_autoack"]
    labels = {n: int(a, 16) for a, n in LABEL.findall(listings["radio_autoack"])}
    indexed = [pc for pc, raw in radio.items()
               if raw == b"\x12"+labels["_setting_address"].to_bytes(2, "big")]
    require(len(indexed) == 2, "Attempt indexed configuration call inventory")
    read = indexed[0]+13
    write = indexed[1]+31
    require(radio.get(read) == b"\xe0" and radio.get(write) == b"\xf0",
            "Attempt indexed configuration instruction operands")
    sites[read] = ("r", None, 0xe0); sites[write] = ("w", None, None)
    rfd, settle = labels["_read_fifo"], labels["_cca_settle"]
    require(sum(raw == b"\x12"+labels["_receive_head"].to_bytes(2, "big")
                for raw in radio.values()) == 2, "Attempt bypasses the shared exact head consumer")
    require(bytes(image[a] for a in range(rfd, rfd+4)) == b"\x85\xd9\x82\x22" and
            sum(raw == b"\x12"+rfd.to_bytes(2, "big") for raw in radio.values()) == 1 and
            bytes(image[a] for a in range(settle, settle+5)) == b"\0\0\0\0\x22" and
            sum(raw == b"\x12"+settle.to_bytes(2, "big") for raw in radio.values()) == 2,
            "Attempt unique destructive RFD / four pre-strobe NOPs")
    sites[settle] = ("c", 0, None)
    if handoff:
        clear = labels["_handoff_clear_sfd"]
        expected = bytes.fromhex("906193e020e50a75e9fde05420440180027420f58222")
        require(bytes(image[a] for a in range(clear, clear+len(expected))) == expected,
                "Handoff SFD-read/clear/read instructions; timing needs live samples")
        require(sum(raw == b"\x12"+clear.to_bytes(2, "big") for raw in radio.values()) == 1,
                "Handoff SFD guard call inventory")
        require(sites[clear+3] == ("r", 0x6193, 0xe0) and
                sites[clear+7] == ("w", 0xe9, 0xe9), "Handoff SFD guard MMIO")
        sites[clear+10] = ("r", 0x6193, 0xe0)
    return sites


def rejected(function):
    try:
        function()
    except ValueError:
        return
    raise ValueError("Attempt negative accepted")


def negatives(image, symbols, debug, memory, listings, objects, profile=LEGACY):
    count = 0

    def reject(**changes):
        nonlocal count
        args = dict(image=image, symbols=symbols, debug=debug, memory=memory, listings=listings, objects=objects)
        rejected(lambda: verify(**(args | changes), profile=profile)); count += 1

    for a in image: reject(image=image | {a: image[a] ^ 1})
    reject(image=image | {profile.size: 0})
    reject(image={a: v for a, v in image.items() if a != profile.size-1})
    for name in symbols: reject(symbols=symbols | {name: symbols[name] ^ 1})
    for line in debug.splitlines():
        if line.startswith(("F:", "S:", "L:", "T:")):
            reject(debug=debug.replace(line+"\n", "", 1))
            reject(debug=debug+line+"\n")
            reject(debug=debug.replace(line, line+"!", 1))
    for changed in (debug.replace("\n", "\r\n"), debug+"\n", debug+"\0"): reject(debug=changed)
    reject(memory=memory+"\n")
    for module in profile.modules:
        text = listings[module]; lines = text.splitlines(keepends=True)
        first, second = [i for i, line in enumerate(lines) if records(line)][:2]
        for operation in ("drop", "duplicate", "reorder"):
            changed = lines.copy()
            if operation == "drop": del changed[first]
            elif operation == "duplicate": changed.insert(first, changed[first])
            else: changed[first], changed[second] = changed[second], changed[first]
            reject(listings=listings | {module: "".join(changed)})
        reject(listings=listings | {module: text.replace(".ds ", ".lost ", 1)})
        for match in LABEL.finditer(text):
            reject(listings=listings | {module: text[:match.start()]+text[match.end():]})
            reject(listings=listings | {module: text+match[0]+"\n"})
        reject(objects=objects | {module: objects[module].replace("A XSEG size ", "A XSEG lost ", 1)})
    return count


def run_vector(simulator, path, symbols, debug, allocated, sites, vector, profile=LEGACY):
    buffers = caller(symbols, profile)
    before, done = (symbols["_mac_attempt_test_"+n] for n in ("before", "done"))
    commands = [ALIAS, "fill xram 0 0x1eff 0xa5", "fill xram 0x6000 0x63ff 0x69",
                f"run 0 {symbols['_main']:#x}", "fill iram 0x7d 0xff 0xc7",
                f"run {symbols['_main']:#x} {before:#x}"]
    commands += [f"break {pc:#x}" for pc in list(sites)+[before, done]]
    checks, number = [], 10
    for step in vector["steps"]:
        if profile.handoff and vector["case"] >= LEGACY.cases:
            require("clock" in step and "radio_diag" in step and "handoff_clock" in step and
                    (step["operation"] != 2 or "body" in step),
                    "Handoff missing original caller/diagnostic observation")
        current = {int(a): v for a, v in step["initial"].items()}
        expected_initial = {a for name, a in symbols.items() if name.startswith("_SOC_")} | set(range(0x6100, 0x6400))
        if profile.handoff and vector["case"] >= LEGACY.cases:
            expected_initial |= {0x6081, 0x6083}
        require(set(current) == expected_initial and all(type(v) is int and 0 <= v <= 255
                                                        for v in current.values()),
                "Attempt initial stimulus escaped the exact peripheral inventory")
        commands += [f"set memory {'sfr' if a < 256 else 'xram'} {a:#x} {v:#x}" for a, v in current.items()]
        for name in ("operation", "input", "output", "length", "limit", "timeout", "window", "config"):
            address, size = buffers[name]
            raw = bytes.fromhex(step["configuration"]) if name == "config" else step[name].to_bytes(size, "little")
            commands.append(f"set memory xram {address:#x} "+" ".join(hex(b) for b in raw))
        if "body" in step:
            require(profile.handoff and step["operation"] == 2 and
                    len(bytes.fromhex(step["body"])) == step["length"] <= 125,
                    "Handoff invalid caller body")
            commands.append(f"set memory xram {buffers['frame'][0]:#x} " +
                            " ".join(hex(b) for b in bytes.fromhex(step["body"])))
        commands.append("step 1"); first = number; previous = None
        for kind, address, value in step["events"]:
            require(kind in ("r", "w", "c"), "Attempt unknown MMIO event")
            if profile.handoff and step["operation"] == 7 and kind == "w":
                require((address, value) in ((0xe9, 0xfd), (0x6189, 0x60), (0x6180, 0x05)),
                        "Handoff unapproved write/strobe/stop/flush")
            if profile.handoff and step["operation"] == 7 and kind == "r":
                require(address != 0xd9 and not 0x6000 <= address < 0x6080,
                        "Handoff unapproved RXFIFO read")
            if kind == "w" and address in (0xe1, 0xd9):
                require((step["operation"] == 2 and (address == 0xd9 or value == 0xee)) or
                        (step["operation"] == 3 and address == 0xe1 and value == 0xea),
                        "Attempt unapproved submission/FIFO/flush")
            if profile.handoff and kind == "w" and address == 0xe9:
                require(step["operation"] == 7 and value == 0xfd, "Handoff unapproved SFD clear")
            space = "sfr" if address < 256 else "xram"
            adjacent = previous is not None and previous[0] == kind == "w" and \
                0xa2 <= previous[1] < 0xa6 and address == previous[1]+1
            adjacent |= profile.handoff and previous == ("w", 0xe9) and kind == "r" and address == 0x6193
            if not adjacent: commands.append("run")
            commands += [marker(number), "state", "dump /h sfr 0x81 0x83"]
            if kind == "r": commands.append(f"set memory {space} {address:#x} {value:#x}")
            commands += [marker(number+1), "step 4" if kind == "c" else "step 1"]
            if kind == "c":
                require(address == 0 and value == 4 and step["operation"] == 3, "Attempt CCA settling")
                commands.append("state")
            elif kind == "r":
                for dest in {d for k, a, d in sites.values() if k == "r" and a in (None, address)}:
                    commands.append(f"dump /h {'iram' if dest < 128 else 'sfr'} {dest:#x} {dest:#x}")
            else: commands.append(f"dump /h {space} {address:#x} {address:#x}")
            commands.append(marker(number+2)); number += 3
            if kind != "c": current[address] = value
            previous = (kind, address)
        commands += ["run"] + snapshot_commands(number)
        commands += [marker(number+4), "dump /h xram 0x6000 0x63ff", marker(number+5),
                     "step 1", "run", marker(number+6), "state", marker(number+7)]
        checks.append((step, first, number, current)); number += 8
    require(number < 65536, "Attempt bounded transcript")
    parts = sections(simulate(simulator, commands, path)); peak = sampled = 0
    status = cdb_address(debug, "L:Fmac_radio$status$0_0$0")
    for step, first, final, current in checks:
        for i, (kind, address, value) in enumerate(step["events"]):
            n = first+3*i
            match = re.search(r"CPU state= OK PC= 0x([0-9a-fA-F]+)", parts[n])
            require(match is not None and int(match[1], 16) in sites, "Attempt unplanned peripheral stop")
            pc = int(match[1], 16); k, a, observed = sites[pc]
            require(kind == k and a in (None, address),
                    f"Attempt case{vector['case']} op{step['operation']} MMIO pc{pc:04x}, "
                    f"event{i}: expected{kind}/{address:04x}, got{k}/{a}")
            regs = memory_dump(parts[n], 0x81, 3); sampled = max(sampled, regs[0])
            if kind == "c": check_pc(parts[n+1], pc+4); continue
            if address >= 256:
                require(regs[1:] == address.to_bytes(2, "little"), "Attempt actual DPTR")
                if a is None: require(address in SETTINGS or 0x616a <= address <= 0x6175,
                                      "Attempt indexed MMIO escaped whitelist")
            require(memory_dump(parts[n+1], address if observed is None else observed, 1)[0] == value,
                    "Attempt genuine instruction value")
        check_pc(parts[final], done); check_pc(parts[final+6], before)
        ram, iram, sfr = snap(parts, final)
        require(ram[0x1e00:0x1e08] == (b"MAH1" if profile.handoff else b"MAT1") + b"\x01\x08\0\0",
                "Attempt status reservation ABI")
        require(ram[buffers["return"][0]] == step["return"] and
                ram[status:status+14] == bytes.fromhex(step["diagnostic"]),
                f"Attempt return/diagnostic case{vector['case']} op{step['operation']}")
        if profile.handoff and vector["case"] >= LEGACY.cases:
            sizes = (4, 2, 2) + (1,)*18
            require(len(step["radio_diag"]) == len(sizes), "Handoff radio diagnostic shape")
            expected = b"".join(value.to_bytes(size, "little")
                                for value, size in zip(step["radio_diag"], sizes))
            address = cdb_address(debug, "L:Fradio_autoack$status$0_0$0")
            require(ram[address:address+len(expected)] == expected,
                    "Handoff original lower diagnostic mismatch")
        if profile.handoff:
            expected = bytes.fromhex(step.get("handoff_clock", "00"*18))
            address = symbols["_mac_radio_handoff_clock"]
            require(len(expected) == 18 and ram[address:address+18] == expected,
                    "Handoff actual live-clock bracket mismatch")
        for name, (address, size) in buffers.items():
            raw = bytes.fromhex(step.get("clock", "69"*6)) if name == "clock" else \
                bytes.fromhex(step["configuration"] if name == "config" else step[name]) \
                if name in ("config", "frame", "record") else step[name].to_bytes(size, "little")
            require(ram[address:address+size] == raw, "Attempt publication/preservation: "+name)
        require(all(v == 0xa5 for a, v in enumerate(ram) if a not in allocated),
                "Attempt unused/reserved/alias write")
        require(iram[0x7d:] == b"\xc7"*131 and sfr[1] == profile.stack+1,
                f"Attempt SP7C cap/unwind/alias: op{step['operation']} "
                f"SP{sfr[1]:02x}, "
                + str(re.findall(r"^Max value of stack pointer=.*$", parts[final], re.M)))
        require(all(sfr[a-128] == v for a, v in current.items() if a < 256), "Attempt unowned SFR write")
        hardware = memory_dump(parts[final+4], 0x6000, 1024)
        require(all(v == current.get(a, 0x69) for a, v in enumerate(hardware, 0x6000)),
                f"Attempt case{vector['case']} op{step['operation']} unplanned XREG/FIFO write")
        peak = max(peak, stack_high_water(parts[final]))
    require(sampled <= peak <= 0x7c, "Attempt full-run SP7C cap")
    return sampled, peak


def load(output, profile=LEGACY):
    path = output/(profile.stem+".ihx")
    raw_debug = path.with_suffix(".cdb").read_bytes()
    require(sha(raw_debug) == profile.cdb_sha, "Attempt complete raw CDB before decoding")
    return (path, parse_ihex(path.read_text()), parse_symbols(path.with_suffix(".map").read_text()),
            raw_debug.decode("ascii"), path.with_suffix(".mem").read_text(),
            {m: (output/f"{profile.stem}.{m}.rst").read_text() for m in profile.modules},
            {m: (output/f"{profile.prefix}_{m}.rel").read_text() for m in profile.modules})


def main(profile=LEGACY):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--simulator", default="s51")
    args = parser.parse_args()
    path, image, symbols, debug, memory, listings, objects = load(args.output, profile)
    allocated, sites = verify(image, symbols, debug, memory, listings, objects, profile)
    count = negatives(image, symbols, debug, memory, listings, objects, profile)
    require(count == profile.negatives, "Attempt artifact negative inventory")
    check_alias(args.simulator); rejected(lambda: check_alias(args.simulator, False))
    addresses = [symbols[n] for n in ("_mac_attempt_test_config", "_mac_attempt_test_frame",
        "_mac_attempt_test_record", "_mac_radio_shared_end", "_mac_radio_reserved_end",
        "_mac_attempt_reserved_end", "_mac_radio_raw", "_mac_radio_config", "_mac_attempt_raw",
        "_mac_attempt_first", "__gptrput_PARM_2")]
    if profile.handoff:
        addresses.append(symbols["_mac_attempt_test_clock"])
        addresses.append(symbols["_mac_radio_handoff_clock"])
    sampled = peak = calls = events = 0
    for n in range(profile.cases):
        raw = subprocess.check_output([str(args.output/profile.native),
            "--vector", str(n), *map(str, addresses)], text=True, timeout=15)
        if profile.handoff:
            require(raw == subprocess.check_output([str(args.output/(profile.native+"-sanitize")),
                "--vector", str(n), *map(str, addresses)], text=True, timeout=15),
                "Handoff native/sanitizer reference disagreement")
        vector = json.loads(raw)
        require(vector["case"] == n, "Attempt missing case")
        a, b = run_vector(args.simulator, path, symbols, debug, allocated, sites, vector, profile)
        sampled = max(sampled, a); peak = max(peak, b)
        calls += len(vector["steps"]); events += sum(len(s["events"]) for s in vector["steps"])
    require((calls, events, sampled, peak) == profile.inventory, "Attempt replay/stack inventory")
    print(f"MAC {'handoff' if profile.handoff else 'attempt'}: {profile.cases} sequences/{calls} genuine calls/{events} MMIO events; "
          f"{profile.size}/32768 CODE, {profile.xdata}+64/2048 XDATA; SP{peak:02X}/7C; "
          f"{count}+1 artifact/alias negatives PASS. Synthetic intervals, no hardware timing claim.")


if __name__ == "__main__":
    main()
