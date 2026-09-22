#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Prove three genuine Temperature/reporting compositions; never board firmware."""

import argparse
import re
import unittest
from pathlib import Path

from boot_image import (
    ALIAS, check_alias, check_pc, section, simulate, snapshot, snapshot_commands,
    verify_component_layout,
)
from boot_nwk_candidates import label, listing_metrics, records
from boot_zcl_basic import (
    FIELDS as COMMON_FIELDS, canonical, private_span, sha, verify_value_abi, verify_write_abi,
)
from verify_firmware import cdb_address, code_bytes, parse_ihex, parse_symbols, require


PRODUCTION = ("zcl_temperature", "zcl_dispatch", "zcl_write", "zcl_attributes", "zcl_frame", "zcl_value")
CODE_BUDGET, XDATA_BUDGET = 24576, 1536
# Aggregate identities include every module name and every byte/ordered record,
# not a selected projection of CDB, map, area declarations or immediate listings.
PINS = {
    "wire": dict(
        part=1, code=21513, xdata=1223, stack=0x58, main=20573, done=20626,
        cases=36, peak=0x7c, local_end=1203, caller_code=3671, caller_const=401, caller_data=3,
        code_sha="50b7219c92628bac2ffd8fe1d03c8ef3080ad664b33c93d289e4643f5a5c5f3e",
        cdb_sha="a6ea59988dd9e3d5391478963bf740eb37f6147630f4585b528223bf9e5f6eb7",
        map_sha="57c3ddd2e04b9b49824d6db10d1f19c6ab6dbb456b8ce9f47ad5e8371420c55b",
        list_sha="2930089aa9e25deaf875f9341ed9c1bed9f6925234d2543a9a22069610ac1522",
        metrics_sha="7fed003a9e8ff21d98a052b4554506b715820a043cc71e0e5985f8ca8a6c3383",
        areas_sha="99957193937fe8f707679bc6635201f6b437262b504d8dff38c30cb117029479",
    ),
    "config": dict(
        part=2, code=22033, xdata=1215, stack=0x58, main=21338, done=21392,
        cases=128, peak=0x75, local_end=1195, caller_code=4393, caller_const=199, caller_data=3,
        code_sha="80b0675dffe2e489d8a705bb9b67148288c3ace2df2e6fe211b55b76c3677a1c",
        cdb_sha="87d4d91ad8bf06e219db9b5ee93e684b571d383412e27bf14f3618e3e0c3c867",
        map_sha="6700b6c4eb9993cfe49f2ec593db20c6d6896ab2b0568e107cb5add38cd9039c",
        list_sha="ced3c6ebcbb7777c92c5f81b75bd45b72d899ae17a28d76de2ff752ad23bae14",
        metrics_sha="ee8e49efc3807dadd131625dab60fa1e2196231bda7e8e200bfceac035594e32",
        areas_sha="712e0b0ae43a3a9e91a2c03aa7614f68f6289033aae1eb226ee25cf48ab69b12",
    ),
    "report": dict(
        part=3, code=24278, xdata=1235, stack=0x5a, main=23737, done=23792,
        cases=2, peak=0x7a, local_end=1215, caller_code=6793, caller_const=44, caller_data=5,
        code_sha="8cc48f58d23b0d06f5306f88b9c972ac74affa960e800e31d7105060949715b4",
        cdb_sha="9bc1de95ab4da9e8c33248c2801bae569e7c7a18e97fb26a2ab5841ed682c526",
        map_sha="349778f35d2cc2273d204ccda896caa56ae9f2f47d5d410f49da766bae858e0e",
        list_sha="d8bbee65698f5f56f4064159083516deada79fad59e5a15fd189d43080ec312d",
        metrics_sha="0a60913f03cfbc36f765016c9b84153a14cbd28c8f8d562f9b9bbffdb2d3f4c1",
        areas_sha="b2f841749570a3d9fa3657b572aa95b9867bb8b1aa2afcfe13e36b73aa5071d6",
    ),
}
# CSEG, XSEG, DSEG, maximum OSEG allocation, BSEG bits.
OBJECTS = {
    "zcl_temperature": (8152, 361, 26, 6, 7),
    "zcl_dispatch": (2725, 134, 11, 3, 1),
    "zcl_write": (1552, 144, 0, 0, 0),
    "zcl_attributes": (1958, 150, 7, 12, 1),
    "zcl_frame": (1173, 35, 6, 15, 0),
    "zcl_value": (1345, 40, 10, 0, 1),
}
CALLER = {"ctx": (864, 37), "saved": (901, 37), "info": (938, 10), "report": (948, 4),
          "request": (952, 102), "response": (1054, 102), "checks": (1156, 2)}
