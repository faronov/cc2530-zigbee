#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""CC2530 fixture bytes with explicit synthetic Timer1 entry/flags, not a peripheral emulator."""

import unittest

from boot_image import (
    boot_commands, check_guards, check_pc, expected_status, marker, memory_dump, section, simulate,
    snapshot, snapshot_commands, timer_sfr,
)
from irq_fixture import decode_irq_fixture, verify_irq_fixture
from verify_firmware import IRQ_CHECKPOINTS, parse_ihex, require


def to(address):
    return [f"break {address:#x}", "run", f"clear {address:#x}"]


def initial(symbols):
    return boot_commands(symbols) + [
        "set memory sfr 0xbe 4", "set memory sfr 0xd8 0x40",
        "set memory sfr 0xe5 0x40 0x40 0x40", "set memory xram 0x62a3 0x40 0x40",
        "set memory sfr 0xc0 0", "set memory sfr 0xa9 0", "set memory sfr 0xb9 0",
        "set memory sfr 0xaf 0", "set memory sfr 0xe2 0 0 0",
    ] + to(symbols[IRQ_CHECKPOINTS[0]])


def compact(number, state):
    return [marker(number), "state", f"dump /h xram {state:#x} {state + 63:#x}",
            "dump /h xram 0x1e00 0x1e1f", marker(number + 1), "dump /h iram 0 0xff",
            marker(number + 2), "dump /h sfr 0x80 0xff", marker(number + 3)]


def record(text, number, pc, state, symbols, board, completed):
    part = section(text, number)
    check_pc(part, pc)
    result = decode_irq_fixture(memory_dump(part, state, 64))
    boot = memory_dump(part, 0x1e00, 32)
    expected = bytearray(expected_status(board))
    expected[8] = completed & 255
    require(boot == expected and result["completed"] == completed & 255, "IRQ changed M0/cycle state")
    iram = memory_dump(section(text, number + 1), 0, 256)
    sfr = memory_dump(section(text, number + 2), 0x80, 128)
    require(iram[128:] == b"\xc7" * 128 and symbols["s_SSEG"] - 1 <= sfr[1] < 128, "IRQ stack guard failed")
    return result, iram, sfr


def check_rejections(image, symbols, debug, proof):
    case = unittest.TestCase()
    addresses = list(range(proof["isr_entry"], proof["isr_reti"] + 1))
    addresses += list(range(symbols["_irq_save_disable"], proof["restore"] + 23))
    addresses += list(range(proof["restore_call"] - 6, proof["restore_call"] + 10))
    addresses += list(range(0x4b, 0x4e))
    addresses += list(range(3, 0x4b, 8))
    addresses += [proof[name] + i for name in ("timer_reset", "timer_start") for i in range(3)]
    addresses += [symbols[name] + i for name in IRQ_CHECKPOINTS for i in range(2 if "fault" not in name else 3)]
    addresses += list(range(symbols["_timebase_read_awake_ticks24"], symbols["_timebase_deadline_after"]))
    addresses += list(range(proof["wait_begin"], proof["wait_begin"] + proof["wait_begin_size"]))
    for pc in addresses:
        mutated = dict(image)
        mutated[pc] ^= 1
        with case.assertRaises(ValueError):
            verify_irq_fixture(mutated, symbols, debug)
    for name, value in (("_IRQ_T1STAT", 0xa8), ("_IRQ_IRCON", 0xe8), ("_irq_ea", 0xae)):
        with case.assertRaises(ValueError):
            verify_irq_fixture(image, dict(symbols, **{name: value}), debug)
    for changed in (debug.replace("L:XG$irq_restore", "L:XG$missing"),
                    debug.replace("{35}S:S$isr_source", "{36}S:S$isr_source"),
                    debug.replace("L:Firq_fixture_state$start$0_0$0:40", "L:Firq_fixture_state$start$0_0$0:41"),
                    debug.replace(",1,9,0", ",1,3,0")):
        require(changed != debug, "IRQ metadata mutation did not apply")
        with case.assertRaises(ValueError):
            verify_irq_fixture(image, symbols, changed)
    with case.assertRaises(ValueError):
        verify_irq_fixture(image | {4: 0}, symbols, debug)


