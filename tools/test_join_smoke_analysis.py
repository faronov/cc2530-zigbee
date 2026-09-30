# SPDX-License-Identifier: BSD-3-Clause
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from join_smoke_analysis import (
    OVERLAY, PHYSICAL_DATA, RESERVATIONS, function_frames, load, main, physical_data,
    solve_data, symbolic_transfers,
)
from verify_banked_join import data_liveness, transfers


def instruction(pc, raw, asm):
    return f"      {pc:06X} {raw.hex(' ').upper():<12} [24] 1 {asm}\n"


class RuntimeTransferTests(unittest.TestCase):
    def fixture(self):
        image = {6: 0x22, 0x100: 0x12, 0x101: 0, 0x102: 6, 0x103: 0x22, 0x200: 0x22}
        symbols = {"_home": 6, "_tail": 0x200, "__sdcc_banked_call": 0x300,
                   "s_SOURCE": 0x100, "l_SOURCE": 4, "s_CSEG": 0x100, "l_CSEG": 0x101}
        listing = (instruction(0x100, b"\x12\0\x06", "lcall _home")
                   + instruction(0x103, b"\x22", "ret")).encode()
        debug = ("F:G$work$0_0$0({2}DF,SV:S),C,0,0,0,0,0\n"
                 "L:G$work$0$0:100\nL:XG$work$0$0:103\n")
        return image, symbols, {"caller": listing}, debug

    def analyze(self, fixture, **options):
        return transfers(*fixture, modules=("caller",), areas=("SOURCE",),
                         library=("_home", "_tail"), indirect_sites=(), **options)

    def test_home_and_cseg_runtime_entries_are_both_owned(self):
        graph = self.analyze(self.fixture(), runtime_spans=((6, 7), (0x200, 0x201)))
        self.assertEqual(graph[1][6], "libc")
        self.assertEqual(graph[5], {0x100: 6})
        with self.assertRaises(ValueError):
            self.analyze(self.fixture(), runtime_spans=((0x200, 0x201),))

    def test_rejects_runtime_holes_reserved_opcodes_overlap_and_wrong_bank(self):
        for spans in (((6, 8), (0x200, 0x201)), ((6, 7), (6, 7)),
                      ((6, 7), (0x100, 0x201)), ((0x18000, 0x18001),)):
            with self.subTest(spans=spans), self.assertRaises(ValueError):
                self.analyze(self.fixture(), runtime_spans=spans)
        fixture = self.fixture()
        fixture[0][6] = 0xa5
        with self.assertRaisesRegex(ValueError, "reserved"):
            self.analyze(fixture, runtime_spans=((6, 7), (0x200, 0x201)))


class SymbolicTransferTests(unittest.TestCase):
    def test_near_call_must_reach_intended_bank_even_if_another_entry_exists(self):
        listing = instruction(0x18000, b"\x12\x90\0", "lcall _work").encode()
        self.assertEqual(symbolic_transfers({"_work": 0x19000}, {"m": listing},
                                           {0x18000: 0x19000}), 1)
        with self.assertRaisesRegex(ValueError, "Wrong linked transfer"):
            symbolic_transfers({"_work": 0x29000}, {"m": listing}, {0x18000: 0x19000})

    def test_module_local_helpers_do_not_bind_to_another_modules_same_name(self):
        listing = ("      019000 1 _work:\n"
                   + instruction(0x18000, b"\x12\x90\0", "lcall _work")).encode()
        self.assertEqual(symbolic_transfers({"_work": 0x29000}, {"m": listing},
                                           {0x18000: 0x19000}), 1)

    def test_far_call_bytes_and_all_three_symbolic_parts_must_agree(self):
        text = (instruction(0x100, b"\x78\0", "mov r0,#_work")
                + instruction(0x102, b"\x79\x90", "mov r1,#(_work >> 8)")
                + instruction(0x104, b"\x7a\x02", "mov r2,#(_work >> 16)")
                + instruction(0x106, b"\x12\x03\0", "lcall __sdcc_banked_call"))
        self.assertEqual(symbolic_transfers({"_work": 0x29000}, {"m": text.encode()},
                                           {0x106: 0x29000}), 1)
        for bad in (text.replace("r1,#(_work", "r1,#(_other"),
                    text.replace("r0,#_work", "r0,#0"),
                    text.replace(">> 16", ">> 8")):
            with self.subTest(text=bad), self.assertRaises(ValueError):
                symbolic_transfers({"_work": 0x29000}, {"m": bad.encode()}, {0x106: 0x29000})


