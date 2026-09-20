# SPDX-License-Identifier: BSD-3-Clause
"""Explicit, Linux-only execution proof in a trusted local pinned build.

Not a build sandbox or a certifier for arbitrary downloaded executables.
Never runs an adapter backend: only the synthetic libjaylink is loaded.
"""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tarfile
import time


HERE = Path(__file__).resolve().parent
SERIAL = "123456789"  # Synthetic; never a hardware selection.
PREFIX = "noinit; gdb_port disabled; tcl_port disabled; telnet_port disabled; "
CONFIG = (
    "adapter driver jlink; adapter serial " + SERIAL +
    "; transport select swd; adapter speed 1000; reset_config none; "
)
RESET_APIS = (
    "jaylink_set_reset", "jaylink_clear_reset",
    "jaylink_jtag_set_trst", "jaylink_jtag_clear_trst",
)
STRICT = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-Wstrict-prototypes"]
EXPECTED_CASES = 58


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def command(args, cwd=None, env=None, timeout=120):
    if env is None:
        env = {"PATH": "/usr/bin:/bin", "LC_ALL": "C", "HOME": "/nonexistent"}
    try:
        return subprocess.run(
            [str(arg) for arg in args], cwd=cwd, env=env, timeout=timeout,
            check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
        ).stdout
    except subprocess.CalledProcessError as error:
        raise RuntimeError(error.stdout) from error


def needed(path):
    text = command(["/usr/bin/readelf", "-d", path])
    return set(re.findall(r"\(NEEDED\).*?\[([^\]]+)\]", text))


def archive_sources(archive, root, digest, change):
    """Only one exact, hash-bound source change is allowed; every other file matches."""
    if sha256(archive) != digest:
        raise ValueError("source archive digest mismatch")
    found_change = False
    sources = {}
    with tarfile.open(archive) as tar:
        for member in tar.getmembers():
            if not member.isfile():
                continue
            relative = Path(*Path(member.name).parts[1:])
            if not relative.parts or relative.is_absolute() or ".." in relative.parts:
                raise ValueError("invalid source archive path")
            original = tar.extractfile(member).read()
            path = root / relative
            actual = path.read_bytes()
            if relative.as_posix() == change["path"]:
                found_change = True
                if hashlib.sha256(original).hexdigest() != change["before"]:
                    raise ValueError("patch preimage mismatch")
                if hashlib.sha256(actual).hexdigest() != change["after"]:
                    raise ValueError("patch postimage mismatch")
            elif actual != original:
                raise ValueError("unreviewed dependency change: " + str(relative))
            sources[str(path)] = hashlib.sha256(actual).hexdigest()
    if not found_change:
        raise ValueError("patched source missing from archive")
    return sources


def validate_library_sources(workspace):
    lock = json.loads((HERE / "dependencies.json").read_text())["libjaylink"]
    change = lock["patch"]
    if change["file"] != "libjaylink-usb-1025.patch" or change["path"] != "libjaylink/discovery_usb.c":
        raise ValueError("unexpected libjaylink patch scope")
    if sha256(HERE / change["file"]) != change["sha256"]:
        raise ValueError("libjaylink patch digest mismatch")
    sources = archive_sources(
        workspace / "downloads/libjaylink_0.3.1.orig.tar.xz",
        workspace / "libjaylink-0.3.1", lock["sha256"], change,
    )
    # Validate the stored diff as well as its complete archived pre/postimages.
    command(["git", "apply", "--reverse", "--check", HERE / change["file"]],
            cwd=workspace / "libjaylink-0.3.1")
    return {str(Path(path).relative_to(workspace)): digest for path, digest in sources.items()}


