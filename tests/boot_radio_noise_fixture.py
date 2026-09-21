#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Execute genuine board/startup/clock/IRND/health with immutable synthetic IO."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from boot_image import boot_commands, check_alias, check_guards, check_pc, expected_status, memory_dump, simulate
from boot_radio_tx_fixture import stack_high_water
from radio_noise_fixture import (
    SIZE, COMMAND_SIZE, CAPTURE_SIZE, HEALTH_SIZE, CLOCK_SIZE, REQUEST,
    CAPTURE_SIZES, HEALTH_SIZES, CLOCK_SIZES, HASHES, modules, decode, decode_capture,
    load_image, verify_code, verify_fixture, verify_listings,
)
from verify_firmware import parse_ihex, parse_symbols, require


def marker(n):
    # Larger ordinal space than older small corpora; still an exact 32-bit
    # expression. Both full 4096-poll clock attempts fit without wrapping IDs.
    require(0 <= n < 0x1000000, "IRND fixture marker overflow")
    return f"expression /0 {0x25000000+n:#x}"


def snapshots(n):
    return [marker(n), "state", "dump /h xram 0 0x1eff",
            marker(n+1), "dump /h iram 0 0xff",
            marker(n+2), "dump /h sfr 0x80 0xff", marker(n+3)]


def sections(text):
    parts = re.split(r"^0x25([0-9a-f]{6})\r?\n", text, flags=re.M)
    ids = [int(parts[i], 16) for i in range(1, len(parts), 2)]
    require(len(ids) == len(set(ids)), "Duplicate IRND fixture transcript marker")
    return dict(zip(ids, parts[2::2]))


def snap(parts, n):
    return (memory_dump(parts[n], 0, 0x1f00), memory_dump(parts[n+1], 0, 256),
            memory_dump(parts[n+2], 0x80, 128))


def packed(values, sizes):
    require(len(values) == len(sizes), "IRND fixture serializer shape changed")
    return b"".join(v.to_bytes(n, "little") for v, n in zip(values, sizes))


def repeated_events(events, index):
    """Only immutable complete observation cycles may use counted breakpoints.
    Changing stimuli, writes, raw reads and shortened work limits never qualify.
    """
    for shape in (("o", "t"), ("c", "c", "t")):
        width = len(shape); block = events[index:index+width]
        if len(block) != width or tuple(e[0] for e in block) != shape or any(e[3] for e in block):
            continue
        count = 1
        while events[index+count*width:index+(count+1)*width] == block:
            count += 1
        if count >= 32:
            return width, count
    return 0, 0


