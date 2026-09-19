/* SPDX-License-Identifier: BSD-3-Clause */
#include "control.h"

#include <string.h>

const uint8_t stim_descriptor[32] = {
    0x9d,0xec,0x85,0x8b,0x7b,0x61,0xeb,0xf3,
    0x60,0x36,0x4b,0x4b,0x65,0x96,0xbe,0xc3,
    0x92,0xd3,0xf8,0xd1,0x13,0x71,0xc6,0xa5,
    0x80,0x60,0xd1,0x2f,0xc1,0x3f,0x09,0xb9
};
const uint8_t stim_tx_frame[23] = {
    22,0x61,0x88,0x51,0xfe,0xca,0x34,0x12,0x78,0x56,
    'N','S','5','1','-','P','U','B','L','I','C',0,0
};

static void le32(uint8_t *p, uint32_t value)
{
    unsigned i;
    for (i = 0; i < 4; ++i) {
        p[i] = (uint8_t)(value >> (8u * i));
    }
}

static void record(struct stim_control *s, struct stim_record *r,
                   uint32_t now, uint8_t type, const uint8_t *p, size_t n)
{
    memset(r, 0, sizeof(*r));
    r->sequence = ++s->sequence;
    r->type = type;
    r->length = (uint8_t)(16u + n);
    r->payload[0] = (uint8_t)r->sequence;
    r->payload[1] = (uint8_t)(r->sequence >> 8);
    r->payload[2] = s->state;
    r->payload[3] = s->error;
    le32(r->payload + 4, now);
    memcpy(r->payload + 8, s->nonce, 8);
    if (n != 0u) {
        memcpy(r->payload + 16, p, n);
    }
}

static void terminal(struct stim_control *s, uint32_t now)
{
    uint8_t p[6];
    if (s->terminal_made) {
        return;
    }
    s->terminal_made = true;
    if (s->error == STIM_OK &&
        !(s->consumed && s->issued && s->schedule_seen && s->scheduled &&
          s->phy_done && s->tx_retired && s->stopped)) {
        stim_fault(s, now, STIM_CALLBACK);
    }
    if (s->error == STIM_OK) {
        s->state = STIM_DONE;
    }
    p[0] = s->consumed;
    p[1] = s->phy_done;
    p[2] = s->received;
    p[3] = s->stopped;
    p[4] = s->tx_retired;
    p[5] = s->stop_issued; /* Sleep may truncate an incoming frame. */
    record(s, &s->terminal_record, now, STIM_TERMINAL, p, sizeof(p));
    s->terminal_pending = true;
}

void stim_fault(struct stim_control *s, uint32_t now, enum stim_error error)
{
    if (s->error != STIM_OK || error == STIM_OK) {
        return;
    }
    s->error = (uint8_t)error;
    s->state = STIM_FAULT;
    record(s, &s->fault_record, now, STIM_FAULT_RECORD, NULL, 0);
    s->fault_pending = true;
    if (s->stopped && s->sdk_ready) {
        terminal(s, now);
    } else {
        s->stop_needed = true;
    }
}

static bool emit(struct stim_control *s, uint32_t now, uint8_t type,
                 const uint8_t *p, size_t n)
{
    uint8_t index;
    if (s->sequence >= STIM_RECORD_MAX) {
        stim_fault(s, now, STIM_WORK);
        return false;
    }
    if (s->count == STIM_QUEUE || n > STIM_PAYLOAD_MAX - 16u) {
        stim_fault(s, now, STIM_QUEUE_LOSS);
        return false;
    }
    index = (uint8_t)((s->head + s->count) % STIM_QUEUE);
    record(s, &s->queue[index], now, type, p, n);
    ++s->count;
    return true;
}

static void initialize(struct stim_control *s, uint32_t now)
{
    memset(s, 0, sizeof(*s));
    s->last_ms = now;
}

void stim_init(struct stim_control *s, uint32_t now, bool profile_asleep)
{
    initialize(s, now);
    s->sdk_ready = true;
    s->stopped = profile_asleep;
    (void)emit(s, now, STIM_HELLO, stim_descriptor, 32);
    if (!profile_asleep) {
        stim_fault(s, now, STIM_PROFILE);
    }
}

bool stim_startup_begin(struct stim_control *s, uint32_t now, bool cold_disabled)
{
    initialize(s, now);
    if (!cold_disabled) {
        stim_fault(s, now, STIM_PROFILE);
        (void)emit(s, now, STIM_HELLO, stim_descriptor, 32);
        s->stop_needed = false;
        terminal(s, now);
        return false;
    }
    s->startup_pending = true;
    return true;
}

