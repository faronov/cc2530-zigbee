#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Execute the admitted complete board caller with synthetic peripherals only."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import boot_banked as banking
from boot_banked_join import model
from boot_banked_security import capture, captured_sections, store
from boot_image import check_alias, check_pc, marker, memory_dump, simulate, simulate_binary_dumps
from boot_mac_attempt import mmio_sites
from boot_mac_smoke import pc_in
from boot_radio_tx_fixture import sections, stack_high_water
from boot_zigbee_security import AES_ALIAS
from join_smoke_analysis import instructions
from join_smoke_image import identities, verify
from verify_firmware import cdb_address, peripheral_accesses, require


CONSTANT = {0xb8: 0, 0x9a: 0, 0xbe: 4, 0xd7: 0, 0x624a: 0xa5, 0x6276: 0x44, 0x6277: 255}
PASSIVE = set(CONSTANT) | {0xc6, 0xc7}
CONDITION = {
    0xa8: "(sfr[0xd6]&1)!=0",
    0x9e: "sfr[0x9e]!=sfr[0xc6]",
    0xd6: "sfr[0xd6]!=0",
    0x6270: "xram[0x6270]!=4",
}
PERIPHERAL_SFR = frozenset((
    0x80, 0x90, 0xa0, 0xa8, 0xb8, 0x9a, 0xfd, 0xfe, 0xff, 0xf3, 0xf4, 0xf5,
    0x8f, 0xf6, 0xf7, 0xf2, 0xf1, 0xc6, 0x9e, 0xc7, 0xbe, 0x95, 0x96, 0x97,
    0xc0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xb1, 0xb2, 0xb3, 0x98,
    0xb4, 0xbc, 0xbd, 0xd9, 0xe1, 0xe9, 0x91, 0xbf, 0x94, 0xc3, 0xa2, 0xa3,
    0xa4, 0xa5, 0xa6, 0xa1, 0xa7, 0x9c,
))
CASES = (0, 1, 2, 3, 4, 5, 6, 8, 9, 10)
FLASH_BUSY_CASE = 10


def check_debugger(simulator):
    """Exercise real register/CODE banks, without temporary direct decoders."""
    image = {0: 2, 1: 0x80, 2: 0}
    for bank in range(1, 8):
        raw = bytes((0x74, bank, 0x53, 3, 0xf6, 0xb4, bank, 0,
                     0x75, 0x9f, bank % 7 + 1, 0x74, 0x60 + bank, 0x80, 0xfe))
        image.update({(bank << 16) + 0x8000 + i: value for i, value in enumerate(raw)})
    for bank in range(1, 8):
        commands = ["set option analyzer false"] + banking.model(image)
        commands += [f"set memory sfr 0x9f {bank}", "fill iram 0 0xff 0x69",
                     "set memory iram 3 0xff", f"set memory sfr 0xd0 {(bank % 4) << 3}",
                     "run 0 0x8002", "step 1", "step 1", "step 1", "step 1",
                     marker(10), "state", "dump /h sfr 0x9f 0x9f",
                     "dump /h sfr 0xe0 0xe0", marker(11), marker(12), "dump /h iram 0 0xff",
                     "dump /h xram 0x1f00 0x1fff", marker(13)]
        parts = sections(simulate(simulator, commands))
        part = parts[10]
        check_pc(part, 0x800d)
        expected = bytearray(b"\x69" * 256)
        expected[3] = 0xf6
        require(memory_dump(part, 0x9f, 1) == bytes((bank % 7 + 1,))
                and memory_dump(part, 0xe0, 1) == bytes((0x60 + bank % 7 + 1,))
                and memory_dump(parts[12], 0, 256) == expected
                and memory_dump(parts[12], 0x1f00, 256) == expected,
                "Debugger altered real register/CODE banking or IRAM alias")


