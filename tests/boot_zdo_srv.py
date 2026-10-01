#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Verify genuine ED ZDO dispatch composed with NWK/APS; no transport model."""
import argparse
import json
from pathlib import Path
import re

from boot_image import (
    ALIAS, check_alias, check_pc, section, simulate, snapshot, snapshot_commands,
    verify_component_layout,
)
from boot_mac_epoch import GUARDS, sha
from boot_nwk_candidates import label, listing_metrics, records
from boot_zcl_basic import private_span
from boot_zdo_node import FIELDS as NODE_FIELDS, OBJECTS as NODE_OBJECTS, rejected
from verify_firmware import cdb_address, code_bytes, parse_ihex, parse_symbols, peripheral_accesses, require

MODULES = ("zdo_srv", "zdo_node", "aps_frame", "nwk_frame", "zdo_srv_test")
SIZE, XDATA, CHECKS, PEAK = 18578, 843, 1818, 0x65
CODE_BUDGET, XDATA_BUDGET = 24576, 1024
CODE_SHA = "10b925d964fa4e5a8c1c9c88cab41efd26fc56e51c72885ac969e4fbaf13aa07"
CDB_SHA = "3a1f364ae65dfe1ef3b29dd7e8f5f034ccfed987d058b13e4fd52a70591feb2f"
MAP_SHA = "ea20c07db3f03ada7dfc9eeeb7939d3747765115fc5f2ae41a21d166a5130ac3"
MEM_SHA = "2e77fdd6859e9cfd3bf097d7c49c5d97eaf5df5e41ed8c1a2e580aa0e296f9a5"
LIST_SHA = dict(zip(MODULES, (
    "efb5e0f5ffb41438a8d10adf5156906fef17744b86f5c08d1e045aa9dec0af22",
    "f4158f572c21a9388ba0c36e781157dca4b6a264d9405a5949a69ef642633980",
    "707fcdac0cd0c35a930757f272d22b4a7012fe76cc0c4832530c3e465b630d13",
    "754c95cb483f34c28110747240105dd8b61964fb644f0a03f3f519e4be4766d4",
    "78cf3d4ca4a37c18bfa8cc17424dc5823326500b11c50e14aabe414ad2e5d877",
)))
METRICS = dict(zip(MODULES, (
    (1543, 2416, "70544f186ea647e16100b7fafa8bb47ac06c903d47d492526ce8a057228c1309"),
    (1758, 3133, "c1b3b48e51f1813999995e82ae9d7e07498d572c2b2ccf4e205fcad17c05f355"),
    (939, 1626, "91779b4cade774e2cd80fbaa0c8af6355ad12e88108d39a84a93aaf97ebfc3c0"),
    (1344, 2294, "595f0fde6a76f16708e31afa1c47adf8b23eef38e490d00d9defcf4354a7e1b4"),
    (4943, 8445, "2f8a197d014357ea28b4a8b8f29e72a98c1b18f772583770c85690667909a8f6"),
)))
# Total CODE including constants/startup contributions, XSEG, DSEG, OSEG, BSEG.
OBJECTS = {
    "zdo_srv": ((2416, 69, 0, 0, 0), "efd37f56e7bcc3debea5f436d28c3bf7c1169e624a858966cd92250ede609973"),
    "zdo_node": NODE_OBJECTS["zdo_node"],
    "aps_frame": NODE_OBJECTS["aps_frame"],
    "nwk_frame": ((2294, 96, 12, 10, 0), "f1d39e71ec25fe27d8f4e269a09810541fcfc830705ea37c640fcd4611d4341a"),
    "zdo_srv_test": ((8582, 503, 4, 0, 1), "8ba089a411d04960f05990dee86791c993e58e7c3892849d52da20f2ef9fc333"),
}
PUBLIC = {
    "zdo_srv_handle": ("zdo_srv", 98),
    "zdo_node_req_decode": ("zdo_node", 4145), "zdo_node_req_encode": ("zdo_node", 4381),
    "zdo_node_rsp_decode": ("zdo_node", 4576), "zdo_node_rsp_encode": ("zdo_node", 5170),
    "aps_frame_decode": ("aps_frame", 5939), "aps_frame_encode": ("aps_frame", 6972),
    "nwk_frame_decode": ("nwk_frame", 7799), "nwk_frame_encode": ("nwk_frame", 9228),
}
CALLER = {
    "local": (324, 19), "rx": (343, 11), "info": (354, 8), "decoded": (362, 20),
    "input": (382, 101), "output": (483, 19), "nwk": (502, 27), "network": (529, 29),
    "aps": (558, 10), "transport": (568, 12), "apdu": (580, 108), "npdu": (688, 116),
    "aps_length": (804, 1), "nwk_length": (805, 1),
}
RUNTIME = {
    "___memcpy_PARM_2": 823, "___memcpy_PARM_3": 826, "_memset_PARM_2": 831,
    "_memset_PARM_3": 832, "__gptrput_PARM_2": 834, "_memcmp_PARM_2": 835, "_memcmp_PARM_3": 838,
}
FIELDS = {
    0: ((0, "type", 1), (1, "delivery_mode", 1), (2, "flags", 1), (3, "destination_endpoint", 1),
        (4, "cluster_id", 2), (6, "profile_id", 2), (8, "source_endpoint", 1), (9, "counter", 1)),
    1: ((0, "header", 10), (10, "payload_offset", 1), (11, "payload_length", 1)),
    2: NODE_FIELDS[0], 3: NODE_FIELDS[1], 4: NODE_FIELDS[2],
    5: ((0, "descriptor", 14), (14, "address", 2), (16, "profile", 2), (18, "endpoint", 1)),
    6: ((0, "header", 10), (10, "broadcast", 1)),
    7: ((0, "cluster_id", 2), (2, "kind", 1), (3, "endpoint", 1), (4, "sequence", 1),
        (5, "status", 1), (6, "length", 1), (7, "consumed", 1)),
}


