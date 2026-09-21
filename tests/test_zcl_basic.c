/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "zcl_basic.h"
#include "zcl_dispatch.h"
#include "cc2530_mmio.h"

#include <stddef.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) return (uint16_t)__LINE__; } while (0)

static const MCU_CODE uint8_t manufacturer[] = "cc2530-zigbee lab";
static const MCU_CODE uint8_t model_name[] = "synthetic-basic";
static const MCU_CODE uint8_t software[] = "lab-r8-1";
static const MCU_CODE zcl_basic_config_t lab = {
    {manufacturer, 17}, {model_name, 15}, {software, 8}, 0
};
static const MCU_CODE uint8_t read_all[] = {
    0x00, 0x5a, 0x00, 0, 0, 4, 0, 5, 0, 7, 0, 0, 0x40, 0xfd, 0xff
};
/* Independent literal wire oracle; no production constants/encoder. */
static const MCU_CODE uint8_t read_golden[] = {
    0x18, 0x5a, 0x01,
    0, 0, 0, 0x20, 8,
    4, 0, 0, 0x42, 17,
    'c','c','2','5','3','0','-','z','i','g','b','e','e',' ','l','a','b',
    5, 0, 0, 0x42, 15,
    's','y','n','t','h','e','t','i','c','-','b','a','s','i','c',
    7, 0, 0, 0x30, 0,
    0, 0x40, 0, 0x42, 8, 'l','a','b','-','r','8','-','1',
    0xfd, 0xff, 0, 0x21, 3, 0
};
static const MCU_CODE uint8_t discover_request[] = {0, 0x5a, 0x0c, 0, 0, 0xff};
static const MCU_CODE uint8_t discover_golden[] = {
    0x18, 0x5a, 0x0d, 1,
    0, 0, 0x20, 4, 0, 0x42, 5, 0, 0x42, 7, 0, 0x30,
    0, 0x40, 0x42, 0xfd, 0xff, 0x21
};
static const MCU_CODE uint8_t absent_request[] = {
    0, 0x5a, 0, 1, 0, 0x10, 0, 0xfe, 0xff, 0xff, 0xff, 0, 0x50
};
static const MCU_CODE uint8_t absent_golden[] = {
    0x18, 0x5a, 1, 1, 0, 0x86, 0x10, 0, 0x86,
    0xfe, 0xff, 0x86, 0xff, 0xff, 0x86, 0, 0x50, 0x86
};
static zcl_basic_t basic, saved;
static zcl_basic_config_t config;
static zcl_dispatch_info_t info;
static zcl_read_info_t read_info;
static zcl_frame_info_t frame;
static zcl_value_info_t value;
static uint8_t request[102], response[102], text[33], encoded_length;
static uint16_t cases;

static uint8_t filled(const void *p, uint16_t size, uint8_t v)
{
    const uint8_t *bytes = p;
    while (size--)
        if (*bytes++ != v) return 0;
    return 1;
}

static void outputs(void)
{
    memset(response, 0xc7, sizeof(response));
    memset(&info, 0xa5, sizeof(info));
}

static uint16_t failure(uint16_t length, uint16_t capacity, zcl_codec_result_t expected)
{
    outputs();
    CHECK(zcl_dispatch_unicast(&basic.set, request, length, response + 1, capacity, &info) == expected);
    CHECK(filled(response, sizeof(response), 0xc7));
    CHECK(filled(&info, sizeof(info), 0xa5));
    CHECK(memcmp(&basic, &saved, sizeof(basic)) == 0);
    cases++;
    return 0;
}

