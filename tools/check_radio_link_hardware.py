#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""MANUAL same-owner TX/RX session, including initial AUTOACK. Never flashes."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import sys

from cc_debugger import Access, Debugger, DebuggerError, PyUsbBackend, UsbAddress
from check_radio_tx_hardware import Deadline
from check_timebase_hardware import reset_and_verify_code, step_nop, wait_checkpoint
from debug_image import decode_bootstrap
from private_artifacts import private_capture
from radio_link_fixture import (
    BODY, CHANNEL, CHECKPOINTS, HASHES, SIZE, check_end, decode, decode_frames, load_image, packet,
)
from verify_firmware import require


def validate_program(image, program, sha256, rf_permission):
    require(rf_permission is True, "Explicit same-owner RF session INCLUDING initial AUTOACK is required")
    require(image.image_name == "radio_link_fixture" and image.board in HASHES,
            "Requires exact radio_link_fixture board image, never a component test")
    size, digest = HASHES[image.board][:2]
    require(type(program) is bytes and len(program) == size == image.metrics["image_extent_bytes"] and
            type(sha256) is str and sha256 == image.sha256 == digest == hashlib.sha256(program).hexdigest(),
            "Link explicit/checked/program CODE identity differs")
    pcs = [image.symbol(name).address for name in CHECKPOINTS]
    require(pcs == image.radio_link_proof["checkpoints"] and
            program[pcs[0]:pcs[0]+8] == b"\0\x22\0\x80\xfd\0\x80\xfd", "Link checkpoint identity differs")


def save(capture, data):
    capture.write(json.dumps(data, sort_keys=True) + "\n")
    capture.flush()
    os.fsync(capture.fileno())


def inspect(debugger, image, pc, capture):
    registers = debugger.read_registers()
    require(registers.pc == pc and registers.dps == 0 and not registers.psw & 0x18 and
            registers.sp == image.metrics["iram_stack_start"]+1, "Link checkpoint PC/DPS/bank/SP mismatch")
    p = image.radio_link_proof
    raw = debugger.read_xdata(p["state"], SIZE)
    frames = debugger.read_xdata(p["frames"], 256)
    clock = debugger.read_xdata(p["clock"], 19)
    radio = debugger.read_xdata(p["radio"], 26)
    mailbox = debugger.read_xdata(p["mailbox"], 8)
    boot = debugger.read_xdata(0x1e00, 32)
    require(debugger.read_registers() == registers, "Link inspection changed full CPU context")
    # Preserve complete fault/CRC-bad/late-drained material before validating it.
    save(capture, dict(schema=1, checkpoint=pc, state_hex=raw.hex(), frames_hex=frames.hex(),
                       clock_hex=clock.hex(), radio_hex=radio.hex(), boot_hex=boot.hex(),
                       mailbox_hex=mailbox.hex()))
    record = decode(raw); decode_frames(record, frames); check_end(record, clock, radio)
    decode_bootstrap(boot, image.board)
    require(mailbox == bytes(8) and boot[8] == record["completed"], "Link mailbox/heartbeat mismatch")
    return record, boot, registers


