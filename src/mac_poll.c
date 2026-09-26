/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "mac_poll.h"
#include <stddef.h>
#include <string.h>
#include "mac_link_workspace_guard_internal.h"
#if defined(CC2530_MAC_LINK_WORKSPACE)
#include "mac_link_workspace_internal.h"
#define active (link_work_arena.protocol.poll.active)
#define syntax (link_work_arena.protocol.poll.syntax)
#define io (link_work_arena.protocol.poll.io)
#define receipt (link_work_arena.protocol.poll.receipt)
#define step_time (link_work_arena.protocol.poll.step_time)
#define step_poll (link_work_arena.protocol.poll.step_poll)
#define step_tx (link_work_arena.protocol.poll.step_tx)
#define step_event (link_work_arena.protocol.poll.step_event)
#define step_action (link_work_arena.protocol.poll.step_action)
#define step_now (link_work_arena.protocol.poll.step_now)
#define step_profile (link_work_arena.protocol.poll.step_profile)
#endif

#if defined(CC2530_MAC_LINK)
#define ENGINE(tx) (&(tx)->engine)
#define submit mac_tx_interval_submit
#define release mac_tx_interval_release
#else
#define ENGINE(tx) tx
#define submit mac_tx_submit
#define release mac_tx_release
#endif

#if defined(CC2530_MAC_LINK_RAM)
#include "mac_link_ram_internal.h"
typedef char control_first[(offsetof(mac_poll_t, control) == 0) ? 1 : -1];
#if !defined(CC2530_MAC_LINK_WORKSPACE)
static mac_poll_control_t MAC_POLL_RAM * MAC_POLL_RAM active;
#endif
#define control (*active)
/* C's structure-to-initial-member conversion, not a mirror/suffix cast:
 * the pointed-to object is the actual mac_poll_control_t first member. */
#define POLL_LOAD(p) (active = (mac_poll_control_t MAC_POLL_RAM *)(p))
#define POLL_STORE(p) ((void)0)
#define START_REQUEST (*request)
#else
static mac_poll_control_t MAC_POLL_RAM control;
#define POLL_LOAD(p) memcpy(&control, &(p)->control, sizeof(control))
#define POLL_STORE(p) memcpy(&(p)->control, &control, sizeof(control))
#define START_REQUEST control.request
#endif
/* start's codec input and step's decoded frame have disjoint returning
 * lifetimes. Both stay separate from control, input, output and the receipt. */
#if !defined(CC2530_MAC_LINK_WORKSPACE)
static union {
    mac_header_t header;
    mac_frame_info_t frame;
} MAC_POLL_RAM syntax;
#endif
#define start_header syntax.header
#define decoded syntax.frame
/* No input field is read after publish. In particular the real ACK witness
 * and FRAME/CLOSED checks finish before output takes ownership of this slot. */
#if !defined(CC2530_MAC_LINK_WORKSPACE)
static union {
    mac_poll_event_t input;
    mac_poll_action_t output;
#if defined(CC2530_MAC_LINK_RAM)
    struct {
        uint8_t body[sizeof(((mac_poll_control_t *)0)->outgoing)], length;
    } start;
#endif
} MAC_POLL_RAM io;
#endif
#define input io.input
#define output io.output
#if !defined(CC2530_MAC_LINK_WORKSPACE)
static mac_poll_record_t MAC_POLL_RAM *receipt;
static uint32_t MAC_POLL_RAM step_time;

/* Sequential argument staging avoids a forwarding call's four SDCC IRAM
 * spills. Both entries replace every argument, including the RX profile.
 */
static mac_poll_t MAC_POLL_RAM * volatile step_poll;
static MAC_POLL_OWNER_T MAC_POLL_RAM * volatile step_tx;
static const mac_poll_event_t MAC_POLL_RAM * volatile step_event;
static mac_poll_action_t MAC_POLL_RAM * volatile step_action;
static volatile uint32_t MAC_POLL_RAM step_now;
static volatile uint8_t MAC_POLL_RAM step_profile;
#endif

#if defined(CC2530_MAC_LINK_RAM)
/* Each public return drops all borrowed views. The real control/receipt,
 * including FAULT/late-cleanup reasons, stays exclusively in the caller.
 * start's wire candidate shares only the disjoint step input/output slot.
 */
