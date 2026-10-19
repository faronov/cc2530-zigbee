#!/bin/bash
# The 13-case semantic matrix of the frozen receipt. env TOOLS, LLVM_SRC, OUT.
H=$(cd "$(dirname "$0")" && pwd)
rm -f "$OUT/semsim-receipt.jsonl"; mkdir -p "$OUT"
fail=0
run() { "$H/run.sh" "$1" "$2" "$3" >> "$OUT/semsim-receipt.jsonl" || fail=1; }
for o in -O0 -O1 -O2 -Os -Oz; do run semd.c $o -O2; done
for o in -O0 -O2 -Oz; do run semi.c $o -Oz; done
for o in -O0 -O1 -O2 -Os -Oz; do run sem.c $o -O2; done
cat "$OUT/semsim-receipt.jsonl"
echo "semsim fail=$fail"
exit $fail
