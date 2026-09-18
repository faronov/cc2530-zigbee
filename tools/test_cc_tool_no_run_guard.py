# SPDX-License-Identifier: BSD-3-Clause
"""Compile the Linux guard against an original fake USB library, never hardware."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


FAKE = r"""
#include <stdint.h>
#include <stdio.h>
struct libusb_device_handle;
int libusb_control_transfer(struct libusb_device_handle *handle, uint8_t type,
                            uint8_t request, uint16_t value, uint16_t index,
                            unsigned char *data, uint16_t length, unsigned int timeout)
{
    if ((uintptr_t)handle != 0x1234 || timeout != 789 || (length && data[0] != 0x69))
        return -99;
    printf("REAL:%u:%u:%u:%u:%u\n", type, request, value, index, length);
    return -7;
}
"""
DRIVER = r"""
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
struct libusb_device_handle;
int libusb_control_transfer(struct libusb_device_handle *, uint8_t, uint8_t,
                            uint16_t, uint16_t, unsigned char *, uint16_t, unsigned int);
int main(int argc, char **argv)
{
    unsigned char bytes[3] = {0x69, 0x96, 0xa5};
    unsigned int length;
    int result;
    if (argc != 6) return 2;
    length = (unsigned int)strtoul(argv[5], NULL, 10);
    if (length > sizeof(bytes)) return 2;
    puts("BEFORE");
    result = libusb_control_transfer((struct libusb_device_handle *)(uintptr_t)0x1234,
        (uint8_t)strtoul(argv[1], NULL, 10), (uint8_t)strtoul(argv[2], NULL, 10),
        (uint16_t)strtoul(argv[3], NULL, 10), (uint16_t)strtoul(argv[4], NULL, 10),
        length ? bytes : NULL, (uint16_t)length, 789);
    printf("RETURN:%d\n", result);
    return 0;
}
"""


@unittest.skipUnless(sys.platform.startswith("linux"), "Linux ELF guard; no portable preload claim")
class NoRunGuardTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("cc")
        if compiler is None:
            raise unittest.SkipTest("Host C compiler unavailable")
        cls.directory = tempfile.TemporaryDirectory(prefix="cc-tool-no-run-test-")
        cls.addClassCleanup(cls.directory.cleanup)
        cls.root = Path(cls.directory.name)
        cls.guard = cls.root / "guard.so"
        cls.driver = cls.root / "driver"
        (cls.root / "fake.c").write_text(FAKE, encoding="ascii")
        (cls.root / "driver.c").write_text(DRIVER, encoding="ascii")
        flags = [compiler, "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror", "-pedantic"]
        commands = (
            flags + ["-shared", "-fPIC", str(Path(__file__).with_name("cc_tool_no_run_guard.c")),
                     "-ldl", "-o", str(cls.guard)],
            flags + ["-shared", "-fPIC", str(cls.root / "fake.c"), "-o", str(cls.root / "libfake.so")],
            flags + [str(cls.root / "driver.c"), "-L" + str(cls.root), "-Wl,-rpath," + str(cls.root),
                     "-lfake", "-o", str(cls.driver)],
            flags + [str(cls.root / "driver.c"), str(cls.guard),
                     "-o", str(cls.root / "without-backend")],
        )
        for command in commands:
            subprocess.run(command, check=True, capture_output=True, text=True, timeout=30)

    def invoke(self, values, *, preload=True):
        environment = dict(os.environ)
        environment.pop("LD_PRELOAD", None)
        environment.pop("LD_LIBRARY_PATH", None)
        if preload:
            environment["LD_PRELOAD"] = str(self.guard)
        return subprocess.run([str(self.driver), *(str(value) for value in values)],
                              env=environment, capture_output=True, text=True, timeout=5)

    def test_blocks_normal_reset_before_backend_or_return(self):
        result = self.invoke((0x40, 0xc9, 0, 0, 0))
        self.assertEqual(result.returncode, 86)
        self.assertEqual(result.stdout, "BEFORE\n")
        self.assertEqual(result.stderr, "cc-tool-no-run-guard: active\n"
                         "cc-tool-no-run-guard: reset-to-run or unreviewed reset blocked before USB\n")

    def test_rejects_unreviewed_out_reset_shapes(self):
        for values in ((0x40, 0xc9, 1, 1, 0), (0x40, 0xc9, 0, 2, 0),
                       (0x40, 0xc9, 0, 1, 1), (0x41, 0xc9, 0, 1, 0),
                       (0, 0xc9, 0, 0, 0)):
            with self.subTest(values=values):
                result = self.invoke(values)
                self.assertEqual(result.returncode, 86)
                self.assertEqual(result.stdout, "BEFORE\n")
                self.assertIn("blocked before USB", result.stderr)

    def test_forwards_debug_reset_and_unrelated_control_exactly(self):
        for values in ((0x40, 0xc9, 0, 1, 0), (0xc0, 0xc0, 0, 0, 3),
                       (0x40, 0xc5, 0, 0, 0), (0x40, 0xc8, 1, 0, 3),
                       (0xc0, 0xc9, 0, 0, 0)):
            with self.subTest(values=values):
                result = self.invoke(values)
                self.assertEqual(result.returncode, 0)
                self.assertEqual(result.stdout, "BEFORE\nREAL:" + ":".join(map(str, values)) + "\nRETURN:-7\n")
                self.assertEqual(result.stderr, "cc-tool-no-run-guard: active\n")

    def test_negative_control_without_guard_reaches_backend(self):
        result = self.invoke((0x40, 0xc9, 0, 0, 0), preload=False)
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stdout, "BEFORE\nREAL:64:201:0:0:0\nRETURN:-7\n")
        self.assertEqual(result.stderr, "")

    def test_missing_backend_is_failure_not_a_successful_transfer(self):
        environment = dict(os.environ)
        environment.pop("LD_PRELOAD", None)
        result = subprocess.run([str(self.root / "without-backend"), "64", "201", "0", "1", "0"],
                                env=environment, capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 87)
        self.assertNotIn("RETURN:", result.stdout)
        self.assertNotIn("REAL:", result.stdout)
        self.assertIn("missing real libusb_control_transfer", result.stderr)

    def test_log_flush_failure_still_never_submits_reset(self):
        environment = dict(os.environ, LD_PRELOAD=str(self.guard))
        environment.pop("LD_LIBRARY_PATH", None)
        with open("/dev/full", "wb") as full:
            result = subprocess.run([str(self.driver), "64", "201", "0", "0", "0"],
                                    env=environment, stdout=full, stderr=subprocess.PIPE,
                                    text=True, timeout=5)
        self.assertEqual(result.returncode, 87)
        self.assertIn("blocked before USB", result.stderr)
        self.assertIn("buffered output flush failed", result.stderr)


if __name__ == "__main__":
    unittest.main()
