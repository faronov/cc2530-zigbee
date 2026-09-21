#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Execute real raw-IRND/health code with explicit synthetic RF/timer inputs."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import unittest

from boot_image import (
    ALIAS, check_alias, check_pc, marker, memory_dump, section, simulate, snapshot,
    snapshot_commands, verify_component_layout,
)
from boot_timebase import GUARD_SFRS, READ_OFFSETS, READER_BYTES
from prng_fixture import PRNG_LENGTHS
from radio_fifo_fixture import instructions
from radio_rx_fixture import verify_driver_listing
from verify_firmware import cdb_address, code_bytes, parse_ihex, parse_symbols, peripheral_accesses, require


CODE_SHA = "5d6296ebee7490562d8e05b3b785abfe865f16e75af20987a905a29843dae0c8"
CDB_SHA = "858bf684db4a441d9b39e64526148e28393349437a28ef30fc7bff474bc4203d"
MAP_SHA = "00cf2d2249e3fbb423f5fbd2eeb27a154c5114f6d13d02ca46bcefc0585bc256"
LENGTHS = PRNG_LENGTHS | {
    op: 1 for op in (0xa4, 0x5b, 0x23, 0x49, 0x2b, 0x3c, 0xc8, 0x68, 0x5d, 0x59, 0x1b, 0x1c, 0xca, 0x6a, 0x13)
}
LISTING_RANGES = {
    "timebase": ((0x62, 0x1f6),),
    "noise_health": ((0x1f6, 0x81d),),
    "radio_noise": ((0x81d, 0x167f),),
    "radio_noise_test": ((0, 3), (0x5f, 0x62), (3, 6), (0x167f, 0x1822)),
}
SETTINGS = (0x6189, 0x618a, 0x6180, 0x6182, 0x6194, 0x6195, 0x61b2, 0x61fa, 0x61ae, 0x618f)
VALUES = bytes((0x4c, 0, 0x0c, 0, 0x7f, 1, 0x15, 9, 0, 0))
XREADS = (
    0x624a, 0x61e1, 0x61a3, 0x61a4, 0x61a5, 0x61a8, 0x61a9, 0x61b8, 0x61b9, 0x618e,
    0x6189, 0x618b, 0x6192, 0x6193, 0x6199, 0x619b, 0x619c, 0x619d, 0x619e, 0x619f,
    0x61a1, 0x61a2, 0x61a7,
)
CAPTURE_SIZES = (4,) * 6 + (2,) * 3 + (1,) * 13
REQUEST_SIZES = (4, 2, 2, 2, 1)


def verify_code(image):
    require(hashlib.sha256(code_bytes(image, 6423)).hexdigest() == CODE_SHA,
            "IRND CODE identity changed")


def verify_listings(image, listings):
    for name, ranges in LISTING_RANGES.items():
        code = {}
        for start, end in ranges:
            code.update(instructions(image, start, end, LENGTHS))
        verify_driver_listing(code, listings[name])


