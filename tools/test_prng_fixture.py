# SPDX-License-Identifier: BSD-3-Clause
"""Independent synthetic wire/runner/USB cases; physical devices are never opened."""
from contextlib import contextmanager, redirect_stderr, redirect_stdout
from dataclasses import replace
from functools import lru_cache
import hashlib
from io import StringIO
import json
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import check_prng_hardware as runner
import debug_image
from cc2530_debug import READ_CONFIG, READ_STATUS
from cc_debugger import Access, Debugger, DebuggerError, State, TransportError, UsbAddress
from prng_fixture import FIELDS, FLAGS, READS, FlagHistory, Sequence, advance, check_flags, decode
from test_dma_fixture import ControllerBackend
from test_m1_transport import Clock
from test_timebase_hardware import FixtureDebugger

BEFORE, READY, FAULT, PROBE = 0x140,0x142,0x144,0x54b
P = dict(state=0x63,buffer=0xbb,probe_output=0xff,limit=0x5c,checkpoints=[BEFORE,READY,FAULT],
         high_write=0x340,low_write=0x342,command=0x400,stop=0x53f,probe_call=PROBE,
         probe=0x500,probe_return=0x623,probe_sp=0x4f,next=0x800,
         flag_check=0x900,snapshot=0x920,wire_version=2)
program=bytearray(bytes(range(256))*16)
program[BEFORE:BEFORE+7]=b"\0\x22\0\x22\0\x80\xfd"
program[0x340:0x344]=b"\x8f\xbc\x8e\xbc"; program[0x400:0x402]=b"\xf5\xb4"
program[0x53f:0x54e]=b"\x75\xb4\x3f\x90\0\x5c\x74\x10\xf0\x90\0\xff\x12\x08\0"
program[0x620:0x623]=b"\x12\x05\0"
program[0x906:0x912]=b"\x90\0\x9e\xe0\xff\xc0\x07\x12\x09\x20\xd0\x07"
PROGRAM=bytes(program)


def image_fixture():
    return SimpleNamespace(image_name="prng_fixture",board="generic",prng_proof=P.copy(),
        sha256=hashlib.sha256(PROGRAM).hexdigest(),metrics={"image_extent_bytes":len(PROGRAM),"iram_stack_start":0x4e})


def encode(r):
    return b"".join(bytes(r[n]) if isinstance(r[n],list) else r[n].to_bytes(size,"little") for n,size in FIELDS)


def transition(value):
    for _ in range(13):
        feedback=(value>>15)&1
        value=((value<<1)&65535) ^ (feedback<<15) ^ (feedback<<2) ^ feedback
    return value


@lru_cache(maxsize=1)
def records():
    r={n:0 for n,_ in FIELDS}
    r.update(signature=int.from_bytes(b"M2PN","little"),version=2,size=88,phase=1,result=255,seed_result=255,
             mismatch=255,adc=0x33,initial_adc=0x33,command=0xc9,status=0xc9,sleep=0x84,initial_sleep=0x84,
             enables=[0]*3,initial_flags=[0x31,14,3,3,3,0x55,0xaa,0x12,0x37,0x20],
             flags=[0x31,14,3,3,3,0x55,0xaa,0x12,0x37,0x20],hardware=65535,probe=[0]*3,
             clock_result=8,clock=[0]*18+[8],guards=0x9669)
    out=[]; buffer=bytes(68)
    def save(): out.append((encode(r),buffer))
    save(); r.update(phase=3,stage=1,benign=15,result=1); save()
    for run in range(6):
        r.update(run=run,index=0,batch=0)
        if run == 3:
            r.update(stage=4,command=0x88,status=0x88,clock_result=0,clock=[0,0,0,0,1,0,0]+[0]*7+[0xc9,0x88,0x88,0x88,8])
            save()
        value=(0x1234,1,3)[run%3]
        for repetition in range(2 if run%3 == 0 else 1):
            value=(0x1234,1,3)[run%3]
            r.update(stage=2,seed=value,hardware=value,seed_result=0,count=0,checked=0,seed_calls=r["seed_calls"]+1)
            save()
            remaining=4 if run%3 == 0 else 32767
            while remaining:
                count=min(32,remaining); words=[]
                for _ in range(count):
                    value=transition(value); words.append(value)
                buffer=b"\x69\x96"+b"".join(v.to_bytes(2,"little") for v in words)+b"\x69\x96"*(32-count)+b"\xa5\x5a"
                r.update(stage=3,index=r["index"]+count,batch=r["batch"]+1,total=r["total"]+count,
                         completed=(r["completed"]+1)&255,count=count,checked=68,hardware=value,result=0)
                save(); remaining-=count
    r.update(stage=5,command=0xc9,status=0xc9,clock=[0,0,0,0,1,0,0]+[0]*7+[0x88,0xc9,0xc9,0xc9,8])
    save()
    assert len(out)==4112
    return tuple(out)


