/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef MAC_LINK_WORKSPACE_GUARD_INTERNAL_H
#define MAC_LINK_WORKSPACE_GUARD_INTERNAL_H
#include "mac_link_ram.h"
#if defined(CC2530_MAC_LINK_WORKSPACE)
#include "cc2530_mmio.h"

/* Operation identities, not general-purpose allocation handles. */
enum {
    LW_NONE, LW_KEYS, LW_JOIN_INIT, LW_JOIN_START, LW_JOIN_STEP, LW_JOIN_OTHER,
    LW_POLL_INIT, LW_POLL_START, LW_POLL_STEP, LW_POLL_OTHER,
    LW_TX_SUBMIT, LW_TX_STEP, LW_TX_INTERVAL, LW_TX_OBSERVED,
    LW_MAC_ENCODE, LW_MAC_DECODE, LW_BEACON,
    LW_ASSOCIATION, LW_SCAN, LW_PARENT, LW_NWK_HINT, LW_NWK_BUILD, LW_NWK_CANCEL,
    LW_ZDO_INIT, LW_ZDO_RX, LW_ZDO_SERVER, LW_ZDO_DECODE, LW_ZDO_ENCODE,
    LW_NWK_BEACON, LW_FRAMES
};
enum {
    LW_NO_GRANT, LW_COUNTER_READ, LW_COUNTER_CREATE, LW_COUNTER_SAVE, LW_COUNTER_TAKE,
    LW_WIRE_NWK, LW_WIRE_DECODE, LW_WIRE_ENCODE, LW_WIRE_INSPECT, LW_WIRE_CRYPT,
    LW_HASH_KEY, LW_HASH_VERIFY, LW_INSTALL_CODE, LW_HINT
};
enum {
    LW_CHILD_WIRE_NWK = 64, LW_CHILD_WIRE_APS, LW_CHILD_WIRE_DECODE,
    LW_CHILD_WIRE_ENCODE, LW_CHILD_WIRE_INSPECT, LW_CHILD_WIRE_CRYPT,
    LW_CHILD_CCM, LW_CHILD_HASH, LW_CHILD_MMO, LW_CHILD_INSTALL,
    LW_CHILD_NWK, LW_CHILD_APS, LW_CHILD_COMMAND
};

uint8_t link_work_enter(uint8_t frame);
uint8_t link_work_leave(uint8_t frame);
/* Preserve both original uint8_t conversions and evaluate each argument once.
 * The volatile word home avoids register-save PUSHes across leave in SDCC4.2
 * model-large. All WORKSPACE callers must use this same internal ABI. */
uint8_t link_work_return_word(uint16_t volatile pair);
#define link_work_return(frame,result) link_work_return_word( \
    (uint16_t)(((uint16_t)(uint8_t)(frame) << 8) | (uint8_t)(result)))
uint8_t link_work_finish_group(uint8_t first, uint8_t last, uint8_t result);
uint8_t link_work_io(uint8_t operation, const void * volatile p, uint16_t volatile size, uint8_t writing);
uint8_t link_work_external(const void * volatile p, uint16_t volatile size);
uint8_t link_work_grant(uint8_t grant);
uint8_t link_work_call_result(uint8_t result);
uint8_t link_work_counter_request(uint8_t operation, const void *data,
    uint16_t size, const void *length);
/* 0 rejects arena access, 1 authorizes a precise checked handoff, 2 means
 * ordinary non-arena storage: the lower owner's original fence still applies. */
uint8_t link_work_counter_object(const void *p, uint8_t size);
uint8_t link_work_clean(void);
uint8_t link_work_root(void);
#if defined(CC2530_HOST_TEST)
void link_work_host_reset(void);
#endif
#define LW_ENTER(frame, error) do { if (!link_work_enter(frame)) return (error); } while (0)
#define LW_RETURN(frame, result) link_work_return((frame), (result))
#define LW_LEAVE(frame) link_work_leave(frame)
#define LW_ROOT(error) do { if (!link_work_root()) return (error); } while (0)
#define LW_IO(op, p, n, wr) link_work_io((op), (p), (n), (wr))
#define LW_CALL(grant, expression) \
    (link_work_grant(grant) ? link_work_call_result(expression) : 255u)
#else
#define LW_ENTER(frame, error) ((void)0)
#define LW_RETURN(frame, result) (result)
#define LW_LEAVE(frame) 1
#define LW_ROOT(error) ((void)0)
#define LW_IO(op, p, n, wr) 1
#define LW_CALL(grant, expression) (expression)
#endif
#endif
