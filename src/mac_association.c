/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "mac_association.h"

#include <stddef.h>
#include <string.h>
#include "mac_link_workspace_guard_internal.h"
#if defined(CC2530_MAC_LINK_WORKSPACE)
#include "mac_link_workspace_internal.h"
#endif

static uint8_t valid(const mac_association_t MAC_ASSOCIATION_RAM *ctx)
{
    return ctx != NULL && ctx->version == MAC_ASSOCIATION_VERSION
        && ctx->phase <= MAC_ASSOCIATION_DONE;
}

mac_association_result_t mac_association_init(mac_association_t MAC_ASSOCIATION_RAM *ctx,
                                               uint32_t now)
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    if (!LW_IO(LW_NONE, ctx, sizeof(*ctx), 1)) return MAC_ASSOCIATION_INVALID;
#endif
    if (ctx == NULL)
        return MAC_ASSOCIATION_INVALID;
    memset(ctx, 0, sizeof(*ctx));
    ctx->version = MAC_ASSOCIATION_VERSION;
    ctx->last = now;
    return MAC_ASSOCIATION_OK;
}

mac_association_result_t mac_association_start(mac_association_t MAC_ASSOCIATION_RAM *ctx,
                    const mac_association_request_t MAC_ASSOCIATION_RAM *request,
                    uint32_t now)
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    if (!LW_IO(LW_ASSOCIATION, ctx, sizeof(*ctx), 1) ||
        !LW_IO(LW_ASSOCIATION, request, sizeof(*request), 0)) return MAC_ASSOCIATION_INVALID;
#endif
    uint8_t i;
    if (!valid(ctx) || request == NULL || request->epoch == 0
            || request->lifetime == 0 || request->lifetime > MAC_ASSOCIATION_MAX_LIFETIME
            || request->work_limit == 0 || request->work_limit > MAC_ASSOCIATION_MAX_WORK
            || request->pan_id == 0xffffu || request->channel < 11 || request->channel > 26
            || (request->coordinator_mode != MAC_ADDRESS_SHORT
                && request->coordinator_mode != MAC_ADDRESS_EXTENDED))
        return MAC_ASSOCIATION_INVALID;
    if (request->coordinator_mode == MAC_ADDRESS_SHORT) {
        if (request->coordinator[1] == 0xff && request->coordinator[0] >= 0xfe)
            return MAC_ASSOCIATION_INVALID;
        for (i = 2; i < 8; i++)
            if (request->coordinator[i] != 0)
                return MAC_ASSOCIATION_INVALID;
    }
    if (ctx->phase != MAC_ASSOCIATION_IDLE)
        return MAC_ASSOCIATION_STATE;
    if ((uint32_t)(now - ctx->last) >= MAC_ASSOCIATION_HALF)
        return MAC_ASSOCIATION_INVALID;
    if (ctx->generation == UINT32_MAX)
        return MAC_ASSOCIATION_GENERATION_LIMIT;
    ctx->request = *request;
    memset(&ctx->record, 0, sizeof(ctx->record));
    ctx->generation++;
    ctx->record.generation = ctx->generation;
    ctx->record.epoch = request->epoch;
    ctx->opened = now;
    ctx->last = now;
    ctx->remaining = request->work_limit;
    ctx->phase = MAC_ASSOCIATION_WAIT;
    return MAC_ASSOCIATION_OK;
}

mac_association_result_t mac_association_step_rx(mac_association_t MAC_ASSOCIATION_RAM *ctx,
                    uint32_t now,
                    const mac_association_event_t MAC_ASSOCIATION_RAM *event,
                    uint8_t MAC_ASSOCIATION_RAM *observation, uint8_t volatile profile)
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    LW_ENTER(LW_ASSOCIATION, MAC_ASSOCIATION_INVALID);
    if (!LW_IO(LW_ASSOCIATION, ctx, sizeof(*ctx), 1) ||
        !LW_IO(LW_ASSOCIATION, event, event ? sizeof(*event) : 0, 0) ||
        !LW_IO(LW_ASSOCIATION, observation, 1, 1))
        return LW_RETURN(LW_ASSOCIATION, MAC_ASSOCIATION_INVALID);
#endif
#if defined(CC2530_MAC_LINK_WORKSPACE)
#define frame (link_work_arena.protocol.operation.association)
#else
    mac_frame_info_t frame;
