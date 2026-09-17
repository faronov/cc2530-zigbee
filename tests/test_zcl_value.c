/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "zcl_wire.h"
#include "cc2530_mmio.h"

#include <stddef.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) return (uint16_t)__LINE__; } while (0)

/* Type, width, non-value shape: 0=none, 1=FF, 2=sign bit, 3=string. */
static const MCU_CODE uint8_t types[][3] = {
    {0x00, 0, 0},
    {0x08, 1, 0}, {0x09, 2, 0}, {0x0a, 3, 0}, {0x0b, 4, 0},
    {0x0c, 5, 0}, {0x0d, 6, 0}, {0x0e, 7, 0}, {0x0f, 8, 0},
    {0x10, 1, 1},
    {0x18, 1, 0}, {0x19, 2, 0}, {0x1a, 3, 0}, {0x1b, 4, 0},
    {0x1c, 5, 0}, {0x1d, 6, 0}, {0x1e, 7, 0}, {0x1f, 8, 0},
    {0x20, 1, 1}, {0x21, 2, 1}, {0x22, 3, 1}, {0x23, 4, 1},
    {0x24, 5, 1}, {0x25, 6, 1}, {0x26, 7, 1}, {0x27, 8, 1},
    {0x28, 1, 2}, {0x29, 2, 2}, {0x2a, 3, 2}, {0x2b, 4, 2},
    {0x2c, 5, 2}, {0x2d, 6, 2}, {0x2e, 7, 2}, {0x2f, 8, 2},
    {0x30, 1, 1}, {0x31, 2, 1}, {0x41, 0, 3}, {0x42, 0, 3}
};
static const MCU_CODE uint8_t data[255] = {0x78, 0x56, 0x34, 0x12, 0x85, 0xff, 0xa9, 0x69};
static const MCU_CODE zcl_value_t code_value = {0x21, 0, data, 2};
static zcl_value_t value;
static zcl_value_info_t decoded, saved;
static uint8_t body[257], sample[8], length;

#if defined(__SDCC)
volatile MCU_XDATA uint8_t zcl_value_test_phase;
#endif

static uint8_t filled(const uint8_t *bytes, uint16_t size, uint8_t byte)
{
    while (size != 0) {
        if (*bytes++ != byte)
            return 0;
        size--;
    }
    return 1;
}

static uint16_t decode_failure(uint8_t type, const uint8_t *input, uint16_t size, zcl_codec_result_t expected)
{
    memset(&decoded, 0xa5, sizeof(decoded));
    memcpy(&saved, &decoded, sizeof(saved));
    CHECK(zcl_value_decode(type, input, size, &decoded) == expected);
    CHECK(memcmp(&decoded, &saved, sizeof(decoded)) == 0);
    return 0;
}

static uint16_t encode_failure(zcl_codec_result_t expected)
{
    memset(body, 0xc7, sizeof(body));
    length = 0xa5;
    CHECK(zcl_value_encode(&value, body + 1, 255, &length) == expected);
    CHECK(length == 0xa5 && filled(body, sizeof(body), 0xc7));
    return 0;
}

