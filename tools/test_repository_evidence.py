# SPDX-License-Identifier: BSD-3-Clause
"""The one immutable large text ledger never exempts arbitrary generated data."""
import hashlib
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import check_repository as checker


class ReviewedEvidenceTests(unittest.TestCase):
    def test_preserved_public_inventory_passes_all_content_checks(self):
        self.assertEqual(checker.inspect_file(Path("experiments/xdata/inventory.json")), [])

    def test_exception_requires_exact_path_size_and_digest(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            relative = Path("ledger.json")
            raw = b"a" * (256 * 1024 + 1)
            (root / relative).write_bytes(raw)
            accepted = {relative: (len(raw), hashlib.sha256(raw).hexdigest())}
            with patch.object(checker, "ROOT", root), \
                    patch.object(checker, "REVIEWED_GENERATED_TEXT", accepted):
                self.assertEqual(checker.inspect_file(relative), [])
                (root / "another.json").write_bytes(raw)
                self.assertTrue(checker.inspect_file(Path("another.json")))
                (root / relative).write_bytes(b"b" + raw[1:])
                self.assertIn("identity changed", checker.inspect_file(relative)[0])
                (root / relative).write_bytes(raw + b"a")
                self.assertIn("size guard", checker.inspect_file(relative)[0])

    def test_content_checks_still_apply_after_reviewed_size_exception(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            relative = Path("ledger.json")
            for prefix in (b"\0", b"\xff", b"/" + b"home/" + b"example/private/"):
                raw = prefix + b"a" * (256 * 1024)
                (root / relative).write_bytes(raw)
                with patch.object(checker, "ROOT", root), \
                        patch.object(checker, "REVIEWED_GENERATED_TEXT",
                                     {relative: (len(raw), hashlib.sha256(raw).hexdigest())}):
                    self.assertTrue(checker.inspect_file(relative))