void stim_startup_complete(struct stim_control *s, uint32_t now,
                           bool profile_valid, bool disabled)
{
    if (!s->startup_pending || s->sdk_ready) {
        stim_fault(s, now, STIM_CALLBACK);
        return;
    }
    stim_tick(s, now);
    s->stopped = disabled;
    if (!profile_valid || !disabled) {
        stim_fault(s, now, STIM_PROFILE);
    }
    s->startup_pending = false;
    s->sdk_ready = true;
    (void)emit(s, now, STIM_HELLO, stim_descriptor, 32);
    if (s->error != STIM_OK) {
        s->stop_needed = !disabled;
        if (disabled) {
            terminal(s, now);
        }
    }
}

void stim_tick(struct stim_control *s, uint32_t now)
{
    if ((uint32_t)(now - s->last_ms) >= UINT32_C(0x80000000)) {
        stim_fault(s, now, STIM_CLOCK);
        return;
    }
    s->last_ms = now;
    if (s->input_nibbles != 0u &&
        (uint32_t)(now - s->partial_ms) >= STIM_PARTIAL_MS) {
        s->input_nibbles = 0;
        stim_fault(s, now, STIM_PROTOCOL);
    }
    if (s->state == STIM_ARMED &&
        (uint32_t)(now - s->arm_ms) >= STIM_ARM_MS) {
        stim_fault(s, now, STIM_EXPIRED);
    }
    if (s->state == STIM_ARMED) {
        if (s->work == STIM_ARM_WORK_MAX) {
            stim_fault(s, now, STIM_WORK);
        } else {
            ++s->work;
        }
    }
    if (s->issued && !s->terminal_made) {
        if (s->work == STIM_WORK_MAX) {
            stim_fault(s, now, STIM_WORK);
        } else {
            ++s->work;
        }
        if ((uint32_t)(now - s->run_ms) >= STIM_TRIAL_MS) {
            if (!s->phy_done) {
                stim_fault(s, now, STIM_EXPIRED);
            } else {
                s->stop_needed = true;
            }
        }
        if (s->phy_done && (uint32_t)(now - s->phy_ms) >= STIM_CAPTURE_MS) {
            s->stop_needed = true;
        }
    }
    if (s->stop_issued && !s->terminal_made) {
        if ((uint32_t)(now - s->stop_ms) >= STIM_STOP_MS ||
            s->stop_work == STIM_STOP_WORK_MAX) {
            stim_fault(s, now, STIM_STOP_ERROR);
            terminal(s, now);
        } else {
            ++s->stop_work;
        }
    }
}

void stim_command(struct stim_control *s, uint32_t now, uint8_t command,
                  const uint8_t *payload, size_t length)
{
    uint8_t any = 0;
    unsigned i;
    stim_tick(s, now);
    if (s->commands == STIM_COMMAND_MAX) {
        stim_fault(s, now, STIM_WORK);
        return;
    }
    ++s->commands;
    if (command == STIM_STATUS && length == 0u) {
        (void)emit(s, now, STIM_HELLO, stim_descriptor, 32);
        return;
    }
    if (command != STIM_ARM && command != STIM_RUN) {
        stim_fault(s, now, STIM_PROTOCOL);
        return;
    }
    if (length != 40u || payload == NULL) {
        stim_fault(s, now, STIM_PROTOCOL);
        return;
    }
    if (memcmp(payload, stim_descriptor, 32) != 0) {
        stim_fault(s, now, STIM_DESCRIPTOR);
        return;
    }
    for (i = 32; i < 40; ++i) {
        any |= payload[i];
    }
    if (any == 0u) {
        stim_fault(s, now, STIM_NONCE);
        return;
    }
    if (s->error != STIM_OK || s->consumed) {
        stim_fault(s, now, STIM_REPLAY);
        return;
    }
    if (command == STIM_ARM) {
        if (s->state != STIM_DISARMED || !s->stopped) {
            stim_fault(s, now, STIM_REPLAY);
            return;
        }
        memcpy(s->nonce, payload + 32, 8);
        s->arm_ms = now;
        s->state = STIM_ARMED;
        (void)emit(s, now, STIM_ARMED_RECORD, NULL, 0);
    } else {
        if (s->state != STIM_ARMED) {
            stim_fault(s, now, STIM_REPLAY);
            return;
        }
        if (memcmp(s->nonce, payload + 32, 8) != 0) {
            stim_fault(s, now, STIM_NONCE);
            return;
        }
        s->consumed = true;
        s->work = 0;
        s->run_ms = now;
        s->state = STIM_REQUESTED;
        (void)emit(s, now, STIM_TX_REQUESTED, NULL, 0);
    }
}

