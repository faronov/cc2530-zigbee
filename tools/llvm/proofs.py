#!/usr/bin/env python3
"""Final-machine-code admission proofs for the LLVM/MCS51 hardware candidate.

Everything is derived from the linked ELF, its lld map and the objects that were
linked (startup, runtime).  Fails closed (exit 1) on any violated requirement.
usage: proofs.py OUTDIR TAG REPORT.json
"""
import collections, hashlib, json, re, sys, os
from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import llvm_image as LI

OUT, TAG, REPORT = sys.argv[1:4]
ELF = f'{OUT}/link/{TAG}.elf'; MAP = f'{OUT}/link/{TAG}.map'
e = ELFFile(open(ELF, 'rb'))
secs = {s.name: s for s in e.iter_sections()}
sym = {}
for s in secs['.symtab'].iter_symbols():
    if s.name and s.name not in sym: sym[s.name] = s
fail = []
def need(cond, msg):
    if not cond: fail.append(msg)
    return cond
BANK = 0x8000; NV_START = 0x3E800

# ---------------------------------------------------------------- map
outs, ins = {}, collections.defaultdict(list); cur = None
for line in open(MAP):
    m = re.match(r'^\s*([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+(\d+) (\s*)(\S.*)$', line.rstrip('\n'))
    if not m: continue
    vma, lma, size, _al, ind, rest = int(m[1], 16), int(m[2], 16), int(m[3], 16), m[4], len(m[5]), m[6]
    if ind == 0: cur = rest; outs[cur] = (vma, lma, size)
    elif ind == 8 and cur: ins[cur].append((vma, lma, size, rest))

# ---------------------------------------------------------------- banks
image, symbols, funcs = LI.load(ELF)
code = LI.decode(image, funcs)
TERM = {0x22, 0x32, 0x02, 0x80, 0x73}  # ret, reti, ljmp, sjmp, jmp @a+dptr
banks = []
common_used = sum(s['sh_size'] for s in e.iter_sections() if s['sh_type'] == 'SHT_PROGBITS' and s['sh_flags'] & 2 and s['sh_flags'] & 4 and not s.name.startswith('.bank'))
data_init = secs['.data']['sh_size'] if '.data' in secs else 0
for b in range(8):
    cap = BANK if b < 7 else NV_START - 7 * BANK
    if b == 0:
        used = common_used + data_init
        banks.append(dict(bank=0, physical_start=0, capacity=cap, used=used, free=cap - used,
                          note='common/root: .vectors+.text (CODE) + .data load image'))
        continue
    name = f'.bank{b}'; s = secs.get(name)
    size = s['sh_size'] if s else 0
    row = dict(bank=b, physical_start=b * BANK, capacity=cap, used=size, free=cap - size)
    if s and size:
        vma, lma, osz = outs[name]
        need(vma == 0x8000 and lma == b * BANK and osz == size, f'{name} map/ELF mismatch')
        isum = sum(x[2] for x in ins[name]); last = max(ins[name], key=lambda x: x[0] + x[2])
        row.update(input_sections=len(ins[name]), input_bytes=isum, last_input=last[3], last_end_vma=last[0] + last[2])
        need(isum == size, f'{name}: input sections {isum} != output {size} (truncation/padding)')
        need(last[0] + last[2] <= 0x10000, f'{name}: input section crosses 0xFFFF')
        need(lma + size <= (b + 1) * BANK, f'{name}: crosses physical bank boundary')
        bf = sorted((a, n, nm) for a, n, nm in funcs if a >> 16 == b)
        if bf:
            a, n, nm = bf[-1]; end = (a & 0xFFFF) + n
            pcs = [pc for pc in code if pc >> 16 == b and a <= pc < a + n]
            lastpc = max(pcs); raw = code[lastpc]
            row.update(last_function=nm, last_function_end=end, last_instruction=f'{lastpc & 0xFFFF:#06x}: ' + raw.hex(),
                       last_instruction_is_terminator=raw[0] in TERM or (raw[0] & 0x1f) == 0x01)
            need((lastpc & 0xFFFF) + len(raw) <= 0x10000, f'{name}: instruction crosses 0xFFFF')
            tail = [(x[0], x[2], x[3]) for x in ins[name] if x[0] >= end]
            row['after_last_function'] = [f'{v:#06x}+{z} {r}' for v, z, r in tail]
            if row['free'] == 0:
                need(row['last_instruction_is_terminator'] or tail, f'{name}: zero-margin bank ends in a non-terminator')
    if row['free'] == 0: row['classification'] = 'ZERO-MARGIN BANK'
    need(row['free'] >= 0, f'bank {b} overflow')
    banks.append(row)

