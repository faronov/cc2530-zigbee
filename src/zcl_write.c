/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "zcl_write.h"

#include <stddef.h>

zcl_codec_result_t zcl_wr_handle(const zcl_attribute_set_t * volatile set,
                                const zcl_frame_info_t * volatile frame,
                                const uint8_t * volatile payload,
                                uint8_t * volatile response, uint16_t capacity,
                                zcl_dispatch_info_t * volatile info,
                                zcl_wr_t * volatile edit)
{
    zcl_header_t reply;
    zcl_value_info_t value;
    uint8_t body[ZCL_FRAME_MAX_BODY - ZCL_FRAME_MIN_HEADER];
    const zcl_attribute_t * volatile attr;
    volatile uint16_t id;
    volatile uint8_t type;
    uint16_t last = 0;
    uint8_t used = 0, i, error, wrote = 0, count = 0, errors = 0, encoded;
    uint8_t size = frame->payload_length, command = frame->header.command_id;
    zcl_codec_result_t rc = ZCL_CODEC_TRUNCATED;

    info->sequence = frame->header.sequence;
    while (size) {
        rc = ZCL_CODEC_TRUNCATED;
        if (size < 3u)
            break;
        id = (uint16_t)((uint16_t)payload[0] | ((uint16_t)payload[1] << 8));
        type = payload[2];
        payload += 3;
        size -= 3;
        rc = zcl_value_decode(type, payload, size, &value);
        /* R8 2.5.3.3 checks existence/type/permission BEFORE value validity.
         * Boolean is the sole supported scalar with rejected wire contents;
         * its primary-defined one-byte extent still permits the next record.
         */
        if (rc == ZCL_CODEC_INVALID_VALUE && type == ZCL_TYPE_BOOLEAN) {
            value.encoded_length = 1;
            rc = ZCL_CODEC_OK;
        }
        if (rc != ZCL_CODEC_OK)
            break;
        attr = set->attributes;
        i = set->count;
        while (i && attr->id != id) {
            attr++;
            i--;
        }
        error = ZCL_STATUS_UNSUPPORTED_ATTRIBUTE;
        if (i) {
            error = ZCL_STATUS_INVALID_DATA_TYPE;
            if (attr->value.type == type) {
                error = ZCL_STATUS_READ_ONLY;
                if (edit != NULL && edit->id == id) {
                    if (type != ZCL_TYPE_UINT16)
                        return ZCL_CODEC_INVALID_TABLE;
                    last = (uint16_t)((uint16_t)payload[0] | ((uint16_t)payload[1] << 8));
                    wrote = 1;
                    error = ZCL_STATUS_SUCCESS;
                }
            }
        }
        if (error != ZCL_STATUS_SUCCESS) {
            body[used++] = error;
            body[used++] = (uint8_t)id;
            body[used++] = (uint8_t)(id >> 8);
            errors++;
        }
        count++;
        payload += value.encoded_length;
        size -= value.encoded_length;
    }
    if (rc != ZCL_CODEC_OK && rc != ZCL_CODEC_TRUNCATED)
        return rc;
    if (command == ZCL_COMMAND_WRITE_NO_RESPONSE) {
        if (rc != ZCL_CODEC_OK)
            return rc;
        info->kind = ZCL_DISPATCH_SILENT;
    } else {
        reply = frame->header;
        reply.flags = (uint8_t)((reply.flags ^ ZCL_FLAG_SERVER_TO_CLIENT) | ZCL_FLAG_DISABLE_DEFAULT_RESPONSE);
        reply.command_id = ZCL_COMMAND_WRITE_RESPONSE;
        if (rc != ZCL_CODEC_OK) {
            /* Missing fields: whole command not carried out (Table 2-12). */
            wrote = 0;
            reply.command_id = ZCL_COMMAND_DEFAULT_RESPONSE;
            body[0] = command;
            body[1] = ZCL_STATUS_MALFORMED_COMMAND;
            used = 2;
            info->default_command = body[0];
            info->default_status = info->default_raw_status = body[1];
        } else {
            if (used && command == ZCL_COMMAND_WRITE_UNDIVIDED)
                wrote = 0;
            info->requested_count = count;
            info->returned_count = errors;
            if (!used) {
                body[used++] = ZCL_STATUS_SUCCESS;
                info->returned_count = 1;
            }
        }
        rc = zcl_frame_encode(&reply, body, used, response, capacity, &encoded);
        if (rc != ZCL_CODEC_OK)
            return rc;
        info->length = encoded;
        info->command_id = reply.command_id;
    }
    if (edit != NULL) {
        edit->written = wrote;
        if (wrote)
            edit->value = last;
    }
    return ZCL_CODEC_OK;
}
