/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "nwk_aps_internal.h"
#include <stddef.h>
#include <string.h>

MCU_XDATA security_keys_status_t nwk_aps_keys;
/* The active-MAC step returns before transmit construction. Receive parsing
 * is a separate foreground call; no lower service retains these views. */
#if !defined(CC2530_MAC_LINK_WORKSPACE)
MCU_XDATA nwk_aps_work_t nwk_aps_work;
#endif

static uint8_t reached(uint32_t now, uint32_t until)
{
    return (uint32_t)(now-until) < MAC_TX_HALF;
}

static nwk_aps_result_t advance(nwk_aps_t * volatile ctx, volatile uint32_t now)
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    if (!LW_IO(LW_NONE, ctx, sizeof(*ctx), 1)) return NWK_APS_ARGUMENT;
#endif
    if (!ctx || ctx->version != NWK_APS_VERSION || !ctx->owner) return NWK_APS_ARGUMENT;
    if ((uint32_t)(now-ctx->last) >= MAC_TX_HALF) return NWK_APS_CLOCK;
    ctx->last = now;
    return NWK_APS_OK;
}

nwk_aps_result_t nwk_aps_init(nwk_aps_t * volatile ctx, NWK_APS_TX_T * volatile owner,
    const nwk_aps_config_t * volatile config, volatile uint32_t now)
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    LW_ROOT(NWK_APS_STATE);
    if (!LW_IO(LW_NONE, ctx, sizeof(*ctx), 1) || !LW_IO(LW_NONE, owner, sizeof(*owner), 1) ||
        !LW_IO(LW_NONE, config, sizeof(*config), 0)) return NWK_APS_ARGUMENT;
#endif
    if (!ctx || !owner || !config || !config->endpoint || config->endpoint == 255 || !config->profile ||
        !config->limits.block_timeout || !config->limits.block_polls || !config->nv_polls ||
        config->ack_wait < 93750UL || config->ack_wait >= MAC_TX_HALF/8u ||
        !config->broadcast_time || config->broadcast_time >= MAC_TX_HALF)
        return NWK_APS_ARGUMENT;
    if (NWK_APS_ENGINE(owner)->phase != MAC_TX_IDLE) return NWK_APS_STATE;
    memset(ctx, 0, sizeof(*ctx));
    ctx->owner = owner; ctx->endpoint = config->endpoint; ctx->profile = config->profile;
    ctx->limits = config->limits; ctx->nv_polls = config->nv_polls; ctx->last = now;
    ctx->ack_wait = config->ack_wait;
    ctx->broadcast_time = config->broadcast_time;
    ctx->duplicate_time = (config->ack_wait+NWK_APS_TX_LIFETIME+MAC_TX_STOP_SYMBOLS)*4u;
    if (ctx->duplicate_time < NWK_APS_DUPLICATE_TIME) ctx->duplicate_time = NWK_APS_DUPLICATE_TIME;
    ctx->version = NWK_APS_VERSION;
    return NWK_APS_OK;
}

static nwk_aps_result_t reserve(nwk_aps_t * volatile ctx, volatile uint32_t now, volatile uint8_t aps)
{
    nwk_aps_result_t result = advance(ctx, now);
    if (result) return result;
    if (ctx->queued || ctx->completed || ctx->stopping) return NWK_APS_FULL;
    if (aps) {
        if (ctx->wrap_wait && !reached(now, ctx->counter_until)) return NWK_APS_EXHAUSTED;
        ctx->wrap_wait = 0;
    }
    return NWK_APS_OK;
}

static void allocated(nwk_aps_t * volatile ctx, volatile uint32_t now, volatile uint8_t aps)
{
    if (aps) {
        ctx->outgoing.aps.counter = ctx->next_aps++;
        if (!ctx->next_aps) { ctx->wrap_wait = 1; ctx->counter_until = now+ctx->duplicate_time; }
    }
    ctx->queued = 1; ctx->retries = ctx->seen_ack = ctx->sent = ctx->quiet = 0;
    ctx->transaction_until = now+ctx->duplicate_time;
}

