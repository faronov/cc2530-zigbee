/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 * Isolated synthetic executable. NEVER flash this image.
 */
#include "dma.h"
#include "timebase.h"

#include <stddef.h>
#include <string.h>

#if defined(__SDCC)
#define COUNT_CASE() ((void)0)
#else
static unsigned cases;
#define COUNT_CASE() (++cases)
#endif
#define CHECK(c) do { COUNT_CASE(); if (!(c)) return (uint16_t)__LINE__; } while (0)

static uint16_t invalid_arguments(dma_diagnostics_t MCU_XDATA *d)
{
    dma_diagnostics_t saved;
    uint8_t bit;
    memset(d, 0xa5, sizeof(*d));
    memcpy(&saved, d, sizeof(saved));
    CHECK(dma_copy_init(0x1000, 0x1100, 0, 1, 1, d) == DMA_INVALID_ARGUMENT);
    CHECK(dma_copy_init(0x1000, 0x1100, 17, 1, 1, d) == DMA_INVALID_ARGUMENT);
    CHECK(dma_copy_init(0x1000, 0x1100, 255, 1, 1, d) == DMA_INVALID_ARGUMENT);
    CHECK(dma_copy_init(0x1000, 0x1100, 1, 0, 1, d) == DMA_INVALID_ARGUMENT);
    CHECK(dma_copy_init(0x1000, 0x1100, 1, 1, 0, d) == DMA_INVALID_ARGUMENT);
    CHECK(dma_copy_init(0x1000, 0x1100, 1, 1, 1, NULL) == DMA_INVALID_ARGUMENT);
    for (bit = 23; bit < 32; bit++)
        CHECK(dma_copy_init(0x1000, 0x1100, 1, 1UL << bit, 1, d) == DMA_INVALID_ARGUMENT);
    CHECK(dma_copy_init(0x1dff, 0x1100, 2, 1, 1, d) == DMA_INVALID_RANGE);
    CHECK(dma_copy_init(0x1000, 0x1dff, 2, 1, 1, d) == DMA_INVALID_RANGE);
    CHECK(dma_copy_init(0x1e00, 0x1100, 1, 1, 1, d) == DMA_INVALID_RANGE);
    CHECK(dma_copy_init(0x1000, 0x1f00, 1, 1, 1, d) == DMA_INVALID_RANGE);
    CHECK(dma_copy_init(0x6080, 0x1100, 1, 1, 1, d) == DMA_INVALID_RANGE);
    CHECK(dma_copy_init(0x1000, 0x70d1, 1, 1, 1, d) == DMA_INVALID_RANGE);
    CHECK(dma_copy_init(0xffff, 0x1100, 1, 1, 1, d) == DMA_INVALID_RANGE);
    CHECK(dma_copy_init(0x1000, 0x100f, 16, 1, 1, d) == DMA_INVALID_RANGE);
    CHECK(dma_copy_init(0x100f, 0x1000, 16, 1, 1, d) == DMA_INVALID_RANGE);
    CHECK(dma_copy_init(0x1000, 0x1000, 1, 1, 1, d) == DMA_INVALID_RANGE);
    CHECK(dma_copy_init(0, 0x1100, 1, 1, 1, d) == DMA_BUFFER_OWNERSHIP);
    CHECK(memcmp(d, &saved, sizeof(saved)) == 0);
    return 0;
}

#if defined(__SDCC)
volatile MCU_XDATA MCU_AT(0x1e00) uint8_t dma_test_result[8];
MCU_XDATA dma_diagnostics_t dma_test_diagnostics;
MCU_XDATA uint8_t dma_test_source[16], dma_test_destination[16];
volatile MCU_XDATA uint16_t dma_test_src, dma_test_dst, dma_test_output, dma_test_limit;
volatile MCU_XDATA uint32_t dma_test_timeout;
volatile MCU_XDATA uint8_t dma_test_length, dma_test_return;

void dma_test_cycle(void)
{
    __asm
        .globl _dma_test_before
    _dma_test_before:
        nop
    __endasm;
    dma_test_return = dma_copy_init(dma_test_src, dma_test_dst, dma_test_length,
                                    dma_test_timeout, dma_test_limit,
                                    (dma_diagnostics_t MCU_XDATA *)dma_test_output);
    __asm
        .globl _dma_test_done
    _dma_test_done:
        nop
    __endasm;
}

