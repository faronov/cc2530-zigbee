# SPDX-License-Identifier: BSD-3-Clause
"""Synthetic subprocess output only; no USB or target hardware."""
from pathlib import Path
import subprocess
import shutil
import sys
import unittest
from unittest.mock import patch

from verify_firmware import ROOT

sys.path.insert(0, str(ROOT / "tests"))
from boot_image import memory_dump, section, simulate, simulate_binary_dumps


class SimulatorOutputTests(unittest.TestCase):
    def test_memory_ranges_require_every_byte_and_preserve_observation_order(self):
        text = "state\n0x0ffe aa bb cc dd\n0x1002 12 34 56 78\n0x1001 69\n"
        self.assertEqual(memory_dump(text, 0x1000, 4), b"\xcc\x69\x12\x34")
        for start, size in ((0xffd, 1), (0x1000, 7)):
            with self.subTest(start=start), self.assertRaisesRegex(ValueError, "Incomplete"):
                memory_dump(text, start, size)
        with self.assertRaisesRegex(ValueError, "Incomplete"):
            memory_dump("0x1000 aa\n0x1002 cc\n", 0x1000, 3)

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

    @unittest.skipUnless(shutil.which("s51"), "s51 required for exact memory observation")
    def test_binary_dump_matches_text_at_every_address(self):
        commands = [
            "memory create chip flash 0x40000 8", "fill flash 0 0x3ffff 0x69",
            "set memory flash 0x3ffff 0xa5", "fill xram 0 0x1eff 0xc7",
            "set memory xram 0x1efe 0x34 0x12",
            "expression /0 0x25300001", "dump /h flash 0x30000 0x3ffff",
            "expression /0 0x25300002", "expression /0 0x25300003",
            "dump /h xram 0 0x1eff", "expression /0 0x25300004",
        ]
        text, binary = simulate("s51", commands), simulate_binary_dumps("s51", commands)
        for marker, start, size in ((1, 0x30000, 0x10000), (3, 0, 0x1f00)):
            self.assertEqual(memory_dump(section(text, marker), start, size),
                             memory_dump(section(binary, marker), start, size))

    def test_missing_or_short_raw_dump_rejected(self):
        def incomplete(simulator, commands):
            command = commands[0]
            path = Path(command.split('>"')[1][:-1])
            path.write_bytes(b"\x69"*255)
            return command+"\n"
        with patch("boot_image.simulate", side_effect=incomplete):
            with self.assertRaisesRegex(ValueError, "Incomplete raw simulator"):
                simulate_binary_dumps("synthetic-s51", ["dump /h iram 0 255"])
