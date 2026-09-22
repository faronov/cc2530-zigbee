/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "zcl_temperature.h"
#include "cc2530_mmio.h"

#include <stddef.h>
#include <string.h>

#ifndef ZCL_TEMP_PART
#define ZCL_TEMP_PART 0
#endif
#if defined(__SDCC) && (ZCL_TEMP_PART < 1 || ZCL_TEMP_PART > 3)
#error Select the wire (1), configuration (2), or reporting (3) target corpus
#endif
#define CHECK(c) do { if (!(c)) return (uint16_t)__LINE__; } while (0)

static const MCU_CODE zcl_temp_cfg_t defaults = {3, 10, 50};
static zcl_temp_t ctx, saved;
static zcl_dispatch_info_t info;
static zcl_temp_report_t report;
static uint8_t request[102], response[102];
static uint16_t checks;

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
    memset(&report, 0xa5, sizeof(report));
}

static zcl_codec_result_t reset(uint32_t now)
{
    return zcl_temp_init(&ctx, now, -27315, 32767, &defaults);
}

static zcl_codec_result_t rx(uint32_t now, const uint8_t *p, uint8_t n, uint8_t cap)
{
    return zcl_temp_rx(&ctx, now, p, n, response + 1, cap, &info);
}

static zcl_codec_result_t prepare(uint32_t now, uint8_t routes, uint8_t cap)
{
    return zcl_temp_prepare(&ctx, now, 0x5a, routes, response + 1, cap, &report);
}

static uint16_t fail_rx(uint16_t n, uint16_t cap, zcl_codec_result_t expected)
{
    saved = ctx;
    outputs();
    CHECK(zcl_temp_rx(&ctx, ctx.stamp + 1u, request, n, response + 1, cap, &info) == expected);
    CHECK(memcmp(&ctx, &saved, sizeof(ctx)) == 0);
    CHECK(filled(response, sizeof(response), 0xc7) && filled(&info, sizeof(info), 0xa5));
    return 0;
}

#if ZCL_TEMP_PART == 0 || ZCL_TEMP_PART == 1
/* n, capacity, result, output length, independent request and reply octets. */
static const MCU_CODE uint8_t wire[] = {
    5,9,0,9, 0,0x5a,0,0,0, 0x18,0x5a,1,0,0,0,0x29,0,0x80,
    13,30,0,30, 0,0x5a,0,0,0,1,0,2,0,3,0,0xfd,0xff,
      0x18,0x5a,1,0,0,0,0x29,0,0x80,1,0,0,0x29,0x4d,0x95,
      2,0,0,0x29,0xff,0x7f,3,0,0x86,0xfd,0xff,0,0x21,3,0,
    6,16,0,16, 0,0x5a,0x0c,0,0,0xff,
      0x18,0x5a,0x0d,1,0,0,0x29,1,0,0x29,2,0,0x29,0xfd,0xff,0x21,
    7,7,0,7, 0xe0,0x5a,0x0c,0,0,1,0xaa, 0x18,0x5a,0x0d,0,0,0,0x29,
    5,6,0,6, 0,0x5a,0,0,0, 0x18,0x5a,1,0,0,0x89,
    4,5,0,5, 0,0x5a,0,0, 0x18,0x5a,0x0b,0,0x80,
    3,5,0,5, 0,0x5a,0, 0x18,0x5a,0x0b,0,0x80,
    8,6,0,6, 0,0x5a,2,0,0,0x29,0,0, 0x18,0x5a,4,0x88,0,0,
    8,6,0,6, 0,0x5a,3,0,0,0x29,0,0, 0x18,0x5a,4,0x88,0,0,
    8,0,0,0, 0,0x5a,5,0,0,0x29,0,0,
    8,6,0,6, 0,0x5a,2,3,0,0x29,0,0, 0x18,0x5a,4,0x86,3,0,
    7,6,0,6, 0,0x5a,2,0,0,0x10,2, 0x18,0x5a,4,0x8d,0,0,
    7,6,0,6, 0,0x5a,2,3,0,0x10,2, 0x18,0x5a,4,0x86,3,0,
    8,6,0,6, 0xf0,0x5a,2,0xfd,0xff,0x21,0,0, 0x18,0x5a,4,0x88,0xfd,0xff,
    9,5,0,5, 0,0x5a,2,0,0,0x29,0,0,0xaa, 0x18,0x5a,0x0b,2,0x80,
    9,0,ZCL_CODEC_TRUNCATED,0, 0,0x5a,5,0,0,0x29,0,0,0xaa,
    6,100,ZCL_CODEC_UNSUPPORTED_DATA_TYPE,0, 0,0x5a,2,0,0,0xff,
    3,5,0,5, 1,0x5a,0, 0x18,0x5a,0x0b,0,0x81,
    3,5,0,5, 0x11,0x5a,0x40, 0x18,0x5a,0x0b,0x40,0x81,
    5,7,0,7, 4,0x34,0x12,0x5a,6, 0x1c,0x34,0x12,0x5a,0x0b,6,0x81,
    5,7,0,7, 4,0x34,0x12,0x5a,8, 0x1c,0x34,0x12,0x5a,0x0b,8,0x81,
    3,100,ZCL_CODEC_UNSUPPORTED_CONTEXT,0, 8,0x5a,6,
    3,100,ZCL_CODEC_UNSUPPORTED_FRAME_TYPE,0, 2,0x5a,6,
    0
};

