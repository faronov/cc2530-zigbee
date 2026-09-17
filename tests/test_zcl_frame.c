/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "zcl_wire.h"
#include "aps_frame.h"
#include "cc2530_mmio.h"

#include <stddef.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) return (uint16_t)__LINE__; } while (0)

static const MCU_CODE uint8_t golden[][8] = {
    {0x18, 0x5a, 0x0a, 0x34, 0x12, 0x29, 0x85, 0xff},
    {0x1d, 0x78, 0x56, 0xa5, 0xe7, 0xa9, 0x55, 0x69}
};
static const MCU_CODE zcl_header_t code_header = {0, 0x18, 0xabcd, 0x5a, 0x0a};
static const MCU_CODE uint8_t payload[98] = {0xa9, 0x55, 0x69};
static zcl_header_t header;
static zcl_frame_info_t decoded, saved;
static uint8_t body[102], length;
static aps_header_t aps;
static aps_frame_info_t transport;
static uint8_t apdu[110], aps_length;

static uint8_t filled(const uint8_t *bytes, uint16_t size, uint8_t value)
{
    uint16_t i;
    for (i = 0; i < size; i++)
        if (bytes[i] != value)
            return 0;
    return 1;
}

static void base_header(uint8_t manufacturer)
{
    memset(&header, 0, sizeof(header));
    header.type = manufacturer;
    header.flags = manufacturer ? 0x1c : 0x18;
    header.manufacturer_code = 0x5678;
    header.sequence = manufacturer ? 0xa5 : 0x5a;
    header.command_id = manufacturer ? 0xe7 : 0x0a;
}

static uint16_t decode_failure(const uint8_t *input, uint16_t size, zcl_codec_result_t expected)
{
    memset(&decoded, 0xa5, sizeof(decoded));
    memcpy(&saved, &decoded, sizeof(saved));
    CHECK(zcl_frame_decode(input, size, &decoded) == expected);
    CHECK(memcmp(&decoded, &saved, sizeof(decoded)) == 0);
    return 0;
}

static uint16_t encode_failure(const zcl_header_t *input, const uint8_t *data,
                               uint16_t size, zcl_codec_result_t expected)
{
    memset(body, 0xc7, sizeof(body));
    length = 0xa5;
    CHECK(zcl_frame_encode(input, data, size, body + 1, 100, &length) == expected);
    CHECK(length == 0xa5 && filled(body, sizeof(body), 0xc7));
    return 0;
}

static uint16_t golden_cases(void)
{
    volatile uint8_t vector, n, offset;
    uint16_t failure;

    CHECK(zcl_frame_encode(&code_header, golden[0] + 3, 5, body, sizeof(body), &length) == ZCL_CODEC_OK);
    CHECK(length == 8 && memcmp(body, golden[0], 8) == 0);
    for (vector = 0; vector < 2; vector++) {
        offset = vector ? 5 : 3;
        CHECK(zcl_frame_decode(golden[vector], 8, &decoded) == ZCL_CODEC_OK);
        CHECK(decoded.header.type == vector && decoded.header.flags == (vector ? 0x1cu : 0x18u));
        CHECK(decoded.header.manufacturer_code == (vector ? 0x5678u : 0u));
        CHECK(decoded.header.sequence == (vector ? 0xa5u : 0x5au));
        CHECK(decoded.header.command_id == (vector ? 0xe7u : 0x0au));
        CHECK(decoded.ignored_control_bits == 0 && decoded.payload_offset == offset);
        CHECK(decoded.payload_length == 8u - offset);
        header = decoded.header;
        memcpy(&saved.header, &header, sizeof(header));
        for (n = 0; n <= 9; n++) {
            memset(body, 0xc7, sizeof(body));
            length = 0xa5;
            if (n < 8) {
                CHECK(zcl_frame_encode(&header, golden[vector] + offset, (uint16_t)(8u - offset),
                                       body + 1, n, &length) == ZCL_CODEC_BUFFER_TOO_SMALL);
                CHECK(length == 0xa5 && filled(body, sizeof(body), 0xc7));
            } else {
                CHECK(zcl_frame_encode(&header, golden[vector] + offset, (uint16_t)(8u - offset),
                                       body + 1, n, &length) == ZCL_CODEC_OK);
                CHECK(length == 8 && body[0] == 0xc7 && memcmp(body + 1, golden[vector], 8) == 0);
                CHECK(filled(body + 9, sizeof(body) - 9u, 0xc7));
            }
            CHECK(memcmp(&header, &saved.header, sizeof(header)) == 0);
        }
        for (n = 0; n <= 8; n++) {
            if (n < offset) {
                failure = decode_failure(golden[vector], n, ZCL_CODEC_TRUNCATED);
                if (failure != 0)
                    return failure;
            } else {
                CHECK(zcl_frame_decode(golden[vector], n, &decoded) == ZCL_CODEC_OK);
                CHECK(decoded.payload_offset == offset && decoded.payload_length == n - offset);
            }
        }
    }
    return 0;
}

