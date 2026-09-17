# SPDX-License-Identifier: BSD-3-Clause
"""Original FIFO fixture ABI and strict SDCC linked proof; no hardware access."""

import hashlib
import re

from verify_firmware import (
    CLOCK_INSTRUCTION_LENGTHS, cdb_address, cdb_local, peripheral_accesses, require,
    verify_clock_code, verify_deadline_helper,
    verify_timebase_reader, xdata_ranges,
)


SIZE = 108
CHECKPOINTS = tuple("_radio_fifo_fixture_" + name for name in ("before", "ready", "fault"))
FIFO_SIZE = 3042
FIFO_SHA256 = "8893db269f9a60b12a57b9d74530bc0e0769661454b366c25bda5feb7e37079b"
FIFO_LENGTHS = {
    op: size for size, text in (
        (1, "08 09 0b 0c 18 19 1a 1d 22 28 2a 2c 2e 33 3b 3d 3f 4b 4d 4e 4f "
            "98 99 9a 9c 9d 9e 9f a3 c3 e0 e4 e8 e9 ea eb ec ed ee ef f0 f8 f9 fa fb fc fd fe ff"),
        (2, "24 25 34 35 40 45 50 54 60 70 74 78 79 7d 7e 7f 80 88 89 8a 8b 8c 8d 8e 8f "
            "94 95 a2 a9 ab ac ad ae af c0 c2 d0 d2 e5 f5"),
        (3, "02 12 20 30 43 53 75 85 90 b5 b8 b9 ba be bf"),
    ) for op in bytes.fromhex(text)
}
EXTERNAL_CODE = {
    "_timebase_read_awake_ticks24": 0x62, "_timebase_deadline_after": 0xba,
    "_timebase_expired": 0x14e, "__gptrget": 0x13ff, "_memset": 0x12dd,
}
EXTERNAL_DATA = {
    "_timebase_deadline_after_PARM_2": (3, 4), "_timebase_deadline_after_PARM_3": (7, 3),
    "_timebase_expired_PARM_2": (0xe, 4), "_timebase_expired_PARM_3": (0x12, 3),
    "_memset_PARM_2": (0x139, 1), "_memset_PARM_3": (0x13a, 2),
}
SPLIT_POINTERS = (
    (0x2de, 0x2e1, 0x13), (0x3c8, 0x3cb, 0x15),
    (0x6c4, 0x6c8, 0x22), (0x83c, 0x83f, 0x2a),
    (0x8c0, 0x8c3, 0x22), (0x976, 0x979, 0x22), (0xa3f, 0xa42, 0x22),
)
IMAGE_HASHES = {
    0x140: "4f7f2691d6d710ea48b5679a4e657a0660b61ece7ce1ec3d4ed377bc680641ac",
    0x168: "caa26c090473b2e9008652d2ee67bb90226b493f392582d978a6ad71aaf37497",
}
STATE_FIELDS = (
    ("signature", 4), ("version", 1), ("size", 1), ("phase", 1), ("reason", 1),
    ("stage", 1), ("completed", 1), ("clock_result", 1), ("fifo_result", 1),
    ("timeout", 3), ("limit", 2), ("clock", 19), ("fifo", 21), ("checked", 1),
    ("mismatch_index", 1), ("actual", 1), ("expected", 1), ("command", 1),
    ("status", 1), ("sleep", 1), ("enables", 3), ("radio", 14), ("initial_flags", 9),
    ("flags", 9), ("radio_valid", 1), ("initial_sleep", 1), ("reserved", 5), ("guards", 2),
)
DIAGNOSTIC_FIELDS = (
    "elapsed_ticks", "polls", "timebase_status", "strobes", "confirmed", "bytes_written",
    "bytes_verified", "errors", "rx_count", "tx_count", "rx_first", "rx_last",
    "rx_packet", "tx_first", "tx_last", "fifo_signals", "sample_valid",
)


def instructions(image, start, end, lengths=FIFO_LENGTHS):
    result = {}
    pc = start
    while pc < end:
        size = lengths.get(image.get(pc))
        require(size is not None and pc + size <= end, "Unreviewed linked instruction/extent")
        require(all(pc + i in image for i in range(size)), "Missing linked instruction byte")
        result[pc] = bytes(image[pc + i] for i in range(size))
        pc += size
    return result


