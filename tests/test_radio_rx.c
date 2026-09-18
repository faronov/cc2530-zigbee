/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 * The isolated SDCC executable is synthetic only: NEVER flash it.
 */
#include "radio_rx.h"
#include "timebase.h"

#if defined(__SDCC)
volatile MCU_XDATA MCU_AT(0x1e00) uint8_t radio_rx_test_result[8];
MCU_XDATA radio_rx_frame_t radio_rx_test_frame;
MCU_XDATA radio_rx_diagnostics_t radio_rx_test_diagnostics;
MCU_XDATA uint8_t radio_rx_test_channel, radio_rx_test_return;
MCU_XDATA uint32_t radio_rx_test_timeout;
MCU_XDATA uint16_t radio_rx_test_limit;

void radio_rx_test_cycle(void)
{
    __asm
        .globl _radio_rx_test_before
    _radio_rx_test_before:
        nop
    __endasm;
    radio_rx_test_return = radio_rx_receive_init(radio_rx_test_channel, radio_rx_test_timeout,
        radio_rx_test_limit, &radio_rx_test_frame, &radio_rx_test_diagnostics);
    __asm
        .globl _radio_rx_test_done
    _radio_rx_test_done:
        nop
    __endasm;
}

void main(void)
{
    uint8_t i;
    SOC_IEN0 = 0; SOC_IEN1 = 0; SOC_IEN2 = 0;
    radio_rx_test_result[0] = 'R'; radio_rx_test_result[1] = 'X';
    radio_rx_test_result[2] = 'O'; radio_rx_test_result[3] = '1';
    radio_rx_test_result[4] = 1; radio_rx_test_result[5] = 8;
    radio_rx_test_result[6] = 0; radio_rx_test_result[7] = 0;
    for (i = 0; i < sizeof(radio_rx_test_frame); i++)
        ((uint8_t MCU_XDATA *)&radio_rx_test_frame)[i] = 0xa5;
    for (;;) radio_rx_test_cycle();
}
#else
#include "host_mmio.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

extern uint8_t radio_rx_fault, radio_rx_reserved_end;
uint8_t _gptrput_PARM_2;
static struct { uint8_t before[4]; radio_rx_frame_t value; uint8_t after[4]; } frame;
static struct { uint8_t before[4]; radio_rx_diagnostics_t value; uint8_t after[4]; } diag;
static radio_rx_frame_t saved_frame;
static radio_rx_diagnostics_t saved_diag;
static uint8_t xregs[0x300], fifo[128], wire[128];
static uint16_t out_address, diag_address;
static uint32_t ticks, latched, tick_step;
static unsigned last_address, last_value, reads_total, writes_total, cases;
static unsigned mode, phase_samples, cal_delay, arrival_delay, stop_delay, flush_delay;
static unsigned expected_length, wire_length, rd, config_writes, on_writes, stop_writes, flush_writes;
static unsigned silent_channel, ignored_config, forced_read_count;
static unsigned inject_sample, samples, inject_address, inject_value;
static unsigned command;
static unsigned trace_enabled, trace_count;
static const uint16_t config_addresses[] = {
    0x6189, 0x618a, 0x6180, 0x6182, 0x6194, 0x6195, 0x61b2, 0x61fa, 0x61ae, 0x618f
};
static const uint8_t config_values[] = {0x40, 0, 0x0c, 0, 0x7f, 1, 0x15, 9, 0};
#define XR(address) xregs[(address) - 0x6100u]

static void trace(char kind, unsigned address, unsigned value)
{
    if (trace_enabled)
        printf("%s[\"%c\",%u,%u]", trace_count++ ? "," : "", kind, address, value);
}

static void consume_reads(void)
{
    assert(read_count + xread_count <= 1);
    if (read_count) {
        assert(reads[0].address == last_address && reads[0].value == last_value);
        read_count = 0;
    }
    if (xread_count) {
        assert(xreads[0].address == last_address && xreads[0].value == last_value);
        xread_count = 0;
    }
}

static volatile uint8_t *sfr(unsigned address)
{
    switch (address) {
#define REG_CASE(name, location) case location: return &name;
        CC2530_REGISTER_LIST(REG_CASE)
#undef REG_CASE
    default: assert(0); return NULL;
    }
}

