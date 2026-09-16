#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Validate SDCC 4.2 non-RF board artifacts, including absolute XDATA."""

import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BOARDS = {"generic": 0, "lg_esl29_rev03": 1}
IMAGES = ("bringup", "debug_fixture", "timebase_fixture")
CAPABILITIES = {
    "bringup": "non-networking-bootstrap",
    "debug_fixture": "non-networking-debug-fixture",
    "timebase_fixture": "non-networking-awake-timebase-fixture",
}
ARTIFACT_EXTENSIONS = ("ihx", "hex", "bin", "map", "mem", "cdb")
STATUS_ADDRESS = 0x1E00
STATUS_SIZE = 32
STATUS_RESERVED = 64
IRAM_ALIAS = 0x1F00
CODE_LIMIT = 0x8000
FIXTURE_SIZE = 16
PROBE_BYTES = bytes.fromhex("74 a5 75 f0 3c 90 12 34 7f 69 d3 00 22")
TIMEBASE_FIXTURE_SIZE = 32
TIMEBASE_DELAY = 128
TIMEBASE_POLL_LIMIT = 1024
TIMEBASE_CHECKPOINTS = ("_timebase_fixture_before_sample", "_timebase_fixture_ready_stop",
                       "_timebase_fixture_fault_stop")
# SDCC 4.2.0 model-large reader at scratch addresses 0/1/2. Only the six
# MOV DPTR,#scratch operands may relocate in a board image, verified via CDB.
TIMEBASE_READER_BYTES = bytes.fromhex(
    "90 00 00 e5 95 f0 90 00 01 e5 96 f0 90 00 02 e5 97 f0 "
    "90 00 00 e0 ff 7e 00 7d 00 7c 00 "
    "90 00 01 e0 f8 79 00 7a 00 8a 03 89 02 88 01 e4 "
    "42 07 e9 42 06 ea 42 05 eb 42 04 "
    "90 00 02 e0 f8 79 00 89 03 88 02 e4 f9 "
    "42 07 e9 42 06 ea 42 05 eb 42 04 8f 82 8e 83 8d f0 ec 22"
)


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
        r"^\s*(?:[CD]:\s*)?([0-9A-Fa-f]{8})\s+([A-Za-z_]\w*)\s*$"
        r"|^\s*(?:[CD]:\s*)?([0-9A-Fa-f]{8})\s+([A-Za-z_]\w*)\s+\S+\s*$",
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


def verify_layout(symbols, memory, debug, image_name="bringup"):
    require(image_name in IMAGES, "Unknown firmware image")
    require(symbols["_m0_status"] == STATUS_ADDRESS, "Status address/IRAM alias violation")
    require(symbols["__XPAGE"] == 0x93, "SDCC page register must be CC2530 MPAGE")
    sizes = re.findall(r"^S:G\$m0_status\$[^(\n]+\(\{(\d+)\}", debug, re.MULTILINE)
    require(sizes and all(int(size) == STATUS_SIZE for size in sizes), "Status debug ABI size mismatch")
    require(f"C${image_name}.c$" in debug, "Missing source-level debug records")
    if image_name == "debug_fixture":
        sizes = re.findall(r"^S:G\$debug_fixture_state\$[^(\n]+\(\{(\d+)\}", debug, re.MULTILINE)
        require(sizes and all(int(size) == FIXTURE_SIZE for size in sizes), "Fixture debug ABI size mismatch")
    if image_name == "timebase_fixture":
        sizes = re.findall(r"^S:G\$timebase_fixture_state\$[^(\n]+\(\{(\d+)\}", debug, re.MULTILINE)
        require(sizes and all(int(size) == TIMEBASE_FIXTURE_SIZE for size in sizes),
                "Timebase fixture debug ABI size mismatch")
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
        require("_" + name in symbols, f"Missing XDATA symbol {name}")
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


def verify_artifacts(output, board, image_name="bringup"):
    require(image_name in IMAGES, "Unknown firmware image")
    image = parse_ihex((output / f"{image_name}.ihx").read_text(encoding="ascii"))
    require(image == parse_ihex((output / f"{image_name}.hex").read_text(encoding="ascii")),
            "IHX/HEX content differs")
    require(min(image) == 0 and max(image) < CODE_LIMIT, "Image outside lower unbanked CODE")
    require(image[0] == 0x02, "Missing reset LJMP")
    binary = bytes(image.get(address, 0xFF) for address in range(max(image) + 1))
    require(binary == (output / f"{image_name}.bin").read_bytes(), "HEX/BIN content differs")
    symbols = parse_symbols((output / f"{image_name}.map").read_text(encoding="utf-8"))
    memory = (output / f"{image_name}.mem").read_text(encoding="utf-8")
    debug = (output / f"{image_name}.cdb").read_text(encoding="utf-8")
    metrics = verify_layout(symbols, memory, debug, image_name)
    for name in ("_main", "_bringup_initialize", "_bringup_tick", "__sdcc_external_startup"):
        require(symbols[name] in image, f"Code symbol {name} is outside image")
    if image_name == "debug_fixture":
        verify_fixture_code(image, symbols)
    elif image_name == "timebase_fixture":
        verify_timebase_fixture_code(image, symbols, debug)
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


