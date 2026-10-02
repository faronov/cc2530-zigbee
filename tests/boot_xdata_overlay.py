#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Execute real linked BTR functions with a simulator-only common-CODE caller."""
import argparse
import json
from pathlib import Path
import re
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from boot_banked_join import model
from boot_banked_security import store
from boot_image import check_alias, check_pc, marker, memory_dump, simulate
from boot_radio_tx_fixture import sections
from join_smoke_analysis import call_graph
from verify_firmware import require
from xdata_overlay import verify_study


def execute(root, simulator, *, production=False):
    if production:
        from xdata_build import admit, canonical
        artifacts, proof = admit(canonical(root), "lg_esl29_rev03", "default-tc")
    else:
        artifacts, proof = verify_study(root, "lg_esl29_rev03", "default-tc")
    image, symbols, debug, _, _ = artifacts
    graph = call_graph(*artifacts)
    body = debug.split("M:nwk_aps\n", 1)[1].split("\nM:", 1)[0]
    shape = re.search(r"^S:Lnwk_aps.nwk_aps_broadcast_put\$ctx[^(]+\(\{3\}DG,ST(__\d+):S\)", body, re.M)
    require(shape is not None, "Missing exact target context type")
    table = re.search(r"^T:Fnwk_aps\$" + shape[1] + r"\[(.*)\]$", body, re.M)
    pattern = r"\(\{(\d+)\}S:S\$(\w+)\$0_0\$0\(\{(\d+)\}([^)]*)\),Z,0,0\)"
    require(table is not None and re.sub(pattern, "", table[1]) == "", "Malformed context fields")
    fields = {n: (int(o), int(s), t) for o, n, s, t in re.findall(pattern, table[1])}
    ctx_size = max(o+s for o, s, _ in fields.values())
    ctx, slot = symbols["_join_smoke_device"], symbols["_join_smoke_phase"]
    require(ctx_size <= 1313 and fields["broadcast"][1] == 72 and fields["broadcast_time"][1] == 4,
            "Fixture does not fit actual retained caller backing")
    harness = 0x7ff0
    require(not set(range(harness, 0x8000)).intersection(image), "Harness overlaps production CODE")
    expected = bytearray(ctx_size)
    time_offset = fields["broadcast_time"][0]
    expected[time_offset:time_offset+4] = (1000).to_bytes(4, "little")
    count, observations = 0, []
    with tempfile.TemporaryDirectory(prefix="xdata-mcu-") as directory:
        commands = model(image, Path(directory))
        commands += ["fill iram 0 0xff 0", "fill xram 0 0x1eff 0",
                     "set memory sfr 0xa8 0", "set memory sfr 0xb8 0", "set memory sfr 0x9a 0",
                     "set memory sfr 0xc7 0", "set memory sfr 0x9f 0", "set memory sfr 0xd0 0",
                     "set memory sfr 0x81 0x4f"]
        commands += store("xram", ctx, expected)

        def call(name, parameters, result, expected_slot=None):
            nonlocal count
            entry = symbols["_" + name]
            require(not graph[3][entry][2] and entry >> 16, "Expected a bank-local near entry")
            raw = bytes((0x75, 0x9f, entry >> 16, 0x12))
            raw += (entry & 0xffff).to_bytes(2, "big") + b"\x75\x9f\0\x80\xfe"
            commands.extend(store("flash", harness, raw))
            commands.extend(store("sfr", 0x82, ctx.to_bytes(2, "little")))
            commands.append("set memory sfr 0xf0 0")
            for index, value in enumerate(parameters, 2):
                commands.extend(store("xram", symbols["_" + name + "_PARM_" + str(index)], value))
            commands.extend([f"run {harness:#x} {harness+9:#x}", marker(count*2+10),
                             "state", "dump /h sfr 0x81 0x83", "dump /h iram 0x1e 0x1f",
                             f"dump /h xram {ctx:#x} {ctx+ctx_size-1:#x}",
                             f"dump /h xram {slot:#x} {slot:#x}", marker(count*2+11)])
            observations.append((bytes(expected), result, expected_slot))
            count += 1

        for i in range(8):
            offset = fields["broadcast"][0] + i*9
            expected[offset:offset+9] = ((1100).to_bytes(4, "little") +
                                       (0x100+i).to_bytes(2, "little") + bytes((i+1, 0, 1)))
            call("nwk_aps_broadcast_put", [bytes((i,)), (0x100+i).to_bytes(2, "little"),
                 bytes((i+1,)), (100).to_bytes(4, "little")], None)
            call("nwk_aps_broadcast_slot", [(0x100+i).to_bytes(2, "little"),
                 bytes((i+1,)), (100).to_bytes(4, "little"), slot.to_bytes(2, "little")+b"\0"], 9, 8)
        call("nwk_aps_broadcast_slot", [b"\xff\x01", b"\x55", (100).to_bytes(4, "little"),
                                      slot.to_bytes(2, "little")+b"\0"], 3, 8)
        call("nwk_aps_broadcast_slot", [b"\xff\x01", b"\x55", (1100).to_bytes(4, "little"),
                                      slot.to_bytes(2, "little")+b"\0"], 0, 7)
        parts = sections(simulate(simulator, commands))
    for index, (wanted, result, expected_slot) in enumerate(observations):
        text = parts[index*2+10]
        check_pc(text, harness+9)
        require(memory_dump(text, ctx, ctx_size) == wanted, "BTR semantic result differs")
        require(memory_dump(text, 0x81, 1) == b"\x4f" and memory_dump(text, 0x1e, 2) == b"\0\0",
                "Banker/stack did not return cleanly")
        if result is not None:
            require(memory_dump(text, 0x82, 1) == bytes((result,)), "BTR status differs")
        if expected_slot is not None:
            require(memory_dump(text, slot, 1) == bytes((expected_slot,)), "BTR slot differs")
    return {"calls": count, "functions": ["nwk_aps_broadcast_put", "nwk_aps_broadcast_slot"],
            "xdata": symbols["l_XSEG"], "simulated": True, "hardware_observed": False,
            "scope": "Standalone real-image BTR fill/duplicate/full/expiry scenario; not complete join replay."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--layout", type=Path, required=True)
    parser.add_argument("--simulator", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--xdata-overlay", action="store_true")
    args = parser.parse_args()
    args.output.unlink(missing_ok=True)
    check_alias(args.simulator)
    result = execute(args.layout, args.simulator, production=args.xdata_overlay)
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result))


if __name__ == "__main__":
    main()