static uint16_t wire_cases(void)
{
    const uint8_t * volatile p = wire;
    volatile uint8_t n, cap, expected, out, i;
    while ((n = *p++) != 0) {
        cap = *p++; expected = *p++; out = *p++;
        CHECK(reset(0) == ZCL_CODEC_OK);
        saved = ctx;
        outputs();
        CHECK(rx(1, p, n, cap) == expected);
        if (expected) {
            CHECK(memcmp(&ctx, &saved, sizeof(ctx)) == 0);
            CHECK(filled(&info, sizeof(info), 0xa5));
        } else {
            saved.stamp = 1; saved.age = 1;
            CHECK(memcmp(&ctx, &saved, sizeof(ctx)) == 0);
            CHECK(info.length == out && info.sequence == 0x5a);
            CHECK(info.kind == (p[2] == 5 ? 2 : 0));
        }
        CHECK(memcmp(response + 1, p + n, out) == 0);
        CHECK(response[0] == 0xc7 && filled(response + out + 1, (uint16_t)(101u - out), 0xc7));
        p += n + out;
        checks++;
    }
    memcpy(request, "\0\x5a\0\0\0", 5);
    for (i = 0; i < 3; i++) CHECK(fail_rx(i, 100, ZCL_CODEC_TRUNCATED) == 0);
    CHECK(fail_rx(101, 100, ZCL_CODEC_TOO_LONG) == 0);
    for (i = 0; i < 6; i++) CHECK(fail_rx(5, i, ZCL_CODEC_BUFFER_TOO_SMALL) == 0);
    CHECK(zcl_temp_sample(&ctx, 1, -1) == ZCL_CODEC_OK);
    CHECK(rx(1, request, 5, 9) == ZCL_CODEC_OK);
    CHECK(memcmp(response + 1, "\x18\x5a\x01\0\0\0\x29\xff\xff", 9) == 0);
    return 0;
}

static uint16_t range_cases(void)
{
    static const MCU_CODE int16_t bounds[][2] = {
        {-27315,32767}, {-27315,-27314}, {32766,32767},
        {ZCL_TEMP_UNKNOWN,ZCL_TEMP_UNKNOWN}, {ZCL_TEMP_UNKNOWN,-27314}, {32766,ZCL_TEMP_UNKNOWN}
    };
    static const MCU_CODE int16_t bad[][2] = {
        {-27316,32767}, {32767,32767}, {-27315,-27315}, {0,0}, {1,0}
    };
    zcl_temp_cfg_t cfg;
    volatile uint8_t i;
    for (i = 0; i < 6; i++) {
        CHECK(zcl_temp_init(&ctx, 5, bounds[i][0], bounds[i][1], &defaults) == ZCL_CODEC_OK);
        CHECK(ctx.value == ZCL_TEMP_UNKNOWN && ctx.baseline == ZCL_TEMP_UNKNOWN && ctx.stamp == 5);
        CHECK(ctx.age == 0 && ctx.serial == 0 && !ctx.pending && !ctx.fault && ctx.configured);
        checks++;
    }
    saved = ctx;
    for (i = 0; i < 5; i++) {
        CHECK(zcl_temp_init(&ctx, 0, bad[i][0], bad[i][1], &defaults) == ZCL_CODEC_INVALID_VALUE);
        CHECK(memcmp(&ctx, &saved, sizeof(ctx)) == 0);
        checks++;
    }
    CHECK(reset(0) == ZCL_CODEC_OK);
    CHECK(zcl_temp_sample(&ctx, 0, -27315) == ZCL_CODEC_OK);
    CHECK(zcl_temp_sample(&ctx, 0, 32767) == ZCL_CODEC_OK);
    CHECK(zcl_temp_sample(&ctx, 0, ZCL_TEMP_UNKNOWN) == ZCL_CODEC_OK);
    saved = ctx;
    CHECK(zcl_temp_sample(&ctx, 1, -27316) == ZCL_CODEC_INVALID_VALUE);
    CHECK(memcmp(&ctx, &saved, sizeof(ctx)) == 0);
    CHECK(zcl_temp_init(&ctx, 0, -1, 1, &defaults) == ZCL_CODEC_OK);
    CHECK(zcl_temp_sample(&ctx, 0, -1) == ZCL_CODEC_OK && ctx.value == -1);
    saved = ctx;
    CHECK(zcl_temp_sample(&ctx, 1, -2) == ZCL_CODEC_INVALID_VALUE);
    CHECK(zcl_temp_sample(&ctx, 1, 2) == ZCL_CODEC_INVALID_VALUE);
    CHECK(memcmp(&ctx, &saved, sizeof(ctx)) == 0);
    cfg = defaults; cfg.minimum = 11;
    CHECK(zcl_temp_init(&ctx, 0, -1, 1, &cfg) == ZCL_CODEC_INVALID_VALUE);
    cfg.minimum = 0xffff; cfg.maximum = 0;
    CHECK(zcl_temp_init(&ctx, 0, -1, 1, &cfg) == ZCL_CODEC_INVALID_VALUE);
    cfg = defaults; cfg.change = ZCL_TEMP_UNKNOWN;
    CHECK(zcl_temp_init(&ctx, 0, -1, 1, &cfg) == ZCL_CODEC_INVALID_VALUE);
    CHECK(memcmp(&ctx, &saved, sizeof(ctx)) == 0);
    cfg.maximum = 0xffff;
    CHECK(zcl_temp_init(&ctx, 0, -1, 1, &cfg) == ZCL_CODEC_OK);
    CHECK(!ctx.configured && ctx.defaults.change == 0 && ctx.reporting.change == 0);
    CHECK(zcl_temp_init(NULL, 0, -1, 1, &defaults) == ZCL_CODEC_INVALID_ARGUMENT);
    CHECK(zcl_temp_init(&ctx, 0, -1, 1, NULL) == ZCL_CODEC_INVALID_ARGUMENT);
    CHECK(zcl_temp_sample(NULL, 0, 0) == ZCL_CODEC_INVALID_ARGUMENT);
    return 0;
}
#endif

