#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Immutable offline admission of the complete caller; never a flashing tool."""
import argparse
import hashlib
import json
from pathlib import Path
import re

from join_smoke_analysis import RUNTIME, analyze_data, call_graph, load
from join_smoke_layout import RUNTIME_OBJECTS
from join_smoke_stack import analyze_stack
from boot_mac_link_child_abi import allocations, full_locations
from verify_banked_join import RUNTIME_XDATA, Field, Schema
from verify_mac_smoke import map_parts
from verify_firmware import cdb_address, require


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def runtime_map_identity(suffix, *, ownership, overlay):
    header = b"                          [ object file ]\n\n"
    require(suffix.startswith(header), "Join runtime table header")
    remaining, rows = suffix[len(header):], []
    expected = [("mcs51.lib", name) for name in
                ("crtclear", "crtxinit", "crtxclear", "gptr_cmp", "crtstart")]
    expected.append(("libsdcc.lib", "_gptrget"))
    for library, member in expected:
        match = re.match(rb"(/\S+\.lib)(?: +\[ (\w+)\.rel \]|\n {42}\[ (\w+)\.rel \])\n", remaining)
        require(match is not None, "Malformed join runtime table row")
        path, module = match[1], match[2] or match[3]
        prefix = path.ljust(42) if len(path) <= 40 else path + b"\n" + b" " * 42
        require(match[0] == prefix + b"[ " + module + b".rel ]\n" and
                Path(path.decode("ascii")).name == library and module.decode("ascii") == member,
                "Changed join runtime library/member/order or row formatting")
        rows.append((Path(path.decode("ascii")), module))
        remaining = remaining[len(match[0]):]
    require(remaining.startswith(b"\n\fASxxxx Linker ") and
            len({p.parent for p, _ in rows}) == 1, "Extra runtime rows or mixed runtime ownership")
    parent = rows[0][0].parent
    # Keep historical identities unchanged; relocate only content-proven runtimes.
    legacy = (Path("/usr/share/sdcc/lib/large"), Path("/usr/bin/../share/sdcc/lib/large"))
    if parent in legacy:
        return suffix
    from xdata_toolchain import load_pins
    wanted = load_pins()["runtime_archives"]
    for path in {p for p, _ in rows}:
        require(path.is_file() and sha(path.read_bytes()) == wanted[path.name],
                "Relocated runtime archive identity changed")
    canonical = legacy[0] if ownership and not overlay else legacy[1]
    result = header
    for path, module in rows:
        name = str(canonical / path.name).encode("ascii")
        result += (name.ljust(42) if len(name) <= 40 else name + b"\n" + b" " * 42)
        result += b"[ " + module + b".rel ]\n"
    return result + remaining


def identities(root):
    report = json.loads((root / "layout.json").read_bytes())
    names = tuple(report["objects"])
    runtime = tuple(m for _, m in RUNTIME_OBJECTS)
    target = root / "join_smoke_unverified"
    mapping = target.with_suffix(".map").read_bytes()
    prefix, suffix = map_parts(mapping.replace(b"/runtime/", b"/"), runtime + names)
    suffix = runtime_map_identity(suffix, ownership=any(root.glob("*.xdata.json")),
                                  overlay=(root / "xdata-overlay.json").is_file())
    noi = target.with_suffix(".noi").read_bytes()
    require(noi.endswith(b"LOAD " + str(target.with_suffix(".ihx")).encode() + b"\n"),
            "Join NoICE load identity")
    return {
        "ihx": sha(target.with_suffix(".ihx").read_bytes()),
        "cdb": sha(target.with_suffix(".cdb").read_bytes()),
        "memory": sha(target.with_suffix(".mem").read_bytes()),
        "map_prefix": sha(prefix), "map_suffix": sha(suffix),
        "noice": sha(noi.rsplit(b"LOAD ", 1)[0]),
        "objects": sha(b"".join(m.encode() + b"\0" + (
            root / (m + ".rel")).read_bytes() for m in names)),
        "listings": sha(b"".join(m.encode() + b"\0" + (
            root / ("join_smoke_unverified." + m + ".rst")).read_bytes() for m in names)),
        "runtime": sha(b"".join(m.encode() + b"\0" + (
            root / "runtime" / (m + ".rel")).read_bytes() for m in runtime)),
    }


