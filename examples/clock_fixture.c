/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "clock_fixture.h"

void clock_fixture_before_call(void) __naked
{
    __asm
        nop
        ret
    __endasm;
}

void clock_fixture_ready_stop(void) __naked
{
    __asm
        nop
        ret
    __endasm;
}

void clock_fixture_fault_stop(void) __naked
{
    __asm
        nop
        sjmp _clock_fixture_fault_stop
    __endasm;
}

void main(void)
{
    clock_fixture_initialize();
    for (;;) {
        if (clock_fixture_state.phase == CLOCK_FIXTURE_FAULT)
            clock_fixture_fault_stop();
        clock_fixture_before_call();
        clock_fixture_cycle();
        if (clock_fixture_state.phase == CLOCK_FIXTURE_FAULT)
            clock_fixture_fault_stop();
        clock_fixture_ready_stop();
    }
}
