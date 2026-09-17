/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "dma_fixture.h"

volatile MCU_XDATA dma_fixture_t dma_fixture_state;
volatile MCU_XDATA dma_fixture_buffer_t dma_fixture_a, dma_fixture_b;
MCU_XDATA dma_fixture_work_t dma_fixture_work;
extern volatile MCU_XDATA uint8_t dma_descriptor[8];
extern MCU_XDATA uint8_t dma_fault;
#define state dma_fixture_state
#define work dma_fixture_work

static void put16(volatile MCU_XDATA uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
}

static void put32(volatile MCU_XDATA uint8_t *out, uint32_t value)
{
    put16(out, (uint16_t)value);
    put16(out + 2, (uint16_t)(value >> 16));
}

static void serialize_clock(void)
{
    put32(state.clock, work.clock.request.elapsed_ticks);
    put16(state.clock + 4, work.clock.request.polls);
    state.clock[6] = work.clock.request.timebase_status;
    put32(state.clock + 7, work.clock.rollback.elapsed_ticks);
    put16(state.clock + 11, work.clock.rollback.polls);
    state.clock[13] = work.clock.rollback.timebase_status;
    state.clock[14] = work.clock.saved_command;
    state.clock[15] = work.clock.requested_command;
    state.clock[16] = work.clock.observed_command;
    state.clock[17] = work.clock.observed_status;
    state.clock[18] = work.clock.rollback_result;
}

static void serialize_dma(void)
{
    put32(state.dma, work.dma.elapsed_ticks);
    put16(state.dma + 4, work.dma.polls);
#define COPY(field, index) state.dma[index] = work.dma.field
    COPY(timebase_status, 6); COPY(actions, 7); COPY(complete, 8); COPY(verified, 9);
    COPY(arm, 10); COPY(request, 11); COPY(irq, 12); COPY(ircon, 13);
    COPY(cfg0_low, 14); COPY(cfg0_high, 15); COPY(cfg1_low, 16); COPY(cfg1_high, 17);
    COPY(sample_valid, 18);
#undef COPY
}

static void cpu_snapshot(void)
{
    state.sample_valid = 0;
    state.command = MMIO_READ(SOC_CLKCONCMD);
    state.status = MMIO_READ(SOC_CLKCONSTA);
    state.sleep = MMIO_READ(SOC_SLEEPCMD);
    state.enables[0] = MMIO_READ(SOC_IEN0);
    state.enables[1] = MMIO_READ(SOC_IEN1);
    state.enables[2] = MMIO_READ(SOC_IEN2);
}

static uint8_t cpu_ok(void)
{
    return (state.command == 0xc9 || state.command == 0x88) && state.status == state.command &&
           state.sleep == state.initial_sleep && (state.sleep & 7u) == 4u &&
           !state.enables[0] && !state.enables[1] && !state.enables[2];
}

static void dma_snapshot(void)
{
    uint8_t i;
    state.controller[0] = MMIO_READ(SOC_DMAARM);
    state.controller[1] = MMIO_READ(SOC_DMAREQ);
    state.controller[2] = MMIO_READ(SOC_DMAIRQ);
    state.controller[3] = MMIO_READ(SOC_IRCON);
    state.controller[4] = MMIO_READ(SOC_DMA0CFGL);
    state.controller[5] = MMIO_READ(SOC_DMA0CFGH);
    state.controller[6] = MMIO_READ(SOC_DMA1CFGL);
    state.controller[7] = MMIO_READ(SOC_DMA1CFGH);
    state.flags[0] = MMIO_READ(DMF_IP0);
    state.flags[1] = MMIO_READ(DMF_IP1);
    state.flags[2] = MMIO_READ(DMF_TCON);
    state.flags[3] = MMIO_READ(DMF_S0CON);
    state.flags[4] = MMIO_READ(DMF_S1CON);
    state.flags[5] = MMIO_READ(DMF_RFIRQF0);
    state.flags[6] = MMIO_READ(DMF_RFIRQF1);
    state.flags[7] = MMIO_READ(DMF_IRCON2);
    state.flags[8] = MMIO_READ(SOC_RFERRF);
    for (i = 0; i < 8; i++)
        state.descriptor[i] = dma_descriptor[i];
    state.fault_latch = dma_fault;
    state.sample_valid = 1;
}

