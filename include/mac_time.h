/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef MAC_TIME_H
#define MAC_TIME_H
#include "cc2530_mmio.h"

#define MAC_TIME_FINE_PERIOD 512u
#define MAC_TIME_OVERFLOW_PERIOD 0x00ffffffUL
typedef enum {
    MAC_TIME_COLD = 0, MAC_TIME_OK, MAC_TIME_PENDING,
    MAC_TIME_INVALID_ARGUMENT, MAC_TIME_INVALID_RANGE, MAC_TIME_BUFFER_OWNERSHIP,
    MAC_TIME_NOT_INITIALIZED, MAC_TIME_ALREADY_INITIALIZED,
    MAC_TIME_UNSUPPORTED_STATE, MAC_TIME_STATE_CHANGED, MAC_TIME_TIMEOUT,
    MAC_TIME_WORK_LIMIT, MAC_TIME_TIMEBASE_ERROR, MAC_TIME_COUNTER_RANGE,
    MAC_TIME_COUNT_ERROR
} mac_time_result_t;

/* A raw two-counter tuple, NOT mac_tx's uint32 symbol time or an event capture.
 * fine=0..511 system-clock increments within a period; periods=0..FFFFFE,
 * each worth 512 increments. Hardware replaces the would-be period values
 * 0200 / FFFFFF by zero. No software multiwrap epoch or rounding is supplied.
 * At nominal XOSC32, one fine increment is 31.25ns, one period 16us:
 * these are nominal ratios, NOT calibration, PHY phase or an adapter.
 */
typedef struct {
    uint16_t fine;
    uint32_t periods;
} mac_time_stamp_t;
typedef struct {
    uint32_t elapsed_ticks;
    uint16_t polls, discarded;
    uint8_t result, phase, control, select, irq_flags, timebase_status;
} mac_time_diagnostics_t;

/* Foreground, non-reentrant, SDCC large-model register-bank0/DPS0.
 * Require awake stable undivided XOSC32; stable exclusive clock/ST0 ownership,
 * all IEN and Timer2/RF masks zero; radio/CSP/DMA quiescent with NO scheduled
 * work. Know Timer2 is untouched/idle since full SoC reset, including no prior
 * synchronous start/stop, writes, capture reads, delta adjustment or debugger
 * access. Samples cannot prove this history. GPIO and radio are not started.
 *
 * One-shot init: disable both Timer2 event outputs, disable SYNC, enable common
 * latching, program/verify periods while IDLE, restore live selectors, then
 * request asynchronous RUN and confirm STATE plus a coherent in-range read.
 * Counters, compares, IRQ flags/masks, clocks and RF are NEVER written.
 * No stop/reset/recovery API; repeated init is ALREADY_INITIALIZED, not success.
 *
 * All operations: 0<timeout<0x800000 raw Sleep Timer ticks and positive 16-bit poll
 * budget. Fixed entry preflight precedes the deadline; every hardware action
 * has a remaining confirmation poll. Equality times out. CPU progress and
 * true half-range/continuity assumptions are mandatory. PM, reset, debugger
 * halt/step/resume or unowned timer/clock accesses invalidate this contract.
 *
 * First operational fault is retained; subsequent init/read returns that
 * result without MMIO or diagnostic/output writes, even for invalid arguments.
 * No implicit stop, cleanup or recovery: Timer2 may still be running.
 * Invalid arguments/storage and COLD/ALREADY misuse do not fault or alter state.
 */
mac_time_result_t mac_time_init(uint32_t timeout, uint16_t poll_limit);

/* Successful init is mandatory. One exclusive owner must prevent EVERY other
 * Timer2 selector/read/write (also ISR/DMA/debugger) throughout the epoch.
 * Live reads make NO MMIO writes. LATCH_MODE=1: read T2M0 once into a private
 * byte. If FF, discard/retry with bounds (SWRZ031); otherwise read T2M1 and
 * T2MOVF0/1/2 once, then validate state/time/range before publication.
 * Output is a complete persistent ordinary XDATA object below1E00, after the
 * whole timebase/mac_time private prefix and outside generic-store scratch.
 * In CC2530_MAC_RADIO, both readers instead exclude the complete lower-service
 * prefix through mac_radio_shared_end and the entire linked memcpy/memset/gptr
 * scratch suffix. The exact combined link must prove both boundaries.
 * Output is unchanged on EVERY error. Struct layout is not a wire format.
 * Init and this reader ALWAYS require physical radio quiescence, including
 * in CC2530_MAC_RADIO. No capture, elapsed arithmetic or mac_tx bridge.
 */
mac_time_result_t mac_time_read_live(uint32_t timeout, uint16_t poll_limit,
                                    mac_time_stamp_t MCU_XDATA *output);
#if defined(CC2530_MAC_RADIO)
/* Explicit co-owned profile ONLY; no declaration or success stub otherwise.
 * Caller KNOWS Timer2 and radio_autoack have one serialized foreground owner
 * since full reset. A register sample cannot prove that history or authorize
 * mixing arbitrary same-reset services. Verified clock -> mac_time_init must
 * precede radio acquisition; successful Timer2 init is required for this read.
 *
 * Same bounded common-latch/whole-FF-discard/range/time/publication/fault path
 * as read_live, but allows this owner's ongoing RX, calibration and TX/ACK.
 * Stable clock, disabled IRQs, DMA inactivity, reset-idle CSP/no scheduled work,
 * Timer2 control/select/event/mask/flags and RF-error checks remain mandatory.
 * No MMIO writes, RF/GPIO control or timer capture-register access. The tuple
 * is live time, NOT an event timestamp, PHY-end attribution, calibrated time
 * or mac_tx event bridge. Init/read_live do not inherit this permission.
 * Error output/diagnostics, caller storage and retained-fault rules above apply.
 */
mac_time_result_t mac_time_read_radio(uint32_t timeout, uint16_t poll_limit,
                                     mac_time_stamp_t MCU_XDATA *output);
#endif
/* Read-only private diagnostics. COLD at startup, PENDING in flight; partial
 * observations on fault. This memory accessor performs no hardware read.
 */
const mac_time_diagnostics_t MCU_XDATA *mac_time_diagnostic(void);
#if defined(CC2530_MAC_ATTEMPT)
#ifndef CC2530_MAC_RADIO
#error CC2530_MAC_ATTEMPT requires CC2530_MAC_RADIO
#endif
/* Internal co-owner scope. begin/end execute full genuine radio-time checks.
 * read stages one bounded live latch WITHOUT full peripheral validation.
 * Its output is provisional until end succeeds; never publish it independently.
 * No other timer client may intervene. All ordinary storage/fault rules apply.
 */
mac_time_result_t mac_time_attempt_begin(uint32_t timeout, uint16_t limit,
                                         mac_time_stamp_t MCU_XDATA *output);
mac_time_result_t mac_time_attempt_read(uint32_t timeout, uint16_t limit,
                                        mac_time_stamp_t MCU_XDATA *output);
mac_time_result_t mac_time_attempt_end(uint32_t timeout, uint16_t limit,
                                       mac_time_stamp_t MCU_XDATA *output);
#endif
#endif
