/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 * Isolated synthetic executable. NEVER flash this image.
 */
#include "aes.h"
#include "aes_vectors.h"
#include "timebase.h"
#include <stddef.h>

#if defined(__SDCC)
volatile MCU_XDATA MCU_AT(0x1e00) uint8_t aes_test_result[8];
MCU_XDATA aes_diagnostics_t aes_test_diagnostics;
MCU_XDATA uint8_t aes_test_key[16], aes_test_input[32], aes_test_output[18];
const uint8_t * volatile MCU_XDATA aes_test_key_pointer, * volatile MCU_XDATA aes_test_input_pointer;
volatile MCU_XDATA uint16_t aes_test_out, aes_test_diag, aes_test_limit;
volatile MCU_XDATA uint32_t aes_test_timeout;
volatile MCU_XDATA uint8_t aes_test_return;

void aes_test_cycle(void)
{
    __asm
        .globl _aes_test_before
    _aes_test_before:
        nop
    __endasm;
    aes_test_return = aes128_encrypt_block(aes_test_key_pointer, aes_test_input_pointer,
                                          (uint8_t MCU_XDATA *)aes_test_out,
                                          aes_test_timeout, aes_test_limit,
                                          (aes_diagnostics_t MCU_XDATA *)aes_test_diag);
    __asm
        .globl _aes_test_done
    _aes_test_done:
        nop
    __endasm;
}

void main(void)
{
    uint8_t i;
    for (i = 0; i < 16; i++) {
        aes_test_key[i] = aes_test_vectors[0][0][i];
        aes_test_input[i] = aes_test_vectors[0][1][i];
    }
    for (i = 0; i < 18; i++) aes_test_output[i] = 0xa5;
    aes_test_key_pointer = aes_test_vectors[0][0];
    aes_test_input_pointer = aes_test_input;
    aes_test_out = (uint16_t)&aes_test_output[1];
    aes_test_diag = (uint16_t)&aes_test_diagnostics;
    aes_test_timeout = 1000; aes_test_limit = 128;
    aes_test_result[0] = 'A'; aes_test_result[1] = 'E'; aes_test_result[2] = 'S'; aes_test_result[3] = 'T';
    aes_test_result[4] = 1; aes_test_result[5] = 8; aes_test_result[6] = 0; aes_test_result[7] = 0;
    for (;;) aes_test_cycle();
}
#else
#include "aes_reference.h"
#include "host_mmio.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

extern volatile uint8_t aes_dma0[8], aes_dma1[32], aes_key[16], aes_iv[16], aes_input[16], aes_output[16];
extern uint8_t aes_fault, aes_used, aes_reserved_end;
uint8_t _gptrput_PARM_2;
static uint8_t ram[0x10000], saved_ram[0x10000], fetched[2][8], loaded_key[16], loaded_input[16], cipher[16];
static aes_diagnostics_t d;
static uint16_t diag_address;
static uint32_t override_location;
static const uint8_t *override_pointer;
static unsigned operations, accesses, observations, samples, next_read, next_tick, extra_clock, trace;
static unsigned phase, received, sent, ready, pending_arm, per_poll, encrypted, input_bytes, output_bytes;
static unsigned stall_phase, stall_after, stall_output, ignore_write, mutate_observation;
static unsigned finish_delay, status_posted, hold_start_phase, defer_observation, enc_phase, enc_value, enc_delay, ack_late;
static uint8_t mutate_address, mutate_value, last_address, last_value, lost_irq, load_rdy, stale_ready;
static uint32_t start, jump, jump_sample, latch;
static const uint8_t order[] = {0xa8,0xb8,0x9a,0xbe,0xc6,0x9e,0xb3,0x98,0xd6,0xd7,0xd1,0xc0,0xd4,0xd5,0xd2,0xd3};
static const uint8_t write_order[] = {0xd5,0xd4,0xd3,0xd2,0xd6,0xd6,0xb3,0xd1,0x98,0xd6,0xb3,0xd1,0x98,0xd6,0xb3,0xd1,0x98};
static const uint8_t write_values[] = {0,0x20,0,0x28,2,1,0x45,0x1e,0xa4,1,0x47,0x1e,0xa4,1,0x41,0x1c,0xa4};

static volatile uint8_t *reg(uint8_t address)
{
    switch (address) {
#define REGISTER_CASE(name, location) case location: return &name;
        CC2530_REGISTER_LIST(REGISTER_CASE)
#undef REGISTER_CASE
    default: assert(0); return NULL;
    }
}

