/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "zcl_attributes.h"
#include "cc2530_mmio.h"

#include <stddef.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) return (uint16_t)__LINE__; } while (0)

static const MCU_CODE uint8_t number[] = {0xef, 0xbe};
static const MCU_CODE uint8_t text[] = {'A', 0, 'z'};
static const MCU_CODE uint8_t long_data[255] = {0xa9, 0, 0x69};
static const MCU_CODE zcl_attribute_t code_attributes[] = {
    {0x1234, 1, {ZCL_TYPE_UINT16, 0, number, 2}},
    {0x2000, 1, {ZCL_TYPE_CHARACTER_STRING, 0, text, 3}},
    {0x8001, 0, {0xff, 0xff, NULL, 65535}},
    {0x0000, 1, {ZCL_TYPE_NO_DATA, 0, NULL, 0}},
    {0x4444, 1, {ZCL_TYPE_OCTET_STRING, 1, NULL, 0}}
};
static const MCU_CODE zcl_attribute_set_t code_set = {code_attributes, 5, 0, 0, 0xabcd};
static const MCU_CODE uint8_t golden_request[] = {
    0, 0x5a, 0, 0x34, 0x12, 0x99, 0x99, 0x01, 0x80, 0, 0, 0, 0x20, 0x44, 0x44, 0x34, 0x12
};
static const MCU_CODE uint8_t golden_response[] = {
    0x18, 0x5a, 1, 0x34, 0x12, 0, 0x21, 0xef, 0xbe,
    0x99, 0x99, 0x86, 0x01, 0x80, 0x7e, 0, 0, 0, 0,
    0, 0x20, 0, 0x42, 3, 'A', 0, 'z', 0x44, 0x44, 0, 0x41, 0xff,
    0x34, 0x12, 0, 0x21, 0xef, 0xbe
};
static zcl_attribute_t attributes[ZCL_ATTRIBUTE_MAX_COUNT + 1u];
static zcl_attribute_set_t set;
static zcl_read_info_t info;
static uint8_t request[102], response[102], raw[8];

static uint8_t filled(const uint8_t *bytes, uint16_t length, uint8_t value)
{
    while (length--)
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
    memcpy(request, golden_request, sizeof(golden_request));
    memset(response, 0xc7, sizeof(response));
    memset(&info, 0xa5, sizeof(info));
}

static uint16_t failure(uint16_t length, uint16_t capacity, zcl_codec_result_t expected)
{
    memset(response, 0xc7, sizeof(response));
    memset(&info, 0xa5, sizeof(info));
    CHECK(zcl_read_attrs_unicast(&set, request, length, response + 1, capacity, &info) == expected);
    CHECK(filled(response, sizeof(response), 0xc7) && filled((const uint8_t *)&info, sizeof(info), 0xa5));
    return 0;
}

