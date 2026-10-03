#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Record actual artifact equivalence, compiler ownership and bounded timing."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import statistics
import subprocess
import sys
import tempfile
import time

sys.path[:0] = [str(Path(__file__).resolve().parents[2] / p) for p in ("tools", "tests")]
from join_smoke_analysis import load, PHYSICAL_DATA
from join_smoke_image import identities, structure
from verify_firmware import require
from xdata_lifetime import inventory
from xdata_relocations import audit, metadata


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def measure(stock, patched, fixture):
    samples = {name: {"compiler": [], "linker": []} for name in ("stock", "off", "on")}
    with tempfile.TemporaryDirectory(prefix="xdata-benchmark-") as tmp:
        root = Path(tmp)
        (root / "input.c").write_bytes(fixture.read_bytes())
        for repeat in range(7):
            for name, compiler, extra in (("stock", stock, []), ("off", patched, []),
                                          ("on", patched, ["--xdata-ownership"])):
                commands = (
                    ("compiler", [compiler, "-mmcs51", "--model-large", "--debug", *extra,
                                  "-S", "input.c"]),
                    ("linker", [compiler, "-mmcs51", "--model-large", "--debug", "-Wl-r",
                                "-o", "image.ihx", "input.rel"]))
                for phase, command in commands:
                    start = time.perf_counter()
                    subprocess.run(command, cwd=root, check=True, capture_output=True)
                    elapsed = time.perf_counter() - start
                    if repeat:
                        samples[name][phase].append(elapsed)
                    if phase == "compiler":
                        subprocess.run(["sdas8051", "-plosgff", "input.asm"],
                                       cwd=root, check=True, capture_output=True)
    return {name: {phase: {"samples_seconds": values, "median_seconds": statistics.median(values)}
                   for phase, values in phases.items()} for name, phases in samples.items()}


def image_metrics(root):
    artifacts, proof = structure(root)
    image, symbols, _, _, objects = artifacts
    _, relocation = audit(root, artifacts, json.loads((root / "layout.json").read_bytes()))
    areas = {}
    for raw in objects.values():
        for area in metadata(raw)[0]:
            if area["flags"] & 0x40:
                areas[area["name"]] = areas.get(area["name"], 0) + area["size"]
    return dict(code=len(image), common_code=sum(a < 0x8000 for a in image),
                xdata=symbols["l_XSEG"], free_xdata=7680-symbols["l_XSEG"],
                physical_data=sum(n for _, n in PHYSICAL_DATA.values()),
                oseg=symbols["l_OSEG"], bseg_bits=symbols["l_BSEG"],
                static_stack=proof["static_stack"]["roots"]["_main"]["bytes"],
                bank_depth=proof["static_stack"]["roots"]["_main"]["bank_depth"],
                application_object_xdata_areas=areas,
                linked_xdata_areas={name: symbols["l_"+name] for name in areas},
                function_owned_areas=0,
                artifact_bytes={ext: sum(p.stat().st_size for p in root.glob("*."+ext))
                                for ext in ("asm", "rel", "cdb")},
                area_count=len([s for s in symbols if s.startswith("l_")]),
                identities=identities(root), proof=proof, relocation_audit=relocation)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stock", type=Path, required=True)
    parser.add_argument("--patched", type=Path, required=True)
    parser.add_argument("--stock-image", type=Path, required=True)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--production", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    a, b = (p / "join-smoke-layout" for p in (args.stock_image, args.image))
    compared = {}
    for ext in ("asm", "rel", "adb", "lst", "rst", "sym", "ihx", "cdb", "mem"):
        files = list(a.glob("*."+ext))
        require(files and {p.name for p in files} == {p.name for p in b.glob("*."+ext)},
                "Artifact file set changed")
        require(all(p.read_bytes() == (b / p.name).read_bytes() for p in files),
                "Representation changed existing artifact: " + ext)
        compared[ext] = len(files)
    for ext in ("ihx", "cdb", "mem"):
        name = "join_smoke_unverified."+ext
        require((args.production / name).read_bytes() == (b / name).read_bytes(),
                "STOP: production firmware artifact changed: " + ext)
    rows = inventory(b, load(b), args.image)
    facts = {r["cdb_key"]: r for p in args.image.rglob("*.xdata.json")
             for r in json.loads(p.read_bytes())["objects"]}
    header = "external_object\tcompiler_owner\tcompiler_class\tarea\tbytes\taddress\tmatch\n"
    lines, classes, counts = [], Counter(), Counter()
    for row in rows:
        if row["key"] is None:
            continue
        fact = facts[row["key"]]
        classes[fact["class"]] += row["size"]
        counts[fact["class"]] += 1
        lines.append("\t".join(map(str, (row["key"], fact["owner"] or "-", fact["class"],
                                        fact["area"], row["size"], hex(row["address"]), "yes"))))
    (args.output / "reconciliation.tsv").write_text(header + "\n".join(lines) + "\n")
    fixture = args.patched.parent.parent / "support/regression/xdata-ownership.c"
    report = dict(stock=image_metrics(a), patched=image_metrics(b),
                  artifacts_identical=compared, production_ihx_cdb_mem_identical=True,
                  compiler_sha256={"stock": sha(args.stock), "patched": sha(args.patched)},
                  ownership_bytes=dict(classes), ownership_objects=dict(counts),
                  sidecar_files=len(list(args.image.rglob("*.xdata.json"))),
                  sidecar_bytes=sum(p.stat().st_size for p in args.image.rglob("*.xdata.json")),
                  timing=measure(str(args.stock.resolve()), str(args.patched.resolve()), fixture))
    (args.output / "measurements.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({k: v for k, v in report.items() if k not in ("stock", "patched", "timing")}))


if __name__ == "__main__":
    main()
