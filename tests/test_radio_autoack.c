/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 * Original synthetic controller, not an RF/filter/timing implementation.
 * The standalone SDCC executable must NEVER be flashed.
 */
#include "radio_autoack.h"
#include "timebase.h"

#if defined(__SDCC)
volatile MCU_XDATA MCU_AT(0x1e00) uint8_t radio_autoack_test_result[8];
MCU_XDATA radio_autoack_config_t radio_autoack_test_config;
MCU_XDATA radio_autoack_frame_t radio_autoack_test_frame;
MCU_XDATA uint8_t radio_autoack_test_operation, radio_autoack_test_return;
MCU_XDATA uint16_t radio_autoack_test_config_ptr, radio_autoack_test_output_ptr;
MCU_XDATA uint32_t radio_autoack_test_timeout;
MCU_XDATA uint16_t radio_autoack_test_limit;
MCU_XDATA uint16_t radio_autoack_test_diag;

void radio_autoack_test_cycle(void)
{
    __asm
        .globl _radio_autoack_test_before
    _radio_autoack_test_before:
        nop
    __endasm;
    if (radio_autoack_test_operation == 0)
        radio_autoack_test_return = radio_autoack_acquire(
            (const radio_autoack_config_t MCU_XDATA *)radio_autoack_test_config_ptr,
            radio_autoack_test_timeout, radio_autoack_test_limit);
    else if (radio_autoack_test_operation == 1)
        radio_autoack_test_return = radio_autoack_receive(
            radio_autoack_test_timeout, radio_autoack_test_limit,
            (radio_autoack_frame_t MCU_XDATA *)radio_autoack_test_output_ptr);
    else if (radio_autoack_test_operation == 2)
        radio_autoack_test_return = radio_autoack_stop(
            radio_autoack_test_timeout, radio_autoack_test_limit);
    else radio_autoack_test_return = radio_autoack_resume(
        radio_autoack_test_timeout, radio_autoack_test_limit);
    radio_autoack_test_diag = MMIO_XADDRESS(radio_autoack_diagnostic());
    __asm
        .globl _radio_autoack_test_done
    _radio_autoack_test_done:
        nop
    __endasm;
}

void main(void)
{
    radio_autoack_test_result[0] = 'A'; radio_autoack_test_result[1] = 'C';
    radio_autoack_test_result[2] = 'K'; radio_autoack_test_result[3] = '1';
    radio_autoack_test_result[4] = 1; radio_autoack_test_result[5] = 8;
    radio_autoack_test_result[6] = 0; radio_autoack_test_result[7] = 0;
    for (;;) radio_autoack_test_cycle();
}
#else
#include "host_mmio.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern uint8_t radio_autoack_state, radio_autoack_fault, radio_autoack_reserved_end;
uint8_t _gptrput_PARM_2;
uint8_t __memcpy_PARM_2[3];
static struct { uint8_t before[4]; radio_autoack_config_t value; uint8_t after[4]; } config;
static struct { uint8_t before[4]; radio_autoack_frame_t value; uint8_t after[4]; } frame;
static uint8_t xregs[0x300], fifo[128], lengths[24];
static unsigned head, tail, count, packets, packet_head, packet_tail, remaining;
static unsigned mode, phase, samples, rfd_reads, accesses, calls, cases;
static unsigned cal_delay, stop_delay, ack_delay, stop_receive, stop_ack;
static unsigned hold_stop, suppress_idle, ready_active, ignored_clear;
static unsigned ignored_enable, arrival_on_ready;
static unsigned corrupt_write, config_writes, after_reads, arrive_after, corrupt_head, corrupt_count, corrupt_tail;
static uint8_t fscal, corrupt_mask;
static uint32_t ticks, tick_step, latched;
static unsigned printing, trace_count, step_count, scenario_id;
static uint16_t config_address, output_address;
static uint16_t normal_config = 0x600, normal_output = 0x700, reserved = 0x500, helper = 0x1d00;
static const uint16_t settings[] = {
    0x6180, 0x6181, 0x6182, 0x6189, 0x618a, 0x6194, 0x6195,
    0x61b2, 0x61fa, 0x61ae, 0x618f, 0x6190, 0x6191
};
static const uint8_t values[] = {1, 0x70, 0, 0x60, 0, 0x7f, 0, 0x15, 9, 0, 0, 5, 0x69};
#define XR(a) xregs[(a) - 0x6100u]
#define CASE_COUNT 157u

static void logs(void)
{
    assert(read_count + xread_count <= 1);
    read_count = xread_count = 0;
    assert(!write_count && !xwrite_count);
}

static void trace(char kind, unsigned address, unsigned value)
{
    accesses++;
    if (printing) printf("%s[\"%c\",%u,%u]", trace_count++ ? "," : "", kind, address, value);
}

static void signals(void)
{
    XR(0x619b) = (uint8_t)count;
    XR(0x619d) = (uint8_t)head; XR(0x619e) = (uint8_t)tail;
    XR(0x619f) = (uint8_t)head;
    XR(0x6193) = (XR(0x6193) & 0x3f) | (count ? 0x80 : 0) | (packets ? 0x40 : 0);
    if (count) XR(0x619a) = fifo[head];
}

