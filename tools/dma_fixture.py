# SPDX-License-Identifier: BSD-3-Clause
"""Strict DMA board ABI, relocated original-driver proof and read-only hold proof."""
import hashlib
import re

from radio_fifo_fixture import FIFO_LENGTHS, instructions
from verify_firmware import (
    CLOCK_INSTRUCTION_LENGTHS, cdb_address, cdb_local, peripheral_accesses, require,
    verify_clock_code, verify_deadline_helper, verify_timebase_reader, xdata_ranges,
)

SIZE = 116
CHECKPOINTS = tuple("_dma_fixture_" + name for name in ("before", "ready", "fault"))
DMA_HASH = "d5cc411d99fad73f654803263414deb629f988d95cc21dee9bd0f07ac54d9919"
LENGTHS = FIFO_LENGTHS | CLOCK_INSTRUCTION_LENGTHS | {
    0: 1, 0x48: 1, 0x4a: 1, 0x4c: 1, 0x7a: 2, 0x7c: 2, 0x92: 2, 0xaa: 2, 0xbd: 3,
    0x04: 1, 0x0a: 1, 0x14: 1, 0x39: 1, 0x7b: 2, 0x9b: 1, 0xf4: 1,
    0x2f: 1, 0x5f: 1, 0x62: 2, 0xbb: 3,
}
DMA_FIELDS = ("elapsed_ticks", "polls", "timebase_status", "actions", "complete", "verified",
              "arm", "request", "irq", "ircon", "cfg0_low", "cfg0_high", "cfg1_low", "cfg1_high", "sample_valid")
STATE_FIELDS = (
    ("signature", 4), ("version", 1), ("size", 1), ("phase", 1), ("reason", 1), ("stage", 1), ("completed", 1),
    ("clock_result", 1), ("dma_result", 1), ("length", 1), ("checked", 1), ("mismatch_buffer", 1),
    ("mismatch_index", 1), ("actual", 1), ("expected", 1), ("source", 2), ("destination", 2),
    ("timeout", 3), ("limit", 2), ("clock", 19), ("dma", 19), ("command", 1), ("status", 1),
    ("sleep", 1), ("enables", 3), ("controller", 8), ("sample_valid", 1), ("fault_latch", 1),
    ("descriptor", 8), ("initial_sleep", 1), ("initial_ircon", 1), ("initial_cfg1", 2),
    ("initial_flags", 9), ("flags", 9), ("guards", 2), ("reserved", 3),
)
IMAGE_HASHES = {
    0x140: "d3085c2c616b2d0a4907bf22c1652cf7a6f94de3f3f613bee35931766f16359d",
    0x168: "74addb4a8af51eb3076aa767c9660e7b16b77c3f537cf34ddfb2c2ee5a15727e",
}
CLOCK_PROFILES = {
    (731, 25, 8844): ("45662edf1782d56f2b3b85b77b7d16253e43c809ea536ec63fa6ee8c34f9e1aa",
                      "7f361afab0a46b62821c363b00140e3eb20e944ec31ec53f3a357437cae7707d", 8),
    (771, 25, 8884): ("794774c95be6c7f2995e217e33e7c6f877480628562448d87fba17fe05d3e179",
                      "037359a09ff3850faeeed4d6eac67ebb386209e958989eea74ffc5df09a4eb47", 8),
}
DIRECT = {0x25, 0x35, 0x43, 0x45, 0x53, 0x75, 0x95, 0xb5, 0xc0, 0xd0, 0xe5, 0xf5}
DIRECT |= set(range(0x88, 0x90)) | set(range(0xa8, 0xb0))
EXTERNAL = {
    "_timebase_read_awake_ticks24": 0x62, "_timebase_deadline_after": 0xba, "_timebase_expired": 0x14e,
}
PARAMETERS = {"_timebase_deadline_after_PARM_2": (3, 4), "_timebase_deadline_after_PARM_3": (7, 3),
              "_timebase_expired_PARM_2": (14, 4), "_timebase_expired_PARM_3": (18, 3)}


