#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Closed-image XDATA inventory and conservative activation-lifetime analysis."""
import argparse
from collections import defaultdict, deque
import hashlib
import json
from pathlib import Path
import re

from join_smoke_analysis import call_graph, instructions, load
from boot_mac_link_child_abi import allocations, full_locations
from join_smoke_image import identities
from verify_banked_join import direct_accesses
from verify_firmware import require
from xdata_relocations import audit, metadata


def assembly_allocations(text):
    """Return exact compiler declaration blocks, not inferred symbol extents."""
    area, labels, result = None, [], []
    for match in re.finditer(r"^.*(?:\n|$)", text, re.M):
        line = match[0].strip()
        directive = re.match(r"\.area\s+(\w+)", line)
        if directive:
            area, labels = directive[1], []
        if area != "XSEG":
            continue
        if not line or line.startswith(";"):
            continue
        if directive:
            continue
        if re.fullmatch(r"(?:G\$join_smoke_status\$0_0\$0|_join_smoke_status)\s*={1,2}\s*0x1e00", line):
            require(not labels, "Absolute status interrupts an allocation")
            continue
        if re.fullmatch(r"[GFL][\w.$]+==\.", line) or re.fullmatch(r"_\w+::?", line):
            labels.append((line, match.start()))
            continue
        size = re.fullmatch(r"\.ds (\d+)", line)
        require(size is not None and len(labels) == 2, "Unclassified XSEG declaration: " + line)
        key, symbol = labels[0][0][:-3], labels[1][0].rstrip(":")
        require(symbol.startswith("_") and key[0] in "GFL", "Malformed XDATA labels")
        result.append({"key": key, "symbol": symbol, "size": int(size[1]),
                       "start": labels[0][1], "end": match.end()})
        labels = []
    require(not labels, "Unallocated XDATA labels")
    return result


def object_metadata(raw):
    return metadata(raw)


def parameter_names(module, function):
    source = Path(__file__).resolve().parents[1] / "src" / (module + ".c")
    require(source.is_file(), "Missing source for parameter classification")
    text = re.sub(r"/\*.*?\*/|//[^\n]*", "", source.read_text("ascii"), flags=re.S)
    aliases = re.findall(r"^#define (\w+) LW_SHALLOW_NAME\(\1,\s*" +
                         re.escape(function) + r"\)", text, re.M)
    require(len(aliases) <= 1, "Ambiguous shallow function name")
    matches = sorted({re.sub(r"\s+", " ", m).strip() for m in re.findall(
        r"\b" + re.escape(function) + r"\s*\(([^(){};]*)\)\s*(?:\w+\s*)?\{", text)})
    if not matches and aliases:
        matches = sorted({re.sub(r"\s+", " ", m).strip() for m in re.findall(
            r"\b" + re.escape(aliases[0]) + r"\s*\(([^(){};]*)\)\s*(?:\w+\s*)?\{", text)})
    candidates = set()
    for match in matches:
        result = []
        for arg in match.split(","):
            if arg.strip() == "void" or not arg.strip():
                continue
            require("(" not in arg and ")" not in arg and "[" not in arg, "Unmodeled parameter declarator")
            result.append(re.findall(r"\b\w+\b", arg)[-1])
        candidates.add(tuple(result))
    return candidates.pop() if len(candidates) == 1 else None


