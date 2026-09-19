# SPDX-License-Identifier: BSD-3-Clause
"""Private artifact paths and exclusive creation; no device access."""
from contextlib import contextmanager
import os
from pathlib import Path
import stat

from verify_firmware import ROOT, require


def private_path(path):
    path = Path(path)
    require(path.is_absolute() and path.name not in ("", ".", "..") and ".." not in path.parts,
            "Capture requires an absolute private path")
    try:
        resolved = path.resolve()
    except RuntimeError as error:
        raise ValueError("Capture path contains a symlink loop") from error
    require(not resolved.is_relative_to(ROOT.resolve()), "Capture must be outside the repository")
    return path


@contextmanager
def private_directory(path):
    """Hold a user-owned 0700 directory without following any symlink component."""
    path = private_path(path)
    flags = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW
    directory = os.open(path.anchor, flags)
    try:
        for part in path.parts[1:]:
            child = os.open(part, flags, dir_fd=directory)
            os.close(directory)
            directory = child
        info = os.fstat(directory)
        require(info.st_uid == os.getuid() and stat.S_IMODE(info.st_mode) == 0o700,
                "Capture directory must be user-owned mode 0700")
        yield directory
    finally:
        os.close(directory)


def private_capture(path):
    """Create a new single-link 0600 text file outside the repository; never overwrite."""
    path = private_path(path)
    with private_directory(path.parent) as directory:
        fd = os.open(path.name, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW,
                     0o600, dir_fd=directory)
        try:
            info = os.fstat(fd)
            require(stat.S_ISREG(info.st_mode) and info.st_uid == os.getuid() and
                    stat.S_IMODE(info.st_mode) == 0o600 and info.st_nlink == 1,
                    "Capture file must be new, regular, single-link mode 0600")
            return os.fdopen(fd, "w", encoding="ascii")
        except BaseException:
            os.close(fd)
            raise