# ---------------------------------------------------------------- section overlap (CODE LMA space)
loads = []
for seg in e.iter_segments():
    if seg['p_type'] == 'PT_LOAD' and seg['p_filesz']:
        loads.append((seg['p_paddr'], seg['p_paddr'] + seg['p_filesz']))
loads.sort()
for (a0, a1), (b0, b1) in zip(loads, loads[1:]):
    need(a1 <= b0, f'PT_LOAD overlap {a0:#x}-{a1:#x} / {b0:#x}-{b1:#x}')
need(all(hi <= NV_START for _, hi in loads), 'CODE reaches reserved NV/lock flash')

# ---------------------------------------------------------------- XDATA / internal memory map
def rng(n):
    s = secs.get(n); return (s['sh_addr'], s['sh_addr'] + s['sh_size']) if s and s['sh_size'] else None
xd = {n: rng(n) for n in ('.mcs51_pdata', '.mcs51_pdata_bss', '.data', '.bss')}
sframes = [(s['sh_addr'], s['sh_addr'] + s['sh_size'], s.name) for s in e.iter_sections() if s.name.startswith('.sf_')]
pool_end = max((x[1] for x in sframes), default=0)
status = sym['join_smoke_status']; st0 = status['st_value']; st1 = st0 + 48
ordinary = [r for r in xd.values() if r]
# Reconcile profile: upstream LLD forbids tagged XDATA relocations to absolute
# symbols, so the status block is a 48-byte typed input section pinned at 0x1E00.
fixed = secs.get('.mcs51_fixed_xdata')
need(fixed is not None and fixed['sh_type'] == 'SHT_NOBITS' and fixed['sh_addr'] == 0x1E00 and fixed['sh_size'] == 48,
     '.mcs51_fixed_xdata not a 48-byte NOBITS section at 0x1E00')
