# SPDX-License-Identifier: BSD-3-Clause
"""Original synthetic timebase records and relocated linked-code fixtures."""

from pathlib import Path
import unittest
from unittest.mock import patch

import debug_image
from debug_image import decode_timebase_fixture
from verify_firmware import (
    IMAGES, TIMEBASE_CHECKPOINTS, TIMEBASE_READER_BYTES, verify_layout, verify_timebase_fixture_code,
)
import test_m0_artifacts


def timebase_record(*, start=0xffff80, end=0, polls=2, cycles=1, phase=3, reason=0, helper=0):
    data = bytearray(b"M2TM\x01\x20" + bytes((phase, reason, cycles, helper)))
    for value in (start, end, (start + 128) & 0xffffff, (end - start) & 0xffffff):
        data.extend(value.to_bytes(3, "little"))
    data.extend(polls.to_bytes(2, "little") + b"\x80\0\0\x04\0\0\x69\x96")
    if phase == 1:
        data[7:24] = b"\0" * 17
    return bytes(data)


class TimebaseRecordTests(unittest.TestCase):
    def test_ready_initial_and_fault_records(self):
        ready = decode_timebase_fixture(timebase_record())
        self.assertEqual((ready["start"], ready["end"], ready["deadline"], ready["elapsed"]), (0xffff80, 0, 0, 128))
        self.assertEqual(decode_timebase_fixture(timebase_record(phase=1))["phase"], 1)
        for end, polls, reason, helper in ((0x100, 1024, 4, 0), (0xff, 1, 3, 0), (0x800180, 1, 2, 2)):
            record = timebase_record(start=0x100, end=end, polls=polls, phase=4, reason=reason, helper=helper, cycles=0)
            self.assertEqual(decode_timebase_fixture(record)["reason"], reason)

    def test_invalid_shapes_statuses_bounds_and_guards(self):
        valid = timebase_record()
        for data in (valid[:-1], valid + b"\0", bytearray(valid), None):
            with self.subTest(data=type(data)), self.assertRaises(ValueError):
                decode_timebase_fixture(data)
        for index, value in ((0, 0), (4, 2), (5, 31), (6, 2), (6, 0), (6, 5),
                             (7, 1), (9, 1), (9, 3), (10, 0), (13, 1), (16, 1), (19, 127),
                             (22, 0), (23, 5), (24, 129), (26, 1), (28, 1), (29, 1), (30, 0), (31, 0)):
            changed = bytearray(valid)
            changed[index] = value
            if index == 22:
                changed[23] = 0
            with self.subTest(index=index, value=value), self.assertRaises(ValueError):
                decode_timebase_fixture(bytes(changed))
        for elapsed in (0, 127, 0x800000, 0xffffff):
            with self.subTest(elapsed=elapsed), self.assertRaises(ValueError):
                decode_timebase_fixture(timebase_record(start=0, end=elapsed))
        for reason, helper, polls in ((0, 0, 1), (6, 0, 1), (1, 0, 0), (1, 1, 1),
                                      (2, 0, 1), (2, 2, 0), (3, 1, 1), (4, 0, 1023)):
            with self.subTest(reason=reason, helper=helper, polls=polls), self.assertRaises(ValueError):
                decode_timebase_fixture(timebase_record(phase=4, reason=reason, helper=helper, polls=polls))
        changed = bytearray(timebase_record(phase=1))
        changed[8] = 1
        with self.assertRaises(ValueError):
            decode_timebase_fixture(bytes(changed))

    def test_offline_cli_selects_only_the_new_image(self):
        from contextlib import redirect_stdout, redirect_stderr
        from io import StringIO
        import json
        for image_name in IMAGES:
            with self.subTest(image=image_name), patch.object(debug_image, "DebugImage") as constructor:
                image = constructor.return_value
                image.board, image.image_name, image.sha256 = "generic", image_name, "synthetic"
                output, error = StringIO(), StringIO()
                with redirect_stdout(output), redirect_stderr(error):
                    result = debug_image.main(["timebase-state", "--output", "unused", "--board", "generic",
                                               "--image", image_name, "--hex", timebase_record().hex()])
                if image_name == "timebase_fixture":
                    self.assertEqual(result, 0)
                    self.assertEqual(json.loads(output.getvalue())["timebase_state"]["elapsed"], 128)
                else:
                    self.assertEqual(result, 1)
                    self.assertEqual(output.getvalue(), "")
                    self.assertIn("requires a timebase_fixture", error.getvalue())


