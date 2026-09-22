/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "radio_link_fixture.h"
void radio_link_fixture_wait(void) __naked
{
    __asm
        nop
        ret
    __endasm;
}
void radio_link_fixture_end(void) __naked
{
    __asm
        nop
        sjmp _radio_link_fixture_end
    __endasm;
}
void radio_link_fixture_fault(void) __naked
{
    __asm
        nop
        sjmp _radio_link_fixture_fault
    __endasm;
}
void main(void)
{
    radio_link_fixture_initialize();
    for (;;) {
        radio_link_fixture_wait();
        radio_link_fixture_poll();
        if (radio_link_fixture_state.phase == LNK_END) radio_link_fixture_end();
        if (radio_link_fixture_state.phase == LNK_FAULT) radio_link_fixture_fault();
    }
}
