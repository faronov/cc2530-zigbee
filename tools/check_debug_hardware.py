#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Explicit non-RF fixture acceptance on real hardware. Never flashes a device."""

import argparse
from dataclasses import asdict, replace
import hashlib
import json
from pathlib import Path
import sys

from cc2530_debug import READ_STATUS, Status
from cc_debugger import Access, Debugger, DebuggerError, PyUsbBackend, UsbAddress
from debug_image import DebugImage, decode_bootstrap, decode_fixture, expected_fixture
from verify_firmware import BOARDS, CODE_LIMIT, require


def validate_program(image: DebugImage, program: bytes, cycles: int) -> None:
    require(image.image_name == "debug_fixture", "Acceptance requires the board debug_fixture image")
    require(type(cycles) is int and 1 <= cycles <= 257, "Fixture cycles must be in 1..257")
    require(isinstance(program, bytes) and 0 < len(program) <= CODE_LIMIT
            and len(program) == image.metrics["image_extent_bytes"]
            and hashlib.sha256(program).hexdigest() == image.sha256,
            "Program differs from the checked debug image")


def wait_breakpoint(debugger: Debugger, expected_pc: int) -> None:
    with debugger._target_operation() as deadline:
        while True:
            status = debugger._exchange_byte(READ_STATUS, deadline)
            debugger._check_status(status, active=True)
            if status & Status.CPU_HALTED:
                require(status & Status.HALT_STATUS, "Halt was not caused by a breakpoint")
                require(debugger._pc(deadline) == expected_pc, "Unexpected breakpoint PC")
                return


def check_alias(debugger: Debugger) -> None:
    # Only the verified, interrupt-free fixture is allowed here. DATA 0x40 is
    # above its bounded call chain; the public writer deliberately forbids aliases.
    with debugger._stopped_operation(memory_access=True, memory_write=True) as deadline:
        with debugger._preserve_registers(deadline):
            original = debugger._instruction(b"\xe5\x40", deadline)
            debugger._instruction(b"\x75\x92\x00", deadline)
            debugger._instruction(b"\x90\x1f\x40", deadline)
            require(debugger._instruction(b"\xe0", deadline) == original, "Initial IRAM alias mismatch")
            debugger._instruction(b"\x75\x40\xa6", deadline)
            require(debugger._instruction(b"\xe0", deadline) == 0xA6, "DATA-to-XDATA alias mismatch")
            debugger._instruction(b"\x74\x5a", deadline)
            debugger._instruction(b"\xf0", deadline)
            require(debugger._instruction(b"\xe5\x40", deadline) == 0x5A, "XDATA-to-DATA alias mismatch")
            debugger._instruction(bytes((0x75, 0x40, original)), deadline)
            require(debugger._instruction(b"\xe0", deadline) == original, "Alias scratch was not restored")