static uint16_t golden_cases(void)
{
    uint8_t i, pos;
    CHECK(zcl_basic_init(&basic, &lab) == ZCL_CODEC_OK); /* actual CODE config */
    memcpy(&saved, &basic, sizeof(basic)); /* snapshot only; never dispatch saved */
    CHECK(basic.set.attributes == basic.attributes && basic.set.count == 6);
    CHECK(basic.set.side == 0 && basic.set.manufacturer_specific == 0 && basic.set.manufacturer_code == 0);
    CHECK(zcl_attr_set_check(&basic.set) == ZCL_CODEC_OK);
    for (i = 0; i < 6; i++) {
        CHECK(basic.attributes[i].readable == 1 && basic.attributes[i].value.string_non_value == 0);
    }
    CHECK(basic.attributes[0].value.data == basic.scalars);
    CHECK(basic.attributes[1].value.data == basic.strings);
    CHECK(basic.attributes[2].value.data == basic.strings + 32);
    CHECK(basic.attributes[3].value.data == basic.scalars + 1);
    CHECK(basic.attributes[4].value.data == basic.strings + 64);
    CHECK(basic.attributes[5].value.data == basic.scalars + 2);
    outputs();
    CHECK(zcl_dispatch_unicast(&basic.set, read_all, sizeof(read_all), response + 1,
                               sizeof(read_golden), &info) == ZCL_CODEC_OK);
    CHECK(info.kind == 0 && info.length == sizeof(read_golden) && info.sequence == 0x5a);
    CHECK(info.command_id == 1 && info.requested_count == 6 && info.returned_count == 6);
    CHECK(info.discovery_complete == 0 && info.default_command == 0
          && info.default_status == 0 && info.default_raw_status == 0);
    CHECK(memcmp(response + 1, read_golden, sizeof(read_golden)) == 0);
    CHECK(response[0] == 0xc7 && filled(response + 1 + info.length, 101u - info.length, 0xc7));
    CHECK(zcl_frame_decode(response + 1, info.length, &frame) == ZCL_CODEC_OK);
    CHECK(frame.payload_offset == 3 && frame.header.command_id == 1 && frame.ignored_control_bits == 0);
    pos = 3;
    for (i = 0; i < 6; i++) {
        CHECK(zcl_value_decode(response[1u + pos + 3u], response + 1u + pos + 4u,
                               (uint16_t)(info.length - pos - 4u), &value) == ZCL_CODEC_OK);
        CHECK(value.non_value_pattern == 0);
        pos += (uint8_t)(4u + value.encoded_length);
    }
    CHECK(pos == info.length);
    CHECK(zcl_frame_encode(&frame.header, response + 4, frame.payload_length,
                           request, sizeof(read_golden), &encoded_length) == ZCL_CODEC_OK);
    CHECK(encoded_length == sizeof(read_golden) && memcmp(request, read_golden, encoded_length) == 0);
    CHECK(zcl_read_attrs_unicast(&basic.set, read_all, sizeof(read_all), response, 100, &read_info) == ZCL_CODEC_OK);
    CHECK(read_info.length == sizeof(read_golden) && memcmp(response, read_golden, read_info.length) == 0);
    CHECK(zcl_dispatch_unicast(&basic.set, discover_request, 6, response, 22, &info) == ZCL_CODEC_OK);
    CHECK(info.length == 22 && info.discovery_complete == 1 && info.returned_count == 6);
    CHECK(memcmp(response, discover_golden, 22) == 0);
    CHECK(zcl_dispatch_unicast(&basic.set, absent_request, sizeof(absent_request), response, 18, &info) == ZCL_CODEC_OK);
    CHECK(info.length == 18 && memcmp(response, absent_golden, 18) == 0);
    CHECK(memcmp(&basic, &saved, sizeof(basic)) == 0);
    cases++;
    return 0;
}

