#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Mixed real banked key/CCM/AES/NV execution; synthetic peripherals, never flash."""
import argparse
from pathlib import Path
from functools import lru_cache
import re
import subprocess

import boot_banked as banking
import boot_flash_exec as flash
import boot_zigbee_security as aes
import verify_banked_security as layout
from boot_image import check_alias, check_pc, marker, memory_dump, simulate, snapshot, snapshot_commands
from boot_security_counter import sections
from verify_firmware import cdb_address, require


CONFIG = bytes.fromhex("1102030405060708220203040506070833020304050607083412ffff0ffe")
INSTALL = bytes.fromhex("83fed3407a939723a5c639b26916d505c3b5")
INITIAL = aes.GUARDS | {
    0xb1: 0x69, 0xb2: 0x69, 0xb3: 8, 0xd2: 0, 0xd3: 0, 0xd4: 0, 0xd5: 0,
    0xc7: 2, 0x9f: 1, 0x92: 0, 0x88: 0, 0xc8: 0,
}


def store(space, address, data):
    return [f"set memory {space} {address+i:#x} " +
            " ".join(hex(b) for b in data[i:i+32]) for i in range(0, len(data), 32)]


def capture(number):
    return snapshot_commands(number)+[
        marker(number+4), "dump /h xram 0x2000 0x7fff", marker(number+5),
        marker(number+6), "dump /h flash 0 0x3ffff", marker(number+7),
        marker(number+8), "dump /h xram 0x1f00 0x1fff", marker(number+9)]


def captured(text, number, pc):
    parts = sections(text)
    check_pc(parts[number], pc)
    cpu = snapshot(text, number)
    require(memory_dump(parts[number+8], 0x1f00, 256) == cpu[1],
            "Actual composition lost XDATA/IRAM aliasing")
    return (*cpu, memory_dump(parts[number+4], 0x2000, 0x6000),
            memory_dump(parts[number+6], 0, 0x40000))


def restore(state, pc):
    ram, iram, sfr, extended, media = state
    require(tuple(map(len, state)) == (0x1f00, 256, 128, 0x6000, 0x40000),
            "Incomplete banked continuation")
    require(type(pc) is int and 0 <= pc < 0x8000, "Continuation is not a common-CODE boundary")
    require(sfr[0x19] == 0 and not sfr[0x18] & 3 and not sfr[8] & 0x50 and not sfr[0x48] & 4 and
            sfr[0x47] == 2 and all(sfr[a-0x80] == 0 for a in (0xa8, 0xb8, 0x9a, 0xd1, 0xd6, 0xd7)),
            "Continuation requires quiescent synthetic IRQ/DMA/UART/timers and restored XMAP")
    commands = store("flash", 0x3e800, media[0x3e800:0x3f800])
    for space, address, data in (("xram", 0, ram), ("iram", 0, iram),
                                 ("xram", 0x2000, extended), ("sfr", 0x80, sfr[:0x19]),
                                 ("sfr", 0x9a, sfr[0x1a:])):
        commands += store(space, address, data)
    return commands+[f"pc {pc:#x}"]


def simulate_chunks(simulator, image, symbols, commands, boundaries):
    state, previous, pc, texts = None, 0, None, []
    for index, (end, stop) in enumerate(boundaries):
        n = 60000+index*20
        prefix = []
        if state is not None:
            prefix = banking.model(image)+[aes.AES_ALIAS]+restore(state, pc)+capture(n)
            prefix += [f"break {symbols['_banked_security_after']:#x}",
                       f"break {symbols['_banked_stop']:#x}"]
        text = simulate(simulator, prefix+commands[previous:end]+capture(n+10))
        if state is not None:
            require(captured(text, n, pc) == state, "Banked continuation changed CPU/RAM/peripheral/NV state")
        state = captured(text, n+10, stop)
        peaks = [int(v, 16) for v in re.findall(r"Max value of stack pointer=\s*0x([0-9a-f]+)", text)]
        require(peaks and max(peaks) <= 0x7c and state[1][0x7d:] == b"\xc7"*(256-0x7d),
                f"Intermediate execution exceeded SP7C at {stop:04x}, peak {max(peaks, default=0):02x}")
        texts.append(text)
        previous, pc = end, stop
    return "\n".join(texts)


