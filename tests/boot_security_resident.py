#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Source-identical resident crypto/NV DATA profile; synthetic, never flash."""
import argparse
import json
from pathlib import Path
import re

import boot_security_counter as counter
import boot_zigbee_key_hash as keyed
import boot_zigbee_mmo as mmo
import boot_zigbee_security as aes
from boot_image import check_alias, verify_component_layout
from boot_nwk_candidates import records
from boot_zdo_node import rejected
from verify_firmware import cdb_address, code_bytes, parse_ihex, parse_symbols, require

MODULES = ("flash_exec", "flash", "flash_write", "nv_record", "security_counter",
           "timebase", "aes", "ccm_star", "nwk_frame", "aps_frame", "zigbee_security",
           "zigbee_mmo", "zigbee_key_hash")
RESERVATIONS = ("security_iram_low", "security_iram_high")
AREAS = dict(zip(MODULES, (
    ("SR_EXEC", 8), ("SR_READ", 16), ("SR_WRITE", 20), ("SR_NV", 34),
    ("SR_COUNTER", 57), ("SR_TIME", 8), ("SR_AES", 34), ("SR_CCM", 8),
    ("SR_NWK", 34), ("SR_APS", 34), ("SR_SECURITY", 8), ("SR_MMO", 8), ("SR_HASH", 66))))
OBJECTS = counter.OBJECTS | aes.OBJECTS | mmo.OBJECTS | keyed.OBJECTS
RESERVATION_SHA = (
    "86f19238fd20241b40f25d1e7c2bb341c0aa5d6f9d471f4ff1a56940230d1feb",
    "3cd06fb264f02be0947c0894be74fc9c6c7311ccc25d92e6f78c04511df50577")
# CODE, raw CDB, complete map, memory report, ordered complete listing manifest.
PINS = {
    "security": (31786, 3101, "security_test", "security", (
        "0714631a29bba51b4d631e3b4180f8ba186d51d199673db59bd478f6fea8afb0",
        "cb12b06ca5a712570bffccee2a268b71e084f09fa09cedfe3e7c2afd94c58978",
        "cc86ac02aa5310e25adfc05d8f0d37bfa466f18b623306222193b60bd32b4066",
        "52377d09714c4e1d81d869cc6afbe9565fc782c748813c26c61556a567515244",
        "1e82cc94ea40ab24d264dec43c7096f88c6603dc4a2ece0b6dda58f0a9b56931")),
    "mmo": (29421, 2367, "mmo_test", "mmo", (
        "00d5c02a5f6ca7ea20cbd2a1c0918e2e426041b60091f0dec4259d36bc8828d0",
        "a14a10d0a3d82b1cd142df5519124fad341b043ba0353e367d35bdd4dc15add9",
        "25dab34bf2d3974f4b2e38a0d9b30b52347b260cbeaf9b86b51e9122ea1afbd5",
        "614099bc6dfcaaac2a0aa4de334614a9cc412632260e7988c2165fc7b2e2d9d7",
        "ec4b2e1f6da408824650d8d81f9c56a69feaf4315e72540d7c993097692e0ba8")),
    "key_hash": (29775, 2370, "key_hash_test", "kh", (
        "b9a1de768402da38018cd7618d0286233325ff000214ecfff9fcb3c6ad6abb75",
        "c842b3f16657a9edb641cb570e88b6b8b08e73e1a9a1ebb677d5590138072fa9",
        "3939101d1437467a325a6e773982f524e51fc818f7f2cc4b7c541f015d8740e7",
        "e65ecc8ee00513b44609661147b75f3ddfe6a6764d081f4f79b768653675ec22",
        "d94081e9f5a5ec7ab27dbd9a4ca5c6cdc2217b645a8c686437f9f880988cca27")),
    "counter": (28231, 2425, "security_counter_test", "counter_test", (
        "32ef4b50ad6d459ac5b4b07ab7ab0e2e412582fe20bf95ffe0bb64a863857f0b",
        "195e22267c9c535ee692672d4927eac00b7123814e31c6305c15f0a9b8f984b6",
        "fe1eb1ddefd634152f3ed6a0ef386d7cb366aaded7a7ad13481b4ba18246ad17",
        "81d70a3b867128aa5f52ba4b9053ddc80f0a2cffb0a8c4f9e485bf351eebf6f0",
        "3fc9cf71accbc372e8e7519884ef911ea9d6e7fb769d08c4203433faf30b1da8")),
}
LENGTHS = tuple(int(n) for row in (
    "1231121111111111", "3231121111111111", "3211221111111111", "3211221111111111",
    "2223221111111111", "2223221111111111", "2223221111111111", "2221232222222222",
    "2221132222222222", "3221221111111111", "2221112222222222", "2221333333333333",
    "2221121111111111", "2221131122222222", "1211121111111111", "1211121111111111") for n in row)
