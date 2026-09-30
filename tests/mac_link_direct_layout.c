/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 * Allocation-only DIRECT target probe, never production firmware. It mirrors
 * mac_link_child_layout.c exactly, except that nwk_aps_t has no private MAC
 * build buffer, so only the retained runtime/work/BDB sizes differ.
 */
#include "mac_link_driver.h"
#include "mac_link_workspace_internal.h"
#include "mac_link_child_workspace_internal.h"
#if !defined(__SDCC_mcs51) || !defined(CC2530_MAC_LINK_DIRECT)
#error This layout record requires the explicit target DIRECT profile
#endif
typedef char upper_key_size[sizeof(link_work_keys_t) == 617 ? 1 : -1];
typedef char upper_protocol_size[sizeof(((link_work_arena_t *)0)->protocol) == 499 ? 1 : -1];
typedef char upper_payload_size[sizeof(link_work_arena_t) == 617 ? 1 : -1];
typedef char upper_metadata_size[sizeof(link_work_ownership_t) == 31 ? 1 : -1];
typedef char child_payload543[sizeof(child_work_arena_t) == 543 ? 1 : -1];
typedef char child_owner31[sizeof(child_work_ownership_t) == 31 ? 1 : -1];
typedef char direct_transport[sizeof(nwk_aps_t) == 708 - MAC_FRAME_MAX_BODY ? 1 : -1];
typedef char compact_association[sizeof(bdb_join_association_t) == 655 ? 1 : -1];
typedef char direct_runtime[sizeof(bdb_join_runtime_t) == 1047 - MAC_FRAME_MAX_BODY ? 1 : -1];
typedef char direct_phase[sizeof(bdb_join_work_t) == 922 ? 1 : -1];
typedef char direct_bdb[sizeof(bdb_join_t) == 1433 - MAC_FRAME_MAX_BODY ? 1 : -1];
typedef char unchanged_owner[sizeof(mac_tx_interval_t) == 180 ? 1 : -1];
typedef char unchanged_driver[sizeof(mac_link_driver_t) == 304 ? 1 : -1];
typedef char unchanged_poll[sizeof(mac_poll_t) == 274 ? 1 : -1];
link_work_arena_t MCU_XDATA mac_link_workspace_layout_arena;
link_work_ownership_t MCU_XDATA mac_link_workspace_layout_metadata;
bdb_join_t MCU_XDATA mac_link_ram_layout_bdb;
mac_tx_interval_t MCU_XDATA mac_link_ram_layout_tx;
mac_link_driver_t MCU_XDATA mac_link_ram_layout_driver;
child_work_arena_t MCU_XDATA child_layout_arena;
child_work_ownership_t MCU_XDATA child_layout_owner;
