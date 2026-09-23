/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 * Original synthetic identities and descriptors; no board advertisement.
 */
#include "zdo_srv.h"
#include "nwk_frame.h"
#include "cc2530_mmio.h"
#include <stddef.h>
#include <string.h>

MCU_XDATA uint32_t zdo_srv_checks;
#define CHECK(c) do { zdo_srv_checks++; if (!(c)) return (uint16_t)__LINE__; } while (0)

static const MCU_CODE zdo_srv_local_t config = {
    {0x5678, 0x1234, 0x2345, 2, 0, 8, 0x8c, 0x52, 0, 22, 0}, 0x1234
};
static const MCU_CODE uint8_t query[] = {0x5a, 0x34, 0x12};
static const MCU_CODE uint8_t expected[] = {
    0x5a, 0, 0x34, 0x12, 0x02, 0x40, 0x8c, 0x78, 0x56,
    0x52, 0x34, 0x12, 0, 0x2c, 0x45, 0x23, 0
};
static const MCU_CODE uint8_t golden[] = {
    0x08, 0x20, 0x78, 0x56, 0x34, 0x12, 0x1e, 0x22,
    0, 7, 2, 0x80, 0, 0, 0, 0xaa,
    0x5a, 0, 0x34, 0x12, 0x02, 0x40, 0x8c, 0x78, 0x56,
    0x52, 0x34, 0x12, 0, 0x2c, 0x45, 0x23, 0
};
static zdo_srv_local_t local;
static zdo_srv_rx_t rx;
static zdo_srv_info_t info;
static zdo_node_response_t decoded;
static uint8_t input[101], output[19];
static nwk_header_t nwk;
static nwk_frame_info_t network;
static aps_header_t aps;
static aps_frame_info_t transport;
static uint8_t apdu[108], npdu[116], aps_length, nwk_length;

static uint8_t filled(const void *p, uint16_t size, uint8_t value)
{
    const uint8_t *b = p;
    while (size--)
        if (*b++ != value)
            return 0;
    return 1;
}

static void reset(void)
{
    local = config;
    memset(&rx, 0, sizeof(rx));
    rx.header.cluster_id = ZDO_NODE_REQUEST_CLUSTER;
    rx.header.source_endpoint = 7;
    rx.header.counter = 0xa9;
    memset(input, 0xff, sizeof(input));
    memcpy(input, query, sizeof(query));
    memset(output, 0xc7, sizeof(output));
    memset(&info, 0xa5, sizeof(info));
}

static uint16_t unchanged(zdo_srv_result_t expected_result, uint16_t length, uint16_t capacity)
{
    CHECK(zdo_srv_handle(&local, &rx, input, length, output + 1, capacity, &info) == expected_result);
    CHECK(filled(output, sizeof(output), 0xc7));
    CHECK(filled(&info, sizeof(info), 0xa5));
    return 0;
}