LIBRARY = ("___memcpy", "_memset", "__gptrput", "__gptrget", "_memcmp",
           "__modsint", "__moduint", "__divsint", "__divuint")
EDGES = {
    ("flash_write", "flash"), ("flash_write", "flash_exec"), ("nv_record", "flash"),
    ("nv_record", "flash_write"), ("security_counter", "nv_record"),
    ("aes", "timebase"), ("ccm_star", "aes"), ("zigbee_security", "nwk_frame"),
    ("zigbee_security", "aps_frame"), ("zigbee_security", "ccm_star"),
    ("zigbee_mmo", "aes"), ("zigbee_key_hash", "zigbee_mmo"),
}
PEAKS = {name: tuple(p+0x51-module.STACK for p in module.PEAKS)
         for name, module in (("security", aes), ("mmo", mmo), ("key_hash", keyed))}
NEGATIVES = {"security": 99456, "mmo": 93116, "key_hash": 92761, "counter": 89734}
INACTIVE = {
    "security": ((0, 990), (2071, 2275)),
    "mmo": ((0, 990), (1197, 2071), (2198, 2275)),
    "key_hash": ((0, 990), (1197, 2071)),
    "counter": ((990, 2275), (2405, 2425)),
}
_pinned_cdb = {}


def pin_cdb(raw, digest):
    require(isinstance(raw, bytes), "Resident CDB must be immutable raw bytes")
    # Only successfully pinned immutable objects bypass repeat hashing, never mutants.
    if _pinned_cdb.get(digest) is not raw:
        require(aes.sha(raw) == digest, "Resident raw CDB changed before decode")
        _pinned_cdb[digest] = raw


def load(output, kind):
    p = output / f"resident_{kind}.ihx"
    modules = RESERVATIONS + MODULES + (PINS[kind][2],)
    return (p, parse_ihex(p.read_text()), parse_symbols(p.with_suffix(".map").read_text()),
            p.with_suffix(".cdb").read_bytes(), p.with_suffix(".mem").read_bytes(),
            {m: (output / f"resident_{kind}.{m}.rst").read_bytes() for m in modules},
            {m: (output / "resident" / f"{m}.rel").read_bytes() for m in modules})


def branch(pc, raw):
    op = raw[0]
    require(op != 0xa5 and len(raw) == LENGTHS[op], "Invalid complete MCS-51 instruction")
    if op in (2, 0x12):
        return int.from_bytes(raw[1:], "big")
    if op & 31 in (1, 17):
        return ((pc+2) & 0xf800) | ((op & 0xe0) << 3) | raw[1]
    if op in (0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0xd5) or \
            0xb4 <= op <= 0xbf or 0xd8 <= op <= 0xdf:
        return pc + len(raw) + int.from_bytes(raw[-1:], "big", signed=True)
    return None


def active_frames(edges, frames):
    def visit(module, ancestors):
        require(module not in ancestors, "Resident inter-module recursion")
        a, n = frames[module]
        for parent in ancestors:
            b, size = frames[parent]
            require(not (set(range(a, a+n)) & set(range(b, b+size))),
                    "Simultaneously active resident DATA frames overlap")
        for source, target in edges:
            if source == module:
                visit(target, ancestors+(module,))
    for module in frames:
        visit(module, ())


