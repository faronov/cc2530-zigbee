# SPDX-License-Identifier: BSD-3-Clause
"""Strict staged-clock emission proof. Offline only; no debugger/backend import."""
import hashlib
import re

from aes_fixture import CLOCK_PROFILES as AES_CLOCK_PROFILES
from dma_fixture import CLOCK_PROFILES as DMA_CLOCK_PROFILES
from prng_fixture import PRNG_LENGTHS, CLOCK_PROFILES as PRNG_CLOCK_PROFILES
from radio_fifo_fixture import FIFO_LENGTHS, instructions, CLOCK_PROFILES as FIFO_CLOCK_PROFILES
from radio_rx_fixture import CLOCK_PROFILES as RX_CLOCK_PROFILES
from verify_firmware import (
    CLOCK_INSTRUCTION_LENGTHS, cdb_address, cdb_local, peripheral_accesses, require,
    verify_clock_diagnostics, verify_deadline_helper, xdata_ranges, code_bytes,
    verify_layout, verify_timebase_reader, CLOCK_CHECKPOINTS,
)

# SWRU191F Table2-3: ADD/ADDC/SUBB/XRL/ORL A,Rn; INC Rn; MOV Rn,direct;
# CJNE Rn,#data,rel; MOV Rn,#data. No new peripheral operation.
LENGTHS = CLOCK_INSTRUCTION_LENGTHS | PRNG_LENGTHS | FIFO_LENGTHS | {
    0xbe: 3, 0x2a: 1, 0x2b: 1, 0x2e: 1, 0x3b: 1, 0x3c: 1, 0x3f: 1,
    0x6b: 1, 0x4a: 1, 0x98: 1, 0x99: 1, 0x9a: 1, 0x9b: 1,
    0xac: 2, 0xad: 2, 0xaa: 2, 0x0c: 1, 0x0d: 1, 0x7b: 2, 0x24: 2, 0x34: 2,
    0x6e: 1, 0x64: 2,
}
# Complete reviewed relocations, not a masked or partial instruction hash.
# Keys are CODE start, private XDATA start, generic-store runtime entry.
# New consumers must supply their own genuine reviewed emission, not bypass it.
PROFILES = {
    (502, 25, 14937): ("f361ab175d1df5ed1c9a076d6f30688eb9a3f91b1e71fb397397d06fece594d6",
                      "05c07fcf61c66bb58b55df94b441e7ccad1bc3523145ec92fc6ef511cd5958cb", 8),
    (502, 25, 3120): ("b539cebd5bce31e5cc0754a193173f3f56b5f3d27f35760cdb8dcf0fe6c690ba",
                     "05c07fcf61c66bb58b55df94b441e7ccad1bc3523145ec92fc6ef511cd5958cb", 8),
    (1877, 111, 3752): ("e5f46f764db5a448a86752de2b274cd42660b04c8424f3b0594270b295be302f",
                      "5382a2a57d51e1fe362b21caae0b615b4aab047430737d049d164c8e8fb8cd75", 8),
    (1917, 111, 3792): ("32fff20616b365adfca2e77bc1413bec0ff84a06215715e8c022b54e7723069e",
                      "c9f07291a8359396e8701b58bc21d9bd4dcb52a176a017253519c1305bb974f3", 8),
    (7924, 173, 11191): ("36ff46fc18fff64fcbb829c5e296479b0e02c5d4f4928ea0caa48b252de3232c",
                       "84975bf3a1387bbf9657e6d8f5aebfe20c318e0b05c472f90aac0a551e807195", 64),
    (7924, 173, 11231): ("1d4cb336a8300907838d84afcaff419922f5ff59f442805cecbf2ca97205a465",
                       "84975bf3a1387bbf9657e6d8f5aebfe20c318e0b05c472f90aac0a551e807195", 64),
    (502, 25, 10649): ("9e86cca30e30dbb6dd766403eb8ac58bfd6465004ca879d66369fba430995f45",
                      "05c07fcf61c66bb58b55df94b441e7ccad1bc3523145ec92fc6ef511cd5958cb", 8),
    (502, 25, 10689): ("dedbc79d616b9fb3e11cabb63b61f352fb14b31de0746bb77f291f67b64ae18a",
                      "05c07fcf61c66bb58b55df94b441e7ccad1bc3523145ec92fc6ef511cd5958cb", 8),
}
for coupled in (FIFO_CLOCK_PROFILES, DMA_CLOCK_PROFILES, AES_CLOCK_PROFILES,
                PRNG_CLOCK_PROFILES, RX_CLOCK_PROFILES):
    require(not PROFILES.keys() & coupled.keys(), "Duplicate staged clock profile")
    PROFILES.update(coupled)


