#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Isolated deterministic PRNG proof/synthetic execution; never flash or access USB."""

import argparse
import hashlib
import re
import unittest
from pathlib import Path

from boot_image import (
    ALIAS, check_alias, check_pc, marker, memory_dump, simulate,
    snapshot_commands, verify_component_layout,
)
from boot_timebase import GUARD_SFRS
from verify_firmware import cdb_address, cdb_local, code_bytes, parse_ihex, parse_symbols, peripheral_accesses, require


IMAGE_SIZE = 1282
IMAGE_HASH = "a7292d43d7e965af5d0565a8abb708cd4ac0df9189ab7523ae9eaa9d2723ca90"
MODULE_HASH = "df20f1945e7993fcfefab707ba75ebec8474a8f949857ef9bed3608679681954"
START, END = 0x62, 0x475
LENGTHS = {op: size for size, text in (
    (1, "0f 22 33 5e 9d 9e 9f a3 c3 d3 e0 e4 eb ec ed ee ef f0 fb fc fd fe ff"),
    (2, "40 42 44 45 50 54 60 70 74 7b 7c 7d 7e 7f 80 8c 8d 8e 8f 92 94 a2 ab ac ad ae af c0 c2 d0 d2 e5 f5"),
    (3, "02 12 30 53 75 90 b5 bc bd be bf"),
) for op in bytes.fromhex(text)}
READS = (0xa8, 0xb8, 0x9a, 0xbe, 0xc6, 0x9e, 0xb4)
SITES = dict(zip(READS, (0xaa, 0xb0, 0xb6, 0xbc, 0xc2, 0xc8, 0x138)))
OBJECTS = {"fault": (0, 1), "seeded": (1, 1), "reserved_end": (0x1d, 1),
           "test_seed": (0x1e, 2), "test_output": (0x20, 2), "test_pointer": (0x22, 2),
           "test_operation": (0x24, 1), "test_limit": (0x25, 1), "test_return": (0x26, 1)}
GUARDS = GUARD_SFRS | {
    0xbe: 4, 0xb4: 0x33, 0xbc: 255, 0xbd: 255,
    0xb5: 0x40, 0xb6: 0, 0xba: 0x69, 0xbb: 0x96,
    0x98: 0, 0xc0: 0, 0xb1: 0x69, 0xb2: 0x96, 0xb3: 0x48,
    0xd1: 0, 0xd2: 0, 0xd3: 0, 0xd4: 0, 0xd5: 0, 0xd6: 0, 0xd7: 0,
    0xd9: 0x5a, 0xe1: 0xa5, 0xbf: 0,
}


def reference(state):
    """Polynomial reduction independent of the host C bit-cell model."""
    for _ in range(13):
        state <<= 1
        if state & 0x10000:
            state ^= 0x18005
    return state


