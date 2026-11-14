/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 * Host model for the CC2530_LEAN AES path: the engine advances on every
 * register read, so only behaviour is constrained (KAT, the exact 17-write
 * MMIO sequence, bounded waits, latching), not the read order.
 */
#include "aes.h"
#include "aes_vectors.h"
#include "aes_reference.h"
#include "host_mmio.h"
#include "timebase.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#if !defined(CC2530_LEAN)
#error "build with -DCC2530_LEAN"
#endif

extern volatile uint8_t aes_dma0[8], aes_dma1[32], aes_key[16], aes_iv[16], aes_input[16], aes_output[16];
extern uint8_t aes_fault, aes_used;
static uint8_t ram[0x800], fetched[2][8], loaded_key[16], loaded_input[16], cipher[16];
static aes_diagnostics_t d;
static unsigned operations, accesses, trace, phase, received, sent, ready, pending_arm, per_poll;
static unsigned encrypted, status_posted, stall_phase, stall_after, stall_output, finish_delay;
static unsigned hold_start_phase, tick_step, next_tick;
static uint8_t lost_irq, stale_ready;
static uint32_t start, ticks, latch;
static const uint8_t write_order[] = {0xd5,0xd4,0xd3,0xd2,0xd6,0xd6,0xb3,0xd1,0x98,0xd6,0xb3,0xd1,0x98,0xd6,0xb3,0xd1,0x98};
static const uint8_t write_values[] = {0,0x20,0,0x28,2,1,0x45,0x1e,0,1,0x47,0x1e,0,1,0x41,0x1c,0};

static volatile uint8_t *reg(uint8_t address)
{
    switch (address) {
#define REGISTER_CASE(name, location) case location: return &name;
        CC2530_REGISTER_LIST(REGISTER_CASE)
#undef REGISTER_CASE
    default: assert(0); return NULL;
    }
}

static uint16_t xaddress(const volatile void *object)
{
    uintptr_t address = (uintptr_t)object;
    if (address >= (uintptr_t)ram && address < (uintptr_t)ram + sizeof(ram))
        return (uint16_t)(0x400 + (address - (uintptr_t)ram));
    if (object == aes_dma0) return 0x20;
    if (object == aes_dma1) return 0x28;
    if (object == aes_key) return 0x48;
    if (object == aes_iv) return 0x58;
    if (object == aes_input) return 0x68;
    if (object == aes_output) return 0x78;
    assert(0); return 0;
}

static volatile uint8_t *stage(uint16_t address)
{
    if (address >= 0x48 && address < 0x58) return &aes_key[address - 0x48];
    if (address >= 0x58 && address < 0x68) return &aes_iv[address - 0x58];
    if (address >= 0x68 && address < 0x78) return &aes_input[address - 0x68];
    if (address >= 0x78 && address < 0x88) return &aes_output[address - 0x78];
    assert(0); return NULL;
}

