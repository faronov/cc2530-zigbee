#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Bounded offline negatives for the separate physical smoke image gate."""
import argparse
import copy
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tests"))
import verify_mac_smoke as verify
from banked_image import pack


class SmokeGate:
    # Deliberately not a discovery TestCase: these tests need an explicitly
    # built exact image. Ordinary test-tools imports no image and builds none.
    def reject(self, changed):
        with self.assertRaises(ValueError):
            verify.verify(changed, BOARD)

    def test_genuine(self):
        verify.verify(ARTIFACTS, BOARD)

    def test_startup_stop_is_checked_before_using_a_snapshot(self):
        import boot_mac_smoke
        for text in ("CPU state= OK PC= 0x0000", "CPU state= ERROR PC= 0x1234", ""):
            with patch.object(boot_mac_smoke, "captured_sections") as snapshot:
                with self.assertRaises(ValueError):
                    boot_mac_smoke.check_boot({59990:text},{"_main":0x1234},0x5678)
                snapshot.assert_not_called()

    def test_code(self):
        for offset in (0,0x100,min(a for a in ARTIFACTS[0] if a>=0x18000),max(ARTIFACTS[0])):
            changed=copy.deepcopy(ARTIFACTS); changed[0][offset]^=1
            self.reject(changed)

    def test_complete_raw_cdb(self):
        raw=ARTIFACTS[2]
        for replacement in (raw+b"\0",raw.replace(b"\n",b"\r\n"),raw[1:],
                            raw.replace(b"mac_smoke_mailbox",b"mac_smoke_mailboX"),raw+b"EXTRA METADATA\n"):
            changed=list(ARTIFACTS); changed[2]=replacement; self.reject(changed)

    def test_complete_map_memory(self):
        for index in (1,3):
            for raw in (ARTIFACTS[index]+b"\0",ARTIFACTS[index][1:],ARTIFACTS[index].replace(b"\n",b"\r\n")):
                changed=list(ARTIFACTS); changed[index]=raw; self.reject(changed)

    def test_missing_swapped_object_listing(self):
        for index in (4,5):
            for m in verify.modules(BOARD):
                changed=copy.deepcopy(ARTIFACTS); del changed[index][m]; self.reject(changed)
            changed=copy.deepcopy(ARTIFACTS)
            changed[index]["clock"],changed[index]["timebase"]=changed[index]["timebase"],changed[index]["clock"]
            self.reject(changed)

    def test_full_listings_objects(self):
        for index in (4,5):
            for m in verify.modules(BOARD):
                changed=copy.deepcopy(ARTIFACTS); changed[index][m]+=b"\0"; self.reject(changed)

    def test_moved_overlap_stack_status(self):
        for old,new in ((b"00001E00",b"00001F00"),(b"00000056",b"00000057"),
                        (b"00018000",b"00028000"),(b"0000004C",b"00000008")):
            changed=list(ARTIFACTS)
            self.assertIn(old,changed[1]); changed[1]=changed[1].replace(old,new); self.reject(changed)

    def test_reserved_physical(self):
        for address in (0x78000+0x6800,0x8000,0x88000):
            with self.assertRaises(ValueError): pack({address:0})

    def test_output_comment_only(self):
        changed=copy.deepcopy(ARTIFACTS)
        for m in verify.modules(BOARD):
            raw=changed[5][m]
            if raw.startswith(b";!FILE "):
                changed[5][m]=b";!FILE /different/build/"+m.encode()+b".asm\n"+raw.split(b"\n",1)[1]
        verify.verify(changed,BOARD)
        raw=changed[5]["clock"]; changed[5]["clock"]=raw.replace(b"clock.asm",b"wrong.asm",1)
        self.reject(changed)


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output",type=Path,required=True)
    parser.add_argument("--board",choices=("generic","lg_esl29_rev03"),required=True)
    args=parser.parse_args()
    BOARD=args.board; ARTIFACTS=verify.load(args.output,BOARD)
    genuine = type("GenuineSmokeGate", (SmokeGate, unittest.TestCase), {})
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(genuine)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    raise SystemExit(0 if result.wasSuccessful() else 1)
