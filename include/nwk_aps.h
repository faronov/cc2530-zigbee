/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef NWK_APS_H
#define NWK_APS_H

#include "ed_wire.h"
#include "mac_tx.h"

#define NWK_APS_DUPLICATES 8u
#define NWK_APS_RETRIES 3u
#define NWK_APS_ACK_WAIT 100000UL
#define NWK_APS_DUPLICATE_TIME 1000000UL
#define NWK_APS_TX_LIFETIME 500000UL
#define NWK_APS_TX_WORK 256u

typedef enum {
    NWK_APS_OK = 0, NWK_APS_ARGUMENT, NWK_APS_STATE, NWK_APS_FULL,
    NWK_APS_SECURITY, NWK_APS_WIRE, NWK_APS_CLOCK, NWK_APS_TIMEOUT,
    NWK_APS_RADIO, NWK_APS_DUPLICATE, NWK_APS_IGNORED, NWK_APS_EXHAUSTED,
    NWK_APS_CANCELLED, NWK_APS_ACK_FAILED
} nwk_aps_result_t;

typedef struct {
    uint32_t until;
    uint16_t source;
    uint8_t counter, kind, used;
} nwk_aps_duplicate_t;

typedef struct {
    ed_packet_t outgoing, incoming, acknowledgment, staging;
    nwk_aps_duplicate_t duplicate[NWK_APS_DUPLICATES];
    nwk_aps_duplicate_t broadcast[NWK_APS_DUPLICATES];
    mac_tx_t *owner;
    ccm_star_limits_t limits;
    uint8_t wire[NWK_FRAME_MAX_BODY], mac[MAC_FRAME_MAX_BODY];
    uint32_t last, deadline, counter_until, ack_wait, duplicate_time, transaction_until, broadcast_time;
    uint16_t nv_polls, profile;
    uint8_t endpoint, next_aps, next_nwk, length, mac_length;
    uint8_t queued, active, active_ack, waiting, retries, seen_ack, sent;
    uint8_t reply, reply_secure, receive_ready, event, secure, special;
    uint8_t completed, result, error, announced, permit_sent, ready, wrap_wait, version;
    uint8_t cancel, cancel_sent, cancel_result, parent_information, quiet;
    uint8_t stopping, reply_result;
} nwk_aps_t;

/* A single foreground owner with one application/control TX, one priority ACK,
 * one RX and eight non-evictable live duplicate entries. Time uses the same
 * uint32 16-us symbols as mac_tx (true gaps/intervals <2^31). No reset of a
 * leased/faulted MAC slot. Initialization never authenticates or resumes BDB.
 * ack_wait includes a caller-established encryption/decryption bound on top
 * of the profile2 93750-symbol base. Drain RX/ACK capacity before more input;
 * FULL never authorizes a dropped control transition or successful delivery.
 * broadcast_time is the established network broadcast-delivery bound in
 * symbols. Eight separate source/NWK-sequence BTRs are never silently evicted.
 */
nwk_aps_result_t nwk_aps_init(nwk_aps_t *ctx, mac_tx_t *owner, uint8_t endpoint,
    uint16_t profile, const ccm_star_limits_t *limits, uint16_t nv_polls,
    uint32_t ack_wait, uint32_t broadcast_time, uint32_t now);
nwk_aps_result_t nwk_aps_queue(nwk_aps_t *ctx, const ed_packet_t *packet,
                               uint8_t aps_secure, uint32_t now);
/* which1=Request-Key, which2=Verify-Key, which3=local Leave. quiet explicitly
 * records keyless abandonment without a PHY transmission.
 * Key selection/phase/counters belong
 * exclusively to the real security owner, never to a caller-supplied boolean.
 */
nwk_aps_result_t nwk_aps_key_exchange(nwk_aps_t *ctx, uint8_t which, uint32_t now);
nwk_aps_result_t nwk_aps_step(nwk_aps_t *ctx, uint32_t now,
    const mac_tx_event_t *event, mac_tx_action_t *action);
nwk_aps_result_t nwk_aps_receive(nwk_aps_t *ctx, const uint8_t *npdu, uint16_t length, uint32_t now);
nwk_aps_result_t nwk_aps_take(nwk_aps_t *ctx, ed_packet_t *packet, uint8_t *event);
nwk_aps_result_t nwk_aps_confirm(nwk_aps_t *ctx, uint8_t *result);
/* Request cancellation; step must still establish physical quiescence. */
nwk_aps_result_t nwk_aps_cancel(nwk_aps_t *ctx, uint32_t now);
/* Stop ordinary TX and priority ACK without releasing an active MAC lease.
 * step drains cancellation/QUIESCED before clearing stopping. A failed ACK
 * with confirmed MAC release returns ACK_FAILED and records its MAC outcome
 * in reply_result; it is not an uncertain radio fault or membership loss.
 */
nwk_aps_result_t nwk_aps_stop(nwk_aps_t *ctx, uint32_t now);

/* Nonreentrant, disjoint complete ordinary caller objects only. Applications
 * remain blocked until a genuine network announcement, verified TC key and
 * actual final permit-join transmission. Broadcast completion means local
 * transmission/quiescence, not acknowledgement from every network device.
 * Secured retransmissions retain APS identity but consume fresh security and
 * NWK counters. Counter wrap has a full duplicate-window quarantine.
 */
#endif
