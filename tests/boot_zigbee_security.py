#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Genuine CCM/NWK/APS/AES/DMA composition. Synthetic peripherals, never flash."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

from boot_aes import AES_ALIAS, READS, FIELDS as AES_FIELDS, SIZES as AES_SIZES
from boot_image import (
    ALIAS, check_alias, check_pc, marker, memory_dump, section, simulate, snapshot,
    snapshot_commands, verify_component_layout,
)
from boot_nwk_candidates import label, listing_metrics, records
from boot_timebase import GUARD_SFRS, READER_BYTES
from boot_zdo_node import rejected
from verify_firmware import (
    cdb_address, code_bytes, parse_ihex, parse_symbols, peripheral_accesses, require,
    verify_deadline_helper,
)

MODULES = ("timebase", "aes", "ccm_star", "nwk_frame", "aps_frame", "zigbee_security", "security_test")
SOURCES = tuple(m + ".c" for m in MODULES[:-1]) + ("test_zigbee_security.c",)
CODE_BUDGET, XDATA_BUDGET = 24576, 2048
SIZE, XDATA, STACK = 20250, 1907, 0x55
CODE_SHA = "d996988b24338e02f0725ea30ebcc737566ecbd0ccd3ddfac125f7bd4c1aa42f"
CDB_SHA = "068db367a39e80573bab8501ca5873dadc4992c1f5f1122edc74e1fa482ba2a5"
MAP_SHA = "0caf5377ee9ad163028eec3b2c7981fc0ed22013bb20411a69afe3a5316c1435"
MEM_SHA = "78ab04bc4ca235aa683e89cc6efe75f977bfe14e4655276f13051c8dfbafbd7a"
LISTINGS = {
    "timebase": ("d47088c626cb6732eeff97a7b0de061a4f4ea1e20be69feca1a5cc17f43d40d6",
                 (265, 404, "83eb3a8d0352bcc0531b63d7562a28fef03c35737248736d9ffbd42d0c2e2a84")),
    "aes": ("fa2039230be750ab0213eb834612401a1c8c6953e989984945271a2d7072d9f8",
            (3098, 5254, "bf23d0b4a47874fc81a1378b9e02d98dc7ffde1ae0461b2d43f46ba66b744986")),
    "ccm_star": ("0978bc9eab6780f54483d63e4b6e208a4c976414d213f028f7cbba6e5b4fc04b",
                 (1308, 2103, "155b162a318680fdcdadb49554985cddd6c17d0ca7e36744e8de9288f70c6f61")),
    "nwk_frame": ("37b8a36001e125475c4338763e5360fd9a3c97b9cdae717a5d082cb05a4a125f",
                  (1344, 2294, "45fbe8a63096ca9796d8d1b84a3fec69355b7aff8dfa0c3363785c1d7a722099")),
    "aps_frame": ("a06658f78b5906e2dc4f36ede07764437175d0bd57697df7fe16ee7939037df4",
                  (939, 1626, "1907fcda928b27c77430308efa106b8a70f33598de74f9fb53bdc6db40730826")),
    "zigbee_security": ("892966b96530d741cc9844d34e9b7df2e4ce18bd9d076f5086d25d03d4e3ff6a",
                        (2650, 4124, "2ff4955261da8215ec8ec3bbacd5ade9c7885c0343a2e69a67d1bef842100fda")),
    "security_test": ("01457342f277ca9f7a6c300a07fd0dd232468bf0546a3d946acd3add7221d928",
                      (2087, 3477, "a852940d6ae70966adbe5ebc4956e336de41c24380e23648e41bbc68526298c4")),
}
OBJECTS = {
    "timebase": ("0f110e21445dc0b93ebdf51b98af4e96f29d208abff27d604bc67c01c334d968", (404, 25, 0, 3, 1)),
    "aes": ("473d9a9bcd8b8e45e8ecf10990c6259cb1c5bb4eff79161209f610701bfb8765", (5254, 182, 32, 0, 3)),
    "ccm_star": ("bcbaf24e1c79201ad27b2d77d6ec78a47271784d4acf64a54ce96dcdf2a4460d", (2103, 314, 0, 0, 1)),
    "nwk_frame": ("a68a2c6fd719dc398c33cfaaab73311be5d646b056e18ac21a23482580ce6171", (2294, 96, 12, 10, 0)),
    "aps_frame": ("98103aafd255517b26d841dc2322f0c3732ad696f93c3af08767e0ac8fb639b2", (1626, 69, 8, 7, 0)),
    "zigbee_security": ("600cf878a4e643abf6a3def25e293f2fdb85ff144da26ea06747e341bf422117", (4124, 395, 9, 4, 1)),
    "security_test": ("f7ab1e9ed796c35bd38954ca9725722e91071d05e409012d000b256644790077", (3605, 794, 0, 0, 3)),
}
TRACES = (
    "5a54e9e8a47becbd9d2a333a316e19f80459364226b14a88fbf913646f58e3de",
    "b86c9f25902ec960b11f280eea0221317138abb022893a80604776e9c29037a3",
    "be4c4b93aa563813bb074caa11d6464c1d46a26f2d0e5cbe9314badf6a528013",
    "c49534c6338b5f3e2b8598f26ab83ae53e054aad54964860c8e1e0760883b47f",
    "0cd95ea69fae45856affcd20f77faffdca93190ea6995e2ef5d9e9f7cdab9f7e",
    "92b7c58ff3e7123fdd9fa29801d38280147f88e25c2c8865dac46fc765dc8c9b",
    "aed0cab8b939a87f8b66b10d97f8801e7055504884a805c74d550d44647246a1",
    "4460897dc0ff8eae356a88537e90ca38081c3bd2f46e84d5c365fdf719854a9e",
    "29e665ff051e070b0ccbeb721c3bac792ccd4baa5eb557ae708db8f68c5d444d",
    "983b5334a2c3773866eb527735d8ed0154e64f33fbc5079b16920ca6ec6d0dc0",
    "eca36783ec3b93ed05b1d90dee3a766dbcdc8154ac14cd7360bb603f63a09a74",
    "fdcfb29ff65d75864179c5323a0ac5d86315c02802c65a951689b08c64df5a2b",
    "93ba30ec4e592bd642e4302032eb811023c1f743258118b2dfd4b97055710598",
    "3256299a296c74a2cc7b0112248475b118790f114b25f651ad361dfbd512de01",
    "5c49027efb57d42f85480cc9a9b4eb7c54d10682d28af255712ce876d94a697c",
    "eb9f38a4684e4549e732489b184f5bf705f593d0843d38d29c9362f5b97364fd",
    "8c100ff4e4ba9b5ef4ac4abb25bd22f9397251e04341662692885323015c99b8",
    "643d22a375e05189e263e713afb9e0fd4d817eafe8af024fe9b9fc0eb0907677",
    "e9ac841c590193c6dc28ef5ab87693b7f9f41ed6a9987870821d801b3968a053",
    "34cab7dc968413d16985584c9d2ee03d38f2b4879ee2b9175c763a5bd42d1fc9",
    "e9b7ad6749d3c49999295c2476bebdb9fa765fe21c94d14033c56c8b8e8370d3",
    "26bb00cc44d46b85a8a48535b9ca0a5e9091a891bc17aac9d752e94177e3d1d0",
    "3c11ee11806de8f1eb68404023e412da3fd6a8411139529f1c97972d394bbfda",
)
PEAKS = (0x79, 0x7b, 0x7b, 0x75) + (0x7c,) * 18 + (0x78,)
RUNTIME = {
    "__divuint_PARM_2": 1875, "___memcpy_PARM_2": 1882, "___memcpy_PARM_3": 1885,
    "_memset_PARM_2": 1890, "_memset_PARM_3": 1891, "__gptrput_PARM_2": 1893,
    "__moduint_PARM_2": 1894, "_memcmp_PARM_2": 1899, "_memcmp_PARM_3": 1902,
}
CALLER = {
    "input": (1081, 132), "output": (1213, 134), "wire": (1347, 132),
    "decoded": (1479, 132), "nonce": (1611, 13), "aad": (1624, 132),
    "written": (1756, 1), "limits": (1757, 6), "crypto_info": (1763, 6),
    "context": (1769, 38), "info": (1807, 26), "meta": (1833, 19),
}
FIELDS = {
    1: ((0, "block_timeout", 4), (4, "block_polls", 2)),
    2: ((0, "polls", 4), (4, "blocks", 1), (5, "aes_status", 1)),
    3: ((0, "key", 16), (16, "source", 8), (24, "counter", 4), (28, "level", 1),
        (29, "key_identifier", 1), (30, "key_sequence", 1), (31, "extended_nonce", 1), (32, "limits", 6)),
    4: ((0, "counter", 4), (4, "source", 8), (12, "level", 1), (13, "key_identifier", 1),
        (14, "key_sequence", 1), (15, "extended_nonce", 1), (16, "header_length", 1),
        (17, "auxiliary_length", 1), (18, "payload_length", 1)),
    5: ((0, "meta", 19), (19, "crypto", 6), (25, "length", 1)),
}
GUARDS = dict(GUARD_SFRS) | {
    0xbe: 4, 0xc0: 0xbe, 0x98: 0xa4, 0xd7: 0, 0xbf: 0x37,
    0xd9: 0x59, 0xe1: 0x71, 0x94: 0x0d, 0xc3: 0,
    0xd1: 0, 0xd6: 0, 0x95: 0, 0x96: 0, 0x97: 0,
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def load(output):
    path = output / "security_test.ihx"
    return (path, parse_ihex(path.read_text(encoding="ascii")),
            parse_symbols((output / "security_test.map").read_text(encoding="ascii")),
            (output / "security_test.cdb").read_bytes(),
            (output / "security_test.mem").read_bytes(),
            {m: (output / f"security_test.{m}.rst").read_bytes() for m in MODULES},
            {m: (output / f"{m}.rel").read_bytes() for m in MODULES})


def state_location(debug, module):
    match = re.search(rf"^S:F{module}\$state\$0_0\$0\(\{{(\d+)\}}", debug, re.M)
    require(match is not None, "Missing private security state")
    return cdb_address(debug, f"L:F{module}$state$0_0$0"), int(match[1])


def aes_sites(listing):
    code = dict(records(listing))
    accesses = peripheral_accesses(code)
    writes = [(pc, raw, reg) for pc, raw, reg in accesses
              if raw[0] != 0xe5]
    require([reg for _, _, reg in writes] ==
            [0xd6, 0xd6, 0xd5, 0xd4, 0xd3, 0xd2, 0xb3, 0xd1, 0xd1, 0x98, 0x98],
            "AES exact write sites changed")
    require([raw for _, raw, _ in accesses if raw[0] == 0xe5] ==
            [bytes((0xe5, r)) for r in READS] + [b"\xe5\xc6"],
            "AES exact read order changed")
    return dict(zip(("input", "output", "cfg0h", "cfg0l", "cfg1h", "cfg1l",
                     "command", "block_ack", "load_ack", "enc_load", "enc_final"),
                    (pc for pc, _, _ in writes))), code


def verify(image, symbols, debug_raw, memory, listings, objects):
    require(isinstance(debug_raw, bytes) and sha(debug_raw) == CDB_SHA,
            "Security complete raw CDB changed BEFORE decoding")
    require(0 < SIZE <= CODE_BUDGET and sha(code_bytes(image, SIZE)) == CODE_SHA,
            "Security complete executable/constants/runtime changed")
    require(sha(json.dumps(symbols, sort_keys=True, separators=(",", ":")).encode()) == MAP_SHA,
            "Security complete map changed")
    require(sha(memory) == MEM_SHA, "Security complete memory accounting changed")
    debug = debug_raw.decode("ascii")
    allocated = verify_component_layout(image, symbols, debug, memory.decode("ascii"),
                                        "security_result", SOURCES, xdata_budget=XDATA_BUDGET)
    require(symbols["s_XSEG"] == 0 and symbols["l_XSEG"] == XDATA and symbols["s_SSEG"] == STACK,
            "Security allocation/stack changed")
    require(set(listings) == set(objects) == set(MODULES), "Security composition changed")
    require("_aes_reference_encrypt" not in symbols and "C$aes_reference.c$" not in debug and
            "C$security_aes_model.c$" not in debug, "Host cryptography/controller entered target")
    instructions, coverage = {}, set()
    for module in MODULES:
        text = listings[module].decode("ascii")
        digest, metrics = LISTINGS[module]
        require(sha(listings[module]) == digest and listing_metrics(text) == metrics,
                "Security full immediate listing/instructions changed")
        code = dict(records(text))
        if module not in ("aes", "timebase"):
            require(not peripheral_accesses(code), "Protocol/caller gained peripheral instructions")
        for address, raw in code.items():
            span = set(range(address, address + len(raw)))
            require(not coverage & span and all(image.get(address+i) == b for i, b in enumerate(raw)),
                    "Security instructions overlap or differ from linked CODE")
            coverage.update(span)
        instructions.update(code)
        obj = objects[module]
        require(obj.startswith(b";!FILE ") and sha(obj.split(b"\n", 1)[1]) == OBJECTS[module][0],
                "Security complete relocatable object changed")
        areas = {n: int(s, 16) for n, s in re.findall(
            r"^A (\S+) size ([0-9A-F]+) flags \S+ addr \S+$", obj.decode("ascii"), re.M)}
        extent = sum(v for n, v in areas.items()
                     if n in ("HOME", "GSFINAL", "CSEG", "CONST") or n.startswith("GSINIT"))
        require((extent, *(areas.get(n, 0) for n in ("XSEG", "DSEG", "OSEG", "BSEG"))) ==
                OBJECTS[module][1], "Security object accounting changed")
    sites, aes_code = aes_sites(listings["aes"].decode("ascii"))
    for name in ("input", "output"):
        address = sites[name]
        require(bytes(image[address+i] for i in range(13)) ==
                bytes((0x75, 0xd6, 1 if name == "input" else 2)) + bytes(9) + b"\x22",
                "AES independent nine-clock arm path changed")
    for name, module in (("ccm_star_crypt", "ccm_star"), ("zigbee_security_crypt", "zigbee_security"),
                         ("zigbee_security_inspect", "zigbee_security"),
                         ("aes128_encrypt_block", "aes"), ("nwk_frame_decode", "nwk_frame"),
                         ("aps_frame_decode", "aps_frame")):
        require(symbols["_" + name] == cdb_address(debug, f"L:G${name}$0$0") ==
                label(listings[module].decode("ascii"), name), "Security public entry ABI changed")
        require(f"F:G${name}$0_0$0({{2}}DF,SC:U),Z,0,0,0,0,0\n" in debug,
                "Security byte result ABI changed")
    for function, module, targets in (
        ("encrypt", "ccm_star", ("aes128_encrypt_block",)),
        ("zigbee_security_crypt", "zigbee_security", ("ccm_star_crypt",)),
        ("header", "zigbee_security", ("nwk_frame_decode", "aps_frame_decode")),
    ):
        prefix = f"G${function}" if function == "zigbee_security_crypt" else f"F{module}${function}"
        lo = cdb_address(debug, "L:" + prefix + "$0$0")
        hi = cdb_address(debug, "L:X" + prefix + "$0$0")
        calls = {int.from_bytes(b[1:], "big") for a, b in instructions.items()
                 if lo <= a <= hi and len(b) == 3 and b[0] == 0x12}
        require(all(symbols["_" + t] in calls for t in targets), "Security bypasses genuine service/codecs")
    reader = symbols["_timebase_read_awake_ticks24"]
    require(bytes(image[reader+i] for i in range(len(READER_BYTES))) == READER_BYTES,
            "Security actual timebase reader changed")
    verify_deadline_helper(image, symbols, debug)
    for module in ("ccm_star", "zigbee_security"):
        a, n = state_location(debug, module)
        require(symbols["_aes_reserved_end"] < a and a + n <= XDATA and
                set(range(a, a+n)) <= allocated, "Security staging overlaps lower AES ownership")
    for module, lo, hi in zip(MODULES, (0, 25, 207, 521, 617, 686, 1081),
                              (25, 207, 521, 617, 686, 1081, 1875)):
        segment = listings[module].decode("ascii").split(".area XSEG    (XDATA)", 1)[1].split(".area XABS", 1)[0]
        covered = set()
        for a, n in re.findall(r"^\s+([0-9A-F]{6})\s+\d+\s+\.ds (\d+)$", segment, re.M):
            span = set(range(int(a, 16), int(a, 16) + int(n)))
            require(span and not covered & span and span <= set(range(lo, hi)),
                    "Security linked storage overlaps/escapes owned module prefix")
            covered.update(span)
        require(covered == set(range(lo, hi)), "Security private/caller allocation hole")
    require(all(symbols.get(k) == v for k, v in RUNTIME.items()) and
            allocated == set(range(XDATA)) | set(range(0x1e00, 0x1e08)),
            "Security complete libc scratch/status ownership changed")
    for name, (a, n) in CALLER.items():
        prefix = f"Ftest_zigbee_security${name}$0_0$0"
        require(cdb_address(debug, "L:" + prefix) == a and f"S:{prefix}({{{n}}}" in debug,
                "Security caller object ABI changed")
    aes_fields, offset = [], 0
    for name, size in zip(AES_FIELDS, AES_SIZES):
        aes_fields.append((offset, name, size))
        offset += size
    for module in ("aes", "ccm_star", "zigbee_security", "test_zigbee_security"):
        fields = {0: tuple(aes_fields)}
        if module != "aes":
            fields.update({n: v for n, v in FIELDS.items() if n < 3 or module != "ccm_star"})
        for n, expected in fields.items():
            found = re.findall(rf"^T:F{module}\$__{n:08d}\[(.*)\]$", debug, re.M)
            require(len(found) == 1, "Security missing/duplicate public field record")
            actual = re.findall(r"\(\{(\d+)\}S:S\$([^$]+)\$0_0\$0\(\{(\d+)\}", found[0])
            require(tuple((int(a), name, int(size)) for a, name, size in actual) == expected,
                    "Security public field offsets/sizes changed")
    for module, function, params, addresses in (
        ("ccm_star", "ccm_star_crypt",
         (("key", "{3}DG,SC:U"), ("nonce", "{3}DG,SC:U"), ("aad", "{3}DG,SC:U"),
          ("aad_length", "{2}SI:U"), ("input", "{3}DG,SC:U"), ("length", "{2}SI:U"),
          ("tag_length", "{1}SC:U"), ("output", "{3}DG,SC:U"), ("capacity", "{2}SI:U"),
          ("written", "{3}DG,SC:U"), ("limits", "{3}DG,ST__00000001:S"), ("info", "{3}DG,ST__00000002:S")),
         (487, 490, 493, 496, 498, 501, 503, 504, 507, 509, 512, 515)),
        ("zigbee_security", "zigbee_security_crypt",
         (("layer", "{1}SC:U"), ("key", "{3}DG,ST__00000003:S"), ("frame", "{3}DG,SC:U"),
          ("length", "{2}SI:U"), ("output", "{3}DG,SC:U"), ("capacity", "{2}SI:U"),
          ("info", "{3}DG,ST__00000005:S")), (1050, 1051, 1054, 1057, 1059, 1062, 1064)),
        ("zigbee_security", "zigbee_security_inspect",
         (("level", "{1}SC:U"), ("frame", "{3}DG,SC:U"), ("length", "{2}SI:U"),
          ("meta", "{3}DG,ST__00000004:S")), (1040, 1041, 1044, 1046)),
    ):
        for i, ((name, spec), address) in enumerate(zip(params, addresses), 2):
            require(symbols.get(f"_{function}_PARM_{i}") == address and re.search(
                rf"^S:L{module}\.{function}\${name}\$[^(]+\(" + re.escape(spec) + r"\),F,0,0$", debug, re.M),
                "Security generic/scalar parameter ABI changed")
    require((symbols["_main"], symbols["_security_before"], symbols["_security_done"],
             symbols["_security_checks"], symbols["_security_case"]) ==
            (19312, 19357, 19367, 1852, 1856), "Security genuine caller/checkpoints changed")
    return allocated, sites, aes_code


def native_trace(executable, number):
    completed = subprocess.run([str(executable), "--trace", str(number)], check=True,
                               capture_output=True, text=True, timeout=15)
    lines = completed.stdout.splitlines()
    blocks, other = [], {}
    for line in lines:
        parts = line.split()
        require(parts and parts[0] in ("BLOCK", "CHECKS", "OUTPUT", "WIRE", "DECODED"),
                "Unexpected synthetic security trace record")
        if parts[0] == "BLOCK":
            require(len(parts) == 4 and all(re.fullmatch("[0-9a-f]{32}", p) for p in parts[1:]),
                    "Malformed public AES oracle block")
            blocks.append(tuple(bytes.fromhex(p) for p in parts[1:]))
        else:
            require(len(parts) == 2 and parts[0] not in other, "Duplicate trace metadata")
            other[parts[0]] = int(parts[1]) if parts[0] == "CHECKS" else bytes.fromhex(parts[1])
    require(set(other) == {"CHECKS", "OUTPUT", "WIRE", "DECODED"} and
            tuple(len(other[n]) for n in ("OUTPUT", "WIRE", "DECODED")) == (134, 132, 132),
            "Incomplete security reference outputs")
    other["AES"] = len(blocks)
    return blocks, other, sha(completed.stdout.encode("ascii"))


def replay(simulator, path, symbols, debug, sites, code, blocks, expected, number):
    before, done = symbols["_security_before"], symbols["_security_done"]
    initial = GUARDS | {0xb1: 0x69, 0xb2: 0x69, 0xb3: 8, 0xd1: 0,
                        0xd2: 0, 0xd3: 0, 0xd4: 0, 0xd5: 0, 0xd6: 0,
                        0x95: 0, 0x96: 0, 0x97: 0}
    desc0, desc1 = symbols["_aes_dma0"], symbols["_aes_dma1"]
    commands = [ALIAS, AES_ALIAS, "fill xram 0 0x1eff 0xa5", "fill xram 0x6000 0x70ff 0x69",
                f"run 0 {symbols['_main']:#x}", "fill iram 0x7d 0xff 0xc7"]
    commands += [f"set memory sfr {r:#x} {v:#x}" for r, v in initial.items()]
    commands += [f"run {symbols['_main']:#x} {before:#x}",
                 f"set memory xram {symbols['_security_case']:#x} {number}"]
    stops = set(sites.values()) | {sites["input"] + 12, sites["output"] + 12, done}
    commands += [f"break {pc:#x}" for pc in sorted(stops)] + ["step 1"]
    serial, events, dumps, current = 10, [], [], before + 1

    def event(pc, reg=None, value=None):
        nonlocal serial, current
        commands.extend(([] if current == pc else ["run"]) +
                        [marker(serial), "state", "step 1", marker(serial+1)])
        if reg is not None:
            commands.append(f"dump /h sfr {reg:#x} {reg:#x}")
        commands.append(marker(serial+2))
        events.append((serial, pc, reg, value))
        serial += 3
        current = None if code[pc] == b"\x22" else pc + len(code[pc])

    def dump(space, address, wanted):
        nonlocal serial
        commands.extend([marker(serial), f"dump /h {space} {address:#x} {address+len(wanted)-1:#x}",
                         marker(serial+1)])
        dumps.append((serial, address, bytes(wanted)))
        serial += 2

    def write(name, reg, value):
        event(sites[name], reg, value)

    def arm(name, old):
        value = 1 if name == "input" else 2
        write(name, 0xd6, value)
        commands.append(f"set memory sfr 0xd6 {old | value}")
        event(sites[name] + 12)

    for key, plain, cipher in blocks:
        for name, reg, value in (("cfg0h", 0xd5, desc0 >> 8), ("cfg0l", 0xd4, desc0 & 255),
                                 ("cfg1h", 0xd3, desc1 >> 8), ("cfg1l", 0xd2, desc1 & 255)):
            write(name, reg, value)
        arm("output", 0)
        for phase, data in enumerate((key, bytes(16), plain), 1):
            arm("input", 2)
            write("command", 0xb3, (0x45, 0x47, 0x41)[phase-1])
            source = symbols[("_aes_key", "_aes_iv", "_aes_input")[phase-1]]
            dump("xram", desc0, source.to_bytes(2, "big") + bytes.fromhex("70b100101d41"))
            dump("xram", desc1, bytes.fromhex("70b2") + symbols["_aes_output"].to_bytes(2, "big") +
                 bytes.fromhex("00101e11") + bytes(24))
            dump("xram", source, data)
            for i, byte in enumerate(data):
                cfg = "(sfr[0xd5]*256+sfr[0xd4])"
                src = f"(xram[{cfg}]*256+xram[{cfg}+1])"
                dst = f"(xram[{cfg}+2]*256+xram[{cfg}+3])"
                commands.append(f"expression xram[{dst}]=xram[{src}+{i}]")
                dump("sfr", 0xb1, bytes((byte,)))
            if phase == 3:
                for i, byte in enumerate(cipher):
                    commands.append(f"set memory sfr 0xb2 {byte}")
                    cfg = "(sfr[0xd3]*256+sfr[0xd2])"
                    src = f"(xram[{cfg}]*256+xram[{cfg}+1])"
                    dst = f"(xram[{cfg}+2]*256+xram[{cfg}+3])"
                    commands.append(f"expression xram[{dst}+{i}]=xram[{src}]")
                dump("xram", symbols["_aes_output"], cipher)
            commands += [f"set memory sfr 0xd6 {0 if phase == 3 else 2}",
                         f"set memory sfr 0xd1 {3 if phase == 3 else 1}",
                         f"set memory sfr 0xb3 {(0x44, 0x46, 0x48)[phase-1]}",
                         "set memory sfr 0x98 0xa7"]
            write("block_ack" if phase == 3 else "load_ack", 0xd1, 0x1c if phase == 3 else 0x1e)
            commands.append("set memory sfr 0xd1 0")
            write("enc_final" if phase == 3 else "enc_load", 0x98, 0xa4)
    commands += ["run"] + snapshot_commands(serial)
    text = simulate(simulator, commands, path)
    pieces = re.split(r"^0x2530([0-9a-f]{4})\r?\n", text, flags=re.M)
    sections = {int(pieces[i], 16): pieces[i+1] for i in range(1, len(pieces), 2)}
    require(len(sections) == (len(pieces)-1)//2, "Duplicate security replay marker")
    for n, pc, reg, value in events:
        try:
            check_pc(sections[n], pc)
        except (KeyError, ValueError) as error:
            raise ValueError(f"Security case {number}, event {n}: expected PC {pc:04x}") from error
        if reg is not None:
            require(memory_dump(sections[n+1], reg, 1) == bytes((value,)),
                    "Security actual AES/DMA SFR write changed")
    for n, a, wanted in dumps:
        require(memory_dump(sections[n], a, len(wanted)) == wanted,
                "Security actual descriptor/input/DMA byte differs from independent AES trace")
    check_pc(section(text, serial), done)
    ram, iram, sfr = snapshot(text, serial)
    check_result(ram, iram, sfr, symbols, debug, expected)
    peak = check_peak(text, PEAKS[number])
    if number == 0:
        require(snapshot_negatives(ram, iram, sfr, symbols, debug, expected) == 54,
                "Security snapshot negative count changed")
    return peak, expected["CHECKS"], len(blocks)


def check_result(ram, iram, sfr, symbols, debug, expected):
    require(ram[0x1e00:0x1e08] == b"SEC1\x01\x08\0\0",
            "Genuine security C corpus failed or status ABI changed")
    address = symbols["_security_checks"]
    require(int.from_bytes(ram[address:address+4], "little") == expected["CHECKS"],
            "Security target check count differs from independent native run")
    for name in ("output", "wire", "decoded"):
        a = cdb_address(debug, f"L:Ftest_zigbee_security${name}$0_0$0")
        require(ram[a:a+len(expected[name.upper()])] == expected[name.upper()],
                "Security complete caller buffer/canary differs from independent native result")
    for module in ("ccm_star", "zigbee_security"):
        a, n = state_location(debug, module)
        require(ram[a:a+n] == bytes(n), "Security private byte state was not overwritten on return")
    require(iram[0x7d:] == b"\xc7" * 131 and sfr[1] == symbols["s_SSEG"] - 1,
            "Security stack/alias guard or unwind changed")
    require(all(sfr[r-128] == v for r, v in GUARDS.items()),
            "Security changed unowned GPIO/clock/IRQ/radio state")
    used = bool(expected["AES"])
    require(ram[symbols["_aes_used"]] == used and
            ram[symbols["_aes_fault"]] == (0 if used else 9),
            "Security AES completion/fault retention changed")
    final = {0xb3: 0x48 if used else 8}
    for lo, hi, name in ((0xd4, 0xd5, "_aes_dma0"), (0xd2, 0xd3, "_aes_dma1")):
        address = symbols[name] if used else 0
        final[lo], final[hi] = address & 255, address >> 8
    require(all(sfr[r-128] == v for r, v in final.items()), "Security final owned AES/DMA state changed")
    used = set(range(symbols["l_XSEG"])) | set(range(0x1e00, 0x1e08))
    require(all(b == 0xa5 for a, b in enumerate(ram) if a not in used),
            "Security changed unallocated or status-tail XDATA")


def check_peak(text, expected):
    peaks = [int(p, 16) for p in re.findall(r"Max value of stack pointer=\s*0x([0-9a-fA-F]+)", text)]
    require(peaks and max(peaks) == expected <= 0x7c, "Security missing/changed full-run stack peak")
    return max(peaks)


def snapshot_negatives(ram, iram, sfr, symbols, debug, expected):
    count = 0
    for space, address in (
        *(("ram", a) for a in (0x1e00, 0x1e06, 0x1e08, 0x1eff, symbols["l_XSEG"],
                               symbols["_security_checks"], CALLER["output"][0],
                               CALLER["wire"][0], CALLER["decoded"][0],
                               symbols["_aes_used"], symbols["_aes_fault"],
                               state_location(debug, "ccm_star")[0],
                               state_location(debug, "zigbee_security")[0])),
        ("iram", 0x7d), ("iram", 0xff), ("sfr", 1),
        *(("sfr", a-128) for a in (0xb3, 0xd2, 0xd3, 0xd4, 0xd5)),
        *(("sfr", a-128) for a in GUARDS),
    ):
        values = dict(ram=ram, iram=iram, sfr=sfr)
        changed = bytearray(values[space])
        changed[address] ^= 1
        values[space] = bytes(changed)
        rejected(lambda: check_result(**values, symbols=symbols, debug=debug, expected=expected))
        count += 1
    return count


def negatives(image, symbols, debug_raw, memory, listings, objects):
    count = 0

    def reject(**changes):
        nonlocal count
        args = dict(image=image, symbols=symbols, debug_raw=debug_raw, memory=memory,
                    listings=listings, objects=objects)
        rejected(lambda: verify(**(args | changes)))
        count += 1

    for a in image:
        reject(image=image | {a: image[a] ^ 1})
    reject(image=image | {SIZE: 0})
    for name in symbols:
        reject(symbols=symbols | {name: symbols[name] ^ 1})
    reject(symbols=symbols | {"unreviewed": 0})
    for line in debug_raw.splitlines(keepends=True):
        if line.startswith((b"S:", b"L:", b"T:", b"F:")):
            reject(debug_raw=debug_raw.replace(line, b"", 1))
            reject(debug_raw=debug_raw + line)
            reject(debug_raw=debug_raw.replace(line, line[:-1] + b"!\n", 1))
    for raw in (debug_raw + b"\xff", debug_raw.replace(b"\n", b"\r\n"),
                debug_raw.replace(b"\n", b"\r")):
        reject(debug_raw=raw)
    reject(memory=memory + b"\n")
    for m in MODULES:
        reject(listings={n: v for n, v in listings.items() if n != m})
        reject(listings=listings | {m: listings[m].replace(b".ds ", b".lost ", 1)})
        lines = listings[m].splitlines(keepends=True)
        first, second = [i for i, line in enumerate(lines) if records(line.decode("ascii"))][:2]
        for op in ("drop", "duplicate", "swap"):
            changed = lines.copy()
            if op == "drop": del changed[first]
            elif op == "duplicate": changed.insert(first, changed[first])
            else: changed[first], changed[second] = changed[second], changed[first]
            reject(listings=listings | {m: b"".join(changed)})
        reject(objects=objects | {m: objects[m].replace(b"A XSEG size ", b"A XSEG lost ", 1)})
        reject(objects=objects | {m: objects[m] + b"A UNKNOWN size 0 flags 0 addr 0\n"})
    return count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--simulator", default="s51")
    args = parser.parse_args()
    path, image, symbols, debug_raw, memory, listings, objects = load(args.output)
    _, sites, code = verify(image, symbols, debug_raw, memory, listings, objects)
    bad = negatives(image, symbols, debug_raw, memory, listings, objects)
    require(bad == 61685, "Security artifact-negative coverage changed")
    check_alias(args.simulator)
    rejected(lambda: check_alias(args.simulator, False))
    for text in ("", "Max value of stack pointer= 0x7d", "Max value of stack pointer= 0x78"):
        rejected(lambda: check_peak(text, 0x79))
    total, blocks = 0, 0
    for case in range(23):
        trace, expected, digest = native_trace(args.output / "host-zigbee-security-tests", case)
        require(digest == TRACES[case], "Security complete synthetic trace identity changed")
        peak, checks, calls = replay(args.simulator, path, symbols, debug_raw.decode("ascii"),
                                    sites, code, trace, expected, case)
        require(peak == PEAKS[case], "Security exact per-case stack peak changed")
        total += checks
        blocks += calls
    require((total, blocks) == (220, 345), "Security target/AES coverage changed")
    print(f"Zigbee security: {total} genuine target checks, {blocks} real AES calls; "
          f"{SIZE}/{CODE_BUDGET} CODE, {XDATA}+64/{XDATA_BUDGET} XDATA; "
          f"SP={max(PEAKS):02X}/7C; {bad} artifact + 54 snapshot + 3 peak + 1 alias negatives PASS. Not membership.")


if __name__ == "__main__":
    main()
