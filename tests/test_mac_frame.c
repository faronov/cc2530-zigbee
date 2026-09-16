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

static const MCU_CODE uint8_t command_payloads[][4] = {
    {0x01, 0x88, 0, 0}, {0x02, 0xbc, 0x9a, 0}, {0x02, 0xfe, 0xff, 0},
    {0x02, 0xff, 0xff, 1}, {0x02, 0xff, 0xff, 2},
    {0x03, 1, 0, 0}, {0x03, 2, 0, 0}, {0x04, 0, 0, 0}, {0x07, 0, 0, 0}
};
static const MCU_CODE uint8_t command_payload_lengths[] = {2, 4, 4, 4, 4, 2, 2, 1, 1};
static const MCU_CODE uint8_t command_frames[][25] = {
    {0x23, 0xc8, 0x5a, 0x34, 0x12, 0x78, 0x56, 0xff, 0xff,
     1, 2, 3, 4, 5, 6, 7, 8, 0x01, 0x88},
    {0x63, 0xcc, 0x5a, 0x34, 0x12, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
     1, 2, 3, 4, 5, 6, 7, 8, 0x02, 0xbc, 0x9a, 0},
    {0x63, 0xc8, 0x5a, 0x34, 0x12, 0x78, 0x56, 1, 2, 3, 4, 5, 6, 7, 8, 0x03, 2},
    {0x63, 0x88, 0x5a, 0x34, 0x12, 0x78, 0x56, 0xbc, 0x9a, 0x04},
    {0x23, 0x80, 0x5a, 0x34, 0x12, 0xbc, 0x9a, 0x04},
    {0x03, 0x08, 0x5a, 0xff, 0xff, 0xff, 0xff, 0x07},
    {0x23, 0xcc, 0x5a, 0x34, 0x12, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
     0xff, 0xff, 1, 2, 3, 4, 5, 6, 7, 8, 0x01, 0x88},
    {0x63, 0xcc, 0x5a, 0x34, 0x12, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
     1, 2, 3, 4, 5, 6, 7, 8, 0x03, 2},
    {0x63, 0xc8, 0x5a, 0x34, 0x12, 0x78, 0x56, 1, 2, 3, 4, 5, 6, 7, 8, 0x04},
    {0x63, 0x8c, 0x5a, 0x34, 0x12, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0xbc, 0x9a, 0x04},
    {0x63, 0xcc, 0x5a, 0x34, 0x12, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
     1, 2, 3, 4, 5, 6, 7, 8, 0x04},
    {0x23, 0xc0, 0x5a, 0x34, 0x12, 1, 2, 3, 4, 5, 6, 7, 8, 0x04}
};
static const MCU_CODE uint8_t command_frame_lengths[] = {19, 25, 17, 10, 8, 8, 25, 23, 16, 16, 22, 14};
static const MCU_CODE uint8_t command_frame_offsets[] = {17, 21, 15, 9, 7, 7, 23, 21, 15, 15, 21, 13};
static const MCU_CODE uint8_t command_destination_modes[] = {2, 3, 2, 2, 0, 2, 3, 3, 2, 3, 3, 0};
static const MCU_CODE uint8_t command_source_modes[] = {3, 3, 3, 2, 2, 0, 3, 3, 3, 2, 3, 3};

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

static void command_header(mac_header_t *header, uint8_t vector)
{
    uint8_t i;
    data_header(header);
    header->type = MAC_FRAME_COMMAND;
    header->version = 0;
    header->destination_mode = command_destination_modes[vector];
    header->source_mode = command_source_modes[vector];
    if (header->source_mode == MAC_ADDRESS_EXTENDED) {
        for (i = 0; i < 8; i++)
            header->source[i] = (uint8_t)(i + 1u);
    }
    if (header->destination_mode == MAC_ADDRESS_EXTENDED) {
        for (i = 0; i < 8; i++)
            header->destination[i] = (uint8_t)(0x21u + i);
    }
    if (vector == 0u || vector == 6u) {
        header->flags = MAC_FLAG_ACK_REQUEST;
        header->source_pan = 0xffff;
    } else if (header->destination_mode == MAC_ADDRESS_NONE) {
        header->flags = MAC_FLAG_ACK_REQUEST;
    } else if (vector == 5u) {
        header->source_mode = MAC_ADDRESS_NONE;
        header->destination_pan = 0xffff;
        header->destination[0] = header->destination[1] = 0xff;
        header->flags = 0;
    }
}

