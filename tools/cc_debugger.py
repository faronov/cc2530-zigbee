#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Opt-in CC Debugger control and reset-attach; no automatic reset or flashing."""

import argparse
from contextlib import contextmanager
from dataclasses import asdict, dataclass
from enum import Enum
import importlib
import json
import sys
import threading
import time
from typing import Callable, Optional, Protocol

from cc2530_debug import GET_BM, HALT, READ_CONFIG, READ_STATUS, RESUME, STEP_INSTR, Status


VID = 0x0451
PID = 0x16A2
ENDPOINT_OUT = 0x04
ENDPOINT_IN = 0x84


class DebuggerError(RuntimeError):
    """Explicit policy, lifecycle or USB failure; never a target success."""


class TransportError(DebuggerError):
    """The exchange failed and the session cannot safely be reused."""


def bounded_integer(value: int, maximum: int, name: str) -> None:
    if type(value) is not int or not 1 <= value <= maximum:
        raise ValueError(f"{name} must be an integer in 1..{maximum}")


@dataclass(frozen=True)
class UsbAddress:
    bus: int
    address: int

    def __post_init__(self):
        bounded_integer(self.bus, 255, "USB bus")
        bounded_integer(self.address, 127, "USB address")


@dataclass(frozen=True)
class AdapterState:
    target_id: int
    firmware_version: int
    firmware_revision: int


@dataclass(frozen=True)
class CpuControlResult:
    status_before: int
    status_after: int
    command_sent: bool


@dataclass(frozen=True)
class StepResult:
    status_before: int
    status_after: int
    accumulator: int


@dataclass(frozen=True)
class AttachResult:
    status_after: int
    reset_sent: bool


class Access(Enum):
    ADAPTER_ONLY = "adapter-only"
    EXISTING_DEBUG_SESSION = "existing-debug-session"
    RESET_DEBUG_SESSION = "reset-debug-session"


class State(Enum):
    NEW = "new"
    OPEN = "open"
    FAULTED = "faulted"
    CLOSED = "closed"


class UsbBackend(Protocol):
    def open(self, address: UsbAddress, timeout_ms: int) -> None: ...
    def device_revision(self) -> int: ...
    def control_read(self, request_type: int, request: int, value: int,
                     index: int, length: int, timeout_ms: int) -> bytes: ...
    def control_write(self, request_type: int, request: int, value: int,
                      index: int, data: bytes, timeout_ms: int) -> int: ...
    def bulk_write(self, endpoint: int, data: bytes, timeout_ms: int) -> int: ...
    def bulk_read(self, endpoint: int, length: int, timeout_ms: int) -> bytes: ...
    def close(self) -> None: ...


class _Deadline:
    def __init__(self, timeout_ms: int, clock: Callable[[], int]):
        self.clock = clock
        self.end = clock() + timeout_ms * 1_000_000

    def remaining_ms(self) -> int:
        remaining = self.end - self.clock()
        if remaining <= 0:
            raise TransportError("USB operation deadline exceeded")
        # libusb interprets zero as an infinite timeout.
        return (remaining + 999_999) // 1_000_000


