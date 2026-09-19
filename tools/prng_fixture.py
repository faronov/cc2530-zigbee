# SPDX-License-Identifier: BSD-3-Clause
"""Deterministic PRNG board ABI, original-driver relocation and host-only oracle."""
import hashlib
import re

from dma_fixture import LENGTHS, layout_fields, verify_expiry
from radio_fifo_fixture import instructions
from verify_firmware import (
    cdb_address, cdb_local, code_bytes, peripheral_accesses, require, verify_clock_code,
    verify_deadline_helper, verify_timebase_reader,
)

SIZE, WORDS, LIMIT, READY_COUNT, TOTAL = 88, 32, 16, 4111, 131084
CHECKPOINTS = tuple("_prng_fixture_"+n for n in ("before", "ready", "fault"))
HASHES = {0x140: (7272, "05751be8d5c37ad4b950c80f25094f3d5908b1bf34d493cc345c15728d9e8694"),
          0x168: (7312, "b91788b7e1fd71a8bdf334d95c042dc41787211b60ffc39e1a1a009aa8a6e920")}
CLOCK_PROFILES = {
    (731, 25, 7047): ("0a85c2e292b75273f0a391d10599ec57b678dec2622ac0f07b26e005c6f01b10",
                      "7f361afab0a46b62821c363b00140e3eb20e944ec31ec53f3a357437cae7707d", 8),
    (771, 25, 7087): ("c45c48c22a9dea97b8b363392b350bcb3259bec8e404c0bbfc03bbf9f142b7aa",
                      "037359a09ff3850faeeed4d6eac67ebb386209e958989eea74ffc5df09a4eb47", 8),
}
MODULE_HASH = "df20f1945e7993fcfefab707ba75ebec8474a8f949857ef9bed3608679681954"
FIELDS = (
    ("signature",4), ("version",1), ("size",1), ("phase",1), ("reason",1), ("stage",1), ("run",1),
    ("completed",1), ("count",1), ("result",1), ("seed_result",1), ("benign",1), ("checked",1),
    ("seed",2), ("index",2), ("batch",2), ("total",3), ("seed_calls",1), ("mismatch",1),
    ("actual",2), ("expected",2), ("adc",1), ("initial_adc",1), ("command",1), ("status",1), ("sleep",1),
    ("enables",3), ("initial_sleep",1), ("initial_flags",10), ("flags",10), ("hardware",2),
    ("probe",3), ("fault_latch",1), ("clock_result",1), ("clock",19), ("guards",2),
)
FLAGS = (0xa9,0xb9,0x88,0x98,0x9b,0xe9,0x91,0xe8,0xbf,0xc0)
READS = (0xb4,0xc6,0x9e,0xbe,0xa8,0xb8,0x9a)+FLAGS+(0xbc,0xbd)
PRNG_LENGTHS = LENGTHS | {0x5e:1, 0x9d:1, 0x9e:1, 0x9f:1, 0xbc:3, 0x42:2, 0x44:2, 0xd3:1,
                          0x03:1, 0x05:2, 0x0d:1, 0x0e:1, 0x6c:1, 0x93:1, 0xc4:1, 0xcc:1, 0xd5:3}


def advance(state):
    require(type(state) is int and 0 <= state <= 65535, "Invalid host PRNG state")
    for _ in range(13):
        state <<= 1
        if state & 0x10000:
            state ^= 0x18005
    return state