def verify(image, symbols, debug, memory, listing):
    require(hashlib.sha256(code_bytes(image, IMAGE_SIZE)).hexdigest() == IMAGE_HASH,
            "PRNG complete executable/caller changed")
    allocated = verify_component_layout(image, symbols, debug, memory, "prng_test_result", ("prng.c", "test_prng.c"))
    require((cdb_address(debug, "L:Fprng$valid_state$0$0"), cdb_address(debug, "L:XG$prng_next16$0$0")+1) == (START, END) and
            hashlib.sha256(bytes(image[i] for i in range(START, END))).hexdigest() == MODULE_HASH,
            "PRNG complete module changed")
    code, pc = {}, START
    while pc < END:
        require(image[pc] in LENGTHS, "Unreviewed PRNG instruction")
        raw = bytes(image[i] for i in range(pc, pc+LENGTHS[image[pc]]))
        code[pc] = raw; pc += len(raw)
    require(pc == END, "PRNG instruction extent changed")
    listed = {int(m[1], 16): bytes.fromhex(m[2]) for m in re.finditer(
        r"^\s+([0-9A-F]{6}) ((?:[0-9A-F]{2} ){1,3})\s+\[\s*\d+\]", listing, re.M)}
    require(code == listed, "PRNG listing differs from linked CODE")
    expected = [(SITES[r], bytes((0xe5, r)), r) for r in READS]
    expected += [(pc, bytes.fromhex(raw), reg) for pc, raw, reg in (
        (0x186, "e5bc", 0xbc), (0x18c, "e5bd", 0xbd), (0x1a9, "e5c6", 0xc6), (0x1ac, "e5b4", 0xb4),
        (0x248, "8fbc", 0xbc), (0x24a, "8ebc", 0xbc), (0x396, "f5b4", 0xb4))]
    require(peripheral_accesses(code) == expected, "PRNG exact SFR sequence changed")
    calls = [(pc, int.from_bytes(raw[1:], "big")) for pc, raw in code.items() if raw[0] == 0x12]
    require(calls == [(0x1b7,0x9c), (0x1fc,0x62), (0x209,0x1a6), (0x21f,0x183), (0x250,0x183),
                      (0x25e,0x9c), (0x317,0x1a6), (0x32b,0x183), (0x33e,0x62), (0x36b,0x9c),
                      (0x3ab,0x9c), (0x3f7,0x183), (0x405,0x9c), (0x428,0x62)],
            "PRNG calls gained a helper/fallback or lost a state check")
    for name, value in (("_prng_seed_explicit",0x1d8), ("_prng_next16",0x2ac), ("_prng_test_before",END),
                        ("_prng_test_done",0x4b0), ("_main",0x4b2), ("_prng_next16_PARM_2",0x17),
                        ("_SOC_ADCCON1",0xb4), ("_SOC_RNDL",0xbc), ("_SOC_RNDH",0xbd),
                        ("s_XSEG",0), ("l_XSEG",39), ("s_SSEG",0x21), ("l_BSEG",1)):
        require(symbols.get(name) == value, "PRNG linked address/allocation changed: "+name)
    require(not any(n in symbols for n in ("__gptrget", "__gptrput", "_reference", "_polynomial")),
            "PRNG gained generic/runtime/software math")
    for name, (address, size) in OBJECTS.items():
        require(symbols["_prng_"+name] == cdb_address(debug, "L:G$prng_"+name+"$0_0$0") == address and
                set(range(address,address+size)) <= allocated, "PRNG typed caller/private allocation changed")
        sizes = re.findall(rf"^S:G\$prng_{name}\$[^(\n]+\(\{{(\d+)\}}", debug, re.M)
        require(sizes and all(int(n) == size for n in sizes), "PRNG object size changed")
    for name, address in (("previous",2), ("clock_command",4), ("adc_control",5)):
        require(cdb_address(debug, f"L:Fprng${name}$0_0$0") == address, "PRNG retained-state location changed")
    segment = listing.split(".area XSEG    (XDATA)",1)[1].split(".area XABS",1)[0]
    ranges = re.findall(r"^\s+([0-9A-F]{6})\s+\d+\s+\.ds (\d+)$", segment, re.M)
    covered = set()
    for address, size in ranges:
        area = set(range(int(address,16), int(address,16)+int(size)))
        require(area and not area & covered and area <= set(range(0x1e)), "PRNG private scratch escape/overlap")
        covered |= area
    require(covered == set(range(0x1e)), "PRNG private prefix has a hole")
    for local, shape in (("prng_seed_explicit$seed", "{2}SI:U"), ("prng_next16$output", "{2}DX,SI:U"),
                         ("prng_next16$limit", "{1}SC:U")):
        require(re.search(r"^S:Lprng\."+re.escape(local)+r"\$[^(\n]+\("+re.escape(shape),debug,re.M),
                "PRNG parameter ABI changed")
    locals_ = (("valid_state","value",6), ("observe","control",8), ("observe","ien0",10), ("observe","ien1",11),
               ("observe","ien2",12), ("observe","sleep",13), ("observe","command",14), ("observe","status",15),
               ("read_state","low",16), ("read_state","high",17), ("entry","control",18),
               ("prng_seed_explicit","seed",19), ("prng_seed_explicit","control",21), ("prng_seed_explicit","result",22),
               ("prng_next16","limit",23), ("prng_next16","output",24), ("prng_next16","control",26),
               ("prng_next16","polls",27), ("prng_next16","result",28))
    for function, local, address in locals_:
        shape = ("{2}DX,SC:U" if (function,local) == ("observe","control") else
                 "{2}DX,SI:U" if local == "output" else "{2}SI:U" if local in ("seed","value") else "{1}SC:U")
        require(cdb_local(debug,f"Lprng.{function}${local}",f"({shape}),F,0,0") == address,
                "PRNG private parameter/scratch ABI changed")
    for function in ("prng_seed_explicit","prng_next16"):
        require(f"F:G${function}$0_0$0({{2}}DF,SC:U),Z,0,0,0,0,0" in debug, "PRNG return ABI changed")
    require(bytes(image[i] for i in range(0x488,0x48b)) == b"\x12\x01\xd8" and
            bytes(image[i] for i in range(0x4a7,0x4aa)) == b"\x12\x02\xac",
            "PRNG caller no longer calls actual service")
    return allocated, code


