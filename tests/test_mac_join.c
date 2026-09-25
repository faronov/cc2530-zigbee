/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 * Synthetic adapter facts; all protocol/state operations are genuine.
 */
#include "mac_join.h"
#include "cc2530_mmio.h"
#include <string.h>
#if defined(CC2530_HOST_TEST) && defined(CC2530_JOIN_WORKSPACE)
/* Real host allocation for the same full external shadow. This is not the
 * target BDB aggregate, a linker binding or a full-profile MCU fit proof. */
mac_join_t mac_join_staged;
#endif
#ifndef MAC_JOIN_CASE
#define MAC_JOIN_CASE 0
#endif
#ifdef __SDCC
#define SC MAC_JOIN_CASE
#if MAC_JOIN_CASE == 5 || MAC_JOIN_CASE == 6 || MAC_JOIN_CASE == 7 || MAC_JOIN_CASE == 8 || MAC_JOIN_CASE == 10 || MAC_JOIN_CASE == 16 || MAC_JOIN_CASE == 18 || MAC_JOIN_CASE == 19 || MAC_JOIN_CASE == 21
#define REQUEST_ONLY 1
#else
#define REQUEST_ONLY 0
#endif
#define F_VALUE (MAC_JOIN_CASE == 15 ? 65534UL : 300UL)
#define LIFE_VALUE (MAC_JOIN_CASE == 7 ? 1920UL : MAC_JOIN_CASE == 15 ? 200000UL : 100000UL)
#define WORK_VALUE (MAC_JOIN_CASE == 19 ? 1 : 256)
#define WAIT_VALUE (MAC_JOIN_CASE == 15 ? 64 : 2)
#define PROFILE_VALUE (MAC_JOIN_CASE == 4 ? 1 : 0)
#define MODE_VALUE (MAC_JOIN_CASE == 4 ? 2 : 3)
#else
static uint8_t scenario;
#define SC scenario
#define F_VALUE 300UL
#define LIFE_VALUE 100000UL
#define WORK_VALUE 256
#define WAIT_VALUE 2
#define PROFILE_VALUE 0
#define MODE_VALUE 3
#define REQUEST_ONLY 0
#endif

static const MCU_CODE mac_join_request_t request = {
    {1, F_VALUE, LIFE_VALUE, WORK_VALUE, 0x1234, 15, 3, MODE_VALUE,
     {1,2,3,4,5,6,7,8},
#if defined(__SDCC) && MAC_JOIN_CASE == 4
     {0x44,0x33,0,0,0,0,0,0}},
#else
     {17,18,19,20,21,22,23,24}},
#endif
    {0xffff, 11, 0, 0}, WAIT_VALUE, 0x88, PROFILE_VALUE
};
static const MCU_CODE uint8_t response[] = {
#if defined(__SDCC) && MAC_JOIN_CASE == 4
    0x33,0xcc,0x91,0xff,0xff,1,2,3,4,5,6,7,8,0x34,0x12,17,18,19,20,21,22,23,24,2,0x78,0x56,0
#elif defined(__SDCC) && (MAC_JOIN_CASE == 1 || MAC_JOIN_CASE == 2)
    0x73,0xcc,0x91,0x34,0x12,1,2,3,4,5,6,7,8,17,18,19,20,21,22,23,24,2,0xff,0xff,MAC_JOIN_CASE
#elif defined(__SDCC) && MAC_JOIN_CASE == 3
    0x73,0xcc,0x91,0x34,0x12,1,2,3,4,5,6,7,8,17,18,19,20,21,22,23,24,2,0xfe,0xff,0
#else
    0x73,0xcc,0x91,0x34,0x12,1,2,3,4,5,6,7,8,17,18,19,20,21,22,23,24,2,0x78,0x56,0
#endif
};
#ifndef __SDCC
static const MCU_CODE uint8_t r22[] = {
    0x33,0xcc,0x91,0xff,0xff,1,2,3,4,5,6,7,8,0x34,0x12,17,18,19,20,21,22,23,24,2,0x78,0x56,0
};
static uint8_t frame[27], size;
#endif
static const MCU_CODE uint8_t kinds[] = {0, 0, 1, 3, 4, 5, 0, 0};
static mac_join_t join;
static mac_join_request_t config;
static mac_tx_t tx;
static mac_join_event_t event;
static mac_join_action_t action;
static mac_tx_action_t radio;
static mac_join_record_t record;
static uint8_t ack[3], source_kind, frames;
static volatile uint32_t now, boundary;
static volatile uint16_t duration;
static const mac_tx_event_t * volatile supplied;
static volatile uint8_t operation, phase, transmitter;
static volatile uint8_t calls;
static uint8_t failure, release_result;
static uint16_t grant;
static uint8_t seen;
#ifndef __SDCC
static uint8_t interleaving;
static unsigned interleaved;
static int other_attempt(void);
#endif

