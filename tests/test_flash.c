/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "flash.h"

#if defined(__SDCC)
volatile MCU_XDATA MCU_AT(0x1e00) uint8_t flash_test_result[8];
MCU_XDATA uint8_t flash_test_output[FLASH_READ_MAX];
uint8_t MCU_XDATA * MCU_XDATA flash_test_pointer;
MCU_XDATA uint16_t flash_test_offset;
MCU_XDATA uint8_t flash_test_page, flash_test_length, flash_test_return;

void flash_test_cycle(void)
{
    __asm
        .globl _flash_test_before
_flash_test_before:
        nop
    __endasm;
    flash_test_return = flash_nv_read(flash_test_page, flash_test_offset,
                                      flash_test_pointer, flash_test_length);
    __asm
        .globl _flash_test_done
_flash_test_done:
        nop
    __endasm;
}

void main(void)
{
    uint8_t i;
    SOC_IEN0 = 0; SOC_IEN1 = 0; SOC_IEN2 = 0;
    flash_test_result[0] = 'F'; flash_test_result[1] = 'L';
    flash_test_result[2] = 'S'; flash_test_result[3] = 'H';
    flash_test_result[4] = 1; flash_test_result[5] = 8;
    flash_test_result[6] = flash_test_result[7] = 0;
    for (i = 0; i < FLASH_READ_MAX; i++) flash_test_output[i] = 0xa5;
    flash_test_pointer = flash_test_output;
    for (;;) flash_test_cycle();
}
#else
#include "host_mmio.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

extern uint8_t flash_fault, flash_reserved_end;
static struct { uint8_t before, data[FLASH_READ_MAX], after; } caller;
static uint8_t controller, chip, info0, info1, original_bank, ignored_write;
static uint16_t pointer_address, next_source, last_xaddress;
static uint8_t last_sfr, last_value, last_xvalue;
static unsigned events, stores, data_reads, controls, fault_control, calls;

static uint8_t pattern(uint16_t address)
{
    return (uint8_t)((address >> 8) * 37u + (address & 255u) * 13u);
}

static void consume(void)
{
    if (read_count) {
        assert(read_count == 1 && reads[0].address == last_sfr && reads[0].value == last_value);
        read_count = 0;
    }
    if (xread_count) {
        assert(xread_count == 1 && xreads[0].address == last_xaddress && xreads[0].value == last_xvalue);
        xread_count = 0;
    }
    assert(!write_count && !xwrite_count);
}

static uint8_t load(uint8_t address, uint8_t value)
{
    consume(); events++;
    assert(address == 0xc7 || address == 0xa8 || address == 0xb8 || address == 0x9a ||
           address == 0xbe || address == 0xc6 || address == 0x9e ||
           address == 0xd6 || address == 0xd7);
    last_sfr = address; last_value = value;
    return value;
}

static uint8_t xload(uint16_t address)
{
    uint8_t value;
    consume(); events++;
    if (address == 0x6270u) {
        controls++;
        if (controls == fault_control) controller ^= 0x80u;
        value = controller;
    } else if (address == 0x624au) value = chip;
    else if (address == 0x6276u) value = info0;
    else if (address == 0x6277u) value = info1;
    else {
        assert(address >= 0xe800u && address < 0xf800u && address == next_source++);
        assert(SOC_MEMCTR == 7 && controller == (controller & 12u));
        data_reads++;
        value = pattern(address);
    }
    last_xaddress = address; last_xvalue = value;
    return value;
}

static void store(uint8_t address, uint8_t before, uint8_t value)
{
    assert(write_count == 1 && writes[0].address == address &&
           writes[0].before == before && writes[0].after == value);
    write_count = 0; consume(); events++; stores++;
    assert(address == 0xc7 && stores <= 2 && value == (stores == 1 ? 7 : original_bank));
    if (stores == ignored_write) SOC_MEMCTR = before;
}

