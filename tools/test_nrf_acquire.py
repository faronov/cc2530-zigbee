# SPDX-License-Identifier: BSD-3-Clause
"""Real Tcl control flow against an original synthetic, non-device backend."""
from contextlib import redirect_stderr, redirect_stdout
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import nrf_acquire as acquire


SYNTHETIC = r"""
set calls 0
set reads 0
set snapshots 0
set scenario $env(NRFP_TEST_SCENARIO)
if {$scenario eq "library-env" && $env(LD_LIBRARY_PATH) ne $env(NRFP_TEST_LIBRARY)} {
    error "wrong selected library environment"
}
proc action {args} {
    global calls scenario
    incr calls
    puts "CALL $calls $args"
    flush stdout
    if {$scenario eq "fail-$calls"} {error "synthetic transfer failure"}
}
proc noinit {} {}
proc gdb_port {v} {if {$v ne "disabled"} {error "server enabled"}}
proc tcl_port {v} {if {$v ne "disabled"} {error "server enabled"}}
proc telnet_port {v} {if {$v ne "disabled"} {error "server enabled"}}
proc adapter {args} {
    if {$args ni {{driver jlink} {serial 123456789} {speed 1000}}} {error "adapter profile"}
}
proc transport {args} {if {$args ne "select swd"} {error "transport profile"}}
proc reset_config {v} {if {$v ne "none"} {error "reset profile"}}
proc jlink {args} {if {$args ne "preserve_reset on"} {error "missing preservation"}}
proc swd {args} {
    if {$args ne "newdap nrfp cpu -expected-id 0x2ba01477"} {error "DAP profile"}
}
proc dap {args} {
    if {$args ne "create nrfp.dap -chain-position nrfp.cpu"} {error "DAP profile"}
}
proc target {args} {
    if {$args ne "create nrfp.mem mem_ap -dap nrfp.dap -ap-num 0 -endian little -defer-examine"} {
        error "target profile"
    }
}
proc init {} {action init}
proc poll {value} {if {$value ne "off"} {error "polling"}}
proc nrfp.dap {args} {
    global scenario snapshots
    action dap {*}$args
    switch -- $args {
        "apreg 1 0xfc" {return 0x02880000}
        "apreg 1 0x0c" {
            if {$scenario eq "locked"} {return 0x0}
            return 0x1
        }
        "apreg 0 0xfc" {incr snapshots; return 0x24770011}
        default {error "unexpected AP access, especially a write"}
    }
}
proc nrfp.mem {args} {
    global scenario snapshots
    if {$args eq "configure -work-area-size 0"} {return}
    action memory {*}$args
    switch -- $args {
        "arp_examine" {return}
        "read_memory 0x10000010 32 2" {set result {0x1000 0x100}}
        "read_memory 0x10000060 32 2" {set result {0x12345678 0x9abcdef0}}
        "read_memory 0x10000100 32 5" {set result {0x52840 0x41414430 0x2004 0x100 0x400}}
        default {error "unexpected memory/CPU operation"}
    }
    if {$scenario eq "wrong-part" && [lindex $args 1] == 0x10000100} {
        lset result 0 0x52832
    }
    if {$scenario eq "changed-identity" && $snapshots == 2 && [lindex $args 1] == 0x10000060} {
        lset result 0 0x12345679
    }
    if {$scenario eq "short-register-list"} {return {}}
    if {$scenario eq "bad-register-word"} {lset result 0 {[exec forbidden]}}
    return $result
}
proc dump_image {path address length} {
    global reads scenario
    action dump $address $length
    incr reads
    if {$address == 0 && $length == 0x100000} {
        set byte A
    } elseif {$address == 0x10001000 && $length == 0x1000} {
        set byte B
    } else {error "unexpected dump region"}
    set data [string repeat $byte $length]
    if {$scenario eq "mismatch" && $reads == 4} {set data [string replace $data 4095 4095 C]}
    if {$scenario eq "truncated" && $reads == 4} {set data [string range $data 1 end]}
    set file [open $path wb]
    puts -nonewline $file $data
    close $file
}
proc shutdown {} {action shutdown}
source [lindex $argv 0]
if {$scenario eq "warning"} {puts "Warn : synthetic uncertainty"}
"""