static mac_poll_result_t poll_return(mac_poll_result_t result)
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    return link_work_finish_group(LW_POLL_INIT, LW_POLL_OTHER, result);
#else
    volatile uint8_t MAC_POLL_RAM *p = (volatile uint8_t MAC_POLL_RAM *)&syntax;
    uint16_t i;
    for (i = 0; i < sizeof(syntax); i++) p[i] = 0;
    p = (volatile uint8_t MAC_POLL_RAM *)&io;
    for (i = 0; i < sizeof(io); i++) p[i] = 0;
    active = NULL; receipt = NULL; step_poll = NULL; step_tx = NULL;
    step_event = NULL; step_action = NULL;
    step_time = step_now = 0; step_profile = 0;
    return result;
#endif
}
#define POLL_RETURN(result) poll_return(result)
#if defined(CC2530_HOST_TEST)
unsigned char mac_link_ram_poll_clean(void)
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    return link_work_clean();
#else
    const uint8_t *p = (const uint8_t *)&syntax;
    size_t i;
    for (i = 0; i < sizeof(syntax); i++) if (p[i]) return 0;
    p = (const uint8_t *)&io;
    for (i = 0; i < sizeof(io); i++) if (p[i]) return 0;
    return !active && !receipt && !step_poll && !step_tx && !step_event && !step_action &&
        !step_time && !step_now && !step_profile;
#endif
}
#endif
#else
#define POLL_RETURN(result) (result)
#endif

#if !defined(CC2530_MAC_LINK_RAM)
typedef char control_first[(offsetof(mac_poll_t, control) == 0) ? 1 : -1];
#endif
typedef char request_first[(offsetof(mac_poll_control_t, request) == 0) ? 1 : -1];
typedef char record_boundary[(offsetof(mac_poll_t, record) == sizeof(mac_poll_control_t)) ? 1 : -1];
#ifdef __SDCC
#if defined(CC2530_MAC_LINK)
typedef char control_size[(sizeof(mac_poll_control_t) == 131) ? 1 : -1];
typedef char context_size[(sizeof(mac_poll_t) == 274) ? 1 : -1];
#else
typedef char control_size[(sizeof(mac_poll_control_t) == 123) ? 1 : -1];
typedef char context_size[(sizeof(mac_poll_t) == 266) ? 1 : -1];
#endif
typedef char record_size[(sizeof(mac_poll_record_t) == 143) ? 1 : -1];
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

#if defined(CC2530_MAC_LINK)
static uint8_t accept_ack(mac_tx_interval_t MAC_POLL_RAM * volatile tx)
{
    if (!(control.tx_phase == MAC_TX_ACK_WAIT && tx->engine.phase == MAC_TX_STOPPING
        && tx->engine.outcome == MAC_TX_ACKED
        && input.source.source.kind == MAC_TX_EVENT_ACK_INTERVAL && input.crc_valid
        && input.source.source.generation == control.tx_generation
        && input.source.source.retry == control.tx_retry && input.source.source.nb == control.tx_nb
        && input.source.source.length == 3 && (input.source.source.bytes[0] & 7u) == MAC_FRAME_ACK
        && input.source.source.bytes[2] == tx->engine.frame[2]
        && ((input.source.source.bytes[0] & MAC_FLAG_PENDING) != 0u) == tx->engine.pending
        /* Physical bounds can precede the last foreground report. The
         * adapter's causal ACK lower bound is the retained TX lower bound,
         * not an invented captured ACK end. Order reports separately. */
        && reached(input.source.source.stamp, control.tx_mark)
        && reached(input.source.lower.symbols, tx->tx_lower.symbols)
        && (input.source.lower.symbols != tx->tx_lower.symbols ||
            input.source.lower.fine >= tx->tx_lower.fine)
        && reached(input.stamp, MAC_LINK_CEIL(input.source.upper))))
        return 0;
    control.ack_seen = 1;
    control.ack_end = MAC_LINK_FLOOR(input.source.lower);
    control.ack_upper = MAC_LINK_CEIL(input.source.upper);
    control.accept_end = control.ack_end + control.request.frame_wait;
    control.receive_end = control.ack_upper + control.request.frame_wait;
    if (control.phase != MAC_POLL_DRAIN) {
        if (tx->engine.pending)
            control.phase = MAC_POLL_RECEIVE;
        else
            decide(MAC_POLL_NO_DATA, MAC_POLL_PENDING_ZERO, control.ack_upper);
    }
    return 1;
}
#else
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
#endif

