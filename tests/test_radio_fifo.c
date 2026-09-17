/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 * Isolated synthetic executable. NEVER flash this image.
 */
#include "radio_fifo.h"
#include "cc2530_mmio.h"
#include "timebase.h"

#include <stddef.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) return (uint16_t)__LINE__; } while (0)

static uint16_t invalid_arguments(void)
{
    radio_fifo_diagnostics_t d, saved;
    uint8_t byte = 0, bit;
    memset(&d, 0xa5, sizeof(d));
    memcpy(&saved, &d, sizeof(d));
    CHECK(radio_fifo_clear_init(0, 1, &d) == RADIO_FIFO_INVALID_ARGUMENT);
    CHECK(radio_fifo_clear_init(1, 0, &d) == RADIO_FIFO_INVALID_ARGUMENT);
    CHECK(radio_fifo_clear_init(1, 1, NULL) == RADIO_FIFO_INVALID_ARGUMENT);
    CHECK(radio_fifo_preload_init(NULL, 1, 1, 1, &d) == RADIO_FIFO_INVALID_ARGUMENT);
    CHECK(radio_fifo_preload_init(&byte, 0, 1, 1, &d) == RADIO_FIFO_INVALID_ARGUMENT);
    CHECK(radio_fifo_preload_init(&byte, 126, 1, 1, &d) == RADIO_FIFO_INVALID_ARGUMENT);
    CHECK(radio_fifo_preload_init(&byte, 128, 1, 1, &d) == RADIO_FIFO_INVALID_ARGUMENT);
    CHECK(radio_fifo_preload_init(&byte, 65535u, 1, 1, &d) == RADIO_FIFO_INVALID_ARGUMENT);
    CHECK(radio_fifo_preload_init(&byte, 1, 0, 1, &d) == RADIO_FIFO_INVALID_ARGUMENT);
    CHECK(radio_fifo_preload_init(&byte, 1, 1, 0, &d) == RADIO_FIFO_INVALID_ARGUMENT);
    CHECK(radio_fifo_preload_init(&byte, 1, 1, 1, NULL) == RADIO_FIFO_INVALID_ARGUMENT);
    for (bit = 23; bit < 32; bit++) {
        CHECK(radio_fifo_clear_init(1UL << bit, 1, &d) == RADIO_FIFO_INVALID_ARGUMENT);
        CHECK(radio_fifo_preload_init(&byte, 1, 1UL << bit, 1, &d) == RADIO_FIFO_INVALID_ARGUMENT);
    }
    CHECK(memcmp(&d, &saved, sizeof(d)) == 0);
    return 0;
}

#if defined(__SDCC)
volatile MCU_XDATA MCU_AT(0x1e00) uint8_t radio_fifo_test_result[8];
MCU_XDATA radio_fifo_diagnostics_t radio_fifo_test_diagnostics;
MCU_XDATA uint8_t radio_fifo_test_body[RADIO_FIFO_BODY_MAX];
static const MCU_CODE uint8_t code_body[3] = {0x31, 0x69, 0xa5};
volatile MCU_XDATA uint8_t radio_fifo_test_action, radio_fifo_test_return;
volatile MCU_XDATA uint16_t radio_fifo_test_length, radio_fifo_test_limit;
volatile MCU_XDATA uint32_t radio_fifo_test_timeout;

void radio_fifo_test_cycle(void)
{
    __asm
        .globl _radio_fifo_test_before
    _radio_fifo_test_before:
        nop
    __endasm;
    if (radio_fifo_test_action == 0)
        radio_fifo_test_return = radio_fifo_clear_init(radio_fifo_test_timeout, radio_fifo_test_limit,
                                                       &radio_fifo_test_diagnostics);
    else {
        const uint8_t *selected;
        /* Preserve each SDCC address-space tag before forming the generic pointer. */
        if (radio_fifo_test_action == 2)
            selected = code_body;
        else
            selected = radio_fifo_test_body;
        radio_fifo_test_return = radio_fifo_preload_init(selected, radio_fifo_test_length,
            radio_fifo_test_timeout, radio_fifo_test_limit, &radio_fifo_test_diagnostics);
    }
    __asm
        .globl _radio_fifo_test_done
    _radio_fifo_test_done:
        nop
    __endasm;
}

