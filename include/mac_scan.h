/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef MAC_SCAN_H
#define MAC_SCAN_H

#include "mac_tx.h"
#include "nwk_candidates.h"

/* Caller records live in ordinary XDATA on SDCC, never the IRAM alias/MMIO.
 * This address-space qualifier is not a physical-pointer validity check.
 */
#ifdef __SDCC
#define MAC_SCAN_RAM __xdata
#else
#define MAC_SCAN_RAM
#endif

#define MAC_SCAN_VERSION 1u
#define MAC_SCAN_MAX_LIFETIME UINT32_C(0x10000000)
#define MAC_SCAN_MAX_WORK 4096u
#define MAC_SCAN_TX_SYMBOLS 4096u
#define MAC_SCAN_TX_STEPS 64u
#define MAC_SCAN_STOP_SYMBOLS 4096u
#define MAC_SCAN_STOP_STEPS 64u
#define MAC_SCAN_CLOSE_GRACE 1024u

#define MAC_SCAN_IDLE 0u
#define MAC_SCAN_CONFIG 1u
#define MAC_SCAN_SUBMIT 2u
#define MAC_SCAN_TX 3u
#define MAC_SCAN_OPEN 4u
#define MAC_SCAN_RX 5u
#define MAC_SCAN_RESTORE 6u
#define MAC_SCAN_DONE 7u
#define MAC_SCAN_FAULT 8u

#define MAC_SCAN_ACTION_NONE 0u
#define MAC_SCAN_ACTION_CONFIG 1u
#define MAC_SCAN_ACTION_TX 2u
#define MAC_SCAN_ACTION_RECEIVE 3u
#define MAC_SCAN_ACTION_RESTORE 4u

#define MAC_SCAN_EVENT_CONFIGURED 1u
#define MAC_SCAN_EVENT_TX 2u
#define MAC_SCAN_EVENT_OPENED 3u
#define MAC_SCAN_EVENT_BEACON 4u
#define MAC_SCAN_EVENT_CLOSED 5u
#define MAC_SCAN_EVENT_RESTORED 6u
#define MAC_SCAN_EVENT_FAILURE 7u
#define MAC_SCAN_EVENT_CANCEL 8u

#define MAC_SCAN_FILTER_NORMAL 0u
#define MAC_SCAN_FILTER_BEACONS 1u

#define MAC_SCAN_REASON_NONE 0u
#define MAC_SCAN_FINISHED 1u
#define MAC_SCAN_CANCELLED 2u
#define MAC_SCAN_LIFETIME 3u
#define MAC_SCAN_WORK_LIMIT 4u
#define MAC_SCAN_CLOCK_ERROR 5u
#define MAC_SCAN_ADAPTER_ERROR 6u
#define MAC_SCAN_TX_ERROR 7u
#define MAC_SCAN_CLOSE_MISSING 8u
#define MAC_SCAN_CLEANUP_FAILED 9u

typedef enum {
    MAC_SCAN_OK = 0,
    MAC_SCAN_INVALID,
    MAC_SCAN_STATE,
    MAC_SCAN_EXHAUSTED,
    MAC_SCAN_BAD_INDEX
} mac_scan_result_t;

/* Logical owned state, NOT CC2530 register values. Entry snapshot must already
 * be confirmed by the exclusive adapter owner, with no old operations pending.
 */
typedef struct {
    uint16_t pan;
    uint8_t channel, filter, rx_on;
} mac_scan_radio_state_t;

typedef struct {
    uint32_t channels, lifetime;
    uint16_t work;
    uint8_t duration;
    mac_scan_radio_state_t saved;
} mac_scan_request_t;

typedef struct {
    uint32_t generation, stamp;
    uint16_t token;
    uint8_t kind, crc_valid, tx_result;
    const uint8_t *body;
    uint16_t length;
    mac_scan_radio_state_t state;
} mac_scan_event_t;

