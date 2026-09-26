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

extern MCU_XDATA security_keys_status_t bdb_join_keys;
bdb_join_result_t bdb_join_advance(bdb_join_t BDB_JOIN_RAM * volatile ctx, volatile uint32_t now);
bdb_join_result_t bdb_join_runtime_init(bdb_join_t BDB_JOIN_RAM * volatile ctx,
    NWK_APS_TX_T BDB_JOIN_RAM * volatile owner, const bdb_join_config_t * volatile config,
    volatile uint32_t now);

#endif
