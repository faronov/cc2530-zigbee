#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Actual RX/queue/IRQ composition; synthetic peripherals/C52 interrupts, never hardware."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import unittest

import boot_radio_rx as rx
from boot_image import (
    ALIAS, check_alias, check_pc, marker, memory_dump, section, simulate,
    snapshot, snapshot_commands, verify_component_layout,
)
from boot_timebase import GUARD_SFRS, READER_BYTES, READ_OFFSETS
from radio_fifo_fixture import instructions
from radio_rx_fixture import verify_driver_listing, verify_fscal1_readback
from verify_firmware import (
    cdb_address, code_bytes, parse_ihex, parse_symbols, peripheral_accesses,
    require, verify_irq_primitives,
)

SIZE = 7189
DIGEST = "10b3de7b32381022dbe99c0e9713ed01805b70ad81539d0fb31e4222d2c820b3"
BEFORE, DONE, MAIN = 0x1ab9, 0x1b86, 0x1b88
LENGTHS = rx.LENGTHS | {0x63: 3}
OBJECTS = {
    "fault": (0xcf, 1), "event_head": (0xd0, 1), "event_tail": (0xd1, 1),
    "events": (0xd2, 4), "dropped": (0xd6, 1), "rx": (0xd7, 256), "tx": (0x1d7, 126),
    "diagnostics": (0x255, 31), "rx_head": (0x274, 1), "rx_tail": (0x275, 1),
    "rx_count": (0x276, 1), "tx_count": (0x277, 1), "reserved_end": (0x29c, 1),
    "test_buffer": (0x29d, 128), "test_status": (0x31d, 9), "test_action": (0x326, 1),
    "test_value": (0x327, 1), "test_return": (0x328, 1), "test_irq_count": (0x329, 1),
    "test_irq_return": (0x32a, 1), "test_timeout": (0x32b, 4), "test_limit": (0x32f, 2),
}


