/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef NOISE_HEALTH_H
#define NOISE_HEALTH_H

#include <stdint.h>

#define NOISE_HEALTH_WINDOW 1024u
#define NOISE_HEALTH_WARMUP 1u
#define NOISE_HEALTH_MONITORING 2u
#define NOISE_HEALTH_RCT_FAILED 3u
#define NOISE_HEALTH_APT_FAILED 4u

typedef enum {
    NOISE_HEALTH_OK = 0,
    NOISE_HEALTH_INVALID_ARGUMENT,
    NOISE_HEALTH_INVALID_STATE,
    NOISE_HEALTH_RCT_FAILURE,
    NOISE_HEALTH_APT_FAILURE
} noise_health_result_t;

typedef struct {
    uint16_t rct_cutoff, apt_cutoff;
    uint16_t run, matches, window_count, startup_remaining;
    uint8_t last, reference, state;
} noise_health_t;

/* Binary raw-sample RCT/APT only: OK never means entropy or a secure seed.
 * Cutoffs are explicit diagnostic parameters, NOT qualified source estimates.
 * rct_cutoff is 2..65535; apt_cutoff is 2..1024. No default H/alpha is assumed.
 * Validate wider inputs before conversion to these argument types.
 * start initializes a complete writable context for a NEW stream. It grants
 * no source admission or hardware recovery, including after a health failure.
 * Caller owns the context and must not alter its fields between calls.
 * Foreground/non-reentrant calls; distinct contexts may be interleaved.
 */
noise_health_result_t noise_health_start(noise_health_t *ctx, uint16_t rct_cutoff,
                                         uint16_t apt_cutoff);

/* Consume exactly one raw bit (0 or 1), without packing/filtering/conditioning.
 * Preserve context across chunks. RCT spans APT windows; APT counts the first
 * sample and resets only after each successful 1024-sample window.
 * startup_remaining counts down from 1024, including a failing sample.
 * MONITORING marks successful startup checks; zero alone does not. Neither
 * establishes entropy qualification. No samples are stored or released.
 * First health failure is retained; later valid calls leave context unchanged.
 * RCT takes precedence if both tests fail on the same sample. Invalid arguments
 * or detected invalid state preserve context, including a retained failure.
 */
noise_health_result_t noise_health_push(noise_health_t *ctx, uint8_t sample);

#endif
