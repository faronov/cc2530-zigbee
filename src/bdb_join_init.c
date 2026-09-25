/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "bdb_join_internal.h"
#include <stddef.h>
#include <string.h>

bdb_join_result_t bdb_join_init(bdb_join_t BDB_JOIN_RAM * volatile ctx, volatile uint32_t now)
{
    if (!ctx) return BDB_JOIN_ARGUMENT;
    memset(ctx, 0, sizeof(*ctx));
    ctx->version = BDB_JOIN_VERSION; ctx->last = ctx->work_at = now;
    return BDB_JOIN_OK;
}

/* No owner reset, admission, counter/NV operation or retained borrowed span.
 * At start this validates into unused storage before any scan lease exists.
 * The later call is legal only after successful association take AND release.
 * Both initializations finish before any runtime API can be admitted.
 */
bdb_join_result_t bdb_join_runtime_init(bdb_join_t BDB_JOIN_RAM * volatile ctx,
    mac_tx_t BDB_JOIN_RAM * volatile owner, const bdb_join_config_t * volatile config,
    volatile uint32_t now)
{
    ctx->workspace = BDB_JOIN_WORK_NONE;
    if (nwk_aps_init(&ctx->work.runtime.transport, owner, &config->transport, now) != NWK_APS_OK ||
        zdo_runtime_init(&ctx->work.runtime.zdo, &config->descriptor, now) != ZDO_RUNTIME_OK)
        return BDB_JOIN_ARGUMENT;
    ctx->workspace = BDB_JOIN_WORK_RUNTIME;
    return BDB_JOIN_OK;
}

bdb_join_result_t bdb_join_start(bdb_join_t BDB_JOIN_RAM * volatile ctx, mac_tx_t BDB_JOIN_RAM * volatile owner,
    const bdb_join_config_t * volatile config, volatile uint32_t now)
{
    bdb_join_result_t result = bdb_join_advance(ctx, now);
    if (result) return result;
    if (!owner || !config || config->link_cost < 1 || config->link_cost > 3 ||
        !config->association.extraction.epoch || config->descriptor.logical_type != 2 ||
        config->descriptor.mac_capability != config->association.capability ||
        config->scan.saved.pan != config->association.saved.pan ||
        config->scan.saved.channel != config->association.saved.channel ||
        config->scan.saved.filter != config->association.saved.filter ||
        config->scan.saved.rx_on != config->association.saved.rx_on) return BDB_JOIN_ARGUMENT;
    if (ctx->phase != BDB_JOIN_IDLE || owner->phase != MAC_TX_IDLE) return BDB_JOIN_STATE;
    if (security_keys_status(&bdb_join_keys) != SECURITY_KEYS_OK) return BDB_JOIN_SECURITY;
    if (bdb_join_keys.phase != SECURITY_KEYS_PROVISIONED) return BDB_JOIN_RECOVERY_REQUIRED;
    if (config->scan.channels != (1UL << bdb_join_keys.config.channel) ||
        config->association.extraction.pan != bdb_join_keys.config.pan ||
        config->association.extraction.channel != bdb_join_keys.config.channel ||
        config->association.extraction.local_mode != MAC_ADDRESS_EXTENDED ||
        config->association.extraction.coordinator_mode != MAC_ADDRESS_EXTENDED ||
        memcmp(config->association.extraction.local, bdb_join_keys.config.own_ieee, 8) ||
        memcmp(config->association.extraction.coordinator, bdb_join_keys.config.tc_ieee, 8))
        return BDB_JOIN_ARGUMENT;
    if (bdb_join_runtime_init(ctx, owner, config, now) != BDB_JOIN_OK)
        return BDB_JOIN_ARGUMENT;
    ctx->config = *config; ctx->owner = owner; ctx->epoch = config->association.extraction.epoch;
    /* The just-validated runtime has never admitted work. Discard it before
     * starting scan; no initialized runtime survives in an inactive member. */
    ctx->workspace = BDB_JOIN_WORK_NONE;
    if (mac_scan_init(&ctx->work.scan) != MAC_SCAN_OK) return BDB_JOIN_STATE;
    ctx->workspace = BDB_JOIN_WORK_SCAN;
    if (mac_scan_start(&ctx->work.scan, owner, &ctx->config.scan, now) != MAC_SCAN_OK)
        return BDB_JOIN_SCAN_FAILED;
    ctx->phase = BDB_JOIN_SCANNING;
    return BDB_JOIN_OK;
}
