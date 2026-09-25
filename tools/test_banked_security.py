# SPDX-License-Identifier: BSD-3-Clause
"""Pure parser/ownership/continuation negatives; no tools or hardware invoked."""
import sys
import unittest
from unittest.mock import patch

from verify_firmware import ROOT
sys.path.insert(0, str(ROOT / "tests"))

import boot_banked_security as lifecycle
from boot_banked_security import restore
from boot_banked import normalized_object, pin_artifacts, sha
from boot_nwk_candidates import records
from boot_security_resident import active_frames, pin_cdb, _pinned_cdb
from verify_banked_security import EDGES, FRAMES, wire_work


def wire_work_fixture():
    """Synthetic CDB/instructions only; no compiler or target image is read."""
    fields = (
        ((0, "nwk", 29), (29, "aps", 12), (41, "transmit", 27), (68, "application", 10)),
        ((0, "nonce", 13), (13, "written", 1), (14, "info", 26)),
        ((0, "wire", 116), (116, "body", 120)),
        ((0, "header", 116), (0, "frame", 116)),
        ((0, "encoded", 116), (0, "packet", 120), (0, "text", 116)),
    )
    debug = []
    for number, members in enumerate(fields, 11):
        text = "".join(f"({{{offset}}}S:S${name}$0_0$0({{{size}}}SC:U),Z,0,0)"
                       for offset, name, size in members)
        debug.append(f"T:Fed_wire$__{number:08d}[{text}]")
    for name, size, number, address in (("syntax", 78, 11, 0x100), ("crypto", 40, 12, 0x14e),
                                        ("buffers", 236, 13, 0x176)):
        key = f"Fed_wire${name}$0_0$0"
        debug.extend((f"S:{key}({{{size}}}ST__{number:08d}:S),F,0,0", f"L:{key}:{address:X}"))
    edges = {
        "counter_value": (), "wipe": (), "finish": ("wipe",),
        "read_nwk": (), "read_aps": (), "header": ("read_nwk", "read_aps"),
        "inspect": ("header", "counter_value"),
        "ed_wire_nwk": ("read_nwk", "finish"), "ed_wire_aps": ("read_aps", "finish"),
        "ed_wire_decode": ("read_nwk", "read_aps", "finish"),
        "ed_wire_encode": ("read_aps", "finish"), "ed_wire_inspect": ("inspect", "finish"),
        "ed_wire_crypt": ("inspect", "header", "finish"),
    }
    entries = {name: 0x28000+64*i for i, name in enumerate(edges)}
    decoded = {}
    for name, targets in edges.items():
        pc = entries[name]
        prefix = ("G$" if name.startswith("ed_wire_") else "Fed_wire$")+name+"$0$0"
        debug.append(f"L:{prefix}:{pc:X}")
        for target in targets:
            decoded[pc] = b"\x12"+(entries[target] & 0xffff).to_bytes(2, "big")
            pc += 3
        debug.append(f"L:X{prefix}:{pc:X}")
        if name in ("finish", "header"):
            decoded[pc] = b"\xf5\x82"; pc += 2
        decoded[pc] = b"\x22"
    return "\n".join(debug), decoded, dict.fromkeys(decoded, "ed_wire")


