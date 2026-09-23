/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "zigbee_security.h"
#include "aps_frame.h"
#include "nwk_frame.h"
#include <stddef.h>
#include <string.h>

static MCU_XDATA struct {
    uint8_t packet[NWK_FRAME_MAX_BODY], payload[CCM_STAR_MAX_MESSAGE + 16], nonce[13];
    nwk_frame_info_t nwk;
    aps_frame_info_t aps;
    zigbee_security_info_t info;
    uint8_t size, mic, command;
} state;

static void wipe(void)
{
    volatile uint8_t MCU_XDATA *p = (volatile uint8_t MCU_XDATA *)&state;
    uint16_t i;
    for (i = 0; i < sizeof(state); i++)
        p[i] = 0;
}

static void security_flag(uint8_t layer, uint8_t enabled)
{
    if (layer == ZIGBEE_SECURITY_NWK) {
        state.packet[1] &= (uint8_t)~(NWK_FLAG_SECURITY >> 8);
        if (enabled)
            state.packet[1] |= (uint8_t)(NWK_FLAG_SECURITY >> 8);
    } else {
        state.packet[0] &= (uint8_t)~APS_FLAG_SECURITY;
        if (enabled)
            state.packet[0] |= APS_FLAG_SECURITY;
    }
}

static zigbee_security_result_t header(uint8_t layer, uint8_t secured, uint8_t level,
                                       const uint8_t * volatile frame, uint16_t length)
{
    uint8_t present;
    if (layer > ZIGBEE_SECURITY_APS)
        return ZIGBEE_SECURITY_ARGUMENT;
    if (!level || level == 4 || level > 7)
        return ZIGBEE_SECURITY_LEVEL;
    if (length < 2 || length > (layer == ZIGBEE_SECURITY_NWK ?
                               NWK_FRAME_MAX_BODY : APS_FRAME_MAX_BODY))
        return ZIGBEE_SECURITY_LENGTH;
    memcpy(state.packet, frame, length);
    present = layer == ZIGBEE_SECURITY_NWK ?
              !!(frame[1] & (NWK_FLAG_SECURITY >> 8)) : !!(frame[0] & APS_FLAG_SECURITY);
    if (present != secured)
        return ZIGBEE_SECURITY_HEADER;
    security_flag(layer, 0);
    state.command = 0;
    if (layer == ZIGBEE_SECURITY_NWK) {
        if (nwk_frame_decode(state.packet, length, &state.nwk) != NWK_CODEC_OK)
            return ZIGBEE_SECURITY_HEADER;
        state.info.meta.header_length = state.nwk.payload_offset;
    } else if ((frame[0] & 3u) == 1) {
        if (state.packet[0] & (uint8_t)~(1u | APS_FLAG_ACK_REQUEST))
            return ZIGBEE_SECURITY_HEADER;
        state.command = 1;
        state.info.meta.header_length = 2;
    } else {
        if (aps_frame_decode(state.packet, length, &state.aps) != APS_CODEC_OK)
            return ZIGBEE_SECURITY_HEADER;
        state.info.meta.header_length = state.aps.payload_offset;
    }
    security_flag(layer, 1);
    state.info.meta.level = level;
    state.mic = (uint8_t)(2u << (level & 3u));
    return ZIGBEE_SECURITY_OK;
}

static zigbee_security_result_t shape(uint8_t layer)
{
    if (state.info.meta.extended_nonce > 1 || state.info.meta.key_identifier > 3)
        return ZIGBEE_SECURITY_AUX;
    if (layer == ZIGBEE_SECURITY_NWK) {
        if (state.info.meta.key_identifier != 1 || !state.info.meta.extended_nonce)
            return ZIGBEE_SECURITY_AUX;
    } else if (state.info.meta.key_identifier == 1 ||
               (!state.command && state.info.meta.key_identifier != 0) ||
               (state.command && !state.info.meta.extended_nonce)) {
        return ZIGBEE_SECURITY_AUX;
    }
    if (state.info.meta.counter == 0xffffffffUL)
        return ZIGBEE_SECURITY_COUNTER;
    state.info.meta.auxiliary_length = (uint8_t)(5u + 8u * state.info.meta.extended_nonce +
                                               (state.info.meta.key_identifier == 1));
    return ZIGBEE_SECURITY_OK;
}

