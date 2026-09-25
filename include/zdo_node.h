/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef ZDO_NODE_H
#define ZDO_NODE_H

#include <stdint.h>
#include "banked_join.h"

#define ZDO_NODE_REQUEST_CLUSTER 0x0002u
#define ZDO_NODE_RESPONSE_CLUSTER 0x8002u
#define ZDO_NODE_MAX_BODY 100u
#define ZDO_NODE_DESCRIPTOR_SIZE 13u
#define ZDO_NODE_SUCCESS 0x00u
#define ZDO_NODE_INVALID_REQUEST 0x80u
#define ZDO_NODE_DEVICE_NOT_FOUND 0x81u
#define ZDO_NODE_NOT_SUPPORTED 0x84u
#define ZDO_NODE_NO_DESCRIPTOR 0x89u

typedef enum {
    ZDO_NODE_OK = 0,
    ZDO_NODE_INVALID_ARGUMENT,
    ZDO_NODE_TRUNCATED,
    ZDO_NODE_TOO_LONG,
    ZDO_NODE_SPACE,
    ZDO_NODE_INVALID_DESCRIPTOR,
    ZDO_NODE_UNSUPPORTED_STATUS
} zdo_node_result_t;

typedef struct {
    uint16_t manufacturer, max_incoming, max_outgoing;
    uint8_t logical_type, available, frequency_band, mac_capability;
    uint8_t max_buffer, server_flags, stack_revision, descriptor_capability;
} zdo_node_descriptor_t;

typedef struct {
    uint16_t address;
    uint8_t sequence, consumed;
} zdo_node_request_t;

typedef struct {
    zdo_node_descriptor_t descriptor;
    uint16_t address;
    uint8_t sequence, status, has_address, consumed;
} zdo_node_response_t;

/* R22 Node_Desc_req/rsp ZDP payloads INCLUDING TSN, excluding APS headers.
 * Callers must establish endpoint0/profile0/correct cluster/unicast transport
 * and separately match peer, requested address and transaction/security state.
 * No endpoint, TSN allocator, responder, advertised device or trust decision.
 *
 * RX requires mandatory fields, ignores trailing bytes per R22 1.2.5 within
 * the bounded100-byte input. consumed identifies only the recognized prefix.
 * SUCCESS carries13 descriptor bytes; addressed failures80/81/89 omit them.
 * Generic NOT_SUPPORTED84 carries TSN/status only (has_address=0). Unknown
 * statuses fail explicitly. Absent descriptor/address fields are zero.
 * Encoder ignores consumed/has_address (RX metadata) and inactive fields;
 * it emits only the canonical prefix for the selected status.
 *
 * available bits0/1 are Complex/User Descriptor Available. server_flags is
 * Server Mask bits0..6; stack_revision is bits9..15, not Beacon version.
 * APS flags and reserved descriptor bits must be zero in this strict subset.
 * Numeric fields are raw metadata, not supported resources or authentication;
 * all16 queried-address bits are preserved, not accepted as routable addresses.
 * Caller-supplied descriptor must truthfully describe any device advertising it.
 *
 * CODE/RAM inputs and complete disjoint caller outputs; no MMIO, reserved
 * status, IRAM alias or compiler scratch. Nonreentrant foreground operations.
 * Errors preserve ALL outputs. No pointers retained and no heap or I/O.
 */
zdo_node_result_t zdo_node_req_decode(const uint8_t *body, uint16_t size,
                                     zdo_node_request_t *output) JOIN_FAR;
zdo_node_result_t zdo_node_req_encode(const zdo_node_request_t *request,
                                     uint8_t *body, uint16_t capacity, uint8_t *size) JOIN_FAR;
zdo_node_result_t zdo_node_rsp_decode(const uint8_t *body, uint16_t size,
                                     zdo_node_response_t *output) JOIN_FAR;
zdo_node_result_t zdo_node_rsp_encode(const zdo_node_response_t *response,
                                     uint8_t *body, uint16_t capacity, uint8_t *size) JOIN_FAR;

#endif
