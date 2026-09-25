/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef SECURITY_COUNTER_H
#define SECURITY_COUNTER_H

#include "nv_record.h"

#define SECURITY_COUNTER_NWK 0u
#define SECURITY_COUNTER_APS 1u
#define SECURITY_COUNTER_RANGE 256u
#define SECURITY_COUNTER_PAYLOAD_MAX 112u

typedef enum {
    SECURITY_COUNTER_OK = 0, SECURITY_COUNTER_EMPTY, SECURITY_COUNTER_ARGUMENT,
    SECURITY_COUNTER_OWNERSHIP, SECURITY_COUNTER_STATE, SECURITY_COUNTER_FORMAT,
    SECURITY_COUNTER_VERSION, SECURITY_COUNTER_RECOVERY, SECURITY_COUNTER_NV,
    SECURITY_COUNTER_ROLLBACK, SECURITY_COUNTER_EXHAUSTED, SECURITY_COUNTER_SPACE,
    SECURITY_COUNTER_PENDING
} security_counter_result_t;

typedef enum {
    SECURITY_COUNTER_COLD = 0, SECURITY_COUNTER_UNPROVISIONED,
    SECURITY_COUNTER_READY, SECURITY_COUNTER_FAILED, SECURITY_COUNTER_BUSY
} security_counter_state_t;

typedef struct {
    uint32_t next[2], until[2], generation;
    uint8_t state, result, nv_result, payload_length;
} security_counter_status_t;

/* Exclusive owner of the real two-page journal, never a second NV writer.
 * Inherit all flash/reset/clock/IRQ/DMA history constraints. Link flash_exec,
 * flash, flash_write, nv_record, this module, then caller storage. All caller
 * objects are complete disjoint stable ordinary XDATA beyond this module's
 * compiler/private fence, below1E00, excluding libc scratch and IRAM aliases.
 * Serialized foreground only, no concurrent observers or callbacks.
 *
 * Persist exclusive ceilings BEFORE allocating a counter. Opening discards
 * every previously reserved but possibly unused counter. NWK has one lifetime
 * domain across keys/networks; APS conservatively shares one domain across all
 * link/derived keys. FFFFFFFF is the exhausted ceiling, never an emitted value.
 * A returned counter is consumed even if later crypto/transmission fails.
 * next is allocation progress, NOT proof of transmission; until is reservation.
 *
 * EMPTY does not prove virgin hardware. Provisioning requires independently
 * established unused identity/key history or trusted nondecreasing floors.
 * Never call it as automatic recovery, leave or factory reset. Those procedures
 * use save to preserve both ceilings, even when clearing the opaque payload.
 * This module has no erase/reset/rewind/recovery-bypass API.
 *
 * Degraded journal selection, unknown versions, unexpected runtime generation/
 * snapshot, and all storage failures retain a failure and forbid allocation.
 * A cold, coherent rollback of BOTH pages or complete erase cannot be detected
 * without an independent trusted anchor. No such hardware anchor is provided.
 * Recovery then requires external history/provisioning, never guessing zero.
 * An NV error can follow a physical commit; no uncertain range is published.
 *
 * Save/read carry opaque upper-layer state, not key validation or membership.
 * No schema migration is implicit. A future migration must retain both ceilings
 * and atomic commit semantics. No physical power-cut/endurance acceptance,
 * cryptography, secure erase, secret diagnostics or successful persistence stub.
 */
security_counter_result_t security_counter_open(void);
security_counter_result_t security_counter_create(
    uint32_t nwk_floor, uint32_t aps_floor, const uint8_t MCU_XDATA * volatile data,
    uint8_t length, uint16_t poll_limit);
security_counter_result_t security_counter_take(
    volatile uint8_t domain, uint32_t MCU_XDATA * volatile value, uint16_t poll_limit);
security_counter_result_t security_counter_save(
    const uint8_t MCU_XDATA * volatile data, uint8_t length, uint16_t poll_limit);
security_counter_result_t security_counter_read(
    uint8_t MCU_XDATA * volatile data, uint8_t capacity, uint8_t MCU_XDATA * volatile length);
const security_counter_status_t MCU_XDATA *security_counter_status(void);

#endif
