# SPDX-License-Identifier: BSD-3-Clause
"""Original synthetic 8051 state and confirmed standalone USB conversations."""

from collections import deque
from contextlib import redirect_stderr, redirect_stdout
from dataclasses import asdict, fields, is_dataclass
from io import StringIO
import json
import unittest
from unittest.mock import Mock, patch

import cc_debugger as driver
from cc_debugger import Access, Debugger, DebuggerError, State, TransportError, UsbAddress
import test_m1_transport as transport_tests
from test_m1_transport import Clock, FakeBackend


SAFE_SFR = (0x81, 0x82, 0x83, 0x84, 0x85, 0x92, 0x93, 0x9F, 0xD0, 0xE0, 0xF0)
SNAPSHOT_FIELDS = ("pc", "bank", "a", "psw", "b", "sp", "dptr0", "dptr1",
                   "dps", "mpage", "r")
BAD_ACTIVE_STATUS = (0x02, 0x20, 0x26, 0xA2, 0x32, 0x62)
OPERATIONS = (
    ("read_pc", ()),
    ("read_registers", ()),
    ("read_sfr", (0x81,)),
    ("read_xdata", (0x123, 1)),
    ("read_code", (0x234, 1)),
    ("write_xdata", (0x345, b"\xD7")),
    ("set_breakpoint", (2, 0x4567)),
)


class CoreBackend(FakeBackend):
    """A small instruction model, not a general adapter-bytecode interpreter."""

    code = bytes((address * 37 + (address >> 8) * 19 + 0x83) & 255
                 for address in range(0x8000))
    initial_ram = bytes((address * 53 + (address >> 8) * 7 + 0x91) & 255
                        for address in range(0x1F00))

    def __init__(self, clock=None, register_bank=0, dps=0, accumulator=0xA5):
        super().__init__(clock)
        self.pc = 0x6A91
        self.sfr = {
            0x81: 0xB7, 0x82: 0xD9, 0x83: 0x8C, 0x84: 0xA6, 0x85: 0xF1,
            0x92: dps, 0x93: 0xD3, 0x9F: 0xA8, 0xD0: 0xC4 | register_bank << 3,
            0xE0: accumulator, 0xF0: 0xE9,
        }
        self.iram = bytearray((index * 29 + 0x47) & 255 for index in range(256))
        self.ram = bytearray(self.initial_ram)
        self.update_parity()
        self.status = 0x22
        self.statuses = deque()
        self.breakpoint_reply = None
        self.breakpoints = {}
        self.pending = None
        self.fail_at = None
        self.failure = TransportError("synthetic access failure")
        self.late_at = None
        self.late_ns = 10_000_000
        self.read_overrides = {}
        self.write_counts = {}
        self.after_instruction = None
        self.after_memory_write = None
        self.memory_events = []

    def update_parity(self):
        parity = bin(self.sfr[0xE0]).count("1") & 1
        self.sfr[0xD0] = (self.sfr[0xD0] & 0xFE) | parity

    def snapshot(self):
        bank_start = ((self.sfr[0xD0] >> 3) & 3) * 8
        return {
            "pc": self.pc, "bank": self.sfr[0x9F] & 7,
            "a": self.sfr[0xE0], "psw": self.sfr[0xD0], "b": self.sfr[0xF0],
            "sp": self.sfr[0x81], "dptr0": self.sfr[0x82] | self.sfr[0x83] << 8,
            "dptr1": self.sfr[0x84] | self.sfr[0x85] << 8,
            "dps": self.sfr[0x92], "mpage": self.sfr[0x93],
            "r": tuple(self.iram[bank_start:bank_start + 8]),
        }

    def cpu_image(self):
        return self.pc, tuple(sorted(self.sfr.items())), bytes(self.iram)

    def xdata(self, address, length):
        return bytes(self.ram + self.iram)[address:address + length]

    def record(self, name, *args):
        super().record(name, *args)
        if len(self.calls) == self.fail_at:
            raise self.failure
        if len(self.calls) == self.late_at:
            self.clock.now += self.late_ns

    def control_write(self, *args):
        raise AssertionError("Access operations must never prepare or reset a target")

    def bulk_write(self, endpoint, data, timeout_ms):
        self.record("write", endpoint, data, timeout_ms)
        if endpoint != 0x04 or type(data) is not bytes or self.pending is not None:
            raise AssertionError("Unexpected OUT transfer or unread previous reply")
        if len(self.calls) in self.write_counts:
            return self.write_counts[len(self.calls)]
        if data == b"\x1F\x34":
            value = self.statuses.popleft() if self.statuses else self.status
            self.pending = bytes([value])
        elif data == b"\x1F\x64":
            self.pending = bytes([self.sfr[0x9F]])
        elif data == b"\x3F\x28":
            self.pending = self.pc.to_bytes(2, "big")
        elif len(data) == 5 and data[:2] == b"\xAF\x3F":
            control, high, low = data[2:]
            if control & 0xC7:
                raise AssertionError("Only bank-zero hardware breakpoints are allowed")
            self.breakpoints[(control >> 4) & 3] = (high << 8 | low, bool(control & 8))
            reply = self.status if self.breakpoint_reply is None else self.breakpoint_reply
            self.pending = bytes([reply])
        elif (len(data), data[:2]) in (
            (3, b"\x4F\x55"), (4, b"\x7F\x56"), (5, b"\xAF\x57"),
        ):
            self.execute(data[2:])
            self.pending = bytes([self.sfr[0xE0]])
        else:
            raise AssertionError(f"Unconfirmed or forbidden USB packet: {data.hex()}")
        return len(data)

    def bulk_read(self, endpoint, length, timeout_ms):
        self.record("read", endpoint, length, timeout_ms)
        if endpoint != 0x84 or self.pending is None or length != len(self.pending):
            raise AssertionError("Unexpected IN endpoint, byte count, or missing request")
        result = self.read_overrides.get(len(self.calls), self.pending)
        self.pending = None
        return result

    def execute(self, instruction):
        op = instruction[0]
        pointer_low = 0x84 if self.sfr[0x92] & 1 else 0x82
        pointer = self.sfr[pointer_low] | self.sfr[pointer_low + 1] << 8
        if instruction == b"\x00":
            pass
        elif len(instruction) == 2 and op == 0xE5:
            address = instruction[1]
            self.sfr[0xE0] = self.iram[address] if address < 0x80 else self.sfr[address]
        elif len(instruction) == 1 and 0xE8 <= op <= 0xEF:
            start = ((self.sfr[0xD0] >> 3) & 3) * 8
            self.sfr[0xE0] = self.iram[start + op - 0xE8]
        elif len(instruction) == 2 and op == 0x74:
            self.sfr[0xE0] = instruction[1]
        elif len(instruction) == 3 and op == 0x75:
            address, value = instruction[1:]
            if address < 0x80:
                self.iram[address] = value
            else:
                if address not in self.sfr:
                    raise AssertionError("Unexpected SFR write")
                self.sfr[address] = value
        elif len(instruction) == 3 and op == 0x90:
            self.sfr[pointer_low], self.sfr[pointer_low + 1] = instruction[2], instruction[1]
        elif instruction == b"\xA3":
            pointer = (pointer + 1) & 0xFFFF
            self.sfr[pointer_low], self.sfr[pointer_low + 1] = pointer & 255, pointer >> 8
        elif instruction == b"\xE0":
            if pointer >= 0x2000:
                raise AssertionError("XDATA read outside SRAM")
            self.sfr[0xE0] = self.xdata(pointer, 1)[0]
            self.memory_events.append(("read", pointer, self.sfr[0xE0]))
        elif instruction == b"\xF0":
            if pointer >= 0x1E00:
                raise AssertionError("Write to reserved, aliased, or MMIO XDATA")
            self.ram[pointer] = self.sfr[0xE0]
            self.memory_events.append(("write", pointer, self.sfr[0xE0]))
            if self.after_memory_write is not None:
                self.after_memory_write(pointer)
        elif instruction == b"\xE4":
            self.sfr[0xE0] = 0
        elif instruction == b"\x93":
            address = (pointer + self.sfr[0xE0]) & 0xFFFF
            if address >= 0x8000:
                raise AssertionError("CODE read outside the lower unbanked window")
            self.sfr[0xE0] = self.code[address]
            self.memory_events.append(("code", address, self.sfr[0xE0]))
        else:
            raise AssertionError(f"Unexpected 8051 instruction: {instruction.hex()}")
        self.update_parity()
        if self.after_instruction is not None:
            self.after_instruction(instruction)

    def packets(self):
        return [call[2] for call in self.calls if call[0] == "write"]