def verify_relocated(image, symbols, debug):
    start = cdb_address(debug, "L:Fprng$valid_state$0$0")
    end = cdb_address(debug, "L:XG$prng_next16$0$0")+1
    require(end-start == 1043 and symbols["_prng_fault"] == 0x4f and symbols["_prng_reserved_end"] == 0x6c,
            "PRNG complete private/module extent changed")
    code = instructions(image,start,end,PRNG_LENGTHS)
    blob = bytearray(image[a] for a in range(start,end))
    for pc, raw in code.items():
        off, op = pc-start, raw[0]
        if op in (2,0x12):
            target = int.from_bytes(raw[1:],"big")
            require(start <= target < end, "PRNG external/software fallback call")
            blob[off+1:off+3] = (target-start+0x62).to_bytes(2,"big")
        if op == 0x90:
            address = int.from_bytes(raw[1:],"big")
            require(0x4f <= address <= 0x6c, "PRNG private DPTR operand escaped prefix")
            blob[off+1:off+3] = (address-0x4f).to_bytes(2,"big")
    for off in (0x2a,0x2e,0x32,0x34):
        require(blob[off] == 1 and symbols["l_BSEG"] == 3, "PRNG BIT scratch changed")
        blob[off] = 0
    require(blob[0x297] == 0x6c, "PRNG private fence immediate changed")
    blob[0x297] = 0x1d
    require(hashlib.sha256(blob).hexdigest() == MODULE_HASH, "PRNG differs from published complete1043-byte driver")
    require(symbols["_prng_seed_explicit"] == start+0x176 and symbols["_prng_next16"] == start+0x24a,
            "PRNG entry addresses changed")
    require(symbols["_prng_next16_PARM_2"] == 0x66, "PRNG map limit argument changed")
    for name, shape, address in (("prng_seed_explicit$seed","{2}SI:U",0x62),
                                 ("prng_next16$output","{2}DX,SI:U",0x67),
                                 ("prng_next16$limit","{1}SC:U",0x66)):
        require(cdb_local(debug,"Lprng."+name,f"({shape}),F,0,0") == address, "PRNG public argument ABI changed")
    for name in ("prng_seed_explicit","prng_next16"):
        require(f"F:G${name}$0_0$0({{2}}DF,SC:U),Z,0,0,0,0,0" in debug, "PRNG return ABI changed")
    return start, code


