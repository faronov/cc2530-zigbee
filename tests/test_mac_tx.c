/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 * Synthetic source events, never a radio mock or a board fixture.
 */
#include "mac_tx.h"
#include "cc2530_mmio.h"

#include <stddef.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) return (uint16_t)__LINE__; } while (0)
#define CALL(c) do { uint16_t line = (c); if (line != 0u) return line; } while (0)

static const MCU_CODE uint8_t golden[] = {
    0x61, 0x98, 0xa5, 0x34, 0x12, 0x78, 0x56, 0xbc, 0x9a, 0xaa, 0x55, 0xcc
};
static const MCU_CODE uint8_t beacon_request[] = {
    0x03, 0x08, 0xa5, 0xff, 0xff, 0xff, 0xff, 0x07
};
static const MCU_CODE uint8_t data_request[] = {
    0x23, 0x80, 0xa5, 0x34, 0x12, 0xbc, 0x9a, 0x04
};
static mac_tx_t tx, saved;
static mac_tx_event_t event;
static mac_tx_action_t action, saved_action;
static uint8_t body[125], copy[125], ack[4], length;
static uint32_t now;
static volatile uint8_t i;

static void source(uint8_t kind)
{
    memset(&event, 0, sizeof(event));
    event.kind = kind;
    event.generation = tx.generation;
    event.retry = tx.retries;
    event.nb = tx.nb;
    event.stamp = now;
}

static uint16_t step(void)
{
    CHECK(mac_tx_step(&tx, now, &event, &action) == MAC_TX_OK);
    return 0;
}

static uint16_t poll(void)
{
    CHECK(mac_tx_step(&tx, now, NULL, &action) == MAC_TX_OK);
    return 0;
}

static uint16_t begin(uint8_t ar, uint8_t dsn, uint32_t start,
                      uint32_t lifetime, uint16_t work)
{
    now = start;
    CHECK(mac_tx_init(&tx, dsn, now) == MAC_TX_OK);
    memcpy(body, golden, sizeof(golden));
    if (!ar)
        body[0] &= (uint8_t)~MAC_FLAG_ACK_REQUEST;
    CHECK(mac_tx_submit(&tx, body, sizeof(golden), now, lifetime, work) == MAC_TX_OK);
    CHECK(tx.phase == MAC_TX_DRAW && tx.frame[2] == dsn && tx.next_dsn == (uint8_t)(dsn + 1u));
    CHECK(body[2] == 0xa5);
    return 0;
}

static uint16_t draw(uint8_t random)
{
    CALL(poll());
    CHECK(action.kind == MAC_TX_ACTION_RANDOM && tx.phase == MAC_TX_DRAW_WAIT);
    CALL(poll());
    CHECK(action.kind == MAC_TX_ACTION_NONE);
    source(MAC_TX_EVENT_RANDOM);
    event.value = random;
    CALL(step());
    CHECK(action.kind == MAC_TX_ACTION_ATTEMPT && tx.phase == MAC_TX_RADIO);
    CHECK(action.until == tx.deadline);
    return 0;
}

static uint16_t sent(void)
{
    now = action.at + 36u + 2u * tx.length;
    source(MAC_TX_EVENT_SENT);
    CALL(step());
    CHECK(tx.transmissions == tx.retries + 1u && !tx.uncertain);
    return 0;
}

static uint16_t receive(uint8_t dsn)
{
    ack[0] = 2;
    ack[1] = 0;
    ack[2] = dsn;
    source(MAC_TX_EVENT_ACK);
    event.bytes = ack;
    event.length = 3;
    CALL(step());
    return 0;
}

static uint16_t quiesce(void)
{
    CHECK(tx.phase == MAC_TX_STOPPING && action.kind == MAC_TX_ACTION_QUIESCE);
    CHECK(action.until == now + MAC_TX_STOP_SYMBOLS && action.at == now);
    CHECK(mac_tx_release(&tx) == MAC_TX_STATE);
    CALL(poll());
    CHECK(action.kind == MAC_TX_ACTION_NONE);
    source(MAC_TX_EVENT_QUIESCED);
    CALL(step());
    return 0;
}

