# SPDX-License-Identifier: BSD-3-Clause
"""Compact resource accounting rejects missing modules and hidden allocations."""
import unittest

import link_ram_resources as ram


def object_text(module, xdata=1, code=1):
    return (
        f";!FILE src/{module}.c\nXH3\nM {module}\nO -mmcs51 --model-large\n"
        "A REG_BANK_0 size 8 flags 4 addr 0\n"
        "A DSEG size 0 flags 0 addr 0\n"
        "A OSEG size 0 flags 4 addr 0\n"
        "A BSEG size 0 flags 80 addr 0\n"
        f"A XSEG size {xdata:X} flags 40 addr 0\n"
        f"A CSEG size {code:X} flags 20 addr 0\n"
        "A CONST size 0 flags 20 addr 0\n"
    )


class LinkRamResourcesTests(unittest.TestCase):
    def objects(self):
        return {m: object_text(m) for m in ram.MODULES} | {
            ram.PROBE: object_text(ram.PROBE, ram.CONTEXT_BYTES, 0)}

    def test_probe_counts_contexts_once_outside_production(self):
        result = ram.report(self.objects())
        self.assertEqual(len(ram.MODULES), 38)
        self.assertEqual(result["xdata"], 38)
        self.assertEqual(result["object_floor"], 38 + 1433 + 180 + 304)
        self.assertEqual(result["code"], 38)

    def test_missing_or_extra_module_is_not_a_smaller_composition(self):
        original = self.objects()
        for objects in ({k: v for k, v in original.items() if k != "mac_join"},
                        {k: v for k, v in original.items() if k != ram.PROBE},
                        original | {"extra": object_text("extra")}):
            with self.assertRaisesRegex(ValueError, "object set"):
                ram.report(objects)

    def test_named_banked_areas_and_bit_units(self):
        text = object_text("mac_join").replace("A DSEG", "A BJ_mac_join").replace(
            "A CSEG size 1 flags 20", "A CSEG size 0 flags 20") + (
            "A BJ_BANK2 size 10 flags 20 addr 0\n")
        text = text.replace("A BSEG size 0", "A BSEG size 3")
        row = ram.object_areas(text, "mac_join")
        self.assertEqual(row["code"], 16)
        self.assertEqual(row["bits"], 3)

    def test_sdcc_omits_empty_overlay_area(self):
        text = object_text("mac_poll").replace("A OSEG size 0 flags 4 addr 0\n", "")
        self.assertEqual(ram.object_areas(text, "mac_poll")["overlay_sum"], 0)

    def test_incompatible_or_unaccounted_objects_fail(self):
        good = object_text("mac_poll")
        for text in (
            good.replace("XH3", "XL3"),
            good.replace("M mac_poll", "M mac_join"),
            good.replace("--model-large", "--model-small"),
            good.replace("A XSEG size 1 flags 40 addr 0\n", ""),
            good.replace("A REG_BANK_0 size 8", "A REG_BANK_0 size 10"),
            good.replace("A CSEG size 1 flags 20", "A CSEG size 1 flags 0"),
            good.replace("addr 0", "addr 1", 1),
            good + "A XSEG size 0 flags 40 addr 0\n",
            good + "A BJ_mac_poll size 0 flags 0 addr 0\n",
            good + "A XISEG size 1 flags 40 addr 0\n",
            good + "A SSEG size 1 flags 0 addr 0\n",
            good + "A malformed\n",
        ):
            with self.subTest(text=text), self.assertRaises(ValueError):
                ram.object_areas(text, "mac_poll")

    def test_regression_and_probe_changes_are_rejected(self):
        for module, text in (
            ("mac_join", object_text("mac_join", ram.LIMITS["xdata"] + 1)),
            ("mac_join", object_text("mac_join", 1, ram.LIMITS["code"] + 1)),
            (ram.PROBE, object_text(ram.PROBE, ram.CONTEXT_BYTES + 1, 0)),
            (ram.PROBE, object_text(ram.PROBE, ram.CONTEXT_BYTES, 1)),
        ):
            with self.subTest(module=module), self.assertRaises(ValueError):
                ram.report(self.objects() | {module: text})
