# SPDX-License-Identifier: BSD-3-Clause
"""AES board fixture wire ABI and original-driver relocation/context proof."""
import hashlib
from pathlib import Path
import re
import subprocess

from dma_fixture import DIRECT, LENGTHS as DMA_LENGTHS, layout_fields
from radio_fifo_fixture import instructions
from verify_firmware import (
    cdb_address, cdb_local, code_bytes, peripheral_accesses, require, verify_clock_code,
    verify_deadline_helper, verify_timebase_reader, xdata_ranges,
)

SIZE, TIMEOUT, LIMIT = 64, 32768, 4096
CHECKPOINTS = tuple("_aes_fixture_" + n for n in ("before", "ready", "fault"))
LENGTHS = DMA_LENGTHS | {0x05: 2, 0x0d: 1, 0x2b: 1, 0x3c: 1, 0x49: 1, 0x52: 2, 0x5a: 1, 0x5f: 1, 0xd5: 3, 0xa4: 1}
AES_HASH = "b80e064f5fb405c8a5d2c28722e18b51f25c5d99d90dd8aaec6da4332103befd"
HASHES = {0x140: (12725, "ce8bf5e85291c93901432612824ab428f0350baaa674d4d2939a6531f3313b92"),
          0x168: (12765, "0ee3e0946685c6fac10fbdd589ce75a6d2fbc14d4bad07e632faf0cd4f2d4997")}
FIELDS = (
    ("signature", 4), ("version", 1), ("size", 1), ("phase", 1), ("reason", 1), ("stage", 1),
    ("completed", 1), ("vector", 1), ("spaces", 1), ("result", 1), ("kind", 1), ("checked", 1),
    ("mismatch_buffer", 1), ("mismatch_index", 1), ("actual", 1), ("expected", 1), ("fault_latch", 1),
    ("timeout", 3), ("limit", 2), ("diagnostic", 19), ("command", 1), ("status", 1), ("sleep", 1),
    ("enables", 3), ("cpu_valid", 1), ("initial_flags", 8), ("initial_ircon", 1), ("initial_enc", 1),
    ("initial_sleep", 1), ("guards", 2),
)
AES_FIELDS = ("elapsed_ticks", "polls", "timebase_status", "phase", "submitted", "input_complete",
              "output_drained", "published", "configured", "arms", "ack_issued", "dma_acked",
              "enc_ack_issued", "enc_acked", "arm", "request", "irq", "ircon", "control", "enc_flags",
              "cfg0_low", "cfg0_high", "cfg1_low", "cfg1_high", "sample_valid")
PROJECTED = AES_FIELDS[:8] + ("arms", "ack_issued", "dma_acked", "enc_ack_issued", "enc_acked", "sample_valid", "configured")
CONTROLLER = (0xb3, 0x98, 0xd6, 0xd7, 0xd1, 0xc0, 0xd4, 0xd5, 0xd2, 0xd3)
FLAGS = (0xa9, 0xb9, 0x88, 0x9b, 0xe9, 0x91, 0xe8, 0xbf)
READS = (0xa8, 0xb8, 0x9a, 0xbe, 0xc6, 0x9e) + CONTROLLER
KATS = (
    ("000102030405060708090a0b0c0d0e0f", "00112233445566778899aabbccddeeff", "69c4e0d86a7b0430d8cdb78070b4c55a"),
    ("2b7e151628aed2a6abf7158809cf4f3c", "6bc1bee22e409f96e93d7e117393172a", "3ad77bb40d7a3660a89ecaf32466ef97"),
    ("2b7e151628aed2a6abf7158809cf4f3c", "ae2d8a571e03ac9c9eb76fac45af8e51", "f5d3d58503b9699de785895a96fdbaaf"),
    ("2b7e151628aed2a6abf7158809cf4f3c", "30c81c46a35ce411e5fbc1191a0a52ef", "43b1cd7f598ece23881b00e3ed030688"),
    ("2b7e151628aed2a6abf7158809cf4f3c", "f69f2445df4f9b17ad2b417be66c3710", "7b0c785e27e8ad3f8223207104725dd4"),
)


