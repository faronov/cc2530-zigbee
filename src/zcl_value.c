/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "zcl_wire.h"

#include <stddef.h>
#include <string.h>

#define KIND_RAW 0u
#define KIND_UNSIGNED 1u
#define KIND_SIGNED 2u
#define KIND_BOOLEAN 3u
#define KIND_STRING 4u

static zcl_codec_result_t value_shape(uint8_t type, uint8_t *width, uint8_t *kind)
{
    *kind = KIND_RAW;
    if (type == ZCL_TYPE_NO_DATA) {
        *width = 0;
    } else if (type >= ZCL_TYPE_DATA8 && type <= ZCL_TYPE_DATA64) {
        *width = (uint8_t)(type - ZCL_TYPE_DATA8 + 1u);
    } else if (type >= ZCL_TYPE_BITMAP8 && type <= ZCL_TYPE_INT64) {
        *width = (uint8_t)((type & 7u) + 1u);
        if (type >= ZCL_TYPE_INT8)
            *kind = KIND_SIGNED;
        else if (type >= ZCL_TYPE_UINT8)
            *kind = KIND_UNSIGNED;
    } else if (type == ZCL_TYPE_BOOLEAN) {
        *width = 1;
        *kind = KIND_BOOLEAN;
    } else if (type == ZCL_TYPE_ENUM8 || type == ZCL_TYPE_ENUM16) {
        *width = (uint8_t)(type - ZCL_TYPE_ENUM8 + 1u);
        *kind = KIND_UNSIGNED;
    } else if (type == ZCL_TYPE_OCTET_STRING || type == ZCL_TYPE_CHARACTER_STRING) {
        *width = 0;
        *kind = KIND_STRING;
    } else {
        return ZCL_CODEC_UNSUPPORTED_DATA_TYPE;
    }
    return ZCL_CODEC_OK;
}

static uint8_t non_value_pattern(uint8_t kind, const uint8_t *data, uint8_t width)
{
    uint8_t i;
    if (kind == KIND_RAW)
        return 0;
    if (kind == KIND_SIGNED) {
        if (data[width - 1u] != 0x80u)
            return 0;
        for (i = 0; i < width - 1u; i++)
            if (data[i] != 0)
                return 0;
    } else {
        for (i = 0; i < width; i++)
            if (data[i] != 0xffu)
                return 0;
    }
    return 1;
}

zcl_codec_result_t zcl_value_decode(uint8_t type, const uint8_t *body, uint16_t length,
                                    zcl_value_info_t *result)
{
    zcl_value_info_t candidate;
    zcl_codec_result_t status;
    uint8_t width, kind;

    if (body == NULL || result == NULL)
        return ZCL_CODEC_INVALID_ARGUMENT;
    status = value_shape(type, &width, &kind);
    if (status != ZCL_CODEC_OK)
        return status;
    memset(&candidate, 0, sizeof(candidate));
    candidate.type = type;
    if (kind == KIND_STRING) {
        if (length == 0)
            return ZCL_CODEC_TRUNCATED;
        candidate.data_offset = 1;
        candidate.non_value_pattern = body[0] == 0xffu;
        width = candidate.non_value_pattern ? 0 : body[0];
    }
    if (length < (uint16_t)candidate.data_offset + width)
        return ZCL_CODEC_TRUNCATED;
    if (kind == KIND_BOOLEAN && body[0] != 0 && body[0] != 1 && body[0] != 0xffu)
        return ZCL_CODEC_INVALID_VALUE;
    if (kind != KIND_STRING)
        candidate.non_value_pattern = non_value_pattern(kind, body, width);
    candidate.data_length = width;
    candidate.encoded_length = (uint8_t)(candidate.data_offset + width);
    *result = candidate;
    return ZCL_CODEC_OK;
}

zcl_codec_result_t zcl_value_encode(const zcl_value_t *value, uint8_t *body,
                                    uint16_t capacity, uint8_t *length)
{
    zcl_codec_result_t status;
    uint8_t width, kind, size, position;

    if (value == NULL || body == NULL || length == NULL
            || (value->data == NULL && value->data_length != 0u))
        return ZCL_CODEC_INVALID_ARGUMENT;
    status = value_shape(value->type, &width, &kind);
    if (status != ZCL_CODEC_OK)
        return status;
    if (kind == KIND_STRING) {
        if (value->string_non_value > 1u || (value->string_non_value && value->data_length != 0u))
            return ZCL_CODEC_INVALID_VALUE;
        if (value->data_length > 254u)
            return ZCL_CODEC_TOO_LONG;
        size = (uint8_t)(1u + value->data_length);
    } else {
        if (value->string_non_value || value->data_length != width)
            return ZCL_CODEC_INVALID_VALUE;
        if (kind == KIND_BOOLEAN && value->data[0] != 0 && value->data[0] != 1 && value->data[0] != 0xffu)
            return ZCL_CODEC_INVALID_VALUE;
        size = width;
    }
    if (capacity < size)
        return ZCL_CODEC_BUFFER_TOO_SMALL;
    position = 0;
    if (kind == KIND_STRING)
        body[position++] = value->string_non_value ? 0xffu : (uint8_t)value->data_length;
    if (size > position)
        memcpy(body + position, value->data, (uint16_t)(size - position));
    *length = size;
    return ZCL_CODEC_OK;
}