static uint16_t config_cases(void)
{
    uint16_t power;
    uint8_t i;
    CHECK(zcl_basic_init(NULL, &lab) == ZCL_CODEC_INVALID_ARGUMENT);
    CHECK(zcl_basic_init(&basic, NULL) == ZCL_CODEC_INVALID_ARGUMENT);
    CHECK(memcmp(&basic, &saved, sizeof(basic)) == 0);
    memset(text, 'X', sizeof(text));
    config = lab;
    config.manufacturer.data = config.model.data = config.software.data = text;
    config.manufacturer.length = config.model.length = 32;
    config.software.length = 16;
    CHECK(zcl_basic_init(&basic, &config) == ZCL_CODEC_OK);
    memset(text, 'Y', sizeof(text)); /* copied, not borrowed */
    CHECK(filled(basic.strings, 80, 'X'));
    memcpy(request, "\x00\x5a\x00\x04\x00", 5);
    CHECK(zcl_dispatch_unicast(&basic.set, request, 5, response, 40, &info) == ZCL_CODEC_OK);
    CHECK(info.length == 40 && memcmp(response, "\x18\x5a\x01\x04\x00\x00\x42\x20", 8) == 0);
    CHECK(filled(response + 8, 32, 'X'));
    CHECK(zcl_dispatch_unicast(&basic.set, request, 5, response, 39, &info) == ZCL_CODEC_OK);
    CHECK(info.length == 6 && memcmp(response, "\x18\x5a\x01\x04\x00\x89", 6) == 0);
    request[3] = 5;
    CHECK(zcl_dispatch_unicast(&basic.set, request, 5, response, 40, &info) == ZCL_CODEC_OK);
    CHECK(info.length == 40 && response[3] == 5 && response[7] == 32 && filled(response + 8, 32, 'X'));
    request[3] = 0; request[4] = 0x40;
    CHECK(zcl_dispatch_unicast(&basic.set, request, 5, response, 24, &info) == ZCL_CODEC_OK);
    CHECK(info.length == 24 && memcmp(response, "\x18\x5a\x01\x00\x40\x00\x42\x10", 8) == 0);
    CHECK(filled(response + 8, 16, 'X'));
    for (i = 0; i < 3; i++) {
        config = lab;
        config.manufacturer.data = config.model.data = config.software.data = text;
        if (i == 0) config.manufacturer.length = 33;
        if (i == 1) config.model.length = 33;
        if (i == 2) config.software.length = 17;
        memcpy(&saved, &basic, sizeof(basic));
        CHECK(zcl_basic_init(&basic, &config) == ZCL_CODEC_INVALID_VALUE);
        CHECK(memcmp(&basic, &saved, sizeof(basic)) == 0);
        config = lab;
        if (i == 0) config.manufacturer.data = NULL;
        if (i == 1) config.model.data = NULL;
        if (i == 2) config.software.data = NULL;
        CHECK(zcl_basic_init(&basic, &config) == ZCL_CODEC_INVALID_ARGUMENT);
        CHECK(memcmp(&basic, &saved, sizeof(basic)) == 0);
    }
    memset(&config, 0, sizeof(config));
    for (power = 0; power < 256u; power++) {
        config.power_source = (uint8_t)power;
        memcpy(&saved, &basic, sizeof(basic));
        if ((power & 0x7fu) <= 6u) {
            CHECK(zcl_basic_init(&basic, &config) == ZCL_CODEC_OK);
            CHECK(basic.scalars[1] == power && filled(basic.strings, 80, 0));
            CHECK(basic.attributes[1].value.data_length == 0
                  && basic.attributes[2].value.data_length == 0 && basic.attributes[4].value.data_length == 0);
        } else {
            CHECK(zcl_basic_init(&basic, &config) == ZCL_CODEC_INVALID_VALUE);
            CHECK(memcmp(&basic, &saved, sizeof(basic)) == 0);
        }
        cases++;
    }
    /* Empty strings are real empty values (00), not non-values (FF). */
    memcpy(request, read_all, sizeof(read_all));
    request[3] = 4;
    CHECK(zcl_dispatch_unicast(&basic.set, request, 5, response, 8, &info) == ZCL_CODEC_OK);
    CHECK(info.length == 8 && memcmp(response, "\x18\x5a\x01\x04\x00\x00\x42\x00", 8) == 0);
    CHECK(zcl_basic_init(&basic, &lab) == ZCL_CODEC_OK);
    memcpy(&saved, &basic, sizeof(basic));
    return 0;
}

