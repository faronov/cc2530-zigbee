/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "security_counter.h"
#if defined(CC2530_MAC_LINK_CHILD_WORKSPACE)
#include "mac_link_child_workspace_internal.h"
#else
#define CW_RETURN(f,r) (r)
#define CW_CALL(f,e) (e)
#define CW_READ_HOME(type,home) (home)
#endif
#include <stddef.h>
/* Every profile: the J3 shallow-read forms are identities outside DIRECT. */
#include "mac_link_workspace_guard_internal.h"
/* DIRECT reloads saved parameters from their homes across calls (J3). */

MCU_XDATA security_counter_status_t security_counter_diagnostic;
#if defined(CC2530_MAC_LINK_CHILD_WORKSPACE)
MCU_XDATA uint8_t security_counter_blob[128];
#define security_counter_check (child_work_arena.nv.counter_check)
#else
MCU_XDATA uint8_t security_counter_blob[128], security_counter_check[128];
#endif
extern MCU_XDATA uint8_t security_counter_reserved_end;

#if defined(CC2530_MAC_LINK_CHILD_WORKSPACE)
#define nv_record_load(...) CW_CALL(CW_RECORD_LOAD,nv_record_load(__VA_ARGS__))
#define nv_record_replace(...) CW_CALL(CW_RECORD_REPLACE,nv_record_replace(__VA_ARGS__))
#endif
#define D security_counter_diagnostic

static uint8_t caller(const void MCU_XDATA *object, uint8_t size)
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    uint8_t permitted = link_work_counter_object(object, size);
    if (permitted != 2) return permitted;
#endif
    uint16_t address = MMIO_XADDRESS(object);
    return address > MMIO_XADDRESS(&security_counter_reserved_end) && address < 0x1e00u &&
        size <= 0x1e00u-address;
}

