/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 * Compact-profile-only native/MMIO regression. Never target firmware.
 */
#if !defined(CC2530_HOST_TEST) || !defined(CC2530_MAC_LINK_RAM)
#error This test requires the explicit native compact LINK profile
#endif
#include "mac_link_driver.h"
#include "mac_link_ram_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned ram_checks, ram_returns;
#define RAM_CHECK(c) do { ram_checks++; if (!(c)) { \
    fprintf(stderr, "Compact LINK line%u: %s\n", (unsigned)__LINE__, #c); exit(1); \
} } while (0)

static void ram_clean(void)
{
    RAM_CHECK(mac_link_ram_join_clean());
    RAM_CHECK(mac_link_ram_poll_clean());
    ram_returns++;
}

/* Run every existing real radio/crypto E2E case unchanged, but additionally
 * check that each returning driver call has dropped the private controller
 * views, including retained-fault/backpressure/cancel/retry paths.
 */
static mac_link_driver_result_t ram_checked_driver(mac_link_driver_t *ctx)
{
    mac_link_driver_result_t result = mac_link_driver_step(ctx);
    ram_clean();
    return result;
}
#define mac_link_driver_step ram_checked_driver
#define MAC_LINK_E2E_MAIN ram_e2e_main
#include "test_mac_link_e2e.c"
#undef MAC_LINK_E2E_MAIN
#undef mac_link_driver_step

static const mac_join_request_t ram_request = {
    {1, 300, 100000, 256, 0x1234, 15, 3, 3,
     {1,2,3,4,5,6,7,8}, {17,18,19,20,21,22,23,24}},
    {0xffff, 11, 0, 0}, 2, 0x88, MAC_RX_IEEE2006
};

