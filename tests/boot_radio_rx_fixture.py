# SPDX-License-Identifier: BSD-3-Clause
"""Actual board instructions, shared host trace replay; no ROM patches or USB."""
import hashlib
import json
import re
import subprocess
import unittest

from boot_image import (
    ALIAS, boot_commands, check_guards, check_pc, marker, memory_dump,
    simulate, snapshot_commands,
)
from boot_radio_fifo_fixture import check_service_listings, sections, snapshot
from boot_radio_rx import check_fscal1_rejections, check_fscal1_trace
from radio_rx_fixture import (
    CHECKPOINTS, DRIVER_HASH, FLAGS, LENGTHS, SETTINGS, SIZE, VALUES, check_frame, decode, instructions, normalized_driver,
    verify_code, verify_driver_listing, verify_fixture,
)
from verify_firmware import cdb_address, parse_ihex, require


def restore(memory):
    ram, iram, sfr = memory
    # C52 SBUF writes start a fictitious serial transfer (not CC2530 RF).
    # This fixture never accesses 99 and its reset byte is zero. Leave it at
    # reset, then compare ALL 128 SFR bytes before executing the continuation.
    require(sfr[0x99-0x80] == 0, "Unexpected synthetic SBUF state")
    commands = ["fill xram 0x6000 0x63ff 0x69"]
    for space, start, data in (("xram", 0, ram), ("iram", 0, iram),
                               ("sfr", 0x80, sfr[:0x19]), ("sfr", 0x9a, sfr[0x1a:])):
        for offset in range(0, len(data), 64):
            commands.append(f"set memory {space} {start+offset:#x} " +
                            " ".join(hex(b) for b in data[offset:offset+64]))
    return commands


def rejections(image, symbols, debug):
    case = unittest.TestCase()
    start = cdb_address(debug, "L:Fradio_rx$ordinary$0$0")
    end = cdb_address(debug, "L:XG$radio_rx_receive_init$0$0")+1
    check_fscal1_rejections(instructions(image, start, end, LENGTHS), start+0x1ab, 0x36,
                           cdb_address(debug, "L:Fradio_rx$values$0_0$0"))
    for address in image:
        changed = dict(image); changed[address] ^= 1
        with case.assertRaisesRegex(ValueError, "instructions"):
            verify_code(changed, symbols[CHECKPOINTS[0]])
    # Test the unchanged-service comparison independently of the whole-image
    # hash: no opcode, branch, constant or relocation operand may disappear.
    for address in range(start, end):
        changed = image | {address: image[address]^1}
        try:
            digest = hashlib.sha256(normalized_driver(changed, symbols, debug)).hexdigest()
        except ValueError:
            continue
        require(digest != DRIVER_HASH, "RX normalized proof ignored a changed service byte")
    for name in (*CHECKPOINTS, "_radio_rx_fixture_frame", "_radio_rx_fixture_diagnostics",
                 "_radio_rx_reserved_end", "__gptrput_PARM_2", "s_SSEG", "l_XSEG"):
        with case.assertRaises(ValueError):
            verify_fixture(image, symbols | {name: symbols[name]+1}, debug)
    for old, new in (("{96}ST", "{95}ST"), ("{31}ST", "{32}ST"), ("{128}ST", "{127}ST"),
                     ("{41}S:S$diagnostic", "{40}S:S$diagnostic"),
                     ("({2}DX,ST", "({3}DG,ST"), ("C$radio_rx.c$", "C$missing.c$")):
        require(old in debug, "RX fixture mutation did not apply")
        with case.assertRaises(ValueError):
            verify_fixture(image, symbols, debug.replace(old, new))


