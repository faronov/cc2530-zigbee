/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "radio_fifo_fixture.h"

volatile MCU_XDATA radio_fifo_fixture_t radio_fifo_fixture_state;
#define state radio_fifo_fixture_state

static MCU_XDATA union {
    clock_diagnostics_t clock;
    radio_fifo_diagnostics_t fifo;
} work;
static MCU_XDATA uint8_t small[3];
static const MCU_CODE uint8_t maximum[125] = {
    0x69, 0x68, 0x6b, 0x6a, 0x6d, 0x6c, 0x6f, 0x6e,
    0x61, 0x60, 0x63, 0x62, 0x65, 0x64, 0x67, 0x66,
    0x79, 0x78, 0x7b, 0x7a, 0x7d, 0x7c, 0x7f, 0x7e,
    0x71, 0x70, 0x73, 0x72, 0x75, 0x74, 0x77, 0x76,
    0x49, 0x48, 0x4b, 0x4a, 0x4d, 0x4c, 0x4f, 0x4e,
    0x41, 0x40, 0x43, 0x42, 0x45, 0x44, 0x47, 0x46,
    0x59, 0x58, 0x5b, 0x5a, 0x5d, 0x5c, 0x5f, 0x5e,
    0x51, 0x50, 0x53, 0x52, 0x55, 0x54, 0x57, 0x56,
    0x29, 0x28, 0x2b, 0x2a, 0x2d, 0x2c, 0x2f, 0x2e,
    0x21, 0x20, 0x23, 0x22, 0x25, 0x24, 0x27, 0x26,
    0x39, 0x38, 0x3b, 0x3a, 0x3d, 0x3c, 0x3f, 0x3e,
    0x31, 0x30, 0x33, 0x32, 0x35, 0x34, 0x37, 0x36,
    0x09, 0x08, 0x0b, 0x0a, 0x0d, 0x0c, 0x0f, 0x0e,
    0x01, 0x00, 0x03, 0x02, 0x05, 0x04, 0x07, 0x06,
    0x19, 0x18, 0x1b, 0x1a, 0x1d, 0x1c, 0x1f, 0x1e,
    0x11, 0x10, 0x13, 0x12, 0x15
};

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

static void serialize_fifo(void)
{
    put32(state.fifo, work.fifo.elapsed_ticks);
    put16(state.fifo + 4, work.fifo.polls);
#define COPY(field, index) state.fifo[index] = work.fifo.field
    COPY(timebase_status, 6); COPY(strobes, 7); COPY(confirmed, 8);
    COPY(bytes_written, 9); COPY(bytes_verified, 10); COPY(errors, 11);
    COPY(rx_count, 12); COPY(tx_count, 13); COPY(rx_first, 14);
    COPY(rx_last, 15); COPY(rx_packet, 16); COPY(tx_first, 17);
    COPY(tx_last, 18); COPY(fifo_signals, 19); COPY(sample_valid, 20);
#undef COPY
}

static void cpu_snapshot(void)
{
    state.radio_valid = 0;
    state.command = MMIO_READ(SOC_CLKCONCMD);
    state.status = MMIO_READ(SOC_CLKCONSTA);
    state.sleep = MMIO_READ(SOC_SLEEPCMD);
    state.enables[0] = MMIO_READ(SOC_IEN0);
    state.enables[1] = MMIO_READ(SOC_IEN1);
    state.enables[2] = MMIO_READ(SOC_IEN2);
}

static uint8_t cpu_ok(void)
{
    return state.command == (m0_status.clock_request & 0xb8u) &&
           state.status == state.command && state.sleep == state.initial_sleep &&
           (state.sleep & 7u) == 4u && !state.enables[0] && !state.enables[1] && !state.enables[2];
}

