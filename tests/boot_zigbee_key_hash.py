#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Genuine R22 transport/load/Verify-Key hashes; synthetic, never flash."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

import boot_zigbee_security as aes
import boot_zigbee_mmo as mmo
from boot_image import check_alias, verify_component_layout
from boot_nwk_candidates import label, records
from boot_zdo_node import rejected
from verify_firmware import cdb_address, code_bytes, parse_ihex, parse_symbols, peripheral_accesses, require

MODULES = ("timebase", "aes", "zigbee_mmo", "zigbee_key_hash", "key_hash_test")
SIZE, XDATA, STACK = 10143, 506, 0x4c
CODE_BUDGET, XDATA_BUDGET = 12288, 640
CODE_SHA = "30dd0b9dcdab02854072dda67ff5e5a96e7a01e1fe36d69b2f87cd61e53d278b"
CDB_SHA = "bf70dc769baecdb897708ffcec145ac90123f8f52ae250bd8e7ddad0a03d96a0"
MAP_SHA = "0abd112ac4ebabca3c068856022c43c5793085307efcbcf5cb19559dbc191a4d"
MEM_SHA = "7e3d9180d0ac037b25e8e0e0dc8691d06302460793444f1c40ae3a0b27572a06"
LISTINGS = {
    "timebase": ("18c5092fe0fe19cfc87f17dd97c5a69f34b756dbddb3f005ffb70cb3cb5a0ca4",
                 (265, 404, "0a1faf691bb8914da95e51e68b8169189d803fa0abf8c19863d8a19c8c1516da")),
    "aes": ("90815b12845b60e64234a918df16cb981b1f0c61dee410e19b292e829af96948",
            (3098, 5254, "06ce9ff6b2162190c45baf702bf40e5ce88e871137ec3150249cc4b8cedbf29b")),
    "zigbee_mmo": ("ea87e61ebde7f16c22afcc754267097550541c0795b423d0315d3eaf02de0a72",
                   (853, 1322, "8b0f8f5abd85ffa686f001bb98b32ef01cd6ec3862101bd7ed67f03a645600c4")),
    "zigbee_key_hash": ("eaca73679598799c094095d24d5cb34747c05e66c629c5ee895633ec3e406aed",
                        (474, 729, "9d257ef2cb15fefef5811f70ba906559ef12337db57b8654358abedeed0d8b5a")),
    "key_hash_test": ("60e81c9778527c97f83e0d28fb98b9dbfc813923f6d3debbaaaf5b4cda463360",
                      (653, 1181, "4ad08be982d7ec26c76bbec305224341577b89bed481e6cde598d1638a2f5ef6")),
}
OBJECTS = {
    **{m: mmo.OBJECTS[m] for m in ("timebase", "aes", "zigbee_mmo")},
    "zigbee_key_hash": ("e4c1471b3994fc89a844ae07add617fa376f648d55446b2b968e8c5ecc15097a", (729, 77, 3, 0, 0)),
    "key_hash_test": ("cf40afda56e7864aa99160962afa270caa4847dd06b2638e9fe07a1ccb398790", (1376, 55, 6, 0, 1)),
}
CALLER = {"key": (411, 16), "output": (427, 18), "info": (445, 6)}
RUNTIME = {"___memcpy_PARM_2": 466, "___memcpy_PARM_3": 469, "_memset_PARM_2": 474,
           "_memset_PARM_3": 475, "__gptrput_PARM_2": 477, "__modsint_PARM_2": 478,
           "__moduint_PARM_2": 482, "_memcmp_PARM_2": 487, "_memcmp_PARM_3": 490,
           "__divsint_PARM_2": 495, "__divuint_PARM_2": 499}
