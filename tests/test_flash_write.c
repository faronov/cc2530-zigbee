/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "flash_write.h"
#include "flash_exec.h"

#if defined(__SDCC)
volatile MCU_XDATA MCU_AT(0x1e00) uint8_t flash_write_test_result[8];
MCU_XDATA uint8_t flash_write_test_word[4];
const uint8_t MCU_XDATA * MCU_XDATA flash_write_test_pointer;
MCU_XDATA uint16_t flash_write_test_offset, flash_write_test_limit;
MCU_XDATA uint8_t flash_write_test_operation, flash_write_test_page, flash_write_test_return;

void flash_write_test_cycle(void)
{
    __asm
        .globl _flash_write_before
    _flash_write_before:
        nop
    __endasm;
    if (flash_write_test_operation == FLASH_EXEC_ERASE)
        flash_write_test_return = flash_nv_erase(flash_write_test_page, flash_write_test_limit);
    else
        flash_write_test_return = flash_nv_program(flash_write_test_page, flash_write_test_offset,
                                                  flash_write_test_pointer, flash_write_test_limit);
    __asm
        .globl _flash_write_done
    _flash_write_done:
        nop
    __endasm;
}

void main(void)
{
    uint8_t i;
    SOC_IEN0 = 0; SOC_IEN1 = 0; SOC_IEN2 = 0;
    flash_write_test_result[0] = 'F'; flash_write_test_result[1] = 'W';
    flash_write_test_result[2] = 'R'; flash_write_test_result[3] = 'T';
    flash_write_test_result[4] = 1; flash_write_test_result[5] = 8;
    flash_write_test_result[6] = flash_write_test_result[7] = 0;
    for (i = 0; i < 4; i++) flash_write_test_word[i] = 0x12u + i * 34u;
    flash_write_test_pointer = flash_write_test_word;
    for (;;) flash_write_test_cycle();
}
#else
#include "host_mmio.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

extern uint8_t flash_exec_work[9], flash_exec_reserved_end, flash_fault, flash_reserved_end;
extern volatile uint8_t flash_exec_ram[FLASH_EXEC_RAM_SIZE];
extern flash_write_diagnostic_t flash_write_status;
extern uint8_t flash_write_known, flash_write_used[128], flash_write_word[4], flash_write_check[32];
extern uint8_t flash_write_reserved_end;
static uint8_t nv[4096], word[4], addresses[2], staged[4], controller, active, accepting, mode;
static unsigned events, commands, data_writes, polls, maps, reads_nv, calls, fail_read, fail_map, bad_byte;
static unsigned word_writes[1024], page_writes[2], erases[2];
static uint16_t pointer_address;
static jmp_buf terminal;

enum { NORMAL, IGNORED, ABORTED, STUCK, DROP_PROGRAM, PARTIAL_PROGRAM, ERASE_RESIDUE };

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
    return value;
}

static void complete(void)
{
    unsigned address = ((unsigned)addresses[0] | ((unsigned)addresses[1] << 8)) - 0xfa00;
    unsigned page = address >> 9, i;
    assert(address < 1024 && page < 2);
    if ((controller & 3) == FLASH_EXEC_ERASE) {
        assert(!(address & 511));
        memset(nv + page*2048, 255, 2048);
        if (mode == ERASE_RESIDUE) nv[page*2048+bad_byte] = 0;
        memset(word_writes+page*512, 0, sizeof(word_writes[0])*512);
        page_writes[page] = 0; erases[page]++;
    } else {
        assert(data_writes == 4 && word_writes[address] == 0);
        word_writes[address]++; page_writes[page]++;
        assert(page_writes[page] <= 512);
        if (mode != DROP_PROGRAM)
            for (i = 0; i < (mode == PARTIAL_PROGRAM ? 1u : 4u); i++) nv[address*4+i] &= staged[i];
    }
    controller &= 12; active = 0;
}

static uint8_t xload(uint16_t address)
{
    consume(); events++;
    if (address == 0x6270) {
        if (accepting) accepting = 0;
        else if (active && ++polls == 2 && mode != STUCK) complete();
        return controller;
    }
    if (address == 0x624a) return 0xa5;
    if (address == 0x6276) return 0x44;
    if (address == 0x6277) return 0xff;
    if (address == 0x6271 || address == 0x6272) return addresses[address-0x6271];
    assert(address >= 0xe800 && address < 0xf800 && SOC_MEMCTR == 7 && !(controller & 0xf3));
    reads_nv++;
    if (reads_nv == fail_read) controller ^= 4;
    return nv[address-0xe800];
}

