/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "aps_frame.h"
#include "cc2530_mmio.h"

#include <stddef.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) return (uint16_t)__LINE__; } while (0)

static const MCU_CODE uint8_t golden[][11] = {
    {0x40, 0x21, 0x78, 0x56, 0x34, 0x12, 0x31, 0xa5, 0xa9, 0x55, 0x69},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xa9, 0x55, 0x69},
    {0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0x00, 0xa9, 0x55, 0x69}
};
static const MCU_CODE uint8_t payload[101] = {0xa9, 0x55, 0x69};
static const MCU_CODE aps_header_t code_header = {
    0, 0, 0x40, 0x21, 0x5678, 0x1234, 0x31, 0xa5
};
static aps_header_t header;
static aps_frame_info_t decoded, saved;
static uint8_t body[APS_FRAME_MAX_BODY + 2u], length;

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
    memset(&header, 0, sizeof(header));
    header.flags = APS_FLAG_ACK_REQUEST;
    header.destination_endpoint = 0x21;
    header.cluster_id = 0x5678;
    header.profile_id = 0x1234;
    header.source_endpoint = 0x31;
    header.counter = 0xa5;
}

static uint16_t decode_failure(const uint8_t *input, uint16_t size, aps_codec_result_t expected)
{
    memset(&decoded, 0xa5, sizeof(decoded));
    memcpy(&saved, &decoded, sizeof(saved));
    CHECK(aps_frame_decode(input, size, &decoded) == expected);
    CHECK(memcmp(&decoded, &saved, sizeof(decoded)) == 0);
    return 0;
}

static uint16_t encode_failure(const aps_header_t *input, const uint8_t *data,
                               uint16_t size, aps_codec_result_t expected)
{
    memset(body, 0xc7, sizeof(body));
    length = 0xa5;
    CHECK(aps_frame_encode(input, data, size, body + 1, 108, &length) == expected);
    CHECK(length == 0xa5 && filled(body, sizeof(body), 0xc7));
    return 0;
}

static uint16_t golden_cases(void)
{
    volatile uint8_t vector, n;
    uint16_t failure;

    CHECK(aps_frame_encode(&code_header, payload, 3, body, sizeof(body), &length) == APS_CODEC_OK);
    CHECK(length == 11 && memcmp(body, golden[0], 11) == 0);
    for (vector = 0; vector < 3; vector++) {
        CHECK(aps_frame_decode(golden[vector], 11, &decoded) == APS_CODEC_OK);
        CHECK(decoded.header.type == 0 && decoded.header.delivery_mode == 0);
        CHECK(decoded.header.flags == (vector == 0 ? 0x40u : 0u));
        CHECK(decoded.header.destination_endpoint == golden[vector][1]);
        CHECK(decoded.header.cluster_id == (vector == 0 ? 0x5678u : vector == 1 ? 0u : 0xffffu));
        CHECK(decoded.header.profile_id == (vector == 0 ? 0x1234u : vector == 1 ? 0u : 0xffffu));
        CHECK(decoded.header.source_endpoint == golden[vector][6]);
        CHECK(decoded.header.counter == golden[vector][7]);
        CHECK(decoded.payload_offset == 8 && decoded.payload_length == 3);
        header = decoded.header;
        memcpy(&saved.header, &header, sizeof(header));
        for (n = 0; n <= 12; n++) {
            memset(body, 0xc7, sizeof(body));
            length = 0xa5;
            if (n < 11) {
                CHECK(aps_frame_encode(&header, payload, 3, body + 1, n, &length) == APS_CODEC_BUFFER_TOO_SMALL);
                CHECK(length == 0xa5 && filled(body, sizeof(body), 0xc7));
            } else {
                CHECK(aps_frame_encode(&header, payload, 3, body + 1, n, &length) == APS_CODEC_OK);
                CHECK(length == 11 && body[0] == 0xc7);
                CHECK(memcmp(body + 1, golden[vector], 11) == 0);
                CHECK(filled(body + 12, sizeof(body) - 12u, 0xc7));
            }
            CHECK(memcmp(&header, &saved.header, sizeof(header)) == 0);
        }
        for (n = 0; n <= 11; n++) {
            if (n < 8) {
                failure = decode_failure(golden[vector], n, APS_CODEC_TRUNCATED);
                if (failure != 0)
                    return failure;
            } else {
                CHECK(aps_frame_decode(golden[vector], n, &decoded) == APS_CODEC_OK);
                CHECK(decoded.payload_offset == 8 && decoded.payload_length == n - 8u);
            }
        }
    }
    return 0;
}

