/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "mac_epoch.h"
#include <stddef.h>

typedef char fixed_profile[
    MAC_TIME_FINE_PERIOD == 512u && MAC_TIME_OVERFLOW_PERIOD == 0x00ffffffUL ? 1 : -1];

static uint8_t valid(const mac_time_stamp_t MCU_XDATA *raw)
{
    return raw && raw->fine < MAC_TIME_FINE_PERIOD &&
           raw->periods < MAC_TIME_OVERFLOW_PERIOD;
}

mac_epoch_result_t mac_epoch_start(mac_epoch_t MCU_XDATA *ctx,
                                   const mac_time_stamp_t MCU_XDATA *raw, uint32_t symbols)
{
    if (!ctx || !valid(raw)) return MAC_EPOCH_INVALID_ARGUMENT;
    ctx->periods = raw->periods;
    ctx->symbols = symbols;
    ctx->fine = raw->fine;
    ctx->state = MAC_EPOCH_READY;
    return MAC_EPOCH_OK;
}

mac_epoch_result_t mac_epoch_step(mac_epoch_t MCU_XDATA *ctx,
                                  const mac_time_stamp_t MCU_XDATA *raw,
                                  mac_epoch_stamp_t MCU_XDATA *output)
{
    uint32_t periods, whole;
    uint16_t fraction;
    if (!ctx) return MAC_EPOCH_INVALID_ARGUMENT;
    if (ctx->state == MAC_EPOCH_FAULT) return MAC_EPOCH_TIME_ERROR;
    if (!output || !valid(raw)) return MAC_EPOCH_INVALID_ARGUMENT;
    if (ctx->state != MAC_EPOCH_READY || ctx->fine >= MAC_TIME_FINE_PERIOD ||
        ctx->periods >= MAC_TIME_OVERFLOW_PERIOD)
        return MAC_EPOCH_INVALID_STATE;
    periods = raw->periods >= ctx->periods ? raw->periods - ctx->periods :
              MAC_TIME_OVERFLOW_PERIOD - ctx->periods + raw->periods;
    whole = periods;
    if (raw->fine < ctx->fine) {
        if (!whole) goto fault;
        whole--;
        fraction = MAC_TIME_FINE_PERIOD - ctx->fine + raw->fine;
    } else fraction = raw->fine - ctx->fine;
    /* Half of an odd number of periods includes 256 fine increments. */
    if (whole > 0x007fffffUL || (whole == 0x007fffffUL && fraction >= 256u))
        goto fault;
    ctx->symbols += periods;
    ctx->periods = raw->periods;
    ctx->fine = raw->fine;
    output->symbols = ctx->symbols;
    output->fine = ctx->fine;
    return MAC_EPOCH_OK;
fault:
    ctx->state = MAC_EPOCH_FAULT;
    return MAC_EPOCH_TIME_ERROR;
}
