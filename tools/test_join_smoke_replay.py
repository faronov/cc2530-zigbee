# SPDX-License-Identifier: BSD-3-Clause
import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tests"))
from boot_join_smoke import (
    CASES, PERIPHERAL_SFR, check_breakpoints, check_flash_retention, check_flash_stop,
    events_for, stops_for, validate_reference,
)
from boot_mac_attempt import mmio_sites
from join_smoke_analysis import instructions


class ReplayBoundaryTests(unittest.TestCase):
    def fixture(self):
        initial = {str(a): 0 for a in PERIPHERAL_SFR | set(range(0x6100, 0x6400))}
        return {"case": 0, "admission": "00" * 150, "draws": "00" * 64,
                "initial": initial, "nv_initial": "ff" * 4096, "peer_tx": 0,
                "aes_blocks": 0, "flash_commands": 0,
                "steps": [{"packet": "00" * 8, "events": [],
                           "status": "00" * 48, "nv": "ff" * 4096, "returned": True}]}

    def test_only_the_final_busy_flash_call_can_fail_to_return(self):
        for case in CASES:
            vector = self.fixture() | {"case": case}
            vector["steps"].append(vector["steps"][0] | {"returned": case != 10})
            validate_reference(vector)
            for index in range(2):
                bad = copy.deepcopy(vector)
                bad["steps"][index]["returned"] = not bad["steps"][index]["returned"]
                with self.subTest(case=case, index=index), self.assertRaises(ValueError):
                    validate_reference(bad)
        for case in (-1, 7, 11, True, "10"):
            with self.subTest(case=case), self.assertRaises(ValueError):
                validate_reference(self.fixture() | {"case": case})
        for value in (-1, True, "0"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                validate_reference(self.fixture() | {"flash_commands": value})

    def flash_state(self):
        ram, iram, sfr, extended = (bytearray(n) for n in (0x1f00, 256, 128, 0x6000))
        symbols = {"_flash_exec_work": 32, "_flash_exec_ram": 64, "_banked_depth": 0x20,
                   "_banked_fault": 0x21, "_flash_write_status": 200, "_nv_record_diagnostic": 210}
        template = b"\x69" * 123
        ram[32:41] = b"\x05\0\0\0\0\xd0\x07\x07\x85"
        ram[64:187] = template
        ram[200], ram[214], iram[0x20], sfr[0x47], extended[0x4270] = 10, 13, 3, 10, 0x85
        ram[0x1e06:0x1e0c] = b"\x04\0\x01\x01\0\0"
        return tuple(map(bytes, (ram, iram, sfr, extended))) + (b"\xff" * 0x40000,), symbols, template

    def test_retained_flash_requires_real_pending_owners_without_publication(self):
        state, symbols, template = self.flash_state()
        check_flash_stop(state, symbols, template)
        for space, address in ((0, 32), (0, 37), (0, 39), (0, 40), (0, 64),
                               (0, 200), (0, 214), (0, 0x1e06), (0, 0x1e08),
                               (0, 0x1e0a), (0, 0x1e0b), (1, 0x21), (2, 0x47), (3, 0x4270)):
            bad = list(state)
            changed = bytearray(bad[space])
            changed[address] ^= 1
            bad[space] = bytes(changed)
            with self.subTest(space=space, address=address), self.assertRaises(ValueError):
                check_flash_stop(tuple(bad), symbols, template)
        for depth in (0, 9):
            bad = list(state)
            changed = bytearray(bad[1])
            changed[0x20] = depth
            bad[1] = bytes(changed)
            with self.subTest(depth=depth), self.assertRaises(ValueError):
                check_flash_stop(tuple(bad), symbols, template)

    def test_ram_stop_survives_later_idle_without_cpu_or_owner_changes(self):
        state, _, _ = self.flash_state()
        check_flash_retention(state, state)
        idle = list(state)
        changed = bytearray(idle[3])
        changed[0x4270] = 4
        idle[3] = bytes(changed)
        check_flash_retention(state, tuple(idle), idle=True)
        with self.assertRaises(ValueError):
            check_flash_retention(state, tuple(idle))
        with self.assertRaises(ValueError):
            check_flash_retention(state, state, idle=True)
        for space, address in ((0, 39), (0, 0x1e06), (1, 0x20), (2, 0x47),
                               (3, 0x4271), (4, 0x3e800)):
            bad = list(idle)
            changed = bytearray(bad[space])
            changed[address] ^= 1
            bad[space] = bytes(changed)
            with self.subTest(space=space, address=address), self.assertRaises(ValueError):
                check_flash_retention(state, tuple(bad), idle=True)

    def test_discovered_provisioning_retains_its_bound_driver(self):
        state, symbols, template = self.flash_state()
        with self.assertRaises(ValueError):
            check_flash_stop(state, symbols, template, discovery=True)
        ram = bytearray(state[0])
        ram[0x1e06:0x1e0c] = b"\x04\0\x04\x01\x01\0"
        state = (bytes(ram),) + state[1:]
        check_flash_stop(state, symbols, template, discovery=True)
        with self.assertRaises(ValueError):
            check_flash_stop(state, symbols, template)
        for offset in range(0x1e06, 0x1e0c):
            bad = bytearray(ram)
            bad[offset] ^= 1
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                check_flash_stop((bytes(bad),) + state[1:], symbols, template, discovery=True)

    def test_initial_inputs_cannot_write_cpu_or_private_ram(self):
        vector = self.fixture()
        validate_reference(vector)
        for address in (3, 0x81, 0x9f, 0xd0, 0x1e00, 0x1f00, 0x3f800):
            bad = copy.deepcopy(vector)
            bad["initial"][str(address)] = 0
            with self.subTest(address=address), self.assertRaises(ValueError):
                validate_reference(bad)
        for field, value in (("admission", "00"), ("draws", "gg" * 64),
                             ("nv_initial", "ff" * 4095)):
            with self.subTest(field=field), self.assertRaises(ValueError):
                validate_reference(vector | {field: value})

    def test_events_cannot_write_cpu_registers_or_fake_private_progress(self):
        for event in (["w", 0x81, 0x7c, [], None, None],
                      ["r", 0x1e06, 5, [], None, None],
                      ["r", 0x95, 0, [[0x9f, 1]], None, None],
                      ["r", 0x95, 0, [[0xc7, 8]], None, None]):
            vector = self.fixture()
            vector["steps"][0]["events"] = [event]
            with self.subTest(event=event), self.assertRaises(ValueError):
                list(events_for(vector))

    def test_passive_reads_require_the_real_mapped_media_and_cannot_inject_dma(self):
        vector = self.fixture()
        vector["steps"][0]["events"] = [["r", 0xe800, 255, [], None, None]]
        self.assertEqual(list(events_for(vector))[0]["events"], [])
        for event in (["r", 0xe800, 0, [], None, None],
                      ["r", 0xbe, 4, [], [1, "00" * 16, "00" * 16], None],
                      ["r", 0xbe, 4, [[0xc7, 2]], None, None]):
            vector["steps"][0]["events"] = [event]
            with self.subTest(event=event), self.assertRaises(ValueError):
                list(events_for(vector))
        vector = self.fixture()
        vector["nv_initial"] = vector["steps"][0]["nv"] = "42" + "ff" * 4095
        vector["steps"][0]["events"] = [["r", 0xe800, 0x42, [], None, None]]
        self.assertEqual(list(events_for(vector))[0]["events"], [])

    def test_same_logical_pc_uses_one_combined_condition_for_code_banks_and_xmap(self):
        symbols = {f"_join_smoke_{n}": a for n, a in (("wait", 0x100), ("ready", 0x110), ("fault", 0x120))}
        symbols["_banked_stop"] = 0x130
        commands = stops_for({0x185e5: ("r", 0x95, 0xe0),
                             0x785e5: ("r", 0x96, 0xe0),
                             0x85e5: ("r", None, 0xe0)}, symbols)
        shared = [c for c in commands if c.startswith("break 0x85e5 ")]
        self.assertEqual(len(shared), 1)
        self.assertIn(' 1 if "', shared[0])
        self.assertIn("||", shared[0])
        self.assertIn("sfr[0x9f]==1", shared[0])
        self.assertIn("sfr[0x9f]==7", shared[0])
        self.assertIn("(sfr[0xc7]&8)!=0", shared[0])
        self.assertFalse(any(c.startswith("memory ") for c in commands))
        commands = stops_for({0x185e5: ("r", 0x95, 0xe0)}, symbols, ram_stop=0x85e5)
        shared = [c for c in commands if c.startswith("break 0x85e5 ")]
        self.assertEqual(len(shared), 1)
        self.assertIn("(sfr[0xc7]&8)!=0", shared[0])
        self.assertIn("||", shared[0])
        with self.assertRaises(ValueError):
            stops_for({}, symbols, ram_stop=0x9e00)

    def test_usage_output_is_not_successful_breakpoint_installation(self):
        check_breakpoints("Breakpoint 1 at 0x000100: nop\n", ["break 0x100"])
        for text in ("break addr [hit [if expr]]\n", "", "Breakpoint 1 at 0x000101: nop\n"):
            with self.subTest(text=text), self.assertRaises(ValueError):
                check_breakpoints(text, ["break 0x100"])
        with self.assertRaises(ValueError):
            check_breakpoints("Breakpoint 1 at 0x000100: nop\n", ["break 0x100", "break 0x100"])

    def test_complete_reader_keeps_reconfiguration_after_five_digit_listing_lines(self):
        image, text = {}, ""

        def ins(pc, raw, line=1):
            nonlocal text
            text += f" {pc:06X} {raw.hex(' ').upper():<12} [24]{line:5d} instruction\n"
            image.update({pc+i: value for i, value in enumerate(raw)})

        for name, pc in (("setting_address", 0x18100), ("read_fifo", 0x18200),
                         ("cca_settle", 0x18210), ("receive_head", 0x18220)):
            text += f" {pc:06X} 1 _{name}:\n"
        for pc in (0x18000, 0x18030, 0x18070):
            ins(pc, bytes.fromhex("128100"), 10687 if pc == 0x18070 else 1)
        ins(0x1800d, b"\xe0")
        ins(0x1804f, b"\xf0")
        pc = 0x18073
        for raw in ("ab82", "ac83", "d005", "8b82", "8c83", "ed", "f0"):
            data = bytes.fromhex(raw)
            ins(pc, data, 10688)
            pc += len(data)
        ins(0x18200, bytes.fromhex("85d982"))
        ins(0x18203, b"\x22")
        for i in range(4):
            ins(0x18210+i, b"\0")
        ins(0x18214, b"\x22")
        for pc, raw in ((0x18250, "128200"), (0x18260, "128220"), (0x18270, "128220"),
                        (0x18280, "128210"), (0x18290, "128210")):
            ins(pc, bytes.fromhex(raw))
        listings = {m: "" for m in ("timebase", "clock", "mac_time")} | {"radio_autoack": text}
        with self.assertRaisesRegex(ValueError, "indexed configuration"):
            mmio_sites(image, "", listings)
        reader = lambda value: [(a, raw) for a, (raw, _) in instructions(value).items()]
        complete = mmio_sites(image, "", listings, reconfigure=True, instruction_records=reader)
        self.assertEqual(complete, mmio_sites(image, "", listings, reconfigure=True))
        self.assertEqual(complete[0x1807e], ("w", None, None))
        with self.assertRaisesRegex(ValueError, "handoff changed"):
            mmio_sites(image | {0x18077: 0}, "", listings, reconfigure=True, instruction_records=reader)


if __name__ == "__main__":
    unittest.main()
