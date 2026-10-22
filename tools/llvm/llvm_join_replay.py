#!/usr/bin/env python3
"""Study-only: replay host join vectors against an LLVM MCS51 ELF in s51.

Forked from tests/boot_join_smoke.py (unchanged in the repository). Differences:
ELF-derived image/sites instead of SDCC .rst/.cdb, banked wait/poll/fault hooks,
IDATA stack 0x80..0xff, startup-cleared XRAM, and adjacency decided per event
then validated against the actual stop PCs (with retry) instead of SDCC
codegen patterns.
"""
import argparse
import hashlib
import json
import os
import pickle
import shutil
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time

from elftools.elf.elffile import ELFFile
import llvm_image  # sets sys.path for the repository harness
import boot_banked as banking
import boot_join_smoke as J
from boot_banked_join import model
from boot_banked_security import capture, captured_sections, store
from boot_image import check_alias, check_pc, marker, memory_dump, simulate_binary_dumps
import boot_image


def simulate(simulator, commands, image=None):
    # Same as boot_image.simulate, but with an adjustable timeout (SIM_TIMEOUT, default 15 s).
    import os
    with tempfile.TemporaryDirectory(prefix="cc2530-sim-") as directory:
        script = Path(directory) / "commands"
        script.write_text("\n".join(commands + ["quit", ""]), encoding="ascii")
        if os.environ.get("SAVE_SIM_COMMANDS"): Path(os.environ["SAVE_SIM_COMMANDS"]).write_text("\n".join(commands) + "\n")
        argv = [simulator, "-t", "C52", "-q", "-c", "-"] + ([str(image)] if image is not None else [])
        try:
            result = subprocess.run(argv, input=f'exec "{script}"\n', capture_output=True, text=True,
                                    timeout=float(os.environ.get("SIM_TIMEOUT", "15")), check=True)
        except subprocess.CalledProcessError:
            (DIAG / "crash-commands.txt").write_text("\n".join(commands) + "\n")
            raise
        except subprocess.TimeoutExpired:
            (DIAG / "timeout-commands.txt").write_text("\n".join(commands) + "\n")
            last = max(k for k, c in enumerate(commands) if c.startswith("break "))
            diag = commands[:last+1] + ["break iram w 0xff", "break iram w 0x00", "break iram w 0x08"]
            for c in commands[last+1:]:
                diag.append(c)
                if c == "run":
                    diag += ["state", "dump /h sfr 0x81 0x81", "dump /h sfr 0x9f 0x9f",
                             "dump /h sfr 0xc7 0xc7", "dump /h iram 0x00 0xff"]
                    break
            script.write_text("\n".join(diag + ["quit", ""]), encoding="ascii")
            try:
                out = subprocess.run(argv, input=f'exec "{script}"\n', capture_output=True, text=True,
                                     timeout=120).stdout
            except subprocess.TimeoutExpired as e:
                out = "DIAG TIMEOUT\n" + str(e.stdout or "")[-20000:]
            (DIAG / "timeout-diag.txt").write_text(out[-40000:])
            raise
    folded = result.stdout.lower()
    boot_image.require(not any(e in folded for e in ("unknown command", "no such command", "syntax error", "error:")),
                       "Simulator rejected a command")
    return result.stdout


boot_image.simulate = simulate
from boot_mac_smoke import pc_in
from boot_radio_tx_fixture import sections
from boot_security_resident import LENGTHS
from boot_zigbee_security import AES_ALIAS
from verify_firmware import peripheral_accesses, require

HERE = Path(__file__).resolve().parent
# Failure diagnostics only; never part of an acceptance decision.
DIAG = Path(os.environ.get("LLVM_SIM_DIAG", tempfile.gettempdir()))


def code_map(elf_path):
    """Linear sweep of every executable section, resynchronised at each symbol."""
    elf = ELFFile(open(elf_path, "rb"))
    secs = {}
    for i, s in enumerate(elf.iter_sections()):
        if s["sh_flags"] & 4 and s["sh_size"]:
            bank = int(s.name[5:]) if s.name.startswith(".bank") else 0
            secs[i] = (s, bank)
    starts, objects = {i: {s["sh_addr"]} for i, (s, _) in secs.items()}, []
    for sym in elf.get_section_by_name(".symtab").iter_symbols():
        i = sym["st_shndx"]
        if isinstance(i, int) and i in secs:
            if sym["st_info"]["type"] == "STT_OBJECT" and sym["st_size"]:
                objects.append((i, sym["st_value"], sym["st_value"] + sym["st_size"]))
            else:
                starts[i].add(sym["st_value"])
    code = {}
    for i, (s, bank) in secs.items():
        data, base, end = s.data(), s["sh_addr"], s["sh_addr"] + s["sh_size"]
        skip = [(a, b) for j, a, b in objects if j == i]
        bounds = sorted(a for a in starts[i] if base <= a < end) + [end]
        for a, b in zip(bounds, bounds[1:]):
            pc = a
            while pc < b:
                if any(x <= pc < y for x, y in skip):
                    pc += 1
                    continue
                n = LENGTHS[data[pc - base]]
                if pc + n > end:
                    break
                code[(bank << 16) | pc if bank else pc] = bytes(data[pc - base:pc - base + n])
                pc += n
    return code