static void enqueue(unsigned length, unsigned crc, unsigned seed)
{
    unsigned i;
    assert(length >= 5 && length <= 127 && count + length + 1 <= 128 && packets < 24);
    lengths[packet_tail] = (uint8_t)(length + 1);
    packet_tail = (packet_tail + 1) % 24;
    if (!packets) remaining = length + 1;
    packets++;
    for (i = 0; i <= length; i++) {
        fifo[tail] = (uint8_t)(i == 0 ? length : i == length ? crc :
            i == length - 1 ? 0x81 : (i ^ seed));
        if (length == 5 && i >= 1 && i <= 3)
            fifo[tail] = (uint8_t)(i == 1 ? 2 : i == 2 ? 0 : seed);
        if (length >= 9 && i >= 1 && i <= 7) {
            if (i == 1) fifo[tail] = (uint8_t)(1u | (seed & 0x38u));
            else if (i == 2) fifo[tail] = 8;
            else if (i == 3) fifo[tail] = (uint8_t)seed;
            else if (i == 4) fifo[tail] = (uint8_t)config.value.pan;
            else if (i == 5) fifo[tail] = (uint8_t)(config.value.pan >> 8);
            else fifo[tail] = (uint8_t)(seed & 0x40u ? 255 :
                i == 6 ? config.value.short_address : config.value.short_address >> 8);
        }
        tail = (tail + 1) & 127; count++;
    }
    signals(); SOC_RFIRQF0 |= 0x66;
}

static void advance(void)
{
    samples++; phase++;
    if (mode == 1 && phase > cal_delay) {
        XR(0x6192) = 0; XR(0x6193) = (XR(0x6193) & 0xc0) | 4 | (ready_active ? 1 : 0);
        XR(0x6199) = 1; XR(0x61ae) = fscal; mode = 2;
        if (arrival_on_ready) {
            arrival_on_ready = 0;
            enqueue(11, 0xe9, 0x38);
        }
    } else if (mode == 3 && !hold_stop && phase > stop_delay) {
        if (stop_receive) {
            enqueue(11, 0xe9, 0x38);
            mode = 4; phase = 0; XR(0x6192) = 0x40;
            XR(0x6193) = (XR(0x6193) & 0xc0) | 6;
        } else if (stop_ack) {
            mode = 4; phase = 0; XR(0x6192) = 0x40;
            XR(0x6193) = (XR(0x6193) & 0xc0) | 6;
        } else {
            mode = 5; XR(0x6192) = 0; XR(0x6193) &= 0xc0;
            if (!suppress_idle) SOC_RFIRQF1 |= 4;
        }
    } else if (mode == 4 && !hold_stop && phase > ack_delay) {
        assert(XR(0x6189) == 0x60);
        mode = 5; XR(0x6192) = 0; XR(0x6193) &= 0xc0;
        SOC_RFIRQF1 |= 1;
        if (!suppress_idle) SOC_RFIRQF1 |= 4;
    }
}

static uint8_t xload(uint16_t address)
{
    logs();
    assert(address >= 0x6100 && address < 0x6300);
    if (address == 0x624a) advance();
    trace('r', address, XR(address));
    return XR(address);
}

static uint8_t load(uint8_t address, uint8_t value)
{
    logs();
    if (address == SOC_ST0_ADDRESS) {
        latched = ticks; ticks = (ticks + tick_step) & TIMEBASE_TICKS_MASK;
        value = (uint8_t)latched;
    } else if (address == SOC_ST1_ADDRESS) value = (uint8_t)(latched >> 8);
    else if (address == SOC_ST2_ADDRESS) value = (uint8_t)(latched >> 16);
    else if (address == SOC_RFD_ADDRESS) {
        assert(count && packets && remaining);
        value = fifo[head]; head = (head + 1) & 127; count--; rfd_reads++;
        if (!--remaining) {
            packets--; packet_head = (packet_head + 1) % 24;
            remaining = packets ? lengths[packet_head] : 0;
        }
        signals();
        if (arrive_after && rfd_reads == arrive_after) enqueue(5, 0xff, 0x12);
        if (after_reads && rfd_reads == after_reads) SOC_RFERRF = 8;
        if (corrupt_head && rfd_reads == corrupt_head) XR(0x619d) ^= 1;
        if (corrupt_count && rfd_reads == corrupt_count) XR(0x619b) = 129;
        if (corrupt_tail && rfd_reads == corrupt_tail) XR(0x619e) ^= 1;
    }
    trace('r', address, value);
    return value;
}

static void store(uint8_t address, uint8_t before, uint8_t value)
{
    assert(write_count == 1 && writes[0].address == address &&
           writes[0].before == before && writes[0].after == value);
    write_count = 0; logs();
    assert(address == SOC_RFIRQF1_ADDRESS && value == 0x3b && mode == 2);
    SOC_RFIRQF1 = ignored_clear ? before : before & value;
    trace('w', address, value);
}

static void xstore(uint16_t address, uint8_t value)
{
    unsigned index;
    assert(xwrite_count == 1 && xwrites[0].address == address && xwrites[0].value == value);
    xwrite_count = 0; logs(); trace('w', address, value);
    if (address == 0x618c) {
        assert(value == 1 && (mode == 0 || mode == 5) &&
               config_writes == 25 && !XR(0x618b) && !count && !packets);
        assert(XR(0x6189) == 0x60 && !XR(0x618a) && !XR(0x6182) && !XR(0x6195));
        if (ignored_enable) return;
        XR(0x618b) = 1; XR(0x6192) = 0x40; XR(0x6193) = 1; XR(0x6199) = 0;
        mode = 1; phase = 0;
    } else if (address == 0x618d) {
        assert(value == 1 && mode == 2 && XR(0x618b) == 1 && XR(0x6189) == 0x60);
        XR(0x618b) = 0; SOC_RFIRQF0 |= 0x80; mode = 3; phase = 0;
    } else {
        assert(!mode);
        index = config_writes++;
        assert(index < 25 && address == (index < 12 ? 0x616a + index : settings[index - 12]));
        if (index < 8) assert(value == config.value.ieee[index]);
        else if (index == 8) assert(value == (uint8_t)config.value.pan);
        else if (index == 9) assert(value == config.value.pan >> 8);
        else if (index == 10) assert(value == (uint8_t)config.value.short_address);
        else if (index == 11) assert(value == config.value.short_address >> 8);
        else assert(value == (index == 22 ? 11 + 5 * (config.value.channel - 11) : values[index - 12]));
        XR(address) = value ^ (config_writes == corrupt_write ? corrupt_mask : 0);
    }
}

