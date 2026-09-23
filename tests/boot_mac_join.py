#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Strict genuine staged-association images; synthetic facts, no physical adapter."""

import argparse
from functools import lru_cache
import re
import struct
import unittest
from pathlib import Path

from boot_image import (
    ALIAS, check_alias, check_pc, section, simulate, snapshot, snapshot_commands,
    verify_component_layout,
)
from boot_nwk_candidates import label, records
from boot_zcl_basic import canonical, sha
from boot_zcl_temperature import cpu_only
from verify_firmware import cdb_address, code_bytes, parse_ihex, parse_symbols, require


MODULES = ("mac_frame", "mac_tx", "mac_association", "mac_poll", "mac_join")
CODE_BUDGET, XDATA_BUDGET = 32768, 3200
# One digest per complete artifact manifest: ALL CODE, raw CDB bytes, complete
# parsed map, ALL ordered object-area declarations and six full raw snapshots.
PINS = (
    (32703, 32045, "4b7f31b7b7e8e19b01b7fbc59e8068bf23c27cb62eb66be15a662f512cc4ed95"),
    (32704, 32046, "6302fd134a885ea88e1e306580993cdbfd67cb1c9ded5747866e7f3035be500f"),
    (32704, 32046, "f46bfb69cb456713a30ff8cd46fa40a271047d38be5d28ec2ce63b6a76c666c4"),
    (32704, 32046, "6b09254e433612b720939b9d657702e497baabd7af1fc79edafe8042c386b73e"),
    (32704, 32044, "db69b1ec878290a81c0ccd46f48c479a98c74070c9224c49479b61872a79c040"),
    (32347, 31689, "dfd66cf3b0d353e473c0aa20734c6a1d272813bc3d4e12b0653f9b50d85a9f09"),
    (32335, 31677, "5b90da048038e760cd1859c66cc82d0d50ca792bd4137b2c2a2d3b67659c7542"),
    (32329, 31671, "252b8400546df3b1a7bcc8d2f81fab4e2f77c43e8cce1e2858fc776d4b5e0f66"),
    (32373, 31715, "587764bbfa90da76aae72e20b559770255a6d4b2f4d14edeb2134346fbab5b9c"),
    (32621, 31963, "bc9e978769fb64077eb25b09023de39d345f3051a842f0b9f1807bb5cdf19c8d"),
    (32370, 31712, "35a273d8c476c6e968572c6f688d4ef27a050fa8ef005b44db8f4005ffded7dc"),
    (32610, 31952, "01031976b74f21bba254fb1b79dec3e50f0c35e1d706a23121077bad794a1d2f"),
    (32746, 32088, "e7078c46430ed90244fd73c0c64ce4a62aaa03186c6c69694c3d915453e3953a"),
    (32740, 32082, "4e5b17bf3f9eb3e2cb89145f11fb88d7c618711ae855d656f9c69e576da19ad2"),
    (32606, 31948, "81f26f0906cbffb642ea4b8f839f2bdf5c5d099626cad0008e8899996c73f62e"),
    (32699, 32041, "9452810782b273faeec1cafbea42a4eb30d75de6235971eda4ee9d02a77c9611"),
    (32348, 31690, "f7533c15ffee5ae73afc08c944ce85b3582e90b04dc617e3e399bfb235b2f901"),
    (32638, 31980, "15cb41d8f025950ba94694c93af03541dbd0d962ac83be22355910208953ce50"),
    (32343, 31685, "4ac538119cfd5d61b83b5fd2f12e222a7fba9512505883b18b7aee64f6c6ece6"),
    (32329, 31671, "f22cd9351105c40ebdae7f5dbab1b6237b74b9d4c76139fa1231c935489bc2b7"),
    (32763, 32105, "8ab9be43aa2b71e40094b57ef038061a6c1eeaa2f2662daddb70d32722f0f4e6"),
    (32435, 31777, "a818dc63f443d1621a45b427b0260b093ea00db797ecf3424b167d475ccf4dd0"),
)
CALLS = (15, 15, 15, 15, 15, 21, 7, 7, 5, 15, 6, 29, 16, 15, 14, 15, 16, 15, 2, 2, 15, 9)
PEAKS = (0x7c, 0x7c, 0x7c, 0x7c, 0x7c, 0x76, 0x76, 0x76, 0x76, 0x7c, 0x76,
         0x7a, 0x7c, 0x7a, 0x7a, 0x7c, 0x76, 0x7a, 0x6e, 0x6e, 0x7c, 0x76)
