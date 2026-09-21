/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef ZCL_WRITE_H
#define ZCL_WRITE_H

#include "zcl_dispatch.h"

/* Internal composition contract, not a generic attribute permission registry.
 * Optional permission for ONE full-range uint16 attribute in the selected set.
 * The cluster owns validation of that declaration and actual state application.
 * id is input; value/written publish the last accepted value on local success.
 * NULL makes the entire existing attribute table read-only (including Basic).
 */
typedef struct {
    uint16_t id;
    uint16_t value;
    uint8_t written;
} zcl_wr_t;

/* Internal entry: set/frame/context already validated by the real caller.
 * frame is a decoded global Write/Undivided/No Response in this namespace.
 * payload has frame->payload_length accessible bytes. info is zeroed caller staging,
 * not a public output: it may change on failure. response and edit do not.
 * Foreground-only, non-reentrant; stable nonoverlapping objects/storage.
 * Unknown value extents are explicitly unsupported, never guessed/skipped.
 */
zcl_codec_result_t zcl_wr_handle(const zcl_attribute_set_t * volatile set,
                                const zcl_frame_info_t * volatile frame,
                                const uint8_t * volatile payload,
                                uint8_t * volatile response, uint16_t capacity,
                                zcl_dispatch_info_t * volatile info,
                                zcl_wr_t * volatile edit);

#endif
