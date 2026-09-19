/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "radio_tx_fixture.h"
void radio_tx_fixture_wait(void) __naked
{
    __asm
        nop
        ret
    __endasm;
}
void radio_tx_fixture_end(void) __naked
{
    __asm
        nop
        sjmp _radio_tx_fixture_end
    __endasm;
}
void radio_tx_fixture_fault(void) __naked
{
    __asm
        nop
        sjmp _radio_tx_fixture_fault
    __endasm;
}
void main(void)
{
    radio_tx_fixture_initialize();
    for (;;) {
        radio_tx_fixture_wait();
        radio_tx_fixture_poll();
        if (radio_tx_fixture_state.phase == TXF_END) radio_tx_fixture_end();
        if (radio_tx_fixture_state.phase == TXF_FAULT) radio_tx_fixture_fault();
    }
}