# CSEG, XSEG, DSEG, OSEG, BSEG bits; no allocation is an extra IRAM alias pool.
OBJECTS = ((7136, 216, 15, 10, 1), (5650, 191, 8, 0, 3), (2839, 76, 27, 0, 1),
           (7619, 350, 5, 0, 4), (7184, 1067, 5, 0, 1))
CALLER = {
    "join": (1900, 645), "config": (2545, 43), "tx": (2588, 168), "event": (2756, 48),
    "action": (2804, 44), "radio": (2848, 22), "record": (2870, 194), "ack": (3064, 3),
    "source_kind": (3067, 1), "frames": (3068, 1), "now": (3069, 4), "boundary": (3073, 4),
    "duration": (3077, 2), "supplied": (3079, 3), "operation": (3082, 1), "phase": (3083, 1),
    "transmitter": (3084, 1), "calls": (3085, 1), "failure": (3086, 1),
    "release_result": (3087, 1), "grant": (3088, 2), "seen": (3090, 1),
}
RUNTIME = {"___memcpy_PARM_2": 3091, "___memcpy_PARM_3": 3094, "_memset_PARM_2": 3099,
           "_memset_PARM_3": 3100, "__gptrput_PARM_2": 3102, "__mulint_PARM_2": 3103,
           "__mullong_PARM_2": 3105, "_memcmp_PARM_2": 3109, "_memcmp_PARM_3": 3112}
PUBLIC = {
    "mac_frame": ("mac_command_decode", "mac_command_encode", "mac_beacon_decode",
                  "mac_frame_decode_profile", "mac_frame_decode", "mac_frame_encode"),
    "mac_tx": ("mac_tx_init", "mac_tx_submit", "mac_tx_copy", "mac_tx_step", "mac_tx_release"),
    "mac_association": ("mac_association_init", "mac_association_start", "mac_association_step",
                        "mac_association_step_rx", "mac_association_take"),
    "mac_poll": ("mac_poll_init", "mac_poll_start", "mac_poll_step", "mac_poll_step_rx",
                "mac_poll_take", "mac_poll_release"),
    "mac_join": ("mac_join_init", "mac_join_start", "mac_join_step", "mac_join_take", "mac_join_release"),
}
FIELDS = {
    17: ((0, "pan", 2), (2, "channel", 1), (3, "filter", 1), (4, "rx_on", 1)),
    18: ((0, "extraction", 35), (35, "saved", 5), (40, "response_wait", 1), (41, "capability", 1), (42, "profile", 1)),
    19: ((0, "poll", 143), (143, "association", 27), (170, "epoch", 4), (174, "generation", 4),
         (178, "request_ack", 4), (182, "result", 1), (183, "stage", 1), (184, "reason", 1),
         (185, "error_stage", 1), (186, "cleanup_error", 1), (187, "tx_outcome", 1), (188, "tx_rc", 1),
         (189, "poll_rc", 1), (190, "poll_reason", 1), (191, "poll_cleanup", 1), (192, "association_rc", 1),
         (193, "observation", 1)),
    20: ((0, "epoch", 4), (4, "generation", 4), (8, "until", 4), (12, "radio", 22),
         (34, "state", 5), (39, "token", 2), (41, "kind", 1), (42, "tx_cancel", 1), (43, "observation", 1)),
    21: ((0, "poll", 266), (266, "association", 73), (339, "request", 43), (382, "record", 194),
         (576, "outgoing", 25), (601, "length", 1), (602, "owner", 2), (604, "generation", 4),
         (608, "tx_generation", 4), (612, "last", 4), (616, "deadline", 4), (620, "wait_until", 4),
         (624, "stop_at", 4), (628, "steps", 2), (630, "token", 2), (632, "issued_token", 2),
         (634, "child_token", 2), (636, "version", 1), (637, "phase", 1), (638, "issued", 1),
         (639, "stopping", 1), (640, "stop_steps", 1), (641, "taken", 1), (642, "window", 1),
         (643, "restored", 1), (644, "uncertain", 1)),
}


