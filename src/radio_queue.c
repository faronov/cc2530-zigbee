/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "radio_queue.h"
#include "irq.h"
#include "timebase.h"
#include <stddef.h>

volatile MCU_XDATA uint8_t radio_queue_fault, radio_queue_event_head, radio_queue_event_tail;
volatile MCU_XDATA uint8_t radio_queue_events[RADIO_QUEUE_EVENTS], radio_queue_dropped;
MCU_XDATA radio_rx_frame_t radio_queue_rx[RADIO_QUEUE_RX_SLOTS];
MCU_XDATA radio_queue_tx_frame_t radio_queue_tx;
MCU_XDATA radio_rx_diagnostics_t radio_queue_diagnostics;
MCU_XDATA uint8_t radio_queue_rx_head, radio_queue_rx_tail, radio_queue_rx_count, radio_queue_tx_count;
static MCU_XDATA uint8_t cancelled, bad_crc, last_cookie, driver_valid, driver_result;
static MCU_XDATA radio_queue_status_t snapshot;
extern MCU_XDATA uint8_t radio_queue_reserved_end, _gptrput_PARM_2;

static radio_queue_result_t latch(radio_queue_result_t result)
{
    if (!radio_queue_fault) radio_queue_fault = result;
    return (radio_queue_result_t)radio_queue_fault;
}

static uint8_t caller(const void MCU_XDATA *object, uint8_t size)
{
    uint16_t address = MMIO_XADDRESS(object), helper = MMIO_XADDRESS(&_gptrput_PARM_2);
    return address > MMIO_XADDRESS(&radio_queue_reserved_end) && address < 0x1e00u &&
        size <= 0x1e00u-address && !(helper >= address && helper-address < size);
}

static uint8_t rx_valid(void)
{
    return radio_queue_rx_head < 2 && radio_queue_rx_tail < 2 && radio_queue_rx_count <= 2 &&
        radio_queue_rx_tail == ((radio_queue_rx_head + radio_queue_rx_count) & 1u);
}

radio_queue_result_t radio_queue_request_rx(uint8_t cookie)
#if defined(__SDCC)
    __reentrant
#endif
{
    irq_state_t token;
    uint8_t head, count;
    radio_queue_result_t result;
    if (radio_queue_fault) return (radio_queue_result_t)radio_queue_fault;
    token = irq_save_disable();
    if (radio_queue_fault) result = (radio_queue_result_t)radio_queue_fault;
    else {
        head = radio_queue_event_head;
        count = (uint8_t)(head - radio_queue_event_tail);
        if (count > RADIO_QUEUE_EVENTS) {
            radio_queue_fault = RADIO_QUEUE_CORRUPT; result = RADIO_QUEUE_CORRUPT;
        } else if (count == RADIO_QUEUE_EVENTS) {
            if (radio_queue_dropped != 255) radio_queue_dropped++;
            result = RADIO_QUEUE_FULL;
        } else {
            radio_queue_events[head & 3u] = cookie;
            radio_queue_event_head = head + 1u;
            result = RADIO_QUEUE_OK;
        }
    }
    if (irq_restore(token) != IRQ_OK) {
        if (!radio_queue_fault) radio_queue_fault = RADIO_QUEUE_IRQ_FAILED;
        return (radio_queue_result_t)radio_queue_fault;
    }
    return result;
}

radio_queue_result_t radio_queue_service(uint8_t channel, uint32_t timeout, uint16_t limit)
{
    uint8_t count, slot;
    radio_rx_result_t result;
    if (radio_queue_fault) return (radio_queue_result_t)radio_queue_fault;
    if (channel < 11 || channel > 26 || !timeout || timeout >= TIMEBASE_HALF_RANGE || !limit)
        return RADIO_QUEUE_INVALID_ARGUMENT;
    count = (uint8_t)(radio_queue_event_head - radio_queue_event_tail);
    if (!rx_valid() || count > RADIO_QUEUE_EVENTS) return latch(RADIO_QUEUE_CORRUPT);
    if (!count) return RADIO_QUEUE_EMPTY;
    if (radio_queue_rx_count == RADIO_QUEUE_RX_SLOTS) return RADIO_QUEUE_FULL;
    if (MMIO_READ(SOC_IEN0) || MMIO_READ(SOC_IEN1) || MMIO_READ(SOC_IEN2))
        return RADIO_QUEUE_IRQ_ACTIVE;
    last_cookie = radio_queue_events[radio_queue_event_tail & 3u];
    radio_queue_event_tail++;
    slot = radio_queue_rx_tail;
    result = radio_rx_receive_init(channel, timeout, limit, &radio_queue_rx[slot], &radio_queue_diagnostics);
    driver_result = result; driver_valid = 1;
    if (result == RADIO_RX_BAD_CRC) {
        if (bad_crc != 255) bad_crc++;
        return RADIO_QUEUE_BAD_CRC;
    }
    if (result != RADIO_RX_OK) return latch(RADIO_QUEUE_RADIO_FAILED);
    radio_queue_rx_tail ^= 1u;
    radio_queue_rx_count++;
    return RADIO_QUEUE_OK;
}