@lru_cache(maxsize=1)
def synthetic_registers(raw):
    # Only immutable fake wire bytes are cached, never a real inspection or result.
    r=decode(raw,running=True)
    return bytes((r["adc"],r["command"],r["status"],r["sleep"],
                  *r["enables"],*r["flags"]))+r["hardware"].to_bytes(2,"little")


class PrngDebugger(FixtureDebugger):
    def __init__(self):
        super().__init__()
        self.registers=replace(self.registers,bank=1,dps=0,sp=0x4f,dptr0=0xff)
        self.cursor=0; self.breakpoints={}; self.config=0x26
        self.bad_sfr=self.bad_memory=None
        self.bad_sfr_bit=1; self.initial_stif=False
        self.stif_c_from=self.stif_c_until=self.stif_live_from=None

    def observation(self):
        return 4112 if self.pc == PROBE else 4113 if self.pc == FAULT else self.cursor

    def read_code(self,address,length):
        self.event("code",address,length)
        if self.corrupt_cpu: self.registers=replace(self.registers,b=(self.registers.b+1)&255)
        return bytes(length) if self.corrupt_code else PROGRAM[address:address+length]

    def set_breakpoint(self,slot,address,enabled=True):
        self.event("breakpoint",slot,address,enabled); self.breakpoints[slot]=address if enabled else None

    def read_debug_config(self):
        self.event("config"); return self.config

    def read_debug_status(self):
        self.event("read-status"); return 0x2a

    def resume(self):
        self.event("resume")
        if self.pc == 0: self.pc=BEFORE
        elif self.pc == PROBE: self.pc=FAULT
        elif self.cursor == 4111: self.pc=PROBE
        else:
            self.cursor+=1; self.pc=READY
        if self.bad_pc is not None: self.pc=self.bad_pc

    def record(self):
        raw,buffer=records()[self.cursor]
        raw=bytearray(raw)
        if self.initial_stif:
            raw[49] |= 0x80; raw[59] |= 0x80
        if self.stif_c_from is not None and self.observation() >= self.stif_c_from and (
                self.stif_c_until is None or self.observation() < self.stif_c_until):
            raw[59] |= 0x80
        if self.pc in (PROBE,FAULT):
            raw[6]=2; raw[8]=6
            if self.pc == FAULT:
                raw[6]=4; raw[7]=7; raw[62:66]=bytes([6]*4); raw[31]=0x3f
        raw=bytes(raw)
        if self.corrupt_record and self.cursor: raw=self.corrupt_record(raw)
        return raw,buffer

    def read_xdata(self,address,length):
        self.event("xdata",address,length)
        raw,buffer=self.record()
        if address == 0x63: value=raw
        elif address == 0xbb: value=buffer
        elif address == 0xff: value=b"\x69\x96" if self.cursor else bytes(2)
        elif address == 0x5c: value=b"\x10"
        elif address == 0x1f4e: value=P["probe_return"].to_bytes(2,"little")
        else:
            assert address == 0x1e00
            value=b"M0CC\x01\x20\x02\0"+bytes((raw[10],))+bytes(15)+b"\xc9\xc9"+bytes(6)
            if self.corrupt_boot: value=self.corrupt_boot(value)
        assert len(value)==length
        if self.bad_memory and self.bad_memory[0] == address:
            at=self.bad_memory[1]; value=value[:at]+bytes((value[at]^1,))+value[at+1:]
        return value

    @contextmanager
    def _stopped_operation(self,*,memory_access=False):
        self.event("stopped",memory_access); yield self.deadline

    @contextmanager
    def _preserve_registers(self,deadline):
        self.event("preserve"); yield
        self.event("preserved")

    def _exchange_byte(self,command,deadline):
        self.event("exchange",command)
        assert command in (READ_CONFIG,READ_STATUS)
        return self.config if command == READ_CONFIG else 0x2a

    def _instruction(self,command,deadline):
        self.event("instruction",command)
        assert command[0]==0xe5 and command[1] in READS
        values=synthetic_registers(self.record()[0])
        value=values[READS.index(command[1])]
        if self.pc == PROBE and command[1] == 0xb4: value=0x3f
        if command[1] == 0xc0 and self.stif_live_from is not None and self.observation() >= self.stif_live_from:
            value |= 0x80
        return value^self.bad_sfr_bit if command[1] == self.bad_sfr else value


