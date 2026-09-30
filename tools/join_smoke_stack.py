#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Static stack bounds for the actual linked join, not execution acceptance."""
import hashlib
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tests"))
from boot_security_resident import branch
from verify_banked_join import direct_accesses
from verify_firmware import require


def linked_bytes(image, start, size):
    require(all(a in image for a in range(start, start + size)), "Missing stack-proof CODE")
    return bytes(image[a] for a in range(start, start + size))


def special_entries(image, symbols, graph):
    decoded, _, functions, entries, _, _ = graph

    def body(module, name):
        matches = [e for e, identity in entries.items() if identity[:2] == (module, name)]
        require(len(matches) == 1, "Missing/ambiguous special stack entry")
        entry = matches[0]
        pcs = sorted(pc for pc, owner in functions.items() if owner == entry)
        end = pcs[-1] + len(decoded[pcs[-1]])
        require(sum(len(decoded[pc]) for pc in pcs) == end - entry, "Special stack body has a hole")
        return entry, linked_bytes(image, entry, end - entry)

    require((symbols["_banked_depth"], symbols["_banked_fault"]) == (0x1e, 0x1f),
            "Banker stack model requires its physical DATA reservations")
    stop, raw = body("banked", "stop")
    require(raw == bytes.fromhex("c2aff51f80fe") and symbols["_banked_fault_entry"] == stop
            and symbols["_banked_stop"] == stop + 4, "Changed retained banker failure")
    call, call_raw = body("banked", "_sdcc_banked_call")
    ret, ret_raw = body("banked", "_sdcc_banked_ret")
    # The sole relocations are LJMPs to the retained fault entry. These exact
    # trampolines prove +5 peak/+3 resident bytes per far call, +2 on return.
    call_expected = bytes.fromhex(
        "abd0ac81bc5100405bbc7a005056c0e0c003e5d054187042e592703ee5c754f87038"
        "e59f54f87032e51ec394085030ea603754f87033e930e72fba0705c394e85027051e"
        "d0d0d0e0c09fc0e08a9fe59f6a701bd0e0c000c00122"
        "74020200007403020000740402000074010200007405020000")
    ret_expected = bytes.fromhex(
        "abd0ac81bc52004045bc7b005040c0e0c003e5d05418702ce5927028e5c754f87022"
        "e51e6023c39409501e151ed0d0d0e0d000c0e0e854f87019889fe59f687012d0e022"
        "7402020000740302000074040200007405020000")
    jump = b"\x02" + stop.to_bytes(2, "big")
    require(call_raw == call_expected.replace(b"\x02\0\0", jump)
            and ret_raw == ret_expected.replace(b"\x02\0\0", jump)
            and symbols["__sdcc_banked_call"] == call and symbols["__sdcc_banked_ret"] == ret,
            "Changed bank-call/return stack ABI")
    template, raw = body("flash_exec", "flash_exec_template")
    require(len(raw) == 123 and hashlib.sha256(raw).hexdigest() ==
            "87ac19a19ee72052c542b9b159adc1d1f2618e506324522ffb0ebfd8db504f8d"
            and symbols["_flash_exec_template_end"] == template + len(raw),
            "Changed copied flash engine/fail-stop")
    enter, raw = body("flash_exec", "enter_ram")
    work, ram = symbols["_flash_exec_work"], symbols["_flash_exec_ram"]
    require(raw == bytes((0x7a, work & 255, 0x7b, work >> 8, 0x90))
            + (ram + 0x8000).to_bytes(2, "big") + b"\xe4\x73",
            "Changed flash XMAP entry")
    return {stop, call, ret}, {stop, stop + 4}, {enter + 8: template}


