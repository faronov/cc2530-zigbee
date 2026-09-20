# SPDX-License-Identifier: BSD-3-Clause
"""Ordinary discovery is offline; the full linked proof needs an explicit workspace."""

import json
import os
from pathlib import Path
import platform
import subprocess
import tempfile
import unittest

from nrf_openocd import offline


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


if __name__ == "__main__":
    unittest.main()
