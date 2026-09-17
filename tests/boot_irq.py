#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Isolated EA primitives and generic C52 preemption, never CC2530 hardware."""

import argparse
from pathlib import Path
import re
import unittest

from boot_image import (
    ALIAS, check_alias, check_pc, section, simulate, snapshot, snapshot_commands, verify_component_layout,
)
from verify_firmware import (
    CODE_LIMIT, cdb_address, parse_ihex, parse_symbols, require, verify_irq_primitives,
    IRQ_SAVE_BYTES as SAVE, IRQ_RESTORE_BYTES as RESTORE,
)


OBJECTS = ("mode", "token", "return", "low_count", "high_count", "error")
CHECKPOINTS = ("before", "done", "disabled", "before_outer")


def verify_image(image, symbols, debug, memory, module):
    allocated = verify_component_layout(
        image, symbols, debug, memory, "irq_test_result", ("irq.c", "test_irq.c"),
        code_holes=(*range(6, 11), *range(12, 19)),
    )
    verify_irq_primitives(image, symbols, debug)
    require(symbols["_irq_restore"] == symbols["_irq_save_disable"] + len(SAVE)
            and cdb_address(debug, "L:Ftest_irq$self_test$0$0") == symbols["_irq_restore"] + len(RESTORE),
            "IRQ leaves contain unexpected code")
    require(re.search(r"^S:Lirq\.irq_restore\$token\$[^(]+\(\{1\}SC:U\),R,0,0,\[\]$", debug, re.MULTILINE),
            "IRQ token is not a byte/register argument")
    areas = re.findall(r"^A (\S+) size ([0-9A-Fa-f]+) flags \S+ addr \S+$", module, re.MULTILINE)
    require(areas and len({name for name, _ in areas}) == len(areas), "Missing/duplicate IRQ module areas")
    nonzero = {name: int(size, 16) for name, size in areas if int(size, 16)}
    require(nonzero == {"REG_BANK_0": 8, "CSEG": 34}, "IRQ module gained code or RAM/overlay scratch")
    for suffix in OBJECTS:
        name = "irq_test_" + suffix
        require(f"S:G${name}$0_0$0({{1}}SC:U),F,0,0" in debug
                and symbols["_" + name] == cdb_address(debug, f"L:G${name}$0_0$0")
                and symbols["_" + name] < 0x1e00 and symbols["_" + name] in allocated,
                "IRQ test byte object ABI/allocation mismatch")
    require(symbols["l_XSEG"] == len(OBJECTS) and
            [symbols["_irq_test_" + name] for name in OBJECTS] ==
            list(range(symbols["s_XSEG"], symbols["s_XSEG"] + len(OBJECTS))),
            "IRQ test control bytes must be distinct and consecutive")
    for suffix in CHECKPOINTS:
        require(image.get(symbols["_irq_test_" + suffix]) == 0, "IRQ test checkpoint is not NOP")
    nested = cdb_address(debug, "L:Ftest_irq$nested$0$0")
    require(nested < symbols["_irq_test_disabled"] < symbols["_irq_test_before_outer"] <
            cdb_address(debug, "L:XFtest_irq$nested$0$0"), "Nested IRQ checkpoints are outside their C function")

    # The actual SDCC wrappers preserve bits/ACC/B/DPTR/bank-0/PSW and use RETI.
    saved = (0x21, 0xe0, 0xf0, 0x82, 0x83, 7, 6, 5, 4, 3, 2, 1, 0, 0xd0)
    require(symbols.get("s_BIT_BANK") == 0x21 and symbols.get("l_BIT_BANK") == 1,
            "SDCC bit-register bank location/size changed")
    for name, vector, number in (("low", 3, 0), ("high", 0x13, 2)):
        start = symbols["_irq_test_" + name]
        code = b"".join(bytes((0xc0, register)) for register in saved) + b"\x75\xd0\0"
        code += b"\x90" + symbols["_irq_test_" + name + "_count"].to_bytes(2, "big") + b"\xe0\x04\xf0"
        code += b"\x12" + nested.to_bytes(2, "big")
        code += b"".join(bytes((0xd0, register)) for register in reversed(saved)) + b"\x32"
        require(cdb_address(debug, f"L:XG$irq_test_{name}$0$0") == start + len(code) - 1
                and all(image.get(start + i) == byte for i, byte in enumerate(code)),
                "IRQ test ISR context/RETI instructions changed")
        require(bytes(image.get(vector + i, 0xff) for i in range(3)) == b"\x02" + start.to_bytes(2, "big")
                and f"F:G$irq_test_{name}$0_0$0({{2}}DF,SV:S),C,0,0,1,{number},0" in debug,
                "Synthetic C52 vector/ISR ABI mismatch")
    require(image.get(0x0b) == 0x32, "Unused synthetic timer vector must be RETI")
    return allocated


