/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef MAC_ASSOCIATION_H
#define MAC_ASSOCIATION_H

#include <stdint.h>
#include "mac_frame.h"

#ifdef __SDCC
#define MAC_ASSOCIATION_RAM __xdata
#else
#define MAC_ASSOCIATION_RAM
#endif

#define MAC_ASSOCIATION_VERSION 1u
#define MAC_ASSOCIATION_HALF UINT32_C(0x80000000)
/* Project context bounds, NOT macResponseWaitTime or a complete procedure. */
#define MAC_ASSOCIATION_MAX_LIFETIME UINT32_C(65535)
#define MAC_ASSOCIATION_MAX_WORK 4096u
#define MAC_ASSOCIATION_IDLE 0u
#define MAC_ASSOCIATION_WAIT 1u
#define MAC_ASSOCIATION_DONE 2u
#define MAC_ASSOCIATION_FRAME 1u
#define MAC_ASSOCIATION_CANCEL 2u

/* step observations; only RESPONSE/EXPIRED/EXHAUSTED/CANCELLED are terminal. */
#define MAC_ASSOCIATION_WAITING 0u
#define MAC_ASSOCIATION_RESPONSE 1u
#define MAC_ASSOCIATION_BAD_CRC 2u
#define MAC_ASSOCIATION_MALFORMED 3u
#define MAC_ASSOCIATION_MISMATCH 4u
#define MAC_ASSOCIATION_STALE 5u
#define MAC_ASSOCIATION_EXPIRED 6u
#define MAC_ASSOCIATION_EXHAUSTED 7u
#define MAC_ASSOCIATION_CANCELLED 8u
#define MAC_ASSOCIATION_SOURCE_UNBOUND 0u
#define MAC_ASSOCIATION_SOURCE_MATCHED 1u
#define MAC_ASSOCIATION_ALLOCATED 1u
#define MAC_ASSOCIATION_EXTENDED_ONLY 2u
#define MAC_ASSOCIATION_REFUSED 3u

typedef enum {
    MAC_ASSOCIATION_OK = 0,
    MAC_ASSOCIATION_INVALID,
    MAC_ASSOCIATION_STATE,
    MAC_ASSOCIATION_GENERATION_LIMIT
} mac_association_result_t;

/* Copied selected-coordinator context, not parent selection or authorization.
 * Short coordinator: address[0..1] is wire order, [2..7] must be zero.
 * Extended coordinator: all eight wire octets must match a Response source.
 * Short selection CANNOT verify the Response's source IEEE; UNBOUND reports
 * that limitation. Extended PAN ID is never used as a coordinator identity.
 * Local IEEE/selection/channel/PAN must be truthful caller-owned facts.
 */
typedef struct {
    uint32_t epoch, lifetime;
    uint16_t work_limit, pan_id;
    uint8_t channel, coordinator_mode;
    uint8_t coordinator[8], local[8];
} mac_association_request_t;

/* FRAME: complete FCS-free body, truthful CRC indication, selected radio epoch
 * and actual captured trailing-PPDU-end stamp. No PHY CRC/timestamp is produced
 * here. CANCEL uses correlation only; other fields are ignored.
 * Body is a generic read-only CODE/RAM span, never retained after step.
 */
typedef struct {
    uint32_t epoch, generation, stamp;
    const uint8_t *body;
    uint16_t length;
    uint8_t kind, channel, crc_valid;
} mac_association_event_t;

/* RESPONSE means contextual metadata received, NOT MAC association success.
 * For other outcomes only epoch/generation/stamp/outcome are meaningful.
 * Response status/address are the codec's raw MAC values (including FFFE);
 * address_kind is MAC interpretation, NOT Zigbee ED address admission.
 * SOURCE_MATCHED is byte equality to caller selection, not authentication.
 * SOURCE_UNBOUND is an observed IEEE, not verified binding to a short address.
 * sequence is the coordinator's DSN, NOT the Association Request DSN.
 * Every Response requires receiver ACK independently of this foreground filter.
 * No ACK generation, timing, delivery or quiescence is implied by this record.
 */
typedef struct {
    uint32_t epoch, generation, stamp;
    uint16_t short_address;
    uint8_t source_ieee[8];
    uint8_t outcome, status, address_kind, source_relation, sequence;
} mac_association_record_t;

/* Caller-owned ordinary RAM/XDATA; public fields are read-only diagnostics.
 * No heap, borrowed pool pointers, MMIO or real-radio ownership.
 */
typedef struct {
    mac_association_request_t request;
    mac_association_record_t record;
    uint32_t generation, opened, last;
    uint16_t remaining;
    uint8_t version, phase;
} mac_association_t;

/* Fresh storage/queued-event epoch only; never MLME reset or radio recovery. */
mac_association_result_t mac_association_init(mac_association_t MAC_ASSOCIATION_RAM *ctx,
                                               uint32_t now);
/* Start only after init or take. Caller establishes a real association Response
 * window, current PAN/channel/local address and independent timely ACK service.
 * No Request/Data Request is transmitted, no PIB is changed. Nonzero caller
 * epoch plus nonwrapping local generation correlates local events, not on-air
 * replay. Lifetime is [now,now+lifetime), with an inclusive opening boundary.
 */
mac_association_result_t mac_association_start(mac_association_t MAC_ASSOCIATION_RAM *ctx,
                    const mac_association_request_t MAC_ASSOCIATION_RAM *request,
                    uint32_t now);
/* NULL event polls. Valid calls consume finite work, including ignored frames.
 * API errors preserve ctx/observation. OK means processed: inspect observation.
 * Expiry precedes input; at/beyond deadline even an earlier captured frame is
 * too late. Frame stamps must be between previous watermark and now inclusive.
 * Consume completed events before later-time polls/cancellation. No retimestamp.
 * Last allowed step may record/cancel; otherwise it terminates EXHAUSTED.
 */
mac_association_result_t mac_association_step(mac_association_t MAC_ASSOCIATION_RAM *ctx,
                    uint32_t now,
                    const mac_association_event_t MAC_ASSOCIATION_RAM *event,
                    uint8_t MAC_ASSOCIATION_RAM *observation);
/* One-shot copy of a terminal record, then IDLE; generation/time are preserved.
 * Taking/cancelling a memory context does NOT stop/release a radio, transmitter
 * lease or outstanding ACK. Caller must separately finish confirmed cleanup.
 */
mac_association_result_t mac_association_take(mac_association_t MAC_ASSOCIATION_RAM *ctx,
                    mac_association_record_t MAC_ASSOCIATION_RAM *record);

/* All records are disjoint ordinary caller storage, never compiler scratch,
 * MMIO, status, IRAM alias or writable CODE. Non-NULL is not pointer validation.
 * Foreground/nonreentrant, serialized with all mac_frame/mac_command calls.
 * now uses abstract uint32 16-us symbols, not raw Sleep Timer ticks; consecutive
 * observations differ by <HALF and no hidden wraps. Invalid calls do not bound
 * progress; caller must service time/work or cancel. No membership/PIB changes,
 * automatic retrieval, authentication, real adapter or restoration is supplied.
 */
#endif
