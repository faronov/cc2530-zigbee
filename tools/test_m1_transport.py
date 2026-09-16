# SPDX-License-Identifier: BSD-3-Clause
"""Synthetic USB conversations only; no enumeration or physical I/O."""

from contextlib import redirect_stderr, redirect_stdout
from io import StringIO
import json
import unittest
from unittest.mock import Mock, patch

from cc_debugger import (
    Access, AdapterState, Debugger, DebuggerError, PyUsbBackend, State,
    TransportError, UsbAddress, main,
)


class Clock:
    def __init__(self):
        self.now = 0

    def __call__(self):
        return self.now


class FakeBackend:
    def __init__(self, clock=None):
        self.calls = []
        self.clock = clock or Clock()
        self.delays = {}
        self.failures = set()
        self.control_reply = b"\x30\x25\x34\x12\x78\x56\xaa\xbb"
        self.byte_reply = b"\x20"
        self.written = 2
        self.control_written = 0
        self.revision = 0x1234

    def record(self, name, *args):
        self.calls.append((name, *args))
        self.clock.now += self.delays.get(name, 0)
        if name in self.failures:
            raise TransportError(f"{name} failed")

    def open(self, address, timeout_ms):
        self.record("open", address, timeout_ms)

    def device_revision(self):
        self.record("revision")
        return self.revision

    def control_read(self, *args):
        self.record("control", *args)
        return self.control_reply

    def control_write(self, *args):
        self.record("control-write", *args)
        return self.control_written

    def bulk_write(self, *args):
        self.record("write", *args)
        return self.written

    def bulk_read(self, *args):
        self.record("read", *args)
        return self.byte_reply

    def close(self):
        self.record("close")


