/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "flash_exec.h"
#include <stddef.h>
#if defined(CC2530_MAC_LINK_WORKSPACE)
#include "mac_link_workspace_guard_internal.h"
#endif

#define FCTL 0x6270u

/* command, four word bytes, little-endian poll limit, result, last FCTL. */
MCU_XDATA uint8_t flash_exec_work[9];
volatile MCU_XDATA uint8_t flash_exec_ram[FLASH_EXEC_RAM_SIZE];
static MCU_XDATA uint8_t mapping, clock_command, cache_mode;
extern MCU_XDATA uint8_t flash_exec_reserved_end;

#if defined(__SDCC)
extern const MCU_CODE uint8_t flash_exec_template_end;

static void flash_exec_template(void) __naked
{
    __asm
        mov dpl,r2
        mov dph,r3
        movx a,@dptr
        mov b,a
        inc dptr
        movx a,@dptr
        mov r4,a
        inc dptr
        movx a,@dptr
        mov r5,a
        inc dptr
        movx a,@dptr
        mov r6,a
        inc dptr
        movx a,@dptr
        mov r7,a
        inc dptr
        movx a,@dptr
        mov r0,a
        inc dptr
        movx a,@dptr
        mov r1,a
        inc dptr
        mov r2,dpl
        mov r3,dph
        mov dptr,#0x6270
        mov a,b
        movx @dptr,a
        movx a,@dptr
        xrl a,b
        cjne a,#0x80,00001$
        mov a,b
        jnb acc.1,00002$
        mov dptr,#0x6273
        mov a,r4
        movx @dptr,a
        mov a,r5
        movx @dptr,a
        mov a,r6
        movx @dptr,a
        mov a,r7
        movx @dptr,a
        sjmp 00002$
    00001$:
        setb b.7
    00002$:
        mov dptr,#0x6270
    00003$:
        movx a,@dptr
        mov r4,a
        anl a,#0x83
        jz 00007$
        mov a,r0
        jnz 00004$
        dec r1
    00004$:
        dec r0
        mov a,r0
        orl a,r1
        jnz 00003$
        mov dpl,r2
        mov dph,r3
        mov a,#0x07
        movx @dptr,a
        inc dptr
        mov a,r4
        movx @dptr,a
    00005$:
        sjmp 00005$
    00007$:
        mov a,r4
        jb acc.5,00008$
        mov a,b
        jb acc.7,00009$
        anl a,#0x0c
        xrl a,r4
        jnz 00009$
        clr a
        sjmp 00010$
    00008$:
        mov a,#0x05
        sjmp 00010$
    00009$:
        mov a,#0x06
    00010$:
        mov dpl,r2
        mov dph,r3
        movx @dptr,a
        inc dptr
        mov a,r4
        movx @dptr,a
        ret
        .globl _flash_exec_template_end
    _flash_exec_template_end:
    __endasm;
}

static void enter_ram(void) __naked
{
    __asm
        mov r2,#_flash_exec_work
        mov r3,#(_flash_exec_work >> 8)
        mov dptr,#(_flash_exec_ram + 0x8000)
        clr a
        jmp @a+dptr
    __endasm;
}

#define TEMPLATE ((const uint8_t MCU_CODE *)flash_exec_template)
#define TEMPLATE_SIZE ((uint16_t)&flash_exec_template_end - (uint16_t)flash_exec_template)
#else
extern const uint8_t flash_exec_host_template[FLASH_EXEC_RAM_SIZE];
extern void flash_exec_host_enter(void);
#define TEMPLATE flash_exec_host_template
#define TEMPLATE_SIZE FLASH_EXEC_RAM_SIZE
#define enter_ram() flash_exec_host_enter()
#endif

static uint8_t idle(uint8_t expected_mapping)
{
    uint8_t ien0, ien1, ien2, arm, request, sleep, command, status, control, bank;
    ien0 = MMIO_READ(SOC_IEN0); ien1 = MMIO_READ(SOC_IEN1); ien2 = MMIO_READ(SOC_IEN2);
    arm = MMIO_READ(SOC_DMAARM); request = MMIO_READ(SOC_DMAREQ);
    sleep = MMIO_READ(SOC_SLEEPCMD);
    command = MMIO_READ(SOC_CLKCONCMD); status = MMIO_READ(SOC_CLKCONSTA);
    control = MMIO_XREAD(FCTL); bank = MMIO_READ(SOC_MEMCTR);
    return !ien0 && !ien1 && !ien2 && !arm && !request && (sleep & 7u) == 4u &&
        command == clock_command && command == status &&
        (command & 7u) == ((command & 0x40u) ? 1u : 0u) &&
        (!(command & 0x40u) || (command & 0x38u)) &&
        control == cache_mode && bank == expected_mapping;
}

