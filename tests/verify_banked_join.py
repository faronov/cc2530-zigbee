#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Complete-join artifact loading and typed native/SDCC observation bridge."""
import argparse
from dataclasses import dataclass
from pathlib import Path
import re

import boot_banked as banking
from boot_nwk_candidates import records
from boot_security_resident import LENGTHS, branch
from verify_firmware import cdb_address, parse_ihex, parse_symbols, require


COMMON = ("flash_exec", "flash", "flash_write", "nv_record", "security_counter", "timebase",
          "aes", "ccm_star", "zigbee_mmo", "zigbee_key_hash", "mac_frame", "banked")
BANKS = (
    ("nwk_frame", "aps_frame", "ed_wire", "security_keys"),
    ("mac_tx", "mac_poll", "mac_association", "mac_join", "zdo_node", "zdo_srv"),
    ("nwk_beacon", "nwk_candidates", "nwk_parent", "mac_scan", "bdb_join", "bdb_join_init"),
    ("nwk_aps", "nwk_aps_transmit", "zdo_runtime"),
)
MODULES = ("banked_join_iram_low", "banked_join_iram_high")+COMMON+sum(BANKS, ())+("banked_join_fixture",)
RUNTIME_XDATA = {
    "___memcpy_PARM_2": 0, "___memcpy_PARM_3": 3,
    "_memset_PARM_2": 8, "_memset_PARM_3": 9, "__gptrput_PARM_2": 11,
    "__mulint_PARM_2": 12, "__mullong_PARM_2": 14,
    "_memcmp_PARM_2": 18, "_memcmp_PARM_3": 21,
}
PINS = (
    "f0c422066541ab494ab4b4b97c86999711d324df10e3e3c23e7d60fc6904f481",
    "70e4279f044aeecf199d369fa254dd3bdfa867f3eaba0fc5904c3e295ae675e5",
    "846963470968d49a1ea27c222439b9c667d10d7378eab04cc2e6abc78e1f5b8c",
    "00c720b1e064c3e388bc1cbe97fa826e4e504d704901483e0625de5f48f1a9bb",
    "21f8ffe924a5f1084a2a42b5f8b0d1db4ee38a8975f4ccb49b7ba25aa33c90ff",
    "46647bc44be093823f8a942aab9aa50aec68fff2626ff758799f2a7e3cdd282f",
)


def load(output):
    path = output/"banked-join"/"banked_join.ihx"
    return (parse_ihex(path.read_text()), parse_symbols(path.with_suffix(".map").read_text()),
            path.with_suffix(".cdb").read_bytes(), path.with_suffix(".mem").read_bytes(),
            {m: (path.parent/f"banked_join.{m}.rst").read_bytes() for m in MODULES},
            {m: (path.parent/f"{m}.rel").read_bytes() for m in MODULES})


def artifact_bytes(*artifacts):
    return banking.artifact_bytes(*artifacts, modules=MODULES)


def verify(*artifacts):
    banking.pin_artifacts(artifact_bytes(*artifacts), PINS)
    require(check_layout(artifacts) == (279, 1262), "Complete join DATA/OSEG lifetime proof changed")


