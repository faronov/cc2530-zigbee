/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "zcl_identify.h"
#include "cc2530_mmio.h"

#include <stddef.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) return (uint16_t)__LINE__; } while (0)
static const MCU_CODE uint8_t start[] = {0x01, 0x5a, 0x00, 3, 0};
static const MCU_CODE uint8_t query[] = {0x01, 0x5a, 0x01};
static const MCU_CODE uint8_t read[] = {0, 0x5a, 0, 0, 0, 0xfd, 0xff};
static const MCU_CODE uint8_t discover[] = {0, 0x5a, 0x0c, 0, 0, 0xff};
static const MCU_CODE uint8_t ack[] = {0x18, 0x5a, 0x0b, 0, 0};
static const MCU_CODE uint8_t read_two[] = {
    0x18, 0x5a, 1, 0, 0, 0, 0x21, 2, 0, 0xfd, 0xff, 0, 0x21, 2, 0
};
static const MCU_CODE uint8_t discover_two[] = {
    0x18, 0x5a, 0x0d, 1, 0, 0, 0x21, 0xfd, 0xff, 0x21
};
static zcl_id_t ctx, saved, other;
static zcl_dispatch_info_t info;
static zcl_frame_info_t frame;
static zcl_value_info_t value;
static uint8_t request[102], response[102], encoded;
static uint16_t cases;

static uint8_t filled(const void *p, uint16_t n, uint8_t v)
{
    const uint8_t *b = p;
    while (n--) if (*b++ != v) return 0;
    return 1;
}

static void outputs(void)
{
    memset(response, 0xc7, sizeof(response));
    memset(&info, 0xa5, sizeof(info));
}

static uint16_t fail_rx(uint32_t now, uint16_t length, uint16_t cap, zcl_codec_result_t expected)
{
    memcpy(&saved, &ctx, sizeof(ctx));
    outputs();
    CHECK(zcl_id_rx(&ctx, now, request, length, response + 1, cap, &info) == expected);
    CHECK(memcmp(&ctx, &saved, sizeof(ctx)) == 0);
    CHECK(filled(response, sizeof(response), 0xc7) && filled(&info, sizeof(info), 0xa5));
    cases++;
    return 0;
}

static uint16_t fail_tick(uint32_t now)
{
    memcpy(&saved, &ctx, sizeof(ctx));
    CHECK(zcl_id_tick(&ctx, now) == ZCL_CODEC_INVALID_VALUE);
    CHECK(memcmp(&ctx, &saved, sizeof(ctx)) == 0);
    cases++;
    return 0;
}

