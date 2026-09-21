/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef CC2530_RADIO_AUTOACK_H
#define CC2530_RADIO_AUTOACK_H

#include "cc2530_mmio.h"

#define RADIO_AUTOACK_BODY_MAX 125u
#define RADIO_AUTOACK_POWER_05 0x05u

typedef enum {
    RADIO_AUTOACK_READY = 0, RADIO_AUTOACK_FRAME, RADIO_AUTOACK_BAD_CRC,
    RADIO_AUTOACK_EMPTY, RADIO_AUTOACK_DRAIN, RADIO_AUTOACK_STOPPED,
    RADIO_AUTOACK_INVALID_ARGUMENT, RADIO_AUTOACK_INVALID_RANGE,
    RADIO_AUTOACK_BUFFER_OWNERSHIP, RADIO_AUTOACK_STATE,
    RADIO_AUTOACK_UNSUPPORTED_STATE, RADIO_AUTOACK_STATE_CHANGED,
    RADIO_AUTOACK_CONTROLLER_ERROR, RADIO_AUTOACK_FIFO_ERROR,
    RADIO_AUTOACK_TIMEOUT, RADIO_AUTOACK_WORK_LIMIT, RADIO_AUTOACK_TIME_ERROR
} radio_autoack_result_t;

typedef enum {
    RADIO_AUTOACK_COLD = 0, RADIO_AUTOACK_RX, RADIO_AUTOACK_DRAINING,
    RADIO_AUTOACK_OFF, RADIO_AUTOACK_FAULT
} radio_autoack_state_t;

typedef struct {
    uint8_t ieee[8];
    uint16_t pan, short_address;
    uint8_t channel, power;
} radio_autoack_config_t;

typedef struct {
    uint8_t length, rssi_raw, crc_correlation;
    uint8_t body[RADIO_AUTOACK_BODY_MAX];
} radio_autoack_frame_t;

typedef struct {
    uint32_t elapsed_ticks;
    uint16_t polls, bytes_read;
    uint8_t phase, result, writes, verified, sample_valid;
    uint8_t mask, calibration, signals, count, first, last, packet;
    uint8_t errors, flags0, flags1, rssi_valid, phr, timebase_status;
} radio_autoack_diagnostics_t;

/* Foreground/nonreentrant, register-bank0/DPS0, awake undivided stable XOSC32.
 * KNOW exclusive radio/CSP/DMA/clock/ST0 ownership since full SoC reset.
 * Only verified clock/timebase operations may precede acquisition. No scheduled
 * work, DMA, IRQs, other RF API, sleep, debugger intervention or GPIO access.
 * AUTOACK TRANSMITS RF without CPU intervention: explicit RF-transmitting
 * ownership/authorization and CPU progress are prerequisites. A finite call
 * does not bound the number of over-air ACKs while the receiver remains on.
 *
 * Cold acquisition copies the configuration; no pointer is retained. All PAN,
 * short and IEEE values are raw caller-owned filter configuration, not identity
 * validation or security. Channel11..26; only raw power05. The fixed profile is
 * version<=0, reserved-FCF mask0, non-coordinator, DATA/ACK/command accepted,
 * filtering/AUTOCRC/AUTOACK on, unslotted, RX-to-RX timeout off, Pending0 and
 * source matching/AUTOPEND off. Security-enabled bodies are NOT excluded.
 * Broadcast filtering has no documented AUTOACK broadcast exception. There
 * is no arbitrary-input unicast-only ACK guarantee or MAC acceptance claim.
 *
 * Configuration/output are complete persistent XDATA below1E00, beyond the
 * linked timebase/driver private prefix and outside all linked libc scratch.
 * timeout is positive and <800000 raw Sleep Timer ticks, NOT symbols; limit
 * is a positive whole-call poll/work bound. Equality expires. True half-range
 * continuity/no missed wraps are required. Each destructive action has room
 * for confirmation. Error outputs are unchanged; unused frame tail is retained.
 *
 * Invalid arguments/storage/state have no MMIO or diagnostic effects. The
 * first operational fault retains ownership and its original result; later
 * operations perform no MMIO or diagnostic/output writes. No abort, flush,
 * retry, RF-off promise or recovery API exists. Acquire still rejects OFF;
 * only explicit same-owner resume may rearm it. FAULT requires genuine full
 * reset. No other RF API may intervene. See docs/RADIO_AUTOACK.md.
 */
radio_autoack_result_t radio_autoack_acquire(
    const radio_autoack_config_t MCU_XDATA *configuration, uint32_t timeout, uint16_t limit);

/* Nonblocking at the initial observation: EMPTY means no complete head frame
 * was observed, not an empty air channel or drained receive window.
 * FRAME and BAD_CRC BOTH publish one original FCS-free body and raw metadata.
 * CRC_OK is bit7 of crc_correlation; low7 is correlation, not calibrated LQI.
 * Neither return attests a per-frame ACK or supplies a timestamp. Late and
 * repeated eligible frames can be autoacknowledged regardless of application
 * decisions. No implicit discard or duplicate/window/MAC filtering occurs.
 */
radio_autoack_result_t radio_autoack_receive(
    uint32_t timeout, uint16_t limit, radio_autoack_frame_t MCU_XDATA *output);

/* Clear only this owner's RXMASK bit, preserving AUTOACK while reception/ACK
 * finishes; wait for verified physical idle. DRAIN retains queued frames:
 * call receive until consumed, then stop again to confirm STOPPED. No hidden
 * flush or output loss. A stopped partial FIFO is a fault, never drained.
 * STOPPED means physical stop plus empty hardware FIFO after explicit reads.
 * It is NOT POLL CLOSED, loss-free continuous reception, IFS completion,
 * ordinary-TX permission, or release to another init-time hardware API.
 */
radio_autoack_result_t radio_autoack_stop(uint32_t timeout, uint16_t limit);

/* OFF only, after this owner's STOPPED and explicit complete-frame drain.
 * Revalidate the retained profile, physical idle and empty consistent FIFO,
 * set only the owned RX mask bit, and confirm RX/RSSI readiness. Preserve
 * nonzero empty ring cursors; no reconfiguration, flush, reset or fault clear.
 * READY begins another RX episode with an explicit reception gap, NOT a
 * continuous/loss-free window, POLL PREPARED or handoff from an ordinary TX API.
 * Eligible frames may again be autoacknowledged before this call returns.
 */
radio_autoack_result_t radio_autoack_resume(uint32_t timeout, uint16_t limit);
const radio_autoack_diagnostics_t MCU_XDATA *radio_autoack_diagnostic(void);

#endif
