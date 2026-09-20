# SPDX-License-Identifier: BSD-3-Clause
"""Record a trusted local build's identities. Does not execute OpenOCD."""

import argparse
import json
from pathlib import Path
import re
import tarfile

if __package__:
    from . import offline
else:
    import offline


def record(workspace):
    binary = offline.validate_workspace(workspace)
    lock = json.loads((offline.HERE / "dependencies.json").read_text())
    sources = {}
    source = workspace / "openocd"
    for name in offline.command(["git", "-C", source, "ls-files"]).splitlines():
        path = source / name
        if path.is_file():
            sources[name] = offline.sha256(path)
    for archive_name, root, digest in (
        ("jimtcl.tar.gz", source / "jimtcl", lock["jimtcl"]["tar_sha256"]),
        ("downloads/libjaylink_0.3.1.orig.tar.xz", workspace / "libjaylink-0.3.1",
         lock["libjaylink"]["sha256"]),
    ):
        archive = workspace / archive_name
        if offline.sha256(archive) != digest:
            raise ValueError("source archive digest mismatch: " + archive_name)
        with tarfile.open(archive) as tar:
            for member in tar.getmembers():
                if not member.isfile() or not member.name.endswith((".c", ".h", ".in", ".ac", ".am")):
                    continue
                relative = Path(*Path(member.name).parts[1:])
                path = root / relative
                if path.read_bytes() != tar.extractfile(member).read():
                    raise ValueError("changed dependency source: " + str(path))
                sources[str(path.relative_to(workspace))] = offline.sha256(path)

    files = [
        binary, workspace / "library/lib/libjaylink.so.0",
        workspace / "deps/usr/lib/x86_64-linux-gnu/libusb-1.0.so.0",
        workspace / "build/config.h", workspace / "libjaylink-0.3.1/config.h",
        workspace / "configure-command.json", workspace / "environment.sh",
        workspace / "autom4te.cfg", workspace / "bin/aclocal",
        workspace / "openocd-strict-configure.log", workspace / "openocd-strict-build.log",
        workspace / "libjaylink-configure.log", workspace / "libjaylink-build.log",
        workspace / "no-device-checks.json", workspace / "offline-tests/evidence.json",
        offline.HERE / "preserve-reset.patch", offline.HERE / "dependencies.json",
        offline.HERE.parent / "test_nrf_openocd.py",
    ]
    for package in lock["packages"]:
        path = workspace / "downloads" / Path(package["path"]).name
        if offline.sha256(path) != package["sha256"]:
            raise ValueError("package digest mismatch: " + str(path))
        files.append(path)
    for name in ("gcc", "as", "ld", "make", "perl", "python3", "readelf", "ar", "git"):
        files.append(Path("/usr/bin") / name)
    for option in ("-print-prog-name=cc1", "-print-prog-name=collect2",
                   "-print-file-name=libgcc.a", "-print-file-name=libgcc_s.so.1",
                   "-print-file-name=crtbeginS.o", "-print-file-name=crtendS.o"):
        files.append(Path(offline.command(["/usr/bin/gcc", option]).strip()).resolve())
    files.extend((workspace / "deps/usr/bin").glob("*"))
    files.extend((workspace / "build").rglob("*.o"))
    files.extend((workspace / "build").rglob("*.a"))
    files.extend((workspace / "build").rglob("*.h"))
    files.extend((workspace / "library/include").rglob("*.h"))
    files.extend((workspace / "libjaylink-0.3.1/libjaylink/.libs").glob("*.o"))
    for pattern in ("*.py", "*.c", "*.sh"):
        files.extend(offline.HERE.glob(pattern))

    runtime = {}
    todo = [binary, workspace / "library/lib/libjaylink.so.0",
            workspace / "deps/usr/lib/x86_64-linux-gnu/libusb-1.0.so.0"]
    directories = [
        workspace / "library/lib", workspace / "deps/usr/lib/x86_64-linux-gnu",
        Path("/lib/x86_64-linux-gnu"), Path("/usr/lib/x86_64-linux-gnu"), Path("/lib64"),
    ]
    while todo:
        path = todo.pop().resolve()
        if str(path) in runtime:
            continue
        runtime[str(path)] = offline.sha256(path)
        dynamic = offline.command(["/usr/bin/readelf", "-d", path])
        for name in re.findall(r"\(NEEDED\).*?\[([^\]]+)\]", dynamic):
            matches = [directory / name for directory in directories if (directory / name).is_file()]
            if not matches:
                raise ValueError("unresolved runtime dependency: " + name)
            todo.append(matches[0])
    interpreter = Path("/lib64/ld-linux-x86-64.so.2").resolve()
    runtime[str(interpreter)] = offline.sha256(interpreter)
    runtime_environment = {
        "PATH": "/usr/bin:/bin", "LC_ALL": "C", "HOME": "/nonexistent",
        "LD_LIBRARY_PATH": str(workspace / "library/lib") + ":" +
                           str(workspace / "deps/usr/lib/x86_64-linux-gnu"),
    }
    loader_listing = offline.command([interpreter, "--list", binary], env=runtime_environment)
    actual_paths = {
        str(Path(name).resolve()) for name in re.findall(
            r"^\s*(?:\S+ => )?(/[^\s]+) \(", loader_listing, re.M)
    }
    if actual_paths != set(runtime) - {str(binary.resolve())}:
        raise ValueError("loader resolution differs from the recorded runtime closure")
    for path in files:
        if not path.is_file():
            raise ValueError("missing build identity input: " + str(path))
    data = {
        "schema": 1,
        "trust": "Trusted locally built workspace and host; recorded digests are identities, "
                 "not authentication of a replaced compiler, host loader, kernel or probe firmware.",
        "openocd_commit": lock["openocd"]["commit"],
        "binary": str(binary),
        "binary_sha256": offline.sha256(binary),
        "runtime_environment": runtime_environment,
        "loader_list": loader_listing,
        "runtime": runtime,
        "source_files": sources,
        "files": {str(path.resolve()): offline.sha256(path) for path in files},
        "compiler": offline.command(["/usr/bin/gcc", "--version"]).splitlines()[0],
    }
    (workspace / "build-evidence.json").write_text(json.dumps(data, indent=2, sort_keys=True) + "\n")
    return data


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workspace", type=Path, required=True)
    args = parser.parse_args()
    evidence = record(args.workspace)
    print(json.dumps({key: evidence[key] for key in
                      ("binary", "binary_sha256", "compiler", "runtime", "runtime_environment")},
                     indent=2))