mac_poll_result_t mac_poll_init(mac_poll_t MAC_POLL_RAM * volatile p)
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    LW_ENTER(LW_POLL_INIT, MAC_POLL_INVALID);
    if (!LW_IO(LW_POLL_INIT, p, sizeof(*p), 1)) return LW_RETURN(LW_POLL_INIT, MAC_POLL_INVALID);
#endif
    if (p == NULL)
        return POLL_RETURN(MAC_POLL_INVALID);
#if defined(CC2530_MAC_LINK_RAM)
    if (!link_ram_span(p, sizeof(*p))) return POLL_RETURN(MAC_POLL_INVALID);
#endif
    memset(p, 0, sizeof(*p));
#if defined(CC2530_MAC_LINK_RAM)
    POLL_LOAD(p);
    control.version = MAC_POLL_VERSION;
#else
    p->control.version = MAC_POLL_VERSION;
#endif
    return POLL_RETURN(MAC_POLL_OK);
}

mac_poll_result_t mac_poll_start(mac_poll_t MAC_POLL_RAM * volatile p,
    MAC_POLL_OWNER_T MAC_POLL_RAM * volatile tx,
    const mac_poll_request_t MAC_POLL_RAM * volatile request, uint32_t volatile now)
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    LW_ENTER(LW_POLL_START, MAC_POLL_INVALID);
    if (!LW_IO(LW_POLL_START, p, sizeof(*p), 1) ||
        !LW_IO(LW_POLL_START, tx, sizeof(*tx), 1) ||
        !LW_IO(LW_POLL_START, request, sizeof(*request), 0)) return LW_RETURN(LW_POLL_START, MAC_POLL_INVALID);
#endif
    uint8_t command = MAC_COMMAND_DATA_REQUEST;
    volatile uint32_t generation;
    if (p == NULL || tx == NULL || request == NULL)
        return POLL_RETURN(MAC_POLL_INVALID);
#if defined(CC2530_MAC_LINK_RAM)
    if (!link_ram_disjoint(p, sizeof(*p), tx, sizeof(*tx)) ||
        !link_ram_disjoint(p, sizeof(*p), request, sizeof(*request)) ||
        !link_ram_disjoint(tx, sizeof(*tx), request, sizeof(*request)))
        return POLL_RETURN(MAC_POLL_INVALID);
#endif
    POLL_LOAD(p);
#if !defined(CC2530_MAC_LINK_RAM)
    memcpy(&control.request, request, sizeof(control.request));
#endif
    if (!START_REQUEST.epoch
            || !START_REQUEST.frame_wait || START_REQUEST.frame_wait > MAC_POLL_MAX_TIME
            || !START_REQUEST.lifetime || START_REQUEST.lifetime > MAC_POLL_MAX_TIME
            || !START_REQUEST.work || START_REQUEST.work > MAC_POLL_MAX_WORK
            || START_REQUEST.pan == 0xffffu || START_REQUEST.channel < 11 || START_REQUEST.channel > 26
            || !address(START_REQUEST.local_mode, START_REQUEST.local)
            || !address(START_REQUEST.coordinator_mode, START_REQUEST.coordinator))
        return POLL_RETURN(MAC_POLL_INVALID);
    if (control.version != MAC_POLL_VERSION || control.phase != MAC_POLL_IDLE || ENGINE(tx)->phase != MAC_TX_IDLE)
        return POLL_RETURN(MAC_POLL_STATE);
    if (!reached(now, ENGINE(tx)->last) || (control.generation && !reached(now, control.last)))
        return POLL_RETURN(MAC_POLL_INVALID);
    if (control.generation == UINT32_MAX || ENGINE(tx)->generation == UINT32_MAX)
        return POLL_RETURN(MAC_POLL_LIMIT);
    /* Replace every field even when the union last held a decoded frame. */
    start_header.version = 0;
    start_header.sequence = 0;
    start_header.type = MAC_FRAME_COMMAND;
    start_header.flags = MAC_FLAG_ACK_REQUEST | MAC_FLAG_PAN_COMPRESSION;
    start_header.destination_mode = START_REQUEST.coordinator_mode;
    start_header.source_mode = START_REQUEST.local_mode;
    start_header.source_pan = start_header.destination_pan = START_REQUEST.pan;
    memcpy(start_header.destination, START_REQUEST.coordinator, 8);
    memcpy(start_header.source, START_REQUEST.local, 8);
    generation = control.generation + 1;