class TimebaseCodeTests(unittest.TestCase):
    def setUp(self):
        names = TIMEBASE_CHECKPOINTS + ("_main", "_timebase_fixture_initialize",
                                      "_timebase_fixture_begin", "_timebase_fixture_poll", "_timebase_expired")
        self.symbols = {name: 0x300 + index * 4 for index, name in enumerate(names)}
        self.symbols.update(_timebase_read_awake_ticks24=0x100,
                            _timebase_deadline_after=0x100 + len(TIMEBASE_READER_BYTES),
                            _timebase_fixture_state=0)
        for i in range(3):
            self.symbols[f"_SOC_ST{i}"] = 0x95 + i
        for area, length in (("XSEG", 0x80), ("XISEG", 0), ("PSEG", 0)):
            self.symbols[f"s_{area}"], self.symbols[f"l_{area}"] = 0, length
        self.image = {address: 0x22 for name, address in self.symbols.items() if name.startswith("_")}
        reader = bytearray(TIMEBASE_READER_BYTES)
        for offset, byte in ((0, 0), (6, 1), (12, 2), (18, 0), (29, 1), (56, 2)):
            reader[offset + 1:offset + 3] = (0x40 + byte).to_bytes(2, "big")
        self.image.update({0x100 + offset: byte for offset, byte in enumerate(reader)})
        for name, code in zip(TIMEBASE_CHECKPOINTS, (b"\0\x22", b"\0\x22", b"\0\x80\xfd")):
            self.image.update({self.symbols[name] + offset: byte for offset, byte in enumerate(code)})
        self.debug = "L:C$timebase.c$16$1_0$5:100\nL:C$timebase_fixture_state.c$1$0_0$0:300\n"
        for offset, name in enumerate(("low", "middle", "high")):
            key = f"Ltimebase.timebase_read_awake_ticks24${name}$1_0$5"
            self.debug += f"S:{key}({{1}}SC:U),F,0,0\nL:{key}:{0x40 + offset:X}\n"

    def verify(self, image=None, symbols=None, debug=None):
        verify_timebase_fixture_code(self.image if image is None else image,
                                     self.symbols if symbols is None else symbols,
                                     self.debug if debug is None else debug)

    def test_relocated_reader_and_checkpoints(self):
        self.verify()

    def test_every_reader_byte_and_checkpoint_operand_is_checked(self):
        addresses = list(range(0x100, 0x100 + len(TIMEBASE_READER_BYTES)))
        addresses += [self.symbols[name] + offset for name, size in zip(TIMEBASE_CHECKPOINTS, (2, 2, 3))
                      for offset in range(size)]
        for address in addresses:
            changed = dict(self.image)
            changed[address] ^= 1
            with self.subTest(address=address), self.assertRaises(ValueError):
                self.verify(image=changed)

    def test_missing_symbols_sfr_addresses_and_scratch_records_fail_closed(self):
        for name in TIMEBASE_CHECKPOINTS + ("_timebase_read_awake_ticks24", "_timebase_expired", "_SOC_ST0"):
            changed = dict(self.symbols)
            del changed[name]
            with self.subTest(name=name), self.assertRaises(ValueError):
                self.verify(symbols=changed)
        for debug in (self.debug.replace("$low$", "$missing$"),
                      self.debug.replace("{1}SC:U", "{2}SC:U"),
                      self.debug.replace(":40\n", ":1F00\n"), self.debug.replace(":40\n", ":0\n"),
                      self.debug.replace(":40\n", ":41\n"),
                      self.debug + "L:Ltimebase.timebase_read_awake_ticks24$low$1_0$5:43\n",
                      self.debug + "L:Ltimebase.timebase_read_awake_ticks24$low$1_0$5:not-an-address\n",
                      self.debug + "S:Ltimebase.timebase_read_awake_ticks24$low$1_0$5({2}SC:U),F,0,0\n"):
            with self.subTest(debug=debug), self.assertRaises(ValueError):
                self.verify(debug=debug)

    def test_new_layout_uses_existing_status_budget_and_absolute_checks(self):
        baseline = test_m0_artifacts.LayoutTests()
        baseline.setUp()
        debug = baseline.debug + "\nL:C$timebase_fixture.c$1$0_0$0:300"
        debug += "\nS:G$timebase_fixture_state$0_0$0({32}STfixture:S),F,0,0"
        symbols = dict(baseline.symbols, _timebase_fixture_state=0, l_XSEG=88)
        metrics = verify_layout(symbols, baseline.memory, debug, "timebase_fixture")
        self.assertEqual(metrics["nonaliased_xdata_reserved_bytes"], 152)
        for changed in (dict(symbols, _timebase_fixture_state=0x1e00),
                        dict(symbols, _timebase_fixture_state=0x1f00), dict(symbols, l_XSEG=449)):
            with self.subTest(changed=changed), self.assertRaises(ValueError):
                verify_layout(changed, baseline.memory, debug, "timebase_fixture")
        with self.assertRaises(ValueError):
            verify_layout(symbols, baseline.memory, debug.replace("{32}STfixture", "{31}STfixture"), "timebase_fixture")

    def test_ci_uploads_only_selected_board_artifacts(self):
        workflow = (Path(__file__).resolve().parents[1] / ".github/workflows/ci.yml").read_text()
        self.assertIn("image: [bringup, debug_fixture, timebase_fixture, clock_fixture, irq_fixture]", workflow)
        uploads = workflow.split("          path: |\n", 1)[1].strip().splitlines()
        prefix = "build/${{ matrix.board }}/${{ matrix.image }}/"
        expected = {prefix + "${{ matrix.image }}." + suffix for suffix in ("hex", "bin", "ihx", "map", "mem", "cdb")}
        expected.add(prefix + "build-info.json")
        self.assertEqual({line.strip() for line in uploads}, expected)


if __name__ == "__main__":
    unittest.main()