static uint16_t golden_cases(void)
{
    CHECK(zcl_id_init(&ctx, 100) == ZCL_CODEC_OK);
    CHECK(ctx.stamp == 100 && ctx.remaining == 0 && ctx.phase == 0);
    outputs();
    CHECK(zcl_id_rx(&ctx, 100, query, 3, response, 0, &info) == ZCL_CODEC_OK);
    CHECK(info.kind == 2 && info.length == 0 && info.sequence == 0x5a);
    CHECK(info.command_id == 0 && info.requested_count == 0 && info.returned_count == 0
          && info.discovery_complete == 0 && info.default_command == 0
          && info.default_status == 0 && info.default_raw_status == 0);
    CHECK(filled(response, sizeof(response), 0xc7));
    CHECK(zcl_id_rx(&ctx, 100, read, 5, response, 9, &info) == ZCL_CODEC_OK);
    CHECK(memcmp(response, "\x18\x5a\x01\x00\x00\x00\x21\x00\x00", 9) == 0);
    outputs();
    CHECK(zcl_id_rx(&ctx, 100, start, 5, response + 1, 5, &info) == ZCL_CODEC_OK);
    CHECK(ctx.remaining == 3 && ctx.phase == 0 && ctx.stamp == 100);
    CHECK(info.kind == 0 && info.command_id == 0x0b && info.length == 5);
    CHECK(info.default_command == 0 && info.default_status == 0 && info.sequence == 0x5a);
    CHECK(memcmp(response + 1, ack, 5) == 0 && response[0] == 0xc7 && filled(response + 6, 96, 0xc7));
    CHECK(zcl_id_rx(&ctx, 1099, query, 3, response, 5, &info) == ZCL_CODEC_OK);
    CHECK(ctx.remaining == 3 && ctx.phase == 999);
    CHECK(info.kind == 0 && info.command_id == 0 && info.length == 5);
    CHECK(memcmp(response, "\x19\x5a\x00\x03\x00", 5) == 0);
    CHECK(zcl_frame_decode(response, 5, &frame) == ZCL_CODEC_OK);
    CHECK(frame.header.type == 1 && frame.header.flags == 0x18 && frame.header.command_id == 0);
    CHECK(zcl_value_decode(0x21, response + 3, 2, &value) == ZCL_CODEC_OK);
    CHECK(value.encoded_length == 2 && value.data_length == 2 && value.non_value_pattern == 0);
    CHECK(zcl_frame_encode(&frame.header, response + 3, 2, request, 5, &encoded) == ZCL_CODEC_OK);
    CHECK(encoded == 5 && memcmp(request, "\x19\x5a\x00\x03\x00", 5) == 0);
    CHECK(zcl_id_rx(&ctx, 1100, read, 7, response, 15, &info) == ZCL_CODEC_OK);
    CHECK(ctx.remaining == 2 && ctx.phase == 0 && ctx.stamp == 1100);
    CHECK(info.length == 15 && info.requested_count == 2 && info.returned_count == 2);
    CHECK(memcmp(response, read_two, 15) == 0);
    CHECK(zcl_id_rx(&ctx, 1101, discover, 6, response, 10, &info) == ZCL_CODEC_OK);
    CHECK(info.length == 10 && info.returned_count == 2 && info.discovery_complete == 1);
    CHECK(memcmp(response, discover_two, 10) == 0 && ctx.phase == 1);
    /* Relocation is valid: there are no borrowed/self-referencing pointers. */
    other = ctx;
    CHECK(zcl_id_tick(&other, 2100) == ZCL_CODEC_OK && other.remaining == 1 && other.phase == 0);
    CHECK(ctx.remaining == 2 && ctx.phase == 1);
    CHECK(zcl_id_rx(&ctx, 3100, query, 3, response, 0, &info) == ZCL_CODEC_OK);
    CHECK(ctx.remaining == 0 && ctx.phase == 0 && info.kind == 2 && info.length == 0);
    CHECK(zcl_id_init(&ctx, 500) == ZCL_CODEC_OK && ctx.remaining == 0 && ctx.phase == 0);
    cases++;
    return 0;
}

