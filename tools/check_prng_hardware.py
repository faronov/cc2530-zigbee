#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Explicit manual deterministic PRNG fixture acceptance. Never flashes or repairs."""
import argparse
from dataclasses import asdict
import hashlib
import json
from pathlib import Path
import sys

from cc2530_debug import READ_CONFIG, Status
from cc_debugger import Access, Debugger, DebuggerError, PyUsbBackend, UsbAddress
from check_timebase_hardware import step_nop, wait_checkpoint
from debug_image import DebugImage, decode_bootstrap
from prng_fixture import READS, READY_COUNT, SIZE, FlagHistory, Sequence, decode
from verify_firmware import BOARDS, CODE_LIMIT, require


def validate_program(image,program,mode):
    require(mode in ("short","full","stopped"), "PRNG mode must be short, full or stopped")
    require(image.image_name == "prng_fixture", "Use the board prng_fixture, never prng_test")
    require(isinstance(program,bytes) and 0 < len(program) <= CODE_LIMIT and
            len(program) == image.metrics["image_extent_bytes"] and hashlib.sha256(program).hexdigest() == image.sha256,
            "Program differs from checked PRNG board image")
    p=image.prng_proof; before,ready,fault=p["checkpoints"]
    require(p["wire_version"] == 2 and (ready,fault) == (before+2,before+4) and
            program[before:before+7] == b"\0\x22\0\x22\0\x80\xfd" and
            program[p["high_write"]:p["low_write"]+2] == b"\x8f\xbc\x8e\xbc" and
            program[p["command"]:p["command"]+2] == b"\xf5\xb4" and
            program[p["stop"]:p["probe_call"]+3] ==
            b"\x75\xb4\x3f\x90\0\x5c\x74\x10\xf0\x90\0\xff\x12"+p["next"].to_bytes(2,"big") and
            program[p["probe_return"]-3:p["probe_return"]] == b"\x12"+p["probe"].to_bytes(2,"big") and
            program[p["flag_check"]+6:p["flag_check"]+18] ==
            b"\x90\0\x9e\xe0\xff\xc0\x07\x12"+p["snapshot"].to_bytes(2,"big")+b"\xd0\x07",
            "PRNG actual seed/command/stopped-call/frame proof differs from program")


def live(debugger):
    """Only this fixed non-mutating SFR set; no ADCH, ADC command, DMA or CRC."""
    with debugger._stopped_operation(memory_access=True) as deadline:
        require(debugger._exchange_byte(READ_CONFIG,deadline) == 0x26, "PRNG requires config26; no repair")
        with debugger._preserve_registers(deadline):
            return bytes(debugger._instruction(bytes((0xe5,r)),deadline) for r in READS)


