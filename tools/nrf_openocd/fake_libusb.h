/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef NRF_OPENOCD_FAKE_LIBUSB_H
#define NRF_OPENOCD_FAKE_LIBUSB_H

#include <stdint.h>

enum usb_case {
	USB_NORMAL, USB_INIT_FAIL, USB_LIST_IO, USB_LIST_OTHER, USB_EMPTY,
	USB_DESCRIPTOR_FAIL, USB_OPEN_FAIL, USB_SERIAL_FAIL, USB_SERIAL_BAD,
	USB_SERIAL_OVERFLOW, USB_SERIAL_EMPTY, USB_SERIAL_MAX, USB_SERIAL_PADDED,
	USB_CONFIG_FAIL, USB_BAD_CLASS, USB_BAD_SUBCLASS, USB_FEW_ENDPOINTS,
	USB_NO_IN, USB_NO_OUT, USB_TRANSPORT_OPEN_FAIL, USB_CLAIM_FAIL,
	USB_RELEASE_FAIL, USB_CASE_COUNT
};

struct usb_counts {
	unsigned init, exit, list, free_list, descriptor, open, close, serial;
	unsigned ref, unref, config, free_config, claim, release;
};

void fake_usb_begin(uint16_t vid, uint16_t pid, enum usb_case mode);
struct usb_counts fake_usb_finish(void);

#endif
