/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "dma_fixture.h"

void dma_fixture_before(void) __naked
{
    __asm
        nop
        ret
    __endasm;
}

void dma_fixture_ready(void) __naked
{
    __asm
        nop
        ret
    __endasm;
}

void dma_fixture_fault(void) __naked
{
    __asm
        nop
        sjmp _dma_fixture_fault
    __endasm;
}
