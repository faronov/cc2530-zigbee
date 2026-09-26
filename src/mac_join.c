/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "mac_join.h"
#include <stddef.h>
#include <string.h>
#include "mac_link_workspace_guard_internal.h"
#if defined(CC2530_MAC_LINK_WORKSPACE)
#include "mac_link_workspace_internal.h"
#define active (link_work_arena.protocol.parent.join.active)
#define LJ_OWNER (link_work_arena.protocol.parent.join.owner)
#define tx_argument (link_work_arena.protocol.parent.join.tx_argument)
#define LJ_WORK (link_work_arena.protocol.parent.join.work)
#define time (link_work_arena.protocol.parent.join.time)
#define remaining (link_work_arena.protocol.parent.join.remaining)
#define LJ_SOURCE (link_work_arena.protocol.parent.join.source)
#define poll_event (link_work_arena.protocol.parent.join.poll_event)
#define admitted (link_work_arena.protocol.parent.join.admitted)
#define LJ_OBSERVATION (link_work_arena.protocol.parent.join.observation)
#else
#define LJ_OWNER owner
#define LJ_WORK work
#define LJ_SOURCE source
#define LJ_OBSERVATION observation
#endif

#if defined(CC2530_MAC_LINK)
#define ENGINE(tx) (&(tx)->engine)
#define SOURCE(e) e.source.source
#define RADIO output.radio.control
#define submit mac_tx_interval_submit
#define tx_step mac_tx_observed_step
#define release mac_tx_interval_release
#else
#define ENGINE(tx) tx
#define SOURCE(e) e.source
#define RADIO output.radio
#define submit mac_tx_submit
#define tx_step mac_tx_step
#define release mac_tx_release
#endif

#if defined(CC2530_MAC_LINK_RAM)
#include "mac_link_ram_internal.h"
/* After admission, step has exactly one publication return (MAC_JOIN_OK).
 * Processed faults belong to the actual context, not a rolled-back shadow.
 * start performs every fallible check/codec call before changing that context.
 * No child retains this pointer, and no join entry calls another join entry.
 */
#if !defined(CC2530_MAC_LINK_WORKSPACE)
static mac_join_t MAC_JOIN_RAM * MAC_JOIN_RAM active;
#endif
#define staged (*active)
#define JOIN_LOAD(ctx) (active = (ctx))
#define JOIN_STORE(ctx) ((void)0)
#else
#define JOIN_LOAD(ctx) (staged = *(ctx))
#define JOIN_STORE(ctx) (*(ctx) = staged)
#if defined(CC2530_JOIN_WORKSPACE)
/* Full-profile binding only: the integrator must back this direct-address
 * symbol with a real, disjoint, complete mac_join_t allocation. It is live
 * through each init/start/step/release and all nested calls, not just start.
 * No fallback pool, pointer indirection or abbreviated snapshot is supplied.
 * See MAC_JOIN.md for the separate aggregate/linker/lifetime proof gate. */
extern mac_join_t MAC_JOIN_RAM mac_join_staged;
#define staged mac_join_staged
#else
static mac_join_t MAC_JOIN_RAM staged;
#endif
#endif
#define j (&staged)
#if !defined(CC2530_MAC_LINK_WORKSPACE)
static MAC_POLL_OWNER_T MAC_JOIN_RAM * volatile LJ_OWNER;
static MAC_POLL_OWNER_T * volatile tx_argument;
#endif
/* Returning foreground work, never retained by a lower service. start and
 * step cannot overlap. Within step, pr is consumed before pe, pe before ar,
 * and ar before ae. pa, input and output remain live across those calls and
 * MUST NOT share the nested slot. In particular ae.body borrows the retained
 * staged.record.poll.body, not this union. The full transactional context
 * remains separate, including every nested receipt and diagnostic byte. */