static uint16_t golden_cases(void)
{
    uint8_t manufacturer, side, disable, offset;
    base();
    CHECK(zcl_read_attrs_unicast(&code_set, golden_request, sizeof(golden_request),
                                      response + 1, 100, &info) == ZCL_CODEC_OK);
    CHECK(info.length == sizeof(golden_response) && info.command_id == 1);
    CHECK(info.requested_count == 7 && info.returned_count == 7);
    CHECK(memcmp(response + 1, golden_response, sizeof(golden_response)) == 0);
    CHECK(response[0] == 0xc7 && filled(response + 1 + info.length, 101u - info.length, 0xc7));
    for (manufacturer = 0; manufacturer < 2; manufacturer++) {
        for (side = 0; side < 2; side++) {
            for (disable = 0; disable < 2; disable++) {
                base();
                set.manufacturer_specific = manufacturer;
                set.manufacturer_code = 0x5678;
                set.side = side;
                offset = manufacturer ? 5 : 3;
                request[0] = (uint8_t)((manufacturer ? 4u : 0xe0u) | (side ? 8u : 0u) | (disable ? 16u : 0u));
                if (manufacturer) {
                    request[1] = 0x78;
                    request[2] = 0x56;
                }
                request[offset - 2u] = 0x5a;
                request[offset - 1u] = 0;
                memcpy(request + offset, golden_request + 3, sizeof(golden_request) - 3u);
                CHECK(zcl_read_attrs_unicast(&set, request, (uint16_t)(offset + 14u),
                                                  response + 1, 100, &info) == ZCL_CODEC_OK);
                CHECK(info.length == offset + sizeof(golden_response) - 3u);
                CHECK(info.requested_count == 7 && info.returned_count == 7 && info.command_id == 1);
                CHECK(response[1] == (uint8_t)(0x10u | (manufacturer ? 4u : 0u) | (side ? 0u : 8u)));
                CHECK(response[offset - 1u] == 0x5a && response[offset] == 1);
                if (manufacturer)
                    CHECK(response[2] == 0x78 && response[3] == 0x56);
                CHECK(memcmp(response + 1 + offset, golden_response + 3, sizeof(golden_response) - 3u) == 0);
                CHECK(memcmp(request + offset, golden_request + 3, 14) == 0);
                CHECK(response[0] == 0xc7 && filled(response + 1 + info.length, 101u - info.length, 0xc7));
            }
        }
    }
    base();
    raw[0] = 0x55;
    raw[1] = 0xaa;
    attributes[0].value.data = raw;
    CHECK(zcl_read_attrs_unicast(&set, request, 5, response, 100, &info) == ZCL_CODEC_OK);
    CHECK(info.length == 9 && response[7] == 0x55 && response[8] == 0xaa);
    raw[0] = 0x69;
    CHECK(zcl_read_attrs_unicast(&set, request, 5, response, 100, &info) == ZCL_CODEC_OK);
    CHECK(response[7] == 0x69 && raw[1] == 0xaa);
    return 0;
}

static uint16_t capacity_cases(void)
{
    uint8_t manufacturer, offset, capacity, count, i;
    for (manufacturer = 0; manufacturer < 2; manufacturer++) {
        base();
        set.attributes = NULL;
        set.count = 0;
        set.manufacturer_specific = manufacturer;
        set.manufacturer_code = 0x5678;
        offset = manufacturer ? 5 : 3;
        memset(request, 0xee, sizeof(request));
        request[0] = manufacturer ? 4 : 0;
        request[1] = 0x78;
        request[2] = 0x56;
        request[offset - 2u] = 0x5a;
        request[offset - 1u] = 0;
        for (capacity = 0; capacity <= 100; capacity++) {
            if (capacity < offset + 3u) {
                CHECK(failure(99, capacity, ZCL_CODEC_BUFFER_TOO_SMALL) == 0);
                continue;
            }
            memset(response, 0xc7, sizeof(response));
            CHECK(zcl_read_attrs_unicast(&set, request, 99, response + 1, capacity, &info) == ZCL_CODEC_OK);
            count = (uint8_t)((capacity - offset) / 3u);
            CHECK(info.length == offset + 3u * count && info.returned_count == count);
            CHECK(info.requested_count == (99u - offset) / 2u && info.command_id == 1);
            for (i = 0; i < count; i++) {
                CHECK(response[1u + offset + i * 3u] == 0xee && response[2u + offset + i * 3u] == 0xee);
                CHECK(response[3u + offset + i * 3u] == 0x86);
            }
            CHECK(response[0] == 0xc7 && filled(response + 1 + info.length, 101u - info.length, 0xc7));
        }
    }
    base();
    request[5] = 0;
    request[6] = 0x20;
    request[7] = 0x99;
    request[8] = 0x99;
    CHECK(zcl_read_attrs_unicast(&set, request, 9, response, 15, &info) == ZCL_CODEC_OK);
    CHECK(info.length == 15 && info.requested_count == 3 && info.returned_count == 3);
    CHECK(memcmp(response, golden_response, 9) == 0);
    CHECK(response[9] == 0 && response[10] == 0x20 && response[11] == 0x89);
    CHECK(response[12] == 0x99 && response[13] == 0x99 && response[14] == 0x86);
    CHECK(zcl_read_attrs_unicast(&set, request, 9, response, 14, &info) == ZCL_CODEC_OK);
    CHECK(info.length == 12 && info.returned_count == 2 && response[11] == 0x89);
    CHECK(zcl_read_attrs_unicast(&set, request, 9, response, 8, &info) == ZCL_CODEC_OK);
    CHECK(info.length == 6 && info.returned_count == 1 && response[5] == 0x89);
    request[3] = request[4] = 0;
    CHECK(zcl_read_attrs_unicast(&set, request, 5, response, 6, &info) == ZCL_CODEC_OK);
    CHECK(info.length == 6 && response[5] == 0x89);
    CHECK(zcl_read_attrs_unicast(&set, request, 5, response, 7, &info) == ZCL_CODEC_OK);
    CHECK(info.length == 7 && response[5] == 0 && response[6] == 0);
    base();
    attributes[0].value.type = ZCL_TYPE_OCTET_STRING;
    attributes[0].value.data = long_data;
    attributes[0].value.data_length = 254;
    CHECK(zcl_read_attrs_unicast(&set, request, 5, response, 100, &info) == ZCL_CODEC_OK);
    CHECK(info.length == 6 && info.returned_count == 1 && response[5] == 0x89);
    attributes[0].value.data_length = 255;
    CHECK(failure(5, 100, ZCL_CODEC_TOO_LONG) == 0);
    return 0;
}