static uint16_t receive_cases(void)
{
    uint8_t n, capacity;
    memcpy(request, discover_request, 6);
    for (n = 0; n < 3; n++) CHECK(failure(n, 100, ZCL_CODEC_TRUNCATED) == 0);
    for (n = 3; n < 6; n++) {
        CHECK(zcl_dispatch_unicast(&basic.set, request, n, response, 5, &info) == ZCL_CODEC_OK);
        CHECK(info.length == 5 && memcmp(response, "\x18\x5a\x0b\x0c\x80", 5) == 0);
        CHECK(failure(n, 4, ZCL_CODEC_BUFFER_TOO_SMALL) == 0);
    }
    /* Standard reserved bits / appended octets ignored; canonical TX. */
    request[0] = 0xf0;
    request[6] = request[7] = 0xee;
    CHECK(zcl_dispatch_unicast(&basic.set, request, 8, response, 22, &info) == ZCL_CODEC_OK);
    CHECK(memcmp(response, discover_golden, 22) == 0);
    for (capacity = 0; capacity <= 23; capacity++) {
        if (capacity < 7) {
            CHECK(failure(8, capacity, ZCL_CODEC_BUFFER_TOO_SMALL) == 0);
        } else {
            outputs();
            CHECK(zcl_dispatch_unicast(&basic.set, request, 8, response + 1, capacity, &info) == ZCL_CODEC_OK);
            n = (uint8_t)((capacity - 4u) / 3u);
            CHECK(info.length == 4u + 3u * n && info.returned_count == n && info.discovery_complete == (n == 6));
            CHECK(memcmp(response + 5, discover_golden + 4, 3u * n) == 0);
            CHECK(response[0] == 0xc7 && filled(response + 1 + info.length, 101u - info.length, 0xc7));
            cases++;
        }
    }
    memcpy(request, read_all, sizeof(read_all));
    CHECK(zcl_dispatch_unicast(&basic.set, request, 3, response, 5, &info) == ZCL_CODEC_OK);
    CHECK(memcmp(response, "\x18\x5a\x0b\x00\x80", 5) == 0);
    CHECK(zcl_dispatch_unicast(&basic.set, request, 4, response, 5, &info) == ZCL_CODEC_OK);
    CHECK(memcmp(response, "\x18\x5a\x0b\x00\x80", 5) == 0);
    /* Read is a variable ID list: an extra full ID is not ignorable padding. */
    CHECK(zcl_dispatch_unicast(&basic.set, request, 7, response, 30, &info) == ZCL_CODEC_OK);
    CHECK(info.requested_count == 2 && info.returned_count == 2 && info.length == 30);
    CHECK(memcmp(response, read_golden, 30) == 0);
    CHECK(zcl_dispatch_unicast(&basic.set, request, 6, response, 5, &info) == ZCL_CODEC_OK);
    CHECK(memcmp(response, "\x18\x5a\x0b\x00\x80", 5) == 0);
    for (capacity = 0; capacity <= 9; capacity++) {
        if (capacity < 6) CHECK(failure(5, capacity, ZCL_CODEC_BUFFER_TOO_SMALL) == 0);
        else {
            outputs();
            CHECK(zcl_dispatch_unicast(&basic.set, request, 5, response + 1, capacity, &info) == ZCL_CODEC_OK);
            CHECK(info.returned_count == 1 && info.requested_count == 1);
            if (capacity < 8) {
                CHECK(info.length == 6 && memcmp(response + 1, "\x18\x5a\x01\x00\x00\x89", 6) == 0);
            } else CHECK(info.length == 8 && memcmp(response + 1, read_golden, 8) == 0);
            CHECK(response[0] == 0xc7 && filled(response + 1 + info.length, 101u - info.length, 0xc7));
            cases++;
        }
    }
    request[0] = 8;
    CHECK(failure(5, 100, ZCL_CODEC_UNSUPPORTED_CONTEXT) == 0);
    request[0] = 2;
    CHECK(failure(5, 100, ZCL_CODEC_UNSUPPORTED_FRAME_TYPE) == 0);
    request[0] = 0;
    CHECK(failure(101, 100, ZCL_CODEC_TOO_LONG) == 0);
    cases++;
    return 0;
}

static uint16_t unsupported_cases(void)
{
    uint8_t i;
    /* Unknown, Write, Undivided, Configure Reporting, Read Reporting, Report. */
    static const MCU_CODE uint8_t commands[] = {0xff, 2, 3, 6, 8, 10};
    memcpy(request, read_all, sizeof(read_all));
    for (i = 0; i < sizeof(commands); i++) {
        request[2] = commands[i];
        CHECK(zcl_dispatch_unicast(&basic.set, request, 5, response, 5, &info) == ZCL_CODEC_OK);
        CHECK(info.length == 5 && response[0] == 0x18 && response[1] == 0x5a && response[2] == 0x0b);
        CHECK(response[3] == commands[i] && response[4] == 0x81);
        CHECK(memcmp(&basic, &saved, sizeof(basic)) == 0);
        cases++;
    }
    request[0] = 1; request[2] = 0; /* Optional factory reset NOT implemented. */
    CHECK(zcl_dispatch_unicast(&basic.set, request, 3, response, 5, &info) == ZCL_CODEC_OK);
    CHECK(memcmp(response, "\x18\x5a\x0b\x00\x81", 5) == 0);
    request[0] = 0; request[2] = 5;
    CHECK(failure(5, 100, ZCL_CODEC_UNSUPPORTED_NO_RESPONSE) == 0);
    /* Synthetic input namespace only: never an assigned project manufacturer. */
    memcpy(request, "\x04\x34\x12\x5a\x00\x00\x00", 7);
    CHECK(zcl_dispatch_unicast(&basic.set, request, 7, response, 7, &info) == ZCL_CODEC_OK);
    CHECK(memcmp(response, "\x1c\x34\x12\x5a\x0b\x00\x81", 7) == 0);
    request[0] = 0xe4;
    CHECK(failure(7, 100, ZCL_CODEC_UNSUPPORTED_LAYOUT) == 0);
    request[0] = 4;
    CHECK(failure(4, 100, ZCL_CODEC_TRUNCATED) == 0);
    memcpy(request, "\x00\x5a\x0b\x02\x86\xee", 6);
    outputs();
    CHECK(zcl_dispatch_unicast(&basic.set, request, 6, response, 0, &info) == ZCL_CODEC_OK);
    CHECK(info.kind == 1 && info.length == 0 && info.default_command == 2 && info.default_status == 0x86);
    CHECK(filled(response, sizeof(response), 0xc7));
    CHECK(failure(4, 100, ZCL_CODEC_TRUNCATED) == 0);
    CHECK(memcmp(&basic, &saved, sizeof(basic)) == 0);
    cases++;
    return 0;
}

