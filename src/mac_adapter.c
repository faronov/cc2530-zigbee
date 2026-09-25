/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "mac_adapter.h"
#include "timebase.h"
#include <stddef.h>
#include <string.h>

#ifndef CC2530_MAC_ADAPTER
#error mac_adapter requires its explicit co-owned profile
#endif

enum { CLOSE_NONE = 0, CLOSE_USER, CLOSE_BUSY, CLOSE_ACK_WINDOW, CLOSE_RETIRE };
static MCU_XDATA mac_adapter_diagnostics_t status;
static MCU_XDATA mac_adapter_observation_t observation;
MCU_XDATA mac_attempt_record_t mac_adapter_receipt;
MCU_XDATA mac_epoch_stamp_t mac_adapter_live;
static MCU_XDATA mac_epoch_stamp_t began, through, retired;
static MCU_XDATA uint8_t settled;
static MCU_XDATA mac_tx_interval_action_t requested;
static const mac_tx_interval_t MCU_XDATA * MCU_XDATA owner;
extern MCU_XDATA uint8_t mac_adapter_reserved_end, __memcpy_PARM_2[3], _mullong_PARM_2[4];

static uint8_t reached(uint32_t now, uint32_t at)
{
    return (uint32_t)(now-at) < MAC_TX_HALF;
}
static uint8_t ordered(const mac_epoch_stamp_t MCU_XDATA *a,
                       const mac_epoch_stamp_t MCU_XDATA *b)
{
    return a->symbols == b->symbols ? a->fine <= b->fine :
        (uint32_t)(b->symbols-a->symbols) < MAC_TX_HALF;
}
static mac_adapter_result_t storage(uint16_t address, uint16_t size)
{
    uint16_t first = MMIO_XADDRESS(__memcpy_PARM_2), last = MMIO_XADDRESS(_mullong_PARM_2)+3u;
    if (address >= 0x1e00 || size > 0x1e00u-address) return MAC_ADAPTER_RANGE;
    if (address <= MMIO_XADDRESS(&mac_adapter_reserved_end) ||
        (address <= last && (address >= first || first-address < size)))
        return MAC_ADAPTER_STORAGE;
    return MAC_ADAPTER_OK;
}
static mac_adapter_result_t bounds(uint32_t timeout, uint16_t limit)
{
    if (status.fault) return (mac_adapter_result_t)status.fault;
    return timeout && timeout < TIMEBASE_HALF_RANGE && limit ?
        MAC_ADAPTER_OK : MAC_ADAPTER_INVALID;
}
static mac_adapter_result_t fail(mac_adapter_result_t result)
{
    status.phase = MAC_ADAPTER_FAULT; status.fault = result;
    return result;
}
static mac_adapter_result_t clock(uint32_t timeout, uint16_t limit)
{
    status.radio_result = mac_attempt_now(timeout, limit, &mac_adapter_live);
    if (status.radio_result != MAC_RADIO_READY) return fail(MAC_ADAPTER_RADIO_ERROR);
    status.live = mac_adapter_live; status.has_time = 1;
    return MAC_ADAPTER_OK;
}
static mac_adapter_result_t publish(uint8_t kind, uint8_t source)
{
    if (status.deliveries == UINT32_MAX) return fail(MAC_ADAPTER_EXHAUSTED);
    memset(&observation.tx, 0, sizeof(observation.tx));
    observation.kind = kind; observation.normal_rx = status.normal_rx;
    observation.has_upper = 1; observation.token = ++status.deliveries;
    observation.rx_serial = status.frames;
    observation.frame = &mac_adapter_receipt.frame;
    observation.through = status.watermark;
    observation.tx.source.kind = source;
    observation.tx.source.generation = status.generation;
    observation.tx.source.retry = status.retry; observation.tx.source.nb = status.nb;
    observation.tx.source.stamp = status.live.symbols;
    status.ready = 1;
    return MAC_ADAPTER_EVENT;
}
static mac_adapter_result_t frame(void)
{
    volatile MCU_XDATA mac_adapter_result_t result;
    uint8_t kind = 0;
    if (!status.bound_valid) {
        mac_adapter_receipt.rx_upper = status.live;
        status.bound_valid = 1;
    }
    if (!reached(status.live.symbols, mac_adapter_receipt.rx_upper.symbols +
                 (mac_adapter_receipt.rx_upper.fine != 0u))) return MAC_ADAPTER_WAIT;
    if (status.phase == MAC_ADAPTER_COLLECT &&
        (mac_adapter_receipt.frame.crc_correlation & 0x80u) &&
        mac_adapter_receipt.frame.length == 3 &&
        (mac_adapter_receipt.frame.body[0] & 7u) == MAC_FRAME_ACK)
        kind = MAC_TX_EVENT_ACK_INTERVAL;
    result = publish(MAC_ADAPTER_RX_EVENT, kind);
    if (result != MAC_ADAPTER_EVENT) return result;
    observation.tx.lower = mac_adapter_receipt.tx_lower;
    observation.tx.upper = mac_adapter_receipt.rx_upper;
    observation.tx.source.bytes = mac_adapter_receipt.frame.body;
    observation.tx.source.length = mac_adapter_receipt.frame.length;
    return result;
}