def artifacts(elf_path):
    image, symbols, _ = llvm_image.load(elf_path)
    elf = ELFFile(open(elf_path, "rb"))
    bss = elf.get_section_by_name(".bss")
    symbols["l_BSS_END"] = bss["sh_addr"] + bss["sh_size"]
    return image, symbols, code_map(elf_path)


def branch_targets(code):
    targets = set()
    for pc, raw in code.items():
        op, n = raw[0], len(raw)
        bank = pc & ~0xffff
        rel = None
        if op in (0x80, 0x40, 0x50, 0x60, 0x70) or 0xd8 <= op <= 0xdf:
            rel = raw[1]
        elif op in (0x10, 0x20, 0x30, 0xd5) or 0xb4 <= op <= 0xbf:
            rel = raw[2]
        if rel is not None:
            targets.add(bank | ((pc + n + (rel - 256 if rel > 127 else rel)) & 0xffff))
        if op in (0x02, 0x12):
            targets.add(bank | int.from_bytes(raw[1:3], "big") if int.from_bytes(raw[1:3], "big") >= 0x8000
                        else int.from_bytes(raw[1:3], "big"))
        if op & 0x1f in (0x01, 0x11):
            targets.add(bank | (((pc + n) & 0xf800) | (op >> 5) << 8 | raw[1]))
    return targets


def static_dptr(pc, code, targets, order):
    """DPTR at a movx if provably set by straight-line code, else None."""
    index = order[pc]
    offset = 0
    i = index - 1
    while i >= 0 and index - i < 64:
        prev = order_list[i]
        if prev + len(code[prev]) != order_list[i + 1] or order_list[i + 1] in targets:
            return None
        raw = code[prev]
        op = raw[0]
        if op == 0x90:
            return (int.from_bytes(raw[1:3], "big") + offset) & 0xffff
        if op == 0xa3:
            offset += 1
        elif op in (0x12, 0x02, 0x22, 0x32, 0x73) or op & 0x1f in (0x01, 0x11):
            return None
        elif any(b in (0x82, 0x83) for b in raw[1:]) and op not in (0x74, 0x24, 0x34, 0x44, 0x54, 0x64, 0x94, 0xb4):
            return None
        i -= 1
    return None


order_list = []


def sites_for(image, symbols, code):
    sites = {}
    for pc, raw, reg in peripheral_accesses(code):
        if reg not in J.PERIPHERAL_SFR:
            continue
        write = raw[0] in (0x75, 0xf5) or 0x88 <= raw[0] <= 0x8f
        destination = reg if write or raw[0] == 0xb5 else (
            raw[2] if raw[0] == 0x85 else raw[0] - 0xa8 if 0xa8 <= raw[0] <= 0xaf else 0xe0)
        require(raw[0] in (0x75, 0xf5, 0xe5, 0x45), f"Unmodelled SFR access {raw.hex()} at {pc:x}")
        sites[pc] = ("w" if write else "r", reg, destination)
    global order_list
    order_list = sorted(code)
    order = {pc: i for i, pc in enumerate(order_list)}
    targets = branch_targets(code)
    pruned = 0
    for pc, raw in code.items():
        if raw in (b"\xe0", b"\xf0"):
            dptr = static_dptr(pc, code, targets, order)
            if dptr is not None and not 0x6000 <= dptr < 0xe800:
                pruned += 1
                continue
            sites[pc] = ("r", None, 0xe0) if raw == b"\xe0" else ("w", None, None)
        # Inlined AES arm settles (9 NOPs after DMAARM) and the CCA settle (4 NOPs).
        if raw == b"\x00" and code.get(pc - 1) != b"\x00":
            run = 0
            while code.get(pc + run) == b"\x00":
                run += 1
            if run == 9:
                require(code.get(pc - 3, b"")[:2] == b"\x75\xd6", "9-NOP run not after DMAARM")
            if run in (4, 9):
                sites[pc] = ("c", 0, run)
    template = symbols["_flash_exec_template"]
    end = symbols["_flash_exec_template_end"]
    ram = symbols["_flash_exec_ram"] + 0x8000
    for pc, site in tuple(sites.items()):
        if template <= pc < end:
            del sites[pc]
            sites[ram + pc - template] = site
    sites_for.pruned = pruned
    sites_for.relocated = (ram, ram + end - template, template)
    return sites


def flash_stop(image, symbols):
    template, end = symbols["_flash_exec_template"], symbols["_flash_exec_template_end"]
    raw = bytes(image[template + i] for i in range(end - template))
    require(raw.count(b"\x80\xfe") == 1, "Missing unique retained flash loop")
    return symbols["_flash_exec_ram"] + 0x8000 + raw.index(b"\x80\xfe"), raw


def hook(symbols, name):
    """(logical pc, s51 break command) for a possibly banked hook."""
    v = symbols["_join_smoke_" + name]
    if v >= 0x10000:
        return v & 0xffff, f'break {v & 0xffff:#x} 1 if "(sfr[0x9f]=={v >> 16})&&((sfr[0xc7]&8)==0)"'
    return v, f"break {v:#x}"


