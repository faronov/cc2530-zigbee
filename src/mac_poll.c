/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "mac_poll.h"
#include <stddef.h>
#include <string.h>

static mac_poll_control_t MAC_POLL_RAM control;
static mac_poll_event_t MAC_POLL_RAM input;
static mac_poll_action_t MAC_POLL_RAM output;
static mac_poll_record_t MAC_POLL_RAM *receipt;
static uint32_t MAC_POLL_RAM step_time;

typedef char control_first[(offsetof(mac_poll_t, control) == 0) ? 1 : -1];
typedef char request_first[(offsetof(mac_poll_control_t, request) == 0) ? 1 : -1];
typedef char record_boundary[(offsetof(mac_poll_t, record) == sizeof(mac_poll_control_t)) ? 1 : -1];
#ifdef __SDCC
typedef char control_size[(sizeof(mac_poll_control_t) == 123) ? 1 : -1];
typedef char record_size[(sizeof(mac_poll_record_t) == 143) ? 1 : -1];
typedef char context_size[(sizeof(mac_poll_t) == 266) ? 1 : -1];
#endif

static uint8_t reached(uint32_t now, uint32_t at)
{
    return (uint32_t)(now - at) < MAC_TX_HALF;
}

static uint8_t address(uint8_t mode, const uint8_t *bytes)
{
    uint8_t i;
    if (mode == MAC_ADDRESS_EXTENDED)
        return 1;
    if (mode != MAC_ADDRESS_SHORT || (bytes[1] == 255 && bytes[0] >= 254))
        return 0;
    for (i = 2; i < 8; i++)
        if (bytes[i])
            return 0;
    return 1;
}

static void drain(uint8_t reason)
{
    if (!control.reason)
        control.reason = reason;
    if (reason == MAC_POLL_ADAPTER_ERROR || reason == MAC_POLL_ORDER_ERROR
            || reason == MAC_POLL_TX_ERROR)
        control.uncertain = 1;
    if (control.phase != MAC_POLL_DRAIN) {
        control.phase = MAC_POLL_DRAIN;
        control.stop_at = step_time + MAC_POLL_STOP_SYMBOLS;
        control.stop_steps = MAC_POLL_STOP_STEPS;
    }
    if (reason && !control.ready) {
        control.timeout_pending = 0;
        receipt->stamp = step_time;
        control.ready = 1;
    }
}

static void fault(uint8_t reason)
{
    if (control.reason && !control.cleanup_error)
        control.cleanup_error = reason;
    drain(reason);
    control.uncertain = 1;
    control.phase = MAC_POLL_FAULT;
}

static void decide(uint8_t protocol, uint8_t cause, uint32_t stamp)
{
    receipt->protocol = protocol;
    receipt->cause = cause;
    receipt->stamp = stamp;
    control.ready = 1;
    drain(0);
}

static uint8_t accept_ack(mac_tx_t MAC_POLL_RAM * volatile tx)
{
    if (!(control.tx_phase == MAC_TX_ACK_WAIT && tx->phase == MAC_TX_STOPPING
        && input.source.kind == MAC_TX_EVENT_ACK && input.crc_valid
        && input.source.generation == control.tx_generation
        && input.source.retry == control.tx_retry && input.source.nb == control.tx_nb
        && input.source.length == 3 && (input.source.bytes[0] & 7u) == MAC_FRAME_ACK
        && input.source.bytes[2] == tx->frame[2]
        && ((input.source.bytes[0] & MAC_FLAG_PENDING) != 0u) == tx->pending
        && reached(input.source.stamp, control.tx_mark)
        && reached(input.stamp, input.source.stamp)
        && (uint32_t)(input.source.stamp - tx->tx_end) != 0
        && (uint32_t)(input.source.stamp - tx->tx_end) <= MAC_TX_ACK_SYMBOLS))
        return 0;
    control.ack_seen = 1;
    control.ack_end = input.source.stamp;
    control.receive_end = control.ack_end + control.request.frame_wait;
    if (control.phase != MAC_POLL_DRAIN) {
        if (tx->pending)
            control.phase = MAC_POLL_RECEIVE;
        else
            decide(MAC_POLL_NO_DATA, MAC_POLL_PENDING_ZERO, control.ack_end);
    }
    return 1;
}

