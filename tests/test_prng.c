/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "prng.h"

#if defined(__SDCC)
volatile MCU_XDATA MCU_AT(0x1e00) uint8_t prng_test_result[8];
MCU_XDATA uint16_t prng_test_seed, prng_test_output;
uint16_t MCU_XDATA * MCU_XDATA prng_test_pointer;
MCU_XDATA uint8_t prng_test_operation, prng_test_limit, prng_test_return;

void prng_test_cycle(void)
{
    __asm
        .globl _prng_test_before
_prng_test_before:
        nop
    __endasm;
    if (prng_test_operation == 0)
        prng_test_return = prng_seed_explicit(prng_test_seed);
    else
        prng_test_return = prng_next16(prng_test_pointer, prng_test_limit);
    __asm
        .globl _prng_test_done
_prng_test_done:
        nop
    __endasm;
}

void main(void)
{
    uint8_t i;
    SOC_IEN0 = 0; SOC_IEN1 = 0; SOC_IEN2 = 0;
    prng_test_result[0] = 'P'; prng_test_result[1] = 'R';
    prng_test_result[2] = 'N'; prng_test_result[3] = 'G';
    prng_test_result[4] = 1; prng_test_result[5] = 8;
    for (i = 6; i < 8; i++) prng_test_result[i] = 0;
    prng_test_pointer = &prng_test_output;
    for (;;) prng_test_cycle();
}
#else
#include "host_mmio.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

extern uint8_t prng_fault, prng_seeded, prng_reserved_end;
static struct { uint16_t before, value, after; } caller;
#define output caller.value
static uint16_t state, forced_state;
static uint8_t force_state;
static uint8_t snapshot[256], logged_address, logged_value;
static unsigned loads, stores, calls, delay, remaining, pending, ignore_write;
static unsigned mutate_at, mutate_address, mutate_value, seed_write;
static uint16_t output_address;
static uint8_t seen[65536];

/* Independent bit-cell interpretation of SWRU191F Figure14-1, not target code. */
static uint16_t reference(uint16_t value)
{
    uint8_t cells[16], next[16];
    unsigned i, step;
    for (i = 0; i < 16; i++) cells[i] = (uint8_t)((value >> i) & 1);
    for (step = 0; step < 13; step++) {
        next[0] = cells[15];
        for (i = 1; i < 16; i++) next[i] = cells[i-1];
        next[2] ^= cells[15]; next[15] ^= cells[15];
        memcpy(cells, next, sizeof(cells));
    }
    value = 0;
    for (i = 0; i < 16; i++) value |= (uint16_t)((uint16_t)cells[i] << i);
    return value;
}

static uint16_t polynomial(uint16_t value)
{
    unsigned i;
    for (i = 0; i < 13; i++)
        value = (uint16_t)((uint32_t)value << 1) ^ ((value & 0x8000u) ? 0x8005u : 0u);
    return value;
}

static volatile uint8_t *reg(unsigned address)
{
    switch (address) {
#define REGISTER_CASE(name, location) case location: return &name;
        CC2530_REGISTER_LIST(REGISTER_CASE)
#undef REGISTER_CASE
    default: assert(0); return NULL;
    }
}

static void consume(void)
{
    assert(!xread_count);
    if (read_count) {
        assert(read_count == 1 && reads[0].address == logged_address && reads[0].value == logged_value);
        read_count = 0;
    }
    assert(!write_count);
}

static void sync_state(void)
{
    SOC_RNDL = (uint8_t)state; SOC_RNDH = (uint8_t)(state >> 8);
}

static uint8_t load(uint8_t address, uint8_t value)
{
    (void)value;
    consume(); loads++;
    if (address == 0xb4 && pending && (SOC_ADCCON1 & 0x0c) == 4) {
        if (remaining) remaining--;
        else { state = force_state ? forced_state : reference(state); sync_state(); SOC_ADCCON1 &= 0xf3; pending = 0; }
    }
    if (loads == mutate_at) *reg(mutate_address) = (uint8_t)mutate_value;
    assert(address == 0xa8 || address == 0xb8 || address == 0x9a || address == 0xbe ||
           address == 0xc6 || address == 0x9e || address == 0xb4 || address == 0xbc || address == 0xbd);
    logged_address = address; logged_value = *reg(address);
    return logged_value;
}

static void store(uint8_t address, uint8_t before, uint8_t value)
{
    assert(write_count == 1 && writes[0].address == address && writes[0].before == before && writes[0].after == value);
    write_count = 0; consume(); stores++;
    assert(!pending && (SOC_ADCCON1 & 0x7f) == (address == 0xb4 ? 0x37 : 0x33));
    assert(address == 0xbc || address == 0xb4);
    if (stores == ignore_write) { *reg(address) = before; return; }
    if (address == 0xbc) {
        seed_write++;
        state = (uint16_t)((uint16_t)(state & 255) << 8) | value; sync_state();
    } else {
        assert(value == 0x37 && !(before & 0x4c) && (before & 0x33) == 0x33);
        SOC_ADCCON1 = (before & 0xf3) | 4; pending = 1; remaining = delay;
    }
}

