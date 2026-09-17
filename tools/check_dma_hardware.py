#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Explicit manual DMA board acceptance. Never programs flash or recovers a failed copy."""

import argparse
from dataclasses import asdict
import hashlib
import json
from pathlib import Path
import sys

from cc2530_debug import READ_CONFIG, Status
from cc_debugger import Access, Debugger, DebuggerError, PyUsbBackend, UsbAddress
from check_clock_hardware import HOLD_SECONDS, hold_halted
from check_timebase_hardware import reset_and_verify_code, step_nop, wait_checkpoint
from debug_image import DebugImage, decode_bootstrap
from dma_fixture import CHECKPOINTS, SIZE, decode, expected_buffers, inspect_expiry
from verify_firmware import BOARDS, CODE_LIMIT, require


def validate_program(image, program, cycles, induced):
    require(image.image_name == "dma_fixture", "Acceptance requires the board dma_fixture, never dma_test")
    require(type(cycles) is int and 1 <= cycles <= 257 and type(induced) is bool and
            (not induced or cycles == 1), "Cycles must be 1..257; timeout mode requires --cycles 1")
    require(isinstance(program, bytes) and 0 < len(program) <= CODE_LIMIT and
            len(program) == image.metrics["image_extent_bytes"] and
            hashlib.sha256(program).hexdigest() == image.sha256, "Program differs from verified DMA image")
    p = image.dma_proof
    require(p["function_size"] == 168 and p["address"] == p["function_start"]+167 and
            program[p["address"]-3:p["address"]+1] == b"\x75\x82\0\x22" and
            program[p["expiry_call"]:p["return_address"]] ==
            b"\x12"+p["function_start"].to_bytes(2, "big") and
            program[p["arm"]:p["arm_ret"]+1] == b"\x75\xd6\x01"+bytes(9)+b"\x22" and
            program[p["arm_call"]:p["arm_call"]+3] == b"\x12"+p["arm"].to_bytes(2, "big") and
            program[p["request"]:p["request"]+3] == b"\x75\xd7\x01",
            "DMA actual nine-NOP/caller/expiry/request proof differs from program")
    before, ready, fault = (image.symbol(n).address for n in CHECKPOINTS)
    require(p["checkpoints"] == [before, ready, fault] and
            program[before:before+7] == b"\0\x22\0\x22\0\x80\xfd", "DMA marker program mismatch")


def live_controller(debugger):
    """Reviewed read-only SFR set, only after this runner's reset/image/DMA gate."""
    with debugger._stopped_operation(memory_access=True) as deadline:
        require(debugger._exchange_byte(READ_CONFIG, deadline) == 0x22,
                "DMA register inspection prohibited with an unconfirmed/paused debug configuration")
        with debugger._preserve_registers(deadline):
            return bytes(debugger._instruction(bytes((0xe5, r)), deadline)
                         for r in (0xd6, 0xd7, 0xd1, 0xc0, 0xd4, 0xd5, 0xd2, 0xd3))


def inspect(debugger, image, pc, *, internal=False):
    require(debugger.read_debug_config() == 0x22, "DMA checkpoint requires config22; no automatic correction")
    status = debugger.read_debug_status()
    debugger._check_status(status, active=True, halted=True)
    require(status & Status.HALT_STATUS, "DMA checkpoint is not an observed breakpoint halt")
    registers = debugger.read_registers()
    p = image.dma_proof
    require(registers.pc == pc and registers.dps == 0 and registers.sp ==
            (p["deadline_sp"] if internal else image.metrics["iram_stack_start"]+1),
            "DMA checkpoint PC/DPS/stack mismatch")
    record = decode(debugger.read_xdata(p["state"], SIZE), allow_running=internal)
    boot = debugger.read_xdata(0x1e00, 32)
    startup = decode_bootstrap(boot, image.board)
    context = None
    if record["phase"] == 3:
        cfg = 0 if record["stage"] == 0 else p["xdata_start"]
        require(record["controller"][4:6] == list(cfg.to_bytes(2, "little")),
                "DMA READY channel-0 configuration changed")
    if internal:
        require(pc == p["address"], "DMA internal stop is not the proved expiry RET")
        context = inspect_expiry(p, debugger.read_xdata, registers.sp, registers.dptr0 & 255,
                                 registers.dps, live_controller(debugger), record)
    elif record["phase"] == 3 and record["stage"] in (1, 3):
        source, destination = ((p["a"]+1, p["b"]+1) if record["stage"] == 1 else (p["b"]+1, p["a"]+1))
        require((record["source"], record["destination"]) == (source, destination) and
                record["controller"][4:6] == list(p["xdata_start"].to_bytes(2, "little")),
                "DMA accepted an unexpected route/descriptor address")
        a, b = expected_buffers(record["stage"], record["completed"], record["length"])
        require(debugger.read_xdata(p["a"], 18) == a and debugger.read_xdata(p["b"], 18) == b,
                "DMA actual source/destination/tail/guard mismatch")
    # FAULT records are outside the owned buffers. Never inspect their payload.
    require(debugger.read_registers() == registers, "DMA read-only inspection changed CPU context")
    return record, startup, boot, registers, context, status