RUNTIME = {"___memcpy_PARM_2": 0, "___memcpy_PARM_3": 3, "_memset_PARM_2": 8,
           "_memset_PARM_3": 9, "__gptrput_PARM_2": 11, "_memcmp_PARM_2": 12, "_memcmp_PARM_3": 15}
FIELDS = COMMON_FIELDS[:7] + (
    ((0, "kind", 1), (1, "length", 1), (2, "command_id", 1), (3, "sequence", 1),
     (4, "requested_count", 1), (5, "returned_count", 1), (6, "discovery_complete", 1),
     (7, "default_command", 1), (8, "default_status", 1), (9, "default_raw_status", 1)),
    ((0, "minimum", 2), (2, "maximum", 2), (4, "change", 2)),
    ((0, "stamp", 4), (4, "pending_at", 4), (8, "age", 2), (10, "serial", 2),
     (12, "minimum", 2), (14, "maximum", 2), (16, "value", 2), (18, "baseline", 2),
     (20, "pending_value", 2), (22, "defaults", 6), (28, "reporting", 6),
     (34, "configured", 1), (35, "pending", 1), (36, "fault", 1)),
    ((0, "token", 2), (2, "ready", 1), (3, "length", 1)),
)
PUBLIC = {
    "zcl_temperature": ("zcl_temp_init", "zcl_temp_sample", "zcl_temp_rx", "zcl_temp_prepare", "zcl_temp_finish"),
    "zcl_dispatch": ("zcl_dispatch_unicast",),
    "zcl_write": ("zcl_wr_handle",),
    "zcl_attributes": ("zcl_attr_set_check", "zcl_read_attrs_unicast"),
    "zcl_frame": ("zcl_frame_decode", "zcl_frame_encode"),
    "zcl_value": ("zcl_value_type_supported", "zcl_value_decode", "zcl_value_encode"),
}
HELPERS = {"get16": "SI:U", "put16": "SV:S", "valid_cfg": "SC:U", "advance": "SC:U",
           "configure": "SV:S", "change_width": "SC:U", "known": "SC:U", "reporting": "SC:U", "due": "SC:U"}


def cpu_only(instructions):
    """Direct/bit SFR operands in all reviewed C-module instructions are CPU-only."""
    direct = {0x05, 0x15, 0x25, 0x35, 0x42, 0x43, 0x45, 0x52, 0x53, 0x55,
              0x62, 0x63, 0x65, 0x75, 0x95, 0xb5, 0xc0, 0xd0, 0xd5, 0xe5, 0xf5}
    bits = {0x10, 0x20, 0x30, 0x72, 0x82, 0x92, 0xa0, 0xa2, 0xb0, 0xb2, 0xc2, 0xd2}
    for data in instructions.values():
        op = data[0]
        operands = data[1:3] if op == 0x85 else data[1:2] if (
            op in direct or 0x86 <= op <= 0x8f or 0xa6 <= op <= 0xaf) else ()
        if op in bits:
            operands = (data[1] & 0xf8,)
        require(all(a < 0x80 or a in (0x81, 0x82, 0x83, 0xd0, 0xe0, 0xf0) for a in operands),
                "Temperature C module accesses a non-CPU SFR")


