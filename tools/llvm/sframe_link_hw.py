#!/usr/bin/env python3
"""Study-only link-time allocator for MCS51 static machine frames (__sframe.<fn>).

Pass 1 links with every .mcs51.pdata.bss.sframe.* section packed (addresses are
irrelevant: `mov r1,#sym` has a fixed size, so code layout is final).  The linked
call graph (stack_analysis.Analyzer; fails closed on recursion and unresolved
indirect jumps) then gives each frame the offset
    offset(f) = max over callers c (offset(c) + size(c))
so frames of simultaneously active functions never overlap, while frames of
functions that are never simultaneously active share PDATA.  Pass 2 relinks with
each frame section at its fixed PDATA address and the result is verified.

usage: sframe_link.py MDIR TAG [extra link flags]   (writes link/Oz-TAG.elf, sframe/TAG.json)
"""
import json, os, re, subprocess, sys, collections
from elftools.elf.elffile import ELFFile
H = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, H)
import stack_analysis as sa
# Hardware-candidate copy of sframe/sframe_link_stk.py: every tool comes from $TOOLS,
# every output goes to $OUT; the original linker-script INCLUDE path is only a
# replacement key.
BASE_KEY = '@CC2530_SDCC_ORDER_PACK_LD@'; TOOLS = os.environ['TOOLS']; OUT = os.environ['OUT']
HERE = OUT + '/link'; LO = OUT + '/link'
MDIR, TAG = sys.argv[1], sys.argv[2]; EXTRA = sys.argv[3:]
BASE_LD = open(H + '/inputs/cc2530-sdcc-order-pack.ld').read()
PROJ_LD = open(H + '/inputs/project-ordered-pack.ld').read()
if os.environ.get('FIRMWARE_VARIANT', 'production') == 'rx-diag':
    PROJ_LD += open(H + '/inputs/rx_diag.ld').read()
PDATA_BSS = '    *(.mcs51.pdata.bss)\n'
assert BASE_LD.count(PDATA_BSS) == 1
assert PROJ_LD.count(BASE_KEY) == 1

def link(tag, base_ld, more=()):
    b = f'{HERE}/{tag}-base.ld'; p = f'{HERE}/{tag}-project.ld'
    open(b, 'w').write(base_ld)
    open(p, 'w').write(PROJ_LD.replace(BASE_KEY, b))
    env = dict(os.environ, MDIR=MDIR, PROJECT_LD=p)
    r = subprocess.run([H + '/link.sh', tag,
                        '-Wl,--no-check-sections', '-Wl,--gc-sections', *EXTRA, *more], env=env, capture_output=True, text=True)
    log = open(f'{LO}/{tag}.log').read()
    return r.stdout.strip(), log, f'{LO}/{tag}.elf'

def frames(elf_path):
    """[(object file, owner function, frame symbol, size, address)] via STT_FILE grouping."""
    e = ELFFile(open(elf_path, 'rb')); out = []; cur = None; groups = collections.defaultdict(dict)
    syms = list(e.get_section_by_name('.symtab').iter_symbols())
    for s in syms:
        t = s['st_info']['type']
        if t == 'STT_FILE': cur = s.name; continue
        if s['st_info']['bind'] == 'STB_LOCAL':
            if t == 'STT_FUNC': groups[cur][s.name] = s
            if s.name.startswith('__sframe.'):
                out.append([cur, s.name[len('__sframe.'):], s.name, s['st_size'], s['st_value']])
    return out, groups

# ---- pass 1
P1 = BASE_LD.replace(PDATA_BSS, '    *(.mcs51.pdata.bss .mcs51.pdata.bss.*)\n')
P1 = re.sub(r'ASSERT\([^;]*"CC2530 PDATA globals exceed the MPAGE zero page"\)', '', P1, flags=re.S)
EXTRA_P1 = ['-Wl,--noinhibit-exec']
msg1, log1, elf1 = link(TAG + '-p1', P1, EXTRA_P1)
if 'rc=0' not in msg1: sys.exit('pass 1 failed:\n' + log1[-3000:])
img = sa.Image(elf1)
vec = next(s for s in img.elf.iter_sections() if s.name == '.vectors')['sh_size']
if vec > 3: sys.exit('FAIL CLOSED: interrupt vectors populated; ISR-reachable frames are not modelled')

class Graph(sa.Analyzer):
    def __init__(s, i): super().__init__(i); s.edges = collections.defaultdict(set)
    def function(s, entry):
        if s.active: s.edges[s.active[-1]].add(entry)
        return super().function(entry)
g = Graph(img)
try:
    g.function(img.sym['main'])
