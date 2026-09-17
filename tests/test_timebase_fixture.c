/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "timebase_fixture.h"
#include "host_mmio.h"

#include <assert.h>
#include <stdio.h>

static uint32_t ticks;
static unsigned samples;

static uint32_t get24(const volatile uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16);
}

static unsigned get16(const volatile uint8_t *bytes)
{
    return (unsigned)bytes[0] | ((unsigned)bytes[1] << 8);
}

static void check_fault(uint8_t reason, uint8_t status)
{
    uint8_t saved[TIMEBASE_FIXTURE_SIZE];
    unsigned i, count = samples;
    uint8_t heartbeat = m0_status.heartbeat;
    const volatile uint8_t *bytes = (const volatile uint8_t *)&timebase_fixture_state;
    assert(timebase_fixture_state.phase == TIMEBASE_FIXTURE_FAULT);
    assert(timebase_fixture_state.reason == reason && timebase_fixture_state.helper_status == status);
    for (i = 0; i < sizeof(saved); i++)
        saved[i] = bytes[i];
    timebase_fixture_begin();
    timebase_fixture_poll();
    assert(samples == count);
    assert(m0_status.heartbeat == heartbeat);
    for (i = 0; i < sizeof(saved); i++)
        assert(bytes[i] == saved[i]);
}

#if defined(TIMEBASE_FIXTURE_FAILURE_TEST)
/* Failure-only link doubles; this executable never links the production reader.
 * Exercise defensive helper errors that genuine 24-bit SFR reads cannot produce.
 */
static timebase_result_t deadline_error, expiry_error;

uint32_t timebase_read_awake_ticks24(void)
{
    samples++;
    return ticks;
}

timebase_result_t timebase_deadline_after(uint32_t now, uint32_t delay, uint32_t *deadline)
{
    assert(now == ticks && delay == 128 && deadline != NULL);
    if (deadline_error != TIMEBASE_OK)
        return deadline_error;
    *deadline = (now + delay) & TIMEBASE_TICKS_MASK;
    return TIMEBASE_OK;
}

timebase_result_t timebase_expired(uint32_t now, uint32_t deadline, bool *expired)
{
    (void)deadline;
    assert(now == ticks && expired != NULL && expiry_error != TIMEBASE_OK);
    return expiry_error;
}

static void exercise(void)
{
    unsigned error;
    assert(get24(timebase_fixture_state.start) == 0);
    assert(get16(timebase_fixture_state.polls) == 0);
    for (error = TIMEBASE_INVALID_ARGUMENT; error <= TIMEBASE_AMBIGUOUS; error++) {
        timebase_fixture_initialize();
        deadline_error = (timebase_result_t)error;
        timebase_fixture_begin();
        check_fault(TIMEBASE_FIXTURE_DEADLINE_ERROR, (uint8_t)error);
        assert(samples == (error - 1) * 3 + 1 && m0_status.heartbeat == 0);
        timebase_fixture_initialize();
        deadline_error = TIMEBASE_OK;
        expiry_error = (timebase_result_t)error;
        timebase_fixture_begin();
        timebase_fixture_poll();
        check_fault(TIMEBASE_FIXTURE_EXPIRY_ERROR, (uint8_t)error);
        assert(m0_status.heartbeat == 0 && timebase_fixture_state.completed_cycles == 0);
    }
}
#else
static uint32_t latched;
static uint8_t sfr_before[256];

static void consume_triplet(void)
{
    unsigned i;
    assert(read_count == 3);
    for (i = 0; i < 3; i++) {
        assert(reads[i].address == 0x95 + i);
        assert(reads[i].value == (uint8_t)(latched >> (8u * i)));
    }
    read_count = 0;
}

static uint8_t read_timer(uint8_t address, uint8_t value)
{
    (void)value;
    if (address == SOC_ST0_ADDRESS) {
        if (samples != 0)
            consume_triplet();
        latched = ticks;
        samples++;
    }
    assert(read_count < 3 && address == 0x95 + read_count);
    assert(timebase_fixture_state.phase == TIMEBASE_FIXTURE_RUNNING);
    assert(timebase_fixture_state.completed_cycles == m0_status.heartbeat);
    return (uint8_t)(latched >> (8u * read_count));
}

static void check_immutable(void)
{
    assert(timebase_fixture_state.reserved[0] == 0 && timebase_fixture_state.reserved[1] == 0);
    assert(timebase_fixture_state.guards[0] == 0x69 && timebase_fixture_state.guards[1] == 0x96);
    assert(get16(timebase_fixture_state.delay) == 128);
    assert(get16(timebase_fixture_state.poll_limit) == 1024);
#define CHECK_REGISTER(name, address) assert(name == sfr_before[address]);
    CC2530_REGISTER_LIST(CHECK_REGISTER)
#undef CHECK_REGISTER
}

static void start(uint32_t value)
{
    ticks = value;
    timebase_fixture_begin();
    assert(timebase_fixture_state.phase == TIMEBASE_FIXTURE_RUNNING);
    assert(get24(timebase_fixture_state.start) == value);
    assert(get24(timebase_fixture_state.deadline) == ((value + 128) & TIMEBASE_TICKS_MASK));
    assert(get16(timebase_fixture_state.polls) == 0);
    assert(get24(timebase_fixture_state.end) == 0 && get24(timebase_fixture_state.elapsed) == 0);
}

