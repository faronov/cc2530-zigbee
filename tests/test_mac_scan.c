/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 * Explicit synthetic source events. No radio implementation or successful stub.
 */
#include "mac_scan.h"
#include "cc2530_mmio.h"
#include <stddef.h>
#include <string.h>

static const MCU_CODE uint8_t beacon[] = {
    0, 0x80, 0x2a, 0x34, 0x12, 0x78, 0x56, 0xff, 0x8f, 0x80, 0,
    0, 0x22, 0xac, 0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe,
    0xff, 0xff, 0xff, 0x7f
};
static const MCU_CODE uint8_t long_beacon[] = {
    0x10, 0xc0, 0x2b, 0x34, 0x12, 1, 2, 3, 4, 5, 6, 7, 8,
    0xff, 0xcf, 0, 0x12, 0x21, 0x22, 0x23, 0x24,
    0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
    0, 0x22, 0x84, 0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe,
    0x56, 0x34, 0x12, 0xa5
};
/* Logical outcome is independent of cleanup success and result count. */
static const MCU_CODE uint8_t reasons[] = {
    1,1,1,1,2,2,2,6,1,7,6,8,4,3,4,1,5,1,1,1,1,2,1,1,5,7,1,3
};
static mac_scan_t scan, saved;
static mac_tx_t tx;
static mac_scan_request_t request;
static mac_scan_event_t event;
static mac_scan_action_t action, before;
static mac_tx_event_t tx_event;
static mac_tx_action_t tx_action;
static nwk_candidate_t entry;
static uint8_t body[44], copy[sizeof(nwk_candidate_t)], length;
static volatile uint8_t scenario, injected, received, dsn, j, was_radio;
static volatile uint16_t iterations, failure;
static volatile uint32_t now, floor_at;
#define CHECK(c) do { if (!(c)) { failure = __LINE__; goto done; } } while (0)