def hex_bytes(value, size):
    require(type(value) is str and re.fullmatch(r"[0-9a-f]{%d}" % (2 * size), value),
            "Malformed fixed-width reference bytes")
    return bytes.fromhex(value)


def validate_reference(vector):
    require(set(vector) == {"case", "admission", "draws", "initial", "nv_initial", "steps",
                            "peer_tx", "aes_blocks", "flash_commands"}, "Malformed join reference")
    require(type(vector["case"]) is int and vector["case"] in CASES, "Unknown join scenario")
    require(all(type(vector[name]) is int and vector[name] >= 0
                for name in ("peer_tx", "aes_blocks", "flash_commands")), "Invalid reference counters")
    hex_bytes(vector["admission"], 150)
    hex_bytes(vector["draws"], 64)
    hex_bytes(vector["nv_initial"], 4096)
    require(set(vector["initial"]) == {str(a) for a in PERIPHERAL_SFR | set(range(0x6100, 0x6400))}
            and all(type(v) is int and 0 <= v <= 255 for v in vector["initial"].values()),
            "Initial state escapes synthetic peripheral ownership")
    require(isinstance(vector["steps"], list) and vector["steps"], "Missing caller steps")
    for index, step in enumerate(vector["steps"]):
        require(set(step) == {"packet", "events", "status", "nv", "returned"}
                and isinstance(step["events"], list), "Malformed caller step")
        require(type(step["returned"]) is bool and step["returned"] == (
            vector["case"] != FLASH_BUSY_CASE or index != len(vector["steps"])-1),
            "Nonreturning call outside the final retained-flash boundary")
        hex_bytes(step["packet"], 8)
        hex_bytes(step["status"], 48)
        hex_bytes(step["nv"], 4096)


def reference(output, case):
    command = [str((output / "host-join-smoke-vectors").resolve()), str(case)]
    raw = subprocess.check_output(command, timeout=15)
    command[0] += "-sanitize"
    require(raw == subprocess.check_output(command, timeout=15), "Native/sanitizer join transcript differs")
    vector = json.loads(raw)
    validate_reference(vector)
    require(vector["case"] == case, "Wrong reference scenario")
    return vector


def check_breakpoints(text, commands):
    expected = [int(c.split()[1], 0) for c in commands if c.startswith("break ")]
    actual = [int(a, 16) for a in re.findall(r"^Breakpoint \d+ at 0x([0-9a-fA-F]+):", text, re.M)]
    require(len(expected) == len(set(expected)) and actual == expected,
            "Simulator did not accept the complete unique breakpoint inventory")


def sites_for(artifacts):
    image, symbols, debug, listings, _ = artifacts
    sites = mmio_sites(image, debug, {m: r.decode("ascii") for m, r in listings.items()},
                       handoff=True, reconfigure=True,
                       instruction_records=lambda text: [(pc, raw) for pc, (raw, _) in instructions(text).items()])
    for module in ("aes", "flash", "flash_exec"):
        rows = instructions(listings[module].decode("ascii"))
        code = {pc: raw for pc, (raw, _) in rows.items()}
        for pc, raw, reg in peripheral_accesses(code):
            write = raw[0] in (0x75, 0xf5) or 0x88 <= raw[0] <= 0x8f
            destination = reg if write or raw[0] == 0xb5 else (
                raw[2] if raw[0] == 0x85 else raw[0] - 0xa8 if 0xa8 <= raw[0] <= 0xaf else 0xe0)
            sites[pc] = ("w" if write else "r", reg, destination)
        if module in ("flash", "flash_exec"):
            for pc, raw in code.items():
                if raw in (b"\xe0", b"\xf0"):
                    sites.setdefault(pc, ("r" if raw == b"\xe0" else "w", None,
                                          0xe0 if raw == b"\xe0" else None))
    for name in ("arm_input", "arm_output"):
        pc = cdb_address(debug, "L:Faes$" + name + "$0$0")
        require(bytes(image[pc+i] for i in range(3, 13)) == bytes(9) + b"\x22",
                "Changed AES arm settle instructions")
        sites[pc+3] = ("c", 0, None)
    template = cdb_address(debug, "L:Fflash_exec$flash_exec_template$0$0")
    ram = symbols["_flash_exec_ram"] + 0x8000
    for pc, site in tuple(sites.items()):
        if template <= pc < template + 123:
            del sites[pc]
            sites[ram + pc - template] = site
    return sites


