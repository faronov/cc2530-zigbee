/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "bdb_join.h"
#include <stddef.h>
#include <string.h>

static MCU_XDATA security_keys_status_t keys;
static MCU_XDATA nwk_parent_policy_t policy;
static MCU_XDATA mac_frame_info_t mac;
static MCU_XDATA nwk_frame_info_t nwk;

static uint8_t expired(uint32_t now, uint32_t until)
{
    return (uint32_t)(now-until) < MAC_TX_HALF;
}

static bdb_join_result_t advance(bdb_join_t BDB_JOIN_RAM *ctx, uint32_t now)
{
    if (!ctx || ctx->version != 1) return BDB_JOIN_ARGUMENT;
    if ((uint32_t)(now-ctx->last) >= MAC_TX_HALF) return BDB_JOIN_CLOCK;
    ctx->last = now;
    return BDB_JOIN_OK;
}

static void fail(bdb_join_t BDB_JOIN_RAM *ctx, uint8_t result)
{
    ctx->result = result; ctx->member = ctx->transport.ready = 0;
    ctx->phase = BDB_JOIN_ABORTING;
    if (ctx->transport.queued && nwk_aps_cancel(&ctx->transport, ctx->last) != NWK_APS_OK) {
        ctx->cleanup_error = BDB_JOIN_TRANSMIT_FAILED; ctx->phase = BDB_JOIN_FAULT;
    }
}

bdb_join_result_t bdb_join_init(bdb_join_t BDB_JOIN_RAM *ctx, uint32_t now)
{
    if (!ctx) return BDB_JOIN_ARGUMENT;
    memset(ctx, 0, sizeof(*ctx));
    if (mac_scan_init(&ctx->scan) != MAC_SCAN_OK ||
        mac_join_init(&ctx->association, now) != MAC_JOIN_OK) return BDB_JOIN_STATE;
    ctx->version = 1; ctx->last = now;
    return BDB_JOIN_OK;
}

bdb_join_result_t bdb_join_start(bdb_join_t BDB_JOIN_RAM *ctx, mac_tx_t BDB_JOIN_RAM *owner,
    const bdb_join_config_t *config, uint32_t now)
{
    bdb_join_result_t result = advance(ctx, now);
    if (result) return result;
    if (!owner || !config || config->link_cost < 1 || config->link_cost > 3 ||
        !config->association.extraction.epoch || config->descriptor.logical_type != 2 ||
        config->descriptor.mac_capability != config->association.capability ||
        config->scan.saved.pan != config->association.saved.pan ||
        config->scan.saved.channel != config->association.saved.channel ||
        config->scan.saved.filter != config->association.saved.filter ||
        config->scan.saved.rx_on != config->association.saved.rx_on) return BDB_JOIN_ARGUMENT;
    if (ctx->phase != BDB_JOIN_IDLE || owner->phase != MAC_TX_IDLE) return BDB_JOIN_STATE;
    if (security_keys_status(&keys) != SECURITY_KEYS_OK) return BDB_JOIN_SECURITY;
    if (keys.phase != SECURITY_KEYS_PROVISIONED) return BDB_JOIN_RECOVERY_REQUIRED;
    if (config->scan.channels != (1UL << keys.config.channel) ||
        config->association.extraction.pan != keys.config.pan ||
        config->association.extraction.channel != keys.config.channel ||
        config->association.extraction.local_mode != MAC_ADDRESS_EXTENDED ||
        config->association.extraction.coordinator_mode != MAC_ADDRESS_EXTENDED ||
        memcmp(config->association.extraction.local, keys.config.own_ieee, 8) ||
        memcmp(config->association.extraction.coordinator, keys.config.tc_ieee, 8))
        return BDB_JOIN_ARGUMENT;
    if (nwk_aps_init(&ctx->transport, owner, config->endpoint, config->profile, &config->crypto,
        config->nv_polls, config->ack_wait, config->broadcast_time, now) != NWK_APS_OK ||
        zdo_runtime_init(&ctx->zdo, &config->descriptor, now) != ZDO_RUNTIME_OK)
        return BDB_JOIN_ARGUMENT;
    ctx->config = *config; ctx->owner = owner; ctx->epoch = config->association.extraction.epoch;
    if (mac_scan_start(&ctx->scan, owner, &ctx->config.scan, now) != MAC_SCAN_OK)
        return BDB_JOIN_SCAN_FAILED;
    ctx->phase = BDB_JOIN_SCANNING;
    return BDB_JOIN_OK;
}