def verify_dma_relocated(image, symbols, debug):
    start = cdb_address(debug, "L:Fdma$ordinary$0$0")
    end = cdb_address(debug, "L:XG$dma_copy_init$0$0") + 1
    require(end - start == 2867, "DMA original module extent changed")
    code = instructions(image, start, end, LENGTHS)
    data = bytearray(image[a] for a in range(start, end))
    xb = symbols["_dma_descriptor"]
    db = cdb_local(debug, "Ldma.poll$sloc0", "({2}DX,ST__00000000:S),E,0,0")
    ordinary = {a for lo, hi in xdata_ranges(symbols) for a in range(lo, hi)}
    require(symbols["_dma_reserved_end"] == xb + 66 and set(range(xb, xb + 67)) <= ordinary and
            8 <= db and db + 27 <= symbols["s_SSEG"] < 128, "DMA private allocation invalid")
    bits = data[0x31]
    require(bits < symbols["l_BSEG"] < 128, "DMA bit scratch is not allocated")
    for pc, b in code.items():
        offset, op = pc - start, b[0]
        if op in (2, 0x12, 0x90):
            value = int.from_bytes(b[1:], "big")
            if op != 0x90:
                if start <= value < end:
                    target = value - start + 0x1f6
                else:
                    names = [n for n in EXTERNAL if symbols[n] == value]
                    require(len(names) == 1, "Unexpected DMA external control flow")
                    target = EXTERNAL[names[0]]
            elif xb <= value < xb + 67:
                target = value - xb + 0x19
            elif value == 0:
                target = 0
            else:
                matches = [old + value - symbols[n] for n, (old, size) in PARAMETERS.items()
                           if symbols[n] <= value < symbols[n] + size]
                require(len(matches) == 1 and value in ordinary, "Unexpected DMA absolute data operand")
                target = matches[0]
            data[offset + 1:offset + 3] = target.to_bytes(2, "big")
        for pos in (1, 2) if op == 0x85 else (1,) if op in DIRECT else ():
            value = b[pos]
            if db <= value < db + 27: data[offset + pos] = value - db + 0x21
            else: require(value < 8 or value >= 128, "Unexpected DMA direct scratch operand")
        if op in (0x20, 0x30, 0xa2, 0x92, 0xc2, 0xd2) and b[1] < 128:
            require(b[1] == bits, "Unexpected DMA bit scratch")
            data[offset + 1] = 1
    for low, high, actual, original in (
        (0x214, 0x217, xb + 31, 0x38), (0x427, 0x42a, xb + 32, 0x39),
        (0x58a, 0x58d, xb + 66, 0x5b), (0x590, 0x593, symbols["__gptrput_PARM_2"], 0xbd),
        (0x817, 0x81b, xb + 46, 0x47), (0x979, 0x97c, xb + 54, 0x4f),
        (0xa06, 0xa08, xb, 0x19),
    ):
        require(data[low] == actual & 255 and data[high] == actual >> 8,
                "DMA split pointer does not match CDB allocation")
        data[low], data[high] = original & 255, original >> 8
    require(hashlib.sha256(data).hexdigest() == DMA_HASH,
            "Relocated DMA differs from the published original instructions")
    arm = cdb_address(debug, "L:Fdma$arm0$0$0")
    require(bytes(image[a] for a in range(arm, arm + 13)) == b"\x75\xd6\x01" + bytes(9) + b"\x22" and
            cdb_address(debug, "L:XFdma$arm0$0$0") == arm + 13,
            "DMA nine-NOP leaf/extent changed")
    require(code[start + 0xa4a] == b"\x12" + arm.to_bytes(2, "big") and
            code[start + 0xa86] == b"\x75\xd7\x01", "DMA post-arm/request caller changed")
    return code, dict(module_start=start, module_end=end, xdata_start=xb, arm=arm, arm_ret=arm + 12,
                      arm_call=start + 0xa4a, armed_poll_call=start + 0xa76,
                      request=start + 0xa86, expiry_call=start + 0x445)


