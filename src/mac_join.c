/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "mac_join.h"
#include <stddef.h>
#include <string.h>

static mac_join_t MAC_JOIN_RAM staged;
#define j (&staged)
static mac_tx_t MAC_JOIN_RAM * volatile owner;
static mac_tx_t * volatile tx_argument;
static mac_join_event_t MAC_JOIN_RAM input;
static mac_join_action_t MAC_JOIN_RAM output;
static mac_poll_event_t MAC_JOIN_RAM pe;
static mac_poll_action_t MAC_JOIN_RAM pa;
static mac_association_request_t MAC_JOIN_RAM ar;
static mac_association_event_t MAC_JOIN_RAM ae;
static mac_poll_request_t MAC_JOIN_RAM pr;
static mac_join_request_t MAC_JOIN_RAM proposed;
static volatile uint32_t MAC_JOIN_RAM time;
static volatile uint32_t MAC_JOIN_RAM remaining;
static const mac_tx_event_t * volatile source;
static const mac_poll_event_t MAC_JOIN_RAM * volatile poll_event;
static volatile uint16_t MAC_JOIN_RAM admitted;
static uint8_t MAC_JOIN_RAM observation, body[25], length;
typedef char identity_contiguous[
    offsetof(mac_join_record_t, generation) == offsetof(mac_join_record_t, epoch) + sizeof(uint32_t)
    && offsetof(mac_join_action_t, epoch) == 0
    && offsetof(mac_join_action_t, generation) == sizeof(uint32_t) ? 1 : -1];

static uint8_t reached(uint32_t a, uint32_t b)
{
    return (uint32_t)(a - b) < MAC_TX_HALF;
}

static void abort_attempt(uint8_t reason)
{
    if (!j->record.reason) {
        j->record.reason = reason;
        j->record.error_stage = j->phase;
    }
    if (!j->record.result) {
        j->record.result = MAC_JOIN_LOCAL_ABORT;
        j->record.stage = j->phase;
    }
    if (!j->stopping) {
        j->stopping = 1;
        j->stop_at = time + MAC_JOIN_STOP_TIME;
        j->stop_steps = MAC_JOIN_STOP_WORK;
    }
}

static void fault(uint8_t reason)
{
    if ((j->stopping || j->phase == MAC_JOIN_RESTORE) && !j->record.cleanup_error)
        j->record.cleanup_error = reason;
    abort_attempt(reason);
    j->uncertain = 1;
    j->phase = MAC_JOIN_FAULT;
}

static void issue(uint8_t kind)
{
    j->issued = output.kind = kind;
    output.token = j->issued_token = ++j->token;
}

static void finish_window(void);

static void window(void)
{
    if (!j->window && j->poll.control.ack_seen && owner->pending) {
        memset(&ar, 0, sizeof(ar));
        ar.epoch = j->request.extraction.epoch;
        ar.lifetime = j->request.extraction.frame_wait + 1u;
        ar.work_limit = 2;
        ar.pan_id = j->request.extraction.pan;
        ar.channel = j->request.extraction.channel;
        ar.coordinator_mode = j->request.extraction.coordinator_mode;
        memcpy(ar.coordinator, j->request.extraction.coordinator, 8);
        memcpy(ar.local, j->request.extraction.local, 8);
        j->record.association_rc = mac_association_start(&j->association, &ar, j->poll.control.ack_end);
        if (j->record.association_rc != MAC_ASSOCIATION_OK)
            abort_attempt(MAC_JOIN_CONTEXT_ERROR);
        else
            j->window = 1;
    }
}

