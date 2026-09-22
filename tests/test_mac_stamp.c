/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "mac_stamp.h"
#include <stddef.h>
#include <string.h>

MCU_XDATA uint32_t mac_stamp_test_checks;
#define CHECK(test) do { mac_stamp_test_checks++; if (!(test)) return (uint16_t)__LINE__; } while (0)

static MCU_XDATA mac_epoch_t first;
static MCU_XDATA mac_time_stamp_t last, sample;
static MCU_XDATA struct {
    uint8_t before[4];
    mac_epoch_stamp_t value;
    uint8_t after[4];
} output;

static uint16_t project(mac_epoch_result_t expected, uint32_t symbols, uint16_t fine)
{
    mac_epoch_t saved_first;
    mac_time_stamp_t saved_last, saved_sample;
    mac_epoch_stamp_t saved_output;
    uint8_t i;
    memcpy(&saved_first, &first, sizeof(first));
    memcpy(&saved_last, &last, sizeof(last));
    memcpy(&saved_sample, &sample, sizeof(sample));
    memcpy(&saved_output, &output.value, sizeof(output.value));
    CHECK(mac_stamp_project(&first, &last, &sample, &output.value) == expected);
    CHECK(!memcmp(&first, &saved_first, sizeof(first)));
    CHECK(!memcmp(&last, &saved_last, sizeof(last)));
    CHECK(!memcmp(&sample, &saved_sample, sizeof(sample)));
    if (expected == MAC_EPOCH_OK) {
        CHECK(output.value.symbols == symbols && output.value.fine == fine);
    } else CHECK(!memcmp(&output.value, &saved_output, sizeof(saved_output)));
    for (i = 0; i < 4; i++) CHECK(output.before[i] == 0x69 && output.after[i] == 0x69);
    return 0;
}
#define PROJECT(result, symbols, fine) do { \
    uint16_t failure = project(result, symbols, fine); if (failure) return failure; \
} while (0)

