/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "mac_frame.h"
#include "cc2530_mmio.h"

#include <stddef.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) return (uint16_t)__LINE__; } while (0)

static const MCU_CODE uint8_t golden[] = {
    0x61, 0x98, 0x5a, 0x34, 0x12, 0x78, 0x56, 0xbc, 0x9a, 0xaa, 0x55, 0xcc
};

static void data_header(mac_header_t *header)
{
    memset(header, 0, sizeof(*header));
    header->type = MAC_FRAME_DATA;
    header->version = 1;
    header->flags = MAC_FLAG_ACK_REQUEST | MAC_FLAG_PAN_COMPRESSION;
    header->sequence = 0x5a;
    header->destination_mode = MAC_ADDRESS_SHORT;
    header->source_mode = MAC_ADDRESS_SHORT;
    header->destination_pan = 0x1234;
    header->source_pan = 0x1234;
    header->destination[0] = 0x78;
    header->destination[1] = 0x56;
    header->source[0] = 0xbc;
    header->source[1] = 0x9a;
}

static uint16_t golden_vectors(void)
{
    mac_header_t header;
    mac_frame_info_t decoded;
    uint8_t output[25], length, i;
    static const MCU_CODE uint8_t long_destination[] = {
        0x01, 0x8c, 0x07, 0x34, 0x12, 1, 2, 3, 4, 5, 6, 7, 8,
        0xcd, 0xab, 0x34, 0x12, 0xef
    };
    static const MCU_CODE uint8_t ack[] = {0x12, 0x00, 0xff};

    data_header(&header);
    CHECK(mac_frame_encode(&header, golden + 9, 3, output, sizeof(output), &length) == MAC_CODEC_OK);
    CHECK(length == sizeof(golden) && memcmp(output, golden, sizeof(golden)) == 0);
    CHECK(header.flags == (MAC_FLAG_ACK_REQUEST | MAC_FLAG_PAN_COMPRESSION));
    CHECK(mac_frame_decode(golden, sizeof(golden), &decoded) == MAC_CODEC_OK);
    CHECK(decoded.header.type == MAC_FRAME_DATA && decoded.header.version == 1);
    CHECK(decoded.header.sequence == 0x5a && decoded.header.flags == 0x60);
    CHECK(decoded.header.destination_pan == 0x1234 && decoded.header.source_pan == 0x1234);
    CHECK(decoded.payload_offset == 9 && decoded.payload_length == 3);
    CHECK(decoded.header.destination[0] == 0x78 && decoded.header.destination[1] == 0x56);
    CHECK(decoded.header.source[0] == 0xbc && decoded.header.source[1] == 0x9a);
    for (i = 2; i < 8; i++)
        CHECK(decoded.header.destination[i] == 0 && decoded.header.source[i] == 0);

    CHECK(mac_frame_decode(long_destination, sizeof(long_destination), &decoded) == MAC_CODEC_OK);
    CHECK(decoded.header.version == 0 && decoded.header.flags == 0);
    CHECK(decoded.header.destination_mode == MAC_ADDRESS_EXTENDED && decoded.header.source_mode == MAC_ADDRESS_SHORT);
    CHECK(decoded.header.destination_pan == 0x1234 && decoded.header.source_pan == 0xabcd);
    CHECK(decoded.payload_offset == 17 && decoded.payload_length == 1);
    for (i = 0; i < 8; i++)
        CHECK(decoded.header.destination[i] == i + 1u);
    CHECK(mac_frame_encode(&decoded.header, long_destination + 17, 1,
                           output, sizeof(output), &length) == MAC_CODEC_OK);
    CHECK(length == sizeof(long_destination) && memcmp(output, long_destination, length) == 0);

    CHECK(mac_frame_decode(ack, sizeof(ack), &decoded) == MAC_CODEC_OK);
    CHECK(decoded.header.type == MAC_FRAME_ACK && decoded.header.flags == MAC_FLAG_PENDING);
    CHECK(decoded.header.version == 0 && decoded.header.sequence == 255);
    CHECK(decoded.header.destination_mode == 0 && decoded.header.source_mode == 0);
    CHECK(decoded.header.destination_pan == 0 && decoded.header.source_pan == 0);
    CHECK(decoded.payload_offset == 3 && decoded.payload_length == 0);
    CHECK(mac_frame_encode(&decoded.header, NULL, 0, output, 3, &length) == MAC_CODEC_OK);
    CHECK(length == 3 && memcmp(output, ack, 3) == 0);
    return 0;
}