PEAKS = (0x71,)*9 + (0x54, 0x6b, 0x71, 0x71, 0x54, 0x54)
TRACES = (
    "72c4141f81de858d162c558c83616d9eec8cafa04149281a38b78d815719ae6d",
    "ef0b1124e5fd13afda77afb7cb37d2eba93be95636e59cd37c73f18494088678",
    "ba39bd3f15312b5610b5f458c9060546bbfb2daf73a3d012fb3107d9c2a6558e",
    "b21f3cbc76920428f7d013dd03b8d15b17509e5c92f7ec34baabaa704932b70d",
    "77fbd6f8b398583910d3ab65c15089c976ad6305abdf79a992e0780271483ffe",
    "5043646b24cc7b5e60436296d65410195ba8c6e6c226910a465f58067daf7d3e",
    "5ed6110fa5184396096dd7c384bc6709ff4faf4056c3c38e85552cccdf523e50",
    "0f442b46646cef3fb37ad473ca8ccabc8a9703cd4c152845520fa733df33c30d",
    "47c1c148a96308e15f022ee0cbd42229a857c05910ce99ca0eb67c17e13bad1b",
    "3765b25bf3416ffd1152c3f7728d72007f35fc5a7a96a3eccabc27066a33c910",
    "922448cd2477730c9118ba7a3d00cfda87855f5bf1a47943f11a04ab73fe8318",
    "bc14751282566cdec8f5ac9a907ccd2a83f1920b7a70f6cd2060e13e0b2d7fdd",
    "65f33fdf4b88b3ba88a34aee830defd54834214998f063b69394dd486c604fc1",
    "3765b25bf3416ffd1152c3f7728d72007f35fc5a7a96a3eccabc27066a33c910",
    "3765b25bf3416ffd1152c3f7728d72007f35fc5a7a96a3eccabc27066a33c910",
)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def load(output):
    p = output / "key_hash_test.ihx"
    return (p, parse_ihex(p.read_text()), parse_symbols(p.with_suffix(".map").read_text()),
            p.with_suffix(".cdb").read_bytes(), p.with_suffix(".mem").read_bytes(),
            {m: (output / f"key_hash_test.{m}.rst").read_bytes() for m in MODULES},
            {m: (output / f"{m}.rel").read_bytes() for m in MODULES})


