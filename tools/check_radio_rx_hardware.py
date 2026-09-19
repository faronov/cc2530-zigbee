#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""MANUAL passive RX only. Explicit reset/selection/private capture; never flash."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import sys

from cc_debugger import Access, Debugger, DebuggerError, PyUsbBackend, UsbAddress
from check_timebase_hardware import reset_and_verify_code, step_nop, wait_checkpoint
from debug_image import DebugImage, decode_bootstrap
from radio_rx_fixture import CHECKPOINTS, SIZE, check_frame, decode
from private_artifacts import private_capture
from verify_firmware import BOARDS, CODE_LIMIT, ROOT, require


def validate_program(image, program, attempts):
    require(image.image_name == "radio_rx_fixture", "Requires the checked radio_rx_fixture board image")
    require(type(attempts) is int and 1 <= attempts <= 16, "Attempts must be 1..16")
    require(type(program) is bytes and 0 < len(program) <= CODE_LIMIT and
            len(program) == image.metrics["image_extent_bytes"] and
            hashlib.sha256(program).hexdigest() == image.sha256, "Program differs from checked RX image")
    pcs = [image.symbol(n).address for n in CHECKPOINTS]
    require(pcs == image.radio_rx_proof["checkpoints"] and
            program[pcs[0]:pcs[0]+10] == b"\0\x22\0\x22\0\x80\xfd\0\x80\xfd",
            "RX checkpoint artifacts mismatch")


def inspect(debugger, image, pc):
    registers = debugger.read_registers()
    require(registers.pc == pc and registers.dps == 0 and
            registers.sp == image.metrics["iram_stack_start"]+1, "RX checkpoint PC/DPS/SP mismatch")
    p = image.radio_rx_proof
    raw = debugger.read_xdata(p["state"], SIZE)
    frame = debugger.read_xdata(p["frame"], 128)
    diagnostic = debugger.read_xdata(p["diagnostics"], 31)
    boot = debugger.read_xdata(0x1e00, 32)
    record = decode(raw); decode_bootstrap(boot, image.board); check_frame(record, frame)
    require(not record["attempt"] or raw[41:72] == diagnostic, "RX diagnostic serialization mismatch")
    if record["clock_result"] != 8:
        require(debugger.read_xdata(p["clock"], 19) == raw[22:41], "RX clock serialization mismatch")
    require(boot[8] == record["completed"], "RX heartbeat mismatch")
    require(debugger.read_registers() == registers, "RX inspection changed full CPU context")
    return record, raw, frame, boot, registers


def save(capture, record, raw, frame):
    # Raw data belongs ONLY to the explicitly selected private file, including
    # CRC failure/fault records. A failed write stops before another resume.
    capture.write(json.dumps({"schema": 1, "attempt": record["attempt"],
                             "state_hex": raw.hex(), "frame_hex": frame.hex()})+"\n")
    capture.flush()
    os.fsync(capture.fileno())


def progression(record, previous, boot, initial_boot):
    require(boot[:8]+boot[9:] == initial_boot[:8]+initial_boot[9:], "RX immutable M0 changed")
    require(record["initial_flags"] == previous["initial_flags"] and
            record["initial_sleep"] == previous["initial_sleep"], "RX initial ownership changed")
    require(record["flags"][6] in (previous["flags"][6], previous["flags"][6] | 128),
            "RX STIF history regressed or unrelated IRCON changed")
    if record["attempt"]:
        require(record["attempt"] == previous["attempt"]+1 and record["clock"] == previous["clock"],
                "RX attempt/clock progression mismatch")
        require(record["completed"] == previous["completed"] +
                int(record["result"] == 0 and record["phase"] == 3), "RX success counter mismatch")


def exercise(debugger, image, program, capture, attempts=1):
    validate_program(image, program, attempts)
    _, config = reset_and_verify_code(debugger, program, "passive RX")
    before, ready, fault, end = image.radio_rx_proof["checkpoints"]
    for slot, address in enumerate((before, ready, fault, end)):
        debugger.set_breakpoint(slot, address)
    debugger.resume()
    pc = wait_checkpoint(debugger, before, fault, "RX initialization")
    previous, raw, frame, initial_boot, registers = inspect(debugger, image, pc)
    save(capture, previous, raw, frame)
    require(pc == before and previous["phase"] == 1 and previous["attempt"] == 0,
            "RX initialization FAULT; separate full-reset recovery required")
    debugger.set_breakpoint(0, before, False)
    for index in range(attempts+1):
        step_nop(debugger, registers, "RX")
        debugger.resume()
        pc = wait_checkpoint(debugger, ready, fault, "bounded RX")
        record, raw, frame, boot, registers = inspect(debugger, image, pc)
        save(capture, record, raw, frame)
        # Record a real failure before raising. No reset, RF-off or retry.
        require(pc == ready and record["phase"] == 3,
                f"RX FAULT reason={record['reason']} result={record['result']}; "
                "RX may remain active; separate full-reset recovery required")
        require(record["attempt"] == index and record["stage"] == 1,
                "RX unexpected checkpoint progression")
        progression(record, previous, boot, initial_boot)
        previous = record
    return {"evidence": "hardware-observed", "scope": "bounded-passive-rx-only",
            "board": image.board, "attempts": record["attempt"], "completed": record["completed"],
            "bad_crc": record["attempt"]-record["completed"], "debug_config": config,
            "verified_code_bytes": len(program), "final_cpu": "halted-at-ready",
            "register_preservation": True,
            "not_tested": ["sniffer agreement", "lossless RX", "calibrated RSSI/LQI",
                           "MAC/security/network acceptance", "TX/ACK", "physical fault recovery"]}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bus", type=int, required=True)
    parser.add_argument("--address", type=int, required=True)
    parser.add_argument("--board", choices=tuple(BOARDS), required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--capture", type=Path, required=True)
    parser.add_argument("--attempts", type=int, default=1)
    parser.add_argument("--confirm-passive-rx-test", action="store_true")
    args = parser.parse_args(argv)
    try:
        require(args.confirm_passive_rx_test, "Requires --confirm-passive-rx-test")
        address = UsbAddress(args.bus, args.address)
        image = DebugImage(args.output, args.board, "radio_rx_fixture")
        program = (args.output/"radio_rx_fixture.bin").read_bytes()
        validate_program(image, program, args.attempts)
        with private_capture(args.capture) as capture:
            with Debugger(PyUsbBackend.load(), Access.RESET_DEBUG_SESSION, timeout_ms=10_000,
                          allow_cpu_control=True, allow_target_reset=True, allow_memory_access=True,
                          allow_breakpoints=True) as debugger:
                debugger.open(address)
                result = exercise(debugger, image, program, capture, args.attempts)
    except (DebuggerError, ValueError, OSError, KeyError) as error:
        # No record/frame representation or hash is formatted in error paths.
        print(f"passive-rx-check: {error}", file=sys.stderr)
        return 1
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