#ifdef CC2530_HOST_TEST
#include <stdio.h>
#include <stdlib.h>
static uint16_t host_bounds(void);
int main(void)
#else
volatile __xdata __at(0x1e00) uint8_t mac_scan_result[8];
void main(void)
#endif
{
    CHECK(mac_scan_init(NULL) == MAC_SCAN_INVALID);
    for (scenario = 0; scenario < sizeof(reasons); scenario++) {
        /* Reuse cases20/22 preserve the real device-wide DSN and IFS across scans.
         * Other cases explicitly start independent synthetic reset/adapter epochs.
         */
        if (scenario != 20 && scenario != 22) {
            now = scenario == 17 ? UINT32_MAX - 100u : 0;
            CHECK(mac_tx_init(&tx, 255, now) == MAC_TX_OK);
            CHECK(mac_scan_init(&scan) == MAC_SCAN_OK);
        }
        floor_at = tx.ready_at;
        dsn = tx.next_dsn;
        memset(&request, 0, sizeof(request));
        request.channels = (UINT32_C(1) << 11) | (UINT32_C(1) << 26);
        if (scenario == 20) request.channels = NWK_CANDIDATES_CHANNEL_MASK;
        if (scenario == 1 || scenario == 18 || scenario == 19 || scenario == 21)
            request.channels = UINT32_C(1) << 11;
        request.duration = scenario == 18 ? 14 : 0;
        request.lifetime = MAC_SCAN_MAX_LIFETIME;
        request.work = 512;
        request.saved.pan = 0x1234;
        request.saved.channel = 15;
        request.saved.filter = MAC_SCAN_FILTER_NORMAL;
        request.saved.rx_on = 1;
        if (scenario == 12) request.work = 1;
        if (scenario == 14) request.work = 8;
        if (scenario == 13) request.lifetime = 1;
        saved = scan;
        request.duration = 15;
        CHECK(mac_scan_start(&scan, &tx, &request, now) == MAC_SCAN_INVALID);
        CHECK(memcmp(&saved, &scan, sizeof(scan)) == 0);
        request.duration = scenario == 18 ? 14 : 0;
        CHECK(mac_scan_start(&scan, &tx, &request, now) == MAC_SCAN_OK);
        CHECK(scan.dwell == (scenario == 18 ? UINT32_C(15729600) : UINT32_C(1920)));
        CHECK(tx.next_dsn == dsn);
        memset(&action, 0xc7, sizeof(action));
        before = action; saved = scan;
        memset(&event, 0, sizeof(event));
        event.kind = MAC_SCAN_EVENT_BEACON; event.body = beacon; event.crc_valid = 2;
        CHECK(mac_scan_step(&scan, &tx, now, &event, &action) == MAC_SCAN_INVALID);
        CHECK(memcmp(&saved, &scan, sizeof(scan)) == 0);
        CHECK(memcmp(&before, &action, sizeof(action)) == 0);
        memset(&action, 0, sizeof(action));
        memset(&tx_action, 0, sizeof(tx_action));
        injected = received = 0;
        for (iterations = 0; iterations < 180; iterations++) {
            memset(&event, 0, sizeof(event));
            event.generation = scan.generation;
            event.token = scan.token;
            event.state = action.state;
            was_radio = 0;
            if (action.kind == MAC_SCAN_ACTION_CONFIG
                    || (scenario == 23 && scan.phase == MAC_SCAN_CONFIG && scan.issued)) {
                event.kind = MAC_SCAN_EVENT_CONFIGURED;
                CHECK(action.state.pan == 0xffff && action.state.channel == scan.channel);
                CHECK(action.state.filter == MAC_SCAN_FILTER_BEACONS && !action.state.rx_on);
            } else if (action.kind == MAC_SCAN_ACTION_TX) {
                /* Serialized bridge: the controller never substitutes a radio
                 * success. Execute this real MAC step once for this grant.
                 */
                memset(&tx_event, 0, sizeof(tx_event));
                event.kind = MAC_SCAN_EVENT_TX;
                tx_event.generation = tx.generation;
                tx_event.retry = tx.retries; tx_event.nb = tx.nb;
                was_radio = tx.phase == MAC_TX_RADIO;
                if (tx_action.kind == MAC_TX_ACTION_RANDOM) {
                    CHECK(mac_tx_copy(&tx, copy, 8, &length) == MAC_TX_OK);
                    CHECK(length == 8 && copy[0] == 3 && copy[1] == 8 && copy[7] == 7);
                    CHECK(!tx_action.ack_requested && copy[2] == dsn);
                    for (j = 3; j < 7; j++) CHECK(copy[j] == 255);
                    tx_event.kind = MAC_TX_EVENT_RANDOM;
                } else if (tx_action.kind == MAC_TX_ACTION_ATTEMPT) {
                    if (scenario == 22 && scan.channel == 11) CHECK(tx_action.at == floor_at);
                    if (scenario == 2 || (scenario == 3 && scan.channel == 11)) {
                        tx_event.kind = MAC_TX_EVENT_BUSY;
                        now = tx_action.at + 8u;
                    } else {
                        tx_event.kind = MAC_TX_EVENT_SENT;
                        now = tx_action.at + 40u;
                    }
                } else if (tx_action.kind == MAC_TX_ACTION_QUIESCE) {
                    tx_event.kind = MAC_TX_EVENT_QUIESCED;
                }
                if (action.tx_cancel) tx_event.kind = MAC_TX_EVENT_CANCEL;
                if (scenario == 5 && was_radio && !injected) tx_event.kind = 0;
                if (scenario == 9 && was_radio) tx_event.kind = MAC_TX_EVENT_FAILURE;
                if (scenario == 25 && tx.phase == MAC_TX_STOPPING) {
                    tx_event.kind = 0; now = tx.stop_at;
                }
                tx_event.stamp = now;
                event.tx_result = mac_tx_step(&tx, now, tx_event.kind ? &tx_event : NULL, &tx_action);
                CHECK(event.tx_result == MAC_TX_OK);
            } else if (scan.phase == MAC_SCAN_TX && scan.issued) {
                /* A deliberately interrupted delivery reports the SAME completed
                 * pump, not a second mac_tx_step without a controller grant.
                 */
                event.kind = MAC_SCAN_EVENT_TX;
                event.tx_result = MAC_TX_OK;
            } else if (action.kind == MAC_SCAN_ACTION_RECEIVE) {
                event.kind = MAC_SCAN_EVENT_OPENED;
                received = 0;
                dsn++; /* One real Request admission per successfully visited channel. */
            } else if (action.kind == MAC_SCAN_ACTION_RESTORE) {
                event.kind = MAC_SCAN_EVENT_RESTORED;
                CHECK(event.state.pan == request.saved.pan && event.state.channel == 15);
                CHECK(event.state.filter == MAC_SCAN_FILTER_NORMAL && event.state.rx_on == 1);
                CHECK(mac_scan_release(&scan, &tx) == MAC_SCAN_STATE);
            } else if (scan.phase == MAC_SCAN_RX) {
                if ((scenario == 1 && received < 10) || (scenario == 19 && received < 2)) {
                    memcpy(body, beacon, sizeof(beacon));
                    event.kind = MAC_SCAN_EVENT_BEACON;
                    event.body = body; event.length = sizeof(beacon); event.crc_valid = 1;
                    if (scenario == 1) {
                        body[3] = (uint8_t)(0x10u + received);
                        if (received >= 5 && received <= 6) {
                            body[3] = 0x11; body[8] &= 0x7f;
                            event.crc_valid = received == 6;
                        }
                        if (received >= 7) body[3] = 0x14;
                        if (received == 8) event.length--;
                    } else if (!received) {
                        event.body = long_beacon; event.length = sizeof(long_beacon);
                    }
                    received++; now++;
                } else {
                    event.kind = MAC_SCAN_EVENT_CLOSED;
                    now = scan.window_end;
                    if (scenario == 10) now--;
                    if (scenario == 11) {
                        event.kind = 0; now += MAC_SCAN_CLOSE_GRACE;
                    }
                }
            }
            if (!injected) {
                if ((scenario == 4 && scan.phase == MAC_SCAN_CONFIG && scan.issued)
                        || (scenario == 5 && was_radio)
                        || (scenario == 6 && scan.phase == MAC_SCAN_RX)
                        || (scenario == 21 && scan.phase == MAC_SCAN_OPEN)) {
                    event.kind = MAC_SCAN_EVENT_CANCEL; injected = 1;
                } else if (scenario == 7 && scan.phase == MAC_SCAN_CONFIG && scan.issued) {
                    event.kind = MAC_SCAN_EVENT_FAILURE; injected = 1;
                } else if ((scenario == 16 || scenario == 24) && scan.issued) {
                    now = scenario == 16 ? now - 1u : now + MAC_TX_HALF;
                    event.kind = 0; injected = 1;
                } else if (scenario == 23 && event.kind == MAC_SCAN_EVENT_CONFIGURED) {
                    event.token--; injected = 1;
                }
            }
            if (scenario == 13 && scan.issued && !scan.stopping) now = scan.deadline;
            if (scenario == 14 && !scan.stopping) event.kind = 0;
            if (event.kind == MAC_SCAN_EVENT_RESTORED) {
                if (scenario == 8) event.state.pan++;
                if (scenario == 26) now = scan.deadline;
            }
            if (scenario == 15 && scan.phase == MAC_SCAN_RESTORE) event.kind = 0;
            if (scenario == 27 && scan.phase == MAC_SCAN_OPEN && scan.issued) {
                event.kind = 0; now = scan.deadline;
            }
            event.stamp = now;
            CHECK(mac_scan_step(&scan, &tx, now, event.kind ? &event : NULL, &action) == MAC_SCAN_OK);
            if (scan.phase >= MAC_SCAN_DONE) break;
            /* CCA-failed channels consumed DSNs even though they did not open RX. */
            if (scan.phase == MAC_SCAN_CONFIG && scan.channel == 26) dsn = tx.next_dsn;
        }
        CHECK(iterations < 180 && scan.reason == reasons[scenario]);
        CHECK(!(scan.unscanned & ~request.channels));
        if (scenario == 2) CHECK(!scan.sent && scan.unscanned == request.channels);
        if (scenario == 3) CHECK(scan.unscanned == (UINT32_C(1) << 11));
        if (scenario == 1) CHECK(scan.candidates.count == 4 && scan.overflow);
        if (scenario == 19) {
            CHECK(scan.candidates.count == 2);
            CHECK(mac_scan_get(&scan, 0, &entry) == MAC_SCAN_OK);
            CHECK(entry.address_mode == MAC_ADDRESS_EXTENDED && entry.extended_pending == 1);
            CHECK(mac_scan_get(&scan, 1, &entry) == MAC_SCAN_OK);
            for (j = 2; j < 8; j++) CHECK(entry.coordinator[j] == 0);
        }
        if (scenario == 8 || scenario == 9 || scenario == 15 || scenario == 16
                || scenario == 24 || scenario == 25 || scenario == 26) {
            CHECK(scan.phase == MAC_SCAN_FAULT && scan.uncertain);
            CHECK(mac_scan_release(&scan, &tx) == MAC_SCAN_STATE);
            if (scenario == 8 || scenario == 15 || scenario == 26)
                CHECK(scan.cleanup_error == MAC_SCAN_CLEANUP_FAILED);
        } else {
            CHECK(scan.phase == MAC_SCAN_DONE && tx.phase == MAC_TX_IDLE);
            if (scan.reason == MAC_SCAN_FINISHED && scenario != 2 && scenario != 3)
                CHECK(!scan.unscanned);
            memset(&entry, 0xc7, sizeof(entry)); memcpy(copy, &entry, sizeof(entry));
            CHECK(mac_scan_get(&scan, 255, &entry) == MAC_SCAN_BAD_INDEX);
            CHECK(memcmp(copy, &entry, sizeof(entry)) == 0);
            CHECK(mac_scan_release(&scan, &tx) == MAC_SCAN_OK);
        }
    }
#ifdef CC2530_HOST_TEST
    failure = host_bounds();
#endif
done:
#ifdef CC2530_HOST_TEST
    if (failure) { fprintf(stderr, "mac_scan: case %u line %u\n", scenario, failure); return 1; }
    puts("mac_scan: 28 real-TX/collector scenarios and exact-boundary corpus PASS");
    return 0;
#else
    mac_scan_result[0] = 'S'; mac_scan_result[1] = 'C';
    mac_scan_result[2] = 'N'; mac_scan_result[3] = '1';
    mac_scan_result[4] = 1; mac_scan_result[5] = 8;
    mac_scan_result[6] = (uint8_t)failure;
    mac_scan_result[7] = (uint8_t)(failure >> 8);
    __asm
        .globl _mac_scan_done
        _mac_scan_done:
        nop
    __endasm;
    for (;;) { }
#endif
}