def run_vector(simulator, path, board, symbols, proof, vector):
    wait, end, fault = proof["checkpoints"]
    current = {int(a): v for a, v in vector["initial"].items()}
    initial = dict(current)
    commands = boot_commands(symbols) + ["fill iram 0x7d 0xff 0xc7", "fill xram 0x6000 0x63ff 0x69"]
    commands += [f"set memory {'sfr' if a < 256 else 'xram'} {a:#x} {v:#x}" for a, v in current.items()]
    commands += [f"fill xram {proof['command']:#x} {proof['command']+15:#x} 0xa6",
                 f"run {symbols['_main']:#x} {wait:#x}"] + snapshots(1)
    commands += [f"break {pc:#x}" for pc in (*proof["sites"], wait, end, fault)]
    records = []; number = 10
    for step in vector["steps"]:
        raw = bytes.fromhex(step["packet"])
        commands.append(f"set memory xram {proof['command']:#x} " + " ".join(hex(v) for v in raw))
        if step["corrupt"]:
            commands.append(f"set memory xram {proof['state']+8:#x} 1")
        require(step["repeat"] == 1 or not step["events"], "Repeated empty command did peripheral work")
        for _ in range(step["repeat"]-1):
            commands += ["step 1", "run"]
        commands += ["step 1"]
        observed_events = []; counted = []; index = 0
        while index < len(step["events"]):
            width, repeats = repeated_events(step["events"], index)
            commands.append(marker(number))
            if repeats:
                block = step["events"][index:index+width]
                skipped = {pc for pc, (k, a, _) in proof["sites"].items()
                           if any(k == e[0] and a == e[1] for e in block)}
                first_kind, first_address = block[0][:2]
                candidates = [pc for pc in skipped if proof["sites"][pc][:2] == (first_kind, first_address)]
                # c/CMD has both the fixture precondition and the actual clock
                # observe site; only the latter is in this counted wait loop.
                pc = min(candidates)
                commands += ["delete"] + [f"break {p:#x}" for p in (*proof["sites"], wait, end, fault) if p not in skipped]
                commands += [f"break {pc:#x} {repeats}", "run"]
                index += (repeats-1)*width
                if first_kind == "o":
                    address = proof["capture"]+28
                    expected = sum(e[0] == "o" for e in step["events"][:index])-1
                else:
                    writes = [i for i, e in enumerate(step["events"][:index]) if e[0] == "w" and e[1] == 0xc6]
                    require(writes, "Counted clock wait has no actual request")
                    last_write = writes[-1]
                    address = proof["clock"]+(4 if step["events"][last_write][2] == 0x88 else 11)
                    expected = sum(e[0] == "t" for e in step["events"][last_write+1:index])
                commands += [f"dump /h xram {address:#x} {address+1:#x}", "delete"]
                commands += [f"break {p:#x}" for p in (*proof["sites"], wait, end, fault)]
                counted.append((number, pc, address, expected, repeats))
            else:
                commands.append("run")
            kind, address, value, changes = step["events"][index]
            commands.append("dump /h sfr 0x81 0x83")
            updates = [f"set memory {'sfr' if a < 256 else 'xram'} {a:#x} {v:#x}" for a, v in changes]
            if kind != "w":
                commands += updates
            commands += ["step 1", marker(number+1)]
            # Dump only the actual operand, not all128 SFRs at every access.
            # Full CPU/RAM/peripheral snapshots remain at every checkpoint.
            if kind == "c":
                commands += ["dump /h iram 0 7", "dump /h sfr 0xe0 0xe0",
                             f"dump /h sfr {address:#x} {address:#x}"]
            else:
                observed = address if kind == "w" else 0xe0
                commands.append(f"dump /h {'sfr' if observed < 256 else 'xram'} {observed:#x} {observed:#x}")
            commands.append(marker(number+2))
            if kind == "w":
                commands += updates; current[address] = value
            current.update(changes)
            observed_events.append((index, number)); index += 1; number += 3
        commands += ["run"] + snapshots(number)
        commands += [marker(number+4), "dump /h xram 0x6000 0x63ff", marker(number+5)]
        records.append((step, observed_events, counted, number, dict(current))); number += 6
    # Terminals are actual self-loops: neither late ARM/RUN nor extra execution
    # can reenter a service. Native coverage separately calls poll/initialize.
    commands += [f"fill xram {proof['command']:#x} {proof['command']+15:#x} 0xa6",
                 "step 64"] + snapshots(number)
    terminal = number
    if vector["name"] in ("success", "controller-prefix"):
        # A new CPU/SFR reset plus explicitly reset synthetic radio registers,
        # not just a jump to the CRT with retained peripheral ownership.
        commands += ["delete", "reset"] + boot_commands(symbols)
        commands += [f"set memory {'sfr' if a < 256 else 'xram'} {a:#x} {v:#x}" for a, v in initial.items()]
        commands += [f"run {symbols['_main']:#x} {wait:#x}"] + snapshots(number+6)
    text = simulate(simulator, commands, path)
    parts = sections(text)
    boot, bi, bs = snap(parts, 1)
    require(boot[0x1e00:0x1e20] == expected_status(board) and
            decode(boot[proof["state"]:proof["state"]+SIZE])["phase"] == "DISARMED" and
            boot[proof["command"]:proof["command"]+16] == bytes(16),
            "IRND boot retained authorization or changed real M0 startup")
    check_guards(boot, bi[128:], bs, symbols)
    peak = 0; last = None
    for step, observed_events, counted, final, current in records:
        for n, pc, address, expected, repeats in counted:
            require(repeats >= 32 and int.from_bytes(memory_dump(parts[n], address, 2), "little") == expected,
                    "IRND counted breakpoint disagrees with actual remaining poll budget")
            require(f"Stop at 0x{pc:06x}: (104) Breakpoint" in parts[n],
                    "IRND counted wait escaped its real observation site")
        for i, n in observed_events:
            kind, address, value, _ = step["events"][i]
            match = re.search(r"Stop at 0x([0-9a-fA-F]+): \(104\) Breakpoint", parts[n])
            require(match is not None and int(match[1], 16) in proof["sites"], vector["name"]+": unplanned stop")
            pc = int(match[1], 16); sk, sa, observed = proof["sites"][pc]
            require(kind == sk and sa in (None, address), f"IRND MMIO order at {pc:04x}: {kind}/{address:x} != {sk}/{sa}")
            registers = memory_dump(parts[n], 0x81, 3); peak = max(peak, registers[0])
            if address >= 256:
                require(registers[1:] == address.to_bytes(2, "little"), "IRND actual DPTR mismatch")
                if sa is None:
                    require(address in (0x6189, 0x618a, 0x6180, 0x6182, 0x6194,
                                        0x6195, 0x61b2, 0x61fa, 0x61ae, 0x618f),
                            "IRND dynamic write escaped no-sync configuration")
            observed = address if observed is None else observed
            require(memory_dump(parts[n+1], observed, 1)[0] == (value & 255), "IRND actual IO value mismatch")
        record = decode(bytes.fromhex(step["state"]))
        pc = end if record["phase"] == "END" else fault if record["phase"] == "FAULT" else wait
        check_pc(parts[final], pc)
        ram, iram, sfr = last = snap(parts, final)
        require(ram[proof["state"]:proof["state"]+SIZE] == bytes.fromhex(step["state"]),
                vector["name"]+": state differs from actual native caller")
        expected = packed(step["capture"], CAPTURE_SIZES) + bytes.fromhex(step["data"])
        require(ram[proof["capture"]:proof["capture"]+CAPTURE_SIZE] == expected, "IRND actual raw capture differs")
        capture = decode_capture(expected)
        # Independent packing/prefix oracle; not only equality with native C.
        data = bytes.fromhex(capture["data_hex"])
        for i in range(1024):
            bit = (0 if i >= capture["samples"] or vector["pattern"] == 1 else
                   int(i % 3 == 2) if vector["pattern"] == 2 else (0x96 >> (i & 7)) & 1)
            require((data[i >> 3] >> (i & 7)) & 1 == bit, "IRND raw bit order/prefix changed")
        for name, sizes, size in (("health", HEALTH_SIZES, HEALTH_SIZE), ("clock", CLOCK_SIZES, CLOCK_SIZE)):
            require(ram[proof[name]:proof[name]+size] == packed(step[name], sizes), "IRND "+name+" diagnostics differ")
        require(ram[proof["request"]:proof["request"]+11] == REQUEST and
                ram[proof["command"]:proof["command"]+COMMAND_SIZE] == bytes(COMMAND_SIZE),
                "IRND modified fixed request or failed to consume command")
        require(ram[symbols["_radio_noise_used"]] == step["used"] and
                ram[symbols["_radio_noise_fault"]] == step["fault"], "IRND one-shot ownership changed")
        require(ram[0x1e00:0x1e20] == bytes.fromhex(step["boot"]), "IRND M0/heartbeat differs")
        check_guards(ram, iram[128:], sfr, symbols)
        require(iram[0x7d:] == b"\xc7"*131 and sfr[1] == symbols["s_SSEG"]+1 and sfr[0x12] == 0,
                "IRND SP7C/DPS0/stack unwind failed")
        for a, v in current.items():
            if a < 256:
                require(sfr[a-128] == v, f"IRND unplanned SFR change {a:02x}")
        radio = memory_dump(parts[final+4], 0x6000, 1024)
        require(all(v == current.get(a, 0x69) for a, v in enumerate(radio, 0x6000)),
                "IRND unexpected peripheral write outside modeled effects")
        # Safe board GPIO/enables must persist independently of stimulus.
        for a in (0x80, 0x90, 0xa0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xfd, 0xfe, 0xff, 0x8f):
            require(sfr[a-128] == bs[a-128], "IRND changed startup board GPIO/routing policy")
    expected_ram = bytearray(last[0]); expected_ram[proof["command"]:proof["command"]+16] = b"\xa6"*16
    require(snap(parts, terminal) == (bytes(expected_ram), last[1], last[2]),
            "IRND terminal execution changed CPU/RAM or consumed a stale command")
    high = stack_high_water(parts[terminal])
    require(peak <= high <= 0x7c, "IRND full-run SP7C limit failed")
    if vector["name"] in ("success", "controller-prefix"):
        reset, _, reset_sfr = snap(parts, terminal+6)
        require(reset[0x1e00:0x1e20] == expected_status(board) and
                reset[proof["state"]:proof["state"]+SIZE] == boot[proof["state"]:proof["state"]+SIZE] and
                reset[proof["capture"]:proof["capture"]+171] == bytes(171) and
                not reset[symbols["_radio_noise_used"]] and not reset[symbols["_radio_noise_fault"]],
                "IRND full CRT reset failed to establish a new disarmed epoch")
        require(all(reset_sfr[a-128] == 0 for a in (0xa8, 0xb8, 0x9a, 0xd6, 0xd7, 0xe1, 0xe9, 0x91, 0xbf)),
                "IRND synthetic full reset retained peripheral flags/ownership")
    return peak, high