static uint16_t initialization(void)
{
    memset(&tx, 0xc7, sizeof(tx));
    saved = tx;
    CHECK(mac_tx_init(NULL, 0x5a, 0x12345678UL) == MAC_TX_INVALID);
    CHECK(memcmp(&tx, &saved, sizeof(tx)) == 0);
    memset(&saved, 0, sizeof(saved));
    saved.next_dsn = 0x5a;
    saved.last = 0x12345678UL;
    saved.ready_at = 0x12345678UL;
    CHECK(mac_tx_init(&tx, 0x5a, 0x12345678UL) == MAC_TX_OK);
    CHECK(memcmp(&tx, &saved, sizeof(tx)) == 0);
    return 0;
}

static uint16_t admission(void)
{
    CALL(begin(1, 0xff, 0, 10000, 100));
    saved = tx;
    CHECK(mac_tx_submit(&tx, body, sizeof(golden), now, 100, 10) == MAC_TX_FULL);
    CHECK(memcmp(&tx, &saved, sizeof(tx)) == 0);
    memset(copy, 0xc7, sizeof(copy));
    length = 0xa5;
    CHECK(mac_tx_copy(&tx, copy, 11, &length) == MAC_TX_SPACE && length == 0xa5);
    for (i = 0; i < sizeof(copy); i++)
        CHECK(copy[i] == 0xc7);
    CHECK(mac_tx_copy(&tx, copy, 12, &length) == MAC_TX_OK && length == 12);
    CHECK(copy[2] == 0xff && memcmp(copy + 3, golden + 3, 9) == 0);
    source(MAC_TX_EVENT_CANCEL);
    CALL(step());
    CHECK(tx.phase == MAC_TX_DONE && tx.outcome == MAC_TX_CANCELLED && !tx.uncertain);
    CHECK(mac_tx_release(&tx) == MAC_TX_OK);
    CHECK(mac_tx_submit(&tx, body, 12, now, 10000, 100) == MAC_TX_OK);
    CHECK(tx.frame[2] == 0 && tx.next_dsn == 1 && tx.generation == 2);
    CHECK(mac_tx_init(&tx, 9, 0) == MAC_TX_OK);
    saved = tx;
    for (i = 0; i < 9; i++) {
        CHECK(mac_tx_submit(&tx, body, i, 0, 100, 10) == MAC_TX_UNSUPPORTED);
        CHECK(memcmp(&tx, &saved, sizeof(tx)) == 0);
    }
    CHECK(mac_tx_submit(&tx, NULL, 12, 0, 100, 10) == MAC_TX_INVALID);
    CHECK(mac_tx_submit(&tx, body, 126, 0, 100, 10) == MAC_TX_UNSUPPORTED);
    CHECK(mac_tx_submit(&tx, body, 12, 0, 0, 10) == MAC_TX_INVALID);
    CHECK(mac_tx_submit(&tx, body, 12, 0, MAC_TX_HALF, 10) == MAC_TX_INVALID);
    CHECK(mac_tx_submit(&tx, body, 12, 0, 100, 0) == MAC_TX_INVALID);
    CHECK(mac_tx_submit(&tx, body, 12, MAC_TX_HALF, 100, 10) == MAC_TX_INVALID);
    CHECK(memcmp(&tx, &saved, sizeof(tx)) == 0);
    CHECK(mac_tx_submit(&tx, golden, sizeof(golden), 0, 100, 10) == MAC_TX_OK);
    CHECK(tx.frame[2] == 9); /* Generic CODE input on the target. */
    CHECK(mac_tx_init(&tx, 9, 0) == MAC_TX_OK);
    body[0] |= MAC_FLAG_SECURITY;
    CHECK(mac_tx_submit(&tx, body, 12, 0, 100, 10) == MAC_TX_UNSUPPORTED);
    body[0] = 0x62;
    CHECK(mac_tx_submit(&tx, body, 12, 0, 100, 10) == MAC_TX_UNSUPPORTED);
    body[0] = 0x71;
    CHECK(mac_tx_submit(&tx, body, 12, 0, 100, 10) == MAC_TX_UNSUPPORTED);
    memcpy(body, golden, 12);
    body[5] = body[6] = 0xff;
    CHECK(mac_tx_submit(&tx, body, 12, 0, 100, 10) == MAC_TX_UNSUPPORTED);
    body[0] &= (uint8_t)~MAC_FLAG_ACK_REQUEST;
    CHECK(mac_tx_submit(&tx, body, 125, 0, 10000, 100) == MAC_TX_OK);
    CHECK(mac_tx_copy(&tx, copy, 124, &length) == MAC_TX_SPACE);
    CHECK(mac_tx_copy(&tx, copy, 125, &length) == MAC_TX_OK && length == 125);
    CHECK(mac_tx_init(&tx, 9, 0) == MAC_TX_OK);
    tx.generation = UINT32_MAX; /* Explicit synthetic boundary, compiled on target. */
    saved = tx;
    CHECK(mac_tx_submit(&tx, body, 12, 0, 100, 10) == MAC_TX_GENERATION_EXHAUSTED);
    CHECK(memcmp(&tx, &saved, sizeof(tx)) == 0);
    return 0;
}

