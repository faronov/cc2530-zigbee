/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef ZCL_WIRE_H
#define ZCL_WIRE_H

#include <stdint.h>

#define ZCL_FRAME_MAX_BODY 100u
#define ZCL_FRAME_MIN_HEADER 3u
#define ZCL_FRAME_MAX_HEADER 5u
#define ZCL_FRAME_GLOBAL 0u
#define ZCL_FRAME_CLUSTER_SPECIFIC 1u
#define ZCL_FLAG_MANUFACTURER_SPECIFIC 0x04u
#define ZCL_FLAG_SERVER_TO_CLIENT 0x08u
#define ZCL_FLAG_DISABLE_DEFAULT_RESPONSE 0x10u

#define ZCL_TYPE_NO_DATA 0x00u
#define ZCL_TYPE_DATA8 0x08u
#define ZCL_TYPE_DATA16 0x09u
#define ZCL_TYPE_DATA24 0x0au
#define ZCL_TYPE_DATA32 0x0bu
#define ZCL_TYPE_DATA40 0x0cu
#define ZCL_TYPE_DATA48 0x0du
#define ZCL_TYPE_DATA56 0x0eu
#define ZCL_TYPE_DATA64 0x0fu
#define ZCL_TYPE_BOOLEAN 0x10u
#define ZCL_TYPE_BITMAP8 0x18u
#define ZCL_TYPE_BITMAP16 0x19u
#define ZCL_TYPE_BITMAP24 0x1au
#define ZCL_TYPE_BITMAP32 0x1bu
#define ZCL_TYPE_BITMAP40 0x1cu
#define ZCL_TYPE_BITMAP48 0x1du
#define ZCL_TYPE_BITMAP56 0x1eu
#define ZCL_TYPE_BITMAP64 0x1fu
#define ZCL_TYPE_UINT8 0x20u
#define ZCL_TYPE_UINT16 0x21u
#define ZCL_TYPE_UINT24 0x22u
#define ZCL_TYPE_UINT32 0x23u
#define ZCL_TYPE_UINT40 0x24u
#define ZCL_TYPE_UINT48 0x25u
#define ZCL_TYPE_UINT56 0x26u
#define ZCL_TYPE_UINT64 0x27u
#define ZCL_TYPE_INT8 0x28u
#define ZCL_TYPE_INT16 0x29u
#define ZCL_TYPE_INT24 0x2au
#define ZCL_TYPE_INT32 0x2bu
#define ZCL_TYPE_INT40 0x2cu
#define ZCL_TYPE_INT48 0x2du
#define ZCL_TYPE_INT56 0x2eu
#define ZCL_TYPE_INT64 0x2fu
#define ZCL_TYPE_ENUM8 0x30u
#define ZCL_TYPE_ENUM16 0x31u
#define ZCL_TYPE_OCTET_STRING 0x41u
#define ZCL_TYPE_CHARACTER_STRING 0x42u
#define ZCL_VALUE_MAX_SIZE 255u

typedef enum {
    ZCL_CODEC_OK = 0,
    ZCL_CODEC_INVALID_ARGUMENT,
    ZCL_CODEC_TRUNCATED,
    ZCL_CODEC_TOO_LONG,
    ZCL_CODEC_BUFFER_TOO_SMALL,
    ZCL_CODEC_UNSUPPORTED_FRAME_TYPE,
    ZCL_CODEC_UNSUPPORTED_DATA_TYPE,
    ZCL_CODEC_UNSUPPORTED_LAYOUT,
    ZCL_CODEC_INVALID_HEADER,
    ZCL_CODEC_INVALID_VALUE,
    ZCL_CODEC_INVALID_TABLE,
    ZCL_CODEC_UNSUPPORTED_COMMAND,
    ZCL_CODEC_UNSUPPORTED_CONTEXT
} zcl_codec_result_t;

typedef struct {
    uint8_t type;
    uint8_t flags;
    uint16_t manufacturer_code;
    uint8_t sequence;
    uint8_t command_id;
} zcl_header_t;

typedef struct {
    zcl_header_t header;
    uint8_t ignored_control_bits;
    uint8_t payload_offset;
    uint8_t payload_length;
} zcl_frame_info_t;

typedef struct {
    uint8_t type;
    uint8_t string_non_value;
    const uint8_t *data;
    uint16_t data_length;
} zcl_value_t;

typedef struct {
    uint8_t type;
    uint8_t data_offset;
    uint8_t data_length;
    uint8_t encoded_length;
    /* Matches the table's pattern; the attribute decides whether to use it. */
    uint8_t non_value_pattern;
} zcl_value_info_t;

/* Wire syntax only, not command/attribute policy or permission to transmit.
 * All input/output objects must be non-overlapping, with truthful storage sizes.
 * Outputs stay unchanged on error; no heap or retained pointers.
 * Foreground only: SDCC parameter/local storage is not ISR-reentrant.
 */
zcl_codec_result_t zcl_frame_decode(const uint8_t *body, uint16_t length,
                                    zcl_frame_info_t *result);
zcl_codec_result_t zcl_frame_encode(const zcl_header_t *header,
                                    const uint8_t *payload, uint16_t payload_length,
                                    uint8_t *body, uint16_t capacity, uint8_t *length);

/* One value, with its type supplied separately (no type byte on output).
 * Fixed values are octets in wire order, not native integers or C strings.
 * Decode permits trailing bytes and returns the number consumed.
 * No-data consumes zero bytes; callers must advance their enclosing record.
 * String data excludes its length prefix; string_non_value requires empty data.
 * Character-string octets are preserved without charset/UTF-8 validation.
 */
zcl_codec_result_t zcl_value_decode(uint8_t type, const uint8_t *body, uint16_t length,
                                    zcl_value_info_t *result);
zcl_codec_result_t zcl_value_encode(const zcl_value_t *value, uint8_t *body,
                                    uint16_t capacity, uint8_t *length);

#endif
