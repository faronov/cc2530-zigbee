/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef ZCL_TEMPERATURE_H
#define ZCL_TEMPERATURE_H

#include "zcl_dispatch.h"

#define ZCL_TEMP_CLUSTER 0x0402u
#define ZCL_TEMP_UNKNOWN (-32767 - 1)
#define ZCL_TEMP_FAULT_TIME 1u
#define ZCL_TEMP_FAULT_TOKEN 2u
#define ZCL_TEMP_PENDING_LIMIT 65535UL

typedef struct {
    uint16_t minimum;
    uint16_t maximum;
    int16_t change;
} zcl_temp_cfg_t;

typedef struct {
    uint32_t stamp;
    uint32_t pending_at;
    uint16_t age;
    uint16_t serial;
    int16_t minimum;
    int16_t maximum;
    int16_t value;
    int16_t baseline;
    int16_t pending_value;
    zcl_temp_cfg_t defaults;
    zcl_temp_cfg_t reporting;
    uint8_t configured;
    uint8_t pending;
    uint8_t fault;
} zcl_temp_t;

typedef struct {
    uint16_t token;
    uint8_t ready;
    uint8_t length;
} zcl_temp_report_t;

/* Synthetic hundredths of Celsius, never a physical sensor assertion.
 * Defaults are explicitly supplied local configuration, not a selected profile.
 * Known min/max must be ordered; 8000 means unknown. Initial value is unknown.
 * Default configuration cannot itself be the FFFF/0000 restore-defaults command.
 * No retained pointers: initialized contexts may be relocated between calls.
 */
zcl_codec_result_t zcl_temp_init(zcl_temp_t * volatile ctx, uint32_t now,
                                int16_t minimum, int16_t maximum,
                                const zcl_temp_cfg_t * volatile defaults);
zcl_codec_result_t zcl_temp_sample(zcl_temp_t * volatile ctx, volatile uint32_t now, int16_t value);
zcl_codec_result_t zcl_temp_rx(zcl_temp_t * volatile ctx, uint32_t now,
                              const uint8_t * volatile request, uint16_t length,
                              uint8_t * volatile response, uint16_t capacity,
                              zcl_dispatch_info_t * volatile info);

/* now is a continuous caller uint32 SECOND epoch, with true forward separation
 * <2^31 between calls. No physical clock is read. Pending lifetime <=65535 s.
 * routes is a truthful 0/1 assertion that the caller resolved nonempty bindings;
 * this module owns neither destinations nor authorization. sequence is caller-owned.
 * OK with ready=0 means no report due/route, not a send. ready=1 leases one report.
 */
zcl_codec_result_t zcl_temp_prepare(zcl_temp_t * volatile ctx, uint32_t now,
                                   uint8_t sequence, uint8_t routes,
                                   uint8_t * volatile response, uint16_t capacity,
                                   zcl_temp_report_t * volatile report);

/* sent=0 cancels without changing the reported baseline (issued_at must be zero).
 * sent=1 confirms the prepared value was successfully sent to the resolved
 * destinations at issued_at, in the same epoch, between prepare and now.
 * This is caller evidence, not peer delivery/authentication inferred from bytes.
 * Samples/reads may occur while pending; configuration and another prepare may not.
 * A stale/duplicate token is rejected. Tokens never wrap within an initialization.
 */
zcl_codec_result_t zcl_temp_finish(zcl_temp_t * volatile ctx, volatile uint32_t now,
                                  uint16_t token, uint8_t sent, volatile uint32_t issued_at);

/* Stable truthful nonoverlapping storage; serialized foreground-only calls.
 * Local failures preserve all outputs/context EXCEPT detected clock/pending-age
 * violations or token exhaustion latch ctx->fault, rejecting until explicit init.
 * Init is volatile abandonment, not persistent/factory/network reset.
 * Full wire, unknown-value, default and transport policies: docs/ZCL_TEMPERATURE.md.
 */
#endif
