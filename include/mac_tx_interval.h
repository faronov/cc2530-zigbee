/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef MAC_TX_INTERVAL_H
#define MAC_TX_INTERVAL_H
#include "mac_tx.h"
#include "mac_epoch.h"

#if defined(CC2530_MAC_INTERVAL)
#define MAC_TX_EVENT_SENT_INTERVAL 8u
#define MAC_TX_EVENT_ACK_INTERVAL 9u
#define MAC_TX_EVENT_RX_CLOSED 10u
#define MAC_TX_ACTION_COLLECT 4u
#define MAC_TX_TIMING_UNCERTAIN 11u

/* An explicit alternative to the captured-end API, not a timestamp adapter.
 * engine is allocation/diagnostic storage only: NEVER pass it to mac_tx_step,
 * mac_poll or mac_join. engine.tx_end is unused by this profile. The ordinary
 * API and its exact-event contract are unchanged.
 */
typedef struct {
    mac_tx_t engine;
    mac_epoch_stamp_t tx_lower, tx_upper;
} mac_tx_interval_t;

/* RANDOM/BUSY/QUIESCED/FAILURE/CANCEL retain their ordinary source contracts.
 * SENT_INTERVAL and ACK_INTERVAL use source.stamp as report time, NOT PHY end.
 * Their actual end lies in [lower,upper], fine=0..511. Bounds and report are
 * in the same continuous epoch; every true compared gap is below 2^31 symbols.
 * SENT_INTERVAL proves a unique own completion, with RX already armed.
 * ACK_INTERVAL additionally proves reception AFTER that actual TX, CRC validity
 * and original FCS-free bytes. Numeric interval membership cannot prove those
 * facts. The lower bound may equal tx_lower when causality supplies strictness.
 * RX_CLOSED uses upper as the loss-free drained receive watermark: all frames
 * through upper have been reported. It does not mean physical radio stop.
 * All events echo generation/retry/nb. Never forward these sources to the old
 * exact-event API, even after rounding. Legacy SENT/ACK inputs are rejected.
 */
typedef struct {
    mac_tx_event_t source;
    mac_epoch_stamp_t lower, upper;
} mac_tx_interval_event_t;

/* COLLECT is emitted once after a confirmed ACK-requesting transmission.
 * through is tx_upper+54, inclusive: an EMPTY sample/elapsed time cannot close
 * it. The adapter owns continuous reception, preservation and loss reporting.
 * QUIESCE remains mandatory for success, failure and retry; its old contract
 * and limits are unchanged. at/ready_at use upward-rounded bounds for IFS.
 */
typedef struct {
    mac_tx_action_t control;
    mac_epoch_stamp_t through;
} mac_tx_interval_action_t;

mac_tx_result_t mac_tx_interval_init(mac_tx_interval_t *tx, uint8_t dsn, uint32_t now);
mac_tx_result_t mac_tx_interval_submit(mac_tx_interval_t *tx,
    const uint8_t *body, uint16_t length, uint32_t now, uint32_t lifetime, uint16_t work);
mac_tx_result_t mac_tx_interval_copy(const mac_tx_interval_t *tx,
    uint8_t *body, uint16_t capacity, uint8_t *length);
mac_tx_result_t mac_tx_interval_step(mac_tx_interval_t * volatile tx, uint32_t now,
    const mac_tx_interval_event_t * volatile event, mac_tx_interval_action_t * volatile action);
mac_tx_result_t mac_tx_interval_release(mac_tx_interval_t *tx);

/* now/report are symbol-boundary observations no earlier than upper (ceil).
 * A definitely timely matching ACK can establish ACKED. A matching or wrong-DSN
 * ACK whose timing straddles the window produces local TIMING_UNCERTAIN, not
 * ACKED/NO_ACK. Definitely late and malformed ACKs do not establish delivery.
 * NO_ACK without a received wrong-DSN ACK requires RX_CLOSED through the latest
 * possible ACK deadline. Cleanup does not replace that reception evidence.
 * All original admission/DSN/CCA/retry/lifetime/work/retained-fault rules apply.
 * One serialized foreground owner, disjoint ordinary storage, no reentrancy.
 * Init is memory initialization, never hardware recovery. API errors are atomic.
 */
#endif
#endif