static uint16_t run_tests(void)
{
    uint16_t rc;
    cases = 0;
    rc = golden_cases(); if (rc != 0) return rc;
    rc = config_cases(); if (rc != 0) return rc;
    rc = receive_cases(); if (rc != 0) return rc;
    return unsupported_cases();
}

#if defined(__SDCC)
volatile MCU_XDATA MCU_AT(0x1e00) uint8_t zcl_basic_test_result[8];
void main(void)
{
    uint16_t rc = run_tests();
    zcl_basic_test_result[0] = 'Z'; zcl_basic_test_result[1] = 'B';
    zcl_basic_test_result[2] = 'A'; zcl_basic_test_result[3] = '1';
    zcl_basic_test_result[4] = 1; zcl_basic_test_result[5] = 8;
    zcl_basic_test_result[6] = (uint8_t)rc;
    zcl_basic_test_result[7] = (uint8_t)(rc >> 8);
    __asm
        .globl _zcl_basic_test_done
        _zcl_basic_test_done:
        nop
    __endasm;
    for (;;) { }
}
#else
#include <stdio.h>
#include <stdlib.h>

static uint16_t exact_native(void)
{
    unsigned n, cap, which;
    zcl_basic_t *m = malloc(sizeof(*m)), *before = malloc(sizeof(*before));
    zcl_basic_config_t *c = malloc(sizeof(*c));
    zcl_dispatch_info_t *out = malloc(sizeof(*out));
    uint8_t *input, *output, *string;
    zcl_codec_result_t rc;
    CHECK(m != NULL && before != NULL && c != NULL && out != NULL);
    /* Exact-size input, output, model, config and metadata allocations. */
    for (n = 0; n <= 255; n++) {
        string = malloc(n ? n : 1);
        CHECK(string != NULL);
        memset(string, 'A', n);
        for (which = 0; which < 3; which++) {
            *c = lab;
            if (which == 0) { c->manufacturer.data = string; c->manufacturer.length = (uint8_t)n; }
            if (which == 1) { c->model.data = string; c->model.length = (uint8_t)n; }
            if (which == 2) { c->software.data = string; c->software.length = (uint8_t)n; }
            memset(m, 0xa5, sizeof(*m));
            rc = zcl_basic_init(m, c);
            CHECK(rc == (n <= (which == 2 ? 16u : 32u) ? ZCL_CODEC_OK : ZCL_CODEC_INVALID_VALUE));
            if (rc != ZCL_CODEC_OK) CHECK(filled(m, sizeof(*m), 0xa5));
        }
        free(string);
    }
    CHECK(zcl_basic_init(m, &lab) == ZCL_CODEC_OK);
    memcpy(before, m, sizeof(*m));
    for (n = 0; n <= 102; n++) {
        input = malloc(n ? n : 1);
        CHECK(input != NULL);
        memset(input, 0xee, n);
        memcpy(input, discover_request, n < 6 ? n : 6);
        for (cap = 0; cap <= 102; cap++) {
            output = malloc(cap ? cap : 1);
            CHECK(output != NULL);
            memset(output, 0xc7, cap);
            memset(out, 0xa5, sizeof(*out));
            rc = zcl_dispatch_unicast(&m->set, input, (uint16_t)n, output, (uint16_t)cap, out);
            CHECK(rc == (n > 100 ? ZCL_CODEC_TOO_LONG : n < 3 ? ZCL_CODEC_TRUNCATED :
                         cap < (n < 6 ? 5u : 7u) ? ZCL_CODEC_BUFFER_TOO_SMALL : ZCL_CODEC_OK));
            if (rc != ZCL_CODEC_OK) {
                CHECK(filled(output, (uint16_t)cap, 0xc7) && filled(out, sizeof(*out), 0xa5));
            } else {
                CHECK(out->length <= cap && filled(output + out->length, (uint16_t)(cap - out->length), 0xc7));
                if (n < 6) CHECK(out->length == 5 && memcmp(output, "\x18\x5a\x0b\x0c\x80", 5) == 0);
                else {
                    unsigned count = (cap - 4u) / 3u;
                    if (count > 6) count = 6;
                    CHECK(out->returned_count == count && out->length == 4u + 3u * count);
                    CHECK(out->discovery_complete == (count == 6));
                    CHECK(memcmp(output + 4, discover_golden + 4, 3u * count) == 0);
                }
            }
            CHECK(memcmp(m, before, sizeof(*m)) == 0);
            CHECK(memcmp(input, discover_request, n < 6 ? n : 6) == 0);
            if (n > 6) CHECK(filled(input + 6, (uint16_t)(n - 6), 0xee));
            free(output);
        }
        free(input);
    }
    free(out); free(c); free(before); free(m);
    return 0;
}

