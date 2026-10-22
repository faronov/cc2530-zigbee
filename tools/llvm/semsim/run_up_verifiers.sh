#!/bin/bash
# usage: run_up_verifiers.sh SRC BUILD OUTFILE
set -u
SRC=$1; B=$2; OUTF=$3
S=$(mktemp -d /tmp/upver.XXXX); R=$S/src
mkdir -p $R/llvm/lib/Target/MCS51/test $R/llvm/test/CodeGen/8051/Inputs
cp $SRC/llvm/lib/Target/MCS51/verify-*.sh $SRC/llvm/lib/Target/MCS51/mcs51-runtime.c $R/llvm/lib/Target/MCS51/
sed 's/\*(\.mcs51\.simresult)/*(.mcs51.simresult .mcs51.xdata.simresult)/' $SRC/llvm/lib/Target/MCS51/test/mcs51-flat-sim.ld > $R/llvm/lib/Target/MCS51/test/mcs51-flat-sim.ld
for f in $SRC/llvm/test/CodeGen/8051/Inputs/*-runtime.c; do sed 's/section("\.mcs51\.simresult")/section(".mcs51.xdata.simresult")/' $f > $R/llvm/test/CodeGen/8051/Inputs/$(basename $f); done
ln -s $B $S/llvm-mcs51-build
: > $OUTF
for v in verify-generic-pointer-runtime.sh verify-arith32-runtime.sh verify-div64-runtime.sh; do
  for o in 0 2 s z; do
    out=$(cd $R/llvm/lib/Target/MCS51 && MCS51_OPT_LEVEL=$o sh $v 2>&1); rc=$?
    echo "$v O$o rc=$rc $(echo "$out" | tail -1)" >> $OUTF
  done
done
rm -rf $S; cat $OUTF