static void receipt(void)
{
    if (j->poll.control.ready && !j->poll.control.taken) {
        j->record.poll_rc = mac_poll_take(&j->poll, &j->record.poll);
        if (j->record.poll_rc != MAC_POLL_OK) {
            fault(MAC_JOIN_POLL_ERROR);
            return;
        }
        if (!j->record.result) {
            j->record.result = MAC_JOIN_POLL_RESULT;
            j->record.stage = MAC_JOIN_EXTRACT;
        }
        if (j->window && j->record.poll.cause == MAC_POLL_COMMAND) {
            memset(&ae, 0, sizeof(ae));
            ae.kind = MAC_ASSOCIATION_FRAME;
            ae.epoch = j->record.poll.epoch;
            ae.generation = j->association.generation;
            ae.stamp = j->record.poll.stamp;
            ae.body = j->record.poll.body;
            ae.length = j->record.poll.length;
            ae.channel = j->request.extraction.channel;
            ae.crc_valid = 1;
            j->record.association_rc = mac_association_step_rx(&j->association, time, &ae,
                &j->record.observation, j->request.profile);
            if (j->record.association_rc != MAC_ASSOCIATION_OK)
                abort_attempt(MAC_JOIN_CONTEXT_ERROR);
            else if (j->association.phase == MAC_ASSOCIATION_DONE)
                finish_window();
        }
    }
}

static void finish_window(void)
{
    if (j->window != 1)
        return;
    if (j->association.phase == MAC_ASSOCIATION_WAIT) {
        memset(&ae, 0, sizeof(ae));
        ae.kind = MAC_ASSOCIATION_CANCEL;
        ae.epoch = j->request.extraction.epoch;
        ae.generation = j->association.generation;
        j->record.association_rc = mac_association_step_rx(&j->association, time, &ae,
            &observation, j->request.profile);
    }
    if (j->record.association_rc == MAC_ASSOCIATION_OK)
        j->record.association_rc = mac_association_take(&j->association, &j->record.association);
    if (j->record.association_rc != MAC_ASSOCIATION_OK)
        fault(MAC_JOIN_CONTEXT_ERROR);
    j->window = 2;
}

mac_join_result_t mac_join_init(mac_join_t MAC_JOIN_RAM * volatile ctx, volatile uint32_t now)
{
    if (ctx == NULL)
        return MAC_JOIN_INVALID;
    memset(&staged, 0, sizeof(staged));
    if (mac_poll_init(&staged.poll) != MAC_POLL_OK
            || mac_association_init(&staged.association, now) != MAC_ASSOCIATION_OK)
        return MAC_JOIN_STATE;
    staged.version = MAC_JOIN_VERSION;
    staged.last = now;
    *ctx = staged;
    return MAC_JOIN_OK;
}

