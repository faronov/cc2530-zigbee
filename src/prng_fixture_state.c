/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "prng_fixture.h"

volatile MCU_XDATA prng_fixture_t prng_fixture_state;
MCU_XDATA prng_fixture_buffer_t prng_fixture_buffer;
MCU_XDATA uint16_t prng_fixture_probe_output;
MCU_XDATA clock_diagnostics_t prng_fixture_clock;
static MCU_XDATA uint16_t index, batch, seed, observed;
static MCU_XDATA uint32_t total;
extern MCU_XDATA uint8_t prng_fault;
#define state prng_fixture_state
#define buffer prng_fixture_buffer

static void put16(volatile MCU_XDATA uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)value; out[1] = (uint8_t)(value >> 8);
}

static uint16_t read_state(void)
{
    uint8_t low, high;
    low = MMIO_READ(SOC_RNDL);
    high = MMIO_READ(SOC_RNDH);
    return (uint16_t)low | ((uint16_t)high << 8);
}

static void snapshot(void)
{
    state.adc = MMIO_READ(SOC_ADCCON1);
    state.command = MMIO_READ(SOC_CLKCONCMD); state.status = MMIO_READ(SOC_CLKCONSTA);
    state.sleep = MMIO_READ(SOC_SLEEPCMD);
    state.enables[0] = MMIO_READ(SOC_IEN0); state.enables[1] = MMIO_READ(SOC_IEN1);
    state.enables[2] = MMIO_READ(SOC_IEN2);
    state.flags[0] = MMIO_READ(PNF_IP0); state.flags[1] = MMIO_READ(PNF_IP1);
    state.flags[2] = MMIO_READ(PNF_TCON); state.flags[3] = MMIO_READ(PNF_S0CON);
    state.flags[4] = MMIO_READ(PNF_S1CON); state.flags[5] = MMIO_READ(PNF_RFIRQF0);
    state.flags[6] = MMIO_READ(PNF_RFIRQF1); state.flags[7] = MMIO_READ(PNF_IRCON2);
    state.flags[8] = MMIO_READ(SOC_RFERRF); state.flags[9] = MMIO_READ(SOC_IRCON);
    state.fault_latch = prng_fault;
}

static uint8_t unchanged(uint8_t stopped)
{
    uint8_t i, previous_ircon = state.flags[9];
    snapshot();
    if (state.adc != (state.initial_adc | (stopped ? 12u : 0u)) ||
        state.command != state.status || (state.command != 0xc9 && state.command != 0x88) ||
        state.sleep != state.initial_sleep || (state.sleep & 7u) != 4u ||
        state.enables[0] || state.enables[1] || state.enables[2])
        return 0;
    for (i = 0; i < 9; i++)
        if (state.flags[i] != state.initial_flags[i]) return 0;
    /* SWRU191F p.47 / 11.1-11.2 p.129: default compare can latch STIF
     * with IRQs disabled. Never clear it or accept a later deassertion.
     */
    if ((state.flags[9] & 0x7fu) != (state.initial_flags[9] & 0x7fu) ||
        (state.flags[9] != previous_ircon && state.flags[9] != (previous_ircon | 0x80u)))
        return 0;
    return 1;
}

static void fault(uint8_t reason)
{
    state.reason = reason; state.phase = PNF_FAULT;
    state.fault_latch = prng_fault;
}

static void counters(void)
{
    put16(state.seed, seed); put16(state.index, index); put16(state.batch, batch);
    put16(state.total, (uint16_t)total); state.total[2] = (uint8_t)(total >> 16);
}

static void mismatch(uint8_t at, uint16_t actual, uint16_t expected)
{
    state.mismatch = at; put16(state.actual, actual); put16(state.expected, expected);
    fault(PNF_BYTES);
}

static uint8_t same_state(uint16_t expected)
{
    observed = read_state(); put16(state.hardware, observed);
    return observed == expected && read_state() == expected;
}