def verify(image, symbols, debug, memory, listings):
    require(hashlib.sha256(code_bytes(image, SIZE)).hexdigest() == DIGEST, "Radio queue complete CODE changed")
    require(SIZE <= 8192, "Radio queue composed CODE budget exceeded")
    allocated = verify_component_layout(image, symbols, debug, memory, "radio_queue_test_result",
        ("timebase.c", "radio_rx.c", "irq.c", "radio_queue.c", "test_radio_queue.c"), xdata_budget=1024)
    verify_irq_primitives(image, symbols, debug)
    for name, start, end in (("radio_rx", 0x1f9, 0x1380), ("radio_queue", 0x13a2, 0x1a6a)):
        code = instructions(image, start, end, LENGTHS)
        verify_driver_listing(code, listings[name])
    private = "\n".join(sorted(set(line for line in debug.splitlines() if re.match(
        r"[SLT]:(?:L(?:timebase|radio_rx|radio_queue)\.|F(?:timebase|radio_rx|radio_queue)\$)", line))))
    require(hashlib.sha256(private.encode()).hexdigest() ==
            "12ef2011231077745bb5d9ccb818483b476282d97a1700fd1582eb02e42baa9b",
            "Radio queue complete compiler-private CODE/IRAM/XDATA/BIT/ABI records changed")
    expected = {"_radio_rx_fault": 0x19, "_radio_rx_reserved_end": 0xce, "__gptrput_PARM_2": 0x331,
        "_radio_rx_receive_init": 0x803, "_irq_save_disable": 0x1380, "_irq_restore": 0x138b,
        "_radio_queue_request_rx": 0x1468, "_radio_queue_service": 0x1503,
        "_radio_queue_rx_read": 0x1669, "_radio_queue_tx_submit": 0x1792, "_radio_queue_tx_read": 0x183e,
        "_radio_queue_tx_cancel": 0x18ef, "_radio_queue_cancel_requests": 0x1913, "_radio_queue_snapshot": 0x198d,
        "_radio_queue_test_interrupt": 0x1a6a, "_radio_queue_before": BEFORE, "_radio_queue_done": DONE,
        "_radio_queue_test_cycle": BEFORE, "_main": MAIN, "s_XSEG": 0, "l_XSEG": 0x332,
        "s_SSEG": 0x35, "l_BSEG": 6, "s_BIT_BANK": 0x21, "l_BIT_BANK": 1}
    for name, value in expected.items():
        require(symbols.get(name) == value, "Radio queue linked symbol changed: "+name)
    for name, (address, size) in OBJECTS.items():
        symbol = "radio_queue_"+name
        require(symbols["_"+symbol] == cdb_address(debug, f"L:G${symbol}$0_0$0") == address,
                "Radio queue object address changed")
        sizes = re.findall(rf"^S:G\${symbol}\$[^(\n]+\(\{{(\d+)\}}", debug, re.M)
        require(sizes and all(int(n) == size for n in sizes), "Radio queue object ABI size changed")
    segment = listings["radio_queue"].split(".area XSEG    (XDATA)", 1)[1].split(".area XABS", 1)[0]
    covered = set()
    for address, size in re.findall(r"^\s+([0-9A-F]{6})\s+\d+\s+\.ds (\d+)$", segment, re.M):
        region = set(range(int(address, 16), int(address, 16)+int(size)))
        require(region and not region & covered, "Radio queue private storage overlaps")
        covered |= region
    require(covered == set(range(0xcf, 0x29d)), "Radio queue private/pool storage escaped its fence")
    queue = instructions(image, 0x13a2, 0x1a6a, LENGTHS)
    require(peripheral_accesses(queue) == [(pc, bytes((0xe5, reg)), reg) for pc, reg in
        ((0x15b4, 0xa8), (0x15b8, 0xb8), (0x15bc, 0x9a))],
        "Radio queue bypasses the receiver or touches RF in a memory-only operation")
    producer = instructions(image, 0x1468, 0x1503, LENGTHS)
    require([(pc, raw) for pc, raw in producer.items() if raw[0] == 0x12] ==
        [(0x147c, b"\x12\x13\x80"), (0x14e2, b"\x12\x13\x8b")],
        "ISR producer calls a non-reentrant foreground/runtime helper")
    require(not re.search(r"^S:Lradio_queue.radio_queue_request_rx\$.*\),[FEH],", debug, re.M) and
            not re.search(r"^L:Lradio_queue.radio_queue_request_rx\$", debug, re.M),
            "ISR producer acquired static XDATA/IRAM/BIT compiler-private storage")
    require(all(int.from_bytes(raw[1:], "big") in (0xcf, 0xd0, 0xd1, 0xd6)
                for raw in producer.values() if raw[0] == 0x90),
            "ISR producer accesses a packet pool or foreground storage")
    saved = (0x21, 0xe0, 0xf0, 0x82, 0x83, 7, 6, 5, 4, 3, 2, 1, 0, 0xd0)
    isr = b"".join(bytes((0xc0, reg)) for reg in saved)+bytes.fromhex(
        "75 d0 00 75 82 e9 12 14 68 e5 82 90 03 2a f0 90 03 29 e0 24 01 f0")
    isr += b"".join(bytes((0xd0, reg)) for reg in reversed(saved))+b"\x32"
    require(bytes(image[a] for a in range(0x1a6a, BEFORE)) == isr and
            bytes(image[a] for a in range(3, 6)) == b"\x02\x1a\x6a" and
            "F:G$radio_queue_test_interrupt$0_0$0({2}DF,SV:S),C,0,0,1,0,0" in debug,
            "Synthetic C52 ISR vector/context/RETI changed")
    code = instructions(image, 0x1f9, 0x1380, LENGTHS)
    accesses = peripheral_accesses(code)
    require([raw.hex() for _, raw, _ in accesses] == [
        "e5a8", "e5b8", "e59a", "acbe", "e5c6", "e59e", "e5bf", "e5e9", "e591",
        "e5c6", "75e1e3", "e5d9", "e5d9", "75e1ed"],
        "Composed RX acquired TX/ACK or duplicate RFD reads")
    sites = {pc: ("w" if raw[0] == 0x75 else "r", reg,
             raw[0]-0xa8 if 0xa8 <= raw[0] <= 0xaf else reg if raw[0] == 0x75 else 0xe0)
             for pc, raw, reg in accesses}
    static_reads = []
    for pc, raw in code.items():
        if raw[0] != 0x90 or int.from_bytes(raw[1:], "big") < 0x1e00: continue
        address = int.from_bytes(raw[1:], "big")
        if address == 0x618d:
            require(code[pc+3] == b"\x74\x80" and code[pc+5] == b"\xf0", "RX soft shutdown changed")
            sites[pc+5] = ("w", address, address)
        else:
            require(code[pc+3] == b"\xe0", "Unexpected RX static XREG instruction")
            static_reads.append(address); sites[pc+3] = ("r", address, 0xe0)
    require(tuple(static_reads) == rx.XREADS and code[0x3a4] == b"\xe0" and code[0xc0e] == b"\xf0",
            "Composed RX peripheral whitelist changed")
    sites[0x3a4] = ("r", None, 0xe0); sites[0xc0e] = ("w", None, None)
    for name, values in (("settings", b"".join(a.to_bytes(2, "little") for a in rx.SETTINGS)), ("values", rx.VALUES)):
        address = cdb_address(debug, f"L:Fradio_rx${name}$0_0$0")
        require(bytes(image[a] for a in range(address, address+len(values))) == values, "RX passive constants changed")
    verify_fscal1_readback(code, 0x3a4, 0x22, cdb_address(debug, "L:Fradio_rx$values$0_0$0"))
    reader = symbols["_timebase_read_awake_ticks24"]
    require(bytes(image[reader+i] for i in range(len(READER_BYTES))) == READER_BYTES, "Actual Sleep Timer reader changed")
    for i, offset in enumerate(READ_OFFSETS): sites[reader+offset] = ("r", 0x95+i, 0xe0)
    sites.update({pc: ("r", address, 0xe0) for pc, address in ((0x15b4, 0xa8), (0x15b8, 0xb8), (0x15bc, 0x9a))})
    return allocated, sites


