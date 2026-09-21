/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef RADIO_NOISE_H
#define RADIO_NOISE_H

#include "cc2530_mmio.h"

#define RADIO_NOISE_SAMPLES_MAX 1024u
#define RADIO_NOISE_BYTES 128u

typedef enum {
    RADIO_NOISE_OK = 0, RADIO_NOISE_INVALID_ARGUMENT, RADIO_NOISE_INVALID_RANGE,
    RADIO_NOISE_BUFFER_OWNERSHIP, RADIO_NOISE_UNSUPPORTED_STATE,
    RADIO_NOISE_STATE_CHANGED, RADIO_NOISE_BUSY, RADIO_NOISE_FIFO_ERROR,
    RADIO_NOISE_CONTROLLER_ERROR, RADIO_NOISE_TIMEOUT, RADIO_NOISE_WORK_LIMIT,
    RADIO_NOISE_TIME_ERROR, RADIO_NOISE_BAD_SAMPLE, RADIO_NOISE_ALREADY_USED
} radio_noise_result_t;

typedef struct {
    uint32_t timeout;
    uint16_t samples, interval, limit;
    uint8_t channel;
} radio_noise_request_t;

typedef struct {
    uint32_t elapsed_ticks, first_before, last_after, min_gap, max_gap, max_span;
    uint16_t samples, timed_samples, polls;
    uint8_t phase, writes, verified, actions, timebase_status;
    uint8_t rx_enable, calibration, signals, rssi_valid, errors, flags0, flags1, last_raw;
    uint8_t data[RADIO_NOISE_BYTES];
} radio_noise_capture_t;

/* One capture per independently established full-reset epoch; no board caller.
 * Foreground/non-reentrant, known exclusive radio/CSP/DMA/clock/ST0 ownership
 * since reset; awake undivided XOSC32, IRQs/RF masks/DMA off, empty reset FIFOs.
 * No normal RX/TX/AUTOACK, out-of-band writer, sleep or reset may interleave.
 * Observations cannot establish that history or guarantee source freshness.
 *
 * channel=11..26, samples=1..1024, interval=1..65535 raw Sleep Timer ticks,
 * timeout=1..7FFFFF raw ticks, limit=1..65535 observation polls, globally across
 * configuration, RSSI-valid warm-up, sampling and confirmed soft stop.
 * interval is a minimum between the preceding post-read observation and the
 * next pre-read observation, not calibrated time or independent-sample proof.
 * All timer separations must remain below half-range without hidden wraps.
 *
 * Both objects are complete, disjoint, persistent ordinary XDATA below1E00.
 * Link timebase, other service-private objects, this driver, then callers.
 * The objects must lie strictly after radio_noise_reserved_end and entirely
 * before the linked libc scratch suffix; prove this for each composition.
 * Wider values must be validated before conversion; caller inputs stay fixed.
 *
 * Configures FRMCTRL0=4C (RX_MODE11/no symbol search, AUTOACK=0, TX_MODE00),
 * enables RX with E3, waits for lock/RSSI-valid, and reads RFRND61A7 exactly
 * once per sample. Samples are IRND bit0, packed first-sample/low-bit first.
 * QRND is ignored; no value filtering, health test, conditioning or RNG output.
 * Only RXMASKCLR80 requests stop; no FIFO read/flush, TX strobe or retry.
 *
 * Invalid argument/range/ownership leaves output and MMIO unchanged.
 * After admission, capture is zeroed and records progress, including errors.
 * samples includes every raw read, even an offending last_raw; timed_samples
 * counts only reads with successful post-observation. Only samples bits are
 * meaningful; unused bits/bytes are zero. Timing is bracket metadata, not
 * captured ADC time. A partial/error capture is diagnostic data, never OK.
 * OK requires complete sampling and confirmed idle/empty stop (phase5).
 * First operational error is retained; re-entry performs no MMIO/publication.
 * RX may remain active after error; no automatic stop/repair/reset occurs.
 * Successful re-entry returns ALREADY_USED without effects, retaining ownership.
 * Every outcome is raw diagnostic evidence, never entropy qualification.
 */
radio_noise_result_t radio_noise_collect(const radio_noise_request_t MCU_XDATA *request,
                                          radio_noise_capture_t MCU_XDATA *capture);

#endif