typedef struct {
    uint32_t generation, until, dwell;
    mac_scan_radio_state_t state;
    uint16_t token;
    uint8_t kind, tx_cancel, observed, candidate_result;
} mac_scan_action_t;

/* Public for static allocation only; do not edit fields. No borrowed frame
 * storage. owner refers only to the caller's persistent device-wide DSN owner.
 */
typedef struct {
    nwk_candidates_t candidates;
    mac_tx_t MAC_SCAN_RAM *owner;
    mac_scan_radio_state_t saved;
    uint32_t generation, tx_generation, last, deadline, dwell, window_start;
    uint32_t window_end, remaining, unscanned, sent;
    uint16_t steps, token;
    uint8_t version, phase, channel, issued, reason, cleanup_error;
    uint8_t uncertain, overflow, stopping, stop_steps, tx_outcome;
} mac_scan_t;

/* Fresh storage/epoch only, never recovery of an outstanding lease or TX fault. */
mac_scan_result_t mac_scan_init(mac_scan_t MAC_SCAN_RAM * volatile scan);
/* Only page0, unsecured active scanning, duration0..14, nonempty channels11..26.
 * saved is a truthful confirmed snapshot; no hardware operation occurs here.
 * tx must be the initialized, idle, single device-wide transmitter. Never reset
 * its DSN/generation to start a scan. The lease lasts until successful release.
 */
mac_scan_result_t mac_scan_start(mac_scan_t MAC_SCAN_RAM * volatile scan,
                                 mac_tx_t MAC_SCAN_RAM * volatile tx,
                                 const mac_scan_request_t MAC_SCAN_RAM * volatile request,
                                 uint32_t volatile now);
/* NULL event polls. One-shot actions require real confirmations; see MAC_SCAN.md.
 * Step owns TX submit/release, never initialization. ACTION_TX delegates exactly
 * one serialized foreground mac_tx_step to the caller, using real source events
 * or a current-identity CANCEL if tx_cancel is set. Execute its real TX action,
 * then report the API result as EVENT_TX at that foreground call's time (NOT a
 * captured PHY timestamp). A pending physical action may complete later.
 * Echo the scan generation/token. Do not call mac_tx_step without this grant.
 * mac_tx_copy remains permitted. See MAC_SCAN.md for the complete bridge.
 * Time is the same abstract uint32 symbol epoch as mac_tx, NOT a live timer read
 * substituted for an event capture. Deliver ordered events before advancing the
 * foreground watermark. No gap or transaction interval may reach half-range.
 */
mac_scan_result_t mac_scan_step(mac_scan_t MAC_SCAN_RAM * volatile scan,
                                mac_tx_t MAC_SCAN_RAM * volatile tx,
                                uint32_t volatile now,
                                const mac_scan_event_t MAC_SCAN_RAM * volatile event,
                                mac_scan_action_t MAC_SCAN_RAM * volatile action);
/* Copies available preliminary candidates only after DONE/FAULT, before release. */
mac_scan_result_t mac_scan_get(const mac_scan_t MAC_SCAN_RAM * volatile scan, uint8_t index,
                               nwk_candidate_t MAC_SCAN_RAM * volatile result);
mac_scan_result_t mac_scan_release(mac_scan_t MAC_SCAN_RAM * volatile scan,
                                   mac_tx_t MAC_SCAN_RAM * volatile tx);

/* All arguments denote accessible disjoint ordinary caller objects, never
 * invented MMIO/compiler-private/status/alias pointers. All storage, including
 * the bound tx, lives for the entire lease. Serialized foreground/non-reentrant
 * with both codecs and transmitter. No callbacks/heap/ISR/radio adapter.
 * API errors preserve state/outputs. OK step may advance time/work even for
 * stale input; inspect phase/reason, not OK. FAULT retains ownership. A logical
 * reason is not physical quiescence. Only confirmed restoration permits DONE.
 */
#endif