static void drain(void)
{
    if (read_count) {
        assert(read_count == 1 && reads[0].address == last_address && reads[0].value == last_value);
        read_count = 0;
    }
    assert(!write_count && !xread_count);
}

static uint16_t xaddress(const volatile void *object)
{
    uintptr_t address = (uintptr_t)object;
    if (address >= (uintptr_t)ram && address < (uintptr_t)ram + sizeof(ram))
        return (uint16_t)(address - (uintptr_t)ram);
    if (object == &d) return diag_address;
    if (object == &aes_reserved_end) return 0x1ff;
    if (object == &_gptrput_PARM_2) return 0x1800;
    if (object == aes_dma0) return 0x20;
    if (object == aes_dma1) return 0x28;
    if (object == aes_key) return 0x48;
    if (object == aes_iv) return 0x58;
    if (object == aes_input) return 0x68;
    if (object == aes_output) return 0x78;
    assert(0); return 0;
}

uint32_t host_aes_pointer(const uint8_t *object)
{
    uintptr_t address = (uintptr_t)object, vectors = (uintptr_t)aes_test_vectors;
    if (object == override_pointer) return override_location;
    if (address >= vectors && address < vectors + sizeof(aes_test_vectors))
        return 0x802000UL + (address - vectors);
    return xaddress(object);
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
    if (SOC_DMAARM & 2) assert(!memcmp(fetched[1], (const void *)aes_dma1, 8));
    if (SOC_DMAARM & 1) assert(!memcmp(fetched[0], (const void *)aes_dma0, 8));
    while (budget && received < 16 && !(stall_phase == phase && received == stall_after)) {
        assert((SOC_DMAARM & 1) && (ready & 1));
        assert(fetched[0][2] == 0x70 && fetched[0][3] == 0xb1 && fetched[0][6] == 29);
        SOC_ENCDI = *stage((uint16_t)(source + received));
        if (phase == 1) loaded_key[received] = SOC_ENCDI;
        else if (phase == 2) assert(SOC_ENCDI == 0);
        else loaded_input[received] = SOC_ENCDI;
        received++; input_bytes++; budget--;
        if (received == 16) {
            SOC_DMAARM &= 0xfe; ready &= ~1u;
            if (!(lost_irq & 1)) SOC_DMAIRQ |= 1;
        }
    }
    if (received != 16) return;
    if (!status_posted && enc_phase == phase && enc_delay) { enc_delay--; return; }
    if (phase != 3) {
        if (!status_posted) {
            status_posted = 1;
            if (!(lost_irq & 4)) SOC_S0CON |= (uint8_t)(enc_phase == phase ? enc_value : 3);
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
            if (!(lost_irq & 4)) SOC_S0CON |= (uint8_t)(enc_phase == phase ? enc_value : 3);
        }
    }
    while (budget && sent < 16 && sent != stall_output) {
        assert((SOC_DMAARM & 2) && (ready & 2));
        assert(fetched[1][0] == 0x70 && fetched[1][1] == 0xb2 && fetched[1][6] == 30);
        SOC_ENCDO = cipher[sent];
        *stage((uint16_t)(destination + sent)) = SOC_ENCDO;
        sent++; output_bytes++; budget--;
        if (sent == 16) {
            SOC_DMAARM &= 0xfd; ready &= ~2u;
            if (!(lost_irq & 2)) SOC_DMAIRQ |= 2;
        }
    }
}

static uint8_t load(uint8_t address, uint8_t value)
{
    drain(); accesses++;
    if (extra_clock) { assert(address == 0xc6); extra_clock = 0; }
    else if (address >= 0x95 && address <= 0x97) {
        assert(address == 0x95 + next_tick && (next_read == sizeof(order) || next_read == 6));
        if (!next_tick) {
            latch = (start + ((jump_sample && samples >= jump_sample) ? jump : samples)) & TIMEBASE_TICKS_MASK;
            samples++;
        }
        value = (uint8_t)(latch >> (8 * next_tick));
        next_tick = (next_tick + 1) % 3;
    } else {
        if (address == 0xa8) {
            assert(!next_tick && (!next_read || next_read == sizeof(order) || next_read == 6));
            next_read = 0; observations++;
            if (observations != defer_observation) engine();
            if (observations == mutate_observation) *reg(mutate_address) = mutate_value;
        }
        assert(next_read < sizeof(order) && address == order[next_read++]);
        value = *reg(address);
        if (address == 0xd6 && observations == defer_observation) engine();
    }
    last_address = address; last_value = value;
    return value;
}