nwk_aps_result_t nwk_aps_queue(nwk_aps_t * volatile ctx, const ed_packet_t * volatile packet,
                               volatile uint8_t aps_secure, volatile uint32_t now)
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    LW_ROOT(NWK_APS_STATE);
    if (!LW_IO(LW_NONE, packet, sizeof(*packet), 0)) return NWK_APS_ARGUMENT;
#endif
    nwk_aps_result_t result;
    if (!packet || aps_secure > 1 || packet->length > ED_PAYLOAD_MAX) return NWK_APS_ARGUMENT;
    result = reserve(ctx, now, !packet->nwk.type);
    if (result) return result;
    /* ZDO owns source endpoint0/profile0 even when replying to another endpoint. */
    if (!packet->nwk.type && packet->aps.type == APS_FRAME_DATA &&
        (packet->aps.source_endpoint || packet->aps.destination_endpoint || packet->aps.profile_id) &&
        (!ctx->ready || ((packet->aps.source_endpoint || packet->aps.profile_id) &&
         (packet->aps.source_endpoint != ctx->endpoint || packet->aps.profile_id != ctx->profile))))
        return NWK_APS_STATE;
    if (!packet->nwk.type && packet->aps.type != APS_FRAME_DATA) return NWK_APS_ARGUMENT;
    ctx->outgoing = *packet; ctx->secure = aps_secure; ctx->special = 0;
    allocated(ctx, now, !packet->nwk.type);
    return NWK_APS_OK;
}

nwk_aps_result_t nwk_aps_key_exchange(nwk_aps_t * volatile ctx, volatile uint8_t which, volatile uint32_t now)
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    LW_ROOT(NWK_APS_STATE);
#endif
    nwk_aps_result_t result;
    if (which < 1 || which > 3) return NWK_APS_ARGUMENT;
    result = reserve(ctx, now, which != 3);
    if (result) return result;
    memset(&ctx->outgoing, 0, sizeof(ctx->outgoing));
    ctx->outgoing.aps.type = ED_APS_COMMAND;
    ctx->special = which; ctx->secure = which == 1;
    allocated(ctx, now, which != 3);
    return NWK_APS_OK;
}

void nwk_aps_complete(nwk_aps_t * volatile ctx, volatile uint8_t result)
{
    ed_packet_t * volatile p = &ctx->outgoing;
    ctx->queued = ctx->waiting = 0; ctx->completed = 1; ctx->result = result;
    ctx->cancel = ctx->cancel_sent = 0;
    if (result || ctx->special || p->nwk.type || p->aps.type || p->aps.profile_id ||
        p->aps.source_endpoint || p->aps.destination_endpoint || p->aps.delivery_mode != 2)
        return;
    if (security_keys_status(&nwk_aps_keys) != SECURITY_KEYS_OK) return;
    if (p->aps.cluster_id == 0x0013u && p->length == 12 && p->nwk.destination == 0xfffdu &&
        p->payload[1] == (uint8_t)nwk_aps_keys.config.address &&
        p->payload[2] == (uint8_t)(nwk_aps_keys.config.address >> 8) &&
        !memcmp(p->payload+3, nwk_aps_keys.config.own_ieee, 8) &&
        (p->payload[11] == 0x88 || p->payload[11] == 0x8c))
        ctx->announced = 1;
    if (p->aps.cluster_id == 0x0036u && p->length == 3 && p->payload[1] >= 180 &&
        p->payload[2] == 1 && p->nwk.destination == 0xfffcu && ctx->announced &&
        nwk_aps_keys.phase == SECURITY_KEYS_VERIFIED) {
        ctx->permit_sent = ctx->ready = 1;
    }
}

nwk_aps_result_t nwk_aps_broadcast_slot(nwk_aps_t * volatile ctx, volatile uint16_t source, volatile uint8_t sequence,
                                      volatile uint32_t now, uint8_t * volatile slot)
{
    uint8_t i;
    *slot = NWK_APS_DUPLICATES;
    for (i = 0; i < NWK_APS_DUPLICATES; i++) {
        nwk_aps_duplicate_t * volatile entry = &ctx->broadcast[i];
        if (!entry->used || reached(now, entry->until)) *slot = i;
        else if (entry->source == source && entry->counter == sequence) return NWK_APS_DUPLICATE;
    }
    return *slot == NWK_APS_DUPLICATES ? NWK_APS_FULL : NWK_APS_OK;
}