#if defined(CC2530_MAC_LINK_RAM)
    /* Complete the last fallible operation before any caller write. The
     * pointer-valid request is disjoint from p/tx and is not retained. */
    if (mac_frame_encode(&start_header, &command, 1, io.start.body,
                         sizeof(io.start.body), &io.start.length) != MAC_CODEC_OK)
        return POLL_RETURN(MAC_POLL_INVALID);
    control.request = *request;
    memset((uint8_t *)&control + sizeof(control.request), 0,
           sizeof(control) - sizeof(control.request));
    memcpy(control.outgoing, io.start.body, io.start.length);
    control.outgoing_length = io.start.length;
#else
    /* Preserve the copied request and clear the remaining object representation.
     * There is no mirror cast or assumed host pointer/padding layout. */
    memset((uint8_t *)&control + sizeof(control.request), 0,
           sizeof(control) - sizeof(control.request));
    if (mac_frame_encode(&start_header, &command, 1, control.outgoing,
                         sizeof(control.outgoing), &control.outgoing_length) != MAC_CODEC_OK)
        return POLL_RETURN(MAC_POLL_INVALID);
#endif
    control.version = MAC_POLL_VERSION;
    control.phase = MAC_POLL_ARM;
    control.generation = generation;
    control.owner = tx;
    control.tx_generation = ENGINE(tx)->generation;
    control.last = now;
    control.deadline = control.last + control.request.lifetime;
    control.steps = control.request.work;
    receipt = &p->record;
    memset(receipt, 0, sizeof(*receipt));
    receipt->epoch = control.request.epoch;
    receipt->generation = generation;
    POLL_STORE(p);
    return POLL_RETURN(MAC_POLL_OK);
}

