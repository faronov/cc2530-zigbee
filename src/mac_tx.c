/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "mac_tx.h"

#include <stddef.h>
#include <string.h>

static uint8_t reached(uint32_t now, uint32_t at)
{
    return (uint32_t)(now - at) < MAC_TX_HALF;
}

mac_tx_result_t mac_tx_init(mac_tx_t * volatile tx, uint8_t random_dsn, uint32_t now)
{
    if (tx == NULL)
        return MAC_TX_INVALID;
    memset(tx, 0, sizeof(*tx));
    tx->next_dsn = random_dsn;
    tx->last = now;
    tx->ready_at = now;
    return MAC_TX_OK;
}

mac_tx_result_t mac_tx_submit(mac_tx_t * volatile tx,
                              const uint8_t * volatile body, uint16_t length,
                              uint32_t now, uint32_t lifetime, uint16_t work_limit)
{
    mac_frame_info_t decoded;
    uint8_t command;

    if (tx == NULL || body == NULL || lifetime == 0u
            || lifetime >= MAC_TX_HALF || work_limit == 0u)
        return MAC_TX_INVALID;
    if (tx->phase != MAC_TX_IDLE)
        return MAC_TX_FULL;
    if ((uint32_t)(now - tx->last) >= MAC_TX_HALF)
        return MAC_TX_INVALID;
    if (tx->generation == UINT32_MAX)
        return MAC_TX_GENERATION_EXHAUSTED;
    if (mac_frame_decode(body, length, &decoded) != MAC_CODEC_OK)
        return MAC_TX_UNSUPPORTED;
    if (decoded.header.flags & MAC_FLAG_PENDING)
        return MAC_TX_UNSUPPORTED;
    /* The unchanged codec establishes command lengths/addressing/ACK, including
     * Association Request source PAN FFFF and no compression.
     * DATA behavior is unchanged: compressed equality is guaranteed by decode.
     * Pending's receive-only allowance never authorizes transmission.
     */
    if (decoded.header.type == MAC_FRAME_COMMAND) {
        /* IEEE 2006 7.3.1/Figure56; selected ED policy: RX-on, address allocation,
         * no coordinator/FFD/MAC-security capability; truthful caller power bit.
         * The codec has already validated addressing, ACK and exact length.
         * Only after identifying Association Request is the last byte CAP.
         */
        command = body[decoded.payload_offset];
        if (!(command == MAC_COMMAND_BEACON_REQUEST
                || (decoded.header.destination_pan != 0xffffu
                    && ((command == MAC_COMMAND_ASSOCIATION_REQUEST
                         && (uint8_t)(body[(uint8_t)(length - 1u)] & 0xfbu) == 0x88u)
                        || (command == MAC_COMMAND_DATA_REQUEST
                            && decoded.header.destination_mode != MAC_ADDRESS_NONE)))))
            return MAC_TX_UNSUPPORTED;
    } else if (decoded.header.type != MAC_FRAME_DATA
            || decoded.header.source_pan == 0xffffu
            || decoded.header.destination_pan == 0xffffu
            || (!(decoded.header.flags & MAC_FLAG_PAN_COMPRESSION)
                && decoded.header.source_pan == decoded.header.destination_pan)
            || (decoded.header.source_mode == MAC_ADDRESS_SHORT
                && decoded.header.source[0] == 0xfeu && decoded.header.source[1] == 0xffu)
            || (decoded.header.destination_mode == MAC_ADDRESS_SHORT
                && decoded.header.destination[0] == 0xfeu && decoded.header.destination[1] == 0xffu))
        return MAC_TX_UNSUPPORTED;
    memcpy(tx->frame, body, length);
    tx->frame[2] = tx->next_dsn++;
    tx->length = (uint8_t)length;
    tx->ack_requested = (decoded.header.flags & MAC_FLAG_ACK_REQUEST) != 0u;
    tx->generation++;
    if (reached(tx->last, tx->ready_at) || reached(now, tx->ready_at))
        tx->ready_at = now;
    tx->last = now;
    tx->deadline = now + lifetime;
    tx->steps = work_limit;
    tx->phase = MAC_TX_DRAW;
    tx->nb = 0;
    tx->be = MAC_TX_MIN_BE;
    /* Six contiguous uint8_t fields, retries through retry_pending; NONE is 0.
     * Use the enclosing object's byte representation. Preserve stop_steps and
     * all other fields, including inactive timestamps and the copied tail.
     */
    memset((uint8_t *)tx + offsetof(mac_tx_t, retries), 0,
           offsetof(mac_tx_t, stop_steps) - offsetof(mac_tx_t, retries));
    return MAC_TX_OK;
}

mac_tx_result_t mac_tx_copy(const mac_tx_t * volatile tx,
                            uint8_t * volatile body, uint16_t capacity,
                            uint8_t * volatile length)
{
    if (tx == NULL || body == NULL || length == NULL)
        return MAC_TX_INVALID;
    if (tx->phase == MAC_TX_IDLE)
        return MAC_TX_STATE;
    if (capacity < tx->length)
        return MAC_TX_SPACE;
    memcpy(body, tx->frame, tx->length);
    *length = tx->length;
    return MAC_TX_OK;
}

