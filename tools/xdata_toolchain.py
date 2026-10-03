#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Prepare only the pinned public mcs51 package; never install system tools."""
import argparse
from contextlib import contextmanager
import fcntl
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import platform
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.error
import urllib.request
import uuid

from verify_firmware import require

ROOT = Path(__file__).resolve().parents[1]
PINS = ROOT / "tools/xdata_toolchain_pins.json"
CACHE = ROOT / "build/toolchains/sdcc-xdata"
BINS = ("sdcc", "sdcpp", "sdas8051", "sdld", "sdar")
LIBS = ("mcs51", "libsdcc", "libint", "liblong", "liblonglong", "libfloat")
PROBE = """#include <stdint.h>
__xdata volatile uint8_t observed;
uint8_t copy_byte(uint8_t value) {
    volatile uint8_t copy = value;
    return copy;
}
void main(void) { observed = copy_byte(observed); }
"""


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_pins(path=PINS):
    pins = json.loads(path.read_bytes())
    require(set(pins) == {"version", "repository", "tag", "asset", "archive_sha256",
                          "source_commit", "executable_sha256", "manifest_sha256",
                          "ownership_schema", "runtime_archives"}, "Incomplete or unknown toolchain pin fields")
    for key in ("version", "ownership_schema"):
        require(type(pins[key]) is int and pins[key] == 1, "Unsupported toolchain " + key)
    require(pins["repository"] == "faronov/sdcc" and
            re.fullmatch(r"v4\.2\.0-xdata-ownership\.[1-9][0-9]*", pins["tag"]),
            "Toolchain requires an explicit reviewed release, never latest or a branch")
    require(pins["asset"] == "sdcc-" + pins["tag"][1:] + "-linux-x86_64.tar.xz",
            "Release asset does not match the pinned tag/platform")
    for key in ("archive_sha256", "executable_sha256", "manifest_sha256", "source_commit"):
        require(isinstance(pins[key], str) and
                re.fullmatch(r"[0-9a-f]{" + ("40" if key == "source_commit" else "64") + "}", pins[key]),
                "Invalid pinned identity: " + key)
    require(set(pins["runtime_archives"]) == {"mcs51.lib", "libsdcc.lib"} and
            all(re.fullmatch(r"[0-9a-f]{64}", value) for value in pins["runtime_archives"].values()),
            "Invalid implicit-runtime archive pins")
    return pins


@contextmanager
def compiler_path(compiler):
    if not all((compiler.parent / name).is_file() for name in BINS):
        yield
        return
    old = os.environ.get("PATH")
    os.environ["PATH"] = str(compiler.parent) + os.pathsep + (old or os.defpath)
    try:
        yield
    finally:
        if old is None:
            os.environ.pop("PATH", None)
        else:
            os.environ["PATH"] = old


def capability(compiler):
    for key in ("SDCC_HOME", "SDCC_INCLUDE", "SDCC_LIB", "SDCC_ASM", "CPATH",
                "C_INCLUDE_PATH", "LIBRARY_PATH", "GCC_EXEC_PREFIX", "COMPILER_PATH"):
        require(not os.environ.get(key), "Unset toolchain override " + key)
    with tempfile.TemporaryDirectory(prefix="xdata-capability-") as temporary, compiler_path(compiler):
        root = Path(temporary)
        version = subprocess.check_output([str(compiler), "--version"], text=True)
        require("4.2.0 #13081" in version and "mcs51" in version, "Unsupported XDATA compiler version")
        outputs = []
        sidecar = None
        for enabled in (False, True):
            work = root / ("on" if enabled else "off")
            work.mkdir()
            (work / "capability.c").write_text(PROBE, encoding="ascii")
            command = [str(compiler), "-mmcs51", "--model-large", "--std-c99", "--debug",
                       *(["--xdata-ownership"] if enabled else []), "capability.c"]
            result = subprocess.run(command, cwd=work, text=True, capture_output=True, timeout=60)
            require(result.returncode == 0, "XDATA capability compile/link failed: " + result.stderr)
            path = work / "capability.xdata.json"
            require(path.is_file() == enabled, "Compiler lacks correct --xdata-ownership behavior")
            if enabled:
                data = json.loads(path.read_bytes())
                require(type(data["version"]) is int and data["version"] == 1 and
                        data["module"] == "capability", "Unsupported ownership probe schema/module")
                classes = {row["class"] for row in data["objects"]}
                require({"GLOBAL", "LOCAL"} <= classes, "Incomplete compiler ownership probe")
                sidecar = sha(path)
            outputs.append({suffix: sha(work / ("capability." + suffix))
                            for suffix in ("asm", "rel", "adb", "lst", "sym", "ihx", "cdb", "mem")})
        require(outputs[0] == outputs[1], "Ownership option changed ordinary probe outputs")
    return dict(schema=1, sidecar_sha256=sidecar, outputs=outputs[0],
                source_sha256=hashlib.sha256(PROBE.encode("ascii")).hexdigest())


