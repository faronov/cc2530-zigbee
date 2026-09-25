#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Execute the complete synthetic join transcript on the real banked MCU image."""
import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/"tools"))

import boot_banked as banking
import boot_banked_security as mixed
import boot_flash_exec as flash
import boot_zigbee_security as aes
import verify_banked_join as layout
from boot_security_counter import sections
from banked_image import ihex
from boot_image import check_alias, simulate_binary_dumps
from boot_zdo_node import rejected
from verify_firmware import require

REFERENCE_SHA = "9897c6467d17df75491aa33a6654bc20ca5117e55fa12538d19ae03799cba579"
CASES = ("joined-data-update-loss-restart", "missing-network-key", "retained-radio-fault")
FLASH_FAILURE = "retained-flash-fault"
EDGE_CASES = {
    "rx-queues": ("88e72147bb38a77a48b61539dd71fb3ad37c7cfb2ef22824f54775e48bf56f20", 136, 1309),
    "wrap-quarantine": ("98ca74aa0323b21e6fa368016d453d5c06105f944b91665877dd8ef68ad1007d", 1154, 1108),
    "ack-correlation": ("ec1d2b312eaea518d79eb4de24c831a74f3f69cc8ed6b19bbd519085f7b6a5f4", 136, 1411),
    "ack-deadlines": ("f453528673a362e8699130dd886e02d796173f3d92f0a8739d1ab93d7a9885a8", 148, 1437),
    "zdo-server": ("4aa750331c2f19b20d32c00b995fc19e5c36fcd741ac58cbb1de17135e943099", 239, 2409),
    "broadcast-table": ("eeb59b035a9513d38c5344a045ea8680f3dd421954e2200bbc243cd1ba735c5e", 138, 1396),
    "address-map": ("272666a6dec9ec250ab25261fa70f1262d86ab4b1860ac957afc2aa9390fddb3", 143, 1358),
    "update-full": ("16c754fa2f7c10d12f1d5154071130d6bb45519a4ced34843e38f431a35df6a9", 146, 1273),
    "install-timeout": ("cbaf01f00def4c3a47112d4fd17505412f5b2680df9c29725d0bde7c829b907e", 127, 909),
    "node-correlation": ("a8c6235a902db9d157e05265fccce605b9fbc3af3137c9d113c5789a8d5073c3", 129, 1071),
    "node-timeout": ("e84675f07629dcfb4f9e25276ddbe769fcf44625b0ea3c75e6e6be32c8ae3d05", 89, 598),
    "node-status": ("08fd69c7f023117154ced88c12b04f08ab0fb16e53b19d8ec0d240557fc7681b", 71, 324),
    "tc-key-timeout": ("7d49bb12f9b8ec00c8162de45e1dafdc9e8548f5cb8cabcc87fab886efe51fcc", 101, 764),
    "tc-confirm-timeout": ("21ce7f0b57534d1bc6e238e8b5df8ae0842f0958af3097fc35b3ca2faf4358d2", 113, 852),
    "parent-status": ("e48f46efd708908e27f3e3420e2c78d5af1ba007c7a266e6906b5477bb95fc34", 114, 903),
    "network-key-late": ("ca047178558505135b37ebee3c8faddbaf0535cb145b38f28bc27e21e96fd386", 58, 228),
    "tc-key-late": ("71cebf6232bd07b8b4b09ce6e91fdb4ccb933b3f1b56cae9bef2e1b55d2da3f6", 88, 585),
    "tc-confirm-late": ("645ec7676aa2f8903caaec810ffd6729febdb816b8d8cd7e1abee1f40ee6826b", 102, 768),
}


