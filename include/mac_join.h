/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef MAC_JOIN_H
#define MAC_JOIN_H
#include "mac_poll.h"
#include "mac_association.h"
#define MAC_JOIN_RAM MAC_POLL_RAM
#define MAC_JOIN_VERSION 1u
#define MAC_JOIN_STOP_TIME 4096UL
#define MAC_JOIN_STOP_WORK 64u
#define MAC_JOIN_IDLE 0u
#define MAC_JOIN_PREPARE 1u
#define MAC_JOIN_REQUEST 2u
#define MAC_JOIN_WAIT 3u
#define MAC_JOIN_EXTRACT 4u
#define MAC_JOIN_RESTORE 5u
#define MAC_JOIN_DONE 6u
#define MAC_JOIN_FAULT 7u
#define MAC_JOIN_ACTION_NONE 0u
#define MAC_JOIN_ACTION_PREPARE 1u
#define MAC_JOIN_ACTION_TX 2u
#define MAC_JOIN_ACTION_RECEIVE 3u
#define MAC_JOIN_ACTION_CLOSE 4u
#define MAC_JOIN_ACTION_RESTORE 5u
#define MAC_JOIN_ACTION_RADIO 6u
#define MAC_JOIN_PREPARED 1u
#define MAC_JOIN_TX 2u
#define MAC_JOIN_FRAME 3u
#define MAC_JOIN_CLOSED 4u
#define MAC_JOIN_RESTORED 5u
#define MAC_JOIN_CANCEL 6u
#define MAC_JOIN_FAILURE 7u
#define MAC_JOIN_SOURCE 8u
#define MAC_JOIN_POLL_RESULT 1u
#define MAC_JOIN_REQUEST_RESULT 2u
#define MAC_JOIN_LOCAL_ABORT 3u
#define MAC_JOIN_CANCELLED 1u
#define MAC_JOIN_LIFETIME 2u
#define MAC_JOIN_WORK_LIMIT 3u
#define MAC_JOIN_CLOCK_ERROR 4u
#define MAC_JOIN_ADAPTER_ERROR 5u
#define MAC_JOIN_TX_ERROR 6u
#define MAC_JOIN_POLL_ERROR 7u
#define MAC_JOIN_CONTEXT_ERROR 8u
#define MAC_JOIN_CLEANUP_FAILED 9u
#define MAC_JOIN_OBS_NONE 0u
#define MAC_JOIN_OBS_STALE 1u
#define MAC_JOIN_OBS_DUPLICATE 2u
#define MAC_JOIN_UNCALLED 255u

typedef enum {
    MAC_JOIN_OK = 0, MAC_JOIN_INVALID, MAC_JOIN_STATE, MAC_JOIN_LIMIT, MAC_JOIN_UNSUPPORTED
} mac_join_result_t;

/* Confirmed logical adapter snapshot, not CC2530 register values.
 * filter 0=normal, 1=beacons; rx_on 0/1. No board defaults. */
typedef struct {
    uint16_t pan;
    uint8_t channel, filter, rx_on;
} mac_join_radio_t;

typedef struct {
    mac_poll_request_t extraction;
    mac_join_radio_t saved;
    uint8_t response_wait, capability, profile;
} mac_join_request_t;

typedef struct {
    mac_poll_record_t poll;
    mac_association_record_t association;
    uint32_t epoch, generation, request_ack;
    uint8_t result, stage, reason, error_stage, cleanup_error;
    uint8_t tx_outcome, tx_rc, poll_rc, poll_reason, poll_cleanup, association_rc, observation;
} mac_join_record_t;

/* SOURCE supplies an original adapter event for the Request's internal real
 * mac_tx_step. TX reports a granted POLL mac_tx_step input/result instead.
 * source.stamp is its actual captured physical event time, not stamp/now.
 * FRAME uses captured trailing end in stamp, nonwrapping receive serial and
 * CRC truth. PREPARED/CLOSED/RESTORED retire only the matching one-shot token.
 * RESTORED affirms all old preparation/RX/ACK/buffer rights retired, applicable
 * IFS finished/handed off, and the exact saved state physically restored.
 */