def rejections(image, symbols, debug, memory, listings):
    case = unittest.TestCase()
    for address in image:
        with case.assertRaises(ValueError):
            verify(image | {address: image[address] ^ 1}, symbols, debug, memory, listings)
    for name in ["_radio_queue_"+name for name in OBJECTS]+["_radio_queue_request_rx", "_radio_queue_before",
        "_radio_queue_done", "_radio_queue_test_interrupt", "s_XSEG", "l_XSEG", "s_SSEG", "l_BSEG",
        "__XPAGE", "l_PSEG", "l_XISEG", "l_XABS", "__gptrput_PARM_2", "s_BIT_BANK", "l_BIT_BANK"]:
        with case.assertRaises(ValueError, msg=name):
            verify(image, symbols | {name: symbols[name]+1}, debug, memory, listings)
    for old, new in (("({256}DA2d", "({255}DA2d"), ("({128}ST", "({127}ST"),
        ("({2}DX,ST", "({3}DG,ST"), ("C$radio_queue.c$", "C$missing.c$"),
        ("({1}SC:U),R,0,0,[r7]", "({1}SC:U),F,0,0,[r7]")):
        require(old in debug, "Missing radio queue ABI mutation")
        with case.assertRaises(ValueError):
            verify(image, symbols, debug.replace(old, new), memory, listings)
    for name, text in listings.items():
        match = re.search(r"^(\s+[0-9A-F]{6} )([0-9A-F]{2})(?= [0-9A-F ])", text, re.M)
        require(match is not None, "Missing radio queue listing mutation")
        changed = text[:match.start(2)]+f"{int(match[2], 16)^1:02X}"+text[match.end(2):]
        with case.assertRaises(ValueError):
            verify(image, symbols, debug, memory, listings | {name: changed})


def setup():
    return [ALIAS, "fill xram 0 0x1eff 0xa5", f"run 0 {MAIN:#x}",
            "fill iram 0x80 0xff 0xc7", f"run {MAIN:#x} {BEFORE:#x}"]


