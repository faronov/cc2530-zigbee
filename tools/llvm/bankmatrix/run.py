#!/usr/bin/env python3
"""Build the bank-call ABI matrix with a toolchain and run it in ucsim with all
eight CC2530 FMAP banks modelled. Usage: run.py BIN_DIR [label] [--entry]"""
import os, re, subprocess, sys, tempfile
from elftools.elf.elffile import ELFFile
H = os.path.dirname(os.path.abspath(__file__))
BIN = sys.argv[1]; LABEL = sys.argv[2] if len(sys.argv) > 2 else "run"
ENTRY = "--entry" in sys.argv
S51 = os.environ["S51"]
# Reconcile copy: linker script/startup default to the toolchain's own resource
# dir; MATRIX_FLAGS adds ABI flags (e.g. -mcs51-csr/-wide-args/-static-frames).
RES = BIN + "/../lib/clang/23/mcs51"
LD = os.environ.get("MATRIX_LD", RES + "/cc2530.ld")
SU = os.environ.get("MATRIX_SU", RES + "/cc2530_startup.o")
XF = os.environ.get("MATRIX_FLAGS", "").split()
OUTD = os.environ.get("MATRIX_OUT", H + "/out")
os.makedirs(OUTD, exist_ok=True)
TUS = ["main", "tu_b1", "tu_b2", "tu_auto", "tu_common"]
O = tempfile.mkdtemp(prefix="abim-")

