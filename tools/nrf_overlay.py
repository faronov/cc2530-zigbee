#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Private offline artifact overlay, never a writer or target/permission evaluator.

Inputs are two complete capture directories and privately staged ELF/HEX files.
Only a new bounded JSON report is written; no image binary or commands are emitted.
All paths must be outside Git, with user-owned 0700 directories and single-link
0400/0600 input files. Trusted exclusive local artifact ownership is required:
matching copied or fabricated captures cannot establish physical origin, ACL
readability, an atomic snapshot, recovery usability or programming permission.
The strict image parser checks artifacts, not firmware startup or restoration.
"""
import argparse
from contextlib import ExitStack
import hashlib
import json
import os
from pathlib import Path
import stat
import sys

from nrf_recovery import (
    REGIONS, _fingerprint, _read_capture, _validate_file, compare_captures, private_bytes,
)
from nrf_stimulus import artifact
from private_artifacts import private_capture, private_directory, private_path
from verify_firmware import require


FLASH_NAME, FLASH_ADDRESS, FLASH_BYTES = REGIONS[0]
UICR_NAME, UICR_ADDRESS, UICR_BYTES = REGIONS[1]
PAGE_BYTES = 4096
ELF_LIMIT = 16 * 1024 * 1024
HEX_LIMIT = 4 * 1024 * 1024
REPORT_LIMIT = 128 * 1024
SCHEMA = "nrf52840-artifact-overlay-v1"


class ReportCleanupError(ValueError):
    """The failed report's cleanup or cleanup durability is uncertain."""


def _validate_overlay(original, memory):
    require(type(original) is bytes and len(original) == FLASH_BYTES,
            "Overlay requires the complete immutable main-flash bytes")
    require(type(memory) is dict and 0 < len(memory) <= artifact.FLASH_LIMIT,
            "Overlay requires bounded explicit image bytes")
    require(all(type(a) is int and FLASH_ADDRESS <= a < artifact.FLASH_LIMIT
                and type(v) is int and 0 <= v <= 255 for a, v in memory.items()),
            "Image byte or address is outside the helper main-flash profile")


def describe_overlay(original, overlaid, memory):
    """Check every byte, including holes and untouched pages; return hashes only."""
    _validate_overlay(original, memory)
    require(type(overlaid) is bytes and len(overlaid) == FLASH_BYTES,
            "Overlay must retain the complete main-flash extent")
    pages, unchanged_before, unchanged_after = [], hashlib.sha256(), hashlib.sha256()
    changed_total = 0
    for start in range(FLASH_ADDRESS, FLASH_ADDRESS + FLASH_BYTES, PAGE_BYTES):
        before = original[start:start + PAGE_BYTES]
        after = overlaid[start:start + PAGE_BYTES]
        covered = changed = 0
        for offset, (old, new) in enumerate(zip(before, after)):
            address = start + offset
            if address in memory:
                require(new == memory[address], "Overlay differs from an explicit image byte")
                covered += 1
            else:
                require(new == old, "Overlay changed a byte outside image coverage")
            changed += old != new
        if covered:
            pages.append({
                "index": start // PAGE_BYTES, "address": start, "bytes": PAGE_BYTES,
                "image_covered_bytes": covered, "preserved_bytes": PAGE_BYTES - covered,
                "changed_bytes": changed, "before_sha256": artifact.digest(before),
                "overlay_sha256": artifact.digest(after),
            })
        else:
            unchanged_before.update(before)
            unchanged_after.update(after)
        changed_total += changed
    end = max(memory) + 1
    untouched = FLASH_BYTES // PAGE_BYTES - len(pages)
    return {
        "address": FLASH_ADDRESS, "bytes": FLASH_BYTES, "page_bytes": PAGE_BYTES,
        "image_start": min(memory), "image_end_exclusive": end,
        "image_covered_bytes": len(memory), "preserved_bytes": FLASH_BYTES - len(memory),
        "changed_bytes": changed_total, "covered_page_count": len(pages),
        "covered_page_preserved_bytes": len(pages) * PAGE_BYTES - len(memory),
        "last_page_tail_bytes": (-end) % PAGE_BYTES,
        "before_sha256": artifact.digest(original), "overlay_sha256": artifact.digest(overlaid),
        "outside_image_unchanged": True, "pages": pages,
        "unaffected_pages": {
            "count": untouched, "bytes": untouched * PAGE_BYTES, "unchanged": True,
            "before_sha256": unchanged_before.hexdigest(),
            "overlay_sha256": unchanged_after.hexdigest(),
        },
    }