def stops_for(sites, symbols, ram_stop=None):
    conditions = {}
    for pc, (kind, address, _) in sites.items():
        if kind == "r" and address in PASSIVE:
            continue
        clauses = []
        if pc >= 0x10000:
            clauses.append(f"sfr[0x9f]=={pc >> 16}")
            clauses.append("(sfr[0xc7]&8)==0")
        elif pc >= 0x8000:
            clauses.append("(sfr[0xc7]&8)!=0")
        if kind != "c" and address is None:
            clauses.append("((sfr[0x83]*256+sfr[0x82])>=0x6000)")
            clauses.append("((sfr[0x83]*256+sfr[0x82])<0xe800)")
            if kind == "r":
                clauses.append("((sfr[0x83]*256+sfr[0x82])!=0x6270||xram[0x6270]!=4)")
                clauses.append("((sfr[0x83]*256+sfr[0x82])!=0x624a)")
                clauses.append("((sfr[0x83]*256+sfr[0x82])!=0x6276)")
                clauses.append("((sfr[0x83]*256+sfr[0x82])!=0x6277)")
        if kind == "r" and address in CONDITION:
            clauses.append(CONDITION[address])
        conditions.setdefault(pc & 0xffff, []).append("&&".join(f"({c})" for c in clauses) or "1")
    if ram_stop is not None:
        require(0x8000 <= ram_stop < 0x9e00, "Flash stop outside ordinary XMAP RAM")
        conditions.setdefault(ram_stop, []).append("(sfr[0xc7]&8)!=0")
    result = [f'break {pc:#x} 1 if "' + "||".join(f"({c})" for c in clauses) + '"'
              for pc, clauses in conditions.items()]
    result += [f"break {symbols[n]:#x}" for n in
               ("_join_smoke_wait", "_join_smoke_ready", "_join_smoke_fault", "_banked_stop")]
    return result


def flash_stop(image, symbols, debug):
    template = cdb_address(debug, "L:Fflash_exec$flash_exec_template$0$0")
    raw = bytes(image[template+i] for i in range(123))
    require(raw.count(b"\x80\xfe") == 1, "Missing unique retained flash loop")
    return symbols["_flash_exec_ram"] + 0x8000 + raw.index(b"\x80\xfe"), raw


def check_flash_stop(state, symbols, template, *, discovery=False):
    ram, iram, sfr, extended, _ = state
    work, code = (symbols["_flash_exec_" + name] for name in ("work", "ram"))
    require(ram[work:work+9] == b"\x05\0\0\0\0\xd0\x07\x07\x85"
            and ram[code:code+123] == template and sfr[0x47] == 0x0a
            and extended[0x6270-0x2000] == 0x85,
            "Flash failure did not retain the actual pending erase/RAM engine")
    require(0 < iram[symbols["_banked_depth"]] <= 8 and not iram[symbols["_banked_fault"]]
            and ram[symbols["_flash_write_status"]] == 10
            and ram[symbols["_nv_record_diagnostic"]+4] == 13,
            "Nested banker/writer/journal ownership escaped the retained flash call")
    require(type(discovery) is bool, "Unknown retained provisioning profile")
    expected = b"\x04\0\x04\x01\x01\0" if discovery else b"\x04\0\x01\x01\0\0"
    require(ram[0x1e06:0x1e0c] == expected,
            "Retained provisioning published a return, FAULT, binding or READY")


