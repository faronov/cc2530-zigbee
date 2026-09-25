/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "mac_tx.h"

#include <stddef.h>
#include <string.h>

#if defined(__SDCC)
#define TX_RAM __xdata
#else
#define TX_RAM
#endif

#define CONTROL_FIELDS(X) \
    X(uint32_t, last) X(uint32_t, deadline) X(uint32_t, at) \
    X(uint32_t, tx_end) X(uint32_t, ready_at) X(uint32_t, generation) \
    X(uint32_t, stop_at) X(uint16_t, steps) \
    X(uint8_t, phase) X(uint8_t, next_dsn) X(uint8_t, length) \
    X(uint8_t, ack_requested) X(uint8_t, nb) X(uint8_t, be) \
    X(uint8_t, retries) X(uint8_t, outcome) X(uint8_t, transmissions) \
    X(uint8_t, uncertain) X(uint8_t, pending) X(uint8_t, retry_pending) \
    X(uint8_t, stop_steps)
#define CONTROL_MEMBER(type, name) type name;
typedef struct {
    CONTROL_FIELDS(CONTROL_MEMBER)
} control_t;
#undef CONTROL_MEMBER
#define CONTROL_LAYOUT(type, name) \
    typedef char control_layout_##name[ \
        offsetof(mac_tx_t, name) == offsetof(mac_tx_t, last) + offsetof(control_t, name) \
        && sizeof(((mac_tx_t *)0)->name) == sizeof(((control_t *)0)->name) ? 1 : -1];
CONTROL_FIELDS(CONTROL_LAYOUT)
#undef CONTROL_LAYOUT
#undef CONTROL_FIELDS
typedef char control_layout_tail[
    sizeof(mac_tx_t) - offsetof(mac_tx_t, last) == sizeof(control_t) ? 1 : -1];
typedef char control_layout_frame[
    offsetof(mac_tx_t, frame) == 0 && sizeof(((mac_tx_t *)0)->frame) == MAC_FRAME_MAX_BODY
    && offsetof(mac_tx_t, last) >= MAC_FRAME_MAX_BODY ? 1 : -1];
#define CONTROL_BYTES (offsetof(control_t, stop_steps) + sizeof(uint8_t))

/* Serialized foreground calls stage only control, never a second frame.
 * Byte copies preserve inactive members; frame alignment and tail padding in
 * the public object are untouched, including the native compiler's padding.
 */
static TX_RAM control_t control;
static TX_RAM volatile struct {
    uint32_t now, lifetime;
    uint16_t length, limit;
} input;

static void load_control(const mac_tx_t *tx)
{
    memcpy(&control, (const unsigned char *)tx + offsetof(mac_tx_t, last), CONTROL_BYTES);
}

static void save_control(mac_tx_t *tx)
{
    memcpy((unsigned char *)tx + offsetof(mac_tx_t, last), &control, CONTROL_BYTES);
}

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
    volatile uint8_t command;

    input.now = now;
    input.lifetime = lifetime;
    input.length = length;
    input.limit = work_limit;
    if (tx == NULL || body == NULL || input.lifetime == 0u
            || input.lifetime >= MAC_TX_HALF || input.limit == 0u)
        return MAC_TX_INVALID;
    load_control(tx);
    if (control.phase != MAC_TX_IDLE)
        return MAC_TX_FULL;
    if ((uint32_t)(input.now - control.last) >= MAC_TX_HALF)
        return MAC_TX_INVALID;
    if (control.generation == UINT32_MAX)
        return MAC_TX_GENERATION_EXHAUSTED;
    if (mac_frame_decode(body, input.length, &decoded) != MAC_CODEC_OK)
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
                         && (uint8_t)(body[(uint8_t)(input.length - 1u)] & 0xfbu) == 0x88u)
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
    memcpy(tx->frame, body, input.length);
    tx->frame[2] = control.next_dsn++;
    control.length = (uint8_t)input.length;
    control.ack_requested = (decoded.header.flags & MAC_FLAG_ACK_REQUEST) != 0u;
    control.generation++;
    if (reached(control.last, control.ready_at) || reached(input.now, control.ready_at))
        control.ready_at = input.now;
    control.last = input.now;
    control.deadline = input.now + input.lifetime;
    control.steps = input.limit;
    control.phase = MAC_TX_DRAW;
    control.nb = 0;
    control.be = MAC_TX_MIN_BE;
    /* Six contiguous uint8_t fields, retries through retry_pending; NONE is 0.
     * Use the enclosing object's byte representation. Preserve stop_steps and
     * all other fields, including inactive timestamps and the copied tail.
     */
    memset((unsigned char *)&control + offsetof(control_t, retries), 0,
           offsetof(control_t, stop_steps) - offsetof(control_t, retries));
    save_control(tx);
    return MAC_TX_OK;
}

