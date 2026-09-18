/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef FLASH_H
#define FLASH_H

#include "cc2530_mmio.h"

#define FLASH_PAGE_SIZE 2048u
#define FLASH_NV_FIRST_PAGE 125u
#define FLASH_NV_PAGE_COUNT 2u
#define FLASH_NV_BASE 0x3e800ul
#define FLASH_NV_END 0x3f800ul
#define FLASH_READ_MAX 32u

typedef enum {
    FLASH_OK = 0, FLASH_INVALID_ARGUMENT, FLASH_INVALID_RANGE,
    FLASH_BUFFER_OWNERSHIP, FLASH_UNSUPPORTED_CHIP, FLASH_UNSUPPORTED_STATE,
    FLASH_CONTROLLER_STATE, FLASH_MAPPING_CHANGED
} flash_result_t;

/* Read-only first slice: no erase/program API, NV records or durability claim.
 * page is a partition-relative index (0/1), not a physical page number.
 * Read 1..32 bytes within one reserved page. Page127, lock/config bytes and
 * the separate information page are never exposed.
 *
 * Foreground/non-reentrant, unbanked CODE below8000, known awake stable
 * undivided RC16/XOSC32, IRQs/DMA off. Exclusive flash-controller, MEMCTR
 * and clock ownership is required; no other flash writer may run. A busy
 * flash can stall instruction fetch, so this reader cannot provide recovery
 * or a wall-clock bound against a concurrent writer.
 *
 * output is a writable XDATA object below1E00 and after flash_reserved_end;
 * link this driver before callers and prove the complete private prefix.
 * Success restores/verifies XBANK before publishing staged bytes. Publication
 * is not CPU-atomic; no concurrent observer is allowed. XMAP is never enabled.
 * Invalid arguments/ranges/ownership perform no MMIO and do not latch a fault.
 * Other errors retain the first fault and leave output unchanged; later calls
 * perform no MMIO. On fault there is no implicit mapping restoration, retry,
 * controller write or recovery. Only separately established full-reset
 * recovery starts a new epoch. Validate wider values before narrowing.
 */
flash_result_t flash_nv_read(uint8_t page, uint16_t offset,
                            uint8_t MCU_XDATA *output, uint8_t length);

#endif
