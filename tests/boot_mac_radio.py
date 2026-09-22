#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Prove and execute the actual co-owned clock/radio composition, never RF."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

from boot_image import (
    ALIAS, check_alias, check_pc, marker, memory_dump, simulate,
    snapshot_commands, verify_component_layout,
)
from boot_radio_tx_fixture import sections, snap, stack_high_water
from clock_fixture import verify_clock_code
from radio_link_fixture import LABEL, SETTINGS, records
from verify_firmware import (
    cdb_address, code_bytes, parse_ihex, parse_symbols, peripheral_accesses, require,
    verify_timebase_reader,
)

MODULES = ("timebase", "clock", "mac_time", "radio_autoack", "mac_epoch", "mac_radio", "test_mac_radio")
SIZE, XDATA, CASES = 15040, 758, 52
CODE_SHA = "8d402707573bf0c436a03b2d6aaf7a48c82c38b28127e64c78cb0ec9f9b06e90"
CDB_SHA = "5429fb55c1a3bd07c17c8fb40ca6388f871d585008c0c832ef924207eabcf6f9"
MAP_SHA = "030b66f1d4a9aecebb8f6ff0d2da7448703ea5dd5379fdccc33d60a817939520"
LIST_SHA = "46e0004f4f68b0d9ba076e05595322fc6c50d8f82621b7a1dd274432184b2a9c"
OBJECT_SHA = "b5e540d9d21c2a7a06e1efe7cec2184ef0c4a3c1f9c42e509052a99775e8d46c"
CALLER = {"config": (585, 14), "frame": (599, 128), "stamp": (727, 6),
          "operation": (733, 1), "return": (734, 1), "length": (735, 1),
          "input": (736, 2), "output": (738, 2), "limit": (740, 2), "timeout": (742, 4)}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def listing_identity(listings):
    parts = []
    for module in MODULES:
        text = listings[module]
        parts.append(module + "\n" + "".join(f"{a:06x}:{v.hex()}\n" for a, v in records(text)))
        parts.append("\n".join(line.strip() for line in text.splitlines()
                              if re.search(r"\b_\w+:|\.ds \d+$|\.area ", line)) + "\n")
    return sha("".join(parts).encode("ascii"))


def object_identity(objects):
    require(all(text.startswith(";!FILE ") for text in objects.values()), "MAC radio object header changed")
    return sha("".join(m + "\n" + objects[m].split("\n", 1)[1] for m in MODULES).encode("ascii"))


def read_cdb(path):
    raw = path.read_bytes()
    require(sha(raw) == CDB_SHA, "MAC radio complete raw CDB changed")
    return raw.decode("ascii")


