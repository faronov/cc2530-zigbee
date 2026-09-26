/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "mac_link_driver.h"
#include "timebase.h"
#include <string.h>

static uint8_t reached(uint32_t now, uint32_t at)
{
    return (uint32_t)(now-at) < MAC_TX_HALF;
}

static uint8_t overlaps(const void MCU_XDATA *a, uint16_t n,
                        const void MCU_XDATA *b, uint16_t m)
{
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return x <= y ? y-x < n : x-y < m;
}

static mac_link_driver_result_t fault(mac_link_driver_t MCU_XDATA * volatile d, uint8_t why)
{
    d->fault = why;
    return (mac_link_driver_result_t)why;
}

static uint8_t sample(mac_link_driver_t MCU_XDATA * volatile d)
{
    d->adapter_result = mac_adapter_now(d->timeout, d->limit, &d->clock);
    if (d->adapter_result != MAC_ADAPTER_OK) { fault(d, MAC_LINK_DRIVER_RADIO); return 0; }
    if (!reached(d->clock.symbols, d->now)) { fault(d, MAC_LINK_DRIVER_ORDER); return 0; }
    d->now = d->clock.symbols;
    return 1;
}

static uint8_t consume(mac_link_driver_t MCU_XDATA * volatile d)
{
    d->adapter_result = mac_adapter_consume(mac_adapter_observation()->token);
    if (d->adapter_result != MAC_ADAPTER_OK) { fault(d, MAC_LINK_DRIVER_RADIO); return 0; }
    return 1;
}

static uint8_t consumer(mac_link_driver_t MCU_XDATA * volatile d, uint8_t event)
{
    d->consumer_result = bdb_join_step(d->join, d->now, event ? &d->event : NULL, &d->action);
    d->stage = 0;
    if (d->consumer_result != BDB_JOIN_OK && d->consumer_result != BDB_JOIN_IGNORED) {
        fault(d, MAC_LINK_DRIVER_CONSUMER); return 0;
    }
    return 1;
}

static void scan_event(mac_link_driver_t MCU_XDATA * volatile d, uint8_t kind)
{
    memset(&d->event, 0, sizeof(d->event));
    d->event.kind = BDB_JOIN_EVENT_SCAN;
    d->event.data.scan.kind = kind;
    d->event.data.scan.generation = d->join->work.scan.generation;
    d->event.data.scan.token = d->join->work.scan.token;
    d->event.data.scan.stamp = d->now;
    d->event.data.scan.state = d->action.data.scan.state;
}

static void join_event(mac_link_driver_t MCU_XDATA * volatile d, uint8_t kind)
{
    memset(&d->event, 0, sizeof(d->event));
    d->event.kind = BDB_JOIN_EVENT_ASSOCIATION;
    d->event.data.association.kind = kind;
    d->event.data.association.epoch = d->join->epoch;
    d->event.data.association.generation = d->join->work.association.context.generation;
    d->event.data.association.token = d->action.data.association.token;
    d->event.data.association.stamp = d->now;
}

static uint8_t frame_consumer(mac_link_driver_t MCU_XDATA * volatile d)
{
    volatile MCU_XDATA uint8_t stage = d->stage;
    d->saved_action = d->action;
    if (!consumer(d, 1)) return 0;
    /* A FRAME is not completion of an outstanding one-shot TX/CLOSE grant.
     * The consumer correctly does not reissue it; retain our original copy. */
    if (!d->action.kind) {
        d->action = d->saved_action;
        d->stage = stage;
    }
    return 1;
}

/* Do not poll a consumer past an undelivered original TX pump report. Both
 * granted calls and their reports are completed together at this sampled now.
 */
