/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "zcl_temperature.h"

#include <stddef.h>
#include <string.h>

#define CONFIGURE 0x06u
#define CONFIG_RESPONSE 0x07u
#define READ_CONFIG 0x08u
#define READ_RESPONSE 0x09u
#define REPORT 0x0au
#define NOT_FOUND 0x8bu
#define UNREPORTABLE 0x8cu

/* Only elapsed age needs staging; publish mutations after the last fallible step. */
static uint16_t next_age;

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static void put16(uint8_t *p, uint16_t n)
{
    p[0] = (uint8_t)n;
    p[1] = (uint8_t)(n >> 8);
}

static uint8_t valid_cfg(const zcl_temp_cfg_t *c)
{
    return c->maximum == 0xffffu
        || ((c->maximum == 0 || c->maximum >= c->minimum)
            && c->change != ZCL_TEMP_UNKNOWN);
}

static zcl_codec_result_t advance(zcl_temp_t * volatile ctx, uint32_t now)
{
    uint32_t elapsed = now - ctx->stamp;
    if (ctx->fault)
        return ZCL_CODEC_INVALID_VALUE;
    if (elapsed >= 0x80000000UL
            || (ctx->pending && now - ctx->pending_at > ZCL_TEMP_PENDING_LIMIT)) {
        ctx->fault = ZCL_TEMP_FAULT_TIME;
        return ZCL_CODEC_INVALID_VALUE;
    }
    elapsed += ctx->age;
    next_age = elapsed > 65535UL ? 65535u : (uint16_t)elapsed;
    return ZCL_CODEC_OK;
}

static void configure(zcl_temp_t * volatile ctx, const zcl_temp_cfg_t * volatile cfg)
{
    ctx->reporting = *cfg;
    ctx->configured = cfg->maximum != 0xffffu;
    if (!ctx->configured)
        ctx->reporting.change = 0;
    ctx->age = 0;
    ctx->baseline = ctx->value;
}

zcl_codec_result_t zcl_temp_init(zcl_temp_t * volatile ctx, uint32_t now,
                                int16_t minimum, int16_t maximum,
                                const zcl_temp_cfg_t * volatile defaults)
{
    if (ctx == NULL || defaults == NULL)
        return ZCL_CODEC_INVALID_ARGUMENT;
    if ((minimum != ZCL_TEMP_UNKNOWN && (minimum < -27315 || minimum > 32766))
            || (maximum != ZCL_TEMP_UNKNOWN && maximum < -27314)
            || (minimum != ZCL_TEMP_UNKNOWN && maximum != ZCL_TEMP_UNKNOWN && maximum <= minimum)
            || (defaults->minimum == 0xffffu && defaults->maximum == 0)
            || !valid_cfg(defaults))
        return ZCL_CODEC_INVALID_VALUE;
    memset(ctx, 0, sizeof(*ctx));
    ctx->minimum = minimum;
    ctx->maximum = maximum;
    ctx->value = ZCL_TEMP_UNKNOWN;
    ctx->stamp = now;
    ctx->defaults = *defaults;
    if (ctx->defaults.maximum == 0xffffu)
        ctx->defaults.change = 0;
    configure(ctx, &ctx->defaults);
    return ZCL_CODEC_OK;
}

zcl_codec_result_t zcl_temp_sample(zcl_temp_t * volatile ctx, volatile uint32_t now, int16_t value)
{
    zcl_codec_result_t rc;
    if (ctx == NULL)
        return ZCL_CODEC_INVALID_ARGUMENT;
    rc = advance(ctx, now);
    if (rc != ZCL_CODEC_OK)
        return rc;
    if (value != ZCL_TEMP_UNKNOWN
            && (value < -27315 || (ctx->minimum != ZCL_TEMP_UNKNOWN && value < ctx->minimum)
                || (ctx->maximum != ZCL_TEMP_UNKNOWN && value > ctx->maximum)))
        return ZCL_CODEC_INVALID_VALUE;
    ctx->value = value;
    ctx->stamp = now;
    ctx->age = next_age;
    return ZCL_CODEC_OK;
}

/* Configure Reporting carries a threshold only for analog types (R8 Table2-11),
 * not an attribute value. Known discrete/composite types have no such field.
 */