def verify_fifo_relocated(image, symbols, debug):
    """Undo only reviewed compiler relocations, then require the published hash."""
    start = cdb_address(debug, "L:Fradio_fifo$observe$0$0")
    end = cdb_address(debug, "L:XG$radio_fifo_preload_init$0$0") + 1
    require(end - start == FIFO_SIZE, "FIFO module extent changed")
    code = instructions(image, start, end)
    normalized = bytearray(image[i] for i in range(start, end))
    ordinary = {a for lo, hi in xdata_ranges(symbols) for a in range(lo, hi)}
    xb = cdb_local(debug, "Lradio_fifo.observe$command", "({2}DX,SC:U),F,0,0")
    db = cdb_local(debug, "Lradio_fifo.wait_for$sloc0", "({2}DX,SI:U),E,0,0")
    ob = cdb_address(debug, "L:Lradio_fifo.observe$sloc0$0_1$0")
    require(set(range(xb, xb + 78)) <= ordinary, "FIFO private XDATA is not allocated")
    require(8 <= db < db + 31 <= symbols["s_SSEG"] <= 128 and
            symbols["s_OSEG"] <= ob < ob + 15 <= symbols["s_OSEG"] + symbols["l_OSEG"] <= symbols["s_SSEG"],
            "FIFO DATA/overlay allocation changed")
    require(not set(range(db, db + 31)) & set(range(ob, ob + 15)), "FIFO DATA overlaps overlay")
    # SDCC emits H declarations but no CDB addresses for these three bit temps.
    # The exact first CLR operand anchors them; the entire code and bit region
    # must agree. No peripheral bit address is eligible for normalization.
    bit_base = normalized[0x23e]
    require(normalized[0x23d] == 0xc2 and bit_base + 3 <= symbols["l_BSEG"] <= 128 and
            symbols["s_BSEG_BYTES"] == 0x20 and 0x20 + (bit_base + 2) // 8 < symbols["s_SSEG"],
            "FIFO bit scratch is not allocated")
    direct = {0x25, 0x35, 0x43, 0x45, 0x53, 0x75, 0x95, 0xb5, 0xc0, 0xd0, 0xe5, 0xf5}
    direct |= set(range(0x88, 0x90)) | set(range(0xa8, 0xb0))
    sites = {}
    for pc, data in code.items():
        offset, op = pc - start, data[0]
        if op in (0x02, 0x12, 0x90):
            value = int.from_bytes(data[1:], "big")
            if op in (0x02, 0x12):
                if start <= value < end:
                    target = value - start + 0x1f6
                else:
                    names = [name for name in EXTERNAL_CODE if symbols[name] == value]
                    require(len(names) == 1, "Unexpected FIFO external call/jump")
                    target = EXTERNAL_CODE[names[0]]
                    if names[0] == "_timebase_deadline_after":
                        sites["deadline_call"] = pc
            elif xb <= value < xb + 78:
                target = value - xb + 0x19
            elif value == 0 or 0x6180 <= value <= 0x61ef:
                target = value
            else:
                matches = [(base + value - symbols[name]) for name, (base, size) in EXTERNAL_DATA.items()
                           if symbols[name] <= value < symbols[name] + size]
                require(len(matches) == 1 and value in ordinary, "Unexpected FIFO absolute data operand")
                target = matches[0]
            normalized[offset + 1:offset + 3] = target.to_bytes(2, "big")
        for position in (1, 2) if op == 0x85 else (1,) if op in direct else ():
            value = data[position]
            if db <= value < db + 31:
                normalized[offset + position] = value - db + 0x21
            elif ob <= value < ob + 15:
                normalized[offset + position] = value - ob + 0x0d
            else:
                require(value < 8 or value >= 0x80, "Unexpected FIFO direct RAM operand")
        if op in (0x20, 0x30, 0xa2, 0xc2, 0xd2) and data[1] < 0x80:
            require(bit_base <= data[1] < bit_base + 3, "Unexpected FIFO bit operand")
            normalized[offset + 1] = data[1] - bit_base + 1
        if data == b"\x75\xe1\xed":
            sites["flush_rx"] = pc
        if data == b"\x75\xe1\xee":
            sites["flush_tx"] = pc
        if data == b"\x89\xd9":
            sites["write"] = pc
    for lo, hi, delta in SPLIT_POINTERS:
        require(normalized[lo] == (xb + delta) & 255 and normalized[hi] == (xb + delta) >> 8,
                "FIFO split pointer differs from allocated storage")
        normalized[lo], normalized[hi] = (0x19 + delta) & 255, (0x19 + delta) >> 8
    require(hashlib.sha256(normalized).hexdigest() == FIFO_SHA256,
            "Relocated FIFO instructions differ from the published standalone contract")
    require(set(sites) == {"deadline_call", "flush_rx", "flush_tx", "write"}, "Missing FIFO operation sites")
    sites.update(module_start=start, module_end=end, xdata_start=xb)
    return code, sites


