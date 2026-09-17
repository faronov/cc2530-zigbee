/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 * Isolated synthetic test executable. NEVER flash this file's linked image.
 */
#include "clock.h"
#include "cc2530_mmio.h"
#include "timebase.h"

#include <stddef.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) return (uint16_t)__LINE__; } while (0)

static uint16_t invalid_arguments(void)
{
    clock_diagnostics_t output, saved;
    uint8_t bit;

    memset(&output, 0xa5, sizeof(output));
    memcpy(&saved, &output, sizeof(saved));
    CHECK(clock_select_init((clock_source_t)2, 1, 1, &output) == CLOCK_INVALID_ARGUMENT);
    CHECK(clock_select_init((clock_source_t)-1, 1, 1, &output) == CLOCK_INVALID_ARGUMENT);
    CHECK(clock_select_init(CLOCK_RC16, 0, 0, &output) == CLOCK_INVALID_ARGUMENT);
    CHECK(clock_select_init(CLOCK_XOSC32, 0, 1, NULL) == CLOCK_INVALID_ARGUMENT);
    for (bit = 23; bit < 32; bit++)
        CHECK(clock_select_init(CLOCK_XOSC32, 1UL << bit, 1, &output) == CLOCK_INVALID_ARGUMENT);
    CHECK(clock_select_init(CLOCK_RC16, 0xffffffffUL, 65535u, &output) == CLOCK_INVALID_ARGUMENT);
    CHECK(memcmp(&output, &saved, sizeof(output)) == 0);
    return 0;
}

#if defined(__SDCC)
volatile MCU_XDATA MCU_AT(0x1e00) uint8_t clock_test_result[8];
MCU_XDATA clock_diagnostics_t clock_test_diagnostics;
volatile MCU_XDATA uint8_t clock_test_source;
volatile MCU_XDATA uint32_t clock_test_timeout;
volatile MCU_XDATA uint16_t clock_test_limit;
volatile MCU_XDATA uint8_t clock_test_return;

void clock_test_cycle(void)
{
    __asm
        .globl _clock_test_before
    _clock_test_before:
        nop
    __endasm;
    clock_test_return = clock_select_init((clock_source_t)clock_test_source, clock_test_timeout,
                                          clock_test_limit, &clock_test_diagnostics);
    __asm
        .globl _clock_test_done
    _clock_test_done:
        nop
    __endasm;
}

void main(void)
{
    uint16_t result = invalid_arguments();
    clock_test_result[0] = 'C';
    clock_test_result[1] = 'L';
    clock_test_result[2] = 'K';
    clock_test_result[3] = 'T';
    clock_test_result[4] = 1;
    clock_test_result[5] = 8;
    clock_test_result[6] = (uint8_t)result;
    clock_test_result[7] = (uint8_t)(result >> 8);
    for (;;)
        clock_test_cycle();
}
#elif defined(CLOCK_FAILURE_TEST)
#include "host_mmio.h"

#include <assert.h>
#include <stdio.h>

static unsigned samples, deadlines_called, expiries_called, failure_mask;
static bool fail_deadline;

uint32_t timebase_read_awake_ticks24(void)
{
    return samples++;
}

timebase_result_t timebase_deadline_after(uint32_t now, uint32_t delay, uint32_t *deadline)
{
    assert(now <= TIMEBASE_TICKS_MASK && delay == 10 && deadline != NULL);
    if (fail_deadline && (failure_mask & (1u << deadlines_called++)))
        return TIMEBASE_INVALID_ARGUMENT;
    *deadline = now + delay;
    return TIMEBASE_OK;
}

timebase_result_t timebase_expired(uint32_t now, uint32_t deadline, bool *expired)
{
    assert(now < deadline && expired != NULL);
    if (!fail_deadline && (failure_mask & (1u << expiries_called++)))
        return TIMEBASE_INVALID_ARGUMENT;
    *expired = false;
    return TIMEBASE_OK;
}

static uint8_t failure_read(uint8_t address, uint8_t value)
{
    static const uint8_t order[] = {0xa8, 0xb8, 0x9a, 0xbe, 0xc6, 0x9e};
    assert(address == (read_count < 6 ? order[read_count] : (read_count & 1) ? 0x9e : 0xc6));
    return address == 0x9e ? SOC_CLKCONCMD : value;
}