def guards(text, number, allocated, *, interrupts=0):
    check_pc(section(text, number), DONE)
    ram, iram, sfr = snapshot(text, number)
    require(ram[0x1e00:0x1e08] == b"RQUE\x01\x08\0\0", "Radio queue status ABI differs")
    require(all(v == 0xa5 for a, v in enumerate(ram) if a not in allocated), "Radio queue escaped allocated XDATA/status")
    require(iram[128:] == b"\xc7"*128 and sfr[1] == 0x36, "Radio queue stack/alias/unwind failed")
    require(sfr[0x28] == interrupts, "Radio queue failed to preserve interrupt enables")
    peaks = [int(value, 16) for value in re.findall(r"Max value of stack pointer=\s*0x([0-9a-f]+)", text)]
    require(peaks and max(peaks) < 128, "Radio queue crossed upper IRAM")
    return ram, iram, sfr, max(peaks)


def irq_path(simulator, path, full):
    commands = setup()+["set memory sfr 0x88 5", "set memory sfr 0xa8 0x81",
        f"set memory xram 0xd0 {full}", "set memory xram 0x326 0 0x55", "run 0x1ab9 0x1468"]
    for i in range(100):
        commands += [marker(10+i*2), "state", marker(11+i*2), "step 1"]
    text = simulate(simulator, commands, path)
    pcs = []
    for i in range(100):
        match = re.search(r"CPU state= OK PC= 0x([0-9a-fA-F]+)", section(text, 10+i*2))
        require(match is not None, "Missing IRQ path PC")
        pc = int(match[1], 16)
        if pc == DONE: return list(dict.fromkeys(pcs))
        pcs.append(pc)
    raise ValueError("IRQ producer did not return within its fixed instruction bound")


def preemption(simulator, path, allocated):
    cases, peak = 0, 0
    for full in (0, 3, 4):
        for pc in irq_path(simulator, path, full):
            commands = setup()+["set memory sfr 0x88 5", "set memory sfr 0xa8 0x81",
                f"set memory xram 0xd0 {full}", "set memory xram 0xd2 1 2 3 4",
                "set memory xram 0x326 0 0x55", f"run {BEFORE:#x} {pc:#x}"]
            commands += snapshot_commands(1)+["set memory sfr 0x88 7", f"run {pc:#x} {DONE:#x}"]+snapshot_commands(5)
            text = simulate(simulator, commands, path)
            check_pc(section(text, 1), pc)
            before = snapshot(text, 1)
            ram, _, sfr, observed = guards(text, 5, allocated, interrupts=0x81)
            peak = max(peak, observed)
            require(ram[0x329] == 1 and sfr[8] == 5 and ram[0xcf] == 0,
                    "Actual C52 delivery/acknowledgment or queue fault differs")
            # C52 samples the injected request after this instruction; JBC can disable EA first.
            early = before[2][0x28] & 0x80 and before[0][0xd0] == full and pc != 0x1380
            if full == 0:
                require(ram[0xd0:0xd2] == b"\x02\0" and
                        ram[0xd2:0xd4] == (b"\xe9\x55" if early else b"\x55\xe9") and
                        ram[0x328] == ram[0x32a] == 0 and ram[0xd6] == 0,
                        "Reentrant publication lost/reordered a cookie or return")
            elif full == 3:
                require(ram[0xd0] == 4 and ram[0xd2:0xd5] == b"\x01\x02\x03" and
                        ram[0xd5] == (0xe9 if early else 0x55) and ram[0xd6] == 1 and
                        (ram[0x328], ram[0x32a]) == ((4, 0) if early else (0, 4)),
                        "ISR/foreground capacity race overwrote or over-admitted a request")
            else:
                require(ram[0xd0] == 4 and ram[0xd2:0xd6] == b"\x01\x02\x03\x04" and
                        ram[0xd6] == 2 and ram[0x328] == ram[0x32a] == 4,
                        "Full queue preemption lost explicit rejection accounting")
            cases += 1
    commands = setup()+["set memory sfr 0x88 5", "set memory sfr 0xa8 0x81",
        "set memory xram 0x326 7", "run 0x1ab9 0x1390", "set memory sfr 0x88 7",
        f"run 0x1390 {DONE:#x}"]+snapshot_commands(1)
    text = simulate(simulator, commands, path)
    ram, _, _, observed = guards(text, 1, allocated, interrupts=0x81)
    require(ram[0xd0] == ram[0x329] == 1 and ram[0x320] == 0 and ram[0x328] == 0,
            "Snapshot used ISR-mutated staging or lost its capture point")
    return cases+1, max(peak, observed)