#ifdef __SDCC
volatile MCU_XDATA MCU_AT(0x1e00) uint8_t mac_join_result[8];
void main(void)
#else
static void run(void)
#endif
{
    config = request;
#ifndef __SDCC
    size = SC == 4 ? sizeof(r22) : sizeof(response);
    memcpy(frame, SC == 4 ? r22 : response, size);
#if !defined(__SDCC) || (MAC_JOIN_CASE >= 1 && MAC_JOIN_CASE <= 3)
    if (SC == 1 || SC == 2 || SC == 3) {
        frame[size - 3] = SC == 3 ? 0xfe : 0xff;
        frame[size - 2] = 0xff;
        frame[size - 1] = SC == 3 ? 0 : SC;
    }
#endif
#if !defined(__SDCC) || MAC_JOIN_CASE == 4
    if (SC == 4) {
        config.profile = MAC_RX_R22_ASSOCIATION_RESPONSE;
        config.extraction.coordinator_mode = MAC_ADDRESS_SHORT;
        memset(config.extraction.coordinator, 0, 8);
        config.extraction.coordinator[0] = 0x44; config.extraction.coordinator[1] = 0x33;
    }
#endif
#if !defined(__SDCC) || MAC_JOIN_CASE == 7
    if (SC == 7) config.extraction.lifetime = 1920;
#endif
#if !defined(__SDCC) || MAC_JOIN_CASE == 15
    if (SC == 15) {
        config.response_wait = 64;
        config.extraction.frame_wait = 65534;
        config.extraction.lifetime = 200000;
    }
#endif
#if !defined(__SDCC) || MAC_JOIN_CASE == 19
    if (SC == 19) config.extraction.work = 1;
#endif
#endif
    now = SC == 4 ? 0xfffff800UL : 100;
    grant = seen = frames = 0;
    failure = 1;
    if (mac_tx_init(&tx, 0xff, now) != MAC_TX_OK
            || mac_join_init(&join, now) != MAC_JOIN_OK
            || mac_join_start(&join, &tx, &config, now) != MAC_JOIN_OK)
        goto complete;
    if (mac_join_step(&join, &tx, now, NULL, &action) != MAC_JOIN_OK)
        goto complete;
    for (calls = 0; calls < 100 && join.phase < MAC_JOIN_DONE; calls++) {
#ifndef __SDCC
        if (interleaving && other_attempt()) {
            failure = 3;
            goto complete;
        }
#endif
        operation = action.kind;
        phase = join.phase;
        transmitter = tx.phase;
        if (operation && operation != MAC_JOIN_ACTION_RADIO) grant = action.token;
        memset(&event, 0, sizeof(event));
        event.epoch = 1;
        event.generation = 1;
        event.token = grant;
        if (operation == MAC_JOIN_ACTION_PREPARE || operation == MAC_JOIN_ACTION_RECEIVE
                || phase == MAC_JOIN_PREPARE) {
            event.kind = MAC_JOIN_PREPARED;
#if REQUEST_ONLY
        } else if (operation == MAC_JOIN_ACTION_RADIO || phase == MAC_JOIN_REQUEST) {
            event.kind = MAC_JOIN_SOURCE;
#else
        } else if (operation == MAC_JOIN_ACTION_RADIO || operation == MAC_JOIN_ACTION_TX
                || phase == MAC_JOIN_REQUEST) {
            event.kind = operation == MAC_JOIN_ACTION_TX ? MAC_JOIN_TX : MAC_JOIN_SOURCE;
#endif
            source_kind = kinds[transmitter];
            if (transmitter == MAC_TX_RADIO) {
                duration = (uint16_t)(24u + 2u * tx.length);
#if !defined(__SDCC) || MAC_JOIN_CASE == 16
                if (SC == 16) {
                    source_kind = MAC_TX_EVENT_BUSY;
                    duration = 8;
                }
#endif
                now = tx.at + duration;
            }
            if (transmitter == MAC_TX_ACK_WAIT) {
                now = tx.tx_end + 34u;
            }
#if !defined(__SDCC) || MAC_JOIN_CASE == 8
            if (SC == 8 && phase == MAC_JOIN_REQUEST && transmitter == MAC_TX_STOPPING)
                now = tx.stop_at;
#endif
            event.source.kind = source_kind;
#if REQUEST_ONLY
            event.source.generation = 1;
#else
            event.source.generation = phase == MAC_JOIN_REQUEST ? 1 : 2;
#endif
#if defined(__SDCC) && MAC_JOIN_CASE != 5 && MAC_JOIN_CASE != 11
            event.source.retry = 0;
#else
            event.source.retry = tx.retries;
#endif
#if defined(__SDCC) && MAC_JOIN_CASE != 16
            event.source.nb = 0;
#else
            event.source.nb = tx.nb;
#endif
            event.source.stamp = now;
            ack[0] = 0x12; ack[1] = 0;
#if REQUEST_ONLY
            ack[2] = 0xff;
#else
            ack[2] = phase == MAC_JOIN_REQUEST ? 0xff : 0;
#endif
#if !defined(__SDCC) || MAC_JOIN_CASE == 14
            if (SC == 14 && phase == MAC_JOIN_EXTRACT) ack[0] = 2;
#endif
#if defined(__SDCC) && MAC_JOIN_CASE == 5
            if (phase == MAC_JOIN_REQUEST) ack[2] = 0;
#elif defined(__SDCC) && MAC_JOIN_CASE == 11
            if (phase == MAC_JOIN_EXTRACT) ack[2] = 1;
#elif !defined(__SDCC)
            if ((SC == 5 && phase == MAC_JOIN_REQUEST) || (SC == 11 && phase == MAC_JOIN_EXTRACT))
                ack[2]++;
#endif
            event.source.bytes = ack;
            event.source.length = 3;
            event.crc_valid = 1;
#if !REQUEST_ONLY
            if (event.kind == MAC_JOIN_TX) {
                supplied = source_kind ? &event.source : NULL;
                event.tx_result = mac_tx_step(&tx, now, supplied, &radio);
            }
#endif
            if (event.kind == MAC_JOIN_SOURCE && !source_kind) event.kind = 0;
#if !REQUEST_ONLY
        } else if (operation == MAC_JOIN_ACTION_CLOSE) {
            now += 100;
            event.kind = MAC_JOIN_CLOSED;
            event.through = now;
#if !defined(__SDCC) || MAC_JOIN_CASE == 20
            if (SC == 20) event.kind = MAC_JOIN_FAILURE;
#endif
#endif
        } else if (operation == MAC_JOIN_ACTION_RESTORE) {
#if defined(__SDCC) && MAC_JOIN_CASE == 9
            now = join.stop_at;
#else
            event.kind = MAC_JOIN_RESTORED;
            boundary = tx.ready_at;
            if ((uint32_t)(now - boundary) >= MAC_TX_HALF) now = boundary;
#if !defined(__SDCC) || MAC_JOIN_CASE == 9
            if (SC == 9) { now = join.stop_at; event.kind = 0; }
#endif
#endif
        } else if (phase == MAC_JOIN_WAIT) {
            now = join.wait_until;
#if !defined(__SDCC) || MAC_JOIN_CASE == 6 || MAC_JOIN_CASE == 21
            if (SC == 6 || SC == 21) event.kind = MAC_JOIN_CANCEL;
#endif
#if !defined(__SDCC) || MAC_JOIN_CASE == 10
            if (SC == 10) now = join.last - 1u;
#endif
        }
#if !defined(__SDCC) || MAC_JOIN_CASE == 20
        else if (SC == 20 && phase == MAC_JOIN_EXTRACT && join.poll.control.phase == MAC_POLL_DRAIN) {
            event.kind = MAC_JOIN_CLOSED;
            event.through = now;
        }
#endif
#if !REQUEST_ONLY && (!defined(__SDCC) || (MAC_JOIN_CASE != 11 && MAC_JOIN_CASE != 14))
        else if (phase == MAC_JOIN_EXTRACT && join.poll.control.phase == MAC_POLL_RECEIVE) {
#if defined(__SDCC) && MAC_JOIN_CASE == 17
            now = join.poll.control.receive_end;
#else
#if defined(__SDCC) && (MAC_JOIN_CASE == 4 || MAC_JOIN_CASE == 13 || MAC_JOIN_CASE == 15 || MAC_JOIN_CASE == 17)
            now = join.poll.control.receive_end;
#elif defined(__SDCC) && MAC_JOIN_CASE != 12
            now = join.poll.control.ack_end + 100u;
#elif defined(__SDCC)
            now += 100u;
#else
            duration = (uint16_t)(100u * (uint16_t)(frames + 1u));
            now = join.poll.control.ack_end + duration;
#ifndef __SDCC
            if (SC == 4 || SC == 13 || SC == 15 || SC == 17)
                now = join.poll.control.receive_end;
#endif
#endif
            event.kind = MAC_JOIN_FRAME;
#if defined(__SDCC) && MAC_JOIN_CASE == 12
            event.crc_valid = frames;
#endif
#if defined(__SDCC) && MAC_JOIN_CASE != 12
            event.serial = 1;
#else
            event.serial = ++frames;
#endif
#ifdef __SDCC
            event.body = response;
            event.length = sizeof(response);
#else
            event.body = SC == 0 ? response : frame;
            event.length = size;
#endif
            event.channel = config.extraction.channel;
#if !defined(__SDCC) || MAC_JOIN_CASE != 12
            event.crc_valid = 1;
#endif
#ifndef __SDCC
            if (SC == 12 && frames == 1) event.crc_valid = 0;
#endif
#if !defined(__SDCC) || MAC_JOIN_CASE == 17
            if (SC == 17) event.kind = 0;
#endif
#endif
        }
#endif
        event.stamp = now;
#if !defined(__SDCC) || MAC_JOIN_CASE == 13
        if (SC == 13 && event.kind == MAC_JOIN_FRAME) now++;
#endif
#if !defined(__SDCC) || MAC_JOIN_CASE == 21
        if (SC == 21 && calls == 0) event.epoch++;
        if (SC == 21 && calls == 2) { event.kind = MAC_JOIN_PREPARED; event.token = 1; }
#endif
#if !defined(__SDCC) || MAC_JOIN_CASE == 18
        if (SC == 18 && phase == MAC_JOIN_PREPARE) event.kind = MAC_JOIN_CANCEL;
#endif
        if (mac_join_step(&join, &tx, now, event.kind ? &event : NULL, &action) != MAC_JOIN_OK)
            goto complete;
#if !defined(__SDCC) || MAC_JOIN_CASE == 12
        if (SC == 12 && event.kind == MAC_JOIN_FRAME && !event.crc_valid)
            seen = action.observation;
#endif
#if !defined(__SDCC) || MAC_JOIN_CASE == 21
        if (SC == 21 && action.observation == MAC_JOIN_OBS_STALE) seen |= 1u;
        if (SC == 21 && action.observation == MAC_JOIN_OBS_DUPLICATE) seen |= 2u;
#endif
    }
    failure = 2;
    if (mac_join_take(&join, &record) != MAC_JOIN_OK)
        goto complete;
    release_result = mac_join_release(&join, &tx);
    failure = 0;
complete:
#ifdef __SDCC
    mac_join_result[0] = 'M'; mac_join_result[1] = 'J';
    mac_join_result[2] = 'N'; mac_join_result[3] = '1';
    mac_join_result[4] = 1; mac_join_result[5] = 8;
    mac_join_result[6] = failure; mac_join_result[7] = MAC_JOIN_CASE;
    __asm
        .globl _mac_join_done
        _mac_join_done:
        nop
    __endasm;
    for (;;) { }
#else
    return;
#endif
}