def check_rejections(image, symbols, debug, memory, module):
    case = unittest.TestCase()
    addresses = list(range(symbols["_irq_save_disable"], symbols["_irq_restore"] + len(RESTORE)))
    for name in ("low", "high"):
        addresses += list(range(symbols["_irq_test_" + name],
                                cdb_address(debug, f"L:XG$irq_test_{name}$0$0") + 1))
    addresses += list(range(3, 6)) + list(range(0x13, 0x16)) + [0x0b]
    for address in addresses:
        changed = dict(image)
        changed[address] ^= 1
        with case.assertRaises(ValueError):
            verify_image(changed, symbols, debug, memory, module)
    for key, value in (
        ("_irq_ea", 0xa8), ("_SOC_IEN0", 0xb8), ("_irq_test_result", 0x1f00),
        ("_irq_test_token", 0x1e40), ("_irq_test_token", symbols["_irq_test_mode"]),
        ("_irq_test_done", CODE_LIMIT), ("s_BIT_BANK", 0x20),
        ("s_XSEG", 0x1e00), ("l_XSEG", 449), ("l_XABS", 1), ("l_XISEG", 1),
        ("l_PSEG", 1), ("__XPAGE", 0xa0), ("s_SSEG", 0x80),
    ):
        with case.assertRaises(ValueError):
            verify_image(image, dict(symbols, **{key: value}), debug, memory, module)
    for changed in (
        debug.replace("L:XG$irq_restore", "L:XG$missing"),
        debug.replace("C$irq.c$", "C$missing.c$"),
        debug.replace("({1}SX:U),J", "({1}SC:U),I"),
        debug.replace("({1}SC:U),R,0,0,[]", "({1}SC:U),F,0,0"),
        debug.replace("({8}DA8d,SC:U)", "({9}DA8d,SC:U)"),
    ):
        require(changed != debug, "IRQ metadata mutation did not apply")
        with case.assertRaises(ValueError):
            verify_image(image, symbols, changed, memory, module)
    for area in ("DSEG", "ISEG", "BSEG", "XSEG"):
        with case.assertRaisesRegex(ValueError, "scratch"):
            verify_image(image, symbols, debug, memory, module.replace(f"A {area} size 0 ", f"A {area} size 1 "))
    with case.assertRaises(ValueError):
        verify_image(image | {CODE_LIMIT: 0}, symbols, debug, memory, module)
    with case.assertRaises(ValueError):
        verify_image(image | {6: 0}, symbols, debug, memory, module)
    with case.assertRaises(ValueError):
        verify_image({address: byte for address, byte in image.items() if address != 0x19},
                     symbols, debug, memory, module)
    with case.assertRaises(ValueError):
        verify_image(image, symbols, debug, memory.replace("bytes available", "absent"), module)


def run_to(address):
    return [f"break {address:#x}", "run", f"clear {address:#x}"]


def setup(symbols):
    return ([ALIAS, "fill xram 0 0x1eff 0xa5"] + run_to(symbols["_main"]) +
            ["fill iram 0x80 0xff 0xc7"] + run_to(symbols["_irq_test_before"]))


def state(text, number, pc, symbols, allocated):
    check_pc(section(text, number), pc)
    ram, iram, sfr = snapshot(text, number)
    require(ram[0x1e00:0x1e08] == b"IRQT\x01\x08\0\0", "Linked IRQ self-test/result ABI failed")
    require(ram[symbols["_irq_test_error"]] == 0, "Nested C IRQ ownership check failed")
    require(all(value == 0xa5 for address, value in enumerate(ram) if address not in allocated),
            "IRQ executable wrote outside allocated XDATA")
    require(iram[128:] == b"\xc7" * 128 and symbols["s_SSEG"] - 1 <= sfr[1] < 128,
            "IRQ stack/IRAM alias guard failed")
    return ram, iram, sfr


