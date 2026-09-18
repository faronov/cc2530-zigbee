/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 * Public synthetic beacons, not RF observations or a radio model.
 */
#include "nwk_candidates.h"
#include "mac_frame.h"
#include "cc2530_mmio.h"

#include <stddef.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) return (uint16_t)__LINE__; } while (0)
#define CALL(c) do { uint16_t line = (c); if (line) return line; } while (0)

static const MCU_CODE uint8_t short_beacon[] = {
    0x00, 0x80, 0x2a, 0x34, 0x12, 0x78, 0x56, 0xff, 0x8f, 0x80, 0x00,
    0x00, 0x22, 0xac, 0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe,
    0xff, 0xff, 0xff, 0x7f
};
static const MCU_CODE uint8_t long_beacon[] = {
    0x10, 0xc0, 0x2b, 0x34, 0x12, 1, 2, 3, 4, 5, 6, 7, 8,
    0xff, 0xcf, 0x00, 0x12, 0x21, 0x22, 0x23, 0x24,
    0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
    0x00, 0x22, 0x84, 0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe,
    0x56, 0x34, 0x12, 0xa5
};
static nwk_candidates_t table, saved;
static nwk_candidate_t entry, saved_entry;
static mac_header_t header;
static uint8_t body[126], payload[75], length;
static volatile uint8_t i, j, k, mode, shorts, extendeds, position;

static uint16_t unchanged(nwk_candidates_result_t expected, uint16_t size,
                           uint8_t crc, uint8_t channel)
{
    saved = table;
    CHECK(nwk_candidates_consider(&table, channel, crc, body, size) == expected);
    CHECK(memcmp(&table, &saved, sizeof(table)) == 0);
    return 0;
}

static uint16_t zero_tail(void)
{
    memset(&saved_entry, 0, sizeof(saved_entry));
    for (k = table.count; k < NWK_CANDIDATES_CAPACITY; k++)
        CHECK(memcmp(&table.entries[k], &saved_entry, sizeof(entry)) == 0);
    return 0;
}

static uint16_t initialization(void)
{
    memset(&table, 0xc7, sizeof(table));
    saved = table;
    CHECK(nwk_candidates_init(NULL, NWK_CANDIDATES_CHANNEL_MASK) == NWK_CANDIDATES_INVALID_ARGUMENT);
    CHECK(nwk_candidates_init(&table, 0) == NWK_CANDIDATES_INVALID_ARGUMENT);
    CHECK(nwk_candidates_init(&table, UINT32_MAX) == NWK_CANDIDATES_INVALID_ARGUMENT);
    CHECK(memcmp(&table, &saved, sizeof(table)) == 0);
    for (i = 0; i < 32; i++) {
        saved = table;
        if (i < 11 || i > 26) {
            CHECK(nwk_candidates_init(&table, UINT32_C(1) << i) == NWK_CANDIDATES_INVALID_ARGUMENT);
            CHECK(memcmp(&table, &saved, sizeof(table)) == 0);
        } else {
            CHECK(nwk_candidates_init(&table, UINT32_C(1) << i) == NWK_CANDIDATES_OK);
            memset(&saved, 0, sizeof(saved));
            saved.version = NWK_CANDIDATES_VERSION;
            saved.channel_mask = UINT32_C(1) << i;
            CHECK(memcmp(&table, &saved, sizeof(table)) == 0);
        }
    }
    memset(&entry, 0xc7, sizeof(entry));
    saved_entry = entry;
    CHECK(nwk_candidates_get(&table, 0, &entry) == NWK_CANDIDATES_BAD_INDEX);
    CHECK(memcmp(&entry, &saved_entry, sizeof(entry)) == 0);
    return 0;
}