def verify_expiry(image, symbols, debug):
    start = symbols["_timebase_expired"]
    end = cdb_address(debug, "L:XG$timebase_expired$0$0") + 1
    require(end - start == 168 and cdb_address(debug, "L:G$timebase_expired$0$0") == start,
            "Expiry helper explicit extent changed")
    data = bytearray(image[a] for a in range(start, end))
    code = instructions(image, start, end, LENGTHS)
    local = {n: cdb_local(debug, "Ltimebase.timebase_expired$" + n, decl + ",F,0,0")
             for n, decl in (("now", "({4}SL:U)"), ("deadline", "({4}SL:U)"), ("expired", "({3}DG,:S)"))}
    scratch = cdb_address(debug, "L:Ltimebase.timebase_expired$sloc0$0_1$0")
    require(symbols["s_OSEG"] <= scratch and scratch + 3 <= symbols["s_OSEG"] + symbols["l_OSEG"],
            "Expiry overlay is not allocated")
    for pc, b in code.items():
        off = pc - start
        if b[0] == 0x90:
            value = int.from_bytes(b[1:], "big")
            names = [n for n in local if local[n] == value]
            require(len(names) == 1, "Expiry data operand changed")
            data[off + 1:off + 3] = {"now": 21, "deadline": 14, "expired": 18}[names[0]].to_bytes(2, "big")
        if b[0] == 0x12:
            require(int.from_bytes(b[1:], "big") == symbols["__gptrput"], "Expiry helper call changed")
            data[off + 1:off + 3] = b"\x14\x53"
        for pos in (1, 2) if b[0] == 0x85 else (1,) if b[0] in DIRECT else ():
            if scratch <= b[pos] < scratch + 3: data[off + pos] = b[pos] - scratch + 13
        if b[0] in (0x92, 0xa2) and b[1] < 128:
            require(b[1] == symbols["s_BSEG"] == 0 and symbols["l_BSEG"] == 3,
                    "Expiry bit scratch allocation changed")
    require(hashlib.sha256(data).hexdigest() == "55b5fdcab9ceac901915431d4207921939e2ab83543e7b5c690618a7ac5e3834",
            "Expiry helper differs from original complete instructions")
    require(bytes(image[a] for a in range(end - 4, end)) == b"\x75\x82\0\x22", "Expiry success RET changed")
    return start, end - 1, local


def layout_fields(debug, module, identifier, fields):
    records = re.findall(rf"^T:F{module}\$\w+\[(.*)\]$", debug, re.MULTILINE)
    records = [r for r in records if f"S:S${identifier}$" in r]
    expected, offset = [], 0
    for name, size in fields: expected.append((offset, name, size)); offset += size
    require(records, "Missing DMA fixture type ABI")
    for r in records:
        actual = re.findall(r"\(\{(\d+)\}S:S\$([^$]+)\$0_0\$0\(\{(\d+)\}", r)
        require([(int(a), b, int(c)) for a, b, c in actual] == expected, "DMA fixture field ABI changed")