static void advance(void)
{
    phase_samples++;
    if (mode == 1 && phase_samples > cal_delay) {
        XR(0x6192) = 0; XR(0x6193) |= 5; XR(0x6199) = 1;
        if (!silent_channel && !XR(0x619b) && phase_samples > cal_delay + arrival_delay) {
            memcpy(fifo, wire, wire_length); rd = 0;
            XR(0x619b) = (uint8_t)wire_length;
            XR(0x619e) = (uint8_t)(wire_length & 127);
            XR(0x6193) |= 0xc0;
            SOC_RFIRQF0 |= 0x66;
        }
    } else if (mode == 2 && phase_samples > stop_delay) {
        XR(0x6192) = 0; XR(0x6193) &= 0xd8;
        SOC_RFIRQF1 |= 4; mode = 3;
    } else if (mode == 4 && phase_samples > flush_delay) {
        XR(0x619b) = XR(0x619d) = XR(0x619e) = XR(0x619f) = 0;
        XR(0x6193) &= 0x3f; mode = 0;
    }
}

static uint8_t xload(uint16_t address)
{
    uint8_t value;
    consume_reads(); reads_total++;
    assert(address >= 0x6180 && address < 0x6300);
    if (address == 0x624a) {
        samples++;
        if (inject_sample && samples == inject_sample) {
            if (inject_address < 256) *sfr(inject_address) = (uint8_t)inject_value;
            else XR(inject_address) = (uint8_t)inject_value;
        }
        advance();
    }
    value = XR(address);
    last_address = address; last_value = value;
    trace('r', address, value);
    return value;
}

static uint8_t load(uint8_t address, uint8_t value)
{
    consume_reads(); reads_total++;
    if (address == SOC_ST0_ADDRESS) {
        latched = ticks; ticks = (ticks + tick_step) & TIMEBASE_TICKS_MASK;
        value = (uint8_t)latched;
    } else if (address == SOC_ST1_ADDRESS) value = (uint8_t)(latched >> 8);
    else if (address == SOC_ST2_ADDRESS) value = (uint8_t)(latched >> 16);
    else if (address == SOC_RFD_ADDRESS) {
        assert(mode == 3 && !XR(0x618b) && !(XR(0x6193) & 0x27) && XR(0x619b));
        assert(rd < wire_length);
        value = fifo[rd++];
        XR(0x619b) = forced_read_count ? 129 : XR(0x619b) - 1;
        XR(0x619d) = (uint8_t)(rd & 127);
        XR(0x6193) &= 0xbf;
        if (!XR(0x619b)) XR(0x6193) &= 0x7f;
    }
    last_address = address; last_value = value;
    trace('r', address, value);
    return value;
}

static void store(uint8_t address, uint8_t before, uint8_t value)
{
    consume_reads(); writes_total++;
    assert(write_count == 1 && writes[0].address == address &&
           writes[0].before == before && writes[0].after == value);
    write_count = 0;
    trace('w', address, value);
    assert(address == SOC_RFST_ADDRESS);
    if (value == 0xe3) {
        assert(mode == 0 && !XR(0x618b) && !XR(0x619b) && config_writes == 10);
        assert(XR(0x6189) == 0x40 && XR(0x618a) == 0 && XR(0x6180) == 0x0c && !XR(0x6182));
        on_writes++; mode = 1; phase_samples = 0;
        XR(0x618b) = 0x80; XR(0x6192) = 0x40; XR(0x6193) = 1; XR(0x6199) = 0;
    } else {
        assert(value == 0xed && mode == 3 && !XR(0x618b) && !(XR(0x6193) & 0x27));
        flush_writes++; mode = 4; phase_samples = 0;
    }
}

static void xstore(uint16_t address, uint8_t value)
{
    consume_reads(); writes_total++;
    assert(xwrite_count == 1 && xwrites[0].address == address && xwrites[0].value == value);
    xwrite_count = 0;
    trace('w', address, value);
    if (address == 0x618d) {
        assert(value == 0x80 && mode == 1 && XR(0x618b) == 0x80);
        stop_writes++; mode = 2; phase_samples = 0;
        XR(0x618b) = 0; SOC_RFIRQF0 |= 0x80;
    } else {
        unsigned i = config_writes++;
        assert(mode == 0 && i < 10 && address == config_addresses[i]);
        assert(value == (i == 9 ? 11 + 5 * (command - 11) : config_values[i]));
        if (ignored_config != i + 1) XR(address) = value;
    }
}