mac_tx_result_t mac_tx_copy(const mac_tx_t * volatile tx,
                            uint8_t * volatile body, uint16_t capacity,
                            uint8_t * volatile length)
{
    TX_RAM volatile uint8_t size;

    if (tx == NULL || body == NULL || length == NULL)
        return MAC_TX_INVALID;
    if (tx->phase == MAC_TX_IDLE)
        return MAC_TX_STATE;
    size = tx->length;
    if (capacity < size)
        return MAC_TX_SPACE;
    memcpy(body, tx->frame, size);
    *length = size;
    return MAC_TX_OK;
}

static void fault(uint8_t outcome)
{
    if (control.phase == MAC_TX_RADIO)
        control.uncertain = 1;
    control.phase = MAC_TX_FAULT;
    control.outcome = outcome;
}

static void spacing(uint32_t end)
{
    /* MPDU includes the two FCS octets absent from our body. */
    control.ready_at = end + (control.length <= 16u ? 12u : 40u);
}

static uint8_t stop(uint8_t outcome)
{
    control.outcome = outcome;
    if (control.phase == MAC_TX_DRAW || control.phase == MAC_TX_DRAW_WAIT) {
        control.phase = MAC_TX_DONE;
        return MAC_TX_ACTION_NONE;
    }
    if (control.phase == MAC_TX_RADIO)
        control.uncertain = 1;
    if (control.phase == MAC_TX_ACK_WAIT && control.ack_requested
            && outcome != MAC_TX_ACKED && outcome != MAC_TX_NO_ACK)
        spacing(control.tx_end + MAC_TX_ACK_SYMBOLS);
    control.phase = MAC_TX_STOPPING;
    control.stop_at = input.now + MAC_TX_STOP_SYMBOLS;
    control.stop_steps = MAC_TX_STOP_STEPS;
    return MAC_TX_ACTION_QUIESCE;
}

