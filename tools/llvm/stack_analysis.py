#!/usr/bin/env python3
"""Whole-program hardware-stack bound for a linked LLVM MCS-51 CC2530 image.

Abstract interpretation of every reachable instruction from main: tracks the SP
offset relative to function entry, FMAP for bank-call trampolines and the
"A = SP + c" idiom used by frame setup. Calls cost 2 bytes plus the callee peak;
callee net SP effects (save/restore helpers) are applied after the call.
Fails closed on anything it cannot model (unknown SP writes, recursion,
SP-growing loops, unresolved banked targets, undecodable bytes).
"""
import json
import os, sys
MODEL_BANKRET = bool(os.environ.get('MODEL_BANKRET'))
from pathlib import Path

from elftools.elf.elffile import ELFFile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import llvm_image  # noqa: F401  (sets the harness path)
from boot_security_resident import LENGTHS

SP, FMAP, ACC = 0x81, 0x9F, 0xE0
# Opcodes that never write the accumulator (A-tracking survives them).
KEEP_A = {0xC0, 0x75, 0x85, 0xF5, 0xF6, 0xF7, 0xF0, 0xF2, 0xF3, 0x90, 0xA3, 0x76, 0x77, 0xC3, 0xD3,
          0x86, 0x87, 0xA6, 0xA7, 0x05, 0x15, 0x06, 0x07, 0x16, 0x17, 0xB2, 0xC2, 0xD2, 0x92, 0xA2,
          0x63, 0x43, 0x53, 0x42, 0x52, 0x62}
KEEP_A |= set(range(0xF8, 0x100)) | set(range(0x88, 0x90)) | set(range(0xA8, 0xB0)) | set(range(0x78, 0x80))
KEEP_A |= set(range(0x08, 0x10)) | set(range(0x18, 0x20))


class Unmodelled(Exception):
    pass


# The only interrupt the firmware may enable: RF vector 16 -> leaf RX ISR
# (CC2530_RADIO_RX_ISR). Anything else in the vector table fails closed.
ALLOWED_ISRS = {16: "radio_autoack_rx_isr"}


def interrupt_roots(img):
    """[(vector, isr_key)] for every populated vector; fail closed otherwise."""
    roots = []
    for s in img.elf.iter_sections():
        if s.name == ".vectors":
            if s["sh_size"] != 3:
                raise Unmodelled(f".vectors is {s['sh_size']} bytes (reset LJMP only)")
        elif s.name.startswith(".mcs51_vector_") and s["sh_size"]:
            n = int(s.name[len(".mcs51_vector_"):])
            name, data = ALLOWED_ISRS.get(n), s.data()
            if name is None or name not in img.sym:
                raise Unmodelled(f"unexpected interrupt vector {n}")
            key = img.sym[name]
            if len(data) != 3 or data[0] != 0x02 or (data[1] << 8 | data[2]) != key[1] or \
                    key[0] != 0 or key[1] >= 0x8000:
                raise Unmodelled(f"vector {n} is not LJMP to common {name}: {data.hex()}")
            roots.append((n, key))
    return roots