static uint8_t pump(mac_link_driver_t MCU_XDATA * volatile d)
{
    volatile MCU_XDATA uint8_t result, cancel;
    cancel = d->action.kind == BDB_JOIN_ACTION_SCAN ?
        d->action.data.scan.tx_cancel : d->action.data.association.tx_cancel;
    if (cancel) {
        memset(&d->source, 0, sizeof(d->source));
        d->source.source.kind = MAC_TX_EVENT_CANCEL;
        d->source.source.generation = d->owner->engine.generation;
        d->source.source.retry = d->owner->engine.retries;
        d->source.source.nb = d->owner->engine.nb;
        d->source.source.stamp = d->now;
    }
    result = mac_tx_observed_step(d->owner, d->now,
        d->source.source.kind ? &d->source : NULL, &d->radio);
    if (d->owner->engine.phase == MAC_TX_DONE &&
        mac_adapter_diagnostic()->phase == MAC_ADAPTER_PREPARED) {
        d->adapter_result = mac_adapter_unprepare(d->owner, d->timeout, d->limit);
        if (d->adapter_result != MAC_ADAPTER_OK) { fault(d, MAC_LINK_DRIVER_RADIO); return 0; }
    }
    if (d->action.kind == BDB_JOIN_ACTION_SCAN) {
        scan_event(d, MAC_SCAN_EVENT_TX);
        d->event.data.scan.tx_result = result;
    } else {
        join_event(d, MAC_JOIN_TX);
        d->event.data.association.tx_result = result;
        d->event.data.association.source = d->source;
        /* ACK_INTERVAL is only published by the adapter for a CRC-valid ACK. */
        d->event.data.association.crc_valid = d->source.source.kind == MAC_TX_EVENT_ACK_INTERVAL;
    }
    return consumer(d, 1);
}

static uint8_t received(mac_link_driver_t MCU_XDATA * volatile d)
{
    const mac_adapter_observation_t MCU_XDATA * volatile o = mac_adapter_observation();
    volatile MCU_XDATA uint8_t type = o->frame->length ? o->frame->body[0] & 7u : 255;
    volatile MCU_XDATA uint8_t crc = (o->frame->crc_correlation & 0x80u) != 0;
    if (o->rx_serial != d->rx_serial + 1u || !o->rx_serial) {
        fault(d, MAC_LINK_DRIVER_ORDER); return 0;
    }
    if (o->normal_rx && crc && (type == MAC_FRAME_DATA || type == MAC_FRAME_COMMAND) &&
        (o->frame->body[0] & MAC_FLAG_ACK_REQUEST)) {
        /* IEEE2006 7.5.6.4.2 / 7.5.1.3: 12-symbol turnaround, 22-symbol
         * legacy ACK PPDU (6 PHY + 5 PSDU octets), conservatively long IFS40.
         * This guard is NOT ACK-success evidence; real stop/drain is required.
         * Foreign AR frames may extend this guard harmlessly.
         */
        d->rx_ready_at = MAC_LINK_CEIL(o->tx.upper) + 74UL;
    }
    if (!o->tx.source.kind && d->join->phase == BDB_JOIN_SCANNING &&
        d->join->work.scan.phase == MAC_SCAN_RX && type == MAC_FRAME_BEACON) {
        scan_event(d, MAC_SCAN_EVENT_BEACON);
        d->event.data.scan.state.pan = d->config.pan;
        d->event.data.scan.state.channel = d->config.channel;
        d->event.data.scan.state.filter = MAC_SCAN_FILTER_BEACONS;
        d->event.data.scan.state.rx_on = 1;
        d->event.data.scan.body = o->frame->body;
        d->event.data.scan.length = o->frame->length;
        d->event.data.scan.crc_valid = crc;
        if (!frame_consumer(d)) return 0;
    } else if (!o->tx.source.kind &&
        (type == MAC_FRAME_COMMAND || type == MAC_FRAME_DATA) &&
        d->join->phase >= BDB_JOIN_ASSOCIATING) {
        if (!o->normal_rx) { fault(d, MAC_LINK_DRIVER_COVERAGE); return 0; }
        if (d->join->phase == BDB_JOIN_ASSOCIATING &&
            d->join->work.association.context.phase == MAC_JOIN_EXTRACT) {
            join_event(d, MAC_JOIN_FRAME);
            d->event.data.association.stamp = MAC_LINK_CEIL(o->tx.upper);
            d->event.data.association.serial = o->rx_serial;
            d->event.data.association.body = o->frame->body;
            d->event.data.association.length = o->frame->length;
            d->event.data.association.channel = d->config.channel;
            d->event.data.association.crc_valid = crc;
            if (!frame_consumer(d)) return 0;
        } else if (type == MAC_FRAME_DATA && d->join->workspace == BDB_JOIN_WORK_RUNTIME &&
                   d->join->phase >= BDB_JOIN_WAIT_KEY && d->join->phase < BDB_JOIN_UPDATING) {
            d->consumer_result = bdb_join_receive(d->join, o->frame->body, o->frame->length, crc, d->now);
            if (d->consumer_result == BDB_JOIN_FULL) {
                /* There is no second driver-owned packet slot. Pumping an
                 * unrelated BDB action here could overwrite an unexecuted
                 * grant while the adapter head still blocks prepare. Fail
                 * locally and retain both owners and the original head. */
                fault(d, MAC_LINK_DRIVER_CONSUMER);
                return 0;
            }
        }
    }
    d->rx_serial = o->rx_serial;
    return 1;
}

