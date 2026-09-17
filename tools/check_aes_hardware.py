#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Explicit manual AES/DMA board acceptance; never programs or recovers a target."""

import argparse
from dataclasses import asdict
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

from aes_fixture import (
    CONTROLLER, FLAGS, READS, SIZE, check_timeout, decode, expected_buffers,
    inspect_context, public_vectors, unpack_aes,
)
from cc2530_debug import READ_CONFIG, READ_STATUS, Status
from cc_debugger import Access, Debugger, DebuggerError, PyUsbBackend, UsbAddress
from check_timebase_hardware import reset_and_verify_code, step_nop, wait_checkpoint
from debug_image import DebugImage, decode_bootstrap
from verify_firmware import BOARDS, CODE_LIMIT, require

HOLD_SECONDS = 2.0
OBSERVATIONS = READS + FLAGS


def validate_program(image, program, vectors, cycles, negative):
    require(image.image_name == "aes_fixture", "Acceptance requires board aes_fixture, never aes_test")
    require(type(cycles) is int and 1 <= cycles <= 257 and negative in ("none", "pre-key", "final") and
            (negative == "none" or cycles == 1), "Choose 1..257 cycles, or one separately reset negative")
    require(isinstance(program, bytes) and 0 < len(program) <= CODE_LIMIT and
            len(program) == image.metrics["image_extent_bytes"] and
            hashlib.sha256(program).hexdigest() == image.sha256, "Program differs from verified AES board image")
    p = image.aes_proof
    require(len(vectors) == 21 and all(isinstance(v, bytes) and len(v) == 49 for v in vectors) and
            program[p["vectors"]:p["vectors"]+1029] == b"".join(vectors),
            "Public fixture corpus differs from independently checked host reference")
    for name, bit in (("arm_input", 1), ("arm_output", 2)):
        a = p[name]
        require(program[a:a+13] == b"\x75\xd6"+bytes([bit])+bytes(9)+b"\x22",
                "AES real individual arm/readiness interval changed")
    require(p["arm_ret"] == p["arm_input"]+12 and
            program[p["expiry"]-3:p["expiry"]+1] == b"\x75\x82\0\x22" and
            program[p["expiry_call"]:p["expiry_call"]+3] == b"\x12"+(p["expiry"]-167).to_bytes(2, "big") and
            p["pre_latch"] == p["reader"]+3 and program[p["reader"]:p["pre_latch"]+2] == b"\x90\0\0\xe5\x95" and
            program[p["reader_call"]:p["reader_call"]+3] == b"\x12"+p["reader"].to_bytes(2, "big") and
            program[p["final_gate"]-2:p["final_gate"]] == b"\xf5\x98",
            "AES actual expiry/pre-latch/unique final-ack path changed")
    before, ready, fault = p["checkpoints"]
    require((ready, fault) == (before+2, before+4) and
            program[before:before+7] == b"\0\x22\0\x22\0\x80\xfd", "AES marker program mismatch")


def live_registers(debugger):
    """Fixture-only, read-only whitelist. ENCDI/ENCDO are deliberately absent."""
    with debugger._stopped_operation(memory_access=True) as deadline:
        require(debugger._exchange_byte(READ_CONFIG, deadline) == 0x22,
                "AES/DMA inspection requires confirmed config22; no automatic correction")
        with debugger._preserve_registers(deadline):
            return bytes(debugger._instruction(bytes((0xe5, r)), deadline) for r in OBSERVATIONS)


