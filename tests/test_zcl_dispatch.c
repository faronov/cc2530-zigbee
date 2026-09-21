/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "zcl_dispatch.h"
#include "cc2530_mmio.h"

#include <stddef.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) return (uint16_t)__LINE__; } while (0)

static const MCU_CODE uint8_t scalar[] = {0x78, 0x56};
static const MCU_CODE zcl_attribute_t code_attributes[] = {
    {0xfffe, 1, {ZCL_TYPE_UINT16, 0, scalar, 2}},
    {0x1234, 0, {ZCL_TYPE_UINT8, 0xff, NULL, 65535}},
    {0x0000, 1, {ZCL_TYPE_NO_DATA, 0, NULL, 0}},
    {0x4000, 1, {ZCL_TYPE_CHARACTER_STRING, 1, NULL, 0}}
};
static const MCU_CODE zcl_attribute_set_t code_set = {code_attributes, 4, 0, 0, 0x5678};
static const MCU_CODE uint8_t code_request[] = {0, 0x5a, 0x0c, 0, 0, 0xff};
static const MCU_CODE uint8_t golden[] = {
    0x18, 0x5a, 0x0d, 1, 0, 0, 0, 0x34, 0x12, 0x20,
    0, 0x40, 0x42, 0xfe, 0xff, 0x21
};
static zcl_attribute_t attributes[ZCL_ATTRIBUTE_MAX_COUNT];
static zcl_attribute_set_t set;
static zcl_dispatch_info_t info;
static uint8_t request[102], response[102];

static uint8_t filled(const uint8_t *bytes, uint16_t size, uint8_t value)
{
    while (size--)
        if (*bytes++ != value)
            return 0;
    return 1;
}

static void base(void)
{
    memset(attributes, 0, sizeof(attributes));
    memcpy(attributes, code_attributes, sizeof(code_attributes));
    set = code_set;
    set.attributes = attributes;
    memset(request, 0xee, sizeof(request));
    memcpy(request, code_request, sizeof(code_request));
    memset(response, 0xc7, sizeof(response));
    memset(&info, 0xa5, sizeof(info));
}

static uint16_t failure(uint16_t length, uint16_t capacity, zcl_codec_result_t expected)
{
    memset(response, 0xc7, sizeof(response));
    memset(&info, 0xa5, sizeof(info));
    CHECK(zcl_dispatch_unicast(&set, request, length, response + 1, capacity, &info) == expected);
    CHECK(filled(response, sizeof(response), 0xc7));
    CHECK(filled((const uint8_t *)&info, sizeof(info), 0xa5));
    return 0;
}

static uint16_t golden_cases(void)
{
    volatile uint8_t manufacturer, side, disable, offset;
    base();
    CHECK(zcl_dispatch_unicast(&code_set, code_request, sizeof(code_request), response + 1, 100, &info) == ZCL_CODEC_OK);
    CHECK(info.kind == ZCL_DISPATCH_RESPONSE && info.sequence == 0x5a && info.length == sizeof(golden));
    CHECK(info.command_id == 0x0d && info.requested_count == 255 && info.returned_count == 4 && info.discovery_complete == 1);
    CHECK(info.default_command == 0 && info.default_status == 0 && info.default_raw_status == 0);
    CHECK(memcmp(response + 1, golden, sizeof(golden)) == 0 && response[0] == 0xc7);
    CHECK(filled(response + 1 + info.length, 101u - info.length, 0xc7));
    for (manufacturer = 0; manufacturer < 2; manufacturer++) {
        for (side = 0; side < 2; side++) {
            for (disable = 0; disable < 2; disable++) {
                base();
                set.manufacturer_specific = manufacturer;
                set.side = side;
                offset = manufacturer ? 5 : 3;
                request[0] = (uint8_t)((manufacturer ? 4u : 0xe0u) | (side ? 8u : 0u) | (disable ? 16u : 0u));
                request[1] = 0x78;
                request[2] = 0x56;
                request[offset - 2u] = 0x5a;
                request[offset - 1u] = 0x0c;
                request[offset] = request[offset + 1u] = 0;
                request[offset + 2u] = 0xff;
                CHECK(zcl_dispatch_unicast(&set, request, (uint16_t)(offset + 3u), response + 1, 100, &info) == ZCL_CODEC_OK);
                CHECK(info.length == offset + 13u && info.returned_count == 4 && info.discovery_complete == 1);
                CHECK(response[1] == (uint8_t)(0x10u | (side ? 0u : 8u) | (manufacturer ? 4u : 0u)));
                if (manufacturer)
                    CHECK(response[2] == 0x78 && response[3] == 0x56);
                CHECK(response[offset - 1u] == 0x5a && response[offset] == 0x0d);
                CHECK(memcmp(response + 1 + offset, golden + 3, 13) == 0);
                CHECK(response[0] == 0xc7 && filled(response + 1 + info.length, 101u - info.length, 0xc7));
            }
        }
    }
    base();
    request[5] = 2;
    CHECK(zcl_dispatch_unicast(&set, request, 6, response, 100, &info) == ZCL_CODEC_OK);
    CHECK(info.length == 10 && info.returned_count == 2 && info.discovery_complete == 0 && response[3] == 0);
    CHECK(memcmp(response + 4, golden + 4, 6) == 0);
    request[3] = 0x35;
    request[4] = 0x12;
    CHECK(zcl_dispatch_unicast(&set, request, 6, response, 100, &info) == ZCL_CODEC_OK);
    CHECK(info.length == 10 && info.returned_count == 2 && info.discovery_complete == 1);
    CHECK(memcmp(response + 4, golden + 10, 6) == 0);
    request[3] = 0xfe;
    request[4] = 0xff;
    CHECK(zcl_dispatch_unicast(&set, request, 6, response, 100, &info) == ZCL_CODEC_OK);
    CHECK(info.length == 7 && info.returned_count == 1 && info.discovery_complete == 1);
    CHECK(response[4] == 0xfe && response[5] == 0xff && response[6] == 0x21);
    request[5] = 0;
    CHECK(zcl_dispatch_unicast(&set, request, 6, response, 4, &info) == ZCL_CODEC_OK);
    CHECK(info.length == 4 && info.returned_count == 0 && info.discovery_complete == 0);
    set.count = 0;
    set.attributes = NULL;
    CHECK(zcl_dispatch_unicast(&set, request, 6, response, 4, &info) == ZCL_CODEC_OK);
    CHECK(info.length == 4 && info.discovery_complete == 1 && response[3] == 1);
    return 0;
}

