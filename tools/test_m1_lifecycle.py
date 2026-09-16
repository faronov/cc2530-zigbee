# SPDX-License-Identifier: BSD-3-Clause
"""Exclusive debugger lifecycle checks with synthetic, bounded backends."""

from concurrent.futures import ThreadPoolExecutor
from threading import Event
import unittest
from unittest.mock import patch

from cc2530_debug import GET_BM, HALT, READ_CONFIG, READ_STATUS, RESUME, STEP_INSTR
from cc_debugger import Access, Debugger, DebuggerError, State, TransportError, UsbAddress
from test_m1_reset import AttachBackend
from test_m1_transport import FakeBackend


class LifecycleTests(unittest.TestCase):
    def session(self, command):
        script = {
            "read_adapter_state": [],
            "read_debug_status": [(READ_STATUS, 0x22)],
            "read_debug_config": [(READ_STATUS, 0x22), (READ_CONFIG, 0x26), (READ_STATUS, 0x22)],
            "read_bank": [(READ_STATUS, 0x22), (GET_BM, 3), (READ_STATUS, 0x22)],
            "halt": [(READ_STATUS, 0x02), (HALT, 0xFF), (READ_STATUS, 0x22)],
            "resume": [(READ_STATUS, 0x22), (RESUME, 0x02), (READ_STATUS, 0x02)],
            "step": [(READ_STATUS, 0x22), (STEP_INSTR, 0xFF), (READ_STATUS, 0x22)],
            "reset_halt": [(READ_STATUS, 0x22), (READ_STATUS, 0x22)],
            "attach_reset": [(READ_STATUS, 0x22)],
        }[command]
        backend = AttachBackend(script)
        access = Access.RESET_DEBUG_SESSION if command == "attach_reset" else Access.EXISTING_DEBUG_SESSION
        debugger = Debugger(backend, access, clock=backend.clock,
                            allow_cpu_control=True, allow_target_reset=True)
        debugger.open(UsbAddress(1, 2))
        backend.calls.clear()
        return debugger, backend

    def operations(self, debugger):
        return [lambda: debugger.open(UsbAddress(1, 2)), debugger.close] + [
            getattr(debugger, name) for name in (
                "read_adapter_state", "read_debug_status", "read_debug_config", "read_bank",
                "halt", "resume", "step", "reset_halt", "attach_reset",
            )
        ]

    def assert_exclusive_during_backend_calls(self, debugger, backend, operation):
        record = backend.record
        probes = []

        def reenter(name, *args):
            before = list(backend.calls)
            state = debugger.state
            for nested in self.operations(debugger):
                with self.assertRaisesRegex(DebuggerError, "busy"):
                    nested()
            self.assertEqual(debugger.state, state)
            self.assertEqual(backend.calls, before)
            probes.append(name)
            record(name, *args)

        with patch.object(backend, "record", side_effect=reenter):
            result = operation()
        self.assertTrue(probes)
        return result

    def test_close_during_read_cannot_resurrect_a_closed_session(self):
        debugger, backend = self.session("read_adapter_state")
        read = backend.control_read

        def close_during_read(*args):
            with self.assertRaisesRegex(DebuggerError, "busy"):
                debugger.close()
            return read(*args)

        with patch.object(backend, "control_read", side_effect=close_during_read):
            self.assertEqual(debugger.read_adapter_state().target_id, 0x2530)
        self.assertEqual(debugger.state, State.OPEN)
        self.assertEqual([call[0] for call in backend.calls], ["control"])
        debugger.close()
        self.assertEqual(debugger.state, State.CLOSED)

    def test_all_command_phases_reject_reentry_without_io_or_state_change(self):
        for name in (
            "read_adapter_state", "read_debug_status", "read_debug_config", "read_bank",
            "halt", "resume", "step", "reset_halt", "attach_reset",
        ):
            with self.subTest(command=name):
                debugger, backend = self.session(name)
                self.assert_exclusive_during_backend_calls(debugger, backend, getattr(debugger, name))
                self.assertEqual(debugger.state, State.OPEN)
                self.assertFalse(backend.script)
                debugger.close()
                self.assertEqual(debugger.state, State.CLOSED)

    def test_open_and_close_are_exclusive_too(self):
        backend = FakeBackend()
        debugger = Debugger(backend)
        self.assert_exclusive_during_backend_calls(
            debugger, backend, lambda: debugger.open(UsbAddress(1, 2)))
        self.assertEqual(debugger.state, State.OPEN)
        self.assert_exclusive_during_backend_calls(debugger, backend, debugger.close)
        self.assertEqual(debugger.state, State.CLOSED)
        debugger.close()
        self.assertEqual([call[0] for call in backend.calls], ["open", "close"])

    def test_another_thread_cannot_close_or_queue_work_behind_a_read(self):
        debugger, backend = self.session("read_adapter_state")
        entered, release = Event(), Event()
        read = backend.control_read

        def blocked_read(*args):
            entered.set()
            if not release.wait(5):
                raise AssertionError("Synthetic transfer was not released")
            return read(*args)

        with patch.object(backend, "control_read", side_effect=blocked_read), ThreadPoolExecutor(1) as worker:
            pending = worker.submit(debugger.read_adapter_state)
            try:
                self.assertTrue(entered.wait(5))
                for operation in self.operations(debugger):
                    with self.assertRaisesRegex(DebuggerError, "busy"):
                        operation()
                self.assertEqual(backend.calls, [])
            finally:
                release.set()
            self.assertEqual(pending.result(timeout=5).target_id, 0x2530)
        self.assertEqual(debugger.state, State.OPEN)
        self.assertEqual([call[0] for call in backend.calls], ["control"])
        debugger.close()

    def test_failure_and_interruption_release_the_guard_for_cleanup(self):
        for error in (TransportError("synthetic failure"), RuntimeError("backend bug"), KeyboardInterrupt()):
            with self.subTest(error=type(error)):
                debugger, backend = self.session("read_adapter_state")
                with patch.object(backend, "control_read", side_effect=error):
                    with self.assertRaises(type(error)):
                        debugger.read_adapter_state()
                self.assertEqual(debugger.state, State.FAULTED)
                debugger.close()
                self.assertEqual(debugger.state, State.CLOSED)
                self.assertEqual(backend.calls, [("close",)])

    def test_denied_policy_releases_guard_and_does_not_fault_session(self):
        backend = FakeBackend()
        debugger = Debugger(backend, Access.EXISTING_DEBUG_SESSION, clock=backend.clock)
        debugger.open(UsbAddress(1, 2))
        backend.calls.clear()
        for operation in (debugger.halt, debugger.resume, debugger.step,
                          debugger.reset_halt, debugger.attach_reset):
            with self.assertRaises(DebuggerError):
                operation()
            self.assertEqual(debugger.state, State.OPEN)
            self.assertEqual(backend.calls, [])
        self.assertEqual(debugger.read_adapter_state().target_id, 0x2530)
        debugger.close()


if __name__ == "__main__":
    unittest.main()