def verify(image, symbols, debug, memory, listings):
    allocated = verify_component_layout(
        image, symbols, debug, memory, "radio_noise_test_result",
        ("timebase.c", "noise_health.c", "radio_noise.c", "test_radio_noise.c"),
    )
    require(len(image) <= 8192, "IRND exceeds 8-KiB CODE budget")
    verify_code(image)
    require(hashlib.sha256(debug.encode("ascii")).hexdigest() == CDB_SHA,
            "IRND complete CDB identity changed")
    canonical = json.dumps(symbols, sort_keys=True, separators=(",", ":")).encode("ascii")
    require(hashlib.sha256(canonical).hexdigest() == MAP_SHA, "IRND complete map identity changed")
    start = cdb_address(debug, "L:Fradio_noise$storage$0$0")
    end = cdb_address(debug, "L:XG$radio_noise_collect$0$0") + 1
    code = instructions(image, start, end, LENGTHS)
    verify_listings(image, listings)
    require([b.hex() for _, b, _ in peripheral_accesses(code)] == [
        "e5a8", "e5b8", "e59a", "aebe", "e5d6", "e5d7", "e5c6", "b59e02",
        "e5bf", "e5e9", "e591", "e5c6", "75e1e3",
    ], "IRND SFR whitelist changed; no TX/FIFO/RND access is allowed")
    reads = []
    for pc, data in code.items():
        if data[0] != 0x90 or int.from_bytes(data[1:], "big") < 0x6000:
            continue
        address = int.from_bytes(data[1:], "big")
        if address == 0x618d:
            require(code.get(pc + 3) == b"\x74\x80" and code.get(pc + 5) == b"\xf0",
                    "IRND stop is not RXMASKCLR80")
        else:
            require(code.get(pc + 3) == b"\xe0", "IRND unexpected static MMIO operation")
            reads.append(address)
    require(tuple(reads) == XREADS, "IRND static reads/exactly-once RFRND site changed")
    for name, expected in (("settings", b"".join(a.to_bytes(2, "little") for a in SETTINGS)),
                           ("values", VALUES)):
        address = cdb_address(debug, f"L:Fradio_noise${name}$0_0$0")
        require(bytes(image[address + i] for i in range(len(expected))) == expected,
                "IRND no-sync configuration table changed")
    require(code.get(0x9fa) == b"\xe0" and code.get(0x1094) == b"\xf0",
            "IRND reviewed dynamic profile sites changed")
    require(bytes(image[a] for a in range(0x9fa, 0xa0a)) ==
            bytes.fromhex("e0fb90005cf0bc080790005c74035bf0"),
            "IRND single-read/index8-only FSCAL1 mask changed")
    reader = symbols["_timebase_read_awake_ticks24"]
    require(bytes(image[reader + i] for i in range(len(READER_BYTES))) == READER_BYTES,
            "IRND real Sleep Timer reader changed")
    require((symbols["_radio_noise_reserved_end"], symbols["___memcpy_PARM_2"],
             symbols["_memset_PARM_2"], symbols["__gptrput_PARM_2"], symbols["l_XSEG"]) ==
            (100, 313, 321, 324, 325), "IRND private/caller/complete libc ownership changed")
    require(all(address in allocated for address in range(101, 313)),
            "IRND caller window is not entirely allocated")
    sites = {
        0x967: ("o", 0x624a), 0x12f4: ("r", 0x61a7),
        0x1094: ("w", None), 0x113b: ("w", 0xe1), 0x1609: ("w", 0x618d),
        reader + READ_OFFSETS[0]: ("t", 0x95),
    }
    return allocated, sites


def rejections(image, symbols, debug, memory, listings):
    case = unittest.TestCase()
    for address in image:
        bad = dict(image); bad[address] ^= 1
        with case.assertRaisesRegex(ValueError, "CODE"):
            verify_code(bad)
    count = len(image)
    for name in ("_radio_noise_collect", "_radio_noise_reserved_end", "___memcpy_PARM_2",
                 "_memset_PARM_2", "__gptrput_PARM_2", "s_SSEG", "l_XSEG", "__XPAGE"):
        with case.assertRaises(ValueError):
            verify(image, symbols | {name: symbols[name] + 1}, debug, memory, listings)
        count += 1
    for prefix in ("F:", "S:", "L:", "T:"):
        line = next(line for line in debug.splitlines() if line.startswith(prefix))
        with case.assertRaises(ValueError):
            verify(image, symbols, debug.replace(line + "\n", "", 1), memory, listings)
        count += 1
    with case.assertRaises(ValueError):
        verify(image, symbols, debug, memory.replace("bytes available", "bytes missing"), listings)
    count += 1
    for name in listings:
        with case.assertRaises(ValueError):
            verify(image, symbols, debug, memory, listings | {name: ""})
        count += 1
    return count


def packed(values, sizes):
    require(len(values) == len(sizes), "IRND vector shape changed")
    return b"".join(value.to_bytes(size, "little") for value, size in zip(values, sizes))