void nwk_aps_broadcast_put(nwk_aps_t * volatile ctx, volatile uint8_t slot, volatile uint16_t source,
    volatile uint8_t sequence, volatile uint32_t now)
{
    nwk_aps_duplicate_t * volatile entry = &ctx->broadcast[slot];
    entry->used = 1; entry->source = source; entry->counter = sequence;
    entry->until = now+ctx->broadcast_time;
}

#if defined(CC2530_MAC_LINK)
static void arm(nwk_aps_t *ctx, NWK_APS_ACTION_T *action)
{
    ctx->armed = 0;
    ctx->disarmed = 0;
    ctx->arm_issued = 1;
    ctx->arm_retry = ctx->owner->engine.retries;
    ctx->arm_nb = ctx->owner->engine.nb;
    action->control.kind = NWK_APS_ACTION_ARM;
    action->control.generation = ctx->owner->engine.generation;
    action->control.retry = ctx->arm_retry;
    action->control.nb = ctx->arm_nb;
    action->control.until = ctx->owner->engine.deadline;
}

nwk_aps_result_t nwk_aps_armed(nwk_aps_t * volatile ctx, uint32_t generation,
    uint8_t retry, uint8_t nb)
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    if (!LW_IO(LW_NONE, ctx, sizeof(*ctx), 1)) return NWK_APS_ARGUMENT;
#endif
    if (!ctx || ctx->version != NWK_APS_VERSION) return NWK_APS_ARGUMENT;
    if (!ctx->active || ctx->arm_issued != 1 || ctx->armed ||
        ctx->owner->engine.phase != MAC_TX_DRAW ||
        generation != ctx->owner->engine.generation ||
        retry != ctx->arm_retry || nb != ctx->arm_nb)
        return NWK_APS_STATE;
    ctx->armed = 1;
    ctx->arm_issued = 0;
    return NWK_APS_OK;
}

nwk_aps_result_t nwk_aps_disarmed(nwk_aps_t * volatile ctx, uint32_t generation,
    uint8_t retry, uint8_t nb)
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    if (!LW_IO(LW_NONE, ctx, sizeof(*ctx), 1)) return NWK_APS_ARGUMENT;
#endif
    if (!ctx || ctx->version != NWK_APS_VERSION) return NWK_APS_ARGUMENT;
    if (!ctx->active || ctx->arm_issued != 2 || ctx->disarmed ||
        ctx->owner->engine.phase != MAC_TX_DONE ||
        generation != ctx->owner->engine.generation ||
        retry != ctx->owner->engine.retries || nb != ctx->owner->engine.nb)
        return NWK_APS_STATE;
    ctx->disarmed = 1;
    ctx->arm_issued = 0;
    return NWK_APS_OK;
}
#endif

nwk_aps_result_t nwk_aps_step(nwk_aps_t * volatile ctx, volatile uint32_t now,
    const NWK_APS_EVENT_T * volatile event, NWK_APS_ACTION_T * volatile action)
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    LW_ROOT(NWK_APS_STATE);
    if (!LW_IO(LW_NONE, action, sizeof(*action), 1) ||
        !LW_IO(LW_NONE, event, event ? sizeof(*event) : 0, 0)) return NWK_APS_ARGUMENT;