def check_flash_retention(before, after, *, idle=False):
    expected = list(before)
    if idle:
        extended = bytearray(expected[3])
        extended[0x6270-0x2000] = 4
        expected[3] = bytes(extended)
    require(tuple(expected) == after,
            "Retained RAM loop changed owners/CPU/media or returned after later idle")


def events_for(vector):
    """Omit stimulus-free reads, never CPU instructions or controller work."""
    hardware = {int(a): v for a, v in vector["initial"].items()} | CONSTANT | {0x6270: 4}
    media = bytearray(hex_bytes(vector["nv_initial"], 4096))
    for step in vector["steps"]:
        selected = []
        for event in step["events"]:
            require(isinstance(event, list) and len(event) == 6, "Malformed peripheral event")
            kind, address, value, effects, dma, flash = event
            require(kind in ("r", "w", "c") and type(address) is int and type(value) is int
                    and 0 <= value <= 255, "Invalid MMIO scalar")
            require(address in PERIPHERAL_SFR or 0x6000 <= address < 0x6400
                    or kind == "r" and 0xe800 <= address < 0xf800
                    or kind == "c" and address == 0 and value in (4, 9),
                    f"Event escapes peripheral/media ownership: {kind} {address:#x}")
            passive = kind == "r" and (
                address in PASSIVE
                or address == 0xa8 and not hardware.get(0xd6, 0) & 1
                or address == 0x9e and hardware[0x9e] == hardware[0xc6]
                or address == 0xd6 and hardware.get(0xd6, 0) == 0
                or address == 0x6270 and hardware[0x6270] == 4 and not flash
                or 0xe800 <= address < 0xf800)
            if passive:
                expected = media[address-0xe800] if address >= 0xe800 else hardware[address]
                require(value == expected and all(a in (0x91, 0xa1) for a, _ in effects)
                        and dma is None and flash is None,
                        f"Passive read needs external stimulus: {address:x} {value}/{expected}, {effects}, {dma}, {flash}")
            else:
                selected.append(event)
            if kind != "c":
                hardware[address] = value
            for a, v in effects:
                require(a in PERIPHERAL_SFR - {0xc7}
                        and type(v) is int and 0 <= v <= 255, "Effect escapes peripheral SFRs")
                hardware[a] = v
            if flash:
                word_address = hardware[0x6271] | hardware[0x6272] << 8
                offset = (word_address - 0xfa00) * 4
                require(0 <= offset <= 4092 and flash[0] in (1, 2), "Flash outside the two NV pages")
                if flash[0] == 1:
                    require(offset % 2048 == 0, "Misaligned modeled erase")
                    media[offset:offset+2048] = b"\xff" * 2048
                else:
                    word = bytes.fromhex(flash[1])
                    require(len(word) == 4 and media[offset:offset+4] == b"\xff" * 4,
                            "Modeled word lacks fresh erased storage")
                    media[offset:offset+4] = word
        require(bytes(media) == bytes.fromhex(step["nv"]), "Peripheral reference NV differs")
        yield step | {"events": selected}


def resume(state, pc):
    ram, iram, sfr, extended, media = state
    require(0 <= pc <= 0xffff and not sfr[0x50] & 0x18
            and all(sfr[a-128] == 0 for a in (0xa8, 0xb8, 0x9a)),
            "Continuation changed foreground register/IRQ ownership")
    commands = store("flash", 0x3e800, media[0x3e800:0x3f800])
    for space, address, data in (("xram", 0, ram), ("iram", 0, iram),
                                ("xram", 0x2000, extended), ("sfr", 0x80, sfr[:0x19]),
                                ("sfr", 0x9a, sfr[0x1a:])):
        commands += store(space, address, data)
    commands += banking.xmap() if sfr[0x47] & 8 else banking.code_banks()
    return commands + [f"pc {pc:#x}"]


