/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 * Synthetic isolated executable, including generic C52 ISRs. NEVER flash it.
 */
#include "irq.h"
#include "cc2530_mmio.h"

#if defined(__SDCC)
volatile MCU_XDATA MCU_AT(0x1e00) uint8_t irq_test_result[8];
volatile MCU_XDATA uint8_t irq_test_mode, irq_test_token, irq_test_return;
volatile MCU_XDATA uint8_t irq_test_low_count, irq_test_high_count, irq_test_error;

#define CHECK(condition) do { if (!(condition)) return (uint16_t)__LINE__; } while (0)

static uint16_t self_test(void)
{
    static const MCU_CODE uint8_t tokens[] = {0, 1, 2, 3, 0x7f, 0x80, 0xfe, 0xff};
    uint16_t value;
    uint8_t i, lower;
    irq_state_t outer, inner;
    for (value = 0; value < 256; value++) {
        SOC_IEN0 = (uint8_t)value;
        SOC_IEN1 = (uint8_t)(value ^ 0x96u);
        SOC_IEN2 = (uint8_t)(value ^ 0x69u);
        outer = irq_save_disable();
        inner = irq_save_disable();
        CHECK(outer == value >> 7 && inner == 0 && SOC_IEN0 == (value & 0x7fu));
        CHECK(irq_restore(inner) == IRQ_OK && SOC_IEN0 == (value & 0x7fu));
        lower = (uint8_t)((value ^ 0x55u) & 0x7fu);
        SOC_IEN0 = lower;
        CHECK(irq_restore(outer) == IRQ_OK && SOC_IEN0 == (lower | (value & 0x80u)));
        for (i = 0; i < sizeof(tokens); i++) {
            SOC_IEN0 = (uint8_t)value;
            CHECK(irq_restore(tokens[i]) == (tokens[i] <= 1 ? IRQ_OK : IRQ_INVALID_TOKEN));
            CHECK(SOC_IEN0 == (tokens[i] <= 1 ? (value & 0x7fu) | ((uint16_t)tokens[i] << 7) : value));
        }
        CHECK(SOC_IEN1 == (uint8_t)(value ^ 0x96u) && SOC_IEN2 == (uint8_t)(value ^ 0x69u));
    }
    SOC_IEN0 = SOC_IEN1 = SOC_IEN2 = 0;
    return 0;
}

static void nested(void) __reentrant
{
    irq_state_t outer = irq_save_disable();
    irq_state_t inner = irq_save_disable();
    __asm
        .globl _irq_test_disabled
    _irq_test_disabled:
        nop
    __endasm;
    if (inner != 0 || (SOC_IEN0 & 0x80u))
        irq_test_error = 1;
    if (irq_restore(inner) != IRQ_OK || (SOC_IEN0 & 0x80u))
        irq_test_error = 2;
    __asm
        .globl _irq_test_before_outer
    _irq_test_before_outer:
        nop
    __endasm;
    if (irq_restore(outer) != IRQ_OK || (SOC_IEN0 >> 7) != outer)
        irq_test_error = 3;
}

/* These vector numbers and pending sources belong to the synthetic C52 CPU,
 * not to a CC2530 peripheral dispatcher.
 */
void irq_test_low(void) __interrupt(0)
{
    irq_test_low_count++;
    nested();
}

void irq_test_high(void) __interrupt(2)
{
    irq_test_high_count++;
    nested();
}

void irq_test_cycle(void) __reentrant
{
    __asm
        .globl _irq_test_before
    _irq_test_before:
        nop
    __endasm;
    if (irq_test_mode == 0)
        irq_test_return = irq_save_disable();
    else if (irq_test_mode == 1)
        irq_test_return = irq_restore(irq_test_token);
    else
        nested();
    __asm
        .globl _irq_test_done
    _irq_test_done:
        nop
    __endasm;
}

