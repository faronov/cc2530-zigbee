/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "board.h"

const MCU_CODE board_description_t board_description = {BOARD_FLOWER_V2, 0u};

void board_early_off(void)
{
    /* Latches first, then direction: LED off, sensor rail off, excitation low. */
    MMIO_CLEAR(SOC_P0, BOARD_FLOWER_LED);
    MMIO_CLEAR(SOC_P0SEL, BOARD_FLOWER_LED);
    MMIO_SET(SOC_P0DIR, BOARD_FLOWER_LED);
    MMIO_CLEAR(SOC_P1, BOARD_FLOWER_RAIL | BOARD_FLOWER_PWM);
    MMIO_CLEAR(SOC_P1SEL, BOARD_FLOWER_RAIL | BOARD_FLOWER_PWM);
    MMIO_SET(SOC_P1DIR, BOARD_FLOWER_RAIL | BOARD_FLOWER_PWM);
    /* Unpowered sensor pins: no internal pull current into the dead rail. */
    MMIO_SET(SOC_P0INP, BOARD_FLOWER_SOIL | BOARD_FLOWER_SCL | BOARD_FLOWER_SDA | BOARD_FLOWER_LIGHT);
    MMIO_SET(SOC_P1INP, BOARD_FLOWER_ONEWIRE);
}
