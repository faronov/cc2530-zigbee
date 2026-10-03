/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef BDB_JOIN_INTERNAL_H
#define BDB_JOIN_INTERNAL_H

#include "bdb_join.h"

#if defined(CC2530_MAC_LINK)
#define BDB_JOIN_ENGINE(tx) (&(tx)->engine)
#define BDB_JOIN_TX_KIND(action) ((action).control.kind)
#else
#define BDB_JOIN_ENGINE(tx) (tx)
#define BDB_JOIN_TX_KIND(action) ((action).kind)
#endif

#if defined(CC2530_MAC_LINK_DIRECT)
/* DIRECT shares NWK/APS's refresh-before-use key status snapshot. Every read
 * follows this module's own security_keys_status() in the same activation,
 * with no intervening call that can refresh it; nothing retains it. */
extern MCU_XDATA security_keys_status_t nwk_aps_keys;
#define bdb_join_keys nwk_aps_keys
#define BDB_JOIN_KEYS_STORAGE extern MCU_XDATA
#else
#define BDB_JOIN_KEYS_STORAGE MCU_XDATA
#endif
extern MCU_XDATA security_keys_status_t bdb_join_keys;
bdb_join_result_t bdb_join_advance(bdb_join_t BDB_JOIN_RAM * volatile ctx, volatile uint32_t now);
bdb_join_result_t bdb_join_runtime_init(bdb_join_t BDB_JOIN_RAM * volatile ctx,
    NWK_APS_TX_T BDB_JOIN_RAM * volatile owner, const bdb_join_config_t * volatile config,
    volatile uint32_t now);
#if defined(CC2530_DEFAULT_TC_KEY)
void bdb_join_discovered_config(bdb_join_t BDB_JOIN_RAM * volatile ctx);
bdb_join_result_t bdb_join_provision_discovered(bdb_join_t BDB_JOIN_RAM * volatile ctx);
#endif

#endif
