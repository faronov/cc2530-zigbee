#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Execute the genuine filtered AUTOACK owner against synthetic MMIO, never RF."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
from tempfile import TemporaryDirectory
from time import perf_counter

from boot_image import (
    ALIAS, check_alias, check_pc, marker, memory_dump, simulate,
    snapshot_commands, verify_component_layout,
)
from boot_timebase import READ_OFFSETS, READER_BYTES
from verify_firmware import cdb_address, code_bytes, parse_ihex, parse_symbols, peripheral_accesses, require

MODULES = ("timebase", "radio_autoack", "radio_autoack_test")
SIZE, XDATA, PRIVATE_END, CASES = 7007, 450, 281, 194
CODE_BUDGET, XDATA_BUDGET = 24576, 1536
DIGEST = "a06a14624e638adb49b87a23d7e2fe35711a32a23c53eaf53f74c6569a1bcde4"
MAP_DIGEST = "1daeb9b51dbaaab5480fa77fa95690458d03ab746e714deabb8f53bbd70b8d81"
RAW_CDB_DIGEST = "f36096a63e6868aa7ebdddb4b8b8c5bd58d507183867a7ab71f7fb3d9ec52610"
LEGACY_DIGEST = "d15ba16889fcb03932468342741390d9807a7ff652aa342aa6b2accdd87d0a32"
REARM_DIGEST = "c19e89572bc7398c799d9a9240f703e1ac5ae92b93f9d397aeefcbf3859e411a"
METADATA = {
    r"^[FSLT]:(?:X?F|L)(?:timebase|radio_autoack)[.$][^\n]+$":
        "270484dabbe7a80453a6d0c3ff964a411072ba9b97eec24a1bcbb35ed0d9a23b",
    r"^[FSLT]:(?:X?F|L)test_radio_autoack[.$][^\n]+$":
        "d79bc1826fbbf21fe78846ebcc7d3032b50ec6dee497f2c8b6a3a7032423f128",
    r"^[FSL]:(?:X?G)\$[^\n]+$":
        "9e38460e11cd12b5390dd13f94190aaef96e34a2af5070f433b8dc3f5c9c8f0d",
}
LISTINGS = {
    "timebase": (265, 404, "b5b3eefed3af6c9a7584687188094d974fb69fcd54a5db0da2034b57980f2712"),
    "radio_autoack": (3356, 5849, "72f664290a409d5174a8455dd5ebfed01dbe99d5c98821ddf518b762f3cb0fb2"),
    "radio_autoack_test": (264, 411, "1c282913bcdf82c92cb2de2742c4afc384539460df99b9fc1be62e1a7b60fd9f"),
}
STORAGE = {
    "timebase": ("7e29643ee1d1225c20d5a4ee1beaee3319490ae992627c43c22b53b006c23580",
                 "f812571971442e3efb76d6e35fc06f820d3ee3da014fbe453f6bd00509c13e80"),
    "radio_autoack": ("cdbf2d272f8183fc1f0e9c29094f553cf60010b4549933af651aefd48d04e96a",
                      "7de73352487114924d2eb8fbd81f8c6ee1052d395ac063a26c2f001859bd8b76"),
    "radio_autoack_test": ("d93f2639ffb319fb000e73407db3a9c052301e7e1c34340b064d16879a12e153",
                           "92f219f71484200af70b1ca4bcb5c3b6eb22666a6390491cf7518e71d40b66c4"),
}
OBJECTS = {
    "timebase": ((404, 25, 0, 3), "0f110e21445dc0b93ebdf51b98af4e96f29d208abff27d604bc67c01c334d968"),
    "radio_autoack": ((5888, 256, 4, 2), "71d7077749be3db10929a61710fe920583baae210cc5b04e012f413717c9e598"),
    "radio_autoack_test": ((411, 157, 2, 0), "f739df4ed377ba272918e9173752237043d9d4ec7e5031f5688fd4d497ae0220"),
}
CALLER = {
    "config": (0x119, 14), "frame": (0x127, 128), "operation": (0x1a7, 1),
    "return": (0x1a8, 1), "config_ptr": (0x1a9, 2), "output_ptr": (0x1ab, 2),
    "timeout": (0x1ad, 4), "limit": (0x1b1, 2), "diag": (0x1b3, 2), "length": (0x1b5, 1),
}
SETTINGS = (0x6180, 0x6181, 0x6182, 0x6189, 0x618a, 0x6194, 0x6195,
            0x61b2, 0x61fa, 0x61ae, 0x618f, 0x6190, 0x6191)
