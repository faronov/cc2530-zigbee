/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 */
#ifndef ZDO_SRV_H
#define ZDO_SRV_H

#include "aps_frame.h"
#include "zdo_node.h"

#define ZDO_SRV_DEVICE_ANNCE 0x0013u
#define ZDO_SRV_PARENT_ANNCE 0x001fu
#define ZDO_SRV_UPDATE_NOTIFY 0x003bu

typedef enum {
    ZDO_SRV_OK = 0,
    ZDO_SRV_ARGUMENT,
    ZDO_SRV_CONTEXT,
    ZDO_SRV_LOCAL,
    ZDO_SRV_TRUNCATED,
    ZDO_SRV_TOO_LONG,
    ZDO_SRV_SPACE,
    ZDO_SRV_NOT_REQUEST,
    ZDO_SRV_NOTIFICATION
} zdo_srv_result_t;

typedef enum {
    ZDO_SRV_REPLY = 1,
    ZDO_SRV_DROP_BROADCAST,
    ZDO_SRV_DROP_PARENT
} zdo_srv_kind_t;

typedef struct {
    zdo_node_descriptor_t descriptor;
    uint16_t address;
} zdo_srv_local_t;

typedef struct {
    aps_header_t header;
    /* 0/1: any lower-layer broadcast addressing, even with APS unicast. */
    uint8_t broadcast;
} zdo_srv_rx_t;

typedef struct {
    uint16_t cluster_id;
    uint8_t kind, endpoint, sequence, status, length, consumed;
} zdo_srv_info_t;

/* Stateless offline ED server, not endpoint registration or admission.
 * The caller already selected/admitted this peer, NWK destination and payload,
 * authenticated as required and removed duplicates. This API cannot do that.
 * Header must describe plain APS Data, profile0/destination endpoint0, source
 * 00..FE; unicast or broadcast only. Only ACK_REQUEST is admitted in flags
 * (not with APS broadcast). broadcast must truthfully include every lower
 * addressing layer. No response to broadcasts, response clusters, Device_annce
 * or unsolicited update notifications. Those notifications return an explicit
 * unimplemented-processing error; Parent_annce is dropped by this ED.
 *
 * local is a caller-supplied truthful ED descriptor with logical_type2 and
 * address1..FFF7. No membership/address assignment or production defaults.
 * Node_Desc_req returns the local descriptor or INV_REQUESTTYPE for another
 * queried address. Other unicast requests get TSN/status-only NOT_SUPPORTED.
 * Unknown mandatory services are still missing, not made conformant by fallback.
 *
 * Inputs include TSN; at most ZDO_NODE_MAX_BODY bytes. Mandatory fields required;
 * trailing fields ignored. consumed is the recognized prefix, not full length.
 * Reply metadata selects the response cluster and requester's source endpoint;
 * NWK destination, fresh APS/NWK counters, ACK/security policy, sending and
 * transaction ownership belong to the caller. No input counter is echoed.
 * On OK inspect kind: drops have zero reply length/cluster/endpoint/status and
 * leave the entire response buffer untouched. Errors preserve buffer AND info.
 * All pointers required, even with zero capacity or a no-reply outcome.
 * Truthful disjoint CODE/RAM objects; exclude MMIO/status/alias/compiler scratch.
 * Serialized foreground only, nonreentrant; no retained pointers, heap or I/O.
 */
zdo_srv_result_t zdo_srv_handle(const zdo_srv_local_t * volatile local,
                               const zdo_srv_rx_t * volatile rx,
                               const uint8_t * volatile body, uint16_t length,
                               uint8_t * volatile response, uint16_t capacity,
                               zdo_srv_info_t * volatile info) JOIN_FAR;

#endif