static uint16_t clock_cases(void)
{
    volatile uint8_t i, j;
    volatile uint32_t base, now;
    static const MCU_CODE uint16_t offsets[] = {0, 1, 999};
    for (i = 0; i < 2; i++) {
        base = i ? 0xffffff00UL : 77UL;
        CHECK(zcl_id_init(&ctx, base) == ZCL_CODEC_OK);
        CHECK(zcl_id_rx(&ctx, base, start, 5, response, 5, &info) == ZCL_CODEC_OK);
        for (j = 0; j < 3; j++) {
            volatile uint8_t k;
            for (k = 0; k < 3; k++) {
                now = base + (uint32_t)j * 1000UL + offsets[k];
                CHECK(zcl_id_tick(&ctx, now) == ZCL_CODEC_OK);
                CHECK(ctx.remaining == 3u - j && ctx.phase == offsets[k] && ctx.stamp == now);
                CHECK(zcl_id_tick(&ctx, now) == ZCL_CODEC_OK); /* exact duplicate time */
                CHECK(ctx.remaining == 3u - j && ctx.phase == offsets[k]);
                cases++;
            }
        }
        CHECK(zcl_id_tick(&ctx, base + 3000UL) == ZCL_CODEC_OK);
        CHECK(ctx.remaining == 0 && ctx.phase == 0);
        CHECK(zcl_id_tick(&ctx, base + 3001UL) == ZCL_CODEC_OK && ctx.phase == 0);
    }
    CHECK(zcl_id_init(&ctx, 0) == ZCL_CODEC_OK);
    CHECK(zcl_id_rx(&ctx, 0, start, 5, response, 5, &info) == ZCL_CODEC_OK);
    CHECK(zcl_id_tick(&ctx, 1999) == ZCL_CODEC_OK && ctx.remaining == 2 && ctx.phase == 999);
    /* Repeated Identify command/TSN/value restarts its own second phase. */
    CHECK(zcl_id_rx(&ctx, 1999, start, 5, response, 5, &info) == ZCL_CODEC_OK);
    CHECK(ctx.remaining == 3 && ctx.phase == 0);
    CHECK(zcl_id_tick(&ctx, 2000) == ZCL_CODEC_OK && ctx.remaining == 3 && ctx.phase == 1);
    CHECK(fail_tick(1999) == 0);
    CHECK(fail_tick(2000UL + 0x80000000UL) == 0);
    CHECK(fail_tick((uint32_t)(2000UL + 0xffffffffUL)) == 0);
    memcpy(request, start, 5);
    CHECK(fail_rx(1999, 5, 100, ZCL_CODEC_INVALID_VALUE) == 0);
    request[3] = request[4] = 0xff;
    CHECK(zcl_id_rx(&ctx, 2000, request, 5, response, 5, &info) == ZCL_CODEC_OK);
    CHECK(ctx.remaining == 65535u && ctx.phase == 0);
    CHECK(zcl_id_rx(&ctx, 2000, query, 3, response, 5, &info) == ZCL_CODEC_OK);
    CHECK(memcmp(response, "\x19\x5a\x00\xff\xff", 5) == 0); /* FFFF is valid IdentifyTime */
    CHECK(zcl_id_rx(&ctx, 2000, read, 5, response, 9, &info) == ZCL_CODEC_OK);
    CHECK(memcmp(response, "\x18\x5a\x01\x00\x00\x00\x21\xff\xff", 9) == 0);
    CHECK(zcl_id_tick(&ctx, 2000UL + 65534999UL) == ZCL_CODEC_OK);
    CHECK(ctx.remaining == 1 && ctx.phase == 999);
    CHECK(zcl_id_rx(&ctx, 2000UL + 65535000UL, query, 3, response, 0, &info) == ZCL_CODEC_OK);
    CHECK(ctx.remaining == 0 && ctx.phase == 0 && info.kind == 2);
    CHECK(zcl_id_tick(&ctx, ctx.stamp + 0x7fffffffUL) == ZCL_CODEC_OK); /* largest admitted delta */
    CHECK(ctx.remaining == 0 && ctx.phase == 0);
    CHECK(zcl_id_rx(&ctx, ctx.stamp, start, 5, response, 5, &info) == ZCL_CODEC_OK);
    CHECK(zcl_id_tick(&ctx, ctx.stamp + 999UL) == ZCL_CODEC_OK);
    CHECK(zcl_id_tick(&ctx, ctx.stamp + 0x7fffffffUL) == ZCL_CODEC_OK);
    CHECK(ctx.remaining == 0 && ctx.phase == 0); /* wide addition with nonzero phase */
    ctx.phase = 1000;
    CHECK(fail_tick(ctx.stamp) == 0);
    CHECK(fail_rx(ctx.stamp, 5, 100, ZCL_CODEC_INVALID_VALUE) == 0);
    ctx.phase = 1;
    CHECK(fail_tick(ctx.stamp) == 0); /* idle phase is invalid */
    CHECK(zcl_id_init(&ctx, 0) == ZCL_CODEC_OK);
    cases++;
    return 0;
}