VALUES = bytes((1, 0x70, 0, 0x60, 0, 0x7f, 0, 0x15, 9, 0, 0, 5, 0x69))
XREADS = (
    0x624a, 0x61e1, 0x61a3, 0x61a4, 0x61a5, 0x61a8, 0x61a9, 0x61b8, 0x61b9, 0x618e,
    0x618b, 0x6192, 0x6193, 0x619b, 0x619d, 0x619e, 0x619f, 0x6199,
    0x6196, 0x6197, 0x619c, 0x61a1, 0x61a2,
    0x619b, 0x619d, 0x619c, 0x61a1, 0x61a2, 0x6189, 0x618a, 0x619a,
)
XWRITES = (
    (0xbb4, 0x6180, 1), (0xc21, 0x6189, 0x60), (0xca3, 0x618c, 1),
    (0xfad, 0x618d, 1), (0x140b, 0x6189, 0x40), (0x146a, 0x6180, 0x0c),
    (0x14d2, 0x6196, 0xf8), (0x1531, 0x6197, 0x1a), (0x1751, 0x618c, 1),
)
SETTLE = 0x11d4
STATUS_SIZES = (4, 2, 2) + (1,) * 18
INSTRUCTION = re.compile(
    r"^\s*([0-9A-F]{6})\s+((?:[0-9A-F]{2}\s+)+)\[\s*\d+\]\s+\d+\s+\S.*$", re.M,
)
LABEL = re.compile(r"^\s*([0-9A-F]{6})\s+\d+\s+(_[A-Za-z_0-9]+):\s*$", re.M)


def digest(text):
    return hashlib.sha256(text.encode("ascii")).hexdigest()


def validate_cdb(debug):
    require(re.search(r"[\x00-\x08\x0b-\x1f\x7f-\x9f\u2028\u2029]", debug) is None,
            "Non-LF control/separator in raw CDB")


def read_cdb(path):
    raw = path.read_bytes()
    require(hashlib.sha256(raw).hexdigest() == RAW_CDB_DIGEST, "Complete raw CDB identity changed")
    debug = raw.decode("utf-8")
    validate_cdb(debug)
    return debug


def records(text):
    return [(int(m[1], 16), bytes.fromhex(m[2])) for m in INSTRUCTION.finditer(text)]


def storage_records(text):
    area, found = "", []
    for line in text.split("\n"):
        match = re.search(r"\.area (\S+)", line)
        if match:
            area = match[1]
        match = re.match(r"^\s+([0-9A-F]{6})\s+\d+\s+\.ds (\d+)$", line)
        if match:
            found.append(f"{area}:{match[1]}:{match[2]}\n")
    return "".join(found)


def mask_proof(code):
    require(b"".join(data for pc, data in sorted(code.items()) if 0x40b <= pc < 0x436)
            == bytes.fromhex("e0fe9000e2f0bf15079000e274035ef08f82c00712022eae82"
                             "d0079000e2e0fdb50602800675820b020581"),
            "FSCAL1-only index/mask/readback changed")