def native_vectors(output):
    text = subprocess.check_output([str(output/"host-radio-rx-tests"), "--queue-vectors"], text=True, timeout=15)
    vectors = [json.loads(line) for line in text.splitlines()]
    require([v["name"] for v in vectors] ==
        ["queue minimum", "queue maximum", "queue bad CRC", "queue reuse", "queue fault"] and
        [v["result"] for v in vectors] == [0, 0, 15, 0, 8], "Stateful receiver trace inventory changed")
    lines = []
    for v in vectors:
        lines += [f'{v["result"]} {len(v["events"])}',
                  " ".join(str(b) for b in bytes.fromhex(v["frame"]))]
        lines += [f"{int(kind=='w')} {address} {value}" for kind, address, value in v["events"]]
    subprocess.run([str(output/"host-radio-queue-tests"), "--rf-vectors"],
                   input="\n".join(lines)+"\n", text=True, check=True, timeout=15)
    return vectors


def memory_cases(simulator, path, allocated, sites):
    sfr = GUARD_SFRS | {0xa8: 0x81, 0xb8: 0, 0x9a: 0, 0x88: 5}
    commands = setup()+[f"set memory sfr {a:#x} {v:#x}" for a, v in sfr.items()]
    commands += ["set memory xram 0x32b 0x10 0x27 0 0 0xe8 3"]
    records = []
    cursor, tail, dropped, cancelled = 0, 0, 0, 0

    def call(action, value, result, *, frame=None, status=None, allow_enables=False):
        if records: commands.extend(["delete", f"run {DONE:#x} {BEFORE:#x}"])
        commands.append(f"set memory xram 0x326 {action} {value}")
        commands.extend(f"break {pc:#x}" for pc in sites if not allow_enables or pc not in (0x15b4, 0x15b8, 0x15bc))
        number = 10+len(records)*2
        commands.extend([f"run {BEFORE:#x} {DONE:#x}", marker(number), "state",
                         "dump /h xram 0xcf 0x330", marker(number+1)])
        records.append((number, result, cursor, tail, dropped, cancelled, frame, status))

    for cycle in range(64):
        for i in range(4):
            cursor = (cursor+1) & 255
            call(0, (cycle*4+i) & 255, 0)
        dropped += 1
        call(0, 255, 4)
        tail = cursor; cancelled = min(255, cancelled+4)
        call(6, 0, 0)
    for i in range(4):
        cursor = (cursor+1) & 255
        call(0, 0x91+i, 0)
    for _ in range(260):
        dropped = min(255, dropped+1)
        call(0, 255, 4)
    tail = cursor
    call(6, 0, 0)
    pattern = bytes((11+29*i) & 255 for i in range(128))
    for length in (1, 2, 124, 125):
        commands.append("set memory xram 0x29d "+" ".join(hex(v) for v in pattern))
        call(3, length, 0)
        commands.append("fill xram 0x29d 0x31c 0xa5")
        call(3, length, 4)
        expected = bytes((length,))+pattern[:length]+b"\xa5"*(127-length)
        call(4, 0, 0, frame=expected)
        call(4, 0, 3, frame=expected)
    call(3, 4, 0); call(5, 0, 0); call(5, 0, 3); call(2, 0, 3)
    cursor = (cursor+1) & 255
    call(0, 0x42, 0)
    call(1, 10, 1)
    call(1, 15, 5, allow_enables=True)
    call(7, 0, 0, status=bytes((0, 255, 0, 1, 255, 255, 0, 0, 0)))
    cursor = (tail+5) & 255
    commands.append(f"set memory xram 0xd0 {cursor}")
    call(0, 0, 8); call(1, 15, 8); call(3, 5, 8)
    commands += snapshot_commands(60000)
    text = simulate(simulator, commands, path)
    pieces = re.split(r"^0x2530([0-9a-f]{4})\r?\n", text, flags=re.M)
    blocks = {int(pieces[i], 16): pieces[i+1] for i in range(1, len(pieces), 2)}
    for number, result, head, tail, dropped, cancelled, frame, status in records:
        check_pc(blocks[number], DONE)
        ram = memory_dump(blocks[number], 0xcf, 0x262)
        require(ram[1:3] == bytes((head, tail)) and ram[7] == dropped and
                ram[0x278-0xcf] == cancelled and ram[0x328-0xcf] == result,
                "Radio queue cursor/overflow/cancellation/result differs")
        if frame is not None:
            require(ram[0x29d-0xcf:0x31d-0xcf] == frame, "TX copy/ownership/tail changed")
        if status is not None:
            require(ram[0x31d-0xcf:0x326-0xcf] == status, "Memory-only queue status differs")
    ram, _, registers, peak = guards(text, 60000, allocated, interrupts=0x81)
    require(ram[0xcf] == 8 and ram[0x19] == ram[0x329] == 0 and
            all(registers[a-0x80] == v for a, v in sfr.items()), "Memory-only queue touched RF/IRQ/board state")
    return len(records), peak