static mac_poll_result_t step(void)
{
    volatile uint8_t kind, observation, status, unbound, response;
    volatile uint32_t remaining;
    if (step_poll == NULL || step_tx == NULL || step_action == NULL || step_profile > MAC_RX_R22_ASSOCIATION_RESPONSE)
        return MAC_POLL_INVALID;
#if defined(CC2530_MAC_LINK_RAM)
    if (!link_ram_disjoint(step_poll, sizeof(*step_poll), step_tx, sizeof(*step_tx)) ||
        !link_ram_disjoint(step_poll, sizeof(*step_poll), step_action, sizeof(*step_action)) ||
        !link_ram_disjoint(step_tx, sizeof(*step_tx), step_action, sizeof(*step_action)) ||
        (step_event && (!link_ram_disjoint(step_poll, sizeof(*step_poll), step_event, sizeof(*step_event)) ||
                        !link_ram_disjoint(step_tx, sizeof(*step_tx), step_event, sizeof(*step_event)) ||
                        !link_ram_disjoint(step_action, sizeof(*step_action), step_event, sizeof(*step_event)))))
        return MAC_POLL_INVALID;
#endif
    if (step_event != NULL) {
        memcpy(&input, step_event, sizeof(input));
        if (input.kind < MAC_POLL_PREPARED || input.kind > MAC_POLL_FAILURE
                || input.crc_valid > 1
                || (input.kind == MAC_POLL_FRAME && (input.body == NULL || !input.serial
                    || input.channel < 11 || input.channel > 26))
#if defined(CC2530_MAC_LINK)
                || (input.kind == MAC_POLL_TX
                    && (input.source.source.kind > MAC_TX_EVENT_RETIRED
                        || input.source.source.kind == MAC_TX_EVENT_SENT
                        || input.source.source.kind == MAC_TX_EVENT_ACK
                        || (input.source.source.kind == MAC_TX_EVENT_ACK_INTERVAL
                            && input.source.source.length && input.source.source.bytes == NULL)
                        || (input.source.source.kind >= MAC_TX_EVENT_SENT_INTERVAL
                            && (input.source.upper.fine >= 512u
                                || !reached(input.source.source.stamp, MAC_LINK_CEIL(input.source.upper))))
                        || ((input.source.source.kind == MAC_TX_EVENT_SENT_INTERVAL
                             || input.source.source.kind == MAC_TX_EVENT_ACK_INTERVAL
                             || input.source.source.kind == MAC_TX_EVENT_BUSY_INTERVAL)
                            && (input.source.lower.fine >= 512u
                                || !reached(input.source.upper.symbols, input.source.lower.symbols)
                                || (input.source.upper.symbols == input.source.lower.symbols
                                    && input.source.upper.fine < input.source.lower.fine))))))
#else
                || (input.kind == MAC_POLL_TX && input.source.kind > MAC_TX_EVENT_CANCEL)
                || (input.kind == MAC_POLL_TX && input.source.kind == MAC_TX_EVENT_ACK
                    && input.source.length && input.source.bytes == NULL))
#endif
            return MAC_POLL_INVALID;
    }
    POLL_LOAD(step_poll);
    if (control.version != MAC_POLL_VERSION || control.phase == MAC_POLL_IDLE
            || control.phase > MAC_POLL_FAULT || control.owner != step_tx)
        return MAC_POLL_STATE;
    receipt = &step_poll->record;
    step_time = step_now;
    kind = 0;
    observation = MAC_POLL_OBS_NONE;
    if (control.phase >= MAC_POLL_DONE)
        goto publish;
    if (!reached(step_now, control.last)) {
        fault(MAC_POLL_CLOCK_ERROR);
        goto publish;
    }
    if (step_event != NULL) {
        observation = MAC_POLL_OBS_STALE;
        if (input.epoch == control.request.epoch && input.generation == control.generation) {
            if (input.kind == MAC_POLL_FRAME && input.serial <= control.rx_serial)
                observation = MAC_POLL_OBS_DUPLICATE;
            else if (reached(input.stamp, control.last) && reached(step_now, input.stamp)) {
                kind = input.kind;
                observation = MAC_POLL_OBS_NONE;
            } else if (input.kind == MAC_POLL_FRAME)
                drain(MAC_POLL_ORDER_ERROR);
        }
    }
    control.last = step_now;
    if (ENGINE(step_tx)->generation != control.tx_generation || (!control.submitted && ENGINE(step_tx)->phase != MAC_TX_IDLE)) {
        fault(MAC_POLL_TX_ERROR);
        goto publish;
    }
    if (control.phase == MAC_POLL_DRAIN) {
        if (reached(step_now, control.stop_at) || !control.stop_steps) {
            fault(MAC_POLL_CLEANUP_FAILED);
            goto publish;
        }
        control.stop_steps--;
    } else if (kind == MAC_POLL_CANCEL || kind == MAC_POLL_FAILURE
            || reached(step_now, control.deadline) || !control.steps)
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
        remaining = control.deadline - step_now;
        if (submit(step_tx, control.outgoing, control.outgoing_length, step_now,
                         remaining, MAC_POLL_TX_STEPS) != MAC_TX_OK)
            drain(MAC_POLL_TX_ERROR);
        else {
            control.submitted = 1;
            control.tx_generation = ENGINE(step_tx)->generation;
            control.phase = MAC_POLL_REQUEST;
        }
    }
    if (kind == MAC_POLL_TX && control.tx_issued && input.token == control.tx_token) {
        control.tx_issued = 0;
        control.tx_outcome = ENGINE(step_tx)->outcome;
        if (input.tx_result != MAC_TX_OK || ENGINE(step_tx)->phase == MAC_TX_IDLE
                || ENGINE(step_tx)->last != input.stamp || ENGINE(step_tx)->phase == MAC_TX_FAULT) {
            drain(MAC_POLL_TX_ERROR);
#if defined(CC2530_MAC_LINK)
        } else if (step_tx->engine.outcome == MAC_TX_TIMING_UNCERTAIN) {
            drain(MAC_POLL_TIMING_UNCERTAIN);
#endif
        } else if (!control.ack_seen && ENGINE(step_tx)->outcome == MAC_TX_ACKED) {
            /* Witness the ORIGINAL event used by the real granted MAC call.
             * Parsing/ignored ACK bits belong to mac_tx, not another decoder.
             */
            if (!accept_ack(step_tx))
                drain(MAC_POLL_TX_ERROR);
        } else if (ENGINE(step_tx)->phase == MAC_TX_DONE && !control.ack_seen && control.phase != MAC_POLL_DRAIN) {
            if (ENGINE(step_tx)->outcome == MAC_TX_NO_ACK || ENGINE(step_tx)->outcome == MAC_TX_CHANNEL_ACCESS)
                decide(ENGINE(step_tx)->outcome == MAC_TX_NO_ACK ? MAC_POLL_NO_ACK : MAC_POLL_CHANNEL_ACCESS,
                       MAC_POLL_TX_RESULT, input.stamp);
            else
                drain(MAC_POLL_TX_ABORT);
        }
    }
    if (kind == MAC_POLL_FRAME) {
        control.rx_serial = input.serial;
        observation = MAC_POLL_OBS_LATE;
#if defined(CC2530_MAC_LINK)
        if (control.phase == MAC_POLL_RECEIVE
                || (control.phase == MAC_POLL_DRAIN && control.timeout_pending)) {
#else
        if (control.phase == MAC_POLL_DRAIN && control.timeout_pending
                && reached(input.stamp, control.ack_end + 1u)
                && reached(control.receive_end, input.stamp))
            drain(MAC_POLL_ORDER_ERROR); /* Timeout poll preceded completed frame. */
        if (control.phase == MAC_POLL_RECEIVE && reached(input.stamp, control.ack_end + 1u)
                && reached(control.receive_end, input.stamp)) {
#endif
            if (input.channel != control.request.channel)
                observation = MAC_POLL_OBS_FOREIGN;
            else if (!input.crc_valid)
                observation = MAC_POLL_OBS_BAD_CRC;
            else {
                status = mac_frame_decode_profile(input.body, input.length, &decoded, step_profile);
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
                    response = decoded.header.type == MAC_FRAME_COMMAND
                        && input.body[decoded.payload_offset] == MAC_COMMAND_ASSOCIATION_RESPONSE;
                    unbound = response && control.request.coordinator_mode == MAC_ADDRESS_SHORT;
                    if ((decoded.header.type != MAC_FRAME_DATA && decoded.header.type != MAC_FRAME_COMMAND)
                            || (decoded.header.destination_pan != control.request.pan
                                && !(step_profile == MAC_RX_R22_ASSOCIATION_RESPONSE && response
                                     && decoded.header.destination_pan == 0xffffu))
                            || decoded.header.source_pan != control.request.pan
                            || decoded.header.destination_mode != control.request.local_mode
                            || memcmp(decoded.header.destination, control.request.local, 8)
                            || (!unbound && (decoded.header.source_mode != control.request.coordinator_mode
                                || memcmp(decoded.header.source, control.request.coordinator, 8))))
                        observation = MAC_POLL_OBS_FOREIGN;
#if defined(CC2530_MAC_LINK)
                    else if (!reached(control.accept_end, input.stamp))
                        drain(MAC_POLL_TIMING_UNCERTAIN);
                    else if (control.timeout_pending)
                        drain(MAC_POLL_ORDER_ERROR);
#endif
                    else {
                        memcpy(receipt->body, input.body, input.length);
                        receipt->length = (uint8_t)input.length;
                        receipt->payload_offset = decoded.payload_offset;
                        receipt->payload_length = decoded.payload_length;
                        receipt->source_relation = unbound ? MAC_POLL_SOURCE_UNBOUND : MAC_POLL_SOURCE_MATCHED;
                        observation = MAC_POLL_OBS_DELIVERY;
                        decide(decoded.header.type == MAC_FRAME_DATA && decoded.payload_length
                                  ? MAC_POLL_SUCCESS : MAC_POLL_NO_DATA,
                               decoded.header.type == MAC_FRAME_COMMAND ? MAC_POLL_COMMAND
                                  : decoded.payload_length ? MAC_POLL_DATA : MAC_POLL_EMPTY,
                               input.stamp);
                    }
                }
            }
        }
    }
    if (control.phase == MAC_POLL_RECEIVE && reached(step_now, control.receive_end)) {
        control.timeout_pending = 1;
        drain(0);
    }
    if (kind == MAC_POLL_CLOSED && control.close_issued && input.token == control.close_token) {
        if (!reached(input.stamp, input.through)
                || (control.timeout_pending && !reached(input.through, control.receive_end))
                || (control.ready && receipt->protocol &&
#if defined(CC2530_MAC_LINK)
                    /* TX_RESULT is backed by MAC CCA/closed-ACK-window
                     * evidence, not RX through its later delivery stamp.
                     * OFF/drain is still required; no post-stop coverage. */
                    receipt->cause != MAC_POLL_TX_RESULT &&
#endif
                    !reached(input.through, receipt->stamp)))
            drain(MAC_POLL_ADAPTER_ERROR);
        else
            control.closed = 1;
    }
    if (control.phase == MAC_POLL_DRAIN && control.closed && !control.tx_issued
            && (!control.submitted || ENGINE(step_tx)->phase == MAC_TX_DONE || ENGINE(step_tx)->phase == MAC_TX_FAULT)) {
        if (control.uncertain || ENGINE(step_tx)->phase == MAC_TX_FAULT)
            fault(MAC_POLL_CLEANUP_FAILED);
        else {
            if (control.timeout_pending)
                decide(MAC_POLL_NO_DATA, MAC_POLL_TIMEOUT, control.receive_end);
            control.phase = MAC_POLL_DONE;
        }
    }
