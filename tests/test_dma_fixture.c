/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "dma_fixture.h"
#include "host_mmio.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

extern volatile uint8_t dma_descriptor[8];
extern uint8_t dma_fault, dma_reserved_end;
uint8_t _gptrput_PARM_2;
static uint32_t ticks, latched, tick_step;
static unsigned reads_seen, writes_seen, requests, bytes, clocks, arms, acks, mode, corruption, copied;
static uint8_t last_address, last_value, ready, active, booting, invalid_owner;
static uint8_t baseline[32], saved[DMF_SIZE], buffers[36], descriptor[8], frozen_descriptor[8];
static uint8_t frozen_work[sizeof(dma_fixture_work_t)];

static void consume(void)
{
    if (read_count) {
        assert(read_count == 1 && reads[0].address == last_address && reads[0].value == last_value);
        read_count = 0;
    }
    assert(xread_count == 0);
}

static uint16_t address(const volatile void *object)
{
    if (object == dma_descriptor) return 0x20;
    if (object == &dma_reserved_end) return 0xff;
    if (object == &_gptrput_PARM_2) return 0x400;
    if (object == &dma_fixture_work.dma) return 0x324;
    if (object == dma_fixture_a.data) return invalid_owner ? 0x20 : 0x301;
    assert(object == dma_fixture_b.data);
    return 0x313;
}

static volatile uint8_t *buffer(uint16_t address)
{
    if (address == 0x301) return dma_fixture_a.data;
    assert(address == 0x313);
    return dma_fixture_b.data;
}

static void transfer(unsigned amount)
{
    volatile uint8_t *source = buffer((uint16_t)descriptor[0] * 256u + descriptor[1]);
    volatile uint8_t *destination = buffer((uint16_t)descriptor[2] * 256u + descriptor[3]);
    if (!active) return;
    SOC_DMAREQ &= 0xfeu;
    while (amount-- && copied < descriptor[5]) {
        destination[copied] = source[copied];
        copied++; bytes++;
    }
    if (copied == descriptor[5]) {
        SOC_DMAARM &= 0xfeu; SOC_DMAIRQ |= 1; active = 0;
        if (corruption) {
            volatile uint8_t *p = corruption <= 18 ? (volatile uint8_t *)&dma_fixture_a :
                                                    (volatile uint8_t *)&dma_fixture_b;
            p[(corruption - 1) % 18] ^= 1;
        }
    }
}

static uint8_t load(uint8_t reg, uint8_t value)
{
    consume(); reads_seen++;
    assert(reg != 0xd9 && reg != 0xe1);
    if (reg == 0x95) {
        if (mode == 8 && clocks == 1) tick_step = 1;
        latched = ticks; ticks = (ticks + tick_step) & 0xffffff;
    }
    if (reg >= 0x95 && reg <= 0x97) value = (uint8_t)(latched >> ((reg - 0x95) * 8));
    last_address = reg; last_value = value;
    return value;
}

static void cycles(uint8_t n)
{
    consume();
    assert(n == 9 && !ready && SOC_DMAARM == 1 && SOC_DMAREQ == 0);
    assert(SOC_DMA0CFGL == 0x20 && SOC_DMA0CFGH == 0);
    memcpy(descriptor, (const void *)dma_descriptor, 8);
    assert(descriptor[4] == 0 && descriptor[5] == dma_fixture_state.length &&
           descriptor[6] == 0x20 && descriptor[7] == 0x51);
    ready = 1;
}

static void store(uint8_t reg, uint8_t before, uint8_t value)
{
    consume(); writes_seen++;
    assert(write_count == 1 && writes[0].address == reg &&
           writes[0].before == before && writes[0].after == value);
    write_count = 0;
    if (booting) return;
    if (reg == 0xc6) {
        assert(!active && !SOC_DMAARM && !SOC_DMAREQ && !SOC_DMAIRQ && !dma_fault);
        clocks++;
        if (mode != 1) SOC_CLKCONSTA = value;
    } else if (reg == 0xd4 || reg == 0xd5) {
        assert(!active && !SOC_DMAARM && !SOC_DMAREQ && !SOC_DMAIRQ && !dma_fault);
        assert(value == (reg == 0xd4 ? 0x20 : 0));
        if (mode == 6 && reg == 0xd4) SOC_DMA0CFGL = 0x21;
    } else if (reg == 0xd6) {
        assert(value == 1 && before == 0 && !active);
        ready = 0; arms++;
    } else if (reg == 0xd7) {
        assert(value == 1 && before == 0 && ready && !active);
        requests++; copied = 0; active = 1;
        if (mode != 3) transfer(mode == 4 ? 3 : 16);
        if (mode == 2 || mode == 4) ticks = (ticks + 4096) & 0xffffff;
        if (mode == 5) SOC_DMAIRQ |= 0x10;
    } else {
        assert(reg == 0xd1 && value == 0x1e && before == 1 && !active);
        SOC_DMAIRQ = before & value; acks++;
    }
}