static uint16_t address(const volatile void *object)
{
    if (object == &flash_reserved_end) return 0x100;
    assert(object == caller.data);
    return pointer_address;
}

static void reset(void)
{
    host_mmio_reset();
    flash_fault = 0;
    controller = 4; chip = 0xa5; info0 = 0x44; info1 = 7;
    SOC_SLEEPCMD = 4; SOC_CLKCONCMD = SOC_CLKCONSTA = 0xc9;
    SOC_MEMCTR = original_bank = 2;
    pointer_address = 0x200;
    ignored_write = 0;
    events = stores = data_reads = controls = fault_control = 0;
    memset(caller.data, 0xa5, sizeof(caller.data));
    caller.before = 0x69; caller.after = 0x96;
    host_mmio_read_hook = load; host_mmio_xread_hook = xload;
    host_mmio_write_hook = store; host_mmio_xaddress_hook = address;
}

static flash_result_t read_page(uint8_t page, uint16_t offset, uint8_t length)
{
    uint8_t previous[FLASH_READ_MAX];
    flash_result_t result;
    unsigned i;
    memcpy(previous, caller.data, sizeof(previous));
    next_source = (uint16_t)(0xe800u + (uint16_t)page * 2048u + offset);
    result = flash_nv_read(page, offset, caller.data, length); calls++; consume();
    assert(caller.before == 0x69 && caller.after == 0x96);
    if (result != FLASH_OK) assert(memcmp(caller.data, previous, sizeof(previous)) == 0);
    else {
        assert(!flash_fault && SOC_MEMCTR == original_bank && stores == 2 && data_reads == length);
        for (i = 0; i < length; i++)
            assert(caller.data[i] == pattern((uint16_t)(next_source - length + i)));
        assert(memcmp(caller.data + length, previous + length, sizeof(previous) - length) == 0);
    }
    return result;
}

static void retained(flash_result_t result)
{
    unsigned old_events = events, old_stores = stores;
    uint8_t mapping = SOC_MEMCTR;
    assert(flash_fault == result);
    assert(read_page(1, 2016, 32) == result && events == old_events && stores == old_stores);
    assert(flash_nv_read(255, 65535, NULL, 0) == result && events == old_events);
    assert(SOC_MEMCTR == mapping);
}