mac_adapter_result_t mac_adapter_init(const radio_autoack_config_t MCU_XDATA * volatile config,
                                      volatile uint32_t timeout, volatile uint16_t limit)
{
    mac_adapter_result_t result = bounds(timeout, limit);
    if (result != MAC_ADAPTER_OK) return result;
    if (!config) return MAC_ADAPTER_INVALID;
    if (status.phase != MAC_ADAPTER_COLD) return MAC_ADAPTER_STATE;
    result = storage(MMIO_XADDRESS(config), sizeof(*config));
    if (result != MAC_ADAPTER_OK) return result;
    if (config->channel < 11 || config->channel > 26 || config->power != RADIO_AUTOACK_POWER_05)
        return MAC_ADAPTER_INVALID;
    memset(&status, 0, sizeof(status)); memset(&observation, 0, sizeof(observation));
    memset(&mac_adapter_receipt, 0, sizeof(mac_adapter_receipt));
    memset(&requested, 0, sizeof(requested)); owner = NULL; settled = 0;
    observation.frame = &mac_adapter_receipt.frame;
    status.radio_result = mac_attempt_init(config, timeout, limit);
    if (status.radio_result != MAC_RADIO_READY) return fail(MAC_ADAPTER_RADIO_ERROR);
    status.phase = MAC_ADAPTER_RX; status.normal_rx = 1;
    return clock(timeout, limit);
}

mac_adapter_result_t mac_adapter_now(volatile uint32_t timeout, volatile uint16_t limit,
                                     mac_epoch_stamp_t MCU_XDATA * volatile output)
{
    mac_adapter_result_t result = bounds(timeout, limit);
    if (result != MAC_ADAPTER_OK) return result;
    if (!output) return MAC_ADAPTER_INVALID;
    if (status.phase == MAC_ADAPTER_COLD) return MAC_ADAPTER_STATE;
    result = storage(MMIO_XADDRESS(output), sizeof(*output));
    if (result != MAC_ADAPTER_OK) return result;
    result = clock(timeout, limit);
    if (result == MAC_ADAPTER_OK) *output = status.live;
    return result;
}

mac_adapter_result_t mac_adapter_prepare(const mac_tx_interval_t MCU_XDATA * volatile tx,
    volatile uint8_t policy, volatile uint32_t timeout, volatile uint16_t limit)
{
    mac_adapter_result_t result = bounds(timeout, limit);
    if (result != MAC_ADAPTER_OK) return result;
    if (!tx || policy > MAC_ADAPTER_KEEP_AUTOACK) return MAC_ADAPTER_INVALID;
    result = storage(MMIO_XADDRESS(tx), sizeof(*tx));
    if (result != MAC_ADAPTER_OK) return result;
    if (status.phase != MAC_ADAPTER_OFF || status.ready || status.held ||
        (owner && owner != tx) || tx->engine.phase != MAC_TX_DRAW || !tx->engine.generation ||
        !tx->engine.length || tx->engine.length > 125 ||
        (policy == MAC_ADAPTER_KEEP_AUTOACK && !tx->engine.ack_requested))
        return MAC_ADAPTER_STATE;
    status.mac_result = mac_tx_interval_copy(tx, mac_adapter_receipt.frame.body, 125, &status.length);
    if (status.mac_result != MAC_TX_OK) return fail(MAC_ADAPTER_PROTOCOL_ERROR);
    status.radio_result = mac_attempt_prepare(mac_adapter_receipt.frame.body, status.length, timeout, limit);
    if (status.radio_result != MAC_RADIO_READY) return fail(MAC_ADAPTER_RADIO_ERROR);
    owner = tx; status.generation = tx->engine.generation;
    status.retry = tx->engine.retries; status.nb = tx->engine.nb;
    status.policy = policy; status.normal_rx = 0; status.transmitted = status.first = 0;
    status.stop_started = status.wait_through = 0;
    status.goal = CLOSE_NONE; settled = 0;
    status.phase = MAC_ADAPTER_PREPARED;
    return MAC_ADAPTER_OK;
}

