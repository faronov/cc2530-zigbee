/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "nwk_frame.h"
#include "cc2530_mmio.h"

#include <stddef.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) return (uint16_t)__LINE__; } while (0)

static const MCU_CODE uint8_t golden[][27] = {
    {0x08, 0x20, 0x78, 0x56, 0x34, 0x12, 0x1e, 0xa5, 0xa9, 0x55, 0x69},
    {0x48, 0x38, 0x78, 0x56, 0x34, 0x12, 0x1e, 0xa5,
     0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
     0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0xa9, 0x55, 0x69}
};
static const MCU_CODE uint8_t payload[109] = {0xa9, 0x55, 0x69};
static const MCU_CODE uint16_t rejected_flags[] = {
    0x0100, 0x0200, 0x0400, 0x4000, 0x8000, 1, 4, 0x40, 0x80
};
static const MCU_CODE uint8_t rejected_status[] = {
    NWK_CODEC_UNSUPPORTED_LAYOUT, NWK_CODEC_UNSUPPORTED_SECURITY, NWK_CODEC_UNSUPPORTED_LAYOUT,
    NWK_CODEC_INVALID_HEADER, NWK_CODEC_INVALID_HEADER, NWK_CODEC_INVALID_HEADER,
    NWK_CODEC_INVALID_HEADER, NWK_CODEC_INVALID_HEADER, NWK_CODEC_INVALID_HEADER
};

static nwk_header_t header;
static nwk_frame_info_t decoded, saved;
static uint8_t body[NWK_FRAME_MAX_BODY + 2u], length;

static uint8_t filled(const uint8_t *bytes, uint16_t size, uint8_t value)
{
    uint16_t i;
    for (i = 0; i < size; i++)
        if (bytes[i] != value)
            return 0;
    return 1;
}

static void base_header(void)
{
    uint8_t i;
    memset(&header, 0, sizeof(header));
    header.version = NWK_FRAME_PROTOCOL_VERSION;
    header.destination = 0x5678;
    header.source = 0x1234;
    header.radius = 0x1e;
    header.sequence = 0xa5;
    for (i = 0; i < 8; i++) {
        header.destination_ieee[i] = (uint8_t)(0x21u + i);
        header.source_ieee[i] = (uint8_t)(0x31u + i);
    }
}

static uint16_t decode_failure(const uint8_t *input, uint16_t size, nwk_codec_result_t expected)
{
    memset(&decoded, 0xa5, sizeof(decoded));
    memcpy(&saved, &decoded, sizeof(saved));
    CHECK(nwk_frame_decode(input, size, &decoded) == expected);
    CHECK(memcmp(&decoded, &saved, sizeof(decoded)) == 0);
    return 0;
}

static uint16_t encode_failure(const nwk_header_t *input, const uint8_t *data,
                               uint16_t size, nwk_codec_result_t expected)
{
    memset(body, 0xc7, sizeof(body));
    length = 0xa5;
    CHECK(nwk_frame_encode(input, data, size, body + 1, 116, &length) == expected);
    CHECK(length == 0xa5 && filled(body, sizeof(body), 0xc7));
    return 0;
}

static uint16_t golden_cases(void)
{
    volatile uint8_t vector, n;
    uint8_t size, offset;
    uint16_t failure;

    for (vector = 0; vector < 2; vector++) {
        offset = vector ? 24u : 8u;
        size = (uint8_t)(offset + 3u);
        CHECK(nwk_frame_decode(golden[vector], size, &decoded) == NWK_CODEC_OK);
        CHECK(decoded.header.type == 0 && decoded.header.version == 2);
        CHECK(decoded.header.flags == (vector ? 0x3800u : 0x2000u));
        CHECK(decoded.header.discover_route == vector);
        CHECK(decoded.header.destination == 0x5678 && decoded.header.source == 0x1234);
        CHECK(decoded.header.radius == 0x1e && decoded.header.sequence == 0xa5);
        CHECK(decoded.payload_offset == offset && decoded.payload_length == 3);
        if (vector) {
            CHECK(memcmp(decoded.header.destination_ieee, golden[1] + 8, 8) == 0);
            CHECK(memcmp(decoded.header.source_ieee, golden[1] + 16, 8) == 0);
        } else {
            CHECK(filled(decoded.header.destination_ieee, 8, 0));
            CHECK(filled(decoded.header.source_ieee, 8, 0));
        }
        header = decoded.header;
        for (n = 0; n <= size + 1u; n++) {
            memset(body, 0xc7, sizeof(body));
            length = 0xa5;
            if (n < size) {
                CHECK(nwk_frame_encode(&header, golden[vector] + offset, 3,
                                       body + 1, n, &length) == NWK_CODEC_BUFFER_TOO_SMALL);
                CHECK(length == 0xa5 && filled(body, sizeof(body), 0xc7));
            } else {
                CHECK(nwk_frame_encode(&header, golden[vector] + offset, 3,
                                       body + 1, n, &length) == NWK_CODEC_OK);
                CHECK(length == size && body[0] == 0xc7);
                CHECK(memcmp(body + 1, golden[vector], size) == 0);
                CHECK(filled(body + size + 1u, sizeof(body) - size - 1u, 0xc7));
            }
        }
        for (n = 0; n <= size; n++) {
            if (n < offset) {
                failure = decode_failure(golden[vector], n, NWK_CODEC_TRUNCATED);
                if (failure != 0)
                    return failure;
            } else {
                CHECK(nwk_frame_decode(golden[vector], n, &decoded) == NWK_CODEC_OK);
                CHECK(decoded.payload_offset == offset && decoded.payload_length == n - offset);
            }
        }
    }
    return 0;
}

