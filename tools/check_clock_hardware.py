#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Explicit manual C clock fixture acceptance. Never flashes or injects memory."""

import argparse
from dataclasses import asdict
import hashlib
import json
from pathlib import Path
import sys
import time

from cc2530_debug import READ_STATUS, Status
from cc_debugger import Access, Debugger, DebuggerError, PyUsbBackend, UsbAddress
from check_timebase_hardware import reset_and_verify_code, step_nop, wait_checkpoint
from debug_image import DebugImage, decode_bootstrap, decode_clock_fixture, effective_clock_status
from verify_firmware import BOARDS, CLOCK_CHECKPOINTS, CLOCK_FIXTURE_SIZE, CODE_LIMIT, require


HOLD_SECONDS = 0.25


def validate_program(image, program, cycles, induce_timeout, induce_late_timeout=False):
    require(image.image_name == "clock_fixture", "Acceptance requires the board clock_fixture image")
    require(type(cycles) is int and 1 <= cycles <= 257, "Clock cycles must be in 1..257")
    require(type(induce_timeout) is bool and type(induce_late_timeout) is bool
            and not (induce_timeout and induce_late_timeout)
            and (not (induce_timeout or induce_late_timeout) or cycles == 1),
            "Choose one induced-timeout mode with exactly one partial sequence (--cycles 1)")
    require(isinstance(program, bytes) and 0 < len(program) <= CODE_LIMIT
            and len(program) == image.metrics["image_extent_bytes"]
            and hashlib.sha256(program).hexdigest() == image.sha256,
            "Program differs from the checked clock image")
    proof = image.clock_timeout_checkpoint
    require(proof["function_size"] == 148 and proof["address"] == proof["function_start"] + 147
            and program[proof["address"] - 3:proof["address"] + 1] == b"\x75\x82\0\x22"
            and program[proof["call_address"]:proof["return_address"]] ==
            b"\x12" + proof["function_start"].to_bytes(2, "big")
            and program[proof["command_write_address"]:proof["command_write_address"] + 2] == b"\x8f\xc6",
            "Clock timeout checkpoint differs from verified program")
    require(proof["post_request_address"] == proof["command_write_address"] + 2
            and program[proof["post_request_address"]] == 0x8f
            and program[proof["poll_sample_address"]] == 0x12
            and proof["poll_sample_address"] == proof["poll_observe_address"] + 40,
            "Clock late-source checkpoint differs from verified program")


def inspect_checkpoint(debugger, image, pc, *, internal_stop=None):
    require(internal_stop in (None, "deadline", "post-request", "poll"), "Unknown clock inspection boundary")
    registers = debugger.read_registers()
    require(registers.pc == pc, "Clock checkpoint PC mismatch")
    if internal_stop:
        require(image.metrics["iram_stack_start"] + 5 <= registers.sp < 0x80,
                "Clock deadline checkpoint stack is outside bounded call context")
    else:
        require(registers.sp == image.metrics["iram_stack_start"] + 1, "Clock checkpoint stack mismatch")
    data = debugger.read_xdata(image.symbol("_clock_fixture_state").address, CLOCK_FIXTURE_SIZE)
    boot = debugger.read_xdata(0x1e00, 32)
    record, startup = decode_clock_fixture(data, allow_running=bool(internal_stop)), decode_bootstrap(boot, image.board)
    if internal_stop:
        proof = image.clock_timeout_checkpoint
        expected_pc = proof[{"deadline": "address", "post-request": "post_request_address",
                             "poll": "poll_sample_address"}[internal_stop]]
        require(pc == expected_pc and registers.dps == 0, "Wrong internal clock checkpoint/DPS")
        require(record["phase"] == 2 and record["stage"] == 1 and record["completed_steps"] == 1,
                "Internal breakpoint is not the first XOSC request")
        require(debugger.read_xdata(proof["command_address"], 1) == bytes((boot[24] & 0xb8,)),
                "Internal breakpoint is not the original requested command")
        require(debugger.read_xdata(proof["source_seen_address"], 1) ==
                (b"\x01" if internal_stop == "poll" else b"\0"),
                "Clock C source evidence does not match the requested experiment")
        if internal_stop == "deadline":
            require(registers.dptr0 & 255 == 0, "Deadline helper did not return DPL=OK")
            stack = debugger.read_xdata(0x1f00 + registers.sp - 1, 2)
            require(int.from_bytes(stack, "little") == proof["return_address"],
                    "Deadline breakpoint has wrong clock caller return address")
            deadline = int.from_bytes(debugger.read_xdata(proof["deadline_address"], 4), "little")
            require(deadline <= 0xffffff, "Computed deadline is not a zero-extended 24-bit value")
        if internal_stop == "poll":
            command = boot[24] & 0xb8
            actual = debugger.read_xdata(proof["diagnostics_address"], 19)
            require(actual == b"\0" * 14 + bytes((boot[24], command, command, command, 8)),
                    "Late timeout requires C-observed stable requested source before its first timed poll")
    require(debugger.read_registers() == registers, "Clock inspection changed CPU state")
    return record, startup, boot, registers


