/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "radio_rx.h"
#include "timebase.h"
#include <stddef.h>

MCU_XDATA uint8_t radio_rx_fault;
static MCU_XDATA uint8_t staging[128];
extern MCU_XDATA uint8_t radio_rx_reserved_end, _gptrput_PARM_2;

/* SWRU191F pp.256-268: fixed passive framing and recommended RF settings.
 * The last value is the validated channel's frequency word.
 */
static const MCU_CODE uint16_t settings[] = {
    0x6189, 0x618a, 0x6180, 0x6182, 0x6194, 0x6195, 0x61b2, 0x61fa, 0x61ae, 0x618f
};
static const MCU_CODE uint8_t values[] = {
    0x40, 0, 0x0c, 0, 0x7f, 1, 0x15, 9, 0, 0
};

typedef struct {
    uint32_t start, previous, deadline;
    uint16_t limit;
    uint8_t frequency, command;
} rx_wait_t;

static uint8_t ordinary(uint16_t address, uint8_t size)
{
    return address < 0x1e00u && size <= 0x1e00u - address;
}

static uint8_t overlaps(uint16_t a, uint8_t an, uint16_t b, uint8_t bn)
{
    return a < b ? b - a < an : a - b < bn;
}

static radio_rx_result_t observe(radio_rx_diagnostics_t MCU_XDATA *d,
                                 rx_wait_t MCU_XDATA *w)
{
    uint8_t command, status, i;
    d->sample_valid = 0;
    if (MMIO_READ(SOC_IEN0) || MMIO_READ(SOC_IEN1) || MMIO_READ(SOC_IEN2) ||
        (MMIO_READ(SOC_SLEEPCMD) & 7u) != 4u)
        return RADIO_RX_UNSUPPORTED_STATE;
    command = MMIO_READ(SOC_CLKCONCMD);
    status = MMIO_READ(SOC_CLKCONSTA);
    if ((command & 0x47u) || status != command)
        return RADIO_RX_UNSUPPORTED_STATE;
    if (command != w->command)
        return RADIO_RX_STATE_CHANGED;
    if (MMIO_XREAD(0x624a) != 0xa5 || MMIO_XREAD(0x61e1) & 0xe0u ||
        MMIO_XREAD(0x6189) != 0x40 || MMIO_XREAD(0x61a3) ||
        MMIO_XREAD(0x61a4) || MMIO_XREAD(0x61a5) ||
        MMIO_XREAD(0x61a8) != 0x85 || MMIO_XREAD(0x61a9) != 0x14 ||
        MMIO_XREAD(0x61b8) != 0x75 || MMIO_XREAD(0x61b9) != 8)
        return RADIO_RX_UNSUPPORTED_STATE;
    for (i = 0; i < d->writes; i++)
        if (MMIO_XREAD(settings[i]) != (i == 9 ? w->frequency : values[i]))
            return RADIO_RX_STATE_CHANGED;
    d->rx_enable = MMIO_XREAD(0x618b);
    d->fsm0 = MMIO_XREAD(0x6192);
    d->signals = MMIO_XREAD(0x6193);
    d->rx_count = MMIO_XREAD(0x619b);
    d->tx_count = MMIO_XREAD(0x619c);
    d->rx_first = MMIO_XREAD(0x619d);
    d->rx_last = MMIO_XREAD(0x619e);
    d->rx_packet = MMIO_XREAD(0x619f);
    d->tx_first = MMIO_XREAD(0x61a1);
    d->tx_last = MMIO_XREAD(0x61a2);
    d->rssi_valid = MMIO_XREAD(0x6199);
    d->errors = MMIO_READ(SOC_RFERRF);
    d->flags0 = MMIO_READ(SOC_RFIRQF0);
    d->flags1 = MMIO_READ(SOC_RFIRQF1);
    d->sample_valid = 1;
    if ((d->rx_enable & 0x7fu) || (d->fsm0 & 0x80u) || (d->signals & 2u) ||
        (d->rssi_valid & 0xfeu) || (d->flags0 & 1u) || (d->flags1 & 0xfbu) ||
        ((d->rx_first | d->rx_last) & 0x80u))
        return RADIO_RX_STATE_CHANGED;
    if (d->errors || (d->signals & 0xc0u) == 0x40u)
        return RADIO_RX_CONTROLLER_ERROR;
    if (d->rx_count > 128 || d->tx_count || d->tx_first || d->tx_last)
        return RADIO_RX_COUNT_ERROR;
    return RADIO_RX_OK;
}