static uint8_t no_ack(void)
{
    /* Deliberately wait the full original ACK window before any next TX,
     * including wrong-DSN failure, so a late ACK cannot overlap that retry.
     */
    spacing(control.tx_end + MAC_TX_ACK_SYMBOLS);
    control.retry_pending = control.retries < MAC_TX_MAX_RETRIES;
    return stop(MAC_TX_NO_ACK);
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

    input.now = now;
    if (tx == NULL || action == NULL
            || (event != NULL && (event->kind < MAC_TX_EVENT_RANDOM
                || event->kind > MAC_TX_EVENT_CANCEL
                || (event->kind == MAC_TX_EVENT_ACK && event->length != 0u
                    && event->bytes == NULL))))
        return MAC_TX_INVALID;
    load_control(tx);
    phase = control.phase;
    if (phase == MAC_TX_IDLE)
        return MAC_TX_STATE;
    if (phase == MAC_TX_DONE || phase == MAC_TX_FAULT)
        goto publish;
    if ((uint32_t)(input.now - control.last) >= MAC_TX_HALF) {
        fault(MAC_TX_CLOCK_ERROR);
        goto publish;
    }
    if (event != NULL && event->generation == control.generation
            && event->retry == control.retries && event->nb == control.nb
            && (uint32_t)(event->stamp - control.last) < MAC_TX_HALF
            && (uint32_t)(input.now - event->stamp) < MAC_TX_HALF)
        kind = event->kind;
    if (reached(control.last, control.ready_at) || reached(input.now, control.ready_at))
        control.ready_at = input.now;
    control.last = input.now;
    if (phase == MAC_TX_STOPPING) {
        if (control.retry_pending && (reached(input.now, control.deadline)
                    || control.steps == 0u || kind == MAC_TX_EVENT_CANCEL)) {
            control.retry_pending = 0;
            control.outcome = kind == MAC_TX_EVENT_CANCEL ? MAC_TX_CANCELLED
                          : control.steps == 0u ? MAC_TX_WORK_LIMIT : MAC_TX_LIFETIME;
        }
        if (reached(input.now, control.stop_at) || control.stop_steps == 0u)
            fault(MAC_TX_STOP_FAILED);
        else {
            control.stop_steps--;
            if (kind == MAC_TX_EVENT_FAILURE)
                fault(MAC_TX_ADAPTER_ERROR);
            else if (kind == MAC_TX_EVENT_QUIESCED) {
                if (control.uncertain)
                    spacing(event->stamp + (control.ack_requested ? MAC_TX_ACK_SYMBOLS : 0u));
                if (control.retry_pending) {
                    control.retry_pending = 0;
                    control.retries++;
                    control.nb = 0;
                    control.be = MAC_TX_MIN_BE;
                    control.outcome = MAC_TX_OUTCOME_NONE;
                    control.phase = MAC_TX_DRAW;
                } else
                    control.phase = MAC_TX_DONE;
            }
        }
        goto publish;
    }
    if (kind == MAC_TX_EVENT_FAILURE) {
        fault(MAC_TX_ADAPTER_ERROR);
        goto publish;
    }
    if (reached(input.now, control.deadline) || control.steps == 0u
            || kind == MAC_TX_EVENT_CANCEL) {
        emitted = stop(kind == MAC_TX_EVENT_CANCEL ? MAC_TX_CANCELLED
                       : control.steps == 0u ? MAC_TX_WORK_LIMIT : MAC_TX_LIFETIME);
        goto publish;
    }
    control.steps--;
    if (phase == MAC_TX_DRAW) {
        control.phase = MAC_TX_DRAW_WAIT;
        emitted = MAC_TX_ACTION_RANDOM;
    } else if (phase == MAC_TX_DRAW_WAIT && kind == MAC_TX_EVENT_RANDOM) {
        control.at = reached(input.now, control.ready_at) ? input.now : control.ready_at;
        control.at += (uint32_t)(event->value & ((1u << control.be) - 1u)) * MAC_TX_BACKOFF_SYMBOLS;
        if (reached(control.at, control.deadline)) {
            control.phase = MAC_TX_DONE;
            control.outcome = MAC_TX_LIFETIME;
        } else {
            control.phase = MAC_TX_RADIO;
            emitted = MAC_TX_ACTION_ATTEMPT;
        }
    } else if (phase == MAC_TX_RADIO) {
        if (kind == MAC_TX_EVENT_BUSY || kind == MAC_TX_EVENT_SENT) {
            if (!reached(event->stamp, control.at + (kind == MAC_TX_EVENT_BUSY
                         ? 8u : (uint32_t)(24u + 2u * control.length)))) {
                fault(MAC_TX_ADAPTER_ERROR);
                goto publish;
            }
            if (kind == MAC_TX_EVENT_BUSY) {
                /* BUSY confirms no TX, no outstanding buffer use, radio idle. */
                control.nb++;
                if (control.be < MAC_TX_MAX_BE)
                    control.be++;
                if (control.nb > MAC_TX_MAX_BACKOFFS) {
                    control.phase = MAC_TX_DONE;
                    control.outcome = MAC_TX_CHANNEL_ACCESS;
                } else
                    control.phase = MAC_TX_DRAW;
            } else {
                control.transmissions++;
                control.tx_end = event->stamp;
                control.phase = MAC_TX_ACK_WAIT;
                if (!control.ack_requested) {
                    spacing(event->stamp);
                    emitted = stop(MAC_TX_UNACKNOWLEDGED);
                }
            }
        }
    } else if (phase == MAC_TX_ACK_WAIT) {
        if (kind == MAC_TX_EVENT_ACK && event->length == 3u
                && (event->bytes[0] & 7u) == MAC_FRAME_ACK
                && (uint32_t)(event->stamp - control.tx_end) > 0u
                && (uint32_t)(event->stamp - control.tx_end) <= MAC_TX_ACK_SYMBOLS) {
            /* 2006 7.2.2.3.1: other ACK FCF subfields ignored on RX.
             * Existing codec remains strict. Decode a bounded canonical copy.
             */
            normalized[0] = event->bytes[0] & (MAC_FLAG_PENDING | 7u);
            normalized[1] = 0;
            normalized[2] = event->bytes[2];
            if (mac_frame_decode(normalized, sizeof(normalized), &decoded) == MAC_CODEC_OK) {
                if (decoded.header.sequence == tx->frame[2]) {
                    control.pending = (decoded.header.flags & MAC_FLAG_PENDING) != 0u;
                    spacing(event->stamp);
                    emitted = stop(MAC_TX_ACKED);
                } else
                    emitted = no_ack();
            }
        }
        if (control.phase == MAC_TX_ACK_WAIT && reached(input.now, control.tx_end + MAC_TX_ACK_SYMBOLS))
            emitted = no_ack();
    }
publish:
    save_control(tx);
    action->kind = emitted;
    action->generation = control.generation;
    action->retry = control.retries;
    action->nb = control.nb;
    action->at = emitted == MAC_TX_ACTION_QUIESCE ? input.now : control.at;
    action->until = control.phase == MAC_TX_STOPPING ? control.stop_at : control.deadline;
    action->length = control.length;
    action->ack_requested = control.ack_requested;
    action->phase = control.phase;
    action->outcome = control.outcome;
    action->transmissions = control.transmissions;
    action->uncertain = control.uncertain;
    action->pending = control.pending;
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

#if defined(CC2530_MAC_INTERVAL)
#include "mac_tx_interval.h"

static TX_RAM mac_epoch_stamp_t interval_end;
static TX_RAM mac_frame_info_t interval_decoded;
static TX_RAM uint8_t interval_ack[3];
static TX_RAM uint32_t interval_previous;

static uint8_t interval_ordered(const mac_epoch_stamp_t *first,
                                const mac_epoch_stamp_t *last)
{
    uint32_t symbols = last->symbols - first->symbols;
    return symbols == 0u ? last->fine >= first->fine : symbols < MAC_TX_HALF;
}

static uint32_t interval_ceil(const mac_epoch_stamp_t *point)
{
    return point->symbols + (point->fine != 0u);
}

static uint8_t interval_stop(uint8_t outcome)
{
    control.outcome = outcome;
    control.phase = MAC_TX_STOPPING;
    control.stop_at = input.now + MAC_TX_STOP_SYMBOLS;
    control.stop_steps = MAC_TX_STOP_STEPS;
    return MAC_TX_ACTION_QUIESCE;
}

static uint8_t interval_no_ack(const mac_tx_interval_t *tx)
{
    spacing(interval_ceil(&tx->tx_upper) + MAC_TX_ACK_SYMBOLS);
    control.retry_pending = control.retries < MAC_TX_MAX_RETRIES;
    return interval_stop(MAC_TX_NO_ACK);
}

static void interval_publish(const mac_tx_interval_t *tx,
                              mac_tx_interval_action_t *action, uint8_t emitted)
{
    action->control.kind = emitted;
    action->control.generation = control.generation;
    action->control.retry = control.retries;
    action->control.nb = control.nb;
    action->control.at = emitted == MAC_TX_ACTION_QUIESCE ? input.now : control.at;
    action->control.until = control.phase == MAC_TX_STOPPING ? control.stop_at : control.deadline;
    action->control.length = control.length;
    action->control.ack_requested = control.ack_requested;
    action->control.phase = control.phase;
    action->control.outcome = control.outcome;
    action->control.transmissions = control.transmissions;
    action->control.uncertain = control.uncertain;
    action->control.pending = control.pending;
    action->through = tx->tx_upper;
    action->through.symbols += MAC_TX_ACK_SYMBOLS;
}

mac_tx_result_t mac_tx_interval_init(mac_tx_interval_t *tx, uint8_t dsn, uint32_t now)
{
    mac_tx_result_t result;
    if (tx == NULL)
        return MAC_TX_INVALID;
    result = mac_tx_init(&tx->engine, dsn, now);
    memset(&tx->tx_lower, 0, sizeof(tx->tx_lower));
    memset(&tx->tx_upper, 0, sizeof(tx->tx_upper));
    return result;
}

mac_tx_result_t mac_tx_interval_submit(mac_tx_interval_t *tx,
    const uint8_t *body, uint16_t length, uint32_t now, uint32_t lifetime, uint16_t work)
{
    mac_tx_result_t result;
    if (tx == NULL)
        return MAC_TX_INVALID;
    result = mac_tx_submit(&tx->engine, body, length, now, lifetime, work);
    if (result == MAC_TX_OK) {
        memset(&tx->tx_lower, 0, sizeof(tx->tx_lower));
        memset(&tx->tx_upper, 0, sizeof(tx->tx_upper));
    }
    return result;
}

mac_tx_result_t mac_tx_interval_copy(const mac_tx_interval_t *tx,
    uint8_t *body, uint16_t capacity, uint8_t *length)
{
    return tx == NULL ? MAC_TX_INVALID : mac_tx_copy(&tx->engine, body, capacity, length);
}

mac_tx_result_t mac_tx_interval_release(mac_tx_interval_t *tx)
{
    return tx == NULL ? MAC_TX_INVALID : mac_tx_release(&tx->engine);
}

mac_tx_result_t mac_tx_interval_step(mac_tx_interval_t * volatile tx, uint32_t now,
    const mac_tx_interval_event_t * volatile event, mac_tx_interval_action_t * volatile action)
{
    uint8_t kind = 0;
    uint8_t emitted = MAC_TX_ACTION_NONE;
    mac_tx_result_t result;

    if (tx == NULL || action == NULL)
        return MAC_TX_INVALID;
    if (event != NULL) {
        kind = event->source.kind;
        if (kind < MAC_TX_EVENT_RANDOM || kind > MAC_TX_EVENT_RX_CLOSED
                || kind == MAC_TX_EVENT_SENT || kind == MAC_TX_EVENT_ACK
                || (kind == MAC_TX_EVENT_ACK_INTERVAL && event->source.length != 0u
                    && event->source.bytes == NULL))
            return MAC_TX_INVALID;
        if (kind >= MAC_TX_EVENT_SENT_INTERVAL) {
            if (event->upper.fine >= 512u
                    || !reached(event->source.stamp, interval_ceil(&event->upper)))
                return MAC_TX_INVALID;
            if (kind != MAC_TX_EVENT_RX_CLOSED
                    && (event->lower.fine >= 512u
                        || !interval_ordered(&event->lower, &event->upper)))
                return MAC_TX_INVALID;
        }
    }
    if (tx->engine.phase != MAC_TX_ACK_WAIT) {
        interval_previous = tx->engine.last;
        result = mac_tx_step(&tx->engine, now,
            event != NULL && kind < MAC_TX_EVENT_SENT_INTERVAL ? &event->source : NULL,
            &action->control);
        if (result != MAC_TX_OK)
            return result;
        action->through = tx->tx_upper;
        action->through.symbols += MAC_TX_ACK_SYMBOLS;
        if (tx->engine.phase != MAC_TX_RADIO || kind != MAC_TX_EVENT_SENT_INTERVAL
                || event->source.generation != tx->engine.generation
                || event->source.retry != tx->engine.retries || event->source.nb != tx->engine.nb
                || !reached(event->source.stamp, interval_previous)
                || !reached(now, event->source.stamp))
            return MAC_TX_OK;
        load_control(&tx->engine);
        interval_end.symbols = control.at + 24u + 2u * control.length;
        interval_end.fine = 0;
        if (!interval_ordered(&interval_end, &event->lower)) {
            fault(MAC_TX_ADAPTER_ERROR);
            goto interval_done;
        }
        tx->tx_lower = event->lower;
        tx->tx_upper = event->upper;
        control.transmissions++;
        control.phase = MAC_TX_ACK_WAIT;
        if (control.ack_requested)
            emitted = MAC_TX_ACTION_COLLECT;
        else {
            spacing(interval_ceil(&tx->tx_upper));
            emitted = interval_stop(MAC_TX_UNACKNOWLEDGED);
        }
        goto interval_done;
    }

    input.now = now;
    load_control(&tx->engine);
    if ((uint32_t)(now - control.last) >= MAC_TX_HALF) {
        fault(MAC_TX_CLOCK_ERROR);
        goto interval_done;
    }
    if (event == NULL || event->source.generation != control.generation
            || event->source.retry != control.retries || event->source.nb != control.nb
            || !reached(event->source.stamp, control.last) || !reached(now, event->source.stamp))
        kind = 0;
    if (reached(control.last, control.ready_at) || reached(now, control.ready_at))
        control.ready_at = now;
    control.last = now;
    if (kind == MAC_TX_EVENT_FAILURE) {
        fault(MAC_TX_ADAPTER_ERROR);
        goto interval_done;
    }
    if (reached(now, control.deadline) || control.steps == 0u || kind == MAC_TX_EVENT_CANCEL) {
        spacing(interval_ceil(&tx->tx_upper) + MAC_TX_ACK_SYMBOLS);
        emitted = interval_stop(kind == MAC_TX_EVENT_CANCEL ? MAC_TX_CANCELLED
                                : control.steps == 0u ? MAC_TX_WORK_LIMIT : MAC_TX_LIFETIME);
        goto interval_done;
    }
    control.steps--;
    if (kind == MAC_TX_EVENT_ACK_INTERVAL && event->source.length == 3u
            && (event->source.bytes[0] & 7u) == MAC_FRAME_ACK) {
        if (!interval_ordered(&tx->tx_lower, &event->lower)
                || interval_ordered(&event->upper, &tx->tx_lower)) {
            fault(MAC_TX_ADAPTER_ERROR);
            goto interval_done;
        }
        interval_ack[0] = event->source.bytes[0] & (MAC_FLAG_PENDING | 7u);
        interval_ack[1] = 0;
        interval_ack[2] = event->source.bytes[2];
        if (mac_frame_decode(interval_ack, sizeof(interval_ack), &interval_decoded) != MAC_CODEC_OK)
            goto interval_done;
        interval_end = tx->tx_lower;
        interval_end.symbols += MAC_TX_ACK_SYMBOLS;
        if (interval_ordered(&event->upper, &interval_end)) {
            if (interval_decoded.header.sequence == tx->engine.frame[2]) {
                control.pending = (interval_decoded.header.flags & MAC_FLAG_PENDING) != 0u;
                spacing(interval_ceil(&event->upper));
                emitted = interval_stop(MAC_TX_ACKED);
            } else
                emitted = interval_no_ack(tx);
        } else {
            interval_end = tx->tx_upper;
            interval_end.symbols += MAC_TX_ACK_SYMBOLS;
            if (interval_ordered(&event->lower, &interval_end)) {
                spacing(interval_ceil(&tx->tx_upper) + MAC_TX_ACK_SYMBOLS);
                emitted = interval_stop(MAC_TX_TIMING_UNCERTAIN);
            }
        }
    } else if (kind == MAC_TX_EVENT_RX_CLOSED) {
        interval_end = tx->tx_upper;
        interval_end.symbols += MAC_TX_ACK_SYMBOLS;
        if (!interval_ordered(&interval_end, &event->upper))
            fault(MAC_TX_ADAPTER_ERROR);
        else
            emitted = interval_no_ack(tx);
    }
interval_done:
    save_control(&tx->engine);
    interval_publish(tx, action, emitted);
    return MAC_TX_OK;
}
#endif

#if defined(CC2530_MAC_OBSERVED)
#include "mac_tx_observed.h"
static TX_RAM uint32_t observed_previous;
static TX_RAM uint8_t observed_phase;
static const mac_epoch_stamp_t * volatile TX_RAM observed_lower;
static const mac_epoch_stamp_t * volatile TX_RAM observed_upper;

mac_tx_result_t mac_tx_observed_step(mac_tx_interval_t * volatile tx, uint32_t now,
    const mac_tx_interval_event_t * volatile event,
    mac_tx_interval_action_t * volatile action)
{
    mac_tx_result_t result;
    if (!event || (event->source.kind != MAC_TX_EVENT_BUSY_INTERVAL &&
                   event->source.kind != MAC_TX_EVENT_RETIRED))
        return mac_tx_interval_step(tx, now, event, action);
    if (!tx || !action) return MAC_TX_INVALID;
    observed_lower = &event->lower; observed_upper = &event->upper;
    if (event->upper.fine >= 512u ||
        !reached(event->source.stamp, interval_ceil(observed_upper)) ||
        (event->source.kind == MAC_TX_EVENT_BUSY_INTERVAL &&
         (event->lower.fine >= 512u || !interval_ordered(observed_lower, observed_upper))))
        return MAC_TX_INVALID;
    observed_previous = tx->engine.last;
    observed_phase = tx->engine.phase;
    result = mac_tx_interval_step(tx, now, NULL, action);
    if (result != MAC_TX_OK) return result;
    if (event->source.generation != tx->engine.generation ||
        event->source.retry != tx->engine.retries || event->source.nb != tx->engine.nb ||
        !reached(event->source.stamp, observed_previous) || !reached(now, event->source.stamp))
        return MAC_TX_OK;
    if (event->source.kind == MAC_TX_EVENT_BUSY_INTERVAL ?
        (observed_phase != MAC_TX_RADIO || tx->engine.phase != MAC_TX_RADIO) :
        (observed_phase != MAC_TX_STOPPING || tx->engine.phase != MAC_TX_STOPPING))
        return MAC_TX_OK;
    load_control(&tx->engine);
    if (event->source.kind == MAC_TX_EVENT_BUSY_INTERVAL) {
        interval_end.symbols = control.at + 8u; interval_end.fine = 0;
        if (!interval_ordered(&interval_end, observed_lower))
            fault(MAC_TX_ADAPTER_ERROR);
        else {
            control.nb++;
            if (control.be < MAC_TX_MAX_BE) control.be++;
            if (control.nb > MAC_TX_MAX_BACKOFFS) {
                control.phase = MAC_TX_DONE; control.outcome = MAC_TX_CHANNEL_ACCESS;
            } else control.phase = MAC_TX_DRAW;
        }
    } else {
        if (control.uncertain)
            spacing(interval_ceil(observed_upper) +
                    (control.ack_requested ? MAC_TX_ACK_SYMBOLS : 0u));
        if (control.retry_pending) {
            control.retry_pending = 0; control.retries++; control.nb = 0;
            control.be = MAC_TX_MIN_BE; control.outcome = MAC_TX_OUTCOME_NONE;
            control.phase = MAC_TX_DRAW;
        } else control.phase = MAC_TX_DONE;
    }
    save_control(&tx->engine);
    interval_publish(tx, action, MAC_TX_ACTION_NONE);
    return MAC_TX_OK;
}
#endif
