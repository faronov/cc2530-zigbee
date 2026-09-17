# SPDX-License-Identifier: BSD-3-Clause
"""Byte ABI and genuine linked proof for the CC2530 Timer1 board fixture."""

import re

from verify_firmware import (
    CLOCK_INSTRUCTION_LENGTHS, IRQ_CHECKPOINTS, IRQ_FIXTURE_SIZE, IRQ_VECTOR_HOLES,
    cdb_address, cdb_local, require, verify_irq_primitives, verify_timebase_reader, peripheral_accesses,
    xdata_ranges,
)


FIELDS = (
    ("signature", 4), *[(name, 1) for name in (
        "version", "size", "phase", "reason", "stage", "completed", "isr_count", "isr_before",
        "disabled_token", "disabled_result", "outer_token", "inner_token", "inner_result",
        "outer_result", "invalid_result", "helper_status")],
    ("timeout", 3), ("poll_limit", 2), ("pending_polls", 2), ("pending_elapsed", 3),
    ("delivery_polls", 2), ("delivery_elapsed", 3),
    *[(name, 1) for name in ("isr_source", "isr_cpu", "initial_command", "initial_status", "initial_sleep",
                            "initial_ip0", "initial_ip1", "initial_timif", "initial_ircon", "ien0", "ien1",
                            "ien2", "control", "source", "cpu", "ip0", "ip1", "timif", "command", "status", "sleep")],
    ("counter", 2), ("reserved", 4), ("guards", 2),
)
OFFSETS = {name: sum(size for _, size in FIELDS[:index]) for index, (name, _) in enumerate(FIELDS)}


def decode_irq_fixture(data):
    require(isinstance(data, bytes) and len(data) == IRQ_FIXTURE_SIZE, "IRQ snapshot must be exactly 64 bytes")
    require(data[:6] == b"M2IQ\x01\x40" and data[58:62] == b"\0" * 4 and data[62:] == b"\x69\x96",
            "IRQ signature/version/size/reserved/guards mismatch")
    record = {name: int.from_bytes(data[OFFSETS[name]:OFFSETS[name] + size], "little")
              for name, size in FIELDS if name not in ("signature", "reserved", "guards")}
    require(record["phase"] in (1, 2, 3, 4) and record["stage"] in range(6) and record["reason"] in range(11),
            "Invalid IRQ phase/stage/reason")
    require((record["phase"] == 4) == (record["reason"] != 0), "IRQ failure/phase mismatch")
    require(record["timeout"] == 1024 and record["poll_limit"] == 4096, "IRQ wait bounds changed")
    require(record["pending_polls"] <= 4096 and record["delivery_polls"] <= 4096,
            "IRQ poll bound exceeded")
    require(record["helper_status"] in (0, 1, 2), "Unknown IRQ timebase result")
    if record["phase"] != 4:
        require(record["initial_command"] == record["command"] == record["initial_status"] ==
                record["status"] == 0xc9 and record["sleep"] == record["initial_sleep"] and
                record["sleep"] & 7 == 4 and record["ien2"] == 0 and record["ip0"] == record["initial_ip0"] and
                record["ip1"] == record["initial_ip1"] and record["timif"] == record["initial_timif"] and
                record["timif"] & 0x40 and record["cpu"] & 0xfd == record["initial_ircon"],
                "IRQ invariant record changed")
        require(record["helper_status"] == 0, "IRQ nonfault has helper error")
    if record["phase"] == 1:
        require(record["stage"] == record["completed"] == record["isr_count"] == record["ien0"] ==
                record["ien1"] == record["control"] == record["source"] == 0 and not record["cpu"] & 2,
                "IRQ initialization mismatch")
    if record["phase"] in (2, 3):
        require(record["disabled_token"] == record["disabled_result"] == record["inner_token"] == 0 and
                record["outer_token"] == record["invalid_result"] == 1, "IRQ token evidence mismatch")
    if record["phase"] == 2 and record["stage"] in (2, 3):
        require(record["ien0"] == record["control"] == 0 and record["ien1"] == 2 and
                record["source"] == 0x20 and record["cpu"] & 2 and
                record["isr_count"] == record["isr_before"] and 0 < record["pending_polls"] <= 4096 and
                record["pending_elapsed"] < 1024, "IRQ stopped pending/inner evidence mismatch")
        if record["stage"] == 3:
            require(record["inner_result"] == 0, "IRQ inner restore failed")
    if record["phase"] == 3:
        require(record["stage"] == 5 and record["completed"] == record["isr_count"] ==
                (record["isr_before"] + 1) & 255 and record["isr_source"] == 0x20 and
                record["isr_cpu"] == record["initial_ircon"] and record["inner_result"] ==
                record["outer_result"] == record["ien0"] == record["ien1"] == record["control"] ==
                record["source"] == 0 and not record["cpu"] & 2 and
                0 < record["pending_polls"] <= 4096 and 0 < record["delivery_polls"] <= 4096 and
                record["pending_elapsed"] < 1024 and record["delivery_elapsed"] < 1024,
                "IRQ READY delivery/acknowledgment/bounds mismatch")
    return record


