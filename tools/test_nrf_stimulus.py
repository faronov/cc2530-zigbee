# SPDX-License-Identifier: BSD-3-Clause
"""Ordinary offline tests: no SDK, downloads, hardware or target build."""

import ctypes
import os
from pathlib import Path
import shutil
import shlex
import struct
import subprocess
import tempfile
import unittest
from unittest import mock

from tools.nrf_stimulus import artifact, protocol, verify_build

SOURCE = Path(__file__).resolve().parent / "nrf_stimulus"
EXPECTED_NATIVE_CHECKS = 160256


def hex_record(kind, address=0, data=b""):
    raw = bytes((len(data), address >> 8, address & 255, kind)) + data
    return ":" + (raw + bytes((-sum(raw) & 255,))).hex().upper() + "\n"


def synthetic_elf(*, ram_only=False):
    """Synthetic parser fixture, not executable evidence or a Cortex-M model."""
    start = artifact.RAM_START if ram_only else 0
    bss = artifact.RAM_START + (32 if ram_only else 0)
    data = bytearray(0x220)
    data[:16] = b"\x7fELF\x01\x01\x01" + bytes(9)
    struct.pack_into("<HHIIIIIHHHHHH", data, 16, 2, 40, 1, start + 9, 52, 0x180,
                     0x05000000, 52, 32, 2, 40, 4, 3)
    struct.pack_into("<IIIIIIII", data, 52, 1, 0x100, start, start, 32, 32, 5, 4)
    struct.pack_into("<IIIIIIII", data, 84, 1, 0, bss, bss, 0, 64, 6, 4)
    data[0x100:0x120] = struct.pack("<II", bss + 64, start + 9) + b"\xaa" * 24
    strings = b"\0.text\0.bss\0.shstrtab\0"
    data[0x140:0x140 + len(strings)] = strings
    struct.pack_into("<IIIIIIIIII", data, 0x180 + 40, 1, 1, 6, start, 0x100, 32, 0, 0, 4, 0)
    struct.pack_into("<IIIIIIIIII", data, 0x180 + 80, 7, 8, 3, bss,
                     0, 64, 0, 0, 4, 0)
    struct.pack_into("<IIIIIIIIII", data, 0x180 + 120, 12, 3, 0, 0, 0x140,
                     len(strings), 0, 0, 1, 0)
    return data


def synthetic_configuration():
    names = ("NRF_802154_RADIO_DRIVER", "NRF_802154_SOURCE_NRFXLIB",
             "NRF_802154_SL_OPENSOURCE", "ENTROPY_NRF5_RNG",
             "UART_INTERRUPT_DRIVEN", "UART_0_INTERRUPT_DRIVEN", "UART_NRFX_UARTE",
             "NRF_APPROTECT_USE_UICR", "ASSERT")
    config = "".join(f"CONFIG_{name}=y\n" for name in names)
    return config + ('CONFIG_XIP=y\nCONFIG_HEAP_MEM_POOL_SIZE=0\nCONFIG_NRF_802154_RX_BUFFERS=4\n'
                     'CONFIG_FLASH_SIZE=1024\nCONFIG_SRAM_SIZE=256\n'
                     'CONFIG_BOARD="nrf52840dk_nrf52840"\n')


class PortableControlTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("cc")
        if compiler is None:
            raise unittest.SkipTest("host C compiler required for portable C checks")
        cls.temp = tempfile.TemporaryDirectory(prefix="ns51-offline-")
        cls.addClassCleanup(cls.temp.cleanup)
        root = Path(cls.temp.name)
        cls.native, cls.sanitized = root / "native", root / "sanitized"
        strict = [compiler, "-std=c99", "-Wall", "-Wextra", "-Werror", "-pedantic"]
        sources = [str(SOURCE / name) for name in ("control.c", "transport.c", "test_control.c")]
        subprocess.run(strict + ["-O2"] + sources + ["-o", str(cls.native)],
                       check=True, capture_output=True, timeout=30)
        subprocess.run(strict + ["-O1", "-g", "-fno-omit-frame-pointer", "-fno-pie",
                                 "-no-pie", "-fsanitize=address,undefined",
                                 "-fno-sanitize-recover=all"] +
                       sources + ["-o", str(cls.sanitized)],
                       check=True, capture_output=True, timeout=30)
        shared = root / "control.so"
        subprocess.run(strict + ["-O2", "-shared", "-fPIC", str(SOURCE / "control.c"),
                                 "-o", str(shared)], check=True, capture_output=True, timeout=30)
        cls.lib = ctypes.CDLL(str(shared))
        cls.lib.stim_encode.argtypes = [ctypes.c_uint8, ctypes.c_void_p, ctypes.c_size_t,
                                       ctypes.c_void_p, ctypes.c_size_t]
        cls.lib.stim_encode.restype = ctypes.c_size_t

    def test_strict_native_complete_corpus(self):
        result = subprocess.run([str(self.native)], check=True, capture_output=True, text=True,
                                timeout=15)
        self.assertEqual(result.stdout, f"NS51 portable checks: {EXPECTED_NATIVE_CHECKS}\n")
        self.assertEqual(result.stderr, "")

    def test_real_asan_ubsan_complete_corpus(self):
        env = dict(os.environ, ASAN_OPTIONS="detect_leaks=1:halt_on_error=1",
                   UBSAN_OPTIONS="halt_on_error=1")
        result = subprocess.run([str(self.sanitized)], env=env, check=True,
                                capture_output=True, text=True, timeout=15)
        self.assertEqual(result.stdout, f"NS51 portable checks: {EXPECTED_NATIVE_CHECKS}\n")
        self.assertEqual(result.stderr, "")

    def test_all_c_python_wire_lengths_and_inactive_output(self):
        for length in range(protocol.MAX_PAYLOAD + 1):
            payload = bytes((i * 137) % 256 for i in range(length))
            raw = ctypes.create_string_buffer(payload)
            out = ctypes.create_string_buffer(b"Z" * (protocol.MAX_LINE + 8))
            size = self.lib.stim_encode(0x85, raw, length, out, protocol.MAX_LINE)
            expected = protocol.encode(0x85, payload)
            self.assertEqual(out.raw[:size], expected)
            self.assertEqual(out.raw[size:-1], b"Z" * (protocol.MAX_LINE + 8 - size))
            self.assertEqual(protocol.decode(expected), (0x85, payload))

    def test_real_c_transcript_separates_schedule_phy_and_receipt(self):
        result = subprocess.run([str(self.native), "--vectors"], check=True,
                                capture_output=True, timeout=15)
        records = [protocol.record(line + b"\n") for line in result.stdout.splitlines()]
        self.assertEqual([r.kind for r in records], [
            "HELLO", "ARMED", "TX_REQUESTED", "SCHEDULED", "PHY_TX_DONE",
            "RX_FRAME", "STOP_REQUESTED", "STOP_ACCEPTED", "TERMINAL"])
        self.assertEqual([r.sequence for r in records], list(range(1, 10)))
        self.assertEqual(records[5].data, b"\xb0\xc8\x01\x03\x02\x00\x51")
        self.assertEqual(records[-1].data, b"\x01\x01\x01\x01\x01\x01")
        self.assertEqual(records[1].nonce, bytes(range(32, 40)))