class Debugger:
    def __init__(self, backend: UsbBackend, access: Access = Access.ADAPTER_ONLY,
                 timeout_ms: int = 1000, clock: Callable[[], int] = time.monotonic_ns,
                 *, allow_cpu_control: bool = False, allow_target_reset: bool = False):
        bounded_integer(timeout_ms, 60_000, "timeout_ms")
        if not isinstance(access, Access):
            raise ValueError("access must be an Access policy")
        if type(allow_cpu_control) is not bool:
            raise ValueError("allow_cpu_control must be a boolean")
        if allow_cpu_control and access == Access.ADAPTER_ONLY:
            raise ValueError("CPU control requires an explicitly confirmed existing debug session")
        if type(allow_target_reset) is not bool:
            raise ValueError("allow_target_reset must be a boolean")
        if allow_target_reset and access == Access.ADAPTER_ONLY:
            raise ValueError("Target reset requires an existing debug session or explicit reset-attach policy")
        if access == Access.RESET_DEBUG_SESSION and not allow_target_reset:
            raise ValueError("Reset-attach policy requires separate target-reset permission")
        self._backend = backend
        self._access = access
        self._timeout_ms = timeout_ms
        self._clock = clock
        self._allow_cpu_control = allow_cpu_control
        self._allow_target_reset = allow_target_reset
        self._debug_session_ready = access == Access.EXISTING_DEBUG_SESSION
        self._state = State.NEW
        self._lock = threading.Lock()

    @property
    def state(self) -> State:
        return self._state

    def __enter__(self):
        self._require(State.NEW)
        return self

    def __exit__(self, exception_type, exception, traceback):
        try:
            self.close()
        except DebuggerError as cleanup_error:
            if exception is not None:
                raise DebuggerError(f"{exception}; cleanup also failed: {cleanup_error}") from exception
            raise
        return False

    def _require(self, expected: State) -> None:
        if self._state != expected:
            raise DebuggerError(f"Session is {self._state.value}, expected {expected.value}; "
                                "no automatic reconnect or target reset")

    @contextmanager
    def _exclusive(self):
        if not self._lock.acquire(blocking=False):
            raise DebuggerError("Session is busy; concurrent or reentrant access is not permitted")
        try:
            yield
        finally:
            self._lock.release()

    def open(self, address: UsbAddress) -> None:
        with self._exclusive():
            self._require(State.NEW)
            if not isinstance(address, UsbAddress):
                raise ValueError("An explicit UsbAddress is required")
            self._state = State.FAULTED
            self._backend.open(address, self._timeout_ms)
            self._state = State.OPEN

    def close(self) -> None:
        with self._exclusive():
            if self._state != State.CLOSED:
                self._state = State.CLOSED
                self._backend.close()

    @contextmanager
    def _operation(self, precheck: Optional[Callable[[], None]] = None):
        with self._exclusive():
            self._require(State.OPEN)
            if precheck is not None:
                precheck()
            self._state = State.FAULTED
            deadline = _Deadline(self._timeout_ms, self._clock)
            yield deadline
            deadline.remaining_ms()
            self._state = State.OPEN

    @staticmethod
    def _exact(data: bytes, length: int) -> bytes:
        if not isinstance(data, bytes) or len(data) != length:
            raise TransportError(f"USB reply must contain exactly {length} bytes")
        return data

    def _adapter_state(self, deadline: _Deadline) -> AdapterState:
        data = self._exact(self._backend.control_read(
            0xC0, 0xC0, 0, 0, 8, deadline.remaining_ms()), 8)
        deadline.remaining_ms()
        return AdapterState(*(int.from_bytes(data[offset:offset + 2], "little")
                              for offset in (0, 2, 4)))

    def read_adapter_state(self) -> AdapterState:
        with self._operation() as deadline:
            result = self._adapter_state(deadline)
        return result

    @contextmanager
    def _target_operation(self, cpu_control=False, target_reset=False):
        def check_access():
            if not self._debug_session_ready:
                raise DebuggerError("Target access requires an explicitly confirmed existing debug session "
                                    "or a completed reset-attach")
            if cpu_control and not self._allow_cpu_control:
                raise DebuggerError("CPU control requires separate explicit permission")
            if target_reset and not self._allow_target_reset:
                raise DebuggerError("Target reset requires separate explicit permission")

        with self._operation(check_access) as deadline:
            if self._adapter_state(deadline).target_id != 0x2530:
                raise TransportError("Adapter does not report a CC2530 target; no target command sent")
            yield deadline

    def _exchange_byte(self, command: int, deadline: _Deadline) -> int:
        packet = bytes([0x1F, command])
        written = self._backend.bulk_write(ENDPOINT_OUT, packet, deadline.remaining_ms())
        if type(written) is not int or written != len(packet):
            raise TransportError("Incomplete USB command write; exchange must not be retried")
        result = self._exact(self._backend.bulk_read(
            ENDPOINT_IN, 1, deadline.remaining_ms()), 1)[0]
        deadline.remaining_ms()
        return result

    @staticmethod
    def _check_status(status: int, *, active=False, halted=False) -> None:
        if status & (Status.DEBUG_LOCKED | Status.CHIP_ERASE_BUSY):
            raise TransportError(f"Target is locked or erasing (status=0x{status:02x})")
        if active and (not status & Status.OSCILLATOR_STABLE
                       or status & (Status.PM_ACTIVE | Status.PCON_IDLE)):
            raise TransportError(f"Target needs a stable oscillator and normal power state (status=0x{status:02x})")
        if halted and not status & Status.CPU_HALTED:
            raise TransportError(f"Target must be halted (status=0x{status:02x})")

    def read_debug_status(self) -> int:
        with self._target_operation() as deadline:
            result = self._exchange_byte(READ_STATUS, deadline)
        return result

    def read_debug_config(self) -> int:
        with self._target_operation() as deadline:
            self._check_status(self._exchange_byte(READ_STATUS, deadline))
            result = self._exchange_byte(READ_CONFIG, deadline)
            self._check_status(self._exchange_byte(READ_STATUS, deadline))
        return result

    def read_bank(self) -> int:
        with self._target_operation() as deadline:
            self._check_status(self._exchange_byte(READ_STATUS, deadline), active=True, halted=True)
            result = self._exchange_byte(GET_BM, deadline) & 0x07
            self._check_status(self._exchange_byte(READ_STATUS, deadline), active=True, halted=True)
        return result

    def halt(self) -> CpuControlResult:
        with self._target_operation(cpu_control=True) as deadline:
            before = self._exchange_byte(READ_STATUS, deadline)
            self._check_status(before, active=True)
            sent = not bool(before & Status.CPU_HALTED)
            after = before
            if sent:
                # A breakpoint can win the race: HALT's reply can be undefined.
                self._exchange_byte(HALT, deadline)
                after = self._exchange_byte(READ_STATUS, deadline)
                self._check_status(after, active=True, halted=True)
        return CpuControlResult(before, after, sent)

    def resume(self) -> CpuControlResult:
        with self._target_operation(cpu_control=True) as deadline:
            before = self._exchange_byte(READ_STATUS, deadline)
            self._check_status(before, active=True, halted=True)
            self._check_status(self._exchange_byte(RESUME, deadline), active=True)
            after = self._exchange_byte(READ_STATUS, deadline)
            self._check_status(after, active=True)
            if after & Status.CPU_HALTED and not after & Status.HALT_STATUS:
                raise TransportError("CPU remained halted without a hardware-breakpoint cause after resume")
        return CpuControlResult(before, after, True)

    def step(self) -> StepResult:
        with self._target_operation(cpu_control=True) as deadline:
            before = self._exchange_byte(READ_STATUS, deadline)
            self._check_status(before, active=True, halted=True)
            accumulator = self._exchange_byte(STEP_INSTR, deadline)
            after = self._exchange_byte(READ_STATUS, deadline)
            self._check_status(after, active=True, halted=True)
        return StepResult(before, after, accumulator)

    def _control_write(self, request: int, value: int, index: int, data: bytes,
                       deadline: _Deadline) -> None:
        written = self._backend.control_write(0x40, request, value, index, data, deadline.remaining_ms())
        if type(written) is not int or written != len(data):
            raise TransportError(f"Invalid USB control completion for 0x{request:02x}; command must not be retried")
        deadline.remaining_ms()

    def _reset_into_halt(self, deadline: _Deadline) -> int:
        self._control_write(0xC9, 0, 1, b"", deadline)
        if self._adapter_state(deadline).target_id != 0x2530:
            raise TransportError("Adapter target changed after reset; no further target command sent")
        after = self._exchange_byte(READ_STATUS, deadline)
        self._check_status(after, active=True, halted=True)
        return after

    def reset_halt(self) -> CpuControlResult:
        with self._target_operation(target_reset=True) as deadline:
            before = self._exchange_byte(READ_STATUS, deadline)
            self._check_status(before, active=True)
            after = self._reset_into_halt(deadline)
        return CpuControlResult(before, after, True)

    def attach_reset(self) -> AttachResult:
        def check_access():
            if self._access != Access.RESET_DEBUG_SESSION or self._debug_session_ready:
                raise DebuggerError("Initial attach requires a fresh explicit reset-attach policy")
            if not self._allow_target_reset:
                raise DebuggerError("Reset-attach requires separate target-reset permission")

        with self._operation(check_access) as deadline:
            if self._adapter_state(deadline).target_id != 0x2530:
                raise TransportError("Adapter does not report a CC2530 target; no attach command sent")
            revision = self._backend.device_revision()
            if type(revision) is not int or not 0 <= revision <= 0xFFFF:
                raise TransportError("Invalid USB bcdDevice revision; no attach command sent")
            deadline.remaining_ms()
            # Adapter chip-information fields, not a target identity or an 8051 program.
            chip_info = bytearray(b" " * 48)
            chip_info[:6] = b"CC2530"
            chip_info[16:20] = b"DID:"
            chip_info[21:25] = f"{revision:04X}".encode("ascii")
            self._control_write(0xC5, 0, 0, b"", deadline)
            self._control_write(0xC8, 1, 0, bytes(chip_info), deadline)
            after = self._reset_into_halt(deadline)
            self._debug_session_ready = True
        return AttachResult(after, True)


