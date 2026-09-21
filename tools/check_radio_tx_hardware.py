#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""MANUAL opt-in ONE conditional-clear RF attempt. Never flashes or selects USB automatically."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import time

from cc_debugger import Access, Debugger, DebuggerError, PyUsbBackend, UsbAddress
from check_timebase_hardware import reset_and_verify_code, step_nop, wait_checkpoint
from debug_image import decode_bootstrap
from radio_tx_fixture import BODY, CHANNEL, CHECKPOINTS, HASHES, SIZE, check_end, decode, load_image, packet
from verify_firmware import require


def validate_program(image, program, sha256, rf_permission):
    require(rf_permission is True, "Explicit one-attempt RF permission is required")
    require(image.image_name == "radio_tx_fixture" and image.board in HASHES,
            "Requires exact separately selected radio_tx_fixture board image; never a component test")
    size, digest = HASHES[image.board][:2]
    require(type(program) is bytes and len(program) == size == image.metrics["image_extent_bytes"] and
            type(sha256) is str and sha256 == image.sha256 == digest ==
            hashlib.sha256(program).hexdigest(), "TX explicit/checked/program CODE identity differs")
    pcs = [image.symbol(name).address for name in CHECKPOINTS]
    require(pcs == image.radio_tx_proof["checkpoints"] and
            program[pcs[0]:pcs[0]+8] == b"\0\x22\0\x80\xfd\0\x80\xfd",
            "TX checkpoint identity differs")


class Deadline:
    """One finite experiment deadline in addition to each transport operation.

    No new target/USB operation. A late completed operation is still failure;
    an in-flight operation retains the transport's independent 10-second cap.
    """
    def __init__(self, target, label="TX"):
        self.target, self.end, self.label = target, time.monotonic()+60, label

    def __getattr__(self, name):
        method = getattr(self.target, name)

        def bounded(*args, **kwargs):
            require(time.monotonic() < self.end, f"{self.label} experiment deadline exhausted; no recovery attempted")
            result = method(*args, **kwargs)
            require(time.monotonic() < self.end, f"{self.label} experiment completed late; no recovery attempted")
            return result
        return bounded


def inspect(debugger, image, pc):
    registers = debugger.read_registers()
    require(registers.pc == pc and registers.dps == 0 and not registers.psw & 0x18 and
            registers.sp == image.metrics["iram_stack_start"]+1,
            "TX checkpoint PC/bank/DPS/SP mismatch")
    p = image.radio_tx_proof
    raw = debugger.read_xdata(p["state"], SIZE)
    mailbox = debugger.read_xdata(p["mailbox"], 8)
    diagnostics = [debugger.read_xdata(p[n], size) for n, size in
                   (("clock", 19), ("fifo", 21), ("tx", 29))]
    boot = debugger.read_xdata(0x1e00, 32)
    record = decode(raw); decode_bootstrap(boot, image.board)
    require(debugger.read_registers() == registers, "TX inspection changed complete CPU context")
    require(mailbox == bytes(8) and boot[8] == record["completed"], "TX mailbox/heartbeat mismatch")
    if record["phase"] in (1, 2, 3):
        require(all(d == bytes(len(d)) for d in diagnostics), "TX admission touched service diagnostics")
    check_end(record, *diagnostics)
    return record, boot, registers


