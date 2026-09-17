# SPDX-License-Identifier: BSD-3-Clause
"""Unchanged board C with synthetic PRNG effects; bounded checkpoint continuations."""
import re
import unittest

from boot_image import ALIAS, boot_commands, check_guards, check_pc, expected_status, marker, memory_dump, simulate, snapshot_commands
from boot_radio_fifo_fixture import sections, snapshot
from prng_fixture import FLAGS, FlagHistory, Sequence, advance, decode, verify_fixture, verify_relocated
from verify_firmware import parse_ihex, require


def completion(mode="normal"):
    terms = [f"(((pn_state>>{i})&1)*{advance(1<<i)})" for i in range(16)]
    text = "expression /0 (0xb4000000+sfr[0xb4]); "
    if mode == "stuck": return text+"run"
    if mode != "ignored":
        text += "expression pn_state="+"^".join(terms)+"; "
        text += "expression sfr[0xbc]=pn_state&255; expression sfr[0xbd]=pn_state>>8; "
    text += "expression sfr[0xb4]=sfr[0xb4]&0xf3; "
    changes = {"eoc":"expression sfr[0xb4]=sfr[0xb4]^128; ", "st":"expression sfr[0xb4]=sfr[0xb4]|64; ",
               "reserved":"expression sfr[0xb4]=sfr[0xb4]|8; ", "irq":"set memory sfr 0xa8 1; ",
               "clock":"set memory sfr 0xc6 0x88; ", "stif":"expression sfr[0xc0]=sfr[0xc0]|128; "}
    return text+changes.get(mode,"")+"run"


def model(proof, retained=None, mode="normal"):
    commands = [ALIAS, "var pn_state", f"expression pn_state={retained if retained is not None else 65535}"]
    for slot, pc in enumerate((proof["high_write"]+2,proof["low_write"]+2),1):
        effect = ("expression pn_state=((pn_state&255)<<8)|sfr[0xbc]; "
                  "expression sfr[0xbc]=pn_state&255; expression sfr[0xbd]=pn_state>>8; ")
        if mode == f"seed{slot}":
            effect = "expression sfr[0xbc]=pn_state&255; "
        commands += [f"break {pc:#x}",f"commands {slot} expression /0 (0xbc000000+sfr[0xbc]); "+effect+"run"]
    commands += [f"break {proof['command']+2:#x}", "commands 3 "+completion(mode),
                 f"break {proof['clock_write']+2:#x}",
                 "commands 4 expression /0 (0xc6000000+sfr[0xc6]); expression sfr[0x9e]=sfr[0xc6]; run"]
    return commands


def restore(memory):
    ram, iram, sfr = memory
    commands=["fill xram 0x6000 0x70ff 0xa6"]
    for space,start,data in (("xram",0,ram),("iram",0,iram),("sfr",0x80,sfr)):
        for offset in range(0,len(data),64):
            commands.append(f"set memory {space} {start+offset:#x} "+" ".join(f"{b:#x}" for b in data[offset:offset+64]))
    return commands


def initial(symbols,stif=0):
    commands=boot_commands(symbols)
    commands += ["fill xram 0x6000 0x70ff 0xa6", "set memory sfr 0xbe 0x84",
                 "set memory sfr 0xb4 0x33", "set memory sfr 0xbc 255 255", "set memory sfr 0x95 100 0 0"]
    for reg,value in zip(FLAGS,(0x31,0x0e,3,3,3,0x55,0xaa,0x12,0x37,0x20|stif)):
        commands.append(f"set memory sfr {reg:#x} {value}")
    return commands


def check_snapshot(parts,n,symbols,proof,pc,initial_boot,*,injected_irq=False):
    check_pc(parts[n],pc)
    ram,iram,sfr = snapshot(parts,n)
    guard_sfr=sfr
    if injected_irq:
        require(sfr[0xa8-128] == 1,"PRNG lost the deliberately injected IRQ enable")
        guard_sfr=sfr[:0xa8-128]+b"\0"+sfr[0xa9-128:]
    check_guards(ram,iram[128:],guard_sfr,symbols)
    require(sfr[1] == 0x4f, "PRNG fixture stack did not unwind")
    record=decode(ram[proof["state"]:proof["state"]+88],running=pc == proof["probe_call"])
    boot=ram[0x1e00:0x1e20]
    require(boot[:8]+boot[9:] == initial_boot[:8]+initial_boot[9:] and boot[8] == record["completed"],
            "PRNG immutable M0/heartbeat changed")
    require(iram[128:] == b"\xc7"*128, "PRNG upper IRAM alias guard changed")
    return record,(ram,iram,sfr)