def call_graph(image, symbols, decoded, owners, caller, frames):
    start = min(symbols[n] for n in LIBRARY if n in symbols)
    end = symbols["s_CSEG"] + symbols["l_CSEG"]
    library_entries = {symbols[n] for n in LIBRARY if n in symbols}
    pc = start
    while pc < end:
        size = LENGTHS[image[pc]]
        require(pc+size <= end and not set(range(pc, pc+size)) & owners.keys(),
                "Resident libc overlaps source instructions")
        raw = bytes(image[a] for a in range(pc, pc+size))
        op = raw[0]
        direct = ([1, 2] if op == 0x85 else [1] if
                  op in (5, 0x15, 0x25, 0x35, 0x42, 0x43, 0x45, 0x52, 0x53, 0x55,
                         0x62, 0x63, 0x65, 0x75, 0x95, 0xb5, 0xc0, 0xc5, 0xd0, 0xd5, 0xe5, 0xf5)
                  or 0x86 <= op <= 0x8f or 0xa6 <= op <= 0xaf else [])
        require(all(raw[i] >= 0x80 or raw[i] < 8 or 0x47 <= raw[i] <= 0x50 for i in direct),
                "Libc direct access reaches reused DATA")
        if op in (0x10, 0x20, 0x30, 0x72, 0x82, 0x92, 0xa0, 0xa2, 0xb0, 0xb2, 0xc2, 0xd2):
            require(raw[1] >= 0x80 or raw[1] < symbols["l_BSEG"], "Libc bit access escapes reserved bits")
        decoded[pc], owners[pc] = raw, "libc"
        pc += size
    edges, indirect = set(), []
    for pc, raw in decoded.items():
        target = branch(pc, raw)
        source = owners[pc]
        if raw[0] in (0x73, 0x32):
            indirect.append((pc, raw))
        if target is None:
            continue
        if pc == 0:
            require(target == symbols["__sdcc_gsinit_startup"], "Resident reset path changed")
            continue
        require(target in owners, "Resident transfer target lacks an instruction owner")
        dest = owners[target]
        if source == "libc":
            require(dest == "libc", "Resident libc acquired a callback")
        elif dest == "libc":
            require(target in library_entries, "Resident enters an unreviewed libc helper")
        elif source != dest:
            edges.add((source, dest))
    expected_indirect = [(0xe5, b"\x73")]
    if caller == "security_counter_test":
        pc = symbols["_counter_before"]+19
        dispatch = bytes(image[a] for a in range(pc-18, pc+1))
        require(dispatch[:3] == b"\x90"+symbols["_counter_test_action"].to_bytes(2, "big") and
                dispatch[3:10] == bytes.fromhex("e0ff24fc500302") and
                owners.get(int.from_bytes(dispatch[10:12], "big")) == caller and
                dispatch[12:] == bytes.fromhex("ef2f2f90")+(pc+1).to_bytes(2, "big")+b"\x73",
                "Counter switch no longer bounds the table index to0..3")
        require(all(decoded[pc+1+i*3][0] == 2 and
                    owners.get(branch(pc+1+i*3, decoded[pc+1+i*3])) == caller for i in range(4)),
                "Counter switch table escapes the current caller")
        expected_indirect.append((pc, b"\x73"))
    require(indirect == expected_indirect and
            bytes(image[a] for a in range(0xdd, 0xe6)) == bytes.fromhex("7a007b00908009e473"),
            "Resident indirect transfer differs from the real fixed RAM engine")
    expected = EDGES | ({(caller, "zigbee_security"), (caller, "ccm_star")} if caller == "security_test" else
                        {(caller, "zigbee_mmo")} if caller == "mmo_test" else
                        {(caller, "zigbee_key_hash")} if caller == "key_hash_test" else
                        {(caller, "security_counter")})
    require(edges == expected, f"Resident graph changed: added {sorted(edges-expected)}, removed {sorted(expected-edges)}")
    active_frames(edges, frames)
    return edges


