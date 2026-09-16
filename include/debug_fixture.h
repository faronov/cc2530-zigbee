/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef CC2530_DEBUG_FIXTURE_H
#define CC2530_DEBUG_FIXTURE_H

#include "bringup.h"

#define M1_FIXTURE_ABI_VERSION 1u
#define M1_FIXTURE_SIZE 16u

enum m1_fixture_phase {
    M1_FIXTURE_INITIALIZED = 1,
    M1_FIXTURE_RUNNING = 2,
    M1_FIXTURE_READY = 3
};

typedef struct {
    uint8_t signature[4];
    uint8_t abi_version;
    uint8_t byte_size;
    uint8_t phase;
    uint8_t iteration;
    uint8_t seed;
    uint8_t result;
    uint8_t checkpoints[4];
    uint8_t guards[2];
} m1_fixture_t;

#define ABI_AT(field, position) \
    typedef char m1_abi_##field[(offsetof(m1_fixture_t, field) == (position)) ? 1 : -1]
ABI_AT(signature, 0);
ABI_AT(abi_version, 4);
ABI_AT(byte_size, 5);
ABI_AT(phase, 6);
ABI_AT(iteration, 7);
ABI_AT(seed, 8);
ABI_AT(result, 9);
ABI_AT(checkpoints, 10);
ABI_AT(guards, 14);
#undef ABI_AT
typedef char m1_abi_size[(sizeof(m1_fixture_t) == M1_FIXTURE_SIZE) ? 1 : -1];

extern volatile MCU_XDATA m1_fixture_t debug_fixture_state;

void debug_fixture_initialize(void);
void debug_fixture_cycle(void);
uint8_t debug_fixture_stage0(uint8_t value);
uint8_t debug_fixture_stage1(uint8_t value);
uint8_t debug_fixture_stage2(uint8_t value);
uint8_t debug_fixture_stage3(uint8_t value);

#endif
