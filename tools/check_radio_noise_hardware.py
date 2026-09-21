#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""MANUAL one raw-IRND capture on channel26. Never flashes or qualifies entropy."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import sys

from cc_debugger import Access, Debugger, DebuggerError, PyUsbBackend, UsbAddress
from cc2530_debug import READ_CONFIG
from check_radio_tx_hardware import Deadline
from check_timebase_hardware import reset_and_verify_code, step_nop, wait_checkpoint
from debug_image import DebugImage, decode_bootstrap
from private_artifacts import private_capture
from radio_noise_fixture import ARM, RUN, SIZE, CAPTURE_SIZE, HEALTH_SIZE, CHECKPOINTS, decode, decode_capture
from verify_firmware import BOARDS, CODE_LIMIT, require

GPIO = (0xfd, 0xfe, 0xff, 0xf3, 0xf4, 0xf5, 0x8f, 0xf6, 0xf7, 0xf2, 0xf1)
FLAGS = (0xa9, 0xb9, 0x88, 0x98, 0x9b, 0xe8, 0xc0)
LIVE = GPIO + FLAGS + (0xa8, 0xb8, 0x9a, 0xd6, 0xd7, 0xd1, 0xc6, 0x9e, 0xbe, 0x80, 0x90)


def read_live(debugger):
    """Fixed read-only fixture whitelist; no FIFO, RFRND, ST0 or general MMIO API."""
    with debugger._stopped_operation(memory_access=True) as deadline:
        require(debugger._exchange_byte(READ_CONFIG, deadline) == 0x26, "IRND debug configuration changed")
        with debugger._preserve_registers(deadline):
            return bytes(debugger._instruction(bytes((0xe5, address)), deadline) for address in LIVE)


def check_live(raw, board, command, previous=None):
    require(type(raw) is bytes and len(raw) == len(LIVE), "IRND live observation shape changed")
    values = dict(zip(LIVE, raw))
    require(all(values[a] == 0 for a in (0xa8, 0xb8, 0x9a, 0xd6, 0xd7, 0xd1)) and
            values[0xc6] == values[0x9e] == command and values[0xbe] & 7 == 4,
            "IRND live IRQ/DMA/clock/sleep ownership changed")
    if board == "lg_esl29_rev03":
        require(values[0xfd] & 0xbc == 0xbc and values[0xfe] & 2 == 2 and
                values[0x8f] & 0xbc == 0xbc and not values[0xf2] & 0xbc and
                not values[0xf3] & 0xbc and not values[0xf4] & 2 and
                not values[0x80] & 0xbc and not values[0x90] & 2,
                "IRND LG display-off GPIO configuration/output pins differ")
    if previous is not None:
        old = dict(zip(LIVE, previous))
        require(all(values[a] == old[a] for a in GPIO + FLAGS[:-1] + (0xbe,)) and
                values[0xc0] in (old[0xc0], old[0xc0] | 0x80),
                "IRND GPIO/unrelated flags changed or STIF regressed")


def validate_program(image, program, sha256, permission):
    require(permission is True, "Explicit one-capture receiver permission is required")
    require(image.image_name == "radio_noise_fixture" and image.board in BOARDS,
            "Requires the genuine board radio_noise_fixture, never the synthetic test")
    require(type(program) is bytes and 0 < len(program) <= CODE_LIMIT and
            len(program) == image.metrics["image_extent_bytes"] and
            type(sha256) is str and sha256 == image.sha256 == hashlib.sha256(program).hexdigest(),
            "Explicit/checked/program IRND CODE identity differs")
    proof = image.radio_noise_proof
    pcs = [image.symbol(name).address for name in CHECKPOINTS]
    require(pcs == proof["checkpoints"] and len(set(pcs)) == 3, "IRND checkpoint bindings differ")
    for pc, expected in zip(pcs, (b"\0\x22", b"\0\x80\xfd", b"\0\x80\xfd")):
        require(program[pc:pc + len(expected)] == expected, "IRND checkpoint instructions differ")
    require(type(ARM) is bytes and type(RUN) is bytes and 4 <= len(ARM) == len(RUN) <= 16 and ARM != RUN,
            "IRND admission packet ABI differs")
    ranges = [(proof[name], proof[name] + size) for name, size in
              (("state", SIZE), ("command", len(ARM)), ("capture", CAPTURE_SIZE),
               ("health", HEALTH_SIZE), ("clock", 19))]
    require(all(0 <= lo < hi <= 0x1e00 for lo, hi in ranges) and
            all(a >= d or c >= b for i, (a, b) in enumerate(ranges) for c, d in ranges[i + 1:]),
            "IRND inspected objects overlap or escape ordinary SRAM")