def verify(kind, image, symbols, debug_raw, memory, listings, objects):
    size, xdata, caller, prefix, pins = PINS[kind]
    pin_cdb(debug_raw, pins[1])
    require(aes.sha(code_bytes(image, size)) == pins[0], "Resident complete CODE changed")
    require(aes.sha(json.dumps(symbols, sort_keys=True, separators=(",", ":")).encode()) == pins[2],
            "Resident complete map changed")
    require(aes.sha(memory) == pins[3], "Resident complete memory report changed")
    modules = RESERVATIONS + MODULES + (caller,)
    require(set(listings) == set(objects) == set(modules), "Resident complete composition changed")
    require(aes.sha(b"".join(m.encode()+b"\0"+listings[m] for m in modules)) == pins[4],
            "Resident immediate ordered complete listing manifest changed")
    debug = debug_raw.decode("ascii")
    source = {"security": "zigbee_security", "mmo": "zigbee_mmo", "key_hash": "zigbee_key_hash",
              "counter": "security_counter"}[kind]
    allocated = verify_component_layout(image, symbols, debug, memory.decode("ascii"), prefix+"_result",
        tuple(m+".c" for m in MODULES)+("test_"+source+".c",), xdata_budget=3200)
    require((symbols["s_XSEG"], symbols["l_XSEG"], symbols["s_SSEG"], symbols["s_OSEG"], symbols["l_OSEG"],
             symbols["s_BSEG_BYTES"], symbols["l_BSEG_BYTES"]) == (0, xdata, 0x51, 0x47, 10, 0x20, 2),
            "Resident physical XDATA/IRAM/bit/overlay/stack allocation changed")
    require(not any(n.startswith(("_host_", "_aes_reference")) for n in symbols), "Host model entered resident CODE")
    retained = re.findall(r"^S:[FG].*\),E,.*$", debug, re.M)
    require(retained == [f"S:G${m}$0_0$0({{{n}}}DA{n}d,SC:U),E,0,0"
                         for m, n in zip(RESERVATIONS, (24, 37))], "Retained DATA entered the reused frames")
    require(all(line.endswith(",0,0,0,0,0") for line in debug.splitlines() if line.startswith("F:")),
            "Resident function gained bank/interrupt/reentrant attributes")
    for m, address, digest in zip(RESERVATIONS, (8, 34), RESERVATION_SHA):
        require(symbols["_"+m] == address and aes.sha(objects[m].split(b"\n", 1)[1]) == digest,
                "Real physical reservation object changed")
    decoded, owners, covered, storage, frames, offset = {}, {}, set(), set(), {}, 0
    for m in MODULES+(caller,):
        area, base = AREAS.get(m, ("SR_CALLER", 26))
        expected_object, extents = OBJECTS[m]
        raw = objects[m]
        require(raw.startswith(b";!FILE ") and raw.count(f"A {area} size ".encode()) == 1,
                "Missing/duplicate resident named DATA area")
        normalized = raw.split(b"\n", 1)[1].replace(f"A {area} size ".encode(), b"A DSEG size ")
        require(aes.sha(normalized) == expected_object,
                "Resident object differs from accepted ABI/code beyond its DATA area name")
        require((symbols["s_"+area], symbols["l_"+area]) == (base, extents[2]),
                "Resident named DATA base/size changed")
        require(set(range(base, base+extents[2])) <= set(range(8, 32)) | set(range(34, 71)),
                "Resident DATA frame escapes the actual physical reservations")
        frames[m] = (base, extents[2])
        text = listings[m].decode("ascii")
        for a, raw in records(text):
            span = set(range(a, a+len(raw)))
            require(not span & covered and bytes(image.get(a+i, 0) for i in range(len(raw))) == raw,
                    "Resident instructions overlap or differ from linked CODE")
            covered |= span
            decoded[a], owners[a] = raw, m
        segment = text.split(".area XSEG    (XDATA)", 1)[1].split(".area XABS", 1)[0]
        owned = set()
        for address, length in re.findall(r"^\s+([0-9A-F]{6})\s+\d+\s+\.ds (\d+)$", segment, re.M):
            span = set(range(int(address, 16), int(address, 16)+int(length)))
            require(span and not span & storage, "Resident private/compiler XDATA overlap")
            owned |= span; storage |= span
        require(owned == set(range(offset, offset+extents[1])), "Resident complete source XDATA span changed")
        offset += extents[1]
    require(offset == 2275+OBJECTS[caller][1][1] and
            min(symbols[n+"_PARM_2"] for n in LIBRARY if n+"_PARM_2" in symbols) == offset,
            "Resident complete caller/libc scratch boundary changed")
    for key, module, length in re.findall(r"^S:(L([^.$]+)\.[^(\n]+)\(\{(\d+)\}[^)\n]*\),E,0,0$", debug, re.M):
        address = cdb_address(debug, "L:"+key)
        base, count = frames[caller if module == "test_"+source else module]
        require("$sloc" in key and set(range(address, address+int(length))) <= set(range(base, base+count)),
                "Resident compiler DATA object escapes its complete module frame")
    for start, end, digest in counter.FLASH_MODULES.values():
        require(aes.sha(bytes(image[a] for a in range(start, end))) == digest,
                "Resident changed the real retained-RAM/flash backend")
    require(all(symbols[n] == value for n, value in counter.ABI.items()
                if n.startswith("_security_counter_")), "Resident lower counter ABI/fence changed")
    require([(pc, int.from_bytes(raw[1:], "big")) for pc, raw in decoded.items()
             if 0xc3f <= pc < 0x1aad and raw[0] == 0x12 and int.from_bytes(raw[1:], "big") < 0xc3f] ==
            [(counter.journal.READ_CALL, 0x5e0), (counter.journal.PROGRAM_CALL, 0xbf7), (0x1788, 0xbc6)],
            "Resident journal no longer reaches actual flash calls at the replay boundaries")
    edges = call_graph(image, symbols, decoded, owners, caller, frames)
    sites, code = aes.aes_sites(listings["aes"].decode("ascii"))
    for name in ("input", "output"):
        a = sites[name]
        require(bytes(image[a+i] for i in range(13)) ==
                bytes((0x75, 0xd6, 1 if name == "input" else 2))+bytes(9)+b"\x22",
                "Resident AES nine-clock arm path changed")
    return allocated, sites, code, edges, frames


