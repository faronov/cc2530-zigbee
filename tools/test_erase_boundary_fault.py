# SPDX-License-Identifier: BSD-3-Clause
"""Offline DYLD tests using an original fake USB library, never native libusb."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory
import unittest


ENABLE_VARIABLE = "CC2530_ERASE_BOUNDARY_FAULT"

FAKE_HEADER = r"""
#ifndef M1_FAKE_USB_H
#define M1_FAKE_USB_H
typedef struct libusb_device_handle libusb_device_handle;
int libusb_bulk_transfer(libusb_device_handle *, unsigned char, unsigned char *,
                         int, int *, unsigned int);
void fake_prepare(libusb_device_handle *, unsigned char, unsigned char *,
                  int, int *, unsigned int, int, int, unsigned char);
#endif
"""

FAKE_LIBRARY = r"""
#include "fake_usb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct {
    libusb_device_handle *device;
    unsigned char endpoint;
    unsigned char *data;
    unsigned char original_data[16];
    int length;
    int *transferred;
    unsigned int timeout;
    int result;
    int count;
    unsigned char reply;
    int prepared;
} expected;
static unsigned int calls;

void fake_prepare(libusb_device_handle *device, unsigned char endpoint,
                  unsigned char *data, int length, int *transferred,
                  unsigned int timeout, int result, int count, unsigned char reply)
{
    expected.device = device;
    expected.endpoint = endpoint;
    expected.data = data;
    memcpy(expected.original_data, data, (size_t)length);
    expected.length = length;
    expected.transferred = transferred;
    expected.timeout = timeout;
    expected.result = result;
    expected.count = count;
    expected.reply = reply;
    expected.prepared = 1;
}

int libusb_bulk_transfer(libusb_device_handle *device, unsigned char endpoint,
                         unsigned char *data, int length, int *transferred,
                         unsigned int timeout)
{
    if (!expected.prepared || device != expected.device ||
        endpoint != expected.endpoint || data != expected.data ||
        length != expected.length || transferred != expected.transferred ||
        timeout != expected.timeout ||
        memcmp(data, expected.original_data, (size_t)length) != 0) {
        fputs("synthetic USB: forwarding mismatch or duplicate call\n", stderr);
        exit(71);
    }
    expected.prepared = 0;
    if (transferred != NULL)
        *transferred = expected.count;
    if ((endpoint & 0x80) != 0 && length > 0)
        data[0] = expected.reply;
    printf("called:%u\n", ++calls);
    fflush(stdout);
    return expected.result;
}
"""

FAKE_DRIVER = r"""
#include "fake_usb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct libusb_device_handle {
    unsigned int identity;
};

static void cleanup(void)
{
    puts("cleanup");
}

static void fail(const char *reason)
{
    fprintf(stderr, "synthetic driver: %s\n", reason);
    exit(72);
}