static uint32_t read_counter(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static zigbee_security_result_t inspect(uint8_t layer, uint8_t level,
                                        const uint8_t * volatile frame, volatile uint16_t length)
{
    volatile uint8_t h, control;
    zigbee_security_result_t result = header(layer, 1, level, frame, length);
    if (result != ZIGBEE_SECURITY_OK)
        return result;
    h = state.info.meta.header_length;
    if (length < h + 5u)
        return ZIGBEE_SECURITY_LENGTH;
    control = frame[h];
    if (control & 0xc0u)
        return ZIGBEE_SECURITY_AUX;
    state.info.meta.key_identifier = (control >> 3) & 3u;
    state.info.meta.extended_nonce = (control >> 5) & 1u;
    state.info.meta.counter = read_counter(frame + h + 1u);
    result = shape(layer);
    if (result != ZIGBEE_SECURITY_OK)
        return result;
    if (length < h + state.info.meta.auxiliary_length + state.mic)
        return ZIGBEE_SECURITY_LENGTH;
    if (state.info.meta.extended_nonce)
        memcpy(state.info.meta.source, frame + h + 5u, 8);
    if (state.info.meta.key_identifier == 1)
        state.info.meta.key_sequence = frame[h + state.info.meta.auxiliary_length - 1u];
    state.info.meta.payload_length = (uint8_t)(length - h - state.info.meta.auxiliary_length - state.mic);
    if (state.command && !state.info.meta.payload_length)
        return ZIGBEE_SECURITY_LENGTH;
    state.packet[h] = (control & 0xf8u) | level;
    return ZIGBEE_SECURITY_OK;
}

zigbee_security_result_t zigbee_security_inspect(
    uint8_t layer, uint8_t level, const uint8_t * volatile frame, uint16_t length,
    zigbee_security_meta_t * volatile meta)
{
    zigbee_security_result_t result;
    if (frame == NULL || meta == NULL)
        return ZIGBEE_SECURITY_ARGUMENT;
    memset(&state, 0, sizeof(state));
    result = inspect(layer, level, frame, length);
    if (result == ZIGBEE_SECURITY_OK)
        *meta = state.info.meta;
    wipe();
    return result;
}

zigbee_security_result_t zigbee_security_crypt(
    volatile uint8_t open, volatile uint8_t layer, const zigbee_security_key_t * volatile key,
    const uint8_t * volatile frame, uint16_t length,
    uint8_t * volatile output, uint16_t capacity,
    zigbee_security_info_t * volatile info)
{
    zigbee_security_result_t result;
    ccm_star_result_t crypto;
    volatile uint8_t h, a, p, i, total;
    volatile uint16_t auth_length, cipher_length;
    const uint8_t * volatile cipher_input;
    if (open > 1 || key == NULL || frame == NULL || output == NULL || info == NULL)
        return ZIGBEE_SECURITY_ARGUMENT;
    memset(&state, 0, sizeof(state));
    result = open ? inspect(layer, key->level, frame, length) :
                    header(layer, 0, key->level, frame, length);
    if (result != ZIGBEE_SECURITY_OK)
        goto done;
    h = state.info.meta.header_length;
    if (open) {
        if (state.info.meta.key_identifier != key->key_identifier ||
            state.info.meta.extended_nonce != key->extended_nonce ||
            (key->key_identifier == 1 && state.info.meta.key_sequence != key->key_sequence)) {
            result = ZIGBEE_SECURITY_SELECTOR;
            goto done;
        }
        if (state.info.meta.extended_nonce && memcmp(state.info.meta.source, key->source, 8)) {
            result = ZIGBEE_SECURITY_SOURCE;
            goto done;
        }
    } else {
        state.info.meta.key_identifier = key->key_identifier;
        state.info.meta.extended_nonce = key->extended_nonce;
        state.info.meta.key_sequence = key->key_identifier == 1 ? key->key_sequence : 0;
        state.info.meta.counter = key->counter;
        result = shape(layer);
        if (result != ZIGBEE_SECURITY_OK)
            goto done;
        state.info.meta.payload_length = (uint8_t)(length - h);
        if (state.command && !state.info.meta.payload_length) {
            result = ZIGBEE_SECURITY_LENGTH;
            goto done;
        }
    }
    memcpy(state.info.meta.source, key->source, 8);
    a = state.info.meta.auxiliary_length;
    p = state.info.meta.payload_length;
    total = (uint8_t)(h + p + (open ? 0u : a + state.mic));
    if (total > (layer == ZIGBEE_SECURITY_NWK ? NWK_FRAME_MAX_BODY : APS_FRAME_MAX_BODY)) {
        result = ZIGBEE_SECURITY_LENGTH;
        goto done;
    }
    if (capacity < total) {
        result = ZIGBEE_SECURITY_SPACE;
        goto done;
    }
    if (!open) {
        state.packet[h] = key->level | (uint8_t)(key->key_identifier << 3) |
                          (uint8_t)(key->extended_nonce << 5);
        for (i = 0; i < 4; i++)
            state.packet[h + 1u + i] = (uint8_t)(key->counter >> (8u * i));
        if (key->extended_nonce)
            memcpy(state.packet + h + 5u, key->source, 8);
        if (key->key_identifier == 1)
            state.packet[h + a - 1u] = key->key_sequence;
        if (p)
            memcpy(state.packet + h + a, frame + h, p);
    }
    memcpy(state.nonce, key->source, 8);
    memcpy(state.nonce + 8, state.packet + h + 1u, 4);
    state.nonce[12] = state.packet[h];
    if (key->level & 4u) {
        auth_length = h + a;
        cipher_input = state.packet + auth_length;
        cipher_length = p + (open ? state.mic : 0u);
    } else {
        auth_length = h + a + p;
        cipher_input = state.packet + auth_length;
        cipher_length = open ? state.mic : 0u;
    }
    crypto = ccm_star_crypt(open, key->key, state.nonce, state.packet, auth_length,
                           cipher_input, cipher_length, state.mic, state.payload,
                           sizeof(state.payload), &state.size, &key->limits, &state.info.crypto);
    if (crypto != CCM_STAR_OK) {
        result = crypto == CCM_STAR_AUTH ? ZIGBEE_SECURITY_AUTH :
                 crypto == CCM_STAR_AES ? ZIGBEE_SECURITY_AES : ZIGBEE_SECURITY_CCM;
        goto done;
    }
    if (open) {
        security_flag(layer, 0);
        if (p) {
            if (key->level & 4u)
                memcpy(state.packet + h, state.payload, p);
            else
                memcpy(state.packet + h, frame + h + a, p);
        }
    } else {
        memcpy(state.packet + h + a + (key->level & 4u ? 0u : p), state.payload, state.size);
        state.packet[h] &= 0xf8u;
    }
    memcpy(output, state.packet, total);
    state.info.length = total;
    *info = state.info;
    result = ZIGBEE_SECURITY_OK;
done:
    wipe();
    return result;
}