class SecurityClient:
    prefix = "security"

    def __init__(self):
        self.bad = 0

    @staticmethod
    def after_blocks(*args):
        pass

    def check(self, text, ram, iram, sfr, symbols, debug, expected, number):
        aes.check_result(ram, iram, sfr, symbols, debug, expected)
        if number == 0:
            self.bad = aes.snapshot_negatives(ram, iram, sfr, symbols, debug, expected)
        return aes.check_peak(text, PEAKS["security"][number])


def inactive(kind, ram):
    require(all(ram[a:b] == bytes(b-a) for a, b in INACTIVE[kind]),
            "Resident call modified another inactive service or unused libc state")


def inactive_negatives(kind, ram):
    return aes.corrupt_snapshots(lambda **v: inactive(kind, v["ram"]), ram, b"", b"",
                                (("ram", a) for start, end in INACTIVE[kind] for a in range(start, end)))


class CryptoClient:
    def __init__(self, kind, client):
        self.kind, self.client, self.prefix, self.bad = kind, client, client.prefix, 0

    def after_blocks(self, *args):
        self.client.after_blocks(*args)

    def check(self, text, ram, iram, sfr, symbols, debug, expected, number):
        inactive(self.kind, ram)
        if number == 0:
            self.bad = inactive_negatives(self.kind, ram)
        return self.client.check(text, ram, iram, sfr, symbols, debug, expected, number)


class CounterClient(counter.CounterClient):
    def check(self, options, ram, iram, sfr):
        inactive("counter", ram)
        super().check(options, ram, iram, sfr)


def run_crypto(output, simulator, kind, path, artifacts, sites, code):
    module = {"security": aes, "mmo": mmo, "key_hash": keyed}[kind]
    executable = output / ("host-zigbee-"+kind.replace("_", "-")+"-tests")
    original = SecurityClient() if kind == "security" else module.Client(PEAKS[kind])
    client = CryptoClient(kind, original)
    checks = calls = peak = 0
    for number, expected_peak in enumerate(PEAKS[kind]):
        for text in ("", "Max value of stack pointer= 0x7d", f"Max value of stack pointer= {expected_peak-1:#x}"):
            rejected(lambda: aes.check_peak(text, expected_peak))
        if kind == "security":
            blocks, expected, digest = aes.native_trace(executable, number)
            require(digest == aes.TRACES[number], "Resident complete security native trace changed")
        else:
            blocks, expected = module.native_trace(executable, number)
        observed, count, physical = aes.replay(simulator, path, artifacts[1], artifacts[2].decode("ascii"),
                                              sites, code, blocks, expected, number, client)
        peak = max(peak, observed); checks += count; calls += physical
    bad = original.bad if kind == "security" else original.negatives()
    require((checks, calls, bad) == {"security": (220, 345, 54), "mmo": (117, 58, 8034),
                                   "key_hash": (45, 51, 3735)}[kind], "Resident original crypto corpus changed")
    require(client.bad == sum(b-a for a, b in INACTIVE[kind]), "Resident inactive-byte negatives changed")
    return len(PEAKS[kind]), checks, calls, peak, bad+len(PEAKS[kind])*3+client.bad


