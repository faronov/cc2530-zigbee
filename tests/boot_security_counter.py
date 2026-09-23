#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Real counter/journal/flash/RAM composition. Synthetic media, never flash."""
import argparse
import hashlib
import json
from pathlib import Path
import re

import boot_nv_record as journal
from boot_flash_write import MODULES as FLASH_MODULES
from boot_image import ALIAS, check_alias, check_pc, marker, memory_dump, simulate, snapshot_commands, verify_component_layout
from boot_nwk_candidates import listing_metrics, records
from boot_zdo_node import rejected
from verify_firmware import cdb_address, code_bytes, parse_ihex, parse_symbols, peripheral_accesses, require

MODULES = ("flash_exec", "flash", "flash_write", "nv_record", "security_counter", "security_counter_test")
SIZE, XDATA, STACK = 9941, 1120, 0x4a
CODE_BUDGET, XDATA_BUDGET = 12288, 1280
CODE_SHA = "466912c409a085aeaa4a8adb88a636f7e6bbad92f6f4dd20f971dd6bcf1f6618"
CDB_SHA = "d3c5407af8f72baef24c865e1d1c71ebb8726630f66ff6ce992f72935789f3a3"
MAP_SHA = "116dfd4c08eb15dfe3806d04826712c617b48ec5b9abcec49afce0c0fbeb2e70"
MEM_SHA = "83ae5938cdad906ae4f712636d3c4bc9d117a41b472963f3253a00458ac16e73"
LISTINGS = dict(zip(MODULES, (
    ("bc959ef00d66fb516eeed570ad704ec146d8f7a2bd924adf0220302b7988e298", (661, 1176, "211c8531b18375463aba8749f024f76b705aafc128226106e2973c6413a69c5e")),
    ("c431531677f617a045afd6b59d5ff38bd54c4568eeed754606a01e0286124bde", (416, 766, "186fbbf903d6e033e258a92d0f44cd921fcb73d82faca5ce4c5d63ec391022c6")),
    ("2a1b23f452a57db0af0f229e01f8786bbd48c98611ba1db77125eb79ce95988f", (646, 1095, "7aa7bf21cd8e4b189d44cf0f84f8eb2006a22ca0d73e94484e1821477cb75ba3")),
    ("a4bec338648583babd4d8c879a0ac7f435f0e33ec7f50df03b9eaec2f3dadf4c", (2204, 3694, "054a9b8d7deec81b7cd647ca9820ac412a087c6091b145d5655413e5a00db286")),
    ("9cdf3f022e786a5316a5d3da39404a4caf8f28d9f04dca4782cb407fcd5c5a28", (1691, 2746, "f1e6bf4dc76c85748309bbaf1dfccefa227ae9bdf9d4bf820e927e0a4cc90e95")),
    ("c4d4c04740304c086c82989e304091aac01f58d7377d2bc41a1c438c20440042", (209, 363, "965cdbc5c792bfc2bb81e7dcdb8e63fb2bfe3f93040eaa013e6b11881f8013c2")),
)))
OBJECTS = dict(zip(MODULES, (
    ("52ceded037af553b1058950185143bb0175dfb7a7dcd350bf41f8c02e9dec2f2", (1176, 155, 8, 0, 1)),
    ("13c294e54276d3d531379be0dc2430396fe44eff2fc236d4dcbbbe58c2346ce6", (766, 54, 4, 0, 0)),
    ("84855d5adec79f79fce76664fa582048eec372717731340771c4b7fb399d8d29", (1095, 195, 5, 0, 0)),
    ("9d80baba6a15d6cc06889220faedc10b8ab2a1938b9bdb92a6f84738811898ca", (3702, 257, 23, 4, 1)),
    ("bdddf0667a01a908f496e7b9a1325abc4a176d837b4ac72706ecb3c440875789", (2746, 329, 14, 4, 1)),
    ("7040dccdf71a2b35a6be41c55e7c8dede9b1b7e55e03285fa23d0591c726e95e", (363, 130, 4, 0, 0)),
)))
ABI = {
    "_counter_before": 9575, "_counter_done": 9835, "_main": 9837,
    "_counter_test_action": 1116, "_counter_test_aps": 1110, "_counter_test_buffer": 990,
    "_counter_test_cycle": 9575, "_counter_test_domain": 1118, "_counter_test_length": 1117,
    "_counter_test_limit": 1114, "_counter_test_nwk": 1106, "_counter_test_result": 7680,
    "_counter_test_return": 1119, "_counter_test_value": 1102,
    "_security_counter_blob": 685, "_security_counter_check": 813,
    "_security_counter_create": 8166, "_security_counter_create_PARM_2": 961,
    "_security_counter_create_PARM_3": 965, "_security_counter_create_PARM_4": 967,
    "_security_counter_create_PARM_5": 968, "_security_counter_diagnostic": 661,
    "_security_counter_open": 7750, "_security_counter_read": 9355,
    "_security_counter_read_PARM_2": 984, "_security_counter_read_PARM_3": 985,
    "_security_counter_reserved_end": 989, "_security_counter_save": 9115,
    "_security_counter_save_PARM_2": 979, "_security_counter_save_PARM_3": 980,
    "_security_counter_status": 9571, "_security_counter_take": 8595,
    "_security_counter_take_PARM_2": 974, "_security_counter_take_PARM_3": 976,
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def load(output):
    p = output / "security_counter_test.ihx"
    return (p, parse_ihex(p.read_text()), parse_symbols(p.with_suffix(".map").read_text()),
            p.with_suffix(".cdb").read_bytes(), p.with_suffix(".mem").read_bytes(),
            {m: (output / f"security_counter_test.{m}.rst").read_bytes() for m in MODULES},
            {m: (output / f"{m}.rel").read_bytes() for m in MODULES})


def verify(image, symbols, debug_raw, memory, listings, objects):
    require(isinstance(debug_raw, bytes) and sha(debug_raw) == CDB_SHA,
            "Counter complete raw CDB changed before decode")
    debug = debug_raw.decode("ascii")
    require(SIZE <= CODE_BUDGET and sha(code_bytes(image, SIZE)) == CODE_SHA, "Counter complete CODE changed")
    require(sha(json.dumps(symbols, sort_keys=True, separators=(",", ":")).encode()) == MAP_SHA,
            "Counter complete map changed")
    require(sha(memory) == MEM_SHA, "Counter complete memory accounting changed")
    allocated = verify_component_layout(image, symbols, debug, memory.decode("ascii"), "counter_test_result",
        tuple(m+".c" for m in MODULES[:-1])+("test_security_counter.c",), xdata_budget=XDATA_BUDGET)
    require(symbols["l_XSEG"] == XDATA and symbols["s_SSEG"] == STACK and symbols["s_XSEG"] == 0,
            "Counter XDATA/stack layout changed")
    require(all(symbols.get(k) == v for k, v in ABI.items()), "Counter public ABI/fence changed")
    require(set(listings) == set(objects) == set(MODULES), "Counter composition changed")
    covered, storage, decoded = set(), set(), {}
    for m in MODULES:
        text = listings[m].decode("ascii")
        require(sha(listings[m]) == LISTINGS[m][0] and listing_metrics(text) == LISTINGS[m][1],
                "Counter immediate ordered linked listing changed")
        code = records(text)
        if m in ("nv_record", "security_counter", "security_counter_test"):
            accesses = peripheral_accesses(dict(code))
            if m == "security_counter_test":
                require(accesses == [(9837, b"\x75\xa8\x00", 0xa8), (9840, b"\x75\xb8\x00", 0xb8),
                                     (9843, b"\x75\x9a\x00", 0x9a)], "Counter harness IRQ initialization changed")
            else:
                require(not accesses, "Counter/journal policy bypassed real flash APIs")
        for a, raw in code:
            span = set(range(a, a+len(raw)))
            require(not covered & span and bytes(image.get(a+i, -1) for i in range(len(raw))) == raw,
                    "Counter instructions overlap or differ from linked CODE")
            covered.update(span); decoded[a] = raw
        segment = text.split(".area XSEG    (XDATA)", 1)[1].split(".area XABS", 1)[0]
        for a, size in re.findall(r"^\s+([0-9A-F]{6})\s+\d+\s+\.ds (\d+)$", segment, re.M):
            span = set(range(int(a, 16), int(a, 16)+int(size)))
            require(span and not storage & span, "Counter XDATA allocations overlap")
            storage |= span
        obj = objects[m]
        require(obj.startswith(b";!FILE ") and sha(obj.split(b"\n", 1)[1]) == OBJECTS[m][0],
                "Counter complete relocatable object changed")
        areas = {n: int(s, 16) for n, s in re.findall(r"^A (\S+) size ([0-9A-F]+) flags \S+ addr \S+$",
                                                    obj.decode("ascii"), re.M)}
        extent = sum(s for n, s in areas.items() if n in ("HOME", "GSFINAL", "CSEG", "CONST") or n.startswith("GSINIT"))
        require((extent, *(areas.get(n, 0) for n in ("XSEG", "DSEG", "OSEG", "BSEG"))) == OBJECTS[m][1],
                "Counter object storage/CODE accounting changed")
    require(storage == set(range(XDATA)), "Counter complete private/caller allocation differs")
    require(not any("gptr" in n or n.startswith("_host_") for n in symbols),
            "Counter acquired unaccounted libc scratch or a host controller")
    for start, end, digest in FLASH_MODULES.values():
        require(sha(bytes(image[a] for a in range(start, end))) == digest,
                "Counter changed published actual flash backend instructions")
    require(sha(bytes(image[a] for a in range(0xc3f, 0x1aad))) ==
            "d7ca121e53c4f489ffbb8e67dedbbaaa4e4f95a3e5a4329eedc3e3c57d4063ac",
            "Counter journal linked code changed")
    calls = [(a, int.from_bytes(raw[1:], "big")) for a, raw in decoded.items() if raw[0] == 0x12]
    require([(a, target) for a, target in calls if 0xc3f <= a < 0x1aad and target < 0xc3f] ==
            [(journal.READ_CALL, 0x5e0), (journal.PROGRAM_CALL, 0xbf7), (0x1788, 0xbc6)],
            "Counter journal no longer uses actual reader/program/erase calls")
    require({target for a, target in calls if 0x1aad <= a < ABI["_counter_before"] and target < 0x1aad} ==
            {0x131f, 0x14d7, 0x1aa9}, "Counter bypassed journal load/replace/status")
    fields = re.findall(r"^T:Fsecurity_counter\$__00000002\[(.*)\]$", debug, re.M)
    require(len(fields) == 1, "Counter status field record missing/duplicated")
    actual = re.findall(r"\(\{(\d+)\}S:S\$([^$]+)\$0_0\$0\(\{(\d+)\}", fields[0])
    require(tuple((int(a), n, int(s)) for a, n, s in actual) ==
            ((0, "next", 8), (8, "until", 8), (16, "generation", 4), (20, "state", 1),
             (21, "result", 1), (22, "nv_result", 1), (23, "payload_length", 1)),
            "Counter status ABI fields changed")
    for name, size in (("security_counter_diagnostic", 24), ("security_counter_blob", 128),
                       ("security_counter_check", 128), ("security_counter_reserved_end", 1),
                       ("counter_test_buffer", 112), ("counter_test_value", 4)):
        require(cdb_address(debug, f"L:G${name}$0_0$0") == symbols["_"+name],
                "Counter object map/CDB address differs")
        sizes = re.findall(rf"^S:G\${name}\$[^(\n]+\(\{{(\d+)\}}", debug, re.M)
        require(sizes and all(int(n) == size for n in sizes), "Counter object extent changed")
    for function, parameter, spec in (
        ("create", "data", "{2}DX,SC:U"), ("take", "value", "{2}DX,SL:U"),
        ("save", "data", "{2}DX,SC:U"), ("read", "data", "{2}DX,SC:U"),
        ("read", "length", "{2}DX,SC:U"),
    ):
        require(re.search(rf"^S:Lsecurity_counter.security_counter_{function}\${parameter}\$[^(\n]+"
                          rf"\({re.escape(spec)}\)", debug, re.M),
                "Counter public pointer lost its XDATA ABI")
    return allocated


def body(nwk, aps, payload=b""):
    require(len(payload) <= 112, "Bad synthetic counter payload")
    return b"CTR1\x01"+bytes((len(payload), 0, 0))+nwk.to_bytes(4, "little")+aps.to_bytes(4, "little")+payload


def diagnostic(nwk, aps, until_nwk, until_aps, generation, state=2, result=0, nv=0, length=0):
    return b"".join(n.to_bytes(4, "little") for n in (nwk, aps, until_nwk, until_aps, generation))+bytes(
        (state, result, nv, length))


def read(page, result=0):
    return dict(result=result, page=page)


def write(page, generation, payload, **extra):
    return dict(action=1, result=0, page=page, generation=generation, body=payload, length=len(payload), **extra)


def op(action, diag, phases=(), **extra):
    return dict(action=action, diag=diag, journal=list(phases), result=diag[21], **extra)


def sections(text):
    parts = re.split(r"^0x2530([0-9a-f]{4})\r?\n", text, flags=re.M)
    keys = [int(parts[i], 16) for i in range(1, len(parts), 2)]
    require(len(keys) == len(set(keys)), "Counter continuation duplicated a marker")
    return dict(zip(keys, parts[2::2]))


def complete_commands(number):
    return snapshot_commands(number)+[
        marker(number+4), "dump /h xram 0x2000 0xffff", marker(number+5),
        marker(number+6), "dump /h rom 0x8000 0x9fff", marker(number+7)]


def complete_snapshot(parts, number, pc):
    check_pc(parts[number], pc)
    return (memory_dump(parts[number], 0, 0x1f00), memory_dump(parts[number+1], 0, 256),
            memory_dump(parts[number+2], 0x80, 128), memory_dump(parts[number+4], 0x2000, 0xe000),
            memory_dump(parts[number+6], 0x8000, 0x2000))


def restore(memory, pc, mapped):
    require(tuple(map(len, memory)) == (0x1f00, 256, 128, 0xe000, 0x2000),
            "Counter continuation is incomplete")
    ram, iram, sfr, extended, rom = memory
    require(sfr[0x99-128] == 0 and sfr[0x98-128] == 0 and
            not sfr[0x88-128] & 0x50 and not sfr[0xc8-128] & 4,
            "Counter continuation requires stopped synthetic UART/timers")
    require(rom == (ram+iram if mapped else b"\xa6"*0x2000),
            "Counter XMAP view differs from actual RAM/IRAM/history")
    commands = [ALIAS]
    if mapped:
        commands.extend(journal.engine.XMAP)
    else:
        for offset in range(0, len(rom), 64):
            commands.append(f"set memory rom {0x8000+offset:#x} "+" ".join(hex(b) for b in rom[offset:offset+64]))
    for space, start, data in (("xram", 0, ram), ("iram", 0, iram), ("xram", 0x2000, extended),
                               ("sfr", 0x80, sfr[:0x19]), ("sfr", 0x9a, sfr[0x1a:])):
        for offset in range(0, len(data), 64):
            commands.append(f"set memory {space} {start+offset:#x} "+" ".join(hex(b) for b in data[offset:offset+64]))
    return commands+[f"pc {pc:#x}"]


def check_continuation(actual, expected):
    require(actual == expected, "Counter continuation changed complete CPU/RAM/flash/XMAP state")


class CounterClient:
    main, before, done = ABI["_main"], ABI["_counter_before"], ABI["_counter_done"]
    signature = b"CTR1\x01\x08\0\0"

    def __init__(self):
        self.snapshots = []
        self.continuations = []

    def simulate(self, simulator, commands, path, boundaries):
        carry, previous, previous_pc, previous_map = None, 0, None, False
        output = []
        for index, (end, pc, mapped) in enumerate(boundaries):
            if index == len(boundaries)-1:
                end = len(commands)
            number = 60000+20*index
            prefix = [] if carry is None else restore(carry, previous_pc, previous_map)+complete_commands(number)
            if carry is not None:
                prefix.extend(f"break {a:#x}" for a in
                              (journal.READ_CALL, journal.PROGRAM_CALL, 0x1788, self.done, 0x487))
            text = simulate(simulator, prefix+commands[previous:end]+complete_commands(number+8), path)
            parts = sections(text)
            if carry is not None:
                check_continuation(complete_snapshot(parts, number, previous_pc), carry)
            carry = complete_snapshot(parts, number+8, pc)
            self.continuations.append((carry, pc, mapped))
            previous, previous_pc, previous_map = end, pc, mapped
            output.append(text)
        return "\n".join(output)

    @staticmethod
    def arena(options):
        payload = options.get("payload", b"")
        return (payload+b"\xa5"*(112-len(payload))+b"\xa5"*4+
                options.get("nwk", 0).to_bytes(4, "little")+options.get("aps", 0).to_bytes(4, "little")+
                options.get("limit", 3).to_bytes(2, "little")+
                bytes((options["action"], len(payload), options.get("domain", 0), 0xaa)))

    def configure(self, options, commands):
        commands.extend(["set memory xram 0x3de "+" ".join(hex(b) for b in self.arena(options)), "step 1"])

    def check(self, options, ram, iram, sfr):
        expected = bytearray(self.arena(options))
        if options.get("mode") not in ("cut", "stuck"):
            expected[-1] = options["result"]
            require(sfr[1] == 0x4b, "Counter stack failed to unwind")
        if "value" in options:
            expected[112:116] = options["value"].to_bytes(4, "little")
        if "read_payload" in options:
            output = options["read_payload"]
            expected[:len(output)] = output
            expected[-3] = len(output)
        require(ram[990:1120] == expected, "Counter caller output/tail/arguments changed")
        require(ram[661:685] == options["diag"], "Counter allocation/reservation/fault diagnostic changed")
        require(iram[0x7d:] == b"\xc7"*(256-0x7d), "Counter crossed upper-IRAM guard")
        require(ram[0x1e00:0x1e08] == self.signature and
                ram[XDATA:0x1e00] == b"\xa5"*(0x1e00-XDATA) and ram[0x1e08:] == b"\xa5"*248,
                "Counter changed status/unallocated memory")
        self.snapshots.append((options, ram, iram, sfr))


def cases():
    empty = b"\xff"*4096
    initial = journal.record(1, body(7, 11))+b"\xff"*2048
    opened = op(0, diagnostic(7, 11, 7, 11, 1), [read(0)])
    yield empty, [
        op(0, diagnostic(0, 0, 0, 0, 0, state=1, result=1, nv=2), [read(0, 2)]),
        op(1, diagnostic(7, 11, 7, 11, 1), [read(0, 2), write(0, 1, body(7, 11))], nwk=7, aps=11),
        op(2, diagnostic(8, 11, 263, 11, 2), [read(0), write(1, 2, body(263, 11))], value=7),
    ]
    yield initial, [
        opened,
        op(2, diagnostic(8, 11, 263, 11, 2), [read(0), write(1, 2, body(263, 11))], value=7),
        op(2, diagnostic(9, 11, 263, 11, 2), value=8),
        op(0, diagnostic(263, 11, 263, 11, 2), [read(1)], reset=True),
        op(2, diagnostic(264, 11, 519, 11, 3), [read(1), write(0, 3, body(519, 11))], value=263),
    ]
    yield initial, [
        opened,
        op(2, diagnostic(7, 12, 7, 267, 2), [read(0), write(1, 2, body(7, 267))], domain=1, value=11),
        op(3, diagnostic(7, 267, 7, 267, 3), [read(1), write(0, 3, body(7, 267))]),
        op(0, diagnostic(7, 267, 7, 267, 3), [read(0)], reset=True),
        op(2, diagnostic(7, 268, 7, 523, 4), [read(0), write(1, 4, body(7, 523))], domain=1, value=267),
    ]
    payload = bytes(i ^ 0x69 for i in range(112))
    media = journal.record(1, body(529, 265, payload))+b"\xff"*2048
    yield media, [
        op(0, diagnostic(529, 265, 529, 265, 1, length=112), [read(0)]),
        op(4, diagnostic(529, 265, 529, 265, 1, length=112), read_payload=payload),
        op(3, diagnostic(529, 265, 529, 265, 2), [read(0), write(1, 2, body(529, 265))]),
        op(0, diagnostic(529, 265, 529, 265, 2), [read(1)], reset=True),
        op(2, diagnostic(530, 265, 785, 265, 3), [read(1), write(0, 3, body(785, 265))], value=529),
    ]
    yield journal.record(1, body(0xfffffffe, 0xffffffff))+b"\xff"*2048, [
        op(0, diagnostic(0xfffffffe, 0xffffffff, 0xfffffffe, 0xffffffff, 1), [read(0)]),
        op(2, diagnostic(0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 2),
           [read(0), write(1, 2, body(0xffffffff, 0xffffffff))], value=0xfffffffe),
        op(2, diagnostic(0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 2, result=10)),
        op(2, diagnostic(0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 2, result=10), domain=1),
    ]
    for offset in range(8):
        bad = bytearray(body(7, 11)); bad[offset] ^= 0x80
        diag = diagnostic(0, 0, 0, 0, 0, state=3, result=6 if offset == 4 else 5)
        yield journal.record(1, bad)+b"\xff"*2048, [op(0, diag, [read(0)]), op(2, diag)]
    damaged = bytearray(initial); damaged[2048] = 0
    diag = diagnostic(0, 0, 0, 0, 0, state=3, result=7, nv=1)
    yield bytes(damaged), [op(0, diag, [read(0, 1)]), op(2, diag)]
    old = journal.record(1, body(273, 265, payload))+journal.record(2, body(529, 265, payload))
    for at in range(1, 39):
        before = diagnostic(529, 265, 529, 265, 2, length=112)
        pending = diagnostic(529, 265, 529, 265, 2, state=4, result=12, nv=13, length=112)
        interrupted = write(0, 3, body(785, 265, payload), mode="cut", at=at)
        interrupted["result"] = 13
        if at in (1, 38):
            after = diagnostic(785 if at == 38 else 529, 265, 785 if at == 38 else 529, 265,
                               3 if at == 38 else 2, length=112)
        else:
            after = diagnostic(0, 0, 0, 0, 0, state=3, result=7, nv=1)
        yield old, [
            op(0, before, [read(1)]),
            op(2, pending, [read(1), interrupted], mode="cut"),
            op(0, after, [read(0 if at == 38 else 1, 0 if at in (1, 38) else 1)], reset=True),
        ]
    for at in (1, 2, 37, 38):
        interrupted = write(1, 2, body(785, 265, payload), mode="stuck", at=at)
        interrupted["result"] = 13
        yield media, [
            op(0, diagnostic(529, 265, 529, 265, 1, length=112), [read(0)]),
            op(2, diagnostic(529, 265, 529, 265, 1, state=4, result=12, nv=13, length=112),
               [read(0), interrupted], mode="stuck"),
        ]


def negatives(image, symbols, debug_raw, memory, listings, objects):
    count = 0
    def reject(**changed):
        nonlocal count
        args = dict(image=image, symbols=symbols, debug_raw=debug_raw, memory=memory, listings=listings, objects=objects)
        rejected(lambda: verify(**(args | changed)))
        count += 1
    for a in image:
        reject(image=image | {a: image[a] ^ 1})
    reject(image=image | {SIZE: 0})
    for name in symbols:
        reject(symbols=symbols | {name: symbols[name] ^ 1})
    reject(symbols=symbols | {"unreviewed": 0})
    for line in debug_raw.splitlines(keepends=True):
        if line.startswith((b"F:", b"S:", b"L:", b"T:")):
            reject(debug_raw=debug_raw.replace(line, b"", 1))
            reject(debug_raw=debug_raw+line)
            reject(debug_raw=debug_raw.replace(line, line[:-1]+b"!\n", 1))
    for raw in (debug_raw+b"\xff", debug_raw.replace(b"\n", b"\r"), debug_raw.replace(b"\n", b"\r\n")):
        reject(debug_raw=raw)
    reject(memory=memory+b"\n")
    for m in MODULES:
        reject(listings={k: v for k, v in listings.items() if k != m})
        reject(listings=listings | {m: listings[m]+b"\n"})
        reject(objects=objects | {m: objects[m]+b"A UNKNOWN size 0 flags 0 addr 0\n"})
    return count


def runtime_negatives(client):
    options, *memory = client.snapshots[-1]
    count = 0
    for space, addresses in ((0, (*range(661, 685), *range(990, 1120), XDATA, 0x1dff, 0x1e00, 0x1e08, 0x1eff)),
                             (1, (0x7d, 0xff)), (2, (1,))):
        for address in addresses:
            changed = list(memory)
            data = bytearray(changed[space]); data[address] ^= 1; changed[space] = bytes(data)
            rejected(lambda: client.check(options, *changed))
            count += 1
    for carry, pc, mapped in (client.continuations[0], client.continuations[-1]):
        for space in range(5):
            for address in (0, len(carry[space])-1):
                changed = list(carry); data = bytearray(changed[space])
                data[address] ^= 1; changed[space] = bytes(data)
                rejected(lambda: check_continuation(tuple(changed), carry))
                count += 1
            changed = list(carry); changed[space] = changed[space][:-1]
            rejected(lambda: restore(tuple(changed), pc, mapped))
            count += 1
        for address, value in ((0x99, 1), (0x98, 1), (0x88, 0x10), (0xc8, 4)):
            changed = list(carry); data = bytearray(changed[2])
            data[address-128] = value; changed[2] = bytes(data)
            rejected(lambda: restore(tuple(changed), pc, mapped))
            count += 1
        rejected(lambda: restore(carry, pc, not mapped))
        rejected(lambda: check_pc(f"CPU state= OK PC= 0x{pc+1:x}", pc))
        count += 2
    return count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--simulator", default="s51")
    args = parser.parse_args()
    path, *artifacts = load(args.output)
    allocated = verify(*artifacts)
    bad = negatives(*artifacts)
    require(bad == 32255, "Counter artifact-negative coverage changed")
    check_alias(args.simulator)
    rejected(lambda: check_alias(args.simulator, False))
    calls = commands = peak = total = runtime = segments = 0
    for number, (media, operations) in enumerate(cases()):
        client = CounterClient()
        try:
            observed, count, physical = journal.execute(args.simulator, path, artifacts[0], allocated, 0x1788,
                                                         media, operations, client)
        except (ValueError, KeyError) as exc:
            raise ValueError(f"Counter sequence {number}: {exc}") from exc
        require(observed <= 0x7c, "Counter full-run stack cap exceeded")
        if number == 0:
            runtime = runtime_negatives(client)
        segments += len(client.continuations)
        peak = max(peak, observed); calls += count; commands += physical; total += 1
    require((total, calls, commands, segments, peak, runtime) == (56, 36695, 919, 162, 0x72, 204),
            "Counter exact sequence/call/command/continuation/peak/negative coverage changed")
    print(f"Security counters: {total} linked sequences, {calls} actual flash calls/{commands} RAM commands; "
          f"{SIZE}/{CODE_BUDGET} CODE, {XDATA}+64/{XDATA_BUDGET} XDATA, SP {peak:02X}/7C; "
          f"{bad} artifact + {runtime} snapshot/continuation + 1 alias negatives; "
          f"{segments} full-state segments PASS. Synthetic, not physical durability.")


if __name__ == "__main__":
    main()
