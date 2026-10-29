/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "bdb_join_internal.h"
#include "sdcc_volatile.h"
#include <stddef.h>
#include <string.h>
#if defined(CC2530_RX_DIAG)
#include "cc2530_rx_diag.h"
#endif
#if defined(__clang__)
#define MCS51_PARAM_VOLATILE
#else
#define MCS51_PARAM_VOLATILE volatile
#endif
#if defined(CC2530_CLANG)
#define BDB_RECOVERY_CODE __attribute__((section(".bank7.text.bdb")))
#else
#define BDB_RECOVERY_CODE
#endif


BDB_JOIN_KEYS_STORAGE security_keys_status_t bdb_join_keys;
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

bdb_join_result_t bdb_join_advance(bdb_join_t BDB_JOIN_RAM * SDCC_VOLATILE ctx, SDCC_VOLATILE uint32_t now)
{
    if (!ctx || ctx->version != BDB_JOIN_VERSION) return BDB_JOIN_ARGUMENT;
    if ((uint32_t)(now-ctx->last) >= MAC_TX_HALF) return BDB_JOIN_CLOCK;
    ctx->last = now;
    return BDB_JOIN_OK;
}

static void fail(bdb_join_t BDB_JOIN_RAM * SDCC_VOLATILE ctx, SDCC_VOLATILE uint8_t result)
{
#if defined(CC2530_RX_DIAG)
    if (!RX_DIAG.fail_valid) {
        RX_DIAG.fail_valid = 1;
        RX_DIAG.fail_bdb_phase = ctx->phase;
        RX_DIAG.fail_result = result;
        RX_DIAG.fail_workspace = ctx->workspace;
        if (ctx->workspace == BDB_JOIN_WORK_RUNTIME) {
            RX_DIAG.fail_aps_result = ctx->work.runtime.transport.result;
            RX_DIAG.fail_aps_retries = ctx->work.runtime.transport.retries;
            RX_DIAG.fail_aps_error = ctx->work.runtime.transport.error;
        }
    }
#endif
    ctx->abandon = result == BDB_JOIN_ADDRESS_CONFLICT ||
        (result != BDB_JOIN_WORK_LIMIT && ctx->phase < BDB_JOIN_READY && !ctx->recovery &&
         !(ctx->phase == BDB_JOIN_INSTALLING && ctx->member));
    ctx->result = result; ctx->member = ctx->work.runtime.transport.ready = 0;
    ctx->phase = BDB_JOIN_ABORTING;
    if (nwk_aps_stop(&ctx->work.runtime.transport, ctx->last) != NWK_APS_OK) {
        ctx->cleanup_error = BDB_JOIN_TRANSMIT_FAILED; ctx->phase = BDB_JOIN_FAULT;
    }
}

static void scan_result(bdb_join_t BDB_JOIN_RAM * SDCC_VOLATILE ctx)
{
    ctx->scan_result.generation = ctx->work.scan.generation;
    ctx->scan_result.sent = ctx->work.scan.sent;
    ctx->scan_result.unscanned = ctx->work.scan.unscanned;
    ctx->scan_result.reason = ctx->work.scan.reason;
    ctx->scan_result.cleanup_error = ctx->work.scan.cleanup_error;
    ctx->scan_result.overflow = ctx->work.scan.overflow;
    ctx->scan_result.tx_outcome = ctx->work.scan.tx_outcome;
    ctx->scan_result.candidates = ctx->work.scan.candidates.count;
#if defined(CC2530_MAC_LINK)
    ctx->scan_result.lossy = ctx->work.scan.lossy;
#endif
}