class TransportTests(unittest.TestCase):
    def setUp(self):
        self.clock = Clock()
        self.backend = FakeBackend(self.clock)
        self.debugger = Debugger(self.backend, Access.EXISTING_DEBUG_SESSION, 10, self.clock)
        self.address = UsbAddress(1, 2)
        self.debugger.open(self.address)
        self.backend.calls.clear()

    def assert_faulted(self):
        self.assertEqual(self.debugger.state, State.FAULTED)
        previous = list(self.backend.calls)
        for operation in (self.debugger.read_adapter_state, self.debugger.read_debug_status,
                          self.debugger.read_debug_config, self.debugger.read_bank,
                          self.debugger.halt, self.debugger.resume, self.debugger.step,
                          self.debugger.reset_halt, self.debugger.attach_reset):
            with self.assertRaisesRegex(DebuggerError, "faulted"):
                operation()
        self.assertEqual(self.backend.calls, previous)

    def test_construction_does_not_open_usb(self):
        backend = FakeBackend()
        debugger = Debugger(backend)
        self.assertEqual(debugger.state, State.NEW)
        self.assertEqual(backend.calls, [])

    def test_adapter_state_wire_and_little_endian(self):
        self.assertEqual(self.debugger.read_adapter_state(), AdapterState(0x2530, 0x1234, 0x5678))
        self.assertEqual(self.backend.calls, [("control", 0xC0, 0xC0, 0, 0, 8, 10)])
        self.assertEqual(self.debugger.state, State.OPEN)

    def test_target_wire_and_fresh_adapter_check(self):
        for method, command in ((self.debugger.read_debug_status, b"\x1f\x34"),
                                (self.debugger.read_debug_config, b"\x1f\x24")):
            self.backend.calls.clear()
            self.assertEqual(method(), 0x20)
            commands = [command] if command == b"\x1f\x34" else [b"\x1f\x34", command, b"\x1f\x34"]
            expected = [("control", 0xC0, 0xC0, 0, 0, 8, 10)]
            for packet in commands:
                expected += [("write", 0x04, packet, 10), ("read", 0x84, 1, 10)]
            self.assertEqual(self.backend.calls, expected)

    def test_all_status_bytes_are_returned_without_inventing_success(self):
        for value in range(256):
            self.backend.byte_reply = bytes([value])
            self.assertEqual(self.debugger.read_debug_status(), value)

    def test_adapter_only_policy_denies_target_before_io(self):
        debugger = Debugger(self.backend)
        debugger.open(self.address)
        self.backend.calls.clear()
        for method in (debugger.read_debug_status, debugger.read_debug_config):
            with self.assertRaisesRegex(DebuggerError, "existing debug session"):
                method()
        self.assertEqual(debugger.state, State.OPEN)
        self.assertEqual(self.backend.calls, [])
        self.assertEqual(debugger.read_adapter_state().target_id, 0x2530)

    def test_unknown_target_blocks_bulk_command(self):
        for target in (0, 0x2531, 0xFFFF):
            with self.subTest(target=target):
                self.setUp()
                self.backend.control_reply = target.to_bytes(2, "little") + b"\0" * 6
                with self.assertRaisesRegex(TransportError, "CC2530"):
                    self.debugger.read_debug_status()
                self.assertEqual([call[0] for call in self.backend.calls], ["control"])
                self.assert_faulted()

    def test_wrong_control_reply_lengths(self):
        for size in (0, 1, 7, 9):
            with self.subTest(size=size):
                self.setUp()
                self.backend.control_reply = b"\0" * size
                with self.assertRaisesRegex(TransportError, "exactly 8"):
                    self.debugger.read_adapter_state()
                self.assert_faulted()

    def test_wrong_target_reply_lengths(self):
        for reply in (b"", b"\0\0", None, 1):
            with self.subTest(reply=reply):
                self.setUp()
                self.backend.byte_reply = reply
                with self.assertRaisesRegex(TransportError, "exactly 1"):
                    self.debugger.read_debug_config()
                self.assert_faulted()

    def test_incomplete_writes_are_not_retried_or_read(self):
        for count in (0, 1, 3, True, 2.0, None):
            with self.subTest(count=count):
                self.setUp()
                self.backend.written = count
                with self.assertRaisesRegex(TransportError, "Incomplete"):
                    self.debugger.read_debug_status()
                self.assertEqual([call[0] for call in self.backend.calls], ["control", "write"])
                self.assert_faulted()

    def test_failures_latch_at_every_exchange_phase(self):
        for phase in ("control", "write", "read"):
            with self.subTest(phase=phase):
                self.setUp()
                self.backend.failures.add(phase)
                with self.assertRaisesRegex(TransportError, f"{phase} failed"):
                    self.debugger.read_debug_status()
                self.assertEqual(self.backend.calls[-1][0], phase)
                self.assert_faulted()

    def test_interruption_latches_without_an_automatic_retry(self):
        with patch.object(self.backend, "bulk_read", side_effect=KeyboardInterrupt):
            with self.assertRaises(KeyboardInterrupt):
                self.debugger.read_debug_status()
        self.assert_faulted()
        self.assertEqual([call[0] for call in self.backend.calls], ["control", "write"])

    def test_one_deadline_covers_all_transfers(self):
        self.backend.delays = {"control": 3_000_000, "write": 4_000_000, "read": 2_000_000}
        self.debugger.read_debug_status()
        self.assertEqual([call[-1] for call in self.backend.calls], [10, 7, 3])

    def test_submillisecond_timeout_never_becomes_infinite_zero(self):
        self.backend.delays["control"] = 9_500_000
        self.debugger.read_debug_status()
        self.assertEqual([call[-1] for call in self.backend.calls], [10, 1, 1])

    def test_late_reply_is_failure_even_when_transfer_reports_success(self):
        self.backend.delays["read"] = 10_000_000
        with self.assertRaisesRegex(TransportError, "deadline"):
            self.debugger.read_debug_status()
        self.assert_faulted()

    def test_deadline_exhaustion_stops_following_transfers(self):
        for phase, expected in (("control", ["control"]), ("write", ["control", "write"])):
            with self.subTest(phase=phase):
                self.setUp()
                self.backend.delays[phase] = 10_000_000
                with self.assertRaisesRegex(TransportError, "deadline"):
                    self.debugger.read_debug_status()
                self.assertEqual([call[0] for call in self.backend.calls], expected)
                self.assert_faulted()

    def test_expired_before_first_transfer(self):
        debugger = Debugger(self.backend, clock=Mock(side_effect=(0, 1_000_000_000)))
        debugger.open(self.address)
        self.backend.calls.clear()
        with self.assertRaisesRegex(TransportError, "deadline"):
            debugger.read_adapter_state()
        self.assertEqual(self.backend.calls, [])
        self.assertEqual(debugger.state, State.FAULTED)

    def test_open_failure_blocks_reopen(self):
        debugger = Debugger(self.backend)
        self.backend.failures.add("open")
        with self.assertRaisesRegex(TransportError, "open failed"):
            debugger.open(self.address)
        self.assertEqual(debugger.state, State.FAULTED)
        with self.assertRaisesRegex(DebuggerError, "faulted"):
            debugger.open(self.address)
        debugger.close()
        self.assertEqual(self.backend.calls[-1], ("close",))

    def test_invalid_address_before_backend_open(self):
        debugger = Debugger(self.backend)
        with self.assertRaisesRegex(ValueError, "UsbAddress"):
            debugger.open(None)
        self.assertEqual(debugger.state, State.NEW)
        self.assertEqual(self.backend.calls, [])

    def test_closed_and_new_sessions_reject_reads(self):
        for closed in (False, True):
            with self.subTest(closed=closed):
                debugger = Debugger(self.backend)
                if closed:
                    debugger.close()
                self.backend.calls.clear()
                with self.assertRaisesRegex(DebuggerError, "Session is"):
                    debugger.read_adapter_state()
                self.assertEqual(self.backend.calls, [])

    def test_context_closes_on_success_and_failure(self):
        for fail in (False, True):
            with self.subTest(fail=fail):
                backend = FakeBackend()
                if fail:
                    backend.failures.add("control")
                try:
                    with Debugger(backend) as debugger:
                        debugger.open(self.address)
                        debugger.read_adapter_state()
                except TransportError:
                    self.assertTrue(fail)
                self.assertEqual(debugger.state, State.CLOSED)
                self.assertEqual(backend.calls[-1], ("close",))

    def test_cleanup_failure_preserves_primary_error(self):
        backend = FakeBackend()
        backend.failures.update(("control", "close"))
        with self.assertRaisesRegex(DebuggerError, "control failed; cleanup also failed: close failed"):
            with Debugger(backend) as debugger:
                debugger.open(self.address)
                debugger.read_adapter_state()
        self.assertEqual(debugger.state, State.CLOSED)

    def test_close_is_terminal_and_idempotent(self):
        self.backend.failures.add("close")
        with self.assertRaisesRegex(TransportError, "close failed"):
            self.debugger.close()
        self.debugger.close()
        self.assertEqual(self.debugger.state, State.CLOSED)
        self.assertEqual(self.backend.calls, [("close",)])

    def test_invalid_timeouts_and_policies(self):
        for timeout in (0, -1, 60_001, True, 1.5, "1000"):
            with self.subTest(timeout=timeout), self.assertRaises(ValueError):
                Debugger(self.backend, timeout_ms=timeout)
        with self.assertRaisesRegex(ValueError, "Access"):
            Debugger(self.backend, access="existing-debug-session")
        self.assertEqual(self.backend.calls, [])

    def test_usb_address_bounds(self):
        for bus, address in ((0, 1), (256, 1), (1, 0), (1, 128), (True, 1), (1, 2.0)):
            with self.subTest(bus=bus, address=address), self.assertRaises(ValueError):
                UsbAddress(bus, address)
        self.assertEqual(UsbAddress(255, 127).address, 127)