static void cycles(uint8_t count)
{
    unsigned ch = pending_arm == 1 ? 0 : 1, i;
    drain();
    assert(count == 9 && pending_arm && !(ready & pending_arm));
    if (!(SOC_DMAARM & pending_arm)) { pending_arm = 0; return; }
    assert(SOC_DMA0CFGL == 0x20 && !SOC_DMA0CFGH && SOC_DMA1CFGL == 0x28 && !SOC_DMA1CFGH);
    memcpy(fetched[ch], (const void *)(ch ? aes_dma1 : aes_dma0), 8);
    assert(fetched[ch][4] == 0 && fetched[ch][5] == 16 && fetched[ch][6] == (ch ? 30 : 29));
    assert(fetched[ch][7] == (ch ? 0x11 : 0x41));
    if (!ch) assert(fetched[ch][0] == 0 && fetched[ch][1] == 0x48 + phase * 16);
    for (i = 8; i < 32; i++) assert(aes_dma1[i] == 0);
    ready |= pending_arm; pending_arm = 0;
}

static void store(uint8_t address, uint8_t before, uint8_t value)
{
    unsigned current = trace++;
    assert(write_count == 1 && writes[0].address == address && writes[0].before == before && writes[0].after == value);
    write_count = 0; drain(); accesses++;
    assert(current < sizeof(write_order) && address == write_order[current]);
    assert(value == (address == 0x98 ? (before & 0xfc) : write_values[current]));
    if (ignore_write && current + 1 == ignore_write) {
        *reg(address) = before;
        if (address == 0xd6) pending_arm = value;
        return;
    }
    if (address == 0xd6) {
        assert(!pending_arm && !(before & value) && !SOC_DMAIRQ && !SOC_DMAREQ);
        SOC_DMAARM = before | value; pending_arm = value;
    } else if (address == 0xb3) {
        assert(ready == 3 && SOC_DMAARM == 3 && !SOC_DMAIRQ && !SOC_DMAREQ && !(SOC_S0CON & 3));
        assert((!phase && !received && !sent) || (phase < 3 && received == 16 && !sent));
        phase++; received = status_posted = 0;
        SOC_ENCCS = (value & 0xf6) | ((phase == 3 ? stale_ready : load_rdy) ? 8 : 0) |
                    (phase == hold_start_phase ? 1 : 0);
    } else if (address == 0xd1) {
        assert(received == 16 && (SOC_S0CON & 3) == 3 && (phase < 3 || (sent == 16 && encrypted)));
        assert(before == (phase < 3 ? 1 : 3));
        SOC_DMAIRQ = before & value;
        /* Completed producers cannot emit more ENC_DW/UP until the next start. */
        if (phase < 3) ready &= ~1u;
    } else if (address == 0x98) {
        assert(received == 16 && !SOC_DMAREQ && !SOC_DMAIRQ && (SOC_S0CON & 3) == 0);
        assert(before == (uint8_t)(value | 3u) && SOC_DMAARM == (phase == 3 ? 0 : 2));
        assert(phase != 3 || (sent == 16 && SOC_ENCCS == 0x48));
        SOC_S0CON = value;
        if (phase == ack_late) { jump_sample = samples; jump = 65536; }
    }
}

static void begin_call(void)
{
    drain();
    accesses = observations = samples = next_read = next_tick = trace = 0;
    phase = received = sent = ready = pending_arm = encrypted = input_bytes = output_bytes = status_posted = 0;
    extra_clock = 1;
}

static void reset_model(void)
{
    /* Independent synthetic full-reset epoch, never production recovery. */
    host_mmio_reset(); aes_fault = aes_used = 0;
    memset(ram, 0xa5, sizeof(ram)); memset(&d, 0xa5, sizeof(d));
    memset((void *)aes_dma0, 0, 8); memset((void *)aes_dma1, 0, 32);
    memset((void *)aes_output, 0x69, 16);
    SOC_CLKCONCMD = SOC_CLKCONSTA = 0xc9; SOC_SLEEPCMD = 4;
    SOC_ENCCS = 8; SOC_S0CON = 0xa4; SOC_IRCON = 0xbe;
    diag_address = 0x700; override_pointer = NULL; override_location = 0;
    start = jump = jump_sample = 0;
    per_poll = 16; stall_phase = 0; stall_after = stall_output = 17;
    ignore_write = mutate_observation = 0; lost_irq = stale_ready = 0; load_rdy = 1;
    finish_delay = hold_start_phase = defer_observation = 0;
    enc_phase = enc_delay = ack_late = 0; enc_value = 3;
    host_mmio_read_hook = load; host_mmio_write_hook = store;
    host_mmio_xaddress_hook = xaddress; host_mmio_cycles_hook = cycles;
    begin_call();
}