static void exercise(void)
{
    unsigned cycle, i, count, operations = write_count;
    uint8_t boot[32];
    const volatile uint8_t *status = (const volatile uint8_t *)&m0_status;
    const volatile uint8_t *state = (const volatile uint8_t *)&timebase_fixture_state;
    static const uint32_t starts[] = {0, 0xff, 0xffffUL, 0xffff80UL, 0xffffffUL};
    static const uint8_t initialized[] = {'M', '2', 'T', 'M', 1, 32, 1};

#define SAVE_REGISTER(name, address) sfr_before[address] = name;
    CC2530_REGISTER_LIST(SAVE_REGISTER)
#undef SAVE_REGISTER
    host_mmio_read_hook = read_timer;
    timebase_fixture_initialize();
    for (i = 0; i < sizeof(initialized); i++)
        assert(state[i] == initialized[i]);
    for (i = 7; i < 24; i++)
        assert(state[i] == 0);
    for (i = 0; i < 32; i++)
        boot[i] = status[i];
    check_immutable();
    for (cycle = 0; cycle < 257; cycle++) {
        uint32_t initial = starts[cycle % 5];
        start(initial);
        for (i = 0; i < 128; i++) {
            ticks = (initial + i) & TIMEBASE_TICKS_MASK;
            timebase_fixture_poll();
            assert(timebase_fixture_state.phase == TIMEBASE_FIXTURE_RUNNING);
            assert(timebase_fixture_state.completed_cycles == (uint8_t)cycle);
            assert(get16(timebase_fixture_state.polls) == i + 1);
        }
        ticks = (initial + 130) & TIMEBASE_TICKS_MASK;
        timebase_fixture_poll();
        assert(timebase_fixture_state.phase == TIMEBASE_FIXTURE_READY);
        assert(timebase_fixture_state.reason == 0 && timebase_fixture_state.helper_status == TIMEBASE_OK);
        assert(get24(timebase_fixture_state.end) == ticks && get24(timebase_fixture_state.elapsed) == 130);
        assert(timebase_fixture_state.completed_cycles == (uint8_t)(cycle + 1));
        assert(m0_status.heartbeat == (uint8_t)(cycle + 1));
        check_immutable();
        for (i = 0; i < 32; i++)
            assert(i == 8 || status[i] == boot[i]);
    }
    count = samples;
    start(0x123456UL);
    for (i = 1; i <= 1024; i++) {
        timebase_fixture_poll();
        assert(get16(timebase_fixture_state.polls) == i);
        assert(timebase_fixture_state.phase == (i == 1024 ? TIMEBASE_FIXTURE_FAULT : TIMEBASE_FIXTURE_RUNNING));
    }
    assert(samples == count + 1025 && timebase_fixture_state.completed_cycles == 1);
    check_fault(TIMEBASE_FIXTURE_POLL_LIMIT_REACHED, TIMEBASE_OK);
    check_immutable();
    timebase_fixture_initialize();
    start(0);
    for (i = 1; i < 1024; i++)
        timebase_fixture_poll();
    ticks = 128;
    timebase_fixture_poll();
    assert(timebase_fixture_state.phase == TIMEBASE_FIXTURE_READY && get16(timebase_fixture_state.polls) == 1024);
    timebase_fixture_poll();
    check_fault(TIMEBASE_FIXTURE_BAD_PHASE, TIMEBASE_OK);
    timebase_fixture_initialize();
    start(0);
    ticks = 0x7fffffUL;
    timebase_fixture_poll();
    assert(timebase_fixture_state.phase == TIMEBASE_FIXTURE_READY);
    assert(get24(timebase_fixture_state.elapsed) == 0x7fffffUL);
    for (i = 0; i < 4; i++) {
        timebase_fixture_initialize();
        start(0x100);
        ticks = i == 0 ? 0xff : i == 1 ? 0x800100UL : i == 2 ? 0x800180UL : 0x110;
        timebase_fixture_poll();
        if (i == 3) {
            ticks = 0x10f;
            timebase_fixture_poll();
        }
        check_fault(i == 2 ? TIMEBASE_FIXTURE_EXPIRY_ERROR : TIMEBASE_FIXTURE_CLOCK_RANGE,
                    i == 2 ? TIMEBASE_AMBIGUOUS : TIMEBASE_OK);
        assert(m0_status.heartbeat == 0 && timebase_fixture_state.completed_cycles == 0);
    }
    timebase_fixture_initialize();
    timebase_fixture_poll();
    check_fault(TIMEBASE_FIXTURE_BAD_PHASE, TIMEBASE_OK);
    timebase_fixture_initialize();
    start(0);
    timebase_fixture_begin();
    check_fault(TIMEBASE_FIXTURE_BAD_PHASE, TIMEBASE_OK);
    check_immutable();
    for (i = 0; i < 3; i++) {
        timebase_fixture_initialize();
        timebase_fixture_state.phase = i == 0 ? 0 : i == 1 ? 5 : 255;
        timebase_fixture_begin();
        check_fault(TIMEBASE_FIXTURE_BAD_PHASE, TIMEBASE_OK);
    }
    consume_triplet();
    assert(write_count == operations);
}
#endif

int main(void)
{
    host_mmio_reset();
    assert(_sdcc_external_startup() == 0);
    exercise();
    printf("host timebase fixture board=%u: bounded phases, helper errors, publication and latched faults PASS\n",
           (unsigned)CC2530_BOARD);
    return 0;
}
