# SPDX-License-Identifier: BSD-3-Clause
import unittest
from unittest.mock import patch

from join_smoke_stack import analyze_stack, instruction_depth, special_entries, stack_paths, startup
from boot_security_resident import LENGTHS, branch


def fixture(bodies, *, far=(), runtime=(), indirect=None):
    symbols = {"__sdcc_banked_call": 0x700, "__sdcc_banked_ret": 0x780}
    decoded, owners, functions, entries, targets, calls = {}, {}, {}, {}, {}, {}
    for entry, raws in bodies.items():
        entries[entry] = ("m", f"f{entry:x}", entry in far)
        pc = entry
        for raw in raws:
            decoded[pc], owners[pc], functions[pc] = raw, "m", entry
            pc += len(raw)
    for pc, raw in decoded.items():
        target = branch(pc, raw)
        if target is not None:
            if target == symbols["__sdcc_banked_call"]:
                target = next(iter(far))
            targets[pc] = target
            if raw[0] == 0x12 or raw[0] & 31 == 17:
                calls[pc] = target
    for entry in runtime:
        name = f"_runtime{entry:x}"
        symbols[name] = entry
        del entries[entry]
        for pc in list(functions):
            if functions[pc] == entry:
                owners[pc] = "libc"
                del functions[pc]
    graph = decoded, owners, functions, entries, targets, calls
    return symbols, graph, tuple(f"_runtime{x:x}" for x in runtime), set(), set(), indirect or {}


