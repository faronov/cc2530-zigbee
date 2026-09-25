# SPDX-License-Identifier: BSD-3-Clause
"""Adapter proof isolation/negative controls; synthetic data, no tools/devices."""
import copy
from dataclasses import FrozenInstanceError
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

from verify_firmware import ROOT
sys.path.insert(0, str(ROOT/"tests"))
import boot_mac_adapter as proof
import verify_mac_adapter as layout
from verify_banked_join import Field
import test_banked_join as join_tests


class AdapterMetadataTests(unittest.TestCase):
    def fixture(self):
        sizes = {"tx": 180, "action": 28, "random": 29, "clock": 6, "through": 6, "config": 14,
                 "observation": 48, "diagnostics": 44, "record": 164, **layout.SCALARS, "status": 8, "packet": 125}
        symbols, lines, address = {}, [], 0x400
        for index, (name, size) in enumerate(sizes.items()):
            shape = f"ST__{index}:S" if name in layout.ROOTS else f"DA{size}d,SC:U"
            if name in layout.ROOTS:
                lines.append(f"T:Fmac_adapter_fixture$__{index}[({{0}}S:S$value$0_0$0({{{size}}}DA{size}d,SC:U),Z,0,0)]")
            symbols["_fixture_"+name] = address
            lines += [f"S:G$fixture_{name}$0_0$0({{{size}}}{shape}),F,0,0",
                      f"L:G$fixture_{name}$0_0$0:{address:X}"]
            address += size
        return ("\n".join(lines)+"\n").encode("ascii"), symbols

    def test_typed_fields_cover_every_byte_without_native_padding(self):
        raw, symbols = self.fixture()
        schema = layout.Layout(raw, symbols)
        value = schema.at("tx", "value")
        self.assertEqual((value.offset, value.size, value.shape), (0, 180, "DA180d,SC:U"))
        for old, new in ((b"{0}S:S$value", b"{1}S:S$value"),
                         (b"{180}DA180d", b"{179}DA179d")):
            changed = layout.Layout(raw.replace(old, new, 1), symbols)
            with self.assertRaisesRegex(ValueError, "unaccounted byte"):
                changed.at("tx", "value")

    def test_missing_duplicate_or_disagreed_caller_metadata_is_not_accepted(self):
        raw, symbols = self.fixture()
        declaration = next(line for line in raw.splitlines(keepends=True) if line.startswith(b"S:G$fixture_tx$"))
        for bad in (raw+declaration, raw.replace(declaration, b""), raw.replace(b"\n", b"\r\n"),
                    raw.replace(b"$fixture_limit$0_0$0({2}", b"$fixture_limit$0_0$0({1}")):
            with self.subTest(bad=bad[:30]), self.assertRaises(ValueError):
                layout.Layout(bad, symbols)
        with self.assertRaisesRegex(ValueError, "map/CDB"):
            layout.Layout(raw, symbols | {"_fixture_tx": symbols["_fixture_tx"]+1})

    def test_explicit_data_profile_does_not_mutate_default_join_ownership(self):
        fixture = join_tests.JoinDataLivenessTests().fixture(overwrite=True)
        expected = copy.deepcopy(fixture)
        original_modules = layout.live_data.__kwdefaults__["modules"]
        self.assertEqual(layout.live_data(*fixture, modules=("caller", "callee"))[0], 2)
        areas = {"caller": "BJ_caller", "callee": "BJ_callee"}
        reserved, overlay = {8}, set()
        self.assertEqual(layout.live_data(*fixture, modules=("caller", "callee"),
                                         reservations=reserved, overlay=overlay, frame_areas=areas)[0], 2)
        self.assertEqual(fixture, expected)
        self.assertEqual(reserved, {8}); self.assertEqual(overlay, set())
        self.assertEqual(layout.live_data.__kwdefaults__["modules"], original_modules)
        with self.assertRaisesRegex(ValueError, "physically allocated"):
            layout.live_data(*fixture, modules=("caller", "callee"), reservations={9}, frame_areas=areas)

    def test_raw_reference_pin_and_sanitizer_are_mandatory_before_execution(self):
        self.assertEqual(len(proof.CASES), 18)
        self.assertEqual((sum(c.calls for c in proof.CASES), sum(c.events for c in proof.CASES)), (2410, 210988))
        with self.assertRaises(FrozenInstanceError):
            proof.CASES[0].calls = 0
        with patch.object(proof.subprocess, "check_output", return_value=b"{}\r\n"):
            with self.assertRaisesRegex(ValueError, "Complete raw"):
                proof.reference(Path("unused"), 0)
        raw = b'{"case":0,"steps":[{"events":[]}]}\n'
        spec = proof.Case("synthetic", proof.banking.sha(raw), 1, 0)
        with patch.object(proof, "CASES", (spec,)), \
                patch.object(proof.subprocess, "check_output", side_effect=[raw, raw+b"\n"]):
            with self.assertRaisesRegex(ValueError, "native/sanitizer"):
                proof.reference(Path("unused"), 0)
        for n in (-1, 18, True):
            with self.assertRaisesRegex(ValueError, "Unknown"):
                proof.reference(Path("unused"), n)

    def test_complete_entrypoint_requires_all_artifacts_mappings_and_cases(self):
        def replay(simulator, artifacts, vector):
            case = proof.CASES[vector["case"]]
            return case.calls, case.events, case.sampled, case.peak
        with patch.object(sys, "argv", ["proof", "--output", "unused"]), \
                patch.object(layout, "load", return_value=("actual",)), patch.object(layout, "verify") as verify, \
                patch.object(layout, "artifact_bytes", return_value="immutable"), \
                patch.object(proof.banking, "artifact_negatives", return_value=277229) as negatives, \
                patch.object(proof, "check_alias", side_effect=[None, ValueError("no alias")]) as alias, \
                patch.object(proof.banking, "check_mapping",
                             side_effect=[None, ValueError("no banking"), ValueError("no alias")]) as mapping, \
                patch.object(proof, "reference", side_effect=lambda output, n: {"case": n}) as native, \
                patch.object(proof, "run_vector", side_effect=replay) as run, patch("builtins.print"):
            proof.main()
            verify.assert_called_once_with("actual")
            negatives.assert_called_once_with("immutable", layout.PINS)
            self.assertEqual(alias.call_count, 2)
            self.assertEqual([c.kwargs for c in mapping.call_args_list], [{}, {"code": False}, {"alias": False}])
            self.assertEqual([c.args[1] for c in native.call_args_list], list(range(18)))
            self.assertEqual(run.call_count, 18)
            run.reset_mock(); negatives.return_value -= 1
            with self.assertRaisesRegex(ValueError, "rejection inventory"):
                proof.main()
            run.assert_not_called()