#if !defined(CC2530_MAC_LINK_WORKSPACE)
static union {
    struct {
        mac_join_request_t proposed;
        mac_header_t header;
        uint8_t command[2], body[25], length;
    } start;
    struct {
        mac_join_event_t input;
        mac_join_action_t output;
        mac_poll_action_t pa;
        union {
            mac_poll_event_t pe;
            mac_association_request_t ar;
            mac_association_event_t ae;
            mac_poll_request_t pr;
        } nested;
    } step;
} MAC_JOIN_RAM LJ_WORK;
#endif
#define input LJ_WORK.step.input
#define output LJ_WORK.step.output
#define pa LJ_WORK.step.pa
#define pe LJ_WORK.step.nested.pe
#define ar LJ_WORK.step.nested.ar
#define ae LJ_WORK.step.nested.ae
#define pr LJ_WORK.step.nested.pr
#define proposed LJ_WORK.start.proposed
#define header LJ_WORK.start.header
#define command LJ_WORK.start.command
#define start_body LJ_WORK.start.body
#define start_length LJ_WORK.start.length
#if !defined(CC2530_MAC_LINK_WORKSPACE)
static volatile uint32_t MAC_JOIN_RAM time;
static volatile uint32_t MAC_JOIN_RAM remaining;
#if defined(CC2530_MAC_LINK)
static const mac_tx_interval_event_t * volatile LJ_SOURCE;
#else
static const mac_tx_event_t * volatile LJ_SOURCE;
#endif
static const mac_poll_event_t MAC_JOIN_RAM * volatile poll_event;
static volatile uint16_t MAC_JOIN_RAM admitted;
static uint8_t MAC_JOIN_RAM LJ_OBSERVATION;
#endif
#if defined(CC2530_MAC_LINK_RAM)
static mac_join_result_t join_return(mac_join_result_t result)
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    return link_work_finish_group(LW_JOIN_INIT, LW_JOIN_OTHER, result);
#else
    volatile uint8_t MAC_JOIN_RAM *p = (volatile uint8_t MAC_JOIN_RAM *)&LJ_WORK;
    uint16_t i;
    for (i = 0; i < sizeof(LJ_WORK); i++) p[i] = 0;
    active = NULL; LJ_OWNER = NULL; tx_argument = NULL;
    LJ_SOURCE = NULL; poll_event = NULL;
    time = remaining = 0; admitted = 0; LJ_OBSERVATION = 0;
    return result;
#endif
}
#define JOIN_RETURN(result) join_return(result)
#if defined(CC2530_HOST_TEST)
unsigned char mac_link_ram_join_clean(void)
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    return link_work_clean();
#else
    const uint8_t *p = (const uint8_t *)&LJ_WORK;
    size_t i;
    for (i = 0; i < sizeof(LJ_WORK); i++) if (p[i]) return 0;
    return !active && !LJ_OWNER && !tx_argument && !LJ_SOURCE && !poll_event &&
        !time && !remaining && !admitted && !LJ_OBSERVATION;
#endif
}
#endif
#else
#define JOIN_RETURN(result) (result)
#endif
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
    if (!j->window && j->poll.control.ack_seen && ENGINE(LJ_OWNER)->pending) {
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
            &LJ_OBSERVATION, j->request.profile);
    }
    if (j->record.association_rc == MAC_ASSOCIATION_OK)
        j->record.association_rc = mac_association_take(&j->association, &j->record.association);
    if (j->record.association_rc != MAC_ASSOCIATION_OK)
        fault(MAC_JOIN_CONTEXT_ERROR);
    j->window = 2;
}

mac_join_result_t mac_join_init(mac_join_t MAC_JOIN_RAM * volatile ctx, volatile uint32_t now)
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    LW_ENTER(LW_JOIN_INIT, MAC_JOIN_INVALID);
    if (!LW_IO(LW_JOIN_INIT, ctx, sizeof(*ctx), 1)) return LW_RETURN(LW_JOIN_INIT, MAC_JOIN_INVALID);
#endif
    if (ctx == NULL)
        return JOIN_RETURN(MAC_JOIN_INVALID);
