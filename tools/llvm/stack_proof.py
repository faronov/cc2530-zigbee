#!/usr/bin/env python3
"""Census-free whole-program hardware-stack bound from the linked ELF only.

Uses stack_census.CensusAnalyzer (abstract interpretation of the final machine
code, fail-closed on recursion / unresolved indirect control flow).  Compiler
census records are not needed for the bound; they only attribute frame bytes.
usage: stack_proof.py ELF OUT.json
"""
import collections, json, sys
sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
import stack_analysis as SA
import stack_census as SC
elf, out = sys.argv[1:3]
img = SA.Image(elf)
vec = next(s for s in img.elf.iter_sections() if s.name == '.vectors')['sh_size']
an = SC.CensusAnalyzer(img)
mpeak, _, mpath, mcomp = an.function(img.sym['main'])
chain = SC.chain_of(img, mpath)
names = [SC.name_of(an, img, x) for x in chain if x not in an.thunks]
init_sp = 0x7F  # cc2530_startup.s: mov sp,#127 ; first push lands at 0x80
rep = dict(elf=elf, interrupt_vector_bytes=vec, isr_contribution=0 if vec <= 3 else None,
           initial_sp=init_sp, demand=2 + mpeak, capacity=128,
           predicted_peak_sp=init_sp + 2 + mpeak, physical_limit_sp=0xFF,
           by_origin=dict(collections.Counter({'ret': 2}) + mcomp),
           worst_path=names, worst_path_banked_calls=sum(1 for x in chain if x in an.thunks),
           top_paths=[dict(peak=p, functions=[SC.name_of(an, img, x) for x in ch if x not in an.thunks],
                           banked_calls=sum(1 for x in ch if x in an.thunks)) for p, ch in SC.top_paths(an, img)])
rep['verdict'] = 'PASS' if vec <= 3 and rep['demand'] <= 128 else 'FAIL'
json.dump(rep, open(out, 'w'), indent=1)
print(json.dumps({k: rep[k] for k in ('demand', 'capacity', 'predicted_peak_sp', 'by_origin', 'worst_path_banked_calls', 'verdict')}))