static uint16_t reply_cases(void)
{
    uint16_t n;
    for (n = 0; n <= 18; n++) {
        reset();
        if (n < 17) {
            CHECK(unchanged(ZDO_SRV_SPACE, 3, n) == 0);
        } else {
            CHECK(zdo_srv_handle(&config, &rx, query, 3, output + 1, n, &info) == ZDO_SRV_OK);
            CHECK(info.kind == ZDO_SRV_REPLY && info.cluster_id == 0x8002 && info.endpoint == 7);
            CHECK(info.sequence == 0x5a && info.status == 0 && info.length == 17 && info.consumed == 3);
            CHECK(output[0] == 0xc7 && output[18] == 0xc7 && memcmp(output + 1, expected, 17) == 0);
            CHECK(zdo_node_rsp_decode(output + 1, info.length, &decoded) == ZDO_NODE_OK);
            CHECK(decoded.address == local.address && decoded.descriptor.logical_type == 2);
        }
    }
    for (n = 0; n <= 100; n++) {
        reset();
        if (n < 3) {
            CHECK(unchanged(ZDO_SRV_TRUNCATED, n, 17) == 0);
        } else {
            CHECK(zdo_srv_handle(&local, &rx, input, n, output + 1, 17, &info) == ZDO_SRV_OK);
            CHECK(info.consumed == 3 && info.length == 17 && memcmp(output + 1, expected, 17) == 0);
        }
    }
    reset();
    CHECK(unchanged(ZDO_SRV_TOO_LONG, 101, 17) == 0);
    CHECK(unchanged(ZDO_SRV_TOO_LONG, 65535, 17) == 0);
    input[1] = 0x35;
    CHECK(zdo_srv_handle(&local, &rx, input, 3, output + 1, 4, &info) == ZDO_SRV_OK);
    CHECK(info.status == 0x80 && info.length == 4 && info.consumed == 3 && info.cluster_id == 0x8002);
    CHECK(memcmp(output + 1, "\x5a\x80\x35\x12", 4) == 0 && filled(output + 5, 14, 0xc7));
    CHECK(zdo_node_rsp_decode(output + 1, 4, &decoded) == ZDO_NODE_OK);
    CHECK(decoded.address == 0x1235 && decoded.has_address == 1
          && filled(&decoded.descriptor, sizeof(decoded.descriptor), 0));
    for (n = 0; n < 256; n++) {
        reset(); rx.header.cluster_id = 0x1234;
        rx.header.source_endpoint = n == 255 ? 0 : (uint8_t)n;
        rx.header.counter = (uint8_t)(n + 1u);
        input[0] = (uint8_t)n;
        CHECK(zdo_srv_handle(&local, &rx, input, 100, output + 1, 2, &info) == ZDO_SRV_OK);
        CHECK(info.kind == ZDO_SRV_REPLY && info.cluster_id == 0x9234 && info.length == 2
              && info.consumed == 1 && info.endpoint == rx.header.source_endpoint);
        CHECK(info.sequence == n && info.status == 0x84 && output[1] == n && output[2] == 0x84);
        CHECK(output[0] == 0xc7 && filled(output + 3, 16, 0xc7));
    }
    reset(); rx.header.cluster_id = 0x1234;
    CHECK(unchanged(ZDO_SRV_SPACE, 1, 0) == 0);
    CHECK(unchanged(ZDO_SRV_SPACE, 1, 1) == 0);
    CHECK(unchanged(ZDO_SRV_TRUNCATED, 0, 17) == 0);
    return 0;
}

static uint16_t no_reply_cases(void)
{
    uint8_t kind, mode;
    for (kind = 0; kind < 3; kind++) {
        for (mode = 0; mode < 4; mode++) {
            reset();
            rx.header.cluster_id = kind == 0 ? 2 : kind == 1 ? 0x1234 : ZDO_SRV_PARENT_ANNCE;
            rx.broadcast = mode & 1u;
            rx.header.delivery_mode = (mode & 2u) ? APS_DELIVERY_BROADCAST : APS_DELIVERY_UNICAST;
            if (!mode && kind != 2) continue;
            CHECK(zdo_srv_handle(&local, &rx, input, 1, output, 0, &info) == ZDO_SRV_OK);
            CHECK(info.kind == (kind == 2 ? ZDO_SRV_DROP_PARENT : ZDO_SRV_DROP_BROADCAST));
            CHECK(info.length == 0 && info.cluster_id == 0 && info.endpoint == 0 && info.status == 0);
            CHECK(info.sequence == 0x5a && info.consumed == 1 && filled(output, sizeof(output), 0xc7));
        }
    }
    for (kind = 0; kind < 3; kind++) {
        for (mode = 0; mode < 4; mode++) {
            reset();
            rx.header.cluster_id = kind == 0 ? 0x8002 : kind == 1 ? ZDO_SRV_DEVICE_ANNCE : ZDO_SRV_UPDATE_NOTIFY;
            rx.broadcast = mode & 1u;
            rx.header.delivery_mode = (mode & 2u) ? APS_DELIVERY_BROADCAST : APS_DELIVERY_UNICAST;
            CHECK(unchanged(kind == 0 ? ZDO_SRV_NOT_REQUEST : ZDO_SRV_NOTIFICATION, 1, 17) == 0);
        }
    }
    return 0;
}