def rejections(output, image, symbols, debug, board):
    case = unittest.TestCase(); count = 0
    for a in image:
        with case.assertRaisesRegex(ValueError, "CODE"):
            verify_code(image | {a: image[a] ^ 1}, board)
        count += 1
    for name in symbols:
        with case.assertRaisesRegex(ValueError, "map"):
            verify_fixture(image, symbols | {name: symbols[name]+1}, debug, board)
        count += 1
    # Entire raw CDB is bound, including all private F/S/L/T records, source
    # locations and duplicate public declarations. No normalization escape.
    for line in set(debug.splitlines()):
        with case.assertRaisesRegex(ValueError, "CDB"):
            verify_fixture(image, symbols, debug.replace(line+"\n", "", 1), board)
        count += 1
    with case.assertRaisesRegex(ValueError, "CDB"):
        verify_fixture(image, symbols, debug.replace("\n", "\r\n"), board)
    count += 1
    with tempfile.TemporaryDirectory(prefix="irnd-listing-negative-") as directory:
        work = Path(directory)
        for name in modules(board):
            path = output / f"radio_noise_fixture.{name}.rst"
            (work / path.name).write_bytes(path.read_bytes())
        for path in work.iterdir():
            raw = path.read_bytes(); path.write_bytes(raw+b"\n")
            with case.assertRaisesRegex(ValueError, "listing"):
                verify_listings(work, image, symbols, board)
            path.write_bytes(raw); count += 1
    return count