def inspect(debugger,image,pc,history,*,probe=False):
    p=image.prng_proof
    require(debugger.read_debug_config() == 0x26,"PRNG debug configuration changed")
    status=debugger.read_debug_status(); debugger._check_status(status,active=True,halted=True)
    require(status & Status.HALT_STATUS,"PRNG halt was not a breakpoint")
    registers=debugger.read_registers()
    require(registers.pc == pc and registers.sp == 0x4f and registers.dps == 0 and not registers.psw & 0x18,
            "PRNG checkpoint PC/stack/DPS/register bank mismatch")
    r=decode(debugger.read_xdata(p["state"],SIZE),running=probe)
    boot=debugger.read_xdata(0x1e00,32); startup=decode_bootstrap(boot,image.board)
    values=live(debugger)
    require(values[1:7] == bytes((r["command"],r["status"],r["sleep"],*r["enables"])),
            "PRNG live shared-state differs from C observation")
    require(values[0] == (r["initial_adc"]|12 if probe else r["adc"]), "PRNG live ADC/RCTRL differs")
    history.observe(r,values[7:17])
    if r["phase"] in (1,3) or probe or r["reason"] == 7:
        require(int.from_bytes(values[-2:],"little") == r["hardware"],"PRNG non-advancing live state changed")
    buffer=debugger.read_xdata(p["buffer"],68) if r["phase"] in (1,3) or r["reason"] == 7 else None
    if r["phase"] == 1:
        require(buffer == bytes(68) and debugger.read_xdata(p["probe_output"],2) == bytes(2),
                "PRNG reset caller objects were not zero initialized")
    context=None
    if probe:
        frame=debugger.read_xdata(0x1f4e,2)
        require(pc == p["probe_call"] and r["phase"] == 2 and r["stage"] == 6 and
                r["total"] == 131084 and r["probe"] == [0]*3 and r["fault_latch"] == 0 and
                registers.dptr0 == p["probe_output"] and debugger.read_xdata(p["limit"],1) == b"\x10" and
                frame == p["probe_return"].to_bytes(2,"little") and
                debugger.read_xdata(p["probe_output"],2) == b"\x69\x96",
                "PRNG stopped-call arguments/complete genuine frame/state invalid")
        context={"pc":pc,"sp":registers.sp,"frame_hex":frame.hex(),"output":p["probe_output"],
                 "limit":16,"control":values[0],"state":r["hardware"],"fault_before_call":0}
    if r["reason"] == 7:
        require(r["stage"] == 6 and r["probe"] == [6]*3 and r["fault_latch"] == 6 and
                debugger.read_xdata(p["probe_output"],2) == b"\x69\x96",
                "PRNG deliberate stop did not preserve exact fault/sentinel")
    require(debugger.read_registers() == registers,"PRNG inspection altered full CPU/FMAP context")
    return r,boot,startup,registers,buffer,context


def stopped_probe(debugger,image,registers,expected_buffer,history):
    """Called only after this invocation's verified full ENDREADY."""
    p=image.prng_proof; _,ready,fault=p["checkpoints"]
    require(registers.pc == ready,"PRNG stopped probe requires ENDREADY PC")
    require(history.last_live is not None,"PRNG stopped probe requires preceding flag history")
    debugger.set_breakpoint(3,p["probe_call"])
    step_nop(debugger,registers,"PRNG"); debugger.resume()
    pc=wait_checkpoint(debugger,p["probe_call"],fault,"PRNG deliberate stop")
    require(pc == p["probe_call"],"PRNG failed before deliberate stopped-call context")
    _,_,_,registers,_,context=inspect(debugger,image,pc,history,probe=True)
    debugger.set_breakpoint(3,p["probe_call"],False); debugger.resume()
    pc=wait_checkpoint(debugger,ready,fault,"PRNG stopped rejection")
    r,boot,_,registers,buffer,_=inspect(debugger,image,pc,history)
    require(pc == fault and r["reason"] == 7 and buffer == expected_buffer,
            "PRNG negative did not reach exact expected FAULT/unchanged caller batch")
    return r,boot,registers,context


