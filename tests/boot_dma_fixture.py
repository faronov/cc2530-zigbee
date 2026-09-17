# SPDX-License-Identifier: BSD-3-Clause
"""Real board CODE with explicit synthetic DMA/clock effects; never hardware."""
import re
import unittest
from boot_image import boot_commands, check_guards, check_pc, expected_status, marker, simulate, snapshot_commands, memory_dump
from boot_radio_fifo_fixture import sections, snapshot
from dma_fixture import decode, expected_buffers, inspect_expiry, verify_fixture, verify_dma_relocated
from verify_firmware import parse_ihex, require


def request_handler(proof, length, mode="normal"):
    cfg = "(sfr[0xd5]*256+sfr[0xd4])"
    src = f"(xram[{cfg}]*256+xram[{cfg}+1])"
    dst = f"(xram[{cfg}+2]*256+xram[{cfg}+3])"
    text = (f"expression dma_model=1; expression /0 0xd0000003; "
            f"dump /h xram {proof['xdata_start']:#x} {proof['xdata_start']+7:#x}; ")
    if mode != "stuck":
        for i in range(min(length, 3) if mode == "partial" else length):
            text += (f"expression xram[{dst}+{i}]=xram[{src}+{i}]; "
                     f"expression /0 (0x60000000+{dst}+{i}); ")
        text += "set memory sfr 0xd7 0; "
        if mode != "partial":
            text += "set memory sfr 0xd6 0; set memory sfr 0xd1 1; "
    if mode.startswith("bytes"):
        index = int(mode[5:])
        address = proof["a"]+index if index < 18 else proof["b"]+index-18
        text += f"expression xram[{address}]=xram[{address}]+1; "
    if mode == "partial":
        text += "set memory sfr 0x95 0xd0 7 0; "
    return text + "expression dma_model=0; run"


def model(symbols, proof):
    commands = ["var dma_model", "expression dma_model=1", "var dma_ticks", "expression dma_ticks=0",
                "break sfr w 0xc6",
                "commands 1 expression /0 (0xc6000000+sfr[0xc6]); expression sfr[0x9e]=sfr[0xc6]; run"]
    for n in range(1, 17):
        commands += [f'break sfr w 0xd7 if "dma_model==0 && xram[{proof["xdata_start"]}+5]=={n}"',
                     f"commands {n+1} " + request_handler(proof, n)]
    commands += ['break sfr w 0xd1 if "dma_model==0"',
                 "commands 18 expression dma_model=1; expression /0 0xd0000004; set memory sfr 0xd1 0; expression dma_model=0; run",
                 'break sfr w 0xd6 if "dma_model==0"', "commands 19 expression /0 0xd0000001; run",
                 f"break {proof['arm_ret']:#x}", "commands 20 expression /0 0xd0000002; run",
                 'break sfr r 0x95 if "dma_ticks!=0"',
                 "commands 21 expression sfr[0x97]=sfr[0x97]+1; run"]
    commands += boot_commands(symbols)
    commands += ["fill xram 0x6000 0x70ff 0xa6", "set memory sfr 0xbe 0x84",
                 "set memory sfr 0x95 100 0 0", "set memory sfr 0xd1 0 0 0 0 0 0 0"]
    for reg, value in ((0xc0, 0xa0), (0xa9, 0x31), (0xb9, 0x0e), (0x88, 3), (0x98, 3),
                       (0x9b, 3), (0xe9, 0x55), (0x91, 0xaa), (0xe8, 0x12), (0xbf, 0x37)):
        commands.append(f"set memory sfr {reg:#x} {value}")
    return commands + ["expression dma_model=0"]


