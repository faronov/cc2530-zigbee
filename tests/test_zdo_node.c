/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 * Original synthetic descriptors; not local capability advertisements.
 */
#include "zdo_node.h"
#include "aps_frame.h"
#include "cc2530_mmio.h"
#include <stddef.h>
#include <string.h>

MCU_XDATA uint32_t zdo_node_checks;
#define CHECK(c) do { zdo_node_checks++; if (!(c)) return (uint16_t)__LINE__; } while (0)

static const MCU_CODE uint8_t query[] = {0x5a, 0x34, 0x12};
static const MCU_CODE uint8_t answer[] = {
    0x5a, 0, 0x34, 0x12, 0x10, 0x40, 0x8e, 0x78, 0x56,
    0x52, 0x34, 0x12, 0x01, 0x2c, 0x45, 0x23, 0x03
};
static zdo_node_request_t request, old_request;
static zdo_node_response_t response, old_response;
static uint8_t input[101], output[102], length;
static aps_header_t header;
static aps_frame_info_t transport;
static uint8_t apdu[108], aps_length;

static uint8_t filled(const void *p, uint16_t size, uint8_t value)
{
    const uint8_t *b = p;
    while (size--)
        if (*b++ != value)
            return 0;
    return 1;
}

static uint8_t wire_valid(const uint8_t *p)
{
    return (p[4] & 7u) < 3 && !(p[4] & 0xe0u) && !(p[5] & 0x17u)
        && !(p[6] & 0x30u) && p[9] < 128 && p[11] < 128
        && !(p[12] & 0x80u) && !(p[13] & 1u)
        && p[15] < 128 && p[16] < 4;
}

static uint16_t reject_rsp(uint16_t size, zdo_node_result_t expected)
{
    memset(&response, 0xa5, sizeof(response));
    CHECK(zdo_node_rsp_decode(input, size, &response) == expected);
    CHECK(filled(&response, sizeof(response), 0xa5));
    return 0;
}

static uint16_t golden_cases(void)
{
    uint16_t n;
    CHECK(zdo_node_req_decode(query, sizeof(query), &request) == ZDO_NODE_OK);
    CHECK(request.sequence == 0x5a && request.address == 0x1234 && request.consumed == 3);
    CHECK(zdo_node_req_encode(&request, output, sizeof(output), &length) == ZDO_NODE_OK);
    CHECK(length == 3 && memcmp(query, output, 3) == 0);
    CHECK(zdo_node_rsp_decode(answer, sizeof(answer), &response) == ZDO_NODE_OK);
    CHECK(response.sequence == 0x5a && response.status == 0 && response.address == 0x1234);
    CHECK(response.has_address == 1 && response.consumed == 17);
    CHECK(response.descriptor.logical_type == 0 && response.descriptor.available == 2);
    CHECK(response.descriptor.frequency_band == 8 && response.descriptor.mac_capability == 0x8e);
    CHECK(response.descriptor.manufacturer == 0x5678 && response.descriptor.max_buffer == 0x52);
    CHECK(response.descriptor.max_incoming == 0x1234 && response.descriptor.max_outgoing == 0x2345);
    CHECK(response.descriptor.server_flags == 1 && response.descriptor.stack_revision == 22);
    CHECK(response.descriptor.descriptor_capability == 3);
    old_response = response;
    for (n = 0; n <= 18; n++) {
        memset(output, 0xc7, sizeof(output)); length = 0xa5;
        CHECK(zdo_node_rsp_encode(&response, output + 1, n, &length)
              == (n < 17 ? ZDO_NODE_SPACE : ZDO_NODE_OK));
        if (n < 17)
            CHECK(length == 0xa5 && filled(output, sizeof(output), 0xc7));
        else {
            CHECK(length == 17 && memcmp(output + 1, answer, 17) == 0);
            CHECK(output[0] == 0xc7 && filled(output + 18, sizeof(output) - 18, 0xc7));
        }
        CHECK(memcmp(&response, &old_response, sizeof(response)) == 0);
    }
    memcpy(input, answer, sizeof(answer));
    for (n = 0; n < 17; n++)
        CHECK(reject_rsp(n, ZDO_NODE_TRUNCATED) == 0);
    CHECK(reject_rsp(101, ZDO_NODE_TOO_LONG) == 0);
    CHECK(reject_rsp(65535, ZDO_NODE_TOO_LONG) == 0);
    memset(input, 0xff, sizeof(input)); memcpy(input, answer, sizeof(answer));
    CHECK(zdo_node_rsp_decode(input, 100, &response) == ZDO_NODE_OK);
    CHECK(response.consumed == 17 && memcmp(&response, &old_response, sizeof(response)) == 0);
    memcpy(input, query, sizeof(query));
    CHECK(zdo_node_req_decode(input, 100, &request) == ZDO_NODE_OK && request.consumed == 3);
    old_request = request;
    for (n = 0; n < 3; n++) {
        CHECK(zdo_node_req_decode(input, n, &request) == ZDO_NODE_TRUNCATED);
        CHECK(memcmp(&request, &old_request, sizeof(request)) == 0);
    }
    CHECK(zdo_node_req_decode(input, 101, &request) == ZDO_NODE_TOO_LONG);
    CHECK(zdo_node_req_decode(input, 65535, &request) == ZDO_NODE_TOO_LONG);
    CHECK(memcmp(&request, &old_request, sizeof(request)) == 0);
    return 0;
}