mac_join_result_t mac_join_start(mac_join_t MAC_JOIN_RAM * volatile ctx, mac_tx_t MAC_JOIN_RAM * volatile tx,
    const mac_join_request_t MAC_JOIN_RAM * volatile request, volatile uint32_t now)
{
    mac_header_t header;
    uint8_t command[2], i;
    volatile uint32_t generation;
    if (ctx == NULL || tx == NULL || request == NULL)
        return MAC_JOIN_INVALID;
    staged = *ctx;
    proposed = *request;
    if (!proposed.extraction.epoch || !proposed.extraction.lifetime
            || proposed.extraction.lifetime > MAC_POLL_MAX_TIME
            || !proposed.extraction.work || proposed.extraction.work > MAC_POLL_MAX_WORK
            || proposed.extraction.pan == 0xffffu
            || proposed.extraction.channel < 11 || proposed.extraction.channel > 26
            || proposed.saved.channel < 11 || proposed.saved.channel > 26
            || proposed.saved.filter > 1 || proposed.saved.rx_on > 1
            || proposed.response_wait < 2 || proposed.response_wait > 64
            || !proposed.extraction.frame_wait)
        return MAC_JOIN_INVALID;
    if (proposed.extraction.frame_wait > 65534UL
            || proposed.extraction.local_mode != MAC_ADDRESS_EXTENDED
            || (proposed.extraction.coordinator_mode != MAC_ADDRESS_SHORT
                && proposed.extraction.coordinator_mode != MAC_ADDRESS_EXTENDED)
            || (proposed.capability != 0x88u && proposed.capability != 0x8cu)
            || proposed.profile > MAC_RX_R22_ASSOCIATION_RESPONSE)
        return MAC_JOIN_UNSUPPORTED;
    if (proposed.extraction.coordinator_mode == MAC_ADDRESS_SHORT) {
        if (proposed.extraction.coordinator[1] == 255 && proposed.extraction.coordinator[0] >= 254)
            return MAC_JOIN_INVALID;
        for (i = 2; i < 8; i++)
            if (proposed.extraction.coordinator[i])
                return MAC_JOIN_INVALID;
    }
    if (staged.version != MAC_JOIN_VERSION || staged.phase != MAC_JOIN_IDLE
            || tx->phase != MAC_TX_IDLE || staged.poll.control.phase != MAC_POLL_IDLE
            || staged.association.phase != MAC_ASSOCIATION_IDLE)
        return MAC_JOIN_STATE;
    if (!reached(now, staged.last) || !reached(now, tx->last))
        return MAC_JOIN_INVALID;
    if (staged.generation == UINT32_MAX || tx->generation >= UINT32_MAX - 1u
            || staged.poll.control.generation == UINT32_MAX || staged.association.generation == UINT32_MAX)
        return MAC_JOIN_LIMIT;
    memset(&header, 0, sizeof(header));
    header.type = MAC_FRAME_COMMAND;
    header.flags = MAC_FLAG_ACK_REQUEST;
    header.destination_mode = proposed.extraction.coordinator_mode;
    header.source_mode = MAC_ADDRESS_EXTENDED;
    header.destination_pan = proposed.extraction.pan;
    header.source_pan = 0xffffu;
    memcpy(header.destination, proposed.extraction.coordinator, 8);
    memcpy(header.source, proposed.extraction.local, 8);
    command[0] = MAC_COMMAND_ASSOCIATION_REQUEST;
    command[1] = proposed.capability;
    if (mac_frame_encode(&header, command, 2, body, sizeof(body), &length) != MAC_CODEC_OK)
        return MAC_JOIN_UNSUPPORTED;
    generation = staged.generation + 1u;
    /* Nested contexts retain their generations; only new-attempt control resets. */
    memset((uint8_t *)&staged + offsetof(mac_join_t, owner), 0, sizeof(staged) - offsetof(mac_join_t, owner));
    staged.request = proposed;
    memset(&staged.record, 0, sizeof(staged.record));
    staged.record.epoch = proposed.extraction.epoch;
    staged.record.generation = staged.generation = generation;
    staged.record.tx_rc = staged.record.poll_rc = staged.record.association_rc = MAC_JOIN_UNCALLED;
    staged.owner = tx;
    staged.tx_generation = tx->generation;
    staged.version = MAC_JOIN_VERSION;
    staged.phase = MAC_JOIN_PREPARE;
    staged.last = now;
    staged.deadline = now + proposed.extraction.lifetime;
    staged.steps = proposed.extraction.work;
    /* Request bytes must survive unrelated foreground codec/controller calls. */
    memcpy(staged.outgoing, body, length);
    staged.length = length;
    *ctx = staged;
    return MAC_JOIN_OK;
}

