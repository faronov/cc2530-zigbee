/* SPDX-License-Identifier: BSD-3-Clause */
/* Original strict descriptor/lifetime model. No USB protocol replies or I/O. */
#include "fake_libusb.h"
#include <libusb.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct libusb_context { int unused; };
struct libusb_device { int unused; };
struct libusb_device_handle { int unused; };
static struct libusb_context context;
static struct libusb_device device;
static struct libusb_device_handle handle;
static struct libusb_device *devices[] = { &device, NULL };
static struct usb_counts counts;
static enum usb_case mode;
static uint16_t vid, pid;
static unsigned references;
static bool active, listed, opened, claimed, configured;
static struct libusb_endpoint_descriptor endpoints[2];
static struct libusb_interface_descriptor settings[2];
static struct libusb_interface interfaces[2];
static struct libusb_config_descriptor config;

#define REQUIRE(condition) do { if (!(condition)) { \
	fprintf(stderr, "UNEXPECTED libusb contract %s:%d\n", __func__, __LINE__); \
	_exit(124); \
} } while (0)

void fake_usb_begin(uint16_t vendor, uint16_t product, enum usb_case selected)
{
	REQUIRE(!active && !listed && !opened && !claimed && !configured && !references);
	REQUIRE(selected < USB_CASE_COUNT);
	memset(&counts, 0, sizeof(counts));
	vid = vendor; pid = product; mode = selected;
}

struct usb_counts fake_usb_finish(void)
{
	REQUIRE(!active && !listed && !opened && !claimed && !configured && !references);
	return counts;
}

int libusb_init(libusb_context **ctx)
{
	REQUIRE(ctx && !active);
	++counts.init;
	if (mode == USB_INIT_FAIL) return LIBUSB_ERROR_OTHER;
	active = true; *ctx = &context;
	return LIBUSB_SUCCESS;
}

void libusb_exit(libusb_context *ctx)
{
	REQUIRE(ctx == &context && active && !references && !opened && !listed && !configured);
	++counts.exit; active = false;
}

ssize_t libusb_get_device_list(libusb_context *ctx, libusb_device ***list)
{
	REQUIRE(ctx == &context && active && list && !listed && !opened);
	++counts.list;
	if (mode == USB_LIST_IO) return LIBUSB_ERROR_IO;
	if (mode == USB_LIST_OTHER) return LIBUSB_ERROR_OTHER;
	listed = true;
	if (mode == USB_EMPTY) { *list = devices + 1; return 0; }
	++references; *list = devices;
	return 1;
}

void libusb_free_device_list(libusb_device **list, int unref)
{
	REQUIRE(active && listed && !opened && unref == 1);
	REQUIRE(list == (mode == USB_EMPTY ? devices + 1 : devices));
	if (mode != USB_EMPTY) { REQUIRE(references); --references; }
	++counts.free_list; listed = false;
}

int libusb_get_device_descriptor(libusb_device *dev, struct libusb_device_descriptor *desc)
{
	REQUIRE(active && listed && references && dev == &device && desc && !opened);
	++counts.descriptor;
	if (mode == USB_DESCRIPTOR_FAIL) return LIBUSB_ERROR_IO;
	memset(desc, 0, sizeof(*desc));
	desc->bLength = LIBUSB_DT_DEVICE_SIZE; desc->bDescriptorType = LIBUSB_DT_DEVICE;
	desc->bcdUSB = 0x0200; desc->bMaxPacketSize0 = 64; desc->bcdDevice = 0x0100;
	desc->iManufacturer = 1; desc->iProduct = 2; desc->bNumConfigurations = 1;
	desc->idVendor = vid; desc->idProduct = pid; desc->iSerialNumber = 3;
	return LIBUSB_SUCCESS;
}

uint8_t libusb_get_bus_number(libusb_device *dev)
{
	REQUIRE(active && references && dev == &device); return 7;
}

uint8_t libusb_get_device_address(libusb_device *dev)
{
	REQUIRE(active && references && dev == &device); return 11;
}

int libusb_open(libusb_device *dev, libusb_device_handle **out)
{
	REQUIRE(active && references && dev == &device && out && !opened && !claimed);
	++counts.open;
	if (mode == USB_OPEN_FAIL || (mode == USB_TRANSPORT_OPEN_FAIL && counts.open == 2))
		return LIBUSB_ERROR_ACCESS;
	opened = true; *out = &handle;
	return LIBUSB_SUCCESS;
}

void libusb_close(libusb_device_handle *devh)
{
	REQUIRE(active && opened && !claimed && devh == &handle);
	++counts.close; opened = false;
}

