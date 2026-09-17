/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef CLOCK_H
#define CLOCK_H

#include <stdint.h>

typedef enum {
    CLOCK_RC16 = 0,
    CLOCK_XOSC32
} clock_source_t;

typedef enum {
    CLOCK_OK = 0,
    CLOCK_INVALID_ARGUMENT,
    CLOCK_UNSUPPORTED_STATE,
    CLOCK_TIMEOUT,
    CLOCK_POLL_LIMIT,
    CLOCK_TIMEBASE_ERROR,
    CLOCK_COUNTER_RANGE,
    CLOCK_COMMAND_CHANGED,
    CLOCK_NOT_ATTEMPTED,
    CLOCK_ROLLBACK_UNCONFIRMED
} clock_result_t;

typedef struct {
    uint32_t elapsed_ticks;
    uint16_t polls;
    uint8_t timebase_status;
} clock_wait_diagnostics_t;

typedef struct {
    clock_wait_diagnostics_t request;
    clock_wait_diagnostics_t rollback;
    uint8_t saved_command;
    uint8_t requested_command;
    uint8_t observed_command;
    uint8_t observed_status;
    uint8_t rollback_result;
} clock_diagnostics_t;

/* Init-time, awake foreground only; not ISR-reentrant. Caller must exclude all
 * other CLKCONCMD writers/ST0 readers and any prior sleep/wake discontinuity.
 * Requires IEN0/1/2=0, SLEEPCMD.MODE=0 with reserved bit 2 set, stable matching
 * CMD/STA and undivided SYS. Caller must independently exclude pending requests;
 * matching CMD/STA cannot establish that. Does not repair unsupported state.
 *
 * timeout_ticks < 0x800000, poll_limit > 0, diagnostics writable/non-null.
 * Invalid arguments cause no MMIO and leave diagnostics unchanged.
 * A stable requested source succeeds without writes or timer reads.
 * Otherwise preserve LF/TICKSPD command fields and request once. Confirm
 * actual STA no later than the deadline (equality accepted), never CMD alone.
 * Pending at the deadline, late confirmation, bad time, or cap exhaustion fail.
 *
 * Any post-request failure restores the saved command exactly once, with a
 * fresh timeout and the same poll cap. An old STA match cannot prove a pending
 * request was canceled. Rollback OK requires observing the originally requested
 * HF source (even late, or after cancellation), then the complete saved settings.
 * At deadline/cap exhaustion, if that source has never been observed, report
 * ROLLBACK_UNCONFIRMED (9), not OK. Other rollback errors remain explicit.
 * Return the ORIGINAL request failure even when rollback is confirmed.
 * Caller must stop on any failure; no automatic retry or recovery is safe.
 * At most 2*poll_limit polls, 2*(poll_limit+1) timer samples and two CMD writes.
 * Bounds require an executing CPU; raw time cannot detect missed full wraps.
 * Zero timeout permits only same-tick confirmation; no calibrated time claim.
 * See docs/ARCHITECTURE.md for clamping/calibration side effects and diagnostics.
 */
clock_result_t clock_select_init(clock_source_t source, uint32_t timeout_ticks,
                                 uint16_t poll_limit, clock_diagnostics_t *diagnostics);

#endif