def check_package(root, pins):
    require(root.is_dir() and not root.is_symlink(), "Missing or symlinked extracted toolchain")
    manifest_path = root / "MANIFEST.json"
    require(manifest_path.is_file() and not manifest_path.is_symlink() and
            sha(manifest_path) == pins["manifest_sha256"], "Pinned package manifest mismatch")
    manifest = json.loads(manifest_path.read_bytes())
    require(type(manifest["version"]) is int and manifest["version"] == 1,
            "Unsupported package manifest schema")
    actual = {}
    for path in sorted(root.rglob("*")):
        require(not path.is_symlink(), "Symlink in extracted toolchain")
        require(path.is_file() or path.is_dir(), "Non-regular extracted toolchain entry")
        if path.is_file() and path != manifest_path:
            actual[str(path.relative_to(root))] = dict(sha256=sha(path), mode=path.stat().st_mode & 0o777)
    require(actual == manifest["files"], "Extracted toolchain content/mode mismatch; use --repair")
    for name in BINS:
        path = root / "bin" / name
        require(path.is_file() and os.access(path, os.X_OK), "Missing executable: " + name)
    for name in LIBS:
        require((root / "share/sdcc/lib/large" / (name + ".lib")).is_file(), "Missing runtime: " + name)
    compiler = root / "bin/sdcc"
    require(sha(compiler) == pins["executable_sha256"], "Pinned compiler executable mismatch")
    info = json.loads((root / "BUILDINFO.txt").read_bytes())
    require(info["release"] == pins["tag"] and info["source_commit"] == pins["source_commit"] and
            type(info["ownership_schema"]) is int and info["ownership_schema"] == pins["ownership_schema"]
            and info["platform"] == "linux-x86_64", "Foreign package BUILDINFO")
    return compiler


def download(pins, destination):
    url = f"https://github.com/{pins['repository']}/releases/download/{pins['tag']}/{pins['asset']}"
    try:
        with urllib.request.urlopen(url, timeout=60) as response, destination.open("wb") as output:
            total = 0
            while chunk := response.read(1024 * 1024):
                total += len(chunk)
                require(total <= 128 * 1024 * 1024, "Toolchain download exceeds bounded archive size")
                output.write(chunk)
    except urllib.error.URLError as error:
        raise ValueError("Pinned toolchain download unavailable; prepare online once, then use --offline: "
                         + str(error)) from error
    require(sha(destination) == pins["archive_sha256"], "Downloaded release archive SHA256 mismatch")


def extract(archive, destination, pins):
    require(sha(archive) == pins["archive_sha256"], "Release archive SHA256 mismatch")
    name = pins["asset"].removesuffix(".tar.xz")
    with tarfile.open(archive, "r:xz") as stream:
        members = stream.getmembers()
        require(0 < len(members) <= 10000 and
                sum(m.size for m in members) <= 256 * 1024 * 1024, "Unbounded package archive")
        seen = set()
        for member in members:
            path = PurePosixPath(member.name)
            require(not path.is_absolute() and path.parts and path.parts[0] == name and
                    ".." not in path.parts and str(path) == member.name and member.name not in seen
                    and (member.isfile() or member.isdir()) and not member.issparse()
                    and member.mode in (0o644, 0o755), "Unsafe or duplicate package archive entry")
            seen.add(member.name)
        stream.extractall(destination, filter="data")
    root = destination / name
    check_package(root, pins)
    return root


def prepare(*, cache=CACHE, offline=False, repair=False, pins=None):
    pins = load_pins() if pins is None else pins
    setting = os.environ.get("XDATA_TOOLCHAIN_OFFLINE", "0")
    require(setting in ("0", "1"), "XDATA_TOOLCHAIN_OFFLINE must be 0 or 1")
    offline = offline or setting == "1"
    require(platform.system() == "Linux" and platform.machine() == "x86_64",
            "Pinned XDATA toolchain supports Linux x86_64 only")
    base = cache.absolute() / pins["tag"]
    require(all(not p.is_symlink() for p in (base, *base.parents)), "Symlink in toolchain cache path")
    base.mkdir(parents=True, exist_ok=True)
    archive, root, lock = base / pins["asset"], base / "toolchain", base / ".lock"
    require(not archive.is_symlink() and not lock.is_symlink() and not root.is_symlink(),
            "Symlinked toolchain archive/lock/directory")
    with lock.open("a") as handle:
        fcntl.flock(handle, fcntl.LOCK_EX)
        archive_ok = archive.is_file() and sha(archive) == pins["archive_sha256"]
        require(not archive.exists() or archive_ok or repair,
                "Cached archive corrupt; use --repair to deliberately redownload")
        if root.exists() and not repair:
            compiler = check_package(root, pins)
        else:
            if not archive_ok:
                require(not offline, "Pinned toolchain unavailable offline; run make prepare-xdata-toolchain online")
                with tempfile.TemporaryDirectory(prefix=".download-", dir=base) as work:
                    pending = Path(work) / pins["asset"]
                    download(pins, pending)
                    os.replace(pending, archive)
            with tempfile.TemporaryDirectory(prefix=".extract-", dir=base) as work:
                pending = extract(archive, Path(work), pins)
                capability(pending / "bin/sdcc")
                require(not root.is_symlink(), "Refusing to replace a symlinked toolchain")
                if root.exists():
                    require(root.is_dir(), "Refusing to replace a non-directory toolchain")
                    os.replace(root, base / ("rejected-" + uuid.uuid4().hex))
                os.replace(pending, root)
            compiler = check_package(root, pins)
        probe = capability(compiler)
    return compiler, dict(mode="release", **pins, capability=probe)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache", type=Path, default=CACHE)
    parser.add_argument("--offline", action="store_true")
    parser.add_argument("--repair", action="store_true")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    try:
        compiler, identity = prepare(cache=args.cache, offline=args.offline, repair=args.repair)
        print(json.dumps(dict(compiler=str(compiler), identity=identity), indent=2) if args.json else compiler)
    except (OSError, ValueError, KeyError, TypeError, tarfile.TarError, subprocess.SubprocessError) as error:
        print(f"XDATA toolchain rejected: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
