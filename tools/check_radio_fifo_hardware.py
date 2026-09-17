#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Explicit manual quiescent FIFO fixture acceptance; no flashing or host MMIO."""

import argparse
from dataclasses import asdict
import hashlib
import json
from pathlib import Path
import sys

from cc_debugger import Access, Debugger, DebuggerError, PyUsbBackend, UsbAddress
from check_clock_hardware import HOLD_SECONDS, hold_halted
from check_timebase_hardware import reset_and_verify_code, step_nop, wait_checkpoint
from debug_image import DebugImage, decode_bootstrap
from radio_fifo_fixture import CHECKPOINTS, SIZE, decode, inspect_deadline
from verify_firmware import BOARDS, CODE_LIMIT, require


def validate_program(image, program, cycles, induced):
    require(image.image_name == "radio_fifo_fixture", "Acceptance requires the board radio_fifo_fixture")
    require(type(cycles) is int and 1 <= cycles <= 257 and type(induced) is bool
            and (not induced or cycles == 1), "Cycles must be 1..257; timeout mode requires --cycles 1")
    require(isinstance(program, bytes) and 0 < len(program) <= CODE_LIMIT and
            len(program) == image.metrics["image_extent_bytes"] and
            hashlib.sha256(program).hexdigest() == image.sha256, "Program differs from verified FIFO image")
    p = image.radio_fifo_proof
    require(p["function_size"] == 148 and p["address"] == p["function_start"] + 147 and
            program[p["address"] - 3:p["address"] + 1] == b"\x75\x82\0\x22" and
            program[p["deadline_call"]:p["return_address"]] ==
            b"\x12" + p["function_start"].to_bytes(2, "big") and
            program[p["write"]:p["write"] + 2] == b"\x89\xd9",
            "FIFO deadline/write checkpoint differs from verified program")
    before, ready, fault = (image.symbol(name).address for name in CHECKPOINTS)
    require(p["checkpoints"] == [before, ready, fault] and
            program[before:before + 7] == b"\0\x22\0\x22\0\x80\xfd", "FIFO checkpoint program mismatch")


def inspect(debugger, image, pc, *, internal=False):
    registers = debugger.read_registers()
    require(registers.pc == pc and registers.dps == 0, "FIFO checkpoint PC/DPS mismatch")
    require((registers.sp == image.radio_fifo_proof["deadline_sp"] < 0x80) if internal else
            registers.sp == image.metrics["iram_stack_start"] + 1, "FIFO checkpoint stack mismatch")
    raw = debugger.read_xdata(image.symbol("_radio_fifo_fixture_state").address, SIZE)
    boot = debugger.read_xdata(0x1e00, 32)
    record, startup = decode(raw, allow_running=internal), decode_bootstrap(boot, image.board)
    context = None
    if internal:
        require(pc == image.radio_fifo_proof["address"] and record["phase"] == 2 and
                record["stage"] == 2 and record["completed"] == 0 and record["fifo_result"] == 255,
                "Deadline checkpoint is not the first small preload")
        context = inspect_deadline(image.radio_fifo_proof, debugger.read_xdata, registers.sp,
                                   registers.dptr0 & 255, registers.dps)
    require(debugger.read_registers() == registers, "FIFO inspection changed CPU context")
    return record, startup, boot, registers, context


def invariants(record, boot, initial_boot, initial, clock_record):
    require(boot[:8] + boot[9:] == initial_boot[:8] + initial_boot[9:] and
            boot[8] == record["completed"], "FIFO immutable M0/heartbeat mismatch")
    require(record["sleep"] == record["initial_sleep"] == initial["initial_sleep"] and
            record["enables"] == [0, 0, 0], "FIFO sleep/IRQ ownership changed")
    require(record["command"] == record["status"] == 0x88 and record["radio_valid"] == 1
            and record["clock_result"] == 0,
            "FIFO lost confirmed XOSC32/quiescent observation")
    if clock_record:
        require(record["clock"] == clock_record["clock"] and
                record["initial_flags"] == record["flags"] == clock_record["initial_flags"],
                "FIFO changed clock diagnostics or unrelated flags/priorities/masks")


def check_timeout(record):
    d, r = record["fifo"], record["radio"]
    require(record["phase"] == 4 and record["stage"] == 2 and record["reason"] == 4 and
            record["fifo_result"] == 8 and record["clock_result"] == 0 and record["completed"] == 0 and
            record["checked"] == 0 and record["mismatch_index"] == 255 and
            1024 < d["elapsed_ticks"] < 0x800000 and d["polls"] == 1 and d["timebase_status"] == 0 and
            d["strobes"] == d["confirmed"] == d["bytes_verified"] == 0 and d["bytes_written"] == 1 and
            d["sample_valid"] == 1 and d["errors"] == d["rx_count"] == d["rx_first"] ==
            d["rx_last"] == d["rx_packet"] == d["tx_first"] == 0 and
            d["tx_count"] <= 1 and d["tx_last"] <= 1 and d["fifo_signals"] & 0xe7 == 0 and
            r[:2] == [0x40, 1] and r[2] & 0xe0 == r[3] & 0xc0 == r[4] == r[5] & 0xe7 == 0 and
            all(r[i] == 0 for i in (6, 8, 9, 10, 11, 13)) and r[7] <= 1 and r[12] <= 1,
            "Induced hold did not produce actual terminal TIMEOUT with one written/unverified byte: "
            + json.dumps(record, sort_keys=True))


