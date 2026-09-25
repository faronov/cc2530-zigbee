# SPDX-License-Identifier: BSD-3-Clause
"""Pure linked-instruction liveness and public-observation regressions."""
import sys
import subprocess
import unittest
from unittest.mock import Mock, patch

from verify_firmware import ROOT
sys.path.insert(0, str(ROOT/"tests"))
import verify_banked_join as layout
import boot_banked_join as join
from verify_banked_join import Field, Schema


class JoinDataLivenessTests(unittest.TestCase):
    def fixture(self, overwrite=False):
        symbols = {"s_BJ_caller": 8, "l_BJ_caller": 1, "s_BJ_callee": 8, "l_BJ_callee": 1,
                   "__sdcc_banked_ret": 0x300}
        debug = "\n".join(
            f"S:L{m}.work$sloc0$0_1$0({{1}}SC:U),E,0,0\nL:L{m}.work$sloc0$0_1$0:8"
            for m in ("caller", "callee"))
        decoded = {0x100: b"\x75\x08\x01", 0x103: b"\x12\x02\x00",
                   0x200: b"\x75\x08\x69", 0x203: b"\x22"}
        if overwrite:
            decoded.update({0x106: b"\x75\x08\x01", 0x109: b"\xe5\x08", 0x10b: b"\x22"})
        else:
            decoded.update({0x106: b"\xe5\x08", 0x108: b"\x22"})
        owners = {p: "caller" if p < 0x200 else "callee" for p in decoded}
        functions = {p: 0x100 if p < 0x200 else 0x200 for p in decoded}
        entries = {0x100: ("caller", "work", False), 0x200: ("callee", "work", False)}
        targets, calls = {0x103: 0x200}, {0x103: 0x200}
        return [symbols, debug, {}, decoded, owners, functions, entries, targets, calls]

    def analyze(self, fixture):
        with patch.object(layout, "MODULES", ("caller", "callee")):
            return layout.live_data(*fixture)

    def test_dead_call_site_storage_can_share_physical_bytes(self):
        self.assertEqual(self.analyze(self.fixture(overwrite=True))[0], 2)

    def test_live_byte_may_not_share_a_nested_callee_write(self):
        with self.assertRaisesRegex(ValueError, "Live DATA overwritten"):
            self.analyze(self.fixture())

    def test_uninitialized_retained_data_is_not_scratch(self):
        fixture = self.fixture(overwrite=True)
        fixture[3][0x100] = b"\xe5\x08"
        fixture[3][0x102] = b"\x00"
        fixture[4][0x102] = "caller"
        fixture[5][0x102] = 0x100
        with self.assertRaisesRegex(ValueError, "Uninitialized/retained"):
            self.analyze(fixture)

    def test_indirect_pointer_to_compiler_scratch_is_rejected(self):
        fixture = self.fixture(overwrite=True)
        fixture[2]["caller"] = b"mov r0,#_work_sloc0_1_0"
        with self.assertRaisesRegex(ValueError, "DATA address escapes"):
            self.analyze(fixture)

    def test_undeclared_frame_byte_and_recursive_call_are_rejected(self):
        fixture = self.fixture(overwrite=True)
        fixture[0]["l_BJ_caller"] = 2
        with self.assertRaisesRegex(ValueError, "Undeclared"):
            self.analyze(fixture)
        fixture = self.fixture(overwrite=True)
        fixture[-1][0x103] = fixture[-2][0x103] = 0x100
        with self.assertRaisesRegex(ValueError, "Recursive"):
            self.analyze(fixture)

    def test_live_overlay_is_checked_for_source_and_transitive_libc_clobbers(self):
        fixture = self.fixture()
        fixture[3][0x100] = b"\x75\x46\x01"
        fixture[3][0x106] = b"\xe5\x46"
        fixture[3][0x200] = b"\x75\x46\x69"
        listing = b".area OSEG (OVR,DATA)\n 000046 1 .ds 1\n"
        fixture[2].update(caller=listing, callee=listing)
        with self.assertRaisesRegex(ValueError, "Live DATA overwritten"):
            self.analyze(fixture)
        fixture[4][0x200] = fixture[4][0x203] = "libc"
        fixture[5].pop(0x200); fixture[5].pop(0x203); fixture[6].pop(0x200)
        with self.assertRaisesRegex(ValueError, "Live DATA overwritten"):
            self.analyze(fixture)
        fixture[3][0x200] = b"\x12\x03\x00"
        fixture[3].update({0x300: b"\x75\x46\x69", 0x303: b"\x22"})
        fixture[4].update({0x300: "libc", 0x303: "libc"})
        fixture[-2][0x200] = fixture[-1][0x200] = 0x300
        with self.assertRaisesRegex(ValueError, "Live DATA overwritten"):
            self.analyze(fixture)
        fixture[3][0x300] = b"\x75\x47\x69"
        self.assertEqual(self.analyze(fixture)[0], 1)

    def test_overlay_extent_and_bit_address_cannot_escape_reservations(self):
        fixture = self.fixture(overwrite=True)
        fixture[2]["caller"] = b".area OSEG (OVR,DATA)\n 00004F 1 .ds 2\n"
        with self.assertRaisesRegex(ValueError, "overlay escapes"):
            self.analyze(fixture)
        fixture = self.fixture(overwrite=True)
        fixture[0]["l_BSEG"] = 47
        fixture[3][0x203] = b"\xd2\x30"
        with self.assertRaisesRegex(ValueError, "Bit access escapes"):
            self.analyze(fixture)


