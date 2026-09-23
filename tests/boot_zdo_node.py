#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Check and execute the actual ZDO Node Descriptor/APS composition; no RF."""
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
from verify_firmware import cdb_address, code_bytes, parse_ihex, parse_symbols, peripheral_accesses, require

MODULES = ("zdo_node", "aps_frame", "zdo_node_test")
SIZE, XDATA, CHECKS, PEAK = 13639, 574, 2559, 0x5e
CODE_BUDGET, XDATA_BUDGET = 16384, 768
CODE_SHA = "a8b2cacd2d011de1aef451dae3f3ab1e2535a6aef6fae5ad3e4faac5db1ebc15"
CDB_SHA = "bcf31186669e2b1909e88d13edde064a030e1c910406b24840afc80f844c743c"
MAP_SHA = "9b8e1b6734e04b115af45f0950f8e8ad2c19fcc499267d516e0522a422efe2be"
LISTINGS = {
    "zdo_node": (1758, 3133, "dc0432ca24829f497b9e3dfd39c123c8a129cf73f9af3880a3c4b4564ea0432f"),
    "aps_frame": (939, 1626, "d071b0a050e02889cf7324b10a6c5cff1c50f3e65cb466acd54bfc67898e2b22"),
    "zdo_node_test": (4814, 8310, "bc1e471079cb0ff55deabc5cda18aeaa34f30a34e2f3acb93bfebbc9b901f1e2"),
}
LIST_SHA = {
    "zdo_node": "04baae6180778d21730630f2b1685c271793394ad4cf564f0a30a35f58c682c0",
    "aps_frame": "dfe2aa17e924b014a53adfbc38b89e1d5336733a79fc1cb00a286eef58329d97",
    "zdo_node_test": "30eec909c7fa9aa9f0b675db893babb7f41940ee74e3b5096d6dfc410deee7cd",
}
# Total CODE, XSEG, DSEG, OSEG, BSEG; complete object except its build-path line.
OBJECTS = {
    "zdo_node": ((3133, 86, 32, 0, 1), "c1541ff1ae550ed859ae9100dc8f6d883327a8639424d3b8c7938e8ffd5e370a"),
    "aps_frame": ((1626, 69, 8, 7, 0), "98103aafd255517b26d841dc2322f0c3732ad696f93c3af08767e0ac8fb639b2"),
    "zdo_node_test": ((8353, 399, 6, 0, 3), "9079a302304582db7ccf4a23e2263998e7b0b7a9aa74d8df458d743e8ef14aaa"),
}
PUBLIC = {
    "zdo_node_req_decode": ("zdo_node", 1729),
    "zdo_node_req_encode": ("zdo_node", 1965),
    "zdo_node_rsp_decode": ("zdo_node", 2160),
    "zdo_node_rsp_encode": ("zdo_node", 2754),
    "aps_frame_decode": ("aps_frame", 3523), "aps_frame_encode": ("aps_frame", 4556),
}
PARAMS = {
    "_zdo_node_req_decode_PARM_2": 24, "_zdo_node_req_decode_PARM_3": 26,
    "_zdo_node_req_encode_PARM_2": 36, "_zdo_node_req_encode_PARM_3": 39,
    "_zdo_node_req_encode_PARM_4": 41,
    "_zdo_node_rsp_decode_PARM_2": 47, "_zdo_node_rsp_decode_PARM_3": 49,
    "_zdo_node_rsp_encode_PARM_2": 75, "_zdo_node_rsp_encode_PARM_3": 78,
    "_zdo_node_rsp_encode_PARM_4": 80,
}
CALLER = {
    "request": (159, 4), "old_request": (163, 4), "response": (167, 20), "old_response": (187, 20),
    "input": (207, 101), "output": (308, 102), "length": (410, 1),
    "header": (411, 10), "transport": (421, 12), "apdu": (433, 108), "aps_length": (541, 1),
}
RUNTIME = {
    "___memcpy_PARM_2": 554, "___memcpy_PARM_3": 557, "_memset_PARM_2": 562,
    "_memset_PARM_3": 563, "__gptrput_PARM_2": 565, "_memcmp_PARM_2": 566, "_memcmp_PARM_3": 569,
}
FIELDS = (
    ((0, "manufacturer", 2), (2, "max_incoming", 2), (4, "max_outgoing", 2),
     (6, "logical_type", 1), (7, "available", 1), (8, "frequency_band", 1),
     (9, "mac_capability", 1), (10, "max_buffer", 1), (11, "server_flags", 1),
     (12, "stack_revision", 1), (13, "descriptor_capability", 1)),
    ((0, "address", 2), (2, "sequence", 1), (3, "consumed", 1)),
    ((0, "descriptor", 14), (14, "address", 2), (16, "sequence", 1),
     (17, "status", 1), (18, "has_address", 1), (19, "consumed", 1)),
)


