#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Validate the SDCC 4.2 M0 linked artifacts, including absolute XDATA."""

import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BOARDS = {"generic": 0, "lg_esl29_rev03": 1}
STATUS_ADDRESS = 0x1E00
STATUS_SIZE = 32
STATUS_RESERVED = 64
IRAM_ALIAS = 0x1F00
CODE_LIMIT = 0x8000


def require(condition, message):
    if not condition:
        raise ValueError(message)


def parse_ihex(text):
    image = {}
    base = 0
    ended = False
    for number, line in enumerate(text.splitlines(), 1):
        line = line.strip()
        if not line:
            continue
        require(not ended, f"HEX line {number}: data after EOF")
        require(line.startswith(":"), f"HEX line {number}: missing colon")
        record = bytes.fromhex(line[1:])
        require(len(record) >= 5, f"HEX line {number}: short record")
        length = record[0]
        address = int.from_bytes(record[1:3], "big")
        kind = record[3]
        data = record[4:-1]
        require(len(data) == length, f"HEX line {number}: length mismatch")
        require(sum(record) % 256 == 0, f"HEX line {number}: checksum mismatch")
        if kind == 0:
            require(address + length <= 0x10000, "HEX record crosses address window")
            for offset, byte in enumerate(data):
                target = base + address + offset
                require(target not in image, "HEX has overlapping records")
                image[target] = byte
        elif kind == 1:
            require(address == 0 and length == 0, "Invalid HEX EOF")
            ended = True
        elif kind in (2, 4):
            require(address == 0 and length == 2, "Invalid HEX extended address")
            base = int.from_bytes(data, "big") << (4 if kind == 2 else 16)
        else:
            raise ValueError(f"Unsupported M0 HEX record type {kind}")
    require(ended and image, "HEX needs data and EOF")
    return image


def parse_symbols(text):
    symbols = {}
    for match in re.finditer(
        r"^\s*(?:C:\s*)?([0-9A-Fa-f]{8})\s+([A-Za-z_]\w*)\s*$"
        r"|^\s*(?:C:\s*)?([0-9A-Fa-f]{8})\s+([A-Za-z_]\w*)\s+\S+\s*$",
        text, re.MULTILINE,
    ):
        address, name = (match[1], match[2]) if match[1] else (match[3], match[4])
        value = int(address, 16)
        require(name not in symbols or symbols[name] == value, f"Conflicting symbol {name}")
        symbols[name] = value
    require("_main" in symbols, "Missing SDCC linked main symbol")
    return symbols


def xdata_ranges(symbols):
    return [
        (symbols[f"s_{name}"], symbols[f"s_{name}"] + symbols[f"l_{name}"])
        for name in ("XSEG", "XISEG", "PSEG")
    ]


def verify_layout(symbols, memory, debug):
    require(symbols["_m0_status"] == STATUS_ADDRESS, "Status address/IRAM alias violation")
    require(symbols["__XPAGE"] == 0x93, "SDCC page register must be CC2530 MPAGE")
    sizes = re.findall(r"^S:G\$m0_status\$[^(\n]+\(\{(\d+)\}", debug, re.MULTILINE)
    require(sizes and all(int(size) == STATUS_SIZE for size in sizes), "Status debug ABI size mismatch")
    require("C$bringup.c$" in debug, "Missing source-level debug records")
    require(STATUS_ADDRESS + STATUS_RESERVED <= IRAM_ALIAS, "Status reservation reaches IRAM alias")
    require(symbols["l_XABS"] == 0, "New absolute XDATA area needs explicit accounting")
    ranges = xdata_ranges(symbols)
    for start, end in ranges:
        require(0 <= start <= end <= STATUS_ADDRESS, "XDATA allocator overlaps status/alias")
    occupied = set()
    for start, end in ranges:
        require(not occupied.intersection(range(start, end)), "Overlapping XDATA allocator areas")
        occupied.update(range(start, end))
    require(len(occupied) + STATUS_RESERVED <= 512, "M0 exceeds 512-byte XDATA budget")

    # SDCC .mem omits __at objects. Account for the status separately, and
    # reject additional global XDATA objects not covered by allocator areas.
    for match in re.finditer(
        r"^S:G\$([^$]+)\$[^(\n]+\(\{(\d+)\}[^)\n]+\),F,", debug, re.MULTILINE,
    ):
        name, size = match[1], int(match[2])
        if name == "m0_status":
            continue
        start = symbols["_" + name]
        require(set(range(start, start + size)) <= occupied, "Unaccounted absolute XDATA object")
    stack = re.search(
        r"Stack starts at: 0x([0-9a-fA-F]+) \(sp set to 0x([0-9a-fA-F]+)\)"
        r" with (\d+) bytes available", memory,
    )
    require(stack is not None, "Missing SDCC IRAM stack accounting")
    start, sp, size = int(stack[1], 16), int(stack[2], 16), int(stack[3])
    require(
        start == symbols["s_SSEG"] == symbols["__start__stack"]
        and size == symbols["l_SSEG"] and sp + 1 == start
        and start >= 8 and size >= 128 and start + size == 256,
        "Invalid/insufficient IRAM stack reservation",
    )
    return {
        "ordinary_xdata_bytes": len(occupied),
        "status_bytes": STATUS_SIZE,
        "status_reserved_bytes": STATUS_RESERVED,
        "nonaliased_xdata_used_bytes": len(occupied) + STATUS_SIZE,
        "nonaliased_xdata_reserved_bytes": len(occupied) + STATUS_RESERVED,
        "iram_stack_start": start,
        "iram_stack_reserved_bytes": size,
    }


