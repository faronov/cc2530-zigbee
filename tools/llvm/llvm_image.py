"""Load an LLVM MCS51 ELF as harness-compatible artifacts (study-only)."""
import sys
from pathlib import Path
from elftools.elf.elffile import ELFFile
W = Path(__file__).resolve().parents[2]  # repository root
sys.path[:0] = [str(W / "tools"), str(W / "tests")]
from boot_security_resident import LENGTHS


def load(elf_path):
    """Return (image{virtual:byte}, symbols{'_name':addr}, funcs[(virtual,size,name)])."""
    elf = ELFFile(open(elf_path, "rb"))
    image = {}
    for seg in elf.iter_segments():
        if seg["p_type"] != "PT_LOAD" or not seg["p_filesz"]:
            continue
        data, vaddr, paddr = seg.data(), seg["p_vaddr"], seg["p_paddr"]
        if paddr < 0x8000:
            base = paddr  # load address: initialized-data images are copied out of CODE by startup
        else:
            bank = paddr // 0x8000
            assert vaddr == 0x8000 and paddr % 0x8000 == 0, (hex(vaddr), hex(paddr))
            base = (bank << 16) | 0x8000
        for i, b in enumerate(data):
            image[base + i] = b
    sec_bank = {}
    for i, s in enumerate(elf.iter_sections()):
        if s.name.startswith(".bank"):
            sec_bank[i] = int(s.name[5:])
    symbols, funcs = {}, []
    for sym in elf.get_section_by_name(".symtab").iter_symbols():
        if not sym.name or sym["st_shndx"] in ("SHN_UNDEF",):
            continue
        value = sym["st_value"]
        shndx = sym["st_shndx"]
        if isinstance(shndx, int) and shndx in sec_bank:
            value |= sec_bank[shndx] << 16
        symbols.setdefault("_" + sym.name, value)
        if sym["st_info"]["type"] == "STT_FUNC" and sym["st_size"]:
            funcs.append((value, sym["st_size"], sym.name))
    return image, symbols, sorted(set(funcs))


def decode(image, funcs):
    """Linear decode inside every sized function: {virtual_pc: raw bytes}."""
    code = {}
    for start, size, name in funcs:
        pc = start
        while pc < start + size:
            op = image[pc]
            n = LENGTHS[op]
            code[pc] = bytes(image[pc + i] for i in range(n))
            pc += n
        assert pc == start + size, (name, hex(pc), hex(start + size))
    return code