static uint16_t xaddress(const volatile void *object)
{
    if (object == &frame.value) return out_address;
    if (object == &diag.value) return diag_address;
    if (object == &radio_rx_reserved_end) return 0x300;
    if (object == &_gptrput_PARM_2) return 0x1d00;
    assert(0); return 0;
}

static void reset(unsigned length, uint8_t rssi, uint8_t crc_correlation, unsigned extra)
{
    unsigned i;
    assert(3 <= length && length <= 127 && length + 1 + extra <= 128);
    host_mmio_reset(); radio_rx_fault = 0;
    memset(xregs, 0, sizeof(xregs));
    memset(&frame, 0xa5, sizeof(frame)); memset(&diag, 0x69, sizeof(diag));
    saved_frame = frame.value; memcpy(&saved_diag, &diag.value, sizeof(saved_diag));
    out_address = 0x400; diag_address = 0x500;
    ticks = 0; tick_step = 1; latched = 0;
    mode = phase_samples = cal_delay = arrival_delay = stop_delay = flush_delay = 0;
    reads_total = writes_total = config_writes = on_writes = stop_writes = flush_writes = rd = 0;
    silent_channel = ignored_config = forced_read_count = inject_sample = samples = 0;
    command = 15; expected_length = length; wire_length = length + 1 + extra;
    wire[0] = (uint8_t)length;
    for (i = 1; i < length - 1; i++) wire[i] = (uint8_t)(i ^ 0x69);
    wire[length-1] = rssi; wire[length] = crc_correlation;
    for (i = length+1; i < wire_length; i++) wire[i] = (uint8_t)(i ^ 0x96);
    SOC_SLEEPCMD = 4; SOC_CLKCONCMD = SOC_CLKCONSTA = 0x88;
    XR(0x624a) = 0xa5; XR(0x6189) = 0x40; XR(0x618a) = 1; XR(0x6180) = 0x0d;
    XR(0x6182) = 7; XR(0x6194) = 0x40; XR(0x6195) = 1;
    XR(0x61a8) = 0x85; XR(0x61a9) = 0x14; XR(0x61b8) = 0x75; XR(0x61b9) = 8;
    XR(0x61b2) = 0x11; XR(0x61fa) = 0x0f; XR(0x61ae) = 0x2b; XR(0x618f) = 0x0b;
    host_mmio_read_hook = load; host_mmio_write_hook = store;
    host_mmio_xread_hook = xload; host_mmio_xwrite_hook = xstore;
    host_mmio_xaddress_hook = xaddress;
}

static void guards(void)
{
    unsigned i;
    consume_reads();
    assert(!write_count && !xwrite_count);
    for (i = 0; i < 4; i++) {
        assert(frame.before[i] == 0xa5 && frame.after[i] == 0xa5);
        assert(diag.before[i] == 0x69 && diag.after[i] == 0x69);
    }
}

static radio_rx_result_t run(uint32_t timeout, uint16_t limit)
{
    radio_rx_result_t result = radio_rx_receive_init((uint8_t)command, timeout, limit,
                                                    &frame.value, &diag.value);
    unsigned i;
    cases++; guards();
    if (result == RADIO_RX_OK) {
        assert(!radio_rx_fault && frame.value.length == expected_length - 2);
        assert(frame.value.rssi_raw == wire[expected_length-1]);
        assert(frame.value.correlation == (wire[expected_length] & 127));
        assert(!memcmp(frame.value.body, wire+1, expected_length-2));
        for (i = expected_length-2; i < 125; i++) assert(frame.value.body[i] == 0xa5);
        assert(on_writes == 1 && stop_writes == 1 && flush_writes == 1 && mode == 0);
        assert(diag.value.writes == 10 && diag.value.verified == 10 && diag.value.actions == 7);
        assert(diag.value.phase == 7 && diag.value.bytes_read == expected_length + 1);
    } else {
        assert(!memcmp(&frame.value, &saved_frame, sizeof(saved_frame)));
        if (radio_rx_fault) {
            unsigned old_reads = reads_total, old_writes = writes_total;
            radio_rx_diagnostics_t copy;
            memcpy(&copy, &diag.value, sizeof(copy));
            assert(radio_rx_receive_init(0, 0, 0, NULL, NULL) == result);
            assert(old_reads == reads_total && old_writes == writes_total &&
                   !memcmp(&copy, &diag.value, sizeof(copy)));
        }
    }
    return result;
}

