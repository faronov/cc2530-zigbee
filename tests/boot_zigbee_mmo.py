#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Genuine bounded AES-MMO/install-code derivation; synthetic, never flash."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

import boot_zigbee_security as aes
from boot_image import check_alias, verify_component_layout
from boot_nwk_candidates import label, listing_metrics, records
from boot_zdo_node import rejected
from verify_firmware import cdb_address, code_bytes, parse_ihex, parse_symbols, peripheral_accesses, require

MODULES = ("timebase", "aes", "zigbee_mmo", "mmo_test")
SIZE, XDATA, STACK = 9060, 426, 0x41
CODE_BUDGET, XDATA_BUDGET = 10240, 512
CODE_SHA = "0bec8d01430e85ea203849995d0744cefc68ced9d38d27008f19e352a1479eae"
CDB_SHA = "10b82acaa4d3d8730c55b75d90c83308433f29ffc4e0dcfcb7aa377c169cf27e"
MAP_SHA = "fd4ff1264257d4a6f7237250be1e3358607cd9980b0e2a3482d9c2d1f645009e"
MEM_SHA = "47688b2014baad9b78a8ea098e5f0546f50f8ddb5d70957f68a7c2a65f01cb25"
LISTINGS = {
    "timebase": ("734aec2f1325f99c0bf9d6dd8dc8b2ca912b8ba41fc85aab637f2c84c2ba96d6",
                 (265, 404, "1ec42c5a71e7c3d723ac26eaa4692fb222b7eac37031613b5684cafb1d651aae")),
    "aes": ("022853f0d8daea13309eaa67f9711cee4b8a2dc76c055e3fdab1a64e6ab0f144",
            (3098, 5254, "ee8b87b51eb66d8d70e3a42a98facde861330681eaaeb51e8bd936d480b1ba4a")),
    "zigbee_mmo": ("1b62a743231dd9d703cf82eaf4e47f8d2fc42ed09ae45b36d4b3d778ca965541",
                   (853, 1322, "952905050e70f3ad3ecb9d81e5be28ebf1b14dbe29f6d2066f5331a3f811bc81")),
    "mmo_test": ("42ccd39f44da5da9f3e8438cc057c10c3ad24e56987686af9af691251b20531d",
                 (887, 1519, "327bf72f565e9ec24be94ce61f638b2fd389c5cf54dd75a98af887b6ea6b350d")),
}
OBJECTS = {
    "timebase": aes.OBJECTS["timebase"], "aes": aes.OBJECTS["aes"],
    "zigbee_mmo": ("9c4f0c0a2a049c5343cf292e67925b45517ed4b951cadfbfa9530f92a0092a4d", (1322, 127, 18, 0, 0)),
    "mmo_test": ("31d177a340923589c6886c056de0da94fe80c4828f9abea7a04efcaa113b2332", (1553, 72, 0, 0, 1)),
}
CALLER = {"input": (334, 33), "output": (367, 18), "info": (385, 6)}
RUNTIME = {"___memcpy_PARM_2": 406, "___memcpy_PARM_3": 409, "_memset_PARM_2": 414,
           "_memset_PARM_3": 415, "__gptrput_PARM_2": 417, "_memcmp_PARM_2": 418, "_memcmp_PARM_3": 421}