static uint16_t fixed_cases(void)
{
    volatile uint8_t row, variant, width, n, expected;

    CHECK(zcl_value_encode(&code_value, body, sizeof(body), &length) == ZCL_CODEC_OK);
    CHECK(length == 2 && body[0] == 0x78 && body[1] == 0x56);
    CHECK(zcl_value_decode(ZCL_TYPE_UINT16, data, sizeof(data), &decoded) == ZCL_CODEC_OK);
    CHECK(decoded.encoded_length == 2 && decoded.data_length == 2 && decoded.non_value_pattern == 0);
    for (row = 0; row < 36; row++) {
        memset(&value, 0, sizeof(value));
        value.type = types[row][0];
        width = types[row][1];
        value.data = sample;
        value.data_length = width;
        for (variant = 0; variant < 4; variant++) {
            memcpy(sample, data, sizeof(sample));
            expected = 0;
            if (variant == 1) {
                memset(sample, 0xff, sizeof(sample));
                expected = types[row][2] == 1;
            } else if (variant >= 2) {
                memset(sample, 0, sizeof(sample));
                if (variant == 3 && width != 0) {
                    sample[width - 1u] = 0x80;
                    expected = types[row][2] == 2;
                }
            }
            if (value.type == ZCL_TYPE_BOOLEAN && (variant == 0 || variant == 3))
                sample[0] = 1;
            memset(body, 0xc7, sizeof(body));
            CHECK(zcl_value_encode(&value, body + 1, 255, &length) == ZCL_CODEC_OK);
            CHECK(length == width && body[0] == 0xc7 && memcmp(body + 1, sample, width) == 0);
            CHECK(filled(body + width + 1u, sizeof(body) - width - 1u, 0xc7));
            CHECK(zcl_value_decode(value.type, body + 1, width, &decoded) == ZCL_CODEC_OK);
            CHECK(decoded.type == value.type && decoded.data_offset == 0);
            CHECK(decoded.data_length == width && decoded.encoded_length == width);
            CHECK(decoded.non_value_pattern == expected);
            for (n = 0; n < width; n++)
                CHECK(decode_failure(value.type, body + 1, n, ZCL_CODEC_TRUNCATED) == 0);
            for (n = 0; n < width; n++) {
                memset(body, 0xc7, sizeof(body));
                length = 0xa5;
                CHECK(zcl_value_encode(&value, body + 1, n, &length) == ZCL_CODEC_BUFFER_TOO_SMALL);
                CHECK(length == 0xa5 && filled(body, sizeof(body), 0xc7));
            }
        }
        value.data = data;
        value.data_length = (uint16_t)(width + 1u);
        CHECK(encode_failure(ZCL_CODEC_INVALID_VALUE) == 0);
        value.data_length = width;
        value.string_non_value = 1;
        CHECK(encode_failure(ZCL_CODEC_INVALID_VALUE) == 0);
    }
    return 0;
}

static uint16_t string_cases(uint8_t type)
{
    volatile uint16_t n;

    memset(&value, 0, sizeof(value));
    value.data = data;
    value.type = type;
    for (n = 0; n <= 254; n++) {
        value.data_length = n;
        memset(body, 0xc7, sizeof(body));
        CHECK(zcl_value_encode(&value, body + 1, 255, &length) == ZCL_CODEC_OK);
        CHECK(length == n + 1u && body[0] == 0xc7 && body[1] == n);
        CHECK(memcmp(body + 2, data, n) == 0);
        CHECK(filled(body + n + 2u, sizeof(body) - n - 2u, 0xc7));
        CHECK(zcl_value_decode(type, body + 1, length, &decoded) == ZCL_CODEC_OK);
        CHECK(decoded.data_offset == 1 && decoded.data_length == n && decoded.encoded_length == length);
        CHECK(decoded.non_value_pattern == 0);
        CHECK(decode_failure(type, body + 1, n, ZCL_CODEC_TRUNCATED) == 0);
    }
    value.data_length = 254;
    for (n = 0; n < 255; n++) {
        memset(body, 0xc7, sizeof(body));
        length = 0xa5;
        CHECK(zcl_value_encode(&value, body + 1, n, &length) == ZCL_CODEC_BUFFER_TOO_SMALL);
        CHECK(length == 0xa5 && filled(body, sizeof(body), 0xc7));
    }
    value.data_length = 255;
    CHECK(encode_failure(ZCL_CODEC_TOO_LONG) == 0);
    value.data_length = 0;
    value.data = NULL;
    for (n = 0; n < 2; n++) {
        value.string_non_value = (uint8_t)n;
        memset(body, 0xc7, sizeof(body));
        CHECK(zcl_value_encode(&value, body + 1, 1, &length) == ZCL_CODEC_OK);
        CHECK(length == 1 && body[1] == (n ? 0xffu : 0u));
        CHECK(body[0] == 0xc7 && filled(body + 2, sizeof(body) - 2u, 0xc7));
        CHECK(zcl_value_decode(type, body + 1, 255, &decoded) == ZCL_CODEC_OK);
        CHECK(decoded.encoded_length == 1 && decoded.data_length == 0 && decoded.non_value_pattern == n);
    }
    value.string_non_value = 2;
    CHECK(encode_failure(ZCL_CODEC_INVALID_VALUE) == 0);
    value.string_non_value = 1;
    value.data = data;
    value.data_length = 1;
    CHECK(encode_failure(ZCL_CODEC_INVALID_VALUE) == 0);
    return 0;
}