#ifdef CC2530_HOST_TEST
#define H(c) do { if (!(c)) return (uint16_t)__LINE__; } while (0)
#define HC(c) do { uint16_t e = (c); if (e) return e; } while (0)

static void host_event(uint8_t kind)
{
    memset(&event, 0, sizeof(event));
    event.kind = kind; event.generation = scan.generation; event.token = scan.token;
    event.stamp = now;
    event.state.pan = 0xffff; event.state.channel = scan.channel;
    event.state.filter = MAC_SCAN_FILTER_BEACONS;
}

static uint16_t host_start(void)
{
    H(mac_tx_init(&tx, 0xfe, now) == MAC_TX_OK);
    H(mac_scan_init(&scan) == MAC_SCAN_OK);
    memset(&request, 0, sizeof(request));
    request.channels = UINT32_C(1) << 11; request.lifetime = 10000;
    request.work = MAC_SCAN_MAX_WORK;
    request.saved.pan = 0x4321; request.saved.channel = 26; request.saved.rx_on = 1;
    return 0;
}

static uint16_t host_finish(void)
{
    host_event(MAC_SCAN_EVENT_CANCEL);
    H(mac_scan_step(&scan, &tx, now, &event, &action) == MAC_SCAN_OK);
    H(action.kind == MAC_SCAN_ACTION_RESTORE);
    host_event(MAC_SCAN_EVENT_RESTORED); event.state = request.saved;
    H(mac_scan_step(&scan, &tx, now, &event, &action) == MAC_SCAN_OK);
    H(scan.phase == MAC_SCAN_DONE && mac_scan_release(&scan, &tx) == MAC_SCAN_OK);
    return 0;
}