int main(void)
{
    clock_diagnostics_t diagnostics;
    unsigned mode;
    host_mmio_reset();
    assert(invalid_arguments() == 0 && read_count == 0 && write_count == 0);
    for (mode = 0; mode < 4; mode++) {
        host_mmio_reset();
        SOC_CLKCONCMD = SOC_CLKCONSTA = 0xc9;
        SOC_SLEEPCMD = 4;
        host_mmio_read_hook = failure_read;
        samples = deadlines_called = expiries_called = 0;
        fail_deadline = mode < 2;
        failure_mask = mode & 1 ? 3 : 1;
        assert(clock_select_init(CLOCK_XOSC32, 10, 1, &diagnostics) == CLOCK_TIMEBASE_ERROR);
        assert(write_count == 2 && writes[0].address == 0xc6 && writes[0].after == 0x88);
        assert(writes[1].address == 0xc6 && writes[1].before == 0x88 && writes[1].after == 0xc9);
        assert(diagnostics.request.timebase_status == TIMEBASE_INVALID_ARGUMENT);
        assert(diagnostics.request.polls == (fail_deadline ? 0 : 1));
        assert(diagnostics.rollback_result == (mode & 1 ? CLOCK_TIMEBASE_ERROR : CLOCK_OK));
        assert(diagnostics.rollback.timebase_status == (mode & 1 ? TIMEBASE_INVALID_ARGUMENT : TIMEBASE_OK));
        assert(diagnostics.rollback.polls == ((mode == 1) ? 0 : 1));
        assert(samples == 2u + diagnostics.request.polls + diagnostics.rollback.polls);
        assert(read_count == 10 && diagnostics.observed_command == 0xc9 && diagnostics.observed_status == 0xc9);
        assert(SOC_CLKCONCMD == 0xc9 && SOC_CLKCONSTA == 0xc9 && SOC_SLEEPCMD == 4);
    }
    puts("host clock: isolated invalid-helper doubles, unconditional cancellation and rollback errors PASS");
    return 0;
}
#else
#include "host_mmio.h"

#include <assert.h>
#include <stdio.h>

/* Host-only scripts check every read and write boundary, including 65535 polls.
 * Consume each checked log entry before the next, never overflow/drop a log.
 */
static struct {
    uint8_t address, value, writes_before;
} script[700000];
static unsigned length, position, expected_writes;
static uint8_t expected_commands[2], initial_registers[256];
static struct {
    uint8_t before[4];
    clock_diagnostics_t value;
    uint8_t after[4];
} guarded_diagnostics;
#define diagnostics guarded_diagnostics.value
static uint32_t timeout;
static uint16_t limit;
static clock_source_t source;

static uint8_t read_hook(uint8_t address, uint8_t value)
{
    (void)value;
    if (position != 0) {
        assert(read_count == 1);
        assert(reads[0].address == script[position - 1].address);
        assert(reads[0].value == script[position - 1].value);
        read_count = 0;
    }
    assert(read_count == 0 && position < length);
    assert(address == script[position].address);
    assert(write_count == script[position].writes_before);
    return script[position++].value;
}

static void add_read(uint8_t address, uint8_t value)
{
    assert(length < sizeof(script) / sizeof(script[0]));
    script[length].address = address;
    script[length].value = value;
    script[length++].writes_before = (uint8_t)expected_writes;
}

static void sample(uint32_t ticks)
{
    add_read(0x95, (uint8_t)ticks);
    add_read(0x96, (uint8_t)(ticks >> 8));
    add_read(0x97, (uint8_t)(ticks >> 16));
}

static uint8_t effective(uint8_t command)
{
    return (command & 0x78) == 0x40 ? command | 8u : command;
}

static void prepare(uint8_t command, uint8_t status)
{
    host_mmio_reset();
#define SEED(name, address) name = (uint8_t)((address) ^ 0x69);
    CC2530_REGISTER_LIST(SEED)
#undef SEED
    SOC_IEN0 = SOC_IEN1 = SOC_IEN2 = 0;
    SOC_SLEEPCMD = 0x84;
    SOC_CLKCONCMD = command;
    SOC_CLKCONSTA = status;
    length = position = expected_writes = 0;
    host_mmio_read_hook = read_hook;
    timeout = 10;
    limit = 3;
    source = command & 0x40 ? CLOCK_XOSC32 : CLOCK_RC16;
    memset(guarded_diagnostics.before, 0x69, sizeof(guarded_diagnostics.before));
    memset(guarded_diagnostics.after, 0x96, sizeof(guarded_diagnostics.after));
    memset(&diagnostics, 0xa5, sizeof(diagnostics));
}

static void entry(void)
{
#define SAVE(name, address) initial_registers[address] = name;
    CC2530_REGISTER_LIST(SAVE)
#undef SAVE
    add_read(0xa8, SOC_IEN0);
    add_read(0xb8, SOC_IEN1);
    add_read(0x9a, SOC_IEN2);
    add_read(0xbe, SOC_SLEEPCMD);
    add_read(0xc6, SOC_CLKCONCMD);
    add_read(0x9e, SOC_CLKCONSTA);
}

