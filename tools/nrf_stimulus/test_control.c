/* SPDX-License-Identifier: BSD-3-Clause */
#include "control.h"
#include "transport.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); \
} } while (0)

static void payload(uint8_t *p)
{
    unsigned i;
    memcpy(p, stim_descriptor, 32);
    for (i = 32; i < 40; ++i) {
        p[i] = (uint8_t)i;
    }
}

static void run(struct stim_control *s, uint32_t now)
{
    uint8_t p[40];
    payload(p);
    stim_init(s, now, true);
    stim_command(s, now, STIM_ARM, p, sizeof(p));
    CHECK(s->state == STIM_ARMED);
    stim_command(s, now, STIM_RUN, p, sizeof(p));
    CHECK(s->consumed && !s->issued);
    CHECK(stim_action(s, now) == STIM_SUBMIT);
    CHECK(stim_action(s, now) == STIM_NOTHING);
}

static void capture(struct stim_control *s, uint32_t now)
{
    run(s, now);
    stim_schedule(s, now, true);
    stim_phy_done(s, now);
    CHECK(s->state == STIM_CAPTURE && s->tx_retired);
}

static void drain(struct stim_control *s)
{
    struct stim_record r, before;
    unsigned previous = 0;
    while (stim_pop(s, &r)) {
        size_t i;
        CHECK(r.sequence > previous);
        previous = r.sequence;
        CHECK(r.length <= STIM_PAYLOAD_MAX);
        CHECK(r.payload[0] == (uint8_t)r.sequence);
        for (i = r.length; i < sizeof(r.payload); ++i) {
            CHECK(r.payload[i] == 0);
        }
    }
    memset(&r, 0xa5, sizeof(r));
    before = r;
    CHECK(!stim_pop(s, &r));
    CHECK(memcmp(&r, &before, sizeof(r)) == 0);
    CHECK(!stim_pop(s, NULL));
}

static void lifecycle(void)
{
    struct stim_control s;
    struct stim_record r;
    uint8_t p[40], ack[3] = {2, 0, 0x51};
    payload(p);
    stim_init(&s, 0, true);
    CHECK(s.state == STIM_DISARMED);
    stim_tick(&s, 100000);
    CHECK(stim_action(&s, 100000) == STIM_NOTHING);
    CHECK(!s.consumed && !s.issued && s.stopped);
    stim_command(&s, 100000, STIM_RUN, p, 40);
    CHECK(s.error == STIM_REPLAY && !s.consumed);
    drain(&s);

    run(&s, 0);
    stim_phy_done(&s, 1); /* Genuine callback before the submitting call returns. */
    stim_rx(&s, 1, ack, 3, -80, 200);
    stim_schedule(&s, 2, true);
    CHECK(s.error == STIM_OK && s.phy_done && s.received == 1);
    CHECK(stim_action(&s, STIM_CAPTURE_MS) == STIM_NOTHING);
    CHECK(stim_action(&s, STIM_CAPTURE_MS + 1u) == STIM_SLEEP);
    stim_stop_result(&s, 21, true);
    stim_stop_observed(&s, 22, false);
    CHECK(!s.terminal_made);
    stim_stop_observed(&s, 23, true);
    CHECK(s.state == STIM_DONE && s.stopped && s.terminal_made);
    CHECK(s.terminal_record.payload[16] == 1);
    CHECK(s.terminal_record.payload[18] == 1);
    CHECK(stim_action(&s, 24) == STIM_NOTHING);
    while (stim_pop(&s, &r)) {
        if (r.type == STIM_RX_FRAME) {
            CHECK(r.length == 23);
            CHECK(r.payload[18] == 1 && r.payload[19] == 3);
            CHECK(memcmp(r.payload + 20, ack, 3) == 0);
        }
    }
    stim_command(&s, 25, STIM_RUN, p, 40);
    CHECK(s.error == STIM_REPLAY && stim_action(&s, 25) == STIM_NOTHING);
    drain(&s);

    run(&s, 0);
    stim_schedule(&s, 1, false);
    CHECK(s.error == STIM_REJECTED && s.consumed && !s.phy_done);
    stim_phy_done(&s, 1);
    CHECK(s.error == STIM_REJECTED && s.phy_done);
    CHECK(stim_action(&s, 1) == STIM_SLEEP);
    stim_stop_result(&s, 1, false);
    CHECK(s.error == STIM_REJECTED && !s.stopped && s.terminal_made);
    CHECK(stim_action(&s, 100) == STIM_NOTHING);
    drain(&s);

    run(&s, 0);
    stim_tx_failed(&s, 0, 7);
    stim_schedule(&s, 0, true);
    CHECK(s.error == STIM_TX_ERROR && s.tx_retired);
    stim_phy_done(&s, 0);
    CHECK(s.error == STIM_TX_ERROR && !s.phy_done);
    drain(&s);

    stim_init(&s, 0, false);
    CHECK(s.error == STIM_PROFILE && stim_action(&s, 0) == STIM_SLEEP);
    drain(&s);
}