def verify(image, symbols, debug_raw, memory, listings, objects):
    require(isinstance(debug_raw, bytes) and sha(debug_raw) == CDB_SHA,
            "ZDO dispatch full raw CDB changed (checked BEFORE decode)")
    debug = debug_raw.decode("ascii")
    require(SIZE <= CODE_BUDGET and sha(code_bytes(image, SIZE)) == CODE_SHA,
            "ZDO dispatch full CODE/runtime/constants changed")
    require(sha(json.dumps(symbols, sort_keys=True, separators=(",", ":")).encode()) == MAP_SHA,
            "ZDO dispatch complete map changed")
    require(sha(memory) == MEM_SHA, "ZDO dispatch complete memory accounting changed")
    allocated = verify_component_layout(
        image, symbols, debug, memory.decode("ascii"), "zdo_srv_result",
        ("zdo_srv.c", "zdo_node.c", "aps_frame.c", "nwk_frame.c", "test_zdo_srv.c"),
        xdata_budget=XDATA_BUDGET,
    )
    require(symbols["s_SSEG"] == 0x4b and symbols["s_XSEG"] == 0 and symbols["l_XSEG"] == XDATA,
            "ZDO dispatch storage/stack changed")
    require(set(listings) == set(objects) == set(MODULES), "ZDO dispatch composition changed")
    instructions, covered = {}, set()
    for module in MODULES:
        require(sha(listings[module]) == LIST_SHA[module], "ZDO dispatch complete immediate listing changed")
        text = listings[module].decode("ascii")
        require(listing_metrics(text) == METRICS[module], "ZDO dispatch ordered instructions changed")
        code = records(text)
        require(not peripheral_accesses(dict(code)) and not any(
            raw[0] == 0x90 and 0x6000 <= int.from_bytes(raw[1:], "big") < 0x6400
            for _, raw in code), "ZDO dispatch gained peripheral instructions")
        for address, raw in code:
            span = set(range(address, address + len(raw)))
            require(not covered & span and all(image.get(address+i) == v for i, v in enumerate(raw)),
                    "ZDO dispatch overlapping/non-linked instructions")
            covered.update(span)
            instructions[address] = raw
        obj = objects[module]
        require(obj.startswith(b";!FILE ") and sha(obj.split(b"\n", 1)[1]) == OBJECTS[module][1],
                "ZDO dispatch complete relocatable object changed")
        areas = {n: int(s, 16) for n, s in re.findall(
            r"^A (\S+) size ([0-9A-F]+) flags \S+ addr \S+$", obj.decode("ascii"), re.M)}
        extent = sum(s for n, s in areas.items()
                     if n in ("HOME", "GSFINAL", "CSEG", "CONST") or n.startswith("GSINIT"))
        require((extent, *(areas.get(n, 0) for n in ("XSEG", "DSEG", "OSEG", "BSEG"))) ==
                OBJECTS[module][0], "ZDO dispatch object accounting changed")
    for module in ("zdo_srv", "test_zdo_srv"):
        for i, expected in FIELDS.items():
            found = re.findall(rf"^T:F{module}\$__{i:08d}\[(.*)\]$", debug, re.M)
            require(len(found) == 1, "ZDO dispatch missing/duplicate field record")
            actual = re.findall(r"\(\{(\d+)\}S:S\$([^$]+)\$0_0\$0\(\{(\d+)\}", found[0])
            require(tuple((int(o), n, int(s)) for o, n, s in actual) == expected,
                    "ZDO dispatch field ABI changed")
    for number, address in enumerate((0, 3, 6, 8, 11, 13), 2):
        require(symbols.get(f"_zdo_srv_handle_PARM_{number}") == address,
                "ZDO dispatch parameter map ABI changed")
    for name, spec in (
        ("local", "{3}DG,ST__00000005:S"), ("rx", "{3}DG,ST__00000006:S"),
        ("body", "{3}DG,SC:U"), ("length", "{2}SI:U"),
        ("response", "{3}DG,SC:U"), ("capacity", "{2}SI:U"), ("info", "{3}DG,ST__00000007:S"),
        ("reply", "{20}ST__00000004:S"), ("request", "{4}ST__00000003:S"),
        ("candidate", "{8}ST__00000007:S"), ("staged", "{17}DA17d,SC:U"), ("simple", "{1}SC:U"),
    ):
        require(re.search(r"^S:Lzdo_srv.zdo_srv_handle\$" + name + r"\$[^(]+\(" + re.escape(spec)
                          + r"\),F,0,0$", debug, re.M), "ZDO dispatch argument/staging ABI changed")
    for module, lo, hi in (("zdo_srv", 0, 69), ("zdo_node", 69, 155),
                           ("aps_frame", 155, 224), ("nwk_frame", 224, 320),
                           ("test_zdo_srv", 806, 823)):
        require(private_span(debug, module) == set(range(lo, hi)),
                "ZDO dispatch private storage boundary changed")
    caller = set(range(320, 324))
    require(symbols["_zdo_srv_checks"] == 320 and
            "S:G$zdo_srv_checks$0_0$0({4}SL:U),F,0,0\n" in debug, "ZDO dispatch check counter ABI changed")
    for name, (address, size) in CALLER.items():
        prefix = f"Ftest_zdo_srv${name}$0_0$0"
        require(cdb_address(debug, "L:" + prefix) == address and f"S:{prefix}({{{size}}}" in debug,
                "ZDO dispatch caller storage changed")
        span = set(range(address, address + size))
        require(not caller & span and span <= allocated, "ZDO dispatch caller overlap")
        caller.update(span)
    require(caller == set(range(320, 806)) and all(symbols.get(k) == v for k, v in RUNTIME.items())
            and allocated == set(range(XDATA)) | set(range(0x1e00, 0x1e08)),
            "ZDO dispatch caller/runtime/status ownership changed")
    for name, (module, address) in PUBLIC.items():
        require(symbols["_" + name] == cdb_address(debug, f"L:G${name}$0$0") ==
                label(listings[module].decode("ascii"), name) == address and address in instructions,
                "ZDO dispatch/composed public entry changed")
        require(f"F:G${name}$0_0$0({{2}}DF,SC:U),Z,0,0,0,0,0\n" in debug,
                "ZDO dispatch/composed result ABI changed")
    for prefix, targets in (
        ("G$zdo_srv_handle", ("zdo_node_req_decode", "zdo_node_rsp_encode")),
        ("Ftest_zdo_srv$chain_cases", ("zdo_srv_handle", "zdo_node_rsp_decode",
                                     "aps_frame_decode", "aps_frame_encode", "nwk_frame_decode", "nwk_frame_encode")),
    ):
        lo, hi = (cdb_address(debug, "L:" + p + "$0$0") for p in (prefix, "X" + prefix))
        calls = {int.from_bytes(raw[1:], "big") for a, raw in instructions.items()
                 if lo <= a <= hi and len(raw) == 3 and raw[0] == 0x12}
        require(all(PUBLIC[t][1] in calls for t in targets), "ZDO dispatch bypasses genuine composition")
    require(symbols["_main"] == 17966 and symbols["_zdo_srv_done"] == 18039
            and instructions.get(18039) == b"\0"
            and code_bytes(image, SIZE)[18039:18042] == b"\0\x80\xfe", "ZDO dispatch checkpoint changed")
    return allocated


