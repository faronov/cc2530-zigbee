#!/bin/bash
# Clean experimental LLVM-MCS51 firmware build (study production profile).
# Uses ONLY the compiler in $TOOLS. The firmware source is a snapshot of this
# worktree's tracked files (including uncommitted edits); runtime and startup are
# built from the compiler source tree $LLVM_SRC with the same ABI flags.
# usage: TOOLS=bin LLVM_SRC=src ABI_FLAGS=.. SF_FLAGS=.. [FIRMWARE_VARIANT=..] build.sh OUTDIR
# (OUTDIR must not exist). FIRMWARE_VARIANT: production (default), rx-diag or
# parent-recovery (strict), parent-recovery-policy-c (experimental RAM floors) or
# autonomous (parent-recovery without a debugger: built-in admission, reset retry).
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$H/../.." && pwd)
PY=${PYTHON:-python3}
T=${TOOLS:?TOOLS required}
VARIANT=${FIRMWARE_VARIANT:-production}
case $VARIANT in
  production) VDEFS="" ;;
  rx-diag) VDEFS="-DCC2530_RX_DIAG" ;;
  parent-recovery) VDEFS="-DCC2530_PARENT_RECOVERY" ;;
  parent-recovery-policy-c) VDEFS="-DCC2530_PARENT_RECOVERY -DCC2530_REPLAY_POLICY_C" ;;
  autonomous) VDEFS="-DCC2530_PARENT_RECOVERY -DCC2530_AUTONOMOUS" ;;
  *) echo "unknown FIRMWARE_VARIANT '$VARIANT'"; exit 2 ;;
esac
LLVM_SRC=${LLVM_SRC:?LLVM_SRC required}
OUT=$1
[ -e "$OUT" ] && { echo "refusing to reuse $OUT"; exit 2; }
RTSRC=$LLVM_SRC/llvm/lib/Target/MCS51
mkdir -p $OUT/src $OUT/obj $OUT/rt/mcs51 $OUT/link
OUT=$(cd $OUT && pwd)
(cd "$REPO" && git ls-files -z --cached | tar --null -T - -cf -) | tar -x -C $OUT/src

TARGET="-target mcs51 -mcpu=cc2530"
ABI=${ABI_FLAGS?ABI_FLAGS required (may be empty)}
SF=${SF_FLAGS?SF_FLAGS required (may be empty)}
echo "ABI_FLAGS=$ABI SF_FLAGS=$SF" > $OUT/profile-flags.txt
[ "$VARIANT" = production ] || echo "FIRMWARE_VARIANT=$VARIANT DEFINES=$VDEFS" >> $OUT/profile-flags.txt
# Runtime library and startup (same ABI flags as the firmware).
$T/clang $TARGET -O2 -ffreestanding -fno-builtin -ffunction-sections -fdata-sections $ABI \
  -c $RTSRC/mcs51-runtime.c -o $OUT/rt/mcs51/mcs51-runtime.o
$T/llvm-ar rcs $OUT/rt/mcs51/libmcs51builtins.a $OUT/rt/mcs51/mcs51-runtime.o
$T/clang $TARGET -c $RTSRC/cc2530_startup.s -o $OUT/rt/mcs51/cc2530_startup.o
cp $RTSRC/cc2530.ld $OUT/rt/mcs51/cc2530.ld

# Firmware translation units.
DEFS=$(cat $H/inputs/production-defines.txt)${VDEFS:+ $VDEFS}
printf '%s\n' "$DEFS" > $OUT/defines.txt
CFLAGS="$TARGET -std=c99 -Oz $SF $ABI -ffunction-sections -fdata-sections -Itoolchain/clang/include $DEFS"
echo "$T/clang $CFLAGS -c SRC -o OBJ   (cwd = firmware source root)" > $OUT/compile-command.txt
cd $OUT/src
while read -r unit src; do
  echo "$unit $src"
done < $H/inputs/srcmap-stk.txt | xargs -P "${JOBS:-8}" -L1 sh -c \
  '"$0" '"$CFLAGS"' -c "$2" -o "'"$OUT"'/obj/$1.o" 2> "'"$OUT"'/obj/$1.err" || { echo "FAIL $1"; cat "'"$OUT"'/obj/$1.err" >&2; exit 255; }' $T/clang
n=$(ls $OUT/obj/*.o | wc -l); [ "$n" = 45 ] || { echo "object count $n != 45"; exit 2; }
$T/clang $TARGET -c $OUT/src/src/flash_exec_mcs51.s -o $OUT/link/flash_exec_mcs51.o
$T/clang $TARGET -c $H/inputs/fixed_xdata_mcs51.s -o $OUT/link/fixed_xdata_mcs51.o
[ "$VARIANT" = rx-diag ] && $T/clang $TARGET -c $H/inputs/rx_diag_mcs51.s -o $OUT/link/rx_diag_mcs51.o

# Two-pass static-frame link.
cd $H
TOOLS=$T OUT=$OUT FIRMWARE_VARIANT=$VARIANT $PY $H/sframe_link_hw.py $OUT/obj hw1
echo BUILD_OK