mac_poll_result_t mac_poll_init(mac_poll_t MAC_POLL_RAM * volatile p)
{
    if (p == NULL)
        return MAC_POLL_INVALID;
    memset(p, 0, sizeof(*p));
    p->control.version = MAC_POLL_VERSION;
    return MAC_POLL_OK;
}

mac_poll_result_t mac_poll_start(mac_poll_t MAC_POLL_RAM * volatile p,
    mac_tx_t MAC_POLL_RAM * volatile tx,
    const mac_poll_request_t MAC_POLL_RAM * volatile request, uint32_t volatile now)
{
    mac_header_t header;
    uint8_t command = MAC_COMMAND_DATA_REQUEST;
    volatile uint32_t generation;
    if (p == NULL || tx == NULL || request == NULL)
        return MAC_POLL_INVALID;
    memcpy(&control, &p->control, sizeof(control));
    memcpy(&control.request, request, sizeof(control.request));
    if (!control.request.epoch
            || !control.request.frame_wait || control.request.frame_wait > MAC_POLL_MAX_TIME
            || !control.request.lifetime || control.request.lifetime > MAC_POLL_MAX_TIME
            || !control.request.work || control.request.work > MAC_POLL_MAX_WORK
            || control.request.pan == 0xffffu || control.request.channel < 11 || control.request.channel > 26
            || !address(control.request.local_mode, control.request.local)
            || !address(control.request.coordinator_mode, control.request.coordinator))
        return MAC_POLL_INVALID;
    if (control.version != MAC_POLL_VERSION || control.phase != MAC_POLL_IDLE || tx->phase != MAC_TX_IDLE)
        return MAC_POLL_STATE;
    if (!reached(now, tx->last) || (control.generation && !reached(now, control.last)))
        return MAC_POLL_INVALID;
    if (control.generation == UINT32_MAX || tx->generation == UINT32_MAX)
        return MAC_POLL_LIMIT;
    memset(&header, 0, sizeof(header));
    header.type = MAC_FRAME_COMMAND;
    header.flags = MAC_FLAG_ACK_REQUEST | MAC_FLAG_PAN_COMPRESSION;
    header.destination_mode = control.request.coordinator_mode;
    header.source_mode = control.request.local_mode;
    header.source_pan = header.destination_pan = control.request.pan;
    memcpy(header.destination, control.request.coordinator, 8);
    memcpy(header.source, control.request.local, 8);
    generation = control.generation + 1;
    /* Preserve the copied request and clear the remaining object representation.
     * There is no mirror cast or assumed host pointer/padding layout. */
    memset((uint8_t *)&control + sizeof(control.request), 0,
           sizeof(control) - sizeof(control.request));
    if (mac_frame_encode(&header, &command, 1, control.outgoing,
                         sizeof(control.outgoing), &control.outgoing_length) != MAC_CODEC_OK)
        return MAC_POLL_INVALID;
    control.version = MAC_POLL_VERSION;
    control.phase = MAC_POLL_ARM;
    control.generation = generation;
    control.owner = tx;
    control.tx_generation = tx->generation;
    control.last = now;
    control.deadline = control.last + control.request.lifetime;
    control.steps = control.request.work;
    receipt = &p->record;
    memset(receipt, 0, sizeof(*receipt));
    receipt->epoch = control.request.epoch;
    receipt->generation = generation;
    memcpy(&p->control, &control, sizeof(control));
    return MAC_POLL_OK;
}