@unittest.skipUnless(sys.platform == "linux", "Linux descriptor-based manual operator")
class AcquisitionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="nrf-read-synthetic-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.fixture = self.root / "backend.tcl"
        self.fixture.write_text(SYNTHETIC)
        self.executable = self.root / "fake-openocd"
        self.selection_path = self.root / "selection.json"
        self.counter = 0
        self.operation = self.new_operation()
        self.tool('#!' + sys.executable + '\nimport os,sys\n'
                  'os.execv(' + repr(shutil.which("tclsh8.6") or "/missing/tclsh8.6") + ', ['
                  + repr(shutil.which("tclsh8.6") or "/missing/tclsh8.6") + ', '
                  + repr(str(self.fixture)) + ', sys.argv[2]])\n')
        self.environment = patch.dict(os.environ, {"NRFP_TEST_SCENARIO": "good"})
        self.environment.start()
        self.addCleanup(self.environment.stop)
        run = acquire.run_programmer

        def synthetic_run(argv, operation, environment, descriptors, log):
            self.assertEqual(set(environment) - {"LD_LIBRARY_PATH"}, {"PATH", "LC_ALL", "HOME"})
            environment = dict(environment, **{name: value for name, value in os.environ.items()
                                               if name.startswith("NRFP_TEST_")})
            return run(argv, operation, environment, descriptors, log)

        runner = patch.object(acquire, "run_programmer", side_effect=synthetic_run)
        runner.start()
        self.addCleanup(runner.stop)

    def new_operation(self):
        self.counter += 1
        path = self.root / f"operation-{self.counter}"
        path.mkdir(mode=0o700)
        return path

    def tool(self, source):
        self.executable.write_text(source)
        self.executable.chmod(0o700)
        self.selected = {
            "schema": acquire.SELECTION_SCHEMA,
            "probe_serial": "123456789",
            "openocd": str(self.executable),
            "openocd_sha256": hashlib.sha256(self.executable.read_bytes()).hexdigest(),
            "library_dir": None,
            "libraries": {},
        }
        self.save_selection(self.selected)

    def save_selection(self, value):
        self.selection_path.write_text(json.dumps(value))
        self.selection_path.chmod(0o600)

    def invoke(self, execute=True):
        stdout, stderr = io.StringIO(), io.StringIO()
        args = ["--selection", str(self.selection_path), "--operation", str(self.operation)]
        if execute:
            args.append("--execute-read")
        with redirect_stdout(stdout), redirect_stderr(stderr):
            result = acquire.main(args)
        self.assertNotIn(str(self.root), stdout.getvalue() + stderr.getvalue())
        self.assertNotIn("123456789", stdout.getvalue() + stderr.getvalue())
        return result, stdout.getvalue(), stderr.getvalue()

    def require_tcl(self):
        self.assertIsNotNone(shutil.which("tclsh8.6"), "Tcl8.6 is required for offline script execution")

    def test_default_is_offline_and_does_not_consume_or_start_programmer(self):
        with patch.object(acquire.subprocess, "Popen", side_effect=AssertionError("no process")):
            result, stdout, stderr = self.invoke(False)
        self.assertEqual(result, 0)
        self.assertIn("offline", stdout)
        self.assertEqual(stderr, "")
        self.assertEqual(list(self.operation.iterdir()), [])

    def test_generated_tcl_actual_complete_read_flow_and_private_result(self):
        self.require_tcl()
        result, stdout, stderr = self.invoke()
        self.assertEqual((result, stderr), (0, ""))
        self.assertIn("NOT authorized", stdout)
        report = json.loads((self.operation / "readback.json").read_bytes())
        self.assertEqual(set(report), {
            "schema", "evidence", "target_words", "read_passes", "region_reads",
            "same_debug_connection", "agreement", "script_sha256", "selection",
            "reset_requested", "cpu_control_requested", "target_memory_write_requested",
            "firmware_execution_verified", "restoration_verified", "authorizes_programming",
        })
        self.assertEqual(report["read_passes"], 2)
        self.assertEqual(report["region_reads"], 4)
        self.assertEqual(report["target_words"]["part"], 0x52840)
        for key in ("reset_requested", "cpu_control_requested", "target_memory_write_requested",
                    "firmware_execution_verified", "restoration_verified", "authorizes_programming"):
            self.assertIs(report[key], False)
        self.assertFalse(report["agreement"]["physical_origin_verified"])
        for directory in (self.operation, self.operation / "read-01", self.operation / "read-02"):
            self.assertEqual(stat.S_IMODE(directory.stat().st_mode), 0o700)
            for path in directory.iterdir():
                if path.is_file():
                    self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o600)
        log = (self.operation / "openocd.txt").read_text()
        self.assertEqual(log.count(" dump "), 4)
        self.assertEqual(log.count(" init\n"), 1)
        self.assertEqual(log.count(" shutdown\n"), 1)
        self.assertEqual(log.count("read_memory 0x10000060 32 2"), 3)
        with patch.object(acquire.subprocess, "Popen", side_effect=AssertionError("no retry")):
            self.assertEqual(self.invoke()[0], 1)

    def test_each_failed_real_tcl_backend_operation_stops_before_next(self):
        self.require_tcl()
        self.assertEqual(self.invoke()[0], 0)
        lines = (self.operation / "openocd.txt").read_text().splitlines()
        count = len([line for line in lines if line.startswith("CALL ")])
        self.assertEqual(count, 27)
        for failed in range(1, count + 1):
            with self.subTest(failed=failed), patch.dict(os.environ, {"NRFP_TEST_SCENARIO": f"fail-{failed}"}):
                self.operation = self.new_operation()
                self.assertEqual(self.invoke()[0], 1)
                text = (self.operation / "openocd.txt").read_text()
                calls = [line for line in text.splitlines() if line.startswith("CALL ")]
                self.assertEqual(len(calls), failed)
                self.assertFalse((self.operation / "readback.json").exists())
                self.assertTrue((self.operation / "consumed.json").exists())

    def test_protected_wrong_changed_or_malformed_target_never_becomes_backup(self):
        self.require_tcl()
        for scenario, reads in (("locked", 0), ("wrong-part", 0), ("changed-identity", 2),
                                ("short-register-list", 0), ("bad-register-word", 0),
                                ("mismatch", 4), ("truncated", 4), ("warning", 4)):
            with self.subTest(scenario=scenario), patch.dict(os.environ, {"NRFP_TEST_SCENARIO": scenario}):
                self.operation = self.new_operation()
                self.assertEqual(self.invoke()[0], 1)
                self.assertEqual((self.operation / "openocd.txt").read_text().count(" dump "), reads)
                self.assertFalse((self.operation / "readback.json").exists())

    def test_scope_marker_durability_failure_prevents_any_programmer(self):
        with patch.object(acquire.os, "fsync", side_effect=OSError(5, "synthetic")), \
                patch.object(acquire.subprocess, "Popen", side_effect=AssertionError("no process")):
            result, stdout, _ = self.invoke()
        self.assertEqual((result, stdout), (1, ""))
        self.assertTrue((self.operation / "consumed.json").exists())

    def test_late_report_durability_error_is_not_success_and_cannot_relaunch(self):
        self.require_tcl()
        fsync = os.fsync

        def fail_report(fd):
            if os.readlink(f"/proc/self/fd/{fd}").endswith("/readback.json"):
                raise OSError(5, "synthetic report durability failure")
            fsync(fd)

        with patch.object(acquire.os, "fsync", side_effect=fail_report):
            result, stdout, _ = self.invoke()
        self.assertEqual((result, stdout), (1, ""))
        with patch.object(acquire.subprocess, "Popen", side_effect=AssertionError("no retry")):
            self.assertEqual(self.invoke()[0], 1)

    def test_tool_failure_timeout_and_output_bound_never_retry(self):
        programs = [
            "import sys\nprint('synthetic')\nsys.exit(7)\n",
            "import time\nprint('synthetic',flush=True)\ntime.sleep(5)\n",
            "import os\nos.write(1,b'X'*65536)\n",
        ]
        for program in programs:
            self.operation = self.new_operation()
            self.tool("#!" + sys.executable + "\n" + program)
            with self.subTest(program=program), patch.object(acquire, "DEADLINE_SECONDS", 0.1), \
                    patch.object(acquire, "LOG_LIMIT", 1024):
                result, stdout, _ = self.invoke()
            self.assertEqual((result, stdout), (1, ""))
            self.assertTrue((self.operation / "consumed.json").exists())
            self.assertFalse((self.operation / "readback.json").exists())
            with patch.object(acquire.subprocess, "Popen", side_effect=AssertionError("no retry")):
                self.assertEqual(self.invoke()[0], 1)

    def test_selection_scope_and_runtime_bytes_fail_before_process(self):
        variants = [
            dict(self.selected, schema="wrong"),
            dict(self.selected, probe_serial="123; init"),
            dict(self.selected, probe_serial="0123"),
            dict(self.selected, probe_serial="4294967296"),
            dict(self.selected, probe_serial=True),
            dict(self.selected, openocd_sha256="0" * 64),
            dict(self.selected, libraries=[]),
            dict(self.selected, library_dir=str(self.root)),
            dict(self.selected, library_dir=str(self.root), libraries={"../libbad.so": "0" * 64}),
            dict(self.selected, extra="unreviewed"),
        ]
        for value in variants:
            with self.subTest(value=value):
                self.save_selection(value)
                with patch.object(acquire.subprocess, "Popen", side_effect=AssertionError("no process")):
                    self.assertEqual(self.invoke()[0], 1)
                self.assertEqual(list(self.operation.iterdir()), [])
        self.save_selection(self.selected)
        self.executable.write_text("replacement")
        with patch.object(acquire.subprocess, "Popen", side_effect=AssertionError("no process")):
            self.assertEqual(self.invoke()[0], 1)

    def test_pinned_runtime_bytes_and_child_library_environment(self):
        self.require_tcl()
        library_dir = self.root / "runtime"
        library_dir.mkdir(mode=0o700)
        library = library_dir / "libsynthetic.so.0"
        library.write_bytes(b"Synthetic unused library pin, never loaded.")
        self.selected["library_dir"] = str(library_dir)
        self.selected["libraries"] = {library.name: hashlib.sha256(library.read_bytes()).hexdigest()}
        self.save_selection(self.selected)
        with patch.dict(os.environ, {"NRFP_TEST_SCENARIO": "library-env",
                                     "NRFP_TEST_LIBRARY": str(library_dir)}):
            self.assertEqual(self.invoke()[0], 0)
        self.operation = self.new_operation()
        library.write_bytes(b"Changed runtime")
        with patch.object(acquire.subprocess, "Popen", side_effect=AssertionError("no process")):
            self.assertEqual(self.invoke()[0], 1)
        self.assertEqual(list(self.operation.iterdir()), [])

    def test_private_selection_permissions_alias_and_duplicate_keys(self):
        self.selection_path.chmod(0o644)
        self.assertEqual(self.invoke(False)[0], 1)
        self.selection_path.chmod(0o600)
        self.selection_path.write_text('{"schema":"one","schema":"two"}')
        self.assertEqual(self.invoke(False)[0], 1)
        self.selection_path.write_text('{"private-identity-SHOULD-NOT-PRINT":')
        result, stdout, stderr = self.invoke(False)
        self.assertEqual((result, stdout), (1, ""))
        self.assertNotIn("SHOULD-NOT-PRINT", stderr)
        self.selection_path.unlink()
        self.selection_path.symlink_to(self.fixture)
        self.assertEqual(self.invoke(False)[0], 1)

    def test_environment_overrides_and_operation_alias_fail_before_launch(self):
        for name in acquire.OVERRIDES:
            with self.subTest(name=name), patch.dict(os.environ, {name: ""}), \
                    patch.object(acquire.subprocess, "Popen", side_effect=AssertionError("no process")):
                self.assertEqual(self.invoke()[0], 1)
            self.assertEqual(list(self.operation.iterdir()), [])
        original = self.operation
        self.operation = self.root / "alias"
        self.operation.symlink_to(original, target_is_directory=True)
        self.assertEqual(self.invoke()[0], 1)

    def test_tool_fifo_directory_and_loop_are_rejected_without_opening(self):
        fifo = self.root / "fifo"
        os.mkfifo(fifo, mode=0o700)
        loop = self.root / "loop"
        loop.symlink_to(loop)
        for path in (fifo, self.root, loop):
            with self.subTest(path=path), patch.object(acquire.os, "open") as opened:
                with self.assertRaises(ValueError):
                    acquire.sha256_file(path)
                opened.assert_not_called()

    def test_private_selection_filename_is_not_exposed_by_validation_errors(self):
        old = self.selection_path
        self.selection_path = self.root / "private-probe-123456789.json"
        old.rename(self.selection_path)
        self.selection_path.chmod(0o644)
        self.assertEqual(self.invoke(False)[0], 1)

    def test_observation_parser_requires_exact_complete_bound_identity(self):
        words = (0x02880000, 1, 0x24770011, 4096, 256, 0x12345678, 0x9abcdef0,
                 0x52840, 0x41414430, 0x2004, 256, 1024)

        def transcript(values):
            row = " ".join(f"{v:08x}" for v in values)
            return ("NS51_READ_V1\n" + "".join(f"{label} {row}\n"
                    for label in ("before", "after-first", "after-second")) + "COMPLETE\n").encode()

        good = transcript(words)
        self.assertEqual(acquire.observations(good), dict(zip(acquire.WORDS, words)))
        for bad in (good[:-1], good + b"\n", good.replace(b"\n", b"\r\n"), good[:80],
                    good.replace(b"COMPLETE", b"UNKNOWN"), good.replace(b"02880000", b"02880001", 1)):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                acquire.observations(bad)
        for index in (0, 1, 2, 3, 4, 7, 8, 10, 11):
            values = list(words)
            values[index] = 0
            with self.subTest(index=index), self.assertRaises(ValueError):
                acquire.observations(transcript(values))


if __name__ == "__main__":
    unittest.main()
