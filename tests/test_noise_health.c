/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "noise_health.h"
#include "cc2530_mmio.h"

#include <stddef.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) return (uint16_t)__LINE__; } while (0)

static uint16_t self_test(void)
{
    noise_health_t ctx, other, saved;
    uint16_t i;

    memset(&ctx, 0xa5, sizeof(ctx));
    memcpy(&saved, &ctx, sizeof(ctx));
    CHECK(noise_health_start(NULL, 21, 589) == NOISE_HEALTH_INVALID_ARGUMENT);
    CHECK(noise_health_start(&ctx, 0, 589) == NOISE_HEALTH_INVALID_ARGUMENT);
    CHECK(noise_health_start(&ctx, 1, 589) == NOISE_HEALTH_INVALID_ARGUMENT);
    CHECK(noise_health_start(&ctx, 21, 0) == NOISE_HEALTH_INVALID_ARGUMENT);
    CHECK(noise_health_start(&ctx, 21, 1) == NOISE_HEALTH_INVALID_ARGUMENT);
    CHECK(noise_health_start(&ctx, 21, 1025) == NOISE_HEALTH_INVALID_ARGUMENT);
    CHECK(noise_health_start(&ctx, 21, 65535u) == NOISE_HEALTH_INVALID_ARGUMENT);
    CHECK(memcmp(&ctx, &saved, sizeof(ctx)) == 0);
    CHECK(noise_health_push(NULL, 0) == NOISE_HEALTH_INVALID_ARGUMENT);
    CHECK(noise_health_push(&ctx, 0) == NOISE_HEALTH_INVALID_STATE);
    CHECK(memcmp(&ctx, &saved, sizeof(ctx)) == 0);
    CHECK(noise_health_start(&ctx, 21, 589) == NOISE_HEALTH_OK);
    memcpy(&saved, &ctx, sizeof(ctx));
    for (i = 2; i < 256; i++) {
        CHECK(noise_health_push(&ctx, (uint8_t)i) == NOISE_HEALTH_INVALID_ARGUMENT);
        CHECK(memcmp(&ctx, &saved, sizeof(ctx)) == 0);
    }
    for (i = 0; i < 20; i++)
        CHECK(noise_health_push(&ctx, 0) == NOISE_HEALTH_OK);
    CHECK(ctx.run == 20 && ctx.matches == 20 && ctx.startup_remaining == 1004);
    CHECK(noise_health_push(&ctx, 0) == NOISE_HEALTH_RCT_FAILURE);
    CHECK(ctx.run == 21 && ctx.matches == 21 && ctx.state == NOISE_HEALTH_RCT_FAILED);
    memcpy(&saved, &ctx, sizeof(ctx));
    CHECK(noise_health_push(&ctx, 1) == NOISE_HEALTH_RCT_FAILURE);
    CHECK(noise_health_push(&ctx, 2) == NOISE_HEALTH_INVALID_ARGUMENT);
    CHECK(memcmp(&ctx, &saved, sizeof(ctx)) == 0);

    CHECK(noise_health_start(&ctx, 65535u, 589) == NOISE_HEALTH_OK);
    for (i = 0; i < 588; i++)
        CHECK(noise_health_push(&ctx, 1) == NOISE_HEALTH_OK);
    CHECK(noise_health_push(&ctx, 0) == NOISE_HEALTH_OK);
    CHECK(ctx.matches == 588 && ctx.run == 1 && ctx.window_count == 589);
    CHECK(noise_health_push(&ctx, 1) == NOISE_HEALTH_APT_FAILURE);
    CHECK(ctx.matches == 589 && ctx.run == 1 && ctx.window_count == 590);
    memcpy(&saved, &ctx, sizeof(ctx));
    CHECK(noise_health_push(&ctx, 0) == NOISE_HEALTH_APT_FAILURE);
    CHECK(memcmp(&ctx, &saved, sizeof(ctx)) == 0);

    CHECK(noise_health_start(&ctx, 65535u, 589) == NOISE_HEALTH_OK);
    for (i = 0; i < 1023; i++)
        CHECK(noise_health_push(&ctx, (uint8_t)(i >= 588)) == NOISE_HEALTH_OK);
    CHECK(ctx.startup_remaining == 1 && ctx.window_count == 1023 && ctx.matches == 588);
    CHECK(ctx.state == NOISE_HEALTH_WARMUP);
    CHECK(noise_health_push(&ctx, 1) == NOISE_HEALTH_OK);
    CHECK(ctx.startup_remaining == 0 && ctx.window_count == 0 && ctx.matches == 0);
    CHECK(ctx.state == NOISE_HEALTH_MONITORING && ctx.run == 436);
    CHECK(noise_health_push(&ctx, 1) == NOISE_HEALTH_OK);
    CHECK(ctx.reference == 1 && ctx.matches == 1 && ctx.run == 437);
    for (i = 1; i < 1024; i++)
        CHECK(noise_health_push(&ctx, (uint8_t)(i < 588)) == NOISE_HEALTH_OK);
    CHECK(ctx.window_count == 0 && ctx.matches == 0 && ctx.run == 436);

    CHECK(noise_health_start(&ctx, 5, 1024) == NOISE_HEALTH_OK);
    for (i = 0; i < 1020; i++)
        CHECK(noise_health_push(&ctx, (uint8_t)(i & 1u)) == NOISE_HEALTH_OK);
    for (i = 0; i < 4; i++)
        CHECK(noise_health_push(&ctx, 0) == NOISE_HEALTH_OK);
    CHECK(ctx.window_count == 0 && ctx.run == 4);
    CHECK(noise_health_push(&ctx, 0) == NOISE_HEALTH_RCT_FAILURE);
    CHECK(ctx.run == 5 && ctx.window_count == 1 && ctx.matches == 1);

    CHECK(noise_health_start(&ctx, 21, 589) == NOISE_HEALTH_OK);
    CHECK(noise_health_start(&other, 2, 2) == NOISE_HEALTH_OK);
    for (i = 0; i < 4096; i++) {
        CHECK(noise_health_push(&ctx, (uint8_t)(i & 1u)) == NOISE_HEALTH_OK);
        if (i == 123 || i == 456) {
            memcpy(&saved, &ctx, sizeof(ctx));
            CHECK(noise_health_push(&other, 1) ==
                  (i == 123 ? NOISE_HEALTH_OK : NOISE_HEALTH_RCT_FAILURE));
            CHECK(memcmp(&ctx, &saved, sizeof(ctx)) == 0);
        }
    }
    CHECK(ctx.state == NOISE_HEALTH_MONITORING && ctx.startup_remaining == 0);
    CHECK(ctx.window_count == 0 && ctx.matches == 0 && ctx.run == 1);
    CHECK(other.state == NOISE_HEALTH_RCT_FAILED && other.matches == 2);

    CHECK(noise_health_start(&ctx, 65535u, 1024) == NOISE_HEALTH_OK);
    for (i = 0; i < 1023; i++)
        CHECK(noise_health_push(&ctx, 0) == NOISE_HEALTH_OK);
    CHECK(noise_health_push(&ctx, 0) == NOISE_HEALTH_APT_FAILURE);
    CHECK(ctx.run == 1024 && ctx.matches == 1024 && ctx.window_count == 1024);
    CHECK(ctx.startup_remaining == 0 && ctx.state == NOISE_HEALTH_APT_FAILED);
    memcpy(&saved, &ctx, sizeof(ctx));
    CHECK(noise_health_push(&ctx, 1) == NOISE_HEALTH_APT_FAILURE);
    CHECK(memcmp(&ctx, &saved, sizeof(ctx)) == 0);
    return 0;
}