static uint16_t control_cases(void)
{
    volatile uint16_t control;
    uint8_t accepted = 0, offset;

    for (control = 0; control < 256; control++) {
        memset(body, 0x69, sizeof(body));
        body[0] = (uint8_t)control;
        if (control % 4u > 1u) {
            CHECK(decode_failure(body, 1, ZCL_CODEC_UNSUPPORTED_FRAME_TYPE) == 0);
            CHECK(decode_failure(body, 8, ZCL_CODEC_UNSUPPORTED_FRAME_TYPE) == 0);
        } else if (control / 4u % 2u && control >= 32u) {
            CHECK(decode_failure(body, 8, ZCL_CODEC_UNSUPPORTED_LAYOUT) == 0);
        } else {
            accepted++;
            offset = control / 4u % 2u ? 5u : 3u;
            CHECK(zcl_frame_decode(body, 8, &decoded) == ZCL_CODEC_OK);
            CHECK(decoded.header.type == control % 4u);
            CHECK(decoded.header.flags == (control & 0x1cu));
            CHECK(decoded.ignored_control_bits == (control & 0xe0u));
            CHECK(decoded.payload_offset == offset && decoded.payload_length == 8u - offset);
            CHECK(zcl_frame_encode(&decoded.header, body + offset, (uint16_t)(8u - offset),
                                   body + 9, 8, &length) == ZCL_CODEC_OK);
            CHECK(length == 8 && body[9] == (control & 0x1fu));
            CHECK(memcmp(body + 1, body + 10, 7) == 0);
        }
    }
    CHECK(accepted == 72);
    return 0;
}

static uint16_t boundary_cases(void)
{
    volatile uint8_t manufacturer, n, size;

    for (manufacturer = 0; manufacturer < 2; manufacturer++) {
        base_header(manufacturer);
        size = manufacturer ? 5 : 3;
        for (n = 0; n <= 100u - size; n++) {
            memset(body, 0xc7, sizeof(body));
            CHECK(zcl_frame_encode(&header, payload, n, body + 1, 100, &length) == ZCL_CODEC_OK);
            CHECK(length == size + n && body[0] == 0xc7);
            CHECK(filled(body + length + 1u, sizeof(body) - length - 1u, 0xc7));
            CHECK(memcmp(body + size + 1u, payload, n) == 0);
            CHECK(zcl_frame_decode(body + 1, length, &decoded) == ZCL_CODEC_OK);
            CHECK(decoded.payload_offset == size && decoded.payload_length == n);
        }
        for (n = 0; n < 100; n++) {
            memset(body, 0xc7, sizeof(body));
            length = 0xa5;
            CHECK(zcl_frame_encode(&header, payload, (uint16_t)(100u - size),
                                   body + 1, n, &length) == ZCL_CODEC_BUFFER_TOO_SMALL);
            CHECK(length == 0xa5 && filled(body, sizeof(body), 0xc7));
        }
        CHECK(encode_failure(&header, payload, (uint16_t)(101u - size), ZCL_CODEC_TOO_LONG) == 0);
        CHECK(zcl_frame_encode(&header, NULL, 0, body, sizeof(body), &length) == ZCL_CODEC_OK);
        CHECK(length == size);
    }
    CHECK(decode_failure(body, 101, ZCL_CODEC_TOO_LONG) == 0);
    CHECK(decode_failure(NULL, 0, ZCL_CODEC_INVALID_ARGUMENT) == 0);
    CHECK(zcl_frame_decode(body, 8, NULL) == ZCL_CODEC_INVALID_ARGUMENT);
    CHECK(encode_failure(NULL, payload, 3, ZCL_CODEC_INVALID_ARGUMENT) == 0);
    CHECK(encode_failure(&header, NULL, 1, ZCL_CODEC_INVALID_ARGUMENT) == 0);
    CHECK(zcl_frame_encode(&header, payload, 3, NULL, 0, &length) == ZCL_CODEC_INVALID_ARGUMENT);
    CHECK(length == 0xa5);
    CHECK(zcl_frame_encode(&header, payload, 3, body, sizeof(body), NULL) == ZCL_CODEC_INVALID_ARGUMENT);
    CHECK(filled(body, sizeof(body), 0xc7));
    header.type = 2;
    CHECK(encode_failure(&header, payload, 3, ZCL_CODEC_UNSUPPORTED_FRAME_TYPE) == 0);
    header.type = 0;
    header.flags = 0x80;
    CHECK(encode_failure(&header, payload, 3, ZCL_CODEC_INVALID_HEADER) == 0);
    header.flags = 1;
    CHECK(encode_failure(&header, payload, 3, ZCL_CODEC_INVALID_HEADER) == 0);
    return 0;
}

