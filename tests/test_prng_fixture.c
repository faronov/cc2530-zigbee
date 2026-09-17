/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "prng_fixture.h"
#include "host_mmio.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

extern uint8_t prng_fault, prng_seeded, prng_reserved_end;
static uint16_t lfsr;
static unsigned loads, stores, commands, seeds, stopped, clocks, mode, booting, at, steps;
static unsigned stif_command, stif_stop;
static uint32_t ticks, latched;
static uint8_t last_reg, last_value, seen[65536], initial[32];

static uint16_t reference(uint16_t value)
{
    uint8_t bits[16], next[16];
    unsigned i, j;
    for (i = 0; i < 16; i++) bits[i] = (uint8_t)((value >> i) & 1u);
    for (j = 0; j < 13; j++) {
        next[0] = bits[15];
        for (i = 1; i < 16; i++) next[i] = bits[i-1];
        next[2] ^= bits[15]; next[15] ^= bits[15];
        memcpy(bits, next, sizeof(bits));
    }
    value = 0;
    for (i = 0; i < 16; i++) value |= (uint16_t)((uint16_t)bits[i] << i);
    return value;
}

static void consume(void)
{
    if (read_count) {
        assert(read_count == 1 && reads[0].address == last_reg && reads[0].value == last_value);
        read_count = 0;
    }
    assert(!write_count && !xread_count);
}

static void sync_state(void)
{
    SOC_RNDL = (uint8_t)lfsr; SOC_RNDH = (uint8_t)(lfsr >> 8);
}

static uint8_t load(uint8_t reg, uint8_t value)
{
    consume(); loads++;
    assert(reg != 0xbb && reg != 0xba && !(reg >= 0xd1 && reg <= 0xd7) && reg != 0xd9 && reg != 0xe1);
    if (reg == 0x95) { latched = ticks; ticks = (ticks + 1) & 0xffffff; }
    if (reg >= 0x95 && reg <= 0x97) value = (uint8_t)(latched >> (8u * (reg - 0x95)));
    last_reg = reg; last_value = value;
    return value;
}

static void store(uint8_t reg, uint8_t before, uint8_t value)
{
    assert(write_count == 1 && writes[0].address == reg && writes[0].before == before && writes[0].after == value);
    write_count = 0; consume(); stores++;
    if (booting) return;
    assert(!prng_fault);
    if (reg == 0xc6) { SOC_CLKCONSTA = value; clocks++; return; }
    if (reg == 0xbc) {
        seeds++;
        if (mode == 1 && seeds == at) { SOC_RNDL = before; return; }
        lfsr = (uint16_t)((uint16_t)(lfsr & 255) << 8) | value;
        sync_state(); return;
    }
    assert(reg == 0xb4 && (value == 0x37 || value == 0x3f));
    assert((before & 0x7f) == 0x33);
    SOC_ADCCON1 = (before & 0x80) | value;
    if (value == 0x3f) {
        if (stif_stop) SOC_IRCON |= 0x80;
        stopped++; return;
    }
    commands++;
    if (commands == stif_command) SOC_IRCON |= 0x80;
    if (mode == 2) return;
    if (mode == 3) { SOC_ADCCON1 = before; return; }
    lfsr = reference(lfsr); sync_state(); SOC_ADCCON1 = before;
    if (mode == 4) SOC_ADCCON1 ^= 0x80;
    if (mode == 5) SOC_ADCCON1 |= 0x40;
    if (mode == 6) SOC_ADCCON1 |= 8;
    if (mode == 7) SOC_IEN0 = 1;
    if (mode == 8) SOC_CLKCONCMD = 0x88;
}

static uint16_t address(const volatile void *p)
{
    unsigned i;
    if (p == &prng_reserved_end) return 0x100;
    if (p == &prng_fixture_probe_output) return 0x300;
    for (i = 0; i < PNF_WORDS; i++)
        if (p == &prng_fixture_buffer.words[i])
            return mode == 9 ? 0x100 : mode == 10 ? 0x1dff : (uint16_t)(0x202 + 2*i);
    assert(0); return 0;
}

static unsigned word(const volatile uint8_t *p)
{
    return p[0] | ((unsigned)p[1] << 8);
}