#if defined(CC2530_MAC_LINK_RAM)
    if (!link_ram_span(ctx, sizeof(*ctx))) return JOIN_RETURN(MAC_JOIN_INVALID);
    active = ctx;
    /* Both initializers reject only NULL and receive actual nested objects.
     * They neither call hardware nor have a fallible post-admission path. */
#endif
    memset(&staged, 0, sizeof(staged));
    if (mac_poll_init(&staged.poll) != MAC_POLL_OK
            || mac_association_init(&staged.association, now) != MAC_ASSOCIATION_OK)
        return JOIN_RETURN(MAC_JOIN_STATE);
    staged.version = MAC_JOIN_VERSION;
    staged.last = now;
    JOIN_STORE(ctx);
    return JOIN_RETURN(MAC_JOIN_OK);
}

mac_join_result_t mac_join_start(mac_join_t MAC_JOIN_RAM * volatile ctx, MAC_POLL_OWNER_T MAC_JOIN_RAM * volatile tx,
    const mac_join_request_t MAC_JOIN_RAM * volatile request, volatile uint32_t now)
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    LW_ENTER(LW_JOIN_START, MAC_JOIN_INVALID);
    if (!LW_IO(LW_JOIN_START, ctx, sizeof(*ctx), 1) ||
        !LW_IO(LW_JOIN_START, tx, sizeof(*tx), 1) ||
        !LW_IO(LW_JOIN_START, request, sizeof(*request), 0)) return LW_RETURN(LW_JOIN_START, MAC_JOIN_INVALID);
#endif
    uint8_t i;
    volatile uint32_t generation;
    if (ctx == NULL || tx == NULL || request == NULL)
        return JOIN_RETURN(MAC_JOIN_INVALID);
#if defined(CC2530_MAC_LINK_RAM)
    if (!link_ram_disjoint(ctx, sizeof(*ctx), tx, sizeof(*tx)) ||
        !link_ram_disjoint(ctx, sizeof(*ctx), request, sizeof(*request)) ||
        !link_ram_disjoint(tx, sizeof(*tx), request, sizeof(*request)))
        return JOIN_RETURN(MAC_JOIN_INVALID);
#endif
    JOIN_LOAD(ctx);
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
        return JOIN_RETURN(MAC_JOIN_INVALID);
    if (proposed.extraction.frame_wait > 65534UL
            || proposed.extraction.local_mode != MAC_ADDRESS_EXTENDED
            || (proposed.extraction.coordinator_mode != MAC_ADDRESS_SHORT
                && proposed.extraction.coordinator_mode != MAC_ADDRESS_EXTENDED)
            || (proposed.capability != 0x88u && proposed.capability != 0x8cu)
            || proposed.profile > MAC_RX_R22_ASSOCIATION_RESPONSE)
        return JOIN_RETURN(MAC_JOIN_UNSUPPORTED);
    if (proposed.extraction.coordinator_mode == MAC_ADDRESS_SHORT) {
        if (proposed.extraction.coordinator[1] == 255 && proposed.extraction.coordinator[0] >= 254)
            return JOIN_RETURN(MAC_JOIN_INVALID);
        for (i = 2; i < 8; i++)
            if (proposed.extraction.coordinator[i])
                return JOIN_RETURN(MAC_JOIN_INVALID);
    }
    if (staged.version != MAC_JOIN_VERSION || staged.phase != MAC_JOIN_IDLE
            || ENGINE(tx)->phase != MAC_TX_IDLE || staged.poll.control.phase != MAC_POLL_IDLE
            || staged.association.phase != MAC_ASSOCIATION_IDLE)
        return JOIN_RETURN(MAC_JOIN_STATE);
    if (!reached(now, staged.last) || !reached(now, ENGINE(tx)->last))
        return JOIN_RETURN(MAC_JOIN_INVALID);
    if (staged.generation == UINT32_MAX || ENGINE(tx)->generation >= UINT32_MAX - 1u
            || staged.poll.control.generation == UINT32_MAX || staged.association.generation == UINT32_MAX)
        return JOIN_RETURN(MAC_JOIN_LIMIT);
    /* Every field is assigned, including the two zero-valued wire fields;
     * the start member may previously have held arbitrary step bytes. */
    header.version = 0;
    header.sequence = 0;
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
    if (mac_frame_encode(&header, command, 2, start_body, sizeof(start_body), &start_length) != MAC_CODEC_OK)
        return JOIN_RETURN(MAC_JOIN_UNSUPPORTED);
    generation = staged.generation + 1u;
    /* Nested contexts retain their generations; only new-attempt control resets. */
    memset((uint8_t *)&staged + offsetof(mac_join_t, owner), 0, sizeof(staged) - offsetof(mac_join_t, owner));
    staged.request = proposed;
    memset(&staged.record, 0, sizeof(staged.record));
    staged.record.epoch = proposed.extraction.epoch;
    staged.record.generation = staged.generation = generation;
    staged.record.tx_rc = staged.record.poll_rc = staged.record.association_rc = MAC_JOIN_UNCALLED;
    staged.owner = tx;
    staged.tx_generation = ENGINE(tx)->generation;
    staged.version = MAC_JOIN_VERSION;
    staged.phase = MAC_JOIN_PREPARE;
    staged.last = now;
    staged.deadline = now + proposed.extraction.lifetime;
    staged.steps = proposed.extraction.work;
    /* Request bytes must survive unrelated foreground codec/controller calls. */
    memcpy(staged.outgoing, start_body, start_length);
    staged.length = start_length;
    JOIN_STORE(ctx);
    return JOIN_RETURN(MAC_JOIN_OK);
}