def replay(simulator, artifacts, vector, directory, *, limit=None, chunk=256):
    image, symbols, debug, listings, _ = artifacts
    sites = sites_for(artifacts)
    ram_stop, template_bytes = flash_stop(image, symbols, debug)
    stops = stops_for(sites, symbols, ram_stop)
    base = ["set option analyzer false"] + model(image, directory) + [AES_ALIAS]
    wait, ready, fault = (symbols["_join_smoke_" + n] for n in ("wait", "ready", "fault"))
    rows = instructions(listings["join_smoke_main"].decode("ascii"))
    after = [pc + len(raw) for pc, (raw, _) in rows.items()
             if raw == b"\x12" + symbols["_join_smoke_poll"].to_bytes(2, "big")]
    require(len(after) == 1, "Missing actual main poll continuation")
    after = after[0]
    stops.append(f"break {after:#x}")
    initial = {int(a): v for a, v in vector["initial"].items()} | CONSTANT | {0x6270: 4}
    require(all(type(v) is int and 0 <= v <= 255 for v in initial.values()), "Invalid initial peripherals")
    commands = base + ["fill iram 0 0xff 0xa5", "fill xram 0 0x1eff 0xa5",
                       "fill xram 0x2000 0x7fff 0x69"]
    commands += [f"set memory {'sfr' if a < 256 else 'xram'} {a:#x} {v:#x}"
                 for a, v in initial.items()]
    commands += store("flash", 0x3e800, hex_bytes(vector["nv_initial"], 4096))
    commands += [f"run 0 {symbols['_main']:#x}", marker(59990), "state", marker(59991),
                 "fill iram 0x7d 0xff 0xc7",
                 f"run {symbols['_main']:#x} {wait:#x}"] + capture(60000)
    parts = sections(simulate_binary_dumps(simulator, commands))
    check_pc(parts[59990], symbols["_main"])
    state = captured_sections(parts, 60000, wait)
    require(state[0][0x1e00:0x1e08] == b"JSN1\x01\x30\x01\0"
            and state[0][symbols["_join_smoke_mailbox"]:symbols["_join_smoke_mailbox"]+8] == bytes(8),
            "Genuine reset did not produce DISARMED")
    pc, count, peak = wait, 0, 0x4f
    physical = state[4]
    writes, programming, faddr = [], None, [0, 0]
    completed = 0
    for index, step in enumerate(events_for(vector)):
        if limit is not None and index == limit:
            break
        events = step["events"]
        # NV verification executes long real loops between mapping writes.
        batch = min(chunk, 128) if any(e[:2] == ["w", 0xc7] for e in events) else chunk
        for first in range(0, max(1, len(events)), batch):
            last = min(first+batch, len(events))
            commands = base + resume(state, pc) + stops
            commands += [f"set memory sfr 0x9f {state[2][0x1f]}"]
            commands += banking.xmap() if state[2][0x47] & 8 else banking.code_banks()
            commands += capture(60000)
            if first == 0:
                if pc == after and state[0][0x1e06] != 4:
                    commands += ["step 1", "run", marker(59980), "state", marker(59981)]
                if index == 0:
                    commands += store("xram", symbols["_join_smoke_phase"], bytes.fromhex(vector["admission"]))
                    commands += store("xram", symbols["_join_smoke_draws"], bytes.fromhex(vector["draws"]))
                require(pc in (wait, after), "Input outside public foreground boundary")
                packet = bytes.fromhex(step["packet"])
                require(len(packet) == 8 and (state[0][0x1e06] != 4 or packet == bytes(8)),
                        "Invalid mailbox handoff")
                commands += store("xram", symbols["_join_smoke_mailbox"], packet)
                commands.append("step 1")
            checks, number = [], 10
            mapping = state[2][0x47]
            previous = tuple(events[first-1][:3]) if first else None
            for kind, address, value, effects, dma, flash in events[first:last]:
                adjacent = previous is not None and (
                    previous[0] == kind == "w" and 0xa2 <= previous[1] < 0xa6 and address == previous[1]+1
                    or previous[0] == kind == "w" and (previous[1], address) in ((0xd5, 0xd4), (0xd3, 0xd2))
                    or previous[:2] == ("w", 0xe9) and kind == "r" and address == 0x6193
                    or previous[:2] == ("w", 0xd6) and kind == "c"
                    or previous[:2] == ("w", 0x6270) and kind == "r" and address == 0x6270)
                if not adjacent:
                    commands.append("run")
                commands += [marker(number), "state", "dump /h sfr 0x81 0x83",
                             "dump /h sfr 0x9f 0x9f", "dump /h sfr 0xc7 0xc7", marker(number+1)]
                if kind == "w" and address == 0x6270:
                    ram = symbols["_flash_exec_ram"]
                    commands.append(f"dump /h xram {ram:#x} {ram+122:#x}")
                if dma:
                    phase, source, output = dma
                    require(phase in (1, 2, 3), "Unknown AES DMA phase")
                    origin = symbols[("_aes_key", "_aes_iv", "_aes_input")[phase-1]]
                    d0 = origin.to_bytes(2, "big") + bytes.fromhex("70b100101d41")
                    d1 = b"\x70\xb2" + symbols["_aes_output"].to_bytes(2, "big") + bytes.fromhex("00101e11") + bytes(24)
                    for name, expected in (("_aes_dma0", d0), ("_aes_dma1", d1),
                                           (("_aes_key", "_aes_iv", "_aes_input")[phase-1], bytes.fromhex(source))):
                        a = symbols[name]
                        commands += [f"dump /h xram {a:#x} {a+len(expected)-1:#x}"]
                    if phase == 3:
                        commands += store("xram", symbols["_aes_output"], bytes.fromhex(output))
                commands.append(marker(number+2))
                space = "sfr" if address < 256 else "xram"
                if kind == "r":
                    commands.append(f"set memory {space} {address:#x} {value:#x}")
                commands += [f"step {value if kind == 'c' else 1}", marker(number+3), "state"]
                if kind == "r":
                    for dest in sorted({d for k, a, d in sites.values() if k == "r" and a in (None, address)}):
                        commands.append(f"dump /h {'iram' if dest < 128 else 'sfr'} {dest:#x} {dest:#x}")
                elif kind == "w":
                    commands.append(f"dump /h {space} {address:#x} {address:#x}")
                commands.append(marker(number+4))
                commands += [f"set memory sfr {a:#x} {v:#x}" for a, v in effects]
                if kind == "w" and address == 0xc7:
                    commands += banking.xmap() if value & 8 else banking.code_banks()
                    mapping = value
                if kind == "w" and address == 0x6270:
                    programming = value & 3
                    writes = []
                if kind == "w" and address == 0x6273:
                    writes.append(value)
                if kind == "w" and address in (0x6271, 0x6272):
                    faddr[address-0x6271] = value
                if flash:
                    require(programming == flash[0] and (programming == 1 or bytes(writes) == bytes.fromhex(flash[1])),
                            "Controller completion without matching actual write events")
                    address_nv = (faddr[0] | faddr[1] << 8) * 4
                    require(0x3e800 <= address_nv <= 0x3f7fc, "Controller escaped NV pages")
                    if programming == 1:
                        require(address_nv % 2048 == 0, "Controller erase misalignment")
                        commands.append(f"fill flash {address_nv:#x} {address_nv+2047:#x} 0xff")
                    else:
                        commands += store("flash", address_nv, bytes.fromhex(flash[1]))
                    programming = None
                checks.append((number, kind, address, value, dma, flash))
                previous = (kind, address, value)
                number += 5
            if last == len(events):
                commands.append("run")
            commands += capture(60010)
            try:
                text = simulate_binary_dumps(simulator, commands)
            except subprocess.CalledProcessError as error:
                print((error.stdout or "")[-2500:], file=sys.stderr)
                raise
            except subprocess.TimeoutExpired as error:
                output = error.stdout or b""
                print(output[-3500:].decode("ascii", errors="replace") if isinstance(output, bytes)
                      else output[-3500:], file=sys.stderr)
                raise
            check_breakpoints(text, stops)
            parts = sections(text)
            require(captured_sections(parts, 60000, pc) == state, "Continuation changed actual machine state")
            if first == 0 and pc == after and state[0][0x1e06] != 4:
                check_pc(parts[59980], wait)
            for n, kind, address, value, dma, flash in checks:
                here = pc_in(parts[n])
                bank = memory_dump(parts[n], 0x9f, 1)[0]
                mapping = memory_dump(parts[n], 0xc7, 1)[0]
                logical = here | bank << 16 if here >= 0x8000 and not mapping & 8 else here
                require(logical in sites, f"Unexpected MMIO stop step{index} event{count}: {logical:x}")
                k, a, dest = sites[logical]
                if k != kind or a not in (None, address):
                    print(parts[n], file=sys.stderr)
                require(k == kind and a in (None, address),
                        f"MMIO identity step{index} event{count} at {logical:x}: {sites[logical]} != {(kind,address)}")
                regs = memory_dump(parts[n], 0x81, 3)
                require(regs[0] <= 0x7c, "Sampled stack overflow")
                if kind == "w" and address == 0x6270:
                    ram = symbols["_flash_exec_ram"]
                    require(mapping & 8 and ram+0x8000 <= here < ram+0x807b
                            and memory_dump(parts[n+1], ram, 123) == template_bytes,
                            "Flash command did not execute the genuine copied RAM template")
                if dma:
                    phase, source, output = dma
                    origin = symbols[("_aes_key", "_aes_iv", "_aes_input")[phase-1]]
                    d0 = origin.to_bytes(2, "big") + bytes.fromhex("70b100101d41")
                    d1 = b"\x70\xb2" + symbols["_aes_output"].to_bytes(2, "big") + bytes.fromhex("00101e11") + bytes(24)
                    for name, expected in (("_aes_dma0", d0), ("_aes_dma1", d1),
                                           (("_aes_key", "_aes_iv", "_aes_input")[phase-1], bytes.fromhex(source))):
                        require(memory_dump(parts[n+1], symbols[name], len(expected)) == expected,
                                "Actual DMA descriptor/input differs from independent AES model")
                if kind == "c":
                    check_pc(parts[n+3], here+value)
                else:
                    require(address < 256 or regs[1:] == address.to_bytes(2, "little"), "Actual MMIO DPTR differs")
                    actual = memory_dump(parts[n+3], address if kind == "w" else dest, 1)[0]
                    require(actual == value, f"MMIO value at {logical:x}: {actual} != {value}")
                count += 1
            pc = pc_in(parts[60010])
            state = captured_sections(parts, 60010, pc)
            peak = max(peak, stack_high_water(parts[60010]))
            require(peak <= 0x7c and state[1][0x7d:] == b"\xc7"*131, "Actual stack/IRAM alias overflow")
            require(state[0][symbols["l_XSEG"]:0x1e00] == b"\xa5"*(0x1e00-symbols["l_XSEG"])
                    and state[0][0x1e30:] == b"\xa5"*(0x1f00-0x1e30), "Unowned XDATA changed")
            require(state[4][:0x3e800] == physical[:0x3e800] and state[4][0x3f800:] == physical[0x3f800:],
                    "Firmware/lock flash changed")
            if last == len(events):
                expected_pc = after if step["returned"] else ram_stop
                require(pc == expected_pc,
                        f"Wrong caller return/RAM-stop boundary: step{index}, PC {pc:x}")
                expected = bytes.fromhex(step["status"])
                require(state[0][0x1e00:0x1e30] == expected,
                        f"Status step{index}: {state[0][0x1e00:0x1e30].hex()} != {expected.hex()}")
                require(state[4][0x3e800:0x3f800] == bytes.fromhex(step["nv"]), "Actual journal differs")
                completed += 1
            print(f"join step{index} events {last}/{len(events)} SP{peak:02x}", flush=True)
    finished = completed == len(vector["steps"])
    terminal_kind = "ready" if vector["case"] == 0 else (
        "flash-ram-stop" if vector["case"] == FLASH_BUSY_CASE else "fault")
    if finished and vector["case"] == FLASH_BUSY_CASE:
        discovery = "_security_keys_provision_default" in symbols
        require(programming == 1 and vector["flash_commands"] == 1 and
                vector["peer_tx"] == (3 if discovery else 0)
                and state[4][0x3e800:0x3f800] == bytes.fromhex(vector["nv_initial"]),
                "Retained erase completed, changed media or differed from its commissioning boundary")
        check_flash_stop(state, symbols, template_bytes, discovery=discovery)
        commands = base + resume(state, pc) + ["step 16"] + capture(60000)
        commands += ["set memory xram 0x6270 4", "step 16"] + capture(60010)
        parts = sections(simulate_binary_dumps(simulator, commands))
        check_flash_retention(state, captured_sections(parts, 60000, ram_stop))
        check_flash_retention(state, captured_sections(parts, 60010, ram_stop), idle=True)
    elif finished:
        terminal = ready if vector["case"] == 0 else fault
        require(state[0][0x1e06] == (5 if vector["case"] == 0 else 6), "Wrong terminal state")
        commands = base + resume(state, pc) + stops + ["step 1", "run"] + capture(60000)
        commands += ["step 16"] + capture(60010)
        parts = sections(simulate_binary_dumps(simulator, commands))
        retained = captured_sections(parts, 60000, terminal)
        require(captured_sections(parts, 60010, terminal) == retained,
                "Terminal loop changed retained ownership or memory")
        require(retained[0] == state[0] and retained[4] == state[4], "Terminal transition changed owners/media")
    return {"complete": finished and limit is None, "steps": completed, "peripheral_stops": count,
            "peak_sp": peak, "phase": state[0][0x1e06], "terminal": terminal_kind if finished else None}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--board", choices=("generic", "lg_esl29_rev03"), required=True)
    parser.add_argument("--key-mode", choices=("install-code", "default-tc"), default="install-code")
    parser.add_argument("--simulator", default="s51")
    parser.add_argument("--case", type=int, choices=CASES, default=0)
    parser.add_argument("--limit", type=int)
    parser.add_argument("--chunk", type=int, default=256)
    args = parser.parse_args()
    require(0 < args.chunk <= 256 and (args.limit is None or args.limit > 0), "Invalid replay bound")
    report = args.output / f"join-replay-{args.case}.json"
    report.unlink(missing_ok=True)
    artifacts, _ = verify(args.output / "join-smoke-layout", args.board, args.key_mode)
    check_alias(args.simulator)
    banking.check_mapping(args.simulator)
    check_debugger(args.simulator)
    vector = reference(args.output, args.case)
    with tempfile.TemporaryDirectory(prefix="join-smoke-") as directory:
        result = replay(args.simulator, artifacts, vector, Path(directory), limit=args.limit, chunk=args.chunk)
    if result["complete"]:
        result.update(identities=identities(args.output / "join-smoke-layout"), board=args.board,
                      key_mode=args.key_mode, case=args.case, simulated=True, hardware_observed=False)
        report.write_text(json.dumps(result, indent=2) + "\n", encoding="ascii")
    print(f"{'Complete' if result['complete'] else 'Partial diagnostic'} case{args.case}: "
          f"{result['peripheral_stops']} peripheral stops, SP{result['peak_sp']:02X}/7C; synthetic only.")


if __name__ == "__main__":
    main()