def validate_workspace(workspace):
    workspace = Path(workspace)
    if not workspace.is_absolute() or workspace != workspace.resolve():
        raise ValueError("workspace must be an absolute, canonical trusted local path")
    validate_library_sources(workspace)
    lock = json.loads((HERE / "dependencies.json").read_text())
    source = workspace / "openocd"
    actual = command(["git", "-C", source, "rev-parse", "HEAD"]).strip()
    if actual != lock["openocd"]["commit"]:
        raise ValueError("wrong OpenOCD revision")
    diff = command(["git", "-C", source, "diff", "--ignore-submodules=all", "--"])
    patch = (HERE / "preserve-reset.patch").read_text()
    if diff != patch[patch.index("diff --git "):]:
        raise ValueError("workspace differs from exactly the reviewed patch")
    binary = workspace / "build/src/openocd"
    if needed(binary) != {"libjaylink.so.0", "libc.so.6"}:
        raise ValueError("unexpected executable dependency; synthetic replacement is not sufficient")
    configured = (workspace / "build/config.h").read_text()
    if re.findall(r"^#define (BUILD_\w+) 1$", configured, re.M) != ["BUILD_JLINK"]:
        raise ValueError("only the J-Link adapter may be compiled")
    return binary


def build_fake(workspace):
    """Compile reviewed test code and pure helpers, never a USB/TCP implementation."""
    output = workspace / "offline-tests"
    output.mkdir(exist_ok=True)
    library = workspace / "libjaylink-0.3.1"
    header = (library / "libjaylink/libjaylink.h").read_text()
    implementation = (HERE / "fake_jaylink.c").read_text()
    pure = ["strutil.c", "util.c", "error.c", "version.c"]
    implemented = set(re.findall(r"\b(jaylink_\w+)\s*\(", implementation))
    implemented.update(re.findall(r"SIMPLE\((jaylink_\w+)\)", implementation))
    for name in pure:
        implemented.update(re.findall(
            r"\b(jaylink_\w+)\s*\(", (library / "libjaylink" / name).read_text()
        ))
    # Unexpected APIs are terminal failures, never successful simulation stubs.
    stubs = ['#include <libjaylink/libjaylink.h>', "#include <stdio.h>", "#include <unistd.h>"]
    for declaration in re.findall(r"^JAYLINK_API ([^;]+);", header, re.M):
        match = re.search(r"\b(jaylink_\w+)\s*\(", declaration)
        if match and match[1] not in implemented:
            stubs.append(declaration + ' { fputs("UNEXPECTED ' + match[1] +
                         '\\n", stderr); _exit(124); }')
    generated = output / "unexpected.c"
    generated.write_text("\n".join(stubs) + "\n")
    fake = output / "libjaylink.so.0"
    command([
        "/usr/bin/gcc", *STRICT, "-Wno-unused-parameter", "-fPIC", "-shared",
        "-Wl,-z,defs", "-Wl,-soname,libjaylink.so.0",
        "-I" + str(workspace / "library/include"),
        "-I" + str(library), "-I" + str(library / "libjaylink"),
        "-I" + str(workspace / "deps/usr/include/libusb-1.0"),
        HERE / "fake_jaylink.c", HERE / "sandbox.c", generated,
        *[library / "libjaylink" / name for name in pure], "-o", fake,
    ])
    if needed(fake) != {"libc.so.6"}:
        raise ValueError("synthetic library unexpectedly links an external backend")
    probe = output / "reset-probe"
    command([
        "/usr/bin/gcc", *STRICT, "-Wno-unused-parameter", "-DHAVE_CONFIG_H",
        "-I" + str(workspace / "build"), "-I" + str(workspace / "openocd/src"),
        "-I" + str(workspace / "build/jimtcl"), "-I" + str(workspace / "openocd/jimtcl"),
        HERE / "reset_probe.c", workspace / "build/src/.libs/libopenocd.a",
        workspace / "build/jimtcl/libjim.a", fake, "-o", probe,
    ])
    if needed(probe) != {"libjaylink.so.0", "libc.so.6"}:
        raise ValueError("unexpected reset-probe dependency")
    return fake, probe


def synthetic_run(binary, fake, script=None, variables=None, arguments=()):
    # Explicit allowlist: no inherited loader hooks, user Tcl configuration or open FDs.
    env = {
        "PATH": "/usr/bin:/bin", "LC_ALL": "C", "HOME": "/nonexistent",
        "LD_LIBRARY_PATH": str(fake.parent), "LD_PRELOAD": str(fake), "LD_BIND_NOW": "1",
    }
    env.update(variables or {})
    args = [str(binary), *arguments]
    if script is not None:
        args += ["-c", PREFIX + script]
    start = time.monotonic()
    result = subprocess.run(
        args, env=env, cwd=fake.parent, close_fds=True, stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=5,
    )
    elapsed = time.monotonic() - start
    if "NS51_FAKE_SANDBOX\n" not in result.stdout or "UNEXPECTED " in result.stdout:
        raise AssertionError(result.stdout)
    return result.returncode, result.stdout, elapsed