class StackPathsTests(unittest.TestCase):
    def test_near_acall_and_banked_returns_count_real_resident_bytes(self):
        near = fixture({0x100: [b"\xc0\xe0", b"\x12\x02\0", b"\xd0\xe0", b"\x22"],
                        0x200: [b"\xc0\xf0", b"\xd0\xf0", b"\x22"]})
        self.assertEqual(stack_paths(*near)[0x100][:2], (4, 0))
        acall = fixture({0x100: [b"\x51\0", b"\x22"], 0x200: [b"\x22"]})
        self.assertEqual(stack_paths(*acall)[0x100][:2], (2, 0))
        far = fixture({0x100: [b"\x12\x07\0", b"\x22"],
                       0x200: [b"\xc0\xe0", b"\x12\x03\0", b"\xd0\xe0", b"\x02\x07\x80"],
                       0x300: [b"\xc0\xf0", b"\xd0\xf0", b"\x22"]}, far=(0x200,))
        self.assertEqual(stack_paths(*far)[0x100][:2], (7, 1))

    def test_branch_maximum_and_balanced_loop(self):
        data = fixture({0x100: [b"\x60\x06", b"\xc0\xe0", b"\xd0\xe0", b"\x80\xf8", b"\x22"]})
        self.assertEqual(stack_paths(*data)[0x100][0], 1)
        for body in ([b"\xc0\xe0", b"\x80\xfc"], [b"\x60\x02", b"\xc0\xe0", b"\x22"],
                     [b"\xd0\xe0", b"\x22"], [b"\xc0\xe0", b"\x22"]):
            with self.subTest(body=body), self.assertRaises(ValueError):
                stack_paths(*fixture({0x100: body}))

    def test_recursion_missing_fallthrough_and_other_indirect_transfers_fail(self):
        for body in ([b"\x12\x01\0", b"\x22"], [b"\x00"], [b"\x73"], [b"\x32"]):
            with self.subTest(body=body), self.assertRaises(ValueError):
                stack_paths(*fixture({0x100: body}))
        with self.assertRaisesRegex(ValueError, "Recursive"):
            stack_paths(*fixture({0x100: [b"\x12\x02\0", b"\x22"],
                                  0x200: [b"\x12\x01\0", b"\x22"]}))

    def test_runtime_is_analyzed_from_its_actual_bytes(self):
        data = fixture({0x100: [b"\x12\x02\0", b"\x22"],
                        0x200: [b"\xc0\xe0", b"\x12\x03\0", b"\xd0\xe0", b"\x22"],
                        0x300: [b"\xc0\xf0", b"\xd0\xf0", b"\x22"]}, runtime=(0x200, 0x300))
        self.assertEqual(stack_paths(*data)[0x100][0], 6)

    def test_known_flash_template_tail_has_no_added_return_address(self):
        data = fixture({0x100: [b"\x12\x02\0", b"\x22"], 0x200: [b"\x73"],
                        0x300: [b"\xc0\xe0", b"\xd0\xe0", b"\x22"]}, indirect={0x200: 0x300})
        self.assertEqual(stack_paths(*data)[0x100][0], 3)
        with self.assertRaisesRegex(ValueError, "Unbalanced flash"):
            stack_paths(*fixture({0x200: [b"\xc0\xe0", b"\x73"], 0x300: [b"\x22"]},
                                 indirect={0x202: 0x300}))

    def test_terminal_paths_do_not_require_an_unwind_or_hide_fallthrough(self):
        data = fixture({0x100: [b"\xc0\xe0", b"\x02\x07\xf0"]})
        data[4].add(0x7f0)
        self.assertEqual(stack_paths(*data)[0x100][0], 1)
        data = fixture({0x100: [b"\x60\x06", b"\xc0\xe0", b"\xd0\xe0", b"\x80\xf8"]})
        data[4].add(0x108)
        self.assertEqual(stack_paths(*data)[0x100][0], 1)

    def test_ordinary_far_return_and_nonzero_far_return_depth_fail(self):
        for body in ([b"\x22"], [b"\xc0\xe0", b"\x02\x07\x80"]):
            with self.subTest(body=body), self.assertRaises(ValueError):
                stack_paths(*fixture({0x100: body}, far=(0x100,)))

    def test_unclassified_sp_and_interrupt_writes_fail_closed(self):
        for raw in (b"\x75\x81\x50", b"\xc0\x81", b"\xd0\x81", b"\xc5\x81",
                    b"\x85\xe0\x81", b"\x88\x81", b"\x43\x81\x01",
                    b"\xd2\xaf", b"\xb2\xa9", b"\x92\xaf", b"\x75\xa8\x80",
                    b"\xf5\xb8", b"\xd0\x9a", b"\x32"):
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                instruction_depth(raw, 1)
        for raw in (b"\xc2\xaf", b"\x75\xa8\0", b"\x75\xb8\0", b"\x75\x9a\0",
                    b"\x10\xaf\0", b"\xe5\x81"):
            self.assertEqual(instruction_depth(raw, 0), 0)
        self.assertEqual(instruction_depth(b"\x05\x81", 0), 1)
        self.assertEqual(instruction_depth(b"\x15\x81", 1), 0)

    def test_exact_45_byte_root_boundary_and_startup_return_address(self):
        symbols = {"_main": 0x100, "__sdcc_external_startup": 0x200}
        with patch("join_smoke_stack.startup"), \
                patch("join_smoke_stack.special_entries", return_value=(set(), set(), {})), \
                patch("join_smoke_stack.stack_paths") as paths:
            paths.return_value = {0x100: (45, 8, []), 0x200: (2, 0, [])}
            result = analyze_stack({}, symbols, None, ())
            self.assertEqual(result["maximum_sp"], 0x7c)
            self.assertEqual(result["roots"]["__sdcc_external_startup"]["bytes"], 4)
            for main, external in (((46, 8, []), (2, 0, [])), ((45, 9, []), (2, 0, [])),
                                   ((45, 8, []), (44, 0, []))):
                paths.return_value = {0x100: main, 0x200: external}
                with self.subTest(main=main, external=external), self.assertRaises(ValueError):
                    analyze_stack({}, symbols, None, ())