static void prepare(void)
{
    host_mmio_reset();
    dma_fault = 0;
    memset((void *)dma_descriptor, 0, 8);
    memset((void *)&dma_fixture_a, 0, 18); memset((void *)&dma_fixture_b, 0, 18);
    SOC_CLKCONCMD = SOC_CLKCONSTA = 0xc9; SOC_SLEEPCMD = 0x84;
    SOC_IRCON = 0xa0;
    DMF_IP0 = 0x31; DMF_IP1 = 0x0e; DMF_TCON = 3; DMF_S0CON = 3; DMF_S1CON = 3;
    DMF_RFIRQF0 = 0x55; DMF_RFIRQF1 = 0xaa; DMF_IRCON2 = 0x12; SOC_RFERRF = 0x37;
    ticks = 0xfffffe; tick_step = 1;
    reads_seen = writes_seen = requests = bytes = clocks = arms = acks = mode = corruption = copied = 0;
    ready = active = invalid_owner = 0;
    host_mmio_read_hook = load; host_mmio_write_hook = store;
    host_mmio_cycles_hook = cycles; host_mmio_xaddress_hook = address;
    booting = 1; _sdcc_external_startup(); booting = 0;
    dma_fixture_initialize(); consume();
    assert(dma_fixture_state.phase == DMF_INIT);
    memcpy(baseline, (const void *)&m0_status, 32);
}

static void step(uint8_t stage, uint8_t phase)
{
    unsigned r, w;
    dma_fixture_step(); consume();
    assert(dma_fixture_state.stage == stage && dma_fixture_state.phase == phase);
    assert(!memcmp((const void *)&m0_status, baseline, 8) &&
           !memcmp((const uint8_t *)&m0_status + 9, baseline + 9, 23));
    assert(m0_status.heartbeat == dma_fixture_state.completed);
    assert(dma_fixture_state.guards[0] == 0x69 && dma_fixture_state.guards[1] == 0x96);
    if (phase != DMF_FAULT) return;
    r = reads_seen; w = writes_seen;
    memcpy(saved, (const void *)&dma_fixture_state, DMF_SIZE);
    memcpy(buffers, (const void *)&dma_fixture_a, 18);
    memcpy(buffers + 18, (const void *)&dma_fixture_b, 18);
    memcpy(frozen_descriptor, (const void *)dma_descriptor, 8);
    memcpy(frozen_work, &dma_fixture_work, sizeof(dma_fixture_work_t));
    dma_fixture_step(); consume();
    assert(r == reads_seen && w == writes_seen && !memcmp(saved, (const void *)&dma_fixture_state, DMF_SIZE));
    assert(!memcmp(buffers, (const void *)&dma_fixture_a, 18) &&
           !memcmp(buffers + 18, (const void *)&dma_fixture_b, 18) &&
           !memcmp(frozen_descriptor, (const void *)dma_descriptor, 8) &&
           !memcmp(frozen_work, &dma_fixture_work, sizeof(dma_fixture_work_t)));
}

