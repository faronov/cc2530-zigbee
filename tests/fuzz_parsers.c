/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
/* Host fuzzer for the over-the-air parsers (MAC frame/command/beacon, NWK
 * beacon payload, NWK, APS, ED wire syntax and security auxiliary header,
 * ZCL frame and values, ZDO node and server). The first input byte selects
 * the parser; the rest is the frame. Beyond crashes and sanitizer reports,
 * every successful decode must describe bytes inside its input.
 *
 * With -DFUZZ_LIBFUZZER and -fsanitize=fuzzer this is a libFuzzer target;
 * otherwise main() replays the hex corpus files named on the command line. */
#include "aps_frame.h"
#include "ed_wire.h"
#include "mac_frame.h"
#include "nwk_beacon.h"
#include "nwk_frame.h"
#include "security_aes_model.h"
#include "zcl_wire.h"
#include "zdo_node.h"
#include "zdo_srv.h"
#include "zigbee_security.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NEED(c) do { if (!(c)) { fprintf(stderr, "invariant failed: %s (line %d)\n", #c, __LINE__); abort(); } } while (0)
#define TARGETS 9u

static uint8_t frame[256], out[256];

static void mac(const uint8_t *body, uint16_t n, uint8_t profile)
{
    mac_frame_info_t info;
    mac_command_t command;
    mac_beacon_info_t beacon;
    nwk_beacon_t upper;
    if (mac_frame_decode_profile(body, n, &info, profile & 1u) != MAC_CODEC_OK) return;
    NEED((uint16_t)info.payload_offset + info.payload_length <= n);
    if (info.header.type == MAC_FRAME_COMMAND)
        (void)mac_command_decode(body + info.payload_offset, info.payload_length, &command);
    if (info.header.type != MAC_FRAME_BEACON ||
        mac_beacon_decode(body + info.payload_offset, info.payload_length, &beacon) != MAC_CODEC_OK) return;
    NEED((uint16_t)beacon.payload_offset + beacon.payload_length <= info.payload_length);
    (void)nwk_beacon_decode(body + info.payload_offset + beacon.payload_offset, beacon.payload_length, &upper);
}

static void nwk(const uint8_t *body, uint16_t n)
{
    nwk_frame_info_t info;
    aps_frame_info_t aps;
    if (nwk_frame_decode(body, n, &info) != NWK_CODEC_OK) return;
    NEED((uint16_t)info.payload_offset + info.payload_length <= n);
    if (aps_frame_decode(body + info.payload_offset, info.payload_length, &aps) == APS_CODEC_OK)
        NEED((uint16_t)aps.payload_offset + aps.payload_length <= info.payload_length);
}

static void aps(const uint8_t *body, uint16_t n)
{
    aps_frame_info_t info;
    zcl_frame_info_t zcl;
    if (aps_frame_decode(body, n, &info) != APS_CODEC_OK) return;
    NEED((uint16_t)info.payload_offset + info.payload_length <= n);
    if (zcl_frame_decode(body + info.payload_offset, info.payload_length, &zcl) == ZCL_CODEC_OK)
        NEED((uint16_t)zcl.payload_offset + zcl.payload_length <= info.payload_length);
}

static void wire(const uint8_t *body, uint16_t n, uint8_t layer)
{
    ed_packet_t packet;
    zigbee_security_meta_t meta;
    if (ed_wire_decode(body, n, &packet) == ZIGBEE_SECURITY_OK) NEED(packet.length <= ED_PAYLOAD_MAX);
    layer &= 1u;
    if (ed_wire_inspect(layer, body, n, &meta) == ZIGBEE_SECURITY_OK)
        NEED((uint16_t)meta.header_length + meta.auxiliary_length + meta.payload_length <= n);
    if (zigbee_security_inspect(layer, 5, body, n, &meta) == ZIGBEE_SECURITY_OK)
        NEED((uint16_t)meta.header_length + meta.auxiliary_length + meta.payload_length <= n);
}