def execution(simulator, path, symbols, proof, v, carry, previous_pc):
    expected = bytes.fromhex(v["state"]); record = decode(expected)
    if record["attempt"]: check_fscal1_trace(v, record["result"])
    before, ready, fault, end = proof["checkpoints"]
    pc_end = {1: before, 3: ready, 4: fault, 5: end}[record["phase"]]
    current = {int(a): n for a, n in v["initial"].items()}
    if v["first"]:
        commands = boot_commands(symbols) + ["fill xram 0x6000 0x63ff 0x69"]
    else:
        require(carry is not None, "RX fixture missing genuine continuation")
        commands = [ALIAS]+restore(carry)+["fill xram 0x6000 0x63ff 0x69",
                                         f"pc {previous_pc:#x}"]+snapshot_commands(1)
    # Only explicit synthetic peripheral inputs; never caller/driver SRAM.
    for address, value in current.items():
        commands.append(f"set memory {'sfr' if address < 256 else 'xram'} {address:#x} {value:#x}")
    sites = proof["sites"]
    commands += [f"break {pc:#x}" for pc in list(sites)+proof["checkpoints"]]
    if not v["first"]: commands.append("step 1")
    for index, (kind, address, value) in enumerate(v["events"]):
        n = 10+3*index
        space = "sfr" if address < 256 else "xram"
        commands += ["run", marker(n), "state", "dump /h sfr 0x81 0x83"]
        if kind == "r": commands.append(f"set memory {space} {address:#x} {value:#x}")
        commands += [marker(n+1), "step 1"]
        if kind == "r":
            commands += ["dump /h sfr 0xe0 0xe0", "dump /h iram 0 7"]
        else:
            commands += [f"dump /h {space} {address:#x} {address:#x}"]
        commands.append(marker(n+2)); current[address] = value
    final = 12+3*len(v["events"])
    commands += ["run"]+snapshot_commands(final)
    commands += [marker(final+4), "dump /h xram 0x6000 0x63ff", marker(final+5)]
    if record["phase"] in (4, 5): commands += ["step 64"]+snapshot_commands(final+6)
    parts = sections(simulate(simulator, commands, path))
    if not v["first"]: require(snapshot(parts, 1) == carry, "RX fixture continuation changed genuine CPU/RAM")
    peak = 0
    for index, (kind, address, value) in enumerate(v["events"]):
        n = 10+3*index
        m = re.search(r"CPU state= OK PC= 0x([0-9a-fA-F]+)", parts[n])
        require(m is not None and int(m[1], 16) in sites, v["name"]+": unplanned MMIO stop")
        actual_kind, actual_address, (space, observed) = sites[int(m[1], 16)]
        require(kind == actual_kind and actual_address in (None, address), "RX fixture MMIO order mismatch")
        regs = memory_dump(parts[n], 0x81, 3); peak = max(peak, regs[0])
        if address >= 256:
            require(regs[1:] == address.to_bytes(2, "little"), "RX fixture actual MOVX address mismatch")
            if actual_address is None: require(address in SETTINGS, "RX fixture indirect MMIO outside whitelist")
        observed = address if observed is None else observed
        require(memory_dump(parts[n+1], observed, 1)[0] == value, "RX fixture MMIO value mismatch")
    check_pc(parts[final], pc_end)
    ram, iram, sfr = final_memory = snapshot(parts, final)
    require(ram[proof["state"]:proof["state"]+SIZE] == expected, v["name"]+": serialized state mismatch")
    raw = ram[proof["frame"]:proof["frame"]+128]
    require(raw == bytes.fromhex(v["frame"]), "RX fixture raw frame/nonpublication/tail mismatch")
    check_frame(record, raw)
    require(ram[0x1e00:0x1e20] == bytes.fromhex(v["boot"]), "RX fixture bootstrap/heartbeat mismatch")
    check_guards(ram, iram[128:], sfr, symbols)
    require(sfr[1] == symbols["s_SSEG"]+1 and peak < 128, "RX fixture SP unwind/upper IRAM failed")
    for a, value in current.items():
        if a < 256: require(sfr[a-128] == value, f"RX fixture unrelated SFR changed: {a:02x}")
    radio = memory_dump(parts[final+4], 0x6000, 1024)
    require(all(value == current.get(a, 0x69) for a, value in enumerate(radio, 0x6000)),
            "RX fixture peripheral RAM changed outside explicit synthetic effects")
    if record["phase"] in (4, 5):
        require(snapshot(parts, final+6) == final_memory, "RX fixture terminal state not retained")
    return final_memory, pc_end, peak


