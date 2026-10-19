"""Audit every __mcs51_bankcall_* thunk: decoded bank/target vs. the callee symbol's real bank."""
import sys, json
from elftools.elf.elffile import ELFFile
f = ELFFile(open(sys.argv[1], 'rb'))
symtab = f.get_section_by_name('.symtab')
banks = {}
for s in f.iter_sections():
    if s.name.startswith('.bank'):
        banks[int(s.name[5:])] = (s['sh_offset'], s['sh_addr'], s['sh_size'], s['sh_flags'])
text = f.get_section_by_name('.text'); tdata = text.data(); taddr = text['sh_addr']
# symbol -> list of (bank, addr) by section membership
def bank_of(sym):
    sec = f.get_section(sym['st_shndx']) if isinstance(sym['st_shndx'], int) else None
    if sec is None: return None
    if sec.name.startswith('.bank'): return int(sec.name[5:])
    return 'common'
funcs = {}
for s in symtab.iter_symbols():
    if s['st_info']['type'] == 'STT_FUNC' or (s['st_info']['type'] == 'STT_NOTYPE' and not s.name.startswith('.')):
        funcs.setdefault(s.name, []).append((bank_of(s), s['st_value'] & 0xffff, s['st_info']['bind']))
rows = []
for s in symtab.iter_symbols():
    if not s.name.startswith('__mcs51_bankcall_'): continue
    a = s['st_value']; o = a - taddr; b = tdata[o:o+11]
    ok_shape = b[0:3] == bytes([0xc0, 0x9f, 0x75]) and b[3] == 0x9f and b[5] == 0x12 and b[8:11] == bytes([0xd0, 0x9f, 0x22])
    nn, tgt = b[4], (b[6] << 8) | b[7]
    callee = s.name[len('__mcs51_bankcall_'):]
    cands = funcs.get(callee, [])
    match = [c for c in cands if (c[0] == nn or (c[0] == 'common' and nn == 0)) and c[1] == tgt]
    at_target = [c for c in cands if c[1] == tgt]
    rows.append(dict(thunk=hex(a), callee=callee, bind=s['st_info']['bind'], shape=ok_shape, bank=nn, target=hex(tgt),
                     ok=bool(match), callee_defs=[(c[0], hex(c[1]), c[2]) for c in cands],
                     real_bank=[c[0] for c in at_target]))
bad = [r for r in rows if not r['ok']]
print(f"thunks {len(rows)} bad {len(bad)}")
for r in bad: print(r)
json.dump(dict(thunks=rows, bad=bad), open(sys.argv[2], 'w'), indent=1)