def verify(image, symbols, debug_raw, memory, listings, objects):
    require(isinstance(debug_raw, bytes) and sha(debug_raw) == CDB_SHA, "Key hash raw CDB changed before decode")
    debug = debug_raw.decode("ascii")
    require(SIZE <= CODE_BUDGET and sha(code_bytes(image, SIZE)) == CODE_SHA, "Key hash complete CODE changed")
    require(sha(json.dumps(symbols, sort_keys=True, separators=(",", ":")).encode()) == MAP_SHA,
            "Key hash complete map changed")
    require(sha(memory) == MEM_SHA, "Key hash complete memory accounting changed")
    allocated = verify_component_layout(image, symbols, debug, memory.decode("ascii"), "kh_result",
        ("timebase.c", "aes.c", "zigbee_mmo.c", "zigbee_key_hash.c", "test_zigbee_key_hash.c"),
        xdata_budget=XDATA_BUDGET)
    require((symbols["s_XSEG"], symbols["l_XSEG"], symbols["s_SSEG"]) == (0, XDATA, STACK),
            "Key hash XDATA/stack changed")
    require("_aes_reference_encrypt" not in symbols and "C$aes_reference.c$" not in debug and
            "C$security_aes_model.c$" not in debug, "Key hash acquired a software AES/controller fallback")
    decoded, storage = aes.crypto_objects(image, listings, objects, LISTINGS, OBJECTS,
        {"timebase": (0, 25), "aes": (25, 207), "zigbee_mmo": (207, 334),
         "zigbee_key_hash": (334, 411), "key_hash_test": (411, 466)})
    for m in ("zigbee_mmo", "zigbee_key_hash"):
        require(not peripheral_accesses(dict(records(listings[m].decode("ascii")))),
                "Key hash bypassed AES driver ownership")
    require(peripheral_accesses(dict(records(listings["key_hash_test"].decode("ascii")))) ==
            [(8911, b"\x75\xa8\x00", 0xa8), (8914, b"\x75\xb8\x00", 0xb8),
             (8917, b"\x75\x9a\x00", 0x9a)], "Key hash caller IRQ initialization changed")
    require(storage == set(range(466)) and all(symbols.get(n) == a for n, a in RUNTIME.items()),
            "Key hash complete linked libc scratch boundary changed")
    require(aes.state_location(debug, "zigbee_mmo") == (207, 89) and
            aes.state_location(debug, "zigbee_key_hash") == (334, 60), "Key hash complete private state changed")
    for name, (address, size) in CALLER.items():
        require(cdb_address(debug, f"L:Ftest_zigbee_key_hash${name}$0_0$0") == address and
                re.search(rf"^S:Ftest_zigbee_key_hash\${name}\$0_0\$0\(\{{{size}\}}", debug, re.M),
                "Key hash complete caller ABI changed")
    require((symbols["_main"], symbols["_kh_before"], symbols["_kh_done"],
             symbols["_kh_checks"], symbols["_kh_case"], symbols["_kh_return"]) ==
            (8911, 8965, 8975, 451, 455, 456), "Key hash actual caller/checkpoints changed")
    require(symbols["_zigbee_key_hash"] == cdb_address(debug, "L:G$zigbee_key_hash$0$0") ==
            label(listings["zigbee_key_hash"].decode("ascii"), "zigbee_key_hash") == 7176,
            "Key hash public entry differs")
    require(tuple(symbols[f"_zigbee_key_hash_PARM_{n}"] for n in range(2, 7)) ==
            (394, 395, 398, 402, 404), "Key hash scalar/generic parameter storage changed")
    for parameter, spec in (("key", "{3}DG,SC:U"), ("purpose", "{1}SC:U"),
                            ("output", "{3}DG,SC:U"), ("timeout", "{4}SL:U"),
                            ("poll_limit", "{2}SI:U"), ("info", "{3}DG,ST__00000001:S")):
        require(re.search(rf"^S:Lzigbee_key_hash.zigbee_key_hash\${parameter}\$[^(\n]+\({re.escape(spec)}\)",
                          debug, re.M), "Key hash public representation changed")
    for n, expected in {
        1: ((0, "polls", 4), (4, "blocks", 1), (5, "aes_status", 1)),
        2: ((0, "message", 32), (32, "hash", 16), (48, "step", 6), (54, "total", 6)),
    }.items():
        found = re.findall(rf"^T:Fzigbee_key_hash\$__{n:08d}\[(.*)\]$", debug, re.M)
        require(len(found) == 1, "Key hash field record missing/duplicated")
        fields = re.findall(r"\(\{(\d+)\}S:S\$([^$]+)\$0_0\$0\(\{(\d+)\}", found[0])
        require(tuple((int(a), name, int(size)) for a, name, size in fields) == expected,
                "Key hash private/diagnostic field ABI changed")
    calls = [(a, int.from_bytes(raw[1:], "big")) for a, raw in decoded.items() if raw[0] == 0x12]
    require(sum(target == symbols["_aes128_encrypt_block"] for a, target in calls if 5760 <= a < 7078) == 1 and
            sum(target == symbols["_zigbee_mmo_hash"] for a, target in calls if 7078 <= a < 7807) == 2,
            "Key hash no longer calls actual MMO/AES")
    sites, code = aes.aes_sites(listings["aes"].decode("ascii"))
    for name in ("input", "output"):
        a = sites[name]
        require(bytes(image[a+i] for i in range(13)) ==
                bytes((0x75, 0xd6, 1 if name == "input" else 2))+bytes(9)+b"\x22",
                "Key hash AES independent nine-clock arm path changed")
    return allocated, sites, code


