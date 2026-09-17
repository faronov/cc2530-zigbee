/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "radio_fifo.h"
#include "cc2530_mmio.h"
#include "timebase.h"

#include <stddef.h>
#include <string.h>

typedef struct {
    uint32_t start, previous, deadline;
    uint16_t limit;
    uint8_t command, rx[5], tx[3];
} fifo_wait_t;

static radio_fifo_result_t observe(radio_fifo_diagnostics_t MCU_XDATA *d,
                                   uint8_t MCU_XDATA *command)
{
    uint8_t ien0, ien1, ien2, sleep, status, control0, control1, csp, fsm0, rxenable;

    d->sample_valid = 0;
    ien0 = MMIO_READ(SOC_IEN0);
    ien1 = MMIO_READ(SOC_IEN1);
    ien2 = MMIO_READ(SOC_IEN2);
    sleep = MMIO_READ(SOC_SLEEPCMD);
    *command = MMIO_READ(SOC_CLKCONCMD);
    status = MMIO_READ(SOC_CLKCONSTA);
    if (ien0 != 0 || ien1 != 0 || ien2 != 0 || (sleep & 7u) != 4u ||
        (*command & 0x47u) != 0 || status != *command)
        return RADIO_FIFO_UNSUPPORTED_STATE;

    /* SWRU191F pp.239,241,259-265. Never inspect source/address RAM or use
     * the reset-unknown fast FSM state number to sequence software (p.236).
     */
    control0 = MMIO_XREAD(0x6189);
    control1 = MMIO_XREAD(0x618a);
    csp = MMIO_XREAD(0x61e1);
    fsm0 = MMIO_XREAD(0x6192);
    rxenable = MMIO_XREAD(0x618b);
    d->fifo_signals = MMIO_XREAD(0x6193);
    d->rx_count = MMIO_XREAD(0x619b);
    d->tx_count = MMIO_XREAD(0x619c);
    d->rx_first = MMIO_XREAD(0x619d);
    d->rx_last = MMIO_XREAD(0x619e);
    d->rx_packet = MMIO_XREAD(0x619f);
    d->tx_first = MMIO_XREAD(0x61a1);
    d->tx_last = MMIO_XREAD(0x61a2);
    d->errors = MMIO_READ(SOC_RFERRF);
    d->sample_valid = 1;
    if (control0 != 0x40 || control1 != 1 || (csp & 0xc0u) != 0 ||
        (fsm0 & 0x80u) != 0 || (d->errors & 0x80u) != 0 ||
        ((d->rx_first | d->rx_last) & 0x80u) != 0)
        return RADIO_FIFO_UNSUPPORTED_STATE;
    if ((csp & 0x20u) != 0 || (fsm0 & 0x40u) != 0 ||
        rxenable != 0 || (d->fifo_signals & 0x27u) != 0)
        return RADIO_FIFO_BUSY;
    if (d->errors != 0 || (d->fifo_signals & 0xc0u) == 0x40u)
        return RADIO_FIFO_CONTROLLER_ERROR;
    if (d->rx_count > 128u || d->tx_count > 128u)
        return RADIO_FIFO_COUNT_ERROR;
    return RADIO_FIFO_OK;
}

static uint8_t rx_empty(const radio_fifo_diagnostics_t MCU_XDATA *d)
{
    return d->rx_count == 0 && d->rx_first == 0 && d->rx_last == 0 &&
           d->rx_packet == 0 && (d->fifo_signals & 0xc0u) == 0;
}

static uint8_t tx_empty(const radio_fifo_diagnostics_t MCU_XDATA *d)
{
    return d->tx_count == 0 && d->tx_first == 0 && d->tx_last == 0;
}

static radio_fifo_result_t wait_for(uint8_t goal, uint8_t expected,
                                    fifo_wait_t MCU_XDATA *w, radio_fifo_diagnostics_t MCU_XDATA *d)
{
    uint32_t now;
    uint8_t command, ready;
    bool expired;
    radio_fifo_result_t result;

    while (d->polls < w->limit) {
        result = observe(d, &command);
        now = timebase_read_awake_ticks24();
        d->polls++;
        d->elapsed_ticks = (now - w->start) & TIMEBASE_TICKS_MASK;
        if (result != RADIO_FIFO_OK)
            return result;
        if (command != w->command)
            return RADIO_FIFO_STATE_CHANGED;
        d->timebase_status = timebase_expired(now, w->deadline, &expired);
        if (d->timebase_status != TIMEBASE_OK)
            return RADIO_FIFO_TIMEBASE_ERROR;
        if (d->elapsed_ticks >= TIMEBASE_HALF_RANGE ||
            ((now - w->previous) & TIMEBASE_TICKS_MASK) >= TIMEBASE_HALF_RANGE)
            return RADIO_FIFO_COUNTER_RANGE;
        if (expired)
            return RADIO_FIFO_TIMEOUT;
        w->previous = now;
        if (goal == RADIO_FIFO_RX_FLUSH) {
            if (d->tx_count != w->tx[0] || d->tx_first != w->tx[1] || d->tx_last != w->tx[2])
                return RADIO_FIFO_STATE_CHANGED;
            ready = rx_empty(d);
        } else {
            if (d->rx_count != w->rx[0] || d->rx_first != w->rx[1] ||
                d->rx_last != w->rx[2] || d->rx_packet != w->rx[3] ||
                (d->fifo_signals & 0xc0u) != w->rx[4])
                return RADIO_FIFO_STATE_CHANGED;
            if (goal == RADIO_FIFO_TX_FLUSH)
                ready = tx_empty(d);
            else {
                if (d->tx_first != 0 || d->tx_count < expected - 1u || d->tx_count > expected ||
                    d->tx_last < expected - 1u || d->tx_last > expected)
                    return RADIO_FIFO_COUNT_ERROR;
                ready = d->tx_count == expected && d->tx_last == expected;
            }
        }
        if (ready)
            return RADIO_FIFO_OK;
    }
    return RADIO_FIFO_POLL_LIMIT;
}