def stops_for(sites, symbols, ram_stop):
    conditions = {}
    for pc, (kind, address, _) in sites.items():
        if kind == "r" and address in J.PASSIVE:
            continue
        clauses = []
        if pc >= 0x10000:
            clauses += [f"sfr[0x9f]=={pc >> 16}", "(sfr[0xc7]&8)==0"]
        elif pc >= 0x8000:
            clauses.append("(sfr[0xc7]&8)!=0")
        if kind != "c" and address is None:
            clauses += ["((sfr[0x83]*256+sfr[0x82])>=0x6000)", "((sfr[0x83]*256+sfr[0x82])<0xe800)"]
            if kind == "r":
                clauses += ["((sfr[0x83]*256+sfr[0x82])!=0x6270||xram[0x6270]!=4)",
                            "((sfr[0x83]*256+sfr[0x82])!=0x624a)",
                            "((sfr[0x83]*256+sfr[0x82])!=0x6276)",
                            "((sfr[0x83]*256+sfr[0x82])!=0x6277)"]
        if kind == "r" and address in J.CONDITION:
            clauses.append(J.CONDITION[address])
        conditions.setdefault(pc & 0xffff, []).append("&&".join(f"({c})" for c in clauses) or "1")
    conditions.setdefault(ram_stop, []).append("(sfr[0xc7]&8)!=0")
    plain = []
    for name in ("wait", "fault"):
        v = symbols["_join_smoke_" + name]
        if v >= 0x10000:
            conditions.setdefault(v & 0xffff, []).append(f"(sfr[0x9f]=={v >> 16})&&((sfr[0xc7]&8)==0)")
        else:
            require(v not in conditions, "Hook collides with an MMIO site")
            plain.append(f"break {v:#x}")
    return [f'break {pc:#x} 1 if "' + "||".join(f"({c})" for c in cl) + '"'
            for pc, cl in conditions.items()] + plain


def check_breakpoints(text, commands):
    expected = [int(c.split()[1], 0) for c in commands if c.startswith("break ")]
    actual = [int(a, 16) for a in re.findall(r"^Breakpoint \d+ at 0x([0-9a-fA-F]+):", text, re.M)]
    require(actual == expected, "Simulator did not accept the complete breakpoint inventory")


def stack_peak(text):
    records = re.findall(r"^Max value of stack pointer=[ \t]*0x([0-9a-fA-F]+)", text, re.M)
    require(len(records) == 1, "Missing full-run stack high-water")
    return int(records[0], 16)


def next_pc(pc, sites, code):
    kind, _, extra = sites[pc]
    if kind == "c":
        return pc + extra
    raw = code.get(pc)
    ram, ram_end, template = getattr(sites_for, "relocated", (0, 0, 0))
    if raw is None and ram <= pc < ram_end:  # site in the copied flash template
        raw = code.get(template + pc - ram)
    if raw is None:
        return None
    return pc + len(raw)


def fires(site, event, dptr):
    kind, address, _ = site
    return kind == event[0] and (address == event[1] if address is not None else dptr == event[1])


def guess(prev, cur, learned):
    return learned.get((tuple(prev[:2]), tuple(cur[:2])), False) if prev else False


def check_poll_loop(simulator, base, symbols, after, sp):
    names = ("wait", "poll", "fault")
    hooks = {n: hook(symbols, n) for n in names}
    stops = [c for _, c in hooks.values()]
    commands = base + stops + ["set memory sfr 0x9f 0", "set memory sfr 0xa8 0"]
    expected = ("wait", "wait", "wait", "poll", "poll", "fault")
    for phase, name in enumerate(expected, 1):
        commands += [f"set memory xram {symbols['_join_smoke_status']+6:#x} {phase}",
                     f"set memory sfr 0x81 {sp:#x}", "set memory sfr 0x9f 0", f"run {after:#x}",
                     marker(phase*2), "state", "dump /h sfr 0x9f 0x9f", marker(phase*2+1)]
    text = simulate(simulator, commands)
    check_breakpoints(text, stops)
    parts = sections(text)
    for phase, name in enumerate(expected, 1):
        check_pc(parts[phase*2], hooks[name][0])
        v = symbols["_join_smoke_" + name]
        require(memory_dump(parts[phase*2], 0x9f, 1)[0] == v >> 16, f"{name} hook reached in wrong bank")


def chunk_commands(base, state, pc, stops, symbols, sites, events, first, last, adjacency,
                   prologue):
    commands = base + J.resume(state, pc) + stops
    commands += [f"set memory sfr 0x9f {state[2][0x1f]}"]
    commands += banking.xmap() if state[2][0x47] & 8 else banking.code_banks()
    commands += capture(60000) + prologue
    checks, number = [], 10
    reads = {}
    for k, a, d in sites.values():
        if k == "r":
            reads.setdefault(a, set()).add(d)
    for j in range(first, last):
        kind, address, value, effects, dma, flash = events[j]
        if not adjacency[j]:
            commands.append("run")
        commands += [marker(number), "state", "dump /h sfr 0x81 0x83",
                     "dump /h sfr 0x9f 0x9f", "dump /h sfr 0xc7 0xc7"]
        if os.environ.get("DIAG_LOGBP"):
            commands.append("dump /h xram 0x1d00 0x1dff")
        commands.append(marker(number+1))
        if kind == "w" and address == 0x6270:
            ram = symbols["_flash_exec_ram"]
            commands.append(f"dump /h xram {ram:#x} {ram+len(flash_template)-1:#x}")
        if dma:
            phase, source, output = dma
            require(phase in (1, 2, 3), "Unknown AES DMA phase")
            for name, size in (("_aes_dma0", 8), ("_aes_dma1", 32),
                               (("_aes_key", "_aes_iv", "_aes_input")[phase-1], 16)):
                a = symbols[name]
                commands.append(f"dump /h xram {a:#x} {a+size-1:#x}")
            if phase == 3:
                commands += store("xram", symbols["_aes_output"], bytes.fromhex(output))
        commands.append(marker(number+2))
        if os.environ.get("DIAG_IRAM"):
            commands.append("dump /h iram 0x0 0xff")
        space = "sfr" if address < 256 else "xram"
        if kind == "r":
            commands.append(f"set memory {space} {address:#x} {value:#x}")
        commands += [f"step {value if kind == 'c' else 1}", marker(number+3), "state"]
        if kind == "r":
            for dest in sorted(reads.get(None, set()) | reads.get(address, set())):
                commands.append(f"dump /h {'iram' if dest < 128 else 'sfr'} {dest:#x} {dest:#x}")
        elif kind == "w":
            commands.append(f"dump /h {space} {address:#x} {address:#x}")
        commands.append(marker(number+4))
        commands += [f"set memory sfr {a:#x} {v:#x}" for a, v in effects]
        if kind == "w" and address == 0xc7:
            commands += banking.xmap() if value & 8 else banking.code_banks()
        if flash:
            commands += flash_media(flash, events, j)
        checks.append((number, j))
        number += 5
    if last == len(events):
        commands.append("run")
    return commands + capture(60010), checks


