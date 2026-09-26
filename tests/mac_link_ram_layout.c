/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 * Allocation-only SDCC probe, excluded from production totals and images.
 */
#include "mac_link_driver.h"
#if defined(CC2530_MAC_LINK_WORKSPACE)
#include "mac_link_workspace_internal.h"
typedef char upper_key_size[sizeof(link_work_keys_t) == 617 ? 1 : -1];
typedef char upper_protocol_size[sizeof(((link_work_arena_t *)0)->protocol) == 499 ? 1 : -1];
typedef char upper_payload_size[sizeof(link_work_arena_t) == 617 ? 1 : -1];
typedef char upper_metadata_size[sizeof(link_work_ownership_t) == 31 ? 1 : -1];
link_work_arena_t MCU_XDATA mac_link_workspace_layout_arena;
link_work_ownership_t MCU_XDATA mac_link_workspace_layout_metadata;
#endif
#if !defined(__SDCC_mcs51) || !defined(CC2530_MAC_LINK_RAM)
#error This layout record requires the explicit target compact profile
#endif
typedef char compact_association[sizeof(bdb_join_association_t) == 655 ? 1 : -1];
typedef char retained_runtime[sizeof(bdb_join_runtime_t) == 1047 ? 1 : -1];
typedef char compact_phase[sizeof(bdb_join_work_t) == 1047 ? 1 : -1];
typedef char compact_bdb[sizeof(bdb_join_t) == 1433 ? 1 : -1];
typedef char unchanged_owner[sizeof(mac_tx_interval_t) == 180 ? 1 : -1];
typedef char unchanged_driver[sizeof(mac_link_driver_t) == 304 ? 1 : -1];
typedef char unchanged_poll[sizeof(mac_poll_t) == 274 ? 1 : -1];
bdb_join_t MCU_XDATA mac_link_ram_layout_bdb;
mac_tx_interval_t MCU_XDATA mac_link_ram_layout_tx;
mac_link_driver_t MCU_XDATA mac_link_ram_layout_driver;
