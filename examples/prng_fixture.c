/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "prng_fixture.h"
void prng_fixture_before(void) __naked
{
    __asm
        nop
        ret
    __endasm;
}
void prng_fixture_ready(void) __naked
{
    __asm
        nop
        ret
    __endasm;
}
void prng_fixture_fault(void) __naked
{
    __asm
        nop
        sjmp _prng_fixture_fault
    __endasm;
}
