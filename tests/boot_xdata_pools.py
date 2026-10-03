#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Repeated real-linked near/far calls across distinct pool/ABI shapes; no RF."""
import argparse
import json
from pathlib import Path
import re
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from boot_banked_join import model
from boot_banked_security import store
from boot_image import check_alias, check_pc, marker, memory_dump, simulate_binary_dumps
from boot_mac_link_child_abi import full_locations
from boot_radio_tx_fixture import sections
from join_smoke_analysis import call_graph
from join_smoke_image import CallerSchema, identities, structure
from verify_banked_join import Field, Schema
from verify_firmware import require
from xdata_multipool import check_identities

ROOT = Path(__file__).resolve().parents[1]


class ModuleSchema(Schema):
    def __init__(self, debug, module):
        self.body = debug.split("M:" + module + "\n", 1)[1].split("\nM:", 1)[0]
        self.module = module
        self.types, self.globals = {}, {}
        pattern = r"\(\{(\d+)\}S:S\$(\w+)\$0_0\$0\(\{(\d+)\}([^)]*)\),Z,0,0\)"
        for name, text in re.findall(r"^T:F" + module + r"\$(__\d+)\[(.*)\]$", self.body, re.M):
            require(re.sub(pattern, "", text) == "", "Unsupported directed-test CDB type")
            self.types[name] = tuple(Field(int(o), n, int(s), t) for o, n, s, t in re.findall(pattern, text))

    def context(self, owner, parameter):
        shapes = re.findall(r"^S:L" + re.escape(self.module + "." + owner + "$" + parameter) +
                            r"\$[^(]+\(\{\d+\}D[GX],ST(__\d+):S\)", self.body, re.M)
        require(len(shapes) == 1, f"Ambiguous real context type: {self.module}.{owner}")
        fields = self.types[shapes[0]]
        size = max(f.offset + f.size for f in fields)
        self.globals["context"] = Field(0, "context", size, "ST" + shapes[0] + ":S")
        return size

    def set(self, raw, field, value):
        f = self.at("context", field)
        raw[f.offset:f.offset+f.size] = value.to_bytes(f.size, "little")