static uint16_t boundary_cases(void)
{
    volatile uint8_t n, ack;

    for (ack = 0; ack < 2; ack++) {
        base_header();
        header.flags = ack ? APS_FLAG_ACK_REQUEST : 0;
        for (n = 0; n <= 100; n++) {
            memset(body, 0xc7, sizeof(body));
            CHECK(aps_frame_encode(&header, payload, n, body + 1, 108, &length) == APS_CODEC_OK);
            CHECK(length == 8u + n && body[0] == 0xc7);
            CHECK(filled(body + length + 1u, sizeof(body) - length - 1u, 0xc7));
            CHECK(memcmp(body + 9, payload, n) == 0);
            CHECK(aps_frame_decode(body + 1, length, &decoded) == APS_CODEC_OK);
            CHECK(decoded.payload_offset == 8 && decoded.payload_length == n);
        }
        for (n = 0; n < 108; n++) {
            memset(body, 0xc7, sizeof(body));
            length = 0xa5;
            CHECK(aps_frame_encode(&header, payload, 100, body + 1, n, &length) == APS_CODEC_BUFFER_TOO_SMALL);
            CHECK(length == 0xa5 && filled(body, sizeof(body), 0xc7));
        }
        CHECK(encode_failure(&header, payload, 101, APS_CODEC_TOO_LONG) == 0);
    }
    CHECK(decode_failure(body, 109, APS_CODEC_TOO_LONG) == 0);
    CHECK(aps_frame_encode(&header, NULL, 0, body, sizeof(body), &length) == APS_CODEC_OK);
    CHECK(length == 8);
    CHECK(encode_failure(NULL, payload, 3, APS_CODEC_INVALID_ARGUMENT) == 0);
    CHECK(encode_failure(&header, NULL, 1, APS_CODEC_INVALID_ARGUMENT) == 0);
    CHECK(aps_frame_encode(&header, payload, 3, NULL, 0, &length) == APS_CODEC_INVALID_ARGUMENT);
    CHECK(length == 0xa5);
    CHECK(aps_frame_encode(&header, payload, 3, body, sizeof(body), NULL) == APS_CODEC_INVALID_ARGUMENT);
    CHECK(filled(body, sizeof(body), 0xc7));
    CHECK(decode_failure(NULL, 0, APS_CODEC_INVALID_ARGUMENT) == 0);
    CHECK(decode_failure(NULL, 8, APS_CODEC_INVALID_ARGUMENT) == 0);
    CHECK(aps_frame_decode(body, 8, NULL) == APS_CODEC_INVALID_ARGUMENT);
    return 0;
}

static uint16_t control_cases(void)
{
    volatile uint16_t control;
    aps_codec_result_t expected;
    uint8_t accepted = 0;
    uint16_t failure;

    for (control = 0; control < 256; control++) {
        /* Independent oracle for every complete on-wire control byte. */
        if (control % 4u != 0)
            expected = APS_CODEC_UNSUPPORTED_TYPE;
        else if (control & 0x20u)
            expected = APS_CODEC_UNSUPPORTED_SECURITY;
        else if (control & 0x90u)
            expected = APS_CODEC_UNSUPPORTED_LAYOUT;
        else if (control / 4u % 4u == 1u)
            expected = APS_CODEC_INVALID_HEADER;
        else if (control / 4u % 4u != 0)
            expected = APS_CODEC_UNSUPPORTED_DELIVERY;
        else
            expected = APS_CODEC_OK;
        memcpy(body, golden[0], 11);
        body[0] = (uint8_t)control;
        base_header();
        header.type = (uint8_t)(control % 4u);
        header.delivery_mode = (uint8_t)(control / 4u % 4u);
        header.flags = (uint8_t)(control & 0xf0u);
        if (expected == APS_CODEC_OK) {
            accepted++;
            CHECK(aps_frame_decode(body, 11, &decoded) == APS_CODEC_OK);
            CHECK(decoded.header.flags == control);
            CHECK(aps_frame_encode(&header, payload, 3, body + 12, 11, &length) == APS_CODEC_OK);
            CHECK(length == 11 && memcmp(body, body + 12, 11) == 0);
        } else {
            failure = decode_failure(body, 11, expected);
            if (failure != 0)
                return failure;
            if (control % 4u != 0) {
                CHECK(decode_failure(body, 1, APS_CODEC_UNSUPPORTED_TYPE) == 0);
                CHECK(decode_failure(body, 2, APS_CODEC_UNSUPPORTED_TYPE) == 0);
            }
            failure = encode_failure(&header, payload, 3, expected);
            if (failure != 0)
                return failure;
        }
    }
    CHECK(accepted == 2);
    base_header();
    header.source_endpoint = 0xff;
    CHECK(encode_failure(&header, payload, 3, APS_CODEC_INVALID_HEADER) == 0);
    memcpy(body, golden[0], 11);
    body[6] = 0xff;
    CHECK(decode_failure(body, 11, APS_CODEC_INVALID_HEADER) == 0);
    header.source_endpoint = 0x31;
    header.flags = 4;
    CHECK(encode_failure(&header, payload, 3, APS_CODEC_INVALID_HEADER) == 0);
    header.flags = 0;
    header.delivery_mode = 0xff;
    CHECK(encode_failure(&header, payload, 3, APS_CODEC_INVALID_HEADER) == 0);
    return 0;
}