static uint8_t change_width(uint8_t type)
{
    if (type >= 0x20u && type <= 0x2fu)
        return (uint8_t)((type & 7u) + 1u);
    if (type >= 0x38u && type <= 0x3au)
        return (uint8_t)(2u << (type - 0x38u));
    if (type >= 0xe0u && type <= 0xe2u)
        return 4;
    if (zcl_value_type_supported(type) || type == 0x43u || type == 0x44u
            || type == 0x48u || type == 0x4cu || type == 0x50u || type == 0x51u
            || (type >= 0xe8u && type <= 0xeau) || type == 0xf0u || type == 0xf1u)
        return 0;
    return 0xff;
}

static uint8_t known(uint16_t id)
{
    return id <= 2u || id == 0xfffdu;
}

static zcl_codec_result_t reporting(zcl_temp_t * volatile next,
                                    const zcl_frame_info_t * volatile frame,
                                    const uint8_t * volatile p,
                                    uint8_t * volatile response, uint16_t capacity,
                                    zcl_dispatch_info_t * volatile info)
{
    uint8_t body[ZCL_FRAME_MAX_BODY - ZCL_FRAME_MIN_HEADER];
    zcl_temp_cfg_t cfg, accepted;
    zcl_header_t reply;
    zcl_codec_result_t rc;
    uint16_t id;
    uint8_t size = frame->payload_length, command = frame->header.command_id;
    uint8_t direction, type = 0, width, status, record, used = 0, count = 0, returned = 0;
    uint8_t malformed = size == 0, full = 0, budget, encoded, changed = 0;

    if (command == CONFIGURE && next->pending)
        return ZCL_CODEC_UNSUPPORTED_CONTEXT;
    budget = capacity < ZCL_FRAME_MAX_BODY ? (uint8_t)capacity : ZCL_FRAME_MAX_BODY;
    budget = budget >= 3u ? (uint8_t)(budget - 3u) : 0;
    while (size) {
        if (size < 3u || p[0] > 1u) {
            malformed = 1;
            break;
        }
        direction = p[0];
        id = get16(p + 1);
        p += 3;
        size -= 3;
        if (command == CONFIGURE) {
            width = direction ? 2u : 5u;
            if (size < width) {
                malformed = 1;
                break;
            }
            if (!direction) {
                type = p[0];
                width = change_width(type);
                if (width == 0xffu)
                    return ZCL_CODEC_UNSUPPORTED_DATA_TYPE;
                width += 5;
                if (size < width) {
                    malformed = 1;
                    break;
                }
                cfg.minimum = get16(p + 1);
                cfg.maximum = get16(p + 3);
                /* Preserve the signed representation without implementation-defined casts. */
                cfg.change = 0;
                if (type == ZCL_TYPE_INT16) {
                    uint16_t raw = get16(p + 5);
                    cfg.change = raw <= 32767u ? (int16_t)raw : (int16_t)(-1 - (int16_t)(0xffffu - raw));
                }
            }
            p += width;
            size -= width;
            status = UNREPORTABLE;
            if (!direction) {
                if (!known(id))
                    status = ZCL_STATUS_UNSUPPORTED_ATTRIBUTE;
                else if (id != 0 || type == 0x48u || type == 0x4cu || type == 0x50u || type == 0x51u)
                    status = UNREPORTABLE;
                else if (type != ZCL_TYPE_INT16)
                    status = ZCL_STATUS_INVALID_DATA_TYPE;
                else if (cfg.minimum == 0xffffu && cfg.maximum == 0) {
                    cfg = next->defaults;
                    status = ZCL_STATUS_SUCCESS;
                } else
                    status = valid_cfg(&cfg) ? ZCL_STATUS_SUCCESS : ZCL_STATUS_INVALID_VALUE;
            }
            if (status == ZCL_STATUS_SUCCESS) {
                /* Keep the last accepted record; publish only after full syntax/serialization. */
                accepted = cfg;
                changed = 1;
            } else {
                body[used++] = status;
                body[used++] = direction;
                put16(body + used, id);
                used += 2;
                returned++;
            }
        } else {
            status = ZCL_STATUS_UNSUPPORTED_ATTRIBUTE;
            if (!direction && known(id))
                status = id != 0 ? UNREPORTABLE : (next->configured ? ZCL_STATUS_SUCCESS : NOT_FOUND);
            record = status == ZCL_STATUS_SUCCESS ? 11u : 4u;
            if (record > budget - used)
                full = 1;
            if (!full) {
                body[used++] = status;
                body[used++] = direction;
                put16(body + used, id);
                used += 2;
                if (status == ZCL_STATUS_SUCCESS) {
                    body[used++] = ZCL_TYPE_INT16;
                    put16(body + used, next->reporting.minimum);
                    put16(body + used + 2, next->reporting.maximum);
                    put16(body + used + 4, (uint16_t)next->reporting.change);
                    used += 6;
                }
                returned++;
            }
        }
        count++;
    }
    memset(info, 0, sizeof(*info));
    info->sequence = frame->header.sequence;
    reply = frame->header;
    reply.flags = ZCL_FLAG_SERVER_TO_CLIENT | ZCL_FLAG_DISABLE_DEFAULT_RESPONSE;
    reply.command_id = command == CONFIGURE ? CONFIG_RESPONSE : READ_RESPONSE;
    if (malformed) {
        changed = 0;
        reply.command_id = ZCL_COMMAND_DEFAULT_RESPONSE;
        body[0] = command;
        body[1] = ZCL_STATUS_MALFORMED_COMMAND;
        used = 2;
        info->default_command = command;
        info->default_status = info->default_raw_status = ZCL_STATUS_MALFORMED_COMMAND;
    } else {
        if (command == CONFIGURE && used == 0) {
            body[used++] = ZCL_STATUS_SUCCESS;
            returned = 1;
        }
        if (!used)
            return ZCL_CODEC_BUFFER_TOO_SMALL;
        info->requested_count = count;
        info->returned_count = returned;
    }
    rc = zcl_frame_encode(&reply, body, used, response, capacity, &encoded);
    if (rc != ZCL_CODEC_OK)
        return rc;
    info->length = encoded;
    info->command_id = reply.command_id;
    if (changed) {
        configure(next, &accepted);
        next_age = 0;
    }
    return ZCL_CODEC_OK;
}