def inspect(debugger, image, pc, capture, stage):
    registers = debugger.read_registers()
    require(registers.pc == pc and registers.dps == 0 and not registers.psw & 0x18 and
            registers.sp == image.metrics["iram_stack_start"] + 1, "IRND checkpoint PC/bank/DPS/SP mismatch")
    proof = image.radio_noise_proof
    raw = {name: debugger.read_xdata(proof[name], size) for name, size in
           (("state", SIZE), ("command", len(ARM)), ("capture", CAPTURE_SIZE),
            ("health", HEALTH_SIZE), ("clock", 19))}
    raw["boot"] = debugger.read_xdata(0x1e00, 32)
    raw["live"] = read_live(debugger)
    # Preserve even malformed/failed captures before interpreting or continuing.
    capture.write(json.dumps({"schema": 1, "stage": stage, "pc": pc,
                              **{name + "_hex": data.hex() for name, data in raw.items()}}) + "\n")
    capture.flush()
    os.fsync(capture.fileno())
    require(debugger.read_registers() == registers, "IRND inspection changed full CPU context")
    record = decode(raw["state"])
    acquisition = decode_capture(raw["capture"])
    decode_bootstrap(raw["boot"], image.board)
    require(raw["command"] == bytes(len(ARM)), "IRND command was not consumed")
    return record, acquisition, raw, registers


def diagnostic_health(data):
    """Independent whole-prefix recount, for this fixed 1024-bit diagnostic only."""
    require(type(data) is bytes and len(data) == 128, "IRND health check needs exactly1024 raw bits")
    bits = [(data[i >> 3] >> (i & 7)) & 1 for i in range(1024)]
    for count in range(1, 1025):
        prefix = bits[:count]
        run = 1
        for value in reversed(prefix[:-1]):
            if value != prefix[-1]:
                break
            run += 1
        matches = prefix.count(prefix[0])
        result = 3 if run >= 21 else 4 if matches >= 589 else 0
        if result or count == 1024:
            fields = (21, 589, run, matches if result else 0, count if result else 0, 1024 - count)
            context = b"".join(n.to_bytes(2, "little") for n in fields)
            context += bytes((prefix[-1], prefix[0], result or 2))
            return result, count if result else 0, context
    raise AssertionError("Unreachable fixed diagnostic window")