def hold_halted(debugger):
    # Host delay is only the stimulus. Actual TIMEOUT/rollback records decide
    # success. A single existing operation deadline also bounds an oversleep.
    with debugger._target_operation() as deadline:
        status = debugger._exchange_byte(READ_STATUS, deadline)
        debugger._check_status(status, active=True)
        require(status & Status.CPU_HALTED, "Clock deadline hold requires a halted CPU")
        time.sleep(HOLD_SECONDS)
        deadline.remaining_ms()
        status = debugger._exchange_byte(READ_STATUS, deadline)
        debugger._check_status(status, active=True)
        require(status & Status.CPU_HALTED, "CPU resumed unexpectedly during deadline hold")


def check_invariants(record, boot, initial_boot, initial_record):
    require(boot[:8] + boot[9:] == initial_boot[:8] + initial_boot[9:], "Immutable M0 startup status changed")
    require(record["initial_sleep_command"] == record["current_sleep_command"] ==
            initial_record["initial_sleep_command"] and
            record["initial_interrupt_enables"] == record["current_interrupt_enables"] == [0, 0, 0],
            "Clock sleep/IRQ invariants changed")
    require(record["current_clock_command"] & 0xb8 == initial_boot[24] & 0xb8,
            "Clock changed LF/TICKSPD command fields")
    require(record["completed_steps"] == boot[8], "Clock counter/heartbeat mismatch")