static bdb_join_result_t scanned(bdb_join_t BDB_JOIN_RAM *ctx)
{
    uint8_t i;
    memset(&policy, 0, sizeof(policy));
    if (security_keys_status(&keys) != SECURITY_KEYS_OK) return BDB_JOIN_SECURITY;
    memcpy(policy.extended_pan_id, keys.config.extended_pan, 8);
    policy.minimum_known = 1; policy.minimum_update_id = keys.config.update_id;
    for (i = 0; i < ctx->scan.candidates.count; i++) {
        const nwk_candidate_t *c = &ctx->scan.candidates.entries[i];
        policy.link_cost[i] = ctx->config.link_cost;
        if (c->address_mode == MAC_ADDRESS_EXTENDED && c->pan_id == keys.config.pan &&
            c->channel == keys.config.channel && c->network.update_id == keys.config.update_id &&
            (c->superframe & MAC_SUPERFRAME_PAN_COORDINATOR) &&
            !memcmp(c->coordinator, keys.config.tc_ieee, 8)) policy.potential_mask |= 1u << i;
    }
    if (ctx->scan.reason != MAC_SCAN_FINISHED ||
        nwk_parent_select(&ctx->scan.candidates, &policy, &ctx->parent) != NWK_PARENT_OK)
        return BDB_JOIN_NO_PARENT;
    return BDB_JOIN_OK;
}

static void base(bdb_join_t BDB_JOIN_RAM *ctx, uint16_t destination, uint16_t cluster)
{
    memset(&ctx->packet, 0, sizeof(ctx->packet));
    ctx->packet.nwk.version = 2; ctx->packet.nwk.radius = 30;
    ctx->packet.nwk.destination = destination; ctx->packet.aps.delivery_mode = 2;
    ctx->packet.aps.cluster_id = cluster; ctx->packet.payload[0] = ctx->zdo.next_tsn++;
}

static bdb_join_result_t announcement(bdb_join_t BDB_JOIN_RAM *ctx)
{
    if (security_keys_status(&keys) != SECURITY_KEYS_OK) return BDB_JOIN_SECURITY;
    base(ctx, 0xfffdu, 0x0013u);
    ctx->packet.payload[1] = (uint8_t)keys.config.address;
    ctx->packet.payload[2] = (uint8_t)(keys.config.address >> 8);
    memcpy(ctx->packet.payload+3, keys.config.own_ieee, 8);
    ctx->packet.payload[11] = ctx->config.association.capability; ctx->packet.length = 12;
    return nwk_aps_queue(&ctx->transport, &ctx->packet, 0, ctx->last) == NWK_APS_OK ?
        BDB_JOIN_OK : BDB_JOIN_TRANSMIT_FAILED;
}

static void install(bdb_join_t BDB_JOIN_RAM *ctx)
{
    if (ctx->token == 0xffffu) { fail(ctx, BDB_JOIN_INSTALL_FAILED); return; }
    ctx->token++; ctx->issued = 0; ctx->phase = BDB_JOIN_INSTALLING;
    ctx->until = ctx->last+BDB_JOIN_EXCHANGE_WAIT;
}

static uint8_t finished(bdb_join_t BDB_JOIN_RAM *ctx, uint8_t *result)
{
    return nwk_aps_confirm(&ctx->transport, result) == NWK_APS_OK;
}