#endif
    nwk_aps_result_t result;
    uint8_t outcome;
    if (!action) return NWK_APS_ARGUMENT;
    result = advance(ctx, now);
    if (result) return result;
    memset(action, 0, sizeof(*action));
    if (ctx->queued && !ctx->cancel && reached(now, ctx->transaction_until)) {
        ctx->cancel = 1; ctx->cancel_result = NWK_APS_TIMEOUT;
    }
    if (ctx->active) {
#if defined(CC2530_MAC_LINK)
        if (!ctx->armed && ctx->owner->engine.phase == MAC_TX_DRAW &&
            !ctx->stopping && !(ctx->cancel && !ctx->active_ack) &&
            !reached(now, ctx->owner->engine.deadline)) {
            if (!ctx->arm_issued) arm(ctx, action);
            return event ? NWK_APS_IGNORED : NWK_APS_OK;
        }
#endif
#if defined(CC2530_MAC_LINK_WORKSPACE)
        LW_ENTER(LW_NWK_CANCEL, NWK_APS_STATE);
#endif
        if ((ctx->stopping || (ctx->cancel && !ctx->active_ack)) && !ctx->cancel_sent && !event) {
            memset(&nwk_aps_work.cancellation, 0, sizeof(nwk_aps_work.cancellation));
            NWK_APS_CANCEL_SOURCE.kind = MAC_TX_EVENT_CANCEL;
            NWK_APS_CANCEL_SOURCE.generation = NWK_APS_ENGINE(ctx->owner)->generation;
            NWK_APS_CANCEL_SOURCE.retry = NWK_APS_ENGINE(ctx->owner)->retries; NWK_APS_CANCEL_SOURCE.nb = NWK_APS_ENGINE(ctx->owner)->nb;
            NWK_APS_CANCEL_SOURCE.stamp = now; event = &nwk_aps_work.cancellation; ctx->cancel_sent = 1;
        }
        if (NWK_APS_STEP(ctx->owner, now, event, action) != MAC_TX_OK)
            return LW_RETURN(LW_NWK_CANCEL, NWK_APS_RADIO);
#if defined(CC2530_MAC_LINK_WORKSPACE)
        if (!LW_LEAVE(LW_NWK_CANCEL)) return NWK_APS_STATE;
#endif
#if defined(CC2530_MAC_LINK)
        if (ctx->owner->engine.phase == MAC_TX_DRAW && !ctx->stopping && !(ctx->cancel && !ctx->active_ack)) arm(ctx, action);
#endif
        if (!ctx->active_ack && NWK_APS_ENGINE(ctx->owner)->transmissions) ctx->sent = 1;
        if (NWK_APS_ENGINE(ctx->owner)->phase == MAC_TX_FAULT) {
            ctx->ready = 0; ctx->error = NWK_APS_ENGINE(ctx->owner)->outcome;
            return NWK_APS_RADIO;
        }
        if (NWK_APS_ENGINE(ctx->owner)->phase != MAC_TX_DONE) return NWK_APS_OK;
#if defined(CC2530_MAC_LINK)
        if (!ctx->disarmed) {
            if (ctx->arm_issued != 2) {
                action->control.kind = NWK_APS_ACTION_DISARM;
                action->control.generation = ctx->owner->engine.generation;
                action->control.retry = ctx->owner->engine.retries;
                action->control.nb = ctx->owner->engine.nb;
                ctx->arm_issued = 2;
            }
            return NWK_APS_OK;
        }
#endif
        outcome = NWK_APS_ENGINE(ctx->owner)->outcome;
        if (NWK_APS_RELEASE(ctx->owner) != MAC_TX_OK) return NWK_APS_RADIO;
        ctx->active = 0;
#if defined(CC2530_MAC_LINK)
        ctx->armed = ctx->arm_issued = 0;
#endif
        if (ctx->active_ack) {
            ctx->reply = ctx->cancel_sent = 0; ctx->reply_result = outcome;
            if (outcome != MAC_TX_ACKED && !ctx->stopping) return NWK_APS_ACK_FAILED;
        } else if (ctx->cancel) nwk_aps_complete(ctx, ctx->cancel_result);
        else if (outcome != MAC_TX_ACKED && outcome != MAC_TX_UNACKNOWLEDGED) {
            nwk_aps_complete(ctx, NWK_APS_RADIO);
        } else if (!ctx->special && (ctx->outgoing.aps.flags & APS_FLAG_ACK_REQUEST) &&
                   !ctx->outgoing.nwk.type && !ctx->seen_ack) {
            ctx->waiting = 1; ctx->deadline = now+ctx->ack_wait;
        } else nwk_aps_complete(ctx, NWK_APS_OK);
        return NWK_APS_OK;
    }
    if (event) return NWK_APS_IGNORED;
    if (NWK_APS_ENGINE(ctx->owner)->phase != MAC_TX_IDLE) return NWK_APS_STATE;
    if (ctx->stopping) {
        if (ctx->reply) { ctx->reply = 0; ctx->reply_result = MAC_TX_CANCELLED; }
        if (ctx->queued) nwk_aps_complete(ctx, NWK_APS_CANCELLED);
        ctx->stopping = ctx->cancel = ctx->cancel_sent = 0;
        return NWK_APS_OK;
    }
    if (ctx->cancel && ctx->queued) nwk_aps_complete(ctx, ctx->cancel_result);
    if (ctx->waiting && ctx->seen_ack) nwk_aps_complete(ctx, NWK_APS_OK);
    if (ctx->waiting && reached(now, ctx->deadline)) {
        ctx->waiting = 0;
        if (ctx->retries == NWK_APS_RETRIES) nwk_aps_complete(ctx, NWK_APS_TIMEOUT);
        else { ctx->retries++; ctx->sent = ctx->seen_ack = 0; }
    }
    if (ctx->reply || (ctx->queued && !ctx->waiting)) {
        result = nwk_aps_transmit(ctx, ctx->reply, now);
        if (result) {
            if (!ctx->reply) nwk_aps_complete(ctx, (uint8_t)result);
            return result;
        }
        if (!ctx->active) return NWK_APS_OK;
#if defined(CC2530_MAC_LINK)
        arm(ctx, action);
        return NWK_APS_OK;
#else
        return NWK_APS_STEP(ctx->owner, now, NULL, action) == MAC_TX_OK ? NWK_APS_OK : NWK_APS_RADIO;
#endif
    }
    return NWK_APS_OK;
}

