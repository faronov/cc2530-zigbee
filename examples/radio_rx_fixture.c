/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "radio_rx_fixture.h"

void radio_rx_fixture_before(void) __naked
{
    __asm
        nop
        ret
    __endasm;
}
void radio_rx_fixture_ready(void) __naked
{
    __asm
        nop
        ret
    __endasm;
}
void radio_rx_fixture_fault(void) __naked
{
    __asm
        nop
        sjmp _radio_rx_fixture_fault
    __endasm;
}
void radio_rx_fixture_end(void) __naked
{
    __asm
        nop
        sjmp _radio_rx_fixture_end
    __endasm;
}
