# SPDX-License-Identifier: BSD-3-Clause
"""Actual board image, synthetic controller/window transitions; never hardware."""
import re
import unittest

import boot_flash_exec as engine
from boot_flash_write import IO_SITES
from boot_image import (boot_commands, check_guards, check_pc, expected_status, marker,
                        memory_dump, simulate, snapshot_commands)
from flash_fixture import decode, packet, verify_fixture, verify_listings
from verify_firmware import parse_ihex, require


def sections(text):
    marks = list(re.finditer(r"^0x2530([0-9a-f]{4})\r?\n", text, re.M))
    parts = {}
    for a, b in zip(marks, marks[1:]):
        n = int(a[1], 16)
        require(n not in parts, "Duplicate flash fixture transcript marker")
        parts[n] = text[a.end():b.start()]
    return parts


def snap(parts, n):
    return (memory_dump(parts[n], 0, 0x1f00), memory_dump(parts[n+1], 0, 256),
            memory_dump(parts[n+2], 0x80, 128))


def initial(symbols, proof):
    return boot_commands(symbols)+[
        "fill xram 0x6000 0x7fff 0x69", "fill xram 0xe7f0 0xf80f 0x69",
        "fill xram 0xe800 0xf7ff 0xff", "set memory xram 0x6270 4",
        "set memory xram 0x624a 0xa5", "set memory xram 0x6276 0x44 0xff",
        "set memory sfr 0xbe 4", "set memory sfr 0xc7 2", "set memory sfr 0x92 0",
        "set memory sfr 0xd6 0", "set memory sfr 0xd7 0",
        f"run {symbols['_main']:#x} {proof['wait']:#x}",
    ]


def send(proof, stage, page, raw=None):
    data = packet(stage, page) if raw is None else raw
    return [f"set memory xram {proof['mailbox']:#x} "+" ".join(f"{v:#x}" for v in data)]


def advance(proof, stop=None):
    return ["step 1", f"run {proof['wait']+1:#x} {(stop or proof['wait']):#x}"]


