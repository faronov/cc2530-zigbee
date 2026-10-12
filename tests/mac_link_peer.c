/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 * Builders and peer checks reused from tests/test_bdb_join.c. Only transport
 * plumbing differs: inputs are actual TXFIFO bodies, outputs are queued for
 * the radio model rather than delivered as fabricated BDB events.
 */
#include "mac_link_peer.h"
#include "security_joint_model.h"
#include "flash_exec.h"
#include "zigbee_key_hash.h"
#include <assert.h>
#include <string.h>

#if !defined(CC2530_DEFAULT_TC_KEY)
static const uint8_t install_code[18] = {
    0x83,0xfe,0xd3,0x40,0x7a,0x93,0x97,0x23,0xa5,0xc6,0x39,0xb2,0x69,0x16,0xd5,0x05,0xc3,0xb5
};
#endif
const security_keys_config_t link_identity = {
    {1,2,3,4,5,6,7,8}, {17,18,19,20,21,22,23,24},
    {0x10,0x32,0x54,0x76,0x98,0xba,0xdc,0xfe}, 0x1234, 0xffff, 15, 0
};
#define identity link_identity
static const ccm_star_limits_t limits = {1000,1000};
static ed_packet_t peer_out, peer_in;
static uint8_t link_a[16], link_b[16], network[16], effective[16], expected[16];
static uint8_t npdu[116], work[116], other[116], npdu_length, aps_counter, nwk_sequence;
static uint8_t announced, described, requested, verified, permit, associated, extracted;
static uint32_t peer_nwk_counter, peer_aps_counter, last_device_nwk, last_device_aps;
static uint8_t network_sequence;
/* Recovery after a modeled DUT power cycle: the coordinator keeps its state. */
static uint8_t recovering, rejoined, reannounced;
#if defined(CC2530_PARENT_RECOVERY)
static const uint8_t router_ieee[8] = {0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48};
static uint8_t alternate, router_hop;
static uint32_t router_counter;
#define PEER_PARENT_ADDRESS (router_hop ? 0x4567u : 0u)
#define PEER_PARENT_IEEE (router_hop ? router_ieee : identity.tc_ieee)
#define PEER_REJOIN_COUNT (alternate ? BDB_JOIN_ATTEMPTS+1u : 1u)
#else
#define PEER_PARENT_ADDRESS 0u
#define PEER_PARENT_IEEE identity.tc_ieee
#define PEER_REJOIN_COUNT 1u
#endif
unsigned link_peer_rejoins;
uint8_t link_peer_mute_rejoin;
uint8_t link_beacon[48], link_response[27], link_peer_body[125];
uint8_t link_beacon_length, link_response_length, link_peer_length, link_peer_pending;
unsigned link_peer_tx, link_peer_checks, link_peer_app, link_peer_parent;
uint16_t link_peer_report_cluster;
uint8_t link_peer_report[ED_PAYLOAD_MAX], link_peer_report_length, link_peer_report_counter;
#define CHECK(c) do { link_peer_checks++; assert(c); } while (0)

static void base_peer(uint8_t nwk_command, uint8_t aps_type, uint16_t cluster)
{
    memset(&peer_out, 0, sizeof(peer_out));
    peer_out.nwk.type = nwk_command; peer_out.nwk.version = 2;
    peer_out.nwk.destination = 0x5678; peer_out.nwk.radius = 1;
    peer_out.nwk.sequence = nwk_sequence++;
#if defined(CC2530_PARENT_RECOVERY)
    if (nwk_command && router_hop) peer_out.nwk.source = 0x4567;
#endif
    peer_out.aps.type = aps_type; peer_out.aps.counter = aps_counter++;
    peer_out.aps.cluster_id = cluster;
}

static void key_context(zigbee_security_key_t *key, const uint8_t *material,
    uint8_t layer, uint8_t selector, uint32_t counter, uint8_t outgoing)
{
    memset(key, 0, sizeof(*key)); memcpy(key->key, material, 16);
    memcpy(key->source, outgoing ? identity.tc_ieee : identity.own_ieee, 8);
#if defined(CC2530_PARENT_RECOVERY)
    if (outgoing && !layer && router_hop) memcpy(key->source, router_ieee, 8);
#endif
    key->level = 5; key->extended_nonce = 1; key->key_identifier = layer ? selector : 1;
    if (!layer) key->key_sequence = network_sequence;
    key->counter = counter; key->limits = limits;
}

