/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef ZCL_DISPATCH_H
#define ZCL_DISPATCH_H

#include "zcl_attributes.h"

#define ZCL_COMMAND_WRITE_ATTRIBUTES 0x02u
#define ZCL_COMMAND_WRITE_UNDIVIDED 0x03u
#define ZCL_COMMAND_WRITE_RESPONSE 0x04u
#define ZCL_COMMAND_WRITE_NO_RESPONSE 0x05u
#define ZCL_COMMAND_DISCOVER_ATTRIBUTES 0x0cu
#define ZCL_COMMAND_DISCOVER_RESPONSE 0x0du
#define ZCL_STATUS_UNSUP_COMMAND 0x81u
#define ZCL_STATUS_INVALID_VALUE 0x87u
#define ZCL_STATUS_READ_ONLY 0x88u
#define ZCL_STATUS_INVALID_DATA_TYPE 0x8du

#define ZCL_DISPATCH_RESPONSE 0u
#define ZCL_DISPATCH_DEFAULT_RECEIVED 1u
#define ZCL_DISPATCH_SILENT 2u

typedef struct {
    uint8_t kind;
    uint8_t length;
    uint8_t command_id;
    uint8_t sequence;
    uint8_t requested_count;
    uint8_t returned_count;
    uint8_t discovery_complete;
    uint8_t default_command;
    uint8_t default_status;
    uint8_t default_raw_status;
} zcl_dispatch_info_t;

/* Caller already selected one existing cluster side on one unicast endpoint
 * and applied profile/delivery/security admission. No endpoint registry,
 * broadcast, transaction matching, reporting or network send.
 * Read, Discover, read-only Write/Undivided/No Response, received Default
 * Response and unsupported-command replies. A writable cluster uses the
 * bounded internal write contract; IdentifyTime is handled by zcl_id_rx.
 * Namespace mismatch never executes a handler; it generates UNSUP_COMMAND,
 * except that Default Response and Write No Response never generate a reply.
 * Namespace-mismatched Write No Response returns UNSUPPORTED_NO_RESPONSE.
 * Parsed No Response requests publish SILENT, not a claim all records wrote.
 * Malformed No Response returns TRUNCATED; unknown value extents return
 * UNSUPPORTED_DATA_TYPE (also for ordinary writes), never an invented span.
 * OK requires inspecting kind: DEFAULT_RECEIVED/SILENT publish only info
 * (length=0). Ordinary Write Response contains only errors, or one SUCCESS.
 * requested_count counts records, returned_count counts response status
 * records; both are zero for SILENT and malformed-command defaults.
 * Other local errors leave both outputs unchanged. Outputs must not overlap
 * each other or stable, truthful input storage. Foreground-only/non-reentrant.
 * Volatile pointer copies constrain SDCC IRAM spilling, not pointed-to data.
 */
zcl_codec_result_t zcl_dispatch_unicast(const zcl_attribute_set_t * volatile set,
                                       const uint8_t * volatile request, uint16_t request_length,
                                       uint8_t * volatile response, uint16_t capacity,
                                       zcl_dispatch_info_t * volatile info);

#endif