class AdapterReplayTests(unittest.TestCase):
    def setUp(self):
        raw, self.symbols = AdapterMetadataTests().fixture()
        self.schema = layout.Layout(raw, self.symbols)
        self.symbols.update(_main=0x1000, _fixture_before=0x1230, _fixture_done=0x1234,
                            _banked_stop=0x1238, _banked_depth=0x1e, _banked_fault=0x1f,
                            _fixture_status=0x1e00, l_XSEG=0x900, s_SSEG=0x56, _SOC_RFIRQF0=0xe9)
        self.initial = {str(a): 0 for a in (0xe9, 0x6081, 0x6083, *range(0x6100, 0x6400))}
        self.step = {key: "00"*self.schema.globals["fixture_"+name].size
                     if name in ("packet", "through", "config") else 0 for name, key in proof.INPUTS.items()}
        self.step.update({key: "00"*self.schema.globals["fixture_"+key].size
                          for key in proof.OUTPUTS[1:]})
        self.step.update({"return": 0, "initial": self.initial, "events": []})
        self.vector = {"case": 0, "steps": [self.step]}
        self.ram = bytearray(0x900)+bytearray(b"\xa5"*(0x1f00-0x900))
        self.ram[0x1e00:0x1e08] = b"MAD1\x01\x08\0\0"
        self.iram = bytearray(0x7d)+bytearray(b"\xc7"*131)
        self.sfr = bytearray(128); self.sfr[1] = 0x57; self.sfr[0x1f] = 1
        self.hardware = bytearray(b"\x69"*1024)
        for a in map(int, self.initial):
            if a >= 256:
                self.hardware[a-0x6000] = 0
        self.media = b"\x69"+b"\xff"*0x3ffff
        self.extended = b"\x69"*0x4000+bytes(self.hardware)+b"\x69"*0x1c00
        self.artifacts = ({0: 0x69}, self.symbols, b"", b"", {}, {})

    def text(self):
        def dump(raw, start):
            return "".join(f"0x{start+i:06x} "+raw[i:i+32].hex(" ")+"\n" for i in range(0, len(raw), 32))
        return ("0x2530000a\nCPU state= OK PC= 0x1234\n"
                "Max value of stack pointer= 0x78, avg= 0x60\n"+dump(self.ram, 0)+
                "0x2530000b\n"+dump(self.iram, 0)+"0x2530000c\n"+dump(self.sfr, 128)+
                "0x2530000d\n0x2530000e\n"+dump(self.hardware, 0x6000)+
                "0x2530000f\n0x25300010\nCPU state= OK PC= 0x1230\n0x25300011\n")

    def check(self, text=None, *, chunk=64, states=None):
        state = (bytes(self.ram), bytes(self.iram), bytes(self.sfr), self.extended, self.media)
        with patch.object(layout, "Layout", return_value=self.schema), \
                patch.object(proof, "mmio_sites", return_value={}), \
                patch.object(proof.banking, "model", return_value=[]), \
                patch.object(proof, "captured_sections", return_value=state, side_effect=states), \
                patch.object(proof, "simulate_binary_dumps", return_value=self.text() if text is None else text):
            return proof.run_vector("synthetic", self.artifacts, self.vector, chunk=chunk)

    def test_continuation_must_match_the_prior_complete_actual_state(self):
        self.vector["steps"] *= 2
        self.assertEqual(self.check(chunk=1), (2, 0, 0, 0x78))
        state = (bytes(self.ram), bytes(self.iram), bytes(self.sfr), self.extended, self.media)
        for index in range(len(state)):
            changed = list(state)
            changed[index] = bytes((changed[index][0] ^ 1,))+changed[index][1:]
            with self.subTest(index=index), self.assertRaisesRegex(ValueError, "Continuation changed"):
                self.check(chunk=1, states=[state, tuple(changed)])

    def test_complete_public_outputs_and_private_guards_are_not_optional(self):
        self.assertEqual(self.check(), (1, 0, 0, 0x78))
        for name in proof.OUTPUTS:
            address = self.symbols["_fixture_"+name]
            self.ram[address] ^= 1
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, "caller"):
                self.check()
            self.ram[address] ^= 1
        for memory, addresses in ((self.ram, (0x900, 0x1e08, 0x1eff)),
                                  (self.iram, (0x1e, 0x1f, 0x7d, 0xff)),
                                  (self.sfr, (1, 0x1f, 0x47, 0x69)),
                                  (self.hardware, (0, 0x100, 0x3ff))):
            for address in addresses:
                memory[address] ^= 1
                with self.subTest(address=address), self.assertRaises(ValueError):
                    self.check()
                memory[address] ^= 1
        for index in (0, 0x2000, 0x3e800, 0x3ffff):
            media = self.media
            self.media = media[:index]+bytes((media[index] ^ 1,))+media[index+1:]
            with self.assertRaisesRegex(ValueError, "flash/extended"):
                self.check()
            self.media = media

    def test_only_declared_inputs_and_exact_peripheral_inventory_are_injected(self):
        for address in (0, 0x400, 0x1e00, 0x1f00, 0x6082):
            self.initial[str(address)] = 0
            with self.subTest(address=address), self.assertRaisesRegex(ValueError, "peripheral ownership"):
                self.check()
            del self.initial[str(address)]
        for key in ("tx", "record", "diagnostics", "packet"):
            value = self.step.pop(key)
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.check()
            self.step[key] = value
        for key, value in (("private_state", 1), ("operation", 12), ("selector", 4),
                           ("timeout", -1), ("limit", 65536), ("packet", "00"*124)):
            old = self.step.get(key); self.step[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.check()
            if old is None: del self.step[key]
            else: self.step[key] = old

    def test_missing_peak_pc_and_observation_byte_are_rejected(self):
        for old, new in (("pointer= 0x78", "pointer= 0x7d"),
                         ("Max value of stack pointer=", "missing peak="),
                         ("PC= 0x1234", "PC= 0x1235"), ("PC= 0x1230", "PC= 0x1231"),
                         ("0x000400 ", "missing ")):
            with self.subTest(old=old), self.assertRaises(ValueError):
                self.check(self.text().replace(old, new))


if __name__ == "__main__":
    unittest.main()
