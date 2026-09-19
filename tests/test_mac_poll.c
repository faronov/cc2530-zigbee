/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 * Synthetic source events, real codecs/transmitter/context. No radio mock.
 */
#include "mac_poll.h"
#include "mac_association.h"
#include "cc2530_mmio.h"
#include <stddef.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { failed((uint16_t)__LINE__); return; } } while (0)
static const MCU_CODE uint8_t response[] = {
    0x73, 0xcc, 0x91, 0x34, 0x12, 1,2,3,4,5,6,7,8,
    0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18, 2,0x78,0x56,0
};
static mac_poll_t poll;
static mac_tx_t tx;
static mac_poll_request_t request;
static mac_poll_event_t event;
static mac_poll_action_t action;
static mac_poll_record_t record;
static mac_tx_event_t source;
static mac_tx_action_t radio_action;
static mac_association_t association;
static mac_association_request_t association_request;
static mac_association_event_t association_event;
static mac_association_record_t association_record;
static mac_header_t header;
static uint8_t bytes[126], ack[3], length, scenario, mode, observation, i, attempt;
static uint16_t failure, grant;
static uint32_t now, origin, checksum;

static void failed(uint16_t line)
{
    if (!failure)
        failure = line;
}

static uint32_t hash(const void *memory, uint16_t size)
{
    const uint8_t * volatile p = memory;
    volatile uint32_t h = 0;
    volatile uint8_t byte;
    while (size--) {
        byte = *p++;
        h = (h * 33u) ^ byte;
    }
    return h;
}

static void tick(void)
{
    CHECK(mac_poll_step(&poll, &tx, now, NULL, &action) == MAC_POLL_OK);
}

static void input(uint8_t kind)
{
    event.kind = kind;
    event.epoch = request.epoch;
    event.generation = poll.control.generation;
    event.stamp = now;
    event.token = action.token;
}

static void send(void)
{
    CHECK(mac_poll_step(&poll, &tx, now, &event, &action) == MAC_POLL_OK);
}

static void setup(void)
{
    memset(&request, 0, sizeof(request));
    request.epoch = 0x10203040UL;
    request.frame_wait = 300; /* Synthetic caller PIB; no claimed default. */
    request.lifetime = 20000;
    request.work = 256;
    if (scenario == 23) request.work = 7;
    request.pan = 0x1234;
    request.channel = (uint8_t)(11u + scenario % 16u);
    request.local_mode = request.coordinator_mode = MAC_ADDRESS_EXTENDED;
    memcpy(request.local, response + 5, 8);
    memcpy(request.coordinator, response + 13, 8);
    if (scenario == 3 || scenario == 36 || scenario == 38) {
        request.coordinator_mode = MAC_ADDRESS_SHORT;
        memset(request.coordinator, 0, 8);
        request.coordinator[0] = 0x44; request.coordinator[1] = 0x33;
    }
    if (scenario == 36 || scenario == 37) {
        request.local_mode = MAC_ADDRESS_SHORT;
        memset(request.local, 0, 8);
        request.local[0] = 0x66; request.local[1] = 0x55;
    }
    memset(&event, 0, sizeof(event));
    memset(&source, 0, sizeof(source));
    memset(&record, 0xc7, sizeof(record));
    now = scenario == 35 ? 0xffffff80UL : 100;
    origin = now;
    CHECK(mac_tx_init(&tx, 0xff, now) == MAC_TX_OK);
    CHECK(mac_poll_init(&poll) == MAC_POLL_OK);
    CHECK(mac_poll_start(&poll, &tx, &request, now) == MAC_POLL_OK);
    CHECK(tx.next_dsn == 0xff && tx.generation == 0);
    memset(&association_request, 0, sizeof(association_request));
    association_request.epoch = request.epoch;
    association_request.lifetime = 18000;
    association_request.work_limit = 8;
    association_request.pan_id = request.pan;
    association_request.channel = request.channel;
    association_request.coordinator_mode = request.coordinator_mode;
    memcpy(association_request.coordinator, request.coordinator, 8);
    memcpy(association_request.local, response + 5, 8);
    CHECK(mac_association_init(&association, now) == MAC_ASSOCIATION_OK);
    CHECK(mac_association_start(&association, &association_request, now) == MAC_ASSOCIATION_OK);
    checksum = hash(&poll, sizeof(poll));
    CHECK(mac_poll_take(&poll, &record) == MAC_POLL_STATE);
    CHECK(mac_poll_start(&poll, &tx, &request, now) == MAC_POLL_STATE);
    CHECK(hash(&poll, sizeof(poll)) == checksum);
}