def public_vectors(reference):
    vectors = []
    for i in range(21):
        key = bytes.fromhex(KATS[i][0]) if i < 5 else bytes(((i-5)*19+j*31)&255 for j in range(16))
        data = bytes.fromhex(KATS[i][1]) if i < 5 else bytes((((i-5)*37+j*13)&255)^0xa7 for j in range(16))
        result = subprocess.run([str(Path(reference)), key.hex(), data.hex()], text=True, capture_output=True,
                                check=True, timeout=5)
        require(re.fullmatch("[0-9a-f]{32}\n", result.stdout), "Invalid host-only AES reference result")
        out = bytes.fromhex(result.stdout)
        require(i >= 5 or out.hex() == KATS[i][2], "Host reference failed primary AES KAT")
        vectors.append(key + data + out + b"\0")
    return tuple(vectors)


def verify_relocated(image, symbols, debug):
    start = cdb_address(debug, "L:Faes$pointer_location$0$0")
    end = cdb_address(debug, "L:XG$aes128_encrypt_block$0$0") + 1
    require(end-start == 5254, "Corrected AES module extent changed")
    code = instructions(image, start, end, LENGTHS)
    blob = bytearray(image[a] for a in range(start, end))
    xb, db = symbols["_aes_dma0"], cdb_address(debug, "L:Laes.source_range$sloc0$0_1$0")
    require(xb == 0x45 and symbols["_aes_reserved_end"] == 0xfa and db == 0x4e and
            symbols["__gptrput_PARM_2"] == 0x1af, "AES private/direct/helper layout changed")
    external = {"_timebase_read_awake_ticks24": 0x62, "_timebase_deadline_after": 0xba,
                "_timebase_expired": 0x14e, "__gptrget": 0x17e3}
    params = {"_timebase_deadline_after_PARM_2": (3, 4), "_timebase_deadline_after_PARM_3": (7, 3),
              "_timebase_expired_PARM_2": (14, 4), "_timebase_expired_PARM_3": (18, 3)}
    for pc, raw in code.items():
        offset, op = pc-start, raw[0]
        if op in (2, 0x12, 0x90):
            n = int.from_bytes(raw[1:], "big")
            if op != 0x90:
                matches = [old for name, old in external.items() if symbols[name] == n]
                require(start <= n < end or len(matches) == 1, "Unexpected AES external control flow")
                target = n-start+0x1f6 if start <= n < end else matches[0]
            elif xb <= n <= symbols["_aes_reserved_end"]:
                target = n-xb+0x19
            elif n == 0:
                target = 0
            else:
                matches = [old+n-symbols[name] for name, (old, size) in params.items() if symbols[name] <= n < symbols[name]+size]
                require(len(matches) == 1, "Unexpected AES absolute XDATA operand")
                target = matches[0]
            blob[offset+1:offset+3] = target.to_bytes(2, "big")
        for pos in (1, 2) if op == 0x85 else (1,) if op in DIRECT | {0x05, 0x42, 0x52, 0xd5} else ():
            if db <= raw[pos] < db+32: blob[offset+pos] = raw[pos]-db+0x21
            else: require(raw[pos] < 8 or raw[pos] >= 128, "Unexpected AES direct scratch operand")
    splits = ((0xcb, 0xfa, 0xce), (0xe3, 0x1af, 0x13f), (0x628, 0xe6, 0xba),
              (0xa3c, 0x45, 0x19), (0xac7, 0x4d, 0x21), (0xbc1, 0xb7, 0x8b),
              (0xc18, 0x6d, 0x41), (0xc3c, 0x8d, 0x61), (0xc5c, 0x7d, 0x51),
              (0xcd7, 0x6d, 0x41), (0xced, 0x4d, 0x21), (0xcfe, 0x9d, 0x71),
              (0xd31, 0x45, 0x19), (0xd53, 0x4d, 0x21), (0xe87, 0x7d, 0x51),
              (0xe8f, 0x8d, 0x61), (0x1450, 0x9d, 0x71))
    for offset, actual, original in splits:
        require(blob[offset] == actual & 255, "AES split pointer changed")
        blob[offset] = original & 255
    require(hashlib.sha256(blob).hexdigest() == AES_HASH, "AES differs from complete corrected 5254-byte module")
    return code, start


