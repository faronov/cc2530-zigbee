/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 * Standalone synthetic-controller image. NEVER FLASH THIS FIXTURE.
 */
#include "banked_fixture.h"
#include "flash_exec.h"

/* Private declarations: do not change the old global SFR list. */
__sfr __at(0x9f) fixture_fmap;
__sfr __at(0x92) fixture_dps;
__sfr __at(0xd0) fixture_psw;

volatile __xdata __at(0x1e00) uint8_t banked_fixture_status[64];
__xdata uint8_t banked_fixture_buffer[8];
__xdata uint8_t banked_fixture_word_bytes[4];
banked_fixture_pointer_t __xdata banked_fixture_pointer;

/* Public synthetic controller inputs, not initialization writes to hardware.
 * triples are XDATA-address low/high/value; see ABI document for event model.
 */
const __code uint8_t banked_fixture_controller_xdata[12] = {
    0x4a, 0x62, 0xa5, 0x76, 0x62, 0x44,
    0x77, 0x62, 0xff, 0x70, 0x62, 0x04
};
const __code uint8_t banked_fixture_controller_sfr[12] = {
    0xc6, 0xc9, 0x9e, 0xc9, 0xbe, 0x04,
    0xd6, 0x00, 0xd7, 0x00, 0xc7, 0x02
};

static void failed(void) __naked
{
    __asm
        clr 0xaf
        .globl _banked_fixture_failed
    _banked_fixture_failed:
        sjmp _banked_fixture_failed
    __endasm;
}

/* Deliberately separate storage/callees from foreground; no nesting or
 * higher-priority source is permitted. Generic8051 vector0 is an offline
 * injection point, NOT a configured CC2530 peripheral source/dispatcher.
 */
void banked_fixture_isr(void) __interrupt(0)
{
    uint32_t result;
    __asm
        push 0x9f
        push 0x92
        push 0x93
        .globl _banked_fixture_irq_enter
    _banked_fixture_irq_enter:
        nop
    __endasm;
    result = banked_fixture_irq_leaf(0x31415926UL);
    banked_fixture_status[18] = (uint8_t)result;
    banked_fixture_status[19] = (uint8_t)(result >> 8);
    banked_fixture_status[20] = (uint8_t)(result >> 16);
    banked_fixture_status[21] = (uint8_t)(result >> 24);
    banked_fixture_status[22]++;
    banked_fixture_status[44] = fixture_fmap;
    __asm
        .globl _banked_fixture_irq_exit
    _banked_fixture_irq_exit:
        nop
        pop 0x93
        pop 0x92
        pop 0x9f
    __endasm;
    /* Real compiler-generated full epilogue and RETI, never plain RET. */
}

unsigned char _sdcc_external_startup(void)
{
    SOC_IEN0 = 0;
    SOC_IEN1 = 0;
    SOC_IEN2 = 0;
    fixture_dps = 0;
    fixture_psw = 0;
    return 0;
}

uint32_t banked_fixture_common(uint32_t value)
{
    banked_fixture_status[43] |= 8;
    return value + 0x01020304UL;
}

uint8_t banked_fixture_flash_common(void)
{
    uint8_t result;
    banked_fixture_status[29] = fixture_fmap;
    __asm
        .globl _banked_fixture_flash_before
    _banked_fixture_flash_before:
        nop
    __endasm;
    result = (uint8_t)flash_exec_command(FLASH_EXEC_PROGRAM, 0, 0,
                                       banked_fixture_word_bytes, 4);
    banked_fixture_status[28] = result;
    banked_fixture_status[30] = fixture_fmap;
    banked_fixture_status[31] = SOC_MEMCTR;
    __asm
        .globl _banked_fixture_flash_after
    _banked_fixture_flash_after:
        nop
    __endasm;
    if (result != FLASH_EXEC_IDLE || (SOC_MEMCTR & 8u)) {
        banked_fixture_status[7] = 6;
        failed();
    }
    return result;
}

static void record32(uint8_t offset, uint32_t value)
{
    banked_fixture_status[offset] = (uint8_t)value;
    banked_fixture_status[offset + 1u] = (uint8_t)(value >> 8);
    banked_fixture_status[offset + 2u] = (uint8_t)(value >> 16);
    banked_fixture_status[offset + 3u] = (uint8_t)(value >> 24);
}

static void require(uint8_t condition, uint8_t reason)
{
    if (!condition) {
        banked_fixture_status[7] = reason;
        failed();
    }
}

