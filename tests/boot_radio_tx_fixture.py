#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Genuine board instructions with original synthetic peripherals; NEVER hardware."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import unittest

from boot_image import (boot_commands, check_alias, check_guards, check_pc, expected_status,
                        marker, memory_dump, simulate, snapshot_commands)
from radio_tx_fixture import (
    CLOCK_SIZES, FIFO_SIZES, TX_SIZES, HASHES, SIZE, SETTINGS,
    check_end, decode, load_image, private_records, public_records,
    verify_fixture, verify_code, verify_listings,
)
from verify_firmware import parse_ihex, parse_symbols, require


def sections(text):
    split = re.split(r"^0x2530([0-9a-f]{4})\r?\n", text, flags=re.M)
    ids = [int(split[i], 16) for i in range(1, len(split), 2)]
    require(len(ids) == len(set(ids)), "Duplicate TX fixture transcript marker")
    return dict(zip(ids, split[2::2]))


def snap(parts, n):
    return (memory_dump(parts[n], 0, 0x1f00), memory_dump(parts[n+1], 0, 256),
            memory_dump(parts[n+2], 0x80, 128))


def stack_high_water(text):
    """Read the cumulative simulator statistic, not an MMIO-stop SP sample."""
    records = re.findall(r"^Max value of stack pointer=([^\n]*)$", text, re.M)
    require(len(records) == 1, "TX missing/duplicate full-run stack high-water")
    match = re.fullmatch(r"[ \t]*0x([0-9a-fA-F]+), avg=[ \t]*0x[0-9a-fA-F]+[ \t]*\r?", records[0])
    require(match is not None, "TX malformed full-run stack high-water")
    peak = int(match[1], 16)
    require(peak < 0x80, "TX full-run stack reached upper IRAM")
    return peak


