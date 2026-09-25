/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "bdb_join_internal.h"
#include <stddef.h>
#include <string.h>

MCU_XDATA security_keys_status_t bdb_join_keys;
/* Parent selection and runtime reception are separate returning operations.
 * MAC and NWK views remain simultaneously live during destination checks. */
static MCU_XDATA union {
    nwk_parent_policy_t policy;
    struct {
        mac_frame_info_t mac;
        nwk_frame_info_t nwk;
    } receive;
} work;

static uint8_t expired(uint32_t now, uint32_t until)
{
    return (uint32_t)(now-until) < MAC_TX_HALF;
}

bdb_join_result_t bdb_join_advance(bdb_join_t BDB_JOIN_RAM * volatile ctx, volatile uint32_t now)
{
    if (!ctx || ctx->version != BDB_JOIN_VERSION) return BDB_JOIN_ARGUMENT;
    if ((uint32_t)(now-ctx->last) >= MAC_TX_HALF) return BDB_JOIN_CLOCK;
    ctx->last = now;
    return BDB_JOIN_OK;
}

static void fail(bdb_join_t BDB_JOIN_RAM * volatile ctx, volatile uint8_t result)
{
    ctx->abandon = result == BDB_JOIN_ADDRESS_CONFLICT ||
        (result != BDB_JOIN_WORK_LIMIT && ctx->phase < BDB_JOIN_READY &&
         !(ctx->phase == BDB_JOIN_INSTALLING && ctx->member));
    ctx->result = result; ctx->member = ctx->work.runtime.transport.ready = 0;
    ctx->phase = BDB_JOIN_ABORTING;
    if (nwk_aps_stop(&ctx->work.runtime.transport, ctx->last) != NWK_APS_OK) {
        ctx->cleanup_error = BDB_JOIN_TRANSMIT_FAILED; ctx->phase = BDB_JOIN_FAULT;
    }
}

static void scan_result(bdb_join_t BDB_JOIN_RAM * volatile ctx)
{
    ctx->scan_result.generation = ctx->work.scan.generation;
    ctx->scan_result.sent = ctx->work.scan.sent;
    ctx->scan_result.unscanned = ctx->work.scan.unscanned;
    ctx->scan_result.reason = ctx->work.scan.reason;
    ctx->scan_result.cleanup_error = ctx->work.scan.cleanup_error;
    ctx->scan_result.overflow = ctx->work.scan.overflow;
    ctx->scan_result.tx_outcome = ctx->work.scan.tx_outcome;
    ctx->scan_result.candidates = ctx->work.scan.candidates.count;
}

static bdb_join_result_t scanned(bdb_join_t BDB_JOIN_RAM * volatile ctx)
{
    uint8_t i;
    memset(&work.policy, 0, sizeof(work.policy));
    if (security_keys_status(&bdb_join_keys) != SECURITY_KEYS_OK) return BDB_JOIN_SECURITY;
    memcpy(work.policy.extended_pan_id, bdb_join_keys.config.extended_pan, 8);
    work.policy.minimum_known = 1; work.policy.minimum_update_id = bdb_join_keys.config.update_id;
    for (i = 0; i < ctx->work.scan.candidates.count; i++) {
        const nwk_candidate_t * volatile c = &ctx->work.scan.candidates.entries[i];
        work.policy.link_cost[i] = ctx->config.link_cost;
        if (c->address_mode == MAC_ADDRESS_EXTENDED && c->pan_id == bdb_join_keys.config.pan &&
            c->channel == bdb_join_keys.config.channel && c->network.update_id == bdb_join_keys.config.update_id &&
            (c->superframe & MAC_SUPERFRAME_PAN_COORDINATOR) &&
            !memcmp(c->coordinator, bdb_join_keys.config.tc_ieee, 8)) work.policy.potential_mask |= 1u << i;
    }
    if (ctx->work.scan.reason != MAC_SCAN_FINISHED ||
        nwk_parent_select(&ctx->work.scan.candidates, &work.policy, &ctx->parent) != NWK_PARENT_OK)
        return BDB_JOIN_NO_PARENT;
    return BDB_JOIN_OK;
}

static void install(bdb_join_t BDB_JOIN_RAM * volatile ctx)
{
    if (ctx->token == 0xffffu) { fail(ctx, BDB_JOIN_INSTALL_FAILED); return; }
    ctx->token++; ctx->issued = 0; ctx->phase = BDB_JOIN_INSTALLING;
    ctx->until = ctx->last+BDB_JOIN_EXCHANGE_WAIT;
}