static void fault(mac_tx_t * volatile tx, uint8_t outcome)
{
    if (tx->phase == MAC_TX_RADIO)
        tx->uncertain = 1;
    tx->phase = MAC_TX_FAULT;
    tx->outcome = outcome;
}

static void spacing(mac_tx_t * volatile tx, uint32_t end)
{
    /* MPDU includes the two FCS octets absent from our body. */
    tx->ready_at = end + (tx->length <= 16u ? 12u : 40u);
}

static uint8_t stop(mac_tx_t * volatile tx, uint8_t outcome, uint32_t now)
{
    tx->outcome = outcome;
    if (tx->phase == MAC_TX_DRAW || tx->phase == MAC_TX_DRAW_WAIT) {
        tx->phase = MAC_TX_DONE;
        return MAC_TX_ACTION_NONE;
    }
    if (tx->phase == MAC_TX_RADIO)
        tx->uncertain = 1;
    if (tx->phase == MAC_TX_ACK_WAIT && tx->ack_requested
            && outcome != MAC_TX_ACKED && outcome != MAC_TX_NO_ACK)
        spacing(tx, tx->tx_end + MAC_TX_ACK_SYMBOLS);
    tx->phase = MAC_TX_STOPPING;
    tx->stop_at = now + MAC_TX_STOP_SYMBOLS;
    tx->stop_steps = MAC_TX_STOP_STEPS;
    return MAC_TX_ACTION_QUIESCE;
}

static uint8_t no_ack(mac_tx_t * volatile tx, uint32_t now)
{
    /* Deliberately wait the full original ACK window before any next TX,
     * including wrong-DSN failure, so a late ACK cannot overlap that retry.
     */
    spacing(tx, tx->tx_end + MAC_TX_ACK_SYMBOLS);
    tx->retry_pending = tx->retries < MAC_TX_MAX_RETRIES;
    return stop(tx, MAC_TX_NO_ACK, now);
}