def expiry_proof(image, symbols, debug):
    start = symbols["_timebase_expired"]
    end = cdb_address(debug, "L:XG$timebase_expired$0$0") + 1
    require(end-start == 168, "AES expiry helper extent changed")
    data = bytearray(image[a] for a in range(start, end))
    code = instructions(image, start, end, LENGTHS)
    local = {n: cdb_local(debug, "Ltimebase.timebase_expired$"+n, decl+",F,0,0")
             for n, decl in (("now", "({4}SL:U)"), ("deadline", "({4}SL:U)"), ("expired", "({3}DG,:S)"))}
    scratch = cdb_address(debug, "L:Ltimebase.timebase_expired$sloc0$0_1$0")
    require(symbols["s_OSEG"] <= scratch and scratch+3 <= symbols["s_OSEG"]+symbols["l_OSEG"],
            "AES expiry overlay unallocated")
    for pc, raw in code.items():
        off = pc-start
        if raw[0] == 0x90:
            matches = [n for n in local if local[n] == int.from_bytes(raw[1:], "big")]
            require(len(matches) == 1, "AES expiry data operand changed")
            data[off+1:off+3] = {"now": 21, "deadline": 14, "expired": 18}[matches[0]].to_bytes(2, "big")
        if raw[0] == 0x12:
            require(int.from_bytes(raw[1:], "big") == symbols["__gptrput"], "AES expiry helper call changed")
            data[off+1:off+3] = b"\x14\x53"
        for pos in (1, 2) if raw[0] == 0x85 else (1,) if raw[0] in DIRECT else ():
            if scratch <= raw[pos] < scratch+3: data[off+pos] = raw[pos]-scratch+13
        if raw[0] in (0x92, 0xa2) and raw[1] < 128:
            require(raw[1] == 0 and symbols["l_BSEG"] == 5, "AES expiry bit allocation changed")
    require(hashlib.sha256(data).hexdigest() == "55b5fdcab9ceac901915431d4207921939e2ab83543e7b5c690618a7ac5e3834",
            "AES expiry differs from original complete helper")
    return end-1, local