static void request(uint8_t command, uint32_t start)
{
    sample(start);
    assert(expected_writes < 2);
    expected_commands[expected_writes++] = command;
}

static void poll(uint8_t command, uint8_t status, uint32_t ticks)
{
    add_read(0xc6, command);
    add_read(0x9e, status);
    sample(ticks);
}

static void run(clock_result_t result, uint16_t polls, uint32_t elapsed,
                clock_result_t rollback, uint16_t rollback_polls, uint32_t rollback_elapsed)
{
    unsigned i;
    assert(clock_select_init(source, timeout, limit, &diagnostics) == result);
    assert(memcmp(guarded_diagnostics.before, "\x69\x69\x69\x69", 4) == 0);
    assert(memcmp(guarded_diagnostics.after, "\x96\x96\x96\x96", 4) == 0);
    assert(position == length && read_count == 1);
    assert(reads[0].address == script[length - 1].address);
    assert(reads[0].value == script[length - 1].value);
    assert(write_count == expected_writes);
    for (i = 0; i < write_count; i++) {
        assert(writes[i].address == 0xc6 && writes[i].after == expected_commands[i]);
        assert(writes[i].before == (i ? expected_commands[i - 1] : initial_registers[0xc6]));
    }
#define UNCHANGED(name, address) \
    if (address != 0xc6) assert(name == initial_registers[address]);
    CC2530_REGISTER_LIST(UNCHANGED)
#undef UNCHANGED
    assert(SOC_CLKCONCMD == (write_count ? expected_commands[write_count - 1] : initial_registers[0xc6]));
    assert(diagnostics.saved_command == initial_registers[0xc6]);
    assert(diagnostics.requested_command ==
           ((initial_registers[0xc6] & 0xb8) | (source == CLOCK_RC16 ? 0x41 : 0)));
    assert(diagnostics.request.polls == polls && diagnostics.request.elapsed_ticks == elapsed);
    assert(diagnostics.rollback_result == rollback && diagnostics.rollback.polls == rollback_polls);
    assert(diagnostics.rollback.elapsed_ticks == rollback_elapsed);
    if (result != CLOCK_TIMEBASE_ERROR)
        assert(diagnostics.request.timebase_status == TIMEBASE_OK);
    if (rollback != CLOCK_TIMEBASE_ERROR)
        assert(diagnostics.rollback.timebase_status == TIMEBASE_OK);
    /* Last complete CMD/STA observation, not the command we hoped would settle. */
    for (i = length; i-- != 0;) {
        if (script[i].address == 0x9e) {
            assert(diagnostics.observed_status == script[i].value);
            assert(i && diagnostics.observed_command == script[i - 1].value);
            break;
        }
    }
}

static void entry_tests(void)
{
    unsigned command, status, i;
    for (command = 0; command < 256; command++) {
        for (status = 0; status < 256; status++) {
            bool valid = (command & 7) == ((command & 0x40) ? 1u : 0u) &&
                         status == effective((uint8_t)command);
            prepare((uint8_t)command, (uint8_t)status);
            source = command & 0x40 ? CLOCK_RC16 : CLOCK_XOSC32;
            timeout = 0;
            entry();
            run(valid ? CLOCK_OK : CLOCK_UNSUPPORTED_STATE, 0, 0, CLOCK_NOT_ATTEMPTED, 0, 0);
        }
    }
    for (i = 0; i < 24; i++) {
        prepare(0xc9, 0xc9);
        if (i < 8) SOC_IEN0 = (uint8_t)(1u << i);
        else if (i < 16) SOC_IEN1 = (uint8_t)(1u << (i - 8));
        else SOC_IEN2 = (uint8_t)(1u << (i - 16));
        entry();
        run(CLOCK_UNSUPPORTED_STATE, 0, 0, CLOCK_NOT_ATTEMPTED, 0, 0);
    }
    for (i = 0; i < 8; i++) {
        prepare(0xc9, 0xc9);
        SOC_SLEEPCMD = (uint8_t)i;
        source = CLOCK_RC16;
        entry();
        run(i == 4 ? CLOCK_OK : CLOCK_UNSUPPORTED_STATE, 0, 0, CLOCK_NOT_ATTEMPTED, 0, 0);
    }
}

