# SPDX-License-Identifier: BSD-3-Clause
"""Execute unchanged board bytes with explicit, nonphysical FIFO/clock effects."""

import re
import unittest

from boot_image import (
    boot_commands, check_guards, check_pc, expected_status, marker, memory_dump,
    simulate, snapshot_commands,
)
from radio_fifo_fixture import decode, inspect_deadline, verify_fixture, verify_fifo_relocated
from verify_firmware import parse_ihex, require


def sections(text):
    parts = re.split(r"^0x2530([0-9a-f]{4})\r?\n", text, flags=re.MULTILINE)
    return {int(parts[i], 16): parts[i + 1] for i in range(1, len(parts), 2)}


def snapshot(parts, number):
    return (memory_dump(parts[number], 0, 0x1f00), memory_dump(parts[number + 1], 0, 256),
            memory_dump(parts[number + 2], 0x80, 128))


def model(symbols, mode="normal", read_site=None):
    clock = "expression sfr[0x9e]=sfr[0xc6]; " if mode != "clock" else ""
    byte = "sfr[0xd9]" + ("+(xram[0x619c]==125)" if mode == "bytes" else "")
    write = (f"expression xram[0x6080+xram[0x619c]]={byte}; "
             "expression xram[0x619c]=xram[0x619c]+1; expression xram[0x61a2]=xram[0x619c]; ")
    if mode == "controller":
        write += "expression sfr[0xbf]=0x10; "
    clear = "" if mode == "cap" else (
        "expression xram[0x619c]=0; expression xram[0x61a1]=0; expression xram[0x61a2]=0; ")
    commands = [
        "break sfr w 0xc6", "commands 1 " + clock + "run",
        "break sfr w 0xd9", "commands 2 " + write + "dump /h sfr 0xd9 0xd9; run",
        "break sfr w 0xe1", "commands 3 " + clear + "dump /h sfr 0xe1 0xe1; run",
    ]
    if read_site is not None:
        commands += [f"break {read_site:#x}",
                     "commands 4 expression /0 (0x60000000+sfr[0x83]*256+sfr[0x82]); run"]
    commands += boot_commands(symbols)
    commands += ["fill xram 0x6000 0x61ff 0xa6", "set memory sfr 0xbe 0x84",
                 "set memory sfr 0xbf 0", "set memory sfr 0x95 100 0 0"]
    for address in (0x618b, 0x619b, 0x619c, 0x619d, 0x619e, 0x619f, 0x61a1, 0x61a2):
        commands.append(f"set memory xram {address:#x} 0")
    for address, value in ((0x6189, 0x40), (0x618a, 1), (0x61e1, 23), (0x6192, 0x3f),
                           (0x6193, 0x18), (0x61a3, 0x53), (0x61a4, 0x21), (0x61a5, 0x55)):
        commands.append(f"set memory xram {address:#x} {value}")
    for address, value in ((0xa9, 0x31), (0xb9, 0x0e), (0xe9, 0xaa), (0x91, 0x35),
                           (0x9b, 3), (0x88, 3)):
        commands.append(f"set memory sfr {address:#x} {value}")
    return commands


def check_rejections(image, symbols, debug):
    case = unittest.TestCase()
    proof = verify_fixture(image, symbols, debug)
    for address in image:
        changed = dict(image)
        changed[address] ^= 1
        with case.assertRaisesRegex(ValueError, "instructions"):
            verify_fixture(changed, symbols, debug)
    for key in ("_radio_fifo_fixture_before", "_radio_fifo_fixture_state", "s_SSEG", "l_XSEG",
                "l_PSEG", "l_XISEG", "l_XABS", "_RFF_RFIRQF0", "_RFF_IP0"):
        with case.assertRaises(ValueError):
            verify_fixture(image, dict(symbols, **{key: symbols[key] + 1}), debug)
    for changed in (debug.replace("{57}S:S$checked", "{56}S:S$checked"),
                    debug.replace("{10}S:S$bytes_verified", "{9}S:S$bytes_verified"),
                    debug.replace("{8}S:S$deadline", "{12}S:S$deadline"),
                    debug.replace("({3}DG,SC:U)", "({2}DX,SC:U)"),
                    debug.replace("({125}DA125d,SC:U),D", "({125}DA125d,SC:U),F"),
                    debug.replace("({21}ST__00000006:S)", "({20}ST__00000006:S)"),
                    debug.replace("L:XG$timebase_deadline_after$", "L:XG$absent$")):
        with case.assertRaises(ValueError):
            verify_fixture(image, symbols, changed)
    for site in (proof["write"], proof["flush_rx"], proof["flush_tx"], proof["deadline_call"]):
        changed = dict(image)
        changed[site + 1] ^= 1
        with case.assertRaises(ValueError):
            verify_fifo_relocated(changed, symbols, debug)
    return proof


