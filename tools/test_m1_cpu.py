# SPDX-License-Identifier: BSD-3-Clause
"""Public target command facts and synthetic USB control conversations."""

from collections import deque
import json
import unittest

from cc2530_debug import (
    GET_BM, HALT, READ_CONFIG, READ_STATUS, RESUME, STEP_INSTR,
    Status, breakpoint_parameters, decode_config, decode_status,
)
from cc_debugger import (
    Access, CpuControlResult, Debugger, DebuggerError, State, StepResult,
    TransportError, UsbAddress,
)
import test_m1_transport as transport_tests
from test_m1_transport import Clock, FakeBackend


class ScriptedBackend(FakeBackend):
    def __init__(self, script, clock=None):
        super().__init__(clock)
        self.script = deque(script)
        self.last_command = None
        self.fail_at = None

    def record(self, name, *args):
        super().record(name, *args)
        if len(self.calls) == self.fail_at:
            raise TransportError("synthetic transfer failure")

    def bulk_write(self, endpoint, data, timeout_ms):
        if not self.script or data != bytes([0x1F, self.script[0][0]]) or endpoint != 0x04:
            raise AssertionError("Unexpected USB command")
        self.last_command = data[1]
        return super().bulk_write(endpoint, data, timeout_ms)

    def bulk_read(self, endpoint, length, timeout_ms):
        if endpoint != 0x84 or length != 1:
            raise AssertionError("Unexpected USB read")
        self.record("read", endpoint, length, timeout_ms)
        command, reply = self.script.popleft()
        if self.last_command != command:
            raise AssertionError("Read without expected command")
        return reply if isinstance(reply, bytes) else bytes([reply])

    def commands(self):
        return [call[2][1] for call in self.calls if call[0] == "write"]


