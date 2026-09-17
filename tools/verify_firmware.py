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
IMAGES = ("bringup", "debug_fixture", "timebase_fixture", "clock_fixture", "irq_fixture")
CAPABILITIES = {
    "bringup": "non-networking-bootstrap",
    "debug_fixture": "non-networking-debug-fixture",
    "timebase_fixture": "non-networking-awake-timebase-fixture",
    "clock_fixture": "non-networking-init-clock-fixture",
    "irq_fixture": "non-networking-timer1-irq-fixture",
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
CLOCK_FIXTURE_SIZE = 56
CLOCK_TIMEOUT = 1024
CLOCK_POLL_LIMIT = 4096
CLOCK_CHECKPOINTS = ("_clock_fixture_before_call", "_clock_fixture_ready_stop", "_clock_fixture_fault_stop")
IRQ_SAVE_BYTES = bytes.fromhex("10 af 04 75 82 00 22 75 82 01 22")
IRQ_RESTORE_BYTES = bytes.fromhex("e5 82 60 09 14 70 0c d2 af 75 82 00 22 c2 af 75 82 00 22 75 82 01 22")
IRQ_FIXTURE_SIZE = 64
IRQ_CHECKPOINTS = tuple("_irq_fixture_" + name + "_stop"
                        for name in ("before", "armed", "pending", "inner", "ready", "fault"))
IRQ_VECTOR_HOLES = tuple(address for vector in range(3, 0x4b, 8) for address in range(vector + 1, vector + 8))
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


# Reviewed SDCC 4.2.0 clock-module subset, shared with the standalone checker.
CLOCK_INSTRUCTION_LENGTHS = {
    opcode: size for size, opcodes in (
        (1, "08 0f 22 29 2d 3a 3e 58 9c 9d 9e 9f a3 c3 e0 e4 e8 e9 ea eb ec ed ee ef f0 f8 f9 fa fb fc fd fe ff"),
        (2, "25 35 40 42 45 50 54 55 60 65 70 74 78 79 7f 80 88 89 8a 8b 8c 8d 8e 8f 94 95 a8 a9 ab ae af c0 d0 e5 f5"),
        (3, "02 12 20 30 43 53 75 85 90 b5 b8 b9 bc bf"),
    ) for opcode in bytes.fromhex(opcodes)
}


def cdb_address(debug, record):
    values = re.findall(rf"^{re.escape(record)}:([^\n]*)$", debug, re.MULTILINE)
    require(values and all(re.fullmatch(r"[0-9A-Fa-f]+", value) for value in values)
            and len({int(value, 16) for value in values}) == 1,
            f"Missing/conflicting clock CDB address: {record}")
    return int(values[0], 16)


def cdb_local(debug, prefix, declaration):
    records = re.findall(rf"^S:({re.escape(prefix)}\$[^(\n]+)([^\n]*)$", debug, re.MULTILINE)
    require(len(set(records)) == 1 and records[0][1] == declaration,
            f"Missing/conflicting CDB local declaration: {prefix}")
    return cdb_address(debug, "L:" + records[0][0])


def verify_irq_primitives(image, symbols, debug):
    require(symbols.get("_irq_ea") == 0xaf and symbols.get("_SOC_IEN0") == 0xa8
            and cdb_address(debug, "L:G$irq_ea$0_0$0") == 0xaf
            and "S:G$irq_ea$0_0$0({1}SX:U),J,0,0" in debug, "EA bit address/type mismatch")
    for name, code in (("irq_save_disable", IRQ_SAVE_BYTES), ("irq_restore", IRQ_RESTORE_BYTES)):
        start = symbols["_" + name]
        require(cdb_address(debug, f"L:G${name}$0$0") == start
                and cdb_address(debug, f"L:XG${name}$0$0") == start + len(code),
                "Naked IRQ explicit function extent mismatch")
        require(all(image.get(start + offset) == byte for offset, byte in enumerate(code)),
                "IRQ primitive instruction/ABI mismatch")
        require(f"F:G${name}$0_0$0({{2}}DF,SC:U),Z,0,0,0,0,0" in debug,
                "IRQ naked byte-return ABI mismatch")


def verify_clock_diagnostics(debug, modules=("clock",)):
    for module in modules:
        for suffix, fields in (
            ("00", ((0, "elapsed_ticks", 4), (4, "polls", 2), (6, "timebase_status", 1))),
            ("01", ((0, "request", 7), (7, "rollback", 7), (14, "saved_command", 1),
                    (15, "requested_command", 1), (16, "observed_command", 1),
                    (17, "observed_status", 1), (18, "rollback_result", 1))),
        ):
            records = re.findall(rf"^T:F{module}\$__000000{suffix}\[(.*)\]$", debug, re.MULTILINE)
            require(records, "Missing clock diagnostic layout")
            for record in records:
                actual = re.findall(r"\(\{(\d+)\}S:S\$([^$]+)\$0_0\$0\(\{(\d+)\}", record)
                require(tuple((int(offset), name, int(size)) for offset, name, size in actual) == fields,
                        "Clock diagnostic field layout changed")


def peripheral_accesses(instructions):
    direct_first = {
        0x05, 0x15, 0x25, 0x35, 0x42, 0x43, 0x45, 0x52, 0x53, 0x55,
        0x62, 0x63, 0x65, 0x75, 0x86, 0x87, 0x95, 0xa6, 0xa7, 0xb5,
        0xc0, 0xc5, 0xd0, 0xd5, 0xe5, 0xf5,
    } | set(range(0x88, 0x90)) | set(range(0xa8, 0xb0))
    bit_first = {0x10, 0x20, 0x30, 0x72, 0x82, 0x92, 0xa0, 0xa2, 0xb0, 0xb2, 0xc2, 0xd2}
    accesses = []
    for address, data in instructions.items():
        operands = data[1:3] if data[0] == 0x85 else data[1:2] if data[0] in direct_first else ()
        if data[0] in bit_first:
            operands = [data[1] & 0xf8] if data[1] >= 0x80 else ()
        for operand in operands:
            if operand >= 0x80 and operand not in (0x81, 0x82, 0x83, 0xd0, 0xe0, 0xf0):
                accesses.append((address, data, operand))
    return accesses


def verify_clock_code(image, symbols, debug):
    start = cdb_address(debug, "L:Fclock$effective_status$0$0")
    end = cdb_address(debug, "L:XG$clock_select_init$0$0") + 1
    require(symbols["_timebase_expired"] < start < symbols["_clock_select_init"] < end <= CODE_LIMIT,
            "Clock module extent changed")
    instructions = {}
    address = start
    while address < end:
        size = CLOCK_INSTRUCTION_LENGTHS.get(image.get(address))
        require(size is not None and address + size <= end
                and all(address + i in image for i in range(size)), "Unreviewed clock instruction/length")
        instructions[address] = bytes(image[address + i] for i in range(size))
        address += size
    require(image[end - 1] == 0x22, "Clock module does not end in RET")
    accesses = peripheral_accesses(instructions)
    expected = (b"\xe5\xc6", b"\xe5\x9e", b"\x88\xc6",
                b"\xe5\xa8", b"\xe5\xb8", b"\xe5\x9a", b"\xe5\xbe")
    require(tuple(data for _, data, _ in accesses) == expected,
            "Clock peripheral read/write instruction contract changed")
    sites = {(data[0], operand): address for address, data, operand in accesses}
    calls = [int.from_bytes(data[1:], "big") for data in instructions.values() if data[0] == 0x12]
    for name, count in (("_timebase_read_awake_ticks24", 2), ("_timebase_deadline_after", 1),
                        ("_timebase_expired", 1)):
        require(calls.count(symbols[name]) == count, "Clock linked timebase calls changed")
    return instructions, sites


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
    require("_radio_fifo_clear_init" not in symbols and "_radio_fifo_preload_init" not in symbols
            and "C$radio_fifo.c$" not in debug,
            "Board image must not link the isolated radio FIFO driver")
    require(image_name == "irq_fixture" or
            ("_irq_save_disable" not in symbols and "_irq_restore" not in symbols and "C$irq.c$" not in debug),
            "Board image must not link the isolated IRQ primitives")
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
    if image_name == "clock_fixture":
        sizes = re.findall(r"^S:G\$clock_fixture_state\$[^(\n]+\(\{(\d+)\}", debug, re.MULTILINE)
        require(sizes and all(int(size) == CLOCK_FIXTURE_SIZE for size in sizes),
                "Clock fixture debug ABI size mismatch")
    if image_name == "irq_fixture":
        sizes = re.findall(r"^S:G\$irq_fixture_state\$[^(\n]+\(\{(\d+)\}", debug, re.MULTILINE)
        require(sizes and all(int(size) == IRQ_FIXTURE_SIZE for size in sizes), "IRQ fixture debug ABI mismatch")
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
    elif image_name == "clock_fixture":
        verify_clock_fixture_code(image, symbols, debug)
    elif image_name == "irq_fixture":
        from irq_fixture import verify_irq_fixture
        verify_irq_fixture(image, symbols, debug)
    address = symbols["_board_description"]
    require(bytes(image[address + offset] for offset in range(2)) == bytes([BOARDS[board]] * 2),
            "Linked board identity/policy does not match selected board")
    flash = re.search(
        r"ROM/EPROM/FLASH\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+(\d+)\s+(\d+)",
        memory,
    )
    holes = IRQ_VECTOR_HOLES if image_name == "irq_fixture" else ()
    require(set(range(max(image) + 1)) - image.keys() == set(holes), "Unexpected board CODE holes")
    require(flash is not None and int(flash[1], 16) == 0
            and int(flash[2], 16) == max(image) and int(flash[3]) == len(image) + len(holes)
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
    verify_timebase_reader(image, symbols, debug, symbols["_timebase_fixture_state"], TIMEBASE_FIXTURE_SIZE)


def verify_timebase_reader(image, symbols, debug, state, state_size):
    ordinary = {address for start, end in xdata_ranges(symbols) for address in range(start, end)}
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
        require(address in ordinary and not state <= address < state + state_size,
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


def clock_timeout_checkpoint(image, symbols, debug, instructions, sites):
    # Entire original 148-byte deadline helper; only CDB-verified RAM and
    # runtime CODE operands relocate. Its final RET is shared: live DPL must be 0.
    expected = bytearray.fromhex(
        "af 82 ae 83 ad f0 fc 90 00 00 ef f0 ee a3 f0 ed a3 f0 ec a3 f0 "
        "90 00 00 e0 fc a3 e0 fd a3 e0 fe a3 e0 ff c3 74 ff 9c 74 ff 9d "
        "74 ff 9e e4 9f 40 31 90 00 00 e0 f8 a3 e0 f9 a3 e0 fa a3 e0 fb "
        "c3 ea 94 80 eb 94 00 50 1a 90 00 00 e0 f5 00 a3 e0 f5 00 a3 e0 "
        "f5 00 90 00 00 e0 f5 f0 a3 e0 45 f0 70 05 75 82 01 80 2c "
        "e8 2c fc e9 3d fd ea 3e fe eb 3f 7f 00 85 00 82 85 00 83 85 00 f0 "
        "ec 12 00 00 a3 ed 12 00 00 a3 ee 12 00 00 a3 ef 12 00 00 75 82 00 22")
    ordinary = {a for start, end in xdata_ranges(symbols) for a in range(start, end)}
    locals_ = {}
    for name, size, declaration in (
        ("now", 4, "({4}SL:U),F,0,0"), ("delay", 4, "({4}SL:U),F,0,0"),
        ("deadline", 3, "({3}DG,SL:U),F,0,0"),
    ):
        address = cdb_local(debug, "Ltimebase.timebase_deadline_after$" + name, declaration)
        require(set(range(address, address + size)) <= ordinary, "Deadline helper RAM is not allocated")
        locals_[name] = address
    # SDCC emits only an address record for this three-byte overlay scratch.
    scratch = cdb_address(debug, "L:Ltimebase.timebase_deadline_after$sloc0$0_1$0")
    require(symbols["s_OSEG"] <= scratch and scratch + 3 <= symbols["s_OSEG"] + symbols["l_OSEG"]
            and scratch + 3 <= symbols["s_SSEG"], "Deadline helper overlay is not allocated")
    for offset, name in ((7, "now"), (21, "now"), (49, "delay"), (72, "deadline"), (86, "deadline")):
        expected[offset + 1:offset + 3] = locals_[name].to_bytes(2, "big")
    for offset, delta in ((77, 0), (81, 1), (85, 2), (117, 0), (120, 1), (123, 2)):
        expected[offset] = scratch + delta
    for offset in (127, 132, 137, 142):
        expected[offset:offset + 2] = symbols["__gptrput"].to_bytes(2, "big")
    start = symbols["_timebase_deadline_after"]
    stop = cdb_address(debug, "L:XG$timebase_deadline_after$0$0")
    require(cdb_address(debug, "L:G$timebase_deadline_after$0$0") == start
            and stop == start + 147 and symbols["_timebase_expired"] == stop + 1,
            "Deadline helper explicit extent mismatch")
    require(all(image.get(start + i) == byte for i, byte in enumerate(expected)),
            "Deadline helper relocated bytes/success tail changed")
    declarations = re.findall(r"^S:G\$timebase_deadline_after\$[^(\n]+([^\n]*)$", debug, re.MULTILINE)
    require(declarations and all(value == "({2}DF,SC:U),C,0,0" for value in declarations),
            "Deadline return ABI changed")
    call_bytes = b"\x12" + start.to_bytes(2, "big")
    calls = [address for address, data in instructions.items() if data == call_bytes]
    require(len(calls) == 1, "Deadline helper must have one clock call site")
    call = calls[0]
    require(cdb_address(debug, "L:Fclock$request_and_wait$0$0") < call <
            cdb_address(debug, "L:XFclock$request_and_wait$0$0"), "Deadline call is outside clock wait")
    require([a for a in image if all(image.get(a + i) == value for i, value in enumerate(call_bytes))] == calls,
            "Unexpected additional deadline call in board image")
    # The straight-line continuation saves DPL, unwinds three saved registers,
    # stores the helper result, reloads the owned command, then writes CLKCONCMD.
    context = bytearray.fromhex(
        "a8 82 d0 01 d0 02 d0 03 85 00 82 85 00 83 85 00 f0 e8 12 00 00 90 00 00 e0 f8 88 c6")
    wait_scratch = cdb_local(debug, "Lclock.request_and_wait$sloc0", "({3}DG,SC:U),E,0,0")
    command = cdb_local(debug, "Lclock.request_and_wait$command", "({1}SC:U),F,0,0")
    deadline = cdb_local(debug, "Lclock.request_and_wait$deadline", "({4}SL:U),F,0,0")
    require(command in ordinary and set(range(deadline, deadline + 4)) <= ordinary,
            "Clock wait command/deadline is not allocated")
    require(0x21 <= wait_scratch and wait_scratch + 3 <= symbols["s_SSEG"],
            "Clock wait scratch is outside DATA")
    for offset, delta in ((9, 0), (12, 1), (15, 2)):
        context[offset] = wait_scratch + delta
    context[19:21] = symbols["__gptrput"].to_bytes(2, "big")
    context[22:24] = command.to_bytes(2, "big")
    require(sites[(0x88, 0xc6)] == call + 3 + 26 and
            all(image.get(call + 3 + i) == value for i, value in enumerate(context)),
            "Deadline return-to-request context changed")
    # A separate late-source experiment stops after the real request write,
    # then at the poll's timer call, after C has stored actual source evidence.
    observe = cdb_address(debug, "L:Fclock$observe$0$0")
    wait_start = cdb_address(debug, "L:Fclock$request_and_wait$0$0")
    wait_end = cdb_address(debug, "L:XFclock$request_and_wait$0$0")
    observe_calls = [a for a, data in instructions.items()
                     if wait_start < a < wait_end and data == b"\x12" + observe.to_bytes(2, "big")]
    timer_calls = [a for a, data in instructions.items()
                   if wait_start < a < wait_end and
                   data == b"\x12" + symbols["_timebase_read_awake_ticks24"].to_bytes(2, "big")]
    require(len(observe_calls) == 1 and len(timer_calls) == 2
            and sites[(0x88, 0xc6)] < observe_calls[0] < timer_calls[1],
            "Clock poll observation/sample context changed")
    prefix = bytearray.fromhex(
        "12 00 00 d0 00 d0 01 d0 02 d0 03 90 00 00 e0 f5 00 a3 e0 f5 00 a3 e0 f5 00 "
        "74 11 25 00 f5 00 e4 35 00 f5 00 85 00 00 85 00 82 85 00 83 85 00 f0 "
        "12 00 00 f5 00 74 0f 25 00 f8 e4 35 00 fc af 00 88 82 8c 83 8f f0 12 00 00 "
        "65 00 d0 00 20 e6 0e 85 00 82 85 00 83 85 00 f0 74 01 12 00 00 "
        "85 00 82 85 00 83 85 00 f0 12 00 00 60 06 75 82 05 02 00 00 "
        "c0 00 c0 03 c0 02 c0 01 c0 00 12 00 00")
    parameter = cdb_local(debug, "Lclock.request_and_wait$diagnostics", "({3}DG,ST__00000001:S),F,0,0")
    require(set(range(parameter, parameter + 3)) <= ordinary, "Clock diagnostics pointer is not allocated")
    prefix[12:14] = parameter.to_bytes(2, "big")
    for index, size, declaration, offsets in (
        (0, 3, "DG,SC:U", ((95, 0), (98, 1), (101, 2))),
        (3, 3, "DG,SC:U", ((81, 0), (84, 1), (87, 2))),
        (7, 3, "DG,SC:U", ((30, 0), (35, 1), (38, 2), (40, 0), (43, 1), (46, 2))),
        (8, 1, "SC:U", ((52, 0), (74, 0))),
        (9, 3, "DG,ST__00000001:S", ((16, 0), (20, 1), (24, 2), (28, 0),
                                    (33, 1), (37, 2), (56, 0), (60, 1), (63, 2))),
    ):
        address = cdb_local(debug, f"Lclock.request_and_wait$sloc{index}", f"({{{size}}}{declaration}),E,0,0")
        require(0x21 <= address and address + size <= symbols["s_SSEG"], "Clock poll scratch is outside DATA")
        for offset, delta in offsets:
            prefix[offset] = address + delta
    for offset, address in ((1, observe), (49, symbols["__gptrget"]), (71, symbols["__gptrget"]),
                            (92, symbols["__gptrput"]), (104, symbols["__gptrget"]),
                            (112, wait_end), (125, symbols["_timebase_read_awake_ticks24"])):
        prefix[offset:offset + 2] = address.to_bytes(2, "big")
    require(timer_calls[1] == observe_calls[0] + 124
            and all(image.get(observe_calls[0] + i) == value for i, value in enumerate(prefix)),
            "Clock source-evidence-to-sample bytes changed")
    source_seen = cdb_local(debug, "Lclock.clock_select_init$source_seen", "({1}SC:U),F,0,0")
    diagnostics = cdb_local(debug, "Fclock_fixture_state$diagnostics", "({19}ST__00000003:S),F,0,0")
    require(source_seen in ordinary and set(range(diagnostics, diagnostics + 19)) <= ordinary
            and not diagnostics <= source_seen < diagnostics + 19
            and not symbols["_clock_fixture_state"] <= diagnostics <
            symbols["_clock_fixture_state"] + CLOCK_FIXTURE_SIZE,
            "Clock evidence/diagnostic storage overlaps or is not allocated")
    select_end = cdb_address(debug, "L:XG$clock_select_init$0$0")
    clear_seen = b"\x90" + source_seen.to_bytes(2, "big") + b"\xe4\xf0"
    require(sum(all(image.get(a + i) == byte for i, byte in enumerate(clear_seen))
                for a in range(symbols["_clock_select_init"], select_end)) == 1,
            "Clock per-call source evidence initialization changed")
    for key, declaration, value, count, begin, end in (
        ("Lclock.request_and_wait$source_seen", "DG,SC:U", source_seen, 2,
         symbols["_clock_select_init"], select_end),
        ("Lclock.clock_select_init$diagnostics", "DG,ST__00000001:S", diagnostics, 1,
         symbols["_clock_fixture_cycle"], cdb_address(debug, "L:XG$clock_fixture_cycle$0$0")),
        ("Ltimebase.timebase_deadline_after$deadline", "DG,SL:U", deadline, 1, wait_start, wait_end),
    ):
        parameter = cdb_local(debug, key, f"({{3}}{declaration}),F,0,0")
        require(set(range(parameter, parameter + 3)) <= ordinary, "Clock pointer argument is not allocated")
        store = (b"\x90" + parameter.to_bytes(2, "big") +
                 bytes((0x74, value & 255, 0xf0, 0x74, value >> 8, 0xa3, 0xf0, 0xe4, 0xa3, 0xf0)))
        require(sum(all(image.get(a + i) == byte for i, byte in enumerate(store))
                    for a in range(begin, end)) == count,
                "Clock inspected object differs from actual pointer argument")
    post_write = sites[(0x88, 0xc6)] + 2
    command_scratch = cdb_local(debug, "Lclock.request_and_wait$sloc2", "({1}SC:U),E,0,0")
    require(instructions.get(post_write) == bytes((0x88, command_scratch))
            and 0x21 <= command_scratch < symbols["s_SSEG"], "Post-request checkpoint instruction changed")
    return {"address": stop, "function_start": start, "function_size": 148,
            "return_address": call + 3, "call_address": call,
            "command_write_address": sites[(0x88, 0xc6)], "deadline_address": deadline,
            "command_address": command, "post_request_address": post_write,
            "poll_observe_address": observe_calls[0], "poll_sample_address": timer_calls[1],
            "source_seen_address": source_seen, "diagnostics_address": diagnostics,
            "return_abi": "DPL=0 with DPS=0; RET shared with error path"}


def verify_clock_fixture_code(image, symbols, debug):
    names = CLOCK_CHECKPOINTS + ("_main", "_clock_fixture_initialize", "_clock_fixture_cycle",
                                "_clock_select_init", "_timebase_read_awake_ticks24",
                                "_timebase_deadline_after", "_timebase_expired")
    require(all(name in symbols and symbols[name] in image for name in names)
            and len({symbols[name] for name in names}) == len(names), "Missing/overlapping clock fixture symbol")
    for name, expected in zip(CLOCK_CHECKPOINTS, (b"\0\x22", b"\0\x22", b"\0\x80\xfd")):
        require(all(image.get(symbols[name] + i) == value for i, value in enumerate(expected)),
                "Clock fixture checkpoint opcodes changed")
    require(all("C$" + file + "$" in debug for file in ("clock.c", "clock_fixture_state.c", "timebase.c")),
            "Missing clock fixture source records")
    declarations = re.findall(r"^S:G\$clock_fixture_state\$[^(\n]+\(\{56\}ST([^:]+):S\),F,0,0$",
                              debug, re.MULTILINE)
    require(declarations and len(set(declarations)) == 1, "Clock fixture declaration mismatch")
    fields = ((0, "signature", 4), (4, "abi_version", 1), (5, "byte_size", 1), (6, "phase", 1),
              (7, "reason", 1), (8, "stage", 1), (9, "requested_source", 1), (10, "completed_steps", 1),
              (11, "clock_result", 1), (12, "timeout", 3), (15, "poll_limit", 2), (17, "diagnostics", 19),
              (36, "initial_sleep_command", 1), (37, "initial_interrupt_enables", 3),
              (40, "current_clock_command", 1), (41, "current_clock_status", 1),
              (42, "current_sleep_command", 1), (43, "current_interrupt_enables", 3),
              (46, "reserved", 8), (54, "guards", 2))
    records = re.findall(rf"^T:Fclock_fixture_state\${re.escape(declarations[0])}\[(.*)\]$",
                         debug, re.MULTILINE)
    require(records, "Missing clock fixture field layout")
    for record in records:
        actual = re.findall(r"\(\{(\d+)\}S:S\$([^$]+)\$0_0\$0\(\{(\d+)\}", record)
        require(tuple((int(offset), name, int(size)) for offset, name, size in actual) == fields,
                "Clock fixture field layout changed")
    for name, address in (("CLKCONCMD", 0xc6), ("CLKCONSTA", 0x9e), ("SLEEPCMD", 0xbe),
                          ("IEN0", 0xa8), ("IEN1", 0xb8), ("IEN2", 0x9a)):
        require(symbols.get("_SOC_" + name) == address, "Clock fixture SFR address mismatch")
    verify_timebase_reader(image, symbols, debug, symbols["_clock_fixture_state"], CLOCK_FIXTURE_SIZE)
    verify_clock_diagnostics(debug)
    instructions, sites = verify_clock_code(image, symbols, debug)
    return clock_timeout_checkpoint(image, symbols, debug, instructions, sites)


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