static aes_result_t call(const uint8_t *key, const uint8_t *input, uint8_t *output, uint32_t timeout, uint16_t cap)
{
    aes_result_t result;
    operations++;
    result = aes128_encrypt_block(key, input, output, timeout, cap, &d);
    drain();
    return result;
}

static void verify_success(const uint8_t *key, const uint8_t *input, uint8_t *out)
{
    uint8_t expected[16], k[16], in[16];
    uint8_t enc_upper = SOC_S0CON;
    memcpy(k, key, 16); memcpy(in, input, 16); memcpy(saved_ram, ram, sizeof(ram));
    aes_reference_encrypt(k, in, expected);
    assert(call(key, input, out, 1000, 128) == AES_OK);
    assert(!memcmp(out, expected, 16));
    memcpy(saved_ram + (out - ram), expected, 16);
    assert(!memcmp(ram, saved_ram, sizeof(ram)));
    assert(!memcmp(k, (const void *)aes_key, 16) && !memcmp(in, (const void *)aes_input, 16));
    assert(!memcmp(k, loaded_key, 16) && !memcmp(in, loaded_input, 16));
    assert(d.phase == 4 && d.submitted == 7 && d.input_complete == 7 && d.output_drained == 1 && d.published == 1);
    assert(d.configured == 3 && d.arms == 4 && d.ack_issued == 3 && d.dma_acked == 7 && d.enc_ack_issued == 3 && d.enc_acked == 7);
    assert(d.sample_valid == 3 && !d.arm && !d.request && !d.irq && d.control == 0x48 && d.enc_flags == enc_upper);
    assert(trace == 17 && input_bytes == 48 && output_bytes == 16 && !aes_fault && aes_used);
    assert(SOC_IRCON == 0xbe && !SOC_IEN0 && !SOC_IEN1 && !SOC_IEN2 && !SOC_DMAREQ);
}

static void verify_fault(aes_result_t expected, uint32_t timeout, uint16_t cap)
{
    uint8_t internal[104], saved_internal[104];
    aes_diagnostics_t saved_d;
    unsigned old_accesses, old_trace;
    memcpy(saved_ram, ram, sizeof(ram));
    assert(call(ram + 0x400, ram + 0x500, ram + 0x600, timeout, cap) == expected);
    assert(!memcmp(saved_ram, ram, sizeof(ram)) && !d.published && aes_fault == expected);
    memcpy(&saved_d, &d, sizeof(d));
    memcpy(saved_internal, (const void *)aes_dma0, 8); memcpy(saved_internal + 8, (const void *)aes_dma1, 32);
    memcpy(saved_internal + 40, (const void *)aes_key, 16); memcpy(saved_internal + 56, (const void *)aes_iv, 16);
    memcpy(saved_internal + 72, (const void *)aes_input, 16); memcpy(saved_internal + 88, (const void *)aes_output, 16);
    old_accesses = accesses; old_trace = trace;
    assert(call(NULL, NULL, NULL, 0, 0) == expected);
    assert(call(ram + 0x400, ram + 0x500, ram + 0x600, 1000, 128) == expected);
    memcpy(internal, (const void *)aes_dma0, 8); memcpy(internal + 8, (const void *)aes_dma1, 32);
    memcpy(internal + 40, (const void *)aes_key, 16); memcpy(internal + 56, (const void *)aes_iv, 16);
    memcpy(internal + 72, (const void *)aes_input, 16); memcpy(internal + 88, (const void *)aes_output, 16);
    assert(accesses == old_accesses && trace == old_trace && !memcmp(&d, &saved_d, sizeof(d)));
    assert(!memcmp(internal, saved_internal, sizeof(internal)));
}

