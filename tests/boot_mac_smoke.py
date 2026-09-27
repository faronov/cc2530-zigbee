#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Exact installed smoke instructions; synthetic MMIO only, never equipment."""
import argparse
import json
from pathlib import Path
import re
import subprocess

import boot_banked as banking
import verify_mac_smoke as layout
from boot_banked_security import capture, captured_sections, restore, store
from boot_image import check_alias, check_pc, marker, memory_dump, simulate_binary_dumps
from boot_mac_attempt import mmio_sites
from boot_radio_tx_fixture import sections, stack_high_water
from radio_link_fixture import SETTINGS
from verify_firmware import require


def pc_in(text):
    match=re.search(r"CPU state= OK PC= 0x([0-9a-fA-F]+)",text)
    require(match is not None,"Smoke missing/failed CPU stop")
    return int(match[1],16)


def reference(output,case):
    command=[str((output/"host-smoke").resolve()),str(case),"--vector"]
    raw=subprocess.check_output(command,timeout=15)
    command[0]+="-sanitize"
    require(raw==subprocess.check_output(command,timeout=15),"Smoke native/sanitizer transcript")
    vector=json.loads(raw)
    require(vector["case"]==case and set(vector)=={"case","initial","steps"},"Smoke vector shape")
    return vector


def check_boot(parts,symbols,wait):
    check_pc(parts[59990],symbols["_main"])
    state=captured_sections(parts,60000,wait)
    require(state[0][symbols["_mac_smoke_status"]:symbols["_mac_smoke_status"]+8]==b"MAC1\x01\x40\x01\0",
            "Smoke genuine boot is not DISARMED")
    require(state[0][symbols["_mac_smoke_mailbox"]:symbols["_mac_smoke_mailbox"]+8]==bytes(8),
            "Smoke boot mailbox")
    return state


