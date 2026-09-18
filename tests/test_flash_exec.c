/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "flash_exec.h"

#if defined(__SDCC)
volatile MCU_XDATA MCU_AT(0x1e00) uint8_t flash_exec_test_result[8];
MCU_XDATA uint8_t flash_exec_test_word[4];
const uint8_t MCU_XDATA * MCU_XDATA flash_exec_test_pointer;
MCU_XDATA uint16_t flash_exec_test_offset, flash_exec_test_limit;
MCU_XDATA uint8_t flash_exec_test_operation, flash_exec_test_page, flash_exec_test_return;

void flash_exec_test_cycle(void)
{
    __asm
        .globl _flash_exec_before
    _flash_exec_before:
        nop
    __endasm;
    flash_exec_test_return = flash_exec_command(flash_exec_test_operation, flash_exec_test_page,
        flash_exec_test_offset, flash_exec_test_pointer, flash_exec_test_limit);
    __asm
        .globl _flash_exec_done
    _flash_exec_done:
        nop
    __endasm;
}

void main(void)
{
    uint8_t i;
    SOC_IEN0 = 0; SOC_IEN1 = 0; SOC_IEN2 = 0;
    flash_exec_test_result[0] = 'F'; flash_exec_test_result[1] = 'E';
    flash_exec_test_result[2] = 'X'; flash_exec_test_result[3] = 'C';
    flash_exec_test_result[4] = 1; flash_exec_test_result[5] = 8;
    flash_exec_test_result[6] = flash_exec_test_result[7] = 0;
    for (i = 0; i < 4; i++) flash_exec_test_word[i] = 0x69u + i * 17u;
    flash_exec_test_pointer = flash_exec_test_word;
    for (;;) flash_exec_test_cycle();
}
#else
#include "host_mmio.h"
#include <assert.h>
#include <setjmp.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

extern uint8_t flash_exec_work[9], flash_exec_reserved_end;
extern volatile uint8_t flash_exec_ram[FLASH_EXEC_RAM_SIZE];
static uint8_t word[4] = {0x12, 0x34, 0x56, 0x78}, staged[4], addresses[2];
static uint8_t controller, mode, accepting, active, saved_bank, chip, info0, info1, ignore_address, ignore_mapping;
static uint16_t buffer_address, ram_address;
static unsigned events, map_writes, command_writes, data_writes, polls, delay, calls, stopped;
static unsigned fault_phase, observations;
static jmp_buf terminal;

enum { NORMAL, ABORT, IGNORED, STUCK, ACTIVE_NO_BUSY, BAD_CACHE, BAD_FULL };

static void consume(void)
{
    assert(read_count <= 1 && xread_count <= 1 && !write_count && !xwrite_count);
    read_count = xread_count = 0;
}

static uint8_t load(uint8_t address, uint8_t value)
{
    consume(); events++;
    assert(address == 0xc7 || address == 0xc6 || address == 0xa8 || address == 0xb8 ||
           address == 0x9a || address == 0xd6 || address == 0xd7 || address == 0xbe || address == 0x9e);
    if (address == 0xa8 && ++observations == fault_phase) return 1;
    return value;
}

static uint8_t xload(uint16_t address)
{
    consume(); events++;
    if (address == 0x6270) {
        if (accepting) accepting = 0;
        else if (active) {
            polls++;
            if (mode == ACTIVE_NO_BUSY) controller &= 0x7f;
            else if (mode != STUCK && polls > delay) {
                active = 0;
                controller = (uint8_t)(flash_exec_work[0] & 12);
                if (mode == BAD_CACHE) controller ^= 4;
                if (mode == BAD_FULL) controller |= 0x40;
            }
        }
        return controller;
    }
    if (address == 0x624a) return chip;
    if (address == 0x6276) return info0;
    if (address == 0x6277) return info1;
    assert(address == 0x6271 || address == 0x6272);
    return addresses[address - 0x6271];
}

static void xstore(uint16_t address, uint8_t value)
{
    assert(xwrite_count == 1 && xwrites[0].address == address && xwrites[0].value == value);
    xwrite_count = 0; consume(); events++;
    if (address == 0x6271 || address == 0x6272) {
        if (!ignore_address) addresses[address - 0x6271] = value;
    } else if (address == 0x6270) {
        assert(!command_writes && !(controller & 0xf3) && (SOC_MEMCTR & 8));
        assert(value == flash_exec_work[0]);
        command_writes++;
        if (mode == IGNORED) return;
        if (mode == ABORT) { controller |= 0x20; return; }
        controller = value | 0x80; accepting = active = 1;
    } else {
        assert(address == 0x6273 && (controller & 0x83) == 0x82 && data_writes < 4);
        staged[data_writes++] = value;
    }
}

