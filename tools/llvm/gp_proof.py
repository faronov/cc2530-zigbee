import re, sys, json, glob, os
# Argument byte addresses: -mcs51-rn-words passes words 1-2 in R2R3/R4R5
# (bank-0 addresses 2-5) and words 5-6 in IP0/IP1 (48-51).
RN = "-mcs51-rn-words" in os.environ.get("ABI_FLAGS", "")
ARGS = {"timebase_deadline_after": [(48 if RN else 56, "deadline")],
        "timebase_expired": [(48 if RN else 56, "expired")],
        "aes128_encrypt_block": [(2 if RN else 48, "key"), (52, "input")]}
sites = []
for f in sorted(glob.glob("*.s")):
    L = open(f).read().split("\n")
    # The machine outliner may move argument setup into a helper; expand
    # helper bodies (label up to its ret) in place of the lcall.
    helpers = {}
    for j, l in enumerate(L):
        h = re.match(r"(OUTLINED_FUNCTION_\d+):", l)
        if not h: continue
        body = []
        for b in L[j + 1:]:
            if b.strip() == "ret": break
            body.append(b)
        helpers[h.group(1)] = body
    def expand(lines):
        out = []
        for w in lines:
            c = re.match(r"\s*lcall (OUTLINED_FUNCTION_\d+)$", w)
            out.extend(helpers[c.group(1)] if c and c.group(1) in helpers else [w])
        return out
    for i, l in enumerate(L):
        m = re.match(r"\s*lcall __mcs51_bankcall_(\w+)$", l)
        if not m or m.group(1) not in ARGS: continue
        win = expand(L[max(0, i - 30):i])
        for base, name in ARGS[m.group(1)]:
            val = {}
            acc = None
            for w in win:
                w = w.strip()
                if w == "clr a": acc = "0"
                elif re.match(r"mov a,", w) or re.match(r"movx a,", w): acc = None
                mm = re.match(r"mov r?(\d+), (.*)$", w)
                if mm and int(mm.group(1)) in range(base, base + 4):
                    v = mm.group(2); val[int(mm.group(1)) - base] = acc if v == "a" else v
            tag = val.get(2); top = val.get(3); lo = val.get(0)
            kind = {"0": "XDATA", "#0": "XDATA", "#64": "DATA", "#96": "PDATA", "#128": "CODE"}.get(tag, "UNKNOWN")
            sym = re.sub(r"#lo8\((.*)\)", r"\1", lo) if lo else None
            ok = (top in ("0", "#0")) and kind != "UNKNOWN" and (kind != "XDATA" or (sym and "lo8" in lo))
            sites.append(dict(tu=f, line=i + 1, callee=m.group(1), arg=name, lo=lo, hi=val.get(1), tag=tag, byte3=top, space=kind, ok=bool(ok)))
for s in sites: print(s["tu"], s["line"], s["callee"], s["arg"], s["lo"], s["hi"], "tag", s["tag"], "b3", s["byte3"], s["space"], "OK" if s["ok"] else "CHECK")
print(json.dumps(dict(sites=len(sites), ok=sum(s["ok"] for s in sites))))
json.dump(sites, open(sys.argv[1], "w"), indent=1)
