/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef CC2530_RADIO_RX_H
#define CC2530_RADIO_RX_H

#include "cc2530_mmio.h"

#define RADIO_RX_BODY_MAX 125u

typedef enum {
    RADIO_RX_OK = 0,
    RADIO_RX_INVALID_ARGUMENT,
    RADIO_RX_INVALID_RANGE,
    RADIO_RX_BUFFER_OWNERSHIP,
    RADIO_RX_UNSUPPORTED_STATE,
    RADIO_RX_BUSY,
    RADIO_RX_NOT_EMPTY,
    RADIO_RX_STATE_CHANGED,
    RADIO_RX_CONTROLLER_ERROR,
    RADIO_RX_COUNT_ERROR,
    RADIO_RX_TIMEOUT,
    RADIO_RX_POLL_LIMIT,
    RADIO_RX_TIMEBASE_ERROR,
    RADIO_RX_COUNTER_RANGE,
    RADIO_RX_BAD_LENGTH,
    RADIO_RX_BAD_CRC
} radio_rx_result_t;

typedef struct {
    uint8_t length, rssi_raw, correlation;
    uint8_t body[RADIO_RX_BODY_MAX];
} radio_rx_frame_t;

typedef struct {
    uint32_t elapsed_ticks;
    uint16_t polls;
    uint8_t timebase_status, phase, writes, verified, actions, sample_valid;
    uint8_t rx_enable, fsm0, signals, rx_count, tx_count;
    uint8_t rx_first, rx_last, rx_packet, tx_first, tx_last;
    uint8_t errors, flags0, flags1, rssi_valid;
    uint8_t bytes_read, phr, rssi_raw, crc_correlation, discarded_bytes;
} radio_rx_diagnostics_t;

/* Passive, foreground/non-reentrant initial receiver; no TX, ACK, IRQ or DMA.
 * Caller must KNOW exclusive radio/CSP/clock/ST0 ownership since full SoC reset,
 * with no DMA or scheduled RF work. Only verified clock/timebase operations
 * and OK/BAD_CRC calls of this service may change that state before reuse.
 * Samples cannot prove that history.
 * Requires awake stable undivided XOSC32, all IENs/RF masks zero, standard modem,
 * empty reset FIFOs and no latched controller error. No GPIO/address-RAM access.
 *
 * channel is 11..26; timeout is 1..7FFFFF raw ticks; limit is a positive poll
 * cap for the entire operation, including configuration/draining/flush.
 * Output and diagnostics are complete disjoint persistent XDATA objects below
 * 1E00, beyond the linked driver/timebase private prefix and generic-store
 * helper scratch. Link timebase, this module, then caller-owned objects.
 *
 * Configures fixed promiscuous RX with AUTOACK=0, AUTOCRC=1, no source matching
 * and FIFOP threshold127. FSCAL1 is written 00; readback checks only its
 * stable VCO_CURR[1:0], not reserved R/W0 upper bits (SWRU191F p.267).
 * All other configuration bytes are checked in full. E3 enables/calibrates RX.
 * After a complete frame is observed, RXMASKCLR80 requests soft shutdown,
 * allowing reception to finish.
 * Only after verified idle are length/body/RSSI/correlation read through RFD.
 * A verified ED flush discards any additional queued bytes; this is NOT
 * lossless continuous reception. No TX/ACK strobe or TXFIFO write exists.
 *
 * OK publishes one FCS-free body of 1..125 bytes and raw metadata; unused body
 * tail is unchanged. CRC_OK is required, but is not authentication or MAC/NWK
 * validation. Correlation is 0..127, NOT a calibrated IEEE LQI; RSSI is raw
 * two's complement, NOT dBm. BAD_CRC also completes the stop/flush but leaves
 * output unchanged; it permits another call.
 *
 * Argument/range/ownership errors leave both objects and MMIO unchanged.
 * Other failures leave output unchanged and latch the ORIGINAL result; later
 * calls perform no MMIO/diagnostic writes. RX/configuration/FIFO effects may
 * remain active, partial or unconfirmed after failure. There is no implicit
 * RF-off/flag-clear/retry/reset; genuine full-reset recovery is required.
 * The deadline ends at the checked publication decision, followed by a fixed
 * bounded CPU copy. Publication is not atomic. Bounds require executing CPU
 * and the timebase's half-range/no-reset/no-missed-wrap history.
 */
radio_rx_result_t radio_rx_receive_init(uint8_t channel, uint32_t timeout, uint16_t limit,
                                      radio_rx_frame_t MCU_XDATA *output,
                                      radio_rx_diagnostics_t MCU_XDATA *diagnostics);

#endif