static void guards(void)
{
    struct stim_control s;
    uint8_t p[256];
    unsigned i;
    memset(p, 0, sizeof(p));
    payload(p);
    stim_init(&s, 0, true);
    for (i = 0; i < STIM_COMMAND_MAX; ++i) {
        stim_command(&s, 0, STIM_STATUS, NULL, 0);
    }
    CHECK(s.error == STIM_OK);
    stim_command(&s, 0, STIM_STATUS, NULL, 0);
    CHECK(s.error == STIM_WORK && !s.consumed && !s.issued);
    for (i = 0; i <= 255; ++i) {
        stim_init(&s, 0, true);
        stim_command(&s, 0, STIM_ARM, p, i);
        CHECK(i == 40 ? s.state == STIM_ARMED : s.error == STIM_PROTOCOL);
        CHECK(!s.consumed && !s.issued);
    }
    for (i = 0; i < 256; ++i) {
        if (i == STIM_STATUS || i == STIM_ARM || i == STIM_RUN) {
            continue;
        }
        stim_init(&s, 0, true);
        stim_command(&s, 0, (uint8_t)i, p, 40);
        CHECK(s.error == STIM_PROTOCOL && !s.consumed);
    }
    for (i = 0; i < 32; ++i) {
        stim_init(&s, 0, true);
        p[i] ^= 1;
        stim_command(&s, 0, STIM_ARM, p, 40);
        CHECK(s.error == STIM_DESCRIPTOR);
        p[i] ^= 1;
    }
    stim_init(&s, 0, true);
    stim_command(&s, 0, STIM_ARM, NULL, 40);
    CHECK(s.error == STIM_PROTOCOL);
    stim_init(&s, 0, true);
    memset(p + 32, 0, 8);
    stim_command(&s, 0, STIM_ARM, p, 40);
    CHECK(s.error == STIM_NONCE);
    payload(p);
    stim_init(&s, 0, true);
    stim_command(&s, 0, STIM_ARM, p, 40);
    p[32] ^= 1;
    stim_command(&s, 1, STIM_RUN, p, 40);
    CHECK(s.error == STIM_NONCE && !s.consumed);
    payload(p);
    stim_init(&s, 0, true);
    stim_command(&s, 0, STIM_ARM, p, 40);
    stim_command(&s, 1, STIM_ARM, p, 40);
    CHECK(s.error == STIM_REPLAY);

    stim_init(&s, 0, true);
    stim_phy_done(&s, 0);
    CHECK(s.error == STIM_CALLBACK && !s.phy_done);
    run(&s, 0);
    stim_rx(&s, 0, p, 3, -1, 0);
    CHECK(s.error == STIM_CALLBACK && s.received == 0);
    run(&s, 0);
    stim_schedule(&s, 0, true);
    stim_schedule(&s, 0, true);
    CHECK(s.error == STIM_CALLBACK);
    run(&s, 0);
    stim_phy_done(&s, 0);
    stim_schedule(&s, 0, false);
    CHECK(s.error == STIM_CALLBACK);
    capture(&s, 0);
    stim_phy_done(&s, 1);
    CHECK(s.error == STIM_CALLBACK && s.phy_ms == 0);
    capture(&s, 0);
    stim_rx_failed(&s, 0, 1);
    CHECK(s.error == STIM_RX_ERROR && s.received == 0);
    stim_fault(&s, 0, STIM_UART);
    CHECK(s.error == STIM_RX_ERROR);
    drain(&s);
}

