# SPDX-License-Identifier: BSD-3-Clause
"""Reset lifecycle contracts with synthetic devices only."""

import json
import unittest
from unittest.mock import patch

from cc2530_debug import READ_STATUS, STEP_INSTR
from cc_debugger import (
    Access, AttachResult, CpuControlResult, Debugger, DebuggerError, State, TransportError, UsbAddress,
)
from test_m1_cpu import ScriptedBackend
import test_m1_transport as transport_tests
from test_m1_transport import Clock, FakeBackend


class ResetTests(unittest.TestCase):
    def session(self, before=0x22, after=0x22, allow=True, cpu_control=False):
        self.clock = Clock()
        self.backend = ScriptedBackend([(READ_STATUS, before), (READ_STATUS, after)], self.clock)
        self.debugger = Debugger(self.backend, Access.EXISTING_DEBUG_SESSION, 10, self.clock,
                                 allow_target_reset=allow, allow_cpu_control=cpu_control)
        self.debugger.open(UsbAddress(1, 2))
        self.backend.calls.clear()
        return self.debugger

    def assert_faulted(self):
        self.assertEqual(self.debugger.state, State.FAULTED)
        previous = list(self.backend.calls)
        for name in ("reset_halt", "halt", "resume", "step", "read_bank", "read_debug_status",
                     "read_debug_config", "read_adapter_state", "attach_reset"):
            with self.subTest(name=name), self.assertRaisesRegex(DebuggerError, "faulted"):
                getattr(self.debugger, name)()
        self.assertEqual(self.backend.calls, previous)

    def test_exact_reset_into_halt_and_fresh_checks(self):
        debugger = self.session(before=0x02)
        self.assertEqual(debugger.reset_halt(), CpuControlResult(0x02, 0x22, True))
        self.assertEqual(self.backend.calls, [
            ("control", 0xC0, 0xC0, 0, 0, 8, 10),
            ("write", 0x04, b"\x1f\x34", 10), ("read", 0x84, 1, 10),
            ("control-write", 0x40, 0xC9, 0, 1, b"", 10),
            ("control", 0xC0, 0xC0, 0, 0, 8, 10),
            ("write", 0x04, b"\x1f\x34", 10), ("read", 0x84, 1, 10),
        ])
        self.assertFalse(self.backend.script)
        self.assertEqual(debugger.state, State.OPEN)

    def test_already_halted_target_is_still_reset_when_explicitly_requested(self):
        debugger = self.session(before=0x23, after=0x22)
        self.assertEqual(debugger.reset_halt(), CpuControlResult(0x23, 0x22, True))
        self.assertEqual(sum(call[0] == "control-write" for call in self.backend.calls), 1)

    def test_cpu_control_does_not_grant_reset_permission(self):
        debugger = self.session(allow=False, cpu_control=True)
        with self.assertRaisesRegex(DebuggerError, "Target reset requires separate"):
            debugger.reset_halt()
        self.assertEqual(self.backend.calls, [])
        self.assertEqual(debugger.state, State.OPEN)

    def test_reset_permission_does_not_grant_cpu_control(self):
        debugger = self.session()
        for name in ("halt", "resume", "step"):
            with self.assertRaisesRegex(DebuggerError, "CPU control requires separate"):
                getattr(debugger, name)()
        self.assertEqual(self.backend.calls, [])

    def test_reset_permission_is_boolean_and_never_implies_initial_attach(self):
        with self.assertRaisesRegex(ValueError, "existing debug session"):
            Debugger(FakeBackend(), allow_target_reset=True)
        for value in (None, 0, 1, "true"):
            with self.subTest(value=value), self.assertRaisesRegex(ValueError, "boolean"):
                Debugger(FakeBackend(), Access.EXISTING_DEBUG_SESSION, allow_target_reset=value)

    def test_unknown_target_never_receives_reset(self):
        for target in (0, 0x2531, 0xFFFF):
            debugger = self.session()
            self.backend.control_reply = target.to_bytes(2, "little") + b"\0" * 6
            with self.subTest(target=target), self.assertRaisesRegex(TransportError, "CC2530"):
                debugger.reset_halt()
            self.assertEqual([call[0] for call in self.backend.calls], ["control"])
            self.assert_faulted()

    def test_locked_erasing_sleeping_or_unstable_target_is_not_reset(self):
        for status in (0x20, 0x26, 0xA2, 0x32, 0x62):
            debugger = self.session(before=status)
            with self.subTest(status=status), self.assertRaises(TransportError):
                debugger.reset_halt()
            self.assertEqual([call[0] for call in self.backend.calls], ["control", "write", "read"])
            self.assert_faulted()

    def test_invalid_control_completion_is_not_success_or_retried(self):
        for count in (-1, 1, False, True, 0.0, None, b""):
            debugger = self.session()
            self.backend.control_written = count
            with self.subTest(count=count), self.assertRaisesRegex(TransportError, "control completion"):
                debugger.reset_halt()
            self.assertEqual(len(self.backend.calls), 4)
            self.assert_faulted()

    def test_invalid_post_status_does_not_trigger_reset_retry_or_halt(self):
        for status in (0x02, 0x20, 0x26, 0xA2, 0x32, 0x62):
            debugger = self.session(after=status)
            with self.subTest(status=status), self.assertRaises(TransportError):
                debugger.reset_halt()
            self.assertEqual(len(self.backend.calls), 7)
            self.assertEqual(self.backend.commands(), [READ_STATUS, READ_STATUS])
            self.assert_faulted()

    def test_post_reset_target_mismatch_stops_target_io(self):
        debugger = self.session()
        read = self.backend.control_read

        def changed_target(*args):
            if len(self.backend.calls) == 4:
                self.backend.control_reply = b"\0" * 8
            return read(*args)

        with patch.object(self.backend, "control_read", side_effect=changed_target):
            with self.assertRaisesRegex(TransportError, "target changed"):
                debugger.reset_halt()
        self.assertEqual(len(self.backend.calls), 5)
        self.assert_faulted()

    def test_all_reset_phases_share_a_deadline(self):
        debugger = self.session()
        self.backend.delays = {name: 1_000_000 for name in ("control", "write", "read", "control-write")}
        debugger.reset_halt()
        self.assertEqual([call[-1] for call in self.backend.calls], [10, 9, 8, 7, 6, 5, 4])

    def test_late_reset_completion_never_sends_another_command(self):
        debugger = self.session()
        self.backend.delays["control-write"] = 10_000_000
        with self.assertRaisesRegex(TransportError, "deadline"):
            debugger.reset_halt()
        self.assertEqual(len(self.backend.calls), 4)
        self.assert_faulted()

    def test_failure_at_every_io_boundary(self):
        for index in range(1, 8):
            debugger = self.session()
            self.backend.fail_at = index
            with self.subTest(index=index), self.assertRaisesRegex(TransportError, "synthetic transfer"):
                debugger.reset_halt()
            self.assertEqual(len(self.backend.calls), index)
            self.assert_faulted()

    def test_interrupted_reset_never_attempts_recovery(self):
        debugger = self.session()
        with patch.object(self.backend, "control_write", side_effect=KeyboardInterrupt):
            with self.assertRaises(KeyboardInterrupt):
                debugger.reset_halt()
        self.assert_faulted()
        debugger.close()
        self.assertEqual(self.backend.calls[-1], ("close",))

    def test_open_and_close_do_not_reset_even_when_authorized(self):
        backend = FakeBackend()
        debugger = Debugger(backend, Access.EXISTING_DEBUG_SESSION, allow_target_reset=True)
        debugger.open(UsbAddress(1, 2))
        debugger.close()
        self.assertEqual(backend.calls, [("open", UsbAddress(1, 2), 1000), ("close",)])


