/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "zcl_dispatch.h"
#include "zcl_write.h"

#include <stddef.h>
#include <string.h>

static uint8_t normalized_status(uint8_t status)
{
    if (status >= 0x82u && status <= 0x84u)
        return ZCL_STATUS_UNSUP_COMMAND;
    if (status == 0x8au || status == 0xc4u)
        return ZCL_STATUS_SUCCESS;
    if (status == 0x8fu)
        return ZCL_STATUS_NOT_AUTHORIZED;
    if (status == 0x90u || status == 0x91u || status == 0x93u
            || status == 0xc0u || status == 0xc1u)
        return 0x01u;
    return status;
}

static const zcl_attribute_t *next_attribute(const zcl_attribute_set_t *set, uint16_t start)
{
    const zcl_attribute_t *current = set->attributes;
    const zcl_attribute_t *chosen = NULL;
    uint8_t count = set->count;
    while (count--) {
        if (current->id >= start && (chosen == NULL || current->id < chosen->id))
            chosen = current;
        current++;
    }
    return chosen;
}

static zcl_codec_result_t discover(const zcl_attribute_set_t * volatile set, const uint8_t * volatile request,
                                    zcl_header_t * volatile reply, uint8_t * volatile response, uint16_t capacity,
                                    zcl_dispatch_info_t * volatile info)
{
    uint8_t payload[1u + 3u * ZCL_ATTRIBUTE_MAX_COUNT];
    const zcl_attribute_t * volatile attribute;
    uint8_t * volatile out = payload + 1;
    uint16_t start;
    uint8_t budget, header_size, limit, count = 0;
    zcl_codec_result_t status;

    start = (uint16_t)((uint16_t)request[0] | ((uint16_t)request[1] << 8));
    info->requested_count = request[2];
    header_size = reply->flags & ZCL_FLAG_MANUFACTURER_SPECIFIC ? ZCL_FRAME_MAX_HEADER : ZCL_FRAME_MIN_HEADER;
    budget = capacity < ZCL_FRAME_MAX_BODY ? (uint8_t)capacity : ZCL_FRAME_MAX_BODY;
    if (budget < header_size + 1u)
        return ZCL_CODEC_BUFFER_TOO_SMALL;
    limit = (uint8_t)((budget - header_size - 1u) / 3u);
    if (limit > request[2])
        limit = request[2];
    payload[0] = 1;
    for (;;) {
        attribute = next_attribute(set, start);
        if (attribute == NULL)
            break;
        if (count == limit) {
            if (limit == 0 && request[2] != 0)
                return ZCL_CODEC_BUFFER_TOO_SMALL;
            payload[0] = 0;
            break;
        }
        if (!zcl_value_type_supported(attribute->value.type))
            return ZCL_CODEC_UNSUPPORTED_DATA_TYPE;
        start = attribute->id;
        *out++ = (uint8_t)start;
        *out++ = (uint8_t)(start >> 8);
        *out++ = attribute->value.type;
        count++;
        if (start == 0xffffu)
            break;
        start++;
    }
    reply->command_id = ZCL_COMMAND_DISCOVER_RESPONSE;
    status = zcl_frame_encode(reply, payload, (uint16_t)(1u + 3u * count), response, capacity, &info->length);
    if (status != ZCL_CODEC_OK)
        return status;
    info->discovery_complete = payload[0];
    info->returned_count = count;
    info->command_id = reply->command_id;
    return ZCL_CODEC_OK;
}

