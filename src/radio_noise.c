/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "radio_noise.h"
#include "timebase.h"
#include <stddef.h>
#include <string.h>

MCU_XDATA uint8_t radio_noise_fault, radio_noise_used;
extern MCU_XDATA uint8_t radio_noise_reserved_end, _gptrput_PARM_2, memset_PARM_2;
extern MCU_XDATA uint8_t __memcpy_PARM_2[3];
static MCU_XDATA radio_noise_request_t owned;
static radio_noise_capture_t MCU_XDATA *out;
static MCU_XDATA struct {
    uint32_t start, previous, deadline, after, before, gap, span;
    uint8_t clock, frequency;
} work;
static const MCU_CODE uint16_t settings[] = {
    0x6189, 0x618a, 0x6180, 0x6182, 0x6194, 0x6195, 0x61b2, 0x61fa, 0x61ae, 0x618f
};
static const MCU_CODE uint8_t values[] = {0x4c, 0, 0x0c, 0, 0x7f, 1, 0x15, 9, 0, 0};

static radio_noise_result_t storage(uint16_t address, uint8_t size)
{
    uint16_t first = MMIO_XADDRESS(__memcpy_PARM_2);
    uint16_t helper = MMIO_XADDRESS(&memset_PARM_2);
    if (helper < first) first = helper;
    helper = MMIO_XADDRESS(&_gptrput_PARM_2);
    if (helper < first) first = helper;
    if (address >= 0x1e00 || size > 0x1e00u - address)
        return RADIO_NOISE_INVALID_RANGE;
    if (address <= MMIO_XADDRESS(&radio_noise_reserved_end) ||
        address >= first || size > first - address)
        return RADIO_NOISE_BUFFER_OWNERSHIP;
    return RADIO_NOISE_OK;
}

static uint8_t overlaps(uint16_t a, uint8_t an, uint16_t b, uint8_t bn)
{
    return a < b ? b - a < an : a - b < bn;
}

static radio_noise_result_t observe(void)
{
    uint8_t i, value, clock;
    if (MMIO_READ(SOC_IEN0) || MMIO_READ(SOC_IEN1) || MMIO_READ(SOC_IEN2) ||
        (MMIO_READ(SOC_SLEEPCMD) & 7u) != 4u ||
        MMIO_READ(SOC_DMAARM) || MMIO_READ(SOC_DMAREQ))
        return RADIO_NOISE_UNSUPPORTED_STATE;
    clock = MMIO_READ(SOC_CLKCONCMD);
    if ((clock & 0x47u) || MMIO_READ(SOC_CLKCONSTA) != clock)
        return RADIO_NOISE_UNSUPPORTED_STATE;
    if (clock != work.clock) return RADIO_NOISE_STATE_CHANGED;
    if (MMIO_XREAD(0x624a) != 0xa5 || MMIO_XREAD(0x61e1) ||
        MMIO_XREAD(0x61a3) || MMIO_XREAD(0x61a4) || MMIO_XREAD(0x61a5) ||
        MMIO_XREAD(0x61a8) != 0x85 || MMIO_XREAD(0x61a9) != 0x14 ||
        MMIO_XREAD(0x61b8) != 0x75 || MMIO_XREAD(0x61b9) != 8 ||
        MMIO_XREAD(0x618e) != 0x0f)
        return RADIO_NOISE_UNSUPPORTED_STATE;
    if (!out->writes && MMIO_XREAD(0x6189) != 0x40)
        return RADIO_NOISE_UNSUPPORTED_STATE;
    for (i = 0; i < out->writes; i++) {
        value = MMIO_XREAD(settings[i]);
        if (i == 8) value &= 3;
        if (value != (i == 9 ? work.frequency : values[i]))
            return RADIO_NOISE_STATE_CHANGED;
    }
    out->rx_enable = MMIO_XREAD(0x618b);
    out->calibration = MMIO_XREAD(0x6192);
    out->signals = MMIO_XREAD(0x6193);
    out->rssi_valid = MMIO_XREAD(0x6199);
    out->errors = MMIO_READ(SOC_RFERRF);
    out->flags0 = MMIO_READ(SOC_RFIRQF0);
    out->flags1 = MMIO_READ(SOC_RFIRQF1);
    if (out->errors) return RADIO_NOISE_CONTROLLER_ERROR;
    if ((out->rx_enable & 0x7fu) || (out->calibration & 0x80u) ||
        (out->signals & 0xe2u) || (out->rssi_valid & 0xfeu) ||
        (out->flags0 & 0x7fu) || (out->flags1 & 0xfbu))
        return RADIO_NOISE_STATE_CHANGED;
    if (MMIO_XREAD(0x619b) || MMIO_XREAD(0x619c) ||
        MMIO_XREAD(0x619d) || MMIO_XREAD(0x619e) || MMIO_XREAD(0x619f) ||
        MMIO_XREAD(0x61a1) || MMIO_XREAD(0x61a2))
        return RADIO_NOISE_FIFO_ERROR;
    return RADIO_NOISE_OK;
}

static uint8_t idle(void)
{
    return !out->rx_enable && !(out->calibration & 0x40u) && !(out->signals & 0x27u);
}

static uint8_t active(void)
{
    return out->rx_enable == 0x80 && !(out->calibration & 0x40u) &&
           (out->signals & 5u) == 5u && out->rssi_valid == 1;
}