def verify(image, symbols, debug_raw, memory, listings, objects):
    require(isinstance(debug_raw, bytes) and sha(debug_raw) == CDB_SHA,
            "ZDO complete raw CDB changed (checked BEFORE decode)")
    debug = debug_raw.decode("ascii")
    require(SIZE <= CODE_BUDGET and sha(code_bytes(image, SIZE)) == CODE_SHA,
            "ZDO complete CODE/runtime/constants changed")
    require(sha(json.dumps(symbols, sort_keys=True, separators=(",", ":")).encode()) == MAP_SHA,
            "ZDO complete parsed map changed")
    allocated = verify_component_layout(
        image, symbols, debug, memory, "zdo_node_result",
        ("zdo_node.c", "aps_frame.c", "test_zdo_node.c"), xdata_budget=XDATA_BUDGET,
    )
    require(symbols["s_XSEG"] == 0 and symbols["l_XSEG"] == XDATA and symbols["s_SSEG"] == 0x41,
            "ZDO storage/stack changed")
    require(re.findall(r"EXTERNAL RAM\s+(\S+)\s+(\S+)\s+(\d+)\s+(\d+)", memory) ==
            [("0x0000", "0x023d", "574", "7680")], "ZDO XDATA memory accounting changed")
    require(set(listings) == set(objects) == set(MODULES), "ZDO composition changed")
    instructions, covered = {}, set()
    for module in MODULES:
        require(sha(listings[module]) == LIST_SHA[module], "ZDO complete immediate listing changed")
        text = listings[module].decode("ascii")
        require(listing_metrics(text) == LISTINGS[module], "ZDO ordered instructions changed")
        code = records(text)
        require(not peripheral_accesses(dict(code)) and not any(
            raw[0] == 0x90 and 0x6000 <= int.from_bytes(raw[1:], "big") < 0x6400
            for _, raw in code), "ZDO gained peripheral instructions")
        for address, raw in code:
            span = set(range(address, address + len(raw)))
            require(not covered & span and all(image.get(address+i) == v for i, v in enumerate(raw)),
                    "ZDO overlapping/non-linked instruction")
            covered.update(span)
            instructions[address] = raw
        obj = objects[module]
        require(obj.startswith(b";!FILE ") and sha(obj.split(b"\n", 1)[1]) == OBJECTS[module][1],
                "ZDO complete relocatable object changed")
        areas = {n: int(s, 16) for n, s in re.findall(
            r"^A (\S+) size ([0-9A-F]+) flags \S+ addr \S+$", obj.decode("ascii"), re.M)}
        extent = sum(s for n, s in areas.items()
                     if n in ("HOME", "GSFINAL", "CSEG", "CONST") or n.startswith("GSINIT"))
        require((extent, *(areas.get(n, 0) for n in ("XSEG", "DSEG", "OSEG", "BSEG"))) ==
                OBJECTS[module][0], "ZDO object accounting changed")
    for module in ("zdo_node", "test_zdo_node"):
        for i, expected in enumerate(FIELDS):
            found = re.findall(rf"^T:F{module}\$__{i:08d}\[(.*)\]$", debug, re.M)
            require(len(found) == 1, "ZDO missing/duplicate field record")
            actual = re.findall(r"\(\{(\d+)\}S:S\$([^$]+)\$0_0\$0\(\{(\d+)\}", found[0])
            require(tuple((int(o), n, int(s)) for o, n, s in actual) == expected, "ZDO field ABI changed")
    require(len({n[:32] for n in PARAMS}) == len(PARAMS)
            and all(symbols.get(n) == v for n, v in PARAMS.items()), "ZDO parameter map ABI changed")
    for kind, struct in (("req", "01"), ("rsp", "02")):
        for operation in ("decode", "encode"):
            name = f"zdo_node_{kind}_{operation}"
            args = (("body", "{3}DG,SC:U"), ("size", "{2}SI:U"),
                    ("output", "{3}DG,ST__000000" + struct + ":S")) if operation == "decode" else (
                        ("request" if kind == "req" else "response", "{3}DG,ST__000000" + struct + ":S"),
                        ("body", "{3}DG,SC:U"), ("capacity", "{2}SI:U"), ("size", "{3}DG,SC:U"))
            for arg, spec in args:
                require(re.search(r"^S:Lzdo_node\." + name + r"\$" + arg + r"\$[^(]+\("
                                  + re.escape(spec) + r"\),F,0,0$", debug, re.M),
                        f"ZDO generic-pointer/argument ABI changed: {name}/{arg}")
    require(private_span(debug, "zdo_node") == set(range(86))
            and private_span(debug, "aps_frame") == set(range(86, 155)),
            "ZDO/APS compiler-private prefixes changed")
    caller = set(range(155, 159))
    require(symbols["_zdo_node_checks"] == 155
            and "S:G$zdo_node_checks$0_0$0({4}SL:U),F,0,0\n" in debug, "ZDO counter ABI changed")
    for name, (address, size) in CALLER.items():
        prefix = f"Ftest_zdo_node${name}$0_0$0"
        require(cdb_address(debug, "L:" + prefix) == address and f"S:{prefix}({{{size}}}" in debug,
                "ZDO caller storage changed")
        span = set(range(address, address + size))
        require(not caller & span and span <= allocated, "ZDO caller objects overlap")
        caller.update(span)
    require(caller == set(range(155, 542))
            and private_span(debug, "test_zdo_node") == set(range(542, 554))
            and all(symbols.get(k) == v for k, v in RUNTIME.items())
            and allocated == set(range(574)) | set(range(0x1e00, 0x1e08)),
            "ZDO caller/local/libc/status ownership changed")
    for name, (module, address) in PUBLIC.items():
        require(symbols["_" + name] == cdb_address(debug, f"L:G${name}$0$0") ==
                label(listings[module].decode("ascii"), name) == address and address in instructions,
                "ZDO/APS public entry changed")
        require(f"F:G${name}$0_0$0({{2}}DF,SC:U),Z,0,0,0,0,0\n" in debug, "ZDO/APS result ABI changed")
    lo = cdb_address(debug, "L:Ftest_zdo_node$aps_cases$0$0")
    hi = cdb_address(debug, "L:XFtest_zdo_node$aps_cases$0$0")
    calls = {int.from_bytes(raw[1:], "big") for a, raw in instructions.items()
             if lo <= a <= hi and len(raw) == 3 and raw[0] == 0x12}
    require(all(address in calls for _, address in PUBLIC.values()), "ZDO corpus bypasses real APS/ZDO calls")
    require(symbols["_main"] == 13099 and symbols["_zdo_node_done"] == 13172
            and instructions.get(13172) == b"\0"
            and code_bytes(image, SIZE)[13172:13175] == b"\0\x80\xfe", "ZDO genuine checkpoint changed")
    return allocated