def composed(simulator, path, allocated, sites, vectors):
    current = GUARD_SFRS | {int(a): n for a, n in vectors[0]["initial"].items()}
    commands = setup()+["fill xram 0x6000 0x63ff 0x69"]
    commands += [f"set memory {'sfr' if a < 256 else 'xram'} {a:#x} {v:#x}" for a, v in current.items()]
    checkpoints, records, event_count = [], [], 0
    position, index = BEFORE, 0

    def begin(action, value):
        nonlocal position
        commands.append("delete")
        if position == DONE: commands.append(f"run {DONE:#x} {BEFORE:#x}")
        commands.append(f"set memory xram 0x326 {action} {value}")
        position = BEFORE

    def finish(result, *, frame=None, status=None):
        nonlocal index, position
        number = 50000+index*4; index += 1
        commands.extend([marker(number), "state", "dump /h xram 0xcf 0x330", marker(number+1)])
        checkpoints.append((number, result, frame, status))
        position = DONE

    def simple(action, value, result, *, frame=None, status=None):
        begin(action, value)
        commands.extend(f"break {pc:#x}" for pc in sites)
        commands.append(f"run {BEFORE:#x} {DONE:#x}")
        finish(result, frame=frame, status=status)

    def receive(vector):
        nonlocal event_count
        begin(1, 15)
        commands.append("set memory xram 0x32b 0x10 0x27 0 0 0xe8 3")
        commands.extend(f"break {pc:#x}" for pc in list(sites)+[DONE])
        commands.append("step 1")
        for kind, address, value in [["r", 0xa8, 0], ["r", 0xb8, 0], ["r", 0x9a, 0]]+vector["events"]:
            number = 10+3*event_count; event_count += 1
            memory = "sfr" if address < 256 else "xram"
            commands.extend(["run", marker(number), "state", "dump /h sfr 0x81 0x83"])
            if kind == "r": commands.append(f"set memory {memory} {address:#x} {value:#x}")
            commands.extend([marker(number+1), "step 1"])
            if kind == "r": commands.extend(["dump /h sfr 0xe0 0xe0", "dump /h iram 4 4"])
            else: commands.append(f"dump /h {memory} {address:#x} {address:#x}")
            commands.append(marker(number+2))
            records.append((number, kind, address, value)); current[address] = value
        commands.append("run")
        finish(0 if vector["result"] == 0 else 6 if vector["result"] == 15 else 7)

    def read_frame(vector):
        commands.append("fill xram 0x29d 0x31c 0xa5")
        simple(2, 0, 0, frame=bytes.fromhex(vector["frame"]))

    for cookie in range(0x31, 0x35): simple(0, cookie, 0)
    simple(0, 0xee, 4)
    receive(vectors[0]); receive(vectors[1])
    simple(1, 15, 4)
    simple(7, 0, 0, status=bytes((0, 0, 0x32, 2, 1, 0, 2, 0, 0)))
    read_frame(vectors[0])
    receive(vectors[2]); receive(vectors[3])
    read_frame(vectors[1]); simple(3, 5, 0)
    simple(0, 0x35, 0); simple(0, 0x36, 0)
    receive(vectors[4])
    simple(1, 15, 7); simple(0, 0xff, 7); simple(3, 5, 7)
    read_frame(vectors[3]); simple(5, 0, 0); simple(6, 0, 0)
    simple(7, 0, 0, status=bytes((7, 8, 0x35, 0, 1, 1, 0, 0, 1)))
    commands += snapshot_commands(60000)+[marker(60004), "dump /h xram 0x6000 0x63ff", marker(60005)]
    text = simulate(simulator, commands, path)
    pieces = re.split(r"^0x2530([0-9a-f]{4})\r?\n", text, flags=re.M)
    numbers = [int(pieces[i], 16) for i in range(1, len(pieces), 2)]
    require(len(numbers) == len(set(numbers)), "Duplicate queue simulator marker")
    blocks = dict(zip(numbers, pieces[2::2]))
    for number, kind, address, value in records:
        part = blocks[number]
        match = re.search(r"CPU state= OK PC= 0x([0-9a-fA-F]+)", part)
        require(match is not None and int(match[1], 16) in sites, "Queue/RX stopped at unplanned MMIO")
        pc = int(match[1], 16); actual_kind, actual_address, observed = sites[pc]
        require(kind == actual_kind and actual_address in (None, address), "Queue/RX MMIO order differs")
        registers = memory_dump(part, 0x81, 3)
        require(registers[0] < 128, "Queue/RX MMIO stack reached upper IRAM")
        if address >= 256:
            require(registers[1:] == address.to_bytes(2, "little"), "Queue/RX MOVX address differs")
            require(actual_address is not None or address in rx.SETTINGS, "Dynamic queue/RX MMIO escaped whitelist")
        require(memory_dump(blocks[number+1], address if observed is None else observed, 1)[0] == value,
                "Queue/RX actual MMIO value differs")
    for number, result, frame, status in checkpoints:
        check_pc(blocks[number], DONE)
        ram = memory_dump(blocks[number], 0xcf, 0x262)
        require(ram[0x328-0xcf] == result, "Composed queue result differs")
        if frame is not None:
            require(ram[0x29d-0xcf:0x31d-0xcf] == frame, "Queued FIFO frame/caller tail changed")
        if status is not None:
            require(ram[0x31d-0xcf:0x326-0xcf] == status, "Queue snapshot/overflow/ownership differs")
    _, _, sfr, peak = guards(text, 60000, allocated)
    for address, value in current.items():
        if address < 256: require(sfr[address-0x80] == value, "Queue/RX touched guarded SFR")
    radio = memory_dump(section(text, 60004), 0x6000, 1024)
    require(all(value == current.get(address, 0x69) for address, value in enumerate(radio, 0x6000)),
            "Queue/RX touched unowned radio RAM/registers")
    return peak, len(checkpoints), len(records)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--simulator", default="s51")
    args = parser.parse_args()
    path = args.output/"radio_queue_test.ihx"
    image = parse_ihex(path.read_text()); symbols = parse_symbols(path.with_suffix(".map").read_text())
    debug, memory = (path.with_suffix("."+ext).read_text() for ext in ("cdb", "mem"))
    listings = {name: (args.output/f"radio_queue_test.{name}.rst").read_text() for name in ("radio_rx", "radio_queue")}
    allocated, sites = verify(image, symbols, debug, memory, listings)
    rejections(image, symbols, debug, memory, listings)
    check_alias(args.simulator)
    vectors = native_vectors(args.output)
    peak, operations, events = composed(args.simulator, path, allocated, sites, vectors)
    irq_cases, irq_peak = preemption(args.simulator, path, allocated)
    memory_count, memory_peak = memory_cases(args.simulator, path, allocated, sites)
    print(f"Radio queue: {operations} actual composed operations/{events} MMIO events, {memory_count} memory-only "
          f"operations, {irq_cases} genuine C52 "
          f"preemptions, complete CODE/private ABI, bounded pools/overflow/retained RX failure and alias guards PASS; "
          f"818 ordinary XDATA+64 reserved, peak SP {max(peak, irq_peak, memory_peak):#x}. Synthetic, not CC2530 IRQ/RF evidence.")


if __name__ == "__main__":
    main()
