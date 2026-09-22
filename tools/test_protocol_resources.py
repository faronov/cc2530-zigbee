# SPDX-License-Identifier: BSD-3-Clause
"""Synthetic object/link resource accounting, not hardware evidence."""

import copy
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from protocol_resources import (
    MODULE_BUDGETS, ZCL_BASELINE_CODE, build_report, check_zcl_headroom, object_resources,
)


def obj(name, code=100, xdata=10, dseg=2, overlay=3, bits=0):
    module = "test_protocol_budget" if name == "protocol_budget_test" else name
    return (
        f"XH3\nM {module}\nO -mmcs51 --model-large\n"
        f"A REG_BANK_0 size 8 flags 4 addr 0\n"
        f"A DSEG size {dseg:X} flags 0 addr 0\n"
        f"A OSEG size {overlay:X} flags 4 addr 0\n"
        f"A XSEG size {xdata:X} flags 40 addr 0\n"
        f"A BSEG size {bits:X} flags 80 addr 0\n"
        f"A CSEG size {code:X} flags 20 addr 0\n"
        f"A SSEG size {int(name == 'protocol_budget_test'):X} flags 0 addr 0\n"
    )


class ObjectTests(unittest.TestCase):
    def test_resources(self):
        self.assertEqual(object_resources(obj("mac_frame"), "mac_frame"), {
            "code": 100, "xdata": 10, "iram_persistent": 2, "iram_overlay": 3, "bits": 0,
        })

    def test_rejects_incompatible_or_unaccounted_objects(self):
        good = obj("mac_frame")
        bad = (
            good.replace("XH3", "XL3"), good.replace("M mac_frame", "M foreign"),
            good.replace("--model-large", "--model-small"),
            good.replace("A DSEG size 2 flags 0 addr 0\n", ""),
            good + "A DSEG size 1 flags 0 addr 0\n",
            good + "A XISEG size 1 flags 40 addr 0\n",
            good + "A PSEG size 1 flags 50 addr 0\n",
            good + "A RSEG0 size 1 flags 8 addr 0\n",
            good.replace("A CSEG size 64 flags 20", "A CSEG size 64 flags 0"),
            good.replace("size 64", "size bad!"),
            good.replace("addr 0", "addr 1", 1),
            good.replace("REG_BANK_0 size 8", "REG_BANK_0 size 10"),
            good.replace("SSEG size 0", "SSEG size 1"),
        )
        for text in bad:
            with self.subTest(text=text), self.assertRaises(ValueError):
                object_resources(text, "mac_frame")


