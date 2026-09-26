/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef MAC_LINK_WORKSPACE_INTERNAL_H
#define MAC_LINK_WORKSPACE_INTERNAL_H
#include "mac_link_workspace_guard_internal.h"
#include "mac_join.h"
#include "nwk_parent.h"
#include "nwk_aps.h"
#include "zdo_runtime.h"
#include "security_keys.h"
#include "zigbee_key_hash.h"

#if !defined(CC2530_MAC_LINK_WORKSPACE)
#error This source-owned workspace requires its explicit composition profile
#endif
#define LINK_WORK_LK_OFFSET 48u
#define LINK_WORK_PK_OFFSET 64u
typedef struct {
    uint8_t top, grant, ancestors[LW_FRAMES];
} link_work_ownership_t;

typedef struct {
    uint8_t record[112], a[116], b[116], hash_key[16];
    ed_packet_t packet;
    nwk_frame_info_t nwk;
    zigbee_security_key_t key;
    zigbee_security_meta_t meta;
    zigbee_security_info_t info;
    zigbee_mmo_info_t hash_info;
    ccm_star_limits_t limits;
    uint32_t counter;
    uint16_t polls;
    uint8_t size, event, pending, slot, secured, aps_secured, key_id;
} link_work_keys_t;

typedef union {
    struct {
        mac_join_request_t proposed;
        mac_header_t header;
        uint8_t command[2], body[25], length;
    } start;
    struct {
        mac_join_event_t input;
        mac_join_action_t output;
        mac_poll_action_t pa;
        union {
            mac_poll_event_t pe;
            mac_association_request_t ar;
            mac_association_event_t ae;
            mac_poll_request_t pr;
        } nested;
    } step;
} link_work_join_call_t;
typedef struct {
    mac_join_t MCU_XDATA *active;
    mac_tx_interval_t MCU_XDATA * volatile owner;
    mac_tx_interval_t * volatile tx_argument;
    link_work_join_call_t work;
    volatile uint32_t time, remaining;
    const mac_tx_interval_event_t * volatile source;
    const mac_poll_event_t MCU_XDATA * volatile poll_event;
    volatile uint16_t admitted;
    uint8_t observation;
} link_work_join_t;
typedef struct {
    mac_poll_control_t MCU_XDATA *active;
    union { mac_header_t header; mac_frame_info_t frame; } syntax;
    union {
        mac_poll_event_t input;
        mac_poll_action_t output;
        struct { uint8_t body[22], length; } start;
    } io;
    mac_poll_record_t MCU_XDATA *receipt;
    uint32_t step_time;
    mac_poll_t MCU_XDATA * volatile step_poll;
    mac_tx_interval_t MCU_XDATA * volatile step_tx;
    const mac_poll_event_t MCU_XDATA * volatile step_event;
    mac_poll_action_t MCU_XDATA * volatile step_action;
    volatile uint32_t step_now;
    volatile uint8_t step_profile;
} link_work_poll_t;
typedef struct {
    uint32_t last, deadline, at, tx_end, ready_at, generation, stop_at;
    uint16_t steps;
    uint8_t phase, next_dsn, length, ack_requested, nb, be, retries, outcome;
    uint8_t transmissions, uncertain, pending, retry_pending, stop_steps;
} link_work_tx_control_t;
typedef struct {
    link_work_tx_control_t control;
    volatile struct { uint32_t now, lifetime; uint16_t length, limit; } input;
    mac_epoch_stamp_t interval_end;
    mac_frame_info_t interval_decoded;
    uint8_t interval_ack[3];
    uint32_t interval_previous, observed_previous;
    uint8_t observed_phase;
    const mac_epoch_stamp_t * volatile observed_lower;
    const mac_epoch_stamp_t * volatile observed_upper;
    mac_frame_info_t decoded;
} link_work_tx_t;
typedef union {
    mac_header_t mac_header;
    mac_tx_interval_event_t cancellation;
    nwk_frame_info_t hint;
} link_work_nwk_t;
typedef union {
    struct { zdo_srv_local_t local; zdo_srv_rx_t rx; zdo_srv_info_t info; } server;
    struct { zdo_node_response_t node; uint8_t descriptor_bytes[17]; } descriptor;
} link_work_zdo_t;
typedef union {
    link_work_keys_t keys;
    struct {
        union {
            link_work_join_t join;
            struct {
                mac_frame_info_t frame;
                mac_beacon_info_t beacon;
                nwk_candidate_t candidate;
                nwk_beacon_t decoded;
            } scan;
            nwk_candidate_t selected;
            link_work_nwk_t nwk;
            struct {
                link_work_zdo_t work;
                struct { zdo_node_response_t reply; uint8_t staged[17]; zdo_srv_info_t candidate; } server;
                zdo_node_response_t node;
            } zdo;
        } parent;
        link_work_poll_t poll;
        union { link_work_tx_t tx; mac_frame_info_t association; } operation;
        struct { mac_frame_info_t candidate; mac_beacon_info_t beacon; } codec;
    } protocol;
} link_work_arena_t;

/* One serialized foreground call tree; not ISR/callback reentrancy. Only
 * source-owned functions may acquire frames or issue the typed call grants.
 * Neither this arena nor owner/compiler work is general caller I/O.
 *
 * Ordinary linker allocation. Integration must put the entire manager object
 * before flash_exec_reserved_end and every other lower private fence, with
 * the arena at a nonzero ordinary address. Check the actual map/ABI. No
 * absolute address, private-symbol alias, caller pool, or fallback placement. */
extern MCU_XDATA link_work_arena_t link_work_arena;
#endif