def verify_fixture(image, symbols, debug):
    require(all(n in symbols for n in CHECKPOINTS), "Missing AES fixture checkpoint symbol")
    before, ready, fault = (symbols[n] for n in CHECKPOINTS)
    require(before in HASHES, "Unknown AES fixture checkpoint layout")
    size, digest = HASHES[before]
    require(hashlib.sha256(code_bytes(image, size)).hexdigest() == digest,
            "AES complete board CODE/constants/caller changed")
    require((ready, fault) == (before+2, before+4) and bytes(image[a] for a in range(before, before+7)) == b"\0\x22\0\x22\0\x80\xfd",
            "AES actual marker instructions changed")
    code, start = verify_relocated(image, symbols, debug)
    for name, address in (("SOC_ENCDI", 0xb1), ("SOC_ENCDO", 0xb2), ("SOC_ENCCS", 0xb3),
                          ("SOC_S0CON", 0x98), ("SOC_DMAARM", 0xd6), ("SOC_DMAREQ", 0xd7),
                          ("SOC_DMAIRQ", 0xd1), ("SOC_DMA0CFGL", 0xd4), ("SOC_DMA0CFGH", 0xd5),
                          ("SOC_DMA1CFGL", 0xd2), ("SOC_DMA1CFGH", 0xd3), ("SOC_IRCON", 0xc0),
                          ("AEF_IP0", 0xa9), ("AEF_IP1", 0xb9), ("AEF_TCON", 0x88),
                          ("AEF_S1CON", 0x9b), ("AEF_RFIRQF0", 0xe9), ("AEF_RFIRQF1", 0x91), ("AEF_IRCON2", 0xe8)):
        require(symbols.get("_"+name) == address, "AES fixture SFR declaration changed: "+name)
    require(symbols["l_XSEG"] == 437 and symbols["s_XSEG"] == 0 and symbols["s_SSEG"] == 0x6e and
            symbols["l_PSEG"] == symbols["l_XISEG"] == symbols["l_XABS"] == 0, "AES board allocation changed")
    for name, address, length in (("dma0", 0x45, 8), ("dma1", 0x4d, 32), ("key", 0x6d, 16),
                                  ("iv", 0x7d, 16), ("input", 0x8d, 16), ("output", 0x9d, 16),
                                  ("fault", 0xad, 1), ("used", 0xae, 1), ("reserved_end", 0xfa, 1)):
        require(symbols.get("_aes_"+name) == cdb_address(debug, f"L:G$aes_{name}$0_0$0") == address,
                "AES private object map/CDB mismatch")
        sizes = re.findall(rf"^S:G\$aes_{name}\$[^(\n]+\(\{{(\d+)\}}", debug, re.MULTILINE)
        require(sizes and all(int(n) == length for n in sizes), "AES private object extent changed")
    objects = {"state": (0xfb, SIZE), "key": (0x13b, 16), "input": (0x14b, 16), "output": (0x15b, 18), "work": (0x16d, 29)}
    require(symbols["_aes_fixture_vectors"] == cdb_address(debug, "L:G$aes_fixture_vectors$0_0$0") and
            symbols["_aes_fixture_vectors"]+1029 <= size, "AES public CODE corpus location changed")
    ordinary = {a for lo, hi in xdata_ranges(symbols) for a in range(lo, hi)}
    for name, (address, length) in objects.items():
        require(symbols["_aes_fixture_"+name] == cdb_address(debug, f"L:G$aes_fixture_{name}$0_0$0") == address,
                "AES caller map/CDB mismatch")
        sizes = re.findall(rf"^S:G\$aes_fixture_{name}\$[^(\n]+\(\{{(\d+)\}}", debug, re.MULTILINE)
        require(sizes and all(int(n) == length for n in sizes) and
                set(range(address, address+length)) <= ordinary and address > symbols["_aes_reserved_end"] and
                not address <= symbols["__gptrput_PARM_2"] < address+length, "AES caller/helper exclusion changed")
    covered = set()
    for name, length in re.findall(r"^S:([^(\n]+)\(\{(\d+)\}[^\n]*\),F,0,0$", debug, re.MULTILINE):
        if not (name.startswith(("Ltimebase.", "Lclock.", "Laes.", "Faes$")) or
                re.match(r"G\$aes_(dma0|dma1|key|iv|input|output|fault|used|reserved_end)\$", name)): continue
        if not re.search(r"^L:"+re.escape(name)+":", debug, re.MULTILINE): continue
        address = cdb_address(debug, "L:"+name)
        block = set(range(address, address+int(length)))
        require(block <= set(range(0xfb)), "AES/clock/timebase private scratch escapes prefix")
        covered |= block
    require(covered == set(range(0xfb)), "AES/clock/timebase private-prefix accounting has a hole")
    for module in ("aes_fixture", "aes_fixture_state"):
        layout_fields(debug, module, "initial_enc", FIELDS)
        layout_fields(debug, module, "output_drained", tuple(zip(AES_FIELDS, (4, 2)+(1,)*23)))
    layout_fields(debug, "aes", "output_drained", tuple(zip(AES_FIELDS, (4, 2)+(1,)*23)))
    verify_timebase_reader(image, symbols, debug, 0xfb, SIZE)
    verify_deadline_helper(image, symbols, debug); verify_clock_code(image, symbols, debug)
    expiry, helper = expiry_proof(image, symbols, debug)
    private = {n: cdb_local(debug, "Laes.aes128_encrypt_block$"+n, decl+",F,0,0")
               for n, decl in (("key", "({3}DG,SC:U)"), ("input", "({3}DG,SC:U)"),
                               ("output", "({2}DX,SC:U)"), ("timeout", "({4}SL:U)"),
                               ("limit", "({2}SI:U)"), ("d", "({2}DX,ST__00000000:S)"))}
    sample = {n: cdb_local(debug, "Laes.sample$"+n, decl+",F,0,0")
              for n, decl in (("d", "({2}DX,ST__00000000:S)"), ("final", "({1}SC:U)"), ("expired", "({1}:S)"))}
    require(cdb_address(debug, "L:Faes$w$0_0$0") == 0xaf, "AES wait context allocation changed")
    main = instructions(image, symbols["_main"], cdb_address(debug, "L:XG$main$0$0")+1, LENGTHS)
    callers = [pc for pc, op in main.items() if op == b"\x12"+symbols["_aes128_encrypt_block"].to_bytes(2, "big")]
    require(len(callers) == 1, "AES genuine inlined fixture caller changed")
    arm_input, arm_output = start+0x1f6, start+0x203
    for arm, bit, caller in ((arm_input, 1, start+0xf24), (arm_output, 2, start+0xdc9)):
        require(bytes(image[a] for a in range(arm, arm+13)) == b"\x75\xd6"+bytes([bit])+bytes(9)+b"\x22" and
                code[caller] == b"\x12"+arm.to_bytes(2, "big"), "AES individual nine-NOP arm/caller changed")
    require(code[start+0xd5a] == b"\x8b\xd2" and code[start+0xff5] == b"\x89\xb3" and
            code[start+0x12e2] == code[start+0x13a6] == b"\xf5\x98",
            "AES configuration/start boundary changed")
    for offset, raw in ((0xf52, bytes.fromhex("c0 07 c0 06 c0 05 c0 04")),
                        (0x63c, bytes.fromhex("c0 07 c0 06 c0 03 c0 02")),
                        (0x13ca, bytes.fromhex("8e 82 8f 83"))):
        require(bytes(image[a] for a in range(start+offset, start+offset+len(raw))) == raw,
                "AES negative saved-register path changed")
    return dict(state=0xfb, key=0x13b, input=0x14b, output=0x15b, work=0x16d, descriptor0=0x45, descriptor1=0x4d,
                vectors=symbols["_aes_fixture_vectors"], checkpoints=[before, ready, fault], module_start=start,
                module_end=start+5254, arm_input=arm_input, arm_output=arm_output, arm_ret=arm_input+12,
                armed_sample_call=start+0xf5a, final_sample_call=start+0x13ce,
                expiry_call=start+0x644, expiry=expiry, helper=helper, sample=sample, private=private, wait=0xaf,
                fixture_return=callers[0]+3, final_gate=start+0x13a6+2,
                reader=symbols["_timebase_read_awake_ticks24"], pre_latch=symbols["_timebase_read_awake_ticks24"]+3,
                reader_call=start+0x5a6, aes_start=start+0xff5,
                verify_buffers=cdb_address(debug, "L:Faes_fixture_state$verify_buffers$0$0"))


