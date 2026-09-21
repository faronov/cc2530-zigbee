/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef ZCL_IDENTIFY_H
#define ZCL_IDENTIFY_H

#include "zcl_dispatch.h"

#define ZCL_ID_CLUSTER 0x0003u
#define ZCL_ID_SILENT 2u

/* Caller-owned state, no retained pointers; may be relocated. Observe but do
 * not edit fields. remaining is IdentifyTime (seconds); phase is elapsed
 * milliseconds into its current second, 0..999 (zero when inactive).
 */
typedef struct {
    uint32_t stamp;
    uint16_t remaining;
    uint16_t phase;
} zcl_id_t;

/* Abstract caller monotonic milliseconds, modulo 2^32; NOT a board clock.
 * Between successful calls actual forward elapsed time MUST be <2^31 ms,
 * including while idle. No clock reset, epoch switch or hidden multi-wrap.
 * Equal timestamps are allowed. Delta >=2^31 (including stale input) or an
 * invalid phase returns INVALID_VALUE, unchanged. The caller must ensure
 * continuity: modular arithmetic cannot detect every broken clock history.
 *
 * init starts idle at now, and is also explicit local cancellation/reset of
 * this volatile procedure only (not factory reset). tick catches up all full
 * seconds in bounded work; it does not restart the fractional phase.
 * All APIs are foreground-only/non-reentrant; ctx must first be initialized.
 */
zcl_codec_result_t zcl_id_init(zcl_id_t *ctx, uint32_t now);
zcl_codec_result_t zcl_id_tick(zcl_id_t *ctx, uint32_t now);

/* One caller-selected, authorized UNICAST standard Identify server (R8).
 * No endpoint/profile registry, authentication, network send or GPIO effect.
 * Identify 00: LE16 seconds, resets phase at now (even repeated same TSN/value).
 * Query 01: no defined payload; active -> Query Response, idle -> silence
 * regardless of Disable Default Response. Standard trailing octets/reserved
 * bits follow existing R8 RX rules. Trigger Effect is unsupported.
 *
 * Global commands use the real existing dispatcher over a fresh two-attribute
 * view: IdentifyTime and ClusterRevision=2. Write Attributes (including
 * IdentifyTime) is STILL unsupported, not a permanent read-only permission.
 * See docs/ZCL_IDENTIFY.md for this full-cluster conformance gap.
 *
 * OK: info.kind is RESPONSE, DEFAULT_RECEIVED, or ZCL_ID_SILENT.
 * SILENT: only kind and sequence are nonzero; response is untouched, length=0.
 * DEFAULT_RECEIVED uses the existing notification contract, no response bytes.
 * Protocol-error replies may return OK: they age the clock but do not apply
 * Identify. Check info, not just return status. All other local failures leave
 * COMPLETE ctx/response/info unchanged (also no time advancement).
 * Response storage must be nonnull even at capacity=0. Inputs/outputs/ctx must
 * not overlap and have truthful accessible extents. No pointers are retained.
 * Caller tick cadence determines observation/indicator latency, not this API.
 */
zcl_codec_result_t zcl_id_rx(zcl_id_t * volatile ctx, uint32_t now,
                            const uint8_t * volatile request, uint16_t length,
                            uint8_t * volatile response, uint16_t capacity,
                            zcl_dispatch_info_t * volatile info);

#endif