mac_join_result_t mac_join_step(mac_join_t MAC_JOIN_RAM * volatile ctx, MAC_POLL_OWNER_T MAC_JOIN_RAM * volatile tx,
    uint32_t now, const mac_join_event_t MAC_JOIN_RAM * volatile event, mac_join_action_t MAC_JOIN_RAM * volatile action)
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    LW_ENTER(LW_JOIN_STEP, MAC_JOIN_INVALID);
    if (!LW_IO(LW_JOIN_STEP, ctx, sizeof(*ctx), 1) ||
        !LW_IO(LW_JOIN_STEP, tx, sizeof(*tx), 1) ||
        !LW_IO(LW_JOIN_STEP, action, sizeof(*action), 1) ||
        !LW_IO(LW_JOIN_STEP, event, event ? sizeof(*event) : 0, 0)) return LW_RETURN(LW_JOIN_STEP, MAC_JOIN_INVALID);
#endif
    volatile uint8_t kind, matched, before;
    if (ctx == NULL || tx == NULL || action == NULL)
        return JOIN_RETURN(MAC_JOIN_INVALID);
#if defined(CC2530_MAC_LINK_RAM)
    if (!link_ram_disjoint(ctx, sizeof(*ctx), tx, sizeof(*tx)) ||
        !link_ram_disjoint(ctx, sizeof(*ctx), action, sizeof(*action)) ||
        !link_ram_disjoint(tx, sizeof(*tx), action, sizeof(*action)) ||
        (event && (!link_ram_disjoint(ctx, sizeof(*ctx), event, sizeof(*event)) ||
                   !link_ram_disjoint(tx, sizeof(*tx), event, sizeof(*event)) ||
                   !link_ram_disjoint(action, sizeof(*action), event, sizeof(*event)))))
        return JOIN_RETURN(MAC_JOIN_INVALID);
