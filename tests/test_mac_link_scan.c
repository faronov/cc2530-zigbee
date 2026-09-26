/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 * Interval scan consumer over the real observed MAC owner. Synthetic events
 * only; no radio implementation or successful stub.
 */
#include "mac_scan.h"
#include <stdio.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) return (uint16_t)__LINE__; } while (0)
#define CALL(c) do { uint16_t line_ = (c); if (line_) return line_; } while (0)

#define CH11 (UINT32_C(1) << 11)
#define CH26 (UINT32_C(1) << 26)

enum {
    ORDINARY, EARLY_CLOSE, BUSY, CLOSE_MISSING, CANCEL_RX, TX_MISMATCH,
    LIFETIME, SCENARIOS
};

static const uint8_t beacon[] = {
    0, 0x80, 0x2a, 0x34, 0x12, 0x78, 0x56, 0xff, 0x8f, 0x80, 0,
    0, 0x22, 0xac, 0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe,
    0xff, 0xff, 0xff, 0x7f
};
static const uint16_t fines[] = {0, 1, 256, 511};
static const uint32_t epochs[] = {0, UINT32_C(0xffffffff) - 3000u};

static mac_scan_t scan, saved;
static mac_tx_interval_t tx;
static mac_scan_request_t request;
static mac_scan_event_t event;
static mac_scan_action_t action, saved_action;
static mac_tx_interval_event_t tx_event;
static mac_tx_interval_action_t tx_action;
static nwk_candidate_t entry;
static uint8_t body[sizeof(beacon)], copy[16], length;
static uint8_t scenario, received, busy, dsn, injected, observed, visited;
static uint16_t fine;
static uint32_t now, at, epoch;

static void local(uint8_t kind)
{
    memset(&event, 0, sizeof(event));
    event.kind = kind;
    event.generation = scan.generation;
    event.token = scan.token;
    event.stamp = now;
    event.state = action.state;
}

static void tx_source(uint8_t kind)
{
    memset(&tx_event, 0, sizeof(tx_event));
    tx_event.source.kind = kind;
    tx_event.source.generation = tx.engine.generation;
    tx_event.source.retry = tx.engine.retries;
    tx_event.source.nb = tx.engine.nb;
}

/* Exactly one observed MAC step per controller grant, fed by the previous
 * MAC action. The report stamp is that step's foreground time.
 */
static uint16_t grant(void)
{
    uint8_t has = 1;
    if (action.tx_cancel)
        tx_source(MAC_TX_EVENT_CANCEL);
    else if (tx_action.control.kind == MAC_TX_ACTION_RANDOM) {
        tx_source(MAC_TX_EVENT_RANDOM);
        tx_event.source.value = 0x3c;
    } else if (tx_action.control.kind == MAC_TX_ACTION_ATTEMPT) {
        at = tx_action.control.at;
        CHECK(mac_tx_interval_copy(&tx, copy, sizeof(copy), &length) == MAC_TX_OK);
        CHECK(length == 8 && copy[0] == 3 && copy[1] == 8 && copy[2] == dsn && copy[7] == 7);
        CHECK(!tx_action.control.ack_requested);
        if (scenario == BUSY && scan.channel == 11) {
            now = at + 20u;
            tx_source(MAC_TX_EVENT_BUSY_INTERVAL);
            tx_event.lower.symbols = at + 8u;
            tx_event.upper.symbols = at + 10u;
            busy++;
        } else {
            now = at + 46u;
            tx_source(MAC_TX_EVENT_SENT_INTERVAL);
            tx_event.lower.symbols = at + 40u;
            tx_event.upper.symbols = at + 44u;
        }
        tx_event.lower.fine = fine;
        tx_event.upper.fine = fine;
    } else if (tx_action.control.kind == MAC_TX_ACTION_QUIESCE) {
        tx_source(MAC_TX_EVENT_RETIRED);
        tx_event.upper.symbols = now;
    } else
        has = 0;
    tx_event.source.stamp = now;
    CHECK(mac_tx_observed_step(&tx, now, has ? &tx_event : NULL, &tx_action) == MAC_TX_OK);
    if (tx.engine.phase == MAC_TX_DONE)
        dsn++;
    local(MAC_SCAN_EVENT_TX);
    event.tx_result = MAC_TX_OK;
    if (scenario == TX_MISMATCH && !injected && tx.engine.phase == MAC_TX_STOPPING) {
        event.stamp = now - 1u;
        injected = 1;
    }
    if (scenario == LIFETIME && tx_action.control.kind == MAC_TX_ACTION_ATTEMPT)
        now = scan.deadline;
    return 0;
}