static uint16_t capacity_cases(void)
{
    volatile uint8_t manufacturer, offset, capacity, n, expected;
    for (manufacturer = 0; manufacturer < 2; manufacturer++) {
        base();
        set.manufacturer_specific = manufacturer;
        offset = manufacturer ? 5 : 3;
        request[0] = manufacturer ? 4 : 0;
        request[1] = 0x78;
        request[2] = 0x56;
        request[offset - 2u] = 0x5a;
        request[offset - 1u] = 0x0c;
        request[offset] = request[offset + 1u] = 0;
        for (n = 0; n <= 5; n++) {
            request[offset + 2u] = n;
            for (capacity = 0; capacity <= 20; capacity++) {
                if (capacity < offset + 1u + (n ? 3u : 0u)) {
                    CHECK(failure((uint16_t)(offset + 3u), capacity, ZCL_CODEC_BUFFER_TOO_SMALL) == 0);
                    continue;
                }
                memset(response, 0xc7, sizeof(response));
                CHECK(zcl_dispatch_unicast(&set, request, (uint16_t)(offset + 3u), response + 1, capacity, &info) == ZCL_CODEC_OK);
                expected = (uint8_t)((capacity - offset - 1u) / 3u);
                if (expected > n) expected = n;
                if (expected > 4u) expected = 4;
                CHECK(info.returned_count == expected && info.length == offset + 1u + 3u * expected);
                CHECK(info.requested_count == n && info.discovery_complete == (expected == 4u));
                CHECK(response[offset + 1u] == info.discovery_complete);
                CHECK(memcmp(response + offset + 2u, golden + 4, 3u * expected) == 0);
                CHECK(response[0] == 0xc7 && filled(response + 1 + info.length, 101u - info.length, 0xc7));
            }
        }
    }
    return 0;
}

