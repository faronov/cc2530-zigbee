#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""SDCC feature-OFF identity proof and CC2530_RX_DIAG fit check (never flashes).

  --variant production  the production XDATA-overlay flow (tools/xdata_build.py)
                        through this driver; must reproduce the admitted final
                        image and the physical HEX exactly (feature-OFF proof).
  --variant rx-diag     compiles with -DCC2530_RX_DIAG and runs the real banked
                        CODE packer (tools/join_smoke_layout.py place()). The
                        SDCC diagnostic image does NOT fit the join-smoke bank
                        layout (docs/H2_RX_INVESTIGATION.md section 10): the
                        receipt records the per-group growth, the exhaustive
                        packing verdict and the measured JS_RADIO headroom.
                        An SDCC diagnostic image is never produced.
"""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from banked_image import ihex, pack  # noqa: E402
from join_smoke_image import identities, structure  # noqa: E402
from join_smoke_layout import place  # noqa: E402
from verify_firmware import parse_ihex, require  # noqa: E402
import xdata_build as xb  # noqa: E402
from xdata_lifetime import compiler_metadata_identities  # noqa: E402
from xdata_multipool import XDATA_OVERLAY_FORMAT_VERSION, prepare  # noqa: E402
from xdata_toolchain import compiler_path  # noqa: E402

ROOT = xb.ROOT
BOARD, KEY_MODE = "lg_esl29_rev03", "default-tc"
FLAGS = ("-mmcs51 --model-large --std-c99 --debug --opt-code-size --Werror "
         "-Iinclude -DCC2530_BOARD=1 --xdata-ownership")
VARIANTS = {"production": "", "rx-diag": "-DCC2530_RX_DIAG"}
# Admitted SDCC physical image of this source (docs/LLVM_DEVELOPMENT.md, legacy identity).
PRODUCTION_PHYSICAL = "518c8aa4ef6ab47205f2556135c947aedae109251211e306791dae608e6c3f21"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def resources(artifacts, proof):
    image, symbols = artifacts[0], artifacts[1]
    roots = proof["static_stack"]["roots"].values()
    return dict(ordinary=symbols["l_XSEG"], saving=proof["xdata"]["saving"],
                pools=len(proof["xdata"]["pools"]), populated_code=len(image),
                common_code=sum(address < 0x8000 for address in image),
                oseg=symbols["l_OSEG"], bseg_bits=symbols["l_BSEG"],
                static_stack=max(root["bytes"] for root in roots),
                bank_depth=max(root["bank_depth"] for root in roots))


def source_identity():
    head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    diff = subprocess.check_output(["git", "diff", "HEAD", "--binary"], cwd=ROOT)
    return {"commit": head, "uncommitted_diff_sha256": sha(diff) if diff else None,
            "inputs_sha256": sha(json.dumps(xb.source_inputs(), sort_keys=True).encode())}


def link_objects(directory):
    objects = {p.stem: p.read_text(encoding="ascii") for p in sorted(directory.glob("*.rel"))}
    require(objects, f"No objects in {directory}")
    return objects


def code_groups(objects):
    groups = {}
    for text in objects.values():
        for name, size in re.findall(r"^A (JS_\w+) size ([0-9A-F]+) flags", text, re.M):
            groups[name] = groups.get(name, 0) + int(size, 16)
    return {name: size for name, size in sorted(groups.items()) if size}


def fits(objects, extra=None):
    probe = dict(objects)
    for group, size in (extra or {}).items():
        probe[f"_probe_{group}"] = f"A {group} size {size:X} flags 0 addr 0\n"
    try:
        place(probe)
    except ValueError as error:
        require(str(error) == "Actual CODE groups cannot fit below the NV partition", str(error))
        return False
    return True


def headroom(objects, group):
    low, high = 0, 0x8000
    require(fits(objects), "Baseline objects do not pack")
    while low < high:
        middle = (low + high + 1) // 2
        low, high = (middle, high) if fits(objects, {group: middle}) else (low, middle - 1)
    return low


def fit_check(output, sdcc, baseline):
    require(not output.exists(), f"refusing to reuse {output}")
    base = baseline.resolve() / "preliminary" / "join-smoke-layout"
    base_receipt = json.loads((baseline / "receipt.json").read_bytes())
    require(base_receipt["variant"] == "production" and
            base_receipt["physical_hex_sha256"] == PRODUCTION_PHYSICAL,
            "--baseline must be a passing production run of this driver")
    compiler, toolchain = xb.toolchain(sdcc)
    before = source_identity()
    require(before == base_receipt["source"], "--baseline was built from different sources")
    output.mkdir(parents=True)
    preliminary = output / "preliminary"
    flags = (FLAGS + " " + VARIANTS["rx-diag"]).strip()
    environment = {k: v for k, v in os.environ.items()
                   if k not in ("MAKEFLAGS", "MFLAGS", "MAKELEVEL", "MAKEOVERRIDES")}
    command = ["make", "--no-print-directory", "-s", "-j2", f"BOARD={BOARD}",
               f"JOIN_SMOKE_KEY_MODE={KEY_MODE}", f"BUILD={preliminary}", f"SDCC={compiler}",
               f"SDCC_FLAGS={flags}", "prepare-join-smoke-layout"]
    with compiler_path(compiler):
        with (output / "build.log").open("w") as log:
            made = subprocess.run(command, cwd=ROOT, env=environment, stdout=log,
                                  stderr=subprocess.STDOUT).returncode
    log = (output / "build.log").read_text(errors="replace")
    diag_objects = link_objects(preliminary / "join-smoke-layout")
    base_objects = link_objects(base)
    require(diag_objects.keys() == base_objects.keys(), "Module sets differ")
    require(any("_cc2530_rx_diag" in text for text in diag_objects.values()) and
            not any("_cc2530_rx_diag" in text for text in base_objects.values()),
            "Diagnostic object set does not differ from production by the RX_DIAG block")
    diag_fits = fits(diag_objects)
    require(diag_fits == (made == 0), "Packer verdict disagrees with the make result")
    require(diag_fits or "Actual CODE groups cannot fit below the NV partition" in log,
            "Build failed before/outside the CODE packer")
    require(source_identity() == before, "Sources changed during the build")
    base_groups, diag_groups = code_groups(base_objects), code_groups(diag_objects)
    receipt = {
        "kind": "CC2530_RX_DIAG SDCC fit check",
        "label": "DIAGNOSTIC - no SDCC diagnostic image is produced",
        "verdict": "FITS" if diag_fits else "INFEASIBLE",
        "board": BOARD, "key_mode": KEY_MODE, "sdcc_flags": flags, "source": before,
        "toolchain_executables": toolchain["executables"],
        "compiler_version": toolchain["compiler_version"],
        "baseline_physical_hex_sha256": base_receipt["physical_hex_sha256"],
        "bank_capacity_bytes": 6 * 0x8000 + 0x6800,
        "production_groups": base_groups, "rx_diag_groups": diag_groups,
        "group_growth": {k: diag_groups[k] - base_groups.get(k, 0)
                         for k in diag_groups if diag_groups[k] != base_groups.get(k, 0)},
        "production_free_bytes": 6 * 0x8000 + 0x6800 - sum(base_groups.values()),
        "rx_diag_free_bytes": 6 * 0x8000 + 0x6800 - sum(diag_groups.values()),
        "production_JS_RADIO_headroom": headroom(base_objects, "JS_RADIO"),
        "simulated": False, "hardware_observed": False,
    }
    (output / "receipt.json").write_text(json.dumps(receipt, indent=1, sort_keys=True) + "\n")
    return receipt


def build(output, sdcc):
    require(not output.exists(), f"refusing to reuse {output}")
    _, entry = xb.policy(BOARD, KEY_MODE)
    compiler, toolchain = xb.toolchain(sdcc)
    before = source_identity()
    output.mkdir(parents=True)
    preliminary, final = output / "preliminary", output / "join-smoke-layout"
    flags = FLAGS
    environment = {k: v for k, v in os.environ.items()
                   if k not in ("MAKEFLAGS", "MFLAGS", "MAKELEVEL", "MAKEOVERRIDES")}
    command = ["make", "--no-print-directory", "-s", "-j2", f"BOARD={BOARD}",
               f"JOIN_SMOKE_KEY_MODE={KEY_MODE}", f"BUILD={preliminary}", f"SDCC={compiler}",
               f"SDCC_FLAGS={flags}", "prepare-join-smoke-stack"]
    with compiler_path(compiler):
        with (output / "build.log").open("w") as log:
            subprocess.run(command, cwd=ROOT, env=environment, stdout=log,
                           stderr=subprocess.STDOUT, check=True)
        binding = json.loads((ROOT / entry["input_catalog"]).read_bytes())
        image_now = identities(preliminary / "join-smoke-layout")
        metadata_now = compiler_metadata_identities(preliminary)
        repin = {"image": sorted(k for k in image_now.keys() | binding["baseline"].keys()
                                 if image_now.get(k) != binding["baseline"].get(k)),
                 "metadata": sorted(k for k in metadata_now.keys() | binding["metadata"].keys()
                                    if metadata_now.get(k) != binding["metadata"].get(k))}
        binding["baseline"], binding["metadata"] = image_now, metadata_now
        (output / "repinned-input-identities.json").write_text(json.dumps(binding))
        prepare(preliminary / "join-smoke-layout", preliminary,
                output / "repinned-input-identities.json", final,
                format_version=XDATA_OVERLAY_FORMAT_VERSION)
    final_ids = identities(final)
    admitted = json.loads((ROOT / entry["image_catalog"]).read_bytes())["overlay"]
    artifacts, proof = structure(final)
    symbols = artifacts[1]
    actual = resources(artifacts, proof)
    ihx = (final / "join_smoke_unverified.ihx").read_bytes()
    physical = ihex(pack(parse_ihex(ihx.decode("ascii")))).encode("ascii")
    (output / "physical.hex").write_bytes(physical)
    require(source_identity() == before, "Sources changed during the build")

    require("_cc2530_rx_diag" not in symbols, "Production image contains cc2530_rx_diag")
    require(final_ids["ihx"] == admitted["ihx"], "Feature-OFF final IHX differs from the admitted image")
    require(sha(physical) == PRODUCTION_PHYSICAL, "Feature-OFF physical HEX differs from 518c8aa4")
    require(repin["image"] == [], "Feature-OFF preliminary image differs from the input catalog")

    receipt = {
        "kind": "feature-OFF identity check", "label": "production identity reproduction",
        "variant": "production", "board": BOARD, "key_mode": KEY_MODE, "sdcc_flags": flags,
        "source": before, "toolchain_executables": toolchain["executables"],
        "compiler_version": toolchain["compiler_version"],
        "repinned_vs_input_catalog": repin,
        "final_vs_admitted": sorted(k for k in final_ids if final_ids[k] != admitted.get(k)),
        "final_identities": final_ids,
        "ihx_sha256": sha(ihx), "physical_hex_sha256": sha(physical),
        "physical_populated": len(pack(parse_ihex(ihx.decode("ascii")))),
        "resources": actual, "admitted_resources": entry["resources"],
        "resource_delta": {k: actual[k] - entry["resources"][k] for k in actual},
        "proofs": {"structure": "PASS", "static_stack_bytes": actual["static_stack"],
                   "bank_depth": actual["bank_depth"]},
        "simulated": False, "hardware_observed": False,
    }
    (output / "receipt.json").write_text(json.dumps(receipt, indent=1, sort_keys=True) + "\n")
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--variant", choices=VARIANTS, required=True)
    parser.add_argument("--baseline", type=Path, help="rx-diag: output of a passing production run")
    parser.add_argument("--sdcc", help="explicit SDCC 4.2.0 #13081 --xdata-ownership; default: pinned")
    args = parser.parse_args()
    if args.variant == "production":
        receipt = build(args.output.resolve(), args.sdcc)
        keys = ("variant", "physical_hex_sha256", "resource_delta", "repinned_vs_input_catalog")
    else:
        require(args.baseline is not None, "--variant rx-diag requires --baseline")
        receipt = fit_check(args.output.resolve(), args.sdcc, args.baseline)
        keys = ("verdict", "group_growth", "production_free_bytes", "rx_diag_free_bytes",
                "production_JS_RADIO_headroom")
    print(json.dumps({k: receipt[k] for k in keys}, indent=1))


if __name__ == "__main__":
    main()
