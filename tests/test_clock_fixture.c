/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "clock_fixture.h"
#include "timebase.h"
#include "host_mmio.h"

#include <assert.h>
#include <stdio.h>

static uint32_t ticks, latched, start;
static unsigned samples, timer_byte, seen_writes, pending_polls, mode, sleep_reads;
static uint8_t last_address, last_value, boot[32], preserved[256];
static unsigned total_reads;

static uint32_t get(const volatile uint8_t *data, unsigned size)
{
    uint32_t result = 0;
    unsigned i;
    for (i = 0; i < size; i++)
        result |= (uint32_t)data[i] << (8u * i);
    return result;
}

static void consume_read(void)
{
    if (read_count) {
        assert(read_count == 1 && reads[0].address == last_address && reads[0].value == last_value);
        read_count = 0;
    }
}

static uint8_t read_model(uint8_t address, uint8_t value)
{
    consume_read();
    if (write_count != seen_writes) {
        assert(write_count == seen_writes + 1 && writes[seen_writes].address == 0xc6);
        seen_writes = write_count;
        pending_polls = 0;
    }
    if (address >= 0x95 && address <= 0x97) {
        assert(clock_fixture_state.phase == CLOCK_FIXTURE_RUNNING);
        assert(address == 0x95 + timer_byte);
        if (timer_byte == 0) {
            if (samples == 0) start = ticks;
            if (samples == 1 && (mode == 1 || mode == 7 || mode == 8))
                ticks = (start + 2048) & TIMEBASE_TICKS_MASK;
            if (samples == 1 && mode == 2) ticks = (start - 1) & TIMEBASE_TICKS_MASK;
            if (samples == 1 && mode == 3) ticks = (start + 1024 + TIMEBASE_HALF_RANGE) & TIMEBASE_TICKS_MASK;
            latched = ticks;
            samples++;
        }
        value = (uint8_t)(latched >> (8u * timer_byte));
        timer_byte = (timer_byte + 1) % 3;
        if (mode != 4 && mode != 5) ticks = (ticks + 1) & TIMEBASE_TICKS_MASK;
    } else {
        assert(timer_byte == 0);
        assert(address == 0xc6 || address == 0x9e || address == 0xbe ||
               address == 0xa8 || address == 0xb8 || address == 0x9a);
        if (address == 0x9e && seen_writes) {
            pending_polls++;
            if (mode >= 1 && mode <= 3 && seen_writes == 2)
                SOC_CLKCONSTA = pending_polls == 2 ? 0x89 : 0xc9;
            else if (mode == 8)
                SOC_CLKCONSTA = SOC_CLKCONCMD;
            else if (mode == 5 && seen_writes == 2)
                SOC_CLKCONSTA = 0x88;
            else if (mode != 4 && mode != 5 && mode != 7 && pending_polls >= 3)
                SOC_CLKCONSTA = (SOC_CLKCONCMD & 0x78) == 0x40 ? SOC_CLKCONCMD | 8u : SOC_CLKCONCMD;
            value = SOC_CLKCONSTA;
        }
        if (address == 0xbe && clock_fixture_state.phase == CLOCK_FIXTURE_RUNNING) {
            sleep_reads++;
            if (mode == 6 && sleep_reads == 2)
                value = SOC_SLEEPCMD ^= 0x80;
        }
    }
    last_address = address;
    last_value = value;
    total_reads++;
    return value;
}