static uint16_t golden_and_copy(void)
{
    CHECK(nwk_candidates_init(&table, NWK_CANDIDATES_CHANNEL_MASK) == NWK_CANDIDATES_OK);
    CHECK(nwk_candidates_consider(&table, 15, 1, short_beacon, sizeof(short_beacon)) == NWK_CANDIDATES_ADDED);
    CHECK(table.count == 1);
    CHECK(nwk_candidates_get(&table, 0, &entry) == NWK_CANDIDATES_OK);
    CHECK(entry.channel == 15 && entry.pan_id == 0x1234 && entry.address_mode == MAC_ADDRESS_SHORT);
    CHECK(entry.coordinator[0] == 0x78 && entry.coordinator[1] == 0x56);
    for (i = 2; i < 8; i++)
        CHECK(entry.coordinator[i] == 0);
    CHECK(entry.sequence == 0x2a && entry.mac_flags == 0 && entry.superframe == 0x8fff);
    CHECK(entry.gts_permit == 1 && !entry.short_pending && !entry.extended_pending);
    CHECK(entry.network.stack_profile == 2 && entry.network.router_capacity == 1);
    CHECK(entry.network.device_depth == 5 && entry.network.end_device_capacity == 1);
    CHECK(memcmp(entry.network.extended_pan_id, short_beacon + 14, 8) == 0);
    CHECK(entry.network.tx_offset == NWK_BEACON_BEACONLESS_OFFSET && entry.network.update_id == 0x7f);
    saved_entry = entry;
    memset(&entry, 0, sizeof(entry));
    CHECK(nwk_candidates_get(&table, 0, &entry) == NWK_CANDIDATES_OK);
    CHECK(memcmp(&entry, &saved_entry, sizeof(entry)) == 0);
    memcpy(body, long_beacon, sizeof(long_beacon));
    CHECK(nwk_candidates_consider(&table, 15, 1, body, sizeof(long_beacon)) == NWK_CANDIDATES_ADDED);
    memset(body, 0xc7, sizeof(body));
    CHECK(nwk_candidates_get(&table, 1, &entry) == NWK_CANDIDATES_OK);
    CHECK(entry.address_mode == MAC_ADDRESS_EXTENDED && entry.pan_id == 0x1234);
    CHECK(memcmp(entry.coordinator, long_beacon + 5, 8) == 0);
    CHECK(entry.sequence == 0x2b && entry.mac_flags == MAC_FLAG_PENDING);
    CHECK(entry.superframe == 0xcfff && !entry.gts_permit);
    CHECK(entry.short_pending == 2 && entry.extended_pending == 1);
    CHECK(entry.network.device_depth == 0 && entry.network.update_id == 0xa5);
    CHECK(entry.network.tx_offset == 0x123456UL); /* Raw, not a beaconless-consistency claim. */
    CALL(zero_tail());
    memcpy(body, long_beacon, sizeof(long_beacon));
    body[14] &= 0x7f;
    CHECK(nwk_candidates_consider(&table, 15, 1, body, sizeof(long_beacon)) == NWK_CANDIDATES_WITHDRAWN);
    CHECK(table.count == 1);
    CALL(zero_tail());
    memcpy(body, short_beacon, sizeof(short_beacon));
    CHECK(nwk_candidates_consider(&table, 26, 1, body, sizeof(short_beacon)) == NWK_CANDIDATES_ADDED);
    CHECK(nwk_candidates_get(&table, 1, &entry) == NWK_CANDIDATES_OK);
    CHECK(entry.address_mode == MAC_ADDRESS_SHORT && entry.channel == 26);
    for (i = 2; i < 8; i++)
        CHECK(entry.coordinator[i] == 0); /* Reused long-address slot cannot leak its old tail. */
    return 0;
}

static uint16_t make_beacon(void)
{
    memset(&header, 0, sizeof(header));
    header.type = MAC_FRAME_BEACON;
    header.source_mode = mode;
    header.source_pan = 0x1234;
    header.source[0] = 0x78;
    header.source[1] = 0x56;
    header.flags = MAC_FLAG_PENDING;
    header.sequence = 0xa5;
    payload[0] = 0xff;
    payload[1] = 0x8f;
    payload[2] = 0x80;
    payload[3] = (uint8_t)(shorts | (extendeds << 4));
    position = 4;
    for (j = 0; j < shorts; j++) {
        payload[position++] = (uint8_t)(0x21u + j);
        payload[position++] = 0x32;
    }
    for (j = 0; j < extendeds; j++)
        for (k = 0; k < 8; k++)
            payload[position++] = (uint8_t)(0x41u + j + k);
    memcpy(payload + position, short_beacon + 11, 15);
    position += 15;
    CHECK(mac_frame_encode(&header, payload, position, body, 125, &length) == MAC_CODEC_OK);
    CHECK(length == position + (mode == MAC_ADDRESS_SHORT ? 7u : 13u));
    return 0;
}