static uint16_t command_vectors(void)
{
    mac_command_t decoded, saved;
    mac_codec_result_t status;
    uint8_t output[6], length, size, vector, boundary, i;

    memset(&saved, 0xa5, sizeof(saved));
    for (vector = 0; vector < sizeof(command_payload_lengths); vector++) {
        size = command_payload_lengths[vector];
        CHECK(mac_command_decode(command_payloads[vector], size, &decoded) == MAC_CODEC_OK);
        CHECK(decoded.identifier == command_payloads[vector][0]);
        CHECK(decoded.capability == (vector == 0u ? 0x88u : 0u));
        CHECK(decoded.reason == (vector == 5u ? 1u : (vector == 6u ? 2u : 0u)));
        CHECK(decoded.status == (vector == 3u ? 1u : (vector == 4u ? 2u : 0u)));
        CHECK(decoded.short_address == (vector == 1u ? 0x9abcu : (vector == 2u ? 0xfffeu
                                      : ((vector == 3u || vector == 4u) ? 0xffffu : 0u))));
        for (boundary = 0; boundary <= size + 1u; boundary++) {
            memset(output, 0xc7, sizeof(output));
            length = 0xa5;
            status = mac_command_encode(&decoded, output + 1, boundary, &length);
            if (boundary < size) {
                CHECK(status == MAC_CODEC_BUFFER_TOO_SMALL && length == 0xa5);
                for (i = 0; i < sizeof(output); i++)
                    CHECK(output[i] == 0xc7);
            } else {
                CHECK(status == MAC_CODEC_OK && length == size);
                CHECK(memcmp(output + 1, command_payloads[vector], size) == 0);
                CHECK(output[0] == 0xc7 && output[size + 1u] == 0xc7);
            }
        }
        for (boundary = 0; boundary < size; boundary++) {
            decoded = saved;
            CHECK(mac_command_decode(command_payloads[vector], boundary, &decoded) == MAC_CODEC_TRUNCATED);
            CHECK(memcmp(&decoded, &saved, sizeof(saved)) == 0);
        }
        memcpy(output, command_payloads[vector], size);
        output[size] = 0;
        CHECK(mac_command_decode(output, size + 1u, &decoded)
              == (size == 4u ? MAC_CODEC_TOO_LONG : MAC_CODEC_INVALID_COMMAND));
        CHECK(memcmp(&decoded, &saved, sizeof(saved)) == 0);
    }
    decoded = saved;
    decoded.identifier = MAC_COMMAND_DATA_REQUEST;
    CHECK(mac_command_encode(&decoded, output, sizeof(output), &length) == MAC_CODEC_OK);
    CHECK(length == 1 && output[0] == 4);
    CHECK(mac_command_decode(NULL, 0, &decoded) == MAC_CODEC_INVALID_ARGUMENT);
    CHECK(mac_command_decode(output, 1, NULL) == MAC_CODEC_INVALID_ARGUMENT);
    CHECK(mac_command_decode(output, 65535u, &decoded) == MAC_CODEC_TOO_LONG);
    CHECK(mac_command_encode(NULL, output, sizeof(output), &length) == MAC_CODEC_INVALID_ARGUMENT);
    CHECK(mac_command_encode(&decoded, NULL, 4, &length) == MAC_CODEC_INVALID_ARGUMENT);
    CHECK(mac_command_encode(&decoded, output, 4, NULL) == MAC_CODEC_INVALID_ARGUMENT);
    return 0;
}

