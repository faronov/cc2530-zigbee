/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "radio_fifo_fixture.h"
#include "host_mmio.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static uint8_t rf[256], tx[128], last_value, baseline[32], saved[108];
static uint16_t last_address;
static uint32_t ticks, latched, tick_step;
static unsigned byte_reads, tx_writes, flushes, rx_flushes, clock_writes, samples;
static unsigned bad_byte, error_write, mode;
static bool pending_read, booting;

static void consume(void)
{
    if (pending_read) {
        if (last_address < 256) {
            assert(read_count == 1 && !xread_count);
            assert(reads[0].address == last_address && reads[0].value == last_value);
            read_count = 0;
        } else {
            assert(xread_count == 1 && !read_count);
            assert(xreads[0].address == last_address && xreads[0].value == last_value);
            xread_count = 0;
        }
    } else
        assert(!read_count && !xread_count);
    pending_read = false;
}

static uint8_t note(uint16_t address, uint8_t value)
{
    consume();
    last_address = address;
    last_value = value;
    pending_read = true;
    return value;
}

static uint8_t load(uint8_t address, uint8_t value)
{
    assert(address != 0xd9 && address != 0xe1);
    if (address == 0x95) {
        latched = ticks;
        ticks = (ticks + tick_step) & 0xffffff;
        samples++;
    }
    if (address >= 0x95 && address <= 0x97)
        value = (uint8_t)(latched >> (8 * (address - 0x95)));
    return note(address, value);
}

static uint8_t xload(uint16_t address)
{
    uint8_t value;
    assert(SOC_CLKCONCMD == 0x88 && SOC_CLKCONSTA == 0x88);
    if (address >= 0x6080 && address <= 0x60fd) {
        assert(address == 0x6080 + byte_reads && byte_reads < rf[0x9c]);
        value = tx[byte_reads];
        if (bad_byte && byte_reads + 1 == bad_byte)
            value ^= 1;
        byte_reads++;
    } else {
        assert(address == 0x6189 || address == 0x618a || address == 0x618b ||
               address == 0x61e1 || address == 0x6192 || address == 0x6193 ||
               (address >= 0x619b && address <= 0x619f) ||
               (address >= 0x61a1 && address <= 0x61a5));
        value = rf[address & 255];
    }
    return note(address, value);
}

static void store(uint8_t address, uint8_t before, uint8_t value)
{
    consume();
    assert(write_count == 1 && writes[0].address == address);
    assert(writes[0].before == before && writes[0].after == value);
    write_count = 0;
    if (booting) {
        assert(address == 0xa8 || address == 0xb8 || address == 0x9a ||
               address == 0x80 || address == 0x90 || address == 0xf2 ||
               address == 0xf3 || address == 0xf4 || address == 0x8f ||
               address == 0xf6 || address == 0xfd || address == 0xfe);
        return;
    }
    if (address == 0xc6) {
        assert(value == 0x88 || value == 0xc9);
        clock_writes++;
        if (mode != 1)
            SOC_CLKCONSTA = value;
    } else if (address == 0xe1) {
        assert(value == 0xee || value == 0xed);
        if (value == 0xee) {
            flushes++;
            if (mode != 3)
                rf[0x9c] = rf[0xa1] = rf[0xa2] = 0;
            byte_reads = 0;
        } else {
            rx_flushes++;
            rf[0x9b] = rf[0x9d] = rf[0x9e] = rf[0x9f] = 0;
            rf[0x93] &= 0x3f;
        }
    } else {
        assert(address == 0xd9 && rf[0x9c] < 126 && !SOC_IEN0 && !SOC_IEN1 && !SOC_IEN2);
        tx[rf[0x9c]++] = value;
        rf[0xa2]++;
        tx_writes++;
        if (mode == 2)
            ticks = (ticks + 4096) & 0xffffff;
        if (error_write && tx_writes == error_write)
            SOC_RFERRF = 0x10;
    }
}