#ifndef __SDCC
#include <stdio.h>
#include <stdlib.h>
#define CHECK(c) do { if (!(c)) return __LINE__; } while (0)

/* An independent synthetic device world, serialized between real foreground
 * calls. Its start overwrites all returning step work. Cancel/restore/take/
 * release retire its own lease; no live context or fault is reset to proceed. */
static int other_attempt(void)
{
    mac_join_t other, saved = join, before;
    mac_tx_t transmitter, saved_tx = tx;
    mac_join_request_t request_copy = request;
    mac_join_event_t input_copy, saved_event = event;
    mac_join_action_t output_copy, saved_action = action, old_output;
    mac_join_record_t receipt_copy, saved_record = record;
    CHECK(mac_tx_init(&transmitter, 0x5a, 0) == MAC_TX_OK);
    CHECK(mac_join_init(&other, 0) == MAC_JOIN_OK);
    request_copy.extraction.epoch = 0x87654321UL;
    request_copy.extraction.channel = 26;
    request_copy.extraction.pan = 0x4567;
    request_copy.extraction.local[0] = 0x77;
    CHECK(mac_join_start(&other, &transmitter, &request_copy, 0) == MAC_JOIN_OK);
    memset(&input_copy, 0, sizeof(input_copy));
    input_copy.epoch = request_copy.extraction.epoch;
    input_copy.generation = other.generation;
    input_copy.kind = MAC_JOIN_CANCEL;
    CHECK(mac_join_step(&other, &transmitter, 0, &input_copy, &output_copy) == MAC_JOIN_OK);
    CHECK(output_copy.kind == MAC_JOIN_ACTION_RESTORE);
    input_copy.kind = MAC_JOIN_RESTORED;
    input_copy.token = output_copy.token;
    CHECK(mac_join_step(&other, &transmitter, 0, &input_copy, &output_copy) == MAC_JOIN_OK);
    CHECK(mac_join_take(&other, &receipt_copy) == MAC_JOIN_OK);
    CHECK(receipt_copy.reason == MAC_JOIN_CANCELLED);
    CHECK(mac_join_release(&other, &transmitter) == MAC_JOIN_OK);
    before = other; old_output = output_copy;
    input_copy.kind = 255;
    CHECK(mac_join_step(&other, &transmitter, 0, &input_copy, &output_copy) == MAC_JOIN_INVALID);
    CHECK(!memcmp(&other, &before, sizeof(other)));
    CHECK(!memcmp(&output_copy, &old_output, sizeof(output_copy)));
    CHECK(!memcmp(&join, &saved, sizeof(join)) && !memcmp(&tx, &saved_tx, sizeof(tx)));
    CHECK(!memcmp(&event, &saved_event, sizeof(event)) && !memcmp(&action, &saved_action, sizeof(action)));
    CHECK(!memcmp(&record, &saved_record, sizeof(record)));
    interleaved++;
    return 0;
}