static void prepare(uint8_t command)
{
    unsigned i;
    host_mmio_reset();
    SOC_P0 = SOC_P1 = SOC_P2 = 0xff;
    SOC_CLKCONCMD = command;
    SOC_CLKCONSTA = (command & 0x78) == 0x40 ? command | 8u : command;
    SOC_SLEEPCMD = 4;
    assert(_sdcc_external_startup() == 0);
    assert(writes[0].address == 0xa8 && writes[1].address == 0xb8 && writes[2].address == 0x9a);
    seen_writes = write_count;
    samples = timer_byte = pending_polls = total_reads = sleep_reads = mode = 0;
    host_mmio_read_hook = read_model;
    clock_fixture_initialize();
    assert(clock_fixture_state.phase == CLOCK_FIXTURE_INITIALIZED);
    assert(clock_fixture_state.clock_result == CLOCK_NOT_ATTEMPTED);
    assert(clock_fixture_state.diagnostics[18] == CLOCK_NOT_ATTEMPTED);
    assert(get(clock_fixture_state.timeout, 3) == 1024 && get(clock_fixture_state.poll_limit, 2) == 4096);
    assert(write_count == seen_writes);
    consume_read();
    write_count = seen_writes = 0;
    for (i = 0; i < sizeof(boot); i++) boot[i] = ((const volatile uint8_t *)&m0_status)[i];
#define SAVE(name, address) preserved[address] = name;
    CC2530_REGISTER_LIST(SAVE)
#undef SAVE
}

static void check_immutable(void)
{
    unsigned i;
    const volatile uint8_t *status = (const volatile uint8_t *)&m0_status;
    for (i = 0; i < sizeof(boot); i++) assert(i == 8 || status[i] == boot[i]);
    for (i = 0; i < 8; i++) assert(clock_fixture_state.reserved[i] == 0);
    assert(clock_fixture_state.guards[0] == 0x69 && clock_fixture_state.guards[1] == 0x96);
#define CHECK_REGISTER(name, address) \
    if (address != 0xc6 && address != 0x9e && address != 0xbe) assert(name == preserved[address]);
    CC2530_REGISTER_LIST(CHECK_REGISTER)
#undef CHECK_REGISTER
}

static void cycle(void)
{
    consume_read();
    assert(timer_byte == 0);
    /* Previous writes have been checked by the caller; no log is silently lost. */
    write_count = seen_writes = samples = pending_polls = sleep_reads = 0;
    clock_fixture_cycle();
    consume_read();
    assert(timer_byte == 0);
    check_immutable();
}

static void latched_fault(uint8_t reason, uint8_t result)
{
    uint8_t bytes[56], heartbeat = m0_status.heartbeat;
    const volatile uint8_t *state = (const volatile uint8_t *)&clock_fixture_state;
    unsigned i, reads_before = total_reads, writes_before = write_count;
    assert(clock_fixture_state.phase == 4 && clock_fixture_state.reason == reason);
    assert(clock_fixture_state.clock_result == result);
    for (i = 0; i < sizeof(bytes); i++) bytes[i] = state[i];
    clock_fixture_cycle();
    clock_fixture_cycle();
    assert(total_reads == reads_before && write_count == writes_before && m0_status.heartbeat == heartbeat);
    for (i = 0; i < sizeof(bytes); i++) assert(bytes[i] == state[i]);
}