class CpuTests(unittest.TestCase):
    def session(self, script, allow=True):
        self.clock = Clock()
        self.backend = ScriptedBackend(script, self.clock)
        self.debugger = Debugger(self.backend, Access.EXISTING_DEBUG_SESSION, 10, self.clock,
                                 allow_cpu_control=allow)
        self.debugger.open(UsbAddress(1, 2))
        self.backend.calls.clear()
        return self.debugger

    def assert_faulted(self):
        self.assertEqual(self.debugger.state, State.FAULTED)
        count = len(self.backend.calls)
        for method in (self.debugger.read_debug_status, self.debugger.read_debug_config,
                       self.debugger.read_bank, self.debugger.halt, self.debugger.resume, self.debugger.step):
            with self.assertRaisesRegex(DebuggerError, "faulted"):
                method()
        self.assertEqual(len(self.backend.calls), count)

    def test_halt_ignores_undefined_reply_and_checks_fresh_status(self):
        debugger = self.session([(READ_STATUS, 0x02), (HALT, 0xFF), (READ_STATUS, 0x22)])
        self.assertEqual(debugger.halt(), CpuControlResult(0x02, 0x22, True))
        self.assertFalse(self.backend.script)

    def test_halt_already_halted_is_reported_without_sending_halt(self):
        debugger = self.session([(READ_STATUS, 0x23)])
        self.assertEqual(debugger.halt(), CpuControlResult(0x23, 0x23, False))
        self.assertEqual(self.backend.commands(), [READ_STATUS])

    def test_breakpoint_can_halt_between_precheck_and_halt_command(self):
        debugger = self.session([(READ_STATUS, 0x02), (HALT, 0), (READ_STATUS, 0x2A)])
        self.assertEqual(debugger.halt().status_after, 0x2A)

    def test_halt_failure_is_not_reported_as_success(self):
        debugger = self.session([(READ_STATUS, 0x02), (HALT, 0x02), (READ_STATUS, 0x02)])
        with self.assertRaisesRegex(TransportError, "must be halted"):
            debugger.halt()
        self.assert_faulted()

    def test_resume_checks_before_reply_and_after(self):
        debugger = self.session([(READ_STATUS, 0x22), (RESUME, 0x02), (READ_STATUS, 0x02)])
        self.assertEqual(debugger.resume(), CpuControlResult(0x22, 0x02, True))
        self.assertFalse(self.backend.script)

    def test_resume_can_observe_an_immediate_breakpoint(self):
        debugger = self.session([(READ_STATUS, 0x2A), (RESUME, 0x2A), (READ_STATUS, 0x2A)])
        self.assertEqual(debugger.resume().status_after, 0x2A)
        self.assertEqual(debugger.state, State.OPEN)

    def test_resume_does_not_ignore_an_error_reply(self):
        debugger = self.session([(READ_STATUS, 0x22), (RESUME, 0x06)])
        with self.assertRaisesRegex(TransportError, "locked"):
            debugger.resume()
        self.assert_faulted()

    def test_resume_still_halted_without_breakpoint_is_error(self):
        debugger = self.session([(READ_STATUS, 0x22), (RESUME, 0x22), (READ_STATUS, 0x22)])
        with self.assertRaisesRegex(TransportError, "without a hardware-breakpoint"):
            debugger.resume()
        self.assert_faulted()

    def test_every_step_accumulator_byte_is_data_not_status(self):
        for accumulator in range(256):
            with self.subTest(accumulator=accumulator):
                debugger = self.session([(READ_STATUS, 0x23), (STEP_INSTR, accumulator),
                                         (READ_STATUS, 0x23)])
                self.assertEqual(debugger.step(), StepResult(0x23, 0x23, accumulator))

    def test_step_must_leave_cpu_halted(self):
        debugger = self.session([(READ_STATUS, 0x22), (STEP_INSTR, 0x20), (READ_STATUS, 0x02)])
        with self.assertRaisesRegex(TransportError, "must be halted"):
            debugger.step()
        self.assert_faulted()

    def test_bank_read_is_allowed_without_cpu_control_and_masks_only_low_bits(self):
        for raw in range(256):
            with self.subTest(raw=raw):
                debugger = self.session([(READ_STATUS, 0x22), (GET_BM, raw), (READ_STATUS, 0x22)],
                                        allow=False)
                self.assertEqual(debugger.read_bank(), raw & 7)

    def test_running_cpu_cannot_resume_step_or_be_used_for_bank_snapshot(self):
        for name in ("resume", "step", "read_bank"):
            with self.subTest(name=name):
                debugger = self.session([(READ_STATUS, 0x02)])
                with self.assertRaisesRegex(TransportError, "must be halted"):
                    getattr(debugger, name)()
                self.assertEqual(self.backend.commands(), [READ_STATUS])
                self.assert_faulted()

    def test_unready_target_denies_cpu_commands_and_bank_reads(self):
        for status in (0x20, 0x26, 0xA2, 0x32, 0x62):
            for name in ("halt", "resume", "step", "read_bank"):
                with self.subTest(status=status, name=name):
                    debugger = self.session([(READ_STATUS, status)])
                    with self.assertRaises(TransportError):
                        getattr(debugger, name)()
                    self.assertEqual(self.backend.commands(), [READ_STATUS])
                    self.assert_faulted()

    def test_invalid_post_status_latches_failure(self):
        for name, opcode, before, reply in (("halt", HALT, 0x02, 0x22),
                                           ("resume", RESUME, 0x22, 0x02),
                                           ("step", STEP_INSTR, 0x22, 0xFF),
                                           ("read_bank", GET_BM, 0x22, 0x07)):
            for after in (0x20, 0x26, 0xA2, 0x32, 0x62):
                with self.subTest(name=name, after=after):
                    debugger = self.session([(READ_STATUS, before), (opcode, reply), (READ_STATUS, after)])
                    with self.assertRaises(TransportError):
                        getattr(debugger, name)()
                    self.assert_faulted()

    def test_control_permission_is_separate_and_checked_before_io(self):
        debugger = self.session([], allow=False)
        for name in ("halt", "resume", "step"):
            with self.subTest(name=name), self.assertRaisesRegex(DebuggerError, "separate explicit"):
                getattr(debugger, name)()
        self.assertEqual(self.backend.calls, [])
        self.assertEqual(debugger.state, State.OPEN)

    def test_invalid_control_permission_does_not_upgrade_adapter_access(self):
        with self.assertRaisesRegex(ValueError, "existing debug session"):
            Debugger(FakeBackend(), allow_cpu_control=True)
        for value in (None, 0, 1, "true"):
            with self.subTest(value=value), self.assertRaisesRegex(ValueError, "boolean"):
                Debugger(FakeBackend(), Access.EXISTING_DEBUG_SESSION, allow_cpu_control=value)

    def test_config_checks_lock_and_erase_without_requiring_stable_clock(self):
        debugger = self.session([(READ_STATUS, 0x20), (READ_CONFIG, 0xFF), (READ_STATUS, 0x20)])
        self.assertEqual(debugger.read_debug_config(), 0xFF)
        for status in (0x04, 0x80):
            with self.subTest(status=status):
                debugger = self.session([(READ_STATUS, status)])
                with self.assertRaisesRegex(TransportError, "locked or erasing"):
                    debugger.read_debug_config()
                self.assertEqual(self.backend.commands(), [READ_STATUS])
                self.assert_faulted()

    def test_config_rejects_lock_change_after_read(self):
        debugger = self.session([(READ_STATUS, 0x20), (READ_CONFIG, 0x26), (READ_STATUS, 0x24)])
        with self.assertRaisesRegex(TransportError, "locked"):
            debugger.read_debug_config()
        self.assert_faulted()

    def test_one_deadline_includes_precheck_command_and_postcheck(self):
        debugger = self.session([(READ_STATUS, 0x22), (STEP_INSTR, 0xA5), (READ_STATUS, 0x22)])
        self.backend.delays = {"control": 2_000_000, "write": 1_000_000, "read": 1_000_000}
        debugger.step()
        self.assertEqual([call[-1] for call in self.backend.calls], [10, 8, 7, 6, 5, 4, 3])

    def test_late_step_reply_is_not_retried(self):
        debugger = self.session([(READ_STATUS, 0x22), (STEP_INSTR, 0xA5)])
        self.backend.delays["read"] = 6_000_000
        with self.assertRaisesRegex(TransportError, "deadline"):
            debugger.step()
        self.assertEqual(self.backend.commands(), [READ_STATUS, STEP_INSTR])
        self.assert_faulted()

    def test_io_failure_at_each_control_boundary(self):
        for index in range(1, 8):
            with self.subTest(index=index):
                debugger = self.session([(READ_STATUS, 0x22), (STEP_INSTR, 0xA5), (READ_STATUS, 0x22)])
                self.backend.fail_at = index
                with self.assertRaisesRegex(TransportError, "synthetic transfer"):
                    debugger.step()
                self.assertEqual(len(self.backend.calls), index)
                self.assert_faulted()


