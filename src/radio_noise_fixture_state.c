/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "radio_noise_fixture.h"
#include <string.h>

volatile MCU_XDATA radio_noise_fixture_t radio_noise_fixture_state;
volatile MCU_XDATA uint8_t radio_noise_fixture_command[RNF_COMMAND_SIZE];
MCU_XDATA radio_noise_request_t radio_noise_fixture_request;
MCU_XDATA radio_noise_capture_t radio_noise_fixture_capture;
MCU_XDATA noise_health_t radio_noise_fixture_health;
MCU_XDATA clock_diagnostics_t radio_noise_fixture_clock;
MCU_XDATA uint8_t radio_noise_fixture_initialized;
static MCU_XDATA uint8_t gate, consumed, packet[RNF_COMMAND_SIZE];
/* Exact opcode/complement, channel/complement, samples16, interval16,
 * timeout24, limit16, magic/complement, stage token. Little endian.
 */
const MCU_CODE uint8_t radio_noise_fixture_arm[RNF_COMMAND_SIZE] = {
    0xa6, 0x59, 0x1a, 0xe5, 0, 4, 1, 0, 0xa0, 0x86, 1, 0x10, 0x27, 0x67, 0x98, 0x3c
};
const MCU_CODE uint8_t radio_noise_fixture_run[RNF_COMMAND_SIZE] = {
    0x59, 0xa6, 0x1a, 0xe5, 0, 4, 1, 0, 0xa0, 0x86, 1, 0x10, 0x27, 0x67, 0x98, 0xc3
};
#define state radio_noise_fixture_state
#define request radio_noise_fixture_request
#define capture radio_noise_fixture_capture
#define health radio_noise_fixture_health

typedef char state_size_check[sizeof(state) == RNF_SIZE ? 1 : -1];
#if defined(__SDCC)
typedef char request_size_check[sizeof(request) == 11 ? 1 : -1];
typedef char capture_size_check[sizeof(capture) == 171 ? 1 : -1];
typedef char health_size_check[sizeof(health) == 15 ? 1 : -1];
typedef char clock_size_check[sizeof(radio_noise_fixture_clock) == 19 ? 1 : -1];
#endif

static void fault(uint8_t reason)
{
    state.reason = reason; gate = RNF_FAULT; state.phase = gate;
}
void radio_noise_fixture_initialize(void)
{
    uint8_t i;
    if (radio_noise_fixture_initialized) return;
    radio_noise_fixture_initialized = 1;
    bringup_initialize();
    for (i = 0; i < sizeof(state); i++) ((volatile uint8_t MCU_XDATA *)&state)[i] = 0;
    for (i = 0; i < RNF_COMMAND_SIZE; i++) radio_noise_fixture_command[i] = 0;
    memset(&capture, 0, sizeof(capture));
    memset(&health, 0, sizeof(health));
    memset(&radio_noise_fixture_clock, 0, sizeof(radio_noise_fixture_clock));
    request.timeout = 100000UL; request.samples = 1024;
    request.interval = 1; request.limit = 10000; request.channel = 26;
    state.signature[0] = 'M'; state.signature[1] = '2';
    state.signature[2] = 'R'; state.signature[3] = 'N';
    state.version = 1; state.size = sizeof(state);
    gate = RNF_DISARMED; state.phase = gate; consumed = 0;
    state.result = state.health_result = 255; state.clock_result = CLOCK_NOT_ATTEMPTED;
    state.channel = 26; state.profile = 1;
    state.guards[0] = 0x69; state.guards[1] = 0x96;
}
void radio_noise_fixture_poll(void)
{
    uint8_t i, any = 0, result;
    uint16_t bit;
    if (gate == RNF_END || gate == RNF_FAULT) return;
    /* Snapshot/consume once. No concurrent debugger writer is permitted. */
    for (i = 0; i < RNF_COMMAND_SIZE; i++) {
        packet[i] = radio_noise_fixture_command[i]; any |= packet[i];
        radio_noise_fixture_command[i] = 0;
    }
    if (!radio_noise_fixture_initialized || state.phase != gate ||
        state.signature[0] != 'M' || state.signature[1] != '2' ||
        state.signature[2] != 'R' || state.signature[3] != 'N' ||
        state.version != 1 || state.size != sizeof(state) ||
        (gate != RNF_DISARMED && gate != RNF_ADMITTED) ||
        consumed || state.attempts || state.reason || state.result != 255 ||
        state.health_result != 255 || state.first_failure[0] || state.first_failure[1] ||
        state.channel != 26 || state.profile != 1 || state.guards[0] != 0x69 ||
        state.guards[1] != 0x96 || state.reserved[0] || state.reserved[1] ||
        state.clock_result != (gate == RNF_DISARMED ? CLOCK_NOT_ATTEMPTED : CLOCK_OK) ||
        request.timeout != 100000UL || request.samples != 1024 || request.interval != 1 ||
        request.limit != 10000 || request.channel != 26) {
        fault(RNF_INVARIANT); return;
    }
    if (!any) return; /* No timer read, radio access, clock setup or expiry. */
    for (i = 0; i < RNF_COMMAND_SIZE; i++) {
        if (packet[i] != (gate == RNF_DISARMED ? radio_noise_fixture_arm[i] : radio_noise_fixture_run[i])) {
            fault(RNF_PACKET); return;
        }
    }
    /* Full-reset RC16 history is independently admitted by the operator.
     * Matching registers alone cannot establish history. Never "repair" it.
     */
    if (MMIO_READ(SOC_CLKCONCMD) != (gate == RNF_DISARMED ? 0xc9 : 0x88) ||
        MMIO_READ(SOC_CLKCONSTA) != (gate == RNF_DISARMED ? 0xc9 : 0x88)) {
        fault(RNF_INVARIANT); return;
    }
    if (gate == RNF_DISARMED) {
        state.clock_result = clock_select_init(CLOCK_XOSC32, 1024, 4096, &radio_noise_fixture_clock);
        if (state.clock_result != CLOCK_OK) { fault(RNF_CLOCK); return; }
        gate = RNF_ADMITTED; state.phase = gate; return; /* ARM never starts RX. */
    }
    consumed = 1; state.attempts = 1; /* Consume before the only collector call. */
    state.result = radio_noise_collect(&request, &capture);
    state.health_result = noise_health_start(&health, 21, 589);
    for (bit = 0; bit < capture.samples; bit++) {
        result = noise_health_push(&health, (uint8_t)((capture.data[bit >> 3] >> (bit & 7u)) & 1u));
        state.health_result = result;
        if (result && !state.first_failure[0] && !state.first_failure[1]) {
            state.first_failure[0] = (uint8_t)(bit + 1);
            state.first_failure[1] = (uint8_t)((bit + 1) >> 8);
        }
    }
    if (state.result != RADIO_NOISE_OK) { fault(RNF_ACQUISITION); return; }
    if (capture.samples != 1024 || capture.timed_samples != 1024 || capture.phase != 5 ||
        capture.actions != 3 || (state.health_result != NOISE_HEALTH_OK &&
        state.health_result != NOISE_HEALTH_RCT_FAILURE && state.health_result != NOISE_HEALTH_APT_FAILURE)) {
        fault(RNF_INVARIANT); return;
    }
    bringup_tick(); gate = RNF_END; state.phase = gate;
}