@lru_cache(maxsize=128)
def areas(text):
    return re.findall(r"^A (\S+) size (\S+) flags (\S+) addr (\S+)$", text, re.MULTILINE)


@lru_cache(maxsize=128)
def span(debug, module):
    result = set()
    for key, size in re.findall(r"^S:([FL]" + re.escape(module)
                               + r"[.$][^(\n]+)\(\{(\d+)\}[^\n]*\),F,0,0$", debug, re.MULTILINE):
        # Optimized-out declarations remain in the complete raw-CDB identity,
        # but have no allocated L record. Public argument checks still require it.
        if not re.search(r"^L:" + re.escape(key) + ":", debug, re.MULTILINE):
            continue
        address = cdb_address(debug, "L:" + key)
        extent = set(range(address, address + int(size)))
        require(not result & extent, "Join compiler/private object overlap")
        result.update(extent)
    return result


def verify(number, image, symbols, debug_raw, memory, listings, objects):
    size, done, digest = PINS[number]
    stem = f"mac_join_{number}_test"
    modules = MODULES + (stem,)
    require(isinstance(debug_raw, bytes), "Join raw CDB must be bytes before decoding")
    require(tuple(listings) == tuple(objects) == modules, "Join ordered module inventory changed")
    require(size <= CODE_BUDGET and set(image) == set(range(size)), "Join complete CODE extent changed")
    manifest = {"cdb": sha(debug_raw), "code": sha(code_bytes(image, size)), "map": symbols,
                "areas": {m: areas(objects[m]) for m in modules},
                "listings": {m: sha(listings[m]) for m in modules}}
    require(sha(canonical(manifest)) == digest, "Join complete CODE/raw-CDB/map/object/snapshot identity changed")
    debug = debug_raw.decode("ascii")  # No F/S/L/T/helper/source-line filtering before the raw proof.
    allocated = verify_component_layout(image, symbols, debug, memory, "mac_join_result",
        tuple(m + ".c" for m in MODULES) + ("test_mac_join.c",), xdata_budget=XDATA_BUDGET)
    require(symbols["s_XSEG"] == 0 and symbols["l_XSEG"] == 3117 and symbols["s_SSEG"] == 0x51,
            "Join exact XDATA/IRAM allocation changed")
    require(re.findall(r"EXTERNAL RAM\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s+(\d+)\s+(\d+)", memory) ==
            [("0x0000", "0x0c2c", "3117", "7680")], "Join ordinary RAM accounting changed")
    instructions, coverage, offset = {}, set(), 0
    for index, m in enumerate(modules):
        text = listings[m].decode("ascii")
        for address, data in records(text):
            extent = set(range(address, address + len(data)))
            require(not coverage & extent and all(image.get(address+i) == b for i, b in enumerate(data)),
                    "Join overlapping/duplicate/non-linked instruction")
            coverage.update(extent); instructions[address] = data
        a = {name: int(n, 16) for name, n, _, _ in areas(objects[m])}
        if index < len(MODULES):
            require(tuple(a.get(k, 0) for k in ("CSEG", "XSEG", "DSEG", "OSEG", "BSEG")) == OBJECTS[index]
                    and a["CONST"] == 0, "Join exact production object cost changed")
            require(span(debug, m) == set(range(offset, offset + a["XSEG"])), "Join production/private ownership changed")
            offset += a["XSEG"]
        else:
            require(a["XSEG"] == 1191 and a["DSEG"] == 0 and a["CONST"] == (78 if number == 4 else 76),
                    "Join actual caller allocation changed")
    require(offset == 1900, "Join private/caller boundary changed")
    for name, (address, length) in CALLER.items():
        key = f"Ftest_mac_join${name}$0_0$0"
        require(cdb_address(debug, "L:" + key) == address and f"S:{key}({{{length}}}" in debug,
                "Join caller field/storage ABI changed")
    require(span(debug, "test_mac_join") == set(range(1900, 3091)), "Join unaccounted caller/private allocation")
    require(all(symbols[k] == v for k, v in RUNTIME.items()), "Join compiler/libc scratch overlaps caller")
    require(allocated == set(range(3117)) | set(range(0x1e00, 0x1e08)), "Join unaccounted allocation/status write region")
    for index, expected in FIELDS.items():
        found = re.findall(rf"^T:Fmac_join\$__{index:08d}\[(.*)\]$", debug, re.MULTILINE)
        require(len(found) == 1, "Join missing/duplicate field declaration")
        actual = re.findall(r"\(\{(\d+)\}S:S\$([^$]+)\$0_0\$0\(\{(\d+)\}", found[0])
        require(tuple((int(o), n, int(s)) for o, n, s in actual) == expected, "Join nested field ABI changed")
    # Alias is the genuine POLL event type, not a cast between lookalike layouts.
    require("S:Fmac_join$input$0_0$0({48}ST__00000009:S),F,0,0\n" in debug
            and "S:Fmac_join$staged$0_0$0({645}ST__00000021:S),F,0,0\n" in debug,
            "Join real event/staging type changed")
    types = {"ctx": "{2}DX,ST__00000021:S", "tx": "{2}DX,ST__00000004:S",
             "request": "{2}DX,ST__00000018:S", "now": "{4}SL:U",
             "event": "{2}DX,ST__00000009:S", "action": "{2}DX,ST__00000020:S",
             "record": "{2}DX,ST__00000019:S"}
    for name, args in (("init", ("ctx", "now")), ("start", ("ctx", "tx", "request", "now")),
                       ("step", ("ctx", "tx", "now", "event", "action")),
                       ("take", ("ctx", "record")), ("release", ("ctx", "tx"))):
        occupied = set()
        for index, arg in enumerate(args, 1):
            found = re.findall(r"^S:(Lmac_join\.mac_join_" + name + r"\$" + arg
                               + r"\$[^(]+)\(" + re.escape(types[arg]) + r"\),F,0,0$", debug, re.MULTILINE)
            require(len(found) == 1, "Join explicit pointer/parameter ABI changed")
            address = cdb_address(debug, "L:" + found[0])
            width = int(re.match(r"\{(\d+)\}", types[arg])[1])
            extent = set(range(address, address + width))
            require(not occupied & extent and extent <= span(debug, "mac_join"), "Join argument aliases caller/arguments")
            occupied.update(extent)
            if index > 1:
                require(symbols[f"_mac_join_{name}_PARM_{index}"] == address, "Join parameter map identity changed")

    def calls(key):
        lo, hi = (cdb_address(debug, "L:" + prefix + key + "$0$0") for prefix in ("", "X"))
        return {int.from_bytes(data[1:], "big") for a, data in instructions.items()
                if lo <= a <= hi and len(data) == 3 and data[0] == 0x12}

    for module, names in PUBLIC.items():
        for name in names:
            address = symbols["_" + name]
            require(address == cdb_address(debug, f"L:G${name}$0$0")
                    == label(listings[module].decode("ascii"), name) and address in instructions,
                    "Join actual public function address changed")
            require(f"F:G${name}$0_0$0({{2}}DF,SC:U),Z,0,0,0,0,0\n" in debug, "Join public result ABI changed")
    for name in ("reached", "abort_attempt", "fault", "issue", "window", "receipt", "finish_window"):
        result = "SC:U" if name == "reached" else "SV:S"
        storage = "Z" if name == "finish_window" else "C"
        require(f"F:Fmac_join${name}$0_0$0({{2}}DF,{result}),{storage},0,0,0,0,0\n" in debug,
                "Join private helper return ABI changed")
        require(label(listings["mac_join"].decode("ascii"), name) ==
                cdb_address(debug, f"L:Fmac_join${name}$0$0"), "Join private helper instructions changed")
    for key, targets in (
        ("G$mac_join_init", ("mac_poll_init", "mac_association_init")),
        ("G$mac_join_start", ("mac_frame_encode",)),
        ("G$mac_join_step", ("mac_tx_submit", "mac_tx_step", "mac_tx_release", "mac_poll_start", "mac_poll_step_rx", "mac_poll_release")),
        ("Fmac_join$window", ("mac_association_start",)),
        ("Fmac_join$receipt", ("mac_poll_take", "mac_association_step_rx")),
        ("Fmac_join$finish_window", ("mac_association_step_rx", "mac_association_take")),
        ("G$mac_association_step_rx", ("mac_frame_decode_profile", "mac_command_decode")),
        ("G$mac_tx_submit", ("mac_frame_decode",)),
        ("G$mac_tx_step", ("mac_frame_decode",)),
        ("G$mac_poll_start", ("mac_frame_encode",)),
        ("Fmac_poll$step", ("mac_tx_submit", "mac_frame_decode_profile")),
        ("G$mac_poll_release", ("mac_tx_release",)),
        ("G$main", ("mac_join_init", "mac_join_start", "mac_join_step", "mac_join_take", "mac_join_release", "mac_tx_init")),
    ):
        require(all(symbols["_" + n] in calls(key) for n in targets), f"Join genuine calls missing: {key}")
    require(cdb_address(debug, "L:Fmac_join$finish_window$0$0") in calls("Fmac_join$receipt"),
            "Join receipt delays taking terminal Response metadata until cleanup")
    require(symbols["_mac_tx_init"] not in calls("G$mac_join_init") | calls("G$mac_join_start") | calls("G$mac_join_step"),
            "Join resets the device-wide transmitter")
    if number not in (5, 6, 7, 8, 10, 16, 18, 19, 21):
        require(symbols["_mac_tx_step"] in calls("G$main"), "Join POLL caller bypasses its real TX grant")
    cpu_only(instructions)
    require(symbols["_main"] == 30526 and symbols["_mac_join_done"] == done
            and code_bytes(image, size)[done:done+3] == b"\0\x80\xfe"
            and cdb_address(debug, "L:XG$main$0$0") == done+3, "Join checkpoint ABI changed")
    return allocated