static uint8_t idle(void)
{
    uint8_t i;
    if (!state.sample_valid || state.fault_latch || state.controller[0] ||
        state.controller[1] || state.controller[2] || (state.controller[3] & 1u) ||
        state.controller[3] != state.initial_ircon ||
        state.controller[6] != state.initial_cfg1[0] || state.controller[7] != state.initial_cfg1[1])
        return 0;
    if (state.controller[4] != ((state.source[0] || state.source[1]) ?
        (uint8_t)MMIO_XADDRESS(dma_descriptor) : 0) ||
        state.controller[5] != ((state.source[0] || state.source[1]) ?
        (uint8_t)(MMIO_XADDRESS(dma_descriptor) >> 8) : 0))
        return 0;
    for (i = 0; i < 9; i++)
        if (state.flags[i] != state.initial_flags[i])
            return 0;
    return 1;
}

static void fault(uint8_t reason)
{
    state.reason = reason;
    state.phase = DMF_FAULT;
}

static uint8_t pattern(uint8_t index)
{
    return (uint8_t)(state.completed + (state.stage == DMF_COPY_RC ? 0x31u : 0x97u)) ^ index;
}

static void fill_buffers(void)
{
    uint8_t i, value;
    dma_fixture_a.before = dma_fixture_b.before = 0x69;
    dma_fixture_a.after = dma_fixture_b.after = 0x96;
    for (i = 0; i < 16; i++) {
        value = pattern(i);
        dma_fixture_a.data[i] = state.stage == DMF_COPY_RC ? value : (uint8_t)~value;
        dma_fixture_b.data[i] = state.stage == DMF_COPY_RC ? (uint8_t)~value : value;
    }
}

static void verify_buffers(void)
{
    uint8_t i, buffer, actual, expected;
    for (buffer = 0; buffer < 2; buffer++)
        for (i = 0; i < 18; i++) {
            if (buffer == 0)
                actual = ((volatile MCU_XDATA uint8_t *)&dma_fixture_a)[i];
            else
                actual = ((volatile MCU_XDATA uint8_t *)&dma_fixture_b)[i];
            expected = i == 0 ? 0x69 : i == 17 ? 0x96 : pattern(i - 1u);
            if (i > state.length && i < 17 &&
                buffer == (state.stage == DMF_COPY_RC ? 1u : 0u))
                expected = (uint8_t)~expected;
            if (actual != expected) {
                state.mismatch_buffer = buffer;
                state.mismatch_index = i;
                state.actual = actual;
                state.expected = expected;
                fault(DMF_BYTES);
                return;
            }
            state.checked++;
        }
}

void dma_fixture_initialize(void)
{
    uint8_t i;
    bringup_initialize();
    for (i = 0; i < DMF_SIZE; i++)
        ((volatile MCU_XDATA uint8_t *)&state)[i] = 0;
    state.signature[0] = 'M'; state.signature[1] = '2';
    state.signature[2] = 'D'; state.signature[3] = 'M';
    state.version = 1; state.size = DMF_SIZE;
    state.clock_result = CLOCK_NOT_ATTEMPTED;
    state.clock[18] = CLOCK_NOT_ATTEMPTED;
    state.dma_result = DMF_NOT_ATTEMPTED;
    state.mismatch_buffer = state.mismatch_index = 255;
    state.guards[0] = 0x69; state.guards[1] = 0x96;
    put16(state.timeout, DMF_TIMEOUT);
    put16(state.limit, DMF_LIMIT);
    cpu_snapshot();
    state.initial_sleep = state.sleep;
    if (!cpu_ok() || state.command != 0xc9 || m0_status.clock_request != 0xc9 ||
        m0_status.clock_status != 0xc9) {
        fault(DMF_ENTRY);
        return;
    }
    dma_snapshot();
    state.initial_ircon = state.controller[3];
    state.initial_cfg1[0] = state.controller[6]; state.initial_cfg1[1] = state.controller[7];
    for (i = 0; i < 9; i++) state.initial_flags[i] = state.flags[i];
    if (!idle() || state.controller[4] || state.controller[5]) {
        fault(DMF_ENTRY);
        return;
    }
    state.phase = DMF_INIT;
}