static void prepare(void)
{
    host_mmio_reset();
    memset(rf, 0, sizeof(rf));
    memset(tx, 0xa5, sizeof(tx));
    rf[0x89] = 0x40; rf[0x8a] = 1;
    rf[0xa3] = 0x69; rf[0xa4] = 0x35; rf[0xa5] = 0x27;
    SOC_CLKCONCMD = SOC_CLKCONSTA = 0xc9;
    SOC_SLEEPCMD = 0x84;
    RFF_IP0 = 0x31; RFF_IP1 = 0x0e;
    RFF_RFIRQF0 = 0x69; RFF_RFIRQF1 = 0x35; RFF_S1CON = 3; RFF_TCON = 0xaf;
    ticks = 0xfffffe; tick_step = 1;
    byte_reads = tx_writes = flushes = rx_flushes = clock_writes = samples = 0;
    bad_byte = error_write = mode = 0;
    pending_read = false;
    host_mmio_read_hook = load;
    host_mmio_xread_hook = xload;
    host_mmio_write_hook = store;
    booting = true;
    _sdcc_external_startup();
    booting = false;
    radio_fifo_fixture_initialize();
    consume();
    assert(radio_fifo_fixture_state.phase == RFF_INIT && !radio_fifo_fixture_state.radio_valid);
    memcpy(baseline, (const void *)&m0_status, sizeof(baseline));
}

static void step(uint8_t stage, uint8_t phase)
{
    unsigned i;
    radio_fifo_fixture_step();
    consume();
    assert(radio_fifo_fixture_state.phase == phase && radio_fifo_fixture_state.stage == stage);
    assert(radio_fifo_fixture_state.command == SOC_CLKCONCMD &&
           radio_fifo_fixture_state.status == SOC_CLKCONSTA);
    assert(!SOC_IEN0 && !SOC_IEN1 && !SOC_IEN2 && SOC_SLEEPCMD == 0x84);
    assert(memcmp((const void *)&m0_status, baseline, 8) == 0 &&
           memcmp((const uint8_t *)&m0_status + 9, baseline + 9, 23) == 0);
    assert(m0_status.heartbeat == radio_fifo_fixture_state.completed && !rx_flushes);
    for (i = 0; i < 5; i++)
        assert(radio_fifo_fixture_state.reserved[i] == 0);
    if (phase == RFF_FAULT) {
        unsigned reads_before = byte_reads, writes_before = tx_writes, flush_before = flushes;
        memcpy(saved, (const void *)&radio_fifo_fixture_state, sizeof(saved));
        radio_fifo_fixture_step();
        assert(!pending_read && !read_count && !xread_count && !write_count);
        assert(memcmp(saved, (const void *)&radio_fifo_fixture_state, sizeof(saved)) == 0);
        assert(reads_before == byte_reads && writes_before == tx_writes && flush_before == flushes);
    }
}