class Image:
    def __init__(self, path):
        self.elf = ELFFile(open(path, "rb"))
        self.mem = {}
        for s in self.elf.iter_sections():
            if s["sh_flags"] & 4 and s["sh_size"]:
                bank = int(s.name[5:]) if s.name.startswith(".bank") else 0
                for i, b in enumerate(s.data()):
                    self.mem[(bank, s["sh_addr"] + i)] = b
        self.names, self.sym, self.values = {}, {}, {}
        for s in self.elf.get_section_by_name(".symtab").iter_symbols():
            self.values.setdefault(s.name, s["st_value"] & 0xFFFF)
            if s["st_info"]["type"] != "STT_FUNC" or not isinstance(s["st_shndx"], int):
                continue
            sec = self.elf.get_section(s["st_shndx"])
            bank = int(sec.name[5:]) if sec.name.startswith(".bank") else 0
            key = (bank, s["st_value"] & 0xFFFF)
            self.names.setdefault(key, s.name)
            self.sym.setdefault(s.name, key)
        ram, start, end = (self.values[n] for n in ("flash_exec_ram", "flash_exec_template",
                                                     "flash_exec_template_end"))
        self.ram_window = (ram + 0x8000, ram + 0x8000 + end - start, start)

    def in_ram_window(self, addr):
        return self.ram_window[0] <= addr < self.ram_window[1]

    def canon(self, bank, addr):
        # The RAM copy of flash_exec_template is reached only via flash_exec_enter_ram
        # (modelled as a jump to the flash copy; the template has only relative branches).
        # 0x8000+ targets reached by lcall/ljmp are flash-bank addresses selected by FMAP.
        return (bank if addr >= 0x8000 else 0, addr)

    def fetch(self, key):
        op = self.mem.get(key)
        if op is None:
            raise Unmodelled(f"no code at {self.label(key)}")
        n = LENGTHS[op]
        return bytes(self.mem.get((key[0], key[1] + i), 0) for i in range(n))

    def label(self, key):
        name = self.names.get(key)
        return f"{key[0]}:{key[1]:04x}" + (f"<{name}>" if name else "")


