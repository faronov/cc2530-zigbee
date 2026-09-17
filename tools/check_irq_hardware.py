#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Explicit manual Timer1/EA board acceptance; no flash or host memory writes."""

import argparse
from dataclasses import asdict, replace
import hashlib
import json
from pathlib import Path
import sys

from cc_debugger import Access, Debugger, DebuggerError, PyUsbBackend, UsbAddress
from check_clock_hardware import hold_halted
from check_timebase_hardware import reset_and_verify_code, step_nop, wait_checkpoint
from debug_image import DebugImage, decode_bootstrap
from irq_fixture import decode_irq_fixture
from verify_firmware import BOARDS, IRQ_CHECKPOINTS, IRQ_RESTORE_BYTES, CODE_LIMIT, require


def validate_program(image, program, cycles, induce_timeout):
    require(image.image_name == "irq_fixture", "Acceptance requires the board irq_fixture image")
    require(type(cycles) is int and type(induce_timeout) is bool and
            (cycles == 1 if induce_timeout else 2 <= cycles <= 257),
            "IRQ normal cycles must be 2..257; induced timeout requires --cycles 1")
    require(isinstance(program, bytes) and 0 < len(program) <= CODE_LIMIT and
            len(program) == image.metrics["image_extent_bytes"] and
            hashlib.sha256(program).hexdigest() == image.sha256, "Program differs from checked IRQ image")
    proof = image.irq_proof
    require(proof["isr_size"] == 53 and proof["isr_reti"] == proof["isr_entry"] + 52 and
            program[0x4b:0x4e] == b"\x02" + proof["isr_entry"].to_bytes(2, "big") and
            program[proof["isr_reti"]] == 0x32 and
            program[proof["restore"]:proof["restore"] + len(IRQ_RESTORE_BYTES)] == IRQ_RESTORE_BYTES and
            program[proof["restore_call"]:proof["restore_return"]] ==
            b"\x12" + proof["restore"].to_bytes(2, "big"), "IRQ proof/program mismatch")


def inspect(debugger, image, pc, *, isr=False):
    if isr and pc == image.symbol(IRQ_CHECKPOINTS[-1]).address:
        record, _, _, _, _ = inspect(debugger, image, pc)
        raise ValueError(f"IRQ FAULT: reason={record['reason']} stage={record['stage']} "
                         f"helper={record['helper_status']}")
    registers = debugger.read_registers()
    require(registers.pc == pc and registers.dps == 0, "IRQ checkpoint PC/DPS mismatch")
    if isr:
        require(image.metrics["iram_stack_start"] + 3 <= registers.sp < 0x80,
                "IRQ ISR frame is outside the bounded stack")
    else:
        require(registers.sp == image.metrics["iram_stack_start"] + 1, "IRQ foreground checkpoint stack mismatch")
    record = decode_irq_fixture(debugger.read_xdata(image.symbol("_irq_fixture_state").address, 64))
    boot = debugger.read_xdata(0x1e00, 32)
    startup = decode_bootstrap(boot, image.board)
    iram = debugger.read_xdata(0x1f00, registers.sp + 1) if isr else None
    require(debugger.read_registers() == registers, "IRQ inspection changed CPU state")
    return record, boot, startup, registers, iram


def invariant(record, boot, initial, completed, initial_record):
    require(boot[:8] + boot[9:] == initial[:8] + initial[9:], "Immutable IRQ M0 status changed")
    require(boot[8] == record["completed"] == completed & 255, "IRQ cycle/heartbeat progression mismatch")
    require(all(record[name] == value for name, value in initial_record.items() if name.startswith("initial_")),
            "Immutable IRQ initial observations changed")


