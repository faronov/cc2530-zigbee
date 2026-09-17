/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "aps_frame.h"
#include "mac_frame.h"
#include "nwk_frame.h"
#include "cc2530_mmio.h"

#include <string.h>

#define CHECK(condition) do { if (!(condition)) return (uint16_t)__LINE__; } while (0)

static const MCU_CODE uint8_t golden[] = {
    0x61, 0x98, 0x5a, 0x34, 0x12, 0x45, 0x45, 0x67, 0x67,
    0x08, 0x20, 0x78, 0x56, 0x34, 0x12, 0x1e, 0xa5,
    0x40, 0x21, 0x78, 0x56, 0x34, 0x12, 0x31, 0xe7, 0xa9, 0x55, 0x69
};
static const MCU_CODE uint8_t application[100] = {0xa9, 0x55, 0x69};
static mac_header_t mac;
static nwk_header_t nwk;
static aps_header_t aps;
static mac_frame_info_t frame;
static nwk_frame_info_t network;
static aps_frame_info_t transport, saved;
static uint8_t apdu[108], npdu[116], body[127];
static uint8_t aps_length, nwk_length, mac_length;
static volatile uint8_t mac_size, nwk_size;

static uint8_t filled(const uint8_t *bytes, uint16_t size, uint8_t value)
{
    uint16_t i;
    for (i = 0; i < size; i++)
        if (bytes[i] != value)
            return 0;
    return 1;
}

static void headers(void)
{
    memset(&mac, 0, sizeof(mac));
    mac.type = MAC_FRAME_DATA;
    mac.version = 1;
    mac.flags = MAC_FLAG_PAN_COMPRESSION | MAC_FLAG_ACK_REQUEST;
    mac.sequence = 0x5a;
    mac.destination_pan = mac.source_pan = 0x1234;
    mac.destination_mode = mac.source_mode = MAC_ADDRESS_SHORT;
    memset(mac.destination, 0x45, 8);
    memset(mac.source, 0x67, 8);
    memset(&nwk, 0, sizeof(nwk));
    nwk.version = NWK_FRAME_PROTOCOL_VERSION;
    nwk.flags = NWK_FLAG_END_DEVICE_INITIATOR;
    nwk.destination = 0x5678;
    nwk.source = 0x1234;
    nwk.radius = 0x1e;
    nwk.sequence = 0xa5;
    memset(nwk.destination_ieee, 0x21, 8);
    memset(nwk.source_ieee, 0x31, 8);
    memset(&aps, 0, sizeof(aps));
    aps.flags = APS_FLAG_ACK_REQUEST;
    aps.destination_endpoint = 0x21;
    aps.source_endpoint = 0x31;
    aps.cluster_id = 0x5678;
    aps.profile_id = 0x1234;
    aps.counter = 0xe7;
}