static uint8_t acknowledgment_matches(nwk_aps_t * volatile ctx, const ed_packet_t * volatile p)
{
    const ed_packet_t * volatile sent = &ctx->outgoing;
    return ctx->queued && ctx->sent && !ctx->special && !sent->nwk.type &&
        !ctx->cancel && !reached(ctx->last, ctx->transaction_until) &&
        (sent->aps.flags & APS_FLAG_ACK_REQUEST) && p->nwk.source == sent->nwk.destination &&
        p->nwk.destination == sent->nwk.source && p->aps.counter == sent->aps.counter &&
        !(p->aps.flags & APS_FLAG_ACK_FORMAT) && p->aps.cluster_id == sent->aps.cluster_id &&
        p->aps.profile_id == sent->aps.profile_id &&
        p->aps.source_endpoint == sent->aps.destination_endpoint &&
        p->aps.destination_endpoint == sent->aps.source_endpoint &&
        (!ctx->waiting || !reached(ctx->last, ctx->deadline));
}

static void acknowledgment(nwk_aps_t * volatile ctx, const ed_packet_t * volatile p, volatile uint8_t secured)
{
    ed_packet_t * volatile a = &ctx->acknowledgment;
    memset(a, 0, sizeof(*a));
    a->nwk.version = 2; a->nwk.destination = p->nwk.source;
    a->nwk.radius = 30; a->nwk.discover_route = NWK_DISCOVER_ROUTE_ENABLE;
    a->aps.type = ED_APS_ACK; a->aps.counter = p->aps.counter;
    if (p->aps.type == ED_APS_COMMAND) a->aps.flags = APS_FLAG_ACK_FORMAT;
    else {
        a->aps.destination_endpoint = p->aps.source_endpoint;
        a->aps.source_endpoint = p->aps.destination_endpoint;
        a->aps.profile_id = p->aps.profile_id; a->aps.cluster_id = p->aps.cluster_id;
    }
    ctx->reply = 1; ctx->reply_secure = secured;
}

nwk_aps_result_t nwk_aps_receive(nwk_aps_t * volatile ctx, const uint8_t * volatile npdu,
    volatile uint16_t length, volatile uint32_t now)
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    LW_ROOT(NWK_APS_STATE);
    if (!LW_IO(LW_NONE, npdu, length, 0)) return NWK_APS_ARGUMENT;