static void deadlines(void)
{
    struct stim_control s;
    uint8_t p[40];
    unsigned i;
    payload(p);
    for (i = 0; i < 3; ++i) {
        uint32_t base = i == 0 ? 0 : (i == 1 ? UINT32_MAX - 10u : UINT32_MAX - 5000u);
        stim_init(&s, base, true);
        stim_command(&s, base, STIM_ARM, p, 40);
        stim_tick(&s, base + STIM_ARM_MS - 1u);
        CHECK(s.state == STIM_ARMED);
        stim_command(&s, base + STIM_ARM_MS, STIM_RUN, p, 40);
        CHECK(s.error == STIM_EXPIRED && !s.consumed);
        run(&s, base);
        stim_schedule(&s, base, true);
        stim_tick(&s, base + STIM_TRIAL_MS - 1u);
        CHECK(s.error == STIM_OK);
        CHECK(stim_action(&s, base + STIM_TRIAL_MS) == STIM_SLEEP);
        CHECK(s.error == STIM_EXPIRED);
        stim_stop_result(&s, base + STIM_TRIAL_MS, true);
        stim_stop_observed(&s, base + STIM_TRIAL_MS + STIM_STOP_MS - 1u, false);
        CHECK(!s.terminal_made);
        stim_stop_observed(&s, base + STIM_TRIAL_MS + STIM_STOP_MS, true);
        CHECK(s.terminal_made && !s.stopped);
        CHECK(s.error == STIM_EXPIRED);
        drain(&s);
        capture(&s, base);
        CHECK(stim_action(&s, base + STIM_CAPTURE_MS - 1u) == STIM_NOTHING);
        CHECK(stim_action(&s, base + STIM_CAPTURE_MS) == STIM_SLEEP);
        stim_stop_result(&s, base + STIM_CAPTURE_MS, true);
        stim_tick(&s, base + STIM_CAPTURE_MS + STIM_STOP_MS);
        CHECK(s.error == STIM_STOP_ERROR && s.terminal_made && !s.stopped);
    }
    stim_init(&s, 100, true);
    stim_tick(&s, 99);
    CHECK(s.error == STIM_CLOCK);
    run(&s, 0);
    stim_schedule(&s, 0, true);
    stim_phy_done(&s, STIM_TRIAL_MS - 1u);
    CHECK(stim_action(&s, STIM_TRIAL_MS) == STIM_SLEEP && s.error == STIM_OK);
    run(&s, 0);
    for (i = 0; i <= STIM_WORK_MAX; ++i) {
        stim_tick(&s, 0);
    }
    CHECK(s.error == STIM_WORK && stim_action(&s, 0) == STIM_SLEEP);
    stim_stop_result(&s, 0, true);
    for (i = 0; i <= STIM_STOP_WORK_MAX; ++i) {
        stim_tick(&s, 0);
    }
    CHECK(s.terminal_made && !s.stopped && s.error == STIM_WORK);
    stim_init(&s, 0, true);
    stim_command(&s, 0, STIM_ARM, p, 40);
    for (i = 0; i <= STIM_ARM_WORK_MAX; ++i) {
        stim_tick(&s, 0);
    }
    CHECK(s.error == STIM_WORK && !s.consumed && s.stopped);
}