static uint16_t status_cases(void)
{
    uint16_t s, cap;
    uint8_t needed;
    for (s = 0; s < 256; s++) {
        memcpy(input, answer, sizeof(answer)); input[1] = (uint8_t)s;
        needed = s == 0 ? 17 : s == 0x84 ? 2 : s == 0x80 || s == 0x81 || s == 0x89 ? 4 : 0;
        if (!needed) {
            CHECK(reject_rsp(17, ZDO_NODE_UNSUPPORTED_STATUS) == 0);
            response.status = (uint8_t)s;
            memset(output, 0xc7, sizeof(output)); length = 0xa5;
            CHECK(zdo_node_rsp_encode(&response, output, sizeof(output), &length) == ZDO_NODE_UNSUPPORTED_STATUS);
            CHECK(length == 0xa5 && filled(output, sizeof(output), 0xc7));
            continue;
        }
        CHECK(zdo_node_rsp_decode(input, needed, &response) == ZDO_NODE_OK);
        CHECK(response.status == s && response.consumed == needed && response.sequence == 0x5a);
        CHECK(response.has_address == (needed >= 4) && response.address == (needed >= 4 ? 0x1234 : 0));
        if (s)
            CHECK(filled(&response.descriptor, sizeof(response.descriptor), 0));
        CHECK(zdo_node_rsp_decode(input, 17, &response) == ZDO_NODE_OK && response.consumed == needed);
        CHECK(reject_rsp((uint16_t)(needed - 1u), ZDO_NODE_TRUNCATED) == 0);
        CHECK(zdo_node_rsp_decode(input, needed, &response) == ZDO_NODE_OK);
        response.has_address = response.consumed = 255;
        if (s) memset(&response.descriptor, 0xff, sizeof(response.descriptor));
        for (cap = 0; cap <= (uint16_t)(needed + 1u); cap++) {
            memset(output, 0xc7, sizeof(output)); length = 0xa5;
            CHECK(zdo_node_rsp_encode(&response, output + 1, cap, &length)
                  == (cap < needed ? ZDO_NODE_SPACE : ZDO_NODE_OK));
            if (cap < needed)
                CHECK(length == 0xa5 && filled(output, sizeof(output), 0xc7));
            else {
                CHECK(length == needed && memcmp(output + 1, input, needed) == 0);
                CHECK(output[0] == 0xc7 && filled(output + needed + 1u, (uint16_t)(101u - needed), 0xc7));
            }
        }
    }
    return 0;
}

