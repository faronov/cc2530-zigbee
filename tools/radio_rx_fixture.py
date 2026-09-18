# SPDX-License-Identifier: BSD-3-Clause
"""Read-only passive RX wire decoder and fixed, genuine SDCC board-image proof."""
import hashlib
import re

from prng_fixture import PRNG_LENGTHS
from radio_fifo_fixture import instructions
from verify_firmware import (
    CLOCK_INSTRUCTION_LENGTHS, cdb_address, code_bytes, peripheral_accesses, require,
    verify_clock_code, verify_clock_diagnostics, verify_deadline_helper,
    verify_timebase_reader, xdata_ranges,
)

SIZE = 96
CHECKPOINTS = tuple("_radio_rx_fixture_" + n for n in ("before", "ready", "fault", "end"))
HASHES = {
    0x140: (9120, "473758bc8bf8e9bf1b503117e254a907ac23918a34bf006bbb62c4e3da63a9e7"),
    0x168: (9160, "0e31578a708d9c8d4556caec33f062fd82baf7498ebef1af3b708f868ab6c8d2"),
}
# SWRU191F (April2014), Table2-3 p.37: 52 is two-byte ANL direct,A.
# Reviewed below as the sole ANL AR0,A (not an SFR).
LENGTHS = PRNG_LENGTHS | CLOCK_INSTRUCTION_LENGTHS | {
    0x1c: 1, 0x23: 1, 0x2b: 1, 0x3c: 1, 0x52: 2, 0xa4: 1}
SETTINGS = (0x6189, 0x618a, 0x6180, 0x6182, 0x6194, 0x6195, 0x61b2, 0x61fa, 0x61ae, 0x618f)
VALUES = bytes((0x40, 0, 12, 0, 127, 1, 21, 9, 0, 0))
XREADS = (0x624a, 0x61e1, 0x6189, 0x61a3, 0x61a4, 0x61a5, 0x61a8, 0x61a9, 0x61b8, 0x61b9,
          0x618b, 0x6192, 0x6193, 0x619b, 0x619c, 0x619d, 0x619e, 0x619f, 0x61a1, 0x61a2, 0x6199)
DIAGNOSTIC_FIELDS = (
    ("elapsed_ticks", 4), ("polls", 2),
) + tuple((n, 1) for n in (
    "timebase_status", "phase", "writes", "verified", "actions", "sample_valid",
    "rx_enable", "fsm0", "signals", "rx_count", "tx_count", "rx_first", "rx_last",
    "rx_packet", "tx_first", "tx_last", "errors", "flags0", "flags1", "rssi_valid",
    "bytes_read", "phr", "rssi_raw", "crc_correlation", "discarded_bytes"))
STATE_FIELDS = (
    ("signature", 4),
) + tuple((n, 1) for n in (
    "version", "size", "phase", "reason", "stage", "attempt", "completed", "result",
    "fault_latch", "clock_result", "channel", "maximum")) + (
    ("timeout", 4), ("limit", 2), ("clock", 19), ("diagnostic", 31),
    ("command", 1), ("status", 1), ("sleep", 1), ("enables", 3), ("initial_sleep", 1),
    ("initial_flags", 7), ("flags", 7), ("guards", 3),
)
FLAGS = (0xa9, 0xb9, 0x88, 0x98, 0x9b, 0xe8, 0xc0)


def verify_code(image, before):
    require(before in HASHES, "RX fixture unknown board checkpoint")
    size, digest = HASHES[before]
    require(hashlib.sha256(code_bytes(image, size)).hexdigest() == digest,
            "RX fixture exact linked instructions/constants changed")


def verify_driver_listing(code, listing):
    listed = [(int(m[1], 16), bytes.fromhex(m[2])) for m in re.finditer(
        r"^\s+([0-9A-F]{6}) ((?:[0-9A-F]{2} ){1,3})\s+\[\s*\d+\]", listing, re.MULTILINE)]
    require(listed == list(code.items()), "RX driver listing differs from genuine image")


