/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef ZDO_RUNTIME_H
#define ZDO_RUNTIME_H
#include "nwk_aps.h"
#include "zdo_srv.h"

#define ZDO_RUNTIME_WAIT 312500UL
#define ZDO_RUNTIME_MAP_SIZE 4u
#define ZDO_RUNTIME_NODE 1u
#define ZDO_RUNTIME_NWK_ADDRESS 2u
#define ZDO_RUNTIME_IEEE_ADDRESS 3u
#define ZDO_RUNTIME_PARENT 4u
#define ZDO_RUNTIME_NO_EVENT 255u

typedef enum {
    ZDO_RUNTIME_OK = 0, ZDO_RUNTIME_ARGUMENT, ZDO_RUNTIME_STATE,
    ZDO_RUNTIME_FULL, ZDO_RUNTIME_FORMAT, ZDO_RUNTIME_IGNORED,
    ZDO_RUNTIME_TIMEOUT, ZDO_RUNTIME_TRANSMIT, ZDO_RUNTIME_REMOTE,
    ZDO_RUNTIME_CONFLICT, ZDO_RUNTIME_SECURITY, ZDO_RUNTIME_CLOCK,
    ZDO_RUNTIME_DROPPED, ZDO_RUNTIME_CANCELLED
} zdo_runtime_result_t;

typedef struct {
    uint8_t ieee[8], used, valid;
    uint16_t address;
} zdo_runtime_address_t;

typedef struct {
    zdo_node_descriptor_t local, tc;
    zdo_runtime_address_t map[ZDO_RUNTIME_MAP_SIZE];
    ed_packet_t packet, response, application;
    uint32_t last, deadline;
    uint8_t version, next_tsn, sequence, query, query_tx, tx_done, result;
    uint8_t response_pending, response_tx, application_ready, security_event;
    uint8_t parent_information, parent_known, conflict;
    uint8_t response_result;
} zdo_runtime_t;

/* Fixed direct TC/parent, one client transaction, one server response and one
 * application delivery. Incoming packets are taken ONLY from real nwk_aps.
 * No authenticated-input flag or external successful-admission shortcut.
 * request: Node Descriptor, single NWK/IEEE address resolution, ED Timeout1.
 * ZDO requests use AR0; the bounded client retries own response transactions.
 * step never drives MAC. It preserves its pending TX through cancellation and
 * quiescence; take_result cannot turn a logical response into physical release.
 * Public context fields are allocation/read-only diagnostics, not setters.
 */
zdo_runtime_result_t zdo_runtime_init(zdo_runtime_t *ctx,
    const zdo_node_descriptor_t *local, uint32_t now);
zdo_runtime_result_t zdo_runtime_request(zdo_runtime_t *ctx, nwk_aps_t *transport,
    uint8_t which, uint32_t now);
zdo_runtime_result_t zdo_runtime_step(zdo_runtime_t *ctx, nwk_aps_t *transport, uint32_t now);
/* Cancel client/server work without discarding physical TX ownership.
 * A client CANCELLED result is available only after its TX is quiescent.
 * Dropped server replies return DROPPED with the NWK_APS cause in
 * response_result. APS wrap exhaustion discards the response, not membership;
 * a deferred response never blocks reception of a client/control response.
 */
zdo_runtime_result_t zdo_runtime_cancel(zdo_runtime_t *ctx, nwk_aps_t *transport, uint32_t now);
zdo_runtime_result_t zdo_runtime_take_result(zdo_runtime_t *ctx, uint8_t *which, uint8_t *result);
zdo_runtime_result_t zdo_runtime_take_application(zdo_runtime_t *ctx, ed_packet_t *packet);

#endif