def verify_fixture(image, symbols, debug):
    require(all(n in symbols for n in CHECKPOINTS), "Missing PRNG fixture marker symbol")
    before, ready, fault = (symbols[n] for n in CHECKPOINTS)
    require(before in HASHES, "Unknown PRNG board layout")
    size, digest = HASHES[before]
    require(hashlib.sha256(code_bytes(image, size)).hexdigest() == digest,
            "PRNG complete board CODE/constants/caller changed")
    require((ready,fault) == (before+2,before+4) and
            bytes(image[a] for a in range(before,before+7)) == b"\0\x22\0\x22\0\x80\xfd",
            "PRNG marker instructions changed")
    start, code = verify_relocated(image,symbols,debug)
    require(symbols["l_XSEG"] == 320 and symbols["s_XSEG"] == 0 and symbols["s_SSEG"] == 0x21 and
            symbols["l_PSEG"] == symbols["l_XISEG"] == symbols["l_XABS"] == 0 and
            symbols["__gptrput_PARM_2"] == 0x13a, "PRNG fixture allocation/helper changed")
    objects = {"state":(0x6d,SIZE), "buffer":(0xc5,68), "probe_output":(0x109,2), "clock":(0x10b,19)}
    for name,(address,length) in objects.items():
        require(symbols["_prng_fixture_"+name] == cdb_address(debug,f"L:G$prng_fixture_{name}$0_0$0") == address,
                "PRNG caller address changed")
        sizes = re.findall(rf"^S:G\$prng_fixture_{name}\$[^(\n]+\(\{{(\d+)\}}",debug,re.M)
        require(sizes and all(int(n) == length for n in sizes) and address > 0x6c and address+length <= 0x13a,
                "PRNG caller size/private/helper exclusion changed")
    covered = set()
    for name,length in re.findall(r"^S:([^(\n]+)\(\{(\d+)\}[^\n]*\),F,0,0$",debug,re.M):
        if not (name.startswith(("Ltimebase.","Lclock.","Fclock$","Lprng.","Fprng$")) or
                re.match(r"G\$prng_(fault|seeded|reserved_end)\$",name)): continue
        if not re.search("^L:"+re.escape(name)+":",debug,re.M): continue
        address = cdb_address(debug,"L:"+name); area = set(range(address,address+int(length)))
        require(area <= set(range(0x6d)), "Clock/timebase/PRNG private scratch escaped prefix")
        covered |= area
    require(covered == set(range(0x6d)), "Clock/timebase/PRNG private prefix has a hole")
    for module in ("prng_fixture","prng_fixture_state"):
        layout_fields(debug,module,"seed_calls",FIELDS)
    verify_timebase_reader(image,symbols,debug,0x6d,SIZE)
    verify_deadline_helper(image,symbols,debug); verify_expiry(image,symbols,debug)
    _, clock_sites = verify_clock_code(image,symbols,debug)
    probe = cdb_address(debug,"L:Fprng_fixture_state$probe$0$0")
    probe_code = instructions(image,probe,cdb_address(debug,"L:XFprng_fixture_state$probe$0$0"),PRNG_LENGTHS)
    stop = probe+0x3f; call = probe+0x4b
    require(probe_code[stop] == b"\x75\xb4\x3f" and
            bytes(image[a] for a in range(stop+3,call+3)) ==
            b"\x90\0\x66\x74\x10\xf0\x90\x01\x09\x12"+symbols["_prng_next16"].to_bytes(2,"big"),
            "PRNG intentional stop/first real caller arguments changed")
    main_code = instructions(image,symbols["_main"],cdb_address(debug,"L:XG$main$0$0")+1,PRNG_LENGTHS)
    callers = [pc for pc,raw in main_code.items() if raw == b"\x12"+probe.to_bytes(2,"big")]
    require(len(callers) == 1, "PRNG genuine probe caller changed")
    reads = cdb_address(debug,"L:Fprng_fixture_state$read_state$0$0")
    read_code = instructions(image,reads,cdb_address(debug,"L:XFprng_fixture_state$read_state$0$0"),PRNG_LENGTHS)
    require([reg for _,_,reg in peripheral_accesses(read_code)] == [0xbc,0xbd],
            "Fixture readback is not non-advancing low/high CPU reads")
    snapshot=cdb_address(debug,"L:Fprng_fixture_state$snapshot$0$0")
    snapshot_code=instructions(image,snapshot,cdb_address(debug,"L:XFprng_fixture_state$snapshot$0$0"),PRNG_LENGTHS)
    require([reg for _,_,reg in peripheral_accesses(snapshot_code)] == list(READS[:-2]),
            "PRNG raw C snapshot read order changed")
    flag_check=cdb_address(debug,"L:Fprng_fixture_state$unchanged$0$0")
    require(bytes(image[a] for a in range(flag_check+6,flag_check+18)) ==
            b"\x90\0\xa8\xe0\xff\xc0\x07\x12"+snapshot.to_bytes(2,"big")+b"\xd0\x07",
            "PRNG previous raw IRCON must be retained before the next C snapshot")
    body = instructions(image,cdb_address(debug,"L:Fprng_fixture_state$put16$0$0"),
                        cdb_address(debug,"L:XG$main$0$0")+1,PRNG_LENGTHS)
    accesses = peripheral_accesses(body)
    require(all(reg in READS for _,_,reg in accesses) and
            [(pc,raw) for pc,raw,reg in accesses if raw[0] != 0xe5] == [(stop,b"\x75\xb4\x3f")],
            "Fixture gained an ADC/CRC/DMA/RF/IRQ/clock or unreviewed write")
    clock_write=clock_sites[(0x8f, 0xc6)]
    require(bytes(image[a] for a in range(clock_write,clock_write+2)) == b"\x8f\xc6",
            "PRNG fixture original clock write site changed")
    return dict(state=0x6d,buffer=0xc5,probe_output=0x109,clock=0x10b,checkpoints=[before,ready,fault],
                module_start=start,module_end=start+1043,private_end=0x6c,limit=0x66,
                seed=symbols["_prng_seed_explicit"],next=symbols["_prng_next16"],
                high_write=start+0x1e6,low_write=start+0x1e8,command=start+0x334,
                probe=probe,stop=stop,probe_call=call,probe_return=callers[0]+3,probe_sp=0x22,
                cpu_reader=reads,clock_write=clock_write,fill=cdb_address(debug,"L:Fprng_fixture_state$fill_batch$0$0"),
                flag_check=flag_check,snapshot=snapshot,wire_version=2)