/* Service one adapter step. source is copied without retimestamping. The
 * delivery remains held until the granted/internal consumer has used it.
 */
static uint8_t observe(mac_link_driver_t MCU_XDATA * volatile d)
{
    const mac_adapter_observation_t MCU_XDATA * volatile o;
    d->adapter_result = mac_adapter_step(d->timeout, d->limit);
    if (d->adapter_result != MAC_ADAPTER_WAIT && d->adapter_result != MAC_ADAPTER_EVENT &&
        d->adapter_result != MAC_ADAPTER_EXPIRED) { fault(d, MAC_LINK_DRIVER_RADIO); return 0; }
    d->clock = mac_adapter_diagnostic()->live;
    if (!reached(d->clock.symbols, d->now)) { fault(d, MAC_LINK_DRIVER_ORDER); return 0; }
    d->now = d->clock.symbols;
    memset(&d->source, 0, sizeof(d->source));
    if (d->adapter_result != MAC_ADAPTER_EVENT) return 1;
    o = mac_adapter_observation();
    if (o->kind == MAC_ADAPTER_RX_EVENT && !received(d)) return 0;
    d->source = o->tx;
    d->closed_through = MAC_LINK_FLOOR(o->through);
    return 1;
}

static uint8_t close_off(mac_link_driver_t MCU_XDATA * volatile d)
{
    volatile MCU_XDATA uint8_t phase = mac_adapter_diagnostic()->phase;
    if (phase == MAC_ADAPTER_OFF && !mac_adapter_diagnostic()->ready &&
        !mac_adapter_diagnostic()->goal) {
        if (!reached(d->now, d->rx_ready_at)) {
            if (!sample(d)) return 0;
            return reached(d->now, d->rx_ready_at);
        }
        return 1;
    }
    if (phase == MAC_ADAPTER_RX && !mac_adapter_diagnostic()->ready) {
        d->adapter_result = mac_adapter_close(NULL);
        if (d->adapter_result != MAC_ADAPTER_OK) fault(d, MAC_LINK_DRIVER_RADIO);
        else d->gaps++;
        return 0;
    }
    if (!observe(d)) return 0;
    if (mac_adapter_diagnostic()->ready) {
        if (d->source.source.kind) { fault(d, MAC_LINK_DRIVER_STATE); return 0; }
        (void)consume(d);
    }
    return 0;
}

static uint8_t prepare(mac_link_driver_t MCU_XDATA * volatile d, uint8_t policy)
{
    if (!close_off(d)) return 0;
    d->adapter_result = mac_adapter_prepare(d->owner, policy, d->timeout, d->limit);
    if (d->adapter_result != MAC_ADAPTER_OK) { fault(d, MAC_LINK_DRIVER_RADIO); return 0; }
    return 1;
}

static uint8_t disarm(mac_link_driver_t MCU_XDATA * volatile d)
{
    volatile MCU_XDATA uint8_t phase = mac_adapter_diagnostic()->phase;
    if (phase == MAC_ADAPTER_PREPARED) {
        d->adapter_result = mac_adapter_unprepare(d->owner, d->timeout, d->limit);
        if (d->adapter_result != MAC_ADAPTER_OK) { fault(d, MAC_LINK_DRIVER_RADIO); return 0; }
    } else if ((phase != MAC_ADAPTER_OFF && phase != MAC_ADAPTER_RX) ||
               mac_adapter_diagnostic()->ready || mac_adapter_diagnostic()->goal) {
        fault(d, MAC_LINK_DRIVER_STATE); return 0;
    }
    return 1;
}