def inspect_probe(proof,record,memory):
    ram,iram,sfr=memory
    require(record["phase"] == 2 and record["stage"] == 6 and record["total"] == 131084 and
            record["fault_latch"] == 0 and record["probe"] == [0]*3 and
            sfr[1] == proof["probe_sp"] and sfr[2:4] == proof["probe_output"].to_bytes(2,"little") and
            sfr[0x12] == 0 and ram[proof["limit"]] == 16 and
            iram[0x4e:0x50] == proof["probe_return"].to_bytes(2,"little") and
            sfr[0xb4-128] == record["initial_adc"]|12 and ram[proof["probe_output"]:proof["probe_output"]+2] == b"\x69\x96",
            "PRNG actual stopped-call frame/arguments/state changed")


def check_prng_fixture(simulator,output,board,symbols):
    path=output/"prng_fixture.ihx"; image=parse_ihex(path.read_text()); debug=path.with_suffix(".cdb").read_text()
    proof=verify_fixture(image,symbols,debug); before,ready,fault=proof["checkpoints"]
    case=unittest.TestCase()
    for a in image:
        with case.assertRaises(ValueError): verify_fixture(image | {a:image[a]^1},symbols,debug)
    for a in range(proof["module_start"],proof["module_end"]):
        with case.assertRaises(ValueError): verify_relocated(image | {a:image[a]^1},symbols,debug)
    for name in ("_prng_fixture_state","_prng_fixture_buffer","_prng_fixture_probe_output","_prng_fixture_clock",
                 "_prng_fault","_prng_reserved_end","_prng_next16_PARM_2","__gptrput_PARM_2","s_SSEG","l_XSEG"):
        with case.assertRaises(ValueError,msg=name): verify_fixture(image,symbols | {name:symbols[name]+1},debug)
    from debug_image import DebugImage
    from check_prng_hardware import validate_program
    actual=DebugImage(output,board,"prng_fixture")
    for mode in ("short","full","stopped"): validate_program(actual,path.with_suffix(".bin").read_bytes(),mode)
    seq=Sequence(); history=FlagHistory()
    carry=None; peak=0; transitions=[]; seed_writes=[]; clocks=[]; chunks=0
    initial_boot=expected_status(board)
    while seq.stage != 5:
        commands=model(proof,None if carry is None else carry[2][0xbc-128] | carry[2][0xbd-128]<<8)
        if carry is None:
            commands+=initial(symbols)+[f"run {symbols['_main']:#x} {before:#x}"]+snapshot_commands(1)
            pc=before
        else:
            commands+=restore(carry)+[f"pc {ready:#x}"]+snapshot_commands(1)
            pc=ready
        count=min(128,4111-seq.ready)
        for i in range(count):
            commands += ["step 1",f"run {pc+1:#x} {ready:#x}"]
            if seq.ready+i+1 == 128:
                commands += ["expression sfr[0xc0]=sfr[0xc0]|128"]
            commands += [marker(100+3*i),"state",
                         "dump /h xram 0x63 0x100","dump /h xram 0x1e00 0x1e1f",
                         marker(101+3*i),"dump /h sfr 0x80 0xff",marker(102+3*i)]
            pc=ready
        commands+=snapshot_commands(500)+[marker(504),"dump /h xram 0x6000 0x70ff",marker(505)]
        text=simulate(simulator,commands,path); parts=sections(text)
        if carry is None:
            r,first=check_snapshot(parts,1,symbols,proof,before,initial_boot)
            require(r["phase"] == 1 and first[0][0xbb:0x101] == bytes(70),"PRNG initial C/caller checks failed")
            history.observe(r,[first[2][reg-128] for reg in FLAGS])
        else:
            check_pc(parts[1],ready)
            require(snapshot(parts,1) == carry,"PRNG continuation did not restore complete genuine memory/CPU state")
        for i in range(count):
            part=parts[100+3*i]; check_pc(part,ready)
            r=decode(memory_dump(part,0x63,88))
            sfr=memory_dump(parts[101+3*i],0x80,128)
            history.observe(r,[sfr[reg-128] for reg in FLAGS])
            seq.accept(r,memory_dump(part,0xbb,68))
            require(memory_dump(parts[101+3*i],0x81,1) == b"\x4f","PRNG READY stack leak")
            boot=memory_dump(part,0x1e00,32)
            require(boot[:8]+boot[9:] == initial_boot[:8]+initial_boot[9:] and boot[8] == seq.completed,
                    "PRNG per-batch M0 mismatch")
        _,carry=check_snapshot(parts,500,symbols,proof,ready,initial_boot)
        require(memory_dump(parts[504],0x6000,0x1100) == b"\xa6"*0x1100,"PRNG touched peripheral XDATA")
        transitions += [int(n,16) for n in re.findall(r"^0xb40000([0-9a-f]{2})\r?$",text,re.M)]
        seed_writes += [int(n,16) for n in re.findall(r"^0xbc0000([0-9a-f]{2})\r?$",text,re.M)]
        clocks += [int(n,16) for n in re.findall(r"^0xc60000([0-9a-f]{2})\r?$",text,re.M)]
        peak=max(peak,*(int(n,16) for n in re.findall(r"Max value of stack pointer=\s*0x([0-9a-f]+)",text)))
        chunks+=1
    require(transitions == [0x37]*131084 and seed_writes == list(bytes.fromhex("12341234000100031234123400010003")) and
            clocks == [0x88,0xc9] and peak<128,"PRNG actual command/seed/clock trace or upper stack guard differs")
    require(history.transition == dict(observation=129,phase=3,stage=3,run=1,index=3904,total=3912,
                                      previous_ircon=0x20,c_ircon=0x20,live_ircon=0xa0,source="live-after-c") and
            history.last_c[9] == history.last_live[9] == carry[2][0xc0-128] == 0xa0 and
            history.initial[9] == 0x20,
            "PRNG lost exact STIF C/live race or preservation across genuine continuation segments")
    commands=model(proof,3)+restore(carry)+[f"pc {ready:#x}","step 1",f"run {ready+1:#x} {proof['probe_call']:#x}"]+snapshot_commands(1)
    commands += [f"run {proof['probe_call']:#x} {fault:#x}"]+snapshot_commands(10)+["step 64"]+snapshot_commands(20)
    parts=sections(simulate(simulator,commands,path))
    r,context=check_snapshot(parts,1,symbols,proof,proof["probe_call"],initial_boot); inspect_probe(proof,r,context)
    history.observe(r,[context[2][reg-128] for reg in FLAGS])
    r,failed=check_snapshot(parts,10,symbols,proof,fault,initial_boot)
    history.observe(r,[failed[2][reg-128] for reg in FLAGS])
    require(r["reason"] == 7 and r["probe"] == [6]*3 and r["fault_latch"] == 6 and
            failed == snapshot(parts,20) and failed[0][0xbb:0xff] == carry[0][0xbb:0xff],
            "PRNG genuine stopped probe/re-entry/frozen output failure")
    require(failed[0][0x101:0x114] == carry[0][0x101:0x114],
            "PRNG stopped probe corrupted the adjacent native clock diagnostic")
    require(history.observations == 4114 and history.summary()["observations_after_transition"] == 3985,
            "PRNG stopped probe lost sticky-flag history")
    for stif,mode in ((0,"stif"),(0x80,"normal")):
        commands=model(proof,mode=mode)+initial(symbols,stif)+[f"run {symbols['_main']:#x} {before:#x}"]+snapshot_commands(1)
        for i in range(5):
            commands += ["step 1",f"run {(before if i == 0 else ready)+1:#x} {ready:#x}"]+snapshot_commands(10+10*i)
        parts=sections(simulate(simulator,commands,path))
        r,first=check_snapshot(parts,1,symbols,proof,before,initial_boot)
        short=Sequence(); flags=FlagHistory(); flags.observe(r,[first[2][reg-128] for reg in FLAGS])
        for i in range(5):
            r,memory=check_snapshot(parts,10+10*i,symbols,proof,ready,initial_boot)
            flags.observe(r,[memory[2][reg-128] for reg in FLAGS])
            short.accept(r,memory[0][0xbb:0xff])
        require(short.total == 8 and flags.last_c[9] == flags.last_live[9] == 0xa0 and
                (flags.transition is None if stif else flags.transition["source"] == "c-snapshot"),
                "PRNG initial STIF1 or actual C-execution assertion failed")
    flag_cases=0
    for reg in FLAGS:
        for bit in range(8):
            if reg == 0xc0 and bit == 7: continue
            commands=model(proof)+initial(symbols)
            # C52-only TMOD: external counter mode, with no input edges.
            # Its TCON.4/.6 timer aliases must not emulate CC2530 flag activity.
            counter_alias=reg == 0x88 and bit in (4,6)
            if counter_alias: commands += ["set memory sfr 0x89 0x44"]
            commands += [f"run {symbols['_main']:#x} {before:#x}"]+snapshot_commands(1)
            commands += [f"expression sfr[{reg:#x}]=sfr[{reg:#x}]^{1<<bit}","step 1",
                         f"run {before+1:#x} {fault:#x}"]+snapshot_commands(10)+["step 64"]+snapshot_commands(20)
            parts=sections(simulate(simulator,commands,path))
            original,initial_memory=check_snapshot(parts,1,symbols,proof,before,initial_boot)
            r,failed=check_snapshot(parts,10,symbols,proof,fault,initial_boot)
            _,later=check_snapshot(parts,20,symbols,proof,fault,initial_boot)
            if counter_alias:
                require(initial_memory[2][9] == failed[2][9] == 0x44 and
                        initial_memory[2][10:14] == failed[2][10:14],
                        "C52-only counter configuration/cells changed during flag rejection")
            changed=original["flags"].copy(); changed[FLAGS.index(reg)] ^= 1<<bit
            require(r["reason"] == 5 and r["fault_latch"] == 0 and r["initial_flags"] == original["initial_flags"] and
                    r["flags"] == changed and [failed[2][a-128] for a in FLAGS] == changed and
                    r["total"] == r["seed_calls"] == 0 and failed == later,
                    f"PRNG flag bit {reg:02x}.{bit} was masked/cleared or did not terminally reject")
            flag_cases+=1
    for stif in (0,0x80):
        commands=model(proof)+initial(symbols,stif)+[f"run {symbols['_main']:#x} {before:#x}",
                    "expression sfr[0xc0]=sfr[0xc0]|128","step 1",f"run {before+1:#x} {ready:#x}"]+snapshot_commands(1)
        commands += ["expression sfr[0xc0]=sfr[0xc0]&127","step 1",f"run {ready+1:#x} {fault:#x}"]+snapshot_commands(10)
        commands += ["step 64"]+snapshot_commands(20)
        parts=sections(simulate(simulator,commands,path))
        previous,_=check_snapshot(parts,1,symbols,proof,ready,initial_boot)
        r,failed=check_snapshot(parts,10,symbols,proof,fault,initial_boot)
        require(previous["flags"][9] == 0xa0 and r["flags"][9] == failed[2][0xc0-128] == 0x20 and
                r["initial_flags"][9] == 0x20|stif and r["reason"] == 5 and r["fault_latch"] == 0 and
                r["total"] == r["seed_calls"] == 0 and failed == snapshot(parts,20),
                "PRNG C accepted/cleared an observed STIF1->0")
        flag_cases+=1
    for mode in ("seed1","seed2","ignored","stuck","eoc","st","reserved","irq","clock"):
        commands=model(proof,mode=mode)+initial(symbols)+[f"run {symbols['_main']:#x} {fault:#x}"]+snapshot_commands(1)
        commands+=["step 64"]+snapshot_commands(10)
        parts=sections(simulate(simulator,commands,path))
        r,failed=check_snapshot(parts,1,symbols,proof,fault,initial_boot,injected_irq=mode == "irq")
        expected=8 if mode == "stuck" else 6 if mode in ("st","irq","clock") else 7
        require(r["reason"] == 4 and r["fault_latch"] == expected and failed == snapshot(parts,10),
                f"PRNG {mode} lost exact fault/output retention")
    print(f"{board}: PRNG {chunks} bounded genuine continuation segments, 4111 READY/131084 actual words, "
          f"four full32767 periods/all65534 states per clock, exact seeds/commands/tails, "
          f"STIF C/live race +3985 preserved observations, initial1/C arrival +16 words, "
          f"stopped frame +9 driver faults +{flag_cases} flag faults; "
          f"CODE/ABI/alias/upper-IRAM PASS, peakSP={peak:02x} (synthetic only).")