def execution(simulator, path, symbols, allocated, sites, vector):
    before, done = (symbols["_radio_noise_test_" + n] for n in ("before", "done"))
    current = GUARD_SFRS | {int(a): value for a, value in vector["initial"].items()}
    commands = [ALIAS, "fill xram 0 0x1eff 0xa5", "fill xram 0x6000 0x63ff 0x69",
                f"run 0 {symbols['_main']:#x}", "fill iram 0x7d 0xff 0xc7"]
    for address, value in current.items():
        commands.append(f"set memory {'sfr' if address < 256 else 'xram'} {address:#x} {value:#x}")
    commands.append(f"run {symbols['_main']:#x} {before:#x}")
    objects = {
        "request": packed(vector["request"], REQUEST_SIZES),
        "rct": vector["cutoffs"][0].to_bytes(2, "little"),
        "apt": vector["cutoffs"][1].to_bytes(2, "little"),
    }
    pointers = {
        0x400: symbols["_radio_noise_test_request"], 0x500: symbols["_radio_noise_test_capture"],
        0x300: symbols["_radio_noise_reserved_end"],
        0x1d00: symbols["___memcpy_PARM_2"], 0x1d08: symbols["_memset_PARM_2"],
        0x1d10: symbols["__gptrput_PARM_2"],
        0x1c55: symbols["___memcpy_PARM_2"] - 170,
        0x1cf5: symbols["___memcpy_PARM_2"] - 10,
    }
    for name, value in zip(("req_ptr", "cap_ptr"), vector["pointers"]):
        objects[name] = pointers.get(value, value).to_bytes(2, "little")
    for name, data in objects.items():
        commands.append(f"set memory xram {symbols['_radio_noise_test_' + name]:#x} " +
                        " ".join(hex(b) for b in data))
    capture = symbols["_radio_noise_test_capture"]
    commands.append(f"fill xram {capture:#x} {capture + 170:#x} 0xa5")
    commands += [f"break {pc:#x}" for pc in (*sites, before, done)]
    commands.append("step 1")
    for i, (kind, address, value, changes) in enumerate(vector["events"]):
        n = 10 + 3 * i
        commands += ["run", marker(n), "state", "dump /h sfr 0x81 0x83"]
        updates = [f"set memory {'sfr' if a < 256 else 'xram'} {a:#x} {v:#x}" for a, v in changes]
        if kind != "w":
            commands += updates
        commands += ["step 1", marker(n + 1)]
        observed = address if kind == "w" else 0xe0
        commands.append(f"dump /h {'sfr' if observed < 256 else 'xram'} {observed:#x} {observed:#x}")
        commands.append(marker(n + 2))
        if kind == "w":
            commands += updates
            current[address] = value
        current.update(changes)
    final = 12 + 3 * len(vector["events"])
    commands += ["run"] + snapshot_commands(final)
    commands += [marker(final + 4), "dump /h xram 0x6000 0x63ff", marker(final + 5)]
    if vector["result"] == 0 or vector["fault"]:
        commands += ["step 1", "run", marker(final + 6), "state", marker(final + 7),
                     "step 1", "run"] + snapshot_commands(final + 8)
    text = simulate(simulator, commands, path)
    parts = re.split(r"^0x2530([0-9a-f]{4})\r?\n", text, flags=re.MULTILINE)
    numbers = [int(parts[i], 16) for i in range(1, len(parts), 2)]
    require(len(numbers) == len(set(numbers)), "Duplicate IRND event marker")
    blocks = dict(zip(numbers, parts[2::2]))
    peak = 0
    for i, (kind, address, value, _) in enumerate(vector["events"]):
        n = 10 + 3 * i
        match = re.search(r"CPU state= OK PC= 0x([0-9a-fA-F]+)", blocks[n])
        require(match is not None and int(match[1], 16) in sites, vector["name"] + ": unplanned stop")
        site_kind, site_address = sites[int(match[1], 16)]
        require(kind == site_kind and site_address in (None, address), vector["name"] + ": MMIO order")
        registers = memory_dump(blocks[n], 0x81, 3)
        peak = max(peak, registers[0])
        if address >= 256:
            require(registers[1:] == address.to_bytes(2, "little"), "IRND actual DPTR address changed")
            if site_address is None:
                require(address in SETTINGS, "IRND dynamic write escaped no-sync profile")
        observed = address if kind == "w" else 0xe0
        require(memory_dump(blocks[n + 1], observed, 1)[0] == (value & 255),
                vector["name"] + ": actual MMIO value changed")
    check_pc(section(text, final), done)
    ram, iram, sfr = snapshot(text, final)
    snapshot_checks(ram, iram, sfr, symbols, allocated, vector, current, objects, peak)
    radio = memory_dump(section(text, final + 4), 0x6000, 1024)
    require(all(v == current.get(a, 0x69) for a, v in enumerate(radio, 0x6000)),
            "IRND unplanned radio RAM/config change")
    if vector["result"] == 0 or vector["fault"]:
        check_pc(section(text, final + 6), before)
        check_pc(section(text, final + 8), done)
        again, ai, asp = snapshot(text, final + 8)
        expected_again = bytearray(ram[capture:313])
        expected_again[symbols["_radio_noise_test_return"] - capture] = vector["fault"] or 13
        require(again[capture:313] == expected_again, "IRND terminal re-entry changed caller state")
        snapshot_checks(again, ai, asp, symbols, allocated, vector | {"result": vector["fault"] or 13},
                        current, objects, peak)
    if vector["name"] == "single":
        for region, address in ((0, 0x1e00), (0, 0x1dff), (0, capture + 43),
                                (0, capture), (0, symbols["_radio_noise_test_health"] + 4),
                                (0, symbols["_radio_noise_used"]), (1, 0x7d), (2, 1), (2, 0x28)):
            bad = [bytearray(ram), bytearray(iram), bytearray(sfr)]
            bad[region][address] ^= 1
            with unittest.TestCase().assertRaises(ValueError):
                snapshot_checks(*bad, symbols, allocated, vector, current, objects, peak)
    return peak


