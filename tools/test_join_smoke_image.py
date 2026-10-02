# SPDX-License-Identifier: BSD-3-Clause
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from join_smoke_image import CallerSchema, main, verify, xdata
from verify_banked_join import RUNTIME_XDATA
from xdata_overlay import verify as verify_overlay
from xdata_multipool import regions


class XdataAdmissionTests(unittest.TestCase):
    def test_ownerless_compiler_storage_is_a_retained_region_anchor(self):
        rows = [dict(module="unit", key=key, symbol="_"+key, owner=owner,
                     compiler_class=kind, address=26+i, size=1, shape="SC:U")
                for i, (key, owner, kind) in enumerate((
                    ("a", "unit.a", "LOCAL"), ("scratch", None, "COMPILER_TEMP"),
                    ("b", "unit.b", "LOCAL")))]
        with patch("xdata_multipool.xdata", return_value={"modules": {"unit": (26, 29)}}), \
                patch("xdata_multipool.CallerSchema") as schema, \
                patch("xdata_multipool.platform_contract",
                      return_value={"frozen_modules": {}, "fixed_objects": {}}):
            schema.return_value.header.return_value = ""
            model = regions(({}, {}, "", {}, {"unit": b""}), {"objects": rows})
        self.assertEqual([a["key"] for a in model["anchors"]], ["scratch"])
        self.assertEqual([r["keys"] for r in model["runs"]], [["a"], ["b"]])
        self.assertEqual(model["runs"][0]["fence_after"], "scratch")
        self.assertEqual(model["runs"][1]["fence_before"], "scratch")

    def fixture(self, wrong_clock_order=False):
        modules = ["banked", "mac_link_workspace", "mac_link_child_workspace",
                   "flash_exec", "timebase", "clock", "flash", "flash_write", "nv_record",
                   "security_counter", "aes", "mac_time", "radio_autoack", "mac_epoch",
                   "mac_radio", "mac_attempt", "mac_tx", "mac_adapter", "join_smoke"]
        if wrong_clock_order:
            modules[3:6] = ["timebase", "clock", "flash_exec"]
        symbols = dict(RUNTIME_XDATA)
        symbols.update({"l_" + a: 0 for a in ("XABS", "XISEG", "XINIT", "PSEG")})
        listings, objects, debug, cursor = {}, {}, "", 26
        for module in modules:
            name, size = {"mac_link_workspace": ("link_work_arena", 617),
                          "mac_link_child_workspace": ("child_work_arena", 543)}.get(module, (module, 4))
            listings[module] = (f" 1 .module {module}\n 2 .area XSEG (XDATA)\n"
                                f" {cursor:06X} 3 .ds {size}\n").encode()
            objects[module] = f"A XSEG size {size:X} flags 40 addr 0\n".encode()
            key = "G$" + name + "$0_0$0"
            debug += f"S:{key}({{{size}}}DA{size}d,SC:U),F,0,0\nL:{key}:{cursor:X}\n"
            fence = "_child_work_reserved_end" if module == "mac_link_child_workspace" else f"_{module}_reserved_end"
            symbols[fence] = cursor + size - 1
            if module == "mac_radio":
                symbols["_mac_radio_shared_end"] = cursor
            cursor += size
        symbols["l_XSEG"] = cursor
        return symbols, debug, listings, objects

    def test_complete_runtime_and_private_spans(self):
        result = xdata(*self.fixture())
        self.assertEqual(result["libc_prefix"], 26)
        self.assertEqual(result["declarations"], 19)
        self.assertLess(result["modules"]["flash_exec"][1], result["modules"]["clock"][0])

    def test_real_clock_deadline_storage_is_outside_the_closed_prefix(self):
        with self.assertRaisesRegex(ValueError, "private-prefix order"):
            xdata(*self.fixture(wrong_clock_order=True))

    def test_all_libc_homes_and_every_private_tail_are_checked(self):
        fixture = self.fixture()
        for name in (*RUNTIME_XDATA, *(n for n in fixture[0] if n.endswith("_reserved_end")
                                      and n not in ("_timebase_reserved_end", "_clock_reserved_end",
                                                    "_mac_link_workspace_reserved_end",
                                                    "_mac_epoch_reserved_end", "_mac_tx_reserved_end",
                                                    "_join_smoke_reserved_end"))):
            with self.subTest(name=name), self.assertRaises(ValueError):
                xdata(fixture[0] | {name: fixture[0][name] + 1}, *fixture[1:])

    def test_cdb_home_extent_allocation_holes_and_object_sizes_rejected(self):
        symbols, debug, listings, objects = self.fixture()
        for bad in (debug.replace("({4}DA4d", "({5}DA4d", 1),
                    debug.replace("L:G$banked$0_0$0:1A", "L:G$banked$0_0$0:1B"),
                    debug + "S:Lfoo$home$0_0$0({1}SC:U),F,0,0\nL:Lfoo$home$0_0$0:1A\n"):
            with self.subTest(debug=bad), self.assertRaises(ValueError):
                xdata(symbols, bad, listings, objects)
        with self.assertRaises(ValueError):
            xdata(symbols, debug, listings | {"banked": listings["banked"].replace(b"00001A", b"00001B")}, objects)
        with self.assertRaises(ValueError):
            xdata(symbols, debug, listings, objects | {"banked": objects["banked"].replace(b"size 4", b"size 5")})
        for area in ("XABS", "XISEG", "XINIT", "PSEG"):
            with self.subTest(area=area), self.assertRaises(ValueError):
                xdata(symbols | {"l_" + area: 1}, debug, listings, objects)