def exercise(debugger, image, program, sha256, rf_permission=False, admit_only=False):
    validate_program(image, program, sha256, rf_permission)
    require(type(admit_only) is bool, "Invalid TX admission-only choice")
    debugger = Deadline(debugger)
    _, config = reset_and_verify_code(debugger, program, "boot-disarmed TX")
    wait, end, fault = image.radio_tx_proof["checkpoints"]
    for slot in range(4): debugger.set_breakpoint(slot, wait, False)
    for slot, address in enumerate((wait, end, fault)): debugger.set_breakpoint(slot, address)
    debugger.resume()
    pc = wait_checkpoint(debugger, wait, fault, "TX DISARMED")
    record, original_boot, registers = inspect(debugger, image, pc)
    require(pc == wait and record["phase"] == 1 and record["remaining"] == 256,
            "TX did not boot DISARMED")

    def advance(expected_phase):
        nonlocal record, registers
        step_nop(debugger, registers, "TX admission")
        debugger.resume()
        pc = wait_checkpoint(debugger, wait, fault, "TX admission")
        record, boot, registers = inspect(debugger, image, pc)
        require(pc == wait and record["phase"] == expected_phase and boot == original_boot,
                "TX admission FAULT/mismatch; no RF continuation")

    # Actual default no-command resume, not simply a decoded initial structure.
    advance(1)
    require(record["remaining"] == 255, "TX default poll did not consume exactly one admission unit")
    for stage, next_phase in (("arm", 2), ("run", 3)):
        require(record["phase"] == next_phase-1 and registers.pc == wait, "TX wrong mailbox phase")
        data = packet(stage)
        debugger.write_xdata(image.radio_tx_proof["mailbox"], data)
        require(debugger.read_xdata(image.radio_tx_proof["mailbox"], 8) == data and
                debugger.read_registers() == registers, "TX packet write/readback/context failed")
        advance(next_phase)
    # RUN consumption has returned and was inspected with zero service state.
    require(record["remaining"] == 0 and record["attempts"] == 0, "TX admission already did work")
    if not admit_only:
        debugger.set_breakpoint(0, wait, False)
        step_nop(debugger, registers, "TX admitted continuation")
        debugger.resume()  # continuous real clock -> FIFO -> IF_CLEAR -> clear
        pc = wait_checkpoint(debugger, end, fault, "one bounded conditional TX")
        record, boot, registers = inspect(debugger, image, pc)
        require(boot[:8]+boot[9:] == original_boot[:8]+original_boot[9:], "TX immutable M0 changed")
        require(pc == end and record["phase"] == 5,
                f"TX FAULT reason={record['reason']} stage={record['stage']} "
                f"result={record['tx_result']}; RF may remain active/transmission may have occurred; "
                "no retry/reset/resume/flush attempted")
    return dict(evidence="hardware-observed", scope="one-conditional-clear-tx-fixture",
                board=image.board, image=image.image_name, image_sha256=sha256,
                profile_channel=CHANNEL, profile_txpower_raw=5,
                debug_config=config, verified_code_bytes=len(program), register_preservation=True,
                final_pc=registers.pc, final_cpu="halted-admitted" if admit_only else "halted-END",
                rf_attempts=record["attempts"], result="ADMITTED" if admit_only else
                "PHY_DONE" if record["tx_result"] == 0 else "CCA_BUSY",
                synthetic_body_hex=BODY.hex(),
                not_tested=["independent on-air bytes/FCS", "calibrated power/CCA/timing",
                            "ACK/delivery/retry", "RX/TX adapter", "MAC/security/network acceptance",
                            "physical fault containment/recovery"])


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bus", type=int, required=True)
    parser.add_argument("--address", type=int, required=True)
    parser.add_argument("--board", choices=tuple(HASHES), required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--sha256", required=True)
    for name in ("allow-target-reset", "allow-cpu-control", "allow-memory-access",
                 "allow-memory-write", "allow-breakpoints", "confirm-rf-one-attempt", "admit-only"):
        parser.add_argument("--"+name, action="store_true")
    args = parser.parse_args(argv)
    try:
        require(all((args.allow_target_reset, args.allow_cpu_control, args.allow_memory_access,
                     args.allow_memory_write, args.allow_breakpoints, args.confirm_rf_one_attempt)),
                "Separate reset/CPU/read/write/breakpoint permissions and RF opt-in are all required")
        address = UsbAddress(args.bus, args.address)
        image = load_image(args.output, args.board)
        program = (args.output/"radio_tx_fixture.bin").read_bytes()
        validate_program(image, program, args.sha256, args.confirm_rf_one_attempt)
        # Backend import/loading/open happens ONLY after all offline preflight.
        with Debugger(PyUsbBackend.load(), Access.RESET_DEBUG_SESSION, timeout_ms=10_000,
                      allow_cpu_control=True, allow_target_reset=True, allow_memory_access=True,
                      allow_memory_write=True, allow_breakpoints=True) as debugger:
            debugger.open(address)
            result = exercise(debugger, image, program, args.sha256, True, args.admit_only)
    except (DebuggerError, ValueError, OSError, KeyError) as error:
        print(f"one-tx-check: {error}", file=sys.stderr)
        return 1
    # No identities, received payload or sensitive captures are collected.
    # Context-manager cleanup failure prevents publishing success.
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