static uint16_t beacon_requests(void)
{
    now = 0;
    CHECK(mac_tx_init(&tx, 0xfe, now) == MAC_TX_OK);
    CHECK(mac_tx_submit(&tx, beacon_request, 8, now, 10000, 100) == MAC_TX_OK);
    CHECK(tx.length == 8 && !tx.ack_requested && tx.frame[2] == 0xfe);
    CHECK(mac_tx_copy(&tx, copy, 8, &length) == MAC_TX_OK && length == 8);
    CHECK(copy[0] == 3 && copy[1] == 8 && memcmp(copy + 3, beacon_request + 3, 5) == 0);
    CALL(draw(0));
    CALL(sent());
    CHECK(tx.phase == MAC_TX_STOPPING && tx.outcome == MAC_TX_UNACKNOWLEDGED);
    CALL(quiesce());
    CHECK(tx.phase == MAC_TX_DONE && tx.transmissions == 1 && tx.retries == 0);
    CHECK(mac_tx_release(&tx) == MAC_TX_OK);
    CHECK(mac_tx_submit(&tx, golden, 12, now, 10000, 100) == MAC_TX_OK);
    CHECK(tx.frame[2] == 0xff && tx.next_dsn == 0);
    CALL(draw(0));
    CHECK(action.at == now + 12u);

    CHECK(mac_tx_init(&tx, 7, 0) == MAC_TX_OK);
    saved = tx;
    memcpy(body, beacon_request, 8);
    body[0] |= MAC_FLAG_PENDING; /* Ignored only by command RX, not allowed on TX. */
    CHECK(mac_tx_submit(&tx, body, 8, 0, 1000, 100) == MAC_TX_UNSUPPORTED);
    CHECK(mac_tx_submit(&tx, data_request, 8, 0, 1000, 100) == MAC_TX_UNSUPPORTED);
    CHECK(memcmp(&tx, &saved, sizeof(tx)) == 0);
    return 0;
}

static uint16_t success_and_spacing(void)
{
    CALL(begin(1, 0x52, 0xfffffff0UL, 10000, 100));
    CALL(draw(7));
    CHECK(action.at == 124u);
    CALL(sent());
    CHECK(tx.phase == MAC_TX_ACK_WAIT && action.kind == MAC_TX_ACTION_NONE);
    now += 34;
    CALL(receive(0x52));
    CHECK(tx.outcome == MAC_TX_ACKED && tx.phase == MAC_TX_STOPPING);
    CALL(quiesce());
    CHECK(tx.phase == MAC_TX_DONE && tx.transmissions == 1 && !tx.uncertain);
    CHECK(mac_tx_release(&tx) == MAC_TX_OK);
    body[0] &= (uint8_t)~MAC_FLAG_ACK_REQUEST;
    CHECK(mac_tx_submit(&tx, body, 12, now, 10000, 100) == MAC_TX_OK);
    CALL(draw(0));
    CHECK(action.at == now + 12u);
    CALL(sent());
    CHECK(tx.outcome == MAC_TX_UNACKNOWLEDGED);
    CALL(quiesce());
    CHECK(tx.phase == MAC_TX_DONE && tx.outcome == MAC_TX_UNACKNOWLEDGED);
    CHECK(mac_tx_release(&tx) == MAC_TX_OK);
    CHECK(mac_tx_submit(&tx, body, 125, now, 10000, 100) == MAC_TX_OK);
    CALL(draw(0));
    CALL(sent());
    CHECK(tx.ready_at == now + 40u);
    CALL(quiesce());
    return 0;
}