static void benign(void)
{
    uint8_t i, result;
    observed = read_state();
    prng_fixture_probe_output = 0x9669;
    for (i = 0; i < 4; i++) {
        result = i == 0 ? prng_next16(&prng_fixture_probe_output, PNF_LIMIT) :
                 i == 1 ? prng_next16(&prng_fixture_probe_output, 0) :
                 prng_seed_explicit(i == 2 ? 0 : 0x8003u);
        state.result = result;
        if (result != (i == 0 ? PRNG_NOT_SEEDED : i == 1 ? PRNG_INVALID_ARGUMENT : PRNG_INVALID_SEED) ||
            prng_fixture_probe_output != 0x9669 || !same_state(0xffffu) || !unchanged(0) || prng_fault) {
            fault(PNF_INVARIANT); return;
        }
        state.benign |= (uint8_t)(1u << i);
    }
}

static void load_seed(void)
{
    state.count = state.checked = 0;
    seed = state.run % 3u == 0 ? 0x1234u : state.run % 3u == 1 ? 1u : 3u;
    state.seed_result = prng_seed_explicit(seed);
    if (state.seed_result != PRNG_OK) { fault(PNF_PRNG_ERROR); return; }
    state.seed_calls++;
    if (!same_state(seed) || !unchanged(0)) { fault(PNF_INVARIANT); return; }
    counters();
}

static void fill_batch(void)
{
    uint8_t i, length;
    uint16_t expected;
    static const MCU_CODE uint16_t prefix[3][4] = {
        {0x8d94, 0xe5ac, 0xcbbe, 0x1731},
        {0x2000, 0x9803, 0x8a03, 0x8783},
        {0x6000, 0x2800, 0x1e00, 0x0880}
    };
    length = state.run % 3u == 0 ? 4u : index == 32736u ? 31u : PNF_WORDS;
    state.count = state.checked = 0;
    buffer.before[0] = 0x69; buffer.before[1] = 0x96;
    buffer.after[0] = 0xa5; buffer.after[1] = 0x5a;
    for (i = 0; i < PNF_WORDS; i++) buffer.words[i] = 0x9669;
    for (i = 0; i < length; i++) {
        state.result = prng_next16(&buffer.words[i], PNF_LIMIT);
        if (state.result != PRNG_OK) { fault(PNF_PRNG_ERROR); return; }
        if (!same_state(buffer.words[i]) || !unchanged(0)) { fault(PNF_INVARIANT); return; }
        if (state.run % 3u == 0 || index < 4u) {
            expected = prefix[state.run % 3u][index & 3u];
            if (buffer.words[i] != expected) { mismatch(i, buffer.words[i], expected); return; }
        }
        index++; total++; state.count++; counters();
    }
    if (buffer.before[0] != 0x69 || buffer.before[1] != 0x96 ||
        buffer.after[0] != 0xa5 || buffer.after[1] != 0x5a) { mismatch(254, 0, 1); return; }
    for (i = length; i < PNF_WORDS; i++)
        if (buffer.words[i] != 0x9669) { mismatch(i, buffer.words[i], 0x9669); return; }
    state.checked = 68;
    batch++; counters(); state.completed++; bringup_tick();
}

static void clock_to(clock_source_t target)
{
    uint8_t i;
    state.clock_result = clock_select_init(target, 1024, 4096, &prng_fixture_clock);
    put16(state.clock, (uint16_t)prng_fixture_clock.request.elapsed_ticks);
    state.clock[2] = (uint8_t)(prng_fixture_clock.request.elapsed_ticks >> 16); state.clock[3] = 0;
    put16(state.clock + 4, prng_fixture_clock.request.polls);
    state.clock[6] = prng_fixture_clock.request.timebase_status;
    put16(state.clock + 7, (uint16_t)prng_fixture_clock.rollback.elapsed_ticks);
    state.clock[9] = (uint8_t)(prng_fixture_clock.rollback.elapsed_ticks >> 16); state.clock[10] = 0;
    put16(state.clock + 11, prng_fixture_clock.rollback.polls);
    state.clock[13] = prng_fixture_clock.rollback.timebase_status;
    state.clock[14] = prng_fixture_clock.saved_command; state.clock[15] = prng_fixture_clock.requested_command;
    state.clock[16] = prng_fixture_clock.observed_command; state.clock[17] = prng_fixture_clock.observed_status;
    state.clock[18] = prng_fixture_clock.rollback_result;
    if (state.clock_result != CLOCK_OK) { fault(PNF_CLOCK_ERROR); return; }
    i = target == CLOCK_RC16 ? 0xc9 : 0x88;
    if (!unchanged(0) || state.command != i) fault(PNF_INVARIANT);
}