class FakeCoreTests(unittest.TestCase):
    def test_accumulator_parity_and_debug_instruction_pc_semantics(self):
        backend = CoreBackend()
        pc = backend.pc
        for value in range(256):
            backend.execute(bytes([0x74, value]))
            self.assertEqual(backend.sfr[0xE0], value)
            self.assertEqual(backend.sfr[0xD0] & 1, bin(value).count("1") & 1)
            backend.execute(b"\x75\xD0\xFF")
            self.assertEqual(backend.sfr[0xD0] & 1, bin(value).count("1") & 1)
            self.assertEqual(backend.pc, pc)

    def test_both_pointers_and_all_register_banks_are_independent(self):
        for bank in range(4):
            for dps in range(2):
                backend = CoreBackend(register_bank=bank, dps=dps)
                before = backend.snapshot()
                backend.execute(b"\x90\x12\xFF")
                backend.execute(b"\xA3")
                self.assertEqual(backend.snapshot()[f"dptr{dps}"], 0x1300)
                self.assertEqual(backend.snapshot()[f"dptr{1 - dps}"], before[f"dptr{1 - dps}"])
                for index in range(8):
                    backend.execute(bytes([0xE8 + index]))
                    self.assertEqual(backend.sfr[0xE0], backend.iram[bank * 8 + index])

    def test_iram_alias_and_movc_accumulator_offset(self):
        backend = CoreBackend()
        backend.execute(b"\x90\x1F\x18")
        backend.execute(b"\xE0")
        self.assertEqual(backend.sfr[0xE0], backend.iram[0x18])
        backend.execute(b"\x90\x01\xFE")
        backend.execute(b"\x74\x03")
        backend.execute(b"\x93")
        self.assertEqual(backend.sfr[0xE0], backend.code[0x201])

    def test_wrong_experimental_prefixes_are_not_accepted(self):
        for packet in (b"\x2F\x28", b"\x8F\x56\x74\xFF", b"\x1F\x28"):
            with self.subTest(packet=packet), self.assertRaises(AssertionError):
                CoreBackend().bulk_write(0x04, packet, 10)

    def test_confirmed_packets_return_accumulator_not_opcode_or_status(self):
        backend = CoreBackend(accumulator=0xD7)
        for packet, result in ((b"\x4F\x55\x00", b"\xD7"),
                               (b"\x7F\x56\x74\xFF", b"\xFF"),
                               (b"\xAF\x57\x90\x12\x34", b"\xFF")):
            self.assertEqual(backend.bulk_write(0x04, packet, 10), len(packet))
            self.assertEqual(backend.bulk_read(0x84, 1, 10), result)
        self.assertEqual(backend.snapshot()["dptr0"], 0x1234)
        backend.pc = 0xAB91
        self.assertEqual(backend.bulk_write(0x04, b"\x3F\x28", 10), 2)
        self.assertEqual(backend.bulk_read(0x84, 2, 10), b"\xAB\x91")