publish:
    /* Early arrivals here are terminal DONE/FAULT only. Consequently they
     * cannot issue any of the ARM / active-TX / DRAIN actions below. */
    memset(&output, 0, sizeof(output));
    if (control.phase == MAC_POLL_ARM && !control.token) {
        output.kind = MAC_POLL_ACTION_PREPARE;
        output.token = ++control.token;
    } else if (control.phase < MAC_POLL_DONE && control.submitted && !control.tx_issued
            && ENGINE(step_tx)->phase != MAC_TX_DONE && ENGINE(step_tx)->phase != MAC_TX_FAULT) {
        control.tx_issued = 1;
        control.tx_phase = ENGINE(step_tx)->phase;
        control.tx_retry = ENGINE(step_tx)->retries;
        control.tx_nb = ENGINE(step_tx)->nb;
        control.tx_mark = ENGINE(step_tx)->last;
        control.tx_token = ++control.token;
        output.token = control.tx_token;
        output.kind = MAC_POLL_ACTION_TX;
        output.tx_cancel = control.phase == MAC_POLL_DRAIN && ENGINE(step_tx)->phase != MAC_TX_STOPPING;
    } else if (control.phase == MAC_POLL_DRAIN && !control.close_issued && !control.tx_issued
            && (!control.submitted || ENGINE(step_tx)->phase == MAC_TX_DONE || ENGINE(step_tx)->phase == MAC_TX_FAULT)) {
        control.close_issued = 1;
        control.close_token = ++control.token;
        output.token = control.close_token;
        output.kind = MAC_POLL_ACTION_CLOSE;
    }
    output.epoch = control.request.epoch;
    output.generation = control.generation;
    output.until = control.phase == MAC_POLL_DRAIN ? control.stop_at : control.deadline;
    output.receive_end = control.receive_end;
    output.phase = control.phase;
    output.observation = observation;
    output.reason = control.reason;
    output.cleanup_error = control.cleanup_error;
    output.ready = control.ready && !control.taken;
    POLL_STORE(step_poll);
    memcpy(step_action, &output, sizeof(output));
    return MAC_POLL_OK;
}

