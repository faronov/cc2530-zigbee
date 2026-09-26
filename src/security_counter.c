/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "security_counter.h"
#include <stddef.h>
#if defined(CC2530_MAC_LINK_WORKSPACE)
#include "mac_link_workspace_guard_internal.h"
#endif

MCU_XDATA security_counter_status_t security_counter_diagnostic;
MCU_XDATA uint8_t security_counter_blob[128], security_counter_check[128];
extern MCU_XDATA uint8_t security_counter_reserved_end;

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
    D.state = SECURITY_COUNTER_FAILED;
    return finish(result);
}

static security_counter_result_t ready(void)
{
    return D.state == SECURITY_COUNTER_FAILED ? (security_counter_result_t)D.result :
        D.state == SECURITY_COUNTER_READY ? SECURITY_COUNTER_OK : SECURITY_COUNTER_STATE;
}

static security_counter_result_t load(uint8_t MCU_XDATA *p)
{
    D.nv_result = (uint8_t)nv_record_load(p, NV_RECORD_MAX);
    if (D.nv_result == NV_RECORD_RECOVERED)
        return fail(SECURITY_COUNTER_RECOVERY);
    if (D.nv_result != NV_RECORD_OK)
        return fail(SECURITY_COUNTER_NV);
    return SECURITY_COUNTER_OK;
}

static security_counter_result_t current(void)
{
    uint8_t i;
    security_counter_result_t result = load(security_counter_check);
    if (result != SECURITY_COUNTER_OK)
        return result;
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
    uint8_t i;
    if (D.state == SECURITY_COUNTER_FAILED)
        return (security_counter_result_t)D.result;
    if (D.state != SECURITY_COUNTER_COLD)
        return SECURITY_COUNTER_STATE;
    D.nv_result = (uint8_t)nv_record_load(security_counter_blob, NV_RECORD_MAX);
    if (D.nv_result == NV_RECORD_EMPTY) {
        D.state = SECURITY_COUNTER_UNPROVISIONED;
        return finish(SECURITY_COUNTER_EMPTY);
    }
    if (D.nv_result != NV_RECORD_OK)
        return fail(D.nv_result == NV_RECORD_RECOVERED ?
                    SECURITY_COUNTER_RECOVERY : SECURITY_COUNTER_NV);
    if (nv_record_status()->length < 16 || security_counter_blob[0] != 'C' ||
        security_counter_blob[1] != 'T' || security_counter_blob[2] != 'R' ||
        security_counter_blob[3] != '1')
        return fail(SECURITY_COUNTER_FORMAT);
    if (security_counter_blob[4] != 1)
        return fail(SECURITY_COUNTER_VERSION);
    if (security_counter_blob[5] > SECURITY_COUNTER_PAYLOAD_MAX ||
        nv_record_status()->length != 16u+security_counter_blob[5] ||
        security_counter_blob[6] || security_counter_blob[7])
        return fail(SECURITY_COUNTER_FORMAT);
    for (i = 0; i < 2; i++)
        D.next[i] = D.until[i] = decode(security_counter_blob+8u+4u*i);
    D.generation = nv_record_status()->generation;
    D.payload_length = security_counter_blob[5];
    D.state = SECURITY_COUNTER_READY;
    return finish(SECURITY_COUNTER_OK);
}

security_counter_result_t security_counter_create(
    uint32_t nwk_floor, uint32_t aps_floor, const uint8_t MCU_XDATA * volatile data,
    uint8_t length, uint16_t poll_limit)
{
    security_counter_result_t result;
    if (D.state == SECURITY_COUNTER_FAILED)
        return (security_counter_result_t)D.result;
    if (D.state != SECURITY_COUNTER_UNPROVISIONED)
        return SECURITY_COUNTER_STATE;
#if defined(CC2530_MAC_LINK_WORKSPACE)
    if (!link_work_counter_request(LW_COUNTER_CREATE, data, length, NULL))
        return SECURITY_COUNTER_OWNERSHIP;
#endif
    if (length > SECURITY_COUNTER_PAYLOAD_MAX || (!data && length) || !poll_limit)
        return SECURITY_COUNTER_ARGUMENT;
    if (length && !caller(data, length))
        return SECURITY_COUNTER_OWNERSHIP;
    D.nv_result = (uint8_t)nv_record_load(security_counter_check, NV_RECORD_MAX);
    if (D.nv_result != NV_RECORD_EMPTY)
        return fail(SECURITY_COUNTER_ROLLBACK);
    security_counter_blob[0] = 'C'; security_counter_blob[1] = 'T';
    security_counter_blob[2] = 'R'; security_counter_blob[3] = '1';
    security_counter_blob[4] = 1; security_counter_blob[6] = security_counter_blob[7] = 0;
    encode(security_counter_blob+8, nwk_floor);
    encode(security_counter_blob+12, aps_floor);
    payload(data, length);
    result = commit(poll_limit);
    if (result == SECURITY_COUNTER_OK) {
        D.next[0] = D.until[0] = nwk_floor;
        D.next[1] = D.until[1] = aps_floor;
    }
    return result;
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
    if (D.next[domain] == D.until[domain]) {
        if (D.until[domain] == 0xffffffffUL)
            return finish(SECURITY_COUNTER_EXHAUSTED);
        result = current();
        if (result != SECURITY_COUNTER_OK)
            return result;
        end = D.until[domain] > 0xffffffffUL-SECURITY_COUNTER_RANGE ?
            0xffffffffUL : D.until[domain]+SECURITY_COUNTER_RANGE;
        encode(security_counter_blob+8u+4u*domain, end);
        result = commit(poll_limit);
        if (result != SECURITY_COUNTER_OK)
            return result;
        D.until[domain] = end;
    }
    taken = D.next[domain];
    D.next[domain]++;
    *value = taken;
    return finish(SECURITY_COUNTER_OK);
}

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
    result = current();
    if (result != SECURITY_COUNTER_OK)
        return result;
    payload(data, length);
    result = commit(poll_limit);
    if (result == SECURITY_COUNTER_OK) {
        D.next[0] = D.until[0];
        D.next[1] = D.until[1];
    }
    return result;
}

security_counter_result_t security_counter_read(
    uint8_t MCU_XDATA * volatile data, uint8_t capacity, uint8_t MCU_XDATA * volatile length)
{
    uint8_t i;
    security_counter_result_t result = ready();
    if (result != SECURITY_COUNTER_OK)
        return result;
#if defined(CC2530_MAC_LINK_WORKSPACE)
    if (!link_work_counter_request(LW_COUNTER_READ, data, capacity, length))
        return SECURITY_COUNTER_OWNERSHIP;
#endif
    if (length == NULL || (data == NULL && capacity))
        return SECURITY_COUNTER_ARGUMENT;
    if (!caller(length, 1) || (capacity && !caller(data, capacity)))
        return SECURITY_COUNTER_OWNERSHIP;
    if (capacity < D.payload_length)
        return SECURITY_COUNTER_SPACE;
    for (i = 0; i < D.payload_length; i++)
        data[i] = security_counter_blob[16u+i];
    *length = D.payload_length;
    return finish(SECURITY_COUNTER_OK);
}

const security_counter_status_t MCU_XDATA *security_counter_status(void)
{
    return &D;
}

MCU_XDATA uint8_t security_counter_reserved_end;