void main(void)
{
    uint8_t i;
    uint16_t result = invalid_arguments(&dma_test_diagnostics);
    for (i = 0; i < 16; i++) {
        dma_test_source[i] = i ^ 0x69u;
        dma_test_destination[i] = 0xa5;
    }
    dma_test_src = (uint16_t)dma_test_source;
    dma_test_dst = (uint16_t)dma_test_destination;
    dma_test_output = (uint16_t)&dma_test_diagnostics;
    dma_test_result[0] = 'D';
    dma_test_result[1] = 'M';
    dma_test_result[2] = 'A';
    dma_test_result[3] = 'T';
    dma_test_result[4] = 1;
    dma_test_result[5] = 8;
    dma_test_result[6] = (uint8_t)result;
    dma_test_result[7] = (uint8_t)(result >> 8);
    for (;;)
        dma_test_cycle();
}
#else
#include "host_mmio.h"
#include <assert.h>
#include <stdio.h>

extern volatile uint8_t dma_descriptor[8];
extern uint8_t dma_fault, dma_reserved_end;
uint8_t _gptrput_PARM_2;

static uint8_t ram[0x1e00], reference[0x1e00], fetched[8];
static dma_diagnostics_t d;
static uint16_t output_address;
static unsigned observations, samples, next_read, next_tick, accesses;
static unsigned cpu_cycles, early, copied, per_poll, active, ready, requested;
static unsigned mutation_observation, late_sample, writes_seen;
static uint8_t mutation_address, mutation_value, ack_foreign, ignore_ack, ignore_config, hold_request;
static uint8_t arm_foreign, request_foreign, lost_irq;
static uint32_t start, jump, ticks, latch;
static uint8_t last_address, last_value;
static uint8_t clock_value;
static const uint8_t order[] = {
    0xa8, 0xb8, 0x9a, 0xbe, 0xc6, 0x9e, 0xd6, 0xd7, 0xd1, 0xc0, 0xd4, 0xd5, 0xd2, 0xd3
};

static void drain_reads(void)
{
    if (read_count) {
        assert(read_count == 1);
        assert(reads[0].address == last_address && reads[0].value == last_value);
        read_count = 0;
    }
    assert(xread_count == 0);
}

static uint16_t xaddress(const volatile void *object)
{
    if (object == &dma_reserved_end)
        return 0xff;
    if (object == dma_descriptor)
        return 0x20;
    if (object == &_gptrput_PARM_2)
        return 0x1700;
    assert(object == &d);
    return output_address;
}

static void engine(unsigned count)
{
    unsigned source = (unsigned)fetched[0] * 256 + fetched[1];
    unsigned destination = (unsigned)fetched[2] * 256 + fetched[3];
    if (!active)
        return;
    assert(ready && requested && !early);
    if (hold_request)
        return;
    SOC_DMAREQ &= 0xfeu;
    while (count-- && copied < fetched[5]) {
        assert(source + copied < sizeof(ram) && destination + copied < sizeof(ram));
        ram[destination + copied] = ram[source + copied];
        copied++;
    }
    if (copied == fetched[5]) {
        active = 0;
        SOC_DMAARM &= 0xfeu;
        if (!lost_irq) SOC_DMAIRQ |= 1;
    }
}

static void cycles(uint8_t count)
{
    drain_reads();
    assert(count == 9 && cpu_cycles == 0 && (SOC_DMAARM & 1));
    cpu_cycles += count;
    memcpy(fetched, (const void *)dma_descriptor, 8);
    assert(SOC_DMA0CFGH == 0 && SOC_DMA0CFGL == 0x20);
    assert(fetched[4] == 0 && fetched[5] > 0 && fetched[5] <= 16);
    assert(fetched[6] == 0x20 && fetched[7] == 0x51);
    ready = SOC_DMAARM == 1;
}