static uint16_t command_frame_vectors(void)
{
    mac_header_t header;
    mac_frame_info_t decoded, saved;
    mac_codec_result_t status;
    uint8_t output[27], size, offset, vector, boundary, length, i;

    memset(&saved, 0xa5, sizeof(saved));
    for (vector = 0; vector < sizeof(command_frame_lengths); vector++) {
        size = command_frame_lengths[vector];
        offset = command_frame_offsets[vector];
        command_header(&header, vector);
        for (boundary = 0; boundary <= size; boundary++) {
            memset(output, 0xc7, sizeof(output));
            length = 0xa5;
            status = mac_frame_encode(&header, command_frames[vector] + offset, size - offset,
                                      output + 1, boundary, &length);
            if (boundary < size) {
                CHECK(status == MAC_CODEC_BUFFER_TOO_SMALL && length == 0xa5);
                for (i = 0; i < sizeof(output); i++)
                    CHECK(output[i] == 0xc7);
            } else {
                CHECK(status == MAC_CODEC_OK && length == size);
                CHECK(memcmp(output + 1, command_frames[vector], size) == 0);
                CHECK(output[0] == 0xc7 && output[size + 1u] == 0xc7);
            }
        }
        for (boundary = 0; boundary <= size; boundary++) {
            decoded = saved;
            status = mac_frame_decode(command_frames[vector], boundary, &decoded);
            if (boundary < size) {
                CHECK(status == MAC_CODEC_TRUNCATED);
                CHECK(memcmp(&decoded, &saved, sizeof(saved)) == 0);
            } else {
                CHECK(status == MAC_CODEC_OK && decoded.header.type == MAC_FRAME_COMMAND);
                CHECK(decoded.header.version == 0 && decoded.header.sequence == 0x5a);
                CHECK(decoded.payload_offset == offset && decoded.payload_length == size - offset);
                CHECK(decoded.header.destination_pan == (header.destination_mode == 0u ? 0u : header.destination_pan));
                CHECK(decoded.header.source_pan == (vector == 5u ? 0u : header.source_pan));
                if (header.destination_mode == 0u || header.source_mode == 0u)
                    for (i = 0; i < 8; i++)
                        CHECK((header.destination_mode == 0u ? decoded.header.destination[i] : decoded.header.source[i]) == 0);
            }
        }
        memcpy(output, command_frames[vector], size);
        output[0] |= MAC_FLAG_PENDING;
        CHECK(mac_frame_decode(output, size, &decoded) == MAC_CODEC_OK);
        CHECK(decoded.header.flags == (header.flags | MAC_FLAG_PENDING));
        memset(output, 0xc7, sizeof(output));
        length = 0xa5;
        CHECK(mac_frame_encode(&decoded.header, command_frames[vector] + offset, size - offset,
                               output, sizeof(output), &length) == MAC_CODEC_INVALID_HEADER);
        CHECK(length == 0xa5);
        for (i = 0; i < sizeof(output); i++)
            CHECK(output[i] == 0xc7);
    }
    return 0;
}

#if !defined(__SDCC)
static uint16_t command_address_shapes(void)
{
    mac_header_t header;
    mac_frame_info_t decoded;
    mac_codec_result_t status;
    uint8_t output[27], length, vector, destination, source, flags, expected, accepted = 0, i;

    for (vector = 0; vector < 6; vector++) {
        if (vector == 4u)
            continue;
        for (destination = 0; destination < 4; destination++) {
            for (source = 0; source < 4; source++) {
                for (flags = 0; flags < 8; flags++) {
                    command_header(&header, vector);
                    header.destination_mode = destination;
                    header.source_mode = source;
                    header.flags = (uint8_t)(flags << 4);
                    if (vector == 0u)
                        expected = destination >= 2u && source == 3u && flags == 2u;
                    else if (vector == 1u)
                        expected = destination == 3u && source == 3u && flags == 6u;
                    else if (vector == 2u)
                        expected = destination >= 2u && source == 3u && flags == 6u;
                    else if (vector == 3u)
                        expected = destination != 1u && source >= 2u && flags == (destination ? 6u : 2u);
                    else
                        expected = destination == 2u && source == 0u && flags == 0u;
                    memset(output, 0xc7, sizeof(output));
                    length = 0xa5;
                    status = mac_frame_encode(&header, command_frames[vector] + command_frame_offsets[vector],
                                              command_frame_lengths[vector] - command_frame_offsets[vector],
                                              output + 1, 25, &length);
                    CHECK((status == MAC_CODEC_OK) == expected);
                    if (expected) {
                        accepted++;
                        CHECK(mac_frame_decode(output + 1, length, &decoded) == MAC_CODEC_OK);
                        CHECK(decoded.header.destination_mode == destination && decoded.header.source_mode == source);
                        CHECK(decoded.header.flags == header.flags);
                        CHECK(decoded.payload_length == command_frame_lengths[vector] - command_frame_offsets[vector]);
                        CHECK(output[0] == 0xc7 && output[length + 1u] == 0xc7);
                    } else {
                        CHECK(length == 0xa5);
                        for (i = 0; i < sizeof(output); i++)
                            CHECK(output[i] == 0xc7);
                    }
                }
            }
        }
    }
    CHECK(accepted == 12u);
    return 0;
}
#endif