static uint16_t dispatch_cases(void)
{
    uint8_t length, i;
    base();
    request[2] = 0;
    request[3] = 0xfe;
    request[4] = 0xff;
    CHECK(zcl_dispatch_unicast(&set, request, 5, response, 100, &info) == ZCL_CODEC_OK);
    CHECK(info.command_id == 1 && info.length == 9 && info.requested_count == 1 && info.returned_count == 1);
    CHECK(response[0] == 0x18 && response[1] == 0x5a && response[2] == 1 && response[5] == 0);
    CHECK(response[6] == 0x21 && response[7] == 0x78 && response[8] == 0x56);
    request[3] = 0x34;
    request[4] = 0x12;
    CHECK(zcl_dispatch_unicast(&set, request, 5, response, 100, &info) == ZCL_CODEC_OK);
    CHECK(info.length == 6 && response[5] == 0x7e);
    for (i = 0; i < 2; i++) {
        request[0] = i ? 0x10 : 0;
        request[2] = 0x0c;
        for (length = 3; length < 6; length++) {
            CHECK(failure(length, 4, ZCL_CODEC_BUFFER_TOO_SMALL) == 0);
            CHECK(zcl_dispatch_unicast(&set, request, length, response, 5, &info) == ZCL_CODEC_OK);
            CHECK(info.length == 5 && info.command_id == 0x0b && info.default_command == 0x0c);
            CHECK(info.default_status == 0x80 && info.default_raw_status == 0x80);
            CHECK(response[0] == 0x18 && response[2] == 0x0b && response[3] == 0x0c && response[4] == 0x80);
        }
        request[2] = 0;
        CHECK(zcl_dispatch_unicast(&set, request, 4, response, 5, &info) == ZCL_CODEC_OK);
        CHECK(info.command_id == 0x0b && info.default_command == 0 && info.default_status == 0x80);
        request[2] = 0x02;
        CHECK(failure(6, 5, ZCL_CODEC_UNSUPPORTED_DATA_TYPE) == 0); /* received type FF */
        request[2] = 5;
        CHECK(failure(3, 0, ZCL_CODEC_TRUNCATED) == 0);
        CHECK(failure(100, 100, ZCL_CODEC_UNSUPPORTED_DATA_TYPE) == 0);
        request[2] = 0x0b;
        CHECK(failure(3, 100, ZCL_CODEC_TRUNCATED) == 0);
        CHECK(failure(4, 100, ZCL_CODEC_TRUNCATED) == 0);
        request[3] = 0x02;
        request[4] = 0x8f;
        CHECK(zcl_dispatch_unicast(&set, request, 5, response, 0, &info) == ZCL_CODEC_OK);
        CHECK(info.kind == ZCL_DISPATCH_DEFAULT_RECEIVED && info.length == 0 && info.sequence == 0x5a);
        CHECK(info.command_id == 0x0b && info.default_command == 2 && info.default_raw_status == 0x8f && info.default_status == 0x7e);
        CHECK(filled(response, sizeof(response), 0xc7));
        CHECK(zcl_dispatch_unicast(&set, request, 100, response, 0, &info) == ZCL_CODEC_OK);
        CHECK(info.kind == ZCL_DISPATCH_DEFAULT_RECEIVED && info.default_status == 0x7e);
        request[0] |= 1u;
        CHECK(zcl_dispatch_unicast(&set, request, 5, response, 100, &info) == ZCL_CODEC_OK);
        CHECK(info.kind == ZCL_DISPATCH_RESPONSE && info.default_command == 0x0b && info.default_status == 0x81);
        request[2] = 5;
        CHECK(zcl_dispatch_unicast(&set, request, 5, response, 100, &info) == ZCL_CODEC_OK);
        CHECK(info.kind == ZCL_DISPATCH_RESPONSE && info.default_command == 5 && info.default_status == 0x81);
    }
    base();
    CHECK(zcl_dispatch_unicast(&set, request, 100, response, 100, &info) == ZCL_CODEC_OK);
    CHECK(info.length == sizeof(golden) && memcmp(response, golden, sizeof(golden)) == 0);
    request[0] = 4;
    request[1] = 0x78;
    request[2] = 0x56;
    request[3] = 0x5a;
    request[4] = 0x0c;
    request[5] = request[6] = 0;
    request[7] = 255;
    CHECK(zcl_dispatch_unicast(&set, request, 8, response, 7, &info) == ZCL_CODEC_OK);
    CHECK(info.default_status == 0x81 && response[0] == 0x1c && response[1] == 0x78 && response[2] == 0x56);
    CHECK(response[3] == 0x5a && response[4] == 0x0b && response[5] == 0x0c && response[6] == 0x81);
    request[4] = 5;
    CHECK(failure(8, 100, ZCL_CODEC_UNSUPPORTED_NO_RESPONSE) == 0);
    request[4] = 0x0b;
    CHECK(failure(7, 100, ZCL_CODEC_UNSUPPORTED_CONTEXT) == 0);
    set.manufacturer_specific = 1;
    request[5] = 0;
    request[6] = 0x82;
    CHECK(zcl_dispatch_unicast(&set, request, 7, response, 0, &info) == ZCL_CODEC_OK);
    CHECK(info.kind == ZCL_DISPATCH_DEFAULT_RECEIVED && info.default_status == 0x81);
    CHECK(failure(8, 100, ZCL_CODEC_UNSUPPORTED_LAYOUT) == 0);
    request[4] = 0x0c;
    request[5] = request[6] = 0;
    CHECK(failure(9, 100, ZCL_CODEC_UNSUPPORTED_LAYOUT) == 0);
    set.manufacturer_code ^= 1u;
    CHECK(zcl_dispatch_unicast(&set, request, 8, response, 7, &info) == ZCL_CODEC_OK);
    CHECK(info.default_status == 0x81 && response[1] == 0x78 && response[2] == 0x56);
    base();
    attributes[1].value.type = ZCL_TYPE_BOOLEAN; /* denied entry has no valid backing storage */
    memcpy(request, "\x00\x5a\x02\x34\x12\x10\x05", 7);
    CHECK(failure(7, 5, ZCL_CODEC_BUFFER_TOO_SMALL) == 0);
    CHECK(zcl_dispatch_unicast(&set, request, 7, response, 6, &info) == ZCL_CODEC_OK);
    CHECK(memcmp(response, "\x18\x5a\x04\x88\x34\x12", 6) == 0); /* permission before invalid value */
    attributes[1].value.type = ZCL_TYPE_UINT8;
    CHECK(zcl_dispatch_unicast(&set, request, 7, response, 6, &info) == ZCL_CODEC_OK);
    CHECK(memcmp(response, "\x18\x5a\x04\x8d\x34\x12", 6) == 0); /* type before permission */
    request[4] = 0x13;
    CHECK(zcl_dispatch_unicast(&set, request, 7, response, 6, &info) == ZCL_CODEC_OK);
    CHECK(memcmp(response, "\x18\x5a\x04\x86\x34\x13", 6) == 0); /* absence before type */
    return 0;
}