static uint16_t address_shapes(void)
{
    mac_header_t header;
    mac_frame_info_t decoded;
    uint8_t storage[127], payload[102], length, expected, i, version, destination, source, flags;

    memset(payload, 0x6d, sizeof(payload));
    for (version = 0; version < 2; version++) {
        for (destination = 2; destination <= 3; destination++) {
            for (source = 2; source <= 3; source++) {
                for (flags = 0; flags < 8; flags++) {
                    data_header(&header);
                    header.version = version;
                    header.destination_mode = destination;
                    header.source_mode = source;
                    header.flags = (uint8_t)(flags << 4);
                    if (!(header.flags & MAC_FLAG_PAN_COMPRESSION))
                        header.source_pan = 0xabcd;
                    for (i = 0; i < 8; i++) {
                        header.destination[i] = (uint8_t)(i + 1u);
                        header.source[i] = (uint8_t)(0x21u + i);
                    }
                    expected = (uint8_t)(5u + (destination == 2 ? 2u : 8u)
                                        + (source == 2 ? 2u : 8u) + ((flags & 4u) ? 0u : 2u));
                    memset(storage, 0xc7, sizeof(storage));
                    CHECK(mac_frame_encode(&header, payload, sizeof(payload), storage + 1,
                                           125, &length) == MAC_CODEC_OK);
                    CHECK(length == expected + sizeof(payload));
                    CHECK(storage[0] == 0xc7 && storage[length + 1u] == 0xc7);
                    CHECK(mac_frame_decode(storage + 1, length, &decoded) == MAC_CODEC_OK);
                    CHECK(decoded.payload_offset == expected && decoded.payload_length == sizeof(payload));
                    CHECK(decoded.header.version == version && decoded.header.flags == header.flags);
                    CHECK(decoded.header.source_pan == header.source_pan);
                    CHECK(memcmp(decoded.header.destination, header.destination, destination == 2 ? 2u : 8u) == 0);
                    CHECK(memcmp(decoded.header.source, header.source, source == 2 ? 2u : 8u) == 0);
                    CHECK(memcmp(storage + 1 + expected, payload, sizeof(payload)) == 0);
                }
            }
        }
    }
    return 0;
}

static uint16_t decode_rejections(void)
{
    mac_frame_info_t decoded, saved;
    uint8_t body[126], length, bit;

    memset(&saved, 0xa5, sizeof(saved));
    for (length = 0; length < 9; length++) {
        decoded = saved;
        CHECK(mac_frame_decode(golden, length, &decoded) == MAC_CODEC_TRUNCATED);
        CHECK(memcmp(&decoded, &saved, sizeof(saved)) == 0);
    }
    memcpy(body, golden, sizeof(golden));
    for (bit = 0; bit < 3; bit++) {
        body[0] = golden[0];
        body[1] = golden[1];
        if (bit == 0)
            body[0] |= 0x80;
        else
            body[1] |= (uint8_t)(1u << (bit - 1u));
        decoded = saved;
        CHECK(mac_frame_decode(body, sizeof(golden), &decoded) == MAC_CODEC_INVALID_HEADER);
        CHECK(memcmp(&decoded, &saved, sizeof(saved)) == 0);
    }
    memcpy(body, golden, sizeof(golden));
    body[0] |= MAC_FLAG_SECURITY;
    decoded = saved;
    CHECK(mac_frame_decode(body, sizeof(golden), &decoded) == MAC_CODEC_UNSUPPORTED_SECURITY);
    CHECK(memcmp(&decoded, &saved, sizeof(saved)) == 0);
    body[0] = golden[0];
    body[1] = 0xa8;
    CHECK(mac_frame_decode(body, sizeof(golden), &decoded) == MAC_CODEC_UNSUPPORTED_VERSION);
    body[1] = 0x94;
    CHECK(mac_frame_decode(body, sizeof(golden), &decoded) == MAC_CODEC_UNSUPPORTED_ADDRESSING);
    body[1] = 0x98;
    body[0] = 0x60;
    CHECK(mac_frame_decode(body, sizeof(golden), &decoded) == MAC_CODEC_UNSUPPORTED_TYPE);
    body[0] = 0x63;
    CHECK(mac_frame_decode(body, sizeof(golden), &decoded) == MAC_CODEC_UNSUPPORTED_TYPE);
    memcpy(body, golden, sizeof(golden));
    body[5] = body[6] = 0xff;
    CHECK(mac_frame_decode(body, sizeof(golden), &decoded) == MAC_CODEC_INVALID_HEADER);
    body[0] &= (uint8_t)~MAC_FLAG_ACK_REQUEST;
    CHECK(mac_frame_decode(body, sizeof(golden), &decoded) == MAC_CODEC_OK);
    body[7] = body[8] = 0xff;
    CHECK(mac_frame_decode(body, sizeof(golden), &decoded) == MAC_CODEC_INVALID_HEADER);
    decoded = saved;
    CHECK(mac_frame_decode(body, sizeof(body), &decoded) == MAC_CODEC_TOO_LONG);
    CHECK(mac_frame_decode(body, 65535u, &decoded) == MAC_CODEC_TOO_LONG);
    CHECK(mac_frame_decode(NULL, 3, &decoded) == MAC_CODEC_INVALID_ARGUMENT);
    CHECK(mac_frame_decode(body, 3, NULL) == MAC_CODEC_INVALID_ARGUMENT);
    CHECK(memcmp(&decoded, &saved, sizeof(saved)) == 0);
    body[0] = 2;
    body[1] = 0;
    CHECK(mac_frame_decode(body, 4, &decoded) == MAC_CODEC_INVALID_HEADER);
    body[0] |= MAC_FLAG_ACK_REQUEST;
    CHECK(mac_frame_decode(body, 3, &decoded) == MAC_CODEC_INVALID_HEADER);
    CHECK(memcmp(&decoded, &saved, sizeof(saved)) == 0);
    return 0;
}