#if ZCL_TEMP_PART == 0 || ZCL_TEMP_PART == 2
static const MCU_CODE uint8_t config[] = {0,0x5a,6,0,0,0,0x29,2,0,9,0,0xfb,0xff};
static const MCU_CODE uint8_t read_config[] = {0,0x5a,8,0,0,0,0,1,0,1,0,0,0,3,0};
/* Primary Table2-11 types and their threshold widths, not value widths. */
static const MCU_CODE uint8_t types[][2] = {
    {0,0},{8,0},{0x10,0},{0x18,0},{0x1f,0},{0x30,0},{0x31,0},
    {0x41,0},{0x42,0},{0x43,0},{0x44,0},{0x48,0},{0x4c,0},{0x50,0},{0x51,0},
    {0xe8,0},{0xe9,0},{0xea,0},{0xf0,0},{0xf1,0},
    {0x20,1},{0x21,2},{0x22,3},{0x23,4},{0x24,5},{0x25,6},{0x26,7},{0x27,8},
    {0x28,1},{0x29,2},{0x2a,3},{0x2b,4},{0x2c,5},{0x2d,6},{0x2e,7},{0x2f,8},
    {0x38,2},{0x39,4},{0x3a,8},{0xe0,4},{0xe1,4},{0xe2,4}
};

static uint16_t config_types(void)
{
    volatile uint8_t i, j, n, status, out;
    for (i = 0; i < sizeof(types)/sizeof(types[0]); i++) {
        for (j = 0; j < 3; j++) {
            CHECK(reset(0) == ZCL_CODEC_OK);
            memset(request, 0, sizeof(request));
            memcpy(request, config, 11);
            request[4] = j;
            request[6] = types[i][0];
            n = (uint8_t)(11u + types[i][1]);
            status = j == 2 ? 0x8c : j == 1 ? 0x8c
                : types[i][0] == 0x29 ? 0
                : (types[i][0] == 0x48 || types[i][0] == 0x4c
                   || types[i][0] == 0x50 || types[i][0] == 0x51) ? 0x8c : 0x8d;
            if (j == 2) { request[4] = 3; status = 0x86; }
            outputs();
            out = status ? 7 : 4;
            CHECK(rx(1, request, n, out) == ZCL_CODEC_OK);
            CHECK(info.length == out && info.command_id == 7 && info.requested_count == 1 && info.returned_count == 1);
            CHECK(memcmp(response + 1, "\x18\x5a\x07", 3) == 0 && response[4] == status);
            if (status) CHECK(response[5] == 0 && response[6] == request[4] && response[7] == 0);
            CHECK(ctx.reporting.minimum == (status ? 3 : 2) && ctx.age == (status ? 1 : 0));
            CHECK(response[0] == 0xc7 && filled(response + out + 1, (uint16_t)(101u - out), 0xc7));
            if (types[i][1]) {
                saved = ctx;
                CHECK(rx(1, request, (uint8_t)(n - 1u), 5) == ZCL_CODEC_OK);
                CHECK(memcmp(response + 1, "\x18\x5a\x0b\x06\x80", 5) == 0);
                CHECK(memcmp(&ctx, &saved, sizeof(ctx)) == 0);
            }
            checks++;
        }
    }
    memcpy(request, config, 13);
    request[6] = 0xff;
    CHECK(fail_rx(13, 100, ZCL_CODEC_UNSUPPORTED_DATA_TYPE) == 0);
    request[6] = 0x17;
    CHECK(fail_rx(13, 100, ZCL_CODEC_UNSUPPORTED_DATA_TYPE) == 0);
    return 0;
}

static uint16_t config_cases(void)
{
    volatile uint8_t n, cap;
    CHECK(reset(0) == ZCL_CODEC_OK);
    CHECK(zcl_temp_sample(&ctx, 0, -1) == ZCL_CODEC_OK);
    memcpy(request, config, 13);
    for (cap = 0; cap < 4; cap++) CHECK(fail_rx(13, cap, ZCL_CODEC_BUFFER_TOO_SMALL) == 0);
    for (n = 3; n < 13; n++) {
        saved = ctx;
        CHECK(rx(0, request, n, 5) == ZCL_CODEC_OK);
        CHECK(memcmp(response + 1, "\x18\x5a\x0b\x06\x80", 5) == 0);
        CHECK(memcmp(&ctx, &saved, sizeof(ctx)) == 0);
    }
    CHECK(rx(7, request, 13, 4) == ZCL_CODEC_OK);
    CHECK(memcmp(response + 1, "\x18\x5a\x07\0", 4) == 0);
    CHECK(ctx.reporting.minimum == 2 && ctx.reporting.maximum == 9 && ctx.reporting.change == -5);
    CHECK(ctx.baseline == -1 && ctx.age == 0 && ctx.stamp == 7);
    CHECK(rx(7, read_config, 15, 26) == ZCL_CODEC_OK);
    CHECK(info.requested_count == 4 && info.returned_count == 4 && info.length == 26);
    CHECK(memcmp(response + 1, "\x18\x5a\x09\0\0\0\0\x29\x02\0\x09\0\xfb\xff"
          "\x8c\0\x01\0\x86\x01\0\0\x86\0\x03\0", 26) == 0);
    for (cap = 0; cap < 14; cap++) {
        memcpy(request, read_config, 15);
        CHECK(fail_rx(15, cap, ZCL_CODEC_BUFFER_TOO_SMALL) == 0);
    }
    CHECK(rx(7, read_config, 15, 17) == ZCL_CODEC_OK);
    CHECK(info.length == 14 && info.returned_count == 1 && info.requested_count == 4);
    CHECK(rx(7, read_config, 15, 18) == ZCL_CODEC_OK);
    CHECK(info.length == 18 && info.returned_count == 2);
    memcpy(request, config, 13);
    memcpy(request + 13, config + 3, 10);
    request[17] = 4; /* second accepted duplicate restarts using the last values */
    CHECK(rx(8, request, 23, 4) == ZCL_CODEC_OK && ctx.reporting.minimum == 4 && ctx.age == 0);
    request[14] = 3; /* missing attribute does not undo the first accepted record */
    CHECK(rx(9, request, 23, 7) == ZCL_CODEC_OK && ctx.reporting.minimum == 2 && ctx.age == 0);
    CHECK(memcmp(response + 1, "\x18\x5a\x07\x86\0\x03\0", 7) == 0);
    request[14] = 0; request[17] = 8; request[23] = 0;
    saved = ctx;
    CHECK(rx(9, request, 24, 5) == ZCL_CODEC_OK);
    CHECK(memcmp(response + 1, "\x18\x5a\x0b\x06\x80", 5) == 0);
    CHECK(memcmp(&ctx, &saved, sizeof(ctx)) == 0);
    request[23] = 2; /* reserved direction even beyond a would-be output prefix */
    CHECK(rx(9, request, 26, 5) == ZCL_CODEC_OK);
    CHECK(memcmp(&ctx, &saved, sizeof(ctx)) == 0);
    memcpy(request, config, 13);
    request[7] = 10;
    CHECK(rx(9, request, 13, 7) == ZCL_CODEC_OK && response[4] == 0x87);
    request[7] = 2; request[11] = 0; request[12] = 0x80;
    CHECK(rx(9, request, 13, 7) == ZCL_CODEC_OK && response[4] == 0x87);
    request[9] = request[10] = 0xff; /* special disable ignores NaS change */
    CHECK(rx(9, request, 13, 4) == ZCL_CODEC_OK && !ctx.configured);
    CHECK(rx(9, read_config, 6, 7) == ZCL_CODEC_OK);
    CHECK(memcmp(response + 1, "\x18\x5a\x09\x8b\0\0\0", 7) == 0);
    request[7] = request[8] = 0xff; request[9] = request[10] = 0;
    CHECK(rx(9, request, 13, 4) == ZCL_CODEC_OK && ctx.configured);
    CHECK(memcmp(&ctx.reporting, &defaults, sizeof(defaults)) == 0 && ctx.baseline == -1);
    request[7] = request[8] = 0; request[11] = request[12] = 0;
    CHECK(rx(9, request, 13, 4) == ZCL_CODEC_OK && ctx.reporting.maximum == 0);
    memcpy(request, "\0\x5a\x06\x01\0\0\xff\xff", 8);
    CHECK(rx(9, request, 8, 7) == ZCL_CODEC_OK);
    CHECK(memcmp(response + 1, "\x18\x5a\x07\x8c\x01\0\0", 7) == 0);
    /* Maximum-size error list and read prefix; partial trailing records are errors. */
    request[2] = 8;
    for (n = 3; n < 99; n += 3) { request[n] = 0; request[n+1] = 3; request[n+2] = 0; }
    CHECK(rx(9, request, 99, 100) == ZCL_CODEC_OK);
    CHECK(info.requested_count == 32 && info.returned_count == 24 && info.length == 99);
    request[99] = 0;
    CHECK(rx(9, request, 100, 7) == ZCL_CODEC_OK);
    CHECK(memcmp(response + 1, "\x18\x5a\x0b\x08\x80", 5) == 0);
    return 0;
}
#endif