static uint16_t address(const volatile void *object)
{
    if (object == &prng_reserved_end) return 0x100;
    assert(object == &output);
    return output_address;
}

static void begin(void)
{
    consume(); loads = stores = seed_write = 0;
}

static void reset(void)
{
    host_mmio_reset();
    state = 0xffff; sync_state();
    SOC_ADCCON1 = 0x33; SOC_CLKCONCMD = SOC_CLKCONSTA = 0xc9; SOC_SLEEPCMD = 4;
    prng_fault = prng_seeded = 0;
    delay = remaining = pending = ignore_write = mutate_at = force_state = 0;
    output = 0x6969; output_address = 0x200;
    caller.before = 0xa596; caller.after = 0x5a69;
    host_mmio_read_hook = load; host_mmio_write_hook = store; host_mmio_xaddress_hook = address;
    begin();
}

static void untouched(void)
{
#define CHECK_REGISTER(name, location) \
    if (location != 0xbc && location != 0xbd && location != 0xb4) assert(name == snapshot[location]);
    CC2530_REGISTER_LIST(CHECK_REGISTER)
#undef CHECK_REGISTER
}

static void capture(void)
{
#define CAPTURE_REGISTER(name, location) snapshot[location] = name;
    CC2530_REGISTER_LIST(CAPTURE_REGISTER)
#undef CAPTURE_REGISTER
}

static prng_result_t seed(uint16_t value)
{
    prng_result_t result;
    begin(); capture(); result = prng_seed_explicit(value); calls++; consume();
    if (!mutate_at) untouched();
    assert(caller.before == 0xa596 && caller.after == 0x5a69);
    if (result == PRNG_OK) assert(state == value && seed_write == 2 && stores == 2 && prng_seeded);
    return result;
}

static prng_result_t step(uint8_t limit)
{
    prng_result_t result;
    uint16_t old = output, expected = reference(state);
    begin(); capture(); result = prng_next16(&output, limit); calls++; consume();
    assert(caller.before == 0xa596 && caller.after == 0x5a69);
    if (!mutate_at) untouched();
    if (result == PRNG_OK) assert(output == expected && stores == 1 && !pending && !(SOC_ADCCON1 & 0x0c));
    else assert(output == old);
    return result;
}

static void terminal(prng_result_t result)
{
    uint16_t old = output, retained = state;
    uint8_t control = SOC_ADCCON1;
    assert(prng_fault == result);
    assert(step(255) == result && !loads && !stores);
    assert(seed(0x1234) == result && !loads && !stores);
    assert(output == old && state == retained && control == SOC_ADCCON1);
}