def check_boundary_preemption(simulator, path, symbols, allocated):
    # All actual leaf instruction boundaries, including invalid tokens while EA=1.
    # Asserted IE0 is a synthetic C52 edge request, not a CC2530 pending flag.
    cases = []
    for enabled, offsets in ((0, (0, 3, 6)), (1, (0, 7, 10))):
        cases += [(0, 0, enabled, offset) for offset in offsets]
    for token, offsets in ((0, (0, 2, 13, 15, 18)), (1, (0, 2, 4, 5, 7, 9, 12)),
                           (2, (0, 2, 4, 5, 19, 22)), (255, (0, 2, 4, 5, 19, 22))):
        cases += [(1, token, 1, offset) for offset in offsets]
    for mode, token, enabled, offset in cases:
        function = symbols["_irq_restore" if mode else "_irq_save_disable"]
        target = function + offset
        commands = setup(symbols) + [
            "set memory sfr 0x88 5", f"set memory sfr 0xa8 {0x81 if enabled else 1}",
            "set memory sfr 0xb8 4", "set memory sfr 0x9a 0x96",
            f"set memory xram {symbols['_irq_test_mode']} {mode} {token}",
        ] + run_to(target) + snapshot_commands(1)
        commands += ["set memory sfr 0x88 7"] + run_to(symbols["_irq_test_done"]) + snapshot_commands(5)
        text = simulate(simulator, commands, path)
        before = state(text, 1, target, symbols, allocated)
        after = state(text, 5, symbols["_irq_test_done"], symbols, allocated)
        ram, _, sfr = after
        delivered = int(mode == 1 and (token != 0 or offset < 13))
        result = enabled if mode == 0 else int(token > 1)
        final_ea = 0 if mode == 0 else token if token <= 1 else enabled
        require(ram[symbols["_irq_test_return"]] == result and ram[symbols["_irq_test_token"]] == token,
                f"IRQ argument/return was corrupted by preemption: {mode}/{token}/{offset}")
        require(ram[symbols["_irq_test_low_count"]] == delivered and
                ram[symbols["_irq_test_high_count"]] == 0, "Unexpected C52 preemption/defer outcome")
        require(sfr[0x28] == 1 | (final_ea << 7) and sfr[8] == (5 if delivered else 7)
                and sfr[1] == symbols["s_SSEG"] + 1, "IRQ enable/pending/stack result mismatch")
        for address in range(128):
            if address not in (1, 2, 3, 8, 0x28, 0x50, 0x60, 0x70):
                require(sfr[address] == before[2][address], "IRQ changed unrelated enable/priority/peripheral SFR")
    return len(cases)


