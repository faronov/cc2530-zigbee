/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 * Public synthetic coordinator/oracles, never part of the target image.
 */
#include "security_keys.h"
#include "security_joint_model.h"
#include "zigbee_key_hash.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const uint8_t install[18] = {
    0x83,0xfe,0xd3,0x40,0x7a,0x93,0x97,0x23,0xa5,0xc6,0x39,0xb2,0x69,0x16,0xd5,0x05,0xc3,0xb5
};
static const uint8_t network[16] = {0x10,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
static const uint8_t updated[16] = {0x30,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
static security_keys_config_t config = {
    {0x11,2,3,4,5,6,7,8}, {0x22,2,3,4,5,6,7,8},
    {0x33,2,3,4,5,6,7,8}, 0x1234, 0xffff, 15, 254
};
static ccm_star_limits_t limits = {1000,128};
static uint8_t bootstrap[16], frame[116], inner[116], raw[116], length, written, event;
static ed_packet_t packet, output;
static security_keys_status_t status;
static zigbee_security_key_t key;
static zigbee_security_info_t info;
static zigbee_mmo_info_t hash;
static nwk_frame_info_t nwk;

static void hex(const uint8_t *p, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; i++) printf("%02x", p[i]);
}
static void word(uint16_t n) { printf("%02x%02x", n & 255u, n >> 8); }
static void configuration(const security_keys_config_t *c)
{
    hex(c->own_ieee,8); hex(c->tc_ieee,8); hex(c->extended_pan,8);
    word(c->pan); word(c->address); printf("%02x%02x",c->channel,c->update_id);
}
static void packet_bytes(const ed_packet_t *p)
{
    printf("%02x%02x%02x",p->nwk.type,p->nwk.version,p->nwk.discover_route);
    word(p->nwk.flags); word(p->nwk.destination); word(p->nwk.source);
    printf("%02x%02x",p->nwk.radius,p->nwk.sequence);
    hex(p->nwk.destination_ieee,8); hex(p->nwk.source_ieee,8);
    printf("%02x%02x%02x%02x",p->aps.type,p->aps.delivery_mode,p->aps.flags,p->aps.destination_endpoint);
    word(p->aps.cluster_id); word(p->aps.profile_id);
    printf("%02x%02x%02x",p->aps.source_endpoint,p->aps.counter,p->length);
    hex(p->payload,ED_PAYLOAD_MAX);
}
static void call(uint8_t action, security_keys_result_t expected, uint8_t phase)
{
    security_keys_result_t result;
    memset(&output,0xa5,sizeof(output)); written = event = 0xa5;
    if (action == 6) output = packet;
    printf("CALL %u %u ",action,length); hex(raw,116); putchar(' '); packet_bytes(&output); putchar('\n');
    security_joint_trace(1);
    if (action == 0) result = security_keys_open();
    else if (action == 1) result = security_keys_provision(&config,install,10,20,&limits,3);
    else if (action == 2) result = security_keys_associate(0x1234,3);
    else if (action == 3) result = security_keys_receive(raw,length,&output,&event,&limits,3);
    else if (action == 4) result = security_keys_request(7,8,raw,116,&written,&limits,3);
    else if (action == 5) result = security_keys_verify(9,10,raw,116,&written,&limits,3);
    else if (action == 6) result = security_keys_send(&output,1,raw,116,&written,&limits,3);
    else { assert(action == 7); result = security_keys_leave(11,raw,116,&written,&limits,3); }
    security_joint_trace(0);
    assert(result == expected);
    assert(security_keys_status(&status) == SECURITY_KEYS_OK && status.phase == phase);
    printf("RESULT %u %u %u ",result,written,event);
    configuration(&status.config);
    printf("%02x%02x%02x%02x%02x%02x%02x ",status.phase,status.active_sequence,
           status.slot_valid,status.newer_pending,status.parent_information,status.timeout_pending,status.result);
    hex(raw,116); putchar(' '); packet_bytes(&output); putchar('\n');
    printf("NV "); hex(security_joint_nv(),4096); putchar('\n');
}
static void reset(uint8_t erase)
{
    printf("RESET %u\n",erase); security_joint_reset(erase);
}
static void material(const uint8_t *secret, uint8_t identifier, uint32_t count)
{
    memset(&key,0,sizeof(key)); key.limits = limits;
    key.level = 5; key.extended_nonce = 1; key.key_identifier = identifier;
    key.counter = count; key.key_sequence = 255;
    memcpy(key.source,config.tc_ieee,8);
    if (identifier == 2 || identifier == 3)
        assert(zigbee_key_hash(secret,identifier == 2 ? 0 : 2,key.key,1000,128,&hash) == ZIGBEE_MMO_OK);
    else memcpy(key.key,secret,16);
}
static void seal(const uint8_t *link, uint8_t identifier, uint32_t aps_count,
                 uint8_t network_secure, uint32_t nwk_count)
{
    uint8_t n;
    assert(ed_wire_encode(&packet,frame,116,&n) == ZIGBEE_SECURITY_OK);
    assert(ed_wire_nwk(frame,n,&nwk) == ZIGBEE_SECURITY_OK);
    material(link,identifier,aps_count);
    assert(ed_wire_crypt(0,1,&key,frame+nwk.payload_offset,nwk.payload_length,inner,116,&info) == ZIGBEE_SECURITY_OK);
    memcpy(frame+nwk.payload_offset,inner,info.length); n = nwk.payload_offset+info.length;
    memset(raw,0xa5,116);
    if (network_secure) {
        material(network,1,nwk_count);
        assert(ed_wire_crypt(0,0,&key,frame,n,raw,116,&info) == ZIGBEE_SECURITY_OK);
        length = info.length;
    } else { memcpy(raw,frame,n); length = n; }
}
static void transport(uint8_t type, const uint8_t *secret)
{
    uint8_t offset = type == 1 ? 19 : 18;
    memset(&packet,0,sizeof(packet)); packet.nwk.version = 2;
    packet.nwk.destination = 0x1234; packet.nwk.radius = 1;
    packet.aps.type = ED_APS_COMMAND; packet.aps.counter = 0x42;
    packet.payload[0] = 5; packet.payload[1] = type; memcpy(packet.payload+2,secret,16);
    if (type == 1) packet.payload[18] = 255;
    memcpy(packet.payload+offset,config.own_ieee,8);
    memcpy(packet.payload+offset+8,config.tc_ieee,8); packet.length = offset+16;
}
int main(void)
{
    reset(1); memset(raw,0xa5,116);
    call(0,SECURITY_KEYS_EMPTY,SECURITY_KEYS_UNPROVISIONED);
    call(2,SECURITY_KEYS_STATE,SECURITY_KEYS_UNPROVISIONED);
    assert(install_code_derive(install,18,bootstrap,1000,128,&hash) == ZIGBEE_MMO_OK);
    call(1,SECURITY_KEYS_OK,SECURITY_KEYS_PROVISIONED);
    call(2,SECURITY_KEYS_OK,SECURITY_KEYS_ASSOCIATED);
    transport(1,network); seal(bootstrap,2,0,0,0);
    raw[length-1] ^= 1; call(3,SECURITY_KEYS_AUTH,SECURITY_KEYS_ASSOCIATED); raw[length-1] ^= 1;
    call(3,SECURITY_KEYS_OK,SECURITY_KEYS_RECEIVED);
    call(3,SECURITY_KEYS_CONTEXT,SECURITY_KEYS_RECEIVED);
    reset(0); call(0,SECURITY_KEYS_OK,SECURITY_KEYS_RECEIVED);
    call(4,SECURITY_KEYS_OK,SECURITY_KEYS_REQUESTED);
    reset(0); call(0,SECURITY_KEYS_OK,SECURITY_KEYS_REQUESTED);
    transport(4,updated); seal(bootstrap,3,1,1,0);
    call(3,SECURITY_KEYS_OK,SECURITY_KEYS_PROVISIONAL);
    reset(0); call(0,SECURITY_KEYS_OK,SECURITY_KEYS_PROVISIONAL);
    call(5,SECURITY_KEYS_OK,SECURITY_KEYS_WAIT_CONFIRM);
    reset(0); call(0,SECURITY_KEYS_OK,SECURITY_KEYS_WAIT_CONFIRM);
    memset(&packet,0,sizeof(packet)); packet.nwk.version = 2;
    packet.nwk.destination = 0x1234; packet.nwk.radius = 1;
    packet.aps.type = ED_APS_COMMAND; packet.aps.counter = 0x42;
    packet.length = 11; packet.payload[0] = 16; packet.payload[2] = 4;
    memcpy(packet.payload+3,config.own_ieee,8);
    seal(updated,0,0,1,1);
    raw[length-1] ^= 1; call(3,SECURITY_KEYS_AUTH,SECURITY_KEYS_WAIT_CONFIRM); raw[length-1] ^= 1;
    call(3,SECURITY_KEYS_OK,SECURITY_KEYS_VERIFIED);
    memcpy(inner,raw,116);
    memset(&packet,0,sizeof(packet)); packet.nwk.version = 2; packet.nwk.source = 0x1234;
    packet.nwk.radius = 1; packet.nwk.sequence = 12;
    packet.aps.source_endpoint = packet.aps.destination_endpoint = 1;
    packet.aps.profile_id = 0x0104; packet.aps.cluster_id = 6; packet.aps.counter = 0x44;
    packet.length = 3; memcpy(packet.payload,"abc",3);
    call(6,SECURITY_KEYS_OK,SECURITY_KEYS_VERIFIED);
    memcpy(raw,inner,116);
    call(3,SECURITY_KEYS_REPLAY,SECURITY_KEYS_VERIFIED);
    reset(0); call(0,SECURITY_KEYS_OK,SECURITY_KEYS_VERIFIED);
    call(3,SECURITY_KEYS_REPLAY,SECURITY_KEYS_VERIFIED);
    call(7,SECURITY_KEYS_OK,SECURITY_KEYS_LEFT);
    reset(0); call(0,SECURITY_KEYS_OK,SECURITY_KEYS_LEFT);
    return 0;
}