#if ZCL_TEMP_PART == 0 || ZCL_TEMP_PART == 3
static uint16_t fail_prepare(uint32_t now, uint8_t routes, uint8_t cap, zcl_codec_result_t expected)
{
    saved = ctx; outputs();
    CHECK(prepare(now, routes, cap) == expected);
    CHECK(memcmp(&ctx, &saved, sizeof(ctx)) == 0);
    CHECK(filled(response, sizeof(response), 0xc7) && filled(&report, sizeof(report), 0xa5));
    return 0;
}

static uint16_t reporting_cases(void)
{
    volatile uint8_t cap;
    volatile uint16_t token;
    zcl_temp_cfg_t cfg;
    CHECK(reset(0) == ZCL_CODEC_OK);
    outputs();
    CHECK(prepare(9, 1, 0) == ZCL_CODEC_OK && !report.ready && !report.token && !report.length);
    CHECK(filled(response, sizeof(response), 0xc7));
    CHECK(prepare(10, 0, 0) == ZCL_CODEC_OK && !report.ready && ctx.age == 10);
    for (cap = 0; cap < 8; cap++) CHECK(fail_prepare(10, 1, cap, ZCL_CODEC_BUFFER_TOO_SMALL) == 0);
    CHECK(prepare(10, 1, 8) == ZCL_CODEC_OK && report.ready && report.length == 8 && report.token == 1);
    CHECK(memcmp(response + 1, "\x18\x5a\x0a\0\0\x29\0\x80", 8) == 0);
    CHECK(ctx.baseline == ZCL_TEMP_UNKNOWN && ctx.pending && ctx.pending_at == 10);
    CHECK(fail_prepare(10, 1, 8, ZCL_CODEC_UNSUPPORTED_CONTEXT) == 0);
    memcpy(request, "\0\x5a\x06\0\0\0\x29\0\0\x01\0\0\0", 13);
    CHECK(fail_rx(13, 100, ZCL_CODEC_UNSUPPORTED_CONTEXT) == 0);
    CHECK(zcl_temp_sample(&ctx, 11, 1234) == ZCL_CODEC_OK);
    CHECK(rx(11, (const uint8_t *)"\0\x5a\0\0\0", 5, 9) == ZCL_CODEC_OK && ctx.pending);
    saved = ctx;
    CHECK(zcl_temp_finish(&ctx, 11, 2, 1, 10) == ZCL_CODEC_INVALID_VALUE);
    CHECK(zcl_temp_finish(&ctx, 11, 1, 1, 9) == ZCL_CODEC_INVALID_VALUE);
    CHECK(zcl_temp_finish(&ctx, 11, 1, 1, 12) == ZCL_CODEC_INVALID_VALUE);
    CHECK(zcl_temp_finish(&ctx, 11, 1, 0, 10) == ZCL_CODEC_INVALID_VALUE);
    CHECK(zcl_temp_finish(&ctx, 11, 1, 2, 10) == ZCL_CODEC_INVALID_VALUE);
    CHECK(memcmp(&ctx, &saved, sizeof(ctx)) == 0);
    CHECK(zcl_temp_finish(&ctx, 12, 1, 1, 10) == ZCL_CODEC_OK);
    CHECK(ctx.baseline == ZCL_TEMP_UNKNOWN && ctx.value == 1234 && ctx.age == 2 && !ctx.pending);
    CHECK(prepare(12, 1, 8) == ZCL_CODEC_OK && !report.ready);
    CHECK(prepare(13, 1, 8) == ZCL_CODEC_OK && report.ready && report.token == 2);
    CHECK(memcmp(response + 1, "\x18\x5a\x0a\0\0\x29\xd2\x04", 8) == 0);
    CHECK(zcl_temp_finish(&ctx, 14, 2, 0, 0) == ZCL_CODEC_OK && ctx.age == 4);
    CHECK(ctx.baseline == ZCL_TEMP_UNKNOWN && !ctx.pending);
    CHECK(prepare(14, 1, 8) == ZCL_CODEC_OK && report.ready && report.token == 3);
    CHECK(zcl_temp_finish(&ctx, 14, 3, 1, 14) == ZCL_CODEC_OK && ctx.baseline == 1234 && ctx.age == 0);
    saved = ctx;
    CHECK(zcl_temp_finish(&ctx, 14, 3, 1, 14) == ZCL_CODEC_INVALID_VALUE);
    CHECK(memcmp(&ctx, &saved, sizeof(ctx)) == 0);
    CHECK(zcl_temp_sample(&ctx, 16, 1284) == ZCL_CODEC_OK);
    CHECK(prepare(16, 1, 0) == ZCL_CODEC_OK && !report.ready);
    CHECK(zcl_temp_sample(&ctx, 17, 1283) == ZCL_CODEC_OK);
    CHECK(prepare(17, 1, 0) == ZCL_CODEC_OK && !report.ready);
    CHECK(zcl_temp_sample(&ctx, 17, 1184) == ZCL_CODEC_OK);
    CHECK(prepare(17, 1, 8) == ZCL_CODEC_OK && report.ready);
    token = report.token;
    CHECK(zcl_temp_sample(&ctx, 18, 2000) == ZCL_CODEC_OK);
    CHECK(zcl_temp_finish(&ctx, 20, token, 1, 18) == ZCL_CODEC_OK);
    CHECK(ctx.baseline == 1184 && ctx.value == 2000 && ctx.age == 2);
    CHECK(prepare(20, 1, 0) == ZCL_CODEC_OK && !report.ready);
    CHECK(prepare(21, 1, 8) == ZCL_CODEC_OK && report.ready);
    CHECK(zcl_temp_finish(&ctx, 21, report.token, 1, 21) == ZCL_CODEC_OK);
    CHECK(zcl_temp_sample(&ctx, 24, ZCL_TEMP_UNKNOWN) == ZCL_CODEC_OK);
    CHECK(prepare(24, 1, 8) == ZCL_CODEC_OK && report.ready);
    CHECK(zcl_temp_finish(&ctx, 24, report.token, 1, 24) == ZCL_CODEC_OK);
    CHECK(prepare(33, 1, 0) == ZCL_CODEC_OK && !report.ready);
    CHECK(prepare(34, 1, 8) == ZCL_CODEC_OK && report.ready);
    CHECK(zcl_temp_finish(&ctx, 34, report.token, 0, 0) == ZCL_CODEC_OK);
    cfg.minimum = cfg.maximum = 0; cfg.change = -32767;
    CHECK(zcl_temp_init(&ctx, 0, -27315, 32767, &cfg) == ZCL_CODEC_OK);
    CHECK(zcl_temp_sample(&ctx, 0, -27315) == ZCL_CODEC_OK);
    CHECK(prepare(0, 1, 8) == ZCL_CODEC_OK && report.ready);
    CHECK(zcl_temp_finish(&ctx, 0, report.token, 1, 0) == ZCL_CODEC_OK);
    CHECK(zcl_temp_sample(&ctx, 0, 5451) == ZCL_CODEC_OK);
    CHECK(prepare(0, 1, 0) == ZCL_CODEC_OK && !report.ready);
    CHECK(zcl_temp_sample(&ctx, 0, 5452) == ZCL_CODEC_OK);
    CHECK(prepare(0, 1, 8) == ZCL_CODEC_OK && report.ready);
    CHECK(zcl_temp_finish(&ctx, 0, report.token, 1, 0) == ZCL_CODEC_OK);
    CHECK(zcl_temp_sample(&ctx, 0, -27315) == ZCL_CODEC_OK);
    CHECK(prepare(0, 1, 8) == ZCL_CODEC_OK && report.ready);
    CHECK(zcl_temp_finish(&ctx, 0, report.token, 1, 0) == ZCL_CODEC_OK);
    CHECK(zcl_temp_sample(&ctx, 0, 32767) == ZCL_CODEC_OK);
    CHECK(prepare(0, 1, 8) == ZCL_CODEC_OK && report.ready); /* delta exceeds INT16 */
    CHECK(zcl_temp_finish(&ctx, 0, report.token, 0, 0) == ZCL_CODEC_OK);
    cfg.change = 0;
    CHECK(zcl_temp_init(&ctx, 0, -27315, 32767, &cfg) == ZCL_CODEC_OK);
    CHECK(prepare(65535UL, 1, 0) == ZCL_CODEC_OK && !report.ready); /* no periodic maximum */
    CHECK(zcl_temp_sample(&ctx, 65535UL, 0) == ZCL_CODEC_OK);
    CHECK(prepare(65535UL, 1, 8) == ZCL_CODEC_OK && report.ready);
    CHECK(zcl_temp_finish(&ctx, 65535UL, report.token, 1, 65535UL) == ZCL_CODEC_OK);
    CHECK(prepare(65536UL, 1, 0) == ZCL_CODEC_OK && !report.ready); /* zero threshold is still a change */
    CHECK(fail_prepare(65536UL, 2, 8, ZCL_CODEC_INVALID_VALUE) == 0);
    return 0;
}