static void xstore(uint16_t address, uint8_t value)
{
    assert(xwrite_count == 1 && xwrites[0].address == address && xwrites[0].value == value);
    xwrite_count = 0; consume(); events++;
    if (address == 0x6271 || address == 0x6272) addresses[address-0x6271] = value;
    else if (address == 0x6270) {
        assert(!(controller & 0xf3) && (SOC_MEMCTR & 8) && value == flash_exec_work[0]);
        assert(addresses[1] >= 0xfa && addresses[1] < 0xfe);
        assert(flash_write_status.result == FLASH_WRITE_PENDING && flash_write_status.phase == FLASH_WRITE_COMMAND);
        commands++; data_writes = polls = 0;
        if (mode == IGNORED) return;
        if (mode == ABORTED) { controller |= 0x20; return; }
        controller = value | 0x80; active = accepting = 1;
    } else {
        assert(address == 0x6273 && (controller & 0x83) == 0x82 && data_writes < 4);
        staged[data_writes++] = value;
    }
}

static void store(uint8_t address, uint8_t before, uint8_t value)
{
    assert(write_count == 1 && writes[0].address == address && writes[0].before == before &&
           writes[0].after == value);
    write_count = 0; consume(); events++; maps++;
    assert(address == 0xc7 && (value == 7 || value == 2 || value == 10));
    if (maps == fail_map) SOC_MEMCTR = before;
}

static uint16_t address(const volatile void *object)
{
    if (object == &flash_exec_reserved_end) return 0x9a;
    if (object == &flash_reserved_end) return 0xd0;
    if (object == &flash_write_reserved_end) return 0x300;
    if (object == flash_exec_ram) return 9;
    if (object == flash_write_word) return 0x220;
    if (object == flash_write_check) return 0x224;
    assert(object == word);
    return pointer_address;
}

void host_flash_engine_stop(void)
{
    assert(flash_exec_work[7] == FLASH_EXEC_RAM_STOP);
    longjmp(terminal, 1);
}

static void reset(void)
{
    host_mmio_reset();
    memset(flash_exec_work, 0, sizeof(flash_exec_work)); flash_fault = 0;
    memset(&flash_write_status, 0, sizeof(flash_write_status)); flash_write_known = 0;
    memset(flash_write_used, 0, sizeof(flash_write_used));
    SOC_CLKCONCMD = SOC_CLKCONSTA = 0xc9; SOC_SLEEPCMD = 4; SOC_MEMCTR = 2;
    controller = 4; active = accepting = mode = 0;
    events = commands = data_writes = polls = maps = reads_nv = fail_read = fail_map = bad_byte = 0;
    pointer_address = 0x400;
    memcpy(word, "\x12\x34\x56\x78", 4);
    host_mmio_read_hook = load; host_mmio_write_hook = store;
    host_mmio_xread_hook = xload; host_mmio_xwrite_hook = xstore; host_mmio_xaddress_hook = address;
}

static flash_write_result_t run(uint8_t operation, uint8_t page, uint16_t offset, uint16_t limit)
{
    uint8_t saved[4];
    flash_write_result_t result;
    memcpy(saved, word, 4); calls++;
    if (setjmp(terminal)) result = FLASH_WRITE_PENDING;
    else if (operation == 1) result = flash_nv_erase(page, limit);
    else result = flash_nv_program(page, offset, word, limit);
    consume();
    assert(memcmp(saved, word, 4) == 0 && flash_write_diagnostic() == &flash_write_status);
    return result;
}

static void retained(flash_write_result_t result)
{
    unsigned before = events;
    flash_write_diagnostic_t diagnostic = flash_write_status;
    uint8_t used[128], known = flash_write_known;
    memcpy(used, flash_write_used, 128);
    assert(run(1, 0, 0, 3) == result && run(2, 1, 2044, 3) == result && events == before);
    assert(memcmp(&diagnostic, &flash_write_status, sizeof(diagnostic)) == 0 &&
           known == flash_write_known && memcmp(used, flash_write_used, 128) == 0);
}

static void erase(uint8_t page)
{
    unsigned before = reads_nv;
    assert(run(1, page, 0, 3) == FLASH_WRITE_OK && SOC_MEMCTR == 2);
    assert(reads_nv-before == 2049 && (flash_write_known & (1u << page)));
    assert(flash_write_status.phase == FLASH_WRITE_DONE && flash_write_status.reader == FLASH_OK &&
           flash_write_status.executor == FLASH_EXEC_IDLE);
}