PEAKS = (0x59,)*14 + (0x5a,)*19 + (0x5c, 0x47, 0x47, 0x53, 0x58, 0x47)
TRACES = (
    "e522eb82be6d08abd11cfb8fa51aafdf1016e3a06332411773e2cb02a74e2f43",
    "a9b14bc4c00ba9d7580453fb11e1d33837e7f2a78c893145b6817531572bed86",
    "4ea5711f4604473ae65f4b3cbda3199c9b3c21ec0128035a13c26fbf2f17b940",
    "d412963c73f5525913699784e972bbfda49ae0c2195f15efa654de798f55ac48",
    "f096dab77f734c0dbb2307798c9e94252b2a23a5c08c4345d95f7385a1b22e02",
    "8e7067a05083324f5b16e363f58a655f6af14ae56ee6ca888aafbfb351248278",
    "80262c80646afd3609e8849c70622462f7faa8c45852849b92fa69b7e9fe84ec",
    "7ad179abe3368ab0e5dbddf798e10ff9f8857969f42d03737ffc110e27ed1db0",
    "e01a7a4daa6710d9019b83d3706581fe65268d3304d7ea9f4994cb58dc25d4d7",
    "d147b9e5996ad11831545e4c45221a89148b17d04f4684a45d673b1ab1594187",
    "dff4463fafaabf7d8cf6a40776a9c21cce4a571b9db782e4529f19045a602365",
    "0c91f34724f56f02f0f9e0eb0fef92cd76bae409cc146e60f58d200e40b11ea5",
    "68e579bfdded3954a412b98cc51c42cc19fb69141bd9ddc7f523db2d890d2d67",
    "3a4364bb4e187791d979ab59b4c5fb0cefd13036f2fb00aededc288cdb8b606a",
    "255c96efd7a2b57a9d85f391ac5b7a6215886751e10a6fd4dbf8a6c670cacef2",
    "17628a2d54280f85c25133da6707a7dcadb68551400379a91ffd82dd1784249e",
    "fd7ebe293be455840638d20935ba5e4ff4acccb7acea574d4318f22ee39e9661",
    "fdfc67f6b69a97421cda955761c8a641f173ed2f2c17a44940b6a86ff86da1c1",
    "b6abac6662532e1b46918e2cf6c53251ec4da2bca5531aa80175a3a66d916c5b",
    "e638707e2d12692cbbaa3c6fbb1a412f8d4d9868d2076c1909cc63bfc352be35",
    "4b20242e193ac2ad5ff924504ca193fd6f3993e2f66323b7fd6f391439d96c2c",
    "696f4964684630d48e96dd84c0da89ebf6ae20252a258a1b7a3d0c7362884f3f",
    "ae5d8b76e45e9d785f6336400c520e981032061bf921f302bfc886035e6f5832",
    "b4473cd5e3ad2e2f104d4cc8927c925fa52fb0b8d97c81cb54c92f128133316a",
    "c21e5ee91267eaafab8fe3bc9d0134948733332616e2fd6d6df37e704e0af034",
    "ec3ba4b7ca795f599163005afbf0083c6efe05ebe1746d5fca0e74782fab1827",
    "68494080a8e51ab726856439ddf15e8bf1001681afcecd0d42e60fd9608dc13f",
    "bbbebd34ae2af795be5ccf519ad28bdbf222e783242475dde270bcf83298a293",
    "24d55a2683e720cd21b05edfd81445e64399cc024ea9904614fb3cb597839447",
    "7a668cfda4cdfb05cc01ae8b3f08a09ccc394b6ca7ff6ed5d82fef6cc0cdbbc5",
    "bce998969c4125f2938aed3e6081eea106b36298790fedeec77a2bf5496ee61e",
    "b01551de1fd288ec5cc14fb15c858c28cf521d0aeaca35bf6781aa5b053dcb0f",
    "8329b035d557896bfcb4090ea3af67f69795fbd8b4176e32be156b0f8f41404f",
    "61ff19082ea9eb8aa4dfac85b712ba136c9609c21f3a61f88175b6faaf2d2e5e",
    "b730fe91885bab93585545009154f50d61ddd8495e96e31972382b6929bea408",
    "512d48de07d73c543769c8d7c91b88ed249b4d486e324ff365dd66ac5c9a47ca",
    "9b9a31289c5835c2935b7f8eaf8a72ef5f9dc45e54576ea457798f7e5f84451e",
    "e522eb82be6d08abd11cfb8fa51aafdf1016e3a06332411773e2cb02a74e2f43",
    "2ce48ed67886456d3bd9afc3f098094be9767f57af7ae987e10036544af18e65",
)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def load(output):
    p = output / "mmo_test.ihx"
    return (p, parse_ihex(p.read_text()), parse_symbols(p.with_suffix(".map").read_text()),
            p.with_suffix(".cdb").read_bytes(), p.with_suffix(".mem").read_bytes(),
            {m: (output / f"mmo_test.{m}.rst").read_bytes() for m in MODULES},
            {m: (output / f"{m}.rel").read_bytes() for m in MODULES})


