/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "mac_epoch.h"
#include <stddef.h>
#include <string.h>

MCU_XDATA uint32_t mac_epoch_test_checks;
#define CHECK(test) do { mac_epoch_test_checks++; if (!(test)) return (uint16_t)__LINE__; } while (0)

static const MCU_CODE struct {
    uint32_t distance;
    uint16_t first, last;
    uint8_t good;
} boundaries[] = {
    {0, 0, 0, 1}, {0, 0, 511, 1}, {0, 511, 0, 0},
    {1, 511, 0, 1}, {1, 0, 511, 1},
    {0x7ffffeUL, 0, 511, 1},
    {0x7fffffUL, 0, 255, 1}, {0x7fffffUL, 0, 256, 0},
    {0x7fffffUL, 0, 257, 0}, {0x7fffffUL, 1, 256, 1},
    {0x800000UL, 511, 254, 1}, {0x800000UL, 511, 255, 0},
    {0x800000UL, 511, 256, 0}, {0x800000UL, 257, 0, 1},
    {0x800000UL, 256, 0, 0}, {0x800001UL, 511, 0, 0},
    {0xfffffeUL, 0, 0, 0}
};

static uint16_t self_test(void)
{
    struct { uint8_t before[4]; mac_epoch_t value; uint8_t after[4]; } guarded;
    struct { uint8_t before[4]; mac_epoch_stamp_t value; uint8_t after[4]; } result;
    mac_epoch_t saved, other;
    mac_epoch_stamp_t saved_output;
    mac_time_stamp_t raw, saved_raw;
    uint16_t i;
    uint8_t n;
    uint32_t expected;
    memset(&guarded, 0xa5, sizeof(guarded));
    memset(&result, 0x69, sizeof(result));
    memset(&raw, 0, sizeof(raw));
    memcpy(&saved, &guarded.value, sizeof(saved));
    memcpy(&saved_output, &result.value, sizeof(saved_output));
    CHECK(mac_epoch_start(NULL, &raw, 0) == MAC_EPOCH_INVALID_ARGUMENT);
    CHECK(mac_epoch_start(&guarded.value, NULL, 0) == MAC_EPOCH_INVALID_ARGUMENT);
    raw.fine = 512;
    CHECK(mac_epoch_start(&guarded.value, &raw, 0) == MAC_EPOCH_INVALID_ARGUMENT);
    raw.fine = 0; raw.periods = 0xffffffUL;
    CHECK(mac_epoch_start(&guarded.value, &raw, 0) == MAC_EPOCH_INVALID_ARGUMENT);
    raw.periods = 0xffffffffUL;
    CHECK(mac_epoch_start(&guarded.value, &raw, 0) == MAC_EPOCH_INVALID_ARGUMENT);
    CHECK(!memcmp(&saved, &guarded.value, sizeof(saved)));
    raw.periods = 0;
    CHECK(mac_epoch_step(&guarded.value, &raw, &result.value) == MAC_EPOCH_INVALID_STATE);
    CHECK(mac_epoch_step(NULL, &raw, &result.value) == MAC_EPOCH_INVALID_ARGUMENT);
    CHECK(!memcmp(&saved_output, &result.value, sizeof(saved_output)));
    CHECK(mac_epoch_start(&guarded.value, &raw, 0xffffffffUL) == MAC_EPOCH_OK);
    memcpy(&saved, &guarded.value, sizeof(saved));
    CHECK(mac_epoch_step(&guarded.value, NULL, &result.value) == MAC_EPOCH_INVALID_ARGUMENT);
    CHECK(mac_epoch_step(&guarded.value, &raw, NULL) == MAC_EPOCH_INVALID_ARGUMENT);
    raw.fine = 65535u;
    CHECK(mac_epoch_step(&guarded.value, &raw, &result.value) == MAC_EPOCH_INVALID_ARGUMENT);
    raw.fine = 0; raw.periods = 0xffffffUL;
    CHECK(mac_epoch_step(&guarded.value, &raw, &result.value) == MAC_EPOCH_INVALID_ARGUMENT);
    CHECK(!memcmp(&saved, &guarded.value, sizeof(saved)));
    CHECK(!memcmp(&saved_output, &result.value, sizeof(saved_output)));
    raw.periods = 0;
    for (n = 0; n < 3; n++) {
        memcpy(&guarded.value, &saved, sizeof(saved));
        if (n == 0) guarded.value.state = 0;
        else if (n == 1) guarded.value.periods = 0xffffffUL;
        else guarded.value.fine = 512;
        memcpy(&other, &guarded.value, sizeof(other));
        CHECK(mac_epoch_step(&guarded.value, &raw, &result.value) == MAC_EPOCH_INVALID_STATE);
        CHECK(!memcmp(&other, &guarded.value, sizeof(other)));
        CHECK(!memcmp(&saved_output, &result.value, sizeof(saved_output)));
    }
    for (n = 0; n < sizeof(boundaries) / sizeof(boundaries[0]); n++) {
        raw.periods = 0xfffffdUL; raw.fine = boundaries[n].first;
        CHECK(mac_epoch_start(&guarded.value, &raw, 0xfffffffeUL) == MAC_EPOCH_OK);
        memcpy(&saved, &guarded.value, sizeof(saved));
        memcpy(&saved_output, &result.value, sizeof(saved_output));
        raw.periods += boundaries[n].distance;
        if (raw.periods >= 0xffffffUL) raw.periods -= 0xffffffUL;
        raw.fine = boundaries[n].last;
        memcpy(&saved_raw, &raw, sizeof(raw));
        CHECK(mac_epoch_step(&guarded.value, &raw, &result.value) ==
              (boundaries[n].good ? MAC_EPOCH_OK : MAC_EPOCH_TIME_ERROR));
        CHECK(!memcmp(&saved_raw, &raw, sizeof(raw)));
        if (boundaries[n].good) {
            expected = 0xfffffffeUL + boundaries[n].distance;
            CHECK(result.value.symbols == expected && result.value.fine == raw.fine);
            CHECK(guarded.value.symbols == expected && guarded.value.periods == raw.periods &&
                  guarded.value.fine == raw.fine && guarded.value.state == MAC_EPOCH_READY);
        } else {
            saved.state = MAC_EPOCH_FAULT;
            CHECK(!memcmp(&saved, &guarded.value, sizeof(saved)));
            CHECK(!memcmp(&saved_output, &result.value, sizeof(saved_output)));
            CHECK(mac_epoch_step(&guarded.value, NULL, NULL) == MAC_EPOCH_TIME_ERROR);
            CHECK(!memcmp(&saved, &guarded.value, sizeof(saved)));
        }
    }
    for (i = 0; i < 512; i++) {
        raw.periods = 0xfffffeUL; raw.fine = i;
        CHECK(mac_epoch_start(&guarded.value, &raw, 0xffffffffUL) == MAC_EPOCH_OK);
        CHECK(mac_epoch_start(&other, &raw, 123) == MAC_EPOCH_OK);
        raw.periods = 0;
        CHECK(mac_epoch_step(&guarded.value, &raw, &result.value) == MAC_EPOCH_OK);
        CHECK(result.value.symbols == 0 && result.value.fine == i);
        CHECK(mac_epoch_step(&guarded.value, &raw, &result.value) == MAC_EPOCH_OK);
        CHECK(result.value.symbols == 0 && result.value.fine == i);
        CHECK(mac_epoch_step(&other, &raw, &result.value) == MAC_EPOCH_OK);
        CHECK(result.value.symbols == 124 && result.value.fine == i);
        memcpy(&saved, &guarded.value, sizeof(saved));
        raw.periods = 1;
        CHECK(mac_epoch_step(&saved, &raw, &result.value) == MAC_EPOCH_OK);
        CHECK(result.value.symbols == 1 && result.value.fine == i);
        CHECK(guarded.value.symbols == 0 && guarded.value.periods == 0);
    }
    for (n = 0; n < 4; n++) {
        CHECK(guarded.before[n] == 0xa5 && guarded.after[n] == 0xa5);
        CHECK(result.before[n] == 0x69 && result.after[n] == 0x69);
    }
    return 0;
}