def verify(image, symbols, debug, memory, listings, objects):
    validate_cdb(debug)
    require(hashlib.sha256(debug.encode("utf-8")).hexdigest() == RAW_CDB_DIGEST,
            "Complete CDB metadata changed")
    raw = code_bytes(image, SIZE)
    require(SIZE <= CODE_BUDGET and hashlib.sha256(raw).hexdigest() == DIGEST,
            "Whole CODE/constants/runtime changed")
    require(digest("".join(f"{k}:{v:x}\n" for k, v in sorted(symbols.items()))) == MAP_DIGEST,
            "Complete map/unused public/storage entry changed")
    allocated = verify_component_layout(
        image, symbols, debug, memory, "radio_autoack_test_result",
        ("timebase.c", "radio_autoack.c", "test_radio_autoack.c"), xdata_budget=XDATA_BUDGET,
    )
    for pattern, expected in METADATA.items():
        # Multiset, not a set: retain all original duplicate declarations.
        require(digest("\n".join(sorted(re.findall(pattern, debug, re.M))) + "\n") == expected,
                "Complete F/S/L/T declaration/entry/end/field multiset changed")
    for name, value in re.findall(r"^L:G\$([^$]+)\$[^:\n]+:([0-9A-F]+)$", debug, re.M):
        require(symbols.get("_" + name) == int(value, 16), "Public map/CDB association changed")
    require(set(listings) == set(objects) == set(MODULES), "Incomplete per-link snapshot/object set")
    codes, private, entire_coverage = {}, set(), set()
    for module in MODULES:
        require((digest(storage_records(listings[module])),
                 digest("".join(f"{a}:{n}\n" for a, n in LABEL.findall(listings[module]))))
                == STORAGE[module], "Complete allocation/entry/helper-label inventory changed")
        found = records(listings[module])
        require((len(found), sum(len(b) for _, b in found),
                 digest("".join(f"{a:06x}:{b.hex()}\n" for a, b in found))) == LISTINGS[module],
                "Whole ordered instruction inventory changed: " + module)
        codes[module] = dict(found)
        for address, data in found:
            region = set(range(address, address + len(data)))
            require(not region & entire_coverage and raw[address:address + len(data)] == data,
                    "Missing/overlapping/incorrect linked instruction")
            entire_coverage.update(region)
        areas = {n: int(size, 16) for n, size in re.findall(
            r"^A (\S+) size ([0-9A-F]+) flags \S+ addr \S+$", objects[module], re.M,
        )}
        extent = sum(size for name, size in areas.items()
                     if name in ("CSEG", "CONST", "HOME", "GSFINAL") or name.startswith("GSINIT"))
        require((extent, areas.get("XSEG", 0), areas.get("DSEG", 0), areas.get("OSEG", 0))
                == OBJECTS[module][0], "Real object CODE/XDATA/DATA/overlay changed")
        require(objects[module].startswith(";!FILE ") and
                digest(objects[module].split("\n", 1)[1]) == OBJECTS[module][1],
                "Relocatable instructions/ABI changed beyond output-path comment")
        if module != "radio_autoack_test":
            segment = listings[module].split(".area XSEG    (XDATA)", 1)[1].split(".area XABS", 1)[0]
            for address, size in re.findall(r"^\s+([0-9A-F]{6})\s+\d+\s+\.ds (\d+)$", segment, re.M):
                region = set(range(int(address, 16), int(address, 16) + int(size)))
                require(region and not region & private, "Private allocation overlap")
                private.update(region)
    require(private == set(range(PRIVATE_END)), "Complete service-prefix allocation changed")
    require(symbols["l_XSEG"] == XDATA and symbols["s_SSEG"] == 0x21, "Actual storage/stack changed")
    caller = set()
    for name, (address, size) in CALLER.items():
        symbol = "_radio_autoack_test_" + name
        require(symbols.get(symbol) == address and
                set(range(address, address + size)) <= allocated - private - caller,
                "Caller object overlaps private/runtime/status/other output")
        caller.update(range(address, address + size))
    require(caller == set(range(PRIVATE_END, 0x1b6)) and symbols["__gptrput_PARM_2"] == 0x1c1,
            "Caller/runtime scratch boundary changed")
    require({name: symbols[name] for name in (
        "___memcpy_PARM_2", "___memcpy_PARM_3", "_memset_PARM_2", "_memset_PARM_3",
    )} == {"___memcpy_PARM_2": 0x1b6, "___memcpy_PARM_3": 0x1b9,
           "_memset_PARM_2": 0x1be, "_memset_PARM_3": 0x1bf} and
        allocated - private - caller - set(range(0x1e00, 0x1e08)) == set(range(0x1b6, 0x1c2)),
        "Entire memcpy/memset/generic-store scratch suffix escaped ownership guards")
    for symbol in ("_radio_autoack_acquire", "_radio_autoack_receive", "_radio_autoack_stop",
                   "_radio_autoack_resume", "_radio_autoack_send",
                   "_radio_autoack_diagnostic", "_radio_autoack_test_cycle",
                   "_radio_autoack_test_before", "_radio_autoack_test_done", "_main",
                   "_timebase_read_awake_ticks24", "_timebase_deadline_after", "_timebase_expired"):
        matches = [int(a, 16) for text in listings.values() for a, name in LABEL.findall(text) if name == symbol]
        require(matches == [symbols[symbol]], "Missing/moved/duplicate entry/checkpoint listing label")
    code = codes["radio_autoack"]
    require(peripheral_accesses(codes["radio_autoack_test"]) == [], "Caller gained peripheral access")
    require([b.hex() for _, b, _ in peripheral_accesses(code)] == [
        "e5a8", "e5b8", "e59a", "aebe", "e5d6", "e5d7", "e5c6", "b59e02",
        "e5bf", "e5e9", "e591", "85d982", "e5bf", "e5c6", "75913b",
        "75e1ee", "8bd9", "75913d", "75e1ea",
    ], "Complete SFR inventory/exactly-once RFD leaf changed")
    require(raw[0x729:0x72d] == bytes.fromhex("85d98222") and
            sum(b == b"\x12\x07\x29" for b in code.values()) == 1,
            "Destructive RFD must have one read and one genuine call site")
    sites = {}
    for pc, data, reg in peripheral_accesses(code):
        write = data[0] == 0x75 or 0x88 <= data[0] <= 0x8f
        observed = reg if write or data[0] == 0xb5 else (
            data[2] if data[0] == 0x85 else data[0] - 0xa8 if 0xa8 <= data[0] <= 0xaf else 0xe0)
        sites[pc] = ("w" if write else "r", reg, observed)
    static, writes = [], []
    for pc, data in code.items():
        if data[0] != 0x90 or int.from_bytes(data[1:], "big") < 0x1e00:
            continue
        address = int.from_bytes(data[1:], "big")
        if code.get(pc + 3, b"")[0:1] == b"\x74":
            require(code.get(pc + 5) == b"\xf0", "Static MMIO write changed")
            writes.append((pc, address, code[pc + 3][1]))
            sites[pc + 5] = ("w", address, address)
        else:
            require(code.get(pc + 3) == b"\xe0", "Unexpected static MMIO operand")
            sites[pc + 3] = ("r", address, 0xe0); static.append(address)
    require(tuple(static) == XREADS and tuple(writes) == XWRITES, "Static MMIO/address whitelist changed")
    require(code.get(0x40b) == b"\xe0" and code.get(0xadc) == b"\xf0", "Indexed MMIO changed")
    sites[0x40b] = ("r", None, 0xe0); sites[0xadc] = ("w", None, None)
    require(raw[SETTLE:SETTLE + 5] == b"\0\0\0\0\x22" and
            sum(data == b"\x12" + SETTLE.to_bytes(2, "big") for data in code.values()) == 1,
            "CCA must execute four real NOPs through one genuine call")
    sites[SETTLE] = ("c", 0, None)
    mask_proof(code)
    for name, expected in (("settings", b"".join(a.to_bytes(2, "little") for a in SETTINGS)),
                           ("values", VALUES)):
        address = cdb_address(debug, f"L:Fradio_autoack${name}$0_0$0")
        require(raw[address:address + len(expected)] == expected, "CODE profile changed")
    reader = symbols["_timebase_read_awake_ticks24"]
    require(raw[reader:reader + len(READER_BYTES)] == READER_BYTES,
            "Unchanged genuine timebase reader lost complete ABI/instructions")
    for index, offset in enumerate(READ_OFFSETS):
        sites[reader + offset] = ("r", 0x95 + index, 0xe0)
    for name, count in (("read_awake_ticks24", 3), ("deadline_after", 2), ("expired", 1)):
        call = b"\x12" + symbols["_timebase_" + name].to_bytes(2, "big")
        require(sum(data == call for data in code.values()) == count, "Missing real timebase call")
    return allocated, sites