static uint16_t shape_cases(void)
{
    volatile uint8_t option, route, broadcast;
    uint8_t size;
    uint16_t failure;

    for (option = 0; option < 8; option++) {
        for (route = 0; route < 2; route++) {
            for (broadcast = 0; broadcast < 2; broadcast++) {
                base_header();
                header.flags = (uint16_t)((uint16_t)option << 11);
                header.discover_route = route;
                if (broadcast)
                    header.destination = 0xffff;
                if (broadcast && (route || (option & 1u))) {
                    failure = encode_failure(&header, NULL, 0, NWK_CODEC_INVALID_HEADER);
                    if (failure != 0)
                        return failure;
                    continue;
                }
                size = (uint8_t)(8u + (option & 1u ? 8u : 0u) + (option & 2u ? 8u : 0u));
                memset(body, 0xc7, sizeof(body));
                memcpy(&saved.header, &header, sizeof(header));
                CHECK(nwk_frame_encode(&header, NULL, 0, body + 1, 116, &length) == NWK_CODEC_OK);
                CHECK(memcmp(&header, &saved.header, sizeof(header)) == 0);
                CHECK(length == size && body[0] == 0xc7);
                CHECK(filled(body + size + 1u, sizeof(body) - size - 1u, 0xc7));
                CHECK(body[1] == 8u + 64u * route && body[2] == 8u * option);
                CHECK(nwk_frame_decode(body + 1, length, &decoded) == NWK_CODEC_OK);
                CHECK(decoded.header.flags == header.flags && decoded.header.discover_route == route);
                CHECK(decoded.header.destination == header.destination && decoded.header.source == header.source);
                CHECK(decoded.payload_offset == size && decoded.payload_length == 0);
                if (option & 1u) {
                    CHECK(memcmp(body + 9, header.destination_ieee, 8) == 0);
                    CHECK(memcmp(decoded.header.destination_ieee, header.destination_ieee, 8) == 0);
                } else {
                    CHECK(filled(decoded.header.destination_ieee, 8, 0));
                }
                if (option & 2u) {
                    CHECK(memcmp(body + 9u + (option & 1u ? 8u : 0u), header.source_ieee, 8) == 0);
                    CHECK(memcmp(decoded.header.source_ieee, header.source_ieee, 8) == 0);
                } else {
                    CHECK(filled(decoded.header.source_ieee, 8, 0));
                }
            }
        }
    }
    return 0;
}

static uint16_t boundary_cases(void)
{
    volatile uint8_t option, n;
    uint8_t size, maximum;
    uint16_t failure;

    for (option = 0; option < 4; option++) {
        base_header();
        header.flags = (uint16_t)((uint16_t)option << 11);
        size = (uint8_t)(8u + (option & 1u ? 8u : 0u) + (option & 2u ? 8u : 0u));
        maximum = (uint8_t)(116u - size);
        for (n = 0; n <= maximum; n++) {
            memset(body, 0xc7, sizeof(body));
            CHECK(nwk_frame_encode(&header, payload, n, body + 1, 116, &length) == NWK_CODEC_OK);
            CHECK(length == size + n && body[0] == 0xc7);
            CHECK(filled(body + length + 1u, sizeof(body) - length - 1u, 0xc7));
            CHECK(memcmp(body + size + 1u, payload, n) == 0);
            CHECK(nwk_frame_decode(body + 1, length, &decoded) == NWK_CODEC_OK);
            CHECK(decoded.payload_offset == size && decoded.payload_length == n);
        }
        for (n = 0; n < 116; n++) {
            memset(body, 0xc7, sizeof(body));
            length = 0xa5;
            CHECK(nwk_frame_encode(&header, payload, maximum, body + 1, n,
                                   &length) == NWK_CODEC_BUFFER_TOO_SMALL);
            CHECK(length == 0xa5 && filled(body, sizeof(body), 0xc7));
        }
        failure = encode_failure(&header, payload, (uint16_t)(maximum + 1u), NWK_CODEC_TOO_LONG);
        if (failure != 0)
            return failure;
    }
    CHECK(decode_failure(body, 117, NWK_CODEC_TOO_LONG) == 0);
    return 0;
}