static uint16_t xaddress(const volatile void *object)
{
    if (object == &config.value) return config_address;
    if (object == &frame.value) return output_address;
    if (object == &radio_autoack_reserved_end) return reserved;
    if (object == &_gptrput_PARM_2) return helper;
    if (object == __memcpy_PARM_2) return (uint16_t)(helper - 11u);
    assert(0); return 0;
}

static void reset(void)
{
    unsigned i;
    host_mmio_reset(); radio_autoack_fault = radio_autoack_state = 0;
    /* Synthetic full-reset RAM initialization only, never between API calls. */
    memset((void *)radio_autoack_diagnostic(), 0, sizeof(radio_autoack_diagnostics_t));
    memset(xregs, 0, sizeof(xregs)); memset(fifo, 0, sizeof(fifo)); memset(lengths, 0, sizeof(lengths));
    memset(&config, 0xa5, sizeof(config)); memset(&frame, 0x69, sizeof(frame));
    for (i = 0; i < 8; i++) config.value.ieee[i] = (uint8_t)(0x10 + i);
    config.value.pan = 0x1234; config.value.short_address = 0x5678;
    config.value.channel = 26; config.value.power = 5;
    config_address = normal_config; output_address = normal_output;
    head = tail = count = packets = packet_head = packet_tail = remaining = 0;
    mode = phase = samples = rfd_reads = accesses = config_writes = 0;
    cal_delay = stop_delay = ack_delay = stop_receive = stop_ack = 0;
    hold_stop = suppress_idle = corrupt_write = after_reads = arrive_after = corrupt_head = ignored_clear = 0;
    ignored_enable = arrival_on_ready = 0;
    corrupt_count = corrupt_tail = 0;
    fscal = 0xfc; corrupt_mask = 1; ready_active = 1;
    ticks = latched = 0; tick_step = 1; step_count = 0;
    SOC_P0 = 0x5a; SOC_P1 = 0xa5; SOC_P2 = 0x69;
    SOC_SLEEPCMD = 4; SOC_CLKCONCMD = SOC_CLKCONSTA = 0x88;
    XR(0x624a) = 0xa5; XR(0x6189) = 0x40; XR(0x618a) = 1;
    XR(0x61a8) = 0x85; XR(0x61a9) = 0x14; XR(0x61b8) = 0x75; XR(0x61b9) = 8;
    XR(0x618e) = 0x0f; XR(0x61ae) = 0x2b;
    host_mmio_read_hook = load; host_mmio_write_hook = store;
    host_mmio_xread_hook = xload; host_mmio_xwrite_hook = xstore; host_mmio_xaddress_hook = xaddress;
}

static void hex(const void *data, unsigned size)
{
    const unsigned char *bytes = data;
    unsigned i;
    for (i = 0; i < size; i++) printf("%02x", bytes[i]);
}

static void emit_configuration(void)
{
    hex(config.value.ieee, 8);
    printf("%02x%02x%02x%02x%02x%02x", config.value.pan & 255, config.value.pan >> 8,
           config.value.short_address & 255, config.value.short_address >> 8,
           config.value.channel, config.value.power);
}

static void emit_diagnostics(void)
{
    const radio_autoack_diagnostics_t *d = radio_autoack_diagnostic();
    printf("%lu,%u,%u", (unsigned long)d->elapsed_ticks, d->polls, d->bytes_read);
#define FIELD(n) printf(",%u", d->n);
    FIELD(phase) FIELD(result) FIELD(writes) FIELD(verified) FIELD(sample_valid)
    FIELD(mask) FIELD(calibration) FIELD(signals) FIELD(count) FIELD(first) FIELD(last) FIELD(packet)
    FIELD(errors) FIELD(flags0) FIELD(flags1) FIELD(rssi_valid) FIELD(phr) FIELD(timebase_status)
#undef FIELD
}

