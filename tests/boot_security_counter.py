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
SIZE, XDATA, STACK = (9907, 1125, 66)
CODE_BUDGET, XDATA_BUDGET = 12288, 1280
CODE_SHA = '3b5ef880f8a88567d5eeea94b4f8ef2d2903403f00b718149b5eeb476da057ea'
CDB_SHA = 'f17bca30d574d8e38cf20b21ef9073c8bcb981f49e8a419f6ccf54bc47271d99'
MAP_SHA = 'be591485d0f0f424ac44b4dd1c10ca177292cc06a878a35f1bd3f73a8c835788'
MEM_SHA = '527dd87f14912c9d5eaf49a24b8febb6a2f840d4c9e3e78e5003839cef211879'
LISTINGS = {'flash_exec': ('eacf1686fa1e60845d4ea95c199624137eb12a1136192664314ea7b6234b17f3',
                (661, 1176, '211c8531b18375463aba8749f024f76b705aafc128226106e2973c6413a69c5e')),
 'flash': ('a9a3fbfeb27861e0591c06522d7226786b5823304313785e1157d5e15f952833',
           (416, 766, '186fbbf903d6e033e258a92d0f44cd921fcb73d82faca5ce4c5d63ec391022c6')),
 'flash_write': ('931fd16bbe4bff1b4e9a3177f638c0aee7ca19d0796d7ba9bf7fa3939d598e7a',
                 (646, 1095, '7aa7bf21cd8e4b189d44cf0f84f8eb2006a22ca0d73e94484e1821477cb75ba3')),
 'nv_record': ('7fb1fa467fea810982b5f939891fd27e1d32e75b63107bf727fff97b8370a53d',
               (2170, 3585, '69f79224e1cfc7ab7cdf64663680bf3e1636e41bcc81de790db370e8945b5e8b')),
 'security_counter': ('a60bc054f03432958cfe3c2583895fb395576a9db18ca55cd4a74093481af067',
                      (1741, 2821, '62962473dacb51db8697e294bde34f5725a73c207d8dd3bc8d7ba784a316b0b5')),
 'security_counter_test': ('fc7c9133bdae2e0d4c1f960576d83aff4df48aa3cb83639b60797ddedc254933',
                           (209, 363, 'e477312e3c7f507a3aa1d0c585d4637e4588c6a1f753ea6fc9e6b562964665f0'))}
OBJECTS = {'flash_exec': ('be36ace42e071daa925d78eaf4790de1470383b0e7fe4d3410d2475d1a43d100', (1176, 155, 8, 0, 1)),
 'flash': ('5aa08e12698b547e83186f1716727166e7e63eb5e0d1c169ea716b6d2d439ca4', (766, 54, 4, 0, 0)),
 'flash_write': ('ddd7f14a6771b79e349b5cb0dde71ad0c815f519390104d5103df3f047d8569e', (1095, 195, 5, 0, 0)),
 'nv_record': ('baa88c4c79fae8ecec89d1abcc99ed143895a04780bbf42c51c285eaba7a451b', (3593, 258, 15, 4, 1)),
 'security_counter': ('dae9936d09f67fdd2a350207b06f78762866840f1ad6a43f385984d3c7f80afd',
                      (2821, 333, 14, 4, 1)),
 'security_counter_test': ('7040dccdf71a2b35a6be41c55e7c8dede9b1b7e55e03285fa23d0591c726e95e',
                           (363, 130, 4, 0, 0))}