typedef mac_poll_event_t mac_join_event_t;

typedef struct {
    uint32_t epoch, generation, until;
    mac_tx_action_t radio;
    mac_join_radio_t state;
    uint16_t token;
    uint8_t kind, tx_cancel, observation;
} mac_join_action_t;

typedef struct {
    mac_poll_t poll;
    mac_association_t association;
    mac_join_request_t request;
    mac_join_record_t record;
    uint8_t outgoing[25], length;
    mac_tx_t MAC_JOIN_RAM *owner;
    uint32_t generation, tx_generation, last, deadline, wait_until, stop_at;
    uint16_t steps, token, issued_token, child_token;
    uint8_t version, phase, issued;
    uint8_t stopping, stop_steps, taken, window, restored, uncertain;
} mac_join_t;

/* Fresh memory/adapter epoch only; never recovery or radio restoration.
 * start leases an already initialized idle device-wide TX owner; DSN, generation
 * and IFS are never reset. Truthful unassociated local IEEE and permitting
 * coordinator selection are caller prerequisites. Only extended local source,
 * capability88/8C, page0 channels11..26 and explicit IEEE2006/R22 RX profile.
 * response_wait is caller-valid macResponseWaitTime (2..64), R=960*value.
 * extraction.frame_wait is caller-valid configured F, restricted to1..65534.
 * extraction.lifetime/work are LOCAL whole-attempt abort limits, not an
 * Association NO_DATA deadline; also bound the real extraction component.
 */
mac_join_result_t mac_join_init(mac_join_t MAC_JOIN_RAM * volatile ctx, volatile uint32_t now);
mac_join_result_t mac_join_start(mac_join_t MAC_JOIN_RAM * volatile ctx,
    mac_tx_t MAC_JOIN_RAM * volatile tx, const mac_join_request_t MAC_JOIN_RAM * volatile request, volatile uint32_t now);
/* RADIO contains one actual Request mac_tx_step action; SOURCE returns its real
 * adapter event. TX action grants one POLL mac_tx_step, then its exact report before
 * later-time events. tx_cancel requests a current-identity CANCEL. mac_tx_copy
 * is permitted; no other caller may use/reset the leased slot.
 * RECEIVE/CLOSE preserve the complete mac_poll continuous RX/ACK/drain contract.
 * All local events echo attempt epoch/generation; action completions echo token.
 * NULL polls; ignored input still consumes work. See MAC_JOIN.md.
 */
mac_join_result_t mac_join_step(mac_join_t MAC_JOIN_RAM * volatile ctx, mac_tx_t MAC_JOIN_RAM * volatile tx,
    uint32_t now, const mac_join_event_t MAC_JOIN_RAM * volatile event, mac_join_action_t MAC_JOIN_RAM * volatile action);
mac_join_result_t mac_join_take(mac_join_t MAC_JOIN_RAM *ctx, mac_join_record_t MAC_JOIN_RAM *record);
mac_join_result_t mac_join_release(mac_join_t MAC_JOIN_RAM *ctx, mac_tx_t MAC_JOIN_RAM *tx);

/* Disjoint ordinary storage only; persistent contexts cannot move while leased.
 * Foreground/nonreentrant with all real dependencies. Frame/ACK spans may be
 * CODE/RAM; never retain them beyond their call/report. Time is abstract uint32
 * 16-us symbols with true compared intervals/gaps <2^31 and no hidden wraps.
 * API errors preserve context/TX/outputs. Processed failures retain real reasons.
 * FAULT keeps ownership. Metadata is neither membership/PIB installation nor
 * Response ACK completion, authentication or complete MLME confirmation.
 */
#endif
