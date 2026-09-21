/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef RADIO_NOISE_FIXTURE_H
#define RADIO_NOISE_FIXTURE_H
#include "bringup.h"
#include "clock.h"
#include "radio_noise.h"
#include "noise_health.h"

#define RNF_SIZE 20u
#define RNF_COMMAND_SIZE 16u
#define RNF_DISARMED 1u
#define RNF_ADMITTED 2u
#define RNF_END 3u
#define RNF_FAULT 4u
#define RNF_PACKET 1u
#define RNF_INVARIANT 2u
#define RNF_CLOCK 3u
#define RNF_ACQUISITION 4u

typedef struct {
    uint8_t signature[4], version, size, phase, reason;
    uint8_t attempts, result, health_result, clock_result;
    uint8_t first_failure[2], channel, profile, reserved[2], guards[2];
} radio_noise_fixture_t;

extern volatile MCU_XDATA radio_noise_fixture_t radio_noise_fixture_state;
extern volatile MCU_XDATA uint8_t radio_noise_fixture_command[RNF_COMMAND_SIZE];
extern MCU_XDATA radio_noise_request_t radio_noise_fixture_request;
extern MCU_XDATA radio_noise_capture_t radio_noise_fixture_capture;
extern MCU_XDATA noise_health_t radio_noise_fixture_health;
extern MCU_XDATA clock_diagnostics_t radio_noise_fixture_clock;
extern MCU_XDATA uint8_t radio_noise_fixture_initialized;
extern const MCU_CODE uint8_t radio_noise_fixture_arm[RNF_COMMAND_SIZE];
extern const MCU_CODE uint8_t radio_noise_fixture_run[RNF_COMMAND_SIZE];

/* Full CRT reset is the only new epoch. initialize() cannot rearm a live epoch.
 * Command writes ONLY while halted at WAIT, exact ARM then RUN packets.
 * Empty input has no hardware effects. No debugger stop during acquisition.
 * These diagnostic 21/589 cutoffs do NOT qualify entropy or output a seed.
 * END/FAULT retain ownership; neither is a normal RX/TX handoff. On failure
 * RX may remain active. No fixture cleanup, retry or reset is performed.
 */
void radio_noise_fixture_initialize(void);
void radio_noise_fixture_poll(void);
#endif