def replay(simulator,artifacts,board,vector):
    symbols,_=layout.structure(artifacts,board)
    image,_,raw,_,listings,_=artifacts
    sites=mmio_sites(image,raw.decode("ascii"),{m:v.decode("ascii") for m,v in listings.items()},handoff=True)
    wait,end,fault=(symbols["_mac_smoke_"+n] for n in ("wait","end","fault"))
    stops=[f"break {pc:#x}" for pc in (*sites,wait,end,fault,symbols["_banked_stop"])]
    model=banking.model(image)
    hardware={int(a):v for a,v in vector["initial"].items()}
    allowed_registers={a for n,a in symbols.items() if n.startswith("_SOC_")}|set(range(0x6100,0x6400))
    require(set(hardware)==allowed_registers and all(type(v) is int and 0<=v<=255 for v in hardware.values()),
            "Smoke initial peripheral input ownership")
    init=[f"set memory {'sfr' if a<256 else 'xram'} {a:#x} {v:#x}" for a,v in hardware.items()]
    commands=model+["fill iram 0 0xff 0xa5","fill xram 0 0x1eff 0xa5",
                    "fill xram 0x2000 0x7fff 0x69"]+init+stops+[
        f"run 0 {symbols['_main']:#x}",marker(59990),"state",marker(59991),
        "fill iram 0x7d 0xff 0xc7",
        f"run {symbols['_main']:#x} {wait:#x}"]+capture(60000)
    parts=sections(simulate_binary_dumps(simulator,commands))
    state=check_boot(parts,symbols,wait)
    saved_pc=wait; peak=0; count=0
    expected_media=bytearray(b"\xff"*0x40000)
    for a,v in banking.pack(image).items(): expected_media[a]=v
    owned=set(range(symbols["l_XSEG"]))|set(range(0x1e00,0x1e20))
    for step_index,step in enumerate(vector["steps"]):
        require(set(step)=={"packet","events","status"},"Smoke step shape")
        packet=bytes.fromhex(step["packet"]); expected=bytes.fromhex(step["status"])
        require(len(packet)==8 and len(expected)==64,"Smoke exact byte ABI")
        events=step["events"]
        for first in range(0,max(len(events),1),2048):
            last=min(first+2048,len(events))
            commands=model+restore(state,saved_pc,memctr=0)+stops+capture(60000)
            if first==0:
                require(saved_pc==wait,"Smoke mailbox outside WAIT")
                commands+=store("xram",symbols["_mac_smoke_mailbox"],packet)+["step 1"]
            number=10; checks=[]
            for index in range(first,last):
                kind,address,value=events[index]
                require(kind in ("r","w","c") and type(value)is int and 0<=value<=255 and
                        (address in allowed_registers if kind!="c" else address==0 and value==4),
                        "Smoke stimulus escaped MMIO")
                previous=events[index-1][:2] if index else None
                adjacent=previous is not None and previous[0]==kind=="w" and 0xa2<=previous[1]<0xa6 and address==previous[1]+1
                adjacent|=previous==["w",0xe9] and kind=="r" and address==0x6193
                if not adjacent: commands.append("run")
                commands += [marker(number),"state","dump /h sfr 0x81 0x83"]
                space="sfr" if address<256 else "xram"
                if kind=="r": commands.append(f"set memory {space} {address:#x} {value:#x}")
                commands += [marker(number+1),"step 4" if kind=="c" else "step 1"]
                if kind=="c": commands.append("state")
                elif kind=="r":
                    for dest in sorted({d for k,a,d in sites.values() if k=="r" and a in (None,address)}):
                        commands.append(f"dump /h {'iram' if dest<128 else 'sfr'} {dest:#x} {dest:#x}")
                else: commands.append(f"dump /h {space} {address:#x} {address:#x}")
                commands.append(marker(number+2)); checks.append((number,kind,address,value)); number+=3
            if last==len(events): commands.append("run")
            commands+=capture(60010)
            parts=sections(simulate_binary_dumps(simulator,commands))
            require(captured_sections(parts,60000,saved_pc)==state,"Smoke continuation changed actual machine")
            for n,kind,address,value in checks:
                pc=pc_in(parts[n])
                require(pc in sites,"Smoke unexpected stop: "+hex(pc))
                k,a,dest=sites[pc]
                require(k==kind and a in (None,address),f"Smoke MMIO mismatch {count} at {pc:x}: {sites[pc]} != {(kind,address,value)}")
                regs=memory_dump(parts[n],0x81,3)
                require(regs[0]<=0x7c,"Smoke sampled SP overflow")
                if kind=="c": check_pc(parts[n+1],pc+4)
                else:
                    if address>=256:
                        require(regs[1:]==address.to_bytes(2,"little"),"Smoke genuine DPTR")
                        if a is None: require(address in SETTINGS or 0x616a<=address<=0x6175,"Smoke indexed MMIO")
                    actual=memory_dump(parts[n+1],address if dest is None else dest,1)[0]
                    require(actual==value,f"Smoke instruction value at {pc:x}: {actual} != {value}")
                count+=1
            saved_pc=pc_in(parts[60010])
            state=captured_sections(parts,60010,saved_pc)
            peak=max(peak,stack_high_water(parts[60010]))
            require(peak<=0x7c and state[1][0x7d:]==b"\xc7"*131,"Smoke stack high-water/canary")
            require(all(v==0xa5 for a,v in enumerate(state[0]) if a not in owned),"Smoke unowned XDATA")
            require(state[4]==expected_media,"Smoke wrote flash")
            if last==len(events):
                terminal=end if expected[6]==5 else fault if expected[6]==6 else wait
                require(saved_pc==terminal,"Smoke false completion/wrong checkpoint")
                a=symbols["_mac_smoke_status"]
                require(state[0][a:a+64]==expected,
                        f"Smoke exact status step{step_index}: {state[0][a:a+64].hex()} != {expected.hex()}")
                require(state[1][symbols["_banked_depth"]]==state[1][symbols["_banked_fault"]]==0 and
                        state[2][0x1f]==1 and state[2][0x47]==0,"Smoke restored banks")
    require(saved_pc in (end,fault),"Smoke reference did not terminate")
    commands=model+restore(state,saved_pc,memctr=0)+["step 1024"]+capture(60000)
    parts=sections(simulate_binary_dumps(simulator,commands))
    repeat=captured_sections(parts,60000,saved_pc)
    require(repeat==state,"Smoke terminal loop changed owner/CPU/RAM/peripheral state")
    return count,peak


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output",type=Path,required=True)
    parser.add_argument("--board",choices=("generic","lg_esl29_rev03"),required=True)
    parser.add_argument("--simulator",default="s51")
    parser.add_argument("--case",type=int,choices=(0,1,2,3,4,5,6,7,9))
    args=parser.parse_args()
    artifacts=layout.load(args.output,args.board); layout.verify(artifacts,args.board)
    check_alias(args.simulator); banking.check_mapping(args.simulator)
    for case in (args.case,) if args.case is not None else (0,1,2,3,4,5,6,7,9):
        count,peak=replay(args.simulator,artifacts,args.board,reference(args.output,case))
        print(f"Smoke {args.board} case{case}: {count} genuine MMIO, SP{peak:02X}/7C; synthetic only.",flush=True)


if __name__=="__main__":
    main()