def verify_abi(debug, symbols, instructions, listings):
    for i, expected in enumerate(FIELDS):
        found = re.findall(rf"^T:Fzcl_temperature\$__{i:08d}\[(.*)\]$", debug, re.MULTILINE)
        require(len(found) == 1, "Temperature field record missing/duplicated")
        actual = re.findall(r"\(\{(\d+)\}S:S\$([^$]+)\$0_0\$0\(\{(\d+)\}", found[0])
        require(tuple((int(o), n, int(s)) for o, n, s in actual) == expected, "Temperature field ABI changed")
    ctx, cfg, info, lease = ("{3}DG,ST__000000" + n + ":S" for n in ("09", "08", "07", "10"))
    u32, u16, i16, u8, ptr = "{4}SL:U", "{2}SI:U", "{2}SI:S", "{1}SC:U", "{3}DG,SC:U"
    parameters = {
        "init": (("ctx", ctx), ("now", u32), ("minimum", i16), ("maximum", i16), ("defaults", cfg)),
        "sample": (("ctx", ctx), ("now", u32), ("value", i16)),
        "rx": (("ctx", ctx), ("now", u32), ("request", ptr), ("length", u16),
               ("response", ptr), ("capacity", u16), ("info", info)),
        "prepare": (("ctx", ctx), ("now", u32), ("sequence", u8), ("routes", u8),
                    ("response", ptr), ("capacity", u16), ("report", lease)),
        "finish": (("ctx", ctx), ("now", u32), ("token", u16), ("sent", u8), ("issued_at", u32)),
    }
    for suffix, args in parameters.items():
        spans = set()
        for number, (name, spec) in enumerate(args, 1):
            found = re.findall(r"^S:(Lzcl_temperature\.zcl_temp_" + suffix + r"\$" + name
                               + r"\$[^(]+)\(" + re.escape(spec) + r"\),F,0,0$", debug, re.MULTILINE)
            require(len(found) == 1, f"Temperature argument ABI changed: {suffix}/{name}")
            address = cdb_address(debug, "L:" + found[0])
            size = int(re.match(r"\{(\d+)\}", spec)[1])
            span = set(range(address, address + size))
            require(not spans & span and span <= private_span(debug, "zcl_temperature"),
                    "Temperature argument aliases caller/other parameters")
            spans.update(span)
            if number > 1:
                require(symbols[f"_zcl_temp_{suffix}_PARM_{number}"] == address, "Temperature parameter map ABI changed")
    for module, functions in PUBLIC.items():
        for name in functions:
            address = cdb_address(debug, f"L:G${name}$0$0")
            require(symbols["_" + name] == label(listings[module].decode("ascii"), name) == address
                    and address in instructions, "Temperature public CODE/map/listing ABI changed")
            require(f"F:G${name}$0_0$0({{2}}DF,SC:U),Z,0,0,0,0,0\n" in debug,
                    "Temperature byte-return ABI changed")
    for name, result in HELPERS.items():
        require(f"F:Fzcl_temperature${name}$0_0$0({{2}}DF,{result}),C,0,0,0,0,0\n" in debug,
                "Temperature private helper return ABI changed")
        address = cdb_address(debug, f"L:Fzcl_temperature${name}$0$0")
        require(label(listings["zcl_temperature"].decode("ascii"), name) == address and address in instructions,
                "Temperature private helper listing changed")
    verify_write_abi(debug, symbols, instructions)
    verify_value_abi(debug, instructions)

    def addr(name):
        return symbols["_" + name] if name not in HELPERS else cdb_address(debug, f"L:Fzcl_temperature${name}$0$0")

    def calls(name):
        key = f"Fzcl_temperature${name}" if name in HELPERS else f"G${name}"
        lo, hi = (cdb_address(debug, "L:" + p + key + "$0$0") for p in ("", "X"))
        return {int.from_bytes(b[1:], "big") for a, b in instructions.items()
                if lo <= a <= hi and len(b) == 3 and b[0] == 0x12}

    for name, targets in (
        ("zcl_temp_init", ("valid_cfg", "configure")),
        ("zcl_temp_sample", ("advance",)),
        ("zcl_temp_rx", ("advance", "reporting", "zcl_dispatch_unicast", "zcl_frame_decode")),
        ("zcl_temp_prepare", ("advance", "due", "zcl_value_encode", "zcl_frame_encode")),
        ("zcl_temp_finish", ("advance",)),
        ("reporting", ("get16", "put16", "change_width", "known", "valid_cfg", "configure", "zcl_frame_encode")),
        ("change_width", ("zcl_value_type_supported",)),
        ("zcl_dispatch_unicast", ("zcl_attr_set_check", "zcl_frame_decode", "zcl_read_attrs_unicast", "zcl_frame_encode", "zcl_wr_handle")),
        ("zcl_read_attrs_unicast", ("zcl_value_encode", "zcl_frame_encode")),
    ):
        require(all(addr(t) in calls(name) for t in targets), f"Temperature genuine calls missing in {name}")