def inspect(debugger, image, pc, vectors, mode="none"):
    require(debugger.read_debug_config() == 0x22, "AES checkpoint requires config22")
    status = debugger.read_debug_status()
    debugger._check_status(status, active=True, halted=True)
    require(status & Status.HALT_STATUS, "AES checkpoint is not an observed breakpoint halt")
    regs = debugger.read_registers(); p = image.aes_proof
    expected_pc = p["expiry"] if mode == "pre-key" else p["pre_latch"] if mode == "final" else pc
    require(regs.pc == pc == expected_pc and regs.dps == 0 and
            (mode != "none" or regs.sp == image.metrics["iram_stack_start"]+1),
            "AES checkpoint PC/DPS/stack changed")
    r = decode(debugger.read_xdata(p["state"], SIZE), allow_running=mode != "none")
    boot = debugger.read_xdata(0x1e00, 32); startup = decode_bootstrap(boot, image.board)
    live = live_registers(debugger); values = dict(zip(OBSERVATIONS, live))
    require(bytes(values[a] for a in FLAGS) == bytes(r["initial_flags"]) and
            bytes(values[a] for a in (0xa8, 0xb8, 0x9a)) == bytes(3) and
            values[0xbe] == r["initial_sleep"] and values[0xc0] == r["initial_ircon"],
            "AES live unrelated flags/IRQ/sleep ownership changed")
    context = None
    if mode != "none":
        require(values[0xc6] == values[0x9e] == 0x88, "AES internal stop is not stable XOSC32")
        context = inspect_context(p, debugger.read_xdata, regs.sp, regs.dptr0 & 255, regs.dps,
                                  bytes(values[a] for a in CONTROLLER), r, mode)
    elif r["phase"] in (1, 3):
        require(values[0xc6] == r["command"] and values[0x9e] == r["status"] and
                not any(values[a] for a in (0xd6, 0xd7, 0xd1)) and values[0x98] == r["initial_enc"],
                "AES READY actual clock/controller/completion flags changed")
        cfg = bytes(4) if r["phase"] == 1 or r["stage"] == 0 else b"\x45\0\x4d\0"
        require(bytes(values[a] for a in (0xd4, 0xd5, 0xd2, 0xd3)) == cfg and
                (not values[0xb3] & ~8 if cfg == bytes(4) else values[0xb3] == 0x48),
                "AES READY actual configuration/control changed")
    if mode == "none" and r["kind"] == 2 and r["result"] != 255:
        native = unpack_aes(debugger.read_xdata(p["work"], 29))
        require(all(native[k] == v for k, v in r["aes"].items()), "AES native/wire diagnostics disagree")
        # These separate caller objects are never DMA destinations. In contrast,
        # private staging is not read here, including after failure.
        require(debugger.read_xdata(p["key"], 50) == expected_buffers(vectors[r["vector"]], r["result"] == 0),
                "AES actual caller key/input/output/sentinel/guards changed")
    require(debugger.read_registers() == regs, "AES read-only inspection changed CPU/FMAP")
    r["live_read_order"] = list(OBSERVATIONS)
    r["live_values"] = list(live)
    return r, startup, boot, regs, context, status


def invariants(r, boot, initial_boot, initial):
    require(boot[:8]+boot[9:] == initial_boot[:8]+initial_boot[9:] and boot[8] == r["completed"],
            "AES immutable M0/heartbeat changed")
    require(all(r[n] == initial[n] for n in ("initial_flags", "initial_ircon", "initial_enc", "initial_sleep")),
            "AES retained initial ownership record changed")


def hold_halted(debugger):
    with debugger._target_operation() as deadline:
        for after in (False, True):
            status = debugger._exchange_byte(READ_STATUS, deadline)
            debugger._check_status(status, active=True, halted=True)
            if not after:
                time.sleep(HOLD_SECONDS)
                deadline.remaining_ms()


