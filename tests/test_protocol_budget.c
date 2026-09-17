/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "mac_frame.h"
#include "nwk_frame.h"
#include "aps_frame.h"
#include "zcl_dispatch.h"
#include "cc2530_mmio.h"

#include <string.h>

#define CHECK(condition) do { if (!(condition)) return (uint16_t)__LINE__; } while (0)

static const MCU_CODE uint8_t scalar[] = {0x78, 0x56};
static const MCU_CODE uint8_t string_data[92] = {0x69, 0xa5, 0};
static const MCU_CODE zcl_attribute_t code_attributes[] = {
    {0x1122, 1, {ZCL_TYPE_UINT16, 0, scalar, 2}},
    {0x3344, 1, {ZCL_TYPE_OCTET_STRING, 0, string_data, 92}}
};
static const MCU_CODE zcl_attribute_t reserved_attribute = {0xffff, 1, {ZCL_TYPE_UINT16, 0, scalar, 2}};
static const MCU_CODE uint8_t discover_golden[] = {
    0x61, 0x98, 0x5b, 0x34, 0x12, 0x34, 0x12, 0x78, 0x56,
    0x08, 0x20, 0x34, 0x12, 0x78, 0x56, 0x1e, 0xa6,
    0x40, 0x31, 0x78, 0x56, 0x34, 0x12, 0x21, 0xe8,
    0x18, 0x4d, 0x0d, 0, 0x22, 0x11, 0x21
};
static const MCU_CODE uint8_t read_golden[] = {
    0x61, 0x98, 0x5b, 0x34, 0x12, 0x34, 0x12, 0x78, 0x56,
    0x08, 0x20, 0x34, 0x12, 0x78, 0x56, 0x1e, 0xa6,
    0x40, 0x31, 0x78, 0x56, 0x34, 0x12, 0x21, 0xe8,
    0x18, 0x4d, 0x01, 0x22, 0x11, 0, 0x21, 0x78, 0x56
};
static mac_header_t mac;
static nwk_header_t nwk;
static aps_header_t aps;
static zcl_header_t zcl;
static mac_frame_info_t frame;
static nwk_frame_info_t network;
static aps_frame_info_t transport;
static zcl_frame_info_t application;
static zcl_value_info_t value;
static zcl_attribute_set_t table;
static zcl_dispatch_info_t dispatch;
static uint8_t body[125], npdu[116], apdu[108], command[100], reply[100], ids[3];
static uint8_t mac_length, nwk_length, aps_length, zcl_length, offset;

static void headers(uint8_t manufacturer)
{
    memset(&mac, 0, sizeof(mac));
    mac.type = MAC_FRAME_DATA;
    mac.version = 1;
    mac.flags = MAC_FLAG_PAN_COMPRESSION | MAC_FLAG_ACK_REQUEST;
    mac.sequence = 0x5a;
    mac.destination_pan = mac.source_pan = 0x1234;
    mac.destination_mode = mac.source_mode = MAC_ADDRESS_SHORT;
    mac.destination[0] = 0x78;
    mac.destination[1] = 0x56;
    mac.source[0] = 0x34;
    mac.source[1] = 0x12;
    memset(&nwk, 0, sizeof(nwk));
    nwk.version = NWK_FRAME_PROTOCOL_VERSION;
    nwk.destination = 0x5678;
    nwk.source = 0x1234;
    nwk.radius = 0x1e;
    nwk.sequence = 0xa5;
    memset(&aps, 0, sizeof(aps));
    aps.flags = APS_FLAG_ACK_REQUEST;
    aps.destination_endpoint = 0x21;
    aps.source_endpoint = 0x31;
    aps.cluster_id = 0x5678;
    aps.profile_id = 0x1234;
    aps.counter = 0xe7;
    memset(&zcl, 0, sizeof(zcl));
    zcl.sequence = 0x4d;
    zcl.manufacturer_code = 0x9abc;
    zcl.flags = manufacturer ? ZCL_FLAG_MANUFACTURER_SPECIFIC : 0;
    table.attributes = code_attributes;
    table.count = 2;
    table.side = ZCL_ATTRIBUTE_SERVER;
    table.manufacturer_specific = manufacturer;
    table.manufacturer_code = 0x9abc;
}