def verify(part, image, symbols, debug_raw, memory, listings, objects):
    pin = PINS[part]
    require(isinstance(debug_raw, bytes) and sha(debug_raw) == pin["cdb_sha"], "Temperature complete raw CDB identity changed")
    debug = debug_raw.decode("ascii")
    require(pin["code"] <= CODE_BUDGET and set(image) == set(range(pin["code"]))
            and sha(code_bytes(image, pin["code"])) == pin["code_sha"], "Temperature complete CODE identity changed")
    require(sha(canonical(symbols)) == pin["map_sha"], "Temperature complete parsed map identity changed")
    stem = f"zcl_temperature_{part}_test"
    modules = PRODUCTION + (stem,)
    require(set(listings) == set(objects) == set(modules), "Temperature module inventory changed")
    allocated = verify_component_layout(
        image, symbols, debug, memory, "zcl_temp_result",
        tuple(m + ".c" for m in PRODUCTION) + ("test_zcl_temperature.c",), xdata_budget=XDATA_BUDGET,
    )
    require(symbols["s_XSEG"] == 0 and symbols["l_XSEG"] == pin["xdata"]
            and symbols["s_SSEG"] == pin["stack"], "Temperature storage extents changed")
    require(re.findall(r"EXTERNAL RAM\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s+(\d+)\s+(\d+)", memory) ==
            [("0x0000", f"0x{pin['xdata']-1:04x}", str(pin["xdata"]), "7680")], "Temperature memory accounting changed")
    require(sha(canonical({m: sha(listings[m]) for m in modules})) == pin["list_sha"],
            "Temperature complete immediate listings changed")
    require(sha(canonical({m: listing_metrics(listings[m].decode("ascii")) for m in modules})) == pin["metrics_sha"],
            "Temperature complete ordered instruction metrics changed")
    areas = {m: re.findall(r"^A (\S+) size (\S+) flags (\S+) addr (\S+)$", objects[m], re.MULTILINE) for m in modules}
    require(sha(canonical(areas)) == pin["areas_sha"], "Temperature complete object allocations changed")
    instructions, coverage, offset = {}, set(), 0
    for module in modules:
        for address, data in records(listings[module].decode("ascii")):
            span = set(range(address, address + len(data)))
            require(not span & coverage and all(image.get(address+i) == b for i, b in enumerate(data)),
                    "Temperature duplicate/overlapping/non-linked instructions")
            coverage.update(span); instructions[address] = data
        sizes = {n: int(s, 16) for n, s, _, _ in areas[module]}
        expected = OBJECTS.get(module, (pin["caller_code"], pin["local_end"]-864, pin["caller_data"], 0, 0))
        require(tuple(sizes.get(n, 0) for n in ("CSEG", "XSEG", "DSEG", "OSEG", "BSEG")) == expected
                and sizes["CONST"] == (pin["caller_const"] if module == stem else 0), "Temperature exact module budget changed")
        if module in PRODUCTION:
            span = private_span(debug, module)
            if module == "zcl_temperature":
                require("S:Fzcl_temperature$next_age$0_0$0({2}SI:U),F,0,0\n" in debug
                        and cdb_address(debug, "L:Fzcl_temperature$next_age$0_0$0") == 0, "Temperature staged age ABI changed")
                span |= {0, 1}
            require(span == set(range(offset, offset + sizes["XSEG"])), "Temperature private storage ownership changed")
            offset += sizes["XSEG"]
    require(offset == 864, "Temperature production/caller boundary changed")
    caller = set()
    for name, (address, size) in CALLER.items():
        key = f"Ftest_zcl_temperature${name}$0_0$0"
        require(cdb_address(debug, "L:" + key) == address and f"S:{key}({{{size}}}" in debug, "Temperature caller ABI changed")
        span = set(range(address, address + size))
        require(not caller & span and span <= allocated, "Temperature caller objects overlap")
        caller.update(span)
    require(caller == set(range(864, 1158)) and private_span(debug, "test_zcl_temperature") ==
            set(range(1158, pin["local_end"])), "Temperature caller/private boundary changed")
    require(all(symbols[k] == pin["local_end"] + v for k, v in RUNTIME.items())
            and pin["xdata"] == pin["local_end"] + 20, "Temperature libc/compiler scratch boundary changed")
    require(allocated == set(range(pin["xdata"])) | set(range(0x1e00, 0x1e08)), "Temperature unaccounted XDATA/status allocation")
    verify_abi(debug, symbols, instructions, listings)
    def function_calls(key):
        lo, hi = (cdb_address(debug, "L:" + p + key + "$0$0") for p in ("", "X"))
        return {int.from_bytes(b[1:], "big") for a, b in instructions.items()
                if lo <= a <= hi and len(b) == 3 and b[0] == 0x12}

    def caller_address(name):
        return cdb_address(debug, f"L:Ftest_zcl_temperature${name}$0$0")

    require(caller_address("run_tests") in function_calls("G$main"), "Temperature main bypasses the real corpus")
    groups = {"wire": ("wire_cases", "range_cases"), "config": ("config_types", "config_cases"),
              "report": ("reporting_cases", "clock_cases")}[part]
    require(all(caller_address(name) in function_calls("Ftest_zcl_temperature$run_tests") for name in groups),
            "Temperature corpus bypasses a partition case group")
    for helper, target in (("reset", "init"), ("rx", "rx"), ("fail_rx", "rx")) + (
            (("prepare", "prepare"),) if part == "report" else ()):
        require(symbols["_zcl_temp_" + target] in function_calls("Ftest_zcl_temperature$" + helper),
                "Temperature caller wrapper bypasses the genuine service")
    for name in groups:
        found = function_calls("Ftest_zcl_temperature$" + name)
        targets = (("reset", "prepare") if part == "report" else ("reset", "rx") if
                   name != "range_cases" else ("reset",))
        require(all(caller_address(t) in found for t in targets), "Temperature case group bypasses actual wrappers")
        if part == "report":
            require(all(symbols["_zcl_temp_" + t] in found for t in ("sample", "finish")),
                    "Temperature report corpus bypasses sample/transport completion")
    discover = cdb_address(debug, "L:Fzcl_dispatch$discover$0$0")
    require(discover in function_calls("G$zcl_dispatch_unicast")
            and all(symbols[n] in function_calls("Fzcl_dispatch$discover")
                    for n in ("_zcl_value_type_supported", "_zcl_frame_encode")),
            "Temperature discovery bypasses actual shared value/frame code")
    cpu_only(instructions)
    require(symbols["_main"] == pin["main"] and symbols["_zcl_temp_done"] == pin["done"]
            and code_bytes(image, pin["code"])[pin["done"]:pin["done"]+3] == b"\0\x80\xfe"
            and cdb_address(debug, "L:XG$main$0$0") == pin["done"]+3
            and instructions[pin["done"]] == b"\0", "Temperature exact checkpoint changed")
    return allocated