static void invalid(aes_result_t expected, const uint8_t *key, const uint8_t *input,
                    uint8_t *out, uint32_t timeout, uint16_t cap)
{
    aes_diagnostics_t saved;
    memcpy(&saved, &d, sizeof(d)); memcpy(saved_ram, ram, sizeof(ram));
    assert(call(key, input, out, timeout, cap) == expected);
    assert(!accesses && !aes_fault && !memcmp(&saved, &d, sizeof(d)) && !memcmp(saved_ram, ram, sizeof(ram)));
}

int main(void)
{
    unsigned i, j, variant;
    aes_reference_check();
    for (i = 0; i < 5; i++) for (variant = 0; variant < 4; variant++) {
        reset_model();
        memcpy(ram + 0x400, aes_test_vectors[i][0], 16); memcpy(ram + 0x500, aes_test_vectors[i][1], 16);
        verify_success(variant & 1 ? aes_test_vectors[i][0] : ram + 0x400,
                       variant & 2 ? aes_test_vectors[i][1] : ram + 0x500, ram + 0x600);
        assert(!memcmp(ram + 0x600, aes_test_vectors[i][2], 16));
    }
    for (i = 0; i < 16; i++) for (j = 0; j < 256; j++) {
        reset_model();
        for (variant = 0; variant < 16; variant++) {
            ram[0x400 + variant] = (uint8_t)(variant * 29 + i);
            ram[0x500 + variant] = (uint8_t)(variant * 17 + j);
        }
        ram[0x400 + i] ^= (uint8_t)j;
        if (j & 1) SOC_CLKCONCMD = SOC_CLKCONSTA = 0x88;
        load_rdy = j & 1; stale_ready = (j >> 1) & 1;
        verify_success(ram + 0x400, ram + 0x500, ram + 0x600);
    }
    reset_model(); per_poll = 1; start = 0xfffff8;
    verify_success(ram + 0x400, ram + 0x500, ram + 0x600);
    start = (start + samples + 1) & TIMEBASE_TICKS_MASK;
    begin_call(); verify_success(ram + 0x400, ram + 0x500, ram + 0x600);
    reset_model();
    for (i = 0; i < 257; i++) {
        if (i) { start = (start + samples + 1) & TIMEBASE_TICKS_MASK; begin_call(); }
        for (j = 0; j < 16; j++) {
            ram[0x400+j] = (uint8_t)(i*19 + j*31);
            ram[0x500+j] = (uint8_t)(i*37 + j*13);
        }
        verify_success(ram + 0x400, ram + 0x500, ram + 0x600);
    }
    reset_model(); finish_delay = 4; verify_success(ram + 0x400, ram + 0x500, ram + 0x600);
    for (i = 6; i <= 17; i += 11) {
        reset_model(); defer_observation = i;
        verify_success(ram + 0x400, ram + 0x500, ram + 0x600);
    }
    for (i = 0; i < 32; i++) {
        reset_model(); verify_success(ram + 0x400, ram + 0x500, ram + 0x4f0 + i);
    }
    for (i = 0; i < 256; i += 4) {
        reset_model(); SOC_S0CON = (uint8_t)i;
        verify_success(ram + 0x400, ram + 0x500, ram + 0x600);
    }
    for (i = 0; i < 4; i++) {
        reset_model();
        invalid(AES_INVALID_ARGUMENT, i == 0 ? NULL : ram + 0x400, i == 1 ? NULL : ram + 0x500,
                i == 2 ? NULL : ram + 0x600, i == 3 ? 0 : 100, 128);
    }
    reset_model(); invalid(AES_INVALID_ARGUMENT, ram + 0x400, ram + 0x500, ram + 0x600, 100, 0);
    for (i = 23; i < 32; i++) {
        reset_model(); invalid(AES_INVALID_ARGUMENT, ram + 0x400, ram + 0x500, ram + 0x600, 1UL << i, 128);
    }
    for (i = 0; i < 0x10000; i++) {
        aes_result_t expected = i >= 0x1df1 ? AES_INVALID_RANGE :
            (i <= 0x1ff || (i >= 0x17f1 && i <= 0x1800) || (i >= 0x5f1 && i <= 0x60f) ||
             (i >= 0x6f1 && i < 0x700 + sizeof(d))) ? AES_BUFFER_OWNERSHIP : AES_OK;
        if (expected == AES_OK) continue;
        reset_model(); override_pointer = ram + 0x400; override_location = i;
        invalid(expected, ram + 0x400, ram + 0x500, ram + 0x600, 100, 128);
    }
    for (i = 1; i < 256; i++) if (i != 0x80) {
        reset_model(); override_pointer = ram + 0x400; override_location = ((uint32_t)i << 16) | 0x400;
        invalid(AES_INVALID_RANGE, ram + 0x400, ram + 0x500, ram + 0x600, 100, 128);
    }
    reset_model(); override_pointer = ram + 0x400; override_location = 0x807ff1;
    invalid(AES_INVALID_RANGE, ram + 0x400, ram + 0x500, ram + 0x600, 100, 128);
    for (i = 0; i < 0x10000; i++) {
        aes_result_t expected = i >= 0x1df1 ? AES_INVALID_RANGE :
            (i <= 0x1ff || (i >= 0x17f1 && i <= 0x1800) ||
             (i >= 0x6f1 && i < 0x700 + sizeof(d))) ? AES_BUFFER_OWNERSHIP : AES_OK;
        if (expected == AES_OK) continue;
        reset_model(); override_pointer = ram + 0x500; override_location = i;
        invalid(expected, ram + 0x400, ram + 0x500, ram + 0x600, 100, 128);
    }
    for (i = 0; i < 0x10000; i++) {
        aes_result_t expected = i >= 0x1df1 ? AES_INVALID_RANGE :
            (i <= 0x1ff || (i >= 0x17f1 && i <= 0x1800) || (i >= 0x3f1 && i <= 0x40f) ||
             (i >= 0x6f1 && i < 0x700 + sizeof(d))) ? AES_BUFFER_OWNERSHIP : AES_OK;
        if (expected == AES_OK) continue;
        reset_model(); invalid(expected, ram + 0x400, ram + 0x500, ram + i, 100, 128);
    }
    for (i = 0; i < 0x10000; i++) {
        aes_result_t expected = i > 0x1e00 - sizeof(d) ? AES_INVALID_RANGE :
            (i <= 0x1ff || (i > 0x1800 - sizeof(d) && i <= 0x1800) ||
             (i > 0x400 - sizeof(d) && i <= 0x40f) ||
             (i > 0x500 - sizeof(d) && i <= 0x50f) ||
             (i > 0x600 - sizeof(d) && i <= 0x60f)) ? AES_BUFFER_OWNERSHIP : AES_OK;
        if (expected == AES_OK) continue;
        reset_model(); diag_address = (uint16_t)i;
        invalid(expected, ram + 0x400, ram + 0x500, ram + 0x600, 100, 128);
    }
    reset_model();
    assert(aes128_encrypt_block(ram + 0x400, ram + 0x500, ram + 0x600, 100, 128, NULL) == AES_INVALID_ARGUMENT);
    assert(!accesses && !aes_fault);
    for (i = 1; i <= 17; i++) {
        /* Writes of zero to already-zero CFG high bytes cannot be distinguished
         * from an accepted write; all consequential ignored writes must fail.
         */
        if (i == 1 || i == 3) continue;
        reset_model(); ignore_write = i; verify_fault(AES_STATE_CHANGED, 100, 128);
    }
    for (i = 1; i <= 3; i++) for (j = 0; j < 16; j++) {
        reset_model(); stall_phase = i; stall_after = j;
        verify_fault(AES_TIMEOUT, 40, 128);
        assert(phase == i && received == j && d.submitted == (1u << i) - 1u && !d.output_drained);
        assert(d.input_complete == (1u << (i - 1)) - 1u);
    }
    for (i = 0; i < 16; i++) {
        reset_model(); stall_output = i; verify_fault(AES_TIMEOUT, 40, 128);
        assert(received == 16 && sent == i && d.input_complete == 7 && !d.output_drained);
        /* A late PRIVATE effect is not release, success, or implicit recovery. */
        stall_output = 17; engine();
        assert(sent == 16 && !memcmp(ram, saved_ram, sizeof(ram)) && aes_fault == AES_TIMEOUT);
        variant = accesses;
        assert(call(ram + 0x400, ram + 0x500, ram + 0x600, 1000, 128) == AES_TIMEOUT);
        assert(accesses == variant && !d.published);
    }
    for (i = 1; i <= 8; i <<= 1) {
        reset_model(); lost_irq = (uint8_t)i; verify_fault(AES_TIMEOUT, 40, 128);
    }
    for (i = 1; i <= 3; i++) {
        reset_model(); hold_start_phase = i; verify_fault(AES_TIMEOUT, 40, 128);
    }
    for (i = 1; i < 18; i++) { reset_model(); verify_fault(AES_POLL_LIMIT, 100, (uint16_t)i); }
    reset_model();
    assert(call(ram + 0x400, ram + 0x500, ram + 0x600, 0x7fffff, 18) == AES_OK && d.polls == 18 && d.published);
    for (i = 1; i <= 18; i++) {
        reset_model(); verify_fault(AES_TIMEOUT, i, 128);
        if (i == 16) assert(d.input_complete == 7 && d.output_drained && d.ack_issued == 2 && d.dma_acked == 3 && SOC_DMAIRQ == 3);
    }
    reset_model(); stall_phase = 1; stall_after = 0; jump_sample = 1; jump = 0;
    verify_fault(AES_POLL_LIMIT, 0x7fffff, 65535);
    assert(d.polls == 65535 && !d.elapsed_ticks && d.submitted == 1);
    reset_model(); jump_sample = 1; jump = 0x800064; verify_fault(AES_TIMEBASE_ERROR, 100, 128);
    reset_model(); jump_sample = 2; jump = 0; verify_fault(AES_COUNTER_RANGE, 100, 128);
    for (i = 0; i < sizeof(order); i++) for (j = 0; j < 8; j++) {
        uint8_t address = order[i], value;
        if (address == 0xb3 || address == 0x98 || address == 0xd6 || address == 0xd7 ||
            address == 0xd1 || address == 0xd4 || address == 0xd5 || address == 0xd2 || address == 0xd3)
            continue;
        reset_model(); value = *reg(address) ^ (1u << j);
        if (address == 0xbe && j >= 3) continue;
        mutate_observation = 8; mutate_address = address; mutate_value = value;
        verify_fault(address == 0xc0 ? AES_STATE_CHANGED : AES_UNSUPPORTED_STATE, 100, 128);
    }
    for (i = 2; i <= 19; i++) for (j = 6; j < sizeof(order); j++) {
        reset_model(); mutate_observation = i; mutate_address = order[j];
        mutate_value = order[j] == 0xb3 ? 0x80 : order[j] == 0x98 ? 0xb4 :
            order[j] == 0xd6 || order[j] == 0xd1 ? 4 : order[j] == 0xd7 ? 1 : 0xdd;
        verify_fault(AES_STATE_CHANGED, 100, 128);
    }
    for (i = 1; i <= 3; i++) {
        for (j = 0; j < 3; j++) {
            reset_model(); enc_phase = i; enc_value = j;
            verify_fault(j ? AES_STATE_CHANGED : AES_TIMEOUT, 40, 128);
            assert(d.phase == i && d.enc_acked == (1u << (i-1))-1u);
        }
        reset_model(); enc_phase = i; enc_delay = 4;
        verify_success(ram + 0x400, ram + 0x500, ram + 0x600);
        reset_model(); ack_late = i; verify_fault(AES_TIMEOUT, 100, 128);
        assert(d.enc_ack_issued == i && d.enc_acked == (1u << (i-1))-1u);
        reset_model(); mutate_observation = i == 1 ? 5 : i == 2 ? 9 : 14;
        mutate_address = 0x98; mutate_value = 0xa7;
        verify_fault(AES_STATE_CHANGED, 100, 128);
        assert(d.submitted == (1u << (i-1))-1u);
    }
    for (i = 0; i < 256; i++) {
        reset_model(); SOC_ENCCS = (uint8_t)i;
        if (i != 8) verify_fault(!(i & 8) || (i & 1) ? AES_BUSY : AES_UNSUPPORTED_STATE, 100, 128);
        if (i) {
            reset_model(); SOC_DMAARM = (uint8_t)i; verify_fault(AES_BUSY, 100, 128);
            reset_model(); SOC_DMAREQ = (uint8_t)i; verify_fault(AES_PENDING, 100, 128);
            reset_model(); SOC_DMAIRQ = (uint8_t)i; verify_fault(AES_PENDING, 100, 128);
        }
        if (i & 3) {
            reset_model(); SOC_S0CON = (uint8_t)i; verify_fault(AES_PENDING, 100, 128);
        }
    }
    printf("AES host: %u real-driver operations; independent primary KAT/reference and checked descriptor/alias/log model PASS\n", operations);
    return 0;
}
#endif
