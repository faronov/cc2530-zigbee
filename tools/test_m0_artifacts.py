# SPDX-License-Identifier: BSD-3-Clause
"""Synthetic regression fixtures; no device dumps or external code."""

import unittest

from verify_firmware import CODE_LIMIT, IMAGES, code_bytes, parse_ihex, parse_symbols, verify_layout


def record(address=0, kind=0, data=b"\x02\x00\x06"):
    body = bytes([len(data), address >> 8, address & 255, kind]) + data
    return ":" + (body + bytes([-sum(body) & 255])).hex()


EOF = record(kind=1, data=b"")


class HexTests(unittest.TestCase):
    def test_data(self):
        self.assertEqual(parse_ihex(record() + "\n" + EOF), {0: 2, 1: 0, 2: 6})

    def test_record_order_does_not_change_image(self):
        image = parse_ihex(record(address=3, data=b"\xaa\xbb") + "\n" + record() + "\n" + EOF)
        self.assertEqual(list(image), list(range(5)))
        self.assertEqual(code_bytes(image, 5), b"\x02\0\x06\xaa\xbb")

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


class CodeBytesTests(unittest.TestCase):
    def test_order_and_every_mutation_are_observed_without_caching_input(self):
        for size in (1, 257, CODE_LIMIT):
            expected = bytes(i & 255 for i in range(size))
            image = dict(enumerate(expected))
            self.assertEqual(code_bytes(image, size), expected)
            self.assertEqual(code_bytes(dict(reversed(list(image.items()))), size), expected)
        image = dict(enumerate(bytes(range(256))))
        for address in image:
            image[address] ^= 1
            actual = code_bytes(image, 256)
            self.assertEqual(actual, bytes(i ^ int(i == address) for i in range(256)))
            image[address] ^= 1
        self.assertEqual(code_bytes(image, 256), bytes(range(256)))

    def test_missing_extra_shifted_and_invalid_data_fail(self):
        for image, size in (({}, 1), ({0: 1}, 2), ({0: 1, 1: 2}, 1),
                            ({0: 1, 2: 2}, 2), ({-1: 1, 0: 2}, 2), ({1: 1}, 1),
                            ({0: 256}, 1), ({0: -1}, 1)):
            with self.subTest(image=image, size=size), self.assertRaises(ValueError):
                code_bytes(image, size)
        for size in (0, -1, CODE_LIMIT+1, True, 1.0):
            with self.subTest(size=size), self.assertRaises(ValueError):
                code_bytes({0: 1}, size)


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

    def test_isolated_flash_services_cannot_enter_board_images(self):
        for image in IMAGES:
            if image == "flash_fixture":
                continue
            for name in ("_flash_nv_read", "_flash_fault", "_flash_reserved_end", "_flash_test_result",
                         "_flash_exec_command", "_flash_exec_ram", "_flash_exec_test_result",
                         "_flash_nv_erase", "_flash_nv_program", "_flash_write_status"):
                with self.subTest(image=image, name=name), self.assertRaisesRegex(ValueError, "isolated flash"):
                    verify_layout(self.symbols | {name: 0x100}, self.memory, self.debug, image)
            for source in ("flash.c", "test_flash.c", "flash_exec.c", "test_flash_exec.c",
                           "flash_write.c", "test_flash_write.c", "host_flash_engine.c"):
                with self.subTest(image=image, source=source), self.assertRaisesRegex(ValueError, "isolated flash"):
                    verify_layout(self.symbols, self.memory, self.debug+f"\nC${source}$1", image)

    def test_isolated_radio_autoack_cannot_enter_board_images(self):
        for image in IMAGES:
            for name in ("_radio_autoack_acquire", "_radio_autoack_receive",
                         "_radio_autoack_stop", "_radio_autoack_diagnostic", "_radio_autoack_state",
                         "_radio_autoack_reserved_end", "_radio_autoack_test_result"):
                with self.subTest(image=image, name=name), self.assertRaisesRegex(ValueError, "receiver AUTOACK"):
                    verify_layout(self.symbols | {name: 0x100}, self.memory, self.debug, image)
            for source in ("radio_autoack.c", "test_radio_autoack.c", "host_radio_autoack.c"):
                with self.subTest(image=image, source=source), self.assertRaisesRegex(ValueError, "receiver AUTOACK"):
                    verify_layout(self.symbols, self.memory, self.debug+f"\nC${source}$1", image)

    def test_isolated_noise_services_cannot_enter_board_images(self):
        for image in IMAGES:
            for name in ("_radio_noise_collect", "_radio_noise_fault", "_radio_noise_used",
                         "_radio_noise_reserved_end", "_radio_noise_test_result",
                         "_noise_health_start", "_noise_health_push", "_noise_health_test_result"):
                if image == "radio_noise_fixture" and "_test_" not in name:
                    continue
                with self.subTest(image=image, name=name), self.assertRaisesRegex(ValueError, "raw-noise"):
                    verify_layout(self.symbols | {name: 0x100}, self.memory, self.debug, image)
            for source in ("radio_noise.c", "test_radio_noise.c", "noise_health.c", "test_noise_health.c"):
                if image == "radio_noise_fixture" and not source.startswith("test_"):
                    continue
                with self.subTest(image=image, source=source), self.assertRaisesRegex(ValueError, "raw-noise"):
                    verify_layout(self.symbols, self.memory, self.debug+f"\nC${source}$1", image)

    def test_isolated_mac_scan_cannot_enter_board_images(self):
        for image in IMAGES:
            for name in ("_mac_scan_init", "_mac_scan_start", "_mac_scan_step",
                         "_mac_scan_get", "_mac_scan_release", "_mac_scan_result"):
                with self.subTest(image=image, name=name), self.assertRaisesRegex(ValueError, "MAC scan"):
                    verify_layout(self.symbols | {name: 0x100}, self.memory, self.debug, image)
            for source in ("mac_scan.c", "test_mac_scan.c", "host_mac_scan.c"):
                with self.subTest(image=image, source=source), self.assertRaisesRegex(ValueError, "MAC scan"):
                    verify_layout(self.symbols, self.memory, self.debug+f"\nC${source}$1", image)

    def test_isolated_mac_association_cannot_enter_board_images(self):
        for image in IMAGES:
            for name in ("_mac_association_init", "_mac_association_start", "_mac_association_step",
                         "_mac_association_take", "_mac_association_result", "_mac_association_done"):
                with self.subTest(image=image, name=name), self.assertRaisesRegex(ValueError, "Association Response"):
                    verify_layout(self.symbols | {name: 0x100}, self.memory, self.debug, image)
            for source in ("mac_association.c", "test_mac_association.c"):
                with self.subTest(image=image, source=source), self.assertRaisesRegex(ValueError, "Association Response"):
                    verify_layout(self.symbols, self.memory, self.debug+f"\nC${source}$1", image)

    def test_isolated_mac_poll_cannot_enter_board_images(self):
        for image in IMAGES:
            for name in ("_mac_poll_init", "_mac_poll_start", "_mac_poll_step",
                         "_mac_poll_take", "_mac_poll_release", "_mac_poll_result", "_mac_poll_floor"):
                with self.subTest(image=image, name=name), self.assertRaisesRegex(ValueError, "MAC poll"):
                    verify_layout(self.symbols | {name: 0x100}, self.memory, self.debug, image)
            for source in ("mac_poll.c", "test_mac_poll.c", "host_mac_poll.c"):
                with self.subTest(image=image, source=source), self.assertRaisesRegex(ValueError, "MAC poll"):
                    verify_layout(self.symbols, self.memory, self.debug+f"\nC${source}$1", image)

    def test_isolated_mac_time_cannot_enter_board_images(self):
        for image in IMAGES:
            for name in ("_mac_time_init", "_mac_time_read_live", "_mac_time_diagnostic",
                         "_mac_time_fault", "_mac_time_test_result"):
                with self.subTest(image=image, name=name), self.assertRaisesRegex(ValueError, "MAC Timer"):
                    verify_layout(self.symbols | {name: 0x100}, self.memory, self.debug, image)
            for source in ("mac_time.c", "test_mac_time.c", "host_mac_time.c"):
                with self.subTest(image=image, source=source), self.assertRaisesRegex(ValueError, "MAC Timer"):
                    verify_layout(self.symbols, self.memory, self.debug+f"\nC${source}$1", image)

    def test_isolated_nwk_candidates_cannot_enter_board_images(self):
        for image in IMAGES:
            for name in ("_nwk_candidates_init", "_nwk_candidates_consider",
                         "_nwk_candidates_get", "_nwk_candidates_test_result"):
                with self.subTest(image=image, name=name), self.assertRaisesRegex(ValueError, "NWK candidate"):
                    verify_layout(self.symbols | {name: 0x100}, self.memory, self.debug, image)
            for source in ("nwk_candidates.c", "test_nwk_candidates.c"):
                with self.subTest(image=image, source=source), self.assertRaisesRegex(ValueError, "NWK candidate"):
                    verify_layout(self.symbols, self.memory, self.debug+f"\nC${source}$1", image)

    def test_isolated_mac_tx_cannot_enter_board_images(self):
        for image in IMAGES:
            for name in ("_mac_tx_init", "_mac_tx_submit", "_mac_tx_step", "_mac_tx_result"):
                with self.subTest(image=image, name=name), self.assertRaisesRegex(ValueError, "MAC TX scheduler"):
                    verify_layout(self.symbols | {name: 0x100}, self.memory, self.debug, image)
            for source in ("mac_tx.c", "test_mac_tx.c"):
                with self.subTest(image=image, source=source), self.assertRaisesRegex(ValueError, "MAC TX scheduler"):
                    verify_layout(self.symbols, self.memory, self.debug+f"\nC${source}$1", image)

    def test_isolated_nv_record_cannot_enter_board_images(self):
        for image in IMAGES:
            for name in ("_nv_record_load", "_nv_record_replace", "_nv_record_fault", "_nv_record_test_result"):
                with self.subTest(image=image, name=name), self.assertRaisesRegex(ValueError, "NV record"):
                    verify_layout(self.symbols | {name: 0x100}, self.memory, self.debug, image)
            for source in ("nv_record.c", "test_nv_record.c"):
                with self.subTest(image=image, source=source), self.assertRaisesRegex(ValueError, "NV record"):
                    verify_layout(self.symbols, self.memory, self.debug+f"\nC${source}$1", image)

    def test_isolated_radio_tx_cannot_enter_board_images(self):
        for image in IMAGES:
            if image == "radio_tx_fixture":
                continue
            for name in ("_radio_tx_send_init", "_radio_tx_cca_init", "_radio_tx_fault",
                         "_radio_tx_fixture_state", "_radio_tx_fixture_poll"):
                with self.subTest(image=image, name=name), self.assertRaisesRegex(ValueError, "radio TX/CCA"):
                    verify_layout(self.symbols | {name: 0x100}, self.memory, self.debug, image)
            for source in ("radio_tx.c", "radio_tx_fixture.c", "radio_tx_fixture_state.c"):
                with self.subTest(image=image, source=source), self.assertRaisesRegex(ValueError, "radio TX/CCA"):
                    verify_layout(self.symbols, self.memory, self.debug+f"\nC${source}$1", image)

    def test_radio_native_callers_never_enter_board_images(self):
        for image in IMAGES:
            for name in ("_radio_tx_test_result", "_radio_tx_component_main", "_radio_fifo_test_result"):
                with self.subTest(image=image, name=name), self.assertRaisesRegex(ValueError, "tests/models"):
                    verify_layout(self.symbols | {name: 0x100}, self.memory, self.debug, image)
            for source in ("test_radio_tx.c", "test_radio_tx_fixture.c",
                           "test_radio_fifo.c", "test_radio_fifo_fixture.c"):
                with self.subTest(image=image, source=source), self.assertRaisesRegex(ValueError, "tests/models"):
                    verify_layout(self.symbols, self.memory, self.debug+f"\nC${source}$1", image)

    def test_isolated_radio_queue_cannot_enter_board_images(self):
        for image in IMAGES:
            for name in ("_radio_queue_request_rx", "_radio_queue_service", "_radio_queue_rx", "_radio_queue_test_result"):
                with self.subTest(image=image, name=name), self.assertRaisesRegex(ValueError, "radio queue"):
                    verify_layout(self.symbols | {name: 0x100}, self.memory, self.debug, image)
            for source in ("radio_queue.c", "test_radio_queue.c"):
                with self.subTest(image=image, source=source), self.assertRaisesRegex(ValueError, "radio queue"):
                    verify_layout(self.symbols, self.memory, self.debug+f"\nC${source}$1", image)

    def test_isolated_irq_cannot_enter_board_images(self):
        for name in ("_irq_save_disable", "_irq_restore"):
            with self.assertRaisesRegex(ValueError, "isolated IRQ"):
                verify_layout(dict(self.symbols, **{name: 0x100}), self.memory, self.debug)
        with self.assertRaisesRegex(ValueError, "isolated IRQ"):
            verify_layout(self.symbols, self.memory, self.debug + "\nC$irq.c$1")

    def test_isolated_radio_fifo_cannot_enter_board_images(self):
        for image in IMAGES:
            if image in ("radio_fifo_fixture", "radio_tx_fixture"):
                continue
            for name in ("_radio_fifo_clear_init", "_radio_fifo_preload_init"):
                with self.subTest(image=image, name=name), self.assertRaisesRegex(ValueError, "isolated radio FIFO"):
                    verify_layout(dict(self.symbols, **{name: 0x100}), self.memory, self.debug, image)
            with self.assertRaisesRegex(ValueError, "isolated radio FIFO"):
                verify_layout(self.symbols, self.memory, self.debug + "\nC$radio_fifo.c$1", image)

    def test_fifo_board_caller_cannot_enter_tx_fixture(self):
        for name in ("_radio_fifo_fixture_state", "_radio_fifo_fixture_cycle"):
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, "FIFO board caller"):
                verify_layout(self.symbols | {name: 0x100}, self.memory, self.debug, "radio_tx_fixture")
        for source in ("radio_fifo_fixture.c", "radio_fifo_fixture_state.c"):
            with self.subTest(source=source), self.assertRaisesRegex(ValueError, "FIFO board caller"):
                verify_layout(self.symbols, self.memory, self.debug+f"\nC${source}$1", "radio_tx_fixture")

    def test_passive_rx_never_enters_existing_board_images(self):
        for image in IMAGES:
            if image == "radio_rx_fixture":
                continue
            for name in ("_radio_rx_receive_init", "_radio_rx_fault", "_radio_rx_reserved_end",
                         "_radio_rx_test_frame", "_radio_rx_test_result"):
                with self.subTest(image=image, name=name), self.assertRaisesRegex(ValueError, "isolated passive RX"):
                    verify_layout(self.symbols | {name: 0x100}, self.memory, self.debug, image)
            for source in ("radio_rx.c", "test_radio_rx.c", "radio_rx_fixture.c", "radio_rx_fixture_state.c"):
                with self.subTest(image=image, source=source), self.assertRaisesRegex(ValueError, "isolated passive RX"):
                    verify_layout(self.symbols, self.memory, self.debug + f"\nC${source}$1", image)

    def test_dma_cannot_enter_other_board_images(self):
        self.assertEqual(len(IMAGES), 13)
        for image in IMAGES:
            if image == "dma_fixture":
                continue
            for name in ("_dma_copy_init", "_dma_descriptor", "_dma_fault", "_dma_reserved_end"):
                with self.subTest(image=image, name=name), self.assertRaisesRegex(ValueError, "isolated DMA"):
                    verify_layout(dict(self.symbols, **{name: 0x100}), self.memory, self.debug, image)
            with self.assertRaisesRegex(ValueError, "isolated DMA"):
                verify_layout(self.symbols, self.memory, self.debug + "\nC$dma.c$1", image)

    def test_aes_cannot_enter_other_board_images(self):
        self.assertEqual(len(IMAGES), 13)
        for image in IMAGES:
            if image == "aes_fixture":
                continue
            for name in ("_aes128_encrypt_block", "_aes_dma0", "_aes_dma1", "_aes_key",
                         "_aes_output", "_aes_fault", "_aes_reserved_end", "_aes_fixture_state"):
                with self.subTest(image=image, name=name), self.assertRaisesRegex(ValueError, "isolated AES"):
                    verify_layout(self.symbols | {name: 0x100}, self.memory, self.debug, image)
            for source in ("aes.c", "aes_fixture.c", "aes_fixture_state.c"):
                with self.subTest(image=image, source=source), self.assertRaisesRegex(ValueError, "isolated AES"):
                    verify_layout(self.symbols, self.memory, self.debug + f"\nC${source}$1", image)

    def test_host_only_math_never_enters_any_board_image(self):
        for image in IMAGES:
            for name in ("_aes_reference_encrypt", "_aes_reference_check"):
                with self.subTest(image=image, name=name), self.assertRaisesRegex(ValueError, "host-only AES"):
                    verify_layout(self.symbols | {name: 0x100}, self.memory, self.debug, image)
            for source in ("aes_reference.c", "test_aes.c", "test_aes_fixture.c"):
                with self.subTest(image=image, source=source), self.assertRaisesRegex(ValueError, "host-only AES"):
                    verify_layout(self.symbols, self.memory, self.debug + f"\nC${source}$1", image)

    def test_deterministic_prng_and_host_model_never_enter_board_images(self):
        for image in IMAGES:
            if image == "prng_fixture":
                continue
            for name in ("_prng_seed_explicit", "_prng_next16", "_prng_fault", "_prng_reserved_end"):
                with self.subTest(image=image, name=name), self.assertRaisesRegex(ValueError, "isolated deterministic PRNG"):
                    verify_layout(self.symbols | {name: 0x100}, self.memory, self.debug, image)
            for source in ("prng.c", "prng_fixture.c", "prng_fixture_state.c"):
                with self.subTest(image=image, source=source), self.assertRaisesRegex(ValueError, "isolated deterministic PRNG"):
                    verify_layout(self.symbols, self.memory, self.debug + f"\nC${source}$1", image)

    def test_prng_models_never_enter_any_board_image(self):
        for image in IMAGES:
            with self.subTest(image=image), self.assertRaisesRegex(ValueError, "PRNG host model"):
                verify_layout(self.symbols | {"_prng_reference": 0x100}, self.memory, self.debug, image)
            for source in ("test_prng.c", "test_prng_fixture.c"):
                with self.subTest(image=image, source=source), self.assertRaisesRegex(ValueError, "PRNG host model"):
                    verify_layout(self.symbols, self.memory, self.debug + f"\nC${source}$1", image)
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
