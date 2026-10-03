#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Decode the observed XH3 relocation subset and compare every emitted CODE byte."""
from collections import defaultdict
import re
import subprocess

from verify_firmware import require


MODES = {0, 2, 0x121, 0x123, 0x103, 0x183, 0x303, 0x101, 0x181, 0x301}


def metadata(raw):
    require(raw.startswith(b"XH3\n"), "Expected big-endian three-byte ASxxxx object")
    areas, symbols, area = [], [], None
    for line in raw.decode("ascii").splitlines():
        if line.startswith("A "):
            m = re.fullmatch(r"A (\w+) size ([0-9A-F]+) flags ([0-9A-F]+) addr ([0-9A-F]+)", line)
            require(m is not None and int(m[4], 16) == 0, "Unsupported object area")
            areas.append(dict(name=m[1], size=int(m[2], 16), flags=int(m[3], 16)))
            area = len(areas) - 1
        elif line.startswith("S "):
            m = re.fullmatch(r"S (\S+) (Def|Ref)([0-9A-F]+)", line)
            require(m is not None, "Malformed object symbol")
            symbols.append(dict(name=m[1], area=area if m[2] == "Def" else None,
                                defined=m[2] == "Def", value=int(m[3], 16)))
    return areas, symbols


def records(raw):
    """Positions refer to the emitted bytes, after byte-selection elision."""
    text = None
    for line in raw.decode("ascii").splitlines():
        if line.startswith("T "):
            require(text is None, "Unpaired text record")
            text = bytes.fromhex(line[2:])
        elif line.startswith("R "):
            require(text is not None and len(text) >= 3, "Missing relocation text")
            raw_modes = bytes.fromhex(line[2:])
            require(raw_modes[:2] == b"\0\0", "Unexpected relocation header")
            area, at, used, relocations = int.from_bytes(raw_modes[2:4], "big"), 4, set(), []
            while at < len(raw_modes):
                mode = raw_modes[at]
                at += 1
                if mode & 0xf0 == 0xf0:
                    mode = ((mode & 15) << 8) | raw_modes[at]
                    at += 1
                require(mode in MODES and at + 3 <= len(raw_modes), "Unsupported relocation mode")
                pos, index = raw_modes[at], int.from_bytes(raw_modes[at+1:at+3], "big")
                at += 3
                require(not relocations or pos > relocations[-1]["pos"],
                        "Nonmonotonic relocation positions")
                width = 3 if mode & 1 else 2
                span = set(range(pos, pos + width))
                require(pos >= 3 and pos + width <= len(text) and not span & used,
                        "Out-of-range or overlapping relocation")
                used |= span
                relocations.append(dict(mode=mode, index=index, pos=pos, width=width,
                                        addend=int.from_bytes(text[pos:pos+width], "big")))
            require(at == len(raw_modes), "Partial relocation")
            yield area, int.from_bytes(text[:3], "big"), text[3:], relocations
            text = None
        elif line.startswith(("P ", "B ")):
            raise ValueError("Unreviewed paging/bank relocation record")
    require(text is None, "Unpaired final text record")


def linked_objects(root, report):
    objects = [(m, (root / "runtime" / (m + ".rel")).read_bytes())
               for m in report["runtime_objects"]]
    objects += [(m, (root / (m + ".rel")).read_bytes()) for m in report["objects"]]
    mapping = (root / "join_smoke_unverified.map").read_text("ascii")
    libraries = mapping.split("Libraries Linked", 1)[1].split("User Base Address Definitions", 1)[0]
    pairs = re.findall(r"^(/\S+\.lib)\s+\[ (\w+)\.rel \]", libraries, re.M)
    require([m for _, m in pairs] ==
            ["crtclear", "crtxinit", "crtxclear", "gptr_cmp", "crtstart", "_gptrget"],
            "Unreviewed implicitly linked library members")
    for archive, module in pairs:
        raw = subprocess.check_output(["sdar", "-p", archive, module + ".rel"])
        require(re.findall(rb"^M (\w+)$", raw, re.M) == ([module.encode()] if module == "_gptrget" else []),
                "Wrong implicit runtime member")
        objects.append((module, raw))
    return objects


