/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "timebase_fixture.h"

void timebase_fixture_before_sample(void) __naked
{
    __asm
        nop
        ret
    __endasm;
}

void timebase_fixture_ready_stop(void) __naked
{
    __asm
        nop
        ret
    __endasm;
}

void timebase_fixture_fault_stop(void) __naked
{
    __asm
        nop
        sjmp _timebase_fixture_fault_stop
    __endasm;
}

void main(void)
{
    timebase_fixture_initialize();
    for (;;) {
        timebase_fixture_before_sample();
        timebase_fixture_begin();
        while (timebase_fixture_state.phase == TIMEBASE_FIXTURE_RUNNING)
            timebase_fixture_poll();
        if (timebase_fixture_state.phase == TIMEBASE_FIXTURE_FAULT)
            timebase_fixture_fault_stop();
        timebase_fixture_ready_stop();
    }
}