static uint32_t decode(const uint8_t MCU_XDATA *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void encode(uint8_t MCU_XDATA *p, uint32_t value)
{
    uint8_t i;
    for (i = 0; i < 4; i++) { p[i] = (uint8_t)value; value >>= 8; }
}

static security_counter_result_t finish(security_counter_result_t result)
{
    D.result = result;
    return result;
}

static security_counter_result_t fail(security_counter_result_t result)
{
#if defined(CC2530_MAC_LINK_CHILD_WORKSPACE)
    (void)child_work_poison((uint8_t)result);
#endif
    D.state = SECURITY_COUNTER_FAILED;
    return finish(result);
}

static security_counter_result_t ready(void)
{
    return D.state == SECURITY_COUNTER_FAILED ? (security_counter_result_t)D.result :
        D.state == SECURITY_COUNTER_READY ? SECURITY_COUNTER_OK : SECURITY_COUNTER_STATE;
}

static void floors(void)
{
    uint8_t i;
    for (i = 0; i < 2; i++)
        D.next[i] = D.until[i] = decode(security_counter_blob+8u+4u*i);
}

static security_counter_result_t current(void)
{
    uint8_t i;
    D.nv_result = (uint8_t)nv_record_load(security_counter_check, NV_RECORD_MAX);
    /* Staged creation: the first commit still requires an empty journal. */
    if (!D.generation)
        return D.nv_result == NV_RECORD_EMPTY ? SECURITY_COUNTER_OK :
            fail(SECURITY_COUNTER_ROLLBACK);
    if (D.nv_result == NV_RECORD_RECOVERED)
        return fail(SECURITY_COUNTER_RECOVERY);
    if (D.nv_result != NV_RECORD_OK)
        return fail(SECURITY_COUNTER_NV);
    if (nv_record_status()->generation != D.generation ||
        nv_record_status()->length != 16u+D.payload_length)
        return fail(SECURITY_COUNTER_ROLLBACK);
    for (i = 0; i < 16u+D.payload_length; i++)
        if (security_counter_check[i] != security_counter_blob[i])
            return fail(SECURITY_COUNTER_ROLLBACK);
    return SECURITY_COUNTER_OK;
}

static security_counter_result_t commit(uint16_t poll_limit)
{
    D.state = SECURITY_COUNTER_BUSY;
    D.result = SECURITY_COUNTER_PENDING;
    D.nv_result = NV_RECORD_PENDING;
    D.nv_result = (uint8_t)nv_record_replace(security_counter_blob,
        16u+security_counter_blob[5], poll_limit, 0);
    if (D.nv_result != NV_RECORD_OK)
        return fail(D.nv_result == NV_RECORD_RECOVERY_REQUIRED ?
                    SECURITY_COUNTER_RECOVERY : SECURITY_COUNTER_NV);
    if (nv_record_status()->generation != D.generation+1)
        return fail(SECURITY_COUNTER_ROLLBACK);
    D.generation = nv_record_status()->generation;
    D.payload_length = security_counter_blob[5];
    D.state = SECURITY_COUNTER_READY;
    return finish(SECURITY_COUNTER_OK);
}

static void payload(const uint8_t MCU_XDATA * volatile data, uint8_t length)
{
    uint8_t i;
    security_counter_blob[5] = length;
    for (i = 0; i < SECURITY_COUNTER_PAYLOAD_MAX; i++)
        security_counter_blob[16u+i] = i < length ? data[i] : 0;
}

security_counter_result_t security_counter_open(void)
{
    if (D.state == SECURITY_COUNTER_FAILED)
        return (security_counter_result_t)D.result;
    if (D.state != SECURITY_COUNTER_COLD)
        return SECURITY_COUNTER_STATE;

#if defined(CC2530_MAC_LINK_CHILD_WORKSPACE)
    if (!child_work_enter(CW_COUNTER_OPEN)) return SECURITY_COUNTER_OWNERSHIP;
#endif
D.nv_result = (uint8_t)nv_record_load(security_counter_blob, NV_RECORD_MAX);
    if (D.nv_result == NV_RECORD_EMPTY) {
        D.state = SECURITY_COUNTER_UNPROVISIONED;
        return CW_RETURN(CW_COUNTER_OPEN,finish(SECURITY_COUNTER_EMPTY));
    }
    if (D.nv_result != NV_RECORD_OK)
        return CW_RETURN(CW_COUNTER_OPEN,fail(D.nv_result == NV_RECORD_RECOVERED ?
                    SECURITY_COUNTER_RECOVERY : SECURITY_COUNTER_NV));
    if (nv_record_status()->length < 16 || security_counter_blob[0] != 'C' ||
        security_counter_blob[1] != 'T' || security_counter_blob[2] != 'R' ||
        security_counter_blob[3] != '1')
        return CW_RETURN(CW_COUNTER_OPEN,fail(SECURITY_COUNTER_FORMAT));
    if (security_counter_blob[4] != 1)
        return CW_RETURN(CW_COUNTER_OPEN,fail(SECURITY_COUNTER_VERSION));
    if (security_counter_blob[5] > SECURITY_COUNTER_PAYLOAD_MAX ||
        nv_record_status()->length != 16u+security_counter_blob[5] ||
        security_counter_blob[6] || security_counter_blob[7])
        return CW_RETURN(CW_COUNTER_OPEN,fail(SECURITY_COUNTER_FORMAT));
    floors();
    D.generation = nv_record_status()->generation;
    D.payload_length = security_counter_blob[5];
    D.state = SECURITY_COUNTER_READY;
    return CW_RETURN(CW_COUNTER_OPEN,finish(SECURITY_COUNTER_OK));
}

/* Validates and stages a payload; the floors are the caller's. */
static security_counter_result_t stage(const uint8_t MCU_XDATA * volatile data,
    uint8_t length, uint16_t poll_limit)
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    if (!link_work_counter_request(LW_COUNTER_CREATE, data, length, NULL))
        return SECURITY_COUNTER_OWNERSHIP;
#endif
    if (length > SECURITY_COUNTER_PAYLOAD_MAX || (!data && length) || !poll_limit)
        return SECURITY_COUNTER_ARGUMENT;
    if (length && !caller(data, length))
        return SECURITY_COUNTER_OWNERSHIP;
#if defined(CC2530_MAC_LINK_CHILD_WORKSPACE)
    if (!child_work_enter(CW_COUNTER_CREATE)) return SECURITY_COUNTER_OWNERSHIP;
#endif
    payload(data, length);
    D.payload_length = length;
    D.state = SECURITY_COUNTER_READY;
    return CW_RETURN(CW_COUNTER_CREATE,finish(SECURITY_COUNTER_OK));
}

security_counter_result_t security_counter_create(
    uint32_t nwk_floor, uint32_t aps_floor, const uint8_t MCU_XDATA * volatile data,
    uint8_t length, uint16_t poll_limit)
{
    security_counter_result_t result;
    if (D.state == SECURITY_COUNTER_FAILED)
        return (security_counter_result_t)D.result;
    if (D.state != SECURITY_COUNTER_UNPROVISIONED &&
        (D.state != SECURITY_COUNTER_READY || D.generation))
        return SECURITY_COUNTER_STATE;
    result = stage(data, length, poll_limit);
    if (result != SECURITY_COUNTER_OK)
        return result;
    security_counter_blob[0] = 'C'; security_counter_blob[1] = 'T';
    security_counter_blob[2] = 'R'; security_counter_blob[3] = '1';
    security_counter_blob[4] = 1; security_counter_blob[6] = security_counter_blob[7] = 0;
    encode(security_counter_blob+8, nwk_floor);
    encode(security_counter_blob+12, aps_floor);
    floors();
    return result;
}

security_counter_result_t security_counter_restage(
    const uint8_t MCU_XDATA * volatile data, uint8_t length)
{
    if (D.state != SECURITY_COUNTER_READY || D.generation)
        return SECURITY_COUNTER_STATE;
    return stage(data, length, 1);
}

