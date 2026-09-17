#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Explicit manual acceptance of the board timebase fixture. Never flashes."""

import argparse
from dataclasses import asdict, replace
import hashlib
import json
from pathlib import Path
import sys

from cc2530_debug import READ_STATUS, Status
from cc_debugger import Access, Debugger, DebuggerError, PyUsbBackend, UsbAddress
from debug_image import DebugImage, decode_bootstrap, decode_timebase_fixture
from verify_firmware import BOARDS, CODE_LIMIT, TIMEBASE_CHECKPOINTS, TIMEBASE_FIXTURE_SIZE, require


def validate_program(image: DebugImage, program: bytes, cycles: int) -> None:
    require(image.image_name == "timebase_fixture", "Acceptance requires the board timebase_fixture image")
    require(type(cycles) is int and 1 <= cycles <= 257, "Timebase cycles must be in 1..257")
    require(isinstance(program, bytes) and 0 < len(program) <= CODE_LIMIT
            and len(program) == image.metrics["image_extent_bytes"]
            and hashlib.sha256(program).hexdigest() == image.sha256,
            "Program differs from the checked timebase image")


def wait_checkpoint(debugger: Debugger, expected_pc: int, fault_pc: int, label="timebase") -> int:
    # A single guarded operation/deadline covers all status polls and the PC read.
    with debugger._target_operation() as deadline:
        while True:
            status = debugger._exchange_byte(READ_STATUS, deadline)
            debugger._check_status(status, active=True)
            if status & Status.CPU_HALTED:
                require(status & Status.HALT_STATUS, "Halt was not caused by a breakpoint")
                pc = debugger._pc(deadline)
                require(pc in (expected_pc, fault_pc), f"Unexpected {label} breakpoint PC")
                return pc


def inspect_checkpoint(debugger: Debugger, image: DebugImage, pc: int) -> tuple:
    registers = debugger.read_registers()
    require(registers.pc == pc, "Timebase checkpoint PC mismatch")
    require(registers.sp == image.metrics["iram_stack_start"] + 1, "Timebase checkpoint stack mismatch")
    data = debugger.read_xdata(image.symbol("_timebase_fixture_state").address, TIMEBASE_FIXTURE_SIZE)
    boot = debugger.read_xdata(0x1E00, 32)
    record, startup = decode_timebase_fixture(data), decode_bootstrap(boot, image.board)
    require(debugger.read_registers() == registers, "Timebase inspection changed CPU state")
    return record, startup, boot, registers


def step_nop(debugger: Debugger, registers, label="Timebase") -> None:
    result = debugger.step()
    require(result.accumulator == registers.a and
            debugger.read_registers() == replace(registers, pc=registers.pc + 1),
            f"{label} NOP changed CPU state or did not advance PC by one")


def reset_and_verify_code(debugger: Debugger, program: bytes, label="timebase") -> tuple:
    adapter = debugger.read_adapter_state()
    debugger.attach_reset()
    require(debugger.read_pc() == 0, f"Initial {label} reset PC is not zero")
    config = debugger.read_debug_config()
    require(config == 0x26, f"{label.capitalize()} acceptance requires the documented reset debug configuration")
    for offset in range(0, len(program), 128):
        expected = program[offset:offset + 128]
        require(debugger.read_code(offset, len(expected)) == expected, f"Physical {label} CODE mismatch")
    return adapter, config


def exercise(debugger: Debugger, image: DebugImage, program: bytes, cycles: int) -> dict:
    validate_program(image, program, cycles)
    adapter, config = reset_and_verify_code(debugger, program)

    before, ready, fault = (image.symbol(name).address for name in TIMEBASE_CHECKPOINTS)
    for slot in range(4):
        debugger.set_breakpoint(slot, before, False)
    for slot, address in enumerate((before, ready, fault)):
        debugger.set_breakpoint(slot, address)
    debugger.resume()
    pc = wait_checkpoint(debugger, before, fault)
    record, startup, initial_boot, registers = inspect_checkpoint(debugger, image, pc)
    require(pc == before and record["phase"] == 1 and startup["heartbeat"] == 0,
            "Timebase fixture did not reach initialized checkpoint")
    debugger.set_breakpoint(0, before, False)
    step_nop(debugger, registers)
    observations = []
    for cycle in range(1, cycles + 1):
        debugger.resume()
        pc = wait_checkpoint(debugger, ready, fault)
        record, boot_status, boot, registers = inspect_checkpoint(debugger, image, pc)
        if pc == fault or record["phase"] == 4:
            raise ValueError(f"Timebase fixture FAULT: reason={record['reason']} "
                             f"helper={record['helper_status']} polls={record['polls']}")
        require(pc == ready and record["phase"] == 3, "Timebase did not publish READY")
        require(record["completed_cycles"] == cycle & 255 and boot_status["heartbeat"] == cycle & 255,
                "Timebase completed-cycle/heartbeat progression mismatch")
        require(boot[:8] + boot[9:] == initial_boot[:8] + initial_boot[9:], "Immutable M0 status changed")
        observations.append(record)
        if cycle < cycles:
            step_nop(debugger, registers)
    return {
        "evidence": "hardware-observed",
        "scope": "compiled-awake-timebase-fixture",
        "board": image.board,
        "image": image.image_name,
        "image_sha256": image.sha256,
        "adapter": asdict(adapter),
        "debug_config": config,
        "verified_code_bytes": len(program),
        "verified_cycles": cycles,
        "cycles": observations,
        "startup_status": startup,
        "register_preservation": True,
        "initial_reset_pc": 0,
        "final_pc": ready,
        "final_cpu": "halted-at-timebase-ready",
        "not_tested_by_this_run": [
            "clock calibration or precise tick period", "guaranteed natural counter rollover",
            "clock switching", "interrupts or compare", "sleep/wake", "RF", "AES", "flash",
        ],
    }


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bus", type=int, required=True)
    parser.add_argument("--address", type=int, required=True)
    parser.add_argument("--board", choices=tuple(BOARDS), required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cycles", type=int, default=3)
    parser.add_argument("--confirm-timebase-test", action="store_true",
                        help="authorize reset-attach, CPU control, read-only inspection and breakpoints")
    args = parser.parse_args(argv)
    try:
        require(args.confirm_timebase_test, "Manual acceptance requires --confirm-timebase-test")
        require(1 <= args.cycles <= 257, "Timebase cycles must be in 1..257")
        address = UsbAddress(args.bus, args.address)
        image = DebugImage(args.output, args.board, "timebase_fixture")
        program = (args.output / "timebase_fixture.bin").read_bytes()
        validate_program(image, program, args.cycles)
        with Debugger(PyUsbBackend.load(), Access.RESET_DEBUG_SESSION, timeout_ms=10_000,
                      allow_cpu_control=True, allow_target_reset=True,
                      allow_memory_access=True, allow_breakpoints=True) as debugger:
            debugger.open(address)
            result = exercise(debugger, image, program, args.cycles)
    except (DebuggerError, ValueError, OSError, KeyError) as error:
        print(f"timebase-hardware-check: {error}", file=sys.stderr)
        return 1
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