static uint16_t rejection_cases(void)
{
    uint8_t length, i;
    base();
    for (length = 0; length < 3; length++)
        CHECK(failure(length, 100, ZCL_CODEC_TRUNCATED) == 0);
    CHECK(failure(101, 100, ZCL_CODEC_TOO_LONG) == 0);
    for (length = 3; length <= 8; length++) {
        if (length != 3 && ((length - 3u) & 1u) == 0)
            continue;
        CHECK(failure(length, 4, ZCL_CODEC_BUFFER_TOO_SMALL) == 0);
        CHECK(zcl_read_attrs_unicast(&set, request, length, response, 5, &info) == ZCL_CODEC_OK);
        CHECK(info.length == 5 && info.command_id == 0x0b && info.requested_count == 0 && info.returned_count == 0);
        CHECK(response[0] == 0x18 && response[1] == 0x5a && response[2] == 0x0b);
        CHECK(response[3] == 0 && response[4] == 0x80);
    }
    request[0] = 0x10;
    CHECK(zcl_read_attrs_unicast(&set, request, 4, response, 100, &info) == ZCL_CODEC_OK);
    CHECK(info.command_id == 0x0b && response[4] == 0x80);
    request[0] = 8;
    CHECK(failure(5, 100, ZCL_CODEC_UNSUPPORTED_CONTEXT) == 0);
    request[0] = 4;
    request[1] = 0x78;
    request[2] = 0x56;
    request[3] = 0x5a;
    request[4] = 0;
    CHECK(failure(7, 100, ZCL_CODEC_UNSUPPORTED_CONTEXT) == 0);
    set.manufacturer_specific = 1;
    set.manufacturer_code = 0x5679;
    CHECK(failure(7, 100, ZCL_CODEC_UNSUPPORTED_CONTEXT) == 0);
    set.manufacturer_code = 0x5678;
    CHECK(zcl_read_attrs_unicast(&set, request, 6, response, 7, &info) == ZCL_CODEC_OK);
    CHECK(info.length == 7 && info.command_id == 0x0b && response[0] == 0x1c);
    CHECK(response[1] == 0x78 && response[2] == 0x56 && response[3] == 0x5a && response[5] == 0 && response[6] == 0x80);
    request[0] = 0xe4;
    CHECK(failure(7, 100, ZCL_CODEC_UNSUPPORTED_LAYOUT) == 0);
    base();
    request[0] = 1;
    CHECK(failure(5, 100, ZCL_CODEC_UNSUPPORTED_COMMAND) == 0);
    request[0] = 2;
    CHECK(failure(5, 100, ZCL_CODEC_UNSUPPORTED_FRAME_TYPE) == 0);
    request[0] = 0;
    request[2] = 0x0b;
    CHECK(failure(5, 100, ZCL_CODEC_UNSUPPORTED_COMMAND) == 0);
    base();
    attributes[1].id = attributes[0].id;
    CHECK(failure(5, 100, ZCL_CODEC_INVALID_TABLE) == 0);
    base();
    attributes[4].readable = 2;
    CHECK(failure(5, 100, ZCL_CODEC_INVALID_TABLE) == 0);
    base();
    set.count = ZCL_ATTRIBUTE_MAX_COUNT + 1u;
    CHECK(failure(5, 100, ZCL_CODEC_INVALID_TABLE) == 0);
    set.count = ZCL_ATTRIBUTE_MAX_COUNT;
    for (i = 0; i < set.count; i++) {
        attributes[i].id = i;
        attributes[i].readable = 0;
    }
    request[3] = ZCL_ATTRIBUTE_MAX_COUNT - 1u;
    request[4] = 0;
    CHECK(zcl_read_attrs_unicast(&set, request, 5, response, 100, &info) == ZCL_CODEC_OK);
    CHECK(info.length == 6 && response[3] == ZCL_ATTRIBUTE_MAX_COUNT - 1u && response[5] == 0x7e);
    set.attributes = NULL;
    CHECK(failure(5, 100, ZCL_CODEC_INVALID_TABLE) == 0);
    base();
    set.side = 2;
    CHECK(failure(5, 100, ZCL_CODEC_INVALID_TABLE) == 0);
    set.side = 0;
    set.manufacturer_specific = 2;
    CHECK(failure(5, 100, ZCL_CODEC_INVALID_TABLE) == 0);
    base();
    attributes[1].value.type = 0xff;
    request[5] = 0;
    request[6] = 0x20;
    CHECK(failure(7, 100, ZCL_CODEC_UNSUPPORTED_DATA_TYPE) == 0);
    CHECK(zcl_read_attrs_unicast(&set, request, 6, response, 100, &info) == ZCL_CODEC_OK);
    CHECK(info.command_id == 0x0b && response[4] == 0x80);
    attributes[0].value.data = NULL;
    CHECK(failure(5, 100, ZCL_CODEC_INVALID_ARGUMENT) == 0);
    base();
    raw[0] = 2;
    attributes[0].value.type = ZCL_TYPE_BOOLEAN;
    attributes[0].value.data = raw;
    attributes[0].value.data_length = 1;
    CHECK(failure(5, 6, ZCL_CODEC_INVALID_VALUE) == 0);
    CHECK(zcl_read_attrs_unicast(NULL, request, 5, response, 100, &info) == ZCL_CODEC_INVALID_ARGUMENT);
    CHECK(zcl_read_attrs_unicast(&set, NULL, 5, response, 100, &info) == ZCL_CODEC_INVALID_ARGUMENT);
    CHECK(zcl_read_attrs_unicast(&set, request, 5, NULL, 100, &info) == ZCL_CODEC_INVALID_ARGUMENT);
    CHECK(zcl_read_attrs_unicast(&set, request, 5, response, 100, NULL) == ZCL_CODEC_INVALID_ARGUMENT);
    CHECK(filled(response, sizeof(response), 0xc7) && filled((const uint8_t *)&info, sizeof(info), 0xa5));
    return 0;
}