static bdb_join_result_t scanned(bdb_join_t BDB_JOIN_RAM * SDCC_VOLATILE ctx)
{
    uint8_t i;
    memset(&work.policy, 0, sizeof(work.policy));
#if defined(CC2530_PARENT_RECOVERY)
    if (ctx->recovery) {
        if (security_keys_status(&bdb_join_keys) != SECURITY_KEYS_OK) return BDB_JOIN_SECURITY;
        if (expired(ctx->last, ctx->commission_until) ||
            ctx->work.scan.overflow || ctx->work.scan.unscanned || !bdb_join_keys.router_known)
            return BDB_JOIN_NO_PARENT;
        memcpy(work.policy.extended_pan_id, bdb_join_keys.config.extended_pan, 8);
        work.policy.minimum_known = 1; work.policy.minimum_update_id = bdb_join_keys.config.update_id;
        for (i = 0; i < ctx->work.scan.candidates.count; i++) {
            const nwk_candidate_t *c = &ctx->work.scan.candidates.entries[i];
            work.policy.link_cost[i] = ctx->config.link_cost;
            if (c->pan_id != bdb_join_keys.config.pan || c->channel != bdb_join_keys.config.channel)
                continue; /* bounded same-PAN/channel known-router slice */
            if ((c->address_mode == MAC_ADDRESS_SHORT &&
                 c->coordinator[0] == (uint8_t)bdb_join_keys.router_address &&
                 c->coordinator[1] == (uint8_t)(bdb_join_keys.router_address >> 8)) ||
                (c->address_mode == MAC_ADDRESS_EXTENDED &&
                 !memcmp(c->coordinator, bdb_join_keys.router_ieee, 8)))
                work.policy.potential_mask |= (uint8_t)(1u << i);
        }
        return ctx->work.scan.reason == MAC_SCAN_FINISHED &&
            nwk_parent_select(&ctx->work.scan.candidates, &work.policy, &ctx->parent) == NWK_PARENT_OK ?
            BDB_JOIN_OK : BDB_JOIN_NO_PARENT;
    }
#endif
#if !defined(CC2530_DEFAULT_TC_KEY)
    if (security_keys_status(&bdb_join_keys) != SECURITY_KEYS_OK) return BDB_JOIN_SECURITY;
    memcpy(work.policy.extended_pan_id, bdb_join_keys.config.extended_pan, 8);
    work.policy.minimum_known = 1; work.policy.minimum_update_id = bdb_join_keys.config.update_id;
#else
    if (ctx->work.scan.overflow || ctx->work.scan.unscanned) return BDB_JOIN_NO_PARENT;
#endif
    for (i = 0; i < ctx->work.scan.candidates.count; i++) {
        const nwk_candidate_t * SDCC_VOLATILE c = &ctx->work.scan.candidates.entries[i];
        work.policy.link_cost[i] = ctx->config.link_cost;
#if defined(CC2530_DEFAULT_TC_KEY)
        if (!(c->superframe & MAC_SUPERFRAME_PAN_COORDINATOR) ||
            (c->address_mode == MAC_ADDRESS_SHORT && (c->coordinator[0] || c->coordinator[1])))
            continue;
        if (work.policy.potential_mask &&
            memcmp(work.policy.extended_pan_id, c->network.extended_pan_id, 8))
            return BDB_JOIN_NO_PARENT;
        memcpy(work.policy.extended_pan_id, c->network.extended_pan_id, 8);
        work.policy.potential_mask |= 1u << i;
#else
        if (c->address_mode == MAC_ADDRESS_EXTENDED && c->pan_id == bdb_join_keys.config.pan &&
            c->channel == bdb_join_keys.config.channel && c->network.update_id == bdb_join_keys.config.update_id &&
            (c->superframe & MAC_SUPERFRAME_PAN_COORDINATOR) &&
            !memcmp(c->coordinator, bdb_join_keys.config.tc_ieee, 8)) work.policy.potential_mask |= 1u << i;
#endif
    }
    if (ctx->work.scan.reason != MAC_SCAN_FINISHED ||
        nwk_parent_select(&ctx->work.scan.candidates, &work.policy, &ctx->parent) != NWK_PARENT_OK)
        return BDB_JOIN_NO_PARENT;
#if defined(CC2530_DEFAULT_TC_KEY)
    ctx->config.association.extraction.pan = ctx->parent.candidate.pan_id;
    ctx->config.association.extraction.channel = ctx->parent.candidate.channel;
    ctx->config.association.extraction.coordinator_mode = ctx->parent.candidate.address_mode;
    memcpy(ctx->config.association.extraction.coordinator, ctx->parent.candidate.coordinator, 8);
#endif
    return BDB_JOIN_OK;
}

static void install(bdb_join_t BDB_JOIN_RAM * SDCC_VOLATILE ctx)
{
    if (ctx->token == 0xffffu) { fail(ctx, BDB_JOIN_INSTALL_FAILED); return; }
    ctx->token++; ctx->issued = 0; ctx->phase = BDB_JOIN_INSTALLING;
    ctx->until = ctx->last+BDB_JOIN_EXCHANGE_WAIT;
}

static uint8_t finished(bdb_join_t BDB_JOIN_RAM * SDCC_VOLATILE ctx, uint8_t * SDCC_VOLATILE result)
{
    return nwk_aps_confirm(&ctx->work.runtime.transport, result) == NWK_APS_OK;
}

/* R22 3.6.10.3: the keepalive method follows nwkParentInformation. A polling
 * (RX-off-when-idle) child may rely on MAC Data Poll keepalive (bit 0); the
 * End Device Timeout Request keepalive is bit 1. */
static uint8_t poll_keepalive(bdb_join_t BDB_JOIN_RAM * MCS51_PARAM_VOLATILE ctx) BDB_RECOVERY_CODE;
static uint8_t poll_keepalive(bdb_join_t BDB_JOIN_RAM * MCS51_PARAM_VOLATILE ctx)
{
    return ctx->work.runtime.zdo.parent_known && (ctx->work.runtime.zdo.parent_information & 3u) == 1u &&
        !(ctx->config.association.capability & 0x08u);
}

