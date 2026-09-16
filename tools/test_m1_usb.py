# SPDX-License-Identifier: BSD-3-Clause
"""PyUSB API facade tests; objects here never open a USB device."""

from array import array
import importlib
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

from cc_debugger import DebuggerError, PyUsbBackend, TransportError, UsbAddress


class Interface(list):
    bInterfaceNumber = 0
    bAlternateSetting = 0

    @property
    def bNumEndpoints(self):
        return len(self)


class Configuration(list):
    bConfigurationValue = 1


class FakeDevice:
    def __init__(self):
        self.config = Configuration([Interface([
            SimpleNamespace(bEndpointAddress=0x04, bmAttributes=2),
            SimpleNamespace(bEndpointAddress=0x84, bmAttributes=2),
        ])])
        self.get_active_configuration = Mock(return_value=self.config)
        self.is_kernel_driver_active = Mock(return_value=False)
        self.ctrl_transfer = Mock(return_value=array("B", [0] * 8))
        self.write = Mock(return_value=2)
        self.read = Mock(return_value=array("B", [0x20]))
        self.default_timeout = None
        self.bcdDevice = 0x1234


class UsbBackendTests(unittest.TestCase):
    def setUp(self):
        self.device = FakeDevice()
        self.core = SimpleNamespace(find=Mock(return_value=[self.device]))
        self.util = SimpleNamespace(claim_interface=Mock(), release_interface=Mock(),
                                    dispose_resources=Mock())
        self.backend = PyUsbBackend(self.core, self.util)
        self.address = UsbAddress(1, 2)

    def test_constructor_and_close_without_open_do_not_access_usb(self):
        self.backend.close()
        self.core.find.assert_not_called()
        self.util.dispose_resources.assert_not_called()

    def test_open_requires_exact_usb_selection_and_claims_only_interface_zero(self):
        self.backend.open(self.address, 25)
        self.core.find.assert_called_once_with(find_all=True, idVendor=0x0451, idProduct=0x16A2,
                                              bus=1, address=2)
        self.assertEqual(self.device.default_timeout, 25)
        self.util.claim_interface.assert_called_once_with(self.device, 0)
        self.device.ctrl_transfer.assert_not_called()
        self.device.write.assert_not_called()
        self.device.read.assert_not_called()

    def test_no_match_or_multiple_matches_never_claim(self):
        for devices in ([], [self.device, self.device]):
            with self.subTest(count=len(devices)):
                self.core.find.return_value = devices
                with self.assertRaisesRegex(DebuggerError, "exactly one"):
                    self.backend.open(self.address, 10)
                self.util.claim_interface.assert_not_called()

    def test_missing_libusb_is_reported(self):
        self.core.find.side_effect = ValueError("No backend available")
        with self.assertRaisesRegex(TransportError, "No backend"):
            self.backend.open(self.address, 10)

    def test_unconfigured_device_is_not_automatically_configured(self):
        self.device.get_active_configuration.side_effect = OSError("Configuration not set")
        with self.assertRaisesRegex(TransportError, "Configuration not set"):
            self.backend.open(self.address, 10)
        self.util.claim_interface.assert_not_called()
        self.backend.close()
        self.util.dispose_resources.assert_called_once_with(self.device)

    def test_wrong_configuration_is_refused(self):
        self.device.config.bConfigurationValue = 2
        with self.assertRaisesRegex(DebuggerError, "configuration 1"):
            self.backend.open(self.address, 10)
        self.util.claim_interface.assert_not_called()

    def test_unexpected_interfaces_or_alternates_are_refused(self):
        for interfaces in ([], [Interface(), Interface()]):
            with self.subTest(count=len(interfaces)):
                self.setUp()
                self.device.config[:] = interfaces
                with self.assertRaisesRegex(DebuggerError, "interface 0"):
                    self.backend.open(self.address, 10)
                self.util.claim_interface.assert_not_called()
        for field in ("bInterfaceNumber", "bAlternateSetting"):
            with self.subTest(field=field):
                self.setUp()
                setattr(self.device.config[0], field, 1)
                with self.assertRaisesRegex(DebuggerError, "interface 0"):
                    self.backend.open(self.address, 10)
                self.util.claim_interface.assert_not_called()

    def test_wrong_or_missing_endpoints_are_refused(self):
        for field, value in (("bEndpointAddress", 0x02), ("bmAttributes", 3)):
            with self.subTest(field=field):
                self.setUp()
                setattr(self.device.config[0][0], field, value)
                with self.assertRaisesRegex(DebuggerError, "bulk endpoints"):
                    self.backend.open(self.address, 10)
                self.util.claim_interface.assert_not_called()

    def test_duplicate_endpoints_are_refused(self):
        self.device.config[0].append(self.device.config[0][0])
        with self.assertRaisesRegex(DebuggerError, "Duplicate"):
            self.backend.open(self.address, 10)
        self.util.claim_interface.assert_not_called()

    def test_active_kernel_driver_is_never_detached(self):
        self.device.is_kernel_driver_active.return_value = True
        with self.assertRaisesRegex(DebuggerError, "detachment"):
            self.backend.open(self.address, 10)
        self.util.claim_interface.assert_not_called()

    def test_claim_error_and_cleanup(self):
        self.util.claim_interface.side_effect = OSError("busy")
        with self.assertRaisesRegex(TransportError, "busy"):
            self.backend.open(self.address, 10)
        self.backend.close()
        self.util.release_interface.assert_called_once_with(self.device, 0)
        self.util.dispose_resources.assert_called_once_with(self.device)

    def test_transfer_arguments_and_byte_buffers(self):
        self.backend.open(self.address, 10)
        self.assertEqual(self.backend.control_read(0xC0, 0xC0, 0, 0, 8, 7), b"\0" * 8)
        self.device.ctrl_transfer.assert_called_once_with(0xC0, 0xC0, 0, 0, 8, timeout=7)
        self.assertEqual(self.backend.bulk_write(0x04, b"\x1f\x34", 5), 2)
        self.device.write.assert_called_once_with(0x04, b"\x1f\x34", timeout=5)
        self.assertEqual(self.backend.bulk_read(0x84, 1, 3), b"\x20")
        self.device.read.assert_called_once_with(0x84, 1, timeout=3)

    def test_usb_errors_are_wrapped_with_phase(self):
        self.backend.open(self.address, 10)
        for call, method, arguments, phase in (
                (self.device.ctrl_transfer, self.backend.control_read, (0xC0, 0xC0, 0, 0, 8, 5), "control read"),
                (self.device.ctrl_transfer, self.backend.control_write, (0x40, 0xC9, 0, 1, b"", 5), "control write"),
                (self.device.write, self.backend.bulk_write, (0x04, b"\x1f\x34", 5), "bulk write"),
                (self.device.read, self.backend.bulk_read, (0x84, 1, 5), "bulk read")):
            with self.subTest(phase=phase):
                call.side_effect = OSError("disconnected")
                with self.assertRaisesRegex(TransportError, f"{phase} failed"):
                    method(*arguments)

    def test_integer_read_is_not_converted_into_zero_filled_success(self):
        self.backend.open(self.address, 10)
        self.device.read.return_value = 1
        with self.assertRaisesRegex(TransportError, "bulk read"):
            self.backend.bulk_read(0x84, 1, 5)

    def test_zero_length_control_write_is_forwarded_as_out_with_finite_timeout(self):
        self.backend.open(self.address, 10)
        self.device.ctrl_transfer.return_value = 0
        self.assertEqual(self.backend.control_write(0x40, 0xC9, 0, 1, b"", 7), 0)
        self.device.ctrl_transfer.assert_called_once_with(0x40, 0xC9, 0, 1, b"", timeout=7)

    def test_device_revision_uses_cached_descriptor_not_a_string_read(self):
        with self.assertRaisesRegex(DebuggerError, "no claimed"):
            self.backend.device_revision()
        self.backend.open(self.address, 10)
        self.assertEqual(self.backend.device_revision(), 0x1234)
        self.device.ctrl_transfer.assert_not_called()

    def test_close_releases_explicitly_before_disposal(self):
        order = []
        self.util.release_interface.side_effect = lambda *args: order.append("release")
        self.util.dispose_resources.side_effect = lambda *args: order.append("dispose")
        self.backend.open(self.address, 10)
        self.backend.close()
        self.backend.close()
        self.assertEqual(order, ["release", "dispose"])
        self.util.release_interface.assert_called_once_with(self.device, 0)
        self.util.dispose_resources.assert_called_once_with(self.device)

    def test_release_and_dispose_errors_are_both_visible(self):
        self.backend.open(self.address, 10)
        self.util.release_interface.side_effect = OSError("release failed")
        self.util.dispose_resources.side_effect = OSError("dispose failed")
        with self.assertRaisesRegex(TransportError, "release failed.*dispose failed"):
            self.backend.close()
        self.util.dispose_resources.assert_called_once_with(self.device)
        with self.assertRaisesRegex(DebuggerError, "no claimed"):
            self.backend.bulk_read(0x84, 1, 10)

    def test_release_failure_still_disposes_handle(self):
        self.backend.open(self.address, 10)
        self.util.release_interface.side_effect = OSError("disconnected")
        with self.assertRaisesRegex(TransportError, "release interface"):
            self.backend.close()
        self.util.dispose_resources.assert_called_once_with(self.device)

    def test_io_before_open_is_refused(self):
        for method, arguments in ((self.backend.control_read, (0xC0, 0xC0, 0, 0, 8, 5)),
                                  (self.backend.control_write, (0x40, 0xC9, 0, 1, b"", 5)),
                                  (self.backend.bulk_read, (0x84, 1, 5)),
                                  (self.backend.bulk_write, (0x04, b"\x1f\x34", 5))):
            with self.subTest(method=method), self.assertRaisesRegex(DebuggerError, "no claimed"):
                method(*arguments)
        self.device.read.assert_not_called()
        self.device.write.assert_not_called()
        self.device.ctrl_transfer.assert_not_called()


class PyUsbReleaseIntegrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        try:
            cls.core = importlib.import_module("usb.core")
            cls.util = importlib.import_module("usb.util")
        except ModuleNotFoundError as error:
            if error.name != "usb":
                raise
            raise unittest.SkipTest("Optional PyUSB not installed; facade tests still run") from error

    def setUp(self):
        self.driver = Mock(spec_set=("open_device", "claim_interface", "release_interface", "close_device"))
        self.device = FakeDevice()
        # Exercise pinned PyUSB resource management, never its USB discovery.
        self.device._ctx = self.core._ResourceManager(object(), self.driver)
        self.backend = PyUsbBackend(SimpleNamespace(find=Mock(return_value=[self.device])), self.util)

    def test_actual_pyusb_release_errors_are_not_suppressed(self):
        self.backend.open(UsbAddress(1, 2), 10)
        self.driver.release_interface.side_effect = self.core.USBError("synthetic disconnect")
        with self.assertRaisesRegex(TransportError, "synthetic disconnect"):
            self.backend.close()
        self.driver.release_interface.assert_called_once()
        self.driver.close_device.assert_called_once()

    def test_actual_pyusb_closes_once_after_release(self):
        self.backend.open(UsbAddress(1, 2), 10)
        self.backend.close()
        self.backend.close()
        self.driver.claim_interface.assert_called_once()
        self.driver.release_interface.assert_called_once()
        self.driver.close_device.assert_called_once()

    def test_interrupted_claim_is_still_released_explicitly(self):
        original_claim = self.util.claim_interface

        def interrupted_claim(device, interface):
            original_claim(device, interface)
            raise KeyboardInterrupt("synthetic interruption")

        with patch.object(self.util, "claim_interface", side_effect=interrupted_claim):
            with self.assertRaises(KeyboardInterrupt):
                self.backend.open(UsbAddress(1, 2), 10)
        self.driver.release_interface.side_effect = self.core.USBError("release after interruption")
        with self.assertRaisesRegex(TransportError, "release after interruption"):
            self.backend.close()
        self.driver.release_interface.assert_called_once()
        self.driver.close_device.assert_called_once()

    def test_failed_claim_does_not_release_an_unclaimed_interface(self):
        self.driver.claim_interface.side_effect = self.core.USBError("synthetic busy")
        with self.assertRaisesRegex(TransportError, "synthetic busy"):
            self.backend.open(UsbAddress(1, 2), 10)
        self.backend.close()
        self.driver.release_interface.assert_not_called()
        self.driver.close_device.assert_called_once()


if __name__ == "__main__":
    unittest.main()