static uint16_t backoff_retry(void)
{
    CALL(begin(1, 0x31, 0, 100000, 500));
    for (i = 0; i < 5; i++) {
        CALL(draw(0xff));
        CHECK(action.at == now + (i == 0 ? 140u : i == 1 ? 300u : 620u));
        now = action.at + 8u;
        source(MAC_TX_EVENT_BUSY);
        CALL(step());
        CHECK(tx.nb == i + 1u && tx.transmissions == 0 && !tx.uncertain);
    }
    CHECK(tx.phase == MAC_TX_DONE && tx.outcome == MAC_TX_CHANNEL_ACCESS);
    CALL(begin(1, 0x32, 0, 100000, 500));
    for (i = 0; i < 4; i++) {
        CHECK(tx.nb == 0 && tx.be == 3 && tx.retries == i);
        CALL(draw(0));
        CALL(sent());
        now += MAC_TX_ACK_SYMBOLS;
        CALL(poll());
        CHECK(tx.outcome == MAC_TX_NO_ACK && tx.frame[2] == 0x32);
        CALL(quiesce());
        CHECK(tx.phase == (i == 3 ? MAC_TX_DONE : MAC_TX_DRAW));
    }
    CHECK(tx.transmissions == 4 && tx.retries == 3 && tx.outcome == MAC_TX_NO_ACK);
    return 0;
}

static uint16_t ack_edges(void)
{
    CALL(begin(1, 0x44, 10, 10000, 100));
    CALL(draw(0));
    CALL(sent());
    now += 34;
    source(MAC_TX_EVENT_ACK);
    ack[0] = 0xfa; /* Pending and all other low ACK FCF bits; ignored on RX. */
    ack[1] = 0xff;
    ack[2] = 0x44;
    event.bytes = ack;
    for (i = 0; i <= 4; i++) {
        if (i == 3)
            continue;
        event.length = i;
        CALL(step());
        CHECK(tx.phase == MAC_TX_ACK_WAIT);
    }
    event.length = 3;
    event.generation++;
    CALL(step());
    CHECK(tx.phase == MAC_TX_ACK_WAIT);
    event.generation--;
    event.nb++;
    CALL(step());
    CHECK(tx.phase == MAC_TX_ACK_WAIT);
    event.nb--;
    event.retry++;
    CALL(step());
    CHECK(tx.phase == MAC_TX_ACK_WAIT);
    event.retry--;
    event.stamp--;
    CALL(step());
    CHECK(tx.phase == MAC_TX_ACK_WAIT);
    event.stamp = now;
    CALL(step());
    CHECK(tx.outcome == MAC_TX_ACKED && tx.pending == 1);
    CALL(quiesce());
    saved = tx;
    CALL(step()); /* duplicate QUIESCED after DONE cannot complete twice */
    CHECK(memcmp(&saved, &tx, sizeof(tx)) == 0);
    CALL(begin(1, 0x44, 0, 10000, 100));
    CALL(draw(0));
    CALL(sent());
    now += 34;
    CALL(receive(0x45)); /* Wrong DSN fails this attempt, not success or rebind. */
    CHECK(tx.outcome == MAC_TX_NO_ACK && tx.retry_pending == 1);
    CALL(quiesce());
    CALL(draw(0));
    CHECK(action.at == tx.tx_end + MAC_TX_ACK_SYMBOLS + 12u);
    source(MAC_TX_EVENT_ACK); /* Old ACK during a new CCA/TX action. */
    event.bytes = ack;
    event.length = 3;
    CALL(step());
    CHECK(tx.phase == MAC_TX_RADIO);
    CALL(sent());
    now += MAC_TX_ACK_SYMBOLS;
    CALL(receive(0x44)); /* Completion exactly at the boundary, before timeout. */
    CHECK(tx.outcome == MAC_TX_ACKED);
    CALL(quiesce());
    CALL(begin(1, 0x44, 0, 10000, 100));
    CALL(draw(0));
    CALL(sent());
    now += MAC_TX_ACK_SYMBOLS + 1u;
    CALL(receive(0x44));
    CHECK(tx.outcome == MAC_TX_NO_ACK && tx.phase == MAC_TX_STOPPING);
    return 0;
}