static uint16_t self_test(void)
{
    mac_epoch_t saved;
    mac_epoch_stamp_t old_output;
    uint16_t i;
    uint8_t n;
    memset(&first, 0, sizeof(first)); memset(&last, 0, sizeof(last));
    memset(&sample, 0, sizeof(sample)); memset(&output, 0x69, sizeof(output));
    CHECK(mac_stamp_project(NULL, &last, &sample, &output.value) == MAC_EPOCH_INVALID_ARGUMENT);
    CHECK(mac_stamp_project(&first, NULL, &sample, &output.value) == MAC_EPOCH_INVALID_ARGUMENT);
    CHECK(mac_stamp_project(&first, &last, NULL, &output.value) == MAC_EPOCH_INVALID_ARGUMENT);
    CHECK(mac_stamp_project(&first, &last, &sample, NULL) == MAC_EPOCH_INVALID_ARGUMENT);
    PROJECT(MAC_EPOCH_INVALID_STATE, 0, 0);
    CHECK(mac_epoch_start(&first, &sample, 0xffffffffUL) == MAC_EPOCH_OK);
    PROJECT(MAC_EPOCH_OK, 0xffffffffUL, 0);
    last.fine = 10; sample.fine = 5;
    PROJECT(MAC_EPOCH_OK, 0xffffffffUL, 5);
    sample.fine = 10; PROJECT(MAC_EPOCH_OK, 0xffffffffUL, 10);
    sample.fine = 11; PROJECT(MAC_EPOCH_TIME_ERROR, 0, 0);
    sample.fine = 0; PROJECT(MAC_EPOCH_OK, 0xffffffffUL, 0);
    first.fine = 1; PROJECT(MAC_EPOCH_TIME_ERROR, 0, 0);
    sample.fine = 1; last.fine = 0; PROJECT(MAC_EPOCH_TIME_ERROR, 0, 0);
    last = sample; PROJECT(MAC_EPOCH_OK, 0xffffffffUL, 1);

    for (i = 0; i < 512; i++) {
        sample.periods = 0xfffffeUL; sample.fine = i;
        CHECK(mac_epoch_start(&first, &sample, 0xffffffffUL) == MAC_EPOCH_OK);
        last.periods = 0; last.fine = i;
        PROJECT(MAC_EPOCH_OK, 0xffffffffUL, i);
        sample.periods = 0; PROJECT(MAC_EPOCH_OK, 0, i);
        sample.periods = 0xfffffeUL; sample.fine = (uint16_t)(i == 511 ? 511 : i+1);
        PROJECT(MAC_EPOCH_OK, 0xffffffffUL, sample.fine);
        sample.periods = 0; sample.fine = (uint16_t)(i == 0 ? 0 : i-1);
        PROJECT(MAC_EPOCH_OK, 0, sample.fine);
        sample.periods = 1; sample.fine = 0; PROJECT(MAC_EPOCH_TIME_ERROR, 0, 0);
    }

    sample.periods = 0; sample.fine = 0;
    CHECK(mac_epoch_start(&first, &sample, 10) == MAC_EPOCH_OK);
    last.periods = 0x7fffffUL; last.fine = 255;
    PROJECT(MAC_EPOCH_OK, 10, 0);
    sample = last; PROJECT(MAC_EPOCH_OK, 0x800009UL, 255);
    last.fine = 256; PROJECT(MAC_EPOCH_TIME_ERROR, 0, 0);
    last.fine = 257; PROJECT(MAC_EPOCH_TIME_ERROR, 0, 0);
    last.periods = 0xffffffUL; PROJECT(MAC_EPOCH_INVALID_ARGUMENT, 0, 0);
    last.periods = 0; last.fine = 512; PROJECT(MAC_EPOCH_INVALID_ARGUMENT, 0, 0);
    last.fine = 0; sample.periods = 0; sample.fine = 512;
    PROJECT(MAC_EPOCH_INVALID_ARGUMENT, 0, 0);
    sample.fine = 0; sample.periods = 0xffffffUL;
    PROJECT(MAC_EPOCH_INVALID_ARGUMENT, 0, 0);
    sample.periods = 0;
    memcpy(&saved, &first, sizeof(first));
    for (n = 0; n < 4; n++) {
        first = saved;
        if (n == 0) first.state = MAC_EPOCH_FAULT;
        if (n == 1) first.state = 3;
        if (n == 2) first.periods = 0xffffffUL;
        if (n == 3) first.fine = 512;
        PROJECT(n == 0 ? MAC_EPOCH_TIME_ERROR : MAC_EPOCH_INVALID_STATE, 0, 0);
    }
    first = saved;
    memcpy(&old_output, &output.value, sizeof(old_output));
    CHECK(mac_stamp_project(NULL, &last, &sample, &output.value) == MAC_EPOCH_INVALID_ARGUMENT);
    CHECK(!memcmp(&old_output, &output.value, sizeof(old_output)));
    /* A rejected projection must not poison the independent live epoch. */
    sample.periods = 1; PROJECT(MAC_EPOCH_TIME_ERROR, 0, 0);
    CHECK(mac_epoch_step(&first, &sample, &output.value) == MAC_EPOCH_OK);
    CHECK(first.periods == 1 && output.value.symbols == 11);
    last = sample;
    PROJECT(MAC_EPOCH_OK, 11, 0);
    return 0;
}

#if defined(__SDCC)
typedef char stamp_size[sizeof(mac_epoch_stamp_t) == 6 ? 1 : -1];
volatile MCU_XDATA MCU_AT(0x1e00) uint8_t mac_stamp_test_result[8];
void main(void)
{
    uint16_t result = self_test();
    mac_stamp_test_result[0] = 'M'; mac_stamp_test_result[1] = 'S';
    mac_stamp_test_result[2] = 'P'; mac_stamp_test_result[3] = '1';
    mac_stamp_test_result[4] = 1; mac_stamp_test_result[5] = 8;
    mac_stamp_test_result[6] = (uint8_t)result;
    mac_stamp_test_result[7] = (uint8_t)(result >> 8);
    __asm
        .globl _mac_stamp_test_done
    _mac_stamp_test_done:
        nop
    __endasm;
    for (;;) {}
}
#else
#include <stdio.h>
#include <stdlib.h>