static uint16_t model_cases(void)
{
    uint8_t i;
    base();
    attributes[1].id = attributes[0].id;
    CHECK(failure(6, 100, ZCL_CODEC_INVALID_TABLE) == 0);
    base();
    attributes[0].readable = 2;
    CHECK(failure(6, 100, ZCL_CODEC_INVALID_TABLE) == 0);
    base();
    set.side = 2;
    CHECK(failure(6, 100, ZCL_CODEC_INVALID_TABLE) == 0);
    set.side = 0;
    request[0] = 8;
    CHECK(failure(6, 100, ZCL_CODEC_UNSUPPORTED_CONTEXT) == 0);
    base();
    set.manufacturer_specific = 2;
    CHECK(failure(6, 100, ZCL_CODEC_INVALID_TABLE) == 0);
    base();
    set.count = 17;
    CHECK(failure(6, 100, ZCL_CODEC_INVALID_TABLE) == 0);
    set.count = 1;
    set.attributes = NULL;
    CHECK(failure(6, 100, ZCL_CODEC_INVALID_TABLE) == 0);
    base();
    attributes[1].value.type = 0xff;
    CHECK(failure(6, 100, ZCL_CODEC_UNSUPPORTED_DATA_TYPE) == 0);
    request[5] = 1;
    CHECK(zcl_dispatch_unicast(&set, request, 6, response, 7, &info) == ZCL_CODEC_OK);
    CHECK(info.returned_count == 1 && info.discovery_complete == 0);
    request[2] = 0;
    request[3] = 0xfe;
    request[4] = 0xff;
    attributes[0].value.data = NULL;
    CHECK(failure(5, 100, ZCL_CODEC_INVALID_ARGUMENT) == 0);
    base();
    set.count = 16;
    for (i = 0; i < set.count; i++) {
        attributes[i].id = (uint16_t)(0xffefu + (15u - i));
        attributes[i].readable = 0;
        attributes[i].value.type = ZCL_TYPE_UINT8;
    }
    CHECK(zcl_dispatch_unicast(&set, request, 6, response, 100, &info) == ZCL_CODEC_OK);
    CHECK(info.length == 52 && info.returned_count == 16 && info.discovery_complete == 1);
    for (i = 0; i < set.count; i++)
        CHECK(response[4u + 3u * i] == 0xefu + i && response[5u + 3u * i] == 0xff && response[6u + 3u * i] == 0x20);
    CHECK(attributes[0].id == 0xfffe && attributes[15].id == 0xffef);
    base();
    for (i = 0; i < 3; i++)
        CHECK(failure(i, 100, ZCL_CODEC_TRUNCATED) == 0);
    CHECK(failure(101, 100, ZCL_CODEC_TOO_LONG) == 0);
    request[0] = 2;
    CHECK(failure(6, 100, ZCL_CODEC_UNSUPPORTED_FRAME_TYPE) == 0);
    request[0] = 0xe4;
    CHECK(failure(6, 100, ZCL_CODEC_UNSUPPORTED_LAYOUT) == 0);
    CHECK(zcl_attr_set_check(NULL) == ZCL_CODEC_INVALID_ARGUMENT);
    CHECK(zcl_dispatch_unicast(NULL, request, 6, response, 100, &info) == ZCL_CODEC_INVALID_ARGUMENT);
    CHECK(zcl_dispatch_unicast(&set, NULL, 6, response, 100, &info) == ZCL_CODEC_INVALID_ARGUMENT);
    CHECK(zcl_dispatch_unicast(&set, request, 6, NULL, 100, &info) == ZCL_CODEC_INVALID_ARGUMENT);
    CHECK(zcl_dispatch_unicast(&set, request, 6, response, 100, NULL) == ZCL_CODEC_INVALID_ARGUMENT);
    CHECK(filled(response, sizeof(response), 0xc7) && filled((const uint8_t *)&info, sizeof(info), 0xa5));
    return 0;
}