def run_vector(simulator, path, board, symbols, proof, vector):
    wait, end, fault = proof["checkpoints"]
    sites = proof["sites"]
    current = {int(a): v for a, v in vector["initial"].items()}
    commands = boot_commands(symbols)+["fill xram 0x6000 0x63ff 0x69"]
    commands += [f"set memory {'sfr' if a < 256 else 'xram'} {a:#x} {v:#x}" for a, v in current.items()]
    commands += [f"fill xram {proof['mailbox']:#x} {proof['mailbox']+7:#x} 0xa6",
                 f"run {symbols['_main']:#x} {wait:#x}"]+snapshot_commands(1)
    commands += [f"break {pc:#x}" for pc in list(sites)+[wait, end, fault]]
    records = []; number = 10
    for step in vector["steps"]:
        raw = bytes.fromhex(step["packet"])
        commands += [f"set memory xram {proof['mailbox']:#x} "+" ".join(hex(v) for v in raw)]
        require(step["repeat"] == 1 or not step["events"], "Repeated TX admission acquired MMIO")
        for _ in range(step["repeat"]-1):
            commands += ["step 1", "run"]
        commands += ["step 1"]
        first = number
        for kind, address, value in step["events"]:
            space = "sfr" if address < 256 else "xram"
            commands += ["run", marker(number), "state", "dump /h sfr 0x81 0x83"]
            if kind == "r": commands.append(f"set memory {space} {address:#x} {value:#x}")
            commands += [marker(number+1), "step 1"]
            if kind == "r": commands += ["dump /h sfr 0xe0 0xe0", "dump /h iram 0 7"]
            commands += [f"dump /h {space} {address:#x} {address:#x}", marker(number+2)]
            current[address] = value; number += 3
        commands += ["run"]+snapshot_commands(number)
        commands += [marker(number+4), "dump /h xram 0x6000 0x63ff", marker(number+5)]
        records.append((step, first, number, dict(current))); number += 6
    require(number < 65530, "TX fixture marker budget exceeded")
    commands += ["step 64"]+snapshot_commands(number)
    text = simulate(simulator, commands, path); parts = sections(text)
    boot, iram, sfr = snap(parts, 1)
    require(boot[0x1e00:0x1e20] == expected_status(board) and
            decode(boot[proof["state"]:proof["state"]+SIZE])["phase"] == 1 and
            boot[proof["mailbox"]:proof["mailbox"]+8] == bytes(8),
            "TX actual boot retained authorization or changed board startup")
    check_guards(boot, iram[128:], sfr, symbols)
    peak = 0; last = None
    for step, first, final, current in records:
        writes = [(a, v) for k, a, v in step["events"] if k == "w"]
        record = decode(bytes.fromhex(step["state"]))
        if record["phase"] in (1, 2, 3):
            require(not step["events"], "TX admission did hardware work")
        require(sum(a == 0xe1 and v == 0xea for a, v in writes) <= 1 and
                not any(a == 0xe1 and v in (0xe9, 0xeb, 0xef) for a, v in writes),
                "TX fixture repeated/fell back/issued unapproved RF command")
        for i, (kind, address, value) in enumerate(step["events"]):
            n = first+3*i
            match = re.search(r"CPU state= OK PC= 0x([0-9a-fA-F]+)", parts[n])
            require(match is not None and int(match[1], 16) in sites,
                    vector["name"]+": unexpected MMIO stop")
            pc = int(match[1], 16); actual_kind, actual_address, observed = sites[pc]
            require(kind == actual_kind and actual_address in (None, address),
                    f"TX MMIO order mismatch at {pc:04x}: wanted {kind}:{address:04x}, got {sites[pc]}")
            regs = memory_dump(parts[n], 0x81, 3); peak = max(peak, regs[0])
            if address >= 256:
                require(regs[1:] == address.to_bytes(2, "little"), "TX actual DPTR/MMIO changed")
                if actual_address is None:
                    require(address in SETTINGS, "TX indexed access escaped settings")
            observed = address if observed is None else observed
            require(memory_dump(parts[n+1], observed, 1)[0] == value, "TX actual MMIO operand mismatch")
        pc = end if record["phase"] == 5 else fault if record["phase"] == 6 else wait
        check_pc(parts[final], pc)
        ram, iram, sfr = last = snap(parts, final)
        require(ram[proof["state"]:proof["state"]+SIZE] == bytes.fromhex(step["state"]),
                vector["name"]+": TX state differs from real native composition")
        diagnostics = []
        for name, sizes in (("clock", CLOCK_SIZES), ("fifo", FIFO_SIZES), ("tx", TX_SIZES)):
            require(len(sizes) == len(step[name]), "TX diagnostic serializer changed")
            expected = b"".join(v.to_bytes(n, "little") for v, n in zip(step[name], sizes))
            a = proof[name]; require(ram[a:a+len(expected)] == expected, "TX "+name+" diagnostic mismatch")
            diagnostics.append(expected)
        check_end(record, *diagnostics)
        require(ram[0x1e00:0x1e20] == bytes.fromhex(step["boot"]), "TX M0/heartbeat changed")
        require(ram[proof["mailbox"]:proof["mailbox"]+8] ==
                (bytes.fromhex(step["packet"]) if record["phase"] == 6 and record["remaining"] == 0
                 and record["reason"] == 2 else bytes(8)), "TX packet clear mismatch")
        check_guards(ram, iram[128:], sfr, symbols)
        require(sfr[1] == symbols["s_SSEG"]+1 and peak < 128, "TX stack/unwind/upper IRAM failed")
        for a, v in current.items():
            if a < 256: require(sfr[a-128] == v, f"TX guarded SFR changed {a:02x}")
        radio = memory_dump(parts[final+4], 0x6000, 1024)
        require(all(v == current.get(a, 0x69) for a, v in enumerate(radio, 0x6000)),
                "TX radio/FIFO memory changed beyond explicit modeled effects")
    require(last == snap(parts, number), "TX terminal loop altered CPU/RAM")
    # The final state follows all executed instructions, including the terminal
    # loop check. uCsim's cumulative maximum must cover every MMIO sample too.
    full_peak = stack_high_water(parts[number])
    require(full_peak >= peak, "TX full-run stack high-water below an MMIO sample")
    return peak, full_peak


