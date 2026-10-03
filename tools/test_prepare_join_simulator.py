# SPDX-License-Identifier: BSD-3-Clause
import hashlib
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from prepare_join_simulator import build


class SimulatorPreparationTests(unittest.TestCase):
    def test_wrong_archive_cannot_create_or_modify_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive = root / "source.tar.xz"
            archive.write_bytes(b"not the pinned upstream archive")
            output = root / "simulator"
            with self.assertRaisesRegex(ValueError, "Wrong upstream"):
                build(archive, output)
            self.assertFalse(output.exists())

    def test_existing_output_and_missing_tools_fail_before_extraction(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive = root / "source.tar.xz"
            archive.write_bytes(b"synthetic")
            digest = hashlib.sha256(archive.read_bytes()).hexdigest()
            with patch("prepare_join_simulator.ARCHIVE_SHA", digest):
                with self.assertRaisesRegex(ValueError, "new isolated"):
                    build(archive, root)
                with patch("prepare_join_simulator.shutil.which", return_value=None):
                    with self.assertRaisesRegex(ValueError, "Missing simulator build dependency"):
                        build(archive, root / "unused")
                self.assertFalse((root / "unused").exists())
                self.assertEqual(archive.read_bytes(), b"synthetic")


if __name__ == "__main__":
    unittest.main()