def negatives(image, symbols, debug_raw, memory, listings, objects):
    count = 0

    def reject(**changes):
        nonlocal count
        args = dict(image=image, symbols=symbols, debug_raw=debug_raw, memory=memory,
                    listings=listings, objects=objects)
        rejected(lambda: verify(**(args | changes)))
        count += 1

    for address in image:
        reject(image=image | {address: image[address] ^ 1})
    reject(image=image | {SIZE: 0})
    reject(image={a: v for a, v in image.items() if a != SIZE - 1})
    for name in symbols:
        reject(symbols=symbols | {name: symbols[name] ^ 1})
    reject(symbols=symbols | {"unexpected_symbol": 0})
    for line in debug_raw.splitlines(keepends=True):
        if line.startswith((b"F:", b"S:", b"L:", b"T:")):
            reject(debug_raw=debug_raw.replace(line, b"", 1))
            reject(debug_raw=debug_raw + line)
            reject(debug_raw=debug_raw.replace(line, line[:-1] + b"!\n", 1))
    for raw in (debug_raw.replace(b"\n", b"\r\n"), debug_raw.replace(b"\n", b"\r"),
                debug_raw + b"\n", debug_raw + b"\xff"):
        reject(debug_raw=raw)
    reject(memory=memory.replace(b"181 bytes available", b"180 bytes available"))
    reject(memory=memory.replace(b"843", b"844"))
    reject(listings={m: listings[m] for m in MODULES[:-1]})
    for module in MODULES:
        lines = listings[module].splitlines(keepends=True)
        first, second = [i for i, line in enumerate(lines) if records(line.decode("ascii"))][:2]
        for operation in ("drop", "duplicate", "reorder"):
            changed = lines.copy()
            if operation == "drop":
                del changed[first]
            elif operation == "duplicate":
                changed.insert(first, changed[first])
            else:
                changed[first], changed[second] = changed[second], changed[first]
            reject(listings=listings | {module: b"".join(changed)})
        reject(listings=listings | {module: listings[module].replace(b".ds ", b".lost ", 1)})
        reject(objects=objects | {module: objects[module].replace(b"A XSEG size ", b"A XSEG lost ", 1)})
        reject(objects=objects | {module: objects[module] + b"A UNREVIEWED size 0 flags 0 addr 0\n"})
    return count


