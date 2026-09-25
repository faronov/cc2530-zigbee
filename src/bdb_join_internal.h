/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef BDB_JOIN_INTERNAL_H
#define BDB_JOIN_INTERNAL_H

#include "bdb_join.h"

extern MCU_XDATA security_keys_status_t bdb_join_keys;
bdb_join_result_t bdb_join_advance(bdb_join_t BDB_JOIN_RAM * volatile ctx, volatile uint32_t now);
bdb_join_result_t bdb_join_runtime_init(bdb_join_t BDB_JOIN_RAM * volatile ctx,
    mac_tx_t BDB_JOIN_RAM * volatile owner, const bdb_join_config_t * volatile config,
    volatile uint32_t now);

#endif
