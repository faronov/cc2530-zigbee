# SPDX-License-Identifier: BSD-3-Clause
"""Strict resource ledger for the isolated SDCC protocol integration image."""

import re

from verify_firmware import CODE_LIMIT, STATUS_ADDRESS, STATUS_RESERVED, require, xdata_ranges


MODULE_BUDGETS = {
    "mac_frame": (7680, 256, 16),
    "nwk_frame": (2560, 128, 12),
    "aps_frame": (2048, 96, 8),
    "zcl_frame": (1536, 64, 8),
    "zcl_value": (2048, 64, 16),
    "zcl_attributes": (2304, 192, 8),
    "zcl_dispatch": (3328, 160, 16),
    "zcl_write": (1792, 160, 0),
    "protocol_budget_test": (4096, 896, 0),
}
CODE_BUDGET = 24576
XDATA_BUDGET = 2048
STACK_START_LIMIT = 0x68
CODE_AREAS = {"CSEG", "CONST", "HOME", "GSFINAL", "GSINIT", *(f"GSINIT{i}" for i in range(6))}
FLAGS = {
    **{name: 0x20 for name in CODE_AREAS},
    "DSEG": 0, "OSEG": 4, "XSEG": 0x40, "BSEG": 0x80,
    "REG_BANK_0": 4, "SSEG": 0,
}


def object_resources(text, module):
    require(text.splitlines().count("XH3") == 1, "Unsupported relocatable format")
    expected_module = "test_protocol_budget" if module == "protocol_budget_test" else module
    require(re.findall(r"^M (\S+)$", text, re.MULTILINE) == [expected_module], "Object module mismatch")
    require(re.findall(r"^O (.+)$", text, re.MULTILINE) == ["-mmcs51 --model-large"],
            "Object is not the expected SDCC large-model ABI")
    areas = {}
    for line in text.splitlines():
        if not line.startswith("A "):
            continue
        match = re.fullmatch(r"A (\S+) size ([0-9A-F]+) flags ([0-9A-F]+) addr ([0-9A-F]+)", line)
        require(match is not None, "Malformed object area")
        name, size, flags, address = match[1], int(match[2], 16), int(match[3], 16), int(match[4], 16)
        require(name not in areas, "Duplicate object area")
        require(address == 0, "Unexpected absolute object allocation")
        if size:
            require(name in FLAGS and flags == FLAGS[name], f"Unsupported nonempty object area {name}")
        areas[name] = size
    require("CSEG" in areas and "XSEG" in areas and "DSEG" in areas, "Missing object areas")
    require(areas.get("REG_BANK_0") == 8, "Unexpected register-bank reservation")
    require(areas.get("SSEG", 0) == (1 if module == "protocol_budget_test" else 0),
            "Unexpected object stack reservation")
    return {
        "code": sum(areas.get(name, 0) for name in CODE_AREAS),
        "xdata": areas["XSEG"],
        "iram_persistent": areas["DSEG"],
        "iram_overlay": areas.get("OSEG", 0),
        "bits": areas.get("BSEG", 0),
    }


def build_report(image, symbols, objects, peak_sp):
    require(set(objects) == set(MODULE_BUDGETS), "Integrated module set mismatch")
    code = len(image)
    require(image and min(image) == 0 and max(image) < CODE_LIMIT
            and set(image) == set(range(code)), "Integrated CODE is not contiguous lower unbanked")
    require(code <= CODE_BUDGET, "Integrated CODE budget exceeded")
    xdata = sum(end - start for start, end in xdata_ranges(symbols))
    require(xdata + STATUS_RESERVED <= XDATA_BUDGET, "Integrated XDATA budget exceeded")
    require(symbols["s_SSEG"] <= STACK_START_LIMIT, "Integrated stack-start budget exceeded")
    require(8 <= symbols["s_SSEG"] <= peak_sp < 128, "Invalid observed stack peak or upper-IRAM crossing")
    rows = {}
    for module, budgets in MODULE_BUDGETS.items():
        row = object_resources(objects[module], module)
        for key, limit in zip(("code", "xdata", "iram_persistent"), budgets):
            require(row[key] <= limit, f"{module} {key} budget exceeded")
        require(row["iram_overlay"] <= 16, f"{module} overlay budget exceeded")
        rows[module] = {**row, "limits": dict(zip(("code", "xdata", "iram_persistent"), budgets))}
    object_code = sum(row["code"] for row in rows.values())
    object_xdata = sum(row["xdata"] for row in rows.values())
    require(object_code <= code and object_xdata <= xdata, "Object resources exceed linked totals")
    shared_code, shared_xdata = code - object_code, xdata - object_xdata
    require(shared_code <= 1024 and shared_xdata <= 128, "Unexpected shared runtime resource growth")
    persistent = sum(row["iram_persistent"] for row in rows.values())
    overlay = max(row["iram_overlay"] for row in rows.values())
    bits = sum(row["bits"] for row in rows.values())
    require(symbols["l_OSEG"] == overlay and symbols["l_BSEG"] == bits,
            "Object overlay/bit totals disagree with link")
    padding = symbols["s_SSEG"] - persistent - overlay - 8 - (bits + 7) // 8
    require(0 <= padding <= 7, "Unaccounted IRAM allocation or excessive packing gap")
    return {
        "schema": 1,
        "scope": "Offline MAC/NWK/APS/ZCL codecs and read/discovery/write dispatch; not a full Zigbee stack",
        "hardware_tested": False,
        "modules": rows,
        "shared_runtime": {"code": shared_code, "xdata": shared_xdata},
        "linked": {
            "code": code, "code_budget": CODE_BUDGET, "unbanked_remaining": CODE_LIMIT - code,
            "ordinary_xdata": xdata, "status_reserved": STATUS_RESERVED,
            "xdata_with_status": xdata + STATUS_RESERVED, "xdata_budget": XDATA_BUDGET,
            "ordinary_allocation_remaining": STATUS_ADDRESS - xdata,
            "iram_persistent": persistent, "iram_overlay_shared": overlay,
            "iram_register_bank": 8, "iram_bit_bytes": (bits + 7) // 8, "iram_packing_gap": padding,
            "stack_start": symbols["s_SSEG"], "stack_start_limit": STACK_START_LIMIT,
            "observed_peak_sp": peak_sp, "observed_stack_bytes": peak_sp - symbols["s_SSEG"] + 1,
            "observed_headroom_to_guard": 127 - peak_sp,
        },
        "excluded": [
            "Radio/platform services and real TX/RX queues",
            "NWK Beacon metadata decoder and live MAC/NWK/APS state machines",
            "AES/CCM, keys/counters, durable NV and commissioning",
            "ZDO, transaction/binding/reporting tables and device-specific clusters",
            "ISR nesting, sleep, sensor/display application and FMAP banking",
        ],
        "limitations": [
            "Object areas include compiler scratch and unused functions; overlay rows are not additive.",
            "Shared runtime is the linked remainder, not attributed to any one protocol.",
            "RAM includes a synthetic two-peer harness and five separate frame buffers, not production pools.",
            "Peak SP covers executed vectors, not a proved worst case or interrupt allowance.",
            "Remaining address space is not a promise that the complete stack fits.",
        ],
    }