def native_trace(executable, number):
    text = subprocess.check_output([str(executable), "--trace", str(number)], text=True, timeout=15)
    blocks, result = [], None
    for line in text.splitlines():
        fields = line.split()
        if fields and fields[0] == "BLOCK":
            require(len(fields) == 4 and all(re.fullmatch("[0-9a-f]{32}", p) for p in fields[1:]),
                    "Malformed key hash AES trace")
            blocks.append(tuple(bytes.fromhex(p) for p in fields[1:]))
        else:
            require(result is None and len(fields) == 8 and fields[0] == "RESULT" and
                    re.fullmatch("[0-9a-f]{32}", fields[3]) and re.fullmatch("[0-9a-f]{36}", fields[4]),
                    "Incomplete/duplicate key hash trace result")
            result = {"CHECKS": int(fields[1]), "RETURN": int(fields[2]), "KEY": bytes.fromhex(fields[3]),
                      "OUTPUT": bytes.fromhex(fields[4]),
                      "INFO": int(fields[5]).to_bytes(4, "little")+bytes(map(int, fields[6:]))}
    require(result is not None and result["CHECKS"] == 3 and len(blocks) <= 5, "Key hash trace extent differs")
    require(sha(text.encode("ascii")) == TRACES[number], "Complete public key hash trace changed")
    result["AES"] = len(blocks)
    if number in (10, 11, 12):
        result["PARTIAL_KEY"] = bytes(a ^ b for a, b in zip(blocks[-1][1], blocks[-1][2])) if number == 12 else bytes(16)
        result["PARTIAL_INPUT"] = (bytes.fromhex("80000000000000000000000000000100") if number == 12 else
                                   bytes(v ^ (0x36 if number == 10 else 0x5c) for v in result["KEY"]))
    return blocks, result


