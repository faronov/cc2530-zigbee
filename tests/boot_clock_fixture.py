#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Actual clock board C execution with synthetic SFR clocks; no hardware access."""

import unittest

from boot_image import (
    boot_commands, check_guards, check_pc, expected_status, marker, memory_dump, section,
    simulate, snapshot, snapshot_commands, timer_sfr,
)
from debug_image import decode_clock_fixture
from verify_firmware import (
    CLOCK_CHECKPOINTS, CLOCK_FIXTURE_SIZE, parse_ihex, require, verify_clock_code, verify_clock_fixture_code,
)


def state_commands(number, state):
    return [marker(number), "state", f"dump /h xram {state:#x} {state + 55:#x}",
            "dump /h xram 0x1e00 0x1e1f", "dump /h sfr 0x81 0x81", marker(number + 1)]


def check_rejections(image, symbols, debug):
    proof = verify_clock_fixture_code(image, symbols, debug)
    case = unittest.TestCase()
    addresses = list(range(proof["function_start"], proof["address"] + 1))
    addresses += list(range(proof["call_address"], proof["command_write_address"] + 2))
    addresses += list(range(proof["poll_observe_address"], proof["poll_sample_address"] + 3))
    addresses += [proof["post_request_address"], proof["post_request_address"] + 1]
    addresses += [symbols[name] + i for name, size in zip(CLOCK_CHECKPOINTS, (2, 2, 3)) for i in range(size)]
    addresses += list(range(symbols["_timebase_read_awake_ticks24"], symbols["_timebase_deadline_after"]))
    for address in addresses:
        changed = dict(image)
        changed[address] ^= 1
        with case.assertRaises(ValueError):
            verify_clock_fixture_code(changed, symbols, debug)
    for name, value in (("_clock_fixture_ready_stop", 0x8000), ("_SOC_CLKCONSTA", 0xc6),
                        ("_timebase_expired", proof["address"]), ("l_OSEG", 2)):
        with case.assertRaises(ValueError):
            verify_clock_fixture_code(image, dict(symbols, **{name: value}), debug)
    for changed in (
        debug.replace("L:XG$timebase_deadline_after", "L:XG$missing"),
        debug + "\nL:XG$timebase_deadline_after$0$0:8000\n",
        debug.replace("$sloc0$0_1$0:8\n", "$sloc0$0_1$0:7\n"),
        debug.replace("{17}S:S$diagnostics", "{18}S:S$diagnostics"),
        debug.replace("{19}DA19d,SC:U", "{18}DA18d,SC:U"),
        debug.replace("source_seen$1_0$14:9A\n", "source_seen$1_0$14:9B\n"),
        debug.replace("Fclock_fixture_state$diagnostics$0_0$0:38\n",
                      "Fclock_fixture_state$diagnostics$0_0$0:39\n"),
    ):
        require(changed != debug, "Clock rejection mutation did not apply")
        with case.assertRaises(ValueError):
            verify_clock_fixture_code(image, symbols, changed)
    extra = dict(image)
    extra.update({0x7f00: 0x12, 0x7f01: proof["function_start"] >> 8, 0x7f02: proof["function_start"] & 255})
    with case.assertRaisesRegex(ValueError, "additional deadline call"):
        verify_clock_fixture_code(extra, symbols, debug)


