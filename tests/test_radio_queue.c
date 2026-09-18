/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "radio_queue.h"

typedef union {
    radio_rx_frame_t rx;
    radio_queue_tx_frame_t tx;
    uint8_t bytes[128];
} queue_test_buffer_t;

#if defined(__SDCC)
volatile MCU_XDATA MCU_AT(0x1e00) uint8_t radio_queue_test_result[8];
MCU_XDATA queue_test_buffer_t radio_queue_test_buffer;
MCU_XDATA radio_queue_status_t radio_queue_test_status;
MCU_XDATA uint8_t radio_queue_test_action, radio_queue_test_value, radio_queue_test_return;
MCU_XDATA uint8_t radio_queue_test_irq_count, radio_queue_test_irq_return;
MCU_XDATA uint32_t radio_queue_test_timeout;
MCU_XDATA uint16_t radio_queue_test_limit;

void radio_queue_test_interrupt(void) __interrupt(0)
{
    radio_queue_test_irq_return = radio_queue_request_rx(0xe9);
    radio_queue_test_irq_count++;
}

void radio_queue_test_cycle(void)
{
    __asm
        .globl _radio_queue_before
    _radio_queue_before:
        nop
    __endasm;
    switch (radio_queue_test_action) {
    case 0: radio_queue_test_return = radio_queue_request_rx(radio_queue_test_value); break;
    case 1: radio_queue_test_return = radio_queue_service(radio_queue_test_value, radio_queue_test_timeout,
                                                         radio_queue_test_limit); break;
    case 2: radio_queue_test_return = radio_queue_rx_read(&radio_queue_test_buffer.rx); break;
    case 3: radio_queue_test_return = radio_queue_tx_submit(radio_queue_test_buffer.bytes, radio_queue_test_value); break;
    case 4: radio_queue_test_return = radio_queue_tx_read(&radio_queue_test_buffer.tx); break;
    case 5: radio_queue_test_return = radio_queue_tx_cancel(); break;
    case 6: radio_queue_test_return = radio_queue_cancel_requests(); break;
    default: radio_queue_test_return = radio_queue_snapshot(&radio_queue_test_status); break;
    }
    __asm
        .globl _radio_queue_done
    _radio_queue_done:
        nop
    __endasm;
}

void main(void)
{
    uint8_t i;
    SOC_IEN0 = 0; SOC_IEN1 = 0; SOC_IEN2 = 0;
    radio_queue_test_result[0] = 'R'; radio_queue_test_result[1] = 'Q';
    radio_queue_test_result[2] = 'U'; radio_queue_test_result[3] = 'E';
    radio_queue_test_result[4] = 1; radio_queue_test_result[5] = 8;
    radio_queue_test_result[6] = radio_queue_test_result[7] = 0;
    for (i = 0; i < 128; i++) radio_queue_test_buffer.bytes[i] = 0xa5;
    for (;;) radio_queue_test_cycle();
}
#else
#include "host_mmio.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

extern uint8_t radio_rx_reserved_end;
uint8_t _gptrput_PARM_2;
extern volatile uint8_t radio_queue_fault, radio_queue_event_head, radio_queue_event_tail;
extern volatile uint8_t radio_queue_events[4], radio_queue_dropped;
extern radio_rx_frame_t radio_queue_rx[2];
extern radio_rx_diagnostics_t radio_queue_diagnostics;
extern uint8_t radio_queue_reserved_end;
static queue_test_buffer_t buffer;
static radio_queue_status_t status;
static unsigned io, calls;
static uint16_t caller_address = 0x800;
static struct { unsigned kind, address, value; } events[16384];
static unsigned event_count, event_index, replay, driver_started, prechecks;
static uint8_t expected_frames[5][128];

static void consume(void)
{
    assert(read_count + xread_count <= 1 && !write_count && !xwrite_count);
    read_count = xread_count = 0;
}

static uint8_t event(unsigned kind, unsigned address)
{
    assert(replay && driver_started && event_index < event_count);
    assert(events[event_index].kind == kind && events[event_index].address == address);
    return (uint8_t)events[event_index++].value;
}

static uint8_t load(uint8_t address, uint8_t value)
{
    static const uint8_t enables[3] = {0xa8, 0xb8, 0x9a};
    consume();
    io++;
    if (replay) {
        if (!driver_started && address != 0xc6) {
            assert(prechecks < 3 && address == enables[prechecks++] && !value);
        } else {
            assert(prechecks == 3);
            driver_started = 1;
            value = event(0, address);
        }
    } else assert(address == 0xa8);
    return value;
}

static uint8_t xload(uint16_t address)
{
    consume(); io++;
    return event(0, address);
}

static void store(uint8_t address, uint8_t before, uint8_t value)
{
    assert(write_count == 1 && writes[0].address == address &&
           writes[0].before == before && writes[0].after == value);
    write_count = 0; consume(); io++;
    if (replay) assert(value == event(1, address));
    else assert(address == 0xa8 && (value & 0x7f) == (before & 0x7f));
}

