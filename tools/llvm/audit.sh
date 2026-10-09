#!/bin/bash
# Final-machine-code audits of a build.sh output (writes OUT/audit, OUT/mir, OUT/asm).
# usage: TOOLS=bin ABI_FLAGS=.. SF_FLAGS=.. audit.sh OUT
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd)
T=${TOOLS:?TOOLS required}
OUT=$(cd "$1" && pwd); ELF=$OUT/link/hw1.elf; A=$OUT/audit
PY=${PYTHON:-python3}
mkdir -p $A $OUT/mir $OUT/asm
$PY $H/measure.py $ELF > $A/measure.json
$PY $H/thunk_audit.py $ELF $A/thunks.json
$PY $H/mir_stackargs.py $T $OUT/src $H/inputs/srcmap-stk.txt $H/inputs/production-defines.txt $OUT/mir
$PY $H/stackarg_entry_audit.py $ELF $OUT/mir $A/stackarg.json
$PY $H/stack_proof.py $ELF $A/stack.json
$PY $H/mkhex.py $ELF $A/hw1.hex $A/hex.json
$T/llvm-objcopy -O ihex $ELF $A/objcopy.hex
$PY $H/proofs.py $OUT hw1 $A/proofs.json
$PY $H/hexbanks.py $A/hexbanks.json llvm=$A/hw1.hex
# Generic-pointer call-site proof on assembly emitted by the same compiler/flags.
DEFS=$(cat $H/inputs/production-defines.txt)
CFLAGS="-target mcs51 -mcpu=cc2530 -std=c99 -Oz ${SF_FLAGS?} ${ABI_FLAGS?} -ffunction-sections -fdata-sections -Itoolchain/clang/include $DEFS"
cd $OUT/src
while read -r unit src; do echo "$unit $src"; done < $H/inputs/srcmap-stk.txt | \
  xargs -P "${JOBS:-8}" -L1 sh -c '"$0" '"$CFLAGS"' -S "$2" -o "'"$OUT"'/asm/$1.s"' $T/clang
cd $OUT/asm && $PY $H/gp_proof.py $A/gp-sites.json > $A/gp-sites.txt
tail -1 $A/gp-sites.txt
echo AUDIT_OK
