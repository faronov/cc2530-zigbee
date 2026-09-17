/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "host_mmio.h"

#include <assert.h>
#include <stddef.h>

#if defined(IRQ_FIXTURE_HOST_TEST)
#include "irq_fixture.h"
#endif
#if defined(RADIO_FIFO_FIXTURE_HOST_TEST)
#include "radio_fifo_fixture.h"
#endif
#if defined(DMA_FIXTURE_HOST_TEST)
#include "dma_fixture.h"
#endif

#define DEFINE_REGISTER(name, address) volatile uint8_t name;
CC2530_REGISTER_LIST(DEFINE_REGISTER)
#if defined(RADIO_FIFO_FIXTURE_HOST_TEST)
RFF_REGISTERS(DEFINE_REGISTER)
#endif
#if defined(DMA_FIXTURE_HOST_TEST)
DMF_REGISTERS(DEFINE_REGISTER)
#endif
#if defined(IRQ_FIXTURE_HOST_TEST)
IRQ_FIXTURE_REGISTERS(DEFINE_REGISTER)
volatile uint8_t IRQ_T1CCTL3, IRQ_T1CCTL4;
#endif
#undef DEFINE_REGISTER

register_write_t writes[32];
unsigned write_count;
register_read_t reads[32];
unsigned read_count;
host_mmio_read_hook_t host_mmio_read_hook;
host_mmio_write_hook_t host_mmio_write_hook;
host_mmio_xread_hook_t host_mmio_xread_hook;
host_mmio_xaddress_hook_t host_mmio_xaddress_hook;
host_mmio_cycles_hook_t host_mmio_cycles_hook;
xregister_read_t xreads[32];
unsigned xread_count;

void host_mmio_reset(void)
{
#define CLEAR_REGISTER(name, address) name = 0;
    CC2530_REGISTER_LIST(CLEAR_REGISTER)
#undef CLEAR_REGISTER
    write_count = 0;
    read_count = 0;
    host_mmio_read_hook = NULL;
    host_mmio_write_hook = NULL;
    host_mmio_xread_hook = NULL;
    host_mmio_xaddress_hook = NULL;
    host_mmio_cycles_hook = NULL;
    xread_count = 0;
}

uint8_t host_mmio_xload(uint16_t address)
{
    uint8_t value;
    assert(xread_count < sizeof(xreads) / sizeof(xreads[0]));
    assert(host_mmio_xread_hook != NULL);
    value = host_mmio_xread_hook(address);
    xreads[xread_count].address = address;
    xreads[xread_count].value = value;
    xread_count++;
    return value;
}

uint16_t host_mmio_xaddress(const volatile void *object)
{
    assert(host_mmio_xaddress_hook != NULL);
    return host_mmio_xaddress_hook(object);
}

void host_mmio_system_cycles(uint8_t cycles)
{
    assert(host_mmio_cycles_hook != NULL);
    host_mmio_cycles_hook(cycles);
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
    if (address != SOC_IEN0_ADDRESS && address != SOC_IEN1_ADDRESS && address != SOC_IEN2_ADDRESS) {
#if defined(IRQ_FIXTURE_HOST_TEST)
        if (address == IRQ_T1CTL_ADDRESS || address == IRQ_T1CNTL_ADDRESS || address == IRQ_T1STAT_ADDRESS) {
            assert(!(SOC_IEN0 & 0x7f) && !(SOC_IEN1 & 0xfd) && !SOC_IEN2);
            if (address == IRQ_T1STAT_ADDRESS)
                assert(value == 0x1f && !SOC_IEN1 && !IRQ_T1CTL);
        } else
#endif
        assert(!SOC_IEN0 && !SOC_IEN1 && !SOC_IEN2);
    }
    writes[write_count].address = address;
    writes[write_count].before = *reg;
    writes[write_count].after = value;
    write_count++;
    *reg = value;
    if (host_mmio_write_hook != NULL)
        host_mmio_write_hook(address, writes[write_count - 1].before, value);
}
