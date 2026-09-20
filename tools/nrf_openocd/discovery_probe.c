/* SPDX-License-Identifier: BSD-3-Clause */
/* Tests the genuine shared libjaylink, not a source excerpt or replacement. */
#include "fake_libusb.h"
#include <libjaylink/libjaylink.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned cases, admitted, warnings, errors;

#define CHECK(condition) do { if (!(condition)) { \
	fprintf(stderr, "discovery assertion %s:%d case=%u\n", __func__, __LINE__, cases); \
	exit(1); \
} } while (0)

static int log_message(const struct jaylink_context *ctx, enum jaylink_log_level level,
		const char *format, va_list arguments, void *data)
{
	(void)ctx; (void)format; (void)arguments; (void)data;
	if (level == JAYLINK_LOG_LEVEL_WARNING) ++warnings;
	if (level == JAYLINK_LOG_LEVEL_ERROR) ++errors;
	return 0;
}

static int expected_address(uint16_t vendor, uint16_t product, bool patched)
{
	static const uint16_t pids[] = {
		0x0101, 0x0102, 0x0103, 0x0104, 0x0105, 0x0107, 0x0108,
		0x1010, 0x1011, 0x1012, 0x1013, 0x1014, 0x1015, 0x1016,
		0x1017, 0x1018, 0x1020, 0x1051, 0x1055, 0x1061,
	};
	if (vendor != 0x1366) return -1;
	if (patched && product == 0x1025) return 0;
	for (size_t i = 0; i < sizeof(pids) / sizeof(pids[0]); ++i)
		if (pids[i] == product)
			return product >= 0x0102 && product <= 0x0104 ? product - 0x0101 : 0;
	return -1;
}