static void runtime(bdb_join_t BDB_JOIN_RAM *ctx)
{
    uint8_t which, result;
    zdo_runtime_result_t rc;
    rc = zdo_runtime_step(&ctx->zdo, &ctx->transport, ctx->last);
    ctx->receive_result = (uint8_t)rc;
    if (rc == ZDO_RUNTIME_CONFLICT) { fail(ctx, BDB_JOIN_ADDRESS_CONFLICT); return; }
    if (rc == ZDO_RUNTIME_SECURITY || rc == ZDO_RUNTIME_TRANSMIT) {
        fail(ctx, rc == ZDO_RUNTIME_SECURITY ? BDB_JOIN_SECURITY : BDB_JOIN_TRANSMIT_FAILED); return;
    }
    if (ctx->zdo.security_event == SECURITY_KEYS_EVENT_LEAVE) {
        ctx->result = BDB_JOIN_REMOTE_LEAVE; ctx->member = ctx->transport.ready = 0;
        ctx->phase = BDB_JOIN_ABORTING;
    } else if (ctx->zdo.security_event == SECURITY_KEYS_EVENT_UPDATE) {
        if (ctx->phase != BDB_JOIN_READY) { fail(ctx, BDB_JOIN_RECOVERY_REQUIRED); return; }
        ctx->phase = BDB_JOIN_UPDATING; ctx->transport.ready = 0;
        if (ctx->transport.queued && nwk_aps_cancel(&ctx->transport, ctx->last) != NWK_APS_OK)
            fail(ctx, BDB_JOIN_TRANSMIT_FAILED);
    } else if (ctx->zdo.security_event == SECURITY_KEYS_EVENT_TC_KEY ||
               ctx->zdo.security_event == SECURITY_KEYS_EVENT_VERIFIED) {
        if (expired(ctx->last, ctx->until)) { fail(ctx, BDB_JOIN_TC_FAILED); return; }
        if (ctx->zdo.security_event == SECURITY_KEYS_EVENT_TC_KEY) ctx->got_tc = 1;
        else ctx->got_confirm = 1;
    }
    if (ctx->application_pending && ctx->transport.completed) {
        if (!finished(ctx, &ctx->application_result)) { fail(ctx, BDB_JOIN_TRANSMIT_FAILED); return; }
        ctx->application_pending = 0; ctx->application_done = 1;
    }
    if (ctx->phase == BDB_JOIN_WAIT_KEY && ctx->zdo.security_event == SECURITY_KEYS_EVENT_NETWORK_KEY) {
        if (expired(ctx->last, ctx->until)) { fail(ctx, BDB_JOIN_KEY_TIMEOUT); return; }
        ctx->member = 1;
        if (announcement(ctx)) fail(ctx, BDB_JOIN_TRANSMIT_FAILED);
        else ctx->phase = BDB_JOIN_ANNOUNCING;
    } else if (ctx->phase == BDB_JOIN_ANNOUNCING && finished(ctx, &result)) {
        if (result) fail(ctx, BDB_JOIN_TRANSMIT_FAILED);
        else { ctx->phase = BDB_JOIN_NODE; ctx->attempts = 0; }
    } else if (ctx->phase == BDB_JOIN_NODE) {
        if (zdo_runtime_take_result(&ctx->zdo, &which, &result) == ZDO_RUNTIME_OK) {
            if (!result && which == ZDO_RUNTIME_NODE && !ctx->zdo.tc.logical_type &&
                ctx->zdo.tc.stack_revision >= 21) {
                ctx->phase = BDB_JOIN_REQUESTING; ctx->issued = ctx->attempts = 0;
            } else if (result != ZDO_RUNTIME_TIMEOUT || ctx->attempts == BDB_JOIN_ATTEMPTS)
                fail(ctx, BDB_JOIN_TC_FAILED);
        }
        if (ctx->phase == BDB_JOIN_NODE && !ctx->zdo.query) {
            rc = zdo_runtime_request(&ctx->zdo, &ctx->transport, ZDO_RUNTIME_NODE, ctx->last);
            if (rc == ZDO_RUNTIME_OK) ctx->attempts++;
            else if (rc != ZDO_RUNTIME_FULL) fail(ctx, BDB_JOIN_TC_FAILED);
        }
    } else if (ctx->phase == BDB_JOIN_REQUESTING || ctx->phase == BDB_JOIN_VERIFYING) {
        if (!ctx->issued) {
            nwk_aps_result_t queued = nwk_aps_key_exchange(&ctx->transport,
                ctx->phase == BDB_JOIN_REQUESTING ? 1 : 2, ctx->last);
            if (queued == NWK_APS_OK) {
                ctx->issued = 1; ctx->attempts++; ctx->until = ctx->last+BDB_JOIN_EXCHANGE_WAIT;
            }
            else if (queued != NWK_APS_FULL) fail(ctx, BDB_JOIN_TC_FAILED);
        } else if (finished(ctx, &result)) {
            if (result) fail(ctx, BDB_JOIN_TRANSMIT_FAILED);
            else {
                ctx->phase = ctx->phase == BDB_JOIN_REQUESTING ? BDB_JOIN_WAIT_TC : BDB_JOIN_WAIT_CONFIRM;
            }
        }
    } else if (ctx->phase == BDB_JOIN_WAIT_TC && ctx->got_tc) {
        ctx->phase = BDB_JOIN_VERIFYING; ctx->issued = ctx->attempts = 0;
    } else if (ctx->phase == BDB_JOIN_WAIT_CONFIRM && ctx->got_confirm) {
        ctx->phase = BDB_JOIN_PARENT; ctx->attempts = 0;
    } else if (ctx->phase == BDB_JOIN_PARENT) {
        if (zdo_runtime_take_result(&ctx->zdo, &which, &result) == ZDO_RUNTIME_OK) {
            if (!result && which == ZDO_RUNTIME_PARENT && ctx->zdo.parent_known &&
                (ctx->zdo.parent_information & 2u)) {
                base(ctx, 0xfffcu, 0x0036u);
                ctx->packet.payload[1] = 180; ctx->packet.payload[2] = 1; ctx->packet.length = 3;
                if (nwk_aps_queue(&ctx->transport, &ctx->packet, 0, ctx->last) != NWK_APS_OK)
                    fail(ctx, BDB_JOIN_TRANSMIT_FAILED);
                else ctx->phase = BDB_JOIN_PERMIT;
            } else if (result != ZDO_RUNTIME_TIMEOUT || ctx->attempts == BDB_JOIN_ATTEMPTS)
                fail(ctx, BDB_JOIN_PARENT_FAILED);
        }
        if (ctx->phase == BDB_JOIN_PARENT && !ctx->zdo.query) {
            rc = zdo_runtime_request(&ctx->zdo, &ctx->transport, ZDO_RUNTIME_PARENT, ctx->last);
            if (rc == ZDO_RUNTIME_OK) ctx->attempts++;
            else if (rc != ZDO_RUNTIME_FULL) fail(ctx, BDB_JOIN_PARENT_FAILED);
        }
    } else if (ctx->phase == BDB_JOIN_PERMIT && finished(ctx, &result)) {
        if (result || !ctx->transport.ready) fail(ctx, BDB_JOIN_TRANSMIT_FAILED);
        else { ctx->phase = BDB_JOIN_READY; ctx->keepalive = ctx->last+BDB_JOIN_KEEPALIVE; }
    } else if (ctx->phase == BDB_JOIN_READY) {
        if (zdo_runtime_take_result(&ctx->zdo, &which, &result) == ZDO_RUNTIME_OK) {
            if (which != ZDO_RUNTIME_PARENT || result || !(ctx->zdo.parent_information & 2u))
                fail(ctx, BDB_JOIN_PARENT_FAILED);
            else ctx->keepalive = ctx->last+BDB_JOIN_KEEPALIVE;
        }
        if (ctx->phase == BDB_JOIN_READY && expired(ctx->last, ctx->keepalive) && !ctx->zdo.query &&
            zdo_runtime_request(&ctx->zdo, &ctx->transport, ZDO_RUNTIME_PARENT, ctx->last) != ZDO_RUNTIME_OK &&
            expired(ctx->last, ctx->keepalive+BDB_JOIN_EXCHANGE_WAIT)) fail(ctx, BDB_JOIN_PARENT_FAILED);
    }
    if (ctx->phase == BDB_JOIN_WAIT_KEY && expired(ctx->last, ctx->until)) fail(ctx, BDB_JOIN_KEY_TIMEOUT);
    if (((ctx->phase == BDB_JOIN_REQUESTING && !ctx->got_tc) ||
         (ctx->phase == BDB_JOIN_VERIFYING && !ctx->got_confirm)) &&
        ctx->issued && expired(ctx->last, ctx->until)) fail(ctx, BDB_JOIN_TC_FAILED);
    if ((ctx->phase == BDB_JOIN_WAIT_TC || ctx->phase == BDB_JOIN_WAIT_CONFIRM) && expired(ctx->last, ctx->until)) {
        if (ctx->attempts == BDB_JOIN_ATTEMPTS) fail(ctx, BDB_JOIN_TC_FAILED);
        else { ctx->phase = ctx->phase == BDB_JOIN_WAIT_TC ? BDB_JOIN_REQUESTING : BDB_JOIN_VERIFYING; ctx->issued = 0; }
    }
}