#if defined(__SDCC)
static inline void dma_fixture_step(void)
#else
void dma_fixture_step(void)
#endif
{
    uint8_t i;
    uint16_t source, destination;
    if (state.phase == DMF_FAULT)
        return;
    if ((state.phase != DMF_INIT && state.phase != DMF_READY) || state.stage > DMF_CLOCK_RC) {
        fault(DMF_PHASE);
        return;
    }
    if (state.phase == DMF_READY)
        state.stage = state.stage == DMF_CLOCK_RC ? DMF_COPY_RC : state.stage + 1u;
    state.phase = DMF_RUNNING;
    state.dma_result = DMF_NOT_ATTEMPTED;
    state.checked = state.length = 0;
    for (i = 0; i < 19; i++) state.dma[i] = 0;
    cpu_snapshot();
    if (!cpu_ok()) { fault(DMF_INVARIANT); return; }
    dma_snapshot();
    if (!idle()) { fault(DMF_INVARIANT); return; }
    if (state.stage == DMF_COPY_RC || state.stage == DMF_COPY_X) {
        if (state.command != (state.stage == DMF_COPY_RC ? 0xc9 : 0x88)) {
            fault(DMF_INVARIANT);
            return;
        }
        state.length = state.stage == DMF_COPY_RC ? (state.completed & 15u) + 1u : 16u;
        source = state.stage == DMF_COPY_RC ? MMIO_XADDRESS(dma_fixture_a.data) : MMIO_XADDRESS(dma_fixture_b.data);
        destination = state.stage == DMF_COPY_RC ? MMIO_XADDRESS(dma_fixture_b.data) : MMIO_XADDRESS(dma_fixture_a.data);
        put16(state.source, source); put16(state.destination, destination);
        fill_buffers();
        for (i = 0; i < sizeof(work.dma); i++) ((MCU_XDATA uint8_t *)&work.dma)[i] = 0;
        state.dma_result = dma_copy_init(source, destination, state.length, DMF_TIMEOUT, DMF_LIMIT, &work.dma);
        serialize_dma();
        cpu_snapshot();
        if (cpu_ok()) dma_snapshot();
        if (state.dma_result != DMA_OK) { fault(DMF_DMA_ERROR); return; }
        if (!cpu_ok() || !idle() || work.dma.actions != 15 || !work.dma.complete ||
            !work.dma.verified || !work.dma.sample_valid ||
            state.controller[4] != (uint8_t)MMIO_XADDRESS(dma_descriptor) ||
            state.controller[5] != (uint8_t)(MMIO_XADDRESS(dma_descriptor) >> 8) ||
            state.descriptor[0] != state.source[1] || state.descriptor[1] != state.source[0] ||
            state.descriptor[2] != state.destination[1] || state.descriptor[3] != state.destination[0] ||
            state.descriptor[4] || state.descriptor[5] != state.length ||
            state.descriptor[6] != 0x20 || state.descriptor[7] != 0x51) {
            fault(DMF_INVARIANT);
            return;
        }
        verify_buffers();
        if (state.phase == DMF_FAULT) return;
    } else {
        state.clock_result = clock_select_init(state.stage == DMF_CLOCK_X ? CLOCK_XOSC32 : CLOCK_RC16,
                                               DMF_TIMEOUT, DMF_LIMIT, &work.clock);
        serialize_clock();
        cpu_snapshot();
        if (state.clock_result != CLOCK_OK) { fault(DMF_CLOCK_ERROR); return; }
        if (!cpu_ok() || state.command != (state.stage == DMF_CLOCK_X ? 0x88 : 0xc9)) {
            fault(DMF_INVARIANT);
            return;
        }
        dma_snapshot();
        if (!idle()) { fault(DMF_INVARIANT); return; }
        if (state.stage == DMF_CLOCK_RC) { state.completed++; bringup_tick(); }
    }
    state.phase = DMF_READY;
}

#if defined(__SDCC)
void main(void)
{
    dma_fixture_initialize();
    if (state.phase == DMF_FAULT) dma_fixture_fault();
    dma_fixture_before();
    for (;;) {
        dma_fixture_step();
        if (state.phase == DMF_FAULT) dma_fixture_fault();
        dma_fixture_ready();
    }
}
#endif
