/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef CC2530_RADIO_AUTOACK_H
#define CC2530_RADIO_AUTOACK_H

#include "cc2530_mmio.h"

#if defined(CC2530_MAC_HANDOFF)
#if !defined(CC2530_MAC_ATTEMPT) || !defined(CC2530_MAC_RADIO)
#error MAC handoff requires the complete same-owner interval-attempt profile
#endif
#define RADIO_AUTOACK_NORMAL_FILTER 0x05
#else
#define RADIO_AUTOACK_NORMAL_FILTER 0x01
#endif

#define RADIO_AUTOACK_BODY_MAX 125u
#define RADIO_AUTOACK_POWER_05 0x05u

typedef enum {
    RADIO_AUTOACK_READY = 0, RADIO_AUTOACK_FRAME, RADIO_AUTOACK_BAD_CRC,
    RADIO_AUTOACK_EMPTY, RADIO_AUTOACK_DRAIN, RADIO_AUTOACK_STOPPED,
    RADIO_AUTOACK_INVALID_ARGUMENT, RADIO_AUTOACK_INVALID_RANGE,
    RADIO_AUTOACK_BUFFER_OWNERSHIP, RADIO_AUTOACK_STATE,
    RADIO_AUTOACK_UNSUPPORTED_STATE, RADIO_AUTOACK_STATE_CHANGED,
    RADIO_AUTOACK_CONTROLLER_ERROR, RADIO_AUTOACK_FIFO_ERROR,
    RADIO_AUTOACK_TIMEOUT, RADIO_AUTOACK_WORK_LIMIT, RADIO_AUTOACK_TIME_ERROR,
    RADIO_AUTOACK_TX_DONE, RADIO_AUTOACK_CCA_BUSY
#if defined(CC2530_MAC_ATTEMPT)
    , RADIO_AUTOACK_ATTEMPT_TIMER_ERROR, RADIO_AUTOACK_ATTEMPT_LATE_ARM
#endif
#if defined(CC2530_MAC_HANDOFF)
    , RADIO_AUTOACK_HANDOFF_RACE
#endif
} radio_autoack_result_t;

