/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef MAC_FRAME_H
#define MAC_FRAME_H

#include <stdint.h>

#define MAC_FRAME_MAX_BODY 125u
#define MAC_FRAME_MAX_HEADER 23u

#define MAC_FRAME_DATA 1u
#define MAC_FRAME_ACK 2u
#define MAC_ADDRESS_NONE 0u
#define MAC_ADDRESS_SHORT 2u
#define MAC_ADDRESS_EXTENDED 3u

#define MAC_FLAG_SECURITY 0x08u
#define MAC_FLAG_PENDING 0x10u
#define MAC_FLAG_ACK_REQUEST 0x20u
#define MAC_FLAG_PAN_COMPRESSION 0x40u

typedef enum {
    MAC_CODEC_OK = 0,
    MAC_CODEC_INVALID_ARGUMENT,
    MAC_CODEC_TRUNCATED,
    MAC_CODEC_TOO_LONG,
    MAC_CODEC_BUFFER_TOO_SMALL,
    MAC_CODEC_UNSUPPORTED_TYPE,
    MAC_CODEC_UNSUPPORTED_VERSION,
    MAC_CODEC_UNSUPPORTED_SECURITY,
    MAC_CODEC_UNSUPPORTED_ADDRESSING,
    MAC_CODEC_INVALID_HEADER
} mac_codec_result_t;

typedef struct {
    uint8_t type;
    uint8_t version;
    uint8_t flags;
    uint8_t sequence;
    uint8_t destination_mode;
    uint8_t source_mode;
    uint16_t destination_pan;
    uint16_t source_pan;
    /* Address octets are in wire order, least significant octet first. */
    uint8_t destination[8];
    uint8_t source[8];
} mac_header_t;

typedef struct {
    mac_header_t header;
    uint8_t payload_offset;
    uint8_t payload_length;
} mac_frame_info_t;

/* Bodies exclude the PHY length, FCS and radio metadata. OK is not authentication.
 * Outputs are unchanged on error. Input/output objects must not overlap.
 * Use in the foreground; the SDCC implementation is not ISR-reentrant.
 */
mac_codec_result_t mac_frame_decode(const uint8_t *body, uint16_t length,
                                    mac_frame_info_t *result);
mac_codec_result_t mac_frame_encode(const mac_header_t *header,
                                    const uint8_t *payload, uint16_t payload_length,
                                    uint8_t *body, uint16_t capacity, uint8_t *length);

#endif