def expected_record(n):
    """Independent wire/time oracle; never infer correctness from the observed record."""
    request_only = n in (5, 6, 7, 8, 10, 16, 18, 19, 21)
    early = n in (18, 19)
    reply = n in (0, 1, 2, 3, 4, 9, 12, 15, 20)
    ack = 0 if early or n in (5, 16) else 0xfffff860 if n == 4 else 208
    poll, association = bytes(143), bytes(27)
    observation = 0
    if not request_only:
        protocol = 3 if n == 11 else 2
        cause = 6 if n == 11 else 4 if n == 14 else 5 if n == 17 else 3
        stamp = {4: 358, 11: 2716, 12: 2430, 13: 2530, 14: 2230, 15: 127284, 17: 2530}.get(n, 2330)
        body = b""
        if reply or n == 13:
            body = bytes.fromhex("73 cc 91 34 12 01 02 03 04 05 06 07 08 11 12 13 14 15 16 17 18 02 78 56 00")
            if n in (1, 2): body = body[:-3] + bytes((255, 255, n))
            if n == 3: body = body[:-3] + b"\xfe\xff\0"
            if n == 4:
                body = bytes.fromhex("33 cc 91 ff ff 01 02 03 04 05 06 07 08 34 12 11 12 13 14 15 16 17 18 02 78 56 00")
        poll = struct.pack("<III6B125s", 1, 1, stamp, protocol, cause, len(body),
                           (23 if n == 4 else 21) if body else 0, 4 if body else 0,
                           int(bool(body) and n != 4), body)
        if reply:
            status = n if n in (1, 2) else 0
            address = 0xffff if status else 0xfffe if n == 3 else 0x5678
            association = struct.pack("<IIIH8s5B", 1, 1, stamp, address,
                bytes(range(17, 25)), 1, status, 3 if status else 2 if n == 3 else 1, int(n != 4), 0x91)
            observation = 1
        elif n in (13, 17):
            association = struct.pack("<IIIH8s5B", 1, 1, 2531 if n == 13 else 2630, 0, bytes(8), 6, 0, 0, 0, 0)
            observation = 6 if n == 13 else 0
    reasons = {6: 1, 7: 2, 8: 6, 9: 9, 10: 4, 18: 1, 19: 3, 20: 5, 21: 1}
    stages = {6: 3, 7: 3, 8: 2, 9: 5, 10: 3, 18: 1, 19: 1, 20: 4, 21: 3}
    reason = reasons.get(n, 0)
    result = 2 if n in (5, 16) else 3 if request_only else 1
    stage = 2 if n in (5, 16) else stages[n] if request_only else 4
    tail = struct.pack("<III12B", 1, 1, ack, result, stage, reason, stages.get(n, 0),
        9 if n == 9 else 7 if n == 20 else 0, 0 if early else 4 if n == 5 else 3 if n == 16 else 10 if n == 8 else 1,
        255 if early else 0, 255 if request_only else 0, 4 if n == 20 else 0, 9 if n == 20 else 0,
        255 if request_only or n in (11, 14) else 0, observation)
    return poll + association + tail