def verify_fixture_code(image, symbols):
    names = ("_debug_fixture_initialize", "_debug_fixture_cycle", "_debug_fixture_probe",
             "_debug_fixture_stop") + tuple(f"_debug_fixture_stage{i}" for i in range(4))
    require(all(name in symbols and symbols[name] in image for name in names),
            "Missing/out-of-image fixture code symbol")
    require(len({symbols[name] for name in names}) == len(names), "Fixture code symbols overlap")
    probe = symbols["_debug_fixture_probe"]
    require(symbols["_debug_fixture_stop"] == probe + 11, "Fixture stop is not the probe NOP")
    require(all(image.get(probe + offset) == value for offset, value in enumerate(PROBE_BYTES)),
            "Fixture register probe opcodes changed")


def verify_timebase_fixture_code(image, symbols, debug):
    names = TIMEBASE_CHECKPOINTS + (
        "_main", "_timebase_fixture_initialize", "_timebase_fixture_begin", "_timebase_fixture_poll",
        "_timebase_read_awake_ticks24", "_timebase_deadline_after", "_timebase_expired",
    )
    require(all(name in symbols and symbols[name] in image for name in names),
            "Missing/out-of-image timebase fixture code symbol")
    require(len({symbols[name] for name in names}) == len(names), "Timebase fixture code symbols overlap")
    for name, expected in zip(TIMEBASE_CHECKPOINTS, (b"\x00\x22", b"\x00\x22", b"\x00\x80\xfd")):
        require(all(image.get(symbols[name] + offset) == value for offset, value in enumerate(expected)),
                "Timebase checkpoint opcodes changed")
    require("C$timebase.c$" in debug and "C$timebase_fixture_state.c$" in debug,
            "Missing timebase component source records")
    ordinary = {address for start, end in xdata_ranges(symbols) for address in range(start, end)}
    state = symbols["_timebase_fixture_state"]
    scratch = []
    for name in ("low", "middle", "high"):
        declarations = re.findall(
            rf"^S:(Ltimebase\.timebase_read_awake_ticks24\${name}\$[^(\n]+)([^\n]*)$",
            debug, re.MULTILINE,
        )
        require(len(set(declarations)) == 1 and declarations[0][1] == "({1}SC:U),F,0,0",
                "Missing/conflicting reader scratch declaration")
        addresses = re.findall(rf"^L:{re.escape(declarations[0][0])}:([^\n]*)$", debug, re.MULTILINE)
        require(len(set(addresses)) == 1 and re.fullmatch(r"[0-9A-Fa-f]+", addresses[0]),
                "Missing/conflicting reader scratch address")
        address = int(addresses[0], 16)
        require(address in ordinary and not state <= address < state + TIMEBASE_FIXTURE_SIZE,
                "Reader scratch overlaps status/fixture or is not allocated")
        scratch.append(address)
    require(len(set(scratch)) == 3, "Reader scratch bytes overlap")
    expected = bytearray(TIMEBASE_READER_BYTES)
    for offset, byte in ((0, 0), (6, 1), (12, 2), (18, 0), (29, 1), (56, 2)):
        expected[offset + 1:offset + 3] = scratch[byte].to_bytes(2, "big")
    start = symbols["_timebase_read_awake_ticks24"]
    require(symbols["_timebase_deadline_after"] == start + len(expected), "Timebase reader extent changed")
    require(all(image.get(start + offset) == value for offset, value in enumerate(expected)),
            "Relocated timebase reader opcodes/operands changed")
    require(all(symbols.get(f"_SOC_ST{i}") == 0x95 + i for i in range(3)), "Sleep Timer SFR addresses changed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board", choices=BOARDS, required=True)
    parser.add_argument("--image", choices=IMAGES, default="bringup")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--compiler", default="sdcc")
    args = parser.parse_args()
    metrics, _ = verify_artifacts(args.output, args.board, args.image)
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
        "image": args.image,
        "capability": CAPABILITIES[args.image],
        "hardware_tested": False,
        "compiler": compiler.splitlines()[0],
        "git_revision": revision.stdout.strip() or None,
        "git_dirty": bool(dirty.stdout),
        "memory": metrics,
        "sha256": {
            f"{args.image}.{extension}": hashlib.sha256((args.output / f"{args.image}.{extension}").read_bytes()).hexdigest()
            for extension in ARTIFACT_EXTENSIONS
        },
    }
    (args.output / "build-info.json").write_text(json.dumps(info, indent=2) + "\n", encoding="utf-8")
    print(f"{args.board}: {metrics['code_bytes']} CODE bytes; "
          f"{metrics['nonaliased_xdata_used_bytes']} XDATA used/"
          f"{metrics['nonaliased_xdata_reserved_bytes']} reserved; "
          f"{metrics['iram_stack_reserved_bytes']} IRAM stack reserved. Image checks PASS.")


if __name__ == "__main__":
    main()
