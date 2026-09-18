# SPDX-License-Identifier: BSD-3-Clause
"""Synthetic subprocess output only; no USB or target hardware."""
from pathlib import Path
import subprocess
import sys
import unittest
from unittest.mock import patch

from verify_firmware import ROOT

sys.path.insert(0, str(ROOT / "tests"))
from boot_image import simulate


class SimulatorOutputTests(unittest.TestCase):
    def run_output(self, program, *, timeout=None):
        paths = []
        run = subprocess.run

        def process(argv, **kwargs):
            self.assertEqual(argv, ["synthetic-s51", "-t", "C52", "-q", "-c", "-", "fixture.ihx"])
            self.assertEqual(kwargs["timeout"], 15)
            self.assertTrue(kwargs["check"])
            self.assertTrue(kwargs["text"])
            self.assertTrue(kwargs["capture_output"])
            script = Path(kwargs["input"].removeprefix('exec "').removesuffix('"\n'))
            self.assertEqual(script.read_text(), "state\nquit\n")
            paths.append(script)
            if timeout is not None:
                kwargs["timeout"] = timeout
            return run([sys.executable, "-c", program], **kwargs)

        try:
            with patch("boot_image.subprocess.run", side_effect=process):
                return simulate("synthetic-s51", ["state"], Path("fixture.ihx"))
        finally:
            self.assertTrue(paths)
            self.assertTrue(all(not path.exists() for path in paths))

    def test_complete_output_and_newline_translation(self):
        text = "0x25300001\r\n" + "0x0000 aa bb cc dd\r\n"*10000 + "0x25300002\r\n"
        program = 'import sys; sys.stdout.write("0x25300001\\r\\n" + "0x0000 aa bb cc dd\\r\\n"*10000 + "0x25300002\\r\\n")'
        self.assertEqual(self.run_output(program), text.replace("\r\n", "\n"))

    def test_all_command_errors_still_fail(self):
        for text in ("Unknown command", "No such command", "Syntax error", "ERROR: rejected"):
            with self.subTest(text=text), self.assertRaisesRegex(ValueError, "Simulator rejected"):
                self.run_output(f"print({text!r})")

    def test_nonzero_exit_preserves_partial_output_and_stderr(self):
        with self.assertRaises(subprocess.CalledProcessError) as raised:
            self.run_output('import sys; print("partial output"); sys.stderr.write("synthetic error"); sys.exit(3)')
        failure = raised.exception
        self.assertEqual(failure.returncode, 3)
        self.assertEqual(failure.output, "partial output\n")
        self.assertEqual(failure.stderr, "synthetic error")

    def test_timeout_preserves_partial_bytes_and_deadline(self):
        with self.assertRaises(subprocess.TimeoutExpired) as raised:
            self.run_output('import sys,time; sys.stdout.write("partial\\r\\n"); sys.stdout.flush(); '
                            'sys.stderr.write("timed out"); sys.stderr.flush(); time.sleep(30)', timeout=1)
        failure = raised.exception
        self.assertEqual(failure.output, b"partial\r\n")
        self.assertEqual(failure.timeout, 1)
        self.assertEqual(failure.stderr, b"timed out")
