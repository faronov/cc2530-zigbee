/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "flash_fixture.h"
void flash_fixture_wait(void) __naked
{
    __asm
        nop
        ret
    __endasm;
}
void flash_fixture_end(void) __naked
{
    __asm
        nop
        sjmp _flash_fixture_end
    __endasm;
}
void flash_fixture_fault(void) __naked
{
    __asm
        nop
        sjmp _flash_fixture_fault
    __endasm;
}
void main(void)
{
    flash_fixture_initialize();
    for (;;) {
        flash_fixture_wait();
        flash_fixture_poll();
        if (flash_fixture_state.phase == FF_END) flash_fixture_end();
        if (flash_fixture_state.phase == FF_FAULT) flash_fixture_fault();
    }
}