static uint16_t clock_cases(void)
{
    zcl_temp_cfg_t cfg;
    volatile uint32_t base;
    volatile uint8_t i;
    for (i = 0; i < 2; i++) {
        base = i ? 0xfffffffbUL : 100UL;
        CHECK(reset(base) == ZCL_CODEC_OK);
        CHECK(prepare(base + 9u, 1, 0) == ZCL_CODEC_OK && !report.ready);
        CHECK(prepare(base + 10u, 1, 8) == ZCL_CODEC_OK && report.ready);
        CHECK(zcl_temp_finish(&ctx, base + 12u, report.token, 1, base + 11u) == ZCL_CODEC_OK);
        CHECK(ctx.age == 1 && ctx.stamp == base + 12u);
        CHECK(prepare(base + 20u, 1, 0) == ZCL_CODEC_OK && !report.ready);
        CHECK(prepare(base + 21u, 1, 8) == ZCL_CODEC_OK && report.ready);
        CHECK(zcl_temp_finish(&ctx, base + 21u + 65535UL, report.token, 0, 0) == ZCL_CODEC_OK);
        CHECK(ctx.age == 65535u);
        CHECK(prepare(ctx.stamp, 1, 8) == ZCL_CODEC_OK && report.ready);
        saved = ctx; outputs();
        CHECK(zcl_temp_sample(&ctx, ctx.stamp + 65536UL, 0) == ZCL_CODEC_INVALID_VALUE);
        saved.fault = ZCL_TEMP_FAULT_TIME;
        CHECK(memcmp(&ctx, &saved, sizeof(ctx)) == 0);
        CHECK(zcl_temp_sample(&ctx, ctx.stamp, 0) == ZCL_CODEC_INVALID_VALUE);
        CHECK(memcmp(&ctx, &saved, sizeof(ctx)) == 0);
    }
    CHECK(reset(0) == ZCL_CODEC_OK);
    CHECK(zcl_temp_sample(&ctx, 0x7fffffffUL, 0) == ZCL_CODEC_OK && ctx.age == 65535u);
    CHECK(zcl_temp_sample(&ctx, 0xfffffffeUL, 0) == ZCL_CODEC_OK && ctx.age == 65535u);
    saved = ctx;
    CHECK(zcl_temp_sample(&ctx, 0xfffffffdUL, 0) == ZCL_CODEC_INVALID_VALUE);
    saved.fault = ZCL_TEMP_FAULT_TIME;
    CHECK(memcmp(&ctx, &saved, sizeof(ctx)) == 0);
    CHECK(reset(0) == ZCL_CODEC_OK);
    saved = ctx; outputs();
    CHECK(prepare(0x80000000UL, 1, 8) == ZCL_CODEC_INVALID_VALUE);
    saved.fault = ZCL_TEMP_FAULT_TIME;
    CHECK(memcmp(&ctx, &saved, sizeof(ctx)) == 0);
    CHECK(filled(response, sizeof(response), 0xc7) && filled(&report, sizeof(report), 0xa5));
    cfg.minimum = 65534u; cfg.maximum = 0; cfg.change = 0;
    CHECK(zcl_temp_init(&ctx, 0, -27315, 32767, &cfg) == ZCL_CODEC_OK);
    CHECK(zcl_temp_sample(&ctx, 0, 0) == ZCL_CODEC_OK);
    CHECK(prepare(65533UL, 1, 0) == ZCL_CODEC_OK && !report.ready);
    CHECK(prepare(65534UL, 1, 8) == ZCL_CODEC_OK && report.ready);
    CHECK(zcl_temp_finish(&ctx, 65534UL, report.token, 1, 65534UL) == ZCL_CODEC_OK);
    /* Reachable boundary fixture; native also drives all 65535 real leases. */
    ctx.serial = 65534u;
    CHECK(zcl_temp_sample(&ctx, 65535UL, 1) == ZCL_CODEC_OK);
    CHECK(prepare(131068UL, 1, 8) == ZCL_CODEC_OK && report.token == 65535u);
    CHECK(zcl_temp_finish(&ctx, 131068UL, report.token, 0, 0) == ZCL_CODEC_OK);
    saved = ctx; outputs();
    CHECK(prepare(131068UL, 1, 8) == ZCL_CODEC_INVALID_VALUE);
    saved.fault = ZCL_TEMP_FAULT_TOKEN;
    CHECK(memcmp(&ctx, &saved, sizeof(ctx)) == 0);
    CHECK(filled(response, sizeof(response), 0xc7) && filled(&report, sizeof(report), 0xa5));
    CHECK(zcl_temp_finish(NULL, 0, 0, 0, 0) == ZCL_CODEC_INVALID_ARGUMENT);
    CHECK(zcl_temp_prepare(NULL, 0, 0, 0, response, 0, &report) == ZCL_CODEC_INVALID_ARGUMENT);
    return 0;
}
#endif

