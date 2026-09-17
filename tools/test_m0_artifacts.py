# SPDX-License-Identifier: BSD-3-Clause
"""Synthetic regression fixtures; no device dumps or external code."""

import unittest

from verify_firmware import IMAGES, parse_ihex, parse_symbols, verify_layout


def record(address=0, kind=0, data=b"\x02\x00\x06"):
    body = bytes([len(data), address >> 8, address & 255, kind]) + data
    return ":" + (body + bytes([-sum(body) & 255])).hex()


EOF = record(kind=1, data=b"")


class HexTests(unittest.TestCase):
    def test_data(self):
        self.assertEqual(parse_ihex(record() + "\n" + EOF), {0: 2, 1: 0, 2: 6})

    def test_extended_addresses(self):
        for kind, expected in ((2, 0x20), (4, 0x20000)):
            with self.subTest(kind=kind):
                image = parse_ihex(record(kind=kind, data=b"\x00\x02") + "\n" + record() + "\n" + EOF)
                self.assertEqual(min(image), expected)

    def test_bad_checksum(self):
        with self.assertRaisesRegex(ValueError, "checksum"):
            parse_ihex(record()[:-2] + "00\n" + EOF)

    def test_bad_length(self):
        with self.assertRaisesRegex(ValueError, "length"):
            parse_ihex(":04000000020006f5\n" + EOF)

    def test_overlap(self):
        with self.assertRaisesRegex(ValueError, "overlapping"):
            parse_ihex(record() + "\n" + record() + "\n" + EOF)

    def test_after_eof(self):
        with self.assertRaisesRegex(ValueError, "after EOF"):
            parse_ihex(record() + "\n" + EOF + "\n" + record())

    def test_missing_eof(self):
        with self.assertRaisesRegex(ValueError, "EOF"):
            parse_ihex(record())

    def test_window_overflow(self):
        with self.assertRaisesRegex(ValueError, "window"):
            parse_ihex(record(address=0xFFFF) + "\n" + EOF)

    def test_invalid_records(self):
        for text in ("hello", ":", ":zz", record(kind=3), record(kind=4, data=b"x"), EOF):
            with self.subTest(text=text), self.assertRaises(ValueError):
                parse_ihex(text)