static uint16_t cancellation_and_faults(void)
{
    for (i = 0; i < 3; i++) {
        CALL(begin(1, 1, 0, 10000, 100));
        if (i != 0)
            CALL(draw(0));
        if (i == 2)
            CALL(sent());
        source(MAC_TX_EVENT_CANCEL);
        CALL(step());
        CHECK(tx.outcome == MAC_TX_CANCELLED);
        if (i != 0)
            CALL(quiesce());
        CHECK(tx.phase == MAC_TX_DONE && tx.uncertain == (i == 1));
        if (i == 1)
            CHECK(tx.ready_at == now + MAC_TX_ACK_SYMBOLS + 12u);
        if (i == 2)
            CHECK(tx.ready_at == tx.tx_end + MAC_TX_ACK_SYMBOLS + 12u);
    }
    CALL(begin(1, 1, 0, 10000, 100));
    CALL(draw(0));
    source(MAC_TX_EVENT_FAILURE);
    CALL(step());
    CHECK(tx.phase == MAC_TX_FAULT && tx.outcome == MAC_TX_ADAPTER_ERROR && tx.uncertain);
    saved = tx;
    CALL(poll());
    CHECK(memcmp(&tx, &saved, sizeof(tx)) == 0);
    CHECK(mac_tx_release(&tx) == MAC_TX_STATE);
    CHECK(mac_tx_submit(&tx, body, 12, now, 1000, 10) == MAC_TX_FULL);
    CALL(begin(1, 1, 0, 10000, 100));
    CALL(draw(0));
    source(MAC_TX_EVENT_SENT); /* Cannot complete before the scheduled frame. */
    CALL(step());
    CHECK(tx.phase == MAC_TX_FAULT && tx.outcome == MAC_TX_ADAPTER_ERROR);
    for (i = 0; i < 2; i++) {
        CALL(begin(1, 1, 20, 10000, 100));
        now += i == 0 ? UINT32_MAX : MAC_TX_HALF;
        CALL(poll());
        CHECK(tx.phase == MAC_TX_FAULT && tx.outcome == MAC_TX_CLOCK_ERROR);
    }
    CALL(begin(1, 1, 0, 10000, 100));
    CALL(draw(0));
    source(MAC_TX_EVENT_CANCEL);
    CALL(step());
    for (i = 0; i < MAC_TX_STOP_STEPS + 1u; i++)
        CALL(poll());
    CHECK(tx.phase == MAC_TX_FAULT && tx.outcome == MAC_TX_STOP_FAILED && tx.uncertain);
    CALL(begin(1, 1, 0, 10000, 100));
    CALL(draw(0));
    source(MAC_TX_EVENT_CANCEL);
    CALL(step());
    now += MAC_TX_STOP_SYMBOLS;
    source(MAC_TX_EVENT_QUIESCED);
    CALL(step());
    CHECK(tx.phase == MAC_TX_FAULT && tx.outcome == MAC_TX_STOP_FAILED);
    return 0;
}

