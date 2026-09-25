#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Exact banked adapter layout and typed public-observation bridge."""
import argparse
from pathlib import Path
import re

import boot_banked as banking
from boot_mac_attempt import mmio_sites
from boot_nwk_candidates import records
from clock_fixture import verify_clock_code
from verify_banked_join import Field, transfers, live_data
from verify_firmware import (cdb_address, parse_ihex, parse_symbols, peripheral_accesses,
                             require, verify_timebase_reader)

LOWER = ("timebase", "clock", "mac_time", "radio_autoack", "mac_epoch", "mac_radio", "mac_attempt")
MODULES = ("mac_adapter_iram_low", "banked", "mac_adapter_iram_high")+LOWER+(
    "mac_frame", "mac_tx", "mac_adapter", "mac_adapter_fixture")
ROOTS = {
    "tx": "mac_tx_interval_t", "action": "mac_tx_interval_action_t",
    "random": "mac_tx_interval_event_t", "clock": "mac_epoch_stamp_t",
    "through": "mac_epoch_stamp_t", "config": "radio_autoack_config_t",
    "observation": "mac_adapter_observation_t", "diagnostics": "mac_adapter_diagnostics_t",
    "record": "mac_attempt_record_t",
}
RUNTIME = ("___memcpy", "_memset", "__gptrput", "__gptrget", "__mullong")
SCALARS = {**dict.fromkeys(("op", "return", "policy", "selector", "random_byte", "length", "dsn"), 1),
           **dict.fromkeys(("limit", "work", "config_ptr", "tx_ptr", "action_ptr", "clock_ptr", "through_ptr"), 2),
           **dict.fromkeys(("timeout", "lifetime", "token"), 4)}
PINS = (
    "f839c6115d93846fd7dc97bed1b1b461fe0c8420f8cb766cc3192e769992118f",
    "b2993132dc575a5f5612f9f396e9c9254fc926a12d2360ddeb8d9dff7760c55d",
    "d8c489a2f6564ed27bbcd44ba43099157e5e21e92e80d5cdd5f0fe13369603a5",
    "f92082525df19d7f6fb49a5f9ff5d9c583140b02eb6b5cfec1098fd1595b892e",
    "eeb9a714c1d8091157bac5495fbf3714041f61592a743f1de52fef9afabf3fd8",
    "1d6050c0e5c6623927bbfc3a6b085666746d811f8655d0149376d0db40853d58",
)


def load(output):
    path = output/"mac-adapter"/"mac_adapter.ihx"
    return (parse_ihex(path.read_text()), parse_symbols(path.with_suffix(".map").read_text()),
            path.with_suffix(".cdb").read_bytes(), path.with_suffix(".mem").read_bytes(),
            {m: (path.parent/f"mac_adapter.{m}.rst").read_bytes() for m in MODULES},
            {m: (path.parent/f"{m}.rel").read_bytes() for m in MODULES})


def check_data(artifacts):
    image, symbols, debug, _, listings, _ = artifacts
    graph = transfers(image, symbols, listings, debug.decode("ascii"), modules=MODULES,
                      areas=("CSEG", "MA_BANK1", "MA_BANK2"), library=RUNTIME, indirect_sites=())
    areas = {m: "MA_CALLER" if m == "mac_adapter_fixture" else "MA_"+m for m in MODULES}
    return live_data(symbols, debug.decode("ascii"), listings, *graph, modules=MODULES,
                     reservations=set(range(8, 0x1e)) | set(range(0x23, 0x4c)),
                     overlay=set(range(0x4c, 0x56)), frame_areas=areas,
                     physical=("mac_adapter_iram_low", "mac_adapter_iram_high"))


def artifact_bytes(*artifacts):
    return banking.artifact_bytes(*artifacts, modules=MODULES)


