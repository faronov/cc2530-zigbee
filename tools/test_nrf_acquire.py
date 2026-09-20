# SPDX-License-Identifier: BSD-3-Clause
"""Real Tcl control flow against an original synthetic, non-device backend."""
from contextlib import redirect_stderr, redirect_stdout
import hashlib
import io
import json
import os
from pathlib import Path
import re
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
set profile $env(NRFP_TEST_PROFILE)
set startup_words {0x8 0x2 0x410fc241 0x0 0x1 0x0 0xffffffff}
if {[string match "startup-enhanced*" $scenario]} {
    lset startup_words 1 0x5
    lset startup_words 6 0xaabbcc5a
    if {$scenario eq "startup-enhanced-erased"} {lset startup_words 6 0xffffffff}
}
if {[regexp {^startup-set-([0-6])-(0x[0-9a-f]+)$} $scenario all index value]} {
    lset startup_words $index $value
}
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
    global scenario snapshots profile startup_words
    if {$args eq "configure -work-area-size 0"} {return}
    action memory {*}$args
    switch -- $args {
        "arp_examine" {return}
        "read_memory 0x10000010 32 2" {set result {0x1000 0x100}}
        "read_memory 0x10000060 32 2" {set result {0x12345678 0x9abcdef0}}
        "read_memory 0x10000100 32 5" {set result {0x52840 0x41414430 0x2004 0x100 0x400}}
        default {
            set start -1
            switch -- $args {
                "read_memory 0x10000130 32 2" {set start 0; set count 2}
                "read_memory 0xe000ed00 32 1" {set start 2; set count 1}
                "read_memory 0x40010400 32 1" {set start 3; set count 1}
                "read_memory 0x4001e400 32 1" {set start 4; set count 1}
                "read_memory 0x4001e504 32 1" {set start 5; set count 1}
                "read_memory 0x10001208 32 1" {set start 6; set count 1}
            }
            if {$start >= 0} {
                if {$profile ne "v3"} {error "v2 attempted startup reads"}
                set result [lrange $startup_words $start [expr {$start + $count - 1}]]
                if {[regexp {^startup-(change|short|malformed)-([0-6])-([123])$} \
                    $scenario all kind index phase] &&
                    $start <= $index && $index < $start + $count && $snapshots >= $phase} {
                    set offset [expr {$index - $start}]
                    switch -- $kind {
                        change {lset result $offset 0x1234}
                        short {set result [lrange $result 1 end]}
                        malformed {lset result $offset {[exec forbidden]}}
                    }
                }
                return $result
            }
            if {[llength $args] != 4 || [lindex $args 0] ne "read_memory" ||
                [lindex $args 2] ne "32" || [lindex $args 3] ne "3" ||
                [lindex $args 1] ni {0x4001e800 0x4001e810 0x4001e820 0x4001e830
                                   0x4001e840 0x4001e850 0x4001e860 0x4001e870}} {
                error "unexpected memory/CPU operation"
            }
            set slot [expr {([lindex $args 1] - 0x4001e800) / 16}]
            set result {0x0 0x0 0x0}
            if {$scenario eq "acl-write-only"} {
                set result [list [format 0x%x [expr {$slot * 4096}]] 0x1000 0x2]
            }
            if {[regexp {^acl-deny-([0-7])-(0x[0-9a-f]+)$} $scenario all selected permission] &&
                $slot == $selected} {
                lset result 2 $permission
            }
            if {$slot == 7} {
                if {$scenario eq "acl-short"} {return {0x0 0x0}}
                if {$scenario eq "acl-bad-word"} {lset result 1 {[exec forbidden]}}
                if {[regexp {^acl-change-(addr|size|perm)-([23])$} $scenario all field phase] &&
                    $snapshots >= $phase} {
                    switch -- $field {
                        addr {lset result 0 0x1000}
                        size {lset result 1 0x1000}
                        perm {lset result 2 0x2}
                    }
                }
            }
        }
    }
    if {$scenario eq "wrong-part" && [lindex $args 1] == 0x10000100} {
        lset result 0 0x52832
    }
    if {[string match "startup-enhanced*" $scenario] && [lindex $args 1] == 0x10000100} {
        lset result 1 0x41414630
    }
    if {[regexp {^startup-variant-(0x[0-9a-f]+)$} $scenario all variant] &&
        [lindex $args 1] == 0x10000100} {lset result 1 $variant}
    if {$scenario eq "changed-identity" && $snapshots == 2 && [lindex $args 1] == 0x10000060} {
        lset result 0 0x12345679
    }
    if {$scenario eq "short-register-list"} {return {}}
    if {$scenario eq "bad-register-word"} {lset result 0 {[exec forbidden]}}
    return $result
}
proc dump_image {path address length} {
    global reads scenario profile startup_words
    action dump $address $length
    incr reads
    if {$address == 0 && $length == 0x100000} {
        set byte A
    } elseif {$address == 0x10001000 && $length == 0x1000} {
        set byte B
    } else {error "unexpected dump region"}
    set data [string repeat $byte $length]
    if {$profile eq "v3" && $address == 0x10001000} {
        set input [open $::env(NRFP_TEST_UICR) rb]
        set data [read $input]
        close $input
    }
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
        self.interpreter = shutil.which(os.environ.get("NRF_ACQUIRE_TEST_INTERPRETER", "tclsh8.6"))
        self.tool('#!' + sys.executable + '\nimport os,sys\n'
                  'os.execv(' + repr(self.interpreter or "/missing/tclsh8.6") + ', ['
                  + repr(self.interpreter or "/missing/tclsh8.6") + ', '
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

    def invoke(self, execute=True, *, startup_binding=False):
        stdout, stderr = io.StringIO(), io.StringIO()
        args = ["--selection", str(self.selection_path), "--operation", str(self.operation)]
        if execute:
            args.append("--execute-read")
        if startup_binding:
            args.append("--startup-binding")
        environment = {"NRFP_TEST_PROFILE": "v3" if startup_binding else "v2"}
        if startup_binding:
            scenario = os.environ["NRFP_TEST_SCENARIO"]
            word = 0xaabbcc5a if scenario == "startup-enhanced" else 0xffffffff
            selected = re.fullmatch(r"startup-set-6-(0x[0-9a-f]+)", scenario)
            if selected:
                word = int(selected[1], 16)
            if scenario == "startup-uicr-mismatch":
                word = 0
            data = bytearray(b"B" * 4096)
            data[520:524] = word.to_bytes(4, "little")
            fixture = self.root / "synthetic-uicr.bin"
            fixture.write_bytes(data)
            environment["NRFP_TEST_UICR"] = str(fixture)
        with redirect_stdout(stdout), redirect_stderr(stderr), \
                patch.dict(os.environ, environment):
            result = acquire.main(args)
        self.assertNotIn(str(self.root), stdout.getvalue() + stderr.getvalue())
        self.assertNotIn("123456789", stdout.getvalue() + stderr.getvalue())
        return result, stdout.getvalue(), stderr.getvalue()

    def require_tcl(self):
        self.assertIsNotNone(self.interpreter, "The selected standalone Tcl interpreter is required")

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
            "acl_read_checks_passed", "atomic_snapshot_verified", "recovery_material_verified",
            "reset_requested", "cpu_control_requested", "target_memory_write_requested",
            "firmware_execution_verified", "restoration_verified", "authorizes_programming",
        })
        self.assertEqual(report["read_passes"], 2)
        self.assertEqual(report["schema"], "nrf52840-readback-v2")
        self.assertEqual(report["region_reads"], 4)
        self.assertEqual(report["target_words"]["part"], 0x52840)
        self.assertIs(report["acl_read_checks_passed"], True)
        for key in ("reset_requested", "cpu_control_requested", "target_memory_write_requested",
                    "firmware_execution_verified", "restoration_verified", "authorizes_programming",
                    "atomic_snapshot_verified", "recovery_material_verified"):
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
        for slot in range(8):
            self.assertEqual(log.count(f"read_memory 0x{0x4001e800 + 16 * slot:08x} 32 3"), 3)
            for field in ("addr", "size", "perm"):
                self.assertEqual(report["target_words"][f"acl_{slot}_{field}"], 0)
        with patch.object(acquire.subprocess, "Popen", side_effect=AssertionError("no retry")):
            self.assertEqual(self.invoke()[0], 1)

    def test_each_failed_real_tcl_backend_operation_stops_before_next(self):
        self.require_tcl()
        self.assertEqual(self.invoke()[0], 0)
        lines = (self.operation / "openocd.txt").read_text().splitlines()
        count = len([line for line in lines if line.startswith("CALL ")])
        self.assertEqual(count, 51)
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

    def test_acl_read_denial_and_unknown_bits_reject_every_slot_before_dumps(self):
        self.require_tcl()
        for slot in range(8):
            for permission in (1, 4, 6, 8, 0x80000000, 0xffffffff):
                scenario = f"acl-deny-{slot}-0x{permission:x}"
                with self.subTest(slot=slot, permission=permission), \
                        patch.dict(os.environ, {"NRFP_TEST_SCENARIO": scenario}):
                    self.operation = self.new_operation()
                    self.assertEqual(self.invoke()[0], 1)
                    log = (self.operation / "openocd.txt").read_text()
                    self.assertNotIn(" dump ", log)
                    self.assertFalse((self.operation / "readback.json").exists())
                    self.assertTrue((self.operation / "consumed.json").exists())
        self.operation = self.new_operation()
        with patch.dict(os.environ, {"NRFP_TEST_SCENARIO": "acl-write-only"}):
            self.assertEqual(self.invoke()[0], 0)
        report = json.loads((self.operation / "readback.json").read_bytes())
        for slot in range(8):
            for field, expected in (("addr", slot * 4096), ("size", 4096), ("perm", 2)):
                self.assertEqual(report["target_words"][f"acl_{slot}_{field}"], expected)
        self.assertFalse(report["authorizes_programming"])
        self.assertFalse(report["recovery_material_verified"])

    def test_acl_changes_and_malformed_reads_never_produce_success(self):
        self.require_tcl()
        cases = [("acl-short", 0), ("acl-bad-word", 0)]
        cases += [(f"acl-change-{field}-{phase}", 2 if phase == 2 else 4)
                  for field in ("addr", "size", "perm") for phase in (2, 3)]
        for scenario, dumps in cases:
            with self.subTest(scenario=scenario), patch.dict(os.environ, {"NRFP_TEST_SCENARIO": scenario}):
                self.operation = self.new_operation()
                self.assertEqual(self.invoke()[0], 1)
                self.assertEqual((self.operation / "openocd.txt").read_text().count(" dump "), dumps)
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
                 0x52840, 0x41414430, 0x2004, 256, 1024) + (0,) * 24

        def transcript(values):
            row = " ".join(f"{v:08x}" for v in values)
            return ("NS51_READ_V2\n" + "".join(f"{label} {row}\n"
                    for label in ("before", "after-first", "after-second")) + "COMPLETE\n").encode()

        good = transcript(words)
        self.assertEqual(acquire.observations(good), dict(zip(acquire.WORDS, words)))
        for bad in (good[:-1], good + b"\n", good.replace(b"\n", b"\r\n"), good[:80],
                    good.replace(b"COMPLETE", b"UNKNOWN"), good.replace(b"02880000", b"02880001", 1),
                    good.replace(b"NS51_READ_V2", b"NS51_READ_V1"), transcript(words[:12])):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                acquire.observations(bad)
        for index in (0, 1, 2, 3, 4, 7, 8, 10, 11):
            values = list(words)
            values[index] = 0
            with self.subTest(index=index), self.assertRaises(ValueError):
                acquire.observations(transcript(values))
        for slot in range(8):
            for bit in range(32):
                values = list(words)
                values[12 + slot * 3 + 2] = 1 << bit
                with self.subTest(slot=slot, bit=bit):
                    if bit == 1:
                        self.assertEqual(acquire.observations(transcript(values))[f"acl_{slot}_perm"], 2)
                    else:
                        with self.assertRaisesRegex(ValueError, "ACL"):
                            acquire.observations(transcript(values))
            for field in range(3):
                values = list(words)
                values[12 + slot * 3 + field] = 2 if field == 2 else 4096
                changed = transcript(values).splitlines(keepends=True)
                mixed = good.splitlines(keepends=True)
                mixed[2] = changed[2]
                with self.subTest(slot=slot, field=field), self.assertRaises(ValueError):
                    acquire.observations(b"".join(mixed))

    def test_v3_default_remains_offline_and_unconsumed(self):
        with patch.object(acquire.subprocess, "Popen", side_effect=AssertionError("no process")):
            result, stdout, stderr = self.invoke(False, startup_binding=True)
        self.assertEqual((result, stderr), (0, ""))
        self.assertIn("offline", stdout)
        self.assertEqual(list(self.operation.iterdir()), [])

    def test_v2_generated_commands_remain_byte_identical_to_the_reviewed_predecessor(self):
        script = acquire.render_script("123456789", 4)
        self.assertEqual(hashlib.sha256(script.encode("ascii")).hexdigest(),
                         "684ca80cef03fbde284ee1936e4fd216919a48129897750a738f4a91a70d18ee")

    def test_v3_reads_exact_extra_words_and_binds_complete_uicr(self):
        self.require_tcl()
        result, stdout, stderr = self.invoke(startup_binding=True)
        self.assertEqual((result, stderr), (0, ""))
        self.assertIn("physical execution remains NO-GO", stdout)
        report = json.loads((self.operation / "readback.json").read_bytes())
        self.assertEqual(report["schema"], "nrf52840-readback-v3")
        self.assertEqual(len(report["target_words"]), 43)
        marker = json.loads((self.operation / "consumed.json").read_bytes())
        self.assertEqual(marker["observation_schema"], "NS51_READ_V3")
        self.assertEqual(marker["schema"], "nrf52840-read-attempt-v2")
        transcript = (self.operation / "observations.txt").read_bytes()
        self.assertEqual(report["observations_sha256"], hashlib.sha256(transcript).hexdigest())
        self.assertEqual(acquire.observations(transcript, startup_binding=True), report["target_words"])
        binding = report["startup_binding"]
        self.assertTrue(binding["source_predicates_met"])
        self.assertEqual(binding["protection_class"], "hardware-only")
        self.assertIs(binding["predicted_whole_word_copy"], False)
        self.assertIsNone(binding["predicted_copy_value"])
        self.assertEqual(binding["physical_execution_decision"], "no-go")
        self.assertEqual(binding["uicr_capture_sha256"], report["agreement"]["regions"][1]["sha256"])
        for name in ("silicon_compatibility_verified", "startup_verified",
                     "debug_access_after_reset_verified", "authorizes_cpu_control",
                     "authorizes_sram_write", "authorizes_programming", "authorizes_rf"):
            self.assertIs(binding[name], False)
        log = (self.operation / "openocd.txt").read_text()
        for address, count in (("10000130", 2), ("e000ed00", 1), ("40010400", 1),
                               ("4001e400", 1), ("4001e504", 1), ("10001208", 1)):
            self.assertEqual(log.count(f"read_memory 0x{address} 32 {count}"), 3)
        self.assertEqual(log.count(" dump "), 4)
        self.assertNotIn("write_memory", log)
        with self.assertRaises(ValueError):
            acquire.observations(transcript)
        with self.assertRaises(ValueError):
            acquire.observations(transcript.replace(b"NS51_READ_V3", b"NS51_READ_V2"))
        with patch.object(acquire.subprocess, "Popen", side_effect=AssertionError("no retry")):
            self.assertEqual(self.invoke(startup_binding=True)[0], 1)

    def test_each_v3_tcl_transfer_error_stops_without_another_call(self):
        self.require_tcl()
        self.assertEqual(self.invoke(startup_binding=True)[0], 0)
        log = (self.operation / "openocd.txt").read_text()
        reference = [line for line in log.splitlines() if line.startswith("CALL ")]
        self.assertEqual(len(reference), 69)
        for failed in range(1, len(reference) + 1):
            with self.subTest(failed=failed), \
                    patch.dict(os.environ, {"NRFP_TEST_SCENARIO": f"fail-{failed}"}):
                self.operation = self.new_operation()
                self.assertEqual(self.invoke(startup_binding=True)[0], 1)
                calls = [line for line in (self.operation / "openocd.txt").read_text().splitlines()
                         if line.startswith("CALL ")]
                self.assertEqual(calls, reference[:failed])
                self.assertTrue((self.operation / "consumed.json").exists())
                self.assertFalse((self.operation / "readback.json").exists())

    def test_every_startup_word_rejects_changes_short_reads_and_malformed_values(self):
        self.require_tcl()
        for index in range(7):
            for kind, phase, dumps in (("change", 2, 2), ("change", 3, 4),
                                       ("short", 1, 0), ("malformed", 1, 0)):
                scenario = f"startup-{kind}-{index}-{phase}"
                with self.subTest(scenario=scenario), \
                        patch.dict(os.environ, {"NRFP_TEST_SCENARIO": scenario}):
                    self.operation = self.new_operation()
                    self.assertEqual(self.invoke(startup_binding=True)[0], 1)
                    log = (self.operation / "openocd.txt").read_text()
                    self.assertEqual(log.count(" dump "), dumps)
                    self.assertFalse((self.operation / "readback.json").exists())

    def test_stable_no_go_facts_produce_explicit_negative_report_and_cli_exit(self):
        self.require_tcl()
        cases = (
            ("startup-set-0-0x0", "unsupported-mdk-selector"),
            ("startup-set-1-0x6", "unsupported-mdk-selector"),
            ("startup-set-1-0x5", "selector-protection-class-conflict"),
            ("startup-set-2-0x410fc231", "unexpected-cortex-m-family"),
            ("startup-set-3-0x1", "watchdog-running"),
            ("startup-set-4-0x0", "nvmc-busy"),
            ("startup-set-5-0x1", "nvmc-not-read-only"),
            ("startup-set-5-0x2", "nvmc-not-read-only"),
            ("startup-set-5-0x3", "nvmc-not-read-only"),
            ("startup-set-6-0xffffff00", "uicr-pall-not-class-disabled"),
            ("startup-variant-0x41414541", "unsupported-production-variant"),
            ("startup-enhanced-erased", "uicr-pall-not-class-disabled"),
        )
        for scenario, blocker in cases:
            with self.subTest(scenario=scenario), \
                    patch.dict(os.environ, {"NRFP_TEST_SCENARIO": scenario}):
                self.operation = self.new_operation()
                result, stdout, stderr = self.invoke(startup_binding=True)
                self.assertEqual((result, stdout), (2, ""))
                self.assertIn("NO-GO", stderr)
                report = json.loads((self.operation / "readback.json").read_bytes())
                self.assertFalse(report["startup_binding"]["source_predicates_met"])
                self.assertEqual(report["startup_binding"]["disposition"], "no-go")
                self.assertIn(blocker, report["startup_binding"]["blockers"])
                self.assertFalse(report["authorizes_programming"])
                with patch.object(acquire.subprocess, "Popen", side_effect=AssertionError("no retry")):
                    self.assertEqual(self.invoke(startup_binding=True)[0], 1)

    def test_enhanced_whole_word_is_not_normalized_to_low_byte(self):
        self.require_tcl()
        with patch.dict(os.environ, {"NRFP_TEST_SCENARIO": "startup-enhanced"}):
            self.assertEqual(self.invoke(startup_binding=True)[0], 0)
        report = json.loads((self.operation / "readback.json").read_bytes())
        binding = report["startup_binding"]
        self.assertTrue(binding["source_predicates_met"])
        self.assertEqual(binding["predicted_copy_value"], 0xaabbcc5a)
        self.assertIn("reserved-uicr-bit-policy", binding["open_gates"])
        self.assertEqual(binding["physical_execution_decision"], "no-go")

    def test_uicr_snapshot_conflict_or_late_capture_mutation_is_not_a_binding(self):
        self.require_tcl()
        with patch.dict(os.environ, {"NRFP_TEST_SCENARIO": "startup-uicr-mismatch"}):
            self.assertEqual(self.invoke(startup_binding=True)[0], 1)
        self.assertFalse((self.operation / "readback.json").exists())
        self.operation = self.new_operation()
        compare = acquire.compare_captures

        def mutate(first, second):
            report = compare(first, second)
            path = first / "uicr.bin"
            data = bytearray(path.read_bytes())
            data[0] ^= 1
            path.write_bytes(data)
            return report

        with patch.object(acquire, "compare_captures", side_effect=mutate):
            self.assertEqual(self.invoke(startup_binding=True)[0], 1)
        self.assertFalse((self.operation / "readback.json").exists())

    def test_observation_profile_cannot_autodetect_downgrade_or_consume_bad_arguments(self):
        self.require_tcl()
        self.assertEqual(self.invoke()[0], 0)
        v2 = (self.operation / "observations.txt").read_bytes()
        for data in (v2, v2.replace(b"NS51_READ_V2", b"NS51_READ_V3"),
                     v2.replace(b"NS51_READ_V2", b"NS51_READ_V1")):
            with self.assertRaises(ValueError):
                acquire.observations(data, startup_binding=True)
        self.operation = self.new_operation()
        for flag in (0, 1, None, "v3"):
            with self.subTest(flag=flag):
                with self.assertRaises(ValueError):
                    acquire.render_script("123456789", 4, startup_binding=flag)
                with self.assertRaises(ValueError):
                    acquire.observations(v2, startup_binding=flag)
                with self.assertRaises(ValueError):
                    acquire.acquire(self.selected, self.operation, startup_binding=flag)
                self.assertEqual(list(self.operation.iterdir()), [])


