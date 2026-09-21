/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "radio_noise_fixture.h"

void radio_noise_fixture_wait(void) __naked
{
    __asm
        nop
        ret
    __endasm;
}
void radio_noise_fixture_end(void) __naked
{
    __asm
        nop
        sjmp _radio_noise_fixture_end
    __endasm;
}
void radio_noise_fixture_fault(void) __naked
{
    __asm
        nop
        sjmp _radio_noise_fixture_fault
    __endasm;
}
void main(void)
{
    radio_noise_fixture_initialize();
    for (;;) {
        radio_noise_fixture_wait();
        radio_noise_fixture_poll();
        if (radio_noise_fixture_state.phase == RNF_END) radio_noise_fixture_end();
        if (radio_noise_fixture_state.phase == RNF_FAULT) radio_noise_fixture_fault();
    }
}
