/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef MAC_POLL_H
#define MAC_POLL_H
#include "mac_tx.h"
#ifdef __SDCC
#define MAC_POLL_RAM __xdata
#else
#define MAC_POLL_RAM
#endif

#define MAC_POLL_VERSION 1u
#define MAC_POLL_MAX_WORK 4096u
#define MAC_POLL_MAX_TIME UINT32_C(0x10000000)
#define MAC_POLL_STOP_SYMBOLS 4096u
#define MAC_POLL_STOP_STEPS 64u
#define MAC_POLL_TX_STEPS 512u
#define MAC_POLL_IDLE 0u
#define MAC_POLL_ARM 1u
#define MAC_POLL_REQUEST 2u
#define MAC_POLL_RECEIVE 3u
#define MAC_POLL_DRAIN 4u
#define MAC_POLL_DONE 5u
#define MAC_POLL_FAULT 6u
#define MAC_POLL_ACTION_NONE 0u
#define MAC_POLL_ACTION_PREPARE 1u
#define MAC_POLL_ACTION_TX 2u
#define MAC_POLL_ACTION_CLOSE 3u
#define MAC_POLL_PREPARED 1u
#define MAC_POLL_TX 2u
#define MAC_POLL_FRAME 3u
#define MAC_POLL_CLOSED 4u
#define MAC_POLL_CANCEL 5u
#define MAC_POLL_FAILURE 6u

/* Logical POLL decisions, NOT IEEE enumeration byte values. */
#define MAC_POLL_NO_CONFIRM 0u
#define MAC_POLL_SUCCESS 1u
#define MAC_POLL_NO_DATA 2u
#define MAC_POLL_NO_ACK 3u
#define MAC_POLL_CHANNEL_ACCESS 4u
#define MAC_POLL_DATA 1u
#define MAC_POLL_EMPTY 2u
#define MAC_POLL_COMMAND 3u
#define MAC_POLL_PENDING_ZERO 4u
#define MAC_POLL_TIMEOUT 5u
#define MAC_POLL_TX_RESULT 6u
/* Independent local reasons, never fabricated protocol confirmations. */
#define MAC_POLL_CANCELLED 1u
#define MAC_POLL_LIFETIME 2u
#define MAC_POLL_WORK_LIMIT 3u
#define MAC_POLL_ADAPTER_ERROR 4u
#define MAC_POLL_TX_ERROR 5u
#define MAC_POLL_ORDER_ERROR 6u
#define MAC_POLL_CLOCK_ERROR 7u
#define MAC_POLL_UNSUPPORTED 8u
#define MAC_POLL_CLEANUP_FAILED 9u
#define MAC_POLL_TX_ABORT 10u
/* Per-step observations; ignored input still consumes bounded work. */
#define MAC_POLL_OBS_NONE 0u
#define MAC_POLL_OBS_STALE 1u
#define MAC_POLL_OBS_DUPLICATE 2u
#define MAC_POLL_OBS_BAD_CRC 3u
#define MAC_POLL_OBS_MALFORMED 4u
#define MAC_POLL_OBS_FOREIGN 5u
#define MAC_POLL_OBS_LATE 6u
#define MAC_POLL_OBS_UNSUPPORTED 7u
#define MAC_POLL_OBS_DELIVERY 8u
#define MAC_POLL_SOURCE_UNBOUND 0u
#define MAC_POLL_SOURCE_MATCHED 1u

typedef enum {
    MAC_POLL_OK = 0, MAC_POLL_INVALID, MAC_POLL_STATE, MAC_POLL_LIMIT
} mac_poll_result_t;

/* F is a caller-valid currently configured PIB, NOT an arbitrary timeout or
 * a default derived here. lifetime/work are independent project abort bounds.
 * Address octets are wire order; short tails must be zero, FFFE/FFFF forbidden.
 * Receive destination uses local_mode/local; no implicit address aliases.
 * Association retrieval must use an extended local source.
 */
typedef struct {
    uint32_t epoch, frame_wait, lifetime;
    uint16_t work, pan;
    uint8_t channel, local_mode, coordinator_mode, local[8], coordinator[8];
} mac_poll_request_t;

/* Original frame bytes survive command Pending and input mutation. Taking
 * NO_DATA with COMMAND must still dispatch the command (real #43 for Response).
 * UNBOUND is not an IEEE-to-short binding or authentication.
 * A record is a logical result; reason/cleanup failure remain in step/ctx even
 * if they occur AFTER take. No membership, PIB change or ACK success implied.
 */
typedef struct {
    uint32_t epoch, generation, stamp;
    uint8_t protocol, cause, length, payload_offset, payload_length, source_relation;
    uint8_t body[MAC_FRAME_MAX_BODY];
} mac_poll_record_t;

