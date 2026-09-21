/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef ZCL_BASIC_H
#define ZCL_BASIC_H

#include "zcl_attributes.h"

#define ZCL_BASIC_CLUSTER_ID 0x0000u
#define ZCL_BASIC_ATTRIBUTE_COUNT 6u
#define ZCL_BASIC_NAME_MAX 32u
#define ZCL_BASIC_BUILD_MAX 16u

typedef struct {
    const uint8_t *data;
    uint8_t length;
} zcl_basic_text_t;

typedef struct {
    zcl_basic_text_t manufacturer;
    zcl_basic_text_t model;
    zcl_basic_text_t software;
    /* R8 Table 3-8: primary 0..6, bit7 battery backup; other values reserved.
     * Use 0 (unknown) for a synthetic lab model, not a guessed board supply.
     */
    uint8_t power_source;
} zcl_basic_config_t;

/* Caller-owned, self-referencing storage. After init, treat ALL fields as
 * read-only; pass &model.set to the existing attribute/dispatch functions.
 * Do not relocate/copy the initialized object. Config strings are copied;
 * they are UTF-8 octets without a length prefix or NUL terminator. The caller
 * supplies valid UTF-8 (no charset validation here).
 */
typedef struct {
    zcl_attribute_set_t set;
    zcl_attribute_t attributes[ZCL_BASIC_ATTRIBUTE_COUNT];
    uint8_t strings[2u * ZCL_BASIC_NAME_MAX + ZCL_BASIC_BUILD_MAX];
    uint8_t scalars[4];
} zcl_basic_t;

/* R8 base-text read-only SERVER model, standard namespace:
 * 0000 ZCLVersion=8; 0004 ManufacturerName; 0005 ModelIdentifier;
 * 0007 PowerSource; 4000 SWBuildID; FFFD ClusterRevision=3.
 * No manufacturer code is assigned (set.manufacturer_code=0 is unused).
 * No endpoint/profile selection, write/report/reset/Identify or network send.
 * This is NOT full Basic/ZCL conformance; see docs/ZCL_LAB.md.
 *
 * Names 0..32 bytes, software 0..16 bytes; NULL data allowed only at length 0.
 * Invalid pointers: INVALID_ARGUMENT; reserved power/oversized text:
 * INVALID_VALUE. Entire output unchanged on failure. Config, backing input
 * spans and output must not overlap and must have truthful accessible sizes.
 * Foreground-only/non-reentrant like the existing SDCC ZCL path.
 * Share one stable model across endpoints only for truly device-wide values.
 */
zcl_codec_result_t zcl_basic_init(zcl_basic_t * volatile model,
                                  const zcl_basic_config_t * volatile config);

#endif