mac_join_result_t mac_join_step(mac_join_t MAC_JOIN_RAM * volatile ctx, mac_tx_t MAC_JOIN_RAM * volatile tx,
    uint32_t now, const mac_join_event_t MAC_JOIN_RAM * volatile event, mac_join_action_t MAC_JOIN_RAM * volatile action)
{
    volatile uint8_t kind, matched, before;
    if (ctx == NULL || tx == NULL || action == NULL)
        return MAC_JOIN_INVALID;
    if (event != NULL) {
        input = *event;
        if (input.kind < MAC_JOIN_PREPARED || input.kind > MAC_JOIN_SOURCE || input.crc_valid > 1
                || (input.kind == MAC_JOIN_FRAME && (input.body == NULL || !input.serial
                    || input.channel < 11 || input.channel > 26))
                || ((input.kind == MAC_JOIN_TX || input.kind == MAC_JOIN_SOURCE)
                    && (input.source.kind > MAC_TX_EVENT_CANCEL
                        || (input.kind == MAC_JOIN_SOURCE && !input.source.kind)
                        || (input.source.kind == MAC_TX_EVENT_ACK && input.source.length && input.source.bytes == NULL))))
            return MAC_JOIN_INVALID;
    } else
        memset(&input, 0, sizeof(input));
    if (ctx->version != MAC_JOIN_VERSION || ctx->owner != tx
            || (uint8_t)(ctx->phase - MAC_JOIN_PREPARE) > MAC_JOIN_FAULT - MAC_JOIN_PREPARE)
        return MAC_JOIN_STATE;
    staged = *ctx;
    owner = tx; tx_argument = tx; time = now;
    memset(&output, 0, sizeof(output));
    kind = 0;
    if (j->phase >= MAC_JOIN_DONE)
        goto publish;
    if (!reached(time, j->last)) {
        fault(MAC_JOIN_CLOCK_ERROR);
        goto publish;
    }
    if (owner->generation != j->tx_generation) {
        fault(MAC_JOIN_TX_ERROR);
        goto publish;
    }
    if (event != NULL) {
        output.observation = MAC_JOIN_OBS_STALE;
        if (input.epoch == j->request.extraction.epoch && input.generation == j->generation
                && (input.kind == MAC_JOIN_FRAME
                    || (reached(input.stamp, j->last) && reached(time, input.stamp)))) {
            kind = input.kind;
            output.observation = MAC_JOIN_OBS_NONE;
        }
    }
    j->last = time;
    matched = kind && j->issued && input.token == j->issued_token;
    if (kind && kind != MAC_JOIN_FRAME && kind < MAC_JOIN_CANCEL && !matched) {
        output.observation = input.token && input.token <= j->token ? MAC_JOIN_OBS_DUPLICATE : MAC_JOIN_OBS_STALE;
        kind = 0;
    }
    if (j->stopping) {
        if (kind == MAC_JOIN_CANCEL || kind == MAC_JOIN_FAILURE)
            abort_attempt(kind == MAC_JOIN_CANCEL ? MAC_JOIN_CANCELLED : MAC_JOIN_ADAPTER_ERROR);
        if (!j->stop_steps || reached(time, j->stop_at)) {
            fault(MAC_JOIN_CLEANUP_FAILED);
            goto publish;
        }
        j->stop_steps--;
    } else if (kind == MAC_JOIN_CANCEL || kind == MAC_JOIN_FAILURE
            || reached(time, j->deadline) || !j->steps)
        abort_attempt(kind == MAC_JOIN_CANCEL ? MAC_JOIN_CANCELLED
            : kind == MAC_JOIN_FAILURE ? MAC_JOIN_ADAPTER_ERROR
            : !j->steps ? MAC_JOIN_WORK_LIMIT : MAC_JOIN_LIFETIME);
    else
        j->steps--;

    if (j->phase == MAC_JOIN_PREPARE) {
        if (j->stopping)
            j->phase = MAC_JOIN_RESTORE;
        else if (matched && kind == MAC_JOIN_PREPARED) {
            j->issued = 0;
            remaining = j->deadline - time;
            admitted = j->length;
            j->record.tx_rc = mac_tx_submit(tx_argument, j->outgoing, admitted,
                time, remaining, MAC_POLL_TX_STEPS);
            if (j->record.tx_rc != MAC_TX_OK) {
                abort_attempt(MAC_JOIN_TX_ERROR);
                j->phase = MAC_JOIN_RESTORE;
            } else {
                j->tx_generation = owner->generation;
                j->phase = MAC_JOIN_REQUEST;
            }
        } else if (!j->issued) {
            issue(MAC_JOIN_ACTION_PREPARE);
            output.state.pan = j->request.extraction.pan;
            output.state.channel = j->request.extraction.channel;
            output.state.rx_on = 1;
        }
    }
    if (j->phase == MAC_JOIN_REQUEST) {
        source = NULL;
        if (kind == MAC_JOIN_SOURCE && (input.source.kind != MAC_TX_EVENT_ACK || input.crc_valid))
            source = &input.source;
        if (j->stopping && owner->phase != MAC_TX_STOPPING) {
            memset(&pe.source, 0, sizeof(pe.source));
            pe.source.kind = MAC_TX_EVENT_CANCEL;
            pe.source.generation = owner->generation;
            pe.source.retry = owner->retries;
            pe.source.nb = owner->nb;
            pe.source.stamp = time;
            source = &pe.source;
        }
        before = owner->phase;
        if (before != MAC_TX_DONE && before != MAC_TX_FAULT) {
            j->record.tx_rc = mac_tx_step(tx_argument, time, source, &output.radio);
            j->record.tx_outcome = owner->outcome;
            if (j->record.tx_rc != MAC_TX_OK) {
                fault(MAC_JOIN_TX_ERROR);
                goto publish;
            }
            if (output.radio.kind)
                output.kind = MAC_JOIN_ACTION_RADIO;
            if (before == MAC_TX_ACK_WAIT && owner->phase == MAC_TX_STOPPING && owner->outcome == MAC_TX_ACKED) {
                j->record.request_ack = input.source.stamp;
                j->wait_until = input.source.stamp + (uint16_t)((uint16_t)j->request.response_wait * 960u);
            }
        }
        if (owner->phase == MAC_TX_FAULT) {
            fault(MAC_JOIN_TX_ERROR);
            goto publish;
        }
        if (owner->phase == MAC_TX_DONE) {
            j->record.tx_outcome = owner->outcome;
            j->record.tx_rc = mac_tx_release(owner);
            if (j->record.tx_rc != MAC_TX_OK) {
                fault(MAC_JOIN_TX_ERROR);
                goto publish;
            }
            if (j->stopping || j->record.tx_outcome != MAC_TX_ACKED) {
                if (!j->record.result) {
                    j->record.result = MAC_JOIN_REQUEST_RESULT;
                    j->record.stage = MAC_JOIN_REQUEST;
                }
                j->phase = MAC_JOIN_RESTORE;
            } else
                j->phase = MAC_JOIN_WAIT;
        }
    }
    if (j->phase == MAC_JOIN_WAIT) {
        if (j->stopping)
            j->phase = MAC_JOIN_RESTORE;
        else if (reached(time, j->wait_until)) {
            pr = j->request.extraction;
            pr.lifetime = j->deadline - time;
            j->record.poll_rc = mac_poll_start(&j->poll, owner, &pr, time);
            if (j->record.poll_rc != MAC_POLL_OK) {
                abort_attempt(MAC_JOIN_POLL_ERROR);
                j->phase = MAC_JOIN_RESTORE;
            } else
                j->phase = MAC_JOIN_EXTRACT;
            kind = matched = 0;
        }
    }
    if (j->phase == MAC_JOIN_EXTRACT) {
        pe = input;
        pe.kind = 0;
        pe.epoch = j->request.extraction.epoch;
        pe.generation = j->poll.control.generation;
        pe.stamp = time;
        if (matched && kind == MAC_JOIN_TX && j->issued == MAC_JOIN_ACTION_TX) {
            pe.kind = MAC_POLL_TX;
        } else if (kind == MAC_JOIN_FAILURE)
            pe.kind = MAC_POLL_FAILURE;
        else if (j->stopping && j->poll.control.phase < MAC_POLL_DRAIN)
            pe.kind = MAC_POLL_CANCEL;
        else if (matched && kind == MAC_JOIN_PREPARED && j->issued == MAC_JOIN_ACTION_RECEIVE)
            pe.kind = MAC_POLL_PREPARED;
        else if (matched && kind == MAC_JOIN_CLOSED && j->issued == MAC_JOIN_ACTION_CLOSE) {
            pe.kind = MAC_POLL_CLOSED;
        } else if (kind == MAC_JOIN_FRAME) {
            pe.kind = MAC_POLL_FRAME;
        }
        if (pe.kind && pe.kind != MAC_POLL_CANCEL) {
            pe.stamp = input.stamp;
            pe.token = j->child_token;
            if (pe.kind != MAC_POLL_FRAME && pe.kind != MAC_POLL_FAILURE)
                j->issued = 0;
        }
        poll_event = pe.kind ? &pe : NULL;
        j->record.poll_rc = mac_poll_step_rx(&j->poll, owner, time, poll_event, &pa, j->request.profile);
        j->tx_generation = owner->generation;
        if (j->record.poll_rc != MAC_POLL_OK) {
            fault(MAC_JOIN_POLL_ERROR);
            goto publish;
        }
        if (pa.observation)
            output.observation = pa.observation;
        window();
        receipt();
        j->record.poll_reason = j->poll.control.reason;
        j->record.poll_cleanup = j->poll.control.cleanup_error;
        if (j->phase == MAC_JOIN_FAULT || j->poll.control.phase == MAC_POLL_FAULT) {
            fault(MAC_JOIN_POLL_ERROR);
            goto publish;
        }
        if (j->poll.control.phase == MAC_POLL_DONE) {
            finish_window();
            if (j->phase == MAC_JOIN_FAULT)
                goto publish;
            j->record.poll_rc = mac_poll_release(&j->poll, owner);
            if (j->record.poll_rc != MAC_POLL_OK) {
                fault(MAC_JOIN_POLL_ERROR);
                goto publish;
            }
            j->issued = 0;
            j->phase = MAC_JOIN_RESTORE;
        } else if (pa.kind) {
            j->child_token = pa.token;
            issue(pa.kind == MAC_POLL_ACTION_TX ? MAC_JOIN_ACTION_TX
                : pa.kind == MAC_POLL_ACTION_PREPARE ? MAC_JOIN_ACTION_RECEIVE : MAC_JOIN_ACTION_CLOSE);
            output.tx_cancel = pa.tx_cancel;
        }
    }
    if (j->phase == MAC_JOIN_RESTORE) {
        if (kind == MAC_JOIN_FAILURE) {
            fault(MAC_JOIN_ADAPTER_ERROR);
            goto publish;
        }
        if (!j->stopping) {
            j->stopping = 1;
            j->stop_at = time + MAC_JOIN_STOP_TIME;
            j->stop_steps = MAC_JOIN_STOP_WORK;
        }
        if (owner->phase != MAC_TX_IDLE || j->poll.control.phase != MAC_POLL_IDLE) {
            fault(MAC_JOIN_CLEANUP_FAILED);
            goto publish;
        }
        if (matched && kind == MAC_JOIN_RESTORED && j->issued == MAC_JOIN_ACTION_RESTORE) {
            if (!reached(input.stamp, owner->ready_at)) {
                fault(MAC_JOIN_CLEANUP_FAILED);
                goto publish;
            }
            j->issued = 0;
            j->restored = 1;
            j->uncertain = 0;
            j->phase = MAC_JOIN_DONE;
        } else if (j->issued != MAC_JOIN_ACTION_RESTORE) {
            issue(MAC_JOIN_ACTION_RESTORE);
            output.state = j->request.saved;
        }
    }
publish:
    memcpy(&output, (const uint8_t *)&j->record + offsetof(mac_join_record_t, epoch),
           2u * sizeof(uint32_t));
    output.until = j->stopping ? j->stop_at : j->deadline;
    *ctx = staged;
    *action = output;
    return MAC_JOIN_OK;
}