def verify(image, symbols, debug, memory, listings, objects):
    require(SIZE <= 16384 and sha(code_bytes(image, SIZE)) == CODE_SHA,
            "MAC radio complete CODE/runtime/constants changed")
    require(sha(debug.encode("ascii")) == CDB_SHA, "MAC radio complete metadata changed")
    require(sha(json.dumps(symbols, sort_keys=True, separators=(",", ":")).encode()) == MAP_SHA,
            "MAC radio complete map changed")
    allocated = verify_component_layout(image, symbols, debug, memory, "mac_radio_test_result",
        tuple(m + ".c" for m in MODULES), xdata_budget=1024)
    require(symbols["l_XSEG"] == XDATA and symbols["s_SSEG"] == 0x3c,
            "MAC radio ordinary storage/stack changed")
    require(set(listings) == set(objects) == set(MODULES), "MAC radio composition changed")
    require(listing_identity(listings) == LIST_SHA and object_identity(objects) == OBJECT_SHA,
            "MAC radio ordered instructions/allocations/helpers/objects changed")
    codes, spans, covered = {}, {}, set()
    for module in MODULES:
        text = listings[module]; found = records(text); codes[module] = dict(found)
        require(f".module {module}" in text, "MAC radio wrong listing snapshot")
        for address, raw in found:
            span = set(range(address, address + len(raw)))
            require(not span & covered and all(image.get(address+i) == v for i, v in enumerate(raw)),
                    "MAC radio overlapping/non-linked instruction")
            covered.update(span)
        segment = text.split(".area XSEG    (XDATA)", 1)[1].split(".area XABS", 1)[0]
        spans[module] = set()
        for address, length in re.findall(r"^\s+([0-9A-F]{6})\s+\d+\s+\.ds (\d+)$", segment, re.M):
            span = set(range(int(address, 16), int(address, 16)+int(length)))
            require(span and not span & set().union(*spans.values()), "MAC radio overlapping allocation")
            spans[module].update(span)
    require(set().union(*(spans[m] for m in MODULES[:5])) == set(range(438)) and
            spans["mac_radio"] == set(range(438, 585)) and
            spans["test_mac_radio"] == set(range(585, 746)), "MAC radio complete private/caller prefix changed")
    require(symbols["_mac_radio_shared_end"] == 438 and symbols["_mac_radio_reserved_end"] == 584 and
            symbols["_mac_radio_raw"] == 439 and symbols["_mac_radio_config"] == 462 and
            [symbols[n] for n in ("___memcpy_PARM_2", "___memcpy_PARM_3", "_memset_PARM_2",
                                 "_memset_PARM_3", "__gptrput_PARM_2")] == [746, 749, 754, 755, 757],
            "MAC radio staging/shared/end/complete runtime scratch changed")
    for name, (address, _) in CALLER.items():
        require(symbols["_mac_radio_test_" + name] == address, "MAC radio caller ABI changed")
    for module in MODULES[4:]:
        require(not peripheral_accesses(codes[module]) and not any(
            raw[0] == 0x90 and 0x6000 <= int.from_bytes(raw[1:], "big") < 0x6400
            for raw in codes[module].values()), "MAC radio upper layer bypasses services")
    for module, names in (
        ("mac_radio", ("clock_select_init", "mac_time_init", "mac_time_read_radio", "mac_epoch_start",
                       "mac_epoch_step", "radio_autoack_acquire", "radio_autoack_receive",
                       "radio_autoack_stop", "radio_autoack_resume", "radio_autoack_send")),
        ("test_mac_radio", ("mac_radio_init", "mac_radio_now", "mac_radio_receive", "mac_radio_stop",
                            "mac_radio_resume", "mac_radio_send", "mac_time_read_live", "mac_time_read_radio")),
    ):
        for name in names:
            require(b"\x12" + symbols["_" + name].to_bytes(2, "big") in codes[module].values(),
                    "MAC radio bypasses real API: " + name)
    verify_timebase_reader(image, symbols, debug, 0x1e00, 8)
    verify_clock_code(image, symbols, debug)
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
            if code.get(pc+3) == b"\xe0":
                sites[pc+3] = ("r", address, 0xe0)
            else:
                require(len(code.get(pc+3, b"")) == 2 and code[pc+3][0] == 0x74 and
                        code.get(pc+5) == b"\xf0", "MAC radio unreviewed MOVX addressing")
                sites[pc+5] = ("w", address, address)
    radio = codes["radio_autoack"]
    labels = {name: int(address, 16) for address, name in LABEL.findall(listings["radio_autoack"])}
    base, rfd, settle = (labels[n] for n in ("_setting_address", "_read_fifo", "_cca_settle"))
    require(radio.get(base+0x215) == b"\xe0" and radio.get(base+0x8e6) == b"\xf0",
            "MAC radio indexed configuration access changed")
    sites[base+0x215] = ("r", None, 0xe0); sites[base+0x8e6] = ("w", None, None)
    require(bytes(image[a] for a in range(rfd, rfd+4)) == b"\x85\xd9\x82\x22" and
            sum(raw == b"\x12" + rfd.to_bytes(2, "big") for raw in radio.values()) == 1 and
            bytes(image[a] for a in range(settle, settle+5)) == b"\0\0\0\0\x22" and
            sum(raw == b"\x12" + settle.to_bytes(2, "big") for raw in radio.values()) == 1,
            "MAC radio destructive RFD/four real NOPs changed")
    sites[settle] = ("c", 0, None)
    return allocated, sites


def rejected(function):
    try:
        function()
    except ValueError:
        return
    raise ValueError("MAC radio negative accepted")


