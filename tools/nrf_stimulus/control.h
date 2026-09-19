/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef NS51_CONTROL_H
#define NS51_CONTROL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define STIM_ARM_MS 5000u
#define STIM_TRIAL_MS 100u
#define STIM_CAPTURE_MS 20u
#define STIM_STOP_MS 20u
#define STIM_PARTIAL_MS 100u
#define STIM_CAPTURE_MAX 4u
#define STIM_QUEUE 16u
#define STIM_WORK_MAX 4096u
#define STIM_ARM_WORK_MAX 8192u
#define STIM_STOP_WORK_MAX 256u
#define STIM_INPUT_MAX 1024u
#define STIM_COMMAND_MAX 8u
#define STIM_RECORD_MAX 32u
#define STIM_PAYLOAD_MAX 145u
#define STIM_LINE_MAX (2u * (STIM_PAYLOAD_MAX + 5u) + 1u)
#define STIM_COMMAND_BYTES 45u

enum stim_state { STIM_DISARMED, STIM_ARMED, STIM_REQUESTED, STIM_CAPTURE,
                  STIM_STOPPING, STIM_DONE, STIM_FAULT };
enum stim_error { STIM_OK, STIM_PROFILE, STIM_PROTOCOL, STIM_EXPIRED,
                  STIM_REPLAY, STIM_DESCRIPTOR, STIM_NONCE, STIM_QUEUE_LOSS,
                  STIM_CAPTURE_LIMIT, STIM_CALLBACK, STIM_REJECTED,
                  STIM_TX_ERROR, STIM_RX_ERROR, STIM_RX_LENGTH, STIM_CLOCK,
                  STIM_WORK, STIM_STOP_ERROR, STIM_UART, STIM_BUFFER };
enum stim_command { STIM_STATUS = 0, STIM_ARM = 1, STIM_RUN = 2 };
enum stim_record_type { STIM_HELLO = 0x80, STIM_ARMED_RECORD, STIM_TX_REQUESTED,
                       STIM_SCHEDULED, STIM_PHY_TX_DONE, STIM_RX_FRAME,
                       STIM_RX_ERROR_RECORD, STIM_STOP_REQUESTED,
                       STIM_STOP_ACCEPTED, STIM_TERMINAL, STIM_FAULT_RECORD,
                       STIM_TX_ERROR_RECORD };
enum stim_action { STIM_NOTHING, STIM_SUBMIT, STIM_SLEEP };

struct stim_record {
    uint16_t sequence;
    uint8_t type, length, payload[STIM_PAYLOAD_MAX];
};

struct stim_control {
    struct stim_record queue[STIM_QUEUE], fault_record, terminal_record;
    uint32_t last_ms, arm_ms, run_ms, phy_ms, stop_ms, partial_ms;
    uint16_t sequence, work, stop_work, input_work;
    uint8_t head, count, commands, received, state, error;
    uint8_t nonce[8], input[STIM_COMMAND_BYTES];
    uint8_t input_nibbles;
    bool consumed, issued, schedule_seen, scheduled, phy_done, tx_retired;
    bool stop_needed, stop_issued, stop_result_seen, stop_accepted, stopped;
    bool fault_pending, terminal_pending, terminal_made;
    bool sdk_ready, startup_pending;
};

extern const uint8_t stim_descriptor[32];
extern const uint8_t stim_tx_frame[23];

/* All calls on an instance require the same short exclusive critical section. */
/* stim_init models an already initialized SDK. Firmware uses the startup pair. */
void stim_init(struct stim_control *s, uint32_t now, bool profile_asleep);
bool stim_startup_begin(struct stim_control *s, uint32_t now, bool cold_disabled);
void stim_startup_complete(struct stim_control *s, uint32_t now,
                           bool profile_valid, bool disabled);
void stim_tick(struct stim_control *s, uint32_t now);
void stim_byte(struct stim_control *s, uint32_t now, uint8_t byte);
void stim_command(struct stim_control *s, uint32_t now, uint8_t command,
                  const uint8_t *payload, size_t length);
enum stim_action stim_action(struct stim_control *s, uint32_t now);
void stim_schedule(struct stim_control *s, uint32_t now, bool accepted);
void stim_phy_done(struct stim_control *s, uint32_t now);
void stim_tx_failed(struct stim_control *s, uint32_t now, uint8_t reason);
void stim_rx(struct stim_control *s, uint32_t now, const uint8_t *body,
             size_t length, int8_t rssi, uint8_t lqi);
void stim_rx_failed(struct stim_control *s, uint32_t now, uint8_t reason);
void stim_stop_result(struct stim_control *s, uint32_t now, bool accepted);
void stim_stop_observed(struct stim_control *s, uint32_t now, bool disabled);
void stim_fault(struct stim_control *s, uint32_t now, enum stim_error error);
bool stim_pop(struct stim_control *s, struct stim_record *out);
uint16_t stim_crc(const uint8_t *data, size_t length);
size_t stim_encode(uint8_t type, const uint8_t *payload, size_t length,
                   char *out, size_t capacity);

#endif
