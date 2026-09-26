/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "flash_write.h"
#if defined(CC2530_MAC_LINK_CHILD_WORKSPACE)
#include "mac_link_child_workspace_internal.h"
#else
#define CW_RETURN(f,r) (r)
#define CW_CALL(f,e) (e)
#endif
#include "flash_exec.h"
#include <stddef.h>
#if defined(CC2530_MAC_LINK_WORKSPACE)
#include "mac_link_workspace_guard_internal.h"
#endif

MCU_XDATA flash_write_diagnostic_t flash_write_status;
MCU_XDATA uint8_t flash_write_known, flash_write_used[128];
#if defined(CC2530_MAC_LINK_CHILD_WORKSPACE)
#define flash_write_word (child_work_arena.nv.writer.word)
#define flash_write_check (child_work_arena.nv.writer.check)
#else
MCU_XDATA uint8_t flash_write_word[4], flash_write_check[FLASH_READ_MAX];
#endif
extern MCU_XDATA uint8_t flash_write_reserved_end;

#if defined(CC2530_MAC_LINK_CHILD_WORKSPACE)
#define flash_nv_read(...) CW_CALL(CW_READ,flash_nv_read(__VA_ARGS__))
#define flash_exec_command(...) CW_CALL(CW_EXEC,flash_exec_command(__VA_ARGS__))
#endif
static flash_write_result_t operate(uint8_t operation, uint8_t page, uint16_t offset,
                                   const uint8_t MCU_XDATA *word, uint16_t poll_limit)
{
    uint16_t address, position;
    uint8_t page_mask, index, mask, i, length;
    flash_write_result_t result;
    if (flash_write_status.result) return (flash_write_result_t)flash_write_status.result;
    if (!poll_limit || (operation == FLASH_EXEC_PROGRAM && word == NULL))
        return FLASH_WRITE_INVALID_ARGUMENT;
    if (page >= FLASH_NV_PAGE_COUNT || offset >= FLASH_PAGE_SIZE || (offset & 3u))
        return FLASH_WRITE_INVALID_RANGE;
    page_mask = (uint8_t)(1u << page);
    index = (uint8_t)((page << 6) + (offset >> 5));
    mask = (uint8_t)(1u << ((offset >> 2) & 7u));
    if (operation == FLASH_EXEC_PROGRAM) {
#if defined(CC2530_MAC_LINK_CHILD_WORKSPACE)
        if (!LW_IO(CW_WRITE_WORD,word,4,0)) return FLASH_WRITE_BUFFER_OWNERSHIP;
#else
#if defined(CC2530_MAC_LINK_WORKSPACE)
        if (!link_work_external(word, 4)) return FLASH_WRITE_BUFFER_OWNERSHIP;
#endif
#endif
        address = MMIO_XADDRESS(word);
        if (address > 0x1dfcu) return FLASH_WRITE_INVALID_RANGE;
#if defined(CC2530_MAC_LINK_CHILD_WORKSPACE)
        if (!child_work_inside(word,4) && address<=MMIO_XADDRESS(&flash_write_reserved_end))
            return FLASH_WRITE_BUFFER_OWNERSHIP;
#else
        if (address <= MMIO_XADDRESS(&flash_write_reserved_end)) return FLASH_WRITE_BUFFER_OWNERSHIP;
#endif
        if (!(flash_write_known & page_mask)) return FLASH_WRITE_HISTORY_UNKNOWN;
        if (flash_write_used[index] & mask) return FLASH_WRITE_WORD_USED;
#if !defined(CC2530_MAC_LINK_CHILD_WORKSPACE)
        for (i = 0; i < 4; i++) flash_write_word[i] = word[i];
#endif
    }

#if defined(CC2530_MAC_LINK_CHILD_WORKSPACE)
    if (!child_work_enter(CW_WRITE)) return FLASH_WRITE_BUFFER_OWNERSHIP;
    if (operation==FLASH_EXEC_PROGRAM)
        for (i = 0; i < 4; i++) flash_write_word[i] = word[i];
#endif
flash_write_status.result = FLASH_WRITE_PENDING;
    flash_write_status.operation = operation;
    flash_write_status.page = page;
    flash_write_status.offset_low = (uint8_t)offset;
    flash_write_status.offset_high = (uint8_t)(offset >> 8);
    flash_write_status.phase = FLASH_WRITE_PREFLIGHT;
    flash_write_status.executor = flash_write_status.reader = 0xff;
    flash_write_status.reader = flash_nv_read(page, offset, flash_write_check,
                                             operation == FLASH_EXEC_PROGRAM ? 4 : 1);
    if (flash_write_status.reader != FLASH_OK) { result = FLASH_WRITE_READER_FAILED; goto failed; }
    if (operation == FLASH_EXEC_PROGRAM) {
        for (i = 0; i < 4; i++)
            if (flash_write_check[i] != 0xff) { result = FLASH_WRITE_NOT_ERASED; goto failed; }
        flash_write_used[index] |= mask;
    } else {
        flash_write_known &= (uint8_t)~page_mask;
    }
    flash_write_status.phase = FLASH_WRITE_COMMAND;
    flash_write_status.executor = flash_exec_command(operation, page, offset, flash_write_word, poll_limit);
    if (flash_write_status.executor != FLASH_EXEC_IDLE) { result = FLASH_WRITE_EXECUTOR_FAILED; goto failed; }
    flash_write_status.phase = FLASH_WRITE_VERIFY;
    position = offset;
    length = operation == FLASH_EXEC_PROGRAM ? 4 : FLASH_READ_MAX;
    do {
        flash_write_status.reader = flash_nv_read(page, position, flash_write_check, length);
        if (flash_write_status.reader != FLASH_OK) { result = FLASH_WRITE_READER_FAILED; goto failed; }
        for (i = 0; i < length; i++) {
            if (flash_write_check[i] != (operation == FLASH_EXEC_PROGRAM ? flash_write_word[i] : 0xff)) {
                result = FLASH_WRITE_VERIFY_FAILED; goto failed;
            }
        }
        position += length;
    } while (operation == FLASH_EXEC_ERASE && position < FLASH_PAGE_SIZE);
    if (operation == FLASH_EXEC_ERASE) {
        for (i = 0; i < 64; i++) flash_write_used[(page << 6) + i] = 0;
        flash_write_known |= page_mask;
    }
    flash_write_status.phase = FLASH_WRITE_DONE;
    flash_write_status.result = FLASH_WRITE_OK;
    return CW_RETURN(CW_WRITE,FLASH_WRITE_OK);
failed:
#if defined(CC2530_MAC_LINK_CHILD_WORKSPACE)
    (void)child_work_poison((uint8_t)result);
#endif
    flash_write_status.result = result;
    return CW_RETURN(CW_WRITE,result);
}

flash_write_result_t flash_nv_erase(uint8_t page, uint16_t poll_limit)
{
    return operate(FLASH_EXEC_ERASE, page, 0, NULL, poll_limit);
}

flash_write_result_t flash_nv_program(uint8_t page, uint16_t offset,
                                     const uint8_t MCU_XDATA *word, uint16_t poll_limit)
{
    return operate(FLASH_EXEC_PROGRAM, page, offset, word, poll_limit);
}

const flash_write_diagnostic_t MCU_XDATA *flash_write_diagnostic(void)
{
    return &flash_write_status;
}

MCU_XDATA uint8_t flash_write_reserved_end;
