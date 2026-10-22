"""Verify the bank-call entry protocol for every function with incoming stack arguments.

Banked callees read stack arguments below the 3-byte trampoline frame, so every entry
must go through __mcs51_bankcall_<fn>; common callees read them right below the return
address, so they must never be entered through a trampoline.

usage: stackarg_entry_audit.py ELF MDIR OUT
"""
import json, sys, glob, collections
sys.path.insert(0, __import__("os").path.dirname(__import__("os").path.abspath(__file__))); import llvm_join_replay as R
from elftools.elf.elffile import ELFFile
elf_path, mdir, out = sys.argv[1:4]
stackarg = {}
for p in glob.glob(f"{mdir}/*.err"):
    for line in open(p):
        if line.startswith("FRAMECENSUS"):
            f = line.rstrip("\n").split("\t")
            objs = dict(x.split("=", 1) for x in f[2:] if "=" in x).get("objs", "")
            n = sum(int(o.split(":")[1]) for o in objs.split(";") if o and o.endswith(":fixed"))
            if n: stackarg[f[1]] = stackarg.get(f[1], 0) + n
e = ELFFile(open(elf_path, "rb"))
secname = {i: s.name for i, s in enumerate(e.iter_sections())}
where = {}
for s in e.get_section_by_name(".symtab").iter_symbols():
    i = s["st_shndx"]
    if not isinstance(i, int): continue
    n = secname[i]; bank = int(n[5:]) if n.startswith(".bank") else 0
    if s["st_info"]["type"] == "STT_FUNC" and not s.name.startswith("__mcs51_bankcall_"):
        where[(bank, s["st_value"])] = s.name
code = R.code_map(elf_path)
# Local trampolines may have no symbol: recognise every trampoline by shape
# (push FMAP; mov FMAP,#n; lcall F; pop FMAP; ret) and resolve F in bank n.
text = e.get_section_by_name(".text"); tdata, taddr = text.data(), text["sh_addr"]
thunk = {}
for i in range(len(tdata) - 10):
    b = tdata[i:i + 11]
    if b[0:3] == bytes([0xc0, 0x9f, 0x75]) and b[3] == 0x9f and b[5] == 0x12 and b[8:11] == bytes([0xd0, 0x9f, 0x22]):
        tgt = (b[6] << 8) | b[7]
        thunk[taddr + i] = where.get((b[4] if tgt >= 0x8000 else 0, tgt), f"?{b[4]}:{tgt:#x}")
direct, via_thunk, ptr, unresolved = collections.defaultdict(list), collections.defaultdict(list), collections.defaultdict(list), []
thunk_body = {a + 5 for a in thunk}
for key, raw in code.items():
    sb, pc = key >> 16, key & 0xffff
    op = raw[0]
    if op in (0x12, 0x02) and len(raw) == 3: t = int.from_bytes(raw[1:], "big")
    elif op & 0x1f in (0x11, 0x01) and len(raw) == 2: t = ((pc + 2) & 0xf800) | ((op >> 5) << 8) | raw[1]
    elif op == 0x90 and len(raw) == 3: t = int.from_bytes(raw[1:], "big")
    else: continue
    kind = "ptr" if op == 0x90 else "call"
    if t in thunk and t < 0x8000:
        cal = thunk[t]
        if cal in stackarg and kind == "call": via_thunk[cal].append(hex(key))
        continue
    if sb == 0 and pc in thunk_body: continue
    if sb == 0 and t >= 0x8000:
        if kind == "call" and stackarg.keys() & {where.get((b, t)) for b in range(1, 8)}: unresolved.append(hex(key))
        continue
    cal = where.get((sb if t >= 0x8000 else 0, t))
    if cal not in stackarg: continue
    if kind == "ptr": ptr[cal].append(hex(key))
    elif not (sb == 0 and pc in thunk_body): direct[cal].append(hex(key))
bank_of = {n: b for (b, a), n in where.items()}
viol = dict(
    banked_entered_directly={c: v for c, v in direct.items() if bank_of.get(c)},
    common_entered_via_thunk={c: v for c, v in via_thunk.items() if not bank_of.get(c)},
    banked_address_taken_directly={c: v for c, v in ptr.items() if bank_of.get(c)},
    common_site_into_bank_window=unresolved)
summary = dict(trampolines=len(thunk), stackarg_functions=len(stackarg),
               banked=sum(1 for c in stackarg if bank_of.get(c)), common=sum(1 for c in stackarg if c in bank_of and not bank_of[c]),
               not_linked=sum(1 for c in stackarg if c not in bank_of),
               thunk_entries=sum(len(v) for c, v in via_thunk.items() if bank_of.get(c)),
               direct_entries=sum(len(v) for c, v in direct.items() if not bank_of.get(c)),
               violations=sum(len(v) for v in viol.values()))
json.dump(dict(summary=summary, violations=viol), open(out, "w"), indent=1)
print(elf_path, summary)
for k, v in viol.items():
    if v: print(" ", k, v if isinstance(v, list) else {c: x[:3] for c, x in v.items()})