static uint16_t value_cases(void)
{
    uint16_t type;
    uint8_t size, supported;
    base();
    set.count = 1;
    memset(raw, 0, sizeof(raw));
    attributes[0].value.data = raw;
    for (type = 0; type < 256; type++) {
        size = 0;
        supported = 1;
        if (type >= 0x08 && type <= 0x0f)
            size = (uint8_t)(type - 7u);
        else if (type >= 0x18 && type <= 0x2f)
            size = (uint8_t)((type & 7u) + 1u);
        else if (type == 0x10 || type == 0x30)
            size = 1;
        else if (type == 0x31)
            size = 2;
        else if (type == 0x41 || type == 0x42)
            size = 3;
        else if (type != 0)
            supported = 0;
        attributes[0].value.type = (uint8_t)type;
        attributes[0].value.data_length = size;
        if (!supported) {
            CHECK(failure(5, 100, ZCL_CODEC_UNSUPPORTED_DATA_TYPE) == 0);
        } else {
            CHECK(zcl_read_attrs_unicast(&set, request, 5, response, 100, &info) == ZCL_CODEC_OK);
            CHECK(info.returned_count == 1 && info.requested_count == 1 && info.command_id == 1);
            CHECK(response[3] == 0x34 && response[4] == 0x12 && response[5] == 0 && response[6] == type);
            CHECK(info.length == 7u + size + (type == 0x41 || type == 0x42 ? 1u : 0u));
            if (type == 0x41 || type == 0x42)
                CHECK(response[7] == size && filled(response + 8, size, 0));
            else
                CHECK(filled(response + 7, size, 0));
        }
    }
    return 0;
}