class JoinObservationTests(unittest.TestCase):
    def schema(self):
        schema = object.__new__(Schema)
        schema.types = {
            "__0": (Field(0, "data", 5, "ST__1:S"), Field(5, "kind", 1, "SC:U")),
            "__1": tuple(Field(0, n, 5 if n == "scan" else 1, "ST__2:S" if n == "scan" else "SC:U")
                         for n in ("scan", "association", "tx", "installed")),
            "__2": (Field(0, "body", 3, "DG,SC:U"), Field(3, "length", 2, "SI:U")),
        }
        schema.roots = lambda: {"event": ("bdb_join_event_t", Field(0, "event", 6, "ST__0:S"))}
        return schema

    def test_active_fields_and_real_pointer_are_canonicalized(self):
        schema = self.schema()
        data = b"\x23\x01\0\x04\0\x01"
        self.assertEqual(schema.canonical("event", data, 0x400, 0x123), b"\x01\0\0\x04\0\x01")
        self.assertEqual(schema.event_input(b"\x01\0\0\x04\0\x01", 0x123), data)
        with self.assertRaisesRegex(ValueError, "Invalid live public pointer"):
            schema.canonical("event", data, 0x400, 0x124)
        with self.assertRaisesRegex(ValueError, "Invalid native pointer"):
            schema.event_input(b"\x02\0\0\x04\0\x01", 0x123)

    def test_inactive_union_tail_is_not_public_state(self):
        self.assertEqual(self.schema().canonical("event", b"\x69\xa5\xa5\xa5\xa5\x02", 0, 0),
                         b"\x69\0\0\0\0\x02")

    def test_truncated_public_observation_fails_explicitly(self):
        with self.assertRaisesRegex(ValueError, "extent differs"):
            join.compare(b"\x01", b"\x01\x02", "public")

    def test_no_event_batches_never_cross_reset_or_peripheral_events(self):
        calls = [{"events": [], "reset": 1}, {"events": [], "reset": None},
                 {"events": ["aes"], "reset": None}, {"events": [], "reset": None},
                 {"events": [], "reset": 0}]
        self.assertEqual([(index, len(group)) for index, group in join.groups(calls)],
                         [(0, 2), (2, 1), (3, 1), (4, 1)])

    def test_unapproved_reference_is_rejected_before_parsing_or_execution(self):
        with patch.object(join.subprocess, "run", return_value=subprocess.CompletedProcess(
                [], 0, "DONE 415\n", "")):
            with self.assertRaisesRegex(ValueError, "reference changed"):
                join.reference("synthetic")

    def test_flash_case_requires_pinned_image_and_genuine_execution(self):
        calls = [{"case": case, "reset": 1} for case in join.CASES]
        with patch.object(sys, "argv", ["boot", "--output", "unused", "--case", join.FLASH_FAILURE]), \
                patch.object(join.layout, "load", return_value=("artifacts",)), \
                patch.object(join.layout, "verify") as verify, \
                patch.object(join, "reference", return_value=calls) as reference, \
                patch.object(join, "check_alias", side_effect=[None, ValueError("missing alias")]) as alias, \
                patch.object(join, "run", return_value=0x7b) as run, \
                patch("builtins.print"):
            join.main()
            verify.assert_called_once_with("artifacts")
            self.assertEqual(reference.call_count, 2)
            self.assertEqual(run.call_args.kwargs, {"limit": None, "failure": True})
            for peak, message in ((0x7a, "exact stack peak"), (0x7c, "exact stack peak"), (0x7d, "SP7C cap")):
                with self.subTest(peak=peak):
                    run.return_value = peak
                    alias.side_effect = [None, ValueError("missing alias")]
                    with self.assertRaisesRegex(ValueError, message):
                        join.main()
            verify.side_effect = ValueError("corrupted image")
            run.reset_mock()
            with self.assertRaisesRegex(ValueError, "corrupted image"):
                join.main()
            run.assert_not_called()

    def test_retained_flash_failure_rejects_publication_mapping_and_guard_corruption(self):
        symbols = {"_banked_depth": 0x1e, "_banked_fault": 0x1f, "_flash_write_status": 0xd1,
                   "_nv_record_diagnostic": 0x195, "_fixture_device": 0x200, "l_XSEG": 7512}
        fields = {("member",): 0, ("phase",): 1, ("work", "runtime", "transport", "ready"): 2}
        schema = Mock()
        schema.at.side_effect = lambda root, *path: Field(fields[path], "test", 1, "SC:U")
        ram, iram, sfr = bytearray(0x1f00), bytearray(256), bytearray(128)
        ram[7], ram[0xd1], ram[0x199] = 7, 10, 13
        ram[0x200:0x203] = b"\x01\x05\x00"
        iram[0x1e], iram[0x7d:] = 3, b"\xc7"*(256-0x7d)
        sfr[0x47], sfr[0x1f] = 0x0a, 1
        state = tuple(map(bytes, (ram, iram, sfr, bytearray(0x6000), bytearray(0x40000))))
        join.retained_failure(state, state, symbols, schema)
        with self.assertRaises(ValueError):
            join.retained_failure(state, None, symbols, schema)
        mutations = {
            0: (7, 0xd1, 0x199, 0x200, 0x201, 0x202, 7512, 0x1dff, 0x1e00, 0x1e26, 0x1eff),
            1: (0x1e, 0x1f, 0x7d, 0xff),
            2: (0x47, 0x1f),
            4: (0, 0x3e7ff, 0x3e800, 0x3f000, 0x3f800, 0x3ffff),
        }
        for part, offsets in mutations.items():
            for offset in offsets:
                with self.subTest(part=part, offset=offset):
                    corrupt = list(state)
                    data = bytearray(corrupt[part]); data[offset] ^= 1; corrupt[part] = bytes(data)
                    with self.assertRaises(ValueError):
                        join.retained_failure(corrupt, state, symbols, schema)