static uint16_t invalid_cases(void)
{
    uint8_t n;
    for (n = 0; n < 12; n++) {
        reset();
        switch (n) {
        case 0: rx.broadcast = 2; break;
        case 1: rx.header.type = 1; break;
        case 2: rx.header.profile_id = 0x104; break;
        case 3: rx.header.destination_endpoint = 1; break;
        case 4: rx.header.destination_endpoint = 255; break;
        case 5: rx.header.source_endpoint = 255; break;
        case 6: rx.header.flags = APS_FLAG_SECURITY; break;
        case 7: rx.header.flags = APS_FLAG_EXTENDED_HEADER; break;
        case 8: rx.header.flags = APS_FLAG_ACK_FORMAT; break;
        case 9: rx.header.delivery_mode = APS_DELIVERY_GROUP; break;
        case 10: rx.header.delivery_mode = 1; break;
        default: rx.header.delivery_mode = APS_DELIVERY_BROADCAST; rx.header.flags = APS_FLAG_ACK_REQUEST; break;
        }
        CHECK(unchanged(ZDO_SRV_CONTEXT, 3, 17) == 0);
    }
    for (n = 0; n < 7; n++) {
        reset();
        switch (n) {
        case 0: local.address = 0; break;
        case 1: local.address = 0xfff8; break;
        case 2: local.address = 0xffff; break;
        case 3: local.descriptor.logical_type = 0; break;
        case 4: local.descriptor.logical_type = 1; break;
        case 5: local.descriptor.max_buffer = 128; break;
        default: local.descriptor.stack_revision = 128; break;
        }
        CHECK(unchanged(ZDO_SRV_LOCAL, 3, 17) == 0);
    }
    reset();
    CHECK(zdo_srv_handle(NULL, &rx, input, 3, output, 17, &info) == ZDO_SRV_ARGUMENT);
    CHECK(zdo_srv_handle(&local, NULL, input, 3, output, 17, &info) == ZDO_SRV_ARGUMENT);
    CHECK(zdo_srv_handle(&local, &rx, NULL, 3, output, 17, &info) == ZDO_SRV_ARGUMENT);
    CHECK(zdo_srv_handle(&local, &rx, input, 3, NULL, 17, &info) == ZDO_SRV_ARGUMENT);
    CHECK(zdo_srv_handle(&local, &rx, input, 3, output, 17, NULL) == ZDO_SRV_ARGUMENT);
    CHECK(filled(output, sizeof(output), 0xc7) && filled(&info, sizeof(info), 0xa5));
    return 0;
}

