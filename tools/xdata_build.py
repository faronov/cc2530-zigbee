#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Opt-in, content-bound XDATA builds with atomic publication; never flashes."""
import argparse
from contextlib import contextmanager
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

from join_smoke_analysis import PHYSICAL_DATA
from join_smoke_image import CallerSchema, identities, structure
from verify_firmware import require
from xdata_multipool import XDATA_OVERLAY_FORMAT_VERSION, check_identities, prepare
from xdata_pool_allocator import ALLOCATOR_ALGORITHM_VERSION

ROOT = Path(__file__).resolve().parents[1]
PINS = ROOT / "tools/xdata_build_pins.json"
BUILD_RECEIPT_VERSION = 1


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="ascii")


def canonical(path):
    resolved = path.resolve()
    require(resolved.is_relative_to(ROOT / "build"), "Overlay output must be inside this checkout's build/")
    return resolved.relative_to(ROOT)


def policy(board, key_mode):
    catalog = json.loads(PINS.read_bytes())
    for key, expected in (("version", BUILD_RECEIPT_VERSION),
                          ("overlay_format", XDATA_OVERLAY_FORMAT_VERSION),
                          ("algorithm_version", ALLOCATOR_ALGORITHM_VERSION)):
        require(type(catalog[key]) is int and catalog[key] == expected,
                f"Unsupported XDATA catalog {key}")
    profile = board + "/" + key_mode
    require(profile in catalog["profiles"],
            f"Overlay profile {profile} is not admitted; no non-overlay fallback")
    entry = catalog["profiles"][profile]
    for kind in ("input", "image"):
        path = ROOT / entry[kind + "_catalog"]
        require(path.is_file(), f"Missing immutable {kind} catalog: {path}")
        require(sha(path) == entry[kind + "_catalog_sha256"],
                f"Immutable {kind} catalog changed: {entry[kind + '_catalog']}")
    contract = json.loads((ROOT / "tools/xdata_platform_contract.json").read_bytes())
    require(type(contract["version"]) is int and type(catalog["platform_version"]) is int and
            contract["version"] == catalog["platform_version"], "Unsupported platform contract version")
    return catalog, entry


def executable(name):
    found = shutil.which(name)
    require(found is not None, f"Missing overlay tool: {name}")
    return Path(found).resolve()


def toolchain(sdcc):
    for name in ("SDCC_HOME", "SDCC_INCLUDE", "SDCC_LIB", "SDCC_ASM", "CPATH",
                 "C_INCLUDE_PATH", "LIBRARY_PATH", "GCC_EXEC_PREFIX", "COMPILER_PATH"):
        require(not os.environ.get(name), f"Unset toolchain override {name} for an admitted overlay build")
    compiler = executable(sdcc)
    tools = {"compiler": compiler, "python": Path(sys.executable).resolve(),
             **{name: executable(name) for name in ("sdcc", "sdcpp", "sdas8051", "sdld", "sdar", "make")}}
    version = subprocess.check_output([str(compiler), "--version"], text=True)
    require("4.2.0 #13081" in version, f"Unsupported compiler version: {version.splitlines()[0]}")
    help_text = subprocess.check_output([str(compiler), "--help"], text=True)
    require("--xdata-ownership" in help_text,
            f"{compiler.name} lacks --xdata-ownership; use the documented patched compiler")
    support = {}
    for name in ("compiler", "sdcc"):
        search = subprocess.check_output(
            [str(tools[name]), "-mmcs51", "--model-large", "--print-search-dirs"], text=True)
        programs = re.search(r"(?ms)^programs:\n(.*?)^datadir:", search)
        require(programs is not None, f"{name}: cannot bind compiler subprocess lookup")
        for child in ("sdcpp", "sdas8051", "sdld"):
            candidates = [Path(directory) / child for directory in programs[1].splitlines() if directory]
            selected = next((p.resolve() for p in candidates if p.is_file() and os.access(p, os.X_OK)),
                            tools[child])
            support[name + "/" + child] = sha(selected)
        match = re.search(r"(?ms)^includedir:\n(.*?)^libdir:\n(.*?)^libpath:", search)
        require(match is not None, f"{name}: cannot bind SDCC include/runtime search paths")
        for label, lines, pattern in (("headers", match[1], "**/*.h"), ("libraries", match[2], "*.lib")):
            files = {}
            for line in lines.splitlines():
                directory = Path(line)
                if not line or not directory.is_dir():
                    continue
                for path in sorted(directory.glob(pattern)):
                    files.setdefault(str(path.relative_to(directory)), sha(path))
            require(files, f"{name}: missing {label}")
            support[name + "/" + label] = files
    return compiler, {
        "executables": {name: sha(path) for name, path in tools.items()},
        "compiler_version": version.strip(),
        "link_driver_version": subprocess.check_output([str(tools["sdcc"]), "--version"], text=True).strip(),
        "python_version": sys.version,
        "support": support,
    }