static uint8_t load(uint8_t address, uint8_t value)
{
    drain_reads();
    accesses++;
    if (address == 0xa8) {
        assert(next_tick == 0);
        assert(next_read == 0 || next_read == sizeof(order));
        next_read = 0;
        observations++;
        if (requested)
            engine(per_poll);
    }
    if (address >= 0x95 && address <= 0x97) {
        assert(address == 0x95 + next_tick);
        assert(next_read == sizeof(order) || next_read == 6);
        if (next_tick++ == 0) {
            ticks = (start + samples) & TIMEBASE_TICKS_MASK;
            if (late_sample && samples >= late_sample)
                ticks = (start + jump) & TIMEBASE_TICKS_MASK;
            samples++;
            latch = ticks;
        }
        value = (uint8_t)(latch >> ((address - 0x95) * 8));
        if (next_tick == 3)
            next_tick = 0;
    } else {
        assert(next_read < sizeof(order) && address == order[next_read++]);
        /* Effects applied at the start of an observation are visible at
         * the genuine later DMA read, not the stale hook input argument.
         */
        if (address == 0xd6) value = SOC_DMAARM;
        if (address == 0xd7) value = SOC_DMAREQ;
        if (address == 0xd1) value = SOC_DMAIRQ;
        if (observations == mutation_observation && address == mutation_address)
            value = mutation_value;
    }
    last_address = address;
    last_value = value;
    return value;
}

static void store(uint8_t address, uint8_t before, uint8_t value)
{
    static const uint8_t write_order[] = {0xd5, 0xd4, 0xd6, 0xd7, 0xd1};
    drain_reads();
    accesses++;
    assert(write_count == 1 && writes[0].address == address &&
           writes[0].before == before && writes[0].after == value);
    assert(writes_seen < sizeof(write_order) && address == write_order[writes_seen++]);
    write_count = 0;
    if (address == 0xd5 || address == 0xd4) {
        assert(value == (address == 0xd5 ? 0 : 0x20));
        if (ignore_config) SOC_DMA0CFGL = 0x21;
    } else if (address == 0xd6) {
        assert(value == 1 && before == 0 && !requested);
        SOC_DMAARM = (uint8_t)(before | value | arm_foreign);
    } else if (address == 0xd7) {
        assert(value == 1 && before == 0 && !requested);
        SOC_DMAREQ = (uint8_t)(before | value | request_foreign);
        requested = 1;
        if (!ready || cpu_cycles < 9) early++;
        else active = 1;
    } else {
        assert(value == 0x1e && before == 1 && copied == fetched[5]);
        SOC_DMAIRQ = (uint8_t)((before | ack_foreign) & (ignore_ack ? 0x1f : value));
    }
}

static void reset_model(void)
{
    /* A new independent reset epoch, not a production recovery API. Re-entry
     * tests intentionally do NOT use this between the failed and later call.
     */
    host_mmio_reset();
    dma_fault = 0;
    memset((void *)dma_descriptor, 0, 8);
    memset(ram, 0xa5, sizeof(ram));
    memset(&d, 0xa5, sizeof(d));
    output_address = 0x1800;
    observations = samples = next_read = next_tick = accesses = 0;
    cpu_cycles = early = copied = active = ready = requested = writes_seen = 0;
    mutation_observation = late_sample = 0;
    mutation_address = mutation_value = ack_foreign = ignore_ack = ignore_config = hold_request = 0;
    arm_foreign = request_foreign = lost_irq = 0;
    start = jump = ticks = latch = 0;
    per_poll = 16;
    SOC_CLKCONCMD = SOC_CLKCONSTA = 0xc9;
    clock_value = 0xc9;
    SOC_SLEEPCMD = 4;
    SOC_IRCON = 0xbe;
    SOC_DMA1CFGH = 0x12;
    SOC_DMA1CFGL = 0x34;
    SOC_P0 = 0x69;
    SOC_P1 = 0xa5;
    SOC_P2 = 0x5a;
    SOC_RFERRF = 0x37;
    host_mmio_read_hook = load;
    host_mmio_write_hook = store;
    host_mmio_xaddress_hook = xaddress;
    host_mmio_cycles_hook = cycles;
}

