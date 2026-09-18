/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 *
 * Linux-only, per-process LD_PRELOAD guard for the reviewed external cc-tool.
 * Reject normal-run reset BEFORE USB submission; never substitute a successful
 * transfer or a debug reset. Exit 86 is an intercepted request, not proof of
 * successful programming, verification, or a halted target. Exit 87 is failure.
 * This does not block debugger RESUME/STEP or the programmer's RAM executor.
 */
#define _GNU_SOURCE
#if !defined(__linux__)
#error This guard requires the reviewed Linux ELF/libusb calling convention
#endif
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

struct libusb_device_handle;
typedef int (*control_transfer_t)(struct libusb_device_handle *, uint8_t, uint8_t,
                                 uint16_t, uint16_t, unsigned char *, uint16_t, unsigned int);
typedef char symbol_pointer_size[(sizeof(control_transfer_t) == sizeof(void *)) ? 1 : -1];

static void notice(const char *text)
{
    size_t length = strlen(text);
    if (write(STDERR_FILENO, text, length) != (ssize_t)length)
        _exit(87);
}

__attribute__((constructor))
static void active(void)
{
    notice("cc-tool-no-run-guard: active\n");
}

int libusb_control_transfer(struct libusb_device_handle *handle, uint8_t type,
                            uint8_t request, uint16_t value, uint16_t index,
                            unsigned char *data, uint16_t length, unsigned int timeout)
{
    control_transfer_t next;
    void *symbol;
    if (request == 0xc9 && !(type & 0x80) &&
        !(type == 0x40 && value == 0 && index == 1 && length == 0 && data == NULL)) {
        notice("cc-tool-no-run-guard: reset-to-run or unreviewed reset blocked before USB\n");
        if (fflush(NULL) != 0) {
            notice("cc-tool-no-run-guard: buffered output flush failed\n");
            _exit(87);
        }
        _exit(86);
    }
    symbol = dlsym(RTLD_NEXT, "libusb_control_transfer");
    if (symbol == NULL) {
        notice("cc-tool-no-run-guard: missing real libusb_control_transfer\n");
        _exit(87);
    }
    memcpy(&next, &symbol, sizeof(next));
    return next(handle, type, request, value, index, data, length, timeout);
}
