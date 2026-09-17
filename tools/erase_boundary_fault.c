/* SPDX-License-Identifier: BSD-3-Clause
 * Manual macOS fault injector for an external programmer, never firmware.
 */
#if !defined(__APPLE__)
#error "This opt-in DYLD interposer is specific to macOS"
#endif

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct libusb_device_handle libusb_device_handle;

extern int libusb_bulk_transfer(libusb_device_handle *, unsigned char,
                               unsigned char *, int, int *, unsigned int);

static libusb_device_handle *erasing_device;
static int status_pending;

static int erase_boundary_transfer(libusb_device_handle *device, unsigned char endpoint,
                                   unsigned char *data, int length, int *transferred,
                                   unsigned int timeout)
{
    int result = libusb_bulk_transfer(device, endpoint, data, length, transferred, timeout);
    const char *enabled = getenv("CC2530_ERASE_BOUNDARY_FAULT");
    if (!enabled || strcmp(enabled, "1") != 0) {
        erasing_device = NULL;
        status_pending = 0;
        return result;
    }
    if (result != 0 || !transferred || length < 0 || *transferred != length
            || (length > 0 && !data)) {
        status_pending = 0;
        return result;
    }

    if (endpoint == 0x04) {
        if (length == 2 && *transferred == 2 && data[0] == 0x1c && data[1] == 0x14)
            erasing_device = device;
        status_pending = device == erasing_device && length == 2 && *transferred == 2
                         && data[0] == 0x1f && data[1] == 0x34;
    } else if (device == erasing_device && endpoint == 0x84 && status_pending) {
        status_pending = 0;
        if (length == 1 && (data[0] & 0xa4) == 0x20) {
            static const char message[] = "M1 fault: erased, halted, unlocked; terminating before programming\n";
            (void)write(STDERR_FILENO, message, sizeof(message) - 1);
            /* No programmer destructor or reset-on-close may run after this point. */
            _exit(99);
        }
    }
    return result;
}

__attribute__((used))
static const struct {
    const void *replacement;
    const void *original;
} interpose __attribute__((section("__DATA,__interpose"))) = {
    (const void *)erase_boundary_transfer, (const void *)libusb_bulk_transfer
};