def require_cdb_lf(debug):
    # Reject alternate record boundaries anywhere, including before a hostile
    # duplicate that a ^-anchored record selector would otherwise ignore.
    require(not re.search(r"[\r\v\f\x1c-\x1e\x85\u2028\u2029]", debug),
            "CDB contains a non-LF line separator")


def private_records(debug):
    # A multiset, not a set: retain every F/S/L helper declaration, entry AND
    # XF end, file/local object and complete T record, including duplicates.
    # Only LF delimits CDB records; never strip CR/Unicode line separators.
    require_cdb_lf(debug)
    return "\n".join(sorted(l for l in debug.split("\n")
                            if re.match(r"^[FSLT]:(?:Lclock\.|X?Fclock\$)", l)))


def public_declarations(debug, name, result):
    require_cdb_lf(debug)
    for kind, tail in (("F", ",Z,0,0,0,0,0"), ("S", ",C,0,0")):
        expected = f"{kind}:G${name}$0_0$0({{2}}DF,{result})"+tail
        declarations = re.findall(rf"^{kind}:G\${re.escape(name)}\$[^\n]*$", debug, re.M)
        require(set(declarations) == {expected}, "Clock public result declaration ABI changed: "+name)


def public_bounds(debug, symbols, name, start, end):
    require_cdb_lf(debug)
    for scope in ("G", "XG"):
        records = re.findall(rf"^L:{scope}\${re.escape(name)}\$([^\n]*)$", debug, re.M)
        require(records and all(re.fullmatch(r"0\$0:[0-9A-Fa-f]+", r) for r in records),
                "Clock public CDB entry/end qualified record changed: "+name)
    require(symbols.get("_"+name) == cdb_address(debug, f"L:G${name}$0$0") == start and
            cdb_address(debug, f"L:XG${name}$0$0") == end,
            "Clock public map/CDB entry/end address ABI changed: "+name)


def source_records(debug, module):
    # Recognized CDB source records only; never open a compiler-named file.
    require(re.search(rf"^L:C\${re.escape(module)}\.c\$\d+\$[^\n]*:[0-9A-Fa-f]+$", debug, re.M),
            "Missing clock source association: "+module)


def verify_clock_metadata(debug, symbols, private):
    require(hashlib.sha256(private_records(debug).encode()).hexdigest() == private,
            "Clock complete private declarations/addresses changed")
    # A valid declaration must not hide a conflicting appended alternative.
    # Identical public declarations/addresses may repeat across linked units.
    public_declarations(debug, "clock_select_init", "SC:U")
    entry = cdb_address(debug, "L:G$clock_select_init$0$0")
    end = cdb_address(debug, "L:XG$clock_select_init$0$0")
    public_bounds(debug, symbols, "clock_select_init", entry, end)
    require(end >= entry,
            "Clock public CDB entry/end address ABI changed")
    for name, declaration in (("source", "({1}SC:U)"),
                              ("timeout_ticks", "({4}SL:U)"),
                              ("poll_limit", "({2}SI:U)"),
                              ("diagnostics", "({3}DG,ST__00000001:S)")):
        cdb_local(debug, "Lclock.clock_select_init$"+name, declaration+",F,0,0")


