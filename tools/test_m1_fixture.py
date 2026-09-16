# SPDX-License-Identifier: BSD-3-Clause
"""Original synthetic instruction fixtures, not target dumps."""

import unittest

from verify_firmware import PROBE_BYTES, verify_fixture_code


class ProbeTests(unittest.TestCase):
    def setUp(self):
        self.symbols = {
            "_debug_fixture_probe": 0x100,
            "_debug_fixture_stop": 0x10B,
            "_debug_fixture_initialize": 0x200,
            "_debug_fixture_cycle": 0x210,
        }
        self.symbols.update({f"_debug_fixture_stage{i}": 0x300 + 0x10 * i for i in range(4)})
        self.image = {address: 0x22 for address in self.symbols.values()}
        self.image.update({0x100 + offset: value for offset, value in enumerate(PROBE_BYTES)})

    def test_exact_probe(self):
        verify_fixture_code(self.image, self.symbols)

    def test_each_opcode_and_operand_is_checked(self):
        for offset in range(len(PROBE_BYTES)):
            image = dict(self.image)
            image[0x100 + offset] ^= 1
            with self.subTest(offset=offset), self.assertRaisesRegex(ValueError, "opcodes"):
                verify_fixture_code(image, self.symbols)

    def test_missing_probe_byte(self):
        del self.image[0x101]
        with self.assertRaisesRegex(ValueError, "opcodes"):
            verify_fixture_code(self.image, self.symbols)

    def test_missing_symbol(self):
        for name in self.symbols:
            symbols = {key: value for key, value in self.symbols.items() if key != name}
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, "symbol"):
                verify_fixture_code(self.image, symbols)

    def test_symbol_outside_image(self):
        for name in self.symbols:
            symbols = dict(self.symbols, **{name: 0x10000})
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, "symbol"):
                verify_fixture_code(self.image, symbols)

    def test_duplicate_breakpoint_location(self):
        self.symbols["_debug_fixture_stage1"] = self.symbols["_debug_fixture_stage0"]
        with self.assertRaisesRegex(ValueError, "overlap"):
            verify_fixture_code(self.image, self.symbols)

    def test_stop_must_name_nop_not_ret(self):
        self.symbols["_debug_fixture_stop"] += 1
        with self.assertRaisesRegex(ValueError, "NOP"):
            verify_fixture_code(self.image, self.symbols)


if __name__ == "__main__":
    unittest.main()
