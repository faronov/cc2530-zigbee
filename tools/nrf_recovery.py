#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Compare private nRF52840 artifacts offline, never read or program a device."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import stat
import sys

from private_artifacts import private_capture, private_directory
from verify_firmware import require


REGIONS = (
    ("main-flash.bin", 0x00000000, 0x100000),
    ("uicr.bin", 0x10001000, 0x1000),
)


def _fingerprint(info):
    return (info.st_dev, info.st_ino, info.st_mode, info.st_uid, info.st_nlink,
            info.st_size, info.st_mtime_ns, info.st_ctime_ns)


def _validate_file(info, name, size):
    require(stat.S_ISREG(info.st_mode), f"{name} must be a regular data file")
    require(info.st_uid == os.getuid() and stat.S_IMODE(info.st_mode) in (0o400, 0o600)
            and info.st_nlink == 1, f"{name} must be user-owned, private and single-link")
    require(info.st_size == size, f"{name} must contain exactly {size} bytes")


def _read_capture(directory, name, size):
    before = os.stat(name, dir_fd=directory, follow_symlinks=False)
    _validate_file(before, name, size)
    fd = os.open(name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=directory)
    try:
        require(_fingerprint(os.fstat(fd)) == _fingerprint(before),
                f"{name} changed before opening")
        data = bytearray()
        while len(data) < size:
            chunk = os.read(fd, min(65536, size - len(data)))
            require(chunk, f"{name} ended before its declared extent")
            data.extend(chunk)
        require(not os.read(fd, 1), f"{name} grew beyond its declared extent")
        require(_fingerprint(os.fstat(fd)) == _fingerprint(before),
                f"{name} changed while reading")
        return bytes(data), (before.st_dev, before.st_ino)
    finally:
        os.close(fd)


def private_bytes(path, limit):
    path = Path(path)
    with private_directory(path.parent) as directory:
        info = os.stat(path.name, dir_fd=directory, follow_symlinks=False)
        require(0 < info.st_size <= limit, "Private input has an invalid size")
        try:
            return _read_capture(directory, path.name, info.st_size)[0]
        except ValueError as error:
            raise ValueError(str(error).replace(path.name, "private input")) from error


def compare_captures(first, second):
    """Check exact file agreement, not independent acquisition or physical origin.

    The caller must exclusively own both artifact directories. Even matching
    all-FF files do not establish a readable target, usable firmware or recovery.
    """
    regions, identities = [], set()
    with private_directory(first) as left, private_directory(second) as right:
        left_info, right_info = os.fstat(left), os.fstat(right)
        require((left_info.st_dev, left_info.st_ino) != (right_info.st_dev, right_info.st_ino),
                "Two distinct capture directories are required")
        for name, address, size in REGIONS:
            a, a_id = _read_capture(left, name, size)
            b, b_id = _read_capture(right, name, size)
            require(a_id != b_id and a_id not in identities and b_id not in identities,
                    "Capture inputs must be distinct files")
            identities.update((a_id, b_id))
            require(a == b, f"{name} captures differ")
            regions.append({"name": name, "address": address, "bytes": size,
                            "sha256": hashlib.sha256(a).hexdigest()})
    return {
        "schema": "nrf52840-artifact-agreement-v1",
        "evidence": "offline-file-agreement-only",
        "geometry": "assumed-nrf52840-not-device-detected",
        "regions": regions,
        "physical_origin_verified": False,
        "independent_acquisition_verified": False,
        "firmware_execution_verified": False,
        "debug_access_verified": False,
        "restoration_verified": False,
        "authorizes_programming": False,
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--first", type=Path, required=True, help="First private 0700 capture directory")
    parser.add_argument("--second", type=Path, required=True, help="Second private 0700 capture directory")
    parser.add_argument("--report", type=Path, required=True, help="New private 0600 JSON report outside Git")
    args = parser.parse_args(argv)
    try:
        report = compare_captures(args.first, args.second)
        with private_capture(args.report) as output:
            output.write(json.dumps(report, sort_keys=True, indent=2) + "\n")
            output.flush()
            os.fsync(output.fileno())
    except ValueError as error:
        print(f"nrf-recovery: {error}", file=sys.stderr)
        return 1
    except OSError as error:
        print(f"nrf-recovery: private file operation failed (errno {error.errno})", file=sys.stderr)
        return 1
    print("Artifact pairs agree; physical origin and restoration are NOT verified. No programming authorized.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
