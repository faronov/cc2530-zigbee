#!/usr/bin/env python3
"""Whole-program hardware-stack census for a linked LLVM MCS-51 image.

Same abstract interpretation as stack_analysis.py, but every stack byte carries
an origin label so the worst-case demand can be decomposed:
  ret    return address of a call (caller -> function or caller -> thunk)
  bank   bank-thunk internals (saved FMAP + thunk -> target return address)
  csr    callee-saved registers (prologue PUSH / __mcs51_save_* net effect)
  frame  SP allocation in the prologue (MachineFrameInfo stack size)
  push   any other PUSH in a body (outgoing stack arguments, temporaries)
The per-function `frame` bytes are split into alloca / spill / temp / fixed
using the compiler's -mcs51-frame-census records (one line per function).

usage: stack_census.py ELF CENSUS_DIR OUT.json
"""
import collections
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import stack_analysis as SA  # noqa: E402

SP, FMAP, ACC = SA.SP, SA.FMAP, SA.ACC


def adj(st, delta, cat):
    """Stack state = (sp, origins of the bytes at offsets 0..sp-1). sp may go
    negative inside helpers that pop their caller's return address."""
    sp, cats = st
    new = sp + delta
    if delta >= 0:
        cats = cats + (cat,) * (max(new, 0) - max(sp, 0))
    else:
        cats = cats[:max(new, 0)]
    return new, cats


class CensusAnalyzer(SA.Analyzer):
    def __init__(self, image):
        super().__init__(image)
        self.sites = {}   # entry -> list of (sp at call, callee)
        self.ownpeak = {}
        self.thunks = {}
        for sym in image.elf.get_section_by_name(".symtab").iter_symbols():
            if sym.name.startswith("__mcs51_bankcall_"):
                self.thunks[(0, sym["st_value"] & 0xFFFF)] = sym.name

    def _analyze(self, entry):
        img = self.img
        name = img.names.get(entry, "")
        thunk = entry in self.thunks
        saver = name.startswith("__mcs51_save_") or name.startswith("__mcs51_restore_")
        seen = {}
        work = [(entry, (0, ()), None, entry[0] if entry[0] else None, False)]
        peak, peak_comp, peak_path = 0, collections.Counter(), []
        nets, sites, own = {}, [], 0
        while work:
            key, st, acc, fmap, body = work.pop()
            sp, cats = st
            prior = seen.setdefault(key, set())
            if (sp, acc, fmap) in prior:
                continue
            if any(p[0] != sp for p in prior):
                raise SA.Unmodelled(f"SP offsets {sorted({p[0] for p in prior}|{sp})} at {img.label(key)} in {img.label(entry)}")
            prior.add((sp, acc, fmap))
            raw = img.fetch(key)
            op, n = raw[0], len(raw)
            bank, pc = key
            nxt = img.canon(bank, (pc + n) & 0xFFFF)
            own = max(own, sp)
            if sp > peak:
                peak, peak_comp, peak_path = sp, collections.Counter(cats), []

            def rel(off):
                return img.canon(bank, (pc + n + (off - 256 if off > 127 else off)) & 0xFFFF)

            push_cat = "bank" if thunk else ("csr" if saver else ("push" if body else "csr"))
            writes_sp = (op in (0x75, 0xF5, 0x05, 0x15, 0xD0) and raw[1] == SP) or \
                        (op == 0x85 and raw[2] == SP) or (0x86 <= op <= 0x8F and raw[1] == SP)
            if writes_sp:
                if op == 0xF5 and acc is not None:
                    new = adj(st, acc - sp, "frame")
                elif op == 0x05:
                    new = adj(st, 1, "frame")
                elif op == 0x15:
                    new = adj(st, -1, "frame")
                else:
                    raise SA.Unmodelled(f"untracked SP write at {img.label(key)}")
                work.append((nxt, new, acc, fmap, True))
                continue
            if op == 0xE5 and raw[1] == SP:
                work.append((nxt, st, sp, fmap, body))
                continue
            if op == 0x24 and acc is not None:
                k = raw[1]
                work.append((nxt, st, acc + (k - 256 if k > 127 else k), fmap, body))
                continue
            if op == 0x75 and raw[1] == FMAP:
                fmap = raw[2]
            elif op == 0xD0 and raw[1] == FMAP:
                fmap = None
            writes_acc_direct = (op == 0x75 and raw[1] == ACC) or (op == 0x85 and raw[2] == ACC) or \
                                (op == 0xD0 and raw[1] == ACC) or (0x86 <= op <= 0x8F and raw[1] == ACC)
            new_acc = acc if op in SA.KEEP_A and not writes_acc_direct else None
            if op == 0xC0:
                work.append((nxt, adj(st, 1, push_cat), new_acc, fmap, body))
            elif op == 0xD0:
                work.append((nxt, adj(st, -1, None), new_acc, fmap, body))
            elif op == 0x12 or (op & 0x1F) == 0x11:
                target = int.from_bytes(raw[1:3], "big") if op == 0x12 else \
                    ((pc + n) & 0xF800) | (op >> 5) << 8 | raw[1]
                if target >= 0x8000 and bank == 0:
                    if not isinstance(fmap, int) or not 1 <= fmap <= 7:
                        raise SA.Unmodelled(f"banked call with FMAP {fmap} at {img.label(key)}")
                    callee = (fmap, target)
                else:
                    callee = img.canon(bank, target)
                cpeak, cnet, cpath, ccomp = self.function(callee)
                cname = img.names.get(callee, "")
                csaver = cname.startswith("__mcs51_save_")
                sites.append((sp, callee))
                ret = "bank" if thunk else "ret"
                if sp + 2 + cpeak > peak:
                    peak = sp + 2 + cpeak
                    peak_comp = collections.Counter(cats) + collections.Counter({ret: 2}) + ccomp
                    peak_path = [img.label(callee)] + cpath
                net_cat = "csr" if csaver else ("bank" if thunk else "push")
                new = adj(st, cnet, net_cat)
                # A save helper is part of the prologue; any other call ends it.
                work.append((nxt, new, None, fmap, body or not csaver))
            elif op in (0x22, 0x32):
                nets[sp] = cats
            elif op == 0x73:
                if name.startswith("__mcs51_save_"):
                    nets[sp + 2] = cats
                elif name == "flash_exec_enter_ram":
                    work.append(((0, img.ram_window[2]), st, None, fmap, body))
                else:
                    raise SA.Unmodelled(f"jmp @a+dptr at {img.label(key)}")
            elif op == 0x02:
                t = int.from_bytes(raw[1:3], "big")
                if t >= 0x8000 and bank == 0:
                    raise SA.Unmodelled("common jumps into banked window")
                work.append((img.canon(bank, t), st, None, fmap, body))
            elif (op & 0x1F) == 0x01:
                work.append((img.canon(bank, ((pc + n) & 0xF800) | (op >> 5) << 8 | raw[1]),
                             st, None, fmap, body))
            elif op == 0x80:
                work.append((rel(raw[1]), st, new_acc, fmap, body))
            elif op in (0x40, 0x50, 0x60, 0x70) or 0xD8 <= op <= 0xDF:
                work += [(rel(raw[1]), st, new_acc, fmap, body), (nxt, st, new_acc, fmap, body)]
            elif op in (0x10, 0x20, 0x30, 0xD5) or 0xB4 <= op <= 0xBF:
                work += [(rel(raw[2]), st, None, fmap, body), (nxt, st, None, fmap, body)]
            elif op == 0xA5:
                raise SA.Unmodelled("undefined opcode")
            else:
                work.append((nxt, st, new_acc, fmap, body))
        if len(nets) > 1:
            raise SA.Unmodelled(f"{img.label(entry)} returns with SP offsets {sorted(nets)}")
        self.sites[entry] = sites
        self.ownpeak[entry] = own
        net = next(iter(nets)) if nets else 0
        return peak, net, peak_path, peak_comp


