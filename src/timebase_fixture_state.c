/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "timebase_fixture.h"

volatile MCU_XDATA timebase_fixture_t timebase_fixture_state;
static MCU_XDATA uint32_t start_ticks, deadline_ticks, previous_ticks;
static MCU_XDATA uint16_t polls;

static void put16(volatile MCU_XDATA uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
}

static void put24(volatile MCU_XDATA uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
    out[2] = (uint8_t)(value >> 16);
}

static void fault(uint8_t reason)
{
    timebase_fixture_state.reason = reason;
    timebase_fixture_state.phase = TIMEBASE_FIXTURE_FAULT;
}

void timebase_fixture_initialize(void)
{
    uint8_t i;
    volatile MCU_XDATA uint8_t *bytes = (volatile MCU_XDATA uint8_t *)&timebase_fixture_state;

    bringup_initialize();
    for (i = 0; i < TIMEBASE_FIXTURE_SIZE; i++)
        bytes[i] = 0;
    start_ticks = deadline_ticks = previous_ticks = 0;
    polls = 0;
    timebase_fixture_state.abi_version = 1;
    timebase_fixture_state.byte_size = TIMEBASE_FIXTURE_SIZE;
    put16(timebase_fixture_state.delay, TIMEBASE_FIXTURE_DELAY);
    put16(timebase_fixture_state.poll_limit, TIMEBASE_FIXTURE_POLL_LIMIT);
    timebase_fixture_state.guards[0] = 0x69;
    timebase_fixture_state.guards[1] = 0x96;
    timebase_fixture_state.signature[0] = 'M';
    timebase_fixture_state.signature[1] = '2';
    timebase_fixture_state.signature[2] = 'T';
    timebase_fixture_state.signature[3] = 'M';
    timebase_fixture_state.phase = TIMEBASE_FIXTURE_INITIALIZED;
}

void timebase_fixture_begin(void)
{
    timebase_result_t status;

    if (timebase_fixture_state.phase == TIMEBASE_FIXTURE_FAULT)
        return;
    if (timebase_fixture_state.phase != TIMEBASE_FIXTURE_INITIALIZED &&
        timebase_fixture_state.phase != TIMEBASE_FIXTURE_READY) {
        fault(TIMEBASE_FIXTURE_BAD_PHASE);
        return;
    }
    timebase_fixture_state.phase = TIMEBASE_FIXTURE_RUNNING;
    timebase_fixture_state.reason = TIMEBASE_FIXTURE_NONE;
    timebase_fixture_state.helper_status = TIMEBASE_OK;
    polls = 0;
    put16(timebase_fixture_state.polls, 0);
    put24(timebase_fixture_state.end, 0);
    put24(timebase_fixture_state.deadline, 0);
    put24(timebase_fixture_state.elapsed, 0);
    start_ticks = timebase_read_awake_ticks24();
    previous_ticks = start_ticks;
    put24(timebase_fixture_state.start, start_ticks);
    status = timebase_deadline_after(start_ticks, TIMEBASE_FIXTURE_DELAY, &deadline_ticks);
    timebase_fixture_state.helper_status = (uint8_t)status;
    if (status != TIMEBASE_OK) {
        fault(TIMEBASE_FIXTURE_DEADLINE_ERROR);
        return;
    }
    put24(timebase_fixture_state.deadline, deadline_ticks);
}

void timebase_fixture_poll(void)
{
    uint32_t now, elapsed;
    bool expired;
    timebase_result_t status;

    if (timebase_fixture_state.phase == TIMEBASE_FIXTURE_FAULT)
        return;
    if (timebase_fixture_state.phase != TIMEBASE_FIXTURE_RUNNING) {
        fault(TIMEBASE_FIXTURE_BAD_PHASE);
        return;
    }
    now = timebase_read_awake_ticks24();
    polls++;
    put16(timebase_fixture_state.polls, polls);
    put24(timebase_fixture_state.end, now);
    elapsed = (now - start_ticks) & TIMEBASE_TICKS_MASK;
    put24(timebase_fixture_state.elapsed, elapsed);
    status = timebase_expired(now, deadline_ticks, &expired);
    timebase_fixture_state.helper_status = (uint8_t)status;
    if (status != TIMEBASE_OK) {
        fault(TIMEBASE_FIXTURE_EXPIRY_ERROR);
        return;
    }
    if (elapsed >= TIMEBASE_HALF_RANGE ||
        ((now - previous_ticks) & TIMEBASE_TICKS_MASK) >= TIMEBASE_HALF_RANGE ||
        (expired && elapsed < TIMEBASE_FIXTURE_DELAY)) {
        fault(TIMEBASE_FIXTURE_CLOCK_RANGE);
        return;
    }
    previous_ticks = now;
    if (expired) {
        timebase_fixture_state.completed_cycles++;
        bringup_tick();
        timebase_fixture_state.phase = TIMEBASE_FIXTURE_READY;
    } else if (polls == TIMEBASE_FIXTURE_POLL_LIMIT) {
        fault(TIMEBASE_FIXTURE_POLL_LIMIT_REACHED);
    }
}
