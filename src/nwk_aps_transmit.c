/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "nwk_aps_internal.h"
#include <stddef.h>
#include <string.h>

/* Frame control/DSN(3), destination PAN/short(4), compressed source short(2).
 * This profile never changes MAC addressing shape. Keep the real codec's
 * input/output disjoint by encoding a zero-payload header into this prefix.
 */
#define MAC_PREFIX 9u
typedef char mac_payload_extent[MAC_PREFIX+NWK_FRAME_MAX_BODY == MAC_FRAME_MAX_BODY ? 1 : -1];

nwk_aps_result_t nwk_aps_transmit(nwk_aps_t * volatile ctx, volatile uint8_t acknowledgment, volatile uint32_t now)
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    LW_ROOT(NWK_APS_STATE);
    if (!LW_IO(LW_NONE, ctx, sizeof(*ctx), 1)) return NWK_APS_ARGUMENT;
#endif
    ed_packet_t * volatile p = acknowledgment ? &ctx->acknowledgment : &ctx->outgoing;
    security_keys_result_t secured;
    nwk_aps_result_t btr;
    uint8_t slot;
    if (security_keys_status(&nwk_aps_keys) != SECURITY_KEYS_OK) return NWK_APS_SECURITY;
    p->nwk.source = nwk_aps_keys.config.address; p->nwk.sequence = ctx->next_nwk++;
    if (!p->nwk.type && !ctx->special && p->nwk.destination < 0xfffbu)
        p->nwk.discover_route = NWK_DISCOVER_ROUTE_ENABLE;
    p->nwk.flags &= (uint16_t)~NWK_FLAG_END_DEVICE_INITIATOR;
    if (ctx->parent_information) p->nwk.flags |= NWK_FLAG_END_DEVICE_INITIATOR;
    if (!acknowledgment && ctx->special) {
        if (ctx->special == 3) {
            secured = security_keys_leave(p->nwk.sequence, ctx->mac+MAC_PREFIX, NWK_FRAME_MAX_BODY,
                &ctx->length, &ctx->limits, ctx->nv_polls);
            if (secured == SECURITY_KEYS_OK && !ctx->length) {
                ctx->quiet = 1; nwk_aps_complete(ctx, NWK_APS_OK);
                return NWK_APS_OK;
            }
        } else secured = ctx->special == 1 ?
            security_keys_request(p->nwk.sequence, p->aps.counter, ctx->mac+MAC_PREFIX, NWK_FRAME_MAX_BODY,
                                  &ctx->length, &ctx->limits, ctx->nv_polls) :
            security_keys_verify(p->nwk.sequence, p->aps.counter, ctx->mac+MAC_PREFIX, NWK_FRAME_MAX_BODY,
                                 &ctx->length, &ctx->limits, ctx->nv_polls);
    } else {
        secured = security_keys_send(p, acknowledgment ? ctx->reply_secure : ctx->secure,
            ctx->mac+MAC_PREFIX, NWK_FRAME_MAX_BODY, &ctx->length, &ctx->limits, ctx->nv_polls);
    }
    if (secured) { ctx->error = (uint8_t)secured; return NWK_APS_SECURITY; }
    if (!acknowledgment && (ctx->special == 3 || (!ctx->special && p->nwk.destination >= 0xfffbu))) {
        btr = nwk_aps_broadcast_slot(ctx, nwk_aps_keys.config.address, p->nwk.sequence, now, &slot);
        if (btr) return btr == NWK_APS_DUPLICATE ? NWK_APS_EXHAUSTED : btr;
        nwk_aps_broadcast_put(ctx, slot, nwk_aps_keys.config.address, p->nwk.sequence, now);
    }
#if defined(CC2530_MAC_LINK_WORKSPACE)
    LW_ENTER(LW_NWK_BUILD, NWK_APS_STATE);
#endif
    memset(&nwk_aps_work.mac_header, 0, sizeof(nwk_aps_work.mac_header));
    nwk_aps_work.mac_header.type = MAC_FRAME_DATA; nwk_aps_work.mac_header.flags = MAC_FLAG_PAN_COMPRESSION;
    nwk_aps_work.mac_header.source_mode = nwk_aps_work.mac_header.destination_mode = MAC_ADDRESS_SHORT;
    nwk_aps_work.mac_header.source_pan = nwk_aps_work.mac_header.destination_pan = nwk_aps_keys.config.pan;
    nwk_aps_work.mac_header.source[0] = (uint8_t)nwk_aps_keys.config.address;
    nwk_aps_work.mac_header.source[1] = (uint8_t)(nwk_aps_keys.config.address >> 8);
    /* R22 3.6.5: an ED sends NWK broadcasts to its parent's short address,
     * without MAC ACK, rather than acting as a broadcasting router. */
    if (acknowledgment || (ctx->special != 3 && (ctx->special || p->nwk.destination < 0xfffbu)))
        nwk_aps_work.mac_header.flags |= MAC_FLAG_ACK_REQUEST;
    if (!ctx->length || ctx->length > NWK_FRAME_MAX_BODY ||
        mac_frame_encode(&nwk_aps_work.mac_header, NULL, 0, ctx->mac,
                         MAC_PREFIX, &ctx->mac_length) != MAC_CODEC_OK ||
        ctx->mac_length != MAC_PREFIX)
        return LW_RETURN(LW_NWK_BUILD, NWK_APS_WIRE);
    ctx->mac_length += ctx->length;
    if (NWK_APS_SUBMIT(ctx->owner, ctx->mac, ctx->mac_length, now,
                      NWK_APS_TX_LIFETIME, NWK_APS_TX_WORK) != MAC_TX_OK)
        return LW_RETURN(LW_NWK_BUILD, NWK_APS_RADIO);
    ctx->active = 1; ctx->active_ack = acknowledgment;
    if (!acknowledgment) ctx->sent = 0;
    return LW_RETURN(LW_NWK_BUILD, NWK_APS_OK);
}