static void prepare(void)
{
    tick();
    CHECK(action.kind == MAC_POLL_ACTION_PREPARE);
    input(MAC_POLL_PREPARED);
    send();
    CHECK(tx.generation == 1 && tx.next_dsn == 0 && tx.frame[2] == 0xff);
    CHECK(tx.frame[tx.length - 1] == 4 && tx.phase == MAC_TX_DRAW);
    CHECK(mac_tx_copy(&tx, bytes, sizeof(bytes), &length) == MAC_TX_OK);
    CHECK(length == poll.control.outgoing_length && bytes[2] == 0xff);
}

static void sent(void)
{
    source.kind = MAC_TX_EVENT_SENT;
    now = tx.at + 24u + 2u * tx.length;
}

/* Exactly one real step per grant, with the exact original source witness.
 * mode1 wrong DSN then success; mode2 no ACK; mode3 busy; mode4 failure.
 */
static void pump(void)
{
    CHECK(poll.control.tx_issued && action.kind == MAC_POLL_ACTION_TX);
    grant = action.token;
    memset(&source, 0, sizeof(source));
    source.generation = tx.generation; source.retry = tx.retries; source.nb = tx.nb;
    if (action.tx_cancel)
        source.kind = MAC_TX_EVENT_CANCEL;
    else if (tx.phase == MAC_TX_DRAW_WAIT) {
        source.kind = MAC_TX_EVENT_RANDOM;
        source.value = 0;
    } else if (tx.phase == MAC_TX_RADIO) {
        if (mode == 4)
            source.kind = MAC_TX_EVENT_FAILURE;
        else if (mode == 3) {
            source.kind = MAC_TX_EVENT_BUSY;
            now = tx.at + 8;
        } else
            sent();
    } else if (tx.phase == MAC_TX_ACK_WAIT) {
        if (mode == 2)
            now = tx.tx_end + MAC_TX_ACK_SYMBOLS;
        else {
            source.kind = MAC_TX_EVENT_ACK;
            now = tx.tx_end + 34;
            ack[0] = scenario == 5 || scenario == 27 ? 0xeau : 0xfau;
            ack[1] = 0xff; /* Ignored on ACK reception, actual codec normalization. */
            ack[2] = tx.frame[2];
            if (mode == 1 && !tx.retries) ack[2]++;
            source.bytes = ack; source.length = 3;
        }
    } else if (tx.phase == MAC_TX_STOPPING) {
        source.kind = MAC_TX_EVENT_QUIESCED;
        if (tx.retry_pending)
            now = tx.tx_end + MAC_TX_ACK_SYMBOLS;
        /* Old operation truly retired; independent prepared RX lease persists. */
    }
    source.stamp = now;
    input(MAC_POLL_TX);
    event.token = grant;
    event.crc_valid = 1;
    event.source = source;
    event.tx_result = (uint8_t)mac_tx_step(&tx, now, source.kind ? &source : NULL, &radio_action);
    if ((scenario == 44 || scenario == 45) && tx.outcome == MAC_TX_ACKED
            && poll.control.tx_phase == MAC_TX_ACK_WAIT) {
        if (scenario == 44) event.source.stamp++;
        else event.crc_valid = 0;
    }
    send();
}

static void request_ack(void)
{
    prepare();
    attempt = 0;
    while (poll.control.phase == MAC_POLL_REQUEST && !failure && attempt++ < 100)
        pump();
    CHECK(attempt < 100);
}

static void make_data(uint8_t payload_size)
{
    memset(&header, 0, sizeof(header));
    header.type = MAC_FRAME_DATA;
    header.flags = MAC_FLAG_PAN_COMPRESSION | MAC_FLAG_ACK_REQUEST | MAC_FLAG_PENDING;
    header.source_pan = header.destination_pan = request.pan;
    header.source_mode = request.coordinator_mode;
    header.destination_mode = request.local_mode;
    header.sequence = 0x42; /* NOT Request DSN. */
    memcpy(header.source, request.coordinator, 8);
    memcpy(header.destination, request.local, 8);
    /* Header is disjoint; tiny payload is in separate storage. */
    memset(ack, 0x9a, sizeof(ack));
    CHECK(mac_frame_encode(&header, ack, payload_size, bytes, sizeof(bytes), &length) == MAC_CODEC_OK);
}