static uint16_t capacity_cases(void)
{
    uint8_t cap;
    memcpy(request, start, 5);
    CHECK(zcl_id_init(&ctx, 0) == ZCL_CODEC_OK);
    for (cap = 0; cap < 5; cap++) CHECK(fail_rx(100, 5, cap, ZCL_CODEC_BUFFER_TOO_SMALL) == 0);
    request[0] = 0x11;
    outputs();
    CHECK(zcl_id_rx(&ctx, 100, request, 5, response, 0, &info) == ZCL_CODEC_OK);
    CHECK(ctx.remaining == 3 && ctx.phase == 0 && info.kind == 2 && info.length == 0);
    CHECK(filled(response, sizeof(response), 0xc7));
    memcpy(request, query, 3);
    for (cap = 0; cap < 5; cap++) CHECK(fail_rx(1100, 3, cap, ZCL_CODEC_BUFFER_TOO_SMALL) == 0);
    CHECK(ctx.remaining == 3 && ctx.stamp == 100 && ctx.phase == 0);
    CHECK(zcl_id_rx(&ctx, 1100, query, 3, response, 5, &info) == ZCL_CODEC_OK);
    CHECK(memcmp(response, "\x19\x5a\x00\x02\x00", 5) == 0 && ctx.remaining == 2);
    memcpy(request, start, 5); request[3] = request[4] = 0;
    CHECK(fail_rx(2100, 5, 4, ZCL_CODEC_BUFFER_TOO_SMALL) == 0); /* failed stop is atomic */
    CHECK(ctx.remaining == 2 && ctx.stamp == 1100);
    CHECK(zcl_id_rx(&ctx, 2100, request, 5, response, 5, &info) == ZCL_CODEC_OK);
    CHECK(ctx.remaining == 0 && ctx.phase == 0 && memcmp(response, ack, 5) == 0);
    request[0] = 0x11;
    outputs();
    CHECK(zcl_id_rx(&ctx, 2101, request, 5, response, 0, &info) == ZCL_CODEC_OK);
    CHECK(info.kind == 2 && filled(response, sizeof(response), 0xc7));
    /* Real Read capacity errors must not commit elapsed state either. */
    CHECK(zcl_id_rx(&ctx, 2200, start, 5, response, 5, &info) == ZCL_CODEC_OK);
    memcpy(request, read, 7);
    for (cap = 0; cap < 6; cap++) CHECK(fail_rx(3200, 7, cap, ZCL_CODEC_BUFFER_TOO_SMALL) == 0);
    outputs();
    CHECK(zcl_id_rx(&ctx, 3200, read, 7, response + 1, 6, &info) == ZCL_CODEC_OK);
    CHECK(info.requested_count == 2 && info.returned_count == 1 && info.length == 6);
    CHECK(memcmp(response + 1, "\x18\x5a\x01\x00\x00\x89", 6) == 0);
    CHECK(ctx.remaining == 2 && ctx.phase == 0 && filled(response + 7, 95, 0xc7));
    memcpy(request, discover, 6);
    for (cap = 0; cap < 7; cap++) CHECK(fail_rx(4200, 6, cap, ZCL_CODEC_BUFFER_TOO_SMALL) == 0);
    CHECK(zcl_id_rx(&ctx, 4200, discover, 6, response, 7, &info) == ZCL_CODEC_OK);
    CHECK(info.length == 7 && info.returned_count == 1 && info.discovery_complete == 0);
    CHECK(memcmp(response, "\x18\x5a\x0d\x00\x00\x00\x21", 7) == 0);
    cases++;
    return 0;
}