static uint16_t round_trip(uint8_t payload_size)
{
    CHECK(aps_frame_encode(&aps, application, payload_size, apdu, sizeof(apdu), &aps_length) == APS_CODEC_OK);
    CHECK(nwk_frame_encode(&nwk, apdu, aps_length, npdu, sizeof(npdu), &nwk_length) == NWK_CODEC_OK);
    memset(body, 0xc7, sizeof(body));
    CHECK(mac_frame_encode(&mac, npdu, nwk_length, body + 1, 125, &mac_length) == MAC_CODEC_OK);
    CHECK(mac_length == mac_size + nwk_size + 8u + payload_size);
    CHECK(body[0] == 0xc7 && filled(body + mac_length + 1u, sizeof(body) - mac_length - 1u, 0xc7));
    CHECK(mac_frame_decode(body + 1, mac_length, &frame) == MAC_CODEC_OK);
    CHECK(frame.header.type == MAC_FRAME_DATA && frame.header.sequence == 0x5a);
    CHECK(frame.payload_offset == mac_size && frame.payload_length == nwk_length);
    CHECK(frame.header.destination[0] == 0x45 && frame.header.source[0] == 0x67);
    CHECK(nwk_frame_decode(body + 1u + frame.payload_offset, frame.payload_length, &network) == NWK_CODEC_OK);
    CHECK(network.header.type == NWK_FRAME_DATA && network.header.sequence == 0xa5);
    CHECK(network.header.destination == 0x5678 && network.header.source == 0x1234);
    CHECK(network.header.flags == nwk.flags && network.payload_offset == nwk_size);
    CHECK(network.payload_length == aps_length);
    CHECK(aps_frame_decode(body + 1u + frame.payload_offset + network.payload_offset,
                           network.payload_length, &transport) == APS_CODEC_OK);
    CHECK(transport.header.type == APS_FRAME_DATA && transport.header.delivery_mode == APS_DELIVERY_UNICAST);
    CHECK(transport.header.flags == aps.flags && transport.header.counter == 0xe7);
    CHECK(transport.header.destination_endpoint == 0x21 && transport.header.source_endpoint == 0x31);
    CHECK(transport.header.cluster_id == 0x5678 && transport.header.profile_id == 0x1234);
    CHECK(transport.payload_offset == 8 && transport.payload_length == payload_size);
    CHECK(memcmp(body + 1u + frame.payload_offset + network.payload_offset + transport.payload_offset,
                 application, payload_size) == 0);
    return 0;
}

