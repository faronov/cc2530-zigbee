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
#define BDB_JOIN_VERSION 3u
#define BDB_JOIN_SECURITY_WAIT 625000UL
#define BDB_JOIN_EXCHANGE_WAIT 312500UL
#define BDB_JOIN_KEEPALIVE 1875000UL
#define BDB_JOIN_ATTEMPTS 3u
#define BDB_JOIN_STALL_STEPS 4096u

typedef enum {
    BDB_JOIN_OK = 0, BDB_JOIN_ARGUMENT, BDB_JOIN_STATE, BDB_JOIN_CLOCK,
    BDB_JOIN_SECURITY, BDB_JOIN_RECOVERY_REQUIRED, BDB_JOIN_SCAN_FAILED,
    BDB_JOIN_NO_PARENT, BDB_JOIN_ASSOCIATION_FAILED, BDB_JOIN_INSTALL_FAILED,
    BDB_JOIN_KEY_TIMEOUT, BDB_JOIN_TRANSMIT_FAILED, BDB_JOIN_TC_FAILED,
    BDB_JOIN_PARENT_FAILED, BDB_JOIN_ADDRESS_CONFLICT, BDB_JOIN_REMOTE_LEAVE,
    BDB_JOIN_FULL, BDB_JOIN_MALFORMED, BDB_JOIN_IGNORED, BDB_JOIN_WORK_LIMIT
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
    nwk_aps_config_t transport;
    uint8_t link_cost;
} bdb_join_config_t;

typedef union {
    mac_scan_event_t scan;
    mac_join_event_t association;
    NWK_APS_EVENT_T tx;
    security_keys_config_t installed;
} bdb_join_event_data_t;

typedef struct {
    bdb_join_event_data_t data;
    uint32_t epoch;
    uint16_t token;
    uint8_t kind;
} bdb_join_event_t;

typedef union {
    mac_scan_action_t scan;
    mac_join_action_t association;
    NWK_APS_ACTION_T tx;
    security_keys_config_t install;
} bdb_join_action_data_t;

typedef struct {
    bdb_join_action_data_t data;
    uint32_t epoch;
    uint16_t token;
    uint8_t kind;
} bdb_join_action_t;

/* Only the tagged member is live. NONE includes fresh/rejected start storage.
 * A failed/faulted controller retains its member, including its lower lease.
 */
#define BDB_JOIN_WORK_NONE 0u
#define BDB_JOIN_WORK_SCAN 1u
#define BDB_JOIN_WORK_ASSOCIATION 2u
#define BDB_JOIN_WORK_RUNTIME 3u

typedef struct {
    nwk_aps_t transport;
    zdo_runtime_t zdo;
} bdb_join_runtime_t;

typedef struct {
    mac_join_t context;
#if defined(CC2530_JOIN_WORKSPACE) && !defined(CC2530_MAC_LINK_RAM)
    mac_join_t staged;
#endif
} bdb_join_association_t;

typedef union {
    mac_scan_t scan;
    bdb_join_association_t association;
    bdb_join_runtime_t runtime;
} bdb_join_work_t;

/* Retained scan outcome after release; the selected parent is copied below.
 * The candidate table itself is available only while WORK_SCAN is retained.
 */
typedef struct {
    uint32_t generation, sent, unscanned;
    uint8_t reason, cleanup_error, overflow, tx_outcome, candidates;
} bdb_join_scan_result_t;

typedef struct {
    bdb_join_work_t work;
    bdb_join_config_t config;
    bdb_join_scan_result_t scan_result;
    mac_join_record_t record;
    nwk_parent_choice_t parent;
    NWK_APS_TX_T BDB_JOIN_RAM *owner;
    uint32_t last, until, keepalive, epoch, commission_until, work_at;
    uint16_t token, steps;
    uint8_t version, phase, result, cleanup_error, attempts, issued, member;
    uint8_t got_tc, got_confirm, application_pending, application_done, application_result;
    uint8_t receive_result, abandon;
    uint8_t workspace;
} bdb_join_t;