def startup(image, symbols):
    require((symbols["s_SSEG"], symbols["l_SSEG"], symbols["s_XSEG"],
             symbols["s_HOME"], symbols["l_HOME"], symbols["___gptr_cmp"]) ==
            (0x50, 45, 0, 0, 52, 6), "Changed reset/stack allocation")
    require(all(symbols["l_" + a] == 0 for a in ("XINIT", "XISEG", "PSEG")),
            "Unreviewed startup initialization storage")
    size = symbols["l_XSEG"]
    require(0 < size <= 0x1e00 and symbols["s_XISEG"] == size, "Startup XDATA exceeds ordinary RAM")
    external = symbols["__sdcc_external_startup"]
    require(linked_bytes(image, 0, 6) ==
            b"\x02\0\x34\x02" + symbols["_main"].to_bytes(2, "big"),
            "Changed reset/main vectors")
    require(symbols["__sdcc_gsinit_startup"] == 52
            and linked_bytes(image, external, 9) == bytes.fromhex("75a80075b800759a00"),
            "Startup does not disable interrupts before calls")
    expected = {
        "GSINIT0": b"\x75\x81\x4f", "GSINIT1": b"",
        "GSINIT2": b"\x12" + external.to_bytes(2, "big") + bytes.fromhex("e5826003020003"),
        "GSINIT3": bytes.fromhex("7900e94400601b7a0090") + symbols["s_XINIT"].to_bytes(2, "big")
        + bytes((0x78, size & 255, 0x75, 0x93, size >> 8))
        + bytes.fromhex("e493f2a308b800020593d9f4daf27593ff"),
        "GSINIT4": bytes.fromhex("e478fff6d8fd7800e84400600a7900759300e4f309d8fc")
        + bytes((0x78, size & 255, 0xe8, 0x44, size >> 8, 0x60, 0x0c, 0x79, (size + 255) >> 8))
        + bytes.fromhex("900000e4f0a3d8fcd9fa"),
        "GSINIT5": b"", "GSINIT": b"", "GSFINAL": b"\x02\0\x03",
    }
    address = 52
    for area, raw in expected.items():
        require(symbols["s_" + area] == address and symbols["l_" + area] == len(raw)
                and linked_bytes(image, address, len(raw)) == raw, "Changed stack/startup CRT: " + area)
        address += len(raw)
    require(symbols["s_CSEG"] == address, "Unclassified startup CODE")


def instruction_depth(raw, depth):
    op = raw[0]
    _, writes = direct_accesses(raw)
    if op in (0xc0, 0xd0):
        require(raw[1] != 0x81, "PUSH/POP of SP is not a stack frame")
        depth += 1 if op == 0xc0 else -1
    elif op in (0x05, 0x15) and raw[1] == 0x81:
        depth += 1 if op == 0x05 else -1
    else:
        require(0x81 not in writes, "Unclassified SP write")
    require(depth >= 0, "Stack underflow")
    if writes & {0xa8, 0xb8, 0x9a}:
        require(op == 0x75 and raw[2] == 0, "Interrupt enable invalidates foreground stack bound")
    if op in (0x10, 0x92, 0xb2, 0xc2, 0xd2) and raw[1] & 0xf8 in (0xa8, 0xb8):
        require(op in (0x10, 0xc2), "Interrupt-bit enable invalidates foreground stack bound")
    require(op != 0x32, "ISR is outside the foreground stack proof")
    return depth