class AttachBackend(ScriptedBackend):
    def __init__(self, script, clock=None):
        super().__init__(script, clock)
        self.control_counts = {}

    def control_write(self, request_type, request, value, index, data, timeout_ms):
        self.record("control-write", request_type, request, value, index, data, timeout_ms)
        return self.control_counts.get(request, len(data))


class AttachTests(unittest.TestCase):
    assert_faulted = ResetTests.assert_faulted
    chip_info = b"CC2530" + b" " * 10 + b"DID: 1234" + b" " * 23

    def session(self, after=0x22, cpu_control=False):
        self.clock = Clock()
        self.backend = AttachBackend([(READ_STATUS, after)], self.clock)
        self.debugger = Debugger(self.backend, Access.RESET_DEBUG_SESSION, 10, self.clock,
                                 allow_target_reset=True, allow_cpu_control=cpu_control)
        self.debugger.open(UsbAddress(1, 2))
        self.backend.calls.clear()
        return self.debugger

    def test_exact_preparation_metadata_reset_and_postcheck(self):
        debugger = self.session()
        self.assertEqual(debugger.attach_reset(), AttachResult(0x22, True))
        self.assertEqual(self.backend.calls, [
            ("control", 0xC0, 0xC0, 0, 0, 8, 10), ("revision",),
            ("control-write", 0x40, 0xC5, 0, 0, b"", 10),
            ("control-write", 0x40, 0xC8, 1, 0, self.chip_info, 10),
            ("control-write", 0x40, 0xC9, 0, 1, b"", 10),
            ("control", 0xC0, 0xC0, 0, 0, 8, 10),
            ("write", 0x04, b"\x1f\x34", 10), ("read", 0x84, 1, 10),
        ])
        self.assertFalse(self.backend.script)

    def test_policy_never_prepares_or_resets_on_open_close(self):
        backend = AttachBackend([])
        debugger = Debugger(backend, Access.RESET_DEBUG_SESSION, allow_target_reset=True)
        debugger.open(UsbAddress(1, 2))
        debugger.close()
        self.assertEqual(backend.calls, [("open", UsbAddress(1, 2), 1000), ("close",)])

    def test_reset_permission_is_required_by_initial_attach_policy(self):
        with self.assertRaisesRegex(ValueError, "separate target-reset permission"):
            Debugger(FakeBackend(), Access.RESET_DEBUG_SESSION)

    def test_other_policies_cannot_prepare_adapter(self):
        for access, allow in ((Access.ADAPTER_ONLY, False), (Access.EXISTING_DEBUG_SESSION, True)):
            backend = FakeBackend()
            debugger = Debugger(backend, access, allow_target_reset=allow)
            debugger.open(UsbAddress(1, 2))
            backend.calls.clear()
            with self.subTest(access=access), self.assertRaisesRegex(DebuggerError, "reset-attach policy"):
                debugger.attach_reset()
            self.assertEqual(backend.calls, [])
            self.assertEqual(debugger.state, State.OPEN)

    def test_target_operations_denied_until_attach_completes(self):
        debugger = self.session(cpu_control=True)
        for name in ("read_debug_status", "read_debug_config", "read_bank", "halt", "resume", "step", "reset_halt"):
            with self.subTest(name=name), self.assertRaisesRegex(DebuggerError, "completed reset-attach"):
                getattr(debugger, name)()
        self.assertEqual(self.backend.calls, [])
        debugger.attach_reset()
        self.backend.script.extend([(READ_STATUS, 0x22), (STEP_INSTR, 0xA5), (READ_STATUS, 0x22)])
        self.assertEqual(debugger.step().accumulator, 0xA5)
        count = len(self.backend.calls)
        with self.assertRaisesRegex(DebuggerError, "fresh explicit"):
            debugger.attach_reset()
        self.assertEqual(len(self.backend.calls), count)
        self.assertEqual(debugger.state, State.OPEN)

    def test_attach_does_not_grant_resume_permission(self):
        debugger = self.session()
        debugger.attach_reset()
        previous = list(self.backend.calls)
        with self.assertRaisesRegex(DebuggerError, "CPU control requires separate"):
            debugger.resume()
        self.assertEqual(self.backend.calls, previous)

    def test_explicit_reset_after_attach_does_not_prepare_again(self):
        debugger = self.session()
        debugger.attach_reset()
        self.backend.script.extend([(READ_STATUS, 0x22), (READ_STATUS, 0x22)])
        debugger.reset_halt()
        self.assertEqual([call[2] for call in self.backend.calls if call[0] == "control-write"],
                         [0xC5, 0xC8, 0xC9, 0xC9])

    def test_unknown_adapter_target_never_receives_preparation(self):
        for target in (0, 0x2531, 0xFFFF):
            debugger = self.session()
            self.backend.control_reply = target.to_bytes(2, "little") + b"\0" * 6
            with self.subTest(target=target), self.assertRaisesRegex(TransportError, "no attach command"):
                debugger.attach_reset()
            self.assertEqual(len(self.backend.calls), 1)
            self.assert_faulted()

    def test_invalid_descriptor_revision_does_not_prepare(self):
        for revision in (-1, 0x10000, True, 0.0, "1234", None):
            debugger = self.session()
            self.backend.revision = revision
            with self.subTest(revision=revision), self.assertRaisesRegex(TransportError, "bcdDevice"):
                debugger.attach_reset()
            self.assertEqual(len(self.backend.calls), 2)
            self.assert_faulted()

    def test_descriptor_revision_is_four_hex_digits_not_a_unique_id(self):
        for revision, expected in ((0, b"0000"), (1, b"0001"), (0xABCD, b"ABCD"), (0xFFFF, b"FFFF")):
            debugger = self.session()
            self.backend.revision = revision
            debugger.attach_reset()
            payload = self.backend.calls[3][5]
            with self.subTest(revision=revision):
                self.assertEqual(len(payload), 48)
                self.assertEqual(payload[21:25], expected)
                self.assertEqual(payload[:21], self.chip_info[:21])
                self.assertEqual(payload[25:], b" " * 23)

    def test_control_writes_require_exact_integer_completion_at_every_stage(self):
        for request, stop in ((0xC5, 3), (0xC8, 4), (0xC9, 5)):
            for count in (-1, 1, 47, 49, False, None, 0.0):
                debugger = self.session()
                self.backend.control_counts[request] = count
                with self.subTest(request=request, count=count), self.assertRaisesRegex(
                        TransportError, "control completion"):
                    debugger.attach_reset()
                self.assertEqual(len(self.backend.calls), stop)
                self.assert_faulted()

    def test_locked_erasing_unstable_sleeping_or_running_post_status_is_failure(self):
        for status in (0x02, 0x20, 0x26, 0xA2, 0x32, 0x62):
            debugger = self.session(after=status)
            with self.subTest(status=status), self.assertRaises(TransportError):
                debugger.attach_reset()
            self.assertEqual(len(self.backend.calls), 8)
            self.assert_faulted()

    def test_unknown_target_after_reset_stops_before_target_status(self):
        debugger = self.session()
        read = self.backend.control_read

        def changed(*args):
            if len(self.backend.calls) == 5:
                self.backend.control_reply = b"\0" * 8
            return read(*args)

        with patch.object(self.backend, "control_read", side_effect=changed):
            with self.assertRaisesRegex(TransportError, "target changed"):
                debugger.attach_reset()
        self.assertEqual(len(self.backend.calls), 6)
        self.assert_faulted()

    def test_one_deadline_includes_metadata_and_all_transfers(self):
        debugger = self.session()
        self.backend.delays = {name: 1_000_000 for name in ("control", "revision", "control-write", "write", "read")}
        debugger.attach_reset()
        self.assertEqual([call[-1] for call in self.backend.calls if call[0] != "revision"],
                         [10, 8, 7, 6, 5, 4, 3])

    def test_failure_at_every_boundary_never_recovers_or_resumes(self):
        for index in range(1, 9):
            debugger = self.session()
            self.backend.fail_at = index
            with self.subTest(index=index), self.assertRaisesRegex(TransportError, "synthetic transfer"):
                debugger.attach_reset()
            self.assertEqual(len(self.backend.calls), index)
            self.assert_faulted()

    def test_each_preparation_completion_is_subject_to_deadline(self):
        for request, count in ((0xC5, 3), (0xC8, 4), (0xC9, 5)):
            debugger = self.session()
            write = self.backend.control_write

            def delayed(*args):
                result = write(*args)
                if args[1] == request:
                    self.clock.now += 10_000_000
                return result

            with patch.object(self.backend, "control_write", side_effect=delayed):
                with self.subTest(request=request), self.assertRaisesRegex(TransportError, "deadline"):
                    debugger.attach_reset()
            self.assertEqual(len(self.backend.calls), count)
            self.assert_faulted()

    def test_interrupted_preparation_leaves_terminal_fault(self):
        debugger = self.session()
        with patch.object(self.backend, "control_write", side_effect=KeyboardInterrupt):
            with self.assertRaises(KeyboardInterrupt):
                debugger.attach_reset()
        self.assert_faulted()
        debugger.close()
        self.assertEqual(self.backend.calls[-1], ("close",))