static uint16_t run_tests(void)
{
    uint16_t result = golden_cases();
    if (result == 0) result = capacity_cases();
    if (result == 0) result = rejection_cases();
    if (result == 0) result = value_cases();
    return result;
}

#ifndef CC2530_HOST_TEST
volatile MCU_XDATA MCU_AT(0x1e00) uint8_t zcl_attributes_test_result[8];

void main(void)
{
    uint16_t result = run_tests();
    zcl_attributes_test_result[0] = 'Z';
    zcl_attributes_test_result[1] = 'C';
    zcl_attributes_test_result[2] = 'A';
    zcl_attributes_test_result[3] = '1';
    zcl_attributes_test_result[4] = 1;
    zcl_attributes_test_result[5] = 8;
    zcl_attributes_test_result[6] = (uint8_t)result;
    zcl_attributes_test_result[7] = (uint8_t)(result >> 8);
    __asm
        .globl _zcl_attributes_test_done
    _zcl_attributes_test_done:
        nop
    __endasm;
    for (;;) {}
}
#else
#include <stdio.h>
#include <stdlib.h>

static uint16_t model_and_header_matrix(void)
{
    zcl_attribute_t *table;
    uint8_t *data;
    uint32_t n;
    size_t table_bytes;
    unsigned count, i, offset, accepted = 0;
    zcl_codec_result_t status;
    data = malloc(2);
    CHECK(data != NULL);
    data[0] = 0xef;
    data[1] = 0xbe;
    for (count = 0; count <= ZCL_ATTRIBUTE_MAX_COUNT; count++) {
        base();
        table_bytes = count ? count * sizeof(*table) : 1u;
        table = malloc(table_bytes);
        CHECK(table != NULL);
        memset(table, 0, table_bytes);
        for (i = 0; i < count; i++) {
            table[i].id = (uint16_t)(0x1000u + i);
            table[i].readable = 1;
            table[i].value.type = ZCL_TYPE_UINT16;
            table[i].value.data = data;
            table[i].value.data_length = 2;
        }
        set.attributes = table;
        set.count = (uint8_t)count;
        for (n = 0; n < (count == ZCL_ATTRIBUTE_MAX_COUNT ? 65536UL : count + 1u); n++) {
            request[3] = (uint8_t)n;
            request[4] = count == ZCL_ATTRIBUTE_MAX_COUNT ? (uint8_t)(n >> 8) : 0x10;
            CHECK(zcl_read_attrs_unicast(&set, request, 5, response, 100, &info) == ZCL_CODEC_OK);
            CHECK(info.command_id == 1 && info.requested_count == 1 && info.returned_count == 1);
            CHECK(response[3] == request[3] && response[4] == request[4]);
            if (count == ZCL_ATTRIBUTE_MAX_COUNT ? n >= 0x1000u && n < 0x1010u : n < count) {
                CHECK(info.length == 9 && response[5] == 0 && response[6] == 0x21 && response[7] == 0xef && response[8] == 0xbe);
            } else {
                CHECK(info.length == 6 && response[5] == 0x86);
            }
        }
        free(table);
    }
    free(data);
    base();
    for (n = 0; n < 256; n++) {
        request[0] = (uint8_t)n;
        set.side = (uint8_t)((n >> 3) & 1u);
        set.manufacturer_specific = (uint8_t)((n >> 2) & 1u);
        set.manufacturer_code = 0x5678;
        offset = set.manufacturer_specific ? 5u : 3u;
        request[1] = 0x78;
        request[2] = 0x56;
        request[offset - 2u] = 0x5a;
        request[offset - 1u] = 0;
        request[offset] = 0x34;
        request[offset + 1u] = 0x12;
        memset(response, 0xc7, sizeof(response));
        memset(&info, 0xa5, sizeof(info));
        status = zcl_read_attrs_unicast(&set, request, (uint16_t)(offset + 2u), response, 100, &info);
        if ((n & 3u) == 0 && (!(n & 4u) || !(n & 0xe0u))) {
            CHECK(status == ZCL_CODEC_OK && info.length == offset + 6u);
            CHECK(response[0] == (uint8_t)(((n & 0x1cu) ^ 8u) | 0x10u));
            accepted++;
        } else {
            CHECK(status != ZCL_CODEC_OK && filled(response, sizeof(response), 0xc7));
            CHECK(filled((const uint8_t *)&info, sizeof(info), 0xa5));
        }
    }
    CHECK(accepted == 36);
    base();
    for (n = 0; n < 256; n++) {
        request[2] = (uint8_t)n;
        if (n == 0) {
            CHECK(zcl_read_attrs_unicast(&set, request, 5, response, 100, &info) == ZCL_CODEC_OK);
        } else {
            CHECK(failure(5, 100, ZCL_CODEC_UNSUPPORTED_COMMAND) == 0);
        }
        request[2] = 0;
        request[1] = (uint8_t)n;
        CHECK(zcl_read_attrs_unicast(&set, request, 5, response, 100, &info) == ZCL_CODEC_OK);
        CHECK(response[1] == n && response[2] == 1);
    }
    set.manufacturer_specific = 1;
    request[0] = 4;
    request[3] = 0x5a;
    request[4] = 0;
    request[5] = 0x34;
    request[6] = 0x12;
    for (n = 0; n <= 65535UL; n++) {
        set.manufacturer_code = (uint16_t)n;
        request[1] = (uint8_t)n;
        request[2] = (uint8_t)(n >> 8);
        CHECK(zcl_read_attrs_unicast(&set, request, 7, response, 100, &info) == ZCL_CODEC_OK);
        CHECK(info.length == 11 && response[0] == 0x1c && response[1] == request[1] && response[2] == request[2]);
        set.manufacturer_code ^= 1u;
        CHECK(failure(7, 100, ZCL_CODEC_UNSUPPORTED_CONTEXT) == 0);
    }
    return 0;
}