#endif
    if (event != NULL) {
        input = *event;
        if (input.kind < MAC_JOIN_PREPARED ||
#if defined(CC2530_MAC_LINK)
                input.kind > MAC_JOIN_DISARMED ||
#else
                input.kind > MAC_JOIN_SOURCE ||
#endif
                input.crc_valid > 1
                || (input.kind == MAC_JOIN_FRAME && (input.body == NULL || !input.serial
                    || input.channel < 11 || input.channel > 26))
                || ((input.kind == MAC_JOIN_TX || input.kind == MAC_JOIN_SOURCE)
#if defined(CC2530_MAC_LINK)
                    && (SOURCE(input).kind > MAC_TX_EVENT_RETIRED
                        || SOURCE(input).kind == MAC_TX_EVENT_SENT
                        || SOURCE(input).kind == MAC_TX_EVENT_ACK
                        || (input.kind == MAC_JOIN_SOURCE && !SOURCE(input).kind)
                        || (SOURCE(input).kind == MAC_TX_EVENT_ACK_INTERVAL
                            && SOURCE(input).length && SOURCE(input).bytes == NULL)
                        || (SOURCE(input).kind >= MAC_TX_EVENT_SENT_INTERVAL
                            && (input.source.upper.fine >= 512u
                                || !reached(SOURCE(input).stamp, MAC_LINK_CEIL(input.source.upper))))
                        || ((SOURCE(input).kind == MAC_TX_EVENT_SENT_INTERVAL
                             || SOURCE(input).kind == MAC_TX_EVENT_ACK_INTERVAL
                             || SOURCE(input).kind == MAC_TX_EVENT_BUSY_INTERVAL)
                            && (input.source.lower.fine >= 512u
                                || !reached(input.source.upper.symbols, input.source.lower.symbols)
                                || (input.source.upper.symbols == input.source.lower.symbols
                                    && input.source.upper.fine < input.source.lower.fine))))))
#else
                    && (input.source.kind > MAC_TX_EVENT_CANCEL
                        || (input.kind == MAC_JOIN_SOURCE && !input.source.kind)
                        || (input.source.kind == MAC_TX_EVENT_ACK && input.source.length && input.source.bytes == NULL))))