static uint8_t finished(bdb_join_t BDB_JOIN_RAM * volatile ctx, uint8_t * volatile result)
{
    return nwk_aps_confirm(&ctx->work.runtime.transport, result) == NWK_APS_OK;
}

static void runtime(bdb_join_t BDB_JOIN_RAM * volatile ctx)
{
    uint8_t which, result;
    zdo_runtime_result_t rc;
    rc = zdo_runtime_step(&ctx->work.runtime.zdo, &ctx->work.runtime.transport, ctx->last);
    ctx->receive_result = (uint8_t)rc;
    if (rc == ZDO_RUNTIME_CONFLICT) { fail(ctx, BDB_JOIN_ADDRESS_CONFLICT); return; }
    if (rc == ZDO_RUNTIME_SECURITY || rc == ZDO_RUNTIME_TRANSMIT) {
        fail(ctx, rc == ZDO_RUNTIME_SECURITY ? BDB_JOIN_SECURITY : BDB_JOIN_TRANSMIT_FAILED); return;
    }
    if (ctx->work.runtime.zdo.security_event == SECURITY_KEYS_EVENT_LEAVE) {
        fail(ctx, BDB_JOIN_REMOTE_LEAVE);
    } else if (ctx->work.runtime.zdo.security_event == SECURITY_KEYS_EVENT_UPDATE) {
        if (ctx->phase != BDB_JOIN_READY) { fail(ctx, BDB_JOIN_RECOVERY_REQUIRED); return; }
        ctx->phase = BDB_JOIN_UPDATING; ctx->work.runtime.transport.ready = 0;
        if (zdo_runtime_cancel(&ctx->work.runtime.zdo, &ctx->work.runtime.transport, ctx->last) != ZDO_RUNTIME_OK ||
            nwk_aps_stop(&ctx->work.runtime.transport, ctx->last) != NWK_APS_OK)
            fail(ctx, BDB_JOIN_TRANSMIT_FAILED);
    } else if (ctx->work.runtime.zdo.security_event == SECURITY_KEYS_EVENT_TC_KEY ||
               ctx->work.runtime.zdo.security_event == SECURITY_KEYS_EVENT_VERIFIED) {
        if (expired(ctx->last, ctx->until)) { fail(ctx, BDB_JOIN_TC_FAILED); return; }
        if (ctx->work.runtime.zdo.security_event == SECURITY_KEYS_EVENT_TC_KEY) ctx->got_tc = 1;
        else ctx->got_confirm = 1;
    }
    if (ctx->application_pending && ctx->work.runtime.transport.completed) {
        if (!finished(ctx, &ctx->application_result)) { fail(ctx, BDB_JOIN_TRANSMIT_FAILED); return; }
        ctx->application_pending = 0; ctx->application_done = 1;
    }
    if (ctx->phase == BDB_JOIN_WAIT_KEY && ctx->work.runtime.zdo.security_event == SECURITY_KEYS_EVENT_NETWORK_KEY) {
        if (expired(ctx->last, ctx->until)) { fail(ctx, BDB_JOIN_KEY_TIMEOUT); return; }
        ctx->member = 1;
        if (zdo_runtime_broadcast(&ctx->work.runtime.zdo, &ctx->work.runtime.transport,
            ZDO_RUNTIME_BROADCAST_ANNOUNCE, ctx->last) != ZDO_RUNTIME_OK)
            fail(ctx, BDB_JOIN_TRANSMIT_FAILED);
        else ctx->phase = BDB_JOIN_ANNOUNCING;
    } else if (ctx->phase == BDB_JOIN_ANNOUNCING && finished(ctx, &result)) {
        if (result) fail(ctx, BDB_JOIN_TRANSMIT_FAILED);
        else { ctx->phase = BDB_JOIN_NODE; ctx->attempts = 0; }
    } else if (ctx->phase == BDB_JOIN_NODE) {
        if (zdo_runtime_take_result(&ctx->work.runtime.zdo, &which, &result) == ZDO_RUNTIME_OK) {
            if (!result && which == ZDO_RUNTIME_NODE && !ctx->work.runtime.zdo.tc.logical_type &&
                ctx->work.runtime.zdo.tc.stack_revision >= 21) {
                ctx->phase = BDB_JOIN_REQUESTING; ctx->issued = ctx->attempts = 0;
            } else if (result != ZDO_RUNTIME_TIMEOUT || ctx->attempts == BDB_JOIN_ATTEMPTS)
                fail(ctx, BDB_JOIN_TC_FAILED);
        }
        if (ctx->phase == BDB_JOIN_NODE && !ctx->work.runtime.zdo.query) {
            rc = zdo_runtime_request(&ctx->work.runtime.zdo, &ctx->work.runtime.transport, ZDO_RUNTIME_NODE, ctx->last);
            if (rc == ZDO_RUNTIME_OK) ctx->attempts++;
            else if (rc != ZDO_RUNTIME_FULL) fail(ctx, BDB_JOIN_TC_FAILED);
        }
    } else if (ctx->phase == BDB_JOIN_REQUESTING || ctx->phase == BDB_JOIN_VERIFYING) {
        if (!ctx->issued) {
            nwk_aps_result_t queued = nwk_aps_key_exchange(&ctx->work.runtime.transport,
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
        if (zdo_runtime_take_result(&ctx->work.runtime.zdo, &which, &result) == ZDO_RUNTIME_OK) {
            if (!result && which == ZDO_RUNTIME_PARENT && ctx->work.runtime.zdo.parent_known &&
                (ctx->work.runtime.zdo.parent_information & 2u)) {
                if (zdo_runtime_broadcast(&ctx->work.runtime.zdo, &ctx->work.runtime.transport,
                    ZDO_RUNTIME_BROADCAST_PERMIT, ctx->last) != ZDO_RUNTIME_OK)
                    fail(ctx, BDB_JOIN_TRANSMIT_FAILED);
                else ctx->phase = BDB_JOIN_PERMIT;
            } else if (result != ZDO_RUNTIME_TIMEOUT || ctx->attempts == BDB_JOIN_ATTEMPTS)
                fail(ctx, BDB_JOIN_PARENT_FAILED);
        }
        if (ctx->phase == BDB_JOIN_PARENT && !ctx->work.runtime.zdo.query) {
            rc = zdo_runtime_request(&ctx->work.runtime.zdo, &ctx->work.runtime.transport, ZDO_RUNTIME_PARENT, ctx->last);
            if (rc == ZDO_RUNTIME_OK) ctx->attempts++;
            else if (rc != ZDO_RUNTIME_FULL) fail(ctx, BDB_JOIN_PARENT_FAILED);
        }
    } else if (ctx->phase == BDB_JOIN_PERMIT && finished(ctx, &result)) {
        if (result || !ctx->work.runtime.transport.ready) fail(ctx, BDB_JOIN_TRANSMIT_FAILED);
        else {
            ctx->phase = BDB_JOIN_READY; ctx->attempts = 0;
            ctx->keepalive = ctx->last+BDB_JOIN_KEEPALIVE;
        }
    } else if (ctx->phase == BDB_JOIN_READY) {
        if (zdo_runtime_take_result(&ctx->work.runtime.zdo, &which, &result) == ZDO_RUNTIME_OK) {
            if (which != ZDO_RUNTIME_PARENT ||
                (!result && !(ctx->work.runtime.zdo.parent_information & 2u)) ||
                (result && result != ZDO_RUNTIME_TIMEOUT && result != ZDO_RUNTIME_TRANSMIT) ||
                (result && ctx->attempts == BDB_JOIN_ATTEMPTS))
                fail(ctx, BDB_JOIN_PARENT_FAILED);
            else if (result) ctx->keepalive = ctx->last;
            else { ctx->attempts = 0; ctx->keepalive = ctx->last+BDB_JOIN_KEEPALIVE; }
        }
        if (ctx->phase == BDB_JOIN_READY && expired(ctx->last, ctx->keepalive) && !ctx->work.runtime.zdo.query) {
            rc = zdo_runtime_request(&ctx->work.runtime.zdo, &ctx->work.runtime.transport, ZDO_RUNTIME_PARENT, ctx->last);
            if (rc == ZDO_RUNTIME_OK) ctx->attempts++;
            else if (rc != ZDO_RUNTIME_FULL || expired(ctx->last, ctx->keepalive+BDB_JOIN_EXCHANGE_WAIT))
                fail(ctx, BDB_JOIN_PARENT_FAILED);
        }
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

bdb_join_result_t bdb_join_step(bdb_join_t BDB_JOIN_RAM * volatile ctx, volatile uint32_t now,
    const bdb_join_event_t BDB_JOIN_RAM * volatile event, bdb_join_action_t BDB_JOIN_RAM * volatile action)
{
    bdb_join_result_t result;
    nwk_aps_result_t transported;
    uint8_t outcome, which;
    volatile uint8_t handled = 0, ignored = 0;
    if (!action) return BDB_JOIN_ARGUMENT;
    result = bdb_join_advance(ctx, now);
    if (result) return result;
    if (!ctx->owner || ctx->phase == BDB_JOIN_IDLE) return BDB_JOIN_STATE;
    memset(action, 0, sizeof(*action));
    if (ctx->phase == BDB_JOIN_FAILED || ctx->phase == BDB_JOIN_FAULT) return BDB_JOIN_STATE;
    if (ctx->workspace != (ctx->phase == BDB_JOIN_SCANNING ? BDB_JOIN_WORK_SCAN :
        ctx->phase == BDB_JOIN_ASSOCIATING ? BDB_JOIN_WORK_ASSOCIATION : BDB_JOIN_WORK_RUNTIME))
        return BDB_JOIN_STATE;
    if (ctx->phase >= BDB_JOIN_INSTALLING && ctx->phase < BDB_JOIN_ABORTING) {
        if (now != ctx->work_at) { ctx->work_at = now; ctx->steps = 0; }
        if (ctx->steps == BDB_JOIN_STALL_STEPS) {
            if (ctx->phase == BDB_JOIN_INSTALLING && ctx->issued) {
                ctx->result = BDB_JOIN_WORK_LIMIT; ctx->phase = BDB_JOIN_FAULT;
                ctx->member = ctx->work.runtime.transport.ready = 0;
                return BDB_JOIN_OK;
            }
            fail(ctx, BDB_JOIN_WORK_LIMIT);
        } else ctx->steps++;
    }
    if (ctx->phase == BDB_JOIN_SCANNING) {
        if (event && event->kind != BDB_JOIN_EVENT_SCAN) return BDB_JOIN_IGNORED;
        if (mac_scan_step(&ctx->work.scan, ctx->owner, now, event ? &event->data.scan : NULL, &action->data.scan) != MAC_SCAN_OK)
            return BDB_JOIN_STATE;
        if (action->data.scan.kind) action->kind = BDB_JOIN_ACTION_SCAN;
        if (ctx->work.scan.phase == MAC_SCAN_FAULT) {
            scan_result(ctx);
            ctx->phase = BDB_JOIN_FAULT; ctx->result = BDB_JOIN_SCAN_FAILED;
        }
        if (ctx->work.scan.phase == MAC_SCAN_DONE) {
            scan_result(ctx);
            result = scanned(ctx);
            if (mac_scan_release(&ctx->work.scan, ctx->owner) != MAC_SCAN_OK) return BDB_JOIN_STATE;
            if (result) { ctx->phase = BDB_JOIN_FAILED; ctx->result = (uint8_t)result; }
            else {
                /* No scan lease remains. No runtime or association was live
                 * in this storage; initialize association once, not per retry. */
                ctx->workspace = BDB_JOIN_WORK_NONE;
                if (mac_join_init(&ctx->work.association.context, now) != MAC_JOIN_OK) return BDB_JOIN_STATE;
                ctx->workspace = BDB_JOIN_WORK_ASSOCIATION;
                if (mac_join_start(&ctx->work.association.context, ctx->owner, &ctx->config.association, now) != MAC_JOIN_OK) {
                    ctx->phase = BDB_JOIN_FAILED; ctx->result = BDB_JOIN_ASSOCIATION_FAILED;
                } else { ctx->phase = BDB_JOIN_ASSOCIATING; ctx->attempts = 1; }
            }
        }
        return BDB_JOIN_OK;
    }
    if (ctx->phase == BDB_JOIN_ASSOCIATING) {
        if (event && event->kind != BDB_JOIN_EVENT_ASSOCIATION) return BDB_JOIN_IGNORED;
        if (mac_join_step(&ctx->work.association.context, ctx->owner, now, event ? &event->data.association : NULL,
            &action->data.association) != MAC_JOIN_OK) return BDB_JOIN_STATE;
        if (action->data.association.kind) action->kind = BDB_JOIN_ACTION_ASSOCIATION;
        if (ctx->work.association.context.phase == MAC_JOIN_FAULT) {
            ctx->phase = BDB_JOIN_FAULT; ctx->result = BDB_JOIN_ASSOCIATION_FAILED;
        }
        if (ctx->work.association.context.phase == MAC_JOIN_DONE) {
            if (mac_join_take(&ctx->work.association.context, &ctx->record) != MAC_JOIN_OK ||
                mac_join_release(&ctx->work.association.context, ctx->owner) != MAC_JOIN_OK) return BDB_JOIN_STATE;
            if (ctx->record.association.outcome != MAC_ASSOCIATION_RESPONSE || ctx->record.association.status ||
                ctx->record.association.address_kind != MAC_ASSOCIATION_ALLOCATED ||
                ctx->record.association.source_relation != MAC_ASSOCIATION_SOURCE_MATCHED ||
                ctx->record.reason || ctx->record.cleanup_error) {
                if (ctx->attempts == BDB_JOIN_ATTEMPTS ||
                    mac_join_start(&ctx->work.association.context, ctx->owner, &ctx->config.association, now) != MAC_JOIN_OK) {
                    ctx->phase = BDB_JOIN_FAILED; ctx->result = BDB_JOIN_ASSOCIATION_FAILED;
                } else ctx->attempts++;
            } else {
                /* take copied the full outcome/stamp outside the union;
                 * release confirmed restoration and the unchanged MAC owner.
                 * Runtime initialization cannot reset its DSN/generation/IFS. */
                if (bdb_join_runtime_init(ctx, ctx->owner, &ctx->config, now) != BDB_JOIN_OK) {
                    ctx->phase = BDB_JOIN_FAILED; ctx->result = BDB_JOIN_STATE;
                } else if (security_keys_associate(ctx->record.association.short_address, ctx->config.transport.nv_polls) != SECURITY_KEYS_OK)
                    fail(ctx, BDB_JOIN_SECURITY);
                else {
                    ctx->member = 0; ctx->commission_until = now+6250000UL;
                    install(ctx);
                }
            }
        }
        return BDB_JOIN_OK;
    }
    if (ctx->phase == BDB_JOIN_INSTALLING) {
        if (security_keys_status(&bdb_join_keys) != SECURITY_KEYS_OK) { fail(ctx, BDB_JOIN_SECURITY); return BDB_JOIN_OK; }
        if (event && event->kind == BDB_JOIN_EVENT_INSTALLED && event->epoch == ctx->epoch &&
            event->token == ctx->token && ctx->issued && !expired(now, ctx->until) &&
            event->data.installed.pan == bdb_join_keys.config.pan && event->data.installed.address == bdb_join_keys.config.address &&
            event->data.installed.channel == bdb_join_keys.config.channel &&
            !memcmp(event->data.installed.own_ieee, bdb_join_keys.config.own_ieee, 8)) {
            if (ctx->member) {
                if (bdb_join_keys.phase != SECURITY_KEYS_VERIFIED || !ctx->work.runtime.transport.announced ||
                    !ctx->work.runtime.transport.permit_sent)
                    fail(ctx, BDB_JOIN_SECURITY);
                else {
                    ctx->phase = BDB_JOIN_READY; ctx->work.runtime.transport.ready = 1; ctx->attempts = 0;
                    ctx->keepalive = now+BDB_JOIN_KEEPALIVE;
                }
            } else {
                ctx->phase = BDB_JOIN_WAIT_KEY;
                ctx->until = ctx->record.association.stamp+BDB_JOIN_SECURITY_WAIT;
            }
        } else if (expired(now, ctx->until)) {
            ctx->phase = BDB_JOIN_FAULT; ctx->result = BDB_JOIN_INSTALL_FAILED;
        } else if (!ctx->issued) {
            action->kind = BDB_JOIN_ACTION_INSTALL; action->epoch = ctx->epoch;
            action->token = ctx->token; action->data.install = bdb_join_keys.config; ctx->issued = 1;
        }
        return BDB_JOIN_OK;
    }
    if (ctx->phase >= BDB_JOIN_WAIT_KEY && ctx->phase < BDB_JOIN_READY &&
        expired(now, ctx->commission_until))
        fail(ctx, BDB_JOIN_KEY_TIMEOUT);
    if (event && event->kind != BDB_JOIN_EVENT_TX) { event = NULL; ignored = 1; }
    if (ctx->phase < BDB_JOIN_ABORTING && ctx->work.runtime.transport.receive_ready) {
        runtime(ctx); handled = 1;
    }
    transported = nwk_aps_step(&ctx->work.runtime.transport, now, event ? &event->data.tx : NULL, &action->data.tx);
    if (action->data.tx.kind) action->kind = BDB_JOIN_ACTION_TX;
    if (ctx->owner->phase == MAC_TX_FAULT) {
        ctx->phase = BDB_JOIN_FAULT; ctx->result = BDB_JOIN_TRANSMIT_FAILED;
        ctx->work.runtime.transport.ready = ctx->member = 0;
        return BDB_JOIN_OK;
    }
    if (transported != NWK_APS_OK && transported != NWK_APS_IGNORED && transported != NWK_APS_ACK_FAILED) {
        if (ctx->phase == BDB_JOIN_LEAVING) {
            ctx->cleanup_error = (uint8_t)transported; ctx->phase = BDB_JOIN_FAILED;
            return BDB_JOIN_OK;
        }
        if (ctx->phase < BDB_JOIN_ABORTING) fail(ctx, BDB_JOIN_TRANSMIT_FAILED);
        else ctx->cleanup_error = (uint8_t)transported;
    }
    if (ctx->phase < BDB_JOIN_ABORTING && !handled) runtime(ctx);
    if (ctx->phase == BDB_JOIN_UPDATING && ctx->work.runtime.zdo.query && ctx->work.runtime.zdo.tx_done &&
        zdo_runtime_take_result(&ctx->work.runtime.zdo, &which, &outcome) != ZDO_RUNTIME_OK)
        fail(ctx, BDB_JOIN_TRANSMIT_FAILED);
    if (ctx->phase == BDB_JOIN_UPDATING && !ctx->work.runtime.transport.active && !ctx->work.runtime.transport.queued &&
        !ctx->work.runtime.transport.reply && !ctx->work.runtime.transport.completed && !ctx->work.runtime.transport.stopping)
        install(ctx);
    if (ctx->phase == BDB_JOIN_ABORTING && !ctx->work.runtime.transport.active && !ctx->work.runtime.transport.queued &&
        !ctx->work.runtime.transport.reply && !ctx->work.runtime.transport.stopping) {
        if (ctx->work.runtime.transport.completed) {
            if (!finished(ctx, &outcome)) return BDB_JOIN_STATE;
            if (ctx->application_pending) {
                ctx->application_result = outcome; ctx->application_pending = 0; ctx->application_done = 1;
            }
        }
        if (!ctx->abandon || security_keys_status(&bdb_join_keys) != SECURITY_KEYS_OK || bdb_join_keys.phase == SECURITY_KEYS_LEFT)
            ctx->phase = BDB_JOIN_FAILED;
        else if (nwk_aps_key_exchange(&ctx->work.runtime.transport, 3, now) != NWK_APS_OK) {
            ctx->phase = BDB_JOIN_FAILED; ctx->cleanup_error = BDB_JOIN_SECURITY;
        } else ctx->phase = BDB_JOIN_LEAVING;
    } else if (ctx->phase == BDB_JOIN_LEAVING && finished(ctx, &outcome)) {
        ctx->cleanup_error = outcome; ctx->phase = BDB_JOIN_FAILED;
    }
    return ignored ? BDB_JOIN_IGNORED : BDB_JOIN_OK;
}

bdb_join_result_t bdb_join_receive(bdb_join_t BDB_JOIN_RAM * volatile ctx,
    const uint8_t * volatile body, volatile uint16_t length, volatile uint8_t crc_valid, volatile uint32_t now)
{
    bdb_join_result_t result = bdb_join_advance(ctx, now);
    nwk_aps_result_t received;
    if (result) return result;
    if (!body || crc_valid > 1) return BDB_JOIN_ARGUMENT;
    if (!crc_valid) return BDB_JOIN_MALFORMED;
    if (ctx->phase < BDB_JOIN_WAIT_KEY || ctx->phase >= BDB_JOIN_UPDATING ||
        ctx->workspace != BDB_JOIN_WORK_RUNTIME) return BDB_JOIN_STATE;
    if (security_keys_status(&bdb_join_keys) != SECURITY_KEYS_OK) return BDB_JOIN_SECURITY;
    if (mac_frame_decode(body, length, &work.receive.mac) != MAC_CODEC_OK || work.receive.mac.header.type != MAC_FRAME_DATA ||
        work.receive.mac.header.destination_mode != MAC_ADDRESS_SHORT || work.receive.mac.header.destination_pan != bdb_join_keys.config.pan ||
        work.receive.mac.header.source_pan != bdb_join_keys.config.pan ||
        !((work.receive.mac.header.source_mode == MAC_ADDRESS_SHORT && !work.receive.mac.header.source[0] && !work.receive.mac.header.source[1]) ||
          (work.receive.mac.header.source_mode == MAC_ADDRESS_EXTENDED && !memcmp(work.receive.mac.header.source, bdb_join_keys.config.tc_ieee, 8))) ||
        !((work.receive.mac.header.destination[0] == (uint8_t)bdb_join_keys.config.address &&
           work.receive.mac.header.destination[1] == (uint8_t)(bdb_join_keys.config.address >> 8)) ||
          (work.receive.mac.header.destination[0] == 255 && work.receive.mac.header.destination[1] == 255)))
        return BDB_JOIN_MALFORMED;
    if (ed_wire_nwk(body+work.receive.mac.payload_offset, work.receive.mac.payload_length, &work.receive.nwk) != ZIGBEE_SECURITY_OK)
        return BDB_JOIN_MALFORMED;
    if (work.receive.mac.header.destination[0] == 255 && work.receive.mac.header.destination[1] == 255 && work.receive.nwk.header.destination < 0xfffbu)
        return BDB_JOIN_MALFORMED;
    received = nwk_aps_receive(&ctx->work.runtime.transport, body+work.receive.mac.payload_offset, work.receive.mac.payload_length, now);
    if (received == NWK_APS_OK) return BDB_JOIN_OK;
    if (received == NWK_APS_FULL) return BDB_JOIN_FULL;
    if (received == NWK_APS_DUPLICATE || received == NWK_APS_IGNORED) return BDB_JOIN_IGNORED;
    if (ctx->work.runtime.transport.error == SECURITY_KEYS_STORAGE || ctx->work.runtime.transport.error == SECURITY_KEYS_CRYPTO ||
        ctx->work.runtime.transport.error == SECURITY_KEYS_EXHAUSTED) fail(ctx, BDB_JOIN_SECURITY);
    return BDB_JOIN_SECURITY;
}

bdb_join_result_t bdb_join_send(bdb_join_t BDB_JOIN_RAM * volatile ctx,
    const ed_packet_t * volatile packet, volatile uint8_t aps_secure, volatile uint32_t now)
{
    nwk_aps_result_t result;
    if (!ctx || ctx->version != BDB_JOIN_VERSION || !packet) return BDB_JOIN_ARGUMENT;
    if (ctx->phase != BDB_JOIN_READY || ctx->workspace != BDB_JOIN_WORK_RUNTIME ||
        !ctx->work.runtime.transport.ready) return BDB_JOIN_STATE;
    if (ctx->application_pending || ctx->application_done || ctx->work.runtime.zdo.query || ctx->work.runtime.zdo.response_tx)
        return BDB_JOIN_FULL;
    if (packet->nwk.type || packet->aps.type || packet->aps.source_endpoint != ctx->config.transport.endpoint ||
        packet->aps.profile_id != ctx->config.transport.profile) return BDB_JOIN_ARGUMENT;
    result = nwk_aps_queue(&ctx->work.runtime.transport, packet, aps_secure, now);
    if (result != NWK_APS_OK) return result == NWK_APS_FULL ? BDB_JOIN_FULL : BDB_JOIN_TRANSMIT_FAILED;
    ctx->application_pending = 1;
    return BDB_JOIN_OK;
}

bdb_join_result_t bdb_join_confirm(bdb_join_t BDB_JOIN_RAM * volatile ctx, uint8_t * volatile result)
{
    if (!ctx || ctx->version != BDB_JOIN_VERSION || !result) return BDB_JOIN_ARGUMENT;
    if (ctx->workspace != BDB_JOIN_WORK_RUNTIME || !ctx->application_done) return BDB_JOIN_STATE;
    *result = ctx->application_result; ctx->application_done = 0;
    return BDB_JOIN_OK;
}
