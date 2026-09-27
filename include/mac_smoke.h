/* SPDX-License-Identifier: BSD-3-Clause
 * Original explicitly admitted laboratory fixture, not a network application.
 */
#ifndef MAC_SMOKE_H
#define MAC_SMOKE_H
#include "bringup.h"
#include "mac_adapter.h"

#define MAC_SMOKE_SIZE 64u
#define MAC_SMOKE_LENGTH 13u
#define MAC_SMOKE_CHANNEL 26u
#define MAC_SMOKE_SERVICE_TICKS 1024UL
#define MAC_SMOKE_SERVICE_POLLS 256u
#define MAC_SMOKE_ADMISSION_POLLS 256u
#define MAC_SMOKE_STEPS 4096u
#define MAC_SMOKE_RAW_TICKS 65536UL
#define MAC_SMOKE_LIFETIME 62500UL
enum { MS_DISARMED=1, MS_ARMED, MS_ADMITTED, MS_RUNNING, MS_END, MS_FAULT };
enum { MS_NONE, MS_PACKET, MS_ADMISSION_EXPIRED, MS_INVARIANT, MS_ADAPTER,
       MS_MAC, MS_TIME, MS_WORK, MS_DRAW, MS_TERMINAL };
enum { MS_NO_OUTCOME, MS_SENT, MS_CCA_BUSY };
typedef struct {
    uint8_t signature[4], version, size, phase, reason;
    uint8_t stage, consumed, completed, outcome;
    uint8_t adapter_result, mac_result, radio_result, adapter_phase, mac_phase;
    uint8_t draws, actions, attempts, busy, sent, retired, received;
    uint8_t channel, power, length, dsn, remaining[2], steps[2], elapsed[3], live[4];
    uint8_t transmissions, held, ready, pending, goal, normal_rx, released;
    uint8_t radio_phase, radio_detail_result, radio_errors, radio_flags0, radio_flags1;
    uint8_t guards[2], mac_outcome, reserved[10];
} mac_smoke_status_t;
typedef char mac_smoke_size[sizeof(mac_smoke_status_t)==MAC_SMOKE_SIZE?1:-1];
extern volatile MCU_XDATA mac_smoke_status_t mac_smoke_status;
extern volatile MCU_XDATA uint8_t mac_smoke_mailbox[8];
extern MCU_XDATA radio_autoack_config_t mac_smoke_config;
extern MCU_XDATA mac_tx_interval_t mac_smoke_tx;
extern MCU_XDATA mac_tx_interval_action_t mac_smoke_action;
extern MCU_XDATA mac_tx_interval_event_t mac_smoke_random;
extern MCU_XDATA mac_epoch_stamp_t mac_smoke_clock;
/* Whole-reset only. ARM/RUN are not a reset/recovery API.
 * ARM: A9 56 1A E5 36 C9 4D B2; RUN: 56 A9 1A E5 C9 36 4D B2.
 * Write mailbox only while halted at WAIT; a RUN admission returns to WAIT
 * without MMIO. One further explicit continuation runs the entire experiment.
 * Initial RX/AUTOACK is enabled by real adapter init: no total RF-packet bound.
 * Only ordinary noACK DATA TX is bounded to one. No ACK/retry/join validation.
 * Five fixed public laboratory backoff draws are not entropy/crypto readiness.
 * Hard work/raw-time/operational errors retain ownership; no automatic reset,
 * retry, abort or forced flush. END requires public physical retirement and
 * release. Diagnostic fields are read-only, never debugger command parameters.
 */
void mac_smoke_initialize(void);
void mac_smoke_poll(void);
void mac_smoke_wait(void);
void mac_smoke_end(void);
void mac_smoke_fault(void);
#endif
