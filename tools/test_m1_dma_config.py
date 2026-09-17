# SPDX-License-Identifier: BSD-3-Clause
"""Synthetic reset-only DMA configuration policy and exact observed USB form."""

from collections import deque
import unittest
from unittest.mock import patch

from cc_debugger import Access, Debugger, DebuggerError, State, TransportError, UsbAddress
import test_m1_lifecycle as lifecycle_tests
from test_m1_transport import FakeBackend


STATUS = b"\x1f\x34"
CONFIG = b"\x1f\x24"
PC = b"\x3f\x28"
BANK = b"\x1f\x64"
ENABLE = b"\x4c\x1d\x22"


def enable_script(bank=1):
    return [
        (STATUS, b"\x22"), (PC, b"\0\0"), (BANK, bytes((bank,))), (CONFIG, b"\x26"),
        (ENABLE, None), (CONFIG, b"\x22"), (PC, b"\0\0"), (BANK, bytes((bank,))),
        (STATUS, b"\x22"),
    ]


class ConfigBackend(FakeBackend):
    def __init__(self, script=()):
        super().__init__()
        self.script = deque(script)
        self.pending = None
        self.fail_at = None
        self.config_written = 3

    def record(self, name, *args):
        super().record(name, *args)
        if len(self.calls) == self.fail_at:
            raise TransportError("synthetic transfer failure")

    def control_write(self, request_type, request, value, index, data, timeout_ms):
        self.record("control-write", request_type, request, value, index, data, timeout_ms)
        return len(data)

    def bulk_write(self, endpoint, data, timeout_ms):
        if endpoint != 0x04 or self.pending is not None or not self.script:
            raise AssertionError("Unexpected write or unread reply")
        packet, reply = self.script[0]
        if packet != data:
            raise AssertionError(f"Unexpected packet {data.hex()}, expected {packet.hex()}")
        self.record("write", endpoint, data, timeout_ms)
        self.script.popleft()
        if reply is not None:
            self.pending = (2 if packet == PC else 1, reply)
        return self.config_written if packet == ENABLE else len(data)

    def bulk_read(self, endpoint, length, timeout_ms):
        if endpoint != 0x84 or self.pending is None or self.pending[0] != length:
            raise AssertionError("Unexpected USB read; the enable packet has no USB reply")
        self.record("read", endpoint, length, timeout_ms)
        _, reply = self.pending
        self.pending = None
        return reply