static void raw_value(mac_time_stamp_t *raw, uint64_t value)
{
    value %= UINT64_C(0x1fffffe00);
    raw->periods = (uint32_t)(value / 512u);
    raw->fine = (uint16_t)(value % 512u);
}
static uint16_t oracle(uint64_t a, uint64_t b, uint64_t s)
{
    const uint64_t modulus = UINT64_C(0x1fffffe00);
    uint64_t window = (b + modulus - a) % modulus;
    uint64_t offset = (s + modulus - a) % modulus;
    uint64_t coordinate = UINT64_C(0xffffffff) * 512u + a % 512u + offset;
    mac_time_stamp_t start;
    raw_value(&start, a); raw_value(&last, b); raw_value(&sample, s);
    CHECK(mac_epoch_start(&first, &start, 0xffffffffUL) == MAC_EPOCH_OK);
    PROJECT(window < modulus/2 && offset <= window ? MAC_EPOCH_OK : MAC_EPOCH_TIME_ERROR,
            (uint32_t)(coordinate / 512u), (uint16_t)(coordinate % 512u));
    return 0;
}
static uint16_t host_test(void)
{
    static const uint64_t lengths[] = {
        0, 1, 2, 255, 256, 511, 512, 513, 0xfffffeff, 0xffffff00, 0xffffff01, 0x1fffffdff
    };
    const uint64_t modulus = UINT64_C(0x1fffffe00);
    uint64_t a, b, s;
    uint32_t word = 0x253081;
    unsigned i, j, k;
    uint16_t failure;
    mac_epoch_t *ctx;
    mac_time_stamp_t *end, *raw;
    mac_epoch_stamp_t *out;
    for (i = 0; i < 512; i++) for (j = 0; j < sizeof(lengths)/sizeof(lengths[0]); j++) {
        a = modulus - 512u + i; b = (a + lengths[j]) % modulus;
        for (k = 0; k < 512; k++) {
            s = (a + k) % modulus;
            failure = oracle(a, b, s); if (failure) return failure;
        }
        for (k = 0; k < 3; k++) {
            s = (b + modulus + k - 1u) % modulus;
            failure = oracle(a, b, s); if (failure) return failure;
        }
    }
    for (i = 0; i < 4096; i++) {
        word = word * 1664525UL + 1013904223UL; a = word;
        word = word * 1664525UL + 1013904223UL; b = (a + word) % modulus;
        word = word * 1664525UL + 1013904223UL; s = (a + word) % modulus;
        failure = oracle(a, b, s); if (failure) return failure;
    }
    sample.periods = last.periods = 0; sample.fine = last.fine = 0;
    CHECK(mac_epoch_start(&first, &sample, 0) == MAC_EPOCH_OK);
    for (i = 512; i < 65536; i++) {
        sample.fine = (uint16_t)i; PROJECT(MAC_EPOCH_INVALID_ARGUMENT, 0, 0);
        sample.fine = 0; last.fine = (uint16_t)i; PROJECT(MAC_EPOCH_INVALID_ARGUMENT, 0, 0);
        last.fine = 0; first.fine = (uint16_t)i; PROJECT(MAC_EPOCH_INVALID_STATE, 0, 0);
        first.fine = 0;
    }
    ctx = malloc(sizeof(*ctx)); end = malloc(sizeof(*end));
    raw = malloc(sizeof(*raw)); out = malloc(sizeof(*out));
    CHECK(ctx && end && raw && out);
    raw_value(raw, modulus-1); raw_value(end, 0);
    CHECK(mac_epoch_start(ctx, raw, 0xffffffffUL) == MAC_EPOCH_OK);
    CHECK(mac_stamp_project(ctx, end, raw, out) == MAC_EPOCH_OK);
    CHECK(out->symbols == 0xffffffffUL && out->fine == 511);
    raw_value(raw, 0);
    CHECK(mac_stamp_project(ctx, end, raw, out) == MAC_EPOCH_OK);
    CHECK(out->symbols == 0 && out->fine == 0);
    free(ctx); free(end); free(raw); free(out);
    return 0;
}
int main(void)
{
    uint16_t result = self_test();
    if (!result) result = host_test();
    if (result) { fprintf(stderr, "MAC stamp failed at C line %u\n", result); return 1; }
    printf("MAC stamp: %lu checks; independent wide-coordinate window oracle, wraps, "
           "preservation and exact allocations PASS (arithmetic only).\n",
           (unsigned long)mac_stamp_test_checks);
    return 0;
}
#endif
