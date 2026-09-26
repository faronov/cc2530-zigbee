/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "mac_join.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const mac_join_request_t config = {
    {1, 300, 100000, 256, 0x1234, 15, 3, 3,
     {1,2,3,4,5,6,7,8}, {17,18,19,20,21,22,23,24}},
    {0xffff, 11, 0, 0}, 2, 0x88, MAC_RX_IEEE2006
};
static const uint8_t response[] = {
    0x73,0xcc,0x91,0x34,0x12,1,2,3,4,5,6,7,8,
    17,18,19,20,21,22,23,24,2,0x78,0x56,0
};
static const uint8_t request_wire[] = {
    0x23,0xcc,0xff,0x34,0x12,17,18,19,20,21,22,23,24,
    0xff,0xff,1,2,3,4,5,6,7,8,1,0x88
};
static const uint8_t poll_wire[] = {
    0x63,0xcc,0,0x34,0x12,17,18,19,20,21,22,23,24,
    1,2,3,4,5,6,7,8,4
};
static mac_join_t join;
static mac_poll_t poll;
static mac_tx_interval_t tx;
static mac_join_request_t request;
static mac_join_event_t event;
static mac_join_action_t action;
static mac_poll_action_t pa;
static mac_tx_interval_action_t radio;
static mac_join_record_t record;
static mac_poll_record_t poll_record;
static uint32_t now, a, b, request_b;
static uint16_t fine;
static uint8_t joining, ack[3], frame[125];
static const char *case_name;
static unsigned cases, checks;

#define CHECK(c) do { checks++; if (!(c)) { \
    fprintf(stderr, "%s: line %u: %s (join=%u poll=%u tx=%u now=%lu)\n", \
        case_name, (unsigned)__LINE__, #c, join.phase, \
        joining ? join.poll.control.phase : poll.control.phase, tx.engine.phase, \
        (unsigned long)now); \
    fprintf(stderr, "observation=%u reason=%u fine=%u\n", pa.observation, \
        consumer()->control.reason, fine); exit(1); } } while (0)

static mac_poll_t *consumer(void)
{
    return joining ? &join.poll : &poll;
}

static uint8_t action_kind(void)
{
    return joining ? action.kind : pa.kind;
}

static void tick(const mac_join_event_t *e)
{
    if (joining)
        CHECK(mac_join_step(&join, &tx, now, e, &action) == MAC_JOIN_OK);
    else
        CHECK(mac_poll_step(&poll, &tx, now, e, &pa) == MAC_POLL_OK);
    CHECK(tx.engine.tx_end == 0);
}

static void input(uint8_t jk, uint8_t pk)
{
    memset(&event, 0, sizeof(event));
    event.kind = joining ? jk : pk;
    event.epoch = request.extraction.epoch;
    event.generation = joining ? join.generation : poll.control.generation;
    event.token = joining ? action.token : pa.token;
    event.stamp = now;
}

static void begin(uint8_t nested, uint32_t start, uint16_t phase,
                  uint16_t work, uint32_t life)
{
    joining = nested;
    now = start;
    fine = phase;
    request = config;
    request.extraction.work = work;
    request.extraction.lifetime = life;
    memset(&action, 0, sizeof(action));
    memset(&pa, 0, sizeof(pa));
    CHECK(mac_tx_interval_init(&tx, nested ? 0xff : 0, now) == MAC_TX_OK);
    if (nested) {
        CHECK(mac_join_init(&join, now) == MAC_JOIN_OK);
        CHECK(mac_join_start(&join, &tx, &request, now) == MAC_JOIN_OK);
    } else {
        CHECK(mac_poll_init(&poll) == MAC_POLL_OK);
        CHECK(mac_poll_start(&poll, &tx, &request.extraction, now) == MAC_POLL_OK);
    }
    tick(NULL);
    CHECK(action_kind() == (nested ? MAC_JOIN_ACTION_PREPARE : MAC_POLL_ACTION_PREPARE));
}

static void prepare(void)
{
    input(MAC_JOIN_PREPARED, MAC_POLL_PREPARED);
    tick(&event);
    if (joining && action.kind == MAC_JOIN_ACTION_ARM) {
        CHECK(tx.engine.phase == MAC_TX_DRAW);
        input(MAC_JOIN_ARMED, 0);
        tick(&event);
        CHECK(action.kind == MAC_JOIN_ACTION_RADIO &&
              action.radio.control.kind == MAC_TX_ACTION_RANDOM);
    }
}