class FunctionDataTests(unittest.TestCase):
    def fixture(self, relocated=False):
        second = 9 if relocated else 8
        symbols = {"s_JF_m_f": 8, "l_JF_m_f": 1, "s_JF_m_g": second, "l_JF_m_g": 1,
                   "l_BSEG": 0, "__sdcc_banked_ret": 0x300}
        debug, listing = "", ""
        for name, base in (("f", 8), ("g", second)):
            key = f"Lm.{name}$sloc0$0_1$0"
            debug += f"S:{key}({{1}}SC:U),E,0,0\nL:{key}:{base:X}\n"
            listing += (f" 1 .area JF_m_{name} (DATA)\n 000000 1 {key}==.\n"
                        f" {base:06X} 2 _{name}_sloc0_1_0:\n {base:06X} 3 .ds 1\n")
        listing += " 1 .area CSEG (CODE)\n"
        decoded = {0x100: b"\x75\x08\x01", 0x103: b"\x12\x02\0", 0x106: b"\xe5\x08",
                   0x108: b"\x22", 0x200: bytes((0x75, second, 0x69)), 0x203: b"\x22"}
        functions = {pc: (0x100 if pc < 0x200 else 0x200) for pc in decoded}
        for pc, raw in decoded.items():
            asm = {0x100: "mov _f_sloc0_1_0,#1", 0x103: "lcall _g",
                   0x106: "mov a,_f_sloc0_1_0", 0x108: "ret",
                   0x200: "mov _g_sloc0_1_0,#0x69", 0x203: "ret"}[pc]
            listing += instruction(pc, raw, asm)
        graph = (decoded, {pc: "m" for pc in decoded}, functions,
                 {0x100: ("m", "f", False), 0x200: ("m", "g", False)},
                 {0x103: 0x200}, {0x103: 0x200})
        return symbols, debug, {"m": listing.encode()}, graph

    def test_function_owned_frames_solve_and_then_check_the_relocated_calls(self):
        fixture = self.fixture()
        frames, ids, locations, areas = function_frames(*fixture)
        constraints = []
        self.assertEqual(data_liveness(fixture[0], *fixture[3], frames, ids,
                                       RESERVATIONS, OVERLAY, constraints=constraints), (2, 1))
        with self.assertRaisesRegex(ValueError, "Live DATA overwritten"):
            data_liveness(fixture[0], *fixture[3], frames, ids, RESERVATIONS, OVERLAY)
        solution = solve_data(constraints, locations, areas)
        self.assertNotEqual(solution["JF_m_f"], solution["JF_m_g"])
        fixture = self.fixture(relocated=True)
        frames, ids, _, _ = function_frames(*fixture)
        self.assertEqual(data_liveness(fixture[0], *fixture[3], frames, ids,
                                       RESERVATIONS, OVERLAY), (2, 1))

    def test_unowned_address_escape_and_metadata_changes_are_rejected(self):
        symbols, debug, listings, graph = self.fixture()
        for bad in (listings["m"].replace(b"mov a,_f_sloc", b"mov a,_g_sloc"),
                    listings["m"].replace(b"mov a,_f_sloc0_1_0", b"mov r0,#_f_sloc0_1_0"),
                    listings["m"].replace(b".ds 1", b".ds 2", 1),
                    listings["m"].replace(b".ds 1", b".ds 1\n 000009 4 .ds 1", 1)):
            with self.subTest(listing=bad), self.assertRaises(ValueError):
                function_frames(symbols, debug, {"m": bad}, graph)
        for bad in (debug.replace("L:Lm.f$sloc0$0_1$0:8", "L:Lm.f$sloc0$0_1$0:9"),
                    debug.replace("({1}SC:U)", "({2}SC:U)", 1), debug + debug):
            with self.subTest(debug=bad), self.assertRaises(ValueError):
                function_frames(symbols, bad, listings, graph)

    def test_fixed_overlay_and_runtime_clobbers_cannot_be_placed_away(self):
        areas = {"OSEG": (0x46, 10), "JF_m_f": (8, 1)}
        locations = {(1, 0x46): ("OSEG", 0)}
        with self.assertRaisesRegex(ValueError, "fixed-area clobber"):
            solve_data([(0x100, {(1, 0x46)}, {("libc", 0x46)})], locations, areas)
        self.assertEqual(solve_data([(0x100, {(1, 0x46)}, {("libc", 0x47)})],
                                    locations, areas), {"JF_m_f": 8})

    def test_no_solution_does_not_raise_the_physical_limits(self):
        areas = {"JF_m_f": (0x2b, 27), "JF_m_g": (0x2b, 27)}
        locations = {(1, 0x2b): ("JF_m_f", 0), (2, 0x2b): ("JF_m_g", 0)}
        with self.assertRaisesRegex(ValueError, "cannot fit"):
            solve_data([(0x100, {(1, 0x2b)}, {(2, 0x2b)})], locations, areas)

    def test_overlay_and_dedicated_bits_keep_their_own_metadata(self):
        symbols, debug, listings, graph = self.fixture()
        symbols.update(s_OSEG=0x46, l_OSEG=10, l_BSEG=1)
        debug += ("L:Lm.f$sloc1$0_1$0:46\n"
                  "S:Lm.f$sloc2$0_1$0({1}SB0$0:S),H,0,0\n")
        extra = (" 1 .area OSEG (OVR,DATA)\n 000000 1 Lm.f$sloc1$0_1$0==.\n"
                 " 000046 2 _f_sloc1_1_0:\n 000046 3 .ds 1\n"
                 " 1 .area BSEG (BIT)\n 000000 1 Lm.f$sloc2$0_1$0==.\n"
                 " 000000 2 _f_sloc2_1_0:\n 000000 3 .ds 1\n")
        listings["m"] += extra.encode()
        frames, _, locations, _ = function_frames(symbols, debug, listings, graph)
        self.assertEqual(frames[0x100], {8, 0x46})
        self.assertEqual(locations[0x100, 0x46], ("OSEG", 0))
        for bad in (extra.replace("000046", "000050"),
                    extra.replace("000000 3 .ds 1", "000001 3 .ds 1")):
            with self.subTest(listing=bad), self.assertRaises(ValueError):
                function_frames(symbols, debug,
                                {"m": self.fixture()[2]["m"] + bad.encode()}, graph)
        with self.assertRaisesRegex(ValueError, "bit inventories"):
            function_frames(symbols, debug.replace("SB0$0:S", "SC:U"), listings, graph)

    def test_source_indirect_iram_access_cannot_evade_frame_ownership(self):
        symbols, debug, listings, graph = self.fixture()
        listings["m"] += instruction(0x204, b"\xe6", "mov a,@r0").encode()
        with self.assertRaisesRegex(ValueError, "indirect IRAM"):
            function_frames(symbols, debug, listings, graph)


