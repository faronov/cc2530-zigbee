/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef MAC_ATTEMPT_H
#define MAC_ATTEMPT_H
#include "mac_radio.h"
#if defined(CC2530_MAC_ATTEMPT)

typedef struct {
    mac_epoch_stamp_t tx_lower, tx_upper, rx_upper, armed, last;
    radio_autoack_frame_t frame;
    uint16_t slot;
    uint8_t length, transmitted, received, within_window;
} mac_attempt_record_t;

/* Sole serialized foreground owner since full reset; all mac_radio ownership,
 * clock, half-range, private/scratch storage and bounded-call rules apply.
 * Init starts normal AUTOACK RX. Stop/drain explicitly before prepare.
 * prepare copies a body into the owned hardware TX slot while truly idle.
 * run submits once through energy-only CCA1, raw threshold F8/hysteresis2;
 * RSSI-valid plus an independent >=8-symbol dwell is not PHY conformance.
 *
 * Receipt is atomic. FRAME/BAD_CRC/EMPTY/CCA_BUSY are hardware observations,
 * never ACKED/NO_ACK/NO_DATA. transmitted identifies a fresh own TX completion.
 * Its real end lies in [tx_lower,tx_upper]; fine phase is preserved.
 * received identifies the original first complete post-TX head, with real end
 * <=rx_upper. within_window means this upper bound <=tx_lower+window symbols;
 * false is uncertainty, not an invented late exact timestamp.
 * window=1..4096 is a collection boundary, not macAckWaitDuration or a lease.
 * EMPTY may leave an incomplete or subsequently arrived frame in the FIFO.
 *
 * RX remains requested after return, unfiltered/AUTOACK-off. Ownership of RX
 * and immutable TX slot persists until explicit stop/drain and prepare/resume.
 * No automatic retry/flush/recovery, captured stamp, MAC confirmation or bridge
 * into mac_tx. Any operational fault is terminal; RF may remain active.
 * slot never wraps: after 65535 preparations, only a new full-reset epoch may
 * submit again. Invalid input changes no state, diagnostics, output or MMIO.
 * All caller buffers must follow mac_attempt_reserved_end and exclude the
 * COMPLETE linked runtime scratch suffix, not just the final generic byte.
 */
mac_radio_result_t mac_attempt_init(const radio_autoack_config_t MCU_XDATA *configuration,
                                    uint32_t timeout, uint16_t limit);
mac_radio_result_t mac_attempt_prepare(const uint8_t MCU_XDATA *body, uint8_t length,
                                       uint32_t timeout, uint16_t limit);
mac_radio_result_t mac_attempt_run(uint16_t window, uint32_t timeout, uint16_t limit,
                                   mac_attempt_record_t MCU_XDATA *output);
mac_radio_result_t mac_attempt_receive(uint32_t timeout, uint16_t limit,
                                       radio_autoack_frame_t MCU_XDATA *output);
mac_radio_result_t mac_attempt_stop(uint32_t timeout, uint16_t limit);
mac_radio_result_t mac_attempt_resume(uint32_t timeout, uint16_t limit);
const mac_radio_diagnostics_t MCU_XDATA *mac_attempt_diagnostic(void);
#endif
#endif
