#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Execute linked non-RF fixtures with the CC2530 XDATA/IRAM alias modeled."""

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from verify_firmware import (
    BOARDS, IMAGES, TIMEBASE_CHECKPOINTS, CLOCK_CHECKPOINTS, CODE_LIMIT, STATUS_ADDRESS, STATUS_RESERVED,
    require, verify_artifacts, xdata_ranges,
)
from debug_image import DebugImage, decode_bootstrap, decode_fixture, decode_timebase_fixture, expected_fixture


ALIAS = "memory create addressdecoder xram 0x1f00 0x1fff iram_chip 0"


def verify_component_layout(image, symbols, debug, memory, result_name, sources, *, code_holes=()):
    """Shared strict layout for isolated components with an eight-byte result."""
    require(image and min(image) == 0 and max(image) < CODE_LIMIT,
            "Component test CODE is not lower unbanked")
    require(image[0] == 2, "Component test reset vector is not LJMP")
    require(symbols.get("_" + result_name) == STATUS_ADDRESS, "Component result address changed")
    sizes = re.findall(rf"^S:G\${re.escape(result_name)}\$[^(\n]+\(\{{(\d+)\}}", debug, re.MULTILINE)
    require(sizes and all(int(size) == 8 for size in sizes), "Component result debug ABI size changed")
    require(all(f"C${source}$" in debug for source in sources), "Missing component source records")
    require(symbols["__XPAGE"] == 0x93, "Component test must use CC2530 MPAGE")
    require(symbols["l_PSEG"] == symbols["l_XISEG"] == symbols["l_XABS"] == 0,
            "Component test has unaccounted paged/initialized/absolute XDATA")
    ordinary = set()
    for start, end in xdata_ranges(symbols):
        require(0 <= start <= end <= STATUS_ADDRESS, "Component XDATA overlaps status/IRAM alias")
        require(not ordinary.intersection(range(start, end)), "Overlapping component XDATA areas")
        ordinary.update(range(start, end))
    require(len(ordinary) + STATUS_RESERVED <= 512, "Component exceeds 512-byte XDATA reservation budget")
    for match in re.finditer(r"^S:G\$([^$]+)\$[^(\n]+\(\{(\d+)\}[^)\n]+\),F,", debug, re.MULTILINE):
        name, size = match[1], int(match[2])
        if name == result_name:
            continue
        require("_" + name in symbols, f"Missing component XDATA symbol {name}")
        start = symbols["_" + name]
        require(set(range(start, start + size)) <= ordinary, "Unaccounted component XDATA object")
    stack = re.search(
        r"Stack starts at: 0x([0-9a-fA-F]+) \(sp set to 0x([0-9a-fA-F]+)\)"
        r" with (\d+) bytes available", memory,
    )
    require(stack is not None, "Missing component IRAM stack accounting")
    start, sp, size = int(stack[1], 16), int(stack[2], 16), int(stack[3])
    require(start == symbols["s_SSEG"] == symbols["__start__stack"]
            and size == symbols["l_SSEG"] and sp + 1 == start
            and 8 <= start < 128 and size >= 128 and start + size == 256,
            "Invalid component IRAM stack reservation")
    flash = re.search(
        r"ROM/EPROM/FLASH\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+(\d+)\s+(\d+)", memory,
    )
    require(set(range(max(image) + 1)) - image.keys() == set(code_holes),
            "Component has unexpected missing/emitted CODE bytes")
    require(flash is not None and int(flash[1], 16) == 0 and int(flash[2], 16) == max(image)
            and int(flash[3]) == len(image) + len(code_holes) and int(flash[4]) == CODE_LIMIT,
            "Component linked flash accounting mismatch")
    return ordinary | set(range(STATUS_ADDRESS, STATUS_ADDRESS + 8))


def marker(number):
    return f"expression /0 0x2530{number:04x}"


def section(text, number):
    start = f"0x2530{number:04x}"
    end = f"0x2530{number + 1:04x}"
    match = re.search(rf"^{start}\r?\n(.*?)^{end}\r?$", text, re.MULTILINE | re.DOTALL)
    require(match is not None, f"Simulator did not emit section {number}")
    return match[1]


