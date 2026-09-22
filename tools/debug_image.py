#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Offline SDCC image/symbol/source/status inspection. Never imports or accesses USB."""

import argparse
from dataclasses import asdict, dataclass
import hashlib
import json
from pathlib import Path
import re
import sys
from typing import Optional

from cc2530_debug import breakpoint_parameters, decode_config, decode_status, unsigned
from verify_firmware import (
    ARTIFACT_EXTENSIONS, BOARDS, CODE_LIMIT, IMAGES, IRAM_ALIAS,
    parse_ihex, parse_symbols, require, verify_artifacts,
    TIMEBASE_FIXTURE_SIZE, TIMEBASE_DELAY, TIMEBASE_POLL_LIMIT,
    CLOCK_FIXTURE_SIZE, CLOCK_TIMEOUT, CLOCK_POLL_LIMIT, verify_clock_fixture_code,
)
from irq_fixture import decode_irq_fixture, verify_irq_fixture


@dataclass(frozen=True)
class Symbol:
    name: str
    address: int
    space: str
    kind: str
    size: Optional[int]


@dataclass(frozen=True)
class SourceLocation:
    file: str
    line: int
    scope: str
    block: int
    address: int


def linked_source_locations(debug_text: str, image: dict) -> tuple[SourceLocation, ...]:
    locations = {}
    for record in debug_text.splitlines():
        if not record.startswith("L:C"):
            continue
        match = re.fullmatch(r"L:C\$([^$]+)\$(\d+)\$(\d+_\d+)\$(\d+):([0-9A-Fa-f]+)", record)
        require(match is not None, "Malformed/unsupported CDB source-location record")
        file, line, scope, block, address = match.groups()
        location = SourceLocation(file, int(line), scope, int(block), int(address, 16))
        require(file.isprintable() and location.line > 0, "Invalid CDB source file/line")
        require(0 <= location.address < CODE_LIMIT and location.address in image,
                "CDB source location is outside the linked image")
        key = (location.file, location.line, location.scope, location.block)
        require(key not in locations or locations[key] == location, "Conflicting CDB source locations")
        locations[key] = location
    return tuple(sorted(locations.values(), key=lambda item: (
        item.address, item.file, item.line, item.scope, item.block)))


def linked_symbols(map_text: str, debug_text: str, image: dict) -> dict:
    addresses = parse_symbols(map_text)
    symbols = {}
    map_spaces = {}
    for match in re.finditer(r"^\s*([CD]):\s*[0-9A-Fa-f]{8}\s+(_\w+)(?:\s+\S+)?\s*$",
                             map_text, re.MULTILINE):
        name, space = match[2], {"C": "CODE", "D": "XDATA"}[match[1]]
        require(name not in map_spaces or map_spaces[name] == space, f"Conflicting map spaces for {name}")
        map_spaces[name] = space
        if space == "CODE":
            symbols[name] = Symbol(name, addresses[name], space, "label", None)
    declarations = {}
    for match in re.finditer(
            r"^[SF]:G\$([^$\n]+)\$[^(\n]+\(\{(\d+)\}([^)\n]+)\),([A-Z]),", debug_text, re.MULTILINE):
        name, size, data_type, space = "_" + match[1], int(match[2]), match[3], match[4]
        if name not in addresses:
            continue
        if data_type.startswith("DF,"):
            symbol = Symbol(name, addresses[name], "CODE", "function", None)
        elif space in ("D", "F", "I"):
            symbol = Symbol(name, addresses[name], {"D": "CODE", "F": "XDATA", "I": "SFR"}[space],
                            "object", size)
        elif space == "J" and data_type == "SX:U":
            symbol = Symbol(name, addresses[name], "SBIT", "object", size)
        else:
            raise ValueError(f"Unsupported CDB address space for linked global {name}")
        require(name not in declarations or declarations[name] == symbol,
                f"Conflicting CDB declarations for {name}")
        if name in map_spaces:
            require(symbol.space == map_spaces[name], f"Map/CDB space mismatch for {name}")
        declarations[name] = symbol
        symbols[name] = symbol
    for record in debug_text.splitlines():
        if not record.startswith("L:G$"):
            continue
        match = re.fullmatch(r"L:G\$([^$]+)\$[^$]+\$[^:]+:([0-9A-Fa-f]+)", record)
        require(match is not None, "Malformed CDB global address record")
        name = "_" + match[1]
        if name in symbols:
            require(symbols[name].address == int(match[2], 16), f"Map/CDB address mismatch for {name}")
    for symbol in symbols.values():
        require(symbol.size is None or symbol.size > 0, f"Invalid object size for {symbol.name}")
        if symbol.space == "CODE":
            require(0 <= symbol.address < CODE_LIMIT
                    and all(address in image for address in range(symbol.address,
                            symbol.address + (symbol.size if symbol.size is not None else 1))),
                    f"CODE symbol {symbol.name} is outside the linked image")
        elif symbol.space == "XDATA":
            require(symbol.size is not None and symbol.size > 0
                    and 0 <= symbol.address < symbol.address + symbol.size <= IRAM_ALIAS,
                    f"XDATA symbol {symbol.name} reaches the IRAM alias")
        else:
            require(symbol.size == 1 and 0x80 <= symbol.address <= 0xFF,
                    f"Invalid SFR symbol {symbol.name}")
    return symbols