def verify_fscal1_readback(code, read, scratch, values):
    """Independent instruction check: exactly one read, mask03 only at index8."""
    # SDCC 4.2 uses bank0 R1 for the settings index, one IRAM byte for the
    # observed value, and a two-byte promoted conditional (high byte zero).
    # The 8-bit AND targets AR0; expected frequency/index9 and CODE values
    # retain their full-byte comparison. No additional MMIO or XDATA scratch.
    expected = (
        b"\xe0", bytes((0xf5, scratch)), b"\xb9\x08\x02", b"\x80\x06",
        b"\xd0\x04", b"\xd0\x05", b"\x80\x0c", b"\xd0\x04", b"\xd0\x05",
        bytes((0x75, scratch+1, 3)), bytes((0x75, scratch+2, 0)), b"\x80\x06",
        bytes((0x75, scratch+1, 255)), bytes((0x75, scratch+2, 0)),
        bytes((0xa8, scratch+1)), bytes((0xe5, scratch)), b"\x52\x00",
        b"\xb9\x09\x09", b"\x8d\x82", b"\x8c\x83", b"\xe0",
        bytes((0xf5, scratch+1)), b"\x80\x07", b"\xe9",
        b"\x90"+values.to_bytes(2, "big"), b"\x93", bytes((0xf5, scratch+1)),
        b"\xe8", bytes((0xb5, scratch+1, 2)), b"\x80\x06", b"\x75\x82\x07",
    )
    pc = read
    for data in expected:
        require(code.get(pc) == data, "RX FSCAL1 mask/full-byte comparison instructions changed")
        pc += len(data)
    require([(a, b) for a, b in code.items() if b[0] == 0x52] == [(read+36, b"\x52\x00")],
            "RX FSCAL1 added an unreviewed ANL direct,A")
    return pc


def fields(debug, module, tag, expected):
    records = re.findall(rf"^T:F{module}\${tag}\[(.*)\]$", debug, re.MULTILINE)
    require(records, "Missing RX fixture ABI")
    offset, wanted = 0, []
    for name, size in expected:
        wanted.append((offset, name, size)); offset += size
    for line in records:
        actual = re.findall(r"\(\{(\d+)\}S:S\$([^$]+)\$0_0\$0\(\{(\d+)\}", line)
        require([(int(a), b, int(c)) for a, b, c in actual] == wanted, "RX fixture field ABI changed")