def transfers(image, symbols, listings, debug):
    decoded, owners, covered = {}, {}, set()
    for module, listing in listings.items():
        for pc, raw in records(listing.decode("ascii")):
            require(raw[0] != 0xa5 and len(raw) == LENGTHS[raw[0]], "Malformed actual join instruction")
            span = set(range(pc, pc+len(raw)))
            require(not span & covered and bytes(image[a] for a in range(pc, pc+len(raw))) == raw,
                    "Actual join instruction overlaps or differs from linked CODE")
            decoded[pc], owners[pc] = raw, module
            covered |= span
    library = ("___memcpy", "_memset", "__gptrput", "__gptrget", "__mulint", "__mullong", "_memcmp")
    pc = min(symbols[n] for n in library)
    end = symbols["s_CSEG"]+symbols["l_CSEG"]
    while pc < end:
        size = LENGTHS[image[pc]]
        span = set(range(pc, pc+size))
        require(pc+size <= end and not span & covered, "Runtime instruction escapes CODE ownership")
        decoded[pc], owners[pc] = bytes(image[a] for a in range(pc, pc+size)), "libc"
        covered |= span
        pc += size
    for area in ("CSEG", "BJ_BANK1", "BJ_BANK2", "BJ_BANK3", "BJ_BANK4"):
        require(set(range(symbols["s_"+area], symbols["s_"+area]+symbols["l_"+area])) <= covered,
                "Incomplete actual join CODE decode")
    entries, functions = {}, {}
    for scope, name, width in re.findall(r"^F:(G|F[^$]+)\$([^$]+)\$0_0\$0\(\{([23])\}DF,", debug, re.M):
        entry = cdb_address(debug, f"L:{scope}${name}$0$0")
        last = cdb_address(debug, f"L:X{scope}${name}$0$0")
        require(entry in decoded and last in decoded, "Function lacks linked entry/end instruction")
        module = owners[entry]
        require(scope == "G" or scope == "F"+module, "Private function/module identity differs")
        require(entry not in entries and entry <= last, "Duplicated/reversed function boundary")
        entries[entry] = (module, name, width == "3")
    for module in MODULES:
        starts = sorted(e for e, (m, _, _) in entries.items() if m == module)
        for i, start in enumerate(starts):
            stop = starts[i+1] if i+1 < len(starts) else max(p for p, m in owners.items() if m == module)+1
            for pc in decoded:
                if owners[pc] == module and start <= pc < stop:
                    require(pc not in functions, "Instruction has overlapping function owners")
                    functions[pc] = start
    targets, calls, indirect = {}, {}, []
    for pc, raw in decoded.items():
        source, target = owners[pc], branch(pc, raw)
        if raw[0] in (0x73, 0x32):
            indirect.append((pc, raw))
        if target is None:
            continue
        far = raw[0] == 0x12 and target == symbols["__sdcc_banked_call"]
        if far:
            setup = bytes(image[a] for a in range(pc-6, pc))
            require(setup[::2] == b"\x78\x79\x7a", "Unreviewed far-call setup")
            target = setup[1] | setup[3] << 8 | setup[5] << 16
            require(target in entries and entries[target][2], "Far call enters an ordinary helper")
        elif raw[0] in (2, 0x12) or raw[0] & 31 in (1, 17):
            if target >= 0x8000:
                require(pc >= 0x10000, "Common transfer depends on selected bank")
                target |= pc & 0x70000
        if pc == 0:
            require(target == symbols["__sdcc_gsinit_startup"], "Join reset path changed")
            continue
        require(target in decoded, f"Transfer escapes instruction ownership: {pc:x}->{target:x}")
        targets[pc] = target
        if source == "libc":
            require(owners[target] == "libc", "Runtime acquired a callback")
        if owners[target] == "libc" and source != "libc":
            require(target in {symbols[n] for n in library}, "Unreviewed runtime entry")
        if raw[0] == 0x12 or raw[0] & 31 == 17:
            require(target in entries or owners[target] == "libc", "Call enters the middle of a function")
            calls[pc] = target
            if not far and target in entries:
                require(not entries[target][2], "Ordinary call enters a banked function")
        elif target in functions and pc in functions and functions[target] != functions[pc]:
            require(target == symbols["__sdcc_banked_ret"] or source == "banked",
                    "Unreviewed inter-function tail transfer")
    require(indirect == [(0xe5, b"\x73")], "Unreviewed indirect transfer/ISR")
    for entry, (module, name, far) in entries.items():
        if far:
            body = [p for p, owner in functions.items() if owner == entry]
            returns = [p for p in body if decoded[p][0] in (0x22, 0x32) or
                       targets.get(p) == symbols["__sdcc_banked_ret"]]
            require(returns and all(targets.get(p) == symbols["__sdcc_banked_ret"] for p in returns),
                    "Far function bypasses common return")
    return decoded, owners, functions, entries, targets, calls


def direct_accesses(raw):
    op = raw[0]
    if op == 0x85:
        return {raw[1]}, {raw[2]}
    if op in (5, 0x15, 0x42, 0x43, 0x52, 0x53, 0x62, 0x63, 0xc5, 0xd5):
        return {raw[1]}, {raw[1]}
    if op in (0x25, 0x35, 0x45, 0x55, 0x65, 0x95, 0xb5, 0xc0, 0xe5) or 0xa6 <= op <= 0xaf:
        return {raw[1]}, set()
    if op in (0x75, 0xd0, 0xf5) or 0x86 <= op <= 0x8f:
        return set(), {raw[1]}
    return set(), set()


