/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "nwk_frame.h"

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

static nwk_codec_result_t header_shape(const nwk_header_t *header, uint8_t *size)
{
    if (header->type != NWK_FRAME_DATA)
        return NWK_CODEC_UNSUPPORTED_TYPE;
    if (header->version != NWK_FRAME_PROTOCOL_VERSION)
        return NWK_CODEC_UNSUPPORTED_VERSION;
    if (header->flags & NWK_FLAG_SECURITY)
        return NWK_CODEC_UNSUPPORTED_SECURITY;
    if (header->flags & (NWK_FLAG_MULTICAST | NWK_FLAG_SOURCE_ROUTE))
        return NWK_CODEC_UNSUPPORTED_LAYOUT;
    if (header->flags & (uint16_t)~(NWK_FLAG_DESTINATION_IEEE | NWK_FLAG_SOURCE_IEEE
                                    | NWK_FLAG_END_DEVICE_INITIATOR))
        return NWK_CODEC_INVALID_HEADER;
    if (header->discover_route > NWK_DISCOVER_ROUTE_ENABLE || header->source >= 0xfff8u)
        return NWK_CODEC_INVALID_HEADER;
    if (header->destination >= 0xfff8u) {
        if (header->destination < 0xfffbu || header->destination == 0xfffeu
                || header->discover_route != NWK_DISCOVER_ROUTE_SUPPRESS
                || (header->flags & NWK_FLAG_DESTINATION_IEEE))
            return NWK_CODEC_INVALID_HEADER;
    }
    *size = NWK_FRAME_MIN_HEADER;
    if (header->flags & NWK_FLAG_DESTINATION_IEEE)
        *size += 8;
    if (header->flags & NWK_FLAG_SOURCE_IEEE)
        *size += 8;
    return NWK_CODEC_OK;
}

nwk_codec_result_t nwk_frame_decode(const uint8_t *body, uint16_t length,
                                    nwk_frame_info_t *result)
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    if (!LW_IO(LW_CHILD_NWK, body, length, 0) || !LW_IO(LW_CHILD_NWK, result, sizeof(*result), 1))
        return NWK_CODEC_INVALID_ARGUMENT;
#endif
    nwk_frame_info_t candidate;
    nwk_codec_result_t status;
    uint16_t control;
    uint8_t size, position;

    if (body == NULL || result == NULL)
        return NWK_CODEC_INVALID_ARGUMENT;
    if (length > NWK_FRAME_MAX_BODY)
        return NWK_CODEC_TOO_LONG;
    if (length < NWK_FRAME_MIN_HEADER)
        return NWK_CODEC_TRUNCATED;
    memset(&candidate, 0, sizeof(candidate));
    control = read_le16(body);
    candidate.header.type = (uint8_t)(control & 3u);
    candidate.header.version = (uint8_t)((control >> 2) & 15u);
    candidate.header.discover_route = (uint8_t)((control >> 6) & 3u);
    candidate.header.flags = control & 0xff00u;
    candidate.header.destination = read_le16(body + 2);
    candidate.header.source = read_le16(body + 4);
    candidate.header.radius = body[6];
    candidate.header.sequence = body[7];
    status = header_shape(&candidate.header, &size);
    if (status != NWK_CODEC_OK)
        return status;
    if (length < size)
        return NWK_CODEC_TRUNCATED;
    position = NWK_FRAME_MIN_HEADER;
    if (candidate.header.flags & NWK_FLAG_DESTINATION_IEEE) {
        memcpy(candidate.header.destination_ieee, body + position, 8);
        position += 8;
    }
    if (candidate.header.flags & NWK_FLAG_SOURCE_IEEE)
        memcpy(candidate.header.source_ieee, body + position, 8);
    candidate.payload_offset = size;
    candidate.payload_length = (uint8_t)(length - size);
    *result = candidate;
    return NWK_CODEC_OK;
}

/* Keep emission a leaf so SDCC can overlay its temporary IRAM. */
static void emit_frame(const nwk_header_t *header, const uint8_t *payload, uint8_t payload_length,
                        uint8_t *body)
{
    uint8_t position = NWK_FRAME_MIN_HEADER, i;
    body[0] = (uint8_t)(header->type | (header->version << 2) | (header->discover_route << 6));
    body[1] = (uint8_t)(header->flags >> 8);
    write_le16(body + 2, header->destination);
    write_le16(body + 4, header->source);
    body[6] = header->radius;
    body[7] = header->sequence;
    if (header->flags & NWK_FLAG_DESTINATION_IEEE) {
        for (i = 0; i < 8; i++)
            body[position++] = header->destination_ieee[i];
    }
    if (header->flags & NWK_FLAG_SOURCE_IEEE) {
        for (i = 0; i < 8; i++)
            body[position++] = header->source_ieee[i];
    }
    for (i = 0; i < payload_length; i++)
        body[position++] = payload[i];
}

nwk_codec_result_t nwk_frame_encode(const nwk_header_t *header,
                                    const uint8_t *payload, uint16_t payload_length,
                                    uint8_t *body, uint16_t capacity, uint8_t *length)
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    if (!LW_IO(LW_CHILD_NWK, header, sizeof(*header), 0) ||
        !LW_IO(LW_CHILD_NWK, payload, payload_length, 0) ||
        !LW_IO(LW_CHILD_NWK, body, capacity, 1) || !LW_IO(LW_CHILD_NWK, length, 1, 1))
        return NWK_CODEC_INVALID_ARGUMENT;
#endif
    nwk_codec_result_t status;
    uint8_t size;

    if (header == NULL || body == NULL || length == NULL
            || (payload == NULL && payload_length != 0u))
        return NWK_CODEC_INVALID_ARGUMENT;
    status = header_shape(header, &size);
    if (status != NWK_CODEC_OK)
        return status;
    if (payload_length > NWK_FRAME_MAX_BODY - size)
        return NWK_CODEC_TOO_LONG;
    if (capacity < size + payload_length)
        return NWK_CODEC_BUFFER_TOO_SMALL;
    emit_frame(header, payload, (uint8_t)payload_length, body);
    *length = (uint8_t)(size + payload_length);
    return NWK_CODEC_OK;
}