static uint16_t descriptor_cases(void)
{
    uint16_t offset, bit;
    for (offset = 0; offset < 128; offset++) {
        CHECK(zdo_node_rsp_decode(answer, sizeof(answer), &response) == ZDO_NODE_OK);
        response.sequence = (uint8_t)(offset + 128u);
        response.descriptor.stack_revision = (uint8_t)offset;
        response.descriptor.server_flags = (uint8_t)(127u - offset);
        response.descriptor.max_buffer = offset ? 127 : 0;
        response.descriptor.max_incoming = offset ? 0x7fff : 0;
        response.descriptor.max_outgoing = offset ? 0 : 0x7fff;
        CHECK(zdo_node_rsp_encode(&response, output, sizeof(output), &length) == ZDO_NODE_OK);
        CHECK(output[0] == offset + 128u && output[12] == 127u - offset && output[13] == offset * 2u);
        CHECK(zdo_node_rsp_decode(output, length, &old_response) == ZDO_NODE_OK);
        CHECK(memcmp(&response, &old_response, sizeof(response)) == 0);
    }
    for (offset = 4; offset < 17; offset++) {
        for (bit = 0; bit < 8; bit++) {
            memcpy(input, answer, sizeof(answer));
            input[offset] ^= (uint8_t)(1u << bit);
            if (!wire_valid(input)) {
                CHECK(reject_rsp(17, ZDO_NODE_INVALID_DESCRIPTOR) == 0);
            } else {
                CHECK(zdo_node_rsp_decode(input, 17, &response) == ZDO_NODE_OK);
                CHECK(zdo_node_rsp_encode(&response, output, sizeof(output), &length) == ZDO_NODE_OK);
                CHECK(length == 17 && memcmp(input, output, 17) == 0);
            }
        }
    }
    for (offset = 0; offset < 10; offset++) {
        CHECK(zdo_node_rsp_decode(answer, sizeof(answer), &response) == ZDO_NODE_OK);
        switch (offset) {
        case 0: response.descriptor.logical_type = 3; break;
        case 1: response.descriptor.available = 4; break;
        case 2: response.descriptor.frequency_band = 2; break;
        case 3: response.descriptor.mac_capability = 0x10; break;
        case 4: response.descriptor.max_buffer = 128; break;
        case 5: response.descriptor.max_incoming = 0x8000; break;
        case 6: response.descriptor.max_outgoing = 0x8000; break;
        case 7: response.descriptor.server_flags = 128; break;
        case 8: response.descriptor.stack_revision = 128; break;
        default: response.descriptor.descriptor_capability = 4; break;
        }
        old_response = response;
        memset(output, 0xc7, sizeof(output)); length = 0xa5;
        CHECK(zdo_node_rsp_encode(&response, output, sizeof(output), &length) == ZDO_NODE_INVALID_DESCRIPTOR);
        CHECK(length == 0xa5 && filled(output, sizeof(output), 0xc7));
        CHECK(memcmp(&response, &old_response, sizeof(response)) == 0);
    }
    return 0;
}