def check_result(n, ram, iram, sfr, initial_sfr, allocated):
    faulted = n in (8, 9, 10, 20)
    early = n in (18, 19)
    request_only = n in (5, 6, 7, 8, 10, 16, 18, 19, 21)
    require(ram[0x1e00:0x1e08] == b"MJN1\x01\x08\0" + bytes((n,)), "Join target did not complete its real script")
    require(ram[3085] == CALLS[n] and ram[3087] == (2 if faulted else 0)
            and ram[3090] == (3 if n in (12, 21) else 0), "Join skipped event/return/observation checks")
    expected = expected_record(n)
    require(ram[2870:3064] == expected, f"Join {n} complete retained record differs from independent wire/time oracle")
    require(ram[1900+382:1900+576] == expected, "Join take/public retained record mismatch")
    require(ram[1900+637] == (7 if faulted else 0) and ram[1900+641] == 1,
            "Join terminal/taken lifecycle changed")
    require(int.from_bytes(ram[1900+602:1900+604], "little") == (2588 if faulted else 0),
            "Join released an uncertain lease or retained a completed lease")
    require(ram[1900+643] == (0 if faulted else 1) and ram[1900+644] == int(faulted),
            "Join restoration/uncertainty boundary changed")
    require(ram[2588+155] == (7 if n == 8 else 6 if n == 20 else 0)
            and ram[2588+156] == (255 if early else 0 if request_only else 1)
            and int.from_bytes(ram[2588+145:2588+149], "little") == (0 if early else 1 if request_only else 2),
            "Join reset/replaced the genuine device-wide DSN owner")
    if not early:
        require(ram[1900+576:1900+601] == (bytes.fromhex(
            "23 c8 00 34 12 44 33 ff ff 01 02 03 04 05 06 07 08 01 88") + bytes(6) if n == 4 else
            bytes.fromhex("23 cc 00 34 12 11 12 13 14 15 16 17 18 ff ff 01 02 03 04 05 06 07 08 01 88")),
            "Join canonical Association Request changed")
    if not request_only:
        expected_tx = (bytes.fromhex("63 c8 00 34 12 44 33 01 02 03 04 05 06 07 08 04") if n == 4 else
                       bytes.fromhex("63 cc 00 34 12 11 12 13 14 15 16 17 18 01 02 03 04 05 06 07 08 04"))
        require(ram[2588:2588+len(expected_tx)] == expected_tx, "Join Data Request is not extended-source/current DSN")
    require(all(v == 0xa5 for a, v in enumerate(ram) if a not in allocated), "Join unallocated/alias/status-tail write")
    require(iram[0x7d:] == b"\xc7" * 131 and sfr[1] == 0x50, "Join SP7C guard/checkpoint unwind failed")
    require(all(sfr[a-0x80] == 0 for a in (0xa8, 0xb8, 0x9a)), "Join enabled interrupts")
    require(all(v == initial_sfr[i] for i, v in enumerate(sfr) if i+0x80 not in (0x81, 0x82, 0x83, 0xd0, 0xe0, 0xf0)),
            "Join changed non-CPU SFRs")


