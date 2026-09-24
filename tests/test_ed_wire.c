/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "ed_wire.h"
#include "security_aes_model.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static ed_packet_t packet, decoded, untouched;
static zigbee_security_key_t key;
static zigbee_security_info_t info, saved_info;
static uint8_t plain[116], sealed[116], opened[116], legacy[116], damaged[116], n;
static unsigned checks;
#define CHECK(x) do { assert(x); checks++; } while (0)

static void setup(uint8_t kind)
{
    uint8_t i;
    memset(&packet, 0, sizeof(packet));
    memset(&key, 0, sizeof(key));
    packet.nwk.type = kind == 4 ? 1 : 0;
    packet.nwk.version = 2; packet.nwk.source = 0x1234;
    packet.nwk.destination = 0; packet.nwk.radius = 12; packet.nwk.sequence = 255;
    packet.aps.counter = kind == 4 ? 0 : 255;
    packet.aps.cluster_id = kind == 0 || kind == 1 || kind == 2 ? 0x8002 : 0;
    packet.aps.type = kind == 2 || kind == 3 ? 2 : kind == 5 ? 1 : 0;
    packet.aps.delivery_mode = kind == 1 ? 2 : 0;
    if (kind == 3) packet.aps.flags = APS_FLAG_ACK_FORMAT;
    if (kind == 1) packet.nwk.destination = 0xfffd;
    packet.length = packet.aps.type == 2 ? 0 : 3;
    if (packet.length) {
        packet.payload[0] = 8; packet.payload[1] = 4; packet.payload[2] = 0x69;
    }
    for (i = 0; i < 16; i++) key.key[i] = i;
    for (i = 0; i < 8; i++) key.source[i] = i+1;
    key.counter = 0xfffffff0UL; key.level = 5; key.extended_nonce = 1;
    key.limits.block_timeout = 1000; key.limits.block_polls = 1000;
}

static void roundtrip(uint8_t kind, uint8_t layer)
{
    uint8_t *input, size, cipher_length, i;
    setup(kind);
    CHECK(ed_wire_encode(&packet, plain, sizeof(plain), &n) == ZIGBEE_SECURITY_OK);
    memset(&decoded, 0xa5, sizeof(decoded));
    CHECK(ed_wire_decode(plain, n, &decoded) == ZIGBEE_SECURITY_OK);
    CHECK(!memcmp(&decoded, &packet, sizeof(packet)));
    input = plain+(layer ? 8 : 0); size = (uint8_t)(n-(layer ? 8 : 0));
    key.key_identifier = layer ? 0 : 1;
    security_aes_reset();
    CHECK(ed_wire_crypt(0, layer, &key, input, size, sealed, sizeof(sealed), &info) == 0);
    cipher_length = info.length;
    CHECK(security_aes_blocks() > 0);
    if ((kind == 0 || kind == 5) && !(layer && kind == 5 && !packet.length)) {
        security_aes_reset();
        CHECK(zigbee_security_crypt(0, layer, &key, input, size, legacy, sizeof(legacy), &info) == 0);
        CHECK(info.length == cipher_length && !memcmp(legacy, sealed, cipher_length));
    }
    for (i = 0; i < cipher_length; i++) {
        memcpy(damaged, sealed, cipher_length); damaged[i] ^= 0x80;
        memset(opened, 0xa5, sizeof(opened)); memset(&info, 0xa5, sizeof(info)); saved_info = info;
        security_aes_reset();
        CHECK(ed_wire_crypt(1, layer, &key, damaged, cipher_length, opened, sizeof(opened), &info) != 0);
        CHECK(!memcmp(&info, &saved_info, sizeof(info)));
        for (size_t j = 0; j < sizeof(opened); j++) CHECK(opened[j] == 0xa5);
    }
    security_aes_reset();
    CHECK(ed_wire_crypt(1, layer, &key, sealed, cipher_length, opened, sizeof(opened), &info) == 0);
    CHECK(info.length == size && !memcmp(opened, input, size));
    sealed[(layer ? (packet.aps.type == 1 || kind == 3 ? 2 : 8) : 8)] |= 7;
    security_aes_reset();
    CHECK(ed_wire_crypt(1, layer, &key, sealed, cipher_length, opened, sizeof(opened), &info) == 0);
    CHECK(info.length == size && !memcmp(opened, input, size));
}

int main(void)
{
    unsigned kind, length, field;
    for (kind = 0; kind < 6; kind++) {
        roundtrip((uint8_t)kind, 0);
        if (kind != 4) roundtrip((uint8_t)kind, 1);
    }
    for (kind = 0; kind < 6; kind++) for (length = 0; length <= ED_PAYLOAD_MAX; length++) {
        setup((uint8_t)kind); packet.length = (uint8_t)length;
        memset(packet.payload, 0, sizeof(packet.payload));
        memset(packet.payload, 0x69, length);
        if ((packet.aps.type == 2 && length) || ((packet.aps.type == 1 || packet.nwk.type) && !length))
            CHECK(ed_wire_encode(&packet, plain, sizeof(plain), &n) != 0);
        else {
            CHECK(ed_wire_encode(&packet, plain, sizeof(plain), &n) == 0);
            CHECK(ed_wire_decode(plain, n, &decoded) == 0);
            CHECK(decoded.length == length && !memcmp(decoded.payload, packet.payload, length));
            memset(&untouched, 0xa5, sizeof(untouched));
            for (field = 0; field < (kind == 4 ? 9u : kind == 3 || kind == 5 ? 10u : 16u); field++) {
                decoded = untouched;
                CHECK(ed_wire_decode(plain, (uint16_t)field, &decoded) != 0);
                CHECK(!memcmp(&decoded, &untouched, sizeof(decoded)));
            }
        }
    }
    for (field = 3; field < 256; field++) {
        setup(0); packet.aps.type = (uint8_t)field;
        CHECK(ed_wire_encode(&packet, plain, sizeof(plain), &n) != 0);
    }
    setup(0); packet.aps.flags = 1;
    CHECK(ed_wire_encode(&packet, plain, sizeof(plain), &n) != 0);
    CHECK(ed_wire_decode(plain, 0xffff, &decoded) != 0);
    CHECK(ed_wire_encode(NULL, plain, sizeof(plain), &n) != 0);
    printf("ED wire: %u checks PASS; original AES/CCM, extended headers, no admission claims.\n", checks);
    return 0;
}