static void xstore(uint16_t address, uint8_t value)
{
    assert(xwrite_count == 1 && xwrites[0].address == address && xwrites[0].value == value);
    xwrite_count = 0; consume(); io++;
    assert(value == event(1, address));
}

static uint16_t address(const volatile void *object)
{
    if (object == &radio_rx_reserved_end) return 0x300;
    if (object == &radio_queue_reserved_end) return 0x700;
    if (object == &_gptrput_PARM_2) return 0x1d00;
    if (object == &radio_queue_rx[0]) return 0x400;
    if (object == &radio_queue_rx[1]) return 0x480;
    if (object == &radio_queue_diagnostics) return 0x500;
    if (object == &buffer || object == buffer.bytes) return caller_address;
    if (object == &status) return 0x900;
    assert(0); return 0;
}

static radio_rx_result_t receive(unsigned number)
{
    unsigned i, value, expected;
    radio_queue_result_t result;
    assert(scanf("%u %u", &expected, &event_count) == 2 && event_count && event_count <= 16384);
    for (i = 0; i < 128; i++) {
        assert(scanf("%u", &value) == 1 && value < 256);
        expected_frames[number][i] = (uint8_t)value;
    }
    for (i = 0; i < event_count; i++) {
        assert(scanf("%u %u %u", &events[i].kind, &events[i].address, &events[i].value) == 3);
        assert(events[i].kind <= 1 && events[i].address <= 65535 && events[i].value <= 255);
    }
    replay = 1; event_index = driver_started = prechecks = 0;
    result = radio_queue_service(15, 10000, 1000);
    consume(); replay = 0;
    assert(driver_started && event_index == event_count);
    assert(result == (expected == RADIO_RX_OK ? RADIO_QUEUE_OK :
        expected == RADIO_RX_BAD_CRC ? RADIO_QUEUE_BAD_CRC : RADIO_QUEUE_RADIO_FAILED));
    assert(radio_queue_snapshot(&status) == RADIO_QUEUE_OK && status.driver_result == expected);
    calls++;
    return (radio_rx_result_t)expected;
}

static void read_frame(unsigned number)
{
    unsigned i, length = expected_frames[number][0];
    memset(&buffer, 0xa5, sizeof(buffer));
    assert(radio_queue_rx_read(&buffer.rx) == RADIO_QUEUE_OK); calls++;
    assert(memcmp(&buffer.rx, expected_frames[number], length+3) == 0);
    for (i = length; i < 125; i++) assert(buffer.rx.body[i] == 0xa5);
}

static void rf_tests(void)
{
    unsigned i, before;
    SOC_CLKCONCMD = SOC_CLKCONSTA = 0x88; SOC_SLEEPCMD = 4;
    for (i = 0; i < 4; i++) assert(radio_queue_request_rx((uint8_t)(0x31+i)) == RADIO_QUEUE_OK);
    assert(radio_queue_request_rx(0xee) == RADIO_QUEUE_FULL);
    SOC_IEN0 = 0x80;
    assert(radio_queue_service(15, 10000, 1000) == RADIO_QUEUE_IRQ_ACTIVE);
    SOC_IEN0 = 0;
    assert(receive(0) == RADIO_RX_OK && status.last_cookie == 0x31 && status.rx_count == 1);
    assert(receive(1) == RADIO_RX_OK && status.last_cookie == 0x32 && status.rx_count == 2);
    before = io;
    assert(radio_queue_service(15, 10000, 1000) == RADIO_QUEUE_FULL && io == before);
    assert(radio_queue_snapshot(&status) == RADIO_QUEUE_OK && status.requests == 2);
    read_frame(0);
    assert(receive(2) == RADIO_RX_BAD_CRC && status.last_cookie == 0x33 && status.rx_count == 1 && status.bad_crc == 1);
    assert(receive(3) == RADIO_RX_OK && status.last_cookie == 0x34 && status.rx_count == 2);
    read_frame(1);
    assert(radio_queue_tx_submit(buffer.bytes, 5) == RADIO_QUEUE_OK);
    assert(radio_queue_request_rx(0x35) == RADIO_QUEUE_OK);
    assert(radio_queue_request_rx(0x36) == RADIO_QUEUE_OK);
    assert(receive(4) == RADIO_RX_CONTROLLER_ERROR && status.last_cookie == 0x35 && status.rx_count == 1);
    before = io;
    assert(radio_queue_service(15, 10000, 1000) == RADIO_QUEUE_RADIO_FAILED);
    assert(radio_queue_request_rx(0xff) == RADIO_QUEUE_RADIO_FAILED);
    assert(radio_queue_tx_submit(buffer.bytes, 5) == RADIO_QUEUE_RADIO_FAILED && io == before);
    read_frame(3);
    assert(radio_queue_tx_cancel() == RADIO_QUEUE_OK && io == before);
    assert(radio_queue_cancel_requests() == RADIO_QUEUE_OK);
    assert(radio_queue_snapshot(&status) == RADIO_QUEUE_OK && status.fault == RADIO_QUEUE_RADIO_FAILED &&
           status.rx_count == 0 && status.tx_count == 0 && status.requests == 0 && status.cancelled == 1);
    puts("Radio queue native actual-RX composition: pressure, FIFO copies, bad CRC reuse, retained error and cleanup PASS");
}