except sa.Unmodelled as ex:
    sys.exit('FAIL CLOSED: ' + str(ex))

fr, groups = frames(elf1)
def owner_key(obj, fn):
    s = groups.get(obj, {}).get(fn)
    if s is not None:  # file-local function in the same object
        sec = img.elf.get_section(s['st_shndx']); bank = int(sec.name[5:]) if sec.name.startswith('.bank') else 0
        return (bank, s['st_value'] & 0xFFFF)
    return img.sym.get(fn)
size = {}; owner = {}
for obj, fn, sym, sz, _ in fr:
    k = owner_key(obj, fn)
    if k is None: sys.exit(f'FAIL CLOSED: no owner for {sym} in {obj}')
    if k in size: sys.exit(f'FAIL CLOSED: two frames for {img.label(k)}')
    size[k] = sz; owner[(obj, sym)] = k

# topological order of the reachable DAG (analyzer already rejected recursion)
order = []; state = {}
def visit(k):
    if state.get(k) == 2: return
    state[k] = 1
    for c in g.edges.get(k, ()): visit(c)
    state[k] = 2; order.append(k)
visit(img.sym['main']); order.reverse()
off = collections.defaultdict(int)
for k in order:
    end = off[k] + size.get(k, 0)
    for c in g.edges.get(k, ()):
        off[c] = max(off[c], end)
reach = set(order)
pool = max((off[k] + size.get(k, 0)) for k in order)
unreached = [img.label(k) for k in size if k not in reach]
other_pdata = sum(s['sh_size'] for s in img.elf.iter_sections() if s.name in ('.mcs51_pdata',)) + \
    sum(1 for s in ELFFile(open(elf1, 'rb')).iter_sections() if s.name == '.mcs51_pdata_bss' and s['sh_size'] != sum(size.values()))
if other_pdata: sys.exit('FAIL CLOSED: firmware has its own PDATA objects; the prototype assumes the page is free')
if pool > 256: sys.exit(f'FAIL: overlaid pool {pool} B exceeds the 256-byte PDATA page')

# ---- pass 2: one output section per frame at its fixed address
blk = []; want = {}
for (obj, sym), k in owner.items():
    a = off[k] if k in reach else 0
    fn = sym[len('__sframe.'):]
    sec = re.sub(r'[^A-Za-z0-9_]', '_', f'sf_{obj}_{fn}')
    blk.append(f'  .{sec} {a:#x} (NOLOAD) : {{ */{os.path.splitext(os.path.basename(obj))[0]}.o(.mcs51.pdata.bss.sframe.{fn}) }} > SFRAME\n')
    want[(obj, sym)] = a
reserve = f'    *(.mcs51.pdata.bss)\n    . = MAX(., {pool:#x});\n'
ld2 = BASE_LD.replace(PDATA_BSS, reserve)
MEM = '  XRAM      (rw) : ORIGIN = 0x0000, LENGTH = 0x2000\n'
assert ld2.count(MEM) == 1
ld2 = ld2.replace(MEM, MEM + '  SFRAME    (rw) : ORIGIN = 0x0000, LENGTH = 0x0100\n')
anchor = '    __mcs51_pdata_bss_end = .;\n  } > XRAM\n'
assert ld2.count(anchor) == 1
ld2 = ld2.replace(anchor, anchor + ''.join(sorted(blk)))
msg2, log2, elf2 = link(TAG, ld2)
if 'rc=0' not in msg2: sys.exit('pass 2 failed:\n' + log2[-3000:])
fr2, _ = frames(elf2)
bad = [(o, s, a, want[(o, s)]) for o, f, s, z, a in fr2 if a != want[(o, s)]]
if bad: sys.exit(f'FAIL: frame address mismatch {bad[:5]}')
code1 = [s.data() for s in ELFFile(open(elf1, 'rb')).iter_sections() if s['sh_flags'] & 4]
bss = next(s for s in ELFFile(open(elf2, 'rb')).iter_sections() if s.name == '.bss')
rep = dict(tag=TAG, frames=len(size), frame_bytes_unoverlaid=sum(size.values()), overlaid_pool=pool,
           reachable_frames=sum(1 for k in size if k in reach), unreached=unreached,
           bss_start=bss['sh_addr'], bss_end=bss['sh_addr'] + bss['sh_size'],
           placement={f'{o}:{s}': want[(o, s)] for (o, s) in want})
json.dump(rep, open(f'{HERE}/{TAG}.json', 'w'), indent=1)
print(json.dumps({k: v for k, v in rep.items() if k not in ('placement', 'unreached')}), 'unreached', len(unreached))
