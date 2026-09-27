# SPDX-License-Identifier: BSD-3-Clause
"""Synthetic physical CODE mapping and fail-stop USB access; no equipment."""
import unittest

from cc_debugger import Access, Debugger, DebuggerError, State, TransportError, UsbAddress
from test_m1_access import BAD_ACTIVE_STATUS, CoreBackend
from test_m1_transport import Clock


class FlashBackend(CoreBackend):
    flash = bytes((address * 37 + (address >> 8) * 19 + (address >> 15) * 71 + 0x83) & 255
                  for address in range(0x40000))

    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self.sfr[0xC7] = 0

    def execute(self, instruction):
        if instruction != b"\x93":
            return super().execute(instruction)
        low = 0x84 if self.sfr[0x92] & 1 else 0x82
        address = ((self.sfr[low] | self.sfr[low + 1] << 8) + self.sfr[0xE0]) & 0xFFFF
        if address >= 0x8000:
            if self.sfr[0xC7] & 8:
                raise AssertionError("A physical CODE reader must reject the XMAP overlay")
            address = (self.sfr[0x9F] & 7) * 0x8000 + address - 0x8000
        self.sfr[0xE0] = self.flash[address]
        self.memory_events.append(("code", address, self.sfr[0xE0]))
        self.update_parity()
        if self.after_instruction is not None:
            self.after_instruction(instruction)


