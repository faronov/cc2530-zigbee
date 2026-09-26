# SPDX-License-Identifier: BSD-3-Clause
"""Compact resource accounting rejects missing modules and hidden allocations."""
import re
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
    def objects(self, workspace=False):
        modules = ram.WORKSPACE_MODULES if workspace else ram.MODULES
        probe = ram.CONTEXT_BYTES + (ram.WORKSPACE_LAYOUT_BYTES if workspace else 0)
        return {m: object_text(m) for m in modules} | {
            ram.PROBE: object_text(ram.PROBE, probe, 0)}

    def test_probe_counts_contexts_once_outside_production(self):
        result = ram.report(self.objects())
        self.assertEqual(len(ram.MODULES), 38)
        self.assertEqual(result["xdata"], 38)
        self.assertEqual(result["object_floor"], 38 + 1433 + 180 + 304)
        self.assertEqual(result["code"], 38)

    def test_workspace_probe_does_not_double_count_the_production_arena(self):
        result = ram.report(self.objects(True), workspace=True)
        self.assertEqual(len(ram.WORKSPACE_MODULES), 39)
        self.assertEqual(ram.WORKSPACE_LAYOUT_BYTES, 617 + 31)
        self.assertEqual(result["xdata"], 39)
        self.assertEqual(result["context_bytes"], 1917)
        self.assertEqual(result["object_floor"], 39 + 1917)

    def test_workspace_and_compact_profiles_are_not_interchangeable(self):
        for objects, workspace in ((self.objects(True), False), (self.objects(), True)):
            with self.assertRaisesRegex(ValueError, "object set"):
                ram.report(objects, workspace=workspace)

    def test_workspace_probe_requires_both_payload_and_ownership_layout(self):
        objects = self.objects(True)
        for extra in (0, 31, 617, ram.WORKSPACE_LAYOUT_BYTES - 1, ram.WORKSPACE_LAYOUT_BYTES + 1):
            bad = objects | {ram.PROBE: object_text(ram.PROBE, ram.CONTEXT_BYTES + extra, 0)}
            with self.subTest(extra=extra), self.assertRaisesRegex(ValueError, "probe"):
                ram.report(bad, workspace=True)

    def test_workspace_ceilings_accept_the_limit_but_reject_one_more(self):
        objects = self.objects(True)
        areas = {"code": "CSEG", "const": "CONST", "xdata": "XSEG",
                 "data_sum": "DSEG", "overlay_sum": "OSEG", "bits": "BSEG"}
        for metric, area in areas.items():
            remaining = 38 if metric in ("code", "xdata") else 0
            size = ram.WORKSPACE_LIMITS[metric] - remaining
            for extra in (0, 1):
                text = re.sub(rf"(A {area} size )[0-9A-F]+",
                              lambda m: m[1] + f"{size + extra:X}", object_text("mac_link_workspace"))
                candidate = objects | {"mac_link_workspace": text}
                with self.subTest(metric=metric, extra=extra):
                    if extra:
                        with self.assertRaisesRegex(ValueError, "ceiling"):
                            ram.report(candidate, workspace=True)
                    else:
                        self.assertEqual(ram.report(candidate, workspace=True)[metric],
                                         ram.WORKSPACE_LIMITS[metric])

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