class ReportTests(unittest.TestCase):
    def setUp(self):
        self.objects = {name: obj(name, dseg=0 if name in ("protocol_budget_test", "zcl_write") else 2)
                        for name in MODULE_BUDGETS}
        self.image = dict.fromkeys(range(1000), 0)
        self.symbols = {"s_XSEG": 0, "l_XSEG": 100, "s_XISEG": 0, "l_XISEG": 0,
                        "s_PSEG": 0, "l_PSEG": 0, "s_SSEG": 25, "l_OSEG": 3, "l_BSEG": 0}

    def report(self):
        return build_report(self.image, self.symbols, self.objects, 32)

    def test_shared_and_nonadditive_accounting(self):
        result = self.report()
        self.assertEqual(result["shared_runtime"], {"code": 100, "xdata": 10})
        self.assertEqual(result["linked"]["iram_overlay_shared"], 3)
        self.assertEqual(result["linked"]["iram_persistent"], 14)
        self.assertEqual(result["linked"]["observed_stack_bytes"], 8)
        self.assertEqual(result["linked"]["observed_headroom_to_guard"], 95)
        self.assertFalse(result["hardware_tested"])
        self.assertIn("not a full Zigbee stack", result["scope"])

    def test_module_set_and_bounds(self):
        original = self.objects
        for change in ("missing", "extra", "code", "xdata", "iram", "overlay"):
            self.objects = copy.copy(original)
            if change == "missing":
                self.objects.pop("mac_frame")
            elif change == "extra":
                self.objects["foreign"] = obj("foreign")
            else:
                budget = MODULE_BUDGETS["mac_frame"]
                values = {"code": 100, "xdata": 10, "dseg": 2, "overlay": 3}
                values[{"iram": "dseg"}.get(change, change)] = {
                    "code": budget[0] + 1, "xdata": budget[1] + 1,
                    "iram": budget[2] + 1, "overlay": 17,
                }[change]
                self.objects["mac_frame"] = obj("mac_frame", **values)
            with self.subTest(change=change), self.assertRaises(ValueError):
                self.report()

    def test_each_module_at_exact_limits(self):
        for name, limits in MODULE_BUDGETS.items():
            objects = dict(self.objects, **{name: obj(
                name, code=limits[0], xdata=limits[1], dseg=limits[2])})
            resources = [object_resources(text, module) for module, text in objects.items()]
            symbols = dict(self.symbols, l_XSEG=sum(row["xdata"] for row in resources),
                           s_SSEG=11 + sum(row["iram_persistent"] for row in resources))
            image = dict.fromkeys(range(sum(row["code"] for row in resources)), 0)
            with self.subTest(module=name):
                report = build_report(image, symbols, objects, 127)
                row = report["modules"][name]
                self.assertEqual((row["code"], row["xdata"], row["iram_persistent"]), limits)

    def test_link_disagreements_and_limits(self):
        for field, value in (("l_OSEG", 2), ("l_BSEG", 1), ("l_XSEG", 79),
                             ("l_XSEG", 2048), ("l_XSEG", 219), ("s_SSEG", 0x69),
                             ("s_SSEG", 34), ("s_SSEG", 24)):
            symbols = dict(self.symbols, **{field: value})
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                build_report(self.image, symbols, self.objects, 40)
        for peak in (24, 128):
            with self.subTest(peak=peak), self.assertRaises(ValueError):
                build_report(self.image, self.symbols, self.objects, peak)
        for size in (899, 1925, 24577, 32769):
            with self.subTest(size=size), self.assertRaises(ValueError):
                build_report(dict.fromkeys(range(size), 0), self.symbols, self.objects, 32)
        self.image.pop(42)
        with self.assertRaisesRegex(ValueError, "contiguous"):
            self.report()

    def test_exact_total_budget_edges(self):
        remaining = 24576
        remaining_xdata = 2048 - 64
        for name, budgets in MODULE_BUDGETS.items():
            size = min(remaining, budgets[0])
            xdata = min(remaining_xdata, budgets[1])
            self.objects[name] = obj(name, code=size, xdata=xdata,
                                     dseg=0 if name in ("protocol_budget_test", "zcl_write") else 2)
            remaining -= size
            remaining_xdata -= xdata
        self.assertEqual(remaining, 0)
        self.symbols["l_XSEG"] = 2048 - 64
        report = build_report(dict.fromkeys(range(24576), 0), self.symbols, self.objects, 127)
        self.assertEqual(report["linked"]["code"], 24576)
        self.assertEqual(report["linked"]["xdata_with_status"], 2048)
        self.assertEqual(report["linked"]["observed_headroom_to_guard"], 0)
        with self.assertRaisesRegex(ValueError, "CODE budget"):
            build_report(dict.fromkeys(range(24577), 0), self.symbols, self.objects, 127)
        self.symbols["l_XSEG"] += 1
        with self.assertRaisesRegex(ValueError, "XDATA budget"):
            build_report(dict.fromkeys(range(24576), 0), self.symbols, self.objects, 127)

    def test_failed_checker_removes_old_report(self):
        script = Path(__file__).resolve().parents[1] / "tests" / "boot_protocol_budget.py"
        with tempfile.TemporaryDirectory() as directory:
            report = Path(directory) / "protocol-resources.json"
            report.write_text('{"stale": true}\n', encoding="ascii")
            result = subprocess.run([sys.executable, "-B", str(script), "--output", directory],
                                    capture_output=True, text=True, timeout=15)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("FileNotFoundError", result.stderr)
            self.assertFalse(report.exists())

    def test_exact_stack_start_budget(self):
        for name, limits in MODULE_BUDGETS.items():
            self.objects[name] = obj(name, dseg=limits[2] - (4 if name == "mac_frame" else 0),
                                     overlay=16)
        self.symbols.update(s_SSEG=0x68, l_OSEG=16)
        self.assertEqual(build_report(self.image, self.symbols, self.objects, 127)
                         ["linked"]["stack_start"], 0x68)
        self.symbols["s_SSEG"] += 1
        with self.assertRaisesRegex(ValueError, "stack-start budget"):
            build_report(self.image, self.symbols, self.objects, 127)


class HeadroomTests(unittest.TestCase):
    def setUp(self):
        self.report = {
            "linked": {"code": 23551},
            "modules": {name: {"code": code} for name, code in ZCL_BASELINE_CODE.items()},
        }
        self.report["modules"]["zcl_value"]["code"] -= 512

    def test_exact_both_thresholds(self):
        result = check_zcl_headroom(self.report)
        self.assertEqual(result["code_saved"], 1024)
        self.assertEqual(result["zcl_code_saved"], 512)
        self.assertEqual(result["zcl_code"], 9102)
        self.assertEqual(result["baseline_zcl_code"], 9614)

    def test_integrated_one_byte_short(self):
        self.report["linked"]["code"] += 1
        with self.assertRaisesRegex(ValueError, "Integrated CODE headroom"):
            check_zcl_headroom(self.report)

    def test_caller_or_runtime_savings_cannot_replace_production(self):
        self.report["linked"]["code"] = 22000
        self.report["modules"]["zcl_value"]["code"] += 1
        with self.assertRaisesRegex(ValueError, "Production ZCL CODE"):
            check_zcl_headroom(self.report)


if __name__ == "__main__":
    unittest.main()
