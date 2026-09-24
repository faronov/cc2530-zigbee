/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 *
 * Original trampoline, derived from this project's compiler listings, not
 * an imported runtime. SWRU191F 2.2.2/2.2.5, pp27,33-34: common CODE and FMAP.
 */
#include "banked.h"

__sfr __at(0x9f) banked_fmap;
__sfr __at(0xc7) banked_memctr;
__sfr __at(0x92) banked_dps;
__sfr __at(0xd0) banked_psw;

volatile __data uint8_t banked_depth;
volatile __data uint8_t banked_fault;

/* A is the fault code. Never resume, restore XMAP, or return to unknown CODE.
 * All assembly numeric limits/codes below are pinned by the ABI document.
 */
static void stop(void) __naked
{
    __asm
        .globl _banked_fault_entry
        .globl _banked_stop
    _banked_fault_entry:
        clr 0xaf
        mov _banked_fault,a
    _banked_stop:
        sjmp _banked_stop
    __endasm;
}

/* SDCC emits exactly this linker symbol (two leading underscores).
 * At entry: R0/R1/R2 = low/high/bank, DPL/DPH/B/A = first argument.
 * Stack: original LCALL return; add caller FMAP and RET into target.
 * Neither scratch DATA nor parameter storage is used by these trampolines.
 */
void _sdcc_banked_call(void) __naked
{
    __asm
        ; R3/R4 are call-clobbered, not argument/return registers in this ABI.
        ; Check BEFORE PUSH: even SP=ff must not wrap and overwrite IRAM0.
        mov r3,psw
        mov r4,sp
        cjne r4,#(BANKED_STACK_FIRST + 1),00007$
    00007$:
        jc 00003$
        cjne r4,#0x7a,00008$
    00008$:
        jnc 00003$
        push acc
        push ar3
        mov a,0xd0
        anl a,#0x18
        jnz 00001$
        mov a,0x92
        jnz 00001$
        mov a,0xc7
        anl a,#0xf8
        jnz 00001$
        mov a,0x9f
        anl a,#0xf8
        jnz 00001$
        mov a,_banked_depth
        clr c
        subb a,#8
        jnc 00002$
        mov a,r2
        jz 00004$
        anl a,#0xf8
        jnz 00004$
        mov a,r1
        jnb acc.7,00004$
        cjne r2,#7,00005$
        clr c
        subb a,#0xe8
        jnc 00004$
    00005$:
        inc _banked_depth
        pop psw
        pop acc
        push 0x9f
        push acc
        mov 0x9f,r2
        mov a,0x9f
        xrl a,r2
        jnz 00006$
        pop acc
        push ar0
        push ar1
        ret
    00001$:
        mov a,#2
        ljmp _banked_fault_entry
    00002$:
        mov a,#3
        ljmp _banked_fault_entry
    00003$:
        mov a,#4
        ljmp _banked_fault_entry
    00004$:
        mov a,#1
        ljmp _banked_fault_entry
    00006$:
        mov a,#5
        ljmp _banked_fault_entry
    __endasm;
}

/* A banked C return is LJMP here, not LCALL. Keep all return registers.
 * No attempt to hide an outstanding XMAP/flash failure behind a return.
 */
void _sdcc_banked_ret(void) __naked
{
    __asm
        mov r3,psw
        mov r4,sp
        cjne r4,#(BANKED_STACK_FIRST + 2),00014$
    00014$:
        jc 00013$
        cjne r4,#0x7b,00015$
    00015$:
        jnc 00013$
        push acc
        push ar3
        mov a,0xd0
        anl a,#0x18
        jnz 00011$
        mov a,0x92
        jnz 00011$
        mov a,0xc7
        anl a,#0xf8
        jnz 00011$
        mov a,_banked_depth
        jz 00012$
        clr c
        subb a,#9
        jnc 00012$
        dec _banked_depth
        pop psw
        pop acc
        pop ar0
        push acc
        mov a,r0
        anl a,#0xf8
        jnz 00016$
        mov 0x9f,r0
        mov a,0x9f
        xrl a,r0
        jnz 00016$
        pop acc
        ret
    00011$:
        mov a,#2
        ljmp _banked_fault_entry
    00012$:
        mov a,#3
        ljmp _banked_fault_entry
    00013$:
        mov a,#4
        ljmp _banked_fault_entry
    00016$:
        mov a,#5
        ljmp _banked_fault_entry
    __endasm;
}

uint8_t banked_code_read(uint8_t bank, uint16_t address, uint8_t length,
                         uint8_t __xdata *destination)
{
    uint16_t last, output;
    uint8_t saved, i;
    if (!length || length > BANKED_CODE_MAX_READ || bank > 7)
        return BANKED_INVALID_SPAN;
    last = bank == 0 ? 0x7fffu : bank == 7 ? 0xe7ffu : 0xffffu;
    if ((bank != 0 && address < 0x8000u) || address > last ||
        (uint16_t)(length - 1u) > (uint16_t)(last - address))
        return BANKED_INVALID_SPAN;
    output = (uint16_t)destination;
    if (output <= (uint16_t)&banked_reserved_end ||
        output > (uint16_t)(0x1e00u - length))
        return BANKED_INVALID_OUTPUT;
    if ((banked_psw & 0x18u) || banked_dps || (banked_memctr & 0xf8u) ||
        (banked_fmap & 0xf8u))
        return BANKED_UNSUPPORTED_STATE;
    saved = banked_fmap;
    banked_fmap = bank;
    if (banked_fmap != bank) {
        __asm
            mov a,#5
            ljmp _banked_fault_entry
        __endasm;
    }
    for (i = 0; i < length; i++)
        destination[i] = *((const uint8_t __code *)(address + i));
    banked_fmap = saved;
    if (banked_fmap != saved) {
        __asm
            mov a,#5
            ljmp _banked_fault_entry
        __endasm;
    }
    return BANKED_OK;
}

/* Must remain after all helper parameters/locals in actual linked XSEG. */
__xdata uint8_t banked_reserved_end;
