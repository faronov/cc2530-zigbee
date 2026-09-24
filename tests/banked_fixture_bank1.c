/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "banked_fixture.h"

const __code uint8_t banked_fixture_const1[4] = {0x13, 0x57, 0x9b, 0xdf};

uint16_t banked_fixture_word(uint16_t value, uint16_t salt) __banked
{
    return (value ^ salt) + 0x1021u;
}

uint32_t banked_fixture_bank1(uint32_t value, uint16_t salt) __banked
{
    banked_fixture_status[43] |= 1;
    return banked_fixture_bank2(value ^ 0x89abcdefUL, salt + 0x1357u) + 0x24681357UL;
}