def xdata(symbols, debug, listings, objects):
    require(all(symbols[name] == offset for name, offset in RUNTIME_XDATA.items()),
            "Complete libc scratch is not in the closed 26-byte prefix")
    spans, all_allocations, cursor = {}, set(), 26
    for module, raw in listings.items():
        text = raw.decode("ascii")
        require(re.findall(r"\.module (\w+)", text) == [module], "Wrong XDATA listing owner")
        ranges = allocations(text).get("XSEG", [])
        size = re.findall(rb"^A XSEG size ([0-9A-F]+) flags 40 addr 0$", objects[module], re.M)
        require(len(size) == 1 and sum(n for _, n in ranges) == int(size[0], 16),
                "Object and listing XDATA sizes differ")
        owned = set()
        for address, length in ranges:
            span = set(range(address, address + length))
            require(length > 0 and not span & owned, "Overlapping module XDATA")
            owned |= span
            all_allocations.add((address, length))
        require(owned == set(range(cursor, cursor + len(owned))), "XDATA allocation gap/order")
        spans[module] = owned
        cursor += len(owned)
    require(cursor == symbols["l_XSEG"] <= 0x1e00, "Unaccounted ordinary XDATA")
    require(all(symbols["l_" + a] == 0 for a in ("XABS", "XISEG", "XINIT", "PSEG")),
            "Unaccounted absolute/initialized/paged storage")
    locations = full_locations(debug)
    declarations = {}
    for key, size in re.findall(r"^S:([FGL][^(]+)\(\{(\d+)\}.*\),F,0,0$", debug, re.M):
        if key not in locations:
            continue
        pair = locations[key], int(size)
        require(key not in declarations or declarations[key] == pair, "Conflicting XDATA declaration")
        declarations[key] = pair
        require(pair in all_allocations or (key == "G$join_smoke_status$0_0$0" and pair == (0x1e00, 48)),
                "CDB object/parameter home lacks exact allocation: " + key)
    fences = {
        "banked": "_banked_reserved_end",
        "mac_link_child_workspace": "_child_work_reserved_end",
        **{m: "_" + m + "_reserved_end" for m in
           ("flash_exec", "flash", "flash_write", "nv_record", "security_counter", "aes",
            "mac_time", "radio_autoack", "mac_radio", "mac_attempt", "mac_adapter")},
    }
    for module, fence in fences.items():
        require(spans[module] and symbols[fence] == max(spans[module]),
                "Private fence misses parameter/compiler homes: " + module)
    require(symbols["_mac_radio_shared_end"] == min(spans["mac_radio"]),
            "Radio shared lower prefix moved")
    require(max(spans["mac_link_workspace"]) < min(spans["mac_link_child_workspace"])
            and max(spans["mac_link_child_workspace"]) < min(spans["flash_exec"])
            and max(spans["flash_exec"]) < min(spans["timebase"])
            and max(spans["flash_exec"]) < min(spans["clock"])
            and max(spans["security_counter"]) < min(spans["aes"])
            and max(spans["mac_epoch"]) < symbols["_mac_radio_shared_end"]
            and max(spans["mac_tx"]) < min(spans["mac_adapter"])
            and max(spans["mac_adapter"]) < min(spans["join_smoke"]),
            "Workspace/lower/upper/caller private-prefix order changed")
    for name, size in (("link_work_arena", 617), ("child_work_arena", 543)):
        key = "G$" + name + "$0_0$0"
        require(key in declarations and declarations[key][1] == size, "Workspace extent changed")
    require(not any(n.startswith(("_host_", "_fixture_", "_test_", "_security_joint", "_aes_reference"))
                    for n in symbols), "Test implementation entered production CODE")
    return {"ordinary": cursor, "libc_prefix": 26, "declarations": len(declarations),
            "modules": {m: [min(s), max(s) + 1] for m, s in spans.items() if s}}


