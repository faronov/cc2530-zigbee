/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef FLASH_WRITE_H
#define FLASH_WRITE_H

#include "flash.h"

typedef enum {
    FLASH_WRITE_OK = 0, FLASH_WRITE_INVALID_ARGUMENT, FLASH_WRITE_INVALID_RANGE,
    FLASH_WRITE_BUFFER_OWNERSHIP, FLASH_WRITE_HISTORY_UNKNOWN, FLASH_WRITE_WORD_USED,
    FLASH_WRITE_EXECUTOR_FAILED, FLASH_WRITE_READER_FAILED, FLASH_WRITE_VERIFY_FAILED,
    FLASH_WRITE_NOT_ERASED, FLASH_WRITE_PENDING
} flash_write_result_t;

typedef enum {
    FLASH_WRITE_PREFLIGHT = 1, FLASH_WRITE_COMMAND, FLASH_WRITE_VERIFY, FLASH_WRITE_DONE
} flash_write_phase_t;

typedef struct {
    uint8_t result, operation, page, offset_low, offset_high, phase, executor, reader;
} flash_write_diagnostic_t;

/* Foreground/non-reentrant, exclusive controller/mapping/clock ownership,
 * awake undivided RC16/XOSC32, IRQs/DMA off, SDCC bank0/DPS0 and common CODE.
 * Link flash_exec, flash, flash_write before callers; word is four ordinary
 * XDATA bytes after the entire prefix through flash_write_reserved_end.
 *
 * page is 0/1 (physical125/126). Program offsets are four-byte aligned.
 * Reset starts with UNKNOWN history even when every flash byte is FF.
 * Only a real accepted erase plus complete 2-KiB FF verification establishes
 * an epoch. Each word is allowed ONE attempt in that epoch, including FF
 * data and failed/interrupted attempts. No history-import/reset API exists.
 * Ownership spans the entire epoch: no other writer, direct executor call,
 * debugger or DMA may program these pages between service calls.
 *
 * Success verifies the entire erased page or the four programmed bytes.
 * Invalid arguments, unknown history and used words do no MMIO or latching.
 * Other errors retain the first diagnostic and block all later operations.
 * A command that cannot quiesce retains the executor's RAM-only fail-stop;
 * the diagnostic stays PENDING/COMMAND and the attempt remains consumed.
 * poll_limit is the executor's finite FCTL-read budget, not elapsed time.
 * No rollback, implicit cleanup, durable record or lifetime-wear claim.
 * A separately authorized full reset loses history, not uncertainty.
 */
flash_write_result_t flash_nv_erase(uint8_t page, uint16_t poll_limit);
flash_write_result_t flash_nv_program(uint8_t page, uint16_t offset,
                                     const uint8_t MCU_XDATA *word, uint16_t poll_limit);
/* Last admitted operation; benign rejections do not replace it. No MMIO. */
const flash_write_diagnostic_t MCU_XDATA *flash_write_diagnostic(void);

#endif