def invariants(record, boot, initial_boot, initial):
    require(boot[:8]+boot[9:] == initial_boot[:8]+initial_boot[9:] and boot[8] == record["completed"],
            "DMA immutable M0/heartbeat mismatch")
    require(record["sleep"] == record["initial_sleep"] == initial["initial_sleep"] and
            record["enables"] == [0, 0, 0] and record["initial_flags"] == record["flags"] ==
            initial["initial_flags"] and record["initial_cfg1"] == initial["initial_cfg1"] and
            record["initial_ircon"] == initial["initial_ircon"], "DMA clock/IRQ/foreign ownership changed")


def check_timeout(record, proof):
    d = record["dma"]
    descriptor = ((proof["b"]+1).to_bytes(2, "big") + (proof["a"]+1).to_bytes(2, "big") +
                  b"\0\x10\x20\x51")
    require(record["phase"] == 4 and record["reason"] == 4 and record["stage"] == 3 and
            record["completed"] == 0 and record["clock_result"] == 0 and record["dma_result"] == 8 and
            record["fault_latch"] == 8 and record["length"] == 16 and record["checked"] == 0 and
            record["mismatch_buffer"] == record["mismatch_index"] == 255 and
            record["command"] == record["status"] == 0x88 and record["sample_valid"] == 1 and
            d["actions"] == 7 and d["verified"] == 0 and d["sample_valid"] == 1 and
            d["polls"] == 4 and d["timebase_status"] == 0 and 1024 < d["elapsed_ticks"] < 0x800000 and
            d["arm"] <= 1 and d["request"] <= 1 and d["irq"] <= 1 and
            d["ircon"] == record["initial_ircon"] and not d["ircon"] & 1 and
            [d["cfg0_low"], d["cfg0_high"]] == record["controller"][4:6] and
            [d["cfg1_low"], d["cfg1_high"]] == record["initial_cfg1"] and
            all(v <= 1 for v in record["controller"][:3]) and
            record["controller"][3] == record["initial_ircon"] and
            record["controller"][6:] == record["initial_cfg1"] and
            record["controller"][4:6] == list(proof["xdata_start"].to_bytes(2, "little")) and
            (record["source"], record["destination"]) == (proof["b"]+1, proof["a"]+1) and
            bytes(record["descriptor"]) == descriptor and
            d["complete"] == int((d["arm"], d["request"], d["irq"]) == (0, 0, 1)),
            "Induced hold did not yield exact REQUESTED/unverified terminal DMA_TIMEOUT: " +
            json.dumps(record, sort_keys=True))


