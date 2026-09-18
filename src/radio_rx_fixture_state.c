/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "radio_rx_fixture.h"

volatile MCU_XDATA radio_rx_fixture_t radio_rx_fixture_state;
MCU_XDATA radio_rx_frame_t radio_rx_fixture_frame;
MCU_XDATA radio_rx_diagnostics_t radio_rx_fixture_diagnostics;
MCU_XDATA clock_diagnostics_t radio_rx_fixture_clock;
extern MCU_XDATA uint8_t radio_rx_fault;
#define state radio_rx_fixture_state
#define d radio_rx_fixture_diagnostics
#define c radio_rx_fixture_clock

static void put16(volatile MCU_XDATA uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)value; out[1] = (uint8_t)(value >> 8);
}
static void put32(volatile MCU_XDATA uint8_t *out, uint32_t value)
{
    put16(out, (uint16_t)value); put16(out + 2, (uint16_t)(value >> 16));
}
static void serialize_clock(void)
{
    put32(state.clock, c.request.elapsed_ticks); put16(state.clock + 4, c.request.polls);
    state.clock[6] = c.request.timebase_status;
    put32(state.clock + 7, c.rollback.elapsed_ticks); put16(state.clock + 11, c.rollback.polls);
    state.clock[13] = c.rollback.timebase_status;
    state.clock[14] = c.saved_command; state.clock[15] = c.requested_command;
    state.clock[16] = c.observed_command; state.clock[17] = c.observed_status;
    state.clock[18] = c.rollback_result;
}
static void serialize_rx(void)
{
    put32(state.diagnostic, d.elapsed_ticks); put16(state.diagnostic + 4, d.polls);
#define COPY(field, index) state.diagnostic[index] = d.field
    COPY(timebase_status, 6); COPY(phase, 7); COPY(writes, 8); COPY(verified, 9);
    COPY(actions, 10); COPY(sample_valid, 11); COPY(rx_enable, 12); COPY(fsm0, 13);
    COPY(signals, 14); COPY(rx_count, 15); COPY(tx_count, 16); COPY(rx_first, 17);
    COPY(rx_last, 18); COPY(rx_packet, 19); COPY(tx_first, 20); COPY(tx_last, 21);
    COPY(errors, 22); COPY(flags0, 23); COPY(flags1, 24); COPY(rssi_valid, 25);
    COPY(bytes_read, 26); COPY(phr, 27); COPY(rssi_raw, 28);
    COPY(crc_correlation, 29); COPY(discarded_bytes, 30);
#undef COPY
}
static void snapshot(void)
{
    state.command = MMIO_READ(SOC_CLKCONCMD); state.status = MMIO_READ(SOC_CLKCONSTA);
    state.sleep = MMIO_READ(SOC_SLEEPCMD);
    state.enables[0] = MMIO_READ(SOC_IEN0); state.enables[1] = MMIO_READ(SOC_IEN1);
    state.enables[2] = MMIO_READ(SOC_IEN2);
    state.flags[0] = MMIO_READ(RXF_IP0); state.flags[1] = MMIO_READ(RXF_IP1);
    state.flags[2] = MMIO_READ(RXF_TCON); state.flags[3] = MMIO_READ(RXF_S0CON);
    state.flags[4] = MMIO_READ(RXF_S1CON); state.flags[5] = MMIO_READ(RXF_IRCON2);
    state.flags[6] = MMIO_READ(SOC_IRCON);
    state.fault_latch = radio_rx_fault;
}
static uint8_t unchanged(uint8_t command)
{
    uint8_t i, previous = state.flags[6];
    snapshot();
    if (state.command != command || state.status != command ||
        state.sleep != state.initial_sleep || (state.sleep & 7u) != 4u ||
        state.enables[0] || state.enables[1] || state.enables[2])
        return 0;
    for (i = 0; i < 6; i++)
        if (state.flags[i] != state.initial_flags[i]) return 0;
    /* SWRU191F p.47, 11.1-11.2: STIF can latch with IRQs disabled.
     * RF flags instead belong to the driver's raw sequential diagnostics.
     */
    return (state.flags[6] & 0x7fu) == (state.initial_flags[6] & 0x7fu) &&
        (state.flags[6] == previous || state.flags[6] == (previous | 0x80u));
}
static void fault(uint8_t reason)
{
    state.reason = reason; state.phase = RXF_FAULT; state.fault_latch = radio_rx_fault;
}
static void poison_frame(void)
{
    uint8_t i;
    for (i = 0; i < 128; i++)
        ((MCU_XDATA uint8_t *)&radio_rx_fixture_frame)[i] = 0xa5;
}
void radio_rx_fixture_initialize(void)
{
    uint8_t i;
    bringup_initialize();
    for (i = 0; i < RXF_SIZE; i++) ((volatile MCU_XDATA uint8_t *)&state)[i] = 0;
    state.signature[0] = 'M'; state.signature[1] = '2';
    state.signature[2] = 'R'; state.signature[3] = 'X';
    state.version = 1; state.size = RXF_SIZE; state.result = 255;
    state.clock_result = state.clock[18] = CLOCK_NOT_ATTEMPTED;
    state.channel = 15; state.maximum = RXF_ATTEMPTS;
    put32(state.timeout, RXF_TIMEOUT); put16(state.limit, RXF_LIMIT);
    state.guards[0] = 0x69; state.guards[1] = 0x96; state.guards[2] = 0xc7;
    poison_frame();
    snapshot();
    state.initial_sleep = state.sleep;
    for (i = 0; i < 7; i++) state.initial_flags[i] = state.flags[i];
    if (state.command != 0xc9 || state.status != 0xc9 ||
        m0_status.clock_request != 0xc9 || m0_status.clock_status != 0xc9 ||
        (state.sleep & 7u) != 4u || state.enables[0] || state.enables[1] ||
        state.enables[2] || state.fault_latch) { fault(RXF_ENTRY); return; }
    state.phase = RXF_INIT;
}
#if defined(__SDCC)
static inline void radio_rx_fixture_step(void)
#else
void radio_rx_fixture_step(void)
#endif
{
    uint8_t ok;
    if (state.phase == RXF_FAULT || state.phase == RXF_END) return;
    if ((state.phase != RXF_INIT && state.phase != RXF_READY) || state.stage > 1 ||
        state.attempt > RXF_ATTEMPTS) { fault(RXF_PHASE); return; }
    state.phase = RXF_RUNNING;
    if (!unchanged(state.stage ? 0x88 : 0xc9) || state.fault_latch) {
        fault(RXF_INVARIANT); return;
    }
    if (!state.stage) {
        state.clock_result = clock_select_init(CLOCK_XOSC32, 1024, 4096, &c);
        serialize_clock();
        ok = unchanged(0x88);
        if (state.clock_result != CLOCK_OK) { fault(RXF_CLOCK_ERROR); return; }
        if (!ok) { fault(RXF_INVARIANT); return; }
        state.stage = 1;
    } else {
        if (state.attempt == RXF_ATTEMPTS) { state.stage = 2; state.phase = RXF_END; return; }
        poison_frame();
        state.attempt++;
        state.result = radio_rx_receive_init(15, RXF_TIMEOUT, RXF_LIMIT, &radio_rx_fixture_frame, &d);
        serialize_rx();
        ok = unchanged(0x88);
        /* Retain the original service failure, even if the CPU snapshot also fails. */
        if (state.result != RADIO_RX_OK && state.result != RADIO_RX_BAD_CRC) {
            fault(RXF_RX_ERROR); return;
        }
        if (!ok || state.fault_latch) { fault(RXF_INVARIANT); return; }
        if (state.result == RADIO_RX_OK) { state.completed++; bringup_tick(); }
    }
    state.phase = RXF_READY;
}
#if defined(__SDCC)
void main(void)
{
    radio_rx_fixture_initialize();
    if (state.phase == RXF_FAULT) radio_rx_fixture_fault();
    radio_rx_fixture_before();
    for (;;) {
        radio_rx_fixture_step();
        if (state.phase == RXF_FAULT) radio_rx_fixture_fault();
        if (state.phase == RXF_END) radio_rx_fixture_end();
        radio_rx_fixture_ready();
    }
}
#endif