class DebugImage:
    def __init__(self, output: Path, board: str, image_name: str = "bringup"):
        self.metrics, _ = verify_artifacts(output, board, image_name)
        info = json.loads((output / "build-info.json").read_text(encoding="utf-8"))
        require(isinstance(info, dict) and type(info.get("schema")) is int and info["schema"] == 1,
                "Unknown build metadata schema")
        require(info.get("board") == board and info.get("image") == image_name,
                "Build metadata board/image mismatch")
        require(isinstance(info.get("compiler"), str) and re.search(r"\b4\.2\.0\b", info["compiler"]),
                "Symbol reader requires the SDCC 4.2.0 build metadata")
        hashes = info.get("sha256")
        require(isinstance(hashes, dict)
                and set(hashes) == {f"{image_name}.{extension}" for extension in ARTIFACT_EXTENSIONS},
                "Missing/unexpected artifact hashes")
        for name, expected in hashes.items():
            require(hashlib.sha256((output / name).read_bytes()).hexdigest() == expected,
                    f"Build metadata hash mismatch for {name}")
        self.board = board
        self.image_name = image_name
        self.sha256 = hashes[f"{image_name}.bin"]
        image = parse_ihex((output / f"{image_name}.ihx").read_text(encoding="ascii"))
        debug_text = (output / f"{image_name}.cdb").read_bytes().decode("utf-8")
        self.symbols = linked_symbols((output / f"{image_name}.map").read_text(encoding="utf-8"),
                                      debug_text, image)
        self.source_locations = linked_source_locations(debug_text, image)
        if image_name == "clock_fixture":
            self.clock_timeout_checkpoint = verify_clock_fixture_code(
                image, parse_symbols((output / f"{image_name}.map").read_text(encoding="utf-8")), debug_text)
        if image_name == "irq_fixture":
            self.irq_proof = verify_irq_fixture(
                image, parse_symbols((output / f"{image_name}.map").read_text(encoding="utf-8")), debug_text)
        if image_name == "radio_fifo_fixture":
            from radio_fifo_fixture import verify_fixture
            self.radio_fifo_proof = verify_fixture(
                image, parse_symbols((output / f"{image_name}.map").read_text(encoding="utf-8")), debug_text)
        if image_name == "dma_fixture":
            from dma_fixture import verify_fixture
            self.dma_proof = verify_fixture(
                image, parse_symbols((output / f"{image_name}.map").read_text(encoding="utf-8")), debug_text)
        if image_name == "aes_fixture":
            from aes_fixture import verify_fixture
            self.aes_proof = verify_fixture(
                image, parse_symbols((output / f"{image_name}.map").read_text(encoding="utf-8")), debug_text)
        if image_name == "prng_fixture":
            from prng_fixture import verify_fixture
            self.prng_proof = verify_fixture(
                image, parse_symbols((output / f"{image_name}.map").read_text(encoding="utf-8")), debug_text)
        if image_name == "radio_rx_fixture":
            from radio_rx_fixture import verify_fixture
            self.radio_rx_proof = verify_fixture(
                image, parse_symbols((output / f"{image_name}.map").read_text(encoding="utf-8")), debug_text)
        if image_name == "radio_tx_fixture":
            from radio_tx_fixture import verify_fixture
            self.radio_tx_proof = verify_fixture(
                image, parse_symbols((output / f"{image_name}.map").read_text(encoding="utf-8")), debug_text, board)
        if image_name == "radio_noise_fixture":
            from radio_noise_fixture import verify_fixture
            self.radio_noise_proof = verify_fixture(
                image, parse_symbols((output / f"{image_name}.map").read_text(encoding="utf-8")), debug_text, board)
        if image_name == "radio_link_fixture":
            from radio_link_fixture import verify_fixture
            self.radio_link_proof = verify_fixture(
                image, parse_symbols((output / f"{image_name}.map").read_text(encoding="utf-8")), debug_text, board)

    def symbol(self, name: str) -> Symbol:
        require(name in self.symbols, f"No supported linked global/label named {name}")
        return self.symbols[name]

    def breakpoint(self, name: str, slot: int) -> dict:
        symbol = self.symbol(name)
        require(symbol.space == "CODE" and symbol.kind in ("function", "label"),
                "Breakpoints require a CODE function or label, not a data object")
        parameters = breakpoint_parameters(slot, symbol.address)
        return {"symbol": name, "slot": slot, "bank": 0, "address": symbol.address,
                "target_parameters_hex": parameters.hex(), "programmed": False}

    def source_lines(self, *, pc: Optional[int] = None, file: Optional[str] = None,
                     line: Optional[int] = None) -> tuple[SourceLocation, ...]:
        require(pc is None or (file is None and line is None),
                "Choose either a PC or an exact source file/line")
        if pc is not None:
            unsigned(pc, CODE_LIMIT - 1, "unbanked PC")
            locations = tuple(item for item in self.source_locations if item.address == pc)
        elif file is not None or line is not None:
            require(isinstance(file, str) and file and type(line) is int and line > 0,
                    "Source lookup requires both an exact file name and a positive line number")
            locations = tuple(item for item in self.source_locations if item.file == file and item.line == line)
        else:
            locations = self.source_locations
        require(locations, "No exact CDB source location; nearest-line inference is not supported")
        return locations