static uint16_t exact_bounds(void)
{
    uint8_t *input, *output;
    zcl_read_info_t *result;
    unsigned manufacturer, size, capacity, offset, count, requested, used, i;
    uint32_t n;
    zcl_codec_result_t expected;
    result = malloc(sizeof(*result));
    CHECK(result != NULL);
    for (manufacturer = 0; manufacturer < 2; manufacturer++) {
        base();
        set.attributes = NULL;
        set.count = 0;
        set.manufacturer_specific = (uint8_t)manufacturer;
        set.manufacturer_code = 0x5678;
        offset = manufacturer ? 5u : 3u;
        for (size = 0; size <= 101; size++) {
            input = malloc(size ? size : 1u);
            CHECK(input != NULL);
            memset(request, 0xee, sizeof(request));
            request[0] = manufacturer ? 4 : 0;
            request[1] = 0x78;
            request[2] = 0x56;
            request[offset - 2u] = 0x5a;
            request[offset - 1u] = 0;
            memcpy(input, request, size);
            for (capacity = 0; capacity <= 102; capacity++) {
                output = malloc(capacity ? capacity : 1u);
                CHECK(output != NULL);
                memset(output, 0xc7, capacity);
                memset(result, 0xa5, sizeof(*result));
                requested = size >= offset ? (size - offset) / 2u : 0;
                expected = size > 100 ? ZCL_CODEC_TOO_LONG : size < offset ? ZCL_CODEC_TRUNCATED : ZCL_CODEC_OK;
                if (expected == ZCL_CODEC_OK && capacity < offset + ((size == offset || (size - offset) % 2u) ? 2u : 3u))
                    expected = ZCL_CODEC_BUFFER_TOO_SMALL;
                CHECK(zcl_read_attrs_unicast(&set, input, (uint16_t)size, output, (uint16_t)capacity, result) == expected);
                CHECK(memcmp(input, request, size) == 0);
                if (expected != ZCL_CODEC_OK) {
                    CHECK(filled(output, (uint16_t)capacity, 0xc7) && filled((const uint8_t *)result, sizeof(*result), 0xa5));
                } else {
                    CHECK(output[0] == (manufacturer ? 0x1cu : 0x18u) && output[offset - 2u] == 0x5a);
                    if (manufacturer)
                        CHECK(output[1] == 0x78 && output[2] == 0x56);
                    if (size == offset || (size - offset) % 2u) {
                        CHECK(result->command_id == 0x0b && result->requested_count == 0 && result->returned_count == 0);
                        CHECK(output[offset - 1u] == 0x0b && output[offset] == 0 && output[offset + 1u] == 0x80);
                        used = offset + 2u;
                    } else {
                        count = ((capacity < 100 ? capacity : 100u) - offset) / 3u;
                        if (count > requested) count = requested;
                        CHECK(result->command_id == 1 && result->requested_count == requested && result->returned_count == count);
                        CHECK(output[offset - 1u] == 1);
                        for (i = 0; i < count; i++)
                            CHECK(output[offset + i * 3u] == 0xee && output[offset + i * 3u + 1u] == 0xee && output[offset + i * 3u + 2u] == 0x86);
                        used = offset + 3u * count;
                    }
                    CHECK(result->length == used && filled(output + used, (uint16_t)(capacity - used), 0xc7));
                }
                free(output);
            }
            free(input);
        }
    }
    base();
    set.attributes = NULL;
    set.count = 0;
    input = malloc(65535);
    output = malloc(65535);
    CHECK(input != NULL && output != NULL);
    memset(input, 0xee, 65535);
    input[0] = 0;
    input[1] = 0x5a;
    input[2] = 0;
    memset(output, 0xc7, 65535);
    for (n = 0; n <= 65535UL; n++) {
        memset(result, 0xa5, sizeof(*result));
        expected = n < 6 ? ZCL_CODEC_BUFFER_TOO_SMALL : ZCL_CODEC_OK;
        CHECK(zcl_read_attrs_unicast(&set, input, 5, output, (uint16_t)n, result) == expected);
        if (expected == ZCL_CODEC_OK)
            CHECK(result->length == 6 && result->requested_count == 1 && result->returned_count == 1 && output[5] == 0x86);
        else
            CHECK(filled(output, 6, 0xc7) && filled((const uint8_t *)result, sizeof(*result), 0xa5));
        if (n > 100) {
            memset(response, 0xc7, sizeof(response));
            memset(result, 0xa5, sizeof(*result));
            CHECK(zcl_read_attrs_unicast(&set, input, (uint16_t)n, response, 100, result) == ZCL_CODEC_TOO_LONG);
            CHECK(filled(response, sizeof(response), 0xc7) && filled((const uint8_t *)result, sizeof(*result), 0xa5));
        }
    }
    CHECK(filled(output + 6, 65529, 0xc7));
    free(input);
    free(output);
    free(result);
    return 0;
}

int main(void)
{
    uint16_t result = run_tests();
    if (result == 0) result = model_and_header_matrix();
    if (result == 0) result = exact_bounds();
    if (result != 0) {
        fprintf(stderr, "ZCL Read Attributes test failed at C source line %u\n", result);
        return 1;
    }
    puts("ZCL Read Attributes: shared vectors, namespace/access/space rules and exact host bounds PASS.");
    return 0;
}
#endif