static void store(uint8_t address, uint8_t before, uint8_t value)
{
    assert(write_count == 1 && writes[0].address == address && writes[0].before == before &&
           writes[0].after == value);
    write_count = 0; consume(); events++; map_writes++;
    assert(address == 0xc7 && value == (map_writes == 1 ? (saved_bank | 8) : saved_bank));
    if (map_writes == ignore_mapping) SOC_MEMCTR = before;
}

static uint16_t address(const volatile void *object)
{
    if (object == &flash_exec_reserved_end) return 0x2ff;
    if (object == flash_exec_ram) return ram_address;
    assert(object == word);
    return buffer_address;
}

void host_flash_engine_stop(void)
{
    stopped++;
    longjmp(terminal, 1);
}

static void reset(void)
{
    unsigned i;
    host_mmio_reset();
    memset(flash_exec_work, 0, sizeof(flash_exec_work));
    for (i = 0; i < FLASH_EXEC_RAM_SIZE; i++) flash_exec_ram[i] = 0;
    memset(addresses, 0, sizeof(addresses));
    memset(staged, 0, sizeof(staged));
    SOC_CLKCONCMD = SOC_CLKCONSTA = 0xc9; SOC_SLEEPCMD = 4;
    SOC_MEMCTR = saved_bank = 2;
    chip = 0xa5; info0 = 0x44; info1 = 0xff; controller = 4;
    mode = accepting = active = ignore_address = ignore_mapping = 0;
    events = map_writes = command_writes = data_writes = polls = stopped = 0;
    fault_phase = observations = 0;
    delay = 2; buffer_address = 0x400; ram_address = 0x100;
    host_mmio_read_hook = load; host_mmio_write_hook = store;
    host_mmio_xread_hook = xload; host_mmio_xwrite_hook = xstore; host_mmio_xaddress_hook = address;
}

static flash_exec_result_t run(uint8_t operation, uint8_t page, uint16_t offset, uint16_t limit)
{
    flash_exec_result_t result;
    calls++;
    if (setjmp(terminal)) result = FLASH_EXEC_RAM_STOP;
    else result = flash_exec_command(operation, page, offset, word, limit);
    consume();
    assert(memcmp(word, "\x12\x34\x56\x78", 4) == 0);
    return result;
}

static void retained(flash_exec_result_t result)
{
    unsigned before = events;
    uint8_t state[9];
    memcpy(state, flash_exec_work, sizeof(state));
    assert(run(FLASH_EXEC_ERASE, 0, 0, 65535) == result && events == before);
    assert(memcmp(state, flash_exec_work, sizeof(state)) == 0);
}

