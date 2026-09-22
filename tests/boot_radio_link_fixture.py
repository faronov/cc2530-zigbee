#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Execute the genuine same-owner board fixture with synthetic peripherals."""
import argparse
import json
from pathlib import Path
import re
import subprocess

from boot_image import (
    boot_commands, check_alias, check_guards, check_pc, expected_status, marker,
    memory_dump, simulate, snapshot_commands,
)
from boot_radio_tx_fixture import sections, snap, stack_high_water
from radio_link_fixture import (
    BODY, CASES, CLOCK_SIZES, HASHES, RADIO_SIZES, SETTINGS, SIZE, check_end,
    decode, decode_frames, load_image, modules, verify_fixture, verify_listings,
)
from verify_firmware import parse_ihex, parse_symbols, require


def rejected(function):
    try:
        function()
    except ValueError:
        return
    raise ValueError("Link fixture negative control accepted")


def run_vector(simulator, path, board, symbols, proof, vector):
    wait, end, fault = proof["checkpoints"]
    sites = proof["sites"]
    current = {int(a): v for a, v in vector["initial"].items()}
    commands = boot_commands(symbols) + ["fill xram 0x6000 0x63ff 0x69", "fill iram 0x7d 0xff 0xc7"]
    commands += [f"set memory {'sfr' if a < 256 else 'xram'} {a:#x} {v:#x}" for a, v in current.items()]
    commands += [f"fill xram {proof['mailbox']:#x} {proof['mailbox']+7:#x} 0xa9",
                 f"run {symbols['_main']:#x} {wait:#x}"] + snapshot_commands(1)
    commands += [f"break {pc:#x}" for pc in list(sites) + [wait, end, fault]]
    checks, number = [], 10
    for step in vector["steps"]:
        commands.append(f"set memory xram {proof['mailbox']:#x} " +
                        " ".join(hex(v) for v in bytes.fromhex(step["packet"])))
        require(step["repeat"] == 1 or not step["events"], "Repeated link admission acquired MMIO")
        for _ in range(step["repeat"]-1):
            commands += ["step 1", "run"]
        commands.append("step 1")
        first = number
        for kind, address, value in step["events"]:
            require(kind in ("r", "w", "c") and not
                    (kind == "w" and address == 0xe1 and value not in (0xee, 0xea)),
                    "Link unapproved strobe/flush/unconditional TX/ACK")
            space = "sfr" if address < 256 else "xram"
            commands += ["run", marker(number), "state", "dump /h sfr 0x81 0x83"]
            if kind == "r":
                commands.append(f"set memory {space} {address:#x} {value:#x}")
            commands += [marker(number+1), "step 4" if kind == "c" else "step 1"]
            if kind == "c":
                require(address == 0 and value == 4, "Link CCA settling changed")
                commands.append("state")
            elif kind == "r":
                destinations = {dest for k, a, dest in sites.values() if k == "r" and a in (None, address)}
                for dest in destinations:
                    commands.append(f"dump /h {'iram' if dest < 128 else 'sfr'} {dest:#x} {dest:#x}")
            else:
                commands.append(f"dump /h {space} {address:#x} {address:#x}")
            commands.append(marker(number+2))
            if kind != "c":
                current[address] = value
            number += 3
        commands += ["run"] + snapshot_commands(number)
        commands += [marker(number+4), "dump /h xram 0x6000 0x63ff", marker(number+5)]
        checks.append((step, first, number, dict(current))); number += 6
    require(number < 65530, "Link fixture transcript exceeds bounded marker space")
    commands += ["step 64"] + snapshot_commands(number)
    text = simulate(simulator, commands, path); parts = sections(text)
    ram, iram, sfr = snap(parts, 1)
    require(ram[0x1e00:0x1e20] == expected_status(board) and
            decode(ram[proof["state"]:proof["state"]+SIZE])["phase"] == 1 and
            ram[proof["mailbox"]:proof["mailbox"]+8] == bytes(8),
            "Link actual boot retained authorization or changed startup")
    check_guards(ram, iram[128:], sfr, symbols)
    sampled, last = 0, None
    for step, first, final, current in checks:
        record = decode(bytes.fromhex(step["state"]))
        if record["phase"] <= 3:
            require(not step["events"], "Link admission performed hardware work")
        require(sum(k == "w" and a == 0xe1 and v == 0xea for k, a, v in step["events"]) <= 1,
                "Link repeated the ordinary transmit attempt")
        for i, (kind, address, value) in enumerate(step["events"]):
            n = first + 3*i
            match = re.search(r"CPU state= OK PC= 0x([0-9a-fA-F]+)", parts[n])
            require(match is not None and int(match[1], 16) in sites, "Link unplanned peripheral stop")
            pc = int(match[1], 16); actual_kind, actual_address, observed = sites[pc]
            require(kind == actual_kind and actual_address in (None, address),
                    f"Link MMIO order/operand changed at {pc:04x}")
            registers = memory_dump(parts[n], 0x81, 3); sampled = max(sampled, registers[0])
            if kind == "c":
                check_pc(parts[n+1], pc+4)
                continue
            if address >= 256:
                require(registers[1:] == address.to_bytes(2, "little"), "Link actual MOVX DPTR changed")
                if actual_address is None:
                    require(address in SETTINGS or 0x616a <= address <= 0x6175,
                            "Link indexed MMIO escaped profile/address RAM")
            observed = address if observed is None else observed
            require(memory_dump(parts[n+1], observed, 1)[0] == value, "Link genuine MMIO value changed")
        check_pc(parts[final], end if record["phase"] == 5 else fault if record["phase"] == 6 else wait)
        ram, iram, sfr = last = snap(parts, final)
        require(ram[proof["state"]:proof["state"]+SIZE] == bytes.fromhex(step["state"]),
                f"Link state/native mismatch in case{vector['case']}")
        diagnostics = []
        for name, sizes in (("clock", CLOCK_SIZES), ("radio", RADIO_SIZES)):
            require(len(step[name]) == len(sizes), "Link diagnostic serializer extent changed")
            raw = b"".join(value.to_bytes(size, "little") for value, size in zip(step[name], sizes))
            require(ram[proof[name]:proof[name]+len(raw)] == raw, "Link diagnostic/native mismatch")
            diagnostics.append(raw)
        check_end(record, *diagnostics)
        raw = ram[proof["frames"]:proof["frames"]+256]
        require(raw == bytes.fromhex(step["frames"]), "Link complete received storage/native mismatch")
        decode_frames(record, raw)
        require(ram[0x1e00:0x1e20] == bytes.fromhex(step["boot"]) and
                ram[proof["mailbox"]:proof["mailbox"]+8] == bytes(8),
                "Link M0/heartbeat/mailbox changed")
        require(ram[proof["config"]:proof["config"]+14] == bytes.fromhex("1011121314151617341278561a05"),
                "Link immutable synthetic receive profile changed")
        require(ram[proof["body"]:proof["body"]+13] == BODY, "Link immutable transmit body changed")
        check_guards(ram, iram[128:], sfr, symbols)
        require(iram[0x7d:] == b"\xc7"*131 and sfr[1] == symbols["s_SSEG"]+1 and sampled <= 0x7c,
                "Link SP7C/alias guard or unwind changed")
        for address, value in current.items():
            if address < 256:
                require(sfr[address-128] == value, "Link guarded SFR/GPIO/clock changed")
        hardware = memory_dump(parts[final+4], 0x6000, 1024)
        require(all(v == current.get(a, 0x69) for a, v in enumerate(hardware, 0x6000)),
                "Link unplanned radio/FIFO RAM mutation")
    require(last == snap(parts, number), "Link terminal loop changed CPU/RAM")
    full = stack_high_water(parts[number])
    require(sampled <= full <= 0x7c, "Link whole-run stack cap changed")
    return sampled, full