flash_exec_result_t flash_exec_command(uint8_t operation, uint8_t page,
                                     uint16_t offset, const uint8_t MCU_XDATA *word,
                                     uint16_t poll_limit)
{
    uint16_t address, code_size, target;
    uint8_t i, chip, info0, info1;
    flash_exec_result_t result;
    if (flash_exec_work[7]) return (flash_exec_result_t)flash_exec_work[7];
    if ((operation != FLASH_EXEC_ERASE && operation != FLASH_EXEC_PROGRAM) ||
        page >= FLASH_NV_PAGE_COUNT || !poll_limit || offset >= FLASH_PAGE_SIZE ||
        (offset & 3u) || (operation == FLASH_EXEC_ERASE && offset))
        return FLASH_EXEC_INVALID_ARGUMENT;
    if (operation == FLASH_EXEC_PROGRAM) {
        if (word == NULL) return FLASH_EXEC_INVALID_ARGUMENT;
#if defined(CC2530_MAC_LINK_WORKSPACE)
        if (!link_work_external(word, 4)) return FLASH_EXEC_INVALID_ARGUMENT;
#endif
        address = MMIO_XADDRESS(word);
        if (address > 0x1dfcu || address <= MMIO_XADDRESS(&flash_exec_reserved_end))
            return FLASH_EXEC_INVALID_ARGUMENT;
    }
    mapping = MMIO_READ(SOC_MEMCTR);
    clock_command = MMIO_READ(SOC_CLKCONCMD);
    cache_mode = MMIO_XREAD(FCTL) & 0x0cu;
    if ((mapping & 0xf8u) || !idle(mapping)) {
        result = FLASH_EXEC_UNSUPPORTED_STATE; goto failed;
    }
    chip = MMIO_XREAD(0x624au); info0 = MMIO_XREAD(0x6276u); info1 = MMIO_XREAD(0x6277u);
    if (chip != 0xa5u || info0 != 0x44u || (info1 & 7u) != 7u) {
        result = FLASH_EXEC_UNSUPPORTED_STATE; goto failed;
    }
    code_size = TEMPLATE_SIZE;
    address = MMIO_XADDRESS(flash_exec_ram);
    if (!code_size || code_size > FLASH_EXEC_RAM_SIZE || address > 0x1e00u - FLASH_EXEC_RAM_SIZE) {
        result = FLASH_EXEC_CODE_CHANGED; goto failed;
    }
    for (i = 0; i < code_size; i++) flash_exec_ram[i] = TEMPLATE[i];
    for (i = 0; i < code_size; i++)
        if (flash_exec_ram[i] != TEMPLATE[i]) { result = FLASH_EXEC_CODE_CHANGED; goto failed; }
    flash_exec_work[0] = cache_mode | operation;
    for (i = 0; i < 4; i++) flash_exec_work[1+i] = operation == FLASH_EXEC_PROGRAM ? word[i] : 0;
    flash_exec_work[5] = (uint8_t)poll_limit;
    flash_exec_work[6] = (uint8_t)(poll_limit >> 8);
    flash_exec_work[8] = 0;
    if (!idle(mapping)) { result = FLASH_EXEC_UNSUPPORTED_STATE; goto failed; }
    target = (uint16_t)(FLASH_NV_BASE >> 2) + ((uint16_t)page << 9) + (offset >> 2);
    MMIO_XWRITE(0x6271u, target);
    MMIO_XWRITE(0x6272u, target >> 8);
    if (MMIO_XREAD(0x6271u) != (uint8_t)target || MMIO_XREAD(0x6272u) != (uint8_t)(target >> 8)) {
        result = FLASH_EXEC_CONTROLLER_STATE; goto failed;
    }
    MMIO_WRITE(SOC_MEMCTR, mapping | 8u);
    if (MMIO_READ(SOC_MEMCTR) != (mapping | 8u)) { result = FLASH_EXEC_MAPPING_CHANGED; goto failed; }
    if (!idle(mapping | 8u)) { result = FLASH_EXEC_UNSUPPORTED_STATE; goto failed; }
    flash_exec_work[7] = 0xff;
    enter_ram();
    if (flash_exec_work[7]) return (flash_exec_result_t)flash_exec_work[7];
    if (!idle(mapping | 8u)) { result = FLASH_EXEC_UNSUPPORTED_STATE; goto failed; }
    MMIO_WRITE(SOC_MEMCTR, mapping);
    if (MMIO_READ(SOC_MEMCTR) != mapping) { result = FLASH_EXEC_MAPPING_CHANGED; goto failed; }
    return FLASH_EXEC_IDLE;
failed:
    flash_exec_work[7] = result;
    return result;
}

MCU_XDATA uint8_t flash_exec_reserved_end;
