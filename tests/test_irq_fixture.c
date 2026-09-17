/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "irq_fixture.h"
#include "host_mmio.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint32_t ticks, latched_ticks, step;
static uint8_t byte_index, race, suppress, preserved[256];
static uint8_t counter_high, counter_latched;
static register_read_t expected[32];
static unsigned expected_count, timer_starts, acknowledgments, deliveries;

static void consume(void)
{
    unsigned i;
    assert(read_count == expected_count);
    for (i = 0; i < read_count; i++)
        assert(reads[i].address == expected[i].address && reads[i].value == expected[i].value);
    read_count = expected_count = 0;
}

static uint8_t read_hook(uint8_t address, uint8_t value)
{
    consume();
    if (counter_latched)
        assert(address == IRQ_T1CNTH_ADDRESS);
    if (address == IRQ_T1CNTL_ADDRESS) {
        counter_high = IRQ_T1CNTH;
        counter_latched = 1;
        if (IRQ_T1CTL && ++IRQ_T1CNTL == 0)
            IRQ_T1CNTH++;
    } else if (address == IRQ_T1CNTH_ADDRESS) {
        assert(counter_latched);
        value = counter_high;
        counter_latched = 0;
    }
    if (address >= SOC_ST0_ADDRESS && address <= SOC_ST2_ADDRESS) {
        assert(address == SOC_ST0_ADDRESS + byte_index);
        if (!byte_index) {
            latched_ticks = ticks;
            ticks = (ticks + step) & TIMEBASE_TICKS_MASK;
        }
        value = (uint8_t)(latched_ticks >> (byte_index * 8));
        byte_index = (uint8_t)((byte_index + 1u) % 3u);
    }
    expected[expected_count].address = address;
    expected[expected_count++].value = value;
    return value;
}

static void write_hook(uint8_t address, uint8_t before, uint8_t value)
{
    assert(write_count == 1 && writes[0].address == address &&
           writes[0].before == before && writes[0].after == value);
    write_count = 0;
    if (address == IRQ_T1CTL_ADDRESS) {
        assert(value <= 1);
        timer_starts += value;
    } else if (address == IRQ_T1CNTL_ADDRESS) {
        assert(value == 0 && !IRQ_T1CTL && !(SOC_IEN0 & 0x80));
        IRQ_T1CNTL = IRQ_T1CNTH = 0;
    } else if (address == IRQ_T1STAT_ADDRESS) {
        assert(value == 0x1f && !(SOC_IEN1 & 2) && !IRQ_T1CTL);
        IRQ_T1STAT = (uint8_t)((before | race) & value);
        if (race)
            IRQ_IRCON |= 2;
        acknowledgments++;
    } else {
        assert(address == SOC_IEN0_ADDRESS || address == SOC_IEN1_ADDRESS);
    }
    if (address == SOC_IEN0_ADDRESS && (value & 0x80) && (SOC_IEN1 & 2) &&
        (IRQ_IRCON & 2) && !suppress) {
        assert(!IRQ_T1CTL);
        IRQ_IRCON &= 0xfd;
        deliveries++;
        irq_fixture_timer1_isr();
    }
}

static void setup(void)
{
    host_mmio_reset();
#define CLEAR(name, address) name = 0;
    IRQ_FIXTURE_REGISTERS(CLEAR)
#undef CLEAR
    IRQ_T1CCTL0 = IRQ_T1CCTL1 = IRQ_T1CCTL2 = IRQ_T1CCTL3 = IRQ_T1CCTL4 = 0x40;
    IRQ_TIMIF = 0x40;
    SOC_CLKCONCMD = SOC_CLKCONSTA = 0xc9;
    SOC_SLEEPCMD = 4;
    ticks = 0xfffff0;
    step = 1;
    byte_index = race = suppress = counter_latched = 0;
    timer_starts = acknowledgments = deliveries = expected_count = 0;
    irq_fixture_initialize();
    assert(irq_fixture_state.phase == IRQ_INITIALIZED);
    read_count = write_count = 0;
    host_mmio_read_hook = read_hook;
    host_mmio_write_hook = write_hook;
#define SAVE(name, address) preserved[address] = name;
    CC2530_REGISTER_LIST(SAVE)
#undef SAVE
}

static void pending(void)
{
    irq_fixture_begin();
    assert(irq_fixture_state.outer_token == 1 && irq_fixture_state.inner_token == 0);
    assert(!SOC_IEN0 && SOC_IEN1 == 2 && !IRQ_T1CTL);
    irq_fixture_start();
    assert(IRQ_T1CTL == 1);
    irq_fixture_poll();
    assert(irq_fixture_state.stage == IRQ_WAIT_PENDING);
    IRQ_T1STAT = 0x20;
    IRQ_IRCON |= 2;
    IRQ_T1CNTL = 0x68;
    IRQ_T1CNTH = 0x96;
    irq_fixture_poll();
    assert(irq_fixture_state.stage == IRQ_PENDING && !IRQ_T1CTL && !SOC_IEN0);
    irq_fixture_inner();
    assert(irq_fixture_state.stage == IRQ_INNER && !SOC_IEN0 && IRQ_IRCON == 2 &&
           irq_fixture_state.isr_count == irq_fixture_state.isr_before);
}

static void latched(uint8_t reason)
{
    irq_fixture_t saved;
    consume();
    assert(irq_fixture_state.phase == IRQ_FAULT && irq_fixture_state.reason == reason);
    assert(!IRQ_T1CTL && !SOC_IEN0 && !SOC_IEN1);
    memcpy(&saved, (const void *)&irq_fixture_state, sizeof(saved));
    irq_fixture_begin();
    irq_fixture_start();
    irq_fixture_poll();
    irq_fixture_inner();
    irq_fixture_release();
    consume();
    assert(!write_count && !memcmp(&saved, (const void *)&irq_fixture_state, sizeof(saved)));
}

