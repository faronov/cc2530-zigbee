/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "noise_health.h"

#include <stddef.h>
#include <string.h>

noise_health_result_t noise_health_start(noise_health_t *ctx, uint16_t rct_cutoff,
                                         uint16_t apt_cutoff)
{
    if (ctx == NULL || rct_cutoff < 2 || apt_cutoff < 2 ||
        apt_cutoff > NOISE_HEALTH_WINDOW)
        return NOISE_HEALTH_INVALID_ARGUMENT;
    memset(ctx, 0, sizeof(*ctx));
    ctx->rct_cutoff = rct_cutoff;
    ctx->apt_cutoff = apt_cutoff;
    ctx->startup_remaining = NOISE_HEALTH_WINDOW;
    ctx->state = NOISE_HEALTH_WARMUP;
    return NOISE_HEALTH_OK;
}

noise_health_result_t noise_health_push(noise_health_t *ctx, uint8_t sample)
{
    if (ctx == NULL || sample > 1)
        return NOISE_HEALTH_INVALID_ARGUMENT;
    if (ctx->state == NOISE_HEALTH_RCT_FAILED)
        return NOISE_HEALTH_RCT_FAILURE;
    if (ctx->state == NOISE_HEALTH_APT_FAILED)
        return NOISE_HEALTH_APT_FAILURE;
    if ((ctx->state != NOISE_HEALTH_WARMUP && ctx->state != NOISE_HEALTH_MONITORING) ||
        ctx->rct_cutoff < 2 || ctx->apt_cutoff < 2 || ctx->apt_cutoff > NOISE_HEALTH_WINDOW ||
        ctx->run >= ctx->rct_cutoff || ctx->last > 1 || ctx->reference > 1 ||
        ctx->window_count >= NOISE_HEALTH_WINDOW || ctx->matches > ctx->window_count ||
        ctx->matches >= ctx->apt_cutoff || (ctx->window_count != 0 && ctx->matches == 0) ||
        ctx->startup_remaining > NOISE_HEALTH_WINDOW ||
        (ctx->state == NOISE_HEALTH_WARMUP &&
         (ctx->startup_remaining == 0 ||
          ctx->window_count != NOISE_HEALTH_WINDOW - ctx->startup_remaining ||
          ctx->run > ctx->window_count)) ||
        (ctx->state == NOISE_HEALTH_MONITORING &&
         (ctx->startup_remaining != 0 || ctx->run == 0)))
        return NOISE_HEALTH_INVALID_STATE;

    if (ctx->run != 0 && sample == ctx->last)
        ctx->run++;
    else {
        ctx->last = sample;
        ctx->run = 1;
    }
    if (ctx->window_count == 0) {
        ctx->reference = sample;
        ctx->matches = 1;
    } else if (sample == ctx->reference)
        ctx->matches++;
    ctx->window_count++;
    if (ctx->startup_remaining != 0)
        ctx->startup_remaining--;

    if (ctx->run >= ctx->rct_cutoff) {
        ctx->state = NOISE_HEALTH_RCT_FAILED;
        return NOISE_HEALTH_RCT_FAILURE;
    }
    if (ctx->matches >= ctx->apt_cutoff) {
        ctx->state = NOISE_HEALTH_APT_FAILED;
        return NOISE_HEALTH_APT_FAILURE;
    }
    if (ctx->window_count == NOISE_HEALTH_WINDOW) {
        ctx->window_count = 0;
        ctx->matches = 0;
    }
    if (ctx->startup_remaining == 0)
        ctx->state = NOISE_HEALTH_MONITORING;
    return NOISE_HEALTH_OK;
}
