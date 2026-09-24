#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Complete banked key-image identity, physical ownership and transfer proof."""
import re

import boot_banked as banking
import boot_security_resident as resident
from boot_nwk_candidates import records
from verify_firmware import cdb_address, parse_ihex, parse_symbols, require


LOWER = ("flash_exec", "flash", "flash_write", "nv_record", "security_counter", "timebase",
         "aes", "ccm_star", "zigbee_mmo", "zigbee_key_hash", "nwk_frame", "aps_frame")
RESERVATIONS = ("banked_security_iram_low", "banked_security_iram_high")
MODULES = RESERVATIONS+LOWER+("banked", "ed_wire", "security_keys", "banked_security_fixture")
FRAMES = dict(zip(LOWER+("ed_wire", "security_keys", "banked_security_fixture"), (
    ("BS_EXEC", 8, 8), ("BS_READ", 16, 4), ("BS_WRITE", 20, 5), ("BS_NV", 35, 23),
    ("BS_COUNTER", 58, 14), ("BS_TIME", 8, 0), ("BS_AES", 35, 32), ("BS_CCM", 8, 0),
    ("BS_MMO", 8, 18), ("BS_HASH", 67, 3), ("BS_NWK", 35, 12), ("BS_APS", 35, 8),
    ("BS_WIRE", 8, 9), ("BS_KEYS", 26, 4), ("BS_CALLER", 26, 0))))
PINS = (
    "4a34660071d0a6014d0f2b55c8d225fa5469fac57f4f1d813ea11a5a7c9378c0",
    "9ab6b704a317c0c0a53d4e6ff6cd9a7883676ffd10d889fd1a97b1cc4211d457",
    "78a890a4657092589ca6a66a0383c3bdd4836462eaddeff893ec3378467ce5dd",
    "e8c0022283b3007cd4ed17ca89785561c94205780f7bef05599de02d3e15e8cd",
    "852ccf062a4f48df7c33ac613702c9215c4cde0afd983a92417c69ea7b9e8983",
    "b9b4740f9dbe215e3e82fdfb551fd3cbdb24dd7fb4a29616df1a0d949a152ca8",
)
EDGES = (resident.EDGES - {e for e in resident.EDGES if e[0] == "zigbee_security"}) | {
    ("ed_wire", "nwk_frame"), ("ed_wire", "aps_frame"), ("ed_wire", "ccm_star"),
    ("security_keys", "security_counter"), ("security_keys", "zigbee_key_hash"),
    ("security_keys", "zigbee_mmo"), ("security_keys", "ed_wire"),
    ("banked_security_fixture", "security_keys"),
}


def load(output):
    path = output/"banked-security"/"banked_security.ihx"
    return (parse_ihex(path.read_text()), parse_symbols(path.with_suffix(".map").read_text()),
            path.with_suffix(".cdb").read_bytes(), path.with_suffix(".mem").read_bytes(),
            {m: (path.parent/f"banked_security.{m}.rst").read_bytes() for m in MODULES},
            {m: (path.parent/f"{m}.rel").read_bytes() for m in MODULES})


def artifact_bytes(*artifacts):
    return banking.artifact_bytes(*artifacts, modules=MODULES)