mac_join_result_t mac_join_take(mac_join_t MAC_JOIN_RAM *ctx, mac_join_record_t MAC_JOIN_RAM *record)
{
    if (ctx == NULL || record == NULL)
        return MAC_JOIN_INVALID;
    if (ctx->version != MAC_JOIN_VERSION
            || (uint8_t)(ctx->phase - MAC_JOIN_DONE) > MAC_JOIN_FAULT - MAC_JOIN_DONE || ctx->taken)
        return MAC_JOIN_STATE;
    *record = ctx->record;
    ctx->taken = 1;
    return MAC_JOIN_OK;
}

mac_join_result_t mac_join_release(mac_join_t MAC_JOIN_RAM *ctx, mac_tx_t MAC_JOIN_RAM *tx)
{
    if (ctx == NULL || tx == NULL)
        return MAC_JOIN_INVALID;
    staged = *ctx;
    if (staged.version != MAC_JOIN_VERSION || staged.owner != tx || staged.phase != MAC_JOIN_DONE
            || !staged.taken || !staged.restored || staged.issued || staged.uncertain
            || tx->phase != MAC_TX_IDLE || tx->generation != staged.tx_generation)
        return MAC_JOIN_STATE;
    staged.owner = NULL;
    staged.phase = MAC_JOIN_IDLE;
    *ctx = staged;
    return MAC_JOIN_OK;
}