def source_inputs():
    paths = {ROOT / "Makefile"}
    for directory in ("src", "include", "boards", "examples", "tools", "tests"):
        root = ROOT / directory
        require(not root.is_symlink(), f"Overlay source directory is a symlink: {directory}")
        entries = list(root.rglob("*"))
        require(all(not p.is_symlink() for p in entries), f"Symlink in overlay source inputs: {directory}")
        paths.update(p for p in entries
                     if p.is_file() and p.suffix in (".c", ".h", ".py", ".json", ".asm", ".peep"))
    paths.update(ROOT / p for p in ("experiments/xdata/runtime-homes.json",
                                   "experiments/xdata/multipool/input-identities.json",
                                   "experiments/xdata/multipool/identities.json",
                                   "experiments/xdata/multipool/pools.json"))
    require(all(not p.is_symlink() for p in paths), "Overlay inputs must not be symlinks")
    return {str(p.relative_to(ROOT)): sha(p) for p in sorted(paths)}


def inputs(sdcc, board, key_mode):
    policy(board, key_mode)
    compiler, tools = toolchain(sdcc)
    return compiler, dict(version=BUILD_RECEIPT_VERSION, board=board, key_mode=key_mode,
                          sources=source_inputs(), toolchain=tools,
                          algorithm_version=ALLOCATOR_ALGORITHM_VERSION,
                          overlay_format=XDATA_OVERLAY_FORMAT_VERSION)


def output_hashes(generation):
    paths = []
    for directory in ("preliminary", "join-smoke-layout"):
        root = generation / directory
        require(root.is_dir() and not root.is_symlink(), f"Missing generation directory: {directory}")
        paths.extend(p for p in root.rglob("*") if p.is_file() or p.is_symlink())
    paths += [generation / "input-identities.json"]
    require(all(p.is_file() and not p.is_symlink() for p in paths), "Missing or symlinked overlay artifact")
    return {str(p.relative_to(generation)): sha(p) for p in sorted(paths)}


@contextmanager
def locked(output):
    output.mkdir(parents=True, exist_ok=True)
    require(not output.is_symlink(), "Overlay output directory is a symlink")
    lock = output / ".build.lock"
    require(not lock.is_symlink(), "Overlay build lock is a symlink")
    with lock.open("a") as stream:
        fcntl.flock(stream, fcntl.LOCK_EX)
        yield


def unpublish(output):
    public = output / "join-smoke-layout"
    if public.is_symlink():
        require(public.resolve().is_relative_to((output / ".generations").resolve()),
                "Refusing to replace a foreign overlay link")
        public.unlink()
    else:
        require(not public.exists(), "Overlay publication path is not an owned generation link")


def checked_receipt(root):
    root = canonical(root)
    generation = root.parent
    receipt = json.loads((generation / "receipt.json").read_bytes())
    require(type(receipt["version"]) is int and receipt["version"] == BUILD_RECEIPT_VERSION,
            "Unsupported build receipt")
    require(root.name == "join-smoke-layout", "Not an overlay final layout")
    check_identities(output_hashes(generation), receipt["outputs"], "Generated artifact")
    return root, receipt


def resource_check(artifacts, proof, expected):
    image, symbols, _, _, _ = artifacts
    roots = proof["static_stack"]["roots"].values()
    actual = dict(ordinary=symbols["l_XSEG"], saving=proof["xdata"]["saving"],
                  pools=len(proof["xdata"]["pools"]), populated_code=len(image),
                  common_code=sum(address < 0x8000 for address in image),
                  data_backing=sum(size for _, size in PHYSICAL_DATA.values()),
                  oseg=symbols["l_OSEG"], bseg_bits=symbols["l_BSEG"],
                  static_stack=max(root["bytes"] for root in roots),
                  bank_depth=max(root["bank_depth"] for root in roots))
    require(actual == expected, f"Overlay resources differ: expected {expected}, actual {actual}")
    return actual