def exercise(debugger, image, program, sha256, capture, permission=False, admit_only=False):
    validate_program(image, program, sha256, permission)
    require(type(admit_only) is bool, "Invalid IRND admission-only choice")
    debugger = Deadline(debugger, "IRND")
    adapter, config = reset_and_verify_code(debugger, program, "boot-disarmed IRND")
    require(adapter.target_id == 0x2530, "IRND fixture requires CC2530")
    wait, end, fault = image.radio_noise_proof["checkpoints"]
    for slot in range(4):
        debugger.set_breakpoint(slot, wait, False)
    for slot, pc in enumerate((wait, end, fault)):
        debugger.set_breakpoint(slot, pc)
    debugger.resume()
    pc = wait_checkpoint(debugger, wait, fault, "IRND DISARMED")
    record, acquisition, raw, registers = inspect(debugger, image, pc, capture, "boot")
    require(pc == wait and record["phase"] == "DISARMED" and record["attempts"] == 0 and
            record["result"] == 255 and raw["boot"][8] == 0, "IRND did not boot DISARMED")
    original = raw["boot"]
    check_live(raw["live"], image.board, 0xc9)
    previous_live = raw["live"]

    def advance(stage, target, phase):
        nonlocal record, acquisition, raw, registers, previous_live
        step_nop(debugger, registers, "IRND")
        debugger.resume()
        pc = wait_checkpoint(debugger, target, fault, "bounded IRND " + stage)
        record, acquisition, raw, registers = inspect(debugger, image, pc, capture, stage)
        require(raw["boot"][:8] + raw["boot"][9:] == original[:8] + original[9:],
                "IRND immutable bootstrap changed")
        require(pc == target and record["phase"] == phase,
                f"IRND FAULT at {stage}; raw evidence retained, RX may remain active; no recovery attempted")
        check_live(raw["live"], image.board, 0xc9 if stage == "empty" else 0x88, previous_live)
        previous_live = raw["live"]

    before = raw
    advance("empty", wait, "DISARMED")
    require(record["attempts"] == 0 and record["result"] == 255 and raw["boot"] == original and
            all(raw[n] == before[n] for n in ("capture", "health", "clock")),
            "IRND empty command touched service state")
    admitted_clock = None
    for stage, packet, target, phase in (("arm", ARM, wait, "ADMITTED"), ("run", RUN, end, "END")):
        if stage == "run" and admit_only:
            break
        debugger.write_xdata(image.radio_noise_proof["command"], packet)
        require(debugger.read_xdata(image.radio_noise_proof["command"], len(packet)) == packet and
                debugger.read_registers() == registers, "IRND packet write/readback/context mismatch")
        advance(stage, target, phase)
        if stage == "arm":
            require(record["attempts"] == 0 and record["result"] == 255 and raw["boot"] == original and
                    raw["capture"] == before["capture"] and raw["health"] == before["health"],
                    "IRND ARM already acquired samples")
            require(raw["clock"][14:19] == b"\xc9\x88\x88\x88\x08" and
                    raw["clock"][6] == 0 and int.from_bytes(raw["clock"][4:6], "little") > 0,
                    "IRND ARM clock diagnostics do not establish XOSC32")
            admitted_clock = raw["clock"]
    if not admit_only:
        require(record["attempts"] == 1 and record["result"] == 0 and raw["boot"][8] == 1 and
                acquisition["samples"] == acquisition["timed_samples"] == 1024 and
                acquisition["phase"] == 5 and acquisition["actions"] == 3 and
                acquisition["rx_enable"] == 0 and acquisition["errors"] == 0 and
                0 < acquisition["elapsed_ticks"] < 100000 and
                0 < acquisition["polls"] <= 10000 and
                1 <= acquisition["min_gap"] <= acquisition["max_gap"] <= acquisition["elapsed_ticks"] and
                0 <= acquisition["max_span"] <= acquisition["elapsed_ticks"] and
                0 <= acquisition["first_before"] <= 0xffffff and
                0 <= acquisition["last_after"] <= 0xffffff and
                ((acquisition["last_after"] - acquisition["first_before"]) & 0xffffff) <=
                acquisition["elapsed_ticks"] and
                acquisition["last_raw"] & 0xfc == 0 and
                acquisition["last_raw"] & 1 == raw["capture"][-1] >> 7,
                "IRND completed acquisition/confirmed stop metadata differs")
        require(raw["clock"] == admitted_clock and raw["health"][:4] == b"\x15\0\x4d\x02",
                "IRND clock/diagnostic health cutoffs changed")
        expected_health = diagnostic_health(raw["capture"][-128:])
        require((record["health_result"], record["first_failure"], raw["health"]) == expected_health,
                "IRND complete health result differs from independent raw-prefix recount")
    return {
        "evidence": "hardware-observed", "scope": "one-raw-irnd-diagnostic",
        "board": image.board, "image_sha256": sha256, "verified_code_bytes": len(program),
        "debug_config": config, "profile_channel": 26, "attempts": record["attempts"],
        "final_cpu": "halted-ADMITTED" if admit_only else "halted-END", "final_pc": registers.pc,
        "result": "ADMITTED" if admit_only else "RAW_CAPTURE_STOPPED",
        "samples": acquisition["samples"], "timed_samples": acquisition["timed_samples"],
        "elapsed_ticks": acquisition["elapsed_ticks"],
        "min_gap": acquisition["min_gap"], "max_gap": acquisition["max_gap"], "max_span": acquisition["max_span"],
        "health_result": record["health_result"], "first_failure": record["first_failure"],
        "entropy_qualified": False, "register_preservation": True,
        "not_tested": ["source independence/min-entropy", "calibrated cadence/environment/restarts",
                      "continuous source/normal-radio handoff", "conditioner/DRBG/security RNG",
                      "physical fault containment/recovery"],
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bus", type=int, required=True)
    parser.add_argument("--address", type=int, required=True)
    parser.add_argument("--board", choices=tuple(BOARDS), required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--capture", type=Path, required=True)
    parser.add_argument("--sha256", required=True)
    for name in ("allow-target-reset", "allow-cpu-control", "allow-memory-access",
                 "allow-memory-write", "allow-breakpoints", "confirm-one-irnd-capture", "admit-only"):
        parser.add_argument("--" + name, action="store_true")
    args = parser.parse_args(argv)
    try:
        require(all((args.allow_target_reset, args.allow_cpu_control, args.allow_memory_access,
                     args.allow_memory_write, args.allow_breakpoints, args.confirm_one_irnd_capture)),
                "Separate reset/CPU/read/write/breakpoint permissions and receiver opt-in are required")
        address = UsbAddress(args.bus, args.address)
        image = DebugImage(args.output, args.board, "radio_noise_fixture")
        program = (args.output / "radio_noise_fixture.bin").read_bytes()
        validate_program(image, program, args.sha256, args.confirm_one_irnd_capture)
        with private_capture(args.capture) as capture:
            with Debugger(PyUsbBackend.load(), Access.RESET_DEBUG_SESSION, timeout_ms=10_000,
                          allow_cpu_control=True, allow_target_reset=True, allow_memory_access=True,
                          allow_memory_write=True, allow_breakpoints=True) as debugger:
                debugger.open(address)
                result = exercise(debugger, image, program, args.sha256, capture, True, args.admit_only)
    except (DebuggerError, ValueError, OSError, KeyError) as error:
        print(f"irnd-check: {error}", file=sys.stderr)
        return 1
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