def decode(data, *, running=False):
    require(isinstance(data,bytes) and len(data) == SIZE and data[:6] == b"M2PN\x02\x58" and data[-2:] == b"\x69\x96",
            "PRNG wire signature/version/size/guards invalid")
    result, offset = {}, 0
    for name,size in FIELDS:
        raw=data[offset:offset+size]; offset+=size
        result[name] = list(raw) if name in ("enables","initial_flags","flags","probe","clock") else int.from_bytes(raw,"little")
    r=result
    require(r["phase"] in ((1,2,3,4) if running else (1,3,4)) and r["reason"] <= 7 and
            (r["phase"] == 4) == bool(r["reason"]) and r["stage"] <= 6 and r["run"] <= 5 and
            r["count"] <= WORDS and r["index"] <= 32767 and r["batch"] <= 1024 and r["total"] <= TOTAL and
            r["seed_calls"] <= 8 and r["benign"] <= 15 and r["checked"] in (0,68) and
            r["fault_latch"] <= 8 and r["clock_result"] <= 9 and all(v <= 8 for v in r["probe"]) and
            all(r[n] <= 8 or r[n] == 255 for n in ("result","seed_result")) and
            r["mismatch"] in (*range(32),254,255), "PRNG wire bounds/phase/result invalid")
    if r["phase"] in (1,3):
        if not (r["reason"] == r["fault_latch"] == r["actual"] == r["expected"] == 0 and r["mismatch"] == 255 and
                r["initial_adc"] in (0x33,0xb3) and r["adc"] == r["initial_adc"] and
                r["command"] == r["status"] == (0xc9 if r["run"] < 3 or r["stage"] == 5 else 0x88) and
                r["sleep"] == r["initial_sleep"] and r["sleep"] & 7 == 4 and r["enables"] == [0]*3):
            raise ValueError("PRNG successful state/ownership invalid: "+repr(r))
        check_flags(r["initial_flags"],r["flags"])
    if r["phase"] == 1:
        require(r["stage"] == r["run"] == r["total"] == r["seed_calls"] == r["count"] == r["benign"] == 0 and
                r["completed"] == r["checked"] == r["seed"] == r["index"] == r["batch"] == 0 and
                r["probe"] == [0]*3 and r["clock_result"] == 8 and r["clock"] == [0]*18+[8] and
                r["result"] == r["seed_result"] == 255 and r["hardware"] == 0xffff, "PRNG initial record invalid")
    if r["phase"] == 3:
        require(r["benign"] == 15 and 1 <= r["stage"] <= 5, "PRNG READY stage/benign checks invalid")
        if r["stage"] == 2: require(r["seed_result"] == 0 and not r["count"], "PRNG seed READY invalid")
        if r["stage"] == 3: require(r["result"] == 0 and r["checked"] == 68 and r["count"] in (4,31,32),
                                   "PRNG batch READY invalid")
        if r["stage"] in (4,5):
            require(r["clock_result"] == 0 and r["clock"][6] == r["clock"][13] == 0 and
                    r["clock"][16:18] == [r["command"]]*2 and r["clock"][18] == 8,
                    "PRNG clock diagnostic result disagrees with READY")
        if r["stage"] == 5: require(r["run"] == 5 and r["total"] == TOTAL and r["seed_calls"] == 8 and
                                   r["index"] == 32767 and r["batch"] == 1024 and r["completed"] == 4,
                                   "PRNG ENDREADY did not complete exact corpus")
    return result


def check_flags(previous,current):
    """SWRU191F p.47/129: only sticky IRCON.STIF may assert; never deassert."""
    if not (len(previous) == len(current) == 10 and tuple(previous[:9]) == tuple(current[:9]) and
            current[9] in (previous[9],previous[9]|0x80)):
        raise ValueError(f"PRNG flags changed outside sticky STIF0->1: {list(previous)} -> {list(current)}")


