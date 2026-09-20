/* SPDX-License-Identifier: BSD-3-Clause */
/* Synthetic API, not an adapter protocol implementation or hardware backend. */
#define _POSIX_C_SOURCE 200809L
#include <libjaylink/libjaylink.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct jaylink_context { int unused; };
struct jaylink_device { int unused; };
struct jaylink_device_handle { int unused; };
static struct jaylink_context context;
static struct jaylink_device device;
static struct jaylink_device_handle handle;
static struct jaylink_device *devices[] = { &device, NULL };

static int event(const char *name)
{
	fprintf(stderr, "FAKE %s\n", name);
	const char *failure = getenv("FAKE_FAIL");
	return failure && !strcmp(failure, name) ? JAYLINK_ERR : JAYLINK_OK;
}

#define STEP() do { if (event(__func__) != JAYLINK_OK) return JAYLINK_ERR; } while (0)
#define SIMPLE(name) int name(struct jaylink_device_handle *devh) \
	{ (void)devh; return event(#name); }

bool jaylink_library_has_cap(enum jaylink_capability cap)
{
	return cap == JAYLINK_CAP_HIF_USB && !getenv("FAKE_NO_USB");
}
int jaylink_init(struct jaylink_context **ctx)
{
	STEP(); *ctx = &context; return JAYLINK_OK;
}
int jaylink_exit(struct jaylink_context *ctx)
{
	(void)ctx; return event(__func__);
}
int jaylink_log_set_callback(struct jaylink_context *ctx,
		jaylink_log_callback callback, void *user_data)
{
	(void)ctx; (void)callback; (void)user_data; STEP(); return JAYLINK_OK;
}
int jaylink_discovery_scan(struct jaylink_context *ctx, uint32_t ifaces)
{
	(void)ctx;
	fprintf(stderr, "SCAN %u\n", ifaces);
	STEP(); return JAYLINK_OK;
}
int jaylink_get_devices(struct jaylink_context *ctx,
		struct jaylink_device ***devs, size_t *count)
{
	(void)ctx; STEP();
	*count = getenv("FAKE_ABSENT") ? 0 : 1;
	*devs = *count ? devices : devices + 1;
	return JAYLINK_OK;
}
void jaylink_free_devices(struct jaylink_device **devs, bool unref)
{
	(void)devs; (void)unref; (void)event(__func__);
}
int jaylink_device_get_serial_number(const struct jaylink_device *dev, uint32_t *serial)
{
	(void)dev; STEP(); *serial = 123456789U; return JAYLINK_OK;
}
int jaylink_open(struct jaylink_device *dev, struct jaylink_device_handle **devh)
{
	(void)dev; STEP(); *devh = &handle; return JAYLINK_OK;
}
SIMPLE(jaylink_close)
SIMPLE(jaylink_set_reset)
SIMPLE(jaylink_clear_reset)
SIMPLE(jaylink_jtag_set_trst)
SIMPLE(jaylink_jtag_clear_trst)
int jaylink_get_firmware_version(struct jaylink_device_handle *devh, char **version,
		size_t *length)
{
	(void)devh; STEP();
	*version = strdup("SYNTHETIC ONLY");
	if (!*version) return JAYLINK_ERR_MALLOC;
	*length = strlen(*version) + 1; return JAYLINK_OK;
}
static void capabilities(uint8_t *caps)
{
	const unsigned int features[] = {
		JAYLINK_DEV_CAP_SELECT_TIF, JAYLINK_DEV_CAP_GET_EXT_CAPS,
		JAYLINK_DEV_CAP_GET_HW_VERSION, JAYLINK_DEV_CAP_GET_FREE_MEMORY,
		JAYLINK_DEV_CAP_READ_CONFIG, JAYLINK_DEV_CAP_REGISTER, JAYLINK_DEV_CAP_GET_SPEEDS,
	};
	memset(caps, 0, JAYLINK_DEV_EXT_CAPS_SIZE);
	for (size_t i = 0; i < sizeof(features) / sizeof(features[0]); ++i)
		caps[features[i] / 8] |= (uint8_t)(1U << (features[i] % 8));
	if (getenv("FAKE_NO_SELECT"))
		caps[JAYLINK_DEV_CAP_SELECT_TIF / 8] &=
			(uint8_t)~(1U << (JAYLINK_DEV_CAP_SELECT_TIF % 8));
}
int jaylink_get_caps(struct jaylink_device_handle *devh, uint8_t *caps)
{
	uint8_t full[JAYLINK_DEV_EXT_CAPS_SIZE];
	(void)devh; STEP(); capabilities(full);
	memcpy(caps, full, JAYLINK_DEV_CAPS_SIZE); return JAYLINK_OK;
}
int jaylink_get_extended_caps(struct jaylink_device_handle *devh, uint8_t *caps)
{
	(void)devh; STEP(); capabilities(caps); return JAYLINK_OK;
}
int jaylink_get_hardware_version(struct jaylink_device_handle *devh,
		struct jaylink_hardware_version *version)
{
	(void)devh; STEP(); memset(version, 0, sizeof(*version));
	version->major = 5; return JAYLINK_OK;
}
int jaylink_get_hardware_status(struct jaylink_device_handle *devh,
		struct jaylink_hardware_status *status)
{
	(void)devh; STEP(); memset(status, 0, sizeof(*status));
	status->target_voltage = 3300; return JAYLINK_OK;
}
int jaylink_get_free_memory(struct jaylink_device_handle *devh, uint32_t *size)
{
	(void)devh; STEP(); *size = getenv("FAKE_LOW_MEMORY") ? 142 : 4096;
	return JAYLINK_OK;
}
int jaylink_read_raw_config(struct jaylink_device_handle *devh, uint8_t *config)
{
	(void)devh; STEP(); memset(config, 0, JAYLINK_DEV_CONFIG_SIZE); return JAYLINK_OK;
}
int jaylink_get_available_interfaces(struct jaylink_device_handle *devh, uint32_t *ifaces)
{
	(void)devh; STEP();
	*ifaces = (1U << JAYLINK_TIF_JTAG) |
		(getenv("FAKE_NO_SWD") ? 0U : (1U << JAYLINK_TIF_SWD));
	return JAYLINK_OK;
}
int jaylink_select_interface(struct jaylink_device_handle *devh,
		enum jaylink_target_interface iface, enum jaylink_target_interface *previous)
{
	(void)devh; STEP(); fprintf(stderr, "INTERFACE %u\n", iface);
	if (previous) *previous = JAYLINK_TIF_JTAG;
	return JAYLINK_OK;
}
int jaylink_register(struct jaylink_device_handle *devh,
		struct jaylink_connection *connection, struct jaylink_connection *connections,
		size_t *count)
{
	(void)devh; STEP(); connection->handle = 1; connections[0] = *connection;
	*count = getenv("FAKE_REG_FULL") ? 0 : 1; return JAYLINK_OK;
}
int jaylink_unregister(struct jaylink_device_handle *devh,
		const struct jaylink_connection *connection, struct jaylink_connection *connections,
		size_t *count)
{
	(void)devh; (void)connection; (void)connections; STEP(); *count = 0;
	return JAYLINK_OK;
}
int jaylink_get_speeds(struct jaylink_device_handle *devh, struct jaylink_speed *speed)
{
	(void)devh; STEP(); speed->freq = 12000000; speed->div = 1; return JAYLINK_OK;
}
int jaylink_set_speed(struct jaylink_device_handle *devh, uint16_t speed)
{
	(void)devh; STEP(); fprintf(stderr, "SPEED %u\n", speed); return JAYLINK_OK;
}
int jaylink_jtag_io(struct jaylink_device_handle *devh, const uint8_t *tms,
		const uint8_t *tdi, uint8_t *tdo, uint16_t length, enum jaylink_jtag_version version)
{
	(void)devh; (void)tms; (void)tdi; (void)version; STEP();
	memset(tdo, 0, (length + 7U) / 8U);
	fprintf(stderr, "JTAG_BITS %u\n", length); return JAYLINK_OK;
}
int jaylink_swd_io(struct jaylink_device_handle *devh, const uint8_t *direction,
		const uint8_t *out, uint8_t *in, uint16_t length)
{
	(void)devh; (void)direction; (void)out; STEP();
	memset(in, 0, (length + 7U) / 8U);
	fprintf(stderr, "SWD_BITS %u\n", length); return JAYLINK_OK;
}
