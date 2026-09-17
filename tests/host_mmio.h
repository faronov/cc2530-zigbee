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
typedef void (*host_mmio_write_hook_t)(uint8_t address, uint8_t before, uint8_t value);
typedef uint8_t (*host_mmio_xread_hook_t)(uint16_t address);
typedef uint16_t (*host_mmio_xaddress_hook_t)(const volatile void *object);
typedef void (*host_mmio_cycles_hook_t)(uint8_t cycles);
typedef struct {
    uint16_t address;
    uint8_t value;
} xregister_read_t;

extern register_write_t writes[32];
extern unsigned write_count;
extern register_read_t reads[32];
extern unsigned read_count;
extern host_mmio_read_hook_t host_mmio_read_hook;
extern host_mmio_write_hook_t host_mmio_write_hook;
extern host_mmio_xread_hook_t host_mmio_xread_hook;
extern host_mmio_xaddress_hook_t host_mmio_xaddress_hook;
extern host_mmio_cycles_hook_t host_mmio_cycles_hook;
extern xregister_read_t xreads[32];
extern unsigned xread_count;
void host_mmio_reset(void);

#endif