static void configure_action(mac_link_driver_t MCU_XDATA * volatile d)
{
    volatile MCU_XDATA uint8_t kind = d->action.kind, child, rx = 0, restored = 0;
    if (kind == BDB_JOIN_ACTION_INSTALL &&
        memcmp(d->config.ieee, d->action.data.install.own_ieee, 8)) {
        fault(d, MAC_LINK_DRIVER_COVERAGE);
        return;
    }
    if (!close_off(d)) return;
    if (kind == BDB_JOIN_ACTION_SCAN) {
        child = d->action.data.scan.kind;
        d->config.pan = d->action.data.scan.state.pan;
        d->config.channel = d->action.data.scan.state.channel;
        rx = d->action.data.scan.state.rx_on;
        restored = child == MAC_SCAN_ACTION_RESTORE;
    } else if (kind == BDB_JOIN_ACTION_ASSOCIATION) {
        child = d->action.data.association.kind;
        restored = child == MAC_JOIN_ACTION_RESTORE;
        if (restored) {
            d->config.pan = d->action.data.association.state.pan;
            d->config.channel = d->action.data.association.state.channel;
            rx = d->action.data.association.state.rx_on;
        } else {
            d->config.pan = d->join->config.association.extraction.pan;
            d->config.channel = d->join->config.association.extraction.channel;
        }
    } else {
        child = 0;
        d->config.pan = d->action.data.install.pan;
        d->config.channel = d->action.data.install.channel;
        rx = 1;
    }
    d->config.short_address = kind == BDB_JOIN_ACTION_INSTALL ?
        d->action.data.install.address : d->saved_short;
    if (restored && !reached(d->now, d->owner->engine.ready_at)) { (void)sample(d); return; }
    d->adapter_result = mac_adapter_configure(&d->config, d->timeout, d->limit);
    if (d->adapter_result != MAC_ADAPTER_OK) { fault(d, MAC_LINK_DRIVER_RADIO); return; }
    if (rx) {
        d->adapter_result = mac_adapter_open(d->timeout, d->limit, &d->clock);
        if (d->adapter_result != MAC_ADAPTER_OK) { fault(d, MAC_LINK_DRIVER_RADIO); return; }
    }
    if (!sample(d)) return;
    if (kind == BDB_JOIN_ACTION_SCAN) {
        scan_event(d, restored ? MAC_SCAN_EVENT_RESTORED : MAC_SCAN_EVENT_CONFIGURED);
    } else if (kind == BDB_JOIN_ACTION_ASSOCIATION) {
        join_event(d, restored ? MAC_JOIN_RESTORED : MAC_JOIN_PREPARED);
    } else {
        memset(&d->event, 0, sizeof(d->event));
        d->event.kind = BDB_JOIN_EVENT_INSTALLED;
        d->event.epoch = d->action.epoch;
        d->event.token = d->action.token;
        d->event.data.installed = d->action.data.install;
    }
    (void)consumer(d, 1);
}