#if defined(__SDCC)
typedef char context_size_check[sizeof(noise_health_t) == 15 ? 1 : -1];
volatile MCU_XDATA MCU_AT(0x1e00) uint8_t noise_health_test_result[8];

void main(void)
{
    uint16_t result = self_test();
    noise_health_test_result[0] = 'N';
    noise_health_test_result[1] = 'O';
    noise_health_test_result[2] = 'H';
    noise_health_test_result[3] = '1';
    noise_health_test_result[4] = 1;
    noise_health_test_result[5] = 8;
    noise_health_test_result[6] = (uint8_t)result;
    noise_health_test_result[7] = (uint8_t)(result >> 8);
    __asm
        .globl _noise_health_test_done
    _noise_health_test_done:
        nop
    __endasm;
    for (;;) {
    }
}
#else
#include <stdio.h>
#include <stdlib.h>

static noise_health_result_t oracle(const uint8_t *bits, unsigned length,
                                     unsigned rct, unsigned apt, unsigned *failed_at)
{
    unsigned i, j, run, matches, start;
    *failed_at = length;
    for (i = 0; i < length; i++) {
        run = 1;
        for (j = i; j > 0 && bits[j - 1] == bits[i]; j--)
            run++;
        start = i / 1024 * 1024;
        matches = 0;
        for (j = start; j <= i; j++)
            matches += bits[j] == bits[start];
        if (run >= rct) {
            *failed_at = i;
            return NOISE_HEALTH_RCT_FAILURE;
        }
        if (matches >= apt) {
            *failed_at = i;
            return NOISE_HEALTH_APT_FAILURE;
        }
    }
    return NOISE_HEALTH_OK;
}