enum stim_action stim_action(struct stim_control *s, uint32_t now)
{
    stim_tick(s, now);
    if (!s->sdk_ready) {
        return STIM_NOTHING;
    }
    if (s->stop_needed && !s->stop_issued) {
        s->stop_issued = true;
        s->stop_ms = now;
        if (s->error == STIM_OK) {
            s->state = STIM_STOPPING;
        }
        (void)emit(s, now, STIM_STOP_REQUESTED, NULL, 0);
        return STIM_SLEEP;
    }
    if (s->state == STIM_REQUESTED && !s->issued && s->error == STIM_OK) {
        if ((uint32_t)(now - s->run_ms) >= STIM_TRIAL_MS) {
            stim_fault(s, now, STIM_EXPIRED);
            return STIM_NOTHING;
        }
        s->issued = true;
        s->stopped = false;
        return STIM_SUBMIT;
    }
    return STIM_NOTHING;
}

void stim_schedule(struct stim_control *s, uint32_t now, bool accepted)
{
    uint8_t p = accepted;
    stim_tick(s, now);
    if (!s->issued || s->schedule_seen) {
        stim_fault(s, now, STIM_CALLBACK);
        return;
    }
    s->schedule_seen = true;
    s->scheduled = accepted;
    (void)emit(s, now, STIM_SCHEDULED, &p, 1);
    if (!accepted) {
        stim_fault(s, now, s->phy_done ? STIM_CALLBACK : STIM_REJECTED);
    }
}

void stim_phy_done(struct stim_control *s, uint32_t now)
{
    stim_tick(s, now);
    if (!s->issued || s->phy_done || s->tx_retired) {
        stim_fault(s, now, STIM_CALLBACK);
        return;
    }
    if (s->schedule_seen && !s->scheduled) {
        stim_fault(s, now, STIM_CALLBACK);
    }
    s->phy_done = true;
    s->tx_retired = true;
    s->phy_ms = now;
    if (s->error == STIM_OK) {
        s->state = STIM_CAPTURE;
    }
    (void)emit(s, now, STIM_PHY_TX_DONE, NULL, 0);
}

void stim_tx_failed(struct stim_control *s, uint32_t now, uint8_t reason)
{
    stim_tick(s, now);
    if (!s->issued || s->tx_retired) {
        stim_fault(s, now, STIM_CALLBACK);
        return;
    }
    s->tx_retired = true;
    (void)emit(s, now, STIM_TX_ERROR_RECORD, &reason, 1);
    stim_fault(s, now, STIM_TX_ERROR);
}

void stim_rx(struct stim_control *s, uint32_t now, const uint8_t *body,
             size_t length, int8_t rssi, uint8_t lqi)
{
    uint8_t p[129];
    stim_tick(s, now);
    if (!s->phy_done || s->stopped || s->terminal_made) {
        stim_fault(s, now, STIM_CALLBACK);
        return;
    }
    if (body == NULL || length < 3u || length > 125u) {
        stim_fault(s, now, STIM_RX_LENGTH);
        return;
    }
    if (s->received == STIM_CAPTURE_MAX) {
        stim_fault(s, now, STIM_CAPTURE_LIMIT);
        return;
    }
    p[0] = (uint8_t)rssi;
    p[1] = lqi;
    p[2] = 1; /* Driver's validated CRC, NOT literal FCS bytes. */
    p[3] = (uint8_t)length;
    memcpy(p + 4, body, length);
    if (emit(s, now, STIM_RX_FRAME, p, length + 4u)) {
        ++s->received;
        if (s->received == STIM_CAPTURE_MAX) {
            stim_fault(s, now, STIM_CAPTURE_LIMIT);
        }
    }
}

void stim_rx_failed(struct stim_control *s, uint32_t now, uint8_t reason)
{
    stim_tick(s, now);
    (void)emit(s, now, STIM_RX_ERROR_RECORD, &reason, 1);
    stim_fault(s, now, STIM_RX_ERROR);
}

void stim_stop_result(struct stim_control *s, uint32_t now, bool accepted)
{
    uint8_t p = accepted;
    stim_tick(s, now);
    if (!s->stop_issued || s->stop_result_seen) {
        stim_fault(s, now, STIM_CALLBACK);
        return;
    }
    s->stop_result_seen = true;
    s->stop_accepted = accepted;
    (void)emit(s, now, STIM_STOP_ACCEPTED, &p, 1);
    if (!accepted) {
        stim_fault(s, now, STIM_STOP_ERROR);
        terminal(s, now);
    }
}

