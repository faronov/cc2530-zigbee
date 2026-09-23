/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#include "zdo_srv.h"

#include <stddef.h>
#include <string.h>

zdo_srv_result_t zdo_srv_handle(const zdo_srv_local_t * volatile local,
                               const zdo_srv_rx_t * volatile rx,
                               const uint8_t * volatile body, uint16_t length,
                               uint8_t * volatile response, uint16_t capacity,
                               zdo_srv_info_t * volatile info)
{
    zdo_node_response_t reply;
    zdo_node_request_t request;
    zdo_srv_info_t candidate;
    uint8_t staged[17], size;

    if (local == NULL || rx == NULL || body == NULL || response == NULL || info == NULL)
        return ZDO_SRV_ARGUMENT;
    if (rx->broadcast > 1 || rx->header.type != APS_FRAME_DATA
            || rx->header.profile_id != 0 || rx->header.destination_endpoint != 0
            || rx->header.source_endpoint == 0xff
            || (rx->header.flags & (uint8_t)~APS_FLAG_ACK_REQUEST)
            || (rx->header.delivery_mode != APS_DELIVERY_UNICAST
                && rx->header.delivery_mode != APS_DELIVERY_BROADCAST)
            || (rx->header.delivery_mode == APS_DELIVERY_BROADCAST && rx->header.flags))
        return ZDO_SRV_CONTEXT;
    if (!local->address || local->address >= 0xfff8u || local->descriptor.logical_type != 2)
        return ZDO_SRV_LOCAL;
    if (length > ZDO_NODE_MAX_BODY)
        return ZDO_SRV_TOO_LONG;
    if (!length)
        return ZDO_SRV_TRUNCATED;
    memset(&reply, 0, sizeof(reply));
    reply.descriptor = local->descriptor;
    reply.sequence = body[0];
    reply.address = local->address;
    if (zdo_node_rsp_encode(&reply, staged, sizeof(staged), &size) != ZDO_NODE_OK)
        return ZDO_SRV_LOCAL;
    if (rx->header.cluster_id & 0x8000u)
        return ZDO_SRV_NOT_REQUEST;
    if (rx->header.cluster_id == ZDO_SRV_DEVICE_ANNCE
            || rx->header.cluster_id == ZDO_SRV_UPDATE_NOTIFY)
        return ZDO_SRV_NOTIFICATION;
    memset(&candidate, 0, sizeof(candidate));
    candidate.sequence = body[0];
    candidate.consumed = 1;
    if (rx->header.cluster_id == ZDO_SRV_PARENT_ANNCE) {
        candidate.kind = ZDO_SRV_DROP_PARENT;
    } else if (rx->broadcast || rx->header.delivery_mode == APS_DELIVERY_BROADCAST) {
        candidate.kind = ZDO_SRV_DROP_BROADCAST;
    } else {
        if (rx->header.cluster_id == ZDO_NODE_REQUEST_CLUSTER) {
            if (zdo_node_req_decode(body, length, &request) != ZDO_NODE_OK)
                return ZDO_SRV_TRUNCATED;
            candidate.consumed = request.consumed;
            reply.address = request.address;
            if (request.address != local->address)
                reply.status = ZDO_NODE_INVALID_REQUEST;
        } else {
            reply.status = ZDO_NODE_NOT_SUPPORTED;
        }
        /* The descriptor and all other codec arguments were validated above. */
        if (zdo_node_rsp_encode(&reply, staged, sizeof(staged), &size) != ZDO_NODE_OK)
            return ZDO_SRV_LOCAL;
        if (capacity < size)
            return ZDO_SRV_SPACE;
        candidate.kind = ZDO_SRV_REPLY;
        candidate.cluster_id = rx->header.cluster_id | 0x8000u;
        candidate.endpoint = rx->header.source_endpoint;
        candidate.status = reply.status;
        candidate.length = size;
        memcpy(response, staged, size);
    }
    *info = candidate;
    return ZDO_SRV_OK;
}
