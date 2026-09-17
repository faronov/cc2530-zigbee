/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef RADIO_FIFO_H
#define RADIO_FIFO_H

#include "cc2530_mmio.h"

#define RADIO_FIFO_BODY_MAX 125u
#define RADIO_FIFO_RX_FLUSH 1u
#define RADIO_FIFO_TX_FLUSH 2u

typedef enum {
    RADIO_FIFO_OK = 0,
    RADIO_FIFO_EMPTY,
    RADIO_FIFO_INVALID_ARGUMENT,
    RADIO_FIFO_UNSUPPORTED_STATE,
    RADIO_FIFO_BUSY,
    RADIO_FIFO_NOT_EMPTY,
    RADIO_FIFO_CONTROLLER_ERROR,
    RADIO_FIFO_COUNT_ERROR,
    RADIO_FIFO_TIMEOUT,
    RADIO_FIFO_POLL_LIMIT,
    RADIO_FIFO_TIMEBASE_ERROR,
    RADIO_FIFO_COUNTER_RANGE,
    RADIO_FIFO_STATE_CHANGED
} radio_fifo_result_t;

typedef struct {
    uint32_t elapsed_ticks;
    uint16_t polls;
    uint8_t timebase_status, strobes, confirmed;
    uint8_t bytes_written, bytes_verified, errors, rx_count, tx_count;
    uint8_t rx_first, rx_last, rx_packet, tx_first, tx_last, fifo_signals, sample_valid;
} radio_fifo_diagnostics_t;

/* Awake init-time, exclusive foreground owner; NOT ISR-reentrant.
 * Caller must KNOW radio/CSP/DMA are quiescent, with no scheduled/pending work
 * or prior sleep/wake/time discontinuity. Samples cannot prove that history.
 * Requires all IEN bytes zero, awake SLEEPCMD, stable undivided XOSC32 CMD/STA,
 * FRMCTRL0=40 (AUTOCRC, no AUTOACK/test modes), FRMCTRL1=01 and idle radio/CSP.
 * No clock, IRQ/mask/flag, DMA, address RAM or RF-enable writes.
 *
 * 0 < timeout_ticks < 800000, poll_limit > 0, diagnostics writable/non-null.
 * All supplied objects must be valid disjoint ordinary RAM/CODE objects, not
 * MMIO; diagnostics must be in XDATA and must not overlap body. The body uses
 * a generic pointer so that both ordinary XDATA and CODE input are supported.
 * Invalid arguments: no MMIO and output unchanged. Other returns initialize
 * diagnostics. sample_valid means the FIFO fields form a complete observation;
 * it is cleared before each observation, including rejected entry.
 *
 * Clear checks both FIFOs, issuing at most one ED and one EE, each only if
 * counts/pointers/status need clearing; verifies one before issuing the next.
 * EMPTY means both were already observably reset, with no write/timer sample.
 * Latched RFERRF errors and FIFO=0/FIFOP=1 RX overflow are rejected, NEVER
 * acknowledged/erased by this API (SWRU191F pp.211,233).
 * Clear resets FIFO pointers/contents, not the radio/CSP or interrupt history.
 *
 * Preload requires a reset-empty TXFIFO; no implicit flush. body_length 1..125
 * excludes PHR and FCS. Writes PHR=body_length+2 then body, not the FCS.
 * Verifies TX count/first/last after EVERY byte and preserves observed RX state.
 * OK means bytes accepted into FIFO, not transmitted/authenticated/valid MAC.
 *
 * One whole-operation raw deadline and poll cap; equality times out. Counts
 * exclude the entry/start sample. At most poll_limit polls and length+1 writes;
 * bytes_verified excludes late/unconfirmed writes. strobes/confirmed are masks
 * of issued/verified flushes, not proof of hidden work cancellation.
 * On failure, effects can be partial/unconfirmed: stop initialization; no
 * retry, rollback, implicit flush or RF-off cleanup. Reestablish ownership and
 * explicitly recover before reuse; latched errors need separate recovery.
 * Bounds require an executing CPU and the timebase half-range contract.
 */
radio_fifo_result_t radio_fifo_clear_init(uint32_t timeout_ticks, uint16_t poll_limit,
                                         radio_fifo_diagnostics_t MCU_XDATA *diagnostics);
radio_fifo_result_t radio_fifo_preload_init(const uint8_t *body, uint16_t body_length,
                                           uint32_t timeout_ticks, uint16_t poll_limit,
                                           radio_fifo_diagnostics_t MCU_XDATA *diagnostics);

#endif