static uint16_t identifier_cases(void)
{
    static const MCU_CODE uint16_t identifiers[] = {0x4fff, 0x5000, 0xefff, 0xf000, 0xfffe, 0xffff};
    volatile uint8_t manufacturer, i, offset;
    for (manufacturer = 0; manufacturer < 2; manufacturer++) {
        for (i = 0; i < sizeof(identifiers) / sizeof(identifiers[0]); i++) {
            base();
            set.count = 2;
            set.manufacturer_specific = manufacturer;
            attributes[0].id = identifiers[i];
            offset = manufacturer ? 5 : 3;
            request[0] = manufacturer ? 4 : 0;
            request[1] = 0x78;
            request[2] = 0x56;
            request[offset - 2u] = 0x5a;
            request[offset - 1u] = 0x0c;
            request[offset] = request[offset + 1u] = 0;
            request[offset + 2u] = 1;
            if (!manufacturer && (i == 1 || i == 2 || i == 5))
                CHECK(failure((uint16_t)(offset + 3u), 100, ZCL_CODEC_INVALID_TABLE) == 0);
            request[offset] = (uint8_t)identifiers[i];
            request[offset + 1u] = (uint8_t)(identifiers[i] >> 8);
            if (!manufacturer && (i == 1 || i == 2 || i == 5)) {
                CHECK(failure((uint16_t)(offset + 3u), 100, ZCL_CODEC_INVALID_TABLE) == 0);
            } else {
                CHECK(zcl_dispatch_unicast(&set, request, (uint16_t)(offset + 3u),
                                           response, 100, &info) == ZCL_CODEC_OK);
                CHECK(info.length == offset + 4u && info.returned_count == 1 && info.discovery_complete == 1);
                CHECK(response[offset] == 1 && response[offset + 1u] == request[offset]);
                CHECK(response[offset + 2u] == request[offset + 1u] && response[offset + 3u] == 0x21);
            }
            CHECK(attributes[0].id == identifiers[i] && attributes[1].id == 0x1234);
        }
    }
    return 0;
}

static uint16_t run_tests(void)
{
    uint16_t result = golden_cases();
    if (result == 0) result = capacity_cases();
    if (result == 0) result = dispatch_cases();
    if (result == 0) result = model_cases();
    if (result == 0) result = identifier_cases();
    return result;
}

#ifndef CC2530_HOST_TEST
volatile MCU_XDATA MCU_AT(0x1e00) uint8_t zcl_dispatch_test_result[8];

void main(void)
{
    uint16_t result = run_tests();
    zcl_dispatch_test_result[0] = 'Z';
    zcl_dispatch_test_result[1] = 'C';
    zcl_dispatch_test_result[2] = 'D';
    zcl_dispatch_test_result[3] = '1';
    zcl_dispatch_test_result[4] = 1;
    zcl_dispatch_test_result[5] = 8;
    zcl_dispatch_test_result[6] = (uint8_t)result;
    zcl_dispatch_test_result[7] = (uint8_t)(result >> 8);
    __asm
        .globl _zcl_dispatch_test_done
    _zcl_dispatch_test_done:
        nop
    __endasm;
    for (;;) {}
}
#else
#include <stdio.h>
#include <stdlib.h>