zcl_codec_result_t zcl_dispatch_unicast(const zcl_attribute_set_t * volatile set,
                                       const uint8_t * volatile request, uint16_t request_length,
                                       uint8_t * volatile response, uint16_t capacity,
                                       zcl_dispatch_info_t * volatile info)
{
    zcl_frame_info_t frame;
    zcl_header_t reply;
    zcl_read_info_t read;
    zcl_dispatch_info_t candidate;
    zcl_codec_result_t status;
    const uint8_t * volatile payload;
    uint8_t error[2];
    volatile uint8_t context_matches;

    if (set == NULL || request == NULL || response == NULL || info == NULL)
        return ZCL_CODEC_INVALID_ARGUMENT;
    status = zcl_attr_set_check(set);
    if (status != ZCL_CODEC_OK)
        return status;
    status = zcl_frame_decode(request, request_length, &frame);
    if (status != ZCL_CODEC_OK)
        return status;
    if (((frame.header.flags & ZCL_FLAG_SERVER_TO_CLIENT) != 0u) != set->side)
        return ZCL_CODEC_UNSUPPORTED_CONTEXT;
    context_matches = ((frame.header.flags & ZCL_FLAG_MANUFACTURER_SPECIFIC) != 0u) == set->manufacturer_specific
        && (!set->manufacturer_specific || frame.header.manufacturer_code == set->manufacturer_code);
    payload = request + frame.payload_offset;
    memset(&candidate, 0, sizeof(candidate));
    candidate.sequence = frame.header.sequence;
    if (frame.header.type == ZCL_FRAME_GLOBAL && frame.header.command_id == ZCL_COMMAND_DEFAULT_RESPONSE) {
        if (!context_matches)
            return ZCL_CODEC_UNSUPPORTED_CONTEXT;
        if (frame.payload_length < 2u)
            return ZCL_CODEC_TRUNCATED;
        if (set->manufacturer_specific && frame.payload_length != 2u)
            return ZCL_CODEC_UNSUPPORTED_LAYOUT;
        candidate.kind = ZCL_DISPATCH_DEFAULT_RECEIVED;
        candidate.command_id = ZCL_COMMAND_DEFAULT_RESPONSE;
        candidate.default_command = payload[0];
        candidate.default_raw_status = payload[1];
        candidate.default_status = normalized_status(payload[1]);
        *info = candidate;
        return ZCL_CODEC_OK;
    }
    if (context_matches && frame.header.type == ZCL_FRAME_GLOBAL
            && (frame.header.command_id == ZCL_COMMAND_WRITE_ATTRIBUTES
                || frame.header.command_id == ZCL_COMMAND_WRITE_UNDIVIDED
                || frame.header.command_id == ZCL_COMMAND_WRITE_NO_RESPONSE)) {
        status = zcl_wr_handle(set, &frame, payload, response, capacity, &candidate, NULL);
        if (status == ZCL_CODEC_OK)
            *info = candidate;
        return status;
    }
    if (frame.header.type == ZCL_FRAME_GLOBAL && frame.header.command_id == ZCL_COMMAND_WRITE_NO_RESPONSE)
        return ZCL_CODEC_UNSUPPORTED_NO_RESPONSE;
    reply = frame.header;
    reply.type = ZCL_FRAME_GLOBAL;
    reply.flags = (uint8_t)((reply.flags ^ ZCL_FLAG_SERVER_TO_CLIENT) | ZCL_FLAG_DISABLE_DEFAULT_RESPONSE);
    error[0] = frame.header.command_id;
    error[1] = ZCL_STATUS_UNSUP_COMMAND;
    if (context_matches && frame.header.type == ZCL_FRAME_GLOBAL) {
        if (frame.header.command_id == ZCL_COMMAND_READ_ATTRIBUTES) {
            status = zcl_read_attrs_unicast(set, request, request_length, response, capacity, &read);
            if (status != ZCL_CODEC_OK)
                return status;
            candidate.command_id = read.command_id;
            candidate.length = read.length;
            candidate.requested_count = read.requested_count;
            candidate.returned_count = read.returned_count;
            if (read.command_id == ZCL_COMMAND_DEFAULT_RESPONSE) {
                candidate.default_command = ZCL_COMMAND_READ_ATTRIBUTES;
                candidate.default_status = candidate.default_raw_status = ZCL_STATUS_MALFORMED_COMMAND;
            }
            *info = candidate;
            return ZCL_CODEC_OK;
        }
        if (frame.header.command_id == ZCL_COMMAND_DISCOVER_ATTRIBUTES) {
            if (frame.payload_length < 3u) {
                error[1] = ZCL_STATUS_MALFORMED_COMMAND;
            } else {
                if (set->manufacturer_specific && frame.payload_length != 3u)
                    return ZCL_CODEC_UNSUPPORTED_LAYOUT;
                status = discover(set, payload, &reply, response, capacity, &candidate);
                if (status != ZCL_CODEC_OK)
                    return status;
                *info = candidate;
                return ZCL_CODEC_OK;
            }
        }
    }
    reply.command_id = ZCL_COMMAND_DEFAULT_RESPONSE;
    status = zcl_frame_encode(&reply, error, sizeof(error), response, capacity, &candidate.length);
    if (status != ZCL_CODEC_OK)
        return status;
    candidate.command_id = reply.command_id;
    candidate.default_command = error[0];
    candidate.default_status = candidate.default_raw_status = error[1];
    *info = candidate;
    return ZCL_CODEC_OK;
}
