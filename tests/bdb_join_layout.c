/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 * SDCC object allocation regression ONLY, not a linked/executable fixture.
 */
#include "bdb_join.h"
#include <stddef.h>

#if !defined(__SDCC_mcs51) || !defined(__SDCC_MODEL_LARGE) || \
    __SDCC_VERSION_MAJOR != 4 || __SDCC_VERSION_MINOR != 2 || __SDCC_VERSION_PATCH != 0
#error This allocation record requires the SDCC 4.2.0 large-model ABI
#endif

typedef char transport_size[sizeof(nwk_aps_t) == 703 ? 1 : -1];
typedef char endpoint_zero_size[sizeof(zdo_runtime_t) == 339 ? 1 : -1];
typedef char work_size[sizeof(bdb_join_work_t) == 1042 ? 1 : -1];
typedef char context_size[sizeof(bdb_join_t) == 1428 ? 1 : -1];
typedef char scan_result_size[sizeof(bdb_join_scan_result_t) == 17 ? 1 : -1];
typedef char event_size[sizeof(bdb_join_event_t) == 55 ? 1 : -1];
typedef char action_size[sizeof(bdb_join_action_t) == 51 ? 1 : -1];
typedef char event_union[
    offsetof(bdb_join_event_data_t, scan) == 0 && offsetof(bdb_join_event_data_t, association) == 0 &&
    offsetof(bdb_join_event_data_t, tx) == 0 && offsetof(bdb_join_event_data_t, installed) == 0 ? 1 : -1];
typedef char action_union[
    offsetof(bdb_join_action_data_t, scan) == 0 && offsetof(bdb_join_action_data_t, association) == 0 &&
    offsetof(bdb_join_action_data_t, tx) == 0 && offsetof(bdb_join_action_data_t, install) == 0 ? 1 : -1];
typedef char tags_outside_data[
    offsetof(bdb_join_event_t, epoch) >= sizeof(bdb_join_event_data_t) &&
    offsetof(bdb_join_event_t, token) >= sizeof(bdb_join_event_data_t) &&
    offsetof(bdb_join_event_t, kind) >= sizeof(bdb_join_event_data_t) &&
    offsetof(bdb_join_action_t, epoch) >= sizeof(bdb_join_action_data_t) &&
    offsetof(bdb_join_action_t, token) >= sizeof(bdb_join_action_data_t) &&
    offsetof(bdb_join_action_t, kind) >= sizeof(bdb_join_action_data_t) ? 1 : -1];
typedef char phase_union[
    offsetof(bdb_join_work_t, scan) == 0 && offsetof(bdb_join_work_t, association) == 0 &&
    offsetof(bdb_join_work_t, runtime) == 0 ? 1 : -1];
typedef char retained_outside_work[
    offsetof(bdb_join_t, config) >= sizeof(bdb_join_work_t) &&
    offsetof(bdb_join_t, scan_result) >= sizeof(bdb_join_work_t) &&
    offsetof(bdb_join_t, record) >= sizeof(bdb_join_work_t) &&
    offsetof(bdb_join_t, parent) >= sizeof(bdb_join_work_t) &&
    offsetof(bdb_join_t, owner) >= sizeof(bdb_join_work_t) &&
    offsetof(bdb_join_t, workspace) >= sizeof(bdb_join_work_t) ? 1 : -1];

/* Real compiler allocations for inspection, never an invented alias pool.
 * No service is replaced, no executable is linked, no stack fit is implied.
 */
bdb_join_t MCU_XDATA bdb_layout_context;
mac_tx_t MCU_XDATA bdb_layout_owner;
bdb_join_config_t MCU_XDATA bdb_layout_config;
bdb_join_event_t MCU_XDATA bdb_layout_event;
bdb_join_action_t MCU_XDATA bdb_layout_action;