def verify(image, symbols, debug_raw, memory, listings, objects):
    require(isinstance(debug_raw, bytes) and sha(debug_raw) == CDB_SHA, "MMO raw CDB changed before decode")
    debug = debug_raw.decode("ascii")
    require(SIZE <= CODE_BUDGET and sha(code_bytes(image, SIZE)) == CODE_SHA, "MMO complete CODE changed")
    require(sha(json.dumps(symbols, sort_keys=True, separators=(",", ":")).encode()) == MAP_SHA,
            "MMO complete map changed")
    require(sha(memory) == MEM_SHA, "MMO complete memory accounting changed")
    allocated = verify_component_layout(image, symbols, debug, memory.decode("ascii"), "mmo_result",
        ("timebase.c", "aes.c", "zigbee_mmo.c", "test_zigbee_mmo.c"), xdata_budget=XDATA_BUDGET)
    require((symbols["s_XSEG"], symbols["l_XSEG"], symbols["s_SSEG"]) == (0, XDATA, STACK),
            "MMO XDATA/stack changed")
    require(set(listings) == set(objects) == set(MODULES), "MMO composition changed")
    require("_aes_reference_encrypt" not in symbols and "C$aes_reference.c$" not in debug and
            "C$security_aes_model.c$" not in debug, "MMO acquired a software AES/controller fallback")
    decoded, covered, storage = {}, set(), set()
    for m in MODULES:
        text = listings[m].decode("ascii")
        require(sha(listings[m]) == LISTINGS[m][0] and listing_metrics(text) == LISTINGS[m][1],
                "MMO immediate ordered linked listing changed")
        code = records(text)
        if m == "zigbee_mmo":
            require(not peripheral_accesses(dict(code)), "MMO bypassed AES driver ownership")
        if m == "mmo_test":
            require(peripheral_accesses(dict(code)) ==
                    [(8522, b"\x75\xa8\x00", 0xa8), (8525, b"\x75\xb8\x00", 0xb8),
                     (8528, b"\x75\x9a\x00", 0x9a)], "MMO caller IRQ initialization changed")
        for a, raw in code:
            span = set(range(a, a+len(raw)))
            require(not covered & span and all(image.get(a+i) == b for i, b in enumerate(raw)),
                    "MMO overlapping/non-linked instructions")
            covered.update(span); decoded[a] = raw
        segment = text.split(".area XSEG    (XDATA)", 1)[1].split(".area XABS", 1)[0]
        owned = set()
        for a, n in re.findall(r"^\s+([0-9A-F]{6})\s+\d+\s+\.ds (\d+)$", segment, re.M):
            span = set(range(int(a, 16), int(a, 16)+int(n)))
            require(span and not storage & span, "MMO compiler/private objects overlap")
            storage |= span; owned |= span
        expected = {"timebase": (0, 25), "aes": (25, 207), "zigbee_mmo": (207, 334), "mmo_test": (334, 406)}[m]
        require(owned == set(range(*expected)), "MMO complete module/private/caller span changed")
        obj = objects[m]
        require(obj.startswith(b";!FILE ") and sha(obj.split(b"\n", 1)[1]) == OBJECTS[m][0],
                "MMO complete relocatable object changed")
        areas = {n: int(s, 16) for n, s in re.findall(r"^A (\S+) size ([0-9A-F]+) flags \S+ addr \S+$",
                                                    obj.decode("ascii"), re.M)}
        extent = sum(s for n, s in areas.items() if n in ("HOME", "GSFINAL", "CSEG", "CONST") or n.startswith("GSINIT"))
        require((extent, *(areas.get(n, 0) for n in ("XSEG", "DSEG", "OSEG", "BSEG"))) == OBJECTS[m][1],
                "MMO object accounting changed")
    require(storage == set(range(406)) and
            all(symbols.get(n) == a for n, a in RUNTIME.items()), "MMO linked libc scratch boundary changed")
    require(aes.state_location(debug, "zigbee_mmo") == (207, 89), "MMO complete private state changed")
    for name, (a, n) in CALLER.items():
        require(cdb_address(debug, f"L:Ftest_zigbee_mmo${name}$0_0$0") == a and
                re.search(rf"^S:Ftest_zigbee_mmo\${name}\$0_0\$0\(\{{{n}\}}", debug, re.M),
                "MMO caller address/extent ABI changed")
    require((symbols["_main"], symbols["_mmo_before"], symbols["_mmo_done"],
             symbols["_mmo_checks"], symbols["_mmo_case"], symbols["_mmo_return"]) ==
            (8522, 8574, 8584, 391, 395, 396), "MMO actual caller/checkpoints changed")
    for name, entry, params in (
        ("zigbee_mmo_hash", 6005, (296, 298, 301, 305, 307)),
        ("install_code_derive", 6682, (315, 317, 320, 324, 326)),
    ):
        require(symbols["_"+name] == cdb_address(debug, f"L:G${name}$0$0") ==
                label(listings["zigbee_mmo"].decode("ascii"), name) == entry, "MMO public entry differs")
        require(tuple(symbols[f"_{name}_PARM_{n}"] for n in range(2, 7)) == params,
                "MMO scalar/generic parameter storage changed")
        for parameter, spec in (
            ("input" if name.endswith("hash") else "code", "{3}DG,SC:U"),
            ("length", "{2}SI:U"), ("output", "{3}DG,SC:U"),
            ("timeout", "{4}SL:U"), ("poll_limit", "{2}SI:U"), ("info", "{3}DG,ST__00000001:S"),
        ):
            require(re.search(rf"^S:Lzigbee_mmo.{name}\${parameter}\$[^(\n]+\({re.escape(spec)}\)",
                              debug, re.M), "MMO public type/representation changed")
    fields = {
        1: ((0, "polls", 4), (4, "blocks", 1), (5, "aes_status", 1)),
        2: ((0, "hash", 16), (16, "block", 16), (32, "cipher", 16), (48, "diagnostics", 29),
            (77, "info", 6), (83, "timeout", 4), (87, "poll_limit", 2)),
    }
    for n, expected in fields.items():
        found = re.findall(rf"^T:Fzigbee_mmo\$__{n:08d}\[(.*)\]$", debug, re.M)
        require(len(found) == 1, "MMO field record missing/duplicated")
        actual = re.findall(r"\(\{(\d+)\}S:S\$([^$]+)\$0_0\$0\(\{(\d+)\}", found[0])
        require(tuple((int(a), name, int(size)) for a, name, size in actual) == expected, "MMO field ABI changed")
    calls = [(a, int.from_bytes(raw[1:], "big")) for a, raw in decoded.items() if raw[0] == 0x12]
    require(sum(target == symbols["_aes128_encrypt_block"] for a, target in calls
                if 5760 <= a < 7078) == 1 and
            sum(target == 6005 for a, target in calls if 6682 <= a < 7078) == 1,
            "MMO/install-code no longer call actual hash/AES")
    sites, code = aes.aes_sites(listings["aes"].decode("ascii"))
    for name in ("input", "output"):
        a = sites[name]
        require(bytes(image[a+i] for i in range(13)) ==
                bytes((0x75, 0xd6, 1 if name == "input" else 2))+bytes(9)+b"\x22",
                "MMO AES independent nine-clock arm path changed")
    return allocated, sites, code


