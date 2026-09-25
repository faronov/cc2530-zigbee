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
    Case("handoff-data", "5ff57778dbdcbec67bbd5cc5dd6eb36fd62f72cbb58d980d314e47e1445ed2e5", 114, 10399),
    Case("busy-cca", "5a51874fc2e1417aff40667c535884b0feee537487a40be38575d63c05b380fd", 380, 26178),
    Case("absent-ack", "8179b2c3b020645bd474ddc4539965645399a6430528bdf7a01824627f2978de", 346, 28305),
    Case("raw-lease", "d1bdbcf403c42efdc943e87422cd210651509d5221dc9d9bf6beee828471ce14", 94, 8523),
    Case("queued-close", "7cfa7c8b43c27b5a1dc4d2c53ef4bed18f4cbfd2c8ea1affd8d50b5b76a3c963", 24, 4646, 0x72, 0x74),
    Case("held-clock-fault", "8a0ce24e154dc924f4c56dcc831b4286cf2ab51c0b323bf3e889e63549af2071", 10, 2161, 0x72, 0x74),
    Case("held-overflow", "fca6f2c38e1749b44ab9cb87248a3a12c00a5d0957d5db7bbeac9b862a814b2b", 14, 3059, 0x72, 0x74),
    Case("sfd-race", "bc62ec98126430b5ee4d95c362edee032973c86e9c31d1f005e44107bdad712b", 93, 8743),
    Case("filter-readback", "7bb58b589e7ff0802c13007833447798127da5b57d3ef47db561cea68f48847a", 93, 9003),
    Case("wide-handoff", "9e5e1e33a5133fafdac38e4b819caafa578b7305431f66287957e6bde25ea3a5", 93, 8906),
    Case("wrong-dsn", "d74aff38dc71a7d65f5903f48efffdf9c55d4a28cfaf57018b10490514b7b9a6", 338, 28289),
    Case("bad-crc", "fe1e4e6f27c09a4584b2849252a425968ff82c8152cc3ae606744bba08fe91a5", 358, 28881),
    Case("cancel-resubmit", "b607d37ef80216d85690dce4c0bd6e52a6bcbb97ebad0b54c8c144c0fcbff12d", 101, 9883),
    Case("expired-action", "92e505375b692de3c4468762b4a3a85ea6d3522a2a45e56c740ba64ea008f8fe", 27, 4323, 0x74, 0x76),
    Case("coarse-wrap", "36f20b69ffc7532c8994ffd18dbf4ff41cfebe1efca1b2fecb9ba609de46c73e", 103, 10247),
    Case("work-exhaustion", "e86afbc414bcf7d2ab600329fb0493e484036590a128796ae64635c6f213185e", 15, 3953, 0x74, 0x76),
    Case("invalid-storage", "042b12e95b13864e6209291bff426b0def19ff6a3c92842a044fab5c6eba6419", 48, 2283, 0x72, 0x74),
    Case("later-ack-uncertainty", "059519224180f41d1637a15d0391dbb9e289244e391c012690ca88e26e2d747d", 159, 13206),
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
        require(count == 277229, "Adapter complete artifact rejection inventory")
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
    campaign = "277229 artifact +3 mapping/alias negatives; " if args.case is None else "selected case; "
    print(f"MAC adapter: {calls} calls/{events} MMIO; 55442 CODE,2848+64 XDATA; SP{peak:02X}/7C; "
          +campaign+"synthetic peripherals, no hardware or full-join claim.")


if __name__ == "__main__":
    main()