int main(void)
{
    unsigned long value, count, cycles = 0;
    unsigned i, limit, baseline, kind;
    uint16_t current;
    for (value = 0; value < 65536UL; value++) {
        assert(reference((uint16_t)value) == polynomial((uint16_t)value));
        assert((reference((uint16_t)value) == value) == (value == 0 || value == 0x8003));
        if (seen[value]) continue;
        current = (uint16_t)value; count = 0;
        do { assert(!seen[current]); seen[current] = 1; count++; current = reference(current); } while (current != value);
        assert(count == ((value == 0 || value == 0x8003) ? 1UL : 32767UL)); cycles++;
    }
    assert(cycles == 4);
    for (value = 0; value < 65536UL; value++) {
        reset();
        if (value == 0 || value == 0x8003) { assert(seed((uint16_t)value) == PRNG_INVALID_SEED && !loads && !stores); continue; }
        assert(seed((uint16_t)value) == PRNG_OK); assert(step(1) == PRNG_OK);
    }
    reset(); assert(step(1) == PRNG_NOT_SEEDED && !loads && !stores && !prng_fault);
    assert(prng_next16(NULL, 1) == PRNG_INVALID_ARGUMENT);
    calls++;
    assert(step(0) == PRNG_INVALID_ARGUMENT && !loads && !stores);
    for (value = 1; value < 65536UL; value++) {
        output_address = (uint16_t)value;
        assert(step(1) == (value <= 0x100 ? PRNG_BUFFER_OWNERSHIP : value >= 0x1dff ? PRNG_INVALID_RANGE : PRNG_NOT_SEEDED));
        assert(!loads && !stores && !prng_fault);
    }
    for (i = 0; i < 256; i++) {
        reset(); SOC_ADCCON1 = (uint8_t)i;
        if (i == 0x33 || i == 0xb3) {
            assert(seed(0x1234) == PRNG_OK && SOC_ADCCON1 == i);
            assert(step(1) == PRNG_OK && SOC_ADCCON1 == i);
        } else { assert(seed(0x1234) == PRNG_UNSUPPORTED_STATE && !stores); terminal(PRNG_UNSUPPORTED_STATE); }
    }
    for (limit = 1; limit <= 255; limit++) {
        reset(); assert(seed(0xffff) == PRNG_OK); delay = limit-1;
        assert(step((uint8_t)limit) == PRNG_OK);
        reset(); assert(seed(0xffff) == PRNG_OK); delay = limit;
        assert(step((uint8_t)limit) == PRNG_POLL_LIMIT); terminal(PRNG_POLL_LIMIT);
        state = reference(state); sync_state(); SOC_ADCCON1 &= 0xf3; pending = 0;
        terminal(PRNG_POLL_LIMIT);
    }
    for (i = 1; i <= 2; i++) {
        reset(); ignore_write = i;
        assert(seed(0x1234) == PRNG_STATE_CHANGED); terminal(PRNG_STATE_CHANGED);
    }
    reset(); assert(seed(0x1234) == PRNG_OK); ignore_write = 1;
    assert(step(1) == PRNG_STATE_CHANGED); terminal(PRNG_STATE_CHANGED);
    reset(); assert(seed(1) == PRNG_OK); assert(step(1) == PRNG_OK); baseline = loads;
    for (i = 1; i <= baseline; i++) {
        reset(); assert(seed(1) == PRNG_OK); mutate_at = i; mutate_address = 0xb4; mutate_value = 0x3b;
        if (step(1) == PRNG_OK) { fprintf(stderr, "Missed control mutation at read %u/%u\n", i, baseline); assert(0); }
        mutate_at = 0; terminal((prng_result_t)prng_fault);
    }
    for (kind = 0; kind < 6; kind++) {
        static const uint8_t locations[] = {0xa8, 0xb8, 0x9a, 0xbe, 0xc6, 0x9e};
        for (i = 0; i < 256; i++) {
            reset(); *reg(locations[kind]) = (uint8_t)i;
            if ((kind < 3 && !i) || (kind == 3 && (i & 7) == 4) || (kind >= 4 && i == 0xc9)) {
                assert(seed(1) == PRNG_OK);
            } else { assert(seed(1) == PRNG_UNSUPPORTED_STATE && !stores); terminal(PRNG_UNSUPPORTED_STATE); }
        }
    }
    reset(); SOC_CLKCONCMD = SOC_CLKCONSTA = 0x88; assert(seed(0x8002) == PRNG_OK);
    for (i = 0; i < 257; i++) { assert(step(1) == PRNG_OK); assert(seed((uint16_t)(i+1)) == PRNG_OK); }
    for (kind = 0; kind < 3; kind++) {
        static const uint16_t bad_states[] = {0, 0x8003, 0x1234};
        reset(); assert(seed(0x1234) == PRNG_OK);
        force_state = 1; forced_state = bad_states[kind];
        assert(step(1) == PRNG_STATE_CHANGED); terminal(PRNG_STATE_CHANGED);
    }
    for (kind = 0; kind < 2; kind++) {
        reset(); assert(seed(0x1234) == PRNG_OK); state = 0x4321; sync_state();
        assert((kind ? seed(0x2222) : step(1)) == PRNG_STATE_CHANGED && !stores);
        terminal(PRNG_STATE_CHANGED);
    }
    for (kind = 0; kind < 4; kind++) {
        static const unsigned positions[] = {3, 12, 19, 28};
        reset(); assert(seed(1) == PRNG_OK);
        mutate_at = positions[kind]; mutate_address = 0xa8; mutate_value = 1;
        assert(step(1) == PRNG_UNSUPPORTED_STATE);
        assert(stores == (kind < 2 ? 0u : 1u));
        mutate_at = 0; terminal(PRNG_UNSUPPORTED_STATE);
    }
    for (kind = 0; kind < 4; kind++) {
        static const unsigned positions[] = {9, 18, 25, 34};
        for (i = 0; i < 3; i++) {
            static const uint8_t controls[] = {0xb3, 0x73, 0x32};
            prng_result_t result = i ? PRNG_UNSUPPORTED_STATE : PRNG_STATE_CHANGED;
            reset(); assert(seed(1) == PRNG_OK);
            mutate_at = positions[kind]; mutate_address = 0xb4; mutate_value = controls[i];
            assert(step(1) == result && stores == (kind < 2 ? 0u : 1u));
            mutate_at = 0; terminal(result);
        }
    }
    for (i = 0; i < 2; i++) {
        reset(); assert(seed(0x1234) == PRNG_OK);
        output_address = i ? 0x1dfe : 0x101;
        assert(step(1) == PRNG_OK);
    }
    printf("PRNG host: %u real-driver calls; all65536 states, two fixed points + two32767 cycles; "
           "seed/order/13-shift independent models, bounded control/log/pointer/fault guards PASS (deterministic, no entropy).\n", calls);
    return 0;
}
#endif