static uint16_t aps_integration(void)
{
    volatile uint8_t control, size, n;

    memset(&aps, 0, sizeof(aps));
    aps.flags = APS_FLAG_ACK_REQUEST;
    aps.destination_endpoint = 0x21;
    aps.source_endpoint = 0x31;
    aps.cluster_id = 0x5678;
    aps.profile_id = 0x1234;
    aps.counter = 0xe7;
    for (control = 0; control < 32; control++) {
        if (control % 4u > 1u)
            continue;
        base_header(control / 4u % 2u);
        header.type = control & 3u;
        header.flags = control & 0x1cu;
        size = control & 4u ? 5 : 3;
        CHECK(zcl_frame_encode(&header, payload, (uint16_t)(100u - size), body, 100, &length) == ZCL_CODEC_OK);
        memset(apdu, 0xc7, sizeof(apdu));
        CHECK(aps_frame_encode(&aps, body, length, apdu + 1, 108, &aps_length) == APS_CODEC_OK);
        CHECK(aps_length == 108 && apdu[0] == 0xc7 && apdu[109] == 0xc7);
        CHECK(aps_frame_decode(apdu + 1, aps_length, &transport) == APS_CODEC_OK);
        CHECK(transport.payload_offset == 8 && transport.payload_length == 100);
        CHECK(zcl_frame_decode(apdu + 1u + transport.payload_offset, transport.payload_length, &decoded) == ZCL_CODEC_OK);
        CHECK(decoded.header.type == header.type && decoded.header.flags == header.flags);
        CHECK(decoded.header.sequence == header.sequence && decoded.header.command_id == header.command_id);
        CHECK(memcmp(apdu + 9u + decoded.payload_offset, payload, decoded.payload_length) == 0);
        apdu[9] |= 0xe0;
        if (control & 4u) {
            CHECK(decode_failure(apdu + 9, 100, ZCL_CODEC_UNSUPPORTED_LAYOUT) == 0);
        } else {
            CHECK(zcl_frame_decode(apdu + 9, 100, &decoded) == ZCL_CODEC_OK);
            CHECK(decoded.ignored_control_bits == 0xe0);
        }
        apdu[9] = 2;
        CHECK(aps_frame_decode(apdu + 1, aps_length, &transport) == APS_CODEC_OK);
        CHECK(decode_failure(apdu + 9, 100, ZCL_CODEC_UNSUPPORTED_FRAME_TYPE) == 0);
        for (n = 0; n < size; n++) {
            CHECK(aps_frame_encode(&aps, body, n, apdu, sizeof(apdu), &aps_length) == APS_CODEC_OK);
            CHECK(aps_frame_decode(apdu, aps_length, &transport) == APS_CODEC_OK);
            CHECK(decode_failure(apdu + transport.payload_offset, transport.payload_length, ZCL_CODEC_TRUNCATED) == 0);
        }
    }
    return 0;
}