def rejections(image, symbols, debug, board):
    case = unittest.TestCase()
    for a in image:
        with case.assertRaises(ValueError):
            verify_code(image | {a: image[a] ^ 1}, board)
    for name in ("_radio_tx_fixture_wait", "_radio_tx_fixture_body", "_radio_tx_fixture_state",
                 "_radio_tx_fixture_tx", "_radio_tx_reserved_end", "__gptrput_PARM_2",
                 "_m0_status", "__XPAGE", "s_SSEG", "l_SSEG", "l_XSEG", "l_OSEG", "l_BSEG",
                 "l_XABS", "l_XISEG", "l_PSEG", "l_REG_BANK_1"):
        with case.assertRaises(ValueError):
            verify_fixture(image, symbols | {name: symbols[name]+1}, debug, board)
    for old, new in (("({24}ST", "({23}ST"), ("({29}ST", "({30}ST"),
                     ("{12}S:S$txdone", "{13}S:S$txdone"), ("({2}DX,ST", "({3}DG,ST"),
                     ("C$radio_tx.c$", "C$missing.c$")):
        require(old in debug, "TX rejection mutation absent")
        with case.assertRaises(ValueError):
            verify_fixture(image, symbols, debug.replace(old, new), board)
    # Exercise the complete private multiset, independently of CODE/listings:
    # every F/S helper declaration, L entry/end/object and T field record.
    for record in private_records(debug).split("\n"):
        changed = (record.rsplit(":", 1)[0]+":"+f"{int(record.rsplit(':', 1)[1], 16)+1:X}"
                   if record.startswith("L:") else
                   re.sub(r"\(\{(\d+)\}", lambda m: "({"+str(int(m[1])+1)+"}", record, count=1))
        require(changed != record, "TX private metadata negative did not apply")
        for mutation in (debug.replace(record+"\n", "", 1),
                         debug.replace(record, changed, 1), debug+"\n"+changed+"\n"):
            with case.assertRaisesRegex(ValueError, "public/private/caller/field ABI"):
                verify_fixture(image, symbols, mutation, board)
    # Public duplicates from multiple translation units may be identical.
    # Distinct declarations (including an appended alternative) must not pass.
    public = public_records(debug).split("\n")
    verify_fixture(image, symbols, debug+"\n"+"\n".join(public)+"\n", board)
    for record in public:
        changed = (record.rsplit(":", 1)[0]+":"+f"{int(record.rsplit(':', 1)[1], 16)+1:X}"
                   if record.startswith("L:") else
                   re.sub(r"\(\{(\d+)\}", lambda m: "({"+str(int(m[1])+1)+"}", record, count=1))
        require(changed != record, "TX public metadata negative did not apply")
        with case.assertRaisesRegex(ValueError, "public/private/caller/field ABI"):
            verify_fixture(image, symbols, debug+"\n"+changed+"\n", board)
    helpers = [r for r in private_records(debug).split("\n")
               if re.match(r"^[FSL]:X?Fclock\$observe\$", r)]
    require(len(helpers) == 4, "TX malformed-helper negative inventory changed")
    for record in helpers:
        hostile = (record.rsplit(":", 1)[0]+":7FFF" if record.startswith("L:")
                   else record.replace("({", "({9", 1))
        for suffix in ("\r", "\v", "\f", "\x1c", "\x1d", "\x1e", "\x85", "\u2028", "\u2029"):
            for mutation in (debug.replace(record+"\n", record+suffix+"\n", 1),
                             debug+"\n"+record+suffix+"\n",
                             debug+"\n"+suffix+hostile+"\n"):
                with case.assertRaisesRegex(ValueError, "non-LF line separator"):
                    verify_fixture(image, symbols, mutation, board)


def check_radio_tx_fixture(simulator, output, board, symbols):
    """Shared boot dispatch and CLI run the same complete, unmodified corpus."""
    output = Path(output)
    fixture = load_image(output, board)
    path = output/"radio_tx_fixture.ihx"
    image = parse_ihex(path.read_text())
    require(symbols == parse_symbols(path.with_suffix(".map").read_text()),
            "TX supplied symbols differ from the canonical linked map")
    debug = path.with_suffix(".cdb").read_bytes().decode("utf-8")
    rejections(image, symbols, debug, board)
    check_alias(simulator)
    with unittest.TestCase().assertRaisesRegex(ValueError, "register bank"):
        check_alias(simulator, alias=False)
    native = subprocess.run([str(output/("host-radio-tx-fixture-tests_"+board)), "--vectors"],
                            capture_output=True, text=True, timeout=15, check=True)
    vectors = [json.loads(line) for line in native.stdout.splitlines()]
    require(len(vectors) == 36, "TX fixture case inventory changed")
    peaks = [run_vector(simulator, path, board, symbols, fixture.radio_tx_proof, v) for v in vectors]
    mmio_peak = max(p[0] for p in peaks)
    full_peak = max(p[1] for p in peaks)
    require(full_peak == 0x77, "TX reviewed overall full-run stack high-water changed")
    count = sum(len(v["steps"]) for v in vectors)
    events = sum(len(s["events"]) for v in vectors for s in v["steps"])
    print(f"{board}: TX fixture {len(image)} CODE, 347 ordinary+64 reserved XDATA, "
          f"stack61..FF/MMIO-sampled SP=0x{mmio_peak:02X}/full-run SP high-water=0x{full_peak:02X}; "
          f"{len(vectors)} cases/{count} checkpoints/{events} actual MMIO "
          "PASS. Boot-disarmed, real clock/FIFO/TX, retained faults; synthetic only, NOT RF evidence.")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--board", choices=tuple(HASHES), required=True)
    p.add_argument("--simulator", default="s51")
    args = p.parse_args()
    symbols = parse_symbols((args.output/"radio_tx_fixture.map").read_text())
    check_radio_tx_fixture(args.simulator, args.output, args.board, symbols)


if __name__ == "__main__":
    main()