def verify_fixture(image, symbols, debug):
    require(all(n in symbols for n in CHECKPOINTS +
                tuple("_dma_fixture_"+n for n in ("state", "a", "b", "work")) +
                ("_dma_copy_init", "_dma_descriptor", "_dma_fault", "_dma_reserved_end", "__gptrput_PARM_2")),
            "Missing DMA fixture/driver symbol")
    require(symbols["_dma_fault"] == symbols["_dma_descriptor"]+8, "DMA fault latch allocation changed")
    before, ready, fault = (symbols[n] for n in CHECKPOINTS)
    program = bytes(image.get(i, 255) for i in range(max(image) + 1))
    require(before in IMAGE_HASHES and hashlib.sha256(program).hexdigest() == IMAGE_HASHES[before],
            "DMA fixture complete CODE/constants changed")
    require(ready == before + 2 and fault == before + 4 and
            program[before:before + 7] == b"\0\x22\0\x22\0\x80\xfd", "DMA fixture markers changed")
    for module in ("dma_fixture", "dma_fixture_state"):
        layout_fields(debug, module, "fault_latch", STATE_FIELDS)
        layout_fields(debug, module, "cfg0_low", tuple(zip(DMA_FIELDS, (4, 2) + (1,) * 13)))
        layout_fields(debug, module, "before", (("before", 1), ("data", 16), ("after", 1)))
    layout_fields(debug, "dma", "verified", tuple(zip(DMA_FIELDS, (4, 2) + (1,) * 13)))
    code, proof = verify_dma_relocated(image, symbols, debug)
    state, a, b, work = (symbols["_dma_fixture_" + n] for n in ("state", "a", "b", "work"))
    require((state, a, b, work, symbols["l_XSEG"], symbols["s_SSEG"]) == (0x92, 0x106, 0x118, 0x12a, 334, 0x3c)
            and symbols["l_PSEG"] == symbols["l_XISEG"] == symbols["l_XABS"] == 0,
            "DMA fixture/private allocation layout changed")
    require(proof["xdata_start"] + 67 == state and symbols["__gptrput_PARM_2"] == 0x14d,
            "DMA caller/helper allocation exclusion changed")
    for name, size in (("state", SIZE), ("a", 18), ("b", 18), ("work", 19)):
        require(cdb_address(debug, f"L:G$dma_fixture_{name}$0_0$0") == symbols["_dma_fixture_" + name] and
                re.findall(rf"^S:G\$dma_fixture_{name}\$0_0\$0\(\{{(\d+)\}}ST[^)]+\),F,0,0$",
                           debug, re.MULTILINE) == [str(size), str(size)], "DMA global caller ABI changed")
    verify_timebase_reader(image, symbols, debug, state, SIZE)
    verify_deadline_helper(image, symbols, debug)
    verify_clock_code(image, symbols, debug)
    helper, stop, locals_ = verify_expiry(image, symbols, debug)
    require(code[proof["expiry_call"]] == b"\x12" + helper.to_bytes(2, "big"), "DMA expiry call changed")
    main_end = cdb_address(debug, "L:XG$main$0$0") + 1
    caller = instructions(image, symbols["_main"], main_end, LENGTHS)
    fixture = instructions(image, cdb_address(debug, "L:Fdma_fixture_state$put16$0$0"), main_end, LENGTHS)
    sfrs = (0xc6, 0x9e, 0xbe, 0xa8, 0xb8, 0x9a, 0xd6, 0xd7, 0xd1, 0xc0, 0xd4, 0xd5, 0xd2, 0xd3,
            0xa9, 0xb9, 0x88, 0x98, 0x9b, 0xe9, 0x91, 0xe8, 0xbf)
    require(tuple(b for _, b, _ in peripheral_accesses(fixture)) ==
            tuple(bytes((0xe5, r)) for r in sfrs), "DMA fixture expanded its passive SFR boundary")
    for pc, op in fixture.items():
        if op[0] == 0x90 and int.from_bytes(op[1:], "big") >= 0x1e00:
            require(int.from_bytes(op[1:], "big") in (0x1e18, 0x1e19) and fixture[pc+3] == b"\xe0",
                    "DMA fixture added peripheral/status/alias access")
    calls = [pc for pc, op in caller.items() if op == b"\x12" + symbols["_dma_copy_init"].to_bytes(2, "big")]
    require(len(calls) == 1, "DMA genuine inlined fixture caller changed")
    private = {name: cdb_local(debug, "Ldma.dma_copy_init$" + name, decl + ",F,0,0")
               for name, decl in (("source", "({2}SI:U)"), ("destination", "({2}SI:U)"),
                                  ("length", "({1}SC:U)"), ("timeout", "({4}SL:U)"),
                                  ("limit", "({2}SI:U)"), ("d", "({2}DX,ST__00000000:S)"),
                                  ("w", "({19}ST__00000001:S)"))}
    poll_expired = cdb_local(debug, "Ldma.poll$expired", "({1}:S),F,0,0")
    require(bytes(image[a] for a in range(proof["expiry_call"]-10, proof["expiry_call"])) ==
            bytes.fromhex("c0 07 c0 06 c0 05 c0 03 c0 02") and
            code[proof["module_start"]+0x3ec] == b"\xc0\x03" and
            code[proof["module_start"]+0x3ee] == b"\xc0\x04",
            "DMA expiry seven-register save frame changed")
    return dict(proof, state=state, a=a, b=b, work=work, checkpoints=[before, ready, fault],
                address=stop, function_start=helper, function_size=168, helper_locals=locals_,
                private=private, poll_expired=poll_expired, fixture_return=calls[0] + 3,
                return_address=proof["expiry_call"] + 3, dma_return=proof["armed_poll_call"] + 3,
                deadline_sp=symbols["s_SSEG"]+12)


