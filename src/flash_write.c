/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "flash_write.h"
#include "flash_exec.h"
#include <stddef.h>

MCU_XDATA flash_write_diagnostic_t flash_write_status;
MCU_XDATA uint8_t flash_write_known, flash_write_used[128];
MCU_XDATA uint8_t flash_write_word[4], flash_write_check[FLASH_READ_MAX];
extern MCU_XDATA uint8_t flash_write_reserved_end;

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
        address = MMIO_XADDRESS(word);
        if (address > 0x1dfcu) return FLASH_WRITE_INVALID_RANGE;
        if (address <= MMIO_XADDRESS(&flash_write_reserved_end)) return FLASH_WRITE_BUFFER_OWNERSHIP;
        if (!(flash_write_known & page_mask)) return FLASH_WRITE_HISTORY_UNKNOWN;
        if (flash_write_used[index] & mask) return FLASH_WRITE_WORD_USED;
        for (i = 0; i < 4; i++) flash_write_word[i] = word[i];
    }
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
    return FLASH_WRITE_OK;
failed:
    flash_write_status.result = result;
    return result;
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
