/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef MAC_RADIO_H
#define MAC_RADIO_H
#include "mac_epoch.h"
#include "radio_autoack.h"

typedef enum {
    MAC_RADIO_READY = 0, MAC_RADIO_FRAME, MAC_RADIO_BAD_CRC, MAC_RADIO_EMPTY,
    MAC_RADIO_DRAIN, MAC_RADIO_STOPPED, MAC_RADIO_TX_DONE, MAC_RADIO_CCA_BUSY,
    MAC_RADIO_INVALID_ARGUMENT, MAC_RADIO_INVALID_RANGE, MAC_RADIO_BUFFER_OWNERSHIP,
    MAC_RADIO_STATE, MAC_RADIO_CLOCK_ERROR, MAC_RADIO_TIMER_ERROR,
    MAC_RADIO_EPOCH_ERROR, MAC_RADIO_DRIVER_ERROR
} mac_radio_result_t;

enum { MAC_RADIO_COLD = 0, MAC_RADIO_STARTING, MAC_RADIO_RX,
       MAC_RADIO_DRAINING, MAC_RADIO_OFF, MAC_RADIO_FAULT };

typedef struct {
    mac_epoch_stamp_t last_live;
    uint8_t phase, result, fault, has_time;
    uint8_t clock_result, timer_result, epoch_result, radio_result;
} mac_radio_diagnostics_t;

/* Explicit CC2530_MAC_RADIO composition, one foreground owner since full SoC
 * reset. No IRQ/DMA/CSP, sleep, debugger intervention or independent use of
 * clock/mac_time/mac_epoch/radio_autoack during this ownership epoch.
 * Init copies configuration, selects real XOSC32, starts the real MAC Timer,
 * binds its containing period to symbol0, then acquires the real receiver.
 * Initial AUTOACK can TRANSMIT before init returns; RF authorization is needed.
 * This is not a passive receiver, radio recovery or a MAC transaction adapter.
 *
 * Every call has a positive raw Sleep Timer timeout <800000 and a positive
 * poll cap PER underlying service call. Init additionally invokes the bounded
 * clock service, including its existing explicit rollback semantics on error.
 * Subsequent operations refresh the live epoch before radio work. Caller
 * guarantees continuous CPU/clock ownership and less than0xFFFFFF00 real fine
 * increments between accepted samples; missed full wraps cannot be detected.
 *
 * All caller objects are immutable/disjoint while borrowed, complete persistent
 * ordinary XDATA after this composition's private prefix and outside the whole
 * linked libc scratch suffix. Invalid arguments/storage/state change nothing.
 * The first operational fault retains its cause and all ownership; subsequent
 * calls return it without MMIO. No implicit retry, abort, stop, reset or release.
 */
mac_radio_result_t mac_radio_init(const radio_autoack_config_t MCU_XDATA *configuration,
                                  uint32_t timeout, uint16_t limit);

/* Exactly preserved coarse/fine live time, not a captured or rounded PHY end.
 * last_live in diagnostics is the last accepted sample, not current time or
 * the time of a published frame/TX. Never feed it to mac_tx as an event stamp.
 * Output is unchanged on every error. Sampling may run while this owner has
 * RX/AUTOACK activity; ordinary mac_time_read_live remains quiescent-only.
 */
mac_radio_result_t mac_radio_now(uint32_t timeout, uint16_t limit,
                                mac_epoch_stamp_t MCU_XDATA *output);

/* Real radio outcomes, not delivery or security acceptance. Receive publishes
 * both CRC-good and CRC-bad original bodies atomically; inactive tail survives.
 * Send requires this owner's STOPPED/empty state. One ordinary hardware-gated
 * attempt enters the explicit no-AUTOACK response phase and keeps RX requested.
 * Stop may require explicit receive/drain calls. Resume restores normal
 * filtering/AUTOACK only after STOPPED. No captured timestamp/ACK/retry/IFS,
 * continuous POLL closure, network membership or calibrated timing is supplied.
 */
mac_radio_result_t mac_radio_receive(uint32_t timeout, uint16_t limit,
                                     radio_autoack_frame_t MCU_XDATA *output);
mac_radio_result_t mac_radio_send(const uint8_t MCU_XDATA *body, uint8_t length,
                                  uint32_t timeout, uint16_t limit);
mac_radio_result_t mac_radio_stop(uint32_t timeout, uint16_t limit);
mac_radio_result_t mac_radio_resume(uint32_t timeout, uint16_t limit);
const mac_radio_diagnostics_t MCU_XDATA *mac_radio_diagnostic(void);
#if defined(CC2530_MAC_ATTEMPT)
#define MAC_RADIO_PREPARED 6u
/* Internal provisional staging hooks for mac_attempt, not public receipts.
 * Both objects must be disjoint ordinary caller storage outside this prefix.
 */
mac_radio_result_t mac_radio_prepare(const uint8_t MCU_XDATA *body,
                                             uint8_t length, uint32_t timeout, uint16_t limit);
mac_radio_result_t mac_radio_attempt(uint16_t window, uint32_t timeout, uint16_t limit,
    radio_autoack_attempt_t MCU_XDATA *output, mac_epoch_t MCU_XDATA *first);
#endif
#if defined(CC2530_MAC_HANDOFF)
/* Internal same-owner hooks. The clock hook also admits PREPARED without
 * issuing an RF operation or weakening ordinary mac_radio_now's contract.
 */
mac_radio_result_t mac_radio_attempt_now(uint32_t timeout, uint16_t limit,
                                        mac_epoch_stamp_t MCU_XDATA *output);
mac_radio_result_t mac_radio_handoff(uint32_t timeout, uint16_t limit);
#endif
#endif