int main(int argc, char **argv)
{
    unsigned i, n, before;
    host_mmio_reset();
    host_mmio_read_hook = load; host_mmio_write_hook = store; host_mmio_xaddress_hook = address;
    host_mmio_xread_hook = xload; host_mmio_xwrite_hook = xstore;
    if (argc == 2 && !strcmp(argv[1], "--rf-vectors")) { rf_tests(); return 0; }
    assert(argc == 1);
    for (n = 0; n < 512; n++) {
        SOC_IEN0 = n & 1 ? 0x81 : 1;
        for (i = 0; i < 4; i++) {
            assert(radio_queue_request_rx((uint8_t)(n+i)) == RADIO_QUEUE_OK); calls++;
            assert(SOC_IEN0 == (n & 1 ? 0x81 : 1));
            assert(radio_queue_events[(radio_queue_event_tail+i) & 3] == (uint8_t)(n+i));
        }
        assert(radio_queue_request_rx(255) == RADIO_QUEUE_FULL); calls++;
        assert(radio_queue_snapshot(&status) == RADIO_QUEUE_OK && status.requests == 4);
        assert(radio_queue_cancel_requests() == RADIO_QUEUE_OK); calls++;
        assert(radio_queue_event_head == radio_queue_event_tail);
    }
    assert(radio_queue_dropped == 255 && status.cancelled == 255);
    for (n = 1; n <= 125; n++) {
        for (i = 0; i < 128; i++) buffer.bytes[i] = (uint8_t)(i+n);
        before = io;
        assert(radio_queue_tx_submit(buffer.bytes, (uint8_t)n) == RADIO_QUEUE_OK); calls++;
        assert(radio_queue_tx_submit(buffer.bytes, (uint8_t)n) == RADIO_QUEUE_FULL); calls++;
        memset(buffer.bytes, 0xa5, 128);
        assert(radio_queue_tx_read(&buffer.tx) == RADIO_QUEUE_OK); calls++;
        assert(buffer.tx.length == n);
        for (i = 0; i < n; i++) assert(buffer.tx.body[i] == (uint8_t)(i+n));
        for (i = n; i < 125; i++) assert(buffer.tx.body[i] == 0xa5);
        assert(radio_queue_tx_read(&buffer.tx) == RADIO_QUEUE_EMPTY && io == before); calls++;
    }
    assert(radio_queue_rx_read(&buffer.rx) == RADIO_QUEUE_EMPTY);
    assert(radio_queue_tx_cancel() == RADIO_QUEUE_EMPTY);
    before = io;
    for (n = 0; n < 65536; n++) {
        radio_queue_result_t expected;
        caller_address = (uint16_t)n;
        expected = n <= 0x700 || n > 0x1d80 || (n <= 0x1d00 && n+128 > 0x1d00) ?
            RADIO_QUEUE_BUFFER_OWNERSHIP : RADIO_QUEUE_EMPTY;
        assert(radio_queue_rx_read(&buffer.rx) == expected && io == before); calls++;
    }
    caller_address = 0x800;
    assert(radio_queue_rx_read(NULL) == RADIO_QUEUE_INVALID_ARGUMENT);
    assert(radio_queue_tx_submit(NULL, 1) == RADIO_QUEUE_INVALID_ARGUMENT);
    assert(radio_queue_tx_submit(buffer.bytes, 0) == RADIO_QUEUE_INVALID_ARGUMENT);
    assert(radio_queue_tx_submit(buffer.bytes, 126) == RADIO_QUEUE_INVALID_ARGUMENT);
    assert(radio_queue_service(10, 100, 100) == RADIO_QUEUE_INVALID_ARGUMENT);
    assert(radio_queue_service(15, 0, 100) == RADIO_QUEUE_INVALID_ARGUMENT);
    assert(radio_queue_service(15, 100, 0) == RADIO_QUEUE_INVALID_ARGUMENT && io == before);
    radio_queue_event_head = 5; radio_queue_event_tail = 0;
    assert(radio_queue_request_rx(0) == RADIO_QUEUE_CORRUPT);
    before = io;
    assert(radio_queue_service(15, 100, 100) == RADIO_QUEUE_CORRUPT &&
           radio_queue_request_rx(0) == RADIO_QUEUE_CORRUPT && io == before);
    printf("Radio queue memory/IRQ host: %u checked calls; wrap, full, copies, caller bounds and retained corruption PASS\n", calls);
    return 0;
}
#endif
