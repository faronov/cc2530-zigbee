#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Strict CHILD pointer ABI micro-image proof; NOT combined PHY/stack acceptance.

CC2530F256: SWRU191F (April2014), section2.2.2/p27 IRAM alias and MPAGE
register93. The actual compiler/runtime ABI is pinned SDCC4.2, not a generic
8051 peripheral model. No AES, flash, USB or RF operation is executed.
"""
import argparse
from dataclasses import dataclass
import hashlib
from pathlib import Path
import re

from boot_image import ALIAS, check_alias, check_pc, marker, memory_dump, section, simulate
from radio_link_fixture import records
from verify_firmware import code_bytes, parse_ihex, require

STEM = "child_abi"
MODULES = ("mac_link_child_workspace", "mac_link_workspace", "flash_exec", "mac_link_child_abi")
SIZE, XDATA, CHECKS = 11105, 1551, 43098
STACK_FIRST, STACK_SIZE, SP_CAP, PEAK = 0x50, 45, 0x7c, 0x59
MAIN, DONE, FAILED, FLASH = 0x1bcd, 0x1b83, 0x1b80, 0x1830
# SDCC4.2.0#13081, canonical src/... / tests/... inputs, BOTH board definitions.
# These bind whole bytes, not source-line-masked or instruction-only identities.
CODE_SHA = "94ce5357761bed5812c2f5722112dde7ab97efe88ecb3bc711ef614a94994a69"
CDB_SHA = "d001b9be94fab607449a3a647ec692704a7641b5af137877b156bbf877193b17"
MEM_SHA = "f0433cd0f02beb7eda5cb8a7ddb432cbd0400200e029c534776d8e929040927b"
MAP_PREFIX_SHA = "8f851ccbb299c3fb0598da31c4f7ee04e13d25aeefffe0423badaf6c9ca3a1bc"
MAP_SUFFIX_SHA = "da6eb110b124ece27e2f55dc4b5f1775a1c494cccbf3731cf862b0a9c297de93"
OBJECT_PINS = (
    "e055058223c4e704a9d97c8c6efd3f028920bcaaa9cfd70273e65cad5d33cb11",
    "85cb14a68f06ec5c1499ffad9b240056f35ba3dc96ce6937077753108c34533d",
    "c2c851f453375df8b7175fba398cb0d1da5dc171231e27fe21bd54bcf79117a3",
    "f80e0539b57b42e16383e44ab0176827f5b6923663803953cf9af4b7df00fb19",
)
LIST_PINS = (
    "4b9e94585f97d6cd9be29f347ebca7c6b2846c3cd76d001cfbdec57c1c21a89d",
    "5bf12f2401d7e8a2d94a3d2b4e058570024aa93cf80cdd5e2de8c819f814c566",
    "6c2b4b1d9f81450db84b3c804c5f5c25e4abe967fdc498c135627133d3091f4e",
    "13bd83b4dcb03161cd3ee561c843d7f724b8c3cf01baef7fd2b21792f9711147",
)
RUNTIME = ("___memcpy_PARM_2", "___memcpy_PARM_3", "__gptrput_PARM_2", "__mullong_PARM_2")


@dataclass(frozen=True)
class Artifacts:
    path: Path
    image: dict
    debug: bytes
    memory: bytes
    mapping: bytes
    objects: dict
    listings: dict


@dataclass(frozen=True)
class Layout:
    symbols: dict
    ordinary: frozenset
    direct: frozenset


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def ascii_text(raw, name):
    require(type(raw) is bytes and b"\r" not in raw and b"\0" not in raw,
            "CHILD ABI malformed raw " + name)
    try:
        return raw.decode("ascii")
    except UnicodeDecodeError as error:
        raise ValueError("CHILD ABI non-ASCII " + name) from error


def object_body(raw, module):
    """Only the existing SDCC ;!FILE OUTPUT-PATH comment is variable."""
    text = ascii_text(raw, "object")
    first, separator, body = text.partition("\n")
    require(separator and first.startswith(";!FILE ") and
            Path(first[7:]).name == module + ".asm", "CHILD ABI object output-path comment")
    require(re.findall(r"^M (\w+)$", body, re.M) == [module], "CHILD ABI object module")
    return body.encode("ascii")


def map_metadata(raw):
    """Bind ALL address/area rows, including ambiguous31-character map names.

    ASlink MAP is not a full-name symbol dictionary. The complete raw metadata
    before Files Linked and after Libraries Linked is immutable. The intervening
    four actual link inputs must be in order, in one BUILD directory, and have
    their exact basenames/format. No source/CODE/CDB text is normalized.
    """
    ascii_text(raw, "map")
    require(raw.count(b"Files Linked") == raw.count(b"Libraries Linked") == 1,
            "CHILD ABI map inventory sections")
    prefix, rest = raw.split(b"Files Linked")
    files, suffix = rest.split(b"Libraries Linked")
    require(sha(prefix) == MAP_PREFIX_SHA and sha(suffix) == MAP_SUFFIX_SHA,
            "CHILD ABI complete map address/area/library/base metadata")
    header = b"                              [ module(s) ]\n\n"
    require(files.startswith(header) and files.endswith(b"\n\n\n"), "CHILD ABI map file framing")
    lines = files[len(header):-3].decode("ascii").split("\n")
    require(len(lines) == 8 and all(lines[i] == " " * 42 + "[  ]" for i in (1, 3, 5, 7)),
            "CHILD ABI map source-object inventory")
    paths = [Path(lines[i]) for i in (0, 2, 4, 6)]
    require(tuple(p.name for p in paths) == tuple(m + ".rel" for m in MODULES) and
            len({p.parent for p in paths}) == 1, "CHILD ABI map source-object order/directory")
    return prefix.decode("ascii")


def immutable(artifacts):
    # In particular, never read_text/decode/strip/replace the CDB before this pin.
    require(type(artifacts.debug) is bytes and sha(artifacts.debug) == CDB_SHA,
            "CHILD ABI complete raw CDB before decoding")
    require(sha(code_bytes(artifacts.image, SIZE)) == CODE_SHA, "CHILD ABI complete CODE")
    require(set(artifacts.objects) == set(artifacts.listings) == set(MODULES),
            "CHILD ABI complete source-object/listing inventories")
    require(sha(artifacts.memory) == MEM_SHA, "CHILD ABI complete memory accounting")
    for module, obj, listing in zip(MODULES, OBJECT_PINS, LIST_PINS):
        require(sha(object_body(artifacts.objects[module], module)) == obj,
                "CHILD ABI complete source object: " + module)
        require(sha(artifacts.listings[module]) == listing,
                "CHILD ABI complete relocated snapshot: " + module)
    return map_metadata(artifacts.mapping)


def full_locations(debug):
    found = {}
    # Source-line records can legitimately recur. These are FULL object/function
    # identities, not truncated MAP names and not L:C source-line records.
    for name, address in re.findall(r"^L:([GFL][^:\n]+):([0-9A-Fa-f]+)$", debug, re.M):
        value = int(address, 16)
        require(name not in found or found[name] == value, "CHILD ABI conflicting full CDB location")
        found[name] = value
    return found


def short_symbols(prefix, locations):
    symbols = {}
    for name, address in locations.items():
        if name.startswith("G$"):
            key = "_" + name.split("$")[1]
            require(key not in symbols or symbols[key] == address, "CHILD ABI conflicting global")
            symbols[key] = address
    # Only these unambiguous short names come from MAP. Every other map row is
    # nevertheless bound by the raw metadata pin above, including collisions.
    for address, name in re.findall(
            r"^\s*(?:[CD]:\s*)?([0-9A-F]{8})\s+(\S+)(?:\s+\S+)?\s*$", prefix, re.M):
        if name.startswith(("s_", "l_")) or name in (*RUNTIME, "__start__stack"):
            value = int(address, 16)
            require(name not in symbols or symbols[name] == value, "CHILD ABI conflicting map area/runtime")
            symbols[name] = value
    return symbols


def allocations(text):
    area, result = None, {}
    for line in text.splitlines():
        match = re.search(r"\s\.area\s+([^\s(]+)", line)
        if match:
            area = match[1]
        match = re.fullmatch(r"\s*([0-9A-F]{6})\s+\d+\s+\.ds (\d+)", line)
        if match:
            require(area is not None, "CHILD ABI allocation without area")
            result.setdefault(area, []).append((int(match[1], 16), int(match[2])))
    return result


def disjoint(ranges, limit, label):
    used = set()
    for first, size in ranges:
        span = set(range(first, first + size))
        require(size > 0 and 0 <= first < first + size <= limit and not used & span,
                "CHILD ABI overlap/range: " + label)
        used.update(span)
    return used


def layout_checks(image, debug, symbols, memory, listings, objects):
    """Structural checks supplement pins; also directly exercised by negatives."""
    require(symbols["_main"] == MAIN and symbols["_child_abi_done"] == DONE and
            symbols["_child_abi_failed"] == FAILED and symbols["_flash_exec_command"] == FLASH,
            "CHILD ABI exact entry/terminal/forbidden PCs")
    require(re.findall(r"^M:(\w+)$", debug, re.M) == list(MODULES),
            "CHILD ABI source/probe module separation")
    locations = full_locations(debug)
    require(symbols["__XPAGE"] == 0x93, "CHILD ABI must use CC2530 MPAGE")
    require(symbols["s_XSEG"] == 1 and symbols["l_XSEG"] == XDATA and
            all(symbols["l_" + a] == 0 for a in ("PSEG", "XISEG", "XABS", "XINIT")),
            "CHILD ABI ordinary/paged/initialized XDATA")
    require(symbols["s_SSEG"] == symbols["__start__stack"] == STACK_FIRST and
            symbols["l_SSEG"] == STACK_SIZE and STACK_FIRST + STACK_SIZE == SP_CAP + 1,
            "CHILD ABI stack reservation")
    require(symbols["s_ISEG"] == 0x4f and symbols["l_ISEG"] == 1 and
            symbols["s_OSEG"] == 0x42 and symbols["l_OSEG"] == 5 and
            symbols["s_BSEG_BYTES"] == 0x20 and symbols["l_BSEG_BYTES"] == 3 and
            symbols["l_BSEG"] == 18 and symbols["l_REG_BANK_0"] == 8 and
            all(symbols["l_REG_BANK_" + str(i)] == 0 for i in (1, 2, 3)),
            "CHILD ABI actual IRAM areas")
    stack = re.search(r"Stack starts at: 0x(\w+) \(sp set to 0x(\w+)\) with (\d+) bytes available", memory)
    require(stack and (int(stack[1], 16), int(stack[2], 16), int(stack[3])) ==
            (STACK_FIRST, STACK_FIRST - 1, STACK_SIZE), "CHILD ABI .mem stack")
    external = re.search(r"EXTERNAL RAM\s+0x(\w+)\s+0x(\w+)\s+(\d+)\s+(\d+)", memory)
    flash = re.search(r"ROM/EPROM/FLASH\s+0x(\w+)\s+0x(\w+)\s+(\d+)\s+(\d+)", memory)
    require(external and (int(external[1], 16), int(external[2], 16),
                          int(external[3]), int(external[4])) == (1, XDATA, XDATA, 0x1dff),
            "CHILD ABI .mem XDATA")
    require(flash and (int(flash[1], 16), int(flash[2], 16), int(flash[3]), int(flash[4])) ==
            (0, SIZE - 1, SIZE, 0x8000) and image[0] == 2, "CHILD ABI .mem CODE/reset")
    ordinary, direct_ranges, bits, idata, covered = set(), [], [], [], set()
    module_spans = {}
    for module in MODULES:
        text = listings[module]
        require(re.findall(r"\.module (\w+)", text) == [module], "CHILD ABI wrong snapshot module")
        a = allocations(text)
        sizes = {name: int(size, 16) for name, size in
                 re.findall(r"^A (\w+) size (\w+) flags \w+ addr \w+$", objects[module], re.M)}
        require(set(a) <= {"REG_BANK_0", "DSEG", "BJ_CHILD_ABI", "BJ_flash_exec",
                           "ISEG", "BSEG", "XSEG"} and a.get("REG_BANK_0") == [(0, 8)] and
                all(sum(n for _, n in spans) == sizes.get(area) for area, spans in a.items()),
                "CHILD ABI complete source allocation areas")
        span = disjoint(a.get("XSEG", ()), 0x1e00, module + " XDATA")
        require(len(span) == sizes["XSEG"] and not span & ordinary, "CHILD ABI source XDATA allocation")
        module_spans[module] = span
        ordinary.update(span)
        direct_ranges += a.get("DSEG", []) + a.get("BJ_CHILD_ABI", []) + a.get("BJ_flash_exec", [])
        bits += a.get("BSEG", [])
        idata += a.get("ISEG", [])
        require(not any(a.get(n) for n in ("PSEG", "XISEG", "XABS", "OSEG")),
                "CHILD ABI unaccounted source allocation")
        for address, raw in records(text):
            span = set(range(address, address + len(raw)))
            require(not span & covered and all(image.get(address + i) == v for i, v in enumerate(raw)),
                    "CHILD ABI overlapping/nonlinked instructions")
            covered.update(span)
    # Exact normal linker allocations; the13-byte libc tail is genuine library
    # storage, not external arrays invented to satisfy private-prefix guards.
    end = 1
    for module, size in zip(MODULES, (624, 702, 155, 57)):
        require(module_spans[module] == set(range(end, end + size)), "CHILD ABI source/caller allocation order")
        end += size
    require(tuple(symbols[n] - end for n in RUNTIME) == (0, 3, 8, 9) and end + 13 == XDATA + 1,
            "CHILD ABI genuine memcpy/store/multiply allocation tail")
    ordinary.update(range(end, end + 13))
    require(ordinary == set(range(1, XDATA + 1)) and len(ordinary) + 64 == 1615,
            "CHILD ABI ordinary plus M0 reservation budget")
    direct = disjoint(direct_ranges, STACK_FIRST, "source DATA")
    declarations = {}
    for name, size in re.findall(r"^S:([^(\n]+)\(\{(\d+)\}[^\n]*\),E,0,0$", debug, re.M):
        require(name not in declarations or declarations[name] == int(size), "CHILD ABI DATA declaration")
        declarations[name] = int(size)
    absent = set(declarations) - locations.keys()
    require(absent == {"G$banked_depth$0_0$0", "G$banked_fault$0_0$0"},
            "CHILD ABI unresolved DATA is not a fabricated allocation")
    actual = disjoint([(locations[n], size) for n, size in declarations.items() if n in locations],
                      STACK_FIRST, "linked CDB DATA")
    require(actual == direct and len(direct) == 26, "CHILD ABI listing/CDB actual DATA ownership")
    require(disjoint(bits, 18, "source BIT") == set(range(18)) and
            disjoint(idata, STACK_FIRST, "source IDATA") == {0x4f}, "CHILD ABI BIT/IDATA accounting")
    disjoint(direct_ranges + [(0, 8), (0x20, 3), (0x42, 5), (0x4f, 1),
                             (STACK_FIRST, STACK_SIZE)], SP_CAP + 1, "physical DATA/OSEG/IRAM/stack")
    for module, arena, size, owner in (
        (MODULES[0], "_child_work_arena", 543, "Fmac_link_child_workspace$owner$0_0$0"),
        (MODULES[1], "_link_work_arena", 617, "Fmac_link_workspace$ownership$0_0$0"),
    ):
        require(set(range(symbols[arena], symbols[arena] + size)) <= module_spans[module] and
                locations[owner] == min(module_spans[module]) and
                symbols[arena] == locations[owner] + 31,
                "CHILD ABI typed arena/owner placement")
    require(symbols["_child_work_reserved_end"] == max(module_spans[MODULES[0]]) and
            symbols["_child_work_reserved_end"] < symbols["_flash_exec_reserved_end"] and
            symbols["_link_work_arena"] + 617 < symbols["_flash_exec_reserved_end"],
            "CHILD ABI complete manager/private fence")
    sizes = re.findall(r"^S:G\$child_abi_result\$[^(\n]+\(\{(\d+)\}", debug, re.M)
    require(sizes == ["8"] and symbols["_child_abi_result"] == 0x1e00 and
            re.findall(r"^S:G\$child_abi_checks\$[^(\n]+\(\{(\d+)\}", debug, re.M) == ["4"] and
            set(range(symbols["_child_abi_checks"], symbols["_child_abi_checks"] + 4)) <= module_spans[MODULES[-1]],
            "CHILD ABI genuine probe status/check objects")
    return Layout(symbols, frozenset(ordinary), frozenset(direct))


def verify(artifacts):
    prefix = immutable(artifacts)
    debug = ascii_text(artifacts.debug, "CDB")
    symbols = short_symbols(prefix, full_locations(debug))
    return layout_checks(artifacts.image, debug, symbols, ascii_text(artifacts.memory, "memory"),
                         {m: ascii_text(v, "listing") for m, v in artifacts.listings.items()},
                         {m: ascii_text(v, "object") for m, v in artifacts.objects.items()})


def load(output):
    """Read the exact ABI directory, normally BUILD/mac-link-child-workspace/abi.

    Do not append another subdirectory, recurse, or fall back to parent objects:
    the40 production objects and allocation-only layout probe remain outside.
    Copied object ;!FILE comments may still name their original parent directory.
    """
    path = output / (STEM + ".ihx")
    debug = path.with_suffix(".cdb").read_bytes()
    require(sha(debug) == CDB_SHA, "CHILD ABI complete raw CDB before decoding")
    require({p.name for p in output.glob("*.rel")} == {m + ".rel" for m in MODULES} and
            {p.name for p in output.glob(STEM + ".*.rst")} == {STEM + "." + m + ".rst" for m in MODULES},
            "CHILD ABI exact on-disk object/snapshot inventories")
    return Artifacts(path, parse_ihex(ascii_text(path.read_bytes(), "HEX")), debug,
                     path.with_suffix(".mem").read_bytes(), path.with_suffix(".map").read_bytes(),
                     {m: (output / (m + ".rel")).read_bytes() for m in MODULES},
                     {m: (output / (STEM + "." + m + ".rst")).read_bytes() for m in MODULES})


def strict_dump(text, first, size):
    result = memory_dump(text, first, size)
    occupied, expected = set(), set(range(first, first + size))
    for address, raw in re.findall(r"^0x([0-9a-fA-F]+)\s+((?:[0-9a-fA-F]{2}(?:\s+|$))+)", text, re.M):
        span = set(range(int(address, 16), int(address, 16) + len(bytes.fromhex(raw))))
        require(not occupied & span and span <= expected,
                "CHILD ABI ambiguous/out-of-range dump")
        occupied.update(span)
    require(occupied == expected, "CHILD ABI complete dump")
    return result


def commands():
    return [ALIAS, "fill xram 0 0x1eff 0xa5", "set memory sfr 0xa8 0",
            f"break {FLASH:#x}", f"break {FAILED:#x}", f"break {DONE:#x}",
            f"run 0 {MAIN:#x}", marker(1), "state", "dump /h sfr 0x81 0x81", marker(2),
            "fill iram 0x7d 0xff 0xc7", "run", marker(3), "state",
            "dump /h sfr 0x81 0x81", marker(4), "dump /h xram 0 0x1eff", marker(5),
            "dump /h iram 0 0xff", marker(6), "dump /h xram 0x1f00 0x1fff", marker(7)]


def observation(text, layout):
    stops = [(int(a, 16), reason) for a, reason in
             re.findall(r"^Stop at 0x([0-9a-fA-F]+): ([^\n]+)$", text, re.M)]
    require(stops == [(MAIN, "(104) Breakpoint"), (DONE, "(104) Breakpoint")],
            "CHILD ABI wrong stop/flash entry")
    for number in range(1, 8):
        require(len(re.findall(r"^0x2530" + f"{number:04x}" + r"$", text, re.M)) == 1,
                "CHILD ABI missing/duplicate observation boundary")
    initial, final = section(text, 1), section(text, 3)
    for part, pc, sp in ((initial, MAIN, STACK_FIRST - 1), (final, DONE, STACK_FIRST + 1)):
        require(len(re.findall(r"CPU state=", part)) == 1, "CHILD ABI ambiguous CPU observation")
        check_pc(part, pc)
        require(strict_dump(part, 0x81, 1) == bytes([sp]), "CHILD ABI actual SP")
        peaks = re.findall(r"Max value of stack pointer=\s*0x([0-9a-fA-F]+)", part)
        require(len(peaks) == 1 and sp <= int(peaks[0], 16) <= SP_CAP, "CHILD ABI stack cap/observation")
    require(int(re.findall(r"Max value of stack pointer=\s*0x([0-9a-fA-F]+)", final)[0], 16) == PEAK,
            "CHILD ABI exact observed peak")
    ram = strict_dump(section(text, 4), 0, 0x1f00)
    iram = strict_dump(section(text, 5), 0, 256)
    alias = strict_dump(section(text, 6), 0x1f00, 256)
    # Byte7 is genuinely unwritten by the C probe, NOT an invented zero result.
    require(ram[0x1e00:0x1e08] == b"CAB1\x01\0\0\xa5", "CHILD ABI exact probe result shape")
    address = layout.symbols["_child_abi_checks"]
    require(int.from_bytes(ram[address:address + 4], "little") == CHECKS,
            "CHILD ABI exact43098 completed checks")
    require(iram[0x7d:] == b"\xc7" * 131, "CHILD ABI stack canary")
    require(alias == iram, "CHILD ABI actual IRAM alias decoding")
    allowed = layout.ordinary | frozenset(range(0x1e00, 0x1e08))
    require(all(v == 0xa5 for a, v in enumerate(ram) if a not in allowed),
            "CHILD ABI unowned XDATA/status reservation")
    return CHECKS, PEAK


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True,
                        help="ABI directory containing child_abi.ihx (BUILD/mac-link-child-workspace/abi)")
    parser.add_argument("--simulator", default="s51")
    args = parser.parse_args()
    artifacts = load(args.output)
    layout = verify(artifacts)
    check_alias(args.simulator)
    count, peak = observation(simulate(args.simulator, commands(), artifacts.path), layout)
    print(f"CHILD ABI-only: {count} checks; {SIZE} ROM, {XDATA}+64 XDATA; "
          f"SP{peak:02X}/{SP_CAP:02X}, complete raw image/metadata/snapshot guards PASS. "
          "Not combined PHY/SP or hardware acceptance.")


if __name__ == "__main__":
    main()
