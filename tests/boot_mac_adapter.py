#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Execute real banked MAC/radio calls with synthetic peripheral inputs only."""
import argparse
from dataclasses import dataclass
import json
from pathlib import Path
import re
import subprocess

import boot_banked as banking
import verify_mac_adapter as layout
from boot_banked_security import capture, captured_sections, restore, store
from boot_image import check_alias, check_pc, marker, memory_dump, simulate_binary_dumps, snapshot_commands
from boot_mac_attempt import mmio_sites, rejected
from boot_radio_tx_fixture import sections, snap, stack_high_water
from radio_link_fixture import SETTINGS
from verify_firmware import require

INPUTS = {
    "op": "operation", "timeout": "timeout", "limit": "limit", "policy": "policy",
    "selector": "selector", "random_byte": "random_byte", "length": "length",
    "dsn": "dsn", "work": "work", "lifetime": "lifetime", "token": "token",
    "config": "configuration", "through": "through", "packet": "packet",
    **{name: name for name in ("config_ptr", "tx_ptr", "action_ptr", "clock_ptr", "through_ptr")},
}
OUTPUTS = ("return", "tx", "action", "random", "clock", "observation", "diagnostics", "record")


@dataclass(frozen=True)
class Case:
    name: str
    digest: str
    calls: int
    events: int
    sampled: int = 0x76
    peak: int = 0x78


CASES = (
    Case("handoff-data", "ed0e216077f75e25b4c40026a484611710ca4ee710d4a8f616c1e165e8058e46", 114, 10361),
    Case("busy-cca", "daf76ebf14a6c2308c27885ee659b1697c7c66b02b4ab9152f926d242c95be8d", 380, 27283),
    Case("absent-ack", "a6b6eb060893d583c5846679d453379042bd17cc1ceba190ff94511afd5bdb88", 346, 28977),
    Case("raw-lease", "a1068bab4877c9e7342a39e1bb02b624d6dc65c661a8a8ae0069a6fa5ad86beb", 94, 8490),
    Case("queued-close", "7d4d9f88a2efd8a564043624e3e7f403921b9e55192f9001f21e56387b7e1822", 24, 4646, 0x72, 0x74),
    Case("held-clock-fault", "fc1e96d8cf966be5b9e6f474220c6ce8e4f4b007b45cd5c815a44859b9a357c9", 10, 2161, 0x72, 0x74),
    Case("held-overflow", "e19acdaffccddb7890582812b8327ee042e948383a14569094218a29d3e09207", 14, 3252, 0x72, 0x74),
    Case("sfd-race", "c33b2436a7118d50e35e0a0e47b319d9bad33e531b8f17706319e0940c7ab765", 93, 8705),
    Case("filter-readback", "f035209e359d1fa1c78b9f3bab4e1d7fe86c361694ef0133236d347d0e67e77d", 93, 8965),
    Case("wide-handoff", "35267a0ae19451786ddf685388c4f1779b0d6ad8d0014ff574cacc6e3ecee8b1", 93, 8868),
    Case("wrong-dsn", "e373d4e084354ca6282ed86344a44acf2ea71d311c208564d3a7d954a2c6c773", 338, 28137),
    Case("bad-crc", "198cc8fad115c09a4dd9a0748cf52d3bd97db33454f9c4ad3228c19f1a1f82da", 354, 28581),
    Case("cancel-resubmit", "cf395ebb9c43d1782ebd94a0149278ff8167a595a831c1670c5e943240faa53e", 101, 9850),
    Case("expired-action", "b67973d210ebc29fb84ab27974ba951c5b165266368d0fb537c6b8d53de2dab4", 27, 4323, 0x74, 0x76),
    Case("coarse-wrap", "00fe56e0b719d4bcd14a8d3415ffda08120a02a5d46ea1fa1c416856e26e29db", 102, 10135),
    Case("work-exhaustion", "07091d26e0d2f178ef304f00a085065a28a9aa577e16917c8feec7e505217fa5", 15, 3953, 0x74, 0x76),
    Case("invalid-storage", "75407bb2c6d1b1b70a05553cc3ee3275e5521b07337b90d6d79d6ec6c5a32786", 48, 2283, 0x72, 0x74),
    Case("later-ack-uncertainty", "67a538d1d5bb6f5cbcf608702381ce6d3c3ac1cc9ecdad9f834841c8a0aa508d", 159, 13168),
)


def raw_field(schema, name, value):
    field = schema.globals["fixture_"+name]
    if name in ("config", "through", "packet")+OUTPUTS[1:]:
        require(type(value) is str and re.fullmatch(r"[0-9a-f]*", value), "Malformed caller bytes")
        result = bytes.fromhex(value)
        require(len(result) == field.size, "Incomplete actual caller object: "+name)
        return result
    require(type(value) is int and 0 <= value < 1 << (8*field.size), "Invalid caller scalar")
    return value.to_bytes(field.size, "little")