def rejections(image, symbols, debug, memory, listing):
    case = unittest.TestCase()
    for pc in image:
        with case.assertRaises(ValueError):
            verify(image | {pc:image[pc]^1}, symbols, debug, memory, listing)
    for name in tuple("_prng_"+n for n in OBJECTS) + (
            "_SOC_ADCCON1", "_SOC_RNDL", "_SOC_RNDH", "_prng_next16_PARM_2", "_prng_seed_explicit",
            "_prng_next16", "_prng_test_before", "_prng_test_done", "_main", "s_XSEG", "l_XSEG",
            "s_SSEG", "l_BSEG", "l_PSEG", "l_XISEG", "l_XABS", "__XPAGE"):
        with case.assertRaises(ValueError, msg=name):
            verify(image, symbols | {name:symbols[name]+1}, debug, memory, listing)
    for old, new in (("({8}DA8d,SC:U)", "({9}DA8d,SC:U)"), ("({2}DX,SI:U)", "({3}DG,SI:U)"),
                     ("L:Fprng$previous$0_0$0:2", "L:Fprng$previous$0_0$0:3"),
                     ("({2}DF,SC:U),Z", "({2}DF,SI:U),Z"), ("C$prng.c$", "C$other.c$"),
                     ("C$test_prng.c$", "C$other.c$")):
        require(old in debug, "Missing PRNG ABI mutation input")
        with case.assertRaises(ValueError):
            verify(image,symbols,debug.replace(old,new),memory,listing)
    for match in re.finditer(r"^L:Lprng\.[^:\n]+:([0-9A-F]+)$",debug,re.M):
        with case.assertRaises(ValueError):
            verify(image,symbols,debug[:match.start(1)]+f"{int(match[1],16)+1:X}"+debug[match.end(1):],memory,listing)
    for old,new in (("8F BC","8F BD"), (".ds 2",".ds 3")):
        require(old in listing, "Missing PRNG listing mutation input")
        with case.assertRaises(ValueError):
            verify(image,symbols,debug,memory,listing.replace(old,new))


class Fault(Exception):
    def __init__(self, result):
        self.result = result