def negatives(image, symbols, debug, memory, listings, objects):
    count = 0

    def reject(**changes):
        nonlocal count
        args = dict(image=image, symbols=symbols, debug=debug, memory=memory, listings=listings, objects=objects)
        rejected(lambda: verify(**(args | changes))); count += 1

    for address in image:
        reject(image=image | {address: image[address] ^ 1})
    reject(image=image | {SIZE: 0})
    reject(image={a: v for a, v in image.items() if a != SIZE-1})
    for name in symbols:
        reject(symbols=symbols | {name: symbols[name] ^ 1})
    for line in debug.splitlines():
        if line.startswith(("F:", "S:", "L:", "T:")):
            reject(debug=debug.replace(line+"\n", "", 1))
            reject(debug=debug+line+"\n")
            reject(debug=debug.replace(line, line+"!", 1))
    for changed in (debug.replace("\n", "\r\n"), debug+"\n", debug+"\0"):
        reject(debug=changed)
    reject(memory=memory.replace("196 bytes available", "195 bytes available"))
    for module in MODULES:
        text = listings[module]; lines = text.splitlines(keepends=True)
        positions = [i for i, line in enumerate(lines) if records(line)]
        first, second = positions[:2]
        for operation in ("drop", "duplicate", "reorder"):
            changed = lines.copy()
            if operation == "drop": del changed[first]
            elif operation == "duplicate": changed.insert(first, changed[first])
            else: changed[first], changed[second] = changed[second], changed[first]
            reject(listings=listings | {module: "".join(changed)})
        reject(listings=listings | {module: text.replace(".ds ", ".lost ", 1)})
        for match in LABEL.finditer(text):
            reject(listings=listings | {module: text[:match.start()]+text[match.end():]})
            reject(listings=listings | {module: text + match[0] + "\n"})
        reject(objects=objects | {module: objects[module].replace("A XSEG size ", "A XSEG lost ", 1)})
    return count