static uint16_t lifetimes_and_errors(void)
{
    CALL(begin(1, 1, 0, 100, 1));
    CALL(poll());
    CALL(poll());
    CHECK(tx.phase == MAC_TX_DONE && tx.outcome == MAC_TX_WORK_LIMIT);
    CALL(begin(1, 1, 0xfffffff0UL, 100, 100));
    now += 100;
    CALL(poll());
    CHECK(tx.phase == MAC_TX_DONE && tx.outcome == MAC_TX_LIFETIME);
    CALL(begin(1, 1, 0, 10, 100));
    CALL(poll());
    source(MAC_TX_EVENT_RANDOM);
    event.value = 1;
    CALL(step());
    CHECK(tx.phase == MAC_TX_DONE && tx.outcome == MAC_TX_LIFETIME);
    CALL(begin(1, 1, 0, 100, 100));
    CALL(draw(0));
    now = 100;
    CALL(poll());
    CHECK(tx.outcome == MAC_TX_LIFETIME && tx.uncertain);
    CALL(quiesce());
    CALL(begin(1, 1, 0, 10000, 100));
    CALL(draw(0));
    CALL(sent());
    now += MAC_TX_ACK_SYMBOLS;
    CALL(poll());
    now = tx.deadline;
    source(MAC_TX_EVENT_QUIESCED);
    CALL(step());
    CHECK(tx.phase == MAC_TX_FAULT && tx.outcome == MAC_TX_STOP_FAILED);
    CALL(begin(1, 1, 0, 120, 100));
    CALL(draw(0));
    CALL(sent());
    now += MAC_TX_ACK_SYMBOLS;
    CALL(poll());
    now = 120;
    source(MAC_TX_EVENT_QUIESCED);
    CALL(step());
    CHECK(tx.phase == MAC_TX_DONE && tx.outcome == MAC_TX_LIFETIME);
    CALL(begin(1, 1, 0, 1000, 100));
    saved = tx;
    memset(&action, 0xc7, sizeof(action));
    saved_action = action;
    source(0xff);
    CHECK(mac_tx_step(&tx, now, &event, &action) == MAC_TX_INVALID);
    source(MAC_TX_EVENT_ACK);
    event.length = 3;
    CHECK(mac_tx_step(&tx, now, &event, &action) == MAC_TX_INVALID);
    CHECK(mac_tx_step(&tx, now, NULL, NULL) == MAC_TX_INVALID);
    CHECK(memcmp(&saved, &tx, sizeof(tx)) == 0);
    CHECK(memcmp(&saved_action, &action, sizeof(action)) == 0);
    return 0;
}

#ifdef CC2530_HOST_TEST
static uint16_t self_test(void)
#else
volatile __xdata __at(0x1e00) uint8_t mac_tx_result[8];

void main(void)
#endif
{
    uint16_t result = initialization();
    if (!result)
        result = admission();
    if (!result)
        result = beacon_requests();
    if (!result)
        result = success_and_spacing();
    if (!result)
        result = backoff_retry();
    if (!result)
        result = ack_edges();
    if (!result)
        result = cancellation_and_faults();
    if (!result)
        result = lifetimes_and_errors();
#ifdef CC2530_HOST_TEST
    return result;
#else
    mac_tx_result[0] = 'M';
    mac_tx_result[1] = 'T';
    mac_tx_result[2] = 'X';
    mac_tx_result[3] = '1';
    mac_tx_result[4] = 1;
    mac_tx_result[5] = 8;
    mac_tx_result[6] = (uint8_t)result;
    mac_tx_result[7] = (uint8_t)(result >> 8);
    __asm
        .globl _mac_tx_done
    _mac_tx_done:
        nop
    __endasm;
    for (;;) {}
#endif
}

#ifdef CC2530_HOST_TEST
#include <stdio.h>
#include <stdlib.h>