def live_data(symbols, debug, listings, decoded, owners, functions, entries, targets, calls):
    """Backwards byte liveness at every actual linked call, not module overlap."""
    reservations = set(range(8, 0x1e)) | set(range(0x26, 0x46))
    overlay = set(range(0x46, 0x50))
    frames, declared = {}, {m: set() for m in MODULES}
    for module in MODULES:
        if module in ("banked", "banked_join_iram_low", "banked_join_iram_high"):
            continue
        area = "BJ_CALLER" if module == "banked_join_fixture" else "BJ_"+module
        frames[module] = set(range(symbols["s_"+area], symbols["s_"+area]+symbols["l_"+area]))
        require(frames[module] <= reservations, "Compiler frame escapes physically allocated DATA")
    for key, module, size in re.findall(r"^S:(L([^.$]+)\.[^(]+)\(\{(\d+)\}.*\),E,0,0$", debug, re.M):
        require(module in frames and "$sloc" in key, "Unreviewed retained/non-spill DATA")
        address = cdb_address(debug, "L:"+key)
        span = set(range(address, address+int(size)))
        require(not declared[module] & span and span <= frames[module], "Spill declarations overlap/escape")
        declared[module] |= span
    require(all(declared[m] == f for m, f in frames.items()), "Undeclared compiler DATA frame bytes")
    if "banked" in MODULES:
        frames["banked"] = set()
    for module, listing in listings.items():
        require(not re.search(rb"#\(?_\w+_sloc\d+", listing), "Compiler DATA address escapes to an indirect user")
        for segment in re.split(rb"\.area\s+", listing)[1:]:
            if not segment.startswith(b"OSEG "):
                continue
            for address, size in re.findall(rb"^\s+([0-9A-F]{6})\s+\d+\s+\.ds (\d+)$", segment, re.M):
                span = set(range(int(address, 16), int(address, 16)+int(size)))
                require(module in frames and span and span <= overlay, "Compiler overlay escapes physical ownership")
                frames[module] |= span
    for pc, raw in decoded.items():
        if raw[0] in (0x10, 0x20, 0x30, 0x72, 0x82, 0x92, 0xa0, 0xa2, 0xb0, 0xb2, 0xc2, 0xd2):
            require(raw[1] >= 0x80 or raw[1] < symbols["l_BSEG"], "Bit access escapes physical bit ownership")
        if owners[pc] == "libc":
            reads, writes = direct_accesses(raw)
            require(not (reads | writes) & reservations, "Runtime touches reusable compiler DATA")
    tracked = reservations | overlay
    runtime_writes = {}
    terminal = {symbols["__sdcc_banked_ret"], symbols.get("_banked_stop", -1)}
    terminal |= {entry for entry, (module, name, _) in entries.items() if (module, name) == ("banked", "stop")}

    def runtime_modified(start):
        if start not in runtime_writes:
            pending, seen, changed = {start}, set(), set()
            while pending:
                pc = pending.pop()
                if pc in seen:
                    continue
                seen.add(pc)
                require(owners.get(pc) == "libc", "Runtime overlay flow escapes instruction ownership")
                raw = decoded[pc]
                changed |= {("libc", a) for a in direct_accesses(raw)[1] & overlay}
                target = targets.get(pc)
                if target is not None:
                    pending.add(target)
                if raw[0] not in (2, 0x22, 0x32, 0x73, 0x80) and raw[0] & 31 != 1:
                    pending.add(pc+len(raw))
            runtime_writes[start] = changed
        return runtime_writes[start]

    bodies = {}
    for pc, entry in functions.items():
        module, name, _ = entries[entry]
        if module in frames and (module != "banked" or name == "banked_code_read"):
            bodies.setdefault(entry, []).append(pc)
    visited, active, conflicts = {}, set(), 0

    def analyze(entry):
        nonlocal conflicts
        if entry in visited:
            return visited[entry]
        require(entry not in active, "Recursive compiler DATA lifetime")
        active.add(entry)
        module = entries[entry][0]
        body = sorted(bodies[entry])
        uses, defines, successors, modified, callees = {}, {}, {}, set(), {}
        for pc in body:
            raw = decoded[pc]
            read, write = direct_accesses(raw)
            relevant = (read | write) & tracked
            require(relevant <= frames[module], "Linked direct access escapes its module frame")
            uses[pc] = {(module, a) for a in read & tracked}
            defines[pc] = {(module, a) for a in write & tracked}
            modified |= defines[pc]
            following = pc+len(raw)
            successors[pc] = {following} if following in functions and functions[following] == entry else set()
            target = targets.get(pc)
            if pc in calls:
                if target in bodies:
                    require(not analyze(target)[0], "Callee reads uninitialized/retained shared DATA")
                    writes = visited[target][1]
                    callees[pc] = writes
                    modified |= writes
                else:
                    require(owners[target] == "libc", "Unaccounted compiler DATA callee")
                    callees[pc] = runtime_modified(target)
                    modified |= callees[pc]
            elif target in terminal or raw[0] in (0x22, 0x32, 0x73):
                successors[pc] = set()
            elif target is not None:
                require(functions.get(target) == entry, "Unclassified compiler-frame tail transfer")
                if raw[0] in (2, 0x80) or raw[0] & 31 == 1:
                    successors[pc] = set()
                successors[pc].add(target)
        live = {pc: set() for pc in body}
        changed = True
        while changed:
            changed = False
            for pc in reversed(body):
                after = set().union(*(live[p] for p in successors[pc]))
                before = uses[pc] | (after-defines[pc])
                if before != live[pc]:
                    live[pc] = before
                    changed = True
        require(not live[entry], f"Uninitialized/retained shared DATA in {entries[entry][:2]}")
        for pc, writes in callees.items():
            after = set().union(*(live[p] for p in successors[pc]))
            require(not {a for _, a in after} & {a for _, a in writes},
                    f"Live DATA overwritten across actual call {pc:x}")
            conflicts += len(after)*len(writes)
        active.remove(entry)
        visited[entry] = (live[entry], modified)
        return visited[entry]

    for entry in bodies:
        analyze(entry)
    return len(visited), conflicts


