#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Explicit, single-use nRF52840 readback through a reviewed local OpenOCD.

Default invocation validates private selection/tool pins offline. Only
--execute-read starts the selected programmer. No flash writer or CPU-control
command is supplied. The trusted tool/host and exclusive operation directory
are preconditions, not established by a caller-provided digest.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import selectors
import stat
import subprocess
import sys
import time

from nrf_recovery import REGIONS, _fingerprint, _read_capture, compare_captures
from private_artifacts import private_capture, private_directory
from verify_firmware import require


DEADLINE_SECONDS = 180
LOG_LIMIT = 1024 * 1024
SELECTION_SCHEMA = "nrf52840-read-selection-v1"
WORDS = ("ctrl_id", "approtect_status", "mem_ap_id", "page_size", "page_count",
         "device_id_0", "device_id_1", "part", "variant", "package", "ram_kib", "flash_kib") + tuple(
             f"acl_{slot}_{field}" for slot in range(8) for field in ("addr", "size", "perm"))
ACL_ADDRESSES = tuple(0x4001e800 + 0x10 * slot for slot in range(8))
OVERRIDES = ("LD_PRELOAD", "LD_LIBRARY_PATH", "LD_AUDIT", "OPENOCD_SCRIPTS", "TCL_LIBRARY")

SCRIPT = r"""
noinit
gdb_port disabled
tcl_port disabled
telnet_port disabled
adapter driver jlink
adapter serial @SERIAL@
transport select swd
adapter speed 1000
reset_config none
jlink preserve_reset on
swd newdap nrfp cpu -expected-id 0x2ba01477
dap create nrfp.dap -chain-position nrfp.cpu
target create nrfp.mem mem_ap -dap nrfp.dap -ap-num 0 -endian little -defer-examine
nrfp.mem configure -work-area-size 0

proc nrfp_u32 {v} {
    set v [string trim $v]
    if {![regexp {^0x[0-9a-fA-F]{1,8}$} $v]} {error "invalid register response"}
    return [expr {$v + 0}]
}
proc nrfp_control {} {
    set id [nrfp_u32 [nrfp.dap apreg 1 0xfc]]
    set protection [nrfp_u32 [nrfp.dap apreg 1 0x0c]]
    if {$id != 0x02880000 || $protection != 1} {error "CTRL-AP unavailable or protected"}
    return [list $id $protection]
}
proc nrfp_words {address count} {
    set raw [nrfp.mem read_memory $address 32 $count]
    if {[llength $raw] != $count} {error "incomplete register read"}
    set values {}
    foreach v $raw {lappend values [nrfp_u32 $v]}
    return $values
}
proc nrfp_acl {} {
    set values {}
    foreach address {@ACL_ADDRESSES@} {
        set region [nrfp_words $address 3]
        set permission [lindex $region 2]
        if {$permission != 0 && $permission != 2} {
            error "ACL read denial or unknown permission; no reset or write"
        }
        set values [concat $values $region]
    }
    return $values
}
proc nrfp_snapshot {} {
    set values [nrfp_control]
    lappend values [nrfp_u32 [nrfp.dap apreg 0 0xfc]]
    set values [concat $values [nrfp_words 0x10000010 2] \
        [nrfp_words 0x10000060 2] [nrfp_words 0x10000100 5]]
    if {[lindex $values 2] == 0 || [lindex $values 2] == 0xffffffff ||
        [lindex $values 3] != 4096 || [lindex $values 4] != 256 ||
        [lindex $values 7] != 0x52840 ||
        [lindex $values 8] == 0 || [lindex $values 8] == 0xffffffff ||
        [lindex $values 10] != 256 || [lindex $values 11] != 1024} {
        error "not the selected nRF52840 geometry"
    }
    if {([lindex $values 5] == 0 && [lindex $values 6] == 0) ||
        ([lindex $values 5] == 0xffffffff && [lindex $values 6] == 0xffffffff)} {
        error "invalid device identity"
    }
    set values [concat $values [nrfp_acl]]
    set encoded {}
    foreach v $values {lappend encoded [format %08x $v]}
    return [join $encoded " "]
}

init
poll off
nrfp_control
nrfp.mem arp_examine
set out [open @ROOT@/observations.txt a]
puts $out "NS51_READ_V2"
set before [nrfp_snapshot]
puts $out "before $before"
flush $out
dump_image @ROOT@/read-01/main-flash.bin 0x00000000 0x100000
dump_image @ROOT@/read-01/uicr.bin 0x10001000 0x1000
set after_first [nrfp_snapshot]
puts $out "after-first $after_first"
flush $out
if {$after_first ne $before} {error "identity or protection changed during first pass"}
dump_image @ROOT@/read-02/main-flash.bin 0x00000000 0x100000
dump_image @ROOT@/read-02/uicr.bin 0x10001000 0x1000
set after_second [nrfp_snapshot]
puts $out "after-second $after_second"
flush $out
if {$after_second ne $before} {error "identity or protection changed during second pass"}
puts $out "COMPLETE"
close $out
shutdown
"""


