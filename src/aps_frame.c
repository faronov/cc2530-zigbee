/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "aps_frame.h"

#include <stddef.h>
#include <string.h>
#if defined(CC2530_MAC_LINK_WORKSPACE)
#include "mac_link_workspace_guard_internal.h"
#endif

static inline uint16_t read_le16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

static inline void write_le16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static aps_codec_result_t validate_header(const aps_header_t *header)
{
    if (header->type != APS_FRAME_DATA)
        return APS_CODEC_UNSUPPORTED_TYPE;
    if (header->flags & APS_FLAG_SECURITY)
        return APS_CODEC_UNSUPPORTED_SECURITY;
    if (header->flags & (APS_FLAG_ACK_FORMAT | APS_FLAG_EXTENDED_HEADER))
        return APS_CODEC_UNSUPPORTED_LAYOUT;
    if (header->flags & (uint8_t)~APS_FLAG_ACK_REQUEST)
        return APS_CODEC_INVALID_HEADER;
    if (header->delivery_mode == 1u || header->delivery_mode > APS_DELIVERY_GROUP)
        return APS_CODEC_INVALID_HEADER;
    if (header->delivery_mode != APS_DELIVERY_UNICAST)
        return APS_CODEC_UNSUPPORTED_DELIVERY;
    if (header->source_endpoint == 0xffu)
        return APS_CODEC_INVALID_HEADER;
    return APS_CODEC_OK;
}

aps_codec_result_t aps_frame_decode(const uint8_t *body, uint16_t length,
                                    aps_frame_info_t *result)
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    if (!LW_IO(LW_CHILD_APS, body, length, 0) || !LW_IO(LW_CHILD_APS, result, sizeof(*result), 1))
        return APS_CODEC_INVALID_ARGUMENT;
#endif
    aps_frame_info_t candidate;
    aps_codec_result_t status;

    if (body == NULL || result == NULL)
        return APS_CODEC_INVALID_ARGUMENT;
    if (length > APS_FRAME_MAX_BODY)
        return APS_CODEC_TOO_LONG;
    if (length == 0)
        return APS_CODEC_TRUNCATED;
    if ((body[0] & 3u) != APS_FRAME_DATA)
        return APS_CODEC_UNSUPPORTED_TYPE;
    if (length < APS_FRAME_HEADER_SIZE)
        return APS_CODEC_TRUNCATED;
    memset(&candidate, 0, sizeof(candidate));
    candidate.header.type = body[0] & 3u;
    candidate.header.delivery_mode = (body[0] >> 2) & 3u;
    candidate.header.flags = body[0] & 0xf0u;
    candidate.header.destination_endpoint = body[1];
    candidate.header.cluster_id = read_le16(body + 2);
    candidate.header.profile_id = read_le16(body + 4);
    candidate.header.source_endpoint = body[6];
    candidate.header.counter = body[7];
    status = validate_header(&candidate.header);
    if (status != APS_CODEC_OK)
        return status;
    candidate.payload_offset = APS_FRAME_HEADER_SIZE;
    candidate.payload_length = (uint8_t)(length - APS_FRAME_HEADER_SIZE);
    *result = candidate;
    return APS_CODEC_OK;
}

/* Keep emission a leaf so SDCC can overlay its temporary IRAM. */
static void emit_frame(const aps_header_t *header, const uint8_t *payload, uint8_t payload_length,
                        uint8_t *body)
{
    uint8_t i;
    body[0] = (uint8_t)(header->type | (header->delivery_mode << 2) | header->flags);
    body[1] = header->destination_endpoint;
    write_le16(body + 2, header->cluster_id);
    write_le16(body + 4, header->profile_id);
    body[6] = header->source_endpoint;
    body[7] = header->counter;
    for (i = 0; i < payload_length; i++)
        body[APS_FRAME_HEADER_SIZE + i] = payload[i];
}

aps_codec_result_t aps_frame_encode(const aps_header_t *header,
                                    const uint8_t *payload, uint16_t payload_length,
                                    uint8_t *body, uint16_t capacity, uint8_t *length)
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    if (!LW_IO(LW_CHILD_APS, header, sizeof(*header), 0) ||
        !LW_IO(LW_CHILD_APS, payload, payload_length, 0) ||
        !LW_IO(LW_CHILD_APS, body, capacity, 1) || !LW_IO(LW_CHILD_APS, length, 1, 1))
        return APS_CODEC_INVALID_ARGUMENT;
#endif
    aps_codec_result_t status;

    if (header == NULL || body == NULL || length == NULL
            || (payload == NULL && payload_length != 0u))
        return APS_CODEC_INVALID_ARGUMENT;
    status = validate_header(header);
    if (status != APS_CODEC_OK)
        return status;
    if (payload_length > APS_FRAME_MAX_BODY - APS_FRAME_HEADER_SIZE)
        return APS_CODEC_TOO_LONG;
    if (capacity < APS_FRAME_HEADER_SIZE + payload_length)
        return APS_CODEC_BUFFER_TOO_SMALL;
    emit_frame(header, payload, (uint8_t)payload_length, body);
    *length = (uint8_t)(APS_FRAME_HEADER_SIZE + payload_length);
    return APS_CODEC_OK;
}
