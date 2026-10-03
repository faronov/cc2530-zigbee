#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""ASlink wraps long library paths but keeps short paths on one line."""
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from xdata_relocations import linked_objects


class LibraryMapTests(unittest.TestCase):
    members = ("crtclear", "crtxinit", "crtxclear", "gptr_cmp", "crtstart", "_gptrget")

    def check_map(self, separator, foreign=False):
        names = self.members[:-1] + ("foreign",) if foreign else self.members
        text = "Libraries Linked\n" + "".join(
            "/tool/lib.lib" + separator + "[ " + name + ".rel ]\n" for name in names
        ) + "User Base Address Definitions\n"
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "join_smoke_unverified.map").write_text(text, encoding="ascii")

            def extract(command):
                return b"XH3\nM _gptrget\n" if command[-1] == "_gptrget.rel" else b"XH3\n"

            with patch("xdata_relocations.subprocess.check_output", side_effect=extract):
                return linked_objects(root, {"runtime_objects": [], "objects": []})

    def test_single_line(self):
        self.assertEqual([name for name, _ in self.check_map("    ")], list(self.members))

    def test_wrapped(self):
        self.assertEqual([name for name, _ in self.check_map("\n    ")], list(self.members))

    def test_foreign_member(self):
        for separator in ("    ", "\n    "):
            with self.subTest(separator=separator), self.assertRaisesRegex(ValueError, "Unreviewed"):
                self.check_map(separator, foreign=True)


if __name__ == "__main__":
    unittest.main()
