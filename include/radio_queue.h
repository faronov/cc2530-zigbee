/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef RADIO_QUEUE_H
#define RADIO_QUEUE_H

#include "radio_rx.h"

#define RADIO_QUEUE_RX_SLOTS 2u
#define RADIO_QUEUE_TX_SLOTS 1u
#define RADIO_QUEUE_EVENTS 4u

typedef enum {
    RADIO_QUEUE_OK = 0, RADIO_QUEUE_INVALID_ARGUMENT, RADIO_QUEUE_BUFFER_OWNERSHIP,
    RADIO_QUEUE_EMPTY, RADIO_QUEUE_FULL, RADIO_QUEUE_IRQ_ACTIVE, RADIO_QUEUE_BAD_CRC,
    RADIO_QUEUE_RADIO_FAILED, RADIO_QUEUE_CORRUPT, RADIO_QUEUE_IRQ_FAILED
} radio_queue_result_t;

typedef struct {
    uint8_t length, body[RADIO_RX_BODY_MAX];
} radio_queue_tx_frame_t;

typedef struct {
    uint8_t fault, driver_result, last_cookie, requests, dropped, cancelled;
    uint8_t rx_count, tx_count, bad_crc;
} radio_queue_status_t;

#if defined(__SDCC)
#define RADIO_QUEUE_ISR_ABI __reentrant
#else
#define RADIO_QUEUE_ISR_ABI
#endif

/* Four opaque RX-request cookies, reject-new on full; dropped saturates at255.
 * The only ISR-callable entry point: reentrant, short EA-protected publication,
 * no packet copy, RF access, Sleep Timer or foreground/helper scratch.
 * A cookie is a scheduling hint, NOT evidence of a received frame or IRQ source.
 * Standard ABI-correct ISR context preservation and bounded nesting are required.
 */
radio_queue_result_t radio_queue_request_rx(uint8_t cookie) RADIO_QUEUE_ISR_ABI;
#undef RADIO_QUEUE_ISR_ABI

/* All remaining functions are foreground/non-reentrant. No pool pointer or
 * lease escapes: submit copies in, read copies out and only then releases.
 * Exactly two RX frames and one pending TX candidate; reject-new on pressure.
 * Full/empty/invalid calls preserve queued objects and caller output. Output
 * tails past length remain unchanged. All caller objects must be ordinary
 * persistent XDATA after radio_queue_reserved_end, outside runtime scratch.
 * Link timebase, radio_rx, irq, this module, then callers; prove the full prefix.
 *
 * Service consumes at most ONE event and attempts at most ONE bounded passive
 * reception. A full RX pool leaves the event pending and touches no RF.
 * Valid channel/time/poll bounds and all IEN0/1/2=0 are required before dequeue;
 * IRQ_ACTIVE does not consume the request or latch the underlying RX driver.
 * Existing passive-RX reset/clock/CSP/DMA/history requirements still apply.
 * BAD_CRC consumes the event, frees the filling slot and permits another request.
 * Other RX failures retain their cause and block service/notification/TX submit without
 * further MMIO. No automatic flush, retry, reset or fault-clear API is added.
 *
 * Existing queued data may still be read/cancelled after a radio fault. Snapshot
 * and explicit event cancellation use short EA critical sections, never RF.
 * TX read/cancel only transfers/releases bytes; NONE of these APIs transmits,
 * acknowledges, authenticates or promises continuous/lossless reception.
 */
radio_queue_result_t radio_queue_service(uint8_t channel, uint32_t timeout, uint16_t limit);
radio_queue_result_t radio_queue_rx_read(radio_rx_frame_t MCU_XDATA * volatile output);
radio_queue_result_t radio_queue_tx_submit(const uint8_t MCU_XDATA * volatile body, uint8_t length);
radio_queue_result_t radio_queue_tx_read(radio_queue_tx_frame_t MCU_XDATA * volatile output);
radio_queue_result_t radio_queue_tx_cancel(void);
radio_queue_result_t radio_queue_cancel_requests(void);
radio_queue_result_t radio_queue_snapshot(radio_queue_status_t MCU_XDATA * volatile output);

#endif