class DmaConfigTests(unittest.TestCase):
    def session(self, *, ready=True, allow=True, cpu=True, timeout=100, script=None,
                access=Access.EXISTING_DEBUG_SESSION):
        backend = ConfigBackend()
        debugger = Debugger(backend, access, timeout, backend.clock,
                            allow_target_reset=True, allow_cpu_control=cpu,
                            allow_dma_enable=allow)
        debugger.open(UsbAddress(1, 2))
        if ready:
            if access == Access.RESET_DEBUG_SESSION:
                backend.script.extend([(STATUS, b"\x22")])
                debugger.attach_reset()
            else:
                backend.script.extend([(STATUS, b"\x22"), (STATUS, b"\x22")])
                debugger.reset_halt()
        backend.calls.clear()
        backend.script.extend(enable_script() if script is None else script)
        return debugger, backend

    def assert_faulted(self, debugger, backend):
        self.assertEqual(debugger.state, State.FAULTED)
        before = list(backend.calls)
        for name in ("enable_dma_after_reset", "read_debug_config", "read_pc", "read_bank",
                     "read_adapter_state", "read_debug_status", "reset_halt", "attach_reset",
                     "resume", "step", "halt"):
            with self.subTest(operation=name), self.assertRaisesRegex(DebuggerError, "faulted"):
                getattr(debugger, name)()
        self.assertEqual(backend.calls, before)
        debugger.close()
        self.assertEqual(backend.calls[-1], ("close",))

    def test_exact_write_only_form_fresh_queries_and_one_deadline(self):
        debugger, backend = self.session()
        backend.delays = {name: 1_000_000 for name in ("control", "write", "read")}
        self.assertEqual(debugger.enable_dma_after_reset(), 0x22)
        expected = [("control", 0xC0, 0xC0, 0, 0, 8)]
        for packet, reply in enable_script():
            expected.append(("write", 0x04, packet))
            if reply is not None:
                expected.append(("read", 0x84, 2 if packet == PC else 1))
        self.assertEqual([call[:-1] for call in backend.calls], expected)
        self.assertEqual([call[-1] for call in backend.calls],
                         list(range(100, 100 - len(expected), -1)))
        self.assertFalse(backend.script)
        self.assertEqual(debugger.state, State.OPEN)

    def test_fmap_is_preserved_not_mistaken_for_the_pc_bank(self):
        for bank in range(8):
            debugger, _ = self.session(script=enable_script(bank))
            self.assertEqual(debugger.enable_dma_after_reset(), 0x22)

    def test_both_explicit_reset_paths_qualify(self):
        for access in (Access.EXISTING_DEBUG_SESSION, Access.RESET_DEBUG_SESSION):
            debugger, _ = self.session(access=access)
            self.assertEqual(debugger.enable_dma_after_reset(), 0x22)

    def test_permission_is_boolean_separate_and_requires_reset_permission(self):
        for value in (None, 0, 1, "true"):
            with self.subTest(value=value), self.assertRaisesRegex(ValueError, "boolean"):
                Debugger(FakeBackend(), Access.EXISTING_DEBUG_SESSION,
                         allow_target_reset=True, allow_dma_enable=value)
        with self.assertRaisesRegex(ValueError, "debug-session"):
            Debugger(FakeBackend(), allow_dma_enable=True)
        with self.assertRaisesRegex(ValueError, "target-reset"):
            Debugger(FakeBackend(), Access.EXISTING_DEBUG_SESSION, allow_dma_enable=True)
        debugger, backend = self.session(allow=False)
        with self.assertRaisesRegex(DebuggerError, "separate explicit permission"):
            debugger.enable_dma_after_reset()
        self.assertEqual(backend.calls, [])
        self.assertEqual(debugger.state, State.OPEN)

    def test_existing_halt_or_unprepared_attach_never_proves_fresh_reset(self):
        for access in (Access.EXISTING_DEBUG_SESSION, Access.RESET_DEBUG_SESSION):
            debugger, backend = self.session(ready=False, access=access)
            with self.assertRaises(DebuggerError):
                debugger.enable_dma_after_reset()
            self.assertEqual(backend.calls, [])
            self.assertEqual(debugger.state, State.OPEN)

    def test_permission_does_not_grant_cpu_control(self):
        debugger, backend = self.session(cpu=False)
        self.assertEqual(debugger.enable_dma_after_reset(), 0x22)
        before = list(backend.calls)
        for name in ("resume", "step", "halt"):
            with self.assertRaisesRegex(DebuggerError, "CPU control"):
                getattr(debugger, name)()
        self.assertEqual(backend.calls, before)

    def test_enabling_consumes_eligibility_without_implicit_restore_on_close(self):
        debugger, backend = self.session()
        debugger.enable_dma_after_reset()
        before = list(backend.calls)
        with self.assertRaisesRegex(DebuggerError, "fresh reset"):
            debugger.enable_dma_after_reset()
        self.assertEqual(backend.calls, before)
        debugger.close()
        self.assertEqual(backend.calls, before + [("close",)])

    def test_explicit_second_reset_is_required_for_another_enable(self):
        debugger, backend = self.session()
        debugger.enable_dma_after_reset()
        backend.script.extend([(STATUS, b"\x22"), (STATUS, b"\x22")] + enable_script())
        debugger.reset_halt()
        debugger.enable_dma_after_reset()
        self.assertEqual(sum(call[0] == "write" and call[2] == ENABLE for call in backend.calls), 2)
        self.assertEqual(sum(call[0] == "control-write" for call in backend.calls), 1)

    def test_cpu_execution_or_observed_running_halt_consumes_reset_eligibility(self):
        operations = {
            "resume": [(STATUS, b"\x22"), (b"\x1f\x4c", b"\x02"), (STATUS, b"\x02")],
            "step": [(STATUS, b"\x22"), (b"\x1f\x5c", b"\x00"), (STATUS, b"\x22")],
            "halt": [(STATUS, b"\x02"), (b"\x1f\x44", b"\xff"), (STATUS, b"\x22")],
        }
        for name, script in operations.items():
            debugger, backend = self.session(script=script)
            getattr(debugger, name)()
            before = list(backend.calls)
            with self.subTest(operation=name), self.assertRaisesRegex(DebuggerError, "fresh reset"):
                debugger.enable_dma_after_reset()
            self.assertEqual(backend.calls, before)

    def test_passive_inspection_and_idempotent_halt_preserve_eligibility(self):
        script = [(STATUS, b"\x22"), (STATUS, b"\x22"), (PC, b"\0\0"), (STATUS, b"\x22")]
        debugger, _ = self.session(script=script + enable_script())
        self.assertFalse(debugger.halt().command_sent)
        self.assertEqual(debugger.read_pc(), 0)
        self.assertEqual(debugger.enable_dma_after_reset(), 0x22)

    def test_contradictory_passive_observations_consume_eligibility(self):
        for name, script in (
            ("read_debug_status", [(STATUS, b"\x02")]),
            ("read_debug_status", [(STATUS, b"\x23")]),
            ("read_debug_config", [(STATUS, b"\x22"), (CONFIG, b"\x22"), (STATUS, b"\x22")]),
            ("read_pc", [(STATUS, b"\x22"), (PC, b"\0\x01"), (STATUS, b"\x22")]),
        ):
            debugger, backend = self.session(script=script + enable_script())
            getattr(debugger, name)()
            before = list(backend.calls)
            with self.subTest(operation=name, script=script), self.assertRaisesRegex(DebuggerError, "fresh reset"):
                debugger.enable_dma_after_reset()
            self.assertEqual(backend.calls, before)

    def test_observed_target_change_consumes_eligibility(self):
        debugger, backend = self.session()
        original = backend.control_reply
        backend.control_reply = b"\0" * 8
        self.assertEqual(debugger.read_adapter_state().target_id, 0)
        backend.control_reply = original
        before = list(backend.calls)
        with self.assertRaisesRegex(DebuggerError, "fresh reset"):
            debugger.enable_dma_after_reset()
        self.assertEqual(backend.calls, before)

    def test_nonstandard_halted_reset_status_does_not_qualify(self):
        debugger, backend = self.session(ready=False, script=[(STATUS, b"\x22"), (STATUS, b"\x23")])
        debugger.reset_halt()
        before = list(backend.calls)
        with self.assertRaisesRegex(DebuggerError, "fresh reset"):
            debugger.enable_dma_after_reset()
        self.assertEqual(backend.calls, before)

    def test_every_wrong_pre_status_or_config_stops_before_enable(self):
        for index, expected in ((0, 0x22), (3, 0x26)):
            for value in range(256):
                if value == expected:
                    continue
                script = enable_script()
                script[index] = (script[index][0], bytes((value,)))
                debugger, backend = self.session(script=script)
                with self.subTest(index=index, value=value), self.assertRaises(TransportError):
                    debugger.enable_dma_after_reset()
                self.assertNotIn(ENABLE, [call[2] for call in backend.calls if call[0] == "write"])
                self.assert_faulted(debugger, backend)

    def test_every_wrong_post_status_or_config_latches_without_retry(self):
        for index in (5, 8):
            for value in range(256):
                if value == 0x22:
                    continue
                script = enable_script()
                script[index] = (script[index][0], bytes((value,)))
                debugger, backend = self.session(script=script)
                with self.subTest(index=index, value=value), self.assertRaises(TransportError):
                    debugger.enable_dma_after_reset()
                self.assertEqual(sum(call[0] == "write" and call[2] == ENABLE for call in backend.calls), 1)
                self.assert_faulted(debugger, backend)

    def test_nonzero_pc_and_changed_fmap_never_allow_success(self):
        for index, values in ((1, (1, 0x8000, 0xffff)), (6, (1, 0x8000, 0xffff)),
                              (7, (0, 2, 3, 4, 5, 6, 7))):
            for value in values:
                script = enable_script()
                script[index] = (script[index][0], value.to_bytes(1 if index == 7 else 2, "big"))
                debugger, backend = self.session(script=script)
                with self.subTest(index=index, value=value), self.assertRaises(TransportError):
                    debugger.enable_dma_after_reset()
                self.assert_faulted(debugger, backend)

    def test_every_wrong_target_stops_before_bulk_io(self):
        for target in (0, 0x2531, 0xffff):
            debugger, backend = self.session()
            backend.control_reply = target.to_bytes(2, "little") + b"\0" * 6
            with self.assertRaisesRegex(TransportError, "CC2530"):
                debugger.enable_dma_after_reset()
            self.assertEqual([call[0] for call in backend.calls], ["control"])
            self.assert_faulted(debugger, backend)

    def test_short_noninteger_or_boolean_enable_write_is_never_retried(self):
        for count in (0, 1, 2, 4, -1, True, False, None, 3.0):
            debugger, backend = self.session()
            backend.config_written = count
            with self.subTest(count=count), self.assertRaisesRegex(TransportError, "Incomplete"):
                debugger.enable_dma_after_reset()
            self.assertEqual(backend.calls[-1][2], ENABLE)
            self.assert_faulted(debugger, backend)

    def test_malformed_replies_fail_at_the_exact_read(self):
        for index in (0, 1, 2, 3, 5, 6, 7, 8):
            for reply in (b"", b"\0\0\0", bytearray(b"\x22"), None):
                script = enable_script()
                # None in a script denotes no reply; inject invalid None at the read instead.
                original = script[index][1]
                script[index] = (script[index][0], original if reply is None else reply)
                debugger, backend = self.session(script=script)
                read = backend.bulk_read
                reads = 0
                wanted = index + 1 if index < 4 else index

                def malformed(*args):
                    nonlocal reads
                    result = read(*args)
                    reads += 1
                    return None if reply is None and reads == wanted else result

                with self.subTest(index=index, reply=reply), patch.object(backend, "bulk_read", side_effect=malformed):
                    with self.assertRaisesRegex(TransportError, "exactly"):
                        debugger.enable_dma_after_reset()
                self.assert_faulted(debugger, backend)

    def test_failure_at_every_io_boundary_never_restores_or_resumes(self):
        debugger, backend = self.session()
        debugger.enable_dma_after_reset()
        expected = list(backend.calls)
        for fail_at in range(1, len(expected) + 1):
            debugger, backend = self.session()
            backend.fail_at = fail_at
            with self.subTest(fail_at=fail_at), self.assertRaises(TransportError):
                debugger.enable_dma_after_reset()
            self.assertEqual(backend.calls, expected[:fail_at])
            self.assert_faulted(debugger, backend)

    def test_expiration_after_any_io_stops_before_the_next_io(self):
        debugger, backend = self.session()
        debugger.enable_dma_after_reset()
        expected = list(backend.calls)
        for late_at in range(1, len(expected) + 1):
            debugger, backend = self.session()
            record = backend.record

            def late(name, *args):
                record(name, *args)
                if len(backend.calls) == late_at:
                    backend.clock.now += 100_000_000

            with self.subTest(late_at=late_at), patch.object(backend, "record", side_effect=late):
                with self.assertRaisesRegex(TransportError, "deadline"):
                    debugger.enable_dma_after_reset()
            self.assertEqual(backend.calls, expected[:late_at])
            self.assert_faulted(debugger, backend)

    def test_enable_is_lifecycle_exclusive_at_every_backend_boundary(self):
        debugger, backend = self.session()
        check = lifecycle_tests.LifecycleTests()
        self.assertEqual(check.assert_exclusive_during_backend_calls(
            debugger, backend, debugger.enable_dma_after_reset), 0x22)

    def test_failed_reset_never_leaves_a_usable_enable_permission(self):
        debugger, backend = self.session(script=[(STATUS, b"\x22"), (STATUS, b"\x20")])
        with self.assertRaises(TransportError):
            debugger.reset_halt()
        self.assert_faulted(debugger, backend)


if __name__ == "__main__":
    unittest.main()
