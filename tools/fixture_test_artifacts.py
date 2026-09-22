# SPDX-License-Identifier: BSD-3-Clause
"""Fresh process-local artifacts for offline linked-metadata regressions."""
import atexit
from functools import cache
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from verify_firmware import BOARDS, ROOT, require


@cache
def linked_fixture(board, image):
    require(board in BOARDS and image in (
        "clock_test", "clock_fixture", "radio_tx_fixture", "radio_fifo_fixture",
        "dma_fixture", "aes_fixture", "prng_fixture", "radio_rx_fixture",
        "radio_noise_fixture", "radio_link_fixture",
    ), "Unknown linked regression fixture")
    if not all(shutil.which(tool) for tool in ("make", "sdcc", "packihx", "makebin")):
        raise unittest.SkipTest("SDCC build tools required for linked metadata regressions")
    temporary = tempfile.TemporaryDirectory(prefix="cc2530-linked-metadata-")
    atexit.register(temporary.cleanup)
    output = Path(temporary.name)
    target = str(output / "clock_test.ihx") if image == "clock_test" else "all"
    selected = "bringup" if image == "clock_test" else image
    result = subprocess.run(
        ["make", "--no-print-directory", "--jobs=1", "-s", f"BOARD={board}",
         f"IMAGE={selected}", f"BUILD={output}", target],
        cwd=ROOT, capture_output=True, text=True, timeout=120,
    )
    require(result.returncode == 0,
            f"Linked regression build failed for {board}/{image}:\n{result.stdout}{result.stderr}")
    return output / image
