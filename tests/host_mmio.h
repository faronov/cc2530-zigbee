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

typedef struct {
    uint8_t address;
    uint8_t value;
} register_read_t;

typedef uint8_t (*host_mmio_read_hook_t)(uint8_t address, uint8_t value);

extern register_write_t writes[32];
extern unsigned write_count;
extern register_read_t reads[32];
extern unsigned read_count;
extern host_mmio_read_hook_t host_mmio_read_hook;
void host_mmio_reset(void);

#endif