def admit(root, board, key_mode, *, sdcc=None):
    root, receipt = checked_receipt(root)
    _, entry = policy(board, key_mode)
    require((receipt["inputs"]["board"], receipt["inputs"]["key_mode"]) == (board, key_mode),
            "Overlay receipt belongs to another profile")
    check_identities(source_inputs(), receipt["inputs"]["sources"], "Build input")
    if sdcc is not None:
        _, current = inputs(sdcc, board, key_mode)
        require(current == receipt["inputs"], "Compiler/toolchain or build inputs changed; rebuild overlay")
    image_catalog = json.loads((ROOT / entry["image_catalog"]).read_bytes())
    check_identities(identities(root), image_catalog["overlay"], "Admitted final image")
    artifacts, proof = structure(root)
    resource_check(artifacts, proof, entry["resources"])
    check_identities(source_inputs(), receipt["inputs"]["sources"], "Build input after verification")
    check_identities(output_hashes(root.parent), receipt["outputs"], "Artifact after verification")
    return artifacts, proof


def generate(generation, compiler, board, key_mode, entry):
    preliminary = generation / "preliminary"
    final = generation / "join-smoke-layout"
    flags = "-mmcs51 --model-large --std-c99 --debug --opt-code-size --Werror " \
            "-Iinclude -DCC2530_BOARD=1 --xdata-ownership"
    environment = {k: v for k, v in os.environ.items()
                   if k not in ("MAKEFLAGS", "MFLAGS", "MAKELEVEL", "MAKEOVERRIDES")}
    command = ["make", "--no-print-directory", "-s", "-j2", f"BOARD={board}",
               f"JOIN_SMOKE_KEY_MODE={key_mode}", f"BUILD={preliminary}", f"SDCC={compiler}",
               f"SDCC_FLAGS={flags}", "prepare-join-smoke-stack"]
    with (generation / "build.log").open("w") as log:
        subprocess.run(command, cwd=ROOT, env=environment, stdout=log, stderr=subprocess.STDOUT, check=True)
    binding = generation / "input-identities.json"
    shutil.copyfile(ROOT / entry["input_catalog"], binding)
    prepare(preliminary / "join-smoke-layout", preliminary, binding, final,
            format_version=XDATA_OVERLAY_FORMAT_VERSION)
    wanted = json.loads((ROOT / entry["image_catalog"]).read_bytes())["overlay"]
    check_identities(identities(final), wanted, "Final immutable image")
    artifacts, proof = structure(final)
    resource_check(artifacts, proof, entry["resources"])
    (final / "join_smoke_layout.h").write_text(
        CallerSchema(artifacts[2]).header(artifacts[1]), encoding="ascii")
    write_json(final / "admission.json",
               dict(proof=proof, identities=wanted, simulation_admitted=True,
                    simulated=False, hardware_observed=False))


def build(output, sdcc, board, key_mode, *, rebuild=False):
    output = canonical(output)
    with locked(output):
        public = output / "join-smoke-layout"
        try:
            compiler, before = inputs(sdcc, board, key_mode)
            _, entry = policy(board, key_mode)
            if public.is_symlink() and not rebuild:
                root, receipt = checked_receipt(public)
                if receipt["inputs"] == before:
                    wanted = json.loads((ROOT / entry["image_catalog"]).read_bytes())["overlay"]
                    check_identities(identities(root), wanted, "Cached immutable image")
                    print(f"Verified unchanged overlay inputs/artifacts: {public}")
                    return root
            unpublish(output)
            generations = output / ".generations"
            generations.mkdir(exist_ok=True)
            require(not generations.is_symlink(), "Overlay generations directory is a symlink")
            generation = canonical(Path(tempfile.mkdtemp(prefix="image-", dir=generations)))
            generate(generation, compiler, board, key_mode, entry)
            _, after = inputs(sdcc, board, key_mode)
            require(before == after, "Source/compiler/toolchain changed during overlay build; not published")
            write_json(generation / "receipt.json",
                       dict(version=BUILD_RECEIPT_VERSION, inputs=before, outputs=output_hashes(generation)))
            pending = output / (generation.name + ".link")
            pending.symlink_to(Path(".generations") / generation.name / "join-smoke-layout")
            os.replace(pending, public)
            print(f"Published independently verified overlay: {public}")
            return generation / "join-smoke-layout"
        except (OSError, ValueError, KeyError, subprocess.SubprocessError):
            unpublish(output)
            raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--sdcc", default="sdcc")
    parser.add_argument("--board", required=True)
    parser.add_argument("--key-mode", required=True)
    parser.add_argument("--verify", action="store_true")
    parser.add_argument("--rebuild", action="store_true")
    args = parser.parse_args()
    os.chdir(ROOT)
    try:
        if args.verify:
            with locked(canonical(args.output)):
                admit(args.output / "join-smoke-layout", args.board, args.key_mode, sdcc=args.sdcc)
            print("Independent overlay/source/toolchain/resource verification PASS")
        else:
            build(args.output, args.sdcc, args.board, args.key_mode, rebuild=args.rebuild)
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print(f"XDATA overlay rejected: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