def execute(root, simulator, repeats, *, production=False, baseline=False):
    if production:
        from xdata_build import admit, canonical
        root = canonical(root)
        artifacts, proof = admit(root, "lg_esl29_rev03", "default-tc")
    else:
        catalog = json.loads((ROOT / "experiments/xdata/multipool/identities.json").read_bytes())
        check_identities(identities(root), catalog["baseline" if baseline else "overlay"], "Directed image")
        artifacts, proof = structure(root)
    image, symbols, debug, listings, _ = artifacts
    graph = call_graph(*artifacts)
    entries = {(module, owner): (entry, far) for entry, (module, owner, far) in graph[3].items()}
    locations = full_locations(debug)
    plans = json.loads((ROOT / "experiments/xdata/multipool/pools.json").read_bytes()) if baseline else \
        json.loads((root / "xdata-overlay.json").read_bytes())["plans"]
    pool_owner = {owner: p["id"] for p in plans for owner in p["owners"]}
    harness = 0x7ff0
    require(not set(range(harness, 0x8000)) & image.keys(), "Directed caller overlaps firmware CODE")
    context_address = symbols["_join_smoke_device"]
    context_size = CallerSchema(debug).globals["join_smoke_device"].size
    scratch = context_address
    observations, exercised = [], {}
    with tempfile.TemporaryDirectory(prefix="xdata-pools-") as directory:
        commands = model(image, Path(directory))
        commands += ["fill iram 0 0xff 0", "fill iram 0x7d 0xff 0xc7", "fill xram 0 0x1eff 0",
                     "set memory sfr 0xa8 0", "set memory sfr 0xb8 0", "set memory sfr 0x9a 0",
                     "set memory sfr 0xc7 0", "set memory sfr 0x9f 0", "set memory sfr 0xd0 0",
                     "set memory sfr 0x81 0x4f"]
        if not baseline:
            for p in plans:
                starts = {locations[key]-offset for key, offset in p["offsets"].items()}
                require(len(starts) == 1, "Actual CDB homes disagree on their physical pool")
                start = starts.pop()
                commands.append(f"fill xram {start:#x} {start+p['width']-1:#x} 0xa5")

        def call(module, owner, first, parameters, expected_result, result_size=1, *, checks=()):
            qualified = module + "." + owner
            require(qualified in pool_owner, f"Directed owner is not pooled: {qualified}")
            entry, far = entries[module, owner]
            first = first.to_bytes(4, "little")
            for address, octet in zip((0x82, 0x83, 0xf0, 0xe0), first):
                commands.extend(store("sfr", address, bytes((octet,))))
            schema = ModuleSchema(debug, module)
            for number, value in enumerate(parameters, 2):
                label = "_" + owner + "_PARM_" + str(number)
                addresses = re.findall(r"^\s*([0-9A-F]{6})\s+\d+\s+" + re.escape(label) +
                                       r"::?\s*$", listings[module].decode("ascii"), re.M)
                require(len(addresses) == 1, f"Missing actual parameter: {qualified}/{number}")
                address = int(addresses[0], 16)
                widths = [int(width) for key, width in re.findall(
                    r"^S:(L" + re.escape(module + "." + owner) + r"\$[^(]+)\(\{(\d+)\}.*\),F,0,0$",
                    schema.body, re.M) if locations.get(key) == address]
                require(len(widths) == 1, "Ambiguous directed parameter width")
                commands.extend(store("xram", address, value.to_bytes(widths[0], "little")))
            if far:
                raw = bytes((0x78, entry & 255, 0x79, (entry >> 8) & 255, 0x7a, entry >> 16, 0x12))
                raw += symbols["__sdcc_banked_call"].to_bytes(2, "big") + b"\x80\xfe"
            else:
                raw = bytes((0x75, 0x9f, entry >> 16, 0x12))
                raw += (entry & 0xffff).to_bytes(2, "big") + b"\x75\x9f\0\x80\xfe"
            require(len(raw) == 11, "Unexpected directed call ABI")
            commands.extend(store("flash", harness, raw))
            n = 10 + len(observations)*3
            commands.extend([f"run {harness:#x} {harness+9:#x}", marker(n), "state",
                             "dump /h sfr 0x81 0x83", "dump /h sfr 0x9f 0x9f",
                             "dump /h sfr 0xe0 0xe0", "dump /h sfr 0xf0 0xf0",
                             "dump /h iram 0x1e 0x1f"])
            for address, data in checks:
                commands.append(f"dump /h xram {address:#x} {address+len(data)-1:#x}")
            commands.extend([marker(n+1), "dump /h iram 0x7d 0xff", marker(n+2)])
            observations.append((qualified, expected_result, result_size, checks))
            exercised[qualified] = dict(pool=pool_owner[qualified], entry=entry,
                                        call_abi="banked" if far else "near")

        for cycle in range(repeats):
            for module, owner in (("mac_tx", "reached"), ("bdb_join", "expired"),
                                  ("zdo_runtime", "expired"), ("mac_adapter", "reached")):
                for now, at in ((cycle, cycle), (0, 0xffffffff), (0xffffffff, 0), (0, 0x80000000)):
                    call(module, owner, now, [at], int((now-at) & 0xffffffff < 0x80000000))
            raw = bytes(((cycle+i*17) & 255 for i in range(16)))
            commands.extend(store("xram", scratch, raw))
            call("security_keys", "u16", scratch, [], int.from_bytes(raw[:2], "little"), 2,
                 checks=((scratch, raw),))
            call("security_keys", "u32", scratch, [], int.from_bytes(raw[:4], "little"), 4,
                 checks=((scratch, raw),))
            value = (0xabcdef01 + cycle) & 0xffffffff
            raw = value.to_bytes(4, "little") + raw[4:]
            call("security_keys", "put32", scratch, [value], None, checks=((scratch, raw),))
            call("security_keys", "same", scratch, [scratch, 8], 1, checks=((scratch, raw),))
            call("security_keys", "same", scratch, [scratch+8, 8], 0, checks=((scratch, raw),))
            call("security_keys", "identity", scratch, [], 1, checks=((scratch, raw),))
            for byte in (0, 255):
                raw = bytes((byte,))*16
                commands.extend(store("xram", scratch, raw))
                call("security_keys", "all", scratch, [8, byte], 1, checks=((scratch, raw),))
                call("security_keys", "identity", scratch, [], 0, checks=((scratch, raw),))
            for module, owner, parameter, version, clock in (
                    ("bdb_join", "bdb_join_advance", "ctx", 3, 3),
                    ("zdo_runtime", "advance", "context", 2, 11)):
                schema = ModuleSchema(debug, module)
                size = schema.context(owner, parameter)
                require(size <= context_size, "Directed context exceeds backing")
                for previous, now, accepted in ((cycle, cycle+1, True), (100, 99, False),
                                                 (0xfffffff0, 0x10, True), (0, 0x80000000, False)):
                    before = bytearray(b"\x69"*size)
                    schema.set(before, "version", version)
                    schema.set(before, "last", previous)
                    commands.extend(store("xram", context_address, before))
                    after = bytearray(before)
                    if accepted:
                        schema.set(after, "last", now)
                    call(module, owner, context_address, [now], 0 if accepted else clock,
                         checks=((context_address, bytes(after)),))
            schema = ModuleSchema(debug, "mac_tx")
            size = schema.context("mac_tx_init", "tx")
            before, after = bytes((0x69,))*size, bytearray(size)
            schema.set(after, "next_dsn", cycle)
            schema.set(after, "last", 0xffff0000 + cycle)
            schema.set(after, "ready_at", 0xffff0000 + cycle)
            commands.extend(store("xram", context_address, before))
            call("mac_tx", "mac_tx_init", context_address, [cycle, 0xffff0000+cycle], 0,
                 checks=((context_address, bytes(after)),))
            call("mac_adapter", "storage", symbols["_mac_adapter_reserved_end"], [1], 6)
            call("mac_adapter", "storage", context_address, [size], 0)
            call("mac_adapter", "bounds", 0, [1], 4)
            call("mac_adapter", "bounds", 100, [1], 0)
        require(len(observations)*3+12 < 65536, "Directed scenario exceeds marker space")
        parts = sections(simulate_binary_dumps(simulator, commands))
    for index, (owner, expected, width, checks) in enumerate(observations):
        text = parts[10+3*index]
        check_pc(text, harness+9)
        require(memory_dump(text, 0x81, 1) == b"\x4f" and memory_dump(text, 0x9f, 1) == b"\0" and
                memory_dump(text, 0x1e, 2) == b"\0\0" and
                memory_dump(parts[11+3*index], 0x7d, 131) == b"\xc7"*131,
                owner + ": stack/banker/alias violation")
        returned = (memory_dump(text, 0x82, 2) + memory_dump(text, 0xf0, 1) +
                    memory_dump(text, 0xe0, 1))
        if expected is not None:
            require(returned[:width] == expected.to_bytes(width, "little"), owner + ": result differs")
        for address, wanted in checks:
            require(memory_dump(text, address, len(wanted)) == wanted, owner + ": complete object differs")
    return dict(calls=len(observations), repeats=repeats, owners=exercised,
                pools=sorted({x["pool"] for x in exercised.values()}), identities=identities(root),
                ordinary=symbols["l_XSEG"], simulated=True, hardware_observed=False,
                scope="Sequential shared-home reuse, nested identity/all, generic and XDATA pointers, "
                      "32-bit returns/wrap/half-range, whole BDB/ZDO context preservation, MAC init, "
                      "actual relocated adapter fences. No RF or low-level AES/flash execution here.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--layout", type=Path, required=True)
    parser.add_argument("--simulator", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--repeats", type=int, default=16)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--baseline", action="store_true")
    mode.add_argument("--xdata-overlay", action="store_true")
    args = parser.parse_args()
    require(1 <= args.repeats <= 64, "Directed repeat bound must be 1..64")
    args.output.unlink(missing_ok=True)
    check_alias(args.simulator)
    result = execute(args.layout, args.simulator, args.repeats,
                     production=args.xdata_overlay, baseline=args.baseline)
    args.output.write_text(json.dumps(result, indent=2)+"\n", encoding="ascii")
    print(json.dumps(result))


if __name__ == "__main__":
    main()