static uint16_t rejected_cases(void)
{
    volatile uint16_t type, byte;
    uint8_t row, supported;

    memset(&value, 0, sizeof(value));
    for (type = 0; type < 256; type++) {
        supported = 0;
        for (row = 0; row < sizeof(types) / sizeof(types[0]); row++)
            if (types[row][0] == type)
                supported = 1;
        if (supported)
            continue;
        value.type = (uint8_t)type;
        CHECK(encode_failure(ZCL_CODEC_UNSUPPORTED_DATA_TYPE) == 0);
        CHECK(decode_failure((uint8_t)type, body, 0, ZCL_CODEC_UNSUPPORTED_DATA_TYPE) == 0);
        CHECK(decode_failure((uint8_t)type, body, 255, ZCL_CODEC_UNSUPPORTED_DATA_TYPE) == 0);
    }
    value.type = ZCL_TYPE_BOOLEAN;
    value.data = sample;
    value.data_length = 1;
    for (byte = 0; byte < 256; byte++) {
        sample[0] = (uint8_t)byte;
        if (byte < 2 || byte == 255) {
            CHECK(zcl_value_encode(&value, body, sizeof(body), &length) == ZCL_CODEC_OK);
            CHECK(length == 1 && body[0] == byte);
            CHECK(zcl_value_decode(value.type, sample, 1, &decoded) == ZCL_CODEC_OK);
            CHECK(decoded.non_value_pattern == (byte == 255));
        } else {
            CHECK(encode_failure(ZCL_CODEC_INVALID_VALUE) == 0);
            CHECK(decode_failure(value.type, sample, 1, ZCL_CODEC_INVALID_VALUE) == 0);
        }
    }
    memset(body, 0xc7, sizeof(body));
    length = 0xa5;
    CHECK(zcl_value_encode(NULL, body, sizeof(body), &length) == ZCL_CODEC_INVALID_ARGUMENT);
    CHECK(zcl_value_encode(&value, NULL, 0, &length) == ZCL_CODEC_INVALID_ARGUMENT);
    CHECK(length == 0xa5);
    CHECK(zcl_value_encode(&value, body, sizeof(body), NULL) == ZCL_CODEC_INVALID_ARGUMENT);
    CHECK(filled(body, sizeof(body), 0xc7));
    value.data = NULL;
    CHECK(encode_failure(ZCL_CODEC_INVALID_ARGUMENT) == 0);
    CHECK(decode_failure(0, NULL, 0, ZCL_CODEC_INVALID_ARGUMENT) == 0);
    CHECK(zcl_value_decode(0, body, 0, NULL) == ZCL_CODEC_INVALID_ARGUMENT);
    value.type = ZCL_TYPE_NO_DATA;
    value.data_length = 0;
    CHECK(zcl_value_encode(&value, body, 0, &length) == ZCL_CODEC_OK);
    CHECK(length == 0 && filled(body, sizeof(body), 0xc7));
    CHECK(zcl_value_decode(0, body, 0, &decoded) == ZCL_CODEC_OK);
    CHECK(decoded.encoded_length == 0 && decoded.data_length == 0 && decoded.non_value_pattern == 0);
    return 0;
}