class PhysicalFlashTests(unittest.TestCase):
    def session(self, access=Access.EXISTING_DEBUG_SESSION, allowed=True, **kwargs):
        self.clock = Clock()
        self.backend = FlashBackend(clock=self.clock, **kwargs)
        self.debugger = Debugger(self.backend, access, timeout_ms=10,
                                 allow_memory_access=allowed, clock=self.clock)
        self.debugger.open(UsbAddress(1, 2))
        self.backend.calls.clear()

    def assert_faulted(self):
        self.assertEqual(self.debugger.state, State.FAULTED)
        count = len(self.backend.calls)
        with self.assertRaises(DebuggerError):
            self.debugger.read_flash_code(0x8000, 1)
        self.assertEqual(len(self.backend.calls), count)

    def test_every_bank_and_boundary_preserves_full_cpu_and_both_pointers(self):
        for register_bank in range(4):
            for dps in range(2):
                for bank in range(8):
                    end = min((bank + 1) * 0x8000, 0x3E800)
                    for address, length in ((bank * 0x8000, 1), (bank * 0x8000 + 0xFF, 256),
                                            (end - 256, 256), (end - 1, 1)):
                        self.session(register_bank=register_bank, dps=dps, accumulator=0xFF)
                        self.backend.sfr[0x9F] = 0xD8 | ((bank + 3) & 7)
                        self.backend.sfr[0xC7] = 0x12
                        before, ram = self.backend.cpu_image(), bytes(self.backend.ram)
                        with self.subTest(register_bank=register_bank, dps=dps, address=address):
                            expected = self.backend.flash[address:address + length]
                            self.assertEqual(self.debugger.read_flash_code(address, length), expected)
                            self.assertEqual(self.backend.memory_events,
                                             [("code", address + i, byte) for i, byte in enumerate(expected)])
                            self.assertEqual(self.backend.cpu_image(), before)
                            self.assertEqual(bytes(self.backend.ram), ram)
                            self.assertEqual(self.debugger.state, State.OPEN)

    def test_invalid_inputs_and_bank_crossings_are_rejected_before_io(self):
        class Integer(int):
            pass

        self.session()
        cases = [(-1, 1), (0x3E800, 1), (0x3E7FF, 2), (0x40000, 1), (0x18000, 0),
                 (0, -1), (0, 257), (True, 1), (1.0, 1), ("0", 1), (None, 1),
                 (Integer(1), 1), (0, True), (0, 1.0), (0, "1"), (0, None), (0, Integer(1))]
        cases += [(bank * 0x8000 - 1, 2) for bank in range(1, 8)]
        for arguments in cases:
            with self.subTest(arguments=arguments), self.assertRaises(ValueError):
                self.debugger.read_flash_code(*arguments)
            self.assertEqual(self.backend.calls, [])
            self.assertEqual(self.debugger.state, State.OPEN)

    def test_permission_and_session_do_not_implicitly_upgrade(self):
        with self.assertRaises(ValueError):
            self.session(access=Access.ADAPTER_ONLY, allowed=True)
        for access, allowed in ((Access.ADAPTER_ONLY, False), (Access.EXISTING_DEBUG_SESSION, False)):
            self.session(access=access, allowed=allowed)
            with self.assertRaises(DebuggerError):
                self.debugger.read_flash_code(0x8000, 1)
            self.assertEqual(self.backend.calls, [])

    def test_original_logical_code_and_breakpoint_bounds_remain_unchanged(self):
        self.session()
        for operation, arguments in ((self.debugger.read_code, (0x8000, 1)),
                                     (self.debugger.set_breakpoint, (0, 0x8000))):
            with self.assertRaises(ValueError):
                operation(*arguments)
        self.assertEqual(self.backend.calls, [])

    def test_xmap_is_rejected_without_bank_write_or_memory_read(self):
        self.session()
        self.backend.sfr[0xC7] = 8
        with self.assertRaisesRegex(TransportError, "XMAP"):
            self.debugger.read_flash_code(0x8000, 1)
        self.assertEqual(self.backend.memory_events, [])
        self.assertFalse(any(packet[2:4] == b"\x75\x9f" for packet in self.backend.packets()))
        self.assert_faulted()

    def test_selection_restoration_and_memctr_changes_fail_closed(self):
        for fault in ("selection", "restoration", "bank-during-read", "memctr", "context"):
            self.session()
            writes = []

            def corrupt(instruction):
                if instruction[:2] == b"\x75\x9f":
                    writes.append(instruction)
                    if (fault == "selection" and len(writes) == 1 or
                            fault == "restoration" and len(writes) == 2):
                        self.backend.sfr[0x9F] ^= 1
                if instruction == b"\x93":
                    if fault == "bank-during-read":
                        self.backend.sfr[0x9F] ^= 1
                    elif fault == "memctr":
                        self.backend.sfr[0xC7] ^= 1
                    elif fault == "context":
                        self.backend.sfr[0x84] ^= 1

            self.backend.after_instruction = corrupt
            with self.subTest(fault=fault), self.assertRaises(TransportError):
                self.debugger.read_flash_code(0x10001, 1)
            if fault in ("selection", "bank-during-read", "memctr"):
                self.assertEqual(len(writes), 1)
            self.assert_faulted()

    def test_status_and_adapter_failures_prevent_bank_access(self):
        for status in BAD_ACTIVE_STATUS:
            self.session()
            self.backend.status = status
            with self.subTest(status=status), self.assertRaises(TransportError):
                self.debugger.read_flash_code(0x8000, 1)
            self.assertEqual(self.backend.packets(), [b"\x1f\x34"])
            self.assert_faulted()
        self.session()
        self.backend.control_reply = b"\x31\x25" + b"\0" * 6
        with self.assertRaises(TransportError):
            self.debugger.read_flash_code(0x8000, 1)
        self.assertEqual(self.backend.packets(), [])
        self.assert_faulted()

    def test_bad_final_status_rejects_even_a_complete_preserved_read(self):
        for status in BAD_ACTIVE_STATUS:
            self.session()
            self.backend.statuses.extend((0x22, status))
            before = self.backend.cpu_image()
            with self.subTest(status=status), self.assertRaises(TransportError):
                self.debugger.read_flash_code(0x10001, 1)
            self.assertEqual(self.backend.cpu_image(), before)
            self.assertEqual(self.backend.packets()[-1], b"\x1f\x34")
            self.assert_faulted()

    def test_failed_short_long_or_late_exchange_never_restores_or_retries(self):
        self.session()
        self.debugger.read_flash_code(0x18023, 2)
        calls = list(self.backend.calls)
        cases = [(index, "failure", None) for index in range(1, len(calls) + 1)]
        cases += [(index, "late", None) for index in range(1, len(calls) + 1)]
        cases += [(index, "reply", b"\0" * size) for index, call in enumerate(calls, 1)
                  if call[0] == "read" for size in (call[2] - 1, call[2] + 1)]
        cases += [(index, "write", size) for index, call in enumerate(calls, 1)
                  if call[0] == "write" for size in (len(call[2]) - 1, len(call[2]) + 1)]
        for index, kind, value in cases:
            self.session()
            if kind == "failure":
                self.backend.fail_at = index
            elif kind == "late":
                self.backend.late_at = index
            elif kind == "reply":
                self.backend.read_overrides[index] = value
            else:
                self.backend.write_counts[index] = value
            with self.subTest(index=index, kind=kind), self.assertRaises(TransportError):
                self.debugger.read_flash_code(0x18023, 2)
            self.assertEqual(len(self.backend.calls), index)
            self.assert_faulted()


if __name__ == "__main__":
    unittest.main()