def rejected(function, name):
    try:
        function()
    except ValueError:
        return
    raise ValueError("Negative control accepted: " + name)


def check_legacy_native(executable):
    vectors = [
        json.loads(subprocess.check_output(
            [str(executable), "--vector", str(n), "1536", "1792", "1280", "7424"],
            text=True, timeout=15,
        )) for n in range(157)
    ]
    for count, expected in ((125, LEGACY_DIGEST), (157, REARM_DIGEST)):
        raw = json.dumps(vectors[:count], sort_keys=True, separators=(",", ":")).encode("ascii")
        require(hashlib.sha256(raw).hexdigest() == expected,
                f"Original{count} scenarios changed result/frame/diagnostic/MMIO behavior")


def negatives(image, symbols, debug, memory, listings, objects, directory):
    count = 0

    def reject(name, **changes):
        nonlocal count
        args = dict(image=image, symbols=symbols, debug=debug, memory=memory,
                    listings=listings, objects=objects)
        args.update(changes)
        rejected(lambda: verify(**args), name); count += 1

    for address in range(SIZE):
        changed = dict(image); changed[address] ^= 1
        reject("every CODE/constants/runtime byte", image=changed)
    reject("CODE extent", image={**image, SIZE: 0})
    reject("CODE missing", image={a: b for a, b in image.items() if a != SIZE - 1})
    for name in symbols:
        reject("every map symbol, including unused public entries",
               symbols={**symbols, name: symbols[name] ^ 1})
    selected = [line for line in debug.split("\n")
                if any(re.fullmatch(pattern, line) for pattern in METADATA)]
    for record in selected:
        reject("removed complete F/S/L/T record", debug=debug.replace(record + "\n", "", 1))
        reject("malformed complete F/S/L/T record", debug=debug.replace(record, record + "!", 1))
        reject("duplicate multiplicity", debug=debug + "\n" + record + "\n")
    for record in selected:
        if record.startswith(("F:G$", "S:G$")) and "DF," in record:
            changed = re.sub(r"(DF,)[^)]+", r"\1" + ("SC:U" if "DF,SV:S" in record else "SV:S"),
                             record, count=1)
            reject("conflicting public return appended", debug=debug + "\n" + changed + "\n")
        if record.startswith("S:Lradio_autoack.") and "({2}DX," in record:
            reject("conflicting XDATA parameter appended",
                   debug=debug + "\n" + record.replace("({2}DX,", "({3}DG,", 1) + "\n")
    reject("raw trailing blank line", debug=debug + "\n")
    reject("raw CRLF metadata", debug=debug.replace("\n", "\r\n"))
    with TemporaryDirectory(prefix="raw-cdb-", dir=directory) as temporary:
        path = Path(temporary) / "artifact.cdb"
        for raw in (debug.replace("\n", "\r\n").encode("utf-8"),
                    debug.encode("utf-8") + b"\n"):
            path.write_bytes(raw)
            rejected(lambda: read_cdb(path), "raw CDB loader must not normalize line endings")
            count += 1
        helper = next(r for r in selected if r.startswith("F:Fradio_autoack$consume$"))
        for separator in ("\0", "\r", "\v", "\f", "\x1c", "\x1d", "\x1e", "\x1f",
                          "\x7f", "\x85", "\u2028", "\u2029"):
            for damaged in (debug.replace(helper, helper + separator, 1),
                            debug + "\n" + separator + helper + "!\n"):
                reject("raw helper/control corruption", debug=damaged)
                path.write_bytes(damaged.encode("utf-8"))
                rejected(lambda: verify(image, symbols, read_cdb(path), memory, listings, objects),
                         "real raw CDB loader")
                count += 1
    for module in MODULES:
        text = listings[module]; lines = text.splitlines(keepends=True)
        indices = [i for i, line in enumerate(lines) if INSTRUCTION.match(line)]
        i, j = indices[len(indices) // 2:len(indices) // 2 + 2]
        for kind in ("drop", "duplicate", "reorder"):
            changed = list(lines)
            if kind == "drop": del changed[i]
            elif kind == "duplicate": changed.insert(i, changed[i])
            else: changed[i], changed[j] = changed[j], changed[i]
            reject("ordered listing " + kind, listings={**listings, module: "".join(changed)})
        reject("listing storage", listings={**listings, module: text.replace(".ds ", ".lost ", 1)})
        for match in LABEL.finditer(text):
            record = match[0]
            reject("every entry/storage/helper label removed",
                   listings={**listings, module: text[:match.start()] + text[match.end():]})
            reject("every entry/storage/helper label duplicated",
                   listings={**listings, module: text + "\n" + record + "\n"})
        reject("object allocation", objects={**objects, module: objects[module].replace("A XSEG size ", "A XSEG lost ", 1)})
    reject("stack accounting", memory=memory.replace("223 bytes available", "222 bytes available"))
    reject("source association", debug=debug.replace("C$radio_autoack.c$", "C$missing.c$"))
    code = dict(records(listings["radio_autoack"]))
    for pc in (pc for pc in code if 0x40b <= pc < 0x436):
        for offset in range(len(code[pc])):
            damaged = bytearray(code[pc]); damaged[offset] ^= 1
            rejected(lambda: mask_proof({**code, pc: bytes(damaged)}), "independent FSCAL1 mask")
            count += 1
    return count


def run_vector(simulator, path, symbols, debug, allocated, sites, vector):
    before, done = (symbols["_radio_autoack_test_" + name] for name in ("before", "done"))
    commands = [ALIAS, "fill xram 0 0x1eff 0xa5", "fill xram 0x6000 0x63ff 0x69",
                f"run 0 {symbols['_main']:#x}", "fill iram 0x80 0xff 0xc7",
                f"run {symbols['_main']:#x} {before:#x}",
                f"fill xram {CALLER['frame'][0]:#x} {CALLER['frame'][0] + 127:#x} 0x69"]
    commands += [f"break {pc:#x}" for pc in list(sites) + [before, done]]
    checks, number = [], 10
    for step in vector["steps"]:
        current = {int(a): v for a, v in step["initial"].items()}
        for address, value in current.items():
            commands.append(f"set memory {'sfr' if address < 256 else 'xram'} {address:#x} {value:#x}")
        inputs = {"operation": step["operation"], "timeout": step["timeout"], "limit": step["limit"],
                  "config_ptr": step["config_address"], "output_ptr": step["output_address"],
                  "length": step.get("length", 0)}
        for name, value in inputs.items():
            address, size = CALLER[name]
            commands.append(f"set memory xram {address:#x} " +
                            " ".join(hex(b) for b in value.to_bytes(size, "little")))
        commands.append(f"set memory xram {CALLER['config'][0]:#x} " +
                        " ".join(hex(b) for b in bytes.fromhex(step["configuration"])))
        commands.append("step 1")
        events = []
        for kind, address, value in step["events"]:
            if kind == "w" and address in (0xe1, 0xd9):
                require(step["operation"] == 4 and (address == 0xd9 or value in (0xee, 0xea)),
                        "Unpermitted FIFO write, RX flush, unconditional TX or ACK strobe")
            if kind == "c":
                require(step["operation"] == 4 and address == 0 and value == 4,
                        "Unpermitted CCA settling operation")
            commands += ["run", marker(number), "state", "dump /h sfr 0x81 0x83"]
            if kind == "r":
                commands.append(f"set memory {'sfr' if address < 256 else 'xram'} {address:#x} {value:#x}")
            commands += [marker(number + 1), "step 4" if kind == "c" else "step 1"]
            if kind == "c":
                commands.append("state")
            elif kind == "r":
                commands += ["dump /h sfr 0x82 0x83", "dump /h sfr 0xe0 0xe0", "dump /h iram 0 7"]
                if address < 256:
                    commands.append(f"dump /h sfr {address:#x} {address:#x}")
            else:
                commands.append(f"dump /h {'sfr' if address < 256 else 'xram'} {address:#x} {address:#x}")
            commands.append(marker(number + 2))
            events.append((number, kind, address, value))
            if kind != "c":
                current[address] = value
            number += 3
        final = number
        commands += ["run"] + snapshot_commands(final)
        commands += [marker(final + 4), "dump /h xram 0x6000 0x63ff", marker(final + 5),
                     "step 1", "run", marker(final + 6), "state", marker(final + 7)]
        checks.append((step, events, final, current))
        number += 8
    require(number < 65536, "Bounded transcript marker space exceeded")
    started = perf_counter()
    text = simulate(simulator, commands, path)
    simulator_seconds = perf_counter() - started
    marks = list(re.finditer(r"^0x2530([0-9a-f]{4})\r?$", text, re.M))
    require(len({m[1] for m in marks}) == len(marks), "Duplicate simulator markers")
    blocks = {int(a[1], 16): text[a.end():b.start()] for a, b in zip(marks, marks[1:])}
    sampled_peak, full_peak, total_rfd, previous_fault = 0, 0, 0, 0
    diagnostic = cdb_address(debug, "L:Fradio_autoack$status$0_0$0")
    for step, events, final, current in checks:
        observed_rfd = 0
        for n, kind, address, value in events:
            match = re.search(r"CPU state= OK PC= 0x([0-9a-fA-F]+)", blocks[n])
            require(match is not None and int(match[1], 16) in sites, "Unplanned MMIO stop")
            pc = int(match[1], 16)
            expected_kind, expected_address, observed = sites[pc]
            require(kind == expected_kind and expected_address in (None, address), "Wrong MMIO operation/operand")
            regs = memory_dump(blocks[n], 0x81, 3); sampled_peak = max(sampled_peak, regs[0])
            if kind == "c":
                check_pc(blocks[n + 1], SETTLE + 4)
                continue
            if address >= 256:
                require(regs[1:] == address.to_bytes(2, "little"), "Wrong actual MOVX DPTR operand")
                if expected_address is None:
                    require(address in SETTINGS or 0x616a <= address <= 0x6175, "Indexed MMIO escaped whitelist")
            observed = address if observed is None else observed
            require(memory_dump(blocks[n + 1], observed, 1)[0] == value, "Wrong genuine MMIO result/write")
            observed_rfd += kind == "r" and address == 0xd9
        inert = previous_fault or 6 <= step["result"] <= 9
        require(not inert or not events, "Rejected/retained call accessed hardware")
        require(observed_rfd == (0 if inert else step["diagnostics"][2]),
                "Destructive RFD was omitted or duplicated")
        previous_fault = step["fault"]
        total_rfd += observed_rfd
        check_pc(blocks[final], done); check_pc(blocks[final + 6], before)
        ram = memory_dump(blocks[final], 0, 0x1f00)
        iram = memory_dump(blocks[final + 1], 0, 256)
        sfr = memory_dump(blocks[final + 2], 0x80, 128)
        require(ram[0x1e00:0x1e08] == b"ACK1\x01\x08\0\0", "Status ABI changed")
        require(ram[CALLER["return"][0]] == step["result"] and
                ram[symbols["_radio_autoack_fault"]] == step["fault"] and
                ram[symbols["_radio_autoack_state"]] == step["state"], "Result/retained ownership mismatch")
        address, size = CALLER["diag"]
        require(int.from_bytes(ram[address:address + size], "little") == diagnostic,
                "Genuine read-only diagnostic pointer ABI changed")
        require(ram[CALLER["frame"][0]:CALLER["frame"][0] + 128] == bytes.fromhex(step["frame"]),
                "Frame/error/inactive-tail publication changed")
        require(len(step["diagnostics"]) == len(STATUS_SIZES), "Diagnostic field count changed")
        expected = b"".join(v.to_bytes(size, "little") for v, size in zip(step["diagnostics"], STATUS_SIZES))
        require(ram[diagnostic:diagnostic + 26] == expected, "Diagnostic field ABI/results changed")
        require(ram[CALLER["config"][0]:CALLER["config"][0] + 14] == bytes.fromhex(step["configuration"]),
                "Configuration input changed")
        for name, key in (("operation", "operation"), ("config_ptr", "config_address"),
                          ("output_ptr", "output_address"), ("timeout", "timeout"), ("limit", "limit"),
                          ("length", "length")):
            address, size = CALLER[name]
            require(ram[address:address + size] == step.get(key, 0).to_bytes(size, "little"), "Caller argument changed")
        require(all(value == 0xa5 for a, value in enumerate(ram) if a not in allocated),
                "Unused/status-tail/alias write")
        require(iram[128:] == b"\xc7" * 128 and sfr[1] == symbols["s_SSEG"] + 1,
                "Upper IRAM or whole-call stack unwind failed")
        peaks = re.findall(r"Max value of stack pointer=\s*0x([0-9a-fA-F]+)", blocks[final])
        require(len(peaks) == 1 and int(peaks[0], 16) <= 0x7c, "Missing/exceeded full-run stack high-water")
        full_peak = max(full_peak, int(peaks[0], 16))
        for address, value in current.items():
            if address < 256:
                require(sfr[address - 128] == value, "Guarded GPIO/IRQ/clock/SFR mutation")
        hardware = memory_dump(blocks[final + 4], 0x6000, 1024)
        require(all(v == current.get(a, 0x69) for a, v in enumerate(hardware, 0x6000)),
                "Unplanned radio RAM/register mutation")
    return sampled_peak, full_peak, total_rfd, simulator_seconds


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--simulator", default="s51")
    args = parser.parse_args()
    path = args.output / "radio_autoack_test.ihx"
    image = parse_ihex(path.read_text())
    symbols = parse_symbols(path.with_suffix(".map").read_text())
    debug = read_cdb(path.with_suffix(".cdb"))
    memory = path.with_suffix(".mem").read_text()
    listings = {m: (args.output / f"radio_autoack_test.{m}.rst").read_text() for m in MODULES}
    objects = {m: (args.output / f"{m}.rel").read_text() for m in MODULES}
    allocated, sites = verify(image, symbols, debug, memory, listings, objects)
    check_legacy_native(args.output / "host-radio-autoack-tests")
    count = negatives(image, symbols, debug, memory, listings, objects, args.output)
    started = perf_counter()
    check_alias(args.simulator)
    timings = [(perf_counter() - started, "alias")]
    started = perf_counter()
    rejected(lambda: check_alias(args.simulator, alias=False), "missing genuine IRAM alias")
    timings.append((perf_counter() - started, "missing-alias"))
    sampled = full = rfd = calls = tx_bytes = attempts = flushes = settles = 0
    overlaps, vector_timings = [], []
    for n in range(CASES):
        process = subprocess.run(
            [str(args.output / "host-radio-autoack-tests"), "--vector", str(n),
             str(CALLER["config"][0]), str(CALLER["frame"][0]), str(PRIVATE_END - 1),
             str(symbols["__gptrput_PARM_2"])], capture_output=True, text=True, check=True, timeout=15,
        )
        vector = json.loads(process.stdout)
        require(vector["case"] == n and vector["steps"], "Missing compiled native scenario")
        if 114 <= n < 125:
            operation = int(n >= 116)
            invalid = [step for step in vector["steps"] if step["result"] == 8]
            require(1 <= len(invalid) <= 16 and all(
                step["operation"] == operation and step["state"] == operation and
                not step["fault"] and not step["events"] for step in invalid),
                "Overlap partition lost its bounded, unchanged cold/RX ownership")
            require([step["result"] for step in vector["steps"]] ==
                    ([0] + [8] * len(invalid) if operation else [8] * len(invalid) + [0]) + [3, 5],
                    "Fresh overlap sequence lost genuine acquisition/empty/stop postconditions")
            overlaps.extend((operation, step["output_address" if operation else "config_address"])
                            for step in invalid)
        started = perf_counter()
        a, b, c, seconds = run_vector(args.simulator, path, symbols, debug, allocated, sites, vector)
        vector_timings.append((perf_counter() - started, n))
        timings.append((seconds, f"case{n}"))
        sampled = max(sampled, a); full = max(full, b); rfd += c; calls += len(vector["steps"])
        for step in vector["steps"]:
            for kind, address, value in step["events"]:
                tx_bytes += kind == "w" and address == 0xd9
                attempts += kind == "w" and address == 0xe1 and value == 0xea
                flushes += kind == "w" and address == 0xe1 and value == 0xee
                settles += kind == "c"
    helper = symbols["__gptrput_PARM_2"]
    require(overlaps == [(0, a) for a in range(helper - 24, helper + 1)] +
            [(1, a) for a in range(helper - 138, helper + 1)],
            "Every original libc overlap must execute exactly once across the fresh partitions")
    inventory = (calls, rfd, sampled, full, count, tx_bytes, attempts, flushes, settles)
    require(inventory == (1732, 1157, 0x2e, 0x34, 9017, 419, 22, 29, 22),
            f"Complete corpus, negative inventory or measured stack high-water changed: {inventory}")
    print(f"AUTOACK: {CASES} sequences/{calls} genuine API calls/{rfd} exactly-once RFD reads; "
          f"{SIZE} CODE SHA256={DIGEST}; {XDATA}+64/{XDATA_BUDGET} XDATA; "
          f"MMIO SP={sampled:02X}, whole-run SP={full:02X}; {count}+1 artifact/alias negatives PASS. "
          f"{tx_bytes} TXFIFO writes/{attempts} CCA attempts/{flushes} TX-only flushes/"
          f"{settles} genuine four-NOP settling calls. "
          "Original125 and pre-TX157 native result/frame/diagnostic/MMIO identities unchanged. "
          "Synthetic controller only; no silicon, ACK timing or MAC/POLL acceptance.")
    seconds, name = max(timings)
    whole, case = max(vector_timings)
    print(f"AUTOACK timing: max simulator-call envelope {seconds:.3f}s ({name}); "
          f"includes script I/O/output validation, not just process execution; "
          f"15s subprocess margin at least {15 - seconds:.3f}s. "
          f"Max whole run_vector {whole:.3f}s (case{case}).")


if __name__ == "__main__":
    main()