def poll_limit(simulator, path, symbols, proof, carry):
    """Full 65535-poll call using stationary synthetic MMIO, in bounded chunks."""
    # Begin from the actual clock READY snapshot, not a fabricated driver return.
    # The only RF effect is E3 -> RXENABLE80/PLL/RSSI valid; no frame arrives.
    ready, fault = proof["checkpoints"][1:3]
    commands = [ALIAS]+restore(carry)
    # Restore reset radio configuration used before the real first call.
    defaults = {a: 0 for a in range(0x6100, 0x6300)}
    defaults.update({0x624a: 0xa5, 0x6189: 0x40, 0x618a: 1, 0x6180: 13, 0x6182: 7,
                     0x6194: 64, 0x6195: 1, 0x61a8: 0x85, 0x61a9: 0x14,
                     0x61b8: 0x75, 0x61b9: 8})
    for a, value in defaults.items(): commands.append(f"set memory xram {a:#x} {value:#x}")
    commands += ["set memory sfr 0x95 0 0 0", "set memory sfr 0xe9 0", "set memory sfr 0x91 0"]
    # Stop at the real poll entry every 2048 hits. No argument/count patch.
    debug = path.with_suffix(".cdb").read_bytes().decode("utf-8")
    poll = cdb_address(debug, "L:Fradio_rx$poll$0$0")
    e3 = next(pc for pc, (kind, a, _) in proof["sites"].items()
              if kind == "w" and a == 0xe1 and pc < proof["driver_end"]-400)
    count = 0
    for chunk in range(33):
        if chunk:
            commands = [ALIAS]+restore(memory)
            commands += [f"set memory xram {a:#x} {v:#x}" for a, v in enumerate(radio, 0x6000)]
        commands += snapshot_commands(10)
        commands += [f"break {e3+3:#x}",
                     "commands 1 set memory xram 0x618b 0x80; set memory xram 0x6192 0 5; "
                     "set memory xram 0x6199 1; set memory xram 0x61ae 0x30; run",
                     f"break {poll:#x} 2048", f"break {fault:#x}"]
        commands += [f"pc {(ready if not chunk else poll):#x}"]
        if not chunk: commands += ["step 1"]
        commands += ["run"]+snapshot_commands(1)+[marker(5), "dump /h xram 0x6000 0x63ff", marker(6)]
        if chunk == 32: commands += ["step 64"]+snapshot_commands(20)
        parts = sections(simulate(simulator, commands, path))
        initial = snapshot(parts, 10)
        if chunk:
            require(initial == memory, "RX poll continuation changed genuine CPU/RAM")
        else:
            require(initial[:2] == carry[:2] and
                    all(v == carry[2][i] for i, v in enumerate(initial[2])
                        if i+128 not in (0x95, 0x96, 0x97, 0xe9, 0x91)),
                    "RX poll initial carry changed outside explicit timer/RF inputs")
        memory = snapshot(parts, 1)
        radio = memory_dump(parts[5], 0x6000, 1024)
        ram, iram, sfr = memory
        check_guards(ram, iram[128:], sfr, symbols)
        require(sfr[1] < 128, "RX poll chunk stack guard failed")
        expected_radio = defaults | dict(zip(SETTINGS, VALUES[:-1]+b"\x1f")) | {
            0x618b: 128, 0x6192: 0, 0x6193: 5, 0x6199: 1, 0x61ae: 0x30}
        require(all(v == expected_radio.get(a, 0x69) for a, v in enumerate(radio, 0x6000)),
                "RX poll changed radio RAM outside explicit configuration/E3 effects")
        for a in FLAGS+(0x80, 0x90, 0xa0, 0xbe, 0xc6, 0x9e, 0xbf, 0xd9,
                        0x8f, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xfd, 0xfe, 0xff):
            require(sfr[a-128] == carry[2][a-128], "RX poll changed unrelated CPU/peripheral state")
        require(sfr[0xe1-128] == 0xe3 and sfr[0x95-128:0x98-128] == bytes(3),
                "RX poll changed stationary timer or issued an unexpected strobe")
        actual = int.from_bytes(ram[proof["diagnostics"]+4:proof["diagnostics"]+6], "little")
        require(count <= actual <= 65535, "RX poll chunk count regressed/overflowed")
        count = actual
        stopped = re.search(r"CPU state= OK PC= 0x([0-9a-fA-F]+)", parts[1])
        require(stopped is not None, "Missing RX poll CPU stop")
        if int(stopped[1], 16) == fault:
            require(chunk == 32 and actual == 65535, "RX cap terminated before the full bound")
            check_pc(parts[1], fault)
            r = decode(ram[proof["state"]:proof["state"]+SIZE])
            check_frame(r, ram[proof["frame"]:proof["frame"]+128])
            require(r["result"] == r["fault_latch"] == 11 and r["attempt"] == 1 and
                    r["completed"] == 0 and r["diagnostic"]["elapsed_ticks"] == 0 and
                    r["diagnostic"]["actions"] == 1 and sfr[1] == symbols["s_SSEG"]+1,
                    "RX full poll cap not retained at genuine FAULT")
            require(snapshot(parts, 20) == memory, "RX full-cap FAULT did not remain terminal")
            return chunk+1
        check_pc(parts[1], poll)
        require(actual == 2047+chunk*2048, "RX genuine poll continuation skipped work")
    raise ValueError("RX fixture full cap did not terminate")


def check_radio_rx_fixture(simulator, output, board, symbols):
    path = output/"radio_rx_fixture.ihx"
    image = parse_ihex(path.read_text()); debug = path.with_suffix(".cdb").read_bytes().decode("utf-8")
    proof = verify_fixture(image, symbols, debug); rejections(image, symbols, debug)
    # Independent listing check of actual decoded driver instructions.
    listing = (output/"radio_rx_fixture.radio_rx.rst").read_text()
    verify_driver_listing(instructions(image, proof["driver_start"], proof["driver_end"], LENGTHS), listing)
    check_service_listings(output, "radio_rx_fixture", "radio_rx",
                           instructions(image, proof["driver_start"], proof["driver_end"], LENGTHS),
                           image, symbols, debug)
    vectors = [json.loads(line) for line in subprocess.run(
        [str(output/("host-radio-rx-fixture-tests_"+board)), "--vectors"],
        check=True, capture_output=True, text=True).stdout.splitlines()]
    require(len(vectors) == 67, "RX fixture linked scenario count changed")
    carry, pc, peak, clock = None, None, 0, None
    for v in vectors:
        carry, pc, observed = execution(simulator, path, symbols, proof, v, carry, pc)
        peak = max(peak, observed)
        if v["name"] == "minimum" and bytes.fromhex(v["state"])[6:10] == b"\x03\0\x01\0":
            clock = carry
    require(clock is not None, "Missing actual clock READY for cap continuation")
    chunks = poll_limit(simulator, path, symbols, proof, clock)
    print(f"{board}: RX fixture {len(vectors)} linked checkpoints/shared traces, full cap in {chunks} "
          f"bounded continuations; MMIO-stop peak SP={peak:02X}; alias/RAM/unwind PASS (synthetic only).")