static void check_wire(uint8_t association)
{
    uint8_t length = 0x55, bytes[25], saved[25];
    const uint8_t *golden = association ? request_wire : poll_wire;
    uint8_t size = association ? sizeof(request_wire) : sizeof(poll_wire);
    memset(bytes, 0xa5, sizeof(bytes));
    memcpy(saved, bytes, sizeof(bytes));
    CHECK(mac_tx_interval_copy(&tx, bytes, (uint16_t)(size - 1), &length) == MAC_TX_SPACE);
    CHECK(length == 0x55 && !memcmp(bytes, saved, sizeof(bytes)));
    CHECK(mac_tx_interval_copy(&tx, bytes, size, &length) == MAC_TX_OK);
    CHECK(length == size && !memcmp(bytes, golden, size));
}

/* Synthetic physical assertions drive the real owner, never controller fields. */
static void pump(uint8_t pending, uint8_t style)
{
    uint8_t internal = joining && join.phase == MAC_JOIN_REQUEST;
    uint8_t phase = tx.engine.phase;
    uint32_t end;
    if (internal && join.issued == MAC_JOIN_ACTION_ARM) {
        CHECK(phase == MAC_TX_DRAW);
        input(MAC_JOIN_ARMED, 0);
        tick(&event);
        return;
    }
    input(internal ? MAC_JOIN_SOURCE : MAC_JOIN_TX, MAC_POLL_TX);
    event.source.source.generation = tx.engine.generation;
    event.source.source.retry = tx.engine.retries;
    event.source.source.nb = tx.engine.nb;
    if (phase == MAC_TX_DRAW) {
        event.source.source.kind = 0;
    } else if (phase == MAC_TX_DRAW_WAIT) {
        event.source.source.kind = MAC_TX_EVENT_RANDOM;
    } else if (phase == MAC_TX_RADIO) {
        event.source.source.kind = MAC_TX_EVENT_SENT_INTERVAL;
        end = tx.engine.at + 24u + 2u * tx.engine.length;
        event.source.lower.symbols = end;
        event.source.lower.fine = fine;
        event.source.upper.symbols = end + 4u;
        event.source.upper.fine = fine;
        now = end + 4u + (fine != 0);
    } else if (phase == MAC_TX_ACK_WAIT) {
        if (style == 2) {
            event.source.source.kind = MAC_TX_EVENT_RX_CLOSED;
            event.source.upper = tx.tx_upper;
            event.source.upper.symbols += 54u;
            now = event.source.upper.symbols + (fine != 0);
        } else {
            event.source.source.kind = MAC_TX_EVENT_ACK_INTERVAL;
            event.source.lower = tx.tx_upper;
            event.source.lower.symbols += 12u;
            event.source.upper = tx.tx_lower;
            event.source.upper.symbols += style == 1 ? 54u : 40u;
            if (style == 1 && ++event.source.upper.fine == 512u) {
                event.source.upper.fine = 0;
                event.source.upper.symbols++;
            }
            a = event.source.lower.symbols;
            b = event.source.upper.symbols + (event.source.upper.fine != 0);
            now = b;
            if (internal) request_b = b;
            ack[0] = pending ? 0x12 : 2;
            ack[1] = 0;
            ack[2] = tx.engine.frame[2];
            event.source.source.bytes = ack;
            event.source.source.length = 3;
            event.crc_valid = 1;
        }
    } else {
        CHECK(phase == MAC_TX_STOPPING);
        event.source.source.kind = MAC_TX_EVENT_RETIRED;
        event.source.upper.symbols = now;
    }
    event.stamp = event.source.source.stamp = now;
    if (!internal) {
        CHECK(action_kind() == (joining ? MAC_JOIN_ACTION_TX : MAC_POLL_ACTION_TX));
        event.tx_result = mac_tx_observed_step(&tx, now,
            event.source.source.kind ? &event.source : NULL, &radio);
        CHECK(event.tx_result == MAC_TX_OK);
    }
    tick(internal && !event.source.source.kind ? NULL : &event);
    if (internal && action.kind == MAC_JOIN_ACTION_DISARM) {
        CHECK(tx.engine.phase == MAC_TX_DONE);
        input(MAC_JOIN_DISARMED, 0);
        tick(&event);
    }
}

static void to_ack(void)
{
    unsigned limit = 0;
    while (tx.engine.phase != MAC_TX_ACK_WAIT) {
        CHECK(++limit < 8);
        pump(1, 0);
    }
}

static void request_success(void)
{
    prepare();
    check_wire(1);
    to_ack();
    pump(1, 0);
    CHECK(tx.engine.outcome == MAC_TX_ACKED && tx.engine.phase == MAC_TX_STOPPING);
    CHECK(join.record.request_ack == request_b && join.wait_until == request_b + 1920u);
    pump(1, 0);
    CHECK(join.phase == MAC_JOIN_WAIT && tx.engine.phase == MAC_TX_IDLE);
    now = request_b + 1919u;
    tick(NULL);
    CHECK(join.phase == MAC_JOIN_WAIT && action.kind == MAC_JOIN_ACTION_NONE);
    now++;
    tick(NULL);
    CHECK(join.phase == MAC_JOIN_EXTRACT && action.kind == MAC_JOIN_ACTION_RECEIVE);
    prepare();
    check_wire(0);
}