static uint16_t command_payload_rejections(void)
{
    static const MCU_CODE uint8_t invalid[][4] = {
        {1, 0x10, 0, 0}, {1, 0x20, 0, 0}, {2, 0xff, 0xff, 0},
        {2, 0xfe, 0xff, 1}, {2, 0, 0, 2}, {2, 0xff, 0xff, 3},
        {3, 0, 0, 0}, {3, 3, 0, 0}, {5, 0, 0, 0}, {6, 0, 0, 0},
        {8, 0, 0, 0}, {9, 0, 0, 0}
    };
    mac_command_t command, saved;
    uint8_t test, size;
    mac_codec_result_t expected;

    memset(&saved, 0xa5, sizeof(saved));
    for (test = 0; test < sizeof(invalid) / sizeof(invalid[0]); test++) {
        size = invalid[test][0] == 2u ? 4u : 2u;
        command = saved;
        expected = test >= 8u ? MAC_CODEC_UNSUPPORTED_COMMAND : MAC_CODEC_INVALID_COMMAND;
        CHECK(mac_command_decode(invalid[test], size, &command) == expected);
        CHECK(memcmp(&command, &saved, sizeof(saved)) == 0);
    }
    return 0;
}

#if !defined(__SDCC)
static uint16_t command_frame_rejections(void)
{
    mac_frame_info_t decoded, frame_saved;
    mac_header_t header;
    uint8_t body[27], output[27], length, test, vector, size, i;
    mac_codec_result_t expected;

    memset(&frame_saved, 0xa5, sizeof(frame_saved));
    for (vector = 0; vector < 6; vector++) {
        size = command_frame_lengths[vector];
        for (test = 0; test < 8; test++) {
            command_header(&header, vector);
            memcpy(body, command_frames[vector], size);
            switch (test) {
            case 0:
                body[command_frame_offsets[vector]] = 0xff;
                expected = MAC_CODEC_UNSUPPORTED_COMMAND;
                break;
            case 1:
                body[size] = 0;
                expected = size - command_frame_offsets[vector] == 4u
                           ? MAC_CODEC_TOO_LONG : MAC_CODEC_INVALID_COMMAND;
                break;
            case 2:
                body[1] |= 0x10;
                header.version = 1;
                expected = MAC_CODEC_UNSUPPORTED_VERSION;
                break;
            case 3:
                body[0] ^= MAC_FLAG_ACK_REQUEST;
                header.flags ^= MAC_FLAG_ACK_REQUEST;
                expected = MAC_CODEC_INVALID_HEADER;
                break;
            case 4:
                if (vector == 0u) {
                    body[7] = 0xfe;
                    header.source_pan = 0xfffe;
                } else if (vector == 4u) {
                    body[5] = 0xfe;
                    body[6] = 0xff;
                    header.source[0] = 0xfe;
                    header.source[1] = 0xff;
                } else if (vector == 5u) {
                    body[3] = 0xfe;
                    header.destination_pan = 0xfffe;
                } else
                    continue;
                expected = MAC_CODEC_INVALID_HEADER;
                break;
            case 5:
                body[0] |= MAC_FLAG_SECURITY;
                header.flags |= MAC_FLAG_SECURITY;
                expected = MAC_CODEC_UNSUPPORTED_SECURITY;
                break;
            case 6:
                if (vector == 1u || vector == 4u)
                    continue;
                body[5] = vector == 5u ? 0xfdu : 0xfeu;
                body[6] = 0xff;
                header.destination[0] = body[5];
                header.destination[1] = body[6];
                expected = MAC_CODEC_INVALID_HEADER;
                break;
            default:
                if (vector == 0u)
                    body[command_frame_offsets[vector] + 1u] |= 0x10;
                else if (vector == 1u)
                    body[command_frame_offsets[vector] + 1u] = body[command_frame_offsets[vector] + 2u] = 0xff;
                else if (vector == 2u)
                    body[command_frame_offsets[vector] + 1u] = 0;
                else
                    continue;
                expected = MAC_CODEC_INVALID_COMMAND;
                break;
            }
            decoded = frame_saved;
            CHECK(mac_frame_decode(body, size + (test == 1u), &decoded) == expected);
            CHECK(memcmp(&decoded, &frame_saved, sizeof(frame_saved)) == 0);
            memset(output, 0xc7, sizeof(output));
            length = 0xa5;
            CHECK(mac_frame_encode(&header, body + command_frame_offsets[vector],
                                   size - command_frame_offsets[vector] + (test == 1u),
                                   output, sizeof(output), &length) == expected);
            CHECK(length == 0xa5);
            for (i = 0; i < sizeof(output); i++)
                CHECK(output[i] == 0xc7);
        }
        command_header(&header, vector);
        if (header.flags & MAC_FLAG_PAN_COMPRESSION) {
            header.source_pan++;
            memset(output, 0xc7, sizeof(output));
            length = 0xa5;
            CHECK(mac_frame_encode(&header, command_frames[vector] + command_frame_offsets[vector],
                                   size - command_frame_offsets[vector], output, sizeof(output), &length)
                  == MAC_CODEC_INVALID_HEADER);
            CHECK(length == 0xa5);
            for (i = 0; i < sizeof(output); i++)
                CHECK(output[i] == 0xc7);
        }
    }
    command_header(&header, 3);
    CHECK(mac_frame_encode(&header, NULL, 0, output, sizeof(output), &length) == MAC_CODEC_TRUNCATED);
    return 0;
}
#endif

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
    CHECK(mac_frame_decode(body, sizeof(golden), &decoded) == MAC_CODEC_UNSUPPORTED_VERSION);
    body[1] = 0x88;
    CHECK(mac_frame_decode(body, sizeof(golden), &decoded) == MAC_CODEC_UNSUPPORTED_COMMAND);
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
            header.type = MAC_FRAME_COMMAND;
            header.version = 0;
            result = mac_frame_encode(&header, NULL, 0, storage, 125, &length);
            CHECK(result == MAC_CODEC_TRUNCATED);
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
    result = encode_rejections();
    if (result != 0)
        return result;
    result = command_vectors();
    if (result != 0)
        return result;
    result = command_frame_vectors();
    if (result != 0)
        return result;
    return command_payload_rejections();
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

