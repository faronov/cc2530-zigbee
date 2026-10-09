#!/usr/bin/env python3
"""Physical CC2530F256 Intel HEX from a linked LLVM/MCS51 ELF.

Bytes come from PT_LOAD segments with file data, keyed by physical (load)
address.  Each byte is expressed as the SDCC virtual banked address
(bank << 16 | logical) and packed by the accepted artifact's own
tools/banked_image.py pack()/ihex(), which re-derives the physical address and
rejects reserved NV/lock flash and overlaps.  Only present bytes are emitted.
usage: mkhex.py ELF OUT.hex OUT.json
"""
import hashlib, json, sys
sys.path.insert(0, __import__('os').path.join(__import__('os').path.dirname(__import__('os').path.abspath(__file__)), '..'))
from elftools.elf.elffile import ELFFile
from banked_image import pack, ihex, physical_address, CODE_END, BANK_SIZE
from verify_firmware import parse_ihex
elf, out_hex, out_json = sys.argv[1:4]
e = ELFFile(open(elf, 'rb'))
virt, segs = {}, []
for s in e.iter_segments():
    if s['p_type'] != 'PT_LOAD' or s['p_filesz'] == 0: continue
    pa, va, data = s['p_paddr'], s['p_vaddr'], s.data()[:s['p_filesz']]
    bank = 0 if pa < BANK_SIZE else pa // BANK_SIZE
    segs.append(dict(paddr=pa, vaddr=va, filesz=len(data), bank=bank,
                     sections=[x.name for x in e.iter_sections() if x['sh_type'] != 'SHT_NOBITS' and x['sh_size'] and s.section_in_segment(x)]))
    for i, b in enumerate(data):
        p = pa + i
        assert p // BANK_SIZE == (bank if bank else 0) or (bank == 0 and p < BANK_SIZE), f'segment crosses bank boundary at {p:#x}'
        v = p if p < BANK_SIZE else ((p // BANK_SIZE) << 16) | (BANK_SIZE + p % BANK_SIZE)
        if bank: assert va + i == BANK_SIZE + p % BANK_SIZE, f'bank VMA/LMA mismatch at {p:#x}'
        assert v not in virt, f'overlap at {p:#x}'
        virt[v] = b
phys = pack(virt)
assert all(physical_address(v) == (v & 0xFFFF if v >> 16 == 0 else (v >> 16) * BANK_SIZE + (v & 0xFFFF) - BANK_SIZE) for v in virt)
text = ihex(phys).encode('ascii')
open(out_hex, 'wb').write(text)
back = parse_ihex(text.decode('ascii'))
assert back == phys, 'HEX does not round-trip'
rep = dict(elf=elf, elf_sha256=hashlib.sha256(open(elf, 'rb').read()).hexdigest(),
           hex_sha256=hashlib.sha256(text).hexdigest(), populated=len(phys),
           lowest=min(phys), highest=max(phys), extent=max(phys) - min(phys) + 1,
           reserved_from=CODE_END, bytes_at_or_above_reserved=sum(1 for a in phys if a >= CODE_END),
           records=text.count(b'\n'), segments=segs)
json.dump(rep, open(out_json, 'w'), indent=1)
print(json.dumps({k: v for k, v in rep.items() if k != 'segments'}))