def sh(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        sys.exit(f"FAILED: {' '.join(cmd)}\n{r.stderr}")
    return r.stdout

for t in TUS:
    sh([BIN + "/clang", "--target=mcs51", "-mcpu=cc2530", "-Oz"] + XF + ["-c", f"{H}/{t}.c", "-o", f"{O}/{t}.o"])
elf_path = f"{O}/m.elf"
sh([BIN + "/clang", "--target=mcs51", "-mcpu=cc2530", "-nostartfiles", "-fuse-ld=lld",
    f"--ld-path={BIN}/ld.lld", SU, "-Wl,-T," + LD, "-Wl,--no-check-sections",
    "-Wl,-Map," + O + "/m.map"] + [f"{O}/{t}.o" for t in TUS] + ["-o", elf_path])

elf = ELFFile(open(elf_path, "rb"))
flash = {}
for seg in elf.iter_segments():
    if seg["p_type"] == "PT_LOAD" and seg["p_filesz"] and seg["p_paddr"] < 0x40000:
        for i, b in enumerate(seg.data()):
            flash[seg["p_paddr"] + i] = b
secs = list(elf.iter_sections())
sym = {}
for s in elf.get_section_by_name(".symtab").iter_symbols():
    if s.name and isinstance(s["st_shndx"], int):
        sec = secs[s["st_shndx"]].name
        bank = int(sec[5:]) if sec.startswith(".bank") else 0
        sym[s.name] = (s["st_value"], bank, sec)

cmds = ["set option analyzer false", "memory create chip flash 0x40000 8", "fill flash 0 0x3ffff 0xff"]
addrs = sorted(flash)
i = 0
while i < len(addrs):
    j = i
    while j + 1 < len(addrs) and addrs[j + 1] == addrs[j] + 1 and j - i < 31:
        j += 1
    cmds.append(f"set memory flash {addrs[i]:#x} " + " ".join(hex(flash[a]) for a in addrs[i:j + 1]))
    i = j + 1
cmds += ["memory create addressdecoder rom 0 0x7fff flash 0",
         "memory create addressdecoder rom 0x8000 0xffff flash 0",
         "memory create banker sfr 0x9f 7 rom 0x8000 0xffff"]
cmds += [f"memory create bank rom 0x8000 {b} flash {b * 0x8000:#x}" for b in range(8)]
ENTRY_FUNCS = ["b1_f5w", "b1_same", "b2_f5w", "c_f5w", "a_f5w"]
if ENTRY:
    for f in ENTRY_FUNCS:
        cmds.append(f"break {sym[f][0]:#x}")
cmds.append(f"break {sym['matrix_done'][0]:#x}")
dumps = []
def dump(name, n):
    a = sym[name][0]
    cmds.append(f"dump /h xram {a:#x} {a + n - 1:#x}")
if ENTRY:
    for _ in range(12):
        cmds += ["run", "info registers", "dump /h iram 0x80 0xff"]
else:
    cmds.append("run 0")
cmds.append("echo ==RESULTS==")
dump("out", 20 * 16); dump("ret16", 40); dump("ret32", 80); dump("done", 1)
cmds.append("quit")
open(f"{O}/m.cmd", "w").write("\n".join(cmds) + "\n")
r = subprocess.run([S51, "-t", "C52", "-q", "-c", "-"], input=f'exec "{O}/m.cmd"\n',
                   capture_output=True, text=True, timeout=60)
text = r.stdout
open(f"{OUTD}/{LABEL}.simlog", "w").write(text)
res = text.split("==RESULTS==")[-1]
vals = []
for line in res.splitlines():
    m = re.match(r"\s*0x[0-9a-fA-F]+\s+((?:[0-9a-fA-F]{2}\s+)+)", line + " ")
    if m:
        vals += [int(x, 16) for x in m.group(1).split()]
out = [vals[s * 16:(s + 1) * 16] for s in range(20)]
r16 = [vals[320 + 2 * k] | vals[321 + 2 * k] << 8 for k in range(20)]
r32 = [int.from_bytes(bytes(vals[360 + 4 * k:364 + 4 * k]), "little") for k in range(20)]
done = vals[440] if len(vals) > 440 else None

def w(*xs): return [b for x in xs for b in (x & 0xff, x >> 8)]
def d(*xs): return [b for x in xs for b in x.to_bytes(4, "little")]
X = {  # slot: (description, caller, callee, expected out bytes, (kind, expected return))
 0: ("root->explicit bank1, cross-TU (weak thunk)", "main", "b1_f5w", w(0x101,0x202,0x303,0x404,0x1234), ("r16", 0x101 ^ 0x1234)),
 1: ("root->autobank, 6x u8", "main", "a_f6b", [0x11,0x22,0x33,0x44,0x55,0x66], ("r16", 0x5566)),
 2: ("root->autobank, 3x u32 + u8", "main", "a_fmix", d(0x01020304,0x05060708,0x090a0b0c) + [0x0d], ("r32", 0x090a0b19)),
 3: ("explicit bank1->bank2", "b1_driver", "b2_f5w", w(0x301,0x302,0x303,0x304,0x3e3e), ("r16", 0x303 ^ 0x3e3e)),
 4: ("same explicit bank, stack args", "b1_driver", "b1_same", w(0x401,0x402,0x403,0x404,0x4e4e), ("r16", 0x402 ^ 0x4e4e)),
 5: ("same explicit bank, registers only", "b1_driver", "b1_regonly", w(0x501,0x502), ("r16", 0x501 + 0x502)),
 6: ("autobank->common, cross-TU", "a_driver", "c_f5w", w(0x601,0x602,0x603,0x604,0x6e6e), ("r16", 0x601 + 0x6e6e)),
 7: ("autobank->common, same TU", "a_driver", "a_common_same", w(0x701,0x702,0x703,0x704,0x7e7e), ("r16", 0x7e7e - 0x701)),
 8: ("function pointer->autobank", "main", "a_f5w", w(0x801,0x802,0x803,0x804,0x5678), ("r16", 0x804 ^ 0x5678)),
 9: ("function pointer->common", "main", "c_f5w", w(0x901,0x902,0x903,0x904,0x9abc), ("r16", (0x901 + 0x9abc) & 0xffff)),
 10: ("varargs autobank", "main", "a_fva", w(0x1111,0x2222,0x3333), ("r16", 0x6666)),
 11: ("varargs common", "main", "c_fva", w(0x4444,0x5555,0x6666), ("r16", 0xffff)),
 12: ("nested autobank->bank2, args forwarded", "a_outer", "b2_inner", w(0xc01,0xc02,0xc03,0xc04,0xcee,0xcee), ("r32", ((0xcee << 16) | 0xc01) + 1)),
 13: ("autobank->static autobank, same TU", "a_driver", "a_local", w(0xd01,0xd02,0xd03,0xd04,0xdede), ("r16", 0xd01 ^ 0xd02 ^ 0xdede)),
}
def where(f):
    if f not in sym: return "?"
    a, b, sec = sym[f]
    return f"bank{b}@{a:#06x}" if b else f"common@{a:#06x}"
fails = 0
print(f"== {LABEL}: {BIN}  done={done if done is None else hex(done)}")
for s, (desc, caller, callee, exp, (kind, er)) in X.items():
    got = out[s][:len(exp)] if len(out[s]) >= len(exp) else out[s]
    gr = r16[s] if kind == "r16" else r32[s]
    ok = got == exp and gr == er
    fails += not ok
    print(f"{s:2} {'PASS' if ok else 'FAIL'} {desc:42} {caller}({where(caller)}) -> {callee}({where(callee)})"
          + ("" if ok else f"\n     args exp {bytes(exp).hex()} got {bytes(got).hex()}; ret exp {er:#x} got {gr:#x}"))
print(f"== {LABEL}: {len(X) - fails}/{len(X)} pass")
os.system(f"cp {O}/m.elf {OUTD}/{LABEL}.elf; cp {O}/m.map {OUTD}/{LABEL}.map; rm -rf {O}")
sys.exit(1 if fails else 0)