def verify_fixture(image, symbols, debug):
    require(all(name in symbols for name in CHECKPOINTS), "Missing radio FIFO checkpoint symbol")
    before, ready, fault = (symbols[name] for name in CHECKPOINTS)
    require(before in IMAGE_HASHES and ready == before + 2 and fault == before + 4
            and symbols["_main"] == before + 0x8d1, "Radio FIFO checkpoint symbols changed")
    program = bytes(image.get(i, 255) for i in range(max(image) + 1))
    require(hashlib.sha256(program).hexdigest() == IMAGE_HASHES[before],
            "Radio FIFO exact board instructions/constants changed")
    require(program[before:before + 7] == b"\0\x22\0\x22\0\x80\xfd",
            "Radio FIFO checkpoint instructions changed")
    require(all("C$" + source + "$" in debug for source in
                ("radio_fifo_fixture.c", "radio_fifo_fixture_state.c", "radio_fifo.c", "clock.c", "timebase.c")),
            "Missing radio FIFO source records")
    expected, offset = [], 0
    for name, size in STATE_FIELDS:
        expected.append((offset, name, size))
        offset += size
    records = re.findall(r"^T:F(?:radio_fifo_fixture|radio_fifo_fixture_state)\$\w+\[(.*)\]$",
                         debug, re.MULTILINE)
    records = [line for line in records if "S:S$radio_valid$" in line]
    require(len(records) == 2, "Missing radio FIFO state ABI")
    for line in records:
        fields = re.findall(r"\(\{(\d+)\}S:S\$([^$]+)\$0_0\$0\(\{(\d+)\}", line)
        require([(int(a), b, int(c)) for a, b, c in fields] == expected, "Radio FIFO state field ABI changed")
    diagnostic_fields, offset = [], 0
    for name, size in zip(DIAGNOSTIC_FIELDS, (4, 2) + (1,) * 15):
        diagnostic_fields.append((offset, name, size))
        offset += size
    context_fields = [(0, "start", 4), (4, "previous", 4), (8, "deadline", 4),
                      (12, "limit", 2), (14, "command", 1), (15, "rx", 5), (20, "tx", 3)]
    for module, tag, expected in (
        ("radio_fifo", "00000000", diagnostic_fields), ("radio_fifo", "00000001", context_fields),
        ("radio_fifo_fixture", "00000004", diagnostic_fields),
        ("radio_fifo_fixture_state", "00000004", diagnostic_fields),
    ):
        records = re.findall(rf"^T:F{module}\$__{tag}\[(.*)\]$", debug, re.MULTILINE)
        require(records, "Missing FIFO diagnostic/context ABI")
        for line in records:
            fields = re.findall(r"\(\{(\d+)\}S:S\$([^$]+)\$0_0\$0\(\{(\d+)\}", line)
            require([(int(a), b, int(c)) for a, b, c in fields] == expected,
                    "FIFO diagnostic/context field ABI changed")
    require(symbols["_radio_fifo_fixture_state"] == 0 and symbols["l_XSEG"] == 309
            and symbols["s_SSEG"] == 0x6d and symbols["l_SSEG"] == 147
            and symbols["l_PSEG"] == symbols["l_XISEG"] == symbols["l_XABS"] == 0,
            "Radio FIFO state/workspace/stack layout changed")
    work = cdb_local(debug, "Fradio_fifo_fixture_state$work", "({21}ST__00000006:S),F,0,0")
    small = cdb_local(debug, "Fradio_fifo_fixture_state$small", "({3}DA3d,SC:U),F,0,0")
    maximum = cdb_local(debug, "Fradio_fifo_fixture_state$maximum", "({125}DA125d,SC:U),D,0,0")
    require(work == 108 and small == 129 and maximum == before + 0x212e
            and program[maximum:maximum + 125] == bytes(i ^ 0x69 for i in range(125)),
            "Radio FIFO workspace or CODE/XDATA payload changed")
    fixture_code = instructions(
        image, cdb_address(debug, "L:Fradio_fifo_fixture_state$put16$0$0"),
        cdb_address(debug, "L:XG$main$0$0") + 1,
        FIFO_LENGTHS | CLOCK_INSTRUCTION_LENGTHS |
        {0x7a: 2, 0x39: 1, 0x0a: 1, 0xf4: 1, 0x04: 1, 0x14: 1, 0xbd: 3, 0x7c: 2, 0x7b: 2},
    )
    sfrs = (0xc6, 0x9e, 0xbe, 0xa8, 0xb8, 0x9a, 0xbf, 0xa9, 0xb9, 0xe9, 0x91, 0x9b, 0x88)
    require(tuple(data for _, data, _ in peripheral_accesses(fixture_code)) ==
            tuple(bytes((0xe5, address)) for address in sfrs), "Fixture added a peripheral write/read")
    expected_xregs = (0x1e18, 0x6189, 0x618a, 0x61e1, 0x6192, 0x618b, 0x6193,
                      0x619b, 0x619c, 0x619d, 0x619e, 0x619f, 0x61a1, 0x61a2,
                      0x61a3, 0x61a4, 0x61a5, 0x1e18, 0x1e19)
    xregs = []
    for pc, data in fixture_code.items():
        if data[0] == 0x90 and int.from_bytes(data[1:], "big") >= 0x1e00:
            require(fixture_code.get(pc + 3) == b"\xe0", "Fixture absolute MMIO is not read-only")
            xregs.append(int.from_bytes(data[1:], "big"))
    require(tuple(xregs) == expected_xregs, "Fixture expanded radio/identity/MMIO inspection")
    payload_start = cdb_address(debug, "L:Fradio_fifo_fixture_state$verify_payload$0$0")
    payload_end = cdb_address(debug, "L:XFradio_fifo_fixture_state$verify_payload$0$0")
    reads = [pc for pc, data in fixture_code.items() if payload_start <= pc < payload_end
             and data == b"\xe0" and program[pc - 4:pc] == b"\x8b\x82\x8c\x83"]
    require(len(reads) == 1, "Fixture TX RAM read instruction changed")
    for name, address in zip(("IP0", "IP1", "RFIRQF0", "RFIRQF1", "S1CON", "TCON"), sfrs[7:]):
        require(symbols.get("_RFF_" + name) == address, "Fixture SFR symbol address changed")
    verify_timebase_reader(image, symbols, debug, 0, SIZE)
    verify_clock_code(image, symbols, debug)
    code, sites = verify_fifo_relocated(image, symbols, debug)
    helper, stop, locals_ = verify_deadline_helper(image, symbols, debug)
    call = sites["deadline_call"]
    require(code[call] == b"\x12" + helper.to_bytes(2, "big")
            and program[call - 4:call] == b"\xc0\x07\xc0\x06", "FIFO helper call/context changed")
    private = {}
    for name, kind in (("length", "({1}SC:U)"), ("timeout", "({4}SL:U)"),
                       ("limit", "({2}SI:U)"), ("d", "({2}DX,ST__00000000:S)"),
                       ("body", "({3}DG,SC:U)"), ("w", "({23}ST__00000001:S)")):
        private[name] = cdb_local(debug, "Lradio_fifo.operate$" + name, kind + ",F,0,0")
    require(private == dict(zip(("length", "timeout", "limit", "d", "body", "w"),
                                (249, 250, 254, 256, 258, 261))), "FIFO operation storage changed")
    caller = before + 0xae3
    require(symbols["_main"] < caller < cdb_address(debug, "L:XG$main$0$0") == before + 0xc36,
            "FIFO caller is not in the genuine inlined main")
    require(program[caller:caller + 3] == b"\x12" + symbols["_radio_fifo_preload_init"].to_bytes(2, "big")
            and code[sites["module_end"] - 4][0] == 0x12 and code[sites["module_end"] - 1] == b"\x22",
            "FIFO nested preload caller changed")
    return dict(sites, address=stop, function_start=helper, function_size=148,
                return_address=call + 3, preload_return=sites["module_end"] - 1,
                fixture_return=caller + 3, helper_locals=locals_, private=private,
                work=work, small=small, maximum=maximum, state=0,
                checkpoints=[before, ready, fault], tx_read_bounds=[0x6080, 0x60fd],
                tx_read_address=reads[0], deadline_sp=0x74)


