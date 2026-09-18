/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef FLASH_EXEC_H
#define FLASH_EXEC_H

#include "flash.h"

#define FLASH_EXEC_RAM_SIZE 123u
#define FLASH_EXEC_ERASE 1u
#define FLASH_EXEC_PROGRAM 2u

typedef enum {
    FLASH_EXEC_IDLE = 0, FLASH_EXEC_INVALID_ARGUMENT, FLASH_EXEC_UNSUPPORTED_STATE,
    FLASH_EXEC_MAPPING_CHANGED, FLASH_EXEC_CODE_CHANGED, FLASH_EXEC_ABORT,
    FLASH_EXEC_CONTROLLER_STATE, FLASH_EXEC_RAM_STOP
} flash_exec_result_t;

/* Internal command engine, not a durable/public NV writer. IDLE means the
 * accepted command ended with idle controller status, not verified flash data.
 * The future caller must enforce erase/program history, write limits and
 * readback before reporting an NV operation successful.
 *
 * page is 0/1; offset is a byte offset, zero for erase or four-byte aligned
 * for program. word is a four-byte ordinary XDATA object after the entire
 * driver prefix through flash_exec_reserved_end (ignored for erase).
 * Foreground, SDCC bank0/DPS0 ABI, common CODE below8000, exclusive flash,
 * mapping/clock and IRQ/DMA ownership; no interrupt, sleep, DMA or other writer.
 *
 * poll_limit counts 1..65535 complete FCTL polls, not time. Code and word bytes
 * are staged before WRITE/ERASE; the critical path has no flash access.
 * Exhaustion with BUSY/WRITE/ERASE set records RAM_STOP and loops in RAM, with
 * XMAP retained. It cannot safely return to flash; external reset recovery is
 * required. Other runtime errors retain the first result with no retries,
 * cleanup or subsequent MMIO. Only successful idle return restores XMAP.
 * No hardware invocation is authorized by this internal interface.
 */
flash_exec_result_t flash_exec_command(uint8_t operation, uint8_t page,
                                     uint16_t offset, const uint8_t MCU_XDATA *word,
                                     uint16_t poll_limit);

#endif