def run_vector(simulator, path, symbols, debug, allocated, sites, vector):
    before, done = (symbols["_mac_radio_test_" + n] for n in ("before", "done"))
    commands = [ALIAS, "fill xram 0 0x1eff 0xa5", "fill xram 0x6000 0x63ff 0x69",
                f"run 0 {symbols['_main']:#x}", "fill iram 0x7d 0xff 0xc7",
                f"run {symbols['_main']:#x} {before:#x}"]
    commands += [f"break {pc:#x}" for pc in list(sites) + [before, done]]
    checks, number = [], 10
    for step in vector["steps"]:
        current = {int(a): v for a, v in step["initial"].items()}
        commands += [f"set memory {'sfr' if a < 256 else 'xram'} {a:#x} {v:#x}" for a, v in current.items()]
        for name in ("operation", "input", "output", "length", "limit", "timeout", "config"):
            address, size = CALLER[name]
            raw = bytes.fromhex(step["configuration"]) if name == "config" else step[name].to_bytes(size, "little")
            commands.append(f"set memory xram {address:#x} " + " ".join(hex(b) for b in raw))
        commands.append("step 1"); first = number; previous = None
        for kind, address, value in step["events"]:
            require(kind in ("r", "w", "c"), "MAC radio unknown event")
            if kind == "w" and address in (0xe1, 0xd9):
                require(step["operation"] == 4 and (address == 0xd9 or value in (0xee, 0xea)),
                        "MAC radio unapproved transmit/flush")
            space = "sfr" if address < 256 else "xram"
            # uCsim run skips a breakpoint at its current PC; period writes
            # are five adjacent genuine MOVs, so step them without another run.
            adjacent = previous is not None and previous[0] == kind == "w" and \
                0xa2 <= previous[1] < 0xa6 and address == previous[1]+1
            if not adjacent: commands.append("run")
            commands += [marker(number), "state", "dump /h sfr 0x81 0x83"]
            if kind == "r": commands.append(f"set memory {space} {address:#x} {value:#x}")
            commands += [marker(number+1), "step 4" if kind == "c" else "step 1"]
            if kind == "c":
                require(address == 0 and value == 4 and step["operation"] == 4, "MAC radio bad settling")
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
    require(number < 65536, "MAC radio transcript exceeds marker bound")
    parts = sections(simulate(simulator, commands, path)); peak = sampled = 0
    status = cdb_address(debug, "L:Fmac_radio$status$0_0$0")
    for step, first, final, current in checks:
        for i, (kind, address, value) in enumerate(step["events"]):
            n = first + 3*i
            match = re.search(r"CPU state= OK PC= 0x([0-9a-fA-F]+)", parts[n])
            require(match is not None and int(match[1], 16) in sites, "MAC radio unplanned peripheral stop")
            pc = int(match[1], 16); k, a, observed = sites[pc]
            require(kind == k and a in (None, address),
                    f"MAC radio MMIO at {pc:04x}: expected {kind}/{address:04x}, actual {k}/{a}, event{i}")
            regs = memory_dump(parts[n], 0x81, 3); sampled = max(sampled, regs[0])
            if kind == "c": check_pc(parts[n+1], pc+4); continue
            if address >= 256:
                require(regs[1:] == address.to_bytes(2, "little"), "MAC radio wrong actual DPTR")
                if a is None: require(address in SETTINGS or 0x616a <= address <= 0x6175,
                                      "MAC radio indexed MMIO escaped whitelist")
            require(memory_dump(parts[n+1], address if observed is None else observed, 1)[0] == value,
                    "MAC radio genuine MMIO result changed")
        check_pc(parts[final], done); check_pc(parts[final+6], before)
        ram, iram, sfr = snap(parts, final)
        require(ram[0x1e00:0x1e08] == b"MRC1\x01\x08\0\0", "MAC radio status ABI changed")
        require(ram[CALLER["return"][0]] == step["result"] and ram[status:status+14] == bytes.fromhex(step["status"]),
                f"MAC radio return/diagnostic mismatch case{vector['case']} op{step['operation']}")
        for name in ("frame", "stamp", "config", "operation", "input", "output", "length", "limit", "timeout"):
            address, size = CALLER[name]
            raw = bytes.fromhex(step["configuration"] if name == "config" else step[name]) \
                if name in ("config", "frame", "stamp") else step[name].to_bytes(size, "little")
            require(ram[address:address+size] == raw, "MAC radio caller publication/preservation changed: " + name)
        for module, key, sizes in (("radio_autoack", "radio", (4, 2, 2)+(1,)*18),
                                   ("mac_time", "timer", (4, 2, 2)+(1,)*6)):
            address = cdb_address(debug, f"L:F{module}$status$0_0$0")
            require(len(step[key]) == len(sizes), "MAC radio diagnostic serializer changed")
            raw = b"".join(v.to_bytes(s, "little") for v, s in zip(step[key], sizes))
            require(ram[address:address+len(raw)] == raw, "MAC radio lower-service diagnostic changed: " + key)
        require(all(v == 0xa5 for a, v in enumerate(ram) if a not in allocated),
                "MAC radio unused/status-tail/alias write")
        require(iram[0x7d:] == b"\xc7"*131 and sfr[1] == 0x3d, "MAC radio stack cap/unwind/alias changed")
        require(all(sfr[a-128] == v for a, v in current.items() if a < 256), "MAC radio guarded SFR changed")
        hardware = memory_dump(parts[final+4], 0x6000, 1024)
        require(all(v == current.get(a, 0x69) for a, v in enumerate(hardware, 0x6000)),
                "MAC radio unplanned XREG/FIFO write")
        peak = max(peak, stack_high_water(parts[final]))
    require(sampled <= peak <= 0x7c, "MAC radio full-run stack cap exceeded")
    return sampled, peak


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--simulator", default="s51")
    args = parser.parse_args(); path = args.output / "mac_radio_test.ihx"
    image = parse_ihex(path.read_text()); symbols = parse_symbols(path.with_suffix(".map").read_text())
    debug = read_cdb(path.with_suffix(".cdb")); memory = path.with_suffix(".mem").read_text()
    listings = {m: (args.output / f"mac_radio_test.{m}.rst").read_text() for m in MODULES}
    objects = {m: (args.output / (("mac_radio_test" if m == "test_mac_radio" else "mr_"+m)+".rel")).read_text()
               for m in MODULES}
    allocated, sites = verify(image, symbols, debug, memory, listings, objects)
    count = negatives(image, symbols, debug, memory, listings, objects)
    require(count == 48143, "MAC radio artifact rejection inventory changed")
    check_alias(args.simulator); rejected(lambda: check_alias(args.simulator, False))
    sampled = peak = calls = events = 0
    addresses = [symbols["_mac_radio_" + n] for n in (
        "test_config", "test_frame", "test_stamp", "shared_end", "reserved_end", "raw", "config")]
    addresses.append(symbols["__gptrput_PARM_2"])
    for n in range(CASES):
        vector = json.loads(subprocess.check_output([str(args.output / "host-mac-radio-tests"),
            "--vector", str(n), *map(str, addresses)], text=True, timeout=15))
        require(vector["case"] == n, "MAC radio missing scenario")
        a, b = run_vector(args.simulator, path, symbols, debug, allocated, sites, vector)
        sampled = max(sampled, a); peak = max(peak, b)
        calls += len(vector["steps"]); events += sum(len(s["events"]) for s in vector["steps"])
    require((calls, events, sampled, peak) == (266, 79308, 0x53, 0x55),
            "MAC radio genuine call/event/stack inventory changed")
    print(f"MAC radio: {CASES} sequences/{calls} genuine calls/{events} MMIO events; "
          f"{SIZE}/16384 CODE, {XDATA}+64/1024 XDATA; sampled/full SP={sampled:02X}/{peak:02X}; "
          f"{count}+1 artifact/alias negatives PASS. Live time, NOT physical event timestamps.")


if __name__ == "__main__":
    main()
