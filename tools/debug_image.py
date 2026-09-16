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
)


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
        debug_text = (output / f"{image_name}.cdb").read_text(encoding="utf-8")
        self.symbols = linked_symbols((output / f"{image_name}.map").read_text(encoding="utf-8"),
                                      debug_text, image)
        self.source_locations = linked_source_locations(debug_text, image)

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
    for name in ("symbols", "breakpoint", "source-lines", "status", "fixture-state", "timebase-state"):
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
        if name in ("status", "fixture-state", "timebase-state"):
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
            else:
                require(args.image == "timebase_fixture", "Timebase state requires a timebase_fixture image")
                result["timebase_state"] = decode_timebase_fixture(snapshot_input(args, TIMEBASE_FIXTURE_SIZE))
    except (OSError, ValueError, KeyError) as error:
        print(f"debug-image: {error}", file=sys.stderr)
        return 1
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