def check_irq_fixture(simulator, output, board, symbols):
    path = output / "irq_fixture.ihx"
    image = parse_ihex(path.read_text(encoding="ascii"))
    debug = path.with_suffix(".cdb").read_text()
    proof = verify_irq_fixture(image, symbols, debug)
    check_rejections(image, symbols, debug, proof)
    before, armed, pending, inner, ready, fault = (symbols[name] for name in IRQ_CHECKPOINTS)
    state = symbols["_irq_fixture_state"]
    resume, entry, reti, ack = proof["restore"] + 9, proof["isr_entry"], proof["isr_reti"], proof["acknowledge"]
    sp = symbols["s_SSEG"] + 3
    commands = initial(symbols) + snapshot_commands(1)
    for cycle in range(257):
        tick = (0, 0xff, 0xffff, 0xfffff0)[cycle % 4]
        commands += [timer_sfr(tick)] + to(proof["timer_reset"]) + ["step 1", "set memory sfr 0xe2 0 0"]
        commands += to(armed) + to(symbols["_irq_fixture_poll"])
        commands += [timer_sfr((tick + 1) & 0xffffff), "set memory sfr 0xaf 0x20",
                     "set memory sfr 0xc0 2", "set memory sfr 0xe2 0x69 0x96"]
        commands += to(pending) + compact(10 + cycle * 20, state)
        commands += to(inner) + compact(14 + cycle * 20, state)
        commands += to(resume)
        # C52 has no CC2530 Timer1 controller. Explicitly model its hardware
        # return frame/H0/vector, without altering any linked instruction byte.
        commands += [f"set memory iram {sp + 1:#x} {resume & 255:#x} {resume >> 8:#x}",
                     f"set memory sfr 0x81 {sp + 2:#x}", "set memory sfr 0xc0 0",
                     f"run 0x4b {entry:#x}"] + compact(18 + cycle * 20, state)
        commands += to(ack) + ["step 1", "set memory sfr 0xaf 0"]
        commands += to(reti) + compact(22 + cycle * 20, state) + ["step 1"]
        commands += to(ready) + compact(26 + cycle * 20, state)
    commands += snapshot_commands(5200)
    text = simulate(simulator, commands, path)
    ram, iram, sfr = snapshot(text, 1)
    check_guards(ram, iram[128:], sfr, symbols)
    require(decode_irq_fixture(ram[state:state + 64])["phase"] == 1, "IRQ initialization failed")
    for cycle in range(257):
        p = record(text, 10 + cycle * 20, pending, state, symbols, board, cycle)
        n = record(text, 14 + cycle * 20, inner, state, symbols, board, cycle)
        e = record(text, 18 + cycle * 20, entry, state, symbols, board, cycle)
        r = record(text, 22 + cycle * 20, reti, state, symbols, board, cycle)
        done = record(text, 26 + cycle * 20, ready, state, symbols, board, cycle + 1)
        require(p[0]["stage"] == 2 and n[0]["stage"] == 3 and p[2][0x28] == n[2][0x28] == 0 and
                p[2][0x38] == n[2][0x38] == 2 and p[2][0x64] == n[2][0x64] == 0,
                "IRQ pending/inner did not keep EA disabled and timer stopped")
        require(all(e[2][i] == r[2][i] for i in range(128) if i not in (0x2f, 0x38)) and
                e[1][:sp + 3] == r[1][:sp + 3] and e[2][1] == sp + 2 and
                int.from_bytes(e[1][sp + 1:sp + 3], "little") == resume and e[2][2] == 1,
                "IRQ ISR failed actual interrupted register/stack/DPL preservation")
        require(done[0]["phase"] == 3 and done[0]["counter"] == p[0]["counter"] ==
                n[0]["counter"] == 0x9669 and done[2][1] == symbols["s_SSEG"] + 1,
                "IRQ actual RETI/foreground completion/counter suspension failed")
    ram, iram, sfr = snapshot(text, 5200)
    check_guards(ram, iram[128:], sfr, symbols)
    for tick, reason, helper, count in ((2100, 6, 0, 1), (99, 5, 0, 1),
                                       (0x800464, 4, 2, 1), (100, 7, 0, 4096)):
        commands = initial(symbols) + [timer_sfr(100)] + to(armed)
        commands += [timer_sfr(tick)] + to(fault) + snapshot_commands(1)
        commands += ["step 64"] + snapshot_commands(5)
        text = simulate(simulator, commands, path)
        check_pc(section(text, 1), fault)
        require(snapshot(text, 1) == snapshot(text, 5), "IRQ fault loop changed CPU/RAM")
        ram, iram, sfr = snapshot(text, 1)
        result = decode_irq_fixture(ram[state:state + 64])
        require(result["phase"] == 4 and result["reason"] == reason and result["helper_status"] == helper and
                result["pending_polls"] == count and result["isr_count"] == result["completed"] == 0 and
                result["control"] == 0, "IRQ pending fault/bound/result mismatch")
        check_guards(ram, iram[128:], sfr, symbols)
    for race in (False, True):
        commands = initial(symbols) + [timer_sfr(100)] + to(armed)
        commands += to(symbols["_irq_fixture_poll"]) + [
            timer_sfr(101), "set memory sfr 0xaf 0x20", "set memory sfr 0xc0 2",
        ] + to(inner)
        if race:
            commands += to(resume) + [
                f"set memory iram {sp + 1:#x} {resume & 255:#x} {resume >> 8:#x}",
                f"set memory sfr 0x81 {sp + 2:#x}", "set memory sfr 0xc0 0",
                f"run 0x4b {entry:#x}",
            ] + to(ack)
            commands += ["set memory sfr 0xaf 0x21", "step 1", "set memory sfr 0xaf 1",
                         "set memory sfr 0xc0 2"]
        commands += to(fault) + snapshot_commands(1) + ["step 64"] + snapshot_commands(5)
        text = simulate(simulator, commands, path)
        require(snapshot(text, 1) == snapshot(text, 5), "IRQ delivery fault did not latch")
        ram, iram, sfr = snapshot(text, 1)
        result = decode_irq_fixture(ram[state:state + 64])
        require(result["reason"] == (9 if race else 7) and result["completed"] == 0 and
                result["isr_count"] == int(race) and result["source"] == (1 if race else 0x20) and
                result["delivery_polls"] == (1 if race else 4096), "IRQ RW0 race/lost-delivery fault mismatch")
        check_guards(ram, iram[128:], sfr, symbols)
    print(f"{board}: IRQ fixture 257 real-C cycles, explicit synthetic CC2530 entry/H0/RW0, "
          "genuine vector/ISR/RETI/context, six bounded fault cases and alias/stack checks PASS "
          "(not hardware/peripheral emulation).")