def reference(executable, selected=None):
    require(selected is None or selected in EDGE_CASES, "Unknown complete join reference")
    digest, expected_calls, expected_events = ((REFERENCE_SHA, 415, 2791) if selected is None
                                              else EDGE_CASES[selected])
    raw = subprocess.run([str(executable)]+([] if selected is None else [selected]), capture_output=True,
                         check=True, timeout=15).stdout
    require(banking.sha(raw) == digest, "Complete synthetic join reference changed")
    text = raw.decode("ascii")
    calls, current, reset, case, total = [], None, None, None, None
    for line in text.splitlines():
        fields = line.split()
        require(fields, "Empty join reference record")
        kind = fields[0]
        if kind == "CASE":
            require(current is None and len(fields) == 2, "Invalid case boundary")
            case = fields[1]
        elif kind == "RESET":
            require(current is None and reset is None and len(fields) == 2 and fields[1] in ("0", "1"),
                    "Invalid join reset")
            reset = int(fields[1])
        elif kind == "CALL":
            require(current is None and case and len(fields) == 8, "Invalid join call")
            command, stamp, arg, length, written = map(int, fields[1:6])
            require(0 <= command <= 12 and 0 <= stamp < 2**32 and
                    0 <= arg <= 255 and 0 <= length <= 125 and written == 165,
                    "Invalid join scalar input")
            current = dict(command=command, now=stamp, arg=arg, length=length, written=written,
                           input=bytes.fromhex(fields[6]), output=bytes.fromhex(fields[7]),
                           reset=reset, case=case, events=[])
            require(len(current["input"]) == 180 and len(current["output"]) == 125, "Invalid join input extent")
            reset = None
        elif kind == "BLOCK":
            require(current is not None and len(fields) == 4, "Unowned join AES event")
            block = tuple(bytes.fromhex(p) for p in fields[1:])
            require(all(len(p) == 16 for p in block), "Invalid join AES extent")
            current["events"].append(("aes", block))
        elif kind == "FLASH":
            require(current is not None and len(fields) == 5, "Unowned join flash event")
            operation, page, offset = map(int, fields[1:4])
            word = bytes.fromhex(fields[4])
            require(operation in (1, 2) and page in (0, 1) and 0 <= offset <= 2044 and
                    offset % 4 == 0 and len(word) == 4, "Invalid join flash event")
            current["events"].append(("flash", (operation, page, offset, word)))
        elif kind == "RESULT":
            require(current is not None and len(fields) == 6 and "result" not in current, "Invalid join result")
            current.update(result=int(fields[1]), metadata_result=int(fields[2]), expected_written=int(fields[3]),
                           metadata=bytes.fromhex(fields[4]), expected_output=bytes.fromhex(fields[5]))
            require(len(current["metadata"]) == 37 and len(current["expected_output"]) == 125, "Invalid result extent")
        elif kind in ("DEVICE", "MAC", "NV"):
            name = kind.lower()
            require(current is not None and "result" in current and len(fields) == 2 and name not in current,
                    "Invalid join object outcome")
            current[name] = bytes.fromhex(fields[1])
            require(len(current[name]) == {"device": 1676, "mac": 168, "nv": 4096}[name],
                    "Invalid join observation extent")
            if name == "nv":
                require("device" in current and "mac" in current, "Missing public owner")
                calls.append(current)
                current = None
        elif kind == "DONE":
            require(current is None and reset is None and len(fields) == 2 and total is None, "Incomplete reference")
            total = int(fields[1])
        else:
            raise ValueError(f"Unknown join reference record: {kind}")
    require(current is None and reset is None and len(calls) == total == expected_calls and calls[0]["reset"] == 1,
            "Incomplete join transcript")
    require(sum(len(call["events"]) for call in calls) == expected_events and
            tuple(dict.fromkeys(call["case"] for call in calls)) == (CASES if selected is None else (selected,)),
            "Join scenario/peripheral coverage changed")
    return calls


def compare(actual, expected, name):
    require(len(actual) == len(expected), f"{name} observation extent differs")
    if actual != expected:
        offset = next(i for i, (a, b) in enumerate(zip(actual, expected)) if a != b)
        raise ValueError(f"{name} differs at +{offset:#x}: {actual[offset]:02x} != {expected[offset]:02x}")