mac_link_driver_result_t mac_link_driver_init(mac_link_driver_t MCU_XDATA * volatile d,
    bdb_join_t MCU_XDATA * volatile join, const radio_autoack_config_t MCU_XDATA * volatile initial,
    uint32_t timeout, uint16_t limit)
{
    if (!d || !join || !initial || !timeout || timeout >= TIMEBASE_HALF_RANGE || !limit)
        return MAC_LINK_DRIVER_ARGUMENT;
    if (overlaps(d, sizeof(*d), join, sizeof(*join)) ||
        overlaps(d, sizeof(*d), initial, sizeof(*initial)) ||
        (join->owner && (overlaps(d, sizeof(*d), join->owner, sizeof(*join->owner)) ||
          overlaps(join, sizeof(*join), join->owner, sizeof(*join->owner)) ||
          overlaps(initial, sizeof(*initial), join->owner, sizeof(*join->owner)))) ||
        overlaps(initial, sizeof(*initial), join, sizeof(*join)))
        return MAC_LINK_DRIVER_ARGUMENT;
#if defined(__SDCC)
    if ((uint16_t)d >= 0x1e00u || sizeof(*d) > 0x1e00u-(uint16_t)d)
        return MAC_LINK_DRIVER_ARGUMENT;
#endif
    if (d->version) return MAC_LINK_DRIVER_STATE;
    if (join->phase != BDB_JOIN_SCANNING || !join->owner ||
        join->owner->engine.phase != MAC_TX_IDLE || join->config.scan.saved.rx_on ||
        join->config.scan.saved.filter || initial->pan != join->config.scan.saved.pan ||
        initial->channel != join->config.scan.saved.channel ||
        initial->short_address != 0xffffu ||
        memcmp(initial->ieee, join->config.association.extraction.local, 8) ||
        join->config.association.saved.pan != initial->pan ||
        join->config.association.saved.channel != initial->channel ||
        join->config.association.saved.filter || join->config.association.saved.rx_on ||
        mac_adapter_diagnostic()->phase != MAC_ADAPTER_OFF || mac_adapter_diagnostic()->ready ||
        mac_adapter_diagnostic()->goal)
        return MAC_LINK_DRIVER_STATE;
    memset(d, 0, sizeof(*d));
    d->version = 1; d->join = join; d->owner = join->owner; d->config = *initial;
    d->saved_short = initial->short_address; d->timeout = timeout; d->limit = limit;
    d->rx_serial = mac_adapter_diagnostic()->frames;
    d->now = d->rx_ready_at = join->last;
    return MAC_LINK_DRIVER_OK;
}

mac_link_driver_result_t mac_link_driver_random(mac_link_driver_t MCU_XDATA * volatile d,
    uint32_t generation, uint8_t retry, uint8_t nb, uint8_t byte)
{
    if (!d || d->version != 1) return MAC_LINK_DRIVER_ARGUMENT;
    if (d->fault) return (mac_link_driver_result_t)d->fault;
    if (!d->random_wait || d->random_ready || generation != d->owner->engine.generation ||
        retry != d->owner->engine.retries || nb != d->owner->engine.nb)
        return MAC_LINK_DRIVER_STATE;
    d->random_byte = byte; d->random_ready = 1;
    return MAC_LINK_DRIVER_OK;
}