def load_census(directory):
    """name -> list of records (static functions may repeat across TUs)."""
    out = collections.defaultdict(list)
    for f in sorted(Path(directory).glob("*.err")):
        for line in f.read_text().splitlines():
            if not line.startswith("FRAMECENSUS"):
                continue
            p = line.split("\t")
            rec = dict(tu=f.stem, name=p[1])
            for kv in p[2:]:
                k, v = kv.split("=", 1)
                rec[k] = v
            for k in ("stack", "csr", "isr", "maxcall", "calls"):
                rec[k] = int(rec[k])
            kinds = collections.Counter()
            objs = []
            for o in filter(None, rec["objs"].split(";")):
                q = o.split(":")
                kinds[q[2]] += int(q[1])
                objs.append(dict(index=int(q[0]), size=int(q[1]), kind=q[2],
                                 name=q[3] if len(q) > 3 else ""))
            rec["objs"] = objs
            rec["kinds"] = dict(kinds)
            out[p[1]].append(rec)
    return out


def pick(census, fname, frame_bytes, csr_bytes):
    recs = census.get(fname, [])
    m = [r for r in recs if r["stack"] == frame_bytes and r["csr"] == csr_bytes]
    if not m:
        return None
    if any(r["kinds"] != m[0]["kinds"] or r["csrregs"] != m[0]["csrregs"] for r in m):
        return None
    return m[0]


def chain_of(img, mpath):
    chain = [img.sym["main"]]
    for label in mpath:
        bank, rest = label.split(":", 1)
        chain.append((int(bank), int(rest.split("<")[0], 16)))
    return chain


def name_of(an, img, key):
    return img.names.get(key) or an.thunks.get(key) or img.label(key)


def own_bytes(an, entry, callee):
    comp = an.memo[entry][3]
    own = collections.Counter(comp)
    if callee is not None:
        own.subtract(an.memo[callee][3])
    return +own