def stack_paths(symbols, graph, runtime, excluded, stops, indirect):
    decoded, owners, original_functions, original_entries, targets, calls = graph
    functions, entries = dict(original_functions), dict(original_entries)
    for name in runtime:
        entry = symbols[name]
        require(entry not in entries and owners[entry] == "libc", "Runtime stack entry identity")
        entries[entry] = ("libc", name, False)
    for pc, owner in owners.items():
        if owner == "libc":
            prior = [symbols[n] for n in runtime if symbols[n] <= pc]
            require(prior, "Unclassified runtime stack instruction")
            functions[pc] = max(prior)
    require(all(pc in functions or pc in (0, 3, symbols.get("s_GSFINAL")) for pc in decoded),
            "Unowned stack instruction")
    require(set(indirect) == {pc for pc, raw in decoded.items() if raw[0] == 0x73},
            "Unclassified computed stack transfer")
    memo, active = {}, set()

    def need(entry):
        require(entry in entries and entry not in excluded, "Unclassified stack callee")
        require(entry not in active, "Recursive stack path")
        if entry in memo:
            return memo[entry]
        active.add(entry)
        pending, depths, peak, banks, path = [entry], {entry: 0}, 0, 0, []
        while pending:
            pc = pending.pop()
            require(pc in decoded and functions.get(pc) == entry, "Stack control flow escapes function")
            raw, depth = decoded[pc], depths[pc]
            after = instruction_depth(raw, depth)
            if after > peak:
                peak, path = after, [{"function": ":".join(entries[entry][:2]), "site": pc, "bytes": after}]
            next_pcs = [pc + len(raw)]
            target = targets.get(pc)

            def include(callee, resident, minimum=0, far=False):
                nonlocal peak, banks, path
                child_peak, child_banks, child_path = need(callee)
                total = depth + max(minimum, resident + child_peak)
                banks = max(banks, int(far) + child_banks)
                if total > peak:
                    peak = total
                    path = [{"function": ":".join(entries[entry][:2]), "site": pc,
                             "bytes": depth + resident}] + child_path

            if pc in calls:
                far = raw[0] == 0x12 and branch(pc, raw) == symbols["__sdcc_banked_call"]
                include(calls[pc], 3 if far else 2, 5 if far else 0, far)
            elif target == symbols["__sdcc_banked_ret"]:
                require(raw[0] == 2 and entries[entry][2] and depth == 0,
                        "Unbalanced/nonbanked far return")
                if depth + 2 > peak:
                    peak, path = depth + 2, [{"function": ":".join(entries[entry][:2]),
                                            "site": pc, "bytes": depth + 2}]
                next_pcs = []
            elif target in stops:
                if raw[0] in (2, 0x80) or raw[0] & 31 == 1:
                    next_pcs = []
            elif raw[0] == 0x22:
                require(depth == 0 and not entries[entry][2], "Unbalanced/ordinary far RET")
                next_pcs = []
            elif pc in indirect:
                require(depth == 0, "Unbalanced flash-engine tail entry")
                include(indirect[pc], 0)
                next_pcs = []
            elif target is not None:
                unconditional = raw[0] in (2, 0x80) or raw[0] & 31 == 1
                if functions.get(target) != entry:
                    require(unconditional and target in entries and owners[pc] == owners[target] == "libc",
                            "Unclassified inter-function stack transfer")
                    require(depth == 0, "Unbalanced runtime tail entry")
                    include(target, 0)
                    next_pcs = []
                else:
                    next_pcs = [target] if unconditional else next_pcs + [target]
            for next_pc in next_pcs:
                if next_pc in depths:
                    require(depths[next_pc] == after, "Unbalanced stack merge/loop")
                else:
                    depths[next_pc] = after
                    pending.append(next_pc)
        active.remove(entry)
        memo[entry] = peak, banks, path
        return memo[entry]

    for entry in entries.keys() - excluded:
        need(entry)
    return memo


def analyze_stack(image, symbols, graph, runtime):
    startup(image, symbols)
    excluded, stops, indirect = special_entries(image, symbols, graph)
    bounds = stack_paths(symbols, graph, runtime, excluded, stops, indirect)
    roots = {}
    for name, incoming in (("_main", 0), ("__sdcc_external_startup", 2)):
        peak, banks, path = bounds[symbols[name]]
        require(peak + incoming <= 45, f"Linked {name} stack bound {peak + incoming} exceeds 45")
        require(banks <= 8, "Linked bank nesting exceeds retained banker capacity")
        roots[name] = {"bytes": peak + incoming, "bank_depth": banks, "path": path}
    return {"functions": len(bounds), "initial_sp": 0x4f, "limit_sp": 0x7c,
            "maximum_sp": 0x4f + max(r["bytes"] for r in roots.values()), "roots": roots}