static uint16_t self_test(void)
{
#if defined(__SDCC)
    switch (zcl_value_test_phase) {
    case 0: return fixed_cases();
    case 1: return string_cases(0x41);
    case 2: return string_cases(0x42);
    case 3: return rejected_cases();
    default: return (uint16_t)__LINE__;
    }
#else
    uint16_t result = fixed_cases();
    if (result == 0)
        result = string_cases(0x41);
    if (result == 0)
        result = string_cases(0x42);
    if (result == 0)
        result = rejected_cases();
    return result;
#endif
}

#if defined(__SDCC)
volatile MCU_XDATA MCU_AT(0x1e00) uint8_t zcl_value_test_result[8];

void main(void)
{
    uint16_t result = self_test();
    zcl_value_test_result[0] = 'Z';
    zcl_value_test_result[1] = 'C';
    zcl_value_test_result[2] = 'V';
    zcl_value_test_result[3] = (uint8_t)('0' + zcl_value_test_phase);
    zcl_value_test_result[4] = 1;
    zcl_value_test_result[5] = 8;
    zcl_value_test_result[6] = (uint8_t)result;
    zcl_value_test_result[7] = (uint8_t)(result >> 8);
    __asm
        .globl _zcl_value_test_done
    _zcl_value_test_done:
        nop
    __endasm;
    for (;;) {
    }
}
#else
#include <stdio.h>
#include <stdlib.h>

static uint8_t pattern(unsigned row, const uint8_t *bytes, unsigned width)
{
    unsigned i;
    if (types[row][2] == 0)
        return 0;
    for (i = 0; i < width; i++)
        if (bytes[i] != (types[row][2] == 1 ? 255u : i + 1u == width ? 128u : 0u))
            return 0;
    return 1;
}

static uint16_t exhaustive_values(void)
{
    zcl_value_t item;
    uint32_t scalar;
    unsigned row, byte, position, width;
    uint8_t input[255], output[257];
    int valid;

    memset(&item, 0, sizeof(item));
    item.data = input;
    for (row = 0; row < 36; row++) {
        item.type = types[row][0];
        width = types[row][1];
        item.data_length = (uint16_t)width;
        for (position = 0; position < width; position++) {
            for (byte = 0; byte < 256; byte++) {
                memset(input, types[row][2] == 2 ? 0 : 0xff, sizeof(input));
                if (types[row][2] == 2)
                    input[width - 1u] = 0x80;
                input[position] = (uint8_t)byte;
                if (item.type == 0x10 && byte > 1 && byte != 255)
                    continue;
                memset(output, 0xc7, sizeof(output));
                CHECK(zcl_value_encode(&item, output + 1, 255, &length) == ZCL_CODEC_OK);
                CHECK(length == width && output[0] == 0xc7 && memcmp(output + 1, input, width) == 0);
                CHECK(filled(output + width + 1u, (uint16_t)(sizeof(output) - width - 1u), 0xc7));
                CHECK(zcl_value_decode(item.type, input, 255, &decoded) == ZCL_CODEC_OK);
                CHECK(decoded.data_length == width && decoded.encoded_length == width);
                CHECK(decoded.non_value_pattern == pattern(row, input, width));
            }
        }
        if (width <= 2) {
            for (scalar = 0; scalar <= 65535UL; scalar++) {
                input[0] = (uint8_t)scalar;
                input[1] = (uint8_t)(scalar >> 8);
                if (item.type == 0x10 && input[0] > 1 && input[0] != 255)
                    continue;
                CHECK(zcl_value_decode(item.type, input, 2, &decoded) == ZCL_CODEC_OK);
                CHECK(decoded.non_value_pattern == pattern(row, input, width));
                CHECK(zcl_value_encode(&item, output, sizeof(output), &length) == ZCL_CODEC_OK);
                CHECK(length == width && memcmp(output, input, width) == 0);
            }
        }
    }
    for (row = 36; row < 38; row++) {
        item.type = types[row][0];
        item.data_length = 254;
        for (position = 0; position < 254; position++) {
            for (byte = 0; byte < 256; byte++) {
                memset(input, 0x69, sizeof(input));
                input[position] = (uint8_t)byte;
                memset(output, 0xc7, sizeof(output));
                CHECK(zcl_value_encode(&item, output + 1, 255, &length) == ZCL_CODEC_OK);
                CHECK(length == 255 && output[0] == 0xc7 && output[256] == 0xc7 && output[1] == 254);
                CHECK(memcmp(output + 2, input, 254) == 0);
                CHECK(zcl_value_decode(item.type, output + 1, 255, &decoded) == ZCL_CODEC_OK);
                CHECK(decoded.data_length == 254 && decoded.encoded_length == 255 && decoded.non_value_pattern == 0);
            }
        }
    }
    memset(input, 1, sizeof(input));
    for (row = 0; row < 38; row++) {
        item.type = types[row][0];
        item.data_length = types[row][1];
        for (byte = 0; byte < 256; byte++) {
            item.string_non_value = (uint8_t)byte;
            valid = byte == 0 || (row >= 36 && byte == 1);
            memset(output, 0xc7, sizeof(output));
            length = 0xa5;
            CHECK((zcl_value_encode(&item, output, sizeof(output), &length) == ZCL_CODEC_OK) == valid);
            if (valid) {
                CHECK(length == (row >= 36 ? 1u : types[row][1]));
                CHECK(zcl_value_decode(item.type, output, length, &decoded) == ZCL_CODEC_OK);
                if (row >= 36)
                    CHECK(decoded.data_length == 0 && decoded.non_value_pattern == byte);
            } else {
                CHECK(length == 0xa5 && filled(output, sizeof(output), 0xc7));
            }
        }
    }
    return 0;
}