static uint16_t self_test(void)
{
    volatile uint8_t destination, source, option, ack, n, maximum, fault, offset;
    uint16_t result;
    aps_codec_result_t expected;

    headers();
    mac_size = 9;
    nwk_size = 8;
    result = round_trip(3);
    if (result != 0)
        return result;
    CHECK(mac_length == sizeof(golden) && memcmp(body + 1, golden, sizeof(golden)) == 0);
    for (destination = MAC_ADDRESS_SHORT; destination <= MAC_ADDRESS_EXTENDED; destination++) {
        for (source = MAC_ADDRESS_SHORT; source <= MAC_ADDRESS_EXTENDED; source++) {
            mac.destination_mode = destination;
            mac.source_mode = source;
            mac_size = (uint8_t)(9u + (destination == MAC_ADDRESS_EXTENDED ? 6u : 0u)
                                + (source == MAC_ADDRESS_EXTENDED ? 6u : 0u));
            for (option = 0; option < 4; option++) {
                nwk.flags = (uint16_t)(NWK_FLAG_END_DEVICE_INITIATOR | ((uint16_t)option << 11));
                nwk_size = (uint8_t)(8u + (option & 1u ? 8u : 0u) + (option & 2u ? 8u : 0u));
                maximum = (uint8_t)(125u - mac_size - nwk_size - 8u);
                for (ack = 0; ack < 2; ack++) {
                    aps.flags = ack ? APS_FLAG_ACK_REQUEST : 0;
                    for (n = 0; n < 3; n++) {
                        result = round_trip(n == 2 ? maximum : n);
                        if (result != 0)
                            return result;
                    }
                    CHECK(mac_length == 125);
                    offset = (uint8_t)(1u + mac_size + nwk_size);
                    for (fault = 0; fault < 7; fault++) {
                        body[offset] = (uint8_t)(aps.flags | (fault == 0 ? 0x20u : fault == 1 ? 1u
                                                : fault == 2 ? 2u : fault == 3 ? 3u : fault == 4 ? 0x80u
                                                : fault == 5 ? 8u : 12u));
                        CHECK(mac_frame_decode(body + 1, mac_length, &frame) == MAC_CODEC_OK);
                        CHECK(nwk_frame_decode(body + 1u + frame.payload_offset,
                                               frame.payload_length, &network) == NWK_CODEC_OK);
                        memset(&transport, 0xa5, sizeof(transport));
                        memcpy(&saved, &transport, sizeof(saved));
                        expected = fault == 0 ? APS_CODEC_UNSUPPORTED_SECURITY : fault < 4 ? APS_CODEC_UNSUPPORTED_TYPE
                                   : fault == 4 ? APS_CODEC_UNSUPPORTED_LAYOUT : APS_CODEC_UNSUPPORTED_DELIVERY;
                        CHECK(aps_frame_decode(body + offset, network.payload_length, &transport) == expected);
                        CHECK(memcmp(&transport, &saved, sizeof(transport)) == 0);
                    }
                    CHECK(aps_frame_encode(&aps, application, 100, apdu, sizeof(apdu), &aps_length) == APS_CODEC_OK);
                    if (nwk_size > 8) {
                        memset(npdu, 0xc7, sizeof(npdu));
                        nwk_length = 0xa5;
                        CHECK(nwk_frame_encode(&nwk, apdu, aps_length, npdu, sizeof(npdu), &nwk_length) == NWK_CODEC_TOO_LONG);
                        CHECK(nwk_length == 0xa5 && filled(npdu, sizeof(npdu), 0xc7));
                    }
                    CHECK(aps_frame_encode(&aps, application, (uint16_t)(108u - nwk_size),
                                           apdu, sizeof(apdu), &aps_length) == APS_CODEC_OK);
                    CHECK(nwk_frame_encode(&nwk, apdu, aps_length, npdu, sizeof(npdu), &nwk_length) == NWK_CODEC_OK);
                    CHECK(nwk_length == 116);
                    if (mac_size > 9) {
                        memset(body, 0xc7, sizeof(body));
                        mac_length = 0xa5;
                        CHECK(mac_frame_encode(&mac, npdu, nwk_length, body + 1, 125, &mac_length) == MAC_CODEC_TOO_LONG);
                        CHECK(mac_length == 0xa5 && filled(body, sizeof(body), 0xc7));
                    }
                    for (n = 0; n < 8; n++) {
                        CHECK(nwk_frame_encode(&nwk, apdu, n, npdu, sizeof(npdu), &nwk_length) == NWK_CODEC_OK);
                        CHECK(mac_frame_encode(&mac, npdu, nwk_length, body + 1, 125, &mac_length) == MAC_CODEC_OK);
                        CHECK(mac_frame_decode(body + 1, mac_length, &frame) == MAC_CODEC_OK);
                        CHECK(nwk_frame_decode(body + 1u + frame.payload_offset, frame.payload_length, &network) == NWK_CODEC_OK);
                        CHECK(aps_frame_decode(body + 1u + frame.payload_offset + network.payload_offset,
                                               network.payload_length, &transport) == APS_CODEC_TRUNCATED);
                        CHECK(memcmp(&transport, &saved, sizeof(transport)) == 0);
                    }
                }
            }
        }
    }
    return 0;
}

#if defined(__SDCC)
volatile MCU_XDATA MCU_AT(0x1e00) uint8_t protocol_frame_test_result[8];

void main(void)
{
    uint16_t result = self_test();
    protocol_frame_test_result[0] = 'P';
    protocol_frame_test_result[1] = 'R';
    protocol_frame_test_result[2] = 'F';
    protocol_frame_test_result[3] = '1';
    protocol_frame_test_result[4] = 1;
    protocol_frame_test_result[5] = 8;
    protocol_frame_test_result[6] = (uint8_t)result;
    protocol_frame_test_result[7] = (uint8_t)(result >> 8);
    __asm
        .globl _protocol_frame_test_done
    _protocol_frame_test_done:
        nop
    __endasm;
    for (;;) {
    }
}
#else
#include <stdio.h>

int main(void)
{
    uint16_t result = self_test();
    if (result != 0) {
        fprintf(stderr, "Protocol frame integration failed at line %u\n", (unsigned)result);
        return 1;
    }
    puts("host MAC/NWK/APS: golden chain, address/ACK/size matrices and layer-specific failures PASS");
    return 0;
}
#endif