bdb_join_result_t bdb_join_step(bdb_join_t BDB_JOIN_RAM *ctx, uint32_t now,
    const bdb_join_event_t BDB_JOIN_RAM *event, bdb_join_action_t BDB_JOIN_RAM *action)
{
    bdb_join_result_t result;
    nwk_aps_result_t transported;
    uint8_t outcome, handled = 0;
    if (!action) return BDB_JOIN_ARGUMENT;
    result = advance(ctx, now);
    if (result) return result;
    if (!ctx->owner || ctx->phase == BDB_JOIN_IDLE) return BDB_JOIN_STATE;
    memset(action, 0, sizeof(*action));
    if (ctx->phase == BDB_JOIN_SCANNING) {
        if (event && event->kind != BDB_JOIN_EVENT_SCAN) return BDB_JOIN_IGNORED;
        if (mac_scan_step(&ctx->scan, ctx->owner, now, event ? &event->scan : NULL, &action->scan) != MAC_SCAN_OK)
            return BDB_JOIN_STATE;
        if (action->scan.kind) action->kind = BDB_JOIN_ACTION_SCAN;
        if (ctx->scan.phase == MAC_SCAN_FAULT) { ctx->phase = BDB_JOIN_FAULT; ctx->result = BDB_JOIN_SCAN_FAILED; }
        if (ctx->scan.phase == MAC_SCAN_DONE) {
            result = scanned(ctx);
            if (mac_scan_release(&ctx->scan, ctx->owner) != MAC_SCAN_OK) return BDB_JOIN_STATE;
            if (result) { ctx->phase = BDB_JOIN_FAILED; ctx->result = (uint8_t)result; }
            else if (mac_join_start(&ctx->association, ctx->owner, &ctx->config.association, now) != MAC_JOIN_OK) {
                ctx->phase = BDB_JOIN_FAILED; ctx->result = BDB_JOIN_ASSOCIATION_FAILED;
            } else { ctx->phase = BDB_JOIN_ASSOCIATING; ctx->attempts = 1; }
        }
        return BDB_JOIN_OK;
    }
    if (ctx->phase == BDB_JOIN_ASSOCIATING) {
        if (event && event->kind != BDB_JOIN_EVENT_ASSOCIATION) return BDB_JOIN_IGNORED;
        if (mac_join_step(&ctx->association, ctx->owner, now, event ? &event->association : NULL,
            &action->association) != MAC_JOIN_OK) return BDB_JOIN_STATE;
        if (action->association.kind) action->kind = BDB_JOIN_ACTION_ASSOCIATION;
        if (ctx->association.phase == MAC_JOIN_FAULT) {
            ctx->phase = BDB_JOIN_FAULT; ctx->result = BDB_JOIN_ASSOCIATION_FAILED;
        }
        if (ctx->association.phase == MAC_JOIN_DONE) {
            if (mac_join_take(&ctx->association, &ctx->record) != MAC_JOIN_OK ||
                mac_join_release(&ctx->association, ctx->owner) != MAC_JOIN_OK) return BDB_JOIN_STATE;
            if (ctx->record.association.outcome != MAC_ASSOCIATION_RESPONSE || ctx->record.association.status ||
                ctx->record.association.address_kind != MAC_ASSOCIATION_ALLOCATED ||
                ctx->record.association.source_relation != MAC_ASSOCIATION_SOURCE_MATCHED ||
                ctx->record.reason || ctx->record.cleanup_error) {
                if (ctx->attempts == BDB_JOIN_ATTEMPTS ||
                    mac_join_start(&ctx->association, ctx->owner, &ctx->config.association, now) != MAC_JOIN_OK) {
                    ctx->phase = BDB_JOIN_FAILED; ctx->result = BDB_JOIN_ASSOCIATION_FAILED;
                } else ctx->attempts++;
            } else if (security_keys_associate(ctx->record.association.short_address, ctx->config.nv_polls) != SECURITY_KEYS_OK)
                fail(ctx, BDB_JOIN_SECURITY);
            else {
                ctx->member = 0; ctx->commission_until = now+6250000UL;
                install(ctx);
            }
        }
        return BDB_JOIN_OK;
    }
    if (ctx->phase == BDB_JOIN_INSTALLING) {
        if (security_keys_status(&keys) != SECURITY_KEYS_OK) { fail(ctx, BDB_JOIN_SECURITY); return BDB_JOIN_OK; }
        if (event && event->kind == BDB_JOIN_EVENT_INSTALLED && event->epoch == ctx->epoch &&
            event->token == ctx->token && ctx->issued && !expired(now, ctx->until) &&
            event->installed.pan == keys.config.pan && event->installed.address == keys.config.address &&
            event->installed.channel == keys.config.channel &&
            !memcmp(event->installed.own_ieee, keys.config.own_ieee, 8)) {
            if (ctx->member) {
                if (keys.phase != SECURITY_KEYS_VERIFIED || !ctx->transport.announced || !ctx->transport.permit_sent)
                    fail(ctx, BDB_JOIN_SECURITY);
                else { ctx->phase = BDB_JOIN_READY; ctx->transport.ready = 1; }
            } else {
                ctx->phase = BDB_JOIN_WAIT_KEY;
                ctx->until = ctx->record.association.stamp+BDB_JOIN_SECURITY_WAIT;
            }
        } else if (expired(now, ctx->until)) {
            ctx->phase = BDB_JOIN_FAULT; ctx->result = BDB_JOIN_INSTALL_FAILED;
        } else if (!ctx->issued) {
            action->kind = BDB_JOIN_ACTION_INSTALL; action->epoch = ctx->epoch;
            action->token = ctx->token; action->install = keys.config; ctx->issued = 1;
        }
        return BDB_JOIN_OK;
    }
    if (ctx->phase == BDB_JOIN_FAILED || ctx->phase == BDB_JOIN_FAULT) return BDB_JOIN_STATE;
    if (ctx->phase >= BDB_JOIN_WAIT_KEY && ctx->phase < BDB_JOIN_READY &&
        (++ctx->steps > 4096u || expired(now, ctx->commission_until)))
        fail(ctx, BDB_JOIN_KEY_TIMEOUT);
    if (event && event->kind != BDB_JOIN_EVENT_TX) return BDB_JOIN_IGNORED;
    if (ctx->phase < BDB_JOIN_ABORTING && ctx->transport.receive_ready) {
        runtime(ctx); handled = 1;
    }
    transported = nwk_aps_step(&ctx->transport, now, event ? &event->tx : NULL, &action->tx);
    if (action->tx.kind) action->kind = BDB_JOIN_ACTION_TX;
    if (ctx->owner->phase == MAC_TX_FAULT) {
        ctx->phase = BDB_JOIN_FAULT; ctx->result = BDB_JOIN_TRANSMIT_FAILED;
        ctx->transport.ready = ctx->member = 0;
        return BDB_JOIN_OK;
    }
    if (transported != NWK_APS_OK && transported != NWK_APS_IGNORED) {
        if (ctx->phase == BDB_JOIN_LEAVING) {
            ctx->cleanup_error = (uint8_t)transported; ctx->phase = BDB_JOIN_FAILED;
            return BDB_JOIN_OK;
        }
        if (ctx->phase < BDB_JOIN_ABORTING) fail(ctx, BDB_JOIN_TRANSMIT_FAILED);
        else ctx->cleanup_error = (uint8_t)transported;
    }
    if (ctx->phase < BDB_JOIN_ABORTING && !handled) runtime(ctx);
    if (ctx->phase == BDB_JOIN_UPDATING)
        (void)zdo_runtime_take_result(&ctx->zdo, &outcome, &ctx->cleanup_error);
    if (ctx->phase == BDB_JOIN_UPDATING && !ctx->transport.active && !ctx->transport.queued &&
        !ctx->transport.reply && !ctx->transport.completed) install(ctx);
    if (ctx->phase == BDB_JOIN_ABORTING && !ctx->transport.active && !ctx->transport.queued && !ctx->transport.reply) {
        if (ctx->transport.completed) {
            if (!finished(ctx, &outcome)) return BDB_JOIN_STATE;
            if (ctx->application_pending) {
                ctx->application_result = outcome; ctx->application_pending = 0; ctx->application_done = 1;
            }
        }
        if (security_keys_status(&keys) != SECURITY_KEYS_OK || keys.phase == SECURITY_KEYS_LEFT) ctx->phase = BDB_JOIN_FAILED;
        else if (nwk_aps_key_exchange(&ctx->transport, 3, now) != NWK_APS_OK) {
            ctx->phase = BDB_JOIN_FAILED; ctx->cleanup_error = BDB_JOIN_SECURITY;
        } else ctx->phase = BDB_JOIN_LEAVING;
    } else if (ctx->phase == BDB_JOIN_LEAVING && finished(ctx, &outcome)) {
        ctx->cleanup_error = outcome; ctx->phase = BDB_JOIN_FAILED;
    }
    return BDB_JOIN_OK;
}

