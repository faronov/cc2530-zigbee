/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "radio_tx_fixture.h"

volatile MCU_XDATA radio_tx_fixture_t radio_tx_fixture_state;
volatile MCU_XDATA uint8_t radio_tx_fixture_mailbox[8];
MCU_XDATA clock_diagnostics_t radio_tx_fixture_clock;
MCU_XDATA radio_fifo_diagnostics_t radio_tx_fixture_fifo;
MCU_XDATA radio_tx_diagnostics_t radio_tx_fixture_tx;
/* Public synthetic legacy DATA, PAN/destination broadcast, synthetic short
 * source1234, sequence5A, "TXF1". No real identity, security or ACK request.
 * Hardware appends FCS; this is not a Zigbee/application/network frame.
 */
const MCU_CODE uint8_t radio_tx_fixture_body[TXF_LENGTH] = {
    0x41, 0x88, 0x5a, 0xff, 0xff, 0xff, 0xff, 0x34, 0x12, 'T', 'X', 'F', '1'
};
MCU_XDATA uint8_t radio_tx_fixture_initialized;
static MCU_XDATA uint8_t gate, consumed, packet[8];
static MCU_XDATA uint16_t remaining;
#define state radio_tx_fixture_state

static void budget(void)
{
    state.remaining[0] = (uint8_t)remaining;
    state.remaining[1] = (uint8_t)(remaining >> 8);
}
static void fault(uint8_t reason)
{
    state.reason = reason; gate = TXF_FAULT; state.phase = TXF_FAULT;
}
void radio_tx_fixture_initialize(void)
{
    uint8_t i;
    if (radio_tx_fixture_initialized) return;
    radio_tx_fixture_initialized = 1;
    bringup_initialize();
    for (i = 0; i < sizeof(state); i++) ((volatile uint8_t MCU_XDATA *)&state)[i] = 0;
    for (i = 0; i < 8; i++) radio_tx_fixture_mailbox[i] = 0;
    state.signature[0] = 'M'; state.signature[1] = '3';
    state.signature[2] = 'T'; state.signature[3] = 'X';
    state.version = 1; state.size = sizeof(state);
    gate = TXF_DISARMED; state.phase = gate; consumed = 0;
    state.clock_result = CLOCK_NOT_ATTEMPTED;
    state.fifo_result = state.tx_result = 255;
    state.channel = TXF_CHANNEL; state.power = RADIO_TX_POWER_05; state.length = TXF_LENGTH;
    state.guards[0] = 0x69; state.guards[1] = 0x96;
    remaining = TXF_ADMISSION_POLLS; budget();
}
void radio_tx_fixture_poll(void)
{
    uint8_t i, any = 0, opcode, token;
    if (gate == TXF_END || gate == TXF_FAULT) return;
    if (!radio_tx_fixture_initialized || state.phase != gate ||
        state.channel != TXF_CHANNEL || state.power != RADIO_TX_POWER_05 || state.length != TXF_LENGTH ||
        state.guards[0] != 0x69 || state.guards[1] != 0x96 ||
        state.attempts != consumed || state.stage || state.completed || state.reason) {
        fault(TXF_INVARIANT); return;
    }
    if (gate == TXF_DISARMED || gate == TXF_ARMED) {
        if (!remaining) { fault(TXF_EXHAUSTED); return; }
        /* Debugger writes are permitted ONLY while halted at WAIT. Snapshot
         * each byte once, clear every packet (including rejected packets).
         */
        for (i = 0; i < 8; i++) { packet[i] = radio_tx_fixture_mailbox[i]; any |= packet[i]; }
        for (i = 0; i < 8; i++) radio_tx_fixture_mailbox[i] = 0;
        if (!any) {
            if (--remaining == 0) fault(TXF_EXHAUSTED);
            budget(); return;
        }
        opcode = gate == TXF_DISARMED ? 0xa6 : 0x59;
        token = gate == TXF_DISARMED ? 0x3c : 0xc3;
        if (packet[0] != opcode || packet[1] != (uint8_t)~opcode ||
            packet[2] != TXF_CHANNEL || packet[3] != (uint8_t)~TXF_CHANNEL ||
            packet[4] != token || packet[5] != (uint8_t)~token ||
            packet[6] != 0x69 || packet[7] != 0x96) {
            fault(TXF_PACKET); return;
        }
        gate++; state.phase = gate;
        remaining = gate == TXF_ARMED ? TXF_ADMISSION_POLLS : 0; budget();
        return; /* ADMITTED: no clock/FIFO/RF work, not even a timer read. */
    }
    if (gate != TXF_ADMITTED || consumed) { fault(TXF_INVARIANT); return; }
    for (i = 0; i < 8; i++) any |= radio_tx_fixture_mailbox[i];
    if (any) { fault(TXF_PACKET); return; }
    gate = TXF_RUNNING; state.phase = gate;
    state.stage = 1;
    state.clock_result = clock_select_init(CLOCK_XOSC32, TXF_TIMEOUT, TXF_LIMIT,
                                           &radio_tx_fixture_clock);
    if (state.clock_result != CLOCK_OK) { fault(TXF_CLOCK); return; }
    state.stage = 2;
    state.fifo_result = radio_fifo_clear_init(TXF_TIMEOUT, TXF_LIMIT, &radio_tx_fixture_fifo);
    if (state.fifo_result != RADIO_FIFO_OK && state.fifo_result != RADIO_FIFO_EMPTY) {
        fault(TXF_CLEAR); return;
    }
    state.stage = 3;
    state.fifo_result = radio_fifo_preload_init(radio_tx_fixture_body, TXF_LENGTH,
                                               TXF_TIMEOUT, TXF_LIMIT, &radio_tx_fixture_fifo);
    if (state.fifo_result != RADIO_FIFO_OK) { fault(TXF_PRELOAD); return; }
    /* Consume the sole attempt BEFORE calling the controller, including error. */
    consumed = 1; state.attempts = 1; state.stage = 4;
    state.tx_result = radio_tx_send_init(RADIO_TX_IF_CLEAR, TXF_CHANNEL, RADIO_TX_POWER_05,
                                         TXF_LENGTH, TXF_TIMEOUT, TXF_LIMIT, &radio_tx_fixture_tx);
    if (state.tx_result != RADIO_TX_PHY_DONE && state.tx_result != RADIO_TX_CCA_BUSY) {
        fault(TXF_TRANSMIT); return;
    }
    state.stage = 5;
    state.fifo_result = radio_fifo_clear_init(TXF_TIMEOUT, TXF_LIMIT, &radio_tx_fixture_fifo);
    if (state.fifo_result != RADIO_FIFO_OK && state.fifo_result != RADIO_FIFO_EMPTY) {
        fault(TXF_FINAL_CLEAR); return;
    }
    state.stage = 6; state.completed = 1; bringup_tick();
    gate = TXF_END; state.phase = gate;
}
