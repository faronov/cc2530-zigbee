/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 * Original synthetic AES/DMA controller; real aes.c executes every operation.
 */
#include "security_aes_model.h"
#include "aes.h"
#include "aes_reference.h"
#include "host_mmio.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

extern volatile uint8_t aes_dma0[8], aes_dma1[32], aes_key[16], aes_iv[16], aes_input[16], aes_output[16];
extern uint8_t aes_fault, aes_used, aes_reserved_end;
uint8_t _gptrput_PARM_2;
static const volatile void *objects[16];
static uint8_t count, phase, ready, pending, active, fetched[2][8], key[16], input[16], output[16], trace;
static unsigned blocks, stall, ticks;

static uint16_t address(const volatile void *object)
{
    uint8_t i;
    if (object == &aes_reserved_end) return 0x1ff;
    if (object == &_gptrput_PARM_2) return 0x1d00;
    if (object == aes_dma0) return 0x20;
    if (object == aes_dma1) return 0x28;
    if (object == aes_key) return 0x48;
    if (object == aes_iv) return 0x58;
    if (object == aes_input) return 0x68;
    if (object == aes_output) return 0x78;
    for (i = 0; i < count; i++)
        if (objects[i] == object)
            return (uint16_t)(0x400u + 64u * i);
    assert(count < sizeof(objects) / sizeof(objects[0]));
    objects[count++] = object;
    return (uint16_t)(0x400u + 64u * i);
}

uint32_t host_aes_pointer(const uint8_t *object)
{
    return address(object);
}

static void hex(const uint8_t *bytes)
{
    uint8_t i;
    for (i = 0; i < 16; i++)
        printf("%02x", bytes[i]);
}

static void engine(void)
{
    uint8_t i;
    if (!active || !(SOC_DMAARM & 1) || blocks == stall)
        return;
    assert(ready == 3 && SOC_DMAARM == 3);
    assert(!memcmp(fetched[0], (const void *)aes_dma0, 8));
    assert(!memcmp(fetched[1], (const void *)aes_dma1, 8));
    for (i = 0; i < 16; i++) {
        SOC_ENCDI = phase == 1 ? aes_key[i] : phase == 2 ? aes_iv[i] : aes_input[i];
        if (phase == 1) key[i] = SOC_ENCDI;
        else if (phase == 2) assert(!SOC_ENCDI);
        else input[i] = SOC_ENCDI;
    }
    SOC_DMAARM = 2; ready = 2; SOC_DMAIRQ |= 1; SOC_S0CON |= 3;
    SOC_ENCCS &= 0xfe;
    active = 0;
    if (phase != 3)
        return;
    aes_reference_encrypt(key, input, output);
    if (trace) {
        fputs("BLOCK ", stdout); hex(key); putchar(' '); hex(input);
        putchar(' '); hex(output); putchar('\n');
    }
    for (i = 0; i < 16; i++) {
        SOC_ENCDO = output[i];
        aes_output[i] = SOC_ENCDO;
    }
    SOC_DMAARM = ready = 0; SOC_DMAIRQ |= 2; SOC_ENCCS |= 8;
}

static uint8_t load(uint8_t reg, uint8_t value)
{
    read_count = 0;
    if (reg == 0xa8)
        engine();
    if (reg == 0x95) {
        ticks++;
        return (uint8_t)ticks;
    }
    if (reg == 0x96) return (uint8_t)(ticks >> 8);
    if (reg == 0x97) return (uint8_t)(ticks >> 16);
    return value;
}

static void cycles(uint8_t n)
{
    uint8_t channel = pending == 1 ? 0 : 1, i;
    assert(n == 9 && pending && !(ready & pending));
    assert(SOC_DMA0CFGL == 0x20 && !SOC_DMA0CFGH && SOC_DMA1CFGL == 0x28 && !SOC_DMA1CFGH);
    memcpy(fetched[channel], (const void *)(channel ? aes_dma1 : aes_dma0), 8);
    assert(fetched[channel][4] == 0 && fetched[channel][5] == 16);
    assert(fetched[channel][6] == (channel ? 30 : 29) && fetched[channel][7] == (channel ? 0x11 : 0x41));
    if (channel) {
        assert(fetched[1][0] == 0x70 && fetched[1][1] == 0xb2 &&
               !fetched[1][2] && fetched[1][3] == 0x78);
        for (i = 8; i < 32; i++) assert(!aes_dma1[i]);
    } else {
        assert(!fetched[0][0] && fetched[0][1] == 0x48 + phase * 16 &&
               fetched[0][2] == 0x70 && fetched[0][3] == 0xb1);
    }
    ready |= pending; pending = 0;
}

static void store(uint8_t reg, uint8_t before, uint8_t value)
{
    write_count = 0; read_count = 0;
    if (reg == 0xd5) {
        assert(!SOC_DMAARM && !ready && !pending);
        blocks++; phase = 0;
    } else if (reg == 0xd6) {
        assert(value == 1 || value == 2);
        assert(!(before & value) && !pending && !SOC_DMAIRQ);
        SOC_DMAARM = before | value; pending = value;
    } else if (reg == 0xb3) {
        assert(SOC_DMAARM == 3 && ready == 3 && !SOC_DMAIRQ && !(SOC_S0CON & 3));
        assert(value == (phase == 0 ? 0x45 : phase == 1 ? 0x47 : 0x41));
        phase++; active = 1;
        SOC_ENCCS = value & 0xf6;
    } else if (reg == 0xd1) {
        assert(value == (phase == 3 ? 0x1c : 0x1e));
        SOC_DMAIRQ = before & value;
    } else if (reg == 0x98) {
        assert((before & 3) == 3 && !(value & 3) && !SOC_DMAIRQ);
    } else {
        assert(reg == 0xd4 || reg == 0xd3 || reg == 0xd2);
    }
}

void security_aes_reset(void)
{
    host_mmio_reset();
    aes_fault = aes_used = count = phase = ready = pending = active = 0;
    blocks = ticks = stall = 0;
    SOC_CLKCONCMD = SOC_CLKCONSTA = 0xc9; SOC_SLEEPCMD = 4;
    SOC_ENCCS = 8; SOC_S0CON = 0xa4; SOC_IRCON = 0xbe;
    host_mmio_read_hook = load;
    host_mmio_write_hook = store;
    host_mmio_cycles_hook = cycles;
    host_mmio_xaddress_hook = address;
}

void security_aes_stall(unsigned block) { stall = block; }
unsigned security_aes_blocks(void) { return blocks; }
void security_aes_trace(uint8_t enabled) { trace = enabled; }
