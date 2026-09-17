/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "nwk_beacon.h"
#include "cc2530_mmio.h"

#include <stddef.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) return (uint16_t)__LINE__; } while (0)

static const MCU_CODE uint8_t golden[][NWK_BEACON_LENGTH] = {
    {0x00, 0x22, 0xac, 0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe, 0x56, 0x34, 0x12, 0xa5},
    {0x00, 0x2f, 0x78, 1, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 0xff, 0}
};
static const MCU_CODE uint32_t offsets[] = {
    0, 1, 0xff, 0x100, 0xffffUL, 0x10000UL, 0xabcdefUL, 0xfffffeUL, 0xffffffUL
};

static uint16_t self_test(void)
{
    nwk_beacon_t decoded, saved;
    uint8_t payload[16], original[16], i, bit, vector, length;
    nwk_beacon_result_t expected;

    for (vector = 0; vector < 2; vector++) {
        CHECK(nwk_beacon_decode(golden[vector], 15, &decoded) == NWK_BEACON_OK);
        CHECK(decoded.stack_profile == (vector ? 15u : 2u));
        CHECK(decoded.router_capacity == (vector ? 0u : 1u));
        CHECK(decoded.device_depth == (vector ? 15u : 5u));
        CHECK(decoded.end_device_capacity == (vector ? 0u : 1u));
        CHECK(memcmp(decoded.extended_pan_id, golden[vector] + 3, 8) == 0);
        CHECK(decoded.tx_offset == (vector ? NWK_BEACON_BEACONLESS_OFFSET : 0x123456UL));
        CHECK(decoded.update_id == (vector ? 0u : 0xa5u));
    }
    memcpy(payload, golden[0], 15);
    payload[15] = 0x69;
    memcpy(original, payload, sizeof(payload));
    for (length = 0; length <= 16; length++) {
        memset(&decoded, 0xa5, sizeof(decoded));
        memcpy(&saved, &decoded, sizeof(saved));
        expected = length < 15 ? NWK_BEACON_TRUNCATED : length > 15 ? NWK_BEACON_TOO_LONG : NWK_BEACON_OK;
        CHECK(nwk_beacon_decode(payload, length, &decoded) == expected);
        CHECK(memcmp(payload, original, sizeof(payload)) == 0);
        if (expected != NWK_BEACON_OK)
            CHECK(memcmp(&decoded, &saved, sizeof(decoded)) == 0);
    }
    CHECK(nwk_beacon_decode(NULL, 15, &decoded) == NWK_BEACON_INVALID_ARGUMENT);
    CHECK(nwk_beacon_decode(NULL, 0, &decoded) == NWK_BEACON_INVALID_ARGUMENT);
    CHECK(memcmp(&decoded, &saved, sizeof(decoded)) == 0);
    CHECK(nwk_beacon_decode(payload, 15, NULL) == NWK_BEACON_INVALID_ARGUMENT);
    CHECK(memcmp(payload, original, sizeof(payload)) == 0);
    for (vector = 0; vector < 6; vector++) {
        memcpy(payload, golden[0], 15);
        expected = NWK_BEACON_INVALID_FIELDS;
        switch (vector) {
        case 0: payload[0] = 1; expected = NWK_BEACON_UNSUPPORTED_PROTOCOL; break;
        case 1: payload[1] = 0x32; expected = NWK_BEACON_UNSUPPORTED_VERSION; break;
        case 2: payload[2] |= 1; break;
        case 3: payload[2] |= 2; break;
        case 4: memset(payload + 3, 0, 8); break;
        default: memset(payload + 3, 0xff, 8); break;
        }
        memset(&decoded, 0xa5, sizeof(decoded));
        memcpy(&saved, &decoded, sizeof(saved));
        CHECK(nwk_beacon_decode(payload, 15, &decoded) == expected);
        CHECK(memcmp(&decoded, &saved, sizeof(decoded)) == 0);
    }
    memcpy(payload, golden[0], 15);
    for (i = 0; i < 8; i++) {
        for (bit = 0; bit < 8; bit++) {
            memset(payload + 3, 0, 8);
            payload[3u + i] = (uint8_t)(1u << bit);
            CHECK(nwk_beacon_decode(payload, 15, &decoded) == NWK_BEACON_OK);
            CHECK(memcmp(decoded.extended_pan_id, payload + 3, 8) == 0);
            memset(payload + 3, 0xff, 8);
            payload[3u + i] &= (uint8_t)~(1u << bit);
            CHECK(nwk_beacon_decode(payload, 15, &decoded) == NWK_BEACON_OK);
            CHECK(memcmp(decoded.extended_pan_id, payload + 3, 8) == 0);
        }
    }
    for (i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i++) {
        payload[11] = (uint8_t)offsets[i];
        payload[12] = (uint8_t)(offsets[i] >> 8);
        payload[13] = (uint8_t)(offsets[i] >> 16);
        CHECK(nwk_beacon_decode(payload, 15, &decoded) == NWK_BEACON_OK);
        CHECK(decoded.tx_offset == offsets[i]);
    }
    return 0;
}

