/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "debug_fixture.h"
#include "host_mmio.h"

#include <assert.h>
#include <stdio.h>

static void exercise_fixture(void)
{
    unsigned cycle, i, operations;
    uint8_t saved[M0_STATUS_SIZE];
    const volatile uint8_t *status = (const volatile uint8_t *)&m0_status;
    volatile uint8_t *state = (volatile uint8_t *)&debug_fixture_state;

    for (i = 0; i < M1_FIXTURE_SIZE; i++)
        state[i] = 0xa5;
    operations = write_count;
    debug_fixture_initialize();
    assert(write_count == operations);
    assert(state[0] == 'M' && state[1] == '1' && state[2] == 'D' && state[3] == 'B');
    assert(state[4] == 1 && state[5] == 16 && state[6] == 1);
    for (i = 7; i < 14; i++)
        assert(state[i] == 0);
    assert(state[14] == 0x69 && state[15] == 0x96);
    for (i = 0; i < M0_STATUS_SIZE; i++)
        saved[i] = status[i];

    for (cycle = 0; cycle < 512; cycle++) {
        unsigned seed = (cycle & 255u) ^ 0x5au;
        unsigned first = (seed + 0x11u) & 255u;
        unsigned second = first ^ 0xa5u;
        unsigned third = ((second * 2u) + (second / 128u)) & 255u;
        unsigned fourth = (third + 0x37u) & 255u;
        unsigned result = (((fourth ^ 0x3cu) + 3u) & 255u) ^ seed;
        debug_fixture_cycle();
        assert(state[0] == 'M' && state[1] == '1' && state[2] == 'D' && state[3] == 'B');
        assert(state[4] == 1 && state[5] == 16 && state[6] == 3);
        assert(state[7] == (uint8_t)(cycle + 1u) && state[8] == seed && state[9] == result);
        assert(state[10] == first && state[11] == second && state[12] == third && state[13] == fourth);
        assert(state[14] == 0x69 && state[15] == 0x96);
        assert(m0_status.heartbeat == (uint8_t)(cycle + 1u));
        for (i = 0; i < M0_STATUS_SIZE; i++) {
            if (i != 8)
                assert(status[i] == saved[i]);
        }
        assert(write_count == operations);
    }
}

int main(void)
{
    host_mmio_reset();
    assert(_sdcc_external_startup() == 0);
    exercise_fixture();
    exercise_fixture();
    printf("host debug fixture board=%u: ABI, reinitialization, all seeds, wrap and no MMIO writes PASS\n",
           (unsigned)CC2530_BOARD);
    return 0;
}