mac_poll_result_t mac_poll_step(mac_poll_t MAC_POLL_RAM * volatile p,
    MAC_POLL_OWNER_T MAC_POLL_RAM * volatile tx, uint32_t volatile now,
    const mac_poll_event_t MAC_POLL_RAM * volatile event,
    mac_poll_action_t MAC_POLL_RAM * volatile action)
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    LW_ENTER(LW_POLL_STEP, MAC_POLL_INVALID);
    if (!LW_IO(LW_POLL_STEP, p, sizeof(*p), 1) ||
        !LW_IO(LW_POLL_STEP, tx, sizeof(*tx), 1) ||
        !LW_IO(LW_POLL_STEP, action, sizeof(*action), 1) ||
        !LW_IO(LW_POLL_STEP, event, event ? sizeof(*event) : 0, 0)) return LW_RETURN(LW_POLL_STEP, MAC_POLL_INVALID);
#endif
    step_poll = p;
    step_tx = tx;
    step_now = now;
    step_event = event;
    step_action = action;
    step_profile = MAC_RX_IEEE2006;
    return POLL_RETURN(step());
}

mac_poll_result_t mac_poll_step_rx(mac_poll_t MAC_POLL_RAM * volatile p,
    MAC_POLL_OWNER_T MAC_POLL_RAM * volatile tx, uint32_t volatile now,
    const mac_poll_event_t MAC_POLL_RAM * volatile event,
    mac_poll_action_t MAC_POLL_RAM * volatile action, uint8_t volatile profile)
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    LW_ENTER(LW_POLL_STEP, MAC_POLL_INVALID);
    if (!LW_IO(LW_POLL_STEP, p, sizeof(*p), 1) ||
        !LW_IO(LW_POLL_STEP, tx, sizeof(*tx), 1) ||
        !LW_IO(LW_POLL_STEP, action, sizeof(*action), 1) ||
        !LW_IO(LW_POLL_STEP, event, event ? sizeof(*event) : 0, 0)) return LW_RETURN(LW_POLL_STEP, MAC_POLL_INVALID);