static void call(unsigned operation, uint32_t timeout, uint16_t limit, radio_autoack_result_t expected)
{
    radio_autoack_result_t result;
    radio_autoack_frame_t saved = frame.value;
    radio_autoack_config_t saved_config = config.value;
    radio_autoack_diagnostics_t diagnostic;
    uint8_t expected_body[125], expected_length, expected_rssi, expected_crc;
    unsigned i, initial_reads = rfd_reads, initial_accesses = accesses, previous_fault = radio_autoack_fault;
    unsigned previous_state = radio_autoack_state;
    expected_length = fifo[head] & 127u;
    for (i = 0; i < 125; i++) expected_body[i] = fifo[(head + 1 + i) & 127];
    expected_rssi = fifo[(head + expected_length - 1) & 127];
    expected_crc = fifo[(head + expected_length) & 127];
    memcpy(&diagnostic, radio_autoack_diagnostic(), sizeof(diagnostic));
    if (printing) {
        printf("%s{\"operation\":%u,\"timeout\":%lu,\"limit\":%u,\"config_address\":%u,"
               "\"output_address\":%u,\"configuration\":\"", step_count ? "," : "", operation,
               (unsigned long)timeout, limit, config_address, output_address);
        emit_configuration(); printf("\",\"initial\":{");
#define EMIT_SFR(n, a) printf("\"%u\":%u,", a, n);
        CC2530_REGISTER_LIST(EMIT_SFR)
#undef EMIT_SFR
        for (i = 0; i < sizeof(xregs); i++)
            printf("\"%u\":%u%s", 0x6100 + i, xregs[i], i + 1 == sizeof(xregs) ? "" : ",");
        printf("},\"events\":["); trace_count = 0;
    }
    if (operation == 0) result = radio_autoack_acquire(config_address ? &config.value : NULL, timeout, limit);
    else if (operation == 1) result = radio_autoack_receive(timeout, limit, output_address ? &frame.value : NULL);
    else if (operation == 2) result = radio_autoack_stop(timeout, limit);
    else result = radio_autoack_resume(timeout, limit);
    if (result != expected)
        fprintf(stderr, "case%u step%u op%u expected%u got%u phase%u polls%u\n",
                scenario_id, step_count, operation, expected, result,
                radio_autoack_diagnostic()->phase, radio_autoack_diagnostic()->polls);
    assert(result == expected); calls++; step_count++; logs();
    assert(!memcmp(&saved_config, &config.value, sizeof(saved_config)));
    for (i = 0; i < 4; i++)
        assert(config.before[i] == 0xa5 && config.after[i] == 0xa5 &&
               frame.before[i] == 0x69 && frame.after[i] == 0x69);
    if (result == RADIO_AUTOACK_FRAME || result == RADIO_AUTOACK_BAD_CRC) {
        assert(rfd_reads - initial_reads == expected_length + 1u);
        assert(frame.value.length == expected_length - 2 &&
               frame.value.rssi_raw == expected_rssi && frame.value.crc_correlation == expected_crc);
        assert(!memcmp(frame.value.body, expected_body, expected_length - 2));
        assert(!memcmp(frame.value.body + expected_length - 2, saved.body + expected_length - 2,
                       127 - expected_length));
        assert(!!(expected_crc & 128) == (result == RADIO_AUTOACK_FRAME));
    } else assert(!memcmp(&frame.value, &saved, sizeof(saved)));
    if (previous_fault || (result >= RADIO_AUTOACK_INVALID_ARGUMENT && result <= RADIO_AUTOACK_STATE)) {
        assert(accesses == initial_accesses);
        assert(radio_autoack_state == previous_state && radio_autoack_fault == previous_fault);
        assert(!memcmp(&diagnostic, radio_autoack_diagnostic(), sizeof(diagnostic)));
    } else {
        assert(radio_autoack_diagnostic()->polls <= limit);
        assert(radio_autoack_diagnostic()->result == result);
    }
    if (result >= RADIO_AUTOACK_UNSUPPORTED_STATE)
        assert(radio_autoack_state == RADIO_AUTOACK_FAULT && radio_autoack_fault == result);
    if (result == RADIO_AUTOACK_STOPPED)
        assert(mode == 5 && !count && !packets && !(XR(0x6193) & 0xe7) &&
               !XR(0x618b) && XR(0x6189) == 0x60);
    if (operation == 3 && result == RADIO_AUTOACK_READY)
        assert(radio_autoack_state == RADIO_AUTOACK_RX && mode == 2 &&
               config_writes == 25 && radio_autoack_diagnostic()->phase == 10 &&
               radio_autoack_diagnostic()->writes == 1 &&
               !radio_autoack_diagnostic()->verified);
    if (printing) {
        printf("],\"result\":%u,\"fault\":%u,\"state\":%u,\"frame\":\"",
               result, radio_autoack_fault, radio_autoack_state);
        hex(&frame.value, sizeof(frame.value)); printf("\",\"diagnostics\":[");
        emit_diagnostics(); printf("]}");
    }
}

