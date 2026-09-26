/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "zdo_node.h"

#include <stddef.h>
#include <string.h>
#include "mac_link_workspace_guard_internal.h"
#if defined(CC2530_MAC_LINK_WORKSPACE)
#include "mac_link_workspace_internal.h"
#endif

static uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static void write_le16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static uint8_t response_size(uint8_t status)
{
    switch (status) {
    case ZDO_NODE_SUCCESS: return 17;
    case ZDO_NODE_INVALID_REQUEST:
    case ZDO_NODE_DEVICE_NOT_FOUND:
    case ZDO_NODE_NO_DESCRIPTOR: return 4;
    case ZDO_NODE_NOT_SUPPORTED: return 2;
    default: return 0;
    }
}

static uint8_t descriptor_valid(const zdo_node_descriptor_t *d)
{
    return d->logical_type <= 2 && d->available <= 3
        && !(d->frequency_band & 0xe2u) && !(d->mac_capability & 0x30u)
        && d->max_buffer <= 0x7f && d->max_incoming <= 0x7fff
        && d->max_outgoing <= 0x7fff && d->server_flags <= 0x7f
        && d->stack_revision <= 0x7f && d->descriptor_capability <= 3;
}

static void read_descriptor(const uint8_t *p, zdo_node_descriptor_t *d)
{
    d->logical_type = p[0] & 7u;
    d->available = (p[0] >> 3) & 3u;
    d->frequency_band = p[1] >> 3;
    d->mac_capability = p[2];
    d->manufacturer = read_le16(p + 3);
    d->max_buffer = p[5];
    d->max_incoming = read_le16(p + 6);
    d->server_flags = p[8] & 0x7fu;
    d->stack_revision = p[9] >> 1;
    d->max_outgoing = read_le16(p + 10);
    d->descriptor_capability = p[12];
}

static void write_descriptor(const zdo_node_descriptor_t *d, uint8_t *p)
{
    p[0] = (uint8_t)(d->logical_type | (d->available << 3));
    p[1] = (uint8_t)(d->frequency_band << 3);
    p[2] = d->mac_capability;
    write_le16(p + 3, d->manufacturer);
    p[5] = d->max_buffer;
    write_le16(p + 6, d->max_incoming);
    write_le16(p + 8, (uint16_t)(d->server_flags | ((uint16_t)d->stack_revision << 9)));
    write_le16(p + 10, d->max_outgoing);
    p[12] = d->descriptor_capability;
}

zdo_node_result_t zdo_node_req_decode(const uint8_t *body, uint16_t size,
                                     zdo_node_request_t *output)
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    if (!LW_IO(LW_NONE, body, size, 0) || !LW_IO(LW_NONE, output, sizeof(*output), 1))
        return ZDO_NODE_INVALID_ARGUMENT;
#endif
    zdo_node_request_t candidate;
    if (body == NULL || output == NULL)
        return ZDO_NODE_INVALID_ARGUMENT;
    if (size > ZDO_NODE_MAX_BODY)
        return ZDO_NODE_TOO_LONG;
    if (size < 3)
        return ZDO_NODE_TRUNCATED;
    memset(&candidate, 0, sizeof(candidate));
    candidate.sequence = body[0];
    candidate.address = read_le16(body + 1);
    candidate.consumed = 3;
    *output = candidate;
    return ZDO_NODE_OK;
}

zdo_node_result_t zdo_node_req_encode(const zdo_node_request_t *request,
                                     uint8_t *body, uint16_t capacity, uint8_t *size)
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    if (!LW_IO(LW_NONE, request, sizeof(*request), 0) ||
        !LW_IO(LW_NONE, body, capacity, 1) || !LW_IO(LW_NONE, size, 1, 1))
        return ZDO_NODE_INVALID_ARGUMENT;
#endif
    if (request == NULL || body == NULL || size == NULL)
        return ZDO_NODE_INVALID_ARGUMENT;
    if (capacity < 3)
        return ZDO_NODE_SPACE;
    body[0] = request->sequence;
    write_le16(body + 1, request->address);
    *size = 3;
    return ZDO_NODE_OK;
}