static uint16_t encode_rejections(void)
{
    mac_header_t header;
    uint8_t storage[127], payload[117], length, i, test;
    mac_codec_result_t result;

    memset(payload, 0, sizeof(payload));
    for (test = 0; test < 8; test++) {
        data_header(&header);
        memset(storage, 0xc7, sizeof(storage));
        length = 0xa5;
        switch (test) {
        case 0:
            header.source_pan++;
            result = mac_frame_encode(&header, NULL, 0, storage, 125, &length);
            CHECK(result == MAC_CODEC_INVALID_HEADER);
            break;
        case 1:
            result = mac_frame_encode(&header, payload, sizeof(payload), storage, 125, &length);
            CHECK(result == MAC_CODEC_TOO_LONG);
            break;
        case 2:
            result = mac_frame_encode(&header, NULL, 0, storage, 8, &length);
            CHECK(result == MAC_CODEC_BUFFER_TOO_SMALL);
            break;
        case 3:
            result = mac_frame_encode(&header, NULL, 1, storage, 125, &length);
            CHECK(result == MAC_CODEC_INVALID_ARGUMENT);
            break;
        case 4:
            header.flags |= MAC_FLAG_SECURITY;
            result = mac_frame_encode(&header, NULL, 0, storage, 125, &length);
            CHECK(result == MAC_CODEC_UNSUPPORTED_SECURITY);
            break;
        case 5:
            header.destination_mode = 0;
            result = mac_frame_encode(&header, NULL, 0, storage, 125, &length);
            CHECK(result == MAC_CODEC_UNSUPPORTED_ADDRESSING);
            break;
        case 6:
            header.version = 2;
            result = mac_frame_encode(&header, NULL, 0, storage, 125, &length);
            CHECK(result == MAC_CODEC_UNSUPPORTED_VERSION);
            break;
        default:
            header.type = 3;
            result = mac_frame_encode(&header, NULL, 0, storage, 125, &length);
            CHECK(result == MAC_CODEC_UNSUPPORTED_TYPE);
            break;
        }
        CHECK(length == 0xa5);
        for (i = 0; i < sizeof(storage); i++)
            CHECK(storage[i] == 0xc7);
    }
    data_header(&header);
    CHECK(mac_frame_encode(NULL, NULL, 0, storage, 125, &length) == MAC_CODEC_INVALID_ARGUMENT);
    CHECK(mac_frame_encode(&header, NULL, 0, NULL, 125, &length) == MAC_CODEC_INVALID_ARGUMENT);
    CHECK(mac_frame_encode(&header, NULL, 0, storage, 125, NULL) == MAC_CODEC_INVALID_ARGUMENT);
    CHECK(mac_frame_encode(&header, payload, 65535u, storage, 125, &length) == MAC_CODEC_TOO_LONG);
    CHECK(length == 0xa5);
    return 0;
}

static uint16_t self_test(void)
{
    uint16_t result;
    result = golden_vectors();
    if (result != 0)
        return result;
    result = address_shapes();
    if (result != 0)
        return result;
    result = decode_rejections();
    if (result != 0)
        return result;
    return encode_rejections();
}

#if defined(__SDCC)
/* Simulator-only result ABI; this is not a board firmware build target. */
volatile MCU_XDATA MCU_AT(0x1e00) uint8_t mac_test_result[8];

void main(void)
{
    uint16_t result = self_test();
    mac_test_result[0] = 'M';
    mac_test_result[1] = 'A';
    mac_test_result[2] = 'C';
    mac_test_result[3] = '1';
    mac_test_result[4] = 1;
    mac_test_result[5] = 8;
    mac_test_result[6] = (uint8_t)result;
    mac_test_result[7] = (uint8_t)(result >> 8);
    __asm
        .globl _mac_codec_done
    _mac_codec_done:
        nop
    __endasm;
    for (;;) {
    }
}
#else
#include <stdio.h>
#include <stdlib.h>