class LayoutTests(unittest.TestCase):
    def setUp(self):
        self.symbols = {
            "_m0_status": 0x1E00, "__XPAGE": 0x93, "l_XABS": 0,
            "s_SSEG": 8, "l_SSEG": 248, "__start__stack": 8,
        }
        for name in ("XSEG", "XISEG", "PSEG"):
            self.symbols["s_" + name] = 0
            self.symbols["l_" + name] = 0
        self.memory = "Stack starts at: 0x08 (sp set to 0x07) with 248 bytes available."
        self.debug = "S:G$m0_status$0_0$0({32}STtest:S),F,0,0\nL:C$bringup.c$6$0_0$0:123"

    def verify(self):
        return verify_layout(self.symbols, self.memory, self.debug)

    def test_absolute_status_is_counted(self):
        self.assertEqual(self.verify()["nonaliased_xdata_used_bytes"], 32)
        self.assertEqual(self.verify()["nonaliased_xdata_reserved_bytes"], 64)

    def test_isolated_irq_cannot_enter_board_images(self):
        for name in ("_irq_save_disable", "_irq_restore"):
            with self.assertRaisesRegex(ValueError, "isolated IRQ"):
                verify_layout(dict(self.symbols, **{name: 0x100}), self.memory, self.debug)
        with self.assertRaisesRegex(ValueError, "isolated IRQ"):
            verify_layout(self.symbols, self.memory, self.debug + "\nC$irq.c$1")

    def test_isolated_radio_fifo_cannot_enter_board_images(self):
        for image in IMAGES:
            if image == "radio_fifo_fixture":
                continue
            for name in ("_radio_fifo_clear_init", "_radio_fifo_preload_init"):
                with self.subTest(image=image, name=name), self.assertRaisesRegex(ValueError, "isolated radio FIFO"):
                    verify_layout(dict(self.symbols, **{name: 0x100}), self.memory, self.debug, image)
            with self.assertRaisesRegex(ValueError, "isolated radio FIFO"):
                verify_layout(self.symbols, self.memory, self.debug + "\nC$radio_fifo.c$1", image)

    def test_dma_cannot_enter_the_twelve_original_board_images(self):
        self.assertEqual(len(IMAGES), 7)
        for image in IMAGES:
            if image == "dma_fixture":
                continue
            for name in ("_dma_copy_init", "_dma_descriptor", "_dma_fault", "_dma_reserved_end"):
                with self.subTest(image=image, name=name), self.assertRaisesRegex(ValueError, "isolated DMA"):
                    verify_layout(dict(self.symbols, **{name: 0x100}), self.memory, self.debug, image)
            with self.assertRaisesRegex(ValueError, "isolated DMA"):
                verify_layout(self.symbols, self.memory, self.debug + "\nC$dma.c$1", image)

    def test_status_cannot_alias_iram(self):
        self.symbols["_m0_status"] = 0x1F00
        with self.assertRaisesRegex(ValueError, "alias"):
            self.verify()

    def test_allocator_cannot_reach_status(self):
        self.symbols["s_XSEG"], self.symbols["l_XSEG"] = 0x1DFF, 2
        with self.assertRaisesRegex(ValueError, "overlaps"):
            self.verify()

    def test_budget_exact_boundary(self):
        self.symbols["l_XSEG"] = 448
        self.assertEqual(self.verify()["nonaliased_xdata_reserved_bytes"], 512)
        self.symbols["l_XSEG"] = 449
        with self.assertRaisesRegex(ValueError, "budget"):
            self.verify()

    def test_allocator_overlap(self):
        self.symbols["l_XSEG"], self.symbols["l_XISEG"] = 10, 2
        with self.assertRaisesRegex(ValueError, "Overlapping"):
            self.verify()

    def test_wrong_page_register(self):
        self.symbols["__XPAGE"] = 0xA0
        with self.assertRaisesRegex(ValueError, "MPAGE"):
            self.verify()

    def test_wrong_debug_size(self):
        self.debug = self.debug.replace("{32}", "{64}")
        with self.assertRaisesRegex(ValueError, "ABI"):
            self.verify()

    def test_missing_stack(self):
        self.memory = ""
        with self.assertRaisesRegex(ValueError, "stack"):
            self.verify()

    def test_bad_stack_accounting(self):
        self.symbols["l_SSEG"] = 247
        with self.assertRaisesRegex(ValueError, "stack"):
            self.verify()

    def test_new_absolute_xdata(self):
        self.symbols["l_XABS"] = 1
        with self.assertRaisesRegex(ValueError, "absolute"):
            self.verify()

    def test_unaccounted_global(self):
        self.symbols["_extra"] = 0x1F40
        self.debug += "\nS:G$extra$0_0$0({1}SC:U),F,0,0"
        with self.assertRaisesRegex(ValueError, "absolute"):
            self.verify()

    def test_symbols(self):
        text = ("C: 00000100 _main bringup\n     00001E00 _m0_status status\n"
                "C: 00000000 l_XSEG\nD: 00000010 _ordinary_xdata fixture\n")
        self.assertEqual(parse_symbols(text)["_m0_status"], 0x1E00)
        self.assertEqual(parse_symbols(text)["_ordinary_xdata"], 0x10)
        with self.assertRaisesRegex(ValueError, "Conflicting"):
            parse_symbols(text + "C: 00000101 _main bringup\n")

    def fixture_layout(self, address=0, size=16):
        self.symbols["_debug_fixture_state"] = address
        self.symbols["l_XSEG"] = 20
        self.debug += f"\nS:G$debug_fixture_state$0_0$0({{{size}}}STfixture:S),F,0,0"
        self.debug += "\nL:C$debug_fixture.c$6$0_0$0:123"
        return verify_layout(self.symbols, self.memory, self.debug, "debug_fixture")

    def test_fixture_xdata_accounted_without_expanding_status(self):
        metrics = self.fixture_layout()
        self.assertEqual(metrics["ordinary_xdata_bytes"], 20)
        self.assertEqual(metrics["nonaliased_xdata_used_bytes"], 52)
        self.assertEqual(metrics["nonaliased_xdata_reserved_bytes"], 84)

    def test_fixture_must_fit_allocator(self):
        for address in (5, 0x1E20, 0x1F00):
            with self.subTest(address=address), self.assertRaisesRegex(ValueError, "absolute"):
                self.fixture_layout(address=address)

    def test_fixture_wrong_abi_size(self):
        with self.assertRaisesRegex(ValueError, "Fixture debug ABI"):
            self.fixture_layout(size=15)

    def test_fixture_missing_abi(self):
        self.debug += "\nL:C$debug_fixture.c$6$0_0$0:123"
        with self.assertRaisesRegex(ValueError, "Fixture debug ABI"):
            verify_layout(self.symbols, self.memory, self.debug, "debug_fixture")

    def test_wrong_image_source(self):
        with self.assertRaisesRegex(ValueError, "source-level"):
            verify_layout(self.symbols, self.memory, self.debug, "debug_fixture")

    def test_unknown_image(self):
        with self.assertRaisesRegex(ValueError, "Unknown firmware"):
            verify_layout(self.symbols, self.memory, self.debug, "unknown")


if __name__ == "__main__":
    unittest.main()