def verify_clock_code(image, symbols, debug):
    start = cdb_address(debug, "L:Fclock$effective_status$0$0")
    # This SDCC XG record precedes MOV DPL,A; RET, not RET alone.
    # Derive both expected boundaries independently of the public CDB record.
    end = start+1875
    public_bounds(debug, symbols, "clock_select_init", start+0x53a, end-3)
    base = cdb_address(debug, "L:Fclock$output$0_0$0")
    key = start, base, symbols.get("__gptrput")
    require(key in PROFILES, "Unreviewed staged clock relocation")
    digest, private, db = PROFILES[key]
    require(hashlib.sha256(bytes(image.get(a, 255) for a in range(start, end))).hexdigest() == digest,
            "Clock complete instructions/ABI/context changed")
    verify_clock_metadata(debug, symbols, private)
    for module in ("clock", "timebase"):
        source_records(debug, module)
    for name, result, size in (("timebase_read_awake_ticks24", "SL:U", 88),
                               ("timebase_deadline_after", "SC:U", 148),
                               ("timebase_expired", "SC:U", 168)):
        require("_"+name in symbols, "Missing clock public map symbol: "+name)
        public_declarations(debug, name, result)
        public_bounds(debug, symbols, name, symbols["_"+name], symbols["_"+name]+size-1)
    # These public map operands were unused by the old byte matcher. Bind
    # them to every exact CDB parameter declaration, not a valid first match.
    for function, module, arguments in (
        ("clock_select_init", "clock", (("timeout_ticks", "{4}SL:U"), ("poll_limit", "{2}SI:U"),
                                         ("diagnostics", "{3}DG,ST__00000001:S"))),
        ("timebase_deadline_after", "timebase", (("delay", "{4}SL:U"), ("deadline", "{3}DG,SL:U"))),
        ("timebase_expired", "timebase", (("deadline", "{4}SL:U"), ("expired", "{3}DG,:S"))),
    ):
        for number, (name, shape) in enumerate(arguments, 2):
            address = cdb_local(debug, f"L{module}.{function}${name}", f"({shape}),F,0,0")
            require(symbols.get(f"_{function}_PARM_{number}") == address,
                    "Clock public parameter map/CDB association changed")
    code = instructions(image, start, end, LENGTHS)
    require(code.get(end-3) == b"\xf5\x82" and code.get(end-1) == b"\x22",
            "Clock actual return tail changed")
    verify_clock_diagnostics(debug)
    ordinary = {a for lo, hi in xdata_ranges(symbols) for a in range(lo, hi)}
    require(set(range(base, base+54)) <= ordinary and base+54 <= 0x1e00,
            "Clock bounded private XDATA is not allocated")
    require(cdb_local(debug, "Lclock.request_and_wait$sloc0", "({1}SC:U),E,0,0") == db and
            8 <= db < db+12 <= symbols["s_OSEG"] and
            symbols["s_OSEG"]+symbols["l_OSEG"] <= symbols["s_SSEG"] <= 128,
            "Clock DATA/overlay allocation changed or overlaps")
    accesses = peripheral_accesses(code)
    require(tuple(raw.hex() for _, raw, _ in accesses) ==
            ("e5c6", "e59e", "8fc6", "e5a8", "e5b8", "e59a", "e5be"),
            "Clock exact peripheral read/write contract changed")
    for _, _, sfr in accesses:
        name = {0xc6: "CLKCONCMD", 0x9e: "CLKCONSTA", 0xa8: "IEN0",
                0xb8: "IEN1", 0x9a: "IEN2", 0xbe: "SLEEPCMD"}[sfr]
        require(symbols.get("_SOC_"+name) == sfr, "Clock SFR declaration changed")
    for raw in code.values():
        if raw[0] == 0x90:
            require(int.from_bytes(raw[1:], "big") < 0x1e00, "Clock acquired MMIO MOVX")
    calls = [int.from_bytes(raw[1:], "big") for raw in code.values() if raw[0] == 0x12]
    for name, count in (("_timebase_read_awake_ticks24", 2), ("_timebase_deadline_after", 1),
                        ("_timebase_expired", 1)):
        require(calls.count(symbols[name]) == count, "Clock real timebase call inventory changed")
    return code, {(raw[0], sfr): pc for pc, raw, sfr in accesses}


def one_sequence(image, begin, end, raw):
    matches = [a for a in range(begin, end-len(raw)+1)
               if all(image.get(a+i) == value for i, value in enumerate(raw))]
    require(len(matches) == 1, "Clock inspected object differs from actual instruction/pointer association")
    return matches[0]