void main(void)
{
    uint16_t result = self_test();
    irq_test_result[0] = 'I';
    irq_test_result[1] = 'R';
    irq_test_result[2] = 'Q';
    irq_test_result[3] = 'T';
    irq_test_result[4] = 1;
    irq_test_result[5] = 8;
    irq_test_result[6] = (uint8_t)result;
    irq_test_result[7] = (uint8_t)(result >> 8);
    for (;;)
        irq_test_cycle();
}
#else
#include "host_mmio.h"

#include <assert.h>
#include <stdio.h>

static uint8_t preserved[256];

static void prepare(uint8_t enables, uint8_t other)
{
    host_mmio_reset();
#define SEED(name, address) name = (uint8_t)((address) ^ other);
    CC2530_REGISTER_LIST(SEED)
#undef SEED
    SOC_IEN0 = enables;
    SOC_IEN1 = other;
    SOC_IEN2 = (uint8_t)~other;
#define SAVE(name, address) preserved[address] = name;
    CC2530_REGISTER_LIST(SAVE)
#undef SAVE
}

static void unchanged(void)
{
#define CHECK_REGISTER(name, address) if (address != 0xa8) assert(name == preserved[address]);
    CC2530_REGISTER_LIST(CHECK_REGISTER)
#undef CHECK_REGISTER
}

int main(void)
{
    unsigned enables, other, i, token;
    for (enables = 0; enables < 256; enables++) {
        for (other = 0; other < 256; other++) {
            uint8_t tokens[10] = {0x69, 0, 0, 0, 0, 0, 0, 0, 0, 0x96};
            prepare((uint8_t)enables, (uint8_t)other);
            for (i = 1; i <= 8; i++) {
                tokens[i] = irq_save_disable();
                assert(tokens[i] == (i == 1 ? enables >> 7 : 0));
                assert(SOC_IEN0 == (enables & 0x7f));
                assert(read_count == i && reads[i - 1].address == 0xa8);
                assert(reads[i - 1].value == (i == 1 ? enables : enables & 0x7f));
                assert(write_count == (enables >> 7));
                unchanged();
            }
            for (i = 8; i; i--) {
                assert(irq_restore(tokens[i]) == IRQ_OK);
                assert(tokens[i] == (i == 1 ? enables >> 7 : 0));
                assert(SOC_IEN0 == (i == 1 ? enables : enables & 0x7f));
                unchanged();
            }
            assert(tokens[0] == 0x69 && tokens[9] == 0x96);
            assert(read_count == 8 && write_count == 8 + (enables >> 7));
            for (i = 0; i < write_count; i++) {
                assert(writes[i].address == 0xa8);
                assert((writes[i].before & 0x7f) == (enables & 0x7f));
                assert((writes[i].after & 0x7f) == (enables & 0x7f));
            }
        }
        for (token = 0; token < 256; token++) {
            struct { uint8_t before, token, after; } input = {0x69, (uint8_t)token, 0x96};
            prepare((uint8_t)enables, (uint8_t)token);
            assert(irq_restore(input.token) == (token <= 1 ? IRQ_OK : IRQ_INVALID_TOKEN));
            assert(input.before == 0x69 && input.token == token && input.after == 0x96);
            assert(read_count == 0 && write_count == (token <= 1));
            assert(SOC_IEN0 == (token <= 1 ? (enables & 0x7f) | (token << 7) : enables));
            if (write_count)
                assert(writes[0].address == 0xa8 && writes[0].before == enables &&
                       writes[0].after == SOC_IEN0);
            unchanged();
        }
        for (other = 0; other < 128; other++) {
            irq_state_t saved;
            prepare((uint8_t)enables, 0);
            saved = irq_save_disable();
            SOC_IEN0 = (uint8_t)other;
            assert(irq_restore(saved) == IRQ_OK);
            assert(SOC_IEN0 == (other | (enables & 0x80)));
            assert(write_count == 1 + (enables >> 7));
            assert(writes[write_count - 1].before == other);
            assert(writes[write_count - 1].after == SOC_IEN0);
            unchanged();
        }
    }
    puts("host IRQ: 65536 enable combinations, eight-deep LIFO, all tokens, "
         "current lower enables, exact MMIO/guards PASS (sequential host model)");
    return 0;
}
#endif