#endif
    nwk_aps_result_t result;
    security_keys_result_t accepted;
    uint8_t event, i, free_slot = NWK_APS_DUPLICATES, duplicate = 0, kind, counter, broadcast = 0, slot = 0;
    ed_packet_t * volatile p;
    if (!npdu) return NWK_APS_ARGUMENT;
    result = advance(ctx, now);
    if (result) return result;
    if (ctx->stopping) return NWK_APS_STATE;
    if (ctx->receive_ready || ctx->reply) return NWK_APS_FULL;
#if defined(CC2530_MAC_LINK_WORKSPACE)
    LW_ENTER(LW_NWK_HINT, NWK_APS_STATE);
#endif
    if (LW_CALL(LW_HINT, ed_wire_nwk(npdu, length, &nwk_aps_work.hint)) != ZIGBEE_SECURITY_OK)
        return LW_RETURN(LW_NWK_HINT, NWK_APS_WIRE);
    if (nwk_aps_work.hint.header.destination >= 0xfffbu) {
        result = nwk_aps_broadcast_slot(ctx, nwk_aps_work.hint.header.source, nwk_aps_work.hint.header.sequence, now, &slot);
        if (result) return LW_RETURN(LW_NWK_HINT, result);
        broadcast = 1;
    }
#if defined(CC2530_MAC_LINK_WORKSPACE)
    if (!LW_LEAVE(LW_NWK_HINT)) return NWK_APS_STATE;
#endif
    /* The slot is unoccupied and all lower operations are synchronous. The
     * key owner publishes into it only after authentication AND durable save.
     * Transport admission still owns receive_ready; no packet escapes early. */
    accepted = security_keys_receive(npdu, length, &ctx->incoming, &event, &ctx->limits, ctx->nv_polls);
    if (accepted) {
        ctx->error = (uint8_t)accepted;
        if (accepted == SECURITY_KEYS_STORAGE || accepted == SECURITY_KEYS_CRYPTO ||
            accepted == SECURITY_KEYS_EXHAUSTED) ctx->ready = 0;
        result = NWK_APS_SECURITY; goto discard;
    }
    p = &ctx->incoming;
    if (broadcast) nwk_aps_broadcast_put(ctx, slot, p->nwk.source, p->nwk.sequence, now);
    if (!p->nwk.type && p->nwk.destination >= 0xfffbu && (p->aps.flags & APS_FLAG_ACK_REQUEST)) {
        result = NWK_APS_WIRE; goto discard;
    }
    if (!p->nwk.type && p->aps.type == ED_APS_ACK) {
        if (!acknowledgment_matches(ctx, p) || (ctx->secure && !(event & 0x80u))) {
            result = NWK_APS_IGNORED; goto discard;
        }
        ctx->seen_ack = 1;
        result = NWK_APS_OK; goto discard;
    }
    if (!p->nwk.type && p->aps.type == APS_FRAME_DATA) {
        if (p->aps.destination_endpoint || p->aps.profile_id) {
            if (!ctx->ready || (p->aps.destination_endpoint != ctx->endpoint &&
                p->aps.destination_endpoint != 255) || p->aps.profile_id != ctx->profile) {
                result = NWK_APS_IGNORED; goto discard;
            }
        }
    }
    if ((event & SECURITY_KEYS_EVENT_MASK) == SECURITY_KEYS_EVENT_LEAVE ||
        (event & SECURITY_KEYS_EVENT_MASK) == SECURITY_KEYS_EVENT_UPDATE) {
        ctx->ready = 0;
        if (ctx->queued) { ctx->cancel = 1; ctx->cancel_result = NWK_APS_CANCELLED; }
    }
    /* A durable owner transition must reach its reserved control recipient,
     * even when the ordinary APS duplicate table has no free record. */
    if (p->nwk.type || (event & SECURITY_KEYS_EVENT_MASK) >= SECURITY_KEYS_EVENT_NETWORK_KEY) {
        if (!p->nwk.type && (p->aps.flags & APS_FLAG_ACK_REQUEST))
            acknowledgment(ctx, p, !!(event & SECURITY_KEYS_EVENT_APS_SECURED));
        ctx->event = event & SECURITY_KEYS_EVENT_MASK; ctx->receive_ready = 1;
        return NWK_APS_OK;
    }
    kind = p->nwk.type ? 4u : p->aps.type;
    counter = p->nwk.type ? p->nwk.sequence : p->aps.counter;
    for (i = 0; i < NWK_APS_DUPLICATES; i++) {
        nwk_aps_duplicate_t * volatile entry = &ctx->duplicate[i];
        if (entry->used && reached(now, entry->until)) entry->used = 0;
        if (!entry->used) free_slot = i;
        else if (entry->source == p->nwk.source && entry->counter == counter && entry->kind == kind)
            duplicate = 1;
    }
    if (!duplicate && (free_slot == NWK_APS_DUPLICATES || ctx->receive_ready)) {
        result = NWK_APS_FULL; goto discard;
    }
    if (!p->nwk.type && (p->aps.flags & APS_FLAG_ACK_REQUEST)) {
        if (ctx->reply) { result = NWK_APS_FULL; goto discard; }
        acknowledgment(ctx, p, !!(event & 0x80u));
    }
    if (duplicate) { result = NWK_APS_DUPLICATE; goto discard; }
    ctx->duplicate[free_slot].used = 1;
    ctx->duplicate[free_slot].source = p->nwk.source;
    ctx->duplicate[free_slot].counter = counter; ctx->duplicate[free_slot].kind = kind;
    ctx->duplicate[free_slot].until = now+ctx->duplicate_time;
    ctx->event = event & 0x7fu; ctx->receive_ready = 1;
    return NWK_APS_OK;
