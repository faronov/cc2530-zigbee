/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "radio_link_fixture.h"
#include "timebase.h"

volatile MCU_XDATA radio_link_fixture_t radio_link_fixture_state;
volatile MCU_XDATA uint8_t radio_link_fixture_mailbox[8];
MCU_XDATA clock_diagnostics_t radio_link_fixture_clock;
MCU_XDATA radio_autoack_diagnostics_t radio_link_fixture_radio;
MCU_XDATA radio_autoack_config_t radio_link_fixture_config;
MCU_XDATA radio_autoack_frame_t radio_link_fixture_frames[LNK_FRAMES];
MCU_XDATA uint8_t radio_link_fixture_body[LNK_LENGTH];
MCU_XDATA uint8_t radio_link_fixture_initialized;
static const MCU_CODE uint8_t body[LNK_LENGTH] = {
    0x41, 0x88, 0x5a, 0xff, 0xff, 0xff, 0xff, 0x34, 0x12, 'L', 'N', 'K', '1'
};
static MCU_XDATA uint8_t gate, consumed, packet[8];
static MCU_XDATA uint16_t remaining, polls;
static MCU_XDATA uint32_t started, previous, deadline;
#define state radio_link_fixture_state

static void budget(void)
{
    state.remaining[0] = (uint8_t)remaining;
    state.remaining[1] = (uint8_t)(remaining >> 8);
}
static void fault(uint8_t reason)
{
    state.reason = reason; gate = LNK_FAULT; state.phase = gate;
}
static void observation(uint8_t result)
{
    state.owner_result = result;
    radio_link_fixture_radio = *radio_autoack_diagnostic();
}
static uint8_t receive(void)
{
    if (state.frames == LNK_FRAMES) { fault(LNK_CAPACITY); return 0; }
    observation(radio_autoack_receive(LNK_SERVICE_TICKS, LNK_SERVICE_LIMIT,
                                      &radio_link_fixture_frames[state.frames]));
    if (state.owner_result == RADIO_AUTOACK_FRAME || state.owner_result == RADIO_AUTOACK_BAD_CRC) {
        state.frames++;
        if (state.stage == 3) state.before_tx = state.frames;
    }
    else if (state.owner_result != RADIO_AUTOACK_EMPTY) { fault(LNK_RECEIVE); return 0; }
    return 1;
}
static uint8_t stop(void)
{
    for (;;) {
        observation(radio_autoack_stop(LNK_SERVICE_TICKS, LNK_SERVICE_LIMIT));
        if (state.owner_result == RADIO_AUTOACK_STOPPED) return 1;
        if (state.owner_result != RADIO_AUTOACK_DRAIN) { fault(LNK_STOP); return 0; }
        if (!receive()) return 0;
        if (state.owner_result == RADIO_AUTOACK_EMPTY) { fault(LNK_STOP); return 0; }
    }
}
void radio_link_fixture_initialize(void)
{
    uint8_t i;
    if (radio_link_fixture_initialized) return;
    radio_link_fixture_initialized = 1;
    bringup_initialize();
    for (i = 0; i < sizeof(state); i++) ((volatile uint8_t MCU_XDATA *)&state)[i] = 0;
    for (i = 0; i < 8; i++) {
        radio_link_fixture_mailbox[i] = 0;
        radio_link_fixture_config.ieee[i] = 0x10u + i;
    }
    radio_link_fixture_config.pan = 0x1234;
    radio_link_fixture_config.short_address = 0x5678;
    radio_link_fixture_config.channel = LNK_CHANNEL;
    radio_link_fixture_config.power = RADIO_AUTOACK_POWER_05;
    for (i = 0; i < LNK_LENGTH; i++) radio_link_fixture_body[i] = body[i];
    state.signature[0] = 'M'; state.signature[1] = '3';
    state.signature[2] = 'L'; state.signature[3] = 'K';
    state.version = 1; state.size = sizeof(state);
    gate = LNK_DISARMED; state.phase = gate; consumed = 0;
    state.clock_result = CLOCK_NOT_ATTEMPTED;
    state.tx_result = state.owner_result = 255;
    state.channel = LNK_CHANNEL; state.power = RADIO_AUTOACK_POWER_05; state.length = LNK_LENGTH;
    state.guards[0] = 0x69; state.guards[1] = 0x96;
    remaining = LNK_ADMISSION_POLLS; budget();
}
void radio_link_fixture_poll(void)
{
    uint8_t i, any = 0, opcode, token;
    uint32_t now, elapsed;
    bool expired;
    if (gate == LNK_END || gate == LNK_FAULT) return;
    if (!radio_link_fixture_initialized || state.phase != gate ||
        state.channel != LNK_CHANNEL || state.power != RADIO_AUTOACK_POWER_05 ||
        state.length != LNK_LENGTH || state.guards[0] != 0x69 || state.guards[1] != 0x96 ||
        state.consumed != consumed || state.attempts || state.stage || state.completed ||
        state.reason || state.outcome || state.frames || state.before_tx) {
        fault(LNK_INVARIANT); return;
    }
    if (radio_link_fixture_config.pan != 0x1234 ||
        radio_link_fixture_config.short_address != 0x5678 ||
        radio_link_fixture_config.channel != LNK_CHANNEL ||
        radio_link_fixture_config.power != RADIO_AUTOACK_POWER_05) {
        fault(LNK_INVARIANT); return;
    }
    for (i = 0; i < 8; i++) if (radio_link_fixture_config.ieee[i] != 0x10u + i) {
        fault(LNK_INVARIANT); return;
    }
    for (i = 0; i < LNK_LENGTH; i++) if (radio_link_fixture_body[i] != body[i]) {
        fault(LNK_INVARIANT); return;
    }
    if (gate == LNK_DISARMED || gate == LNK_ARMED) {
        if (!remaining) { fault(LNK_EXHAUSTED); return; }
        for (i = 0; i < 8; i++) { packet[i] = radio_link_fixture_mailbox[i]; any |= packet[i]; }
        for (i = 0; i < 8; i++) radio_link_fixture_mailbox[i] = 0;
        if (!any) {
            if (--remaining == 0) fault(LNK_EXHAUSTED);
            budget(); return;
        }
        opcode = gate == LNK_DISARMED ? 0xa9 : 0x56;
        token = gate == LNK_DISARMED ? 0x36 : 0xc9;
        if (packet[0] != opcode || packet[1] != (uint8_t)~opcode ||
            packet[2] != LNK_CHANNEL || packet[3] != (uint8_t)~LNK_CHANNEL ||
            packet[4] != token || packet[5] != (uint8_t)~token ||
            packet[6] != 0x4c || packet[7] != 0xb3) {
            fault(LNK_PACKET); return;
        }
        gate++; state.phase = gate;
        remaining = gate == LNK_ARMED ? LNK_ADMISSION_POLLS : 0; budget();
        return;
    }
    if (gate != LNK_ADMITTED || consumed) { fault(LNK_INVARIANT); return; }
    for (i = 0; i < 8; i++) any |= radio_link_fixture_mailbox[i];
    if (any) { fault(LNK_PACKET); return; }
    consumed = 1; state.consumed = 1; gate = LNK_RUNNING; state.phase = gate;
    state.stage = 1;
    state.clock_result = clock_select_init(CLOCK_XOSC32, LNK_SERVICE_TICKS, LNK_SERVICE_LIMIT,
                                           &radio_link_fixture_clock);
    if (state.clock_result != CLOCK_OK) { fault(LNK_CLOCK); return; }
    state.stage = 2;
    observation(radio_autoack_acquire(&radio_link_fixture_config, LNK_SERVICE_TICKS, LNK_SERVICE_LIMIT));
    if (state.owner_result != RADIO_AUTOACK_READY) { fault(LNK_ACQUIRE); return; }
    state.stage = 3;
    if (!stop()) return;
    state.before_tx = state.frames;
    if (state.frames == LNK_FRAMES) { fault(LNK_CAPACITY); return; }
    state.stage = 4; state.attempts = 1;
    state.tx_result = radio_autoack_send(radio_link_fixture_body, LNK_LENGTH,
                                        LNK_SERVICE_TICKS, LNK_SERVICE_LIMIT);
    observation(state.tx_result);
    if (state.tx_result != RADIO_AUTOACK_TX_DONE && state.tx_result != RADIO_AUTOACK_CCA_BUSY) {
        fault(LNK_TRANSMIT); return;
    }
    if (state.tx_result == RADIO_AUTOACK_CCA_BUSY) state.outcome = LNK_CCA_BUSY;
    else {
        state.stage = 5;
        started = timebase_read_awake_ticks24(); previous = started; polls = 0;
        if (timebase_deadline_after(started, LNK_RX_TICKS, &deadline) != TIMEBASE_OK) {
            fault(LNK_TIME); return;
        }
        for (;;) {
            now = timebase_read_awake_ticks24();
            elapsed = (now - started) & TIMEBASE_TICKS_MASK;
            for (i = 0; i < 4; i++) state.elapsed[i] = (uint8_t)(elapsed >> (8u * i));
            if (elapsed >= TIMEBASE_HALF_RANGE ||
                ((now - previous) & TIMEBASE_TICKS_MASK) >= TIMEBASE_HALF_RANGE ||
                timebase_expired(now, deadline, &expired) != TIMEBASE_OK) {
                fault(LNK_TIME); return;
            }
            previous = now;
            if (expired) { state.outcome = LNK_RX_TIMEOUT; break; }
            if (polls == LNK_RX_POLLS) { fault(LNK_WORK); return; }
            polls++;
            state.rx_polls[0] = (uint8_t)polls; state.rx_polls[1] = (uint8_t)(polls >> 8);
            if (!receive()) return;
            if (state.owner_result != RADIO_AUTOACK_EMPTY) {
                state.outcome = state.owner_result == RADIO_AUTOACK_FRAME ? LNK_RX_GOOD : LNK_RX_BAD;
                break;
            }
        }
    }
    state.stage = 6;
    if (!stop()) return;
    state.stage = 7; state.completed = 1; bringup_tick();
    gate = LNK_END; state.phase = gate;
}