fixed_ndx = [i for i, s in enumerate(e.iter_sections()) if s.name == '.mcs51_fixed_xdata'][0]
need(status['st_shndx'] == fixed_ndx and st0 == 0x1E00, 'join_smoke_status not in .mcs51_fixed_xdata at 0x1E00')
need(all(r[1] <= st0 or r[0] >= st1 for r in ordinary), 'join_smoke_status overlaps allocated XDATA')
need(st1 <= 0x1F00, 'join_smoke_status reaches the IRAM alias')
need(all(r[1] <= 0x1E00 for r in ordinary), 'XDATA allocation reaches 0x1E00')
need(pool_end <= (xd['.mcs51_pdata_bss'] or (0, 0))[1], 'static-frame pool outside reserved PDATA')
abs_all = sorted(s.name for s in secs['.symtab'].iter_symbols() if s['st_shndx'] == 'SHN_ABS' and s.name and s['st_info']['type'] != 'STT_FILE')
abs_x = [n for n in abs_all if not n.startswith(('__cc2530_', '__mcs51_'))]
# SDCC runtime parameter-home names referenced by firmware overlap guards only as
# addresses (MMIO_XADDRESS(&sym) comparisons, never dereferenced); the study
# project script aliases them to the status block so every guard stays satisfied.
GUARDS = {'__memcpy_PARM_2', '_gptrput_PARM_2', '_mullong_PARM_2'}
need(all(sym[n]['st_value'] == 0x1E00 and sym[n]['st_shndx'] == fixed_ndx for n in GUARDS), 'guard symbols not at 0x1E00 in .mcs51_fixed_xdata')
internal = {n: secs[n]['sh_size'] for n in ('.mcs51_data1', '.mcs51_data1_bss', '.mcs51_data2', '.mcs51_data2_bss', '.mcs51_bit', '.mcs51_bit_bss') if n in secs}
need(sum(internal.values()) == 0, f'unexpected DATA/IDATA/BIT globals {internal}')
memory = dict(
    xdata_pdata_page=dict(sframe_pool=[0, pool_end], pdata_init=xd['.mcs51_pdata'], pdata_bss=xd['.mcs51_pdata_bss'], static_frames=len(sframes)),
    xdata_data=xd['.data'], xdata_bss=xd['.bss'],
    raw_xdata=sum(r[1] - r[0] for r in ordinary),
    fixed_xdata=dict(join_smoke_status=[st0, st1], iram_alias=[0x1F00, 0x2000], xreg=[0x6000, 0x6400], sfr_xdata=[0x7080, 0x7100]),
    absolute_non_linker_symbols=abs_x,
    internal_globals=internal,
    pseudo_registers='IM0..IM7 pseudo-register pairs + R0-R7 (bank 0 registers)',
    hardware_stack=dict(initial_sp=0x7F, first_push=0x80, top=0xFF, capacity=128))
need(set(abs_x) == set(), f'unexpected absolute symbols {abs_x}')
DIRECT = {0xE5: (1,), 0xF5: (1,), 0x85: (1, 2), 0x75: (1,), 0xA6: (1,), 0x86: (1,), 0x05: (1,), 0x15: (1,), 0xD5: (1,),
          0x25: (1,), 0x35: (1,), 0x95: (1,), 0x45: (1,), 0x55: (1,), 0x65: (1,), 0xC5: (1,), 0xB5: (1,), 0xC0: (1,),
          0xD0: (1,), 0x42: (1,), 0x52: (1,), 0x62: (1,), 0x43: (1,), 0x53: (1,), 0x63: (1,)}
DIRECT.update({op: (1,) for op in range(0x88, 0x90)}); DIRECT.update({op: (1,) for op in range(0xA8, 0xB0)})
direct = collections.Counter()
for pc, raw in code.items():
    for i in DIRECT.get(raw[0], ()):
        if raw[i] < 0x80: direct[raw[i]] += 1
im = sorted(a for a in direct if 0x30 <= a < 0x48)
other = sorted(a for a in direct if not (a < 0x08 or 0x30 <= a < 0x48))
need(not other, f'direct DATA accesses outside R0-R7/IM pseudo-registers: {other}')
memory['internal_data_direct'] = dict(r0_r7=[0x00, 0x08], pseudo_registers=[im[0], im[-1] + 1] if im else None,
                                      pseudo_register_bytes=len(im), other=other,
                                      unused=['0x08-0x2F (banks 1-3, bit area)', '0x48-0x7F (static DATA window)'])

# ---------------------------------------------------------------- startup
def relocated_compare(obj_path, secname, base_sym):
    """Linked bytes of an object section equal the object bytes outside relocated fields."""
    o = ELFFile(open(obj_path, 'rb')); s = o.get_section_by_name(secname); raw = s.data()
    masked = set()
    for rs in o.iter_sections():
        if isinstance(rs, RelocationSection) and rs['sh_info'] == o.get_section_index(secname):
            for r in rs.iter_relocations():
                off = r['r_offset']
                masked.update(range(off, off + 2))  # conservative: widest MCS51 field
    a = symbols['_' + base_sym]
    diff = [i for i, b in enumerate(raw) if i not in masked and image[a + i] != b]
    return dict(section=secname, bytes=len(raw), relocated_bytes=len(masked), mismatches=len(diff))