security_counter_result_t security_counter_take(
    volatile uint8_t domain, uint32_t MCU_XDATA * volatile value, uint16_t poll_limit)
{
    volatile uint32_t end;
    uint32_t taken;
    security_counter_result_t result = ready();
    if (result != SECURITY_COUNTER_OK)
        return result;
#if defined(CC2530_MAC_LINK_WORKSPACE)
    if (!link_work_counter_request(LW_COUNTER_TAKE, value, 4, NULL))
        return SECURITY_COUNTER_OWNERSHIP;
#endif
    if (domain > 1 || value == NULL || !poll_limit)
        return SECURITY_COUNTER_ARGUMENT;
    if (!caller(value, 4))
        return SECURITY_COUNTER_OWNERSHIP;

#if defined(CC2530_MAC_LINK_CHILD_WORKSPACE)
    if (!child_work_enter(CW_COUNTER_TAKE)) return SECURITY_COUNTER_OWNERSHIP;
#endif
if (D.next[domain] == D.until[domain]) {
        if (D.until[domain] == 0xffffffffUL)
            return CW_RETURN(CW_COUNTER_TAKE,finish(SECURITY_COUNTER_EXHAUSTED));
        result = current();
        if (result != SECURITY_COUNTER_OK)
            return CW_RETURN(CW_COUNTER_TAKE,result);
        end = D.until[domain] > 0xffffffffUL-SECURITY_COUNTER_RANGE ?
            0xffffffffUL : D.until[domain]+SECURITY_COUNTER_RANGE;
        encode(security_counter_blob+8u+4u*domain, end);
        result = commit(poll_limit);
        if (result != SECURITY_COUNTER_OK)
            return CW_RETURN(CW_COUNTER_TAKE,result);
        D.until[domain] = end;
    }
    taken = D.next[domain];
    D.next[domain]++;
    *value = taken;
    return CW_RETURN(CW_COUNTER_TAKE,finish(SECURITY_COUNTER_OK));
}
#define commit(limit) commit(LW_SHALLOW_READ(uint16_t, limit))
security_counter_result_t security_counter_save(
    const uint8_t MCU_XDATA * volatile data, uint8_t length, uint16_t poll_limit)
{
    security_counter_result_t result = ready();
    if (result != SECURITY_COUNTER_OK)
        return result;
#if defined(CC2530_MAC_LINK_WORKSPACE)
    if (!link_work_counter_request(LW_COUNTER_SAVE, data, length, NULL))
        return SECURITY_COUNTER_OWNERSHIP;
#endif
    if (length > SECURITY_COUNTER_PAYLOAD_MAX || (!data && length) || !poll_limit)
        return SECURITY_COUNTER_ARGUMENT;
    if (length && !caller(data, length))
        return SECURITY_COUNTER_OWNERSHIP;
#define payload(d, n) payload(d, LW_SHALLOW_READ(uint8_t, n))
#if defined(CC2530_MAC_LINK_CHILD_WORKSPACE)
    if (!child_work_enter(CW_COUNTER_SAVE)) return SECURITY_COUNTER_OWNERSHIP;
#endif
result = current();
    if (result != SECURITY_COUNTER_OK)
        return CW_RETURN(CW_COUNTER_SAVE,result);
    payload(data, length);
    result = commit(poll_limit);
    if (result == SECURITY_COUNTER_OK) {
        D.next[0] = D.until[0];
        D.next[1] = D.until[1];
    }
    return CW_RETURN(CW_COUNTER_SAVE,result);
}

security_counter_result_t security_counter_read(
    uint8_t MCU_XDATA * volatile data, uint8_t capacity, uint8_t MCU_XDATA * volatile length)
{
    uint8_t i;
    security_counter_result_t result = ready();
    if (result != SECURITY_COUNTER_OK)
        return result;
#if defined(CC2530_MAC_LINK_WORKSPACE)
    if (!link_work_counter_request(LW_COUNTER_READ, data, CW_READ_HOME(uint8_t,capacity), length))
        return SECURITY_COUNTER_OWNERSHIP;
#endif
    if (length == NULL || (data == NULL && CW_READ_HOME(uint8_t,capacity)))
        return SECURITY_COUNTER_ARGUMENT;
    if (!caller(length, 1) ||
        (CW_READ_HOME(uint8_t,capacity) && !caller(data, CW_READ_HOME(uint8_t,capacity))))
        return SECURITY_COUNTER_OWNERSHIP;
    if (CW_READ_HOME(uint8_t,capacity) < D.payload_length)
        return SECURITY_COUNTER_SPACE;

#if defined(CC2530_MAC_LINK_CHILD_WORKSPACE)
    if (!child_work_disjoint(data,CW_READ_HOME(uint8_t,capacity),length,1)) return SECURITY_COUNTER_OWNERSHIP;
    if (!child_work_enter(CW_COUNTER_READ)) return SECURITY_COUNTER_OWNERSHIP;
#endif
for (i = 0; i < D.payload_length; i++)
        data[i] = security_counter_blob[16u+i];
    *length = D.payload_length;
    return CW_RETURN(CW_COUNTER_READ,finish(SECURITY_COUNTER_OK));
}

const security_counter_status_t MCU_XDATA *security_counter_status(void)
{
    return &D;
}

MCU_XDATA uint8_t security_counter_reserved_end;
