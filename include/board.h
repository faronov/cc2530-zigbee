/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef CC2530_BOARD_H
#define CC2530_BOARD_H

#include "cc2530_mmio.h"

#define BOARD_GENERIC 0u
#define BOARD_LG_ESL29_REV03 1u
#define BOARD_POLICY_DISPLAY_OFF 0x01u

#ifndef CC2530_BOARD
#error The build must select CC2530_BOARD
#endif
#if CC2530_BOARD != BOARD_GENERIC && CC2530_BOARD != BOARD_LG_ESL29_REV03
#error Unsupported CC2530 board
#endif

typedef struct {
    uint8_t identifier;
    uint8_t policy;
} board_description_t;

extern const MCU_CODE board_description_t board_description;

#if CC2530_BOARD == BOARD_LG_ESL29_REV03
/* Called before C data initialization; only direct MMIO operations are allowed. */
void board_early_off(void);
#endif

#endif
