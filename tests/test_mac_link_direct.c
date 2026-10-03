/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 * Native DIRECT staging contract regression, never target firmware.
 */
#if !defined(CC2530_HOST_TEST) || !defined(CC2530_MAC_LINK_DIRECT)
#error This test requires the explicit native DIRECT profile
#endif
#include "nwk_aps_internal.h"
#include "mac_link_workspace_internal.h"
#include "mac_tx_interval.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Native stand-ins for the SDCC helper homes named by the real radio/child
 * alias guards; this test never runs the radio path. */
uint8_t __memcpy_PARM_2[3], _mullong_PARM_2[4];
static unsigned checks;
#define CHECK(c) do { checks++; if (!(c)) { \
    fprintf(stderr, "DIRECT line %u: %s\n", (unsigned)__LINE__, #c); exit(1); } } while (0)

#define START 1000u
#define LIFE 500000UL
#define WORK 256u
/* DATA, ACK request, PAN compression, short/short: PAN 1234, 0000 <- 5678. */
static const uint8_t data[] = {
    0x61, 0x88, 0xee, 0x34, 0x12, 0x00, 0x00, 0x78, 0x56,
    0x48, 0x02, 0x00, 0x00, 0x78, 0x56, 0x1e, 0x2a, 0x11, 0x22, 0x33
};
static mac_tx_interval_t tx, twin, other;
static nwk_aps_t nwk;
static uint8_t engine_tail[sizeof(mac_tx_t) - offsetof(mac_tx_t, last)];

static void fresh(mac_tx_interval_t *t, uint8_t dsn)
{
    memset(t, 0, sizeof(*t));
    CHECK(mac_tx_interval_init(t, dsn, START) == MAC_TX_OK);
    memset(&t->tx_lower, 0xa5, sizeof(t->tx_lower));
    memset(&t->tx_upper, 0x5a, sizeof(t->tx_upper));
}

static void snapshot(const mac_tx_interval_t *t)
{
    memcpy(engine_tail, (const uint8_t *)&t->engine + offsetof(mac_tx_t, last), sizeof(engine_tail));
}

/* Rejections leave control, timestamps, generation and interval bounds. */
static void unchanged(const mac_tx_interval_t *t)
{
    CHECK(!memcmp(engine_tail, (const uint8_t *)&t->engine + offsetof(mac_tx_t, last), sizeof(engine_tail)));
    CHECK(t->tx_lower.symbols == 0xa5a5a5a5UL && t->tx_upper.symbols == 0x5a5a5a5aUL);
}

static uint8_t stage_bytes(mac_tx_interval_t *t, const uint8_t *body, uint8_t length)
{
    uint8_t *frame = mac_tx_interval_stage(t);
    CHECK(frame == t->engine.frame);
    memcpy(frame, body, length);
    return length;
}

static void test_equivalent_admission(void)
{
    fresh(&tx, 0x40);
    fresh(&twin, 0x40);
    CHECK(mac_tx_interval_submit(&twin, data, sizeof(data), START + 1u, LIFE, WORK) == MAC_TX_OK);
    stage_bytes(&tx, data, sizeof(data));
    CHECK(mac_tx_interval_submit_staged(&tx, sizeof(data), START + 1u, LIFE, WORK) == MAC_TX_OK);
    /* Byte-identical owner, including frame, DSN, generation and cleared bounds. */
    CHECK(!memcmp(&tx, &twin, sizeof(tx)));
    CHECK(tx.engine.phase == MAC_TX_DRAW && tx.engine.frame[2] == 0x40 && tx.engine.next_dsn == 0x41);
    CHECK(tx.engine.length == sizeof(data) && tx.engine.ack_requested && tx.engine.generation == 1u);
    CHECK(tx.tx_lower.symbols == 0 && tx.tx_upper.symbols == 0);
    /* Busy: no loan is issued and admission is FULL, not a replacement. */
    snapshot(&tx);
    CHECK(mac_tx_interval_stage(&tx) == NULL);
    CHECK(mac_tx_interval_submit_staged(&tx, sizeof(data), START + 2u, LIFE, WORK) == MAC_TX_FULL);
    CHECK(!memcmp(engine_tail, (const uint8_t *)&tx.engine + offsetof(mac_tx_t, last), sizeof(engine_tail)));
    CHECK(!memcmp(tx.engine.frame, twin.engine.frame, sizeof(tx.engine.frame)));
}

static void test_loan_state(void)
{
    uint8_t n;
    /* No loan at all: this object was never staged. */
    fresh(&other, 7);
    fresh(&tx, 9);
    memcpy(other.engine.frame, data, sizeof(data));
    snapshot(&other);
    CHECK(mac_tx_interval_submit_staged(&other, sizeof(data), START + 1u, LIFE, WORK) == MAC_TX_STATE);
    unchanged(&other);
    /* A loan belongs to one owner; another owner's call leaves it intact. */
    n = stage_bytes(&tx, data, sizeof(data));
    CHECK(mac_tx_interval_submit_staged(&other, n, START + 1u, LIFE, WORK) == MAC_TX_STATE);
    unchanged(&other);
    CHECK(mac_tx_interval_submit_staged(&tx, n, START + 1u, LIFE, WORK) == MAC_TX_OK);
    /* Single use: the consumed loan cannot authorize a later IDLE owner. */
    fresh(&other, 7);
    n = stage_bytes(&other, data, sizeof(data));
    other.engine.generation++;          /* an intervening admission */
    snapshot(&other);
    CHECK(mac_tx_interval_submit_staged(&other, n, START + 1u, LIFE, WORK) == MAC_TX_STATE);
    unchanged(&other);
    n = stage_bytes(&other, data, sizeof(data));
    CHECK(mac_tx_interval_submit_staged(&other, n, START + 1u, LIFE, WORK) == MAC_TX_OK);
    CHECK(other.engine.generation == 2u);
    fresh(&other, 7);
    memcpy(other.engine.frame, data, sizeof(data));
    CHECK(mac_tx_interval_submit_staged(&other, n, START + 1u, LIFE, WORK) == MAC_TX_STATE);
}

static void test_arguments_keep_loan(void)
{
    uint8_t n;
    fresh(&tx, 3);
    n = stage_bytes(&tx, data, sizeof(data));
    snapshot(&tx);
    CHECK(mac_tx_interval_submit_staged(NULL, n, START + 1u, LIFE, WORK) == MAC_TX_INVALID);
    CHECK(mac_tx_interval_submit_staged(&tx, 0, START + 1u, LIFE, WORK) == MAC_TX_INVALID);
    CHECK(mac_tx_interval_submit_staged(&tx, MAC_FRAME_MAX_BODY + 1u, START + 1u, LIFE, WORK) == MAC_TX_INVALID);
    CHECK(mac_tx_interval_submit_staged(&tx, 0xffffu, START + 1u, LIFE, WORK) == MAC_TX_INVALID);
    CHECK(mac_tx_interval_submit_staged(&tx, n, START + 1u, 0, WORK) == MAC_TX_INVALID);
    CHECK(mac_tx_interval_submit_staged(&tx, n, START + 1u, MAC_TX_HALF, WORK) == MAC_TX_INVALID);
    CHECK(mac_tx_interval_submit_staged(&tx, n, START + 1u, LIFE, 0) == MAC_TX_INVALID);
    unchanged(&tx);
    CHECK(!memcmp(tx.engine.frame, data, sizeof(data)));
    /* Protected arena storage is neither loaned nor admitted. */
    CHECK(mac_tx_interval_stage((mac_tx_interval_t *)(void *)&link_work_arena) == NULL);
    CHECK(mac_tx_interval_submit_staged((mac_tx_interval_t *)(void *)&link_work_arena,
        n, START + 1u, LIFE, WORK) == MAC_TX_INVALID);
    CHECK(link_work_root());
    CHECK(mac_tx_interval_submit_staged(&tx, n, START + 1u, LIFE, WORK) == MAC_TX_OK);
}

static void reject(const uint8_t *body, uint8_t length, uint32_t now, mac_tx_result_t expected)
{
    fresh(&tx, 0x21);
    stage_bytes(&tx, body, length);
    snapshot(&tx);
    CHECK(mac_tx_interval_submit_staged(&tx, length, now, LIFE, WORK) == expected);
    unchanged(&tx);
    CHECK(tx.engine.phase == MAC_TX_IDLE);
    /* The loan was consumed; even a now-valid frame needs a fresh stage. */
    memcpy(tx.engine.frame, data, sizeof(data));
    CHECK(mac_tx_interval_submit_staged(&tx, sizeof(data), START + 1u, LIFE, WORK) == MAC_TX_STATE);
    unchanged(&tx);
    CHECK(link_work_root());
}

static void test_admission_rules(void)
{
    static const uint8_t beacon_request[] = {0x03, 0x08, 0x01, 0xff, 0xff, 0xff, 0xff, 0x07};
    /* Otherwise DATA-valid addressing: only the frame type can reject it. */
    static const uint8_t data_request[] = {0x63, 0x88, 0x01, 0x34, 0x12, 0x00, 0x00, 0x78, 0x56, 0x04};
    uint8_t body[sizeof(data)];
    memcpy(body, data, sizeof(data)); body[0] |= 0x10;             /* Pending */
    reject(body, sizeof(body), START + 1u, MAC_TX_UNSUPPORTED);
    /* Commands are admitted only by the ordinary copying submit. */
    reject(beacon_request, sizeof(beacon_request), START + 1u, MAC_TX_UNSUPPORTED);
    reject(data_request, sizeof(data_request), START + 1u, MAC_TX_UNSUPPORTED);
    fresh(&twin, 1);
    CHECK(mac_tx_interval_submit(&twin, beacon_request, sizeof(beacon_request),
        START + 1u, LIFE, WORK) == MAC_TX_OK);
    fresh(&twin, 1);
    CHECK(mac_tx_interval_submit(&twin, data_request, sizeof(data_request),
        START + 1u, LIFE, WORK) == MAC_TX_OK);
    memcpy(body, data, sizeof(data)); body[3] = body[4] = 0xff;    /* PAN FFFF */
    reject(body, sizeof(body), START + 1u, MAC_TX_UNSUPPORTED);
    memcpy(body, data, sizeof(data)); body[5] = 0xfe; body[6] = 0xff; /* dest FFFE */
    reject(body, sizeof(body), START + 1u, MAC_TX_UNSUPPORTED);
    memcpy(body, data, sizeof(data)); body[7] = 0xfe; body[8] = 0xff; /* source FFFE */
    reject(body, sizeof(body), START + 1u, MAC_TX_UNSUPPORTED);
    reject(data, 8, START + 1u, MAC_TX_UNSUPPORTED);                  /* truncated */
    /* Clock order and generation exhaustion follow the ordinary submit. */
    reject(data, sizeof(data), START + MAC_TX_HALF, MAC_TX_INVALID);
    fresh(&tx, 0x21);
    tx.engine.generation = UINT32_MAX;
    stage_bytes(&tx, data, sizeof(data));
    snapshot(&tx);
    CHECK(mac_tx_interval_submit_staged(&tx, sizeof(data), START + 1u, LIFE, WORK)
          == MAC_TX_GENERATION_EXHAUSTED);
    unchanged(&tx);
}

static void test_nwk_busy_owner(void)
{
    static nwk_aps_t before;
    static mac_tx_interval_t owner_before;
    fresh(&tx, 0x40);
    stage_bytes(&tx, data, sizeof(data));
    CHECK(mac_tx_interval_submit_staged(&tx, sizeof(data), START + 1u, LIFE, WORK) == MAC_TX_OK);
    memset(&nwk, 0x3c, sizeof(nwk));
    nwk.owner = &tx;
    before = nwk;
    owner_before = tx;
    /* No NWK sequence, security, BTR or frame mutation without an IDLE loan. */
    CHECK(nwk_aps_transmit(&nwk, 0, START + 2u) == NWK_APS_STATE);
    CHECK(nwk_aps_transmit(&nwk, 1, START + 2u) == NWK_APS_STATE);
    CHECK(!memcmp(&nwk, &before, sizeof(nwk)));
    CHECK(!memcmp(&tx, &owner_before, sizeof(tx)));
    CHECK(link_work_root());
    /* Arena-backed context is rejected before any loan or mutation. */
    CHECK(nwk_aps_transmit((nwk_aps_t *)(void *)&link_work_arena, 0, START + 2u) == NWK_APS_ARGUMENT);
}

static void test_nwk_security_failure(void)
{
    /* Unprovisioned keys: the real security path refuses; the IDLE owner's
     * control, generation and bounds stay untouched and nothing is admitted. */
    fresh(&tx, 0x40);
    memset(&nwk, 0, sizeof(nwk));
    nwk.owner = &tx;
    snapshot(&tx);
    CHECK(nwk_aps_transmit(&nwk, 0, START + 2u) == NWK_APS_SECURITY);
    unchanged(&tx);
    CHECK(tx.engine.phase == MAC_TX_IDLE && !nwk.active);
    CHECK(link_work_root());
}

int main(void)
{
    test_equivalent_admission();
    test_loan_state();
    test_arguments_keep_loan();
    test_admission_rules();
    test_nwk_busy_owner();
    test_nwk_security_failure();
    printf("DIRECT staging host tests: 6 cases, %u checks passed\n", checks);
    return 0;
}