static void beacon_event(uint8_t pan, uint8_t channel)
{
    memcpy(body, beacon, sizeof(beacon));
    body[3] = pan;
    local(MAC_SCAN_EVENT_BEACON);
    event.body = body;
    event.length = sizeof(beacon);
    event.crc_valid = 1;
    event.state.pan = 0xffffu;
    event.state.channel = channel;
    event.state.filter = MAC_SCAN_FILTER_BEACONS;
    event.state.rx_on = 1;
}

static uint16_t receive(void)
{
    if (scenario == ORDINARY && scan.channel == 11 && received < 3) {
        if (received == 0) {
            now += 100u;
            beacon_event(0x10, 11);
        } else if (received == 1) {
            now += 10u;
            beacon_event(0x20, 12);
        } else {
            /* Ordered delivery after dwell but before closure is still a
             * beacon physically received on this channel. */
            now = scan.window_end + 5u;
            beacon_event(0x30, 11);
        }
        received++;
        return 0;
    }
    if (scenario == CANCEL_RX) {
        now += 5u;
        local(MAC_SCAN_EVENT_CANCEL);
        return 0;
    }
    if (scenario == CLOSE_MISSING && scan.channel == 11) {
        now = scan.window_end + MAC_SCAN_CLOSE_GRACE;
        memset(&event, 0, sizeof(event));
        return 0;
    }
    if (scenario == EARLY_CLOSE && scan.channel == 11)
        now = scan.window_end - 1u;
    else if (scan.channel == 11)
        now = scan.window_end + 7u;
    else
        now = scan.window_end;
    local(MAC_SCAN_EVENT_CLOSED);
    if (scenario == ORDINARY && scan.channel == 11) {
        /* Drain reports advanced last to end+5; the actual pre-stop watermark
         * remains end. Delivery latency cannot move coverage forward. */
        event.stamp = scan.window_end;
    }
    event.state.rx_on = 0;
    return 0;
}

static uint16_t run(void)
{
    uint16_t guard;
    memset(&action, 0, sizeof(action));
    memset(&tx_action, 0, sizeof(tx_action));
    received = busy = injected = observed = visited = 0;
    for (guard = 0; guard < 400; guard++) {
        const mac_scan_event_t *input = &event;
        if (scan.phase == MAC_SCAN_DONE || scan.phase == MAC_SCAN_FAULT)
            break;
        if (action.kind == MAC_SCAN_ACTION_CONFIG) {
            now += 2u;
            local(MAC_SCAN_EVENT_CONFIGURED);
            CHECK(!event.state.rx_on && event.state.pan == 0xffffu);
        } else if (action.kind == MAC_SCAN_ACTION_TX)
            CALL(grant());
        else if (action.kind == MAC_SCAN_ACTION_RECEIVE) {
            now += 3u;
            local(MAC_SCAN_EVENT_OPENED);
            CHECK(event.state.rx_on);
            visited++;
        } else if (action.kind == MAC_SCAN_ACTION_RESTORE) {
            now += 2u;
            local(MAC_SCAN_EVENT_RESTORED);
            CHECK(event.state.pan == 0x1234u && event.state.channel == 15);
            CHECK(mac_scan_release(&scan, &tx) == MAC_SCAN_STATE);
        } else if (scan.phase == MAC_SCAN_RX)
            CALL(receive());
        else
            input = NULL;
        if (input != NULL && !event.kind)
            input = NULL;
        CHECK(mac_scan_step(&scan, &tx, now, input, &action) == MAC_SCAN_OK);
        if (scan.phase == MAC_SCAN_OPEN || scan.phase == MAC_SCAN_RX)
            CHECK(tx.engine.phase == MAC_TX_IDLE);
        if (action.observed) {
            observed++;
            CHECK(action.candidate_result == NWK_CANDIDATES_ADDED);
        }
    }
    CHECK(guard < 400);
    return 0;
}