st = relocated_compare(f'{OUT}/rt/mcs51/cc2530_startup.o', '.text.startup', '__mcs51_start')
need(st['mismatches'] == 0, 'startup bytes differ from cc2530_startup.o')
st0a = sym['__mcs51_start']['st_value']
need(image[0] == 0x02 and (image[1] << 8 | image[2]) == st0a, 'reset vector does not ljmp __mcs51_start')
need(bytes(image[st0a + i] for i in range(3)) == bytes([0x75, 0x81, 0x7F]), 'startup does not set SP=0x7F')
seq = []; pc = st0a
for k in range(200):
    op = image[pc]; n = LI.LENGTHS[op]; raw = bytes(image[pc + i] for i in range(n))
    if op == 0x90: seq.append(raw[1] << 8 | raw[2])
    if op == 0x12 and (raw[1] << 8 | raw[2]) == sym['main']['st_value']: break
    pc += n
names = ['__mcs51_bit_start', '__mcs51_bit_end', '__mcs51_bit_load', '__mcs51_data_start', '__mcs51_data_end',
         '__mcs51_data_load', '__mcs51_pdata_start', '__mcs51_pdata_end', '__mcs51_pdata_load',
         '__mcs51_data1_xdata_start', '__mcs51_data1_xdata_end', '__mcs51_data1_load',
         '__mcs51_data2_xdata_start', '__mcs51_data2_xdata_end', '__mcs51_data2_load']
need(seq[0] == 0 and seq[1:] == [sym[n]['st_value'] for n in names], 'startup operand/symbol mismatch')
dsegs = [sg for sg in e.iter_segments() if sg['p_type'] == 'PT_LOAD' and sg['p_vaddr'] == secs['.data']['sh_addr']
         and sg['p_filesz'] == secs['.data']['sh_size'] and sg['p_paddr'] != sg['p_vaddr']]
# CODE and XDATA share numeric VMAs; .data may start at XDATA 0x0 like the CODE root segment.
need(len(dsegs) == 1, f'.data load segment ambiguous ({len(dsegs)} candidates)')
dseg = dsegs[0]
need(sym['__mcs51_data_load']['st_value'] == dseg['p_paddr'] and
     sym['__mcs51_data_end']['st_value'] - sym['__mcs51_data_start']['st_value'] == secs['.data']['sh_size'] and
     bytes(image[dseg['p_paddr'] + i] for i in range(secs['.data']['sh_size'])) == secs['.data'].data(),
     '.data load image mismatch')
startup = dict(object_compare=st, reset_vector='ljmp __mcs51_start', sp_init=0x7F,
               xdata_clear='0x0000-0x1EFF + IRAM 0x01-0xFF (covers .bss, PDATA pool and status 0x1E00)',
               copy_ranges={n: sym[n]['st_value'] for n in names},
               data_init=dict(load=dseg['p_paddr'], vma=secs['.data']['sh_addr'], size=secs['.data']['sh_size']))

# ---------------------------------------------------------------- runtime helpers
rt = ELFFile(open(f'{OUT}/rt/mcs51/mcs51-runtime.o', 'rb'))
rtf = [s.name for s in rt.get_section_by_name('.symtab').iter_symbols() if s['st_info']['type'] == 'STT_FUNC' and s['st_info']['bind'] == 'STB_GLOBAL']
linked = sorted(n for n in rtf if n in sym)
calls = collections.Counter()
addr2name = {}
for a, n, nm in funcs: addr2name[a] = nm
for pc, raw in code.items():
    if raw[0] in (0x12, 0x02) and len(raw) == 3:
        t = raw[1] << 8 | raw[2]; tgt = addr2name.get(t if t < 0x8000 else (pc & 0xFF0000) | t)
        if tgt in linked: calls[tgt] += 1
rtcmp = {}
for n in linked:
    secn = rt.get_section(next(s for s in rt.get_section_by_name('.symtab').iter_symbols() if s.name == n)['st_shndx']).name
    rtcmp[n] = relocated_compare(f'{OUT}/rt/mcs51/mcs51-runtime.o', secn, n)['mismatches']
