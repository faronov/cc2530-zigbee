#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Synthetic regression cases for the lightweight publication guard."""

import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import check_repository as checker


class RepositoryPolicyTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="cc2530-policy-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.patch = patch.object(checker, "ROOT", self.root)
        self.patch.start()
        self.addCleanup(self.patch.stop)

    def write(self, name, content):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        if isinstance(content, str):
            path.write_text(content, encoding="utf-8")
        else:
            path.write_bytes(content)
        return Path(name)

    def test_regular_source_is_allowed(self):
        path = self.write("src/test.c", "int value = 1;\n")
        self.assertEqual(checker.inspect_file(path), [])

    def test_generated_firmware_is_rejected_even_if_textual(self):
        path = self.write("firmware.hex", ":00000001FF\n")
        self.assertIn("file type", " ".join(checker.inspect_file(path)))

    def test_environment_file_is_rejected(self):
        path = self.write(".env.local", "EXAMPLE=value\n")
        self.assertIn("environment", " ".join(checker.inspect_file(path)))

    def test_nul_bytes_are_rejected(self):
        path = self.write("src/image.c", b"hello\x00world")
        self.assertIn("binary", " ".join(checker.inspect_file(path)))

    def test_non_utf8_is_rejected(self):
        path = self.write("src/image.c", b"\xff")
        self.assertIn("UTF-8", " ".join(checker.inspect_file(path)))

    def test_oversized_file_is_rejected_before_read(self):
        path = self.write("large.txt", b"x" * (256 * 1024 + 1))
        with patch.object(Path, "read_bytes", side_effect=AssertionError("must not read")):
            self.assertIn("size guard", " ".join(checker.inspect_file(path)))

    def test_symlink_is_rejected(self):
        self.write("target.c", "int value;\n")
        (self.root / "link.c").symlink_to("target.c")
        self.assertIn("symbolic", " ".join(checker.inspect_file(Path("link.c"))))

    def test_missing_file_is_rejected(self):
        self.assertIn("regular source", " ".join(checker.inspect_file(Path("missing.c"))))

    def test_personal_path_is_rejected(self):
        synthetic = "/" + "/".join(("Users", "example", "project", "file.c"))
        path = self.write("note.md", synthetic)
        self.assertIn("personal absolute", " ".join(checker.inspect_file(path)))

    def test_credential_match_is_not_printed(self):
        synthetic = "ghp_" + "A" * 30
        path = self.write("note.txt", synthetic)
        errors = " ".join(checker.inspect_file(path))
        self.assertIn("credential-like", errors)
        self.assertNotIn(synthetic, errors)

    def test_private_key_marker_is_rejected(self):
        synthetic = "-----BEGIN " + "PRIVATE KEY-----"
        path = self.write("note.txt", synthetic)
        self.assertIn("credential-like", " ".join(checker.inspect_file(path)))

    def test_existing_relative_link_and_anchor(self):
        self.write("docs/plan.md", "# Scope\n")
        path = self.write("README.md", "[Plan](docs/plan.md#scope)\n")
        self.assertEqual(checker.inspect_file(path), [])

    def test_missing_relative_link_is_rejected(self):
        path = self.write("README.md", "[Plan](docs/missing.md)\n")
        self.assertIn("missing local", " ".join(checker.inspect_file(path)))

    def test_external_and_same_page_links_are_not_fetched(self):
        path = self.write(
            "README.md", "[Web](https://example.invalid/no-file)\n[Here](#scope)\n",
        )
        self.assertEqual(checker.inspect_file(path), [])

    def test_link_escape_is_rejected(self):
        path = self.write("README.md", "[Outside](../outside.md)\n")
        self.assertIn("escapes", " ".join(checker.inspect_file(path)))

    def test_encoded_local_link(self):
        self.write("docs/test plan.md", "# Plan\n")
        path = self.write("README.md", "[Plan](docs/test%20plan.md)\n")
        self.assertEqual(checker.inspect_file(path), [])


if __name__ == "__main__":
    unittest.main()
