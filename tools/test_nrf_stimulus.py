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


def synthetic_elf():
    """Synthetic parser fixture, not executable evidence or a Cortex-M model."""
    data = bytearray(0x220)
    data[:16] = b"\x7fELF\x01\x01\x01" + bytes(9)
    struct.pack_into("<HHIIIIIHHHHHH", data, 16, 2, 40, 1, 9, 52, 0x180,
                     0x05000000, 52, 32, 2, 40, 4, 3)
    struct.pack_into("<IIIIIIII", data, 52, 1, 0x100, 0, 0, 32, 32, 5, 4)
    struct.pack_into("<IIIIIIII", data, 84, 1, 0, artifact.RAM_START,
                     artifact.RAM_START, 0, 64, 6, 4)
    data[0x100:0x120] = struct.pack("<II", artifact.RAM_START + 64, 9) + b"\xaa" * 24
    strings = b"\0.text\0.bss\0.shstrtab\0"
    data[0x140:0x140 + len(strings)] = strings
    struct.pack_into("<IIIIIIIIII", data, 0x180 + 40, 1, 1, 6, 0, 0x100, 32, 0, 0, 4, 0)
    struct.pack_into("<IIIIIIIIII", data, 0x180 + 80, 7, 8, 3, artifact.RAM_START,
                     0, 64, 0, 0, 4, 0)
    struct.pack_into("<IIIIIIIIII", data, 0x180 + 120, 12, 3, 0, 0, 0x140,
                     len(strings), 0, 0, 1, 0)
    return data


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
        config = "".join(f"CONFIG_{name}=y\n" for name in names)
        config += ('CONFIG_HEAP_MEM_POOL_SIZE=0\nCONFIG_NRF_802154_RX_BUFFERS=4\n'
                   'CONFIG_FLASH_SIZE=1024\nCONFIG_SRAM_SIZE=256\n'
                   'CONFIG_BOARD="nrf52840dk_nrf52840"\n')
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