def execute(simulator, path, symbols, allocated, code, actions, initial=None):
    before, done = symbols["_prng_test_before"], symbols["_prng_test_done"]
    values = GUARDS | (initial or {})
    commands = [ALIAS, "fill xram 0 0x1eff 0xa5", f"run 0 {symbols['_main']:#x}", "fill iram 0x80 0xff 0xc7",
                f"run {symbols['_main']:#x} {before:#x}", "set memory xram 0x20 0x69 0x96"]
    commands += [f"set memory sfr {r:#x} {v:#x}" for r,v in values.items()]
    sites = set(SITES.values()) | {0x186,0x18c,0x1a9,0x1ac,0x248,0x24a,0x396,before,done}
    commands += [f"break {pc:#x}" for pc in sorted(sites)]
    checks, finishes = [], []
    number, current = 10, before
    state = values[0xbc] | values[0xbd]<<8
    seeded, fault, previous, output = False, 0, 0, 0x9669
    reads = writes = pending = remaining = 0
    action = {}

    def setreg(reg,value):
        values[reg] = value
        commands.append(f"set memory sfr {reg:#x} {value:#x}")

    def sync():
        setreg(0xbc,state & 255); setreg(0xbd,state>>8)

    def event(pc, observed, expected):
        nonlocal current, number
        commands.extend(([] if current == pc else ["run"]) +
                        [marker(number),"state","step 1",f"dump /h sfr {observed:#x} {observed:#x}",marker(number+1)])
        checks.append((number,pc,observed,expected))
        number += 2; current = pc+len(code[pc])

    def read(reg, pc=None):
        nonlocal reads, state, pending, remaining
        reads += 1
        if reg == 0xb4 and pending and values[reg] & 12 == 4:
            if remaining: remaining -= 1
            else:
                state = action.get("effect", reference(state)); sync()
                setreg(reg,values[reg]&0xf3); pending = 0
        for changed,value in action.get("changes",{}).get(reads,{}).items(): setreg(changed,value)
        event(SITES[reg] if pc is None else pc,0xe0,values[reg])
        return values[reg]

    def write(reg,value,pc):
        nonlocal writes, state, pending, remaining
        old = values[reg]; event(pc,reg,value); writes += 1
        if writes == action.get("ignore",0): setreg(reg,old); return
        if reg == 0xbc:
            state = ((state&255)<<8)|value; sync()
        else:
            require(value == 0x37 and not old&0x4c, "Model tried ADC/reserved/busy write")
            setreg(reg,(old&0xf3)|4); pending=1; remaining=action.get("delay",0)

    def observe(clock,adc):
        sample = [read(reg) for reg in READS[:6]]
        ien0,ien1,ien2,sleep,command,status = sample
        if ien0 or ien1 or ien2 or sleep&7 != 4 or command != status or (
                command&7 != (1 if command&0x40 else 0)) or (command&0x40 and not command&0x38):
            raise Fault(6)
        control = read(0xb4)
        if control&0x73 != 0x33: raise Fault(6)
        if command != clock or control&0xf3 != adc: raise Fault(7)
        return control

    def entry():
        clock, adc = read(0xc6,0x1a9), read(0xb4,0x1ac)&0xf3
        if observe(clock,adc)&12: raise Fault(6)
        return clock,adc

    def state_read():
        return read(0xbc,0x186) | read(0xbd,0x18c)<<8

    for index, action in enumerate(actions):
        reads = writes = 0
        if index: commands += ["step 1","run"]
        commands += [f"set memory xram 0x1e {action.get('seed',0x1234)&255} {action.get('seed',0x1234)>>8}",
                     f"set memory xram 0x22 {action.get('pointer',0x20)&255} {action.get('pointer',0x20)>>8} "
                     f"{action['op']} {action.get('limit',1)}", "step 1"]
        current = before+1
        for reg,value in action.get("initial",{}).items(): setreg(reg,value)
        try:
            if fault: raise Fault(fault)
            seed, pointer, limit = action.get("seed",0x1234), action.get("pointer",0x20), action.get("limit",1)
            if action["op"] == 0:
                if seed in (0,0x8003): raise Fault(1)
                clock,adc = entry()
                if seeded and state_read() != previous: raise Fault(7)
                write(0xbc,seed>>8,0x248); write(0xbc,seed&255,0x24a)
                value = state_read()
                if observe(clock,adc)&12 or value != seed: raise Fault(7)
                previous=value; seeded=True
            else:
                if not pointer or not limit: raise Fault(2)
                if pointer >= 0x1dff: raise Fault(3)
                if pointer <= 0x1d: raise Fault(4)
                if not seeded: raise Fault(5)
                clock,adc = entry(); value=state_read()
                if value in (0,0x8003) or value != previous: raise Fault(7)
                if observe(clock,adc)&12: raise Fault(7)
                write(0xb4,0x37,0x396)
                for _ in range(limit):
                    control=observe(clock,adc)
                    if not control&12: break
                    if control&12 != 4: raise Fault(7)
                else: raise Fault(8)
                value=state_read()
                if observe(clock,adc)&12 or value in (0,0x8003) or value == previous: raise Fault(7)
                previous=output=value
            result=0
        except Fault as error:
            result=error.result
            if result >= 6: fault=result
        require(result == action.get("result",0), "Synthetic PRNG expected-result mismatch")
        commands += ["run"]+snapshot_commands(number)
        finishes.append((number,result,dict(values),previous,output,seeded,fault,reads,writes,index,action.copy()))
        number += 4; current=done
        if action.get("late"):
            require(pending and fault, "Late effect needs a genuinely pending failed command")
            state=reference(state); sync(); setreg(0xb4,values[0xb4]&0xf3); pending=0
    text=simulate(simulator,commands,path)
    marks=list(re.finditer(r"^0x2530([0-9a-f]{4})\r?$",text,re.M))
    parts={int(left[1],16):text[left.end()+1:right.start()] for left,right in zip(marks,marks[1:])}
    require(len(parts) == len(marks)-1, "Duplicate PRNG simulator marker")
    for n,pc,reg,value in checks:
        check_pc(parts[n],pc)
        actual=memory_dump(parts[n],reg,1)
        require(actual == bytes((value,)),
                f"PRNG MMIO at{pc:04x}/marker{n} register{reg:02x}: {actual.hex()} != {value:02x}; actions={actions[:2]}")
    for n,result,regs,retained,out,used,error,loads,stores,index,act in finishes:
        check_pc(parts[n],done)
        ram=memory_dump(parts[n],0,0x1f00)
        iram=memory_dump(parts[n+1],0,256)
        sfr=memory_dump(parts[n+2],0x80,128)
        require(ram[0x1e00:0x1e08] == b"PRNG\x01\x08\0\0" and ram[0x26] == result, "PRNG result/status ABI differs")
        require(ram[:4] == bytes((error,used))+retained.to_bytes(2,"little") and ram[0x20:0x22] == out.to_bytes(2,"little"),
                "PRNG retained state/caller publication differs")
        require(ram[0x1e:0x20] == act.get("seed",0x1234).to_bytes(2,"little") and
                ram[0x22:0x26] == act.get("pointer",0x20).to_bytes(2,"little")+bytes((act["op"],act.get("limit",1))),
                "PRNG corrupted caller guards/arguments")
        require(all(b == 0xa5 for a,b in enumerate(ram) if a not in allocated), "PRNG escaped allocated nonaliased XDATA/status")
        require(iram[0x80:] == b"\xc7"*128 and sfr[1] == symbols["s_SSEG"]+1, "PRNG alias/upper-IRAM/stack leak")
        require(all(sfr[r-0x80] == value for r,value in regs.items()), "PRNG changed unrelated shared/ADC/IRQ/clock/peripheral SFR")
        if index and actions[index-1].get("result",0) >= 6:
            require(not loads and not stores, "PRNG terminal re-entry accessed MMIO")
    peaks=[int(n,16) for n in re.findall(r"Max value of stack pointer=\s*0x([0-9a-f]+)",text)]
    require(peaks and max(peaks)<128,"PRNG upper IRAM high-water violation")
    return max(peaks)