static void poll_ack(uint8_t pending)
{
    to_ack();
    pump(pending, 0);
    CHECK(tx.engine.outcome == MAC_TX_ACKED && consumer()->control.ack_seen);
    CHECK(consumer()->control.ack_end == a && consumer()->control.ack_upper == b);
    CHECK(consumer()->control.accept_end == a + 300u);
    CHECK(consumer()->control.receive_end == b + 300u);
    pump(pending, 0);
    CHECK(tx.engine.phase == MAC_TX_DONE);
}

static void frame_event(uint32_t stamp, uint32_t serial, uint8_t crc, uint16_t length)
{
    now = stamp;
    input(MAC_JOIN_FRAME, MAC_POLL_FRAME);
    event.serial = serial;
    event.crc_valid = crc;
    event.channel = 15;
    event.body = frame;
    event.length = length;
}

static void close_poll(uint32_t through)
{
    CHECK(consumer()->control.close_issued);
    input(MAC_JOIN_CLOSED, MAC_POLL_CLOSED);
    event.token = joining ? join.issued_token : poll.control.close_token;
    event.through = through;
    tick(&event);
}

static void finish(void)
{
    if (joining) {
        CHECK(join.phase == MAC_JOIN_RESTORE && action.kind == MAC_JOIN_ACTION_RESTORE);
        CHECK(!memcmp(&action.state, &request.saved, sizeof(request.saved)));
        if ((uint32_t)(now - tx.engine.ready_at) >= MAC_TX_HALF)
            now = tx.engine.ready_at;
        input(MAC_JOIN_RESTORED, 0);
        tick(&event);
        CHECK(join.phase == MAC_JOIN_DONE);
        CHECK(mac_join_take(&join, &record) == MAC_JOIN_OK);
        CHECK(mac_join_take(&join, &record) == MAC_JOIN_STATE);
        CHECK(mac_join_release(&join, &tx) == MAC_JOIN_OK);
    } else {
        CHECK(poll.control.phase == MAC_POLL_DONE);
        CHECK(mac_poll_take(&poll, &poll_record) == MAC_POLL_OK);
        CHECK(mac_poll_take(&poll, &poll_record) == MAC_POLL_STATE);
        CHECK(mac_poll_release(&poll, &tx) == MAC_POLL_OK);
    }
    CHECK(tx.engine.phase == MAC_TX_IDLE);
}

static void opened(uint8_t nested, uint32_t start, uint16_t phase)
{
    begin(nested, start, phase, 256, 100000);
    if (nested) request_success();
    else prepare();
    poll_ack(1);
    memcpy(frame, response, sizeof(response));
}

static void successful(uint32_t start, uint16_t phase)
{
    case_name = "association-at-A+F";
    opened(1, start, phase);
    frame_event(a + 300u, 1, 1, sizeof(response));
    tick(&event);
    CHECK(join.record.poll.protocol == MAC_POLL_NO_DATA && join.record.poll.cause == MAC_POLL_COMMAND);
    CHECK(join.record.association.outcome == MAC_ASSOCIATION_RESPONSE);
    CHECK(join.record.association.short_address == 0x5678);
    CHECK(join.record.association.stamp == a + 300u && join.record.poll.stamp == a + 300u);
    CHECK(!memcmp(join.record.poll.body, response, sizeof(response)));
    memset(frame, 0, sizeof(frame));
    close_poll(now);
    finish();
    CHECK(record.result == MAC_JOIN_POLL_RESULT && !record.reason && !record.cleanup_error);
    CHECK(record.request_ack == request_b && tx.engine.next_dsn == 1);
    cases++;
}

static void pending_zero(uint8_t nested, uint32_t start, uint16_t phase)
{
    case_name = "pending-zero";
    begin(nested, start, phase, 256, 100000);
    if (nested) request_success();
    else prepare();
    poll_ack(0);
    CHECK(consumer()->record.protocol == MAC_POLL_NO_DATA);
    CHECK(consumer()->record.cause == MAC_POLL_PENDING_ZERO && consumer()->record.stamp == b);
    close_poll(now);
    finish();
    cases++;
}