def audit(root, artifacts, report):
    image, linked, _, _, _ = artifacts
    objects = linked_objects(root, report)
    parsed, offsets, definitions = {}, defaultdict(int), {}
    for module, raw in objects:
        areas, symbols = metadata(raw)
        for index, area in enumerate(areas):
            name, flags = area["name"], area["flags"]
            area["base"] = (0 if flags & 8 else linked.get("s_" + name, 0))
            if not flags & 12:
                area["base"] += offsets[name]
                offsets[name] += area["size"]
            if not flags & 0x60:
                anchors = {linked[s["name"]] - s["value"] for s in symbols
                           if s["defined"] and s["area"] == index and s["name"] in linked}
                require(len(anchors) <= 1, "Inconsistent non-CODE area anchors")
                if anchors:
                    area["base"] = anchors.pop()
        parsed[module] = areas, symbols
        for symbol in symbols:
            if not symbol["defined"]:
                continue
            area = areas[symbol["area"]] if symbol["area"] is not None else None
            value = symbol["value"] + (area["base"] if area else 0)
            # Debug aliases are also full, untruncated object symbols.
            if area and area["flags"] & 0x60:
                if symbol["name"] in linked:
                    require(linked[symbol["name"]] == value,
                            "Object/linked address mismatch: " + symbol["name"])
                definitions[symbol["name"]] = (module, area["name"], symbol["value"], value)
    refs, emitted, count = [], {}, 0
    for module, raw in objects:
        areas, symbols = parsed[module]
        for area_index, offset, payload, rels in records(raw):
            require(area_index < len(areas), "Invalid source area")
            area = areas[area_index]
            removed, data = set(), bytearray(payload)
            for rel in rels:
                mode, index, pos = rel["mode"], rel["index"], rel["pos"] - 3
                target = None
                if mode & 2:
                    require(index < len(symbols), "Invalid target symbol")
                    symbol = symbols[index]
                    target = definitions.get(symbol["name"])
                    if target:
                        value = target[3]
                    elif symbol["defined"]:
                        a = areas[symbol["area"]] if symbol["area"] is not None else None
                        value = symbol["value"] + (a["base"] if a else 0)
                    else:
                        require(symbol["name"] in linked, "Unresolved relocation symbol")
                        value = linked[symbol["name"]]
                else:
                    require(index < len(areas), "Invalid target area")
                    a = areas[index]
                    value = a["base"]
                    target = module, a["name"], 0, value
                value += rel["addend"]
                if mode & 1:
                    shift = 16 if mode & 0x200 else 8 if mode & 0x80 else 0
                    data[pos] = (value >> shift) & 255
                    removed.update((pos + 1, pos + 2))
                else:
                    data[pos:pos+2] = (value & 0xffff).to_bytes(2, "big")
                refs.append(dict(module=module, source_area=area["name"],
                                 source=area["base"] + offset + pos
                                 - sum(p < pos for p in removed),
                                 mode=mode, width=1 if mode & 1 else 2,
                                 target_module=target[0] if target else None,
                                 target_area=target[1] if target else None,
                                 target_offset=target[2] + rel["addend"] if target else None,
                                 value=value))
                count += 1
            data = bytes(b for i, b in enumerate(data) if i not in removed)
            if not area["flags"] & 0x20:
                require(not data, "Initialized non-CODE storage needs a separate model")
                continue
            for i, octet in enumerate(data):
                pc = area["base"] + offset + i
                require(pc not in emitted and image.get(pc) == octet,
                        f"Relocation/emitted CODE mismatch: {module}:{pc:x} "
                        f"decoded={octet:02x}, linked={image.get(pc)}, area={area['name']}, rels={rels}")
                emitted[pc] = octet
    require(emitted == image, "Relocation objects do not cover the entire final CODE image")
    return refs, {"relocations": count, "emitted_code": len(emitted),
                  "object_count": len(objects)}