static uint16_t null_cases(void)
{
    CHECK(zdo_node_rsp_decode(answer, 17, &response) == ZDO_NODE_OK);
    old_response = response;
    CHECK(zdo_node_req_decode(query, 3, &request) == ZDO_NODE_OK);
    old_request = request;
    memset(output, 0xc7, sizeof(output)); length = 0xa5;
    CHECK(zdo_node_req_decode(NULL, 3, &request) == ZDO_NODE_INVALID_ARGUMENT);
    CHECK(zdo_node_req_decode(query, 3, NULL) == ZDO_NODE_INVALID_ARGUMENT);
    CHECK(zdo_node_rsp_decode(NULL, 17, &response) == ZDO_NODE_INVALID_ARGUMENT);
    CHECK(zdo_node_rsp_decode(answer, 17, NULL) == ZDO_NODE_INVALID_ARGUMENT);
    CHECK(zdo_node_req_encode(NULL, output, sizeof(output), &length) == ZDO_NODE_INVALID_ARGUMENT);
    CHECK(zdo_node_req_encode(&request, NULL, sizeof(output), &length) == ZDO_NODE_INVALID_ARGUMENT);
    CHECK(zdo_node_req_encode(&request, output, sizeof(output), NULL) == ZDO_NODE_INVALID_ARGUMENT);
    CHECK(zdo_node_rsp_encode(NULL, output, sizeof(output), &length) == ZDO_NODE_INVALID_ARGUMENT);
    CHECK(zdo_node_rsp_encode(&response, NULL, sizeof(output), &length) == ZDO_NODE_INVALID_ARGUMENT);
    CHECK(zdo_node_rsp_encode(&response, output, sizeof(output), NULL) == ZDO_NODE_INVALID_ARGUMENT);
    CHECK(length == 0xa5 && filled(output, sizeof(output), 0xc7));
    CHECK(memcmp(&request, &old_request, sizeof(request)) == 0);
    CHECK(memcmp(&response, &old_response, sizeof(response)) == 0);
    return 0;
}

static uint16_t aps_cases(void)
{
    uint8_t n;
    memset(&header, 0, sizeof(header));
    header.counter = 0xa9;
    for (n = 0; n < 2; n++) {
        header.cluster_id = n ? ZDO_NODE_RESPONSE_CLUSTER : ZDO_NODE_REQUEST_CLUSTER;
        CHECK(aps_frame_encode(&header, n ? answer : query, n ? 17 : 3,
                               apdu, sizeof(apdu), &aps_length) == APS_CODEC_OK);
        CHECK(aps_length == (n ? 25 : 11) && apdu[0] == 0 && apdu[1] == 0);
        CHECK(apdu[2] == 2 && apdu[3] == (n ? 0x80 : 0) && apdu[4] == 0 && apdu[5] == 0);
        CHECK(apdu[6] == 0 && apdu[7] == 0xa9 && apdu[8] == 0x5a);
        CHECK(aps_frame_decode(apdu, aps_length, &transport) == APS_CODEC_OK);
        CHECK(transport.header.cluster_id == header.cluster_id && transport.payload_offset == 8);
        if (n) {
            CHECK(zdo_node_rsp_decode(apdu + transport.payload_offset, transport.payload_length, &response) == ZDO_NODE_OK);
            CHECK(response.descriptor.stack_revision == 22 && response.sequence == 0x5a);
            CHECK(zdo_node_rsp_encode(&response, output, sizeof(output), &length) == ZDO_NODE_OK);
        } else {
            CHECK(zdo_node_req_decode(apdu + transport.payload_offset, transport.payload_length, &request) == ZDO_NODE_OK);
            CHECK(request.address == 0x1234 && request.sequence == 0x5a);
            CHECK(zdo_node_req_encode(&request, output, sizeof(output), &length) == ZDO_NODE_OK);
        }
        CHECK(length == transport.payload_length && memcmp(output, apdu + 8, length) == 0);
    }
    return 0;
}

static uint16_t run_tests(void)
{
    uint16_t failure;
    failure = golden_cases(); if (failure) return failure;
    failure = status_cases(); if (failure) return failure;
    failure = descriptor_cases(); if (failure) return failure;
    failure = null_cases(); if (failure) return failure;
    return aps_cases();
}

#if defined(__SDCC)
volatile MCU_XDATA MCU_AT(0x1e00) uint8_t zdo_node_result[8];
void main(void)
{
    uint16_t failure = run_tests();
    memcpy(zdo_node_result, "ZND1", 4);
    zdo_node_result[4] = 1; zdo_node_result[5] = 8;
    zdo_node_result[6] = (uint8_t)failure; zdo_node_result[7] = (uint8_t)(failure >> 8);
    __asm
        .globl _zdo_node_done
    _zdo_node_done:
        nop
    __endasm;
    for (;;) {}
}
#else
#include <stdio.h>
#include <stdlib.h>