mac_poll_result_t mac_poll_step(mac_poll_t MAC_POLL_RAM * volatile p,
    mac_tx_t MAC_POLL_RAM * volatile tx, uint32_t volatile now,
    const mac_poll_event_t MAC_POLL_RAM * volatile event,
    mac_poll_action_t MAC_POLL_RAM * volatile action)
{
    mac_frame_info_t frame;
    volatile uint8_t kind, observation, status, unbound;
    volatile uint32_t remaining;
    if (p == NULL || tx == NULL || action == NULL)
        return MAC_POLL_INVALID;
    if (event != NULL) {
        memcpy(&input, event, sizeof(input));
        if (input.kind < MAC_POLL_PREPARED || input.kind > MAC_POLL_FAILURE
                || input.crc_valid > 1
                || (input.kind == MAC_POLL_FRAME && (input.body == NULL || !input.serial
                    || input.channel < 11 || input.channel > 26))
                || (input.kind == MAC_POLL_TX && input.source.kind > MAC_TX_EVENT_CANCEL)
                || (input.kind == MAC_POLL_TX && input.source.kind == MAC_TX_EVENT_ACK
                    && input.source.length && input.source.bytes == NULL))
            return MAC_POLL_INVALID;
    }
    memcpy(&control, &p->control, sizeof(control));
    if (control.version != MAC_POLL_VERSION || control.phase == MAC_POLL_IDLE
            || control.phase > MAC_POLL_FAULT || control.owner != tx)
        return MAC_POLL_STATE;
    receipt = &p->record;
    step_time = now;
    memset(&output, 0, sizeof(output));
    kind = 0;
    observation = MAC_POLL_OBS_NONE;
    if (control.phase >= MAC_POLL_DONE)
        goto publish;
    if (!reached(now, control.last)) {
        fault(MAC_POLL_CLOCK_ERROR);
        goto publish;
    }
    if (event != NULL) {
        observation = MAC_POLL_OBS_STALE;
        if (input.epoch == control.request.epoch && input.generation == control.generation) {
            if (input.kind == MAC_POLL_FRAME && input.serial <= control.rx_serial)
                observation = MAC_POLL_OBS_DUPLICATE;
            else if (reached(input.stamp, control.last) && reached(now, input.stamp)) {
                kind = input.kind;
                observation = MAC_POLL_OBS_NONE;
            } else if (input.kind == MAC_POLL_FRAME)
                drain(MAC_POLL_ORDER_ERROR);
        }
    }
    control.last = now;
    if (tx->generation != control.tx_generation || (!control.submitted && tx->phase != MAC_TX_IDLE)) {
        fault(MAC_POLL_TX_ERROR);
        goto publish;
    }
    if (control.phase == MAC_POLL_DRAIN) {
        if (reached(now, control.stop_at) || !control.stop_steps) {
            fault(MAC_POLL_CLEANUP_FAILED);
            goto publish;
        }
        control.stop_steps--;
    } else if (kind == MAC_POLL_CANCEL || kind == MAC_POLL_FAILURE
            || reached(now, control.deadline) || !control.steps)
        drain(kind == MAC_POLL_CANCEL ? MAC_POLL_CANCELLED
              : kind == MAC_POLL_FAILURE ? MAC_POLL_ADAPTER_ERROR
              : !control.steps ? MAC_POLL_WORK_LIMIT : MAC_POLL_LIFETIME);
    else
        control.steps--;
    if (kind == MAC_POLL_FAILURE)
        drain(MAC_POLL_ADAPTER_ERROR);

    if (kind == MAC_POLL_PREPARED && control.phase == MAC_POLL_ARM && control.token
            && input.token == control.token) {
        control.prepared = 1;
        remaining = control.deadline - now;
        if (mac_tx_submit(tx, control.outgoing, control.outgoing_length, now,
                         remaining, MAC_POLL_TX_STEPS) != MAC_TX_OK)
            drain(MAC_POLL_TX_ERROR);
        else {
            control.submitted = 1;
            control.tx_generation = tx->generation;
            control.phase = MAC_POLL_REQUEST;
        }
    }
    if (kind == MAC_POLL_TX && control.tx_issued && input.token == control.tx_token) {
        control.tx_issued = 0;
        control.tx_outcome = tx->outcome;
        if (input.tx_result != MAC_TX_OK || tx->phase == MAC_TX_IDLE
                || tx->last != input.stamp || tx->phase == MAC_TX_FAULT) {
            drain(MAC_POLL_TX_ERROR);
        } else if (!control.ack_seen && tx->outcome == MAC_TX_ACKED) {
            /* Witness the ORIGINAL event used by the real granted MAC call.
             * Parsing/ignored ACK bits belong to mac_tx, not another decoder.
             */
            if (!accept_ack(tx))
                drain(MAC_POLL_TX_ERROR);
        } else if (tx->phase == MAC_TX_DONE && !control.ack_seen && control.phase != MAC_POLL_DRAIN) {
            if (tx->outcome == MAC_TX_NO_ACK || tx->outcome == MAC_TX_CHANNEL_ACCESS)
                decide(tx->outcome == MAC_TX_NO_ACK ? MAC_POLL_NO_ACK : MAC_POLL_CHANNEL_ACCESS,
                       MAC_POLL_TX_RESULT, input.stamp);
            else
                drain(MAC_POLL_TX_ABORT);
        }
    }
    if (kind == MAC_POLL_FRAME) {
        control.rx_serial = input.serial;
        observation = MAC_POLL_OBS_LATE;
        if (control.phase == MAC_POLL_DRAIN && control.timeout_pending
                && reached(input.stamp, control.ack_end + 1u)
                && reached(control.receive_end, input.stamp))
            drain(MAC_POLL_ORDER_ERROR); /* Timeout poll preceded completed frame. */
        if (control.phase == MAC_POLL_RECEIVE && reached(input.stamp, control.ack_end + 1u)
                && reached(control.receive_end, input.stamp)) {
            if (input.channel != control.request.channel)
                observation = MAC_POLL_OBS_FOREIGN;
            else if (!input.crc_valid)
                observation = MAC_POLL_OBS_BAD_CRC;
            else {
                status = mac_frame_decode(input.body, input.length, &frame);
                if (status == MAC_CODEC_UNSUPPORTED_TYPE || status == MAC_CODEC_UNSUPPORTED_VERSION
                        || status == MAC_CODEC_UNSUPPORTED_SECURITY
                        || status == MAC_CODEC_UNSUPPORTED_ADDRESSING
                        || status == MAC_CODEC_UNSUPPORTED_COMMAND || status == MAC_CODEC_UNSUPPORTED_BEACON) {
                    observation = MAC_POLL_OBS_UNSUPPORTED;
                    drain(MAC_POLL_UNSUPPORTED);
                } else if (status != MAC_CODEC_OK)
                    observation = MAC_POLL_OBS_MALFORMED;
                else {
                    /* Only Response can learn a still-unbound coordinator IEEE. */
                    unbound = frame.header.type == MAC_FRAME_COMMAND
                        && input.body[frame.payload_offset] == MAC_COMMAND_ASSOCIATION_RESPONSE
                        && control.request.coordinator_mode == MAC_ADDRESS_SHORT;
                    if ((frame.header.type != MAC_FRAME_DATA && frame.header.type != MAC_FRAME_COMMAND)
                            || frame.header.destination_pan != control.request.pan
                            || frame.header.source_pan != control.request.pan
                            || frame.header.destination_mode != control.request.local_mode
                            || memcmp(frame.header.destination, control.request.local, 8)
                            || (!unbound && (frame.header.source_mode != control.request.coordinator_mode
                                || memcmp(frame.header.source, control.request.coordinator, 8))))
                        observation = MAC_POLL_OBS_FOREIGN;
                    else {
                        memcpy(receipt->body, input.body, input.length);
                        receipt->length = (uint8_t)input.length;
                        receipt->payload_offset = frame.payload_offset;
                        receipt->payload_length = frame.payload_length;
                        receipt->source_relation = unbound ? MAC_POLL_SOURCE_UNBOUND : MAC_POLL_SOURCE_MATCHED;
                        observation = MAC_POLL_OBS_DELIVERY;
                        decide(frame.header.type == MAC_FRAME_DATA && frame.payload_length
                                  ? MAC_POLL_SUCCESS : MAC_POLL_NO_DATA,
                               frame.header.type == MAC_FRAME_COMMAND ? MAC_POLL_COMMAND
                                  : frame.payload_length ? MAC_POLL_DATA : MAC_POLL_EMPTY,
                               input.stamp);
                    }
                }
            }
        }
    }
    if (control.phase == MAC_POLL_RECEIVE && reached(now, control.receive_end)) {
        control.timeout_pending = 1;
        drain(0);
    }
    if (kind == MAC_POLL_CLOSED && control.close_issued && input.token == control.close_token) {
        if (!reached(input.stamp, input.through)
                || (control.timeout_pending && !reached(input.through, control.receive_end))
                || (control.ready && receipt->protocol && !reached(input.through, receipt->stamp)))
            drain(MAC_POLL_ADAPTER_ERROR);
        else
            control.closed = 1;
    }
    if (control.phase == MAC_POLL_DRAIN && control.closed && !control.tx_issued
            && (!control.submitted || tx->phase == MAC_TX_DONE || tx->phase == MAC_TX_FAULT)) {
        if (control.uncertain || tx->phase == MAC_TX_FAULT)
            fault(MAC_POLL_CLEANUP_FAILED);
        else {
            if (control.timeout_pending)
                decide(MAC_POLL_NO_DATA, MAC_POLL_TIMEOUT, control.receive_end);
            control.phase = MAC_POLL_DONE;
        }
    }
    if (control.phase == MAC_POLL_ARM && !control.token) {
        output.kind = MAC_POLL_ACTION_PREPARE;
        output.token = ++control.token;
    } else if (control.phase < MAC_POLL_DONE && control.submitted && !control.tx_issued
            && tx->phase != MAC_TX_DONE && tx->phase != MAC_TX_FAULT) {
        control.tx_issued = 1;
        control.tx_phase = tx->phase;
        control.tx_retry = tx->retries;
        control.tx_nb = tx->nb;
        control.tx_mark = tx->last;
        control.tx_token = ++control.token;
        output.token = control.tx_token;
        output.kind = MAC_POLL_ACTION_TX;
        output.tx_cancel = control.phase == MAC_POLL_DRAIN && tx->phase != MAC_TX_STOPPING;
    } else if (control.phase == MAC_POLL_DRAIN && !control.close_issued && !control.tx_issued
            && (!control.submitted || tx->phase == MAC_TX_DONE || tx->phase == MAC_TX_FAULT)) {
        control.close_issued = 1;
        control.close_token = ++control.token;
        output.token = control.close_token;
        output.kind = MAC_POLL_ACTION_CLOSE;
    }
publish:
    output.epoch = control.request.epoch;
    output.generation = control.generation;
    output.until = control.phase == MAC_POLL_DRAIN ? control.stop_at : control.deadline;
    output.receive_end = control.receive_end;
    output.phase = control.phase;
    output.observation = observation;
    output.reason = control.reason;
    output.cleanup_error = control.cleanup_error;
    output.ready = control.ready && !control.taken;
    memcpy(&p->control, &control, sizeof(control));
    memcpy(action, &output, sizeof(output));
    return MAC_POLL_OK;
}

