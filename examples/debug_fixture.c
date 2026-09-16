/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "debug_fixture.h"

/* Target-only probe: scratch registers under SDCC's foreground calling ABI.
 * The exported stop label names exactly one NOP, without a C prologue.
 */
void debug_fixture_probe(void) __naked
{
    __asm
        mov a,#0xa5
        mov b,#0x3c
        mov dptr,#0x1234
        mov r7,#0x69
        setb c
_debug_fixture_stop::
        nop
        ret
    __endasm;
}

void main(void)
{
    debug_fixture_initialize();
    for (;;) {
        debug_fixture_cycle();
        debug_fixture_probe();
    }
}