def overlay_pages(original, memory):
    """Pure byte-overlay arithmetic; unaligned/sparse coverage is not a write plan."""
    _validate_overlay(original, memory)
    overlaid = bytearray(original)
    for address, value in memory.items():
        overlaid[address] = value
    return describe_overlay(original, bytes(overlaid), memory)


def _directory_identity(fd):
    info = os.fstat(fd)
    return info.st_dev, info.st_ino, info.st_mode, info.st_uid


def _write_report(path, directory, report, check_inputs):
    encoded = json.dumps(report, sort_keys=True, indent=2) + "\n"
    require(len(encoded.encode("ascii")) <= REPORT_LIMIT, "Overlay report exceeded its bound")
    identity, held = None, None
    try:
        with private_capture(path) as stream:
            info = os.fstat(stream.fileno())
            identity = info.st_dev, info.st_ino
            # Pin the created inode through cleanup, including after stream.close().
            held = os.dup(stream.fileno())
            bound = os.stat(path.name, dir_fd=directory, follow_symlinks=False)
            require((bound.st_dev, bound.st_ino) == identity, "Report directory changed")
            require(stream.write(encoded) == len(encoded), "Incomplete report write")
            stream.flush()
            info = os.fstat(stream.fileno())
            _validate_file(info, "Private report", len(encoded))
            require(stat.S_IMODE(info.st_mode) == 0o600, "Report must remain mode 0600")
            fingerprint = _fingerprint(info)
            os.fsync(stream.fileno())
        os.fsync(directory)
        data, current = _read_capture(directory, path.name, len(encoded))
        require(current == identity and data == encoded.encode("ascii"), "Report changed")
        check_inputs()
        require(_fingerprint(os.fstat(held)) == fingerprint and
                _fingerprint(os.stat(path.name, dir_fd=directory, follow_symlinks=False)) == fingerprint,
                "Report changed during completion")
    except (OSError, ValueError):
        if identity is not None:
            try:
                current = os.stat(path.name, dir_fd=directory, follow_symlinks=False)
                require((current.st_dev, current.st_ino) == identity,
                        "Failed report was replaced; cleanup refused")
                os.unlink(path.name, dir_fd=directory)
                os.fsync(directory)
            except (OSError, ValueError) as error:
                raise ReportCleanupError(
                    "Report failed; cleanup or its durability could not be verified") from error
        raise
    finally:
        if held is not None:
            os.close(held)


