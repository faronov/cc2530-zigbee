# SPDX-License-Identifier: BSD-3-Clause
"""Original synthetic captures/images only; no SDK, equipment or private baseline."""
from contextlib import contextmanager, redirect_stderr, redirect_stdout
import hashlib
import io
import json
import os
from pathlib import Path
import stat
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import nrf_overlay as overlay
import nrf_recovery as recovery
import private_artifacts
from test_nrf_stimulus import hex_record, synthetic_elf


CLAIMS = {
    "physical_origin_verified", "independent_acquisition_verified", "acl_readability_verified",
    "atomic_snapshot_verified", "recovery_material_verified", "silicon_compatibility_verified",
    "firmware_startup_verified", "firmware_execution_verified", "debug_access_verified",
    "restoration_verified", "authorizes_programming",
}


def synthetic_pair(extent=32, padding=15):
    """Relocate the existing original parser fixture, not an executable Cortex-M image."""
    base = synthetic_elf()
    extra = extent - 32
    data = base[:0x120] + b"\xaa" * extra + base[0x120:]
    shoff = 0x180 + extra
    struct.pack_into("<I", data, 32, shoff)
    struct.pack_into("<II", data, 52 + 16, extent, extent)
    struct.pack_into("<I", data, shoff + 40 + 20, extent - padding)
    struct.pack_into("<I", data, shoff + 120 + 16, 0x140 + extra)
    if padding:
        data[0x100 + extent - padding:0x100 + extent] = bytes(padding)
    body = bytes(data[0x100:0x100 + extent - padding]) + b"\xff" * padding
    records = []
    for address in range(0, extent, 16):
        if address % 0x10000 == 0:
            records.append(hex_record(4, 0, (address >> 16).to_bytes(2, "big")))
        records.append(hex_record(0, address & 0xffff, body[address:address + 16]))
    records += [hex_record(5, 0, (9).to_bytes(4, "big")), hex_record(1)]
    return bytes(data), "".join(records).encode("ascii"), body


