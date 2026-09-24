/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "ed_wire.h"
#include <stddef.h>
#include <string.h>

static MCU_XDATA struct {
    uint8_t header[NWK_FRAME_MAX_BODY], encoded[NWK_FRAME_MAX_BODY];
    nwk_frame_info_t nwk;
    aps_frame_info_t aps;
    nwk_header_t transmit;
    aps_header_t application;
    ed_packet_t packet;
} syntax;

static MCU_XDATA struct {
    uint8_t frame[NWK_FRAME_MAX_BODY], text[NWK_FRAME_MAX_BODY], nonce[13], written;
    zigbee_security_info_t info;
} crypto;

static uint32_t counter_value(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wipe(void)
{
    volatile uint8_t MCU_XDATA *p = (volatile uint8_t MCU_XDATA *)&crypto;
    uint16_t i;
    for (i = 0; i < sizeof(crypto); i++) p[i] = 0;
    p = (volatile uint8_t MCU_XDATA *)&syntax;
    for (i = 0; i < sizeof(syntax); i++) p[i] = 0;
}

zigbee_security_result_t ed_wire_nwk(const uint8_t *frame, uint16_t length,
                                     nwk_frame_info_t *info)
{
    uint8_t type, h;
    if (!frame || !info) return ZIGBEE_SECURITY_ARGUMENT;
    if (length < 8 || length > NWK_FRAME_MAX_BODY) return ZIGBEE_SECURITY_LENGTH;
    type = frame[0] & 3;
    if (type > ED_NWK_COMMAND) return ZIGBEE_SECURITY_HEADER;
    h = (uint8_t)(8u+((frame[1] & 8u) ? 8u : 0u)+((frame[1] & 16u) ? 8u : 0u));
    if (length < h) return ZIGBEE_SECURITY_LENGTH;
    memcpy(syntax.header, frame, h);
    /* Data and Command share this header. Only this syntax copy is normalized;
     * the original type/security octets remain in authenticated AAD.
     */
    syntax.header[0] &= 0xfcu;
    syntax.header[1] &= (uint8_t)~2u;
    if (nwk_frame_decode(syntax.header, length, &syntax.nwk) != NWK_CODEC_OK)
        return ZIGBEE_SECURITY_HEADER;
    syntax.nwk.header.type = type;
    syntax.nwk.header.flags |= (uint16_t)(frame[1] & 2u) << 8;
    *info = syntax.nwk;
    return ZIGBEE_SECURITY_OK;
}

zigbee_security_result_t ed_wire_aps(const uint8_t *frame, uint16_t length,
                                     aps_frame_info_t *info)
{
    uint8_t type, delivery, flags, short_header;
    if (!frame || !info) return ZIGBEE_SECURITY_ARGUMENT;
    if (length < 2 || length > APS_FRAME_MAX_BODY) return ZIGBEE_SECURITY_LENGTH;
    type = frame[0] & 3u; delivery = (frame[0] >> 2) & 3u; flags = frame[0] & 0xf0u;
    short_header = type == ED_APS_COMMAND || (type == ED_APS_ACK && (flags & APS_FLAG_ACK_FORMAT));
    if (type > ED_APS_ACK || (delivery != 0 && delivery != APS_DELIVERY_BROADCAST) ||
        (flags & APS_FLAG_EXTENDED_HEADER) || (type != 0 && delivery) ||
        (type != ED_APS_ACK && (flags & APS_FLAG_ACK_FORMAT)) ||
        ((type == ED_APS_ACK || delivery) && (flags & APS_FLAG_ACK_REQUEST)))
        return ZIGBEE_SECURITY_HEADER;
    memset(&syntax.aps, 0, sizeof(syntax.aps));
    if (short_header) {
        syntax.aps.header.counter = frame[1];
        syntax.aps.payload_offset = 2;
    } else {
        if (length < 8) return ZIGBEE_SECURITY_LENGTH;
        memcpy(syntax.header, frame, 8);
        syntax.header[0] &= APS_FLAG_ACK_REQUEST;
        if (aps_frame_decode(syntax.header, length, &syntax.aps) != APS_CODEC_OK)
            return ZIGBEE_SECURITY_HEADER;
    }
    syntax.aps.header.type = type; syntax.aps.header.delivery_mode = delivery; syntax.aps.header.flags = flags;
    syntax.aps.payload_length = (uint8_t)(length - syntax.aps.payload_offset);
    *info = syntax.aps;
    return ZIGBEE_SECURITY_OK;
}

zigbee_security_result_t ed_wire_decode(const uint8_t *frame, uint16_t length, ed_packet_t *packet)
{
    zigbee_security_result_t result;
    uint8_t offset, size;
    if (!packet) return ZIGBEE_SECURITY_ARGUMENT;
    result = ed_wire_nwk(frame, length, &syntax.nwk);
    if (result != ZIGBEE_SECURITY_OK) return result;
    if (syntax.nwk.header.flags & NWK_FLAG_SECURITY) return ZIGBEE_SECURITY_HEADER;
    memset(&syntax.packet, 0, sizeof(syntax.packet));
    syntax.packet.nwk = syntax.nwk.header;
    offset = syntax.nwk.payload_offset; size = syntax.nwk.payload_length;
    if (!syntax.packet.nwk.type) {
        result = ed_wire_aps(frame+offset, size, &syntax.aps);
        if (result != ZIGBEE_SECURITY_OK) return result;
        if (syntax.aps.header.flags & APS_FLAG_SECURITY) return ZIGBEE_SECURITY_HEADER;
        syntax.packet.aps = syntax.aps.header;
        offset += syntax.aps.payload_offset; size = syntax.aps.payload_length;
        if (syntax.packet.aps.type == ED_APS_ACK && size) return ZIGBEE_SECURITY_LENGTH;
        if (syntax.packet.aps.type == ED_APS_COMMAND && !size) return ZIGBEE_SECURITY_LENGTH;
    } else if (!size) return ZIGBEE_SECURITY_LENGTH;
    if (size > ED_PAYLOAD_MAX) return ZIGBEE_SECURITY_LENGTH;
    syntax.packet.length = size;
    memcpy(syntax.packet.payload, frame+offset, size);
    *packet = syntax.packet;
    wipe();
    return ZIGBEE_SECURITY_OK;
}

zigbee_security_result_t ed_wire_encode(const ed_packet_t *packet, uint8_t *frame,
                                        uint16_t capacity, uint8_t *length)
{
    uint8_t n, total, control;
    if (!packet || !frame || !length) return ZIGBEE_SECURITY_ARGUMENT;
    if (packet->length > ED_PAYLOAD_MAX || packet->nwk.type > ED_NWK_COMMAND ||
        (packet->nwk.flags & NWK_FLAG_SECURITY) || (packet->aps.flags & APS_FLAG_SECURITY))
        return ZIGBEE_SECURITY_ARGUMENT;
    if (!packet->nwk.type && (packet->aps.type > ED_APS_ACK || packet->aps.delivery_mode > 3 ||
                              (packet->aps.flags & 0x0fu)))
        return ZIGBEE_SECURITY_ARGUMENT;
    n = packet->length;
    if (packet->nwk.type) {
        if (!n) return ZIGBEE_SECURITY_LENGTH;
        memcpy(syntax.encoded, packet->payload, n);
    } else {
        control = packet->aps.type | (uint8_t)(packet->aps.delivery_mode << 2) | packet->aps.flags;
        if (packet->aps.type == ED_APS_COMMAND ||
            (packet->aps.type == ED_APS_ACK && (packet->aps.flags & APS_FLAG_ACK_FORMAT))) {
            syntax.encoded[0] = control; syntax.encoded[1] = packet->aps.counter;
            memcpy(syntax.encoded+2, packet->payload, n);
            n += 2;
        } else {
            syntax.application = packet->aps;
            syntax.application.type = 0; syntax.application.delivery_mode = 0;
            syntax.application.flags &= APS_FLAG_ACK_REQUEST;
            if (aps_frame_encode(&syntax.application, packet->payload, n,
                                 syntax.encoded, sizeof(syntax.encoded), &n) != APS_CODEC_OK)
                return ZIGBEE_SECURITY_HEADER;
            syntax.encoded[0] = control;
        }
        if (ed_wire_aps(syntax.encoded, n, &syntax.aps) != ZIGBEE_SECURITY_OK ||
            (packet->aps.type == ED_APS_ACK && packet->length) ||
            (packet->aps.type == ED_APS_COMMAND && !packet->length))
            return ZIGBEE_SECURITY_HEADER;
    }
    syntax.transmit = packet->nwk; syntax.transmit.type = 0;
    if (nwk_frame_encode(&syntax.transmit, syntax.encoded, n, syntax.header,
                         sizeof(syntax.header), &total) != NWK_CODEC_OK)
        return ZIGBEE_SECURITY_HEADER;
    if (total > capacity) return ZIGBEE_SECURITY_SPACE;
    syntax.header[0] |= packet->nwk.type;
    memcpy(frame, syntax.header, total); *length = total;
    wipe();
    return ZIGBEE_SECURITY_OK;
}

static zigbee_security_result_t header(uint8_t layer, const uint8_t *frame, uint16_t length,
                                       uint8_t *size)
{
    zigbee_security_result_t result;
    if (layer > ZIGBEE_SECURITY_APS) return ZIGBEE_SECURITY_ARGUMENT;
    if (!layer) {
        result = ed_wire_nwk(frame, length, &syntax.nwk);
        if (!result) *size = syntax.nwk.payload_offset;
    } else {
        result = ed_wire_aps(frame, length, &syntax.aps);
        if (!result) *size = syntax.aps.payload_offset;
    }
    return result;
}

static zigbee_security_result_t inspect(uint8_t layer, const uint8_t *frame, uint16_t length)
{
    zigbee_security_result_t result;
    uint8_t h, c, a;
    result = header(layer, frame, length, &h);
    if (result) return result;
    if (!(frame[layer ? 0 : 1] & (layer ? APS_FLAG_SECURITY : 2)))
        return ZIGBEE_SECURITY_HEADER;
    if (length < h+5u) return ZIGBEE_SECURITY_LENGTH;
    c = frame[h]; a = (uint8_t)(5u+((c & 0x20u) ? 8u : 0u)+(((c >> 3) & 3u) == 1));
    if ((c & 0xc0u) || length < h+a+4u) return ZIGBEE_SECURITY_LENGTH;
    crypto.info.meta.header_length = h; crypto.info.meta.auxiliary_length = a;
    crypto.info.meta.key_identifier = (c >> 3) & 3u;
    crypto.info.meta.extended_nonce = (c >> 5) & 1u;
    crypto.info.meta.counter = counter_value(frame+h+1);
    crypto.info.meta.key_sequence = crypto.info.meta.key_identifier == 1 ? frame[h+a-1] : 0;
    if (c & 0x20u) memcpy(crypto.info.meta.source, frame+h+5, 8);
    crypto.info.meta.level = 5;
    crypto.info.meta.payload_length = (uint8_t)(length-h-a-4u);
    if (crypto.info.meta.counter == 0xffffffffUL) return ZIGBEE_SECURITY_COUNTER;
    if (!layer && (crypto.info.meta.key_identifier != 1 || !(c & 0x20)))
        return ZIGBEE_SECURITY_SELECTOR;
    if (layer && (crypto.info.meta.key_identifier == 1 || !(c & 0x20) ||
                  (syntax.aps.header.type != ED_APS_COMMAND && crypto.info.meta.key_identifier)))
        return ZIGBEE_SECURITY_SELECTOR;
    return ZIGBEE_SECURITY_OK;
}

zigbee_security_result_t ed_wire_inspect(uint8_t layer, const uint8_t *frame, uint16_t length,
                                        zigbee_security_meta_t *meta)
{
    zigbee_security_result_t result;
    if (!frame || !meta) return ZIGBEE_SECURITY_ARGUMENT;
    memset(&crypto, 0, sizeof(crypto));
    result = inspect(layer, frame, length);
    if (!result) *meta = crypto.info.meta;
    wipe();
    return result;
}

zigbee_security_result_t ed_wire_crypt(uint8_t open, uint8_t layer,
    const zigbee_security_key_t *key, const uint8_t *frame, uint16_t length,
    uint8_t *output, uint16_t capacity, zigbee_security_info_t *info)
{
    zigbee_security_result_t result;
    ccm_star_result_t encrypted;
    uint8_t h, a, p, i, total;
    if (!key || !frame || !output || !info || open > 1 || layer > 1)
        return ZIGBEE_SECURITY_ARGUMENT;
    if (key->level != 5 || key->extended_nonce != 1) return ZIGBEE_SECURITY_LEVEL;
    if ((!layer && key->key_identifier != 1) ||
        (layer && (key->key_identifier == 1 || key->key_identifier > 3)))
        return ZIGBEE_SECURITY_SELECTOR;
    memset(&crypto, 0, sizeof(crypto));
    result = open ? inspect(layer, frame, length) : header(layer, frame, length, &crypto.info.meta.header_length);
    if (result) goto done;
    h = crypto.info.meta.header_length;
    if (open) {
        if (crypto.info.meta.key_identifier != key->key_identifier ||
            (key->key_identifier == 1 && crypto.info.meta.key_sequence != key->key_sequence) ||
            memcmp(crypto.info.meta.source, key->source, 8)) {
            result = ZIGBEE_SECURITY_SOURCE; goto done;
        }
        a = crypto.info.meta.auxiliary_length; p = crypto.info.meta.payload_length;
        total = h+p;
        memcpy(crypto.frame, frame, length);
        crypto.frame[h] = (crypto.frame[h] & 0xf8u) | 5u;
    } else {
        if (frame[layer ? 0 : 1] & (layer ? APS_FLAG_SECURITY : 2)) {
            result = ZIGBEE_SECURITY_HEADER; goto done;
        }
        if (key->counter == 0xffffffffUL) { result = ZIGBEE_SECURITY_COUNTER; goto done; }
        if (layer && syntax.aps.header.type != ED_APS_COMMAND && key->key_identifier) {
            result = ZIGBEE_SECURITY_SELECTOR; goto done;
        }
        a = key->key_identifier == 1 ? 14 : 13; p = (uint8_t)(length-h);
        total = (uint8_t)(h+a+p+4);
        if (total > (layer ? APS_FRAME_MAX_BODY : NWK_FRAME_MAX_BODY)) {
            result = ZIGBEE_SECURITY_LENGTH; goto done;
        }
        memcpy(crypto.frame, frame, h);
        crypto.frame[layer ? 0 : 1] |= layer ? APS_FLAG_SECURITY : 2;
        crypto.frame[h] = 0x25u | (uint8_t)(key->key_identifier << 3);
        for (i = 0; i < 4; i++) crypto.frame[h+1+i] = (uint8_t)(key->counter >> (8u*i));
        memcpy(crypto.frame+h+5, key->source, 8);
        if (key->key_identifier == 1) crypto.frame[h+a-1] = key->key_sequence;
        memcpy(crypto.frame+h+a, frame+h, p);
        crypto.info.meta.counter = key->counter; crypto.info.meta.key_identifier = key->key_identifier;
        crypto.info.meta.key_sequence = key->key_identifier == 1 ? key->key_sequence : 0;
        crypto.info.meta.extended_nonce = 1; crypto.info.meta.level = 5;
        crypto.info.meta.auxiliary_length = a; crypto.info.meta.payload_length = p;
        memcpy(crypto.info.meta.source, key->source, 8);
    }
    if (layer && ((syntax.aps.header.type == ED_APS_COMMAND && !p) ||
                  (syntax.aps.header.type == ED_APS_ACK && p))) {
        result = ZIGBEE_SECURITY_LENGTH; goto done;
    }
    if (capacity < total) { result = ZIGBEE_SECURITY_SPACE; goto done; }
    memcpy(crypto.nonce, key->source, 8);
    memcpy(crypto.nonce+8, crypto.frame+h+1, 4); crypto.nonce[12] = crypto.frame[h];
    encrypted = ccm_star_crypt(open, key->key, crypto.nonce, crypto.frame, h+a,
        crypto.frame+h+a, p+(open ? 4u : 0u), 4, crypto.text, sizeof(crypto.text),
        &crypto.written, &key->limits, &crypto.info.crypto);
    if (encrypted) {
        result = encrypted == CCM_STAR_AUTH ? ZIGBEE_SECURITY_AUTH :
                 encrypted == CCM_STAR_AES ? ZIGBEE_SECURITY_AES : ZIGBEE_SECURITY_CCM;
        goto done;
    }
    if (open) {
        crypto.frame[layer ? 0 : 1] &= (uint8_t)~(layer ? APS_FLAG_SECURITY : 2u);
        memcpy(crypto.frame+h, crypto.text, p);
    } else {
        memcpy(crypto.frame+h+a, crypto.text, crypto.written);
        crypto.frame[h] &= 0xf8u;
    }
    memcpy(output, crypto.frame, total); crypto.info.length = total; *info = crypto.info;
    result = ZIGBEE_SECURITY_OK;
done:
    wipe();
    return result;
}
