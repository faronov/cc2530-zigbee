#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""MANUAL boot-disarmed real-MAC broadcast trial. Never programs or discovers USB."""
import argparse
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import sys
import time

from cc2530_debug import READ_STATUS, Status
from cc_debugger import Access, Debugger, DebuggerError, PyUsbBackend, UsbAddress
from check_radio_tx_hardware import Deadline
from check_timebase_hardware import step_nop
from debug_image import decode_bootstrap
from private_artifacts import private_capture
from verify_firmware import ROOT, parse_ihex, require

sys.path.insert(0, str(ROOT / "tests"))
from banked_image import ihex, pack
import verify_mac_smoke


HASHES = {
    "generic": "15b17d1d452c6e7cb7393f29d0486475b794ce2ccaf2b8e056b03a6d5c7d4127",
    "lg_esl29_rev03": "2a499e591e591b29878d55f977cf6ad91b77e8b95a5c4438f5aac5d506eaa703",
}
BODY = bytes.fromhex("41 88 5A 34 12 FF FF 78 56 4D 41 43 31")
PACKETS = (bytes.fromhex("A9 56 1A E5 36 C9 4D B2"), bytes.fromhex("56 A9 1A E5 C9 36 4D B2"))
INITIAL = bytes.fromhex(
    "4D4143310140010000000000FFFFFF00"
    "00000000000000001A050D5A00010000"
    "00000000000000000000000000000000"
    "00000069960000000000000000000000")
FIELDS = (
    "stage", "consumed", "completed", "outcome", "adapter_result", "mac_result",
    "radio_result", "adapter_phase", "mac_phase", "draws", "actions", "attempts",
    "busy", "sent", "retired", "received", "channel", "power", "length", "dsn")
TAIL_FIELDS = ("transmissions", "held", "ready", "pending", "goal", "normal_rx",
               "released", "radio_phase", "radio_detail_result", "radio_errors",
               "radio_flags0", "radio_flags1")


@dataclass(frozen=True)
class SmokeImage:
    board: str
    physical_hex: bytes
    program: bytes
    populated: int
    status: int
    mailbox: int
    contexts: int
    context_size: int
    wait: int
    end: int
    fault: int
    banker: int
    checkpoint_sp: int


def load_image(output, board):
    require(board in HASHES, "Unknown MAC smoke board")
    artifacts = verify_mac_smoke.load(output, board)
    symbols, _ = verify_mac_smoke.verify(artifacts, board)
    physical = pack(artifacts[0])
    raw = (output / "mac_smoke.physical.hex").read_bytes()
    require(raw == ihex(physical).encode("ascii") and
            hashlib.sha256(raw).hexdigest() == HASHES[board],
            "Physical MAC HEX differs from the exact checked linked image")
    # FF is an explicit installation/preflight policy, not information in sparse HEX.
    program = bytes(physical.get(address, 255) for address in range(max(physical) + 1))
    return SmokeImage(
        board, raw, program, len(physical), symbols["_mac_smoke_status"],
        symbols["_mac_smoke_mailbox"], symbols["_mac_smoke_tx"],
        symbols["_mac_smoke_clock"] + 6 - symbols["_mac_smoke_tx"],
        symbols["_mac_smoke_wait"], symbols["_mac_smoke_end"], symbols["_mac_smoke_fault"],
        symbols["_banked_stop"], symbols["s_SSEG"] + 1)