void main(void)
{
    uint8_t index;
    uint16_t result = invalid_arguments();
    for (index = 0; index < RADIO_FIFO_BODY_MAX; index++)
        radio_fifo_test_body[index] = index ^ 0x69u;
    radio_fifo_test_result[0] = 'R';
    radio_fifo_test_result[1] = 'F';
    radio_fifo_test_result[2] = 'Q';
    radio_fifo_test_result[3] = 'T';
    radio_fifo_test_result[4] = 1;
    radio_fifo_test_result[5] = 8;
    radio_fifo_test_result[6] = (uint8_t)result;
    radio_fifo_test_result[7] = (uint8_t)(result >> 8);
    for (;;)
        radio_fifo_test_cycle();
}
#else
#include "host_mmio.h"
#include <assert.h>
#include <stdio.h>

static const uint16_t order[] = {
    0xa8, 0xb8, 0x9a, 0xbe, 0xc6, 0x9e,
    0x6189, 0x618a, 0x61e1, 0x6192, 0x618b, 0x6193,
    0x619b, 0x619c, 0x619d, 0x619e, 0x619f, 0x61a1, 0x61a2, 0xbf
};
static uint8_t xregs[256], original[256], body[125], fifo[128];
static struct {
    uint8_t before[4];
    radio_fifo_diagnostics_t d;
    uint8_t after[4];
} guarded;
#define diag guarded.d
static unsigned cursor, sample_byte, samples, operations, data_writes, rx_flushes, tx_flushes;
static unsigned delay, remaining, pending, pending_value, fault_at, fault_kind, fault_value;
static uint16_t last_address;
static uint8_t last_value;
static uint32_t ticks, tick_step, latched;
static bool have_read, stalled;
static unsigned cases;

static void consume_read(void)
{
    if (!have_read) {
        assert(read_count == 0 && xread_count == 0);
        return;
    }
    if (last_address < 256) {
        assert(read_count == 1 && xread_count == 0);
        assert(reads[0].address == last_address && reads[0].value == last_value);
        read_count = 0;
    } else {
        assert(xread_count == 1 && read_count == 0);
        assert(xreads[0].address == last_address && xreads[0].value == last_value);
        xread_count = 0;
    }
    have_read = false;
}

static void apply_effect(void)
{
    if (pending == 0xed) {
        xregs[0x9b] = xregs[0x9d] = xregs[0x9e] = xregs[0x9f] = 0;
        xregs[0x93] &= 0x3fu;
    } else if (pending == 0xee) {
        xregs[0x9c] = xregs[0xa1] = xregs[0xa2] = 0;
    } else {
        assert(pending == 0xd9 && xregs[0x9c] < 128);
        fifo[xregs[0x9c]++] = (uint8_t)pending_value;
        xregs[0xa2]++;
    }
    pending = 0;
}

static uint8_t event(uint16_t address, uint8_t value)
{
    consume_read();
    if (address >= 0x95 && address <= 0x97) {
        assert(cursor == 0 && address == 0x95 + sample_byte);
        if (sample_byte == 0) {
            latched = ticks;
            ticks = (ticks + tick_step) & TIMEBASE_TICKS_MASK;
            samples++;
        }
        value = (uint8_t)(latched >> (8 * sample_byte));
        sample_byte = (sample_byte + 1) % 3;
    } else {
        assert(sample_byte == 0 && address == order[cursor]);
        if (cursor == 0 && pending && !stalled && --remaining == 0)
            apply_effect();
        if (address >= 0x6100)
            value = xregs[address & 255];
        cursor = (cursor + 1) % (sizeof(order) / sizeof(order[0]));
    }
    last_address = address;
    last_value = value;
    have_read = true;
    return value;
}