static int expected(void)
{
    uint8_t faulted = SC == 8 || SC == 9 || SC == 10 || SC == 20;
    uint8_t early = SC == 18 || SC == 19;
    uint8_t request_only = early || SC == 5 || SC == 6 || SC == 7 || SC == 8 || SC == 10 || SC == 16 || SC == 21;
    uint8_t reply = SC <= 4 || SC == 9 || SC == 12 || SC == 15 || SC == 20;
    CHECK(failure == 0 && calls < 100);
    CHECK(join.phase == (faulted ? MAC_JOIN_FAULT : MAC_JOIN_IDLE));
    CHECK(release_result == (faulted ? MAC_JOIN_STATE : MAC_JOIN_OK));
    CHECK(join.owner == (faulted ? &tx : NULL));
    CHECK(tx.generation == (early ? 0 : request_only ? 1 : 2));
    CHECK(tx.next_dsn == (early ? 0xff : request_only ? 0 : 1));
    CHECK(record.epoch == 1 && record.generation == 1);
    if (reply) {
        CHECK(record.result == MAC_JOIN_POLL_RESULT && record.stage == MAC_JOIN_EXTRACT);
        CHECK(record.poll.protocol == MAC_POLL_NO_DATA && record.poll.cause == MAC_POLL_COMMAND);
        CHECK(record.poll.length == size && memcmp(record.poll.body, SC == 0 ? response : frame, size) == 0);
        CHECK(record.association.outcome == MAC_ASSOCIATION_RESPONSE && record.observation == MAC_ASSOCIATION_RESPONSE);
        CHECK(record.association.stamp == record.poll.stamp && record.association.sequence == 0x91);
        CHECK(record.association.status == (SC == 1 || SC == 2 ? SC : 0));
        CHECK(record.association.short_address == (SC == 1 || SC == 2 ? 0xffff : SC == 3 ? 0xfffe : 0x5678));
        CHECK(record.association.address_kind == (SC == 1 || SC == 2 ? MAC_ASSOCIATION_REFUSED
            : SC == 3 ? MAC_ASSOCIATION_EXTENDED_ONLY : MAC_ASSOCIATION_ALLOCATED));
        CHECK(record.association.source_relation == (SC == 4 ? MAC_ASSOCIATION_SOURCE_UNBOUND : MAC_ASSOCIATION_SOURCE_MATCHED));
    } else
        CHECK(record.association.outcome != MAC_ASSOCIATION_RESPONSE);
    if (SC == 5 || SC == 16) {
        CHECK(record.result == MAC_JOIN_REQUEST_RESULT && record.stage == MAC_JOIN_REQUEST);
        CHECK(record.tx_outcome == (SC == 5 ? MAC_TX_NO_ACK : MAC_TX_CHANNEL_ACCESS));
        CHECK(record.poll_rc == MAC_JOIN_UNCALLED && record.association_rc == MAC_JOIN_UNCALLED);
    }
    if (SC == 6 || SC == 7 || SC == 8 || SC == 10 || SC == 21 || early) {
        CHECK(record.result == MAC_JOIN_LOCAL_ABORT && record.poll.protocol == MAC_POLL_NO_CONFIRM);
        CHECK(record.reason == (SC == 6 || SC == 18 || SC == 21 ? MAC_JOIN_CANCELLED
            : SC == 7 ? MAC_JOIN_LIFETIME : SC == 8 ? MAC_JOIN_TX_ERROR
            : SC == 10 ? MAC_JOIN_CLOCK_ERROR : MAC_JOIN_WORK_LIMIT));
    } else if (SC == 9 || SC == 20) {
        CHECK(record.reason == (SC == 9 ? MAC_JOIN_CLEANUP_FAILED : MAC_JOIN_ADAPTER_ERROR));
        CHECK(record.cleanup_error == (SC == 9 ? MAC_JOIN_CLEANUP_FAILED : MAC_JOIN_POLL_ERROR));
        CHECK(record.error_stage == (SC == 9 ? MAC_JOIN_RESTORE : MAC_JOIN_EXTRACT));
    } else
        CHECK(record.reason == 0 && record.cleanup_error == 0);
    if (SC == 11 || SC == 14 || SC == 17) {
        CHECK(record.result == MAC_JOIN_POLL_RESULT && !record.poll.length);
        CHECK(record.poll.protocol == (SC == 11 ? MAC_POLL_NO_ACK : MAC_POLL_NO_DATA));
        CHECK(record.poll.cause == (SC == 11 ? MAC_POLL_TX_RESULT : SC == 14 ? MAC_POLL_PENDING_ZERO : MAC_POLL_TIMEOUT));
    }
    if (SC == 13) {
        CHECK(record.poll.protocol == MAC_POLL_NO_DATA && record.poll.cause == MAC_POLL_COMMAND);
        CHECK(record.poll.stamp == join.poll.control.receive_end);
        CHECK(record.association.outcome == MAC_ASSOCIATION_EXPIRED && record.observation == MAC_ASSOCIATION_EXPIRED);
        CHECK(record.association.stamp == record.poll.stamp + 1u);
    }
    if (SC == 12) CHECK(seen == MAC_POLL_OBS_BAD_CRC);
    if (SC == 21) CHECK(seen == 3);
    if (!faulted) CHECK(tx.phase == MAC_TX_IDLE && join.restored);
    if (SC == 20) CHECK(tx.phase == MAC_TX_DONE && record.poll_reason == MAC_POLL_ADAPTER_ERROR
                      && record.poll_cleanup == MAC_POLL_CLEANUP_FAILED);
    if (SC == 8) CHECK(tx.phase == MAC_TX_FAULT && record.tx_outcome == MAC_TX_STOP_FAILED);
    if (faulted) CHECK(join.uncertain && !join.restored);
    return 0;
}

