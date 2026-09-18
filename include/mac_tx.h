/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef MAC_TX_H
#define MAC_TX_H

#include <stdint.h>
#include "mac_frame.h"

/* IEEE 802.15.4-2006, selected nonbeacon-enabled 2450 MHz O-QPSK subset.
 * Time is an adapter-provided uint32_t SYMBOL counter (one unit = 16 us),
 * NOT Sleep Timer ticks. See docs/MAC_TX.md for mandatory adapter contracts.
 */
#define MAC_TX_HALF UINT32_C(0x80000000)
#define MAC_TX_BACKOFF_SYMBOLS 20u
#define MAC_TX_ACK_SYMBOLS 54u
#define MAC_TX_MIN_BE 3u
#define MAC_TX_MAX_BE 5u
#define MAC_TX_MAX_BACKOFFS 4u
#define MAC_TX_MAX_RETRIES 3u
#define MAC_TX_STOP_SYMBOLS 1024u
#define MAC_TX_STOP_STEPS 16u

#define MAC_TX_IDLE 0u
#define MAC_TX_DRAW 1u
#define MAC_TX_DRAW_WAIT 2u
#define MAC_TX_RADIO 3u
#define MAC_TX_ACK_WAIT 4u
#define MAC_TX_STOPPING 5u
#define MAC_TX_DONE 6u
#define MAC_TX_FAULT 7u

#define MAC_TX_ACTION_NONE 0u
#define MAC_TX_ACTION_RANDOM 1u
#define MAC_TX_ACTION_ATTEMPT 2u
#define MAC_TX_ACTION_QUIESCE 3u

#define MAC_TX_EVENT_RANDOM 1u
#define MAC_TX_EVENT_BUSY 2u
#define MAC_TX_EVENT_SENT 3u
#define MAC_TX_EVENT_ACK 4u
#define MAC_TX_EVENT_QUIESCED 5u
#define MAC_TX_EVENT_FAILURE 6u
#define MAC_TX_EVENT_CANCEL 7u

#define MAC_TX_OUTCOME_NONE 0u
#define MAC_TX_ACKED 1u
#define MAC_TX_UNACKNOWLEDGED 2u
#define MAC_TX_CHANNEL_ACCESS 3u
#define MAC_TX_NO_ACK 4u
#define MAC_TX_CANCELLED 5u
#define MAC_TX_LIFETIME 6u
#define MAC_TX_WORK_LIMIT 7u
#define MAC_TX_CLOCK_ERROR 8u
#define MAC_TX_ADAPTER_ERROR 9u
#define MAC_TX_STOP_FAILED 10u

typedef enum {
    MAC_TX_OK = 0,
    MAC_TX_INVALID,
    MAC_TX_FULL,
    MAC_TX_UNSUPPORTED,
    MAC_TX_SPACE,
    MAC_TX_STATE,
    MAC_TX_GENERATION_EXHAUSTED
} mac_tx_result_t;

/* Caller-owned persistent storage; fields are public for allocation/diagnostics,
 * not mutation. One foreground owner, no ISR/reentrancy. No packed wire ABI.
 * init requires a fresh adapter epoch with all old actions/events purged.
 */
typedef struct {
    uint8_t frame[MAC_FRAME_MAX_BODY];
    uint32_t last, deadline, at, tx_end, ready_at, generation, stop_at;
    uint16_t steps;
    uint8_t phase, next_dsn, length, ack_requested, nb, be, retries;
    uint8_t outcome, transmissions, uncertain, pending, retry_pending, stop_steps;
} mac_tx_t;

/* RANDOM supplies an independent uniform byte in value (not security entropy).
 * ACK supplies exactly the CRC-validated, FCS-free legacy body in bytes/length.
 * stamp is the actual physical event time, not the foreground processing time.
 * All events echo generation/retry/nb from their action. CANCEL uses the same
 * current identity. QUIESCED means no future TX or buffer access for that action.
 */
typedef struct {
    uint32_t generation, stamp;
    const uint8_t *bytes;
    uint16_t length;
    uint8_t kind, retry, nb, value;
} mac_tx_event_t;

/* Actions are one-shot, never repeated by poll. ATTEMPT schedules one CCA at at,
 * and immediate TX iff clear; it is NOT permission to send after an old CCA.
 * Adapter copies the immutable body with mac_tx_copy before executing.
 * SENT must mean confirmed PHY completion and RX already armed if requested.
 * QUIESCE must be acknowledged even after logical success or timeout.
 */
typedef struct {
    uint32_t generation, at, until;
    uint8_t kind, retry, nb, length, ack_requested;
    uint8_t phase, outcome, transmissions, uncertain, pending;
} mac_tx_action_t;

/* Memory initialization only, never radio recovery. NULL returns INVALID
 * without mutation; valid fresh-epoch storage is initialized and returns OK.
 */
mac_tx_result_t mac_tx_init(mac_tx_t * volatile tx, uint8_t random_dsn, uint32_t now);
/* Direct DATA or canonical Beacon Request command only. Request transmission
 * does not scan a channel, receive a Beacon or establish a network.
 */
mac_tx_result_t mac_tx_submit(mac_tx_t * volatile tx,
                              const uint8_t * volatile body, uint16_t length,
                              uint32_t now, uint32_t lifetime, uint16_t work_limit);
mac_tx_result_t mac_tx_copy(const mac_tx_t * volatile tx,
                            uint8_t * volatile body, uint16_t capacity,
                            uint8_t * volatile length);
mac_tx_result_t mac_tx_step(mac_tx_t * volatile tx, uint32_t now,
                            const mac_tx_event_t * volatile event,
                            mac_tx_action_t * volatile action);
mac_tx_result_t mac_tx_release(mac_tx_t *tx);

/* NULL event polls. Invalid arguments and admission/copy/release errors leave
 * state and outputs unchanged. A successful step may advance timers/work even
 * for stale/duplicate/unrelated input; inspect phase/outcome, not just OK.
 * No completion callback, queue dequeue, syntax parse or API OK proves delivery.
 * Input/output objects must be disjoint and valid for their declared sizes.
 * FAULT retains the slot; never reinitialize it to bypass real radio recovery.
 */
#endif