static uint16_t layouts(void)
{
    CHECK(nwk_candidates_init(&table, NWK_CANDIDATES_CHANNEL_MASK) == NWK_CANDIDATES_OK);
    for (mode = MAC_ADDRESS_SHORT; mode <= MAC_ADDRESS_EXTENDED; mode++) {
        for (shorts = 0; shorts <= 7; shorts++) {
            for (extendeds = 0; extendeds <= 7u - shorts; extendeds++) {
                CALL(make_beacon());
                CHECK(nwk_candidates_consider(&table, 11, 1, body, length)
                      == (shorts == 0 && extendeds == 0 ? NWK_CANDIDATES_ADDED : NWK_CANDIDATES_UPDATED));
                CHECK(table.count == mode - 1u); /* Mode is part of identity, even with equal octets. */
                CHECK(nwk_candidates_get(&table, (uint8_t)(mode - 2u), &entry) == NWK_CANDIDATES_OK);
                CHECK(entry.short_pending == shorts && entry.extended_pending == extendeds);
                CHECK(entry.gts_permit == 1 && entry.network.update_id == 0x7f);
                CHECK(memcmp(entry.network.extended_pan_id, short_beacon + 14, 8) == 0);
                CHECK(entry.coordinator[0] == 0x78 && entry.coordinator[1] == 0x56);
                for (i = 2; i < 8; i++)
                    CHECK(entry.coordinator[i] == 0);
            }
        }
    }
    CHECK(table.count == 2);
    CALL(zero_tail());
    return 0;
}

static uint16_t fill(void)
{
    CHECK(nwk_candidates_init(&table, NWK_CANDIDATES_CHANNEL_MASK) == NWK_CANDIDATES_OK);
    memcpy(body, short_beacon, sizeof(short_beacon));
    for (i = 0; i < NWK_CANDIDATES_CAPACITY; i++) {
        body[3] = (uint8_t)(0x10u + i);
        CHECK(nwk_candidates_consider(&table, 15, 1, body, 26) == NWK_CANDIDATES_ADDED);
        CHECK(table.count == i + 1u);
        CHECK(nwk_candidates_get(&table, i, &entry) == NWK_CANDIDATES_OK);
        CHECK(entry.pan_id == (uint16_t)(0x1210u + i));
        CALL(zero_tail());
    }
    return 0;
}

