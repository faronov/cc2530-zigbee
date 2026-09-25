# SPDX-License-Identifier: BSD-3-Clause
"""Interval runner rejection controls; no compiler, simulator or hardware."""
import sys
import unittest

from verify_firmware import ROOT
sys.path.insert(0, str(ROOT / "tests"))
import boot_mac_tx_interval as proof


class IntervalObservationTests(unittest.TestCase):
    def setUp(self):
        self.symbols = {"_mac_tx_interval_done": 0x1234, "_mac_tx_interval_case": 17, "s_SSEG": 0x4f}
        self.allocated = set(range(1285)) | set(range(0x1e00, 0x1e08))
        self.ram = bytearray([0xa5] * 0x1f00)
        self.ram[17] = 7
        self.ram[0x1e00:0x1e08] = b"MTI1\x01\x08\0\0"
        self.iram = bytearray(0x7d) + bytearray([0xc7] * 131)
        self.sfr = bytearray(128)
        self.sfr[1] = 0x4e
        self.pc, self.peak = 0x1234, 0x67

    def text(self):
        def dump(raw, start):
            return "".join(f"0x{start+i:06x} " + raw[i:i+16].hex(" ") + "\n"
                           for i in range(0, len(raw), 16))
        return ("0x25300001\n"
                f"CPU state= OK PC= 0x{self.pc:x}\nMax value of stack pointer= 0x{self.peak:x}\n"
                + dump(self.ram, 0) + "0x25300002\n"
                + dump(self.iram, 0) + "0x25300003\n"
                + dump(self.sfr, 0x80) + "0x25300004\n")

    def check(self, text=None):
        proof.check_execution(self.text() if text is None else text, self.symbols, self.allocated, 7)

    def test_complete_observation_and_every_result_byte(self):
        self.check()
        for at in range(0x1e00, 0x1e08):
            with self.subTest(at=at):
                self.ram[at] ^= 1
                with self.assertRaises(ValueError):
                    self.check()
                self.ram[at] ^= 1

    def test_unowned_writes_and_every_upper_iram_byte(self):
        for raw, positions in ((self.ram, (17, 1285, 0x1dff, 0x1e08)),
                               (self.iram, range(0x7d, 0x100)),
                               (self.sfr, (1, 0x12, 0x28, 0x38, 0x1a))):
            for at in positions:
                with self.subTest(at=at):
                    raw[at] ^= 1
                    with self.assertRaises(ValueError):
                        self.check()
                    raw[at] ^= 1

    def test_missing_dump_pc_and_peak(self):
        for first, last in (("0x000000 ", "omitted "), ("CPU state=", "missing state="),
                            ("Max value of stack pointer=", "missing peak=")):
            with self.subTest(first=first), self.assertRaises(ValueError):
                self.check(self.text().replace(first, last))
        self.pc += 1
        with self.assertRaises(ValueError):
            self.check()
        self.pc -= 1
        self.peak = 0x7d
        with self.assertRaises(ValueError):
            self.check()


if __name__ == "__main__":
    unittest.main()