def negatives(part, args):
    count = 0
    case = unittest.TestCase()
    pin = PINS[part]

    def reject(**change):
        nonlocal count
        with case.assertRaises(ValueError):
            verify(part, **(args | change))
        count += 1

    image, symbols, raw = (args[k] for k in ("image", "symbols", "debug_raw"))
    for address in (0, pin["code"]-1, pin["done"], symbols["___memcpy"],
                    *(symbols["_" + f] for fs in PUBLIC.values() for f in fs)):
        reject(image=image | {address: image[address] ^ 1})
    reject(image={a: b for a, b in image.items() if a != pin["code"]-1})
    reject(image=image | {pin["code"]: 0})
    for name in (*RUNTIME, "s_SSEG", "l_XSEG", "_zcl_temp_done", "_zcl_temp_result"):
        reject(symbols=symbols | {name: symbols[name]+1})
    reject(symbols=symbols | {"unreviewed_extra_symbol": 0})
    for prefix in (b"F:", b"S:", b"L:", b"T:", b"F:Fzcl_temperature", b"S:Lzcl_temperature",
                   b"L:Lzcl_temperature", b"S:Fzcl_temperature$next_age", b"S:Ftest_zcl_temperature",
                   b"L:C$test_zcl_temperature", b"F:G$zcl_wr_handle", b"T:Fzcl_write",
                   b"F:Fzcl_value$value_shape", b"S:Lzcl_value.non_value_pattern"):
        lines = [line for line in raw.splitlines(keepends=True) if line.startswith(prefix)]
        require(lines, f"Missing Temperature raw metadata negative prerequisite: {prefix!r}")
        reject(debug_raw=raw.replace(lines[0], b"", 1))
        reject(debug_raw=raw + lines[0])
    reject(debug_raw=raw.replace(b"\n", b"\r\n"))
    reject(debug_raw=raw + b"\xff")
    reject(memory=args["memory"].replace(str(256-pin["stack"]) + " bytes available", "1 bytes available"))
    reject(memory=args["memory"].replace(str(pin["xdata"]), str(pin["xdata"]+1)))
    for module, raw_listing in args["listings"].items():
        reject(listings=args["listings"] | {module: b""})
        lines = raw_listing.splitlines(keepends=True)
        indexes = [i for i, line in enumerate(lines) if records(line.decode("ascii"))]
        first, second = indexes[:2]
        for operation in ("delete", "duplicate", "reorder", "mutate"):
            changed = lines.copy()
            if operation == "delete": del changed[first]
            elif operation == "duplicate": changed.insert(first, lines[first])
            elif operation == "reorder": changed[first], changed[second] = changed[second], changed[first]
            else: changed[first] = changed[first].replace(b" ", b"\t", 1)
            reject(listings=args["listings"] | {module: b"".join(changed)})
        reject(objects=args["objects"] | {module: ""})
        reject(objects=args["objects"] | {module: args["objects"][module].replace("A XSEG size ", "A ISEG size ", 1)})
    reject(listings={m: v for m, v in args["listings"].items() if m != "zcl_temperature"})
    return count


