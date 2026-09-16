/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "mac_frame.h"

#include <stddef.h>
#include <string.h>

static uint16_t read_le16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

static void write_le16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static mac_codec_result_t header_shape(const mac_header_t *header,
                                       uint8_t *destination_size, uint8_t *source_size,
                                       uint8_t *header_size)
{
    if (header->version > 1u || (header->type == MAC_FRAME_ACK && header->version != 0u))
        return MAC_CODEC_UNSUPPORTED_VERSION;
    if (header->flags & MAC_FLAG_SECURITY)
        return MAC_CODEC_UNSUPPORTED_SECURITY;
    if (header->flags & (uint8_t)~(MAC_FLAG_PENDING | MAC_FLAG_ACK_REQUEST | MAC_FLAG_PAN_COMPRESSION))
        return MAC_CODEC_INVALID_HEADER;
    if (header->type == MAC_FRAME_ACK) {
        if (header->destination_mode != MAC_ADDRESS_NONE || header->source_mode != MAC_ADDRESS_NONE
                || (header->flags & (MAC_FLAG_ACK_REQUEST | MAC_FLAG_PAN_COMPRESSION)))
            return MAC_CODEC_INVALID_HEADER;
        *destination_size = 0;
        *source_size = 0;
        *header_size = 3;
        return MAC_CODEC_OK;
    }
    if (header->type != MAC_FRAME_DATA)
        return MAC_CODEC_UNSUPPORTED_TYPE;
    if ((header->destination_mode != MAC_ADDRESS_SHORT && header->destination_mode != MAC_ADDRESS_EXTENDED)
            || (header->source_mode != MAC_ADDRESS_SHORT && header->source_mode != MAC_ADDRESS_EXTENDED))
        return MAC_CODEC_UNSUPPORTED_ADDRESSING;
    *destination_size = header->destination_mode == MAC_ADDRESS_SHORT ? 2u : 8u;
    *source_size = header->source_mode == MAC_ADDRESS_SHORT ? 2u : 8u;
    *header_size = (uint8_t)(5u + *destination_size + *source_size
                            + ((header->flags & MAC_FLAG_PAN_COMPRESSION) ? 0u : 2u));
    return MAC_CODEC_OK;
}

static mac_codec_result_t address_policy(const mac_header_t *header)
{
    if (header->type != MAC_FRAME_DATA)
        return MAC_CODEC_OK;
    if ((header->flags & MAC_FLAG_PAN_COMPRESSION) && header->source_pan != header->destination_pan)
        return MAC_CODEC_INVALID_HEADER;
    if (header->source_mode == MAC_ADDRESS_SHORT && read_le16(header->source) == 0xffffu)
        return MAC_CODEC_INVALID_HEADER;
    if ((header->flags & MAC_FLAG_ACK_REQUEST) && header->destination_mode == MAC_ADDRESS_SHORT
            && read_le16(header->destination) == 0xffffu)
        return MAC_CODEC_INVALID_HEADER;
    return MAC_CODEC_OK;
}

mac_codec_result_t mac_frame_decode(const uint8_t *body, uint16_t length,
                                    mac_frame_info_t *result)
{
    mac_frame_info_t candidate;
    mac_codec_result_t status;
    uint8_t destination_size, source_size, header_size, position, i;

    if (body == NULL || result == NULL)
        return MAC_CODEC_INVALID_ARGUMENT;
    if (length > MAC_FRAME_MAX_BODY)
        return MAC_CODEC_TOO_LONG;
    if (length < 3u)
        return MAC_CODEC_TRUNCATED;
    /* Legacy frames cannot use sequence suppression or information elements. */
    if (body[1] & 0x03u)
        return MAC_CODEC_INVALID_HEADER;
    memset(&candidate, 0, sizeof(candidate));
    candidate.header.type = body[0] & 0x07u;
    candidate.header.flags = body[0] & 0xf8u;
    candidate.header.version = (body[1] >> 4) & 0x03u;
    candidate.header.destination_mode = (body[1] >> 2) & 0x03u;
    candidate.header.source_mode = (body[1] >> 6) & 0x03u;
    candidate.header.sequence = body[2];
    status = header_shape(&candidate.header, &destination_size, &source_size, &header_size);
    if (status != MAC_CODEC_OK)
        return status;
    if (length < header_size)
        return MAC_CODEC_TRUNCATED;
    if (candidate.header.type == MAC_FRAME_ACK && length != header_size)
        return MAC_CODEC_INVALID_HEADER;
    position = 3;
    if (candidate.header.type == MAC_FRAME_DATA) {
        candidate.header.destination_pan = read_le16(body + position);
        position += 2;
        for (i = 0; i < destination_size; i++)
            candidate.header.destination[i] = body[position++];
        if (candidate.header.flags & MAC_FLAG_PAN_COMPRESSION) {
            candidate.header.source_pan = candidate.header.destination_pan;
        } else {
            candidate.header.source_pan = read_le16(body + position);
            position += 2;
        }
        for (i = 0; i < source_size; i++)
            candidate.header.source[i] = body[position++];
    }
    status = address_policy(&candidate.header);
    if (status != MAC_CODEC_OK)
        return status;
    candidate.payload_offset = header_size;
    candidate.payload_length = (uint8_t)(length - header_size);
    *result = candidate;
    return MAC_CODEC_OK;
}

mac_codec_result_t mac_frame_encode(const mac_header_t *header,
                                    const uint8_t *payload, uint16_t payload_length,
                                    uint8_t *body, uint16_t capacity, uint8_t *length)
{
    mac_codec_result_t status;
    uint8_t destination_size, source_size, header_size, position, i;

    if (header == NULL || body == NULL || length == NULL || (payload == NULL && payload_length != 0u))
        return MAC_CODEC_INVALID_ARGUMENT;
    status = header_shape(header, &destination_size, &source_size, &header_size);
    if (status != MAC_CODEC_OK)
        return status;
    status = address_policy(header);
    if (status != MAC_CODEC_OK)
        return status;
    if (header->type == MAC_FRAME_ACK && payload_length != 0u)
        return MAC_CODEC_INVALID_HEADER;
    if (payload_length > MAC_FRAME_MAX_BODY - header_size)
        return MAC_CODEC_TOO_LONG;
    if (capacity < header_size + payload_length)
        return MAC_CODEC_BUFFER_TOO_SMALL;
    body[0] = (uint8_t)(header->type | header->flags);
    body[1] = (uint8_t)((header->destination_mode << 2) | (header->version << 4)
                       | (header->source_mode << 6));
    body[2] = header->sequence;
    position = 3;
    if (header->type == MAC_FRAME_DATA) {
        write_le16(body + position, header->destination_pan);
        position += 2;
        for (i = 0; i < destination_size; i++)
            body[position++] = header->destination[i];
        if (!(header->flags & MAC_FLAG_PAN_COMPRESSION)) {
            write_le16(body + position, header->source_pan);
            position += 2;
        }
        for (i = 0; i < source_size; i++)
            body[position++] = header->source[i];
    }
    for (i = 0; i < payload_length; i++)
        body[position++] = payload[i];
    *length = position;
    return MAC_CODEC_OK;
}
