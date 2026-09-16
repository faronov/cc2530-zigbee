/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef TIMEBASE_H
#define TIMEBASE_H

#include <stdbool.h>
#include <stdint.h>

#define TIMEBASE_TICKS_MASK 0x00ffffffUL
#define TIMEBASE_HALF_RANGE 0x00800000UL

typedef enum {
    TIMEBASE_OK = 0,
    TIMEBASE_INVALID_ARGUMENT,
    TIMEBASE_AMBIGUOUS
} timebase_result_t;

/* Raw current-source Sleep Timer ticks, zero-extended to 32 bits, not wall time.
 * Awake-only: no PM1/PM2 wake synchronization or PM3/reset continuity.
 * One foreground owner; no ISR/other reader may interleave ST0 reads.
 * Does not write MMIO, mask interrupts, select clocks or wait for an edge.
 */
uint32_t timebase_read_awake_ticks24(void);

/* Foreground-only, not ISR-reentrant on SDCC. Outputs are unchanged on error.
 * All ticks must be 24-bit, delay < HALF_RANGE, and output pointers non-null.
 * Expiry requires true separation strictly below HALF_RANGE; the exactly-half
 * modular delta returns AMBIGUOUS. Zero delay expires immediately.
 * Caller must observe within that window: missed full wraps/reset and an
 * out-of-window true separation cannot be detected from these raw values.
 */
timebase_result_t timebase_deadline_after(uint32_t now, uint32_t delay,
                                          uint32_t *deadline);
timebase_result_t timebase_expired(uint32_t now, uint32_t deadline, bool *expired);

#endif