def exercise(workspace):
    workspace = Path(workspace)
    binary = validate_workspace(workspace)
    fake, probe = build_fake(workspace)
    results = []

    def case(name, script, ok=True, variables=None, no_reset=True, absent=(), present=()):
        rc, text, duration = synthetic_run(binary, fake, script, variables)
        if (rc == 0) != ok:
            raise AssertionError((name, rc, text))
        events = re.findall(r"^FAKE (\w+)$", text, re.M)
        if no_reset and any(api in events for api in RESET_APIS):
            raise AssertionError((name, "reset submitted", text))
        for marker in absent:
            if marker in text:
                raise AssertionError((name, "forbidden", marker, text))
        for marker in present:
            if marker not in text:
                raise AssertionError((name, "missing", marker, text))
        results.append({"name": name, "returncode": rc, "seconds": duration, "output": text})
        return text

    enabled = CONFIG + "jlink preserve_reset on; "
    for mode in ("", "jlink preserve_reset off; ", "jlink preserve_reset on; jlink preserve_reset off; "):
        case("default-" + mode, CONFIG + mode + "init; shutdown", no_reset=False,
             present=("FAKE jaylink_set_reset", "FAKE jaylink_jtag_set_trst", "FAKE jaylink_close"))
    text = case("default-cached-deassert", CONFIG +
                "init; adapter deassert srst; adapter deassert srst; shutdown",
                no_reset=False, present=("FAKE jaylink_set_reset",))
    if text.count("FAKE jaylink_set_reset\n") != 2:
        raise AssertionError(("default cached reset behavior", text))
    text = case("default-reset-cycle", CONFIG.replace("reset_config none", "reset_config srst_only") +
                "init; adapter assert srst; adapter deassert srst; shutdown", no_reset=False,
                present=("FAKE jaylink_clear_reset",))
    if text.count("FAKE jaylink_set_reset\n") != 2 or text.count("FAKE jaylink_clear_reset\n") != 1:
        raise AssertionError(("default reset cycle", text))
    case("preserved-init-quit", enabled + "init; shutdown",
         present=("INTERFACE 1", "SPEED 1000", "FAKE jaylink_unregister", "FAKE jaylink_close",
                  "FAKE jaylink_exit"), absent=("JTAG_BITS", "SWD_BITS"))
    case("config-only", enabled + "shutdown", absent=("FAKE jaylink_init",))
    for arguments in ("", "on off", "nonsense"):
        case("bad-mode-" + arguments, CONFIG + "jlink preserve_reset " + arguments + "; shutdown",
             ok=False, absent=("FAKE jaylink_init",))
    case("config-only-after-init", enabled + "init; jlink preserve_reset off; shutdown", ok=False,
         present=("FAKE jaylink_close",))
    for config in (
        "adapter driver jlink; transport select swd; adapter speed 1000; reset_config none; ",
        CONFIG.replace("transport select swd", "transport select jtag"),
        CONFIG.replace("reset_config none", "reset_config srst_only"),
        CONFIG.replace("reset_config none", "reset_config trst_only"),
        CONFIG.replace("reset_config none", "reset_config none srst_push_pull"),
    ):
        case("preflight-" + config, config + "jlink preserve_reset on; init; shutdown",
             ok=False, absent=("FAKE jaylink_init",))
    for serial in ("{}", "{ }", "+1", "-1", "0x1", "12x", "4294967296", "999999999999999999999999"):
        case("serial-" + serial, enabled.replace(SERIAL, serial) + "init; shutdown",
             ok=False, absent=("FAKE jaylink_init",))
    case("no-usb", enabled + "init; shutdown", ok=False, variables={"FAKE_NO_USB": "1"},
         absent=("FAKE jaylink_init",))
    for serial in ("0", "4294967295", "000123456789"):
        matches = serial == "000123456789"
        case("valid-decimal-" + serial, enabled.replace(SERIAL, serial) + "init; shutdown",
             ok=matches, present=("FAKE jaylink_init",), absent=("SCAN 2",))
    case("selected-absent", enabled + "init; shutdown", ok=False,
         variables={"FAKE_ABSENT": "1"}, present=("SCAN 1",), absent=("SCAN 2",))
    case("default-network-fallback", CONFIG + "init; shutdown", ok=False,
         variables={"FAKE_ABSENT": "1"}, present=("SCAN 1", "SCAN 2"))
    failures = (
        "jaylink_init", "jaylink_log_set_callback", "jaylink_discovery_scan",
        "jaylink_get_devices", "jaylink_device_get_serial_number", "jaylink_open",
        "jaylink_get_firmware_version", "jaylink_get_caps", "jaylink_get_extended_caps",
        "jaylink_get_hardware_version", "jaylink_get_free_memory", "jaylink_read_raw_config",
        "jaylink_get_hardware_status", "jaylink_register", "jaylink_get_available_interfaces",
        "jaylink_select_interface",
    )
    for failure in failures:
        text = case("failure-" + failure, enabled + "init; shutdown", ok=False,
                    variables={"FAKE_FAIL": failure}, absent=("SCAN 2",))
        if failure not in failures[:6] and text.count("FAKE jaylink_close\n") != 1:
            raise AssertionError(("cleanup", failure, text))
        if failure != "jaylink_init" and text.count("FAKE jaylink_exit\n") != 1:
            raise AssertionError(("exit", failure, text))
    for variable in ("FAKE_LOW_MEMORY", "FAKE_NO_SELECT", "FAKE_NO_SWD", "FAKE_REG_FULL"):
        case(variable, enabled + "init; shutdown", ok=False, variables={variable: "1"},
             present=("FAKE jaylink_close", "FAKE jaylink_exit"))
    # Retain and expose upstream cleanup semantics: unregister reports an error,
    # but adapter_quit does not make it the process exit status.
    case("unregister-failure", enabled + "init; shutdown",
         variables={"FAKE_FAIL": "jaylink_unregister"},
         present=("Error:", "FAKE jaylink_close", "FAKE jaylink_exit"))
    for operation in ("assert", "deassert"):
        for signal in ("srst", "trst"):
            case(operation + "-" + signal, enabled + "init; adapter " + operation +
                 " " + signal + "; shutdown", ok=False, absent=("JTAG_BITS", "SWD_BITS"))
    # reset_config can be altered at EXEC; the retained driver flag still rejects.
    for operation in ("assert", "deassert"):
        case("changed-reset-config-" + operation, enabled +
             "init; reset_config srst_only; adapter " + operation + " srst; shutdown",
             ok=False, absent=("JTAG_BITS", "SWD_BITS"))
    rc, text, duration = synthetic_run(probe, fake)
    marker = "9 reset hook rejections with a nonempty real SWD queue\n"
    if rc or marker not in text:
        raise AssertionError(("reset probe", rc, text))
    before, after = text.split(marker)
    if "FAKE " in before or "JTAG_BITS 64" not in after:
        raise AssertionError(("reset hook flushed or discarded the queue", text))
    results.append({"name": "direct-reset-queue", "returncode": rc, "seconds": duration, "output": text})
    if len(results) != EXPECTED_CASES:
        raise AssertionError(("case inventory changed", len(results)))
    report = {
        "schema": 1, "cases": len(results), "worst_seconds": max(r["seconds"] for r in results),
        "binary_sha256": sha256(binary), "patch_sha256": sha256(HERE / "preserve-reset.patch"),
        "fake_sha256": sha256(fake), "probe_sha256": sha256(probe), "results": results,
    }
    (workspace / "offline-tests/evidence.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workspace", type=Path, required=True)
    arguments = parser.parse_args()
    evidence = exercise(arguments.workspace)
    print(json.dumps({k: v for k, v in evidence.items() if k != "results"}, indent=2))