def reference(executable):
    text = subprocess.run([str(executable)], capture_output=True, text=True,
                          check=True, timeout=15).stdout
    require(banking.sha(text.encode("ascii")) ==
            "b1ea8524cd0ee1443d089dafdc0fb99b5237b8093bbfdf9bc6744ac7c26f6e35",
            "Complete synthetic AES/flash/key lifecycle reference changed")
    calls, reset, current = [], None, None
    for line in text.splitlines():
        fields = line.split()
        if fields[0] == "RESET":
            require(current is None and len(fields) == 2 and fields[1] in ("0", "1"),
                    "Invalid reference reset")
            reset = int(fields[1])
        elif fields[0] == "CALL":
            require(current is None and len(fields) == 5, "Invalid reference call")
            current = dict(action=int(fields[1]), length=int(fields[2]),
                           raw=bytes.fromhex(fields[3]), input_packet=bytes.fromhex(fields[4]),
                           reset=reset, events=[])
            require(len(current["raw"]) == 116 and len(current["input_packet"]) == 120 and
                    0 <= current["length"] <= 116,
                    "Invalid reference input length")
            reset = None
        elif fields[0] == "BLOCK":
            require(current is not None and len(fields) == 4, "Unowned AES event")
            block = tuple(bytes.fromhex(p) for p in fields[1:])
            require(all(len(p) == 16 for p in block), "Invalid AES oracle extent")
            current["events"].append(("aes", block))
        elif fields[0] == "FLASH":
            require(current is not None and len(fields) == 5, "Unowned flash event")
            operation, page, offset = map(int, fields[1:4])
            word = bytes.fromhex(fields[4])
            require(operation in (1, 2) and page in (0, 1) and 0 <= offset <= 2044 and
                    offset % 4 == 0 and len(word) == 4, "Invalid flash oracle command")
            current["events"].append(("flash", (operation, page, offset, word)))
        elif fields[0] == "RESULT":
            require(current is not None and len(fields) == 7 and "result" not in current,
                    "Invalid reference result")
            current.update(result=int(fields[1]), written=int(fields[2]), event=int(fields[3]),
                           metadata=bytes.fromhex(fields[4]), frame=bytes.fromhex(fields[5]),
                           packet=bytes.fromhex(fields[6]))
            require(tuple(len(current[k]) for k in ("metadata", "frame", "packet")) == (37, 116, 120),
                    "Invalid reference output ABI")
        elif fields[0] == "NV":
            require(current is not None and "result" in current and len(fields) == 2,
                    "Invalid reference NV outcome")
            current["nv"] = bytes.fromhex(fields[1])
            require(len(current["nv"]) == 4096, "Incomplete reference NV")
            calls.append(current); current = None
        else:
            raise ValueError("Unknown reference record")
    require(current is None and reset is None and len(calls) == 22, "Incomplete key lifecycle oracle")
    return calls