static uint16_t self_test(void)
{
    uint16_t result = golden_cases();
    if (result == 0)
        result = boundary_cases();
    if (result == 0)
        result = control_cases();
    return result;
}

#if defined(__SDCC)
volatile MCU_XDATA MCU_AT(0x1e00) uint8_t aps_frame_test_result[8];

void main(void)
{
    uint16_t result = self_test();
    aps_frame_test_result[0] = 'A';
    aps_frame_test_result[1] = 'P';
    aps_frame_test_result[2] = 'F';
    aps_frame_test_result[3] = '1';
    aps_frame_test_result[4] = 1;
    aps_frame_test_result[5] = 8;
    aps_frame_test_result[6] = (uint8_t)result;
    aps_frame_test_result[7] = (uint8_t)(result >> 8);
    __asm
        .globl _aps_frame_test_done
    _aps_frame_test_done:
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
    unsigned field;
    uint8_t input[108], opaque[100];
    aps_codec_result_t status;
    int valid;

    for (field = 0; field < 6; field++) {
        for (value = 0; value < 256; value++) {
            base_header();
            switch (field) {
            case 0: header.type = (uint8_t)value; break;
            case 1: header.delivery_mode = (uint8_t)value; break;
            case 2: header.flags = (uint8_t)value; break;
            case 3: header.destination_endpoint = (uint8_t)value; break;
            case 4: header.source_endpoint = (uint8_t)value; break;
            default: header.counter = (uint8_t)value; break;
            }
            valid = field < 2 ? value == 0 : field == 2 ? (value == 0 || value == 0x40)
                    : field == 4 ? value < 255 : 1;
            memset(body, 0xc7, sizeof(body));
            length = 0xa5;
            status = aps_frame_encode(&header, NULL, 0, body, sizeof(body), &length);
            CHECK((status == APS_CODEC_OK) == valid);
            if (!valid) {
                CHECK(length == 0xa5 && filled(body, sizeof(body), 0xc7));
            } else {
                CHECK(aps_frame_decode(body, length, &decoded) == APS_CODEC_OK);
                CHECK(memcmp(&header, &decoded.header, sizeof(header)) == 0);
            }
        }
    }
    for (field = 0; field < 2; field++) {
        for (value = 0; value <= 65535UL; value++) {
            base_header();
            memcpy(input, golden[0], 11);
            input[2u + 2u * field] = (uint8_t)value;
            input[3u + 2u * field] = (uint8_t)(value >> 8);
            if (field)
                header.profile_id = (uint16_t)value;
            else
                header.cluster_id = (uint16_t)value;
            CHECK(aps_frame_decode(input, 11, &decoded) == APS_CODEC_OK);
            CHECK((field ? decoded.header.profile_id : decoded.header.cluster_id) == value);
            CHECK(aps_frame_encode(&header, payload, 3, body, sizeof(body), &length) == APS_CODEC_OK);
            CHECK(length == 11 && memcmp(body, input, 11) == 0);
        }
    }
    for (field = 1; field < 11; field++) {
        for (value = 0; value < 256; value++) {
            memcpy(input, golden[0], 11);
            input[field] = (uint8_t)value;
            if (field == 6 && value == 255) {
                CHECK(decode_failure(input, 11, APS_CODEC_INVALID_HEADER) == 0);
            } else {
                CHECK(aps_frame_decode(input, 11, &decoded) == APS_CODEC_OK);
                CHECK(aps_frame_encode(&decoded.header, input + 8, 3, body, sizeof(body), &length) == APS_CODEC_OK);
                CHECK(length == 11 && memcmp(body, input, 11) == 0);
            }
        }
    }
    base_header();
    for (field = 0; field < sizeof(opaque); field++) {
        for (value = 0; value < 256; value++) {
            memset(opaque, 0x69, sizeof(opaque));
            opaque[field] = (uint8_t)value;
            memcpy(input, golden[0], 8);
            memcpy(input + 8, opaque, sizeof(opaque));
            memset(body, 0xc7, sizeof(body));
            CHECK(aps_frame_encode(&header, opaque, sizeof(opaque), body + 1, 108, &length) == APS_CODEC_OK);
            CHECK(length == 108 && body[0] == 0xc7 && body[109] == 0xc7);
            CHECK(memcmp(body + 1, input, sizeof(input)) == 0);
            CHECK(aps_frame_decode(body + 1, length, &decoded) == APS_CODEC_OK);
            CHECK(decoded.payload_offset == 8 && decoded.payload_length == sizeof(opaque));
        }
    }
    return 0;
}