static void seal_peer(uint8_t aps_secure, uint8_t selector, const uint8_t *link, uint8_t nwk_secure)
{
    zigbee_security_key_t key;
    zigbee_security_info_t info;
    nwk_frame_info_t header;
    mac_header_t mh;
    CHECK(!link_peer_pending);
    CHECK(ed_wire_encode(&peer_out, npdu, sizeof(npdu), &npdu_length) == ZIGBEE_SECURITY_OK);
    CHECK(ed_wire_nwk(npdu, npdu_length, &header) == ZIGBEE_SECURITY_OK);
    if (aps_secure) {
        key_context(&key, link, 1, selector, peer_aps_counter++, 1);
        CHECK(ed_wire_crypt(0, 1, &key, npdu+header.payload_offset, header.payload_length,
            work, sizeof(work), &info) == ZIGBEE_SECURITY_OK);
        memcpy(npdu+header.payload_offset, work, info.length);
        npdu_length = (uint8_t)(header.payload_offset+info.length);
    }
    if (nwk_secure) {
        key_context(&key, network, 0, 1,
#if defined(CC2530_PARENT_RECOVERY)
            router_hop ? router_counter++ :
#endif
            peer_nwk_counter++, 1);
        CHECK(ed_wire_crypt(0, 0, &key, npdu, npdu_length, work, sizeof(work), &info) == ZIGBEE_SECURITY_OK);
        memcpy(npdu, work, info.length); npdu_length = info.length;
    }
    memset(&mh, 0, sizeof(mh));
    mh.type = MAC_FRAME_DATA; mh.flags = MAC_FLAG_PAN_COMPRESSION;
    mh.source_mode = mh.destination_mode = MAC_ADDRESS_SHORT;
    mh.source_pan = mh.destination_pan = identity.pan;
#if defined(CC2530_PARENT_RECOVERY)
    if (router_hop) { mh.source[0] = 0x67; mh.source[1] = 0x45; }
#endif
    mh.destination[0] = 0x78; mh.destination[1] = 0x56;
    CHECK(mac_frame_encode(&mh, npdu, npdu_length, link_peer_body, sizeof(link_peer_body),
                           &link_peer_length) == MAC_CODEC_OK);
    link_peer_pending = 1;
}

static void open_device(const uint8_t *body, uint8_t size, uint8_t *aps_secured)
{
    mac_frame_info_t mh;
    nwk_frame_info_t nh;
    aps_frame_info_t ah;
    zigbee_security_key_t key;
    zigbee_security_info_t info;
    zigbee_security_meta_t meta;
    uint8_t length;
    CHECK(mac_frame_decode(body, size, &mh) == MAC_CODEC_OK && mh.header.type == MAC_FRAME_DATA);
    CHECK(mh.header.source[0] == 0x78 && mh.header.source[1] == 0x56);
    CHECK(ed_wire_inspect(0, body+mh.payload_offset, mh.payload_length, &meta) == ZIGBEE_SECURITY_OK);
    CHECK(meta.counter > last_device_nwk); last_device_nwk = meta.counter;
    key_context(&key, network, 0, 1, 0, 0);
    CHECK(ed_wire_crypt(1, 0, &key, body+mh.payload_offset, mh.payload_length,
                      work, sizeof(work), &info) == ZIGBEE_SECURITY_OK);
    length = info.length;
    CHECK(ed_wire_nwk(work, length, &nh) == ZIGBEE_SECURITY_OK);
    *aps_secured = 0;
    if (!nh.header.type) {
        CHECK(ed_wire_aps(work+nh.payload_offset, nh.payload_length, &ah) == ZIGBEE_SECURITY_OK);
        if (ah.header.flags & APS_FLAG_SECURITY) {
            *aps_secured = 1;
            CHECK(ed_wire_inspect(1, work+nh.payload_offset, nh.payload_length, &meta) == ZIGBEE_SECURITY_OK);
            CHECK(meta.key_identifier == 0 && meta.counter > last_device_aps);
            last_device_aps = meta.counter;
            key_context(&key, verified ? link_b : link_a, 1, 0, 0, 0);
            CHECK(ed_wire_crypt(1, 1, &key, work+nh.payload_offset, nh.payload_length,
                               other, sizeof(other), &info) == ZIGBEE_SECURITY_OK);
            memcpy(work+nh.payload_offset, other, info.length);
            length = (uint8_t)(nh.payload_offset+info.length);
        }
    }
    CHECK(ed_wire_decode(work, length, &peer_in) == ZIGBEE_SECURITY_OK);
}