def check_result(part, ram, iram, sfr, initial_sfr, allocated):
    pin = PINS[part]
    require(ram[0x1e00:0x1e08] == bytes((0x5a, 0x54, pin["part"], 1, 1, 8, 0, 0)),
            f"Temperature {part} target failure: {ram[0x1e00:0x1e08].hex()}")
    require(int.from_bytes(ram[1156:1158], "little") == pin["cases"], "Temperature skipped common cases")
    require(all(v == 0xa5 for a, v in enumerate(ram) if a not in allocated), "Temperature writes outside ordinary/status allocation")
    require(iram[0x7d:] == b"\xc7" * 131 and sfr[1] == pin["stack"]-1, "Temperature SP7C/IRAM alias/checkpoint guard failed")
    require(all(sfr[a-0x80] == 0 for a in (0xa8, 0xb8, 0x9a)), "Temperature enabled interrupts")
    require(all(v == initial_sfr[i] for i, v in enumerate(sfr) if i+0x80 not in (0x81, 0x82, 0x83, 0xd0, 0xe0, 0xf0)),
            "Temperature changed non-CPU SFR state")


def check_peak(part, text):
    found = re.findall(r"Max value of stack pointer=\s*0x([0-9a-fA-F]+)", text)
    require(len(found) == 1 and int(found[0], 16) == PINS[part]["peak"] <= 0x7c, "Temperature uninterrupted SP peak changed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--simulator", default="s51")
    args = parser.parse_args()
    case = unittest.TestCase()
    check_alias(args.simulator)
    with case.assertRaises(ValueError):
        check_alias(args.simulator, False)
    for code in (b"\xe5\x90", b"\x75\xa8\x01", b"\x85\x90\xe0", b"\xd2\xaf"):
        with case.assertRaises(ValueError):
            cpu_only({0: code})
    for part, pin in PINS.items():
        stem = f"zcl_temperature_{part}_test"
        path = args.output / (stem + ".ihx")
        modules = PRODUCTION + (stem,)
        artifacts = dict(
            image=parse_ihex(path.read_text(encoding="ascii")),
            symbols=parse_symbols(path.with_suffix(".map").read_text(encoding="ascii")),
            debug_raw=path.with_suffix(".cdb").read_bytes(),
            memory=path.with_suffix(".mem").read_text(encoding="ascii"),
            listings={m: (args.output / f"{stem}.{m}.rst").read_bytes() for m in modules},
            objects={m: (args.output / f"{m}.rel").read_text(encoding="ascii") for m in modules},
        )
        allocated = verify(part, **artifacts)
        count = negatives(part, artifacts)
        setup = [ALIAS, "fill xram 0 0x1eff 0xa5",
                 "set memory sfr 0xa8 0", "set memory sfr 0xb8 0", "set memory sfr 0x9a 0"]
        text = simulate(args.simulator, setup + [f"run 0 {pin['main']:#x}", "fill iram 0x7d 0xff 0xc7"]
                        + snapshot_commands(1) + [f"run {pin['main']:#x} {pin['done']:#x}"] + snapshot_commands(5), path)
        initial_sfr = snapshot(text, 1)[2]
        check_pc(section(text, 5), pin["done"])
        ram, iram, sfr = snapshot(text, 5)
        check_result(part, ram, iram, sfr, initial_sfr, allocated)
        full = simulate(args.simulator, setup + [f"run 0 {pin['done']:#x}"] + snapshot_commands(1), path)
        check_pc(section(full, 1), pin["done"])
        check_peak(part, section(full, 1))
        for region, address in ((0, 0x1e00), (0, 0x1e06), (0, 1156), (0, pin["xdata"]),
                                (0, 0x1dff), (0, 0x1e3f), (1, 0x7d), (2, 1), (2, 0x28), (2, 0x10)):
            bad = [bytearray(ram), bytearray(iram), bytearray(sfr)]
            bad[region][address] ^= 1
            with case.assertRaises(ValueError):
                check_result(part, *bad, initial_sfr, allocated)
        for bad in ("", "Max value of stack pointer= 0x7d", f"Max value of stack pointer= {pin['stack']-1:#x}"):
            with case.assertRaises(ValueError):
                check_peak(part, bad)
        print(f"Temperature {part}: {pin['code']}/{CODE_BUDGET} CODE, {pin['xdata']}+64/{XDATA_BUDGET} XDATA; "
              f"{pin['cases']} case groups, full SP{pin['peak']:02X}/cap7C, checkpoint SP{pin['stack']-1:02X}; "
              f"complete CODE/raw-CDB/map, 7 snapshots, {count} artifact +10 guard +3 peak negatives PASS.")
    print("Temperature: 3 real compositions, 1 alias +4 MMIO negatives PASS; logical synthetic state only, no RF or delivery evidence.")


if __name__ == "__main__":
    main()