static uint16_t receive_cases(void)
{
    uint8_t n;
    CHECK(zcl_id_init(&ctx, 0) == ZCL_CODEC_OK);
    CHECK(zcl_id_rx(&ctx, 0, start, 5, response, 5, &info) == ZCL_CODEC_OK);
    memcpy(request, start, 5);
    for (n = 0; n < 3; n++) CHECK(fail_rx(1000, n, 100, ZCL_CODEC_TRUNCATED) == 0);
    for (n = 3; n < 5; n++) {
        CHECK(fail_rx(1000, n, 4, ZCL_CODEC_BUFFER_TOO_SMALL) == 0);
        request[0] = 0x11; /* errors still respond with default disabled */
        CHECK(zcl_id_rx(&ctx, 1000, request, n, response, 5, &info) == ZCL_CODEC_OK);
        CHECK(info.default_status == 0x80 && info.kind == 0);
        CHECK(memcmp(response, "\x18\x5a\x0b\x00\x80", 5) == 0);
        CHECK(ctx.remaining == 2 && ctx.phase == 0); /* ages, never applies malformed Identify */
        cases++;
    }
    request[0] = 0xe1;
    memset(request + 5, 0xee, 97);
    CHECK(zcl_id_rx(&ctx, 1000, request, 100, response, 5, &info) == ZCL_CODEC_OK);
    CHECK(memcmp(response, ack, 5) == 0 && ctx.remaining == 3);
    CHECK(fail_rx(1001, 101, 100, ZCL_CODEC_TOO_LONG) == 0);
    request[2] = 1; /* Query defined length zero; trailing data ignored */
    CHECK(zcl_id_rx(&ctx, 1001, request, 100, response, 5, &info) == ZCL_CODEC_OK);
    CHECK(memcmp(response, "\x19\x5a\x00\x03\x00", 5) == 0 && ctx.phase == 1);
    request[0] = 0x11;
    outputs();
    CHECK(zcl_id_rx(&ctx, 4000, request, 100, response, 0, &info) == ZCL_CODEC_OK);
    CHECK(info.kind == 2 && ctx.remaining == 0 && filled(response, sizeof(response), 0xc7));
    request[0] = 1;
    CHECK(zcl_id_rx(&ctx, 4000, request, 3, response, 0, &info) == ZCL_CODEC_OK && info.kind == 2);
    request[0] = 9; request[2] = 0; /* Query Response is for a client, not this server */
    CHECK(fail_rx(4001, 5, 100, ZCL_CODEC_UNSUPPORTED_CONTEXT) == 0);
    request[0] = 2;
    CHECK(fail_rx(4001, 5, 100, ZCL_CODEC_UNSUPPORTED_FRAME_TYPE) == 0);
    memcpy(request, "\x05\x34\x12\x5a\x00\xff\xff", 7);
    for (n = 0; n < 5; n++) CHECK(fail_rx(4001, n, 100, ZCL_CODEC_TRUNCATED) == 0);
    CHECK(fail_rx(4001, 7, 6, ZCL_CODEC_BUFFER_TOO_SMALL) == 0);
    CHECK(zcl_id_rx(&ctx, 4001, request, 7, response, 7, &info) == ZCL_CODEC_OK);
    CHECK(memcmp(response, "\x1c\x34\x12\x5a\x0b\x00\x81", 7) == 0 && ctx.remaining == 0);
    request[0] = 0xe5;
    CHECK(fail_rx(4002, 7, 100, ZCL_CODEC_UNSUPPORTED_LAYOUT) == 0);
    request[0] = 0x0d;
    CHECK(fail_rx(4002, 7, 100, ZCL_CODEC_UNSUPPORTED_CONTEXT) == 0);
    CHECK(zcl_id_rx(&ctx, 4002, start, 5, response, 5, &info) == ZCL_CODEC_OK);
    memcpy(request, "\x01\x5a\x40\x00\x00", 5); /* Trigger Effect remains unsupported */
    CHECK(zcl_id_rx(&ctx, 5002, request, 5, response, 5, &info) == ZCL_CODEC_OK);
    CHECK(memcmp(response, "\x18\x5a\x0b\x40\x81", 5) == 0 && ctx.remaining == 2);
    request[2] = 0xff;
    CHECK(zcl_id_rx(&ctx, 5002, request, 3, response, 5, &info) == ZCL_CODEC_OK);
    CHECK(memcmp(response, "\x18\x5a\x0b\xff\x81", 5) == 0);
    memcpy(request, "\x00\x5a\x02\x00\x00\x21\x00\x00", 8); /* well-formed Write IdentifyTime=0 */
    CHECK(zcl_id_rx(&ctx, 5002, request, 8, response, 5, &info) == ZCL_CODEC_OK);
    CHECK(memcmp(response, "\x18\x5a\x0b\x02\x81", 5) == 0 && ctx.remaining == 2);
    request[2] = 5;
    CHECK(fail_rx(6002, 5, 100, ZCL_CODEC_UNSUPPORTED_NO_RESPONSE) == 0);
    memcpy(request, "\x00\x5a\x0b\x00\x82\xee", 6);
    outputs();
    CHECK(zcl_id_rx(&ctx, 6002, request, 6, response, 0, &info) == ZCL_CODEC_OK);
    CHECK(info.kind == 1 && info.default_raw_status == 0x82 && info.default_status == 0x81);
    CHECK(info.length == 0 && ctx.remaining == 1 && filled(response, sizeof(response), 0xc7));
    CHECK(fail_rx(6003, 4, 100, ZCL_CODEC_TRUNCATED) == 0);
    memcpy(request, "\x00\x5a\x00\x01\x00\xfe\xff\xff\xff", 9);
    CHECK(zcl_id_rx(&ctx, 6003, request, 9, response, 12, &info) == ZCL_CODEC_OK);
    CHECK(memcmp(response, "\x18\x5a\x01\x01\x00\x86\xfe\xff\x86\xff\xff\x86", 12) == 0);
    memcpy(&saved, &ctx, sizeof(ctx));
    outputs();
    CHECK(zcl_id_init(NULL, 0) == ZCL_CODEC_INVALID_ARGUMENT);
    CHECK(zcl_id_tick(NULL, 0) == ZCL_CODEC_INVALID_ARGUMENT);
    CHECK(zcl_id_rx(NULL, 6004, request, 9, response, 100, &info) == ZCL_CODEC_INVALID_ARGUMENT);
    CHECK(zcl_id_rx(&ctx, 6004, NULL, 9, response, 100, &info) == ZCL_CODEC_INVALID_ARGUMENT);
    CHECK(zcl_id_rx(&ctx, 6004, request, 9, NULL, 100, &info) == ZCL_CODEC_INVALID_ARGUMENT);
    CHECK(zcl_id_rx(&ctx, 6004, request, 9, response, 100, NULL) == ZCL_CODEC_INVALID_ARGUMENT);
    CHECK(memcmp(&ctx, &saved, sizeof(ctx)) == 0);
    CHECK(filled(response, sizeof(response), 0xc7) && filled(&info, sizeof(info), 0xa5));
    cases++;
    return 0;
}