#if defined(__SDCC)
typedef char context_size[sizeof(mac_epoch_t) == 11 ? 1 : -1];
typedef char stamp_size[sizeof(mac_epoch_stamp_t) == 6 ? 1 : -1];
volatile MCU_XDATA MCU_AT(0x1e00) uint8_t mac_epoch_test_result[8];

void main(void)
{
    uint16_t result = self_test();
    mac_epoch_test_result[0] = 'M'; mac_epoch_test_result[1] = 'E';
    mac_epoch_test_result[2] = 'P'; mac_epoch_test_result[3] = '1';
    mac_epoch_test_result[4] = 1; mac_epoch_test_result[5] = 8;
    mac_epoch_test_result[6] = (uint8_t)result;
    mac_epoch_test_result[7] = (uint8_t)(result >> 8);
    __asm
        .globl _mac_epoch_test_done
    _mac_epoch_test_done:
        nop
    __endasm;
    for (;;) {}
}
#else
#include <stdio.h>
#include <stdlib.h>

static uint16_t compare(mac_epoch_t *ctx, const mac_time_stamp_t *first,
                        const mac_time_stamp_t *last, mac_epoch_stamp_t *output)
{
    const uint64_t modulus = UINT64_C(0xffffff) * 512;
    uint64_t a = (uint64_t)first->periods * 512 + first->fine;
    uint64_t b = (uint64_t)last->periods * 512 + last->fine;
    uint64_t delta = (b + modulus - a) % modulus;
    mac_epoch_t saved;
    mac_epoch_stamp_t saved_output;
    uint32_t origin = 0xfffffffeUL;
    memset(ctx, 0xa5, sizeof(*ctx));
    memset(output, 0x69, sizeof(*output));
    CHECK(mac_epoch_start(ctx, first, origin) == MAC_EPOCH_OK);
    memcpy(&saved, ctx, sizeof(saved));
    memcpy(&saved_output, output, sizeof(saved_output));
    CHECK(mac_epoch_step(ctx, last, output) ==
          (delta < modulus / 2 ? MAC_EPOCH_OK : MAC_EPOCH_TIME_ERROR));
    if (delta < modulus / 2) {
        uint64_t coordinate = (uint64_t)origin * 512 + first->fine + delta;
        CHECK(output->symbols == (uint32_t)(coordinate / 512) && output->fine == coordinate % 512);
    } else {
        saved.state = MAC_EPOCH_FAULT;
        CHECK(!memcmp(&saved, ctx, sizeof(saved)));
        CHECK(!memcmp(&saved_output, output, sizeof(saved_output)));
    }
    return 0;
}

