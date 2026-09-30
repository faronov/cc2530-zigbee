/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 * Native DIRECT key-status lifetime regression, never target firmware.
 *
 * Linked with GNU ld --wrap into the unchanged E2E/compact/UPPER corpus. Each
 * cross-module entry that can refresh the shared nwk_aps_keys snapshot first
 * overwrites it with a changing pattern, as a refresh by another reader would.
 * The corpus must still pass: no BDB, NWK/APS or ZDO read may rely on the
 * snapshot across such a call. Same-module calls are covered by the static
 * window review, not by this harness.
 */
#if !defined(CC2530_HOST_TEST) || !defined(CC2530_MAC_LINK_DIRECT)
#error This harness requires the explicit native DIRECT profile
#endif
#include "bdb_join.h"
#include "mac_link_driver.h"
#include "nwk_aps_internal.h"
#include "security_keys.h"
#include "zdo_runtime.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static unsigned long poisoned;

static void poison(void)
{
    memset(&nwk_aps_keys, (int)(0x5au ^ (uint8_t)(poisoned++ * 29u)), sizeof(nwk_aps_keys));
}

__attribute__((destructor)) static void poison_report(void)
{
    fprintf(stderr, "DIRECT key-status snapshot poisoned at %lu refresher entries\n", poisoned);
    if (poisoned < 1000u)
        _exit(1);
}

#define WRAP(ret, name, params, args) \
    ret __real_##name params; \
    ret __wrap_##name params; \
    ret __wrap_##name params { poison(); return __real_##name args; }

WRAP(security_keys_result_t, security_keys_status, (security_keys_status_t * volatile output), (output))
WRAP(bdb_join_result_t, bdb_join_start, (bdb_join_t * volatile ctx, mac_tx_interval_t * volatile owner,
     const bdb_join_config_t * volatile config, volatile uint32_t now), (ctx, owner, config, now))
WRAP(bdb_join_result_t, bdb_join_step, (bdb_join_t * volatile ctx, volatile uint32_t now,
     const bdb_join_event_t * volatile event, bdb_join_action_t * volatile action),
     (ctx, now, event, action))
WRAP(bdb_join_result_t, bdb_join_receive, (bdb_join_t * volatile ctx, const uint8_t * volatile body,
     volatile uint16_t length, volatile uint8_t crc_valid, volatile uint32_t now),
     (ctx, body, length, crc_valid, now))
WRAP(mac_link_driver_result_t, mac_link_driver_step, (mac_link_driver_t * volatile ctx), (ctx))
WRAP(nwk_aps_result_t, nwk_aps_step, (nwk_aps_t * volatile ctx, volatile uint32_t now,
     const mac_tx_interval_event_t * volatile event, mac_tx_interval_action_t * volatile action),
     (ctx, now, event, action))
WRAP(nwk_aps_result_t, nwk_aps_transmit, (nwk_aps_t * volatile ctx, volatile uint8_t acknowledgment,
     volatile uint32_t now), (ctx, acknowledgment, now))
WRAP(zdo_runtime_result_t, zdo_runtime_step, (zdo_runtime_t * volatile ctx, nwk_aps_t * volatile transport,
     volatile uint32_t now), (ctx, transport, now))
WRAP(zdo_runtime_result_t, zdo_runtime_request, (zdo_runtime_t * volatile ctx, nwk_aps_t * volatile transport,
     volatile uint8_t which, volatile uint32_t now), (ctx, transport, which, now))
WRAP(zdo_runtime_result_t, zdo_runtime_broadcast, (zdo_runtime_t * volatile ctx, nwk_aps_t * volatile transport,
     volatile uint8_t which, volatile uint32_t now), (ctx, transport, which, now))

void __real_nwk_aps_complete(nwk_aps_t * volatile ctx, volatile uint8_t result);
void __wrap_nwk_aps_complete(nwk_aps_t * volatile ctx, volatile uint8_t result);
void __wrap_nwk_aps_complete(nwk_aps_t * volatile ctx, volatile uint8_t result)
{
    poison();
    __real_nwk_aps_complete(ctx, result);
}