static uint16_t rejected_cases(void)
{
    volatile uint8_t i;
    uint16_t failure;
    nwk_codec_result_t expected;

    base_header();
    CHECK(encode_failure(NULL, payload, 3, NWK_CODEC_INVALID_ARGUMENT) == 0);
    CHECK(encode_failure(&header, NULL, 1, NWK_CODEC_INVALID_ARGUMENT) == 0);
    CHECK(nwk_frame_encode(&header, payload, 3, NULL, 0, &length) == NWK_CODEC_INVALID_ARGUMENT);
    CHECK(length == 0xa5);
    CHECK(nwk_frame_encode(&header, payload, 3, body + 1, 116, NULL) == NWK_CODEC_INVALID_ARGUMENT);
    CHECK(filled(body, sizeof(body), 0xc7));
    CHECK(decode_failure(NULL, 0, NWK_CODEC_INVALID_ARGUMENT) == 0);
    CHECK(decode_failure(NULL, 8, NWK_CODEC_INVALID_ARGUMENT) == 0);
    CHECK(nwk_frame_decode(body, 8, NULL) == NWK_CODEC_INVALID_ARGUMENT);
    CHECK(filled(body, sizeof(body), 0xc7));
    for (i = 0; i < sizeof(rejected_flags) / sizeof(rejected_flags[0]); i++) {
        header.flags = rejected_flags[i];
        expected = (nwk_codec_result_t)rejected_status[i];
        failure = encode_failure(&header, payload, 3, expected);
        if (failure != 0)
            return failure;
        if (header.flags >= 0x100u) {
            memcpy(body, golden[0], 11);
            body[1] = (uint8_t)(header.flags >> 8);
            failure = decode_failure(body, 11, expected);
            if (failure != 0)
                return failure;
        }
    }
    for (i = 0; i < 8; i++) {
        base_header();
        expected = i == 3 || i == 4 || i == 5 || i == 7 ? NWK_CODEC_OK : NWK_CODEC_INVALID_HEADER;
        header.destination = (uint16_t)(0xfff8u + i);
        memcpy(body, golden[0], 11);
        body[2] = (uint8_t)header.destination;
        body[3] = 0xff;
        if (expected == NWK_CODEC_OK) {
            CHECK(nwk_frame_decode(body, 11, &decoded) == NWK_CODEC_OK);
            CHECK(decoded.header.destination == header.destination);
            CHECK(nwk_frame_encode(&header, payload, 3, body, sizeof(body), &length) == NWK_CODEC_OK);
        } else {
            CHECK(decode_failure(body, 11, expected) == 0);
            CHECK(encode_failure(&header, payload, 3, expected) == 0);
        }
        base_header();
        header.source = (uint16_t)(0xfff8u + i);
        CHECK(encode_failure(&header, payload, 3, NWK_CODEC_INVALID_HEADER) == 0);
        memcpy(body, golden[0], 11);
        body[4] = (uint8_t)header.source;
        body[5] = 0xff;
        CHECK(decode_failure(body, 11, NWK_CODEC_INVALID_HEADER) == 0);
    }
    base_header();
    header.destination = 0;
    header.source = 0xfff7;
    header.radius = 0;
    header.sequence = 0xff;
    CHECK(nwk_frame_encode(&header, NULL, 0, body, sizeof(body), &length) == NWK_CODEC_OK);
    CHECK(nwk_frame_decode(body, length, &decoded) == NWK_CODEC_OK);
    CHECK(decoded.header.source == 0xfff7 && decoded.header.destination == 0);
    CHECK(decoded.header.radius == 0 && decoded.header.sequence == 0xff);
    for (i = 0; i < 2; i++) {
        header.flags = NWK_FLAG_DESTINATION_IEEE | NWK_FLAG_SOURCE_IEEE;
        memset(header.destination_ieee, i ? 0xff : 0, 8);
        memset(header.source_ieee, i ? 0xff : 0, 8);
        CHECK(nwk_frame_encode(&header, NULL, 0, body, sizeof(body), &length) == NWK_CODEC_OK);
        CHECK(nwk_frame_decode(body, length, &decoded) == NWK_CODEC_OK);
        CHECK(filled(decoded.header.destination_ieee, 8, i ? 0xff : 0));
        CHECK(filled(decoded.header.source_ieee, 8, i ? 0xff : 0));
    }
    for (i = 0; i < 4; i++) {
        base_header();
        memcpy(body, golden[0], 11);
        if (i == 0) {
            header.type = 1;
            body[0] |= 1;
            expected = NWK_CODEC_UNSUPPORTED_TYPE;
        } else if (i == 1) {
            header.version = 3;
            body[0] = 0x0c;
            expected = NWK_CODEC_UNSUPPORTED_VERSION;
        } else {
            header.discover_route = i;
            body[0] |= (uint8_t)(i << 6);
            expected = NWK_CODEC_INVALID_HEADER;
        }
        CHECK(decode_failure(body, 11, expected) == 0);
        CHECK(encode_failure(&header, payload, 3, expected) == 0);
    }
    return 0;
}