def verify_fixture(image, symbols, debug):
    require(all(n in symbols for n in CHECKPOINTS), "Missing RX fixture checkpoint symbol")
    before, ready, fault, end = (symbols[n] for n in CHECKPOINTS)
    verify_code(image, before)
    delta = before - 0x140
    require((ready, fault, end) == (before+2, before+4, before+7) and
            bytes(image[i] for i in range(before, before+10)) == b"\0\x22\0\x22\0\x80\xfd\0\x80\xfd",
            "RX fixture checkpoint instructions changed")
    for source in ("radio_rx_fixture.c", "radio_rx_fixture_state.c", "radio_rx.c", "clock.c", "timebase.c"):
        require(f"C${source}$" in debug, "Missing RX fixture source records")
    for name, address in (("SOC_IEN0", 0xa8), ("SOC_IEN1", 0xb8), ("SOC_IEN2", 0x9a),
                          ("SOC_CLKCONCMD", 0xc6), ("SOC_CLKCONSTA", 0x9e), ("SOC_SLEEPCMD", 0xbe),
                          ("SOC_RFD", 0xd9), ("SOC_RFST", 0xe1), ("SOC_RFERRF", 0xbf),
                          ("SOC_RFIRQF0", 0xe9), ("SOC_RFIRQF1", 0x91), ("SOC_IRCON", 0xc0),
                          ("RXF_IP0", 0xa9), ("RXF_IP1", 0xb9), ("RXF_TCON", 0x88),
                          ("RXF_S0CON", 0x98), ("RXF_S1CON", 0x9b), ("RXF_IRCON2", 0xe8)):
        require(symbols.get("_"+name) == address, "RX fixture SFR symbol changed")
    for module in ("radio_rx_fixture", "radio_rx_fixture_state"):
        fields(debug, module, "__00000006", STATE_FIELDS)
        fields(debug, module, "__00000005", DIAGNOSTIC_FIELDS)
        fields(debug, module, "__00000004",
               (("length", 1), ("rssi_raw", 1), ("correlation", 1), ("body", 125)))
    fields(debug, "radio_rx", "__00000001", DIAGNOSTIC_FIELDS)
    fields(debug, "radio_rx", "__00000000",
           (("length", 1), ("rssi_raw", 1), ("correlation", 1), ("body", 125)))
    ordinary = {a for lo, hi in xdata_ranges(symbols) for a in range(lo, hi)}
    require(symbols["l_XSEG"] == 538 and symbols["s_SSEG"] == 0x61 and symbols["l_SSEG"] == 159
            and symbols["l_PSEG"] == symbols["l_XISEG"] == symbols["l_XABS"] == 0,
            "RX fixture allocated memory/stack changed")
    require(symbols["_radio_rx_reserved_end"] == 0xfa and symbols["_radio_rx_fault"] == 0x45 and
            symbols["__gptrput_PARM_2"] == 0x219, "RX fixture private prefix/helper changed")
    for name, address, size in (("state", 0xfb, SIZE), ("frame", 0x15b, 128),
                               ("diagnostics", 0x1db, 31), ("clock", 0x1fa, 19)):
        name = "radio_rx_fixture_" + name
        sizes = re.findall(rf"^S:G\${name}\$[^(\n]+\(\{{(\d+)\}}", debug, re.MULTILINE)
        require(symbols.get("_"+name) == address and sizes and all(int(n) == size for n in sizes)
                and set(range(address, address+size)) <= ordinary and 0xfa < address
                and address+size <= 0x219, "RX fixture caller allocation/ABI changed")
    for n in ("output", "d"):
        require(re.search(rf"S:Lradio_rx.radio_rx_receive_init\${n}\$[^(]+\(\{{2\}}DX,ST", debug),
                "RX fixture driver pointer ABI is not XDATA")
    require("F:G$radio_rx_receive_init$0_0$0({2}DF,SC:U),Z,0,0,0,0,0" in debug,
            "RX fixture result ABI changed")
    start = cdb_address(debug, "L:Fradio_rx$ordinary$0$0")
    stop = cdb_address(debug, "L:XG$radio_rx_receive_init$0$0")+1
    require((start, stop) == (0x9fe+delta, 0x1b85+delta), "RX fixture driver extent changed")
    code = instructions(image, start, stop, LENGTHS)
    accesses = peripheral_accesses(code)
    require([b.hex() for _, b, _ in accesses] == [
        "e5a8", "e5b8", "e59a", "acbe", "e5c6", "e59e", "e5bf", "e5e9", "e591",
        "e5c6", "75e1e3", "e5d9", "e5d9", "75e1ed",
    ], "RX fixture SFR/exactly-once destructive RFD contract changed")
    sites = {}

    def sfr_sites(accesses):
        for pc, data, reg in accesses:
            write = data[0] in (0x75, 0x88)
            sites[pc] = ("w" if write else "r", reg,
                         ("iram", data[0]-0xa8) if 0xa8 <= data[0] <= 0xaf else
                         ("sfr", reg if write else 0xe0))
    sfr_sites(accesses)
    actual_reads = []
    for pc, data in code.items():
        if data[0] != 0x90 or int.from_bytes(data[1:], "big") < 0x6000:
            continue
        address = int.from_bytes(data[1:], "big")
        if address == 0x618d:
            require(code.get(pc+3) == b"\x74\x80" and code.get(pc+5) == b"\xf0",
                    "RX fixture soft-stop write changed")
            sites[pc+5] = ("w", address, ("xram", address))
        else:
            require(code.get(pc+3) == b"\xe0", "RX fixture unexpected XREG operation")
            actual_reads.append(address); sites[pc+3] = ("r", address, ("sfr", 0xe0))
    require(tuple(actual_reads) == XREADS, "RX fixture expanded static MMIO")
    for pc, op, kind in ((0xba9+delta, b"\xe0", "r"), (0x1413+delta, b"\xf0", "w")):
        require(code.get(pc) == op, "RX fixture reviewed indexed MMIO site changed")
        sites[pc] = (kind, None, ("sfr", 0xe0) if kind == "r" else ("xram", None))
    for name, value in (("settings", b"".join(a.to_bytes(2, "little") for a in SETTINGS)),
                        ("values", VALUES)):
        a = cdb_address(debug, f"L:Fradio_rx${name}$0_0$0")
        require(bytes(image[i] for i in range(a, a+len(value))) == value, "RX fixture CODE table changed")
    verify_fscal1_readback(code, 0xba9+delta, 0x4e, cdb_address(debug, "L:Fradio_rx$values$0_0$0"))
    caller = instructions(image, stop, cdb_address(debug, "L:XG$main$0$0")+1, LENGTHS)
    reads = peripheral_accesses(caller)
    require([data for _, data, _ in reads] ==
            [bytes((0xe5, a)) for a in (0xc6, 0x9e, 0xbe, 0xa8, 0xb8, 0x9a)+FLAGS],
            "RX fixture caller added MMIO")
    require(all(data[0] != 0x90 or int.from_bytes(data[1:], "big") < 0x1e00 or
                int.from_bytes(data[1:], "big") in (0x1e18, 0x1e19) for data in caller.values()),
            "RX fixture caller absolute MMIO outside M0")
    sfr_sites(reads)
    clock_code, _ = verify_clock_code(image, symbols, debug)
    sfr_sites(peripheral_accesses(clock_code))
    verify_clock_diagnostics(debug); verify_deadline_helper(image, symbols, debug)
    verify_timebase_reader(image, symbols, debug, 0xfb, SIZE)
    reader = symbols["_timebase_read_awake_ticks24"]
    for offset, reg in ((3, 0x95), (9, 0x96), (15, 0x97)):
        sites[reader+offset] = ("r", reg, ("sfr", 0xe0))
    # SDCC retains an unused out-of-line copy of the inline step. Main's live
    # call path has one call per service; the full image pins both copies.
    calls = [b for pc, b in caller.items() if pc >= symbols["_main"] and b[0] == 0x12]
    for name in ("_radio_rx_receive_init", "_clock_select_init"):
        require(calls.count(b"\x12"+symbols[name].to_bytes(2, "big")) == 1,
                "RX fixture must call each real service at exactly one site")
    return {"checkpoints": [before, ready, fault, end], "sites": sites,
            "driver_start": start, "driver_end": stop, "state": 0xfb, "frame": 0x15b,
            "diagnostics": 0x1db, "clock": 0x1fa, "stack_start": 0x61}


