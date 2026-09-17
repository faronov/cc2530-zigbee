/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "timebase.h"
#include "cc2530_mmio.h"

#include <stddef.h>

#define CHECK(condition) do { if (!(condition)) return (uint16_t)__LINE__; } while (0)

static const MCU_CODE uint32_t deadlines[][3] = {
    {0, 0, 0}, {0xffffffUL, 0, 0xffffffUL}, {0, 1, 1},
    {0xff, 1, 0x100}, {0xffffUL, 1, 0x10000UL}, {0xffffffUL, 1, 0},
    {0xfffffeUL, 2, 0}, {0xffff00UL, 0x100, 0},
    {0xff0000UL, 0x10000UL, 0}, {0x123456UL, 0x654321UL, 0x777777UL},
    {0, 0x7fffffUL, 0x7fffffUL}, {0x800000UL, 0x7fffffUL, 0xffffffUL},
    {0x800001UL, 0x7fffffUL, 0}, {0xffffffUL, 0x7fffffUL, 0x7ffffeUL}
};

static const MCU_CODE uint32_t phases[] = {
    0, 1, 0xff, 0x100, 0xffffUL, 0x10000UL, 0x7fffffUL,
    0x800000UL, 0x800001UL, 0xfffffeUL, 0xffffffUL
};

static uint16_t self_test(void)
{
    uint32_t output, now, deadline, invalid;
    uint8_t i, bit, initial;
    bool expired;

    for (i = 0; i < sizeof(deadlines) / sizeof(deadlines[0]); i++) {
        output = 0xa5c76996UL;
        CHECK(timebase_deadline_after(deadlines[i][0], deadlines[i][1], &output) == TIMEBASE_OK);
        CHECK(output == deadlines[i][2] && output <= TIMEBASE_TICKS_MASK);
        expired = false;
        CHECK(timebase_expired(output, output, &expired) == TIMEBASE_OK && expired);
        CHECK(timebase_expired(deadlines[i][0], output, &expired) == TIMEBASE_OK);
        CHECK(expired == (deadlines[i][1] == 0));
        CHECK(timebase_expired((output - 1UL) & TIMEBASE_TICKS_MASK, output, &expired) == TIMEBASE_OK);
        CHECK(!expired);
        CHECK(timebase_expired((output + 1UL) & TIMEBASE_TICKS_MASK, output, &expired) == TIMEBASE_OK);
        CHECK(expired);
    }
    for (i = 0; i < sizeof(phases) / sizeof(phases[0]); i++) {
        deadline = phases[i];
        now = (deadline + 0x7fffffUL) & TIMEBASE_TICKS_MASK;
        expired = false;
        CHECK(timebase_expired(now, deadline, &expired) == TIMEBASE_OK && expired);
        CHECK(timebase_expired(deadline, now, &expired) == TIMEBASE_OK && !expired);
        now = (deadline + 0x800000UL) & TIMEBASE_TICKS_MASK;
        for (initial = 0; initial < 2; initial++) {
            expired = initial != 0;
            CHECK(timebase_expired(now, deadline, &expired) == TIMEBASE_AMBIGUOUS);
            CHECK(expired == (initial != 0));
            CHECK(timebase_expired(deadline, now, &expired) == TIMEBASE_AMBIGUOUS);
            CHECK(expired == (initial != 0));
        }
    }
    for (bit = 23; bit < 32; bit++) {
        invalid = 1UL << bit;
        output = 0xa5c76996UL;
        CHECK(timebase_deadline_after(0, invalid, &output) == TIMEBASE_INVALID_ARGUMENT);
        CHECK(output == 0xa5c76996UL);
        if (bit == 23)
            continue;
        CHECK(timebase_deadline_after(invalid, 0, &output) == TIMEBASE_INVALID_ARGUMENT);
        CHECK(output == 0xa5c76996UL);
        for (initial = 0; initial < 2; initial++) {
            expired = initial != 0;
            CHECK(timebase_expired(invalid, 0, &expired) == TIMEBASE_INVALID_ARGUMENT);
            CHECK(expired == (initial != 0));
            CHECK(timebase_expired(0, invalid, &expired) == TIMEBASE_INVALID_ARGUMENT);
            CHECK(expired == (initial != 0));
        }
    }
    output = 0xa5c76996UL;
    CHECK(timebase_deadline_after(0, 0x800001UL, &output) == TIMEBASE_INVALID_ARGUMENT);
    CHECK(output == 0xa5c76996UL);
    CHECK(timebase_deadline_after(0, 0xffffffUL, &output) == TIMEBASE_INVALID_ARGUMENT);
    CHECK(output == 0xa5c76996UL);
    CHECK(timebase_deadline_after(0xffffffffUL, 0xffffffffUL, &output) == TIMEBASE_INVALID_ARGUMENT);
    CHECK(output == 0xa5c76996UL);
    for (initial = 0; initial < 2; initial++) {
        expired = initial != 0;
        CHECK(timebase_expired(0xffffffffUL, 0xffffffffUL, &expired) == TIMEBASE_INVALID_ARGUMENT);
        CHECK(expired == (initial != 0));
    }
    CHECK(timebase_deadline_after(0, 0, NULL) == TIMEBASE_INVALID_ARGUMENT);
    CHECK(timebase_deadline_after(0xffffffUL, 0x7fffffUL, NULL) == TIMEBASE_INVALID_ARGUMENT);
    CHECK(timebase_deadline_after(0xffffffffUL, 0xffffffffUL, NULL) == TIMEBASE_INVALID_ARGUMENT);
    CHECK(timebase_expired(0, 0, NULL) == TIMEBASE_INVALID_ARGUMENT);
    CHECK(timebase_expired(0xffffffUL, 0, NULL) == TIMEBASE_INVALID_ARGUMENT);
    CHECK(timebase_expired(0, 0x800000UL, NULL) == TIMEBASE_INVALID_ARGUMENT);
    CHECK(timebase_expired(0xffffffffUL, 0xffffffffUL, NULL) == TIMEBASE_INVALID_ARGUMENT);
    return 0;
}