static uint16_t run_tests(void)
{
    uint16_t rc;
    checks = 0;
#if ZCL_TEMP_PART == 0 || ZCL_TEMP_PART == 1
    rc = wire_cases(); if (rc) return rc; checks++;
    rc = range_cases(); if (rc) return rc; checks++;
#endif
#if ZCL_TEMP_PART == 0 || ZCL_TEMP_PART == 2
    rc = config_types(); if (rc) return rc; checks++;
    rc = config_cases(); if (rc) return rc; checks++;
#endif
#if ZCL_TEMP_PART == 0 || ZCL_TEMP_PART == 3
    rc = reporting_cases(); if (rc) return rc; checks++;
    rc = clock_cases(); if (rc) return rc; checks++;
#endif
    return 0;
}

#if defined(__SDCC)
volatile MCU_XDATA MCU_AT(0x1e00) uint8_t zcl_temp_result[8];
void main(void)
{
    uint16_t rc = run_tests();
    zcl_temp_result[0] = 'Z'; zcl_temp_result[1] = 'T';
    zcl_temp_result[2] = ZCL_TEMP_PART; zcl_temp_result[3] = 1;
    zcl_temp_result[4] = 1; zcl_temp_result[5] = 8;
    zcl_temp_result[6] = (uint8_t)rc; zcl_temp_result[7] = (uint8_t)(rc >> 8);
    __asm
        .globl _zcl_temp_done
        _zcl_temp_done:
        nop
    __endasm;
    for (;;) { }
}
#else
#include <stdio.h>
#include <stdlib.h>