zcl_codec_result_t zcl_temp_rx(zcl_temp_t * volatile ctx, uint32_t now,
                              const uint8_t * volatile request, uint16_t length,
                              uint8_t * volatile response, uint16_t capacity,
                              zcl_dispatch_info_t * volatile info)
{
    zcl_frame_info_t frame;
    zcl_dispatch_info_t candidate;
    zcl_attribute_set_t set;
    zcl_attribute_t attributes[4];
    zcl_attribute_t * volatile attr = attributes;
    uint8_t values[8], i;
    zcl_codec_result_t rc;
    if (ctx == NULL || request == NULL || response == NULL || info == NULL)
        return ZCL_CODEC_INVALID_ARGUMENT;
    rc = advance(ctx, now);
    if (rc != ZCL_CODEC_OK)
        return rc;
    rc = zcl_frame_decode(request, length, &frame);
    if (rc != ZCL_CODEC_OK)
        return rc;
    if (frame.header.flags & ZCL_FLAG_SERVER_TO_CLIENT)
        return ZCL_CODEC_UNSUPPORTED_CONTEXT;
    if (frame.header.type == ZCL_FRAME_GLOBAL
            && !(frame.header.flags & ZCL_FLAG_MANUFACTURER_SPECIFIC)
            && (frame.header.command_id == CONFIGURE || frame.header.command_id == READ_CONFIG)) {
        rc = reporting(ctx, &frame, request + frame.payload_offset, response, capacity, &candidate);
    } else {
        memset(&set, 0, sizeof(set));
        memset(attributes, 0, sizeof(attributes));
        put16(values, (uint16_t)ctx->value);
        put16(values + 2, (uint16_t)ctx->minimum);
        put16(values + 4, (uint16_t)ctx->maximum);
        put16(values + 6, 3);
        set.attributes = attributes;
        set.count = 4;
        for (i = 0; i < 4; i++) {
            attr->id = i == 3 ? 0xfffdu : i;
            attr->readable = 1;
            attr->value.type = i == 3 ? ZCL_TYPE_UINT16 : ZCL_TYPE_INT16;
            attr->value.data = values + 2u * i;
            attr->value.data_length = 2;
            attr++;
        }
        rc = zcl_dispatch_unicast(&set, request, length, response, capacity, &candidate);
    }
    if (rc == ZCL_CODEC_OK) {
        ctx->stamp = now;
        ctx->age = next_age;
        *info = candidate;
    }
    return rc;
}