int main(int argc, char **argv)
{
    struct libusb_device_handle devices[2] = {{1}, {2}};
    if (atexit(cleanup) != 0)
        fail("atexit registration failed");
    for (int index = 1; index < argc; ++index) {
        unsigned int handle, endpoint, timeout, with_count, reply;
        int length, result, count, parsed = 0;
        int actual_count = -777;
        char hex[33] = {0};
        unsigned char data[16] = {0};
        unsigned char after[16] = {0};
        if (sscanf(argv[index], "%u:%x:%d:%d:%d:%u:%u:%x:%32s%n",
                   &handle, &endpoint, &length, &result, &count, &timeout,
                   &with_count, &reply, hex, &parsed) != 9 ||
            argv[index][parsed] != '\0' || handle < 1 || handle > 2 ||
            endpoint > 255 || reply > 255 || with_count > 1 ||
            length < 0 || length > 16)
            fail("invalid synthetic transfer");
        if (strcmp(hex, "-") != 0) {
            size_t hex_length = strlen(hex);
            if (hex_length % 2 != 0)
                fail("invalid synthetic payload");
            for (size_t offset = 0; offset < hex_length; offset += 2) {
                unsigned int value;
                if (sscanf(hex + offset, "%2x", &value) != 1)
                    fail("invalid synthetic byte");
                data[offset / 2] = (unsigned char)value;
            }
        }
        memcpy(after, data, sizeof(after));
        if ((endpoint & 0x80) != 0 && length > 0)
            after[0] = (unsigned char)reply;
        fake_prepare(&devices[handle - 1], (unsigned char)endpoint, data, length,
                     with_count ? &actual_count : NULL, timeout, result, count,
                     (unsigned char)reply);
        int actual_result = libusb_bulk_transfer(
            &devices[handle - 1], (unsigned char)endpoint, data, length,
            with_count ? &actual_count : NULL, timeout);
        if (actual_result != result ||
            actual_count != (with_count ? count : -777) ||
            memcmp(data, after, sizeof(data)) != 0)
            fail("return value, completion count, or data changed");
        printf("returned:%d\n", index);
        fflush(stdout);
    }
    puts("complete");
    return 0;
}
"""


def transfer(endpoint, data=b"", *, handle=1, length=None, result=0,
             count=None, timeout=123457, with_count=True, reply=0):
    length = len(data) if length is None else length
    count = length if count is None else count
    return (f"{handle}:{endpoint:x}:{length}:{result}:{count}:{timeout}:"
            f"{int(with_count)}:{reply:x}:{data.hex() or '-'}")


def erase(**options):
    return transfer(0x04, b"\x1C\x14", **options)


def query(**options):
    return transfer(0x04, b"\x1F\x34", **options)


def status(value=0x22, **options):
    return transfer(0x84, b"\xCC", reply=value, **options)


@unittest.skipUnless(sys.platform == "darwin", "macOS DYLD interposition test; no hardware fallback")
class EraseBoundaryFaultTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("clang") or shutil.which("cc")
        if compiler is None:
            raise unittest.SkipTest("A macOS host C compiler is required for the synthetic USB test")
        cls.directory = TemporaryDirectory(prefix="cc2530-erase-boundary-")
        cls.addClassCleanup(cls.directory.cleanup)
        directory = Path(cls.directory.name)
        cls.environment = {
            key: value for key, value in os.environ.items()
            if not key.startswith("DYLD_") and key not in (ENABLE_VARIABLE, "LD_PRELOAD")
        }
        for name, source in (("fake_usb.h", FAKE_HEADER), ("fake_usb.c", FAKE_LIBRARY),
                             ("driver.c", FAKE_DRIVER)):
            (directory / name).write_text(source, encoding="ascii")
        cls.library = directory / "libm1_synthetic_usb.dylib"
        cls.interposer = directory / "erase_boundary_fault.dylib"
        cls.driver = directory / "synthetic_driver"
        flags = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-Wpedantic"]
        commands = (
            [*flags, "-dynamiclib", str(directory / "fake_usb.c"),
             f"-Wl,-install_name,{cls.library}", "-o", str(cls.library)],
            [*flags, str(directory / "driver.c"), str(cls.library), "-o", str(cls.driver)],
            [*flags, "-dynamiclib", "-Wl,-undefined,dynamic_lookup",
             str(Path(__file__).with_name("erase_boundary_fault.c")),
             "-o", str(cls.interposer)],
        )
        for command in commands:
            result = subprocess.run(command, env=cls.environment, capture_output=True,
                                    text=True, timeout=60)
            if result.returncode != 0:
                raise AssertionError(f"Offline compile failed for {command[-1]}:\n"
                                     f"{result.stdout}{result.stderr}")

    def conversation(self, steps, *, enabled="1", stop_at=None, interpose=True):
        environment = dict(self.environment)
        if interpose:
            environment["DYLD_INSERT_LIBRARIES"] = str(self.interposer)
        if enabled is not None:
            environment[ENABLE_VARIABLE] = enabled
        result = subprocess.run([str(self.driver), *steps], env=environment,
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0 if stop_at is None else 99,
                         f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}")
        expected = []
        for index in range(1, len(steps) + 1):
            expected.append(f"called:{index}")
            if index == stop_at:
                break
            expected.append(f"returned:{index}")
        if stop_at is None:
            expected += ["complete", "cleanup"]
            self.assertEqual(result.stderr, "")
        else:
            self.assertIn("M1 fault:", result.stderr)
            self.assertIn("terminating before programming", result.stderr)
        self.assertEqual(result.stdout, "\n".join(expected) + "\n")

    def test_synthetic_driver_without_interposer_is_a_passing_control(self):
        self.conversation([erase(), query(), status()], interpose=False)

    def test_unset_off_and_invalid_opt_in_are_exact_pass_through(self):
        for enabled in (None, "", "0", "false", "true", "yes", "01", " 1", "1 ", "2", "-1", "1\n"):
            with self.subTest(enabled=enabled):
                self.conversation([erase(), query(), status()], enabled=enabled)

    def test_enabled_exits_only_after_ready_status_before_next_transfer_and_cleanup(self):
        self.conversation([erase(), query(), status(), transfer(0x04, b"\x99\x42")], stop_at=3)

    def test_status_bit_combinations_require_not_busy_unlocked_and_halted(self):
        for value in range(256):
            eligible = not (value & 0x80) and not (value & 0x04) and bool(value & 0x20)
            with self.subTest(status=value):
                self.conversation([erase(), query(), status(value)], stop_at=3 if eligible else None)

    def test_busy_locked_and_running_then_fresh_ready_status(self):
        for value in (0xA2, 0x82, 0x26, 0x06, 0x02, 0x00):
            with self.subTest(status=value):
                self.conversation([erase(), query(), status(value), query(), status()], stop_at=5)

    def test_ready_reply_without_an_exact_erase_does_not_trigger(self):
        self.conversation([query(), status()])
        for packet in (b"", b"\x1C", b"\x1C\x14\x00", b"\x1F\x14",
                       b"\x1C\x10", b"\x1C\x15", b"\x00\x14", b"\x1C\x00"):
            with self.subTest(packet=packet):
                self.conversation([transfer(0x04, packet), query(), status()])
        self.conversation([transfer(0x05, b"\x1C\x14"), query(), status()])

    def test_unrelated_commands_and_unsolicited_replies_do_not_trigger(self):
        self.conversation([erase(), status()])
        for packet in (b"", b"\x1F", b"\x1F\x34\x00", b"\x1F\x24",
                       b"\x1F\x64", b"\x3F\x28", b"\x1C\x34"):
            with self.subTest(packet=packet):
                self.conversation([erase(), transfer(0x04, packet), status()])
        self.conversation([erase(), transfer(0x05, b"\x1F\x34"), status()])
        self.conversation([erase(), query(), transfer(0x81, b"\xCC", reply=0x22)])

    def test_intervening_out_invalidates_the_pending_status_reply(self):
        self.conversation([erase(), query(), transfer(0x04, b"\x1F\x24"), status()])

    def test_other_handle_cannot_arm_or_supply_the_completion_reply(self):
        self.conversation([erase(handle=1), query(handle=2), status(handle=2)])
        self.conversation([erase(handle=1), query(handle=1), status(handle=2)])
        self.conversation([erase(handle=1), query(handle=2), status(handle=1)])
        self.conversation([erase(handle=2), query(handle=2), status(handle=2)], stop_at=3)

    def test_unrelated_handle_reply_does_not_consume_the_matching_pending_reply(self):
        self.conversation([erase(), query(), status(handle=2), status()], stop_at=4)

    def test_erase_out_must_succeed_with_exact_full_completion(self):
        for options in ({"count": 0}, {"count": 1}, {"count": 3}, {"count": -1},
                        {"result": -7}, {"result": 1}, {"with_count": False}):
            with self.subTest(options=options):
                self.conversation([erase(**options), query(), status()])

    def test_status_out_must_succeed_with_exact_full_completion(self):
        for options in ({"count": 0}, {"count": 1}, {"count": 3}, {"count": -1},
                        {"result": -7}, {"result": 1}, {"with_count": False}):
            with self.subTest(options=options):
                self.conversation([erase(), query(**options), status()])

    def test_status_in_must_succeed_with_a_complete_one_byte_request_and_reply(self):
        for options in ({"count": 0}, {"count": 2}, {"count": -1}, {"result": -7},
                        {"result": 1}, {"with_count": False}, {"length": 0, "count": 0},
                        {"length": 0, "count": 1}, {"length": 2, "count": 1},
                        {"length": 2, "count": 2}):
            with self.subTest(options=options):
                self.conversation([erase(), query(), status(**options)])
        self.conversation([erase(), query(), transfer(0x84, b"\x22", length=0, count=1)])

    def test_consumed_busy_reply_requires_a_new_status_command(self):
        self.conversation([erase(), query(), status(0xA2), status()])

    def test_failed_or_short_in_cannot_leave_a_stale_status_token(self):
        for options in ({"count": 0}, {"result": -7}, {"with_count": False}):
            with self.subTest(options=options):
                self.conversation([erase(), query(), status(**options), status()])

    def test_failed_intervening_out_cannot_leave_a_stale_status_token(self):
        for options in ({"result": -7}, {"with_count": False}):
            with self.subTest(options=options):
                self.conversation([erase(), query(),
                                   transfer(0x04, b"\x1F\x24", **options), status()])

    def test_a_new_successful_status_query_can_follow_a_failed_transfer(self):
        self.conversation([erase(), query(), status(result=-7), query(), status()], stop_at=5)

    def test_all_abi_parameters_results_and_buffers_are_forwarded_unchanged(self):
        steps = [
            transfer(0x04, b"\xAB\xCD", handle=2, result=-7, count=1, timeout=0),
            transfer(0x82, b"\x11\x22\x33\x44", count=3, timeout=0xFFFFFFFF, reply=0xE7),
            transfer(0x05, b"\x80\xFF", handle=2, result=7, count=-1, timeout=1),
            transfer(0x84, b"\xCC", with_count=False, reply=0x80, timeout=987654321),
            transfer(0x02, b"", with_count=False),
        ]
        for enabled in (None, "0", "invalid", "1"):
            with self.subTest(enabled=enabled):
                self.conversation(steps, enabled=enabled)


if __name__ == "__main__":
    unittest.main()