def unpack_aes(data):
    require(isinstance(data, bytes) and len(data) == 29, "AES native diagnostic size mismatch")
    result, offset = {}, 0
    for name, size in zip(AES_FIELDS, (4, 2)+(1,)*23):
        result[name] = int.from_bytes(data[offset:offset+size], "little"); offset += size
    return result


def decode(data, *, allow_running=False):
    require(isinstance(data, bytes) and len(data) == SIZE and data[:6] == b"M2AE\x02\x40" and data[62:] == b"\x69\x96",
            "AES fixture signature/version/size/guard mismatch")
    result, offset = {}, 0
    for name, size in FIELDS:
        raw = data[offset:offset+size]; offset += size
        result[name] = list(raw) if name in ("enables", "initial_flags") else raw if name == "diagnostic" else int.from_bytes(raw, "little")
    r = result
    require(r["phase"] in (1, 2, 3, 4) and (r["phase"] != 2 or allow_running) and r["stage"] <= 4 and
            r["reason"] <= 6 and (r["phase"] == 4) == bool(r["reason"]), "AES phase/reason/stage invalid")
    require(r["timeout"] == TIMEOUT and r["limit"] == LIMIT and r["vector"] < 21 and r["spaces"] < 4 and
            r["kind"] <= 2 and (r["result"] <= 11 or r["result"] == 255) and r["checked"] <= 50 and
            r["cpu_valid"] <= 1 and r["fault_latch"] <= 11, "AES bounds/result/validity invalid")
    raw = r["diagnostic"]
    if r["kind"] == 2:
        d, offset = {}, 0
        for name, size in zip(PROJECTED, (4, 2)+(1,)*13):
            d[name] = int.from_bytes(raw[offset:offset+size], "little"); offset += size
        r["aes"] = d
        require(d["elapsed_ticks"] <= 0xffffff and d["polls"] <= LIMIT and d["timebase_status"] <= 2 and
                d["phase"] <= 4 and d["submitted"] in (0, 1, 3, 7) and not d["input_complete"] & ~d["submitted"] and
                d["output_drained"] <= 1 and d["published"] <= 1 and d["arms"] <= 4 and d["ack_issued"] <= 3 and
                not d["dma_acked"] & ~d["input_complete"] and d["enc_ack_issued"] <= 3 and
                d["enc_acked"] in (0, 1, 3, 7) and not d["enc_acked"] & ~d["dma_acked"] and
                bin(d["enc_acked"]).count("1") <= d["enc_ack_issued"] <= d["ack_issued"] and
                d["sample_valid"] <= 3 and d["configured"] in (0, 1, 3), "AES diagnostic shape invalid")
    if r["phase"] in (1, 3):
        require(r["cpu_valid"] == 1 and r["command"] == r["status"] == (0x88 if r["stage"] in (2, 3) else 0xc9) and
                r["enables"] == [0]*3 and r["sleep"] == r["initial_sleep"] and r["sleep"] & 7 == 4 and
                not r["initial_ircon"] & 1 and not r["initial_enc"] & 3 and not r["fault_latch"],
                "AES stable clock/IRQ state invalid")
        require(r["mismatch_buffer"] == r["mismatch_index"] == 255 and r["actual"] == r["expected"] == 0,
                "AES successful record contains mismatch")
    if r["phase"] == 1:
        require(not any(r[n] for n in ("stage", "completed", "vector", "spaces", "kind", "checked")) and
                r["result"] == 255 and raw == bytes(19), "AES initial record invalid")
    if r["phase"] == 3:
        require(r["result"] == 0 and r["kind"] == (2 if r["stage"] in (1, 3) else 1), "AES READY operation invalid")
        if r["kind"] == 2:
            require(r["vector"] == r["completed"] % 21 and r["spaces"] == ((r["completed"]//21)+(r["stage"] == 3))&3 and
                    r["checked"] == 50 and d["phase"] == 4 and d["submitted"] == d["input_complete"] == d["dma_acked"] == d["enc_acked"] == 7 and
                    d["output_drained"] == d["published"] == 1 and d["enc_ack_issued"] == 3 and
                    d["arms"] == 4 and d["ack_issued"] == d["configured"] == d["sample_valid"] == 3 and
                    not d["timebase_status"] and d["elapsed_ticks"] < TIMEOUT and d["polls"] >= 17,
                    "AES READY block not freshly confirmed/published")
        else:
            require(r["checked"] == 0 and raw[7:14] == bytes(7) and raw[18] == 8 and
                    raw[15:18] == bytes([r["command"]])*3 and not raw[6] and
                    int.from_bytes(raw[:4], "little") <= TIMEOUT and int.from_bytes(raw[4:6], "little") <= LIMIT,
                    "AES READY clock request/rollback invalid")
    r["diagnostic"] = list(raw)
    return r


def expected_buffers(vector, success):
    return vector[:32] + b"\x69" + (vector[32:48] if success else bytes(b ^ 255 for b in vector[32:48])) + b"\x96"


def inspect_context(p, read, sp, dpl, dps, controller, record, mode):
    require(mode in ("pre-key", "final") and dpl == dps == 0 and
            sp == (0x7b if mode == "pre-key" else 0x73), "AES negative CPU context changed")
    r = record
    require((r["phase"], r["stage"], r["completed"], r["vector"], r["spaces"], r["result"], r["kind"]) ==
            (2, 3, 0, 0, 1, 255, 2) and r["command"] == r["status"] == 0x88 and
            not r["checked"] and not r["fault_latch"], "AES hold is not the first XOSC invocation")
    for name, value in (
        ("key", p["vectors"].to_bytes(2, "little")+b"\x80"),
        ("input", p["input"].to_bytes(2, "little")+b"\0"),
        ("output", (p["output"]+1).to_bytes(2, "little")),
        ("d", p["work"].to_bytes(2, "little")),
        ("timeout", TIMEOUT.to_bytes(4, "little")), ("limit", LIMIT.to_bytes(2, "little")),
    ):
        require(read(p["private"][name], len(value)) == value, "AES live generic/XDATA argument changed: "+name)
    require(read(0xad, 2) == b"\0\x01" and read(p["sample"]["d"], 2) == p["work"].to_bytes(2, "little") and
            read(p["sample"]["final"], 1) == bytes([mode == "final"]), "AES owned history/sample mode changed")
    w = read(p["wait"], 22)
    start, previous, deadline = (int.from_bytes(w[i:i+4], "little") for i in (0, 4, 8))
    require(max(start, previous, deadline) <= 0xffffff and deadline == (start+TIMEOUT)&0xffffff and
            ((previous-start)&0xffffff) < TIMEOUT and
            w[12:] == LIMIT.to_bytes(2, "little")+bytes((0x88, r["initial_ircon"], r["initial_enc"], 0x40))+
            p["descriptor0"].to_bytes(2, "little")+p["descriptor1"].to_bytes(2, "little"),
            "AES start/previous/deadline/clock/history changed")
    control = bytes((0x48, r["initial_enc"], 3 if mode == "pre-key" else 0, 0, 0, r["initial_ircon"])) + \
              p["descriptor0"].to_bytes(2, "little")+p["descriptor1"].to_bytes(2, "little")
    require(controller == control and not r["initial_ircon"] & 1 and not r["initial_enc"] & 3,
            "AES actual owned control/IRQ/config state changed")
    source = 0x6d if mode == "pre-key" else 0x8d
    descriptors = source.to_bytes(2, "big")+b"\x70\xb1\0\x10\x1d\x41" + \
                  b"\x70\xb2\0\x9d\0\x10\x1e\x11"+bytes(24)
    require(read(p["descriptor0"], 40) == descriptors, "AES finite input/output descriptors changed")
    d = unpack_aes(read(p["work"], 29))
    elapsed = (previous-start)&0xffffff
    now = None
    if mode == "pre-key":
        now = int.from_bytes(read(p["helper"]["now"], 4), "little")
        elapsed = (now-start)&0xffffff
        require(now <= 0xffffff and ((previous-start)&0xffffff) <= elapsed < TIMEOUT and
                read(p["helper"]["deadline"], 4) == deadline.to_bytes(4, "little") and
                read(p["helper"]["expired"], 3) == p["sample"]["expired"].to_bytes(2, "little")+b"\0" and
                read(p["sample"]["expired"], 1) == b"\0",
                "AES genuine expiry arguments/result changed or cached sample already expired")
    expected = dict(elapsed_ticks=elapsed, polls=4 if mode == "pre-key" else d["polls"], timebase_status=0,
                    phase=1 if mode == "pre-key" else 4, submitted=0 if mode == "pre-key" else 7,
                    input_complete=0 if mode == "pre-key" else 7, output_drained=int(mode == "final"), published=0,
                    configured=3, arms=2 if mode == "pre-key" else 4, ack_issued=0 if mode == "pre-key" else 3,
                    dma_acked=0 if mode == "pre-key" else 7, enc_ack_issued=0 if mode == "pre-key" else 3,
                    enc_acked=0 if mode == "pre-key" else 3,
                    arm=control[2], request=0, irq=0, ircon=control[5], control=0x48, enc_flags=control[1],
                    cfg0_low=control[6], cfg0_high=control[7], cfg1_low=control[8], cfg1_high=control[9],
                    sample_valid=3 if mode == "pre-key" else 1)
    require(d == expected and (mode == "pre-key" or 17 <= d["polls"] <= LIMIT), "AES live phase/completion/ACK diagnostics changed")
    stack = p["fixture_return"].to_bytes(2, "little")
    if mode == "pre-key":
        stack += p["work"].to_bytes(2, "big")+(p["work"]+7).to_bytes(2, "big")
    stack += (p["armed_sample_call"]+3 if mode == "pre-key" else p["final_sample_call"]+3).to_bytes(2, "little")
    if mode == "pre-key":
        stack += (p["work"]+6).to_bytes(2, "big")+p["work"].to_bytes(2, "big")
    stack += (p["expiry_call"]+3 if mode == "pre-key" else p["reader_call"]+3).to_bytes(2, "little")
    require(read(0x1f6e, len(stack)) == stack, "AES complete nested caller/saved-register frame changed")
    vector = b"".join(bytes.fromhex(s) for s in KATS[0])+b"\0"
    require(read(p["key"], 50) == expected_buffers(vector, False), "AES pre-publication caller buffers changed")
    return dict(mode=mode, start=start, previous=previous, deadline=deadline, sampled_now=now,
                final_now_not_yet_latched=mode == "final", stack_hex=stack.hex(), descriptors_hex=descriptors.hex(),
                phase=d["phase"], submitted=d["submitted"], input_complete=d["input_complete"],
                output_drained=d["output_drained"], published=0, polls=d["polls"])


def check_timeout(r, context):
    d = r["aes"]; final = context["mode"] == "final"
    require((r["phase"], r["reason"], r["stage"], r["completed"], r["vector"], r["spaces"], r["result"],
             r["fault_latch"], r["checked"], r["cpu_valid"]) == (4, 4, 3, 0, 0, 1, 8, 8, 50, 0) and
            r["mismatch_buffer"] == r["mismatch_index"] == 255 and
            r["command"] == r["status"] == 0x88 and not d["timebase_status"] and
            TIMEOUT <= d["elapsed_ticks"] < 0x800000 and
            d["polls"] == (context["polls"] if final else 5) and d["sample_valid"] == d["configured"] == 3 and
            d["phase"] == (4 if final else 1) and d["submitted"] == (7 if final else 1) and
            d["input_complete"] in ((7,) if final else (0, 1)) and d["output_drained"] == int(final) and
            not d["published"] and d["arms"] == (4 if final else 2) and
            d["ack_issued"] == (3 if final else 0) and d["dma_acked"] == (7 if final else 0) and
            d["enc_ack_issued"] == (3 if final else 0) and d["enc_acked"] == (3 if final else 0),
            "AES hold did not produce the exact intended unpublished terminal AES_TIMEOUT")
