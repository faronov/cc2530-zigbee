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
from boot_mac_poll import returning_work
from boot_zcl_basic import canonical, sha
from boot_zcl_temperature import cpu_only
from verify_firmware import cdb_address, code_bytes, parse_ihex, parse_symbols, require


MODULES = ("mac_frame", "mac_tx", "mac_association", "mac_poll", "mac_join")
CODE_BUDGET, XDATA_BUDGET = 32768, 3200
# One digest per complete artifact manifest: ALL CODE, raw CDB bytes, complete
# parsed map, ALL ordered object-area declarations and six full raw snapshots.
# Includes the ordinary static-shadow branch and the codec lowering reviewed
# in boot_mac_tx. The CC2530_MAC_LINK refresh changed no executable or
# allocation: all 22 IHX, parsed maps and memory reports are byte-identical;
# only source-line CDB/listing/object records moved. Stack start/checkpoint
# remain 51/50; PEAKS below are actual uninterrupted observations for all 22
# images. Compile-only WORKSPACE_PINS changed only in line records (adb same).
PINS = (
    (32641, 31983, "dca3f1f016c61dc28b044d060f2bcba850457fbb20612167b6017e34a7221a00"),
    (32642, 31984, "4740d9a8da1705beab03beb56fc41f866b0f95c71bb8c81e5f06426214676cea"),
    (32642, 31984, "8576fee4836e6bfb0d7529a9dc0c6bb53bfeddadc928c650fb450b95c965b0ed"),
    (32642, 31984, "8132b8c523e228a49e2c57ee948da37df6c077b076c67c0073af5fa05abf7aaa"),
    (32642, 31982, "2f50305ffdbcecefdc73f82a5501ec883e2515cb6df614fa0b53cadbd1516465"),
    (32285, 31627, "89a6ef89179f18c1b19fab03d084b8c4c2e7257a9f3dc9cd275bc505295b303a"),
    (32273, 31615, "946a23ec4aa99b5d2341ec0e14fe7048cf28c7050695414929613b2396fd29f1"),
    (32267, 31609, "11cd7274b0f547c02f55c083c45093de9360ca428a194474ce356d6c31cad6cd"),
    (32311, 31653, "0dacb539466a69d27e005878358098d55aba675d146c2545568834472b9021a2"),
    (32559, 31901, "d1bcd9cbb2d7012283627c67bf084c862e123f5f5cec2b37805bee1a10cabe7f"),
    (32308, 31650, "26206cd04947e3750a2b97d0c8c194508539fec2d4b435d140ad691288b03ba7"),
    (32548, 31890, "0c7096d70e452f20ca5a1239e5005012c404a442650acd7df945b550ad463122"),
    (32684, 32026, "65709d2aa0d56ec1091ff1096ccabcef56a9f603693d7244d62a2b6f647cb5a1"),
    (32678, 32020, "6e438e1879c20440ca37424e66ec2d5a4c9ef8b8eac497380833e6284f026a10"),
    (32544, 31886, "948df46dd6125acc8e24768385842f5625495a449bd743abc474ab908e2ed0cf"),
    (32637, 31979, "3877f4301e326c6c0a906706ee76fd355a28baa4cadb236b88add65f6f29e79d"),
    (32286, 31628, "e911da2783e1845d01c5da3fa8a01effe260e0fd5afc0c1db10a61761de51ebf"),
    (32576, 31918, "ce341dca3914a876382f217bad2a895e44ab55280e3c7abbc0a8524c5c8109c7"),
    (32281, 31623, "64c35724e9f4ef854c2af7d80aea2bcc77c36d529305b9621b9391ec31b1c81a"),
    (32267, 31609, "1faaef2b42ba01dd5aeef02dbb24e1858e7e00473816168a54c31d3c2033c6cf"),
    (32701, 32043, "c34979f1025bb1a21635af4c9577a59b2483e4aa54301a8ac79f379e4f33d380"),
    (32373, 31715, "5193e72b92c9c020a597007837c89d6f82f32978753d349ae37d444797382725"),
)
CALLS = (15, 15, 15, 15, 15, 21, 7, 7, 5, 15, 6, 29, 16, 15, 14, 15, 16, 15, 2, 2, 15, 9)
PEAKS = (0x73, 0x73, 0x73, 0x73, 0x73, 0x6d, 0x6d, 0x6d, 0x6d, 0x73, 0x6d,
         0x71, 0x73, 0x71, 0x71, 0x73, 0x6d, 0x71, 0x65, 0x65, 0x73, 0x6d)
