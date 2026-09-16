/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "irq.h"
#include "cc2530_mmio.h"

#if defined(__SDCC)
__sbit __at(0xaf) irq_ea;

/* SWRU191F pp.41-46: EA is a control bit, not an interrupt flag.
 * JBC reads/clears only EA; no XCH A,IEN0 or whole-enable-byte write.
 */
irq_state_t irq_save_disable(void) __reentrant __naked
{
    __asm
        jbc _irq_ea,00001$
        mov dpl,#0
        ret
    00001$:
        mov dpl,#1
        ret
    __endasm;
}

irq_result_t irq_restore(irq_state_t token) __reentrant __naked
{
    (void)token;
    __asm
        mov a,dpl
        jz 00002$
        dec a
        jnz 00003$
        setb _irq_ea
        mov dpl,#0
        ret
    00002$:
        clr _irq_ea
        mov dpl,#0
        ret
    00003$:
        mov dpl,#1
        ret
    __endasm;
}
#else
irq_state_t irq_save_disable(void)
{
    irq_state_t state = MMIO_READ(SOC_IEN0) >> 7;
    if (state)
        MMIO_CLEAR(SOC_IEN0, 0x80u);
    return state;
}

irq_result_t irq_restore(irq_state_t token)
{
    if (token > 1)
        return IRQ_INVALID_TOKEN;
    if (token)
        MMIO_SET(SOC_IEN0, 0x80u);
    else
        MMIO_CLEAR(SOC_IEN0, 0x80u);
    return IRQ_OK;
}
#endif
