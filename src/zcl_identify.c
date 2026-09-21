/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "zcl_identify.h"
#include "zcl_write.h"

#include <stddef.h>
#include <string.h>

static zcl_codec_result_t advance(const zcl_id_t * volatile ctx, uint32_t now,
                                  zcl_id_t * volatile next)
{
    uint32_t elapsed, seconds;
    elapsed = now - ctx->stamp;
    if (elapsed >= 0x80000000UL || ctx->phase >= 1000u
            || (ctx->remaining == 0u && ctx->phase != 0u))
        return ZCL_CODEC_INVALID_VALUE;
    *next = *ctx;
    next->stamp = now;
    if (next->remaining != 0u) {
        /* At most 0x7fffffff + 999: no 32-bit overflow. */
        elapsed += next->phase;
        seconds = elapsed / 1000UL;
        if (seconds >= next->remaining) {
            next->remaining = 0;
            next->phase = 0;
        } else {
            next->remaining -= (uint16_t)seconds;
            next->phase = (uint16_t)(elapsed - seconds * 1000UL);
        }
    }
    return ZCL_CODEC_OK;
}

zcl_codec_result_t zcl_id_init(zcl_id_t *ctx, uint32_t now)
{
    if (ctx == NULL)
        return ZCL_CODEC_INVALID_ARGUMENT;
    memset(ctx, 0, sizeof(*ctx));
    ctx->stamp = now;
    return ZCL_CODEC_OK;
}

zcl_codec_result_t zcl_id_tick(zcl_id_t *ctx, uint32_t now)
{
    zcl_id_t next;
    zcl_codec_result_t rc;
    if (ctx == NULL)
        return ZCL_CODEC_INVALID_ARGUMENT;
    rc = advance(ctx, now, &next);
    if (rc == ZCL_CODEC_OK)
        *ctx = next;
    return rc;
}

/* Only a two-attribute transient view, not a copied frame or endpoint registry.
 * The existing read/dispatch path owns all global command/status semantics.
 */
static zcl_codec_result_t global(zcl_id_t * volatile next,
                                 const uint8_t * volatile request, const zcl_frame_info_t * volatile frame,
                                 uint8_t * volatile response, uint16_t capacity,
                                 zcl_dispatch_info_t * volatile info)
{
    zcl_attribute_set_t set;
    zcl_attribute_t attributes[2];
    zcl_attribute_t * volatile attribute = attributes;
    zcl_wr_t edit;
    zcl_codec_result_t rc;
    uint8_t values[4], i;
    memset(&set, 0, sizeof(set));
    memset(attributes, 0, sizeof(attributes));
    values[0] = (uint8_t)next->remaining;
    values[1] = (uint8_t)(next->remaining >> 8);
    values[2] = 2;
    values[3] = 0;
    set.attributes = attributes;
    set.count = 2;
    attributes[1].id = 0xfffd;
    for (i = 0; i < 2; i++) {
        attribute->readable = 1;
        attribute->value.type = ZCL_TYPE_UINT16;
        attribute->value.data = values + 2u * i;
        attribute->value.data_length = 2;
        attribute++;
    }
    if (frame->header.type == ZCL_FRAME_GLOBAL
            && !(frame->header.flags & ZCL_FLAG_MANUFACTURER_SPECIFIC)
            && (frame->header.command_id == ZCL_COMMAND_WRITE_ATTRIBUTES
                || frame->header.command_id == ZCL_COMMAND_WRITE_UNDIVIDED
                || frame->header.command_id == ZCL_COMMAND_WRITE_NO_RESPONSE)) {
        edit.id = 0; /* The one writable, full-range uint16 is IdentifyTime. */
        memset(info, 0, sizeof(*info));
        rc = zcl_wr_handle(&set, frame, request + frame->payload_offset,
                           response, capacity, info, &edit);
        if (rc == ZCL_CODEC_OK && edit.written) {
            next->remaining = edit.value;
            next->phase = 0; /* Same explicit logical-time policy as Identify. */
        }
        return rc;
    }
    return zcl_dispatch_unicast(&set, request, (uint16_t)(frame->payload_offset + frame->payload_length),
                                 response, capacity, info);
}

zcl_codec_result_t zcl_id_rx(zcl_id_t * volatile ctx, uint32_t now,
                            const uint8_t * volatile request, uint16_t length,
                            uint8_t * volatile response, uint16_t capacity,
                            zcl_dispatch_info_t * volatile info)
{
    zcl_id_t next;
    zcl_frame_info_t frame;
    zcl_dispatch_info_t candidate;
    zcl_header_t reply;
    zcl_codec_result_t rc;
    uint8_t payload[2];
    const uint8_t * volatile input;
    if (ctx == NULL || request == NULL || response == NULL || info == NULL)
        return ZCL_CODEC_INVALID_ARGUMENT;
    rc = advance(ctx, now, &next);
    if (rc != ZCL_CODEC_OK)
        return rc;
    rc = zcl_frame_decode(request, length, &frame);
    if (rc != ZCL_CODEC_OK)
        return rc;
    if (frame.header.flags & ZCL_FLAG_SERVER_TO_CLIENT)
        return ZCL_CODEC_UNSUPPORTED_CONTEXT;
    if (frame.header.type != ZCL_FRAME_CLUSTER_SPECIFIC
            || (frame.header.flags & ZCL_FLAG_MANUFACTURER_SPECIFIC)
            || frame.header.command_id > 1u) {
        rc = global(&next, request, &frame, response, capacity, &candidate);
        if (rc != ZCL_CODEC_OK)
            return rc;
    } else {
        memset(&candidate, 0, sizeof(candidate));
        candidate.sequence = frame.header.sequence;
        reply = frame.header;
        reply.flags = ZCL_FLAG_SERVER_TO_CLIENT | ZCL_FLAG_DISABLE_DEFAULT_RESPONSE;
        reply.type = ZCL_FRAME_GLOBAL;
        reply.command_id = ZCL_COMMAND_DEFAULT_RESPONSE;
        payload[0] = frame.header.command_id;
        payload[1] = ZCL_STATUS_SUCCESS;
        if (frame.header.command_id == 0u) {
            if (frame.payload_length < 2u) {
                payload[1] = ZCL_STATUS_MALFORMED_COMMAND;
            } else {
                input = request + frame.payload_offset;
                next.remaining = (uint16_t)((uint16_t)input[0] | ((uint16_t)input[1] << 8));
                next.phase = 0;
                if (frame.header.flags & ZCL_FLAG_DISABLE_DEFAULT_RESPONSE)
                    candidate.kind = ZCL_ID_SILENT;
            }
        } else if (next.remaining == 0u) {
            /* 3.5.2.3.2.1 overrides the general default-response rule. */
            candidate.kind = ZCL_ID_SILENT;
        } else {
            reply.type = ZCL_FRAME_CLUSTER_SPECIFIC;
            reply.command_id = 0;
            payload[0] = (uint8_t)next.remaining;
            payload[1] = (uint8_t)(next.remaining >> 8);
        }
        if (candidate.kind != ZCL_ID_SILENT) {
            rc = zcl_frame_encode(&reply, payload, 2, response, capacity, &candidate.length);
            if (rc != ZCL_CODEC_OK)
                return rc;
            candidate.command_id = reply.command_id;
            if (reply.type == ZCL_FRAME_GLOBAL) {
                candidate.default_command = payload[0];
                candidate.default_status = candidate.default_raw_status = payload[1];
            }
        }
    }
    /* No fallible operation after response serialization. */
    *ctx = next;
    *info = candidate;
    return ZCL_CODEC_OK;
}
