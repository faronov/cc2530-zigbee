/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef RADIO_LINK_FIXTURE_H
#define RADIO_LINK_FIXTURE_H
#include "bringup.h"
#include "clock.h"
#include "radio_autoack.h"

#define LNK_CHANNEL 26u
#define LNK_LENGTH 13u
#define LNK_SIZE 36u
#define LNK_ADMISSION_POLLS 256u
#define LNK_SERVICE_TICKS 1024UL
#define LNK_SERVICE_LIMIT 256u
#define LNK_RX_TICKS 1024UL
#define LNK_RX_POLLS 4096u
#define LNK_FRAMES 2u

enum { LNK_DISARMED = 1, LNK_ARMED, LNK_ADMITTED, LNK_RUNNING, LNK_END, LNK_FAULT };
enum { LNK_NONE, LNK_EXHAUSTED, LNK_PACKET, LNK_INVARIANT, LNK_CLOCK,
       LNK_ACQUIRE, LNK_PREPARE, LNK_TRANSMIT, LNK_RECEIVE, LNK_STOP,
       LNK_CAPACITY, LNK_TIME, LNK_WORK };
enum { LNK_NO_OUTCOME, LNK_RX_GOOD, LNK_RX_BAD, LNK_RX_TIMEOUT, LNK_CCA_BUSY };

typedef struct {
    uint8_t signature[4], version, size, phase, reason, stage, consumed, attempts, completed;
    uint8_t outcome, clock_result, tx_result, owner_result, frames, before_tx, channel, power, length;
    uint8_t rx_polls[2], elapsed[4], remaining[2], guards[2], reserved[5];
} radio_link_fixture_t;
typedef char radio_link_fixture_size[sizeof(radio_link_fixture_t) == LNK_SIZE ? 1 : -1];

extern volatile MCU_XDATA radio_link_fixture_t radio_link_fixture_state;
extern volatile MCU_XDATA uint8_t radio_link_fixture_mailbox[8];
extern MCU_XDATA clock_diagnostics_t radio_link_fixture_clock;
extern MCU_XDATA radio_autoack_diagnostics_t radio_link_fixture_radio;
extern MCU_XDATA radio_autoack_config_t radio_link_fixture_config;
extern MCU_XDATA radio_autoack_frame_t radio_link_fixture_frames[LNK_FRAMES];
extern MCU_XDATA uint8_t radio_link_fixture_body[LNK_LENGTH];

/* Manual board fixture, never automatic RF. One admitted experiment per reset.
 * ARM: A9 56 1A E5 36 C9 4C B3; RUN: 56 A9 1A E5 C9 36 4C B3.
 * Write all eight mailbox bytes only while halted at WAIT in DISARMED/ARMED.
 * RUN admission returns without MMIO; another explicit continuation executes
 * clock -> acquire -> stop/drain -> one CCA/TX -> bounded raw RX -> stop/drain.
 * No intermediate debugger intervention. AUTOACK may transmit during initial
 * acquisition; ordinary/post-TX phase disables it. No finite ACK-count claim.
 * No ACK request/matching, retry, security, calibrated timing or MAC window.
 *
 * Two complete CRC-good/bad bodies are retained, including pre-TX drainage.
 * Capacity or operational faults retain ownership without abort/flush/cleanup.
 * END requires actual physical stop and empty FIFO; timeout is not silence or
 * RX acceptance. Received bytes/identities must stay in private reports.
 * Reinitialization cannot clear admission/attempt/fault history.
 */
void radio_link_fixture_initialize(void);
void radio_link_fixture_poll(void);
#endif
