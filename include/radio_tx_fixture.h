/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef RADIO_TX_FIXTURE_H
#define RADIO_TX_FIXTURE_H
#include "bringup.h"
#include "clock.h"
#include "radio_fifo.h"
#include "radio_tx.h"

#define TXF_ADMISSION_POLLS 256u
#define TXF_TIMEOUT 1024UL
#define TXF_LIMIT 256u
#define TXF_CHANNEL 26u
#define TXF_LENGTH 13u
#define TXF_SIZE 24u
enum { TXF_DISARMED = 1, TXF_ARMED, TXF_ADMITTED, TXF_RUNNING, TXF_END, TXF_FAULT };
enum { TXF_NONE, TXF_EXHAUSTED, TXF_PACKET, TXF_INVARIANT, TXF_CLOCK,
       TXF_CLEAR, TXF_PRELOAD, TXF_TRANSMIT, TXF_FINAL_CLEAR };
typedef struct {
    uint8_t signature[4], version, size, phase, reason, stage, attempts, completed;
    uint8_t clock_result, fifo_result, tx_result, channel, power, length;
    uint8_t remaining[2], guards[2], reserved[3];
} radio_tx_fixture_t;
typedef char radio_tx_fixture_size[(sizeof(radio_tx_fixture_t) == TXF_SIZE) ? 1 : -1];
extern volatile MCU_XDATA radio_tx_fixture_t radio_tx_fixture_state;
extern volatile MCU_XDATA uint8_t radio_tx_fixture_mailbox[8];
extern MCU_XDATA clock_diagnostics_t radio_tx_fixture_clock;
extern MCU_XDATA radio_fifo_diagnostics_t radio_tx_fixture_fifo;
extern MCU_XDATA radio_tx_diagnostics_t radio_tx_fixture_tx;
extern const MCU_CODE uint8_t radio_tx_fixture_body[TXF_LENGTH];

/* Manual, one IF_CLEAR call per full SoC reset. No real identity, ACK, retry,
 * RX-owner reuse, direct fallback, automatic recovery or radio-off-on-error.
 * Only write mailbox while halted at WAIT, in DISARMED/ARMED, all eight bytes:
 * [opcode, ~opcode, 26, ~26, token, ~token, 69, 96].
 * ARM=A6/3C, RUN=59/C3. Not authentication or RF authorization.
 * Both windows have independent finite work caps; debugger pauses do not
 * consume a wall-time window. RUN admission clears the packet and RETURNS
 * without MMIO, permitting inspection of ADMITTED before continuous execution.
 * A further explicit resume executes the real clock/FIFO/TX sequence without
 * intermediate checkpoints. Real service deadlines/caps remain independent.
 * Startup/GPIO unchanged. Foreground bank0/DPS0, no IRQ/DMA/CSP/other owner.
 * Reinitialization cannot clear authorization/attempt/fault history.
 */
void radio_tx_fixture_initialize(void);
void radio_tx_fixture_poll(void);
#endif