def decode(data):
    """No raw body, addresses, identities or body hashes in returned status."""
    require(type(data) is bytes and len(data) == SIZE and data[:6] == b"M2RX\x01\x60",
            "RX fixture signature/version/size mismatch")
    require(data[93:] == b"\x69\x96\xc7" and data[14:22] == b"\x0f\x10\0\0\x01\0\xff\xff",
            "RX fixture guards/bounds changed")
    phase, reason, stage, attempt, completed, result, latch, clock_result = data[6:14]
    require(phase in (1, 3, 4, 5) and reason <= 5 and stage <= 2 and
            completed <= attempt <= 16 and (result <= 15 or result == 255) and latch <= 15
            and clock_result <= 9,
            "RX fixture invalid phase/counters/result")
    require((phase != 1 or (stage == attempt == completed == 0)) and
            (phase != 3 or stage == 1), "RX fixture checkpoint/stage mismatch")
    require((phase == 4) == bool(reason) and (phase != 5 or (stage == 2 and attempt == 16)) and
            (stage != 2 or phase == 5), "RX fixture terminal phase mismatch")
    require((attempt == 0) == (result == 255), "RX fixture attempt/result mismatch")
    require(phase == 4 or (latch == 0 and ((stage == 0 and phase == 1 and clock_result == 8) or
            (stage >= 1 and clock_result == 0))), "RX fixture clock/fault mismatch")
    require(phase == 4 or result in (0, 15, 255), "RX fixture nonterminal service error")
    require(reason != 4 or (result not in (0, 15, 255) and (latch == result or result in (1, 2, 3))),
            "RX fixture original failure lost")
    raw = data[41:72]
    if not attempt: require(raw == bytes(31), "RX fixture diagnostics before an attempt")
    clock = data[22:41]
    require(clock[6] <= 1 and clock[13] <= 1 and clock[18] <= 9, "RX fixture clock ABI invalid")
    if clock_result == 8:
        require(clock == bytes(18)+b"\x08", "RX fixture unattempted clock diagnostics changed")
    if clock_result == 0:
        require(int.from_bytes(clock[:4], "little") <= 1024 and
                1 <= int.from_bytes(clock[4:6], "little") <= 4096 and
                clock[6:14] == bytes(8) and clock[14:] == b"\xc9\x88\x88\x88\x08",
                "RX fixture did not confirm bounded XOSC32")
    d, offset = {}, 0
    for name, size in DIAGNOSTIC_FIELDS:
        d[name] = int.from_bytes(raw[offset:offset+size], "little"); offset += size
    require(d["elapsed_ticks"] <= 0xffffff and d["timebase_status"] <= 1 and d["phase"] <= 7 and
            d["writes"] <= 10 and d["verified"] <= d["writes"] and d["actions"] <= 7 and
            d["sample_valid"] <= 1, "RX fixture diagnostic ABI invalid")
    if attempt and result in (0, 15):
        require(d["phase"] == (7 if result == 0 else 6) and
                d["writes"] == d["verified"] == 10 and d["actions"] == 7
                and d["sample_valid"] == 1 and 3 <= d["phr"] <= 127 and
                d["bytes_read"] == d["phr"]+1 and d["elapsed_ticks"] < 65536 and d["polls"] > 0
                and not any(d[n] for n in ("rx_enable", "rx_count", "rx_first", "rx_last", "rx_packet",
                                           "tx_count", "tx_first", "tx_last", "errors"))
                and not (d["fsm0"] & 0xc0 or d["signals"] & 0xe7 or
                         d["flags0"] & 1 or d["flags1"] & 0xfb)
                and bool(d["crc_correlation"] & 128) == (result == 0),
                "RX fixture invalid completed RX diagnostics")
    if phase != 4:
        require(data[72] == data[73] == (0xc9 if stage == 0 else 0x88) and
                data[74] == data[78] and data[74] & 7 == 4 and data[75:78] == bytes(3)
                and data[79:85] == data[86:92] and
                data[92] in (data[85], data[85] | 128), "RX fixture CPU/flag ownership changed")
    return dict(phase=phase, reason=reason, stage=stage, attempt=attempt, completed=completed,
                result=result, fault_latch=latch, clock_result=clock_result, diagnostic=d,
                clock=list(data[22:41]), command=data[72], status=data[73], sleep=data[74],
                enables=list(data[75:78]), initial_sleep=data[78],
                initial_flags=list(data[79:86]), flags=list(data[86:93]))


def check_frame(record, frame):
    require(type(frame) is bytes and len(frame) == 128, "RX fixture frame ABI size mismatch")
    if record["result"] != 0:
        require(frame == b"\xa5"*128, "RX failure published a frame")
    else:
        d = record["diagnostic"]
        require(frame[:3] == bytes((d["phr"]-2, d["rssi_raw"], d["crc_correlation"] & 127)) and
                frame[3+frame[0]:] == b"\xa5"*(125-frame[0]), "RX frame metadata/tail mismatch")
