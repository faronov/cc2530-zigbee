# SPDX-License-Identifier: BSD-3-Clause
"""Synthetic private files only: no Nordic SDK, programmer, USB or serial."""
from contextlib import redirect_stderr, redirect_stdout
import hashlib
import io
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import nrf_recovery as recovery
import private_artifacts


@unittest.skipUnless(os.name == "posix" and hasattr(os, "O_NOFOLLOW"), "POSIX private-file policy")
class RecoveryTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="nrf-synthetic-")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.first, self.second = self.root / "first", self.root / "second"
        for directory in (self.first, self.second):
            directory.mkdir(mode=0o700)
            for name, _, size in recovery.REGIONS:
                pattern = bytes(range(251))
                self.write(directory / name, (pattern * (size // len(pattern) + 1))[:size])

    @staticmethod
    def write(path, data):
        path.write_bytes(data)
        path.chmod(0o600)

    def invoke(self, report=None):
        stdout, stderr = io.StringIO(), io.StringIO()
        with redirect_stdout(stdout), redirect_stderr(stderr):
            result = recovery.main(["--first", str(self.first), "--second", str(self.second),
                                    "--report", str(report or self.root / "agreement.json")])
        return result, stdout.getvalue(), stderr.getvalue()

    def test_exact_report_shape_does_not_authorize_hardware(self):
        report = recovery.compare_captures(self.first, self.second)
        self.assertEqual(set(report), {
            "schema", "evidence", "geometry", "regions", "physical_origin_verified",
            "independent_acquisition_verified", "firmware_execution_verified",
            "debug_access_verified", "restoration_verified", "authorizes_programming",
        })
        self.assertEqual(report["schema"], "nrf52840-artifact-agreement-v1")
        self.assertEqual(report["evidence"], "offline-file-agreement-only")
        self.assertEqual(report["geometry"], "assumed-nrf52840-not-device-detected")
        for key in report.keys() - {"schema", "evidence", "geometry", "regions"}:
            self.assertIs(report[key], False)
        self.assertEqual(report["regions"], [
            {"name": name, "address": address, "bytes": size,
             "sha256": hashlib.sha256((self.first / name).read_bytes()).hexdigest()}
            for name, address, size in recovery.REGIONS
        ])

    def test_private_readonly_captures_are_accepted_without_modification(self):
        for directory in (self.first, self.second):
            for path in directory.iterdir():
                path.chmod(0o400)
        recovery.compare_captures(self.first, self.second)
        for directory in (self.first, self.second):
            for path in directory.iterdir():
                self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o400)

    def test_blank_agreement_is_not_a_firmware_or_restoration_claim(self):
        for directory in (self.first, self.second):
            for name, _, size in recovery.REGIONS:
                self.write(directory / name, b"\xff" * size)
        report = recovery.compare_captures(self.first, self.second)
        self.assertFalse(report["firmware_execution_verified"])
        self.assertFalse(report["restoration_verified"])
        self.assertFalse(report["authorizes_programming"])

    def test_mismatches_at_each_extent_boundary(self):
        for name, _, size in recovery.REGIONS:
            path = self.second / name
            original = path.read_bytes()
            for offset in (0, size // 2, size - 1):
                with self.subTest(name=name, offset=offset):
                    changed = bytearray(original)
                    changed[offset] ^= 1
                    self.write(path, changed)
                    with self.assertRaisesRegex(ValueError, "captures differ"):
                        recovery.compare_captures(self.first, self.second)
            self.write(path, original)

    def test_truncated_or_overlong_capture_rejected_before_reading(self):
        for name, _, size in recovery.REGIONS:
            path = self.first / name
            original = path.read_bytes()
            for length in (0, size - 1, size + 1):
                with self.subTest(name=name, length=length):
                    self.write(path, b"\0" * length)
                    with private_artifacts.private_directory(self.first) as directory:
                        with patch.object(recovery.os, "read", side_effect=AssertionError("must not read")):
                            with self.assertRaisesRegex(ValueError, "exactly"):
                                recovery._read_capture(directory, name, size)
            self.write(path, original)

    def test_symlinks_directories_and_fifos_are_not_opened_as_captures(self):
        path = self.first / "main-flash.bin"
        path.unlink()
        path.symlink_to(self.second / path.name)
        with self.assertRaisesRegex(ValueError, "regular"):
            recovery.compare_captures(self.first, self.second)
        path.unlink()
        path.mkdir(mode=0o700)
        with self.assertRaisesRegex(ValueError, "regular"):
            recovery.compare_captures(self.first, self.second)
        path.rmdir()
        os.mkfifo(path, mode=0o600)
        with self.assertRaisesRegex(ValueError, "regular"):
            recovery.compare_captures(self.first, self.second)

    def test_hardlinks_and_same_directory_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "distinct capture directories"):
            recovery.compare_captures(self.first, self.first)
        path = self.second / "main-flash.bin"
        path.unlink()
        os.link(self.first / path.name, path)
        with self.assertRaisesRegex(ValueError, "single-link"):
            recovery.compare_captures(self.first, self.second)

    def test_bad_permissions_owners_and_path_components(self):
        path = self.first / "main-flash.bin"
        for mode in (0o000, 0o200, 0o640, 0o644, 0o700):
            with self.subTest(mode=mode):
                path.chmod(mode)
                with self.assertRaisesRegex(ValueError, "private"):
                    recovery.compare_captures(self.first, self.second)
        path.chmod(0o600)
        self.first.chmod(0o755)
        with self.assertRaisesRegex(ValueError, "0700"):
            recovery.compare_captures(self.first, self.second)
        self.first.chmod(0o700)
        with patch.object(private_artifacts.os, "getuid", return_value=os.getuid() + 1):
            with self.assertRaisesRegex(ValueError, "user-owned"):
                recovery.compare_captures(self.first, self.second)
        link = self.root / "alias"
        link.symlink_to(self.first, target_is_directory=True)
        with self.assertRaises(OSError):
            recovery.compare_captures(link, self.second)
        link.unlink()
        link.symlink_to(link)
        with self.assertRaises((ValueError, OSError)):
            recovery.compare_captures(link, self.second)
        for bad in ("relative", self.first / ".." / "first", private_artifacts.ROOT):
            with self.subTest(path=bad), self.assertRaises(ValueError):
                recovery.compare_captures(bad, self.second)

    def test_bounded_short_reads_and_premature_eof(self):
        read = os.read
        with patch.object(recovery.os, "read", side_effect=lambda fd, size: read(fd, min(size, 701))):
            recovery.compare_captures(self.first, self.second)
        with patch.object(recovery.os, "read", return_value=b""):
            with self.assertRaisesRegex(ValueError, "ended before"):
                recovery.compare_captures(self.first, self.second)

    def test_file_owner_is_checked_independently_of_directory_owner(self):
        path = self.first / "main-flash.bin"
        info = path.stat()
        changed = SimpleNamespace(st_mode=info.st_mode, st_uid=info.st_uid + 1,
                                  st_nlink=info.st_nlink, st_size=info.st_size)
        with self.assertRaisesRegex(ValueError, "user-owned"):
            recovery._validate_file(changed, path.name, info.st_size)

    def test_replaced_file_is_rejected_before_reading(self):
        fstat = os.fstat

        def replaced(fd):
            info = fstat(fd)
            if not stat.S_ISREG(info.st_mode):
                return info
            fields = ("st_dev", "st_ino", "st_mode", "st_uid", "st_nlink", "st_size",
                      "st_mtime_ns", "st_ctime_ns")
            changed = SimpleNamespace(**{name: getattr(info, name) for name in fields})
            changed.st_ino += 1
            return changed

        with patch.object(recovery.os, "fstat", side_effect=replaced):
            with patch.object(recovery.os, "read", side_effect=AssertionError("must not read")):
                with self.assertRaisesRegex(ValueError, "changed before"):
                    recovery.compare_captures(self.first, self.second)

    def test_mutation_during_read_is_rejected(self):
        read = os.read
        path = self.first / "main-flash.bin"
        changed = False

        def mutate(fd, size):
            nonlocal changed
            data = read(fd, size)
            if not changed:
                changed = True
                path.chmod(0o400)
            return data

        with patch.object(recovery.os, "read", side_effect=mutate):
            with self.assertRaisesRegex(ValueError, "changed while"):
                recovery.compare_captures(self.first, self.second)

    def test_cli_private_report_and_no_overwrite(self):
        path = self.root / "agreement.json"
        result, stdout, stderr = self.invoke(path)
        self.assertEqual(result, 0)
        self.assertIn("NOT verified", stdout)
        self.assertEqual(stderr, "")
        self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o600)
        report = path.read_bytes()
        self.assertFalse(json.loads(report)["authorizes_programming"])
        result, stdout, stderr = self.invoke(path)
        self.assertEqual(result, 1)
        self.assertEqual(stdout, "")
        self.assertIn("errno", stderr)
        self.assertNotIn(str(self.root), stderr)
        self.assertEqual(path.read_bytes(), report)

    def test_cli_missing_or_mismatched_input_does_not_create_report(self):
        path = self.second / "uicr.bin"
        path.unlink()
        result, stdout, stderr = self.invoke()
        self.assertEqual((result, stdout), (1, ""))
        self.assertNotIn(str(self.root), stderr)
        self.assertFalse((self.root / "agreement.json").exists())
        self.write(path, b"\0" * 0x1000)
        result, stdout, stderr = self.invoke()
        self.assertEqual((result, stdout), (1, ""))
        self.assertIn("captures differ", stderr)
        self.assertFalse((self.root / "agreement.json").exists())

    def test_report_symlink_and_unsafe_directory_are_rejected(self):
        report = self.root / "report"
        original = (self.first / "main-flash.bin").read_bytes()
        report.symlink_to(self.first / "main-flash.bin")
        result, stdout, _ = self.invoke(report)
        self.assertEqual((result, stdout), (1, ""))
        self.assertEqual((self.first / "main-flash.bin").read_bytes(), original)
        unsafe = self.root / "unsafe"
        unsafe.mkdir(mode=0o755)
        result, stdout, stderr = self.invoke(unsafe / "report")
        self.assertEqual((result, stdout), (1, ""))
        self.assertIn("0700", stderr)
        self.assertFalse((unsafe / "report").exists())

    def test_report_durability_failure_is_not_success(self):
        with patch.object(recovery.os, "fsync", side_effect=OSError(5, "synthetic failure")):
            result, stdout, stderr = self.invoke()
        self.assertEqual((result, stdout), (1, ""))
        self.assertIn("errno 5", stderr)

    def test_standalone_import_does_not_load_device_backends(self):
        result = subprocess.run(
            [sys.executable, "-B", "-c",
             "import sys,nrf_recovery; "
             "assert not {'cc_debugger','serial','usb','pynrfjprog'} & sys.modules.keys()"],
            cwd=Path(__file__).resolve().parent, capture_output=True, text=True, timeout=10,
        )
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
