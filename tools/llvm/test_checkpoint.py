# SPDX-License-Identifier: BSD-3-Clause
"""Integrity tests of the production replay checkpoint codec."""
import os
from pathlib import Path
import signal
import tempfile
import unittest
from unittest.mock import patch

import llvm_join_replay as replay


class Checkpoint(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.path = Path(self.directory.name)/"case22-step2-ev256.pkl"
        self.bind = dict(elf="elf", case=22, simulator="s51", replay="replay", vector="vector",
                         run_identity="compiler-runtime-firmware-selection",
                         support={"boot_image": "support-code"})
        self.record = dict(index=2, case=22, elf_sha="elf", state=(b"a", b"b", b"c", b"d", b"e"),
                           pc=123, count=256, peak=228, sampled=218, completed=2,
                           retries=3, learned={(("w", 1), ("r", 2)): True}, previous_stop=(123, 4),
                           bind=self.bind, application_checks=0, first=256,
                           adjacency=[False]*512)

    def test_case22_step_and_intra_step_roundtrip(self):
        for first in (0, 256):
            record = self.record | {"first": first}
            want = replay.write_checkpoint(self.path, record)
            self.assertEqual(replay.load_checkpoint(self.path, self.bind, want), record)

    def test_missing_corrupted_and_wrong_record_digest(self):
        with self.assertRaises(FileNotFoundError):
            replay.load_checkpoint(self.path, self.bind)
        want = replay.write_checkpoint(self.path, self.record)
        with self.assertRaises(ValueError):
            replay.load_checkpoint(self.path, self.bind, "different")
        raw = bytearray(self.path.read_bytes())
        raw[-1] ^= 1
        self.path.write_bytes(raw)
        with self.assertRaises(ValueError):
            replay.load_checkpoint(self.path, self.bind, want)

    def test_each_bound_identity_is_required(self):
        replay.write_checkpoint(self.path, self.record)
        for field in self.bind:
            wrong = self.bind | {field: "different"}
            with self.subTest(field=field), self.assertRaises(ValueError):
                replay.load_checkpoint(self.path, wrong)

    def test_failed_atomic_replace_preserves_previous_checkpoint(self):
        want = replay.write_checkpoint(self.path, self.record)
        with patch.object(replay.os, "replace", side_effect=OSError("injected write failure")):
            with self.assertRaises(OSError):
                replay.write_checkpoint(self.path, self.record | {"count": 257})
        self.assertEqual(replay.load_checkpoint(self.path, self.bind, want), self.record)

    def test_sigterm_near_checkpoint_is_a_request_not_partial_state(self):
        with replay.checkpoint_signals() as stop:
            real_replace = os.replace
            with patch.object(replay.os, "replace", side_effect=lambda a, b: (
                    os.kill(os.getpid(), signal.SIGTERM), real_replace(a, b))[-1]):
                want = replay.write_checkpoint(self.path, self.record)
            self.assertTrue(stop.is_set())
        self.assertEqual(replay.load_checkpoint(self.path, self.bind, want), self.record)


if __name__ == "__main__":
    unittest.main()