#if defined(__SDCC)
volatile MCU_XDATA MCU_AT(0x1e00) uint8_t timebase_test_result[8];
volatile MCU_XDATA uint32_t timebase_test_ticks;

void timebase_test_sample(void)
{
    __asm
        .globl _timebase_sample_ready
    _timebase_sample_ready:
        nop
    __endasm;
    timebase_test_ticks = timebase_read_awake_ticks24();
    __asm
        .globl _timebase_sample_done
    _timebase_sample_done:
        nop
    __endasm;
}

void main(void)
{
    uint16_t result = self_test();
    timebase_test_result[0] = 'T';
    timebase_test_result[1] = '2';
    timebase_test_result[2] = '4';
    timebase_test_result[3] = 'T';
    timebase_test_result[4] = 1;
    timebase_test_result[5] = 8;
    timebase_test_result[6] = (uint8_t)result;
    timebase_test_result[7] = (uint8_t)(result >> 8);
    for (;;) {
        timebase_test_sample();
    }
}
#else
#include "host_mmio.h"

#include <assert.h>
#include <stdio.h>

static uint32_t counter, latched, tick_step;

static uint8_t sleep_timer_read(uint8_t address, uint8_t value)
{
    (void)value;
    assert(read_count < 3 && address == SOC_ST0_ADDRESS + read_count);
    if (address == SOC_ST0_ADDRESS)
        latched = counter;
    value = (uint8_t)(latched >> (8u * read_count));
    counter = (counter + tick_step) & TIMEBASE_TICKS_MASK;
    return value;
}

static uint16_t read_sample(uint32_t ticks, uint32_t step)
{
    uint8_t i;
    uint32_t result;
    counter = ticks;
    tick_step = step;
    latched = ticks ^ TIMEBASE_TICKS_MASK;
    read_count = 0;
    result = timebase_read_awake_ticks24();
    CHECK(result == ticks && (result & 0xff000000UL) == 0);
    CHECK(counter == ((ticks + step * 3UL) & TIMEBASE_TICKS_MASK));
    CHECK(read_count == 3 && write_count == 0);
    for (i = 0; i < 3; i++) {
        CHECK(reads[i].address == SOC_ST0_ADDRESS + i);
        CHECK(reads[i].value == (uint8_t)(ticks >> (8u * i)));
    }
#define CHECK_REGISTER(name, address) CHECK(name == (uint8_t)((address) ^ 0x5a));
    CC2530_REGISTER_LIST(CHECK_REGISTER)
#undef CHECK_REGISTER
    return 0;
}