def private_bytes(path, limit):
    path = Path(path)
    with private_directory(path.parent) as directory:
        info = os.stat(path.name, dir_fd=directory, follow_symlinks=False)
        require(0 < info.st_size <= limit, "Private input has an invalid size")
        try:
            return _read_capture(directory, path.name, info.st_size)[0]
        except ValueError as error:
            raise ValueError(str(error).replace(path.name, "private input")) from error


def tool_path(value):
    try:
        return Path(value).resolve(strict=True)
    except RuntimeError as error:
        raise ValueError("Tool path contains a symlink loop") from error


def sha256_file(path):
    path = tool_path(path)
    before = os.stat(path, follow_symlinks=False)
    require(stat.S_ISREG(before.st_mode) and 0 < before.st_size <= 64 * 1024 * 1024,
            "Tool input must be a bounded regular file")
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    try:
        require(_fingerprint(os.fstat(fd)) == _fingerprint(before), "Tool file changed before opening")
        digest, remaining = hashlib.sha256(), before.st_size
        while remaining:
            chunk = os.read(fd, min(65536, remaining))
            require(chunk, "Tool file was truncated")
            digest.update(chunk)
            remaining -= len(chunk)
        require(not os.read(fd, 1) and _fingerprint(os.fstat(fd)) == _fingerprint(before)
                and _fingerprint(os.stat(path, follow_symlinks=False)) == _fingerprint(before),
                "Tool file changed while hashing")
        return digest.hexdigest()
    finally:
        os.close(fd)


def selection(path):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            require(key not in result, "Duplicate selection key")
            result[key] = value
        return result

    data = json.loads(private_bytes(path, 16384), object_pairs_hook=unique)
    require(isinstance(data, dict) and set(data) == {
        "schema", "probe_serial", "openocd", "openocd_sha256", "library_dir", "libraries"},
        "Invalid selection fields")
    require(data["schema"] == SELECTION_SCHEMA, "Wrong selection schema")
    serial = data["probe_serial"]
    require(isinstance(serial, str) and re.fullmatch(r"[1-9][0-9]{0,9}", serial)
            and int(serial) <= 0xffffffff, "Expected an explicit decimal probe serial")
    require(isinstance(data["openocd"], str) and Path(data["openocd"]).is_absolute(),
            "OpenOCD path must be absolute")
    executable = tool_path(data["openocd"])
    require(os.access(executable, os.X_OK), "OpenOCD must be executable")
    require(isinstance(data["libraries"], dict) and len(data["libraries"]) <= 32,
            "Invalid selected library pins")
    require(data["library_dir"] is None or
            isinstance(data["library_dir"], str) and Path(data["library_dir"]).is_absolute(),
            "Library directory must be absolute or null")
    require(bool(data["libraries"]) == (data["library_dir"] is not None),
            "Library directory and pins must be supplied together")
    files = [(executable, data["openocd_sha256"])]
    if data["library_dir"] is not None:
        directory = tool_path(data["library_dir"])
        require(directory.is_dir(), "Selected library directory missing")
        for name, digest in data["libraries"].items():
            require(re.fullmatch(r"lib[A-Za-z0-9_+.-]+\.so(?:\.[0-9]+)*", name) is not None,
                    "Invalid library name")
            files.append((directory / name, digest))
        data["library_dir"] = str(directory)
    for file, expected in files:
        require(isinstance(expected, str) and re.fullmatch(r"[0-9a-f]{64}", expected),
                "Invalid tool digest")
        require(sha256_file(file) == expected, "Selected tool or library digest differs")
    data["openocd"] = str(executable)
    return data


