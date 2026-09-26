/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "mac_scan.h"
#include <stddef.h>
#include <string.h>

/* IEEE 2006 7.3.7: source-less, uncompressed short/PAN broadcast, no ACK.
 * mac_tx_submit validates through the actual codec and replaces byte2 by DSN.
 */
static const uint8_t beacon_request[8] = {3, 8, 0, 255, 255, 255, 255, 7};

#if defined(CC2530_MAC_LINK)
#define ENGINE(tx) (&(tx)->engine)
#define SUBMIT mac_tx_interval_submit
#define RELEASE mac_tx_interval_release
#else
#define ENGINE(tx) (tx)
#define SUBMIT mac_tx_submit
#define RELEASE mac_tx_release
#endif

static uint8_t reached(uint32_t now, uint32_t at)
{
    return (uint32_t)(now - at) < MAC_TX_HALF;
}

static uint8_t valid(const mac_scan_t MAC_SCAN_RAM * volatile scan)
{
    return scan->version == MAC_SCAN_VERSION && scan->phase <= MAC_SCAN_FAULT;
}

static uint8_t radio_state(const mac_scan_radio_state_t MAC_SCAN_RAM * volatile state)
{
    return state->channel >= 11u && state->channel <= 26u
        && state->filter <= MAC_SCAN_FILTER_BEACONS && state->rx_on <= 1u;
}

static uint8_t scan_state(const mac_scan_radio_state_t MAC_SCAN_RAM * volatile state,
                          uint8_t channel, uint8_t rx)
{
    return state->pan == 0xffffu && state->channel == channel
        && state->filter == MAC_SCAN_FILTER_BEACONS && state->rx_on == rx;
}

static void stop(mac_scan_t MAC_SCAN_RAM * volatile scan, uint8_t reason, uint32_t now)
{
    if (!scan->reason)
        scan->reason = reason;
    if (reason == MAC_SCAN_ADAPTER_ERROR || reason == MAC_SCAN_CLOSE_MISSING)
        scan->uncertain = 1;
    if (!scan->stopping) {
        scan->stopping = 1;
        scan->deadline = now + MAC_SCAN_STOP_SYMBOLS;
        scan->stop_steps = MAC_SCAN_STOP_STEPS;
    }
    if (scan->phase != MAC_SCAN_TX) {
        scan->phase = MAC_SCAN_RESTORE;
        scan->issued = 0;
    }
}

static void fault(mac_scan_t MAC_SCAN_RAM * volatile scan, uint8_t reason)
{
    if (!scan->reason)
        scan->reason = reason;
    else if (!scan->cleanup_error)
        scan->cleanup_error = reason;
    scan->uncertain = 1;
    scan->phase = MAC_SCAN_FAULT;
}

static void next_channel(mac_scan_t MAC_SCAN_RAM * volatile scan, uint32_t now)
{
    volatile uint32_t bit = 0;
    if (!scan->remaining) {
        stop(scan, MAC_SCAN_FINISHED, now);
        return;
    }
    for (scan->channel = 11; scan->channel <= 26; scan->channel++) {
        bit = UINT32_C(1) << scan->channel;
        if (scan->remaining & bit)
            break;
    }
    bit = scan->remaining & ~bit;
    scan->remaining = bit;
    scan->phase = MAC_SCAN_CONFIG;
    scan->issued = 0;
}

mac_scan_result_t mac_scan_init(mac_scan_t MAC_SCAN_RAM * volatile scan)
{
    if (scan == NULL)
        return MAC_SCAN_INVALID;
    memset(scan, 0, sizeof(*scan));
    scan->version = MAC_SCAN_VERSION;
    return MAC_SCAN_OK;
}