class ProtocolTests(unittest.TestCase):
    def test_descriptor_and_frame(self):
        self.assertEqual(protocol.DESCRIPTOR.hex(),
                         "9dec858b7b61ebf360364b4b6596bec392d3f8d11371c6a58060d12fc13f09b9")
        self.assertEqual(len(protocol.BODY), 20)
        self.assertEqual(protocol.BODY[:9], bytes.fromhex("618851feca34127856"))
        self.assertEqual(protocol.crc16(b"123456789"), 0x29B1)
        self.assertEqual(len(protocol.command(1, b"12345678")), 91)
        self.assertEqual(protocol.decode(protocol.command(0)), (0, b""))

    def test_command_guards(self):
        for kind, nonce in [(1, None), (2, b"\0" * 8), (3, b"12345678"), (0, b"1"),
                            (1, b"short"), (2, "12345678")]:
            with self.subTest(kind=kind, nonce=nonce), self.assertRaises(protocol.ProtocolError):
                protocol.command(kind, nonce)
        with self.assertRaises(protocol.ProtocolError):
            protocol.command(1, b"12345678", bytes(31))
        for kind in (-1, 256, True, "1"):
            with self.assertRaises(protocol.ProtocolError):
                protocol.encode(kind)

    def test_every_single_bit_crc_mutation(self):
        line = protocol.command(1, b"12345678")
        raw = bytearray.fromhex(line.decode())
        for bit in range(len(raw) * 8):
            changed = raw.copy()
            changed[bit // 8] ^= 1 << (bit % 8)
            with self.subTest(bit=bit), self.assertRaises(protocol.ProtocolError):
                protocol.decode(changed.hex().upper().encode() + b"\n")

    def test_framing_lengths_and_noncanonical_input(self):
        good = protocol.command(1, b"12345678")
        for bad in [b"", good[:-1], good + b"\n", good.lower(), b" " + good,
                    good[:-1] + b"\r\n", good + b"\0", b"F\n",
                    b"A" * (protocol.MAX_LINE + 1), good[1:], good.decode()]:
            with self.subTest(bad=bad), self.assertRaises(protocol.ProtocolError):
                protocol.decode(bad)
        for length in (146, 255, 256):
            with self.assertRaises(protocol.ProtocolError):
                protocol.encode(0, bytes(length))

    def test_record_specific_shapes(self):
        prefix = struct.pack("<HBBI8s", 1, 3, 0, 123, b"12345678")
        for length in range(3, 126):
            body = bytes((length,)) * length
            line = protocol.encode(0x85, prefix + b"\x80\xff\x01" + bytes((length,)) + body)
            self.assertEqual(protocol.record(line).data[4:], body)
        invalid = [(0x85, b""), (0x85, b"\0\0\0\3abc"), (0x85, b"\0\0\1\4abc"),
                   (0x83, b"\2"), (0x84, b"\0"), (0x80, bytes(32)),
                   (0x89, bytes((0, 0, 5, 0, 0, 0))), (0x8A, b""), (0x90, b"")]
        for kind, data in invalid:
            with self.subTest(kind=kind, data=data), self.assertRaises(protocol.ProtocolError):
                protocol.record(protocol.encode(kind, prefix + data))
        for offset, value in [(0, 0), (0, 35), (2, 7), (3, 19)]:
            broken = bytearray(prefix)
            broken[offset] = value
            with self.assertRaises(protocol.ProtocolError):
                protocol.record(protocol.encode(0x84, bytes(broken)))


class ArtifactTests(unittest.TestCase):
    def setUp(self):
        self.image = synthetic_elf()
        self.hex = hex_record(0, 0, self.image[0x100:0x120]) + hex_record(1)

    def test_synthetic_elf_hex_and_both_entry_encodings(self):
        image = artifact.compare(bytes(self.image), self.hex)
        self.assertEqual((image.flash_extent, image.sram_allocated, image.sram_extent), (32, 64, 64))
        self.assertEqual(artifact.compare(bytes(self.image), self.hex.replace("\n", "\r\n")).entry, 9)
        for kind in (3, 5):
            text = self.hex.splitlines(keepends=True)[0]
            text += hex_record(kind, 0, b"\0\0\0\x09") + hex_record(1)
            self.assertEqual(artifact.compare(bytes(self.image), text).entry, 9)

    def test_only_proven_zero_alignment_padding_uses_upstream_ff_gap_fill(self):
        image = self.image.copy()
        struct.pack_into("<I", image, 0x180 + 40 + 20, 31)
        with self.assertRaisesRegex(artifact.ArtifactError, "hidden LOAD padding"):
            artifact.elf(bytes(image))
        image[0x11F] = 0
        filled = hex_record(0, 0, image[0x100:0x11F] + b"\xff") + hex_record(1)
        self.assertEqual(artifact.compare(bytes(image), filled).flash_extent, 32)
        unfilled = hex_record(0, 0, image[0x100:0x120]) + hex_record(1)
        with self.assertRaises(artifact.ArtifactError):
            artifact.compare(bytes(image), unfilled)

    def test_hex_refuses_excluded_ranges_and_overlaps(self):
        for upper in (0x0010, 0x1000, 0x1001, 0x2000, 0xFFFF):
            bad = hex_record(4, 0, upper.to_bytes(2, "big"))
            bad += hex_record(0, 0x1000 if upper == 0x1000 else 0, b"x") + hex_record(1)
            with self.subTest(upper=upper), self.assertRaises(artifact.ArtifactError):
                artifact.ihex(bad)
        bad = hex_record(4, 0, b"\0\4") + hex_record(0, 0, b"x") + hex_record(1)
        with self.assertRaises(artifact.ArtifactError):
            artifact.ihex(bad)
        for bad in [self.hex + hex_record(1), self.hex.replace(":00000001FF\n", ""),
                    hex_record(0, 0, b"x") * 2 + hex_record(1),
                    hex_record(0, 0xFFFF, b"xx") + hex_record(1),
                    hex_record(1, 1), hex_record(6), self.hex.replace("AA", "AB", 1),
                    "\n" + self.hex, hex_record(0, 0, b"") + hex_record(1),
                    hex_record(4, 1, b"\0\0") + self.hex,
                    hex_record(3, 0, b"\0\0\0\x09") * 2 + self.hex]:
            with self.subTest(bad=bad), self.assertRaises(artifact.ArtifactError):
                artifact.ihex(bad)
        for separator in ("\r", "\v", "\f", "\x1c", "\x85", "\u2028", "\u2029"):
            with self.subTest(separator=separator), self.assertRaises(artifact.ArtifactError):
                artifact.ihex(self.hex.replace("\n", separator))

    def test_elf_excluded_ranges_geometry_entry_and_truncation(self):
        # field offset, invalid little-endian value
        for offset, value in [(52 + 12, 0x10001000), (52 + 8, 0x10000000),
                              (52 + 16, 33), (84 + 20, 0x10004),
                              (84 + 8, 0x20040000), (84 + 24, 7),
                              (24, 8), (24, 0x10001), (0x100, 0x10001000),
                              (0x104, 11), (28, len(self.image)), (32, len(self.image))]:
            broken = self.image.copy()
            struct.pack_into("<I", broken, offset, value)
            with self.subTest(offset=offset, value=value), self.assertRaises(artifact.ArtifactError):
                artifact.elf(bytes(broken))
        for length in (0, 51, 53, 0x100, len(self.image) - 1):
            with self.assertRaises(artifact.ArtifactError):
                artifact.elf(bytes(self.image[:length]))
        with self.assertRaises(artifact.ArtifactError):
            artifact.compare(bytes(self.image), self.hex.replace("AA", "AB", 1))
        body = bytes(self.image[0x100:0x120])
        changed = hex_record(0, 0, body[:-1] + b"\xab") + hex_record(1)
        with self.assertRaises(artifact.ArtifactError):
            artifact.compare(bytes(self.image), changed)

    def test_undefined_not_zero_startup_and_unreviewed_body(self):
        macros = "#define NRF52840_XXAA 1\n#define CONFIG_NRF_APPROTECT_USE_UICR 1\n"
        for name in ("CONFIG_GPIO_AS_PINRESET", "CONFIG_NFCT_PINS_AS_GPIOS",
                     "CONFIG_NRF_APPROTECT_LOCK", "ENABLE_APPROTECT"):
            for value in ("0", "1"):
                with self.assertRaisesRegex(artifact.ArtifactError, "UNDEFINED"):
                    artifact.startup(macros + f"#define {name} {value}\n", "")
        with self.assertRaisesRegex(artifact.ArtifactError, "unreviewed"):
            artifact.startup(macros, "void SystemInit(void) {}\n")
        with self.assertRaises(artifact.ArtifactError):
            artifact.function_body("void SystemInit(void) {", "SystemInit")
        with self.assertRaises(artifact.ArtifactError):
            artifact.function_body("void SystemInit(void) {} void SystemInit(void) {}", "SystemInit")

    def test_configuration_requires_real_source_profile_and_rejects_writers(self):
        names = ("NRF_802154_RADIO_DRIVER", "NRF_802154_SOURCE_NRFXLIB",
                 "NRF_802154_SL_OPENSOURCE", "ENTROPY_NRF5_RNG",
                 "UART_INTERRUPT_DRIVEN", "UART_0_INTERRUPT_DRIVEN", "UART_NRFX_UARTE",
                 "NRF_APPROTECT_USE_UICR", "ASSERT")
        config = synthetic_configuration()
        dts = "/ { chosen { zephyr,entropy = &rng; }; };"
        artifact.configuration(config, dts)
        for name in names:
            with self.subTest(name=name), self.assertRaises(artifact.ArtifactError):
                artifact.configuration(config.replace(f"CONFIG_{name}=y\n", ""), dts)
        for name in ("FLASH", "NRFX_NVMC", "NFCT_PINS_AS_GPIOS", "MPSL", "HW_CC3XX",
                     "BOOTLOADER_PROVISION_HEX", "NRF_REGTOOL_GENERATE_UICR", "REBOOT",
                     "NRF_802154_SOURCE_HAL_NORDIC", "ZERO_LATENCY_IRQS", "RESET_ON_FATAL_ERROR"):
            with self.subTest(name=name), self.assertRaises(artifact.ArtifactError):
                artifact.configuration(config + f"CONFIG_{name}=y\n", dts)
        for mutation in ("gpio-as-nreset;", "nfct-pins-as-gpios;"):
            with self.assertRaises(artifact.ArtifactError):
                artifact.configuration(config, dts + mutation)
        with self.assertRaises(artifact.ArtifactError):
            artifact.configuration(config.replace("CONFIG_XIP=y\n", ""), dts)


class SramArtifactTests(unittest.TestCase):
    def setUp(self):
        self.image = synthetic_elf(ram_only=True)
        self.prefix = hex_record(4, 0, b"\x20\x00")
        self.hex = self.prefix + hex_record(0, 0, self.image[0x100:0x120]) + hex_record(1)

    def test_explicit_profile_counts_code_data_and_bss_without_flash(self):
        image = artifact.compare(bytes(self.image), self.hex, ram_only=True)
        self.assertEqual((image.flash_extent, image.load_start, image.load_extent,
                          image.sram_allocated, image.sram_extent),
                         (0, 0x20000000, 32, 96, 96))
        self.assertEqual(set(image.memory), set(range(0x20000000, 0x20000020)))
        self.assertEqual(image.entry, 0x20000009)
        entry = hex_record(5, 0, (0x20000009).to_bytes(4, "big"))
        with_entry = self.hex.replace(hex_record(1), entry + hex_record(1))
        self.assertEqual(artifact.compare(bytes(self.image), with_entry, ram_only=True), image)
        self.assertEqual(artifact.compare(bytes(self.image), self.hex.replace("\n", "\r\n"),
                                          ram_only=True), image)

    def test_flash_and_sram_profiles_never_auto_detect_or_accept_each_other(self):
        flash = synthetic_elf()
        flash_hex = hex_record(0, 0, flash[0x100:0x120]) + hex_record(1)
        for data, text, ram_only in ((self.image, self.hex, False), (flash, flash_hex, True)):
            with self.subTest(ram_only=ram_only):
                with self.assertRaises(artifact.ArtifactError):
                    artifact.elf(bytes(data), ram_only=ram_only)
                with self.assertRaises(artifact.ArtifactError):
                    artifact.ihex(text, ram_only=ram_only)
                with self.assertRaises(artifact.ArtifactError):
                    artifact.compare(bytes(data), text, ram_only=ram_only)
        for profile in (None, 0, 1, "ram", "flash"):
            with self.subTest(profile=profile), self.assertRaises(artifact.ArtifactError):
                artifact.compare(bytes(self.image), self.hex, ram_only=profile)

    def test_rejects_non_sram_loads_aliases_overlap_and_nonexecutable_entry(self):
        changes = [(52 + 12, address) for address in
                   (0, 0x10000000, 0x10001000, 0x40000000, 0x20000004, 0x20020000)]
        changes += [(52 + 8, 0), (52 + 24, 6), (52 + 20, 33),
                    (84 + 12, 0), (84 + 8, 0x20020000),
                    (0x180 + 40 + 8, 2), (0x180 + 40 + 12, 0),
                    (0x180 + 80 + 12, 0x20020000),
                    (24, 0x20000008), (24, 0x20000101),
                    (0x100, 0x20020008), (0x100, 0x20000064),
                    (0x100, 0x20000010), (84 + 24, 4), (0x104, 0x2000000B)]
        for offset, value in changes:
            bad = self.image.copy()
            struct.pack_into("<I", bad, offset, value)
            with self.subTest(offset=offset, value=value), self.assertRaises(artifact.ArtifactError):
                artifact.elf(bytes(bad), ram_only=True)

    def test_exact_128k_total_extent_including_nobits_and_one_byte_over(self):
        image = self.image.copy()
        for offset in (84 + 20, 0x180 + 80 + 20):
            struct.pack_into("<I", image, offset, 128 * 1024 - 32)
        result = artifact.elf(bytes(image), ram_only=True)
        self.assertEqual((result.sram_allocated, result.sram_extent), (128 * 1024, 128 * 1024))
        struct.pack_into("<I", image, 84 + 20, 128 * 1024 - 31)
        with self.assertRaises(artifact.ArtifactError):
            artifact.elf(bytes(image), ram_only=True)
        last = hex_record(4, 0, b"\x20\x01") + hex_record(0, 0xFFFF, b"x")
        self.assertEqual(artifact.ihex(last + hex_record(1), ram_only=True)[0],
                         {0x2001FFFF: ord("x")})
        extra = hex_record(4, 0, b"\x20\x02") + hex_record(0, 0, b"x")
        with self.assertRaises(artifact.ArtifactError):
            artifact.ihex(last + extra + hex_record(1), ram_only=True)

    def test_hex_rejects_flash_ficr_uicr_peripherals_mixed_ranges_and_bad_entries(self):
        for upper, address in ((0, 0), (0x1000, 0), (0x1000, 0x1000),
                               (0x1001, 0x1000), (0x4000, 0), (0x2002, 0), (0x2004, 0)):
            bad = self.hex.removesuffix(hex_record(1))
            bad += hex_record(4, 0, upper.to_bytes(2, "big"))
            bad += hex_record(0, address, b"x") + hex_record(1)
            with self.subTest(upper=upper, address=address), self.assertRaises(artifact.ArtifactError):
                artifact.ihex(bad, ram_only=True)
        for entry in (9, 0x20000008, 0x2000000B, 0x20020001):
            text = self.hex.replace(hex_record(1), hex_record(5, 0, entry.to_bytes(4, "big")) +
                                    hex_record(1))
            with self.subTest(entry=entry), self.assertRaises(artifact.ArtifactError):
                artifact.compare(bytes(self.image), text, ram_only=True)
        for text in (self.hex.replace("AA", "AB", 1), self.hex + hex_record(1),
                     self.hex.removesuffix(hex_record(1)),
                     self.prefix + hex_record(0, 0, self.image[0x100:0x120]) * 2 + hex_record(1)):
            with self.subTest(text=text), self.assertRaises(artifact.ArtifactError):
                artifact.compare(bytes(self.image), text, ram_only=True)

    def test_zero_load_padding_and_gaps_use_the_same_canonical_ff_rule(self):
        image = self.image.copy()
        struct.pack_into("<I", image, 0x180 + 40 + 20, 31)
        with self.assertRaises(artifact.ArtifactError):
            artifact.elf(bytes(image), ram_only=True)
        image[0x11F] = 0
        filled = self.prefix + hex_record(0, 0, image[0x100:0x11F] + b"\xff") + hex_record(1)
        self.assertEqual(artifact.compare(bytes(image), filled, ram_only=True).load_extent, 32)

        image = self.image.copy()
        struct.pack_into("<IIIIIIII", image, 84, 1, 0x120, 0x20000040, 0x20000040,
                         16, 64, 6, 4)
        struct.pack_into("<IIIIIIIIII", image, 0x180 + 80, 7, 1, 3, 0x20000040,
                         0x120, 16, 0, 0, 4, 0)
        text = self.prefix + hex_record(0, 0, image[0x100:0x120])
        text += hex_record(0, 32, b"\xff" * 32) + hex_record(0, 64, image[0x120:0x130])
        result = artifact.compare(bytes(image), text + hex_record(1), ram_only=True)
        self.assertEqual((result.load_extent, result.sram_extent), (80, 128))
        self.assertTrue(all(result.memory[i] == 255 for i in range(0x20000020, 0x20000040)))
        with self.assertRaises(artifact.ArtifactError):
            artifact.compare(bytes(image), text.replace(hex_record(0, 32, b"\xff" * 32), "") +
                             hex_record(1), ram_only=True)

    def test_configuration_binds_non_xip_geometry_and_no_regulator_cache_enable(self):
        config = synthetic_configuration().replace("CONFIG_FLASH_SIZE=1024", "CONFIG_FLASH_SIZE=0")
        config = config.replace("CONFIG_XIP=y\n", "")
        disabled = ("XIP", "BOARD_ENABLE_DCDC", "BOARD_ENABLE_DCDC_HV", "NRF_ENABLE_ICACHE")
        config += "".join(f"# CONFIG_{name} is not set\n" for name in disabled)
        fixed = {"FLASH_BASE_ADDRESS": "0x0", "FLASH_LOAD_OFFSET": "0", "FLASH_LOAD_SIZE": "0",
                 "SRAM_BASE_ADDRESS": "0x20000000", "SRAM_OFFSET": "0",
                 "INIT_ARCH_HW_AT_BOOT": "y", "CPU_CORTEX_M_HAS_VTOR": "y",
                 "ARM_MPU": "y", "MPU_STACK_GUARD": "y"}
        config += "".join(f"CONFIG_{name}={value}\n" for name, value in fixed.items())
        dts = "/ { chosen { zephyr,entropy = &rng; }; };"
        artifact.configuration(config, dts, ram_only=True)
        for name in disabled:
            with self.subTest(missing=name), self.assertRaises(artifact.ArtifactError):
                artifact.configuration(config.replace(f"# CONFIG_{name} is not set\n", ""),
                                       dts, ram_only=True)
        for name in disabled + ("SOC_DCDC_NRF52X", "SOC_DCDC_NRF52X_HV"):
            for value in ("y", "n", "0"):
                with self.subTest(name=name, value=value), self.assertRaises(artifact.ArtifactError):
                    artifact.configuration(config + f"CONFIG_{name}={value}\n", dts, ram_only=True)
        for name, value in fixed.items():
            with self.subTest(name=name), self.assertRaises(artifact.ArtifactError):
                artifact.configuration(config.replace(f"CONFIG_{name}={value}\n", ""),
                                       dts, ram_only=True)
        for name in ("FLASH", "NRFX_NVMC", "REBOOT", "RESET_ON_FATAL_ERROR"):
            with self.subTest(name=name), self.assertRaises(artifact.ArtifactError):
                artifact.configuration(config + f"CONFIG_{name}=y\n", dts, ram_only=True)
        with self.assertRaises(artifact.ArtifactError):
            artifact.configuration(config.replace("CONFIG_FLASH_SIZE=0", "CONFIG_FLASH_SIZE=1024"),
                                   dts, ram_only=True)


class AuditBoundaryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="ns51-audit-")
        self.addCleanup(self.temp.cleanup)
        self.sdk = Path(self.temp.name).resolve()
        self.build = self.sdk / "build"
        self.build.mkdir()
        self.source = self.sdk / "nrfxlib/unit.c"
        self.source.parent.mkdir()
        self.source.write_text("/* Synthetic path fixture, never executed. */\n")
        self.compiler = self.sdk / verify_build.TOOLCHAIN / "bin/arm-zephyr-eabi-gcc"
        self.compiler.parent.mkdir(parents=True)
        self.compiler.write_bytes(b"not an executable")
        self.args = [str(self.compiler), "-std=c99", "-mcpu=cortex-m4", "-mthumb",
                     "-o", "unit.c.obj", "-c", str(self.source)]
        self.item = {"directory": str(self.build), "file": str(self.source),
                     "command": shlex.join(self.args)}

    def test_reviewed_compile_shape_and_vetted_bytes(self):
        args, source, obj = verify_build.compile_command(self.item, self.sdk, self.build)
        self.assertEqual(args, self.args)
        self.assertEqual(source, self.source)
        self.assertEqual(obj, self.build / "unit.c.obj")
        sha = artifact.digest(self.compiler.read_bytes())
        self.assertEqual(verify_build.checked_digest(self.compiler, self.sdk, sha), sha)
        self.compiler.write_bytes(b"replacement")
        with self.assertRaisesRegex(artifact.ArtifactError, "digest mismatch"):
            verify_build.checked_digest(self.compiler, self.sdk, sha)
        escaped = self.build / "escaped"
        escaped.symlink_to(SOURCE / "control.c")
        with self.assertRaisesRegex(artifact.ArtifactError, "outside trusted root"):
            verify_build.checked_digest(escaped, self.sdk, artifact.digest(escaped.read_bytes()))

    def test_execution_affecting_and_unknown_flags_rejected_before_preprocessing(self):
        additions = [
            ["-fplugin=/tmp/plugin.so"], ["-fplugin-arg-test=on"], ["-wrapper", "python,script"],
            ["-B/tmp/bin"], ["--specs=/tmp/specs"], ["-specs", "/tmp/specs"], ["@response"],
            ["-Xpreprocessor", "-include"], ["-Wp,-include,/tmp/injected.h"], ["-flto"],
            ["-O0"], ["-DUNREVIEWED=1"], ["-include", "/tmp/injected.h"],
            ["-imacros", str(self.source)], ["-I/tmp"], ["--sysroot=/tmp/sysroot"],
            ["-o", "other.obj"], ["-MF", "other.obj.d"], [str(self.source)],
        ]
        for extra in additions:
            item = dict(self.item, command=shlex.join(self.args + extra))
            with self.subTest(extra=extra), mock.patch.object(verify_build, "command") as run:
                with self.assertRaises(artifact.ArtifactError):
                    verify_build.preprocess(item, False, self.sdk, self.build)
                run.assert_not_called()

    def test_cwd_source_output_and_environment_rejected_before_execution(self):
        changes = [
            dict(self.item, directory=str(self.sdk)),
            dict(self.item, directory="build"),
            dict(self.item, file=str(self.source.parent / "other.c")),
            dict(self.item, command=shlex.join([str(self.source)] + self.args[1:])),
            dict(self.item, command=self.item["command"] + "\n"),
            dict(self.item, command=shlex.join(self.args[:5] + ["../escape.obj"] + self.args[6:])),
            dict(self.item, arguments=self.args),
        ]
        escaped = self.build / "escape.obj"
        escaped.symlink_to(self.source)
        changes.append(dict(self.item, command=shlex.join(
            ["escape.obj" if a == "unit.c.obj" else a for a in self.args])))
        alias = self.build / "gcc-alias"
        alias.symlink_to(self.compiler)
        changes.append(dict(self.item, command=shlex.join([str(alias)] + self.args[1:])))
        for item in changes:
            with self.subTest(item=item), mock.patch.object(verify_build, "command") as run:
                with self.assertRaises(artifact.ArtifactError):
                    verify_build.preprocess(item, False, self.sdk, self.build)
                run.assert_not_called()
        for value in ("", "/unreviewed"):
            with mock.patch.dict(os.environ, {"COMPILER_PATH": value}), \
                    mock.patch.object(verify_build, "command") as run:
                with self.assertRaisesRegex(artifact.ArtifactError, "environment"):
                    verify_build.preprocess(self.item, False, self.sdk, self.build)
                run.assert_not_called()

    def test_startup_branch_and_fresh_observation_guard(self):
        # Synthetic objdump text only; the manual audit also checks the real ELF.
        text = """00000100 <main>:
 100: f000 f800 bl 1000 <stim_startup_begin>
 104: f3bf 8f5f dmb sy
 108: f384 8810 msr PRIMASK, r4
 10c: b148 cbz r0, 122 <main+0x22>
 10e: f000 f800 bl 2000 <nrf_802154_init>
 112: f000 f800 bl 3000 <radio_stopped>
 116: f000 f800 bl 4000 <stim_startup_complete>
 11a: f3bf 8f5f dmb sy
 11e: f385 8810 msr PRIMASK, r5
 122: 4600 mov r0, r0
"""
        self.assertEqual(verify_build.startup_order(text)["cold_failure_skip_target"], 0x122)
        for old, new in (("cbz r0", "cbnz r0"), ("cbz r0", "cbz r1"),
                         ("122 <main+", "116 <main+"),
                         ("<radio_stopped>", "<stale_observation>"),
                         ("<stim_startup_complete>", "<stim_init>"),
                         ("dmb sy", "mov r0, r1")):
            with self.subTest(old=old, new=new), self.assertRaises(artifact.ArtifactError):
                verify_build.startup_order(text.replace(old, new))


if __name__ == "__main__":
    unittest.main()