static uint16_t encode_layers(const uint8_t *payload, uint8_t length)
{
    CHECK(aps_frame_encode(&aps, payload, length, apdu, sizeof(apdu), &aps_length) == APS_CODEC_OK);
    CHECK(nwk_frame_encode(&nwk, apdu, aps_length, npdu, sizeof(npdu), &nwk_length) == NWK_CODEC_OK);
    CHECK(mac_frame_encode(&mac, npdu, nwk_length, body, sizeof(body), &mac_length) == MAC_CODEC_OK);
    return 0;
}

static uint16_t decode_layers(void)
{
    CHECK(mac_frame_decode(body, mac_length, &frame) == MAC_CODEC_OK);
    CHECK(nwk_frame_decode(body + frame.payload_offset, frame.payload_length, &network) == NWK_CODEC_OK);
    offset = (uint8_t)(frame.payload_offset + network.payload_offset);
    CHECK(aps_frame_decode(body + offset, network.payload_length, &transport) == APS_CODEC_OK);
    offset += transport.payload_offset;
    return 0;
}

static uint16_t run_tests(void)
{
    volatile uint8_t manufacturer, step, i;
    for (manufacturer = 0; manufacturer < 2; manufacturer++) {
        ids[0] = ids[1] = 0;
        ids[2] = 1;
        for (step = 0; step < 5; step++) {
            headers(manufacturer);
            zcl.command_id = step == 0 ? ZCL_COMMAND_DISCOVER_ATTRIBUTES : ZCL_COMMAND_READ_ATTRIBUTES;
            if (step == 2) {
                ids[0] = 0x44;
                ids[1] = 0x33;
            }
            if (step == 4)
                ids[0] = ids[1] = 0xff;
            CHECK(zcl_frame_encode(&zcl, ids, step == 0 ? 3u : 2u, command, sizeof(command), &zcl_length) == ZCL_CODEC_OK);
            CHECK(encode_layers(command, zcl_length) == 0 && decode_layers() == 0);
            CHECK(network.header.destination == 0x5678 && transport.header.destination_endpoint == 0x21);
            CHECK(transport.header.profile_id == 0x1234 && transport.header.cluster_id == 0x5678);
            CHECK(zcl_dispatch_unicast(&table, body + offset, transport.payload_length,
                                       reply, step == 3 ? 6u + 2u * manufacturer : 100u, &dispatch) == ZCL_CODEC_OK);
            CHECK(dispatch.kind == ZCL_DISPATCH_RESPONSE && dispatch.requested_count == 1 && dispatch.returned_count == 1);
            memcpy(mac.destination, frame.header.source, 8);
            memcpy(mac.source, frame.header.destination, 8);
            mac.sequence = 0x5b;
            nwk.destination = network.header.source;
            nwk.source = network.header.destination;
            nwk.flags = NWK_FLAG_END_DEVICE_INITIATOR;
            nwk.sequence = 0xa6;
            aps.destination_endpoint = transport.header.source_endpoint;
            aps.source_endpoint = transport.header.destination_endpoint;
            aps.counter = 0xe8;
            CHECK(encode_layers(reply, dispatch.length) == 0 && decode_layers() == 0);
            CHECK(network.header.destination == 0x1234 && network.header.source == 0x5678);
            CHECK(transport.header.destination_endpoint == 0x31 && transport.header.source_endpoint == 0x21);
            CHECK(zcl_frame_decode(body + offset, transport.payload_length, &application) == ZCL_CODEC_OK);
            CHECK(application.header.command_id == (step == 0 ? 0x0du : 1u));
            CHECK(application.header.sequence == 0x4d && application.header.flags == (manufacturer ? 0x1cu : 0x18u));
            CHECK(application.header.manufacturer_code == (manufacturer ? 0x9abcu : 0u));
            offset += application.payload_offset;
            if (step == 0) {
                CHECK(dispatch.discovery_complete == 0 && application.payload_length == 4 && body[offset] == 0);
                CHECK(body[offset + 1u] == 0x22 && body[offset + 2u] == 0x11 && body[offset + 3u] == 0x21);
                ids[0] = body[offset + 1u];
                ids[1] = body[offset + 2u];
                if (!manufacturer)
                    CHECK(mac_length == sizeof(discover_golden) && memcmp(body, discover_golden, sizeof(discover_golden)) == 0);
            } else {
                CHECK(body[offset] == ids[0] && body[offset + 1u] == ids[1]);
                if (step == 4) {
                    CHECK(body[offset + 2u] == ZCL_STATUS_UNSUPPORTED_ATTRIBUTE && application.payload_length == 3);
                } else if (step == 3 || (step == 2 && manufacturer)) {
                    CHECK(body[offset + 2u] == ZCL_STATUS_INSUFFICIENT_SPACE && application.payload_length == 3);
                } else {
                    CHECK(body[offset + 2u] == 0);
                    CHECK(zcl_value_decode(body[offset + 3u], body + offset + 4u,
                                           (uint16_t)(application.payload_length - 4u), &value) == ZCL_CODEC_OK);
                    if (step == 1) {
                        CHECK(value.type == ZCL_TYPE_UINT16 && value.encoded_length == 2);
                        CHECK(memcmp(body + offset + 4u, scalar, 2) == 0);
                        if (!manufacturer)
                            CHECK(mac_length == sizeof(read_golden) && memcmp(body, read_golden, sizeof(read_golden)) == 0);
                    } else {
                        CHECK(mac_length == 125 && value.type == ZCL_TYPE_OCTET_STRING && value.data_length == 92);
                        CHECK(memcmp(body + offset + 4u + value.data_offset, string_data, 92) == 0);
                    }
                }
            }
        }
    }
    headers(0);
    command[0] = 0;
    command[1] = 0x4d;
    command[2] = ZCL_COMMAND_WRITE_NO_RESPONSE;
    CHECK(encode_layers(command, 3) == 0 && decode_layers() == 0);
    memset(reply, 0xa5, sizeof(reply));
    CHECK(zcl_dispatch_unicast(&table, body + offset, transport.payload_length,
                               reply, sizeof(reply), &dispatch) == ZCL_CODEC_UNSUPPORTED_NO_RESPONSE);
    for (step = 0; step < sizeof(reply); step++)
        CHECK(reply[step] == 0xa5);
    table.attributes = &reserved_attribute;
    table.count = 1;
    command[3] = command[4] = 0xff;
    command[5] = 1;
    for (step = 0; step < 2; step++) {
        command[2] = step ? ZCL_COMMAND_DISCOVER_ATTRIBUTES : ZCL_COMMAND_READ_ATTRIBUTES;
        CHECK(encode_layers(command, step ? 6u : 5u) == 0 && decode_layers() == 0);
        memset(reply, 0xa5, sizeof(reply));
        memset(&dispatch, 0xa5, sizeof(dispatch));
        CHECK(zcl_dispatch_unicast(&table, body + offset, transport.payload_length,
                                   reply, sizeof(reply), &dispatch) == ZCL_CODEC_INVALID_TABLE);
        for (i = 0; i < sizeof(reply); i++)
            CHECK(reply[i] == 0xa5);
        for (i = 0; i < sizeof(dispatch); i++)
            CHECK(((const uint8_t *)&dispatch)[i] == 0xa5);
    }
    return 0;
}

#ifndef CC2530_HOST_TEST
volatile MCU_XDATA MCU_AT(0x1e00) uint8_t protocol_budget_result[8];

void main(void)
{
    uint16_t result = run_tests();
    protocol_budget_result[0] = 'P';
    protocol_budget_result[1] = 'B';
    protocol_budget_result[2] = 'G';
    protocol_budget_result[3] = '1';
    protocol_budget_result[4] = 1;
    protocol_budget_result[5] = 8;
    protocol_budget_result[6] = (uint8_t)result;
    protocol_budget_result[7] = (uint8_t)(result >> 8);
    __asm
        .globl _protocol_budget_done
    _protocol_budget_done:
        nop
    __endasm;
    for (;;) {}
}
#else
#include <stdio.h>

int main(void)
{
    uint16_t result = run_tests();
    if (result) {
        fprintf(stderr, "Integrated protocol resource test failed at line %u\n", result);
        return 1;
    }
    puts("Integrated MAC/NWK/APS/ZCL: Discover-to-Read, golden replies and bounded errors PASS.");
    return 0;
}
#endif