static void timeout(uint8_t early, uint32_t start, uint16_t phase)
{
    case_name = early ? "closure-too-early" : "loss-free-latest-closure";
    opened(0, start, phase);
    now = a + 300u;
    tick(NULL);
    CHECK(poll.control.phase == MAC_POLL_RECEIVE && !poll.control.ready);
    now = b + 300u;
    tick(NULL);
    CHECK(poll.control.timeout_pending && !poll.control.ready);
    CHECK(poll.record.protocol == MAC_POLL_NO_CONFIRM);
    close_poll(early ? b + 299u : b + 300u);
    if (early) {
        CHECK(poll.control.reason == MAC_POLL_ADAPTER_ERROR && !poll.control.closed);
        CHECK(poll.record.protocol == MAC_POLL_NO_CONFIRM);
        input(0, MAC_POLL_CLOSED);
        event.token = poll.control.close_token;
        event.through = now;
        tick(&event);
        CHECK(poll.control.phase == MAC_POLL_FAULT);
        CHECK(mac_poll_release(&poll, &tx) == MAC_POLL_STATE);
    } else {
        finish();
        CHECK(poll_record.protocol == MAC_POLL_NO_DATA && poll_record.cause == MAC_POLL_TIMEOUT);
        CHECK(poll_record.stamp == b + 300u);
    }
    cases++;
}

static void uncertain_frame(uint8_t nested, uint8_t timed_out, uint32_t start, uint16_t phase)
{
    case_name = timed_out ? "post-timeout-uncertain" : "frame-A+F-plus-one";
    opened(nested, start, phase);
    if (timed_out) {
        now = b + 300u;
        tick(NULL);
    }
    frame_event(timed_out ? b + 300u : a + 301u, 1, 1, sizeof(response));
    tick(&event);
    CHECK(consumer()->control.reason == MAC_POLL_TIMING_UNCERTAIN);
    CHECK(consumer()->record.protocol == MAC_POLL_NO_CONFIRM && !consumer()->record.length);
    CHECK(!consumer()->control.timeout_pending);
    if (nested) CHECK(join.record.result == MAC_JOIN_LOCAL_ABORT && join.record.reason == MAC_JOIN_TIMING_UNCERTAIN);
    close_poll(now);
    finish();
    cases++;
}

static void uncertain_ack(uint8_t nested, uint8_t extraction, uint32_t start, uint16_t phase)
{
    case_name = "ACK-one-fine-tick-straddle";
    begin(nested, start, phase, 256, 100000);
    if (extraction) request_success();
    else prepare();
    to_ack();
    pump(1, 1);
    CHECK(tx.engine.phase == MAC_TX_STOPPING && tx.engine.outcome == MAC_TX_TIMING_UNCERTAIN);
    CHECK(!tx.engine.retry_pending && !tx.engine.retries);
    if (nested && !extraction) {
        CHECK(join.record.result == MAC_JOIN_LOCAL_ABORT && join.record.reason == MAC_JOIN_TIMING_UNCERTAIN);
        CHECK(!join.record.request_ack);
    } else {
        CHECK(consumer()->control.tx_outcome == MAC_TX_TIMING_UNCERTAIN);
        CHECK(consumer()->control.reason == MAC_POLL_TIMING_UNCERTAIN);
        CHECK(consumer()->record.protocol == MAC_POLL_NO_CONFIRM);
    }
    pump(1, 0);
    if (!nested || extraction) close_poll(now);
    finish();
    if (nested) CHECK(record.reason == MAC_JOIN_TIMING_UNCERTAIN);
    cases++;
}

static void retry_request(uint8_t exhausted)
{
    uint8_t i;
    case_name = exhausted ? "Request-NO_ACK-exhaustion" : "Request-RX_CLOSED-retry";
    begin(1, 0xfffffff0u, 511, 256, 100000);
    prepare();
    for (i = 0; i < (exhausted ? 4 : 1); i++) {
        to_ack();
        pump(0, 2);
        CHECK(tx.engine.outcome == MAC_TX_NO_ACK && tx.engine.phase == MAC_TX_STOPPING);
        CHECK(tx.engine.retry_pending == (i < 3));
        pump(0, 0);
        if (i < 3) {
            CHECK(join.phase == MAC_JOIN_REQUEST && tx.engine.phase == MAC_TX_DRAW);
            CHECK(tx.engine.retries == i + 1u && tx.engine.frame[2] == 0xff);
        }
    }
    if (!exhausted) {
        to_ack();
        pump(1, 0);
        CHECK(tx.engine.outcome == MAC_TX_ACKED && tx.engine.retries == 1);
        pump(1, 0);
        CHECK(join.phase == MAC_JOIN_WAIT);
        input(MAC_JOIN_CANCEL, 0);
        tick(&event);
    } else {
        CHECK(join.record.result == MAC_JOIN_REQUEST_RESULT && join.record.tx_outcome == MAC_TX_NO_ACK);
    }
    finish();
    cases++;
}