class Analyzer:
    def __init__(self, image):
        self.img = image
        self.memo = {}
        self.active = []

    def function(self, entry):
        if entry in self.memo:
            return self.memo[entry]
        if entry in self.active:
            raise Unmodelled("recursion: " + " -> ".join(self.img.label(k) for k in self.active + [entry]))
        self.active.append(entry)
        try:
            result = self._analyze(entry)
        finally:
            self.active.pop()
        self.memo[entry] = result
        return result

    def _analyze(self, entry):
        img = self.img
        seen = {}
        work = [(entry, 0, None, entry[0] if entry[0] else None)]
        peak, peak_path, nets = 0, [], set()
        while work:
            key, sp, acc, fmap = work.pop()
            prior = seen.setdefault(key, set())
            if (sp, acc, fmap) in prior:
                continue
            if any(p[0] != sp for p in prior):
                raise Unmodelled(f"SP offsets {sorted({p[0] for p in prior} | {sp})} at {img.label(key)}"
                                 f" in {img.label(entry)}")
            prior.add((sp, acc, fmap))
            raw = img.fetch(key)
            op, n = raw[0], len(raw)
            bank, pc = key
            nxt = img.canon(bank, (pc + n) & 0xFFFF)
            if sp > peak:
                peak, peak_path = sp, []

            def rel(off):
                return img.canon(bank, (pc + n + (off - 256 if off > 127 else off)) & 0xFFFF)

            def absolute(target):
                if target >= 0x8000 and bank == 0:
                    raise Unmodelled(f"common code jumps into banked window {target:#x} at {img.label(key)}")
                return img.canon(bank, target)

            writes_sp = (op in (0x75, 0xF5, 0x05, 0x15, 0xD0) and raw[1] == SP) or \
                        (op == 0x85 and raw[2] == SP) or (0x86 <= op <= 0x8F and raw[1] == SP)
            if writes_sp:
                if op == 0xF5 and acc is not None:
                    sp = acc
                elif op == 0x05:
                    sp += 1
                elif op == 0x15:
                    sp -= 1
                else:
                    raise Unmodelled(f"untracked SP write {raw.hex()} at {img.label(key)}")
                work.append((nxt, sp, acc, fmap))
                continue
            if op == 0xE5 and raw[1] == SP:
                work.append((nxt, sp, sp, fmap))
                continue
            if op == 0x24 and acc is not None:
                k = raw[1]
                work.append((nxt, sp, acc + (k - 256 if k > 127 else k), fmap))
                continue
            if op == 0x75 and raw[1] == FMAP:
                fmap = raw[2]
            elif op == 0xD0 and raw[1] == FMAP:
                fmap = None
            writes_acc_direct = (op == 0x75 and raw[1] == ACC) or (op == 0x85 and raw[2] == ACC) or \
                                (op == 0xD0 and raw[1] == ACC) or (0x86 <= op <= 0x8F and raw[1] == ACC)
            new_acc = acc if op in KEEP_A and not writes_acc_direct else None
            if op == 0xC0:
                work.append((nxt, sp + 1, new_acc, fmap))
            elif op == 0xD0:
                work.append((nxt, sp - 1, new_acc, fmap))
            elif op == 0x12 or (op & 0x1F) == 0x11:
                target = int.from_bytes(raw[1:3], "big") if op == 0x12 else \
                    ((pc + n) & 0xF800) | (op >> 5) << 8 | raw[1]
                if target >= 0x8000 and bank == 0:
                    if not isinstance(fmap, int) or not 1 <= fmap <= 7:
                        raise Unmodelled(f"call into banked window with FMAP {fmap} at {img.label(key)}")
                    callee = (fmap, target)
                else:
                    callee = img.canon(bank, target)
                cpeak, cnet, cpath = self.function(callee)
                # MODEL_BANKRET: projection of a trampoline that LJMPs to the banked
                # callee (which returns through a common pop-FMAP/RET helper).
                ra = 0 if (MODEL_BANKRET and bank == 0 and target >= 0x8000) else 2
                if sp + ra + cpeak > peak:
                    peak, peak_path = sp + ra + cpeak, [img.label(callee)] + cpath
                work.append((nxt, sp + cnet, None, fmap))
            elif op in (0x22, 0x32):
                nets.add(sp)
            elif op == 0x73:
                name = img.names.get(entry, "")
                if name.startswith("__mcs51_save_"):
                    nets.add(sp + 2)
                elif name == "flash_exec_enter_ram":
                    work.append(((0, img.ram_window[2]), sp, None, fmap))
                else:
                    raise Unmodelled(f"jmp @a+dptr at {img.label(key)}")
            elif op == 0x02:
                work.append((absolute(int.from_bytes(raw[1:3], "big")), sp, None, fmap))
            elif (op & 0x1F) == 0x01:
                work.append((absolute(((pc + n) & 0xF800) | (op >> 5) << 8 | raw[1]), sp, None, fmap))
            elif op == 0x80:
                work.append((rel(raw[1]), sp, new_acc, fmap))
            elif op in (0x40, 0x50, 0x60, 0x70) or 0xD8 <= op <= 0xDF:
                work += [(rel(raw[1]), sp, new_acc, fmap), (nxt, sp, new_acc, fmap)]
            elif op in (0x10, 0x20, 0x30, 0xD5) or 0xB4 <= op <= 0xBF:
                work += [(rel(raw[2]), sp, None, fmap), (nxt, sp, None, fmap)]
            elif op == 0xA5:
                raise Unmodelled(f"undefined opcode at {img.label(key)}")
            else:
                work.append((nxt, sp, new_acc, fmap))
        if len(nets) > 1:
            raise Unmodelled(f"{img.label(entry)} returns with SP offsets {sorted(nets)}")
        return peak, (nets.pop() if nets else 0), peak_path


def main():
    elf, out = sys.argv[1], sys.argv[2]
    img = Image(elf)
    an = Analyzer(img)
    try:
        mpeak, _, mpath = an.function(img.sym["main"])
        sp_main = 0x7F + 2
        report = dict(status="bounded", sp_reset=0x7F, sp_at_main_entry=sp_main, main_peak=mpeak,
                      max_sp=sp_main + mpeak, stack_bytes_used=sp_main + mpeak - 0x7F,
                      capacity=0xFF - 0x7F, headroom=0xFF - (sp_main + mpeak),
                      worst_path=["main"] + mpath, functions=len(an.memo))
        report["deepest_functions"] = sorted(((v[0], img.label(k)) for k, v in an.memo.items()),
                                             reverse=True)[:25]
    except Unmodelled as e:
        report = dict(status="unmodelled", error=str(e), functions=len(an.memo))
    json.dump(report, open(out, "w"), indent=1)
    print(json.dumps({k: v for k, v in report.items() if k != "deepest_functions"}, indent=1))


if __name__ == "__main__":
    main()