class FlagHistory:
    """Ordered initial -> C -> live -> next C observations, never normalized wire bytes."""
    def __init__(self):
        self.initial=None; self.last_c=None; self.last_live=None
        self.observations=0; self.transition=None

    def observe(self,r,live_flags):
        initial=tuple(r["initial_flags"]); current=tuple(r["flags"]); live_flags=tuple(live_flags)
        require(self.initial is None or initial == self.initial,"PRNG immutable initial flags changed")
        previous=initial if self.last_live is None else self.last_live
        check_flags(previous,current); check_flags(current,live_flags)
        check_flags(initial,current)
        if not previous[9] & 0x80 and live_flags[9] & 0x80:
            self.transition={"observation":self.observations+1,
                             **{key:r[key] for key in ("phase","stage","run","index","total")},
                             "previous_ircon":previous[9],"c_ircon":current[9],"live_ircon":live_flags[9],
                             "source":"c-snapshot" if current[9] & 0x80 else "live-after-c"}
        self.initial=initial; self.last_c=current; self.last_live=live_flags; self.observations+=1

    def summary(self):
        require(self.initial is not None,"PRNG flag history has no observations")
        return {"initial_flags":list(self.initial),"last_c_flags":list(self.last_c),
                "last_live_flags":list(self.last_live),"observations":self.observations,
                "stif_transition":self.transition,
                "observations_after_transition":None if self.transition is None else
                    self.observations-self.transition["observation"]}


class Sequence:
    """Every actual word is checked; sets prove first repeat and complete disjoint cycles."""
    def __init__(self):
        self.ready=0; self.total=0; self.seeds=0; self.completed=0; self.run=0
        self.index=0; self.batch=0; self.state=0xffff; self.stage=0; self.seen=set(); self.periods=0
        self.buffer=bytes(68)

    def accept(self, r, buffer):
        require(r["phase"] == 3, "PRNG FAULT is not a sequence checkpoint")
        if self.stage == 0: expected=1
        elif self.stage in (1,4): expected=2
        elif self.stage == 2: expected=3
        elif self.stage == 3:
            end=8 if self.run%3 == 0 else 32767
            if self.index == end:
                if self.run == 5: expected=5
                else:
                    self.run+=1; self.index=self.batch=0
                    expected=4 if self.run == 3 else 2
            else: expected=2 if self.run%3 == 0 else 3
        else: raise ValueError("PRNG sequence continued after ENDREADY")
        require(r["stage"] == expected and r["run"] == self.run, "PRNG stage/run progression invalid")
        if expected != 3:
            require(buffer == self.buffer,"PRNG seed/clock/idle operation changed caller buffer")
        if expected == 2:
            self.state=(0x1234,1,3)[self.run%3]; self.seeds+=1
            require(r["seed"] == r["hardware"] == self.state, "PRNG exact seed readback mismatch")
            if self.run in (1,4): self.seen=set()
        if expected == 3:
            length=4 if self.run%3 == 0 else min(32,32767-self.index)
            require(isinstance(buffer,bytes) and len(buffer) == 68 and buffer[:2] == b"\x69\x96" and
                    buffer[-2:] == b"\xa5\x5a" and r["count"] == length, "PRNG caller count/guard invalid")
            for i in range(length):
                actual=int.from_bytes(buffer[2+2*i:4+2*i],"little")
                self.state=advance(self.state); self.index+=1; self.total+=1
                require(actual == self.state, f"PRNG actual word mismatch run{self.run} index{self.index}")
                if self.run%3:
                    require(actual not in self.seen and
                            (actual == (1 if self.run%3 == 1 else 3)) == (self.index == 32767),
                            "PRNG early repeat/wrong cycle period")
                    self.seen.add(actual)
            require(buffer[2+2*length:-2] == b"\x69\x96"*(32-length) and r["hardware"] == self.state,
                    "PRNG unfilled tail or repeated-read state mismatch")
            self.batch+=1; self.completed=(self.completed+1)&255
            self.buffer=buffer
            if self.run%3 and self.index == 32767:
                self.periods+=1
                if self.run%3 == 2:
                    require(self.seen == set(range(65536))-{0,0x8003}, "PRNG clock corpus missed valid states")
        require((r["index"],r["batch"],r["total"],r["seed_calls"],r["completed"]) ==
                (self.index,self.batch,self.total,self.seeds,self.completed), "PRNG actual counters mismatch")
        self.stage=expected; self.ready+=1
        if expected == 5: require(self.ready == READY_COUNT and self.periods == 4, "PRNG full period corpus incomplete")