static void radio_snapshot(void)
{
    state.radio[0] = MMIO_XREAD(0x6189);
    state.radio[1] = MMIO_XREAD(0x618a);
    state.radio[2] = MMIO_XREAD(0x61e1);
    state.radio[3] = MMIO_XREAD(0x6192);
    state.radio[4] = MMIO_XREAD(0x618b);
    state.radio[5] = MMIO_XREAD(0x6193);
    state.radio[6] = MMIO_XREAD(0x619b);
    state.radio[7] = MMIO_XREAD(0x619c);
    state.radio[8] = MMIO_XREAD(0x619d);
    state.radio[9] = MMIO_XREAD(0x619e);
    state.radio[10] = MMIO_XREAD(0x619f);
    state.radio[11] = MMIO_XREAD(0x61a1);
    state.radio[12] = MMIO_XREAD(0x61a2);
    state.radio[13] = MMIO_READ(SOC_RFERRF);
    state.flags[0] = MMIO_READ(RFF_IP0);
    state.flags[1] = MMIO_READ(RFF_IP1);
    state.flags[2] = MMIO_READ(RFF_RFIRQF0);
    state.flags[3] = MMIO_READ(RFF_RFIRQF1);
    state.flags[4] = MMIO_READ(RFF_S1CON);
    state.flags[5] = MMIO_READ(RFF_TCON);
    state.flags[6] = MMIO_XREAD(0x61a3);
    state.flags[7] = MMIO_XREAD(0x61a4);
    state.flags[8] = MMIO_XREAD(0x61a5);
    state.radio_valid = 1;
}

static uint8_t radio_ok(uint8_t count)
{
    return state.radio[0] == 0x40 && state.radio[1] == 1 && !(state.radio[2] & 0xe0u) &&
           !(state.radio[3] & 0xc0u) && !state.radio[4] && !(state.radio[5] & 0xe7u) &&
           !state.radio[6] && state.radio[7] == count && !state.radio[8] &&
           !state.radio[9] && !state.radio[10] && !state.radio[11] &&
           state.radio[12] == count && !state.radio[13];
}

static uint8_t flags_ok(void)
{
    uint8_t i;
    for (i = 0; i < 9; i++)
        if (state.flags[i] != state.initial_flags[i])
            return 0;
    return 1;
}

static void fault(uint8_t reason)
{
    state.reason = reason;
    state.phase = RFF_FAULT;
}

static void verify_payload(const uint8_t *payload, uint8_t length)
{
    uint8_t i, actual, expected;
    /* SWRU191F 23.4.2/23.8.3: inspect only our accepted TX bytes in RAM.
     * This is not an RFD read, FIFO-tail read, pointer update or RAM writer.
     */
    for (i = 0; i <= length; i++) {
        actual = MMIO_XREAD(0x6080u + i);
        expected = i == 0 ? length + 2u : payload[i - 1u];
        if (actual != expected) {
            state.mismatch_index = i;
            state.actual = actual;
            state.expected = expected;
            fault(RFF_BYTES);
            return;
        }
        state.checked++;
    }
}

void radio_fifo_fixture_initialize(void)
{
    uint8_t i;
    volatile MCU_XDATA uint8_t *bytes = (volatile MCU_XDATA uint8_t *)&state;
    bringup_initialize();
    for (i = 0; i < RFF_SIZE; i++)
        bytes[i] = 0;
    state.signature[0] = 'M'; state.signature[1] = '2';
    state.signature[2] = 'R'; state.signature[3] = 'F';
    state.version = 1;
    state.size = RFF_SIZE;
    state.clock_result = CLOCK_NOT_ATTEMPTED;
    state.clock[18] = CLOCK_NOT_ATTEMPTED;
    state.fifo_result = RFF_NOT_ATTEMPTED;
    state.mismatch_index = 255;
    put16(state.timeout, RFF_TIMEOUT);
    put16(state.limit, RFF_LIMIT);
    state.guards[0] = 0x69; state.guards[1] = 0x96;
    small[0] = 0x13; small[1] = 0x57; small[2] = 0xa9;
    cpu_snapshot();
    state.initial_sleep = state.sleep;
    if (state.command != 0xc9 || state.status != 0xc9 ||
        state.command != m0_status.clock_request || state.status != m0_status.clock_status ||
        (state.sleep & 7u) != 4u ||
        state.enables[0] || state.enables[1] || state.enables[2]) {
        fault(RFF_ENTRY);
        return;
    }
    state.phase = RFF_INIT;
}