mac_link_driver_result_t mac_link_driver_step(mac_link_driver_t MCU_XDATA * volatile d)
{
    volatile MCU_XDATA uint8_t kind, child, ready;
    if (!d || d->version != 1) return MAC_LINK_DRIVER_ARGUMENT;
    if (d->fault) return (mac_link_driver_result_t)d->fault;
    if (d->radio.control.kind) {
        if (d->radio.control.kind == MAC_TX_ACTION_RANDOM) {
            d->random_wait = 1;
            d->radio.control.kind = 0;
            return MAC_LINK_DRIVER_RANDOM;
        }
        d->adapter_result = mac_adapter_accept(d->owner, &d->radio);
        if (d->adapter_result != MAC_ADAPTER_OK) return fault(d, MAC_LINK_DRIVER_RADIO);
        d->radio.control.kind = 0;
        return MAC_LINK_DRIVER_WAIT;
    }
    if (d->random_wait && !d->random_ready) {
        if (!sample(d)) goto done;
        if (!reached(d->now, d->owner->engine.deadline)) return MAC_LINK_DRIVER_RANDOM;
        /* Expiry supplies no invented RANDOM: a NULL step observes the real
         * deadline, then normal DISARM/unprepare retires the prepared slot. */
        d->random_ready = 2;
    }
    kind = d->action.kind;
    child = kind == BDB_JOIN_ACTION_SCAN ? d->action.data.scan.kind :
        kind == BDB_JOIN_ACTION_ASSOCIATION ? d->action.data.association.kind : 0;
    if (kind == BDB_JOIN_ACTION_INSTALL ||
        (kind == BDB_JOIN_ACTION_SCAN && (child == MAC_SCAN_ACTION_CONFIG || child == MAC_SCAN_ACTION_RESTORE)) ||
        (kind == BDB_JOIN_ACTION_ASSOCIATION && (child == MAC_JOIN_ACTION_PREPARE ||
            child == MAC_JOIN_ACTION_RECEIVE || child == MAC_JOIN_ACTION_RESTORE))) {
        configure_action(d);
        goto done;
    }
    if ((kind == BDB_JOIN_ACTION_ASSOCIATION && child == MAC_JOIN_ACTION_ARM) ||
        (kind == BDB_JOIN_ACTION_TX && d->action.data.tx.control.kind == NWK_APS_ACTION_ARM)) {
        if (!prepare(d, kind == BDB_JOIN_ACTION_ASSOCIATION ? MAC_ADAPTER_STOP_RX :
                d->owner->engine.ack_requested ? MAC_ADAPTER_KEEP_AUTOACK : MAC_ADAPTER_STOP_RX)) goto done;
        if (!sample(d)) goto done;
        if (kind == BDB_JOIN_ACTION_ASSOCIATION) {
            join_event(d, MAC_JOIN_ARMED);
            (void)consumer(d, 1);
        } else {
            d->consumer_result = bdb_join_armed(d->join, d->action.data.tx.control.generation,
                d->action.data.tx.control.retry, d->action.data.tx.control.nb);
            if (d->consumer_result != BDB_JOIN_OK) fault(d, MAC_LINK_DRIVER_CONSUMER);
            else d->action.kind = 0;
        }
        goto done;
    }
    if ((kind == BDB_JOIN_ACTION_ASSOCIATION && child == MAC_JOIN_ACTION_DISARM) ||
        (kind == BDB_JOIN_ACTION_TX && d->action.data.tx.control.kind == NWK_APS_ACTION_DISARM)) {
        if (!disarm(d) || !sample(d)) goto done;
        if (kind == BDB_JOIN_ACTION_ASSOCIATION) {
            join_event(d, MAC_JOIN_DISARMED);
            (void)consumer(d, 1);
        } else {
            d->consumer_result = bdb_join_disarmed(d->join, d->action.data.tx.control.generation,
                d->action.data.tx.control.retry, d->action.data.tx.control.nb);
            if (d->consumer_result != BDB_JOIN_OK) fault(d, MAC_LINK_DRIVER_CONSUMER);
            else d->action.kind = 0;
        }
        goto done;
    }
    if ((kind == BDB_JOIN_ACTION_ASSOCIATION && child == MAC_JOIN_ACTION_RADIO) ||
        kind == BDB_JOIN_ACTION_TX) {
        d->radio = kind == BDB_JOIN_ACTION_TX ? d->action.data.tx : d->action.data.association.radio;
        d->action.kind = 0;
        goto done;
    }
    if (kind == BDB_JOIN_ACTION_SCAN && child == MAC_SCAN_ACTION_RECEIVE) {
        if (mac_adapter_diagnostic()->phase != MAC_ADAPTER_RX || mac_adapter_diagnostic()->normal_rx)
            return fault(d, MAC_LINK_DRIVER_COVERAGE);
        if (!d->stage) {
            if (!sample(d)) goto done;
            d->end.symbols = MAC_LINK_CEIL(d->clock); d->end.fine = 0; d->stage = 1;
        }
        if (!sample(d) || !reached(d->now, d->end.symbols)) goto done;
        scan_event(d, MAC_SCAN_EVENT_OPENED);
        d->event.data.scan.stamp = d->end.symbols;
        (void)consumer(d, 1);
        goto done;
    }
    if (kind == BDB_JOIN_ACTION_ASSOCIATION && child == MAC_JOIN_ACTION_CLOSE) {
        if (!close_off(d) || !sample(d) || !reached(d->now, d->owner->engine.ready_at)) goto done;
        join_event(d, MAC_JOIN_CLOSED);
        d->event.data.association.through = d->closed_through;
        (void)consumer(d, 1);
        goto done;
    }
    if ((kind == BDB_JOIN_ACTION_SCAN && child == MAC_SCAN_ACTION_TX) ||
        (kind == BDB_JOIN_ACTION_ASSOCIATION && child == MAC_JOIN_ACTION_TX)) {
        if (d->owner->engine.phase == MAC_TX_DRAW && mac_adapter_diagnostic()->phase != MAC_ADAPTER_PREPARED) {
            (void)prepare(d, kind == BDB_JOIN_ACTION_SCAN ? MAC_ADAPTER_KEEP_RAW : MAC_ADAPTER_KEEP_AUTOACK);
            goto done;
        }
    }
    if (d->join->phase >= BDB_JOIN_FAILED) return MAC_LINK_DRIVER_FINISHED;
    if (d->join->phase == BDB_JOIN_SCANNING && d->join->work.scan.phase == MAC_SCAN_RX &&
        mac_adapter_diagnostic()->phase == MAC_ADAPTER_RX && !mac_adapter_diagnostic()->ready) {
        d->end.symbols = d->join->work.scan.window_end; d->end.fine = 0;
        d->adapter_result = mac_adapter_close(reached(d->now, d->end.symbols) ? NULL : &d->end);
        if (d->adapter_result != MAC_ADAPTER_OK) return fault(d, MAC_LINK_DRIVER_RADIO);
    }
    if (d->join->workspace == BDB_JOIN_WORK_RUNTIME && mac_adapter_diagnostic()->phase == MAC_ADAPTER_OFF &&
        d->owner->engine.phase == MAC_TX_IDLE && !mac_adapter_diagnostic()->ready) {
        d->adapter_result = mac_adapter_open(d->timeout, d->limit, &d->clock);
        if (d->adapter_result != MAC_ADAPTER_OK) return fault(d, MAC_LINK_DRIVER_RADIO);
    }
    if (d->random_ready) {
        if (!sample(d)) goto done;
        memset(&d->source, 0, sizeof(d->source));
        d->source.source.kind = d->random_ready == 1 ? MAC_TX_EVENT_RANDOM : 0;
        d->source.source.generation = d->owner->engine.generation;
        d->source.source.retry = d->owner->engine.retries;
        d->source.source.nb = d->owner->engine.nb;
        d->source.source.stamp = d->now;
        d->source.source.value = d->random_byte;
        d->random_ready = d->random_wait = 0;
    } else if (!observe(d)) goto done;
    ready = mac_adapter_diagnostic()->ready;
    /* received() may already have delivered a frame and produced the next
     * action. Do not consume its new TX grant as if it preceded that frame.
     */
    if (ready && mac_adapter_observation()->kind == MAC_ADAPTER_RX_EVENT && !d->source.source.kind) {
        (void)consume(d);
        goto done;
    }
    if (ready && mac_adapter_observation()->kind == MAC_ADAPTER_CLOSED_EVENT &&
        d->join->phase == BDB_JOIN_SCANNING && d->join->work.scan.phase == MAC_SCAN_RX) {
        scan_event(d, MAC_SCAN_EVENT_CLOSED);
        d->event.data.scan.stamp = d->closed_through;
        d->event.data.scan.state.pan = d->config.pan;
        d->event.data.scan.state.channel = d->config.channel;
        d->event.data.scan.state.filter = MAC_SCAN_FILTER_BEACONS;
        d->event.data.scan.state.rx_on = 0;
        (void)consumer(d, 1);
    } else if ((kind == BDB_JOIN_ACTION_SCAN && child == MAC_SCAN_ACTION_TX) ||
               (kind == BDB_JOIN_ACTION_ASSOCIATION && child == MAC_JOIN_ACTION_TX)) {
        (void)pump(d);
    } else if (d->source.source.kind) {
        if (d->join->phase == BDB_JOIN_ASSOCIATING) {
            join_event(d, MAC_JOIN_SOURCE);
            d->event.data.association.source = d->source;
            d->event.data.association.crc_valid = d->source.source.kind == MAC_TX_EVENT_ACK_INTERVAL;
        } else {
            memset(&d->event, 0, sizeof(d->event));
            d->event.kind = BDB_JOIN_EVENT_TX;
            d->event.data.tx = d->source;
        }
        (void)consumer(d, 1);
    } else (void)consumer(d, 0);
    if (ready && !d->fault) (void)consume(d);
done:
    return d->fault ? (mac_link_driver_result_t)d->fault : MAC_LINK_DRIVER_WAIT;
}
