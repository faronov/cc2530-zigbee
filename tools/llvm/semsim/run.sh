#!/bin/bash
# Generic-pointer semantic simulation (reconcile copy; compiler from $TOOLS).
# run.sh SRC OPT RTOPT -> XDATA 0x100.. = 01 per passing check, 0x1FF = 0xA5 on completion.
set -e
TOOLS=${TOOLS:?TOOLS required}
L=${LLVM_SRC:?LLVM_SRC required}/llvm/lib/Target/MCS51
OUT=${OUT:?OUT required}
H=$(cd "$(dirname "$0")" && pwd)
X=${SEM_EXTRA:-}  # optional ABI/codegen flags for TU and runtime
S=$1; O=$2; RO=$3; T=$(basename "$S" .c)$O$RO
mkdir -p "$OUT"; cd "$OUT"
[ -f start81.o ] || "$TOOLS/clang" -target mcs51 -c "$H/start81.s" -o start81.o
[ -f rt$RO.o ] || "$TOOLS/clang" -target mcs51 $X $RO -ffunction-sections -fdata-sections -c "$L/mcs51-runtime.c" -o rt$RO.o
"$TOOLS/clang" -target mcs51 $X $O -ffreestanding -fno-builtin -ffunction-sections -c "$H/$S" -o $T.o
# Upstream efc84d8b4 rejects MOVX to sections lld cannot classify as XDATA,
# so the result buffer lives in .mcs51.xdata.simresult; map it to 0x100.
sed 's/\*(\.mcs51\.simresult)/*(.mcs51.simresult .mcs51.xdata.simresult)/' "$L/test/mcs51-flat-sim.ld" > flat-sim.ld
grep -q 'mcs51.xdata.simresult' flat-sim.ld
"$TOOLS/ld.lld" -m elf32-mcs51 -T flat-sim.ld --gc-sections --no-check-sections -o $T.elf start81.o $T.o rt$RO.o
"$TOOLS/llvm-objcopy" -O ihex $T.elf $T.hex
printf '%s\n' 'break xram w 0x1ff' 'run' 'dx 0x100 0x11f' 'dx 0x1f8 0x1ff' 'quit' | timeout 300 "${S51:?S51 required}" -t 8052 -q -c - $T.hex > $T.log 2>&1
python3 - "$T" "$H/$S" <<'PY'
import re, sys, json
t, src = sys.argv[1], sys.argv[2]
log = open(t + '.log').read()
mem = {}
for m in re.finditer(r'^0x([0-9a-f]{4})((?: [0-9a-f]{2})+)', log, re.M):
    a = int(m.group(1), 16)
    for i, b in enumerate(m.group(2).split()):
        mem.setdefault(a + i, int(b, 16))
want = open(src).read().count('R[n++]')
got = [mem.get(0x100 + i) for i in range(want)]
done = mem.get(0x1FF) == 0xA5
ok = done and all(v == 1 for v in got)
print(json.dumps(dict(case=t, expected=want, passed=sum(v == 1 for v in got), complete=done,
                      verdict='PASS' if ok else 'FAIL')))
sys.exit(0 if ok else 1)
PY