bdb_join_result_t bdb_join_receive(bdb_join_t BDB_JOIN_RAM *ctx,
    const uint8_t *body, uint16_t length, uint8_t crc_valid, uint32_t now)
{
    bdb_join_result_t result = advance(ctx, now);
    nwk_aps_result_t received;
    if (result) return result;
    if (!body || crc_valid > 1) return BDB_JOIN_ARGUMENT;
    if (!crc_valid) return BDB_JOIN_MALFORMED;
    if (ctx->phase < BDB_JOIN_WAIT_KEY || ctx->phase >= BDB_JOIN_UPDATING) return BDB_JOIN_STATE;
    if (security_keys_status(&keys) != SECURITY_KEYS_OK) return BDB_JOIN_SECURITY;
    if (mac_frame_decode(body, length, &mac) != MAC_CODEC_OK || mac.header.type != MAC_FRAME_DATA ||
        mac.header.destination_mode != MAC_ADDRESS_SHORT || mac.header.destination_pan != keys.config.pan ||
        mac.header.source_pan != keys.config.pan ||
        !((mac.header.source_mode == MAC_ADDRESS_SHORT && !mac.header.source[0] && !mac.header.source[1]) ||
          (mac.header.source_mode == MAC_ADDRESS_EXTENDED && !memcmp(mac.header.source, keys.config.tc_ieee, 8))) ||
        !((mac.header.destination[0] == (uint8_t)keys.config.address &&
           mac.header.destination[1] == (uint8_t)(keys.config.address >> 8)) ||
          (mac.header.destination[0] == 255 && mac.header.destination[1] == 255)))
        return BDB_JOIN_MALFORMED;
    if (ed_wire_nwk(body+mac.payload_offset, mac.payload_length, &nwk) != ZIGBEE_SECURITY_OK)
        return BDB_JOIN_MALFORMED;
    if (mac.header.destination[0] == 255 && mac.header.destination[1] == 255 && nwk.header.destination < 0xfffbu)
        return BDB_JOIN_MALFORMED;
    received = nwk_aps_receive(&ctx->transport, body+mac.payload_offset, mac.payload_length, now);
    if (received == NWK_APS_OK) return BDB_JOIN_OK;
    if (received == NWK_APS_FULL) return BDB_JOIN_FULL;
    if (received == NWK_APS_DUPLICATE || received == NWK_APS_IGNORED) return BDB_JOIN_IGNORED;
    if (ctx->transport.error == SECURITY_KEYS_STORAGE || ctx->transport.error == SECURITY_KEYS_CRYPTO ||
        ctx->transport.error == SECURITY_KEYS_EXHAUSTED) fail(ctx, BDB_JOIN_SECURITY);
    return BDB_JOIN_SECURITY;
}