static uint16_t host_to_receive(void)
{
    unsigned calls = 0;
    now = 0;
    HC(host_start());
    H(mac_scan_start(&scan, &tx, &request, now) == MAC_SCAN_OK);
    H(mac_scan_step(&scan, &tx, now, NULL, &action) == MAC_SCAN_OK);
    host_event(MAC_SCAN_EVENT_CONFIGURED);
    H(mac_scan_step(&scan, &tx, now, &event, &action) == MAC_SCAN_OK);
    H(mac_scan_step(&scan, &tx, now, NULL, &action) == MAC_SCAN_OK);
    H(mac_scan_step(&scan, &tx, now, NULL, &action) == MAC_SCAN_OK);
    while (action.kind == MAC_SCAN_ACTION_TX) {
        H(++calls <= 5);
        memset(&tx_event, 0, sizeof(tx_event));
        tx_event.generation = tx.generation;
        if (tx.phase == MAC_TX_DRAW_WAIT)
            tx_event.kind = MAC_TX_EVENT_RANDOM;
        else if (tx.phase == MAC_TX_RADIO) {
            tx_event.kind = MAC_TX_EVENT_SENT; now = tx.at + 40u;
        } else if (tx.phase == MAC_TX_STOPPING)
            tx_event.kind = MAC_TX_EVENT_QUIESCED;
        tx_event.stamp = now;
        host_event(MAC_SCAN_EVENT_TX);
        event.tx_result = mac_tx_step(&tx, now, tx_event.kind ? &tx_event : NULL, &tx_action);
        H(event.tx_result == MAC_TX_OK);
        H(mac_scan_step(&scan, &tx, now, &event, &action) == MAC_SCAN_OK);
    }
    H(scan.phase == MAC_SCAN_OPEN && tx.phase == MAC_TX_IDLE);
    H(mac_scan_step(&scan, &tx, now, NULL, &action) == MAC_SCAN_OK);
    H(action.kind == MAC_SCAN_ACTION_RECEIVE);
    return 0;
}