def run_counter(simulator, path, artifacts, allocated):
    peak = calls = commands = segments = bad = total = 0
    for number, (media, operations) in enumerate(counter.cases()):
        client = CounterClient(artifacts[1])
        try:
            observed, count, physical = counter.journal.execute(
                simulator, path, artifacts[0], allocated, 0x1788, media, operations, client)
        except (ValueError, KeyError) as exc:
            raise ValueError(f"Resident counter sequence {number}: {exc}") from exc
        require(observed <= 0x7c, "Resident counter crossed original stack cap")
        if number == 0:
            bad = counter.runtime_negatives(client)
            require(inactive_negatives("counter", client.snapshots[0][1]) == 1305,
                    "Resident inactive crypto/libc negatives changed")
        peak = max(peak, observed); calls += count; commands += physical
        segments += len(client.continuations); total += 1
    require((total, calls, commands, segments, peak, bad) == (56, 36695, 919, 162, 0x79, 204),
            "Resident original counter sequence/call/command/continuation/peak/negative coverage changed")
    return total, segments, calls, peak, bad+1305


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--simulator", default="s51")
    parser.add_argument("--profile", choices=tuple(PINS), required=True)
    args = parser.parse_args()
    for pc, raw, target in (
        (0x2000, "123456", 0x3456), (0x2000, "023456", 0x3456),
        (0x1ffe, "41ab", 0x22ab), (0x1ffe, "51ab", 0x22ab),
        (0x2000, "8001", 0x2003), (0x2000, "80fe", 0x2000),
        (0x2000, "1001fc", 0x1fff), (0x2000, "b469fc", 0x1fff),
        (0x2000, "d8fe", 0x2000), (0x2000, "d501fc", 0x1fff), (0x2000, "22", None),
    ):
        require(branch(pc, bytes.fromhex(raw)) == target, "Resident transfer decoder changed")
    for raw in (b"\xa5", b"\x12\0", b"\x85\0"):
        rejected(lambda: branch(0, raw))
    check_alias(args.simulator)
    rejected(lambda: check_alias(args.simulator, False))
    for kind, (size, xdata, caller, _, _) in PINS.items():
        if kind != args.profile:
            continue
        path, *artifacts = load(args.output, kind)
        allocated, sites, code, edges, frames = verify(kind, *artifacts)
        verifier = lambda **values: verify(kind, **values)
        bad = aes.negatives(*artifacts, modules=MODULES+(caller,), verifier=verifier, code_size=size)
        for m in RESERVATIONS:
            values = dict(zip(("image", "symbols", "debug_raw", "memory", "listings", "objects"), artifacts))
            for field in ("listings", "objects"):
                changed = values[field] | {m: values[field][m]+b"\n"}
                rejected(lambda: verifier(**(values | {field: changed})))
                bad += 1
        require(bad == NEGATIVES[kind], "Resident complete artifact-negative coverage changed")
        rejected(lambda: active_frames(edges | {("aes", "security_counter")}, frames))
        rejected(lambda: active_frames(edges | {("timebase", "aes")}, frames))
        rejected(lambda: active_frames(edges, frames | {"timebase": (34, 1)}))
        result = (run_counter(args.simulator, path, artifacts, allocated) if kind == "counter" else
                  run_crypto(args.output, args.simulator, kind, path, artifacts, sites, code))
        total, checks, calls, peak, runtime = result
        print(f"Resident {kind}: {total} cases, {checks} checks/continuations, {calls} genuine AES/flash calls; "
              f"{size}/32768 CODE, {xdata}+64/3200 XDATA, SP {peak:02X}/7C; "
              f"{bad} artifact + {runtime} runtime +3 graph +3 decoder +1 alias negatives PASS.", flush=True)
    print("Resident layout only: original services, not mixed lifecycle, TC verification or network join.")


if __name__ == "__main__":
    main()