class TargetFormatTests(unittest.TestCase):
    def test_status_flags_are_independent_and_complete(self):
        for value in range(256):
            decoded = decode_status(value)
            self.assertEqual(decoded["raw"], value)
            self.assertEqual(sum(int(decoded[flag.name.lower()]) * int(flag) for flag in Status), value)
        self.assertFalse(decode_status(0x23)["debug_locked"])
        self.assertTrue(decode_status(0x23)["stack_overflow"])

    def test_cc2530_config_bit_zero_is_reserved_not_info_page_selection(self):
        self.assertEqual(decode_config(0x26), {
            "raw": 0x26, "soft_power_mode": True, "timers_off": False,
            "dma_pause": True, "timer_suspend": True, "reserved_bits": 0,
        })
        self.assertEqual(decode_config(0xD1)["reserved_bits"], 0xD1)

    def test_byte_decoders_reject_invalid_inputs(self):
        for decoder in (decode_status, decode_config):
            for value in (-1, 256, True, 1.0, "32", None):
                with self.subTest(decoder=decoder, value=value), self.assertRaises(ValueError):
                    decoder(value)

    def test_breakpoint_bytes_golden_and_slot_bank_boundaries(self):
        self.assertEqual(breakpoint_parameters(0, 0x1234), bytes.fromhex("08 12 34"))
        self.assertEqual(breakpoint_parameters(3, 0xFFFF, 7), bytes.fromhex("3f ff ff"))
        self.assertEqual(breakpoint_parameters(3, 0, 7, False), bytes.fromhex("37 00 00"))
        for slot in range(4):
            for bank in range(8):
                for enabled in (False, True):
                    data = breakpoint_parameters(slot, 0x8001, bank, enabled)
                    self.assertEqual(data[0] & 0xC0, 0)
                    self.assertEqual((data[0] >> 4) & 3, slot)
                    self.assertEqual(data[0] & 7, bank)
                    self.assertEqual(bool(data[0] & 8), enabled)
                    self.assertEqual(data[1:], b"\x80\x01")

    def test_breakpoint_invalid_parameters(self):
        for args in ((4, 0), (-1, 0), (0, -1), (0, 0x10000), (0, 0, 8),
                     (0, 0, -1), (True, 0), (0, 0, 0, 1), (0, 0.0)):
            with self.subTest(args=args), self.assertRaises(ValueError):
                breakpoint_parameters(*args)


class CpuCliTests(unittest.TestCase):
    run_cli = transport_tests.CliTests.run_cli

    def test_missing_cpu_permission_never_loads_backend(self):
        for command in ("halt", "resume", "step"):
            for flags in ([], ["--confirm-existing-debug-session"], ["--allow-cpu-control"]):
                with self.subTest(command=command, flags=flags):
                    code, out, err, load = self.run_cli(
                        [command, "--bus", "1", "--address", "2", *flags], FakeBackend())
                    self.assertEqual((code, out), (1, ""))
                    self.assertTrue(err)
                    load.assert_not_called()

    def test_step_json_reports_accumulator_and_observed_statuses(self):
        backend = ScriptedBackend([(READ_STATUS, 0x22), (STEP_INSTR, 0xFF), (READ_STATUS, 0x22)])
        code, out, err, _ = self.run_cli([
            "step", "--bus", "1", "--address", "2", "--confirm-existing-debug-session", "--allow-cpu-control",
        ], backend)
        self.assertEqual((code, err), (0, ""))
        self.assertEqual(json.loads(out), {"status_before": 0x22, "status_after": 0x22, "accumulator": 0xFF})
        self.assertEqual(backend.calls[-1], ("close",))


if __name__ == "__main__":
    unittest.main()