static uint16_t command_field_case(const mac_command_t *command, const uint8_t *payload,
                                   uint8_t size, uint8_t valid)
{
    mac_command_t decoded, saved;
    mac_codec_result_t status, expected;
    uint8_t output[4], length = 0xa5, i;
    uint8_t id = command->identifier;

    expected = valid ? MAC_CODEC_OK : ((id >= 1u && id <= 4u) || id == 7u
                                      ? MAC_CODEC_INVALID_COMMAND : MAC_CODEC_UNSUPPORTED_COMMAND);
    memset(output, 0xc7, sizeof(output));
    status = mac_command_encode(command, output, sizeof(output), &length);
    CHECK(status == expected);
    if (valid) {
        CHECK(length == size && memcmp(output, payload, size) == 0);
        for (i = size; i < sizeof(output); i++)
            CHECK(output[i] == 0xc7);
    } else {
        CHECK(length == 0xa5);
        for (i = 0; i < sizeof(output); i++)
            CHECK(output[i] == 0xc7);
    }
    memset(&saved, 0xa5, sizeof(saved));
    decoded = saved;
    CHECK(mac_command_decode(payload, size, &decoded) == expected);
    if (valid) {
        CHECK(decoded.identifier == id);
        CHECK(decoded.capability == (id == 1u ? command->capability : 0u));
        CHECK(decoded.short_address == (id == 2u ? command->short_address : 0u));
        CHECK(decoded.status == (id == 2u ? command->status : 0u));
        CHECK(decoded.reason == (id == 3u ? command->reason : 0u));
    } else
        CHECK(memcmp(&decoded, &saved, sizeof(saved)) == 0);
    return 0;
}