def exercise(debugger, image, program, cycles=1, induce_timeout=False, induce_late_timeout=False):
    validate_program(image, program, cycles, induce_timeout, induce_late_timeout)
    negative = induce_timeout or induce_late_timeout
    adapter, config = reset_and_verify_code(debugger, program, "clock")
    before, ready, fault = (image.symbol(name).address for name in CLOCK_CHECKPOINTS)
    for slot in range(4):
        debugger.set_breakpoint(slot, before, False)
    for slot, address in enumerate((before, ready, fault)):
        debugger.set_breakpoint(slot, address)
    debugger.resume()
    pc = wait_checkpoint(debugger, before, fault, "clock")
    initial, startup, initial_boot, registers = inspect_checkpoint(debugger, image, pc)
    require(pc == before and initial["phase"] == 1 and startup["heartbeat"] == 0,
            f"Clock fixture failed initialization: phase={initial['phase']} reason={initial['reason']}")
    require(initial["current_clock_command"] == startup["clock_request"]
            and initial["current_clock_status"] == startup["clock_status"], "Clock startup snapshots differ")
    debugger.set_breakpoint(0, before, False)
    step_nop(debugger, registers, "Clock")
    observations = []
    for step in range(1, (2 if negative else cycles * 3) + 1):
        if negative and step == 2:
            proof = image.clock_timeout_checkpoint
            debugger.set_breakpoint(3, proof["address"])
            debugger.resume()
            pc = wait_checkpoint(debugger, proof["address"], fault, "clock deadline")
            require(pc == proof["address"], "Clock fixture FAULT before induced deadline checkpoint")
            running, _, boot, registers = inspect_checkpoint(debugger, image, pc, internal_stop="deadline")
            check_invariants(running, boot, initial_boot, initial)
            require(running["current_clock_command"] == initial_boot[24]
                    and running["current_clock_status"] == initial_boot[25],
                    "Clock request executed before deadline hold")
            debugger.set_breakpoint(3, proof["address"], False)
            if induce_late_timeout:
                debugger.set_breakpoint(3, proof["post_request_address"])
                debugger.resume()
                pc = wait_checkpoint(debugger, proof["post_request_address"], fault, "clock post-request")
                running, _, boot, registers = inspect_checkpoint(debugger, image, pc, internal_stop="post-request")
                check_invariants(running, boot, initial_boot, initial)
                debugger.set_breakpoint(3, proof["post_request_address"], False)
            hold_halted(debugger)
            require(debugger.read_registers() == registers, "Deadline hold changed CPU state")
            if induce_late_timeout:
                debugger.set_breakpoint(3, proof["poll_sample_address"])
                debugger.resume()
                pc = wait_checkpoint(debugger, proof["poll_sample_address"], fault, "clock source observation")
                running, _, boot, registers = inspect_checkpoint(debugger, image, pc, internal_stop="poll")
                check_invariants(running, boot, initial_boot, initial)
                debugger.set_breakpoint(3, proof["poll_sample_address"], False)
        debugger.resume()
        pc = wait_checkpoint(debugger, ready, fault, "clock")
        record, _, boot, registers = inspect_checkpoint(debugger, image, pc)
        check_invariants(record, boot, initial_boot, initial)
        if negative and step == 2:
            diag = record["diagnostics"]
            require(pc == fault and record["phase"] == 4 and record["reason"] == 1
                    and record["stage"] == 1 and record["clock_result"] == 3
                    and record["completed_steps"] == 1
                    and record["timeout_ticks"] < diag["request"]["elapsed_ticks"] < 0x800000
                    and diag["request"]["timebase_status"] == 0 and diag["rollback_result"] == 0
                    and diag["observed_command"] == record["current_clock_command"] == initial_boot[24]
                    and diag["observed_status"] == record["current_clock_status"] == initial_boot[25],
                    f"Induced timeout did not produce actual CLOCK_TIMEOUT with confirmed rollback: "
                    f"result={record['clock_result']} rollback={diag['rollback_result']} "
                    f"observed={diag['observed_command']:02x}/{diag['observed_status']:02x} "
                    f"current={record['current_clock_command']:02x}/{record['current_clock_status']:02x}; "
                    "rollback 9 means bounded cancellation uncertainty, not confirmation")
        else:
            require(pc == ready and record["phase"] == 3,
                    f"Clock fixture FAULT: reason={record['reason']} result={record['clock_result']} "
                    f"rollback={record['diagnostics']['rollback_result']}")
            require(record["stage"] == (step - 1) % 3 and record["completed_steps"] == step & 255,
                    "Clock stage/counter progression mismatch")
            command = (initial_boot[24] & 0xb8) | (0 if record["stage"] == 1 else 0x41)
            require(record["current_clock_command"] == command
                    and record["current_clock_status"] == effective_clock_status(command),
                    "Clock did not select expected undivided source")
        observations.append(record)
        if not (negative and step == 2) and step < (2 if negative else cycles * 3):
            step_nop(debugger, registers, "Clock")
    return {
        "evidence": "hardware-observed", "scope": "compiled-init-clock-fixture",
        "board": image.board, "image": image.image_name, "image_sha256": image.sha256,
        "adapter": asdict(adapter), "debug_config": config, "verified_code_bytes": len(program),
        "completed_sequences": 0 if negative else cycles, "observations": observations,
        "induced_timeout": negative, "hold_seconds": HOLD_SECONDS if negative else None,
        "timeout_mode": "late-confirmed-source" if induce_late_timeout else "pending-cancel" if negative else None,
        "timeout_checkpoint": image.clock_timeout_checkpoint, "startup_status": startup,
        "register_preservation": True, "initial_reset_pc": 0, "final_pc": pc,
        "final_cpu": "halted-at-expected-clock-fault" if negative else "halted-at-clock-ready",
        "not_tested_by_this_run": [
            "frequency measurement or calibrated timing", "LF calibration completion/precision",
            "oscillator-failure or stopped-clock injection", "LF source switching",
            "sleep/wake or IRQ/compare", "RF", "AES", "flash",
        ],
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bus", type=int, required=True)
    parser.add_argument("--address", type=int, required=True)
    parser.add_argument("--board", choices=tuple(BOARDS), required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cycles", type=int, default=1, help="complete RC/XOSC/RC sequences, 1..257")
    parser.add_argument("--induce-timeout", action="store_true",
                        help="hold at the verified deadline RET; require terminal TIMEOUT and confirmed rollback")
    parser.add_argument("--induce-late-timeout", action="store_true",
                        help="hold after the real request; verify C-observed source before late TIMEOUT/rollback")
    parser.add_argument("--confirm-clock-test", action="store_true",
                        help="authorize reset-attach, CPU control, read-only inspection and breakpoints")
    args = parser.parse_args(argv)
    try:
        require(args.confirm_clock_test, "Manual acceptance requires --confirm-clock-test")
        require(1 <= args.cycles <= 257 and not (args.induce_timeout and args.induce_late_timeout)
                and (not (args.induce_timeout or args.induce_late_timeout) or args.cycles == 1),
                "Cycles must be 1..257; choose one induced-timeout mode with --cycles 1")
        address = UsbAddress(args.bus, args.address)
        image = DebugImage(args.output, args.board, "clock_fixture")
        program = (args.output / "clock_fixture.bin").read_bytes()
        validate_program(image, program, args.cycles, args.induce_timeout, args.induce_late_timeout)
        with Debugger(PyUsbBackend.load(), Access.RESET_DEBUG_SESSION, timeout_ms=10_000,
                      allow_cpu_control=True, allow_target_reset=True, allow_memory_access=True,
                      allow_breakpoints=True) as debugger:
            debugger.open(address)
            result = exercise(debugger, image, program, args.cycles, args.induce_timeout, args.induce_late_timeout)
    except (DebuggerError, ValueError, OSError, KeyError) as error:
        print(f"clock-hardware-check: {error}", file=sys.stderr)
        return 1
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
