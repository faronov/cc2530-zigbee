/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef NWK_APS_H
#define NWK_APS_H

#include "ed_wire.h"
#include "mac_tx.h"

#define NWK_APS_VERSION 2u
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
    ccm_star_limits_t limits;
    uint32_t ack_wait, broadcast_time;
    uint16_t nv_polls, profile;
    uint8_t endpoint;
} nwk_aps_config_t;

typedef struct {
    ed_packet_t outgoing, incoming, acknowledgment;
    nwk_aps_duplicate_t duplicate[NWK_APS_DUPLICATES];
    nwk_aps_duplicate_t broadcast[NWK_APS_DUPLICATES];
    mac_tx_t *owner;
    ccm_star_limits_t limits;
    uint8_t mac[MAC_FRAME_MAX_BODY];
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
 * An unoccupied incoming slot is returning receive scratch; receive_ready is
 * set only after actual authentication, durable admission and routing checks.
 * Nonpublication clears that slot; a pending packet/ACK blocks reuse before
 * security processing. Protected TX is constructed after the fixed short/
 * short compressed MAC prefix, then the real codec emits only that header.
 * No overlapping payload/output is passed to a codec.
 * init copies a complete, disjoint configuration; it retains no pointer to
 * that input. The grouped input avoids an eleven-argument SDCC caller frame.
 */
nwk_aps_result_t nwk_aps_init(nwk_aps_t * volatile ctx, mac_tx_t * volatile owner,
    const nwk_aps_config_t * volatile config, volatile uint32_t now) JOIN_FAR;
nwk_aps_result_t nwk_aps_queue(nwk_aps_t * volatile ctx, const ed_packet_t * volatile packet,
                               volatile uint8_t aps_secure, volatile uint32_t now) JOIN_FAR;
/* which1=Request-Key, which2=Verify-Key, which3=local Leave. quiet explicitly
 * records keyless abandonment without a PHY transmission.
 * Key selection/phase/counters belong
 * exclusively to the real security owner, never to a caller-supplied boolean.
 */
nwk_aps_result_t nwk_aps_key_exchange(nwk_aps_t * volatile ctx, volatile uint8_t which, volatile uint32_t now) JOIN_FAR;
nwk_aps_result_t nwk_aps_step(nwk_aps_t * volatile ctx, volatile uint32_t now,
    const mac_tx_event_t * volatile event, mac_tx_action_t * volatile action) JOIN_FAR;
nwk_aps_result_t nwk_aps_receive(nwk_aps_t * volatile ctx, const uint8_t * volatile npdu,
    volatile uint16_t length, volatile uint32_t now) JOIN_FAR;
nwk_aps_result_t nwk_aps_take(nwk_aps_t * volatile ctx, ed_packet_t * volatile packet, uint8_t * volatile event) JOIN_FAR;
nwk_aps_result_t nwk_aps_confirm(nwk_aps_t * volatile ctx, uint8_t * volatile result) JOIN_FAR;
/* Request cancellation; step must still establish physical quiescence. */
nwk_aps_result_t nwk_aps_cancel(nwk_aps_t * volatile ctx, volatile uint32_t now) JOIN_FAR;
/* Stop ordinary TX and priority ACK without releasing an active MAC lease.
 * step drains cancellation/QUIESCED before clearing stopping. A failed ACK
 * with confirmed MAC release returns ACK_FAILED and records its MAC outcome
 * in reply_result; it is not an uncertain radio fault or membership loss.
 */
nwk_aps_result_t nwk_aps_stop(nwk_aps_t * volatile ctx, volatile uint32_t now) JOIN_FAR;

/* Nonreentrant, disjoint complete ordinary caller objects only. Applications
 * remain blocked until a genuine network announcement, verified TC key and
 * actual final permit-join transmission. Broadcast completion means local
 * transmission/quiescence, not acknowledgement from every network device.
 * Secured retransmissions retain APS identity but consume fresh security and
 * NWK counters. Counter wrap has a full duplicate-window quarantine.
 */
#endif