class PyUsbBackend:
    """PyUSB facade with explicit claiming and no automatic configuration/reset."""

    def __init__(self, core, util):
        self._core = core
        self._util = util
        self._device = None
        self._claimed = False

    @classmethod
    def load(cls):
        try:
            core = importlib.import_module("usb.core")
            util = importlib.import_module("usb.util")
        except ImportError as error:
            raise DebuggerError("Optional PyUSB backend unavailable; install requirements-debug.txt "
                                "in a virtual environment and the system libusb-1.0 runtime") from error
        return cls(core, util)

    def open(self, address: UsbAddress, timeout_ms: int) -> None:
        if self._device is not None:
            raise DebuggerError("USB backend is already open")
        try:
            devices = list(self._core.find(find_all=True, idVendor=VID, idProduct=PID,
                                           bus=address.bus, address=address.address))
            if len(devices) != 1:
                raise DebuggerError("Expected exactly one CC Debugger at the selected USB bus/address")
            self._device = devices[0]
            self._device.default_timeout = timeout_ms
            config = self._device.get_active_configuration()
            if config.bConfigurationValue != 1:
                raise DebuggerError("USB configuration 1 must already be active; refusing to change it")
            interfaces = list(config)
            if (len(interfaces) != 1 or interfaces[0].bInterfaceNumber != 0
                    or interfaces[0].bAlternateSetting != 0):
                raise DebuggerError("Expected only debug interface 0, alternate setting 0")
            endpoints = {endpoint.bEndpointAddress: endpoint for endpoint in interfaces[0]}
            if len(endpoints) != interfaces[0].bNumEndpoints:
                raise DebuggerError("Duplicate or missing USB endpoint descriptors")
            for address_value in (ENDPOINT_OUT, ENDPOINT_IN):
                if address_value not in endpoints or endpoints[address_value].bmAttributes & 3 != 2:
                    raise DebuggerError("Expected bulk endpoints 0x04 and 0x84 on interface 0")
            if self._device.is_kernel_driver_active(0):
                raise DebuggerError("Interface 0 has a kernel driver; refusing automatic detachment")
            self._util.claim_interface(self._device, 0)
            self._claimed = True
        except (OSError, ValueError) as error:
            raise TransportError(f"Cannot open selected USB adapter: {error}") from error

    def _opened_device(self):
        if self._device is None or not self._claimed:
            raise DebuggerError("USB backend has no claimed debug interface")
        return self._device

    def device_revision(self) -> int:
        return self._opened_device().bcdDevice

    def control_read(self, request_type: int, request: int, value: int,
                     index: int, length: int, timeout_ms: int) -> bytes:
        device = self._opened_device()
        try:
            return memoryview(device.ctrl_transfer(request_type, request, value, index,
                                                   length, timeout=timeout_ms)).tobytes()
        except (OSError, ValueError, TypeError) as error:
            raise TransportError(f"USB control read failed: {error}") from error

    def control_write(self, request_type: int, request: int, value: int,
                      index: int, data: bytes, timeout_ms: int) -> int:
        device = self._opened_device()
        try:
            return device.ctrl_transfer(request_type, request, value, index, data, timeout=timeout_ms)
        except (OSError, ValueError, TypeError) as error:
            raise TransportError(f"USB control write failed: {error}") from error

    def bulk_write(self, endpoint: int, data: bytes, timeout_ms: int) -> int:
        device = self._opened_device()
        try:
            return device.write(endpoint, data, timeout=timeout_ms)
        except (OSError, ValueError) as error:
            raise TransportError(f"USB bulk write failed: {error}") from error

    def bulk_read(self, endpoint: int, length: int, timeout_ms: int) -> bytes:
        device = self._opened_device()
        try:
            return memoryview(device.read(endpoint, length, timeout=timeout_ms)).tobytes()
        except (OSError, ValueError, TypeError) as error:
            raise TransportError(f"USB bulk read failed: {error}") from error

    def close(self) -> None:
        if self._device is None:
            return
        errors = []
        try:
            try:
                # Also covers interruption after PyUSB claims but before we record it.
                # dispose_resources alone suppresses release errors in PyUSB.
                self._util.release_interface(self._device, 0)
            except (OSError, ValueError) as error:
                errors.append(f"release interface: {error}")
            try:
                self._util.dispose_resources(self._device)
            except (OSError, ValueError) as error:
                errors.append(f"dispose USB resources: {error}")
        finally:
            self._device = None
            self._claimed = False
        if errors:
            raise TransportError("; ".join(errors))


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("adapter-state", "debug-status", "debug-config",
                                           "bank", "halt", "resume", "step", "reset-halt", "attach-reset"))
    parser.add_argument("--bus", type=int, required=True)
    parser.add_argument("--address", type=int, required=True)
    parser.add_argument("--timeout-ms", type=int, default=1000)
    session = parser.add_mutually_exclusive_group()
    session.add_argument("--confirm-existing-debug-session", action="store_true",
                         help="confirm a debug session already exists; does not establish or reset one")
    session.add_argument("--confirm-reset-attach", action="store_true",
                         help="confirm the selected CC2530 target/verified image for destructive initial attach")
    parser.add_argument("--allow-cpu-control", action="store_true",
                        help="separately authorize halt/resume/step of an already verified target image")
    parser.add_argument("--allow-target-reset", action="store_true",
                        help="separately authorize destructive reset into halt or reset-based initial attach")
    args = parser.parse_args(argv)
    try:
        address = UsbAddress(args.bus, args.address)
        bounded_integer(args.timeout_ms, 60_000, "timeout_ms")
        if args.command == "attach-reset":
            if not args.confirm_reset_attach or not args.allow_target_reset:
                raise DebuggerError("Initial attach requires --confirm-reset-attach and --allow-target-reset")
        elif args.confirm_reset_attach:
            raise DebuggerError("--confirm-reset-attach is only valid for attach-reset")
        elif args.command != "adapter-state" and not args.confirm_existing_debug_session:
            raise DebuggerError("Target access requires --confirm-existing-debug-session")
        if args.allow_cpu_control and not (args.confirm_existing_debug_session or args.confirm_reset_attach):
            raise DebuggerError("--allow-cpu-control also requires an explicit debug-session policy")
        if args.command in ("halt", "resume", "step") and not args.allow_cpu_control:
            raise DebuggerError("CPU control requires --allow-cpu-control")
        if args.allow_target_reset and not (args.confirm_existing_debug_session or args.confirm_reset_attach):
            raise DebuggerError("--allow-target-reset also requires an explicit debug-session policy")
        if args.command == "reset-halt" and not args.allow_target_reset:
            raise DebuggerError("Target reset requires --allow-target-reset")
        access = (Access.RESET_DEBUG_SESSION if args.confirm_reset_attach else
                  Access.EXISTING_DEBUG_SESSION if args.confirm_existing_debug_session else Access.ADAPTER_ONLY)
        with Debugger(PyUsbBackend.load(), access, args.timeout_ms,
                      allow_cpu_control=args.allow_cpu_control,
                      allow_target_reset=args.allow_target_reset) as debugger:
            debugger.open(address)
            if args.command == "adapter-state":
                result = asdict(debugger.read_adapter_state())
            elif args.command == "debug-status":
                result = {"debug_status": debugger.read_debug_status()}
            elif args.command == "debug-config":
                result = {"debug_config": debugger.read_debug_config()}
            elif args.command == "bank":
                result = {"bank": debugger.read_bank()}
            elif args.command == "halt":
                result = asdict(debugger.halt())
            elif args.command == "resume":
                result = asdict(debugger.resume())
            elif args.command == "reset-halt":
                result = asdict(debugger.reset_halt())
            elif args.command == "attach-reset":
                result = asdict(debugger.attach_reset())
            else:
                result = asdict(debugger.step())
    except (DebuggerError, ValueError) as error:
        print(f"cc-debugger: {error}", file=sys.stderr)
        return 1
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