def check_flash_fixture(simulator, output, board, symbols):
    path = output/"flash_fixture.ihx"
    image = parse_ihex(path.read_text()); debug = path.with_suffix(".cdb").read_text()
    proof = verify_fixture(image, symbols, debug); verify_listings(output, image)
    case = unittest.TestCase()
    for a in image:
        with case.assertRaises(ValueError):
            verify_fixture(image | {a: image[a] ^ 1}, symbols, debug)
    for name in symbols:
        if name.startswith("_flash") or name in ("s_XSEG", "l_XSEG", "s_SSEG", "l_BSEG"):
            with case.assertRaises(ValueError, msg=name):
                verify_fixture(image, symbols | {name: symbols[name]+1}, debug)
    for old, new in (("{16}ST", "{15}ST"), ("{2}DX,SC:U", "{3}DG,SC:U"),
                     ("{10}S:S$result", "{9}S:S$result")):
        require(old in debug, "Flash fixture CDB mutation absent")
        with case.assertRaises(ValueError):
            verify_fixture(image, symbols, debug.replace(old, new))
    peak = 0; cases = 0

    def checked(text, n, pc):
        nonlocal peak
        parts = sections(text); check_pc(parts[n], pc)
        ram, iram, sfr = snap(parts, n)
        check_guards(ram, iram[128:], sfr, symbols)
        state = decode(ram[0x194:0x1a4])
        baseline = bytearray(expected_status(board)); baseline[8] = int(state["phase"] == 4)
        require(ram[0x1e00:0x1e20] == baseline, "Flash fixture altered M0 outside completed heartbeat")
        require(ram[0x1ac:0x1b0] == b"\x12\x34\x56\x78", "Flash fixture source changed")
        peaks = [int(v, 16) for v in re.findall(r"Max value of stack pointer=\s*0x([0-9a-f]+)", text)]
        require(peaks and max(peaks) < 128, "Flash fixture stack reached upper IRAM")
        peak = max(peak, *peaks)
        return state, (ram, iram, sfr)

    # No command/MMIO on default resume, either timeout, malformed/replayed
    # packet, wrong page, premature RUN, or late RUN after terminal timeout.
    scenarios = ["boot", "armed-timeout", "early-run", "replay", "scope"]
    scenarios += [(stage, i) for stage in ("arm", "run") for i in range(8)]
    for scenario in scenarios:
        commands = initial(symbols, proof)+snapshot_commands(1)
        commands += [f"break {pc:#x}" for pc in IO_SITES]
        if scenario == "armed-timeout" or scenario in ("replay", "scope") or isinstance(scenario, tuple) and scenario[0] == "run":
            commands += send(proof, "arm", 0)+advance(proof)
        if scenario in ("boot", "armed-timeout"):
            # The real remaining counter executes every decrement, not a shortcut.
            commands += advance(proof, proof["fault"])
        elif scenario == "early-run":
            commands += send(proof, "run", 0)+advance(proof, proof["fault"])
        elif scenario == "replay":
            commands += send(proof, "arm", 0)+advance(proof, proof["fault"])
        elif scenario == "scope":
            commands += send(proof, "run", 1)+advance(proof, proof["fault"])
        else:
            stage, i = scenario; raw = bytearray(packet(stage, 0)); raw[i] ^= 1
            commands += send(proof, stage, 0, raw)+advance(proof, proof["fault"])
        commands += snapshot_commands(10)+send(proof, "run", 0)+["step 64"]+snapshot_commands(20)
        # A real CRT reboot must erase a pre-existing RUN packet.
        commands += ["delete", f"run 0 {symbols['_main']:#x}", "fill iram 0x80 0xff 0xc7",
                     f"run {symbols['_main']:#x} {proof['wait']:#x}"]+snapshot_commands(30)
        text = simulate(simulator, commands, path); parts = sections(text)
        before, _ = checked(text, 1, proof["wait"])
        result, memory = checked(text, 10, proof["fault"])
        later, other = checked(text, 20, proof["fault"])
        # Host intentionally changed only mailbox in terminal loop.
        require(result == later and memory[0][:0x1a4]+memory[0][0x1ac:] ==
                other[0][:0x1a4]+other[0][0x1ac:] and memory[1:] == other[1:] and
                before["phase"] == 1 and result["reason"] == (1 if scenario in ("boot", "armed-timeout") else 2),
                "Flash fixture unarmed/fault path changed state")
        require(memory[0][:0x194] == bytes(0x194), "Unarmed fixture touched hardware-service storage")
        reset, reset_mem = checked(text, 30, proof["wait"])
        require(reset == before and reset_mem[0][0x1a4:0x1ac] == bytes(8), "Reset retained authorization")
        cases += 1

    for page, mode in [(p, "normal") for p in (0, 1)] + [(0, m) for m in
                      ("ignored", "abort", "residue", "drop", "partial", "stuck-erase", "stuck-program", "reader", "history")]:
        commands = initial(symbols, proof)
        # Exhaust 255 polls in both windows; final observation must still accept.
        commands += [f"break {proof['wait']:#x} 255", f"run {proof['wait']+1:#x}", "delete"]
        commands += send(proof, "arm", page)+advance(proof)
        commands += [f"break {proof['wait']:#x} 255", f"run {proof['wait']+1:#x}", "delete"]
        commands += send(proof, "run", page)+advance(proof)+snapshot_commands(1)
        if mode == "history":
            commands += ["set memory xram 0xd1 6"]
        # Real reader MOVX trace, indexed once; no quadratic full-page scan.
        commands += ["break 0x774",
                     "commands 1 expression /0 (0xfb000000+sfr[0x82]+sfr[0x83]*256); "
                     "expression /0 (0xfc000000+sfr[0xc7]); run"]
        commands += advance(proof, proof["fault"] if mode == "history" else None)
        expected_reads = []; entries = []; writes = []
        peripheral = bytearray(b"\x69"*0x2000)
        for a, v in ((0x6270,4),(0x624a,0xa5),(0x6276,0x44),(0x6277,255)):
            peripheral[a-0x6000] = v
        nv = bytearray(b"\xff"*4096)
        final = proof["end"]; reason = 0; result = 0; stuck = False
        for step, offset in ((1, 0), (2, 0), (4, 2044)):
            if mode == "history":
                final, reason, result = proof["fault"], 3, 6; break
            operation = 1 if step == 1 else 2
            base = 100+step*100
            expected_reads += list(range(0xe800+page*2048+offset, 0xe800+page*2048+offset+(1 if step == 1 else 4)))
            commands += [f"run {proof['wait']:#x} 0x487", "step 1", marker(base),
                         "dump /h sfr 0xc7 0xc7", marker(base+1)]
            commands += list(engine.XMAP)
            commands += [f"run 0x489 {engine.RAM:#x}", marker(base+2), "state",
                         "dump /h rom 0x8009 0x8083", "dump /h xram 0 8",
                         "dump /h xram 0xd1 0x159", marker(base+3),
                         "dump /h iram 0 0x7f", "dump /h sfr 0x81 0x81", marker(base+4),
                         f"run {engine.RAM:#x} {engine.COMMAND:#x}", "step 1", marker(base+5),
                         "dump /h xram 0x6270 0x6273", marker(base+6)]
            entries.append((base, operation, page, offset))
            accepted = not (step == 1 and mode in ("ignored", "abort"))
            control = (4 | operation | 0x80) if accepted else 0x24 if mode == "abort" else 4
            commands += [f"set memory xram 0x6270 {control:#x}"]
            peripheral[0x270] = control
            peripheral[0x271:0x273] = (0xfa00+page*512+(offset >> 2)).to_bytes(2,"little")
            current = engine.ACCEPT
            if operation == 2:
                for i, pc in enumerate((0x95,0x97,0x99,0x9b)):
                    pc += engine.DELTA
                    commands += [f"run {current:#x} {pc:#x}", marker(base+10+i*2),
                                 "dump /h sfr 0x82 0x83", "dump /h sfr 0xe0 0xe0", marker(base+11+i*2), "step 1"]
                    current = pc+1
                writes.append(base)
                peripheral[0x273] = 0x78
            commands += [f"run {current:#x} {engine.POLL:#x}"]
            stuck = (step == 1 and mode == "stuck-erase") or (step == 2 and mode == "stuck-program")
            if stuck:
                commands += [f"run {engine.POLL:#x} {engine.STOP:#x}"]
                final, result = engine.STOP, 10; break
            if accepted:
                commands += ["step 1", f"run {engine.POLL+1:#x} {engine.POLL:#x}", "set memory xram 0x6270 4"]
                peripheral[0x270] = 4
                pos = page*2048+offset
                if operation == 1:
                    commands += [f"fill xram {0xe800+pos:#x} {0xefff+pos:#x} 255"]
                    if mode == "residue":
                        nv[pos+2047] = 0; commands += [f"set memory xram {0xefff+pos:#x} 0"]
                elif mode != "drop":
                    word = b"\x12\x34\x56\x78"[:1 if mode == "partial" else 4]
                    nv[pos:pos+len(word)] = word
                    commands += [f"set memory xram {0xe800+pos:#x} "+" ".join(str(v) for v in word)]
            commands += [f"run {engine.POLL:#x} {engine.RET:#x}", marker(base+20), "state",
                         "dump /h xram 0x6270 0x6270", marker(base+21), "step 1",
                         marker(base+22), "state", marker(base+23)]
            bad = (step == 1 and mode in ("ignored","abort","residue","reader")) or (step == 2 and mode in ("drop","partial"))
            if accepted:
                expected_reads += list(range(0xe800+page*2048+offset,
                                              0xe800+page*2048+offset+(2048 if step == 1 else 4)))
            if step == 1 and mode == "reader":
                commands += ["run 0x4b3 0x5e0", "set memory sfr 0xd6 1"]
                expected_reads = expected_reads[:-2048]
            target = proof["fault"] if bad else proof["end"] if step == 4 else proof["wait"]
            current = 0x5e0 if step == 1 and mode == "reader" else 0x4b3
            commands += [f"run {current:#x} {target:#x}"]
            if bad:
                final, reason, result = proof["fault"], 4, 6 if mode in ("ignored","abort") else 7 if mode == "reader" else 8
                break
            if step == 2:
                commands += advance(proof)  # WORD_USED with no service MMIO.
        commands += snapshot_commands(2000)+["step 64"]+snapshot_commands(2010)
        if stuck:
            commands += ["set memory xram 0x6270 4", "step 64"]+snapshot_commands(2020)
            peripheral[0x270] = 4
        commands += [marker(2100), "dump /h xram 0x6000 0x7fff", marker(2101),
                     "dump /h xram 0xe7f0 0xf80f", marker(2102)]
        text = simulate(simulator, commands, path); parts = sections(text)
        admitted, _ = checked(text, 1, proof["wait"])
        require(admitted == dict(phase=3,reason=0,page=page,step=0,result=255,checks=0,remaining=0),
                "Last bounded handshake observation failed")
        outcome, memory = checked(text, 2000, final)
        require(outcome["reason"] == reason and outcome["result"] == result and
                outcome["phase"] == (3 if stuck else 5 if reason else 4),
                f"Flash fixture terminal outcome {page}/{mode}: {outcome}, expected {reason}/{result}")
        require(snap(parts, 2010) == memory, "Flash fixture terminal loop changed RAM/CPU")
        if stuck:
            require(snap(parts, 2020) == memory and memory[0][7] == 7 and
                    memory[0][0xd1] == 10 and memory[0][0xd6:0xd8] == b"\x02\xff" and
                    memory[2][0x47] == 10, "Flash fixture lost RAM_STOP/PENDING/XMAP after late idle")
        for base, op, p, offset in entries:
            require(memory_dump(parts[base], 0xc7, 1) == b"\x0a", "XMAP preceded actual MEMCTR")
            check_pc(parts[base+2], engine.RAM)
            require(memory_dump(parts[base+2], engine.RAM, 123) == bytes(image[a] for a in range(0x62,0xdd)),
                    "Fixture did not copy/execute real RAM engine")
            work = memory_dump(parts[base+2], 0, 9)
            require(work == bytes((4|op,))+(bytes(4) if op == 1 else b"\x12\x34\x56\x78")+b"\xff\xff\xff\0",
                    "Fixture staged command/limit differs")
            sp = memory_dump(parts[base+3], 0x81, 1)[0]
            require(memory_dump(parts[base+3], sp-1, 2) == b"\xb3\x04", "Fixture RAM call frame changed")
            state = memory_dump(parts[base+2], 0xd1, 137)
            require(state[:8] == bytes((10,op,p,offset&255,offset>>8,2,255,0)) and
                    (not state[8] & (1<<p) if op == 1 else state[9+p*64+(offset>>5)] & (1<<((offset>>2)&7))),
                    "Fixture history not committed before command")
            require(memory_dump(parts[base+5], 0x6270, 3) ==
                    bytes((4|op,))+(0xfa00+p*512+(offset>>2)).to_bytes(2,"little"), "Fixture FCTL/FADDR differs")
            if base+20 in parts:
                check_pc(parts[base+20], engine.RET); check_pc(parts[base+22], 0x4b3)
                require(not memory_dump(parts[base+20], 0x6270, 1)[0] & 0x83, "Busy flash RET")
        for base in writes:
            for i, v in enumerate(b"\x12\x34\x56\x78"):
                require(memory_dump(parts[base+10+2*i], 0x82, 2) == b"\x73\x62" and
                        memory_dump(parts[base+10+2*i], 0xe0, 1) == bytes((v,)), "Fixture ordered FWDATA differs")
        reads = [int(n,16) for n in re.findall(r"^0xfb00([0-9a-f]{4})\r?$", text, re.M)]
        maps = [int(n,16) for n in re.findall(r"^0xfc0000([0-9a-f]{2})\r?$", text, re.M)]
        require(reads == expected_reads and maps == [7]*len(reads), "Fixture full real MOVX verification trace differs")
        require(memory_dump(parts[2100],0x6000,0x2000) == peripheral, "Fixture changed excluded peripheral/information window")
        require(memory_dump(parts[2101],0xe7f0,0x1020) == b"\x69"*16+nv+b"\x69"*16, "Fixture escaped selected scratch/neighbor bounds")
        cases += 1
    print(f"{board}: flash fixture {cases} linked scenarios, default/reset disarm, bounded handshake, "
          f"genuine service/RAM command/frame/full MOVX/terminal failure proofs, alias/upper IRAM PASS; "
          f"peak SP={peak:#x} (synthetic only, NO hardware acceptance)")