static void classification(uint8_t outside, uint8_t which)
{
    uint32_t stamp;
    uint8_t expected;
    case_name = outside ? "classification-outside-window" : "classification-inside-window";
    opened(0, 10, 1);
    stamp = a + (outside ? 301u : 100u);
    if (outside == 2) {
        now = b + 300u;
        tick(NULL);
        stamp = now + 1u;
    }
    frame_event(stamp, 1, which != 0, sizeof(response));
    expected = MAC_POLL_OBS_BAD_CRC;
    if (which == 1) { frame[5]++; expected = MAC_POLL_OBS_FOREIGN; }
    if (which == 2) { event.channel = 16; expected = MAC_POLL_OBS_FOREIGN; }
    if (which == 3) { event.length--; expected = MAC_POLL_OBS_MALFORMED; }
    if (which == 4) { frame[0] |= 8; expected = MAC_POLL_OBS_UNSUPPORTED; }
    tick(&event);
    CHECK(pa.observation == expected && poll.record.protocol == MAC_POLL_NO_CONFIRM);
    CHECK(poll.control.reason == (which == 4 ? MAC_POLL_UNSUPPORTED : 0));
    if (which != 4) {
        CHECK(poll.control.phase == (outside == 2 ? MAC_POLL_DRAIN : MAC_POLL_RECEIVE));
        if (outside != 2) {
            now = b + 300u;
            tick(NULL);
        }
    }
    close_poll(now);
    finish();
    cases++;
}

static void stale_duplicate(void)
{
    case_name = "stale-and-duplicate-serials";
    opened(0, 100, 0);
    frame[5]++;
    frame_event(a + 100u, 1, 1, sizeof(response));
    tick(&event);
    CHECK(pa.observation == MAC_POLL_OBS_FOREIGN);
    tick(&event);
    CHECK(pa.observation == MAC_POLL_OBS_DUPLICATE);
    frame[5]--;
    event.serial = 2;
    event.generation++;
    tick(&event);
    CHECK(pa.observation == MAC_POLL_OBS_STALE && !poll.control.ready);
    event.generation--;
    event.epoch++;
    tick(&event);
    CHECK(pa.observation == MAC_POLL_OBS_STALE && !poll.control.ready);
    event.epoch--;
    tick(&event);
    CHECK(pa.observation == MAC_POLL_OBS_DELIVERY);
    close_poll(now);
    finish();
    cases++;
}

static void order_error(void)
{
    case_name = "in-window-frame-after-timeout";
    opened(0, 0, 1);
    now = b + 300u;
    tick(NULL);
    input(0, MAC_POLL_FRAME);
    event.stamp = a + 300u;
    event.serial = 1; event.channel = 15; event.crc_valid = 1;
    event.body = response; event.length = sizeof(response);
    tick(&event);
    CHECK(poll.control.reason == MAC_POLL_ORDER_ERROR && !poll.record.protocol);
    close_poll(now);
    CHECK(poll.control.phase == MAC_POLL_FAULT);
    cases++;
}

static void cancel_active(uint8_t nested)
{
    case_name = "cancellation-drain";
    begin(nested, 0, 1, 256, 100000);
    prepare();
    while (tx.engine.phase != MAC_TX_RADIO) pump(1, 0);
    input(MAC_JOIN_CANCEL, MAC_POLL_CANCEL);
    tick(&event);
    if (!nested) {
        CHECK(pa.kind == MAC_POLL_ACTION_NONE);
        /* The outstanding grant is consumed once with the cancellation. */
        input(0, MAC_POLL_TX);
        event.token = poll.control.tx_token;
        event.source.source.kind = MAC_TX_EVENT_CANCEL;
        event.source.source.generation = tx.engine.generation;
        event.source.source.retry = tx.engine.retries;
        event.source.source.nb = tx.engine.nb;
        event.source.source.stamp = now;
        event.tx_result = mac_tx_observed_step(&tx, now, &event.source, &radio);
        tick(&event);
    }
    CHECK(tx.engine.phase == MAC_TX_STOPPING);
    pump(0, 0);
    if (!nested) close_poll(now);
    finish();
    CHECK(nested ? record.reason == MAC_JOIN_CANCELLED : poll.control.reason == MAC_POLL_CANCELLED);
    cases++;
}

static void budget(uint8_t nested, uint8_t lifetime)
{
    case_name = lifetime ? "lifetime-exhaustion" : "work-exhaustion";
    begin(nested, 10, 0, lifetime ? 256 : 1, lifetime ? 5 : 100000);
    if (lifetime) now += 5;
    tick(NULL);
    CHECK(nested ? join.record.reason == (lifetime ? MAC_JOIN_LIFETIME : MAC_JOIN_WORK_LIMIT)
                 : poll.control.reason == (lifetime ? MAC_POLL_LIFETIME : MAC_POLL_WORK_LIMIT));
    if (!nested) close_poll(now);
    finish();
    cases++;
}