def inspect_deadline(proof, read, sp, dpl, dps):
    """Read only allocated C objects and the live stack; never radio MMIO/RAM."""
    require(dpl == dps == 0 and sp == proof["deadline_sp"] < 0x80, "FIFO deadline return ABI/stack invalid")
    stack = read(0x1f00 + sp - 7, 8)
    require(int.from_bytes(stack[6:8], "little") == proof["return_address"] and
            int.from_bytes(stack[2:4], "little") == proof["preload_return"] and
            int.from_bytes(stack[:2], "little") == proof["fixture_return"],
            "FIFO deadline has wrong nested caller frames")
    p = proof["private"]
    for name, expected in (
        ("length", b"\x03"), ("timeout", b"\0\x04\0\0"), ("limit", b"\0\x10"),
        ("body", proof["small"].to_bytes(2, "little") + b"\0"),
        ("d", proof["work"].to_bytes(2, "little")),
    ):
        require(read(p[name], len(expected)) == expected, "FIFO live argument mismatch: " + name)
    require(read(proof["small"], 3) == b"\x13\x57\xa9", "FIFO small XDATA payload changed")
    w = read(p["w"], 23)
    start, previous, deadline = (int.from_bytes(w[i:i + 4], "little") for i in (0, 4, 8))
    require(start <= 0xffffff and previous == start and deadline == (start + 1024) & 0xffffff
            and w[12:] == b"\0\x10\x88" + bytes(8), "FIFO computed deadline/context mismatch")
    for name, expected in (("now", start.to_bytes(4, "little")), ("delay", b"\0\x04\0\0"),
                           ("deadline", (p["w"] + 8).to_bytes(2, "little") + b"\0")):
        require(read(proof["helper_locals"][name], len(expected)) == expected,
                "FIFO deadline helper argument mismatch")
    d = read(proof["work"], 21)
    require(d[:19] == bytes(19) and d[19] & 0xe7 == 0 and d[20] == 1,
            "FIFO write/strobe occurred before the deadline hold")
    return {"start": start, "deadline": deadline, "stack_hex": stack.hex(),
            "body_space": "XDATA", "body_length": 3, "written": 0, "verified": 0}