/* Original bounded R22/BDB3.0.1 centralized, install-code, awake direct-TC ED.
 * start requires explicit real provisioning, not EMPTY-media initialization.
 * Existing verified storage returns RECOVERY_REQUIRED; it is not resumed join.
 * scan/association actions preserve the real lower-controller grant contracts.
 * Event/action data is a real tagged union: construct/read only the member
 * selected by kind. epoch/token/kind are outside that union. Finish consuming
 * a lower TX event before reusing its bytes for a scan-completion event.
 * INSTALL requires truthful confirmation of channel/PAN/IEEE/allocated-short
 * filters and continuous awake receive service, with matching epoch/token.
 * No function accesses equipment or supplies RF, entropy, CRC or timestamps.
 * Time is mac_tx's coherent captured 16-us symbol epoch, gaps <2^31.
 * After association, at most STALL_STEPS consecutive step calls may observe
 * the same symbol time. Advancing time resets this local work guard; normal
 * polling frequency does not shorten protocol deadlines. Exhaustion latches
 * WORK_LIMIT, not KEY_TIMEOUT, and drains without durable Leave. An outstanding
 * unconfirmed INSTALL instead retains FAULT. Scan/association retain their
 * own explicit lower work limits. Unrelated events cannot bypass this guard
 * or retirement: inspect actions even when step returns IGNORED.
 * READY keepalive retries at most ATTEMPTS times. Operational failures stop
 * application readiness but preserve durable keys for explicit recovery;
 * commissioning abandonment/address conflict still requests genuine Leave.
 */
bdb_join_result_t bdb_join_init(bdb_join_t BDB_JOIN_RAM * volatile ctx, volatile uint32_t now) JOIN_FAR;
bdb_join_result_t bdb_join_start(bdb_join_t BDB_JOIN_RAM * volatile ctx, NWK_APS_TX_T BDB_JOIN_RAM * volatile owner,
    const bdb_join_config_t * volatile config, volatile uint32_t now) JOIN_FAR;
bdb_join_result_t bdb_join_step(bdb_join_t BDB_JOIN_RAM * volatile ctx, volatile uint32_t now,
    const bdb_join_event_t BDB_JOIN_RAM * volatile event, bdb_join_action_t BDB_JOIN_RAM * volatile action) JOIN_FAR;
/* FCS-free DATA body; CRC truth is PHY metadata, never authentication.
 * Authentication, replay checks and all key decisions execute real C services.
 */
bdb_join_result_t bdb_join_receive(bdb_join_t BDB_JOIN_RAM * volatile ctx,
    const uint8_t * volatile body, volatile uint16_t length, volatile uint8_t crc_valid, volatile uint32_t now) JOIN_FAR;
bdb_join_result_t bdb_join_send(bdb_join_t BDB_JOIN_RAM * volatile ctx,
    const ed_packet_t * volatile packet, volatile uint8_t aps_secure, volatile uint32_t now) JOIN_FAR;
bdb_join_result_t bdb_join_confirm(bdb_join_t BDB_JOIN_RAM * volatile ctx, uint8_t * volatile result) JOIN_FAR;
#if defined(CC2530_MAC_LINK)
/* ACTION_TX/NWK_APS_ACTION_ARM is a one-shot adapter preparation request.
 * Confirm the unchanged slot identity after physical prepare, before RANDOM.
 * No RX coverage is implied; OFF gaps around ordinary TX remain explicit.
 */
bdb_join_result_t bdb_join_armed(bdb_join_t BDB_JOIN_RAM * volatile ctx,
    uint32_t generation, uint8_t retry, uint8_t nb) JOIN_FAR;
bdb_join_result_t bdb_join_disarmed(bdb_join_t BDB_JOIN_RAM * volatile ctx,
    uint32_t generation, uint8_t retry, uint8_t nb) JOIN_FAR;
#endif

/* CC2530_MAC_LINK selects the observed interval owner for scan, association
 * and transport; TX events/actions are interval types. The retained Response
 * stamp is then a conservative upper bound, so security waits measured from it
 * are never shorter than configured.
 */
/* Public context fields are read-only diagnostics. Inspect workspace before
 * reading work.scan, work.association or work.runtime; inactive union members
 * are not diagnostics. A successful release precedes every reuse. Association
 * retries retain their context/generations; runtime persists through Update,
 * stop and fault. record (including the actual Response stamp), scan_result
 * and parent survive all subsequent phases. Runtime readiness exists only in
 * WORK_RUNTIME; NONE/SCAN/ASSOCIATION can never authorize application traffic.
 * init is for fresh storage/adapter epoch only, never recovery of a live lease.
 * One foreground owner,
 * complete disjoint ordinary persistent objects; no reentrancy/ISR or resets
 * of leased/faulted lower owners. READY is local authenticated commissioning
 * completion, not certification, hardware observation or peer app receipt.
 */
#endif
