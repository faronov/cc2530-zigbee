#!/usr/bin/env python3
"""Incoming stack-argument inventory from the hardware compiler itself.

The study-only FRAMECENSUS instrumentation is not part of the hardware compiler.
This regenerates the only field stackarg_entry_audit.py consumes (fixed stack
objects per function) from MIR printed by the same frozen clang, stopped before
prolog/epilog insertion, with the exact firmware flags.  Output: one
OUTDIR/<unit>.err per unit with lines
    FRAMECENSUS<TAB>fn<TAB>objs=<idx>:<size>:fixed;...
usage: mir_stackargs.py TOOLS SRCROOT SRCMAP DEFS OUTDIR
"""
import os, re, subprocess, sys
from concurrent.futures import ThreadPoolExecutor
tools, srcroot, srcmap, defs, out = sys.argv[1:6]
os.makedirs(out, exist_ok=True)
DEFS = open(defs).read().split()
FLAGS = ['-target', 'mcs51', '-mcpu=cc2530', '-std=c99', '-Oz', *os.environ['SF_FLAGS'].split(),
         *os.environ['ABI_FLAGS'].split(), '-ffunction-sections',
         '-fdata-sections', '-Itoolchain/clang/include', *DEFS]
units = [l.split() for l in open(srcmap) if l.strip()]

def one(u):
    unit, src = u
    mir = subprocess.run([tools + '/clang', *FLAGS, '-S', '-mllvm', '-stop-before=prolog-epilog', src, '-o', '-'],
                         cwd=srcroot, capture_output=True, text=True, check=True).stdout
    lines = []
    for doc in mir.split('\n---\n')[1:]:
        m = re.search(r'^name:\s+(\S+)', doc, re.M)
        if not m: continue
        fs = re.search(r'^fixedStack:\n(.*?)(?=^\S)', doc, re.M | re.S)
        objs = []
        if fs:
            for e in re.finditer(r'- \{ id: (\d+), type: (\S+), offset: (-?\d+), size: (\d+),.*?isImmutable: (\w+)', fs.group(1), re.S):
                if e.group(2) == 'default' and e.group(5) == 'true':
                    objs.append(f'-{int(e.group(1)) + 1}:{e.group(4)}:fixed')
        lines.append(f'FRAMECENSUS\t{m.group(1)}\tobjs={";".join(objs)}{";" if objs else ""}')
    open(f'{out}/{unit}.err', 'w').write('\n'.join(lines) + '\n')
    return unit, len(lines)

with ThreadPoolExecutor(8) as ex:
    res = list(ex.map(one, units))
print(len(res), 'units', sum(n for _, n in res), 'functions')