static void invalid_inputs(uint8_t nested)
{
    mac_join_t saved_join;
    mac_poll_t saved_poll;
    mac_tx_interval_t saved_tx;
    mac_join_action_t saved_action;
    mac_poll_action_t saved_pa;
    unsigned i;
    case_name = "invalid-inputs-atomic";
    begin(nested, 0, 1, 256, 100000);
    saved_join = join; saved_poll = poll; saved_tx = tx;
    saved_action = action; saved_pa = pa;
    for (i = 0; i < 10; i++) {
        input(MAC_JOIN_TX, MAC_POLL_TX);
        event.source.source.kind = MAC_TX_EVENT_ACK_INTERVAL;
        event.source.source.bytes = ack;
        event.source.source.length = 3;
        if (i == 0) event.kind = 255;
        if (i == 1) event.crc_valid = 2;
        if (i == 2) event.source.source.kind = MAC_TX_EVENT_ACK;
        if (i == 3) event.source.source.kind = MAC_TX_EVENT_SENT;
        if (i == 4) event.source.source.bytes = NULL;
        if (i == 5) event.source.upper.fine = 512;
        if (i == 6) event.source.lower.fine = 512;
        if (i == 7) event.source.lower.fine = 1;
        if (i == 8) event.source.upper.symbols = 1;
        if (i == 9) event.source.source.kind = 255;
        if (nested)
            CHECK(mac_join_step(&join, &tx, now, &event, &action) == MAC_JOIN_INVALID);
        else
            CHECK(mac_poll_step(&poll, &tx, now, &event, &pa) == MAC_POLL_INVALID);
        CHECK(!memcmp(&join, &saved_join, sizeof(join)) && !memcmp(&poll, &saved_poll, sizeof(poll)));
        CHECK(!memcmp(&tx, &saved_tx, sizeof(tx)));
        CHECK(!memcmp(&action, &saved_action, sizeof(action)) && !memcmp(&pa, &saved_pa, sizeof(pa)));
    }
    request.extraction.frame_wait = 0;
    if (nested)
        CHECK(mac_join_start(&join, &tx, &request, now) == MAC_JOIN_INVALID);
    else
        CHECK(mac_poll_start(&poll, &tx, &request.extraction, now) == MAC_POLL_INVALID);
    CHECK(!memcmp(&join, &saved_join, sizeof(join)) && !memcmp(&poll, &saved_poll, sizeof(poll)));
    CHECK(!memcmp(&tx, &saved_tx, sizeof(tx)));
    cases++;
}

static void ack_witness(uint8_t damage)
{
    mac_tx_interval_event_t original;
    case_name = "original-ACK-witness";
    begin(0, 100, 1, 256, 100000);
    prepare(); to_ack();
    input(0, MAC_POLL_TX);
    event.source.source.kind = MAC_TX_EVENT_ACK_INTERVAL;
    event.source.source.generation = tx.engine.generation;
    event.source.source.retry = tx.engine.retries;
    event.source.source.nb = tx.engine.nb;
    event.source.lower = tx.tx_upper;
    event.source.lower.symbols += 12u;
    event.source.upper = tx.tx_lower;
    event.source.upper.symbols += 40u;
    if (damage == 11) event.source.lower = tx.tx_lower;
    a = event.source.lower.symbols;
    b = event.source.upper.symbols + 1u;
    now = b + 1u; /* An original report may precede the step's now. */
    event.stamp = now;
    event.source.source.stamp = b;
    ack[0] = 0x12; ack[1] = 0; ack[2] = tx.engine.frame[2];
    event.source.source.bytes = ack; event.source.source.length = 3; event.crc_valid = 1;
    original = event.source;
    event.tx_result = mac_tx_observed_step(&tx, now, &original, &radio);
    CHECK(event.tx_result == MAC_TX_OK && tx.engine.outcome == MAC_TX_ACKED);
    if (damage == 1) event.crc_valid = 0;
    if (damage == 2) event.source.source.generation++;
    if (damage == 3) event.source.source.retry++;
    if (damage == 4) event.source.source.nb++;
    if (damage == 5) event.source.source.length = 2;
    if (damage == 6) ack[0] = 0x13;
    if (damage == 7) ack[2]++;
    if (damage == 8) ack[0] = 2;
    if (damage == 9) event.source.lower.symbols = tx.tx_lower.symbols - 1u;
    if (damage == 10) event.source.source.kind = 0;
    tick(&event);
    CHECK(poll.control.reason == (damage && damage != 11 ? MAC_POLL_TX_ERROR : 0));
    CHECK(poll.control.ack_seen == (!damage || damage == 11));
    pump(1, 0);
    if (damage && damage != 11) {
        close_poll(now);
        CHECK(poll.control.phase == MAC_POLL_FAULT && !poll.record.protocol);
    } else {
        memcpy(frame, response, sizeof(response));
        frame_event(a + 300u, 1, 1, sizeof(response));
        tick(&event);
        close_poll(now); finish();
    }
    cases++;
}