static radio_fifo_result_t operate(const uint8_t *body, uint8_t length, uint32_t timeout,
                                   uint16_t limit, radio_fifo_diagnostics_t MCU_XDATA *d)
{
    fifo_wait_t w;
    radio_fifo_result_t result;
    uint8_t index;

    memset(d, 0, sizeof(*d));
    result = observe(d, &w.command);
    if (result != RADIO_FIFO_OK)
        return result;
    if (body != NULL && !tx_empty(d))
        return RADIO_FIFO_NOT_EMPTY;
    if (body == NULL && rx_empty(d) && tx_empty(d))
        return RADIO_FIFO_EMPTY;
    w.rx[0] = d->rx_count;
    w.rx[1] = d->rx_first;
    w.rx[2] = d->rx_last;
    w.rx[3] = d->rx_packet;
    w.rx[4] = d->fifo_signals & 0xc0u;
    w.tx[0] = d->tx_count;
    w.tx[1] = d->tx_first;
    w.tx[2] = d->tx_last;
    w.limit = limit;
    w.start = timebase_read_awake_ticks24();
    w.previous = w.start;
    d->timebase_status = timebase_deadline_after(w.start, timeout, &w.deadline);
    if (d->timebase_status != TIMEBASE_OK)
        return RADIO_FIFO_TIMEBASE_ERROR;
    if (body == NULL) {
        if (!rx_empty(d)) {
            MMIO_WRITE(SOC_RFST, 0xed);
            d->strobes |= RADIO_FIFO_RX_FLUSH;
            result = wait_for(RADIO_FIFO_RX_FLUSH, 0, &w, d);
            if (result != RADIO_FIFO_OK)
                return result;
            d->confirmed |= RADIO_FIFO_RX_FLUSH;
            memset(w.rx, 0, sizeof(w.rx));
        }
        if (!tx_empty(d)) {
            if (d->polls == limit)
                return RADIO_FIFO_POLL_LIMIT;
            MMIO_WRITE(SOC_RFST, 0xee);
            d->strobes |= RADIO_FIFO_TX_FLUSH;
            result = wait_for(RADIO_FIFO_TX_FLUSH, 0, &w, d);
            if (result != RADIO_FIFO_OK)
                return result;
            d->confirmed |= RADIO_FIFO_TX_FLUSH;
        }
    } else {
        for (index = 0; index <= length; index++) {
            if (d->polls == limit)
                return RADIO_FIFO_POLL_LIMIT;
            MMIO_WRITE(SOC_RFD, index == 0 ? length + 2u : body[index - 1u]);
            d->bytes_written++;
            result = wait_for(0, d->bytes_written, &w, d);
            if (result != RADIO_FIFO_OK)
                return result;
            d->bytes_verified++;
        }
    }
    return RADIO_FIFO_OK;
}

radio_fifo_result_t radio_fifo_clear_init(uint32_t timeout_ticks, uint16_t poll_limit,
                                         radio_fifo_diagnostics_t MCU_XDATA *diagnostics)
{
    if (timeout_ticks == 0 || timeout_ticks >= TIMEBASE_HALF_RANGE ||
        poll_limit == 0 || diagnostics == NULL)
        return RADIO_FIFO_INVALID_ARGUMENT;
    return operate(NULL, 0, timeout_ticks, poll_limit, diagnostics);
}

radio_fifo_result_t radio_fifo_preload_init(const uint8_t *body, uint16_t body_length,
                                           uint32_t timeout_ticks, uint16_t poll_limit,
                                           radio_fifo_diagnostics_t MCU_XDATA *diagnostics)
{
    if (body == NULL || body_length == 0 || body_length > RADIO_FIFO_BODY_MAX ||
        timeout_ticks == 0 || timeout_ticks >= TIMEBASE_HALF_RANGE ||
        poll_limit == 0 || diagnostics == NULL)
        return RADIO_FIFO_INVALID_ARGUMENT;
    return operate(body, (uint8_t)body_length, timeout_ticks, poll_limit, diagnostics);
}
