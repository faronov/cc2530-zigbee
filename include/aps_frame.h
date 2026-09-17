/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef APS_FRAME_H
#define APS_FRAME_H

#include <stdint.h>

#define APS_FRAME_MAX_BODY 108u
#define APS_FRAME_HEADER_SIZE 8u
#define APS_FRAME_DATA 0u

#define APS_DELIVERY_UNICAST 0u
#define APS_DELIVERY_BROADCAST 2u
#define APS_DELIVERY_GROUP 3u

#define APS_FLAG_ACK_FORMAT 0x10u
#define APS_FLAG_SECURITY 0x20u
#define APS_FLAG_ACK_REQUEST 0x40u
#define APS_FLAG_EXTENDED_HEADER 0x80u

typedef enum {
    APS_CODEC_OK = 0,
    APS_CODEC_INVALID_ARGUMENT,
    APS_CODEC_TRUNCATED,
    APS_CODEC_TOO_LONG,
    APS_CODEC_BUFFER_TOO_SMALL,
    APS_CODEC_UNSUPPORTED_TYPE,
    APS_CODEC_UNSUPPORTED_DELIVERY,
    APS_CODEC_UNSUPPORTED_SECURITY,
    APS_CODEC_UNSUPPORTED_LAYOUT,
    APS_CODEC_INVALID_HEADER
} aps_codec_result_t;

typedef struct {
    uint8_t type;
    uint8_t delivery_mode;
    uint8_t flags;
    uint8_t destination_endpoint;
    uint16_t cluster_id;
    uint16_t profile_id;
    uint8_t source_endpoint;
    uint8_t counter;
} aps_header_t;

typedef struct {
    aps_header_t header;
    uint8_t payload_offset;
    uint8_t payload_length;
} aps_frame_info_t;

/* APDUs only, without NWK/MAC headers or security. Payload stays opaque.
 * OK is syntax, not endpoint/profile acceptance, ACK handling or authentication.
 * Outputs stay unchanged on error; input/output objects must not overlap.
 * The caller supplies truthful storage sizes. Payload may be NULL only if empty.
 * Foreground only: SDCC parameter/local storage is not ISR-reentrant.
 */
aps_codec_result_t aps_frame_decode(const uint8_t *body, uint16_t length,
                                    aps_frame_info_t *result);
aps_codec_result_t aps_frame_encode(const aps_header_t *header,
                                    const uint8_t *payload, uint16_t payload_length,
                                    uint8_t *body, uint16_t capacity, uint8_t *length);

#endif
