/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef MAC_ADAPTER_H
#define MAC_ADAPTER_H
#include "mac_attempt.h"
#include "mac_tx_observed.h"

#if defined(CC2530_MAC_ADAPTER)
#if !defined(CC2530_MAC_HANDOFF) || !defined(CC2530_MAC_OBSERVED)
#error MAC adapter requires the complete handoff and observed-event profiles
#endif

typedef enum {
    MAC_ADAPTER_OK = 0, MAC_ADAPTER_WAIT, MAC_ADAPTER_EVENT, MAC_ADAPTER_EXPIRED,
    MAC_ADAPTER_INVALID, MAC_ADAPTER_RANGE, MAC_ADAPTER_STORAGE, MAC_ADAPTER_STATE,
    MAC_ADAPTER_RADIO_ERROR, MAC_ADAPTER_PROTOCOL_ERROR, MAC_ADAPTER_EXHAUSTED
} mac_adapter_result_t;

enum { MAC_ADAPTER_COLD = 0, MAC_ADAPTER_RX, MAC_ADAPTER_OFF,
       MAC_ADAPTER_PREPARED, MAC_ADAPTER_SCHEDULED, MAC_ADAPTER_REPORT,
       MAC_ADAPTER_WAIT_MAC, MAC_ADAPTER_COLLECT, MAC_ADAPTER_RETIRING,
       MAC_ADAPTER_CLOSING, MAC_ADAPTER_DRAINING, MAC_ADAPTER_FAULT };
enum { MAC_ADAPTER_STOP_RX = 0, MAC_ADAPTER_KEEP_RAW, MAC_ADAPTER_KEEP_AUTOACK };
enum { MAC_ADAPTER_TX_EVENT = 1, MAC_ADAPTER_RX_EVENT, MAC_ADAPTER_CLOSED_EVENT };

typedef struct {
    mac_tx_interval_event_t tx;
    mac_epoch_stamp_t through;
    const radio_autoack_frame_t MCU_XDATA *frame;
    uint32_t token, rx_serial;
    uint8_t kind, normal_rx, has_upper;
} mac_adapter_observation_t;

typedef struct {
    mac_epoch_stamp_t live, watermark;
    uint32_t generation, deliveries, frames;
    uint16_t slot;
    uint8_t phase, fault, radio_result, mac_result, has_time, ready, held;
    uint8_t retry, nb, length, policy, normal_rx, transmitted, first, bound_valid;
    uint8_t goal, stop_started, wait_through;
} mac_adapter_diagnostics_t;

/* One full-reset foreground owner. No independent calls to the lower services.
 * init enables normal RX/AUTOACK and therefore requires RF permission on real
 * equipment. It is not recovery. All objects are persistent, disjoint ordinary
 * XDATA beyond the complete linked adapter prefix and outside libc scratch.
 * Invalid API input is atomic. Operational faults retain ownership and frames;
 * no stale clock value or fabricated successful PHY event is returned.
 */
mac_adapter_result_t mac_adapter_init(const radio_autoack_config_t MCU_XDATA * volatile config,
                                      volatile uint32_t timeout, volatile uint16_t limit) JOIN_FAR;
mac_adapter_result_t mac_adapter_now(volatile uint32_t timeout, volatile uint16_t limit,
                                     mac_epoch_stamp_t MCU_XDATA * volatile output) JOIN_FAR;
/* Explicit close/drain must have completed before prepare. Copy through the
 * real interval MAC API before drawing backoff. The device-wide owner persists
 * across retries/transactions; do not reset its DSN or change the owner object.
 * KEEP_AUTOACK requires an ACK-requesting transaction and a successful guarded
 * first-ACK handoff; uncertainty is terminal, never an implicit stop/resume gap.
 */
mac_adapter_result_t mac_adapter_prepare(const mac_tx_interval_t MCU_XDATA * volatile tx,
    volatile uint8_t policy, volatile uint32_t timeout, volatile uint16_t limit) JOIN_FAR;
/* Cancel a prepared slot whose MAC finished before issuing an ATTEMPT (for
 * example cancellation during RANDOM). No receive-coverage claim is made.
 */
mac_adapter_result_t mac_adapter_unprepare(const mac_tx_interval_t MCU_XDATA * volatile tx,
    volatile uint32_t timeout, volatile uint16_t limit) JOIN_FAR;
/* Queue a genuine one-shot ATTEMPT/COLLECT/QUIESCE action. RANDOM remains an
 * explicit caller responsibility: supply a qualified uniform byte to the MAC.
 * Use mac_tx_observed_step for returned sources, not the captured-end API.
 */
mac_adapter_result_t mac_adapter_accept(const mac_tx_interval_t MCU_XDATA * volatile tx,
    const mac_tx_interval_action_t MCU_XDATA * volatile action) JOIN_FAR;
/* Independent RX lease closure. NULL closes now; otherwise through must be
 * no earlier than the last observed live point and less than half an epoch
 * ahead. Continue receiving until actual time reaches it; no future watermark.
 * Soft stop, all complete heads, then verified empty/idle establish closure.
 */
mac_adapter_result_t mac_adapter_close(const mac_epoch_stamp_t MCU_XDATA * volatile through) JOIN_FAR;
/* Bounded foreground progress, with at most one radio operation per call.
 * SCHEDULED waits for observed time >= requested at and will not start a run
 * when that observation is at/after until. CPU latency and actual CCA start
 * are not bounded by this check; it does not attest exact PHY timing.
 * EVENT remains stable/backpressured until consume(token). Every complete head,
 * including bad CRC/non-ACK, is retained; no implicit frame overwrite or flush.
 */
mac_adapter_result_t mac_adapter_step(volatile uint32_t timeout, volatile uint16_t limit) JOIN_FAR;
mac_adapter_result_t mac_adapter_consume(uint32_t token) JOIN_FAR;
const mac_adapter_observation_t MCU_XDATA *mac_adapter_observation(void) JOIN_FAR;
const mac_adapter_diagnostics_t MCU_XDATA *mac_adapter_diagnostic(void) JOIN_FAR;
/* Read-only retained receipt, also on a fault after receiving but before clock
 * publication. held/bound_valid distinguish original bytes from valid timing.
 */
const mac_attempt_record_t MCU_XDATA *mac_adapter_record(void) JOIN_FAR;
#if defined(CC2530_MAC_RECONFIG)
/* OFF only: after a completed closure/retirement or unprepare, with no held
 * frame, pending delivery or closure goal. Updates PAN, short address and
 * channel through the real same-owner radio service; IEEE and power must be
 * the initialization values (INVALID, no MMIO). The radio stays OFF.
 * This is not network membership, PIB validation or recovery.
 */
mac_adapter_result_t mac_adapter_configure(const radio_autoack_config_t MCU_XDATA * volatile config,
    volatile uint32_t timeout, volatile uint16_t limit) JOIN_FAR;
/* OFF only, same preconditions. Starts a NEW normal filtered RX/AUTOACK
 * episode through the real resume service. opened is a live sample taken
 * after the receiver was confirmed ready: a conservative coverage start, not
 * a captured edge. Frames sent while OFF were not received; the gap is
 * explicit and never continuous coverage. RX_EVENT tx.lower is not a frame
 * lower bound outside a transmission; order frames by rx_serial.
 */
mac_adapter_result_t mac_adapter_open(volatile uint32_t timeout, volatile uint16_t limit,
    mac_epoch_stamp_t MCU_XDATA * volatile opened) JOIN_FAR;
#endif
#endif
#endif