def flash_media(flash, events, j):
    """Model controller completion from the actual address/data write events."""
    faddr = [None, None]
    for k in range(j, -1, -1):
        kind, address, value = events[k][:3]
        if kind == "w" and address in (0x6271, 0x6272) and faddr[address-0x6271] is None:
            faddr[address-0x6271] = value
    require(None not in faddr, "Flash completion without an address in this step")
    address_nv = (faddr[0] | faddr[1] << 8) * 4
    require(0x3e800 <= address_nv <= 0x3f7fc, "Controller escaped NV pages")
    if flash[0] == 1:
        require(address_nv % 2048 == 0, "Controller erase misalignment")
        return [f"fill flash {address_nv:#x} {address_nv+2047:#x} 0xff"]
    return store("flash", address_nv, bytes.fromhex(flash[1]))


flash_template = b""


def sha_file(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


class Suspended(Exception):
    """Time budget reached right after a bound step snapshot; the run is NOT complete."""
    def __init__(self, path, index, completed, count):
        super().__init__(f"suspended at step{index}")
        self.path, self.index, self.completed, self.count = path, index, completed, count


RX_DIAG = (0x1e40, 0x1e90)
RX_DIAG_RESET = b"RXDG\x01\x50" + bytes(74)


class Mismatch(Exception):
    def __init__(self, j, message):
        super().__init__(message)
        self.j = j


def replay(simulator, elf, vector, directory, *, limit=None, chunk=256, log=print):
    global flash_template
    image, symbols, code = artifacts(elf)
    sites = sites_for(image, symbols, code)
    ram_stop, flash_template = flash_stop(image, symbols)
    stops = stops_for(sites, symbols, ram_stop)
    base = ["set option analyzer false"] + model(image, directory) + [AES_ALIAS]
    wait, _ = hook(symbols, "wait")
    fault, _ = hook(symbols, "fault")
    poll = symbols["_join_smoke_poll"]
    thunk = [pc for pc, raw in code.items() if pc < 0x8000 and raw == b"\x12" + (poll & 0xffff).to_bytes(2, "big")
             and code.get(pc - 3) == bytes((0x75, 0x9f, poll >> 16))]
    require(len(thunk) == 1, "Missing unique poll thunk")
    thunk = thunk[0] - 5
    main = symbols["_main"]
    after = [pc + 3 for pc, raw in code.items() if pc < 0x8000 and raw == b"\x12" + thunk.to_bytes(2, "big")]
    require(len(after) == 1, "Missing actual main poll continuation")
    after = after[0]
    stops.append(f"break {after:#x}")
    # DIAG_LOGBP="bank:pc:tag;..." adds never-stopping breakpoints that log
    # (tag, A, R7, iram 0x30..0x32) into a 42-slot XDATA ring at 0x1d01
    # (next slot at 0x1d00, total hits at 0x1dff).
    for spec in filter(None, os.environ.get("DIAG_LOGBP", "").split(";")):
        bank, lpc, tag = (int(x, 0) for x in spec.split(":"))
        gate = f"((sfr[0x9f]=={bank})&&((sfr[0xc7]&8)==0))" if lpc >= 0x8000 else "1"
        rec = lambda k, v: f"(xram[{0x1d01+k:#x}+6*xram[0x1d00]]={v})*0"
        # ucsim does not short-circuit: always write the next slot, advance only if gated.
        cond = "(" + "+".join([rec(0, tag), rec(1, "sfr[0xe0]"), rec(2, "iram[7]")]
                              + [rec(3 + i, f"iram[{0x30+i:#x}]") for i in range(3)]
                              + [f"(xram[0x1dff]=xram[0x1dff]+{gate})*0",
                                 f"(xram[0x1d00]=(xram[0x1d00]+{gate})%42)*0"]) + ")"
        same = [i for i, c in enumerate(stops) if int(c.split()[1], 0) == lpc]
        if same:
            prior = stops[same[0]]
            prior = prior.split(' if "', 1)[1][:-1] if ' if "' in prior else "1"
            stops[same[0]] = f'break {lpc:#x} 1 if "({cond})||({prior})"'
        else:
            stops.append(f'break {lpc:#x} 1 if "{cond}"')
    initial = {int(a): v for a, v in vector["initial"].items()} | J.CONSTANT | {0x6270: 4}
    bss_end = symbols["l_BSS_END"]
    commands = base + ["fill iram 0 0xff 0xa5", "fill xram 0 0x1eff 0xa5", "fill xram 0x2000 0x7fff 0x69"]
    commands += [f"set memory {'sfr' if a < 256 else 'xram'} {a:#x} {v:#x}" for a, v in initial.items()]
    commands += store("flash", 0x3e800, J.hex_bytes(vector["nv_initial"], 4096))
    commands += [f"run 0 {main:#x}", marker(59990), "state", "dump /h sfr 0x81 0x81", marker(59991)]
    commands += ["fill iram 0x82 0xff 0xc7", f"fill xram {bss_end:#x} 0x1dff 0xa5",
                 "fill xram 0x1e30 0x1eff 0xa5"]
    if os.environ.get("DIAG_LOGBP"):
        commands += ["set memory xram 0x1d00 0", "set memory xram 0x1dff 0"]
    _, wait_break = hook(symbols, "wait")
    commands += [wait_break, f"run {main:#x}"] + capture(60000)
    parts = sections(simulate_binary_dumps(simulator, commands))
    check_pc(parts[59990], main)
    sp_main = memory_dump(parts[59990], 0x81, 1)[0]
    require(sp_main <= 0x81, f"SP at main {sp_main:#x}")
    state = captured_sections(parts, 60000, wait)
    reset_status = state[0][0x1e00:0x1e08]
    # Opt-in CC2530_RX_DIAG image: the only owner of 0x1e40..0x1e8f (80 bytes).
    rx_diag = symbols.get("_cc2530_rx_diag")
    require(rx_diag in (None, RX_DIAG[0]), f"cc2530_rx_diag at {rx_diag!r}, not {RX_DIAG[0]:#x}")
    if rx_diag is not None:
        require(state[0][RX_DIAG[0]:RX_DIAG[1]] == RX_DIAG_RESET, "RX_DIAG block not reset to RXDG v1")
    require(reset_status == b"JSN1\x02\x30\x01\0"
            and state[0][symbols["_join_smoke_mailbox"]:symbols["_join_smoke_mailbox"]+8] == bytes(8),
            f"Genuine reset did not produce DISARMED: {reset_status.hex()}")
    check_poll_loop(simulator, base, symbols, after, sp_main)
    reset_peak = stack_peak(parts[60000])  # high-water of the reset->wait run; seeds the IRAM check
    pc, count, peak, sampled = wait, 0, max(sp_main, reset_peak), sp_main
    physical = state[4]
    completed, retries, learned = 0, 0, {}
    previous_stop = None  # (pc, dptr) of the last executed event
    snap_dir = os.environ.get("SNAP_DIR")
    if snap_dir:
        Path(snap_dir).mkdir(parents=True, exist_ok=True)
    snap_every = int(os.environ.get("SNAP_EVERY", "10"))
    snap_events = int(os.environ.get("SNAP_EVENTS", "4096"))
    resume = os.environ.get("RESUME")
    # A snapshot is only valid for the exact image, scenario, simulator binary and
    # replay logic; suspension happens only right after a step-boundary snapshot.
    bind = {"elf": sha_file(elf), "case": vector["case"], "simulator": sha_file(shutil.which(simulator) or simulator),
            "replay": sha_file(__file__), "vector": hashlib.sha256(json.dumps(vector, sort_keys=True).encode()).hexdigest()}
    stop_after = float(os.environ.get("STOP_AFTER_SECONDS", "0"))
    started = time.time()
    if resume:
        with open(resume, "rb") as fh:
            snap = pickle.load(fh)
        require(snap["elf_sha"] == sha_file(elf) and snap["case"] == vector["case"], "Snapshot is for another image/case")
        require(snap.get("bind") == bind, "Snapshot is bound to another simulator/replay/vector identity")
        (state, pc, count, peak, sampled, completed, retries, learned, previous_stop) = (
            snap[k] for k in ("state", "pc", "count", "peak", "sampled", "completed", "retries", "learned",
                              "previous_stop"))
        log(f"resumed at step{snap['index']}")
    for index, step in enumerate(J.events_for(vector)):
        if limit is not None and index == limit:
            break
        if resume and index < snap["index"]:
            continue
        if snap_dir and index % snap_every == 0 and not (resume and index == snap["index"]):
            snap_path = Path(snap_dir) / f"case{vector['case']}-step{index}.pkl"
            with open(snap_path, "wb") as fh:
                pickle.dump({"index": index, "case": vector["case"], "elf_sha": sha_file(elf), "state": state,
                             "pc": pc, "count": count, "peak": peak, "sampled": sampled, "completed": completed,
                             "retries": retries, "learned": learned, "previous_stop": previous_stop,
                             "bind": bind}, fh)
            if stop_after and time.time() - started > stop_after:
                raise Suspended(snap_path, index, completed, count)
        events = [relocate_dma_cfg(e, symbols) for e in step["events"]]
        batch = min(chunk, 128) if any(e[:2] == ["w", 0xc7] for e in events) else chunk
        adjacency = [False] * len(events)
        first = 0
        if resume and index == snap["index"] and snap.get("first"):
            first, adjacency, previous_stop = snap["first"], snap["adjacency"], snap["previous_stop"]
        while first < max(1, len(events)):
            last = min(first + batch, len(events))
            if snap_dir and first and first % snap_events < batch and not (resume and first == snap.get("first")):
                with open(Path(snap_dir) / f"case{vector['case']}-step{index}-ev{first}.pkl", "wb") as fh:
                    pickle.dump({"index": index, "case": vector["case"], "elf_sha": sha_file(elf), "state": state,
                                 "pc": pc, "count": count, "peak": peak, "sampled": sampled, "completed": completed,
                                 "retries": retries, "learned": learned, "first": first, "adjacency": adjacency,
                                 "previous_stop": previous_stop, "bind": bind}, fh)
            for j in range(first, last):
                adjacency[j] = j > 0 and guess(events[j-1], events[j], learned)
            flipped = set()
            for attempt in range(64):
                prologue = []
                if first == 0:
                    if pc == after and not J.active_phase(state[0][0x1e06]):
                        prologue += ["step 1", "run", marker(59980), "state", marker(59981)]
                    if index == 0:
                        prologue += store("xram", symbols["_join_smoke_phase"], bytes.fromhex(vector["admission"]))
                        prologue += store("xram", symbols["_join_smoke_draws"], bytes.fromhex(vector["draws"]))
                    require(pc in (wait, after), "Input outside public foreground boundary")
                    packet = bytes.fromhex(step["packet"])
                    require(not J.active_phase(state[0][0x1e06]) or packet == bytes(8), "Invalid mailbox handoff")
                    prologue += store("xram", symbols["_join_smoke_mailbox"], packet) + ["step 1"]
                commands, checks = chunk_commands(base, state, pc, stops, symbols, sites, events,
                                                  first, last, adjacency, prologue)
                if os.environ.get("SAVE_CHUNK_COMMANDS"):
                    Path(os.environ["SAVE_CHUNK_COMMANDS"]).write_text("\n".join(commands) + "\n")
                try:
                    text = simulate_binary_dumps(simulator, commands)
                    if os.environ.get("SAVE_CHUNK_OUTPUT"):
                        Path(os.environ["SAVE_CHUNK_OUTPUT"]).write_text(text)
                except subprocess.CalledProcessError as error:
                    (DIAG / "fail-commands.txt").write_text("\n".join(commands) + "\n")
                    (DIAG / "fail-output.txt").write_text(str(error.stdout)[-20000:] + "\n--stderr--\n" + str(error.stderr)[-5000:])
                    raise
                check_breakpoints(text, stops)
                parts = sections(text)
                restored = captured_sections(parts, 60000, pc)
                if restored != state:
                    for name, a, b in zip(("xram", "iram", "sfr", "ext", "flash"), restored, state):
                        diff = [(hex(i), x, y) for i, (x, y) in enumerate(zip(a, b)) if x != y]
                        if diff:
                            print(f"restore diff {name}: n={len(diff)} first={diff[:3]} last={diff[-3:]}", file=sys.stderr)
                    raise ValueError("Continuation changed machine state")
                if first == 0 and pc == after and not J.active_phase(state[0][0x1e06]):
                    check_pc(parts[59980], wait)
                try:
                    stops_seen = verify_chunk(parts, checks, events, sites, symbols, code, previous_stop, adjacency)
                    break
                except Mismatch as error:
                    if error.j in flipped:
                        raise RuntimeError(f"step{index} event{error.j}: {error} (both adjacency choices fail)")
                    flipped.add(error.j)
                    retries += 1
                    adjacency[error.j] = not adjacency[error.j]
                    log(f"  retry step{index} event{error.j}: adjacency->{adjacency[error.j]} ({error})")
            else:
                raise RuntimeError("Adjacency did not converge")
            for j, (here, dptr, sp) in stops_seen.items():
                if j > 0:
                    learned[(tuple(events[j-1][:2]), tuple(events[j][:2]))] = adjacency[j]
                sampled = max(sampled, sp)
                previous_stop = (here, dptr)
            count += last - first
            pc = pc_in(parts[60010])
            state = captured_sections(parts, 60010, pc)
            peak = max(peak, stack_peak(parts[60010]))
            require(peak < 0xff and state[1][peak+1:] == b"\xc7" * (0xff - peak),
                    f"IDATA stack overflow or IRAM above stack peak {peak:#x} changed: "
                    + " ".join(f"{a:#x}={state[1][a]:02x}" for a in range(peak + 1, len(state[1])) if state[1][a] != 0xc7)
                    + f" (step{index}, pc {pc:#x})")
            unowned = bytearray(state[0][bss_end:0x1e00])
            if os.environ.get("DIAG_LOGBP"):
                unowned[0x1d00 - bss_end:] = b"\xa5" * (0x1e00 - 0x1d00)
            high = state[0][0x1e30:0x1f00]
            if rx_diag is not None:
                diag_block = high[RX_DIAG[0] - 0x1e30:RX_DIAG[1] - 0x1e30]
                require(diag_block[:6] == RX_DIAG_RESET[:6], "RX_DIAG header changed")
                high = high[:RX_DIAG[0] - 0x1e30] + high[RX_DIAG[1] - 0x1e30:]
            require(unowned == b"\xa5" * (0x1e00 - bss_end)
                    and high == b"\xa5" * len(high), "Unowned XDATA changed")
            require(state[4][:0x3e800] == physical[:0x3e800] and state[4][0x3f800:] == physical[0x3f800:],
                    "Firmware/lock flash changed")
            if last == len(events):
                expected_pc = after if step["returned"] else ram_stop
                if pc != expected_pc and os.environ.get("DIAG_FINAL"):
                    Path(os.environ["DIAG_FINAL"]).write_text(json.dumps(
                        {"step": index, "pc": pc, "sp": state[2][1],
                         "fmap": state[2][0x1f], "memctr": state[2][0x47], "iram": state[1].hex(), "xdata": state[0].hex()}, indent=1))
                require(pc == expected_pc, f"Wrong caller return/RAM-stop boundary: step{index}, PC {pc:x}")
                if os.environ.get("DIAG_STEPS"):
                    with open(os.environ["DIAG_STEPS"], "a") as fh:
                        fh.write(json.dumps({"step": index, "pc": pc, "xdata": state[0].hex()}) + "\n")
                expected = bytes.fromhex(step["status"])
                require(state[0][0x1e00:0x1e30] == expected,
                        f"Status step{index}: {state[0][0x1e00:0x1e30].hex()} != {expected.hex()}")
                require(state[4][0x3e800:0x3f800] == bytes.fromhex(step["nv"]), "Actual journal differs")
                completed += 1
                previous_stop = None
            log(f"join step{index} events {last}/{len(events)} SP{peak:02x} retries {retries}")
            first = last if events else 1
    finished = completed == len(vector["steps"])
    result = {"complete": finished and limit is None, "steps": completed, "of_steps": len(vector["steps"]),
              "peripheral_stops": count, "peak_sp": peak, "reset_peak_sp": reset_peak, "sampled_sp": sampled,
              "stack_bytes": peak - 0x7f, "phase": state[0][0x1e06], "adjacency_retries": retries}
    if rx_diag is not None:
        result["rx_diag"] = state[0][RX_DIAG[0]:RX_DIAG[1]].hex()
    if finished and vector["case"] == J.FLASH_BUSY_CASE:
        require(state[0][symbols["_flash_write_status"]] == 10
                and state[0][symbols["_nv_record_diagnostic"]+4] == 13
                and state[0][symbols["_flash_exec_ram"]:symbols["_flash_exec_ram"]+len(flash_template)] == flash_template,
                "Retained flash call lost writer/journal/template ownership")
        commands = base + J.resume(state, pc) + ["step 16"] + capture(60000)
        commands += ["set memory xram 0x6270 4", "step 16"] + capture(60010)
        parts = sections(simulate_binary_dumps(simulator, commands))
        J.check_flash_retention(state, captured_sections(parts, 60000, ram_stop))
        J.check_flash_retention(state, captured_sections(parts, 60010, ram_stop), idle=True)
        result["terminal"] = "flash-ram-stop"
    elif finished and vector["case"] == 0:
        require(state[0][0x1e06] == 5 and pc == after, "READY did not keep serving")
        result["terminal"] = "serving"
    elif finished:
        require(state[0][0x1e06] == 6, "Wrong terminal state")
        commands = base + J.resume(state, pc) + stops + ["step 1", "run"] + capture(60000)
        commands += ["step 16"] + capture(60010)
        parts = sections(simulate_binary_dumps(simulator, commands))
        retained = captured_sections(parts, 60000, fault)
        require(captured_sections(parts, 60010, fault) == retained, "Terminal loop changed retained state")
        require(retained[0] == state[0] and retained[4] == state[4], "Terminal transition changed owners/media")
        result["terminal"] = "fault"
    return result


# The host reference writes DMAxCFG with the SDCC target layout (TARGET_aes_dma*
# in ref/join_smoke_layout.h). The image must instead program the addresses of
# its own descriptors, whose contents are already checked against its symbols.
DMA_CFG = {0xd4: ("aes_dma0", 0), 0xd5: ("aes_dma0", 8), 0xd2: ("aes_dma1", 0), 0xd3: ("aes_dma1", 8)}
TARGET_LAYOUT = {m.group(1): int(m.group(2)) for m in re.finditer(
    r"^#define TARGET_(\w+) (\d+)u$", (HERE / "ref/join_smoke_layout.h").read_text(), re.M)}


def relocate_dma_cfg(event, symbols):
    kind, address, value = event[:3]
    if kind not in ("r", "w") or address not in DMA_CFG:
        return event
    name, shift = DMA_CFG[address]
    if kind == "r" and value == 0:  # channel not configured
        return event
    require(value == (TARGET_LAYOUT[name] >> shift) & 0xff,
            f"Reference DMA CFG {kind} {address:#x}={value:#x} is not the reference {name} address")
    return [kind, address, (symbols["_" + name] >> shift) & 0xff, *event[3:]]


def verify_chunk(parts, checks, events, sites, symbols, code, previous_stop, adjacency):
    seen, last_ok = {}, None
    for n, j in checks:
        kind, address, value, effects, dma, flash = events[j]
        here = pc_in(parts[n])
        bank = memory_dump(parts[n], 0x9f, 1)[0]
        mapping = memory_dump(parts[n], 0xc7, 1)[0]
        regs = memory_dump(parts[n], 0x81, 3)
        dptr = regs[1] | regs[2] << 8
        logical = here | bank << 16 if here >= 0x8000 and not mapping & 8 else here
        if logical not in sites:
            if os.environ.get("DIAG_STACK") and last_ok:
                pn, pj, plog, psp = last_ok
                Path(os.environ["DIAG_STACK"]).write_text(json.dumps(
                    {"event": j, "stop": logical, "sp": regs[0], "prev_event": pj, "prev_pc": plog, "prev_sp": psp,
                     "prev_iram": memory_dump(parts[pn+2], 0, 256).hex() if os.environ.get("DIAG_IRAM") else None}, indent=1))
            if os.environ.get("DIAG_IRAM") and regs[0] < 0x80:
                (DIAG / "overflow-iram.json").write_text(json.dumps(
                    {"event": j, "pc": logical, "sp": regs[0], "bank": bank, "memctr": mapping,
                     "iram": memory_dump(parts[n+2], 0, 256).hex()}, indent=1))
            if os.environ.get("DIAG_LOGBP"):
                ring = memory_dump(parts[n], 0x1d00, 0x100)
                Path(os.environ.get("DIAG_LOGOUT", "/tmp/logbp.json")).write_text(json.dumps(
                    {"event": j, "stop": logical, "next": ring[0], "total": ring[0xff],
                     "ring": [ring[1+6*k:7+6*k].hex() for k in range(42)]}, indent=1))
            raise Mismatch(j, f"unexpected stop {logical:x} SP {regs[0]:02x}")
        last_ok = (n, j, logical, regs[0])
        k, a, _ = sites[logical]
        if k != kind or a not in (None, address) or (a is None and kind != "c" and dptr != address):
            raise Mismatch(j, f"identity {logical:x} {sites[logical]} dptr {dptr:x} != {(kind, address)}")
        # Validate the adjacency decision against the actual previous stop.
        if previous_stop is not None and not adjacency[j]:
            p, pd = previous_stop
            nxt = next_pc(p, sites, code)
            if nxt is not None and nxt in sites and nxt != logical and fires(sites[nxt], events[j], pd) \
                    and not (sites[nxt][0] == "r" and sites[nxt][1] in J.PASSIVE):
                raise Mismatch(j, f"run skipped adjacent site {nxt:x}")
        if kind == "w" and address == 0x6270:
            ram = symbols["_flash_exec_ram"]
            require(mapping & 8 and ram+0x8000 <= here < ram+0x8000+len(flash_template)
                    and memory_dump(parts[n+1], ram, len(flash_template)) == flash_template,
                    "Flash command did not execute the genuine copied RAM template")
        if dma:
            phase, source, output = dma
            origin = symbols[("_aes_key", "_aes_iv", "_aes_input")[phase-1]]
            d0 = origin.to_bytes(2, "big") + bytes.fromhex("70b100101d41")
            d1 = b"\x70\xb2" + symbols["_aes_output"].to_bytes(2, "big") + bytes.fromhex("00101e11") + bytes(24)
            for name, expected in (("_aes_dma0", d0), ("_aes_dma1", d1),
                                   (("_aes_key", "_aes_iv", "_aes_input")[phase-1], bytes.fromhex(source))):
                require(memory_dump(parts[n+1], symbols[name], len(expected)) == expected,
                        f"DMA descriptor/input {name} differs from independent AES model")
        if kind == "c":
            check_pc(parts[n+3], here + value)
        else:
            if address >= 256 and regs[1:] != address.to_bytes(2, "little"):
                raise Mismatch(j, "DPTR differs")
            dest = address if kind == "w" else (sites[logical][2])
            actual = memory_dump(parts[n+3], dest, 1)[0]
            if actual != value:
                raise Mismatch(j, f"value at {logical:x}: {actual} != {value}")
        seen[j] = (logical, dptr, regs[0])
        previous_stop = (logical, dptr)
    return seen


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--vectors", type=Path, required=True)
    parser.add_argument("--simulator", default="s51")
    parser.add_argument("--case", type=int, choices=J.CASES, default=2)
    parser.add_argument("--limit", type=int)
    parser.add_argument("--chunk", type=int, default=256)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    vector = json.loads((args.vectors / f"v{args.case}.json").read_text())
    J.validate_reference(vector)
    require(vector["case"] == args.case, "Wrong reference scenario")
    if args.selftest:
        check_alias(args.simulator)
        banking.check_mapping(args.simulator)
        try:
            J.check_debugger(args.simulator)
        except subprocess.CalledProcessError as error:
            print(f"check_debugger: simulator died ({error.returncode})", file=sys.stderr)
    start = time.time()
    with (tempfile.TemporaryDirectory(prefix="llvm-join-") if not os.environ.get("KEEP_SIM_DIR") else __import__("contextlib").nullcontext(tempfile.mkdtemp(prefix="llvm-join-keep-"))) as directory:
        try:
            result = replay(args.simulator, args.elf, vector, Path(directory), limit=args.limit, chunk=args.chunk)
        except Suspended as pause:
            result = {"complete": False, "suspended": True, "resume": str(pause.path), "next_step": pause.index,
                      "steps": pause.completed, "of_steps": len(vector["steps"]), "peripheral_stops": pause.count}
    result |= {"case": args.case, "elf": str(args.elf), "seconds": round(time.time() - start, 1)}
    text = json.dumps(result, indent=1)
    print(text)
    if args.report:
        args.report.write_text(text + "\n")
    if result.get("suspended"):
        sys.exit(75)


if __name__ == "__main__":
    main()