radio_queue_result_t radio_queue_rx_read(radio_rx_frame_t MCU_XDATA * volatile output)
{
    uint8_t i, slot, length;
    if (output == NULL) return RADIO_QUEUE_INVALID_ARGUMENT;
    if (!caller(output, sizeof(*output))) return RADIO_QUEUE_BUFFER_OWNERSHIP;
    if (!rx_valid()) return latch(RADIO_QUEUE_CORRUPT);
    if (!radio_queue_rx_count) return RADIO_QUEUE_EMPTY;
    slot = radio_queue_rx_head; length = radio_queue_rx[slot].length;
    if (!length || length > RADIO_RX_BODY_MAX) return latch(RADIO_QUEUE_CORRUPT);
    for (i = 0; i < length; i++) output->body[i] = radio_queue_rx[slot].body[i];
    output->length = length;
    output->rssi_raw = radio_queue_rx[slot].rssi_raw;
    output->correlation = radio_queue_rx[slot].correlation;
    radio_queue_rx_head ^= 1u;
    radio_queue_rx_count--;
    return RADIO_QUEUE_OK;
}

radio_queue_result_t radio_queue_tx_submit(const uint8_t MCU_XDATA * volatile body, uint8_t length)
{
    uint8_t i;
    if (radio_queue_fault) return (radio_queue_result_t)radio_queue_fault;
    if (body == NULL || !length || length > RADIO_RX_BODY_MAX) return RADIO_QUEUE_INVALID_ARGUMENT;
    if (!caller(body, length)) return RADIO_QUEUE_BUFFER_OWNERSHIP;
    if (radio_queue_tx_count > 1) return latch(RADIO_QUEUE_CORRUPT);
    if (radio_queue_tx_count) return RADIO_QUEUE_FULL;
    for (i = 0; i < length; i++) radio_queue_tx.body[i] = body[i];
    radio_queue_tx.length = length;
    radio_queue_tx_count = 1;
    return RADIO_QUEUE_OK;
}

radio_queue_result_t radio_queue_tx_read(radio_queue_tx_frame_t MCU_XDATA * volatile output)
{
    uint8_t i, length;
    if (output == NULL) return RADIO_QUEUE_INVALID_ARGUMENT;
    if (!caller(output, sizeof(*output))) return RADIO_QUEUE_BUFFER_OWNERSHIP;
    if (radio_queue_tx_count > 1) return latch(RADIO_QUEUE_CORRUPT);
    if (!radio_queue_tx_count) return RADIO_QUEUE_EMPTY;
    length = radio_queue_tx.length;
    if (!length || length > RADIO_RX_BODY_MAX) return latch(RADIO_QUEUE_CORRUPT);
    for (i = 0; i < length; i++) output->body[i] = radio_queue_tx.body[i];
    output->length = length;
    radio_queue_tx_count = 0;
    return RADIO_QUEUE_OK;
}

radio_queue_result_t radio_queue_tx_cancel(void)
{
    if (radio_queue_tx_count > 1) return latch(RADIO_QUEUE_CORRUPT);
    if (!radio_queue_tx_count) return RADIO_QUEUE_EMPTY;
    radio_queue_tx_count = 0;
    return RADIO_QUEUE_OK;
}

radio_queue_result_t radio_queue_cancel_requests(void)
{
    uint8_t count;
    irq_state_t token = irq_save_disable();
    radio_queue_result_t result = RADIO_QUEUE_OK;
    count = (uint8_t)(radio_queue_event_head-radio_queue_event_tail);
    if (count > RADIO_QUEUE_EVENTS) result = latch(RADIO_QUEUE_CORRUPT);
    else {
        radio_queue_event_tail = radio_queue_event_head;
        cancelled = count > 255u-cancelled ? 255 : cancelled+count;
    }
    if (irq_restore(token) != IRQ_OK) return latch(RADIO_QUEUE_IRQ_FAILED);
    return result;
}

radio_queue_result_t radio_queue_snapshot(radio_queue_status_t MCU_XDATA * volatile output)
{
    uint8_t i;
    irq_state_t token;
    if (output == NULL) return RADIO_QUEUE_INVALID_ARGUMENT;
    if (!caller(output, sizeof(*output))) return RADIO_QUEUE_BUFFER_OWNERSHIP;
    token = irq_save_disable();
    snapshot.fault = radio_queue_fault;
    snapshot.driver_result = driver_valid ? driver_result : 255;
    snapshot.last_cookie = last_cookie;
    snapshot.requests = (uint8_t)(radio_queue_event_head-radio_queue_event_tail);
    snapshot.dropped = radio_queue_dropped; snapshot.cancelled = cancelled;
    snapshot.rx_count = radio_queue_rx_count; snapshot.tx_count = radio_queue_tx_count;
    snapshot.bad_crc = bad_crc;
    if (irq_restore(token) != IRQ_OK) return latch(RADIO_QUEUE_IRQ_FAILED);
    for (i = 0; i < sizeof(snapshot); i++)
        ((uint8_t MCU_XDATA *)output)[i] = ((uint8_t MCU_XDATA *)&snapshot)[i];
    return RADIO_QUEUE_OK;
}

MCU_XDATA uint8_t radio_queue_reserved_end;