def cases():
    seed={"op":0}; step={"op":1}
    for value in (1,2,0xff,0x100,0x7fff,0x8000,0x8001,0x8002,0x8004,0xff00,0xfffe,0xffff):
        yield [seed|{"seed":value}]+[step]*4, {}
    yield [seed]+[step]*257, {0xc6:0x88,0x9e:0x88,0xb4:0xb3}
    for value in (0,0x8003): yield [seed|{"seed":value,"result":1}], {}
    yield [step|{"result":5}], {}
    for pointer,result in ((0,2),(1,4),(0x17,4),(0x1d,4),(0x1dff,3),(0x1e00,3),(0x1f00,3),(0xffff,3)):
        yield [seed,step|{"pointer":pointer,"result":result}], {}
    yield [seed,step|{"limit":0,"result":2}], {}
    for delay,limit in ((1,2),(254,255),(1,1),(255,255)):
        result=0 if delay<limit else 8
        yield [seed,step|{"delay":delay,"limit":limit,"result":result,"late":bool(result)}]+(
            [step|{"result":result},seed|{"result":result}] if result else []), {}
    for ignored in (1,2): yield [seed|{"ignore":ignored,"result":7},step|{"result":7}], {}
    yield [seed,step|{"ignore":1,"result":7},seed|{"result":7}], {}
    for effect in (0,0x8003,0x1234):
        yield [seed,step|{"effect":effect,"result":7},step|{"result":7}], {}
    for control in (0,0x03,0x13,0x23,0x32,0x37,0x3b,0x3f,0x73,0xf3):
        yield [seed|{"result":6},step|{"result":6}], {0xb4:control}
    for reg,value in ((0xa8,0x10),(0xb8,1),(0x9a,1),(0xbe,5),(0xc6,0x88),(0x9e,0x88)):
        yield [seed|{"result":6},step|{"result":6}], {reg:value}
    for read in (3,9,10,12,18,19,25,26,28,34):
        result=6 if read<=9 else 7
        yield [seed,step|{"changes":{read:{0xb4:0x3b}},"result":result},step|{"result":result}], {}
    for reg,value in ((0xbc,0),(0xbd,0)):
        yield [seed,step|{"initial":{reg:value},"result":7},seed|{"result":7}], {}
        yield [seed,seed|{"initial":{reg:value},"result":7},step|{"result":7}], {}
    for position in (9,18,25,34):
        for value,result in ((0xb3,7),(0x73,6),(0x32,6)):
            yield [seed,step|{"changes":{position:{0xb4:value}},"result":result},step|{"result":result}], {}
    for position in (3,12,19,28):
        yield [seed,step|{"changes":{position:{0xa8:1}},"result":6},seed|{"result":6}], {}
    for position in (7,16,23,32):
        yield [seed,step|{"changes":{position:{0xc6:0x88,0x9e:0x88}},"result":7},seed|{"result":7}], {}
    yield [seed,step|{"initial":{0xc6:0x88,0x9e:0x88}},seed,step], {}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output",type=Path,required=True); parser.add_argument("--simulator",default="s51")
    args=parser.parse_args(); path=args.output/"prng_test.ihx"
    image=parse_ihex(path.read_text()); symbols=parse_symbols(path.with_suffix(".map").read_text())
    debug=path.with_suffix(".cdb").read_text(); memory=path.with_suffix(".mem").read_text()
    listing=(args.output/"prng.rst").read_text()
    allocated,code=verify(image,symbols,debug,memory,listing)
    rejections(image,symbols,debug,memory,listing); check_alias(args.simulator)
    scenarios=list(cases())
    peak=max(execute(args.simulator,path,symbols,allocated,code,actions,initial) for actions,initial in scenarios)
    print(f"PRNG: {len(image)} CODE ({END-START} driver), 39 ordinary XDATA +8 result/64 reserved; "
          f"stack21..ff (223 reserved), peak{peak:02x}; {len(scenarios)} linked scenarios/"
          f"{sum(len(actions) for actions,_ in scenarios)} real-driver calls. "
          "Complete CODE/ABI/private-prefix/SFR order/alias guards PASS; synthetic deterministic LFSR, no entropy/hardware evidence.")


if __name__ == "__main__":
    main()