static uint16_t self_test(void)
{
    uint16_t result = golden_cases();
    if (result == 0)
        result = control_cases();
    if (result == 0)
        result = boundary_cases();
    if (result == 0)
        result = aps_integration();
    return result;
}

#if defined(__SDCC)
volatile MCU_XDATA MCU_AT(0x1e00) uint8_t zcl_frame_test_result[8];

void main(void)
{
    uint16_t result = self_test();
    zcl_frame_test_result[0] = 'Z';
    zcl_frame_test_result[1] = 'C';
    zcl_frame_test_result[2] = 'F';
    zcl_frame_test_result[3] = '1';
    zcl_frame_test_result[4] = 1;
    zcl_frame_test_result[5] = 8;
    zcl_frame_test_result[6] = (uint8_t)result;
    zcl_frame_test_result[7] = (uint8_t)(result >> 8);
    __asm
        .globl _zcl_frame_test_done
    _zcl_frame_test_done:
        nop
    __endasm;
    for (;;) {
    }
}
#else
#include <stdio.h>
#include <stdlib.h>

static uint16_t exhaustive_fields(void)
{
    uint32_t value;
    unsigned manufacturer, field;
    uint8_t input[100], opaque[97];
    int valid;

    for (manufacturer = 0; manufacturer < 2; manufacturer++) {
        for (field = 0; field < 4; field++) {
            for (value = 0; value < 256; value++) {
                base_header((uint8_t)manufacturer);
                switch (field) {
                case 0: header.type = (uint8_t)value; break;
                case 1: header.flags = (uint8_t)value; break;
                case 2: header.sequence = (uint8_t)value; break;
                default: header.command_id = (uint8_t)value; break;
                }
                valid = field == 0 ? value < 2 : field == 1 ? (value & ~0x1cUL) == 0 : 1;
                memset(body, 0xc7, sizeof(body));
                length = 0xa5;
                CHECK((zcl_frame_encode(&header, NULL, 0, body, sizeof(body), &length) == ZCL_CODEC_OK) == valid);
                if (!valid) {
                    CHECK(length == 0xa5 && filled(body, sizeof(body), 0xc7));
                } else {
                    CHECK(zcl_frame_decode(body, length, &decoded) == ZCL_CODEC_OK);
                    CHECK(decoded.header.type == header.type && decoded.header.flags == header.flags);
                    CHECK(decoded.header.sequence == header.sequence && decoded.header.command_id == header.command_id);
                    CHECK(decoded.header.manufacturer_code == (header.flags & 4u ? header.manufacturer_code : 0));
                }
            }
        }
        for (value = 0; value <= 65535UL; value++) {
            base_header((uint8_t)manufacturer);
            header.manufacturer_code = (uint16_t)value;
            CHECK(zcl_frame_encode(&header, NULL, 0, body, sizeof(body), &length) == ZCL_CODEC_OK);
            CHECK(zcl_frame_decode(body, length, &decoded) == ZCL_CODEC_OK);
            CHECK(decoded.header.manufacturer_code == (manufacturer ? value : 0));
            if (manufacturer)
                CHECK(body[1] == (uint8_t)value && body[2] == (uint8_t)(value >> 8));
        }
    }
    base_header(0);
    for (field = 0; field < sizeof(opaque); field++) {
        for (value = 0; value < 256; value++) {
            memset(opaque, 0x69, sizeof(opaque));
            opaque[field] = (uint8_t)value;
            memcpy(input, golden[0], 3);
            memcpy(input + 3, opaque, sizeof(opaque));
            CHECK(zcl_frame_encode(&header, opaque, sizeof(opaque), body + 1, 100, &length) == ZCL_CODEC_OK);
            CHECK(length == 100 && memcmp(body + 1, input, 100) == 0);
            CHECK(zcl_frame_decode(body + 1, length, &decoded) == ZCL_CODEC_OK);
            CHECK(decoded.payload_offset == 3 && decoded.payload_length == 97);
        }
    }
    return 0;
}