class PhysicalDataTests(unittest.TestCase):
    def fixture(self):
        symbols = {"s_REG_BANK_0": 0, "l_REG_BANK_0": 8, "s_BSEG": 0,
                   "s_BSEG_BYTES": 0x20, "l_BSEG_BYTES": 11, "l_BSEG": 88}
        symbols.update({"l_" + area: 0 for area in
                        ("REG_BANK_1", "REG_BANK_2", "REG_BANK_3", "ISEG", "IABS", "BIT_BANK")})
        debug, listings = "", {}
        for name, (base, size) in PHYSICAL_DATA.items():
            symbols["_" + name] = base
            debug += f"S:G${name}$0_0$0({{{size}}}SC:U),E,0,0\n"
            module = "banked" if name.startswith("banked_") else name
            listings[module] = listings.get(module, b"") + (
                f" 1 .area DSEG (DATA)\n {base:06X} 2 .ds {size}\n").encode()
        graph = ({0x100: b"\x75\x1e\0", 0x200: b"\xe5\x1e"},
                 {0x100: "banked", 0x200: "m"})
        return symbols, debug, listings, graph

    def test_exact_backing_and_read_only_banker_observation(self):
        self.assertIsNone(physical_data(*self.fixture()))
        fixture = self.fixture()
        for name, value in (("_banked_depth", 0x1d), ("l_BSEG", 89), ("l_BSEG_BYTES", 12),
                            ("l_REG_BANK_1", 8), ("l_ISEG", 1)):
            with self.subTest(symbol=name), self.assertRaises(ValueError):
                physical_data(fixture[0] | {name: value}, *fixture[1:])
        with self.assertRaises(ValueError):
            physical_data(fixture[0], fixture[1] + "S:G$extra$0_0$0({1}SC:U),E,0,0\n",
                          *fixture[2:])
        with self.assertRaises(ValueError):
            physical_data(fixture[0], fixture[1],
                          fixture[2] | {"extra": b" 1 .area DSEG (DATA)\n 000008 1 .ds 1\n"}, fixture[3])

    def test_byte_writes_cannot_bypass_bit_and_banker_ownership(self):
        for raw in (b"\xe5\x20", b"\x75\x1f\0", b"\xe5\x50"):
            fixture = self.fixture()
            fixture[3][0][0x200] = raw
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                physical_data(*fixture)