def render_script(serial, directory_fd):
    require(isinstance(serial, str) and re.fullmatch(r"[1-9][0-9]{0,9}", serial)
            and int(serial) <= 0xffffffff, "Invalid probe serial")
    require(type(directory_fd) is int and directory_fd >= 0, "Invalid directory descriptor")
    return (SCRIPT.replace("@SERIAL@", serial).replace("@ROOT@", f"/proc/self/fd/{directory_fd}")
            .replace("@ACL_ADDRESSES@", " ".join(f"0x{address:08x}" for address in ACL_ADDRESSES)))


def observations(data):
    require(len(data) <= 2048, "Oversized acquisition observations")
    text = data.decode("ascii")
    rows = text.split("\n")
    require(len(rows) == 6 and rows[0] == "NS51_READ_V2"
            and rows[4:] == ["COMPLETE", ""], "Incomplete acquisition observations")
    snapshots = []
    for label, row in zip(("before", "after-first", "after-second"), rows[1:4]):
        require(re.fullmatch(rf"{label}(?: [0-9a-f]{{8}}){{{len(WORDS)}}}", row) is not None,
                "Malformed acquisition snapshot")
        snapshots.append(tuple(int(word, 16) for word in row.split(" ")[1:]))
    require(snapshots[0] == snapshots[1] == snapshots[2],
            "Acquired identity, geometry or protection changed")
    values = dict(zip(WORDS, snapshots[0]))
    require(values["ctrl_id"] == 0x02880000 and values["approtect_status"] == 1
            and values["mem_ap_id"] not in (0, 0xffffffff)
            and values["page_size"] == 4096 and values["page_count"] == 256
            and values["part"] == 0x52840 and values["ram_kib"] == 256
            and values["flash_kib"] == 1024 and values["variant"] not in (0, 0xffffffff),
            "Unexpected target geometry or protection")
    require((values["device_id_0"], values["device_id_1"])
            not in ((0, 0), (0xffffffff, 0xffffffff)), "Invalid acquired identity")
    require(all(values[f"acl_{slot}_perm"] in (0, 2) for slot in range(8)),
            "ACL read denial or unknown permission; no reset or write")
    return values


def run_programmer(argv, operation, environment, descriptors, log):
    deadline = time.monotonic() + DEADLINE_SECONDS
    process = subprocess.Popen(argv, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, cwd=operation, env=environment,
                               pass_fds=descriptors)
    try:
        total = 0
        with selectors.DefaultSelector() as events:
            events.register(process.stdout, selectors.EVENT_READ)
            while True:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise subprocess.TimeoutExpired("selected programmer", DEADLINE_SECONDS)
                if not events.select(remaining):
                    continue
                chunk = os.read(process.stdout.fileno(), 65536)
                if not chunk:
                    break
                total += len(chunk)
                require(total <= LOG_LIMIT, "Programmer output limit exceeded; no retry")
                log.write(chunk.decode("ascii"))
        result = process.wait(timeout=max(0, deadline - time.monotonic()))
        if result != 0:
            raise subprocess.CalledProcessError(result, "selected programmer")
    finally:
        if process.poll() is None:
            try:
                process.kill()
            except ProcessLookupError:
                pass
        process.wait()
        process.stdout.close()