static uint16_t exhaustive(void)
{
    uint32_t fcf;
    unsigned n, j;
    uint8_t *exact;
    mac_header_t header;
    mac_frame_info_t decoded;

    CHECK(mac_frame_decode(data_request, sizeof(data_request), &decoded) == MAC_CODEC_OK);
    CHECK(decoded.header.type == MAC_FRAME_COMMAND);
    for (fcf = 2; fcf <= 0xffffu; fcf += 8) {
        CALL(begin(1, 0x5a, 0, 10000, 100));
        CALL(draw(0));
        CALL(sent());
        now += 34;
        ack[0] = (uint8_t)fcf;
        ack[1] = (uint8_t)(fcf >> 8);
        ack[2] = 0x5a;
        source(MAC_TX_EVENT_ACK);
        event.bytes = ack;
        event.length = 3;
        CALL(step());
        CHECK(tx.outcome == MAC_TX_ACKED && tx.pending == ((fcf & 0x10u) != 0u));
    }
    for (n = 0; n <= 255; n++) {
        CALL(begin(1, (uint8_t)n, 0, 10000, 100));
        CALL(draw((uint8_t)n));
        CHECK(action.at == (uint32_t)(n & 7u) * 20u);
        CALL(sent());
        now += 34;
        CALL(receive((uint8_t)n));
        CHECK(tx.outcome == MAC_TX_ACKED);
    }
    for (n = 0; n <= 126; n++) {
        exact = malloc(n ? n : 1);
        CHECK(exact != NULL);
        memset(exact, 0xc7, n);
        CALL(begin(1, 1, 0, 10000, 100));
        length = 0xa5;
        CHECK(mac_tx_copy(&tx, exact, (uint16_t)n, &length)
              == (n < 12 ? MAC_TX_SPACE : MAC_TX_OK));
        if (n < 12) {
            CHECK(length == 0xa5);
            for (j = 0; j < n; j++)
                CHECK(exact[j] == 0xc7);
        }
        free(exact);
    }
    for (n = 0; n <= 126; n++) {
        exact = malloc(n ? n : 1);
        CHECK(exact != NULL);
        memset(exact, 0x69, n);
        memcpy(exact, golden, n < sizeof(golden) ? n : sizeof(golden));
        CHECK(mac_tx_init(&tx, 0x5a, 0) == MAC_TX_OK);
        saved = tx;
        CHECK(mac_tx_submit(&tx, exact, (uint16_t)n, 0, 10000, 100)
              == (n < 9 || n > 125 ? MAC_TX_UNSUPPORTED : MAC_TX_OK));
        if (n < 9 || n > 125)
            CHECK(memcmp(&saved, &tx, sizeof(tx)) == 0);
        memset(exact, 0x69, n);
        memcpy(exact, beacon_request, n < sizeof(beacon_request) ? n : sizeof(beacon_request));
        CHECK(mac_tx_init(&tx, 0x5a, 0) == MAC_TX_OK);
        saved = tx;
        CHECK(mac_tx_submit(&tx, exact, (uint16_t)n, 0, 10000, 100)
              == (n == sizeof(beacon_request) ? MAC_TX_OK : MAC_TX_UNSUPPORTED));
        if (n != sizeof(beacon_request))
            CHECK(memcmp(&saved, &tx, sizeof(tx)) == 0);
        free(exact);
    }
    for (j = 0; j < sizeof(beacon_request); j++) {
        for (n = 0; n <= 255; n++) {
            memcpy(body, beacon_request, sizeof(beacon_request));
            body[j] = (uint8_t)n;
            CHECK(mac_tx_init(&tx, 0x5a, 0) == MAC_TX_OK);
            saved = tx;
            CHECK(mac_tx_submit(&tx, body, sizeof(beacon_request), 0, 10000, 100)
                  == (j == 2 || n == beacon_request[j] ? MAC_TX_OK : MAC_TX_UNSUPPORTED));
            if (j != 2 && n != beacon_request[j])
                CHECK(memcmp(&saved, &tx, sizeof(tx)) == 0);
        }
    }
    now = 0;
    CHECK(mac_tx_init(&tx, 0x5a, now) == MAC_TX_OK);
    CHECK(mac_tx_submit(&tx, beacon_request, 8, now, 10000, 100) == MAC_TX_OK);
    for (n = 0; n < 5; n++) {
        CALL(draw(0));
        now = action.at + 8u;
        source(MAC_TX_EVENT_BUSY);
        CALL(step());
    }
    CHECK(tx.phase == MAC_TX_DONE && tx.outcome == MAC_TX_CHANNEL_ACCESS);
    CHECK(tx.transmissions == 0 && tx.retries == 0 && !tx.uncertain);
    for (n = 0; n <= 4; n++) {
        exact = malloc(n ? n : 1);
        CHECK(exact != NULL);
        memset(exact, 0, n);
        if (n != 0)
            exact[0] = 2;
        if (n >= 3)
            exact[2] = 1;
        CALL(begin(1, 1, 0, 10000, 100));
        CALL(draw(0));
        CALL(sent());
        now += 34;
        source(MAC_TX_EVENT_ACK);
        event.bytes = exact;
        event.length = (uint16_t)n;
        CALL(step());
        CHECK(tx.phase == (n == 3 ? MAC_TX_STOPPING : MAC_TX_ACK_WAIT));
        free(exact);
    }
    /* Reuse the real encoder for every admitted DATA address/version/PAN/AR
     * combination; mismatched canonical compression remains a TX rejection.
     */
    for (n = 0; n < 32; n++) {
        memset(&header, 0, sizeof(header));
        header.type = MAC_FRAME_DATA;
        header.version = (uint8_t)(n & 1u);
        header.destination_mode = (n & 2u) ? MAC_ADDRESS_EXTENDED : MAC_ADDRESS_SHORT;
        header.source_mode = (n & 4u) ? MAC_ADDRESS_EXTENDED : MAC_ADDRESS_SHORT;
        header.source_pan = 0x1234;
        header.destination_pan = (n & 8u) ? 0x2345 : 0x1234;
        header.flags = (n & 8u) ? 0 : MAC_FLAG_PAN_COMPRESSION;
        if (n & 16u)
            header.flags |= MAC_FLAG_ACK_REQUEST;
        memset(header.source, 0x31, sizeof(header.source));
        memset(header.destination, 0x42, sizeof(header.destination));
        CHECK(mac_frame_encode(&header, NULL, 0, body, 125, &length) == MAC_CODEC_OK);
        CHECK(mac_tx_init(&tx, 0x5a, 0) == MAC_TX_OK);
        CHECK(mac_tx_submit(&tx, body, length, 0, 10000, 100) == MAC_TX_OK);
        CHECK(tx.frame[2] == 0x5a && tx.length == length);
        CHECK(mac_tx_init(&tx, 0x5a, 0) == MAC_TX_OK);
        header.flags &= (uint8_t)~MAC_FLAG_PAN_COMPRESSION;
        header.destination_pan = header.source_pan;
        CHECK(mac_frame_encode(&header, NULL, 0, body, 125, &length) == MAC_CODEC_OK);
        CHECK(mac_tx_submit(&tx, body, length, 0, 10000, 100) == MAC_TX_UNSUPPORTED);
    }
    CALL(begin(1, 1, 0, 10000, 100));
    CALL(draw(0));
    CALL(sent());
    now += 34;
    source(MAC_TX_EVENT_ACK);
    ack[0] = 2;
    ack[1] = 0;
    ack[2] = 1;
    event.bytes = ack;
    event.length = 3;
    event.stamp = now + 1u; /* Future and pre-transmission stamps do not match. */
    CALL(step());
    CHECK(tx.phase == MAC_TX_ACK_WAIT);
    event.stamp = tx.tx_end;
    CALL(step());
    CHECK(tx.phase == MAC_TX_ACK_WAIT);
    event.stamp = now;
    ack[0] = 1; /* A DATA frame is not an ACK, even with matching DSN. */
    CALL(step());
    CHECK(tx.phase == MAC_TX_ACK_WAIT);
    ack[0] = 2;
    CALL(step());
    CHECK(tx.outcome == MAC_TX_ACKED);
    return 0;
}

int main(void)
{
    uint16_t result = self_test();
    if (!result)
        result = exhaustive();
    if (result) {
        fprintf(stderr, "MAC TX failure at C line %u\n", (unsigned)result);
        return 1;
    }
    puts("MAC TX: portable state corpus, 8192 ACK FCFs, 256 DSNs/draws, "
         "2048 Beacon Request variants, exact bounds PASS");
    return 0;
}
#endif