static uint16_t run_tests(void)
{
    uint16_t rc;
    cases = 0;
    rc = golden_cases(); if (rc) return rc;
    rc = clock_cases(); if (rc) return rc;
    rc = capacity_cases(); if (rc) return rc;
    return receive_cases();
}

#if defined(__SDCC)
volatile MCU_XDATA MCU_AT(0x1e00) uint8_t zcl_id_result[8];
void main(void)
{
    uint16_t rc = run_tests();
    zcl_id_result[0] = 'Z'; zcl_id_result[1] = 'I';
    zcl_id_result[2] = 'D'; zcl_id_result[3] = '1';
    zcl_id_result[4] = 1; zcl_id_result[5] = 8;
    zcl_id_result[6] = (uint8_t)rc;
    zcl_id_result[7] = (uint8_t)(rc >> 8);
    __asm
        .globl _zcl_id_done
        _zcl_id_done:
        nop
    __endasm;
    for (;;) { }
}
#else
#include <stdio.h>
#include <stdlib.h>

static uint16_t native_clock(void)
{
    uint32_t sec, base, now;
    unsigned k;
    uint8_t cmd[] = {0x11, 0x5a, 0, 0xff, 0xff};
    static const uint16_t offsets[] = {0, 1, 999};
    base = 0xffffff00UL;
    CHECK(zcl_id_init(&ctx, base) == ZCL_CODEC_OK);
    CHECK(zcl_id_rx(&ctx, base, cmd, 5, response, 0, &info) == ZCL_CODEC_OK);
    /* Every boundary of the maximum duration, including wrapped timestamps. */
    for (sec = 0; sec <= 65535UL; sec++) {
        for (k = 0; k < 3; k++) {
            now = base + sec * 1000UL + offsets[k];
            CHECK(zcl_id_tick(&ctx, now) == ZCL_CODEC_OK);
            CHECK(ctx.remaining == 65535UL - sec && ctx.phase == (sec == 65535UL ? 0 : offsets[k]));
        }
    }
    /* Fractional accumulation, mixed commands/ticks and many clock wraps.
     * Independent absolute-deadline oracle, not the production recurrence. */
    {
        uint64_t absolute = 0, deadline = 0;
        uint32_t random = 0x13579bdfUL;
        unsigned i;
        CHECK(zcl_id_init(&ctx, 0) == ZCL_CODEC_OK);
        for (i = 0; i < 20000; i++) {
            random = random * 1664525UL + 1013904223UL;
            absolute += i % 11 == 0 ? 0x7fffffffUL : random % 1973u;
            if (i % 7 == 0) {
                uint16_t duration = (uint16_t)random;
                cmd[3] = (uint8_t)duration; cmd[4] = (uint8_t)(duration >> 8);
                CHECK(zcl_id_rx(&ctx, (uint32_t)absolute, cmd, 5, response, 0, &info) == ZCL_CODEC_OK);
                deadline = absolute + (uint64_t)duration * 1000u;
            } else {
                CHECK(zcl_id_tick(&ctx, (uint32_t)absolute) == ZCL_CODEC_OK);
            }
            CHECK(ctx.stamp == (uint32_t)absolute);
            CHECK(ctx.remaining == (absolute >= deadline ? 0 : (deadline - absolute + 999u) / 1000u));
            CHECK(ctx.phase == (absolute >= deadline ? 0 : (1000u - (deadline - absolute) % 1000u) % 1000u));
        }
    }
    return 0;
}