class ArtifactLoadingTests(unittest.TestCase):
    def test_raw_cdb_hash_is_checked_before_decoding_or_symbol_parsing(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            artifacts = {suffix: hashlib.sha256(b"old").hexdigest()
                         for suffix in (".ihx", ".map", ".mem", ".cdb", ".noi")}
            (root / "layout.json").write_text(json.dumps(
                {"resource_link_completed": True, "accepted_image": False, "artifacts": artifacts}))
            for suffix in artifacts:
                (root / ("join_smoke_unverified" + suffix)).write_bytes(b"old")
            (root / "join_smoke_unverified.cdb").write_bytes(b"old\r\n")
            with patch("join_smoke_analysis.link_symbols") as symbols:
                with self.assertRaisesRegex(ValueError, "Changed linked artifact: .cdb"):
                    load(root)
                symbols.assert_not_called()

    def test_failed_analysis_removes_previous_success_report(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            report = root / "analysis.json"
            report.write_text('{"DATA_liveness_verified": true}\n')
            with patch("sys.argv", ["analysis", "--output", str(root), "--check-data"]), \
                    patch("join_smoke_analysis.load", side_effect=ValueError("changed artifact")):
                with self.assertRaisesRegex(ValueError, "changed artifact"):
                    main()
            self.assertFalse(report.exists())

    def test_stack_cannot_bypass_data_or_retain_a_stale_success(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            report = root / "analysis.json"
            report.write_text('{"static_stack_verified": true}\n')
            with patch("sys.argv", ["analysis", "--output", str(root), "--check-stack"]), \
                    self.assertRaisesRegex(ValueError, "requires the strict linked DATA"):
                main()
            self.assertFalse(report.exists())


if __name__ == "__main__":
    unittest.main()
