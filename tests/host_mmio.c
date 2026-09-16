/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "host_mmio.h"

#include <assert.h>
#include <stddef.h>

#define DEFINE_REGISTER(name, address) volatile uint8_t name;
CC2530_REGISTER_LIST(DEFINE_REGISTER)
#undef DEFINE_REGISTER

register_write_t writes[32];
unsigned write_count;
register_read_t reads[32];
unsigned read_count;
host_mmio_read_hook_t host_mmio_read_hook;

void host_mmio_reset(void)
{
#define CLEAR_REGISTER(name, address) name = 0;
    CC2530_REGISTER_LIST(CLEAR_REGISTER)
#undef CLEAR_REGISTER
    write_count = 0;
    read_count = 0;
    host_mmio_read_hook = NULL;
}

uint8_t host_mmio_load(const volatile uint8_t *reg, uint8_t address)
{
    uint8_t value = *reg;
    assert(read_count < sizeof(reads) / sizeof(reads[0]));
    if (host_mmio_read_hook != NULL)
        value = host_mmio_read_hook(address, value);
    reads[read_count].address = address;
    reads[read_count].value = value;
    read_count++;
    return value;
}

void host_mmio_store(volatile uint8_t *reg, uint8_t address, uint8_t value)
{
    uint8_t newly_enabled = (uint8_t)(value & (uint8_t)~*reg);
    assert(write_count < sizeof(writes) / sizeof(writes[0]));
    if (address == SOC_P0DIR_ADDRESS) {
        assert(!(SOC_P0 & newly_enabled));
        assert(!(SOC_P0SEL & newly_enabled));
    }
    if (address == SOC_P1DIR_ADDRESS) {
        assert(!(SOC_P1 & newly_enabled));
        assert(!(SOC_P1SEL & newly_enabled));
    }
    if (address != SOC_IEN0_ADDRESS && address != SOC_IEN1_ADDRESS && address != SOC_IEN2_ADDRESS)
        assert(!SOC_IEN0 && !SOC_IEN1 && !SOC_IEN2);
    writes[write_count].address = address;
    writes[write_count].before = *reg;
    writes[write_count].after = value;
    write_count++;
    *reg = value;
}