mac_tx_result_t mac_tx_step(mac_tx_t * volatile tx, uint32_t now,
                            const mac_tx_event_t * volatile event,
                            mac_tx_action_t * volatile action)
{
    uint8_t emitted = MAC_TX_ACTION_NONE;
    uint8_t kind = 0;
    uint8_t phase;
    uint8_t normalized[3];
    mac_frame_info_t decoded;

    if (tx == NULL || action == NULL
            || (event != NULL && (event->kind < MAC_TX_EVENT_RANDOM
                || event->kind > MAC_TX_EVENT_CANCEL
                || (event->kind == MAC_TX_EVENT_ACK && event->length != 0u
                    && event->bytes == NULL))))
        return MAC_TX_INVALID;
    phase = tx->phase;
    if (phase == MAC_TX_IDLE)
        return MAC_TX_STATE;
    if (phase == MAC_TX_DONE || phase == MAC_TX_FAULT)
        goto publish;
    if ((uint32_t)(now - tx->last) >= MAC_TX_HALF) {
        fault(tx, MAC_TX_CLOCK_ERROR);
        goto publish;
    }
    if (event != NULL && event->generation == tx->generation
            && event->retry == tx->retries && event->nb == tx->nb
            && (uint32_t)(event->stamp - tx->last) < MAC_TX_HALF
            && (uint32_t)(now - event->stamp) < MAC_TX_HALF)
        kind = event->kind;
    if (reached(tx->last, tx->ready_at) || reached(now, tx->ready_at))
        tx->ready_at = now;
    tx->last = now;
    if (phase == MAC_TX_STOPPING) {
        if (tx->retry_pending && (reached(now, tx->deadline)
                    || tx->steps == 0u || kind == MAC_TX_EVENT_CANCEL)) {
            tx->retry_pending = 0;
            tx->outcome = kind == MAC_TX_EVENT_CANCEL ? MAC_TX_CANCELLED
                          : tx->steps == 0u ? MAC_TX_WORK_LIMIT : MAC_TX_LIFETIME;
        }
        if (reached(now, tx->stop_at) || tx->stop_steps == 0u)
            fault(tx, MAC_TX_STOP_FAILED);
        else {
            tx->stop_steps--;
            if (kind == MAC_TX_EVENT_FAILURE)
                fault(tx, MAC_TX_ADAPTER_ERROR);
            else if (kind == MAC_TX_EVENT_QUIESCED) {
                if (tx->uncertain)
                    spacing(tx, event->stamp + (tx->ack_requested ? MAC_TX_ACK_SYMBOLS : 0u));
                if (tx->retry_pending) {
                    tx->retry_pending = 0;
                    tx->retries++;
                    tx->nb = 0;
                    tx->be = MAC_TX_MIN_BE;
                    tx->outcome = MAC_TX_OUTCOME_NONE;
                    tx->phase = MAC_TX_DRAW;
                } else
                    tx->phase = MAC_TX_DONE;
            }
        }
        goto publish;
    }
    if (kind == MAC_TX_EVENT_FAILURE) {
        fault(tx, MAC_TX_ADAPTER_ERROR);
        goto publish;
    }
    if (reached(now, tx->deadline) || tx->steps == 0u
            || kind == MAC_TX_EVENT_CANCEL) {
        emitted = stop(tx, kind == MAC_TX_EVENT_CANCEL ? MAC_TX_CANCELLED
                       : tx->steps == 0u ? MAC_TX_WORK_LIMIT : MAC_TX_LIFETIME, now);
        goto publish;
    }
    tx->steps--;
    if (phase == MAC_TX_DRAW) {
        tx->phase = MAC_TX_DRAW_WAIT;
        emitted = MAC_TX_ACTION_RANDOM;
    } else if (phase == MAC_TX_DRAW_WAIT && kind == MAC_TX_EVENT_RANDOM) {
        tx->at = reached(now, tx->ready_at) ? now : tx->ready_at;
        tx->at += (uint32_t)(event->value & ((1u << tx->be) - 1u)) * MAC_TX_BACKOFF_SYMBOLS;
        if (reached(tx->at, tx->deadline)) {
            tx->phase = MAC_TX_DONE;
            tx->outcome = MAC_TX_LIFETIME;
        } else {
            tx->phase = MAC_TX_RADIO;
            emitted = MAC_TX_ACTION_ATTEMPT;
        }
    } else if (phase == MAC_TX_RADIO) {
        if (kind == MAC_TX_EVENT_BUSY || kind == MAC_TX_EVENT_SENT) {
            if (!reached(event->stamp, tx->at + (kind == MAC_TX_EVENT_BUSY
                         ? 8u : (uint32_t)(24u + 2u * tx->length)))) {
                fault(tx, MAC_TX_ADAPTER_ERROR);
                goto publish;
            }
            if (kind == MAC_TX_EVENT_BUSY) {
                /* BUSY confirms no TX, no outstanding buffer use, radio idle. */
                tx->nb++;
                if (tx->be < MAC_TX_MAX_BE)
                    tx->be++;
                if (tx->nb > MAC_TX_MAX_BACKOFFS) {
                    tx->phase = MAC_TX_DONE;
                    tx->outcome = MAC_TX_CHANNEL_ACCESS;
                } else
                    tx->phase = MAC_TX_DRAW;
            } else {
                tx->transmissions++;
                tx->tx_end = event->stamp;
                tx->phase = MAC_TX_ACK_WAIT;
                if (!tx->ack_requested) {
                    spacing(tx, event->stamp);
                    emitted = stop(tx, MAC_TX_UNACKNOWLEDGED, now);
                }
            }
        }
    } else if (phase == MAC_TX_ACK_WAIT) {
        if (kind == MAC_TX_EVENT_ACK && event->length == 3u
                && (event->bytes[0] & 7u) == MAC_FRAME_ACK
                && (uint32_t)(event->stamp - tx->tx_end) > 0u
                && (uint32_t)(event->stamp - tx->tx_end) <= MAC_TX_ACK_SYMBOLS) {
            /* 2006 7.2.2.3.1: other ACK FCF subfields ignored on RX.
             * Existing codec remains strict. Decode a bounded canonical copy.
             */
            normalized[0] = event->bytes[0] & (MAC_FLAG_PENDING | 7u);
            normalized[1] = 0;
            normalized[2] = event->bytes[2];
            if (mac_frame_decode(normalized, sizeof(normalized), &decoded) == MAC_CODEC_OK) {
                if (decoded.header.sequence == tx->frame[2]) {
                    tx->pending = (decoded.header.flags & MAC_FLAG_PENDING) != 0u;
                    spacing(tx, event->stamp);
                    emitted = stop(tx, MAC_TX_ACKED, now);
                } else
                    emitted = no_ack(tx, now);
            }
        }
        if (tx->phase == MAC_TX_ACK_WAIT && reached(now, tx->tx_end + MAC_TX_ACK_SYMBOLS))
            emitted = no_ack(tx, now);
    }
publish:
    action->kind = emitted;
    action->generation = tx->generation;
    action->retry = tx->retries;
    action->nb = tx->nb;
    action->at = emitted == MAC_TX_ACTION_QUIESCE ? now : tx->at;
    action->until = tx->phase == MAC_TX_STOPPING ? tx->stop_at : tx->deadline;
    action->length = tx->length;
    action->ack_requested = tx->ack_requested;
    action->phase = tx->phase;
    action->outcome = tx->outcome;
    action->transmissions = tx->transmissions;
    action->uncertain = tx->uncertain;
    action->pending = tx->pending;
    return MAC_TX_OK;
}

mac_tx_result_t mac_tx_release(mac_tx_t *tx)
{
    if (tx == NULL)
        return MAC_TX_INVALID;
    if (tx->phase != MAC_TX_DONE)
        return MAC_TX_STATE;
    tx->phase = MAC_TX_IDLE;
    return MAC_TX_OK;
}