def verify(image, symbols, debug_raw, memory, listings, objects):
    banking.pin_artifacts(artifact_bytes(image, symbols, debug_raw, memory, listings, objects), PINS)
    require(len(image) <= 51200 and symbols["l_XSEG"]+64 <= 4096, "Banked key image exceeds its own budgets")
    require(len(image) == len(banking.pack(image)) == 49261, "Complete banked security CODE extent changed")
    areas = ("HOME", "GSINIT0", "GSINIT1", "GSINIT2", "GSINIT3", "GSINIT4", "GSINIT5",
             "GSINIT", "GSFINAL", "CSEG", "CONST", "BK_KEYS", "BK_WIRE")
    covered = set()
    for area in areas:
        span = set(range(symbols["s_"+area], symbols["s_"+area]+symbols["l_"+area]))
        require(not span & covered, "Banked security CODE areas overlap")
        covered |= span
    require(covered == set(image), "Missing/unassigned banked security CODE")
    require((symbols["s_BK_KEYS"], symbols["l_BK_KEYS"], symbols["s_BK_WIRE"], symbols["l_BK_WIRE"]) ==
            (0x18000, 17039, 0x28000, 7205), "Banked owner/wire placement changed")
    require((symbols["s_XSEG"], symbols["l_XSEG"], symbols["s_SSEG"], symbols["l_SSEG"],
             symbols["s_OSEG"], symbols["l_OSEG"], symbols["s_BSEG_BYTES"], symbols["l_BSEG_BYTES"],
             symbols["_banked_depth"], symbols["_banked_fault"], symbols["_fixture_status"]) ==
            (0, 3555, 0x52, 43, 0x48, 10, 0x20, 3, 0x1e, 0x1f, 0x1e00),
            "Physical banked security IRAM/XDATA allocation changed")
    require(b"16 bit mode initial stack starts at: 0x52 (sp set to 0x51) with 43 bytes available." in memory,
            "CPU stack/return-address ABI changed")
    require(all(symbols["l_"+a] == 0 for a in ("XABS", "XISEG", "XINIT", "PSEG", "ISEG", "IABS", "BIT_BANK")),
            "Unaccounted banked security storage")
    require(not any(n.startswith(("_host_", "_aes_reference", "_security_joint")) for n in symbols),
            "Host peripheral/crypto model entered target CODE")
    debug = debug_raw.decode("ascii")
    require("S:G$fixture_status$0_0$0({64}DA64d,SC:U),F,0,0" in debug and
            (symbols["s_REG_BANK_0"], symbols["l_REG_BANK_0"]) == (0, 8),
            "Status reservation/register-bank ABI changed")
    frames = {m: (a, n) for m, (_, a, n) in FRAMES.items()}
    reservations = set(range(8, 30)) | set(range(35, 72))
    for m, base, size in zip(RESERVATIONS, (8, 35), (22, 37)):
        require(symbols["_"+m] == base and
                f"S:G${m}$0_0$0({{{size}}}DA{size}d,SC:U),E,0,0" in debug,
                "Actual physical DATA reservation changed")
    for m, (area, base, size) in FRAMES.items():
        require((symbols["s_"+area], symbols["l_"+area]) == (base, size) and
                set(range(base, base+size)) <= reservations, "Named DATA frame escapes physical ownership")
        if m in LOWER:
            normalized = objects[m].split(b"\n", 1)[1].replace(f"A {area} size ".encode(), b"A DSEG size ")
            require(banking.sha(normalized) == resident.OBJECTS[m][0],
                    f"Original lower object {m} changed beyond DATA area naming")
    retained = re.findall(r"^S:([FG][^(]+)\(\{(\d+)\}.*\),E,0,0$", debug, re.M)
    require({(key.split("$")[1], int(size)) for key, size in retained} ==
            {(RESERVATIONS[0], 22), (RESERVATIONS[1], 37), ("banked_depth", 1), ("banked_fault", 1)},
            "Retained DATA entered reusable frames")
    for key, module, size in re.findall(r"^S:(L([^.$]+)\.[^(]+)\(\{(\d+)\}.*\),E,0,0$", debug, re.M):
        address = cdb_address(debug, "L:"+key)
        base, extent = frames[module]
        require("$sloc" in key and set(range(address, address+int(size))) <= set(range(base, base+extent)),
                "DATA contains unreviewed retained/local storage")
    decoded, owners, storage, coverage, offset = {}, {}, set(), set(), 0
    for m in MODULES:
        text = listings[m].decode("ascii")
        for pc, raw in records(text):
            require(raw[0] != 0xa5 and len(raw) == resident.LENGTHS[raw[0]],
                    "Malformed banked security instruction")
            span = set(range(pc, pc+len(raw)))
            require(not span & coverage and bytes(image[pc+i] for i in range(len(raw))) == raw,
                    "Overlapping or mismatched linked instruction")
            coverage |= span
            decoded[pc], owners[pc] = raw, m
        segment = text.split(".area XSEG    (XDATA)", 1)[1].split(".area XABS", 1)[0]
        owned = set()
        for a, size in re.findall(r"^\s+([0-9A-F]{6})\s+\d+\s+\.ds (\d+)$", segment, re.M):
            span = set(range(int(a, 16), int(a, 16)+int(size)))
            require(span and not span & storage, "Compiler/private XDATA overlaps")
            owned |= span; storage |= span
        require(owned == set(range(offset, offset+len(owned))), "Noncontiguous source XDATA ownership")
        offset += len(owned)
    require(offset == 3535 and symbols["___memcpy_PARM_2"] == offset,
            "Complete source/libc scratch boundary changed")
    require(storage == set(range(offset)), "Source XDATA has an ownership hole")
    require(symbols["l_XSEG"]-offset == 20 and symbols["__gptrput_PARM_2"] == 3546,
            "Complete libc scratch (not only gptrput) changed")
    pc = symbols["___memcpy"]
    end = symbols["s_CSEG"]+symbols["l_CSEG"]
    entries = {symbols[n] for n in resident.LIBRARY if n in symbols}
    while pc < end:
        raw = bytes(image[a] for a in range(pc, pc+resident.LENGTHS[image[pc]]))
        span = set(range(pc, pc+len(raw)))
        require(not span & coverage and pc+len(raw) <= end, "Libc/source CODE overlap")
        coverage |= span
        direct = ([1, 2] if raw[0] == 0x85 else [1] if
                  raw[0] in (5, 0x15, 0x25, 0x35, 0x42, 0x43, 0x45, 0x52, 0x53, 0x55,
                             0x62, 0x63, 0x65, 0x75, 0x95, 0xb5, 0xc0, 0xc5, 0xd0, 0xd5, 0xe5, 0xf5)
                  or 0x86 <= raw[0] <= 0x8f or 0xa6 <= raw[0] <= 0xaf else [])
        require(all(raw[i] >= 0x80 or raw[i] < 8 or 0x48 <= raw[i] <= 0x51 for i in direct),
                "Libc accesses reusable/persistent DATA")
        if raw[0] in (0x10, 0x20, 0x30, 0x72, 0x82, 0x92, 0xa0, 0xa2, 0xb0, 0xb2, 0xc2, 0xd2):
            require(raw[1] >= 0x80 or raw[1] < symbols["l_BSEG"], "Libc bit access escapes ownership")
        decoded[pc], owners[pc] = raw, "libc"
        pc += len(raw)
    for area in ("CSEG", "BK_KEYS", "BK_WIRE"):
        require(set(range(symbols["s_"+area], symbols["s_"+area]+symbols["l_"+area])) <= coverage,
                "Incomplete actual CODE instruction decode")
    edges, far_calls, indirect = set(), [], []
    for pc, raw in decoded.items():
        source, target = owners[pc], resident.branch(pc, raw)
        if raw[0] in (0x73, 0x32):
            indirect.append((pc, raw))
        if target is None:
            continue
        if raw[0] == 0x12 and target == symbols["__sdcc_banked_call"]:
            setup = bytes(image[a] for a in range(pc-6, pc))
            require(setup[::2] == b"\x78\x79\x7a", "Unknown banked indirect call ABI")
            target = setup[1] | (setup[3] << 8) | (setup[5] << 16)
            far_calls.append((pc, target))
        elif raw[0] in (2, 0x12) and target >= 0x8000:
            require(pc >= 0x10000, "Common absolute transfer depends on FMAP")
            target |= pc & 0x70000
        if pc == 0:
            require(target == symbols["__sdcc_gsinit_startup"], "Reset path changed")
            continue
        require(target in owners, f"Unowned transfer {pc:x}->{target:x}")
        dest = owners[target]
        if source == "libc":
            require(dest == "libc", "Libc acquired a callback")
        elif dest == "libc":
            require(target in entries, "Unreviewed libc entry")
        elif source != dest and dest != "banked":
            edges.add((source, dest))
    require(indirect == [(0xe5, b"\x73")], "Unreviewed indirect jump/ISR in serialized key image")
    require(edges == EDGES, f"Active call graph changed: added {edges-EDGES}, removed {EDGES-edges}")
    resident.active_frames(edges, frames)
    public = {name for name in symbols if name.startswith(("_ed_wire_", "_security_keys_")) and
              "_PARM_" not in name}
    require(len(public) == 15 and far_calls, "Incomplete banked public API")
    for name in public:
        prefix = "G$"+name[1:]+"$0$0"
        entry, last = cdb_address(debug, "L:"+prefix), cdb_address(debug, "L:X"+prefix)
        require(entry == symbols[name] and entry >= 0x10000 and
                f"F:G${name[1:]}$0_0$0({{3}}DF,SC:U),Z,0,0,0,0,0" in debug,
                "Banked public function identity/ABI changed")
        require(decoded[last] == b"\x02"+symbols["__sdcc_banked_ret"].to_bytes(2, "big"),
                "Banked public return bypassed the common trampoline")
    require(all(target in {symbols[n] for n in public} for _, target in far_calls),
            "Far call enters an ordinary helper")
    require(banking.sha(bytes(image[a] for a in range(0x62, 0xdd))) ==
            "87ac19a19ee72052c542b9b159adc1d1f2618e506324522ffb0ebfd8db504f8d",
            "Real copied flash RAM engine changed")
    wire_work(debug, decoded, owners)
    return decoded