static uint16_t host_test(void)
{
    static const uint32_t distances[] = {0, 1, 0x7ffffe, 0x7fffff, 0x800000, 0xfffffe};
    mac_epoch_t *ctx = malloc(sizeof(*ctx));
    mac_epoch_stamp_t *out = malloc(sizeof(*out));
    mac_time_stamp_t *a = malloc(sizeof(*a)), *b = malloc(sizeof(*b));
    mac_epoch_t saved;
    mac_epoch_stamp_t saved_output;
    unsigned i, j, k;
    uint16_t failure;
    uint64_t absolute, next;
    uint32_t word = 0x513;
    CHECK(ctx && out && a && b);
    memset(a, 0, sizeof(*a)); memset(b, 0, sizeof(*b));
    for (i = 0; i < 512; i++) for (j = 0; j < 512; j++)
        for (k = 0; k < sizeof(distances) / sizeof(distances[0]); k++) {
            a->periods = i & 1u ? 0xfffffeUL : 0x100;
            a->fine = (uint16_t)i; b->fine = (uint16_t)j;
            b->periods = (a->periods + distances[k]) % 0xffffffUL;
            failure = compare(ctx, a, b, out);
            if (failure) return failure;
        }
    a->periods = 0; a->fine = 0;
    CHECK(mac_epoch_start(ctx, a, 0xfffffffeUL) == MAC_EPOCH_OK);
    absolute = UINT64_C(0xfffffffe) * 512;
    for (i = 0; i < 4096; i++) {
        word = word * 1664525UL + 1013904223UL;
        next = absolute + word % UINT64_C(0xffffff00);
        a->periods = (uint32_t)((next - UINT64_C(0xfffffffe) * 512) / 512 % 0xffffff);
        a->fine = (uint16_t)(next % 512);
        CHECK(mac_epoch_step(ctx, a, out) == MAC_EPOCH_OK);
        CHECK(out->symbols == (uint32_t)(next / 512) && out->fine == next % 512);
        absolute = next;
    }
    memcpy(&saved, ctx, sizeof(saved));
    memcpy(&saved_output, out, sizeof(saved_output));
    for (i = 512; i < 65536; i++) {
        a->fine = (uint16_t)i;
        CHECK(mac_epoch_step(ctx, a, out) == MAC_EPOCH_INVALID_ARGUMENT);
        CHECK(mac_epoch_start(ctx, a, 0) == MAC_EPOCH_INVALID_ARGUMENT);
        CHECK(!memcmp(&saved, ctx, sizeof(saved)));
        CHECK(!memcmp(&saved_output, out, sizeof(saved_output)));
    }
    free(ctx); free(out); free(a); free(b);
    return 0;
}

int main(void)
{
    uint16_t result = self_test();
    if (!result) result = host_test();
    if (result) {
        fprintf(stderr, "MAC epoch failed at line %u\n", result);
        return 1;
    }
    printf("MAC epoch: %lu checks; exhaustive fine pairs/half-range edges, "
           "4096 continuous multiwrap samples and exact allocations PASS.\n",
           (unsigned long)mac_epoch_test_checks);
    return 0;
}
#endif