class Replay:
    def __init__(self, commands, symbols, sites, code, image):
        self.commands, self.symbols, self.sites, self.code = commands, symbols, sites, code
        self.template = bytes(image[a] for a in range(0x62, 0xdd))
        self.serial, self.current, self.points, self.dumps = 10, None, [], []

    def at(self, pc):
        if self.current != pc:
            self.commands += [f"tbreak {pc:#x}", "run"]
        self.commands += [marker(self.serial), "state", marker(self.serial+1)]
        self.points.append((self.serial, pc))
        self.serial += 2
        self.current = pc

    def dump(self, space, address, expected):
        self.commands += [marker(self.serial),
                          f"dump /h {space} {address:#x} {address+len(expected)-1:#x}",
                          marker(self.serial+1)]
        self.dumps.append((self.serial, address, bytes(expected)))
        self.serial += 2

    def step(self, pc, size=1):
        self.at(pc)
        self.commands.append("step 1")
        self.current = pc+size

    def write(self, name, reg, value):
        pc = self.sites[name]
        self.step(pc, len(self.code[pc]))
        self.dump("sfr", reg, bytes((value,)))

    def arm(self, name, old):
        value = 1 if name == "input" else 2
        self.write(name, 0xd6, value)
        self.commands.append(f"set memory sfr 0xd6 {old | value}")
        self.step(self.sites[name]+12)
        self.current = None

    def flash(self, operation, page, offset, word, *, stuck=False):
        self.step(0x487, 2)
        self.dump("sfr", 0xc7, b"\x0a")
        self.commands += banking.xmap()
        self.at(flash.RAM)
        self.dump("rom", flash.RAM, self.template)
        self.dump("xram", 9, self.template)
        self.dump("xram", 0, bytes((4 | operation,))+word+b"\x03\0\xff\0")
        self.dump("sfr", 0x9f, b"\x01")
        self.step(flash.COMMAND)
        self.dump("xram", 0x6270, bytes((4 | operation,)) +
                  (0xfa00+page*512+offset//4).to_bytes(2, "little"))
        self.commands.append(f"set memory xram 0x6270 {0x84 | operation}")
        if operation == 2:
            for i, pc in enumerate((0x95, 0x97, 0x99, 0x9b)):
                self.at(pc+flash.DELTA)
                self.dump("sfr", 0x82, b"\x73\x62")
                self.dump("sfr", 0xe0, word[i:i+1])
                self.step(pc+flash.DELTA)
                self.dump("xram", 0x6273, word[i:i+1])
        self.step(flash.POLL)
        if stuck:
            self.at(flash.STOP)
            self.commands += ["step 64", "set memory xram 0x6270 4", "step 64"]
            self.at(flash.STOP)
            self.dump("sfr", 0xc7, b"\x0a")
            self.dump("xram", 7, b"\x07")
            return
        self.at(flash.POLL)
        self.commands.append("set memory xram 0x6270 4")
        address = 0x3e800+page*2048+offset
        if operation == 1:
            self.commands.append(f"fill flash {address:#x} {address+2047:#x} 0xff")
        else:
            self.dump("flash", address, b"\xff"*4)
            self.commands += store("flash", address, word)
        self.step(flash.POLL)
        self.at(flash.RET)
        self.dump("xram", 0x6270, b"\x04")
        self.step(flash.RET)
        self.current = 0x4b3
        self.at(0x4b3)
        self.step(0x4da, 2)
        self.dump("sfr", 0xc7, b"\x02")
        self.commands += banking.code_banks()

    def check(self, parts):
        for number, pc in self.points:
            try:
                check_pc(parts[number], pc)
            except (KeyError, ValueError) as exc:
                raise ValueError(f"Mixed replay event {number}: expected PC {pc:04x}") from exc
        for number, address, expected in self.dumps:
            require(memory_dump(parts[number], address, len(expected)) == expected,
                    f"Mixed replay event {number}: actual peripheral/operand {address:04x} differs")


@lru_cache(maxsize=1)
def wiped_regions(debug):
    regions = []
    for module, name, size in (("security_keys", "w", 617), ("ccm_star", "state", 267),
                                ("zigbee_mmo", "state", 89), ("zigbee_key_hash", "state", 60),
                                ("ed_wire", "crypto", 272)):
        key = f"F{module}${name}$0_0$0"
        require(re.findall(rf"^S:{re.escape(key)}\(\{{(\d+)\}}[^)]*\),F,0,0$", debug, re.M) == [str(size)],
                "Complete wipe-owned XDATA extent changed")
        regions.append((module, cdb_address(debug, "L:"+key), size))
    return tuple(regions)


def outcome(state, symbols, debug, call, values, physical):
    ram, iram, sfr, extended, media = state
    require(ram[0x1e00:0x1e02] == bytes((call["result"], 0)), "Public key/status result differs")
    outputs = {"frame": call["frame"], "metadata": call["metadata"], "packet": call["packet"],
               "event": bytes((call["event"],)), "written": bytes((call["written"],))}
    for name, expected in (values | outputs).items():
        a = symbols["_fixture_"+name]
        require(ram[a:a+len(expected)] == expected, f"Caller object {name} changed")
    for module, a, size in wiped_regions(debug):
        require(ram[a:a+size] == bytes(size), f"Private {module} staging was not wiped")
    require(ram[symbols["l_XSEG"]:0x1e00] == b"\xa5"*(0x1e00-symbols["l_XSEG"]) and
            ram[0x1e02:] == b"\xa5"*254, "Unowned/status XDATA changed")
    require(sfr[1] == 0x51 and sfr[0x1f] == 1 and sfr[0x47] == 2 and
            iram[symbols["_banked_depth"]] == iram[symbols["_banked_fault"]] == 0,
            "Banked lifecycle failed to unwind stack/mapping/depth")
    require(iram[0x7d:] == b"\xc7"*(256-0x7d), "Key operation exceeded SP7C")
    for a, value in call["sfr"].items():
        require(sfr[a-0x80] == value, f"Guarded SFR {a:02x} changed")
    allowed = call["peripheral"]
    require(all(value == allowed.get(a, 0x69) for a, value in enumerate(extended, 0x2000)),
            "Unowned peripheral/information memory changed")
    require(media[0x3e800:0x3f800] == call["nv"] and media[:0x3e800] == physical[:0x3e800] and
            media[0x3f800:] == physical[0x3f800:], "Actual full flash/old NV/lock neighbor changed")


def outcome_negatives(state, symbols, debug, call, values, physical):
    ranges = [set(), set(range(0x7d, 256)) | {0x1e, 0x1f}, {1, 0x1f, 0x47},
              {0, 0x2000, 0x424a, 0x4270, 0x4276, 0x4277, 0x5000, 0x5fff},
              {0, 0x8000, 0x10000, 0x3e7ff, 0x3f800, 0x3ffff} | set(range(0x3e800, 0x3f800))]
    for name in values.keys() | {"metadata"}:
        a = symbols["_fixture_"+name]
        n = 37 if name == "metadata" else len(values[name])
        ranges[0].update(range(a, a+n))
    for _, a, size in wiped_regions(debug):
        ranges[0].update(range(a, a+size))
    ranges[0].update(range(symbols["l_XSEG"], 0x1f00))
    count = 0
    for domain, addresses in enumerate(ranges):
        for a in sorted(addresses):
            altered = list(state); data = bytearray(altered[domain]); data[a] ^= 1; altered[domain] = bytes(data)
            try:
                outcome(altered, symbols, debug, call, values, physical)
            except ValueError:
                count += 1
            else:
                raise ValueError(f"Altered banked key outcome passed: {domain}:{a:x}")
    return count


def run(output, simulator, artifacts=None, reference_calls=None, *, failure=False):
    artifacts = layout.load(output) if artifacts is None else artifacts
    image, symbols, debug_raw, _, listings, _ = artifacts
    debug = debug_raw.decode("ascii")
    sites, code = aes.aes_sites(listings["aes"].decode("ascii"))
    reference_calls = reference(output/"host-banked-security-vectors") if reference_calls is None else reference_calls
    if failure:
        reference_calls = reference_calls[:3]
    physical = bytearray(b"\xff"*0x40000)
    for address, byte in banking.pack(image).items():
        physical[address] = byte
    physical = bytes(physical)
    carry, media, peak = None, b"\xff"*4096, 0
    negatives = 0
    before, after = (symbols["_banked_security_"+n] for n in ("before", "after"))
    for index, call in enumerate(reference_calls):
        commands = banking.model(image)+[aes.AES_ALIAS]
        if call["reset"] is not None:
            carry = None
            if call["reset"]:
                media = b"\xff"*4096
        commands += store("flash", 0x3e800, media)
        if carry is None:
            commands += ["fill xram 0 0x1eff 0xa5", "fill xram 0x2000 0x7fff 0x69",
                         f"run 0 {symbols['_main']:#x}", "fill iram 0x7d 0xff 0xc7"]
            commands += [f"set memory sfr {r:#x} {v:#x}" for r, v in INITIAL.items()]
            commands += ["set memory xram 0x624a 0xa5", "set memory xram 0x6276 0x44 0xff",
                         "set memory xram 0x6270 4", f"run {symbols['_main']:#x} {before:#x}"]
        else:
            commands += restore(carry, after)+capture(58000)
            commands += ["step 1", f"run {after+1:#x} {before:#x}"]
        values = {
            "config": CONFIG, "install": INSTALL, "limits": b"\xe8\x03\0\0\x80\0",
            "polls": b"\x03\0", "address": b"\x34\x12", "capacity": b"\x74\0",
            "action": bytes((call["action"],)), "length": bytes((call["length"],)),
            "written": b"\xa5", "event": b"\xa5", "packet": call["input_packet"],
            "nwk_sequence": bytes(({4: 7, 5: 9, 7: 11}.get(call["action"], 0),)),
            "aps_counter": bytes(({4: 8, 5: 10}.get(call["action"], 0),)),
            "secure": bytes((call["action"] == 6,)), "frame": call["raw"],
        }
        expected_sfr = INITIAL.copy() if carry is None else {a: carry[2][a-0x80] for a in INITIAL}
        peripheral = {0x624a: 0xa5, 0x6270: 4, 0x6276: 0x44, 0x6277: 0xff}
        peripheral.update({a: 0x69 if carry is None else carry[3][a-0x2000]
                           for a in (0x6271, 0x6272, 0x6273)})
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
            commands += store("xram", symbols["_fixture_"+name], data)
        commands += [f"break {after:#x}", f"break {symbols['_banked_stop']:#x}"]
        replay = Replay(commands, symbols, sites, code, image)
        boundaries, terminal = [], False
        for event_index, (kind, event) in enumerate(call["events"], 1):
            if kind == "aes":
                aes.append_block(commands, symbols, *event, replay.write, replay.arm, replay.dump)
            else:
                terminal = failure and index == 2
                replay.flash(*event, stuck=terminal)
                if terminal:
                    break
            if event_index % 16 == 0:
                boundaries.append((len(commands), replay.current))
        stop = flash.STOP if terminal else after
        replay.at(stop)
        n = replay.serial
        commands += capture(n)
        boundaries.append((len(commands), stop))
        text = simulate_chunks(simulator, image, symbols, commands, boundaries)
        parts = sections(text)
        replay.check(parts)
        if carry is not None:
            require(captured(text, 58000, after) == carry, "Between-call continuation changed complete state")
        state = captured(text, n, stop)
        if terminal:
            ram, iram, sfr, _, contents = state
            require(carry is not None and ram[0x1e00:] == carry[0][0x1e00:] and
                    contents == carry[4], "Retained flash failure published status or changed media")
            require(sfr[0x47] == 0x0a and sfr[0x1f] == 1 and iram[0x1e:0x20] == b"\x01\0" and
                    ram[7] == 7 and ram[0xd1] == 10 and ram[0x199] == 13,
                    "Actual owner/journal/writer/RAM pending failure was not retained")
            for name in ("frame", "packet", "written", "event"):
                a = symbols["_fixture_"+name]
                require(ram[a:a+len(values[name])] == values[name], "Terminal call published output")
            print("Banked key retained busy failure: real RAM stop survives later idle, no publication", flush=True)
            return peak, 0
        outcome(state, symbols, debug, call, values, physical)
        if index == 15:
            negatives = outcome_negatives(state, symbols, debug, call, values, physical)
        media = state[4][0x3e800:0x3f800]
        peak = max(peak, *(int(v, 16) for v in re.findall(
            r"Max value of stack pointer=\s*0x([0-9a-f]+)", text)))
        require(peak <= 0x7c, f"Key operation {index} exceeded stack cap: {peak:02x}")
        carry = state
        print(f"Banked key operation {index}: {len(call['events'])} peripheral events, SP {peak:02x}", flush=True)
    return peak, negatives


def artifact_campaign(artifacts, campaign):
    if campaign == "full":
        count = banking.artifact_negatives(layout.artifact_bytes(*artifacts), layout.PINS)
        require(count == 244699, "Banked key artifact-negative coverage changed")
        return f"{count} artifact negatives"
    require(campaign == "deferred", "Unknown artifact campaign")
    return "artifact corruption explicitly deferred to full tier"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--simulator", default="s51")
    parser.add_argument("--artifact-campaign", choices=("full", "deferred"), default="full")
    args = parser.parse_args()
    artifacts = layout.load(args.output)
    layout.verify(*artifacts)
    banking.verify_files(args.output/"banked-security", "security")
    calls = reference(args.output/"host-banked-security-vectors")
    require(reference(args.output/"host-banked-security-vectors-sanitize") == calls,
            "Sanitized public reference differs")
    campaign = artifact_campaign(artifacts, args.artifact_campaign)
    check_alias(args.simulator)
    peak, negatives = run(args.output, args.simulator, artifacts, calls)
    require((peak, negatives) == (0x7b, 9941), "Banked key peak/negative coverage changed")
    run(args.output, args.simulator, artifacts, calls, failure=True)
    print(f"Banked security: 22 real lifecycle operations, {campaign}, "
          f"{negatives} outcome negatives, peak SP {peak:02x}/7c; synthetic, never flash.")


if __name__ == "__main__":
    main()