def generic_store(parameter, address):
    # SDCC three-byte generic pointer: ordinary XDATA has memory-space tag00.
    return (b"\x90"+parameter.to_bytes(2, "big")+
            bytes((0x74, address & 255, 0xf0, 0x74, address >> 8, 0xa3, 0xf0, 0xe4, 0xa3, 0xf0)))


def clock_timeout_checkpoint(image, symbols, debug, code=None, sites=None):
    """Actual deadline RET and source-evidence gates, without instruction patches."""
    code, sites = verify_clock_code(image, symbols, debug)
    start, stop, helper = verify_deadline_helper(image, symbols, debug)
    call_bytes = b"\x12"+start.to_bytes(2, "big")
    calls = [a for a, raw in code.items() if raw == call_bytes]
    require(len(calls) == 1 and
            [a for a in image if all(image.get(a+i) == v for i, v in enumerate(call_bytes))] == calls,
            "Unexpected additional deadline call")
    call = calls[0]
    wait_start = cdb_address(debug, "L:Fclock$request_and_wait$0$0")
    wait_end = cdb_address(debug, "L:XFclock$request_and_wait$0$0")
    observe = cdb_address(debug, "L:Fclock$observe$0$0")
    observes = [a for a, raw in code.items() if wait_start <= a < wait_end and
                raw == b"\x12"+observe.to_bytes(2, "big")]
    timers = [a for a, raw in code.items() if wait_start <= a < wait_end and
              raw == b"\x12"+symbols["_timebase_read_awake_ticks24"].to_bytes(2, "big")]
    work = cdb_address(debug, "L:Fclock$work$0_0$0")
    command = cdb_local(debug, "Lclock.request_and_wait$command", "({1}SC:U),F,0,0")
    require(len(observes) == 1 and len(timers) == 2 and
            wait_start < call < sites[(0x8f, 0xc6)] < observes[0] < timers[1] < wait_end,
            "Clock request/observation/timebase ordering changed")
    output = cdb_address(debug, "L:Fclock$output$0_0$0")
    count = cdb_address(debug, "L:Fclock$count$0_0$0")
    observed = cdb_address(debug, "L:Fclock$observed$0_0$0")
    publish = cdb_address(debug, "L:Fclock$publish_status$0$0")
    # Bind the inspected deadline to the actual generic argument before CALL.
    require(one_sequence(image, wait_start, call, generic_store(helper["deadline"], work+12)) < call,
            "Clock deadline argument must precede its real call")
    context = (b"\xaf\x82\x90"+(count+6).to_bytes(2, "big")+b"\xef\xf0\x12"+
               publish.to_bytes(2, "big")+b"\x90"+command.to_bytes(2, "big")+b"\xe0\xff\x8f\xc6")
    require(one_sequence(image, call+3, sites[(0x8f, 0xc6)]+2, context) == call+3,
            "Clock deadline return-to-request context changed")
    # Source observation stores the actual static work.source_seen before the
    # timed sample. No former pointer-to-local or old numeric address is assumed.
    prefix = (b"\x12"+observe.to_bytes(2, "big")+b"\xd0\x07\x90"+
              (observed+3).to_bytes(2, "big")+b"\xe0\xfe\x90"+
              (observed+1).to_bytes(2, "big")+b"\xe0\x6e\xd0\x07\x20\xe6\x06\x90"+
              (work+17).to_bytes(2, "big")+b"\x74\x01\xf0\x90"+
              (count+6).to_bytes(2, "big")+b"\xe0\x60\x06\x75\x82\x05\x02"+
              wait_end.to_bytes(2, "big")+b"\xc0\x07\x12"+symbols["_timebase_read_awake_ticks24"].to_bytes(2, "big"))
    require(one_sequence(image, observes[0], timers[1]+3, prefix) == observes[0],
            "Clock source-evidence-to-sample context changed")
    scratch = cdb_local(debug, "Lclock.request_and_wait$sloc0", "({1}SC:U),E,0,0")
    require(code.get(sites[(0x8f, 0xc6)]+2) == bytes((0x8f, scratch)),
            "Clock post-request checkpoint instruction changed")

    # This is the board experiment's proof, not a generic successful fallback
    # when caller/source/diagnostic records are absent.
    for module in ("clock_fixture", "clock_fixture_state"):
        source_records(debug, module)
    delta = symbols.get(CLOCK_CHECKPOINTS[0], -1)-0x140
    require(delta in (0, 40), "Clock timeout requires the known board caller")
    for name, entry, last in (("clock_fixture_initialize", 0x34c, 0x442),
                              ("clock_fixture_cycle", 0x443, 0x5c0)):
        public_declarations(debug, name, "SV:S")
        public_bounds(debug, symbols, name, entry+delta, last+delta)
    diagnostics = cdb_local(debug, "Fclock_fixture_state$diagnostics", "({19}ST__00000003:S),F,0,0")
    parameter = cdb_local(debug, "Lclock.clock_select_init$diagnostics", "({3}DG,ST__00000001:S),F,0,0")
    # SDCC optimizes this caller-local pointer away (no L/address record), but
    # its declaration still must not claim another memory space or referent.
    caller_pointer = re.findall(r"^S:(Lclock_fixture_state\.clock_fixture_cycle\$bytes\$[^(\n]+)([^\n]*)$",
                                debug, re.M)
    require(len(set(caller_pointer)) == 1 and caller_pointer[0][1] == "({3}DG,SC:U),F,0,0",
            "Clock caller generic diagnostic pointer declaration changed")
    ordinary = {a for lo, hi in xdata_ranges(symbols) for a in range(lo, hi)}
    state = cdb_address(debug, "L:G$clock_fixture_state$0_0$0")
    require(symbols.get("_clock_fixture_state") == state, "Clock caller state map/CDB association changed")
    regions = [set(range(a, a+n)) for a, n in
               ((state, 56), (diagnostics, 19), (work, 20), (output, 3), (parameter, 3))]
    require(all(r <= ordinary and max(r) < 0x1e00 for r in regions) and
            sum(map(len, regions)) == len(set().union(*regions)),
            "Clock source/diagnostic/argument storage overlaps or is not allocated")
    # The fixture's native diagnostic type must be the same full layout as the
    # clock's, including pointer referents/field types, not just a19-byte object.
    for native, service in ((2, 0), (3, 1)):
        expected = re.findall(rf"^T:Fclock\$__0000000{service}([^\n]*)$", debug, re.M)
        actual = re.findall(rf"^T:Fclock_fixture_state\$__0000000{native}([^\n]*)$", debug, re.M)
        require(len(set(expected)) == 1 and set(actual) ==
                {expected[0].replace("ST__00000000", "ST__00000002")},
                "Clock native caller diagnostic type association changed")
    caller, caller_end = 0x443+delta, 0x5c1+delta
    call_select = one_sequence(image, caller, caller_end, b"\x12"+symbols["_clock_select_init"].to_bytes(2, "big"))
    require(one_sequence(image, caller, call_select, generic_store(parameter, diagnostics)) < call_select,
            "Clock caller did not pass the inspected diagnostics")
    select, select_end = symbols["_clock_select_init"], cdb_address(debug, "L:XG$clock_select_init$0$0")+3
    one_sequence(image, select, select_end, b"\x90"+(work+17).to_bytes(2, "big")+b"\xe4\xf0")
    # Prove the parameter's complete generic value is copied to the published
    # output pointer (including its memory tag), not merely allocated nearby.
    temp = cdb_local(debug, "Lclock.clock_select_init$sloc0", "({3}DG,ST__00000001:S),E,0,0")
    load = b"\x90"+parameter.to_bytes(2, "big")+bytes((0xe0,0xf5,temp,0xa3,0xe0,0xf5,temp+1,0xa3,0xe0,0xf5,temp+2))
    store = b"\x90"+output.to_bytes(2, "big")+bytes((0xe5,temp,0xf0,0xe5,temp+1,0xa3,0xf0,0xe5,temp+2,0xa3,0xf0))
    require(one_sequence(image, select, select_end, load) < one_sequence(image, select, select_end, store),
            "Clock generic diagnostic pointer staging changed")
    return dict(address=stop, function_start=start, function_size=148, return_address=call+3,
                call_address=call, command_write_address=sites[(0x8f, 0xc6)],
                deadline_address=work+12, command_address=command,
                post_request_address=sites[(0x8f, 0xc6)]+2,
                poll_observe_address=observes[0], poll_sample_address=timers[1],
                source_seen_address=work+17, output_pointer_address=output,
                diagnostics_address=diagnostics,
                return_abi="DPL=0 with DPS=0; genuine deadline RET")


