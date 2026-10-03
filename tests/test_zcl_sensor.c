/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zcl_sensor.h"

/* The application ZCL/Bind server against its real device overlay. The
 * runtime publication and the caller's queueing are covered by
 * test_bdb_join.c and test_join_smoke.c; values are SYNTHETIC. */

MCU_XDATA bdb_join_t join_smoke_device;

#define CTX join_smoke_device.work.runtime.zdo
#define ST ZCL_SENSOR_STATE
#define CHECK(x) do { checks++; if (!(x)) { \
    fprintf(stderr, "zcl_sensor: line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static const uint8_t own[8] = {1, 2, 3, 4, 5, 6, 7, 8};
static const uint8_t tc[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
static const char model[] = BOARD_MODEL, manufacturer[] = BOARD_MANUFACTURER;
static unsigned long checks;

static void reset(uint32_t now)
{
    memset(&join_smoke_device, 0, sizeof(join_smoke_device));
    join_smoke_device.config.transport.profile = 0x0104;
    join_smoke_device.config.transport.endpoint = 1;
    memcpy(join_smoke_device.config.association.extraction.local, own, 8);
    memcpy(join_smoke_device.record.association.source_ieee, tc, 8);
    CTX.last = now;
    zcl_sensor_init();
    CHECK(!memcmp(ST.tc_ieee, tc, 8) && ST.basic && ST.bound[0] == 1 && ST.bound[1] == 1 && ST.bound[2] == 1);
}

static uint8_t receive(uint16_t cluster, const void *payload, uint8_t length)
{
    uint8_t result;
    memset(&CTX.application, 0, sizeof(CTX.application));
    CTX.application.nwk.source = 0x0000; CTX.application.nwk.destination = 0x4321;
    CTX.application.aps.profile_id = 0x0104; CTX.application.aps.cluster_id = cluster;
    CTX.application.aps.source_endpoint = 9; CTX.application.aps.destination_endpoint = 1;
    memcpy(CTX.application.payload, payload, length); CTX.application.length = length;
    CTX.application_ready = 1;
    result = zcl_sensor_serve();
    CTX.application_ready = 0;
    memset(&CTX.application, 0, sizeof(CTX.application));
    return result;
}

static uint8_t zdo(uint16_t cluster, const void *payload, uint8_t length)
{
    uint8_t result;
    memset(&CTX.application, 0, sizeof(CTX.application));
    CTX.application.nwk.source = 0x0000; CTX.application.nwk.destination = 0x4321;
    CTX.application.aps.cluster_id = cluster;
    memcpy(CTX.application.payload, payload, length); CTX.application.length = length;
    CTX.application_ready = 1;
    result = zcl_sensor_serve();
    CTX.application_ready = 0;
    return result;
}

static void reply(uint16_t cluster, const void *expected, uint8_t length)
{
    CHECK(CTX.response_pending && CTX.response.length == length);
    CHECK(!memcmp(CTX.response.payload, expected, length));
    CHECK(CTX.response.aps.cluster_id == cluster && !CTX.response.nwk.destination);
    CHECK(CTX.response.nwk.version == 2 && CTX.response.nwk.radius == 30 && !CTX.response.aps.flags);
    CHECK(CTX.response.nwk.discover_route == NWK_DISCOVER_ROUTE_ENABLE);
    if (cluster & 0x8000u) CHECK(!CTX.response.aps.profile_id && !CTX.response.aps.destination_endpoint);
    else CHECK(CTX.response.aps.profile_id == 0x0104 && CTX.response.aps.source_endpoint == 1 &&
               CTX.response.aps.destination_endpoint == 9);
    CTX.response_pending = 0;
}

static void zcl(uint16_t cluster, const void *request, uint8_t length, const void *expected, uint8_t reply_length)
{
    CHECK(receive(cluster, request, length) == ZCL_SENSOR_HANDLED);
    if (reply_length) reply(cluster, expected, reply_length);
    else CHECK(!CTX.response_pending);
}

static void discarded(uint16_t cluster, const void *request, uint8_t length)
{
    CHECK(receive(cluster, request, length) == ZCL_SENSOR_DISCARDED && !CTX.response_pending);
}

/* Returns the next report's attribute and value, committing it as accepted. */
static uint8_t report(uint16_t cluster, uint16_t attribute, uint8_t type, uint16_t value)
{
    uint8_t width = type == 0x20u ? 1u : 2u;
    if (zcl_sensor_serve() != ZCL_SENSOR_REPORT) return 0;
    CHECK(CTX.application.aps.cluster_id == cluster && CTX.application.aps.profile_id == 0x0104);
    CHECK(CTX.application.aps.flags == APS_FLAG_ACK_REQUEST && !CTX.application.nwk.destination);
    CHECK(CTX.application.aps.source_endpoint == 1 && CTX.application.aps.destination_endpoint ==
          (cluster ? ST.bound[cluster == 0x0402u ? 0 : cluster == 0x0405u ? 1 : 2] : 1));
    CHECK(CTX.application.length == 6u+width && CTX.application.payload[0] == 0x18);
    CHECK(CTX.application.payload[1] == ST.tsn && CTX.application.payload[2] == 0x0a);
    CHECK(CTX.application.payload[3] == (uint8_t)attribute && CTX.application.payload[4] == attribute >> 8);
    CHECK(CTX.application.payload[5] == type && CTX.application.payload[6] == (uint8_t)value);
    if (width == 2u) CHECK(CTX.application.payload[7] == value >> 8);
    memset(&CTX.application, 0, sizeof(CTX.application));
    ST.sent = 1;
    return 1;
}

static void idle(void)
{
    CHECK(zcl_sensor_serve() == ZCL_SENSOR_IDLE && !CTX.application.length);
}

static void values(void)
{
    uint8_t s;
    CHECK(ZCL_SENSOR_SAMPLE(0) == 0 && ZCL_SENSOR_SAMPLE((1ul << 22)-1) == 0);
    CHECK(ZCL_SENSOR_SAMPLE(1ul << 22) == 1 && ZCL_SENSOR_SAMPLE(0x3ffffffful) == 255);
    CHECK(ZCL_SENSOR_SAMPLE(0x40000000ul) == 0 && ZCL_SENSOR_SAMPLE(0xfffffffful) == 255);
    for (s = 0; s < 255; s++) {
        uint8_t p = ZCL_SENSOR_PHASE(s);
        CHECK(p == s % 41);
        CHECK(ZCL_SENSOR_TEMPERATURE(p) == 2100 + 10*p && ZCL_SENSOR_HUMIDITY(p) == 4500 + 25*p);
        CHECK(ZCL_SENSOR_PERCENTAGE(p) == 200 - p && ZCL_SENSOR_VOLTAGE(p) == 30 - p/14);
    }
    CHECK(ZCL_SENSOR_TEMPERATURE(40) == 2500 && ZCL_SENSOR_HUMIDITY(40) == 5500);
    CHECK(ZCL_SENSOR_PERCENTAGE(40) == 160 && ZCL_SENSOR_VOLTAGE(40) == 28);
}

static void reads(void)
{
    static const uint8_t basic[] = {0x00, 0x31, 0x00, 0x04, 0x00, 0x05, 0x00, 0x00, 0x00, 0x07, 0x00,
        0xfd, 0xff, 0x34, 0x12};
    static const uint8_t sensor[] = {0x00, 0x32, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0xfd, 0xff, 0x03, 0x00};
    static const uint8_t power[] = {0x00, 0x33, 0x00, 0x20, 0x00, 0x21, 0x00, 0xfd, 0xff, 0x00, 0x00};
    static const uint8_t odd[] = {0x10, 0x34, 0x00, 0x05, 0x00, 0x04};
    uint8_t expected[ED_PAYLOAD_MAX], request[ED_PAYLOAD_MAX], at = 3, i;
    reset(5ul << 22);
    expected[0] = 0x18; expected[1] = 0x31; expected[2] = 0x01;
    expected[at++] = 4; expected[at++] = 0; expected[at++] = 0; expected[at++] = 0x42;
    expected[at++] = sizeof(manufacturer)-1; memcpy(expected+at, manufacturer, sizeof(manufacturer)-1);
    at += sizeof(manufacturer)-1;
    expected[at++] = 5; expected[at++] = 0; expected[at++] = 0; expected[at++] = 0x42;
    expected[at++] = sizeof(model)-1; memcpy(expected+at, model, sizeof(model)-1); at += sizeof(model)-1;
    memcpy(expected+at, "\x00\x00\x00\x20\x08\x07\x00\x00\x30\x03\xfd\xff\x00\x21\x03\x00\x34\x12\x86", 19);
    zcl(0x0000, basic, sizeof(basic), expected, (uint8_t)(at+19));
    zcl(0x0402, sensor, sizeof(sensor),
        "\x18\x32\x01\x00\x00\x00\x29\x66\x08\x01\x00\x00\x29\x34\x08\x02\x00\x00\x29\xc4\x09"
        "\xfd\xff\x00\x21\x03\x00\x03\x00\x86", 30);
    memcpy(expected, "\x18\x32\x01\x00\x00\x00\x21\x00\x00\x01\x00\x00\x21\x94\x11\x02\x00\x00\x21\x7c\x15"
        "\xfd\xff\x00\x21\x02\x00\x03\x00\x86", 30);
    expected[7] = (uint8_t)(4500+25*5); expected[8] = (uint8_t)((4500+25*5) >> 8);
    zcl(0x0405, sensor, sizeof(sensor), expected, 30);
    zcl(0x0001, power, sizeof(power),
        "\x18\x33\x01\x20\x00\x00\x20\x1e\x21\x00\x00\x20\xc3\xfd\xff\x00\x21\x02\x00\x00\x00\x86", 22);
    zcl(0x0003, "\x00\x35\x00\x00\x00\xfd\xff", 7, "\x18\x35\x01\x00\x00\x00\x21\x00\x00\xfd\xff\x00\x21\x02\x00", 15);
    /* Disable-default-response does not suppress a Read Attributes Response;
     * a trailing odd octet is ignored. */
    memcpy(expected, "\x18\x34\x01\x05\x00\x00\x42", 7); expected[7] = sizeof(model)-1;
    memcpy(expected+8, model, sizeof(model)-1);
    zcl(0x0000, odd, sizeof(odd), expected, (uint8_t)(8+sizeof(model)-1));
    /* Manufacturer-specific frame header: echoed code and TSN. */
    zcl(0x0000, "\x04\x34\x12\x36\x00\x04\x00", 7, "\x1c\x34\x12\x36\x0b\x00\x81", 7);
    /* Records that would overflow one APS payload are omitted, not split. */
    request[0] = 0; request[1] = 0x3c; request[2] = 0;
    for (i = 3; i+1u < ED_PAYLOAD_MAX; i += 2) { request[i] = 7; request[i+1] = 0x10; }
    memcpy(expected, "\x18\x3c\x01", 3); at = 3;
    while (at+3u <= ED_PAYLOAD_MAX) { expected[at++] = 7; expected[at++] = 0x10; expected[at++] = 0x86; }
    zcl(0x0000, request, ED_PAYLOAD_MAX-1, expected, at);
    for (i = 3; i+1u < ED_PAYLOAD_MAX; i += 2) { request[i] = 5; request[i+1] = 0; }
    at = 3;
    while (at+5u+sizeof(model)-1u <= ED_PAYLOAD_MAX) {
        expected[at++] = 5; expected[at++] = 0; expected[at++] = 0; expected[at++] = 0x42;
        expected[at++] = sizeof(model)-1; memcpy(expected+at, model, sizeof(model)-1); at += sizeof(model)-1;
    }
    zcl(0x0000, request, ED_PAYLOAD_MAX-1, expected, at);
}

static void defaults_and_discards(void)
{
    uint8_t length;
    reset(0);
    /* Unsupported cluster, server-to-client direction, general commands. */
    zcl(0x0006, "\x00\x40\x00\x00\x00", 5, "\x18\x40\x0b\x00\xc3", 5);
    zcl(0x0402, "\x08\x41\x00\x00\x00", 5, "\x18\x41\x0b\x00\xc3", 5);
    zcl(0x0402, "\x00\x42\x08\x00\x00\x00", 6, "\x18\x42\x0b\x08\x81", 5);
    zcl(0x0402, "\x00\x43\x0c\x00\x00\x05", 6, "\x18\x43\x0b\x0c\x81", 5);
    zcl(0x0402, "\x01\x44\x00", 3, "\x18\x44\x0b\x00\x81", 5);
    zcl(0x0003, "\x01\x45\x40\x00\x00", 5, "\x18\x45\x0b\x40\x81", 5);
    zcl(0x0003, "\x01\x46\x00\x05", 4, "\x18\x46\x0b\x00\x80", 5);
    /* Errors are sent even with the disable-default-response bit. */
    zcl(0x0402, "\x10\x47\x08\x00\x00\x00", 6, "\x18\x47\x0b\x08\x81", 5);
    /* Silent: wrong profile/endpoint, empty, reserved type, short header, a
     * received Default Response, broadcast and group delivery. */
    CHECK(receive(0x0402, "\x02\x48\x00", 3) == ZCL_SENSOR_DISCARDED);
    discarded(0x0402, "", 0);
    discarded(0x0402, "\x00\x49", 2);
    discarded(0x0402, "\x04\x49\x12\x34", 4);
    discarded(0x0402, "\x00\x4a\x0b\x00\x00", 5);
    discarded(0x0402, "\x18\x4a\x0b\x00\x00", 5);
    for (length = 0; length < 4; length++) {
        memset(&CTX.application, 0, sizeof(CTX.application));
        CTX.application.aps.profile_id = length == 0 ? 0x0105 : 0x0104;
        CTX.application.aps.cluster_id = 0x0402;
        CTX.application.aps.source_endpoint = 9;
        CTX.application.aps.destination_endpoint = length == 1 ? 2 : 1;
        CTX.application.nwk.destination = length == 2 ? 0xfffd : 0x4321;
        CTX.application.aps.delivery_mode = length == 3 ? APS_DELIVERY_GROUP : 0;
        memcpy(CTX.application.payload, "\x00\x4b\x00\x00\x00", 5); CTX.application.length = 5;
        CTX.application_ready = 1;
        CHECK(zcl_sensor_serve() == ZCL_SENSOR_DISCARDED && !CTX.response_pending);
        CTX.application_ready = 0;
    }
    /* A busy response slot drops the request; the slot is untouched. */
    CTX.response_pending = 1; CTX.response.length = 77;
    CHECK(receive(0x0402, "\x00\x4c\x00\x00\x00", 5) == ZCL_SENSOR_DISCARDED);
    CHECK(CTX.response_result == NWK_APS_FULL && CTX.response.length == 77);
    CTX.response_pending = 0;
    /* ZDO frames other than Bind/Unbind are not the application's. */
    memset(&CTX.application, 0, sizeof(CTX.application));
    CTX.application.aps.cluster_id = 0x0005; CTX.application.length = 3; CTX.application_ready = 1;
    CHECK(zcl_sensor_serve() == ZCL_SENSOR_DISCARDED && !CTX.response_pending);
    CTX.application_ready = 0;
}

static void identify(void)
{
    reset(0);
    zcl(0x0003, "\x01\x50\x01", 3, NULL, 0);
    zcl(0x0003, "\x00\x51\x02\x00\x00\x21\x0a\x00", 8, "\x18\x51\x04\x00", 4);
    CHECK(ST.identify == 10);
    zcl(0x0003, "\x01\x52\x01", 3, "\x19\x52\x00\x0a\x00", 5);
    CTX.last = 3*ZCL_SENSOR_SECOND+5;
    zcl(0x0003, "\x01\x53\x01", 3, "\x19\x53\x00\x07\x00", 5);
    zcl(0x0003, "\x01\x54\x00\x02\x00", 5, "\x18\x54\x0b\x00\x00", 5);
    CHECK(ST.identify == 2);
    zcl(0x0003, "\x11\x55\x00\x00\x00", 5, NULL, 0);
    CHECK(!ST.identify);
    zcl(0x0003, "\x01\x56\x01", 3, NULL, 0);
    CTX.last += 100ul*ZCL_SENSOR_SECOND;
    zcl(0x0003, "\x01\x57\x01", 3, NULL, 0);
    CHECK(!ST.identify && ST.report[0].age == 103);
    /* Write: unsupported, wrong type, read-only; malformed and validated
     * before any record is applied. */
    zcl(0x0003, "\x00\x58\x02\x00\x00\x21\x05\x00\x09\x00\x21\x01\x00\x00\x00\x20\x01", 17,
        "\x18\x58\x04\x86\x09\x00\x8d\x00\x00", 9);
    CHECK(ST.identify == 5);
    zcl(0x0402, "\x00\x59\x02\x00\x00\x29\x01\x00", 8, "\x18\x59\x04\x88\x00\x00", 6);
    zcl(0x0000, "\x00\x5a\x02\x05\x00\x42\x01\x41", 8, "\x18\x5a\x04\x88\x05\x00", 6);
    zcl(0x0003, "\x00\x5b\x02\x00\x00\x21\x09\x00\x00\x00\x21\x01", 12, "\x18\x5b\x0b\x02\x80", 5);
    zcl(0x0003, "\x00\x5c\x02\x00\x00\x48\x00", 7, "\x18\x5c\x0b\x02\x80", 5);
    CHECK(ST.identify == 5);
    zcl(0x0003, "\x10\x5d\x02\x00\x00\x21\x00\x00", 8, "\x18\x5d\x04\x00", 4);
    CHECK(!ST.identify);
}

static void configure(void)
{
    static const uint16_t defaults[4][3] = {{30, 300, 10}, {30, 300, 25}, {60, 3600, 1}, {60, 3600, 2}};
    uint8_t i;
    reset(0);
    for (i = 0; i < 4; i++)
        CHECK(ST.report[i].min == defaults[i][0] && ST.report[i].max == defaults[i][1] &&
              ST.report[i].change == defaults[i][2] && !ST.report[i].age);
    /* The ZHA temperature, humidity and battery configurations. */
    zcl(0x0402, "\x00\x60\x06\x00\x00\x00\x29\x1e\x00\x84\x03\x32\x00", 13, "\x18\x60\x07\x00", 4);
    zcl(0x0405, "\x00\x61\x06\x00\x00\x00\x21\x1e\x00\x84\x03\x64\x00", 13, "\x18\x61\x07\x00", 4);
    zcl(0x0001, "\x00\x62\x06\x00\x21\x00\x20\x10\x0e\x30\x2a\x01", 12, "\x18\x62\x07\x00", 4);
    CHECK(ST.report[0].min == 30 && ST.report[0].max == 900 && ST.report[0].change == 50);
    CHECK(ST.report[1].min == 30 && ST.report[1].max == 900 && ST.report[1].change == 100);
    CHECK(ST.report[3].min == 3600 && ST.report[3].max == 10800 && ST.report[3].change == 1);
    /* Per-record failures: wrong type, max below min, not reportable,
     * unsupported attribute, received direction; the valid record applies. */
    zcl(0x0402, "\x00\x63\x06\x00\x00\x00\x21\x01\x00\x02\x00\x01\x00"
        "\x00\x00\x00\x29\x05\x00\x04\x00\x01\x00"
        "\x00\x01\x00\x29\x01\x00\x02\x00\x01\x00"
        "\x00\x09\x00\x29\x01\x00\x02\x00\x01\x00"
        "\x01\x00\x00\x0a\x00"
        "\x00\x00\x00\x29\x00\x00\x00\x00\x07\x00", 58,
        "\x18\x63\x07\x8d\x00\x00\x00\x87\x00\x00\x00\x8c\x00\x01\x00\x86\x00\x09\x00\x8c\x01\x00\x00", 23);
    CHECK(!ST.report[0].min && !ST.report[0].max && ST.report[0].change == 7);
    /* Malformed: nothing applies. */
    zcl(0x0402, "\x00\x64\x06\x00\x00\x00\x29\x01\x00\x02\x00\x03\x00\x02\x00\x00", 16,
        "\x18\x64\x0b\x06\x80", 5);
    zcl(0x0402, "\x00\x65\x06\x00\x00\x00\x29\x01\x00\x02\x00\x03", 12, "\x18\x65\x0b\x06\x80", 5);
    zcl(0x0402, "\x00\x66\x06\x00\x00\x00", 6, "\x18\x66\x0b\x06\x80", 5);
    CHECK(!ST.report[0].min && !ST.report[0].max && ST.report[0].change == 7);
    /* A non-integer reportable type has no parsed change field. */
    zcl(0x0001, "\x00\x67\x06\x00\x20\x00\x30\x01\x00\x02\x00", 11, "\x18\x67\x07\x8d\x00\x20\x00", 7);
    /* Restore defaults and disable. */
    zcl(0x0402, "\x10\x68\x06\x00\x00\x00\x29\xff\xff\x00\x00\x00\x00", 13, "\x18\x68\x07\x00", 4);
    CHECK(ST.report[0].min == 30 && ST.report[0].max == 300 && ST.report[0].change == 10);
    zcl(0x0405, "\x00\x69\x06\x00\x00\x00\x21\x00\x00\xff\xff\x00\x00", 13, "\x18\x69\x07\x00", 4);
    CHECK(ST.report[1].max == 0xffff);
}

static void bind(void)
{
    uint8_t request[22];
    reset(0);
    request[0] = 0x70; memcpy(request+1, own, 8); request[9] = 1; request[10] = 0x02; request[11] = 0x04;
    request[12] = 3; memcpy(request+13, tc, 8); request[21] = 5;
    /* Bound to endpoint 1 by init: another endpoint is TABLE_FULL. */
    CHECK(zdo(0x0021, request, 22) == ZCL_SENSOR_HANDLED); reply(0x8021, "\x70\x8c", 2);
    CHECK(zdo(0x0022, request, 22) == ZCL_SENSOR_HANDLED); reply(0x8022, "\x70\x88", 2);
    request[21] = 1;
    CHECK(zdo(0x0021, request, 22) == ZCL_SENSOR_HANDLED); reply(0x8021, "\x70\x00", 2);
    CHECK(zdo(0x0022, request, 22) == ZCL_SENSOR_HANDLED); reply(0x8022, "\x70\x00", 2);
    CHECK(!ST.bound[0] && ST.bound[1] == 1);
    CHECK(zdo(0x0022, request, 22) == ZCL_SENSOR_HANDLED); reply(0x8022, "\x70\x88", 2);
    request[21] = 5;
    CHECK(zdo(0x0021, request, 22) == ZCL_SENSOR_HANDLED); reply(0x8021, "\x70\x00", 2);
    CHECK(ST.bound[0] == 5);
    /* Not the coordinator, unknown cluster, source endpoint, invalid
     * destination endpoint, foreign source and group destination. */
    request[13] ^= 1;
    CHECK(zdo(0x0021, request, 22) == ZCL_SENSOR_HANDLED); reply(0x8021, "\x70\x8c", 2);
    request[13] ^= 1; request[10] = 0x06;
    CHECK(zdo(0x0021, request, 22) == ZCL_SENSOR_HANDLED); reply(0x8021, "\x70\x8c", 2);
    request[10] = 0x01; request[9] = 2;
    CHECK(zdo(0x0021, request, 22) == ZCL_SENSOR_HANDLED); reply(0x8021, "\x70\x82", 2);
    request[9] = 1; request[21] = 0xff;
    CHECK(zdo(0x0021, request, 22) == ZCL_SENSOR_HANDLED); reply(0x8021, "\x70\x82", 2);
    request[21] = 1; request[1] ^= 1;
    CHECK(zdo(0x0021, request, 22) == ZCL_SENSOR_HANDLED); reply(0x8021, "\x70\x84", 2);
    request[1] ^= 1; request[12] = 1;
    CHECK(zdo(0x0021, request, 15) == ZCL_SENSOR_HANDLED); reply(0x8021, "\x70\x84", 2);
    CHECK(ST.bound[0] == 5 && ST.bound[1] == 1 && ST.bound[2] == 1);
    /* Truncated requests are dropped. */
    CHECK(zdo(0x0021, request, 14) == ZCL_SENSOR_DISCARDED && !CTX.response_pending);
    request[12] = 3;
    CHECK(zdo(0x0021, request, 21) == ZCL_SENSOR_DISCARDED && !CTX.response_pending);
    CHECK(zdo(0x0021, request, 12) == ZCL_SENSOR_DISCARDED && !CTX.response_pending);
}

static void reports(void)
{
    uint32_t t;
    reset(0);
    /* Basic ModelIdentifier first, rebuilt until accepted. */
    CHECK(zcl_sensor_serve() == ZCL_SENSOR_REPORT);
    CHECK(CTX.application.aps.cluster_id == 0 && CTX.application.aps.destination_endpoint == 1);
    CHECK(CTX.application.length == 7u+sizeof(model)-1u && CTX.application.payload[1] == 0);
    CHECK(!memcmp(CTX.application.payload+2, "\x0a\x05\x00\x42", 4) &&
          CTX.application.payload[6] == sizeof(model)-1 &&
          !memcmp(CTX.application.payload+7, model, sizeof(model)-1));
    /* A rebuild keeps its sequence number; acceptance advances it. */
    CHECK(zcl_sensor_serve() == ZCL_SENSOR_REPORT && CTX.application.payload[1] == 0 && ST.basic);
    ST.sent = 1; memset(&CTX.application, 0, sizeof(CTX.application));
    idle(); CHECK(!ST.basic && !ST.sent && ST.tsn == 1);
    /* An outstanding application TX or a pending confirmation blocks. */
    CTX.last = 1ul << 22;
    join_smoke_device.application_pending = 1; idle();
    join_smoke_device.application_pending = 0; join_smoke_device.application_done = 1; idle();
    join_smoke_device.application_done = 0;
    /* Phase 1 at 67 s: temperature +10, humidity +25; battery 0.5 % below
     * its change of 2 and voltage unchanged. */
    CHECK(report(0x0402, 0, 0x29, 2110));
    CHECK(report(0x0405, 0, 0x21, 4525));
    idle();
    CHECK(ST.report[0].last == 2110 && !ST.report[0].age && ST.report[1].last == 4525);
    /* Not committed until accepted: unaccepted builds repeat. */
    CTX.last = 2ul << 22;
    CHECK(zcl_sensor_serve() == ZCL_SENSOR_REPORT && CTX.application.aps.cluster_id == 0x0402);
    CHECK(CTX.application.payload[1] == 3 && ST.tsn == 3);
    memset(&CTX.application, 0, sizeof(CTX.application));
    CHECK(report(0x0402, 0, 0x29, 2120) && ST.tsn == 3);
    CHECK(report(0x0405, 0, 0x21, 4550) && ST.tsn == 4);
    CHECK(report(0x0001, 0x21, 0x20, 198));
    idle();
    /* Unbound clusters are silent; max interval reports without change. */
    ST.bound[0] = 0;
    CTX.last = 3ul << 22;
    CHECK(report(0x0405, 0, 0x21, 4575));
    idle();
    ST.bound[1] = 0;
    for (t = CTX.last; ST.report[2].age < 3600; ) {
        t += ZCL_SENSOR_SECOND*60; if (ZCL_SENSOR_PHASE(ZCL_SENSOR_SAMPLE(t)) > 13) break;
        CTX.last = t;
        while (zcl_sensor_serve() == ZCL_SENSOR_REPORT) {
            CHECK(CTX.application.aps.cluster_id == 0x0001 && CTX.application.payload[3] == 0x21);
            memset(&CTX.application, 0, sizeof(CTX.application)); ST.sent = 1;
        }
    }
    CHECK(ZCL_SENSOR_VOLTAGE(ZCL_SENSOR_PHASE(ZCL_SENSOR_SAMPLE(CTX.last))) == 30);
    ST.report[2].age = 3599; ST.report[3].max = 0xffff;
    CTX.last += ZCL_SENSOR_SECOND;
    CHECK(report(0x0001, 0x20, 0x20, 30));
    idle();
    /* Change-only (max 0) and min gating. */
    ST.bound[0] = 1; ST.bound[2] = 0;
    zcl(0x0402, "\x00\x70\x06\x00\x00\x00\x29\x05\x00\x00\x00\x01\x00", 13, "\x18\x70\x07\x00", 4);
    ST.report[0].age = 0; ST.report[0].last = 0;
    CTX.last += 4*ZCL_SENSOR_SECOND; idle();
    CTX.last += ZCL_SENSOR_SECOND;
    CHECK(report(0x0402, 0, 0x29, ZCL_SENSOR_TEMPERATURE(ZCL_SENSOR_PHASE(ZCL_SENSOR_SAMPLE(CTX.last)))));
    t = CTX.last | ((1ul << 22)-1u);
    CTX.last = t; idle();
    CTX.last = t+1u;
    CHECK(report(0x0402, 0, 0x29, ZCL_SENSOR_TEMPERATURE(ZCL_SENSOR_PHASE(ZCL_SENSOR_SAMPLE(CTX.last)))));
    idle();
    /* A served request takes the poll; the report waits. */
    ST.report[0].last = 0; CTX.last += 5*ZCL_SENSOR_SECOND;
    zcl(0x0003, "\x01\x71\x01", 3, NULL, 0);
    CHECK(report(0x0402, 0, 0x29, ZCL_SENSOR_TEMPERATURE(ZCL_SENSOR_PHASE(ZCL_SENSOR_SAMPLE(CTX.last)))));
    /* Ages saturate. */
    idle(); CHECK(!ST.report[0].age);
    ST.report[0].age = 0xfffe; ST.report[0].max = 0xffff;
    CTX.last += 5*ZCL_SENSOR_SECOND; zcl_sensor_serve();
    CHECK(ST.report[0].age == 0xffff);
}

int main(void)
{
    values(); reads(); defaults_and_discards(); identify(); configure(); bind(); reports();
    printf("ZCL sensor: %lu checks PASS; SYNTHETIC values, Read/Write/Configure Reporting, "
           "Identify, Bind/Unbind and report scheduling (host only).\n", checks);
    return 0;
}