def exercise(debugger, image, program, cycles=1, induced=False):
    validate_program(image, program, cycles, induced)
    adapter, config = reset_and_verify_code(debugger, program, "radio FIFO")
    before, ready, fault = (image.symbol(name).address for name in CHECKPOINTS)
    for slot in range(4):
        debugger.set_breakpoint(slot, before, False)
    for slot, address in enumerate((before, ready, fault)):
        debugger.set_breakpoint(slot, address)
    debugger.resume()
    pc = wait_checkpoint(debugger, before, fault, "radio FIFO")
    initial, startup, initial_boot, registers, _ = inspect(debugger, image, pc)
    require(pc == before and initial["phase"] == 1 and startup["heartbeat"] == 0 and
            initial["command"] == initial_boot[24] and initial["status"] == initial_boot[25],
            "FIFO initialization failed: " + json.dumps(initial, sort_keys=True))
    debugger.set_breakpoint(0, before, False)
    step_nop(debugger, registers, "FIFO")
    observations, clock_record, context = [], None, None
    count = 3 if induced else 1 + cycles * 5
    for index in range(count):
        if induced and index == 2:
            point = image.radio_fifo_proof["address"]
            debugger.set_breakpoint(3, point)
            debugger.resume()
            pc = wait_checkpoint(debugger, point, fault, "FIFO deadline")
            require(pc == point, "FIFO fault before operation-deadline hold")
            running, _, boot, registers, context = inspect(debugger, image, pc, internal=True)
            invariants(running, boot, initial_boot, initial, clock_record)
            debugger.set_breakpoint(3, point, False)
            hold_halted(debugger)
            require(debugger.read_registers() == registers, "FIFO deadline hold changed CPU context")
        debugger.resume()
        pc = wait_checkpoint(debugger, ready, fault, "radio FIFO")
        record, _, boot, registers, _ = inspect(debugger, image, pc)
        if induced and index == 2:
            require(pc == fault, "FIFO timeout did not reach terminal FAULT")
            check_timeout(record)
        else:
            require(pc == ready and record["phase"] == 3,
                    "FIFO terminal failure: " + json.dumps(record, sort_keys=True))
            require(record["stage"] == (0 if index == 0 else (index - 1) % 5 + 1) and
                    record["completed"] == (index // 5) & 255, "FIFO stage/counter progression mismatch")
        invariants(record, boot, initial_boot, initial, clock_record)
        if index == 0:
            clock_record = record
        observations.append(record)
        if index + 1 < count:
            step_nop(debugger, registers, "FIFO")
    return {
        "evidence": "hardware-observed", "scope": "compiled-quiescent-radio-fifo-fixture",
        "board": image.board, "image": image.image_name, "image_sha256": image.sha256,
        "adapter": asdict(adapter), "debug_config": config, "verified_code_bytes": len(program),
        "completed_cycles": 0 if induced else cycles, "observations": observations,
        "induced_timeout": induced, "hold_seconds": HOLD_SECONDS if induced else None,
        "deadline_context": context, "linked_proof": image.radio_fifo_proof, "startup_status": startup,
        "register_preservation": True, "initial_reset_pc": 0, "final_pc": pc,
        "final_cpu": "halted-at-fifo-fault" if induced else "halted-at-fifo-ready",
        "not_tested_by_this_run": [
            "RX flush on fresh empty RX FIFO", "received frames or on-air radio",
            "FCS generation, authentication or MAC", "calibrated time or physical stopped clock",
            "error-latch recovery, interrupts, DMA, sleep, AES or flash services",
        ],
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bus", type=int, required=True)
    parser.add_argument("--address", type=int, required=True)
    parser.add_argument("--board", choices=tuple(BOARDS), required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cycles", type=int, default=1)
    parser.add_argument("--induce-timeout", action="store_true")
    parser.add_argument("--confirm-radio-fifo-test", action="store_true",
                        help="authorize reset-attach, CPU/read/breakpoint access to this quiescent fixture")
    args = parser.parse_args(argv)
    try:
        require(args.confirm_radio_fifo_test, "Manual acceptance requires --confirm-radio-fifo-test")
        require(1 <= args.cycles <= 257 and (not args.induce_timeout or args.cycles == 1),
                "Cycles must be 1..257; timeout mode requires --cycles 1")
        address = UsbAddress(args.bus, args.address)
        image = DebugImage(args.output, args.board, "radio_fifo_fixture")
        program = (args.output / "radio_fifo_fixture.bin").read_bytes()
        validate_program(image, program, args.cycles, args.induce_timeout)
        with Debugger(PyUsbBackend.load(), Access.RESET_DEBUG_SESSION, timeout_ms=10_000,
                      allow_cpu_control=True, allow_target_reset=True, allow_memory_access=True,
                      allow_breakpoints=True) as debugger:
            debugger.open(address)
            result = exercise(debugger, image, program, args.cycles, args.induce_timeout)
    except (DebuggerError, ValueError, OSError, KeyError) as error:
        print(f"radio-fifo-hardware-check: {error}", file=sys.stderr)
        return 1
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