void stim_stop_observed(struct stim_control *s, uint32_t now, bool disabled)
{
    stim_tick(s, now);
    if (!s->stop_result_seen || !s->stop_accepted) {
        stim_fault(s, now, STIM_CALLBACK);
        return;
    }
    if (disabled && !s->terminal_made) {
        s->stopped = true;
        terminal(s, now);
    }
}

bool stim_pop(struct stim_control *s, struct stim_record *out)
{
    struct stim_record *r = s->count != 0u ? &s->queue[s->head] : NULL;
    if (out == NULL) {
        return false;
    }
    if (s->fault_pending && (r == NULL || s->fault_record.sequence < r->sequence)) {
        r = &s->fault_record;
    }
    if (s->terminal_pending &&
        (r == NULL || s->terminal_record.sequence < r->sequence)) {
        r = &s->terminal_record;
    }
    if (r == NULL) {
        return false;
    }
    *out = *r;
    if (r == &s->fault_record) {
        s->fault_pending = false;
    } else if (r == &s->terminal_record) {
        s->terminal_pending = false;
    } else {
        s->head = (uint8_t)((s->head + 1u) % STIM_QUEUE);
        --s->count;
    }
    return true;
}

uint16_t stim_crc(const uint8_t *data, size_t length)
{
    uint16_t crc = UINT16_C(0xffff);
    size_t i;
    unsigned bit;
    for (i = 0; i < length; ++i) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (bit = 0; bit < 8; ++bit) {
            crc = (uint16_t)((crc & UINT16_C(0x8000)) != 0u ?
                  ((uint32_t)crc << 1) ^ UINT32_C(0x1021) : (uint32_t)crc << 1);
        }
    }
    return crc;
}

size_t stim_encode(uint8_t type, const uint8_t *payload, size_t length,
                   char *out, size_t capacity)
{
    static const char hex[] = "0123456789ABCDEF";
    uint8_t raw[STIM_PAYLOAD_MAX + 5u];
    uint16_t crc;
    size_t i, n = length + 5u;
    if (length > STIM_PAYLOAD_MAX || capacity < 2u * n + 1u ||
        out == NULL || (length != 0u && payload == NULL)) {
        return 0;
    }
    raw[0] = 1;
    raw[1] = type;
    raw[2] = (uint8_t)length;
    if (length != 0u) {
        memcpy(raw + 3, payload, length);
    }
    crc = stim_crc(raw, length + 3u);
    raw[length + 3u] = (uint8_t)crc;
    raw[length + 4u] = (uint8_t)(crc >> 8);
    for (i = 0; i < n; ++i) {
        out[2u * i] = hex[raw[i] >> 4];
        out[2u * i + 1u] = hex[raw[i] & 15u];
    }
    out[2u * n] = '\n';
    return 2u * n + 1u;
}

void stim_byte(struct stim_control *s, uint32_t now, uint8_t byte)
{
    uint8_t digit, n;
    uint16_t crc;
    stim_tick(s, now);
    if (s->input_work == STIM_INPUT_MAX) {
        stim_fault(s, now, STIM_WORK);
        return;
    }
    ++s->input_work;
    if (byte == '\n') {
        n = (uint8_t)(s->input_nibbles / 2u);
        if ((s->input_nibbles & 1u) != 0u || n < 5u ||
            s->input[0] != 1u || s->input[2] != n - 5u) {
            s->input_nibbles = 0;
            stim_fault(s, now, STIM_PROTOCOL);
            return;
        }
        s->input_nibbles = 0;
        crc = stim_crc(s->input, n - 2u);
        if (s->input[n - 2u] != (uint8_t)crc ||
            s->input[n - 1u] != (uint8_t)(crc >> 8)) {
            stim_fault(s, now, STIM_PROTOCOL);
            return;
        }
        stim_command(s, now, s->input[1], s->input + 3, n - 5u);
        return;
    }
    if (byte >= '0' && byte <= '9') {
        digit = (uint8_t)(byte - '0');
    } else if (byte >= 'A' && byte <= 'F') {
        digit = (uint8_t)(byte - 'A' + 10u);
    } else {
        stim_fault(s, now, STIM_PROTOCOL);
        return;
    }
    if (s->input_nibbles == 2u * STIM_COMMAND_BYTES) {
        stim_fault(s, now, STIM_PROTOCOL);
        return;
    }
    if (s->input_nibbles == 0u) {
        s->partial_ms = now;
    }
    n = (uint8_t)(s->input_nibbles / 2u);
    if ((s->input_nibbles & 1u) == 0u) {
        s->input[n] = (uint8_t)(digit << 4);
    } else {
        s->input[n] |= digit;
    }
    ++s->input_nibbles;
}