def plan_report(first, second, elf_path, hex_path, report_path):
    """Compare bounded private artifacts and persist only an offline hash report.

    A zero CLI exit is required for report acceptance. Failed/interrupted attempts
    confer no validity; cleanup failures are explicit, not successful fallbacks.
    """
    first, second, elf_path, hex_path = map(Path, (first, second, elf_path, hex_path))
    report_path = private_path(report_path)
    inputs = [(directory / name, size, size)
              for directory in (first, second) for name, _, size in REGIONS]
    inputs += [(elf_path, None, ELF_LIMIT), (hex_path, None, HEX_LIMIT)]
    with ExitStack() as stack:
        directories = {}
        for path in [p.parent for p, _, _ in inputs] + [report_path.parent]:
            if path not in directories:
                directories[path] = stack.enter_context(private_directory(path))
        directory_ids = {p: _directory_identity(fd) for p, fd in directories.items()}
        identities, fingerprints = set(), {}
        for path, size, limit in inputs:
            info = os.stat(path.name, dir_fd=directories[path.parent], follow_symlinks=False)
            _validate_file(info, "Private input", info.st_size if size is None else size)
            require(0 < info.st_size <= limit, "Private input exceeds its size bound")
            identity = info.st_dev, info.st_ino
            require(identity not in identities, "All input files must be distinct")
            identities.add(identity)
            fingerprints[path] = _fingerprint(info)

        def check_inputs():
            for path, identity in directory_ids.items():
                with private_directory(path) as current:
                    require(_directory_identity(current) == identity, "Private directory changed")
            for path, expected in fingerprints.items():
                info = os.stat(path.name, dir_fd=directories[path.parent], follow_symlinks=False)
                require(_fingerprint(info) == expected, "Private input changed during planning")

        agreement = compare_captures(first, second)
        original, _ = _read_capture(directories[first], FLASH_NAME, FLASH_BYTES)
        uicr, _ = _read_capture(directories[first], UICR_NAME, UICR_BYTES)
        require([artifact.digest(original), artifact.digest(uicr)] ==
                [r["sha256"] for r in agreement["regions"]], "Capture changed after comparison")
        elf_bytes = private_bytes(elf_path, ELF_LIMIT)
        hex_bytes = private_bytes(hex_path, HEX_LIMIT)
        image = artifact.compare(elf_bytes, hex_bytes.decode("ascii"))
        report = {
            "schema": SCHEMA, "evidence": "offline-artifact-overlay-only",
            "geometry": "assumed-nrf52840-not-device-detected", "agreement": agreement,
            "image": {
                "evidence": "strict-elf-hex-artifact-comparison-only",
                "elf_bytes": len(elf_bytes), "elf_sha256": artifact.digest(elf_bytes),
                "hex_bytes": len(hex_bytes), "hex_sha256": artifact.digest(hex_bytes),
                "entry": image.entry, "flash_extent": image.flash_extent,
                "flash_load_bytes": len(image.memory), "sram_allocated": image.sram_allocated,
                "sram_extent": image.sram_extent, "flash_limit": artifact.FLASH_LIMIT,
                "allocated_sram_limit": artifact.RAM_LIMIT,
            },
            "overlay": overlay_pages(original, image.memory),
            "uicr": {
                "address": UICR_ADDRESS, "bytes": UICR_BYTES, "excluded": True,
                "unchanged": True, "before_sha256": artifact.digest(uicr),
                "overlay_sha256": artifact.digest(uicr),
            },
            "physical_origin_verified": False, "independent_acquisition_verified": False,
            "acl_readability_verified": False, "atomic_snapshot_verified": False,
            "recovery_material_verified": False, "silicon_compatibility_verified": False,
            "firmware_startup_verified": False, "firmware_execution_verified": False,
            "debug_access_verified": False, "restoration_verified": False,
            "authorizes_programming": False,
        }
        check_inputs()
        _write_report(report_path, directories[report_path.parent], report, check_inputs)
        return report


class _Parser(argparse.ArgumentParser):
    def error(self, message):
        raise ValueError("Invalid planner arguments")


def main(argv=None):
    parser = _Parser(prog="nrf-overlay", description=__doc__, allow_abbrev=False)
    parser.add_argument("--first", required=True, type=Path, help="First private full-capture directory")
    parser.add_argument("--second", required=True, type=Path, help="Second private full-capture directory")
    parser.add_argument("--elf", required=True, type=Path, help="Privately staged bounded ELF")
    parser.add_argument("--hex", required=True, type=Path, help="Privately staged canonical HEX")
    parser.add_argument("--report", required=True, type=Path, help="New private 0600 JSON report")
    try:
        args = parser.parse_args(argv)
        plan_report(args.first, args.second, args.elf, args.hex, args.report)
    except ReportCleanupError:
        print("nrf-overlay: report failed; cleanup or its durability is uncertain; no report accepted.",
              file=sys.stderr)
        return 1
    except ValueError:
        print("nrf-overlay: invalid private artifacts, arguments or report; no report accepted.",
              file=sys.stderr)
        return 1
    except OSError as error:
        print(f"nrf-overlay: private file operation failed (errno {error.errno}); no report accepted.",
              file=sys.stderr)
        return 1
    print("Offline artifact overlay report created. Physical recovery and programming are NOT authorized.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