static void frame_event(void)
{
    input(MAC_POLL_FRAME);
    event.serial = poll.control.rx_serial + 1;
    event.channel = request.channel;
    event.body = bytes; event.length = length; event.crc_valid = 1;
}

static void take(void)
{
    CHECK(mac_poll_take(&poll, &record) == MAC_POLL_OK);
    checksum = hash(&record, sizeof(record));
    CHECK(mac_poll_take(&poll, &record) == MAC_POLL_STATE);
    CHECK(hash(&record, sizeof(record)) == checksum);
}

static void close_poll(void)
{
    attempt = 0;
    while (poll.control.phase < MAC_POLL_DONE && !failure && attempt++ < 80) {
        if (poll.control.tx_issued) {
            action.kind = MAC_POLL_ACTION_TX; action.token = poll.control.tx_token;
            action.tx_cancel = poll.control.phase == MAC_POLL_DRAIN && tx.phase != MAC_TX_STOPPING;
            pump();
        } else if (action.kind == MAC_POLL_ACTION_CLOSE) {
            /* Synthetic confirmed ACK service, local IFS, complete drainage and
             * baseline RX handoff, not a fake hardware implementation. */
            now += 80;
            input(MAC_POLL_CLOSED);
            event.through = now;
            if (scenario == 26) event.through = poll.control.receive_end - 1;
            send();
            if (scenario == 26) break;
        } else
            tick();
    }
    CHECK(attempt < 80);
}

static void forward_response(void)
{
    memset(&association_event, 0, sizeof(association_event));
    association_event.kind = MAC_ASSOCIATION_FRAME;
    association_event.epoch = record.epoch;
    association_event.generation = association.generation;
    association_event.stamp = record.stamp;
    association_event.channel = request.channel;
    association_event.crc_valid = 1;
    association_event.body = record.body;
    association_event.length = record.length;
    CHECK(mac_association_step(&association, now, &association_event, &observation) == MAC_ASSOCIATION_OK);
    CHECK(observation == (scenario == 4 ? MAC_ASSOCIATION_EXPIRED : MAC_ASSOCIATION_RESPONSE));
    CHECK(mac_association_take(&association, &association_record) == MAC_ASSOCIATION_OK);
    if (scenario != 4) {
        CHECK(association_record.short_address == 0x5678);
        CHECK(association_record.source_relation == (scenario == 3
              ? MAC_ASSOCIATION_SOURCE_UNBOUND : MAC_ASSOCIATION_SOURCE_MATCHED));
        CHECK(record.source_relation == association_record.source_relation);
        CHECK(association_record.stamp == record.stamp && record.body[0] == 0x73);
    }
}

static void interrupted(void)
{
    prepare();
    while (tx.phase != MAC_TX_ACK_WAIT && !failure) pump();
    grant = action.token;
    memset(&source, 0, sizeof(source));
    source.kind = MAC_TX_EVENT_ACK;
    source.generation = tx.generation; source.retry = tx.retries; source.nb = tx.nb;
    now = source.stamp = tx.tx_end + 34;
    ack[0] = 0x12; ack[1] = 0; ack[2] = tx.frame[2];
    source.bytes = ack; source.length = 3;
    CHECK(mac_tx_step(&tx, now, &source, &radio_action) == MAC_TX_OK);
    CHECK(tx.outcome == MAC_TX_ACKED);
    event.source = source; event.crc_valid = 1; event.tx_result = MAC_TX_OK;
    if (scenario == 47) now++; /* Deliberate ordered-watermark violation. */
    input(MAC_POLL_CANCEL); send();
    event.kind = MAC_POLL_TX; event.token = grant; event.stamp = source.stamp;
    send(); /* Deliver the completed pump ONCE, never retimestamp/repeat it. */
    CHECK(poll.control.reason == MAC_POLL_CANCELLED && !poll.record.protocol);
    if (scenario == 47) {
        CHECK(poll.control.tx_issued);
        now = poll.control.stop_at; tick();
        CHECK(poll.control.phase == MAC_POLL_FAULT);
    }
}