def memory_dump(text, start, size):
    values = {}
    for line in text.splitlines():
        match = re.match(r"^0x([0-9a-fA-F]+)\s+((?:[0-9a-fA-F]{2}(?:\s+|$))+)", line)
        if match:
            address = int(match[1], 16)
            for offset, value in enumerate(match[2].split()):
                values[address + offset] = int(value, 16)
    require(all(address in values for address in range(start, start + size)), "Incomplete simulator memory dump")
    return bytes(values[address] for address in range(start, start + size))


def simulate(simulator, commands, image=None):
    # A command file avoids concurrent stdin echo interleaving with memory dumps.
    with tempfile.TemporaryDirectory(prefix="cc2530-sim-") as directory:
        script = Path(directory) / "commands"
        script.write_text("\n".join(commands + ["quit", ""]), encoding="ascii")
        argv = [simulator, "-t", "C52", "-q", "-c", "-"]
        if image is not None:
            argv.append(str(image))
        result = subprocess.run(
            argv, input=f'exec "{script}"\n', capture_output=True, text=True,
            timeout=15, check=True,
        )
    require(not re.search(r"Unknown command|No such command|Syntax error|Error:", result.stdout, re.IGNORECASE),
            "Simulator rejected a command")
    return result.stdout


def check_alias(simulator, alias=True):
    # Original synthetic MOVX fixture: overwriting XDATA 1F07 must overwrite R7.
    text = simulate(simulator, ([ALIAS] if alias else []) + [
        "set memory rom 0 0x7f 0x07 0x90 0x1f 0x07 0xe4 0xf0 0x80 0xfe",
        "step 4", marker(1), "dump /h iram 7 7", marker(2),
        "set memory iram 0xa0 0x96", marker(3),
        "dump /h xram 0x1fa0 0x1fa0", marker(4),
    ])
    require(memory_dump(section(text, 1), 7, 1) == b"\0", "Alias failed to corrupt the CPU register bank")
    require(memory_dump(section(text, 3), 0x1FA0, 1) == b"\x96", "Reverse IRAM/XDATA alias is missing")


def check_artifact_rejections(output, board, image_name="bringup"):
    case = unittest.TestCase()
    with tempfile.TemporaryDirectory(prefix="cc2530-m0-artifacts-") as directory:
        work = Path(directory)
        for extension in ("ihx", "hex", "bin", "map", "mem", "cdb"):
            shutil.copyfile(output / f"{image_name}.{extension}", work / f"{image_name}.{extension}")
        other_board = next(name for name in BOARDS if name != board)
        with case.assertRaisesRegex(ValueError, "board identity"):
            verify_artifacts(work, other_board, image_name)
        mutations = (
            ("bin", lambda data: data + b"\0", "HEX/BIN"),
            ("map", lambda data: data.replace(b"00001E00", b"00001F00"), "alias"),
            ("cdb", lambda data: data.replace(b"{32}ST", b"{64}ST"), "ABI"),
            ("mem", lambda data: re.sub(rb"(\d+) bytes available",
                                       lambda match: str(int(match[1]) - 1).encode() + b" bytes available", data),
             "stack"),
        )
        if image_name == "debug_fixture":
            mutations += (
                ("cdb", lambda data: data.replace(b"{16}ST", b"{15}ST"), "Fixture debug ABI"),
                ("map", lambda data: data.replace(b"_debug_fixture_stop ", b"_missing_fixture_stop "), "symbol"),
            )
        elif image_name == "timebase_fixture":
            mutations += (
                ("map", lambda data: data.replace(b"_timebase_fixture_ready_stop ", b"_missing_timebase_stop "),
                 "symbol"),
                ("cdb", lambda data: data.replace(b"S:Ltimebase.timebase_read_awake_ticks24$low$",
                                                  b"S:Ltimebase.timebase_read_awake_ticks24$missing$"),
                 "scratch declaration"),
            )
        elif image_name == "clock_fixture":
            mutations += (
                ("map", lambda data: data.replace(b"_clock_fixture_ready_stop ", b"_missing_clock_stop "), "symbol"),
                ("cdb", lambda data: data.replace(b"{56}ST", b"{55}ST"), "ABI"),
                ("cdb", lambda data: data.replace(b"L:XG$timebase_deadline_after$", b"L:XG$missing$"), "CDB"),
            )
        elif image_name == "irq_fixture":
            mutations += (
                ("map", lambda data: data.replace(b"_irq_fixture_ready_stop ", b"_missing_irq_stop "), "symbol"),
                ("cdb", lambda data: data.replace(b"{64}ST", b"{63}ST"), "ABI"),
            )
        elif image_name == "radio_fifo_fixture":
            mutations += (
                ("map", lambda data: data.replace(b"_radio_fifo_fixture_ready ", b"_missing_fifo_ready "), "symbol"),
                ("cdb", lambda data: data.replace(b"{108}ST", b"{107}ST"), "ABI"),
                ("cdb", lambda data: data.replace(b"{57}S:S$checked", b"{56}S:S$checked"), "ABI"),
            )
        elif image_name == "dma_fixture":
            mutations += (
                ("map", lambda data: data.replace(b"_dma_fixture_ready ", b"_missing_dma_ready "), "symbol"),
                ("cdb", lambda data: data.replace(b"{116}ST", b"{115}ST"), "ABI"),
                ("cdb", lambda data: data.replace(b"{80}S:S$fault_latch", b"{79}S:S$fault_latch"), "ABI"),
            )
        for extension, mutate, message in mutations:
            path = work / f"{image_name}.{extension}"
            original = path.read_bytes()
            modified = mutate(original)
            require(modified != original, f"Artifact mutation did not apply: {extension}")
            path.write_bytes(modified)
            with case.assertRaisesRegex(ValueError, message):
                verify_artifacts(work, board, image_name)
            path.write_bytes(original)
        for extension in ("ihx", "hex"):
            path = work / f"{image_name}.{extension}"
            original = path.read_text(encoding="ascii")
            modified = original.replace(":00000001FF", ":01800000007F\n:00000001FF")
            require(modified != original, "CODE-boundary mutation did not apply")
            path.write_text(modified, encoding="ascii")
        with case.assertRaisesRegex(ValueError, "unbanked CODE"):
            verify_artifacts(work, board, image_name)