static void zcl(const uint8_t *body, uint16_t n, uint8_t type)
{
    zcl_frame_info_t info;
    zcl_value_info_t value;
    if (zcl_frame_decode(body, n, &info) == ZCL_CODEC_OK)
        NEED((uint16_t)info.payload_offset + info.payload_length <= n);
    if (zcl_value_decode(type, body, n, &value) == ZCL_CODEC_OK) {
        NEED(value.encoded_length <= n);
        NEED((uint16_t)value.data_offset + value.data_length <= value.encoded_length);
    }
}

static void zdo(const uint8_t *body, uint16_t n, uint8_t which)
{
    static const zdo_srv_local_t local = {
        {0x5678, 0x1234, 0x2345, 2, 0, 8, 0x8c, 0x52, 0, 22, 0}, 0x1234, 0x0104, 1
    };
    zdo_node_request_t request;
    zdo_node_response_t response;
    zdo_srv_rx_t rx;
    zdo_srv_info_t info;
    uint16_t capacity;
    (void)zdo_node_req_decode(body, n, &request);
    (void)zdo_node_rsp_decode(body, n, &response);
    if (n < 3u) return;
    /* Cluster, source endpoint and the lower-layer broadcast flag come from
     * the input; the APS header is otherwise a plain unicast ZDO request. */
    memset(&rx, 0, sizeof(rx));
    rx.header.cluster_id = (uint16_t)(body[0] | (uint16_t)(body[1] & 0x80u ? 0x8000u : 0u) | (uint16_t)(body[1] & 0x3fu) << 8);
    rx.header.source_endpoint = body[2];
    rx.broadcast = which & 1u;
    capacity = (uint16_t)((which >> 1) % sizeof(out));
    memset(out, 0xc7, sizeof(out));
    if (zdo_srv_handle(&local, &rx, body + 3, (uint16_t)(n - 3u), out, capacity, &info) == ZDO_SRV_OK) {
        NEED(info.length <= capacity);
        NEED(info.consumed <= n - 3u);
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    uint16_t n;
    uint8_t selector, extra;
    if (size < 2u) return 0;
    selector = data[0];
    n = (uint16_t)(size - 1u > sizeof(frame) ? sizeof(frame) : size - 1u);
    /* A private copy, so a decoder that reads past n hits the sanitizer. */
    memcpy(frame, data + 1, n);
    extra = (uint8_t)(selector / TARGETS);
    switch (selector % TARGETS) {
    case 0: mac(frame, n, extra); break;
    case 1: (void)nwk_beacon_decode(frame, n, &(nwk_beacon_t){0}); break;
    case 2: nwk(frame, n); break;
    case 3: aps(frame, n); break;
    case 4: security_aes_reset(); wire(frame, n, extra); break;
    case 5: zcl(frame, n, data[0]); break;
    case 6: zcl(frame, n, extra); break;
    case 7: zdo(frame, n, extra); break;
    default: {
        mac_command_t command;
        (void)mac_command_decode(frame, n, &command);
        break;
    }
    }
    return 0;
}

#if !defined(FUZZ_LIBFUZZER)
/* Replays hex corpus files: one input per line, '#' lines are comments. */
int main(int argc, char **argv)
{
    static char line[2 * 4096 + 8];
    static uint8_t buffer[4096];
    int i, runs = 0;
    for (i = 1; i < argc; i++) {
        FILE *f = fopen(argv[i], "r");
        if (!f) { perror(argv[i]); return 2; }
        while (fgets(line, sizeof(line), f)) {
            size_t size = 0;
            const char *p = line;
            unsigned value;
            if (*p == '#' || *p == '\n') continue;
            while (size < sizeof(buffer) && sscanf(p, "%2x", &value) == 1) {
                buffer[size++] = (uint8_t)value;
                p += 2;
            }
            LLVMFuzzerTestOneInput(buffer, size);
            runs++;
        }
        fclose(f);
    }
    printf("parser fuzz replay: %d inputs PASS\n", runs);
    return runs ? 0 : 1;
}
#endif