static uint16_t host_corpus(void)
{
    noise_health_t ctx, saved;
    noise_health_t *exact;
    uint8_t bits[2049];
    unsigned sequence, i, rct, apt, failed_at;
    uint32_t word = 1;
    noise_health_result_t expected;

    for (sequence = 0; sequence < 4096; sequence++) {
        rct = 2 + sequence % 12;
        apt = 2 + sequence / 16 % 12;
        CHECK(noise_health_start(&ctx, (uint16_t)rct, (uint16_t)apt) == NOISE_HEALTH_OK);
        for (i = 0; i < 12; i++) {
            bits[i] = (uint8_t)((sequence >> i) & 1u);
            expected = oracle(bits, i + 1, rct, apt, &failed_at);
            memcpy(&saved, &ctx, sizeof(ctx));
            CHECK(noise_health_push(&ctx, bits[i]) == expected);
            if (saved.state >= NOISE_HEALTH_RCT_FAILED)
                CHECK(memcmp(&ctx, &saved, sizeof(ctx)) == 0);
        }
    }
    for (sequence = 0; sequence < 32; sequence++) {
        rct = sequence & 1u ? 21 : 65535u;
        apt = sequence & 2u ? 589 : 1024;
        for (i = 0; i < sizeof(bits); i++) {
            word = word * 1664525UL + 1013904223UL;
            bits[i] = (uint8_t)((word >> 24) < 64 + 4 * sequence);
        }
        expected = oracle(bits, sizeof(bits), rct, apt, &failed_at);
        CHECK(noise_health_start(&ctx, (uint16_t)rct, (uint16_t)apt) == NOISE_HEALTH_OK);
        for (i = 0; i < sizeof(bits); i++)
            CHECK(noise_health_push(&ctx, bits[i]) == (i < failed_at ? NOISE_HEALTH_OK : expected));
    }
    for (i = 0; i < 65536UL; i++) {
        memset(&ctx, 0xa5, sizeof(ctx));
        memcpy(&saved, &ctx, sizeof(ctx));
        CHECK(noise_health_start(&ctx, (uint16_t)i, 589) ==
              (i < 2 ? NOISE_HEALTH_INVALID_ARGUMENT : NOISE_HEALTH_OK));
        if (i < 2)
            CHECK(memcmp(&ctx, &saved, sizeof(ctx)) == 0);
        memset(&ctx, 0xa5, sizeof(ctx));
        CHECK(noise_health_start(&ctx, 21, (uint16_t)i) ==
              (i < 2 || i > 1024 ? NOISE_HEALTH_INVALID_ARGUMENT : NOISE_HEALTH_OK));
        if (i < 2 || i > 1024)
            CHECK(memcmp(&ctx, &saved, sizeof(ctx)) == 0);
    }
    for (i = 0; i < 12; i++) {
        CHECK(noise_health_start(&ctx, 21, 589) == NOISE_HEALTH_OK);
        switch (i) {
        case 0: ctx.state = 0; break;
        case 1: ctx.state = 255; break;
        case 2: ctx.rct_cutoff = 1; break;
        case 3: ctx.apt_cutoff = 1025; break;
        case 4: ctx.run = 21; break;
        case 5: ctx.window_count = 1024; break;
        case 6: ctx.matches = 1; break;
        case 7: ctx.last = 2; break;
        case 8: ctx.reference = 2; break;
        case 9: ctx.startup_remaining = 1025; break;
        case 10: ctx.startup_remaining = 0; break;
        default: ctx.state = NOISE_HEALTH_MONITORING; break;
        }
        memcpy(&saved, &ctx, sizeof(ctx));
        CHECK(noise_health_push(&ctx, 0) == NOISE_HEALTH_INVALID_STATE);
        CHECK(memcmp(&ctx, &saved, sizeof(ctx)) == 0);
    }
    exact = malloc(sizeof(*exact));
    CHECK(exact != NULL);
    CHECK(noise_health_start(exact, 21, 589) == NOISE_HEALTH_OK);
    for (i = 0; i < 2049; i++)
        CHECK(noise_health_push(exact, (uint8_t)(i & 1u)) == NOISE_HEALTH_OK);
    memcpy(&saved, exact, sizeof(saved));
    CHECK(noise_health_push(exact, 255) == NOISE_HEALTH_INVALID_ARGUMENT);
    CHECK(memcmp(exact, &saved, sizeof(saved)) == 0);
    free(exact);
    return 0;
}

int main(void)
{
    uint16_t result = self_test();
    if (!result)
        result = host_corpus();
    if (result) {
        fprintf(stderr, "Noise health failed at C line %u\n", result);
        return EXIT_FAILURE;
    }
    puts("Noise health: binary RCT/APT, startup, retained faults, independent prefix oracle "
         "and parameter domains PASS; no entropy qualification.");
    return EXIT_SUCCESS;
}
#endif