static void response_spans(void)
{
    uint16_t size;
    case_name = "response-exact-truncated-trailing";
    for (size = 0; size <= sizeof(response) + 1u; size++) {
        uint8_t *bytes = malloc(size ? size : 1u);
        CHECK(bytes != NULL);
        opened(0, 100, 511);
        case_name = "response-exact-truncated-trailing";
        memcpy(bytes, response, size < sizeof(response) ? size : sizeof(response));
        if (size > sizeof(response)) bytes[size - 1u] = 0;
        frame_event(a + 100u, 1, 1, size);
        event.body = bytes;
        tick(&event);
        free(bytes);
        if (size == sizeof(response)) {
            CHECK(pa.observation == MAC_POLL_OBS_DELIVERY && poll.record.length == size);
        } else {
            CHECK(pa.observation == MAC_POLL_OBS_MALFORMED && !poll.control.ready);
            input(0, MAC_POLL_CANCEL); tick(&event);
        }
        close_poll(now); finish();
        cases++;
    }
}

static void fine_tick_frame(void)
{
    mac_epoch_stamp_t observed;
    case_name = "frame-one-fine-tick-beyond-A+F";
    opened(0, 0xfffffff0u, 511);
    observed.symbols = a + 300u;
    observed.fine = 1;
    /* The adapter's upward projection loses any proof of lateness. */
    frame_event(observed.symbols + (observed.fine != 0), 1, 1, sizeof(response));
    tick(&event);
    CHECK(poll.control.reason == MAC_POLL_TIMING_UNCERTAIN && !poll.record.protocol);
    close_poll(now); finish();
    cases++;
}

static void cleanup_fault(uint8_t nested)
{
    case_name = "retirement-fault-retains-owner";
    begin(nested, 100, 1, 256, 100000);
    prepare(); to_ack(); pump(1, 1);
    now = tx.engine.stop_at;
    pump(1, 0);
    CHECK(tx.engine.phase == MAC_TX_FAULT);
    if (nested) {
        CHECK(join.phase == MAC_JOIN_FAULT);
        CHECK(mac_join_release(&join, &tx) == MAC_JOIN_STATE);
    } else {
        close_poll(now);
        CHECK(poll.control.phase == MAC_POLL_FAULT);
        CHECK(mac_poll_release(&poll, &tx) == MAC_POLL_STATE);
    }
    cases++;
}

static void data_delivery(uint8_t empty)
{
    static const uint8_t data[] = {
        0x61,0xcc,0x91,0x34,0x12,1,2,3,4,5,6,7,8,
        17,18,19,20,21,22,23,24,0xa5
    };
    case_name = empty ? "empty-DATA" : "DATA-at-inclusive-boundary";
    opened(0, 100, 1);
    memcpy(frame, data, sizeof(data));
    frame_event(a + 300u, 1, 1, (uint16_t)(sizeof(data) - empty));
    tick(&event);
    CHECK(poll.record.protocol == (empty ? MAC_POLL_NO_DATA : MAC_POLL_SUCCESS));
    CHECK(poll.record.cause == (empty ? MAC_POLL_EMPTY : MAC_POLL_DATA));
    CHECK(poll.record.payload_length == !empty);
    close_poll(now); finish();
    cases++;
}

static void arm_handshake(uint8_t ending)
{
    mac_tx_interval_t saved;
    uint16_t token;
    case_name = "ARM-correlation-cancel-lifetime-failure";
    begin(1, 100, 511, 256, 100000);
    input(MAC_JOIN_PREPARED, 0);
    tick(&event);
    CHECK(action.kind == MAC_JOIN_ACTION_ARM && tx.engine.phase == MAC_TX_DRAW);
    token = action.token;
    saved = tx;
    input(MAC_JOIN_ARMED, 0); event.token++;
    tick(&event);
    CHECK(!memcmp(&tx, &saved, sizeof(tx)) && !join.armed);
    input(MAC_JOIN_ARMED, 0); event.token = token; event.generation++;
    tick(&event);
    CHECK(!memcmp(&tx, &saved, sizeof(tx)) && !join.armed);
    input(MAC_JOIN_ARMED, 0); event.token = token; event.epoch++;
    tick(&event);
    CHECK(!memcmp(&tx, &saved, sizeof(tx)) && !join.armed);
    if (!ending) {
        input(MAC_JOIN_ARMED, 0); event.token = token;
        tick(&event);
        CHECK(join.armed && tx.engine.phase == MAC_TX_DRAW_WAIT &&
              action.radio.control.kind == MAC_TX_ACTION_RANDOM);
        input(MAC_JOIN_CANCEL, 0);
    } else if (ending == 1) input(MAC_JOIN_CANCEL, 0);
    else if (ending == 2) {
        now = join.deadline;
        input(MAC_JOIN_ARMED, 0); event.token = token;
    } else input(MAC_JOIN_FAILURE, 0);
    tick(&event);
    CHECK(tx.engine.phase == MAC_TX_DONE && !tx.engine.transmissions &&
          action.kind == MAC_JOIN_ACTION_DISARM);
    CHECK(mac_join_release(&join, &tx) == MAC_JOIN_STATE);
    token = action.token;
    input(MAC_JOIN_DISARMED, 0); event.token++;
    tick(&event);
    CHECK(tx.engine.phase == MAC_TX_DONE);
    input(MAC_JOIN_DISARMED, 0); event.token = token;
    tick(&event);
    CHECK(join.phase == MAC_JOIN_RESTORE);
    finish();
    CHECK(record.reason == (ending == 2 ? MAC_JOIN_LIFETIME :
          ending == 3 ? MAC_JOIN_ADAPTER_ERROR : MAC_JOIN_CANCELLED));
    cases++;
}