int main(void)
{
    unsigned page, offset, value, before;
    memset(nv, 0, sizeof(nv));
    reset();
    assert(run(2, 0, 0, 3) == FLASH_WRITE_HISTORY_UNKNOWN && !events);
    memset(nv, 255, sizeof(nv));
    assert(run(2, 1, 0, 3) == FLASH_WRITE_HISTORY_UNKNOWN && !events);
    erase(0); erase(1);
    for (page = 0; page < 2; page++) for (offset = 0; offset < 2048; offset += 4) {
        if (offset == 2044) memset(word, 255, 4);
        else memcpy(word, "\x12\x34\x56\x78", 4);
        before = reads_nv;
        assert(run(2, (uint8_t)page, (uint16_t)offset, 3) == FLASH_WRITE_OK && reads_nv-before == 8);
        assert(memcmp(nv+page*2048+offset, word, 4) == 0);
        before = events;
        assert(run(2, (uint8_t)page, (uint16_t)offset, 3) == FLASH_WRITE_WORD_USED && events == before);
    }
    assert(page_writes[0] == 512 && page_writes[1] == 512);
    erase(0);
    assert(run(2, 0, 2044, 3) == FLASH_WRITE_OK && run(2, 1, 2044, 3) == FLASH_WRITE_WORD_USED);
    reset();
    assert(run(2, 0, 0, 3) == FLASH_WRITE_HISTORY_UNKNOWN && !events);
    for (value = 0; value < 65536u; value++) {
        if (value >= 2048 || (value & 3))
            assert(run(2, 0, (uint16_t)value, 3) == FLASH_WRITE_INVALID_RANGE && !events);
        pointer_address = (uint16_t)value;
        if (value > 0x1dfc) assert(run(2, 0, 0, 3) == FLASH_WRITE_INVALID_RANGE && !events);
        else if (value <= 0x300) assert(run(2, 0, 0, 3) == FLASH_WRITE_BUFFER_OWNERSHIP && !events);
        else assert(run(2, 0, 0, 3) == FLASH_WRITE_HISTORY_UNKNOWN && !events);
        pointer_address = 0x400;
    }
    for (value = 2; value < 256; value++) {
        assert(run(1, (uint8_t)value, 0, 3) == FLASH_WRITE_INVALID_RANGE);
        assert(run(2, (uint8_t)value, 0, 3) == FLASH_WRITE_INVALID_RANGE && !events);
    }
    assert(run(1, 0, 0, 0) == FLASH_WRITE_INVALID_ARGUMENT && !events);
    assert(flash_nv_program(0, 0, NULL, 3) == FLASH_WRITE_INVALID_ARGUMENT && !events);
    for (page = 0; page < 2; page++) for (offset = 0; offset < 2048; offset++) {
        reset(); mode = ERASE_RESIDUE; bad_byte = offset;
        assert(run(1, (uint8_t)page, 0, 3) == FLASH_WRITE_VERIFY_FAILED && !flash_write_known);
        retained(FLASH_WRITE_VERIFY_FAILED);
    }
    for (value = IGNORED; value <= PARTIAL_PROGRAM; value++) {
        reset(); erase(0); mode = (uint8_t)value;
        assert(run(2, 0, 0, 3) == (value == STUCK ? FLASH_WRITE_PENDING :
            value >= DROP_PROGRAM ? FLASH_WRITE_VERIFY_FAILED : FLASH_WRITE_EXECUTOR_FAILED));
        assert((flash_write_used[0] & 1) && flash_write_status.phase != FLASH_WRITE_DONE);
        if (value == STUCK) assert(SOC_MEMCTR == 10 && flash_exec_work[7] == FLASH_EXEC_RAM_STOP);
        retained((flash_write_result_t)flash_write_status.result);
    }
    reset(); mode = IGNORED; memset(nv, 255, sizeof(nv));
    assert(run(1, 0, 0, 3) == FLASH_WRITE_EXECUTOR_FAILED && !flash_write_known);
    retained(FLASH_WRITE_EXECUTOR_FAILED);
    reset(); erase(0); nv[0] = 0; before = commands;
    assert(run(2, 0, 0, 3) == FLASH_WRITE_NOT_ERASED && commands == before && !flash_write_used[0]);
    retained(FLASH_WRITE_NOT_ERASED);
    for (value = 1; value <= 8; value++) {
        reset(); erase(0); fail_read = reads_nv+value;
        assert(run(2, 0, 0, 3) == FLASH_WRITE_READER_FAILED);
        assert(flash_write_status.reader == FLASH_CONTROLLER_STATE);
        assert((flash_write_used[0] & 1) == (value > 4));
        retained(FLASH_WRITE_READER_FAILED);
    }
    reset(); fail_map = 1;
    assert(run(1, 0, 0, 3) == FLASH_WRITE_READER_FAILED && !commands && !flash_write_known);
    retained(FLASH_WRITE_READER_FAILED);
    reset(); erase(0); mode = STUCK;
    assert(run(1, 0, 0, 1) == FLASH_WRITE_PENDING && !flash_write_known);
    retained(FLASH_WRITE_PENDING);
    reset(); erase(0);
    assert(run(2, 0, 0, 3) == FLASH_WRITE_OK && erases[0] && erases[1]);
    printf("Flash writer host: %u calls; real reader/executor integration, all page bytes/word slots, "
           "fresh-erase-only history, one attempt per word and retained faults PASS "
           "(synthetic flash/controller model, no hardware)\n", calls);
    return 0;
}
#endif