def boot_commands(symbols):
    commands = [
        ALIAS, "fill xram 0 0x1eff 0xa5",
        "set memory sfr 0x80 0xff", "set memory sfr 0x90 0xff", "set memory sfr 0xa0 0xff",
    ]
    for address in (0x8F, 0x9A, 0xA8, 0xB8, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xFD, 0xFE, 0xFF):
        commands.append(f"set memory sfr {address:#x} 0")
    commands += [
        "set memory sfr 0xc6 0xc9", "set memory sfr 0x9e 0xc9",
        f"run 0 {symbols['_main']:#x}",
        # Normal CRT clears IRAM; seed the unused upper stack guard afterwards.
        "fill iram 0x80 0xff 0xc7",
    ]
    return commands


def expected_status(board):
    lg = BOARDS[board]
    return bytes(
        [ord("M"), ord("0"), ord("C"), ord("C"), 1, 32, 2, lg, 0, lg]
        + ([0x43, 0xFD, 0xFF, 0xBC, 2, 0] if lg else [0xFF, 0xFF, 0xFF, 0, 0, 0])
        + [0, 0, 0, 0xBC if lg else 0, 0, 0, 0, 0, 0xC9, 0xC9, 0, 0, 0, 0, 0, 0]
    )


def check_guards(ram, upper_iram, sfr, symbols):
    allocated = set(range(0x1E00, 0x1E20))
    for start, end in xdata_ranges(symbols):
        allocated.update(range(start, end))
    require(all(value == 0xA5 for address, value in enumerate(ram) if address not in allocated),
            "Image wrote outside its allocated nonaliased XDATA")
    require(upper_iram == b"\xC7" * 128, "Image reached the upper IRAM stack guard")
    require(sfr[0xA8 - 0x80] == sfr[0xB8 - 0x80] == sfr[0x9A - 0x80] == 0,
            "Interrupts became enabled")
    require(symbols["s_SSEG"] - 1 <= sfr[1] < 0x80, "Invalid final stack pointer")