def check_peak(n, text):
    values = re.findall(r"Max value of stack pointer=\s*0x([0-9a-fA-F]+)", text)
    require(len(values) == 1 and int(values[0], 16) == PEAKS[n] <= 0x7c, "Join uninterrupted SP peak changed")


def negatives(n, args):
    case, count = unittest.TestCase(), 0

    def reject(**change):
        nonlocal count
        with case.assertRaises(ValueError):
            verify(n, **(args | change))
        count += 1

    im, sym, raw = (args[k] for k in ("image", "symbols", "debug_raw"))
    size, done, _ = PINS[n]
    for a in (0, done, size-1, sym["___memcpy"], *(sym["_" + f] for fs in PUBLIC.values() for f in fs)):
        reject(image=im | {a: im[a] ^ 1})
    reject(image={a: b for a, b in im.items() if a != size-1})
    reject(image=im | {size: 0})
    for k in (*RUNTIME, "s_SSEG", "l_XSEG", "_mac_join_result", "_mac_join_done"):
        reject(symbols=sym | {k: sym[k]+1})
    reject(symbols=sym | {"unreviewed_extra_symbol": 0})
    for prefix in (b"F:", b"S:", b"L:", b"T:", b"F:Fmac_join", b"S:Fmac_join", b"S:Lmac_join",
                   b"L:Lmac_join", b"S:Ftest_mac_join", b"L:C$test_mac_join", b"F:Fmac_poll",
                   b"T:Fmac_poll", b"F:G$mac_association_step_rx", b"S:Lmac_tx"):
        lines = [line for line in raw.splitlines(keepends=True) if line.startswith(prefix)]
        require(lines, "Join raw metadata negative prerequisite missing")
        reject(debug_raw=raw.replace(lines[0], b"", 1)); reject(debug_raw=raw + lines[0])
    reject(debug_raw=raw.replace(b"\n", b"\r\n"))
    reject(debug_raw=raw + b"\xff")
    reject(memory=args["memory"].replace("175 bytes available", "174 bytes available"))
    reject(memory=args["memory"].replace("3117", "3118"))
    for m, listing in args["listings"].items():
        lines = listing.splitlines(keepends=True)
        indexes = [i for i, line in enumerate(lines) if records(line.decode("ascii"))]
        first, second = indexes[:2]
        for operation in ("delete", "duplicate", "reorder", "mutate"):
            changed = lines.copy()
            if operation == "delete": del changed[first]
            elif operation == "duplicate": changed.insert(first, lines[first])
            elif operation == "reorder": changed[first], changed[second] = changed[second], changed[first]
            else: changed[first] = changed[first].replace(b" ", b"\t", 1)
            reject(listings=args["listings"] | {m: b"".join(changed)})
        reject(objects=args["objects"] | {m: ""})
        reject(objects=args["objects"] | {m: args["objects"][m].replace("A XSEG size ", "A ISEG size ", 1)})
    reject(listings={m: v for m, v in args["listings"].items() if m != "mac_join"})
    return count