class Client:
    prefix = "kh"

    def __init__(self, peaks=PEAKS):
        self.peaks = peaks
        self.snapshots = []

    @staticmethod
    def after_blocks(number, commands, write, arm, dump, symbols, expected):
        if number not in (10, 11, 12):
            return
        desc0, desc1 = symbols["_aes_dma0"], symbols["_aes_dma1"]
        for name, reg, value in (("cfg0h", 0xd5, desc0 >> 8), ("cfg0l", 0xd4, desc0 & 255),
                                 ("cfg1h", 0xd3, desc1 >> 8), ("cfg1l", 0xd2, desc1 & 255)):
            write(name, reg, value)
        arm("output", 0)
        if number != 10:
            arm("input", 2)
            write("command", 0xb3, 0x45)
            dump("xram", desc0, symbols["_aes_key"].to_bytes(2, "big")+bytes.fromhex("70b100101d41"))
            commands.append("set memory sfr 0xb3 0x44")
        dump("xram", desc1, bytes.fromhex("70b2")+symbols["_aes_output"].to_bytes(2, "big")+
             bytes.fromhex("00101e11")+bytes(24))
        dump("xram", symbols["_aes_key"], expected["PARTIAL_KEY"])
        dump("xram", symbols["_aes_input"], expected["PARTIAL_INPUT"])

    def check(self, text, ram, iram, sfr, symbols, debug, expected, number):
        require(ram[0x1e00:0x1e08] == b"KHS1\x01\x08\0\0", "Actual key hash C corpus/status failed")
        checks, case, result = (symbols[f"_kh_{name}"] for name in ("checks", "case", "return"))
        require(int.from_bytes(ram[checks:checks+4], "little") == expected["CHECKS"] and
                ram[case] == number and ram[result] == expected["RETURN"], "Key hash case/result/check count differs")
        for name, (_, n) in CALLER.items():
            a = cdb_address(debug, f"L:Ftest_zigbee_key_hash${name}$0_0$0")
            require(ram[a:a+n] == expected[name.upper()], "Key hash complete caller result/input/info differs")
        for module in ("zigbee_mmo", "zigbee_key_hash"):
            a, n = aes.state_location(debug, module)
            require(ram[a:a+n] == bytes(n), "Key hash private staging not overwritten")
        require(iram[0x7d:] == b"\xc7"*131 and sfr[1] == symbols["s_SSEG"]-1, "Key hash stack/alias/unwind changed")
        failed = number in (10, 11, 12)
        arm = 2 if number == 10 else 3 if failed else 0
        guards = aes.GUARDS | {0xd6: arm}
        require(all(sfr[r-128] == v for r, v in guards.items()), "Key hash changed unowned registers")
        used = bool(expected["AES"])
        require(ram[symbols["_aes_used"]] == used and ram[symbols["_aes_fault"]] == (9 if failed else 0),
                "Key hash AES completion/retained-fault state changed")
        final = {0xb3: 0x44 if number in (11, 12) else 0x48 if used else 8}
        for lo, hi, name in ((0xd4, 0xd5, "_aes_dma0"), (0xd2, 0xd3, "_aes_dma1")):
            address = symbols[name] if used or failed else 0
            final[lo], final[hi] = address & 255, address >> 8
        require(all(sfr[r-128] == v for r, v in final.items()), "Key hash final owned AES/DMA registers changed")
        allocated = set(range(symbols["l_XSEG"])) | set(range(0x1e00, 0x1e08))
        require(all(v == 0xa5 for a, v in enumerate(ram) if a not in allocated), "Key hash unallocated/status-tail write")
        peak = aes.check_peak(text, self.peaks[number])
        self.snapshots.append((text, ram, iram, sfr, symbols, debug, expected, number))
        return peak

    def negatives(self):
        count = 0
        for text, ram, iram, sfr, symbols, debug, expected, number in tuple(self.snapshots):
            states = [aes.state_location(debug, module) for module in ("zigbee_mmo", "zigbee_key_hash")]
            private = tuple(a for start, size in states for a in range(start, start+size))
            caller = cdb_address(debug, "L:Ftest_zigbee_key_hash$key$0_0$0")
            addresses = (
                *(("ram", a) for a in (*range(0x1e00, 0x1e08), 0x1e08, 0x1eff, symbols["l_XSEG"],
                                       *private, *range(caller, symbols["_kh_return"]+1),
                                       symbols["_aes_used"], symbols["_aes_fault"])),
                ("iram", 0x7d), ("iram", 0xff), ("sfr", 1),
                *(("sfr", a-128) for a in (0xb3, 0xd2, 0xd3, 0xd4, 0xd5)),
                *(("sfr", a-128) for a in aes.GUARDS),
            )
            count += aes.corrupt_snapshots(
                lambda **values: self.check(text, **values, symbols=symbols, debug=debug,
                                            expected=expected, number=number), ram, iram, sfr, addresses)
        return count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--simulator", default="s51")
    args = parser.parse_args()
    path, *artifacts = load(args.output)
    _, sites, code = verify(*artifacts)
    bad = aes.negatives(*artifacts, modules=MODULES, verifier=verify, code_size=SIZE)
    require(bad == 29883, "Key hash artifact-negative coverage changed")
    check_alias(args.simulator)
    rejected(lambda: check_alias(args.simulator, False))
    for peak in PEAKS:
        for text in ("", "Max value of stack pointer= 0x7d",
                     f"Max value of stack pointer= {peak-1:#x}"):
            rejected(lambda: aes.check_peak(text, peak))
    peaks, checks, calls = [], 0, 0
    client = Client()
    for number in range(15):
        trace, expected = native_trace(args.output / "host-zigbee-key-hash-tests", number)
        peak, count, blocks = aes.replay(args.simulator, path, artifacts[1], artifacts[2].decode("ascii"),
                                       sites, code, trace, expected, number, client)
        peaks.append(peak); checks += count; calls += blocks
    require((checks, calls) == (45, 51), "Key hash actual shared checks/AES calls changed")
    runtime_bad = client.negatives()
    require(runtime_bad == 3735, "Key hash snapshot-negative coverage changed")
    print(f"Zigbee keyed hash: {checks} target checks / {calls} real AES calls; "
          f"{SIZE}/{CODE_BUDGET} CODE, {XDATA}+64/{XDATA_BUDGET} XDATA, SP {max(peaks):02X}/7C; "
          f"{bad} artifact + {runtime_bad} snapshot +45 peak +1 alias negatives PASS. Not TC verification.")


if __name__ == "__main__":
    main()