static void ram_admission(void)
{
    mac_join_t j, original_j;
    mac_poll_t p, original_p;
    mac_tx_interval_t tx, original_tx;
    mac_join_request_t request;
    mac_join_action_t ja, original_ja;
    mac_poll_action_t pa, original_pa;
    mac_join_event_t event;
    unsigned i;
    RAM_CHECK(mac_join_init(&j, 100) == MAC_JOIN_OK);
    RAM_CHECK(mac_poll_init(&p) == MAC_POLL_OK);
    RAM_CHECK(mac_tx_interval_init(&tx, 9, 100) == MAC_TX_OK);
    ram_clean();
    original_j = j; original_p = p; original_tx = tx;
    memset(&ja, 0xa5, sizeof(ja)); original_ja = ja;
    memset(&pa, 0x5a, sizeof(pa)); original_pa = pa;
    for (i = 0; i < 7; i++) {
        request = ram_request;
        if (i == 0) request.extraction.epoch = 0;
        if (i == 1) request.extraction.frame_wait = 0;
        if (i == 2) request.extraction.lifetime = MAC_POLL_MAX_TIME + 1UL;
        if (i == 3) request.extraction.work = 0;
        if (i == 4) request.extraction.pan = 0xffff;
        if (i == 5) request.extraction.channel = 27;
        if (i == 6) request.extraction.local_mode = 0;
        RAM_CHECK(mac_join_start(&j, &tx, &request, 100) != MAC_JOIN_OK);
        ram_clean();
        RAM_CHECK(mac_poll_start(&p, &tx, &request.extraction, 100) != MAC_POLL_OK);
        ram_clean();
        RAM_CHECK(!memcmp(&j, &original_j, sizeof(j)));
        RAM_CHECK(!memcmp(&p, &original_p, sizeof(p)));
        RAM_CHECK(!memcmp(&tx, &original_tx, sizeof(tx)));
    }
    RAM_CHECK(mac_join_start(&j, &tx, &ram_request, 99) == MAC_JOIN_INVALID);
    RAM_CHECK(mac_poll_start(&p, &tx, &ram_request.extraction, 99) == MAC_POLL_INVALID);
    ram_clean();
    RAM_CHECK(!memcmp(&j, &original_j, sizeof(j)));
    RAM_CHECK(!memcmp(&p, &original_p, sizeof(p)));
    j.generation = UINT32_MAX; original_j = j;
    p.control.generation = UINT32_MAX; original_p = p;
    RAM_CHECK(mac_join_start(&j, &tx, &ram_request, 100) == MAC_JOIN_LIMIT);
    RAM_CHECK(mac_poll_start(&p, &tx, &ram_request.extraction, 100) == MAC_POLL_LIMIT);
    ram_clean();
    RAM_CHECK(!memcmp(&j, &original_j, sizeof(j)));
    RAM_CHECK(!memcmp(&p, &original_p, sizeof(p)));
    RAM_CHECK(!memcmp(&tx, &original_tx, sizeof(tx)));

    /* Incompatible subobjects are rejected before any typed dereference/write.
     * Distinct nested BDB objects remain legal; overlap is never a RAM pool. */
    RAM_CHECK(mac_join_start(&j, (mac_tx_interval_t *)&j, &ram_request, 100) == MAC_JOIN_INVALID);
    RAM_CHECK(mac_join_start(&j, &tx, &j.request, 100) == MAC_JOIN_INVALID);
    RAM_CHECK(mac_join_step(&j, &tx, 100, NULL, (mac_join_action_t *)&j) == MAC_JOIN_INVALID);
    RAM_CHECK(mac_join_take(&j, &j.record) == MAC_JOIN_INVALID);
    RAM_CHECK(mac_join_release(&j, (mac_tx_interval_t *)&j) == MAC_JOIN_INVALID);
    RAM_CHECK(mac_poll_start(&p, (mac_tx_interval_t *)&p, &ram_request.extraction, 100) == MAC_POLL_INVALID);
    RAM_CHECK(mac_poll_start(&p, &tx, &p.control.request, 100) == MAC_POLL_INVALID);
    RAM_CHECK(mac_poll_step(&p, &tx, 100, NULL, (mac_poll_action_t *)&p) == MAC_POLL_INVALID);
    RAM_CHECK(mac_poll_take(&p, &p.record) == MAC_POLL_INVALID);
    RAM_CHECK(mac_poll_release(&p, (mac_tx_interval_t *)&p) == MAC_POLL_INVALID);
    ram_clean();
    RAM_CHECK(!memcmp(&j, &original_j, sizeof(j)));
    RAM_CHECK(!memcmp(&p, &original_p, sizeof(p)));
    RAM_CHECK(!memcmp(&tx, &original_tx, sizeof(tx)));

    memset(&event, 0, sizeof(event)); event.kind = 255;
    RAM_CHECK(mac_join_step(&j, &tx, 100, &event, &ja) == MAC_JOIN_INVALID);
    RAM_CHECK(mac_poll_step_rx(&p, &tx, 100, &event, &pa, MAC_RX_IEEE2006) == MAC_POLL_INVALID);
    ram_clean();
    RAM_CHECK(!memcmp(&ja, &original_ja, sizeof(ja)));
    RAM_CHECK(!memcmp(&pa, &original_pa, sizeof(pa)));
    RAM_CHECK(!memcmp(&j, &original_j, sizeof(j)));
    RAM_CHECK(!memcmp(&p, &original_p, sizeof(p)));
    RAM_CHECK(mac_join_init(NULL, 0) == MAC_JOIN_INVALID);
    RAM_CHECK(mac_poll_init(NULL) == MAC_POLL_INVALID);
    RAM_CHECK(mac_join_start(NULL, &tx, &ram_request, 100) == MAC_JOIN_INVALID);
    RAM_CHECK(mac_poll_step(NULL, &tx, 100, NULL, &pa) == MAC_POLL_INVALID);
    ram_clean();
}