need(all(v == 0 for v in rtcmp.values()), f'runtime bytes differ {rtcmp}')
banked_rt = {n: e.get_section(sym[n]['st_shndx']).name for n in linked if e.get_section(sym[n]['st_shndx']).name != '.text'}
runtime = dict(object_sha256=hashlib.sha256(open(f'{OUT}/rt/mcs51/mcs51-runtime.o', 'rb').read()).hexdigest(),
               helpers_linked=linked, call_sites=dict(calls), byte_compare_mismatches=sum(rtcmp.values()),
               banked_helpers=banked_rt)

# ---------------------------------------------------------------- SFR / port writes
PORTS = {0x80: 'P0', 0x90: 'P1', 0xA0: 'P2'}
port = []
for pc in sorted(code):
    raw = code[pc]; op = raw[0]
    if op in (0xF5,) and raw[1] in PORTS: kind = 'mov dir,a'
    elif op in (0x42, 0x52, 0x62) and raw[1] in PORTS: kind = {0x42: 'orl dir,a', 0x52: 'anl dir,a', 0x62: 'xrl dir,a'}[op]
    elif op in (0x43, 0x53, 0x63) and raw[1] in PORTS: kind = {0x43: 'orl dir,#', 0x53: 'anl dir,#', 0x63: 'xrl dir,#'}[op]
    elif op == 0x75 and raw[1] in PORTS: kind = 'mov dir,#'
    elif op in (0xD2, 0xC2, 0xB2, 0x92) and (raw[1] & 0xF8) in PORTS: kind = {0xD2: 'setb', 0xC2: 'clr', 0xB2: 'cpl', 0x92: 'mov bit,c'}[op]
    elif op in (0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F, 0x85, 0x86, 0x87) and raw[1 if op != 0x85 else 2] in PORTS: kind = 'mov dir,src'
    else: continue
    fn = next((nm for a, n, nm in funcs if a <= pc < a + n), '?')
    prev = [p for p in code if p >> 16 == pc >> 16 and pc - 8 <= p < pc]
    rmw_via_a = kind == 'mov dir,a' and any(code[p][0] == 0xE5 and code[p][1] == raw[1] for p in prev)
    port.append(dict(pc=f'{pc:#x}', function=fn, port=PORTS.get(raw[1], PORTS.get(raw[1] & 0xF8)), kind=kind,
                     read_pins_then_write=rmw_via_a))
sfr = dict(port_write_sites=len(port), pin_read_rmw_sites=sum(p['read_pins_then_write'] for p in port),
           functions=sorted({p['function'] for p in port}), sites=port,
           note='LLVM MMIO_SET/CLEAR = mov a,Px; anl/orl a,#m; mov Px,a (reads pins). SDCC uses anl/orl Px,#m (latch). Known hazard, not hardware-verified; no ISRs.')

rep = dict(elf=ELF, elf_sha256=hashlib.sha256(open(ELF, 'rb').read()).hexdigest(), banks=banks,
           minimum_bank_headroom=min(r['free'] for r in banks if r['bank'] and r['used']),
           zero_margin_banks=[r['bank'] for r in banks if r['free'] == 0],
           memory=memory, startup=startup, runtime=runtime, sfr=sfr, failures=fail,
           verdict='PASS' if not fail else 'FAIL')
json.dump(rep, open(REPORT, 'w'), indent=1)
for r in banks: print({k: r[k] for k in r if k not in ('after_last_function',)})
print(json.dumps(dict(min_headroom=rep['minimum_bank_headroom'], zero=rep['zero_margin_banks'], raw_xdata=memory['raw_xdata'],
                      runtime=dict(n=len(linked), calls=sum(calls.values())), port_sites=sfr['port_write_sites'],
                      pin_rmw=sfr['pin_read_rmw_sites'], failures=fail, verdict=rep['verdict'])))
sys.exit(1 if fail else 0)