static uint16_t self_test(void)
{
    uint16_t result = golden_cases();
    if (result == 0)
        result = shape_cases();
    if (result == 0)
        result = boundary_cases();
    if (result == 0)
        result = rejected_cases();
    return result;
}

#if defined(__SDCC)
volatile MCU_XDATA MCU_AT(0x1e00) uint8_t nwk_frame_test_result[8];

void main(void)
{
    uint16_t result = self_test();
    nwk_frame_test_result[0] = 'N';
    nwk_frame_test_result[1] = 'W';
    nwk_frame_test_result[2] = 'F';
    nwk_frame_test_result[3] = '1';
    nwk_frame_test_result[4] = 1;
    nwk_frame_test_result[5] = 8;
    nwk_frame_test_result[6] = (uint8_t)result;
    nwk_frame_test_result[7] = (uint8_t)(result >> 8);
    __asm
        .globl _nwk_frame_test_done
    _nwk_frame_test_done:
        nop
    __endasm;
    for (;;) {
    }
}
#else
#include <stdio.h>
#include <stdlib.h>

static uint16_t exhaustive_control(void)
{
    uint8_t input[24], output[24], written;
    uint32_t control, address, accepted;
    nwk_codec_result_t status;
    int valid;

    for (address = 0; address < 2; address++) {
        accepted = 0;
        for (control = 0; control <= 65535UL; control++) {
            memcpy(input, golden[1], sizeof(input));
            input[0] = (uint8_t)control;
            input[1] = (uint8_t)(control >> 8);
            if (address)
                input[2] = input[3] = 0xff;
            valid = control % 4 == 0 && control / 4 % 16 == 2 && control / 64 % 4 < 2
                    && (control & 0xc700UL) == 0;
            if (address && (control / 64 % 4 != 0 || (control & 0x0800UL)))
                valid = 0;
            memset(&decoded, 0xa5, sizeof(decoded));
            memcpy(&saved, &decoded, sizeof(saved));
            status = nwk_frame_decode(input, sizeof(input), &decoded);
            CHECK((status == NWK_CODEC_OK) == valid);
            base_header();
            header.type = (uint8_t)(control % 4);
            header.version = (uint8_t)(control / 4 % 16);
            header.discover_route = (uint8_t)(control / 64 % 4);
            header.flags = (uint16_t)(control & 0xff00UL);
            if (address)
                header.destination = 0xffff;
            if (!valid) {
                CHECK(memcmp(&decoded, &saved, sizeof(decoded)) == 0);
                CHECK(nwk_frame_encode(&header, NULL, 0, output, sizeof(output), &written) != NWK_CODEC_OK);
            } else {
                accepted++;
                CHECK(nwk_frame_encode(&decoded.header, input + decoded.payload_offset,
                                       decoded.payload_length, output, sizeof(output), &written) == NWK_CODEC_OK);
                CHECK(written == sizeof(input) && memcmp(input, output, sizeof(input)) == 0);
                CHECK(nwk_frame_encode(&header, NULL, 0, output, sizeof(output), &written) == NWK_CODEC_OK);
                CHECK(output[0] == input[0] && output[1] == input[1]);
            }
        }
        CHECK(accepted == (address ? 4u : 16u));
    }
    for (control = 0; control <= 65535UL; control++) {
        base_header();
        header.flags = (uint16_t)control;
        valid = (control & ~0x3800UL) == 0;
        memset(output, 0xc7, sizeof(output));
        written = 0xa5;
        status = nwk_frame_encode(&header, NULL, 0, output, sizeof(output), &written);
        CHECK((status == NWK_CODEC_OK) == valid);
        if (!valid)
            CHECK(written == 0xa5 && filled(output, sizeof(output), 0xc7));
    }
    return 0;
}