# CSEG, XSEG, DSEG, OSEG, BSEG bits; no allocation is an extra IRAM alias pool.
OBJECTS = ((7093, 216, 12, 10, 1), (5650, 191, 8, 0, 3), (2839, 76, 27, 0, 1),
           (7614, 299, 5, 0, 3), (7170, 885, 5, 0, 1))
# Compile-only external-shadow evidence, NOT an allocation/link/simulation
# proof for the full composition. All raw files are pinned; only the .rel
# first-line build-path comment is excluded, never any object/relocation data.
WORKSPACE_PINS = {
    "ordinary": {
        "rel": "b95f9cba7a1dc6d40652c778f90c82675e236b48af746b317d5c22546b72b7ae",
        "asm": "9c15e4c4b5ecdbe5bceb9ddef7f59a8173802820c011d29f0a1cd9134e72b01c",
        "lst": "552ea9d1c31b6cbe9108cbd4a1af8489c5870ea33d6576e52c4eb152592d9b99",
        "adb": "48445e14480dc1155fec670957c9253f456c9fe9b2fcaf0d35030fbac1ef127c",
    },
    "external": {
        "rel": "da798e7d6c96c4759d8328e688127a7296e682f6c2483987b23ce5eac5a0d0ce",
        "asm": "fc42dfb9fbff5ac8ab8f78fe46ebe67809ef5a4c45c64886c8085afb88710b94",
        "lst": "c5db30486f006e32bf0a1d0be708ab98bd10ff2848e61e6a63504c31c00ba976",
        "adb": "52be57f03a919c0dd1ade88913153376f513d6e3512e38561314b67558b43978",
    },
}
CALLER = {
    "join": (1667, 645), "config": (2312, 43), "tx": (2355, 168), "event": (2523, 48),
    "action": (2571, 44), "radio": (2615, 22), "record": (2637, 194), "ack": (2831, 3),
    "source_kind": (2834, 1), "frames": (2835, 1), "now": (2836, 4), "boundary": (2840, 4),
    "duration": (2844, 2), "supplied": (2846, 3), "operation": (2849, 1), "phase": (2850, 1),
    "transmitter": (2851, 1), "calls": (2852, 1), "failure": (2853, 1),
    "release_result": (2854, 1), "grant": (2855, 2), "seen": (2857, 1),
}
RUNTIME = {"___memcpy_PARM_2": 2858, "___memcpy_PARM_3": 2861, "_memset_PARM_2": 2866,
           "_memset_PARM_3": 2867, "__gptrput_PARM_2": 2869, "__mulint_PARM_2": 2870,
           "__mullong_PARM_2": 2872, "_memcmp_PARM_2": 2876, "_memcmp_PARM_3": 2879}
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
    22: ((0, "start", 97), (0, "step", 165)),
    23: ((0, "proposed", 43), (43, "header", 26), (69, "command", 2),
         (71, "body", 25), (96, "length", 1)),
    24: ((0, "input", 48), (48, "output", 44), (92, "pa", 25), (117, "nested", 48)),
    25: ((0, "pe", 48), (0, "ar", 30), (0, "ae", 20), (0, "pr", 35)),
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
    returning_work(debug)
    allocated = verify_component_layout(image, symbols, debug, memory, "mac_join_result",
        tuple(m + ".c" for m in MODULES) + ("test_mac_join.c",), xdata_budget=XDATA_BUDGET)
    require(symbols["s_XSEG"] == 0 and symbols["l_XSEG"] == 2884 and symbols["s_SSEG"] == 0x51,
            "Join exact XDATA/IRAM allocation changed")
    require(re.findall(r"EXTERNAL RAM\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s+(\d+)\s+(\d+)", memory) ==
            [("0x0000", "0x0b43", "2884", "7680")], "Join ordinary RAM accounting changed")
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
    require(offset == 1667, "Join private/caller boundary changed")
    for name, (address, length) in CALLER.items():
        key = f"Ftest_mac_join${name}$0_0$0"
        require(cdb_address(debug, "L:" + key) == address and f"S:{key}({{{length}}}" in debug,
                "Join caller field/storage ABI changed")
    require(span(debug, "test_mac_join") == set(range(1667, 2858)), "Join unaccounted caller/private allocation")
    require(all(symbols[k] == v for k, v in RUNTIME.items()), "Join compiler/libc scratch overlaps caller")
    require(allocated == set(range(2884)) | set(range(0x1e00, 0x1e08)), "Join unaccounted allocation/status write region")
    for index, expected in FIELDS.items():
        found = re.findall(rf"^T:Fmac_join\$__{index:08d}\[(.*)\]$", debug, re.MULTILINE)
        require(len(found) == 1, "Join missing/duplicate field declaration")
        actual = re.findall(r"\(\{(\d+)\}S:S\$([^$]+)\$0_0\$0\(\{(\d+)\}", found[0])
        require(tuple((int(o), n, int(s)) for o, n, s in actual) == expected, "Join nested field ABI changed")
    # Alias is the genuine POLL event type, not a cast between lookalike layouts.
    require("S:Fmac_join$work$0_0$0({165}ST__00000022:S),F,0,0\n" in debug
            and "({0}S:S$input$0_0$0({48}ST__00000009:S),Z,0,0)" in debug
            and "({0}S:S$pe$0_0$0({48}ST__00000009:S),Z,0,0)" in debug
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
    require(symbols["_main"] == 30464 and symbols["_mac_join_done"] == done
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
    require(ram[2852] == CALLS[n] and ram[2854] == (2 if faulted else 0)
            and ram[2857] == (3 if n in (12, 21) else 0), "Join skipped event/return/observation checks")
    expected = expected_record(n)
    require(ram[2637:2831] == expected, f"Join {n} complete retained record differs from independent wire/time oracle")
    require(ram[1667+382:1667+576] == expected, "Join take/public retained record mismatch")
    require(ram[1667+637] == (7 if faulted else 0) and ram[1667+641] == 1,
            "Join terminal/taken lifecycle changed")
    require(int.from_bytes(ram[1667+602:1667+604], "little") == (2355 if faulted else 0),
            "Join released an uncertain lease or retained a completed lease")
    require(ram[1667+643] == (0 if faulted else 1) and ram[1667+644] == int(faulted),
            "Join restoration/uncertainty boundary changed")
    require(ram[2355+155] == (7 if n == 8 else 6 if n == 20 else 0)
            and ram[2355+156] == (255 if early else 0 if request_only else 1)
            and int.from_bytes(ram[2355+145:2355+149], "little") == (0 if early else 1 if request_only else 2),
            "Join reset/replaced the genuine device-wide DSN owner")
    if not early:
        require(ram[1667+576:1667+601] == (bytes.fromhex(
            "23 c8 00 34 12 44 33 ff ff 01 02 03 04 05 06 07 08 01 88") + bytes(6) if n == 4 else
            bytes.fromhex("23 cc 00 34 12 11 12 13 14 15 16 17 18 ff ff 01 02 03 04 05 06 07 08 01 88")),
            "Join canonical Association Request changed")
    if not request_only:
        expected_tx = (bytes.fromhex("63 c8 00 34 12 44 33 01 02 03 04 05 06 07 08 04") if n == 4 else
                       bytes.fromhex("63 cc 00 34 12 11 12 13 14 15 16 17 18 01 02 03 04 05 06 07 08 04"))
        require(ram[2355:2355+len(expected_tx)] == expected_tx, "Join Data Request is not extended-source/current DSN")
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
    reject(memory=args["memory"].replace("2884", "2885"))
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


def workspace_artifacts(directory):
    return {suffix: (directory / f"mac_join.{suffix}").read_bytes()
            for suffix in ("rel", "asm", "lst", "adb")}


def verify_workspace(ordinary, external):
    """Compare real compiler outputs; never create/patch an object or bind RAM."""
    for profile, files in (("ordinary", ordinary), ("external", external)):
        require(tuple(files) == tuple(WORKSPACE_PINS[profile]), "Workspace artifact inventory changed")
        for suffix, digest in WORKSPACE_PINS[profile].items():
            raw = files[suffix]
            require(isinstance(raw, bytes), "Workspace artifacts must be raw bytes")
            if suffix == "rel":
                first, newline, raw = raw.partition(b"\n")
                require(newline and first.startswith(b";!FILE ") and len(first) > 8
                        and all(32 <= b < 127 for b in first), "Workspace object path comment malformed")
            require(sha(raw) == digest, f"Workspace complete {profile} {suffix} identity changed")

    old = [(n, int(s, 16), f, a) for n, s, f, a in areas(ordinary["rel"].decode("ascii"))]
    new = [(n, int(s, 16), f, a) for n, s, f, a in areas(external["rel"].decode("ascii"))]
    require([entry for entry in old if entry[0] == "XSEG"] == [("XSEG", 885, "40", "0")]
            and new == [(n, s - 645 if n == "XSEG" else s, f, a) for n, s, f, a in old],
            "Workspace changes more than the complete 645-byte XDATA allocation")
    require(re.findall(rb"^S _mac_join_staged (Ref|Def)([0-9A-F]+)$", external["rel"], re.M)
            == [(b"Ref", b"000000")], "Workspace is not an unresolved external binding obligation")

    # All types, parameters, locals, returns and qualifiers must stay identical.
    # The only raw compiler metadata change is this complete object's linkage.
    local = b"S:Fmac_join$staged$0_0$0({645}ST__00000021:S),F,0,0\n"
    exported = b"S:G$mac_join_staged$0_0$0({645}ST__00000021:S),F,0,0\n"
    require(ordinary["adb"].count(local) == 1
            and ordinary["adb"].replace(local, exported) == external["adb"],
            "Workspace full type/public/private/parameter ABI differs")

    # Compare the ENTIRE emitted assembly, not just selected mnemonic lines.
    # Precisely one real allocation disappears; every instruction, argument,
    # label, other allocation and helper remains, with the same static-symbol
    # operands. The separately pinned .rel/.lst bind actual assembled bytes
    # and relocation records. Nothing here resolves the external address.
    allocation = b"Fmac_join$staged$0_0$0==.\n_staged:\n\t.ds 645\n"
    require(ordinary["asm"].count(allocation) == 1, "Workspace ordinary full shadow allocation missing")
    expected = ordinary["asm"].replace(allocation, b"").replace(b"_staged", b"_mac_join_staged")
    require(expected == external["asm"], "Workspace introduces pointer indirection or different instructions/storage")
    require(len(re.findall(rb"^\t[a-z][a-z0-9]*(?:\t[^\n]*)?$", expected, re.M)) == 4640
            and expected.count(b"_mac_join_staged") == 235, "Workspace instruction/direct-symbol inventory changed")


def workspace_negatives(ordinary, external):
    case, count = unittest.TestCase(), 0

    def reject(which, suffix, value):
        nonlocal count
        changed = dict(ordinary if which == 0 else external)
        changed[suffix] = value
        with case.assertRaises(ValueError):
            verify_workspace(changed if which == 0 else ordinary, changed if which == 1 else external)
        count += 1

    for which, files in enumerate((ordinary, external)):
        for suffix, raw in files.items():
            bad = bytearray(raw); bad[len(bad) // 2] ^= 1
            for changed in (b"", raw[:-1], bytes(bad)):
                reject(which, suffix, changed)
    reject(1, "rel", external["rel"].replace(b"S _mac_join_staged Ref", b"S _mac_join_staged Def", 1))
    reject(1, "rel", external["rel"].replace(b"A XSEG size F0", b"A XSEG size 375", 1))
    reject(1, "rel", external["rel"].replace(b";!FILE ", b";OTHER ", 1))
    reject(1, "adb", external["adb"].replace(b"{645}ST__00000021", b"{2}DX,ST__00000021", 1))
    reject(1, "asm", external["asm"].replace(b"_mac_join_staged", b"_work", 1))
    reject(1, "asm", ordinary["asm"])
    reject(1, "asm", external["asm"].replace(b"\t.area XSEG    (XDATA)", b"\t.area XSEG    (XDATA)\n\t.ds 645", 1))
    with case.assertRaises(ValueError):
        verify_workspace(ordinary, ordinary)
    return count + 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--simulator", default="s51")
    parser.add_argument("--check-workspace-object", type=Path, metavar="DIRECTORY",
                        help="explicit compile-only comparison with --output's ordinary mac_join object; no simulation")
    args = parser.parse_args()
    if args.check_workspace_object is not None:
        ordinary = workspace_artifacts(args.output)
        external = workspace_artifacts(args.check_workspace_object)
        verify_workspace(ordinary, external)
        count = workspace_negatives(ordinary, external)
        print(f"Join workspace object: XSEG885->240, CSEG7170/DSEG5/BSEG1 unchanged; "
              f"4640 identical instructions, 235 direct-symbol operands, {count} artifact negatives PASS. "
              "Compile-only: external 645-byte backing, full-profile link/lifetimes/stack/alias execution NOT proved.")
        return
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
        text = simulate(args.simulator, setup + ["run 0 0x7700", "fill iram 0x7d 0xff 0xc7"]
            + snapshot_commands(1) + [f"run 0x7700 {done:#x}"] + snapshot_commands(5), path)
        initial = snapshot(text, 1)[2]
        check_pc(section(text, 5), done)
        ram, iram, sfr = snapshot(text, 5)
        check_result(n, ram, iram, sfr, initial, allocated)
        full = simulate(args.simulator, setup + [f"run 0 {done:#x}"] + snapshot_commands(1), path)
        check_pc(section(full, 1), done); check_peak(n, section(full, 1))
        for region, address in ((0, 0x1e00), (0, 0x1e06), (0, 0x1e3f), (0, 0x1dff), (0, 2884),
                                (0, 2852), (0, 2637), (0, 1667+637), (0, 2355+156), (1, 0x7d), (2, 1), (2, 0x10)):
            bad = [bytearray(ram), bytearray(iram), bytearray(sfr)]
            bad[region][address] ^= 1
            with test.assertRaises(ValueError): check_result(n, *bad, initial, allocated)
        for bad in ("", "Max value of stack pointer= 0x7d", "Max value of stack pointer= 0x50"):
            with test.assertRaises(ValueError): check_peak(n, bad)
        print(f"Join {n}: {size}/{CODE_BUDGET} CODE, 2884+64/{XDATA_BUDGET} XDATA, "
              f"{CALLS[n]} scripted events, full SP{PEAKS[n]:02X}/cap7C, checkpoint50; "
              f"{count} artifact +12 guard +3 peak negatives PASS.")
    print("Join: 22 complete real compositions, 1 alias +4 MMIO negatives PASS; no physical adapter or MLME conformance claim.")


if __name__ == "__main__":
    main()
