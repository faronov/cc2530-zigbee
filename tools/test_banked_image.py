# SPDX-License-Identifier: BSD-3-Clause
"""Host-only bank identity/packing rejection tests; no hardware transport."""

from pathlib import Path
import tempfile
import unittest

from banked_image import CODE_END, identity, ihex, manifest, pack, physical_address, verify_files
from verify_firmware import parse_ihex


class BankedImageTests(unittest.TestCase):
    def test_all_legal_addresses_are_unique_and_cover_only_executable_flash(self):
        addresses = list(range(0x8000))
        addresses += [(bank << 16) | offset for bank in range(1, 8)
                      for offset in range(0x8000, 0xE800 if bank == 7 else 0x10000)]
        self.assertEqual([physical_address(a) for a in addresses], list(range(CODE_END)))
        for bank in range(1, 8):
            self.assertEqual(identity((bank << 16) | 0x8000),
                             {"virtual": (bank << 16) | 0x8000, "bank": bank,
                              "logical": 0x8000, "physical": bank * 0x8000})

    def test_alias_banks_invalid_types_and_every_excluded_bank_seven_byte(self):
        invalid = [-1, 0x8000, 0xFFFF, 0x10000, 0x17FFF, 0x80000, 0xFFFFFFFF,
                   None, True, 1.0]
        invalid += list(range(0x7E800, 0x80000))
        for address in invalid:
            with self.assertRaises(ValueError):
                physical_address(address)
        for address in range(1, 8):
            with self.assertRaises(ValueError):
                physical_address(address << 16)
        for image in ({}, {0: 256}, {0: -1}, {0: True}, {0: b"a"}):
            with self.assertRaises(ValueError):
                pack(image)

    def test_sparse_hex_preserves_banks_holes_and_record_window_boundaries(self):
        image = {0: 2, 2: 0x69, 0x18000: 0x11, 0x1FFFF: 0x12,
                 0x28000: 0x21, 0x2FFFF: 0x22, 0x78000: 0x71, 0x7E7FF: 0x7F}
        physical = pack(image)
        self.assertEqual(parse_ihex(ihex(physical)), physical)
        self.assertEqual(set(physical), {0, 2, 0x8000, 0xFFFF, 0x10000,
                                        0x17FFF, 0x38000, 0x3E7FF})
        self.assertEqual(len(physical), len(image))
        self.assertEqual([b["bank"] for b in manifest(image)["banks"]], [0, 1, 2, 7])
        self.assertNotEqual(manifest({0: 2, 0x18000: 3}), manifest({0: 2, 0x28000: 3}))
        self.assertNotEqual(manifest({0: 2, 2: 3}), manifest({0: 2, 3: 3}))

    def test_published_artifacts_must_agree_with_the_linker(self):
        import json
        image = {0: 2, 1: 0, 2: 3, 0x18000: 0x11, 0x78000: 0x71}
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            (output / "banked.ihx").write_text(ihex(image), encoding="ascii")
            (output / "banked.hex").write_text(ihex(pack(image)), encoding="ascii")
            path = output / "banked-layout.json"
            path.write_text(json.dumps(manifest(image)), encoding="ascii")
            self.assertEqual(verify_files(output), image)
            (output / "banked.hex").write_text(ihex(image), encoding="ascii")
            with self.assertRaisesRegex(ValueError, "physical HEX"):
                verify_files(output)
            changed = pack(image)
            changed[0x38000] ^= 1
            (output / "banked.hex").write_text(ihex(changed), encoding="ascii")
            with self.assertRaisesRegex(ValueError, "physical HEX"):
                verify_files(output)
            (output / "banked.hex").write_text(ihex(pack(image)), encoding="ascii")
            changed = manifest(image)
            changed["banks"][-1]["first"]["bank"] = 1
            path.write_text(json.dumps(changed), encoding="ascii")
            with self.assertRaisesRegex(ValueError, "layout identity"):
                verify_files(output)

    def test_packing_cannot_smuggle_excluded_bytes_in_an_extended_hex_record(self):
        for forbidden in (0x7E800, 0x7F000, 0x7F800, 0x7FFFF, 0x80000):
            text = ihex({0: 2, forbidden: 0x69})
            with self.assertRaises(ValueError):
                pack(parse_ihex(text))
        for line in (":01000000AA55\n", ":01000000AA00\n", ":00000001FF\n"):
            with self.assertRaises(ValueError):
                parse_ihex(line)

    def test_security_profile_cannot_claim_abi_fixture_identity(self):
        import json
        image = {0: 2, 0x18000: 0x11, 0x28000: 0x22}
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            (output / "banked_security.ihx").write_text(ihex(image), encoding="ascii")
            (output / "banked_security.hex").write_text(ihex(pack(image)), encoding="ascii")
            path = output / "banked_security-layout.json"
            path.write_text(json.dumps(manifest(image, "security")), encoding="ascii")
            self.assertEqual(verify_files(output, "security"), image)
            self.assertEqual(manifest(image, "security")["capability"], "offline-banked-security-fixture")
            path.write_text(json.dumps(manifest(image)), encoding="ascii")
            with self.assertRaisesRegex(ValueError, "layout identity"):
                verify_files(output, "security")
        with self.assertRaisesRegex(ValueError, "Unknown"):
            manifest(image, "arbitrary")


if __name__ == "__main__":
    unittest.main()
