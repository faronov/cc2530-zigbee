/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "zcl_wire.h"

#include <stddef.h>
#include <string.h>

zcl_codec_result_t zcl_frame_decode(const uint8_t *body, uint16_t length,
                                    zcl_frame_info_t *result)
{
    zcl_frame_info_t candidate;
    uint8_t size, position;

    if (body == NULL || result == NULL)
        return ZCL_CODEC_INVALID_ARGUMENT;
    if (length > ZCL_FRAME_MAX_BODY)
        return ZCL_CODEC_TOO_LONG;
    if (length == 0)
        return ZCL_CODEC_TRUNCATED;
    if ((body[0] & 3u) > ZCL_FRAME_CLUSTER_SPECIFIC)
        return ZCL_CODEC_UNSUPPORTED_FRAME_TYPE;
    if ((body[0] & ZCL_FLAG_MANUFACTURER_SPECIFIC) && (body[0] & 0xe0u))
        return ZCL_CODEC_UNSUPPORTED_LAYOUT;
    size = body[0] & ZCL_FLAG_MANUFACTURER_SPECIFIC ? ZCL_FRAME_MAX_HEADER : ZCL_FRAME_MIN_HEADER;
    if (length < size)
        return ZCL_CODEC_TRUNCATED;
    memset(&candidate, 0, sizeof(candidate));
    candidate.header.type = body[0] & 3u;
    candidate.header.flags = body[0] & 0x1cu;
    candidate.ignored_control_bits = body[0] & 0xe0u;
    position = 1;
    if (candidate.header.flags & ZCL_FLAG_MANUFACTURER_SPECIFIC) {
        candidate.header.manufacturer_code = (uint16_t)((uint16_t)body[1] | ((uint16_t)body[2] << 8));
        position = 3;
    }
    candidate.header.sequence = body[position++];
    candidate.header.command_id = body[position];
    candidate.payload_offset = size;
    candidate.payload_length = (uint8_t)(length - size);
    *result = candidate;
    return ZCL_CODEC_OK;
}

zcl_codec_result_t zcl_frame_encode(const zcl_header_t *header,
                                    const uint8_t *payload, uint16_t payload_length,
                                    uint8_t *body, uint16_t capacity, uint8_t *length)
{
    uint8_t size, position, i;

    if (header == NULL || body == NULL || length == NULL
            || (payload == NULL && payload_length != 0u))
        return ZCL_CODEC_INVALID_ARGUMENT;
    if (header->type > ZCL_FRAME_CLUSTER_SPECIFIC)
        return ZCL_CODEC_UNSUPPORTED_FRAME_TYPE;
    if (header->flags & (uint8_t)~0x1cu)
        return ZCL_CODEC_INVALID_HEADER;
    size = header->flags & ZCL_FLAG_MANUFACTURER_SPECIFIC ? ZCL_FRAME_MAX_HEADER : ZCL_FRAME_MIN_HEADER;
    if (payload_length > ZCL_FRAME_MAX_BODY - size)
        return ZCL_CODEC_TOO_LONG;
    if (capacity < size + payload_length)
        return ZCL_CODEC_BUFFER_TOO_SMALL;
    body[0] = header->type | header->flags;
    position = 1;
    if (header->flags & ZCL_FLAG_MANUFACTURER_SPECIFIC) {
        body[position++] = (uint8_t)header->manufacturer_code;
        body[position++] = (uint8_t)(header->manufacturer_code >> 8);
    }
    body[position++] = header->sequence;
    body[position++] = header->command_id;
    for (i = 0; i < payload_length; i++)
        body[position++] = payload[i];
    *length = position;
    return ZCL_CODEC_OK;
}