mac_poll_result_t mac_poll_take(mac_poll_t MAC_POLL_RAM * volatile p,
    mac_poll_record_t MAC_POLL_RAM * volatile record)
{
    if (p == NULL || record == NULL)
        return MAC_POLL_INVALID;
    memcpy(&control, &p->control, sizeof(control));
    if (control.version != MAC_POLL_VERSION || control.phase == MAC_POLL_IDLE || control.phase > MAC_POLL_FAULT
            || !control.ready || control.taken)
        return MAC_POLL_STATE;
    *record = p->record;
    control.taken = 1;
    memcpy(&p->control, &control, sizeof(control));
    return MAC_POLL_OK;
}

mac_poll_result_t mac_poll_release(mac_poll_t MAC_POLL_RAM * volatile p,
    mac_tx_t MAC_POLL_RAM * volatile tx)
{
    if (p == NULL || tx == NULL)
        return MAC_POLL_INVALID;
    memcpy(&control, &p->control, sizeof(control));
    if (control.version != MAC_POLL_VERSION || control.owner != tx || control.phase != MAC_POLL_DONE || !control.taken
            || tx->generation != control.tx_generation
            || (control.submitted ? tx->phase != MAC_TX_DONE : tx->phase != MAC_TX_IDLE))
        return MAC_POLL_STATE;
    if (control.submitted)
        (void)mac_tx_release(tx);
    control.owner = NULL;
    control.phase = MAC_POLL_IDLE;
    memcpy(&p->control, &control, sizeof(control));
    return MAC_POLL_OK;
}
