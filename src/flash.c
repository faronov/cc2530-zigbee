/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "flash.h"
#include <stddef.h>
#if defined(CC2530_MAC_LINK_WORKSPACE)
#include "mac_link_workspace_guard_internal.h"
#endif

#define FCTL 0x6270u
#define CHIPID 0x624au
#define CHIPINFO0 0x6276u
#define CHIPINFO1 0x6277u
#define NV_WINDOW 0xe800u

MCU_XDATA uint8_t flash_fault;
static MCU_XDATA uint8_t staging[FLASH_READ_MAX], saved_bank, saved_clock, saved_cache;
extern MCU_XDATA uint8_t flash_reserved_end;

static flash_result_t observe(uint8_t bank)
{
    uint8_t ien0, ien1, ien2, sleep, command, status, arm, request, control, mapping;
    ien0 = MMIO_READ(SOC_IEN0);
    ien1 = MMIO_READ(SOC_IEN1);
    ien2 = MMIO_READ(SOC_IEN2);
    sleep = MMIO_READ(SOC_SLEEPCMD);
    command = MMIO_READ(SOC_CLKCONCMD);
    status = MMIO_READ(SOC_CLKCONSTA);
    arm = MMIO_READ(SOC_DMAARM);
    request = MMIO_READ(SOC_DMAREQ);
    if (ien0 || ien1 || ien2 || arm || request || (sleep & 7u) != 4u ||
        command != saved_clock || command != status ||
        (command & 7u) != ((command & 0x40u) ? 1u : 0u) ||
        ((command & 0x40u) && !(command & 0x38u)))
        return FLASH_UNSUPPORTED_STATE;
    control = MMIO_XREAD(FCTL);
    if (control != saved_cache)
        return FLASH_CONTROLLER_STATE;
    mapping = MMIO_READ(SOC_MEMCTR);
    if (mapping != bank)
        return FLASH_MAPPING_CHANGED;
    return FLASH_OK;
}

flash_result_t flash_nv_read(uint8_t page, uint16_t offset,
                            uint8_t MCU_XDATA *output, uint8_t length)
{
    uint16_t address, source;
    uint8_t i, chip, info0, info1;
    flash_result_t result;
    if (flash_fault) return (flash_result_t)flash_fault;
    if (output == NULL || !length || length > FLASH_READ_MAX)
        return FLASH_INVALID_ARGUMENT;
    if (page >= FLASH_NV_PAGE_COUNT || offset >= FLASH_PAGE_SIZE ||
        length > FLASH_PAGE_SIZE - offset)
        return FLASH_INVALID_RANGE;
    address = MMIO_XADDRESS(output);
    if (address >= 0x1e00u || length > 0x1e00u - address)
        return FLASH_INVALID_RANGE;
    if (address <= MMIO_XADDRESS(&flash_reserved_end))
        return FLASH_BUFFER_OWNERSHIP;
#if defined(CC2530_MAC_LINK_WORKSPACE)
    if (!link_work_external(output, length)) return FLASH_BUFFER_OWNERSHIP;
#endif

    saved_bank = MMIO_READ(SOC_MEMCTR);
    saved_clock = MMIO_READ(SOC_CLKCONCMD);
    saved_cache = MMIO_XREAD(FCTL) & 0x0cu;
    if (saved_bank & 0xf8u) { result = FLASH_UNSUPPORTED_STATE; goto failed; }
    result = observe(saved_bank);
    if (result != FLASH_OK) goto failed;
    chip = MMIO_XREAD(CHIPID);
    info0 = MMIO_XREAD(CHIPINFO0);
    info1 = MMIO_XREAD(CHIPINFO1);
    if (chip != 0xa5u || info0 != 0x44u || (info1 & 7u) != 7u) {
        result = FLASH_UNSUPPORTED_CHIP; goto failed;
    }
    MMIO_WRITE(SOC_MEMCTR, 7);
    result = observe(7);
    if (result != FLASH_OK) goto failed;
    source = NV_WINDOW + (uint16_t)page * FLASH_PAGE_SIZE + offset;
    for (i = 0; i < length; i++) {
        staging[i] = MMIO_XREAD(source + i);
        result = observe(7);
        if (result != FLASH_OK) goto failed;
    }
    MMIO_WRITE(SOC_MEMCTR, saved_bank);
    result = observe(saved_bank);
    if (result != FLASH_OK) goto failed;
    for (i = 0; i < length; i++) output[i] = staging[i];
    return FLASH_OK;
failed:
    flash_fault = result;
    return result;
}

MCU_XDATA uint8_t flash_reserved_end;
