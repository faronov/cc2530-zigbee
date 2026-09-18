/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef RADIO_TX_H
#define RADIO_TX_H
#include "cc2530_mmio.h"

#define RADIO_TX_POWER_05 0x05u
typedef enum { RADIO_TX_DIRECT = 1, RADIO_TX_IF_CLEAR = 2 } radio_tx_mode_t;
typedef enum {
    RADIO_TX_PHY_DONE = 0, RADIO_TX_CCA_CLEAR, RADIO_TX_CCA_BUSY,
    RADIO_TX_INVALID_ARGUMENT, RADIO_TX_INVALID_RANGE, RADIO_TX_BUFFER_OWNERSHIP,
    RADIO_TX_UNSUPPORTED_STATE, RADIO_TX_STATE_CHANGED, RADIO_TX_CONTROLLER_ERROR,
    RADIO_TX_COUNT_ERROR, RADIO_TX_TIMEOUT, RADIO_TX_POLL_LIMIT,
    RADIO_TX_TIMEBASE_ERROR, RADIO_TX_COUNTER_RANGE, RADIO_TX_NOT_PREPARED
} radio_tx_result_t;

typedef struct {
    uint32_t elapsed_ticks;
    uint16_t polls;
    uint8_t phase, writes, verified, actions, sample_valid, cca, txdone, radio_idle;
    uint8_t timebase_status, errors, flags0, flags1, rx_enable, fsm0, signals, rssi_valid;
    uint8_t rx_count, tx_count, rx_first, rx_last, rx_packet, tx_first, tx_last;
} radio_tx_diagnostics_t;

/* Init-time, foreground/non-reentrant, normal SDCC register-bank0/DPS0 ABI.
 * Require awake stable undivided XOSC32,
 * all IEN/RF masks zero, standard modem, and exclusive RF/CSP/DMA/clock/ST0
 * ownership since full SoC reset. Only this API's completed calls, the legacy
 * quiescent FIFO API and verified clock/timebase calls may precede reuse.
 * NO same-reset composition with radio_rx_receive_init/queue_service, a CSP
 * program, DMA, RF ISR, sleep or an unconfirmed operation. Samples cannot prove
 * history. No address/source-match RAM or GPIO accesses.
 *
 * Caller FIRST uses radio_fifo_preload_init for a complete FCS-free body
 * (1..125 bytes). This API verifies initial count/pointers and PHR, not MAC
 * syntax or addresses. No data pointer/lease is retained. TX FIFO contents
 * remain owned until an explicit, separate quiescent FIFO clear; no retry.
 * channel11..26; power MUST be RADIO_TX_POWER_05 (datasheet typical -22dBm,
 * NOT a calibrated board power or RF permission). Other modes/powers reject.
 *
 * AUTOCRC on, AUTOACK/filter/source match off; CCA mode3, raw threshold F8,
 * hysteresis2. RX calibration/readiness plus >=4 system clocks precede CCA.
 * PHY_DONE requires fresh TXDONE and verified idle, NOT an ACK or delivery.
 * CCA_BUSY issues no unconditional TX, ends verified idle and permits explicit
 * caller-directed reuse after FIFO clearing. RX bytes may have accumulated;
 * no frame is published. This API does not flush/discard them or clear RF errors.
 * Normal returns restore FRMCTRL1=01 for the existing quiescent FIFO API.
 *
 * One positive raw timeout<800000 and positive 16-bit poll cap cover this call,
 * not the separate preload/clear. Equality times out. No action without a
 * remaining confirmation poll. True half-range/CPU-progress assumptions apply.
 * Diagnostics is complete persistent ordinary XDATA below1E00, after the entire
 * timebase/FIFO/TX compiler prefix, excluding generic-store helper scratch.
 * Invalid argument/range/ownership: no MMIO or diagnostic mutation.
 * All other errors retain the FIRST result. Re-entry then does no MMIO or
 * diagnostic writes, even for invalid arguments. TX/RX may remain active and
 * a frame may have transmitted on error; never retry or infer quiescence.
 * Genuine separately established full reset is the only fault recovery.
 */
radio_tx_result_t radio_tx_send_init(radio_tx_mode_t mode, uint8_t channel, uint8_t power,
                                    uint8_t body_length, uint32_t timeout, uint16_t limit,
                                    radio_tx_diagnostics_t MCU_XDATA *diagnostics);
/* Same ownership/profile/deadline contract, but requires reset-empty TXFIFO,
 * issues no TX command and returns CCA_CLEAR/CCA_BUSY only after idle.
 * The sample describes one past instant; it reserves no future channel access.
 */
radio_tx_result_t radio_tx_cca_init(uint8_t channel, uint32_t timeout, uint16_t limit,
                                   radio_tx_diagnostics_t MCU_XDATA *diagnostics);
#endif