static void engine(void)
{
    unsigned budget = per_poll;
    uint16_t source = (uint16_t)((unsigned)fetched[0][0] * 256 + fetched[0][1]);
    uint16_t destination = (uint16_t)((unsigned)fetched[1][2] * 256 + fetched[1][3]);
    if (!phase) return;
    while (budget && received < 16 && !(stall_phase == phase && received == stall_after)) {
        assert((SOC_DMAARM & 1) && (ready & 1));
        assert(fetched[0][2] == 0x70 && fetched[0][3] == 0xb1 && fetched[0][6] == 29);
        SOC_ENCDI = *stage((uint16_t)(source + received));
        if (phase == 1) loaded_key[received] = SOC_ENCDI;
        else if (phase == 2) assert(SOC_ENCDI == 0);
        else loaded_input[received] = SOC_ENCDI;
        received++; budget--;
        if (received == 16) {
            SOC_DMAARM &= 0xfe; ready &= ~1u;
            if (!(lost_irq & 1)) SOC_DMAIRQ |= 1;
        }
    }
    if (received != 16) return;
    if (phase != 3) {
        if (!status_posted) {
            status_posted = 1;
            if (!(lost_irq & 4)) SOC_S0CON |= 3;
        }
        return;
    }
    if (!encrypted) {
        aes_reference_encrypt(loaded_key, loaded_input, cipher);
        encrypted = 1;
    }
    if (!status_posted) {
        if (finish_delay) finish_delay--;
        else {
            status_posted = 1;
            if (!(lost_irq & 8)) SOC_ENCCS |= 8;
            if (!(lost_irq & 4)) SOC_S0CON |= 3;
        }
    }
    while (budget && sent < 16 && sent != stall_output) {
        assert((SOC_DMAARM & 2) && (ready & 2));
        assert(fetched[1][0] == 0x70 && fetched[1][1] == 0xb2 && fetched[1][6] == 30);
        *stage((uint16_t)(destination + sent)) = SOC_ENCDO = cipher[sent];
        sent++; budget--;
        if (sent == 16) {
            SOC_DMAARM &= 0xfd; ready &= ~2u;
            if (!(lost_irq & 2)) SOC_DMAIRQ |= 2;
        }
    }
}

static uint8_t load(uint8_t address, uint8_t value)
{
    read_count = 0; accesses++;
    if (address >= 0x95 && address <= 0x97) {
        assert(address == 0x95 + next_tick);
        if (!next_tick) { latch = (start + ticks) & TIMEBASE_TICKS_MASK; ticks += tick_step; }
        value = (uint8_t)(latch >> (8 * next_tick));
        next_tick = (next_tick + 1) % 3;
        return value;
    }
    assert(!next_tick);
    assert(address == 0x98 || address == 0xd1 || address == 0xb3 || address == 0xd6);
    engine();
    return *reg(address);
}

static void cycles(uint8_t count)
{
    unsigned ch = pending_arm == 1 ? 0 : 1, i;
    assert(count == 9 && pending_arm && !(ready & pending_arm));
    assert(SOC_DMA0CFGL == 0x20 && !SOC_DMA0CFGH && SOC_DMA1CFGL == 0x28 && !SOC_DMA1CFGH);
    memcpy(fetched[ch], (const void *)(ch ? aes_dma1 : aes_dma0), 8);
    assert(fetched[ch][4] == 0 && fetched[ch][5] == 16 && fetched[ch][6] == (ch ? 30 : 29));
    assert(fetched[ch][7] == (ch ? 0x11 : 0x41));
    if (ch) assert(fetched[ch][2] == 0 && fetched[ch][3] == 0x78);
    else assert(fetched[ch][0] == 0 && fetched[ch][1] == 0x48 + phase * 16);
    for (i = 8; i < 32; i++) assert(aes_dma1[i] == 0);
    ready |= pending_arm; pending_arm = 0;
}

static void store(uint8_t address, uint8_t before, uint8_t value)
{
    unsigned current = trace++;
    write_count = 0; accesses++;
    assert(current < sizeof(write_order) && address == write_order[current]);
    assert(value == (address == 0x98 ? (before & 0xfc) : write_values[current]));
    if (address == 0xd6) {
        assert(!pending_arm && !(before & value) && !SOC_DMAIRQ && !SOC_DMAREQ);
        SOC_DMAARM = before | value; pending_arm = value;
    } else if (address == 0xb3) {
        assert(ready == 3 && SOC_DMAARM == 3 && !SOC_DMAIRQ && !(SOC_S0CON & 3));
        assert((!phase && !received && !sent) || (phase < 3 && received == 16 && !sent));
        phase++; received = status_posted = 0;
        SOC_ENCCS = (value & 0xf6) | (phase == 3 && !stale_ready ? 0 : 8) | (phase == hold_start_phase ? 1 : 0);
    } else if (address == 0xd1) {
        assert(received == 16 && (SOC_S0CON & 3) == 3 && (phase < 3 || (sent == 16 && encrypted)));
        assert(before == (phase < 3 ? 1 : 3));
        SOC_DMAIRQ = before & value;
        if (phase < 3) ready &= ~1u;
    } else if (address == 0x98) {
        assert(received == 16 && !SOC_DMAIRQ && (before & 3) == 3);
        assert(SOC_DMAARM == (phase == 3 ? 0 : 2) && (phase != 3 || (sent == 16 && SOC_ENCCS == 0x48)));
        SOC_S0CON = value;
    }
}

