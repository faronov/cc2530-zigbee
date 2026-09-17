/* SPDX-License-Identifier: BSD-3-Clause */
#include "aes_fixture.h"
#include "aes_reference.h"
#include "host_mmio.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

extern volatile uint8_t aes_dma0[8], aes_dma1[32], aes_key[16], aes_iv[16], aes_input[16], aes_output[16];
extern uint8_t aes_fault, aes_used, aes_reserved_end;
uint8_t _gptrput_PARM_2;
static uint8_t fetched[2][8], key[16], input[16], encrypted[16], last_reg, last_value, booting;
static unsigned ready, pending, phase, downloaded, uploaded, copies, inputs, outputs, arms, acknowledgments, enc_acks, clocks;
static unsigned accesses, mode, stop_phase, stop_byte, corruption, bad_owner, delay_flags, posted, tests, ack_phase, enc_phase, enc_value;
static uint32_t ticks, latch, step_ticks;
static uint8_t baseline[32];

static void consume(void)
{
    if (read_count) {
        assert(read_count == 1 && reads[0].address == last_reg && reads[0].value == last_value);
        read_count = 0;
    }
    assert(!xread_count);
}
static uint16_t address(const volatile void *object)
{
    uintptr_t p = (uintptr_t)object;
    if (object == &aes_reserved_end) return 0xfa;
    if (object == &_gptrput_PARM_2) return 0x1af;
    if (object == aes_dma0) return 0x45;
    if (object == aes_dma1) return 0x4d;
    if (object == aes_key) return 0x6d;
    if (object == aes_iv) return 0x7d;
    if (object == aes_input) return 0x8d;
    if (object == aes_output) return 0x9d;
    if (object == &aes_fixture_work) return 0x16d;
    if (object == aes_fixture_key) return bad_owner ? 0x45 : 0x13b;
    if (object == aes_fixture_input) return 0x14b;
    if (p >= (uintptr_t)aes_fixture_output && p < (uintptr_t)aes_fixture_output + 18)
        return 0x15b + (uint16_t)(p - (uintptr_t)aes_fixture_output);
    assert(0); return 0;
}
uint32_t host_aes_pointer(const uint8_t *p)
{
    uintptr_t n = (uintptr_t)p, base = (uintptr_t)aes_fixture_vectors;
    if (n >= base && n < base + sizeof(aes_fixture_vectors)) return 0x803000UL + n - base;
    return address(p);
}
static volatile uint8_t *stage(uint16_t a)
{
    if (a >= 0x6d && a < 0x7d) return aes_key + a - 0x6d;
    if (a >= 0x7d && a < 0x8d) return aes_iv + a - 0x7d;
    if (a >= 0x8d && a < 0x9d) return aes_input + a - 0x8d;
    assert(a >= 0x9d && a < 0xad); return aes_output + a - 0x9d;
}
static void engine(void)
{
    uint16_t src = (uint16_t)fetched[0][0]*256 + fetched[0][1];
    uint16_t dst = (uint16_t)fetched[1][2]*256 + fetched[1][3];
    unsigned budget = 16;
    if (!phase) return;
    if (SOC_DMAARM & 1) assert(!memcmp(fetched[0], (const void *)aes_dma0, 8));
    if (SOC_DMAARM & 2) assert(!memcmp(fetched[1], (const void *)aes_dma1, 8));
    while (budget && downloaded < 16 && !(phase == stop_phase && downloaded == stop_byte)) {
        assert((SOC_DMAARM & 1) && (ready & 1));
        SOC_ENCDI = *stage(src + downloaded);
        if (phase == 1) key[downloaded] = SOC_ENCDI;
        else if (phase == 2) assert(!SOC_ENCDI);
        else input[downloaded] = SOC_ENCDI;
        downloaded++; inputs++; budget--;
        if (downloaded == 16) { SOC_DMAARM &= 0xfe; if (mode != 6) SOC_DMAIRQ |= 1; ready &= ~1u; }
    }
    if (downloaded != 16) return;
    if (phase != 3) {
        if (!posted) { posted = 1; if (mode != 7) SOC_S0CON |= (uint8_t)(enc_phase == phase ? enc_value : 3); }
        return;
    }
    if (!posted) {
        aes_reference_encrypt(key, input, encrypted);
        if (delay_flags) delay_flags--;
        else { posted = 1; if (mode != 8) SOC_ENCCS |= 8; if (mode != 7) SOC_S0CON |= (uint8_t)(enc_phase == phase ? enc_value : 3); }
    }
    while (budget && uploaded < 16 && !(stop_phase == 4 && uploaded == stop_byte)) {
        assert((SOC_DMAARM & 2) && (ready & 2));
        SOC_ENCDO = encrypted[uploaded]; *stage(dst + uploaded) = SOC_ENCDO;
        uploaded++; outputs++; budget--;
        if (uploaded == 16) { SOC_DMAARM &= 0xfd; if (mode != 9) SOC_DMAIRQ |= 2; ready &= ~2u; }
    }
}
static uint8_t load(uint8_t r, uint8_t value)
{
    consume(); accesses++;
    assert(r != 0xb1 && r != 0xb2);
    if (r == 0xa8) engine();
    if (r == 0x95) { latch = ticks; ticks = (ticks + step_ticks) & 0xffffff; }
    if (r >= 0x95 && r <= 0x97) value = (uint8_t)(latch >> ((r - 0x95)*8));
    if (r == 0xb3) value = SOC_ENCCS;
    if (r == 0x98) value = SOC_S0CON;
    if (r == 0xd6) value = SOC_DMAARM;
    if (r == 0xd1) value = SOC_DMAIRQ;
    if (r == 0xbf && corruption && enc_acks == 3) {
        unsigned i = corruption - 1;
        uint8_t *p = i < 16 ? aes_fixture_key : i < 32 ? aes_fixture_input : aes_fixture_output;
        p[i < 16 ? i : i < 32 ? i - 16 : i - 32] ^= 1;
        corruption = 0;
    }
    last_reg = r; last_value = value; return value;
}
static void cycles(uint8_t n)
{
    unsigned ch = pending == 1 ? 0 : 1, i;
    consume(); assert(n == 9 && pending && !(ready & pending));
    memcpy(fetched[ch], (const void *)(ch ? aes_dma1 : aes_dma0), 8);
    assert(fetched[ch][4] == 0 && fetched[ch][5] == 16 && fetched[ch][6] == (ch ? 30 : 29) &&
           fetched[ch][7] == (ch ? 0x11 : 0x41));
    assert(ch ? fetched[ch][0] == 0x70 && fetched[ch][1] == 0xb2 :
                fetched[ch][2] == 0x70 && fetched[ch][3] == 0xb1);
    for (i = 8; i < 32; i++) assert(!aes_dma1[i]);
    ready |= pending; pending = 0;
}
static void store(uint8_t r, uint8_t old, uint8_t value)
{
    consume(); accesses++;
    assert(write_count == 1 && writes[0].address == r && writes[0].before == old && writes[0].after == value);
    write_count = 0;
    if (booting) return;
    if (r == 0xc6) {
        assert(!aes_fault && !SOC_DMAARM && !SOC_DMAREQ && !SOC_DMAIRQ);
        clocks++; if (mode != 1) SOC_CLKCONSTA = value;
        if (mode == 11) step_ticks = 1;
    } else if (r == 0xd4 || r == 0xd5 || r == 0xd2 || r == 0xd3) {
        assert(!aes_fault && !SOC_DMAARM && !SOC_DMAIRQ && !SOC_DMAREQ);
        if (mode == 10) SOC_DMA0CFGL = 0;
    } else if (r == 0xd6) {
        assert(value == 1 || value == 2); assert(!(old & value) && !SOC_DMAIRQ && !SOC_DMAREQ && !aes_fault);
        SOC_DMAARM = old | value; pending = value; arms++;
    } else if (r == 0xb3) {
        assert(ready == 3 && SOC_DMAARM == 3 && !SOC_DMAIRQ && !SOC_DMAREQ);
        if (value == 0x45) { phase = 1; copies++; uploaded = posted = 0; }
        else { assert(value == (phase == 1 ? 0x47 : 0x41)); phase++; }
        downloaded = posted = 0; SOC_ENCCS = (value & 0xf6) | (phase != 3 ? 8 : 0);
        if (mode == 3 && phase == stop_phase) SOC_ENCCS |= 1;
        if (mode == 4 && phase == stop_phase) ticks += 65536;
        if (mode == 5) SOC_DMAREQ = 1;
    } else if (r == 0xd1) {
        assert(downloaded == 16 && value == (phase == 3 ? 0x1c : 0x1e));
        SOC_DMAIRQ = old & value; acknowledgments++;
    } else {
        assert(r == 0x98 && downloaded == 16 && SOC_DMAARM == (phase == 3 ? 0 : 2) && !SOC_DMAIRQ);
        assert(phase != 3 || uploaded == 16);
        SOC_S0CON = value; enc_acks++;
        if (mode == 12 && phase == 3) ticks += 65536;
        if (phase == ack_phase) {
            if (mode == 13) SOC_S0CON = old;
            else if (mode == 14) ticks += 65536;
        }
        if (phase == 3) phase = 0;
    }
}
static void prepare(void)
{
    host_mmio_reset(); aes_fault = aes_used = 0;
    memset((void *)aes_dma0, 0, 8); memset((void *)aes_dma1, 0, 32);
    ready = pending = phase = downloaded = uploaded = copies = inputs = outputs = arms = acknowledgments = enc_acks = clocks = 0;
    mode = stop_phase = corruption = bad_owner = delay_flags = posted = accesses = ack_phase = enc_phase = 0;
    stop_byte = 17; enc_value = 3;
    ticks = 0xfffff8; step_ticks = 1;
    SOC_CLKCONCMD = SOC_CLKCONSTA = 0xc9; SOC_SLEEPCMD = 0x84; SOC_ENCCS = 8; SOC_S0CON = 0xa4; SOC_IRCON = 0xa0;
    AEF_IP0 = 0x31; AEF_IP1 = 0xe; AEF_TCON = AEF_S1CON = 3;
    AEF_RFIRQF0 = 0x55; AEF_RFIRQF1 = 0xaa; AEF_IRCON2 = 0x12; SOC_RFERRF = 0x37;
    host_mmio_read_hook = load; host_mmio_write_hook = store; host_mmio_cycles_hook = cycles; host_mmio_xaddress_hook = address;
    booting = 1; _sdcc_external_startup(); booting = 0;
    aes_fixture_initialize(); consume(); assert(aes_fixture_state.phase == AEF_INIT);
    memcpy(baseline, (const void *)&m0_status, 32);
}
static void step(uint8_t stage_, uint8_t phase_)
{
    uint8_t saved[AEF_SIZE], buffers[50], descriptors[40], diagnostic[sizeof(aes_fixture_work_t)];
    unsigned before;
    tests++; aes_fixture_step(); consume();
    if (aes_fixture_state.stage != stage_ || aes_fixture_state.phase != phase_)
        fprintf(stderr, "step %u mode %u stop %u/%u: stage/phase %u/%u expected %u/%u reason %u result %u\n",
                tests, mode, stop_phase, stop_byte, aes_fixture_state.stage, aes_fixture_state.phase,
                stage_, phase_, aes_fixture_state.reason, aes_fixture_state.result);
    assert(aes_fixture_state.stage == stage_ && aes_fixture_state.phase == phase_);
    assert(!memcmp((const void *)&m0_status, baseline, 8) && !memcmp((const uint8_t *)&m0_status + 9, baseline + 9, 23) &&
           m0_status.heartbeat == aes_fixture_state.completed);
    if (phase_ != AEF_FAULT) return;
    before = accesses; memcpy(saved, (const void *)&aes_fixture_state, AEF_SIZE);
    memcpy(buffers, aes_fixture_key, 16); memcpy(buffers + 16, aes_fixture_input, 16); memcpy(buffers + 32, aes_fixture_output, 18);
    memcpy(descriptors, (const void *)aes_dma0, 8); memcpy(descriptors + 8, (const void *)aes_dma1, 32);
    memcpy(diagnostic, &aes_fixture_work, sizeof(diagnostic));
    aes_fixture_step(); consume();
    assert(accesses == before && !memcmp(saved, (const void *)&aes_fixture_state, AEF_SIZE) &&
           !memcmp(buffers, aes_fixture_key, 16) && !memcmp(buffers + 16, aes_fixture_input, 16) &&
           !memcmp(buffers + 32, aes_fixture_output, 18) && !memcmp(descriptors, (const void *)aes_dma0, 8) &&
           !memcmp(descriptors + 8, (const void *)aes_dma1, 32) && !memcmp(diagnostic, &aes_fixture_work, sizeof(diagnostic)));
}
int main(void)
{
    unsigned c, i, j, m, seen[2][21][4] = {{{0}}};
    uint8_t out[16];
    aes_reference_check();
    for (c = 0; c < 21; c++) {
        if (c >= 5) for (i = 0; i < 16; i++) {
            assert(aes_fixture_vectors[c][i] == (uint8_t)((c-5)*19+i*31));
            assert(aes_fixture_vectors[c][16+i] == ((uint8_t)((c-5)*37+i*13) ^ 0xa7));
        }
        aes_reference_encrypt(aes_fixture_vectors[c], aes_fixture_vectors[c] + 16, out);
        assert(!memcmp(out, aes_fixture_vectors[c] + 32, 16) && !aes_fixture_vectors[c][48]);
    }
    prepare(); step(0, AEF_READY);
    for (c = 0; c < 257; c++) for (i = 1; i <= 4; i++) {
        step(i, AEF_READY);
        if (i == 1 || i == 3) {
            unsigned v = (c & 255)%21, space = (((c & 255)/21) + (i == 3)) & 3;
            assert(aes_fixture_state.vector == v && aes_fixture_state.spaces == space && aes_fixture_state.checked == 50);
            assert(!memcmp(aes_fixture_output + 1, aes_fixture_vectors[v] + 32, 16));
            seen[i == 3][v][space]++;
        }
    }
    for (c = 0; c < 2; c++) for (i = 0; i < 21; i++) for (j = 0; j < 4; j++) assert(seen[c][i][j]);
    assert(copies == 514 && inputs == 24672 && outputs == 8224 && arms == 2056 && acknowledgments == 1542 && enc_acks == 1542 && clocks == 514);
    for (i = 1; i <= 50; i++) {
        prepare(); step(0, AEF_READY); corruption = i; step(1, AEF_FAULT);
        assert(aes_fixture_state.reason == AEF_BYTES && !aes_fixture_state.result && aes_fixture_state.checked == i-1);
    }
    for (c = 1; c <= 4; c++) for (i = 0; i < 16; i++) {
        prepare(); step(0, AEF_READY); stop_phase = c; stop_byte = i; step_ticks = 1024;
        step(1, AEF_FAULT); assert(aes_fixture_state.result == AES_TIMEOUT && aes_fixture_state.checked == 50);
        stop_phase = 0; engine(); step(1, AEF_FAULT);
    }
    for (m = 3; m <= 12; m++) {
        prepare(); step(0, AEF_READY); mode = m; stop_phase = 1; step_ticks = 1024;
        if (m == 11) { step(1, AEF_READY); step_ticks = 65536; step(2, AEF_FAULT); assert(aes_fixture_state.result == CLOCK_TIMEOUT); }
        else {
            step(1, AEF_FAULT);
            assert(aes_fixture_state.result == (m == 5 || m == 10 ? AES_STATE_CHANGED : AES_TIMEOUT));
            assert(aes_fixture_state.checked == 50);
        }
    }
    prepare(); step(0, AEF_READY); stop_phase = 1; stop_byte = 0; step_ticks = 0;
    step(1, AEF_FAULT); assert(aes_fixture_state.result == AES_POLL_LIMIT);
    prepare(); step(0, AEF_READY); bad_owner = 1; step(1, AEF_FAULT); assert(aes_fixture_state.result == AES_BUFFER_OWNERSHIP && !copies);
    for (i = 0; i < 3; i++) {
        prepare(); step(0, AEF_READY);
        if (i == 0) SOC_IEN0 = 0x10;
        else if (i == 1) SOC_IEN1 = 1;
        else SOC_IEN2 = 1;
        step(1, AEF_FAULT);
        assert(aes_fixture_state.reason == AEF_INVARIANT && aes_fixture_state.result == 255 && !copies);
    }
    prepare(); step(0, AEF_READY); step(1, AEF_READY); mode = 1; step_ticks = 65536;
    step(2, AEF_FAULT); assert(aes_fixture_state.result == CLOCK_TIMEOUT && aes_fixture_state.diagnostic[18] == CLOCK_ROLLBACK_UNCONFIRMED);
    prepare(); step(0, AEF_READY); step(1, AEF_READY); mode = 1; step_ticks = 0;
    step(2, AEF_FAULT); assert(aes_fixture_state.result == CLOCK_POLL_LIMIT);
    for (c = 1; c <= 3; c++) {
        for (i = 0; i < 3; i++) {
            prepare(); step(0, AEF_READY); enc_phase = c; enc_value = i; step_ticks = 1024;
            step(1, AEF_FAULT);
            assert(aes_fixture_state.result == (i ? AES_STATE_CHANGED : AES_TIMEOUT));
            assert(aes_fixture_state.checked == 50 && !aes_fixture_work.aes.published);
        }
        for (m = 13; m <= 14; m++) {
            prepare(); step(0, AEF_READY); mode = m; ack_phase = c;
            step(1, AEF_FAULT);
            assert(aes_fixture_state.result == (m == 13 ? AES_STATE_CHANGED : AES_TIMEOUT));
            assert(aes_fixture_work.aes.enc_ack_issued == c && aes_fixture_work.aes.enc_acked == (1u << (c-1))-1u);
        }
    }
    printf("AES fixture: %u steps, 257 cycles/514 AES calls/32896 DMA bytes, 21 vectors x 4 spaces x 2 clocks; 50 corruptions and terminal partial/late/clock/ownership faults PASS\n", tests);
    return 0;
}
