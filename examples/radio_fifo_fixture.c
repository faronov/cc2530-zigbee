/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "radio_fifo_fixture.h"

void radio_fifo_fixture_before(void) __naked
{
    __asm
        nop
        ret
    __endasm;
}

void radio_fifo_fixture_ready(void) __naked
{
    __asm
        nop
        ret
    __endasm;
}

void radio_fifo_fixture_fault(void) __naked
{
    __asm
        nop
        sjmp _radio_fifo_fixture_fault
    __endasm;
}