BOARD_HASHES = {
    "generic": (3781, "260c8b60fc3780e37d7082b44cb71ed8b369987fa7a14d76c35264af6dae5cf2"),
    "lg_esl29_rev03": (3821, "0a02809ee84cc76456d316c55fd6e52f7c9baf6d146e5fbe057659626158736c"),
}


def verify_clock_board(image, symbols, debug):
    """New exact emission of the unchanged clock board fixture and its gates."""
    names = CLOCK_CHECKPOINTS+("_main", "_clock_fixture_initialize", "_clock_fixture_cycle",
                              "_clock_select_init", "_timebase_read_awake_ticks24",
                              "_timebase_deadline_after", "_timebase_expired")
    require(all(n in symbols and symbols[n] in image for n in names) and
            len({symbols[n] for n in names}) == len(names),
            "Missing/overlapping clock fixture symbol")
    before = symbols.get(CLOCK_CHECKPOINTS[0])
    require(before in (0x140, 0x168), "Clock board checkpoint identity changed")
    board = "generic" if before == 0x140 else "lg_esl29_rev03"
    size, digest = BOARD_HASHES[board]
    require(hashlib.sha256(code_bytes(image, size)).hexdigest() == digest,
            "Clock board complete instructions/runtime changed")
    for name, delta, raw in zip(CLOCK_CHECKPOINTS, (0, 2, 4), (b"\0\x22", b"\0\x22", b"\0\x80\xfd")):
        require(symbols.get(name) == before+delta and
                bytes(image[a] for a in range(before+delta, before+delta+len(raw))) == raw,
                "Clock board checkpoint changed")
    fields = ((0, "signature", 4), (4, "abi_version", 1), (5, "byte_size", 1), (6, "phase", 1),
              (7, "reason", 1), (8, "stage", 1), (9, "requested_source", 1), (10, "completed_steps", 1),
              (11, "clock_result", 1), (12, "timeout", 3), (15, "poll_limit", 2), (17, "diagnostics", 19),
              (36, "initial_sleep_command", 1), (37, "initial_interrupt_enables", 3),
              (40, "current_clock_command", 1), (41, "current_clock_status", 1),
              (42, "current_sleep_command", 1), (43, "current_interrupt_enables", 3),
              (46, "reserved", 8), (54, "guards", 2))
    records = re.findall(r"^T:Fclock_fixture_state\$__00000004[^\n]*$", debug, re.M)
    # The public byte-array wire type from clock_fixture.h; compare entire
    # records, not just offsets extracted from one valid alternative.
    wanted = "T:Fclock_fixture_state$__00000004["+"".join(
        f"({{{a}}}S:S${n}$0_0$0({{{s}}}"+("SC:U" if s == 1 else f"DA{s}d,SC:U")+
        "),Z,0,0)" for a, n, s in fields)+"]"
    require(set(records) == {wanted}, "Clock board complete field ABI changed")
    declarations = re.findall(r"^S:G\$clock_fixture_state\$[^\n]*$", debug, re.M)
    require(set(declarations) == {"S:G$clock_fixture_state$0_0$0({56}ST__00000004:S),F,0,0"} and
            symbols["_clock_fixture_state"] == cdb_address(debug, "L:G$clock_fixture_state$0_0$0") == 0,
            "Clock board state declaration changed")
    verify_timebase_reader(image, symbols, debug, 0, 56)
    proof = clock_timeout_checkpoint(image, symbols, debug)
    diagnostics = proof["diagnostics_address"]
    require(diagnostics == 56 and proof["output_pointer_address"] == 111 and
            proof["source_seen_address"] == 144 and proof["deadline_address"] == 139 and
            symbols["s_SSEG"] == 33 and symbols["s_OSEG"] == 20 and symbols["l_OSEG"] == 3 and
            symbols["l_XSEG"] == 166, "Clock board actual diagnostic/evidence/work binding changed")
    proof["diagnostics_address"] = diagnostics
    return proof