def check_nested_preemption(simulator, path, symbols, debug, allocated):
    low, high = symbols["_irq_test_low"], symbols["_irq_test_high"]
    low_end = cdb_address(debug, "L:XG$irq_test_low$0$0")
    high_end = cdb_address(debug, "L:XG$irq_test_high$0$0")
    for priority in (0, 4):
        commands = setup(symbols) + [
            "set memory sfr 0xa8 5", f"set memory sfr 0xb8 {priority}",
            "set memory sfr 0x88 7", f"set memory xram {symbols['_irq_test_mode']} 1 1",
        ] + run_to(low) + snapshot_commands(1)
        commands += run_to(symbols["_irq_test_disabled"]) + snapshot_commands(5)
        commands += ["set memory sfr 0x88 13"] + run_to(symbols["_irq_test_before_outer"]) + snapshot_commands(9)
        if priority == 0:
            commands += run_to(low_end) + snapshot_commands(13)
        commands += run_to(high) + snapshot_commands(17) + run_to(high_end) + snapshot_commands(21)
        commands += ["step 1"] + snapshot_commands(25)
        if priority:
            commands += run_to(low_end) + snapshot_commands(13)
        commands += run_to(symbols["_irq_test_done"]) + snapshot_commands(29)
        # A second low-priority event proves RETI released the in-service state.
        commands += ["set memory sfr 0x88 7"] + run_to(low_end) + snapshot_commands(33)
        text = simulate(simulator, commands, path)
        first = state(text, 1, low, symbols, allocated)
        disabled = state(text, 5, symbols["_irq_test_disabled"], symbols, allocated)
        inner_restored = state(text, 9, symbols["_irq_test_before_outer"], symbols, allocated)
        low_return = state(text, 13, low_end, symbols, allocated)
        high_entry = state(text, 17, high, symbols, allocated)
        high_return = state(text, 21, high_end, symbols, allocated)
        high_resume = int.from_bytes(high_entry[1][high_entry[2][1] - 1:high_entry[2][1] + 1], "little")
        resumed = state(text, 25, high_resume, symbols, allocated)
        finished = state(text, 29, symbols["_irq_test_done"], symbols, allocated)
        repeated = state(text, 33, low_end, symbols, allocated)
        require(disabled[2][0x28] == inner_restored[2][0x28] == 5 and inner_restored[2][8] == 13
                and inner_restored[0][symbols["_irq_test_high_count"]] == 0,
                "Inner restore reenabled outer section or cleared/blocked flag assertion")
        require((high_entry[2][1] > first[2][1]) if priority else (high_entry[2][1] == first[2][1]),
                "Only higher priority may preempt an active C52 ISR")
        require(low_return[0][symbols["_irq_test_high_count"]] == (1 if priority else 0),
                "Equal-priority event nested before RETI")
        require(all(first[2][i] == low_return[2][i] for i in range(128) if i != 8)
                and high_entry[2] == high_return[2],
                "Compiled ISR failed to preserve interrupted SFR/CPU context")
        require(first[1][:first[2][1] + 1] == low_return[1][:first[2][1] + 1] and
                high_entry[1][:high_entry[2][1] + 1] == high_return[1][:high_entry[2][1] + 1],
                "Compiled ISR corrupted interrupted IRAM/register-bank/live-stack state")
        require(resumed[2][1] == high_entry[2][1] - 2, "RETI failed to unwind the hardware frame")
        low_resume = int.from_bytes(first[1][first[2][1] - 1:first[2][1] + 1], "little")
        require(low_resume == symbols["_irq_restore"] + 9 and first[2][2] == 1,
                "Foreground restore was not interrupted between SETB EA and its status return")
        if priority:
            require(high_resume == low_resume and high_entry[2][2] == resumed[2][2] == 1,
                    "Nested restore's live DPL argument was not preserved through preemption/RETI")
        require(finished[0][symbols["_irq_test_low_count"]] == finished[0][symbols["_irq_test_high_count"]] == 1
                and repeated[0][symbols["_irq_test_low_count"]] == 2
                and finished[0][symbols["_irq_test_return"]] == 0
                and finished[2][0x28] == 0x85 and finished[2][1] == symbols["s_SSEG"] + 1,
                "IRQ nesting/return/retrigger result mismatch")
        require(all(record[2][0x38] == priority for record in (first, high_entry, low_return, finished)),
                "IRQ changed priority/source-enable byte")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--simulator", default="s51")
    args = parser.parse_args()
    path = args.output / "irq_test.ihx"
    image = parse_ihex(path.read_text(encoding="ascii"))
    symbols = parse_symbols(path.with_suffix(".map").read_text())
    debug, memory = path.with_suffix(".cdb").read_text(), path.with_suffix(".mem").read_text()
    module = (args.output / "irq.rel").read_text()
    allocated = verify_image(image, symbols, debug, memory, module)
    check_rejections(image, symbols, debug, memory, module)
    check_alias(args.simulator)
    with unittest.TestCase().assertRaisesRegex(ValueError, "register bank"):
        check_alias(args.simulator, alias=False)
    cases = check_boundary_preemption(args.simulator, path, symbols, allocated)
    check_nested_preemption(args.simulator, path, symbols, debug, allocated)
    print(f"IRQ: 34-byte zero-scratch leaves; {len(image)} emitted / {max(image) + 1} reserved test CODE "
          f"(12 vector-padding bytes), {len(allocated) - 8} ordinary XDATA, "
          f"{symbols['l_SSEG']} reserved stack. {cases} instruction-boundary cases, real C52 IRQ entry/"
          "priority nesting/RETI/context preservation and alias/stack guards PASS "
          "(synthetic CPU interrupts, not CC2530 peripheral/hardware evidence).")


if __name__ == "__main__":
    main()
