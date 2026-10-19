#!/usr/bin/env python3
"""Summarize the 10 simulator cases against the host-reference terminal of each vector.
usage: sim_summary.py SIMDIR OUT.json"""
import json, sys
EXPECT = {0: 'serving', 1: 'fault', 2: 'fault', 3: 'fault', 4: 'fault', 5: 'fault', 6: 'fault', 8: 'fault',
          9: 'fault', 10: 'flash-ram-stop'}
d, out = sys.argv[1:3]
rows, ok = [], True
for c, term in EXPECT.items():
    try:
        r = json.load(open(f'{d}/case{c}.json'))
    except FileNotFoundError:
        rows.append(dict(case=c, verdict='MISSING')); ok = False; continue
    good = r.get('complete') is True and r.get('steps') == r.get('of_steps') and r.get('terminal') == term \
        and r.get('stack_bytes', 999) <= 128
    ok &= good
    rows.append(dict(case=c, steps=r.get('steps'), of_steps=r.get('of_steps'), terminal=r.get('terminal'),
                     expected_terminal=term, complete=r.get('complete'), peak_sp=hex(r.get('peak_sp', 0)),
                     stack_bytes=r.get('stack_bytes'), seconds=r.get('seconds'), verdict='PASS' if good else 'FAIL'))
res = dict(cases=rows, passed=sum(r['verdict'] == 'PASS' for r in rows), total=len(EXPECT),
           max_stack_bytes=max((r.get('stack_bytes') or 0) for r in rows), verdict='PASS' if ok else 'FAIL')
json.dump(res, open(out, 'w'), indent=1)
for r in rows: print(r)
print(json.dumps({k: v for k, v in res.items() if k != 'cases'}))
sys.exit(0 if ok else 1)