#if defined(__SDCC)
volatile MCU_XDATA MCU_AT(0x1e00) uint8_t nwk_beacon_test_result[8];

void main(void)
{
    uint16_t result = self_test();
    nwk_beacon_test_result[0] = 'N';
    nwk_beacon_test_result[1] = 'W';
    nwk_beacon_test_result[2] = 'B';
    nwk_beacon_test_result[3] = '1';
    nwk_beacon_test_result[4] = 1;
    nwk_beacon_test_result[5] = 8;
    nwk_beacon_test_result[6] = (uint8_t)result;
    nwk_beacon_test_result[7] = (uint8_t)(result >> 8);
    __asm
        .globl _nwk_beacon_test_done
    _nwk_beacon_test_done:
        nop
    __endasm;
    for (;;) {
    }
}
#else
#include <stdio.h>
#include <stdlib.h>

static uint16_t exhaustive_fields(void)
{
    struct {
        uint8_t before;
        nwk_beacon_t value;
        uint8_t after;
    } output, saved;
    uint8_t payload[15];
    unsigned field, value;
    nwk_beacon_result_t expected, status;

    for (field = 0; field < 15; field++) {
        for (value = 0; value < 256; value++) {
            memcpy(payload, golden[0], 15);
            payload[field] = (uint8_t)value;
            expected = NWK_BEACON_OK;
            if (field == 0 && value != 0)
                expected = NWK_BEACON_UNSUPPORTED_PROTOCOL;
            else if (field == 1 && value / 16 != 2)
                expected = NWK_BEACON_UNSUPPORTED_VERSION;
            else if (field == 2 && value % 4 != 0)
                expected = NWK_BEACON_INVALID_FIELDS;
            memset(&output, 0xc7, sizeof(output));
            memcpy(&saved, &output, sizeof(saved));
            status = nwk_beacon_decode(payload, 15, &output.value);
            CHECK(status == expected);
            if (status != NWK_BEACON_OK) {
                CHECK(memcmp(&output, &saved, sizeof(output)) == 0);
            } else {
                CHECK(output.before == 0xc7 && output.after == 0xc7);
                CHECK(output.value.stack_profile == payload[1] % 16);
                CHECK(output.value.router_capacity == payload[2] / 4 % 2);
                CHECK(output.value.device_depth == payload[2] / 8 % 16);
                CHECK(output.value.end_device_capacity == payload[2] / 128);
                CHECK(memcmp(output.value.extended_pan_id, payload + 3, 8) == 0);
                CHECK(output.value.tx_offset == payload[11] + 256UL * payload[12] + 65536UL * payload[13]);
                CHECK(output.value.update_id == payload[14]);
            }
        }
    }
    return 0;
}

static uint16_t exact_bounds(void)
{
    nwk_beacon_t *output, saved;
    uint8_t *input, *large;
    unsigned size;
    uint32_t length;
    nwk_beacon_result_t expected;

    output = malloc(sizeof(*output));
    large = malloc(65535);
    CHECK(output != NULL && large != NULL);
    memset(large, 0x69, 65535);
    memcpy(large, golden[0], 15);
    for (size = 0; size <= 16; size++) {
        input = malloc(size ? size : 1u);
        CHECK(input != NULL);
        memcpy(input, large, size);
        memset(output, 0xa5, sizeof(*output));
        memcpy(&saved, output, sizeof(saved));
        expected = size < 15 ? NWK_BEACON_TRUNCATED : size > 15 ? NWK_BEACON_TOO_LONG : NWK_BEACON_OK;
        CHECK(nwk_beacon_decode(input, (uint16_t)size, output) == expected);
        if (expected != NWK_BEACON_OK)
            CHECK(memcmp(output, &saved, sizeof(saved)) == 0);
        CHECK(memcmp(input, large, size) == 0);
        free(input);
    }
    memset(output, 0xa5, sizeof(*output));
    memcpy(&saved, output, sizeof(saved));
    for (length = 16; length <= 65535UL; length++) {
        CHECK(nwk_beacon_decode(large, (uint16_t)length, output) == NWK_BEACON_TOO_LONG);
        CHECK(memcmp(output, &saved, sizeof(saved)) == 0);
    }
    free(large);
    free(output);
    return 0;
}

int main(void)
{
    uint16_t result = self_test();
    if (result == 0)
        result = exhaustive_fields();
    if (result == 0)
        result = exact_bounds();
    if (result != 0) {
        fprintf(stderr, "NWK Beacon test failed at line %u\n", (unsigned)result);
        return 1;
    }
    puts("host NWK Beacon: R22 fields, all byte values/lengths, PAN boundaries and exact buffers PASS");
    return 0;
}
#endif