int main(void)
{
    unsigned p, offset, value, bank, cache, clock, model;
    volatile uint8_t *registers[] = {&SOC_IEN0, &SOC_IEN1, &SOC_IEN2, &SOC_DMAARM,
        &SOC_DMAREQ, &SOC_SLEEPCMD, &SOC_CLKCONCMD, &SOC_CLKCONSTA};
    const uint16_t limits[] = {1, 255, 256, 257, 65535};
    for (p = 0; p < 2; p++) for (offset = 0; offset < 2048; offset += 4) {
        reset();
        assert(run(FLASH_EXEC_PROGRAM, (uint8_t)p, (uint16_t)offset, 3) == FLASH_EXEC_IDLE);
        assert(addresses[0] == (uint8_t)(offset >> 2) &&
               addresses[1] == (uint8_t)(0xfa + p*2 + (offset >> 10)));
        assert(command_writes == 1 && data_writes == 4 && polls == 3 && map_writes == 2);
        assert(SOC_MEMCTR == saved_bank && memcmp(staged, word, 4) == 0);
    }
    for (bank = 0; bank < 8; bank++) for (cache = 0; cache < 4; cache++) for (clock = 0; clock < 2; clock++) {
        reset(); SOC_MEMCTR = saved_bank = (uint8_t)bank; controller = (uint8_t)(cache << 2);
        SOC_CLKCONCMD = SOC_CLKCONSTA = clock ? 0x88 : 0xc9;
        assert(run(FLASH_EXEC_ERASE, 1, 0, 3) == FLASH_EXEC_IDLE);
        assert(addresses[0] == 0 && addresses[1] == 0xfc && !data_writes && map_writes == 2);
    }
    for (value = 0; value < 65536u; value++) {
        reset();
        if (value >= 2048 || (value & 3))
            assert(run(FLASH_EXEC_PROGRAM, 0, (uint16_t)value, 1) == FLASH_EXEC_INVALID_ARGUMENT && !events);
        reset(); buffer_address = (uint16_t)value;
        if (value <= 0x2ff || value > 0x1dfc)
            assert(run(FLASH_EXEC_PROGRAM, 0, 0, 1) == FLASH_EXEC_INVALID_ARGUMENT && !events);
    }
    for (value = 0; value < 256; value++) {
        reset();
        if (value != 1 && value != 2)
            assert(run((uint8_t)value, 0, 0, 1) == FLASH_EXEC_INVALID_ARGUMENT && !events);
        reset();
        if (value >= 2)
            assert(run(FLASH_EXEC_ERASE, (uint8_t)value, 0, 1) == FLASH_EXEC_INVALID_ARGUMENT && !events);
        reset(); chip = (uint8_t)value;
        if (value != 0xa5) { assert(run(1, 0, 0, 3) == FLASH_EXEC_UNSUPPORTED_STATE); retained(FLASH_EXEC_UNSUPPORTED_STATE); }
        reset(); info0 = (uint8_t)value;
        if (value != 0x44) { assert(run(1, 0, 0, 3) == FLASH_EXEC_UNSUPPORTED_STATE); retained(FLASH_EXEC_UNSUPPORTED_STATE); }
        reset(); info1 = (uint8_t)value;
        if ((value & 7) != 7) { assert(run(1, 0, 0, 3) == FLASH_EXEC_UNSUPPORTED_STATE); retained(FLASH_EXEC_UNSUPPORTED_STATE); }
        else assert(run(1, 0, 0, 3) == FLASH_EXEC_IDLE);
        reset(); SOC_MEMCTR = (uint8_t)value;
        if (value & 0xf8) { assert(run(1, 0, 0, 3) == FLASH_EXEC_UNSUPPORTED_STATE); retained(FLASH_EXEC_UNSUPPORTED_STATE); }
        reset(); controller = (uint8_t)value;
        if (value & 0xf3) { assert(run(1, 0, 0, 3) == FLASH_EXEC_UNSUPPORTED_STATE); retained(FLASH_EXEC_UNSUPPORTED_STATE); }
        for (model = 0; model < sizeof(registers)/sizeof(registers[0]); model++) {
            if ((model < 5 && !value) || (model == 5 && (value & 7) == 4) || (model >= 6 && value == 0xc9))
                continue;
            reset(); *registers[model] = (uint8_t)value;
            assert(run(1, 0, 0, 3) == FLASH_EXEC_UNSUPPORTED_STATE && !command_writes && !map_writes);
            retained(FLASH_EXEC_UNSUPPORTED_STATE);
        }
    }
    reset(); assert(run(1, 0, 0, 0) == FLASH_EXEC_INVALID_ARGUMENT && !events);
    reset(); assert(run(1, 0, 4, 1) == FLASH_EXEC_INVALID_ARGUMENT && !events);
    reset(); assert(flash_exec_command(2, 0, 0, NULL, 1) == FLASH_EXEC_INVALID_ARGUMENT && !events);
    for (model = ABORT; model <= BAD_FULL; model++) {
        flash_exec_result_t expected = model == ABORT ? FLASH_EXEC_ABORT :
            (model == STUCK || model == ACTIVE_NO_BUSY) ? FLASH_EXEC_RAM_STOP : FLASH_EXEC_CONTROLLER_STATE;
        reset(); mode = (uint8_t)model;
        assert(run(2, 0, 0, 3) == expected && map_writes == 1 && SOC_MEMCTR == (saved_bank | 8));
        assert(stopped == (expected == FLASH_EXEC_RAM_STOP));
        assert(data_writes == (model == ABORT || model == IGNORED ? 0 : 4));
        retained(expected);
    }
    for (value = 1; value <= 2; value++) {
        reset(); ignore_mapping = (uint8_t)value;
        assert(run(2, 0, 0, 3) == FLASH_EXEC_MAPPING_CHANGED);
        assert(command_writes == (value == 2) && map_writes == value);
        retained(FLASH_EXEC_MAPPING_CHANGED);
    }
    reset(); ignore_address = 1;
    assert(run(2, 0, 0, 3) == FLASH_EXEC_CONTROLLER_STATE && !command_writes && !map_writes);
    retained(FLASH_EXEC_CONTROLLER_STATE);
    reset(); ram_address = 0x1e00 - FLASH_EXEC_RAM_SIZE + 1;
    assert(run(2, 0, 0, 3) == FLASH_EXEC_CODE_CHANGED && !command_writes);
    retained(FLASH_EXEC_CODE_CHANGED);
    for (value = 0; value < sizeof(limits)/sizeof(limits[0]); value++) for (p = 1; p <= 2; p++) {
        reset(); mode = STUCK;
        assert(run((uint8_t)p, 0, 0, limits[value]) == FLASH_EXEC_RAM_STOP);
        assert(polls == limits[value] && map_writes == 1 && stopped == 1);
        retained(FLASH_EXEC_RAM_STOP);
    }
    for (value = 1; value <= 4; value++) {
        reset(); fault_phase = value;
        assert(run(2, 0, 0, 3) == FLASH_EXEC_UNSUPPORTED_STATE);
        assert(command_writes == (value == 4) && map_writes == (value >= 3));
        retained(FLASH_EXEC_UNSUPPORTED_STATE);
    }
    printf("Flash executor host: %u calls; reserved addresses, staging, mapping, controller outcomes "
           "and non-returning active-controller exhaustion PASS (C peripheral model only)\n", calls);
    return 0;
}
#endif