int main(void)
{
    unsigned page, offset, value, bank, cache, clock, count;
    uint8_t length;
    assert(FLASH_NV_BASE == (unsigned long)FLASH_NV_FIRST_PAGE * FLASH_PAGE_SIZE);
    assert(FLASH_NV_END == FLASH_NV_BASE + (unsigned long)FLASH_NV_PAGE_COUNT * FLASH_PAGE_SIZE);
    assert(FLASH_NV_END == 127ul * FLASH_PAGE_SIZE && FLASH_NV_BASE > 0x8000ul);
    for (page = 0; page < 2; page++) {
        for (offset = 0; offset < 2048; offset++) {
            reset();
            length = (uint8_t)(2048u - offset < 32u ? 2048u - offset : 32u);
            assert(read_page((uint8_t)page, (uint16_t)offset, length) == FLASH_OK);
        }
    }
    for (bank = 0; bank < 8; bank++) for (cache = 0; cache < 4; cache++)
        for (clock = 0; clock < 2; clock++) for (value = 0; value < 32; value++) {
            reset(); SOC_MEMCTR = original_bank = (uint8_t)bank; controller = (uint8_t)(cache << 2);
            SOC_CLKCONCMD = SOC_CLKCONSTA = clock ? 0x88 : 0xc9;
            info1 = (uint8_t)((value << 3) | 7);
            assert(read_page(1, 2047, 1) == FLASH_OK);
        }
    for (value = 0; value < 256; value++) {
        reset();
        if (!value || value > 32) {
            assert(read_page(0, 0, (uint8_t)value) == FLASH_INVALID_ARGUMENT && !events && !flash_fault);
        } else assert(read_page(0, 0, (uint8_t)value) == FLASH_OK);
        reset();
        if (value >= 2) assert(read_page((uint8_t)value, 0, 1) == FLASH_INVALID_RANGE && !events && !flash_fault);
        reset(); chip = (uint8_t)value;
        if (value != 0xa5) { assert(read_page(0, 0, 1) == FLASH_UNSUPPORTED_CHIP && !stores); retained(FLASH_UNSUPPORTED_CHIP); }
        reset(); info0 = (uint8_t)value;
        if (value != 0x44) { assert(read_page(0, 0, 1) == FLASH_UNSUPPORTED_CHIP && !stores); retained(FLASH_UNSUPPORTED_CHIP); }
        reset(); info1 = (uint8_t)value;
        if ((value & 7) != 7) { assert(read_page(0, 0, 1) == FLASH_UNSUPPORTED_CHIP && !stores); retained(FLASH_UNSUPPORTED_CHIP); }
        reset(); controller = (uint8_t)value;
        if (value & 0xf3) { assert(read_page(0, 0, 1) == FLASH_CONTROLLER_STATE && !stores); retained(FLASH_CONTROLLER_STATE); }
        reset(); SOC_MEMCTR = (uint8_t)value;
        if (value & 0xf8) { assert(read_page(0, 0, 1) == FLASH_UNSUPPORTED_STATE && !stores); retained(FLASH_UNSUPPORTED_STATE); }
    }
    reset();
    assert(flash_nv_read(0, 0, NULL, 1) == FLASH_INVALID_ARGUMENT && !events && !flash_fault);
    for (offset = 0; offset < 65536u; offset++) {
        reset();
        if (offset > 2016) assert(read_page(0, (uint16_t)offset, 32) == FLASH_INVALID_RANGE && !events && !flash_fault);
        reset(); pointer_address = (uint16_t)offset;
        if (offset <= 0x100)
            assert(read_page(0, 0, 32) == FLASH_BUFFER_OWNERSHIP && !events && !flash_fault);
        else if (offset > 0x1de0)
            assert(read_page(0, 0, 32) == FLASH_INVALID_RANGE && !events && !flash_fault);
    }
    reset(); pointer_address = 0x101;
    assert(read_page(0, 0, 32) == FLASH_OK);
    reset(); pointer_address = 0x1de0;
    assert(read_page(1, 2016, 32) == FLASH_OK);
    for (value = 0; value < 8; value++) {
        reset();
        switch (value) {
        case 0: SOC_IEN0 = 0x80; break;
        case 1: SOC_IEN1 = 1; break;
        case 2: SOC_IEN2 = 1; break;
        case 3: SOC_DMAARM = 1; break;
        case 4: SOC_DMAREQ = 1; break;
        case 5: SOC_SLEEPCMD = 5; break;
        case 6: SOC_CLKCONSTA = 0x88; break;
        default: SOC_CLKCONCMD = SOC_CLKCONSTA = 0x8a; break;
        }
        assert(read_page(0, 0, 1) == FLASH_UNSUPPORTED_STATE && !stores);
        retained(FLASH_UNSUPPORTED_STATE);
    }
    reset(); assert(read_page(0, 0, 32) == FLASH_OK); count = controls;
    for (value = 1; value <= count; value++) {
        reset(); fault_control = value;
        assert(read_page(0, 0, 32) == FLASH_CONTROLLER_STATE);
        retained(FLASH_CONTROLLER_STATE);
    }
    for (value = 1; value <= 2; value++) {
        reset(); ignored_write = (uint8_t)value;
        assert(read_page(0, 0, 32) == FLASH_MAPPING_CHANGED && stores == value);
        assert(data_reads == (value == 1 ? 0 : 32));
        retained(FLASH_MAPPING_CHANGED);
    }
    printf("Flash read host: %u calls; both reserved pages, all offsets/identity bytes, "
           "mapping/cache/clock variants, complete caller bounds, staged failure and retained faults PASS\n", calls);
    return 0;
}
#endif