static void prepare(uint8_t eoc, uint8_t stif)
{
    host_mmio_reset(); prng_fault = prng_seeded = 0; lfsr = 0xffff; sync_state();
    loads = stores = commands = seeds = stopped = clocks = mode = steps = 0;
    stif_command = stif_stop = 0;
    ticks = 100;
    SOC_CLKCONCMD = SOC_CLKCONSTA = 0xc9; SOC_SLEEPCMD = 0x84; SOC_ADCCON1 = 0x33 | eoc;
    PNF_IP0 = 0x31; PNF_IP1 = 0x0e; PNF_TCON = 3; PNF_S0CON = 3; PNF_S1CON = 3;
    PNF_RFIRQF0 = 0x55; PNF_RFIRQF1 = 0xaa; PNF_IRCON2 = 0x12; SOC_RFERRF = 0x37; SOC_IRCON = 0x20 | stif;
    host_mmio_read_hook = load; host_mmio_write_hook = store; host_mmio_xaddress_hook = address;
    booting = 1; _sdcc_external_startup(); booting = 0;
    prng_fixture_initialize(); consume();
    assert(prng_fixture_state.phase == PNF_INIT && prng_fixture_state.version == 2);
    memcpy(initial, (const void *)&m0_status, 32);
}

static void step(void)
{
    prng_fixture_step(); steps++; consume();
    assert(m0_status.heartbeat == prng_fixture_state.completed);
    assert(!memcmp(initial, (const void *)&m0_status, 8) && !memcmp(initial+9, (const uint8_t *)&m0_status+9, 23));
}

static void terminal(void)
{
    uint8_t state[PNF_SIZE], buffer[68];
    unsigned r = loads, w = stores;
    memcpy(state, (const void *)&prng_fixture_state, sizeof(state));
    memcpy(buffer, &prng_fixture_buffer, sizeof(buffer));
    step();
    assert(r == loads && w == stores && !memcmp(state, (const void *)&prng_fixture_state, sizeof(state)) &&
           !memcmp(buffer, &prng_fixture_buffer, sizeof(buffer)));
}

