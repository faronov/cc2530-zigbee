# SPDX-License-Identifier: BSD-3-Clause
"""Actual board C with explicit synthetic AES/DMA effects; never hardware."""
import re
import unittest
from boot_image import boot_commands, check_guards, check_pc, expected_status, marker, simulate, snapshot_commands, memory_dump
from boot_radio_fifo_fixture import sections, snapshot
from aes_fixture import CONTROLLER, check_timeout, decode, expected_buffers, inspect_context, public_vectors, verify_fixture, verify_relocated
from check_aes_hardware import validate_program
from debug_image import DebugImage
from verify_firmware import parse_ihex, require

AES_ALIAS = "memory create addressdecoder xram 0x70b1 0x70b2 sfr_chip 0x31"


def output_effects(p, vectors, count=16, start=0):
    cfg = "(sfr[0xd3]*256+sfr[0xd2])"
    destination = f"(xram[{cfg}+2]*256+xram[{cfg}+3])"
    source = f"(xram[{cfg}]*256+xram[{cfg}+1])"
    vector = f"xram[{p['state']+10}]"
    text = ""
    for i in range(start, count):
        value = ":".join(f"({vector}=={v}?{row[32+i]}" for v, row in enumerate(vectors)) + ":0" + ")"*21
        text += (f"expression sfr[0xb2]={value}; expression xram[{destination}+{i}]=xram[{source}]; "
                 f"expression /0 (0xa2000000+({destination}+{i})*256+xram[{destination}+{i}]); ")
    return text