static radio_noise_result_t poll(void)
{
    uint32_t now;
    bool expired;
    radio_noise_result_t result;
    if (out->polls == owned.limit) return RADIO_NOISE_WORK_LIMIT;
    result = observe();
    now = timebase_read_awake_ticks24();
    out->polls++;
    out->elapsed_ticks = (now - work.start) & TIMEBASE_TICKS_MASK;
    if (result != RADIO_NOISE_OK) return result;
    out->timebase_status = timebase_expired(now, work.deadline, &expired);
    if (out->timebase_status != TIMEBASE_OK || out->elapsed_ticks >= TIMEBASE_HALF_RANGE ||
        ((now - work.previous) & TIMEBASE_TICKS_MASK) >= TIMEBASE_HALF_RANGE)
        return RADIO_NOISE_TIME_ERROR;
    if (expired) return RADIO_NOISE_TIMEOUT;
    work.previous = now;
    return RADIO_NOISE_OK;
}

#define CHECK(call) do { result = (call); if (result != RADIO_NOISE_OK) goto failed; } while (0)
#define REQUIRE(test, error) do { if (!(test)) { result = (error); goto failed; } } while (0)
#define ROOM() REQUIRE(out->polls < owned.limit, RADIO_NOISE_WORK_LIMIT)

radio_noise_result_t radio_noise_collect(const radio_noise_request_t MCU_XDATA *request,
                                          radio_noise_capture_t MCU_XDATA *capture)
{
    radio_noise_result_t result;
    uint16_t req, cap, index;
    uint8_t i, raw;
    if (radio_noise_fault) return (radio_noise_result_t)radio_noise_fault;
    if (radio_noise_used) return RADIO_NOISE_ALREADY_USED;
    if (request == NULL || capture == NULL) return RADIO_NOISE_INVALID_ARGUMENT;
    req = MMIO_XADDRESS(request); cap = MMIO_XADDRESS(capture);
    result = storage(req, sizeof(*request));
    if (result != RADIO_NOISE_OK) return result;
    result = storage(cap, sizeof(*capture));
    if (result != RADIO_NOISE_OK) return result;
    if (overlaps(req, sizeof(*request), cap, sizeof(*capture)))
        return RADIO_NOISE_INVALID_RANGE;
    if (request->channel < 11 || request->channel > 26 ||
        !request->samples || request->samples > RADIO_NOISE_SAMPLES_MAX ||
        !request->interval || !request->timeout || request->timeout >= TIMEBASE_HALF_RANGE ||
        !request->limit)
        return RADIO_NOISE_INVALID_ARGUMENT;
    memcpy(&owned, request, sizeof(owned));
    out = capture;
    memset(out, 0, sizeof(*out));
    radio_noise_used = 1;
    work.clock = MMIO_READ(SOC_CLKCONCMD);
    work.frequency = (uint8_t)(11u + 5u * (owned.channel - 11u));
    CHECK(observe());
    REQUIRE(idle(), RADIO_NOISE_BUSY);
    work.start = timebase_read_awake_ticks24();
    work.previous = work.start;
    out->timebase_status = timebase_deadline_after(work.start, owned.timeout, &work.deadline);
    REQUIRE(out->timebase_status == TIMEBASE_OK, RADIO_NOISE_TIME_ERROR);
    out->phase = 1;
    for (i = 0; i < sizeof(values); i++) {
        CHECK(poll());
        REQUIRE(idle(), RADIO_NOISE_STATE_CHANGED);
        ROOM();
        MMIO_XWRITE(settings[i], i == 9 ? work.frequency : values[i]);
        out->writes++;
        CHECK(poll());
        REQUIRE(idle(), RADIO_NOISE_STATE_CHANGED);
        out->verified = out->writes;
    }
    ROOM();
    out->phase = 2;
    MMIO_WRITE(SOC_RFST, 0xe3);
    out->actions = 1;
    do {
        CHECK(poll());
        REQUIRE(out->rx_enable == 0x80, RADIO_NOISE_STATE_CHANGED);
    } while (!active());
    work.after = work.previous;
    out->phase = 3;
    while (out->samples < owned.samples) {
        do {
            CHECK(poll());
            REQUIRE(active(), RADIO_NOISE_STATE_CHANGED);
            work.gap = (work.previous - work.after) & TIMEBASE_TICKS_MASK;
        } while (work.gap < owned.interval);
        ROOM();
        work.before = work.previous;
        if (!out->samples) out->first_before = work.before;
        raw = MMIO_XREAD(0x61a7);
        out->last_raw = raw;
        index = out->samples;
        out->data[index >> 3] |= (uint8_t)((raw & 1u) << (index & 7u));
        out->samples++;
        REQUIRE(!(raw & 0xfcu), RADIO_NOISE_BAD_SAMPLE);
        CHECK(poll());
        REQUIRE(active(), RADIO_NOISE_STATE_CHANGED);
        work.after = work.previous;
        work.span = (work.after - work.before) & TIMEBASE_TICKS_MASK;
        if (out->timed_samples) {
            if (work.gap < out->min_gap) out->min_gap = work.gap;
            if (work.gap > out->max_gap) out->max_gap = work.gap;
        } else out->min_gap = out->max_gap = work.gap;
        if (work.span > out->max_span) out->max_span = work.span;
        out->last_after = work.after;
        out->timed_samples++;
    }
    ROOM();
    out->phase = 4;
    MMIO_XWRITE(0x618d, 0x80);
    out->actions |= 2;
    do {
        CHECK(poll());
        REQUIRE(!out->rx_enable, RADIO_NOISE_STATE_CHANGED);
    } while (!idle());
    out->phase = 5;
    return RADIO_NOISE_OK;
failed:
    radio_noise_fault = result;
    return result;
}

MCU_XDATA uint8_t radio_noise_reserved_end;