static void probe(void)
{
    if (!unchanged(0) || prng_fault) { fault(PNF_INVARIANT); return; }
    observed = read_state(); put16(state.hardware, observed);
    prng_fixture_probe_output = 0x9669;
    /* Deliberate fixture-only precondition violation, before the driver call.
     * SWRU191F 14.3 p.145: RCTRL11 stops PRNG. ST stays0, low reserved bits11.
     */
    MMIO_WRITE(SOC_ADCCON1, 0x3f);
    state.probe[0] = prng_next16(&prng_fixture_probe_output, PNF_LIMIT);
    if (state.probe[0] != PRNG_UNSUPPORTED_STATE) { fault(PNF_PRNG_ERROR); return; }
    state.probe[1] = prng_next16(&prng_fixture_probe_output, PNF_LIMIT);
    state.probe[2] = prng_seed_explicit(0x1234);
    if (state.probe[1] != PRNG_UNSUPPORTED_STATE || state.probe[2] != PRNG_UNSUPPORTED_STATE ||
        prng_fixture_probe_output != 0x9669 || read_state() != observed || read_state() != observed ||
        !unchanged(1) || prng_fault != PRNG_UNSUPPORTED_STATE) { fault(PNF_INVARIANT); return; }
    fault(PNF_EXPECTED_STOP);
}

void prng_fixture_initialize(void)
{
    uint8_t i;
    bringup_initialize();
    index = batch = seed = observed = 0; total = 0;
    for (i = 0; i < PNF_SIZE; i++) ((volatile MCU_XDATA uint8_t *)&state)[i] = 0;
    state.signature[0] = 'M'; state.signature[1] = '2'; state.signature[2] = 'P'; state.signature[3] = 'N';
    state.version = PNF_VERSION; state.size = PNF_SIZE; state.result = state.seed_result = 255;
    state.mismatch = 255; state.clock_result = state.clock[18] = CLOCK_NOT_ATTEMPTED;
    state.guards[0] = 0x69; state.guards[1] = 0x96;
    snapshot();
    state.initial_adc = state.adc; state.initial_sleep = state.sleep;
    for (i = 0; i < 10; i++) state.initial_flags[i] = state.flags[i];
    if ((state.adc & 0x7fu) != 0x33 || state.command != 0xc9 || prng_fault ||
        !same_state(0xffffu) || !unchanged(0)) { fault(PNF_ENTRY); return; }
    state.phase = PNF_INIT;
}

#if defined(__SDCC)
static inline void prng_fixture_step(void)
#else
void prng_fixture_step(void)
#endif
{
    if (state.phase == PNF_FAULT) return;
    if ((state.phase != PNF_INIT && state.phase != PNF_READY) || state.stage > PNF_END || state.run > 5) {
        fault(PNF_PHASE); return;
    }
    state.phase = PNF_RUNNING;
    if (!unchanged(0) || prng_fault) { fault(PNF_INVARIANT); return; }
    if (state.stage == PNF_INITIAL) {
        state.stage = PNF_BENIGN; benign();
    } else if (state.stage == PNF_END) {
        state.stage = PNF_PROBE; probe();
    } else if (state.stage == PNF_SEED) {
        state.stage = PNF_BATCH; fill_batch();
    } else if (state.stage == PNF_BATCH && index == (state.run % 3u == 0 ? 8u : 32767u)) {
        if (state.run == 5) { state.stage = PNF_END; clock_to(CLOCK_RC16); }
        else {
            state.run++; index = batch = 0; counters();
            if (state.run == 3) { state.stage = PNF_CLOCK; clock_to(CLOCK_XOSC32); }
            else { state.stage = PNF_SEED; load_seed(); }
        }
    } else if (state.stage == PNF_BATCH && state.run % 3u != 0) {
        fill_batch();
    } else {
        state.stage = PNF_SEED; load_seed();
    }
    if (state.phase != PNF_FAULT) state.phase = PNF_READY;
}

#if defined(__SDCC)
void main(void)
{
    prng_fixture_initialize();
    if (state.phase == PNF_FAULT) prng_fixture_fault();
    prng_fixture_before();
    for (;;) {
        prng_fixture_step();
        if (state.phase == PNF_FAULT) prng_fixture_fault();
        prng_fixture_ready();
    }
}
#endif
