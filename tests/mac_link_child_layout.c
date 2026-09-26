/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 * Allocation-only target probe, never production firmware.
 */
#include "mac_link_ram_layout.c"
#include "mac_link_child_workspace_internal.h"
typedef char child_payload543[sizeof(child_work_arena_t)==543?1:-1];
typedef char child_owner31[sizeof(child_work_ownership_t)==31?1:-1];
child_work_arena_t MCU_XDATA child_layout_arena;
child_work_ownership_t MCU_XDATA child_layout_owner;
