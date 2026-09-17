/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "zcl_attributes.h"

#include <stddef.h>
#include <string.h>

zcl_codec_result_t zcl_attr_set_check(const zcl_attribute_set_t *set)
{
    uint8_t i, j;
    if (set == NULL)
        return ZCL_CODEC_INVALID_ARGUMENT;
    if (set->count > ZCL_ATTRIBUTE_MAX_COUNT || set->side > ZCL_ATTRIBUTE_CLIENT
            || set->manufacturer_specific > 1u || (set->attributes == NULL && set->count != 0u))
        return ZCL_CODEC_INVALID_TABLE;
    for (i = 0; i < set->count; i++) {
        if (set->attributes[i].readable > 1u)
            return ZCL_CODEC_INVALID_TABLE;
        for (j = 0; j < i; j++)
            if (set->attributes[i].id == set->attributes[j].id)
                return ZCL_CODEC_INVALID_TABLE;
    }
    return ZCL_CODEC_OK;
}

zcl_codec_result_t zcl_read_attrs_unicast(const zcl_attribute_set_t * volatile set,
                                          const uint8_t * volatile request, uint16_t request_length,
                                          uint8_t * volatile response, uint16_t capacity,
                                          zcl_read_info_t * volatile info)
{
    zcl_frame_info_t frame;
    zcl_header_t reply;
    zcl_read_info_t candidate;
    zcl_codec_result_t status;
    const zcl_attribute_t * volatile attribute;
    const uint8_t * volatile ids;
    uint8_t payload[ZCL_FRAME_MAX_BODY - ZCL_FRAME_MIN_HEADER];
    uint16_t id;
    uint8_t budget, header_size, used, remaining, index, position, encoded, wire_status;

    if (set == NULL || request == NULL || response == NULL || info == NULL)
        return ZCL_CODEC_INVALID_ARGUMENT;
    status = zcl_attr_set_check(set);
    if (status != ZCL_CODEC_OK)
        return status;
    status = zcl_frame_decode(request, request_length, &frame);
    if (status != ZCL_CODEC_OK)
        return status;
    if (frame.header.type != ZCL_FRAME_GLOBAL || frame.header.command_id != ZCL_COMMAND_READ_ATTRIBUTES)
        return ZCL_CODEC_UNSUPPORTED_COMMAND;
    if (((frame.header.flags & ZCL_FLAG_SERVER_TO_CLIENT) != 0u) != set->side
            || ((frame.header.flags & ZCL_FLAG_MANUFACTURER_SPECIFIC) != 0u) != set->manufacturer_specific
            || (set->manufacturer_specific && frame.header.manufacturer_code != set->manufacturer_code))
        return ZCL_CODEC_UNSUPPORTED_CONTEXT;
    reply = frame.header;
    reply.flags = (uint8_t)((reply.flags ^ ZCL_FLAG_SERVER_TO_CLIENT) | ZCL_FLAG_DISABLE_DEFAULT_RESPONSE);
    reply.command_id = ZCL_COMMAND_READ_ATTRIBUTES_RESPONSE;
    header_size = frame.payload_offset;
    budget = capacity < ZCL_FRAME_MAX_BODY ? (uint8_t)capacity : ZCL_FRAME_MAX_BODY;
    memset(&candidate, 0, sizeof(candidate));
    used = 0;
    if (frame.payload_length == 0 || (frame.payload_length & 1u)) {
        reply.command_id = ZCL_COMMAND_DEFAULT_RESPONSE;
        payload[0] = ZCL_COMMAND_READ_ATTRIBUTES;
        payload[1] = ZCL_STATUS_MALFORMED_COMMAND;
        used = 2;
    } else {
        if (budget < header_size + 3u)
            return ZCL_CODEC_BUFFER_TOO_SMALL;
        candidate.requested_count = frame.payload_length / 2u;
        ids = request + frame.payload_offset;
        for (position = 0; position < frame.payload_length; position += 2) {
            remaining = (uint8_t)(budget - header_size - used);
            if (remaining < 3u)
                break;
            id = (uint16_t)((uint16_t)ids[position] | ((uint16_t)ids[position + 1u] << 8));
            attribute = NULL;
            for (index = 0; index < set->count; index++) {
                if (set->attributes[index].id == id) {
                    attribute = &set->attributes[index];
                    break;
                }
            }
            wire_status = ZCL_STATUS_UNSUPPORTED_ATTRIBUTE;
            encoded = 0;
            if (attribute != NULL) {
                if (!attribute->readable) {
                    wire_status = ZCL_STATUS_NOT_AUTHORIZED;
                } else {
                    /* With only three bytes left, validate without writing a success value. */
                    status = zcl_value_encode(&attribute->value,
                                              payload + used + (remaining >= 4u ? 4u : 0u),
                                              remaining >= 4u ? (uint16_t)(remaining - 4u) : 0u,
                                              &encoded);
                    if (status == ZCL_CODEC_BUFFER_TOO_SMALL || (status == ZCL_CODEC_OK && remaining < 4u)) {
                        wire_status = ZCL_STATUS_INSUFFICIENT_SPACE;
                    } else if (status != ZCL_CODEC_OK) {
                        return status;
                    } else {
                        wire_status = ZCL_STATUS_SUCCESS;
                    }
                }
            }
            payload[used++] = (uint8_t)id;
            payload[used++] = (uint8_t)(id >> 8);
            payload[used++] = wire_status;
            if (wire_status == ZCL_STATUS_SUCCESS) {
                payload[used++] = attribute->value.type;
                used += encoded;
            }
            candidate.returned_count++;
        }
    }
    status = zcl_frame_encode(&reply, payload, used, response, capacity, &candidate.length);
    if (status != ZCL_CODEC_OK)
        return status;
    candidate.command_id = reply.command_id;
    *info = candidate;
    return ZCL_CODEC_OK;
}
