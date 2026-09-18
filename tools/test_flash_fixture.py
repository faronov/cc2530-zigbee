# SPDX-License-Identifier: BSD-3-Clause
"""Synthetic ABI, publication guards and genuine per-link snapshot regressions."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from flash_fixture import decode, packet, verify_fixture, verify_listings
import test_m0_artifacts
from verify_firmware import BOARDS, IMAGES, ROOT, parse_ihex, parse_symbols, verify_layout


def record(phase=1, reason=0, page=255, step=0, result=255, checks=0, remaining=256):
    return (b"M2FL\x01\x10"+bytes((phase,reason,page,step,result,checks))+
            remaining.to_bytes(2,"little")+b"\x69\x96")


class FlashFixtureTests(unittest.TestCase):
    def test_packet_pure_bytes_and_wide_rejections(self):
        for page in range(2):
            self.assertEqual(packet("arm",page), bytes((166,89,page,255-page,60,195,105,150)))
            self.assertEqual(packet("run",page), bytes((89,166,page,255-page,195,60,105,150)))
        for page in (-1,2,125,126,127,256,65536,True,1.0,None):
            for stage in ("arm","run"):
                with self.assertRaises(ValueError): packet(stage,page)
        for stage in ("erase",1,None,"ARM"):
            with self.assertRaises(ValueError): packet(stage,0)

    def test_abi_valid_progress_pending_and_terminal(self):
        records = [record(), record(2,page=0), record(5,1,remaining=0),
                   record(5,2), record(4,page=1,step=5,result=0,checks=3,remaining=0)]
        for step, result in enumerate((255,4,0,0,5)):
            for pending in (False,True):
                records.append(record(3,page=0,step=step,result=10 if pending else result,
                                      checks=0 if step == 0 else 1 if step <= 3 else 3,remaining=0))
        records += [record(5,4,0,1,8,1,0),record(5,3,0,0,6,0,0)]
        for raw in records: self.assertEqual(decode(raw)["phase"],raw[6])

    def test_abi_invalid_bounds_progress_and_end(self):
        raw = record()
        for offset, value in ((0,0),(4,2),(5,15),(6,0),(6,6),(7,5),(8,2),(9,6),
                              (10,11),(11,4),(13,2),(14,0),(15,0),(6,4),(6,3),(7,1)):
            changed = bytearray(raw); changed[offset] = value
            with self.subTest(offset=offset,value=value), self.assertRaises(ValueError): decode(bytes(changed))
        for raw in (b"",bytes(16),record()+b"\0",bytearray(record()),record(4,page=0,step=4,result=0,checks=3,remaining=0)):
            with self.assertRaises(ValueError): decode(raw)

    def test_test_executables_and_native_models_never_board_inputs(self):
        base = test_m0_artifacts.LayoutTests(); base.setUp()
        for image in IMAGES:
            for name in ("_flash_test_result","_flash_exec_test_result","_flash_write_test_result",
                         "_flash_exec_host_enter","_host_flash_engine_stop"):
                with self.assertRaisesRegex(ValueError,"isolated flash"):
                    verify_layout(base.symbols | {name:0},base.memory,base.debug,image)
            for source in ("test_flash.c","test_flash_exec.c","test_flash_write.c","test_flash_fixture.c","host_flash_engine.c"):
                with self.assertRaisesRegex(ValueError,"isolated flash"):
                    verify_layout(base.symbols,base.memory,base.debug+f"\nC${source}$1",image)

    def test_fixture_budget_does_not_relax_components_or_alias(self):
        base = test_m0_artifacts.LayoutTests(); base.setUp()
        debug = base.debug.replace("bringup.c","flash_fixture.c")
        for size in (436,448):
            self.assertEqual(verify_layout(base.symbols | {"l_XSEG":size},base.memory,debug,"flash_fixture")
                             ["nonaliased_xdata_reserved_bytes"],size+64)
        for size in (449,0x1e01,0x1f00):
            with self.assertRaises(ValueError):
                verify_layout(base.symbols | {"l_XSEG":size},base.memory,debug,"flash_fixture")


@unittest.skipUnless(shutil.which("sdcc") and shutil.which("make"), "SDCC/Make unavailable")
class SnapshotTests(unittest.TestCase):
    def test_shared_listing_overwrite_never_replaces_linked_proof(self):
        for board in BOARDS:
            with self.subTest(board=board), tempfile.TemporaryDirectory(prefix="cc2530-flash-snapshot-") as directory:
                output = Path(directory)
                argv = ["make","--no-print-directory","-j1",f"BOARD={board}","IMAGE=flash_fixture",f"BUILD={output}"]
                def build(name):
                    result = subprocess.run(argv+[str(output/(name+".ihx"))],cwd=ROOT,
                                            capture_output=True,text=True,timeout=120)
                    self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                build("flash_fixture")
                path = output/"flash_fixture"
                image = parse_ihex(path.with_suffix(".ihx").read_text())
                verify_fixture(image,parse_symbols(path.with_suffix(".map").read_text()),path.with_suffix(".cdb").read_text())
                verify_listings(output,image)
                saved = {p:p.read_bytes() for p in output.glob("flash_fixture.*.rst")}
                build("flash_write_test"); build("flash_exec_test"); build("flash_test")
                verify_listings(output,image)
                self.assertEqual({p:p.read_bytes() for p in saved},saved)
                for name in ("flash_test.reader.rst","flash_exec_test.exec.rst",
                             "flash_write-exec.rst","flash_write-reader.rst","flash_write-service.rst"):
                    self.assertTrue((output/name).is_file())
                for p, original in saved.items():
                    text = original.decode()
                    import re
                    match = re.search(r"^(\s+[0-9A-F]{6} )([0-9A-F]{2})(?= [0-9A-F ])",text,re.M)
                    self.assertIsNotNone(match)
                    p.write_text(text[:match.start(2)]+f"{int(match[2],16)^1:02X}"+text[match.end(2):])
                    with self.assertRaises(ValueError): verify_listings(output,image)
                    p.write_bytes(original)


if __name__ == "__main__":
    unittest.main()