def exercise(debugger, image, program, vectors, cycles=1, negative="none"):
    validate_program(image, program, vectors, cycles, negative)
    adapter, reset_config = reset_and_verify_code(debugger, program, "AES")
    reset_core = debugger.read_registers()
    config = debugger.enable_dma_after_reset()
    require(config == 0x22 and debugger.read_registers() == reset_core, "AES DMA gate changed reset CPU/FMAP")
    gate_status = debugger.read_debug_status()
    require(gate_status == 0x22, "AES reset/gate status changed before first resume")
    p = image.aes_proof; before, ready, fault = p["checkpoints"]
    for slot in range(4): debugger.set_breakpoint(slot, before, False)
    for slot, address in enumerate((before, ready, fault)): debugger.set_breakpoint(slot, address)
    debugger.resume()
    pc = wait_checkpoint(debugger, before, fault, "AES")
    initial, startup, initial_boot, regs, _, first_status = inspect(debugger, image, pc, vectors)
    require(pc == before and initial["phase"] == 1 and not startup["heartbeat"], "AES initialization failed")
    debugger.set_breakpoint(0, before, False); step_nop(debugger, regs, "AES")
    records, statuses, context = [], [first_status], None
    count = 1+4*cycles if negative == "none" else 4
    for index in range(count):
        if negative != "none" and index == 3:
            gate = p["arm_ret"] if negative == "pre-key" else p["final_gate"]
            stop = p["expiry"] if negative == "pre-key" else p["pre_latch"]
            debugger.set_breakpoint(3, gate); debugger.resume()
            require(wait_checkpoint(debugger, gate, fault, "AES unique gate") == gate, "AES failed before its unique gate")
            # No slow inspection at the intermediate stop; the final live frame
            # must prove the phase and an actually unexpired preceding sample.
            debugger.set_breakpoint(3, stop); debugger.resume()
            require(wait_checkpoint(debugger, stop, fault, "AES timed boundary") == stop, "AES failed before the timed boundary")
            running, _, boot, regs, context, status = inspect(debugger, image, stop, vectors, negative)
            invariants(running, boot, initial_boot, initial); statuses.append(status)
            debugger.set_breakpoint(3, stop, False); hold_halted(debugger)
            require(debugger.read_registers() == regs and debugger.read_debug_config() == 0x22,
                    "AES hold changed CPU/configuration")
        debugger.resume()
        pc = wait_checkpoint(debugger, ready, fault, "AES")
        r, _, boot, regs, _, status = inspect(debugger, image, pc, vectors)
        if negative != "none" and index == 3:
            require(pc == fault, "AES hold did not reach terminal FAULT")
            check_timeout(r, context)
        else:
            require(pc == ready and r["phase"] == 3, "AES terminal failure: "+json.dumps(r, sort_keys=True))
            require(r["stage"] == (0 if index == 0 else (index-1)%4+1) and r["completed"] == (index//4)&255,
                    "AES stage/counter progression changed")
        invariants(r, boot, initial_boot, initial); records.append(r); statuses.append(status)
        if index+1 < count: step_nop(debugger, regs, "AES")
    accepted = sum(r["kind"] == 2 and r["phase"] == 3 for r in records)
    aes = [r["aes"] for r in records if r["kind"] == 2 and r["result"] != 255]
    submitted = sum(bool(d["submitted"] & (1 << i)) for d in aes for i in range(3))
    confirmed = sum(bool(d["input_complete"] & (1 << i)) for d in aes for i in range(3))
    acknowledged = sum(bool(d["dma_acked"] & (1 << i)) for d in aes for i in range(3))
    published = sum(d["published"] for d in aes)
    return dict(evidence="hardware-observed", scope="compiled-aes128-dma-board-fixture", board=image.board,
                image=image.image_name, image_sha256=image.sha256, adapter=asdict(adapter), verified_code_bytes=len(program),
                debug_gate=dict(reset_config=reset_config, confirmed_config=config, observed_status=gate_status,
                                preserved_fmap=reset_core.bank), checkpoint_statuses=statuses,
                completed_cycles=cycles if negative == "none" else 0, accepted_blocks=accepted,
                command_submissions=submitted, c_confirmed_input_bytes=16*confirmed,
                c_drained_output_bytes=16*sum(d["output_drained"] for d in aes),
                published_blocks=published, published_output_bytes=16*published,
                arms_issued=sum(d["arms"] for d in aes), dma_acks_issued=sum(d["ack_issued"] for d in aes),
                dma_phases_acknowledged=acknowledged, enc_acks_issued=sum(d["enc_ack_issued"] for d in aes),
                enc_acks_confirmed=sum(bin(d["enc_acked"]).count("1") for d in aes),
                observations=records, negative=negative, deadline_context=context, linked_proof=p,
                hold_seconds=HOLD_SECONDS if negative != "none" else None, startup_status=startup,
                register_preservation=True, initial_reset_pc=0, final_pc=pc,
                final_cpu="halted-at-aes-ready" if negative == "none" else "halted-at-aes-fault",
                private_payload_inspected=False, error_is_quiescence=False,
                not_tested_by_this_run=["CPU-only AES pacing", "key management or erasure", "CCM/authentication",
                                       "sleep/wake, RF, flash, interrupts, calibrated time"])


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bus", type=int, required=True)
    parser.add_argument("--address", type=int, required=True)
    parser.add_argument("--board", choices=tuple(BOARDS), required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cycles", type=int, default=1)
    parser.add_argument("--negative", choices=("none", "pre-key", "final"), default="none")
    parser.add_argument("--confirm-aes-test", action="store_true",
                        help="authorize reset, full CODE verification, explicit config26->22, and this AES/DMA fixture")
    args = parser.parse_args(argv)
    try:
        require(args.confirm_aes_test, "Manual acceptance requires --confirm-aes-test")
        require(1 <= args.cycles <= 257 and (args.negative == "none" or args.cycles == 1), "Invalid cycle/negative selection")
        address = UsbAddress(args.bus, args.address)
        image = DebugImage(args.output, args.board, "aes_fixture")
        program = (args.output/"aes_fixture.bin").read_bytes()
        vectors = public_vectors(args.output/"aes-reference")
        validate_program(image, program, vectors, args.cycles, args.negative)
        with Debugger(PyUsbBackend.load(), Access.RESET_DEBUG_SESSION, timeout_ms=10_000,
                      allow_cpu_control=True, allow_target_reset=True, allow_memory_access=True,
                      allow_breakpoints=True, allow_dma_enable=True) as debugger:
            debugger.open(address)
            result = exercise(debugger, image, program, vectors, args.cycles, args.negative)
    except (DebuggerError, ValueError, OSError, KeyError, subprocess.SubprocessError) as error:
        print(f"aes-hardware-check: {error}", file=sys.stderr)
        return 1
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