static uint16_t exact_bounds(void)
{
    uint8_t *input, *output, *data, *large, written;
    zcl_header_t *exact_header;
    zcl_frame_info_t *result;
    zcl_codec_result_t expected;
    uint32_t n;
    unsigned manufacturer, size, offset, data_size, capacity;

    result = malloc(sizeof(*result));
    exact_header = malloc(sizeof(*exact_header));
    large = malloc(65535);
    CHECK(result != NULL && exact_header != NULL && large != NULL);
    memset(large, 0x69, 65535);
    for (manufacturer = 0; manufacturer < 2; manufacturer++) {
        base_header((uint8_t)manufacturer);
        memcpy(exact_header, &header, sizeof(header));
        offset = manufacturer ? 5u : 3u;
        CHECK(zcl_frame_encode(exact_header, payload, (uint16_t)(100u - offset), body, sizeof(body), &length) == ZCL_CODEC_OK);
        for (size = 0; size <= 101; size++) {
            input = malloc(size ? size : 1u);
            CHECK(input != NULL);
            memcpy(input, body, size);
            memset(result, 0xa5, sizeof(*result));
            memcpy(&saved, result, sizeof(saved));
            expected = size > 100 ? ZCL_CODEC_TOO_LONG : size < offset ? ZCL_CODEC_TRUNCATED : ZCL_CODEC_OK;
            CHECK(zcl_frame_decode(input, (uint16_t)size, result) == expected);
            if (expected != ZCL_CODEC_OK)
                CHECK(memcmp(result, &saved, sizeof(saved)) == 0);
            else
                CHECK(result->payload_offset == offset && result->payload_length == size - offset);
            CHECK(memcmp(input, body, size) == 0);
            free(input);
        }
        for (data_size = 0; data_size <= 100u - offset; data_size++) {
            data = malloc(data_size ? data_size : 1u);
            CHECK(data != NULL);
            memcpy(data, payload, data_size);
            for (capacity = 0; capacity <= offset + data_size + 1u; capacity++) {
                output = malloc(capacity ? capacity : 1u);
                CHECK(output != NULL);
                memset(output, 0xc7, capacity ? capacity : 1u);
                written = 0xa5;
                expected = capacity < offset + data_size ? ZCL_CODEC_BUFFER_TOO_SMALL : ZCL_CODEC_OK;
                CHECK(zcl_frame_encode(exact_header, data, (uint16_t)data_size, output,
                                       (uint16_t)capacity, &written) == expected);
                if (expected != ZCL_CODEC_OK) {
                    CHECK(written == 0xa5 && filled(output, (uint16_t)(capacity ? capacity : 1u), 0xc7));
                } else {
                    CHECK(written == offset + data_size && memcmp(output, body, written) == 0);
                    CHECK(filled(output + written, (uint16_t)(capacity - written), 0xc7));
                }
                CHECK(memcmp(data, payload, data_size) == 0);
                CHECK(memcmp(exact_header, &header, sizeof(header)) == 0);
                free(output);
            }
            free(data);
        }
        for (n = 101; n <= 65535UL; n++)
            CHECK(decode_failure(large, (uint16_t)n, ZCL_CODEC_TOO_LONG) == 0);
        for (n = 101u - offset; n <= 65535UL; n++)
            CHECK(encode_failure(exact_header, large, (uint16_t)n, ZCL_CODEC_TOO_LONG) == 0);
        for (n = 100; n <= 65535UL; n++) {
            memset(large, 0xc7, 101);
            CHECK(zcl_frame_encode(exact_header, payload, (uint16_t)(100u - offset),
                                   large, (uint16_t)n, &written) == ZCL_CODEC_OK);
            CHECK(written == 100 && large[100] == 0xc7);
        }
        memset(large, 0x69, 65535);
    }
    free(large);
    free(exact_header);
    free(result);
    return 0;
}

int main(void)
{
    uint16_t result = self_test();
    if (result == 0)
        result = exhaustive_fields();
    if (result == 0)
        result = exact_bounds();
    if (result != 0) {
        fprintf(stderr, "ZCL frame test failed at line %u\n", (unsigned)result);
        return 1;
    }
    puts("host ZCL frame: golden/control/field/length matrices, exact buffers and APS integration PASS");
    return 0;
}
#endif