static uint16_t exhaustive_control_fields(void)
{
    uint32_t control;
    unsigned accepted = 0;
    uint8_t body[23], output[23], length, input_length;
    mac_frame_info_t decoded, saved;

    memset(&saved, 0xa5, sizeof(saved));
    memset(body, 0x10, sizeof(body));
    for (control = 0; control < 65536UL; control++) {
        body[0] = (uint8_t)control;
        body[1] = (uint8_t)(control >> 8);
        input_length = (control & 7u) == MAC_FRAME_ACK ? 3u : 23u;
        decoded = saved;
        if (mac_frame_decode(body, input_length, &decoded) != MAC_CODEC_OK) {
            CHECK(memcmp(&decoded, &saved, sizeof(saved)) == 0);
            continue;
        }
        accepted++;
        CHECK(mac_frame_encode(&decoded.header, body + decoded.payload_offset,
                               decoded.payload_length, output, sizeof(output), &length) == MAC_CODEC_OK);
        CHECK(length == input_length && memcmp(output, body, length) == 0);
    }
    CHECK(accepted == 66u);
    return 0;
}

static uint16_t exact_buffer_boundaries(void)
{
    uint8_t complete[23], payload[116], *body, length;
    unsigned size, i;
    mac_header_t header;
    mac_frame_info_t decoded;
    mac_codec_result_t result;

    memset(complete, 0x10, sizeof(complete));
    complete[0] = 1;
    complete[1] = 0xdc;
    for (size = 0; size < sizeof(complete); size++) {
        body = malloc(size ? size : 1u);
        CHECK(body != NULL);
        memcpy(body, complete, size);
        result = mac_frame_decode(body, (uint16_t)size, &decoded);
        free(body);
        CHECK(result == MAC_CODEC_TRUNCATED);
    }
    data_header(&header);
    memset(payload, 0xa5, sizeof(payload));
    for (size = 0; size <= MAC_FRAME_MAX_BODY; size++) {
        body = malloc(size ? size : 1u);
        CHECK(body != NULL);
        memset(body, 0xc7, size);
        length = 0xcc;
        result = mac_frame_encode(&header, payload, sizeof(payload), body, (uint16_t)size, &length);
        if (size < MAC_FRAME_MAX_BODY) {
            CHECK(result == MAC_CODEC_BUFFER_TOO_SMALL && length == 0xcc);
            for (i = 0; i < size; i++)
                CHECK(body[i] == 0xc7);
        } else {
            CHECK(result == MAC_CODEC_OK && length == 125);
            CHECK(memcmp(body + 9, payload, sizeof(payload)) == 0);
        }
        free(body);
    }
    return 0;
}

static uint16_t invalid_header_fields(void)
{
    mac_header_t header;
    mac_codec_result_t result;
    uint8_t output[23], length;
    unsigned field, value, expected, i;

    for (field = 0; field < 5; field++) {
        for (value = 0; value < 256; value++) {
            data_header(&header);
            expected = 0;
            switch (field) {
            case 0:
                header.type = (uint8_t)value;
                expected = value == 1;
                break;
            case 1:
                header.version = (uint8_t)value;
                expected = value <= 1;
                break;
            case 2:
                header.flags = (uint8_t)value;
                expected = (value & ~0x70u) == 0;
                break;
            case 3:
                header.destination_mode = (uint8_t)value;
                expected = value == 2 || value == 3;
                break;
            default:
                header.source_mode = (uint8_t)value;
                expected = value == 2 || value == 3;
                break;
            }
            memset(output, 0xc7, sizeof(output));
            length = 0xcc;
            result = mac_frame_encode(&header, NULL, 0, output, sizeof(output), &length);
            CHECK((result == MAC_CODEC_OK) == expected);
            if (!expected) {
                CHECK(length == 0xcc);
                for (i = 0; i < sizeof(output); i++)
                    CHECK(output[i] == 0xc7);
            }
        }
    }
    memset(&header, 0, sizeof(header));
    header.type = MAC_FRAME_ACK;
    CHECK(mac_frame_encode(&header, golden + 9, 1, output, sizeof(output), &length) == MAC_CODEC_INVALID_HEADER);
    header.version = 1;
    CHECK(mac_frame_encode(&header, NULL, 0, output, sizeof(output), &length) == MAC_CODEC_UNSUPPORTED_VERSION);
    return 0;
}

int main(void)
{
    uint16_t result = self_test();
    if (result == 0)
        result = exhaustive_control_fields();
    if (result == 0)
        result = exact_buffer_boundaries();
    if (result == 0)
        result = invalid_header_fields();
    if (result != 0) {
        fprintf(stderr, "MAC codec test failed at line %u\n", (unsigned)result);
        return 1;
    }
    puts("host MAC codec: golden vectors, 64 DATA layouts, all 65536 FCF values and bounded failures PASS");
    return 0;
}
#endif
