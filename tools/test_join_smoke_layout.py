# SPDX-License-Identifier: BSD-3-Clause
import unittest

from join_smoke_layout import apply_data_placement, link_symbols, place, resources


def area(name, size):
    return f"A {name} size {size:X} flags 0 addr 0\n"


class JoinLayoutTests(unittest.TestCase):
    def test_complete_link_symbols_do_not_merge_truncated_map_names(self):
        names = {"_main": 0x120,
                 "l_JF_radio_autoack_radio_autoack_prepare": 1,
                 "l_JF_radio_autoack_radio_autoack_configure": 3,
                 "_mac_tx_interval_submit_staged_PARM_2": 0xe69,
                 "_mac_tx_interval_submit_staged_PARM_3": 0xe6b}
        noice = "".join(f"DEF {name} 0x{value:X}\n" for name, value in names.items())
        noice += "LOAD image.ihx\n"
        map_text = "".join(f"C:   {value:08X} {name[:32]}\n" for name, value in names.items())
        self.assertEqual(link_symbols(noice, map_text, "image.ihx"), names)
        for bad in (noice.replace("0xE69", "0xE68"),
                    noice + "DEF _main 0x121\n", noice.replace("image.ihx", "other.ihx"),
                    noice + "DEF malformed\n"):
            with self.subTest(noice=bad), self.assertRaises(ValueError):
                link_symbols(bad, map_text, "image.ihx")
        with self.assertRaises(ValueError):
            link_symbols(noice, map_text.replace("00000E69", "00000E68"), "image.ihx")

    def test_code_packing_respects_the_short_last_bank(self):
        objects = {f"m{n}": area(f"JS_{n}", 0x8000) for n in range(6)}
        objects["last"] = area("JS_last", 0x6800) + area("JF_last_f", 27)
        plan = place(objects)
        self.assertEqual(plan["remaining_bank_bytes"], [0] * 7)
        self.assertEqual(plan["placements"]["JS_last"], 0x78000)
        self.assertEqual(plan["placements"]["JF_last_f"], 0x2b)
        self.assertFalse(plan["DATA_liveness_verified"])
        self.assertFalse(plan["stack_verified"])
        objects["last"] = area("JS_last", 0x6801)
        with self.assertRaisesRegex(ValueError, "NV partition"):
            place(objects)

    def test_rejects_wide_and_unsplit_spills_or_ambiguous_objects(self):
        for text in (area("JF_m_f", 28), area("JD_m", 1), "",
                     area("JS_m", 1) + area("JS_m", 2)):
            with self.subTest(text=text), self.assertRaises(ValueError):
                place({"m": text})
        self.assertEqual(place({"m": area("JF_m_f", 22)})["placements"]["JF_m_f"], 8)

    def test_solved_placement_stays_bound_to_objects_and_real_reservations(self):
        plan = place({"m": area("JF_m_f", 22)}) | {"objects": {"m": "identity"}}
        candidate = {"objects": {"m": "identity"}, "placements": {"JF_m_f": 0x2b}}
        apply_data_placement(plan, candidate)
        self.assertEqual(plan["placements"]["JF_m_f"], 0x2b)
        self.assertFalse(plan["DATA_liveness_verified"])
        for bad in (candidate | {"objects": {"m": "changed"}},
                    candidate | {"placements": {}},
                    candidate | {"placements": {"JF_m_f": 0x2c, "JS_m": 0}},
                    candidate | {"placements": {"JF_m_f": 0x1e}},
                    candidate | {"placements": {"JF_m_f": 0x31}},
                    candidate | {"placements": {"JF_m_f": True}}):
            with self.subTest(candidate=bad), self.assertRaises(ValueError):
                apply_data_placement(plan, bad)

    def baseline(self):
        return ({0: 2, 0x7e7ff: 0x22},
                {"s_XSEG": 0, "l_XSEG": 7629, "l_XISEG": 0, "l_PSEG": 0,
                 "_join_smoke_status": 0x1e00, "_join_smoke_iram_low": 8,
                 "_join_smoke_iram_high": 0x2b, "s_OSEG": 0x46, "l_OSEG": 10,
                 "s_SSEG": 0x50, "l_SSEG": 45},
                "EXTERNAL RAM  0x0000  0x1dcc  7629  7680\n")

    def test_resource_fit_is_not_acceptance(self):
        result = resources(*self.baseline())
        self.assertEqual(result["xdata_remaining"], 51)
        self.assertEqual(result["last_physical_code"], 0x3e7ff)
        self.assertTrue(result["resource_link_completed"])
        for key in ("accepted_image", "DATA_liveness_verified", "stack_verified",
                    "simulated", "hardware_observed"):
            self.assertFalse(result[key])

    def test_rejects_memory_and_physical_code_boundary_mutations(self):
        image, symbols, memory = self.baseline()
        for name, value in (("s_XSEG", 1), ("l_XSEG", 7681), ("l_XISEG", 1),
                            ("l_PSEG", 1), ("_join_smoke_status", 0x1f00),
                            ("_join_smoke_iram_high", 0x26), ("s_SSEG", 0x80),
                            ("l_SSEG", 46), ("s_OSEG", 0x45), ("l_OSEG", 11)):
            with self.subTest(name=name), self.assertRaises(ValueError):
                resources(image, symbols | {name: value}, memory)
        for bad in ({0: 2, 0x7e800: 0}, {0: 2, 0x10000: 0}, {}):
            with self.subTest(image=bad), self.assertRaises(ValueError):
                resources(bad, symbols, memory)
        for bad in (memory + "*** ERROR", memory.replace("7629", "7628")):
            with self.subTest(memory=bad), self.assertRaises(ValueError):
                resources(image, symbols, bad)


if __name__ == "__main__":
    unittest.main()