static uint16_t pressure_and_withdrawal(void)
{
    CALL(fill());
    body[3] = 0x20;
    CALL(unchanged(NWK_CANDIDATES_FULL, 26, 1, 15));
    body[3] = 0x12;
    body[2] = 0; /* Deliberately older sequence and Update ID: last observed wins. */
    body[13] = 0xf8; /* Router Capacity zero, depth15, ED Capacity one. */
    body[22] = 0x12; body[23] = 0x34; body[24] = 0x56; body[25] = 0;
    CHECK(nwk_candidates_consider(&table, 15, 1, body, 26) == NWK_CANDIDATES_UPDATED);
    CHECK(table.count == 4);
    CHECK(nwk_candidates_get(&table, 2, &entry) == NWK_CANDIDATES_OK);
    CHECK(entry.sequence == 0 && entry.network.update_id == 0 && !entry.network.router_capacity);
    CHECK(entry.network.device_depth == 15 && entry.network.tx_offset == 0x563412UL);
    body[13] &= 0x7f;
    CALL(unchanged(NWK_CANDIDATES_BAD_CRC, 26, 0, 15));
    body[13] |= 1;
    CALL(unchanged(NWK_CANDIDATES_NWK_REJECTED, 26, 1, 15));
    body[13] &= 0xfe;
    CHECK(nwk_candidates_consider(&table, 15, 1, body, 26) == NWK_CANDIDATES_WITHDRAWN);
    CHECK(table.count == 3 && table.entries[2].pan_id == 0x1213);
    CALL(zero_tail());
    CALL(unchanged(NWK_CANDIDATES_NOT_ELIGIBLE, 26, 1, 15));
    body[13] |= 0x80;
    CHECK(nwk_candidates_consider(&table, 15, 1, body, 26) == NWK_CANDIDATES_ADDED);
    CHECK(table.count == 4 && table.entries[3].pan_id == 0x1212);
    /* Withdraw each possible index while full; preserve order and zero the tail. */
    for (mode = 0; mode < 4; mode++) {
        CALL(fill());
        body[3] = (uint8_t)(0x10u + mode);
        body[8] &= 0x7f; /* MAC Association Permit clear, ED Capacity still one. */
        CHECK(nwk_candidates_consider(&table, 15, 1, body, 26) == NWK_CANDIDATES_WITHDRAWN);
        CHECK(table.count == 3);
        for (i = 0; i < 3; i++)
            CHECK(table.entries[i].pan_id == 0x1210u + i + (i >= mode ? 1u : 0u));
        CALL(zero_tail());
    }
    CALL(fill());
    for (i = 0; i < 4; i++) {
        body[3] = (uint8_t)(0x10u + i);
        body[8] &= 0x7f;
        CHECK(nwk_candidates_consider(&table, 15, 1, body, 26) == NWK_CANDIDATES_WITHDRAWN);
        CHECK(table.count == 3u - i);
        CALL(zero_tail());
    }
    body[8] |= 0x80;
    CHECK(nwk_candidates_consider(&table, 15, 1, body, 26) == NWK_CANDIDATES_ADDED);
    CHECK(table.count == 1);
    CHECK(nwk_candidates_init(&table, UINT32_C(1) << 26) == NWK_CANDIDATES_OK);
    CHECK(table.count == 0);
    CALL(zero_tail());
    return 0;
}

static uint16_t identity_and_filters(void)
{
    CHECK(nwk_candidates_init(&table, NWK_CANDIDATES_CHANNEL_MASK) == NWK_CANDIDATES_OK);
    memcpy(body, short_beacon, 26);
    CHECK(nwk_candidates_consider(&table, 15, 1, body, 26) == NWK_CANDIDATES_ADDED);
    CHECK(nwk_candidates_consider(&table, 16, 1, body, 26) == NWK_CANDIDATES_ADDED);
    body[3]++;
    CHECK(nwk_candidates_consider(&table, 15, 1, body, 26) == NWK_CANDIDATES_ADDED);
    body[3]--;
    body[14]++;
    CHECK(nwk_candidates_consider(&table, 15, 1, body, 26) == NWK_CANDIDATES_ADDED);
    body[14]--;
    body[5]++;
    CALL(unchanged(NWK_CANDIDATES_FULL, 26, 1, 15));
    body[5]--;
    body[12] = 0x21; /* Structurally valid changed profile withdraws this identity only. */
    CHECK(nwk_candidates_consider(&table, 15, 1, body, 26) == NWK_CANDIDATES_WITHDRAWN);
    CHECK(table.count == 3 && table.entries[0].channel == 16);
    body[12] = 0x22;
    body[5]++;
    CHECK(nwk_candidates_consider(&table, 15, 1, body, 26) == NWK_CANDIDATES_ADDED);
    CHECK(table.entries[3].coordinator[0] == 0x79);
    body[7] &= 0xf0; /* Changed BO withdraws, not a malformed frame. */
    CHECK(nwk_candidates_consider(&table, 15, 1, body, 26) == NWK_CANDIDATES_WITHDRAWN);
    CHECK(table.count == 3);
    CHECK(nwk_candidates_init(&table, NWK_CANDIDATES_CHANNEL_MASK) == NWK_CANDIDATES_OK);
    memcpy(body, short_beacon, 26);
    for (i = 0; i < 15; i++) {
        body[7] = (uint8_t)(0xf0u | i);
        CALL(unchanged(NWK_CANDIDATES_NOT_ELIGIBLE, 26, 1, 15));
    }
    for (i = 0; i < 16; i++) {
        body[7] = (uint8_t)((i << 4) | 15u); /* SO ignored on receive with BO15. */
        CHECK(nwk_candidates_consider(&table, 15, 1, body, 26)
              == (i == 0 ? NWK_CANDIDATES_ADDED : NWK_CANDIDATES_UPDATED));
        CHECK(table.entries[0].superframe == (uint16_t)(0x8f00u | body[7]));
    }
    body[13] &= 0x7f;
    CHECK(nwk_candidates_consider(&table, 15, 1, body, 26) == NWK_CANDIDATES_WITHDRAWN);
    body[8] &= 0x7f;
    CALL(unchanged(NWK_CANDIDATES_NOT_ELIGIBLE, 26, 1, 15));
    body[13] |= 0x80;
    CALL(unchanged(NWK_CANDIDATES_NOT_ELIGIBLE, 26, 1, 15));
    return 0;
}