def wire_work(debug, decoded, owners):
    """Real union objects, disjoint metadata and complete internal transfers.

    Public operations finish/wipe only after their private readers return.
    Those readers cannot clear a parent's still-live encoded/decoded buffer.
    Full byte identities bind the reviewed load/store ordering; execution
    checks operands/results and all three returning work regions separately.
    This function is not a stack-high-water or cryptographic-success oracle.
    """
    fields = {
        11: ((0, "nwk", 29), (29, "aps", 12), (41, "transmit", 27), (68, "application", 10)),
        12: ((0, "nonce", 13), (13, "written", 1), (14, "info", 26)),
        13: ((0, "wire", 116), (116, "body", 120)),
        14: ((0, "header", 116), (0, "frame", 116)),
        15: ((0, "encoded", 116), (0, "packet", 120), (0, "text", 116)),
    }
    for number, expected in fields.items():
        record = re.findall(rf"^T:Fed_wire\$__{number:08d}\[(.*)\]$", debug, re.M)
        require(len(record) == 1, "Missing/duplicate wire-work field record")
        actual = re.findall(r"\(\{(\d+)\}S:S\$([^$]+)\$0_0\$0\(\{(\d+)\}", record[0])
        require(tuple((int(o), name, int(n)) for o, name, n in actual) == expected,
                "Wire work union/metadata field layout changed")
    storage = set()
    for name, size, number in (("syntax", 78, 11), ("crypto", 40, 12), ("buffers", 236, 13)):
        key = f"Fed_wire${name}$0_0$0"
        require(f"S:{key}({{{size}}}ST__{number:08d}:S),F,0,0" in debug,
                "Wire work is not its actual ordinary-XDATA object")
        a = cdb_address(debug, "L:"+key)
        span = set(range(a, a+size))
        require(not storage & span and max(span) < 0x1e00, "Wire metadata/buffer objects overlap")
        storage |= span

    private = ("counter_value", "wipe", "finish", "read_nwk", "read_aps", "header", "inspect")
    public = tuple("ed_wire_"+n for n in ("nwk", "aps", "decode", "encode", "inspect", "crypt"))
    ranges, function = {}, {}
    for name in private+public:
        prefix = ("Fed_wire$" if name in private else "G$")+name+"$0$0"
        first, last = (cdb_address(debug, "L:"+p+prefix) for p in ("", "X"))
        require(first <= last, "Reversed wire function boundary")
        # SDCC marks these ordinary epilogues at MOV DPL,A, before RET.
        # Account for the real trailing instruction, not an unowned gap.
        if name in ("finish", "header"):
            require(decoded.get(last) == b"\xf5\x82" and decoded.get(last+2) == b"\x22",
                    "Wire ordinary-helper result epilogue changed")
            last += 2
        if name in private:
            require(decoded.get(last) == b"\x22", "Wire private helper does not return normally")
        ranges[name] = (first, last)
        for pc in decoded:
            if first <= pc <= last:
                require(pc not in function and owners[pc] == "ed_wire", "Wire function overlap")
                function[pc] = name
    require(set(function) == {pc for pc in decoded if owners[pc] == "ed_wire"},
            "Incomplete wire helper instruction ownership")
    edges = set()
    for pc, source in function.items():
        target = resident.branch(pc, decoded[pc])
        if target is None:
            continue
        if 0x8000 <= target < 0x10000:
            target |= pc & 0x70000
        if target in function and function[target] != source:
            dest = function[target]
            require(target == ranges[dest][0], "Wire transfer enters another helper's middle")
            edges.add((source, dest))
    expected = {
        ("finish", "wipe"),
        ("ed_wire_nwk", "read_nwk"), ("ed_wire_nwk", "finish"),
        ("ed_wire_aps", "read_aps"), ("ed_wire_aps", "finish"),
        ("ed_wire_decode", "read_nwk"), ("ed_wire_decode", "read_aps"), ("ed_wire_decode", "finish"),
        ("ed_wire_encode", "read_aps"), ("ed_wire_encode", "finish"),
        ("header", "read_nwk"), ("header", "read_aps"),
        ("inspect", "header"), ("inspect", "counter_value"),
        ("ed_wire_inspect", "inspect"), ("ed_wire_inspect", "finish"),
        ("ed_wire_crypt", "inspect"), ("ed_wire_crypt", "header"), ("ed_wire_crypt", "finish"),
    }
    require(edges == expected, f"Wire work lifetime edges changed: {edges ^ expected}")