class StartupBindingTests(unittest.TestCase):
    def values(self, variant=0x41414430, selector=2, word=0xffffffff):
        base = (0x02880000, 1, 0x24770011, 4096, 256, 0x12345678, 0x9abcdef0,
                0x52840, variant, 0x2004, 256, 1024) + (0,) * 24
        return dict(zip(acquire.WORDS_V3, base + (8, selector, 0x410fc241, 0, 1, 0, word)))

    def capture(self, values):
        data = bytearray(b"\xa5" * 4096)
        data[0x208:0x20c] = values["uicr_approtect"].to_bytes(4, "little")
        return bytes(data)

    def test_bounded_class_selector_matrix_never_approves_future_default(self):
        known = (0x41414330, 0x41414430, 0x41414431, 0x41414630)
        for variant in known + (0x41414541, 0x41414141, 0x41414641, 0x42414141, 0x41414730):
            for first in (0, 8, 0xffffffff):
                for second in (0, 1, 2, 3, 4, 5, 6, 0xffffffff):
                    with self.subTest(variant=variant, first=first, second=second):
                        enhanced = variant == 0x41414630
                        values = self.values(variant, second, 0xffffff5a if enhanced else 0xffffffff)
                        values["mdk_selector_0"] = first
                        report = acquire.assess_startup(values, self.capture(values))
                        supported = first == 8 and second <= 5
                        copies = first == 8 and second >= 5
                        expected = variant in known and supported and copies == enhanced
                        self.assertIs(report["source_predicates_met"], expected)
                        self.assertIs(report["predicted_whole_word_copy"], copies)
                        self.assertIs(report["mdk_selector_supported"], supported)
                        self.assertEqual(report["physical_execution_decision"], "no-go")
                        self.assertFalse(report["authorizes_cpu_control"])

    def test_all_pall_encodings_and_reserved_bits_remain_distinct(self):
        for variant, selector, disabled in ((0x41414430, 2, 0xff), (0x41414630, 5, 0x5a)):
            for pall in range(256):
                for upper in (0, 0xffffff00, 0x12345600):
                    values = self.values(variant, selector, upper | pall)
                    report = acquire.assess_startup(values, self.capture(values))
                    self.assertIs(report["source_predicates_met"], pall == disabled)
                    self.assertEqual(report["uicr_approtect_word"], upper | pall)
                    self.assertEqual(report["predicted_copy_value"],
                                     upper | pall if selector == 5 else None)
                    self.assertIn("reserved-uicr-bit-policy", report["open_gates"])

    def test_missing_legacy_extra_or_untyped_fields_and_partial_uicr_are_rejected(self):
        good = self.values()
        cases = [{name: good[name] for name in acquire.WORDS}, dict(good, unreviewed=1)]
        for field in acquire.WORDS_V3:
            missing = good.copy()
            del missing[field]
            cases.append(missing)
            for invalid in (None, True, -1, 0x100000000, "0x8"):
                cases.append(dict(good, **{field: invalid}))
        for values in cases:
            with self.assertRaises(ValueError):
                acquire.assess_startup(values, self.capture(good))
        for data in (bytes(4095), bytes(4097), bytearray(self.capture(good)), bytes(4096)):
            with self.assertRaises(ValueError):
                acquire.assess_startup(good, data)

    def test_binding_cannot_bypass_geometry_protection_or_acl_checks(self):
        good = self.values()
        for field in ("ctrl_id", "approtect_status", "mem_ap_id", "page_size",
                      "page_count", "part", "variant", "ram_kib", "flash_kib"):
            values = dict(good, **{field: 0})
            with self.subTest(field=field), self.assertRaises(ValueError):
                acquire.assess_startup(values, self.capture(good))
        for slot in range(8):
            for bit in range(32):
                values = dict(good, **{f"acl_{slot}_perm": 1 << bit})
                if bit == 1:
                    self.assertTrue(acquire.assess_startup(values, self.capture(good))["source_predicates_met"])
                else:
                    with self.assertRaises(ValueError):
                        acquire.assess_startup(values, self.capture(good))

    def test_documented_status_masks_not_reserved_bits_decide_sampled_predicates(self):
        values = self.values()
        values.update(cpuid=0x41afc24f, wdt_runstatus=0xfffffffe,
                      nvmc_ready=0xffffffff, nvmc_config=0xfffffffc)
        self.assertTrue(acquire.assess_startup(values, self.capture(values))["source_predicates_met"])


if __name__ == "__main__":
    unittest.main()