static uint16_t exhaustive_fields(void)
{
    uint32_t value;
    unsigned field;
    uint8_t input[11], opaque[108];
    int valid;
    nwk_codec_result_t status;

    for (field = 0; field < 2; field++) {
        for (value = 0; value <= 65535UL; value++) {
            base_header();
            memcpy(input, golden[0], sizeof(input));
            input[2u + 2u * field] = (uint8_t)value;
            input[3u + 2u * field] = (uint8_t)(value >> 8);
            if (field)
                header.source = (uint16_t)value;
            else
                header.destination = (uint16_t)value;
            valid = value < 0xfff8UL || (!field && (value == 0xfffbUL || value == 0xfffcUL
                                                   || value == 0xfffdUL || value == 0xffffUL));
            memset(&decoded, 0xa5, sizeof(decoded));
            memcpy(&saved, &decoded, sizeof(saved));
            status = nwk_frame_decode(input, sizeof(input), &decoded);
            CHECK((status == NWK_CODEC_OK) == valid);
            if (!valid) {
                CHECK(memcmp(&decoded, &saved, sizeof(decoded)) == 0);
                CHECK(encode_failure(&header, payload, 3, NWK_CODEC_INVALID_HEADER) == 0);
            } else {
                CHECK((field ? decoded.header.source : decoded.header.destination) == value);
                CHECK(nwk_frame_encode(&header, payload, 3, body, sizeof(body), &length) == NWK_CODEC_OK);
                CHECK(body[2u + 2u * field] == input[2u + 2u * field]);
                CHECK(body[3u + 2u * field] == input[3u + 2u * field]);
            }
        }
    }
    for (field = 0; field < 5; field++) {
        for (value = 0; value < 256; value++) {
            base_header();
            switch (field) {
            case 0: header.type = (uint8_t)value; break;
            case 1: header.version = (uint8_t)value; break;
            case 2: header.discover_route = (uint8_t)value; break;
            case 3: header.radius = (uint8_t)value; break;
            default: header.sequence = (uint8_t)value; break;
            }
            valid = field == 0 ? value == 0 : field == 1 ? value == 2 : field == 2 ? value < 2 : 1;
            memset(body, 0xc7, sizeof(body));
            length = 0xa5;
            status = nwk_frame_encode(&header, NULL, 0, body, sizeof(body), &length);
            CHECK((status == NWK_CODEC_OK) == valid);
            if (!valid) {
                CHECK(length == 0xa5 && filled(body, sizeof(body), 0xc7));
            } else {
                CHECK(nwk_frame_decode(body, length, &decoded) == NWK_CODEC_OK);
                CHECK(decoded.header.radius == header.radius && decoded.header.sequence == header.sequence);
            }
        }
    }
    for (field = 8; field < 27; field++) {
        for (value = 0; value < 256; value++) {
            memcpy(body, golden[1], 27);
            body[field] = (uint8_t)value;
            CHECK(nwk_frame_decode(body, 27, &decoded) == NWK_CODEC_OK);
            CHECK(memcmp(decoded.header.destination_ieee, body + 8, 8) == 0);
            CHECK(memcmp(decoded.header.source_ieee, body + 16, 8) == 0);
            CHECK(nwk_frame_encode(&decoded.header, body + 24, 3, body + 28, 27, &length) == NWK_CODEC_OK);
            CHECK(length == 27 && memcmp(body, body + 28, 27) == 0);
        }
    }
    base_header();
    for (field = 0; field < sizeof(opaque); field++) {
        for (value = 0; value < 256; value++) {
            memset(opaque, 0x69, sizeof(opaque));
            opaque[field] = (uint8_t)value;
            memset(body, 0xc7, sizeof(body));
            CHECK(nwk_frame_encode(&header, opaque, sizeof(opaque), body, sizeof(body), &length) == NWK_CODEC_OK);
            CHECK(length == 116 && body[116] == 0xc7 && body[117] == 0xc7);
            CHECK(nwk_frame_decode(body, length, &decoded) == NWK_CODEC_OK);
            CHECK(decoded.payload_offset == 8 && decoded.payload_length == sizeof(opaque));
            CHECK(memcmp(body + decoded.payload_offset, opaque, sizeof(opaque)) == 0);
        }
    }
    return 0;
}