static uint16_t errors(void)
{
    CALL(fill());
    memcpy(body, short_beacon, 26);
    body[3] = 0x10; /* Existing identity: corrupt input must never withdraw it. */
    CALL(unchanged(NWK_CANDIDATES_INVALID_ARGUMENT, 26, 2, 15));
    CALL(unchanged(NWK_CANDIDATES_INVALID_ARGUMENT, 26, 1, 10));
    CALL(unchanged(NWK_CANDIDATES_INVALID_ARGUMENT, 26, 1, 27));
    CALL(unchanged(NWK_CANDIDATES_BAD_CRC, 0, 0, 15));
    for (i = 0; i < 26; i++)
        CALL(unchanged(i < 11 ? NWK_CANDIDATES_MAC_REJECTED : NWK_CANDIDATES_NWK_REJECTED,
                       i, 1, 15));
    CALL(unchanged(NWK_CANDIDATES_NWK_REJECTED, 27, 1, 15));
    CALL(unchanged(NWK_CANDIDATES_MAC_REJECTED, 126, 1, 15));
    body[0] = MAC_FLAG_SECURITY;
    CALL(unchanged(NWK_CANDIDATES_MAC_REJECTED, 26, 1, 15));
    body[0] = 0;
    body[1] |= 0x10; /* Unsupported Beacon version1. */
    CALL(unchanged(NWK_CANDIDATES_MAC_REJECTED, 26, 1, 15));
    body[1] = 0x80;
    body[9] |= 1; /* GTS descriptor layouts unsupported by the actual MAC decoder. */
    CALL(unchanged(NWK_CANDIDATES_MAC_REJECTED, 26, 1, 15));
    body[9] = 0x80;
    body[10] = 0x44; /* More than seven total pending addresses. */
    CALL(unchanged(NWK_CANDIDATES_MAC_REJECTED, 26, 1, 15));
    body[10] = 0;
    body[11] = 1;
    CALL(unchanged(NWK_CANDIDATES_NWK_REJECTED, 26, 1, 15));
    body[11] = 0;
    body[12] = 0x32;
    CALL(unchanged(NWK_CANDIDATES_NWK_REJECTED, 26, 1, 15));
    body[12] = 0x22;
    memset(body + 14, 0, 8);
    CALL(unchanged(NWK_CANDIDATES_NWK_REJECTED, 26, 1, 15));
    memset(body + 14, 0xff, 8);
    CALL(unchanged(NWK_CANDIDATES_NWK_REJECTED, 26, 1, 15));
    body[0] = MAC_FRAME_ACK; body[1] = 0; body[2] = 0;
    CALL(unchanged(NWK_CANDIDATES_NOT_BEACON, 3, 1, 15));
    saved = table;
    CHECK(nwk_candidates_consider(NULL, 15, 1, body, 3) == NWK_CANDIDATES_INVALID_ARGUMENT);
    CHECK(nwk_candidates_consider(&table, 15, 1, NULL, 3) == NWK_CANDIDATES_INVALID_ARGUMENT);
    CHECK(memcmp(&table, &saved, sizeof(table)) == 0);
    memset(&entry, 0xc7, sizeof(entry));
    saved_entry = entry;
    CHECK(nwk_candidates_get(NULL, 0, &entry) == NWK_CANDIDATES_INVALID_ARGUMENT);
    CHECK(nwk_candidates_get(&table, 0, NULL) == NWK_CANDIDATES_INVALID_ARGUMENT);
    CHECK(nwk_candidates_get(&table, 4, &entry) == NWK_CANDIDATES_BAD_INDEX);
    CHECK(nwk_candidates_get(&table, 255, &entry) == NWK_CANDIDATES_BAD_INDEX);
    CHECK(memcmp(&entry, &saved_entry, sizeof(entry)) == 0);
    for (mode = 0; mode < 4; mode++) {
        CALL(fill());
        saved_entry = entry;
        if (mode == 0)
            table.count = 5;
        else if (mode == 1)
            table.version = 0;
        else if (mode == 2)
            table.channel_mask = 0;
        else
            table.channel_mask |= 1;
        CALL(unchanged(NWK_CANDIDATES_INVALID_TABLE, 26, 1, 15));
        CHECK(nwk_candidates_get(&table, 0, &entry) == NWK_CANDIDATES_INVALID_TABLE);
        CHECK(memcmp(&entry, &saved_entry, sizeof(entry)) == 0);
    }
    CHECK(nwk_candidates_init(&table, UINT32_C(1) << 11) == NWK_CANDIDATES_OK);
    memcpy(body, short_beacon, 26);
    CALL(unchanged(NWK_CANDIDATES_OUTSIDE_MASK, 26, 1, 15));
    return 0;
}