int main(void)
{
    unsigned cycle, i, position;
    prepare();
    step(RFF_CLOCK, RFF_READY);
    assert(clock_writes == 1 && radio_fifo_fixture_state.clock[18] == CLOCK_NOT_ATTEMPTED);
    for (cycle = 1; cycle <= 257; cycle++) {
        step(RFF_EMPTY, RFF_READY);
        assert(radio_fifo_fixture_state.fifo_result == RADIO_FIFO_EMPTY);
        assert(!radio_fifo_fixture_state.fifo[7] && !radio_fifo_fixture_state.fifo[9]);
        step(RFF_SMALL, RFF_READY);
        assert(radio_fifo_fixture_state.checked == 4 && byte_reads == 4);
        assert(memcmp(tx, "\x05\x13\x57\xa9", 4) == 0);
        assert(radio_fifo_fixture_state.fifo[9] == 4 && radio_fifo_fixture_state.fifo[10] == 4);
        step(RFF_CLEAR_SMALL, RFF_READY);
        assert(radio_fifo_fixture_state.fifo[7] == 2 && radio_fifo_fixture_state.fifo[8] == 2);
        step(RFF_MAX, RFF_READY);
        assert(radio_fifo_fixture_state.checked == 126 && byte_reads == 126 && tx[0] == 127);
        for (i = 0; i < 125; i++)
            assert(tx[i + 1] == (i ^ 0x69));
        step(RFF_CLEAR_MAX, RFF_READY);
        assert(!rf[0x9c] && radio_fifo_fixture_state.completed == (cycle & 255));
    }
    assert(tx_writes == 257 * 130 && flushes == 514);
    for (position = 1; position <= 126; position++) {
        prepare(); step(RFF_CLOCK, RFF_READY); step(RFF_EMPTY, RFF_READY);
        step(RFF_SMALL, RFF_READY); step(RFF_CLEAR_SMALL, RFF_READY);
        bad_byte = position;
        step(RFF_MAX, RFF_FAULT);
        assert(radio_fifo_fixture_state.reason == RFF_BYTES &&
               radio_fifo_fixture_state.fifo_result == RADIO_FIFO_OK &&
               radio_fifo_fixture_state.checked == position - 1 &&
               radio_fifo_fixture_state.mismatch_index == position - 1 &&
               radio_fifo_fixture_state.actual == (radio_fifo_fixture_state.expected ^ 1));
        assert(flushes == 1 && rf[0x9c] == 126);
    }
    prepare(); mode = 1; tick_step = 2048;
    step(RFF_CLOCK, RFF_FAULT);
    assert(radio_fifo_fixture_state.reason == RFF_CLOCK_ERROR &&
           radio_fifo_fixture_state.clock_result == CLOCK_TIMEOUT &&
           radio_fifo_fixture_state.clock[18] == CLOCK_ROLLBACK_UNCONFIRMED &&
           !radio_fifo_fixture_state.radio_valid && !tx_writes && !flushes);
    prepare(); rf[0x9b] = 1;
    step(RFF_CLOCK, RFF_FAULT);
    assert(radio_fifo_fixture_state.reason == RFF_ENTRY && !tx_writes && !flushes);
    prepare(); step(RFF_CLOCK, RFF_READY); step(RFF_EMPTY, RFF_READY); mode = 2;
    step(RFF_SMALL, RFF_FAULT);
    assert(radio_fifo_fixture_state.reason == RFF_FIFO_ERROR &&
           radio_fifo_fixture_state.fifo_result == RADIO_FIFO_TIMEOUT &&
           radio_fifo_fixture_state.fifo[9] == 1 && !radio_fifo_fixture_state.fifo[10] &&
           radio_fifo_fixture_state.fifo[4] == 1 && !radio_fifo_fixture_state.fifo[5] &&
           !radio_fifo_fixture_state.checked && radio_fifo_fixture_state.radio[7] == 1 &&
           radio_fifo_fixture_state.radio_valid && tx_writes == 1 && !flushes);
    prepare(); step(RFF_CLOCK, RFF_READY); step(RFF_EMPTY, RFF_READY);
    step(RFF_SMALL, RFF_READY); mode = 3; tick_step = 0;
    step(RFF_CLEAR_SMALL, RFF_FAULT);
    assert(radio_fifo_fixture_state.fifo_result == RADIO_FIFO_POLL_LIMIT &&
           radio_fifo_fixture_state.fifo[4] == 0 && radio_fifo_fixture_state.fifo[5] == 16 &&
           radio_fifo_fixture_state.fifo[7] == 2 && !radio_fifo_fixture_state.fifo[8] && flushes == 1);
    prepare(); step(RFF_CLOCK, RFF_READY); step(RFF_EMPTY, RFF_READY); error_write = 2;
    step(RFF_SMALL, RFF_FAULT);
    assert(radio_fifo_fixture_state.fifo_result == RADIO_FIFO_CONTROLLER_ERROR &&
           radio_fifo_fixture_state.fifo[9] == 2 && radio_fifo_fixture_state.fifo[10] == 1 &&
           SOC_RFERRF == 0x10 && !flushes && !radio_fifo_fixture_state.checked);
    prepare(); step(RFF_CLOCK, RFF_READY); RFF_IP0 ^= 1;
    step(RFF_EMPTY, RFF_FAULT);
    assert(radio_fifo_fixture_state.reason == RFF_INVARIANT && !tx_writes && !flushes);
    prepare(); radio_fifo_fixture_state.phase = 0;
    step(RFF_CLOCK, RFF_FAULT);
    assert(radio_fifo_fixture_state.reason == RFF_PHASE && !clock_writes);
    puts("host radio FIFO fixture: real startup/clock/FIFO, 257 cycles, CODE/XDATA bytes, "
         "126 corruption positions, partial/clock/terminal faults and consumed logs PASS");
    return 0;
}