def snapshot_checks(ram, iram, sfr, symbols, allocated, vector, current, objects, peak):
    capture = symbols["_radio_noise_test_capture"]
    require(ram[0x1e00:0x1e08] == b"RNO1\x01\x08\0\0", "IRND result ABI changed")
    require(ram[symbols["_radio_noise_test_return"]] == vector["result"] and
            ram[symbols["_radio_noise_fault"]] == vector["fault"], vector["name"] + ": result/fault")
    used = bool(vector["fault"] or vector["result"] in (0, 13))
    require(ram[symbols["_radio_noise_used"]] == ram[symbols["_radio_noise_test_processed"]] == used,
            "IRND reset-epoch admission/health processing state changed")
    expected = packed(vector["capture"], CAPTURE_SIZES) + bytes.fromhex(vector["data"])
    require(ram[capture:capture + 171] == expected, vector["name"] + ": raw capture/timing/partial result")
    health = symbols["_radio_noise_test_health"]
    require(ram[health:health + 15] == packed(vector["health_context"], (2,) * 6 + (1,) * 3),
            "IRND complete health context differs from native composition")
    actual_health = [ram[symbols["_radio_noise_test_health_return"]], ram[health + 14],
                     int.from_bytes(ram[health + 10:health + 12], "little"),
                     int.from_bytes(ram[symbols["_radio_noise_test_first_failure"]:
                                        symbols["_radio_noise_test_first_failure"] + 2], "little")]
    require(actual_health == vector["health"], vector["name"] + ": real health/chunk state")
    if used:
        count = vector["capture"][6]
        raw = bytes.fromhex(vector["data"])
        for i in range(1024):
            bit = 0 if i >= count or vector["pattern"] == 1 else 1 if vector["pattern"] == 2 else (0x96 >> (i & 7)) & 1
            require((raw[i >> 3] >> (i & 7)) & 1 == bit, "IRND independent raw-packing oracle")
    require(all(v == 0xa5 for a, v in enumerate(ram) if a not in allocated),
            "IRND unallocated/status XDATA corruption")
    require(iram[0x7d:] == b"\xc7" * 131 and sfr[1] == symbols["s_SSEG"] + 1 and peak <= 0x7c,
            "IRND stack unwind/SP7C/alias guard")
    for address, value in current.items():
        if address < 256:
            require(sfr[address - 128] == value, f"IRND unplanned SFR change {address:02x}")
    for name, data in objects.items():
        address = symbols["_radio_noise_test_" + name]
        require(ram[address:address + len(data)] == data, "IRND changed caller input")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--simulator", default="s51")
    args = parser.parse_args()
    path = args.output / "radio_noise_test.ihx"
    image = parse_ihex(path.read_text(encoding="ascii"))
    symbols = parse_symbols(path.with_suffix(".map").read_text())
    debug, memory = (path.with_suffix(s).read_text() for s in (".cdb", ".mem"))
    listings = {name: (args.output / f"radio_noise_test.{name}.rst").read_text() for name in LISTING_RANGES}
    allocated, sites = verify(image, symbols, debug, memory, listings)
    negatives = rejections(image, symbols, debug, memory, listings)
    check_alias(args.simulator)
    with unittest.TestCase().assertRaises(ValueError):
        check_alias(args.simulator, False)
    result = subprocess.run([str(args.output / "host-radio-noise-tests"), "--vectors"],
                            capture_output=True, text=True, check=True, timeout=15)
    vectors = [json.loads(line) for line in result.stdout.splitlines()]
    require(len(vectors) == 39, "IRND scenario inventory changed")
    peaks = []
    for vector in vectors:
        try:
            peaks.append(execution(args.simulator, path, symbols, allocated, sites, vector))
        except ValueError as error:
            raise ValueError(f"{vector['name']}: {error}") from error
    peak = max(peaks)
    print(f"Raw IRND: {len(image)} CODE, {len(allocated) - 8}+64 XDATA, "
          f"{len(vectors)} genuine scenarios, {negatives} artifact +9 snapshot +1 alias negatives; "
          f"observed projected-MMIO SP{peak:02X}, SP7C/unwind guards PASS. Synthetic only; NEVER flash.")


if __name__ == "__main__":
    main()