mac_adapter_result_t mac_adapter_unprepare(const mac_tx_interval_t MCU_XDATA * volatile tx,
    volatile uint32_t timeout, volatile uint16_t limit)
{
    mac_adapter_result_t result = bounds(timeout, limit);
    if (result != MAC_ADAPTER_OK) return result;
    if (!tx) return MAC_ADAPTER_INVALID;
    result = storage(MMIO_XADDRESS(tx), sizeof(*tx));
    if (result != MAC_ADAPTER_OK) return result;
    if (owner != tx || status.ready || status.phase != MAC_ADAPTER_PREPARED ||
        tx->engine.phase != MAC_TX_DONE || tx->engine.generation != status.generation)
        return MAC_ADAPTER_STATE;
    status.radio_result = mac_attempt_stop(timeout, limit);
    if (status.radio_result != MAC_RADIO_STOPPED) return fail(MAC_ADAPTER_RADIO_ERROR);
    status.phase = MAC_ADAPTER_OFF;
    return MAC_ADAPTER_OK;
}

mac_adapter_result_t mac_adapter_accept(const mac_tx_interval_t MCU_XDATA * volatile tx,
    const mac_tx_interval_action_t MCU_XDATA * volatile action)
{
    mac_adapter_result_t result;
    if (status.fault) return (mac_adapter_result_t)status.fault;
    if (!tx || !action) return MAC_ADAPTER_INVALID;
    result = storage(MMIO_XADDRESS(tx), sizeof(*tx));
    if (result != MAC_ADAPTER_OK) return result;
    result = storage(MMIO_XADDRESS(action), sizeof(*action));
    if (result != MAC_ADAPTER_OK) return result;
    if (status.ready || owner != tx || action->control.generation != status.generation ||
        action->control.retry != status.retry || action->control.nb != status.nb ||
        tx->engine.generation != status.generation || tx->engine.retries != status.retry ||
        tx->engine.nb != status.nb || action->control.phase != tx->engine.phase ||
        action->control.outcome != tx->engine.outcome)
        return MAC_ADAPTER_STATE;
    if (action->control.kind == MAC_TX_ACTION_ATTEMPT) {
        if (status.phase != MAC_ADAPTER_PREPARED || tx->engine.phase != MAC_TX_RADIO ||
            action->control.at != tx->engine.at || action->control.until != tx->engine.deadline ||
            action->control.length != status.length ||
            action->control.ack_requested != tx->engine.ack_requested)
            return MAC_ADAPTER_STATE;
        requested = *action; status.phase = MAC_ADAPTER_SCHEDULED;
    } else if (action->control.kind == MAC_TX_ACTION_COLLECT) {
        if (status.phase != MAC_ADAPTER_WAIT_MAC || !status.transmitted ||
            tx->engine.phase != MAC_TX_ACK_WAIT ||
            action->through.symbols != mac_adapter_receipt.tx_upper.symbols + MAC_TX_ACK_SYMBOLS ||
            action->through.fine != mac_adapter_receipt.tx_upper.fine)
            return MAC_ADAPTER_STATE;
        through = action->through; status.phase = MAC_ADAPTER_COLLECT;
    } else if (action->control.kind == MAC_TX_ACTION_QUIESCE) {
        if (tx->engine.phase != MAC_TX_STOPPING || status.phase == MAC_ADAPTER_RX ||
            status.phase == MAC_ADAPTER_COLD || status.phase == MAC_ADAPTER_RETIRING ||
            action->control.until != tx->engine.stop_at)
            return MAC_ADAPTER_STATE;
        requested = *action; status.goal = CLOSE_RETIRE;
        settled = 0;
        if (status.phase != MAC_ADAPTER_DRAINING && status.phase != MAC_ADAPTER_OFF)
            status.phase = MAC_ADAPTER_RETIRING;
    } else return MAC_ADAPTER_INVALID;
    return MAC_ADAPTER_OK;
}

mac_adapter_result_t mac_adapter_close(const mac_epoch_stamp_t MCU_XDATA * volatile end)
{
    mac_adapter_result_t result;
    if (status.fault) return (mac_adapter_result_t)status.fault;
    if (status.phase != MAC_ADAPTER_RX || status.ready) return MAC_ADAPTER_STATE;
    if (end) {
        result = storage(MMIO_XADDRESS(end), sizeof(*end));
        if (result != MAC_ADAPTER_OK) return result;
        if (end->fine >= 512u || !ordered(&status.live, end)) return MAC_ADAPTER_INVALID;
        through = *end;
    }
    status.wait_through = end != NULL; status.stop_started = 0; settled = 0;
    status.goal = CLOSE_USER; status.phase = MAC_ADAPTER_CLOSING;
    return MAC_ADAPTER_OK;
}

