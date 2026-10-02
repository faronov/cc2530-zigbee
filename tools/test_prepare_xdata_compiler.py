# SPDX-License-Identifier: BSD-3-Clause
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from prepare_xdata_compiler import ARCHIVE_SHA, PATCH_HASHES, ROOT, build, digest


class FrozenCompilerTests(unittest.TestCase):
    def test_exported_patch_series_is_the_reviewed_series(self):
        patches = sorted((ROOT / "experiments/sdcc-function/patches").glob("*.patch"))
        self.assertEqual(tuple(map(digest, patches)), PATCH_HASHES)

    def test_foreign_archive_never_creates_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive = root / "bad.tar.xz"
            archive.write_bytes(b"not SDCC")
            with self.assertRaisesRegex(ValueError, "archive"):
                build(archive, root / "output")
            self.assertFalse((root / "output").exists())

    def test_existing_compiler_is_never_overwritten(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with patch("prepare_xdata_compiler.digest", return_value=ARCHIVE_SHA), \
                    self.assertRaisesRegex(ValueError, "preserved"):
                build(root / "archive", root)