def rejected(function):
    try:
        function()
    except ValueError:
        return
    raise ValueError("ZDO negative accepted")


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
    reject(memory=memory.replace("191 bytes available", "190 bytes available"))
    reject(memory=memory.replace("574", "575"))
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
    require(ram[0x1e00:0x1e06] == b"ZND1\x01\x08", "ZDO status ABI changed")
    failure = int.from_bytes(ram[0x1e06:0x1e08], "little")
    require(failure == 0, f"Genuine ZDO corpus failed at C line {failure}")
    address = symbols["_zdo_node_checks"]
    require(int.from_bytes(ram[address:address+4], "little") == CHECKS, "ZDO target check count changed")
    require(all(v == 0xa5 for a, v in enumerate(ram) if a not in allocated),
            "ZDO unused/status-tail/alias XDATA write")
    require(iram[0x7d:] == b"\xc7" * 131 and sfr[1] == 0x40, "ZDO stack/alias cap or unwind changed")
    require(all(sfr[a-128] == v for a, v in GUARDS.items()), "ZDO changed GPIO/clock/timer/RF")


def check_peak(text):
    peaks = re.findall(r"Max value of stack pointer=\s*0x([0-9a-fA-F]+)", text)
    require(len(peaks) == 1 and int(peaks[0], 16) == PEAK <= 0x7c, "ZDO full-run stack changed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--simulator", default="s51")
    args = parser.parse_args()
    path = args.output / "zdo_node_test.ihx"
    image = parse_ihex(path.read_text())
    symbols = parse_symbols(path.with_suffix(".map").read_text())
    debug_raw = path.with_suffix(".cdb").read_bytes()
    memory = path.with_suffix(".mem").read_text()
    listings = {m: (args.output / f"zdo_node_test.{m}.rst").read_bytes() for m in MODULES}
    objects = {m: (args.output / f"{m}.rel").read_bytes() for m in MODULES}
    allocated = verify(image, symbols, debug_raw, memory, listings, objects)
    count = negatives(image, symbols, debug_raw, memory, listings, objects)
    require(count == 39047, "ZDO artifact rejection inventory changed")
    check_alias(args.simulator)
    rejected(lambda: check_alias(args.simulator, False))
    commands = [ALIAS, "fill xram 0 0x1eff 0xa5", f"run 0 {symbols['_main']:#x}",
                "fill iram 0x7d 0xff 0xc7"]
    commands += [f"set memory sfr {a:#x} {v:#x}" for a, v in GUARDS.items()]
    stop = symbols["_zdo_node_done"]
    text = simulate(args.simulator, commands + [f"run {symbols['_main']:#x} {stop:#x}"] +
                    snapshot_commands(1), path)
    check_pc(section(text, 1), stop)
    state = snapshot(text, 1)
    check_result(*state, allocated, symbols)
    check_peak(section(text, 1))
    guards = ((0, 0x1e00), (0, 0x1e06), (0, symbols["_zdo_node_checks"]),
              (0, XDATA), (0, 0x1dff), (0, 0x1e08), (1, 0xff), (1, 0x7d), (2, 1))
    guards += tuple((2, a - 128) for a in GUARDS)
    for region, address in guards:
        changed = [bytearray(s) for s in state]
        changed[region][address] ^= 1
        rejected(lambda: check_result(*changed, allocated, symbols))
    for bad in ("", "Max value of stack pointer=0x5f",
                "Max value of stack pointer=0x5e\nMax value of stack pointer=0x5e"):
        rejected(lambda: check_peak(bad))
    print(f"ZDO Node Descriptor: {CHECKS} genuine target checks; {SIZE}/{CODE_BUDGET} CODE, "
          f"{XDATA}+64/{XDATA_BUDGET} XDATA; full-run SP={PEAK:02X}/7C; "
          f"{count} artifact + {len(guards)} snapshot + 3 peak + 1 alias negatives PASS. "
          "Synthetic descriptor metadata, NOT endpoint exposure or authenticated join.")


if __name__ == "__main__":
    main()