static uint16_t self_test(void)
{
    CALL(initialization());
    CALL(golden_and_copy());
    CALL(layouts());
    CALL(pressure_and_withdrawal());
    CALL(identity_and_filters());
    CALL(errors());
    return 0;
}

#ifdef CC2530_HOST_TEST
#include <stdio.h>
#include <stdlib.h>

static uint16_t exact_and_fields(void)
{
    uint8_t *input;
    nwk_candidate_t *output;
    unsigned n, field, value;
    nwk_candidates_result_t expected;
    static const uint8_t fields[] = {0, 1, 7, 8, 9, 10, 11, 12, 13};
    mac_frame_info_t mac;
    mac_beacon_info_t beacon;
    nwk_beacon_t network;

    output = malloc(sizeof(*output));
    CHECK(output != NULL);
    /* Exact-sized inputs for no-pending, mixed and maximum pending layouts. */
    for (mode = 0; mode < 3; mode++) {
        if (mode == 0) {
            memcpy(body, short_beacon, 26); length = 26; position = 11;
        } else if (mode == 1) {
            memcpy(body, long_beacon, 44); length = 44; position = 29;
        } else {
            mode = MAC_ADDRESS_EXTENDED; shorts = 0; extendeds = 7;
            CALL(make_beacon());
            mode = 2; position = 73;
            CHECK(length == 88);
        }
        for (n = 0; n <= 126; n++) {
            input = malloc(n ? n : 1u);
            CHECK(input != NULL);
            memset(input, 0x69, n);
            memcpy(input, body, n < length ? n : length);
            CHECK(nwk_candidates_init(&table, NWK_CANDIDATES_CHANNEL_MASK) == NWK_CANDIDATES_OK);
            saved = table;
            expected = n < position || n > position + 52u || n > 125u
                       ? NWK_CANDIDATES_MAC_REJECTED
                       : n == length ? NWK_CANDIDATES_ADDED : NWK_CANDIDATES_NWK_REJECTED;
            CHECK(nwk_candidates_consider(&table, 26, 1, input, (uint16_t)n) == expected);
            if (expected != NWK_CANDIDATES_ADDED)
                CHECK(memcmp(&table, &saved, sizeof(table)) == 0);
            else {
                CHECK(nwk_candidates_get(&table, 0, output) == NWK_CANDIDATES_OK);
                CHECK(output->channel == 26 && output->network.stack_profile == 2);
            }
            free(input);
        }
    }
    free(output);
    /* All values of each filter/control byte. Real independent codec results
     * determine syntax errors; policy expectations below are explicit.
     */
    for (field = 0; field < sizeof(fields); field++) {
        for (value = 0; value < 256; value++) {
            memcpy(body, short_beacon, 26);
            body[fields[field]] = (uint8_t)value;
            CHECK(nwk_candidates_init(&table, NWK_CANDIDATES_CHANNEL_MASK) == NWK_CANDIDATES_OK);
            saved = table;
            if (mac_frame_decode(body, 26, &mac) != MAC_CODEC_OK)
                expected = NWK_CANDIDATES_MAC_REJECTED;
            else if (mac.header.type != MAC_FRAME_BEACON)
                expected = NWK_CANDIDATES_NOT_BEACON;
            else {
                CHECK(mac_beacon_decode(body + mac.payload_offset, mac.payload_length, &beacon) == MAC_CODEC_OK);
                if (nwk_beacon_decode(body + mac.payload_offset + beacon.payload_offset,
                                      beacon.payload_length, &network) != NWK_BEACON_OK)
                    expected = NWK_CANDIDATES_NWK_REJECTED;
                else
                    expected = network.stack_profile == 2 && (beacon.superframe_specification & 15u) == 15u
                               && (beacon.superframe_specification & 0x8000u) && network.end_device_capacity
                               ? NWK_CANDIDATES_ADDED : NWK_CANDIDATES_NOT_ELIGIBLE;
            }
            CHECK(nwk_candidates_consider(&table, 15, 1, body, 26) == expected);
            if (expected != NWK_CANDIDATES_ADDED)
                CHECK(memcmp(&table, &saved, sizeof(table)) == 0);
        }
    }
    CHECK(nwk_candidates_init(&table, UINT32_C(1) << 11) == NWK_CANDIDATES_OK);
    memcpy(body, short_beacon, 26);
    CHECK(nwk_candidates_consider(&table, 11, 1, body, 26) == NWK_CANDIDATES_ADDED);
    for (value = 0; value < 256; value++) {
        CALL(unchanged(value == 0 ? NWK_CANDIDATES_BAD_CRC : value == 1
                       ? NWK_CANDIDATES_UPDATED : NWK_CANDIDATES_INVALID_ARGUMENT,
                       26, (uint8_t)value, 11));
        CALL(unchanged(value < 11 || value > 26 ? NWK_CANDIDATES_INVALID_ARGUMENT
                       : value == 11 ? NWK_CANDIDATES_UPDATED : NWK_CANDIDATES_OUTSIDE_MASK,
                       26, 1, (uint8_t)value));
    }
    return 0;
}

int main(void)
{
    uint16_t result = self_test();
    if (!result)
        result = exact_and_fields();
    if (result) {
        fprintf(stderr, "NWK candidates failed at C line %u\n", (unsigned)result);
        return 1;
    }
    puts("NWK candidates: golden/state corpus, 72 pending layouts, exact sizes and field/channel/CRC matrices PASS");
    return 0;
}
#else
volatile __xdata __at(0x1e00) uint8_t nwk_candidates_test_result[8];

void main(void)
{
    uint16_t result = self_test();
    nwk_candidates_test_result[0] = 'N';
    nwk_candidates_test_result[1] = 'C';
    nwk_candidates_test_result[2] = 'D';
    nwk_candidates_test_result[3] = '1';
    nwk_candidates_test_result[4] = 1;
    nwk_candidates_test_result[5] = 8;
    nwk_candidates_test_result[6] = (uint8_t)result;
    nwk_candidates_test_result[7] = (uint8_t)(result >> 8);
    __asm
        .globl _nwk_candidates_test_done
    _nwk_candidates_test_done:
        nop
    __endasm;
    for (;;) {}
}
#endif