static uint16_t exact_bounds(void)
{
    uint8_t *input, *output, *large, *exact_data, written;
    zcl_value_t *exact_value;
    zcl_value_info_t *result;
    zcl_codec_result_t expected;
    uint32_t n;
    unsigned row, width, size, capacity, string, data_size;

    exact_value = malloc(sizeof(*exact_value));
    result = malloc(sizeof(*result));
    large = malloc(65535);
    CHECK(exact_value != NULL && result != NULL && large != NULL);
    for (row = 0; row < 38; row++) {
        memset(&value, 0, sizeof(value));
        value.type = types[row][0];
        string = row >= 36;
        width = string ? 254u : types[row][1];
        value.data = data;
        value.data_length = (uint16_t)width;
        if (value.type == 0x10) {
            sample[0] = 1;
            value.data = sample;
        }
        CHECK(zcl_value_encode(&value, body, sizeof(body), &length) == ZCL_CODEC_OK);
        for (size = 0; size <= width + string + 1u; size++) {
            input = malloc(size ? size : 1u);
            CHECK(input != NULL);
            memcpy(input, body, size);
            memset(result, 0xa5, sizeof(*result));
            memcpy(&saved, result, sizeof(saved));
            expected = size < width + string ? ZCL_CODEC_TRUNCATED : ZCL_CODEC_OK;
            CHECK(zcl_value_decode(value.type, input, (uint16_t)size, result) == expected);
            if (expected != ZCL_CODEC_OK)
                CHECK(memcmp(result, &saved, sizeof(saved)) == 0);
            else
                CHECK(result->data_length == width && result->encoded_length == width + string);
            CHECK(memcmp(input, body, size) == 0);
            free(input);
        }
        for (data_size = string ? 0 : width; data_size <= width; data_size++) {
            exact_data = malloc(data_size ? data_size : 1u);
            CHECK(exact_data != NULL);
            memcpy(exact_data, value.data, data_size);
            value.data_length = (uint16_t)data_size;
            memcpy(exact_value, &value, sizeof(value));
            exact_value->data = exact_data;
            for (capacity = 0; capacity <= data_size + string + 1u; capacity++) {
                output = malloc(capacity ? capacity : 1u);
                CHECK(output != NULL);
                memset(output, 0xc7, capacity ? capacity : 1u);
                written = 0xa5;
                expected = capacity < data_size + string ? ZCL_CODEC_BUFFER_TOO_SMALL : ZCL_CODEC_OK;
                CHECK(zcl_value_encode(exact_value, output, (uint16_t)capacity, &written) == expected);
                if (expected != ZCL_CODEC_OK) {
                    CHECK(written == 0xa5 && filled(output, (uint16_t)(capacity ? capacity : 1u), 0xc7));
                } else {
                    CHECK(written == data_size + string);
                    if (string)
                        CHECK(output[0] == data_size);
                    CHECK(memcmp(output + string, exact_data, data_size) == 0);
                    CHECK(filled(output + written, (uint16_t)(capacity - written), 0xc7));
                }
                CHECK(memcmp(exact_data, value.data, data_size) == 0);
                CHECK(exact_value->type == value.type && exact_value->data == exact_data
                      && exact_value->data_length == value.data_length && exact_value->string_non_value == 0);
                free(output);
            }
            free(exact_data);
        }
        memset(large, 0x69, 65535);
        memcpy(large, body, width + string);
        for (n = 0; n <= 65535UL; n++) {
            memset(result, 0xa5, sizeof(*result));
            memcpy(&saved, result, sizeof(saved));
            expected = n < width + string ? ZCL_CODEC_TRUNCATED : ZCL_CODEC_OK;
            CHECK(zcl_value_decode(value.type, large, (uint16_t)n, result) == expected);
            if (expected != ZCL_CODEC_OK)
                CHECK(memcmp(result, &saved, sizeof(saved)) == 0);
            else
                CHECK(result->encoded_length == width + string && result->data_length == width);
            value.data_length = (uint16_t)n;
            value.data = large;
            if ((string && n > 254) || (!string && n != width)) {
                expected = string ? ZCL_CODEC_TOO_LONG : ZCL_CODEC_INVALID_VALUE;
                CHECK(encode_failure(expected) == 0);
            }
        }
        value.data_length = (uint16_t)width;
        value.data = data;
        if (value.type == 0x10)
            value.data = sample;
        for (n = 0; n <= 65535UL; n++) {
            memset(large, 0xc7, 256);
            written = 0xa5;
            expected = n < width + string ? ZCL_CODEC_BUFFER_TOO_SMALL : ZCL_CODEC_OK;
            CHECK(zcl_value_encode(&value, large, (uint16_t)n, &written) == expected);
            if (expected == ZCL_CODEC_OK) {
                CHECK(written == width + string && large[written] == 0xc7);
            } else {
                CHECK(written == 0xa5 && filled(large, 256, 0xc7));
            }
        }
    }
    for (row = 36; row < 38; row++) {
        input = malloc(1);
        CHECK(input != NULL);
        input[0] = 0xff;
        CHECK(zcl_value_decode(types[row][0], input, 1, result) == ZCL_CODEC_OK);
        CHECK(result->encoded_length == 1 && result->data_length == 0 && result->non_value_pattern == 1);
        free(input);
    }
    free(large);
    free(exact_value);
    free(result);
    return 0;
}

int main(void)
{
    uint16_t result = self_test();
    if (result == 0)
        result = exhaustive_values();
    if (result == 0)
        result = exact_bounds();
    if (result != 0) {
        fprintf(stderr, "ZCL value test failed at line %u\n", (unsigned)result);
        return 1;
    }
    puts("host ZCL values: 38 types, boolean/non-value/string matrices, uint16 spans and exact buffers PASS");
    return 0;
}
#endif