class ImmutableAdmissionTests(unittest.TestCase):
    def test_overlay_schema_does_not_fall_back_to_legacy_admission(self):
        for version in (2, 4, True, "3", None):
            with self.subTest(version=version), patch.object(
                    Path, "read_bytes", return_value=json.dumps({"version": version}).encode()), \
                    self.assertRaisesRegex(ValueError, "Unknown XDATA overlay schema"):
                verify_overlay(Path("unused"))
        with patch.object(Path, "read_bytes", return_value=b'{"version":3}'), \
                patch("xdata_multipool.verify", return_value={"routed": True}) as multi:
            self.assertEqual(verify_overlay(Path("unused")), {"routed": True})
            multi.assert_called_once_with(Path("unused"))

    def test_every_artifact_and_wrong_board_fail_before_structure(self):
        pins = json.loads(Path(__file__).with_name("join_smoke_pins.json").read_bytes())
        for name in pins["generic"]:
            with self.subTest(name=name), patch("join_smoke_image.identities",
                    return_value=pins["generic"] | {name: "0" * 64}), \
                    patch("join_smoke_image.structure") as structure:
                with self.assertRaisesRegex(ValueError, "immutable"):
                    verify(Path("unused"), "generic")
                structure.assert_not_called()
        with patch("join_smoke_image.identities", return_value=pins["generic"]), \
                self.assertRaisesRegex(ValueError, "immutable"):
            verify(Path("unused"), "lg_esl29_rev03")

    def test_failed_admission_removes_stale_report_and_native_header(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            report, header = root / "admission.json", root / "join_smoke_layout.h"
            report.write_text('{"simulation_admitted":true}\n')
            header.write_text("stale accepted layout\n")
            with patch("sys.argv", ["image", "--output", str(root), "--board", "generic",
                                    "--header", str(header)]), \
                    patch("join_smoke_image.verify", side_effect=ValueError("changed")):
                with self.assertRaisesRegex(ValueError, "changed"):
                    main()
            self.assertFalse(report.exists())
            self.assertFalse(header.exists())

    def schema(self, shape="SC:U", size=1, offset=0):
        return CallerSchema("M:join_smoke\n"
            f"T:Fjoin_smoke$__1[({{0}}S:S$admission$0_0$0({{{size}}}ST__2:S),Z,0,0)]\n"
            f"T:Fjoin_smoke$__2[({{{offset}}}S:S$value$0_0$0({{{size}}}{shape}),Z,0,0)]\n"
            f"S:G$join_smoke_phase$0_0$0({{{size}}}ST__1:S),F,0,0\n")

    def test_admission_serialization_never_accepts_pointers_padding_or_private_state(self):
        header = self.schema().header()
        self.assertIn("JOIN_ADMISSION_SIZE 1", header)
        self.assertIn("(*p).value", header)
        for args in (("DG,SC:U", 3, 0), ("SC:U", 1, 1), ("SC:U", 3, 0)):
            with self.subTest(args=args), self.assertRaises(ValueError):
                self.schema(*args).header()


if __name__ == "__main__":
    unittest.main()