static uint16_t chain_cases(void)
{
    uint8_t layout, kind, i;
    for (layout = 0; layout < 4; layout++) {
        for (kind = 0; kind < 3; kind++) {
            reset();
            if (kind == 1) input[1] = 0x35;
            if (kind == 2) rx.header.cluster_id = 0x1234;
            CHECK(aps_frame_encode(&rx.header, input, 3, apdu, sizeof(apdu), &aps_length) == APS_CODEC_OK);
            memset(&nwk, 0, sizeof(nwk));
            nwk.version = 2; nwk.destination = 0x1234; nwk.source = 0x5678;
            nwk.radius = 30; nwk.sequence = 0x21;
            nwk.flags = (layout & 1u ? NWK_FLAG_DESTINATION_IEEE : 0)
                | (layout & 2u ? NWK_FLAG_SOURCE_IEEE : 0);
            for (i = 0; i < 8; i++) { nwk.destination_ieee[i] = i; nwk.source_ieee[i] = i + 8u; }
            CHECK(nwk_frame_encode(&nwk, apdu, aps_length, npdu, sizeof(npdu), &nwk_length) == NWK_CODEC_OK);
            CHECK(nwk_frame_decode(npdu, nwk_length, &network) == NWK_CODEC_OK);
            CHECK(network.header.destination == local.address && network.header.source == 0x5678);
            CHECK(aps_frame_decode(npdu + network.payload_offset, network.payload_length, &transport) == APS_CODEC_OK);
            rx.header = transport.header;
            CHECK(zdo_srv_handle(&local, &rx, npdu + network.payload_offset + transport.payload_offset,
                                transport.payload_length, output, sizeof(output), &info) == ZDO_SRV_OK);
            CHECK(info.kind == ZDO_SRV_REPLY && info.endpoint == 7 && info.sequence == 0x5a);
            memset(&aps, 0, sizeof(aps));
            aps.destination_endpoint = info.endpoint; aps.cluster_id = info.cluster_id; aps.counter = 0xaa;
            CHECK(aps_frame_encode(&aps, output, info.length, apdu, sizeof(apdu), &aps_length) == APS_CODEC_OK);
            nwk.destination = network.header.source; nwk.source = local.address;
            nwk.sequence = 0x22; nwk.flags = NWK_FLAG_END_DEVICE_INITIATOR;
            CHECK(nwk_frame_encode(&nwk, apdu, aps_length, npdu, sizeof(npdu), &nwk_length) == NWK_CODEC_OK);
            if (!kind) CHECK(nwk_length == sizeof(golden) && memcmp(npdu, golden, sizeof(golden)) == 0);
            CHECK(nwk_frame_decode(npdu, nwk_length, &network) == NWK_CODEC_OK);
            CHECK(network.header.source == 0x1234 && network.header.destination == 0x5678 && network.header.sequence == 0x22);
            CHECK(aps_frame_decode(npdu + network.payload_offset, network.payload_length, &transport) == APS_CODEC_OK);
            CHECK(transport.header.source_endpoint == 0 && transport.header.destination_endpoint == 7
                  && transport.header.counter == 0xaa && transport.header.cluster_id == info.cluster_id);
            CHECK(zdo_node_rsp_decode(npdu + network.payload_offset + transport.payload_offset,
                                      transport.payload_length, &decoded) == ZDO_NODE_OK);
            CHECK(decoded.sequence == 0x5a && decoded.status == (kind == 0 ? 0 : kind == 1 ? 0x80 : 0x84));
        }
    }
    npdu[1] |= 2;
    CHECK(nwk_frame_decode(npdu, nwk_length, &network) == NWK_CODEC_UNSUPPORTED_SECURITY);
    apdu[0] |= APS_FLAG_SECURITY;
    CHECK(aps_frame_decode(apdu, aps_length, &transport) == APS_CODEC_UNSUPPORTED_SECURITY);
    apdu[0] = APS_DELIVERY_BROADCAST << 2;
    CHECK(aps_frame_decode(apdu, aps_length, &transport) == APS_CODEC_UNSUPPORTED_DELIVERY);
    return 0;
}

static uint16_t run_tests(void)
{
    uint16_t failure;
    failure = reply_cases(); if (failure) return failure;
    failure = no_reply_cases(); if (failure) return failure;
    failure = invalid_cases(); if (failure) return failure;
    return chain_cases();
}

#if defined(__SDCC)
volatile MCU_XDATA MCU_AT(0x1e00) uint8_t zdo_srv_result[8];
void main(void)
{
    uint16_t failure = run_tests();
    memcpy(zdo_srv_result, "ZDS1", 4);
    zdo_srv_result[4] = 1; zdo_srv_result[5] = 8;
    zdo_srv_result[6] = (uint8_t)failure; zdo_srv_result[7] = (uint8_t)(failure >> 8);
    __asm
        .globl _zdo_srv_done
    _zdo_srv_done:
        nop
    __endasm;
    for (;;) {}
}
#else
#include <stdio.h>
#include <stdlib.h>