static uint8_t idle(const radio_rx_diagnostics_t MCU_XDATA *d)
{
    return !d->rx_enable && !(d->fsm0 & 0x40u) && !(d->signals & 0x27u);
}

static uint8_t empty(const radio_rx_diagnostics_t MCU_XDATA *d)
{
    return !d->rx_count && !d->rx_first && !d->rx_last && !d->rx_packet &&
           !(d->signals & 0xc0u);
}

static radio_rx_result_t poll(rx_wait_t MCU_XDATA *w, radio_rx_diagnostics_t MCU_XDATA *d)
{
    uint32_t now;
    bool expired;
    radio_rx_result_t result;
    if (d->polls == w->limit)
        return RADIO_RX_POLL_LIMIT;
    result = observe(d, w);
    now = timebase_read_awake_ticks24();
    d->polls++;
    d->elapsed_ticks = (now - w->start) & TIMEBASE_TICKS_MASK;
    if (result != RADIO_RX_OK)
        return result;
    d->timebase_status = timebase_expired(now, w->deadline, &expired);
    if (d->timebase_status != TIMEBASE_OK)
        return RADIO_RX_TIMEBASE_ERROR;
    if (d->elapsed_ticks >= TIMEBASE_HALF_RANGE ||
        ((now - w->previous) & TIMEBASE_TICKS_MASK) >= TIMEBASE_HALF_RANGE)
        return RADIO_RX_COUNTER_RANGE;
    if (expired)
        return RADIO_RX_TIMEOUT;
    w->previous = now;
    return RADIO_RX_OK;
}