def check_radio_fifo_fixture(simulator, output, board, symbols):
    path = output / "radio_fifo_fixture.ihx"
    image = parse_ihex(path.read_text(encoding="ascii"))
    debug = (output / "radio_fifo_fixture.cdb").read_text(encoding="utf-8")
    proof = check_rejections(image, symbols, debug)
    before, ready, fault = proof["checkpoints"]
    for mode in ("normal", "timeout", "controller", "bytes", "cap", "clock", "flags"):
        commands = model(symbols, mode, proof["tx_read_address"]) + [
            f"run {symbols['_main']:#x} {before:#x}"] + snapshot_commands(1)
        stages = 1286 if mode == "normal" else {
            "timeout": 2, "controller": 2, "bytes": 4, "cap": 3, "clock": 0, "flags": 1,
        }[mode]
        pc = before
        for index in range(stages):
            if pc == ready:
                commands += ["step 1"]
                pc += 1
            commands += [f"run {pc:#x} {ready:#x}", marker(100 + index * 2), "state",
                         "dump /h xram 0 0x6b", "dump /h xram 0x1e00 0x1e1f",
                         "dump /h sfr 0x81 0x81", "dump /h xram 0x6080 0x60fd",
                         marker(101 + index * 2)]
            pc = ready
        if mode == "timeout":
            commands += [f"run {pc:#x} {proof['address']:#x}"] + snapshot_commands(4000)
            commands += ["set memory sfr 0x95 0xd0 7 0"]
            pc = proof["address"]
        if mode == "flags":
            commands += ["set memory sfr 0xe9 0xab"]
        if mode != "normal":
            commands += [f"run {pc:#x} {fault:#x}"]
        commands += snapshot_commands(5000) + [
            marker(5004), "dump /h xram 0x6000 0x61ff", marker(5005),
        ]
        if mode != "normal":
            commands += ["step 64"] + snapshot_commands(5010)
        text = simulate(simulator, commands, path)
        parts = sections(text)
        initial, initial_iram, initial_sfr = snapshot(parts, 1)
        require(initial[0x1e00:0x1e20] == expected_status(board) and decode(initial[:108])["phase"] == 1,
                "FIFO changed real M0 startup")
        check_guards(initial, initial_iram[128:], initial_sfr, symbols)
        clock_record = None
        for index in range(stages):
            entry = parts[100 + index * 2]
            check_pc(entry, ready)
            record = decode(memory_dump(entry, 0, 108))
            require(record["phase"] == 3 and record["stage"] == (0 if index == 0 else (index - 1) % 5 + 1)
                    and record["completed"] == (index // 5) & 255,
                    f"FIFO C stage/wrap mismatch: {mode} step {index}: {record}")
            boot = memory_dump(entry, 0x1e00, 32)
            require(boot[:8] + boot[9:] == initial[0x1e00:0x1e08] + initial[0x1e09:0x1e20] and
                    boot[8] == record["completed"], "FIFO changed immutable M0/heartbeat")
            require(memory_dump(entry, 0x81, 1) == bytes((symbols["s_SSEG"] + 1,)), "FIFO stage leaked stack")
            if index == 0:
                clock_record = record["clock"]
            require(record["clock"] == clock_record, "FIFO clobbered original clock diagnostics")
            if record["stage"] in (2, 4):
                expected = b"\x05\x13\x57\xa9" if record["stage"] == 2 else bytes([127]) + bytes(
                    i ^ 0x69 for i in range(125))
                require(memory_dump(entry, 0x6080, len(expected)) == expected, "FIFO accepted wrong payload/tag")
        ram, iram, sfr = final = snapshot(parts, 5000)
        check_pc(parts[5000], ready if mode == "normal" else fault)
        check_guards(ram, iram[128:], sfr, symbols)
        require(sfr[1] == symbols["s_SSEG"] + 1, "FIFO final call frame leaked")
        record = decode(ram[:108])
        guarded = [address for name, address in symbols.items()
                   if name.startswith("_SOC_") and name not in (
                       "_SOC_CLKCONCMD", "_SOC_CLKCONSTA", "_SOC_RFD", "_SOC_RFST", "_SOC_RFERRF",
                       "_SOC_ST0", "_SOC_ST1", "_SOC_ST2")]
        require(all(sfr[a - 128] == initial_sfr[a - 128] for a in guarded),
                "FIFO changed unowned GPIO/IRQ/sleep SFR")
        radio = memory_dump(parts[5004], 0x6000, 512)
        require(radio[:128] == b"\xa6" * 128 and radio[254:384] == b"\xa6" * 130,
                "FIFO touched RX RAM/unknown TX tail/address RAM")
        strobes = [int(n, 16) for n in re.findall(r"^0xe1\s+([0-9a-f]{2})\s", text, re.MULTILINE)]
        writes = [int(n, 16) for n in re.findall(r"^0xd9\s+([0-9a-f]{2})\s", text, re.MULTILINE)]
        reads = [int(n, 16) for n in re.findall(r"^0x6000([0-9a-f]{4})\r?$", text, re.MULTILINE)]
        payload_reads = list(range(0x6080, 0x6084)) + list(range(0x6080, 0x60fe))
        require(reads == (payload_reads * 257 if mode == "normal" else payload_reads if mode == "bytes" else
                          payload_reads[:4] if mode == "cap" else []),
                "Fixture did not read exactly its accepted TX bytes, or read unconfirmed/unknown bytes")
        require(all(byte == 0xee for byte in strobes), "Fixture executed a non-TX-clear RFST strobe")
        expected_strobes = 514 if mode == "normal" else int(mode in ("bytes", "cap"))
        require(len(strobes) == expected_strobes, "FIFO repeated/implicit clear")
        if mode == "normal":
            require(record["completed"] == ram[0x1e08] == 1 and record["stage"] == 5 and
                    writes == list((b"\x05\x13\x57\xa9" + b"\x7f" + bytes(i ^ 0x69 for i in range(125))) * 257),
                    "257 FIFO cycles did not execute exact RFD bytes/counter wrap")
        else:
            require(final == snapshot(parts, 5010), "FIFO terminal fault changed CPU/RAM")
            require(record["phase"] == 4 and record["completed"] == ram[0x1e08] == 0,
                    "FIFO fault fabricated completed work")
            if mode == "timeout":
                from check_radio_fifo_hardware import check_timeout
                check_timeout(record)
                live, stack, regs = snapshot(parts, 4000)
                read = lambda a, n: stack[a - 0x1f00:a - 0x1f00 + n] if a >= 0x1f00 else live[a:a + n]
                inspect_deadline(proof, read, regs[1], regs[2], regs[0x12])
                require(writes == [5], "Timeout appended bytes after unconfirmed PHR")
            elif mode == "clock":
                require(record["reason"] == 3 and record["clock_result"] == 4 and
                        record["clock"]["rollback_result"] == 9 and record["fifo_result"] == 255 and
                        not record["radio_valid"] and not writes, "Clock failure lost original/rollback evidence")
            else:
                reason, result = {"controller": (4, 6), "bytes": (6, 0), "cap": (4, 9), "flags": (5, 255)}[mode]
                require(record["reason"] == reason and record["fifo_result"] == result, "Wrong FIFO fault reason")
                if mode == "bytes":
                    require(record["checked"] == record["mismatch_index"] == 125 and len(writes) == 130,
                            "Maximum payload verification did not reach its last byte")
    print(f"{board}: FIFO fixture 257 genuine C cycles, CODE/XDATA payloads, exact RFD/EE trace, "
          "deadline/clock/controller/byte/cap/flag faults and alias/stack guards PASS "
          "(synthetic clock/FIFO/CSP only; no hardware evidence).")
