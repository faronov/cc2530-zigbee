# SPDX-License-Identifier: BSD-3-Clause
"""Ordinary discovery is offline; the full linked proof needs an explicit workspace."""

import json
import hashlib
import io
import os
from pathlib import Path
import platform
import subprocess
import tempfile
import tarfile
import unittest
from unittest import mock

from nrf_openocd import discovery, offline


class TestOpenocdPreservation(unittest.TestCase):
    def test_locked_inputs(self):
        lock = json.loads((offline.HERE / "dependencies.json").read_text())
        self.assertEqual(lock["openocd"]["commit"], "9ea7f3d647c8ecf6b0f1424002dfc3f4504a162c")
        self.assertEqual(lock["libjaylink"]["version"], "0.3.1")
        for item in lock["packages"] + [lock["libjaylink"]]:
            self.assertRegex(item["sha256"], r"^[0-9a-f]{64}$")

    def test_patch_scope(self):
        patch = (offline.HERE / "preserve-reset.patch").read_text()
        changed = [line.split(" b/")[1] for line in patch.splitlines() if line.startswith("diff --git")]
        self.assertEqual(changed, ["src/jtag/core.c", "src/jtag/drivers/jlink.c", "src/jtag/interface.h"])
        patch = (offline.HERE / "libjaylink-usb-1025.patch").read_text()
        self.assertEqual([line for line in patch.splitlines() if line.startswith("diff --git")],
                         ["diff --git a/libjaylink/discovery_usb.c b/libjaylink/discovery_usb.c"])
        self.assertEqual([line for line in patch.splitlines()
                          if line.startswith("+") and not line.startswith("+++")],
                         ["+\t{0x1025, JAYLINK_USB_ADDRESS_0},"])

    def test_archive_allows_only_exact_reviewed_change(self):
        with tempfile.TemporaryDirectory(prefix="nrf-openocd-source-") as folder:
            root = Path(folder)
            old, new, other = b"old source\n", b"reviewed source\n", b"untouched notice\n"
            archive = root / "source.tar"
            with tarfile.open(archive, "w") as tar:
                for name, content in (("discovery.c", old), ("COPYING", other)):
                    member = tarfile.TarInfo("source/" + name)
                    member.size = len(content)
                    tar.addfile(member, io.BytesIO(content))
            source = root / "source"
            source.mkdir()
            (source / "discovery.c").write_bytes(new)
            (source / "COPYING").write_bytes(other)
            change = {"path": "discovery.c", "before": hashlib.sha256(old).hexdigest(),
                      "after": hashlib.sha256(new).hexdigest()}
            digest = offline.sha256(archive)
            self.assertEqual(len(offline.archive_sources(archive, source, digest, change)), 2)
            for name, content in (("discovery.c", old), ("discovery.c", new + b"extra"),
                                  ("COPYING", b"changed")):
                with self.subTest(name=name, content=content):
                    previous = (source / name).read_bytes()
                    (source / name).write_bytes(content)
                    with self.assertRaises(ValueError):
                        offline.archive_sources(archive, source, digest, change)
                    (source / name).write_bytes(previous)
            for changed in (dict(change, before="0" * 64), dict(change, path="missing")):
                with self.assertRaises(ValueError):
                    offline.archive_sources(archive, source, digest, changed)
            with self.assertRaises(ValueError):
                offline.archive_sources(archive, source, "0" * 64, change)

    def test_patch_hash_and_scope_are_required(self):
        lock = json.loads((offline.HERE / "dependencies.json").read_text())
        with tempfile.TemporaryDirectory(prefix="nrf-openocd-patch-") as folder:
            root = Path(folder)
            (root / "libjaylink-usb-1025.patch").write_text("unreviewed patch")
            (root / "dependencies.json").write_text(json.dumps(lock))
            with mock.patch.object(offline, "HERE", root):
                with self.assertRaisesRegex(ValueError, "patch digest"):
                    offline.validate_library_sources(root)
                lock["libjaylink"]["patch"]["path"] = "libjaylink/target.c"
                (root / "dependencies.json").write_text(json.dumps(lock))
                with self.assertRaisesRegex(ValueError, "patch scope"):
                    offline.validate_library_sources(root)

    def test_workspace_must_be_canonical_absolute(self):
        with self.assertRaises(ValueError):
            offline.validate_workspace(Path("relative/untrusted"))

    @unittest.skipUnless(platform.system() == "Linux" and platform.machine() == "x86_64",
                         "test confinement is Linux x86-64 only")
    def test_device_and_network_syscalls_are_denied(self):
        with tempfile.TemporaryDirectory(prefix="nrf-openocd-sandbox-") as folder:
            executable = Path(folder) / "sandbox"
            offline.command(["/usr/bin/gcc", *offline.STRICT, "-DSANDBOX_SELFTEST",
                             offline.HERE / "sandbox.c", "-o", executable])
            result = subprocess.run([str(executable)], check=True, capture_output=True,
                                    text=True, timeout=5)
            self.assertEqual(result.stdout, "15 denied syscall families\n")
            self.assertEqual(result.stderr, "NS51_FAKE_SANDBOX\n")

    @unittest.skipUnless(os.environ.get("NRF_OPENOCD_WORKSPACE"),
                         "full real-driver proof requires explicit NRF_OPENOCD_WORKSPACE")
    def test_real_linked_driver_with_synthetic_backend(self):
        evidence = offline.exercise(Path(os.environ["NRF_OPENOCD_WORKSPACE"]))
        self.assertEqual(evidence["cases"], offline.EXPECTED_CASES)

    @unittest.skipUnless(os.environ.get("NRF_OPENOCD_WORKSPACE"),
                         "genuine discovery proof requires explicit NRF_OPENOCD_WORKSPACE")
    def test_genuine_discovery_with_strict_fake_libusb(self):
        evidence = discovery.exercise(Path(os.environ["NRF_OPENOCD_WORKSPACE"]))
        self.assertEqual(evidence["cases"], discovery.EXPECTED_CASES)
        self.assertEqual(evidence["processes"], discovery.EXPECTED_PROCESSES)


if __name__ == "__main__":
    unittest.main()