void link_peer_transmitted(const uint8_t *body, uint8_t length)
{
    uint8_t aps_secured;
    mac_frame_info_t mh;
    mac_command_t command;
    zdo_node_response_t descriptor;
    zigbee_mmo_info_t info;
    CHECK(mac_frame_decode(body, length, &mh) == MAC_CODEC_OK);
    link_peer_tx++;
    if (mh.header.type == MAC_FRAME_COMMAND) {
        CHECK(mac_command_decode(body+mh.payload_offset, mh.payload_length, &command) == MAC_CODEC_OK);
        CHECK(!recovering || command.identifier == MAC_COMMAND_BEACON_REQUEST);
        if (command.identifier == MAC_COMMAND_ASSOCIATION_REQUEST) {
            CHECK(command.capability == 0x88 && !memcmp(mh.header.source, identity.own_ieee, 8));
            associated = 1;
        } else if (command.identifier == MAC_COMMAND_DATA_REQUEST) {
            CHECK(associated && !memcmp(mh.header.source, identity.own_ieee, 8));
            extracted = 1;
        } else CHECK(command.identifier == MAC_COMMAND_BEACON_REQUEST);
        return;
    }
    open_device(body, length, &aps_secured);
#if defined(CC2530_PARENT_RECOVERY)
    router_hop = alternate && mh.header.destination[0] == 0x67 && mh.header.destination[1] == 0x45;
    if (!router_hop)
#endif
    CHECK(!mh.header.destination[0] && !mh.header.destination[1]);
    CHECK(!!(mh.header.flags & MAC_FLAG_ACK_REQUEST) == (peer_in.nwk.destination < 0xfffbu));
    if (peer_in.nwk.type) {
        if (peer_in.payload[0] == 4) { CHECK(!recovering); return; } /* Genuine commissioning Leave. */
        if (peer_in.payload[0] == 6) {
            /* R22 3.4.6: secured, MAC-acknowledged, both IEEE, radius 1, to the parent. */
            CHECK(recovering && !rejoined && peer_in.length == 2 && peer_in.payload[1] == 0x08);
            CHECK(!(peer_in.nwk.flags & NWK_FLAG_END_DEVICE_INITIATOR));
            CHECK(peer_in.nwk.destination == PEER_PARENT_ADDRESS &&
                peer_in.nwk.radius == 1 && !peer_in.nwk.discover_route &&
                (peer_in.nwk.flags & (NWK_FLAG_SOURCE_IEEE | NWK_FLAG_DESTINATION_IEEE)) ==
                (NWK_FLAG_SOURCE_IEEE | NWK_FLAG_DESTINATION_IEEE));
            CHECK(!memcmp(peer_in.nwk.source_ieee, identity.own_ieee, 8) &&
                !memcmp(peer_in.nwk.destination_ieee, PEER_PARENT_IEEE, 8));
            link_peer_rejoins++;
            if (link_peer_mute_rejoin) return; /* Parent absent: request is lost. */
#if defined(CC2530_PARENT_RECOVERY)
            if (alternate && !router_hop) return;
#endif
            base_peer(1, 0, 0);
            peer_out.nwk.flags = NWK_FLAG_SOURCE_IEEE | NWK_FLAG_DESTINATION_IEEE;
            memcpy(peer_out.nwk.source_ieee,
#if defined(CC2530_PARENT_RECOVERY)
                router_hop ? router_ieee :
#endif
                identity.tc_ieee, 8);
            memcpy(peer_out.nwk.destination_ieee, identity.own_ieee, 8);
            peer_out.length = 4; peer_out.payload[0] = 7; peer_out.payload[1] = 0x78;
            peer_out.payload[2] = 0x56; peer_out.payload[3] = 0;
            seal_peer(0, 0, NULL, 1); rejoined = 1;
            return;
        }
        CHECK(peer_in.length == 3 && peer_in.payload[0] == 0x0b && peer_in.payload[1] == 1 &&
              !peer_in.payload[2] && verified && (!recovering || reannounced));
        link_peer_parent++;
        base_peer(1, 0, 0);
        peer_out.nwk.flags = NWK_FLAG_SOURCE_IEEE | NWK_FLAG_DESTINATION_IEEE;
        memcpy(peer_out.nwk.source_ieee,
#if defined(CC2530_PARENT_RECOVERY)
            router_hop ? router_ieee :
#endif
            identity.tc_ieee, 8);
        memcpy(peer_out.nwk.destination_ieee, identity.own_ieee, 8);
        peer_out.length = 3; peer_out.payload[0] = 0x0c; peer_out.payload[2] = 2;
        seal_peer(0, 0, NULL, 1);
    } else if (peer_in.aps.type == ED_APS_ACK) return;
    else if (peer_in.aps.type == ED_APS_COMMAND) {
        CHECK(!recovering);
        if (peer_in.payload[0] == 8) {
            CHECK(announced && described && aps_secured && peer_in.length == 2 && peer_in.payload[1] == 4);
            requested = 1;
            base_peer(0, ED_APS_COMMAND, 0);
            peer_out.payload[0] = 5; peer_out.payload[1] = 4; peer_out.length = 34;
            memcpy(peer_out.payload+2, link_b, 16);
            memcpy(peer_out.payload+18, identity.own_ieee, 8);
            memcpy(peer_out.payload+26, identity.tc_ieee, 8);
            CHECK(zigbee_key_hash(link_a, ZIGBEE_HASH_LOAD, effective, 1000, 1000, &info) == ZIGBEE_MMO_OK);
            seal_peer(1, 3, effective, 1);
        } else {
            CHECK(requested && !aps_secured && peer_in.length == 26 &&
                  peer_in.payload[0] == 15 && peer_in.payload[1] == 4);
            CHECK(!memcmp(peer_in.payload+2, identity.own_ieee, 8));
            CHECK(zigbee_key_hash(link_b, ZIGBEE_HASH_VERIFY, expected, 1000, 1000, &info) == ZIGBEE_MMO_OK);
            CHECK(!memcmp(peer_in.payload+10, expected, 16)); verified = 1;
            base_peer(0, ED_APS_COMMAND, 0);
            peer_out.length = 11; peer_out.payload[0] = 16; peer_out.payload[2] = 4;
            memcpy(peer_out.payload+3, identity.own_ieee, 8);
            seal_peer(1, 0, link_b, 1);
        }
    } else if (!peer_in.aps.profile_id && peer_in.aps.cluster_id == 0x0013u) {
        if (recovering) { CHECK(rejoined && !reannounced && peer_in.nwk.destination == 0xfffdu); reannounced = 1; }
        else CHECK(extracted && !requested && peer_in.nwk.destination == 0xfffdu);
        CHECK(peer_in.length == 12 && peer_in.payload[1] == 0x78 && peer_in.payload[2] == 0x56);
        CHECK(!memcmp(peer_in.payload+3, identity.own_ieee, 8) && peer_in.payload[11] == 0x88);
        announced = 1;
    } else if (!peer_in.aps.profile_id && peer_in.aps.cluster_id == 2) {
        CHECK(!recovering && announced && !requested && peer_in.length == 3 && !peer_in.payload[1] && !peer_in.payload[2]);
        memset(&descriptor, 0, sizeof(descriptor)); descriptor.sequence = peer_in.payload[0];
        descriptor.descriptor.frequency_band = 8; descriptor.descriptor.mac_capability = 0x8e;
        descriptor.descriptor.max_buffer = 127; descriptor.descriptor.max_incoming = 64;
        descriptor.descriptor.max_outgoing = 64; descriptor.descriptor.stack_revision = 22;
        descriptor.descriptor.manufacturer = 0x1234;
        base_peer(0, 0, 0x8002u);
        CHECK(zdo_node_rsp_encode(&descriptor, peer_out.payload, sizeof(peer_out.payload), &peer_out.length) == ZDO_NODE_OK);
        seal_peer(0, 0, NULL, 1); described = 1;
    } else if (!peer_in.aps.profile_id && peer_in.aps.cluster_id == 0x0036u) {
        CHECK(!recovering && verified && link_peer_parent && peer_in.nwk.destination == 0xfffcu && peer_in.length == 3);
        CHECK(peer_in.payload[1] == 180 && peer_in.payload[2] == 1); permit = 1;
    } else {
        CHECK(permit && peer_in.aps.profile_id == 0x0104 && (peer_in.aps.flags & APS_FLAG_ACK_REQUEST));
        CHECK(!recovering || (reannounced && link_peer_parent));
        link_peer_app++; link_peer_report_cluster = peer_in.aps.cluster_id; link_peer_report_counter = peer_in.aps.counter;
        link_peer_report_length = peer_in.length; memcpy(link_peer_report, peer_in.payload, peer_in.length);
        base_peer(0, ED_APS_ACK, peer_in.aps.cluster_id);
        peer_out.aps.counter = peer_in.aps.counter; peer_out.aps.profile_id = peer_in.aps.profile_id;
        peer_out.aps.destination_endpoint = peer_in.aps.source_endpoint;
        peer_out.aps.source_endpoint = peer_in.aps.destination_endpoint;
        seal_peer(aps_secured, 0, link_b, 1);
    }
}