static uint16_t exact_bounds(void)
{
    uint8_t *input, *output, *data, *large, written;
    aps_header_t *exact_header;
    aps_frame_info_t *result;
    aps_codec_result_t expected;
    uint32_t n;
    unsigned ack, size, data_size, capacity;

    result = malloc(sizeof(*result));
    exact_header = malloc(sizeof(*exact_header));
    large = malloc(65535);
    CHECK(result != NULL && exact_header != NULL && large != NULL);
    memset(large, 0x69, 65535);
    for (ack = 0; ack < 2; ack++) {
        base_header();
        header.flags = ack ? APS_FLAG_ACK_REQUEST : 0;
        memcpy(exact_header, &header, sizeof(header));
        CHECK(aps_frame_encode(exact_header, payload, 100, body, sizeof(body), &length) == APS_CODEC_OK);
        for (size = 0; size <= 109; size++) {
            input = malloc(size ? size : 1u);
            CHECK(input != NULL);
            memcpy(input, body, size);
            memset(result, 0xa5, sizeof(*result));
            memcpy(&saved, result, sizeof(saved));
            expected = size > 108 ? APS_CODEC_TOO_LONG : size < 8 ? APS_CODEC_TRUNCATED : APS_CODEC_OK;
            CHECK(aps_frame_decode(input, (uint16_t)size, result) == expected);
            if (expected != APS_CODEC_OK)
                CHECK(memcmp(result, &saved, sizeof(saved)) == 0);
            else
                CHECK(result->payload_offset == 8 && result->payload_length == size - 8u);
            CHECK(memcmp(input, body, size) == 0);
            free(input);
        }
        for (size = 1; size <= 3; size++) {
            input = malloc(size);
            CHECK(input != NULL);
            memset(input, 0, size);
            for (capacity = 1; capacity <= 3; capacity++) {
                input[0] = (uint8_t)capacity;
                CHECK(decode_failure(input, (uint16_t)size, APS_CODEC_UNSUPPORTED_TYPE) == 0);
            }
            free(input);
        }
        for (data_size = 0; data_size <= 100; data_size++) {
            data = malloc(data_size ? data_size : 1u);
            CHECK(data != NULL);
            memcpy(data, payload, data_size);
            for (capacity = 0; capacity <= 9u + data_size; capacity++) {
                output = malloc(capacity ? capacity : 1u);
                CHECK(output != NULL);
                memset(output, 0xc7, capacity ? capacity : 1u);
                written = 0xa5;
                expected = capacity < 8u + data_size ? APS_CODEC_BUFFER_TOO_SMALL : APS_CODEC_OK;
                CHECK(aps_frame_encode(exact_header, data, (uint16_t)data_size, output,
                                       (uint16_t)capacity, &written) == expected);
                if (expected != APS_CODEC_OK) {
                    CHECK(written == 0xa5 && filled(output, (uint16_t)(capacity ? capacity : 1u), 0xc7));
                } else {
                    CHECK(written == 8u + data_size && memcmp(output, body, written) == 0);
                    CHECK(filled(output + written, (uint16_t)(capacity - written), 0xc7));
                }
                CHECK(memcmp(data, payload, data_size) == 0);
                CHECK(memcmp(exact_header, &header, sizeof(header)) == 0);
                free(output);
            }
            free(data);
        }
        for (n = 109; n <= 65535UL; n++) {
            memset(result, 0xa5, sizeof(*result));
            memcpy(&saved, result, sizeof(saved));
            CHECK(aps_frame_decode(large, (uint16_t)n, result) == APS_CODEC_TOO_LONG);
            CHECK(memcmp(result, &saved, sizeof(saved)) == 0);
        }
        for (n = 101; n <= 65535UL; n++)
            CHECK(encode_failure(exact_header, large, (uint16_t)n, APS_CODEC_TOO_LONG) == 0);
        for (n = 108; n <= 65535UL; n++) {
            memset(large, 0xc7, 109);
            CHECK(aps_frame_encode(exact_header, payload, 100, large, (uint16_t)n, &written) == APS_CODEC_OK);
            CHECK(written == 108 && large[108] == 0xc7);
            CHECK(memcmp(large + 8, payload, 100) == 0);
        }
        memset(large, 0x69, 65535);
        CHECK(aps_frame_encode(exact_header, NULL, 0, large, 65535, &written) == APS_CODEC_OK);
        CHECK(written == 8 && filled(large + written, (uint16_t)(65535u - written), 0x69));
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
        fprintf(stderr, "APS frame test failed at line %u\n", (unsigned)result);
        return 1;
    }
    puts("host APS frame: golden/FCF/endpoint/identifier/length matrices and exact buffers PASS");
    return 0;
}
#endif