class PrngTests(unittest.TestCase):
    args=["--board","generic","--bus","1","--address","2","--output","unused"]

    def invoke(self,extra):
        out,err=StringIO(),StringIO()
        with redirect_stdout(out),redirect_stderr(err): result=runner.main(self.args+extra)
        return result,out.getvalue(),err.getvalue()

    def test_fake_byte_updates_match_decoded_reference_for_entire_corpus(self):
        d=PrngDebugger()
        for cursor,(raw,buffer) in enumerate(records()):
            d.cursor=cursor; d.pc=BEFORE if cursor == 0 else READY
            self.assertEqual(d.record(),(raw,buffer))
            r=decode(raw)
            for initial in (False,True):
                d.initial_stif=initial; d.stif_c_from=cursor
                expected=decode(raw); expected["flags"][9] |= 0x80
                if initial: expected["initial_flags"][9] |= 0x80
                self.assertEqual(d.record(),(encode(expected),buffer))
            d.initial_stif=False; d.stif_c_from=None
            for pc in (PROBE,FAULT):
                d.pc=pc
                expected=dict(r,phase=2,stage=6)
                if pc == FAULT: expected.update(phase=4,reason=7,probe=[6]*3,fault_latch=6,adc=0x3f)
                self.assertEqual(d.record(),(encode(expected),buffer))

    def test_synthetic_register_cache_observes_changed_and_invalid_bytes(self):
        raw=records()[0][0]; original=synthetic_registers(raw)
        changed=bytearray(raw); changed[59] |= 0x80
        self.assertEqual(synthetic_registers(bytes(changed)),original[:16]+bytes([original[16]|0x80])+original[17:])
        changed[0]=0
        with self.assertRaises(ValueError): synthetic_registers(bytes(changed))
        self.assertEqual(synthetic_registers(raw),original)
        d=PrngDebugger()
        d.corrupt_record=lambda data:data[:31]+b"\0"+data[32:]
        d.cursor=1; d.pc=READY
        with self.assertRaises(ValueError): d._instruction(b"\xe5\xb4",d.deadline)

    def test_independent_full_word_and_first_repeat_corpus(self):
        for i in range(65536): self.assertEqual(transition(i),advance(i))
        seq=Sequence()
        for raw,buffer in records()[1:]: seq.accept(decode(raw),buffer)
        self.assertEqual((seq.ready,seq.total,seq.periods,seq.completed),(4111,131084,4,4))

    def test_full_stopped_and_short_runner_modes(self):
        for mode in ("short","full","stopped"):
            d=PrngDebugger(); r=runner.exercise(d,image_fixture(),PROGRAM,mode)
            self.assertEqual(r["checked_words"],8 if mode=="short" else 131084)
            self.assertEqual(r["seed_loads"],2 if mode=="short" else 8)
            self.assertEqual(r["final_pc"],FAULT if mode=="stopped" else READY)
            first=d.events.index(("resume",))
            self.assertEqual([e for e in d.events[:first] if e[0]=="code"],
                             [("code",i,128) for i in range(0,len(PROGRAM),128)])
            self.assertEqual(d.events.count(("attach-reset",)),1)
            self.assertFalse(r["automatic_recovery"])
            self.assertTrue(all(e[1][1] in READS for e in d.events if e[0]=="instruction"))

    def test_every_short_and_negative_suffix_io_failure_stops_without_following_io(self):
        for negative in (False,True):
            def prepare():
                d=PrngDebugger()
                if negative: d.pc=READY; d.cursor=4111
                return d
            def run(d):
                if negative:
                    history=FlagHistory(); r=decode(records()[-1][0]); history.observe(r,r["flags"])
                    return runner.stopped_probe(d,image_fixture(),replace(d.registers,pc=READY),records()[-1][1],history)
                return runner.exercise(d,image_fixture(),PROGRAM)
            reference=prepare(); run(reference)
            for boundary in range(1,len(reference.events)+1):
                d=prepare(); d.fail_at=boundary
                with self.subTest(negative=negative,boundary=boundary), self.assertRaises(TransportError): run(d)
                self.assertEqual(d.events,reference.events[:boundary])

    def test_all_probe_frame_argument_and_live_sfr_mutations_fail_before_call(self):
        for address,length in ((0x1f4e,2),(0xff,2),(0x5c,1)):
            for i in range(length):
                d=PrngDebugger(); d.cursor=4111; d.pc=PROBE; d.bad_memory=(address,i)
                with self.subTest(address=address,index=i),self.assertRaises(ValueError):
                    runner.inspect(d,image_fixture(),PROBE,FlagHistory(),probe=True)
                self.assertNotIn(("resume",),d.events)
        for reg in READS:
            d=PrngDebugger(); d.cursor=4111; d.pc=PROBE; d.bad_sfr=reg
            with self.subTest(reg=reg),self.assertRaises(ValueError):
                runner.inspect(d,image_fixture(),PROBE,FlagHistory(),probe=True)

    def test_stif_ordered_c_live_races_initial_probe_and_final_fault(self):
        for point,source in ((0,"live-after-c"),(0,"c-snapshot"),(3,"live-after-c"),
                             (3,"c-snapshot"),(128,"live-after-c"),(4112,"live-after-c"),
                             (4112,"c-snapshot"),(4113,"live-after-c")):
            d=PrngDebugger(); d.stif_c_from=point+(source=="live-after-c"); d.stif_live_from=point
            mode="short" if point < 5 else "stopped"
            with self.subTest(point=point,source=source):
                r=runner.exercise(d,image_fixture(),PROGRAM,mode)
                h=r["flag_history"]; event=h["stif_transition"]
                self.assertEqual((event["observation"],event["source"]),(point+1,source))
                self.assertEqual((event["previous_ircon"],event["c_ircon"],event["live_ircon"]),
                                 (0x20,0x20 if source=="live-after-c" else 0xa0,0xa0))
                self.assertEqual(h["last_live_flags"][-1],0xa0)
                self.assertEqual(h["initial_flags"][-1],0x20)
                self.assertEqual(h["observations_after_transition"],h["observations"]-point-1)
                self.assertEqual(r["checked_words"],8 if mode=="short" else 131084)
                if point == 4113: self.assertEqual(r["final_record"]["flags"][-1],0x20)
        d=PrngDebugger(); d.initial_stif=True
        h=runner.exercise(d,image_fixture(),PROGRAM)["flag_history"]
        self.assertEqual(h["initial_flags"][-1],0xa0); self.assertIsNone(h["stif_transition"])
        self.assertEqual(h["last_c_flags"],h["last_live_flags"])

    def test_stif_deassertion_and_stale_c_after_prior_live_assertion_are_terminal(self):
        for previous_live in (False,True):
            d=PrngDebugger()
            d.stif_c_from=3 if previous_live else 2; d.stif_c_until=3
            d.stif_live_from=2 if previous_live else None
            with self.subTest(previous_live=previous_live),self.assertRaisesRegex(ValueError,"sticky STIF"):
                runner.exercise(d,image_fixture(),PROGRAM)
            self.assertEqual(d.cursor,3)
            self.assertNotEqual(d.events[-1][0],"resume")
            self.assertNotEqual(d.events[-1],("xdata",0xbb,68))

    def test_every_other_flag_bit_and_immutable_initial_flags_remain_strict(self):
        raw=records()[0][0]
        for flag,reg in enumerate(FLAGS):
            for bit in range(8):
                r=decode(raw); history=FlagHistory(); history.observe(r,r["flags"])
                r["initial_flags"][flag] ^= 1<<bit; r["flags"][flag] ^= 1<<bit
                with self.subTest(flag=flag,bit=bit),self.assertRaisesRegex(ValueError,"immutable"):
                    history.observe(r,r["flags"])
                if flag == 9 and bit == 7: continue
                data=raw[:50+flag]+bytes((raw[50+flag]^(1<<bit),))+raw[51+flag:]
                with self.assertRaises(ValueError): decode(data)
                d=PrngDebugger(); d.bad_sfr=reg; d.bad_sfr_bit=1<<bit
                with self.assertRaisesRegex(ValueError,"sticky STIF"): runner.exercise(d,image_fixture(),PROGRAM)
                self.assertEqual(d.cursor,0)

    def test_all_ircon_transitions_and_v1_wire_rejection(self):
        for previous in range(256):
            for current in range(256):
                if current in (previous,previous|0x80): check_flags([0]*9+[previous],[0]*9+[current])
                else:
                    with self.assertRaises(ValueError): check_flags([0]*9+[previous],[0]*9+[current])
        raw=records()[0][0]
        with self.assertRaisesRegex(ValueError,"version"): decode(raw[:4]+b"\1"+raw[5:])

    def test_probe_and_fault_cannot_drop_preceding_stif_history(self):
        for until in (4112,4113):
            d=PrngDebugger(); d.cursor=4111; d.pc=READY; d.stif_c_from=4111; d.stif_c_until=until
            r=decode(d.record()[0]); history=FlagHistory(); history.observe(r,r["flags"])
            with self.subTest(until=until),self.assertRaisesRegex(ValueError,"sticky STIF"):
                runner.stopped_probe(d,image_fixture(),replace(d.registers,pc=READY),records()[-1][1],history)
            self.assertEqual(d.pc,PROBE if until==4112 else FAULT)
            self.assertNotEqual(d.events[-1][0],"resume")

    def test_every_caller_byte_and_wire_bounds_reject(self):
        for i in range(68):
            d=PrngDebugger(); d.bad_memory=(0xbb,i)
            with self.subTest(i=i),self.assertRaises(ValueError): runner.exercise(d,image_fixture(),PROGRAM)
        raw=records()[0][0]
        for index in (0,4,5,6,7,8,9,10,11,12,13,14,15,16,18,20,22,25,26,27,29,31,32,33,34,35,36,37,38,39,50,60,62,65,66,67,85,86,87):
            data=raw[:index]+bytes((raw[index]^0x80,))+raw[index+1:]
            with self.subTest(index=index),self.assertRaises(ValueError): decode(data)
        for value in (b"",raw[:-1],raw+b"\0",bytearray(raw)):
            with self.assertRaises(ValueError): decode(value)

    def test_wrong_code_config_pc_cpu_nop_and_boot_are_terminal(self):
        for field,value in (("config",0x22),("bad_pc",0x700),("corrupt_code",True),("corrupt_cpu",True),("bad_step",True)):
            d=PrngDebugger(); setattr(d,field,value)
            with self.subTest(field=field),self.assertRaises(ValueError): runner.exercise(d,image_fixture(),PROGRAM)
        d=PrngDebugger(); d.corrupt_boot=lambda b:b[:24]+b"\0"+b[25:]
        with self.assertRaises(ValueError): runner.exercise(d,image_fixture(),PROGRAM)

    def test_actual_opcode_validation_cannot_be_bypassed_by_matching_mutated_hash(self):
        for index in (BEFORE,0x340,0x342,0x400,0x53f,0x547,0x54b,0x620,*range(0x906,0x912)):
            changed=PROGRAM[:index]+bytes((PROGRAM[index]^1,))+PROGRAM[index+1:]
            image=image_fixture(); image.sha256=hashlib.sha256(changed).hexdigest()
            for mode in ("short","full","stopped"):
                with self.subTest(index=index,mode=mode),self.assertRaises(ValueError):
                    runner.validate_program(image,changed,mode)

    def test_preflight_binds_relocated_operands_to_verified_storage(self):
        for field in ("limit", "probe_output", "state"):
            image=image_fixture(); image.prng_proof=dict(image.prng_proof)
            image.prng_proof[field] += 10
            for mode in ("short","full","stopped"):
                with self.subTest(field=field,mode=mode),self.assertRaises(ValueError):
                    runner.validate_program(image,PROGRAM,mode)
        image=image_fixture(); image.prng_proof=dict(image.prng_proof)
        for field in ("limit", "probe_output", "state"):
            image.prng_proof[field] += 10
        p=image.prng_proof; program=bytearray(PROGRAM)
        for address, value in ((p["stop"]+4,p["limit"]), (p["stop"]+10,p["probe_output"]),
                               (p["flag_check"]+7,p["state"]+59)):
            program[address:address+2]=value.to_bytes(2,"big")
        image.sha256=hashlib.sha256(program).hexdigest()
        for mode in ("short","full","stopped"):
            runner.validate_program(image,bytes(program),mode)

    def test_cli_offline_prevalidation_and_success_only_after_cleanup(self):
        for extra in ([],["--confirm-prng-test","--bus","0"]):
            with patch.object(runner.PyUsbBackend,"load") as load:
                self.assertEqual(self.invoke(extra)[:2],(1,"")); load.assert_not_called()
        for body in (b"",PROGRAM[:-1],PROGRAM+b"\0"):
            with patch.object(runner,"DebugImage",return_value=image_fixture()),patch.object(runner.Path,"read_bytes",return_value=body), \
                    patch.object(runner.PyUsbBackend,"load") as load:
                self.assertEqual(self.invoke(["--confirm-prng-test"])[:2],(1,"")); load.assert_not_called()
        for cleanup in (None,DebuggerError("release failed")):
            with patch.object(runner,"DebugImage",return_value=image_fixture()),patch.object(runner.Path,"read_bytes",return_value=PROGRAM), \
                    patch.object(runner.PyUsbBackend,"load") as load,patch.object(runner,"Debugger") as ctor, \
                    patch.object(runner,"exercise",return_value={"evidence":"synthetic-test"}):
                ctor.return_value.__exit__.return_value=False; ctor.return_value.__exit__.side_effect=cleanup
                status,out,err=self.invoke(["--confirm-prng-test"])
                ctor.assert_called_once_with(load.return_value,Access.RESET_DEBUG_SESSION,timeout_ms=10_000,
                    allow_cpu_control=True,allow_target_reset=True,allow_memory_access=True,allow_breakpoints=True)
                if cleanup: self.assertEqual((status,out),(1,"")); self.assertIn("release failed",err)
                else: self.assertEqual((status,json.loads(out)),(0,{"evidence":"synthetic-test"}))

    def test_offline_state_and_proof_commands_require_matching_image(self):
        for command in ("prng-state","prng-checkpoints"):
            for image in ("prng_fixture","aes_fixture"):
                args=[command,"--board","generic","--image",image,"--output","unused"]
                if command=="prng-state": args+=["--hex",records()[0][0].hex()]
                out,err=StringIO(),StringIO()
                with patch.object(debug_image,"DebugImage",return_value=image_fixture()),redirect_stdout(out),redirect_stderr(err):
                    result=debug_image.main(args)
                self.assertEqual(result,0 if image=="prng_fixture" else 1)