def reference(output, number):
    require(type(number) is int and 0 <= number < len(CASES), "Unknown adapter reference")
    command = [str(output/"host-mac-adapter-vectors"), "--vector", str(number)]
    raw = subprocess.check_output(command, timeout=15)
    require(banking.sha(raw) == CASES[number].digest, "Complete raw adapter reference changed")
    require(raw == subprocess.check_output([command[0]+"-sanitize", *command[1:]], timeout=15),
            "Adapter native/sanitizer transcript mismatch")
    vector = json.loads(raw)
    require(set(vector) == {"case", "steps"} and vector["case"] == number and
            len(vector["steps"]) == CASES[number].calls and
            sum(len(s["events"]) for s in vector["steps"]) == CASES[number].events,
            "Missing/bounded adapter reference")
    return vector


def run_vector(simulator, artifacts, vector, *, chunk=64):
    image, symbols, debug, _, listings, _ = artifacts
    schema = layout.Layout(debug, symbols)
    sites = mmio_sites(image, debug.decode("ascii"), {m: r.decode("ascii") for m, r in listings.items()},
                       handoff=True)
    before, done = (symbols["_fixture_"+name] for name in ("before", "done"))
    model = banking.model(image)
    stops = [f"break {pc:#x}" for pc in (*sites, before, done, symbols["_banked_stop"])]
    initial_keys = {a for n, a in symbols.items() if n.startswith("_SOC_")} | set(range(0x6100, 0x6400)) | {0x6081, 0x6083}
    expected_media = bytearray(b"\xff"*0x40000)
    for address, byte in banking.pack(image).items():
        expected_media[address] = byte
    allowed = set(range(symbols["l_XSEG"])) | set(range(0x1e00, 0x1e08))
    state, current, peak, sampled, calls, events = None, {}, 0, 0, 0, 0
    for start in range(0, len(vector["steps"]), chunk):
        commands = model.copy()
        if state is None:
            commands += ["fill xram 0 0x1eff 0xa5", "fill xram 0x2000 0x7fff 0x69",
                         "set memory sfr 0x9a 0", f"run 0 {symbols['_main']:#x}",
                         "fill iram 0x7d 0xff 0xc7",
                         f"run {symbols['_main']:#x} {before:#x}"]
        else:
            commands += restore(state, before, memctr=0)+capture(60000)
        commands += stops
        checks, number = [], 10
        for step in vector["steps"][start:start+chunk]:
            require(set(step) == set(INPUTS.values()) | set(OUTPUTS) | {"initial", "events"} and
                    type(step["operation"]) is int and 0 <= step["operation"] <= 11 and
                    step["selector"] in (0, 1, 2, 3), "Unreviewed caller command/input")
            hardware = {int(a): v for a, v in step["initial"].items()}
            require(set(hardware) == initial_keys and all(type(v) is int and 0 <= v <= 255 for v in hardware.values()),
                    "Stimulus escaped peripheral ownership")
            commands += [f"set memory {'sfr' if a < 256 else 'xram'} {a:#x} {v:#x}"
                         for a, v in hardware.items() if current.get(a) != v]
            current = hardware
            for name, key in INPUTS.items():
                commands += store("xram", symbols["_fixture_"+name], raw_field(schema, name, step[key]))
            commands.append("step 1")
            first, previous = number, None
            for kind, address, value in step["events"]:
                require(kind in ("r", "w", "c") and type(value) is int and 0 <= value <= 255 and
                        (address in initial_keys if kind != "c" else address == 0 and value == 4),
                        "Unreviewed MMIO event")
                adjacent = previous is not None and previous[0] == kind == "w" and \
                    0xa2 <= previous[1] < 0xa6 and address == previous[1]+1
                adjacent |= previous == ("w", 0xe9) and kind == "r" and address == 0x6193
                if not adjacent:
                    commands.append("run")
                space = "sfr" if address < 256 else "xram"
                commands += [marker(number), "state", "dump /h sfr 0x81 0x83"]
                if kind == "r":
                    commands.append(f"set memory {space} {address:#x} {value:#x}")
                commands += [marker(number+1), "step 4" if kind == "c" else "step 1"]
                if kind == "c":
                    commands.append("state")
                elif kind == "r":
                    for dest in sorted({d for k, a, d in sites.values() if k == "r" and a in (None, address)}):
                        commands.append(f"dump /h {'iram' if dest < 128 else 'sfr'} {dest:#x} {dest:#x}")
                else:
                    commands.append(f"dump /h {space} {address:#x} {address:#x}")
                commands.append(marker(number+2)); number += 3
                if kind != "c":
                    current[address] = value
                previous = (kind, address)
            commands += ["run"]+snapshot_commands(number)+[
                marker(number+4), "dump /h xram 0x6000 0x63ff", marker(number+5),
                "step 1", "run", marker(number+6), "state", marker(number+7)]
            checks.append((step, first, number, current.copy())); number += 8
        require(number < 60000, "Unbounded adapter replay chunk")
        commands += capture(60010)
        parts = sections(simulate_binary_dumps(simulator, commands))
        if state is not None:
            require(captured_sections(parts, 60000, before) == state, "Continuation changed actual machine state")
        for step, first, final, hardware in checks:
            context = f"adapter case{vector['case']} call{calls} op{step['operation']}"
            for i, (kind, address, value) in enumerate(step["events"]):
                n = first+3*i
                match = re.search(r"CPU state= OK PC= 0x([0-9a-fA-F]+)", parts[n])
                require(match and int(match[1], 16) in sites, context+": unplanned peripheral stop")
                pc = int(match[1], 16); k, a, observed = sites[pc]
                require(kind == k and a in (None, address), context+f": event{i} MMIO identity at {pc:04x}")
                regs = memory_dump(parts[n], 0x81, 3); sampled = max(sampled, regs[0])
                if kind == "c":
                    check_pc(parts[n+1], pc+4)
                    continue
                if address >= 256:
                    require(regs[1:] == address.to_bytes(2, "little"), context+": actual DPTR")
                    if a is None:
                        require(address in SETTINGS or 0x616a <= address <= 0x6175, context+": indexed MMIO escaped")
                require(memory_dump(parts[n+1], address if observed is None else observed, 1)[0] == value,
                        context+": genuine MMIO instruction value")
            check_pc(parts[final], done); check_pc(parts[final+6], before)
            ram, iram, sfr = snap(parts, final)
            require(ram[0x1e00:0x1e08] == b"MAD1\x01\x08\0\0", context+": status reservation")
            for name, key in dict(INPUTS, **{n: n for n in OUTPUTS}).items():
                address = symbols["_fixture_"+name]
                expected = raw_field(schema, name, step[key])
                actual = ram[address:address+len(expected)]
                require(actual == expected, context+": caller "+name+f" {actual.hex()} != {expected.hex()}")
            require(all(v == 0xa5 for a, v in enumerate(ram) if a not in allowed), context+": unowned XDATA")
            require(iram[0x7d:] == b"\xc7"*131 and sfr[1] == symbols["s_SSEG"]+1,
                    context+f": stack/IRAM/SP {sfr[1]:02x}")
            require(iram[symbols["_banked_depth"]] == iram[symbols["_banked_fault"]] == 0 and
                    sfr[0x1f] == 1 and sfr[0x47] == 0, context+": bank return/depth/fault")
            require(all(sfr[a-128] == v for a, v in hardware.items() if a < 256), context+": unowned SFR")
            actual = memory_dump(parts[final+4], 0x6000, 1024)
            require(all(v == hardware.get(a, 0x69) for a, v in enumerate(actual, 0x6000)),
                    context+": unowned XREG/FIFO")
            peak = max(peak, stack_high_water(parts[final]))
            require(sampled <= peak <= 0x7c, context+f": SP{peak:02X} exceeds 7C")
            calls += 1; events += len(step["events"])
        state = captured_sections(parts, 60010, before)
        require(state[4] == expected_media and state[3][:0x4000] == b"\x69"*0x4000 and
                state[3][0x4400:] == b"\x69"*0x1c00, "Unowned flash/extended RAM write")
    return calls, events, sampled, peak


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--simulator", default="s51")
    parser.add_argument("--case", type=int, choices=range(len(CASES)))
    args = parser.parse_args()
    artifacts = layout.load(args.output)
    layout.verify(*artifacts)
    if args.case is None:
        count = banking.artifact_negatives(layout.artifact_bytes(*artifacts), layout.PINS)
        require(count == 283179, "Adapter complete artifact rejection inventory")
    check_alias(args.simulator); rejected(lambda: check_alias(args.simulator, False))
    banking.check_mapping(args.simulator)
    rejected(lambda: banking.check_mapping(args.simulator, code=False))
    rejected(lambda: banking.check_mapping(args.simulator, alias=False))
    calls = events = peak = 0
    for case in range(len(CASES)) if args.case is None else (args.case,):
        vector = reference(args.output, case)
        a, b, c, d = run_vector(args.simulator, artifacts, vector)
        spec = CASES[case]
        require((a, b, c, d) == (spec.calls, spec.events, spec.sampled, spec.peak), "Adapter replay/stack inventory")
        calls += a; events += b; peak = max(peak, d)
        print(f"MAC adapter {spec.name}: {a} genuine calls/{b} MMIO; SP{d:02X}/7C PASS.", flush=True)
    campaign = "283179 artifact +3 mapping/alias negatives; " if args.case is None else "selected case; "
    print(f"MAC adapter: {calls} calls/{events} MMIO; 56632 CODE,2848+64 XDATA; SP{peak:02X}/7C; "
          +campaign+"synthetic peripherals, no hardware or full-join claim.")


if __name__ == "__main__":
    main()