static uint16_t exact_native(void)
{
    static const uint8_t bytes[] = {0,0x5a,6,0,0,0,0x29,2,0,9,0,5,0};
    zcl_temp_t *c = malloc(sizeof(*c)), before;
    zcl_dispatch_info_t *result = malloc(sizeof(*result));
    uint8_t *input, *out, expected[5];
    unsigned n, cap, mode, used;
    zcl_codec_result_t want, got;
    CHECK(c != NULL && result != NULL);
    for (mode = 0; mode < 4; mode++) {
        for (n = 0; n <= 102; n++) {
            input = malloc(n ? n : 1);
            CHECK(input != NULL);
            memset(input, 0xaa, n ? n : 1);
            memcpy(input, bytes, n < sizeof(bytes) ? n : sizeof(bytes));
            if (n) input[0] = (uint8_t)((mode & 1u ? 0x10 : 0) | (mode & 2u ? 0xe0 : 0));
            for (cap = 0; cap <= 8; cap++) {
                out = malloc(cap ? cap : 1);
                CHECK(out != NULL);
                memset(out, 0xc7, cap ? cap : 1); memset(result, 0xa5, sizeof(*result));
                CHECK(zcl_temp_init(c, 0, -27315, 32767, &defaults) == ZCL_CODEC_OK);
                before = *c;
                used = n == 13 ? 4 : 5;
                want = n > 100 ? ZCL_CODEC_TOO_LONG : n < 3 ? ZCL_CODEC_TRUNCATED
                    : cap < used ? ZCL_CODEC_BUFFER_TOO_SMALL : ZCL_CODEC_OK;
                got = zcl_temp_rx(c, 7, input, (uint16_t)n, out, (uint16_t)cap, result);
                CHECK(got == want);
                if (got) {
                    CHECK(memcmp(c, &before, sizeof(before)) == 0);
                    CHECK(filled(result, sizeof(*result), 0xa5) && filled(out, (uint16_t)(cap ? cap : 1), 0xc7));
                } else {
                    expected[0] = 0x18; expected[1] = 0x5a;
                    expected[2] = n == 13 ? 7 : 0x0b;
                    expected[3] = n == 13 ? 0 : 6; expected[4] = 0x80;
                    CHECK(memcmp(out, expected, used) == 0);
                    CHECK(result->length == used && result->sequence == 0x5a && result->kind == 0);
                    CHECK(c->stamp == 7 && c->age == (n == 13 ? 0 : 7));
                    CHECK(c->reporting.minimum == (n == 13 ? 2 : 3));
                    CHECK(filled(out + used, (uint16_t)(cap - used), 0xc7));
                }
                free(out);
            }
            free(input);
        }
    }
    free(c); free(result);
    return 0;
}

static uint16_t native_boundaries(void)
{
    zcl_temp_cfg_t cfg = {0, 0, 0};
    uint32_t n;
    int32_t v;
    CHECK(zcl_temp_init(&ctx, 0, -27315, 32767, &cfg) == ZCL_CODEC_OK);
    for (v = -32768; v <= 32767; v++) {
        saved = ctx;
        CHECK(zcl_temp_sample(&ctx, 0, (int16_t)v) ==
              (v == -32768 || v >= -27315 ? ZCL_CODEC_OK : ZCL_CODEC_INVALID_VALUE));
        if (v != -32768 && v < -27315) CHECK(memcmp(&ctx, &saved, sizeof(ctx)) == 0);
    }
    CHECK(zcl_temp_init(&ctx, 0, -27315, 32767, &cfg) == ZCL_CODEC_OK);
    for (n = 1; n <= 65535UL; n++) {
        CHECK(zcl_temp_sample(&ctx, n, (int16_t)(n & 1u)) == ZCL_CODEC_OK);
        CHECK(prepare(n, 1, 8) == ZCL_CODEC_OK && report.ready && report.token == n);
        CHECK(zcl_temp_finish(&ctx, n, report.token, 1, n) == ZCL_CODEC_OK);
        CHECK(ctx.baseline == (int16_t)(n & 1u) && ctx.age == 0 && !ctx.pending);
    }
    CHECK(zcl_temp_sample(&ctx, 65536UL, 0) == ZCL_CODEC_OK);
    CHECK(prepare(65536UL, 1, 8) == ZCL_CODEC_INVALID_VALUE && ctx.fault == ZCL_TEMP_FAULT_TOKEN);
    cfg.minimum = cfg.maximum = 65534u;
    CHECK(zcl_temp_init(&ctx, 0xffff8000UL, -27315, 32767, &cfg) == ZCL_CODEC_OK);
    CHECK(zcl_temp_sample(&ctx, 0xffff8000UL, 0) == ZCL_CODEC_OK);
    for (n = 0; n < 65534UL; n++) {
        CHECK(prepare(0xffff8000UL + n, 1, 0) == ZCL_CODEC_OK && !report.ready);
        CHECK(ctx.age == n);
    }
    CHECK(prepare((uint32_t)(0xffff8000UL + 65534UL), 1, 8) == ZCL_CODEC_OK && report.ready);
    return 0;
}