def outcome(state, symbols, debug, schema, call, values, physical):
    ram, iram, sfr, extended, media = state
    compare(ram[0x1e00:0x1e27], bytes((call["result"], call["metadata_result"]))+call["metadata"],
            "Actual public result/key status")
    owner = symbols["_fixture_mac"]
    frame = symbols["_fixture_input"]+schema.at("fixture_input", "receive", "bytes").offset
    for root in ("device", "mac"):
        address = symbols["_fixture_"+root]
        size = schema.roots()[root][1].size
        compare(schema.canonical(root, ram[address:address+size], owner, frame), call[root], root)
    address = symbols["_fixture_output"]
    output = ram[address:address+125]
    root = {5: "action", 6: "radio", 10: "packet"}.get(call["command"])
    if root is not None:
        size = schema.roots()[root][1].size
        output = schema.canonical(root, output[:size], owner, frame)+output[size:]
    compare(output, call["expected_output"], "Public output")
    for name, expected in (values | {"written": bytes((call["expected_written"],))}).items():
        if name == "output":
            continue
        address = symbols["_fixture_"+name]
        compare(ram[address:address+len(expected)], expected, "Caller "+name)
    for module, address, size in mixed.wiped_regions(debug):
        require(ram[address:address+size] == bytes(size), f"Private {module} staging was not wiped")
    require(ram[symbols["l_XSEG"]:0x1e00] == b"\xa5"*(0x1e00-symbols["l_XSEG"]) and
            ram[0x1e27:] == b"\xa5"*(0x1f00-0x1e27), "Unowned/status XDATA changed")
    require(sfr[1] == 0x4f and sfr[0x1f] == 1 and sfr[0x47] == 2 and
            iram[symbols["_banked_depth"]] == iram[symbols["_banked_fault"]] == 0,
            "Join call failed to unwind stack/mapping/depth")
    require(iram[0x7d:] == b"\xc7"*(256-0x7d), "Join operation exceeded SP7C")
    for address, value in call["sfr"].items():
        require(sfr[address-0x80] == value, f"Guarded SFR {address:02x} changed")
    require(all(value == call["peripheral"].get(a, 0x69) for a, value in enumerate(extended, 0x2000)),
            "Unowned peripheral/information memory changed")
    require(media[0x3e800:0x3f800] == call["nv"] and media[:0x3e800] == physical[:0x3e800] and
            media[0x3f800:] == physical[0x3f800:], "Actual full flash/NV/lock neighbor differs")


def model(image, directory):
    commands = banking.model(image)
    first = next(i for i, c in enumerate(commands) if c.startswith("set memory flash "))
    result = ["set option analyzer false"]+commands[:first]
    physical = banking.pack(image)
    for base in range(0, 0x40000, 0x10000):
        path = directory/f"flash-{base:x}.ihx"
        data = {a-base: v for a, v in physical.items() if base <= a < base+0x10000}
        path.write_text(ihex(data))
        result += [f"memory create addressdecoder rom 0 0xffff flash {base:#x}", f'file "{path}"']
    result += ["memory create addressdecoder rom 0 0x7fff flash 0"]
    return result+banking.code_banks()+commands[-2:]


def groups(calls):
    index = 0
    while index < len(calls):
        end = index+1
        if not calls[index]["events"]:
            while end < min(index+8, len(calls)) and calls[end]["reset"] is None and not calls[end]["events"]:
                end += 1
        yield index, calls[index:end]
        index = end


def inputs(call, schema, frame):
    values = {name: call[name].to_bytes(size, "little")
              for name, size in (("command", 1), ("now", 4), ("arg", 1), ("length", 2), ("written", 1))}
    return values | {
        "input": schema.event_input(call["input"], frame) if call["command"] in (5, 6) else call["input"],
        "output": call["output"],
    }


def retained_failure(state, carry, symbols, schema):
    ram, iram, sfr, _, media = state
    require(carry is not None and ram[0x1e00:] == carry[0][0x1e00:] and media == carry[4],
            "Terminal join flash failure published caller status or changed media")
    require(sfr[0x47] == 0x0a and sfr[0x1f] == 1 and
            iram[symbols["_banked_depth"]] == 3 and iram[symbols["_banked_fault"]] == 0 and
            ram[7] == 7 and ram[symbols["_flash_write_status"]] == 10 and
            ram[symbols["_nv_record_diagnostic"]+4] == 13,
            "Actual nested join/journal/flash RAM failure was not retained")
    require(iram[0x7d:] == b"\xc7"*(256-0x7d) and
            ram[symbols["l_XSEG"]:0x1e00] == carry[0][symbols["l_XSEG"]:0x1e00],
            "Terminal join flash failure exceeded stack or ordinary XDATA ownership")
    for path, expected in ((("member",), 1), (("phase",), 5),
                           (("work", "runtime", "transport", "ready"), 0)):
        field = schema.at("fixture_device", *path)
        require(ram[symbols["_fixture_device"]+field.offset] == expected,
                "Terminal flash failure confused authenticated membership with READY")