int libusb_get_string_descriptor_ascii(libusb_device_handle *devh, uint8_t index,
		unsigned char *data, int length)
{
	REQUIRE(active && listed && opened && !claimed && devh == &handle);
	REQUIRE(index == 3 && data && length == 13);
	++counts.serial;
	if (mode == USB_SERIAL_FAIL) return LIBUSB_ERROR_IO;
	const char *text = "123456789";
	switch (mode) {
	case USB_SERIAL_BAD: text = "bad"; break;
	case USB_SERIAL_OVERFLOW: text = "4294967296"; break;
	case USB_SERIAL_EMPTY: text = ""; break;
	case USB_SERIAL_MAX: text = "004294967295"; break;
	case USB_SERIAL_PADDED: text = "000123456789"; break;
	default: break;
	}
	size_t size = strlen(text);
	REQUIRE(size < (size_t)length);
	memcpy(data, text, size + 1);
	return (int)size;
}

libusb_device *libusb_ref_device(libusb_device *dev)
{
	REQUIRE(active && references && dev == &device);
	++counts.ref; ++references; return dev;
}

void libusb_unref_device(libusb_device *dev)
{
	REQUIRE(active && references && dev == &device && !opened);
	++counts.unref; --references;
}

int libusb_get_active_config_descriptor(libusb_device *dev,
		struct libusb_config_descriptor **out)
{
	REQUIRE(active && references && !listed && !opened && !configured && dev == &device && out);
	++counts.config;
	if (mode == USB_CONFIG_FAIL) return LIBUSB_ERROR_NOT_FOUND;
	memset(&config, 0, sizeof(config));
	memset(settings, 0, sizeof(settings));
	memset(endpoints, 0, sizeof(endpoints));
	memset(interfaces, 0, sizeof(interfaces));
	endpoints[0].bEndpointAddress = mode == USB_NO_IN ? 0x01 : 0x81;
	endpoints[1].bEndpointAddress = mode == USB_NO_OUT ? 0x82 : 0x02;
	endpoints[0].bmAttributes = endpoints[1].bmAttributes = LIBUSB_TRANSFER_TYPE_BULK;
	for (unsigned i = 0; i < 2; ++i) {
		endpoints[i].bLength = LIBUSB_DT_ENDPOINT_SIZE;
		endpoints[i].bDescriptorType = LIBUSB_DT_ENDPOINT;
		endpoints[i].wMaxPacketSize = 64;
		settings[i].bLength = LIBUSB_DT_INTERFACE_SIZE;
		settings[i].bDescriptorType = LIBUSB_DT_INTERFACE;
	}
	settings[0].bInterfaceClass = LIBUSB_CLASS_COMM;
	settings[1].bInterfaceClass = mode == USB_BAD_CLASS ? LIBUSB_CLASS_COMM : LIBUSB_CLASS_VENDOR_SPEC;
	settings[1].bInterfaceSubClass = mode == USB_BAD_SUBCLASS ? 0 : LIBUSB_CLASS_VENDOR_SPEC;
	settings[1].bInterfaceNumber = 1;
	settings[1].bNumEndpoints = mode == USB_FEW_ENDPOINTS ? 1 : 2;
	settings[1].endpoint = endpoints;
	for (unsigned i = 0; i < 2; ++i) {
		interfaces[i].num_altsetting = 1; interfaces[i].altsetting = &settings[i];
	}
	config.bNumInterfaces = 2; config.interface = interfaces;
	config.bLength = LIBUSB_DT_CONFIG_SIZE; config.bDescriptorType = LIBUSB_DT_CONFIG;
	config.wTotalLength = LIBUSB_DT_CONFIG_SIZE + 2 * LIBUSB_DT_INTERFACE_SIZE +
		settings[1].bNumEndpoints * LIBUSB_DT_ENDPOINT_SIZE;
	config.bConfigurationValue = 1; config.bmAttributes = 0x80; config.MaxPower = 50;
	configured = true; *out = &config; return LIBUSB_SUCCESS;
}

void libusb_free_config_descriptor(struct libusb_config_descriptor *desc)
{
	REQUIRE(active && configured && desc == &config);
	++counts.free_config; configured = false;
}

int libusb_claim_interface(libusb_device_handle *devh, int number)
{
	REQUIRE(active && opened && !listed && !configured && !claimed && devh == &handle && number == 1);
	++counts.claim;
	if (mode == USB_CLAIM_FAIL) return LIBUSB_ERROR_BUSY;
	claimed = true; return LIBUSB_SUCCESS;
}

int libusb_release_interface(libusb_device_handle *devh, int number)
{
	REQUIRE(active && opened && claimed && devh == &handle && number == 1);
	++counts.release; claimed = false;
	return mode == USB_RELEASE_FAIL ? LIBUSB_ERROR_IO : LIBUSB_SUCCESS;
}

const char *libusb_error_name(int code)
{
	REQUIRE(code < 0); return "synthetic USB failure";
}

int libusb_get_port_numbers(libusb_device *dev, uint8_t *ports, int length)
{
	(void)dev; (void)ports; (void)length; REQUIRE(false); return LIBUSB_ERROR_OTHER;
}

int libusb_bulk_transfer(libusb_device_handle *devh, unsigned char endpoint,
		unsigned char *data, int length, int *transferred, unsigned int timeout)
{
	(void)devh; (void)endpoint; (void)data; (void)length; (void)transferred; (void)timeout;
	REQUIRE(false); return LIBUSB_ERROR_OTHER;
}