int main(void)
{
    unsigned fields, step, failure;
    for (fields = 0; fields < 16; fields++) {
        uint8_t command = (uint8_t)(((fields & 8) << 4) | ((fields & 7) << 3) | 0x41);
        prepare(command);
        ticks = 0xfffffeUL;
        for (step = 0; step < 771; step++) {
            unsigned stage = step % 3;
            uint8_t target = stage == 1 ? command & 0xb8u : command;
            cycle();
            assert(clock_fixture_state.phase == 3 && clock_fixture_state.reason == 0);
            assert(clock_fixture_state.stage == stage && clock_fixture_state.requested_source == (stage == 1));
            assert(clock_fixture_state.completed_steps == (uint8_t)(step + 1));
            assert(m0_status.heartbeat == (uint8_t)(step + 1));
            assert(clock_fixture_state.clock_result == CLOCK_OK);
            assert(clock_fixture_state.current_clock_command == target && SOC_CLKCONCMD == target);
            assert(clock_fixture_state.current_clock_status == ((target & 0x78) == 0x40 ? target | 8u : target));
            assert(clock_fixture_state.diagnostics[18] == CLOCK_NOT_ATTEMPTED);
            assert(write_count == (stage == 0 ? 0 : 1));
            if (stage) assert(writes[0].address == 0xc6 && writes[0].after == target);
            assert(samples == (stage == 0 ? 0 : 4));
            assert(get(clock_fixture_state.diagnostics, 4) == (stage == 0 ? 0 : 9));
            assert(get(clock_fixture_state.diagnostics + 4, 2) == (stage == 0 ? 0 : 3));
            assert(get(clock_fixture_state.diagnostics + 7, 4) == 0);
            assert(get(clock_fixture_state.diagnostics + 11, 2) == 0);
        }
    }
    for (failure = 1; failure <= 5; failure++) {
        uint8_t result = failure == 1 ? CLOCK_TIMEOUT : failure == 2 ? CLOCK_COUNTER_RANGE :
                         failure == 3 ? CLOCK_TIMEBASE_ERROR : CLOCK_POLL_LIMIT;
        prepare(0xc9); cycle(); assert(write_count == 0);
        mode = failure; ticks = 100; cycle();
        latched_fault(CLOCK_FIXTURE_DRIVER_ERROR, result);
        assert(write_count == 2 && writes[0].after == 0x88 && writes[1].after == 0xc9);
        assert(clock_fixture_state.completed_steps == 1 && m0_status.heartbeat == 1);
        assert(clock_fixture_state.diagnostics[18] == (failure == 5 ? CLOCK_POLL_LIMIT :
                                                      failure == 4 ? CLOCK_ROLLBACK_UNCONFIRMED : CLOCK_OK));
        assert(clock_fixture_state.diagnostics[6] == (failure == 3 ? TIMEBASE_AMBIGUOUS : TIMEBASE_OK));
        assert(get(clock_fixture_state.diagnostics + 4, 2) == (failure >= 4 ? 4096 : 1));
        assert(get(clock_fixture_state.diagnostics + 11, 2) == (failure >= 4 ? 4096 : 3));
        assert(samples == 2u + get(clock_fixture_state.diagnostics + 4, 2) +
               get(clock_fixture_state.diagnostics + 11, 2));
    }
    for (failure = 7; failure <= 8; failure++) {
        prepare(0xc9); cycle(); mode = failure; ticks = 100; cycle();
        latched_fault(CLOCK_FIXTURE_DRIVER_ERROR, CLOCK_TIMEOUT);
        assert(write_count == 2 && writes[0].after == 0x88 && writes[1].after == 0xc9);
        assert(clock_fixture_state.diagnostics[18] == (failure == 7 ? CLOCK_ROLLBACK_UNCONFIRMED : CLOCK_OK));
        assert(get(clock_fixture_state.diagnostics + 11, 2) == (failure == 7 ? 342 : 1));
        assert(get(clock_fixture_state.diagnostics + 7, 4) == (failure == 7 ? 1026 : 3));
        assert(clock_fixture_state.completed_steps == 1 && m0_status.heartbeat == 1);
    }
    prepare(0xc9); mode = 6; cycle(); assert(write_count == 0);
    latched_fault(CLOCK_FIXTURE_INVARIANT, CLOCK_OK);
    prepare(0xc9); clock_fixture_state.phase = CLOCK_FIXTURE_RUNNING;
    clock_fixture_cycle();
    latched_fault(CLOCK_FIXTURE_BAD_PHASE, CLOCK_NOT_ATTEMPTED);
    for (failure = 0; failure < 4; failure++) {
        prepare(0xc9);
        if (failure == 0) SOC_SLEEPCMD = 0;
        if (failure == 1) SOC_IEN1 = 1;
        if (failure == 2) SOC_CLKCONCMD = SOC_CLKCONSTA = 0x88;
        if (failure == 3) SOC_CLKCONSTA = 0x88;
        clock_fixture_initialize();
        latched_fault(CLOCK_FIXTURE_INVARIANT, CLOCK_NOT_ATTEMPTED);
        assert(write_count == 0);
    }
    puts("host clock fixture: 16 field combinations x771 steps, serialized ABI, rollover, "
         "timeouts/stall/ambiguity/backwards, rollback, invariant/latched faults PASS");
    return 0;
}