typedef enum {
    RADIO_AUTOACK_COLD = 0, RADIO_AUTOACK_RX, RADIO_AUTOACK_DRAINING,
    RADIO_AUTOACK_OFF, RADIO_AUTOACK_FAULT,
    RADIO_AUTOACK_RX_NOACK, RADIO_AUTOACK_DRAIN_NOACK, RADIO_AUTOACK_OFF_NOACK
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
 * The explicit CC2530_MAC_RADIO profile additionally permits preceding verified
 * mac_time_init under the SAME foreground owner: clock -> Timer2 init -> radio
 * acquisition. That owner may then use mac_time_read_radio, not relax init or
 * mac_time_read_live quiescence. Register samples cannot prove this history;
 * this exception does not authorize arbitrary same-reset hardware-owner mixing.
 * AUTOACK TRANSMITS RF without CPU intervention: explicit RF-transmitting
 * ownership/authorization and CPU progress are prerequisites. A finite call
 * does not bound the number of over-air ACKs while the receiver remains on.
 *
 * Cold acquisition copies the configuration; no pointer is retained. All PAN,
 * short and IEEE values are raw caller-owned filter configuration, not identity
 * validation or security. Channel11..26; only raw power05. The normal profile is
 * version<=0, reserved-FCF mask0, non-coordinator, DATA/ACK/command accepted,
 * filtering/AUTOCRC/AUTOACK on, unslotted, RX-to-RX timeout off, Pending0 and
 * source matching/AUTOPEND off. Security-enabled bodies are NOT excluded.
 * Broadcast filtering has no documented AUTOACK broadcast exception. There
 * is no arbitrary-input unicast-only ACK guarantee or MAC acceptance claim.
 *
 * Configuration/output are complete persistent XDATA below1E00, beyond the
 * linked timebase/driver private prefix and outside all linked libc scratch.
 * CC2530_MAC_RADIO instead uses mac_radio_shared_end after the complete linked
 * lower-service private prefix; the parent must prove this boundary and the
 * full memcpy/memset/gptr scratch suffix. Isolated markers remain for legacy.
 * timeout is positive and <800000 raw Sleep Timer ticks, NOT symbols; limit
 * is a positive whole-call poll/work bound. Equality expires. True half-range
 * continuity/no missed wraps are required. Each destructive action has room
 * for confirmation. Error outputs are unchanged; unused frame tail is retained.
 *
 * Invalid arguments/storage/state have no MMIO or diagnostic effects. The
 * first operational fault retains ownership and its original result; later
 * operations perform no MMIO or diagnostic/output writes. No abort, RX flush,
 * retry, RF-off promise or recovery API exists. Acquire still rejects OFF;
 * only explicit same-owner resume/send may rearm it. FAULT requires genuine full
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

/* Clear only this owner's RXMASK bit, preserving its profile while reception/ACK
 * finishes; wait for verified physical idle. DRAIN retains queued frames:
 * call receive until consumed, then stop again to confirm STOPPED. No hidden
 * flush or output loss. A stopped partial FIFO is a fault, never drained.
 * STOPPED means physical stop plus empty hardware FIFO after explicit reads.
 * It is NOT POLL CLOSED, loss-free continuous reception, IFS completion,
 * ordinary-TX permission, or release to another init-time hardware API.
 */
radio_autoack_result_t radio_autoack_stop(uint32_t timeout, uint16_t limit);

/* OFF/OFF_NOACK only, after this owner's STOPPED and complete-frame drain.
 * Revalidate the retained profile, physical idle and empty consistent FIFO,
 * set only the owned RX mask bit, and confirm RX/RSSI readiness. Preserve
 * nonzero empty ring cursors; no flush, reset or fault clear. OFF_NOACK first
 * restores the normal filter/AUTOACK profile while idle. Ordinary OFF retains
 * the original one-mask-write behavior. CCA settings, once owned, stay checked.
 * READY begins another RX episode with an explicit reception gap, NOT a
 * continuous/loss-free window, POLL PREPARED or handoff from an ordinary TX API.
 * Eligible frames may again be autoacknowledged before this call returns.
 */
radio_autoack_result_t radio_autoack_resume(uint32_t timeout, uint16_t limit);

/* OFF/OFF_NOACK only. One explicit ordinary CCA/TX attempt, not MAC scheduling.
 * Body is immutable, caller-owned XDATA, 1..125 FCS-free bytes; no MAC parsing.
 * At verified idle, disable AUTOACK/filtering, establish mode3 CCA (F8/1A),
 * flush/reload ONLY TXFIFO, and clear/verify TXDONE. Keep the owned RX request
 * through ISTXONCCA and automatic post-TX reception. No software-CCA-then-TX,
 * unconditional TX, retry, RX flush, abort or cleanup-on-fault.
 *
 * TX_DONE means fresh ordinary TXDONE plus inactive TX, not ACK/delivery or
 * captured time. CCA_BUSY means that one hardware-gated attempt did not TX.
 * Both enter RX_NOACK: receive/stop work, but filtering/AUTOACK are disabled.
 * Received bodies may be unrelated; neither call supplies a timed ACK window.
 * Stop/drain -> OFF_NOACK -> explicit resume restores normal filtered RX.
 * The RX gap and no-AUTOACK interval MUST NOT be hidden as continuous MAC/POLL.
 * TXFIFO remains owned; a later explicit send replaces it only while idle.
 * Existing timeout/work/storage/retained-fault rules apply to this whole call.
 */
radio_autoack_result_t radio_autoack_send(
    const uint8_t MCU_XDATA *body, uint8_t length, uint32_t timeout, uint16_t limit);
const radio_autoack_diagnostics_t MCU_XDATA *radio_autoack_diagnostic(void);

#if defined(CC2530_MAC_ATTEMPT)
#include "mac_time.h"
#define RADIO_AUTOACK_CCA_ENERGY_ONLY 1u
#define RADIO_AUTOACK_CCA_THRESHOLD_RAW 0xf8u
#define RADIO_AUTOACK_ATTEMPT_PREPARED 8u

typedef struct {
    mac_time_stamp_t before, armed, tx, rx, last;
    radio_autoack_frame_t frame;
    uint8_t transmitted, received, sampled_cca, within_window;
} radio_autoack_attempt_t;

/* Co-owner hooks only. prepare needs OFF/empty and owns the immutable TX slot.
 * run uses energy-only CCA1, explicit >=8-symbol dwell and RX_MODE11 before
 * admission. RX_MODE00 afterward remains unfiltered/AUTOACK-off. No MAC result.
 * Provisional output belongs above mac_radio_shared_end, not driver-private RAM.
 * Output is provisional internal staging, including on operational error.
 * Only the top-level composition may publish a fully checked receipt.
 * EMPTY is an observation boundary, not NO_ACK.
 */
radio_autoack_result_t radio_autoack_prepare(
    const uint8_t MCU_XDATA *body, uint8_t length, uint32_t timeout, uint16_t limit);
radio_autoack_result_t radio_autoack_attempt(
    uint16_t window, uint32_t timeout, uint16_t limit,
    radio_autoack_attempt_t MCU_XDATA *output);
#endif
#if defined(CC2530_MAC_HANDOFF)
typedef struct {
    mac_time_stamp_t before, after, last;
} radio_autoack_handoff_clock_t;
/* Co-owner hooks, not standalone ownership or a MAC confirmation. Eligibility
 * is history only, never a live no-race assertion. Handoff consumes the first
 * CRC-good ACK already read by attempt, checks the immutable hardware TX DSN,
 * and restores version<=1 filtering/AUTOACK without stopping RX. A new SFD,
 * queued/partial frame, RF error or failed readback retains a terminal fault.
 * Both SFD probes must fit inside a measured <512-tick live-clock bracket;
 * instruction counts cannot bound flash-cache stalls. Clock staging is private,
 * after the complete lower-service prefix, and is provisional on failure.
 * No FIFO read/flush, strobe, receiver-mask write or recovery occurs.
 */
uint8_t radio_autoack_handoff_eligible(void);
radio_autoack_result_t radio_autoack_handoff(volatile uint32_t timeout, volatile uint16_t limit,
                                            radio_autoack_handoff_clock_t MCU_XDATA * volatile clock);
#endif
#if defined(CC2530_MAC_RECONFIG)
#if !defined(CC2530_MAC_HANDOFF)
#error MAC reconfiguration requires the complete same-owner handoff profile
#endif
/* Co-owner hook. OFF/OFF_NOACK only, after this owner's STOPPED and complete
 * drain. Rewrites only differing PAN, short-address and FREQCTRL bytes while
 * the receiver mask is clear, verifying the whole owned profile after each
 * write. IEEE, power and the filter/AUTOACK/CCA profile are immutable here.
 * The state stays OFF/OFF_NOACK: a separate resume starts a new RX episode.
 * Frames transmitted during the gap are not received. Invalid input changes
 * nothing; an operational fault is terminal like every other operation.
 */
radio_autoack_result_t radio_autoack_configure(
    const radio_autoack_config_t MCU_XDATA *configuration, uint32_t timeout, uint16_t limit);
#endif
#endif
