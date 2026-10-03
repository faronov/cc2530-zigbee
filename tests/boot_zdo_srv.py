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
# Five-cluster descriptor: staged[23], guarded caller output[25], and
# four additional undersized-capacity cases (16 shared assertions).
SIZE, XDATA, CHECKS, PEAK = 18673, 855, 1834, 0x65
CODE_BUDGET, XDATA_BUDGET = 24576, 1024
CODE_SHA = "a6b971fc3bae8c324ef87940587396c32d7bb0d2a7c8a048265dedb977c8bd17"
CDB_SHA = "ed0e6bf083ded8a5051230e77574a6cdcbf9292bad08ac76ffb5eda4315603f9"
MAP_SHA = "f627a80e872f5895c6b723158b3037309f34cd125666c27a3f9439bdc6670bad"
MEM_SHA = "ab1c5f05b68b6b208730531219cd6a3c4134fd2c05e2c0ee9862640f26831d96"
LIST_SHA = dict(zip(MODULES, (
    "7cb6a7e8ec4e6709e033940412b3dee0dddaa580d646ffd403c4cd151460a468",
    "ce904c1be17c4da7c3e44436e7cf3a5cb0b1632dba21bd7aefa3f351adfd0744",
    "7fd2114cde8bf16b361804133f9b1e2068ac97680272f047257e5b6e063c8dff",
    "70512f99044d926f1448ca0e39b28eb47cc412d578d5c452f998d805aa3783d6",
    "499572a8274c8717cc2ac668ce3e3427767081e91049b5c8f1eb67377c6c82cd",
)))
METRICS = dict(zip(MODULES, (
    (1568, 2460, "b71cbfe7e8c64a12ac90ccbec4ab47435cd700814e9bcc46c9c88b4a3ccfc91a"),
    (1758, 3133, "66c6035b8986da826d56a6c8812bc19cd383d8e6406df02a7ab7d4f30a4e857d"),
    (939, 1626, "2db3bf8b3f0c68d830a69d99a749a75800ae50978c566b1489129b5980d5a26e"),
    (1344, 2294, "4979ef8e0248725480c001014819d9799e5665d9160bdf94e6f6fb9887b4b5cc"),
    (4965, 8488, "897b224ee13baada0a690e31ef16059519e0866af1168d8e48cf24ba36d33415"),
)))
# Total CODE including constants/startup contributions, XSEG, DSEG, OSEG, BSEG.
OBJECTS = {
    "zdo_srv": ((2460, 75, 0, 0, 0), "03e92ef7276dc018e7ab38ba6184659134aa709e0b7b750053afcc05b6cd5e9f"),
    "zdo_node": NODE_OBJECTS["zdo_node"],
    "aps_frame": NODE_OBJECTS["aps_frame"],
    "nwk_frame": ((2294, 96, 12, 10, 0), "f1d39e71ec25fe27d8f4e269a09810541fcfc830705ea37c640fcd4611d4341a"),
    "zdo_srv_test": ((8633, 509, 4, 0, 1), "853719e9222b114f7606bc09331f6a46a5c6d6d8c0565d36d9cc69c19aac4615"),
}
PUBLIC = {
    "zdo_srv_handle": ("zdo_srv", 98),
    "zdo_node_req_decode": ("zdo_node", 4189), "zdo_node_req_encode": ("zdo_node", 4425),
    "zdo_node_rsp_decode": ("zdo_node", 4620), "zdo_node_rsp_encode": ("zdo_node", 5214),
    "aps_frame_decode": ("aps_frame", 5983), "aps_frame_encode": ("aps_frame", 7016),
    "nwk_frame_decode": ("nwk_frame", 7843), "nwk_frame_encode": ("nwk_frame", 9272),
}
CALLER = {
    "local": (330, 19), "rx": (349, 11), "info": (360, 8), "decoded": (368, 20),
    "input": (388, 101), "output": (489, 25), "nwk": (514, 27), "network": (541, 29),
    "aps": (570, 10), "transport": (580, 12), "apdu": (592, 108), "npdu": (700, 116),
    "aps_length": (816, 1), "nwk_length": (817, 1),
}
RUNTIME = {
    "___memcpy_PARM_2": 835, "___memcpy_PARM_3": 838, "_memset_PARM_2": 843,
    "_memset_PARM_3": 844, "__gptrput_PARM_2": 846, "_memcmp_PARM_2": 847, "_memcmp_PARM_3": 850,
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
        ("candidate", "{8}ST__00000007:S"), ("staged", "{23}DA23d,SC:U"), ("simple", "{1}SC:U"),
    ):
        require(re.search(r"^S:Lzdo_srv.zdo_srv_handle\$" + name + r"\$[^(]+\(" + re.escape(spec)
                          + r"\),F,0,0$", debug, re.M), "ZDO dispatch argument/staging ABI changed")
    for module, lo, hi in (("zdo_srv", 0, 75), ("zdo_node", 75, 161),
                           ("aps_frame", 161, 230), ("nwk_frame", 230, 326),
                           ("test_zdo_srv", 818, 835)):
        require(private_span(debug, module) == set(range(lo, hi)),
                "ZDO dispatch private storage boundary changed")
    caller = set(range(326, 330))
    require(symbols["_zdo_srv_checks"] == 326 and
            "S:G$zdo_srv_checks$0_0$0({4}SL:U),F,0,0\n" in debug, "ZDO dispatch check counter ABI changed")
    for name, (address, size) in CALLER.items():
        prefix = f"Ftest_zdo_srv${name}$0_0$0"
        require(cdb_address(debug, "L:" + prefix) == address and f"S:{prefix}({{{size}}}" in debug,
                "ZDO dispatch caller storage changed")
        span = set(range(address, address + size))
        require(not caller & span and span <= allocated, "ZDO dispatch caller overlap")
        caller.update(span)
    require(caller == set(range(326, 818)) and all(symbols.get(k) == v for k, v in RUNTIME.items())
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
    require(symbols["_main"] == 18053 and symbols["_zdo_srv_done"] == 18126
            and instructions.get(18126) == b"\0"
            and code_bytes(image, SIZE)[18126:18129] == b"\0\x80\xfe", "ZDO dispatch checkpoint changed")
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
    reject(memory=memory.replace(str(XDATA).encode(), str(XDATA + 1).encode()))
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
    require(count == 54675, "ZDO dispatch artifact rejection inventory changed")
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