static void ram_boundaries(void)
{
    uint8_t bytes[16];
    RAM_CHECK(!link_ram_span(NULL, 1));
    RAM_CHECK(!link_ram_span(bytes, 0));
    RAM_CHECK(!link_ram_span((const void *)(uintptr_t)UINTPTR_MAX, 2));
    RAM_CHECK(link_ram_disjoint(bytes, 8, bytes + 8, 8));
    RAM_CHECK(link_ram_disjoint(bytes + 8, 8, bytes, 8));
    RAM_CHECK(!link_ram_disjoint(bytes, 9, bytes + 8, 8));
    RAM_CHECK(!link_ram_disjoint(bytes + 8, 8, bytes, 9));
    RAM_CHECK(!link_ram_disjoint(bytes, 16, bytes, 16));
}

static void ram_return_lifetimes(void)
{
    mac_join_t j, saved_j;
    mac_poll_t p, saved_p;
    mac_tx_interval_t tx, saved_tx, spare;
    mac_join_action_t ja;
    mac_poll_action_t pa;
    mac_join_event_t event;
    unsigned i;
    RAM_CHECK(mac_join_init(&j, 100) == MAC_JOIN_OK);
    RAM_CHECK(mac_tx_interval_init(&tx, 255, 100) == MAC_TX_OK);
    RAM_CHECK(mac_join_start(&j, &tx, &ram_request, 100) == MAC_JOIN_OK);
    RAM_CHECK(mac_join_step(&j, &tx, 100, NULL, &ja) == MAC_JOIN_OK);
    saved_j = j; saved_tx = tx;
    ram_clean();
    /* Repeated unrelated admitted/rejected calls exercise the actual returning
     * union and borrowed-control boundary without changing j's retained grant. */
    RAM_CHECK(mac_poll_init(&p) == MAC_POLL_OK);
    RAM_CHECK(mac_tx_interval_init(&spare, 0, 100) == MAC_TX_OK);
    RAM_CHECK(mac_poll_start(&p, &spare, &ram_request.extraction, 100) == MAC_POLL_OK);
    RAM_CHECK(p.control.outgoing_length == 22 && p.control.outgoing[21] == MAC_COMMAND_DATA_REQUEST);
    for (i = 0; i < 4; i++) {
        RAM_CHECK(mac_poll_step(&p, &spare, 100, NULL, &pa) == MAC_POLL_OK);
        ram_clean();
        saved_p = p;
        RAM_CHECK(mac_poll_start(&p, &spare, &ram_request.extraction, 100) == MAC_POLL_STATE);
        RAM_CHECK(!memcmp(&p, &saved_p, sizeof(p)));
        RAM_CHECK(!memcmp(&j, &saved_j, sizeof(j)));
        RAM_CHECK(!memcmp(&tx, &saved_tx, sizeof(tx)));
        ram_clean();
    }
    memset(&event, 0, sizeof(event));
    event.kind = MAC_JOIN_FAILURE; event.epoch = j.record.epoch;
    event.generation = j.generation; event.stamp = 100;
    RAM_CHECK(mac_join_step(&j, &tx, 100, &event, &ja) == MAC_JOIN_OK);
    RAM_CHECK(j.record.reason == MAC_JOIN_ADAPTER_ERROR);
    saved_j = j; saved_tx = tx;
    ram_clean();
    /* A rejected subsequent call cannot roll back or overwrite a real fault. */
    event.kind = 255;
    RAM_CHECK(mac_join_step(&j, &tx, 100, &event, &ja) == MAC_JOIN_INVALID);
    RAM_CHECK(!memcmp(&j, &saved_j, sizeof(j)));
    RAM_CHECK(!memcmp(&tx, &saved_tx, sizeof(tx)));
    ram_clean();
}

int main(void)
{
    ram_boundaries();
    ram_admission();
    ram_return_lifetimes();
    RAM_CHECK(ram_e2e_main() == 0);
    printf("Compact LINK: %u additional checks, %u public-return cleanup observations PASS.\n",
           ram_checks, ram_returns);
    return 0;
}