def validate_image(image, sha256, rf_permission, erased_gaps):
    require(type(image) is SmokeImage and image.board in HASHES, "Exact checked MAC smoke image required")
    require(rf_permission is True and erased_gaps is True,
            "Explicit initial-AUTOACK RF permission and verified erased-gap policy required")
    require(type(image.physical_hex) is bytes and type(sha256) is str and sha256 == HASHES[image.board] ==
            hashlib.sha256(image.physical_hex).hexdigest(),
            "Explicit MAC physical HEX identity differs")
    require(type(image.program) is bytes and len(image.program) == 0x11A51 and
            image.populated == (58109 if image.board == "generic" else 58149) and
            (image.status, image.mailbox, image.contexts, image.context_size) == (0x86B, 0x8AB, 0x8C1, 243) and
            image.banker == 0x66 and image.checkpoint_sp == 0x57,
            "MAC image extent or checked layout differs")
    wait = 0x7854 if image.board == "generic" else 0x787C
    require((image.wait, image.end, image.fault) == (wait, wait + 2, wait + 5) and
            image.program[wait:wait + 8] == b"\0\x22\0\x80\xfd\0\x80\xfd",
            "MAC common checkpoint instructions differ")
    physical = parse_ihex(image.physical_hex.decode("ascii"))
    require(len(physical) == image.populated and max(physical) + 1 == len(image.program) and
            bytes(physical.get(address, 255) for address in range(len(image.program))) == image.program,
            "MAC physical program or erased-gap bytes differ")


def save(capture, value):
    capture.write(json.dumps(value, sort_keys=True) + "\n")
    capture.flush()
    os.fsync(capture.fileno())


def read_region(debugger, address, size):
    return b"".join(debugger.read_xdata(offset, min(256, address + size - offset))
                    for offset in range(address, address + size, 256))


def verify_physical_code(debugger, image, capture):
    deadline = time.monotonic() + 300

    def bounded(method, *args):
        require(time.monotonic() < deadline, "MAC physical preflight deadline exhausted; no resume")
        result = method(*args)
        require(time.monotonic() < deadline, "MAC physical preflight completed late; no resume")
        return result

    adapter = bounded(debugger.read_adapter_state)
    bounded(debugger.attach_reset)
    require(bounded(debugger.read_pc) == 0 and bounded(debugger.read_debug_config) == 0x26 and
            bounded(debugger.read_debug_status) == 0x22,
            "MAC preflight needs genuine reset/halt and unchanged reset configuration")
    before = bounded(debugger.read_registers)
    fmap = bounded(debugger.read_sfr, 0x9F)
    for offset in range(0, len(image.program), 256):
        expected = image.program[offset:offset + 256]
        require(bounded(debugger.read_flash_code, offset, len(expected)) == expected,
                f"MAC physical CODE/erased gap mismatch at {offset:05X}; no resume")
    require(bounded(debugger.read_registers) == before and
            bounded(debugger.read_sfr, 0x9F) == fmap and
            bounded(debugger.read_debug_config) == 0x26 and bounded(debugger.read_debug_status) == 0x22,
            "MAC physical verification changed CPU/FMAP/configuration")
    save(capture, dict(schema=1, event="physical-preflight", bytes=len(image.program),
                       populated=image.populated, gap_policy="verified-FF",
                       adapter_family=adapter.target_id, full_context_preserved=True))


def wait_checkpoint(debugger, allowed):
    with debugger._target_operation() as deadline:
        while True:
            status = debugger._exchange_byte(READ_STATUS, deadline)
            debugger._check_status(status, active=True)
            if status & Status.CPU_HALTED:
                require(status & Status.HALT_STATUS, "MAC CPU halt was not a breakpoint")
                pc = debugger._pc(deadline)
                require(pc in allowed, f"Unexpected MAC breakpoint {pc:04X}; no recovery")
                return pc


def decode(raw):
    require(type(raw) is bytes and len(raw) == 64 and raw[:6] == INITIAL[:6] and
            raw[24:28] == INITIAL[24:28] and raw[51:53] == INITIAL[51:53] and
            raw[54:] == bytes(10) and 1 <= raw[6] <= 6 and raw[7] <= 9,
            "MAC status signature/version/size/guards/range mismatch")
    value = dict(zip(FIELDS, raw[8:28]))
    value.update(zip(TAIL_FIELDS, raw[39:51]))
    value.update(phase=raw[6], reason=raw[7], mac_outcome=raw[53])
    for name, start, end in (("remaining", 28, 30), ("steps", 30, 32),
                             ("elapsed", 32, 35), ("live", 35, 39)):
        value[name] = int.from_bytes(raw[start:end], "little")
    return value