static void receipts(void)
{
    struct stim_control s;
    struct stim_record r, saved[STIM_QUEUE];
    uint8_t body[128];
    unsigned i, n;
    memset(body, 0xe9, sizeof(body)); /* Opaque secured/vendor/ordinary payload allowed. */
    for (n = 0; n <= 128; ++n) {
        capture(&s, 0);
        memcpy(saved, s.queue, sizeof(saved));
        stim_rx(&s, 1, body, n, -128, 255);
        if (n >= 3 && n <= 125) {
            CHECK(s.error == STIM_OK && s.received == 1);
            CHECK(s.queue[s.count - 1u].length == n + 20u);
            CHECK(memcmp(s.queue[s.count - 1u].payload + 20, body, n) == 0);
        } else {
            CHECK(s.error == STIM_RX_LENGTH && s.received == 0);
            CHECK(memcmp(saved, s.queue, sizeof(saved)) == 0);
        }
        drain(&s);
    }
    capture(&s, 0);
    stim_rx(&s, 0, NULL, 3, 0, 0);
    CHECK(s.error == STIM_RX_LENGTH);
    capture(&s, 0);
    for (i = 0; i < STIM_CAPTURE_MAX; ++i) {
        body[0] = (uint8_t)i;
        stim_rx(&s, 0, body, 125, -40, 50);
    }
    CHECK(s.error == STIM_CAPTURE_LIMIT && s.received == 4);
    memcpy(saved, s.queue, sizeof(saved));
    stim_rx(&s, 0, body, 3, 0, 0);
    CHECK(memcmp(saved, s.queue, sizeof(saved)) == 0);
    CHECK(stim_action(&s, 0) == STIM_SLEEP);
    drain(&s);

    run(&s, 0);
    for (i = 0; i < STIM_QUEUE + 3u; ++i) {
        stim_rx_failed(&s, 0, (uint8_t)i);
    }
    CHECK(s.count == STIM_QUEUE && s.error == STIM_RX_ERROR);
    memcpy(saved, s.queue, sizeof(saved));
    stim_rx_failed(&s, 0, 42);
    CHECK(memcmp(saved, s.queue, sizeof(saved)) == 0);
    CHECK(s.fault_pending);
    drain(&s);
    for (i = 0; i < STIM_RECORD_MAX + 2u; ++i) {
        stim_rx_failed(&s, 0, 42);
        while (stim_pop(&s, &r)) {
            CHECK(r.sequence <= 34);
        }
    }
    CHECK(s.sequence <= 34);

    /* Queue exhaustion itself, without an earlier failure, is a retained fault. */
    capture(&s, 0);
    for (i = 0; i < 6; ++i) {
        stim_command(&s, 0, STIM_STATUS, NULL, 0);
    }
    for (i = 0; i < 3; ++i) {
        stim_rx(&s, 0, body, 3, 0, 0);
    }
    CHECK(stim_action(&s, STIM_CAPTURE_MS) == STIM_SLEEP);
    stim_stop_result(&s, STIM_CAPTURE_MS, true);
    CHECK(s.error == STIM_OK && s.count == STIM_QUEUE);
    memcpy(saved, s.queue, sizeof(saved));
    stim_rx(&s, STIM_CAPTURE_MS, body, 3, 0, 0);
    CHECK(s.error == STIM_QUEUE_LOSS && s.received == 3);
    CHECK(memcmp(saved, s.queue, sizeof(saved)) == 0);
}