static dma_result_t invoke(uint16_t source, uint16_t destination, uint8_t length,
                           uint32_t timeout, uint16_t limit)
{
    dma_result_t result = dma_copy_init(source, destination, length, timeout, limit, &d);
    drain_reads();
    assert(!write_count && !xread_count && !early);
    assert(SOC_P0 == 0x69 && SOC_P1 == 0xa5 && SOC_P2 == 0x5a && SOC_RFERRF == 0x37);
    assert(SOC_IRCON == 0xbe && SOC_DMA1CFGH == 0x12 && SOC_DMA1CFGL == 0x34);
    assert(SOC_IEN0 == 0 && SOC_IEN1 == 0 && SOC_IEN2 == 0);
    assert(SOC_CLKCONCMD == clock_value && SOC_CLKCONSTA == clock_value && SOC_SLEEPCMD == 4);
    cases++;
    return result;
}

static dma_result_t range_oracle(uint16_t source, uint16_t destination, uint8_t n)
{
    uint32_t se = (uint32_t)source + n, de = (uint32_t)destination + n;
    uint32_t oe = (uint32_t)output_address + sizeof(d);
    if (se > 0x1e00 || de > 0x1e00 || oe > 0x1e00 ||
        ((uint32_t)source < de && (uint32_t)destination < se))
        return DMA_INVALID_RANGE;
    if (source < 0x100 || destination < 0x100 || output_address < 0x100 ||
        ((uint32_t)source < oe && output_address < se) ||
        ((uint32_t)destination < oe && output_address < de) ||
        (source <= 0x1700 && se > 0x1700) || (destination <= 0x1700 && de > 0x1700) ||
        (output_address <= 0x1700 && oe > 0x1700))
        return DMA_BUFFER_OWNERSHIP;
    return DMA_OK;
}

static void successful(uint16_t source, uint16_t destination, uint8_t n, uint8_t pattern)
{
    unsigned i;
    uint8_t descriptor[8] = {(uint8_t)(source >> 8), (uint8_t)source,
        (uint8_t)(destination >> 8), (uint8_t)destination, 0, n, 0x20, 0x51};
    for (i = 0; i < n; i++) ram[source + i] = (uint8_t)(i ^ pattern);
    memcpy(reference, ram, sizeof(ram));
    memcpy(reference + destination, ram + source, n);
    assert(invoke(source, destination, n, 100, 32) == DMA_OK);
    assert(!memcmp(reference, ram, sizeof(ram)));
    assert(!memcmp(descriptor, (const void *)dma_descriptor, 8));
    assert(d.actions == 15 && d.complete == 1 && d.verified == 1 && d.sample_valid == 1);
    assert(d.polls == (per_poll == 1 ? n + 4 : 5) && d.elapsed_ticks == d.polls);
    assert(d.timebase_status == 0 && d.arm == 0 && d.request == 0 && d.irq == 0);
    assert(dma_fault == 0 && writes_seen == 5 && copied == n && cpu_cycles == 9);
}

static void terminal(dma_result_t expected, uint8_t actions, uint8_t complete,
                     uint32_t timeout, uint16_t limit)
{
    dma_diagnostics_t saved;
    uint8_t descriptor[8], memory[0x1e00];
    unsigned count;
    for (count = 0; count < 16; count++)
        ram[0x100 + count] = (uint8_t)(count ^ 0x69u);
    memcpy(reference, ram, sizeof(ram));
    assert(invoke(0x100, 0x200, 16, timeout, limit) == expected);
    memcpy(reference + 0x200, ram + 0x100, copied);
    assert(!memcmp(reference, ram, sizeof(ram)));
    assert(d.actions == actions && d.complete == complete && d.verified == 0);
    assert(dma_fault == expected);
    saved = d;
    memcpy(descriptor, (const void *)dma_descriptor, 8);
    memcpy(memory, ram, sizeof(ram));
    count = accesses;
    assert(invoke(0x300, 0x400, 8, 100, 32) == expected);
    assert(dma_copy_init(0, 0, 0, 0, 0, NULL) == expected);
    cases++;
    assert(!memcmp(&saved, &d, sizeof(d)) && !memcmp(descriptor, (const void *)dma_descriptor, 8));
    assert(!memcmp(memory, ram, sizeof(ram)) && accesses == count);
}