def verify_transfers(text, vectors):
    calls, phase = [], 0
    for raw in re.findall(r"^0x(a[012][0-9a-f]{6})\r?$", text, re.MULTILINE):
        event = int(raw, 16); kind, value = event >> 24, event & 0xffffff
        if kind == 0xa0:
            require(value in (0x45, 0x47, 0x41), "AES unexpected actual start command")
            if value == 0x45:
                require(not calls or calls[-1] == [16]*4, "AES replaced a not-fully-delivered/drained invocation")
                calls.append([0]*4); phase = 1
            else:
                require(calls and phase == (1 if value == 0x47 else 2) and calls[-1][phase-1] == 16,
                        "AES next phase preceded finite input completion")
                phase += 1
        else:
            require(calls, "AES transferred bytes before a command")
            row = vectors[((len(calls)-1)//2 & 255) % 21]
            index = calls[-1][phase-1 if kind == 0xa1 else 3]
            require(index < 16 and (kind == 0xa1 or (phase == 3 and calls[-1][2] == 16)),
                    "AES extra/premature alias transfer")
            data = (row[:16] if phase == 1 else bytes(16) if phase == 2 else row[16:32]) if kind == 0xa1 else row[32:48]
            address = (0x5d+16*phase if kind == 0xa1 else 0x9d)+index
            require(value == address*256+data[index], "AES actual descriptor/alias byte differs from oracle inputs/output")
            calls[-1][phase-1 if kind == 0xa1 else 3] += 1
    return calls


def start_handler(p, vectors, phase, mode="normal"):
    if ":" in mode:
        selected, mode = mode.split(":")
        if phase != int(selected): mode = "normal"
    if mode in ("partial-output", "lost-output", "lost-enc") and phase != 3:
        mode = "normal"
    cfg0 = "(sfr[0xd5]*256+sfr[0xd4])"
    source = f"(xram[{cfg0}]*256+xram[{cfg0}+1])"
    dest = f"(xram[{cfg0}+2]*256+xram[{cfg0}+3])"
    text = ("expression aes_model=1; expression /0 (0xa0000000+sfr[0xb3]); "
            "expression /0 (0xa3000000+aes_ready); expression aes_phase=aes_phase+1; "
            f"expression /0 {0xa5000000+phase}; dump /h xram {p['descriptor0']} {p['descriptor1']+31}; "
            "expression /0 0xa6000000; "
            f"set memory sfr 0xb3 {0x4c if phase == 1 else 0x4e if phase == 2 else 0x40}; ")
    count = 0 if mode in ("stuck", "cap") else 7 if mode == "partial" else 16
    for i in range(count):
        text += (f"expression xram[{dest}]=xram[{source}+{i}]; "
                 f"expression /0 (0xa1000000+({source}+{i})*256+xram[{dest}]); ")
    if count == 16:
        text += "expression aes_arm=aes_arm&0xfe; set memory sfr 0xd6 2; expression aes_ready=aes_ready&0xfe; "
        if mode != "lost-input": text += "set memory sfr 0xd1 1; "
        if phase != 3: text += "set memory sfr 0x98 0xa7; "
    # Synthetic output is prepared by the independent mathematical oracle,
    # never read from the fixture's expected-answer table. All actual downloads
    # are independently checked before this run can be accepted.
    out_count = (0 if mode in ("output-cap", "ack-cap", "cap-success") else 7 if mode == "partial-output" else 16) if phase == 3 and count == 16 else 0
    text += output_effects(p, vectors, out_count)
    if phase == 3 and count == 16 and mode not in ("ack-cap", "cap-success"):
        text += "set memory sfr 0xb3 0x48; "
        if mode != "lost-enc": text += "set memory sfr 0x98 0xa7; "
        if out_count == 16:
            text += ("expression aes_arm=0; set memory sfr 0xd6 0; "
                     "expression aes_ready=aes_ready&(aes_phase==3?0xfd:0xff); ")
            if mode != "lost-output": text += "set memory sfr 0xd1 3; "
    if mode in ("partial", "partial-output", "lost-input", "lost-output", "lost-enc", "timeout"):
        text += "set memory sfr 0x95 100 0 1; "
    if mode == "timebase": text += "set memory sfr 0x95 100 128 128; "
    if mode == "counter": text += "set memory sfr 0x95 100 0 128; "
    if mode == "state": text += "set memory sfr 0xd7 1; "
    if mode == "control": text += "set memory sfr 0xb3 0x50; "
    if mode.startswith("enc-flags"):
        text += f"set memory sfr 0x98 {0xa4+int(mode[-1])}; "
        if mode.endswith("0"): text += "set memory sfr 0x95 100 0 1; "
    return text + "expression aes_model=0; run"


def model(symbols, p, vectors):
    commands = ["var aes_model", "var aes_arm", "var aes_ready", "var aes_phase", "var aes_delayed",
                "expression aes_model=1", "expression aes_arm=0", "expression aes_ready=0", "expression aes_phase=0", "expression aes_delayed=0",
                "break sfr w 0xc6",
                "commands 1 expression /0 (0xc6000000+sfr[0xc6]); expression sfr[0x9e]=sfr[0xc6]; run",
                f"break {p['arm_input']+3}",
                "commands 2 expression /0 (0xd6000000+sfr[0xd6]); expression aes_arm=aes_arm|1; expression sfr[0xd6]=aes_arm; run",
                f"break {p['arm_ret']}", "commands 3 expression aes_ready=aes_ready|1; expression /0 0xa4000001; run",
                f"break {p['arm_output']+12}", "commands 4 expression aes_ready=aes_ready|2; expression /0 0xa4000002; run",
                ]
    for phase in range(3):
        commands += [f'break sfr w 0xb3 if "aes_model==0 && aes_phase=={phase}"',
                     f"commands {5+phase} "+start_handler(p, vectors, phase+1)]
    commands += ['break sfr w 0xd1 if "aes_model==0"',
                 "commands 8 expression aes_model=1; expression /0 (0xd1000000+sfr[0xd1]); set memory sfr 0xd1 0; expression aes_model=0; run",
                 'break sfr w 0x98 if "aes_model==0 && aes_phase==1"',
                 "commands 9 expression /0 (0x98000000+sfr[0x98]); run",
                 f"break {p['arm_output']+3}",
                 "commands 10 expression /0 (0xd6000000+sfr[0xd6]); expression aes_arm=aes_arm|2; expression sfr[0xd6]=aes_arm; run",
                 f'break {p["module_start"]+0x359} if "aes_phase==3 && aes_delayed && xram[{p["work"]+4}]+256*xram[{p["work"]+5}]==aes_delayed"',
                 "commands 11 expression aes_model=1; "+output_effects(p, vectors)+
                 "set memory sfr 0xb3 0x48; set memory sfr 0x98 0xa7; set memory sfr 0xd6 0; "
                 "set memory sfr 0xd1 3; expression aes_arm=0; expression aes_ready=0; expression aes_model=0; run",
                 'break sfr w 0x98 if "aes_model==0 && aes_phase==3"',
                 "commands 12 expression /0 (0x98000000+sfr[0x98]); expression aes_phase=0; run",
                 'break sfr w 0x98 if "aes_model==0 && aes_phase==2"',
                 "commands 13 expression /0 (0x98000000+sfr[0x98]); run"]
    commands += boot_commands(symbols) + [AES_ALIAS, "fill xram 0x6000 0x70ff 0xa6",
                  "set memory sfr 0xbe 0x84", "set memory sfr 0x95 100 0 0",
                  "set memory sfr 0xb3 8", "set memory sfr 0xd1 0 0 0 0 0 0 0",
                  "set memory sfr 0xc0 0xa0", "set memory sfr 0x98 0xa4"]
    for reg, value in ((0xa9, 0x31), (0xb9, 0xe), (0x88, 3), (0x9b, 3), (0xe9, 0x55), (0x91, 0xaa), (0xe8, 0x12), (0xbf, 0x37)):
        commands.append(f"set memory sfr {reg} {value}")
    return commands + ["expression aes_model=0"]


def execute(simulator, path, board, symbols, p, vectors, mode="normal", cycles=257):
    before, ready, fault = p["checkpoints"]
    positive = mode in ("normal", "3:cap-success")
    commands = model(symbols, p, vectors) + [f"run {symbols['_main']} {before}"] + snapshot_commands(1)
    pc = before
    stages = 1 + 4*cycles if mode == "normal" else 2 if mode == "clock" else 3
    for i in range(stages):
        if pc == ready: commands += ["step 1"]; pc += 1
        commands += [f"run {pc} {ready}", marker(100+2*i), "state",
                     f"dump /h xram {p['state']} {p['output']+17}", "dump /h xram 0x1e00 0x1e1f",
                     "dump /h sfr 0x81 0x81", marker(101+2*i)]
        pc = ready
    if mode == "pre-key":
        commands += ["commands 3 expression aes_ready=aes_ready|1; expression /0 0xa4000001",
                     f"run {ready} {p['arm_ret']}"] + snapshot_commands(4000)
        commands += [f"run {p['arm_ret']} {p['expiry']}"] + snapshot_commands(4010)
        commands += ["set memory sfr 0x95 100 0 1"]; pc = p["expiry"]
    elif mode == "final":
        commands += ["commands 12 expression /0 (0x98000000+sfr[0x98]); expression aes_phase=0",
                     f"run {ready} {p['final_gate']}"] + snapshot_commands(4000)
        commands += [f"run {p['final_gate']} {p['pre_latch']}"] + snapshot_commands(4010)
        commands += ["set memory sfr 0x95 100 0 1"]; pc = p["pre_latch"]
    elif mode.startswith("bytes"):
        i = int(mode[5:])
        # Actual fixture readback entry follows successful publication and its
        # controller checks, not the earlier S0CON acknowledgment.
        commands += [f"run {ready} {p['verify_buffers']}",
                     f"expression xram[{p['key']+i}]=xram[{p['key']+i}]^1"]
        pc = p["verify_buffers"]
    elif mode == "clock":
        commands += ["commands 1 run"]
    elif mode == "flags":
        commands += ["set memory sfr 0xa9 0x30"]
    elif mode == "dma-ack":
        commands += ["commands 8 expression /0 (0xd1000000+sfr[0xd1]); expression aes_model=1; set memory sfr 0xd1 1; expression aes_model=0; run"
                     ]
    elif mode == "enc-ack" or mode.endswith(("enc-ack", "enc-late")):
        phase = 3 if mode == "enc-ack" else int(mode[0])
        effect = "set memory sfr 0x98 0xa7; " if mode.endswith("enc-ack") else "set memory sfr 0x95 100 0 1; "
        number = {1: 9, 2: 13, 3: 12}[phase]
        commands += [f"commands {number} expression /0 (0x98000000+sfr[0x98]); expression aes_model=1; "+effect+
                     ("expression aes_phase=0; " if phase == 3 else "")+"expression aes_model=0; run"]
    elif mode != "normal":
        for phase in range(3):
            commands += [f"commands {5+phase} "+start_handler(p, vectors, phase+1, mode)]
        if mode in ("3:ack-cap", "3:cap-success"):
            polls = 4094 if mode == "3:ack-cap" else 4093
            commands += [f"expression aes_delayed={polls}"]
    if mode == "3:cap-success":
        commands += ["step 1"]; pc = ready+1
    if mode != "normal": commands += [f"run {pc} {ready if positive else fault}"]
    commands += snapshot_commands(5000) + [marker(5004), "dump /h xram 0x6000 0x70ff", marker(5005)]
    if not positive: commands += ["step 64"] + snapshot_commands(5010)
    if mode in ("partial", "partial-output"):
        commands += ["expression aes_model=1"]
        if mode == "partial":
            for i in range(7, 16):
                commands += [f"expression xram[0x70b1]=xram[{0x6d+i}]",
                             f"expression /0 (0xa1000000+{(0x6d+i)*256}+xram[0x70b1])"]
        else:
            commands += [output_effects(p, vectors, start=7).rstrip("; ")]
        commands += ["set memory sfr 0x98 0xa7", f"set memory sfr 0xd6 {2 if mode == 'partial' else 0}",
                     f"set memory sfr 0xd1 {1 if mode == 'partial' else 3}", "expression aes_model=0",
                     "step 64"] + snapshot_commands(5020)
    text = simulate(simulator, commands, path); parts = sections(text)
    initial, _, _ = snapshot(parts, 1)
    require(initial[0x1e00:0x1e20] == expected_status(board), "AES changed startup evidence")
    decode(initial[p["state"]:p["state"]+64])
    for i in range(stages):
        entry = parts[100+2*i]; check_pc(entry, ready)
        r = decode(memory_dump(entry, p["state"], 64))
        require(r["stage"] == (0 if i == 0 else (i-1)%4+1) and r["completed"] == (i//4)&255, "AES stage/wrap changed")
        if r["kind"] == 2:
            require(memory_dump(entry, p["key"], 50) == expected_buffers(vectors[r["vector"]], True), "AES actual C output/source/guard mismatch")
        boot = memory_dump(entry, 0x1e00, 32)
        require(boot[:8]+boot[9:] == initial[0x1e00:0x1e08]+initial[0x1e09:0x1e20] and boot[8] == r["completed"],
                "AES immutable M0/heartbeat changed")
        require(memory_dump(entry, 0x81, 1) == bytes([symbols["s_SSEG"]+1]), "AES leaked caller stack")
    final = ram, iram, sfr = snapshot(parts, 5000)
    check_guards(ram, iram[128:], sfr, symbols)
    peripheral = bytearray(b"\xa6"*0x1100)
    peripheral[0x10b1:0x10b3] = sfr[0x31:0x33]
    require(memory_dump(parts[5004], 0x6000, 0x1100) == peripheral, "AES touched another peripheral XDATA address")
    check_pc(parts[5000], ready if positive else fault)
    require(sfr[1] == symbols["s_SSEG"]+1, "AES final stack did not unwind")
    peak = max(int(n, 16) for n in re.findall(r"Max value of stack pointer= 0x([0-9a-f]+)", text))
    require(peak < 128, "AES reached upper IRAM guard")
    r = decode(ram[p["state"]:p["state"]+64])
    transfers = verify_transfers(text, vectors)
    descriptors = re.findall(r"^0xa500000([123])\r?$\n(.*?)^0xa6000000\r?$", text, re.MULTILINE | re.DOTALL)
    require(descriptors, "AES lacks actual fetched descriptor evidence")
    for phase, body in descriptors:
        expected = bytes((0, 0x5d+16*int(phase)))+b"\x70\xb1\0\x10\x1d\x41\x70\xb2\0\x9d\0\x10\x1e\x11"+bytes(24)
        require(memory_dump(body, p["descriptor0"], 40) == expected, "AES actual finite SINGLE-byte descriptors changed")
    if mode in ("pre-key", "final"):
        saved, stack, regs = snapshot(parts, 4010)
        context = inspect_context(p, lambda a, n: (saved+stack)[a:a+n], regs[1], regs[2], regs[0x12],
                                  bytes(regs[a-0x80] for a in CONTROLLER),
                                  decode(saved[p["state"]:p["state"]+64], allow_running=True), mode)
        check_timeout(r, context)
    if mode == "normal":
        require(transfers == [[16]*4]*(2*cycles), "AES did not transfer every byte of every complete invocation")
        trace = re.findall(r"^(0x(?:a00000[0-9a-f]{2}|a3000003|a400000[12]|d600000[12]|d100001[ce]|980000a4))\r?$", text, re.MULTILINE)
        one = ["0xd6000002", "0xa4000002", "0xd6000001", "0xa4000001", "0xa0000045", "0xa3000003", "0xd100001e", "0x980000a4",
               "0xd6000001", "0xa4000001", "0xa0000047", "0xa3000003", "0xd100001e", "0x980000a4",
               "0xd6000001", "0xa4000001", "0xa0000041", "0xa3000003", "0xd100001c", "0x980000a4"]
        require(trace == one*(2*cycles), "AES command/individual readiness/ack order changed")
        require(re.findall(r"^0xc60000([0-9a-f]{2})\r?$", text, re.MULTILINE) == ["88", "c9"]*cycles,
                "AES clock request count/order changed")
    elif mode == "3:cap-success":
        require(transfers == [[16]*4]*2 and r["aes"]["polls"] == 4096 and r["stage"] == 3 and
                ram[p["key"]:p["key"]+50] == expected_buffers(vectors[0], True),
                "AES last-budget final sample/publication did not actually succeed")
    else:
        require(final == snapshot(parts, 5010), "AES terminal firmware mutated/reused state")
        reason = 6 if mode.startswith("bytes") else 3 if mode == "clock" else 5 if mode == "flags" else 4
        result = (0 if mode.startswith("bytes") else 4 if mode == "clock" else 255 if reason == 5 else
                  9 if mode == "stuck" or mode.endswith("cap") else 10 if mode.endswith("timebase") else
                  11 if mode.endswith("counter") else
                  7 if mode.endswith(("state", "control", "-ack", "enc-flags1", "enc-flags2")) else 8)
        require((r["reason"], r["result"]) == (reason, result), f"AES wrong {mode} negative result: {r}")
        if mode.startswith("bytes"):
            i = int(mode[5:])
            require((r["checked"], r["mismatch_buffer"], r["mismatch_index"]) ==
                    (i, 0 if i < 16 else 1 if i < 32 else 2, i if i < 16 else i-16 if i < 32 else i-32),
                    "AES C readback missed exact mismatch")
        elif reason == 4:
            require(r["checked"] == 50 and not r["aes"]["published"] and
                    memory_dump(parts[5000], p["key"], 50) == expected_buffers(vectors[0], False),
                    "AES failure changed/reused caller output")
        if mode == "3:ack-cap":
            require(r["aes"]["phase"] == 3 and r["aes"]["output_drained"] == 1 and r["aes"]["enc_ack_issued"] == 2 and
                    r["aes"]["polls"] == 4096 and r["aes"]["dma_acked"] == 3,
                    "AES exhausted cap did not stop before confirmed block ACK/final publication")
        if mode in ("partial", "partial-output"):
            late, late_stack, late_regs = snapshot(parts, 5020)
            require(late == ram and late_stack == iram and late_regs[0x56] == (2 if mode == "partial" else 0) and
                    late_regs[0x51] == (1 if mode == "partial" else 3) and
                    transfers[-1] == ([16, 0, 0, 0] if mode == "partial" else [16]*4),
                    "Late delivery/drain changed C state/caller buffers or was not actually applied")
    return peak, parts, text


def check_aes_fixture(simulator, output, board, symbols):
    path = output/"aes_fixture.ihx"; image = parse_ihex(path.read_text()); debug = path.with_suffix(".cdb").read_text()
    p = verify_fixture(image, symbols, debug); vectors = public_vectors(output/"aes-reference")
    require(bytes(image[a] for a in range(p["vectors"], p["vectors"]+1029)) == b"".join(vectors), "AES fixture table differs from independent oracle")
    board_image = DebugImage(output, board, "aes_fixture")
    program = path.with_suffix(".bin").read_bytes()
    for cycles, negative in ((257, "none"), (1, "pre-key"), (1, "final")):
        validate_program(board_image, program, vectors, cycles, negative)
    case = unittest.TestCase()
    for a in image:
        with case.assertRaises(ValueError): verify_fixture(image | {a: image[a]^1}, symbols, debug)
    for off in range(5254):
        with case.assertRaises(ValueError): verify_relocated(image | {p["module_start"]+off: image[p["module_start"]+off]^1}, symbols, debug)
    critical = [n for n in symbols if n.startswith(("_aes_", "_SOC_DMA", "_AEF_"))]
    critical += ["__gptrput_PARM_2", "l_XSEG", "s_XSEG", "s_SSEG", "l_PSEG", "l_XISEG", "l_XABS", "l_BSEG",
                 "_SOC_ENCDI", "_SOC_ENCDO", "_SOC_ENCCS", "_SOC_S0CON"]
    for name in critical:
        if name in ("_aes_fixture_initialize",): continue
        if name.startswith("_aes_fixture_") and name not in ("_aes_fixture_state", "_aes_fixture_key", "_aes_fixture_input",
                "_aes_fixture_output", "_aes_fixture_work", "_aes_fixture_before", "_aes_fixture_ready", "_aes_fixture_fault", "_aes_fixture_vectors"):
            continue
        with case.assertRaises(ValueError, msg=name): verify_fixture(image, symbols | {name: symbols[name]+1}, debug)
    peak, count = 0, 0
    modes = ("normal", "pre-key", "final", "partial", "partial-output", "stuck", "lost-input", "lost-output", "lost-enc",
             "dma-ack", "enc-ack", "flags", "clock", "3:output-cap", "3:ack-cap", "3:cap-success")
    modes += tuple(f"{phase}:{effect}" for phase in (1, 2, 3) for effect in ("timeout", "cap", "timebase", "counter", "state", "control", "partial"))
    modes += tuple(f"{phase}:{effect}" for phase in (1, 2, 3) for effect in ("enc-flags0", "enc-flags1", "enc-flags2", "enc-ack", "enc-late"))
    for mode in modes + tuple(f"bytes{i}" for i in range(50)):
        high, _, _ = execute(simulator, path, board, symbols, p, vectors, mode)
        peak = max(peak, high); count += 1
    print(f"{board}: AES fixture {count} compiled scenarios; 257 cycles/514 AES calls/32896 actual descriptor/alias bytes, "
          f"21 public vectors/all spaces/both clocks; corrected module/negative contexts/terminal guards PASS, peak SP={peak:02x} (synthetic only).")