static uint16_t start(uint32_t channels)
{
    memset(&request, 0, sizeof(request));
    request.channels = channels;
    request.duration = 0;
    request.lifetime = scenario == LIFETIME ? 5000u : MAC_SCAN_MAX_LIFETIME;
    request.work = 512;
    request.saved.pan = 0x1234;
    request.saved.channel = 15;
    request.saved.filter = MAC_SCAN_FILTER_NORMAL;
    request.saved.rx_on = 1;
    dsn = tx.engine.next_dsn;
    CHECK(mac_scan_start(&scan, &tx, &request, now) == MAC_SCAN_OK);
    CHECK(scan.dwell == UINT32_C(1920) && scan.owner == &tx);
    return 0;
}

static uint16_t scenario_case(void)
{
    now = epoch;
    CHECK(mac_tx_interval_init(&tx, 0x40, now) == MAC_TX_OK);
    CHECK(mac_scan_init(&scan) == MAC_SCAN_OK);
    CALL(start(CH11 | CH26));
    CALL(run());
    CHECK(scan.phase == MAC_SCAN_DONE);
    switch (scenario) {
    case ORDINARY:
        CHECK(scan.reason == MAC_SCAN_FINISHED && !scan.uncertain && !scan.unscanned);
        CHECK(scan.sent == (CH11 | CH26) && scan.tx_outcome == MAC_TX_UNACKNOWLEDGED);
        CHECK(observed == 2 && visited == 2);
        CHECK(mac_scan_get(&scan, 0, &entry) == MAC_SCAN_OK && entry.pan_id == 0x1210u);
        CHECK(mac_scan_get(&scan, 1, &entry) == MAC_SCAN_OK && entry.pan_id == 0x1230u);
        CHECK(mac_scan_get(&scan, 2, &entry) == MAC_SCAN_BAD_INDEX);
        CHECK(tx.engine.next_dsn == (uint8_t)0x42);
        break;
    case EARLY_CLOSE:
        CHECK(scan.reason == MAC_SCAN_ADAPTER_ERROR && scan.uncertain);
        CHECK(scan.unscanned == (CH11 | CH26) && scan.sent == CH11 && visited == 1);
        break;
    case BUSY:
        CHECK(busy == 5 && scan.reason == MAC_SCAN_FINISHED && !scan.uncertain);
        CHECK(scan.sent == CH26 && scan.unscanned == CH11 && visited == 1);
        break;
    case CLOSE_MISSING:
        CHECK(scan.reason == MAC_SCAN_CLOSE_MISSING && scan.uncertain);
        CHECK(scan.unscanned == (CH11 | CH26));
        break;
    case CANCEL_RX:
        CHECK(scan.reason == MAC_SCAN_CANCELLED && !scan.uncertain && visited == 1);
        break;
    case LIFETIME:
        CHECK(scan.reason == MAC_SCAN_LIFETIME && scan.uncertain && !scan.sent && !visited);
        break;
    }
    CHECK(mac_scan_release(&scan, &tx) == MAC_SCAN_OK);
    CHECK(mac_scan_release(&scan, &tx) == MAC_SCAN_STATE);
    return 0;
}

static uint16_t mismatch_case(void)
{
    now = epoch;
    CHECK(mac_tx_interval_init(&tx, 0x40, now) == MAC_TX_OK);
    CHECK(mac_scan_init(&scan) == MAC_SCAN_OK);
    CALL(start(CH11));
    CALL(run());
    CHECK(scan.phase == MAC_SCAN_FAULT && scan.reason == MAC_SCAN_TX_ERROR && scan.uncertain);
    CHECK(mac_scan_release(&scan, &tx) == MAC_SCAN_STATE);
    return 0;
}

