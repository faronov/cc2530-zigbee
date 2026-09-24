# SPDX-License-Identifier: BSD-3-Clause
"""Pure parser/ownership/continuation negatives; no tools or hardware invoked."""
import sys
import unittest

from verify_firmware import ROOT
sys.path.insert(0, str(ROOT / "tests"))

from boot_banked_security import restore
from boot_banked import normalized_object, pin_artifacts, sha
from boot_nwk_candidates import records
from boot_security_resident import active_frames
from verify_banked_security import EDGES, FRAMES


class BankedSecurityTests(unittest.TestCase):
    def test_data_only_object_keeps_its_nonoptional_format_header(self):
        raw = b"XH3\nH 1A areas 3 global symbols\n"
        self.assertEqual(normalized_object(raw), raw)
        self.assertEqual(normalized_object(b";!FILE arbitrary/path.asm\n"+raw), raw)
        for changed in (b"XL3"+raw[3:], raw[1:], b"junk\n"+raw, raw.replace(b"\n", b"\r\n"),
                        b";!FILE no-newline"):
            with self.assertRaises(ValueError):
                normalized_object(changed)

    def test_only_hash_approved_immutable_artifacts_can_seed_exact_comparison(self):
        raw = tuple((str(i)+"\n").encode("ascii") for i in range(6))
        pins = tuple(map(sha, raw))
        changed = raw[:2]+(b"bad",)+raw[3:]
        with self.assertRaises(ValueError):
            pin_artifacts(changed, pins)
        pin_artifacts(raw, pins)
        for index in range(6):
            for replacement in (raw[index]+b"x", raw[index].replace(b"\n", b"\r\n"), bytearray(raw[index])):
                with self.assertRaises(ValueError):
                    pin_artifacts(raw[:index]+(replacement,)+raw[index+1:], pins)
        pin_artifacts(tuple(bytes(bytearray(value)) for value in raw), pins)

    def test_five_digit_listing_lines_have_no_gap_after_cycle_bracket(self):
        text = ("      01B00A 02 B2 B4         [24] 9905 ljmp 00148$\n"
                "      01B2B4 90 0D 87         [24]10364 mov dptr,#0x0d87\n"
                "      01B2B7 E0               [24]10365 movx a,@dptr\n"
                "      01B2B8 FF               [12]10366 mov r7,a\n")
        self.assertEqual(records(text), [(0x1b00a, b"\x02\xb2\xb4"), (0x1b2b4, b"\x90\x0d\x87"),
                                         (0x1b2b7, b"\xe0"), (0x1b2b8, b"\xff")])

    def test_reused_data_rejects_new_active_edges_and_recursion(self):
        frames = {m: (a, n) for m, (_, a, n) in FRAMES.items()}
        active_frames(EDGES, frames)
        for edge in (("ed_wire", "security_counter"), ("nv_record", "aes"),
                     ("aes", "security_keys"), ("security_counter", "security_counter")):
            with self.subTest(edge=edge), self.assertRaises(ValueError):
                active_frames(EDGES | {edge}, frames)

    def test_continuations_reject_missing_state_live_timer_uart_and_xmap(self):
        sfr = bytearray(128); sfr[0x47] = 2
        state = (bytes(0x1f00), bytes(256), bytes(sfr), bytes(0x6000), bytes(0x40000))
        self.assertTrue(restore(state, 0x1234)[-1].endswith("0x1234"))
        for index, value in ((0x19, 1), (0x18, 2), (8, 0x10), (8, 0x40), (0x48, 4), (0x47, 10),
                             (0x28, 0x80), (0x38, 1), (0x1a, 1), (0x51, 1), (0x56, 1), (0x57, 1)):
            changed = bytearray(sfr); changed[index] = value
            with self.assertRaises(ValueError):
                restore((state[0], state[1], bytes(changed), state[3], state[4]), 0x1234)
        for index in range(5):
            changed = list(state); changed[index] = changed[index][1:]
            with self.assertRaises(ValueError):
                restore(changed, 0x1234)
        for pc in (-1, 0x8000, 0x18000, True):
            with self.assertRaises(ValueError):
                restore(state, pc)


if __name__ == "__main__":
    unittest.main()