mac_scan_result_t mac_scan_start(mac_scan_t MAC_SCAN_RAM * volatile scan,
                                 mac_scan_tx_t MAC_SCAN_RAM * volatile tx,
                                 const mac_scan_request_t MAC_SCAN_RAM * volatile request,
                                 uint32_t volatile now)
{
    volatile uint32_t dwell;
    if (scan == NULL || tx == NULL || request == NULL
            || !request->channels || (request->channels & ~NWK_CANDIDATES_CHANNEL_MASK)
            || request->duration > 14u || !request->work || request->work > MAC_SCAN_MAX_WORK
            || !request->lifetime
            || request->lifetime > MAC_SCAN_MAX_LIFETIME || !radio_state(&request->saved))
        return MAC_SCAN_INVALID;
    if (!valid(scan) || scan->phase != MAC_SCAN_IDLE || ENGINE(tx)->phase != MAC_TX_IDLE)
        return MAC_SCAN_STATE;
    if ((uint32_t)(now - ENGINE(tx)->last) >= MAC_TX_HALF)
        return MAC_SCAN_INVALID;
    if (scan->generation == UINT32_MAX || ENGINE(tx)->generation == UINT32_MAX)
        return MAC_SCAN_EXHAUSTED;
    /* All validation precedes mutation. This cannot fail for the checked mask. */
    (void)nwk_candidates_init(&scan->candidates, request->channels);
    scan->owner = tx;
    scan->saved = request->saved;
    scan->generation++;
    scan->tx_generation = ENGINE(tx)->generation;
    scan->last = now;
    scan->deadline = now + request->lifetime;
    dwell = UINT32_C(960) * ((UINT32_C(1) << request->duration) + 1u);
    scan->dwell = dwell;
    scan->steps = request->work;
    scan->remaining = request->channels;
    scan->unscanned = request->channels;
    scan->sent = 0;
    scan->reason = 0;
    scan->cleanup_error = 0;
    scan->uncertain = 0;
    scan->overflow = 0;
    scan->token = 0;
    scan->stopping = 0;
    scan->tx_outcome = 0;
    scan->phase = MAC_SCAN_CONFIG;
    next_channel(scan, now);
    return MAC_SCAN_OK;
}