static void emit(const char *name, uint32_t timeout, uint16_t limit, radio_rx_result_t expected)
{
    unsigned i;
    radio_rx_diagnostics_t *d = &diag.value;
    printf("{\"name\":\"%s\",\"channel\":%u,\"timeout\":%lu,\"limit\":%u,\"initial\":{",
           name, command, (unsigned long)timeout, limit);
#define EMIT_SFR(name, address) printf("\"%u\":%u,", address, name);
    CC2530_REGISTER_LIST(EMIT_SFR)
#undef EMIT_SFR
    for (i = 0; i < sizeof(xregs); i++)
        printf("\"%u\":%u%s", 0x6100u+i, xregs[i], i+1 == sizeof(xregs) ? "" : ",");
    printf("},\"events\":["); trace_count = 0; trace_enabled = 1;
    assert(run(timeout, limit) == expected);
    trace_enabled = 0;
    printf("],\"result\":%u,\"fault\":%u,\"frame\":\"", expected, radio_rx_fault);
    for (i = 0; i < sizeof(frame.value); i++) printf("%02x", ((uint8_t *)&frame.value)[i]);
    printf("\",\"diagnostics\":[%lu,%u", (unsigned long)d->elapsed_ticks, d->polls);
#define FIELD(name) printf(",%u", d->name);
    FIELD(timebase_status) FIELD(phase) FIELD(writes) FIELD(verified) FIELD(actions) FIELD(sample_valid)
    FIELD(rx_enable) FIELD(fsm0) FIELD(signals) FIELD(rx_count) FIELD(tx_count)
    FIELD(rx_first) FIELD(rx_last) FIELD(rx_packet) FIELD(tx_first) FIELD(tx_last)
    FIELD(errors) FIELD(flags0) FIELD(flags1) FIELD(rssi_valid)
    FIELD(bytes_read) FIELD(phr) FIELD(rssi_raw) FIELD(crc_correlation) FIELD(discarded_bytes)
#undef FIELD
    puts("]}");
}

static void emit_vectors(void)
{
    unsigned i;
    reset(3, 0x81, 0xff, 0); emit("minimum", 10000, 1000, RADIO_RX_OK);
    reset(127, 0x7f, 0x80, 0); emit("maximum", 10000, 1000, RADIO_RX_OK);
    reset(5, 0xa5, 0xe9, 122); emit("discard queued bytes", 10000, 1000, RADIO_RX_OK);
    reset(5, 0xa5, 0xe9, 0); ticks = 0xfffff0;
    cal_delay = 2; arrival_delay = 3; stop_delay = 2; flush_delay = 1;
    emit("delayed phases and wrap", 10000, 1000, RADIO_RX_OK);
    reset(5, 0, 0x69, 0); emit("bad CRC is not publication", 10000, 1000, RADIO_RX_BAD_CRC);
    reset(5, 0, 128, 0); XR(0x619f) = 128;
    emit("RXP1 is eight bits but entry must be empty", 10000, 1000, RADIO_RX_NOT_EMPTY);
    reset(5, 0, 128, 0); SOC_RFIRQF0 = 0x18;
    emit("sticky source flags do not imply TX", 10000, 1000, RADIO_RX_OK);
    reset(5, 0, 128, 0); inject_sample = 22; inject_address = 0x619f; inject_value = 128;
    emit("RXP1 high bit during reception", 10000, 1000, RADIO_RX_OK);
    reset(5, 0, 128, 0); ignored_config = 2;
    emit("unconfirmed configuration", 10000, 1000, RADIO_RX_STATE_CHANGED);
    reset(5, 0, 128, 0); inject_sample = 21; inject_address = 0x618b; inject_value = 128;
    emit("last configuration sample must still be idle", 10000, 1000, RADIO_RX_STATE_CHANGED);
    reset(5, 0, 128, 0); silent_channel = 1; tick_step = 0;
    emit("stopped counter cap", 10000, 27, RADIO_RX_POLL_LIMIT);
    reset(5, 0, 128, 0); silent_channel = 1;
    emit("raw deadline exact edge", 25, 1000, RADIO_RX_TIMEOUT);
    reset(5, 0, 128, 0); tick_step = 0x800000;
    emit("counter range", 10000, 1000, RADIO_RX_COUNTER_RANGE);
    reset(5, 0, 128, 0); tick_step = 0x800001;
    emit("ambiguous deadline helper failure", 1, 1000, RADIO_RX_TIMEBASE_ERROR);
    reset(5, 0, 128, 0); forced_read_count = 1;
    emit("read count changed", 10000, 1000, RADIO_RX_COUNT_ERROR);
    reset(5, 0, 128, 0); wire[0] = 0;
    emit("invalid PHR", 10000, 1000, RADIO_RX_BAD_LENGTH);
    for (i = 21; i <= 28; i++) {
        reset(5, 0, 128, 0); emit("no action without remaining poll", 10000, (uint16_t)i, RADIO_RX_POLL_LIMIT);
    }
    reset(5, 0, 128, 0); emit("last available decision poll", 10000, 29, RADIO_RX_OK);
    reset(5, 0, 128, 0); inject_sample = 24; inject_address = 0xbf; inject_value = 4;
    emit("late controller fault", 10000, 1000, RADIO_RX_CONTROLLER_ERROR);
    reset(5, 0, 128, 0); SOC_RFIRQF1 = 1;
    emit("TXACKDONE is never allowed", 10000, 1000, RADIO_RX_STATE_CHANGED);
}

