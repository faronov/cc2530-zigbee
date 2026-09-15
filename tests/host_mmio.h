/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef HOST_MMIO_H
#define HOST_MMIO_H

#include "cc2530_mmio.h"

typedef struct {
    uint8_t address;
    uint8_t before;
    uint8_t after;
} register_write_t;

extern register_write_t writes[32];
extern unsigned write_count;
void host_mmio_reset(void);

#endif
