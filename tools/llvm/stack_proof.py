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
an = SC.CensusAnalyzer(img)
mpeak, _, mpath, mcomp = an.function(img.sym['main'])
# One non-nesting ISR may preempt any foreground depth: 2 (vector call) + its peak.
roots = SA.interrupt_roots(img)
isrs = {SC.name_of(an, img, key): 2 + an.function(key)[0] for _, key in roots}
isr = max(isrs.values(), default=0)
chain = SC.chain_of(img, mpath)
names = [SC.name_of(an, img, x) for x in chain if x not in an.thunks]
init_sp = 0x7F  # cc2530_startup.s: mov sp,#127 ; first push lands at 0x80
rep = dict(elf=elf, interrupt_vectors=[n for n, _ in roots], isr_peaks=isrs, isr_contribution=isr,
           foreground_demand=2 + mpeak, initial_sp=init_sp, demand=2 + mpeak + isr, capacity=128,
           predicted_peak_sp=init_sp + 2 + mpeak + isr, physical_limit_sp=0xFF,
           by_origin=dict(collections.Counter({'ret': 2}) + mcomp),
           worst_path=names, worst_path_banked_calls=sum(1 for x in chain if x in an.thunks),
           top_paths=[dict(peak=p, functions=[SC.name_of(an, img, x) for x in ch if x not in an.thunks],
                           banked_calls=sum(1 for x in ch if x in an.thunks)) for p, ch in SC.top_paths(an, img)])
rep['verdict'] = 'PASS' if len(roots) <= 1 and rep['demand'] <= 128 else 'FAIL'
json.dump(rep, open(out, 'w'), indent=1)
print(json.dumps({k: rep[k] for k in ('demand', 'isr_contribution', 'capacity', 'predicted_peak_sp', 'by_origin', 'worst_path_banked_calls', 'verdict')}))