class ResetCliTests(unittest.TestCase):
    run_cli = transport_tests.CliTests.run_cli
    arguments = ["reset-halt", "--bus", "1", "--address", "2",
                 "--confirm-existing-debug-session", "--allow-target-reset"]
    attach_arguments = ["attach-reset", "--bus", "1", "--address", "2",
                        "--confirm-reset-attach", "--allow-target-reset"]

    def test_missing_reset_permission_never_loads_backend(self):
        for flags in ([], ["--confirm-existing-debug-session"], ["--allow-target-reset"],
                      ["--confirm-existing-debug-session", "--allow-cpu-control"]):
            with self.subTest(flags=flags):
                code, out, err, load = self.run_cli(["reset-halt", "--bus", "1", "--address", "2", *flags],
                                                  FakeBackend())
                self.assertEqual((code, out), (1, ""))
                self.assertTrue(err)
                load.assert_not_called()

    def test_reset_json_only_after_cleanup(self):
        backend = ScriptedBackend([(READ_STATUS, 0x02), (READ_STATUS, 0x22)])
        code, out, err, _ = self.run_cli(self.arguments, backend)
        self.assertEqual((code, err), (0, ""))
        self.assertEqual(json.loads(out), {"status_before": 2, "status_after": 34, "command_sent": True})
        self.assertEqual(backend.calls[-1], ("close",))

    def test_reset_or_cleanup_error_produces_no_success_json(self):
        for phase in ("control-write", "close"):
            backend = ScriptedBackend([(READ_STATUS, 0x02), (READ_STATUS, 0x22)])
            backend.failures.add(phase)
            code, out, err, _ = self.run_cli(self.arguments, backend)
            self.assertEqual((code, out), (1, ""))
            self.assertIn(f"{phase} failed", err)
            self.assertEqual(backend.calls[-1], ("close",))

    def test_attach_requires_both_permissions_before_backend_load(self):
        for flags in ([], ["--confirm-reset-attach"], ["--allow-target-reset"],
                      ["--confirm-existing-debug-session", "--allow-target-reset"]):
            code, out, err, load = self.run_cli(["attach-reset", "--bus", "1", "--address", "2", *flags],
                                              FakeBackend())
            self.assertEqual((code, out), (1, ""))
            self.assertTrue(err)
            load.assert_not_called()

    def test_attach_policy_cannot_substitute_for_an_existing_session(self):
        for command in ("adapter-state", "debug-status", "reset-halt", "step"):
            code, out, err, load = self.run_cli(
                [command, "--bus", "1", "--address", "2", "--confirm-reset-attach", "--allow-target-reset"],
                FakeBackend())
            self.assertEqual((code, out), (1, ""))
            self.assertIn("only valid for attach-reset", err)
            load.assert_not_called()

    def test_attach_json_is_only_post_status_not_a_fabricated_prior_snapshot(self):
        backend = AttachBackend([(READ_STATUS, 0x22)])
        code, out, err, _ = self.run_cli(self.attach_arguments, backend)
        self.assertEqual((code, err), (0, ""))
        self.assertEqual(json.loads(out), {"status_after": 34, "reset_sent": True})
        self.assertEqual(backend.calls[-1], ("close",))

    def test_attach_error_or_cleanup_failure_never_prints_success(self):
        for phase in ("control-write", "close"):
            backend = AttachBackend([(READ_STATUS, 0x22)])
            backend.failures.add(phase)
            code, out, err, _ = self.run_cli(self.attach_arguments, backend)
            self.assertEqual((code, out), (1, ""))
            self.assertIn(f"{phase} failed", err)
            self.assertEqual(backend.calls[-1], ("close",))


if __name__ == "__main__":
    unittest.main()