static uint16_t exhaustive_command_fields(void)
{
    mac_command_t command;
    uint8_t payload[4], size;
    uint16_t result;
    uint32_t address;
    unsigned value, status;

    memset(&command, 0xa5, sizeof(command));
    command.capability = 0;
    command.short_address = 0xffff;
    command.status = 1;
    command.reason = 1;
    for (value = 0; value < 256; value++) {
        command.identifier = (uint8_t)value;
        payload[0] = (uint8_t)value;
        payload[1] = value == 1u ? 0u : (value == 3u ? 1u : 0xffu);
        payload[2] = 0xff;
        payload[3] = 1;
        size = value == 2u ? 4u : ((value == 1u || value == 3u) ? 2u : 1u);
        result = command_field_case(&command, payload, size, (value >= 1u && value <= 4u) || value == 7u);
        if (result != 0)
            return result;
    }
    for (value = 0; value < 256; value++) {
        command.identifier = payload[0] = 1;
        command.capability = payload[1] = (uint8_t)value;
        result = command_field_case(&command, payload, 2, (value & 0x30u) == 0u);
        if (result != 0)
            return result;
        command.identifier = payload[0] = 3;
        command.reason = payload[1] = (uint8_t)value;
        result = command_field_case(&command, payload, 2, value == 1u || value == 2u);
        if (result != 0)
            return result;
        command.identifier = payload[0] = 2;
        command.status = payload[3] = (uint8_t)value;
        payload[2] = 0xff;
        for (address = 0xfffeu; address <= 0xffffu; address++) {
            command.short_address = (uint16_t)address;
            payload[1] = (uint8_t)address;
            result = command_field_case(&command, payload, 4,
                                         address == 0xfffeu ? value == 0u : (value == 1u || value == 2u));
            if (result != 0)
                return result;
        }
    }
    for (address = 0; address < 65536UL; address++) {
        command.short_address = (uint16_t)address;
        payload[1] = (uint8_t)address;
        payload[2] = (uint8_t)(address >> 8);
        for (status = 0; status < 3; status++) {
            command.status = payload[3] = (uint8_t)status;
            result = command_field_case(&command, payload, 4, status == 0u ? address != 0xffffu : address == 0xffffu);
            if (result != 0)
                return result;
        }
    }
    return 0;
}

static uint16_t exhaustive_command_control_fields(void)
{
    uint32_t control, canonical, alternate;
    unsigned vector, i;
    uint8_t body[25], output[25], length, size, valid;
    mac_frame_info_t decoded, saved;
    mac_codec_result_t status;

    memset(&saved, 0xa5, sizeof(saved));
    for (vector = 0; vector < sizeof(command_frame_lengths); vector++) {
        size = command_frame_lengths[vector];
        memcpy(body, command_frames[vector], size);
        canonical = (uint32_t)body[0] | ((uint32_t)body[1] << 8);
        /* Mixed-address polls have equal MHR lengths in either address order. */
        alternate = vector == 8u || vector == 9u ? canonical ^ 0x4400u : canonical;
        for (control = 3; control < 65536UL; control += 8) {
            body[0] = (uint8_t)control;
            body[1] = (uint8_t)(control >> 8);
            decoded = saved;
            valid = (control & ~MAC_FLAG_PENDING) == canonical || (control & ~MAC_FLAG_PENDING) == alternate;
            CHECK((mac_frame_decode(body, size, &decoded) == MAC_CODEC_OK) == valid);
            if (!valid) {
                CHECK(memcmp(&decoded, &saved, sizeof(saved)) == 0);
                continue;
            }
            memset(output, 0xc7, sizeof(output));
            length = 0xa5;
            status = mac_frame_encode(&decoded.header, body + decoded.payload_offset,
                                      decoded.payload_length, output, sizeof(output), &length);
            if (control & MAC_FLAG_PENDING) {
                CHECK(status == MAC_CODEC_INVALID_HEADER && length == 0xa5);
                for (i = 0; i < sizeof(output); i++)
                    CHECK(output[i] == 0xc7);
            } else
                CHECK(status == MAC_CODEC_OK && length == size && memcmp(output, body, size) == 0);
        }
    }
    return 0;
}