int main(void)
{
    unsigned i, run, n, count, previous_run, sequences = 0, bit, flag_cases = 0;
    volatile uint8_t *const flags[] = {
        &PNF_IP0, &PNF_IP1, &PNF_TCON, &PNF_S0CON, &PNF_S1CON,
        &PNF_RFIRQF0, &PNF_RFIRQF1, &PNF_IRCON2, &SOC_RFERRF, &SOC_IRCON
    };
    uint16_t expected, seed_value;
    for (n = 0; n < 2; n++) {
        prepare(n ? 0x80 : 0, 0);
        stif_command = n ? 0 : 97; stif_stop = n;
        expected = 0; previous_run = 255; count = 0;
        while (prng_fixture_state.stage != PNF_END) {
            step();
            if (prng_fixture_state.phase != PNF_READY)
                fprintf(stderr, "PRNG phase=%u reason=%u stage=%u run=%u index=%u result=%u seed=%u mismatch=%u actual=%04x expected=%04x\n",
                        prng_fixture_state.phase, prng_fixture_state.reason, prng_fixture_state.stage,
                        prng_fixture_state.run, word(prng_fixture_state.index), prng_fixture_state.result,
                        prng_fixture_state.seed_result, prng_fixture_state.mismatch,
                        word(prng_fixture_state.actual), word(prng_fixture_state.expected));
            assert(prng_fixture_state.phase == PNF_READY);
            assert(prng_fixture_state.initial_flags[9] == 0x20 &&
                   prng_fixture_state.flags[9] == (commands >= 97 && !n ? 0xa0 : 0x20) &&
                   SOC_IRCON == prng_fixture_state.flags[9]);
            run = prng_fixture_state.run;
            if (prng_fixture_state.stage == PNF_SEED) {
                seed_value = run % 3 == 0 ? 0x1234 : run % 3 == 1 ? 1 : 3;
                assert(lfsr == seed_value && word(prng_fixture_state.seed) == seed_value);
                expected = seed_value;
                if (run != previous_run) {
                    count = 0; previous_run = run;
                    if (run == 1 || run == 4) memset(seen, 0, sizeof(seen));
                }
            } else if (prng_fixture_state.stage == PNF_BATCH) {
                assert(prng_fixture_state.count == (run%3 == 0 ? 4 : count == 32736 ? 31 : 32));
                assert(prng_fixture_state.checked == 68);
                for (i = 0; i < prng_fixture_state.count; i++) {
                    expected = reference(expected); count++; sequences++;
                    assert(prng_fixture_buffer.words[i] == expected);
                    if (run%3) {
                        assert(!seen[expected]); seen[expected] = 1;
                        assert((expected == (run%3 == 1 ? 1 : 3)) == (count == 32767));
                    }
                }
                for (; i < PNF_WORDS; i++) assert(prng_fixture_buffer.words[i] == 0x9669);
                if (run%3 == 2 && count == 32767)
                    for (i = 0; i < 65536; i++) assert(seen[i] == (i != 0 && i != 0x8003));
            }
        }
        assert(steps == 4111 && commands == 131084 && seeds == 16 && clocks == 2 && !stopped &&
               prng_fixture_state.completed == 4 && prng_fixture_state.benign == 15 && prng_fixture_state.seed_calls == 8);
        step(); assert(prng_fixture_state.phase == PNF_FAULT && prng_fixture_state.reason == PNF_EXPECTED_STOP &&
                       stopped == 1 && prng_fault == 6 && prng_fixture_state.probe[0] == 6 &&
                       prng_fixture_state.probe[1] == 6 && prng_fixture_state.probe[2] == 6);
        assert(prng_fixture_state.initial_flags[9] == 0x20 && prng_fixture_state.flags[9] == 0xa0 && SOC_IRCON == 0xa0);
        terminal();
    }
    for (n = 1; n <= 10; n++) {
        prepare(0, 0); step();
        if (n != 1) step();
        mode = n; at = 1; step();
        assert(prng_fixture_state.phase == PNF_FAULT && prng_fixture_state.reason == PNF_PRNG_ERROR);
        terminal();
        if (n == 2) {
            lfsr = reference(lfsr); sync_state(); SOC_ADCCON1 &= 0xf3;
            terminal(); assert(prng_fault == PRNG_POLL_LIMIT);
        }
    }
    prepare(0, 0); step(); step(); step();
    lfsr ^= 1; sync_state();
    step(); assert(prng_fixture_state.reason == PNF_PRNG_ERROR && prng_fault == PRNG_STATE_CHANGED);
    terminal();
    prepare(0, 0x80);
    for (i = 0; i < 5; i++) {
        step(); assert(prng_fixture_state.phase == PNF_READY &&
                       prng_fixture_state.initial_flags[9] == 0xa0 && prng_fixture_state.flags[9] == 0xa0);
    }
    for (n = 0; n < 10; n++) for (bit = 0; bit < 8; bit++) {
        if (n == 9 && bit == 7) continue;
        prepare(0, 0); step();
        *flags[n] ^= (uint8_t)(1u << bit);
        step(); assert(prng_fixture_state.phase == PNF_FAULT &&
                       prng_fixture_state.reason == PNF_INVARIANT && !prng_fault && !commands);
        terminal(); flag_cases++;
    }
    for (n = 0; n < 2; n++) {
        prepare(0, n ? 0x80 : 0);
        SOC_IRCON |= 0x80; step();
        assert(prng_fixture_state.phase == PNF_READY && prng_fixture_state.flags[9] == 0xa0);
        SOC_IRCON &= 0x7f; step();
        assert(prng_fixture_state.phase == PNF_FAULT && prng_fixture_state.reason == PNF_INVARIANT &&
               prng_fixture_state.initial_flags[9] == (n ? 0xa0 : 0x20) &&
               prng_fixture_state.flags[9] == 0x20 && !prng_fault && !commands);
        terminal(); flag_cases++;
    }
    printf("PRNG fixture: %u individually checked words, two complete cycles per clock/EOC model, "
           "4111 READY stages/run, true31-word tails, STIF arrival in C/probe, initial1 and %u flag rejections, "
           "benign/stopped terminal paths and guards PASS (host only).\n", sequences, flag_cases);
    return 0;
}