def acquire(selected, operation):
    require(sys.platform == "linux" and Path("/proc/self/fd").is_dir(),
            "Acquisition requires the reviewed Linux descriptor interface")
    for name in OVERRIDES:
        require(name not in os.environ, f"Unset tool environment override: {name}")
    operation = Path(operation)
    with private_directory(operation) as directory:
        require(not os.listdir(directory), "Operation directory must be new and empty; no retry")
        with private_capture(operation / "consumed.json") as stream:
            json.dump({"schema": "nrf52840-read-attempt-v1", "selection": selected,
                       "programming_authorized": False}, stream, sort_keys=True)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.fsync(directory)
        for pass_name in ("read-01", "read-02"):
            os.mkdir(pass_name, 0o700, dir_fd=directory)
            for name, _, _ in REGIONS:
                with private_capture(operation / pass_name / name):
                    pass
        with private_capture(operation / "observations.txt"):
            pass
        script = render_script(selected["probe_serial"], directory)
        with private_capture(operation / "acquire.tcl") as stream:
            stream.write(script)
            stream.flush()
            os.fsync(stream.fileno())
        os.fsync(directory)
        script_fd = os.open("acquire.tcl", os.O_RDONLY | os.O_NOFOLLOW, dir_fd=directory)
        try:
            environment = {"PATH": "/usr/bin:/bin", "LC_ALL": "C", "HOME": "/nonexistent"}
            if selected["library_dir"] is not None:
                environment["LD_LIBRARY_PATH"] = selected["library_dir"]
            with private_capture(operation / "openocd.txt") as log:
                run_programmer([selected["openocd"], "-f", f"/proc/self/fd/{script_fd}"],
                               operation, environment, (directory, script_fd), log)
                log.flush()
                os.fsync(log.fileno())
        finally:
            os.close(script_fd)
        with private_directory(operation) as current:
            old, new = os.fstat(directory), os.fstat(current)
            require((old.st_dev, old.st_ino) == (new.st_dev, new.st_ino),
                    "Operation directory changed")
        log = private_bytes(operation / "openocd.txt", LOG_LIMIT)
        require(re.search(rb"(?m)^(?:Error|Warn)\s*:", log) is None,
                "Programmer reported an error or warning; inspect the private log")
        identity = observations(private_bytes(operation / "observations.txt", 2048))
        agreement = compare_captures(operation / "read-01", operation / "read-02")
        for pass_name in ("read-01", "read-02"):
            for name, _, _ in REGIONS:
                with (operation / pass_name / name).open("rb") as stream:
                    os.fsync(stream.fileno())
            with private_directory(operation / pass_name) as child:
                os.fsync(child)
        report = {
            "schema": "nrf52840-readback-v2",
            "evidence": "trusted-local-openocd-reported-readback",
            "target_words": identity,
            "read_passes": 2,
            "region_reads": 4,
            "same_debug_connection": True,
            "acl_read_checks_passed": True,
            "atomic_snapshot_verified": False,
            "recovery_material_verified": False,
            "agreement": agreement,
            "script_sha256": hashlib.sha256(script.encode("ascii")).hexdigest(),
            "selection": selected,
            "reset_requested": False,
            "cpu_control_requested": False,
            "target_memory_write_requested": False,
            "firmware_execution_verified": False,
            "restoration_verified": False,
            "authorizes_programming": False,
        }
        with private_capture(operation / "readback.json") as stream:
            json.dump(report, stream, sort_keys=True, indent=2)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.fsync(directory)
        return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--selection", required=True, type=Path, help="Private pinned-tool/probe JSON")
    parser.add_argument("--operation", required=True, type=Path, help="New empty private 0700 directory")
    parser.add_argument("--execute-read", action="store_true", help="Explicitly start one real read attempt")
    args = parser.parse_args(argv)
    try:
        selected = selection(args.selection)
        with private_directory(args.operation) as directory:
            require(not os.listdir(directory), "Operation directory must be new and empty; no retry")
        if not args.execute_read:
            print("Selection/tool pins checked offline; no device opened and no operation consumed.")
            return 0
        acquire(selected, args.operation)
    except (ValueError, UnicodeError) as error:
        # JSON error messages can contain private input fragments.
        message = "Invalid private JSON selection" if isinstance(error, json.JSONDecodeError) else str(error)
        print(f"nrf-acquire: {message}", file=sys.stderr)
        return 1
    except subprocess.SubprocessError:
        print("nrf-acquire: programmer failed or timed out; no retry. Inspect private artifacts.",
              file=sys.stderr)
        return 1
    except OSError as error:
        print(f"nrf-acquire: host file/process operation failed (errno {error.errno}); no retry.",
              file=sys.stderr)
        return 1
    print("Two read passes and sampled ACL checks agree. Restoration and programming are NOT authorized.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