class StartupTests(unittest.TestCase):
    def fixture(self):
        symbols = {"s_SSEG": 0x50, "l_SSEG": 45, "s_XSEG": 0, "l_XSEG": 0x1dd9,
                   "s_XISEG": 0x1dd9, "l_XISEG": 0, "s_XINIT": 0x7cdd, "l_XINIT": 0,
                   "l_PSEG": 0, "s_HOME": 0, "l_HOME": 52, "___gptr_cmp": 6,
                   "_main": 0x748e, "__sdcc_external_startup": 0x68dd,
                   "__sdcc_gsinit_startup": 52, "s_CSEG": 0x90}
        image = dict(enumerate(bytes.fromhex("02003402748e")))
        areas = {
            "GSINIT0": "75814f", "GSINIT1": "", "GSINIT2": "1268dde5826003020003",
            "GSINIT3": "7900e94400601b7a00907cdd78d975931de493f2a308b800020593d9f4daf27593ff",
            "GSINIT4": "e478fff6d8fd7800e84400600a7900759300e4f309d8fc78d9e8441d600c791e900000e4f0a3d8fcd9fa",
            "GSINIT5": "", "GSINIT": "", "GSFINAL": "020003",
        }
        address = 52
        for area, text in areas.items():
            raw = bytes.fromhex(text)
            symbols["s_" + area], symbols["l_" + area] = address, len(raw)
            image.update({address + i: v for i, v in enumerate(raw)})
            address += len(raw)
        image.update({0x68dd + i: v for i, v in enumerate(bytes.fromhex("75a80075b800759a00"))})
        return image, symbols

    def test_exact_startup_and_every_bound_byte_mutation(self):
        image, symbols = self.fixture()
        startup(image, symbols)
        for address, value in image.items():
            with self.subTest(address=address), self.assertRaises(ValueError):
                startup(image | {address: value ^ 1}, symbols)
        for key, value in (("l_SSEG", 46), ("l_XSEG", 0x1e01), ("l_XINIT", 1),
                           ("l_GSINIT1", 1), ("s_GSFINAL", 0x8c)):
            with self.subTest(symbol=key), self.assertRaises(ValueError):
                startup(image, symbols | {key: value})


class SpecialEntryTests(unittest.TestCase):
    def fixture(self):
        symbols = {"_banked_depth": 0x1e, "_banked_fault": 0x1f,
                   "_banked_fault_entry": 0x90, "_banked_stop": 0x94,
                   "__sdcc_banked_call": 0x96, "__sdcc_banked_ret": 0x109,
                   "_flash_exec_template_end": 0x234e, "_flash_exec_work": 0x581,
                   "_flash_exec_ram": 0x58a}
        bodies = (
            ("banked", "stop", 0x90, "c2aff51f80fe"),
            ("banked", "_sdcc_banked_call", 0x96,
             "abd0ac81bc5100405bbc7a005056c0e0c003e5d054187042e592703ee5c754f87038"
             "e59f54f87032e51ec394085030ea603754f87033e930e72fba0705c394e85027051e"
             "d0d0d0e0c09fc0e08a9fe59f6a701bd0e0c000c0012274020200907403020090"
             "740402009074010200907405020090"),
            ("banked", "_sdcc_banked_ret", 0x109,
             "abd0ac81bc52004045bc7b005040c0e0c003e5d05418702ce5927028e5c754f87022"
             "e51e6023c39409501e151ed0d0d0e0d000c0e0e854f87019889fe59f687012d0e022"
             "7402020090740302009074040200907405020090"),
            ("flash_exec", "flash_exec_template", 0x22d3,
             "8a828b83e0f5f0a3e0fca3e0fda3e0fea3e0ffa3e0f8a3e0f9a3aa82ab83906270"
             "e5f0f0e065f0b48012e5f030e10f906273ecf0edf0eef0eff08002d2f7906270e0fc"
             "54836015e870011918e84970f18a828b837407f0a3ecf080feec20e50ce5f020e70b"
             "540c6c700680067405800274068a828b83f0a3ecf022"),
            ("flash_exec", "enter_ram", 0x234e, "7a817b0590858ae473"),
        )
        image, decoded, owners, functions, entries = {}, {}, {}, {}, {}
        for module, name, entry, text in bodies:
            raw = bytes.fromhex(text)
            image.update({entry + i: v for i, v in enumerate(raw)})
            entries[entry] = module, name, False
            offset = 0
            while offset < len(raw):
                size = LENGTHS[raw[offset]]
                pc = entry + offset
                decoded[pc], owners[pc], functions[pc] = raw[offset:offset + size], module, entry
                offset += size
        return image, symbols, (decoded, owners, functions, entries, {}, {})

    def test_exact_abi_and_every_trampoline_flash_byte(self):
        image, symbols, graph = self.fixture()
        self.assertEqual(special_entries(image, symbols, graph),
                         ({0x90, 0x96, 0x109}, {0x90, 0x94}, {0x2356: 0x22d3}))
        for address, value in image.items():
            with self.subTest(address=address), self.assertRaises(ValueError):
                special_entries(image | {address: value ^ 1}, symbols, graph)
        for name in symbols:
            with self.subTest(symbol=name), self.assertRaises(ValueError):
                special_entries(image, symbols | {name: symbols[name] + 1}, graph)


if __name__ == "__main__":
    unittest.main()