def exercise(debugger, image, program, cycles=3, induce_timeout=False):
    validate_program(image, program, cycles, induce_timeout)
    adapter, config = reset_and_verify_code(debugger, program, "IRQ")
    before, armed, pending, inner, ready, fault = (image.symbol(name).address for name in IRQ_CHECKPOINTS)
    proof = image.irq_proof
    for slot in range(4):
        debugger.set_breakpoint(slot, before, False)
    debugger.set_breakpoint(0, before)
    debugger.set_breakpoint(3, fault)
    debugger.resume()
    pc = wait_checkpoint(debugger, before, fault, "IRQ initialization")
    record, initial_boot, startup, registers, _ = inspect(debugger, image, pc)
    require(pc == before and record["phase"] == 1 and record["completed"] == startup["heartbeat"] == 0,
            f"IRQ initialization failed: reason={record['reason']}")
    initial_record = record
    debugger.set_breakpoint(0, before, False)
    step_nop(debugger, registers, "IRQ")
    observations = []
    if induce_timeout:
        debugger.set_breakpoint(0, armed)
        debugger.resume()
        pc = wait_checkpoint(debugger, armed, fault, "IRQ armed")
        record, boot, _, registers, _ = inspect(debugger, image, pc)
        require(pc == armed and record["phase"] == 2 and record["stage"] == 1 and
                record["control"] == record["source"] == record["ien0"] == record["isr_count"] == 0 and
                record["ien1"] == 2 and record["pending_polls"] == 0, "IRQ timeout is not before timer start")
        invariant(record, boot, initial_boot, 0, initial_record)
        start = int.from_bytes(debugger.read_xdata(proof["start_address"], 4), "little")
        deadline = int.from_bytes(debugger.read_xdata(proof["deadline_address"], 4), "little")
        require(start <= 0xffffff and deadline == (start + 1024) & 0xffffff,
                "IRQ armed checkpoint has no valid computed raw deadline")
        require(debugger.read_registers() == registers, "IRQ deadline inspection changed CPU state")
        debugger.set_breakpoint(0, armed, False)
        debugger.set_breakpoint(0, pending)
        debugger.set_breakpoint(2, ready)
        hold_halted(debugger)
        require(debugger.read_registers() == registers, "IRQ armed hold changed CPU state")
        step_nop(debugger, registers, "IRQ armed")
        debugger.resume()
        pc = wait_checkpoint(debugger, fault, fault, "IRQ timeout")
        record, boot, _, registers, _ = inspect(debugger, image, pc)
        invariant(record, boot, initial_boot, 0, initial_record)
        require(pc == fault and record["phase"] == 4 and record["reason"] == 6 and record["stage"] == 1 and
                1024 < record["pending_elapsed"] < 0x800000 and record["pending_polls"] == 1 and
                record["helper_status"] == record["isr_count"] == record["ien0"] == record["ien1"] ==
                record["control"] == 0, "IRQ induced timeout did not retain bounded failure/disabled timer")
        observations.append(record)
        final = fault
    else:
        debugger.set_breakpoint(0, pending)
        debugger.set_breakpoint(1, inner)
        debugger.set_breakpoint(2, ready)
        for cycle in range(1, cycles + 1):
            debugger.resume()
            pc = wait_checkpoint(debugger, pending, fault, "IRQ pending")
            first, boot, _, registers, _ = inspect(debugger, image, pc)
            require(pc == pending and first["phase"] == 2 and first["stage"] == 2 and
                    first["isr_count"] == (cycle - 1) & 255, f"IRQ pending failed: reason={first['reason']}")
            invariant(first, boot, initial_boot, cycle - 1, initial_record)
            step_nop(debugger, registers, "IRQ pending")
            debugger.resume()
            pc = wait_checkpoint(debugger, inner, fault, "IRQ inner")
            record, boot, _, registers, _ = inspect(debugger, image, pc)
            require(pc == inner and record["stage"] == 3 and record["phase"] == 2 and
                    record["counter"] == first["counter"], "IRQ inner restore/counter suspension failed")
            invariant(record, boot, initial_boot, cycle - 1, initial_record)
            step_nop(debugger, registers, "IRQ inner")
            debugger.set_breakpoint(0, pending, False)
            debugger.set_breakpoint(1, inner, False)
            debugger.set_breakpoint(0, proof["isr_entry"])
            debugger.set_breakpoint(1, proof["isr_reti"])
            debugger.resume()
            pc = wait_checkpoint(debugger, proof["isr_entry"], fault, "Timer1 ISR entry")
            entry, boot, _, saved, live = inspect(debugger, image, pc, isr=True)
            require(pc == proof["isr_entry"] and entry["phase"] == 2 and entry["stage"] == 4 and
                    entry["isr_count"] == (cycle - 1) & 255, "Timer1 ISR did not enter in outer restore")
            invariant(entry, boot, initial_boot, cycle - 1, initial_record)
            resume = int.from_bytes(live[-2:], "little")
            require(resume in proof["resume_addresses"], "Timer1 interrupt has unverified foreground return PC")
            in_restore = resume in (proof["restore"] + 9, proof["restore"] + 12)
            require(saved.sp == image.metrics["iram_stack_start"] + (5 if in_restore else 3),
                    "Timer1 hardware frame has wrong caller depth")
            if in_restore:
                require(int.from_bytes(live[-4:-2], "little") == proof["restore_return"] and
                        saved.dptr0 & 255 == (1 if resume == proof["restore"] + 9 else 0),
                        "Interrupted restore caller/live DPL mismatch")
            debugger.set_breakpoint(0, proof["isr_entry"], False)
            debugger.resume()
            pc = wait_checkpoint(debugger, proof["isr_reti"], fault, "Timer1 RETI")
            serviced, boot, _, restored, after = inspect(debugger, image, pc, isr=True)
            require(pc == proof["isr_reti"] and restored == replace(saved, pc=pc) and live == after,
                    "Timer1 ISR corrupted CPU/IRAM/live-stack context")
            invariant(serviced, boot, initial_boot, cycle - 1, initial_record)
            require(serviced["isr_count"] == cycle & 255 and serviced["isr_source"] == 0x20 and
                    serviced["isr_cpu"] == serviced["initial_ircon"], "Timer1 overflow/H0 ISR evidence failed")
            result = debugger.step()
            require(result.accumulator == restored.a and debugger.read_registers() ==
                    replace(restored, pc=resume, sp=restored.sp - 2), "Actual RETI failed to restore foreground frame")
            debugger.set_breakpoint(1, proof["isr_reti"], False)
            debugger.set_breakpoint(0, pending)
            debugger.set_breakpoint(1, inner)
            debugger.resume()
            pc = wait_checkpoint(debugger, ready, fault, "IRQ ready")
            record, boot, _, registers, _ = inspect(debugger, image, pc)
            require(pc == ready and record["phase"] == 3 and record["counter"] == first["counter"],
                    f"IRQ did not complete: reason={record['reason']}")
            invariant(record, boot, initial_boot, cycle, initial_record)
            observations.append({"record": record, "interrupt_return_pc": resume,
                                 "interrupted_restore": in_restore, "isr_context_preserved": True})
            if cycle < cycles:
                step_nop(debugger, registers, "IRQ ready")
        final = ready
    return {
        "evidence": "hardware-observed", "scope": "compiled-CC2530-Timer1-EA-fixture",
        "board": image.board, "image": image.image_name, "image_sha256": image.sha256,
        "adapter": asdict(adapter), "debug_config": config, "verified_code_bytes": len(program),
        "verified_cycles": 0 if induce_timeout else cycles, "induced_timeout": induce_timeout,
        "observations": observations, "startup_status": startup, "final_pc": final,
        "final_cpu": "halted-at-IRQ-fault" if induce_timeout else "halted-at-IRQ-ready",
        "not_tested_by_this_run": ["higher-priority hardware nesting", "other interrupt sources",
                                 "calibrated timing or exact hardware overflow count", "physical stopped-clock faults",
                                 "DMA", "sleep/wake", "RF", "AES", "flash services"],
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bus", type=int, required=True)
    parser.add_argument("--address", type=int, required=True)
    parser.add_argument("--board", choices=tuple(BOARDS), required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cycles", type=int, default=3)
    parser.add_argument("--induce-timeout", action="store_true")
    parser.add_argument("--confirm-irq-test", action="store_true",
                        help="authorize reset-attach, CPU control, read-only inspection and breakpoints")
    args = parser.parse_args(argv)
    try:
        require(args.confirm_irq_test, "Manual acceptance requires --confirm-irq-test")
        require(args.cycles == 1 if args.induce_timeout else 2 <= args.cycles <= 257, "Invalid IRQ cycles/mode")
        address = UsbAddress(args.bus, args.address)
        image = DebugImage(args.output, args.board, "irq_fixture")
        program = (args.output / "irq_fixture.bin").read_bytes()
        validate_program(image, program, args.cycles, args.induce_timeout)
        with Debugger(PyUsbBackend.load(), Access.RESET_DEBUG_SESSION, timeout_ms=10_000,
                      allow_cpu_control=True, allow_target_reset=True, allow_memory_access=True,
                      allow_breakpoints=True) as debugger:
            debugger.open(address)
            result = exercise(debugger, image, program, args.cycles, args.induce_timeout)
    except (DebuggerError, ValueError, OSError, KeyError) as error:
        print(f"irq-hardware-check: {error}", file=sys.stderr)
        return 1
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