def check_clock_fixture(simulator, output, board, symbols):
    path = output / "clock_fixture.ihx"
    image = parse_ihex(path.read_text(encoding="ascii"))
    debug = (output / "clock_fixture.cdb").read_text(encoding="utf-8")
    proof = verify_clock_fixture_code(image, symbols, debug)
    check_rejections(image, symbols, debug)
    _, sites = verify_clock_code(image, symbols, debug)
    before, ready, fault = (symbols[name] for name in CLOCK_CHECKPOINTS)
    state, reader = symbols["_clock_fixture_state"], symbols["_timebase_read_awake_ticks24"]
    write, status_read = sites[(0x88, 0xc6)], sites[(0xe5, 0x9e)]
    initial_commands = boot_commands(symbols) + ["set memory sfr 0xbe 4", "set memory sfr 0x9d 0x60",
                                              f"run {symbols['_main']:#x} {before:#x}"]
    commands = initial_commands + snapshot_commands(1)
    for step in range(771):
        stage = step % 3
        target = 0x88 if stage == 1 else 0xc9
        if step:
            commands += [f"run {ready:#x} {before:#x}"]
        if stage == 0:
            commands += [f"run {before:#x} {ready:#x}"]
        else:
            start = (0, 0xff, 0xffff, 0xfffffe)[step % 4]
            commands += [timer_sfr(start), f"run {before:#x} {write:#x}", "step 1",
                         timer_sfr((start + 1) & 0xffffff),
                         f"run {write + 2:#x} {reader:#x}",
                         f"run {reader:#x} {status_read:#x}",
                         f"set memory sfr 0x9e {target:#x}", timer_sfr((start + 6) & 0xffffff),
                         f"run {status_read:#x} {ready:#x}"]
        commands += state_commands(10 + step * 2, state)
    commands += snapshot_commands(1600)
    text = simulate(simulator, commands, path)
    initial, initial_iram, initial_sfr = snapshot(text, 1)
    require(initial[0x1e00:0x1e20] == expected_status(board), "Clock fixture changed M0 startup")
    require(decode_clock_fixture(initial[state:state + 56])["phase"] == 1, "Clock fixture failed initialization")
    check_guards(initial, initial_iram[128:], initial_sfr, symbols)
    for step in range(771):
        entry = section(text, 10 + step * 2)
        check_pc(entry, ready)
        record = decode_clock_fixture(memory_dump(entry, state, CLOCK_FIXTURE_SIZE))
        require(record["phase"] == 3 and record["stage"] == step % 3
                and record["completed_steps"] == (step + 1) & 255
                and record["diagnostics"]["request"]["polls"] == (0 if step % 3 == 0 else 2)
                and record["diagnostics"]["request"]["elapsed_ticks"] == (0 if step % 3 == 0 else 6),
                "Clock compiled sequence/result mismatch")
        boot = memory_dump(entry, 0x1e00, 32)
        require(boot[8] == (step + 1) & 255 and boot[:8] + boot[9:] ==
                initial[0x1e00:0x1e08] + initial[0x1e09:0x1e20], "Clock changed immutable M0/counter")
        require(memory_dump(entry, 0x81, 1)[0] == symbols["s_SSEG"] + 1, "Clock fixture leaked stack")
    ram, iram, sfr = snapshot(text, 1600)
    check_guards(ram, iram[128:], sfr, symbols)
    guarded = [address for name, address in symbols.items() if name.startswith("_SOC_")
               and address not in (0xc6, 0x9e, 0x95, 0x96, 0x97)]
    require(all(sfr[a - 0x80] == initial_sfr[a - 0x80] for a in guarded), "Clock fixture changed guarded SFR")

    # Deadline hold, backward/ambiguous time, stopped time, and unconfirmed rollback.
    for end, cause, rollback_failure in ((2200, 3, False), (99, 6, False), (0x800464, 5, False),
                                          (100, 4, False), (2200, 3, True)):
        commands = initial_commands + [f"run {before:#x} {ready:#x}", f"run {ready:#x} {before:#x}",
                                      timer_sfr(100), f"run {before:#x} {proof['address']:#x}"]
        commands += snapshot_commands(1)
        commands += [timer_sfr(end), f"run {proof['address']:#x} {write:#x}", "step 1"]
        if cause == 4:
            commands += [f"run {write + 2:#x} {write:#x}"]
        else:
            commands += [timer_sfr(end), "set memory sfr 0x9e 0x88",
                         f"run {write + 2:#x} {reader:#x}", "step 1", f"run {reader + 3:#x} {reader:#x}",
                         timer_sfr(3000), f"run {reader:#x} {write:#x}"]
        commands += ["step 1", timer_sfr(3000 if rollback_failure else 3001 if cause != 4 else 100),
                     f"set memory sfr 0x9e {0x88 if rollback_failure else 0xc9:#x}",
                     f"run {write + 2:#x} {fault:#x}"]
        commands += snapshot_commands(5) + ["step 64"] + snapshot_commands(9)
        text = simulate(simulator, commands, path)
        check_pc(section(text, 1), proof["address"])
        running, stack, regs = snapshot(text, 1)
        require(regs[0x02] == regs[0x12] == 0, "Deadline checkpoint did not have DPL=OK/DPS=0")
        require(int.from_bytes(stack[regs[1] - 1:regs[1] + 1], "little") == proof["return_address"],
                "Deadline RET has wrong actual clock call context")
        require(int.from_bytes(running[proof["deadline_address"]:proof["deadline_address"] + 4], "little") == 1124,
                "Actual C deadline was not completely stored before RET")
        record = decode_clock_fixture(running[state:state + 56], allow_running=True)
        require(record["phase"] == 2 and record["stage"] == 1 and regs[0xc6 - 0x80] == 0xc9,
                "Deadline checkpoint is not before the actual XOSC request")
        check_pc(section(text, 5), fault)
        require(snapshot(text, 5) == snapshot(text, 9), "Clock terminal fault loop changed CPU/RAM")
        ram, iram, sfr = snapshot(text, 5)
        record = decode_clock_fixture(ram[state:state + 56])
        require(record["phase"] == 4 and record["reason"] == 1 and record["clock_result"] == cause
                and record["completed_steps"] == 1 and record["diagnostics"]["rollback_result"] ==
                (4 if rollback_failure else 9 if cause == 4 else 0),
                f"Clock fault/result/rollback mismatch: expected {cause}, {record}")
        require(record["diagnostics"]["request"]["polls"] == (4096 if cause == 4 else 1)
                and record["diagnostics"]["rollback"]["polls"] == (4096 if rollback_failure or cause == 4 else 1),
                "Clock independent poll bound mismatch")
        require(ram[0x1e08] == 1 and sfr[1] == symbols["s_SSEG"] + 1, "Clock fault changed heartbeat/stack")
        check_guards(ram, iram[128:], sfr, symbols)
        require(all(sfr[a - 0x80] == initial_sfr[a - 0x80] for a in guarded), "Clock fault changed guarded SFR")

    sample, observe = proof["poll_sample_address"], proof["poll_observe_address"]
    for mode, rollback_polls, result in (
        ("pending", ((0xc9, 3001), (0x89, 3002), (0xc9, 3003)), 0),
        ("canceled", ((0xc9, 3001), (0xc9, 4024)), 9),
        ("late-source", ((0xc9, 3001),), 0),
    ):
        commands = initial_commands + [f"run {before:#x} {ready:#x}", f"run {ready:#x} {before:#x}",
                                      timer_sfr(100), f"run {before:#x} {proof['address']:#x}"]
        commands += snapshot_commands(1)
        if mode != "late-source":
            commands += [timer_sfr(2200)]
        commands += [f"run {proof['address']:#x} {write:#x}", "step 1"] + snapshot_commands(5)
        commands += [timer_sfr(2200), f"set memory sfr 0x9e {0x88 if mode == 'late-source' else 0xc9:#x}",
                     f"run {write + 2:#x} {sample:#x}"] + snapshot_commands(9)
        commands += [f"run {sample:#x} {reader:#x}", "step 1", f"run {reader + 3:#x} {reader:#x}",
                     timer_sfr(3000), f"run {reader:#x} {write:#x}", "step 1"]
        cursor = write + 2
        for index, (status, tick) in enumerate(rollback_polls):
            commands += [f"set memory sfr 0x9e {status:#x}", timer_sfr(tick),
                         f"run {cursor:#x} {sample:#x}"] + snapshot_commands(13 + index * 4)
            if index + 1 < len(rollback_polls):
                commands += [f"run {sample:#x} {observe:#x}"]
                cursor = observe
        commands += [f"run {sample:#x} {fault:#x}"] + snapshot_commands(25)
        commands += ["step 64"] + snapshot_commands(29)
        text = simulate(simulator, commands, path)
        for number, pc in ((1, proof["address"]), (5, proof["post_request_address"]), (9, sample), (25, fault)):
            check_pc(section(text, number), pc)
            if number != 25:
                _, _, registers = snapshot(text, number)
                require(symbols["s_SSEG"] + 5 <= registers[1] < 0x80 and registers[0x12] == 0,
                        "Internal checkpoint violates manual stack/DPS preconditions")
        require(snapshot(text, 1)[0][proof["source_seen_address"]] == 0
                and snapshot(text, 5)[0][proof["source_seen_address"]] == 0,
                "Request setup invented source evidence")
        require(snapshot(text, 9)[0][proof["source_seen_address"]] == int(mode == "late-source"),
                "Actual C did not record the pre-timeout source observation")
        ram, _, _ = snapshot(text, 9)
        require(ram[proof["command_address"]] == 0x88
                and ram[proof["diagnostics_address"]:proof["diagnostics_address"] + 19] ==
                b"\0" * 14 + bytes((0xc9, 0x88, 0x88, 0x88 if mode == "late-source" else 0xc9, 8)),
                "C-observed source/untimed diagnostics differ from the manual checkpoint contract")
        for index, (status, tick) in enumerate(rollback_polls):
            number = 13 + index * 4
            check_pc(section(text, number), sample)
            ram, _, _ = snapshot(text, number)
            require(ram[proof["source_seen_address"]] ==
                    int(mode == "late-source" or (mode == "pending" and index >= 1)),
                    "Old STA became source evidence before delayed departure")
            require(int.from_bytes(ram[proof["diagnostics_address"] + 11:
                                       proof["diagnostics_address"] + 13], "little") == index,
                    "Old STA match returned before the required departure/return evidence")
        require(snapshot(text, 25) == snapshot(text, 29), "Cancellation fault was not terminal")
        ram, iram, sfr = snapshot(text, 25)
        record = decode_clock_fixture(ram[state:state + 56])
        require(record["clock_result"] == 3 and record["phase"] == 4 and record["reason"] == 1
                and record["completed_steps"] == 1 and ram[0x1e08] == 1
                and record["diagnostics"]["rollback_result"] == result
                and record["diagnostics"]["rollback"]["polls"] == len(rollback_polls)
                and record["diagnostics"]["rollback"]["elapsed_ticks"] == rollback_polls[-1][1] - 3000,
                f"{mode}: cancellation result/elapsed/poll contract failed")
        check_guards(ram, iram[128:], sfr, symbols)
        require(all(sfr[a - 0x80] == initial_sfr[a - 0x80] for a in guarded), "Cancellation changed guarded SFR")
    print(f"{board}: clock fixture, 771 compiled steps, exact deadline RET/DPL/caller proof, "
          "timeout/backwards/ambiguity/stall and old/departed/restored or unconfirmed cancellation, "
          "late-source checkpoint and alias/stack guards PASS "
          "(synthetic SFR clocks, not analogue/hardware evidence).")