def native_trace(executable, number):
    text = subprocess.check_output([str(executable), "--trace", str(number)], text=True, timeout=15)
    blocks, result = [], None
    for line in text.splitlines():
        fields = line.split()
        if fields and fields[0] == "BLOCK":
            require(len(fields) == 4 and all(re.fullmatch("[0-9a-f]{32}", p) for p in fields[1:]),
                    "MMO malformed independent AES trace")
            blocks.append(tuple(bytes.fromhex(p) for p in fields[1:]))
        else:
            require(result is None and len(fields) == 8 and fields[0] == "RESULT" and
                    re.fullmatch("[0-9a-f]{66}", fields[3]) and re.fullmatch("[0-9a-f]{36}", fields[4]),
                    "MMO incomplete/duplicate trace result")
            result = {"CHECKS": int(fields[1]), "RETURN": int(fields[2]), "INPUT": bytes.fromhex(fields[3]),
                      "OUTPUT": bytes.fromhex(fields[4]),
                      "INFO": int(fields[5]).to_bytes(4, "little")+bytes(map(int, fields[6:]))}
    require(result is not None and result["CHECKS"] == 3 and len(blocks) <= 3, "MMO trace extent differs")
    result["AES"] = len(blocks)
    require(sha(text.encode("ascii")) == TRACES[number], "MMO complete public reference trace changed")
    return blocks, result