mac_scan_result_t mac_scan_step(mac_scan_t MAC_SCAN_RAM * volatile scan,
                                mac_scan_tx_t MAC_SCAN_RAM * volatile tx,
                                uint32_t volatile now,
                                const mac_scan_event_t MAC_SCAN_RAM * volatile event,
                                mac_scan_action_t MAC_SCAN_RAM * volatile action)
{
    volatile uint8_t kind, candidate;
    volatile uint32_t bits;

    if (scan == NULL || tx == NULL || action == NULL
            || (event != NULL && (event->kind < MAC_SCAN_EVENT_CONFIGURED
                || event->kind > MAC_SCAN_EVENT_CANCEL
                || (event->kind == MAC_SCAN_EVENT_BEACON
                    && (event->body == NULL || event->crc_valid > 1u))
                || (event->kind == MAC_SCAN_EVENT_TX
                    && event->tx_result > MAC_TX_GENERATION_EXHAUSTED))))
        return MAC_SCAN_INVALID;
    if (!valid(scan) || scan->phase == MAC_SCAN_IDLE || scan->owner != tx)
        return MAC_SCAN_STATE;
    memset(action, 0, sizeof(*action));
    if (scan->phase >= MAC_SCAN_DONE)
        goto publish;
    if ((uint32_t)(now - scan->last) >= MAC_TX_HALF) {
        fault(scan, MAC_SCAN_CLOCK_ERROR);
        goto publish;
    }
    kind = 0;
    if (event != NULL && event->generation == scan->generation
            && event->token == scan->token
            && (
#if defined(CC2530_MAC_LINK)
                /* CLOSED is the saved pre-stop coverage watermark, not the
                 * delivery clock. Draining after stop can advance last. */
                event->kind == MAC_SCAN_EVENT_CLOSED ||
#endif
                (uint32_t)(event->stamp - scan->last) < MAC_TX_HALF)
            && (uint32_t)(now - event->stamp) < MAC_TX_HALF)
        kind = event->kind;
    scan->last = now;
    if (ENGINE(tx)->generation != scan->tx_generation
            || (scan->phase != MAC_SCAN_TX && ENGINE(tx)->phase != MAC_TX_IDLE)) {
        fault(scan, MAC_SCAN_TX_ERROR);
        goto publish;
    }
    if (scan->stopping) {
        if (reached(now, scan->deadline) || !scan->stop_steps
                || kind == MAC_SCAN_EVENT_FAILURE) {
            fault(scan, MAC_SCAN_CLEANUP_FAILED);
            goto publish;
        }
        scan->stop_steps--;
    } else if (kind == MAC_SCAN_EVENT_CANCEL || kind == MAC_SCAN_EVENT_FAILURE
            || reached(now, scan->deadline) || !scan->steps) {
        if (kind == MAC_SCAN_EVENT_FAILURE)
            scan->uncertain = 1;
        stop(scan, kind == MAC_SCAN_EVENT_CANCEL ? MAC_SCAN_CANCELLED
             : kind == MAC_SCAN_EVENT_FAILURE ? MAC_SCAN_ADAPTER_ERROR
             : !scan->steps ? MAC_SCAN_WORK_LIMIT : MAC_SCAN_LIFETIME, now);
    } else
        scan->steps--;

    if (scan->phase == MAC_SCAN_CONFIG) {
        if (!scan->issued) {
            scan->issued = 1;
            scan->token++;
            action->kind = MAC_SCAN_ACTION_CONFIG;
        } else if (kind == MAC_SCAN_EVENT_CONFIGURED) {
            if (!scan_state(&event->state, scan->channel, 0))
                stop(scan, MAC_SCAN_ADAPTER_ERROR, now);
            else
                scan->phase = MAC_SCAN_SUBMIT;
        }
    } else if (scan->phase == MAC_SCAN_SUBMIT) {
        if (SUBMIT(tx, beacon_request, sizeof(beacon_request), now,
                          MAC_SCAN_TX_SYMBOLS, MAC_SCAN_TX_STEPS) != MAC_TX_OK)
            stop(scan, MAC_SCAN_TX_ERROR, now);
        else {
            scan->tx_generation = ENGINE(tx)->generation;
            scan->phase = MAC_SCAN_TX;
            scan->token++;
            scan->issued = 0;
        }
    } else if (scan->phase == MAC_SCAN_TX) {
        if (scan->issued && kind == MAC_SCAN_EVENT_TX) {
            scan->issued = 0;
            scan->tx_outcome = ENGINE(tx)->outcome;
            if (ENGINE(tx)->transmissions) {
                bits = UINT32_C(1) << scan->channel;
                bits |= scan->sent;
                scan->sent = bits;
            }
            if (ENGINE(tx)->uncertain)
                scan->uncertain = 1;
            if (event->tx_result != MAC_TX_OK || ENGINE(tx)->phase == MAC_TX_FAULT
                    || ENGINE(tx)->phase == MAC_TX_IDLE || ENGINE(tx)->last != event->stamp) {
                fault(scan, MAC_SCAN_TX_ERROR);
                goto publish;
            }
            if (ENGINE(tx)->phase == MAC_TX_DONE) {
                (void)RELEASE(tx); /* Exact DONE precondition checked above. */
                scan->phase = MAC_SCAN_OPEN;
                if (scan->stopping)
                    stop(scan, scan->reason, now);
                else if (scan->tx_outcome == MAC_TX_CHANNEL_ACCESS)
                    next_channel(scan, now);
                else if (scan->tx_outcome != MAC_TX_UNACKNOWLEDGED)
                    stop(scan, MAC_SCAN_TX_ERROR, now);
            }
        }
        if (scan->phase == MAC_SCAN_TX && !scan->issued) {
            scan->issued = 1;
            scan->token++;
            action->kind = MAC_SCAN_ACTION_TX;
            action->tx_cancel = scan->stopping && ENGINE(tx)->phase != MAC_TX_STOPPING
                && ENGINE(tx)->phase != MAC_TX_DONE && ENGINE(tx)->phase != MAC_TX_FAULT;
        }
    } else if (scan->phase == MAC_SCAN_OPEN) {
        if (!scan->issued) {
            scan->issued = 1;
            scan->token++;
            action->kind = MAC_SCAN_ACTION_RECEIVE;
        } else if (kind == MAC_SCAN_EVENT_OPENED) {
            if (!scan_state(&event->state, scan->channel, 1))
                stop(scan, MAC_SCAN_ADAPTER_ERROR, now);
            else {
                scan->window_start = event->stamp;
                bits = event->stamp;
                bits += scan->dwell;
                scan->window_end = bits;
                scan->phase = MAC_SCAN_RX;
            }
        }
    } else if (scan->phase == MAC_SCAN_RX) {
        if (kind == MAC_SCAN_EVENT_CLOSED) {
#if defined(CC2530_MAC_LINK)
            if (!reached(event->stamp, scan->window_end)
#else
            if (event->stamp != scan->window_end
#endif
                    || !scan_state(&event->state, scan->channel, 0))
                stop(scan, MAC_SCAN_ADAPTER_ERROR, now);
            else {
                bits = ~(UINT32_C(1) << scan->channel);
                bits &= scan->unscanned;
                scan->unscanned = bits;
                next_channel(scan, now);
            }
        } else if (kind == MAC_SCAN_EVENT_BEACON
#if defined(CC2530_MAC_LINK)
                && event->state.channel == scan->channel) {
#else
                && event->state.channel == scan->channel
                && (uint32_t)(event->stamp - scan->window_start) < scan->dwell) {
#endif
            candidate = nwk_candidates_consider(&scan->candidates, scan->channel,
                                                event->crc_valid, event->body, event->length);
            action->observed = 1;
            action->candidate_result = candidate;
            if (candidate == NWK_CANDIDATES_FULL)
                scan->overflow = 1;
        }
        if (scan->phase == MAC_SCAN_RX && reached(now, scan->window_end + MAC_SCAN_CLOSE_GRACE))
            stop(scan, MAC_SCAN_CLOSE_MISSING, now);
    }
    /* Cleanup is distinct from logical termination and cancels/drains every
     * outstanding configuration/RX action before restoring the exact snapshot.
     */
    if (scan->phase == MAC_SCAN_RESTORE) {
        if (!scan->issued) {
            scan->issued = 1;
            scan->token++;
            action->kind = MAC_SCAN_ACTION_RESTORE;
        } else if (kind == MAC_SCAN_EVENT_RESTORED) {
            if (event->state.pan != scan->saved.pan
                    || event->state.channel != scan->saved.channel
                    || event->state.filter != scan->saved.filter
                    || event->state.rx_on != scan->saved.rx_on)
                fault(scan, MAC_SCAN_CLEANUP_FAILED);
            else
                scan->phase = MAC_SCAN_DONE;
        }
    }
publish:
    action->generation = scan->generation;
    action->token = scan->token;
    action->until = scan->deadline;
    action->dwell = scan->dwell;
    action->state.pan = 0xffffu;
    action->state.channel = scan->channel;
    action->state.filter = MAC_SCAN_FILTER_BEACONS;
    action->state.rx_on = action->kind == MAC_SCAN_ACTION_RECEIVE;
    if (action->kind == MAC_SCAN_ACTION_RESTORE)
        action->state = scan->saved;
    return MAC_SCAN_OK;
}

mac_scan_result_t mac_scan_get(const mac_scan_t MAC_SCAN_RAM * volatile scan, uint8_t index,
                               nwk_candidate_t MAC_SCAN_RAM * volatile result)
{
    if (scan == NULL || result == NULL)
        return MAC_SCAN_INVALID;
    if (!valid(scan) || scan->phase < MAC_SCAN_DONE)
        return MAC_SCAN_STATE;
    if (nwk_candidates_get(&scan->candidates, index, result) != NWK_CANDIDATES_OK)
        return MAC_SCAN_BAD_INDEX;
    return MAC_SCAN_OK;
}

mac_scan_result_t mac_scan_release(mac_scan_t MAC_SCAN_RAM * volatile scan,
                                   mac_scan_tx_t MAC_SCAN_RAM * volatile tx)
{
    if (scan == NULL || tx == NULL)
        return MAC_SCAN_INVALID;
    if (!valid(scan) || scan->phase != MAC_SCAN_DONE || scan->owner != tx
            || ENGINE(tx)->phase != MAC_TX_IDLE || ENGINE(tx)->generation != scan->tx_generation)
        return MAC_SCAN_STATE;
    scan->phase = MAC_SCAN_IDLE;
    return MAC_SCAN_OK;
}