def verify(*artifacts):
    banking.pin_artifacts(artifact_bytes(*artifacts), PINS)
    image, symbols, raw, memory, listings, _ = artifacts
    require(len(image) == len(banking.pack(image)) == 55442, "Adapter physical CODE accounting")
    areas = ("HOME", "GSINIT0", "GSINIT1", "GSINIT2", "GSINIT3", "GSINIT4", "GSINIT5",
             "GSINIT", "GSFINAL", "CSEG", "CONST", "MA_BANK1", "MA_BANK2")
    covered = set()
    for area in areas:
        span = set(range(symbols["s_"+area], symbols["s_"+area]+symbols["l_"+area]))
        require(not span & covered, "Adapter CODE areas overlap")
        covered |= span
    require(covered == set(image) and symbols["s_CONST"]+symbols["l_CONST"] <= 0x8000,
            "Missing/unassigned or implicitly banked CODE/constants")
    for bank in (1, 2):
        require(symbols[f"s_MA_BANK{bank}"] == (bank << 16)+0x8000 and
                0 < symbols[f"l_MA_BANK{bank}"] <= 0x8000, "Adapter bank placement")
    require((symbols["s_SSEG"], symbols["l_SSEG"], symbols["s_OSEG"], symbols["l_OSEG"],
             symbols["s_BSEG_BYTES"], symbols["l_BSEG_BYTES"], symbols["_banked_depth"],
             symbols["_banked_fault"], symbols["_fixture_status"], symbols["s_XSEG"], symbols["l_XSEG"]) ==
            (0x56, 0x27, 0x4c, 10, 0x20, 3, 0x1e, 0x1f, 0x1e00, 0, 2848),
            "Adapter physical RAM/IRAM/status ABI")
    require(b"16 bit mode initial stack starts at: 0x56 (sp set to 0x55) with 39 bytes available." in memory,
            "Adapter CPU return-address/stack ABI")
    require(all(symbols["l_"+a] == 0 for a in ("XABS", "XISEG", "XINIT", "PSEG", "ISEG", "IABS", "BIT_BANK")),
            "Unaccounted adapter storage")
    require((symbols["s_REG_BANK_0"], symbols["l_REG_BANK_0"], symbols["__XPAGE"]) == (0, 8, 0x93),
            "Adapter register bank/MPAGE")
    debug = raw.decode("ascii")
    retained = re.findall(r"^S:([FG][^(]+)\(\{(\d+)\}.*\),E,0,0$", debug, re.M)
    require({(k.split("$")[1], int(n)) for k, n in retained} ==
            {("mac_adapter_iram_low", 22), ("mac_adapter_iram_high", 41), ("banked_depth", 1), ("banked_fault", 1)},
            "Adapter retained DATA entered compiler frames")
    require(symbols["_mac_adapter_iram_low"] == 8 and symbols["_mac_adapter_iram_high"] == 0x23,
            "Missing actual DATA reservations")
    require(not any(n.startswith(("_host_", "_traced_", "_aes_reference")) for n in symbols),
            "Synthetic/native services entered target CODE")
    spans, storage, offset = {}, set(), 0
    for module in MODULES:
        text = listings[module].decode("ascii")
        require(f".module {module}" in text, "Wrong adapter relocated snapshot")
        segment = text.split(".area XSEG    (XDATA)", 1)[1].split(".area XABS", 1)[0]
        owned = set()
        for address, size in re.findall(r"^\s+([0-9A-F]{6})\s+\d+\s+\.ds (\d+)$", segment, re.M):
            span = set(range(int(address, 16), int(address, 16)+int(size)))
            require(span and not span & storage, "Adapter source/compiler XDATA overlaps")
            owned |= span; storage |= span
        require(owned == set(range(offset, offset+len(owned))), "Adapter source XDATA has unowned holes")
        spans[module] = owned; offset += len(owned)
    runtime = {"___memcpy_PARM_2": 0, "___memcpy_PARM_3": 3, "_memset_PARM_2": 8,
               "_memset_PARM_3": 9, "__gptrput_PARM_2": 11, "__mullong_PARM_2": 12}
    require(symbols["l_XSEG"]-offset == 16 and all(symbols[n] == offset+i for n, i in runtime.items()),
            "Adapter complete libc scratch suffix, including multiplication")
    shared, radio_end, attempt_end, adapter_end = (symbols[n] for n in (
        "_mac_radio_shared_end", "_mac_radio_reserved_end", "_mac_attempt_reserved_end", "_mac_adapter_reserved_end"))
    require(set().union(*(spans[m] for m in MODULES[:8])) == set(range(shared)) and
            spans["mac_radio"] == set(range(shared, radio_end+1)) and
            spans["mac_attempt"] == set(range(radio_end+1, attempt_end+1)) and
            set().union(*(spans[m] for m in MODULES[10:13])) == set(range(attempt_end+1, adapter_end+1)) and
            spans["mac_adapter_fixture"] == set(range(adapter_end+1, offset)),
            "Adapter complete private-prefix/caller ownership")
    schema = Layout(raw, symbols)
    caller = set()
    for name, field in schema.globals.items():
        if name == "fixture_status":
            continue
        span = set(range(symbols["_"+name], symbols["_"+name]+field.size))
        require(not caller & span, "Adapter caller objects overlap")
        caller |= span
    require(caller == spans["mac_adapter_fixture"], "Unaccounted caller byte")
    for module in ("mac_epoch", "mac_radio", "mac_attempt", "mac_frame", "mac_tx", "mac_adapter", "mac_adapter_fixture"):
        code = dict(records(listings[module].decode("ascii")))
        require(not peripheral_accesses(code) and not any(
            r[0] == 0x90 and 0x6000 <= int.from_bytes(r[1:], "big") < 0x6400 for r in code.values()),
            "Upper adapter/protocol layer bypasses the genuine hardware services")
    verify_timebase_reader(image, symbols, debug, 0x1e00, 8)
    verify_clock_code(image, symbols, debug)
    require(len(mmio_sites(image, debug, {m: r.decode("ascii") for m, r in listings.items()}, handoff=True)) == 165,
            "Actual peripheral instruction inventory changed")
    require(check_data(artifacts) == (146, 634), "Adapter DATA call-lifetime inventory changed")