def exercise(debugger: Debugger, image: DebugImage, program: bytes, cycles: int) -> dict:
    validate_program(image, program, cycles)
    adapter = debugger.read_adapter_state()
    debugger.attach_reset()
    require(debugger.read_pc() == 0, "Initial reset PC is not zero")
    config = debugger.read_debug_config()
    require(config == 0x26, "Fixture acceptance requires the documented reset debug configuration")
    for offset in range(0, len(program), 128):
        expected = program[offset:offset + 128]
        require(debugger.read_code(offset, len(expected)) == expected, "Physical fixture CODE mismatch")

    stop = image.symbol("_debug_fixture_stop").address
    state = image.symbol("_debug_fixture_state").address
    stages = [image.symbol(f"_debug_fixture_stage{index}").address for index in range(4)]
    for slot, address in enumerate(stages):
        debugger.set_breakpoint(slot, address)
    hits = []
    for slot, address in enumerate(stages):
        debugger.resume()
        wait_breakpoint(debugger, address)
        registers = debugger.read_registers()
        require(registers.pc == address, "Stage snapshot PC mismatch")
        require(registers.sp == image.metrics["iram_stack_start"] + 3 + 2 * slot,
                "Stage call-chain depth mismatch")
        hits.append({"slot": slot, "pc": address, "sp": registers.sp})
        debugger.set_breakpoint(slot, address, False)

    debugger.set_breakpoint(0, stop)
    debugger.resume()
    wait_breakpoint(debugger, stop)
    immutable_boot = None
    scratch_address = 0x1D00
    scratch = debugger.read_xdata(scratch_address, 16)
    debugger.write_xdata(scratch_address, bytes(range(16)))
    require(debugger.read_xdata(scratch_address, 16) == bytes(range(16)), "RAM write mismatch")
    debugger.write_xdata(scratch_address, scratch)
    require(debugger.read_xdata(scratch_address, 16) == scratch, "RAM scratch was not restored")
    check_alias(debugger)

    for cycle in range(cycles):
        registers = debugger.read_registers()
        require(registers.pc == stop and registers.a == 0xA5 and registers.b == 0x3C,
                "Known probe PC/A/B mismatch")
        require(registers.dptr0 == 0x1234 and registers.dps == 0 and registers.r[7] == 0x69,
                "Known probe DPTR/DPS/R7 mismatch")
        require(registers.psw & 0x98 == 0x80, "Known probe carry/register bank mismatch")
        require(registers.sp == image.metrics["iram_stack_start"] + 1, "Probe stack depth mismatch")
        data = debugger.read_xdata(state, 16)
        require(data == expected_fixture(cycle), "Fixture cycle/checkpoint mismatch")
        decode_fixture(data)
        bootstrap = debugger.read_xdata(0x1E00, 32)
        decoded = decode_bootstrap(bootstrap, image.board)
        require(decoded["heartbeat"] == (cycle + 1) & 255, "Bootstrap heartbeat mismatch")
        immutable = bootstrap[:8] + bootstrap[9:]
        if immutable_boot is None:
            immutable_boot = immutable
        require(immutable == immutable_boot, "Immutable bootstrap status changed")
        require(debugger.read_registers() == registers, "Inspection changed CPU registers")
        step = debugger.step()
        require(step.accumulator == registers.a, "NOP changed the accumulator")
        require(debugger.read_registers() == replace(registers, pc=stop + 1), "NOP changed CPU state")
        require(debugger.read_xdata(state, 16) == data, "NOP changed fixture state")
        if cycle + 1 < cycles:
            debugger.resume()
            wait_breakpoint(debugger, stop)

    require(debugger.read_xdata(scratch_address, 16) == scratch, "Fixture corrupted unused XDATA")
    debugger.set_breakpoint(0, stop, False)
    debugger.resume()
    halt = debugger.halt()
    require(halt.command_sent and not halt.status_after & Status.HALT_STATUS,
            "Explicit HALT was not observed")
    debugger.reset_halt()
    require(debugger.read_pc() == 0, "Explicit reset PC is not zero")
    for slot in range(4):
        debugger.set_breakpoint(slot, stop, False)
    debugger.set_breakpoint(0, stop)
    debugger.resume()
    wait_breakpoint(debugger, stop)
    require(debugger.read_xdata(state, 16) == expected_fixture(0), "Reset did not reinitialize fixture")
    return {
        "evidence": "hardware-observed",
        "board": image.board,
        "image_sha256": image.sha256,
        "adapter": asdict(adapter),
        "debug_config": config,
        "breakpoint_hits": hits,
        "verified_cycles": cycles,
        "register_preservation": True,
        "nop_pc_delta": 1,
        "xdata_write_readback": True,
        "bidirectional_iram_alias": True,
        "initial_and_explicit_reset_pc": 0,
        "final_pc": stop,
        "final_cpu": "halted-at-fixture-probe",
        "not_tested_by_this_run": ["physical USB disconnect", "interrupted flash recovery", "banked CODE"],
    }


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bus", type=int, required=True)
    parser.add_argument("--address", type=int, required=True)
    parser.add_argument("--board", choices=tuple(BOARDS), required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cycles", type=int, default=257)
    parser.add_argument("--confirm-fixture-test", action="store_true",
                        help="authorize reset, CPU control, temporary fixture RAM writes and all breakpoints")
    args = parser.parse_args(argv)
    try:
        require(args.confirm_fixture_test, "Manual hardware acceptance requires --confirm-fixture-test")
        require(1 <= args.cycles <= 257, "Fixture cycles must be in 1..257")
        address = UsbAddress(args.bus, args.address)
        image = DebugImage(args.output, args.board, "debug_fixture")
        program = (args.output / "debug_fixture.bin").read_bytes()
        validate_program(image, program, args.cycles)
        with Debugger(PyUsbBackend.load(), Access.RESET_DEBUG_SESSION, timeout_ms=10_000,
                      allow_cpu_control=True, allow_target_reset=True,
                      allow_memory_access=True, allow_memory_write=True,
                      allow_breakpoints=True) as debugger:
            debugger.open(address)
            result = exercise(debugger, image, program, args.cycles)
    except (DebuggerError, ValueError, OSError) as error:
        print(f"hardware-check: {error}", file=sys.stderr)
        return 1
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