class Client:
    prefix = "mmo"

    def __init__(self):
        self.snapshots = []

    @staticmethod
    def after_blocks(number, commands, write, arm, dump, symbols, expected):
        if number != 36:
            return
        desc0, desc1 = symbols["_aes_dma0"], symbols["_aes_dma1"]
        for name, reg, value in (("cfg0h", 0xd5, desc0 >> 8), ("cfg0l", 0xd4, desc0 & 255),
                                 ("cfg1h", 0xd3, desc1 >> 8), ("cfg1l", 0xd2, desc1 & 255)):
            write(name, reg, value)
        # The third poll expires after output arm, before input arm/KEY.
        arm("output", 0)
        dump("xram", desc1, bytes.fromhex("70b2")+symbols["_aes_output"].to_bytes(2, "big")+
             bytes.fromhex("00101e11")+bytes(24))
        dump("xram", symbols["_aes_key"], bytes(16))
        dump("xram", symbols["_aes_input"], expected["INPUT"][:16])

    def check(self, text, ram, iram, sfr, symbols, debug, expected, number):
        require(ram[0x1e00:0x1e08] == b"MMO1\x01\x08\0\0", "Actual MMO C corpus/status failed")
        require(int.from_bytes(ram[391:395], "little") == expected["CHECKS"] and
                ram[395] == number and ram[396] == expected["RETURN"], "MMO case/result/check count differs")
        for name, (a, n) in CALLER.items():
            require(ram[a:a+n] == expected[name.upper()], "MMO complete caller output/input/info differs")
        require(ram[207:296] == bytes(89), "MMO private hash/block state was not overwritten")
        require(iram[0x7d:] == b"\xc7"*131 and sfr[1] == STACK-1, "MMO stack/alias/unwind changed")
        guards = aes.GUARDS | ({0xd6: 2} if number == 36 else {})
        require(all(sfr[r-128] == v for r, v in guards.items()), "MMO changed unowned registers")
        used = bool(expected["AES"])
        require(ram[symbols["_aes_used"]] == used and ram[symbols["_aes_fault"]] == (9 if number == 36 else 0),
                "MMO AES completion/retained-fault state changed")
        final = {0xb3: 0x48 if used else 8}
        for lo, hi, name in ((0xd4, 0xd5, "_aes_dma0"), (0xd2, 0xd3, "_aes_dma1")):
            address = symbols[name] if used or number == 36 else 0
            final[lo], final[hi] = address & 255, address >> 8
        require(all(sfr[r-128] == v for r, v in final.items()), "MMO final owned AES/DMA registers changed")
        allocated = set(range(XDATA)) | set(range(0x1e00, 0x1e08))
        require(all(v == 0xa5 for a, v in enumerate(ram) if a not in allocated), "MMO unallocated/status-tail write")
        peak = aes.check_peak(text, PEAKS[number])
        self.snapshots.append((text, ram, iram, sfr, symbols, debug, expected, number))
        return peak

    def negatives(self):
        count = 0
        for text, ram, iram, sfr, symbols, debug, expected, number in tuple(self.snapshots):
            addresses = (
                *(("ram", a) for a in (*range(0x1e00, 0x1e08), 0x1e08, 0x1eff, XDATA,
                                       *range(207, 296), *range(334, 397),
                                       symbols["_aes_used"], symbols["_aes_fault"])),
                ("iram", 0x7d), ("iram", 0xff), ("sfr", 1),
                *(("sfr", a-128) for a in (0xb3, 0xd2, 0xd3, 0xd4, 0xd5)),
                *(("sfr", a-128) for a in aes.GUARDS),
            )
            for space, address in addresses:
                values = dict(ram=ram, iram=iram, sfr=sfr)
                changed = bytearray(values[space])
                changed[address] ^= 1
                values[space] = bytes(changed)
                rejected(lambda: self.check(text, **values, symbols=symbols, debug=debug,
                                            expected=expected, number=number))
                count += 1
        return count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--simulator", default="s51")
    args = parser.parse_args()
    path, *artifacts = load(args.output)
    _, sites, code = verify(*artifacts)
    bad = aes.negatives(*artifacts, modules=MODULES, verifier=verify, code_size=SIZE)
    require(bad == 27597, "MMO artifact-negative coverage changed")
    check_alias(args.simulator)
    rejected(lambda: check_alias(args.simulator, False))
    for peak in PEAKS:
        for text in ("", "Max value of stack pointer= 0x7d",
                     f"Max value of stack pointer= {peak-1:#x}"):
            rejected(lambda: aes.check_peak(text, peak))
    peaks, checks, calls = [], 0, 0
    client = Client()
    for number in range(39):
        trace, expected = native_trace(args.output / "host-zigbee-mmo-tests", number)
        peak, count, blocks = aes.replay(args.simulator, path, artifacts[1], artifacts[2].decode("ascii"),
                                         sites, code, trace, expected, number, client)
        peaks.append(peak); checks += count; calls += blocks
    require((checks, calls) == (117, 58), "MMO actual shared checks/AES calls changed")
    runtime_bad = client.negatives()
    require(runtime_bad == 8034, "MMO snapshot-negative coverage changed")
    print(f"Zigbee MMO/install code: {checks} target checks / {calls} real AES calls; "
          f"{SIZE}/{CODE_BUDGET} CODE, {XDATA}+64/{XDATA_BUDGET} XDATA, SP {max(peaks):02X}/7C; "
          f"{bad} artifact + {runtime_bad} snapshot +117 peak +1 alias negatives PASS. Not provisioning.")


if __name__ == "__main__":
    main()