def inspect_expiry(proof, read, sp, dpl, dps, controller, record):
    """Only the proved, not-yet-requested TRIG0 checkpoint permits payload reads."""
    require(dpl == dps == 0 and sp == proof["deadline_sp"] < 0x80, "DMA expiry return ABI/stack invalid")
    require(record["phase"] == 2 and record["stage"] == 3 and record["completed"] == 0 and
            record["dma_result"] == 255 and record["length"] == 16 and not record["checked"] and
            record["clock_result"] == 0 and record["command"] == record["status"] == 0x88,
            "DMA hold is not the first XOSC32 copy")
    p = proof["private"]
    for name, expected in (
        ("source", (proof["b"]+1).to_bytes(2, "little")),
        ("destination", (proof["a"]+1).to_bytes(2, "little")), ("length", b"\x10"),
        ("timeout", b"\0\x04\0\0"), ("limit", b"\0\x10"), ("d", proof["work"].to_bytes(2, "little")),
    ):
        require(read(p[name], len(expected)) == expected, "DMA live argument mismatch: " + name)
    require(record["source"] == proof["b"]+1 and record["destination"] == proof["a"]+1,
            "DMA serialized route differs from live arguments")
    w = read(p["w"], 19)
    start, previous, deadline = (int.from_bytes(w[i:i+4], "little") for i in (0, 4, 8))
    now = int.from_bytes(read(proof["helper_locals"]["now"], 4), "little")
    elapsed = (now-start) & 0xffffff
    require(max(start, previous, now) <= 0xffffff and deadline == (start+1024)&0xffffff and
            (previous-start)&0xffffff <= elapsed < 1024 and
            w[12:] == b"\0\x10\x88" + proof["xdata_start"].to_bytes(2, "little") +
            bytes(record["initial_cfg1"]), "DMA start/previous/deadline/clock context invalid")
    require(read(proof["helper_locals"]["deadline"], 4) == deadline.to_bytes(4, "little") and
            read(proof["helper_locals"]["expired"], 3) == proof["poll_expired"].to_bytes(2, "little")+b"\0" and
            read(proof["poll_expired"], 1) == b"\0", "DMA genuine helper arguments/result changed or already expired")
    expected_controller = (b"\x01\0\0" + bytes([record["initial_ircon"]]) +
                           proof["xdata_start"].to_bytes(2, "little") + bytes(record["initial_cfg1"]))
    require(controller == expected_controller and not record["initial_ircon"] & 1,
            "DMA actual pre-request control/IRQ/config state changed")
    require(read(proof["work"], 19) == elapsed.to_bytes(4, "little") +
            b"\x03\0\0\x03\0\0" + expected_controller + b"\x01",
            "DMA configured/armed third-poll diagnostics invalid")
    descriptor = ((proof["b"]+1).to_bytes(2, "big") + (proof["a"]+1).to_bytes(2, "big") +
                  b"\0\x10\x20\x51")
    require(read(proof["xdata_start"], 8) == descriptor, "DMA persistent descriptor changed")
    expected_stack = (proof["fixture_return"].to_bytes(2, "little") + proof["dma_return"].to_bytes(2, "little") +
                      (proof["work"]+7).to_bytes(2, "little") +
                      (proof["work"]+6).to_bytes(2, "big") +
                      bytes((p["w"] >> 8, deadline >> 16, p["w"] & 255)) +
                      proof["return_address"].to_bytes(2, "little"))
    stack = read(0x1f00+sp-12, 13)
    require(stack == expected_stack, "DMA complete nested caller/saved-register frame mismatch")
    a, b = expected_buffers(3, 0, 0)
    require(read(proof["a"], 18) == a and read(proof["b"], 18) == b,
            "DMA pre-request source/destination/tail/guard mismatch")
    return {"start": start, "previous": previous, "sampled_now": now, "deadline": deadline,
            "stack_hex": stack.hex(), "descriptor_hex": descriptor.hex(),
            "actions": 3, "requested": False, "verified": False}