bdb_join_result_t bdb_join_send(bdb_join_t BDB_JOIN_RAM *ctx,
    const ed_packet_t *packet, uint8_t aps_secure, uint32_t now)
{
    nwk_aps_result_t result;
    if (!ctx || ctx->version != 1 || !packet) return BDB_JOIN_ARGUMENT;
    if (ctx->phase != BDB_JOIN_READY || !ctx->transport.ready) return BDB_JOIN_STATE;
    if (ctx->application_pending || ctx->application_done || ctx->zdo.query || ctx->zdo.response_tx)
        return BDB_JOIN_FULL;
    if (packet->nwk.type || packet->aps.type || packet->aps.source_endpoint != ctx->config.endpoint ||
        packet->aps.profile_id != ctx->config.profile) return BDB_JOIN_ARGUMENT;
    result = nwk_aps_queue(&ctx->transport, packet, aps_secure, now);
    if (result != NWK_APS_OK) return result == NWK_APS_FULL ? BDB_JOIN_FULL : BDB_JOIN_TRANSMIT_FAILED;
    ctx->application_pending = 1;
    return BDB_JOIN_OK;
}

bdb_join_result_t bdb_join_confirm(bdb_join_t BDB_JOIN_RAM *ctx, uint8_t *result)
{
    if (!ctx || ctx->version != 1 || !result) return BDB_JOIN_ARGUMENT;
    if (!ctx->application_done) return BDB_JOIN_STATE;
    *result = ctx->application_result; ctx->application_done = 0;
    return BDB_JOIN_OK;
}