#endif
            return JOIN_RETURN(MAC_JOIN_INVALID);
    } else
        memset(&input, 0, sizeof(input));
    if (ctx->version != MAC_JOIN_VERSION || ctx->owner != tx
            || (uint8_t)(ctx->phase - MAC_JOIN_PREPARE) > MAC_JOIN_FAULT - MAC_JOIN_PREPARE)
        return JOIN_RETURN(MAC_JOIN_STATE);
    JOIN_LOAD(ctx);
    LJ_OWNER = tx; tx_argument = tx; time = now;
    memset(&output, 0, sizeof(output));
    kind = 0;
    if (j->phase >= MAC_JOIN_DONE)
        goto publish;
    if (!reached(time, j->last)) {
        fault(MAC_JOIN_CLOCK_ERROR);
        goto publish;
    }
    if (ENGINE(LJ_OWNER)->generation != j->tx_generation) {
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
            j->record.tx_rc = submit(tx_argument, j->outgoing, admitted,
                time, remaining, MAC_POLL_TX_STEPS);
            if (j->record.tx_rc != MAC_TX_OK) {
                abort_attempt(MAC_JOIN_TX_ERROR);
                j->phase = MAC_JOIN_RESTORE;
            } else {
                j->tx_generation = ENGINE(LJ_OWNER)->generation;
                j->phase = MAC_JOIN_REQUEST;
#if defined(CC2530_MAC_LINK)
                j->armed = j->disarmed = 0;
                issue(MAC_JOIN_ACTION_ARM);
                goto publish;
#endif
            }
        } else if (!j->issued) {
            issue(MAC_JOIN_ACTION_PREPARE);
            output.state.pan = j->request.extraction.pan;
            output.state.channel = j->request.extraction.channel;
            output.state.rx_on = 1;
        }
    }
    if (j->phase == MAC_JOIN_REQUEST) {
#if defined(CC2530_MAC_LINK)
        if (!j->armed && !j->stopping && ENGINE(LJ_OWNER)->phase != MAC_TX_DONE) {
            if (kind == MAC_JOIN_ARMED && matched && j->issued == MAC_JOIN_ACTION_ARM) {
                j->armed = 1;
                j->issued = 0;
            } else {
                if (j->issued != MAC_JOIN_ACTION_ARM) issue(MAC_JOIN_ACTION_ARM);
                goto publish;
            }
        }
#endif
        LJ_SOURCE = NULL;
#if defined(CC2530_MAC_LINK)
        if (kind == MAC_JOIN_SOURCE && (SOURCE(input).kind != MAC_TX_EVENT_ACK_INTERVAL || input.crc_valid))
#else
        if (kind == MAC_JOIN_SOURCE && (input.source.kind != MAC_TX_EVENT_ACK || input.crc_valid))
#endif
            LJ_SOURCE = &input.source;
        if (j->stopping && ENGINE(LJ_OWNER)->phase != MAC_TX_STOPPING) {
            memset(&pe.source, 0, sizeof(pe.source));
            SOURCE(pe).kind = MAC_TX_EVENT_CANCEL;
            SOURCE(pe).generation = ENGINE(LJ_OWNER)->generation;
            SOURCE(pe).retry = ENGINE(LJ_OWNER)->retries;
            SOURCE(pe).nb = ENGINE(LJ_OWNER)->nb;
            SOURCE(pe).stamp = time;
            LJ_SOURCE = &pe.source;
        }
        before = ENGINE(LJ_OWNER)->phase;
        if (before != MAC_TX_DONE && before != MAC_TX_FAULT) {
            j->record.tx_rc = tx_step(tx_argument, time, LJ_SOURCE, &output.radio);
            j->record.tx_outcome = ENGINE(LJ_OWNER)->outcome;
            if (j->record.tx_rc != MAC_TX_OK) {
                fault(MAC_JOIN_TX_ERROR);
                goto publish;
            }
            if (RADIO.kind)
                output.kind = MAC_JOIN_ACTION_RADIO;
#if defined(CC2530_MAC_LINK)
            if (ENGINE(LJ_OWNER)->phase == MAC_TX_DRAW) {
                j->armed = 0;
                issue(MAC_JOIN_ACTION_ARM);
            }
#endif
            if (before == MAC_TX_ACK_WAIT && ENGINE(LJ_OWNER)->phase == MAC_TX_STOPPING && ENGINE(LJ_OWNER)->outcome == MAC_TX_ACKED) {
#if defined(CC2530_MAC_LINK)
                j->record.request_ack = MAC_LINK_CEIL(input.source.upper);
                j->wait_until = j->record.request_ack + (uint16_t)((uint16_t)j->request.response_wait * 960u);
#else
                j->record.request_ack = input.source.stamp;
                j->wait_until = input.source.stamp + (uint16_t)((uint16_t)j->request.response_wait * 960u);
#endif
            }
#if defined(CC2530_MAC_LINK)
            if (LJ_OWNER->engine.outcome == MAC_TX_TIMING_UNCERTAIN)
                abort_attempt(MAC_JOIN_TIMING_UNCERTAIN);
#endif
        }
        if (ENGINE(LJ_OWNER)->phase == MAC_TX_FAULT) {
            fault(MAC_JOIN_TX_ERROR);
            goto publish;
        }
        if (ENGINE(LJ_OWNER)->phase == MAC_TX_DONE) {
#if defined(CC2530_MAC_LINK)
            if (!j->disarmed) {
                if (kind == MAC_JOIN_DISARMED && matched && j->issued == MAC_JOIN_ACTION_DISARM) {
                    j->disarmed = 1;
                    j->issued = 0;
                } else {
                    if (j->issued != MAC_JOIN_ACTION_DISARM) issue(MAC_JOIN_ACTION_DISARM);
                    goto publish;
                }
            }
#endif
            j->record.tx_outcome = ENGINE(LJ_OWNER)->outcome;
            j->record.tx_rc = release(LJ_OWNER);
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
            j->record.poll_rc = mac_poll_start(&j->poll, LJ_OWNER, &pr, time);
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
        j->record.poll_rc = mac_poll_step_rx(&j->poll, LJ_OWNER, time, poll_event, &pa, j->request.profile);
        j->tx_generation = ENGINE(LJ_OWNER)->generation;
        if (j->record.poll_rc != MAC_POLL_OK) {
            fault(MAC_JOIN_POLL_ERROR);
            goto publish;
        }
        if (pa.observation)
            output.observation = pa.observation;
#if defined(CC2530_MAC_LINK)
        if (j->poll.control.reason == MAC_POLL_TIMING_UNCERTAIN)
            abort_attempt(MAC_JOIN_TIMING_UNCERTAIN);
#endif
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
            j->record.poll_rc = mac_poll_release(&j->poll, LJ_OWNER);
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
        if (ENGINE(LJ_OWNER)->phase != MAC_TX_IDLE || j->poll.control.phase != MAC_POLL_IDLE) {
            fault(MAC_JOIN_CLEANUP_FAILED);
            goto publish;
        }
        if (matched && kind == MAC_JOIN_RESTORED && j->issued == MAC_JOIN_ACTION_RESTORE) {
            if (!reached(input.stamp, ENGINE(LJ_OWNER)->ready_at)) {
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
    JOIN_STORE(ctx);
    *action = output;
    return JOIN_RETURN(MAC_JOIN_OK);
}

mac_join_result_t mac_join_take(mac_join_t MAC_JOIN_RAM *ctx, mac_join_record_t MAC_JOIN_RAM *record)
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    LW_ENTER(LW_JOIN_OTHER, MAC_JOIN_INVALID);
    if (!LW_IO(LW_JOIN_OTHER, ctx, sizeof(*ctx), 1) ||
        !LW_IO(LW_JOIN_OTHER, record, sizeof(*record), 1)) return LW_RETURN(LW_JOIN_OTHER, MAC_JOIN_INVALID);
#endif
    if (ctx == NULL || record == NULL)
        return JOIN_RETURN(MAC_JOIN_INVALID);
#if defined(CC2530_MAC_LINK_RAM)
    if (!link_ram_disjoint(ctx, sizeof(*ctx), record, sizeof(*record)))
        return JOIN_RETURN(MAC_JOIN_INVALID);
#endif
    if (ctx->version != MAC_JOIN_VERSION
            || (uint8_t)(ctx->phase - MAC_JOIN_DONE) > MAC_JOIN_FAULT - MAC_JOIN_DONE || ctx->taken)
        return JOIN_RETURN(MAC_JOIN_STATE);
    *record = ctx->record;
    ctx->taken = 1;
    return JOIN_RETURN(MAC_JOIN_OK);
}

mac_join_result_t mac_join_release(mac_join_t MAC_JOIN_RAM *ctx, MAC_POLL_OWNER_T MAC_JOIN_RAM *tx)
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    LW_ENTER(LW_JOIN_OTHER, MAC_JOIN_INVALID);
    if (!LW_IO(LW_JOIN_OTHER, ctx, sizeof(*ctx), 1) ||
        !LW_IO(LW_JOIN_OTHER, tx, sizeof(*tx), 1)) return LW_RETURN(LW_JOIN_OTHER, MAC_JOIN_INVALID);
#endif
    if (ctx == NULL || tx == NULL)
        return JOIN_RETURN(MAC_JOIN_INVALID);
#if defined(CC2530_MAC_LINK_RAM)
    if (!link_ram_disjoint(ctx, sizeof(*ctx), tx, sizeof(*tx)))
        return JOIN_RETURN(MAC_JOIN_INVALID);
#endif
    JOIN_LOAD(ctx);
    if (staged.version != MAC_JOIN_VERSION || staged.owner != tx || staged.phase != MAC_JOIN_DONE
            || !staged.taken || !staged.restored || staged.issued || staged.uncertain
            || ENGINE(tx)->phase != MAC_TX_IDLE || ENGINE(tx)->generation != staged.tx_generation)
        return JOIN_RETURN(MAC_JOIN_STATE);
    staged.owner = NULL;
    staged.phase = MAC_JOIN_IDLE;
    JOIN_STORE(ctx);
    return JOIN_RETURN(MAC_JOIN_OK);
}