int main(void)
{
    unsigned cycle, i;
    setup();
    for (cycle = 1; cycle <= 257; cycle++) {
        pending();
        irq_fixture_release();
        assert(irq_fixture_state.outer_result == IRQ_OK && SOC_IEN0 == 0x80 && !SOC_IEN1);
        irq_fixture_poll();
        assert(irq_fixture_state.phase == IRQ_READY && irq_fixture_state.stage == IRQ_DONE);
        assert(irq_fixture_state.isr_count == (uint8_t)cycle &&
               irq_fixture_state.completed == (uint8_t)cycle && m0_status.heartbeat == (uint8_t)cycle);
        assert(irq_fixture_state.disabled_token == 0 && irq_fixture_state.disabled_result == IRQ_OK &&
               irq_fixture_state.invalid_result == IRQ_INVALID_TOKEN && irq_fixture_state.inner_result == IRQ_OK);
        assert(irq_fixture_state.counter[0] == 0x69 && irq_fixture_state.counter[1] == 0x96);
        assert(irq_fixture_state.pending_polls[0] == 2 && irq_fixture_state.delivery_polls[0] == 1);
        assert(!SOC_IEN0 && !SOC_IEN1 && !IRQ_T1CTL && !IRQ_T1STAT && !IRQ_IRCON);
#define SAME(name, address) if (address != 0xa8 && address != 0xb8) assert(name == preserved[address]);
        CC2530_REGISTER_LIST(SAME)
#undef SAME
        consume();
    }
    assert(timer_starts == 257 && acknowledgments == 257 && deliveries == 257);
    for (i = 0; i < 4; i++)
        assert(!irq_fixture_state.reserved[i]);
    assert(irq_fixture_state.guards[0] == 0x69 && irq_fixture_state.guards[1] == 0x96);

    setup();
    irq_fixture_begin();
    ticks = (ticks + 2000) & TIMEBASE_TICKS_MASK;
    irq_fixture_start();
    irq_fixture_poll();
    latched(IRQ_REASON_TIMEOUT);
    assert(!deliveries && !acknowledgments);

    setup();
    step = 0;
    irq_fixture_begin();
    irq_fixture_start();
    for (i = 0; i < IRQ_FIXTURE_POLL_LIMIT; i++)
        irq_fixture_poll();
    latched(IRQ_REASON_POLL_LIMIT);
    assert(irq_fixture_state.pending_polls[0] == 0 && irq_fixture_state.pending_polls[1] == 16);

    setup();
    irq_fixture_begin();
    ticks = (ticks - 2) & TIMEBASE_TICKS_MASK;
    irq_fixture_start();
    irq_fixture_poll();
    latched(IRQ_REASON_RANGE);

    setup();
    irq_fixture_begin();
    ticks = (ticks - 1 + IRQ_FIXTURE_TIMEOUT + TIMEBASE_HALF_RANGE) & TIMEBASE_TICKS_MASK;
    irq_fixture_start();
    irq_fixture_poll();
    latched(IRQ_REASON_TIMEBASE);
    assert(irq_fixture_state.helper_status == TIMEBASE_AMBIGUOUS);

    for (i = 1; i < 32; i++) {
        setup();
        pending();
        race = (uint8_t)i;
        irq_fixture_release();
        irq_fixture_poll();
        latched(IRQ_REASON_SOURCE);
        assert(IRQ_T1STAT == i && deliveries == 1 && acknowledgments == 1);
    }

    setup();
    pending();
    suppress = 1;
    step = 0;
    irq_fixture_release();
    for (i = 0; i < IRQ_FIXTURE_POLL_LIMIT; i++)
        irq_fixture_poll();
    latched(IRQ_REASON_POLL_LIMIT);
    assert(IRQ_T1STAT == 0x20 && !deliveries && !acknowledgments);

    setup();
    irq_fixture_begin();
    irq_fixture_inner();
    latched(IRQ_REASON_PHASE);
    for (i = 0; i < 4; i++) {
        setup();
        consume();
        read_count = write_count = 0;
        host_mmio_read_hook = NULL;
        host_mmio_write_hook = NULL;
        if (i == 0)
            IRQ_T1CCTL3 = 0x41;
        else if (i == 1)
            IRQ_T1CTL = 1;
        else if (i == 2)
            IRQ_IRCON = 2;
        else
            IRQ_TIMIF = 0;
        irq_fixture_initialize();
        assert(irq_fixture_state.phase == IRQ_FAULT && irq_fixture_state.reason == IRQ_REASON_ENTRY);
        for (cycle = 0; cycle < write_count; cycle++)
            assert(writes[cycle].address != IRQ_T1CTL_ADDRESS &&
                   writes[cycle].address != IRQ_T1CNTL_ADDRESS && writes[cycle].address != IRQ_T1STAT_ADDRESS);
    }
    for (i = 0; i < 2; i++) {
        setup();
        irq_fixture_begin();
        irq_fixture_start();
        IRQ_T1CNTL = 0xff;
        IRQ_T1CNTH = i ? 0xff : 0;
        irq_fixture_poll();
        assert(irq_fixture_state.counter[0] == 0xff && irq_fixture_state.counter[1] == (i ? 0xff : 0));
        assert(IRQ_T1CNTL == 0 && IRQ_T1CNTH == (i ? 0 : 1));
        consume();
    }
    puts("host IRQ fixture: 257 real-primitive cycles, nested masking, H0/RW0 model, "
         "flag race, wrap, raw/poll bounds, terminal failures and byte ABI PASS");
    return 0;
}
