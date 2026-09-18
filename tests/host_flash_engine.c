/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "flash_exec.h"
#include <assert.h>

#if defined(__SDCC)
#error Native test model must never enter target firmware.
#endif

extern uint8_t flash_exec_work[9];
extern volatile uint8_t flash_exec_ram[FLASH_EXEC_RAM_SIZE];
extern void host_flash_engine_stop(void);

/* Copy/guard stand-in only; linked tests execute genuine copied opcodes. */
const uint8_t flash_exec_host_template[FLASH_EXEC_RAM_SIZE] = {0x69, 0x96, 0xa5, 0x5a};

void flash_exec_host_enter(void)
{
    uint16_t limit = (uint16_t)flash_exec_work[5] | ((uint16_t)flash_exec_work[6] << 8);
    uint8_t control, accepted, i;
    assert((SOC_MEMCTR & 8) && flash_exec_work[7] == 255);
    for (i = 0; i < FLASH_EXEC_RAM_SIZE; i++) assert(flash_exec_ram[i] == flash_exec_host_template[i]);
    MMIO_XWRITE(0x6270, flash_exec_work[0]);
    accepted = MMIO_XREAD(0x6270) == (flash_exec_work[0] | 0x80);
    if (accepted && (flash_exec_work[0] & 2))
        for (i = 0; i < 4; i++) MMIO_XWRITE(0x6273, flash_exec_work[1+i]);
    do {
        control = MMIO_XREAD(0x6270);
        if (!(control & 0x83)) {
            flash_exec_work[7] = (control & 0x20) ? FLASH_EXEC_ABORT :
                (!accepted || control != (flash_exec_work[0] & 12)) ? FLASH_EXEC_CONTROLLER_STATE : FLASH_EXEC_IDLE;
            flash_exec_work[8] = control;
            return;
        }
    } while (--limit);
    flash_exec_work[7] = FLASH_EXEC_RAM_STOP;
    flash_exec_work[8] = control;
    host_flash_engine_stop();
    assert(0 && "RAM fail-stop returned");
}