static uint16_t exact_cases(void)
{
    uint32_t cluster;
    uint16_t size, cap, n;
    uint8_t mode, needed;
    uint8_t *bytes, *reply;
    zdo_srv_result_t status, expected_result;
    zdo_srv_info_t *out = malloc(sizeof(*out));
    zdo_srv_local_t *own = malloc(sizeof(*own));
    zdo_srv_rx_t *context = malloc(sizeof(*context));
    CHECK(out != NULL && own != NULL && context != NULL);
    reset(); *own = local; *context = rx;
    for (cluster = 0; cluster < 65536UL; cluster++) {
        for (mode = 0; mode < 2; mode++) {
            context->header.cluster_id = (uint16_t)cluster;
            context->broadcast = mode;
            memset(output, 0xc7, sizeof(output)); memset(out, 0xa5, sizeof(*out));
            status = zdo_srv_handle(own, context, query, 3, output + 1, 17, out);
            expected_result = cluster & 0x8000u ? ZDO_SRV_NOT_REQUEST
                : cluster == 0x13 || cluster == 0x3b ? ZDO_SRV_NOTIFICATION : ZDO_SRV_OK;
            CHECK(status == expected_result);
            if (status != ZDO_SRV_OK) {
                CHECK(filled(out, sizeof(*out), 0xa5) && filled(output, sizeof(output), 0xc7));
            } else if (mode || cluster == 0x1f) {
                CHECK(out->kind == (cluster == 0x1f ? ZDO_SRV_DROP_PARENT : ZDO_SRV_DROP_BROADCAST));
                CHECK(out->length == 0 && filled(output, sizeof(output), 0xc7));
            } else {
                CHECK(out->kind == ZDO_SRV_REPLY && out->cluster_id == (cluster | 0x8000u));
                CHECK(out->sequence == 0x5a && out->status == (cluster == 2 ? 0 : 0x84));
                CHECK(out->length == (cluster == 2 ? 17 : 2));
                CHECK(output[0] == 0xc7 && filled(output + 1 + out->length, 18u - out->length, 0xc7));
            }
        }
    }
    *context = rx;
    for (size = 0; size <= 101; size++) {
        bytes = malloc(size ? size : 1);
        CHECK(bytes != NULL);
        memset(bytes, 0xff, size ? size : 1); memcpy(bytes, query, size < 3 ? size : 3);
        for (mode = 0; mode < 3; mode++) {
            context->header.cluster_id = mode == 2 ? 0x1234 : 2;
            if (size >= 2) bytes[1] = mode == 1 ? 0x35 : 0x34;
            needed = mode == 0 ? 17 : mode == 1 ? 4 : 2;
            for (cap = 0; cap <= 18; cap++) {
                reply = malloc(cap ? cap : 1);
                CHECK(reply != NULL);
                memset(reply, 0xc7, cap ? cap : 1); memset(out, 0xa5, sizeof(*out));
                expected_result = size > 100 ? ZDO_SRV_TOO_LONG
                    : size < (mode == 2 ? 1u : 3u) ? ZDO_SRV_TRUNCATED
                    : cap < needed ? ZDO_SRV_SPACE : ZDO_SRV_OK;
                CHECK(zdo_srv_handle(own, context, bytes, size, reply, cap, out) == expected_result);
                if (expected_result != ZDO_SRV_OK) {
                    CHECK(filled(out, sizeof(*out), 0xa5) && filled(reply, cap ? cap : 1, 0xc7));
                } else {
                    CHECK(out->length == needed && reply[0] == 0x5a
                          && reply[1] == (mode == 0 ? 0 : mode == 1 ? 0x80 : 0x84));
                    CHECK(filled(reply + needed, cap - needed, 0xc7));
                }
                free(reply);
            }
        }
        free(bytes);
    }
    for (cluster = 0; cluster < 65536UL; cluster++) {
        *context = rx;
        context->header.profile_id = (uint16_t)cluster;
        memset(out, 0xa5, sizeof(*out)); memset(output, 0xc7, sizeof(output));
        CHECK(zdo_srv_handle(own, context, query, 3, output, 17, out)
              == (cluster ? ZDO_SRV_CONTEXT : ZDO_SRV_OK));
        if (cluster) CHECK(filled(out, sizeof(*out), 0xa5) && filled(output, sizeof(output), 0xc7));
    }
    for (n = 0; n < 256; n++) {
        for (mode = 0; mode < 6; mode++) {
            *context = rx;
            switch (mode) {
            case 0: context->header.flags = (uint8_t)n; expected_result = n == 0 || n == 0x40 ? ZDO_SRV_OK : ZDO_SRV_CONTEXT; break;
            case 1: context->header.type = (uint8_t)n; expected_result = n == 0 ? ZDO_SRV_OK : ZDO_SRV_CONTEXT; break;
            case 2: context->header.delivery_mode = (uint8_t)n; expected_result = n == 0 || n == 2 ? ZDO_SRV_OK : ZDO_SRV_CONTEXT; break;
            case 3: context->header.destination_endpoint = (uint8_t)n; expected_result = n == 0 ? ZDO_SRV_OK : ZDO_SRV_CONTEXT; break;
            case 4: context->header.source_endpoint = (uint8_t)n; expected_result = n != 255 ? ZDO_SRV_OK : ZDO_SRV_CONTEXT; break;
            default: context->broadcast = (uint8_t)n; expected_result = n < 2 ? ZDO_SRV_OK : ZDO_SRV_CONTEXT; break;
            }
            memset(out, 0xa5, sizeof(*out)); memset(output, 0xc7, sizeof(output));
            CHECK(zdo_srv_handle(own, context, query, 3, output, 17, out) == expected_result);
            if (expected_result != ZDO_SRV_OK) CHECK(filled(out, sizeof(*out), 0xa5) && filled(output, sizeof(output), 0xc7));
        }
    }
    *context = rx;
    for (cluster = 0; cluster < 65536UL; cluster++) {
        uint8_t request[3] = {(uint8_t)cluster, (uint8_t)cluster, (uint8_t)(cluster >> 8)};
        memset(out, 0xa5, sizeof(*out)); memset(output, 0xc7, sizeof(output));
        CHECK(zdo_srv_handle(own, context, request, 3, output, 17, out) == ZDO_SRV_OK);
        CHECK(out->status == (cluster == own->address ? 0 : 0x80));
        CHECK(out->length == (cluster == own->address ? 17 : 4));
        CHECK(output[0] == (uint8_t)cluster && output[2] == (uint8_t)cluster && output[3] == (uint8_t)(cluster >> 8));
        own->address = (uint16_t)cluster;
        memset(out, 0xa5, sizeof(*out)); memset(output, 0xc7, sizeof(output));
        CHECK(zdo_srv_handle(own, context, request, 3, output, 17, out) ==
              (cluster == 0 || cluster >= 0xfff8u ? ZDO_SRV_LOCAL : ZDO_SRV_OK));
        if (cluster == 0 || cluster >= 0xfff8u)
            CHECK(filled(out, sizeof(*out), 0xa5) && filled(output, sizeof(output), 0xc7));
        else CHECK(out->status == 0 && out->length == 17);
        *own = local;
    }
    reply = malloc(65535);
    bytes = malloc(65535);
    CHECK(reply != NULL && bytes != NULL);
    memset(reply, 0xc7, 65535); memset(bytes, 0xff, 65535); memcpy(bytes, query, 3);
    CHECK(zdo_srv_handle(own, context, bytes, 3, reply, 65535, out) == ZDO_SRV_OK);
    CHECK(out->length == 17 && memcmp(reply, expected, 17) == 0 && filled(reply + 17, 65518, 0xc7));
    memset(out, 0xa5, sizeof(*out)); memset(reply, 0xc7, 65535);
    CHECK(zdo_srv_handle(own, context, bytes, 65535, reply, 65535, out) == ZDO_SRV_TOO_LONG);
    CHECK(filled(out, sizeof(*out), 0xa5) && filled(reply, 65535, 0xc7));
    context->broadcast = 1;
    CHECK(zdo_srv_handle(own, context, query, 3, reply, 0, out) == ZDO_SRV_OK);
    CHECK(out->kind == ZDO_SRV_DROP_BROADCAST && filled(reply, 65535, 0xc7));
    free(reply); free(bytes);
    free(context); free(own); free(out);
    return 0;
}

int main(void)
{
    uint16_t failure = run_tests();
    uint32_t shared = zdo_srv_checks;
    if (!failure) failure = exact_cases();
    if (failure) {
        fprintf(stderr, "ZDO ED dispatch failed at line %u after %lu checks\n", failure, (unsigned long)zdo_srv_checks);
        return 1;
    }
    printf("ZDO ED dispatch: %lu shared / %lu total checks PASS; offline policy, not admission.\n",
           (unsigned long)shared, (unsigned long)zdo_srv_checks);
    return 0;
}
#endif