static uint16_t exact_reads(void)
{
    static const uint8_t sizes[] = {5, 22, 20, 5, 13, 6};
    uint8_t expected[100], *input, *output;
    zcl_dispatch_info_t *out = malloc(sizeof(*out));
    unsigned cap, n, i, used, source, count;
    CHECK(out != NULL);
    /* Every complete prefix, empty list and half-ID truncation in exact RAM. */
    for (n = 3; n <= sizeof(read_all); n++) {
        input = malloc(n);
        CHECK(input != NULL);
        memcpy(input, read_all, n);
        for (cap = 0; cap <= 102; cap++) {
            output = malloc(cap ? cap : 1);
            CHECK(output != NULL);
            memset(output, 0xc7, cap);
            memset(out, 0xa5, sizeof(*out));
            if (n == 3 || !(n & 1u)) {
                if (cap < 5) {
                    CHECK(zcl_dispatch_unicast(&basic.set, input, (uint16_t)n, output, (uint16_t)cap, out)
                          == ZCL_CODEC_BUFFER_TOO_SMALL);
                    CHECK(filled(output, (uint16_t)cap, 0xc7) && filled(out, sizeof(*out), 0xa5));
                } else {
                    CHECK(zcl_dispatch_unicast(&basic.set, input, (uint16_t)n, output, (uint16_t)cap, out) == ZCL_CODEC_OK);
                    CHECK(out->length == 5 && memcmp(output, "\x18\x5a\x0b\x00\x80", 5) == 0);
                    CHECK(filled(output + 5, (uint16_t)(cap - 5), 0xc7));
                }
            } else if (cap < 6) {
                CHECK(zcl_dispatch_unicast(&basic.set, input, (uint16_t)n, output, (uint16_t)cap, out)
                      == ZCL_CODEC_BUFFER_TOO_SMALL);
                CHECK(filled(output, (uint16_t)cap, 0xc7) && filled(out, sizeof(*out), 0xa5));
            } else {
                memcpy(expected, "\x18\x5a\x01", 3);
                used = source = 3; count = 0;
                for (i = 0; i < (n - 3) / 2 && used + 3 <= cap; i++) {
                    if (used + sizes[i] <= cap) {
                        memcpy(expected + used, read_golden + source, sizes[i]);
                        used += sizes[i];
                    } else {
                        memcpy(expected + used, read_golden + source, 2);
                        expected[used + 2] = 0x89;
                        used += 3;
                    }
                    source += sizes[i]; count++;
                }
                CHECK(zcl_dispatch_unicast(&basic.set, input, (uint16_t)n, output, (uint16_t)cap, out) == ZCL_CODEC_OK);
                CHECK(out->length == used && out->returned_count == count && out->requested_count == (n - 3) / 2);
                CHECK(memcmp(output, expected, used) == 0 && filled(output + used, (uint16_t)(cap - used), 0xc7));
            }
            CHECK(memcmp(input, read_all, n) == 0 && memcmp(&basic, &saved, sizeof(basic)) == 0);
            free(output);
        }
        free(input);
    }
    free(out);
    return 0;
}

int main(void)
{
    uint16_t rc = run_tests();
    if (rc == 0) rc = exact_native();
    if (rc == 0) rc = exact_reads();
    if (rc != 0) {
        fprintf(stderr, "Basic test failed at C line %u\n", rc);
        return 1;
    }
    printf("Basic: %u common cases; exact native allocations and real ZCL path PASS.\n", cases);
    return 0;
}
#endif