def compiler_ownership(directory, module, blocks, assembly):
    paths = list(directory.rglob(module + ".xdata.json"))
    require(len(paths) == 1, "Missing or ambiguous compiler ownership: " + module)
    manifest = json.loads(paths[0].read_bytes())
    require(manifest["version"] == 1 and manifest["module"] == module,
            "Wrong compiler ownership schema/module")
    classes = {"GLOBAL", "FILE_STATIC", "STATIC_LOCAL", "LOCAL", "FIRST_ARGUMENT_HOME",
               "REGISTER_ARGUMENT_HOME", "PARAM_CALLER_WRITTEN", "COMPILER_TEMP",
               "INLINE_RETURN_HOME", "UNKNOWN"}
    result, seen = {}, set()
    for row in manifest["objects"]:
        require(row["class"] in classes and type(row["size"]) is int and row["size"] > 0,
                "Unknown compiler class/size")
        require(all(type(row[f]) is bool for f in
                    ("absolute", "owner_reentrant", "owner_isr", "address_taken")),
                "Invalid compiler storage flags")
        require(row["area"] == "XSEG", "Unmodeled compiler storage area")
        key = row["cdb_key"]
        require(key not in seen, "Duplicate compiler storage key")
        seen.add(key)
        if row["absolute"]:
            require(re.search(r"^" + re.escape(key) + r"\s*==\s*0x" +
                              f'{row["address"]:04x}' + r"\s*$", assembly, re.M),
                    "Compiler absolute storage differs from assembly")
            continue
        require(row["address"] == 0, "Relocatable compiler object has an absolute address")
        result[key] = row
    require(set(result) == {b["key"] for b in blocks}, "Compiler/assembly allocation coverage differs")
    for block in blocks:
        row = result[block["key"]]
        require(row["symbol"] == block["symbol"] and row["size"] == block["size"],
                "Compiler/assembly storage identity differs")
        owner = module + "." + row["owner"] if row["owner"] is not None else None
        cdb_owner = block["key"][1:].split("$")[0] if block["key"].startswith("L") else None
        require(owner == cdb_owner, "Compiler/CDB owner differs")
        require(row["owner_symbol"] == ("_" + row["owner"] if owner else None),
                "Compiler function entry identity differs")
        require((row["class"] not in {"GLOBAL", "FILE_STATIC"} or owner is None) and
                (row["class"] not in {"LOCAL", "STATIC_LOCAL", "FIRST_ARGUMENT_HOME",
                 "REGISTER_ARGUMENT_HOME", "PARAM_CALLER_WRITTEN", "INLINE_RETURN_HOME"} or owner),
                "Compiler storage class/owner conflict")
        require(("_PARM_" in row["symbol"]) == (row["class"] == "PARAM_CALLER_WRITTEN"),
                "Caller-written parameter class differs")
    return result