static void wire(void)
{
    struct stim_control s;
    uint8_t p[40], raw[45];
    char line[STIM_LINE_MAX + 8u], saved[sizeof(line)];
    size_t n, i, bit, capacity;
    uint16_t crc;
    payload(p);
    CHECK(stim_crc((const uint8_t *)"123456789", 9) == 0x29b1);
    n = stim_encode(STIM_ARM, p, 40, line, sizeof(line));
    CHECK(n == 91);
    stim_init(&s, 0, true);
    for (i = 0; i < n; ++i) {
        stim_byte(&s, 0, (uint8_t)line[i]);
    }
    CHECK(s.state == STIM_ARMED && !s.issued);
    n = stim_encode(STIM_RUN, p, 40, line, sizeof(line));
    for (i = 0; i < n; ++i) {
        stim_byte(&s, 1, (uint8_t)line[i]);
    }
    CHECK(stim_action(&s, 1) == STIM_SUBMIT);

    raw[0] = 1; raw[1] = STIM_ARM; raw[2] = 40;
    memcpy(raw + 3, p, 40);
    crc = stim_crc(raw, 43);
    raw[43] = (uint8_t)crc; raw[44] = (uint8_t)(crc >> 8);
    for (bit = 0; bit < sizeof(raw) * 8u; ++bit) {
        static const char hex[] = "0123456789ABCDEF";
        raw[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
        stim_init(&s, 0, true);
        for (i = 0; i < sizeof(raw); ++i) {
            stim_byte(&s, 0, (uint8_t)hex[raw[i] >> 4]);
            stim_byte(&s, 0, (uint8_t)hex[raw[i] & 15u]);
        }
        stim_byte(&s, 0, '\n');
        CHECK(s.error != STIM_OK && !s.consumed && !s.issued);
        raw[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
    }
    for (i = 0; i < 256; ++i) {
        stim_init(&s, 0, true);
        stim_byte(&s, 0, (uint8_t)i);
        stim_tick(&s, STIM_PARTIAL_MS);
        CHECK(s.error != STIM_OK && !s.issued);
    }
    stim_init(&s, 0, true);
    for (i = 0; i < 91; ++i) {
        stim_byte(&s, 0, '0');
    }
    CHECK(s.error == STIM_PROTOCOL && !s.consumed);
    stim_init(&s, UINT32_MAX - 5u, true);
    stim_byte(&s, UINT32_MAX - 5u, '0');
    stim_tick(&s, UINT32_MAX - 5u + STIM_PARTIAL_MS - 1u);
    CHECK(s.error == STIM_OK);
    stim_tick(&s, UINT32_MAX - 5u + STIM_PARTIAL_MS);
    CHECK(s.error == STIM_PROTOCOL);
    stim_init(&s, 0, true);
    for (i = 0; i <= STIM_INPUT_MAX; ++i) {
        stim_byte(&s, 0, 'X');
    }
    CHECK(s.input_work == STIM_INPUT_MAX && s.error == STIM_PROTOCOL);
    CHECK(stim_action(&s, 0) == STIM_NOTHING);

    for (capacity = 0; capacity < 100; ++capacity) {
        memset(line, 'Z', sizeof(line));
        memcpy(saved, line, sizeof(line));
        n = stim_encode(STIM_ARM, p, 40, line, capacity);
        if (capacity < 91) {
            CHECK(n == 0 && memcmp(line, saved, sizeof(line)) == 0);
        } else {
            CHECK(n == 91);
            CHECK(memcmp(line + n, saved + n, sizeof(line) - n) == 0);
        }
    }
    CHECK(stim_encode(0, NULL, 1, line, sizeof(line)) == 0);
    CHECK(stim_encode(0, p, SIZE_MAX, line, sizeof(line)) == 0);
    CHECK(stim_encode(0, p, 40, NULL, 100) == 0);
}

static void vectors(void)
{
    struct stim_control s;
    struct stim_record r;
    char line[STIM_LINE_MAX];
    uint8_t ack[3] = {2, 0, 0x51};
    capture(&s, 0);
    stim_rx(&s, 1, ack, 3, -80, 200);
    CHECK(stim_action(&s, 20) == STIM_SLEEP);
    stim_stop_result(&s, 20, true);
    stim_stop_observed(&s, 21, true);
    while (stim_pop(&s, &r)) {
        size_t n = stim_encode(r.type, r.payload, r.length, line, sizeof(line));
        CHECK(n != 0);
        CHECK(fwrite(line, 1, n, stdout) == n);
    }
}

static void transport(void)
{
    struct stim_uart u;
    uint8_t before[STIM_UART_RING], out;
    unsigned i, length;
    memset(&u, 0, sizeof(u));
    for (i = 0; i < 3u * STIM_UART_RING; ++i) {
        CHECK(stim_uart_put(&u, (uint8_t)i));
        CHECK(stim_uart_take(&u, &out) && out == (uint8_t)i);
    }
    out = 0xa5;
    CHECK(!stim_uart_take(&u, &out) && out == 0xa5);
    for (i = 0; i < STIM_UART_RING; ++i) {
        CHECK(stim_uart_put(&u, (uint8_t)i));
    }
    memcpy(before, u.rx, sizeof(before));
    CHECK(!stim_uart_put(&u, 42) && u.rx_fault);
    CHECK(memcmp(before, u.rx, sizeof(before)) == 0);
    CHECK(!stim_uart_take(&u, &out) && out == 0xa5);
    CHECK(stim_uart_begin(&u, 1, 0)); /* RX fault must not suppress the fault report. */
    CHECK(stim_uart_advance(&u, 1, 1) && u.busy);
    CHECK(stim_uart_complete(&u) && !u.busy && !u.tx_fault);
    for (length = 1; length <= STIM_LINE_MAX; ++length) {
        memset(&u, 0, sizeof(u));
        CHECK(stim_uart_begin(&u, length, UINT32_MAX - 10u));
        while (u.position < u.length) {
            size_t n = u.length - u.position;
            if (n > STIM_UART_SLICE) {
                n = STIM_UART_SLICE;
            }
            CHECK(stim_uart_advance(&u, n, 1));
        }
        CHECK(u.busy && !u.tx_fault);
        CHECK(stim_uart_complete(&u) && !u.busy);
    }
    for (i = 0; i < 5; ++i) {
        memset(&u, 0, sizeof(u));
        CHECK(stim_uart_begin(&u, 8, 0));
        if (i == 0) {
            CHECK(!stim_uart_advance(&u, 1, 0));
        } else if (i == 1) {
            CHECK(!stim_uart_advance(&u, 1, -1));
        } else if (i == 2) {
            CHECK(!stim_uart_advance(&u, 1, 2));
        } else if (i == 3) {
            CHECK(!stim_uart_advance(&u, 33, 1));
        } else {
            CHECK(!stim_uart_complete(&u));
        }
        CHECK(u.tx_fault && u.position == 0);
        CHECK(!stim_uart_begin(&u, 1, 0));
    }
    memset(&u, 0, sizeof(u));
    CHECK(!stim_uart_take(&u, NULL) && u.rx_fault);
    CHECK(!stim_uart_begin(&u, 0, 0) && u.tx_fault);
    memset(&u, 0, sizeof(u));
    CHECK(!stim_uart_begin(&u, STIM_LINE_MAX + 1u, 0));
    memset(&u, 0, sizeof(u));
    CHECK(stim_uart_begin(&u, 1, 0));
    CHECK(!stim_uart_begin(&u, 2, 0) && u.tx_fault && u.length == 1);
    memset(&u, 0, sizeof(u));
    CHECK(stim_uart_begin(&u, 1, UINT32_MAX - 50u));
    stim_uart_tick(&u, UINT32_MAX - 50u + STIM_UART_TX_MS - 1u);
    CHECK(!u.tx_fault);
    stim_uart_tick(&u, UINT32_MAX - 50u + STIM_UART_TX_MS);
    CHECK(u.tx_fault);
    memset(&u, 0, sizeof(u));
    CHECK(stim_uart_begin(&u, 1, 0));
    for (i = 0; i <= STIM_WORK_MAX; ++i) {
        stim_uart_tick(&u, 0);
    }
    CHECK(u.tx_fault);
}

static void startup(void)
{
    struct stim_control s;
    struct stim_record first;
    uint8_t p[40];
    unsigned early, profile, disabled, i;
    payload(p);
    for (early = 0; early < 4; ++early) {
        for (profile = 0; profile < 2; ++profile) {
            for (disabled = 0; disabled < 2; ++disabled) {
                uint8_t expected = STIM_OK;
                CHECK(stim_startup_begin(&s, 0, true));
                CHECK(!s.stopped && !s.sdk_ready && s.startup_pending);
                CHECK(s.sequence == 0 && s.count == 0);
                CHECK(!s.terminal_made && stim_action(&s, 0) == STIM_NOTHING);
                if (early == 1) {
                    stim_phy_done(&s, 1);
                    expected = STIM_CALLBACK;
                } else if (early == 2) {
                    stim_rx_failed(&s, 1, 7);
                    expected = STIM_RX_ERROR;
                } else if (early == 3) {
                    stim_fault(&s, 1, STIM_UART);
                    expected = STIM_UART;
                }
                first = s.fault_record;
                CHECK(!s.terminal_made && !s.stopped);
                CHECK(stim_action(&s, 1) == STIM_NOTHING && !s.stop_issued);
                stim_startup_complete(&s, 2, profile != 0, disabled != 0);
                CHECK(s.sdk_ready && !s.startup_pending && s.stopped == (disabled != 0));
                if (early != 0) {
                    CHECK(memcmp(&first, &s.fault_record, sizeof(first)) == 0);
                } else if (profile == 0 || disabled == 0) {
                    expected = STIM_PROFILE;
                }
                CHECK(s.error == expected);
                CHECK(s.queue[s.count - 1u].type == STIM_HELLO &&
                      s.queue[s.count - 1u].payload[2] == s.state &&
                      s.queue[s.count - 1u].payload[3] == expected);
                CHECK(s.queue[s.count - 1u].payload[4] == 2);
                CHECK(!s.stop_issued && !s.stop_result_seen && !s.consumed && !s.issued);
                if (disabled == 0) {
                    CHECK(!s.terminal_made && s.stop_needed);
                    CHECK(stim_action(&s, 3) == STIM_SLEEP);
                    CHECK(!s.stop_result_seen && !s.terminal_made);
                    stim_stop_result(&s, 4, true);
                    stim_stop_observed(&s, 5, false);
                    CHECK(!s.stopped && !s.terminal_made);
                    stim_stop_observed(&s, 6, true);
                    CHECK(s.terminal_made && s.stopped && s.error == expected);
                    CHECK(s.terminal_record.payload[19] == 1);
                    CHECK(s.terminal_record.payload[21] == 1);
                } else if (expected != STIM_OK) {
                    CHECK(s.terminal_made && !s.stop_needed);
                    CHECK(s.terminal_record.payload[19] == 1);
                    CHECK(s.terminal_record.payload[21] == 0);
                    CHECK(stim_action(&s, 3) == STIM_NOTHING);
                } else {
                    CHECK(!s.terminal_made && !s.stop_needed);
                    stim_command(&s, 3, STIM_ARM, p, 40);
                    CHECK(s.state == STIM_ARMED);
                }
                drain(&s);
            }
        }
    }

    CHECK(!stim_startup_begin(&s, 0, false));
    CHECK(s.error == STIM_PROFILE && !s.sdk_ready && !s.startup_pending);
    CHECK(s.terminal_made && !s.stopped && !s.stop_needed);
    for (i = 16; i < 22; ++i) {
        CHECK(s.terminal_record.payload[i] == 0);
    }
    CHECK(stim_action(&s, 1) == STIM_NOTHING && !s.stop_issued && !s.stop_result_seen);
    first = s.terminal_record;
    stim_startup_complete(&s, 2, true, true); /* SDK was never initialized. */
    stim_stop_result(&s, 2, false);          /* No operation was called. */
    CHECK(!s.sdk_ready && !s.stopped && !s.stop_result_seen);
    CHECK(memcmp(&first, &s.terminal_record, sizeof(first)) == 0);
    stim_command(&s, 3, STIM_ARM, p, 40);
    CHECK(!s.consumed && stim_action(&s, 3) == STIM_NOTHING);
    drain(&s);

    CHECK(stim_startup_begin(&s, 0, true));
    stim_command(&s, 0, STIM_ARM, p, 40);
    CHECK(s.error == STIM_REPLAY && !s.consumed && !s.terminal_made);
    stim_startup_complete(&s, 1, true, false);
    CHECK(s.error == STIM_REPLAY && !s.stopped && !s.terminal_made);
    stim_startup_complete(&s, 1, true, true); /* A duplicate cannot refresh evidence. */
    CHECK(!s.stopped && !s.terminal_made);
    CHECK(stim_action(&s, 2) == STIM_SLEEP);
    stim_stop_result(&s, 3, false);
    CHECK(s.terminal_made && !s.stopped && s.terminal_record.payload[19] == 0);
    drain(&s);
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--vectors") == 0) {
        vectors();
        return 0;
    }
    lifecycle();
    guards();
    deadlines();
    receipts();
    wire();
    transport();
    startup();
    printf("NS51 portable checks: %u\n", checks);
    return 0;
}