static uint16_t exact_bounds(void)
{
    uint8_t *input, *output, *data, *large;
    nwk_frame_info_t *result;
    nwk_codec_result_t expected;
    uint32_t n;
    unsigned option, size, offset, data_size, capacity;
    uint8_t written;

    result = malloc(sizeof(*result));
    large = malloc(65535);
    CHECK(result != NULL && large != NULL);
    memset(large, 0x69, 65535);
    for (option = 0; option < 4; option++) {
        base_header();
        header.flags = (uint16_t)(option << 11);
        offset = 8u + (option & 1u ? 8u : 0u) + (option & 2u ? 8u : 0u);
        CHECK(nwk_frame_encode(&header, payload, (uint16_t)(116u - offset),
                               body, sizeof(body), &length) == NWK_CODEC_OK);
        for (size = 0; size <= 117; size++) {
            input = malloc(size ? size : 1u);
            CHECK(input != NULL);
            memcpy(input, body, size);
            memset(result, 0xa5, sizeof(*result));
            memcpy(&saved, result, sizeof(saved));
            expected = size > 116 ? NWK_CODEC_TOO_LONG : size < offset ? NWK_CODEC_TRUNCATED : NWK_CODEC_OK;
            CHECK(nwk_frame_decode(input, (uint16_t)size, result) == expected);
            if (expected != NWK_CODEC_OK)
                CHECK(memcmp(result, &saved, sizeof(saved)) == 0);
            else
                CHECK(result->payload_offset == offset && result->payload_length == size - offset);
            CHECK(memcmp(input, body, size) == 0);
            free(input);
        }
        for (data_size = 0; data_size <= 116u - offset; data_size++) {
            data = malloc(data_size ? data_size : 1u);
            CHECK(data != NULL);
            memcpy(data, payload, data_size);
            for (capacity = 0; capacity <= offset + data_size + 1u; capacity++) {
                output = malloc(capacity ? capacity : 1u);
                CHECK(output != NULL);
                memset(output, 0xc7, capacity);
                written = 0xa5;
                expected = capacity < offset + data_size ? NWK_CODEC_BUFFER_TOO_SMALL : NWK_CODEC_OK;
                CHECK(nwk_frame_encode(&header, data, (uint16_t)data_size, output,
                                       (uint16_t)capacity, &written) == expected);
                if (expected != NWK_CODEC_OK) {
                    CHECK(written == 0xa5 && filled(output, (uint16_t)capacity, 0xc7));
                } else {
                    CHECK(written == offset + data_size && memcmp(output, body, written) == 0);
                    CHECK(filled(output + written, (uint16_t)(capacity - written), 0xc7));
                }
                CHECK(memcmp(data, payload, data_size) == 0);
                free(output);
            }
            free(data);
        }
        for (n = 117; n <= 65535UL; n++) {
            memset(result, 0xa5, sizeof(*result));
            memcpy(&saved, result, sizeof(saved));
            CHECK(nwk_frame_decode(large, (uint16_t)n, result) == NWK_CODEC_TOO_LONG);
            CHECK(memcmp(result, &saved, sizeof(saved)) == 0);
        }
        for (n = 117u - offset; n <= 65535UL; n++)
            CHECK(encode_failure(&header, large, (uint16_t)n, NWK_CODEC_TOO_LONG) == 0);
        CHECK(nwk_frame_encode(&header, NULL, 0, large, 65535, &written) == NWK_CODEC_OK);
        CHECK(written == offset && filled(large + written, (uint16_t)(65535u - written), 0x69));
        memset(large, 0x69, 65535);
    }
    free(large);
    free(result);
    return 0;
}

int main(void)
{
    uint16_t result = self_test();
    if (result == 0)
        result = exhaustive_control();
    if (result == 0)
        result = exhaustive_fields();
    if (result == 0)
        result = exact_bounds();
    if (result != 0) {
        fprintf(stderr, "NWK frame test failed at line %u\n", (unsigned)result);
        return 1;
    }
    puts("host NWK frame: golden/layout/FCF/address/length matrices and exact buffers PASS");
    return 0;
}
#endif