def inspect(debugger, image, pc, capture, admission=None):
    registers = debugger.read_registers()
    raw = debugger.read_xdata(image.status, 64)
    mailbox = debugger.read_xdata(image.mailbox, 8)
    boot = debugger.read_xdata(0x1E00, 32)
    banker = debugger.read_xdata(0x1F1E, 2)
    prefix = read_region(debugger, 0, image.status)
    contexts = read_region(debugger, image.contexts, image.context_size)
    require(debugger.read_registers() == registers, "MAC inspection changed the complete CPU context")
    save(capture, dict(schema=1, event="checkpoint", pc=pc, status_hex=raw.hex(),
                       mailbox_hex=mailbox.hex(), boot_hex=boot.hex(), banker_hex=banker.hex(),
                       prefix_hex=prefix.hex(), contexts_hex=contexts.hex(),
                       sp=registers.sp, dps=registers.dps, psw=registers.psw, bank=registers.bank))
    if pc == image.banker:
        raise ValueError(f"MAC banker fail-stop, error={banker[1]}; RF may remain active; no recovery")
    record = decode(raw)
    require(registers.pc == pc and registers.sp == image.checkpoint_sp and
            registers.dps == 0 and not registers.psw & 0x18 and registers.bank == 1 and
            banker == bytes(2), "MAC checkpoint CPU/banker invariant differs")
    decode_bootstrap(boot, image.board)
    require(mailbox == bytes(8) and boot[8] == 0, "MAC mailbox/immutable M0 heartbeat differs")
    if pc == image.fault or record["phase"] == 6:
        raise ValueError(f"MAC FAULT reason={record['reason']} stage={record['stage']}; "
                         "RF may remain active; no retry/reset/resume/flush attempted")
    if admission is not None:
        phase, remaining = admission
        expected = bytearray(INITIAL)
        expected[6], expected[28:30] = phase, remaining.to_bytes(2, "little")
        require(pc == image.wait and raw == bytes(expected) and prefix == bytes(len(prefix)) and
                contexts == bytes(len(contexts)), "MAC admission/private state differs; no RF continuation")
    return record, boot, registers


def check_end(record):
    fixed = dict(phase=5, reason=0, stage=6, consumed=1, completed=1, adapter_result=0,
                 mac_result=0, radio_result=0, adapter_phase=2, mac_phase=0, remaining=0,
                 held=0, ready=0, pending=0, goal=0, normal_rx=0, released=1,
                 radio_detail_result=5, radio_errors=0)
    require(all(record[name] == value for name, value in fixed.items()) and
            record["radio_phase"] in (3, 7) and 0 < record["steps"] <= 4096,
            "MAC END lacks actual owner retirement/public slot release")
    if record["outcome"] == 1:
        require(record["mac_outcome"] == 2 and record["transmissions"] == record["sent"] ==
                record["retired"] == 1 and 1 <= record["attempts"] <= 5 and
                record["draws"] == record["attempts"] and record["busy"] == record["attempts"] - 1 and
                record["actions"] == 2 * record["attempts"] + 1, "MAC SENT accounting differs")
        return "LOCAL_SENT"
    require(record["outcome"] == 2 and record["mac_outcome"] == 3 and
            record["transmissions"] == record["sent"] == record["retired"] == 0 and
            record["draws"] == record["attempts"] == record["busy"] == 5 and record["actions"] == 10,
            "MAC CCA_BUSY accounting differs")
    return "CCA_BUSY"