static int exact_arguments(void)
{
    static const uint32_t frame_waits[] = {0, 1, 65534, 65535, UINT32_MAX};
    static const uint8_t frame_results[] = {
        MAC_JOIN_INVALID, MAC_JOIN_OK, MAC_JOIN_OK, MAC_JOIN_UNSUPPORTED, MAC_JOIN_UNSUPPORTED
    };
    mac_join_t *c = malloc(sizeof(*c)), before;
    mac_tx_t *t = malloc(sizeof(*t)), old_tx;
    mac_join_request_t *r = malloc(sizeof(*r));
    mac_join_action_t *a = malloc(sizeof(*a)), old_action;
    mac_join_event_t *e = malloc(sizeof(*e));
    uint8_t *b, ack_bytes[3], out_length;
    unsigned n, cap, profile;
    CHECK(c && t && r && a && e);
    for (n = 0; n < sizeof(frame_waits) / sizeof(frame_waits[0]); n++) {
        CHECK(mac_tx_init(t, 0xff, 0) == MAC_TX_OK && mac_join_init(c, 0) == MAC_JOIN_OK);
        *r = request; r->extraction.frame_wait = frame_waits[n];
        before = *c; old_tx = *t;
        CHECK(mac_join_start(c, t, r, 0) == frame_results[n]);
        CHECK(memcmp(t, &old_tx, sizeof(*t)) == 0);
        if (frame_results[n] != MAC_JOIN_OK)
            CHECK(memcmp(c, &before, sizeof(*c)) == 0);
        else
            CHECK(c->phase == MAC_JOIN_PREPARE && c->request.extraction.frame_wait == frame_waits[n]);
    }
    for (n = 0; n <= 255; n++) {
        CHECK(mac_tx_init(t, 0xff, 0) == MAC_TX_OK && mac_join_init(c, 0) == MAC_JOIN_OK);
        *r = request; r->response_wait = (uint8_t)n;
        before = *c; old_tx = *t;
        CHECK(mac_join_start(c, t, r, 0) == (n >= 2 && n <= 64 ? MAC_JOIN_OK : MAC_JOIN_INVALID));
        CHECK(memcmp(t, &old_tx, sizeof(*t)) == 0);
        if (n < 2 || n > 64)
            CHECK(memcmp(c, &before, sizeof(*c)) == 0);
        else
            CHECK(c->phase == MAC_JOIN_PREPARE && c->request.response_wait == n);
    }
    for (profile = 2; profile <= 255; profile++) {
        CHECK(mac_tx_init(t, 0xff, 0) == MAC_TX_OK && mac_join_init(c, 0) == MAC_JOIN_OK);
        *r = request; r->profile = (uint8_t)profile;
        before = *c; old_tx = *t;
        CHECK(mac_join_start(c, t, r, 0) == MAC_JOIN_UNSUPPORTED);
        CHECK(memcmp(c, &before, sizeof(*c)) == 0 && memcmp(t, &old_tx, sizeof(*t)) == 0);
    }
    for (n = 0; n <= 126; n++) {
        /* Each loop is a fresh synthetic adapter epoch, not fault recovery. */
        CHECK(mac_tx_init(t, 0xff, 0) == MAC_TX_OK && mac_join_init(c, 0) == MAC_JOIN_OK);
        *r = request;
        CHECK(mac_join_start(c, t, r, 0) == MAC_JOIN_OK);
        CHECK(mac_join_step(c, t, 0, NULL, a) == MAC_JOIN_OK && a->kind == MAC_JOIN_ACTION_PREPARE);
        memset(e, 0, sizeof(*e)); e->epoch = 1; e->generation = 1;
        e->kind = MAC_JOIN_PREPARED; e->token = a->token;
        CHECK(mac_join_step(c, t, 0, e, a) == MAC_JOIN_OK && a->kind == MAC_JOIN_ACTION_RADIO);
        for (cap = 0; cap <= 26; cap++) {
            b = malloc(cap ? cap : 1);
            CHECK(b != NULL);
            memset(b, 0xa5, cap ? cap : 1); out_length = 0xc7;
            old_tx = *t;
            CHECK(mac_tx_copy(t, b, (uint16_t)cap, &out_length) == (cap < 25 ? MAC_TX_SPACE : MAC_TX_OK));
            CHECK(memcmp(t, &old_tx, sizeof(*t)) == 0);
            if (cap < 25) {
                unsigned i;
                CHECK(out_length == 0xc7);
                for (i = 0; i < (cap ? cap : 1); i++) CHECK(b[i] == 0xa5);
            } else {
                static const uint8_t wire[] = {0x23,0xcc,0xff,0x34,0x12,17,18,19,20,21,22,23,24,
                                              0xff,0xff,1,2,3,4,5,6,7,8,1,0x88};
                CHECK(out_length == 25 && memcmp(b, wire, 25) == 0);
            }
            free(b);
        }
        e->kind = MAC_JOIN_SOURCE; e->source.kind = MAC_TX_EVENT_RANDOM;
        e->source.generation = t->generation;
        CHECK(mac_join_step(c, t, 0, e, a) == MAC_JOIN_OK);
        e->stamp = e->source.stamp = 74; e->source.kind = MAC_TX_EVENT_SENT;
        CHECK(mac_join_step(c, t, 74, e, a) == MAC_JOIN_OK && t->phase == MAC_TX_ACK_WAIT);
        b = malloc(n ? n : 1);
        CHECK(b != NULL);
        memset(b, 0, n ? n : 1);
        ack_bytes[0] = 0xfa; ack_bytes[1] = 0xff; ack_bytes[2] = 0xff;
        memcpy(b, ack_bytes, n < 3 ? n : 3);
        e->stamp = e->source.stamp = 108; e->source.kind = MAC_TX_EVENT_ACK;
        e->source.bytes = b; e->source.length = (uint16_t)n; e->crc_valid = 1;
        CHECK(mac_join_step(c, t, 108, e, a) == MAC_JOIN_OK);
        CHECK(t->phase == (n == 3 ? MAC_TX_STOPPING : MAC_TX_ACK_WAIT));
        CHECK(c->record.request_ack == (n == 3 ? 108 : 0));
        before = *c; old_tx = *t;
        memset(a, 0xa5, sizeof(*a)); old_action = *a;
        e->source.kind = 255;
        CHECK(mac_join_step(c, t, 108, e, a) == MAC_JOIN_INVALID);
        CHECK(memcmp(c, &before, sizeof(*c)) == 0 && memcmp(t, &old_tx, sizeof(*t)) == 0);
        CHECK(memcmp(a, &old_action, sizeof(*a)) == 0);
        free(b);
    }
    free(e); free(a); free(r); free(t); free(c);
    return 0;
}

int main(void)
{
    int rc = 0;
    for (scenario = 0; scenario <= 21; scenario++) {
        run();
        rc = expected();
        if (rc) break;
    }
    interleaving = 1;
    for (scenario = 0; !rc && scenario <= 21; scenario++) {
        run();
        rc = expected();
    }
    if (!rc) rc = exact_arguments();
    if (rc) {
        fprintf(stderr, "Join case %u line %d: failure %u phase %u reason %u poll %u/%u, %u calls\n",
                scenario, rc, failure, join.phase, record.reason, join.poll.control.phase, record.poll_reason, calls);
        return 1;
    }
    printf("Join: 22 genuine sequence cases, 5 F boundaries, 256 response waits, "
           "254 rejected profiles, exact TX-copy and ACK spans PASS.\n");
    printf("Join: all 22 sequences repeated with %u interleaved retired attempts PASS.\n", interleaved);
    return 0;
}
#endif