static void corpus(void)
{
    CHECK(mac_poll_init(NULL) == MAC_POLL_INVALID);
    for (scenario = 0; scenario < 52 && !failure; scenario++) {
        setup();
        mode = scenario == 27 ? 1 : scenario == 28 ? 2 : scenario == 29 ? 3
             : scenario == 33 ? 4 : 0;
        if (scenario == 46 || scenario == 47)
            interrupted();
        else if (scenario == 30 || scenario == 31) {
            if (scenario == 31) tick();
            input(MAC_POLL_CANCEL); send();
        } else if (scenario == 32) {
            prepare();
            input(MAC_POLL_CANCEL); send();
        } else {
            request_ack();
            if (scenario == 5 || scenario == 27 || scenario == 28 || scenario == 29) {
                CHECK(poll.control.ready);
                CHECK(poll.record.protocol == (scenario == 28 ? MAC_POLL_NO_ACK
                      : scenario == 29 ? MAC_POLL_CHANNEL_ACCESS : MAC_POLL_NO_DATA));
                CHECK(tx.retries == (scenario == 27 ? 1 : scenario == 28 ? 3 : 0));
            } else if (scenario != 33 && scenario != 44 && scenario != 45) {
                CHECK(poll.control.phase == MAC_POLL_RECEIVE && tx.phase == MAC_TX_STOPPING);
                CHECK(poll.control.ack_end == event.source.stamp);
                /* Retire TX promptly while the independently prepared RX stays
                 * continuous, except explicit in-STOPPING delivery case0. */
                if (scenario != 0) pump();
                CHECK(scenario == 0 || tx.phase == MAC_TX_DONE);
                make_data(scenario == 1 || scenario == 41 ? 0 : 2);
                now = poll.control.ack_end + 100;
                if (scenario == 2 || scenario == 3 || scenario == 4 || scenario == 40) {
                    memcpy(bytes, response, sizeof(response)); length = sizeof(response);
                }
                if (scenario == 4 || scenario == 7) now = poll.control.receive_end;
                if (scenario == 4) {
                    /* An independently chosen half-open metadata context ends
                     * exactly at D; poll must still retain/forward its command. */
                    CHECK(mac_association_init(&association, origin) == MAC_ASSOCIATION_OK);
                    association_request.lifetime = now - origin;
                    CHECK(mac_association_start(&association, &association_request, origin) == MAC_ASSOCIATION_OK);
                }
                if (scenario == 39) {
                    memset(bytes + length, 0x6b, 125u - length);
                    length = 125;
                }
                frame_event();
                if (scenario == 2) event.body = response; /* Real generic CODE decode. */
                switch (scenario) {
                case 6: case 26: case 42:
                    now = poll.control.receive_end; tick();
                    CHECK(!poll.control.ready && poll.control.timeout_pending);
                    if (scenario == 42) { input(MAC_POLL_FAILURE); send(); }
                    break;
                case 8:
                    now = poll.control.receive_end + 1; event.stamp = now;
                    send(); CHECK(action.observation == MAC_POLL_OBS_LATE && !poll.control.ready); break;
                case 9: case 15:
                    event.crc_valid = 0; send();
                    CHECK(action.observation == MAC_POLL_OBS_BAD_CRC);
                    if (scenario == 15) { send(); CHECK(action.observation == MAC_POLL_OBS_DUPLICATE); }
                    now++; frame_event(); send(); break;
                case 10:
                    event.length = 2; send();
                    CHECK(action.observation == MAC_POLL_OBS_MALFORMED);
                    now++; frame_event(); send(); break;
                case 11: case 12: case 13: case 14:
                    i = scenario == 11 ? 3 : scenario == 12 ? 13 : 5;
                    if (scenario == 14) event.channel = request.channel == 26 ? 11 : 26;
                    else bytes[i] ^= 1;
                    send(); CHECK(action.observation == MAC_POLL_OBS_FOREIGN);
                    if (scenario != 14) bytes[i] ^= 1;
                    now++; frame_event(); send(); break;
                case 16: case 17:
                    if (scenario == 16) event.epoch--;
                    else event.generation--;
                    send(); CHECK(action.observation == MAC_POLL_OBS_STALE);
                    now++; frame_event(); send(); break;
                case 18:
                    tick(); event.stamp = now - 1; send();
                    CHECK(poll.control.reason == MAC_POLL_ORDER_ERROR); break;
                case 19:
                    bytes[0] |= MAC_FLAG_SECURITY; send();
                    CHECK(action.observation == MAC_POLL_OBS_UNSUPPORTED); break;
                case 20:
                    memcpy(bytes, response, sizeof(response)); length = sizeof(response);
                    bytes[21] = 6; frame_event(); send();
                    CHECK(action.observation == MAC_POLL_OBS_UNSUPPORTED); break;
                case 21:
                    input(MAC_POLL_CANCEL); send(); break;
                case 22:
                    now = poll.control.deadline; tick(); break;
                case 23:
                    CHECK(!poll.control.steps); tick(); break; /* Real step consumption. */
                case 24:
                    input(MAC_POLL_FAILURE); send(); break;
                case 34:
                    now = poll.control.last - 1; tick(); break;
                case 43:
                    /* Already consumed pump token cannot replay an ACK. */
                    input(MAC_POLL_TX); event.token = 2; send();
                    now++; frame_event(); send(); break;
                case 48:
                    now = poll.control.receive_end; tick();
                    frame_event(); send();
                    CHECK(poll.control.reason == MAC_POLL_ORDER_ERROR); break;
                case 50:
                    tx.generation++; tick(); break;
                default: send(); break;
                }
                if (poll.record.length) {
                    CHECK(poll.control.ready && poll.control.phase == MAC_POLL_DRAIN);
                    CHECK(poll.record.protocol == (scenario == 1 || scenario == 2 || scenario == 3
                          || scenario == 4 || scenario == 40 || scenario == 41
                          ? MAC_POLL_NO_DATA : MAC_POLL_SUCCESS));
                    take();
                    CHECK(record.length == length && !memcmp(record.body, bytes, length));
                    for (i = length; i < 125; i++) CHECK(record.body[i] == 0);
                    memset(bytes, 0, sizeof(bytes));
                    CHECK(hash(&record, sizeof(record)) == checksum);
                    if (record.cause == MAC_POLL_COMMAND) forward_response();
                    if (scenario == 25 || scenario == 49) {
                        if (scenario == 49) {
                            input(MAC_POLL_CLOSED); event.token--; event.through = now; send();
                            CHECK(!poll.control.closed && poll.control.phase == MAC_POLL_DRAIN);
                        }
                        now = poll.control.stop_at; tick();
                        CHECK(poll.control.phase == MAC_POLL_FAULT && record.protocol == MAC_POLL_SUCCESS);
                    }
                }
            }
        }
        if (poll.control.phase < MAC_POLL_DONE) close_poll();
        if (scenario == 26) { now = poll.control.stop_at; tick(); }
        CHECK(poll.control.phase == MAC_POLL_DONE || poll.control.phase == MAC_POLL_FAULT);
        if (scenario == 51) {
            checksum = hash(&poll, sizeof(poll));
            now++; frame_event(); send();
            CHECK(hash(&poll, sizeof(poll)) == checksum);
        }
        if (!poll.control.taken) take();
        if (poll.control.reason)
            CHECK(record.protocol == MAC_POLL_NO_CONFIRM || record.length);
        checksum = hash(&poll, sizeof(poll));
        if (poll.control.phase == MAC_POLL_FAULT) {
            CHECK(mac_poll_release(&poll, &tx) == MAC_POLL_STATE);
            CHECK(hash(&poll, sizeof(poll)) == checksum);
        } else {
            CHECK(tx.next_dsn == (poll.control.submitted ? 0 : 0xff));
            CHECK(mac_poll_release(&poll, &tx) == MAC_POLL_OK);
            CHECK(tx.phase == MAC_TX_IDLE && poll.control.phase == MAC_POLL_IDLE);
            if (scenario == 36) {
                CHECK(mac_poll_start(&poll, &tx, &request, now) == MAC_POLL_OK);
                tick(); input(MAC_POLL_PREPARED); send();
                CHECK(tx.next_dsn == 1 && tx.generation == 2 && tx.frame[2] == 0);
                input(MAC_POLL_CANCEL); send(); close_poll(); take();
                CHECK(mac_poll_release(&poll, &tx) == MAC_POLL_OK);
            }
        }
    }
}