def check_result(ram, iram, sfr, allocated, symbols):
    require(ram[0x1e00:0x1e06] == b"ZDS1\x01\x08", "ZDO dispatch status ABI changed")
    failure = int.from_bytes(ram[0x1e06:0x1e08], "little")
    require(failure == 0, f"Genuine ZDO dispatch corpus failed at C line {failure}")
    a = symbols["_zdo_srv_checks"]
    require(int.from_bytes(ram[a:a+4], "little") == CHECKS, "ZDO dispatch check count changed")
    require(all(v == 0xa5 for a, v in enumerate(ram) if a not in allocated),
            "ZDO dispatch unused/status-tail XDATA write")
    require(iram[0x7d:] == b"\xc7" * 131 and sfr[1] == 0x4a, "ZDO dispatch stack/alias/unwind changed")
    require(all(sfr[a-128] == v for a, v in GUARDS.items()), "ZDO dispatch changed GPIO/clock/timer/RF")


def check_peak(text):
    peaks = re.findall(r"Max value of stack pointer=\s*0x([0-9a-fA-F]+)", text)
    require(len(peaks) == 1 and int(peaks[0], 16) == PEAK <= 0x7c, "ZDO dispatch full-run stack changed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--simulator", default="s51")
    args = parser.parse_args()
    path = args.output / "zdo_srv_test.ihx"
    image = parse_ihex(path.read_text())
    symbols = parse_symbols(path.with_suffix(".map").read_text())
    debug_raw = path.with_suffix(".cdb").read_bytes()
    memory = path.with_suffix(".mem").read_bytes()
    listings = {m: (args.output / f"zdo_srv_test.{m}.rst").read_bytes() for m in MODULES}
    objects = {m: (args.output / f"{m}.rel").read_bytes() for m in MODULES}
    allocated = verify(image, symbols, debug_raw, memory, listings, objects)
    count = negatives(image, symbols, debug_raw, memory, listings, objects)
    require(count == 54409, "ZDO dispatch artifact rejection inventory changed")
    check_alias(args.simulator)
    rejected(lambda: check_alias(args.simulator, False))
    commands = [ALIAS, "fill xram 0 0x1eff 0xa5", f"run 0 {symbols['_main']:#x}",
                "fill iram 0x7d 0xff 0xc7"]
    commands += [f"set memory sfr {a:#x} {v:#x}" for a, v in GUARDS.items()]
    stop = symbols["_zdo_srv_done"]
    text = simulate(args.simulator, commands + [f"run {symbols['_main']:#x} {stop:#x}"] +
                    snapshot_commands(1), path)
    check_pc(section(text, 1), stop)
    state = snapshot(text, 1)
    check_result(*state, allocated, symbols)
    check_peak(section(text, 1))
    guards = ((0, 0x1e00), (0, 0x1e06), (0, symbols["_zdo_srv_checks"]), (0, XDATA),
              (0, 0x1dff), (0, 0x1e08), (1, 0xff), (1, 0x7d), (2, 1))
    guards += tuple((2, a - 128) for a in GUARDS)
    for region, address in guards:
        changed = [bytearray(s) for s in state]
        changed[region][address] ^= 1
        rejected(lambda: check_result(*changed, allocated, symbols))
    for bad in ("", "Max value of stack pointer=0x66",
                "Max value of stack pointer=0x65\nMax value of stack pointer=0x65"):
        rejected(lambda: check_peak(bad))
    print(f"ZDO ED dispatch: {CHECKS} genuine target checks; {SIZE}/{CODE_BUDGET} CODE, "
          f"{XDATA}+64/{XDATA_BUDGET} XDATA; full-run SP={PEAK:02X}/7C; "
          f"{count} artifact + {len(guards)} snapshot + 3 peak + 1 alias negatives PASS. "
          "Offline dispatch/composition, NOT admission or authenticated join.")


if __name__ == "__main__":
    main()