static uint16_t host_open(void)
{
    HC(host_to_receive());
    host_event(MAC_SCAN_EVENT_OPENED); event.state.rx_on = 1;
    H(mac_scan_step(&scan, &tx, now, &event, &action) == MAC_SCAN_OK);
    H(scan.phase == MAC_SCAN_RX);
    return 0;
}

static uint16_t host_late_pump_cancellation(void)
{
    mac_scan_event_t completed;
    mac_tx_t held_tx;
    unsigned polls;

    /* Independent synthetic epoch. Use real calls to reach an outstanding
     * radio attempt and complete one granted foreground poll at time t.
     */
    now = 100;
    HC(host_start());
    H(mac_scan_start(&scan, &tx, &request, now) == MAC_SCAN_OK);
    H(mac_scan_step(&scan, &tx, now, NULL, &action) == MAC_SCAN_OK);
    host_event(MAC_SCAN_EVENT_CONFIGURED);
    H(mac_scan_step(&scan, &tx, now, &event, &action) == MAC_SCAN_OK);
    H(mac_scan_step(&scan, &tx, now, NULL, &action) == MAC_SCAN_OK);
    H(mac_scan_step(&scan, &tx, now, NULL, &action) == MAC_SCAN_OK);
    H(action.kind == MAC_SCAN_ACTION_TX);
    host_event(MAC_SCAN_EVENT_TX);
    event.tx_result = mac_tx_step(&tx, now, NULL, &tx_action);
    H(event.tx_result == MAC_TX_OK && tx_action.kind == MAC_TX_ACTION_RANDOM);
    H(mac_scan_step(&scan, &tx, now, &event, &action) == MAC_SCAN_OK);
    H(action.kind == MAC_SCAN_ACTION_TX);
    memset(&tx_event, 0, sizeof(tx_event));
    tx_event.kind = MAC_TX_EVENT_RANDOM;
    tx_event.generation = tx.generation; tx_event.stamp = now;
    host_event(MAC_SCAN_EVENT_TX);
    event.tx_result = mac_tx_step(&tx, now, &tx_event, &tx_action);
    H(event.tx_result == MAC_TX_OK && tx_action.kind == MAC_TX_ACTION_ATTEMPT);
    H(mac_scan_step(&scan, &tx, now, &event, &action) == MAC_SCAN_OK);
    H(action.kind == MAC_SCAN_ACTION_TX && scan.issued);
    now = tx_action.at + 40u; /* No physical completion is supplied. */
    host_event(MAC_SCAN_EVENT_TX);
    completed = event;
    completed.tx_result = mac_tx_step(&tx, now, NULL, &tx_action);
    H(completed.tx_result == MAC_TX_OK && tx_action.kind == MAC_TX_ACTION_NONE);
    H(tx.phase == MAC_TX_RADIO && tx.last == completed.stamp);
    held_tx = tx;

    /* Deliberately violate ordered delivery: cancellation at t+1 advances the
     * watermark before the completed result at t. Unlike target scenario5,
     * these timestamps differ. Never retimestamp or repeat the completed call.
     */
    now++;
    host_event(MAC_SCAN_EVENT_CANCEL);
    H(mac_scan_step(&scan, &tx, now, &event, &action) == MAC_SCAN_OK);
    H(scan.reason == MAC_SCAN_CANCELLED && scan.stopping && scan.phase == MAC_SCAN_TX);
    H(scan.last == now && scan.stop_steps == MAC_SCAN_STOP_STEPS);
    H(action.kind == MAC_SCAN_ACTION_NONE && mac_scan_release(&scan, &tx) == MAC_SCAN_STATE);
    H(mac_scan_step(&scan, &tx, now, &completed, &action) == MAC_SCAN_OK);
    H(scan.phase == MAC_SCAN_TX && scan.issued && scan.token == completed.token);
    H(scan.stop_steps == MAC_SCAN_STOP_STEPS - 1u && !scan.cleanup_error);
    H(action.kind == MAC_SCAN_ACTION_NONE && mac_scan_release(&scan, &tx) == MAC_SCAN_STATE);

    /* Freeze time: the existing independent cleanup work cap must retain a
     * fault, not emit a new pump/restoration grant or silently release either
     * owner. No mac_tx_step is called again without a grant.
     */
    for (polls = 0; polls < MAC_SCAN_STOP_STEPS; polls++) {
        H(mac_scan_step(&scan, &tx, now, NULL, &action) == MAC_SCAN_OK);
        H(action.kind == MAC_SCAN_ACTION_NONE && scan.owner == &tx && scan.issued);
        H(scan.reason == MAC_SCAN_CANCELLED && scan.unscanned == request.channels && !scan.sent);
        H(memcmp(&held_tx, &tx, sizeof(tx)) == 0);
        H(mac_scan_release(&scan, &tx) == MAC_SCAN_STATE);
        H(scan.phase == (polls + 1u == MAC_SCAN_STOP_STEPS ? MAC_SCAN_FAULT : MAC_SCAN_TX));
    }
    H(scan.cleanup_error == MAC_SCAN_CLEANUP_FAILED && scan.uncertain && !scan.stop_steps);
    host_event(MAC_SCAN_EVENT_RESTORED); event.state = request.saved;
    H(mac_scan_step(&scan, &tx, now, &event, &action) == MAC_SCAN_OK);
    H(scan.phase == MAC_SCAN_FAULT && scan.reason == MAC_SCAN_CANCELLED);
    H(scan.cleanup_error == MAC_SCAN_CLEANUP_FAILED && scan.owner == &tx);
    H(memcmp(&held_tx, &tx, sizeof(tx)) == 0);
    H(mac_scan_release(&scan, &tx) == MAC_SCAN_STATE);
    H(mac_scan_start(&scan, &tx, &request, now) == MAC_SCAN_STATE);
    return 0;
}