static uint8_t read_sfr(uint8_t address, uint8_t value)
{
    assert(address != 0xd9 && address != 0xe1);
    return event(address, value);
}

static uint8_t read_xreg(uint16_t address)
{
    assert(address >= 0x6180 && address <= 0x61ef);
    return event(address, 0);
}

static void write_sfr(uint8_t address, uint8_t before, uint8_t value)
{
    consume_read();
    assert(cursor == 0 && sample_byte == 0 && pending == 0);
    assert(write_count == 1 && writes[0].address == address);
    assert(writes[0].before == before && writes[0].after == value);
    write_count = 0;
    assert(address == 0xe1 || address == 0xd9);
    if (address == 0xe1) {
        assert(value == 0xed || value == 0xee);
        if (value == 0xed)
            rx_flushes++;
        else
            tx_flushes++;
        pending = value;
        assert(rx_flushes <= 1 && tx_flushes <= 1);
    } else {
        assert(data_writes < 126 && rx_flushes == 0 && tx_flushes == 0);
        fifo[data_writes] = value;
        data_writes++;
        pending = address;
        pending_value = value;
    }
    operations++;
    remaining = delay;
    if (!stalled && remaining == 0)
        apply_effect();
    if (operations == fault_at) {
        switch (fault_kind) {
        case 1: SOC_RFERRF = (uint8_t)fault_value; break;
        case 2: xregs[0x9c] = (uint8_t)fault_value; break;
        case 3: xregs[0xa1] = (uint8_t)fault_value; break;
        case 4: xregs[0xa2] = (uint8_t)fault_value; break;
        case 5: xregs[0x9b] ^= 1u; break;
        case 6: xregs[0xe1] = 0x20; break;
        case 7: SOC_CLKCONCMD = SOC_CLKCONSTA = 0x80; break;
        default: assert(fault_kind == 0);
        }
    }
}

static void prepare(void)
{
    unsigned i;
    host_mmio_reset();
    memset(xregs, 0, sizeof(xregs));
    memset(fifo, 0xa5, sizeof(fifo));
    memset(&guarded, 0xa5, sizeof(guarded));
#define SEED(name, address) name = original[address] = (uint8_t)((address) ^ 0x69);
    CC2530_REGISTER_LIST(SEED)
#undef SEED
    SOC_IEN0 = SOC_IEN1 = SOC_IEN2 = SOC_RFERRF = 0;
    SOC_CLKCONCMD = SOC_CLKCONSTA = 0x88;
    SOC_SLEEPCMD = 0x84;
    xregs[0x89] = 0x40;
    xregs[0x8a] = 1;
    /* Defined but irrelevant PC/FSM/CCA bits are intentionally nonzero. */
    xregs[0xe1] = 23;
    xregs[0x92] = 0x3f;
    xregs[0x93] = 0x18;
    for (i = 0; i < sizeof(body); i++)
        body[i] = (uint8_t)(i ^ 0x69);
    cursor = sample_byte = samples = operations = data_writes = rx_flushes = tx_flushes = 0;
    delay = remaining = pending = fault_at = fault_kind = fault_value = 0;
    ticks = 0xfffffe;
    tick_step = 1;
    stalled = have_read = false;
    host_mmio_read_hook = read_sfr;
    host_mmio_write_hook = write_sfr;
    host_mmio_xread_hook = read_xreg;
}

