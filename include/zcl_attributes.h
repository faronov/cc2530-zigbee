/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef ZCL_ATTRIBUTES_H
#define ZCL_ATTRIBUTES_H

#include "zcl_wire.h"

#define ZCL_ATTRIBUTE_MAX_COUNT 16u
#define ZCL_ATTRIBUTE_SERVER 0u
#define ZCL_ATTRIBUTE_CLIENT 1u
#define ZCL_COMMAND_READ_ATTRIBUTES 0x00u
#define ZCL_COMMAND_READ_ATTRIBUTES_RESPONSE 0x01u
#define ZCL_COMMAND_DEFAULT_RESPONSE 0x0bu
#define ZCL_STATUS_SUCCESS 0x00u
#define ZCL_STATUS_NOT_AUTHORIZED 0x7eu
#define ZCL_STATUS_MALFORMED_COMMAND 0x80u
#define ZCL_STATUS_UNSUPPORTED_ATTRIBUTE 0x86u
#define ZCL_STATUS_INSUFFICIENT_SPACE 0x89u

typedef struct {
    uint16_t id;
    uint8_t readable;
    zcl_value_t value;
} zcl_attribute_t;

typedef struct {
    const zcl_attribute_t *attributes;
    uint8_t count;
    uint8_t side;
    uint8_t manufacturer_specific;
    uint16_t manufacturer_code;
} zcl_attribute_set_t;

typedef struct {
    uint8_t length;
    uint8_t command_id;
    uint8_t requested_count;
    uint8_t returned_count;
} zcl_read_info_t;

/* Check table structure/selectors/unique IDs, not value types or backing data. */
zcl_codec_result_t zcl_attr_set_check(const zcl_attribute_set_t *set);

/* One selected cluster-side/namespace, after caller-owned unicast delivery,
 * endpoint/profile/cluster filtering and applicable authentication checks.
 * Capacity is the complete response budget, also limited by ZCL_FRAME_MAX_BODY.
 * OK means a response was built, including protocol errors or a partial list.
 * Unsupported commands/contexts and local failures leave both outputs unchanged.
 * Inputs must remain stable and accessible for the whole foreground-only,
 * non-reentrant call. Outputs must not overlap each other or any input storage.
 * A non-readable entry's value is never inspected; no write API is provided.
 * Volatile pointer copies constrain SDCC IRAM spilling, not pointed-to data.
 */
zcl_codec_result_t zcl_read_attrs_unicast(const zcl_attribute_set_t * volatile set,
                                          const uint8_t * volatile request, uint16_t request_length,
                                          uint8_t * volatile response, uint16_t capacity,
                                          zcl_read_info_t * volatile info);

#endif