static uint16_t exact_native(void)
{
    zcl_id_t *c = malloc(sizeof(*c)), *before = malloc(sizeof(*before));
    zcl_dispatch_info_t *result = malloc(sizeof(*result));
    uint8_t *in, *out;
    uint8_t expected[5];
    unsigned mode, n, cap, stored, wanted;
    zcl_codec_result_t rc, want;
    CHECK(c != NULL && before != NULL && result != NULL);
    for (mode = 0; mode < 4; mode++) { /* Identify, active/idle Query, no-default Identify */
        for (n = 0; n <= 102; n++) {
            in = malloc(n ? n : 1);
            CHECK(in != NULL);
            memset(in, 0xee, n ? n : 1);
            if (n) in[0] = mode == 3 ? 0x11 : 1;
            if (n > 1) in[1] = 0x5a;
            if (n > 2) in[2] = mode == 1 || mode == 2 ? 1 : 0;
            if (n > 3) in[3] = 2;
            if (n > 4) in[4] = 0;
            for (cap = 0; cap <= 6; cap++) {
                stored = cap ? cap : 1;
                out = malloc(stored);
                CHECK(out != NULL);
                memset(out, 0xc7, stored); memset(result, 0xa5, sizeof(*result));
                CHECK(zcl_id_init(c, 0) == ZCL_CODEC_OK);
                if (mode != 2) CHECK(zcl_id_rx(c, 0, start, 5, response, 5, &info) == ZCL_CODEC_OK);
                memcpy(before, c, sizeof(*c));
                wanted = mode == 2 || (mode == 3 && n >= 5) ? 2u : 0u;
                want = n > 100 ? ZCL_CODEC_TOO_LONG : n < 3 ? ZCL_CODEC_TRUNCATED :
                       wanted == 0 && cap < 5 ? ZCL_CODEC_BUFFER_TOO_SMALL : ZCL_CODEC_OK;
                rc = zcl_id_rx(c, 1001, in, (uint16_t)n, out, (uint16_t)cap, result);
                CHECK(rc == want);
                if (rc != ZCL_CODEC_OK) {
                    CHECK(memcmp(c, before, sizeof(*c)) == 0);
                    CHECK(filled(out, (uint16_t)stored, 0xc7) && filled(result, sizeof(*result), 0xa5));
                } else {
                    CHECK(result->kind == wanted && result->sequence == 0x5a && c->stamp == 1001);
                    CHECK(c->remaining == (mode == 2 ? 0 : 2));
                    CHECK(c->phase == ((mode == 1 || n < 5) && mode != 2 ? 1 : 0));
                    if (wanted == 2) {
                        CHECK(result->length == 0 && filled(out, (uint16_t)stored, 0xc7));
                    } else {
                        memcpy(expected, ack, 5);
                        if (mode == 1) { expected[0] = 0x19; expected[2] = 0; expected[3] = 2; }
                        else if (n < 5) expected[4] = 0x80;
                        CHECK(result->length == 5 && memcmp(out, expected, 5) == 0);
                        CHECK(filled(out + 5, (uint16_t)(stored - 5), 0xc7));
                    }
                }
                if (n) CHECK(in[0] == (mode == 3 ? 0x11 : 1));
                if (n > 5) CHECK(filled(in + 5, (uint16_t)(n - 5), 0xee));
                free(out);
            }
            free(in);
        }
    }
    free(result); free(before); free(c);
    return 0;
}