def check_dma_fixture(simulator, output, board, symbols):
    path = output / "dma_fixture.ihx"
    image = parse_ihex(path.read_text(encoding="ascii"))
    debug = path.with_suffix(".cdb").read_text()
    proof = verify_fixture(image, symbols, debug)
    before, ready, fault = proof["checkpoints"]
    state = proof["state"]
    case = unittest.TestCase()
    for a in image:
        changed = dict(image); changed[a] ^= 1
        with case.assertRaises(ValueError): verify_fixture(changed, symbols, debug)
    for name in ("_dma_descriptor", "_dma_reserved_end", "__gptrput_PARM_2", "_dma_fixture_state",
                 "_dma_fixture_a", "_dma_fixture_work", "s_SSEG", "l_XSEG"):
        with case.assertRaises(ValueError): verify_fixture(image, symbols | {name: symbols[name]+1}, debug)
    for offset in range(2867):
        changed = dict(image); changed[proof["module_start"] + offset] ^= 1
        with case.assertRaises(ValueError): verify_dma_relocated(changed, symbols, debug)
    peak = 0
    for mode in ("normal", "timeout", "partial", "stuck", "clock", "flags") + tuple(f"bytes{i}" for i in range(36)):
        commands = model(symbols, proof) + [f"run {symbols['_main']:#x} {before:#x}"] + snapshot_commands(1)
        stages = 1029 if mode == "normal" else 2 if mode in ("clock", "flags") else 3
        pc = before
        for index in range(stages):
            if pc == ready: commands += ["step 1"]; pc += 1
            commands += [f"run {pc:#x} {ready:#x}", marker(100 + 2*index), "state",
                         f"dump /h xram {state:#x} {proof['b']+17:#x}",
                         "dump /h xram 0x1e00 0x1e1f", "dump /h sfr 0x81 0x81", marker(101+2*index)]
            pc = ready
        if mode == "timeout":
            # Reach the last NOP's real successor, then the third poll helper.
            commands += ["delete 20", f"run {pc:#x} {proof['arm_ret']:#x}"] + snapshot_commands(4000)
            commands += [f"run {proof['arm_ret']:#x} {proof['address']:#x}"] + snapshot_commands(4010)
            commands += ["set memory sfr 0x95 0xd0 7 0"]
            pc = proof["address"]
        elif mode in ("partial", "stuck") or mode.startswith("bytes"):
            commands += ["commands 17 " + request_handler(proof, 16, mode)]
        elif mode == "clock":
            commands += ["commands 1 run", "expression dma_ticks=1"]
        elif mode == "flags":
            commands += ["set memory sfr 0xa9 0x30"]
        if mode != "normal": commands += [f"run {pc:#x} {fault:#x}"]
        commands += snapshot_commands(5000) + [marker(5004), "dump /h xram 0x6000 0x70ff", marker(5005)]
        if mode != "normal": commands += ["step 64"] + snapshot_commands(5010)
        if mode == "partial":
            for i in range(3, 16):
                commands += [f"expression xram[{proof['a']+1+i}]=xram[{proof['b']+1+i}]"]
            commands += ["expression dma_model=1", "set memory sfr 0xd6 0 0", "set memory sfr 0xd1 1",
                         "expression dma_model=0", "step 64"] + snapshot_commands(5020)
        text = simulate(simulator, commands, path)
        parts = sections(text)
        peak = max(peak, *(int(n, 16) for n in re.findall(r"Max value of stack pointer= 0x([0-9a-f]+)", text)))
        require(peak < 128, "DMA compiled stack reached the upper IRAM guard")
        initial, iram, sfr = snapshot(parts, 1)
        require(initial[0x1e00:0x1e20] == expected_status(board), "DMA changed M0 startup")
        require(decode(initial[state:state+116])["phase"] == 1, "DMA initializer failed")
        for index in range(stages):
            entry = parts[100+index*2]; check_pc(entry, ready)
            r = decode(memory_dump(entry, state, 116))
            stage = 0 if not index else (index-1) % 4 + 1
            require(r["stage"] == stage and r["completed"] == (index//4)&255, "DMA stage/wrap mismatch")
            require(memory_dump(entry, 0x81, 1) == bytes([symbols["s_SSEG"]+1]), "DMA fixture stack leaked")
            boot = memory_dump(entry, 0x1e00, 32)
            require(boot[:8]+boot[9:] == initial[0x1e00:0x1e08]+initial[0x1e09:0x1e20] and
                    boot[8] == r["completed"], "DMA changed immutable M0")
            if stage in (1, 3):
                a, b = expected_buffers(stage, r["completed"], r["length"])
                require(memory_dump(entry, proof["a"], 18) == a and memory_dump(entry, proof["b"], 18) == b,
                        "DMA actual payload/source/tail/guard differs")
        final = ram, iram, sfr = snapshot(parts, 5000)
        check_pc(parts[5000], ready if mode == "normal" else fault)
        check_guards(ram, iram[128:], sfr, symbols)
        require(sfr[1] == symbols["s_SSEG"]+1, "DMA final stack leaked")
        require(memory_dump(parts[5004], 0x6000, 0x1100) == b"\xa6"*0x1100, "DMA touched peripheral XDATA")
        r = decode(ram[state:state+116])
        trace = [int(n) for n in re.findall(r"^0xd000000([1-4])\r?$", text, re.MULTILINE)]
        if mode == "normal":
            require(trace == [1, 2, 3, 4]*514 and r["completed"] == 1, "DMA request/arm/ack repetition changed")
            clocks = [int(n, 16) for n in re.findall(r"^0xc60000([0-9a-f]{2})\r?$", text, re.MULTILINE)]
            require(clocks == [0x88, 0xc9]*257, "DMA actual clock request count/order changed")
            copied = [int(n, 16) for n in re.findall(r"^0x6000([0-9a-f]{4})\r?$", text, re.MULTILINE)]
            expected = []
            for i in range(257):
                expected += list(range(proof["b"]+1, proof["b"]+2+(i&15)))
                expected += list(range(proof["a"]+1, proof["a"]+17))
            require(len(copied) == 6289 and copied == expected, "DMA actual byte/address accounting changed")
        else:
            require(final == snapshot(parts, 5010), "DMA terminal fault mutated state or reused buffers")
            expected = (6, 0) if mode.startswith("bytes") else {
                "timeout": (4, 8), "partial": (4, 8), "stuck": (4, 9),
                "clock": (3, 255), "flags": (5, 255)}[mode]
            require((r["reason"], r["dma_result"]) == expected, f"DMA wrong {mode} failure: {r}")
            if mode.startswith("bytes"):
                index = int(mode[5:])
                require((r["checked"], r["mismatch_buffer"], r["mismatch_index"]) == (index, index//18, index%18),
                        "DMA actual C readback failed to locate a corrupted byte")
            if mode in ("timeout", "partial", "stuck"):
                require(r["dma"]["actions"] == 7 and not r["dma"]["verified"] and not r["checked"],
                        "DMA error falsely confirmed or reused")
            if mode == "timeout":
                live, stack, regs = snapshot(parts, 4010)
                controller = bytes(regs[r-128] for r in (0xd6, 0xd7, 0xd1, 0xc0, 0xd4, 0xd5, 0xd2, 0xd3))
                memory = live + stack
                inspect_expiry(proof, lambda a,n: memory[a:a+n], regs[1], regs[2], regs[0x12],
                               controller, decode(live[state:state+116], allow_running=True))
            if mode == "partial":
                late, late_iram, late_sfr = snapshot(parts, 5020)
                require(late[state:state+116] == ram[state:state+116] and
                        late[proof["work"]:proof["work"]+19] == ram[proof["work"]:proof["work"]+19] and
                        late[proof["xdata_start"]:proof["xdata_start"]+8] ==
                        ram[proof["xdata_start"]:proof["xdata_start"]+8] and late_iram == iram,
                        "Late DMA effects caused firmware recovery/descriptor or diagnostic replacement")
                require(late[proof["a"]+1:proof["a"]+17] == late[proof["b"]+1:proof["b"]+17] and
                        late_sfr[0x51] == 1 and late_sfr[0x56:0x58] == b"\0\0",
                        "Synthetic post-return DMA completion was not actually applied")
                check_guards(late, late_iram[128:], late_sfr, symbols)
    print(f"{board}: DMA fixture 257 compiled cycles / 514 transfers / 6289 bytes; both clocks/routes, "
          f"exact byte addresses, real pre-request hold and 41 fault cases; alias/stack guards PASS, peak SP={peak:02X} "
          "(synthetic only).")