void link_peer_transport(uint8_t request_ack)
{
    zigbee_mmo_info_t info;
    base_peer(0, ED_APS_COMMAND, 0);
    if (request_ack) peer_out.aps.flags = APS_FLAG_ACK_REQUEST;
    peer_out.length = 35; peer_out.payload[0] = 5; peer_out.payload[1] = 1;
    memcpy(peer_out.payload+2, network, 16); peer_out.payload[18] = 0;
    memcpy(peer_out.payload+19, identity.own_ieee, 8);
    memcpy(peer_out.payload+27, identity.tc_ieee, 8);
    CHECK(zigbee_key_hash(link_a, 0, effective, 1000, 1000, &info) == ZIGBEE_MMO_OK);
    seal_peer(1, 2, effective, 0);
}

void link_peer_application(ed_packet_t *packet)
{
    memset(packet, 0, sizeof(*packet));
    packet->nwk.version = 2; packet->nwk.radius = 1;
    packet->aps.flags = APS_FLAG_ACK_REQUEST;
    packet->aps.source_endpoint = packet->aps.destination_endpoint = 1;
    packet->aps.profile_id = 0x0104; packet->aps.cluster_id = 6;
    packet->length = 3; packet->payload[0] = 0x55;
}

static void receive_application(uint8_t acknowledge)
{
    CHECK(verified && permit && (!recovering || link_peer_parent));
    base_peer(0, 0, 6);
    peer_out.aps.flags = acknowledge ? APS_FLAG_ACK_REQUEST : 0;
    peer_out.aps.source_endpoint = peer_out.aps.destination_endpoint = 1;
    peer_out.aps.profile_id = 0x0104;
    peer_out.length = 3; peer_out.payload[0] = 0x55;
    seal_peer(0, 0, NULL, 1);
}