static void begin_call(void)
{
    accesses = trace = phase = received = sent = ready = pending_arm = encrypted = status_posted = 0;
    next_tick = 0; ticks = 0;
}

static void reset_model(void)
{
    host_mmio_reset(); aes_fault = aes_used = 0;
    memset(ram, 0xa5, sizeof(ram)); memset(&d, 0xa5, sizeof(d));
    memset((void *)aes_dma0, 0, 8); memset((void *)aes_dma1, 0x5a, 32);
    memset((void *)aes_output, 0x69, 16);
    SOC_ENCCS = 8; SOC_S0CON = 0xa4;
    start = ticks = 0; tick_step = 1; per_poll = 16;
    stall_phase = 0; stall_after = stall_output = 17; finish_delay = hold_start_phase = 0;
    lost_irq = stale_ready = 0;
    host_mmio_read_hook = load; host_mmio_write_hook = store;
    host_mmio_xaddress_hook = xaddress; host_mmio_cycles_hook = cycles;
    begin_call();
}

static aes_result_t call(const uint8_t *key, const uint8_t *input, uint8_t *output, uint32_t timeout, uint16_t cap)
{
    operations++; accesses = trace = 0;
    return aes128_encrypt_block(key, input, output, timeout, cap, &d);
}

static uint16_t verify_success(const uint8_t *key, const uint8_t *input, uint8_t *out, uint16_t cap)
{
    uint8_t expected[16], k[16], in[16], saved[sizeof(ram)];
    uint8_t upper = SOC_S0CON;
    memcpy(k, key, 16); memcpy(in, input, 16); memcpy(saved, ram, sizeof(ram));
    aes_reference_encrypt(k, in, expected);
    begin_call();
    assert(call(key, input, out, 1000, cap) == AES_OK);
    assert(!memcmp(out, expected, 16));
    memcpy(saved + (out - ram), expected, 16);
    assert(!memcmp(ram, saved, sizeof(ram)));
    assert(!memcmp(k, loaded_key, 16) && !memcmp(in, loaded_input, 16));
    assert(trace == 17 && !aes_fault && aes_used && d.polls >= 3 && d.polls <= cap);
    assert(!SOC_DMAARM && !SOC_DMAIRQ && SOC_ENCCS == 0x48 && SOC_S0CON == upper);
    return d.polls;
}

static void verify_fault(aes_result_t expected, uint32_t timeout, uint16_t cap)
{
    uint8_t saved[sizeof(ram)], internal[104], saved_internal[104];
    memcpy(saved, ram, sizeof(ram));
    assert(call(ram, ram + 0x100, ram + 0x200, timeout, cap) == expected);
    assert(!memcmp(saved, ram, sizeof(ram)) && aes_fault == expected && d.polls <= cap);
    if (expected == AES_POLL_LIMIT) assert(d.polls == cap);
    memcpy(saved_internal, (const void *)aes_dma0, 8); memcpy(saved_internal + 8, (const void *)aes_dma1, 32);
    memcpy(saved_internal + 40, (const void *)aes_key, 16); memcpy(saved_internal + 56, (const void *)aes_iv, 16);
    memcpy(saved_internal + 72, (const void *)aes_input, 16); memcpy(saved_internal + 88, (const void *)aes_output, 16);
    /* Latched: no MMIO, staging, diagnostics or output effects. */
    assert(call(ram, ram + 0x100, ram + 0x200, 1000, 128) == expected);
    assert(!accesses && !trace && !memcmp(saved, ram, sizeof(ram)));
    memcpy(internal, (const void *)aes_dma0, 8); memcpy(internal + 8, (const void *)aes_dma1, 32);
    memcpy(internal + 40, (const void *)aes_key, 16); memcpy(internal + 56, (const void *)aes_iv, 16);
    memcpy(internal + 72, (const void *)aes_input, 16); memcpy(internal + 88, (const void *)aes_output, 16);
    assert(!memcmp(internal, saved_internal, sizeof(internal)));
}