#endif
    mac_command_t command;
    uint8_t decision = MAC_ASSOCIATION_WAITING;

    if (!valid(ctx) || observation == NULL || profile > MAC_RX_R22_ASSOCIATION_RESPONSE)
        return LW_RETURN(LW_ASSOCIATION, MAC_ASSOCIATION_INVALID);
    if (ctx->phase != MAC_ASSOCIATION_WAIT)
        return LW_RETURN(LW_ASSOCIATION, MAC_ASSOCIATION_STATE);
    if ((uint32_t)(now - ctx->last) >= MAC_ASSOCIATION_HALF
            || (event != NULL
                && (event->kind != MAC_ASSOCIATION_FRAME && event->kind != MAC_ASSOCIATION_CANCEL))
            || (event != NULL && event->kind == MAC_ASSOCIATION_FRAME
                && (event->body == NULL || event->crc_valid > 1
                    || event->channel < 11 || event->channel > 26)))
        return LW_RETURN(LW_ASSOCIATION, MAC_ASSOCIATION_INVALID);

    if ((uint32_t)(now - ctx->opened) >= ctx->request.lifetime)
        decision = MAC_ASSOCIATION_EXPIRED;
    else if (ctx->remaining == 0)
        decision = MAC_ASSOCIATION_EXHAUSTED;
    else {
        ctx->remaining--;
        if (event != NULL) {
            if (event->epoch != ctx->request.epoch || event->generation != ctx->generation)
                decision = MAC_ASSOCIATION_STALE;
            else if (event->kind == MAC_ASSOCIATION_CANCEL)
                decision = MAC_ASSOCIATION_CANCELLED;
            else if ((uint32_t)(event->stamp - ctx->last) >= MAC_ASSOCIATION_HALF
                    || (uint32_t)(now - event->stamp) >= MAC_ASSOCIATION_HALF)
                decision = MAC_ASSOCIATION_STALE;
            else if (event->channel != ctx->request.channel)
                decision = MAC_ASSOCIATION_MISMATCH;
            else if (!event->crc_valid)
                decision = MAC_ASSOCIATION_BAD_CRC;
            else if (mac_frame_decode_profile(event->body, event->length, &frame, profile)
                     != MAC_CODEC_OK)
                decision = MAC_ASSOCIATION_MALFORMED;
            else if (frame.header.type != MAC_FRAME_COMMAND)
                decision = MAC_ASSOCIATION_MISMATCH;
            else if (mac_command_decode(event->body + frame.payload_offset,
                                        frame.payload_length, &command) != MAC_CODEC_OK)
                decision = MAC_ASSOCIATION_MALFORMED;
            else if (command.identifier != MAC_COMMAND_ASSOCIATION_RESPONSE
                    || frame.header.source_pan != ctx->request.pan_id
                    || (frame.header.destination_pan != ctx->request.pan_id
                        && (profile != MAC_RX_R22_ASSOCIATION_RESPONSE
                            || frame.header.destination_pan != 0xffffu))
                    || memcmp(frame.header.destination, ctx->request.local, 8)
                    || (ctx->request.coordinator_mode == MAC_ADDRESS_EXTENDED
                        && memcmp(frame.header.source, ctx->request.coordinator, 8)))
                decision = MAC_ASSOCIATION_MISMATCH;
            else {
                /* Both profiles checked ext/ext, AR and exact command bytes.
                 * A selected PAN must actually be present; R22 D.3 may put it
                 * only in the uncompressed source. IEEE2006 7.5.3.1 permits
                 * learning an unknown IEEE, not verifying its short binding.
                 */
                ctx->record.stamp = event->stamp;
                ctx->record.short_address = command.short_address;
                ctx->record.status = command.status;
                ctx->record.sequence = frame.header.sequence;
                memcpy(ctx->record.source_ieee, frame.header.source, 8);
                ctx->record.source_relation = ctx->request.coordinator_mode == MAC_ADDRESS_EXTENDED
                    ? MAC_ASSOCIATION_SOURCE_MATCHED : MAC_ASSOCIATION_SOURCE_UNBOUND;
                ctx->record.address_kind = command.status != MAC_ASSOCIATION_SUCCESS
                    ? MAC_ASSOCIATION_REFUSED : command.short_address == 0xfffeu
                    ? MAC_ASSOCIATION_EXTENDED_ONLY : MAC_ASSOCIATION_ALLOCATED;
                decision = MAC_ASSOCIATION_RESPONSE;
            }
        }
        if (decision != MAC_ASSOCIATION_RESPONSE && decision != MAC_ASSOCIATION_CANCELLED
                && ctx->remaining == 0)
            decision = MAC_ASSOCIATION_EXHAUSTED;
    }
    ctx->last = now;
    if (decision == MAC_ASSOCIATION_RESPONSE || decision >= MAC_ASSOCIATION_EXPIRED) {
        ctx->phase = MAC_ASSOCIATION_DONE;
        ctx->record.outcome = decision;
        if (decision != MAC_ASSOCIATION_RESPONSE)
            ctx->record.stamp = now;
    }
    *observation = decision;
    return LW_RETURN(LW_ASSOCIATION, MAC_ASSOCIATION_OK);
}
#undef frame

mac_association_result_t mac_association_step(mac_association_t MAC_ASSOCIATION_RAM * volatile ctx,
                    uint32_t now,
                    const mac_association_event_t MAC_ASSOCIATION_RAM *event,
                    uint8_t MAC_ASSOCIATION_RAM *observation)
{
    return mac_association_step_rx(ctx, now, event, observation, MAC_RX_IEEE2006);
}

mac_association_result_t mac_association_take(mac_association_t MAC_ASSOCIATION_RAM *ctx,
                    mac_association_record_t MAC_ASSOCIATION_RAM *record)
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    if (!LW_IO(LW_NONE, ctx, sizeof(*ctx), 1) ||
        !LW_IO(LW_NONE, record, sizeof(*record), 1)) return MAC_ASSOCIATION_INVALID;
#endif
    if (!valid(ctx) || record == NULL)
        return MAC_ASSOCIATION_INVALID;
    if (ctx->phase != MAC_ASSOCIATION_DONE)
        return MAC_ASSOCIATION_STATE;
    *record = ctx->record;
    ctx->phase = MAC_ASSOCIATION_IDLE;
    return MAC_ASSOCIATION_OK;
}