class PrngReadTests(unittest.TestCase):
    def session(self,bank=0,dps=0,memory=True):
        clock=Clock(); backend=ControllerBackend(clock,bank,dps); backend.config=0x26
        backend.sfr.update({r:(r*3)&255 for r in READS})
        d=Debugger(backend,Access.EXISTING_DEBUG_SESSION,10,clock,allow_memory_access=memory)
        d.open(UsbAddress(1,2)); backend.calls.clear()
        return d,backend

    def test_fixed_reads_preserve_full_cpu_memory_and_banks(self):
        for bank in range(4):
            for dps in range(2):
                d,b=self.session(bank,dps); before=b.cpu_image(),bytes(b.ram)
                self.assertEqual(runner.live(d),bytes(b.sfr[r] for r in READS))
                self.assertEqual((b.cpu_image(),bytes(b.ram)),before)
                reads=[p[3] for p in b.packets() if len(p)==4 and p[:3]==b"\x7f\x56\xe5" and p[3] in READS]
                self.assertEqual(reads,list(READS)); d.close()

    def test_every_wrong_config_and_missing_permission_precedes_sfr_instruction(self):
        d,b=self.session(memory=False)
        with self.assertRaises(DebuggerError): runner.live(d)
        self.assertEqual(b.calls,[]); d.close()
        for config in range(256):
            if config==0x26: continue
            d,b=self.session(); b.config=config
            with self.subTest(config=config),self.assertRaises(ValueError): runner.live(d)
            self.assertFalse(any(p[:2] in (b"\x7f\x56",b"\xaf\x57",b"\x4f\x55") for p in b.packets())); d.close()

    def test_every_usb_failure_and_late_effect_has_no_restore_retry_or_following_io(self):
        d,b=self.session(); runner.live(d); count=len(b.calls); d.close()
        for late in (False,True):
            for boundary in range(1,count+1):
                d,b=self.session()
                if late: b.late_at=boundary
                else: b.fail_at=boundary
                with self.subTest(late=late,boundary=boundary),self.assertRaises(TransportError): runner.live(d)
                self.assertEqual(d.state,State.FAULTED); self.assertEqual(len(b.calls),boundary)
                with self.assertRaises(DebuggerError): runner.live(d)
                self.assertEqual(len(b.calls),boundary); d.close(); self.assertEqual(b.calls[-1],("close",))