int main(void)
{
    unsigned cycle, i, m;
    prepare(); step(0, DMF_READY);
    for (cycle = 0; cycle < 257; cycle++) {
        step(1, DMF_READY);
        assert(dma_fixture_state.length == (cycle & 15) + 1 && dma_fixture_state.checked == 36);
        for (i = 0; i < 16; i++) {
            uint8_t value = (uint8_t)(cycle + 0x31) ^ i;
            assert(dma_fixture_a.data[i] == value);
            assert(dma_fixture_b.data[i] == (i < (cycle & 15) + 1 ? value : (uint8_t)~value));
        }
        step(2, DMF_READY); step(3, DMF_READY);
        assert(dma_fixture_state.length == 16 && dma_fixture_state.checked == 36);
        for (i = 0; i < 16; i++)
            assert(dma_fixture_a.data[i] == ((uint8_t)(cycle + 0x97) ^ i) &&
                   dma_fixture_a.data[i] == dma_fixture_b.data[i]);
        step(4, DMF_READY);
        assert(dma_fixture_state.completed == ((cycle + 1) & 255));
    }
    assert(requests == 514 && arms == 514 && acks == 514 && bytes == 6289 && clocks == 514);
    for (m = 1; m <= 6; m++) {
        prepare(); step(0, DMF_READY); step(1, DMF_READY);
        if (m == 1) {
            mode = 1; tick_step = 2048; step(2, DMF_FAULT);
            assert(dma_fixture_state.clock_result == CLOCK_TIMEOUT &&
                   dma_fixture_state.clock[18] == CLOCK_ROLLBACK_UNCONFIRMED);
        } else {
            step(2, DMF_READY);
            mode = m; if (m == 3) tick_step = 0;
            step(3, DMF_FAULT);
            assert(dma_fixture_state.reason == DMF_DMA_ERROR &&
                   dma_fixture_state.dma_result == (m == 2 || m == 4 ? DMA_TIMEOUT :
                                                    m == 3 ? DMA_POLL_LIMIT : DMA_STATE_CHANGED));
            assert(!dma_fixture_state.checked && !dma_fixture_state.dma[9] && clocks == 1 && acks == 1);
            if (m == 3 || m == 4) {
                transfer(16);
                assert(!memcmp((const void *)dma_fixture_a.data, (const void *)dma_fixture_b.data, 16));
                step(3, DMF_FAULT);
                assert(dma_fault == dma_fixture_state.dma_result && acks == 1 && clocks == 1);
            }
        }
    }
    for (i = 1; i <= 36; i++) {
        prepare(); step(0, DMF_READY); step(1, DMF_READY); step(2, DMF_READY);
        corruption = i; step(3, DMF_FAULT);
        assert(dma_fixture_state.reason == DMF_BYTES && dma_fixture_state.dma_result == DMA_OK &&
               dma_fixture_state.checked == i - 1 && dma_fixture_state.mismatch_buffer == (i - 1) / 18 &&
               dma_fixture_state.mismatch_index == (i - 1) % 18);
    }
    prepare(); step(0, DMF_READY); invalid_owner = 1; step(1, DMF_FAULT);
    assert(dma_fixture_state.dma_result == DMA_BUFFER_OWNERSHIP && !requests);
    prepare(); step(0, DMF_READY); SOC_DMAREQ = 1; step(1, DMF_FAULT);
    assert(dma_fixture_state.reason == DMF_INVARIANT && !requests);
    prepare(); step(0, DMF_READY); DMF_IP0 ^= 1; step(1, DMF_FAULT);
    assert(dma_fixture_state.reason == DMF_INVARIANT && !requests);
    prepare(); step(0, DMF_READY); step(1, DMF_READY);
    mode = 8; tick_step = 2048; step(2, DMF_FAULT);
    assert(dma_fixture_state.clock_result == CLOCK_TIMEOUT &&
           dma_fixture_state.clock[18] == CLOCK_OK && clocks == 2 && requests == 1);
    prepare(); step(0, DMF_READY); step(1, DMF_READY);
    mode = 1; tick_step = 0; step(2, DMF_FAULT);
    assert(dma_fixture_state.clock_result == CLOCK_POLL_LIMIT && requests == 1);
    for (m = 0; m < 8; m++) {
        prepare(); step(0, DMF_READY);
        if (m == 0) SOC_DMAARM = 2;
        if (m == 1) SOC_DMAIRQ = 1;
        if (m == 2) SOC_IRCON |= 1;
        if (m == 3) SOC_DMA1CFGL = 1;
        if (m == 4) SOC_DMA0CFGH = 1;
        if (m == 5) SOC_IEN0 = 0x80;
        if (m == 6) SOC_CLKCONSTA = 0x88;
        if (m == 7) SOC_SLEEPCMD = 0x80;
        step(1, DMF_FAULT);
        assert(dma_fixture_state.reason == DMF_INVARIANT && !requests && !arms && !clocks);
        assert(dma_fixture_state.sample_valid == (m < 5 ? 1 : 0));
    }
    puts("DMA fixture: real drivers, 257 cycles / 514 copies / 6289 bytes, both clocks/routes, "
         "36 corruptions, partial/late/stuck/clock/ownership/state faults and terminal lifetime PASS");
    return 0;
}