static uint16_t exact_command_boundaries(void)
{
    mac_command_t command, decoded_command, saved_command;
    mac_frame_info_t decoded_frame, saved_frame;
    mac_header_t header;
    mac_codec_result_t status, expected;
    unsigned kind, vector, count, size, complete, payload_size, offset, i;
    uint8_t *input, *output, *payload, length;
    uint8_t storage[25];
    const uint8_t *golden_body;

    memset(&saved_command, 0xa5, sizeof(saved_command));
    memset(&saved_frame, 0xa5, sizeof(saved_frame));
    for (kind = 0; kind < 2; kind++) {
        count = kind == 0u ? sizeof(command_payload_lengths) : sizeof(command_frame_lengths);
        for (vector = 0; vector < count; vector++) {
            complete = kind == 0u ? command_payload_lengths[vector] : command_frame_lengths[vector];
            offset = kind == 0u ? 0u : command_frame_offsets[vector];
            payload_size = complete - offset;
            golden_body = kind == 0u ? command_payloads[vector] : command_frames[vector];
            payload = malloc(payload_size);
            CHECK(payload != NULL);
            memcpy(payload, golden_body + offset, payload_size);
            CHECK(mac_command_decode(payload, (uint16_t)payload_size, &command) == MAC_CODEC_OK);
            if (kind != 0u)
                command_header(&header, (uint8_t)vector);
            for (size = 0; size <= complete + 1u; size++) {
                input = malloc(size ? size : 1u);
                output = malloc(size ? size : 1u);
                CHECK(input != NULL && output != NULL);
                memset(input, 0, size ? size : 1u);
                memcpy(input, golden_body, size <= complete ? size : complete);
                memset(output, 0xc7, size ? size : 1u);
                expected = size < complete ? MAC_CODEC_TRUNCATED : (size == complete ? MAC_CODEC_OK
                           : (payload_size == 4u ? MAC_CODEC_TOO_LONG : MAC_CODEC_INVALID_COMMAND));
                decoded_command = saved_command;
                decoded_frame = saved_frame;
                status = kind == 0u ? mac_command_decode(input, (uint16_t)size, &decoded_command)
                         : mac_frame_decode(input, (uint16_t)size, &decoded_frame);
                CHECK(status == expected);
                if (status != MAC_CODEC_OK) {
                    CHECK(memcmp(&decoded_command, &saved_command, sizeof(saved_command)) == 0);
                    CHECK(memcmp(&decoded_frame, &saved_frame, sizeof(saved_frame)) == 0);
                }
                length = 0xa5;
                status = kind == 0u ? mac_command_encode(&command, output, (uint16_t)size, &length)
                         : mac_frame_encode(&header, payload, (uint16_t)payload_size, output, (uint16_t)size, &length);
                if (size < complete) {
                    CHECK(status == MAC_CODEC_BUFFER_TOO_SMALL && length == 0xa5);
                    for (i = 0; i < (size ? size : 1u); i++)
                        CHECK(output[i] == 0xc7);
                } else {
                    CHECK(status == MAC_CODEC_OK && length == complete);
                    CHECK(memcmp(output, golden_body, complete) == 0);
                    if (size > complete)
                        CHECK(output[complete] == 0xc7);
                }
                free(input);
                free(output);
            }
            if (kind != 0u) {
                for (size = 0; size < payload_size; size++) {
                    input = malloc(size ? size : 1u);
                    CHECK(input != NULL);
                    memcpy(input, payload, size);
                    memset(storage, 0xc7, sizeof(storage));
                    length = 0xa5;
                    CHECK(mac_frame_encode(&header, input, (uint16_t)size, storage, sizeof(storage), &length)
                          == MAC_CODEC_TRUNCATED);
                    CHECK(length == 0xa5);
                    for (i = 0; i < sizeof(storage); i++)
                        CHECK(storage[i] == 0xc7);
                    free(input);
                }
            }
            free(payload);
        }
    }
    return 0;
}

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
    if (result == 0)
        result = command_address_shapes();
    if (result == 0)
        result = command_frame_rejections();
    if (result == 0)
        result = exhaustive_command_fields();
    if (result == 0)
        result = exhaustive_command_control_fields();
    if (result == 0)
        result = exact_command_boundaries();
    if (result != 0) {
        fprintf(stderr, "MAC codec test failed at line %u\n", (unsigned)result);
        return 1;
    }
    puts("host MAC codec: DATA/ACK, five commands, 12 command layouts, exhaustive fields and exact bounds PASS");
    return 0;
}
#endif