def rejections(image, symbols, debug, board, listings):
    count = 0

    def reject(**changes):
        nonlocal count
        args = dict(image=image, symbols=symbols, debug=debug, board=board)
        rejected(lambda: verify_fixture(**(args | changes)))
        count += 1

    for address in image:
        reject(image=image | {address: image[address] ^ 1})
    reject(image=image | {len(image): 0})
    reject(image={a: v for a, v in image.items() if a != len(image)-1})
    for name in symbols:
        reject(symbols=symbols | {name: symbols[name] ^ 1})
    for line in debug.splitlines():
        if line.startswith(("F:", "S:", "L:", "T:")):
            reject(debug=debug.replace(line+"\n", "", 1))
            reject(debug=debug+line+"\n")
            reject(debug=debug.replace(line, line+"!", 1))
    for changed in (debug.replace("\n", "\r\n"), debug+"\n", debug+"\0"):
        reject(debug=changed)
    for module in modules(board):
        rejected(lambda: verify_listings(listings | {module: ""}, image, symbols, board))
        count += 1
    return count


def check_radio_link_fixture(simulator, output, board, symbols):
    output = Path(output); fixture = load_image(output, board)
    path = output / "radio_link_fixture.ihx"
    image = parse_ihex(path.read_text()); debug = path.with_suffix(".cdb").read_bytes().decode("ascii")
    require(symbols == parse_symbols(path.with_suffix(".map").read_text()), "Link noncanonical map")
    listings = {m: (output / f"radio_link_fixture.{m}.rst").read_text() for m in modules(board)}
    count = rejections(image, symbols, debug, board, listings)
    check_alias(simulator); rejected(lambda: check_alias(simulator, False))
    proof = fixture.radio_link_proof
    sampled = full = calls = events = 0
    for n in range(CASES):
        vector = json.loads(subprocess.check_output(
            [str(output / f"host-radio-link-fixture-tests_{board}"), "--vector", str(n),
             str(proof["config"]), str(proof["frames"]), str(proof["body"]),
             str(proof["reserved"]), str(proof["helper"])], text=True, timeout=15))
        require(vector["case"] == n, "Link missing native fixture scenario")
        a, b = run_vector(simulator, path, board, symbols, proof, vector)
        sampled = max(sampled, a); full = max(full, b)
        calls += sum(step["repeat"] for step in vector["steps"])
        events += sum(len(step["events"]) for step in vector["steps"])
    require(sampled == 0x31 and full == 0x37, "Link reviewed sampled/full-run stack high-water changed")
    print(f"Link fixture: {CASES} sequences/{calls} genuine polls/{events} MMIO/settle events; "
          f"{HASHES[board][0]} CODE,750+64/1024 XDATA; sampled SP={sampled:02X}, full SP={full:02X}; "
          f"{count}+1 artifact/alias negatives PASS. Synthetic peripherals, not hardware.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--board", choices=tuple(HASHES), required=True)
    parser.add_argument("--simulator", default="s51")
    args = parser.parse_args()
    symbols = parse_symbols((args.output / "radio_link_fixture.map").read_text())
    check_radio_link_fixture(args.simulator, args.output, args.board, symbols)