def exercise(debugger, image, sha256, capture, rf_permission=False, erased_gaps=False,
             admit_only=False, before_rf=None):
    validate_image(image, sha256, rf_permission, erased_gaps)
    require(type(admit_only) is bool and (before_rf is None or callable(before_rf)), "Invalid MAC run mode")
    save(capture, dict(schema=1, scope="one-ordinary-MAC-broadcast", board=image.board,
                       physical_hex_sha256=sha256, initial_autoack_authorized=True, admit_only=admit_only))
    verify_physical_code(debugger, image, capture)
    debugger = Deadline(debugger, "MAC admission/experiment")
    stops = (image.wait, image.end, image.fault, image.banker)
    for slot, address in enumerate(stops):
        debugger.set_breakpoint(slot, address)
    debugger.resume()
    pc = wait_checkpoint(debugger, stops)
    record, original, registers = inspect(debugger, image, pc, capture, (1, 256))

    def advance(phase, remaining):
        nonlocal record, registers
        step_nop(debugger, registers, "MAC admission")
        debugger.resume()
        pc = wait_checkpoint(debugger, stops)
        record, boot, registers = inspect(debugger, image, pc, capture, (phase, remaining))
        require(boot == original, "MAC admission changed M0")

    advance(1, 255)
    for data, phase, remaining in ((PACKETS[0], 2, 256), (PACKETS[1], 3, 0)):
        debugger.write_xdata(image.mailbox, data)
        require(debugger.read_xdata(image.mailbox, 8) == data and debugger.read_registers() == registers,
                "MAC mailbox write/readback/context failed")
        advance(phase, remaining)
    outcome = "ADMITTED"
    if not admit_only:
        if before_rf is not None:
            before_rf()
            require(debugger.read_registers() == registers, "CPU changed while preparing independent capture")
        debugger.set_breakpoint(0, image.wait, False)
        step_nop(debugger, registers, "MAC RF continuation")
        debugger.resume()
        pc = wait_checkpoint(debugger, (image.end, image.fault, image.banker))
        record, boot, registers = inspect(debugger, image, pc, capture)
        require(boot == original, "MAC run changed immutable M0")
        outcome = check_end(record)
    return dict(evidence="hardware-observed", scope="one-ordinary-MAC-broadcast",
                board=image.board, physical_hex_sha256=sha256, outcome=outcome,
                final_pc=registers.pc, final_cpu="halted-ADMITTED" if admit_only else "halted-END",
                profile_channel=26, profile_txpower_raw=5, synthetic_body_hex=BODY.hex(),
                ordinary_transmissions=record["transmissions"], cca_attempts=record["attempts"],
                busy_observations=record["busy"], initial_autoack_possible=not admit_only,
                physical_extent_verified=len(image.program), independent_reception_proved=False,
                not_tested=["ACK/retry/delivery", "captured PHY timing", "calibrated CCA/power",
                            "Zigbee join/security", "physical fault containment/recovery"])


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bus", type=int, required=True)
    parser.add_argument("--address", type=int, required=True)
    parser.add_argument("--board", choices=tuple(HASHES), required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--sha256", required=True, help="Exact sparse physical HEX SHA256, never virtual IHX")
    parser.add_argument("--capture", type=Path, required=True)
    flags = ("allow-target-reset", "allow-cpu-control", "allow-memory-access",
             "allow-memory-write", "allow-breakpoints", "confirm-rf-including-autoack",
             "confirm-erased-gaps")
    for name in (*flags, "admit-only"):
        parser.add_argument("--" + name, action="store_true")
    args = parser.parse_args(argv)
    try:
        require(all(getattr(args, flag.replace("-", "_")) for flag in flags),
                "Separate reset/CPU/read/write/breakpoint, RF/AUTOACK and erased-gap permissions required")
        address = UsbAddress(args.bus, args.address)
        image = load_image(args.output, args.board)
        validate_image(image, args.sha256, args.confirm_rf_including_autoack, args.confirm_erased_gaps)
        with private_capture(args.capture) as capture:
            with Debugger(PyUsbBackend.load(), Access.RESET_DEBUG_SESSION, timeout_ms=10_000,
                          allow_target_reset=True, allow_cpu_control=True, allow_memory_access=True,
                          allow_memory_write=True, allow_breakpoints=True) as debugger:
                debugger.open(address)
                result = exercise(debugger, image, args.sha256, capture, True, True, args.admit_only)
    except (DebuggerError, ValueError, OSError, KeyError) as error:
        print(f"mac-smoke-check: {error}", file=sys.stderr)
        return 1
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