class AccessTests(unittest.TestCase):
    def setUp(self):
        loader = patch.object(driver.PyUsbBackend, "load",
                              side_effect=AssertionError("Tests must not load real USB"))
        loader.start()
        self.addCleanup(loader.stop)

    def session(self, *, memory=True, write=True, breakpoints=True,
                register_bank=0, dps=0, accumulator=0xA5, timeout_ms=10, **permissions):
        self.clock = Clock()
        self.backend = CoreBackend(self.clock, register_bank, dps, accumulator)
        self.debugger = Debugger(
            self.backend, Access.EXISTING_DEBUG_SESSION, timeout_ms, self.clock,
            allow_memory_access=memory, allow_memory_write=write,
            allow_breakpoints=breakpoints, **permissions,
        )
        self.debugger.open(UsbAddress(1, 2))
        self.backend.calls.clear()
        return self.debugger

    def invoke(self, operation):
        name, arguments = operation
        return getattr(self.debugger, name)(*arguments)

    def assert_denied(self, operation, error=DebuggerError):
        before = list(self.backend.calls)
        state = self.debugger.state
        with self.assertRaises(error):
            self.invoke(operation)
        self.assertEqual(self.backend.calls, before)
        self.assertEqual(self.debugger.state, state)

    def assert_faulted(self):
        self.assertEqual(self.debugger.state, State.FAULTED)
        before = list(self.backend.calls)
        for operation in OPERATIONS:
            with self.assertRaisesRegex(DebuggerError, "faulted"):
                self.invoke(operation)
        self.assertEqual(self.backend.calls, before)
        self.debugger.close()
        self.assertEqual(self.debugger.state, State.CLOSED)
        self.assertEqual(self.backend.calls, before + [("close",)])

    def assert_preserved(self, before):
        self.assertEqual(self.backend.cpu_image(), before)
        self.assertEqual(self.debugger.state, State.OPEN)
        self.assertIsNone(self.backend.pending)

    def test_permission_arguments_are_exact_booleans(self):
        for permission in ("allow_memory_access", "allow_memory_write", "allow_breakpoints"):
            for value in (None, 0, 1, "true", [], 1.0):
                backend = CoreBackend()
                options = {"allow_memory_access": True, permission: value}
                with self.subTest(permission=permission, value=value), self.assertRaises(ValueError):
                    Debugger(backend, Access.EXISTING_DEBUG_SESSION, **options)
                self.assertEqual(backend.calls, [])

    def test_adapter_only_cannot_be_upgraded_by_permissions(self):
        for permissions in ({"allow_memory_access": True}, {"allow_breakpoints": True},
                            {"allow_memory_access": True, "allow_memory_write": True}):
            backend = CoreBackend()
            with self.subTest(permissions=permissions), self.assertRaises(ValueError):
                Debugger(backend, **permissions)
            self.assertEqual(backend.calls, [])

    def test_write_permission_requires_memory_access(self):
        backend = CoreBackend()
        with self.assertRaises(ValueError):
            Debugger(backend, Access.EXISTING_DEBUG_SESSION, allow_memory_write=True)
        self.assertEqual(backend.calls, [])

    def test_defaults_deny_memory_and_breakpoints_before_io(self):
        self.backend = CoreBackend()
        self.debugger = Debugger(self.backend, Access.EXISTING_DEBUG_SESSION,
                                 clock=self.backend.clock)
        self.debugger.open(UsbAddress(1, 2))
        self.backend.calls.clear()
        for operation in OPERATIONS[1:]:
            with self.subTest(operation=operation):
                self.assert_denied(operation)
        self.assertEqual(self.debugger.read_pc(), self.backend.pc)

    def test_memory_read_permission_does_not_grant_write_breakpoints_or_cpu_control(self):
        self.session(write=False, breakpoints=False)
        for operation in (OPERATIONS[5], OPERATIONS[6], ("halt", ()), ("resume", ()),
                          ("step", ()), ("reset_halt", ())):
            with self.subTest(operation=operation):
                self.assert_denied(operation)
        self.assertEqual(self.debugger.read_xdata(0, 1), self.backend.xdata(0, 1))

    def test_breakpoint_permission_does_not_grant_memory_access(self):
        self.session(memory=False, write=False)
        for operation in OPERATIONS[1:6]:
            self.assert_denied(operation)
        self.assertEqual(self.debugger.set_breakpoint(0, 0).slot, 0)

    def test_cpu_and_reset_permissions_do_not_grant_access(self):
        self.session(memory=False, write=False, breakpoints=False,
                     allow_cpu_control=True, allow_target_reset=True)
        for operation in OPERATIONS[1:]:
            self.assert_denied(operation)
        self.assertEqual(self.backend.calls, [])

    def test_adapter_only_denies_read_pc_without_target_io(self):
        self.backend = CoreBackend()
        self.debugger = Debugger(self.backend)
        self.debugger.open(UsbAddress(1, 2))
        self.backend.calls.clear()
        self.assert_denied(OPERATIONS[0])
        self.assertEqual(self.debugger.read_adapter_state().target_id, 0x2530)

    def test_unprepared_reset_attach_denies_all_access(self):
        self.backend = CoreBackend()
        self.debugger = Debugger(
            self.backend, Access.RESET_DEBUG_SESSION, allow_target_reset=True,
            allow_memory_access=True, allow_memory_write=True, allow_breakpoints=True,
        )
        self.debugger.open(UsbAddress(1, 2))
        self.backend.calls.clear()
        for operation in OPERATIONS:
            self.assert_denied(operation)
        self.assertEqual(self.backend.calls, [])

    def test_open_and_close_with_permissions_do_not_send_target_commands(self):
        backend = CoreBackend()
        debugger = Debugger(backend, Access.EXISTING_DEBUG_SESSION,
                            allow_memory_access=True, allow_memory_write=True,
                            allow_breakpoints=True)
        self.assertEqual(backend.calls, [])
        debugger.open(UsbAddress(1, 2))
        debugger.close()
        self.assertEqual(backend.calls, [("open", UsbAddress(1, 2), 1000), ("close",)])

    def test_new_and_closed_sessions_reject_access(self):
        for closed in (False, True):
            self.backend = CoreBackend()
            self.debugger = Debugger(self.backend, Access.EXISTING_DEBUG_SESSION,
                                     allow_memory_access=True, allow_memory_write=True,
                                     allow_breakpoints=True)
            if closed:
                self.debugger.close()
            self.backend.calls.clear()
            for operation in OPERATIONS:
                self.assert_denied(operation)

    def test_read_pc_exact_packet_two_byte_big_endian_reply(self):
        for pc in (0, 1, 0x7FFF, 0x8000, 0xAB91, 0xFFFF):
            self.session(memory=False, write=False, breakpoints=False)
            self.backend.pc = pc
            with self.subTest(pc=pc):
                self.assertEqual(self.debugger.read_pc(), pc)
                self.assertEqual(self.backend.calls, [
                    ("control", 0xC0, 0xC0, 0, 0, 8, 10),
                    ("write", 0x04, b"\x1F\x34", 10), ("read", 0x84, 1, 10),
                    ("write", 0x04, b"\x3F\x28", 10), ("read", 0x84, 2, 10),
                    ("write", 0x04, b"\x1F\x34", 10), ("read", 0x84, 1, 10),
                ])

    def test_snapshot_fields_and_preservation_in_every_register_bank_and_dps(self):
        for bank in range(4):
            for dps in range(2):
                self.session(register_bank=bank, dps=dps)
                before = self.backend.cpu_image()
                expected = self.backend.snapshot()
                with self.subTest(bank=bank, dps=dps):
                    result = self.debugger.read_registers()
                    self.assertIs(type(result), driver.RegisterSnapshot)
                    self.assertTrue(is_dataclass(result))
                    self.assertEqual(tuple(field.name for field in fields(result)), SNAPSHOT_FIELDS)
                    self.assertIs(type(result.r), tuple)
                    self.assertEqual(asdict(result), expected)
                    self.assert_preserved(before)

    def test_all_accumulator_values_are_data_and_parity_is_preserved(self):
        for accumulator in range(256):
            self.session(accumulator=accumulator, register_bank=accumulator % 4,
                         dps=(accumulator >> 2) & 1)
            before = self.backend.cpu_image()
            with self.subTest(accumulator=accumulator):
                result = self.debugger.read_registers()
                self.assertEqual(asdict(result), self.backend.snapshot())
                self.assertEqual(result.a, accumulator)
                self.assertEqual(result.psw & 1, bin(accumulator).count("1") & 1)
                self.assert_preserved(before)

    def test_snapshot_saves_accumulator_with_nop_before_reading_psw(self):
        self.session()
        self.debugger.read_registers()
        instructions = [packet for packet in self.backend.packets()
                        if packet[:2] in (b"\x4F\x55", b"\x7F\x56", b"\xAF\x57")]
        self.assertEqual(instructions[:2], [b"\x4F\x55\x00", b"\x7F\x56\xE5\xD0"])
        self.assertTrue(any(packet[:3] == b"\xAF\x57\x75" for packet in instructions))

    def test_safe_sfr_reads_return_original_values_without_state_changes(self):
        for bank in range(4):
            for dps in range(2):
                self.session(register_bank=bank, dps=dps)
                for address in SAFE_SFR:
                    before = self.backend.cpu_image()
                    expected = self.backend.sfr[address]
                    with self.subTest(bank=bank, dps=dps, address=address):
                        self.assertEqual(self.debugger.read_sfr(address), expected)
                        self.assert_preserved(before)

    def test_all_other_sfr_addresses_are_rejected_without_fault_or_io(self):
        self.session()
        invalid = [address for address in range(256) if address not in SAFE_SFR]
        invalid += [-1, 256, True, False, 0x81 + 0.0, "129", None]
        for address in invalid:
            with self.subTest(address=address):
                self.assert_denied(("read_sfr", (address,)), ValueError)
        self.assertEqual(self.debugger.read_pc(), self.backend.pc)

    def test_memory_range_validation_is_exact_and_pre_io(self):
        class IntSubclass(int):
            pass

        self.session()
        for name, end in (("read_xdata", 0x2000), ("read_code", 0x8000)):
            cases = [
                (-1, 1), (end, 1), (end - 1, 2), (end - 255, 256),
                (0, 0), (0, -1), (0, 257), (True, 1), (False, 1),
                (0.0, 1), ("0", 1), (None, 1), (0, True), (0, False),
                (0, 1.0), (0, "1"), (0, None), (0, IntSubclass(1)),
            ]
            for arguments in cases:
                with self.subTest(name=name, arguments=arguments):
                    self.assert_denied((name, arguments), ValueError)
        self.assertEqual(self.debugger.read_pc(), self.backend.pc)

    def test_xdata_read_boundaries_include_status_and_iram_alias(self):
        for address, length in ((0, 1), (0, 256), (0xFF, 256), (0x1DFF, 1),
                                (0x1E00, 64), (0x1EFF, 2), (0x1F00, 256),
                                (0x1FFF, 1), (0x1F01, 255), (0x1E80, 256)):
            self.session(register_bank=3, dps=1)
            before = self.backend.cpu_image()
            ram = bytes(self.backend.ram)
            expected = self.backend.xdata(address, length)
            with self.subTest(address=address, length=length):
                result = self.debugger.read_xdata(address, length)
                self.assertIs(type(result), bytes)
                self.assertEqual(result, expected)
                self.assertEqual(self.backend.memory_events,
                                 [("read", address + i, value) for i, value in enumerate(expected)])
                self.assertEqual(bytes(self.backend.ram), ram)
                self.assert_preserved(before)

    def test_code_read_boundaries_and_movc_zero_accumulator(self):
        for address, length in ((0, 1), (0, 256), (0xFF, 256), (0x1234, 17),
                                (0x7F00, 256), (0x7FFF, 1), (0x7F01, 255)):
            self.session(dps=1, accumulator=0xFF)
            before = self.backend.cpu_image()
            expected = self.backend.code[address:address + length]
            with self.subTest(address=address, length=length):
                result = self.debugger.read_code(address, length)
                self.assertIs(type(result), bytes)
                self.assertEqual(result, expected)
                self.assertEqual(self.backend.memory_events,
                                 [("code", address + i, value) for i, value in enumerate(expected)])
                self.assert_preserved(before)

    def test_writes_validate_data_and_keep_reserved_status_alias_and_mmio_out(self):
        self.session()
        cases = [
            (-1, b"x"), (0x1E00, b"x"), (0x1DFF, b"xy"), (0x1F00, b"x"),
            (0x1FFF, b"x"), (0x2000, b"x"), (0x70E0, b"x"), (0x10000, b"x"),
            (True, b"x"), (0.0, b"x"), ("0", b"x"), (None, b"x"),
            (0, b""), (0, b"x" * 257), (0, bytearray(b"x")), (0, memoryview(b"x")),
            (0, "x"), (0, [1]), (0, 1), (0, None),
        ]
        for arguments in cases:
            with self.subTest(arguments=arguments):
                self.assert_denied(("write_xdata", arguments), ValueError)
        self.assertEqual(self.debugger.read_pc(), self.backend.pc)

    def test_write_boundaries_readback_every_byte_and_preserve_other_memory(self):
        for address, data in ((0, b"\xFF"), (0, bytes(range(256))),
                              (0x1D00, bytes(range(255, -1, -1))), (0x1DFF, b"\x80"),
                              (0x1C81, bytes(range(256)))):
            self.session(register_bank=2, dps=1)
            before = self.backend.cpu_image()
            expected = bytearray(self.backend.ram)
            expected[address:address + len(data)] = data
            with self.subTest(address=address, length=len(data)):
                self.assertIsNone(self.debugger.write_xdata(address, data))
                self.assertEqual(self.backend.ram, expected)
                writes = [event for event in self.backend.memory_events if event[0] == "write"]
                reads = [event for event in self.backend.memory_events if event[0] == "read"]
                self.assertEqual(writes, [("write", address + i, value)
                                          for i, value in enumerate(data)])
                self.assertEqual(reads, [("read", address + i, value)
                                         for i, value in enumerate(data)])
                for write, read in zip(writes, reads):
                    self.assertLess(self.backend.memory_events.index(write),
                                    self.backend.memory_events.index(read))
                self.assert_preserved(before)

    def test_memory_operations_preserve_all_banks_both_dptrs_and_dps(self):
        for operation in OPERATIONS[3:6]:
            for bank in range(4):
                for dps in range(2):
                    self.session(register_bank=bank, dps=dps, accumulator=0xF7)
                    before = self.backend.cpu_image()
                    with self.subTest(operation=operation, bank=bank, dps=dps):
                        self.invoke(operation)
                        self.assert_preserved(before)

    def test_memory_access_preserves_nonzero_code_bank(self):
        for bank in range(8):
            self.session(dps=1)
            self.backend.sfr[0x9F] = 0xD8 | bank
            before = self.backend.cpu_image()
            self.assertEqual(self.debugger.read_code(0x1234, 2), self.backend.code[0x1234:0x1236])
            self.assertEqual(self.debugger.read_registers().bank, bank)
            self.assert_preserved(before)

    def test_three_instruction_lengths_use_only_confirmed_standalone_prefixes(self):
        self.session()
        self.debugger.write_xdata(0x1234, b"\x80\xFF")
        packets = self.backend.packets()
        self.assertIn(b"\x3F\x28", packets)
        self.assertIn(b"\x4F\x55\x00", packets)
        self.assertIn(b"\x7F\x56\xE5\xD0", packets)
        self.assertIn(b"\xAF\x57\x90\x12\x34", packets)
        self.assertTrue(any(packet[:3] == b"\x7F\x56\x74" for packet in packets))
        self.assertFalse(any(packet[:2] in (b"\x2F\x28", b"\x8F\x56") for packet in packets))
        for index, call in enumerate(self.backend.calls):
            if call[0] == "write" and call[2][:2] in (b"\x4F\x55", b"\x7F\x56", b"\xAF\x57"):
                self.assertEqual(self.backend.calls[index + 1][:3], ("read", 0x84, 1))

    def test_breakpoint_slot_enable_address_and_status_result(self):
        for slot in range(4):
            for address in (0, 0x1234, 0x7FFF):
                for enabled in (False, True):
                    self.session()
                    self.backend.statuses.extend((0x22, 0x2A))
                    self.backend.breakpoint_reply = 0x2A
                    before = self.backend.cpu_image()
                    with self.subTest(slot=slot, address=address, enabled=enabled):
                        result = self.debugger.set_breakpoint(slot, address, enabled)
                        self.assertIs(type(result), driver.BreakpointResult)
                        self.assertEqual(asdict(result), {
                            "slot": slot, "address": address, "enabled": enabled, "status_after": 0x2A,
                        })
                        packet = bytes([0xAF, 0x3F, slot << 4 | (8 if enabled else 0),
                                        address >> 8, address & 255])
                        packets = self.backend.packets()
                        self.assertEqual(packets[0], b"\x1F\x34")
                        self.assertEqual(packets[-1], b"\x1F\x34")
                        self.assertEqual([item for item in packets if item[:2] == b"\xAF\x3F"],
                                         [packet])
                        self.assertEqual(self.backend.breakpoints, {slot: (address, enabled)})
                        self.assert_preserved(before)

    def test_invalid_breakpoint_arguments_never_fault_or_touch_io(self):
        self.session()
        cases = [
            (-1, 0), (4, 0), (True, 0), (0.0, 0), ("0", 0), (None, 0),
            (0, -1), (0, 0x8000), (0, 0xFFFF), (0, 0x10000), (0, True),
            (0, 0.0), (0, "0"), (0, None), (0, 0, 0), (0, 0, 1),
            (0, 0, None), (0, 0, "true"),
        ]
        for arguments in cases:
            with self.subTest(arguments=arguments):
                self.assert_denied(("set_breakpoint", arguments), ValueError)
        self.assertEqual(self.debugger.read_pc(), self.backend.pc)

    def test_bad_pre_status_blocks_every_access_before_command(self):
        for operation in OPERATIONS:
            for status in BAD_ACTIVE_STATUS:
                self.session()
                self.backend.status = status
                with self.subTest(operation=operation, status=status), self.assertRaises(TransportError):
                    self.invoke(operation)
                self.assertEqual(self.backend.packets(), [b"\x1F\x34"])
                self.assert_faulted()

    def test_stack_and_breakpoint_flags_do_not_reject_an_active_halted_target(self):
        for operation in OPERATIONS:
            for status in (0x23, 0x2A, 0x2B):
                self.session()
                self.backend.status = status
                with self.subTest(operation=operation, status=status):
                    self.invoke(operation)
                    self.assertEqual(self.debugger.state, State.OPEN)
                    self.assertIsNone(self.backend.pending)

    def test_bad_post_status_faults_every_operation_without_recovery(self):
        for operation in OPERATIONS:
            for status in BAD_ACTIVE_STATUS:
                self.session()
                self.backend.statuses.extend((0x22, status))
                with self.subTest(operation=operation, status=status), self.assertRaises(TransportError):
                    self.invoke(operation)
                self.assertEqual(self.backend.packets()[-1], b"\x1F\x34")
                self.assert_faulted()

    def test_wrong_adapter_target_stops_before_bulk_io(self):
        for operation in OPERATIONS:
            self.session()
            self.backend.control_reply = b"\x31\x25" + b"\0" * 6
            with self.subTest(operation=operation), self.assertRaises(TransportError):
                self.invoke(operation)
            self.assertEqual([call[0] for call in self.backend.calls], ["control"])
            self.assert_faulted()

    def test_breakpoint_reply_is_status_not_accumulator_or_ignored(self):
        for status in BAD_ACTIVE_STATUS:
            self.session()
            self.backend.breakpoint_reply = status
            with self.subTest(status=status), self.assertRaises(TransportError):
                self.debugger.set_breakpoint(0, 0)
            self.assertEqual(self.backend.packets()[-1], b"\xAF\x3F\x08\x00\x00")
            self.assertEqual(self.backend.packets().count(b"\x1F\x34"), 1)
            self.assert_faulted()

    def test_transport_failure_at_every_exchange_boundary_stops_immediately(self):
        for operation in OPERATIONS:
            self.session()
            self.invoke(operation)
            count = len(self.backend.calls)
            for index in range(1, count + 1):
                self.session()
                self.backend.fail_at = index
                with self.subTest(operation=operation, index=index), self.assertRaises(TransportError):
                    self.invoke(operation)
                self.assertEqual(len(self.backend.calls), index)
                self.assert_faulted()

    def test_short_and_long_replies_at_every_bulk_read_stop_immediately(self):
        for operation in OPERATIONS:
            self.session()
            self.invoke(operation)
            reads = [(index, call[2]) for index, call in enumerate(self.backend.calls, 1)
                     if call[0] == "read"]
            for index, length in reads:
                for size in (length - 1, length + 1):
                    self.session()
                    self.backend.read_overrides[index] = b"\0" * size
                    with self.subTest(operation=operation, index=index, size=size):
                        with self.assertRaises(TransportError):
                            self.invoke(operation)
                        self.assertEqual(len(self.backend.calls), index)
                        self.assert_faulted()

    def test_partial_out_at_every_write_boundary_is_not_retried_or_read(self):
        for operation in OPERATIONS:
            self.session()
            self.invoke(operation)
            writes = [(index, len(call[2])) for index, call in enumerate(self.backend.calls, 1)
                      if call[0] == "write"]
            for index, length in writes:
                self.session()
                self.backend.write_counts[index] = length - 1
                with self.subTest(operation=operation, index=index), self.assertRaises(TransportError):
                    self.invoke(operation)
                self.assertEqual(len(self.backend.calls), index)
                self.assert_faulted()

    def test_malformed_reply_types_and_write_counts_are_not_success(self):
        for operation in (OPERATIONS[0], OPERATIONS[1], OPERATIONS[6]):
            self.session()
            self.invoke(operation)
            index = next(index for index, call in enumerate(self.backend.calls, 1)
                         if call[0] == "write" and call[2] != b"\x1F\x34")
            for reply in (None, 1, True, 1.0, "x"):
                self.session()
                self.backend.read_overrides[index + 1] = reply
                with self.subTest(operation=operation, reply=reply), self.assertRaises(TransportError):
                    self.invoke(operation)
                self.assertEqual(len(self.backend.calls), index + 1)
                self.assert_faulted()
            for written in (None, True, False, 2.0, -1, 99):
                self.session()
                self.backend.write_counts[index] = written
                with self.subTest(operation=operation, written=written), self.assertRaises(TransportError):
                    self.invoke(operation)
                self.assertEqual(len(self.backend.calls), index)
                self.assert_faulted()

    def test_late_completion_at_every_boundary_never_triggers_restoration(self):
        for operation in OPERATIONS:
            self.session()
            self.invoke(operation)
            count = len(self.backend.calls)
            for index in range(1, count + 1):
                self.session()
                self.backend.late_at = index
                with self.subTest(operation=operation, index=index):
                    with self.assertRaisesRegex(TransportError, "deadline"):
                        self.invoke(operation)
                    self.assertEqual(len(self.backend.calls), index)
                    self.assert_faulted()

    def test_one_deadline_covers_snapshots_commands_restoration_and_postcheck(self):
        for operation in OPERATIONS:
            self.session(timeout_ms=1000)
            self.backend.delays = {"control": 1_000_000, "write": 1_000_000, "read": 1_000_000}
            with self.subTest(operation=operation):
                self.invoke(operation)
                self.assertEqual([call[-1] for call in self.backend.calls],
                                 [1000 - index for index in range(len(self.backend.calls))])
                self.assertEqual(self.debugger.read_adapter_state().target_id, 0x2530)
                self.assertEqual(self.backend.calls[-1][-1], 1000)

    def test_submillisecond_remaining_budget_is_never_zero(self):
        self.session(memory=False, write=False, breakpoints=False)
        self.backend.delays["control"] = 9_500_000
        self.assertEqual(self.debugger.read_pc(), self.backend.pc)
        self.assertEqual([call[-1] for call in self.backend.calls], [10, 1, 1, 1, 1, 1, 1])

    def test_expired_deadline_before_first_transfer_faults_without_io(self):
        for operation in OPERATIONS:
            self.backend = CoreBackend()
            self.debugger = Debugger(
                self.backend, Access.EXISTING_DEBUG_SESSION, 10,
                Mock(side_effect=(0, 10_000_000)), allow_memory_access=True,
                allow_memory_write=True, allow_breakpoints=True,
            )
            self.debugger.open(UsbAddress(1, 2))
            self.backend.calls.clear()
            with self.subTest(operation=operation), self.assertRaisesRegex(TransportError, "deadline"):
                self.invoke(operation)
            self.assertEqual(self.backend.calls, [])
            self.assert_faulted()

    def test_backend_exceptions_after_mutation_do_not_attempt_restore(self):
        for error in (RuntimeError("synthetic backend bug"), KeyboardInterrupt()):
            self.session()
            self.debugger.read_registers()
            changed_read = next(
                index + 1 for index, call in enumerate(self.backend.calls, 1)
                if call[0] == "write" and call[2] == b"\x7F\x56\xE5\xD0"
            )
            self.session()
            original_a = self.backend.sfr[0xE0]
            self.backend.fail_at = changed_read
            self.backend.failure = error
            with self.subTest(error=type(error)), self.assertRaises(type(error)):
                self.debugger.read_registers()
            self.assertNotEqual(self.backend.sfr[0xE0], original_a)
            self.assertEqual(len(self.backend.calls), changed_read)
            self.assert_faulted()

    def test_partial_memory_write_is_not_rolled_back_after_transport_failure(self):
        self.session()
        self.debugger.write_xdata(0x123, b"\xD7\x86")
        writes = [index for index, call in enumerate(self.backend.calls, 1)
                  if call[0] == "write" and call[2] == b"\x4F\x55\xF0"]
        self.assertEqual(len(writes), 2)
        self.session()
        old_second = self.backend.ram[0x124]
        self.backend.fail_at = writes[1]
        with self.assertRaises(TransportError):
            self.debugger.write_xdata(0x123, b"\xD7\x86")
        self.assertEqual(self.backend.ram[0x123], 0xD7)
        self.assertEqual(self.backend.ram[0x124], old_second)
        self.assertEqual(len(self.backend.calls), writes[1])
        self.assert_faulted()

    def test_readback_mismatch_is_detected_without_more_target_commands(self):
        self.session()
        self.backend.after_memory_write = lambda address: self.backend.ram.__setitem__(address, 0x19)
        with self.assertRaises(TransportError):
            self.debugger.write_xdata(0x123, b"\xD7")
        self.assertEqual(self.backend.memory_events, [("write", 0x123, 0xD7), ("read", 0x123, 0x19)])
        self.assertEqual(self.backend.packets()[-1], b"\x4F\x55\xE0")
        self.assert_faulted()

    def test_full_snapshot_comparison_detects_corrupted_restoration(self):
        for operation in OPERATIONS[3:6]:
            for field in ("a", "psw", "b", "sp", "dptr0", "dptr1", "dps", "mpage",
                          "r", "pc", "bank"):
                self.session(register_bank=3, dps=1)
                original = self.backend.snapshot()
                corrupted = []

                def corrupt(instruction):
                    if (not corrupted and self.backend.memory_events
                            and instruction == bytes([0x75, 0xD0, original["psw"]])):
                        corrupted.append(field)
                        addresses = {"a": 0xE0, "psw": 0xD0, "b": 0xF0, "sp": 0x81,
                                     "dptr0": 0x82, "dptr1": 0x84, "dps": 0x92,
                                     "mpage": 0x93, "bank": 0x9F}
                        if field == "r":
                            self.backend.iram[24] ^= 0x80
                        elif field == "pc":
                            self.backend.pc ^= 0x100
                        else:
                            self.backend.sfr[addresses[field]] ^= 0x80 if field not in ("bank", "dps") else 1
                            self.backend.update_parity()

                self.backend.after_instruction = corrupt
                with self.subTest(operation=operation, field=field), self.assertRaises(TransportError):
                    self.invoke(operation)
                self.assertEqual(corrupted, [field])
                self.assert_faulted()

    def test_snapshot_and_sfr_inspection_verify_restored_a_and_psw(self):
        for operation, restore_number in ((OPERATIONS[1], 1), (OPERATIONS[2], 2)):
            for address in (0xE0, 0xD0):
                self.session()
                original_psw = self.backend.sfr[0xD0]
                restores = []

                def corrupt(instruction):
                    if instruction == bytes([0x75, 0xD0, original_psw]):
                        restores.append(instruction)
                        if len(restores) == restore_number:
                            self.backend.sfr[address] ^= 0x80
                            self.backend.update_parity()

                self.backend.after_instruction = corrupt
                with self.subTest(operation=operation, address=address), self.assertRaises(TransportError):
                    self.invoke(operation)
                self.assertGreaterEqual(len(restores), restore_number)
                self.assert_faulted()

    def test_one_lock_rejects_reentry_at_every_transfer_and_releases_on_success(self):
        for operation in OPERATIONS:
            self.session()
            record = self.backend.record
            probes = []
            in_probe = False

            def reenter(name, *arguments):
                nonlocal in_probe
                if in_probe:
                    raise AssertionError("Reentrant access reached backend I/O")
                in_probe = True
                try:
                    before = list(self.backend.calls)
                    state = self.debugger.state
                    for nested in OPERATIONS:
                        with self.assertRaisesRegex(DebuggerError, "busy"):
                            self.invoke(nested)
                    for nested in (self.debugger.close, self.debugger.read_adapter_state):
                        with self.assertRaisesRegex(DebuggerError, "busy"):
                            nested()
                    self.assertEqual(self.backend.calls, before)
                    self.assertEqual(self.debugger.state, state)
                    probes.append(name)
                finally:
                    in_probe = False
                record(name, *arguments)

            with self.subTest(operation=operation), patch.object(self.backend, "record", side_effect=reenter):
                self.invoke(operation)
            self.assertEqual(len(probes), len(self.backend.calls))
            self.assertEqual(self.debugger.read_adapter_state().target_id, 0x2530)
            self.debugger.close()
            self.assertEqual(self.debugger.state, State.CLOSED)