#ifdef CC2530_HOST_TEST
#include <stdio.h>
#include <stdlib.h>
static void native(void)
{
    mac_poll_t saved, other;
    mac_poll_record_t old;
    mac_poll_action_t before;
    mac_tx_t saved_tx;
    unsigned n;
    uint8_t *exact;
    scenario = 0;
    for (n = 0; n <= 126 && !failure; n++) {
        setup(); mode = 0; request_ack(); pump(); make_data(2);
        exact = malloc(n ? n : 1); CHECK(exact != NULL);
        memset(exact, 0x6b, n); memcpy(exact, bytes, n < length ? n : length);
        now++; frame_event(); event.body = exact; event.length = (uint16_t)n;
        send(); free(exact);
        CHECK((poll.record.protocol == MAC_POLL_SUCCESS) == (n > 21 && n <= 125));
    }
    setup(); saved = poll; saved_tx = tx; before = action; old = record;
    CHECK(mac_poll_start(NULL, &tx, &request, now) == MAC_POLL_INVALID);
    CHECK(mac_poll_step(&poll, &tx, now, NULL, NULL) == MAC_POLL_INVALID);
    event.kind = 0;
    CHECK(mac_poll_step(&poll, &tx, now, &event, &action) == MAC_POLL_INVALID);
    CHECK(!memcmp(&poll, &saved, sizeof(poll)) && !memcmp(&tx, &saved_tx, sizeof(tx)));
    CHECK(!memcmp(&action, &before, sizeof(action)) && !memcmp(&record, &old, sizeof(record)));
    CHECK(mac_poll_take(NULL, &record) == MAC_POLL_INVALID);
    CHECK(mac_poll_release(NULL, &tx) == MAC_POLL_INVALID);
    CHECK(mac_poll_init(&poll) == MAC_POLL_OK);
    for (n = 0; n < 256; n++) {
        request.channel = (uint8_t)n; saved = poll;
        if (n < 11 || n > 26) {
            CHECK(mac_poll_start(&poll, &tx, &request, now) == MAC_POLL_INVALID);
            CHECK(!memcmp(&poll, &saved, sizeof(poll)));
        }
    }
    request.channel = 11;
    poll.control.generation = UINT32_MAX; saved = poll;
    CHECK(mac_poll_start(&poll, &tx, &request, now) == MAC_POLL_LIMIT);
    CHECK(!memcmp(&poll, &saved, sizeof(poll)));

    /* Rejected calls on another context must not leak staged state or inputs.
     * Inactive receipt bytes are independent of the staged control prefix. */
    setup();
    for (n = 0; n < sizeof(poll.record.body); n++)
        poll.record.body[n] = (uint8_t)(n ^ 0xa5u);
    old = poll.record;
    tick();
    saved = poll; saved_tx = tx; before = action;
    CHECK(mac_poll_init(&other) == MAC_POLL_OK);
    input(MAC_POLL_CANCEL);
    CHECK(mac_poll_step(&other, &tx, now, &event, &action) == MAC_POLL_STATE);
    CHECK(!memcmp(&action, &before, sizeof(action)));
    request.channel = 0;
    CHECK(mac_poll_start(&other, &tx, &request, now) == MAC_POLL_INVALID);
    request.channel = 11;
    tick();
    saved.control.steps--;
    CHECK(!memcmp(&poll, &saved, sizeof(poll)));
    CHECK(!memcmp(&poll.record, &old, sizeof(old)));
    CHECK(!memcmp(&tx, &saved_tx, sizeof(tx)));

    setup(); mode = 0; request_ack(); pump(); make_data(2);
    now++; frame_event(); send(); take();
    saved = poll; saved_tx = tx; old = record; before = action;
    CHECK(mac_poll_take(&other, &record) == MAC_POLL_STATE);
    CHECK(mac_poll_step(&other, &tx, now, NULL, &action) == MAC_POLL_STATE);
    CHECK(!memcmp(&action, &before, sizeof(action)));
    tick();
    saved.control.stop_steps--;
    CHECK(!memcmp(&poll, &saved, sizeof(poll)));
    CHECK(!memcmp(&record, &old, sizeof(record)));
    CHECK(!memcmp(&tx, &saved_tx, sizeof(tx)));
}
int main(void)
{
    corpus();
    if (!failure) native();
    if (failure) { fprintf(stderr, "mac_poll scenario %u failed line %u\n", scenario, failure); return 1; }
    puts("MAC poll: 52 shared synthetic scenarios + native exact/atomic cases PASS");
    return 0;
}
#else
volatile __xdata __at(0x1e00) uint8_t mac_poll_result[8];
void main(void)
{
    corpus();
    mac_poll_result[0] = 'P'; mac_poll_result[1] = 'O';
    mac_poll_result[2] = 'L'; mac_poll_result[3] = '1';
    mac_poll_result[4] = 1; mac_poll_result[5] = 8;
    mac_poll_result[6] = (uint8_t)failure;
    mac_poll_result[7] = (uint8_t)(failure >> 8);
    __asm
        .globl _mac_poll_done
    _mac_poll_done:
        nop
    __endasm;
    for (;;) {}
}
#endif