discard:
    memset(&ctx->incoming, 0, sizeof(ctx->incoming));
    return result;
}

nwk_aps_result_t nwk_aps_take(nwk_aps_t * volatile ctx, ed_packet_t * volatile packet, uint8_t * volatile event)
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    if (!LW_IO(LW_NONE, ctx, sizeof(*ctx), 1) || !LW_IO(LW_NONE, packet, sizeof(*packet), 1) ||
        !LW_IO(LW_NONE, event, 1, 1)) return NWK_APS_ARGUMENT;
#endif
    if (!ctx || ctx->version != NWK_APS_VERSION || !packet || !event) return NWK_APS_ARGUMENT;
    if (!ctx->receive_ready) return NWK_APS_STATE;
    *packet = ctx->incoming; *event = ctx->event;
    memset(&ctx->incoming, 0, sizeof(ctx->incoming)); ctx->receive_ready = 0;
    return NWK_APS_OK;
}

nwk_aps_result_t nwk_aps_confirm(nwk_aps_t * volatile ctx, uint8_t * volatile result)
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    if (!LW_IO(LW_NONE, ctx, sizeof(*ctx), 1) || !LW_IO(LW_NONE, result, 1, 1)) return NWK_APS_ARGUMENT;
#endif
    if (!ctx || ctx->version != NWK_APS_VERSION || !result) return NWK_APS_ARGUMENT;
    if (!ctx->completed) return NWK_APS_STATE;
    *result = ctx->result; ctx->completed = 0;
    return NWK_APS_OK;
}

nwk_aps_result_t nwk_aps_cancel(nwk_aps_t * volatile ctx, volatile uint32_t now)
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    LW_ROOT(NWK_APS_STATE);
#endif
    nwk_aps_result_t result = advance(ctx, now);
    if (result) return result;
    if (!ctx->queued) return NWK_APS_STATE;
    ctx->cancel = 1; ctx->cancel_result = NWK_APS_CANCELLED;
    return NWK_APS_OK;
}

nwk_aps_result_t nwk_aps_stop(nwk_aps_t * volatile ctx, volatile uint32_t now)
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    LW_ROOT(NWK_APS_STATE);
#endif
    nwk_aps_result_t result = advance(ctx, now);
    if (result) return result;
    ctx->ready = 0; ctx->stopping = 1;
    if (ctx->queued) { ctx->cancel = 1; ctx->cancel_result = NWK_APS_CANCELLED; }
    return NWK_APS_OK;
}