static uint16_t host_bounds(void)
{
    mac_tx_t saved_tx;
    nwk_candidate_t *output;
    uint8_t *input;
    uint8_t maximum[88];
    const uint8_t *frame;
    unsigned n, m, size, field;
    mac_scan_result_t expected;
    uint32_t generation;
    /* Every duration byte and every channel-mask bit, including preserved
     * transmitter state and scan output on invalid admission.
     */
    for (n = 0; n < 256; n++) {
        now = 0; HC(host_start());
        request.duration = (uint8_t)n;
        saved = scan; saved_tx = tx;
        expected = n <= 14 ? MAC_SCAN_OK : MAC_SCAN_INVALID;
        H(mac_scan_start(&scan, &tx, &request, now) == expected);
        H(memcmp(&tx, &saved_tx, sizeof(tx)) == 0);
        if (expected == MAC_SCAN_OK) {
            H(scan.dwell == UINT32_C(960) * ((UINT32_C(1) << n) + 1u));
            HC(host_finish());
        } else H(memcmp(&scan, &saved, sizeof(scan)) == 0);
    }
    for (n = 0; n < 32; n++) {
        HC(host_start()); request.channels = UINT32_C(1) << n;
        saved = scan; saved_tx = tx;
        expected = n >= 11 && n <= 26 ? MAC_SCAN_OK : MAC_SCAN_INVALID;
        H(mac_scan_start(&scan, &tx, &request, now) == expected);
        H(memcmp(&tx, &saved_tx, sizeof(tx)) == 0);
        if (expected == MAC_SCAN_OK) {
            H(scan.channel == n); HC(host_finish());
        } else H(memcmp(&scan, &saved, sizeof(scan)) == 0);
    }
    for (n = 0; n < 15; n++) {
        HC(host_start()); saved = scan; saved_tx = tx;
        expected = MAC_SCAN_INVALID;
        if (n == 0) request.channels = 0;
        if (n == 1) request.channels = UINT32_MAX;
        if (n == 2) request.work = 0;
        if (n == 3) request.work = MAC_SCAN_MAX_WORK + 1u;
        if (n == 4) request.lifetime = 0;
        if (n == 5) request.lifetime = MAC_SCAN_MAX_LIFETIME + 1u;
        if (n == 6) request.saved.channel = 10;
        if (n == 7) request.saved.channel = 27;
        if (n == 8) request.saved.filter = 2;
        if (n == 9) request.saved.rx_on = 2;
        if (n == 10) { tx.generation = UINT32_MAX; expected = MAC_SCAN_EXHAUSTED; }
        if (n == 11) { scan.generation = UINT32_MAX; expected = MAC_SCAN_EXHAUSTED; }
        if (n == 12) { scan.version = 0; expected = MAC_SCAN_STATE; }
        if (n == 13) { tx.phase = MAC_TX_FAULT; expected = MAC_SCAN_STATE; }
        if (n == 14) now = MAC_TX_HALF;
        saved = scan; saved_tx = tx;
        H(mac_scan_start(&scan, &tx, &request, now) == expected);
        H(memcmp(&saved, &scan, sizeof(scan)) == 0 && memcmp(&saved_tx, &tx, sizeof(tx)) == 0);
        now = 0;
    }
    HC(host_start()); saved = scan;
    H(mac_scan_start(NULL, &tx, &request, now) == MAC_SCAN_INVALID);
    H(mac_scan_start(&scan, NULL, &request, now) == MAC_SCAN_INVALID);
    H(mac_scan_start(&scan, &tx, NULL, now) == MAC_SCAN_INVALID);
    H(memcmp(&saved, &scan, sizeof(scan)) == 0);
    H(mac_scan_start(&scan, &tx, &request, now) == MAC_SCAN_OK);
    saved = scan; saved_tx = tx;
    H(mac_scan_start(&scan, &tx, &request, now) == MAC_SCAN_STATE);
    H(memcmp(&saved, &scan, sizeof(scan)) == 0);
    H(mac_scan_get(&scan, 0, &entry) == MAC_SCAN_STATE);
    H(mac_scan_get(NULL, 0, &entry) == MAC_SCAN_INVALID);
    H(mac_scan_get(&scan, 0, NULL) == MAC_SCAN_INVALID);
    H(mac_scan_release(NULL, &tx) == MAC_SCAN_INVALID);
    H(mac_scan_release(&scan, NULL) == MAC_SCAN_INVALID);
    memset(&action, 0xc7, sizeof(action)); before = action;
    H(mac_scan_step(NULL, &tx, now, NULL, &action) == MAC_SCAN_INVALID);
    H(mac_scan_step(&scan, NULL, now, NULL, &action) == MAC_SCAN_INVALID);
    H(mac_scan_step(&scan, &tx, now, NULL, NULL) == MAC_SCAN_INVALID);
    H(mac_scan_step(&scan, &saved_tx, now, NULL, &action) == MAC_SCAN_STATE);
    H(memcmp(&before, &action, sizeof(action)) == 0);
    for (n = 0; n < 256; n++) {
        if (n >= 1 && n <= 8) continue;
        host_event((uint8_t)n);
        H(mac_scan_step(&scan, &tx, now, &event, &action) == MAC_SCAN_INVALID);
        H(memcmp(&before, &action, sizeof(action)) == 0);
        H(memcmp(&saved, &scan, sizeof(scan)) == 0);
    }
    HC(host_finish());

    /* Exact-size FCS-free bodies: short/no pending, extended/mixed pending and
     * extended/seven pending. The complete real decoder chain sees each input.
     */
    memcpy(maximum, long_beacon, 13);
    maximum[13] = 0xff; maximum[14] = 0xcf; maximum[15] = 0; maximum[16] = 0x70;
    for (n = 17; n < 73; n++) maximum[n] = (uint8_t)n;
    memcpy(maximum + 73, beacon + 11, 15);
    for (m = 0; m < 3; m++) {
        frame = m == 0 ? beacon : m == 1 ? long_beacon : maximum;
        size = m == 0 ? sizeof(beacon) : m == 1 ? sizeof(long_beacon) : sizeof(maximum);
        for (n = 0; n <= 126; n++) {
            HC(host_open());
            input = malloc(n ? n : 1u); H(input != NULL);
            memset(input, 0x69, n); memcpy(input, frame, n < size ? n : size);
            host_event(MAC_SCAN_EVENT_BEACON);
            event.body = input; event.length = (uint16_t)n; event.crc_valid = 1;
            H(mac_scan_step(&scan, &tx, now, &event, &action) == MAC_SCAN_OK);
            H(action.observed);
            H((action.candidate_result == NWK_CANDIDATES_ADDED) == (n == size));
            memset(input, 0xc7, n); free(input);
            host_event(MAC_SCAN_EVENT_CANCEL);
            H(mac_scan_step(&scan, &tx, now, &event, &action) == MAC_SCAN_OK);
            host_event(MAC_SCAN_EVENT_RESTORED); event.state = request.saved;
            H(mac_scan_step(&scan, &tx, now, &event, &action) == MAC_SCAN_OK);
            output = malloc(sizeof(*output)); H(output != NULL);
            memset(output, 0xc7, sizeof(*output));
            if (n == size) {
                H(mac_scan_get(&scan, 0, output) == MAC_SCAN_OK);
                H(output->network.stack_profile == 2 && output->channel == 11);
            } else {
                H(mac_scan_get(&scan, 0, output) == MAC_SCAN_BAD_INDEX);
                for (field = 0; field < sizeof(*output); field++)
                    H(((uint8_t *)output)[field] == 0xc7);
            }
            free(output);
            H(mac_scan_release(&scan, &tx) == MAC_SCAN_OK);
        }
    }
    HC(host_open());
    for (n = 0; n < 256; n++) {
        host_event(MAC_SCAN_EVENT_BEACON);
        event.body = beacon; event.length = sizeof(beacon); event.crc_valid = (uint8_t)n;
        before = action; saved = scan;
        H(mac_scan_step(&scan, &tx, now, &event, &action)
          == (n < 2 ? MAC_SCAN_OK : MAC_SCAN_INVALID));
        if (n > 1) {
            H(memcmp(&before, &action, sizeof(action)) == 0);
            H(memcmp(&saved, &scan, sizeof(scan)) == 0);
        }
    }
    for (n = 0; n < 5; n++) {
        host_event(MAC_SCAN_EVENT_BEACON);
        event.body = beacon; event.length = sizeof(beacon); event.crc_valid = 1;
        if (n == 0) event.generation--;
        if (n == 1) event.token--;
        if (n == 2) event.stamp--;
        if (n == 3) event.stamp++;
        if (n == 4) event.state.channel = 26;
        generation = scan.candidates.count;
        H(mac_scan_step(&scan, &tx, now, &event, &action) == MAC_SCAN_OK);
        H(!action.observed && scan.candidates.count == generation);
    }
    now = scan.window_end;
    host_event(MAC_SCAN_EVENT_BEACON);
    event.body = beacon; event.length = sizeof(beacon); event.crc_valid = 1;
    H(mac_scan_step(&scan, &tx, now, &event, &action) == MAC_SCAN_OK);
    H(!action.observed && scan.unscanned); /* End-exclusive; no close confirmation yet. */
    HC(host_finish());
    /* Actual issued CONFIG/RECEIVE/window-close confirmations with each owned
     * state field wrong. They must not count a channel or bypass restoration.
     */
    for (m = 0; m < 3; m++) {
        for (field = 0; field < 4; field++) {
            if (m == 0) {
                now = 0; HC(host_start());
                H(mac_scan_start(&scan, &tx, &request, now) == MAC_SCAN_OK);
                H(mac_scan_step(&scan, &tx, now, NULL, &action) == MAC_SCAN_OK);
                host_event(MAC_SCAN_EVENT_CONFIGURED);
            } else if (m == 1) {
                HC(host_to_receive());
                host_event(MAC_SCAN_EVENT_OPENED); event.state.rx_on = 1;
            } else {
                HC(host_open()); now = scan.window_end;
                host_event(MAC_SCAN_EVENT_CLOSED);
            }
            if (field == 0) event.state.pan = 0x1234;
            if (field == 1) event.state.channel = 26;
            if (field == 2) event.state.filter = MAC_SCAN_FILTER_NORMAL;
            if (field == 3) event.state.rx_on ^= 1;
            H(mac_scan_step(&scan, &tx, now, &event, &action) == MAC_SCAN_OK);
            H(action.kind == MAC_SCAN_ACTION_RESTORE && scan.reason == MAC_SCAN_ADAPTER_ERROR);
            H(scan.unscanned == request.channels && scan.uncertain);
            host_event(MAC_SCAN_EVENT_RESTORED); event.state = request.saved;
            H(mac_scan_step(&scan, &tx, now, &event, &action) == MAC_SCAN_OK);
            H(scan.phase == MAC_SCAN_DONE && scan.reason == MAC_SCAN_ADAPTER_ERROR);
            H(mac_scan_release(&scan, &tx) == MAC_SCAN_OK);
        }
    }
    HC(host_late_pump_cancellation());
    return 0;
}
#endif
