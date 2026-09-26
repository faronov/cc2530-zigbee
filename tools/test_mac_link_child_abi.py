#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Bounded CHILD ABI verifier negatives. No compiler/simulator/hardware calls.

Discovery runs synthetic parser/observation tests. --output additionally runs
the genuine artifact regressions against an already built four-module image.
No generated firmware/metadata is embedded here or substituted for a real link.
"""
import argparse
from dataclasses import replace
from pathlib import Path
import re
import shutil
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tests"))
import boot_mac_link_child_abi as proof


def dump(raw, first=0):
    return "".join(f"0x{first + i:06x} " + raw[i:i + 16].hex(" ") + "\n"
                   for i in range(0, len(raw), 16))


class ObservationTests(unittest.TestCase):
    """Synthetic verifier inputs only, never claimed to be simulated outcomes."""
    def setUp(self):
        self.layout = proof.Layout({"_child_abi_checks": 0x5ca},
                                  frozenset(range(1, proof.XDATA + 1)), frozenset())
        self.ram = bytearray([0xa5] * 0x1f00)
        self.ram[1:proof.XDATA + 1] = bytes(proof.XDATA)
        self.ram[0x5ca:0x5ce] = proof.CHECKS.to_bytes(4, "little")
        self.ram[0x1e00:0x1e08] = b"CAB1\x01\0\0\xa5"
        self.iram = bytearray(0x7d) + bytearray([0xc7] * 131)
        self.alias = self.iram.copy()

    def text(self):
        return (f"Stop at 0x{proof.MAIN:06x}: (104) Breakpoint\n"
                f"0x25300001\nCPU state= OK PC= 0x{proof.MAIN:06x}\n"
                "Max value of stack pointer= 0x51, avg= 0x50\n0x81 4f\n0x25300002\n"
                f"Stop at 0x{proof.DONE:06x}: (104) Breakpoint\n"
                f"0x25300003\nCPU state= OK PC= 0x{proof.DONE:06x}\n"
                "Max value of stack pointer= 0x59, avg= 0x50\n0x81 51\n0x25300004\n" +
                dump(self.ram) + "0x25300005\n" + dump(self.iram) +
                "0x25300006\n" + dump(self.alias, 0x1f00) + "0x25300007\n")

    def reject(self, text=None):
        with self.assertRaises(ValueError):
            proof.observation(self.text() if text is None else text, self.layout)

    def test_nominal_and_fixed_contract(self):
        self.assertEqual(proof.observation(self.text(), self.layout), (43098, 0x59))
        self.assertEqual((proof.SIZE, proof.XDATA, proof.STACK_FIRST, proof.STACK_SIZE, proof.SP_CAP),
                         (11105, 1551, 0x50, 45, 0x7c))

    def test_exact_completed_count(self):
        for value in (0, 1, 43097, 43099, 0xffffffff):
            self.ram[0x5ca:0x5ce] = value.to_bytes(4, "little")
            with self.subTest(value=value):
                self.reject()

    def test_every_status_byte_including_unwritten_sentinel(self):
        for at in range(0x1e00, 0x1e08):
            self.ram[at] ^= 1
            with self.subTest(at=at):
                self.reject()
            self.ram[at] ^= 1

    def test_wrong_stop_flash_entry_and_non_breakpoint(self):
        good = self.text()
        for pc in (proof.FAILED, proof.FLASH, proof.MAIN, proof.DONE + 1):
            with self.subTest(pc=pc):
                self.reject(good.replace(f"Stop at 0x{proof.DONE:06x}", f"Stop at 0x{pc:06x}"))
                self.reject(good.replace(f"PC= 0x{proof.DONE:06x}", f"PC= 0x{pc:06x}"))
        for reason in ("(104) Breakpoint", "(101) Halted", "(105) Unknown"):
            self.reject(f"Stop at 0x{proof.FLASH:06x}: {reason}\n" + good)
        self.reject(good.replace("(104) Breakpoint", "(101) Halted", 1))

    def test_stack_peak_sp_canary_and_alias(self):
        good = self.text()
        for old, new in (("pointer= 0x59", "pointer= 0x7d"), ("pointer= 0x59", "pointer= 0x58"),
                         ("Max value of stack pointer=", "missing peak="), ("0x81 51", "0x81 50"),
                         ("0x81 4f", "0x81 50")):
            self.reject(good.replace(old, new))
        for at in (0x7d, 0x7e, 0xc0, 0xfe, 0xff):
            self.iram[at] ^= 1
            self.alias[at] ^= 1
            self.reject()
            self.iram[at] ^= 1
            self.alias[at] ^= 1
        for at in (0, 0x7d, 0xff):
            self.alias[at] ^= 1
            self.reject()
            self.alias[at] ^= 1

    def test_unowned_xdata_and_full_status_reservation(self):
        for at in (0, proof.XDATA + 1, 0x1dff, 0x1e08, 0x1e3f, 0x1e40, 0x1eff):
            self.ram[at] ^= 1
            self.reject()
            self.ram[at] ^= 1

    def test_missing_duplicate_or_conflicting_observations(self):
        good = self.text()
        for marker in ("0x25300003\n", "0x25300005\n", "0x25300007\n"):
            self.reject(good.replace(marker, ""))
            self.reject(good.replace(marker, marker + marker))
        self.reject(good.replace("0x000000 ", "omitted ", 1))
        self.reject(good.replace("0x81 51\n", "0x81 51\n0x81 51\n"))
        self.reject(good.replace("0x81 51\n", "0x81 50\n0x81 51\n"))
        self.reject(good.replace("0x81 51\n", "0x81 51\n0x80 00\n"))
        self.reject(good.replace("0x81 51\n", f"0x81 51\nCPU state= OK PC= 0x{proof.DONE:x}\n"))

    def test_commands_use_alias_and_forbid_flash_before_first_run(self):
        commands = proof.commands()
        self.assertEqual(commands[0], proof.ALIAS)
        self.assertLess(commands.index(f"break {proof.FLASH:#x}"), commands.index(f"run 0 {proof.MAIN:#x}"))
        self.assertIn("dump /h xram 0x1f00 0x1fff", commands)
        self.assertNotIn("step", commands)
        # The shared helper retains its real15-second whole-process timeout.
        from types import SimpleNamespace
        with patch("boot_image.subprocess.run", return_value=SimpleNamespace(stdout="")) as run:
            proof.simulate("not-a-simulator", [], None)
        self.assertEqual(run.call_args.kwargs["timeout"], 15)

    def test_raw_cdb_rejected_before_any_text_decoding_or_missing_artifacts(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for raw in (b"M:wrong\n", b"M:wrong\r\n", b"\0", b"\xff"):
                (root / "child_abi.cdb").write_bytes(raw)
                with patch.object(Path, "read_text", side_effect=AssertionError("text decoding used")):
                    with self.assertRaisesRegex(ValueError, "raw CDB before decoding"):
                        proof.load(root)

    def test_object_comment_treatment_is_only_one_output_path(self):
        for name in proof.MODULES:
            body = f"XH3\nM {name}\nS C$src.c$11$1_0$1 Def000000\n".encode()
            self.assertEqual(proof.object_body(b";!FILE one/" + name.encode() + b".asm\n" + body, name), body)
            self.assertEqual(proof.object_body(b";!FILE other/" + name.encode() + b".asm\n" + body, name), body)
            for raw in (b";!FILE wrong.asm\n" + body, b";!FILE x/" + name.encode() + b".asm\r\n" + body,
                        b";!FILE x/" + name.encode() + b".asm\n\0" + body):
                with self.assertRaises(ValueError):
                    proof.object_body(raw, name)


def genuine_suite(output):
    """Real-artifact tests are explicitly selected, not skipped or faked by discovery."""
    class GenuineArtifactTests(unittest.TestCase):
        @classmethod
        def setUpClass(cls):
            cls.artifacts = proof.load(output)
            cls.layout = proof.verify(cls.artifacts)
            cls.debug = cls.artifacts.debug.decode("ascii")
            cls.listings = {m: v.decode("ascii") for m, v in cls.artifacts.listings.items()}
            cls.objects = {m: v.decode("ascii") for m, v in cls.artifacts.objects.items()}
            cls.memory = cls.artifacts.memory.decode("ascii")

        def reject(self, **changes):
            with self.assertRaises(ValueError):
                proof.verify(replace(self.artifacts, **changes))

        def structural(self, **changes):
            args = dict(image=self.artifacts.image, debug=self.debug, symbols=self.layout.symbols,
                        memory=self.memory, listings=self.listings, objects=self.objects)
            args.update(changes)
            with self.assertRaises(ValueError):
                proof.layout_checks(**args)

        def test_genuine_nominal_and_relocated_output_comment(self):
            self.assertEqual(len(self.layout.direct), 26)
            objects = {m: b";!FILE another/build/" + m.encode() + b".asm\n" +
                       raw.split(b"\n", 1)[1] for m, raw in self.artifacts.objects.items()}
            proof.verify(replace(self.artifacts, objects=objects))
            with patch.object(Path, "read_text", side_effect=AssertionError("universal-newline input")):
                proof.verify(proof.load(output))

        def test_corrupt_complete_code_and_extent(self):
            for at in (0, proof.SIZE // 2, proof.SIZE - 1):
                image = self.artifacts.image.copy()
                image[at] ^= 1
                self.reject(image=image)
            image = self.artifacts.image.copy()
            del image[proof.SIZE // 2]
            self.reject(image=image)
            self.reject(image=self.artifacts.image | {proof.SIZE: 0})
            self.reject(image=self.artifacts.image | {0x8000: 0})

        def test_raw_cdb_cr_nul_and_full_metadata(self):
            raw = self.artifacts.debug
            edits = (raw.replace(b"\n", b"\r\n"), raw + b"\r", raw + b"\0", b"\0" + raw,
                     raw[:-1], raw + b"\n", raw.replace(b"M:flash_exec", b"M:fake_exec"),
                     raw.replace(b"{543}", b"{542}", 1), raw.replace(b"({31}", b"({30}", 1))
            for new in edits:
                self.assertNotEqual(new, raw)
                with self.assertRaisesRegex(ValueError, "raw CDB before decoding"):
                    proof.verify(replace(self.artifacts, debug=new))
            # Change a non-location type record, and a source-line record.
            for prefix in (b"T:", b"L:C"):
                line = next(x for x in raw.splitlines(keepends=True) if x.startswith(prefix))
                self.reject(debug=raw.replace(line, line[:-1] + b"x\n", 1))

        def test_complete_objects_and_snapshots(self):
            for field in ("objects", "listings"):
                original = getattr(self.artifacts, field)
                for module in proof.MODULES:
                    missing = original.copy()
                    del missing[module]
                    self.reject(**{field: missing})
                    for raw in (original[module] + b"\n", original[module] + b"\0",
                                original[module].replace(b"\n", b"\r\n")):
                        self.reject(**{field: original | {module: raw}})
                self.reject(**{field: original | {"security_joint_model": b"not production"}})
                a, b = proof.MODULES[:2]
                self.reject(**{field: original | {a: original[b], b: original[a]}})

        def test_map_areas_inventory_metadata_and_memory(self):
            raw = self.artifacts.mapping
            for old, new in ((b"00001E00", b"00001F00"), (b"00000050", b"0000004F"),
                             (b"0000060F", b"00000610"), (b"BJ_CHILD_ABI=0x10", b"BJ_CHILD_ABI=0x30"),
                             (b"mac_link_child_workspace.rel", b"mac_link_workspace.rel"),
                             (b"[ _gptrget.rel ]", b"[ made_up.rel ]")):
                changed = raw.replace(old, new)
                self.assertNotEqual(changed, raw, old)
                self.reject(mapping=changed)
            self.reject(mapping=raw + b"\n")
            for old, new in ((b"with 45 bytes", b"with 46 bytes"), (b"1551", b"1552"),
                             (b"11105", b"11106"), (b"0x7d", b"0x7c")):
                changed = self.artifacts.memory.replace(old, new)
                self.assertNotEqual(changed, self.artifacts.memory)
                self.reject(memory=changed)

        def test_structural_moved_regions_stack_runtime_and_probe(self):
            for name, value in (
                ("s_XSEG", 0), ("l_XSEG", 0x1f00), ("l_XISEG", 1), ("l_XABS", 1),
                ("s_SSEG", 0x4f), ("l_SSEG", 46), ("s_ISEG", 0x50), ("s_OSEG", 0x4f),
                ("s_BSEG_BYTES", 0x30), ("__XPAGE", 0xa0), ("_main", proof.MAIN + 1),
                ("_child_work_arena", self.layout.symbols["_child_work_arena"] + 1),
                ("_link_work_arena", self.layout.symbols["_link_work_arena"] - 1),
                ("_child_work_reserved_end", self.layout.symbols["_child_work_reserved_end"] - 1),
                ("___memcpy_PARM_2", self.layout.symbols["___memcpy_PARM_2"] + 1),
                ("_child_abi_result", 0x1f00), ("_child_abi_checks", 1),
            ):
                with self.subTest(name=name):
                    self.structural(symbols=self.layout.symbols | {name: value})
            for old, new in (("({8}DA8d,SC:U),F", "({9}DA8d,SC:U),F"),
                             ("M:flash_exec", "M:security_joint_model")):
                changed = self.debug.replace(old, new)
                self.assertNotEqual(changed, self.debug)
                self.structural(debug=changed)

        def test_structural_actual_data_and_owner_overlap(self):
            module = proof.MODULES[0]
            text = self.listings[module]
            pattern = r"^(\s*)000030(\s+\d+\s+\.ds \d+)$"
            changed, count = re.subn(pattern, r"\g<1>000010\2", text, count=1, flags=re.M)
            self.assertEqual(count, 1)
            self.structural(listings=self.listings | {module: changed})
            record = "L:Fmac_link_child_workspace$owner$0_0$0:1"
            self.assertIn(record + "\n", self.debug)
            self.structural(debug=self.debug.replace(record + "\n", record + "0\n"))
            self.structural(listings=self.listings | {module: text + "\n 1 .area FAKE\n 000010 2 .ds 8\n"})

        def test_on_disk_inventory_and_snapshot_consumption(self):
            with tempfile.TemporaryDirectory() as directory:
                parent = Path(directory) / "mac-link-child-workspace"
                root = parent / "abi"
                root.mkdir(parents=True)
                # Real parent objects are not ABI inventory or a missing-file
                # fallback. No generated/fake probe is needed for this negative.
                for module in proof.MODULES[:3]:
                    shutil.copyfile(output / (module + ".rel"), parent / (module + ".rel"))
                for name in ("child_abi.ihx", "child_abi.cdb", "child_abi.map", "child_abi.mem",
                             *(m + ".rel" for m in proof.MODULES),
                             *("child_abi." + m + ".rst" for m in proof.MODULES)):
                    shutil.copyfile(output / name, root / name)
                # The mutable linker listing is NOT the consumed snapshot.
                (root / (proof.MODULES[0] + ".rst")).write_bytes(b"subsequent unrelated link")
                proof.verify(proof.load(root))
                for name in (proof.MODULES[0] + ".rel", "child_abi." + proof.MODULES[0] + ".rst"):
                    raw = (root / name).read_bytes()
                    (root / name).unlink()
                    with self.assertRaisesRegex(ValueError, "on-disk"):
                        proof.load(root)
                    (root / name).write_bytes(raw)
                for name in ("extra.rel", "child_abi.extra.rst"):
                    (root / name).write_bytes(b"extra")
                    with self.assertRaisesRegex(ValueError, "on-disk"):
                        proof.load(root)
                    (root / name).unlink()
                raw = self.artifacts.debug.replace(b"\n", b"\r\n")
                (root / "child_abi.cdb").write_bytes(raw)
                with self.assertRaisesRegex(ValueError, "raw CDB before decoding"):
                    proof.load(root)
    return unittest.defaultTestLoader.loadTestsFromTestCase(GenuineArtifactTests)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, help="Add genuine linked-artifact negatives; no build/simulation")
    args = parser.parse_args()
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(ObservationTests)
    if args.output is not None:
        suite.addTests(genuine_suite(args.output))
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    raise SystemExit(not result.wasSuccessful())


if __name__ == "__main__":
    main()