def decode(data, *, allow_running=False):
    require(isinstance(data, bytes) and len(data) == SIZE and data[:6] == b"M2DM\x01\x74",
            "DMA fixture signature/version/size mismatch")
    require(data[111:113] == b"\x69\x96" and data[113:] == bytes(3), "DMA fixture guards/reserved changed")
    r = {}; offset = 0
    arrays = {"enables", "controller", "descriptor", "initial_cfg1", "initial_flags", "flags"}
    for name, size in STATE_FIELDS:
        raw = data[offset:offset + size]; offset += size
        if name in ("signature", "version", "size", "guards", "reserved"): continue
        r[name] = list(raw) if name in arrays else raw if name in ("clock", "dma") else int.from_bytes(raw, "little")
    require(r["phase"] in (1, 2, 3, 4) and r["stage"] <= 4 and r["reason"] <= 6 and
            (r["phase"] != 2 or allow_running) and (r["phase"] == 4) == bool(r["reason"]),
            "DMA fixture phase/reason/stage invalid")
    require(r["timeout"] == 1024 and r["limit"] == 4096 and r["length"] <= 16 and r["checked"] <= 36 and
            r["clock_result"] <= 9 and (r["dma_result"] <= 11 or r["dma_result"] == 255) and
            r["sample_valid"] <= 1 and r["fault_latch"] <= 11, "DMA fixture bounds/result invalid")
    def wait(raw):
        return {"elapsed_ticks": int.from_bytes(raw[:4], "little"), "polls": int.from_bytes(raw[4:6], "little"),
                "timebase_status": raw[6]}
    raw_dma, raw_clock = r["dma"], r["clock"]
    r["dma"] = wait(raw_dma) | dict(zip(DMA_FIELDS[3:], raw_dma[7:]))
    r["clock"] = {"request": wait(raw_clock), "rollback": wait(raw_clock[7:]),
                  **dict(zip(("saved_command", "requested_command", "observed_command", "observed_status",
                              "rollback_result"), raw_clock[14:]))}
    d, c = r["dma"], r["clock"]
    for w in (d, c["request"], c["rollback"]):
        require(w["elapsed_ticks"] <= 0xffffff and w["polls"] <= 4096 and w["timebase_status"] <= 2,
                "DMA fixture diagnostic bounds invalid")
    require(d["actions"] in (0, 1, 3, 7, 15) and d["complete"] <= 1 and d["verified"] <= 1 and
            d["sample_valid"] <= 1, "DMA diagnostic shape invalid")
    require(not d["complete"] or d["actions"] & 4, "DMA completion predates its request")
    require(not d["verified"] or (d["complete"] and d["actions"] == 15 and r["dma_result"] == 0),
            "DMA verification contradicts actions/result")
    if r["phase"] == 1:
        require(r["stage"] == r["completed"] == r["length"] == r["checked"] == 0 and
                r["clock_result"] == 8 and raw_clock == bytes(18) + b"\x08" and raw_dma == bytes(19) and
                r["dma_result"] == 255 and r["command"] == r["status"] == 0xc9 and
                r["source"] == r["destination"] == r["fault_latch"] == 0 and
                r["descriptor"] == [0]*8 and r["sample_valid"] == 1 and r["enables"] == [0]*3 and
                r["sleep"] == r["initial_sleep"] and r["sleep"] & 7 == 4 and
                r["controller"][:3] == [0]*3 and r["controller"][4:6] == [0, 0] and
                r["controller"][3] == r["initial_ircon"] and not r["initial_ircon"] & 1 and
                r["controller"][6:] == r["initial_cfg1"] and r["flags"] == r["initial_flags"] and
                r["mismatch_buffer"] == r["mismatch_index"] == 255 and r["actual"] == r["expected"] == 0,
                "DMA initial record invalid")
    if r["phase"] == 3:
        target = 0x88 if r["stage"] in (2, 3) else 0xc9
        require(r["command"] == r["status"] == target and r["sleep"] == r["initial_sleep"] and
                r["sleep"] & 7 == 4 and r["enables"] == [0, 0, 0] and r["sample_valid"] == 1 and
                not r["fault_latch"] and r["controller"][:3] == [0, 0, 0] and
                r["controller"][3] == r["initial_ircon"] and not r["initial_ircon"] & 1 and
                r["controller"][6:] == r["initial_cfg1"] and r["flags"] == r["initial_flags"],
                "DMA READY controller/clock/IRQ preservation failed")
        require(r["clock_result"] == 0 and c["rollback_result"] == 8 and
                raw_clock[7:14] == bytes(7) and c["requested_command"] == c["observed_command"] ==
                c["observed_status"] == target and c["request"]["timebase_status"] == 0,
                "DMA READY clock result invalid")
        require(r["mismatch_buffer"] == r["mismatch_index"] == 255 and r["actual"] == r["expected"] == 0,
                "DMA READY byte mismatch")
        if r["stage"] in (1, 3):
            n = (r["completed"] & 15) + 1 if r["stage"] == 1 else 16
            require(r["dma_result"] == 0 and r["length"] == n and r["checked"] == 36 and
                    d["actions"] == 15 and d["complete"] == d["verified"] == d["sample_valid"] == 1 and
                    d["timebase_status"] == 0 and 5 <= d["polls"] <= 4096 and d["elapsed_ticks"] < 1024 and
                    list(raw_dma[10:18]) == r["controller"], "DMA READY transfer not confirmed")
            expected = r["source"].to_bytes(2, "big") + r["destination"].to_bytes(2, "big") + bytes((0, n, 0x20, 0x51))
            require(bytes(r["descriptor"]) == expected, "DMA READY descriptor mismatch")
        else:
            require(r["dma_result"] == 255 and raw_dma == bytes(19) and r["length"] == r["checked"] == 0,
                    "DMA ran during clock-only stage")
    return r


def expected_buffers(stage, completed, length):
    pattern = bytes(((completed + (0x31 if stage == 1 else 0x97)) & 255) ^ i for i in range(16))
    dest = pattern[:length] + bytes(b ^ 255 for b in pattern[length:])
    a, b = (pattern, dest) if stage == 1 else (dest, pattern)
    return b"\x69" + a + b"\x96", b"\x69" + b + b"\x96"