def expected_fixture(cycle: int) -> bytes:
    require(type(cycle) is int and cycle >= 0, "Fixture cycle must be a nonnegative integer")
    seed = (cycle & 255) ^ 0x5A
    first = (seed + 0x11) & 255
    second = first ^ 0xA5
    third = ((second * 2) + (second // 128)) & 255
    fourth = (third + 0x37) & 255
    result = (((fourth ^ 0x3C) + 3) & 255) ^ seed
    return b"M1DB" + bytes([1, 16, 3, (cycle + 1) & 255, seed, result,
                           first, second, third, fourth, 0x69, 0x96])


def decode_bootstrap(data: bytes, board: str) -> dict:
    require(board in BOARDS, "Unknown board")
    require(isinstance(data, bytes) and len(data) == 32, "M0 snapshot must contain exactly 32 bytes")
    require(data[:6] == b"M0CC\x01\x20", "Wrong M0 signature/version/size")
    require(data[6] == 2, "M0 snapshot is not ready")
    require(data[7] == BOARDS[board] and data[9] == BOARDS[board], "M0 board/policy mismatch")
    require(data[26:29] == b"\0" * 3, "M0 snapshot has interrupts enabled")
    require(data[29:] == b"\0" * 3, "M0 reserved bytes must be zero")
    result = {"signature": "M0CC", "abi_version": 1, "byte_size": 32, "phase": 2,
              "board": board, "heartbeat": data[8], "policy": data[9]}
    for name, start in (("ports", 10), ("directions", 13), ("selections", 16), ("pulls", 19)):
        result[name] = list(data[start:start + 3])
    result.update(analog=data[22], routing=data[23], clock_request=data[24], clock_status=data[25],
                  interrupt_enables=list(data[26:29]))
    return result


def decode_fixture(data: bytes) -> dict:
    require(isinstance(data, bytes) and len(data) == 16, "M1 snapshot must contain exactly 16 bytes")
    require(data[:6] == b"M1DB\x01\x10", "Wrong M1 signature/version/size")
    require(data[14:] == b"\x69\x96", "M1 fixture guards changed")
    if data[6] == 1:
        require(data[7:14] == b"\0" * 7, "M1 initialized snapshot contains nonzero cycle data")
    elif data[6] == 3:
        require(data == expected_fixture((data[7] - 1) & 255), "M1 cycle/checkpoint mismatch")
    else:
        raise ValueError("M1 snapshot is in progress or has an unknown phase; use a known stop boundary")
    return {"signature": "M1DB", "abi_version": 1, "byte_size": 16, "phase": data[6],
            "iteration": data[7], "seed": data[8], "result": data[9],
            "checkpoints": list(data[10:14]), "guards": list(data[14:])}


def decode_timebase_fixture(data: bytes) -> dict:
    require(isinstance(data, bytes) and len(data) == TIMEBASE_FIXTURE_SIZE,
            "Timebase snapshot must contain exactly 32 bytes")
    require(data[:6] == b"M2TM\x01\x20", "Wrong timebase signature/version/size")
    require(data[28:] == b"\0\0\x69\x96", "Timebase reserved bytes/guards changed")
    phase, reason, cycles, helper = data[6:10]
    result = {"signature": "M2TM", "abi_version": 1, "byte_size": 32,
              "phase": phase, "reason": reason, "completed_cycles": cycles, "helper_status": helper}
    for name, offset, size in (("start", 10, 3), ("end", 13, 3), ("deadline", 16, 3),
                               ("elapsed", 19, 3), ("polls", 22, 2), ("delay", 24, 2),
                               ("poll_limit", 26, 2)):
        result[name] = int.from_bytes(data[offset:offset + size], "little")
    require(result["delay"] == TIMEBASE_DELAY and result["poll_limit"] == TIMEBASE_POLL_LIMIT,
            "Timebase fixed delay/poll budget mismatch")
    require(helper in (0, 1, 2) and 0 <= reason <= 5, "Unknown timebase helper status/reason")
    require(result["polls"] <= TIMEBASE_POLL_LIMIT, "Timebase poll count exceeds budget")
    if phase == 1:
        require(data[7:24] == b"\0" * 17, "Initialized timebase record contains cycle data")
    elif phase in (3, 4):
        if phase == 3:
            require(reason == helper == 0 and 1 <= result["polls"] <= TIMEBASE_POLL_LIMIT,
                    "READY timebase record has failed status/poll count")
            require(TIMEBASE_DELAY <= result["elapsed"] < 0x800000, "READY elapsed is outside bounded window")
        else:
            require(reason != 0 and (helper != 0 if reason in (1, 2) else helper == 0),
                    "FAULT timebase reason/helper mismatch")
            if reason == 1:
                require(result["polls"] == 0 and data[13:24] == b"\0" * 11,
                        "Deadline fault contains poll results")
            if reason in (2, 3, 4):
                require(result["polls"] >= 1, "Timebase poll fault lacks a sample")
            if reason == 4:
                require(result["polls"] == TIMEBASE_POLL_LIMIT and result["elapsed"] < TIMEBASE_DELAY,
                        "Poll-limit fault has inconsistent count/elapsed")
        if result["polls"] > 0:
            require(result["deadline"] == (result["start"] + TIMEBASE_DELAY) & 0xffffff,
                    "Timebase deadline mismatch")
            require(result["elapsed"] == (result["end"] - result["start"]) & 0xffffff,
                    "Timebase modular elapsed mismatch")
    else:
        raise ValueError("Timebase record is in progress or has an unknown phase; halt at a checkpoint")
    return result


def effective_clock_status(command: int) -> int:
    return command | 8 if command & 0x78 == 0x40 else command


def decode_clock_fixture(data: bytes, *, allow_running: bool = False) -> dict:
    require(isinstance(data, bytes) and len(data) == CLOCK_FIXTURE_SIZE,
            "Clock snapshot must contain exactly 56 bytes")
    require(data[:6] == b"M2CK\x01\x38", "Wrong clock signature/version/size")
    require(data[46:] == b"\0" * 8 + b"\x69\x96", "Clock reserved bytes/guards changed")
    phase, reason, stage, source, steps, result = data[6:12]
    require(phase in (1, 3, 4) or (allow_running and phase == 2),
            "Clock record is in progress or has an unknown phase; halt at a verified checkpoint")
    require(reason in range(4) and stage in range(3) and source == (1 if stage == 1 else 0)
            and result in range(9), "Unknown clock reason/stage/source/result")
    timeout, limit = int.from_bytes(data[12:15], "little"), int.from_bytes(data[15:17], "little")
    require(timeout == CLOCK_TIMEOUT and limit == CLOCK_POLL_LIMIT, "Clock fixed timeout/poll budget changed")
    diag = {}
    for name, start in (("request", 17), ("rollback", 24)):
        wait = {"elapsed_ticks": int.from_bytes(data[start:start + 4], "little"),
                "polls": int.from_bytes(data[start + 4:start + 6], "little"),
                "timebase_status": data[start + 6]}
        require(wait["elapsed_ticks"] <= 0xffffff and wait["polls"] <= limit
                and wait["timebase_status"] in (0, 1, 2), "Clock diagnostic range/status violation")
        diag[name] = wait
    diag.update(zip(("saved_command", "requested_command", "observed_command", "observed_status",
                     "rollback_result"), data[31:36]))
    require(diag["rollback_result"] in (0, 3, 4, 5, 6, 7, 8, 9), "Unknown clock rollback result")
    no_attempt = b"\0" * 18 + b"\x08"
    initial_sleep, current_sleep = data[36], data[42]
    initial_irq, current_irq = data[37:40], data[43:46]
    command, status = data[40:42]
    if phase in (1, 2) or result in (1, 8):
        require((result == 8 or (phase == 4 and reason == 1 and result == 1))
                and data[17:36] == no_attempt, "Unattempted clock record contains results")
        if phase == 1:
            require(reason == stage == source == steps == 0, "Invalid clock initialization")
        if phase == 2:
            require(reason == 0, "RUNNING clock record has a fault")
    else:
        require(diag["requested_command"] == (diag["saved_command"] & 0xb8) | (0x41 if source == 0 else 0),
                "Clock requested command/source mismatch")
        if result not in (1, 2):
            require(diag["saved_command"] & 7 == (1 if diag["saved_command"] & 0x40 else 0),
                    "Clock request did not start from an undivided source")
    if phase in (1, 2, 3):
        require(reason == 0 and initial_sleep == current_sleep and initial_sleep & 7 == 4
                and initial_irq == current_irq == b"\0" * 3, "Clock sleep/IRQ invariant changed")
    if phase == 1:
        require(command & 0x47 == 0x41 and status == effective_clock_status(command),
                "Clock fixture did not initialize on stable undivided RC16")
    if phase == 3:
        require(result == 0 and diag["rollback_result"] == 8 and data[24:31] == b"\0" * 7,
                "READY clock record contains a failure/rollback")
        require(diag["request"]["timebase_status"] == 0 and diag["request"]["elapsed_ticks"] <= timeout,
                "READY clock request exceeded its deadline or failed")
        require(command == diag["requested_command"] == diag["observed_command"]
                and status == diag["observed_status"] == effective_clock_status(command),
                "READY clock actual CMD/STA does not match the requested stable source")
        if stage == 0:
            require(data[17:24] == b"\0" * 7 and diag["saved_command"] == command,
                    "RC idempotent stage performed a request")
        else:
            require(1 <= diag["request"]["polls"] <= limit and diag["saved_command"] != command,
                    "Switch stage lacks a bounded request")
    if phase == 4:
        require(reason != 0 and (1 <= result <= 7 if reason == 1 else result in (0, 8)),
                "Clock FAULT reason/result mismatch")
        if reason == 1:
            request, rollback = diag["request"], diag["rollback"]
            if result in (1, 2):
                require(request["polls"] == 0 and diag["rollback_result"] == 8,
                        "Pre-request error performed polling/rollback")
            else:
                require(diag["rollback_result"] != 8, "Post-request error did not attempt rollback")
            for cause, wait in ((result, request), (diag["rollback_result"], rollback)):
                if cause == 5:
                    require(wait["timebase_status"] != 0, "Clock timebase error has successful helper status")
                else:
                    require(wait["timebase_status"] == 0, "Clock cause/helper status mismatch")
                if cause in (0, 3, 4, 6, 7, 9):
                    require(wait["polls"] >= 1, "Clock outcome lacks a poll observation")
                if cause == 0:
                    require(wait["elapsed_ticks"] <= timeout, "Rollback confirmation was late")
                if cause == 3:
                    require(timeout <= wait["elapsed_ticks"] < 0x800000, "Clock timeout elapsed is invalid")
                if cause == 4:
                    require(wait["polls"] == limit and wait["elapsed_ticks"] < timeout,
                            "Clock poll-limit result has inconsistent bounds")
                if cause == 9:
                    require(wait["elapsed_ticks"] < 0x800000 and
                            (wait["elapsed_ticks"] >= timeout or wait["polls"] == limit)
                            and diag["observed_command"] == diag["saved_command"]
                            and (diag["observed_status"] & 0x40) == (diag["saved_command"] & 0x40),
                            "Unconfirmed cancellation lacks bounded, never-departed source evidence")
            if diag["rollback_result"] == 0:
                require(diag["observed_command"] == diag["saved_command"] and
                        diag["observed_status"] == effective_clock_status(diag["saved_command"]),
                        "Confirmed clock rollback does not restore saved settings")
    return {"signature": "M2CK", "abi_version": 1, "byte_size": CLOCK_FIXTURE_SIZE,
            "phase": phase, "reason": reason, "stage": stage, "requested_source": source,
            "completed_steps": steps, "clock_result": result, "timeout_ticks": timeout, "poll_limit": limit,
            "diagnostics": diag, "initial_sleep_command": initial_sleep,
            "initial_interrupt_enables": list(initial_irq), "current_clock_command": command,
            "current_clock_status": status, "current_sleep_command": current_sleep,
            "current_interrupt_enables": list(current_irq)}


def snapshot_input(args, size: int) -> bytes:
    if args.hex is not None:
        data = bytes.fromhex(args.hex)
    else:
        with args.snapshot.open("rb") as stream:
            data = stream.read(size + 1)
    require(len(data) == size, f"Snapshot must contain exactly {size} bytes")
    return data


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("decode-status", "decode-config"):
        child = commands.add_parser(name)
        child.add_argument("value", type=lambda text: int(text, 0))
    for name in ("symbols", "breakpoint", "source-lines", "status", "fixture-state", "timebase-state",
                 "clock-state", "clock-checkpoint", "irq-state", "irq-checkpoints",
                 "radio-fifo-state", "radio-fifo-checkpoints", "dma-state", "dma-checkpoints",
                 "aes-state", "aes-checkpoints", "prng-state", "prng-checkpoints",
                 "radio-rx-state", "radio-rx-checkpoints"):
        child = commands.add_parser(name)
        child.add_argument("--output", type=Path, required=True)
        child.add_argument("--board", choices=BOARDS, required=True)
        child.add_argument("--image", choices=IMAGES, default="bringup")
        if name in ("symbols", "breakpoint"):
            child.add_argument("--name", required=name == "breakpoint")
        if name == "breakpoint":
            child.add_argument("--slot", type=int, required=True)
        if name == "source-lines":
            source = child.add_mutually_exclusive_group()
            source.add_argument("--pc", type=lambda text: int(text, 0))
            source.add_argument("--file")
            child.add_argument("--line", type=int)
        if name in ("status", "fixture-state", "timebase-state", "clock-state", "irq-state",
                    "radio-fifo-state", "dma-state", "aes-state", "prng-state", "radio-rx-state"):
            source = child.add_mutually_exclusive_group(required=True)
            source.add_argument("--hex")
            source.add_argument("--snapshot", type=Path)
    args = parser.parse_args(argv)
    try:
        if args.command in ("decode-status", "decode-config"):
            result = (decode_status if args.command == "decode-status" else decode_config)(args.value)
        else:
            image = DebugImage(args.output, args.board, args.image)
            result = {"board": image.board, "image": image.image_name, "image_sha256": image.sha256,
                      "evidence": "offline-image-checked"}
            if args.command == "symbols":
                symbols = [image.symbol(args.name)] if args.name is not None else [
                    image.symbols[name] for name in sorted(image.symbols)]
                result["symbols"] = [asdict(symbol) for symbol in symbols]
            elif args.command == "breakpoint":
                result["breakpoint"] = image.breakpoint(args.name, args.slot)
            elif args.command == "source-lines":
                result["mapping"] = "exact-cdb-records"
                result["source_lines"] = [asdict(location) for location in image.source_lines(
                    pc=args.pc, file=args.file, line=args.line)]
            elif args.command == "status":
                result["status"] = decode_bootstrap(snapshot_input(args, 32), args.board)
            elif args.command == "fixture-state":
                require(args.image == "debug_fixture", "M1 state requires a debug_fixture image")
                result["fixture_state"] = decode_fixture(snapshot_input(args, 16))
            elif args.command == "timebase-state":
                require(args.image == "timebase_fixture", "Timebase state requires a timebase_fixture image")
                result["timebase_state"] = decode_timebase_fixture(snapshot_input(args, TIMEBASE_FIXTURE_SIZE))
            elif args.command in ("irq-state", "irq-checkpoints"):
                require(args.image == "irq_fixture", "IRQ inspection requires an irq_fixture image")
                result["irq_proof"] = image.irq_proof
                if args.command == "irq-state":
                    result["irq_state"] = decode_irq_fixture(snapshot_input(args, 64))
            elif args.command in ("radio-fifo-state", "radio-fifo-checkpoints"):
                from radio_fifo_fixture import decode, SIZE
                require(args.image == "radio_fifo_fixture", "FIFO inspection requires radio_fifo_fixture")
                result["radio_fifo_proof"] = image.radio_fifo_proof
                if args.command == "radio-fifo-state":
                    result["radio_fifo_state"] = decode(snapshot_input(args, SIZE))
            elif args.command in ("dma-state", "dma-checkpoints"):
                from dma_fixture import decode, SIZE
                require(args.image == "dma_fixture", "DMA inspection requires dma_fixture")
                result["dma_proof"] = image.dma_proof
                if args.command == "dma-state":
                    result["dma_state"] = decode(snapshot_input(args, SIZE))
            elif args.command in ("aes-state", "aes-checkpoints"):
                from aes_fixture import decode, SIZE
                require(args.image == "aes_fixture", "AES inspection requires aes_fixture")
                result["aes_proof"] = image.aes_proof
                if args.command == "aes-state":
                    result["aes_state"] = decode(snapshot_input(args, SIZE))
            elif args.command in ("radio-rx-state", "radio-rx-checkpoints"):
                from radio_rx_fixture import decode, SIZE
                require(args.image == "radio_rx_fixture", "RX inspection requires radio_rx_fixture")
                result["radio_rx_proof"] = image.radio_rx_proof
                if args.command == "radio-rx-state":
                    result["radio_rx_state"] = decode(snapshot_input(args, SIZE))
            elif args.command in ("prng-state", "prng-checkpoints"):
                from prng_fixture import decode, SIZE
                require(args.image == "prng_fixture", "PRNG inspection requires prng_fixture")
                result["prng_proof"] = image.prng_proof
                if args.command == "prng-state":
                    result["prng_state"] = decode(snapshot_input(args, SIZE))
            else:
                require(args.image == "clock_fixture", "Clock inspection requires a clock_fixture image")
                result["timeout_checkpoint"] = image.clock_timeout_checkpoint
                if args.command == "clock-state":
                    result["clock_state"] = decode_clock_fixture(snapshot_input(args, CLOCK_FIXTURE_SIZE))
    except (OSError, ValueError, KeyError) as error:
        print(f"debug-image: {error}", file=sys.stderr)
        return 1
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
