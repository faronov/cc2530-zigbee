# SPDX-License-Identifier: BSD-3-Clause
"""The manual hardware runner is tested with mocks only, never USB discovery."""

from contextlib import nullcontext, redirect_stderr, redirect_stdout
from io import StringIO
import json
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

import check_debug_hardware as hardware_check
from cc_debugger import Access, Debugger, DebuggerError, TransportError


class HardwareRunnerTests(unittest.TestCase):
    arguments = ["--bus", "1", "--address", "2", "--board", "generic", "--output", "unused"]

    def invoke(self, arguments):
        out, error = StringIO(), StringIO()
        with redirect_stdout(out), redirect_stderr(error):
            status = hardware_check.main(arguments)
        return status, out.getvalue(), error.getvalue()

    def test_explicit_confirmation_is_required_before_loading_usb(self):
        with patch.object(hardware_check.PyUsbBackend, "load") as load:
            status, output, error = self.invoke(self.arguments)
        self.assertEqual(status, 1)
        self.assertEqual(output, "")
        self.assertIn("--confirm-fixture-test", error)
        load.assert_not_called()

    def test_invalid_cycles_and_usb_address_never_load_backend(self):
        for extra in (["--cycles", "0"], ["--cycles", "258"], ["--bus", "0"],
                      ["--address", "128"]):
            with self.subTest(extra=extra), patch.object(hardware_check.PyUsbBackend, "load") as load:
                status, output, error = self.invoke(self.arguments + ["--confirm-fixture-test"] + extra)
                self.assertEqual(status, 1)
                self.assertEqual(output, "")
                self.assertTrue(error)
                load.assert_not_called()

    def test_invalid_image_never_loads_backend(self):
        with patch.object(hardware_check, "DebugImage", side_effect=ValueError("mixed image")), \
                patch.object(hardware_check.PyUsbBackend, "load") as load:
            status, output, error = self.invoke(self.arguments + ["--confirm-fixture-test"])
        self.assertEqual(status, 1)
        self.assertEqual(output, "")
        self.assertIn("mixed image", error)
        load.assert_not_called()

    def test_success_is_emitted_only_after_cleanup(self):
        for cleanup_error in (None, DebuggerError("release failed")):
            with self.subTest(cleanup_error=cleanup_error), \
                    patch.object(hardware_check, "DebugImage") as image, \
                    patch.object(hardware_check.Path, "read_bytes", return_value=b"fixture"), \
                    patch.object(hardware_check.PyUsbBackend, "load") as load, \
                    patch.object(hardware_check, "Debugger") as constructor, \
                    patch.object(hardware_check, "exercise", return_value={"evidence": "synthetic-test"}) as exercise:
                context = constructor.return_value
                context.__exit__.return_value = False
                context.__exit__.side_effect = cleanup_error
                status, output, error = self.invoke(self.arguments + ["--confirm-fixture-test"])
                constructor.assert_called_once_with(
                    load.return_value, Access.RESET_DEBUG_SESSION, timeout_ms=10_000,
                    allow_cpu_control=True, allow_target_reset=True, allow_memory_access=True,
                    allow_memory_write=True, allow_breakpoints=True)
                exercise.assert_called_once_with(context.__enter__.return_value, image.return_value,
                                                 b"fixture", 257)
                if cleanup_error is None:
                    self.assertEqual(status, 0)
                    self.assertEqual(json.loads(output), {"evidence": "synthetic-test"})
                    self.assertEqual(error, "")
                else:
                    self.assertEqual(status, 1)
                    self.assertEqual(output, "")
                    self.assertIn("release failed", error)

    def waiter(self, statuses, pc=0x1234):
        return SimpleNamespace(_target_operation=Mock(return_value=nullcontext(object())),
                               _exchange_byte=Mock(side_effect=statuses),
                               _check_status=Debugger._check_status, _pc=Mock(return_value=pc))

    def test_breakpoint_wait_allows_startup_latency_in_one_operation(self):
        debugger = self.waiter([0x03, 0x03, 0x2B])
        hardware_check.wait_breakpoint(debugger, 0x1234)
        debugger._target_operation.assert_called_once_with()
        self.assertEqual(debugger._exchange_byte.call_count, 3)
        debugger._pc.assert_called_once()

    def test_wait_rejects_wrong_cause_address_or_target_state(self):
        for status, pc, error in ((0x23, 0x1234, ValueError), (0x2B, 0x2345, ValueError),
                                  (0x26, 0x1234, TransportError), (0x32, 0x1234, TransportError)):
            with self.subTest(status=status, pc=pc), self.assertRaises(error):
                hardware_check.wait_breakpoint(self.waiter([status], pc), 0x1234)

    def test_wait_propagates_transfer_deadline_without_retry(self):
        debugger = self.waiter([0x03, TransportError("deadline exceeded")])
        with self.assertRaisesRegex(TransportError, "deadline"):
            hardware_check.wait_breakpoint(debugger, 0x1234)
        self.assertEqual(debugger._exchange_byte.call_count, 2)
        debugger._pc.assert_not_called()


if __name__ == "__main__":
    unittest.main()
