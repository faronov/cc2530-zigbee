/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef NWK_FRAME_H
#define NWK_FRAME_H

#include <stdint.h>

#define NWK_FRAME_MAX_BODY 116u
#define NWK_FRAME_MIN_HEADER 8u
#define NWK_FRAME_MAX_HEADER 24u
#define NWK_FRAME_DATA 0u
#define NWK_FRAME_PROTOCOL_VERSION 2u

#define NWK_DISCOVER_ROUTE_SUPPRESS 0u
#define NWK_DISCOVER_ROUTE_ENABLE 1u

#define NWK_FLAG_MULTICAST 0x0100u
#define NWK_FLAG_SECURITY 0x0200u
#define NWK_FLAG_SOURCE_ROUTE 0x0400u
#define NWK_FLAG_DESTINATION_IEEE 0x0800u
#define NWK_FLAG_SOURCE_IEEE 0x1000u
#define NWK_FLAG_END_DEVICE_INITIATOR 0x2000u

typedef enum {
    NWK_CODEC_OK = 0,
    NWK_CODEC_INVALID_ARGUMENT,
    NWK_CODEC_TRUNCATED,
    NWK_CODEC_TOO_LONG,
    NWK_CODEC_BUFFER_TOO_SMALL,
    NWK_CODEC_UNSUPPORTED_TYPE,
    NWK_CODEC_UNSUPPORTED_VERSION,
    NWK_CODEC_UNSUPPORTED_SECURITY,
    NWK_CODEC_UNSUPPORTED_LAYOUT,
    NWK_CODEC_INVALID_HEADER
} nwk_codec_result_t;

typedef struct {
    uint8_t type;
    uint8_t version;
    uint8_t discover_route;
    uint16_t flags;
    uint16_t destination;
    uint16_t source;
    uint8_t radius;
    uint8_t sequence;
    /* Octets in wire order, least significant octet first. */
    uint8_t destination_ieee[8];
    uint8_t source_ieee[8];
} nwk_header_t;

typedef struct {
    nwk_header_t header;
    uint8_t payload_offset;
    uint8_t payload_length;
} nwk_frame_info_t;

/* Input/output bodies are NPDUs only, without MAC/PHY/FCS or radio metadata.
 * OK is syntax only, not APS validation, authentication or permission to send.
 * Outputs stay unchanged on error; input/output objects must not overlap.
 * The caller supplies truthful storage sizes. Payload may be NULL only if empty.
 * Absent IEEE addresses decode as zero and are ignored by the encoder.
 * Foreground only: SDCC parameter/local storage is not ISR-reentrant.
 */
nwk_codec_result_t nwk_frame_decode(const uint8_t *body, uint16_t length,
                                    nwk_frame_info_t *result);
nwk_codec_result_t nwk_frame_encode(const nwk_header_t *header,
                                    const uint8_t *payload, uint16_t payload_length,
                                    uint8_t *body, uint16_t capacity, uint8_t *length);

#endif
