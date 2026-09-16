/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "debug_fixture.h"

volatile MCU_XDATA m1_fixture_t debug_fixture_state;

void debug_fixture_initialize(void)
{
    uint8_t offset;
    volatile MCU_XDATA uint8_t *storage = (volatile MCU_XDATA uint8_t *)&debug_fixture_state;

    bringup_initialize();
    for (offset = 0; offset < M1_FIXTURE_SIZE; offset++)
        storage[offset] = 0;
    debug_fixture_state.abi_version = M1_FIXTURE_ABI_VERSION;
    debug_fixture_state.byte_size = M1_FIXTURE_SIZE;
    debug_fixture_state.guards[0] = 0x69;
    debug_fixture_state.guards[1] = 0x96;
    debug_fixture_state.signature[0] = 'M';
    debug_fixture_state.signature[1] = '1';
    debug_fixture_state.signature[2] = 'D';
    debug_fixture_state.signature[3] = 'B';
    debug_fixture_state.phase = M1_FIXTURE_INITIALIZED;
}

uint8_t debug_fixture_stage3(uint8_t value)
{
    debug_fixture_state.checkpoints[3] = (uint8_t)(value + 0x37u);
    return debug_fixture_state.checkpoints[3];
}

uint8_t debug_fixture_stage2(uint8_t value)
{
    debug_fixture_state.checkpoints[2] = (uint8_t)((value << 1) | (value >> 7));
    return (uint8_t)(debug_fixture_stage3(debug_fixture_state.checkpoints[2]) ^ 0x3cu);
}

uint8_t debug_fixture_stage1(uint8_t value)
{
    debug_fixture_state.checkpoints[1] = (uint8_t)(value ^ 0xa5u);
    return (uint8_t)(debug_fixture_stage2(debug_fixture_state.checkpoints[1]) + 3u);
}

uint8_t debug_fixture_stage0(uint8_t value)
{
    debug_fixture_state.checkpoints[0] = (uint8_t)(value + 0x11u);
    return (uint8_t)(debug_fixture_stage1(debug_fixture_state.checkpoints[0]) ^ debug_fixture_state.seed);
}

void debug_fixture_cycle(void)
{
    debug_fixture_state.phase = M1_FIXTURE_RUNNING;
    debug_fixture_state.seed = (uint8_t)(debug_fixture_state.iteration ^ 0x5au);
    debug_fixture_state.result = debug_fixture_stage0(debug_fixture_state.seed);
    debug_fixture_state.iteration = (uint8_t)(debug_fixture_state.iteration + 1u);
    bringup_tick();
    debug_fixture_state.phase = M1_FIXTURE_READY;
}