static uint16_t reader_tests(void)
{
    uint32_t phase;
    uint16_t result;
    uint8_t high, i;
    static const uint8_t high_bytes[] = {0, 0x7f, 0xff};
    static const uint32_t steps[] = {0, 1, 0x10101UL, 0xffffffUL};

    host_mmio_reset();
    SOC_ST0 = 0x12;
    SOC_ST1 = 0x34;
    SOC_ST2 = 0x56;
    CHECK(timebase_read_awake_ticks24() == 0x563412UL);
    CHECK(read_count == 3 && write_count == 0);
    CHECK(reads[0].address == 0x95 && reads[0].value == 0x12);
    CHECK(reads[1].address == 0x96 && reads[1].value == 0x34);
    CHECK(reads[2].address == 0x97 && reads[2].value == 0x56);
    host_mmio_read_hook = sleep_timer_read;
    host_mmio_reset();
    CHECK(read_count == 0 && write_count == 0 && host_mmio_read_hook == NULL);
#define SEED_REGISTER(name, address) name = (uint8_t)((address) ^ 0x5a);
    CC2530_REGISTER_LIST(SEED_REGISTER)
#undef SEED_REGISTER
    host_mmio_read_hook = sleep_timer_read;
    for (i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
        for (high = 0; high < sizeof(phases) / sizeof(phases[0]); high++) {
            result = read_sample(phases[high], steps[i]);
            if (result != 0)
                return result;
        }
    }
    for (high = 0; high < sizeof(high_bytes); high++) {
        for (phase = 0; phase < 0x10000UL; phase++) {
            result = read_sample(((uint32_t)high_bytes[high] << 16) | phase, 1);
            if (result != 0)
                return result;
        }
    }
    host_mmio_reset();
    return 0;
}

static uint16_t exhaustive_arithmetic(void)
{
    uint32_t phase, now, expected, output;
    uint8_t i;
    bool expired;
    timebase_result_t result;
    static const uint32_t origins[] = {0, 0x654321UL, 0xffffffUL};

    for (phase = 0; phase < 0x1000000UL; phase++) {
        expected = phase + 0x7fffffUL;
        if (expected >= 0x1000000UL)
            expected -= 0x1000000UL;
        output = 0xa5c76996UL;
        CHECK(timebase_deadline_after(phase, 0x7fffffUL, &output) == TIMEBASE_OK);
        CHECK(output == expected);
        CHECK(timebase_expired(phase, output, &expired) == TIMEBASE_OK && !expired);
        CHECK(timebase_deadline_after(phase, 0, &output) == TIMEBASE_OK && output == phase);
        CHECK(timebase_expired(phase, output, &expired) == TIMEBASE_OK && expired);
        for (i = 0; i < sizeof(origins) / sizeof(origins[0]); i++) {
            now = origins[i] + phase;
            if (now >= 0x1000000UL)
                now -= 0x1000000UL;
            expired = true;
            result = timebase_expired(now, origins[i], &expired);
            if (phase == 0x800000UL) {
                CHECK(result == TIMEBASE_AMBIGUOUS && expired);
            } else {
                CHECK(result == TIMEBASE_OK && expired == (phase < 0x800000UL));
            }
        }
    }
    CHECK(read_count == 0 && write_count == 0);
    return 0;
}

int main(void)
{
    uint16_t result = self_test();
    if (result == 0 && (read_count != 0 || write_count != 0))
        result = (uint16_t)__LINE__;
    if (result == 0)
        result = reader_tests();
    if (result == 0)
        result = exhaustive_arithmetic();
    if (result != 0) {
        fprintf(stderr, "Timebase test failed at line %u\n", (unsigned)result);
        return 1;
    }
    puts("host timebase: latched ST0/ST1/ST2, zero writes, boundaries, all 24-bit arithmetic phases PASS");
    return 0;
}
#endif
