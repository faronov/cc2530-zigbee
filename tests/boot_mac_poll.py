#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Genuine legacy POLL composition; optional independent no-poll allocation floor.

Synthetic caller events, real codec/TX/Association/controller instructions.
Neither executable is a board image or a radio adapter. All s51 processes
retain the shared 15-second bound, including complete-state continuations.
"""
import argparse
from functools import lru_cache
import hashlib
from pathlib import Path
import re
import tempfile

from boot_image import (
    ALIAS, check_alias, check_pc, marker, memory_dump, simulate,
    snapshot_commands, verify_component_layout,
)
from verify_firmware import code_bytes, parse_ihex, parse_symbols, require

CODE_BUDGET, XDATA_BUDGET, SP_CAP = 0x8000, 2048, 0x7c
COMMON_MODULES = ("mac_frame", "mac_tx", "mac_association")
COMMON_PUBLIC = {
    "mac_command_decode": ("mac_frame", 554, 1060),
    "mac_command_encode": ("mac_frame", 1061, 1594),
    "mac_beacon_decode": ("mac_frame", 2272, 2375),
    "mac_frame_decode": ("mac_frame", 4628, 5766),
    "mac_frame_encode": ("mac_frame", 6492, 7106),
    "mac_tx_init": ("mac_tx", 7303, 7508),
    "mac_tx_submit": ("mac_tx", 7509, 8622),
    "mac_tx_copy": ("mac_tx", 8623, 8868),
    "mac_tx_step": ("mac_tx", 9212, 12671),
    "mac_tx_release": ("mac_tx", 12672, 12756),
    "mac_association_init": ("mac_association", 12823, 12938),
    "mac_association_start": ("mac_association", 12939, 13662),
    "mac_association_step": ("mac_association", 13663, 15286),
    "mac_association_take": ("mac_association", 15287, 15430),
}
OBJECTS = {
    "mac_frame": (7009, 207, 15, 10, 1),
    "mac_tx": (5650, 191, 8, 0, 3),
    "mac_association": (2674, 65, 25, 0, 1),
    "mac_poll": (7332, 323, 5, 0, 4),
    "mac_poll_test": (7809, 1068, 0, 0, 1),
    "mac_association_test": (3812, 383, 0, 0, 0),
}
POLL = {
    "name": "mac_poll_test", "modules": COMMON_MODULES + ("mac_poll", "mac_poll_test"),
    "result": "mac_poll_result", "tag": b"POL1", "cases": 52,
    "size": 31109, "xdata": 1878, "private_end": 786, "caller_end": 1854,
    "stack": 0x4a, "gptrput": 1865, "done": 0x7746, "boundary": 0x6a65,
    "code": "35c4a8245c271cb6115c5d136887acc00bd27ef6471c5b9433bfd72a423b4277",
    "metadata": (21422, "58d34a5bc45a609e3ae72ff05192377897bff46ded0ce8617e2c8dc788465d39"),
    "public": {**COMMON_PUBLIC,
        "mac_poll_init": ("mac_poll", 16637, 16721),
        "mac_poll_start": ("mac_poll", 16722, 17961),
        "mac_poll_step": ("mac_poll", 17962, 22185),
        "mac_poll_take": ("mac_poll", 22186, 22409),
        "mac_poll_release": ("mac_poll", 22410, 22762),
        "main": ("mac_poll_test", 30471, 30537)},
    "listings": {
        "mac_frame": (4168, 7009, "ae3da97205098b5b6e4f3f8411fe1d344911c04407e2179680b5add902fa599c"),
        "mac_tx": (3729, 5650, "f9141aa8bb3363fff593f09f93bcb558a6aeefef7b14152a997047026127f1ee"),
        "mac_association": (1715, 2674, "b2e999c78a812f6f441924586343b98d9592f4facc144b4d4f31553d1b03bbef"),
        "mac_poll": (4884, 7332, "b8024503f580a1f6b55a0e13c7770857668ab8d8adc729a128193aafe8364646"),
        "mac_poll_test": (4580, 7682, "4ce59dfde9e6582aacbf83a96a4c2911d1720349271a06d0fc443f33d803a93f")},
    "data": (127, 51, "dde481525fd01bd5e8b48c757767256f46aec0876b727b688baf623c1d69e0e3"),
    "caller": {
        "poll": (786, 266), "tx": (1052, 168), "request": (1220, 35),
        "event": (1255, 48), "action": (1303, 25), "record": (1328, 143),
        "source": (1471, 17), "radio_action": (1488, 22),
        "association": (1510, 73), "association_request": (1583, 30),
        "association_event": (1613, 20), "association_record": (1633, 27),
        "header": (1660, 26), "bytes": (1686, 126), "ack": (1812, 3),
        "length": (1815, 1), "scenario": (1816, 1), "mode": (1817, 1),
        "observation": (1818, 1), "i": (1819, 1), "attempt": (1820, 1),
        "failure": (1821, 2), "grant": (1823, 2), "now": (1825, 4),
        "origin": (1829, 4), "checksum": (1833, 4)},
}
FLOOR = {
    "name": "association_tx_floor", "modules": COMMON_MODULES + ("mac_association_test",),
    "result": "mac_association_result", "tag": b"ASR1", "cases": 34,
    "size": 19780, "xdata": 870, "private_end": 463, "caller_end": 846,
    "stack": 0x44, "gptrput": 857, "done": 0x4b05,
    "code": "432c4b07bb57eae4c0368ae973cd934c97adbb2553d3c4e9122501e73be3877c",
    "metadata": (13305, "0bbb6ecc06b894e87de2d58d24cda8e049877f6edc1fd3a8e1ec2f285b5e7072"),
    "public": {**COMMON_PUBLIC, "main": ("mac_association_test", 19149, 19208)},
    "listings": {
        "mac_frame": (4168, 7009, "808744a52869cb26ff80a317f2924bb138d71d80d31fd30c2cd26262eccb38bd"),
        "mac_tx": (3729, 5650, "285a3e49f506ba4fc95d7d17e3e9a534303747e75143b8aa4495db16b1a58080"),
        "mac_association": (1715, 2674, "3b2eae4ec0a13b8cd50bb6d546f9c5311fc93dc89f6b66aa52a4bfd98fc3a2a5"),
        "mac_association_test": (2166, 3719, "26c2f7a6796de9a8e4f0581117ebbb8470589248a86230c30e7b15d0f9fd34d5")},
    "data": (93, 34, "b159bd5ce37d2b8fb7cb2329a2f072dbbcc4b8d4a252fa60b862f442e6f6af4b"),
    "caller": {
        "ctx": (463, 73), "saved": (536, 73), "request": (609, 30),
        "event": (639, 20), "record": (659, 27), "before": (686, 27),
        "body": (713, 126), "observation": (839, 1), "expected": (840, 1),
        "scenario": (841, 1), "now": (842, 4)},
}
INSTRUCTION = re.compile(r"^\s*([0-9A-Fa-f]{6})\s+((?:[0-9A-Fa-f]{2}\s+)+)"
                         r"\[\s*\d+\]\s+\d+\s+\S.*$", re.M)
DATA = re.compile(r"^\s*([0-9A-Fa-f]{6})\s+([0-9A-Fa-f]{2})\s+\d+\s+\.db\s+\S.*$", re.M)


def digest(text):
    return hashlib.sha256(text.encode("ascii")).hexdigest()


def read_cdb(path):
    # No universal-newline conversion before control-separator checks.
    return path.read_bytes().decode("ascii")


def metadata(debug, public):
    rows = []
    for line in debug.split("\n"):
        if line.startswith(("F:", "S:", "L:", "T:")):
            match = re.match(r"^(?:F:G|L:X?G)\$(\w+)\$", line)
            if not match or match[1] not in public:
                rows.append(line)
    return "\n".join(sorted(rows)) + "\n"


@lru_cache(maxsize=2)
def cdb_index(debug):
    addresses, declarations = {}, {}
    for line in debug.split("\n"):
        if line.startswith("L:"):
            key, value = line[2:].split(":", 1)
            require(re.fullmatch(r"[0-9A-Fa-f]+", value) is not None, "Malformed CDB address")
            addresses.setdefault("L:" + key, set()).add(int(value, 16))
        match = re.match(r"F:G\$(\w+)\$", line)
        if match:
            declarations.setdefault(match[1], set()).add(line)
    return addresses, declarations


@lru_cache(maxsize=8)
def records(text):
    return tuple((int(m[1], 16), bytes.fromhex(m[2])) for m in INSTRUCTION.finditer(text))


def label(text, name):
    found = re.findall(rf"^\s*([0-9A-Fa-f]{{6}})\s+\d+\s+_{re.escape(name)}:\s*$", text, re.M)
    require(len(found) == 1, "Missing/duplicate label: " + name)
    return int(found[0], 16)


def stem(module):
    return "test_" + module[:-5] if module.endswith("_test") else module


def verify(profile, image, symbols, debug, memory, listings, objects):
    p, public = profile, profile["public"]
    raw = code_bytes(image, p["size"])
    require(p["size"] <= CODE_BUDGET and hashlib.sha256(raw).hexdigest() == p["code"],
            "Whole CODE changed")
    allocated = verify_component_layout(image, symbols, debug, memory, p["result"],
        tuple(stem(m) + ".c" for m in p["modules"]), xdata_budget=XDATA_BUDGET)
    require(all(c == "\n" or 32 <= ord(c) < 127 for c in debug), "CDB control/non-ASCII separator")
    meta = metadata(debug, public)
    require((len(meta.splitlines()), digest(meta)) == p["metadata"], "Complete F/S/L/T metadata changed")
    addresses, declarations = cdb_index(debug)
    require(symbols["s_XSEG"] == 0 and symbols["l_XSEG"] == p["xdata"]
            and symbols["s_SSEG"] == p["stack"] and symbols["__gptrput_PARM_2"] == p["gptrput"],
            "Exact allocation/runtime changed")
    require(tuple(listings) == tuple(objects) == p["modules"], "Ordered module set changed")
    private, covered, starts, calls = set(), set(), {}, {}
    caller_storage = set()
    for module in p["modules"]:
        text = listings[module]
        found = records(text)
        count, size, expected = p["listings"][module]
        require(len(found) == count and digest("".join(f"{a:06x}:{b.hex()}\n" for a, b in found)) == expected,
                "Complete ordered instructions changed: " + module)
        starts[module], calls[module] = set(), set()
        local = set()
        for address, data in found:
            span = set(range(address, address + len(data)))
            require(not span.intersection(local) and not span.intersection(covered), "Overlapping instructions")
            require(all(image.get(address + i) == b for i, b in enumerate(data)), "Relocated CODE differs")
            local.update(span)
            starts[module].add(address)
            if data[0] in (2, 0x12) and len(data) == 3:
                calls[module].add(int.from_bytes(data[1:], "big"))
        require(len(local) == size, "Instruction coverage changed")
        covered.update(local)
        db = [(int(m[1], 16), int(m[2], 16)) for m in DATA.finditer(text)]
        if module == p["modules"][-1]:
            count, targets, expected = p["data"]
            require(len(db) == count and digest("".join(f"{a:06x}:{b:02x}\n" for a, b in db)) == expected,
                    "Switch/CODE fixture ordered data changed")
            require(all((db[i][1] | db[i + targets][1] << 8) in starts[module] for i in range(targets)),
                    "Switch target is not an instruction")
        else:
            require(not db, "Unexpected data in module")
        storage = caller_storage if module == p["modules"][-1] else private
        before = len(storage)
        segment = text.split(".area XSEG    (XDATA)", 1)[1].split(".area XABS", 1)[0]
        for a, n in re.findall(r"^\s+([0-9A-F]{6})\s+\d+\s+\.ds (\d+)$", segment, re.M):
            span = set(range(int(a, 16), int(a, 16) + int(n)))
            require(span and not span.intersection(private) and not span.intersection(caller_storage),
                    "Private/caller overlap")
            storage.update(span)
        require(len(storage) - before == OBJECTS[module][1], "Full private/caller storage changed")
        for a, b in db:
            require(a not in covered and image[a] == b, "Data/instruction coverage conflict")
            covered.add(a)
        areas = {n: int(s, 16) for n, s in re.findall(r"^A (\S+) size (\S+) flags", objects[module], re.M)}
        code = sum(s for n, s in areas.items() if n in ("HOME", "GSFINAL", "CSEG", "CONST")
                   or n.startswith("GSINIT"))
        require((code,) + tuple(areas.get(n, 0) for n in ("XSEG", "DSEG", "OSEG", "BSEG"))
                == OBJECTS[module] and code == size + len(db), "Object extent changed")
    require(private == set(range(p["private_end"])), "Complete private prefix changed")
    require(caller_storage == set(range(p["private_end"], p["caller_end"])), "Complete caller prefix changed")
    require(p["xdata"] - p["caller_end"] == 24 and len(image.keys() - covered) == 635,
            "Runtime storage/CODE accounting changed")
    caller = set()
    for name, (a, n) in p["caller"].items():
        prefix = f"F{stem(p['modules'][-1])}${name}$0_0$0"
        require(addresses.get("L:" + prefix) == {a} and f"S:{prefix}({{{n}}}" in debug,
                "Caller extent changed")
        span = set(range(a, a + n))
        require(not span.intersection(caller) and span <= caller_storage, "Caller allocation")
        caller.update(span)
    for name, (module, a, end) in public.items():
        require(symbols.get("_" + name) == a and label(listings[module], name) == a
                and a in starts[module] and end in starts[module] and image[end] == 0x22
                and addresses.get(f"L:G${name}$0$0") == {a}
                and addresses.get(f"L:XG${name}$0$0") == {end}, "Public entry/end changed")
        declaration = (f"F:G${name}$0_0$0({{2}}DF,SV:S),C,0,0,0,0,0" if name == "main"
                       else f"F:G${name}$0_0$0({{2}}DF,SC:U),Z,0,0,0,0,0")
        require(declarations.get(name) == {declaration}, "Public return conflict")
    done_name = p["result"].replace("_result", "_done")
    require(symbols.get("_" + done_name) == p["done"]
            and label(listings[p["modules"][-1]], done_name) == p["done"]
            and p["done"] in starts[p["modules"][-1]]
            and raw[p["done"]:p["done"] + 4] == b"\0\x80\xfe\x22", "Checkpoint changed")
    require(raw[3:6] == b"\x02" + public["main"][1].to_bytes(2, "big"), "Startup target changed")
    edges = {"mac_association": ("mac_frame_decode", "mac_command_decode"),
             p["modules"][-1]: tuple(n for n in public if n.startswith("mac_association_"))}
    if "mac_poll" in p["modules"]:
        edges["mac_poll"] = ("mac_frame_encode", "mac_frame_decode", "mac_tx_submit", "mac_tx_release")
        edges["mac_tx"] = ("mac_frame_decode",)
        edges["mac_poll_test"] += tuple(n for n in public if n.startswith("mac_poll_"))
        edges["mac_poll_test"] += ("mac_tx_init", "mac_tx_copy", "mac_tx_step")
        a = p["boundary"]
        require(a in starts["mac_poll_test"] and raw[a:a + 3] ==
                b"\x12" + label(listings["mac_poll_test"], "setup").to_bytes(2, "big"),
                "Genuine scenario continuation boundary changed")
    for module, names in edges.items():
        require(all(public[n][1] in calls[module] for n in names), "Real composition call missing: " + module)
    return allocated


def rejected(call):
    try:
        call()
    except ValueError:
        return
    raise ValueError("Negative control accepted")


def negatives(args, output):
    count = 0

    def reject(**changes):
        nonlocal count
        rejected(lambda: verify(**{**args, **changes}))
        count += 1

    p, image, debug = args["profile"], args["image"], args["debug"]
    reject(image={a: b for a, b in image.items() if a != p["size"] - 1})
    reject(image={**image, p["size"]: 0})
    a = p["public"]["mac_association_step"][1]
    reject(image={**image, a: image[a] ^ 1})
    duplicates = []
    for name, (_, a, end) in p["public"].items():
        reject(symbols={**args["symbols"], "_" + name: a + 1})
        declaration = re.findall(rf"^F:G\${name}\$[^\n]*$", debug, re.M)[0]
        wrong = declaration.replace("DF,SV:S", "DF,SC:U") if name == "main" else declaration.replace("DF,SC:U", "DF,SV:S")
        reject(debug=debug + "\n" + wrong + "\n")
        duplicates.append(declaration)
        for prefix, value in ((f"L:G${name}$0$0", a), (f"L:XG${name}$0$0", end)):
            reject(debug=debug + f"\n{prefix}:{value + 1:X}\n")
            reject(debug=debug + f"\n{prefix}:{value:X}:garbage\n")
            duplicates.append(f"{prefix}:{value:X}")
    verify(**{**args, "debug": debug + "\n" + "\n".join(duplicates) + "\n"})
    for module in p["modules"]:
        for prefix in ("F:F", "S:F", "L:F", "L:XF", "S:L", "L:L", "T:F"):
            pattern = r"^" + re.escape(prefix + stem(module)) + r"[.$][^\n]+$"
            found = re.findall(pattern, debug, re.M)
            if prefix == "L:L" and module == "mac_association_test":
                # This unchanged caller has only register locals, no L:L rows.
                pattern = r"^L:C\$test_mac_association\.c\$[^\n]+$"
                found = re.findall(pattern, debug, re.M)
            require(found, "Missing metadata negative fixture: " + pattern)
            line = found[0]
            reject(debug=debug.replace(line + "\n", "", 1))
            reject(debug=debug + "\n" + line + "\n")
            reject(debug=debug.replace(line, line + ":conflict", 1))
    for separator in ("\v", "\f", "\r", "\x85"):
        reject(debug=debug + separator)
        with tempfile.TemporaryDirectory(prefix="poll-cdb-", dir=output) as directory:
            path = Path(directory) / "bad.cdb"
            path.write_bytes(debug.encode("ascii") + separator.encode("latin1"))
            rejected(lambda: verify(**{**args, "debug": read_cdb(path)}))
            count += 1
    for module in p["modules"]:
        lines = args["listings"][module].splitlines(keepends=True)
        indices = [i for i, line in enumerate(lines) if INSTRUCTION.fullmatch(line.rstrip("\n"))]
        a, b = indices[len(indices) // 2:len(indices) // 2 + 2]
        for operation in range(3):
            changed = list(lines)
            if operation == 0:
                del changed[a]
            elif operation == 1:
                changed.insert(a, lines[a])
            else:
                changed[a], changed[b] = changed[b], changed[a]
            reject(listings={**args["listings"], module: "".join(changed)})
    caller = p["modules"][-1]
    text = args["listings"][caller]
    done_name = p["result"].replace("_result", "_done")
    reject(listings={**args["listings"], caller: text.replace("_" + done_name + ":", "_wrong:", 1)})
    line = DATA.search(text)[0]
    reject(listings={**args["listings"], caller: text.replace(line, "", 1)})
    reject(listings={**args["listings"], caller: text + "\n" + line + "\n"})
    reject(listings=dict(reversed(tuple(args["listings"].items()))))
    reject(symbols={**args["symbols"], "_" + done_name: p["done"] + 1})
    reject(symbols={**args["symbols"], "s_XSEG": 0x1f00})
    obj = args["objects"]["mac_tx"]
    require("A XSEG size BF " in obj, "Object negative did not apply")
    reject(objects={**args["objects"], "mac_tx": obj.replace("A XSEG size BF ", "A XSEG size BE ", 1)})
    available = f"{256 - p['stack']} bytes available"
    require(available in args["memory"], "Stack negative did not apply")
    reject(memory=args["memory"].replace(available, f"{255 - p['stack']} bytes available"))
    return count


def sections(text):
    marks = list(re.finditer(r"^0x2530([0-9a-fA-F]{4})\r?$", text, re.M))
    keys = [int(m[1], 16) for m in marks]
    require(len(keys) == len(set(keys)), "Duplicate snapshot marker")
    return {int(a[1], 16): text[a.end():b.start()] for a, b in zip(marks, marks[1:])}


def complete_commands(number):
    return snapshot_commands(number) + [
        marker(number + 4), "dump /h xram 0x1f00 0xffff", marker(number + 5),
    ]


def snapshot(parts, number):
    return (memory_dump(parts[number], 0, 0x1f00),
            memory_dump(parts[number + 1], 0, 256),
            memory_dump(parts[number + 2], 0x80, 128),
            memory_dump(parts[number + 4], 0x1f00, 0xe100))


def restore(memory, pc):
    ram, iram, sfr, peripheral = memory
    require(tuple(map(len, memory)) == (0x1f00, 256, 128, 0xe100), "Incomplete continuation")
    require(peripheral[:256] == iram and peripheral[256:] == b"\xa5" * 0xe000,
            "Continuation alias/peripheral guard changed")
    # SBUF writes start a synthetic UART operation; never write it. The complete
    # SFR comparison below still includes its zero reset byte. No timer runs.
    require(sfr[0x99 - 128] == 0 and sfr[0x98 - 128] == 0
            and not sfr[0x88 - 128] & 0x50 and not sfr[0xc8 - 128] & 4,
            "Continuation requires inactive synthetic UART/timers")
    commands = [ALIAS, "fill xram 0x2000 0xffff 0xa5"]
    for space, start, data in (("xram", 0, ram), ("iram", 0, iram),
                              ("sfr", 0x80, sfr[:0x19]), ("sfr", 0x9a, sfr[0x1a:])):
        for offset in range(0, len(data), 64):
            commands.append(f"set memory {space} {start + offset:#x} " +
                            " ".join(hex(b) for b in data[offset:offset + 64]))
    return commands + [f"pc {pc:#x}"]


def check_continuation(actual, expected):
    require(actual == expected, "Continuation changed complete CPU/RAM/peripheral state")


def guards(parts, number, p, allocated, pc, finished):
    check_pc(parts[number], pc)
    memory = snapshot(parts, number)
    ram, iram, sfr, peripheral = memory
    require(all(v == 0xa5 for a, v in enumerate(ram) if a not in allocated),
            "Unallocated/status-tail write")
    require(iram[128:] == b"\xc7" * 128 and peripheral[:256] == iram
            and peripheral[256:] == b"\xa5" * 0xe000, "Upper IRAM/alias/peripheral guard")
    require(sfr[1] == p["stack"] - 1 + (0 if finished else 2), "Exact stack unwind changed")
    require(all(sfr[a - 0x80] == 0 for a in (0xa8, 0xb8, 0x9a)), "Interrupt guard changed")
    peaks = re.findall(r"Max value of stack pointer=\s*0x([0-9a-fA-F]+)", parts[number])
    require(len(peaks) == 1 and int(peaks[0], 16) <= SP_CAP, "Observed stack exceeds SP7C")
    if finished:
        require(ram[0x1e00:0x1e06] == p["tag"] + b"\x01\x08", "Result ABI changed")
        failure = int.from_bytes(ram[0x1e06:0x1e08], "little")
        require(not failure, f"Real corpus failed at C line {failure}")
        require(ram[p["caller"]["scenario"][0]] == p["cases"], "Incomplete scenario corpus")
    return memory, int(peaks[0], 16)


def continuation_negatives(memory, pc):
    count = 0
    for space in range(4):
        for offset in (0, len(memory[space]) - 1):
            changed = list(memory)
            data = bytearray(changed[space])
            data[offset] ^= 1
            changed[space] = bytes(data)
            rejected(lambda: check_continuation(tuple(changed), memory))
            count += 1
        changed = list(memory)
        changed[space] = changed[space][:-1]
        rejected(lambda: restore(tuple(changed), pc))
        count += 1
    for address, value in ((0x99, 1), (0x98, 1), (0x88, 0x10), (0xc8, 4)):
        changed = list(memory)
        data = bytearray(changed[2])
        data[address - 128] = value
        changed[2] = bytes(data)
        rejected(lambda: restore(tuple(changed), pc))
        count += 1
    rejected(lambda: check_pc(f"CPU state= OK PC= 0x{pc + 1:x}", pc))
    return count + 1


def execute(simulator, path, p, allocated):
    start = p["public"]["main"][1]
    initial = [ALIAS, "fill xram 0 0x1eff 0xa5", "fill xram 0x2000 0xffff 0xa5",
        "set memory sfr 0xa8 0", "set memory sfr 0xb8 0", "set memory sfr 0x9a 0",
        f"run 0 {start:#x}", "fill iram 0x80 0xff 0xc7"]
    if "boundary" not in p:
        text = simulate(simulator, initial + [f"run {start:#x} {p['done']:#x}"] + complete_commands(1), path)
        _, peak = guards(sections(text), 1, p, allocated, p["done"], True)
        require(peak == 0x6b, "Reviewed floor peak changed")
        return peak, 1, 0
    carry, peak, negatives_count = None, 0, 0
    boundary, scenario = p["boundary"], p["caller"]["scenario"][0]
    failure = p["caller"]["failure"][0]
    for first in range(0, p["cases"], 4):
        commands = (initial + [f"run {start:#x} {boundary:#x}"] if carry is None
                    else restore(carry, boundary))
        commands += complete_commands(1) + [f"break {boundary:#x}", f"break {p['done']:#x}"]
        for index in range(4):
            number = 100 + 2 * index
            commands += ["step 1", "run", marker(number), "state",
                         f"dump /h xram {scenario:#x} {failure + 1:#x}", marker(number + 1)]
        commands += complete_commands(500)
        parts = sections(simulate(simulator, commands, path))  # Index this long transcript once.
        before, initial_peak = guards(parts, 1, p, allocated, boundary, False)
        if carry is not None:
            check_continuation(before, carry)
        else:
            negatives_count = continuation_negatives(before, boundary)
        peak = max(peak, initial_peak)
        for index in range(4):
            complete = first + index + 1
            part = parts[100 + 2 * index]
            check_pc(part, p["done"] if complete == p["cases"] else boundary)
            data = memory_dump(part, scenario, failure + 2 - scenario)
            require(data[0] == complete and data[-2:] == b"\0\0",
                    f"Genuine scenario {complete - 1} did not complete")
        finished = first + 4 == p["cases"]
        carry, high = guards(parts, 500, p, allocated, p["done"] if finished else boundary, finished)
        peak = max(peak, high)
    require(peak == 0x73, "Reviewed poll peak changed")
    return peak, p["cases"] // 4, negatives_count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--simulator", default="s51")
    parser.add_argument("--diagnose-floor", action="store_true")
    args = parser.parse_args()
    p = FLOOR if args.diagnose_floor else POLL
    path = args.output / (p["name"] + ".ihx")
    inputs = dict(profile=p, image=parse_ihex(path.read_text()),
        symbols=parse_symbols(path.with_suffix(".map").read_text()),
        debug=read_cdb(path.with_suffix(".cdb")), memory=path.with_suffix(".mem").read_text(),
        listings={m: (args.output / f"{p['name']}.{m}.rst").read_text() for m in p["modules"]},
        objects={m: (args.output / f"{m}.rel").read_text() for m in p["modules"]})
    allocated = verify(**inputs)
    count = negatives(inputs, args.output)
    check_alias(args.simulator)
    rejected(lambda: check_alias(args.simulator, alias=False))
    peak, chunks, continuation_count = execute(args.simulator, path, p, allocated)
    print(f"{p['name']}: {p['cases']} genuine cases, {p['size']} CODE, {p['xdata']}+64 XDATA, "
          f"SP{peak:02X}, {chunks} bounded processes; {p['metadata'][0]} raw F/S/L/T records, "
          f"{count} artifact +1 alias +{continuation_count} continuation negatives PASS "
          "(simulation only" + ("; independent floor, NOT poll acceptance)." if args.diagnose_floor else ")."))


if __name__ == "__main__":
    main()