static void transition_tests(void)
{
    unsigned fields, direction, i;
    uint8_t saved, target;
    static const uint32_t starts[] = {0, 0xfe, 0xfffe, 0xfffffe};
    for (fields = 0; fields < 16; fields++) {
        for (direction = 0; direction < 2; direction++) {
            saved = (uint8_t)(((fields & 8) << 4) | ((fields & 7) << 3) |
                              (direction ? 0 : 0x41));
            target = (saved & 0xb8) | (direction ? 0x41 : 0);
            for (i = 0; i < sizeof(starts) / sizeof(starts[0]); i++) {
                prepare(saved, effective(saved)); entry();
                request(target, starts[i]);
                poll(target, effective(saved), (starts[i] + 1) & TIMEBASE_TICKS_MASK);
                poll(target, effective(target), (starts[i] + 2) & TIMEBASE_TICKS_MASK);
                run(CLOCK_OK, 2, 2, CLOCK_NOT_ATTEMPTED, 0, 0);
            }
        }
    }
    /* First/final permitted poll and exact deadline, including zero delay. */
    for (i = 0; i < 2; i++) {
        prepare(0xc1, 0xc9); limit = 1; timeout = i ? 0 : 10; entry();
        request(0x80, 100); poll(0x80, 0x80, 100 + timeout);
        run(CLOCK_OK, 1, timeout, CLOCK_NOT_ATTEMPTED, 0, 0);
    }
    prepare(0x80, 0x80); timeout = 0x7fffffUL; entry();
    request(0xc1, 0xffffffUL); poll(0xc1, 0xc9, 0x7ffffeUL);
    run(CLOCK_OK, 1, 0x7fffffUL, CLOCK_NOT_ATTEMPTED, 0, 0);
}

static void failure_tests(void)
{
    unsigned i, late;
    /* Pending at deadline and late confirmation both fail, then restore once. */
    for (late = 0; late < 2; late++) {
        prepare(0xc9, 0xc9); entry(); request(0x88, 100);
        poll(0x88, late ? 0x88 : 0xc9, 110 + late);
        request(0xc9, 200); poll(0xc9, 0x88, 201); poll(0xc9, 0xc9, 202);
        run(CLOCK_TIMEOUT, 1, 10 + late, CLOCK_OK, 2, 2);
    }
    /* Old STA is never success; stopped-counter budgets cannot wrap uint16_t. */
    for (i = 0; i < 2; i++) {
        unsigned count;
        prepare(0xc9, 0xc9); limit = i ? 65535u : 3u; entry(); request(0x88, 0);
        for (count = 0; count < limit; count++) poll(0x88, 0xc9, 0);
        request(0xc9, 0);
        for (count = 0; count < limit; count++) poll(0xc9, 0x88, 0);
        run(CLOCK_POLL_LIMIT, limit, 0, CLOCK_POLL_LIMIT, limit, 0);
    }
    prepare(0xc9, 0xc9); entry(); request(0x88, 100);
    poll(0x88, 0xc9, 101); poll(0x88, 0xc9, 102); poll(0x88, 0x88, 103);
    run(CLOCK_OK, 3, 3, CLOCK_NOT_ATTEMPTED, 0, 0);
    /* Backward before start, backward within window, elapsed half range,
     * and exactly-half deadline ambiguity. No bad observation becomes ready. */
    for (i = 0; i < 4; i++) {
        uint32_t now = i == 0 ? 99 : i == 1 ? 101 : i == 2 ? 0x800064UL : 0x80006eUL;
        prepare(0xc9, 0xc9); entry(); request(0x88, 100);
        if (i == 1) poll(0x88, 0xc9, 102);
        poll(0x88, 0x88, now);
        request(0xc9, 20); poll(0xc9, 0xc9, 21);
        run(i == 3 ? CLOCK_TIMEBASE_ERROR : CLOCK_COUNTER_RANGE,
            i == 1 ? 2 : 1, (now - 100) & TIMEBASE_TICKS_MASK, CLOCK_OK, 1, 1);
        if (i == 3) assert(diagnostics.request.timebase_status == TIMEBASE_AMBIGUOUS);
    }
    /* Rollback has its own deadline/time errors, never converts failure to OK. */
    for (i = 0; i < 4; i++) {
        uint32_t now = i == 0 ? 211 : i == 1 ? 199 : i == 2 ? 0x8000d2UL : 201;
        clock_result_t result = i == 0 ? CLOCK_ROLLBACK_UNCONFIRMED : i == 1 ? CLOCK_COUNTER_RANGE :
                                i == 2 ? CLOCK_TIMEBASE_ERROR : CLOCK_COMMAND_CHANGED;
        prepare(0xc9, 0xc9); entry(); request(0x88, 100); poll(0x88, 0xc9, 110);
        request(0xc9, 200); poll(i == 3 ? 0x88 : 0xc9, 0xc9, now);
        run(CLOCK_TIMEOUT, 1, 10, result, 1, (now - 200) & TIMEBASE_TICKS_MASK);
        if (i == 2) assert(diagnostics.rollback.timebase_status == TIMEBASE_AMBIGUOUS);
    }
    prepare(0xc9, 0xc9); entry(); request(0x88, 100); poll(0x89, 0x88, 101);
    request(0xc9, 200); poll(0xc9, 0xc9, 200);
    run(CLOCK_COMMAND_CHANGED, 1, 1, CLOCK_OK, 1, 0);
    prepare(0xc9, 0xc9); timeout = 0; entry(); request(0x88, 100); poll(0x88, 0xc9, 100);
    request(0xc9, 100); poll(0xc9, 0xc9, 100);
    run(CLOCK_TIMEOUT, 1, 0, CLOCK_ROLLBACK_UNCONFIRMED, 1, 0);
}