class BankedSecurityTests(unittest.TestCase):
    def test_resident_raw_pin_compares_complete_immutable_content(self):
        raw = b"F:synthetic\nS:synthetic\nL:synthetic\nT:synthetic\n"
        digest = sha(raw)
        with patch.dict(_pinned_cdb, clear=True):
            with self.assertRaises(ValueError):
                pin_cdb(raw + b"!", digest)
            self.assertNotIn(digest, _pinned_cdb)
            pin_cdb(raw, digest)
            copied = bytes(bytearray(raw))
            self.assertIsNot(copied, raw)
            pin_cdb(copied, digest)
            for changed in (b"", raw[:-1], raw + b"\n", raw.replace(b"L:", b"X:"),
                            raw[:-1] + b"!", bytearray(raw)):
                with self.subTest(changed=changed), self.assertRaises(ValueError):
                    pin_cdb(changed, digest)
            self.assertEqual(_pinned_cdb[digest], raw)

    def test_artifact_campaign_is_explicit_and_complete_for_current_image(self):
        with patch.object(lifecycle.layout, "artifact_bytes", return_value=()), \
                patch.object(lifecycle.banking, "artifact_negatives", return_value=246199) as negative:
            self.assertEqual(lifecycle.artifact_campaign((), "full"), "246199 artifact negatives")
            negative.assert_called_once()
            negative.reset_mock()
            self.assertIn("explicitly deferred", lifecycle.artifact_campaign((), "deferred"))
            negative.assert_not_called()
            for count in (246198, 246200, 246323):
                negative.return_value = count
                with self.subTest(count=count), self.assertRaises(ValueError):
                    lifecycle.artifact_campaign((), "full")
        with self.assertRaises(ValueError):
            lifecycle.artifact_campaign((), "unknown")

    def test_deferred_campaign_still_checks_image_files_alias_execution_and_busy_failure(self):
        with patch.object(sys, "argv", ["boot", "--output", "unused", "--artifact-campaign", "deferred"]), \
                patch.object(lifecycle.layout, "load", return_value=("artifacts",)), \
                patch.object(lifecycle.layout, "verify") as verify, \
                patch.object(lifecycle.banking, "verify_files") as files, \
                patch.object(lifecycle.banking, "artifact_negatives", side_effect=AssertionError("deferred")), \
                patch.object(lifecycle, "reference", return_value=["calls"]) as reference, \
                patch.object(lifecycle, "check_alias") as alias, \
                patch.object(lifecycle, "run", return_value=(0x77, 10345)) as run, \
                patch("builtins.print"):
            lifecycle.main()
            verify.assert_called_once_with("artifacts")
            files.assert_called_once()
            self.assertEqual(reference.call_count, 2)
            alias.assert_called_once_with("s51")
            self.assertEqual(run.call_count, 2)
            self.assertEqual(run.call_args.kwargs, {"failure": True})
            verify.side_effect = ValueError("Corrupted immutable image")
            run.reset_mock()
            with self.assertRaisesRegex(ValueError, "Corrupted immutable"):
                lifecycle.main()
            run.assert_not_called()

    def test_wire_real_union_layout_and_all_internal_lifetimes(self):
        debug, decoded, owners = wire_work_fixture()
        wire_work(debug, decoded, owners)
        for old, new in (("{116}S:S$body", "{115}S:S$body"),
                         ("{0}S:S$text", "{1}S:S$text"),
                         ("{29}S:S$aps", "{28}S:S$aps"),
                         ("$buffers$0_0$0({236}", "$buffers$0_0$0({235}"),
                         ("$crypto$0_0$0:14E", "$crypto$0_0$0:14D"),
                         ("$buffers$0_0$0:176", "$buffers$0_0$0:1DF0")):
            self.assertIn(old, debug)
            with self.subTest(new=new), self.assertRaises(ValueError):
                wire_work(debug.replace(old, new), decoded, owners)
        # A nested parser wiping work would destroy a live encode/decode
        # buffer. A call to its parent's body would be equally invalid.
        read_nwk, wipe, finish = 0x280c0, 0x28040, 0x28080
        changed = dict(decoded); changed[read_nwk] = b"\x12\x80\x40"
        changed[read_nwk+3] = b"\x22"
        owner = dict(owners); owner[read_nwk+3] = "ed_wire"
        nested = debug.replace("L:XFed_wire$read_nwk$0$0:280C0",
                               "L:XFed_wire$read_nwk$0$0:280C3")
        with self.assertRaisesRegex(ValueError, "lifetime edges"):
            wire_work(nested, changed, owner)
        for pc, raw in ((finish, b"\x12\x80\x41"),
                        (finish+3, b"\x00"), (0x2f000, b"\x22")):
            changed = dict(decoded); changed[pc] = raw
            owner = dict(owners); owner[pc] = "ed_wire"
            with self.subTest(pc=pc, raw=raw), self.assertRaises(ValueError):
                wire_work(debug, changed, owner)
        changed = dict(owners); changed[wipe] = "security_keys"
        with self.assertRaises(ValueError):
            wire_work(debug, decoded, changed)

    def test_complete_wire_named_work_is_checked_for_wiping(self):
        debug = "\n".join(f"L:F{module}${name}$0_0$0:{index*0x400:X}\n"
                          f"S:F{module}${name}$0_0$0({{{size}}}DA{size}d,SC:U),F,0,0"
                          for index, (module, name, size) in enumerate((
                              ("security_keys", "w", 617), ("ccm_star", "state", 267),
                              ("zigbee_mmo", "state", 89), ("zigbee_key_hash", "state", 60),
                              ("ed_wire", "crypto", 40), ("ed_wire", "syntax", 78),
                              ("ed_wire", "buffers", 236))))
        regions = lifecycle.wiped_regions(debug)
        self.assertEqual([size for _, _, size in regions], [617, 267, 89, 60, 40, 78, 236])
        for name in ("crypto", "syntax", "buffers"):
            with self.subTest(name=name), self.assertRaises(ValueError):
                lifecycle.wiped_regions(debug.replace(f"${name}$", "$missing$"))

    def test_data_only_object_keeps_its_nonoptional_format_header(self):
        raw = b"XH3\nH 1A areas 3 global symbols\n"
        self.assertEqual(normalized_object(raw), raw)
        self.assertEqual(normalized_object(b";!FILE arbitrary/path.asm\n"+raw), raw)
        for changed in (b"XL3"+raw[3:], raw[1:], b"junk\n"+raw, raw.replace(b"\n", b"\r\n"),
                        b";!FILE no-newline"):
            with self.assertRaises(ValueError):
                normalized_object(changed)

    def test_only_hash_approved_immutable_artifacts_can_seed_exact_comparison(self):
        raw = tuple((str(i)+"\n").encode("ascii") for i in range(6))
        pins = tuple(map(sha, raw))
        changed = raw[:2]+(b"bad",)+raw[3:]
        with self.assertRaises(ValueError):
            pin_artifacts(changed, pins)
        pin_artifacts(raw, pins)
        for index in range(6):
            for replacement in (raw[index]+b"x", raw[index].replace(b"\n", b"\r\n"), bytearray(raw[index])):
                with self.assertRaises(ValueError):
                    pin_artifacts(raw[:index]+(replacement,)+raw[index+1:], pins)
        pin_artifacts(tuple(bytes(bytearray(value)) for value in raw), pins)

    def test_five_digit_listing_lines_have_no_gap_after_cycle_bracket(self):
        text = ("      01B00A 02 B2 B4         [24] 9905 ljmp 00148$\n"
                "      01B2B4 90 0D 87         [24]10364 mov dptr,#0x0d87\n"
                "      01B2B7 E0               [24]10365 movx a,@dptr\n"
                "      01B2B8 FF               [12]10366 mov r7,a\n")
        self.assertEqual(records(text), [(0x1b00a, b"\x02\xb2\xb4"), (0x1b2b4, b"\x90\x0d\x87"),
                                         (0x1b2b7, b"\xe0"), (0x1b2b8, b"\xff")])

    def test_reused_data_rejects_new_active_edges_and_recursion(self):
        frames = {m: (a, n) for m, (_, a, n) in FRAMES.items()}
        active_frames(EDGES, frames)
        for edge in (("ed_wire", "security_counter"), ("nv_record", "aes"),
                     ("aes", "security_keys"), ("security_counter", "security_counter")):
            with self.subTest(edge=edge), self.assertRaises(ValueError):
                active_frames(EDGES | {edge}, frames)

    def test_continuations_reject_missing_state_live_timer_uart_and_xmap(self):
        sfr = bytearray(128); sfr[0x47] = 2
        state = (bytes(0x1f00), bytes(256), bytes(sfr), bytes(0x6000), bytes(0x40000))
        self.assertTrue(restore(state, 0x1234)[-1].endswith("0x1234"))
        for index, value in ((0x19, 1), (0x18, 2), (8, 0x10), (8, 0x40), (0x48, 4), (0x47, 10),
                             (0x28, 0x80), (0x38, 1), (0x1a, 1), (0x51, 1), (0x56, 1), (0x57, 1)):
            changed = bytearray(sfr); changed[index] = value
            with self.assertRaises(ValueError):
                restore((state[0], state[1], bytes(changed), state[3], state[4]), 0x1234)
        for index in range(5):
            changed = list(state); changed[index] = changed[index][1:]
            with self.assertRaises(ValueError):
                restore(changed, 0x1234)
        for pc in (-1, 0x8000, 0x18000, True):
            with self.assertRaises(ValueError):
                restore(state, pc)


if __name__ == "__main__":
    unittest.main()