class CallerSchema(Schema):
    def __init__(self, debug):
        self.types, self.globals = {}, {}
        debug = debug.split("M:join_smoke\n", 1)[1].split("\nM:", 1)[0]
        pattern = r"\(\{(\d+)\}S:S\$([A-Za-z_]\w*)\$0_0\$0\(\{(\d+)\}([^)]*)\),Z,0,0\)"
        for name, body in re.findall(r"^T:Fjoin_smoke\$(__\d+)\[(.*)\]$", debug, re.M):
            require(name not in self.types and re.sub(pattern, "", body) == "", "Malformed caller type")
            self.types[name] = tuple(Field(int(a), n, int(s), k)
                                    for a, n, s, k in re.findall(pattern, body))
        for name, size, shape in re.findall(
                r"^S:G\$(join_smoke_\w+)\$0_0\$0\(\{(\d+)\}([^)]*)\),F,0,0$", debug, re.M):
            require(name not in self.globals, "Duplicate caller global")
            self.globals[name] = Field(0, name, int(size), shape)

    def header(self, symbols=None):
        root = self.at("join_smoke_phase", "admission")
        lines = ["/* Generated from the admitted SDCC caller ABI. */",
                 f"#define JOIN_ADMISSION_SIZE {root.size}",
                 "static void join_pack_admission(uint8_t *out, const join_smoke_admission_t *p)",
                 "{", f"    memset(out,0,{root.size});"]

        def walk(field, expression, offset):
            array = re.fullmatch(r"DA(\d+)d,(.+)", field.shape)
            if array:
                count = int(array[1])
                require(count > 0 and field.size % count == 0, "Invalid admission array")
                for i in range(count):
                    walk(Field(0, "", field.size // count, array[2]),
                         f"{expression}[{i}]", offset + i * (field.size // count))
            elif field.shape.startswith("ST"):
                covered = set()
                for child in self.fields(field):
                    span = set(range(child.offset, child.offset + child.size))
                    require(not span & covered, "Unexpected admission union")
                    covered |= span
                    walk(child, expression + "." + child.name, offset + child.offset)
                require(covered == set(range(field.size)), "Unclassified admission padding")
            else:
                require(re.fullmatch(r"S[CIKL]:[US]", field.shape) and field.size in (1, 2, 4),
                        "Admission contains an unsupported scalar or pointer")
                for i in range(field.size):
                    lines.append(f"    out[{offset+i}]=(uint8_t)((uint32_t){expression}>>{8*i});")

        walk(root, "(*p)", 0)
        lines += ["}", ""]
        if symbols is not None:
            lines += [f"#define TARGET_{n} {symbols['_'+n]}u" for n in
                      ("aes_dma0", "aes_dma1", "aes_key", "aes_iv", "aes_input", "aes_output")]
        return "\n".join(lines)


def structure(root):
    image, symbols, debug, listings, objects = load(root)
    if (root / "xdata-overlay.json").exists():
        from xdata_overlay import verify as verify_overlay
        ownership = verify_overlay(root)
    else:
        ownership = xdata(symbols, debug, listings, objects)
    graph = call_graph(image, symbols, debug, listings, objects)
    data, _, _ = analyze_data(symbols, debug, listings, graph)
    stack = analyze_stack(image, symbols, graph, RUNTIME)
    CallerSchema(debug).header()
    return (image, symbols, debug, listings, objects), {
        "xdata": ownership, "DATA_functions": data[0], "DATA_pairs": data[1], "static_stack": stack}


def verify(root, board, key_mode="install-code"):
    pins = json.loads(Path(__file__).with_name("join_smoke_pins.json").read_bytes())
    require(key_mode in ("install-code", "default-tc"), "Unknown initial key profile")
    profile = board + ("-default-tc" if key_mode == "default-tc" else "")
    require(profile in pins and identities(root) == pins[profile], "Complete immutable join artifacts differ")
    artifacts, result = structure(root)
    expected = "_security_keys_provision_default" if key_mode == "default-tc" else "_security_keys_provision"
    require(expected in artifacts[1], "Wrong linked initial key provisioning API")
    require(("_bdb_join_provision_discovered" in artifacts[1]) == (key_mode == "default-tc"),
            "Wrong linked network discovery profile")
    return artifacts, result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--board", choices=("generic", "lg_esl29_rev03"), required=True)
    parser.add_argument("--key-mode", choices=("install-code", "default-tc"), default="install-code")
    parser.add_argument("--header", type=Path)
    parser.add_argument("--xdata-study", action="store_true",
                        help="Use the separate immutable experimental XDATA catalog; not production admission")
    args = parser.parse_args()
    report = args.output / "admission.json"
    report.unlink(missing_ok=True)
    if args.header:
        args.header.unlink(missing_ok=True)
    if args.xdata_study:
        from xdata_overlay import verify_study
        artifacts, result = verify_study(args.output, args.board, args.key_mode)
    else:
        artifacts, result = verify(args.output, args.board, args.key_mode)
    if args.header:
        args.header.write_text(CallerSchema(artifacts[2]).header(artifacts[1]), encoding="ascii")
    result.update(simulation_admitted=True, simulated=False, hardware_observed=False,
                  identities=identities(args.output))
    report.write_text(json.dumps(result, indent=2) + "\n", encoding="ascii")
    print(json.dumps(result))


if __name__ == "__main__":
    main()