static uint16_t exact_cases(void)
{
    uint32_t n;
    uint16_t offset, value, cap, status;
    uint8_t needed, valid;
    uint8_t *bytes;
    zdo_node_request_t *q = malloc(sizeof(*q));
    zdo_node_response_t *r = malloc(sizeof(*r));
    CHECK(q != NULL && r != NULL);
    for (n = 0; n <= 65535UL; n++) {
        q->address = (uint16_t)n; q->sequence = (uint8_t)n; q->consumed = 255;
        for (cap = 0; cap <= 4; cap++) {
            bytes = malloc(cap ? cap : 1);
            CHECK(bytes != NULL);
            memset(bytes, 0xc7, cap ? cap : 1); length = 0xa5;
            CHECK(zdo_node_req_encode(q, bytes, cap, &length) == (cap < 3 ? ZDO_NODE_SPACE : ZDO_NODE_OK));
            if (cap < 3)
                CHECK(length == 0xa5 && filled(bytes, cap ? cap : 1, 0xc7));
            else {
                CHECK(length == 3 && bytes[0] == (uint8_t)n && bytes[1] == (uint8_t)n && bytes[2] == (uint8_t)(n >> 8));
                CHECK(zdo_node_req_decode(bytes, cap, &request) == ZDO_NODE_OK);
                CHECK(request.sequence == q->sequence && request.address == n && request.consumed == 3);
                if (cap == 4) CHECK(bytes[3] == 0xc7);
            }
            free(bytes);
        }
    }
    for (cap = 0; cap <= 101; cap++) {
        bytes = malloc(cap ? cap : 1);
        CHECK(bytes != NULL);
        memset(bytes, 0xc7, cap ? cap : 1);
        memcpy(bytes, answer, cap < 17 ? cap : 17);
        memset(q, 0xa5, sizeof(*q));
        CHECK(zdo_node_req_decode(bytes, cap, q) == (cap > 100 ? ZDO_NODE_TOO_LONG
              : cap < 3 ? ZDO_NODE_TRUNCATED : ZDO_NODE_OK));
        if (cap < 3 || cap > 100) CHECK(filled(q, sizeof(*q), 0xa5));
        else CHECK(q->sequence == 0x5a && q->address == 0x3400 && q->consumed == 3);
        memset(r, 0xa5, sizeof(*r)); old_response = *r;
        CHECK(zdo_node_rsp_decode(bytes, cap, r) == (cap > 100 ? ZDO_NODE_TOO_LONG
              : cap < 17 ? ZDO_NODE_TRUNCATED : ZDO_NODE_OK));
        if (cap < 17 || cap > 100) CHECK(memcmp(r, &old_response, sizeof(*r)) == 0);
        else CHECK(r->consumed == 17 && r->descriptor.stack_revision == 22);
        free(bytes);
    }
    for (status = 0; status < 256; status++) {
        needed = status == 0 ? 17 : status == 0x84 ? 2
            : status == 0x80 || status == 0x81 || status == 0x89 ? 4 : 0;
        for (cap = 0; cap <= 18; cap++) {
            bytes = malloc(cap ? cap : 1);
            CHECK(bytes != NULL);
            memset(bytes, 0xc7, cap ? cap : 1); length = 0xa5;
            CHECK(zdo_node_rsp_decode(answer, 17, r) == ZDO_NODE_OK);
            r->status = (uint8_t)status;
            CHECK(zdo_node_rsp_encode(r, bytes, cap, &length) == (!needed ? ZDO_NODE_UNSUPPORTED_STATUS
                  : cap < needed ? ZDO_NODE_SPACE : ZDO_NODE_OK));
            if (!needed || cap < needed)
                CHECK(length == 0xa5 && filled(bytes, cap ? cap : 1, 0xc7));
            else {
                CHECK(length == needed && bytes[0] == 0x5a && bytes[1] == status);
                CHECK(memcmp(bytes + 2, answer + 2, needed - 2u) == 0);
                CHECK(filled(bytes + needed, cap - needed, 0xc7));
            }
            memcpy(bytes, answer, cap < 17 ? cap : 17);
            if (cap >= 2) bytes[1] = (uint8_t)status;
            memset(r, 0xa5, sizeof(*r));
            CHECK(zdo_node_rsp_decode(bytes, cap, r) == (cap < 2 ? ZDO_NODE_TRUNCATED
                  : !needed ? ZDO_NODE_UNSUPPORTED_STATUS : cap < needed ? ZDO_NODE_TRUNCATED : ZDO_NODE_OK));
            if (cap < 2 || !needed || cap < needed) CHECK(filled(r, sizeof(*r), 0xa5));
            else CHECK(r->status == status && r->consumed == needed);
            free(bytes);
        }
    }
    for (offset = 0; offset < 8; offset++) {
        for (value = 0; value < 256; value++) {
            CHECK(zdo_node_rsp_decode(answer, 17, r) == ZDO_NODE_OK);
            switch (offset) {
            case 0: r->descriptor.logical_type = (uint8_t)value; valid = value < 3; break;
            case 1: r->descriptor.available = (uint8_t)value; valid = value < 4; break;
            case 2: r->descriptor.frequency_band = (uint8_t)value; valid = !(value & 0xe2u); break;
            case 3: r->descriptor.mac_capability = (uint8_t)value; valid = !(value & 0x30u); break;
            case 4: r->descriptor.max_buffer = (uint8_t)value; valid = value < 128; break;
            case 5: r->descriptor.server_flags = (uint8_t)value; valid = value < 128; break;
            case 6: r->descriptor.stack_revision = (uint8_t)value; valid = value < 128; break;
            default: r->descriptor.descriptor_capability = (uint8_t)value; valid = value < 4; break;
            }
            memset(output, 0xc7, sizeof(output)); length = 0xa5;
            CHECK(zdo_node_rsp_encode(r, output, sizeof(output), &length)
                  == (valid ? ZDO_NODE_OK : ZDO_NODE_INVALID_DESCRIPTOR));
            if (valid) {
                CHECK(length == 17 && zdo_node_rsp_decode(output, length, &response) == ZDO_NODE_OK);
                CHECK(memcmp(r, &response, sizeof(*r)) == 0);
            } else CHECK(length == 0xa5 && filled(output, sizeof(output), 0xc7));
        }
    }
    for (offset = 4; offset < 17; offset++) {
        bytes = malloc(17);
        CHECK(bytes != NULL);
        for (value = 0; value < 256; value++) {
            memcpy(bytes, answer, 17); bytes[offset] = (uint8_t)value;
            memset(r, 0xa5, sizeof(*r)); old_response = *r;
            CHECK(zdo_node_rsp_decode(bytes, 17, r) == (wire_valid(bytes) ? ZDO_NODE_OK : ZDO_NODE_INVALID_DESCRIPTOR));
            if (wire_valid(bytes)) {
                CHECK(zdo_node_rsp_encode(r, output, sizeof(output), &length) == ZDO_NODE_OK);
                CHECK(length == 17 && memcmp(output, bytes, 17) == 0);
            } else CHECK(memcmp(r, &old_response, sizeof(*r)) == 0);
        }
        free(bytes);
    }
    free(r); free(q);
    return 0;
}

int main(void)
{
    uint16_t failure = run_tests();
    uint32_t shared = zdo_node_checks;
    if (!failure) failure = exact_cases();
    if (failure) {
        fprintf(stderr, "ZDO Node Descriptor failed at line %u after %lu checks\n",
                failure, (unsigned long)zdo_node_checks);
        return 1;
    }
    printf("ZDO Node Descriptor: %lu shared / %lu total checks PASS; synthetic metadata only.\n",
           (unsigned long)shared, (unsigned long)zdo_node_checks);
    return 0;
}
#endif