def exercise(debugger, image, program, cycles=1, induced=False):
    validate_program(image, program, cycles, induced)
    adapter, reset_config = reset_and_verify_code(debugger, program, "DMA")
    reset_core = debugger.read_registers()
    config = debugger.enable_dma_after_reset()
    require(config == 0x22 and debugger.read_registers() == reset_core,
            "DMA gate did not preserve the full reset CPU/FMAP")
    gate_status = debugger.read_debug_status()
    require(gate_status == 0x22, "DMA reset/gate status changed before first resume")
    p = image.dma_proof
    before, ready, fault = p["checkpoints"]
    for slot in range(4): debugger.set_breakpoint(slot, before, False)
    for slot, address in enumerate((before, ready, fault)): debugger.set_breakpoint(slot, address)
    debugger.resume()
    pc = wait_checkpoint(debugger, before, fault, "DMA")
    initial, startup, initial_boot, registers, _, initial_status = inspect(debugger, image, pc)
    require(pc == before and initial["phase"] == 1 and startup["heartbeat"] == 0,
            "DMA initialization failed: " + json.dumps(initial, sort_keys=True))
    debugger.set_breakpoint(0, before, False)
    step_nop(debugger, registers, "DMA")
    observations, statuses, context = [], [initial_status], None
    count = 4 if induced else 1+4*cycles
    for index in range(count):
        if induced and index == 3:
            debugger.set_breakpoint(3, p["arm_ret"])
            debugger.resume()
            require(wait_checkpoint(debugger, p["arm_ret"], fault, "DMA arm") == p["arm_ret"],
                    "DMA failed before its nine-NOP arm return")
            # No slow context inspection here: require the next actual sample
            # still unexpired, rather than assuming breakpoint I/O took <1024 ticks.
            debugger.set_breakpoint(3, p["address"])
            debugger.resume()
            pc = wait_checkpoint(debugger, p["address"], fault, "DMA expiry")
            require(pc == p["address"], "DMA failed before the genuine expiry RET")
            running, _, boot, registers, context, status = inspect(debugger, image, pc, internal=True)
            invariants(running, boot, initial_boot, initial)
            statuses.append(status)
            debugger.set_breakpoint(3, p["address"], False)
            hold_halted(debugger)
            require(debugger.read_registers() == registers, "DMA hold changed CPU context")
            require(debugger.read_debug_config() == 0x22, "DMA debug configuration changed during hold")
        debugger.resume()
        pc = wait_checkpoint(debugger, ready, fault, "DMA")
        record, _, boot, registers, _, status = inspect(debugger, image, pc)
        if induced and index == 3:
            require(pc == fault, "DMA hold did not reach terminal FAULT")
            check_timeout(record, p)
        else:
            require(pc == ready and record["phase"] == 3,
                    "DMA terminal failure: " + json.dumps(record, sort_keys=True))
            require(record["stage"] == (0 if not index else (index-1)%4+1) and
                    record["completed"] == (index//4)&255, "DMA stage/counter progression mismatch")
        invariants(record, boot, initial_boot, initial)
        observations.append(record); statuses.append(status)
        if index+1 < count: step_nop(debugger, registers, "DMA")
    return {
        "evidence": "hardware-observed", "scope": "compiled-software-triggered-channel0-dma-fixture",
        "board": image.board, "image": image.image_name, "image_sha256": image.sha256,
        "adapter": asdict(adapter), "verified_code_bytes": len(program),
        "debug_gate": {"reset_config": reset_config, "requested_config": 0x22, "confirmed_config": config,
                       "observed_status": gate_status, "preserved_fmap": reset_core.bank},
        "checkpoint_statuses": statuses, "completed_cycles": 0 if induced else cycles,
        "confirmed_copies": sum(r["stage"] in (1, 3) and r["dma"]["verified"] == 1 for r in observations),
        "verified_bytes": sum(r["length"] for r in observations if r["dma"]["verified"] == 1),
        "observations": observations, "induced_timeout": induced, "deadline_context": context,
        "hold_seconds": HOLD_SECONDS if induced else None, "linked_proof": p,
        "startup_status": startup, "register_preservation": True, "initial_reset_pc": 0, "final_pc": pc,
        "final_cpu": "halted-at-dma-fault" if induced else "halted-at-dma-ready",
        "fault_payload_inspected": False, "error_is_quiescence": False,
        "not_tested_by_this_run": ["physical stuck-DMA injection or abort recovery", "other DMA channels or triggers",
                                 "calibrated time, sleep/wake, interrupts, RF, AES or flash services"],
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bus", type=int, required=True)
    parser.add_argument("--address", type=int, required=True)
    parser.add_argument("--board", choices=tuple(BOARDS), required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cycles", type=int, default=1)
    parser.add_argument("--induce-timeout", action="store_true")
    parser.add_argument("--confirm-dma-test", action="store_true",
                        help="authorize reset, full CODE inspection, explicit config26->22, and this DMA fixture")
    args = parser.parse_args(argv)
    try:
        require(args.confirm_dma_test, "Manual acceptance requires --confirm-dma-test")
        require(1 <= args.cycles <= 257 and (not args.induce_timeout or args.cycles == 1),
                "Cycles must be 1..257; timeout mode requires --cycles 1")
        address = UsbAddress(args.bus, args.address)
        image = DebugImage(args.output, args.board, "dma_fixture")
        program = (args.output/"dma_fixture.bin").read_bytes()
        validate_program(image, program, args.cycles, args.induce_timeout)
        with Debugger(PyUsbBackend.load(), Access.RESET_DEBUG_SESSION, timeout_ms=10_000,
                      allow_cpu_control=True, allow_target_reset=True, allow_memory_access=True,
                      allow_breakpoints=True, allow_dma_enable=True) as debugger:
            debugger.open(address)
            result = exercise(debugger, image, program, args.cycles, args.induce_timeout)
    except (DebuggerError, ValueError, OSError, KeyError) as error:
        print(f"dma-hardware-check: {error}", file=sys.stderr)
        return 1
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