static uint16_t exhaustive_cases(void)
{
    uint32_t n;
    unsigned maximum, capacity, count, i, expected, used, status, manufacturer, command, control;
    uint16_t start;
    uint8_t *input, *output;
    zcl_dispatch_info_t *result;
    zcl_attribute_t *table;
    size_t bytes;
    zcl_codec_result_t rc;
    base();
    for (n = 0; n <= 65535UL; n++) {
        request[3] = (uint8_t)n;
        request[4] = (uint8_t)(n >> 8);
        CHECK(zcl_dispatch_unicast(&set, request, 6, response, 100, &info) == ZCL_CODEC_OK);
        expected = 0;
        for (i = 0; i < 4; i++) {
            start = (uint16_t)((uint16_t)golden[4u + i * 3u] | ((uint16_t)golden[5u + i * 3u] << 8));
            if (start >= n) {
                CHECK(memcmp(response + 4 + expected * 3, golden + 4 + i * 3, 3) == 0);
                expected++;
            }
        }
        CHECK(info.discovery_complete == 1 && info.returned_count == expected && info.length == 4u + 3u * expected);
    }
    for (count = 0; count <= 16; count++) {
        base();
        bytes = count ? count * sizeof(*table) : 1u;
        table = malloc(bytes);
        CHECK(table != NULL);
        memset(table, 0, bytes);
        for (i = 0; i < count; i++) {
            table[i].id = (uint16_t)(count - 1u - i);
            table[i].value.type = ZCL_TYPE_NO_DATA;
        }
        set.attributes = table;
        set.count = (uint8_t)count;
        for (maximum = 0; maximum < 256; maximum++) {
            request[5] = (uint8_t)maximum;
            for (capacity = 0; capacity <= 102; capacity++) {
                memset(response, 0xc7, sizeof(response));
                memset(&info, 0xa5, sizeof(info));
                rc = zcl_dispatch_unicast(&set, request, 6, response, (uint16_t)capacity, &info);
                if (capacity < 4 || (capacity < 7 && maximum && count)) {
                    CHECK(rc == ZCL_CODEC_BUFFER_TOO_SMALL);
                    CHECK(filled(response, sizeof(response), 0xc7) && filled((const uint8_t *)&info, sizeof(info), 0xa5));
                } else {
                    expected = ((capacity > 100 ? 100u : capacity) - 4u) / 3u;
                    if (expected > maximum) expected = maximum;
                    if (expected > count) expected = count;
                    CHECK(rc == ZCL_CODEC_OK && info.returned_count == expected);
                    CHECK(info.length == 4u + 3u * expected && info.discovery_complete == (expected == count));
                    for (i = 0; i < expected; i++)
                        CHECK(response[4u + i * 3u] == i && response[5u + i * 3u] == 0 && response[6u + i * 3u] == 0);
                    CHECK(filled(response + info.length, (uint16_t)(sizeof(response) - info.length), 0xc7));
                }
            }
        }
        free(table);
    }
    base();
    for (control = 0; control < 256; control++) {
        request[0] = (uint8_t)control;
        set.side = (uint8_t)((control >> 3) & 1u);
        set.manufacturer_specific = (uint8_t)((control >> 2) & 1u);
        used = set.manufacturer_specific ? 5u : 3u;
        request[1] = 0x78;
        request[2] = 0x56;
        request[used - 2u] = 0x5a;
        request[used] = request[used + 1u] = 0;
        request[used + 2u] = 255;
        for (command = 0; command < 256; command++) {
            request[used - 1u] = (uint8_t)command;
            memset(response, 0xc7, sizeof(response));
            memset(&info, 0xa5, sizeof(info));
            rc = zcl_dispatch_unicast(&set, request, (uint16_t)(used + (command == 0x0b ? 2u : 3u)), response, 100, &info);
            if ((control & 3u) > 1u || (control & 4u && control & 0xe0u)) {
                CHECK(rc != ZCL_CODEC_OK && filled(response, sizeof(response), 0xc7));
                CHECK(filled((const uint8_t *)&info, sizeof(info), 0xa5));
            } else if ((control & 3u) == 0 && (command == 2 || command == 3 || command == 5)) {
                CHECK(rc == ZCL_CODEC_UNSUPPORTED_DATA_TYPE && filled(response, sizeof(response), 0xc7));
                CHECK(filled((const uint8_t *)&info, sizeof(info), 0xa5));
            } else {
                CHECK(rc == ZCL_CODEC_OK);
                if ((control & 3u) == 0 && command == 0x0b) {
                    CHECK(info.kind == ZCL_DISPATCH_DEFAULT_RECEIVED && info.length == 0 && filled(response, sizeof(response), 0xc7));
                } else {
                    CHECK(info.kind == ZCL_DISPATCH_RESPONSE && response[0] == (((control & 0x1cu) ^ 8u) | 0x10u));
                    CHECK(response[used - 2u] == 0x5a && response[used - 1u] == info.command_id);
                    if ((control & 3u) == 0 && command == 0x0c) {
                        CHECK(info.command_id == 0x0d && info.discovery_complete == 1 && info.returned_count == 4);
                    } else {
                        CHECK(info.command_id == 0x0b && info.default_command == command);
                        CHECK(info.default_status == ((control & 3u) == 0 && command == 0 ? 0x80u : 0x81u));
                    }
                }
            }
        }
    }
    base();
    request[2] = 0x0b;
    for (n = 0; n < 256; n++) {
        request[3] = (uint8_t)n;
        for (status = 0; status < 256; status++) {
            request[4] = (uint8_t)status;
            expected = status;
            switch (status) {
            case 0x82: case 0x83: case 0x84: expected = 0x81; break;
            case 0x8a: case 0xc4: expected = 0; break;
            case 0x8f: expected = 0x7e; break;
            case 0x90: case 0x91: case 0x93: case 0xc0: case 0xc1: expected = 1; break;
            }
            CHECK(zcl_dispatch_unicast(&set, request, 5, response, 0, &info) == ZCL_CODEC_OK);
            CHECK(info.kind == ZCL_DISPATCH_DEFAULT_RECEIVED && info.default_command == n && info.default_status == expected);
            CHECK(info.default_raw_status == status && info.length == 0 && filled(response, sizeof(response), 0xc7));
        }
    }
    for (n = 0; n < 256; n++) {
        base();
        set.count = 1;
        attributes[0].id = 0;
        attributes[0].readable = 0;
        attributes[0].value.data = NULL;
        attributes[0].value.data_length = 65535;
        attributes[0].value.type = (uint8_t)n;
        expected = n == 0 || (n >= 8 && n <= 16) || (n >= 0x18 && n <= 0x31) || n == 0x41 || n == 0x42;
        CHECK(zcl_value_type_supported((uint8_t)n) == expected);
        if (expected) {
            CHECK(zcl_dispatch_unicast(&set, request, 6, response, 100, &info) == ZCL_CODEC_OK);
            CHECK(info.length == 7 && response[6] == n);
        } else {
            CHECK(failure(6, 100, ZCL_CODEC_UNSUPPORTED_DATA_TYPE) == 0);
        }
    }
    result = malloc(sizeof(*result));
    CHECK(result != NULL);
    for (manufacturer = 0; manufacturer < 2; manufacturer++) {
        base();
        set.manufacturer_specific = (uint8_t)manufacturer;
        used = manufacturer ? 5u : 3u;
        request[0] = manufacturer ? 4 : 0;
        request[1] = 0x78;
        request[2] = 0x56;
        request[used - 2u] = 0x5a;
        request[used - 1u] = 0x0c;
        request[used] = request[used + 1u] = 0;
        request[used + 2u] = 255;
        for (n = 0; n <= 101; n++) {
            input = malloc(n ? n : 1u);
            CHECK(input != NULL);
            memcpy(input, request, n);
            for (capacity = 0; capacity <= 102; capacity++) {
                output = malloc(capacity ? capacity : 1u);
                CHECK(output != NULL);
                memset(output, 0xc7, capacity);
                memset(result, 0xa5, sizeof(*result));
                rc = zcl_dispatch_unicast(&set, input, (uint16_t)n, output, (uint16_t)capacity, result);
                if (n > 100) CHECK(rc == ZCL_CODEC_TOO_LONG);
                else if (n < used) CHECK(rc == ZCL_CODEC_TRUNCATED);
                else if (n < used + 3u) CHECK(rc == (capacity < used + 2u ? ZCL_CODEC_BUFFER_TOO_SMALL : ZCL_CODEC_OK));
                else if (manufacturer && n > used + 3u) CHECK(rc == ZCL_CODEC_UNSUPPORTED_LAYOUT);
                else CHECK(rc == (capacity < used + 4u ? ZCL_CODEC_BUFFER_TOO_SMALL : ZCL_CODEC_OK));
                if (rc != ZCL_CODEC_OK) {
                    CHECK(filled(output, (uint16_t)capacity, 0xc7) && filled((const uint8_t *)result, sizeof(*result), 0xa5));
                } else {
                    CHECK(result->kind == ZCL_DISPATCH_RESPONSE && result->length <= capacity);
                    CHECK(filled(output + result->length, (uint16_t)(capacity - result->length), 0xc7));
                }
                CHECK(memcmp(input, request, n) == 0);
                free(output);
            }
            free(input);
        }
    }
    free(result);
    return 0;
}