mac_adapter_result_t mac_adapter_step(volatile uint32_t timeout, volatile uint16_t limit)
{
    volatile MCU_XDATA mac_adapter_result_t result = bounds(timeout, limit);
    volatile MCU_XDATA uint8_t source;
    if (result != MAC_ADAPTER_OK) return result;
    if (status.phase == MAC_ADAPTER_COLD) return MAC_ADAPTER_STATE;
    if (status.ready) return MAC_ADAPTER_EVENT;
    if (status.deliveries == UINT32_MAX || status.frames == UINT32_MAX)
        return fail(MAC_ADAPTER_EXHAUSTED);
    result = clock(timeout, limit);
    if (result != MAC_ADAPTER_OK) return result;
    if (status.phase == MAC_ADAPTER_SCHEDULED) {
        if (reached(status.live.symbols, requested.control.until)) return MAC_ADAPTER_EXPIRED;
        if (!reached(status.live.symbols, requested.control.at)) return MAC_ADAPTER_WAIT;
        began = status.live;
        status.radio_result = mac_attempt_run(requested.control.ack_requested ? MAC_TX_ACK_SYMBOLS : 1,
                                              timeout, limit, &mac_adapter_receipt);
        if (status.radio_result != MAC_RADIO_FRAME && status.radio_result != MAC_RADIO_BAD_CRC &&
            status.radio_result != MAC_RADIO_EMPTY && status.radio_result != MAC_RADIO_CCA_BUSY)
            return fail(MAC_ADAPTER_RADIO_ERROR);
        status.transmitted = mac_adapter_receipt.transmitted; status.slot = mac_adapter_receipt.slot;
        status.held = mac_adapter_receipt.received; status.bound_valid = status.held;
        status.first = status.held;
        if (status.held) status.frames++;
        if (status.radio_result == MAC_RADIO_CCA_BUSY) {
            status.goal = CLOSE_BUSY; status.stop_started = status.wait_through = 0;
            status.phase = MAC_ADAPTER_CLOSING;
        } else status.phase = MAC_ADAPTER_REPORT;
        return MAC_ADAPTER_WAIT;
    }
    if (status.phase == MAC_ADAPTER_REPORT) {
        if (!status.transmitted) return fail(MAC_ADAPTER_PROTOCOL_ERROR);
        if (!reached(status.live.symbols, mac_adapter_receipt.tx_upper.symbols +
                     (mac_adapter_receipt.tx_upper.fine != 0u))) return MAC_ADAPTER_WAIT;
        result = publish(MAC_ADAPTER_TX_EVENT, MAC_TX_EVENT_SENT_INTERVAL);
        if (result == MAC_ADAPTER_EVENT) {
            observation.tx.lower = mac_adapter_receipt.tx_lower;
            observation.tx.upper = mac_adapter_receipt.tx_upper;
        }
        return result;
    }
    if (status.phase == MAC_ADAPTER_WAIT_MAC || status.phase == MAC_ADAPTER_PREPARED)
        return MAC_ADAPTER_WAIT;
    if (status.held) return frame();
    if (status.phase == MAC_ADAPTER_RETIRING && !status.stop_started) {
        if (requested.control.outcome == MAC_TX_ACKED && status.policy == MAC_ADAPTER_KEEP_AUTOACK) {
            status.radio_result = mac_attempt_handoff(timeout, limit);
            if (status.radio_result != MAC_RADIO_READY) return fail(MAC_ADAPTER_RADIO_ERROR);
            status.normal_rx = 1; status.phase = MAC_ADAPTER_RX; status.goal = CLOSE_RETIRE;
            settled = 0;
            return MAC_ADAPTER_WAIT;
        }
        if (status.transmitted && status.policy == MAC_ADAPTER_KEEP_RAW &&
            requested.control.outcome == MAC_TX_UNACKNOWLEDGED) {
            status.phase = MAC_ADAPTER_RX; status.goal = CLOSE_RETIRE;
            settled = 0;
            return MAC_ADAPTER_WAIT;
        } else {
            status.phase = MAC_ADAPTER_CLOSING; status.wait_through = 0;
        }
    }
    if (status.phase == MAC_ADAPTER_RX && status.goal == CLOSE_RETIRE) {
        if (!settled) { retired = status.live; settled = 1; }
        if (!reached(status.live.symbols, retired.symbols + (retired.fine != 0u)))
            return MAC_ADAPTER_WAIT;
        result = publish(MAC_ADAPTER_TX_EVENT, MAC_TX_EVENT_RETIRED);
        if (result == MAC_ADAPTER_EVENT) observation.tx.upper = retired;
        return result;
    }
    if (status.phase == MAC_ADAPTER_COLLECT && ordered(&through, &status.live)) {
        status.goal = CLOSE_ACK_WINDOW; status.phase = MAC_ADAPTER_CLOSING;
        status.wait_through = 0;
    }
    if (status.phase == MAC_ADAPTER_CLOSING &&
        (!status.wait_through || ordered(&through, &status.live))) {
        if (!status.stop_started) status.watermark = status.live;
        status.stop_started = 1;
        status.radio_result = mac_attempt_stop(timeout, limit);
        if (status.radio_result == MAC_RADIO_STOPPED) {
            status.phase = MAC_ADAPTER_OFF; settled = 0;
        }
        else if (status.radio_result == MAC_RADIO_DRAIN) status.phase = MAC_ADAPTER_DRAINING;
        else return fail(MAC_ADAPTER_RADIO_ERROR);
        return MAC_ADAPTER_WAIT;
    }
    if (status.phase == MAC_ADAPTER_OFF) {
        if (status.goal == CLOSE_NONE) return MAC_ADAPTER_WAIT;
        if (!settled) { retired = status.live; settled = 1; }
        if (!reached(status.live.symbols, retired.symbols + (retired.fine != 0u)))
            return MAC_ADAPTER_WAIT;
        source = status.goal == CLOSE_BUSY ? MAC_TX_EVENT_BUSY_INTERVAL :
            status.goal == CLOSE_ACK_WINDOW ? MAC_TX_EVENT_RX_CLOSED :
            status.goal == CLOSE_RETIRE ? MAC_TX_EVENT_RETIRED : 0;
        result = publish(source ? MAC_ADAPTER_TX_EVENT : MAC_ADAPTER_CLOSED_EVENT, source);
        if (result != MAC_ADAPTER_EVENT) return result;
        if (source == MAC_TX_EVENT_RX_CLOSED) observation.tx.upper = status.watermark;
        else if (source == MAC_TX_EVENT_BUSY_INTERVAL) observation.tx.upper = mac_adapter_receipt.armed;
        else observation.tx.upper = retired;
        if (source == MAC_TX_EVENT_BUSY_INTERVAL) {
            observation.tx.lower = began; observation.tx.lower.symbols += 8;
        }
        return result;
    }
    status.radio_result = mac_attempt_receive(timeout, limit, &mac_adapter_receipt.frame);
    if (status.radio_result == MAC_RADIO_FRAME || status.radio_result == MAC_RADIO_BAD_CRC) {
        status.held = 1; status.bound_valid = status.first = 0; status.frames++;
        return MAC_ADAPTER_WAIT;
    }
    if (status.radio_result != MAC_RADIO_EMPTY) return fail(MAC_ADAPTER_RADIO_ERROR);
    if (status.phase == MAC_ADAPTER_DRAINING) {
        status.phase = MAC_ADAPTER_CLOSING; status.wait_through = 0;
    }
    return MAC_ADAPTER_WAIT;
}

mac_adapter_result_t mac_adapter_consume(uint32_t token)
{
    if (status.fault) return (mac_adapter_result_t)status.fault;
    if (!status.ready || token != observation.token) return MAC_ADAPTER_STATE;
    status.ready = 0;
    if (observation.kind == MAC_ADAPTER_RX_EVENT) status.held = 0;
    if (observation.tx.source.kind == MAC_TX_EVENT_SENT_INTERVAL) status.phase = MAC_ADAPTER_WAIT_MAC;
    if (observation.kind == MAC_ADAPTER_CLOSED_EVENT ||
        (observation.kind == MAC_ADAPTER_TX_EVENT &&
         observation.tx.source.kind != MAC_TX_EVENT_SENT_INTERVAL)) status.goal = CLOSE_NONE;
    return MAC_ADAPTER_OK;
}
const mac_adapter_observation_t MCU_XDATA *mac_adapter_observation(void) { return &observation; }
const mac_adapter_diagnostics_t MCU_XDATA *mac_adapter_diagnostic(void) { return &status; }
const mac_attempt_record_t MCU_XDATA *mac_adapter_record(void) { return &mac_adapter_receipt; }
MCU_XDATA uint8_t mac_adapter_reserved_end;