def verify_irq_fixture(image, symbols, debug):
    verify_irq_primitives(image, symbols, debug)
    state = symbols["_irq_fixture_state"]
    verify_timebase_reader(image, symbols, debug, state, IRQ_FIXTURE_SIZE)
    require(all(f"C${name}$" in debug for name in ("irq.c", "irq_fixture_state.c", "irq_fixture.c")),
            "Missing IRQ fixture source records")
    require(set(range(max(image) + 1)) - image.keys() == set(IRQ_VECTOR_HOLES),
            "IRQ vector padding changed")
    actual_fields = []
    for text in re.findall(r"^T:Firq_fixture_state\$\w+\[(.*)\]$", debug, re.MULTILINE):
        fields = tuple((int(offset), name, int(size)) for offset, name, size in
                       re.findall(r"\(\{(\d+)\}S:S\$([^$]+)\$0_0\$0\(\{(\d+)\}", text))
        if any(name == "isr_count" for _, name, _ in fields):
            actual_fields.append(fields)
    expected_fields = tuple((OFFSETS[name], name, size) for name, size in FIELDS)
    require(actual_fields and all(fields == expected_fields for fields in actual_fields), "IRQ field ABI changed")
    require(len({symbols.get(name) for name in IRQ_CHECKPOINTS}) == len(IRQ_CHECKPOINTS), "IRQ checkpoint symbols overlap")
    for name in IRQ_CHECKPOINTS:
        code = b"\0\x80\xfd" if "fault" in name else b"\0\x22"
        require(name in symbols and all(image.get(symbols[name] + i) == byte for i, byte in enumerate(code)),
                "IRQ checkpoint symbol/opcodes changed")
        require(cdb_address(debug, f"L:XG${name[1:]}$0$0") == symbols[name] + len(code),
                "IRQ checkpoint explicit extent changed")
    regs = {"_IRQ_T1CNTL": 0xe2, "_IRQ_T1CNTH": 0xe3, "_IRQ_T1CTL": 0xe4, "_IRQ_T1STAT": 0xaf,
            "_IRQ_T1CCTL0": 0xe5, "_IRQ_T1CCTL1": 0xe6, "_IRQ_T1CCTL2": 0xe7,
            "_IRQ_TIMIF": 0xd8, "_IRQ_IRCON": 0xc0, "_IRQ_IP0": 0xa9, "_IRQ_IP1": 0xb9}
    require(all(symbols.get(name) == value for name, value in regs.items()), "IRQ Timer1 SFR address changed")
    isr = symbols["_irq_fixture_timer1_isr"]
    saved = (0xe0, 0x82, 0x83, 7, 0xd0)
    code = b"".join(bytes((0xc0, register)) for register in saved) + b"\x75\xd0\0\x53\xb8\xfd"
    code += b"\x90" + (state + 35).to_bytes(2, "big") + b"\xe5\xaf\xf0"
    code += b"\x90" + (state + 36).to_bytes(2, "big") + b"\xe5\xc0\xf0\x75\xaf\x1f"
    code += b"\x90" + (state + 10).to_bytes(2, "big") + b"\xe0\xff\x0f\x90"
    code += (state + 10).to_bytes(2, "big") + b"\xef\xf0"
    code += b"".join(bytes((0xd0, register)) for register in reversed(saved)) + b"\x32"
    require(all(image.get(isr + i) == byte for i, byte in enumerate(code)) and
            cdb_address(debug, "L:XG$irq_fixture_timer1_isr$0$0") == isr + len(code) - 1 and
            "F:G$irq_fixture_timer1_isr$0_0$0({2}DF,SV:S),Z,0,0,1,9,0" in debug,
            "Timer1 ISR context/mask/RW0/RETI ABI changed")
    require(bytes(image.get(0x4b + i, 0xff) for i in range(3)) == b"\x02" + isr.to_bytes(2, "big") and
            all(image.get(vector) == 0x32 for vector in range(3, 0x4b, 8)), "CC2530 Timer1 vector changed")
    lengths = dict(CLOCK_INSTRUCTION_LENGTHS)
    for size, opcodes in ((1, "04 0e 14 32 33 4e 98 99 9a f4"),
                          (2, "24 34 7c 7e a2 ac ad c2 d2"), (3, "be")):
        lengths.update({opcode: size for opcode in bytes.fromhex(opcodes)})
    instructions = {}
    address = cdb_address(debug, "L:Firq_fixture_state$put16$0$0")
    end = cdb_address(debug, "L:XG$irq_fixture_poll$0$0") + 1
    while address < end:
        size = lengths.get(image.get(address))
        require(size is not None and address + size <= end, f"Unreviewed IRQ instruction at {address:04x}")
        instructions[address] = bytes(image[address + i] for i in range(size))
        address += size
    accesses = tuple(data for _, data, _ in peripheral_accesses(instructions))
    expected = tuple(bytes((0xe5, address)) for address in
                     (0xa8, 0xb8, 0x9a, 0xe4, 0xaf, 0xc0, 0xa9, 0xb9, 0xd8, 0xc6, 0x9e, 0xbe, 0xe2, 0xe3))
    expected += tuple(bytes.fromhex(text) for text in (
        "b5 e5 1c", "b5 e6 17", "b5 e7 12", "53 a8 7f", "53 b8 fd", "75 e4 00",
        "53 b8 fd", "e5 af", "e5 c0", "75 af 1f", "e5 a8", "43 a8 80", "43 b8 02",
        "75 e2 00", "75 e4 01", "75 e4 00", "53 a8 7f"))
    require(accesses == expected, "IRQ peripheral accesses/counter read order/RW0 write changed")
    xregs = [(data, instructions.get(pc + 3)) for pc, data in instructions.items()
             if data[0] == 0x90 and int.from_bytes(data[1:], "big") >= 0x1e00]
    require(xregs == [(b"\x90\x62\xa3", b"\xe0"), (b"\x90\x62\xa4", b"\xe0"),
                      (b"\x90\x1e\x10", b"\xe0"), (b"\x90\x1e\x11", b"\xe0"), (b"\x90\x1e\x12", b"\xe0")],
            "IRQ absolute reads are not the inactive-channel/GPIO-selection checks")
    release = symbols["_irq_fixture_release"]
    release_end = cdb_address(debug, "L:XG$irq_fixture_release$0$0")
    calls = [pc for pc, data in instructions.items() if release <= pc <= release_end and
             data == b"\x12" + symbols["_irq_restore"].to_bytes(2, "big")]
    require(len(calls) == 1, "IRQ outer restore caller is not unique")
    call = calls[0]
    tail = b"\x90" + (state + 14).to_bytes(2, "big") + b"\xe0\xf5\x82\x12"
    tail += symbols["_irq_restore"].to_bytes(2, "big") + b"\xaf\x82\x90"
    tail += (state + 17).to_bytes(2, "big") + b"\xef\xf0"
    require(all(image.get(call - 6 + i) == byte for i, byte in enumerate(tail)),
            "IRQ outer token/return continuation changed")
    wait = cdb_address(debug, "L:Firq_fixture_state$wait_begin$0$0")
    wait_calls = [pc for pc, data in instructions.items() if release <= pc <= release_end and
                  data[0] in (0x02, 0x12) and int.from_bytes(data[1:], "big") == wait]
    require(len(wait_calls) == 1 and wait_calls[0] > call + 10,
            "IRQ delivery deadline must begin after the observed restore continuation")
    begin = symbols["_irq_fixture_begin"]
    begin_end = cdb_address(debug, "L:XG$irq_fixture_begin$0$0")
    require(sum(data == b"\x12" + wait.to_bytes(2, "big") for pc, data in instructions.items()
                if begin <= pc <= begin_end) == 1, "IRQ arm must construct its own deadline")
    main_calls = []
    pc, main_end = symbols["_main"], cdb_address(debug, "L:XG$main$0$0") + 1
    while pc < main_end:
        size = lengths.get(image.get(pc))
        require(size is not None and pc + size <= main_end, "Unreviewed IRQ main instruction")
        if image[pc] == 0x12:
            main_calls.append(int.from_bytes(bytes(image[pc + i] for i in (1, 2)), "big"))
        pc += size
    names = ("initialize", "fault_stop", "before_stop", "begin", "fault_stop", "armed_stop", "start",
             "poll", "fault_stop", "pending_stop", "inner", "fault_stop", "inner_stop", "release",
             "poll", "fault_stop", "ready_stop")
    require(main_calls == [symbols["_irq_fixture_" + name] for name in names],
            "IRQ main arm/checkpoint/delivery call sequence changed")
    storage = {name: cdb_local(debug, "Firq_fixture_state$" + name, "({4}SL:U),F,0,0")
               for name in ("start", "previous", "deadline")}
    storage["polls"] = cdb_local(debug, "Firq_fixture_state$polls", "({2}SI:U),F,0,0")
    ordinary = {a for low, high in xdata_ranges(symbols) for a in range(low, high)}
    occupied = set(range(state, state + 64))
    for name, address in storage.items():
        region = set(range(address, address + (2 if name == "polls" else 4)))
        require(region <= ordinary and not occupied.intersection(region),
                "IRQ deadline storage overlaps fixture or allocation boundary")
        occupied.update(region)
    setup = bytearray.fromhex(
        "90 00 4c e4 f0 a3 f0 12 0a ca ac 82 ad 83 ae f0 ff "
        "90 00 40 ec f0 ed a3 f0 ee a3 f0 ef a3 f0 "
        "90 00 44 ec f0 ed a3 f0 ee a3 f0 ef a3 f0 "
        "90 00 62 e4 f0 74 04 a3 f0 e4 a3 f0 a3 f0 "
        "90 00 66 74 48 f0 74 00 a3 f0 e4 a3 f0 8c 82 8d 83 8e f0 ef "
        "12 0b 22 af 82 90 00 13 ef f0 e0 60 0b 75 82 04 12 03 bd "
        "75 82 00 80 03 75 82 01 22")
    for offset, address in (
        (0, storage["polls"]), (7, symbols["_timebase_read_awake_ticks24"]),
        (17, storage["start"]), (31, storage["previous"]),
        (45, symbols["_timebase_deadline_after_PARM_2"]),
        (59, symbols["_timebase_deadline_after_PARM_3"]),
        (79, symbols["_timebase_deadline_after"]), (84, state + 19),
        (95, cdb_address(debug, "L:Firq_fixture_state$fault$0$0")),
    ):
        setup[offset + 1:offset + 3] = address.to_bytes(2, "big")
    setup[63], setup[66] = storage["deadline"] & 255, storage["deadline"] >> 8
    require(cdb_address(debug, "L:XFirq_fixture_state$wait_begin$0$0") == wait + len(setup) - 1 and
            all(image.get(wait + i) == byte for i, byte in enumerate(setup)),
            "IRQ actual deadline setup/storage/call ABI changed")
    return {"isr_entry": isr, "isr_reti": isr + len(code) - 1, "isr_size": len(code),
            "restore": symbols["_irq_restore"], "restore_call": call, "restore_return": call + 3,
            "resume_addresses": [symbols["_irq_restore"] + 9, symbols["_irq_restore"] + 12,
                                 call + 3, call + 5, call + 8, call + 9, call + 10],
            "timer_start": next(pc for pc, data in instructions.items() if data == b"\x75\xe4\x01"),
            "timer_reset": next(pc for pc, data in instructions.items() if data == b"\x75\xe2\0"),
            "acknowledge": next(pc for pc, data in instructions.items() if data == b"\x75\xaf\x1f"),
            "start_address": storage["start"], "deadline_address": storage["deadline"],
            "wait_begin": wait, "wait_begin_size": len(setup)}