static uint16_t identifier_matrix(void)
{
    uint32_t id;
    unsigned manufacturer, offset;
    zcl_codec_result_t expected;
    for (manufacturer = 0; manufacturer < 2; manufacturer++) {
        base();
        set.count = 1;
        set.manufacturer_specific = (uint8_t)manufacturer;
        offset = manufacturer ? 5u : 3u;
        request[0] = manufacturer ? 4 : 0;
        request[1] = 0x78;
        request[2] = 0x56;
        request[offset - 2u] = 0x5a;
        request[offset + 2u] = 1;
        for (id = 0; id <= 65535UL; id++) {
            attributes[0].id = (uint16_t)id;
            expected = manufacturer || id <= 0x4fff || (id >= 0xf000 && id <= 0xfffe)
                       ? ZCL_CODEC_OK : ZCL_CODEC_INVALID_TABLE;
            CHECK(zcl_attr_set_check(&set) == expected);
            request[offset] = (uint8_t)id;
            request[offset + 1u] = (uint8_t)(id >> 8);
            request[offset - 1u] = 0x0c;
            if (expected != ZCL_CODEC_OK) {
                CHECK(failure((uint16_t)(offset + 3u), 100, expected) == 0);
                request[offset - 1u] = 0;
                CHECK(failure((uint16_t)(offset + 2u), 100, expected) == 0);
            } else {
                CHECK(zcl_dispatch_unicast(&set, request, (uint16_t)(offset + 3u),
                                           response, 100, &info) == ZCL_CODEC_OK);
                CHECK(info.returned_count == 1 && info.discovery_complete == 1 && info.length == offset + 4u);
                CHECK(response[offset + 1u] == (uint8_t)id && response[offset + 2u] == (uint8_t)(id >> 8));
                request[offset - 1u] = 0;
                CHECK(zcl_dispatch_unicast(&set, request, (uint16_t)(offset + 2u),
                                           response, 100, &info) == ZCL_CODEC_OK);
                CHECK(info.returned_count == 1 && info.length == offset + 6u && response[offset + 2u] == 0);
                CHECK(response[offset] == (uint8_t)id && response[offset + 1u] == (uint8_t)(id >> 8));
            }
        }
    }
    return 0;
}

