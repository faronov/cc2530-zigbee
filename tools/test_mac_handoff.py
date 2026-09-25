# SPDX-License-Identifier: BSD-3-Clause
"""Handoff proof rejection controls; no compiler, simulator or hardware."""
from dataclasses import FrozenInstanceError
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

from verify_firmware import ROOT
sys.path.insert(0, str(ROOT / "tests"))
import boot_mac_attempt as proof
from boot_mac_handoff import HANDOFF


class HandoffProofTests(unittest.TestCase):
    def setUp(self):
        self.symbols = {"_main": 0x1000, "_mac_attempt_test_before": 0x1230,
                        "_mac_attempt_test_done": 0x1234, "_mac_radio_handoff_clock": 0x60,
                        "_SOC_RFIRQF0": 0xe9}
        address = 0x100
        for name, size in HANDOFF.caller_sizes:
            self.symbols["_mac_attempt_test_"+name] = address
            address += size
        self.allocated = set(range(address)) | set(range(0x1e00, 0x1e08))
        self.ram = bytearray([0xa5] * 0x1f00)
        self.ram[:address] = bytes(address)
        self.ram[0x1e00:0x1e08] = b"MAH1\x01\x08\0\0"
        self.iram = bytearray(0x7d) + bytearray([0xc7] * 131)
        self.sfr = bytearray(128)
        self.sfr[1] = HANDOFF.stack+1
        self.initial = {str(a): 0 for a in (0xe9, 0x6081, 0x6083, *range(0x6100, 0x6400))}
        self.hardware = bytearray([0x69] * 1024)
        for a in map(int, self.initial):
            if a >= 256:
                self.hardware[a-0x6000] = 0
        self.step = {name: 0 for name, _ in HANDOFF.caller_sizes}
        self.step.update(operation=7, configuration="00"*14, frame="00"*128, record="00"*164,
                         clock="00"*6, handoff_clock="00"*18, diagnostic="00"*14,
                         radio_diag=[0]*21, initial=self.initial, events=[])
        self.ram[self.symbols["_mac_attempt_test_operation"]] = 7
        self.vector = {"case": proof.LEGACY.cases, "steps": [self.step]}
        self.debug = "L:Fmac_radio$status$0_0$0:0\nL:Fradio_autoack$status$0_0$0:20\n"

    def text(self):
        def dump(raw, start):
            return "".join(f"0x{start+i:06x} "+raw[i:i+16].hex(" ")+"\n"
                           for i in range(0, len(raw), 16))
        return ("0x2530000a\nCPU state= OK PC= 0x1234\n"
                "Max value of stack pointer= 0x7b, avg= 0x60\n" + dump(self.ram, 0) +
                "0x2530000b\n" + dump(self.iram, 0) +
                "0x2530000c\n" + dump(self.sfr, 0x80) +
                "0x2530000d\n0x2530000e\n" + dump(self.hardware, 0x6000) +
                "0x2530000f\n0x25300010\nCPU state= OK PC= 0x1230\n0x25300011\n")

    def check(self, text=None):
        with patch.object(proof, "simulate", return_value=self.text() if text is None else text):
            return proof.run_vector("no-simulator", Path("not-an-image"), self.symbols,
                                    self.debug, self.allocated, {}, self.vector, HANDOFF)

    def test_profile_is_immutable_and_does_not_replace_legacy(self):
        legacy = proof.LEGACY
        for profile in (legacy, HANDOFF):
            with self.assertRaises(FrozenInstanceError):
                profile.size = 1
        self.assertEqual(self.check(), (0, 0x7b))
        self.assertIs(proof.LEGACY, legacy)
        self.assertEqual((legacy.size, legacy.xdata, legacy.stack, legacy.cases),
                         (24621, 1475, 0x5a, 28))
        self.assertEqual(legacy.caller_sizes, tuple(proof.CALLER_SIZES.items()))
        self.assertNotEqual(HANDOFF.code_sha, legacy.code_sha)

    def test_exact_clock_staging_diagnostics_and_upper_iram(self):
        for raw, positions in (
            (self.ram, (*range(0x60, 0x72), *range(0x20, 0x3a),
                        *range(self.symbols["_mac_attempt_test_clock"],
                               self.symbols["_mac_attempt_test_clock"]+6), 0x1e08, 0x1eff)),
            (self.iram, range(0x7d, 0x100)),
        ):
            for at in positions:
                with self.subTest(at=at):
                    raw[at] ^= 1
                    with self.assertRaises(ValueError):
                        self.check()
                    raw[at] ^= 1

    def test_missing_or_malformed_observations(self):
        for key in ("clock", "handoff_clock", "radio_diag"):
            saved = self.step.pop(key)
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.check()
            self.step[key] = saved
        for key, value in (("handoff_clock", "00"*17), ("clock", "00"*5), ("radio_diag", [0]*20)):
            saved = self.step[key]
            self.step[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.check()
            self.step[key] = saved
        for old, new in (("0x000000 ", "omitted "), ("CPU state=", "missing state="),
                         ("Max value of stack pointer=", "missing peak="),
                         ("pointer= 0x7b", "pointer= 0x7d")):
            with self.subTest(old=old), self.assertRaises(ValueError):
                self.check(self.text().replace(old, new))

    def test_only_exact_peripheral_initial_values_not_private_state(self):
        for at in (0x60, 0x100, 0x1f00, 0x6082):
            self.initial[str(at)] = 0
            with self.subTest(at=at), self.assertRaises(ValueError):
                self.check()
            del self.initial[str(at)]
        for key in ("233", "24705", "24707", "24832"):
            saved = self.initial.pop(key)
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.check()
            self.initial[key] = saved

    def test_handoff_cannot_strobe_stop_flush_or_read_fifo(self):
        for kind, address, value in (
            ("w", 0xe1, 0xea), ("w", 0xe1, 0xed), ("w", 0xe1, 0xee), ("w", 0xd9, 0),
            ("w", 0x618b, 0), ("w", 0x618d, 1), ("w", 0x6189, 0x40), ("w", 0xe9, 0),
            ("r", 0xd9, 0), ("r", 0x6000, 0),
        ):
            self.step["events"] = [(kind, address, value)]
            with self.subTest(kind=kind, address=address), self.assertRaises(ValueError):
                self.check()

    def test_sfd_clear_and_next_probe_do_not_skip_current_breakpoint(self):
        self.step["events"] = [("w", 0xe9, 0xfd), ("r", 0x6193, 0)]
        with patch.object(proof, "simulate", side_effect=RuntimeError("commands ready")) as simulate:
            with self.assertRaisesRegex(RuntimeError, "commands ready"):
                proof.run_vector("no-simulator", Path("not-an-image"), self.symbols,
                                 self.debug, self.allocated, {}, self.vector, HANDOFF)
        commands = simulate.call_args.args[1]
        first, second = commands.index(proof.marker(10)), commands.index(proof.marker(13))
        self.assertEqual(commands[first-1], "run")
        self.assertNotIn("run", commands[first:second])


if __name__ == "__main__":
    unittest.main()
