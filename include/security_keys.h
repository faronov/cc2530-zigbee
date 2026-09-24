/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef SECURITY_KEYS_H
#define SECURITY_KEYS_H

#include "ed_wire.h"

typedef struct {
    uint8_t own_ieee[8], tc_ieee[8], extended_pan[8];
    uint16_t pan, address;
    uint8_t channel, update_id;
} security_keys_config_t;

typedef enum {
    SECURITY_KEYS_OK = 0, SECURITY_KEYS_EMPTY, SECURITY_KEYS_ARGUMENT,
    SECURITY_KEYS_STATE, SECURITY_KEYS_FORMAT, SECURITY_KEYS_STORAGE,
    SECURITY_KEYS_CRYPTO, SECURITY_KEYS_AUTH, SECURITY_KEYS_REPLAY,
    SECURITY_KEYS_IDENTITY, SECURITY_KEYS_SELECTOR, SECURITY_KEYS_CONTEXT,
    SECURITY_KEYS_UNSUPPORTED, SECURITY_KEYS_SPACE, SECURITY_KEYS_EXHAUSTED
} security_keys_result_t;

typedef enum {
    SECURITY_KEYS_COLD = 0, SECURITY_KEYS_UNPROVISIONED,
    SECURITY_KEYS_PROVISIONED, SECURITY_KEYS_ASSOCIATED,
    SECURITY_KEYS_RECEIVED, SECURITY_KEYS_REQUESTED,
    SECURITY_KEYS_PROVISIONAL, SECURITY_KEYS_WAIT_CONFIRM,
    SECURITY_KEYS_VERIFIED, SECURITY_KEYS_LEFT, SECURITY_KEYS_REJOINING,
    SECURITY_KEYS_FAILED
} security_keys_phase_t;

typedef enum {
    SECURITY_KEYS_EVENT_DATA = 0, SECURITY_KEYS_EVENT_MANAGEMENT,
    SECURITY_KEYS_EVENT_NETWORK_KEY, SECURITY_KEYS_EVENT_TC_KEY,
    SECURITY_KEYS_EVENT_VERIFIED, SECURITY_KEYS_EVENT_SWITCH,
    SECURITY_KEYS_EVENT_UPDATE, SECURITY_KEYS_EVENT_LEAVE, SECURITY_KEYS_EVENT_REJOINED
} security_keys_event_t;

/* Output-only receive metadata: low seven bits are security_keys_event_t.
 * Set only for genuinely APS-authenticated, durably admitted input. The
 * returned packet stays normalized (APS security flag cleared); this bit
 * lets the upper transaction owner choose ACK protection without an input
 * authentication assertion or exposing protected Transport-Key payloads.
 */
#define SECURITY_KEYS_EVENT_APS_SECURED 0x80u
#define SECURITY_KEYS_EVENT_MASK 0x7fu

typedef struct {
    security_keys_config_t config;
    uint8_t phase, active_sequence, slot_valid, newer_pending;
    /* Last authenticated successful Timeout Response, recognized bits0..2;
     * durable outstanding request intent, not delivery/deadline/readiness. */
    uint8_t parent_information, timeout_pending, result;
} security_keys_status_t;

/* One serialized foreground owner, fixed direct coordinator/TC only.
 * All objects complete, disjoint and stable; ordinary storage only, excluding
 * private/lower-service/libc/MMIO/status/IRAM alias storage. Link real lower
 * services before this module and callers. No ISR/reentrancy or callbacks.
 * Errors preserve caller outputs (including written/event); counters already
 * consumed stay consumed. Uncertain storage failure latches FAILED.
 * No key-export, authentication-boolean, reset, recovery or NV bypass API.
 */
security_keys_result_t security_keys_open(void) SECURITY_FAR;
security_keys_result_t security_keys_provision(
    const security_keys_config_t * volatile config, const uint8_t * volatile install_code18,
    volatile uint32_t nwk_floor, volatile uint32_t aps_floor,
    const ccm_star_limits_t *limits, uint16_t nv_polls) SECURITY_FAR;
security_keys_result_t security_keys_associate(uint16_t short_address, uint16_t nv_polls) SECURITY_FAR;
security_keys_result_t security_keys_receive(
    const uint8_t * volatile raw_npdu, volatile uint16_t length,
    ed_packet_t * volatile output, uint8_t * volatile event,
    const ccm_star_limits_t *limits, uint16_t nv_polls) SECURITY_FAR;
/* Request/Verify use AR=0, normalized APS header {01, aps_counter}.
 * Each call, including a retry with the same APS counter, builds a fresh
 * envelope and consumes fresh security counters for the protected layers.
 */
security_keys_result_t security_keys_request(
    uint8_t nwk_seq, uint8_t aps_counter, uint8_t * volatile out, volatile uint16_t capacity,
    uint8_t * volatile written, const ccm_star_limits_t *limits, uint16_t nv_polls) SECURITY_FAR;
security_keys_result_t security_keys_verify(
    uint8_t nwk_seq, uint8_t aps_counter, uint8_t * volatile out, volatile uint16_t capacity,
    uint8_t * volatile written, const ccm_star_limits_t *limits, uint16_t nv_polls) SECURITY_FAR;
/* APS protection of Data or full/short ACK uses data key-id0 only.
 * Real NWK Timeout Request0B commits its outstanding context before output.
 * All outgoing NPDU EDI bits are normalized from durable parent information,
 * never accepted as caller assertions.
 */
security_keys_result_t security_keys_send(
    const ed_packet_t * volatile packet, uint8_t aps_secure, uint8_t * volatile out, volatile uint16_t capacity,
    uint8_t * volatile written, const ccm_star_limits_t *limits, uint16_t nv_polls) SECURITY_FAR;
/* Local failed-join abandonment, not authenticated input or recovery.
 * With a network key: real protected NWK Leave 04 00, source IEEE, radius1,
 * destinationFFFD; requires36 bytes. LEFT is durable before frame publication.
 * Without a network key: durable quiet abandonment, OK with written=0 means
 * NO frame produced/sent; out stays untouched (out still required).
 * All key/replay history and both outgoing ceilings are retained. Only the
 * counter owner's normal journal saves write NV; no history erase, rejoin
 * request, automatic provisioning, counter reset or delivery claim.
 */
security_keys_result_t security_keys_leave(
    uint8_t nwk_seq, uint8_t * volatile out, volatile uint16_t capacity, uint8_t * volatile written,
    const ccm_star_limits_t *limits, uint16_t nv_polls) SECURITY_FAR;
/* Metadata only. VERIFIED is persisted key verification, NOT restart/rejoin,
 * BDB completion, radio readiness or application-traffic permission from BDB.
 */
security_keys_result_t security_keys_status(security_keys_status_t * volatile output) SECURITY_FAR;

#endif
