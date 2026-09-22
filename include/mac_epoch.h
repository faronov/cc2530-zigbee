/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef MAC_EPOCH_H
#define MAC_EPOCH_H

#include "mac_time.h"

#define MAC_EPOCH_READY 1u
#define MAC_EPOCH_FAULT 2u

typedef enum {
    MAC_EPOCH_OK = 0,
    MAC_EPOCH_INVALID_ARGUMENT,
    MAC_EPOCH_INVALID_STATE,
    MAC_EPOCH_TIME_ERROR
} mac_epoch_result_t;

typedef struct {
    uint32_t symbols;
    uint16_t fine;
} mac_epoch_stamp_t;

typedef struct {
    uint32_t periods, symbols;
    uint16_t fine;
    uint8_t state;
} mac_epoch_t;

/* Arithmetic only, no MMIO. One foreground owner; disjoint complete objects
 * in caller-owned persistent ordinary XDATA, outside compiler/runtime scratch.
 * raw must be a coherent tuple from the fixed mac_time profile, with real
 * uninterrupted clock history. Numeric validity does not establish that.
 * start binds raw's containing period to symbols; it does NOT set the sample's
 * fractional phase to zero. Fine remains 0..511; periods wraps at FFFFFF,
 * not 1000000. Contexts retain no pointers and can be moved/interleaved.
 *
 * start initializes a NEW caller epoch, not timer/radio recovery. Purge all
 * old events/users before restarting, including after a retained time error.
 * Invalid start arguments preserve the entire context.
 */
mac_epoch_result_t mac_epoch_start(mac_epoch_t MCU_XDATA *ctx,
                                   const mac_time_stamp_t MCU_XDATA *raw, uint32_t symbols);

/* True time between consecutive accepted samples must be strictly below
 * half the raw tuple period: 0xFFFFFF00 fine increments (nominally134.21772s).
 * Equality, backward or ambiguous numeric progress faults the context.
 * Missed full wraps, reset and a foreign writer cannot always be detected:
 * callers must establish continuity independently, not infer it from OK.
 *
 * Repeated identical samples are valid, but do not prove clock progress.
 * Successful output preserves fine exactly and wraps symbols modulo2^32.
 * It is NOT a captured PHY end, rounded mac_tx timestamp or calibrated time.
 * Invalid arguments/state preserve context and output. A time error changes
 * only state to FAULT; output and last accepted coordinates remain unchanged.
 * Retained FAULT wins over raw/output errors; only ctx must be non-NULL.
 */
mac_epoch_result_t mac_epoch_step(mac_epoch_t MCU_XDATA *ctx,
                                  const mac_time_stamp_t MCU_XDATA *raw,
                                  mac_epoch_stamp_t MCU_XDATA *output);

#endif