zdo_node_result_t zdo_node_rsp_decode(const uint8_t *body, uint16_t size,
                                     zdo_node_response_t *output)
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    LW_ENTER(LW_ZDO_DECODE, ZDO_NODE_INVALID_ARGUMENT);
    if (!LW_IO(LW_ZDO_DECODE, body, size, 0) || !LW_IO(LW_ZDO_DECODE, output, sizeof(*output), 1))
        return LW_RETURN(LW_ZDO_DECODE, ZDO_NODE_INVALID_ARGUMENT);
#endif
#if defined(CC2530_MAC_LINK_WORKSPACE)
#define candidate (link_work_arena.protocol.parent.zdo.node)
#else
    zdo_node_response_t candidate;
#endif
    uint8_t needed;
    if (body == NULL || output == NULL)
        return LW_RETURN(LW_ZDO_DECODE, ZDO_NODE_INVALID_ARGUMENT);
    if (size > ZDO_NODE_MAX_BODY)
        return LW_RETURN(LW_ZDO_DECODE, ZDO_NODE_TOO_LONG);
    if (size < 2)
        return LW_RETURN(LW_ZDO_DECODE, ZDO_NODE_TRUNCATED);
    needed = response_size(body[1]);
    if (!needed)
        return LW_RETURN(LW_ZDO_DECODE, ZDO_NODE_UNSUPPORTED_STATUS);
    if (size < needed)
        return LW_RETURN(LW_ZDO_DECODE, ZDO_NODE_TRUNCATED);
    memset(&candidate, 0, sizeof(candidate));
    candidate.sequence = body[0];
    candidate.status = body[1];
    candidate.consumed = needed;
    if (needed >= 4) {
        candidate.has_address = 1;
        candidate.address = read_le16(body + 2);
    }
    if (candidate.status == ZDO_NODE_SUCCESS) {
        /* Reserved bits must be checked before unpacking discards them. */
        if ((body[4] & 0xe0u) || (body[5] & 7u)
                || (body[12] & 0x80u) || (body[13] & 1u))
            return LW_RETURN(LW_ZDO_DECODE, ZDO_NODE_INVALID_DESCRIPTOR);
        read_descriptor(body + 4, &candidate.descriptor);
        if (!descriptor_valid(&candidate.descriptor))
            return LW_RETURN(LW_ZDO_DECODE, ZDO_NODE_INVALID_DESCRIPTOR);
    }
    *output = candidate;
    return LW_RETURN(LW_ZDO_DECODE, ZDO_NODE_OK);
}
#undef candidate

zdo_node_result_t zdo_node_rsp_encode(const zdo_node_response_t *response,
                                     uint8_t *body, uint16_t capacity, uint8_t *size)
{
#if defined(CC2530_MAC_LINK_WORKSPACE)
    LW_ENTER(LW_ZDO_ENCODE, ZDO_NODE_INVALID_ARGUMENT);
    if (!LW_IO(LW_ZDO_ENCODE, response, sizeof(*response), 0) ||
        !LW_IO(LW_ZDO_ENCODE, body, capacity, 1) || !LW_IO(LW_ZDO_ENCODE, size, 1, 1))
        return LW_RETURN(LW_ZDO_ENCODE, ZDO_NODE_INVALID_ARGUMENT);
#endif
    uint8_t needed;
    if (response == NULL || body == NULL || size == NULL)
        return LW_RETURN(LW_ZDO_ENCODE, ZDO_NODE_INVALID_ARGUMENT);
    needed = response_size(response->status);
    if (!needed)
        return LW_RETURN(LW_ZDO_ENCODE, ZDO_NODE_UNSUPPORTED_STATUS);
    if (response->status == ZDO_NODE_SUCCESS && !descriptor_valid(&response->descriptor))
        return LW_RETURN(LW_ZDO_ENCODE, ZDO_NODE_INVALID_DESCRIPTOR);
    if (capacity < needed)
        return LW_RETURN(LW_ZDO_ENCODE, ZDO_NODE_SPACE);
    body[0] = response->sequence;
    body[1] = response->status;
    if (needed >= 4)
        write_le16(body + 2, response->address);
    if (response->status == ZDO_NODE_SUCCESS)
        write_descriptor(&response->descriptor, body + 4);
    *size = needed;
    return LW_RETURN(LW_ZDO_ENCODE, ZDO_NODE_OK);
}