static void constants(void)
{
    uint8_t i, result;
    require(banked_code_read(0, (uint16_t)banked_fixture_controller_xdata, 4,
                            banked_fixture_buffer) == BANKED_OK, 3);
    require(banked_fixture_buffer[0] == 0x4a && banked_fixture_buffer[3] == 0x76, 3);
    require(banked_code_read(1, (uint16_t)banked_fixture_const1, 4,
                            banked_fixture_buffer) == BANKED_OK, 3);
    require(banked_fixture_buffer[0] == 0x13 && banked_fixture_buffer[3] == 0xdf, 3);
    require(banked_code_read(2, (uint16_t)banked_fixture_const2, 4,
                            banked_fixture_buffer) == BANKED_OK, 3);
    require(banked_fixture_buffer[0] == 0x24 && banked_fixture_buffer[3] == 0xe0, 3);
    require(banked_code_read(7, (uint16_t)banked_fixture_const7, 8,
                            banked_fixture_buffer) == BANKED_OK, 3);
    for (i = 0; i < 8; i++) banked_fixture_status[32u+i] = banked_fixture_buffer[i];
    require(banked_fixture_buffer[0] == 0xd3 && banked_fixture_buffer[7] == 0x5b, 3);
    /* Error cases must not alter even the first output byte. No rejected
     * source is dereferenced; the parent can mutate remaining arguments.
     */
    result = banked_code_read(8, 0x8000, 1, banked_fixture_buffer);
    require(result == BANKED_INVALID_SPAN, 4);
    result = banked_code_read(7, 0xe800, 1, banked_fixture_buffer);
    require(result == BANKED_INVALID_SPAN, 4);
    result = banked_code_read(7, 0xf800, 1, banked_fixture_buffer);
    require(result == BANKED_INVALID_SPAN, 4);
    result = banked_code_read(7, 0xe7ff, 2, banked_fixture_buffer);
    require(result == BANKED_INVALID_SPAN, 4);
    result = banked_code_read(2, 0xffff, 2, banked_fixture_buffer);
    require(result == BANKED_INVALID_SPAN, 4);
    result = banked_code_read(0, 0x8000, 1, banked_fixture_buffer);
    require(result == BANKED_INVALID_SPAN, 4);
    result = banked_code_read(1, 0x8000, 0, banked_fixture_buffer);
    require(result == BANKED_INVALID_SPAN, 4);
    result = banked_code_read(1, 0x8000, 33, banked_fixture_buffer);
    require(result == BANKED_INVALID_SPAN, 4);
    result = banked_code_read(1, 0x8000, 1, (uint8_t __xdata *)0x1f00);
    require(result == BANKED_INVALID_OUTPUT, 4);
    result = banked_code_read(1, 0x8000, 2, (uint8_t __xdata *)0x1dff);
    require(result == BANKED_INVALID_OUTPUT, 4);
    result = banked_code_read(1, 0x8000, 1, &banked_reserved_end);
    require(result == BANKED_INVALID_OUTPUT, 4);
    SOC_MEMCTR |= 8;
    result = banked_code_read(1, 0x8000, 1, banked_fixture_buffer);
    SOC_MEMCTR &= (uint8_t)~8u;
    require(result == BANKED_UNSUPPORTED_STATE, 4);
    for (i = 0; i < 8; i++)
        require(banked_fixture_status[32u+i] == banked_fixture_buffer[i], 4);
    banked_fixture_status[40] = 12;
}

void main(void)
{
    uint8_t i, original;
    uint16_t word;
    uint32_t wide;
    for (i = 0; i < 64; i++) banked_fixture_status[i] = 0;
    banked_fixture_status[0] = 'B'; banked_fixture_status[1] = 'N';
    banked_fixture_status[2] = 'K'; banked_fixture_status[3] = '1';
    banked_fixture_status[4] = 1; banked_fixture_status[5] = 64;
    original = fixture_fmap;
    banked_fixture_status[23] = original;
    banked_fixture_status[6] = 1;
    __asm
        .globl _banked_fixture_before_calls
    _banked_fixture_before_calls:
        nop
    __endasm;
    word = banked_fixture_word(0x1234, 0xabcd);
    banked_fixture_status[8] = (uint8_t)word;
    banked_fixture_status[9] = (uint8_t)(word >> 8);
    require(word == 0xca1a, 1);
    wide = banked_fixture_bank1(0x12345678UL, 0x2468);
    record32(10, wide);
    require(wide == 0x332cd55fUL && banked_fixture_status[43] == 15, 1);
    banked_fixture_pointer = banked_fixture_pointer_leaf;
    wide = banked_fixture_pointer(0x13572468UL);
    record32(14, wide);
    require(wide == 0xd00d8154UL, 2);
    banked_fixture_status[24] = fixture_fmap;
    require(fixture_fmap == original && banked_depth == 0, 2);
    __asm
        .globl _banked_fixture_after_calls
    _banked_fixture_after_calls:
        nop
    __endasm;
    banked_fixture_status[6] = 2;
    __asm
        .globl _banked_fixture_before_constants
    _banked_fixture_before_constants:
        nop
    __endasm;
    constants();
    banked_fixture_status[25] = fixture_fmap;
    require(fixture_fmap == original, 3);
    __asm
        .globl _banked_fixture_after_constants
    _banked_fixture_after_constants:
        nop
    __endasm;
    banked_fixture_status[6] = 3;
    banked_fixture_status[26] = fixture_fmap;
    SOC_IEN0 = 0x81;
    wide = banked_fixture_irq_foreground(0x89abcdefUL);
    SOC_IEN0 = 0;
    record32(48, wide);
    banked_fixture_status[27] = fixture_fmap;
    require(wide == 0xffffffffUL && banked_fixture_status[22] == 1 &&
            banked_fixture_status[18] == 0x6d && banked_fixture_status[19] == 0x91 &&
            banked_fixture_status[20] == 0x6a && banked_fixture_status[21] == 0x41 &&
            banked_fixture_status[44] == 2 && fixture_fmap == original, 5);
    __asm
        .globl _banked_fixture_after_irq
    _banked_fixture_after_irq:
        nop
    __endasm;
    banked_fixture_status[6] = 4;
    banked_fixture_word_bytes[0] = 0x12; banked_fixture_word_bytes[1] = 0x34;
    banked_fixture_word_bytes[2] = 0x56; banked_fixture_word_bytes[3] = 0x78;
    require(banked_fixture_flash() == FLASH_EXEC_IDLE && fixture_fmap == original, 6);
    banked_fixture_status[41] = banked_depth;
    banked_fixture_status[42] = banked_fault;
    require(!banked_depth && !banked_fault, 7);
    banked_fixture_status[6] = 5;
    __asm
        .globl _banked_fixture_done
    _banked_fixture_done:
        sjmp _banked_fixture_done
    __endasm;
}