int main(int argc, char **argv)
{
    unsigned length, value, i, need;
    static const uint16_t guarded_registers[] = {
        0x624a, 0x61e1, 0x6189, 0x61a3, 0x61a4, 0x61a5, 0x61a8, 0x61a9, 0x61b8, 0x61b9
    };
    if (argc == 2 && !strcmp(argv[1], "--vectors")) { emit_vectors(); return 0; }
    assert(argc == 1);
    assert(sizeof(radio_rx_frame_t) == 128);
    reset(5, 0xa5, 0xe9, 0);
    assert(run(10000, 1000) == RADIO_RX_OK); need = diag.value.polls;
    reset(5, 0xa5, 0xe9, 0); assert(run(10000, (uint16_t)need) == RADIO_RX_OK);
    reset(5, 0xa5, 0xe9, 0); assert(run(10000, (uint16_t)(need-1)) == RADIO_RX_POLL_LIMIT);
    for (length = 3; length <= 127; length++) {
        for (value = 0; value < 256; value++) {
            reset(length, (uint8_t)value, (uint8_t)(value | 128), 127-length);
            assert(run(100000, 1000) == RADIO_RX_OK);
            assert(diag.value.discarded_bytes == 127-length);
        }
    }
    for (value = 0; value < 128; value++) {
        reset(5, 0x81, (uint8_t)value, 0);
        assert(run(10000, 1000) == RADIO_RX_BAD_CRC && !radio_rx_fault && mode == 0);
    }
    for (value = 0; value < 256; value++) {
        reset(5, 0, 128, 0); command = value;
        assert(run(10000, 1000) == (value >= 11 && value <= 26 ? RADIO_RX_OK : RADIO_RX_INVALID_ARGUMENT));
        if (value < 11 || value > 26) assert(!reads_total && !writes_total &&
            !memcmp(&diag.value, &saved_diag, sizeof(saved_diag)));
    }
    for (value = 0; value < 65536; value++) {
        reset(5, 0, 128, 0); out_address = (uint16_t)value;
        if (value <= 0x300 || value >= 0x1d81 || (value < 0x500 + sizeof(diag.value) && value + 128 > 0x500) ||
            (value <= 0x1d00 && value + 128 > 0x1d00)) {
            radio_rx_result_t result = run(10000, 1000);
            assert(result == RADIO_RX_INVALID_RANGE || result == RADIO_RX_BUFFER_OWNERSHIP);
            assert(!reads_total && !writes_total);
        }
        reset(5, 0, 128, 0); diag_address = (uint16_t)value;
        if (value <= 0x300 || value + sizeof(diag.value) > 0x1e00 ||
            (value < 0x480 && value + sizeof(diag.value) > 0x400) ||
            (value <= 0x1d00 && value + sizeof(diag.value) > 0x1d00)) {
            radio_rx_result_t result = run(10000, 1000);
            assert(result == RADIO_RX_INVALID_RANGE || result == RADIO_RX_BUFFER_OWNERSHIP);
            assert(!reads_total && !writes_total);
        }
    }
    for (i = 0; i < sizeof(guarded_registers)/sizeof(guarded_registers[0]); i++) {
        for (value = 0; value < 8; value++) {
            reset(5, 0, 128, 0); XR(guarded_registers[i]) ^= (uint8_t)(1u << value);
            if (guarded_registers[i] == 0x61e1 && value < 5) continue;
            assert(run(10000, 1000) == RADIO_RX_UNSUPPORTED_STATE && !writes_total);
        }
    }
    for (i = 0; i < 8; i++) {
        reset(5, 0, 128, 0); SOC_RFERRF = (uint8_t)(1u << i);
        assert(run(10000, 1000) == RADIO_RX_CONTROLLER_ERROR && !writes_total);
        reset(5, 0, 128, 0); SOC_RFIRQF1 = (uint8_t)(1u << i);
        assert(run(10000, 1000) == (i == 2 ? RADIO_RX_OK : RADIO_RX_STATE_CHANGED));
    }
    for (i = 1; i <= 10; i++) {
        reset(5, 0, 128, 0); ignored_config = i;
        if (i == 1 || i == 6) continue;
        assert(run(10000, 1000) == RADIO_RX_STATE_CHANGED && !on_writes);
    }
    for (i = 0; i < need; i++) {
        reset(5, 0, 128, 0);
        assert(run(10000, (uint16_t)i) == (i ? RADIO_RX_POLL_LIMIT : RADIO_RX_INVALID_ARGUMENT));
    }
    reset(5, 0, 128, 0); cal_delay = 4; arrival_delay = 5; stop_delay = 3; flush_delay = 2;
    assert(run(10000, 1000) == RADIO_RX_OK);
    reset(5, 0, 128, 0); silent_channel = 1; tick_step = 0;
    assert(run(10000, 100) == RADIO_RX_POLL_LIMIT && on_writes && !stop_writes);
    reset(5, 0, 128, 0); silent_channel = 1;
    assert(run(50, 1000) == RADIO_RX_TIMEOUT && diag.value.elapsed_ticks == 50);
    reset(5, 0, 128, 0); ticks = 0xfffff0;
    assert(run(10000, 1000) == RADIO_RX_OK);
    reset(5, 0, 128, 0); tick_step = 0x800000;
    assert(run(10000, 1000) == RADIO_RX_COUNTER_RANGE && !writes_total);
    reset(5, 0, 128, 0); forced_read_count = 1;
    assert(run(10000, 1000) == RADIO_RX_COUNT_ERROR && rd == 1);
    for (value = 0; value < 256; value++) {
        if (value >= 3 && value <= 127) continue;
        reset(5, 0, 128, 0); wire[0] = (uint8_t)value;
        assert(run(10000, 1000) == RADIO_RX_BAD_LENGTH && rd == 1);
    }
    reset(5, 0, 128, 0); wire[0] = 127;
    assert(run(10000, 1000) == RADIO_RX_COUNT_ERROR && rd == 1);
    for (i = 1; i < need; i++) {
        reset(5, 0, 128, 0); inject_sample = i; inject_address = 0xbf; inject_value = 4;
        assert(run(10000, 1000) == RADIO_RX_CONTROLLER_ERROR);
    }
    for (i = 2; i <= 21; i++) {
        reset(5, 0, 128, 0); inject_sample = i; inject_address = 0x618b; inject_value = 128;
        assert(run(10000, 1000) == RADIO_RX_STATE_CHANGED && !on_writes);
    }
    reset(5, 0, 128, 0); assert(run(0, 1000) == RADIO_RX_INVALID_ARGUMENT);
    reset(5, 0, 128, 0); assert(run(0x800000, 1000) == RADIO_RX_INVALID_ARGUMENT);
    reset(5, 0, 128, 0); assert(run(0xffffffff, 1000) == RADIO_RX_INVALID_ARGUMENT);
    reset(5, 0, 128, 0);
    assert(radio_rx_receive_init(15, 1000, 1000, NULL, &diag.value) == RADIO_RX_INVALID_ARGUMENT);
    assert(radio_rx_receive_init(15, 1000, 1000, &frame.value, NULL) == RADIO_RX_INVALID_ARGUMENT);
    assert(!reads_total && !writes_total);
    reset(5, 0x81, 0x69, 0);
    assert(run(10000, 1000) == RADIO_RX_BAD_CRC);
    config_writes = on_writes = stop_writes = flush_writes = 0; wire[5] |= 128;
    assert(run(10000, 1000) == RADIO_RX_OK);
    config_writes = on_writes = stop_writes = flush_writes = 0;
    assert(run(10000, 1000) == RADIO_RX_OK);
    printf("Passive RX: %u strict host cases, bounded FIFO/CRC/metadata and no-TX writes PASS (synthetic only).\n", cases);
    return 0;
}
#endif