void link_peer_receive_application(void) { receive_application(1); }
#if defined(CC2530_REPLAY_POLICY_C)
void link_peer_receive_service(void) { receive_application(0); }
void link_peer_receive_completion(uint8_t step)
{
    static const uint8_t next_network[16] = {
        0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x97,
        0x98,0x99,0x9a,0x9b,0x9c,0x9d,0x9e,0x9f
    };
    zigbee_mmo_info_t hash;
    CHECK(step < 4 && verified && permit && router_hop);
    base_peer(step == 0 || step == 3, ED_APS_COMMAND, 0);
    peer_out.nwk.source = 0; peer_out.length = 1; peer_out.payload[0] = 0xfe;
    if (step == 2) {
        peer_out.length = 35; peer_out.payload[0] = 5; peer_out.payload[1] = 1;
        memcpy(peer_out.payload+2,next_network,16); peer_out.payload[18] = 1;
        memcpy(peer_out.payload+19,identity.own_ieee,8);
        memcpy(peer_out.payload+27,identity.tc_ieee,8);
        CHECK(zigbee_key_hash(link_b,ZIGBEE_HASH_TRANSPORT,effective,1000,1000,&hash)==ZIGBEE_MMO_OK);
        seal_peer(1,2,effective,1);
    } else {
        if (step == 3) { memcpy(network,next_network,16); network_sequence = 1; }
        seal_peer(step == 1,0,link_b,1);
    }
}
#endif