int main(void)
{
    unsigned i, j, variant;
    uint16_t polls;
    aes_reference_check();
    for (i = 0; i < 5; i++) for (variant = 0; variant < 2; variant++) {
        reset_model();
        memcpy(ram, aes_test_vectors[i][0], 16); memcpy(ram + 0x100, aes_test_vectors[i][1], 16);
        verify_success(variant ? aes_test_vectors[i][0] : ram, variant ? aes_test_vectors[i][1] : ram + 0x100,
                       ram + 0x200, 128);
        assert(!memcmp(ram + 0x200, aes_test_vectors[i][2], 16));
    }
    /* Consecutive operations reuse the engine (aes_used) without re-entry setup. */
    reset_model();
    for (i = 0; i < 257; i++) {
        for (j = 0; j < 16; j++) { ram[j] = (uint8_t)(i*19 + j*31); ram[0x100+j] = (uint8_t)(i*37 + j*13); }
        verify_success(ram, ram + 0x100, ram + 0x200, 128);
    }
    /* In-place input/output. */
    reset_model(); verify_success(ram, ram + 0x100, ram + 0x100, 128);
    for (i = 0; i < 256; i += 4) {
        reset_model(); SOC_S0CON = (uint8_t)i; verify_success(ram, ram + 0x100, ram + 0x200, 128);
    }
    for (j = 1; j <= 16; j++) {
        reset_model(); per_poll = j; polls = verify_success(ram, ram + 0x100, ram + 0x200, 0xffff);
        reset_model(); per_poll = j; assert(verify_success(ram, ram + 0x100, ram + 0x200, polls) == polls);
        reset_model(); per_poll = j; verify_fault(AES_POLL_LIMIT, 0x7fffff, (uint16_t)(polls - 1));
    }
    reset_model(); finish_delay = 4; verify_success(ram, ram + 0x100, ram + 0x200, 128);
    reset_model(); stale_ready = 1; finish_delay = 4; verify_success(ram, ram + 0x100, ram + 0x200, 128);
    /* 24-bit wrap of the awake counter does not end the wait early. */
    reset_model(); start = 0xfffff8; verify_success(ram, ram + 0x100, ram + 0x200, 128);
    for (i = 1; i <= 3; i++) for (j = 0; j < 16; j++) {
        reset_model(); stall_phase = i; stall_after = j; verify_fault(AES_TIMEOUT, 40, 0xffff);
        assert(phase == i && received == j);
        reset_model(); stall_phase = i; stall_after = j; verify_fault(AES_POLL_LIMIT, 0x7fffff, 40);
    }
    for (i = 0; i < 16; i++) {
        reset_model(); stall_output = i; verify_fault(AES_TIMEOUT, 40, 0xffff);
        assert(received == 16 && sent == i && phase == 3);
    }
    for (i = 1; i <= 8; i <<= 1) { reset_model(); lost_irq = (uint8_t)i; verify_fault(AES_TIMEOUT, 40, 0xffff); }
    for (i = 1; i <= 3; i++) { reset_model(); hold_start_phase = i; verify_fault(AES_TIMEOUT, 40, 0xffff); }
    for (i = 1; i <= 40; i++) {
        reset_model(); stall_phase = 1; stall_after = 0; tick_step = 7; verify_fault(AES_TIMEOUT, i, 0xffff);
        assert(d.polls == (i + 6) / 7);
    }
    printf("AES lean host: %u operations; KAT, 17-write sequence, deadline/poll bounds and latch PASS\n", operations);
    return 0;
}