radio_rx_result_t radio_rx_receive_init(uint8_t channel, uint32_t timeout, uint16_t limit,
                                      radio_rx_frame_t MCU_XDATA *output,
                                      radio_rx_diagnostics_t MCU_XDATA *d)
{
    rx_wait_t w;
    radio_rx_result_t result;
    uint16_t out, diag, end, helper;
    uint8_t i, count;
    if (radio_rx_fault)
        return (radio_rx_result_t)radio_rx_fault;
    if (channel < 11 || channel > 26 || !timeout || timeout >= TIMEBASE_HALF_RANGE ||
        !limit || output == NULL || d == NULL)
        return RADIO_RX_INVALID_ARGUMENT;
    out = MMIO_XADDRESS(output); diag = MMIO_XADDRESS(d);
    end = MMIO_XADDRESS(&radio_rx_reserved_end); helper = MMIO_XADDRESS(&_gptrput_PARM_2);
    if (!ordinary(out, sizeof(*output)) || !ordinary(diag, sizeof(*d)) ||
        overlaps(out, sizeof(*output), diag, sizeof(*d)))
        return RADIO_RX_INVALID_RANGE;
    if (out <= end || diag <= end || overlaps(out, sizeof(*output), helper, 1) ||
        overlaps(diag, sizeof(*d), helper, 1))
        return RADIO_RX_BUFFER_OWNERSHIP;
    for (i = 0; i < sizeof(*d); i++)
        ((uint8_t MCU_XDATA *)d)[i] = 0;
    w.command = MMIO_READ(SOC_CLKCONCMD);
    w.frequency = (uint8_t)(11u + 5u * (channel - 11u));
    result = observe(d, &w);
    if (result != RADIO_RX_OK) goto failed;
    if (!idle(d)) { result = RADIO_RX_BUSY; goto failed; }
    if (!empty(d)) { result = RADIO_RX_NOT_EMPTY; goto failed; }
    w.limit = limit;
    w.start = timebase_read_awake_ticks24(); w.previous = w.start;
    d->timebase_status = timebase_deadline_after(w.start, timeout, &w.deadline);
    if (d->timebase_status != TIMEBASE_OK) { result = RADIO_RX_TIMEBASE_ERROR; goto failed; }
    d->phase = 1;
    for (i = 0; i < sizeof(values); i++) {
        result = poll(&w, d);
        if (result != RADIO_RX_OK) goto failed;
        if (!idle(d) || !empty(d)) { result = RADIO_RX_STATE_CHANGED; goto failed; }
        if (d->polls == limit) { result = RADIO_RX_POLL_LIMIT; goto failed; }
        MMIO_XWRITE(settings[i], i == 9 ? w.frequency : values[i]);
        d->writes++;
        result = poll(&w, d);
        if (result != RADIO_RX_OK) goto failed;
        if (!idle(d) || !empty(d)) { result = RADIO_RX_STATE_CHANGED; goto failed; }
        d->verified = d->writes;
    }
    if (d->polls == limit) { result = RADIO_RX_POLL_LIMIT; goto failed; }
    d->phase = 2;
    MMIO_WRITE(SOC_RFST, 0xe3); d->actions |= 1;
    do {
        result = poll(&w, d);
        if (result != RADIO_RX_OK) goto failed;
    } while (d->rx_enable != 0x80 || !(d->signals & 4u) || !d->rssi_valid);
    d->phase = 3;
    while (!(d->signals & 0x40u)) {
        result = poll(&w, d);
        if (result != RADIO_RX_OK) goto failed;
        if (d->rx_enable != 0x80) { result = RADIO_RX_STATE_CHANGED; goto failed; }
    }
    if (d->polls == limit) { result = RADIO_RX_POLL_LIMIT; goto failed; }
    d->phase = 4;
    MMIO_XWRITE(0x618d, 0x80); d->actions |= 2;
    do {
        result = poll(&w, d);
        if (result != RADIO_RX_OK) goto failed;
    } while (!idle(d));
    if (!(d->signals & 0x40u) || d->rx_count < 4) {
        result = RADIO_RX_COUNT_ERROR; goto failed;
    }
    d->phase = 5;
    count = d->rx_count;
    if (d->polls == limit) { result = RADIO_RX_POLL_LIMIT; goto failed; }
    d->phr = MMIO_READ(SOC_RFD); d->bytes_read++;
    result = poll(&w, d);
    if (result != RADIO_RX_OK) goto failed;
    if (!idle(d) || d->rx_count != count - 1u) { result = RADIO_RX_COUNT_ERROR; goto failed; }
    if (d->phr < 3 || d->phr > 127) { result = RADIO_RX_BAD_LENGTH; goto failed; }
    if (d->rx_count < d->phr) { result = RADIO_RX_COUNT_ERROR; goto failed; }
    for (i = 1; i <= d->phr; i++) {
        if (d->polls == limit) { result = RADIO_RX_POLL_LIMIT; goto failed; }
        count = d->rx_count;
        staging[i] = MMIO_READ(SOC_RFD); d->bytes_read++;
        result = poll(&w, d);
        if (result != RADIO_RX_OK) goto failed;
        if (!idle(d) || d->rx_count != count - 1u) { result = RADIO_RX_COUNT_ERROR; goto failed; }
    }
    d->rssi_raw = staging[d->phr - 1u];
    d->crc_correlation = staging[d->phr];
    d->discarded_bytes = d->rx_count;
    if (d->polls == limit) { result = RADIO_RX_POLL_LIMIT; goto failed; }
    d->phase = 6;
    MMIO_WRITE(SOC_RFST, 0xed); d->actions |= 4;
    do {
        result = poll(&w, d);
        if (result != RADIO_RX_OK) goto failed;
        if (!idle(d)) { result = RADIO_RX_STATE_CHANGED; goto failed; }
    } while (!empty(d));
    if (!(d->crc_correlation & 0x80u))
        return RADIO_RX_BAD_CRC;
    d->phase = 7;
    for (i = 0; i < d->phr - 2u; i++)
        output->body[i] = staging[i + 1u];
    output->length = d->phr - 2u;
    output->rssi_raw = d->rssi_raw;
    output->correlation = d->crc_correlation & 0x7fu;
    return RADIO_RX_OK;
failed:
    radio_rx_fault = result;
    return result;
}

MCU_XDATA uint8_t radio_rx_reserved_end;
