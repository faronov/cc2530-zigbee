/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "banked_fixture.h"

const __code uint8_t banked_fixture_const2[4] = {0x24, 0x68, 0xac, 0xe0};

uint32_t banked_fixture_bank2(uint32_t value, uint16_t salt) __banked
{
    banked_fixture_status[43] |= 2;
    return banked_fixture_bank7(value + 0x10203040UL, salt ^ 0x369cu) ^ 0xa55a0ff0UL;
}

uint32_t banked_fixture_irq_foreground(uint32_t value) __banked
{
    /* Parent injects a real modeled interrupt here, not a call to the ISR.
     * It can also inject at arbitrary call/return instructions in replay.
     */
    __asm
        .globl _banked_fixture_irq_window
    _banked_fixture_irq_window:
        nop
    __endasm;
    return value ^ 0x76543210UL;
}