def decode(data, *, allow_running=False):
    require(isinstance(data, bytes) and len(data) == SIZE and data[:6] == b"M2RF\x01\x6c",
            "Radio FIFO fixture signature/version/size mismatch")
    require(data[101:106] == bytes(5) and data[106:] == b"\x69\x96", "Radio FIFO reserved/guard corruption")
    phase, reason, stage, completed, clock_result, fifo_result = data[6:12]
    require(phase in (1, 2, 3, 4) and stage <= 5 and reason <= 6
            and (phase != 2 or allow_running), "Invalid radio FIFO phase/stage")
    require((reason != 0) == (phase == 4), "Radio FIFO fault/reason mismatch")
    timeout = int.from_bytes(data[12:15], "little")
    limit = int.from_bytes(data[15:17], "little")
    require(timeout == 1024 and limit == 4096, "Radio FIFO fixed bounds changed")
    raw_clock, raw_fifo = data[17:36], data[36:57]

    def wait(raw):
        return {"elapsed_ticks": int.from_bytes(raw[:4], "little"),
                "polls": int.from_bytes(raw[4:6], "little"), "timebase_status": raw[6]}

    clock = {"request": wait(raw_clock[:7]), "rollback": wait(raw_clock[7:14]),
             **dict(zip(("saved_command", "requested_command", "observed_command",
                         "observed_status", "rollback_result"), raw_clock[14:]))}
    fifo = wait(raw_fifo)
    fifo.update(zip(DIAGNOSTIC_FIELDS[3:], raw_fifo[7:]))
    require(clock_result <= 9 and (fifo_result <= 12 or fifo_result == 255)
            and fifo["sample_valid"] <= 1 and data[99] <= 1,
            "Radio FIFO result/validity byte out of range")
    for item in (clock["request"], clock["rollback"], fifo):
        require(item["elapsed_ticks"] <= 0xffffff and item["polls"] <= limit
                and item["timebase_status"] <= 2, "Radio FIFO diagnostic bounds invalid")
    require(fifo["strobes"] <= 3 and fifo["confirmed"] & ~fifo["strobes"] == 0 and
            fifo["bytes_verified"] <= fifo["bytes_written"] <= 126, "Radio FIFO partial-effect shape invalid")
    record = {
        "phase": phase, "reason": reason, "stage": stage, "completed": completed,
        "clock_result": clock_result, "fifo_result": fifo_result, "timeout_ticks": timeout,
        "poll_limit": limit, "clock": clock, "fifo": fifo, "checked": data[57],
        "mismatch_index": data[58], "actual": data[59], "expected": data[60],
        "command": data[61], "status": data[62], "sleep": data[63], "enables": list(data[64:67]),
        "radio": list(data[67:81]), "initial_flags": list(data[81:90]), "flags": list(data[90:99]),
        "radio_valid": data[99], "initial_sleep": data[100],
    }
    if phase == 1:
        require(stage == completed == 0 and clock_result == 8 and fifo_result == 255 and
                raw_clock == bytes(18) + b"\x08" and raw_fifo == bytes(21) and
                record["command"] == record["status"] == 0xc9 and not record["radio_valid"],
                "Invalid radio FIFO initial record")
    if phase == 3:
        require(clock_result == 0 and clock["rollback_result"] == 8 and
                clock["saved_command"] == 0xc9 and clock["requested_command"] ==
                clock["observed_command"] == clock["observed_status"] == 0x88 and
                1 <= clock["request"]["polls"] <= limit and
                clock["request"]["elapsed_ticks"] <= timeout and
                clock["request"]["timebase_status"] == 0 and raw_clock[7:14] == bytes(7),
                "Unconfirmed clock in radio FIFO READY")
        require(record["command"] == record["status"] == 0x88 and record["enables"] == [0, 0, 0]
                and record["sleep"] == record["initial_sleep"] and record["sleep"] & 7 == 4
                and record["radio_valid"] == 1 and record["flags"] == record["initial_flags"],
                "Radio FIFO READY clock/IRQ/flag invariant failed")
        count = 4 if stage == 2 else 126 if stage == 4 else 0
        radio = record["radio"]
        require(radio[0:2] == [0x40, 1] and radio[2] & 0xe0 == 0 and radio[3] & 0xc0 == 0
                and radio[4] == 0 and radio[5] & 0xe7 == 0 and radio[6:] ==
                [0, count, 0, 0, 0, 0, count, 0], "Radio FIFO READY live state invalid")
        require(record["mismatch_index"] == 255 and record["actual"] == record["expected"] == 0
                and record["checked"] == count, "Radio FIFO payload verification incomplete")
        if stage == 0:
            require(fifo_result == 255 and raw_fifo == bytes(21), "FIFO ran before clock checkpoint")
        else:
            require(fifo["sample_valid"] == 1 and fifo["timebase_status"] == 0 and
                    fifo["errors"] == fifo["rx_count"] == fifo["rx_first"] ==
                    fifo["rx_last"] == fifo["rx_packet"] == fifo["tx_first"] == 0 and
                    fifo["tx_count"] == fifo["tx_last"] == count and
                    fifo["fifo_signals"] & 0xe7 == 0, "Radio FIFO READY driver observations invalid")
            if stage == 1:
                require(fifo_result == 1 and raw_fifo[:11] == bytes(11), "Expected genuine EMPTY clear")
            elif count:
                require(fifo_result == 0 and fifo["strobes"] == fifo["confirmed"] == 0
                        and fifo["bytes_written"] == fifo["bytes_verified"] == count and
                        count <= fifo["polls"] <= limit and fifo["elapsed_ticks"] < timeout,
                        "Radio FIFO preload result invalid")
            else:
                require(fifo_result == 0 and fifo["strobes"] == fifo["confirmed"] == 2 and
                        fifo["bytes_written"] == fifo["bytes_verified"] == 0 and
                        1 <= fifo["polls"] <= limit and fifo["elapsed_ticks"] < timeout,
                        "Radio FIFO TX clear unconfirmed")
    return record