/* TX reports exactly one granted real mac_tx_step at stamp (foreground time).
 * source is its ORIGINAL input (kind=0 for NULL), including ACK physical end.
 * crc_valid must be true for an ACK source. Do not feed failed CRC to mac_tx.
 * FRAME uses body/length, captured trailing end stamp, channel, crc_valid and
 * a nonzero monotonically increasing receive serial for this lease.
 * CLOSED: through is a loss-free drained receive watermark, <= stamp; all
 * owned RX/ACK activity and applicable local TX IFS are finished/handed off.
 */
typedef struct {
    uint32_t epoch, generation, stamp, serial, through;
    mac_tx_event_t source;
    const uint8_t *body;
    uint16_t length, token;
    uint8_t kind, crc_valid, channel, tx_result;
} mac_poll_event_t;

typedef struct {
    uint32_t epoch, generation, until, receive_end;
    uint16_t token;
    uint8_t kind, tx_cancel, phase, observation, reason, cleanup_error, ready;
} mac_poll_action_t;

/* Also the exact type of the private foreground control staging object.
 * owner is persistent caller storage, not a borrowed payload/pool pointer. */
typedef struct {
    mac_poll_request_t request;
    mac_tx_t MAC_POLL_RAM *owner;
    uint32_t generation, tx_generation, last, deadline, ack_end, receive_end;
    uint32_t stop_at, rx_serial, tx_mark;
    uint16_t steps, token, tx_token, close_token;
    uint8_t outgoing[22], outgoing_length;
    uint8_t version, phase, prepared, submitted, tx_issued, close_issued, closed;
    uint8_t ack_seen, timeout_pending, ready, taken, reason, cleanup_error, uncertain;
    uint8_t stop_steps, tx_phase, tx_retry, tx_nb, tx_outcome;
} mac_poll_control_t;

/* Caller-owned ordinary storage; public fields are read-only diagnostics.
 * Only control is staged. A previously retained receipt is never copied back
 * from scratch or overwritten by a later cancellation/cleanup failure.
 */
typedef struct {
    mac_poll_control_t control;
    mac_poll_record_t record;
} mac_poll_t;

mac_poll_result_t mac_poll_init(mac_poll_t MAC_POLL_RAM * volatile poll);
mac_poll_result_t mac_poll_start(mac_poll_t MAC_POLL_RAM * volatile poll,
    mac_tx_t MAC_POLL_RAM * volatile tx,
    const mac_poll_request_t MAC_POLL_RAM * volatile request, uint32_t volatile now);
mac_poll_result_t mac_poll_step(mac_poll_t MAC_POLL_RAM * volatile poll,
    mac_tx_t MAC_POLL_RAM * volatile tx, uint32_t volatile now,
    const mac_poll_event_t MAC_POLL_RAM * volatile event,
    mac_poll_action_t MAC_POLL_RAM * volatile action);
mac_poll_result_t mac_poll_take(mac_poll_t MAC_POLL_RAM * volatile poll,
    mac_poll_record_t MAC_POLL_RAM * volatile record);
mac_poll_result_t mac_poll_release(mac_poll_t MAC_POLL_RAM * volatile poll,
    mac_tx_t MAC_POLL_RAM * volatile tx);

/* Fresh memory init is never radio recovery. All API errors are atomic.
 * PREPARE establishes a continuous independent poll RX/ACK lease through the
 * real TX operation's eventual confirmed quiescence; it does not weaken that
 * operation's QUIESCED contract or its 1024-symbol/16-step bound.
 * No radio/PAN/channel changes, CRC, timestamp capture or immediate ACK engine
 * are implemented. Current reset-exclusive platform services cannot compose it.
 * step grants one foreground mac_tx_step, never repeats it. Deliver its report
 * before any later-time poll/cancel/frame; no retimestamping. copy is permitted.
 * Frames end in (accepted ACK end, ACK end+F], inclusive at the upper boundary
 * before timeout. Timeout NO_DATA requires CLOSED; elapsed time alone is not
 * coverage. CLOSED retires prior PREPARE/CLOSE rights even after cancellation.
 * Local aborts have no protocol confirmation; established receipts survive
 * later errors. take can be prompt during DRAIN; release requires DONE+taken.
 * FAULT retains ownership. No caller may use/reset the bound tx during a lease.
 * All objects are disjoint, accessible ordinary RAM (never MMIO/compiler
 * scratch/status/IRAM alias). Frame/ACK byte spans may be CODE/RAM and are
 * not retained. Foreground/nonreentrant with codecs, tx and association context.
 * Time is continuous uint32 abstract 16-us symbols, not Sleep Timer ticks;
 * every compared true interval/gap must be <2^31, without hidden wraps.
 */
#endif