#define CALL(op, result) call(op, 10000, 1000, result)
static void scenario(unsigned n)
{
    unsigned i;
    scenario_id = n; cases++; reset();
    if (printing) printf("{\"case\":%u,\"steps\":[", n);
    if (n >= 125) {
        CALL(3, RADIO_AUTOACK_STATE);
        CALL(0, RADIO_AUTOACK_READY);
        CALL(3, RADIO_AUTOACK_STATE);
        if (n == 126 || n == 128) {
            head = tail = 119; signals();
            enqueue(n == 128 ? 127 : 11, 128, 0x38);
            CALL(2, RADIO_AUTOACK_DRAIN);
            CALL(3, RADIO_AUTOACK_STATE);
            CALL(1, RADIO_AUTOACK_FRAME);
        }
        if (n == 129) {
            stop_ack = 1; ack_delay = 3; XR(0x6193) = 0x26;
        }
        if (n == 148) SOC_RFIRQF1 = 7;
        CALL(2, RADIO_AUTOACK_STOPPED);
        CALL(0, RADIO_AUTOACK_STATE);
        CALL(1, RADIO_AUTOACK_STATE);
        CALL(2, RADIO_AUTOACK_STATE);
        if (n == 125) {
            call(3, 0, 1, RADIO_AUTOACK_INVALID_ARGUMENT);
            call(3, 0x800000, 1, RADIO_AUTOACK_INVALID_ARGUMENT);
            call(3, 0xffffffff, 1, RADIO_AUTOACK_INVALID_ARGUMENT);
            call(3, 1000, 0, RADIO_AUTOACK_INVALID_ARGUMENT);
        }
        if (n == 130) {
            count = tail = 3; signals();
            CALL(3, RADIO_AUTOACK_FIFO_ERROR);
        } else if (n == 131 || n == 137 || n == 138) {
            cal_delay = 3;
            if (n == 137) tick_step = 0;
            call(3, 10000, n == 138 ? 4 : 5,
                 n == 138 ? RADIO_AUTOACK_WORK_LIMIT : RADIO_AUTOACK_READY);
        } else if (n == 132 || n == 133 || n == 134) {
            if (n == 134) { ready_active = 0; tick_step = 0; }
            call(3, 10000, n == 132 ? 1 : n == 133 ? 2 : 7,
                 n == 133 ? RADIO_AUTOACK_READY : RADIO_AUTOACK_WORK_LIMIT);
        } else if (n == 135 || n == 136) {
            call(3, n - 134, 1000, RADIO_AUTOACK_TIMEOUT);
        } else if (n == 139) {
            ignored_enable = 1; CALL(3, RADIO_AUTOACK_STATE_CHANGED);
        } else if (n == 140) {
            XR(0x6180) ^= 1; CALL(3, RADIO_AUTOACK_STATE_CHANGED);
        } else if (n == 141) {
            SOC_CLKCONCMD = SOC_CLKCONSTA = 8; CALL(3, RADIO_AUTOACK_STATE_CHANGED);
        } else if (n == 142) {
            SOC_DMAARM = 1; CALL(3, RADIO_AUTOACK_UNSUPPORTED_STATE);
        } else if (n == 143) {
            XR(0x6193) = 0x26; CALL(3, RADIO_AUTOACK_STATE_CHANGED);
        } else if (n == 144) {
            enqueue(5, 128, 0x44); CALL(3, RADIO_AUTOACK_FIFO_ERROR);
        } else if (n == 145) {
            SOC_RFIRQF1 &= 0xfb; CALL(3, RADIO_AUTOACK_STATE_CHANGED);
        } else if (n == 146) {
            SOC_RFERRF = 8; CALL(3, RADIO_AUTOACK_CONTROLLER_ERROR);
        } else if (n == 147) {
            XR(0x619b) = 129; CALL(3, RADIO_AUTOACK_FIFO_ERROR);
        } else if (n == 151) {
            tick_step = 0x800000; CALL(3, RADIO_AUTOACK_TIME_ERROR);
        } else if (n == 152) {
            call(3, 3, 2, RADIO_AUTOACK_READY);
        } else if (n == 153) {
            XR(0x619e) ^= 1; CALL(3, RADIO_AUTOACK_FIFO_ERROR);
        } else if (n == 154) {
            XR(0x6193) = 5; CALL(3, RADIO_AUTOACK_STATE_CHANGED);
        } else if (n == 155) {
            XR(0x618b) = 2; CALL(3, RADIO_AUTOACK_STATE_CHANGED);
        } else if (n == 156) {
            XR(0x6192) = 0x40; CALL(3, RADIO_AUTOACK_STATE_CHANGED);
        } else {
            if (n == 127) arrival_on_ready = 1;
            if (n == 149) { ticks = 0xfffffe; cal_delay = 2; }
            CALL(3, RADIO_AUTOACK_READY);
        }
        if (!radio_autoack_fault) {
            CALL(3, RADIO_AUTOACK_STATE);
            if (n == 127) CALL(1, RADIO_AUTOACK_FRAME);
            else CALL(1, RADIO_AUTOACK_EMPTY);
            if (n == 148) assert(SOC_RFIRQF1 == 7);
            for (i = 0; i < (n == 150 ? 4u : 1u); i++) {
                enqueue(11, 128, 0x38);
                CALL(2, RADIO_AUTOACK_DRAIN);
                CALL(3, RADIO_AUTOACK_STATE);
                CALL(1, RADIO_AUTOACK_FRAME);
                CALL(2, RADIO_AUTOACK_STOPPED);
                CALL(3, RADIO_AUTOACK_READY);
            }
            CALL(2, RADIO_AUTOACK_STOPPED);
        }
        goto end;
    }
    if (n >= 114 && n < 125) {
        unsigned output = n >= 116;
        unsigned first = (n - (output ? 116u : 114u)) * 16u;
        unsigned last = output ? 139u : 25u;
        if (first + 16u < last) last = first + 16u;
        if (output) CALL(0, RADIO_AUTOACK_READY);
        for (i = first; i < last; i++) {
            if (output) output_address = (uint16_t)(helper - 138u + i);
            else config_address = (uint16_t)(helper - 24u + i);
            CALL(output, RADIO_AUTOACK_BUFFER_OWNERSHIP);
        }
        config_address = normal_config; output_address = normal_output;
        if (!output) CALL(0, RADIO_AUTOACK_READY);
        CALL(1, RADIO_AUTOACK_EMPTY); CALL(2, RADIO_AUTOACK_STOPPED);
        goto end;
    }
    if (n >= 10 && n < 35) {
        corrupt_write = n - 9;
        CALL(0, RADIO_AUTOACK_STATE_CHANGED);
    } else {
        if (n == 0 || n == 94) {
            CALL(1, RADIO_AUTOACK_STATE); CALL(2, RADIO_AUTOACK_STATE);
            call(0, 0, 1, RADIO_AUTOACK_INVALID_ARGUMENT);
            config_address = 0; CALL(0, RADIO_AUTOACK_INVALID_ARGUMENT);
            config_address = reserved; CALL(0, RADIO_AUTOACK_BUFFER_OWNERSHIP);
            config_address = 0x1df3; CALL(0, RADIO_AUTOACK_INVALID_RANGE);
            config_address = helper; CALL(0, RADIO_AUTOACK_BUFFER_OWNERSHIP);
            config_address = normal_config;
        }
        if (n == 4) { ticks = 0xfffff0; cal_delay = 3; }
        if (n == 6) { ready_active = 0; call(0, 10000, 32, RADIO_AUTOACK_WORK_LIMIT); goto end; }
        if (n == 7) { call(0, 1, 1000, RADIO_AUTOACK_TIMEOUT); goto end; }
        if (n == 8) { tick_step = 0x800000; CALL(0, RADIO_AUTOACK_TIME_ERROR); goto end; }
        if (n == 9) { call(0, 1000, 1, RADIO_AUTOACK_WORK_LIMIT); goto end; }
        if (n >= 82 && n < 90) {
            static const uint16_t guarded[] = {0x624a, 0x61e1, 0x61a3, 0x61a4, 0x61a5, 0x61a8, 0x61b9, 0x618e};
            XR(guarded[n - 82]) ^= 1;
            CALL(0, RADIO_AUTOACK_UNSUPPORTED_STATE); goto end;
        }
        if (n == 90) { SOC_DMAARM = 1; CALL(0, RADIO_AUTOACK_UNSUPPORTED_STATE); goto end; }
        if (n == 91) { SOC_IEN2 = 1; CALL(0, RADIO_AUTOACK_UNSUPPORTED_STATE); goto end; }
        if (n == 92) { fscal = 0xfd; CALL(0, RADIO_AUTOACK_STATE_CHANGED); goto end; }
        if (n == 93) { cal_delay = 100; call(0, 10000, 32, RADIO_AUTOACK_WORK_LIMIT); goto end; }
        if (n >= 110 && n <= 112) {
            for (i = 0; i < 8; i++)
                config.value.ieee[i] = (uint8_t)(n == 110 ? 0 : n == 111 ? 255 : 0xf0u - 17u * i);
            config.value.pan = n == 110 ? 0 : n == 111 ? 0xffff : 0xa10f;
            config.value.short_address = n == 110 ? 0 : n == 111 ? 0xffff : 0x08f1;
            config.value.channel = (uint8_t)(n == 110 ? 11 : n == 111 ? 26 : 15);
        }
        CALL(0, RADIO_AUTOACK_READY);
        if (n >= 35 && n < 60) {
            i = n - 35; XR(i < 12 ? 0x616a + i : settings[i - 12]) ^= 1;
            CALL(1, RADIO_AUTOACK_STATE_CHANGED);
        } else if (n == 0 || n == 94) {
            CALL(0, RADIO_AUTOACK_STATE); CALL(1, RADIO_AUTOACK_EMPTY);
            output_address = 0; CALL(1, RADIO_AUTOACK_INVALID_ARGUMENT);
            output_address = reserved; CALL(1, RADIO_AUTOACK_BUFFER_OWNERSHIP);
            output_address = helper; CALL(1, RADIO_AUTOACK_BUFFER_OWNERSHIP);
            output_address = 0x1d81; CALL(1, RADIO_AUTOACK_INVALID_RANGE);
            output_address = normal_output;
            enqueue(5, 0xff, 0x22); CALL(1, RADIO_AUTOACK_FRAME);
            enqueue(9, 0x69, 0x92); enqueue(11, 0x80, 0xe9);
            SOC_RFIRQF1 = 5;
            CALL(2, RADIO_AUTOACK_DRAIN);
            CALL(1, RADIO_AUTOACK_BAD_CRC); CALL(1, RADIO_AUTOACK_FRAME);
            CALL(1, RADIO_AUTOACK_EMPTY); CALL(2, RADIO_AUTOACK_STOPPED);
            CALL(0, RADIO_AUTOACK_STATE); CALL(1, RADIO_AUTOACK_STATE); CALL(2, RADIO_AUTOACK_STATE);
        } else if (n == 1) {
            head = tail = 119; signals();
            enqueue(127, 0xe9, 0xff); fifo[head] |= 128; XR(0x619a) = fifo[head];
            arrive_after = 8;
            CALL(1, RADIO_AUTOACK_FRAME); CALL(1, RADIO_AUTOACK_FRAME);
            CALL(2, RADIO_AUTOACK_STOPPED);
        } else if (n == 2 || n == 3 || n == 4) {
            stop_receive = n != 3; stop_ack = n == 3; stop_delay = 2; ack_delay = 3;
            XR(0x6193) = n == 3 ? 0x26 : 0x25;
            SOC_RFIRQF1 = 5;
            CALL(2, n == 3 ? RADIO_AUTOACK_STOPPED : RADIO_AUTOACK_DRAIN);
            if (n != 3) { CALL(1, RADIO_AUTOACK_FRAME); CALL(2, RADIO_AUTOACK_STOPPED); }
        } else if (n == 5) {
            enqueue(5, 0, 0); CALL(1, RADIO_AUTOACK_BAD_CRC);
            enqueue(5, 255, 0); CALL(1, RADIO_AUTOACK_FRAME);
            CALL(2, RADIO_AUTOACK_STOPPED);
        } else if (n >= 60 && n <= 62) {
            if (n == 60) SOC_RFERRF = 4;
            if (n == 61) XR(0x6193) = 0x45;
            if (n == 62) XR(0x619b) = 129;
            CALL(1, n == 62 ? RADIO_AUTOACK_FIFO_ERROR : RADIO_AUTOACK_CONTROLLER_ERROR);
        } else if (n >= 63 && n <= 67) {
            enqueue(11, 0x80, 0x98);
            if (n == 63) { fifo[head] = 4; XR(0x619a) = 4; }
            if (n == 64) XR(0x619b) = 5;
            if (n == 65) after_reads = 1;
            if (n == 66) after_reads = 12;
            if (n == 67) corrupt_head = 1;
            CALL(1, n == 65 || n == 66 ? RADIO_AUTOACK_CONTROLLER_ERROR : RADIO_AUTOACK_FIFO_ERROR);
        } else if (n == 68) {
            XR(0x6193) = 4; CALL(1, RADIO_AUTOACK_STATE_CHANGED);
        } else if (n == 69) {
            XR(0x618b) = 3; CALL(1, RADIO_AUTOACK_STATE_CHANGED);
        } else if (n == 70) {
            enqueue(11, 128, 0x18); call(1, 10000, 8, RADIO_AUTOACK_WORK_LIMIT);
        } else if (n == 71) {
            enqueue(11, 128, 0x18); call(1, 4, 1000, RADIO_AUTOACK_TIMEOUT);
        } else if (n == 72) {
            hold_stop = 1; tick_step = 0; call(2, 10000, 7, RADIO_AUTOACK_WORK_LIMIT);
        } else if (n == 73) {
            stop_ack = 1; ack_delay = 30; call(2, 6, 1000, RADIO_AUTOACK_TIMEOUT);
        } else if (n == 74) {
            suppress_idle = 1; SOC_RFIRQF1 = 5; call(2, 10000, 7, RADIO_AUTOACK_WORK_LIMIT);
        } else if (n == 75) {
            ignored_clear = 1; SOC_RFIRQF1 = 5; CALL(2, RADIO_AUTOACK_STATE_CHANGED);
        } else if (n == 76) {
            count = tail = 3; signals(); CALL(2, RADIO_AUTOACK_FIFO_ERROR);
        } else if (n == 77) {
            SOC_CLKCONCMD = SOC_CLKCONSTA = 0x08; CALL(1, RADIO_AUTOACK_STATE_CHANGED);
        } else if (n == 78) {
            enqueue(5, 128, 0x18); call(1, 10000, 8, RADIO_AUTOACK_FRAME);
            CALL(2, RADIO_AUTOACK_STOPPED);
        } else if (n == 79) {
            enqueue(5, 128, 0x18); call(1, 10000, 7, RADIO_AUTOACK_WORK_LIMIT);
        } else if (n == 80) {
            enqueue(5, 128, 0x18); CALL(2, RADIO_AUTOACK_DRAIN);
            XR(0x6193) &= 0xbf; CALL(1, RADIO_AUTOACK_FIFO_ERROR);
        } else if (n == 81) {
            SOC_RFIRQF0 = 0xfe; SOC_RFIRQF1 = 3;
            XR(0x619f) = 255; CALL(1, RADIO_AUTOACK_EMPTY);
            CALL(2, RADIO_AUTOACK_STOPPED);
        } else if (n >= 95 && n <= 99) {
            call(2, n == 98 ? 3 : n == 99 ? 4 : 10000,
                 (uint16_t)(n <= 97 ? n - 94 : 3),
                 n < 97 ? RADIO_AUTOACK_WORK_LIMIT :
                 n == 98 ? RADIO_AUTOACK_TIMEOUT : RADIO_AUTOACK_STOPPED);
        } else if (n == 100 || n == 101 || n == 103) {
            enqueue(11, 128, 0x38);
            if (n == 100) fifo[head] = 10;
            if (n == 101) corrupt_head = 12;
            if (n == 103) corrupt_count = 11;
            CALL(1, RADIO_AUTOACK_FIFO_ERROR);
        } else if (n == 102) {
            SOC_RFERRF = 8; CALL(2, RADIO_AUTOACK_CONTROLLER_ERROR);
        } else if (n == 104) {
            count = tail = 3; signals();
            CALL(1, RADIO_AUTOACK_EMPTY); CALL(2, RADIO_AUTOACK_FIFO_ERROR);
        } else if (n == 105) {
            for (i = 0; i < 21; i++) enqueue(5, 128, i);
            CALL(2, RADIO_AUTOACK_DRAIN);
            for (i = 0; i < 21; i++) CALL(1, RADIO_AUTOACK_FRAME);
            CALL(2, RADIO_AUTOACK_STOPPED);
        } else if (n == 106) {
            XR(0x619e) = 1; CALL(2, RADIO_AUTOACK_FIFO_ERROR);
        } else if (n == 107 || n == 108) {
            enqueue(11, 128, 0x38); CALL(2, RADIO_AUTOACK_DRAIN);
            if (n == 107) XR(0x619e) ^= 1;
            else corrupt_tail = 12;
            CALL(1, RADIO_AUTOACK_FIFO_ERROR);
        } else if (n == 109) {
            head = tail = 119; signals(); enqueue(127, 128, 0xff);
            CALL(2, RADIO_AUTOACK_DRAIN); CALL(1, RADIO_AUTOACK_FRAME);
            CALL(2, RADIO_AUTOACK_STOPPED);
        } else if (n >= 110 && n <= 113) {
            if (n == 113) memset(&config.value, 0, sizeof(config.value));
            CALL(1, RADIO_AUTOACK_EMPTY); CALL(2, RADIO_AUTOACK_STOPPED);
        } else assert(0);
    }
end:
    if (radio_autoack_fault) {
        radio_autoack_result_t fault = (radio_autoack_result_t)radio_autoack_fault;
        config_address = output_address = 0;
        call(0, 0, 0, fault); call(1, 0, 0, fault); call(2, 0, 0, fault);
        if (n >= 125) call(3, 0, 0, fault);
    }
    if (printing) puts("]}");
}

