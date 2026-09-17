/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef MAC_FRAME_H
#define MAC_FRAME_H

#include <stdint.h>

#define MAC_FRAME_MAX_BODY 125u
#define MAC_FRAME_MAX_HEADER 23u
#define MAC_COMMAND_MAX_PAYLOAD 4u
#define MAC_BEACON_MAX_PAYLOAD 52u
#define MAC_BEACON_MAX_PENDING 7u

#define MAC_FRAME_BEACON 0u
#define MAC_FRAME_DATA 1u
#define MAC_FRAME_ACK 2u
#define MAC_FRAME_COMMAND 3u
#define MAC_ADDRESS_NONE 0u
#define MAC_ADDRESS_SHORT 2u
#define MAC_ADDRESS_EXTENDED 3u

#define MAC_FLAG_SECURITY 0x08u
#define MAC_FLAG_PENDING 0x10u
#define MAC_FLAG_ACK_REQUEST 0x20u
#define MAC_FLAG_PAN_COMPRESSION 0x40u

#define MAC_COMMAND_ASSOCIATION_REQUEST 0x01u
#define MAC_COMMAND_ASSOCIATION_RESPONSE 0x02u
#define MAC_COMMAND_DISASSOCIATION 0x03u
#define MAC_COMMAND_DATA_REQUEST 0x04u
#define MAC_COMMAND_BEACON_REQUEST 0x07u

#define MAC_CAPABILITY_ALTERNATE_COORDINATOR 0x01u
#define MAC_CAPABILITY_FFD 0x02u
#define MAC_CAPABILITY_MAINS_POWER 0x04u
#define MAC_CAPABILITY_RX_ON_WHEN_IDLE 0x08u
#define MAC_CAPABILITY_SECURITY 0x40u
#define MAC_CAPABILITY_ALLOCATE_ADDRESS 0x80u

#define MAC_ASSOCIATION_SUCCESS 0u
#define MAC_ASSOCIATION_PAN_AT_CAPACITY 1u
#define MAC_ASSOCIATION_PAN_ACCESS_DENIED 2u
#define MAC_DISASSOCIATION_COORDINATOR_REQUEST 1u
#define MAC_DISASSOCIATION_DEVICE_REQUEST 2u

#define MAC_SUPERFRAME_BATTERY_LIFE_EXTENSION 0x1000u
#define MAC_SUPERFRAME_PAN_COORDINATOR 0x4000u
#define MAC_SUPERFRAME_ASSOCIATION_PERMIT 0x8000u

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
    MAC_CODEC_INVALID_HEADER,
    MAC_CODEC_UNSUPPORTED_COMMAND,
    MAC_CODEC_INVALID_COMMAND,
    MAC_CODEC_UNSUPPORTED_BEACON,
    MAC_CODEC_INVALID_BEACON
} mac_codec_result_t;

typedef struct {
    uint8_t identifier;
    uint8_t capability;
    uint16_t short_address;
    uint8_t status;
    uint8_t reason;
} mac_command_t;

typedef struct {
    uint16_t superframe_specification;
    uint8_t gts_permit;
    uint8_t short_count;
    uint8_t extended_count;
    uint8_t short_offset;
    uint8_t extended_offset;
    uint8_t payload_offset;
    uint8_t payload_length;
} mac_beacon_info_t;

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
 * Volatile pointer copies reduce SDCC IRAM spills, not alter pointed-to
 * bytes; caller storage/lifetime requirements are unchanged.
 */
mac_codec_result_t mac_frame_decode(const uint8_t * volatile body, uint16_t length,
                                    mac_frame_info_t * volatile result);
mac_codec_result_t mac_frame_encode(const mac_header_t * volatile header,
                                    const uint8_t * volatile payload, uint16_t payload_length,
                                    uint8_t * volatile body, uint16_t capacity, uint8_t * volatile length);

/* Command payloads include their identifier. Unused fields decode as zero.
 * Payload-only success does not validate a frame header or perform a procedure.
 */
mac_codec_result_t mac_command_decode(const uint8_t *payload, uint16_t length,
                                      mac_command_t *result);
mac_codec_result_t mac_command_encode(const mac_command_t * volatile command,
                                      uint8_t * volatile payload, uint16_t capacity, uint8_t * volatile length);

/* Decode the MAC payload (starting with Superframe Specification), not the MHR.
 * GTS descriptors are unsupported. Offsets refer to this input; no bytes are
 * copied for pending addresses or the opaque upper-layer Beacon Payload.
 * Superframe fields are raw metadata, not a validated schedule.
 */
mac_codec_result_t mac_beacon_decode(const uint8_t *payload, uint16_t length,
                                     mac_beacon_info_t *result);

#endif