def check_layout(artifacts):
    image, symbols, raw, memory, listings, objects = artifacts
    require(len(image) == len(banking.pack(image)) and len(image) <= 5*0x8000,
            "Join CODE does not fit its common/four-bank envelope")
    areas = ("HOME", "GSINIT0", "GSINIT1", "GSINIT2", "GSINIT3", "GSINIT4", "GSINIT5",
             "GSINIT", "GSFINAL", "CSEG", "CONST", "BJ_BANK1", "BJ_BANK2", "BJ_BANK3", "BJ_BANK4")
    covered = set()
    for area in areas:
        span = set(range(symbols["s_"+area], symbols["s_"+area]+symbols["l_"+area]))
        require(not span & covered, "Join CODE areas overlap")
        covered |= span
    require(covered == set(image), "Unassigned/missing linked CODE")
    require((symbols["s_SSEG"], symbols["l_SSEG"], symbols["s_OSEG"], symbols["l_OSEG"],
             symbols["s_BSEG_BYTES"], symbols["l_BSEG_BYTES"], symbols["_banked_depth"],
             symbols["_banked_fault"], symbols["_fixture_status"]) ==
            (0x50, 0x2d, 0x46, 10, 0x20, 6, 0x1e, 0x1f, 0x1e00),
            "Physical join IRAM/status ABI changed")
    require(symbols["s_XSEG"] == 0 and symbols["l_XSEG"] <= 0x1e00, "Ordinary RAM crosses status/IRAM alias")
    require(b"16 bit mode initial stack starts at: 0x50 (sp set to 0x4f) with 45 bytes available." in memory,
            "CPU return-address/stack ABI changed")
    require(all(symbols["l_"+a] == 0 for a in ("XABS", "XISEG", "XINIT", "PSEG", "ISEG", "IABS", "BIT_BANK")),
            "Unaccounted join storage")
    require((symbols["s_REG_BANK_0"], symbols["l_REG_BANK_0"], symbols["__XPAGE"]) == (0, 8, 0x93),
            "Register bank/MPAGE ABI changed")
    for number in range(1, 5):
        require(symbols[f"s_BJ_BANK{number}"] == (number << 16)+0x8000 and
                symbols[f"l_BJ_BANK{number}"] <= 0x8000, "Bank placement escaped its physical window")
    debug = raw.decode("ascii")
    retained = re.findall(r"^S:([FG][^(]+)\(\{(\d+)\}.*\),E,0,0$", debug, re.M)
    require({(k.split("$")[1], int(n)) for k, n in retained} ==
            {("banked_join_iram_low", 22), ("banked_join_iram_high", 32), ("banked_depth", 1), ("banked_fault", 1)},
            "Retained DATA entered compiler frames")
    for name, address in (("banked_join_iram_low", 8), ("banked_join_iram_high", 0x26)):
        require(symbols["_"+name] == address, "Actual physical reservation moved")
    schema = Schema(raw)
    require(schema.globals["fixture_status"].size == 64 and
            schema.globals["fixture_device"].size == 1676 and
            schema.at("fixture_device", "work").size == 1290, "Complete caller/status extent changed")
    shadow = schema.at("fixture_device", "work", "association", "staged")
    require(shadow.offset == shadow.size == 645 and
            symbols["_mac_join_staged"] == symbols["_fixture_device"]+shadow.offset,
            "Association shadow is not backed by its actual phase field")
    require(not any(n.startswith(("_host_", "_aes_reference", "_security_joint")) for n in symbols),
            "Host services entered target CODE")
    storage, offset = set(), 0
    for module in MODULES:
        text = listings[module].decode("ascii")
        segment = text.split(".area XSEG    (XDATA)", 1)[1].split(".area XABS", 1)[0]
        owned = set()
        for address, size in re.findall(r"^\s+([0-9A-F]{6})\s+\d+\s+\.ds (\d+)$", segment, re.M):
            span = set(range(int(address, 16), int(address, 16)+int(size)))
            require(span and not span & storage, "Source/compiler XDATA overlaps")
            owned |= span
            storage |= span
        require(owned == set(range(offset, offset+len(owned))), "Source XDATA has unowned holes")
        offset += len(owned)
    require(symbols["l_XSEG"]-offset == 26 and
            all(symbols[name] == offset+relative for name, relative in RUNTIME_XDATA.items()),
            "Complete libc XDATA scratch boundary changed")
    caller = set()
    for name, field in schema.globals.items():
        if name == "fixture_status":
            continue
        address = symbols["_"+name]
        span = set(range(address, address+field.size))
        require(span <= storage and not span & caller, "Caller objects overlap or escape ordinary XDATA")
        caller |= span
    require(symbols["s_CONST"]+symbols["l_CONST"] <= 0x8000,
            "Constant data depends on an implicit CODE bank")
    require(banking.sha(bytes(image[a] for a in range(0x62, 0xdd))) ==
            "87ac19a19ee72052c542b9b159adc1d1f2618e506324522ffb0ebfd8db504f8d",
            "Copied flash RAM engine changed")
    graph = transfers(image, symbols, listings, debug)
    return live_data(symbols, debug, listings, *graph)


