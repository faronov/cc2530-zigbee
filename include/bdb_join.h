/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef BDB_JOIN_H
#define BDB_JOIN_H
#include "mac_scan.h"
#include "mac_join.h"
#include "nwk_parent.h"
#include "security_keys.h"
#include "zdo_runtime.h"

#define BDB_JOIN_RAM MAC_JOIN_RAM
#define BDB_JOIN_SECURITY_WAIT 625000UL
#define BDB_JOIN_EXCHANGE_WAIT 312500UL
#define BDB_JOIN_KEEPALIVE 1875000UL
#define BDB_JOIN_ATTEMPTS 3u

typedef enum {
    BDB_JOIN_OK = 0, BDB_JOIN_ARGUMENT, BDB_JOIN_STATE, BDB_JOIN_CLOCK,
    BDB_JOIN_SECURITY, BDB_JOIN_RECOVERY_REQUIRED, BDB_JOIN_SCAN_FAILED,
    BDB_JOIN_NO_PARENT, BDB_JOIN_ASSOCIATION_FAILED, BDB_JOIN_INSTALL_FAILED,
    BDB_JOIN_KEY_TIMEOUT, BDB_JOIN_TRANSMIT_FAILED, BDB_JOIN_TC_FAILED,
    BDB_JOIN_PARENT_FAILED, BDB_JOIN_ADDRESS_CONFLICT, BDB_JOIN_REMOTE_LEAVE,
    BDB_JOIN_FULL, BDB_JOIN_MALFORMED, BDB_JOIN_IGNORED
} bdb_join_result_t;

typedef enum {
    BDB_JOIN_IDLE = 0, BDB_JOIN_SCANNING, BDB_JOIN_ASSOCIATING, BDB_JOIN_INSTALLING,
    BDB_JOIN_WAIT_KEY, BDB_JOIN_ANNOUNCING, BDB_JOIN_NODE, BDB_JOIN_REQUESTING,
    BDB_JOIN_WAIT_TC, BDB_JOIN_VERIFYING, BDB_JOIN_WAIT_CONFIRM, BDB_JOIN_PARENT,
    BDB_JOIN_PERMIT, BDB_JOIN_READY, BDB_JOIN_UPDATING, BDB_JOIN_ABORTING,
    BDB_JOIN_LEAVING, BDB_JOIN_FAILED, BDB_JOIN_FAULT
} bdb_join_phase_t;

#define BDB_JOIN_ACTION_SCAN 1u
#define BDB_JOIN_ACTION_ASSOCIATION 2u
#define BDB_JOIN_ACTION_TX 3u
#define BDB_JOIN_ACTION_INSTALL 4u
#define BDB_JOIN_EVENT_SCAN 1u
#define BDB_JOIN_EVENT_ASSOCIATION 2u
#define BDB_JOIN_EVENT_TX 3u
#define BDB_JOIN_EVENT_INSTALLED 4u

typedef struct {
    mac_scan_request_t scan;
    mac_join_request_t association;
    zdo_node_descriptor_t descriptor;
    ccm_star_limits_t crypto;
    uint32_t ack_wait, broadcast_time;
    uint16_t nv_polls, profile;
    uint8_t endpoint, link_cost;
} bdb_join_config_t;

typedef struct {
    mac_scan_event_t scan;
    mac_join_event_t association;
    mac_tx_event_t tx;
    security_keys_config_t installed;
    uint32_t epoch;
    uint16_t token;
    uint8_t kind;
} bdb_join_event_t;

typedef struct {
    mac_scan_action_t scan;
    mac_join_action_t association;
    mac_tx_action_t tx;
    security_keys_config_t install;
    uint32_t epoch;
    uint16_t token;
    uint8_t kind;
} bdb_join_action_t;

typedef struct {
    mac_scan_t scan;
    mac_join_t association;
    nwk_aps_t transport;
    zdo_runtime_t zdo;
    bdb_join_config_t config;
    mac_join_record_t record;
    nwk_parent_choice_t parent;
    ed_packet_t packet;
    mac_tx_t BDB_JOIN_RAM *owner;
    uint32_t last, until, keepalive, epoch, commission_until;
    uint16_t token, steps;
    uint8_t version, phase, result, cleanup_error, attempts, issued, member;
    uint8_t got_tc, got_confirm, application_pending, application_done, application_result;
    uint8_t receive_result;
} bdb_join_t;

/* Original bounded R22/BDB3.0.1 centralized, install-code, awake direct-TC ED.
 * start requires explicit real provisioning, not EMPTY-media initialization.
 * Existing verified storage returns RECOVERY_REQUIRED; it is not resumed join.
 * scan/association actions preserve the real lower-controller grant contracts.
 * INSTALL requires truthful confirmation of channel/PAN/IEEE/allocated-short
 * filters and continuous awake receive service, with matching epoch/token.
 * No function accesses equipment or supplies RF, entropy, CRC or timestamps.
 * Time is mac_tx's coherent captured 16-us symbol epoch, gaps <2^31.
 */
bdb_join_result_t bdb_join_init(bdb_join_t BDB_JOIN_RAM *ctx, uint32_t now);
bdb_join_result_t bdb_join_start(bdb_join_t BDB_JOIN_RAM *ctx, mac_tx_t BDB_JOIN_RAM *owner,
    const bdb_join_config_t *config, uint32_t now);
bdb_join_result_t bdb_join_step(bdb_join_t BDB_JOIN_RAM *ctx, uint32_t now,
    const bdb_join_event_t BDB_JOIN_RAM *event, bdb_join_action_t BDB_JOIN_RAM *action);
/* FCS-free DATA body; CRC truth is PHY metadata, never authentication.
 * Authentication, replay checks and all key decisions execute real C services.
 */
bdb_join_result_t bdb_join_receive(bdb_join_t BDB_JOIN_RAM *ctx,
    const uint8_t *body, uint16_t length, uint8_t crc_valid, uint32_t now);
bdb_join_result_t bdb_join_send(bdb_join_t BDB_JOIN_RAM *ctx,
    const ed_packet_t *packet, uint8_t aps_secure, uint32_t now);
bdb_join_result_t bdb_join_confirm(bdb_join_t BDB_JOIN_RAM *ctx, uint8_t *result);

/* Public context fields are read-only diagnostics. One foreground owner,
 * complete disjoint ordinary persistent objects; no reentrancy/ISR or resets
 * of leased/faulted lower owners. READY is local authenticated commissioning
 * completion, not certification, hardware observation or peer app receipt.
 */
#endif