#if defined(__SDCC)
/* Inlining removes one live return frame from the combined driver call chain. */
static inline void radio_fifo_fixture_step(void)
#else
void radio_fifo_fixture_step(void)
#endif
{
    uint8_t i, count;
    const uint8_t *payload = small;
    if (state.phase == RFF_FAULT)
        return;
    if ((state.phase != RFF_INIT && state.phase != RFF_READY) || state.stage > RFF_CLEAR_MAX) {
        fault(RFF_PHASE);
        return;
    }
    if (state.phase == RFF_READY)
        state.stage = state.stage == RFF_CLEAR_MAX ? RFF_EMPTY : state.stage + 1;
    state.phase = RFF_RUNNING;
    state.checked = 0;
    state.fifo_result = RFF_NOT_ATTEMPTED;
    for (i = 0; i < 21; i++)
        state.fifo[i] = 0;
    if (state.stage == RFF_CLOCK) {
        state.clock_result = clock_select_init(CLOCK_XOSC32, RFF_TIMEOUT, RFF_LIMIT, &work.clock);
        serialize_clock();
        cpu_snapshot();
        if (state.clock_result != CLOCK_OK) {
            fault(RFF_CLOCK_ERROR);
            return;
        }
        if (!cpu_ok()) {
            fault(RFF_INVARIANT);
            return;
        }
        radio_snapshot();
        for (i = 0; i < 9; i++)
            state.initial_flags[i] = state.flags[i];
        if (!radio_ok(0)) {
            fault(RFF_ENTRY);
            return;
        }
    } else {
        cpu_snapshot();
        if (!cpu_ok()) {
            fault(RFF_INVARIANT);
            return;
        }
        radio_snapshot();
        count = state.stage == RFF_CLEAR_SMALL ? 4 : state.stage == RFF_CLEAR_MAX ? 126 : 0;
        if (!radio_ok(count) || !flags_ok()) {
            fault(RFF_INVARIANT);
            return;
        }
        if (state.stage == RFF_SMALL || state.stage == RFF_MAX) {
            if (state.stage == RFF_SMALL) {
                payload = small;
                count = 3;
            } else {
                payload = maximum;
                count = 125;
            }
            state.fifo_result = radio_fifo_preload_init(payload, count, RFF_TIMEOUT, RFF_LIMIT, &work.fifo);
        } else
            state.fifo_result = radio_fifo_clear_init(RFF_TIMEOUT, RFF_LIMIT, &work.fifo);
        serialize_fifo();
        cpu_snapshot();
        if (cpu_ok())
            radio_snapshot();
        if (state.fifo_result != (state.stage == RFF_EMPTY ? RADIO_FIFO_EMPTY : RADIO_FIFO_OK)) {
            fault(RFF_FIFO_ERROR);
            return;
        }
        if (!cpu_ok() || !radio_ok(state.stage == RFF_SMALL ? 4 : state.stage == RFF_MAX ? 126 : 0) ||
            !flags_ok()) {
            fault(RFF_INVARIANT);
            return;
        }
        if (state.stage == RFF_SMALL || state.stage == RFF_MAX)
            verify_payload(payload, count);
        radio_snapshot();
        count = state.stage == RFF_SMALL ? 4 : state.stage == RFF_MAX ? 126 : 0;
        if (state.phase == RFF_FAULT)
            return;
        if (!radio_ok(count) || !flags_ok()) {
            fault(RFF_INVARIANT);
            return;
        }
        if (state.stage == RFF_CLEAR_MAX) {
            state.completed++;
            bringup_tick();
        }
    }
    state.phase = RFF_READY;
}

#if defined(__SDCC)
void main(void)
{
    radio_fifo_fixture_initialize();
    if (state.phase == RFF_FAULT)
        radio_fifo_fixture_fault();
    radio_fifo_fixture_before();
    for (;;) {
        if (state.phase == RFF_FAULT)
            radio_fifo_fixture_fault();
        radio_fifo_fixture_step();
        if (state.phase == RFF_FAULT)
            radio_fifo_fixture_fault();
        radio_fifo_fixture_ready();
    }
}
#endif