static void finish(radio_fifo_result_t actual, radio_fifo_result_t expected)
{
    unsigned i;
    if (actual != expected)
        fprintf(stderr, "radio case %u: got %u expected %u\n", cases, actual, expected);
    assert(actual == expected);
    consume_read();
    assert(write_count == 0 && sample_byte == 0 && (cursor == 0 || cursor == 6));
    for (i = 0; i < 4; i++)
        assert(guarded.before[i] == 0xa5 && guarded.after[i] == 0xa5);
#define PRESERVED(name, address) \
    if ((address) != 0xd9 && (address) != 0xe1 && (address) != 0xbf && \
        (address) != 0xa8 && (address) != 0xb8 && (address) != 0x9a && \
        (address) != 0xc6 && (address) != 0x9e && (address) != 0xbe) \
        assert(name == original[address]);
    CC2530_REGISTER_LIST(PRESERVED)
#undef PRESERVED
    for (i = 0; i < sizeof(body); i++)
        assert(body[i] == (uint8_t)(i ^ 0x69));
    cases++;
}

static radio_fifo_result_t preload(uint16_t length, uint32_t timeout, uint16_t limit)
{
    return radio_fifo_preload_init(body, length, timeout, limit, &diag);
}

int main(void)
{
    unsigned i, j;
    uint32_t length;
    radio_fifo_diagnostics_t saved;
    prepare();
    assert(invalid_arguments() == 0 && !read_count && !xread_count && !write_count);
    memcpy(&saved, &diag, sizeof(saved));
    for (length = 126; length <= 65535; length++)
        assert(preload((uint16_t)length, 10, 1) == RADIO_FIFO_INVALID_ARGUMENT);
    assert(memcmp(&diag, &saved, sizeof(saved)) == 0 && !have_read && !operations);
    for (i = 1; i <= 125; i++) {
        prepare();
        delay = 2;
        finish(preload((uint16_t)i, 1000, 252), RADIO_FIFO_OK);
        assert(diag.bytes_written == i + 1 && diag.bytes_verified == i + 1);
        assert(diag.tx_count == i + 1 && diag.tx_first == 0 && diag.tx_last == i + 1);
        assert(diag.polls == 2 * (i + 1) && samples == diag.polls + 1u);
        assert(fifo[0] == i + 2 && memcmp(fifo + 1, body, i) == 0);
        assert(diag.strobes == 0 && diag.confirmed == 0);
    }
    prepare();
    finish(radio_fifo_clear_init(1, 1, &diag), RADIO_FIFO_EMPTY);
    assert(!samples && !operations && diag.sample_valid == 1);
    for (i = 0; i <= 128; i++) {
        prepare();
        xregs[0x9b] = (uint8_t)i;
        xregs[0x9d] = 1; xregs[0x9e] = 2; xregs[0x9f] = 3; xregs[0x93] |= 0xc0;
        xregs[0x9c] = (uint8_t)i;
        xregs[0xa1] = 4; xregs[0xa2] = 5;
        delay = 3;
        finish(radio_fifo_clear_init(100, 6, &diag), RADIO_FIFO_OK);
        assert(diag.strobes == 3 && diag.confirmed == 3 && diag.polls == 6);
        assert(!diag.rx_count && !diag.tx_count && !diag.rx_first && !diag.rx_last &&
               !diag.rx_packet && !diag.tx_first && !diag.tx_last && diag.fifo_signals == 0x18);
    }
    for (i = 1; i <= 128; i++) {
        prepare();
        xregs[0x9c] = (uint8_t)i;
        finish(preload(1, 10, 3), RADIO_FIFO_NOT_EMPTY);
        assert(!operations && !samples);
    }
    for (i = 0; i < 3; i++) {
        prepare();
        xregs[i == 0 ? 0xa1 : i == 1 ? 0xa2 : 0x9c] = 1;
        finish(preload(1, 10, 3), RADIO_FIFO_NOT_EMPTY);
        assert(!operations);
    }
    for (i = 1; i < 256; i++) {
        prepare(); SOC_IEN0 = (uint8_t)i;
        finish(preload(1, 10, 3), RADIO_FIFO_UNSUPPORTED_STATE);
        assert(SOC_IEN0 == i && !operations && !samples && !diag.sample_valid);
        prepare(); SOC_IEN1 = (uint8_t)i;
        finish(preload(1, 10, 3), RADIO_FIFO_UNSUPPORTED_STATE);
        assert(SOC_IEN1 == i && !operations);
        prepare(); SOC_IEN2 = (uint8_t)i;
        finish(preload(1, 10, 3), RADIO_FIFO_UNSUPPORTED_STATE);
        assert(SOC_IEN2 == i && !operations);
    }
    for (i = 0; i < 256; i++)
        for (j = 0; j < 256; j++) {
            bool valid = !(i & 0x47) && i == j;
            prepare(); SOC_CLKCONCMD = (uint8_t)i; SOC_CLKCONSTA = (uint8_t)j;
            finish(radio_fifo_clear_init(1, 1, &diag),
                   valid ? RADIO_FIFO_EMPTY : RADIO_FIFO_UNSUPPORTED_STATE);
            assert(!operations && !samples && SOC_CLKCONCMD == i && SOC_CLKCONSTA == j);
        }
    for (i = 0; i < 256; i++) {
        prepare(); xregs[0x89] = (uint8_t)i;
        finish(radio_fifo_clear_init(1, 1, &diag),
               i == 0x40 ? RADIO_FIFO_EMPTY : RADIO_FIFO_UNSUPPORTED_STATE);
        assert(!operations);
        prepare(); xregs[0x8a] = (uint8_t)i;
        finish(radio_fifo_clear_init(1, 1, &diag),
               i == 1 ? RADIO_FIFO_EMPTY : RADIO_FIFO_UNSUPPORTED_STATE);
        assert(!operations);
        prepare(); xregs[0xe1] = (uint8_t)i;
        finish(radio_fifo_clear_init(1, 1, &diag),
               i & 0xc0 ? RADIO_FIFO_UNSUPPORTED_STATE : i & 0x20 ? RADIO_FIFO_BUSY : RADIO_FIFO_EMPTY);
        prepare(); xregs[0x92] = (uint8_t)i;
        finish(radio_fifo_clear_init(1, 1, &diag),
               i & 0x80 ? RADIO_FIFO_UNSUPPORTED_STATE : i & 0x40 ? RADIO_FIFO_BUSY : RADIO_FIFO_EMPTY);
        prepare(); xregs[0x8b] = (uint8_t)i;
        finish(radio_fifo_clear_init(1, 1, &diag), i ? RADIO_FIFO_BUSY : RADIO_FIFO_EMPTY);
        prepare(); SOC_SLEEPCMD = (uint8_t)i;
        finish(radio_fifo_clear_init(1, 1, &diag),
               (i & 7) == 4 ? RADIO_FIFO_EMPTY : RADIO_FIFO_UNSUPPORTED_STATE);
    }
    for (i = 1; i < 256; i++) {
        prepare(); SOC_RFERRF = (uint8_t)i;
        finish(preload(1, 10, 3), i & 0x80 ? RADIO_FIFO_UNSUPPORTED_STATE : RADIO_FIFO_CONTROLLER_ERROR);
        assert(diag.errors == i && !operations);
        prepare(); SOC_RFERRF = (uint8_t)i; xregs[0x9c] = 128;
        finish(radio_fifo_clear_init(10, 3, &diag),
               i & 0x80 ? RADIO_FIFO_UNSUPPORTED_STATE : RADIO_FIFO_CONTROLLER_ERROR);
        assert(diag.errors == i && !operations && xregs[0x9c] == 128);
    }
    for (i = 129; i < 256; i++) {
        prepare(); xregs[0x9c] = (uint8_t)i;
        finish(preload(1, 10, 3), RADIO_FIFO_COUNT_ERROR);
        prepare(); xregs[0x9b] = (uint8_t)i;
        finish(preload(1, 10, 3), RADIO_FIFO_COUNT_ERROR);
    }
    for (i = 0; i < 8; i++) {
        prepare(); xregs[0x93] = (uint8_t)(1u << i);
        if ((1u << i) & 0x27u)
            finish(preload(1, 10, 3), RADIO_FIFO_BUSY);
        else if (i == 6)
            finish(preload(1, 10, 3), RADIO_FIFO_CONTROLLER_ERROR);
        else
            finish(preload(1, 10, 3), RADIO_FIFO_OK);
    }
    for (i = 0; i < 7; i++)
        for (j = 1; j <= 126; j++) {
            prepare(); fault_at = j; fault_kind = 1; fault_value = 1u << i;
            finish(preload(125, 1000, 126), RADIO_FIFO_CONTROLLER_ERROR);
            assert(diag.bytes_written == j && diag.bytes_verified == j - 1 && diag.errors == (1u << i));
            assert(SOC_RFERRF == (1u << i) && !diag.strobes);
        }
    for (i = 2; i <= 7; i++) {
        prepare(); fault_at = 2; fault_kind = i; fault_value = 4;
        finish(preload(3, 100, 10), i <= 4 ? RADIO_FIFO_COUNT_ERROR :
               i == 6 ? RADIO_FIFO_BUSY : RADIO_FIFO_STATE_CHANGED);
        assert(diag.bytes_written == 2 && diag.bytes_verified == 1 && data_writes == 2);
    }
    prepare(); stalled = true; tick_step = 0;
    finish(preload(125, 1, 65535u), RADIO_FIFO_POLL_LIMIT);
    assert(diag.polls == 65535u && diag.bytes_written == 1 && !diag.bytes_verified);
    prepare(); delay = 3;
    finish(preload(1, 3, 10), RADIO_FIFO_TIMEOUT);
    assert(diag.polls == 3 && diag.elapsed_ticks == 3 && !diag.bytes_verified && diag.tx_count == 1);
    prepare(); tick_step = 11;
    finish(preload(1, 10, 3), RADIO_FIFO_TIMEOUT);
    assert(diag.elapsed_ticks == 11 && diag.tx_count == 1 && !diag.bytes_verified);
    prepare(); tick_step = 0xffffff;
    finish(preload(1, 10, 3), RADIO_FIFO_COUNTER_RANGE);
    prepare(); tick_step = 0x800000;
    finish(preload(1, 10, 3), RADIO_FIFO_COUNTER_RANGE);
    prepare(); tick_step = 0x80000a;
    finish(preload(1, 10, 3), RADIO_FIFO_TIMEBASE_ERROR);
    assert(diag.timebase_status == TIMEBASE_AMBIGUOUS);
    prepare(); tick_step = 0;
    finish(preload(125, 0x7fffff, 1), RADIO_FIFO_POLL_LIMIT);
    assert(diag.bytes_written == 1 && diag.bytes_verified == 1);
    prepare(); xregs[0x9b] = xregs[0x9c] = 1;
    finish(radio_fifo_clear_init(100, 1, &diag), RADIO_FIFO_POLL_LIMIT);
    assert(diag.strobes == 1 && diag.confirmed == 1 && xregs[0x9c] == 1);
    for (i = 0; i < 2; i++) {
        prepare(); xregs[i ? 0x9c : 0x9b] = 1; stalled = true; tick_step = 0;
        finish(radio_fifo_clear_init(100, 3, &diag), RADIO_FIFO_POLL_LIMIT);
        assert(diag.strobes == (i ? 2 : 1) && !diag.confirmed && diag.polls == 3);
        prepare(); xregs[i ? 0x9c : 0x9b] = 1; fault_at = 1; fault_kind = 1; fault_value = 0x40;
        finish(radio_fifo_clear_init(100, 3, &diag), RADIO_FIFO_CONTROLLER_ERROR);
        assert(diag.strobes == (i ? 2 : 1) && !diag.confirmed && diag.errors == 0x40);
    }
    printf("host radio FIFO: %u modeled cases, all lengths/flags, ordered consumed logs, "
           "bounded partial effects and preserved state PASS (no radio hardware)\n", cases);
    return 0;
}
#endif
