# SPDX-License-Identifier: BSD-3-Clause
"""Object-only compact LINK regression ledger; never a placement/stack proof."""
import argparse
from pathlib import Path
import re

from verify_firmware import require


MODULES = frozenset("""
aes aps_frame bdb_join bdb_join_init ccm_star clock ed_wire flash flash_exec
flash_write mac_adapter mac_association mac_attempt mac_epoch mac_frame mac_join
mac_link_driver mac_poll mac_radio mac_scan mac_time mac_tx nv_record nwk_aps
nwk_aps_transmit nwk_beacon nwk_candidates nwk_frame nwk_parent radio_autoack
security_counter security_keys timebase zdo_node zdo_runtime zdo_srv
zigbee_key_hash zigbee_mmo
""".split())
PROBE = "mac_link_ram_layout"
CODE_AREAS = {"CSEG", "HOME", "GSINIT", "GSFINAL", "MA_BANK2",
              *(f"GSINIT{i}" for i in range(6)), *(f"BJ_BANK{i}" for i in range(1, 5))}
LIMITS = {"code": 211892, "const": 55, "xdata": 7035,
          "data_sum": 519, "overlay_sum": 77, "bits": 70}
WORKSPACE_MODULES = MODULES | {"mac_link_workspace"}
WORKSPACE_LIMITS = {"code": 230576, "const": 962, "xdata": 6308,
                    "data_sum": 556, "overlay_sum": 74, "bits": 80}
WORKSPACE_LAYOUT_BYTES = 617 + 31
CONTEXT_BYTES = 1433 + 180 + 304


def object_areas(text, module):
    require(text.splitlines().count("XH3") == 1, "Unsupported relocatable format")
    require(re.findall(r"^M (\S+)$", text, re.M) == [module], "Object module mismatch")
    require(re.findall(r"^O (.+)$", text, re.M) == ["-mmcs51 --model-large"],
            "Object is not the large-model mcs51 ABI")
    flags = {**dict.fromkeys(CODE_AREAS | {"CONST"}, 0x20),
             "XSEG": 0x40, "BSEG": 0x80, "REG_BANK_0": 4, "OSEG": 4,
             "DSEG": 0, "BJ_" + module: 0, "MA_" + module: 0}
    areas = {}
    for line in text.splitlines():
        if not line.startswith("A "):
            continue
        match = re.fullmatch(r"A (\S+) size ([0-9A-F]+) flags ([0-9A-F]+) addr ([0-9A-F]+)", line)
        require(match is not None, "Malformed object area")
        name, size, mode, address = match[1], *(int(v, 16) for v in match.groups()[1:])
        require(name not in areas and address == 0, "Duplicate or absolute object area")
        require(not size or name in flags and mode == flags[name],
                f"Unsupported nonempty object area {name}")
        areas[name] = size
    require({"CSEG", "CONST", "XSEG", "BSEG"} <= areas.keys(), "Missing object areas")
    frames = {"DSEG", "BJ_" + module, "MA_" + module} & areas.keys()
    require(len(frames) == 1, "Missing or ambiguous compiler DATA frame")
    require(areas.get("REG_BANK_0") == 8, "Unexpected register-bank reservation")
    return {"code": sum(areas.get(a, 0) for a in CODE_AREAS), "const": areas["CONST"],
            "xdata": areas["XSEG"], "data_sum": sum(areas[a] for a in frames),
            "overlay_sum": areas.get("OSEG", 0), "bits": areas["BSEG"]}


def report(objects, *, workspace=False):
    modules = WORKSPACE_MODULES if workspace else MODULES
    limits = WORKSPACE_LIMITS if workspace else LIMITS
    probe_bytes = CONTEXT_BYTES + (WORKSPACE_LAYOUT_BYTES if workspace else 0)
    require(set(objects) == modules | {PROBE}, "Incomplete or extra compact object set")
    rows = {m: object_areas(text, m) for m, text in objects.items()}
    require(rows[PROBE] == dict.fromkeys(limits, 0) | {"xdata": probe_bytes},
            "Allocation-only layout probe changed")
    totals = {key: sum(rows[m][key] for m in modules) for key in limits}
    for key, limit in limits.items():
        require(totals[key] <= limit, f"Compact {key} regression ceiling exceeded")
    return totals | {"context_bytes": CONTEXT_BYTES,
                     "object_floor": totals["xdata"] + CONTEXT_BYTES}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--workspace", action="store_true",
                        help="Check the separate shared-UPPER profile, not the ordinary compact profile")
    args = parser.parse_args()
    result = report({p.stem: p.read_text() for p in args.output.glob("*.rel")}, workspace=args.workspace)
    label = "Shared UPPER LINK" if args.workspace else "Compact LINK"
    modules = WORKSPACE_MODULES if args.workspace else MODULES
    print(f"{label}: {len(modules)} production objects, {result['code']} CODE + "
          f"{result['const']} CONST, {result['xdata']} XDATA; "
          f"caller-inclusive floor {result['object_floor']}/7680. "
          "Object regression checks PASS; no linked placement/stack or MCU acceptance.")


if __name__ == "__main__":
    main()