static void runtime(bdb_join_t BDB_JOIN_RAM * MCS51_PARAM_VOLATILE ctx) BDB_RECOVERY_CODE;
static void runtime(bdb_join_t BDB_JOIN_RAM * MCS51_PARAM_VOLATILE ctx)
{
    MCU_ESCAPED_LOCAL uint8_t which, result;
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
        if (!ctx->timely) { fail(ctx, BDB_JOIN_TC_FAILED); return; }
        if (ctx->work.runtime.zdo.security_event == SECURITY_KEYS_EVENT_TC_KEY) ctx->got_tc = 1;
        else ctx->got_confirm = 1;
    }
    if (ctx->application_pending && ctx->work.runtime.transport.completed) {
        if (!finished(ctx, &ctx->application_result)) { fail(ctx, BDB_JOIN_TRANSMIT_FAILED); return; }
        ctx->application_pending = 0; ctx->application_done = 1;
    }
    if (ctx->phase == BDB_JOIN_WAIT_KEY && ctx->work.runtime.zdo.security_event == SECURITY_KEYS_EVENT_NETWORK_KEY) {
        if (!ctx->timely) { fail(ctx, BDB_JOIN_KEY_TIMEOUT); return; }
        ctx->member = 1;
        if (zdo_runtime_broadcast(&ctx->work.runtime.zdo, &ctx->work.runtime.transport,
            ZDO_RUNTIME_BROADCAST_ANNOUNCE, ctx->last) != ZDO_RUNTIME_OK)
            fail(ctx, BDB_JOIN_TRANSMIT_FAILED);
        else ctx->phase = BDB_JOIN_ANNOUNCING;
    } else if (ctx->phase == BDB_JOIN_ANNOUNCING && finished(ctx, &result)) {
        if (result) fail(ctx, BDB_JOIN_TRANSMIT_FAILED);
        else { ctx->phase = ctx->recovery ? BDB_JOIN_PARENT : BDB_JOIN_NODE; ctx->attempts = 0; }
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
                ((ctx->work.runtime.zdo.parent_information & 2u) || poll_keepalive(ctx))) {
                if (ctx->recovery) {
                    if (!ctx->work.runtime.transport.announced) fail(ctx, BDB_JOIN_TRANSMIT_FAILED);
                    else {
                        ctx->phase = BDB_JOIN_READY; ctx->work.runtime.transport.ready = 1;
                        ctx->attempts = 0; ctx->recovery = 3;
                        ctx->keepalive = ctx->last+BDB_JOIN_KEEPALIVE;
                    }
                } else if (zdo_runtime_broadcast(&ctx->work.runtime.zdo, &ctx->work.runtime.transport,
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
    } else if (ctx->phase == BDB_JOIN_RECOVERING) {
        if (ctx->work.runtime.zdo.security_event == SECURITY_KEYS_EVENT_REJOIN_REFUSED) {
            fail(ctx, BDB_JOIN_REJOIN_REFUSED); return;
        }
        /* State-driven: success needs the REJOINED event, VERIFIED durable
         * keys and no outstanding request, so late responses are safe. */
        if (ctx->work.runtime.zdo.security_event == SECURITY_KEYS_EVENT_REJOINED) ctx->recovery = 2;
        if (ctx->issued == 1 && finished(ctx, &result)) {
            if (!result) {
                ctx->issued = 2; ctx->until = ctx->last+BDB_JOIN_REJOIN_WAIT;
            } else ctx->issued = 0;
        }
        if (ctx->issued != 1) {
            if (security_keys_status(&bdb_join_keys) != SECURITY_KEYS_OK) fail(ctx, BDB_JOIN_SECURITY);
            else if (ctx->recovery == 2 && bdb_join_keys.phase == SECURITY_KEYS_VERIFIED) {
                ctx->member = 1; install(ctx);
            } else if (!ctx->issued || expired(ctx->last, ctx->until)) {
                if (ctx->attempts == BDB_JOIN_ATTEMPTS) fail(ctx, BDB_JOIN_NO_PARENT);
                else {
                    nwk_aps_result_t queued = nwk_aps_rejoin(&ctx->work.runtime.transport,
                        ctx->config.association.capability, ctx->last);
                    if (queued == NWK_APS_OK) { ctx->issued = 1; ctx->attempts++; }
                    else if (queued != NWK_APS_FULL) fail(ctx, BDB_JOIN_TRANSMIT_FAILED);
                }
            }
        }
    } else if (ctx->phase == BDB_JOIN_READY) {
        if (zdo_runtime_take_result(&ctx->work.runtime.zdo, &which, &result) == ZDO_RUNTIME_OK) {
            if (which != ZDO_RUNTIME_PARENT ||
                (!result && !(ctx->work.runtime.zdo.parent_information & 2u) && !poll_keepalive(ctx)) ||
                (result && result != ZDO_RUNTIME_TIMEOUT && result != ZDO_RUNTIME_TRANSMIT) ||
                (result && ctx->attempts == BDB_JOIN_KEEPALIVE_ATTEMPTS))
                fail(ctx, BDB_JOIN_PARENT_FAILED);
            else if (result) ctx->keepalive = ctx->last+BDB_JOIN_KEEPALIVE_RETRY;
            else { ctx->attempts = 0; ctx->keepalive = ctx->last+BDB_JOIN_KEEPALIVE; }
        }
        if (ctx->phase == BDB_JOIN_READY && !poll_keepalive(ctx) && expired(ctx->last, ctx->keepalive) &&
            !ctx->work.runtime.zdo.query) {
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

/* Indirect-delivery service for an RX-off-when-idle capability. The receiver
 * itself stays on; only the parent's queueing behaviour requires the poll. */
static void poll(bdb_join_t BDB_JOIN_RAM * MCS51_PARAM_VOLATILE ctx, MCS51_PARAM_VOLATILE uint32_t now) BDB_RECOVERY_CODE;
static void poll(bdb_join_t BDB_JOIN_RAM * MCS51_PARAM_VOLATILE ctx, MCS51_PARAM_VOLATILE uint32_t now)
{
    nwk_aps_t BDB_JOIN_RAM * MCS51_PARAM_VOLATILE t = &ctx->work.runtime.transport;
    uint32_t period;
    if ((ctx->config.association.capability & 0x08u) ||
        !((ctx->phase >= BDB_JOIN_WAIT_KEY && ctx->phase <= BDB_JOIN_READY) || ctx->phase == BDB_JOIN_RECOVERING)) {
        ctx->poll_phase = ctx->phase; return;
    }
    if (ctx->phase == BDB_JOIN_READY && ctx->poll_phase != BDB_JOIN_READY) ctx->poll_fast = now+BDB_JOIN_POLL_BURST;
    ctx->poll_phase = ctx->phase;
    if (expired(now, ctx->poll_fast)) ctx->poll_fast = now;
    period = ctx->phase != BDB_JOIN_READY || ctx->poll_fast != now || t->queued || t->waiting ||
        t->reply || ctx->work.runtime.zdo.query ? BDB_JOIN_POLL_FAST : BDB_JOIN_POLL_SLOW;
    /* A deadline further than the slowest interval is stale storage. A slow
     * deadline set while idle must not outlive the APS ACK wait of new work. */
    if ((uint32_t)(ctx->poll_at-now) > BDB_JOIN_POLL_SLOW) ctx->poll_at = now;
    else if ((uint32_t)(ctx->poll_at-now) > period) ctx->poll_at = now+period;
    if (!expired(now, ctx->poll_at) || t->poll || t->active_poll || t->stopping) return;
    if (nwk_aps_poll(t, ctx->phase == BDB_JOIN_RECOVERING, now) != NWK_APS_OK) return;
    ctx->poll_at = now+period;
}

bdb_join_result_t bdb_join_step(bdb_join_t BDB_JOIN_RAM * MCS51_PARAM_VOLATILE ctx, MCS51_PARAM_VOLATILE uint32_t now,
    const bdb_join_event_t BDB_JOIN_RAM * MCS51_PARAM_VOLATILE event,
    bdb_join_action_t BDB_JOIN_RAM * MCS51_PARAM_VOLATILE action) BDB_RECOVERY_CODE;

bdb_join_result_t bdb_join_step(bdb_join_t BDB_JOIN_RAM * MCS51_PARAM_VOLATILE ctx, MCS51_PARAM_VOLATILE uint32_t now,
    const bdb_join_event_t BDB_JOIN_RAM * MCS51_PARAM_VOLATILE event, bdb_join_action_t BDB_JOIN_RAM * MCS51_PARAM_VOLATILE action)
{
    bdb_join_result_t result;
    nwk_aps_result_t transported;
    MCU_ESCAPED_LOCAL uint8_t outcome, which;
    MCS51_PARAM_VOLATILE uint8_t handled = 0, ignored = 0, pending;
    if (!action) return BDB_JOIN_ARGUMENT;
    result = bdb_join_advance(ctx, now);
    if (result) return result;
    if (!ctx->owner || ctx->phase == BDB_JOIN_IDLE) return BDB_JOIN_STATE;
    memset(action, 0, sizeof(*action));
    if (ctx->phase == BDB_JOIN_FAILED || ctx->phase == BDB_JOIN_FAULT) return BDB_JOIN_STATE;
    if (ctx->workspace != (ctx->phase == BDB_JOIN_SCANNING ? BDB_JOIN_WORK_SCAN :
        ctx->phase == BDB_JOIN_ASSOCIATING ? BDB_JOIN_WORK_ASSOCIATION : BDB_JOIN_WORK_RUNTIME))
        return BDB_JOIN_STATE;
    if (ctx->phase >= BDB_JOIN_INSTALLING && BDB_JOIN_LIVE(ctx->phase)) {
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
            /* A complete, loss-free scan without any eligible coordinator is
             * usually one lost beacon: rescan the same channels. Ambiguous,
             * closed, overflowed or unscanned results still fail at once. */
            if (result == BDB_JOIN_NO_PARENT &&
#if defined(CC2530_PARENT_RECOVERY)
                !ctx->recovery &&
#endif
                ctx->scan_result.reason == MAC_SCAN_FINISHED &&
                !ctx->scan_result.overflow && !ctx->scan_result.unscanned &&
                !work.policy.potential_mask && ++ctx->attempts < BDB_JOIN_ATTEMPTS) {
                ctx->workspace = BDB_JOIN_WORK_NONE;
                if (mac_scan_init(&ctx->work.scan) != MAC_SCAN_OK) return BDB_JOIN_STATE;
                ctx->workspace = BDB_JOIN_WORK_SCAN;
                if (mac_scan_start(&ctx->work.scan, ctx->owner, &ctx->config.scan, now) == MAC_SCAN_OK)
                    return BDB_JOIN_OK;
                result = BDB_JOIN_SCAN_FAILED;
            }
            if (result) { ctx->phase = BDB_JOIN_FAILED; ctx->result = (uint8_t)result; }
            else {
#if defined(CC2530_PARENT_RECOVERY)
                if (ctx->recovery) {
                    if (security_keys_select_parent(1, ctx->parent.candidate.network.update_id,
                        ctx->config.transport.nv_polls) != SECURITY_KEYS_OK ||
                        bdb_join_runtime_init(ctx, ctx->owner, &ctx->config, now) != BDB_JOIN_OK) {
                        ctx->phase = BDB_JOIN_FAILED; ctx->result = BDB_JOIN_SECURITY;
                    } else {
                        ctx->parent_search = 2; ctx->phase = BDB_JOIN_RECOVERING;
                        ctx->attempts = ctx->issued = 0;
                    }
                    return BDB_JOIN_OK;
                }
#endif
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
#if defined(CC2530_DEFAULT_TC_KEY)
                ctx->record.association.source_relation !=
                    (ctx->config.association.extraction.coordinator_mode == MAC_ADDRESS_SHORT ?
                     MAC_ASSOCIATION_SOURCE_UNBOUND : MAC_ASSOCIATION_SOURCE_MATCHED) ||
#else
                ctx->record.association.source_relation != MAC_ASSOCIATION_SOURCE_MATCHED ||
#endif
                ctx->record.reason || ctx->record.cleanup_error) {
                if (ctx->attempts == BDB_JOIN_ATTEMPTS ||
                    mac_join_start(&ctx->work.association.context, ctx->owner, &ctx->config.association, now) != MAC_JOIN_OK) {
                    ctx->phase = BDB_JOIN_FAILED; ctx->result = BDB_JOIN_ASSOCIATION_FAILED;
                } else ctx->attempts++;
            } else {
                /* take copied the full outcome/stamp outside the union;
                 * release confirmed restoration and the unchanged MAC owner.
                 * Runtime initialization cannot reset its DSN/generation/IFS. */
                /* The parent sends Transport Key within milliseconds; LG
                 * hardware needs ~1.6 s for the NV writes. INSTALL first
                 * (AUTOACK RX on the new address), persist on INSTALLED. */
                if (bdb_join_runtime_init(ctx, ctx->owner, &ctx->config, now) != BDB_JOIN_OK) {
                    ctx->phase = BDB_JOIN_FAILED; ctx->result = BDB_JOIN_STATE;
                } else {
                    ctx->member = 0; ctx->commission_until = now+6250000UL;
                    install(ctx);
                }
            }
        }
        return BDB_JOIN_OK;
    }
    if (ctx->phase == BDB_JOIN_INSTALLING) {
        if (security_keys_status(&bdb_join_keys) != SECURITY_KEYS_OK) { fail(ctx, BDB_JOIN_SECURITY); return BDB_JOIN_OK; }
        /* After association: not yet persisted, install the RAM identity. */
        pending = 0;
        if (!ctx->member && bdb_join_keys.phase < SECURITY_KEYS_ASSOCIATED) {
            pending = 1;
#if defined(CC2530_DEFAULT_TC_KEY)
            bdb_join_discovered_config(ctx);
#endif
            bdb_join_keys.config.address = ctx->record.association.short_address;
        }
        if (event && event->kind == BDB_JOIN_EVENT_INSTALLED && event->epoch == ctx->epoch &&
            event->token == ctx->token && ctx->issued && !expired(now, ctx->until) &&
            event->data.installed.pan == bdb_join_keys.config.pan && event->data.installed.address == bdb_join_keys.config.address &&
            event->data.installed.channel == bdb_join_keys.config.channel &&
            !memcmp(event->data.installed.own_ieee, bdb_join_keys.config.own_ieee, 8)) {
            if (pending) {
                /* Persist now; RX is already open on the new address. */
#if defined(CC2530_DEFAULT_TC_KEY)
                if (bdb_join_provision_discovered(ctx) != BDB_JOIN_OK) pending = 2;
                else
#endif
                if (security_keys_associate(ctx->record.association.short_address, ctx->config.transport.nv_polls) != SECURITY_KEYS_OK ||
                    security_keys_status(&bdb_join_keys) != SECURITY_KEYS_OK)
                    pending = 2;
                else if (bdb_join_keys.phase != SECURITY_KEYS_ASSOCIATED ||
                    memcmp(&event->data.installed, &bdb_join_keys.config, sizeof(bdb_join_keys.config)))
                    pending = 2;
            }
            if (pending == 2) {
                fail(ctx, BDB_JOIN_SECURITY);
            } else if (ctx->recovery == 1) {
                /* The persisted identity is installed; nothing is admitted yet. */
                if (bdb_join_keys.phase != SECURITY_KEYS_VERIFIED && bdb_join_keys.phase != SECURITY_KEYS_REJOINING)
                    fail(ctx, BDB_JOIN_SECURITY);
                else {
#if defined(CC2530_PARENT_RECOVERY)
                    if (ctx->parent_search == 1) {
                        if (expired(now, ctx->commission_until)) {
                            ctx->phase = BDB_JOIN_FAILED; ctx->result = BDB_JOIN_NO_PARENT;
                            return BDB_JOIN_OK;
                        }
                        /* INSTALL has just confirmed this complete saved RX
                         * state; only now is it a truthful scan snapshot. */
                        ctx->config.scan.saved.pan = bdb_join_keys.config.pan;
                        ctx->config.scan.saved.channel = bdb_join_keys.config.channel;
                        ctx->config.scan.saved.filter = MAC_SCAN_FILTER_NORMAL;
                        ctx->config.scan.saved.rx_on = 1;
                        ctx->config.scan.channels = 1UL << bdb_join_keys.config.channel;
                        if (ctx->config.scan.lifetime > ctx->commission_until-now)
                            ctx->config.scan.lifetime = ctx->commission_until-now;
                        ctx->workspace = BDB_JOIN_WORK_NONE;
                        if (mac_scan_init(&ctx->work.scan) != MAC_SCAN_OK ||
                            mac_scan_start(&ctx->work.scan, ctx->owner, &ctx->config.scan, now) != MAC_SCAN_OK ||
                            nwk_candidates_rejoin(&ctx->work.scan.candidates) != NWK_CANDIDATES_OK) {
                            ctx->phase = BDB_JOIN_FAULT; ctx->result = BDB_JOIN_SCAN_FAILED;
                        } else {
                            ctx->workspace = BDB_JOIN_WORK_SCAN; ctx->phase = BDB_JOIN_SCANNING;
                        }
                        return BDB_JOIN_OK;
                    }
#endif
                    ctx->phase = BDB_JOIN_RECOVERING; ctx->issued = ctx->attempts = 0;
                }
            } else if (ctx->recovery == 2) {
                if (bdb_join_keys.phase != SECURITY_KEYS_VERIFIED || !ctx->member ||
                    zdo_runtime_broadcast(&ctx->work.runtime.zdo, &ctx->work.runtime.transport,
                        ZDO_RUNTIME_BROADCAST_ANNOUNCE, now) != ZDO_RUNTIME_OK)
                    fail(ctx, BDB_JOIN_SECURITY);
                else ctx->phase = BDB_JOIN_ANNOUNCING;
            } else if (ctx->member) {
                if (bdb_join_keys.phase != SECURITY_KEYS_VERIFIED || !ctx->work.runtime.transport.announced ||
                    (!ctx->work.runtime.transport.permit_sent && ctx->recovery != 3))
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
    if (((ctx->phase >= BDB_JOIN_WAIT_KEY && ctx->phase < BDB_JOIN_READY) || ctx->phase == BDB_JOIN_RECOVERING) &&
        expired(now, ctx->commission_until))
        fail(ctx, ctx->recovery ? BDB_JOIN_NO_PARENT : BDB_JOIN_KEY_TIMEOUT);
    if (event && event->kind != BDB_JOIN_EVENT_TX) { event = NULL; ignored = 1; }
    poll(ctx, now);
    if (BDB_JOIN_LIVE(ctx->phase) && ctx->work.runtime.transport.receive_ready) {
        runtime(ctx); handled = 1;
    }
    transported = nwk_aps_step(&ctx->work.runtime.transport, now, event ? &event->data.tx : NULL, &action->data.tx);
    if (BDB_JOIN_TX_KIND(action->data.tx)) action->kind = BDB_JOIN_ACTION_TX;
    if (BDB_JOIN_ENGINE(ctx->owner)->phase == MAC_TX_FAULT) {
        ctx->phase = BDB_JOIN_FAULT; ctx->result = BDB_JOIN_TRANSMIT_FAILED;
        ctx->work.runtime.transport.ready = ctx->member = 0;
        return BDB_JOIN_OK;
    }
    if (transported != NWK_APS_OK && transported != NWK_APS_IGNORED && transported != NWK_APS_ACK_FAILED) {
        if (ctx->phase == BDB_JOIN_LEAVING) {
            ctx->cleanup_error = (uint8_t)transported; ctx->phase = BDB_JOIN_FAILED;
            return BDB_JOIN_OK;
        }
        if (BDB_JOIN_LIVE(ctx->phase)) fail(ctx, BDB_JOIN_TRANSMIT_FAILED);
        else ctx->cleanup_error = (uint8_t)transported;
    }
    if (BDB_JOIN_LIVE(ctx->phase) && !handled) runtime(ctx);
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
#if defined(CC2530_PARENT_RECOVERY)
        if (ctx->recovery && !ctx->parent_search &&
            (ctx->result == BDB_JOIN_NO_PARENT || ctx->result == BDB_JOIN_REJOIN_REFUSED) &&
            !expired(now, ctx->commission_until) &&
            security_keys_status(&bdb_join_keys) == SECURITY_KEYS_OK && bdb_join_keys.router_known) {
            ctx->parent_search = 1; ctx->result = BDB_JOIN_OK;
            ctx->work.runtime.transport.ready = 0; install(ctx);
            return BDB_JOIN_OK;
        }
#endif
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

bdb_join_result_t bdb_join_receive(bdb_join_t BDB_JOIN_RAM * SDCC_VOLATILE ctx,
    const uint8_t * SDCC_VOLATILE body, SDCC_VOLATILE uint16_t length, SDCC_VOLATILE uint8_t crc_valid, SDCC_VOLATILE uint32_t now)
{
    bdb_join_result_t result = bdb_join_advance(ctx, now);
    nwk_aps_result_t received;
#if defined(CC2530_PARENT_RECOVERY)
    uint16_t parent_address;
    const uint8_t *parent_ieee;
#endif
    if (result) return result;
    if (!body || crc_valid > 1) return BDB_JOIN_ARGUMENT;
    if (!crc_valid) return BDB_JOIN_MALFORMED;
    if ((ctx->phase < BDB_JOIN_WAIT_KEY || ctx->phase >= BDB_JOIN_UPDATING) && ctx->phase != BDB_JOIN_RECOVERING)
        return BDB_JOIN_STATE;
    if (ctx->workspace != BDB_JOIN_WORK_RUNTIME) return BDB_JOIN_STATE;
    if (security_keys_status(&bdb_join_keys) != SECURITY_KEYS_OK) return BDB_JOIN_SECURITY;
#if defined(CC2530_PARENT_RECOVERY)
    parent_address = ctx->phase == BDB_JOIN_RECOVERING ?
        SECURITY_REJOIN_ADDRESS(bdb_join_keys) : SECURITY_PARENT_ADDRESS(bdb_join_keys);
    parent_ieee = ctx->phase == BDB_JOIN_RECOVERING ?
        SECURITY_REJOIN_IEEE(bdb_join_keys) : SECURITY_PARENT_IEEE(bdb_join_keys);
#endif
    /* Before the first sealed Rejoin Request, restored floors may still admit
     * one replay of a pre-reset frame; RECOVERING consumes nothing until then. */
    if (ctx->phase == BDB_JOIN_RECOVERING && bdb_join_keys.phase != SECURITY_KEYS_REJOINING)
        return BDB_JOIN_IGNORED;
    if (mac_frame_decode(body, length, &work.receive.mac) != MAC_CODEC_OK || work.receive.mac.header.type != MAC_FRAME_DATA ||
        work.receive.mac.header.destination_mode != MAC_ADDRESS_SHORT || work.receive.mac.header.destination_pan != bdb_join_keys.config.pan ||
        work.receive.mac.header.source_pan != bdb_join_keys.config.pan ||
        !((work.receive.mac.header.source_mode == MAC_ADDRESS_SHORT &&
#if defined(CC2530_PARENT_RECOVERY)
           work.receive.mac.header.source[0] == (uint8_t)parent_address &&
           work.receive.mac.header.source[1] == (uint8_t)(parent_address >> 8)
#else
           !work.receive.mac.header.source[0] && !work.receive.mac.header.source[1]
#endif
           ) ||
          (work.receive.mac.header.source_mode == MAC_ADDRESS_EXTENDED && !memcmp(work.receive.mac.header.source,
#if defined(CC2530_PARENT_RECOVERY)
           parent_ieee,
#else
           bdb_join_keys.config.tc_ieee,
#endif
           8))) ||
        !((work.receive.mac.header.destination[0] == (uint8_t)bdb_join_keys.config.address &&
           work.receive.mac.header.destination[1] == (uint8_t)(bdb_join_keys.config.address >> 8)) ||
          (work.receive.mac.header.destination[0] == 255 && work.receive.mac.header.destination[1] == 255)))
        return BDB_JOIN_MALFORMED;
    if (ed_wire_nwk(body+work.receive.mac.payload_offset, work.receive.mac.payload_length, &work.receive.nwk) != ZIGBEE_SECURITY_OK)
        return BDB_JOIN_MALFORMED;
    if (work.receive.mac.header.destination[0] == 255 && work.receive.mac.header.destination[1] == 255 && work.receive.nwk.header.destination < 0xfffbu)
        return BDB_JOIN_MALFORMED;
    received = nwk_aps_receive(&ctx->work.runtime.transport, body+work.receive.mac.payload_offset, work.receive.mac.payload_length, now);
    if (received == NWK_APS_OK) {
        /* Key deadlines judge the frame's arrival, not the end of the durable
         * key save that nwk_aps_receive has just completed. */
        if (ctx->work.runtime.transport.receive_ready) {
            ctx->timely = 1;
            if (expired(now, ctx->until)) ctx->timely = 0;
        }
        /* A unicast may be one of several queued indirect frames. */
        if (work.receive.mac.header.destination[0] != 255 || work.receive.mac.header.destination[1] != 255) {
            ctx->poll_at = now;
            if (expired(now+BDB_JOIN_POLL_ACTIVE, ctx->poll_fast)) ctx->poll_fast = now+BDB_JOIN_POLL_ACTIVE;
        }
        return BDB_JOIN_OK;
    }
    if (received == NWK_APS_FULL) return BDB_JOIN_FULL;
    if (received == NWK_APS_DUPLICATE || received == NWK_APS_IGNORED) return BDB_JOIN_IGNORED;
    if (ctx->work.runtime.transport.error == SECURITY_KEYS_STORAGE || ctx->work.runtime.transport.error == SECURITY_KEYS_CRYPTO ||
        ctx->work.runtime.transport.error == SECURITY_KEYS_EXHAUSTED) fail(ctx, BDB_JOIN_SECURITY);
    return BDB_JOIN_SECURITY;
}

bdb_join_result_t bdb_join_send(bdb_join_t BDB_JOIN_RAM * SDCC_VOLATILE ctx,
    const ed_packet_t * SDCC_VOLATILE packet, SDCC_VOLATILE uint8_t aps_secure, SDCC_VOLATILE uint32_t now)
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

bdb_join_result_t bdb_join_confirm(bdb_join_t BDB_JOIN_RAM * SDCC_VOLATILE ctx, uint8_t * SDCC_VOLATILE result)
{
    if (!ctx || ctx->version != BDB_JOIN_VERSION || !result) return BDB_JOIN_ARGUMENT;
    if (ctx->workspace != BDB_JOIN_WORK_RUNTIME || !ctx->application_done) return BDB_JOIN_STATE;
    *result = ctx->application_result; ctx->application_done = 0;
    return BDB_JOIN_OK;
}
#if defined(CC2530_MAC_LINK)
bdb_join_result_t bdb_join_armed(bdb_join_t BDB_JOIN_RAM * SDCC_VOLATILE ctx,
    uint32_t generation, uint8_t retry, uint8_t nb)
{
    if (!ctx || ctx->version != BDB_JOIN_VERSION) return BDB_JOIN_ARGUMENT;
    if (ctx->workspace != BDB_JOIN_WORK_RUNTIME || ctx->phase < BDB_JOIN_WAIT_KEY ||
        ctx->phase == BDB_JOIN_FAILED || ctx->phase == BDB_JOIN_FAULT) return BDB_JOIN_STATE;
    return nwk_aps_armed(&ctx->work.runtime.transport, generation, retry, nb) == NWK_APS_OK ?
        BDB_JOIN_OK : BDB_JOIN_STATE;
}
bdb_join_result_t bdb_join_disarmed(bdb_join_t BDB_JOIN_RAM * SDCC_VOLATILE ctx,
    uint32_t generation, uint8_t retry, uint8_t nb)
{
    if (!ctx || ctx->version != BDB_JOIN_VERSION) return BDB_JOIN_ARGUMENT;
    if (ctx->workspace != BDB_JOIN_WORK_RUNTIME || ctx->phase < BDB_JOIN_WAIT_KEY ||
        ctx->phase == BDB_JOIN_FAILED || ctx->phase == BDB_JOIN_FAULT) return BDB_JOIN_STATE;
    return nwk_aps_disarmed(&ctx->work.runtime.transport, generation, retry, nb) == NWK_APS_OK ?
        BDB_JOIN_OK : BDB_JOIN_STATE;
}
#endif