def check_boot(simulator, output, board, symbols):
    commands = boot_commands(symbols) + [
        f"run {symbols['_main']:#x} {symbols['_bringup_tick']:#x}",
        marker(10), "dump /h xram 0x1e00 0x1e1f", marker(11),
        "step 40", marker(12), "dump /h xram 0 0x1eff", marker(13),
        "dump /h iram 0x80 0xff", marker(14),
        "dump /h sfr 0x80 0xff", marker(15),
    ]
    text = simulate(simulator, commands, output / "bringup.ihx")
    first = memory_dump(section(text, 10), 0x1E00, 32)
    ram = memory_dump(section(text, 12), 0, 0x1F00)
    second = ram[0x1E00:0x1E20]
    require(first == expected_status(board), f"{board}: unexpected initial status {first.hex()}")
    require(0 < second[8] <= 40 and second[:8] + second[9:] == first[:8] + first[9:],
            "Heartbeat did not progress, or changed immutable status")
    check_guards(ram, memory_dump(section(text, 13), 0x80, 128),
                 memory_dump(section(text, 14), 0x80, 128), symbols)
    print(f"{board}: actual-image alias-aware boot, status, heartbeat, XDATA/stack guards PASS (simulation only).")


def snapshot_commands(number):
    return [
        marker(number), "state", "dump /h xram 0 0x1eff",
        marker(number + 1), "dump /h iram 0 0xff",
        marker(number + 2), "dump /h sfr 0x80 0xff", marker(number + 3),
    ]


def snapshot(text, number):
    return (memory_dump(section(text, number), 0, 0x1F00),
            memory_dump(section(text, number + 1), 0, 256),
            memory_dump(section(text, number + 2), 0x80, 128))


def check_pc(text, address):
    match = re.search(r"CPU state= OK PC= 0x([0-9a-fA-F]+)", text)
    require(match is not None and int(match[1], 16) == address, "Unexpected simulator PC")


def check_debug_fixture(simulator, output, board, symbols):
    start = symbols["_debug_fixture_state"]
    cycle_address = symbols["_debug_fixture_cycle"]
    stop = symbols["_debug_fixture_stop"]
    stages = [symbols[f"_debug_fixture_stage{i}"] for i in range(4)]
    commands = boot_commands(symbols) + [
        f"run {symbols['_main']:#x} {cycle_address:#x}",
        marker(20), "dump /h xram 0x1e00 0x1e1f",
        f"dump /h xram {start:#x} {start + 15:#x}", marker(21),
    ]
    previous = cycle_address
    for index, address in enumerate(stages):
        commands += [
            f"run {previous:#x} {address:#x}", marker(30 + index * 2),
            "state", "dump /h sfr 0x81 0x81", marker(31 + index * 2),
        ]
        previous = address
    commands += [f"run {previous:#x} {stop:#x}"] + snapshot_commands(40)
    commands += ["step 1"] + snapshot_commands(44)
    # Execute real returns/calls rather than restarting main or overwriting PC.
    for cycle in range(1, 257):
        number = 100 + cycle * 2
        commands += [
            f"run {stop + 1:#x} {stop:#x}", marker(number),
            f"dump /h xram {start:#x} {start + 15:#x}",
            "dump /h xram 0x1e08 0x1e08", marker(number + 1), "step 1",
        ]
    commands += snapshot_commands(620)
    text = simulate(simulator, commands, output / "debug_fixture.ihx")
    require(memory_dump(section(text, 20), 0x1E00, 32) == expected_status(board),
            "Debug fixture changed the M0 startup status")
    decode_bootstrap(memory_dump(section(text, 20), 0x1E00, 32), board)
    initial = b"M1DB" + bytes([1, 16, 1, 0, 0, 0, 0, 0, 0, 0, 0x69, 0x96])
    require(memory_dump(section(text, 20), start, 16) == initial, "Wrong fixture initialization")
    decode_fixture(memory_dump(section(text, 20), start, 16))
    for index, address in enumerate(stages):
        entry = section(text, 30 + index * 2)
        check_pc(entry, address)
        sp = memory_dump(entry, 0x81, 1)[0]
        require(sp == symbols["s_SSEG"] - 1 + 4 + index * 2, "Fixture call-chain depth changed")

    before, after = snapshot(text, 40), snapshot(text, 44)
    check_pc(section(text, 40), stop)
    check_pc(section(text, 44), stop + 1)
    require(before == after, "Probe NOP changed RAM/registers")
    ram, iram, sfr = before
    require(ram[start:start + 16] == expected_fixture(0), "Wrong first fixture cycle")
    require(sfr[0xE0 - 0x80] == 0xA5 and sfr[0xF0 - 0x80] == 0x3C
            and sfr[2:4] == b"\x34\x12" and iram[7] == 0x69
            and sfr[0xD0 - 0x80] & 0x98 == 0x80, "Wrong probe registers/bank/carry")
    require(sfr[1] == symbols["s_SSEG"] + 1, "Probe stack did not unwind")
    check_guards(ram, iram[128:], sfr, symbols)
    for cycle in range(1, 257):
        sample = section(text, 100 + cycle * 2)
        require(memory_dump(sample, start, 16) == expected_fixture(cycle),
                f"Fixture mismatch after resumed cycle {cycle}")
        decode_fixture(memory_dump(sample, start, 16))
        require(memory_dump(sample, 0x1E08, 1)[0] == (cycle + 1) & 255,
                "Fixture heartbeat mismatch")
    final_ram, final_iram, final_sfr = snapshot(text, 620)
    check_guards(final_ram, final_iram[128:], final_sfr, symbols)
    require(final_sfr[1] == sfr[1], "Resumed fixture leaked stack")
    require(final_ram[0x1E00:0x1E20] == ram[0x1E00:0x1E20], "M0 status changed across 256 cycles")
    print(f"{board}: debug fixture, four code locations, nested calls, NOP PC+1, "
          "registers, 257 cycles and alias/XDATA/stack guards PASS (simulation only).")