static uint16_t reuse_case(void)
{
    uint8_t first;
    scenario = ORDINARY;
    now = epoch;
    CHECK(mac_tx_interval_init(&tx, 0xff, now) == MAC_TX_OK);
    CHECK(mac_scan_init(&scan) == MAC_SCAN_OK);
    CALL(start(CH26));
    CALL(run());
    CHECK(scan.phase == MAC_SCAN_DONE && scan.reason == MAC_SCAN_FINISHED);
    CHECK(mac_scan_release(&scan, &tx) == MAC_SCAN_OK);
    first = tx.engine.next_dsn;
    CHECK(first == 0);
    CALL(start(CH26));
    CHECK(scan.generation == 2);
    CALL(run());
    CHECK(scan.phase == MAC_SCAN_DONE && tx.engine.next_dsn == (uint8_t)(first + 1u));
    CHECK(mac_scan_release(&scan, &tx) == MAC_SCAN_OK);
    return 0;
}

static uint16_t guard_case(void)
{
    now = epoch;
    CHECK(mac_tx_interval_init(&tx, 0x40, now) == MAC_TX_OK);
    CHECK(mac_scan_init(&scan) == MAC_SCAN_OK);
    CALL(start(CH11));
    memset(&action, 0x5a, sizeof(action));
    memcpy(&saved, &scan, sizeof(scan));
    memcpy(&saved_action, &action, sizeof(action));
    local(MAC_SCAN_EVENT_CANCEL + 1u);
    CHECK(mac_scan_step(&scan, &tx, now, &event, &action) == MAC_SCAN_INVALID);
    local(MAC_SCAN_EVENT_BEACON);
    event.crc_valid = 1;
    CHECK(mac_scan_step(&scan, &tx, now, &event, &action) == MAC_SCAN_INVALID);
    CHECK(mac_scan_step(&scan, NULL, now, NULL, &action) == MAC_SCAN_INVALID);
    CHECK(!memcmp(&saved, &scan, sizeof(scan)) && !memcmp(&saved_action, &action, sizeof(action)));
    CHECK(mac_scan_release(&scan, &tx) == MAC_SCAN_STATE);
    CALL(run());
    CHECK(scan.phase == MAC_SCAN_DONE && scan.reason == MAC_SCAN_FINISHED);
    CHECK(mac_scan_release(&scan, &tx) == MAC_SCAN_OK);
    /* A leased or busy interval owner cannot start another scan. */
    CHECK(mac_tx_interval_submit(&tx, copy, 8, now, 100, 1) == MAC_TX_OK);
    CHECK(mac_scan_start(&scan, &tx, &request, now) == MAC_SCAN_STATE);
    CHECK(mac_scan_init(&saved) == MAC_SCAN_OK);
    CHECK(mac_scan_start(&saved, &tx, &request, now) == MAC_SCAN_STATE);
    return 0;
}

int main(void)
{
    uint16_t line = 0;
    uint8_t e, f;
    for (e = 0; e < 2 && !line; e++) {
        epoch = epochs[e];
        for (f = 0; f < 4 && !line; f++) {
            fine = fines[f];
            for (scenario = 0; scenario < SCENARIOS; scenario++)
                if ((line = scenario == TX_MISMATCH ? mismatch_case() : scenario_case()) != 0)
                    break;
            if (!line)
                line = reuse_case();
        }
    }
    if (!line) {
        scenario = ORDINARY;
        line = guard_case();
    }
    if (line) {
        fprintf(stderr, "mac_scan link failure line%u scenario%u fine%u epoch%lu phase%u reason%u\n",
                line, scenario, fine, (unsigned long)epoch, scan.phase, scan.reason);
        return 1;
    }
    puts("mac_scan link: interval TX, ordered beacons, loss-free closure, busy/cancel/lifetime/"
         "early-close/missing-close/TX-identity cases over all fine phases and wrap PASS");
    return 0;
}