static uint16_t exact_read_report(void)
{
    zcl_temp_t *c = malloc(sizeof(*c)), before;
    zcl_dispatch_info_t *result = malloc(sizeof(*result)), expected_info;
    zcl_temp_report_t *lease = malloc(sizeof(*lease));
    uint8_t *input, *out, expected[100];
    unsigned mode, n, cap, records, returned, used, width, i;
    zcl_codec_result_t want, got;
    CHECK(c != NULL && result != NULL && lease != NULL);
    for (mode = 0; mode < 4; mode++) {
        width = mode == 0 ? 11 : 4;
        for (n = 0; n <= 102; n++) {
            input = malloc(n ? n : 1);
            CHECK(input != NULL);
            for (i = 0; i < n; i++) input[i] = i < 3 ? (i == 0 ? 0xf0 : i == 1 ? 0x5a : 8)
                : (i % 3 == 0 ? (mode == 3 ? 1 : 0) : i % 3 == 1 ? (mode == 1 ? 1 : mode == 2 ? 3 : 0) : 0);
            for (cap = 0; cap <= 100; cap++) {
                out = malloc(cap ? cap : 1);
                CHECK(out != NULL);
                memset(out, 0xc7, cap ? cap : 1); memset(result, 0xa5, sizeof(*result));
                CHECK(zcl_temp_init(c, 0, -27315, 32767, &defaults) == ZCL_CODEC_OK);
                before = *c;
                records = n >= 6 && n % 3 == 0 ? (n - 3) / 3 : 0;
                returned = cap >= 3 ? (cap - 3) / width : 0;
                if (returned > records) returned = records;
                used = records ? 3 + returned * width : 5;
                want = n > 100 ? ZCL_CODEC_TOO_LONG : n < 3 ? ZCL_CODEC_TRUNCATED
                    : (records ? returned == 0 : cap < 5) ? ZCL_CODEC_BUFFER_TOO_SMALL : ZCL_CODEC_OK;
                got = zcl_temp_rx(c, 3, input, (uint16_t)n, out, (uint16_t)cap, result);
                CHECK(got == want);
                if (got) {
                    CHECK(memcmp(c, &before, sizeof(before)) == 0);
                    CHECK(filled(result, sizeof(*result), 0xa5) && filled(out, (uint16_t)(cap ? cap : 1), 0xc7));
                } else {
                    memset(&expected_info, 0, sizeof(expected_info));
                    expected_info.sequence = 0x5a; expected_info.length = (uint8_t)used;
                    expected[0] = 0x18; expected[1] = 0x5a;
                    expected_info.command_id = expected[2] = records ? 9 : 0x0b;
                    if (records) {
                        expected_info.requested_count = (uint8_t)records;
                        expected_info.returned_count = (uint8_t)returned;
                        for (i = 0; i < returned; i++) {
                            if (mode == 0) memcpy(expected + 3 + 11*i, "\0\0\0\0\x29\x03\0\x0a\0\x32\0", 11);
                            else {
                                expected[3 + 4*i] = mode == 1 ? 0x8c : 0x86;
                                expected[4 + 4*i] = mode == 3 ? 1 : 0;
                                expected[5 + 4*i] = mode == 1 ? 1 : mode == 2 ? 3 : 0;
                                expected[6 + 4*i] = 0;
                            }
                        }
                    } else {
                        expected_info.default_command = expected[3] = 8;
                        expected_info.default_status = expected_info.default_raw_status = expected[4] = 0x80;
                    }
                    CHECK(memcmp(result, &expected_info, sizeof(expected_info)) == 0);
                    CHECK(memcmp(out, expected, used) == 0 && filled(out + used, (uint16_t)(cap - used), 0xc7));
                    before.stamp = 3; before.age = 3;
                    CHECK(memcmp(c, &before, sizeof(before)) == 0);
                }
                free(out);
            }
            free(input);
        }
    }
    for (cap = 0; cap <= 9; cap++) {
        out = malloc(cap ? cap : 1);
        CHECK(out != NULL);
        memset(out, 0xc7, cap ? cap : 1); memset(lease, 0xa5, sizeof(*lease));
        CHECK(zcl_temp_init(c, 0, -27315, 32767, &defaults) == ZCL_CODEC_OK);
        before = *c;
        CHECK(zcl_temp_prepare(c, 10, 0x5a, 1, out, (uint16_t)cap, lease) ==
              (cap < 8 ? ZCL_CODEC_BUFFER_TOO_SMALL : ZCL_CODEC_OK));
        if (cap < 8) {
            CHECK(memcmp(c, &before, sizeof(before)) == 0);
            CHECK(filled(out, (uint16_t)(cap ? cap : 1), 0xc7) && filled(lease, sizeof(*lease), 0xa5));
        } else {
            CHECK(memcmp(out, "\x18\x5a\x0a\0\0\x29\0\x80", 8) == 0);
            CHECK(lease->token == 1 && lease->ready == 1 && lease->length == 8);
            CHECK(c->pending && c->baseline == ZCL_TEMP_UNKNOWN && filled(out + 8, (uint16_t)(cap - 8), 0xc7));
        }
        free(out);
    }
    CHECK(zcl_temp_rx(NULL, 0, request, 3, response, 100, result) == ZCL_CODEC_INVALID_ARGUMENT);
    CHECK(zcl_temp_rx(c, 0, NULL, 3, response, 100, result) == ZCL_CODEC_INVALID_ARGUMENT);
    CHECK(zcl_temp_rx(c, 0, request, 3, NULL, 100, result) == ZCL_CODEC_INVALID_ARGUMENT);
    CHECK(zcl_temp_rx(c, 0, request, 3, response, 100, NULL) == ZCL_CODEC_INVALID_ARGUMENT);
    CHECK(zcl_temp_prepare(c, 0, 0, 0, NULL, 0, lease) == ZCL_CODEC_INVALID_ARGUMENT);
    CHECK(zcl_temp_prepare(c, 0, 0, 0, response, 0, NULL) == ZCL_CODEC_INVALID_ARGUMENT);
    free(lease); free(result); free(c);
    return 0;
}

int main(void)
{
    uint16_t rc = run_tests(), common = checks;
    if (!rc) rc = exact_native();
    if (!rc) rc = native_boundaries();
    if (!rc) rc = exact_read_report();
    if (rc) {
        fprintf(stderr, "Temperature failed at C line %u\n", rc);
        return 1;
    }
    printf("Temperature: %u shared case groups; exact allocations, all int16 values, intervals/tokens PASS.\n", common);
    return 0;
}
#endif
