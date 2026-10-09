#!/usr/bin/env python3
"""Resource summary of a linked cc2530 LLVM ELF.  usage: measure.py ELF [ELF...]"""
import sys, json
from elftools.elf.elffile import ELFFile
def measure(p):
    e=ELFFile(open(p,'rb')); r={'elf':p,'banks':{}}
    for s in e.iter_sections():
        n=s.name; sz=s['sh_size']
        if n=='.vectors': r['vectors']=sz
        elif n=='.text': r['common_text']=sz
        elif n.startswith('.bank'): r['banks'][n]=sz
        elif n in ('.bss','.data','.mcs51_pdata','.mcs51_pdata_bss','.idata','.rodata'): r[n.strip('.')]=sz
    r['common']=r.get('vectors',0)+r.get('common_text',0)
    r['code']=r['common']+sum(r['banks'].values())
    cap={n:(26624 if n=='.bank7' else 32768) for n in r['banks']}
    r['bank_free']={n:cap[n]-v for n,v in r['banks'].items()}
    r['aggregate_bank_slack']=sum(r['bank_free'].values())
    sym=e.get_section_by_name('.symtab')
    sf=[(s.name,s['st_value'],s['st_size']) for s in sym.iter_symbols() if s.name.startswith('__sframe.')]
    r['sframe_symbols']=len(sf); r['sframe_bytes']=sum(x[2] for x in sf)
    return r
for p in sys.argv[1:]: print(json.dumps(measure(p)))