int main(int argc, char **argv)
{
    unsigned n, length, value, i;
    assert(sizeof(radio_autoack_config_t) == 14);
    assert(sizeof(radio_autoack_frame_t) == 128);
    if (argc == 7 && !strcmp(argv[1], "--vector")) {
        n = (unsigned)strtoul(argv[2], NULL, 0);
        normal_config = (uint16_t)strtoul(argv[3], NULL, 0);
        normal_output = (uint16_t)strtoul(argv[4], NULL, 0);
        reserved = (uint16_t)strtoul(argv[5], NULL, 0);
        helper = (uint16_t)strtoul(argv[6], NULL, 0);
        assert(n < CASE_COUNT); printing = 1; scenario(n); return 0;
    }
    assert(argc == 1);
    for (n = 0; n < CASE_COUNT; n++) scenario(n);
    reset(); CALL(0, RADIO_AUTOACK_READY);
    for (length = 5; length <= 127; length++)
        for (value = 0; value < 256; value++) {
            enqueue(length, value, value);
            CALL(1, value & 128 ? RADIO_AUTOACK_FRAME : RADIO_AUTOACK_BAD_CRC);
        }
    CALL(2, RADIO_AUTOACK_STOPPED);
    reset();
    for (value = 0; value < 65536; value++) {
        config_address = (uint16_t)value;
        if (!value) CALL(0, RADIO_AUTOACK_INVALID_ARGUMENT);
        else if (value > 0x1df2) CALL(0, RADIO_AUTOACK_INVALID_RANGE);
        else if (value <= reserved || (value <= helper &&
                 (value >= helper - 11u || helper - 11u - value < sizeof(config.value))))
            CALL(0, RADIO_AUTOACK_BUFFER_OWNERSHIP);
    }
    config_address = normal_config; CALL(0, RADIO_AUTOACK_READY);
    for (value = 0; value < 65536; value++) {
        output_address = (uint16_t)value;
        if (!value) CALL(1, RADIO_AUTOACK_INVALID_ARGUMENT);
        else if (value > 0x1d80) CALL(1, RADIO_AUTOACK_INVALID_RANGE);
        else if (value <= reserved || (value <= helper &&
                 (value >= helper - 11u || helper - 11u - value < sizeof(frame.value))))
            CALL(1, RADIO_AUTOACK_BUFFER_OWNERSHIP);
    }
    for (value = 0; value < 256; value++) {
        reset(); config.value.channel = (uint8_t)value;
        CALL(0, value >= 11 && value <= 26 ? RADIO_AUTOACK_READY : RADIO_AUTOACK_INVALID_ARGUMENT);
        reset(); config.value.power = (uint8_t)value;
        CALL(0, value == 5 ? RADIO_AUTOACK_READY : RADIO_AUTOACK_INVALID_ARGUMENT);
    }
    for (value = 0; value < 256; value += 4) {
        reset(); fscal = (uint8_t)value; CALL(0, RADIO_AUTOACK_READY);
        CALL(1, RADIO_AUTOACK_EMPTY); CALL(2, RADIO_AUTOACK_STOPPED);
    }
    for (i = 0; i < 12; i++) for (value = 0; value < 256; value++) {
        reset();
        if (i < 8) config.value.ieee[i] = (uint8_t)value;
        else if (i == 8) config.value.pan = (uint16_t)((config.value.pan & 0xff00u) | value);
        else if (i == 9) config.value.pan = (uint16_t)((config.value.pan & 0xffu) | (value << 8));
        else if (i == 10) config.value.short_address = (uint16_t)((config.value.short_address & 0xff00u) | value);
        else config.value.short_address = (uint16_t)((config.value.short_address & 0xffu) | (value << 8));
        CALL(0, RADIO_AUTOACK_READY);
    }
    for (i = 0; i < 25; i++) for (value = 0; value < 8; value++) {
        reset(); corrupt_write = i + 1; corrupt_mask = (uint8_t)(1u << value);
        CALL(0, i == 21 && value >= 2 ? RADIO_AUTOACK_READY : RADIO_AUTOACK_STATE_CHANGED);
        reset(); CALL(0, RADIO_AUTOACK_READY);
        XR(i < 12 ? 0x616a + i : settings[i - 12]) ^= (uint8_t)(1u << value);
        CALL(1, i == 21 && value >= 2 ? RADIO_AUTOACK_EMPTY : RADIO_AUTOACK_STATE_CHANGED);
    }
    for (i = 1; i <= 128; i++) {
        reset(); CALL(0, RADIO_AUTOACK_READY); enqueue(127, 128, 0xe9); after_reads = i;
        CALL(1, RADIO_AUTOACK_CONTROLLER_ERROR);
    }
    for (i = 0; i <= 27; i++) {
        reset();
        call(0, 10000, (uint16_t)i, i == 27 ? RADIO_AUTOACK_READY :
             i ? RADIO_AUTOACK_WORK_LIMIT : RADIO_AUTOACK_INVALID_ARGUMENT);
    }
    reset(); call(0, 0x800000, 1000, RADIO_AUTOACK_INVALID_ARGUMENT);
    reset(); call(0, 0xffffffff, 1000, RADIO_AUTOACK_INVALID_ARGUMENT);
    for (i = 0; i < 25; i++) for (value = 0; value < 8; value++) {
        reset(); CALL(0, RADIO_AUTOACK_READY); CALL(2, RADIO_AUTOACK_STOPPED);
        XR(i < 12 ? 0x616a + i : settings[i - 12]) ^= (uint8_t)(1u << value);
        CALL(3, i == 21 && value >= 2 ? RADIO_AUTOACK_READY : RADIO_AUTOACK_STATE_CHANGED);
    }
    reset(); CALL(0, RADIO_AUTOACK_READY); CALL(2, RADIO_AUTOACK_STOPPED);
    ready_active = 0; tick_step = 0;
    call(3, 10000, 65535, RADIO_AUTOACK_WORK_LIMIT);
    printf("AUTOACK: %u linked scenarios / %u native API calls; all lengths/CRC bytes, "
           "address/profile bits, FIFO/stop/rearm/failure/atomic guards PASS (synthetic only).\n", cases, calls);
    return 0;
}
#endif