static uint16_t wide_spans_and_context(void)
{
    uint8_t *input, *output;
    zcl_dispatch_info_t *result;
    uint32_t n;
    unsigned capacity;
    zcl_codec_result_t expected;
    base();
    input = malloc(65535);
    output = malloc(65535);
    result = malloc(sizeof(*result));
    CHECK(input != NULL && output != NULL && result != NULL);
    memset(input, 0xee, 65535);
    memset(output, 0xc7, 65535);
    memcpy(input, code_request, 6);
    for (n = 0; n <= 65535UL; n++) {
        memset(result, 0xa5, sizeof(*result));
        memset(response, 0xc7, sizeof(response));
        expected = n > 100 ? ZCL_CODEC_TOO_LONG : n < 3 ? ZCL_CODEC_TRUNCATED : ZCL_CODEC_OK;
        CHECK(zcl_dispatch_unicast(&set, input, (uint16_t)n, response, 100, result) == expected);
        if (expected != ZCL_CODEC_OK) {
            CHECK(filled(response, sizeof(response), 0xc7) && filled((const uint8_t *)result, sizeof(*result), 0xa5));
        } else {
            CHECK(result->command_id == (n < 6 ? 0x0bu : 0x0du));
        }
        memset(output, 0xc7, 20);
        memset(result, 0xa5, sizeof(*result));
        expected = n < 7 ? ZCL_CODEC_BUFFER_TOO_SMALL : ZCL_CODEC_OK;
        CHECK(zcl_dispatch_unicast(&set, input, 6, output, (uint16_t)n, result) == expected);
        if (expected == ZCL_CODEC_OK) {
            capacity = ((n > 100 ? 100u : n) - 4u) / 3u;
            if (capacity > 4) capacity = 4;
            CHECK(result->length == 4u + 3u * capacity && result->returned_count == capacity);
            CHECK(result->discovery_complete == (capacity == 4));
            CHECK(filled(output + result->length, (uint16_t)(20u - result->length), 0xc7));
        } else {
            CHECK(filled(output, 20, 0xc7) && filled((const uint8_t *)result, sizeof(*result), 0xa5));
        }
    }
    CHECK(filled(output + 20, 65515, 0xc7));
    free(input);
    free(output);
    free(result);
    base();
    set.manufacturer_specific = 1;
    request[0] = 4;
    request[3] = 0x5a;
    request[4] = 0x0c;
    request[5] = request[6] = 0;
    request[7] = 1;
    for (n = 0; n <= 65535UL; n++) {
        set.manufacturer_code = (uint16_t)n;
        request[1] = (uint8_t)n;
        request[2] = (uint8_t)(n >> 8);
        request[3] = (uint8_t)n;
        CHECK(zcl_dispatch_unicast(&set, request, 8, response, 100, &info) == ZCL_CODEC_OK);
        CHECK(info.command_id == 0x0d && info.sequence == (uint8_t)n && info.returned_count == 1 && info.discovery_complete == 0);
        CHECK(response[1] == request[1] && response[2] == request[2] && response[3] == request[3]);
        set.manufacturer_code ^= 1u;
        CHECK(zcl_dispatch_unicast(&set, request, 8, response, 100, &info) == ZCL_CODEC_OK);
        CHECK(info.command_id == 0x0b && info.default_status == 0x81);
        CHECK(response[1] == request[1] && response[2] == request[2] && response[3] == request[3]);
    }
    return 0;
}

int main(void)
{
    uint16_t result = run_tests();
    if (result == 0) result = exhaustive_cases();
    if (result == 0) result = identifier_matrix();
    if (result == 0) result = wide_spans_and_context();
    if (result != 0) {
        fprintf(stderr, "ZCL dispatch test failed at C source line %u\n", result);
        return 1;
    }
    puts("ZCL dispatch: discovery/read/default responses, ordered pages and exact host boundaries PASS.");
    return 0;
}
#endif