void link_peer_verify(void)
{
    security_keys_status_t status;
    CHECK(security_keys_status(&status) == SECURITY_KEYS_OK && status.phase == SECURITY_KEYS_VERIFIED);
    CHECK(announced && described && requested && verified && permit && link_peer_parent);
    CHECK(!recovering || (rejoined && reannounced && link_peer_rejoins == PEER_REJOIN_COUNT));
}

/* Coordinator state survives the DUT power cycle; in-flight frames do not. */
#define PEER_STATE(X) X(link_a) X(link_b) X(network) X(network_sequence) X(aps_counter) X(nwk_sequence) \
    X(announced) X(described) X(requested) X(verified) X(permit) X(associated) X(extracted) \
    X(peer_nwk_counter) X(peer_aps_counter) X(last_device_nwk) X(last_device_aps)
#define PEER_SAVE(v) memcpy(out+n, &v, sizeof(v)); n += sizeof(v);
#define PEER_LOAD(v) memcpy(&v, in+n, sizeof(v)); n += sizeof(v);
unsigned link_peer_save(uint8_t *out)
{
    unsigned n = 0;
    CHECK(!recovering && verified && permit);
    PEER_STATE(PEER_SAVE)
    return n;
}

void link_peer_recover(bdb_join_config_t *config, const uint8_t *in, unsigned length)
{
    unsigned n = 0;
    PEER_STATE(PEER_LOAD)
    CHECK(n == length && verified && permit);
    recovering = 1; rejoined = reannounced = link_peer_mute_rejoin = 0; link_peer_rejoins = 0;
    link_peer_pending = 0; link_peer_tx = link_peer_checks = link_peer_parent = link_peer_app = 0;
    security_joint_physical_flash(1);
    link_peer_config(config);
#if defined(CC2530_PARENT_RECOVERY)
    alternate = router_hop = 0; router_counter = 1000;
#endif
}

#if defined(CC2530_PARENT_RECOVERY)
/* Explicit public synthetic pre-existing router history, not DUT membership. */
void link_peer_known_parent(security_keys_parent_t *parent)
{
    memset(parent, 0, sizeof(*parent));
    memcpy(parent->ieee, router_ieee, 8); parent->address = 0x4567;
    parent->nwk_floor[0] = 1000;
}

void link_peer_alternate(uint8_t exhausted)
{
    mac_header_t header;
    mac_frame_info_t old;
    uint8_t payload[19];
    CHECK(recovering && !rejoined);
    CHECK(mac_frame_decode(link_beacon, link_beacon_length, &old) == MAC_CODEC_OK);
    memcpy(payload, link_beacon+old.payload_offset, sizeof(payload));
    memset(&header, 0, sizeof(header)); header.type = MAC_FRAME_BEACON;
    header.source_mode = MAC_ADDRESS_SHORT; header.source_pan = identity.pan;
    header.source[0] = 0x67; header.source[1] = 0x45;
    payload[1] &= 0x7f; payload[6] = 0x94;
    if (exhausted) payload[7] ^= 1; /* wrong Extended PAN; cannot become a parent */
    CHECK(mac_frame_encode(&header, payload, sizeof(payload), link_beacon,
        sizeof(link_beacon), &link_beacon_length) == MAC_CODEC_OK);
    alternate = 1;
}
#endif