class OverlayMathTests(unittest.TestCase):
    def setUp(self):
        pattern = bytes(range(251))
        self.original = (pattern * (overlay.FLASH_BYTES // len(pattern) + 1))[:overlay.FLASH_BYTES]

    def test_full_page_exact_hashes_counts_and_unchanged_complement(self):
        start, end = 2 * 4096, 3 * 4096
        memory = {i: self.original[i] ^ 0xff for i in range(start, end)}
        result = overlay.overlay_pages(self.original, memory)
        changed = self.original[:start] + bytes(memory.values()) + self.original[end:]
        untouched = self.original[:start] + self.original[end:]
        self.assertEqual(result["pages"], [{
            "index": 2, "address": start, "bytes": 4096, "image_covered_bytes": 4096,
            "preserved_bytes": 0, "changed_bytes": 4096,
            "before_sha256": hashlib.sha256(self.original[start:end]).hexdigest(),
            "overlay_sha256": hashlib.sha256(changed[start:end]).hexdigest(),
        }])
        self.assertEqual(result["overlay_sha256"], hashlib.sha256(changed).hexdigest())
        self.assertEqual(result["unaffected_pages"], {
            "count": 255, "bytes": 255 * 4096, "unchanged": True,
            "before_sha256": hashlib.sha256(untouched).hexdigest(),
            "overlay_sha256": hashlib.sha256(untouched).hexdigest(),
        })
        self.assertEqual(result["covered_page_preserved_bytes"], 0)
        self.assertEqual(result["last_page_tail_bytes"], 0)

    def test_sparse_unaligned_bytes_preserve_holes_and_unaffected_pages(self):
        memory = {1: 255, 3: self.original[3], 4095: 254, 4096: 253, 8195: 252}
        result = overlay.overlay_pages(self.original, memory)
        self.assertEqual([p["index"] for p in result["pages"]], [0, 1, 2])
        self.assertEqual([p["image_covered_bytes"] for p in result["pages"]], [3, 1, 1])
        self.assertEqual(result["changed_bytes"], 4)
        self.assertEqual((result["image_start"], result["image_end_exclusive"]), (1, 8196))
        self.assertEqual(result["last_page_tail_bytes"], 4092)
        self.assertEqual(result["covered_page_preserved_bytes"], 3 * 4096 - 5)
        self.assertEqual(result["preserved_bytes"], overlay.FLASH_BYTES - 5)
        candidate = bytearray(self.original)
        for address, value in memory.items():
            candidate[address] = value
        self.assertEqual(result["overlay_sha256"], hashlib.sha256(candidate).hexdigest())
        self.assertTrue(result["outside_image_unchanged"])

    def test_word_page_and_provisional_image_limit_boundaries(self):
        for address in (0, 1, 2, 3, 4094, 4095, 4096, 4097,
                        overlay.artifact.FLASH_LIMIT - 4, overlay.artifact.FLASH_LIMIT - 1):
            with self.subTest(address=address):
                result = overlay.overlay_pages(self.original, {address: self.original[address] ^ 1})
                self.assertEqual(result["covered_page_count"], 1)
                self.assertEqual(result["pages"][0]["index"], address // 4096)
                self.assertEqual(result["pages"][0]["preserved_bytes"], 4095)
                self.assertEqual(result["changed_bytes"], 1)
                self.assertEqual(result["last_page_tail_bytes"], (4095 - address) % 4096)

    def test_full_helper_limit_and_equal_covered_bytes_are_not_erase_requests(self):
        memory = dict(enumerate(self.original[:overlay.artifact.FLASH_LIMIT]))
        result = overlay.overlay_pages(self.original, memory)
        self.assertEqual(result["covered_page_count"], 64)
        self.assertEqual(result["changed_bytes"], 0)
        self.assertEqual(result["unaffected_pages"]["count"], 192)
        self.assertEqual(result["before_sha256"], result["overlay_sha256"])

    def test_verifier_rejects_changes_in_image_holes_tail_and_unaffected_pages(self):
        memory = {2: 255, 4097: 254}
        valid = bytearray(self.original)
        for address, value in memory.items():
            valid[address] = value
        for address in (0, 1, 3, 4095, 4096, 4098, 8191, 8192, overlay.FLASH_BYTES - 1):
            bad = valid.copy()
            bad[address] ^= 1
            with self.subTest(address=address), self.assertRaisesRegex(ValueError, "outside image"):
                overlay.describe_overlay(self.original, bytes(bad), memory)
        bad = valid.copy()
        bad[2] ^= 1
        with self.assertRaisesRegex(ValueError, "explicit image"):
            overlay.describe_overlay(self.original, bytes(bad), memory)

    def test_invalid_coverage_extents_types_values_and_all_excluded_destinations(self):
        for memory in ({}, [], {True: 0}, {0.0: 0}, {0: True}, {0: -1}, {0: 256},
                       {0: "x"}, {-1: 0}, {overlay.artifact.FLASH_LIMIT: 0},
                       {overlay.FLASH_BYTES - 1: 0}, {overlay.FLASH_BYTES: 0},
                       {0x10000000: 0}, {0x10001000: 0}, {0x20000000: 0},
                       {0x40000000: 0}, {1 << 64: 0}):
            with self.subTest(memory=memory), self.assertRaises(ValueError):
                overlay.overlay_pages(self.original, memory)
        for original in (b"", self.original[:-1], self.original + b"x", bytearray(self.original)):
            with self.assertRaises(ValueError):
                overlay.overlay_pages(original, {0: 1})
            with self.assertRaises(ValueError):
                overlay.describe_overlay(self.original, original, {0: 1})


@unittest.skipUnless(os.name == "posix" and hasattr(os, "O_NOFOLLOW"), "POSIX private-file policy")
class PrivatePlannerTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="nrf-overlay-synthetic-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.first, self.second, self.stage = (self.root / n for n in ("first", "second", "stage"))
        self.original = (bytes(range(251)) * 4178)[:overlay.FLASH_BYTES]
        self.uicr = bytes(range(256)) * 16
        for directory in (self.first, self.second):
            directory.mkdir(mode=0o700)
            self.write(directory / "main-flash.bin", self.original)
            self.write(directory / "uicr.bin", self.uicr)
        self.stage.mkdir(mode=0o700)
        self.elf = self.stage / "private-identity.elf"
        self.hex = self.stage / "private-identity.hex"
        self.report = self.root / "private-result.json"
        self.set_image()

    @staticmethod
    def write(path, content):
        path.write_bytes(content)
        path.chmod(0o600)

    def set_image(self, extent=32, padding=15):
        self.elf_data, self.hex_data, self.body = synthetic_pair(extent, padding)
        self.write(self.elf, self.elf_data)
        self.write(self.hex, self.hex_data)

    def arguments(self):
        return ["--first", str(self.first), "--second", str(self.second),
                "--elf", str(self.elf), "--hex", str(self.hex), "--report", str(self.report)]

    def invoke(self, arguments=None):
        stdout, stderr = io.StringIO(), io.StringIO()
        with redirect_stdout(stdout), redirect_stderr(stderr):
            result = overlay.main(self.arguments() if arguments is None else arguments)
        output, error = stdout.getvalue(), stderr.getvalue()
        for private in (str(self.root), self.elf.name, self.hex.name,
                        hashlib.sha256(self.original).hexdigest(),
                        hashlib.sha256(self.uicr).hexdigest()):
            self.assertNotIn(private, output + error)
        return result, output, error

    def assert_failed(self):
        result, output, error = self.invoke()
        self.assertEqual((result, output), (1, ""))
        self.assertIn("no report accepted", error)
        self.assertFalse(self.report.exists())

    def test_exact_private_report_shape_and_no_physical_or_permission_claim(self):
        result, stdout, stderr = self.invoke()
        self.assertEqual(result, 0)
        self.assertIn("NOT authorized", stdout)
        self.assertEqual(stderr, "")
        self.assertEqual(stat.S_IMODE(self.report.stat().st_mode), 0o600)
        self.assertEqual(self.report.stat().st_nlink, 1)
        self.assertLess(self.report.stat().st_size, overlay.REPORT_LIMIT)
        report = json.loads(self.report.read_bytes())
        self.assertEqual(set(report), CLAIMS | {"schema", "evidence", "geometry", "agreement",
                                              "image", "overlay", "uicr"})
        self.assertEqual(report["schema"], "nrf52840-artifact-overlay-v1")
        self.assertEqual(report["evidence"], "offline-artifact-overlay-only")
        self.assertEqual(report["geometry"], "assumed-nrf52840-not-device-detected")
        for claim in CLAIMS:
            self.assertIs(report[claim], False)
        self.assertEqual(report["agreement"], recovery.compare_captures(self.first, self.second))
        self.assertEqual(report["image"], {
            "evidence": "strict-elf-hex-artifact-comparison-only",
            "elf_bytes": len(self.elf_data), "elf_sha256": hashlib.sha256(self.elf_data).hexdigest(),
            "hex_bytes": len(self.hex_data), "hex_sha256": hashlib.sha256(self.hex_data).hexdigest(),
            "entry": 9, "flash_extent": 32, "flash_load_bytes": 32,
            "sram_allocated": 64, "sram_extent": 64, "flash_limit": 262144,
            "allocated_sram_limit": 65536,
        })
        self.assertEqual(report["uicr"], {
            "address": 0x10001000, "bytes": 4096, "excluded": True, "unchanged": True,
            "before_sha256": hashlib.sha256(self.uicr).hexdigest(),
            "overlay_sha256": hashlib.sha256(self.uicr).hexdigest(),
        })
        math = report["overlay"]
        self.assertEqual(set(math), {
            "address", "bytes", "page_bytes", "image_start", "image_end_exclusive",
            "image_covered_bytes", "preserved_bytes", "changed_bytes", "covered_page_count",
            "covered_page_preserved_bytes", "last_page_tail_bytes", "before_sha256",
            "overlay_sha256", "outside_image_unchanged", "pages", "unaffected_pages",
        })
        expected = self.body + self.original[len(self.body):]
        self.assertEqual(math["overlay_sha256"], hashlib.sha256(expected).hexdigest())
        self.assertEqual(math["changed_bytes"], sum(a != b for a, b in zip(self.original, expected)))
        self.assertEqual(math["last_page_tail_bytes"], 4096 - 32)
        self.assertEqual({p.name for p in self.root.iterdir()},
                         {"first", "second", "stage", self.report.name})
        for directory in (self.first, self.second):
            self.assertEqual((directory / "main-flash.bin").read_bytes(), self.original)
            self.assertEqual((directory / "uicr.bin").read_bytes(), self.uicr)

    def test_accepted_extent_arithmetic_with_fifteen_real_parser_padding_differences(self):
        self.set_image(56204, 15)
        self.assertEqual(self.elf_data[0x100 + 56204 - 15:0x100 + 56204], bytes(15))
        self.assertEqual(self.body[-15:], b"\xff" * 15)
        self.assertEqual(self.invoke()[0], 0)
        result = json.loads(self.report.read_bytes())["overlay"]
        self.assertEqual(result["covered_page_count"], 14)
        self.assertEqual(result["image_covered_bytes"], 56204)
        self.assertEqual(result["last_page_tail_bytes"], 1140)
        self.assertEqual(result["covered_page_preserved_bytes"], 1140)
        self.assertEqual([p["image_covered_bytes"] for p in result["pages"]], [4096] * 13 + [2956])
        last = self.body[13 * 4096:] + self.original[56204:14 * 4096]
        self.assertEqual(result["pages"][-1]["overlay_sha256"], hashlib.sha256(last).hexdigest())
        self.assertEqual(result["unaffected_pages"]["before_sha256"],
                         hashlib.sha256(self.original[14 * 4096:]).hexdigest())

    def test_exact_page_and_tail_boundaries_through_actual_parsers(self):
        for size, pages, tail in ((4095, 1, 1), (4096, 1, 0), (4097, 2, 4095)):
            with self.subTest(size=size):
                self.set_image(size, 0)
                self.assertEqual(self.invoke()[0], 0)
                result = json.loads(self.report.read_bytes())["overlay"]
                self.assertEqual((result["covered_page_count"], result["last_page_tail_bytes"]),
                                 (pages, tail))
                self.report.unlink()

    def test_exact_existing_flash_and_sram_limits_are_admitted(self):
        self.set_image(262144, 15)
        data = bytearray(self.elf_data)
        shoff = struct.unpack_from("<I", data, 32)[0]
        struct.pack_into("<I", data, 84 + 20, 65536)
        struct.pack_into("<I", data, shoff + 80 + 20, 65536)
        self.write(self.elf, data)
        self.assertEqual(self.invoke()[0], 0)
        result = json.loads(self.report.read_bytes())
        self.assertEqual((result["image"]["flash_extent"], result["image"]["sram_allocated"],
                          result["image"]["sram_extent"]), (262144, 65536, 65536))
        self.assertEqual(result["overlay"]["covered_page_count"], 64)
        self.assertEqual(result["overlay"]["last_page_tail_bytes"], 0)

    def test_readonly_inputs_and_report_in_input_directory(self):
        paths = [self.elf, self.hex] + [d / n for d in (self.first, self.second)
                                     for n, _, _ in recovery.REGIONS]
        for path in paths:
            path.chmod(0o400)
        self.report = self.first / "overlay.json"
        self.assertEqual(self.invoke()[0], 0)
        self.assertTrue(all(stat.S_IMODE(p.stat().st_mode) == 0o400 for p in paths))

    def test_equal_fabricated_blank_captures_still_make_no_baseline_claim(self):
        for directory in (self.first, self.second):
            for name, _, size in recovery.REGIONS:
                self.write(directory / name, b"\xff" * size)
        self.assertEqual(self.invoke()[0], 0)
        result = json.loads(self.report.read_bytes())
        self.assertTrue(all(result[name] is False for name in CLAIMS))

    def test_mismatch_at_both_capture_region_extents(self):
        for name, _, size in recovery.REGIONS:
            path = self.second / name
            before = path.read_bytes()
            for offset in (0, size // 2, size - 1):
                changed = bytearray(before)
                changed[offset] ^= 1
                self.write(path, changed)
                with self.subTest(name=name, offset=offset):
                    self.assert_failed()
            self.write(path, before)

    def test_short_overlong_missing_and_bounded_input_sizes(self):
        for path, original in ((self.first / "main-flash.bin", self.original),
                               (self.second / "uicr.bin", self.uicr)):
            for size in (0, len(original) - 1, len(original) + 1):
                self.write(path, b"\0" * size)
                with self.subTest(path=path, size=size), \
                        patch.object(overlay, "private_bytes", side_effect=AssertionError("no parsing")):
                    self.assert_failed()
            self.write(path, original)
        for path, bound, original in ((self.elf, overlay.ELF_LIMIT, self.elf_data),
                                      (self.hex, overlay.HEX_LIMIT, self.hex_data)):
            for size in (0, bound + 1):
                with path.open("wb") as stream:
                    stream.truncate(size)
                with patch.object(overlay.os, "read", side_effect=AssertionError("no reading")):
                    self.assert_failed()
            path.unlink()
            self.assert_failed()
            self.write(path, original)

    def test_actual_hex_parser_rejects_malformed_overlap_sparse_and_excluded_bytes(self):
        for upper, address in ((0x0004, 0), (0x0010, 0), (0x1000, 0),
                               (0x1000, 0x1000), (0x2000, 0), (0x4000, 0)):
            bad = (hex_record(4, 0, upper.to_bytes(2, "big")) +
                   hex_record(0, address, b"x") + hex_record(1)).encode()
            self.write(self.hex, bad)
            with self.subTest(upper=upper, address=address):
                self.assert_failed()
        cases = [
            self.hex_data.replace(b"AA", b"AB", 1), self.hex_data + hex_record(1).encode(),
            self.hex_data.replace(b"\n", b"\r"), self.hex_data + b"\xff",
            (hex_record(0, 0, self.body) * 2 + hex_record(1)).encode(),
            (hex_record(0, 0, self.body[:16]) + hex_record(1)).encode(),
            (hex_record(0, 0, self.body[:-1] + b"\0") + hex_record(1)).encode(),
            (hex_record(0, 0, self.elf_data[0x100:0x120]) + hex_record(1)).encode(),
        ]
        for bad in cases:
            self.write(self.hex, bad)
            self.assert_failed()

    def test_actual_elf_parser_rejects_load_startup_vector_and_resource_violations(self):
        for offset, value in ((52 + 12, 0x10001000), (52 + 12, 0x10000000),
                              (52 + 12, 0x40000000), (24, 8), (0x104, 11),
                              (84 + 20, 0x10001), (84 + 24, 7)):
            broken = bytearray(self.elf_data)
            struct.pack_into("<I", broken, offset, value)
            self.write(self.elf, broken)
            with self.subTest(offset=offset, value=value):
                self.assert_failed()
        self.set_image(overlay.artifact.FLASH_LIMIT + 1, 0)
        self.assert_failed()
        self.write(self.elf, b"not an ELF")
        self.assert_failed()

    def test_valid_sram_profile_never_becomes_a_flash_overlay(self):
        elf_data = bytes(synthetic_elf(ram_only=True))
        text = (hex_record(4, 0, b"\x20\x00") +
                hex_record(0, 0, elf_data[0x100:0x120]) + hex_record(1))
        image = overlay.artifact.compare(elf_data, text, ram_only=True)
        self.assertEqual(image.flash_extent, 0)
        with self.assertRaises(ValueError):
            overlay.overlay_pages(self.original, image.memory)
        self.write(self.elf, elf_data)
        self.write(self.hex, text.encode("ascii"))
        self.assert_failed()

    def test_private_modes_and_ownership(self):
        for path in (self.first / "main-flash.bin", self.second / "uicr.bin", self.elf, self.hex):
            for mode in (0o000, 0o200, 0o640, 0o644, 0o700):
                path.chmod(mode)
                with self.subTest(path=path, mode=mode):
                    self.assert_failed()
            path.chmod(0o600)
        for path in (self.first, self.second, self.stage, self.root):
            path.chmod(0o755)
            self.assert_failed()
            path.chmod(0o700)
        with patch.object(private_artifacts.os, "getuid", return_value=os.getuid() + 1):
            self.assert_failed()

    def test_input_aliases_symlinks_and_nonregular_files(self):
        old_first, old_elf = self.first, self.elf
        self.first = self.second
        self.assert_failed()
        self.first = old_first
        for path in (self.hex, self.first / "main-flash.bin"):
            self.elf = path
            self.assert_failed()
        self.elf = old_elf
        self.elf.unlink()
        self.elf.symlink_to(self.hex)
        self.assert_failed()
        self.elf.unlink()
        os.link(self.hex, self.elf)
        self.assert_failed()
        self.elf.unlink()
        self.elf.mkdir(mode=0o700)
        self.assert_failed()
        self.elf.rmdir()
        os.mkfifo(self.elf, mode=0o600)
        self.assert_failed()

    def test_path_policy_rejects_repository_relative_parent_and_directory_aliases(self):
        old = self.first
        alias = self.root / "alias"
        alias.symlink_to(self.first, target_is_directory=True)
        for path in (alias, Path("relative"), self.first / ".." / "first", private_artifacts.ROOT):
            self.first = path
            self.assert_failed()
        self.first = old
        self.report = private_artifacts.ROOT / "overlay-must-not-exist.json"
        self.assert_failed()

    def test_short_reads_and_premature_eof(self):
        read = os.read
        with patch.object(overlay.os, "read", side_effect=lambda fd, n: read(fd, min(n, 701))):
            self.assertEqual(self.invoke()[0], 0)
        self.report.unlink()
        with patch.object(overlay.os, "read", return_value=b""):
            self.assert_failed()

    def test_mutation_during_actual_read(self):
        read, changed = os.read, False

        def mutate(fd, count):
            nonlocal changed
            result = read(fd, count)
            if not changed:
                changed = True
                (self.first / "main-flash.bin").chmod(0o400)
            return result

        with patch.object(overlay.os, "read", side_effect=mutate):
            self.assert_failed()

    def test_mutations_after_reading_each_input_are_rejected(self):
        real = overlay.overlay_pages
        paths = [d / n for d in (self.first, self.second) for n, _, _ in recovery.REGIONS]
        paths += [self.elf, self.hex]
        for path in paths:
            def mutate(original, memory):
                result = real(original, memory)
                path.chmod(0o400)
                return result
            with self.subTest(path=path), patch.object(overlay, "overlay_pages", side_effect=mutate):
                self.assert_failed()
            path.chmod(0o600)

    def test_late_capture_byte_mutation_and_same_byte_rewrite_are_rejected(self):
        real = overlay.overlay_pages
        path = self.first / "main-flash.bin"
        for value in (self.original[-1] ^ 1, self.original[-1]):
            def mutate(original, memory):
                result = real(original, memory)
                with path.open("r+b") as stream:
                    stream.seek(-1, os.SEEK_END)
                    stream.write(bytes((value,)))
                return result
            with self.subTest(value=value), patch.object(overlay, "overlay_pages", side_effect=mutate):
                self.assert_failed()
            self.write(path, self.original)

    def test_replaced_file_and_directory_after_parsing_are_rejected(self):
        real = overlay.artifact.compare

        def replace_file(*args):
            result = real(*args)
            self.elf.rename(self.stage / "old.elf")
            self.write(self.elf, self.elf_data)
            return result

        with patch.object(overlay.artifact, "compare", side_effect=replace_file):
            self.assert_failed()

        def replace_directory(*args):
            result = real(*args)
            self.stage.rename(self.root / "old-stage")
            self.stage.mkdir(mode=0o700)
            self.set_image()
            return result

        with patch.object(overlay.artifact, "compare", side_effect=replace_directory):
            self.assert_failed()

    def test_existing_or_aliased_report_is_never_overwritten(self):
        self.assertEqual(self.invoke()[0], 0)
        receipt = self.report.read_bytes()
        self.assertEqual(self.invoke()[0:2], (1, ""))
        self.assertEqual(self.report.read_bytes(), receipt)
        self.report.unlink()
        self.report.symlink_to(self.elf)
        self.assertEqual(self.invoke()[0:2], (1, ""))
        self.assertEqual(self.elf.read_bytes(), self.elf_data)
        self.report.unlink()
        os.link(self.elf, self.report)
        self.assertEqual(self.invoke()[0:2], (1, ""))
        self.assertEqual(self.elf.read_bytes(), self.elf_data)
        self.report.unlink()
        self.report = self.elf
        self.assertEqual(self.invoke()[0:2], (1, ""))
        self.assertEqual(self.elf.read_bytes(), self.elf_data)

    def test_file_and_late_directory_fsync_failures_never_succeed(self):
        real = os.fsync
        for fail_at in (1, 2):
            calls = 0

            def failing(fd):
                nonlocal calls
                calls += 1
                if calls == fail_at:
                    raise OSError(5, "private-identity-do-not-print")
                real(fd)

            with self.subTest(fail_at=fail_at), patch.object(overlay.os, "fsync", side_effect=failing):
                self.assert_failed()
            self.assertGreater(calls, fail_at)

    def test_input_or_report_mutation_during_durability_is_rejected(self):
        real = os.fsync
        for target, mode in ((self.second / "main-flash.bin", 0o400),
                             (self.report, 0o644), (self.report, 0o400)):
            calls = 0

            def mutate(fd):
                nonlocal calls
                real(fd)
                calls += 1
                if calls == 2:
                    target.chmod(mode)

            with self.subTest(target=target, mode=mode), \
                    patch.object(overlay.os, "fsync", side_effect=mutate):
                self.assert_failed()
            if target != self.report:
                target.chmod(0o600)

    def test_report_bytes_modified_during_fsync_are_rejected(self):
        real, calls = os.fsync, 0

        def mutate(fd):
            nonlocal calls
            real(fd)
            calls += 1
            if calls == 2:
                with self.report.open("r+b") as stream:
                    stream.seek(-2, os.SEEK_END)
                    stream.write(b" ")

        with patch.object(overlay.os, "fsync", side_effect=mutate):
            self.assert_failed()

    def test_partial_write_close_and_cleanup_durability_errors(self):
        create = overlay.private_capture

        @contextmanager
        def partial(path):
            with create(path) as stream:
                write = stream.write
                stream.write = lambda text: write(text[:-1])
                yield stream

        @contextmanager
        def close_failure(path):
            with create(path) as stream:
                yield stream
            raise OSError(5, "synthetic close failure")

        for failure in (partial, close_failure):
            with patch.object(overlay, "private_capture", side_effect=failure):
                self.assert_failed()
        with patch.object(overlay.os, "fsync", side_effect=OSError(5, "synthetic persistent failure")):
            result, output, error = self.invoke()
            self.assertEqual((result, output), (1, ""))
            self.assertIn("cleanup or its durability is uncertain", error)
            self.assertFalse(self.report.exists())

    def test_output_unlink_failure_is_explicit_and_never_reports_success(self):
        with patch.object(overlay.os, "fsync", side_effect=OSError(5, "synthetic failure")), \
                patch.object(overlay.os, "unlink", side_effect=OSError(13, "synthetic cleanup failure")):
            result, output, error = self.invoke()
        self.assertEqual((result, output), (1, ""))
        self.assertIn("cleanup or its durability is uncertain", error)
        self.assertTrue(self.report.exists())
        self.assertTrue(all(json.loads(self.report.read_bytes())[name] is False for name in CLAIMS))

    def test_output_replacement_is_retained_not_deleted_by_failure_cleanup(self):
        real, changed = os.fsync, False
        foreign = b'{"synthetic_foreign_receipt":true}\n'

        def replace(fd):
            nonlocal changed
            real(fd)
            if not changed:
                changed = True
                self.report.rename(self.root / "moved-report.json")
                self.write(self.report, foreign)

        with patch.object(overlay.os, "fsync", side_effect=replace):
            self.assertEqual(self.invoke()[0:2], (1, ""))
        self.assertEqual(self.report.read_bytes(), foreign)

    def test_report_bound_and_argument_errors_are_private(self):
        with patch.object(overlay, "REPORT_LIMIT", 16):
            self.assert_failed()
        for arguments in ([], self.arguments() + ["--execute-read", str(self.root)],
                          self.arguments() + ["--private-" + self.elf.name],
                          ["--fir", str(self.first)]):
            result, output, error = self.invoke(arguments)
            self.assertEqual((result, output), (1, ""))
            self.assertIn("no report accepted", error)

    def test_shared_reader_reexport_is_unchanged(self):
        import nrf_acquire
        self.assertIs(nrf_acquire.private_bytes, recovery.private_bytes)
        self.assertEqual(recovery.private_bytes(self.elf, overlay.ELF_LIMIT), self.elf_data)

    def test_real_cli_is_confined_to_artifacts_without_device_or_build_backends(self):
        tools = Path(__file__).resolve().parent
        script = r"""
import os, runpy, sys
def audit(event, args):
    if event in ("subprocess.Popen", "os.system", "os.exec", "socket.__new__", "ctypes.dlopen"):
        raise AssertionError("process/device backend is forbidden")
    if event == "open" and isinstance(args[0], str):
        if args[0].startswith(("/dev/", "/sys/", "/proc/")):
            raise AssertionError("device/probe filesystem access is forbidden")
sys.addaudithook(audit)
tool = sys.argv.pop(1)
sys.path.insert(0, os.path.dirname(tool))
try:
    runpy.run_path(tool, run_name="__main__")
except SystemExit:
    assert not {"serial", "usb", "pynrfjprog", "nrf_acquire", "nrf_stimulus.verify_build"} & sys.modules.keys()
    raise
"""
        result = subprocess.run([sys.executable, "-B", "-c", script,
                                 str(tools / "nrf_overlay.py"), *self.arguments()],
                                cwd=self.root, capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")
        self.assertIn("NOT authorized", result.stdout)
        self.assertNotIn(str(self.root), result.stdout)


if __name__ == "__main__":
    unittest.main()
