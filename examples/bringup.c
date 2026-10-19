/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "bringup.h"

void main(void)
{
#if defined(CC2530_CLANG)
    MMIO_WRITE(SOC_IEN0, 0);
    MMIO_WRITE(SOC_IEN1, 0);
    MMIO_WRITE(SOC_IEN2, 0);
#if CC2530_BOARD == BOARD_LG_ESL29_REV03
    board_early_off();
#endif
#endif
    bringup_initialize();
    for (;;) {
        bringup_tick();
    }
}
