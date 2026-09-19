# SPDX-License-Identifier: BSD-3-Clause
"""Synthetic scan-proof metadata; no compiler, simulator or hardware access."""
import re
import sys
import unittest

from verify_firmware import ROOT, cdb_address

sys.path.insert(0, str(ROOT / "tests"))
import boot_mac_scan as scan


ADDRESS = "L:G$entry$0$0"
DECLARATION = "F:G$entry$0_0$0({2}DF,SC:U),Z,0,0,0,0,0"
LISTING = (
    "   000062            11 _entry:\n"
    "   000062 02 00 08 [24] 12 ljmp target\n"
    "   000065 22       [24] 13 ret\n"
)


class ScanMetadataTests(unittest.TestCase):
    def setUp(self):
        for parser in (scan.cdb_index, scan.records, scan.listing_metrics, scan.label_index):
            parser.cache_clear()

    def test_address_index_preserves_original_validation(self):
        cases = (
            "", ADDRESS + ":a", ADDRESS + ":A\n" + ADDRESS + ":000a",
            ADDRESS + ":A\n" + ADDRESS + ":B",
            ADDRESS + ":", ADDRESS + ":xyz", ADDRESS + ":A:B",
            ADDRESS + ":A\n" + ADDRESS + ":A:B",
            ADDRESS + ":A\n" + ADDRESS + ":\r",
            ADDRESS + ":A\n" + ADDRESS + ":A\u2028B",
            " " + ADDRESS + ":A", ADDRESS + ":A\n" + ADDRESS,
            ADDRESS + "_other:A\n" + ADDRESS + ":B",
        )
        for text in cases:
            with self.subTest(text=text):
                indexed = scan.cdb_index(text)[0].get(ADDRESS, "")
                try:
                    expected = cdb_address(text, ADDRESS)
                except ValueError:
                    with self.assertRaises(ValueError):
                        cdb_address(indexed, ADDRESS)
                else:
                    self.assertEqual(cdb_address(indexed, ADDRESS), expected)

    def test_declarations_preserve_duplicates_conflicts_and_exact_lines(self):
        for extra in (
            DECLARATION,
            DECLARATION.replace("SC:U", "SV:S"),
            DECLARATION + "\r",
            DECLARATION + "\u2028extra",
            "F:G$entry", "F:G$entry_other$0_0$0",
        ):
            text = DECLARATION + "\n" + extra + "\n"
            with self.subTest(extra=extra):
                expected = re.findall(r"^F:G\$entry\$[^\n]*$", text, re.MULTILINE)
                self.assertEqual(scan.cdb_index(text)[1]["entry"], tuple(expected))

    def test_warm_cdb_cache_does_not_hide_changed_text(self):
        good = ADDRESS + ":A\n" + DECLARATION
        addresses, declarations = scan.cdb_index(good)
        self.assertEqual(cdb_address(addresses[ADDRESS], ADDRESS), 10)
        for bad in (good + "\n" + ADDRESS + ":B", good.replace(":A\n", ":G\n")):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                cdb_address(scan.cdb_index(bad)[0][ADDRESS], ADDRESS)
        for bad in (good + "\n" + DECLARATION.replace("SC:U", "SV:S"),
                    good.replace("SC:U", "SV:S")):
            with self.subTest(bad=bad):
                self.assertNotEqual(set(scan.cdb_index(bad)[1]["entry"]), {DECLARATION})
        self.assertEqual(scan.cdb_index(good), (addresses, declarations))
        with self.assertRaises(TypeError):
            addresses[ADDRESS] = ADDRESS + ":B"
        with self.assertRaises(TypeError):
            declarations["entry"] = ()

    def test_instruction_records_and_metrics_are_exact(self):
        self.assertEqual(scan.records(LISTING), ((0x62, b"\x02\0\x08"), (0x65, b"\x22")))
        self.assertEqual(scan.listing_metrics(LISTING),
                         (2, 4, scan.digest("000062:020008\n000065:22\n")))
        with self.assertRaises(TypeError):
            scan.records(LISTING)[0] = (0, b"\0")

    def test_warm_listing_cache_rejects_changed_instruction_sequences(self):
        expected = scan.listing_metrics(LISTING)
        lines = LISTING.splitlines(keepends=True)
        cases = (
            LISTING.replace("02 00 08", "02 00 09"),
            "".join(lines[:1] + lines[2:]),
            "".join(lines[:1] + [lines[1]] + lines[1:]),
            "".join((lines[0], lines[2], lines[1])),
            LISTING.replace("000065", "000066"),
        )
        for text in cases:
            with self.subTest(text=text):
                self.assertNotEqual(scan.listing_metrics(text), expected)
        self.assertEqual(scan.listing_metrics(LISTING), expected)

    def test_labels_keep_duplicate_and_missing_rejections(self):
        self.assertEqual(scan.label(LISTING, "entry"), 0x62)
        for text in (
            LISTING + "   000062   14 _entry:\n",
            LISTING + "   000065   14 _entry:\n",
            LISTING.replace("_entry:", "_entry_other:"),
        ):
            with self.subTest(text=text):
                self.assertEqual(scan.listing_metrics(text), scan.listing_metrics(LISTING))
                with self.assertRaises(ValueError):
                    scan.label(text, "entry")
        moved = LISTING.replace("000062            11", "000064            11")
        self.assertEqual(scan.label(moved, "entry"), 0x64)
        self.assertEqual(scan.label(LISTING, "entry"), 0x62)
        with self.assertRaises(TypeError):
            scan.label_index(LISTING)["entry"] = (0x64,)

    def test_label_index_matches_original_whitespace_and_name_rules(self):
        cases = (
            LISTING,
            LISTING + "  000066 14 _entry_other:\n  000067 15 __entry2:\n",
            "\n\t000062\n  11\n _entry:\n\n  \n",
            "  000062 11 _entry:\r\n  000063 12 __entry2:\r\n",
            LISTING + "  000064 14 _entry:\n",
        )
        for text in cases:
            for name in ("entry", "entry_other", "_entry2"):
                with self.subTest(text=text, name=name):
                    expected = re.findall(
                        rf"^\s*([0-9A-Fa-f]{{6}})\s+\d+\s+_{name}:\s*$",
                        text, re.MULTILINE,
                    )
                    self.assertEqual(scan.label_index(text).get(name, ()),
                                     tuple(int(value, 16) for value in expected))

    def test_parse_caches_have_fixed_bounds(self):
        for n in range(20):
            text = ADDRESS + f":{n:X}"
            scan.cdb_index(text)
            text = LISTING + f"; variant {n}\n"
            scan.records(text)
            scan.listing_metrics(text)
            scan.label_index(text)
        for parser, limit in (
            (scan.cdb_index, 4), (scan.records, 12),
            (scan.listing_metrics, 12), (scan.label_index, 12),
        ):
            with self.subTest(parser=parser.__name__):
                self.assertEqual(parser.cache_info().maxsize, limit)
                self.assertLessEqual(parser.cache_info().currsize, limit)
        self.assertEqual(scan.label(LISTING, "entry"), 0x62)
