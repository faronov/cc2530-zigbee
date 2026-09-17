/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef CC2530_DMA_H
#define CC2530_DMA_H

#include "cc2530_mmio.h"

#define DMA_COPY_MAX 16u

typedef enum {
    DMA_OK = 0,
    DMA_INVALID_ARGUMENT,
    DMA_INVALID_RANGE,
    DMA_BUFFER_OWNERSHIP,
    DMA_UNSUPPORTED_STATE,
    DMA_BUSY,
    DMA_PENDING,
    DMA_STATE_CHANGED,
    DMA_TIMEOUT,
    DMA_POLL_LIMIT,
    DMA_TIMEBASE_ERROR,
    DMA_COUNTER_RANGE
} dma_result_t;

#define DMA_CONFIGURED 1u
#define DMA_ARMED 2u
#define DMA_REQUESTED 4u
#define DMA_ACKNOWLEDGED 8u

typedef struct {
    uint32_t elapsed_ticks;
    uint16_t polls;
    uint8_t timebase_status, actions, complete, verified;
    uint8_t arm, request, irq, ircon, cfg0_low, cfg0_high, cfg1_low, cfg1_high;
    uint8_t sample_valid;
} dma_diagnostics_t;

/* Foreground/non-reentrant, awake, stable undivided RC16 or XOSC32, all IENs=0.
 * BEFORE any call: debug DMA_PAUSE must be clear (or no debug session), with
 * exclusive DMA ownership since full reset or only this service's TRIG0 use.
 * No other armed/in-flight/scheduled work, ISR, debugger or descriptor writer.
 * Register snapshots cannot establish these history/debug preconditions.
 *
 * Copies 1..16 bytes using channel 0, fixed byte BLOCK/TRIG0/+1/+1/assured,
 * IRQMASK=0. timeout is positive and < half the raw 24-bit timer range; limit
 * is a positive whole-operation poll cap. Five polls suffice only for the
 * immediate synthetic completion path, not a silicon timing guarantee.
 * Validate wider external values before converting to these fixed-width types.
 *
 * Addresses identify caller-owned, explicitly allocated persistent XDATA
 * buffers, not compiler scratch or an implicit free-RAM pool. Entire buffers
 * and XDATA diagnostics must lie below 1E00, beyond the linked private prefix,
 * and be pairwise disjoint. No IRAM alias, peripheral, CODE or status access.
 * Link timebase before dma and all eligible objects after dma_reserved_end;
 * the linked checker must prove driver/timebase allocations below that end.
 * The separate SDCC generic-store helper scratch byte is also excluded.
 *
 * Argument/range/ownership errors make no MMIO or diagnostic stores; valid
 * caller-owned outputs are unchanged. Normal C ABI scratch writes still occur.
 * Any other error latches its ORIGINAL result. Later calls return it before
 * MMIO or diagnostic/descriptor modification (even with invalid arguments).
 * There is deliberately no retry/abort/release/reset API. Reinitializing C
 * state alone is NOT recovery; a genuinely established full SoC reset is.
 *
 * After ARM, an error is NOT quiescence: DMA can still change destination
 * after return. Descriptor and both buffers remain valid, source immutable,
 * destination unreused until full-reset recovery. No partial byte count is
 * available. actions records issued operations; complete records a fresh
 * complete/disarmed/request-clear observation even when late; verified is
 * set only after timely completion AND checked owned acknowledgment.
 * Only success releases the buffers. A raw timer cannot detect missed full
 * wraps/reset; caller observation/history must remain within half range.
 */
dma_result_t dma_copy_init(uint16_t source, uint16_t destination, uint8_t length,
                          uint32_t timeout, uint16_t limit,
                          dma_diagnostics_t MCU_XDATA *diagnostics);

#endif