@dataclass(frozen=True)
class Field:
    offset: int
    name: str
    size: int
    shape: str


class Schema:
    """Only public fixture types, never CDB-provided source or C expressions."""
    def __init__(self, raw):
        text = raw.decode("ascii")
        self.types = {}
        pattern = r"\(\{(\d+)\}S:S\$([A-Za-z_]\w*)\$0_0\$0\(\{(\d+)\}([^)]*)\),Z,0,0\)"
        for name, body in re.findall(r"^T:Fbanked_join_fixture\$(__\d+)\[(.*)\]$", text, re.M):
            fields = tuple(Field(int(a), n, int(s), k) for a, n, s, k in re.findall(pattern, body))
            require(fields and re.sub(pattern, "", body) == "" and name not in self.types,
                    "Incomplete/duplicate public fixture type")
            self.types[name] = fields
        self.globals = {}
        for name, size, shape in re.findall(r"^S:G\$(fixture_\w+)\$0_0\$0\(\{(\d+)\}([^)]*)\),F,0,0$", text, re.M):
            require(name not in self.globals, "Duplicate fixture object")
            self.globals[name] = Field(0, name, int(size), shape)
        require({"fixture_device", "fixture_mac", "fixture_input", "fixture_output", "fixture_status"}
                <= self.globals.keys(), "Incomplete fixture objects")

    def fields(self, value):
        match = re.fullmatch(r"ST(__\d+):S", value.shape)
        require(match and match[1] in self.types, "Unknown public aggregate type")
        fields = self.types[match[1]]
        require(all(0 <= f.offset and f.size > 0 and f.offset+f.size <= value.size for f in fields),
                "Public field escapes object")
        return fields

    def at(self, root, *path):
        value = self.globals[root]
        offset = 0
        for name in path:
            matches = [f for f in self.fields(value) if f.name == name]
            require(len(matches) == 1, "Missing/duplicate public field")
            value = matches[0]
            offset += value.offset
        return Field(offset, value.name, value.size, value.shape)

    def roots(self):
        return {
            "device": ("bdb_join_t", self.globals["fixture_device"]),
            "mac": ("mac_tx_t", self.globals["fixture_mac"]),
            "config": ("bdb_join_config_t", self.at("fixture_input", "config")),
            "event": ("bdb_join_event_t", self.at("fixture_input", "receive", "event")),
            "action": ("bdb_join_action_t", self.at("fixture_output", "action")),
            "radio": ("mac_tx_action_t", self.at("fixture_output", "radio")),
            "packet": ("ed_packet_t", self.at("fixture_output", "packet")),
            "metadata": ("security_keys_status_t", self.at("fixture_status", "metadata")),
            "identity": ("security_keys_config_t", self.at("fixture_input", "provision", "config")),
            "limits": ("ccm_star_limits_t", self.at("fixture_input", "provision", "limits")),
        }

    def children(self, root, path, field):
        fields = self.fields(field)
        if root == "device" and path == ("work",):
            selected = {"scan": "BDB_JOIN_WORK_SCAN", "association": "BDB_JOIN_WORK_ASSOCIATION",
                        "runtime": "BDB_JOIN_WORK_RUNTIME"}
            require({f.name for f in fields} == selected.keys(), "BDB phase union changed")
            return "workspace", [(selected[f.name], f) for f in fields]
        if root in ("event", "action") and path == ("data",):
            names = ("scan", "association", "tx", "installed" if root == "event" else "install")
            prefix = "BDB_JOIN_EVENT_" if root == "event" else "BDB_JOIN_ACTION_"
            suffix = ("SCAN", "ASSOCIATION", "TX", "INSTALLED" if root == "event" else "INSTALL")
            require({f.name for f in fields} == set(names), "BDB I/O union changed")
            tags = dict(zip(names, (prefix+s for s in suffix)))
            return "kind", [(tags[f.name], f) for f in fields]
        if root == "device" and path == ("work", "association"):
            require([(f.name, f.offset, f.size) for f in fields] == [("context", 0, 645), ("staged", 645, 645)],
                    "Association phase backing changed")
            return None, [(None, fields[0])]
        occupied = set()
        for child in fields:
            span = set(range(child.offset, child.offset+child.size))
            require(not span & occupied, "Unexpected union in public observation")
            occupied |= span
        require(occupied == set(range(field.size)), "Unaccounted public padding")
        return None, [(None, f) for f in fields]

    def emit_header(self):
        lines = ["/* Generated from the actual target CDB; public synthetic observation only. */",
                 "#ifndef BANKED_JOIN_LAYOUT_H", "#define BANKED_JOIN_LAYOUT_H"]
        for name in ("input", "output", "device", "mac"):
            lines.append(f"#define JOIN_{name.upper()}_SIZE {self.globals['fixture_'+name].size}u")
        for name, (_, value) in self.roots().items():
            if name not in ("device", "mac"):
                lines.append(f"#define JOIN_{name.upper()}_SIZE {value.size}u")
        lines.append(f"#define JOIN_BYTES_OFFSET {self.at('fixture_input', 'receive', 'bytes').offset}u")
        for name in ("config", "limits", "nwk", "aps", "polls", "install"):
            lines.append(f"#define JOIN_PROVISION_{name.upper()} {self.at('fixture_input', 'provision', name).offset}u")
        for root, (ctype, value) in self.roots().items():
            lines += [f"static void pack_{root}(const {ctype} *p)", "{",
                      f"    memset(packed, 0, {value.size});"]

            def walk(field, expression, offset, path, indent):
                nonlocal lines
                array = re.fullmatch(r"DA(\d+)d,(.+)", field.shape)
                if array:
                    count = int(array[1])
                    require(count and field.size % count == 0 and count <= 256, "Invalid public array")
                    size = field.size//count
                    for i in range(count):
                        walk(Field(0, "", size, array[2]), f"{expression}[{i}]", offset+i*size, path, indent)
                elif field.shape.startswith("ST"):
                    tag, children = self.children(root, path, field)
                    if tag:
                        lines.append(indent+f"switch (p->{tag}) {{")
                    for case, child in children:
                        if case:
                            lines.append(indent+f"case {case}:")
                        walk(child, expression+"."+child.name, offset+child.offset, path+(child.name,),
                             indent+("    " if tag else ""))
                        if case:
                            lines.append(indent+"    break;")
                    if tag:
                        lines += [indent+"default: break;", indent+"}"]
                elif field.shape.startswith(("DG,", "DX,")):
                    require(field.size in (2, 3), "Unexpected pointer width")
                    helper = "put_frame_pointer" if root == "event" else "put_owner_pointer"
                    lines.append(indent+f"{helper}({offset}, {expression}, {field.size});")
                else:
                    require(re.fullmatch(r"S[CIKL]:[US]", field.shape) and field.size in (1, 2, 4),
                            f"Unsupported public scalar {field.shape}")
                    lines.append(indent+f"put_number({offset}, {expression}, {field.size});")

            walk(value, "(*p)", 0, (), "    ")
            lines += ["}", ""]
        return "\n".join(lines+["#endif", ""])

    def leaves(self, root, data):
        value = self.roots()[root][1]
        require(len(data) == value.size, "Incomplete public object")

        def walk(field, offset, path):
            array = re.fullmatch(r"DA(\d+)d,(.+)", field.shape)
            if array:
                count = int(array[1])
                require(count and field.size % count == 0, "Invalid public array")
                size = field.size//count
                for i in range(count):
                    yield from walk(Field(0, "", size, array[2]), offset+i*size, path)
            elif field.shape.startswith("ST"):
                tag, children = self.children(root, path, field)
                selected = None
                if tag:
                    tag_field = next(f for f in self.fields(value) if f.name == tag)
                    selected = int.from_bytes(data[tag_field.offset:tag_field.offset+tag_field.size], "little")
                tags = {"scan": 1, "association": 2, "runtime": 3, "tx": 3, "installed": 4, "install": 4}
                for case, child in children:
                    number = tags.get(child.name)
                    if tag is None or selected == number:
                        yield from walk(child, offset+child.offset, path+(child.name,))
            else:
                yield Field(offset, ".".join(path), field.size, field.shape)

        return tuple(walk(value, 0, ()))

    def canonical(self, root, data, owner, frame):
        result = bytearray(len(data))
        for field in self.leaves(root, data):
            value = data[field.offset:field.offset+field.size]
            if field.shape.startswith(("DG,", "DX,")):
                expected = frame if root == "event" else owner
                address = int.from_bytes(value, "little")
                require(address in (0, expected), f"Invalid live public pointer {root}.{field.name}")
                value = int(address != 0).to_bytes(field.size, "little")
            result[field.offset:field.offset+field.size] = value
        return bytes(result)

    def event_input(self, data, frame):
        result = bytearray(data)
        size = self.roots()["event"][1].size
        for field in self.leaves("event", data[:size]):
            if field.shape.startswith(("DG,", "DX,")):
                value = int.from_bytes(data[field.offset:field.offset+field.size], "little")
                require(value in (0, 1), "Invalid native pointer marker")
                result[field.offset:field.offset+field.size] = (frame if value else 0).to_bytes(field.size, "little")
        return bytes(result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--emit-header", required=True, type=Path)
    args = parser.parse_args()
    artifacts = load(args.output)
    args.emit_header.write_text(Schema(artifacts[2]).emit_header())


if __name__ == "__main__":
    main()