def decompose(an, img, census, chain, start=2):
    levels, depth, total = [], start, collections.Counter({"ret": start})
    for i, key in enumerate(chain):
        name = name_of(an, img, key)
        nxt = chain[i + 1] if i + 1 < len(chain) else None
        own = own_bytes(an, key, nxt)
        if nxt is None:
            own = collections.Counter(an.memo[key][3])
        detail = collections.Counter()
        if key in an.thunks:
            b = own.get("bank", 0) + own.get("ret", 0)
            detail["bank-thunk-ret"] = min(b, 2)
            detail["bank-fmap"] = b - min(b, 2)
            detail["return-address"] = 0
        else:
            rec = pick(census, name, own.get("frame", 0), own.get("csr", 0))
            detail["return-address"] = own.get("ret", 0)
            if rec is None and own.get("csr", 0) + own.get("frame", 0) == 0:
                rec = dict(kinds={}, csrregs="")
            if rec is None:
                detail["frame-unattributed"] = own.get("frame", 0)
                detail["csr-unattributed"] = own.get("csr", 0)
            else:
                for k, v in rec["kinds"].items():
                    if k != "fixed" and v:
                        detail["frame-" + k] = v
                regs = [r for r in rec["csrregs"].split(",") if r]
                detail["csr-R"] = sum(1 for r in regs if r.startswith("R"))
                detail["csr-IM"] = sum(1 for r in regs if r.startswith("IM"))
                extra = own.get("csr", 0) - len(regs)
                if extra:
                    detail["csr-helper-rounding"] = extra
            detail["outgoing-args/push"] = own.get("push", 0)
        detail = +detail
        levels.append(dict(function=name, depth_at_entry=depth, bytes=sum(own.values()), detail=dict(detail)))
        total.update(detail)
        depth += sum(own.values())
    return levels, total, depth


def top_paths(an, img, k=20):
    """Best-first enumeration of complete call chains from main, ranked by the
    SP depth reached inside the last function's own body."""
    import heapq
    main = img.sym["main"]
    heap = [(-(2 + an.memo[main][0]), 0, (main,), 2, False)]
    out, n = [], 1
    while heap and len(out) < k:
        prio, _, chain, depth, terminal = heapq.heappop(heap)
        if terminal:
            out.append((-prio, chain))
            continue
        f = chain[-1]
        heapq.heappush(heap, (-(depth + an.ownpeak[f]), n, chain, depth, True)); n += 1
        best = {}
        for sp, callee in an.sites.get(f, []):
            best[callee] = max(best.get(callee, -1), sp)
        for callee, sp in best.items():
            d = depth + sp + 2
            heapq.heappush(heap, (-(d + an.memo[callee][0]), n, chain + (callee,), d, False)); n += 1
    return out


def main():
    elf, cdir, out = sys.argv[1], sys.argv[2], sys.argv[3]
    img = SA.Image(elf)
    an = CensusAnalyzer(img)
    census = load_census(cdir)
    mpeak, _, mpath, mcomp = an.function(img.sym["main"])
    chain = chain_of(img, mpath)
    levels, total, end = decompose(an, img, census, chain)
    assert end == 2 + mpeak, (end, mpeak)
    report = dict(demand=2 + mpeak, capacity=128, interrupt_vectors_populated=0,
                  by_origin=dict(collections.Counter({"ret": 2}) + mcomp), by_category=dict(total),
                  worst_path=levels)
    paths = []
    for peak, ch in top_paths(an, img):
        names = [name_of(an, img, x) for x in ch if x not in an.thunks]
        _lv, tot, _e = decompose(an, img, census, list(ch))
        paths.append(dict(peak=peak, functions=names, banked_calls=sum(1 for x in ch if x in an.thunks),
                          by_category=dict(tot)))
    report["top_paths"] = paths
    funcs = []
    for name, recs in census.items():
        for r in recs:
            key = img.sym.get(name)
            regs = [x for x in r["csrregs"].split(",") if x]
            funcs.append(dict(function=name, tu=r["tu"], frame=r["stack"], csr=r["csr"],
                              csr_im=sum(1 for x in regs if x.startswith("IM")), maxcall=r["maxcall"],
                              hw_bytes=r["stack"] + r["csr"] + r["maxcall"] + 2, kinds=r["kinds"]))
    funcs.sort(key=lambda d: -d["hw_bytes"])
    report["top_functions"] = funcs[:50]
    agg = collections.Counter()
    for r in funcs:
        agg.update({k: v for k, v in r["kinds"].items() if k != "fixed"})
        agg["csr-R"] += r["csr"] - r["csr_im"]
        agg["csr-IM"] += r["csr_im"]
        agg["outgoing-args(max)"] += r["maxcall"]
        agg["functions"] += 1
    report["all_functions_static_totals"] = dict(agg)
    json.dump(report, open(out, "w"), indent=1)
    print(json.dumps(dict(demand=report["demand"], by_origin=report["by_origin"],
                          by_category=report["by_category"]), indent=1))


if __name__ == "__main__":
    main()