def execute(output, simulator, calls, directory, *, limit=None, failure=False):
    image, symbols, debug_raw, _, listings, _ = layout.load(output)
    debug, schema = debug_raw.decode("ascii"), layout.Schema(debug_raw)
    sites, code = aes.aes_sites(listings["aes"].decode("ascii"))
    physical = bytearray(b"\xff"*0x40000)
    for address, byte in banking.pack(image).items():
        physical[address] = byte
    physical = bytes(physical)
    carry, media, peak, last_phase = None, b"\xff"*4096, 0, None
    before, after = (symbols["_banked_join_"+n] for n in ("before", "after"))
    frame = symbols["_fixture_input"]+schema.at("fixture_input", "receive", "bytes").offset
    phase_offset = schema.at("fixture_device", "phase").offset
    model_commands = model(image, directory)
    for index, group in groups(calls[:limit]):
        call = group[0]
        commands = model_commands+[aes.AES_ALIAS]
        if call["reset"] is not None:
            carry = None
            if call["reset"]:
                media = b"\xff"*4096
        commands += mixed.store("flash", 0x3e800, media)
        if carry is None:
            commands += ["fill xram 0 0x1eff 0xa5", "fill xram 0x2000 0x7fff 0x69",
                         f"run 0 {symbols['_main']:#x}", "fill iram 0x7d 0xff 0xc7"]
            commands += [f"set memory sfr {r:#x} {v:#x}" for r, v in mixed.INITIAL.items()]
            commands += ["set memory xram 0x624a 0xa5", "set memory xram 0x6276 0x44 0xff",
                         "set memory xram 0x6270 4", f"run {symbols['_main']:#x} {before:#x}"]
        else:
            commands += mixed.restore(carry, after)+mixed.capture(58000)
            commands += ["step 1", f"run {after+1:#x} {before:#x}"]
        values = inputs(call, schema, frame)
        expected_sfr = mixed.INITIAL.copy() if carry is None else {a: carry[2][a-0x80] for a in mixed.INITIAL}
        peripheral = {0x624a: 0xa5, 0x6270: 4, 0x6276: 0x44, 0x6277: 0xff}
        peripheral.update({a: 0x69 if carry is None else carry[3][a-0x2000] for a in (0x6271, 0x6272, 0x6273)})
        for kind, event in call["events"]:
            if kind == "aes":
                expected_sfr.update({0xb1: event[1][-1], 0xb2: event[2][-1], 0xb3: 0x48,
                                     0xd2: symbols["_aes_dma1"] & 255, 0xd3: symbols["_aes_dma1"] >> 8,
                                     0xd4: symbols["_aes_dma0"] & 255, 0xd5: symbols["_aes_dma0"] >> 8})
            else:
                operation, page, offset, word = event
                peripheral[0x6271], peripheral[0x6272] = (0xfa00+page*512+offset//4).to_bytes(2, "little")
                if operation == 2:
                    peripheral[0x6273] = word[-1]
        peripheral.update({0x70b1: expected_sfr[0xb1], 0x70b2: expected_sfr[0xb2]})
        call = call | {"sfr": expected_sfr, "peripheral": peripheral}
        for name, data in values.items():
            commands += mixed.store("xram", symbols["_fixture_"+name], data)
        commands += [f"break {after:#x}", f"break {symbols['_banked_stop']:#x}"]
        replay = mixed.Replay(commands, symbols, sites, code, image, flash_polls=2000)
        boundaries, terminal = [], False
        for number, (kind, event) in enumerate(call["events"], 1):
            if kind == "aes":
                aes.append_block(commands, symbols, *event, replay.write, replay.arm, replay.dump)
            else:
                terminal = failure and index == 51
                replay.flash(*event, stuck=terminal)
                if terminal:
                    break
            if number % 8 == 0:
                boundaries.append((len(commands), replay.current))
        stop = flash.STOP if terminal else after
        replay.at(stop)
        serial = replay.serial
        commands += mixed.capture(serial)
        observations = [(serial, call, values)]
        for following in group[1:]:
            serial += 10
            values = inputs(following, schema, frame)
            commands += ["step 1", f"run {after+1:#x} {before:#x}"]
            for name, data in values.items():
                commands += mixed.store("xram", symbols["_fixture_"+name], data)
            commands += ["run"]+mixed.capture(serial)
            observations.append((serial, following | {"sfr": expected_sfr, "peripheral": peripheral}, values))
        boundaries.append((len(commands), stop))
        text = None
        last_chunk = None

        def observe_chunk(simulator, chunk):
            nonlocal last_chunk
            last_chunk = simulate_binary_dumps(simulator, chunk)
            return last_chunk

        try:
            text = mixed.simulate_chunks(simulator, image, symbols, commands, boundaries,
                                         after_symbol="_banked_join_after", model_commands=model_commands,
                                         runner=observe_chunk)
            parts = sections(text)
            replay.check(parts)
            if carry is not None:
                require(mixed.captured_sections(parts, 58000, after) == carry, "Between-call continuation changed complete state")
            if terminal:
                state = mixed.captured_sections(parts, serial, stop)
                retained_failure(state, carry, symbols, schema)
            else:
                for number, observed, expected in observations:
                    state = mixed.captured_sections(parts, number, after)
                    outcome(state, symbols, debug, schema, observed, expected, physical)
        except (ValueError, subprocess.SubprocessError):
            standalone = banking.model(image)+commands[len(model_commands):]
            (output/"banked-join"/"failed-commands.txt").write_text("\n".join(standalone)+"\n")
            diagnostic = text if text is not None else last_chunk
            if diagnostic is not None:
                (output/"banked-join"/"failed-simulation.txt").write_text(diagnostic)
            print(f"Failed join operation {index}: command {call['command']}, case {call['case']}", file=sys.stderr)
            raise
        old_peak = peak
        peak = max(peak, *(int(v, 16) for v in re.findall(r"Max value of stack pointer=\s*0x([0-9a-f]+)", text)))
        if terminal:
            print("Complete join retained flash failure: actual RAM stop survives later idle, no READY/status publication",
                  flush=True)
            return peak
        media, carry = state[4][0x3e800:0x3f800], state
        for offset, observed in enumerate(group):
            phase = observed["device"][phase_offset]
            if phase != last_phase or old_peak != peak or (index+offset) % 25 == 0:
                print(f"Join operation {index+offset}: command {observed['command']}, phase {phase}, SP {peak:02x}", flush=True)
            last_phase, old_peak = phase, peak
    require(not failure, "Retained join flash failure was not executed")
    return peak


def run(output, simulator, calls, *, limit=None, failure=False):
    with tempfile.TemporaryDirectory(prefix="cc2530-join-") as directory:
        return execute(output, simulator, calls, Path(directory), limit=limit, failure=failure)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--simulator", default="s51")
    parser.add_argument("--limit", type=int, help="Development prefix only; not complete acceptance")
    parser.add_argument("--case", choices=("all",)+CASES+(FLASH_FAILURE,)+tuple(EDGE_CASES), default="all")
    args = parser.parse_args()
    require(args.limit is None or args.limit > 0, "Development prefix must contain operations")
    artifacts = layout.load(args.output)
    layout.verify(*artifacts)
    selected = args.case if args.case in EDGE_CASES else None
    calls = reference(args.output/"host-banked-join-vectors", selected)
    require(calls == reference(args.output/"host-banked-join-vectors-sanitize", selected),
            "Native/sanitized transcript differs")
    if args.case == "all" and args.limit is None:
        for selected in EDGE_CASES:
            extra = reference(args.output/"host-banked-join-vectors", selected)
            require(extra == reference(args.output/"host-banked-join-vectors-sanitize", selected),
                    "Native/sanitized edge transcript differs")
            calls += extra
    require(args.case != FLASH_FAILURE or args.limit is None, "Terminal flash case cannot be truncated")
    if args.case not in ("all", FLASH_FAILURE):
        calls = [call for call in calls if call["case"] == args.case]
        require(calls and calls[0]["reset"] == 1, "Join scenario does not begin with actual cold initialization")
    check_alias(args.simulator)
    rejected(lambda: check_alias(args.simulator, False))
    if args.limit is None and args.case in ("all", "missing-network-key"):
        count = banking.artifact_negatives(layout.artifact_bytes(*artifacts), layout.PINS)
        require(count == 716229, "Complete join artifact-negative coverage changed")
        print(f"Complete join: {count} immutable artifact negatives", flush=True)
    peak = run(args.output, args.simulator, calls, limit=args.limit, failure=args.case == FLASH_FAILURE)
    if args.case == "all" and args.limit is None:
        peak = max(peak, run(args.output, args.simulator, calls, failure=True))
    require(peak <= 0x7c, "Complete join exceeded the unchanged SP7C cap")
    if args.limit is None:
        require(peak == 0x7b, "Complete join exact stack peak changed")
    print(f"{'Partial' if args.limit is not None else 'Complete'} join transcript ({args.case}) executed; SP {peak:02x}")


if __name__ == "__main__":
    main()
