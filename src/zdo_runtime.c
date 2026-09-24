/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "zdo_runtime.h"
#include "security_keys.h"
#include <stddef.h>
#include <string.h>

static MCU_XDATA security_keys_status_t keys;
static MCU_XDATA zdo_srv_local_t local;
static MCU_XDATA zdo_srv_rx_t rx;
static MCU_XDATA zdo_srv_info_t info;
static MCU_XDATA zdo_node_response_t node;
static MCU_XDATA uint8_t descriptor_bytes[17];

static uint16_t little(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint8_t expired(uint32_t now, uint32_t until)
{
    return (uint32_t)(now-until) < MAC_TX_HALF;
}

static zdo_runtime_result_t advance(zdo_runtime_t *ctx, uint32_t now)
{
    if (!ctx || ctx->version != 1) return ZDO_RUNTIME_ARGUMENT;
    if ((uint32_t)(now-ctx->last) >= MAC_TX_HALF) return ZDO_RUNTIME_CLOCK;
    ctx->last = now;
    return ZDO_RUNTIME_OK;
}

static void base(ed_packet_t *p, uint16_t destination, uint16_t cluster)
{
    memset(p, 0, sizeof(*p));
    p->nwk.version = 2; p->nwk.radius = 30; p->nwk.destination = destination;
    p->nwk.discover_route = NWK_DISCOVER_ROUTE_ENABLE;
    p->aps.cluster_id = cluster;
}

zdo_runtime_result_t zdo_runtime_init(zdo_runtime_t *ctx,
    const zdo_node_descriptor_t *descriptor, uint32_t now)
{
    uint8_t length;
    if (!ctx || !descriptor || descriptor->logical_type != 2) return ZDO_RUNTIME_ARGUMENT;
    memset(&node, 0, sizeof(node)); node.descriptor = *descriptor; node.address = 1;
    if (zdo_node_rsp_encode(&node, descriptor_bytes, sizeof(descriptor_bytes), &length) != ZDO_NODE_OK)
        return ZDO_RUNTIME_ARGUMENT;
    memset(ctx, 0, sizeof(*ctx));
    ctx->local = *descriptor; ctx->last = now; ctx->version = 1;
    ctx->security_event = ZDO_RUNTIME_NO_EVENT;
    return ZDO_RUNTIME_OK;
}

zdo_runtime_result_t zdo_runtime_request(zdo_runtime_t *ctx, nwk_aps_t *transport,
    uint8_t which, uint32_t now)
{
    zdo_runtime_result_t result = advance(ctx, now);
    nwk_aps_result_t queued;
    ed_packet_t *p;
    if (result) return result;
    if (!transport || which < 1 || which > 4) return ZDO_RUNTIME_ARGUMENT;
    if (ctx->query || ctx->response_pending || ctx->response_tx) return ZDO_RUNTIME_FULL;
    if (security_keys_status(&keys) != SECURITY_KEYS_OK) return ZDO_RUNTIME_SECURITY;
    p = &ctx->response; base(p, 0, which == 1 ? 2 : which == 2 ? 0 : 1);
    p->payload[0] = ctx->next_tsn; p->length = 3;
    if (which == ZDO_RUNTIME_NWK_ADDRESS) {
        memcpy(p->payload+1, keys.config.tc_ieee, 8); p->length = 11;
    } else if (which == ZDO_RUNTIME_IEEE_ADDRESS) p->length = 5;
    else if (which == ZDO_RUNTIME_PARENT) {
        p->nwk.type = ED_NWK_COMMAND; p->nwk.radius = 1;
        p->nwk.flags = NWK_FLAG_SOURCE_IEEE | NWK_FLAG_DESTINATION_IEEE;
        memcpy(p->nwk.source_ieee, keys.config.own_ieee, 8);
        memcpy(p->nwk.destination_ieee, keys.config.tc_ieee, 8);
        p->payload[0] = 0x0b; p->payload[1] = 1;
    }
    queued = nwk_aps_queue(transport, p, 0, now);
    if (queued != NWK_APS_OK) return queued == NWK_APS_FULL ? ZDO_RUNTIME_FULL : ZDO_RUNTIME_TRANSMIT;
    ctx->sequence = ctx->next_tsn++; ctx->query = which; ctx->query_tx = 1;
    ctx->tx_done = 0; ctx->result = ZDO_RUNTIME_NO_EVENT; ctx->deadline = now+ZDO_RUNTIME_WAIT;
    return ZDO_RUNTIME_OK;
}

static zdo_runtime_result_t address_request(zdo_runtime_t *ctx, const ed_packet_t *p)
{
    ed_packet_t *r = &ctx->response;
    uint8_t ieee_request = p->aps.cluster_id == 1;
    uint8_t type, match, broadcast = p->nwk.destination >= 0xfffbu || p->aps.delivery_mode == 2;
    uint16_t address;
    if (p->length < (ieee_request ? 5 : 11)) return ZDO_RUNTIME_FORMAT;
    address = ieee_request ? little(p->payload+1) : keys.config.address;
    match = ieee_request ? address == keys.config.address :
        !memcmp(p->payload+1, keys.config.own_ieee, 8);
    type = p->payload[ieee_request ? 3 : 9];
    if (broadcast && (!match || type > 1)) return ZDO_RUNTIME_IGNORED;
    base(r, p->nwk.source, p->aps.cluster_id | 0x8000u);
    r->aps.destination_endpoint = p->aps.source_endpoint;
    r->payload[0] = p->payload[0];
    r->payload[1] = !match ? ZDO_NODE_DEVICE_NOT_FOUND : type > 1 ? ZDO_NODE_INVALID_REQUEST : 0;
    if (ieee_request) {
        if (match) memcpy(r->payload+2, keys.config.own_ieee, 8);
        else memset(r->payload+2, 255, 8);
    } else {
        memcpy(r->payload+2, p->payload+1, 8);
        if (!match) address = 0xffffu;
    }
    r->payload[10] = (uint8_t)address; r->payload[11] = (uint8_t)(address >> 8);
    r->length = !r->payload[1] && type == 1 ? 13 : 12;
    ctx->response_pending = 1;
    return ZDO_RUNTIME_OK;
}

static zdo_runtime_result_t announce(zdo_runtime_t *ctx, nwk_aps_t *transport, const ed_packet_t *p)
{
    uint8_t i, found = ZDO_RUNTIME_MAP_SIZE, free_slot = ZDO_RUNTIME_MAP_SIZE, unknown = 1;
    uint16_t address;
    if (p->length < 12 || p->nwk.destination != 0xfffdu) return ZDO_RUNTIME_FORMAT;
    address = little(p->payload+1);
    if (address > 0xfff7u) return ZDO_RUNTIME_FORMAT;
    if (!ctx->map[0].used) {
        memcpy(ctx->map[0].ieee, keys.config.own_ieee, 8);
        ctx->map[0].address = keys.config.address; ctx->map[0].used = ctx->map[0].valid = 1;
        memcpy(ctx->map[1].ieee, keys.config.tc_ieee, 8);
        ctx->map[1].used = ctx->map[1].valid = 1;
    }
    for (i = 0; i < ZDO_RUNTIME_MAP_SIZE; i++) {
        zdo_runtime_address_t *m = &ctx->map[i];
        if (!m->used) free_slot = i;
        else if (!memcmp(m->ieee, p->payload+3, 8)) found = i;
        else if (m->valid && m->address == address) {
            m->valid = 0;
            if (i < 2) ctx->conflict = 1;
        }
    }
    if (found < 2 && ctx->map[found].address != address) ctx->conflict = 1;
    if (ctx->conflict) { transport->ready = 0; return ZDO_RUNTIME_CONFLICT; }
    for (i = 0; i < 8; i++) if (p->payload[3+i] != 255) unknown = 0;
    if (unknown) return ZDO_RUNTIME_OK;
    if (found == ZDO_RUNTIME_MAP_SIZE) found = free_slot;
    if (found == ZDO_RUNTIME_MAP_SIZE) return ZDO_RUNTIME_FULL;
    memcpy(ctx->map[found].ieee, p->payload+3, 8);
    ctx->map[found].address = address; ctx->map[found].valid = ctx->map[found].used = 1;
    return ZDO_RUNTIME_OK;
}

static zdo_runtime_result_t received(zdo_runtime_t *ctx, nwk_aps_t *transport)
{
    const ed_packet_t *p = &ctx->packet;
    uint8_t event;
    if (nwk_aps_take(transport, &ctx->packet, &event) != NWK_APS_OK) return ZDO_RUNTIME_STATE;
    if (security_keys_status(&keys) != SECURITY_KEYS_OK) return ZDO_RUNTIME_SECURITY;
    if (event != SECURITY_KEYS_EVENT_DATA && event != SECURITY_KEYS_EVENT_MANAGEMENT) {
        ctx->security_event = event;
        return ZDO_RUNTIME_OK;
    }
    if (p->nwk.type) {
        if (ctx->query != ZDO_RUNTIME_PARENT || ctx->result != ZDO_RUNTIME_NO_EVENT ||
            (!ctx->tx_done && !transport->sent) || expired(ctx->last, ctx->deadline))
            return ZDO_RUNTIME_IGNORED;
        if (p->length < 3 || p->payload[0] != 0x0c || p->payload[1] > 1 ||
            p->nwk.source || p->nwk.destination != keys.config.address || p->nwk.radius != 1 ||
            (p->nwk.flags & (NWK_FLAG_SOURCE_IEEE | NWK_FLAG_DESTINATION_IEEE)) !=
                (NWK_FLAG_SOURCE_IEEE | NWK_FLAG_DESTINATION_IEEE) ||
            memcmp(p->nwk.source_ieee, keys.config.tc_ieee, 8) ||
            memcmp(p->nwk.destination_ieee, keys.config.own_ieee, 8)) return ZDO_RUNTIME_FORMAT;
        ctx->result = p->payload[1] ? ZDO_RUNTIME_REMOTE : ZDO_RUNTIME_OK;
        if (!ctx->result) {
            ctx->parent_information = p->payload[2]; ctx->parent_known = 1;
            transport->parent_information = p->payload[2];
        }
        return ZDO_RUNTIME_OK;
    }
    if (p->aps.destination_endpoint || p->aps.profile_id) {
        ctx->application = *p; ctx->application_ready = 1;
        return ZDO_RUNTIME_OK;
    }
    if (p->aps.type || !p->length) return ZDO_RUNTIME_FORMAT;
    if (p->aps.cluster_id & 0x8000u) {
        uint16_t cluster = ctx->query == 1 ? 0x8002u : ctx->query == 2 ? 0x8000u : 0x8001u;
        if (!ctx->query || ctx->query == 4 || ctx->result != ZDO_RUNTIME_NO_EVENT ||
            (!ctx->tx_done && !transport->sent) || expired(ctx->last, ctx->deadline) ||
            p->nwk.source || p->nwk.destination != keys.config.address ||
            p->aps.source_endpoint || p->aps.delivery_mode || p->aps.cluster_id != cluster ||
            p->payload[0] != ctx->sequence) return ZDO_RUNTIME_IGNORED;
        if (ctx->query == ZDO_RUNTIME_NODE) {
            if (zdo_node_rsp_decode(p->payload, p->length, &node) != ZDO_NODE_OK ||
                (node.has_address && node.address)) return ZDO_RUNTIME_FORMAT;
            ctx->result = node.status ? ZDO_RUNTIME_REMOTE : ZDO_RUNTIME_OK;
            if (!node.status) ctx->tc = node.descriptor;
        } else {
            if (p->length < 12 || (p->payload[1] && p->payload[1] != ZDO_NODE_DEVICE_NOT_FOUND &&
                p->payload[1] != ZDO_NODE_INVALID_REQUEST)) return ZDO_RUNTIME_FORMAT;
            if (!p->payload[1] && (little(p->payload+10) || memcmp(p->payload+2, keys.config.tc_ieee, 8)))
                return ZDO_RUNTIME_FORMAT;
            ctx->result = p->payload[1] ? ZDO_RUNTIME_REMOTE : ZDO_RUNTIME_OK;
        }
        return ZDO_RUNTIME_OK;
    }
    if (p->aps.cluster_id == ZDO_SRV_DEVICE_ANNCE) return announce(ctx, transport, p);
    if (ctx->response_pending) {
        ctx->response_result = NWK_APS_FULL;
        return ZDO_RUNTIME_DROPPED;
    }
    if (p->aps.cluster_id < 2) return address_request(ctx, p);
    local.descriptor = ctx->local; local.address = keys.config.address;
    rx.header = p->aps; rx.broadcast = p->nwk.destination >= 0xfffbu || p->aps.delivery_mode == 2;
    base(&ctx->response, p->nwk.source, 0);
    if (zdo_srv_handle(&local, &rx, p->payload, p->length, ctx->response.payload,
        sizeof(ctx->response.payload), &info) != ZDO_SRV_OK) return ZDO_RUNTIME_FORMAT;
    if (info.kind != ZDO_SRV_REPLY) return ZDO_RUNTIME_IGNORED;
    ctx->response.aps.cluster_id = info.cluster_id;
    ctx->response.aps.destination_endpoint = info.endpoint;
    ctx->response.length = info.length; ctx->response_pending = 1;
    return ZDO_RUNTIME_OK;
}

zdo_runtime_result_t zdo_runtime_step(zdo_runtime_t *ctx, nwk_aps_t *transport, uint32_t now)
{
    uint8_t result;
    zdo_runtime_result_t rc = advance(ctx, now);
    nwk_aps_result_t queued;
    if (rc) return rc;
    if (!transport) return ZDO_RUNTIME_ARGUMENT;
    ctx->security_event = ZDO_RUNTIME_NO_EVENT;
    if ((ctx->query_tx || ctx->response_tx) && transport->completed) {
        if (nwk_aps_confirm(transport, &result) != NWK_APS_OK) return ZDO_RUNTIME_TRANSMIT;
        if (ctx->query_tx) {
            ctx->query_tx = 0; ctx->tx_done = 1;
            if (result && ctx->result != ZDO_RUNTIME_TIMEOUT && ctx->result != ZDO_RUNTIME_CANCELLED)
                ctx->result = ZDO_RUNTIME_TRANSMIT;
        } else {
            ctx->response_tx = 0; ctx->response_result = result;
            if (result) return ZDO_RUNTIME_DROPPED;
        }
    }
    if (ctx->query && ctx->result == ZDO_RUNTIME_NO_EVENT && expired(now, ctx->deadline)) {
        ctx->result = ZDO_RUNTIME_TIMEOUT;
        if (ctx->query_tx && nwk_aps_cancel(transport, now) != NWK_APS_OK) return ZDO_RUNTIME_TRANSMIT;
    }
    if (transport->receive_ready && !ctx->application_ready)
        rc = received(ctx, transport);
    if (ctx->response_pending && !transport->queued && !transport->completed) {
        queued = nwk_aps_queue(transport, &ctx->response, 0, now);
        if (queued == NWK_APS_EXHAUSTED) {
            ctx->response_pending = 0; ctx->response_result = (uint8_t)queued;
            return ZDO_RUNTIME_DROPPED;
        }
        if (queued != NWK_APS_OK) return queued == NWK_APS_FULL ? ZDO_RUNTIME_FULL : ZDO_RUNTIME_TRANSMIT;
        ctx->response_pending = 0; ctx->response_tx = 1;
    }
    return rc;
}

zdo_runtime_result_t zdo_runtime_cancel(zdo_runtime_t *ctx, nwk_aps_t *transport, uint32_t now)
{
    zdo_runtime_result_t result = advance(ctx, now);
    if (result) return result;
    if (!transport) return ZDO_RUNTIME_ARGUMENT;
    if ((ctx->query_tx || ctx->response_tx) && transport->queued &&
        nwk_aps_cancel(transport, now) != NWK_APS_OK) return ZDO_RUNTIME_TRANSMIT;
    if (ctx->query) ctx->result = ZDO_RUNTIME_CANCELLED;
    if (ctx->response_pending) {
        ctx->response_pending = 0; ctx->response_result = NWK_APS_CANCELLED;
    }
    return ZDO_RUNTIME_OK;
}

zdo_runtime_result_t zdo_runtime_take_result(zdo_runtime_t *ctx, uint8_t *which, uint8_t *result)
{
    if (!ctx || ctx->version != 1 || !which || !result) return ZDO_RUNTIME_ARGUMENT;
    if (!ctx->query || !ctx->tx_done || ctx->result == ZDO_RUNTIME_NO_EVENT) return ZDO_RUNTIME_STATE;
    *which = ctx->query; *result = ctx->result; ctx->query = 0;
    return ZDO_RUNTIME_OK;
}

zdo_runtime_result_t zdo_runtime_take_application(zdo_runtime_t *ctx, ed_packet_t *packet)
{
    if (!ctx || ctx->version != 1 || !packet) return ZDO_RUNTIME_ARGUMENT;
    if (!ctx->application_ready) return ZDO_RUNTIME_STATE;
    *packet = ctx->application;
    memset(&ctx->application, 0, sizeof(ctx->application)); ctx->application_ready = 0;
    return ZDO_RUNTIME_OK;
}
