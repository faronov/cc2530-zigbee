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
    /* Default hook is address(). A combined controller may supply one shared
     * synthetic RAM map without resetting the live AES/DMA controller. */
    return host_mmio_xaddress_hook(object);
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
    uint16_t d0=host_aes_pointer((const uint8_t *)aes_dma0);
    uint16_t d1=host_aes_pointer((const uint8_t *)aes_dma1);
    uint16_t source=host_aes_pointer((const uint8_t *)(phase==0?aes_key:phase==1?aes_iv:aes_input));
    uint16_t destination=host_aes_pointer((const uint8_t *)aes_output);
    assert(n == 9 && pending && !(ready & pending));
    assert(SOC_DMA0CFGL == (uint8_t)d0 && SOC_DMA0CFGH == (uint8_t)(d0>>8) &&
           SOC_DMA1CFGL == (uint8_t)d1 && SOC_DMA1CFGH == (uint8_t)(d1>>8));
    memcpy(fetched[channel], (const void *)(channel ? aes_dma1 : aes_dma0), 8);
    assert(fetched[channel][4] == 0 && fetched[channel][5] == 16);
    assert(fetched[channel][6] == (channel ? 30 : 29) && fetched[channel][7] == (channel ? 0x11 : 0x41));
    if (channel) {
        assert(fetched[1][0] == 0x70 && fetched[1][1] == 0xb2 &&
               fetched[1][2] == (uint8_t)(destination>>8) && fetched[1][3] == (uint8_t)destination);
        for (i = 8; i < 32; i++) assert(!aes_dma1[i]);
    } else {
        assert(fetched[0][0] == (uint8_t)(source>>8) && fetched[0][1] == (uint8_t)source &&
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

static struct {
    const volatile void *objects[16];
    uint8_t count, phase, ready, pending, active, fetched[2][8];
    uint8_t key[16], input[16], output[16], trace, fault, used, sfr[256];
    uint8_t dma0[8], dma1[32], aes_key[16], iv[16], aes_input[16], aes_output[16];
    unsigned blocks, stall, ticks;
} peer_saved;
static uint8_t peer_active;

void security_aes_peer_enter(void)
{
    assert(!peer_active && !aes_fault && !active && !ready && !pending && !SOC_DMAARM && !SOC_DMAREQ);
    peer_active = 1;
#define SAVE(name) peer_saved.name = name
    SAVE(count); SAVE(phase); SAVE(ready); SAVE(pending); SAVE(active);
    SAVE(trace); SAVE(blocks); SAVE(stall); SAVE(ticks);
#undef SAVE
#define SAVE_ARRAY(name) memcpy(peer_saved.name, name, sizeof(name))
    SAVE_ARRAY(objects); SAVE_ARRAY(fetched); SAVE_ARRAY(key); SAVE_ARRAY(input); SAVE_ARRAY(output);
#undef SAVE_ARRAY
    peer_saved.fault = aes_fault; peer_saved.used = aes_used;
    memcpy(peer_saved.dma0, (const void *)aes_dma0, sizeof(aes_dma0));
    memcpy(peer_saved.dma1, (const void *)aes_dma1, sizeof(aes_dma1));
    memcpy(peer_saved.aes_key, (const void *)aes_key, sizeof(aes_key));
    memcpy(peer_saved.iv, (const void *)aes_iv, sizeof(aes_iv));
    memcpy(peer_saved.aes_input, (const void *)aes_input, sizeof(aes_input));
    memcpy(peer_saved.aes_output, (const void *)aes_output, sizeof(aes_output));
#define REG(name, address) peer_saved.sfr[address] = name;
    CC2530_REGISTER_LIST(REG)
#undef REG
}

void security_aes_peer_leave(void)
{
    assert(peer_active && !aes_fault && !active && !ready && !pending && !SOC_DMAARM && !SOC_DMAREQ);
#define RESTORE(name) name = peer_saved.name
    RESTORE(count); RESTORE(phase); RESTORE(ready); RESTORE(pending); RESTORE(active);
    RESTORE(trace); RESTORE(blocks); RESTORE(stall); RESTORE(ticks);
#undef RESTORE
#define RESTORE_ARRAY(name) memcpy(name, peer_saved.name, sizeof(name))
    RESTORE_ARRAY(objects); RESTORE_ARRAY(fetched); RESTORE_ARRAY(key); RESTORE_ARRAY(input); RESTORE_ARRAY(output);
#undef RESTORE_ARRAY
    aes_fault = peer_saved.fault; aes_used = peer_saved.used;
    memcpy((void *)aes_dma0, peer_saved.dma0, sizeof(aes_dma0));
    memcpy((void *)aes_dma1, peer_saved.dma1, sizeof(aes_dma1));
    memcpy((void *)aes_key, peer_saved.aes_key, sizeof(aes_key));
    memcpy((void *)aes_iv, peer_saved.iv, sizeof(aes_iv));
    memcpy((void *)aes_input, peer_saved.aes_input, sizeof(aes_input));
    memcpy((void *)aes_output, peer_saved.aes_output, sizeof(aes_output));
#define REG(name, address) name = peer_saved.sfr[address];
    CC2530_REGISTER_LIST(REG)
#undef REG
    peer_active = 0;
}