int main(void)
{
    uint32_t address;
    unsigned n, pattern, observation, bit;
    dma_result_t expected;
    reset_model();
    assert(invalid_arguments(&d) == 0 && accesses == 0);
    for (n = 1; n <= 16; n++)
        for (pattern = 0; pattern < 256; pattern++) {
            reset_model();
            successful(0x10f, 0x21f, (uint8_t)n, (uint8_t)pattern);
        }
    for (address = 0; address <= 0xffff; address++)
        for (n = 0; n < 2; n++) {
            uint16_t source = n ? 0x100 : (uint16_t)address;
            uint16_t destination = n ? (uint16_t)address : 0x100;
            reset_model();
            expected = range_oracle(source, destination, 16);
            if (expected == DMA_OK)
                successful(source, destination, 16, (uint8_t)address);
            else {
                dma_diagnostics_t saved = d;
                assert(invoke(source, destination, 16, 100, 32) == expected);
                assert(accesses == 0 && !memcmp(&saved, &d, sizeof(d)) && dma_fault == 0);
            }
        }
    for (address = 0x1df0; address < 0x1e00; address++) {
        reset_model();
        successful((uint16_t)address, 0x200, (uint8_t)(0x1e00 - address), 0xff);
    }
    for (address = 1; address <= 0xffff; address++) {
        reset_model();
        output_address = (uint16_t)address;
        expected = range_oracle(0x100, 0x200, 16);
        if (expected != DMA_OK) {
            dma_diagnostics_t saved = d;
            assert(invoke(0x100, 0x200, 16, 100, 32) == expected);
            assert(accesses == 0 && !memcmp(&saved, &d, sizeof(d)));
        }
    }
    for (n = 1; n <= 16; n++) {
        reset_model();
        per_poll = 1;
        start = 0xfffff8;
        successful(0x100, 0x200, (uint8_t)n, 0x80);
    }
    for (n = 0; n < 256; n++) {
        reset_model();
        SOC_CLKCONCMD = SOC_CLKCONSTA = clock_value = (uint8_t)n;
        if ((n & 7) == ((n & 0x40) ? 1u : 0u) && (!(n & 0x40) || (n & 0x38)))
            successful(0x100, 0x200, 16, 0x69);
        else
            terminal(DMA_UNSUPPORTED_STATE, 0, 0, 100, 32);
    }
    reset_model();
    assert(invoke(0x100, 0x200, 16, TIMEBASE_HALF_RANGE - 1, 5) == DMA_OK);
    for (bit = 0; bit < 8; bit++)
        for (n = 0; n < sizeof(order); n++) {
            if (n == 3 || n == 4 || n == 5 || n >= 10 || (n == 9 && bit != 0))
                continue;
            reset_model();
            mutation_observation = 1;
            mutation_address = order[n];
            mutation_value = (uint8_t)(1u << bit);
            expected = n < 3 || (n >= 6 && n <= 8 && bit >= 5) ? DMA_UNSUPPORTED_STATE :
                       n == 6 ? DMA_BUSY : DMA_PENDING;
            terminal(expected, 0, 0, 100, 32);
            assert(writes_seen == 0);
        }
    for (observation = 2; observation <= 6; observation++)
        for (n = 0; n < sizeof(order); n++) {
            reset_model();
            mutation_observation = observation;
            mutation_address = order[n];
            mutation_value = n == 3 ? 0 : n == 4 || n == 5 ? 0x88 : n == 6 || n == 7 || n == 8 ? 2 : 1;
            expected = n < 6 ? DMA_UNSUPPORTED_STATE : DMA_STATE_CHANGED;
            terminal(expected, observation == 2 ? 0 : observation == 3 ? 1 : observation == 4 ? 3 :
                     observation == 5 ? 7 : 15, observation == 6, 100, 32);
        }
    for (n = 1; n <= 5; n++) {
        reset_model();
        late_sample = n;
        jump = 20;
        terminal(DMA_TIMEOUT, n == 1 ? 0 : n == 2 ? 1 : n == 3 ? 3 : n == 4 ? 7 : 15,
                 n >= 4, 20, 32);
    }
    for (n = 1; n <= 4; n++) {
        reset_model();
        terminal(DMA_POLL_LIMIT, n == 1 ? 0 : n == 2 ? 1 : n == 3 ? 3 : 7, n == 4, 100, (uint16_t)n);
    }
    reset_model();
    late_sample = 4;
    jump = TIMEBASE_HALF_RANGE + 100;
    terminal(DMA_TIMEBASE_ERROR, 7, 1, 100, 32);
    assert(d.timebase_status == TIMEBASE_AMBIGUOUS);
    reset_model();
    late_sample = 4;
    jump = TIMEBASE_HALF_RANGE;
    terminal(DMA_COUNTER_RANGE, 7, 1, 100, 32);
    reset_model();
    late_sample = 4;
    jump = 0xffffff;
    terminal(DMA_COUNTER_RANGE, 7, 1, 100, 32);
    reset_model();
    hold_request = 1;
    terminal(DMA_POLL_LIMIT, 7, 0, 100, 5);
    assert(SOC_DMAARM == 1 && SOC_DMAREQ == 1 && copied == 0);
    hold_request = 0;
    engine(16);
    assert(copied == 16 && SOC_DMAIRQ == 1 && SOC_DMAREQ == 0);
    reset_model();
    per_poll = 0;
    late_sample = 1;
    jump = 0;
    terminal(DMA_POLL_LIMIT, 7, 0, 100, 65535u);
    assert(d.polls == 65535u && d.elapsed_ticks == 0 && d.request == 0 && d.arm == 1);
    reset_model();
    per_poll = 0;
    terminal(DMA_POLL_LIMIT, 7, 0, 100, 5);
    assert(SOC_DMAARM == 1 && SOC_DMAREQ == 0 && copied == 0);
    engine(3);
    assert(copied == 3 && SOC_DMAARM == 1);
    assert(!memcmp(ram + 0x100, ram + 0x200, 3) && ram[0x203] == 0xa5);
    engine(16);
    assert(copied == 16 && SOC_DMAIRQ == 1 && SOC_DMAARM == 0);
    assert(!memcmp(ram + 0x100, ram + 0x200, 16));
    assert(invoke(0x300, 0x400, 1, 100, 32) == DMA_POLL_LIMIT && SOC_DMAIRQ == 1);
    /* Negative engine contract: an early request can be lost, while its
     * request bit remains set. ARM readback alone must not imply readiness.
     */
    reset_model();
    writes_seen = 3;
    SOC_DMAARM = 1;
    MMIO_WRITE(SOC_DMAREQ, 1);
    assert(early == 1 && !ready && !active && SOC_DMAREQ == 1);
    cpu_cycles = 9;
    ready = 1;
    engine(16);
    assert(copied == 0 && SOC_DMAREQ == 1 && SOC_DMAIRQ == 0);
    cases++;
    reset_model();
    arm_foreign = 0x10;
    terminal(DMA_STATE_CHANGED, 3, 0, 100, 32);
    assert(SOC_DMAARM == 0x11 && !requested && !ready);
    reset_model();
    request_foreign = 0x10;
    hold_request = 1;
    terminal(DMA_STATE_CHANGED, 7, 0, 100, 32);
    assert(SOC_DMAREQ == 0x11);
    reset_model();
    lost_irq = 1;
    terminal(DMA_POLL_LIMIT, 7, 0, 100, 5);
    assert(copied == 16 && SOC_DMAARM == 0 && SOC_DMAREQ == 0 && SOC_DMAIRQ == 0);
    reset_model();
    ack_foreign = 0x10;
    terminal(DMA_STATE_CHANGED, 15, 1, 100, 32);
    assert(SOC_DMAIRQ == 0x10);
    reset_model();
    ignore_ack = 1;
    terminal(DMA_STATE_CHANGED, 15, 1, 100, 32);
    reset_model();
    ignore_config = 1;
    terminal(DMA_STATE_CHANGED, 1, 0, 100, 32);
    reset_model();
    /* Ready fetch but ARM unexpectedly absent: no software request follows. */
    mutation_observation = 4;
    mutation_address = 0xd6;
    mutation_value = 0;
    terminal(DMA_STATE_CHANGED, 3, 0, 100, 32);
    for (n = 0; n < 257; n++) {
        if (n == 0) reset_model();
        observations = samples = next_read = next_tick = writes_seen = 0;
        cpu_cycles = copied = active = ready = requested = 0;
        successful(0x100, 0x200, 16, (uint8_t)n);
    }
    printf("DMA: %u host cases; exact descriptor/32-entry log consumption, reference copies, "
           "range/ownership, bounded waits, retained faults and post-return effects PASS.\n", cases);
    return 0;
}
#endif