def verify_artifacts(output, board):
    image = parse_ihex((output / "bringup.ihx").read_text(encoding="ascii"))
    require(image == parse_ihex((output / "bringup.hex").read_text(encoding="ascii")),
            "IHX/HEX content differs")
    require(min(image) == 0 and max(image) < CODE_LIMIT, "Image outside lower unbanked CODE")
    require(image[0] == 0x02, "Missing reset LJMP")
    binary = bytes(image.get(address, 0xFF) for address in range(max(image) + 1))
    require(binary == (output / "bringup.bin").read_bytes(), "HEX/BIN content differs")
    symbols = parse_symbols((output / "bringup.map").read_text(encoding="utf-8"))
    memory = (output / "bringup.mem").read_text(encoding="utf-8")
    debug = (output / "bringup.cdb").read_text(encoding="utf-8")
    metrics = verify_layout(symbols, memory, debug)
    for name in ("_main", "_bringup_initialize", "_bringup_tick", "__sdcc_external_startup"):
        require(symbols[name] in image, f"Code symbol {name} is outside image")
    address = symbols["_board_description"]
    require(bytes(image[address + offset] for offset in range(2)) == bytes([BOARDS[board]] * 2),
            "Linked board identity/policy does not match selected board")
    flash = re.search(
        r"ROM/EPROM/FLASH\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+(\d+)\s+(\d+)",
        memory,
    )
    require(flash is not None and int(flash[1], 16) == 0
            and int(flash[2], 16) == max(image) and int(flash[3]) == len(image)
            and int(flash[4]) == CODE_LIMIT, "Linked flash accounting mismatch")
    metrics["code_bytes"] = len(image)
    metrics["image_extent_bytes"] = len(binary)
    return metrics, symbols


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board", choices=BOARDS, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--compiler", default="sdcc")
    args = parser.parse_args()
    metrics, _ = verify_artifacts(args.output, args.board)
    compiler = subprocess.run([args.compiler, "--version"], check=True, capture_output=True, text=True).stdout
    require(re.search(r"\b4\.2\.0\b", compiler) is not None, "M0 baseline requires SDCC 4.2.0")
    revision = subprocess.run(
        ["git", "rev-parse", "--verify", "--quiet", "HEAD"], cwd=ROOT, capture_output=True, text=True,
    )
    require(revision.returncode in (0, 1), "Cannot determine source revision")
    dirty = subprocess.run(
        ["git", "status", "--porcelain"], cwd=ROOT, check=True, capture_output=True, text=True,
    )
    info = {
        "schema": 1,
        "board": args.board,
        "capability": "non-networking-bootstrap",
        "hardware_tested": False,
        "compiler": compiler.splitlines()[0],
        "git_revision": revision.stdout.strip() or None,
        "git_dirty": bool(dirty.stdout),
        "memory": metrics,
        "sha256": {
            f"bringup.{extension}": hashlib.sha256((args.output / f"bringup.{extension}").read_bytes()).hexdigest()
            for extension in ("ihx", "hex", "bin", "map", "mem", "cdb")
        },
    }
    (args.output / "build-info.json").write_text(json.dumps(info, indent=2) + "\n", encoding="utf-8")
    print(f"{args.board}: {metrics['code_bytes']} CODE bytes; "
          f"{metrics['nonaliased_xdata_used_bytes']} XDATA used/"
          f"{metrics['nonaliased_xdata_reserved_bytes']} reserved; "
          f"{metrics['iram_stack_reserved_bytes']} IRAM stack reserved. Image checks PASS.")


if __name__ == "__main__":
    main()