static void pending_cancel_tests(void)
{
    unsigned fields, direction, old_polls, i;
    for (fields = 0; fields < 16; fields++) {
        for (direction = 0; direction < 2; direction++) {
            uint8_t saved = (uint8_t)(((fields & 8) << 4) | ((fields & 7) << 3) |
                                      (direction ? 0 : 0x41));
            uint8_t target = (saved & 0xb8) | (direction ? 0x41 : 0);
            uint8_t pending = (saved ^ 0x40) | (direction ? 1u : 0u);
            for (old_polls = 1; old_polls <= 32; old_polls++) {
                prepare(saved, effective(saved)); timeout = 64; limit = 40; entry();
                request(target, 100); poll(target, effective(saved), 165);
                request(saved, 0xfffff0UL);
                for (i = 1; i <= old_polls; i++)
                    poll(saved, effective(saved), (0xfffff0UL + i) & TIMEBASE_TICKS_MASK);
                poll(saved, effective(pending), (0xfffff1UL + old_polls) & TIMEBASE_TICKS_MASK);
                poll(saved, effective(saved), (0xfffff2UL + old_polls) & TIMEBASE_TICKS_MASK);
                run(CLOCK_TIMEOUT, 1, 65, CLOCK_OK, old_polls + 2, old_polls + 2);
            }
        }
    }
    /* A truly canceled request and a still-pending request look identical.
     * Neither a stopped clock nor repeated old STA matches prove drainage. */
    for (i = 0; i < 2; i++) {
        unsigned count;
        prepare(0xc9, 0xc9); limit = i ? 65535u : 3u; entry();
        request(0x88, 100); poll(0x88, 0xc9, 111);
        request(0xc9, 200);
        for (count = 0; count < limit; count++) poll(0xc9, 0xc9, 200);
        run(CLOCK_TIMEOUT, 1, 11, CLOCK_ROLLBACK_UNCONFIRMED, limit, 0);
    }
    prepare(0xc9, 0xc9); entry();
    request(0x88, 100); poll(0x88, 0xc9, 111);
    request(0xc9, 200); poll(0xc9, 0xc9, 201); poll(0xc9, 0xc9, 210);
    run(CLOCK_TIMEOUT, 1, 11, CLOCK_ROLLBACK_UNCONFIRMED, 2, 10);
    /* Seeing the pending source at the last poll is not a return to saved. */
    prepare(0xc9, 0xc9); limit = 2; entry();
    request(0x88, 100); poll(0x88, 0xc9, 111);
    request(0xc9, 200); poll(0xc9, 0xc9, 201); poll(0xc9, 0x89, 202);
    run(CLOCK_TIMEOUT, 1, 11, CLOCK_POLL_LIMIT, 2, 2);
    /* A late source confirmation still supplies evidence for a later return. */
    prepare(0xc9, 0xc9); entry();
    request(0x88, 100); poll(0x88, 0x88, 111);
    request(0xc9, 200); poll(0xc9, 0xc9, 210);
    run(CLOCK_TIMEOUT, 1, 11, CLOCK_OK, 1, 10);
}

int main(void)
{
    uint16_t result;
    host_mmio_reset();
    result = invalid_arguments();
    if (result != 0) {
        fprintf(stderr, "Clock invalid-argument test failed at line %u\n", (unsigned)result);
        return 1;
    }
    assert(read_count == 0 && write_count == 0);
    entry_tests();
    transition_tests();
    failure_tests();
    pending_cancel_tests();
    puts("host clock: all CMD/STA entry pairs, both sources, clamping, raw-time/poll bounds, rollback PASS");
    return 0;
}
#endif