def compiler_metadata_identities(directory):
    return {str(p.relative_to(directory)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(directory.rglob("*.xdata.json"))}


def inventory(root, artifacts, metadata_directory=None):
    _, symbols, debug, listings, objects = artifacts
    locations = full_locations(debug)
    declarations = {}
    for section in re.split(r"^M:", debug, flags=re.M)[1:]:
        module, _, body = section.partition("\n")
        for key, size, shape in re.findall(r"^S:([FGL][^(]+)\(\{(\d+)\}(.*)\),F,0,0$", body, re.M):
            if key in locations:
                identity = module, key
                require(identity not in declarations or declarations[identity] == (int(size), shape),
                        "Conflicting module-scoped CDB declaration: " + key)
                declarations[identity] = int(size), shape
    result, occupied = [], set()
    cursor = 0
    layout = json.loads((root / "layout.json").read_bytes())
    for module in layout["runtime_objects"]:
        runtime_raw = (root / "runtime" / (module + ".rel")).read_bytes()
        areas, syms = object_metadata(runtime_raw)
        indices = [i for i, a in enumerate(areas) if a["name"] == "XSEG"]
        require(len(indices) == 1, "Missing runtime XSEG")
        index = indices[0]
        size = areas[index]["size"]
        homes = sorted((s["value"], s["name"]) for s in syms if s["area"] == index)
        if module in ("__memcpy", "_memcmp"):
            evidence = json.loads((Path(__file__).resolve().parents[1] /
                                   "experiments/xdata/runtime-homes.json").read_bytes())[module]
            require(hashlib.sha256(runtime_raw).hexdigest() == evidence["object_sha256"],
                    "Unreviewed runtime anonymous homes")
            homes = [(r["offset"], r["symbol"]) for r in evidence["allocations"]]
        offsets = sorted({a for a, _ in homes} | {size})
        require(not size or offsets[0] == 0, "Runtime XSEG has anonymous prefix")
        for a, b in zip(offsets, offsets[1:]):
            names = [n for off, n in homes if off == a]
            require(names and all(symbols[n] == cursor + a for n in names if n in symbols),
                    "Runtime home address mismatch")
            result.append(dict(module=module, symbol=names[0], aliases=names, address=cursor+a,
                               linker_aliases=[n for n in names if n in symbols],
                               size=b-a, area="XSEG", storage_class="runtime_shared", category="runtime_shared",
                               owner=None, key=None, shape=None, address_taken="unknown",
                               pointer_escapes="unknown", retained_across_calls="yes",
                               candidate_for_overlay="no"))
        cursor += size
    require(cursor == 26, "Runtime XDATA prefix changed")
    for module, raw in listings.items():
        module_base = cursor
        text = (root / (module + ".asm")).read_text("ascii")
        blocks = assembly_allocations(text)
        owned = compiler_ownership(metadata_directory, module, blocks, text) if metadata_directory else None
        ranges = allocations(raw.decode("ascii")).get("XSEG", [])
        require(len(ranges) == len(blocks), "Compiler/listing allocation count differs")
        areas, syms = object_metadata(objects[module])
        index = next(i for i, a in enumerate(areas) if a["name"] == "XSEG")
        defined = {s["name"]: s["value"] for s in syms if s["area"] == index}
        row_symbols = {name: int(addr, 16) for addr, name in re.findall(
            r"^\s*([0-9A-F]{6})\s+\d+\s+(_\w+)::?$", raw.decode("ascii"), re.M)}
        parameters = {}
        formal_scopes = defaultdict(set)
        for block in blocks:
            if "_PARM_" in block["symbol"]:
                formal_scopes[block["key"].split("$")[0]].add(tuple(block["key"].split("$")[2:]))
        for block, (address, size) in zip(blocks, ranges):
            key, name = block["key"], block["symbol"]
            require(size == block["size"] and address == cursor, "XSEG gap/size/order mismatch")
            require((module, key) in declarations and declarations[module, key][0] == size
                    and locations[key] == address,
                    "CDB/listing XDATA identity mismatch: " + key)
            require(row_symbols[name] == address and defined[key] + module_base == address,
                    "Linked allocation label mismatch")
            if name in defined:
                require(defined[name] + module_base == address and symbols[name] == address,
                        "Global object/linker symbol differs")
            owner = key[1:].split("$")[0] if key.startswith("L") else None
            shape = declarations[module, key][1]
            variable = key.split("$")[1]
            if owned is None and owner and owner not in parameters and not formal_scopes[key.split("$")[0]]:
                parameters[owner] = parameter_names(module, owner.split(".", 1)[1])
            # Inlined/shadowed variables in deeper blocks are not formal parameters.
            scopes = formal_scopes[key.split("$")[0]]
            known = parameters.get(owner)
            parameter = owner and (tuple(key.split("$")[2:]) in scopes if scopes else
                                    known is not None and variable in known and key.split("$")[2] == "1_0")
            if owned is None and "_PARM_" in name:
                require(parameter, "Compiler/source parameter identity differs")
            category = ("parameter_home" if parameter else
                        "unknown_unclassified" if owner and not scopes and known is None else
                        "compiler_temporary" if owner and variable.startswith(("__", "sloc")) else
                        "function_local_array" if owner and shape.startswith("DA") else
                        "function_local_struct" if owner and shape.startswith("ST") else
                        "function_local_scalar" if owner else "persistent_global")
            compiler = {}
            if owned is not None:
                data = owned[key]
                owner = module + "." + data["owner"] if data["owner"] is not None else None
                kind = data["class"]
                category = ("persistent_global" if not owner else
                            "static_local" if kind == "STATIC_LOCAL" else
                            "unknown_unclassified" if kind == "UNKNOWN" else
                            "parameter_home" if kind in {"FIRST_ARGUMENT_HOME", "REGISTER_ARGUMENT_HOME",
                                                        "PARAM_CALLER_WRITTEN"} else
                            "compiler_temporary" if kind in {"COMPILER_TEMP", "INLINE_RETURN_HOME"} else
                            "function_local_array" if shape.startswith("DA") else
                            "function_local_struct" if shape.startswith("ST") else "function_local_scalar")
                compiler = dict(compiler_class=kind, compiler_address_taken=data["address_taken"],
                                compiler_reentrant=data["owner_reentrant"], compiler_isr=data["owner_isr"])
            result.append(dict(module=module, symbol=name, address=address, size=size,
                               object_offset=address-module_base,
                               area="XSEG",
                               storage_class=category if owner else
                               ("file_static" if key.startswith("F") else "global"),
                               category=category, owner=owner, key=key,
                               shape=shape, address_taken="unknown", pointer_escapes="unknown",
                               retained_across_calls="unknown", candidate_for_overlay="unknown", **compiler))
            cursor += size
        require(sum(b["size"] for b in blocks) == areas[index]["size"], "Object XSEG size differs")
    for row in result:
        span = set(range(row["address"], row["address"] + row["size"]))
        require(not span & occupied, "Baseline XDATA allocations overlap")
        occupied |= span
    require(occupied == set(range(symbols["l_XSEG"])), "Inventory does not cover entire l_XSEG")
    require(sum(r["size"] for r in result if r["owner"]) == 2849,
            "Requested baseline function-home inventory differs")
    return result


def activation_graph(graph, symbols):
    decoded, _, functions, entries, targets, calls = graph
    edges = {e: set() for e in entries}
    for pc, target in calls.items():
        if pc in functions and target in entries:
            edges[functions[pc]].add(target)
    for pc, target in targets.items():
        if pc not in calls and pc in functions and target in entries and target != functions[pc]:
            edges[functions[pc]].add(target)
    enter = next(e for e, ident in entries.items() if ident[:2] == ("flash_exec", "enter_ram"))
    template = next(e for e, ident in entries.items() if ident[:2] == ("flash_exec", "flash_exec_template"))
    edges[enter].add(template)
    closure, cycles = {}, set()
    for entry in entries:
        pending, seen = list(edges[entry]), set()
        while pending:
            child = pending.pop()
            if child in seen:
                continue
            seen.add(child)
            pending.extend(edges[child] - seen)
        if entry in seen:
            cycles.add(entry)
        closure[entry] = seen
    for raw in decoded.values():
        _, writes = direct_accesses(raw)
        if writes & {0xa8, 0xb8, 0x9a}:
            require(raw[0] == 0x75 and raw[2] == 0, "Interrupt enable invalidates XDATA lifetime")
        if raw[0] in (0x10, 0x92, 0xb2, 0xc2, 0xd2) and raw[1] & 0xf8 in (0xa8, 0xb8):
            require(raw[0] in (0x10, 0xc2), "Interrupt-bit enable invalidates XDATA lifetime")
    return edges, closure, cycles


def nonescape(row, graph, reference_index, debug):
    """DPTR is the only permitted address materialization; values may be pointers."""
    decoded, _, functions, entries, targets, calls = graph
    identity = tuple(row["owner"].split(".", 1))
    matches = [e for e, ident in entries.items() if ident[:2] == identity]
    require(len(matches) == 1, "Missing storage owner function")
    entry = matches[0]
    name, lo, size = row["symbol"], row["address"], row["size"]
    references = []
    for pc, raw, asm in reference_index.get(row["key"], []):
        if functions.get(pc) != entry:
            return False, "reference outside owner activation", []
        if raw[0] != 0x90:
            return False, "address materialized outside DPTR", []
        address = int.from_bytes(raw[1:], "big")
        if not lo <= address < lo + size:
            return False, "reference offset outside object", []
        references.append(pc)
    if not references:
        return False, "no instruction references; dead stripping is separate", []
    # Track live address bytes and definitely initialized object bytes at every CFG join.
    shape = re.search(r"^F:(?:G|F" + re.escape(identity[0]) + r")\$" +
                      re.escape(identity[1]) + r"\$0_0\$0\(\{[23]\}DF,(.*?)\),[CZ],0,0,0,0,0$", debug, re.M)
    require(shape is not None, "Missing return ABI")
    ret = shape[1]
    require(ret.startswith(("SV:", "SC:", "SI:", "SX:", "SL:", "DG", "DX", "DC")),
            "Unknown return ABI")
    returns = 0 if ret.startswith("SV:") else 1 if ret.startswith("SC:") else 2
    states, pending = {entry: {(0, 0, 0)}}, deque([entry])
    refs = set(references)
    banker_return = next(e for e, ident in entries.items()
                         if ident[:2] == ("banked", "_sdcc_banked_ret"))
    while pending:
        pc = pending.popleft()
        require(pc in decoded and functions.get(pc) == entry, "Owner CFG escapes function")
        raw = decoded[pc]
        outgoing = set()
        for mask, offset, initialized in states[pc]:
            op = raw[0]
            if pc in refs:
                mask, offset = 3, int.from_bytes(raw[1:], "big") - lo
            elif op == 0x90:
                mask, offset = 0, 0
            elif op == 0xa3 and mask:
                if mask != 3 or offset >= size:
                    return False, "unbounded/partial DPTR increment", references
                offset += 1
            elif op in (0xe0, 0xf0) and mask:
                if mask != 3 or not 0 <= offset < size:
                    return False, "indirect access escapes object", references
                if op == 0xe0 and not initialized & (1 << offset):
                    return False, "home read before activation-local definition", references
                if op == 0xf0:
                    initialized |= 1 << offset
            elif op in (0x73, 0x93) and mask:
                return False, "implicit DPTR CODE consumer", references
            else:
                reads, writes = direct_accesses(raw)
                if mask & 1 and 0x82 in reads or mask & 2 and 0x83 in reads:
                    return False, "storage address read from DPTR", references
                if 0x82 in writes:
                    mask &= ~1
                if 0x83 in writes:
                    mask &= ~2
                if not mask:
                    offset = 0
            if pc in calls:
                if mask:
                    return False, "storage address live at call boundary", references
                mask, offset = 0, 0
            terminal = op == 0x22 or targets.get(pc) == banker_return
            if terminal and (mask & 1 and returns or mask & 2 and returns >= 2):
                return False, "storage address in return registers", references
            outgoing.add((mask, offset, initialized))
        if raw[0] == 0x22 or targets.get(pc) == banker_return:
            continue
        successor = pc + len(raw)
        if pc in calls:
            successors = [successor]
        elif pc in targets:
            successors = [targets[pc]]
            if raw[0] not in (2, 0x80) and raw[0] & 31 != 1:
                successors.append(successor)
        elif raw[0] == 0x73:
            return False, "indirect owner control flow", references
        else:
            successors = [successor]
        for child in successors:
            if functions.get(child) != entry:
                return False, "non-return inter-function branch", references
            previous = states.setdefault(child, set())
            if not outgoing <= previous:
                previous.update(outgoing)
                require(len(previous) <= 4096, "Object proof state bound exceeded")
                pending.append(child)
    return True, "owner-only DPTR uses; initialized before read; no address transfer", references


def analyze(root, metadata_directory=None):
    artifacts = load(root)
    image, symbols, debug, listings, objects = artifacts
    rows = inventory(root, artifacts, metadata_directory)
    graph = call_graph(image, symbols, debug, listings, objects)
    from join_smoke_analysis import RUNTIME
    from join_smoke_stack import analyze_stack
    stack = analyze_stack(image, symbols, graph, RUNTIME)
    relocations, relocation_summary = audit(root, artifacts, json.loads((root / "layout.json").read_bytes()))
    edges, closure, cycles = activation_graph(graph, symbols)
    entries = {":".join(v[:2]): k for k, v in graph[3].items()}
    groups = defaultdict(list)
    reference_index = defaultdict(list)
    locations = {}
    assembly = {}
    for module, listing in listings.items():
        for pc, (raw, asm) in instructions(listing.decode("ascii")).items():
            assembly[pc] = asm
            for offset in range(len(raw)):
                locations[pc + offset] = pc
    ranges = {}
    for row in rows:
        if row["key"]:
            for address in range(row["address"], row["address"] + row["size"]):
                ranges[address] = row
    for ref in relocations:
        if ref["target_area"] != "XSEG" or ref["value"] not in ranges:
            continue
        row = ranges[ref["value"]]
        pc = locations.get(ref["source"])
        raw = graph[0].get(pc, b"\0")
        valid = (pc is not None and ref["source"] == pc + 1
                 and ref["mode"] in (0, 2) and raw[0] == 0x90)
        reference_index[row["key"]].append((pc, raw if valid else b"\0", assembly.get(pc, "")))
    for row in rows:
        if not row["owner"]:
            continue
        if (row.get("compiler_class") in {"STATIC_LOCAL", "UNKNOWN", "PARAM_CALLER_WRITTEN"} or
                row.get("compiler_reentrant") or row.get("compiler_isr")):
            row.update(reason="compiler storage is retained, unknown, reentrant, ISR or caller-written",
                       candidate_for_overlay="no")
            continue
        if row["module"] in ("banked", "flash_exec") or "_PARM_" in row["symbol"]:
            row["reason"] = "runtime state or caller-written parameter home excluded"
            row["candidate_for_overlay"] = "no"
            continue
        safe, reason, refs = nonescape(row, graph, reference_index, debug)
        row.update(reason=reason, references=refs, candidate_for_overlay="yes" if safe else "no",
                   address_taken="no" if safe else "unknown",
                   pointer_escapes="no" if safe else "unknown",
                   retained_across_calls="unknown")
        key = row["owner"].replace(".", ":", 1)
        if entries[key] in cycles:
            row.update(candidate_for_overlay="no", reason="recursive activation")
        if row["candidate_for_overlay"] == "yes":
            groups[row["owner"]].append(row)
    summaries = []
    for owner, members in groups.items():
        entry = entries[owner.replace(".", ":", 1)]
        conflicts = [o for o in groups if o != owner and
                     (entries[o.replace(".", ":", 1)] in closure[entry]
                      or entry in closure[entries[o.replace(".", ":", 1)]])]
        summaries.append({"owner": owner, "bytes": sum(r["size"] for r in members),
                          "objects": [r["key"] for r in members], "conflicts": conflicts})
    categories = defaultdict(lambda: {"bytes": 0, "objects": 0})
    for row in rows:
        categories[row["category"]]["bytes"] += row["size"]
        categories[row["category"]]["objects"] += 1
    models = {}
    for model in ("A", "B"):
        weights = defaultdict(int)
        for row in rows:
            if row["candidate_for_overlay"] == "yes" and (
                    model == "B" or row["category"] != "parameter_home"):
                weights[entries[row["owner"].replace(".", ":", 1)]] += row["size"]
        predecessors = {e: set() for e in edges}
        for parent, children in edges.items():
            for child in children:
                predecessors[child].add(parent)
        finish, placement = {}, {}

        def bound(entry):
            if entry not in finish:
                start = max((bound(p) for p in predecessors[entry]), default=0)
                placement[entry] = start
                finish[entry] = start + weights[entry]
            return finish[entry]

        require(not cycles, "Recursive graph cannot be interval-colored")
        minimum = max(map(bound, edges), default=0)
        current = sum(weights.values())
        models[model] = {"eligible_bytes": current, "pool_minimum": minimum,
                         "saving": current - minimum,
                         "xdata_minimum": symbols["l_XSEG"] - current + minimum,
                         "placement_semantics_proved": False,
                         "constraint": "Activation/escape model only. Cross-module private fences and "
                                       "pointer-admission ordering require additional placement constraints.",
                         "placements": {":".join(graph[3][e][:2]): [placement[e], weights[e]]
                                        for e in edges if weights[e]},
                         "optimality": "Weighted longest ancestor chain is a lower bound; "
                                       "the emitted intervals attain it in the activation-conflict model."}
    models["C"] = {**models["B"], "phase_exclusions": "None added; no independently proved extra exclusions."}
    return {"identities": identities(root), "xseg": symbols["l_XSEG"],
            "function_scoped": sum(r["size"] for r in rows if r["owner"]),
            "categories": dict(categories), "objects": rows,
            "groups": sorted(summaries, key=lambda r: (-r["bytes"], r["owner"])),
            "cycles": sorted(cycles), "complete_escape_proof": True,
            "relocation_audit": relocation_summary, "stack": stack,
            "models": models,
            "scope": "Closed compiler image; all homes remain live for their entire owning activation. "
                     "Caller-written parameters, arbitrary/out-of-bounds pointer inputs, "
                     "interrupt-enabled/reentrant images and hardware execution are not admitted."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("layout", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--compiler-metadata", type=Path,
                        help="Compiler sidecar root; replaces source-based ownership classification")
    args = parser.parse_args()
    result = analyze(args.layout, args.compiler_metadata)
    if args.compiler_metadata:
        result["compiler_metadata"] = compiler_metadata_identities(args.compiler_metadata)
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({k: result[k] for k in ("xseg", "function_scoped", "categories", "cycles")}))
    print(json.dumps(result["groups"][:20], indent=2))


if __name__ == "__main__":
    main()