ABI = {'_counter_before': 9541,
 '_counter_done': 9801,
 '_main': 9803,
 '_counter_test_action': 1121,
 '_counter_test_aps': 1115,
 '_counter_test_buffer': 995,
 '_counter_test_cycle': 9541,
 '_counter_test_domain': 1123,
 '_counter_test_length': 1122,
 '_counter_test_limit': 1119,
 '_counter_test_nwk': 1111,
 '_counter_test_result': 7680,
 '_counter_test_return': 1124,
 '_counter_test_value': 1107,
 '_security_counter_blob': 686,
 '_security_counter_check': 814,
 '_security_counter_create': 8057,
 '_security_counter_create_PARM_2': 962,
 '_security_counter_create_PARM_3': 966,
 '_security_counter_create_PARM_4': 968,
 '_security_counter_create_PARM_5': 969,
 '_security_counter_diagnostic': 662,
 '_security_counter_open': 7641,
 '_security_counter_read': 9321,
 '_security_counter_read_PARM_2': 989,
 '_security_counter_read_PARM_3': 990,
 '_security_counter_reserved_end': 994,
 '_security_counter_save': 9081,
 '_security_counter_save_PARM_2': 984,
 '_security_counter_save_PARM_3': 985,
 '_security_counter_status': 9537,
 '_security_counter_take': 8486,
 '_security_counter_take_PARM_2': 975,
 '_security_counter_take_PARM_3': 977}


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
                require(accesses == [(9803, b"\x75\xa8\x00", 0xa8), (9806, b"\x75\xb8\x00", 0xb8),
                                     (9809, b"\x75\x9a\x00", 0x9a)], "Counter harness IRQ initialization changed")
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
    require(sha(bytes(image[a] for a in range(0xc3f, 0x1a40))) ==
            "1c2406bee23c02986a516de7deabf98b05603e57c67302aa53d8aeb78e2ddd6d",
            "Counter journal linked code changed")
    calls = [(a, int.from_bytes(raw[1:], "big")) for a, raw in decoded.items() if raw[0] == 0x12]
    require([(a, target) for a, target in calls if 0xc3f <= a < 0x1a40 and target < 0xc3f] ==
            [(journal.READ_CALL, 0x5e0), (journal.PROGRAM_CALL, 0xbf7), (0x174c, 0xbc6)],
            "Counter journal no longer uses actual reader/program/erase calls")
    require({target for a, target in calls if 0x1a40 <= a < ABI["_counter_before"] and target < 0x1a40} ==
            {0x131f, 0x14d7, 0x1a3c}, "Counter bypassed journal load/replace/status")
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

    def __init__(self, symbols=None):
        self.caller, self.xdata, self.unwind = 995, XDATA, STACK+1
        if symbols is not None:
            self.main, self.before, self.done = (symbols[n] for n in ("_main", "_counter_before", "_counter_done"))
            self.caller, self.xdata = symbols["_counter_test_buffer"], symbols["l_XSEG"]
            self.unwind = symbols["s_SSEG"] + 1
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
                              (journal.READ_CALL, journal.PROGRAM_CALL, 0x174c, self.done, 0x487))
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
        commands.extend([f"set memory xram {self.caller:#x} "+" ".join(hex(b) for b in self.arena(options)), "step 1"])

    def check(self, options, ram, iram, sfr):
        expected = bytearray(self.arena(options))
        if options.get("mode") not in ("cut", "stuck"):
            expected[-1] = options["result"]
            require(sfr[1] == self.unwind, "Counter stack failed to unwind")
        if "value" in options:
            expected[112:116] = options["value"].to_bytes(4, "little")
        if "read_payload" in options:
            output = options["read_payload"]
            expected[:len(output)] = output
            expected[-3] = len(output)
        require(ram[self.caller:self.caller+130] == expected, "Counter caller output/tail/arguments changed")
        # NV's new one-byte returning local moves this complete 24-byte object;
        # do not compare the old fence byte and silently omit its final field.
        require(ram[662:686] == options["diag"], "Counter allocation/reservation/fault diagnostic changed")
        require(iram[0x7d:] == b"\xc7"*(256-0x7d), "Counter crossed upper-IRAM guard")
        require(ram[0x1e00:0x1e08] == self.signature and
                ram[self.xdata:0x1e00] == b"\xa5"*(0x1e00-self.xdata) and ram[0x1e08:] == b"\xa5"*248,
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
    for space, addresses in ((0, (*range(662, 686), *range(client.caller, client.caller+130),
                                 client.xdata, 0x1dff, 0x1e00, 0x1e08, 0x1eff)),
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
    require(bad == 32257, "Counter artifact-negative coverage changed")
    check_alias(args.simulator)
    rejected(lambda: check_alias(args.simulator, False))
    calls = commands = peak = total = runtime = segments = 0
    for number, (media, operations) in enumerate(cases()):
        client = CounterClient()
        try:
            observed, count, physical = journal.execute(args.simulator, path, artifacts[0], allocated, 0x174c,
                                                         media, operations, client)
        except (ValueError, KeyError) as exc:
            raise ValueError(f"Counter sequence {number}: {exc}") from exc
        require(observed <= 0x7c, "Counter full-run stack cap exceeded")
        if number == 0:
            runtime = runtime_negatives(client)
        segments += len(client.continuations)
        peak = max(peak, observed); calls += count; commands += physical; total += 1
    # Full 56-sequence observation, not a subtraction from the object DATA sum.
    require((total, calls, commands, segments, peak, runtime) == (56, 36695, 919, 162, 0x66, 204),
            f"Counter exact sequence/call/command/continuation/peak/negative coverage changed: "
            f"{(total, calls, commands, segments, peak, runtime)}")
    print(f"Security counters: {total} linked sequences, {calls} actual flash calls/{commands} RAM commands; "
          f"{SIZE}/{CODE_BUDGET} CODE, {XDATA}+64/{XDATA_BUDGET} XDATA, SP {peak:02X}/7C; "
          f"{bad} artifact + {runtime} snapshot/continuation + 1 alias negatives; "
          f"{segments} full-state segments PASS. Synthetic, not physical durability.")


if __name__ == "__main__":
    main()