class Layout:
    def __init__(self, raw, symbols):
        text = raw.decode("ascii")
        self.symbols, self.types, self.globals = symbols, {}, {}
        pattern = r"\(\{(\d+)\}S:S\$([A-Za-z_]\w*)\$0_0\$0\(\{(\d+)\}([^)]*)\),Z,0,0\)"
        for name, body in re.findall(r"^T:Fmac_adapter_fixture\$(__\d+)\[(.*)\]$", text, re.M):
            fields = tuple(Field(int(a), n, int(s), k) for a, n, s, k in re.findall(pattern, body))
            require(fields and re.sub(pattern, "", body) == "" and name not in self.types,
                    "Incomplete/duplicate adapter public type")
            self.types[name] = fields
        for name, size, shape in re.findall(r"^S:G\$(fixture_\w+)\$0_0\$0\(\{(\d+)\}([^)]*)\),F,0,0$", text, re.M):
            require(name not in self.globals, "Duplicate adapter public object")
            self.globals[name] = Field(0, name, int(size), shape)
            require(symbols["_"+name] == cdb_address(text, "L:G$"+name+"$0_0$0"),
                    "Adapter caller map/CDB address differs")
        require({"fixture_"+n for n in (*ROOTS, *SCALARS, "status", "packet")} == self.globals.keys(),
                "Missing/unreviewed adapter caller roots")
        for name, size in {**SCALARS, "status": 8, "packet": 125}.items():
            require(self.globals["fixture_"+name].size == size, "Caller scalar/array extent changed")

    def fields(self, value):
        match = re.fullmatch(r"ST(__\d+):S", value.shape)
        require(match and match[1] in self.types, "Unresolved adapter struct type")
        fields = self.types[match[1]]
        occupied = set()
        for field in fields:
            span = set(range(field.offset, field.offset+field.size))
            require(span and not span & occupied, "Overlapping adapter fields")
            occupied |= span
        require(occupied == set(range(value.size)), "Adapter struct has an unaccounted byte")
        return fields

    def at(self, name, *path):
        value = self.globals["fixture_"+name]
        offset = 0
        for member in path:
            matches = [f for f in self.fields(value) if f.name == member]
            require(len(matches) == 1, "Missing adapter field")
            value = matches[0]; offset += value.offset
        return Field(offset, value.name, value.size, value.shape)

    def header(self):
        lines = ["/* Generated from the actual adapter CDB. Never hand-edit. */",
                 "#ifndef MAC_ADAPTER_LAYOUT_H", "#define MAC_ADAPTER_LAYOUT_H"]
        names = ("_mac_radio_shared_end", "_mac_radio_reserved_end", "_mac_attempt_reserved_end",
                 "_mac_radio_raw", "_mac_radio_config", "_mac_attempt_raw", "_mac_attempt_first",
                 "__gptrput_PARM_2", "_mac_radio_handoff_clock", "_mac_adapter_receipt",
                 "_mac_adapter_live", "_mac_adapter_reserved_end")
        for name in names + tuple("_"+n for n in self.globals):
            lines.append(f"#define TARGET{name} {self.symbols[name]}u")
        for suffix, path in (("frame", ("frame",)), ("body", ("frame", "body"))):
            lines.append(f"#define TARGET_adapter_{suffix} "
                         f"{self.symbols['_mac_adapter_receipt']+self.at('record', *path).offset}u")
        for root, typename in ROOTS.items():
            value = self.globals["fixture_"+root]
            lines += [f"static void emit_adapter_{root}(const {typename} *p)", "{",
                      f"    unsigned char raw[{value.size}] = {{0}};"]

            def walk(field, expression, offset):
                array = re.fullmatch(r"DA(\d+)d,(.+)", field.shape)
                if array:
                    count = int(array[1])
                    require(count and field.size % count == 0, "Invalid adapter array")
                    width = field.size//count
                    for i in range(count):
                        walk(Field(0, "", width, array[2]), f"{expression}[{i}]", offset+i*width)
                elif field.shape.startswith("ST"):
                    for child in self.fields(field):
                        walk(child, expression+"."+child.name, offset+child.offset)
                elif field.shape.startswith(("DG,", "DX,")):
                    require(field.size in (2, 3), "Unexpected adapter pointer width")
                    lines.append(f"    emit_pointer(raw+{offset}, {expression}, {field.size});")
                else:
                    require(re.fullmatch(r"S[CIKL]:[US]", field.shape) and field.size in (1, 2, 4),
                            "Unsupported adapter public scalar")
                    lines.append(f"    emit_number(raw+{offset}, {expression}, {field.size});")

            walk(value, "(*p)", 0)
            lines += ["    hex(raw, sizeof(raw));", "}", ""]
        return "\n".join(lines+["#endif", ""])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--emit-header", type=Path, required=True)
    args = parser.parse_args()
    artifacts = load(args.output)
    verify(*artifacts)
    args.emit_header.write_text(Layout(artifacts[2], artifacts[1]).header())


if __name__ == "__main__":
    main()