static uint16_t native_globals(void)
{
    zcl_id_t *c = malloc(sizeof(*c)), *before = malloc(sizeof(*before));
    zcl_dispatch_info_t *out = malloc(sizeof(*out));
    uint8_t *input, *output;
    uint8_t expected[15];
    unsigned mode, n, cap, used, count;
    zcl_codec_result_t want;
    CHECK(c != NULL && before != NULL && out != NULL);
    for (mode = 0; mode < 2; mode++) {
        for (n = mode ? 7u : 0u; n <= (mode ? 7u : 102u); n++) {
            input = malloc(n ? n : 1);
            CHECK(input != NULL);
            memset(input, 0xee, n ? n : 1);
            memcpy(input, mode ? read : discover, mode ? 7u : n < 6u ? n : 6u);
            for (cap = 0; cap <= 102; cap++) {
                output = malloc(cap ? cap : 1);
                CHECK(output != NULL);
                memset(output, 0xc7, cap ? cap : 1); memset(out, 0xa5, sizeof(*out));
                CHECK(zcl_id_init(c, 0) == ZCL_CODEC_OK);
                CHECK(zcl_id_rx(c, 0, start, 5, response, 5, &info) == ZCL_CODEC_OK);
                memcpy(before, c, sizeof(*c));
                want = n > 100 ? ZCL_CODEC_TOO_LONG : n < 3 ? ZCL_CODEC_TRUNCATED :
                       cap < (mode ? 6u : n < 6 ? 5u : 7u) ? ZCL_CODEC_BUFFER_TOO_SMALL : ZCL_CODEC_OK;
                CHECK(zcl_id_rx(c, 1000, input, (uint16_t)n, output, (uint16_t)cap, out) == want);
                if (want != ZCL_CODEC_OK) {
                    CHECK(memcmp(c, before, sizeof(*c)) == 0);
                    CHECK(filled(output, (uint16_t)(cap ? cap : 1), 0xc7) && filled(out, sizeof(*out), 0xa5));
                } else {
                    CHECK(c->remaining == 2 && c->phase == 0 && c->stamp == 1000);
                    if (mode) {
                        unsigned i;
                        memcpy(expected, read_two, 3);
                        used = 3; count = 0;
                        for (i = 0; i < 2 && used + 3 <= cap; i++) {
                            memcpy(expected + used, read_two + 3 + 6u * i, 2);
                            if (used + 6 <= cap) {
                                memcpy(expected + used + 2, read_two + 5 + 6u * i, 4);
                                used += 6;
                            } else {
                                expected[used + 2] = 0x89; used += 3;
                            }
                            count++;
                        }
                        CHECK(out->requested_count == 2 && out->returned_count == count);
                    } else if (n < 6) {
                        memcpy(expected, "\x18\x5a\x0b\x0c\x80", 5); used = 5;
                    } else {
                        count = (cap - 4u) / 3u; if (count > 2) count = 2;
                        used = 4 + 3u * count;
                        memcpy(expected, discover_two, used);
                        expected[3] = count == 2;
                        CHECK(out->returned_count == count && out->discovery_complete == (count == 2));
                    }
                    CHECK(out->length == used && memcmp(output, expected, used) == 0);
                    CHECK(filled(output + used, (uint16_t)(cap - used), 0xc7));
                }
                CHECK(memcmp(input, mode ? read : discover, mode ? 7u : n < 6 ? n : 6u) == 0);
                if (!mode && n > 6) CHECK(filled(input + 6, (uint16_t)(n - 6), 0xee));
                free(output);
            }
            free(input);
        }
    }
    free(out); free(before); free(c);
    return 0;
}

int main(void)
{
    uint16_t rc = run_tests();
    if (!rc) rc = native_clock();
    if (!rc) rc = exact_native();
    if (!rc) rc = native_globals();
    if (rc) {
        fprintf(stderr, "Identify test failed at C line %u\n", rc);
        return 1;
    }
    printf("Identify: %u common cases; maximum countdown/clock oracle and exact native allocations PASS.\n", cases);
    return 0;
}
#endif