class CliTests(unittest.TestCase):
    def run_cli(self, arguments, backend):
        stdout, stderr = StringIO(), StringIO()
        with patch.object(PyUsbBackend, "load", return_value=backend) as load:
            with redirect_stdout(stdout), redirect_stderr(stderr):
                result = main(arguments)
        return result, stdout.getvalue(), stderr.getvalue(), load

    def test_adapter_state_json_is_emitted_only_after_close(self):
        backend = FakeBackend()
        code, out, err, _ = self.run_cli(["adapter-state", "--bus", "1", "--address", "2"], backend)
        self.assertEqual(code, 0)
        self.assertEqual(err, "")
        self.assertEqual(json.loads(out), {"target_id": 0x2530, "firmware_version": 0x1234,
                                          "firmware_revision": 0x5678})
        self.assertEqual(backend.calls[-1], ("close",))

    def test_missing_target_consent_and_invalid_bounds_do_not_load_backend(self):
        for arguments in (["debug-status", "--bus", "1", "--address", "2"],
                          ["debug-config", "--bus", "1", "--address", "2"],
                          ["adapter-state", "--bus", "0", "--address", "2"],
                          ["adapter-state", "--bus", "1", "--address", "2", "--timeout-ms", "0"]):
            with self.subTest(arguments=arguments):
                backend = FakeBackend()
                code, out, err, load = self.run_cli(arguments, backend)
                self.assertEqual((code, out), (1, ""))
                self.assertTrue(err.startswith("cc-debugger: "))
                load.assert_not_called()
                self.assertEqual(backend.calls, [])

    def test_explicit_target_read(self):
        code, out, err, _ = self.run_cli(
            ["debug-config", "--bus", "1", "--address", "2", "--confirm-existing-debug-session"],
            FakeBackend())
        self.assertEqual((code, json.loads(out), err), (0, {"debug_config": 0x20}, ""))

    def test_io_and_cleanup_failures_never_print_success(self):
        for phase in ("open", "control", "close"):
            with self.subTest(phase=phase):
                backend = FakeBackend()
                backend.failures.add(phase)
                code, out, err, _ = self.run_cli(["adapter-state", "--bus", "1", "--address", "2"], backend)
                self.assertEqual((code, out), (1, ""))
                self.assertIn(f"{phase} failed", err)
                self.assertEqual(backend.calls[-1], ("close",))

    def test_help_does_not_load_or_enumerate_usb(self):
        with patch.object(PyUsbBackend, "load") as load, redirect_stdout(StringIO()):
            with self.assertRaises(SystemExit) as result:
                main(["--help"])
        self.assertEqual(result.exception.code, 0)
        load.assert_not_called()

    def test_missing_optional_dependency_is_explicit(self):
        with patch("cc_debugger.importlib.import_module", side_effect=ModuleNotFoundError("usb")):
            with self.assertRaisesRegex(DebuggerError, "requirements-debug.txt"):
                PyUsbBackend.load()


if __name__ == "__main__":
    unittest.main()