class AccessCliTests(unittest.TestCase):
    run_cli = transport_tests.CliTests.run_cli

    cases = {
        "pc": ((), ()),
        "registers": (("--allow-memory-access",), ()),
        "read-sfr": (("--allow-memory-access",), ("--memory-address", "0x81")),
        "read-xdata": (("--allow-memory-access",),
                       ("--memory-address", "0x123", "--length", "4")),
        "read-code": (("--allow-memory-access",),
                      ("--memory-address", "0x1234", "--length", "4")),
        "write-xdata": (("--allow-memory-access", "--allow-memory-write"),
                        ("--memory-address", "0x123", "--data-hex", "00 80 aB fF")),
        "breakpoint": (("--allow-breakpoints",), ("--slot", "2", "--code-address", "0x4567")),
    }

    def arguments(self, command, operands=None):
        permissions, default_operands = self.cases[command]
        return [command, "--bus", "1", "--address", "2",
                "--confirm-existing-debug-session", *permissions,
                *(default_operands if operands is None else operands)]

    def assert_rejected(self, arguments):
        backend = CoreBackend()
        code, out, err, load = self.run_cli(arguments, backend)
        self.assertEqual((code, out), (1, ""))
        self.assertTrue(err.startswith("cc-debugger: "), err)
        load.assert_not_called()
        self.assertEqual(backend.calls, [])

    def assert_parse_rejected(self, arguments):
        backend = CoreBackend()
        stdout, stderr = StringIO(), StringIO()
        with patch.object(driver.PyUsbBackend, "load", return_value=backend) as load:
            with redirect_stdout(stdout), redirect_stderr(stderr):
                with self.assertRaises(SystemExit) as error:
                    driver.main(arguments)
        self.assertEqual(error.exception.code, 2)
        self.assertEqual(stdout.getvalue(), "")
        self.assertIn("error:", stderr.getvalue())
        load.assert_not_called()
        self.assertEqual(backend.calls, [])

    def test_exact_success_json_for_every_new_command(self):
        for command in self.cases:
            backend = CoreBackend(register_bank=3, dps=1, accumulator=0xE7)
            backend.pc = 0xAB91
            backend.sfr[0x9F] = 0xAB
            backend.status = 0x2A
            backend.ram[0x123:0x127] = b"\0\x80\xAB\xFF"
            before = backend.cpu_image()
            snapshot = backend.snapshot()
            snapshot["r"] = list(snapshot["r"])
            expected = {
                "pc": {"pc": 0xAB91},
                "registers": snapshot,
                "read-sfr": {"address": 0x81, "value": 0xB7},
                "read-xdata": {"address": 0x123, "data": "0080abff"},
                "read-code": {"address": 0x1234, "data": backend.code[0x1234:0x1238].hex()},
                "write-xdata": {"address": 0x123, "bytes_written": 4, "readback_verified": True},
                "breakpoint": {"slot": 2, "address": 0x4567, "enabled": True, "status_after": 0x2A},
            }[command]
            with self.subTest(command=command):
                code, out, err, load = self.run_cli(self.arguments(command), backend)
                self.assertEqual((code, err), (0, ""))
                self.assertEqual(json.loads(out), expected)
                self.assertEqual(out, json.dumps(expected, sort_keys=True) + "\n")
                load.assert_called_once_with()
                self.assertEqual(backend.calls[0], ("open", UsbAddress(1, 2), 1000))
                self.assertEqual(backend.calls[-1], ("close",))
                self.assertEqual(backend.cpu_image(), before)
                self.assertIsNone(backend.pending)

    def test_breakpoint_disable_json_is_boolean_for_all_slots(self):
        for slot in range(4):
            backend = CoreBackend()
            arguments = self.arguments("breakpoint", (
                "--slot", str(slot), "--code-address", "0x7fff", "--disable",
            ))
            with self.subTest(slot=slot):
                code, out, err, _ = self.run_cli(arguments, backend)
                self.assertEqual((code, err), (0, ""))
                self.assertEqual(out, json.dumps({
                    "slot": slot, "address": 0x7FFF, "enabled": False, "status_after": 0x22,
                }, sort_keys=True) + "\n")
                self.assertEqual(backend.breakpoints, {slot: (0x7FFF, False)})

    def test_maximum_reads_emit_exact_lowercase_hex_without_truncation(self):
        for command, address in (("read-xdata", 0x1F00), ("read-code", 0x7F00)):
            backend = CoreBackend(register_bank=2, dps=1)
            expected = (backend.xdata(address, 256) if command == "read-xdata"
                        else backend.code[address:address + 256])
            arguments = self.arguments(command, (
                "--memory-address", str(address), "--length", "256",
            ))
            with self.subTest(command=command):
                code, out, err, _ = self.run_cli(arguments, backend)
                self.assertEqual((code, err), (0, ""))
                self.assertEqual(out, json.dumps({"address": address, "data": expected.hex()},
                                                sort_keys=True) + "\n")
                self.assertEqual(len(json.loads(out)["data"]), 512)

    def test_write_hex_is_passed_to_public_api_as_exact_immutable_bytes(self):
        for text, expected in (("00 80 aB fF", b"\0\x80\xAB\xFF"),
                               (" \t80\n", b"\x80"),
                               (bytes(range(256)).hex(), bytes(range(256)))):
            backend = CoreBackend()
            address = 0x1E00 - len(expected)
            before = bytes(backend.ram)
            arguments = self.arguments("write-xdata", (
                "--memory-address", hex(address), "--data-hex", text,
            ))
            with self.subTest(length=len(expected)):
                write = Debugger.write_xdata
                with patch.object(Debugger, "write_xdata", autospec=True, side_effect=write) as invoked:
                    code, out, err, _ = self.run_cli(arguments, backend)
                self.assertEqual((code, err), (0, ""))
                invoked.assert_called_once()
                _, actual_address, data = invoked.call_args.args
                self.assertEqual(actual_address, address)
                self.assertIs(type(data), bytes)
                self.assertEqual(data, expected)
                self.assertEqual(bytes(backend.ram),
                                 before[:address] + expected + before[address + len(expected):])
                self.assertEqual(out, json.dumps({
                    "address": address, "bytes_written": len(expected), "readback_verified": True,
                }, sort_keys=True) + "\n")

    def test_target_consent_is_required_before_loading_backend(self):
        for command in self.cases:
            arguments = self.arguments(command)
            arguments.remove("--confirm-existing-debug-session")
            with self.subTest(command=command):
                self.assert_rejected(arguments)
                self.assert_rejected(arguments + ["--confirm-reset-attach", "--allow-target-reset"])

    def test_every_required_permission_is_checked_before_loading_backend(self):
        for command, (permissions, _) in self.cases.items():
            for permission in permissions:
                arguments = self.arguments(command)
                arguments.remove(permission)
                with self.subTest(command=command, permission=permission):
                    self.assert_rejected(arguments)

    def test_cpu_reset_and_other_access_grants_cannot_replace_required_permission(self):
        for command, (permissions, _) in self.cases.items():
            for permission in permissions:
                arguments = self.arguments(command)
                arguments.remove(permission)
                arguments += ["--allow-cpu-control", "--allow-target-reset"]
                if permission != "--allow-breakpoints":
                    arguments += ["--allow-breakpoints"]
                else:
                    arguments += ["--allow-memory-access", "--allow-memory-write"]
                with self.subTest(command=command, permission=permission):
                    self.assert_rejected(arguments)

    def test_write_grant_without_memory_access_is_rejected_even_for_other_commands(self):
        for command in self.cases:
            arguments = [argument for argument in self.arguments(command)
                         if argument not in ("--allow-memory-access", "--allow-memory-write")]
            with self.subTest(command=command):
                self.assert_rejected(arguments + ["--allow-memory-write"])

    def test_missing_required_operands_are_rejected_before_loading_backend(self):
        cases = (
            ("read-sfr", ()),
            ("read-xdata", ()), ("read-xdata", ("--memory-address", "0")),
            ("read-xdata", ("--length", "1")),
            ("read-code", ()), ("read-code", ("--memory-address", "0")),
            ("read-code", ("--length", "1")),
            ("write-xdata", ()), ("write-xdata", ("--memory-address", "0")),
            ("write-xdata", ("--data-hex", "80")),
            ("breakpoint", ()), ("breakpoint", ("--slot", "0")),
            ("breakpoint", ("--code-address", "0")),
        )
        for command, operands in cases:
            with self.subTest(command=command, operands=operands):
                self.assert_rejected(self.arguments(command, operands))

    def test_invalid_and_peripheral_sfr_operands_never_load_backend(self):
        for address in ("-1", "0", "0x7f", "0x80", "0x90", "0x100", "0x70e0"):
            with self.subTest(address=address):
                self.assert_rejected(self.arguments("read-sfr", ("--memory-address", address)))

    def test_invalid_read_ranges_never_load_backend(self):
        for command, limit in (("read-xdata", 0x2000), ("read-code", 0x8000)):
            for address, length in ((-1, 1), (limit, 1), (limit - 1, 2),
                                    (limit - 255, 256), (0, 0), (0, -1), (0, 257)):
                with self.subTest(command=command, address=address, length=length):
                    self.assert_rejected(self.arguments(command, (
                        "--memory-address", str(address), "--length", str(length),
                    )))

    def test_invalid_write_ranges_and_hex_data_never_load_backend(self):
        for address, text in (
            ("0", ""), ("0", " \t"), ("0", "0"), ("0", "gg"), ("0", "0x80"),
            ("0", "aa" * 257), ("-1", "80"), ("0x1dff", "aabb"),
            ("0x1e00", "80"), ("0x1f00", "80"), ("0x2000", "80"), ("0x70e0", "80"),
        ):
            with self.subTest(address=address, text=text):
                self.assert_rejected(self.arguments("write-xdata", (
                    "--memory-address", address, "--data-hex", text,
                )))

    def test_invalid_breakpoint_operands_never_load_backend(self):
        for slot, address in ((-1, 0), (4, 0), (0, -1), (0, 0x8000), (0, 0xFFFF)):
            with self.subTest(slot=slot, address=address):
                self.assert_rejected(self.arguments("breakpoint", (
                    "--slot", str(slot), "--code-address", str(address),
                )))

    def test_unrelated_operands_are_rejected_before_loading_backend(self):
        operands = (
            (("--memory-address", "0"), ("read-sfr", "read-xdata", "read-code", "write-xdata")),
            (("--length", "1"), ("read-xdata", "read-code")),
            (("--data-hex", "80"), ("write-xdata",)),
            (("--slot", "0"), ("breakpoint",)),
            (("--code-address", "0"), ("breakpoint",)),
            (("--disable",), ("breakpoint",)),
        )
        for command in self.cases:
            for extra, accepted in operands:
                if command not in accepted:
                    with self.subTest(command=command, operand=extra):
                        self.assert_rejected(self.arguments(command) + list(extra))

    def test_parser_errors_never_load_backend(self):
        cases = (
            ("read-sfr", ("--memory-address",)),
            ("read-sfr", ("--memory-address", "not-an-address")),
            ("read-xdata", ("--memory-address", "0", "--length")),
            ("read-xdata", ("--memory-address", "0", "--length", "1.5")),
            ("read-code", ("--memory-address", "0", "--length", "True")),
            ("write-xdata", ("--memory-address", "0", "--data-hex")),
            ("breakpoint", ("--slot",)),
            ("breakpoint", ("--slot", "one", "--code-address", "0")),
            ("breakpoint", ("--slot", "0", "--code-address")),
            ("breakpoint", ("--slot", "0", "--code-address", "invalid")),
            ("pc", ("--unexpected-operand", "1")),
        )
        for command, operands in cases:
            with self.subTest(command=command, operands=operands):
                self.assert_parse_rejected(self.arguments(command, operands))
        for command in self.cases:
            for selector in ("--bus", "--address"):
                arguments = self.arguments(command)
                index = arguments.index(selector)
                del arguments[index:index + 2]
                with self.subTest(command=command, selector=selector):
                    self.assert_parse_rejected(arguments)
            with self.subTest(command=command, conflicting_consent=True):
                self.assert_parse_rejected(self.arguments(command) + ["--confirm-reset-attach"])

    def test_cleanup_failure_never_emits_success_json_for_any_new_command(self):
        for command in self.cases:
            backend = CoreBackend()
            backend.failures.add("close")
            with self.subTest(command=command):
                code, out, err, load = self.run_cli(self.arguments(command), backend)
                self.assertEqual((code, out), (1, ""))
                self.assertIn("close failed", err)
                load.assert_called_once_with()
                self.assertEqual(backend.calls[-1], ("close",))
                self.assertTrue(backend.packets())
                self.assertIsNone(backend.pending)

    def test_primary_and_cleanup_errors_are_both_reported_without_success_json(self):
        for command in self.cases:
            backend = CoreBackend()
            backend.failures.update(("control", "close"))
            with self.subTest(command=command):
                code, out, err, _ = self.run_cli(self.arguments(command), backend)
                self.assertEqual((code, out), (1, ""))
                self.assertIn("control failed", err)
                self.assertIn("cleanup also failed: close failed", err)
                self.assertEqual([call[0] for call in backend.calls], ["open", "control", "close"])


if __name__ == "__main__":
    unittest.main()