def check_radio_noise_fixture(simulator, output, board, symbols):
    output = Path(output); fixture = load_image(output, board)
    path = output / "radio_noise_fixture.ihx"
    image = parse_ihex(path.read_text())
    require(symbols == parse_symbols(path.with_suffix(".map").read_text()), "IRND supplied map differs")
    debug = path.with_suffix(".cdb").read_bytes().decode("ascii")
    negatives = rejections(output, image, symbols, debug, board)
    check_alias(simulator)
    with unittest.TestCase().assertRaisesRegex(ValueError, "register bank"):
        check_alias(simulator, False)
    native = subprocess.run([str(output / ("host-radio-noise-fixture-tests_"+board)), "--vectors"],
                            capture_output=True, text=True, timeout=15, check=True)
    vectors = [json.loads(line) for line in native.stdout.splitlines()]
    require(len(vectors) == 49, "IRND fixture scenario inventory changed")
    peaks = []
    for vector in vectors:
        try:
            peaks.append(run_vector(simulator, path, board, symbols, fixture.radio_noise_proof, vector))
        except ValueError as error:
            raise ValueError(vector["name"]+": "+str(error)) from error
    print(f"{board}: raw IRND board {len(image)} CODE, 440+64 XDATA; {len(vectors)} genuine scenarios, "
          f"{negatives} artifact +1 alias negatives; MMIO SP{max(p[0] for p in peaks):02X}, "
          f"full-run SP{max(p[1] for p in peaks):02X}, stack2F..FF. Synthetic only; hardware pending.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--board", choices=tuple(HASHES), required=True)
    parser.add_argument("--simulator", default="s51")
    args = parser.parse_args()
    check_radio_noise_fixture(args.simulator, args.output, args.board,
                              parse_symbols((args.output / "radio_noise_fixture.map").read_text()))


if __name__ == "__main__":
    main()