#endif
    step_poll = p;
    step_tx = tx;
    step_now = now;
    step_event = event;
    step_action = action;
    step_profile = profile;
    return POLL_RETURN(step());
}

mac_poll_result_t mac_poll_take(mac_poll_t MAC_POLL_RAM * volatile p,
    mac_poll_record_t MAC_POLL_RAM * volatile record)
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    LW_ENTER(LW_POLL_OTHER, MAC_POLL_INVALID);
    if (!LW_IO(LW_POLL_OTHER, p, sizeof(*p), 1) ||
        !LW_IO(LW_POLL_OTHER, record, sizeof(*record), 1)) return LW_RETURN(LW_POLL_OTHER, MAC_POLL_INVALID);
#endif
    if (p == NULL || record == NULL)
        return POLL_RETURN(MAC_POLL_INVALID);
#if defined(CC2530_MAC_LINK_RAM)
    if (!link_ram_disjoint(p, sizeof(*p), record, sizeof(*record)))
        return POLL_RETURN(MAC_POLL_INVALID);
#endif
    POLL_LOAD(p);
    if (control.version != MAC_POLL_VERSION || control.phase == MAC_POLL_IDLE || control.phase > MAC_POLL_FAULT
            || !control.ready || control.taken)
        return POLL_RETURN(MAC_POLL_STATE);
    *record = p->record;
    control.taken = 1;
    POLL_STORE(p);
    return POLL_RETURN(MAC_POLL_OK);
}

mac_poll_result_t mac_poll_release(mac_poll_t MAC_POLL_RAM * volatile p,
    MAC_POLL_OWNER_T MAC_POLL_RAM * volatile tx)
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    LW_ENTER(LW_POLL_OTHER, MAC_POLL_INVALID);
    if (!LW_IO(LW_POLL_OTHER, p, sizeof(*p), 1) ||
        !LW_IO(LW_POLL_OTHER, tx, sizeof(*tx), 1)) return LW_RETURN(LW_POLL_OTHER, MAC_POLL_INVALID);
#endif
    if (p == NULL || tx == NULL)
        return POLL_RETURN(MAC_POLL_INVALID);
#if defined(CC2530_MAC_LINK_RAM)
    if (!link_ram_disjoint(p, sizeof(*p), tx, sizeof(*tx)))
        return POLL_RETURN(MAC_POLL_INVALID);
#endif
    POLL_LOAD(p);
    if (control.version != MAC_POLL_VERSION || control.owner != tx || control.phase != MAC_POLL_DONE || !control.taken
            || ENGINE(tx)->generation != control.tx_generation
            || (control.submitted ? ENGINE(tx)->phase != MAC_TX_DONE : ENGINE(tx)->phase != MAC_TX_IDLE))
        return POLL_RETURN(MAC_POLL_STATE);
    if (control.submitted)
        (void)release(tx);
    control.owner = NULL;
    control.phase = MAC_POLL_IDLE;
    POLL_STORE(p);
    return POLL_RETURN(MAC_POLL_OK);
}