static void tx_result_closed(uint8_t nested, uint8_t future)
{
    uint32_t through = 0;
    unsigned guard = 0;
    case_name = "NO_ACK-pre-stop-watermark-not-later-report";
    begin(nested, UINT32_C(0xffffffc0), 511, 256, 100000);
    if (nested) request_success();
    else prepare();
    while (tx.engine.phase != MAC_TX_DONE) {
        CHECK(++guard < 40);
        if (tx.engine.phase == MAC_TX_STOPPING) now += 7u;
        pump(1, 2);
        if (event.source.source.kind == MAC_TX_EVENT_RX_CLOSED)
            through = MAC_LINK_FLOOR(event.source.upper);
    }
    CHECK(consumer()->record.protocol == MAC_POLL_NO_ACK &&
          consumer()->record.cause == MAC_POLL_TX_RESULT &&
          consumer()->record.stamp == now && through != now);
    close_poll(future ? now+1u : through);
    if (future) {
        CHECK(!consumer()->control.closed && consumer()->control.reason == MAC_POLL_ADAPTER_ERROR);
        close_poll(now);
        CHECK(poll.control.phase == MAC_POLL_FAULT && mac_poll_release(&poll, &tx) == MAC_POLL_STATE);
    } else {
        CHECK(consumer()->control.closed && !consumer()->control.reason && !consumer()->control.cleanup_error);
        finish();
    }
    cases++;
}

static void decision_requires_coverage(uint8_t pending)
{
    case_name = "FRAME-or-PENDING_ZERO-still-needs-coverage";
    if (pending) {
        begin(0, 100, 1, 256, 100000);
        prepare(); poll_ack(0);
    } else {
        opened(0, 100, 1);
        frame_event(a+300u, 1, 1, sizeof(response));
        tick(&event);
    }
    CHECK(poll.record.protocol && poll.record.cause != MAC_POLL_TX_RESULT);
    close_poll(poll.record.stamp-1u);
    CHECK(!poll.control.closed && poll.control.reason == MAC_POLL_ADAPTER_ERROR);
    close_poll(now);
    CHECK(poll.control.phase == MAC_POLL_FAULT && mac_poll_release(&poll, &tx) == MAC_POLL_STATE);
    cases++;
}

int main(void)
{
    static const uint16_t phases[] = {0, 1, 511};
    unsigned f, wrap, nested, k;
    tx_result_closed(0, 0); tx_result_closed(1, 0); tx_result_closed(0, 1);
    decision_requires_coverage(0); decision_requires_coverage(1);
    for (k = 0; k < 4; k++) arm_handshake((uint8_t)k);
    for (wrap = 0; wrap < 2; wrap++)
        for (f = 0; f < sizeof(phases) / sizeof(phases[0]); f++) {
            uint32_t start = wrap ? UINT32_C(0xffffffc0) : 100;
            successful(start, phases[f]);
            for (nested = 0; nested < 2; nested++) {
                pending_zero((uint8_t)nested, start, phases[f]);
                uncertain_frame((uint8_t)nested, 0, start, phases[f]);
                uncertain_ack((uint8_t)nested, 0, start, phases[f]);
            }
            uncertain_ack(1, 1, start, phases[f]);
            uncertain_frame(0, 1, start, phases[f]);
            timeout(0, start, phases[f]);
            timeout(1, start, phases[f]);
        }
    retry_request(0); retry_request(1);
    for (k = 0; k < 5; k++) {
        classification(0, (uint8_t)k);
        classification(1, (uint8_t)k);
        classification(2, (uint8_t)k);
    }
    stale_duplicate(); order_error();
    for (k = 0; k < 12; k++) ack_witness((uint8_t)k);
    response_spans(); fine_tick_frame();
    data_delivery(0); data_delivery(1);
    for (nested = 0; nested < 2; nested++) {
        cancel_active((uint8_t)nested);
        budget((uint8_t)nested, 0); budget((uint8_t)nested, 1);
        invalid_inputs((uint8_t)nested);
        cleanup_fault((uint8_t)nested);
    }
    printf("Interval POLL/join: %u cases, %u checks PASS.\n", cases, checks);
    return 0;
}