def load(directory, n):
    stem = f"mac_join_{n}_test"
    path = directory / (stem + ".ihx")
    modules = MODULES + (stem,)
    return path, dict(image=parse_ihex(path.read_text(encoding="ascii")),
        symbols=parse_symbols(path.with_suffix(".map").read_text(encoding="ascii")),
        debug_raw=path.with_suffix(".cdb").read_bytes(), memory=path.with_suffix(".mem").read_text(encoding="ascii"),
        listings={m: (directory / f"{stem}.{m}.rst").read_bytes() for m in modules},
        objects={m: (directory / f"{m}.rel").read_text(encoding="ascii") for m in modules})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--simulator", default="s51")
    args = parser.parse_args()
    test = unittest.TestCase()
    check_alias(args.simulator)
    with test.assertRaises(ValueError): check_alias(args.simulator, False)
    for op in (b"\xe5\x90", b"\x75\xa8\x01", b"\x85\x90\xe0", b"\xd2\xaf"):
        with test.assertRaises(ValueError): cpu_only({0: op})
    for n, (size, done, _) in enumerate(PINS):
        path, artifacts = load(args.output, n)
        allocated = verify(n, **artifacts)
        count = negatives(n, artifacts)
        setup = [ALIAS, "fill xram 0 0x1eff 0xa5",
                 "set memory sfr 0xa8 0", "set memory sfr 0xb8 0", "set memory sfr 0x9a 0"]
        text = simulate(args.simulator, setup + ["run 0 0x773e", "fill iram 0x7d 0xff 0xc7"]
            + snapshot_commands(1) + [f"run 0x773e {done:#x}"] + snapshot_commands(5), path)
        initial = snapshot(text, 1)[2]
        check_pc(section(text, 5), done)
        ram, iram, sfr = snapshot(text, 5)
        check_result(n, ram, iram, sfr, initial, allocated)
        full = simulate(args.simulator, setup + [f"run 0 {done:#x}"] + snapshot_commands(1), path)
        check_pc(section(full, 1), done); check_peak(n, section(full, 1))
        for region, address in ((0, 0x1e00), (0, 0x1e06), (0, 0x1e3f), (0, 0x1dff), (0, 3117),
                                (0, 3085), (0, 2870), (0, 1900+637), (0, 2588+156), (1, 0x7d), (2, 1), (2, 0x10)):
            bad = [bytearray(ram), bytearray(iram), bytearray(sfr)]
            bad[region][address] ^= 1
            with test.assertRaises(ValueError): check_result(n, *bad, initial, allocated)
        for bad in ("", "Max value of stack pointer= 0x7d", "Max value of stack pointer= 0x50"):
            with test.assertRaises(ValueError): check_peak(n, bad)
        print(f"Join {n}: {size}/{CODE_BUDGET} CODE, 3117+64/{XDATA_BUDGET} XDATA, "
              f"{CALLS[n]} scripted events, full SP{PEAKS[n]:02X}/cap7C, checkpoint50; "
              f"{count} artifact +12 guard +3 peak negatives PASS.")
    print("Join: 22 complete real compositions, 1 alias +4 MMIO negatives PASS; no physical adapter or MLME conformance claim.")


if __name__ == "__main__":
    main()
