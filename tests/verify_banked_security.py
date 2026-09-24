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
    "f9ad03896a76b77ae1e22a775cbc91c61c9ab7ad506034bddbf121a7e405e96f",
    "062f9c0f046993cb1f034db700cf44a5219dee8579aaaf089af7b1509611046f",
    "dd1cc7f6adcd61331fc9d093dabea2098ed6b7f785e2bd49257ab19dbb4bbc27",
    "ed2059b8cbbb60bb0fbf12d8b44558c84c4008bf7b35eab7afae948a9aa4f725",
    "e3bc9800544bcd2911ecbd20b64935977550f6d0c295129d09e3eb952ee74be3",
    "b253251f333f27a50c39c0f9f8e954ce614bc0a059a6639177fe472443a54454",
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
    require(len(image) == len(banking.pack(image)) == 48936, "Complete banked security CODE extent changed")
    areas = ("HOME", "GSINIT0", "GSINIT1", "GSINIT2", "GSINIT3", "GSINIT4", "GSINIT5",
             "GSINIT", "GSFINAL", "CSEG", "CONST", "BK_KEYS", "BK_WIRE")
    covered = set()
    for area in areas:
        span = set(range(symbols["s_"+area], symbols["s_"+area]+symbols["l_"+area]))
        require(not span & covered, "Banked security CODE areas overlap")
        covered |= span
    require(covered == set(image), "Missing/unassigned banked security CODE")
    require((symbols["s_BK_KEYS"], symbols["l_BK_KEYS"], symbols["s_BK_WIRE"], symbols["l_BK_WIRE"]) ==
            (0x18000, 17039, 0x28000, 6880), "Banked owner/wire placement changed")
    require((symbols["s_XSEG"], symbols["l_XSEG"], symbols["s_SSEG"], symbols["l_SSEG"],
             symbols["s_OSEG"], symbols["l_OSEG"], symbols["s_BSEG_BYTES"], symbols["l_BSEG_BYTES"],
             symbols["_banked_depth"], symbols["_banked_fault"], symbols["_fixture_status"]) ==
            (0, 3886, 0x52, 43, 0x48, 10, 0x20, 3, 0x1e, 0x1f, 0x1e00),
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
    require(offset == 3866 and symbols["___memcpy_PARM_2"] == offset,
            "Complete source/libc scratch boundary changed")
    require(storage == set(range(offset)), "Source XDATA has an ownership hole")
    require(symbols["l_XSEG"]-offset == 20 and symbols["__gptrput_PARM_2"] == 3877,
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
    return decoded