def timer_sfr(ticks):
    return "set memory sfr 0x95 " + " ".join(f"{byte:#x}" for byte in ticks.to_bytes(3, "little"))


def check_timebase_fixture(simulator, output, board, symbols):
    before, ready, fault = (symbols[name] for name in TIMEBASE_CHECKPOINTS)
    poll, reader = symbols["_timebase_fixture_poll"], symbols["_timebase_read_awake_ticks24"]
    state = symbols["_timebase_fixture_state"]
    path = output / "timebase_fixture.ihx"
    commands = boot_commands(symbols) + [f"run {symbols['_main']:#x} {before:#x}"] + snapshot_commands(1)
    for cycle in range(257):
        start = (0, 0xff, 0xffff, 0xffff80, 0xffffff)[cycle % 5]
        elapsed = 0x7fffff if cycle == 256 else 128
        if cycle:
            commands += [f"run {ready:#x} {before:#x}"]
        commands += [
            timer_sfr(start), f"run {before:#x} {poll:#x}",
            timer_sfr((start + 127) & 0xffffff), f"run {poll:#x} {reader:#x}",
            f"run {reader:#x} {poll:#x}", timer_sfr((start + elapsed) & 0xffffff),
            f"run {poll:#x} {ready:#x}", marker(10 + cycle * 2), "state",
            f"dump /h xram {state:#x} {state + 31:#x}", "dump /h xram 0x1e00 0x1e1f",
            "dump /h sfr 0x81 0x81", marker(11 + cycle * 2),
        ]
    commands += snapshot_commands(600)
    text = simulate(simulator, commands, path)
    initial, initial_iram, initial_sfr = snapshot(text, 1)
    require(initial[0x1e00:0x1e20] == expected_status(board), "Timebase changed M0 startup")
    require(decode_timebase_fixture(initial[state:state + 32])["phase"] == 1, "Timebase did not initialize")
    check_guards(initial, initial_iram[128:], initial_sfr, symbols)
    for cycle in range(257):
        entry = section(text, 10 + cycle * 2)
        check_pc(entry, ready)
        record = decode_timebase_fixture(memory_dump(entry, state, 32))
        start = (0, 0xff, 0xffff, 0xffff80, 0xffffff)[cycle % 5]
        require(record["phase"] == 3 and record["start"] == start
                and record["elapsed"] == (0x7fffff if cycle == 256 else 128) and record["polls"] == 2
                and record["completed_cycles"] == (cycle + 1) & 255, "Wrong compiled timebase cycle")
        boot = memory_dump(entry, 0x1e00, 32)
        decode_bootstrap(boot, board)
        require(boot[8] == (cycle + 1) & 255 and boot[:8] + boot[9:] ==
                initial[0x1e00:0x1e08] + initial[0x1e09:0x1e20], "Timebase heartbeat/M0 changed")
        require(memory_dump(entry, 0x81, 1)[0] == symbols["s_SSEG"] + 1, "Timebase cycle leaked stack")
    ram, iram, sfr = snapshot(text, 600)
    check_guards(ram, iram[128:], sfr, symbols)
    guarded = [address for name, address in symbols.items()
               if name.startswith("_SOC_") and not name.startswith("_SOC_ST")]
    require(all(sfr[address - 0x80] == initial_sfr[address - 0x80] for address in guarded),
            "Timebase changed GPIO/clock/IRQ registers")

    for samples, reason, helper, polls in (((0x100,), 4, 0, 1024), ((0xff,), 3, 0, 1),
                                           ((0x800100,), 3, 0, 1), ((0x800180,), 2, 2, 1),
                                           ((0x110, 0x10f), 3, 0, 2)):
        commands = boot_commands(symbols) + [
            timer_sfr(0x100), f"run {symbols['_main']:#x} {poll:#x}",
        ]
        for sample in samples[:-1]:
            commands += [timer_sfr(sample), f"run {poll:#x} {reader:#x}", f"run {reader:#x} {poll:#x}"]
        end = samples[-1]
        commands += [timer_sfr(end), f"run {poll:#x} {fault:#x}"]
        commands += snapshot_commands(1) + ["step 64"] + snapshot_commands(5)
        text = simulate(simulator, commands, path)
        check_pc(section(text, 1), fault)
        check_pc(section(text, 5), fault)
        failed = snapshot(text, 1)
        require(failed == snapshot(text, 5), "Terminal fault loop changed RAM/CPU state")
        ram, iram, sfr = failed
        record = decode_timebase_fixture(ram[state:state + 32])
        require(record["phase"] == 4 and record["reason"] == reason
                and record["helper_status"] == helper and record["polls"] == polls
                and record["completed_cycles"] == 0, "Wrong compiled timebase fault")
        require(ram[0x1e00:0x1e20] == expected_status(board), "Fault advanced heartbeat/changed M0")
        require(sfr[1] == symbols["s_SSEG"] + 1, "Fault stack did not unwind")
        require(sfr[0x15:0x18] == end.to_bytes(3, "little"), "Fixture wrote Sleep Timer SFRs")
        require(all(sfr[address - 0x80] == initial_sfr[address - 0x80] for address in guarded),
                "Fault changed GPIO/clock/IRQ registers")
        check_guards(ram, iram[128:], sfr, symbols)
    print(f"{board}: timebase fixture, 257 real C cycles, rollover, stopped/backward/ambiguous counter "
          "faults and alias/XDATA/stack guards PASS (synthetic SFR simulation only).")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board", choices=BOARDS, required=True)
    parser.add_argument("--image", choices=IMAGES, default="bringup")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--simulator", default="s51")
    args = parser.parse_args()
    _, symbols = verify_artifacts(args.output, args.board, args.image)
    debug_image = DebugImage(args.output, args.board, args.image)
    require(debug_image.symbol("_m0_status").space == "XDATA", "M0 symbol space mismatch")
    require(debug_image.symbol("_SOC_P0").space == "SFR", "SFR symbol space mismatch")
    require(debug_image.symbol("_board_description").kind == "object", "CODE data misclassified")
    source_file = Path(__file__).resolve().parents[1] / "examples" / f"{args.image}.c"
    if args.image in ("radio_fifo_fixture", "dma_fixture"):
        source_file = Path(__file__).resolve().parents[1] / "src" / f"{args.image}_state.c"
    main_lines = debug_image.source_lines(pc=symbols["_main"])
    require(any(location.file == source_file.name for location in main_lines), "Main source mapping missing")
    if args.image == "debug_fixture":
        require(debug_image.symbol("_debug_fixture_state").size == 16, "M1 symbol size mismatch")
        pattern_source = (Path(__file__).resolve().parents[1] / "src" / "debug_pattern.c").read_text(
            encoding="ascii").splitlines()
        for slot in range(4):
            name = f"_debug_fixture_stage{slot}"
            require(debug_image.symbol(name).kind == "function", "Fixture function misclassified")
            require(debug_image.breakpoint(name, slot)["address"] == symbols[name], "Breakpoint lookup mismatch")
            line = next(index for index, text in enumerate(pattern_source, 1)
                        if text.startswith(f"uint8_t {name[1:]}("))
            locations = debug_image.source_lines(file="debug_pattern.c", line=line)
            require(any(location.address == symbols[name] for location in locations), "Stage source mapping mismatch")
    elif args.image == "timebase_fixture":
        require(debug_image.symbol("_timebase_fixture_state").size == 32, "Timebase symbol size mismatch")
        source = source_file.read_text(encoding="ascii").splitlines()
        for slot, name in enumerate(TIMEBASE_CHECKPOINTS):
            require(debug_image.symbol(name).kind == "function", "Timebase checkpoint is not a function")
            require(debug_image.breakpoint(name, slot)["address"] == symbols[name], "Timebase breakpoint mismatch")
            line = next(index for index, text in enumerate(source, 1) if text.startswith(f"void {name[1:]}("))
            require(any(location.address == symbols[name] for location in
                        debug_image.source_lines(file=source_file.name, line=line)), "Timebase source mismatch")
    elif args.image == "clock_fixture":
        require(debug_image.symbol("_clock_fixture_state").size == 56, "Clock symbol size mismatch")
        source = source_file.read_text(encoding="ascii").splitlines()
        for slot, name in enumerate(CLOCK_CHECKPOINTS):
            require(debug_image.symbol(name).kind == "function", "Clock checkpoint is not a function")
            require(debug_image.breakpoint(name, slot)["address"] == symbols[name], "Clock breakpoint mismatch")
            line = next(index for index, text in enumerate(source, 1) if text.startswith(f"void {name[1:]}("))
            require(any(location.address == symbols[name] for location in
                        debug_image.source_lines(file=source_file.name, line=line)), "Clock source mismatch")
    elif args.image == "irq_fixture":
        require(debug_image.symbol("_irq_fixture_state").size == 64, "IRQ symbol size mismatch")
        require(debug_image.symbol("_irq_ea").space == "SBIT" and
                debug_image.symbol("_IRQ_T1STAT").space == "SFR", "EA bit/T1STAT SFR spaces were conflated")
    elif args.image == "radio_fifo_fixture":
        require(debug_image.symbol("_radio_fifo_fixture_state").size == 108, "FIFO symbol size mismatch")
    elif args.image == "dma_fixture":
        require(debug_image.symbol("_dma_fixture_state").size == 116, "DMA symbol size mismatch")
        require(debug_image.symbol("_dma_fixture_a").size == debug_image.symbol("_dma_fixture_b").size == 18,
                "DMA caller buffer size mismatch")
    check_artifact_rejections(args.output, args.board, args.image)
    check_alias(args.simulator)
    with unittest.TestCase().assertRaisesRegex(ValueError, "register bank"):
        check_alias(args.simulator, alias=False)
    if args.image == "bringup":
        check_boot(args.simulator, args.output, args.board, symbols)
    elif args.image == "debug_fixture":
        check_debug_fixture(args.simulator, args.output, args.board, symbols)
    elif args.image == "timebase_fixture":
        check_timebase_fixture(args.simulator, args.output, args.board, symbols)
    elif args.image == "clock_fixture":
        from boot_clock_fixture import check_clock_fixture
        check_clock_fixture(args.simulator, args.output, args.board, symbols)
    elif args.image == "irq_fixture":
        from boot_irq_fixture import check_irq_fixture
        check_irq_fixture(args.simulator, args.output, args.board, symbols)
    elif args.image == "radio_fifo_fixture":
        from boot_radio_fifo_fixture import check_radio_fifo_fixture
        check_radio_fifo_fixture(args.simulator, args.output, args.board, symbols)
    else:
        from boot_dma_fixture import check_dma_fixture
        check_dma_fixture(args.simulator, args.output, args.board, symbols)


if __name__ == "__main__":
    main()
