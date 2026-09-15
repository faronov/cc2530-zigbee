/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "bringup.h"

void main(void)
{
    bringup_initialize();
    for (;;) {
        bringup_tick();
    }
}
