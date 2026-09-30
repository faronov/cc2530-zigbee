# SPDX-License-Identifier: BSD-3-Clause
import unittest

from split_link_spills import scope_header_lines, split_assembly


EXAMPLE = b"""\
\t.module example
\t.optsdcc -mmcs51 --model-large
\t.area JD_example (DATA)
; private compiler spills
Lexample.first$sloc0$0_1$0==.
_first_sloc0_1_0:
\t.ds 4
Lexample.first$sloc1$0_1$0==.
_first_sloc1_1_0:
\t.ds 2
Lexample.second$sloc0$0_1$0==.
_second_sloc0_1_0:
\t.ds 3
\t.area OSEG (OVR,DATA)
\t.area CSEG (CODE)
_first:
\tmov _first_sloc0_1_0,a
\tret
"""


class SplitSpillsTests(unittest.TestCase):
    def test_only_area_directives_change(self):
        output, areas = split_assembly(EXAMPLE)
        self.assertEqual(areas, {"JF_example_first": 6, "JF_example_second": 3})
        for area in areas:
            output = output.replace(f"\t.area {area} (DATA)\n".encode(), b"")
        self.assertEqual(output, EXAMPLE)

    def test_rejects_unclassified_or_mismatched_data(self):
        variants = (
            EXAMPLE.replace(b"mcs51", b"z80"),
            EXAMPLE.replace(b"$0_1$0", b"$0_2$0", 1),
            EXAMPLE.replace(b"_first_sloc0", b"_other_sloc0", 1),
            EXAMPLE.replace(b"\t.ds 4", b"\t.ds 0", 1),
            EXAMPLE.replace(b"\t.ds 4", b"\t.ds 5", 1),
            EXAMPLE.replace(b"; private compiler spills", b"_retained:\n\t.ds 1"),
            EXAMPLE.replace(b"\n", b"\r\n"),
            EXAMPLE.replace(b"JD_example", b"DSEG"),
        )
        for data in variants:
            with self.subTest(data=data):
                with self.assertRaises(ValueError):
                    split_assembly(data)

    def test_rejects_repeat_transformation(self):
        output, _ = split_assembly(EXAMPLE)
        with self.assertRaises(ValueError):
            split_assembly(output)

    def test_long_area_name_without_padding(self):
        output, areas = split_assembly(EXAMPLE.replace(b"JD_example (DATA)", b"JD_example(DATA)"))
        self.assertEqual(areas, {"JF_example_first": 6, "JF_example_second": 3})
        self.assertIn(b"\t.area JD_example(DATA)\n", output)

    def test_header_debug_locations_keep_their_distinct_module_identity(self):
        data=EXAMPLE+b"\tC$shared.h$25$0_0$138 ==.\n\tC$shared.h$25$0_0$138 ==.\n"
        output, names=scope_header_lines(data)
        self.assertEqual(names, {"C$shared.h$25$0_0$138": "C$example.shared.h$25$0_0$138"})
        self.assertEqual(output.replace(b"C$example.shared.h",b"C$shared.h"),data)
        with self.assertRaises(ValueError):
            scope_header_lines(data+b"\tljmp C$shared.h$25$0_0$138\n")


if __name__ == "__main__":
    unittest.main()