def exercise(debugger,image,program,mode="short"):
    validate_program(image,program,mode)
    adapter=debugger.read_adapter_state(); debugger.attach_reset()
    require(debugger.read_pc() == 0 and debugger.read_debug_config() == 0x26,"PRNG own reset PC/config invalid")
    core=debugger.read_registers()
    require(core.pc == 0,"PRNG reset context not PC0")
    for offset in range(0,len(program),128):
        require(debugger.read_code(offset,min(128,len(program)-offset)) == program[offset:offset+128],
                "PRNG physical CODE mismatch")
    require(debugger.read_registers() == core and debugger.read_debug_config() == 0x26,
            "PRNG full CODE preflight changed reset CPU/FMAP/config")
    p=image.prng_proof; before,ready,fault=p["checkpoints"]
    for slot in range(4): debugger.set_breakpoint(slot,before,False)
    for slot,pc in enumerate((before,ready,fault)): debugger.set_breakpoint(slot,pc)
    debugger.resume(); pc=wait_checkpoint(debugger,before,fault,"PRNG")
    history=FlagHistory()
    initial,initial_boot,startup,registers,_,_=inspect(debugger,image,pc,history)
    require(pc == before and initial["phase"] == 1 and startup["heartbeat"] == 0 and
            startup["clock_request"] == startup["clock_status"] == 0xc9,"PRNG initialization failed")
    debugger.set_breakpoint(0,before,False)
    seq=Sequence(); context=None
    for _ in range(5 if mode == "short" else READY_COUNT):
        step_nop(debugger,registers,"PRNG"); debugger.resume()
        pc=wait_checkpoint(debugger,ready,fault,"PRNG")
        r,boot,_,registers,buffer,_=inspect(debugger,image,pc,history)
        require(pc == ready,"PRNG terminal failure: "+json.dumps(r,sort_keys=True))
        seq.accept(r,buffer)
        require(boot[:8]+boot[9:] == initial_boot[:8]+initial_boot[9:] and boot[8] == seq.completed and
                r["initial_flags"] == initial["initial_flags"] and r["initial_adc"] == initial["initial_adc"] and
                r["initial_sleep"] == initial["initial_sleep"],"PRNG retained startup/ownership history changed")
    if mode == "stopped":
        r,boot,registers,context=stopped_probe(debugger,image,registers,seq.buffer,history); pc=registers.pc
        require(r["total"] == seq.total and
                boot == initial_boot[:8]+bytes((seq.completed,))+initial_boot[9:],
                "PRNG negative did not reach exact stable expected FAULT")
    return {"evidence":"hardware-observed","scope":"explicitly-seeded-deterministic-prng-not-entropy",
            "board":image.board,"image_sha256":image.sha256,"adapter":asdict(adapter),"mode":mode,
            "verified_code_bytes":len(program),"reset_pc":0,"debug_config":0x26,"preserved_reset_fmap":core.bank,
            "ready_stages":seq.ready,"checked_words":seq.total,"completed_periods":seq.periods,
            "valid_states_per_clock":65534 if seq.periods == 4 else None,"seed_loads":seq.seeds,
            "heartbeat":seq.completed,"probe_context":context,"flag_history":history.summary(),
            "final_record":r,"final_pc":pc,
            "final_cpu":"halted-at-prng-fault" if mode == "stopped" else "halted-at-prng-ready",
            "register_preservation":True,"automatic_recovery":False,
            "not_tested":["entropy/security randomness","RF/noise seeding","ADC/CRC/DMA/AES/flash/sleep/ISR",
                          "physical stuck/late PRNG or poll-cap failure","calibrated timing or throughput"]}


def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bus",type=int,required=True); parser.add_argument("--address",type=int,required=True)
    parser.add_argument("--board",choices=tuple(BOARDS),required=True); parser.add_argument("--output",type=Path,required=True)
    parser.add_argument("--mode",choices=("short","full","stopped"),default="short")
    parser.add_argument("--confirm-prng-test",action="store_true")
    args=parser.parse_args(argv)
    try:
        require(args.confirm_prng_test,"Manual PRNG acceptance requires --confirm-prng-test")
        address=UsbAddress(args.bus,args.address)
        image=DebugImage(args.output,args.board,"prng_fixture"); program=(args.output/"prng_fixture.bin").read_bytes()
        validate_program(image,program,args.mode)
        with Debugger(PyUsbBackend.load(),Access.RESET_DEBUG_SESSION,timeout_ms=10_000,
                      allow_cpu_control=True,allow_target_reset=True,allow_memory_access=True,allow_breakpoints=True) as debugger:
            debugger.open(address); result=exercise(debugger,image,program,args.mode)
    except (DebuggerError,ValueError,OSError,KeyError) as error:
        print(f"prng-hardware-check: {error}",file=sys.stderr); return 1
    print(json.dumps(result,sort_keys=True)); return 0


if __name__ == "__main__":
    sys.exit(main())