def exercise(debugger, image, program, sha256, capture, rf_permission=False, admit_only=False):
    validate_program(image, program, sha256, rf_permission)
    require(type(admit_only) is bool, "Invalid link admission-only choice")
    save(capture, dict(schema=1, scope="same-owner-radio-session", board=image.board,
                       image_sha256=sha256, initial_autoack_authorized=True, admit_only=admit_only))
    debugger = Deadline(debugger, "link")
    _, config = reset_and_verify_code(debugger, program, "boot-disarmed link")
    wait, end, fault = image.radio_link_proof["checkpoints"]
    for slot in range(4):
        debugger.set_breakpoint(slot, wait, False)
    for slot, address in enumerate((wait, end, fault)):
        debugger.set_breakpoint(slot, address)
    debugger.resume()
    pc = wait_checkpoint(debugger, wait, fault, "link DISARMED")
    record, original, registers = inspect(debugger, image, pc, capture)
    require(pc == wait and record["phase"] == 1 and record["remaining"] == 256, "Link did not boot DISARMED")

    def advance(phase):
        nonlocal record, registers
        step_nop(debugger, registers, "link admission")
        debugger.resume()
        pc = wait_checkpoint(debugger, wait, fault, "link admission")
        record, boot, registers = inspect(debugger, image, pc, capture)
        require(pc == wait and record["phase"] == phase and boot == original,
                "Link admission FAULT/mismatch; no RF continuation")

    advance(1)
    require(record["remaining"] == 255, "Link default poll did not consume exactly one admission unit")
    for stage, phase in (("arm", 2), ("run", 3)):
        require(record["phase"] == phase-1 and registers.pc == wait, "Link wrong mailbox phase")
        data = packet(stage)
        debugger.write_xdata(image.radio_link_proof["mailbox"], data)
        require(debugger.read_xdata(image.radio_link_proof["mailbox"], 8) == data and
                debugger.read_registers() == registers, "Link packet write/readback/context failed")
        advance(phase)
    require(record["remaining"] == record["attempts"] == record["consumed"] == 0,
            "Link admission already performed work")
    if not admit_only:
        debugger.set_breakpoint(0, wait, False)
        step_nop(debugger, registers, "link admitted continuation")
        debugger.resume()
        pc = wait_checkpoint(debugger, end, fault, "bounded same-owner TX/RX")
        record, boot, registers = inspect(debugger, image, pc, capture)
        require(boot[:8]+boot[9:] == original[:8]+original[9:], "Link immutable M0 changed")
        require(pc == end and record["phase"] == 5,
                f"Link FAULT reason={record['reason']} stage={record['stage']} "
                f"owner={record['owner_result']} tx={record['tx_result']}; RF may remain active; "
                "no retry/reset/resume/flush attempted")
    return dict(evidence="hardware-observed", scope="same-owner-radio-session",
                board=image.board, image=image.image_name, image_sha256=sha256,
                profile_channel=CHANNEL, profile_txpower_raw=5, synthetic_body_hex=BODY.hex(),
                verified_code_bytes=len(program), debug_config=config, register_preservation=True,
                final_pc=registers.pc, final_cpu="halted-admitted" if admit_only else "halted-END",
                ordinary_tx_attempts=record["attempts"], initial_autoack_possible=not admit_only,
                outcome="ADMITTED" if admit_only else
                {1: "RX_CRC_GOOD", 2: "RX_CRC_BAD", 3: "RX_WINDOW_TIMEOUT", 4: "CCA_BUSY"}[record["outcome"]],
                retained_frames=record["frames"], before_tx_frames=record["before_tx"],
                not_tested=["independent on-air bytes/FCS", "ACK identity/delivery/retry",
                            "captured PHY end or MAC ACK window", "calibrated CCA/power/time",
                            "network/security acceptance", "physical fault containment/recovery"])


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bus", type=int, required=True)
    parser.add_argument("--address", type=int, required=True)
    parser.add_argument("--board", choices=tuple(HASHES), required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--sha256", required=True)
    parser.add_argument("--capture", type=Path, required=True)
    for name in ("allow-target-reset", "allow-cpu-control", "allow-memory-access",
                 "allow-memory-write", "allow-breakpoints", "confirm-rf-including-autoack", "admit-only"):
        parser.add_argument("--"+name, action="store_true")
    args = parser.parse_args(argv)
    try:
        require(all((args.allow_target_reset, args.allow_cpu_control, args.allow_memory_access,
                     args.allow_memory_write, args.allow_breakpoints, args.confirm_rf_including_autoack)),
                "Separate reset/CPU/read/write/breakpoint permissions and initial-AUTOACK RF opt-in are required")
        address = UsbAddress(args.bus, args.address)
        image = load_image(args.output, args.board)
        program = (args.output/"radio_link_fixture.bin").read_bytes()
        validate_program(image, program, args.sha256, args.confirm_rf_including_autoack)
        with private_capture(args.capture) as capture:
            with Debugger(PyUsbBackend.load(), Access.RESET_DEBUG_SESSION, timeout_ms=10_000,
                          allow_cpu_control=True, allow_target_reset=True, allow_memory_access=True,
                          allow_memory_write=True, allow_breakpoints=True) as debugger:
                debugger.open(address)
                result = exercise(debugger, image, program, args.sha256, capture, True, args.admit_only)
    except (DebuggerError, ValueError, OSError, KeyError) as error:
        print(f"radio-link-check: {error}", file=sys.stderr)
        return 1
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
