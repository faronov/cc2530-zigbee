/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "banked_fixture.h"

/* Link BANK7_CONST at virtual7:e7f8, ending exactly before reserved NV. */
const __code uint8_t banked_fixture_const7[8] =
    {0xd3, 0x6e, 0xa1, 0x4c, 0x87, 0x29, 0xf0, 0x5b};

uint32_t banked_fixture_bank7(uint32_t value, uint16_t salt) __banked
{
    banked_fixture_status[43] |= 4;
    return banked_fixture_common(value ^ (((uint32_t)salt << 16) | salt));
}

uint32_t banked_fixture_pointer_leaf(uint32_t value) __banked
{
    return value ^ 0xc35aa53cUL;
}

/* Never called by foreground; no call, shared helper or static parameter
 * slot is shared with the interrupted foreground path.
 */
uint32_t banked_fixture_irq_leaf(uint32_t value) __banked
{
    return value + 0x10293847UL;
}

uint8_t banked_fixture_flash(void) __banked
{
    /* The common wrapper traps every error before returning into this bank,
     * including an idle-controller error which leaves XMAP asserted.
     */
    return banked_fixture_flash_common();
}