void link_peer_setup(bdb_join_config_t *config)
{
    uint8_t i;
#if !defined(CC2530_DEFAULT_TC_KEY)
    zigbee_mmo_info_t info;
#endif
    announced = described = requested = verified = permit = associated = extracted = 0;
    recovering = rejoined = reannounced = link_peer_mute_rejoin = 0; link_peer_rejoins = 0;
#if defined(CC2530_PARENT_RECOVERY)
    alternate = router_hop = 0; router_counter = 1000;
#endif
    aps_counter = nwk_sequence = network_sequence = link_peer_pending = 0;
    link_peer_tx = link_peer_checks = link_peer_parent = link_peer_app = 0;
    last_device_nwk = last_device_aps = 0;
    peer_nwk_counter = peer_aps_counter = 1;
    security_joint_physical_flash(1);
    CHECK(security_keys_open() == SECURITY_KEYS_EMPTY);
#if defined(CC2530_DEFAULT_TC_KEY)
    memcpy(link_a, "ZigBeeAlliance09", 16);
#else
    CHECK(install_code_derive(install_code, sizeof(install_code), link_a, 1000, 1000, &info) == ZIGBEE_MMO_OK);
#endif
    for (i = 0; i < 16; i++) { link_b[i] = (uint8_t)(0xa0u+i); network[i] = (uint8_t)(0x30u+i); }
#if defined(CC2530_DEFAULT_TC_KEY)
    CHECK(security_keys_provision_default(&identity, 1, 1, &limits, FLASH_EXEC_POLL_MAX) == SECURITY_KEYS_OK);
#else
    CHECK(security_keys_provision(&identity, install_code, 1, 1, &limits, FLASH_EXEC_POLL_MAX) == SECURITY_KEYS_OK);
#endif
    link_peer_config(config);
}

void link_peer_config(bdb_join_config_t *config)
{
    mac_header_t mh;
    uint8_t payload[19];
    memset(config, 0, sizeof(*config));
    config->scan.channels = 1UL << 15; config->scan.lifetime = 100000; config->scan.work = 512;
    config->scan.saved.pan = 0xffff; config->scan.saved.channel = 11;
    config->association.saved.pan = 0xffff; config->association.saved.channel = 11;
    config->association.response_wait = 2; config->association.capability = 0x88;
    config->association.extraction.epoch = 1; config->association.extraction.frame_wait = 300;
    config->association.extraction.lifetime = 100000; config->association.extraction.work = 512;
    config->association.extraction.pan = identity.pan; config->association.extraction.channel = 15;
    config->association.extraction.local_mode = config->association.extraction.coordinator_mode = 3;
    memcpy(config->association.extraction.local, identity.own_ieee, 8);
    memcpy(config->association.extraction.coordinator, identity.tc_ieee, 8);
    config->transport.limits = limits; config->transport.nv_polls = FLASH_EXEC_POLL_MAX; config->transport.profile = 0x0104;
    config->transport.endpoint = 1; config->link_cost = 1; config->transport.ack_wait = NWK_APS_ACK_WAIT;
    config->transport.broadcast_time = 1000000;
    config->descriptor.logical_type = 2; config->descriptor.frequency_band = 8;
    config->descriptor.mac_capability = 0x88; config->descriptor.max_buffer = 127;
    config->descriptor.max_incoming = config->descriptor.max_outgoing = 64;
    config->descriptor.stack_revision = 22;
    memset(&mh, 0, sizeof(mh)); mh.type = MAC_FRAME_BEACON;
#if defined(CC2530_DEFAULT_TC_KEY)
    mh.source_mode = MAC_ADDRESS_SHORT;
#else
    mh.source_mode = MAC_ADDRESS_EXTENDED;
    memcpy(mh.source, identity.tc_ieee, 8);
#endif
    mh.source_pan = identity.pan;
    memset(payload, 0, sizeof(payload)); payload[0] = 0xff; payload[1] = 0xcf;
    payload[5] = 0x22; payload[6] = 0x84; memcpy(payload+7, identity.extended_pan, 8);
    payload[15] = payload[16] = payload[17] = 255;
    CHECK(mac_frame_encode(&mh, payload, sizeof(payload), link_beacon, sizeof(link_beacon), &link_beacon_length) == MAC_CODEC_OK);
    mh.type = MAC_FRAME_COMMAND; mh.flags = 0x60; mh.destination_mode = mh.source_mode = 3;
    memcpy(mh.source, identity.tc_ieee, 8);
    mh.destination_pan = identity.pan; mh.sequence = 0x91; memcpy(mh.destination, identity.own_ieee, 8);
    payload[0] = 2; payload[1] = 0x78; payload[2] = 0x56; payload[3] = 0;
    CHECK(mac_frame_encode(&mh, payload, 4, link_response, sizeof(link_response), &link_response_length) == MAC_CODEC_OK);
}