static void run(uint16_t vendor, uint16_t product, enum usb_case mode,
		bool patched, bool transport, bool repeat)
{
	++cases; warnings = errors = 0;
	fake_usb_begin(vendor, product, mode);
	struct jaylink_context *ctx = NULL;
	int ret = jaylink_init(&ctx);
	if (mode == USB_INIT_FAIL) {
		CHECK(ret != JAYLINK_OK && !ctx);
		struct usb_counts count = fake_usb_finish();
		CHECK(count.init == 1 && count.exit == 0 && count.list == 0);
		return;
	}
	CHECK(ret == JAYLINK_OK && ctx);
	CHECK(jaylink_log_set_callback(ctx, log_message, NULL) == JAYLINK_OK);
	ret = jaylink_discovery_scan(ctx, JAYLINK_HIF_USB);
	bool list_failure = mode == USB_LIST_IO || mode == USB_LIST_OTHER;
	CHECK(ret == (mode == USB_LIST_IO ? JAYLINK_ERR_IO :
		(mode == USB_LIST_OTHER ? JAYLINK_ERR : JAYLINK_OK)));
	int address = expected_address(vendor, product, patched);
	bool candidate = address >= 0 && !list_failure && mode != USB_EMPTY &&
		mode != USB_DESCRIPTOR_FAIL && mode != USB_OPEN_FAIL &&
		mode != USB_SERIAL_BAD && mode != USB_SERIAL_OVERFLOW;
	struct jaylink_device **devices = NULL;
	size_t count = 999;
	CHECK(jaylink_get_devices(ctx, &devices, &count) == JAYLINK_OK);
	CHECK(count == (candidate ? 1U : 0U) && devices[count] == NULL);
	if (candidate) {
		++admitted;
		uint32_t serial = 0xfeedbeefU;
		enum jaylink_usb_address usb_address;
		enum jaylink_host_interface iface;
		ret = jaylink_device_get_serial_number(devices[0], &serial);
		if (mode == USB_SERIAL_FAIL)
			CHECK(ret == JAYLINK_ERR_NOT_AVAILABLE && serial == 0xfeedbeefU);
		else {
			CHECK(ret == JAYLINK_OK);
			CHECK(serial == (mode == USB_SERIAL_EMPTY ? 0U :
				(mode == USB_SERIAL_MAX ? UINT32_MAX : 123456789U)));
		}
		CHECK(jaylink_device_get_usb_address(devices[0], &usb_address) == JAYLINK_OK);
		CHECK((int)usb_address == address);
		CHECK(jaylink_device_get_host_interface(devices[0], &iface) == JAYLINK_OK);
		CHECK(iface == JAYLINK_HIF_USB);
		if (repeat) {
			CHECK(jaylink_discovery_scan(ctx, JAYLINK_HIF_USB) == JAYLINK_OK);
			struct jaylink_device **again = NULL;
			size_t again_count = 999;
			CHECK(jaylink_get_devices(ctx, &again, &again_count) == JAYLINK_OK);
			CHECK(again_count == 1 && again[0] == devices[0] && !again[1]);
			jaylink_free_devices(again, true);
		}
		if (transport) {
			struct jaylink_device_handle *handle = NULL;
			ret = jaylink_open(devices[0], &handle);
			bool successful = mode == USB_NORMAL || mode == USB_RELEASE_FAIL;
			CHECK((ret == JAYLINK_OK) == successful);
			if (successful)
				CHECK(jaylink_close(handle) == (mode == USB_RELEASE_FAIL ? JAYLINK_ERR : JAYLINK_OK));
		}
	}
	jaylink_free_devices(devices, true);
	CHECK(jaylink_exit(ctx) == JAYLINK_OK);
	struct usb_counts c = fake_usb_finish();
	unsigned scans = repeat ? 2 : 1;
	CHECK(c.init == 1 && c.exit == 1 && c.list == scans);
	CHECK(c.free_list == (list_failure ? 0U : scans));
	CHECK(c.descriptor == (list_failure || mode == USB_EMPTY ? 0U : scans));
	bool serial_attempt = address >= 0 && !list_failure && mode != USB_EMPTY &&
		mode != USB_DESCRIPTOR_FAIL;
	CHECK(c.serial == (serial_attempt && mode != USB_OPEN_FAIL ? 1U : 0U));
	CHECK(c.ref == (candidate ? 1U : 0U) && c.unref == c.ref);
	bool config_ok = transport && mode != USB_CONFIG_FAIL && mode != USB_BAD_CLASS &&
		mode != USB_BAD_SUBCLASS && mode != USB_FEW_ENDPOINTS && mode != USB_NO_IN && mode != USB_NO_OUT;
	bool opened_transport = config_ok && mode != USB_TRANSPORT_OPEN_FAIL;
	CHECK(c.open == (serial_attempt ? 1U : 0U) + (config_ok ? 1U : 0U));
	CHECK(c.close == (serial_attempt && mode != USB_OPEN_FAIL ? 1U : 0U) +
		(opened_transport ? 1U : 0U));
	CHECK(c.config == (transport ? 1U : 0U));
	CHECK(c.free_config == (transport && mode != USB_CONFIG_FAIL ? 1U : 0U));
	CHECK(c.claim == (opened_transport ? 1U : 0U));
	CHECK(c.release == (opened_transport && mode != USB_CLAIM_FAIL ? 1U : 0U));
	if (mode == USB_DESCRIPTOR_FAIL || mode == USB_OPEN_FAIL || mode == USB_SERIAL_FAIL ||
			mode == USB_SERIAL_BAD || mode == USB_SERIAL_OVERFLOW)
		CHECK(warnings > 0);
	if (list_failure || (transport && mode != USB_NORMAL))
		CHECK(errors > 0);
}

static unsigned number(const char *text)
{
	char *end;
	errno = 0;
	unsigned long value = strtoul(text, &end, 10);
	CHECK(text[0] && !*end && !errno && value <= 65536);
	return (unsigned)value;
}

int main(int argc, char **argv)
{
	CHECK(argc == 2 || argc == 4);
	if (!strcmp(argv[1], "baseline")) {
		CHECK(argc == 2);
		run(0x1366, 0x1025, USB_NORMAL, false, false, false);
	} else if (!strcmp(argv[1], "special")) {
		CHECK(argc == 2);
		for (enum usb_case mode = USB_NORMAL; mode < USB_CASE_COUNT; ++mode)
			run(0x1366, 0x1025, mode, true, mode >= USB_CONFIG_FAIL, false);
		run(0x1366, 0x1025, USB_NORMAL, true, true, false);
		run(0x1366, 0x1025, USB_NORMAL, true, false, true);
	} else {
		CHECK(argc == 4);
		bool pids = !strcmp(argv[1], "pids");
		CHECK(pids || !strcmp(argv[1], "vendors"));
		unsigned first = number(argv[2]), length = number(argv[3]);
		CHECK(length && length <= 4096 && first + length <= 65536);
		for (unsigned i = first; i < first + length; ++i)
			run(pids ? 0x1366 : (uint16_t)i, pids ? (uint16_t)i : 0x1025,
				USB_NORMAL, true, false, false);
	}
	printf("cases=%u admitted=%u\n", cases, admitted);
	return 0;
}