static uint8_t due(const zcl_temp_t *ctx)
{
    int32_t delta, threshold;
    if (!ctx->configured || next_age < ctx->reporting.minimum)
        return 0;
    if (ctx->reporting.maximum && next_age >= ctx->reporting.maximum)
        return 1;
    if (ctx->value == ctx->baseline)
        return 0;
    /* NaS is not a numeric temperature; transitions are an explicit lab policy. */
    if (ctx->value == ZCL_TEMP_UNKNOWN || ctx->baseline == ZCL_TEMP_UNKNOWN)
        return 1;
    delta = (int32_t)ctx->value - ctx->baseline;
    threshold = ctx->reporting.change;
    if (delta < 0) delta = -delta;
    if (threshold < 0) threshold = -threshold;
    return delta >= threshold;
}

zcl_codec_result_t zcl_temp_prepare(zcl_temp_t * volatile ctx, uint32_t now,
                                   uint8_t sequence, uint8_t routes,
                                   uint8_t * volatile response, uint16_t capacity,
                                   zcl_temp_report_t * volatile report)
{
    zcl_temp_report_t candidate;
    zcl_header_t header;
    zcl_value_t value;
    zcl_codec_result_t rc;
    uint8_t bytes[2], payload[5], encoded;
    if (ctx == NULL || response == NULL || report == NULL)
        return ZCL_CODEC_INVALID_ARGUMENT;
    if (routes > 1u)
        return ZCL_CODEC_INVALID_VALUE;
    rc = advance(ctx, now);
    if (rc != ZCL_CODEC_OK)
        return rc;
    if (ctx->pending)
        return ZCL_CODEC_UNSUPPORTED_CONTEXT;
    memset(&candidate, 0, sizeof(candidate));
    if (routes && due(ctx)) {
        if (ctx->serial == 0xffffu) {
            ctx->fault = ZCL_TEMP_FAULT_TOKEN;
            return ZCL_CODEC_INVALID_VALUE;
        }
        memset(&value, 0, sizeof(value));
        put16(bytes, (uint16_t)ctx->value);
        value.type = ZCL_TYPE_INT16;
        value.data = bytes;
        value.data_length = 2;
        payload[0] = payload[1] = 0;
        payload[2] = ZCL_TYPE_INT16;
        rc = zcl_value_encode(&value, payload + 3, 2, &encoded);
        if (rc != ZCL_CODEC_OK)
            return rc;
        memset(&header, 0, sizeof(header));
        header.flags = ZCL_FLAG_SERVER_TO_CLIENT | ZCL_FLAG_DISABLE_DEFAULT_RESPONSE;
        header.sequence = sequence;
        header.command_id = REPORT;
        rc = zcl_frame_encode(&header, payload, 5, response, capacity, &encoded);
        if (rc != ZCL_CODEC_OK)
            return rc;
        candidate.ready = 1;
        candidate.length = encoded;
        candidate.token = ++ctx->serial;
        ctx->pending = 1;
        ctx->pending_at = now;
        ctx->pending_value = ctx->value;
    }
    ctx->stamp = now;
    ctx->age = next_age;
    *report = candidate;
    return ZCL_CODEC_OK;
}

zcl_codec_result_t zcl_temp_finish(zcl_temp_t * volatile ctx, volatile uint32_t now,
                                  uint16_t token, uint8_t sent, volatile uint32_t issued_at)
{
    zcl_codec_result_t rc;
    if (ctx == NULL)
        return ZCL_CODEC_INVALID_ARGUMENT;
    if (sent > 1u || (!sent && issued_at != 0) || !ctx->pending || token != ctx->serial)
        return ZCL_CODEC_INVALID_VALUE;
    rc = advance(ctx, now);
    if (rc != ZCL_CODEC_OK)
        return rc;
    if (sent) {
        if (issued_at - ctx->pending_at > now - ctx->pending_at)
            return ZCL_CODEC_INVALID_VALUE;
        ctx->baseline = ctx->pending_value;
        next_age = (uint16_t)(now - issued_at);
    }
    ctx->pending = 0;
    ctx->pending_at = 0;
    ctx->pending_value = 0;
    ctx->stamp = now;
    ctx->age = next_age;
    return ZCL_CODEC_OK;
}
