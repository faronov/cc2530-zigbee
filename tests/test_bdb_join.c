/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 * Synthetic coordinator, PHY events and public identities; genuine services.
 */
#include "bdb_join.h"
#include "security_joint_model.h"
#include "zigbee_key_hash.h"
#include "nv_record.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t install_code[18] = {
    0x83,0xfe,0xd3,0x40,0x7a,0x93,0x97,0x23,0xa5,0xc6,0x39,0xb2,0x69,0x16,0xd5,0x05,0xc3,0xb5
};
static const security_keys_config_t identity = {
    {1,2,3,4,5,6,7,8}, {17,18,19,20,21,22,23,24},
    {0x10,0x32,0x54,0x76,0x98,0xba,0xdc,0xfe}, 0x1234, 0xffff, 15, 0
};
static const ccm_star_limits_t limits = {1000,1000};
static bdb_join_t device;
static mac_tx_t transmitter;
static bdb_join_config_t config;
static bdb_join_action_t action;
static bdb_join_event_t event;
static mac_tx_action_t granted;
static security_keys_status_t status;
static ed_packet_t peer_out, peer_in;
static uint8_t link_a[16], link_b[16], network[16], effective[16], expected[16];
static uint8_t npdu[116], work[116], other[116], mac_body[125], saved[125], ack[3];
static uint8_t beacon[48], response[27], beacon_length, response_length;
static uint8_t mac_length, npdu_length, aps_counter, nwk_sequence, pending;
static uint8_t announced, described, requested, verified, parent_set, permit;
static uint8_t scan_beacon, associated_request, extracted, initial_key;
static uint8_t app_sends, app_counter, dropped_ack, test_case;
static uint8_t leave_sent;
static uint8_t wrap_mode, drop_all_acks;
static uint8_t phy_busy, phy_lose, mute_timeout;
static unsigned lost_frames, muted, busy_events;
static const uint8_t *aux_source;
static uint16_t expected_reply_cluster;
static uint16_t peer_pan;
static uint8_t expected_reply_status, expected_reply_length, reply_seen;
static uint32_t now, peer_nwk_counter, peer_aps_counter, last_device_nwk, last_device_aps;
static uint32_t initial_now = 100;
static unsigned checks, iterations, transmissions;
#define CHECK(x) do { checks++; if (!(x)) { \
    fprintf(stderr, "BDB integration: case%u line%d phase%u/%u key%u error%u/%u\n", \
        test_case, __LINE__, device.phase, transmitter.phase, status.phase, device.result, device.transport.error); \
    exit(1); } } while (0)

static void base_peer(uint8_t nwk_command, uint8_t aps_type, uint16_t cluster)
{
    memset(&peer_out, 0, sizeof(peer_out));
    peer_out.nwk.type = nwk_command; peer_out.nwk.version = 2;
    peer_out.nwk.destination = 0x5678; peer_out.nwk.radius = 1;
    peer_out.nwk.sequence = nwk_sequence++;
    peer_out.aps.type = aps_type; peer_out.aps.counter = aps_counter++;
    peer_out.aps.cluster_id = cluster;
}

static void key_context(zigbee_security_key_t *key, const uint8_t *material,
    uint8_t layer, uint8_t selector, uint32_t counter, uint8_t outgoing)
{
    memset(key, 0, sizeof(*key));
    memcpy(key->key, material, 16);
    memcpy(key->source, outgoing ? (aux_source ? aux_source : identity.tc_ieee) : identity.own_ieee, 8);
    key->level = 5; key->extended_nonce = 1; key->key_identifier = layer ? selector : 1;
    key->counter = counter; key->limits = limits;
}

static void seal_peer(uint8_t aps_secure, uint8_t selector, const uint8_t *link, uint8_t nwk_secure)
{
    zigbee_security_key_t key;
    zigbee_security_info_t info;
    nwk_frame_info_t header;
    mac_header_t mh;
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
        key_context(&key, network, 0, 1, peer_nwk_counter++, 1);
        CHECK(ed_wire_crypt(0, 0, &key, npdu, npdu_length, work, sizeof(work), &info) == ZIGBEE_SECURITY_OK);
        memcpy(npdu, work, info.length); npdu_length = info.length;
    }
    memset(&mh, 0, sizeof(mh));
    mh.type = MAC_FRAME_DATA; mh.flags = MAC_FLAG_PAN_COMPRESSION;
    mh.source_mode = mh.destination_mode = MAC_ADDRESS_SHORT;
    mh.source_pan = mh.destination_pan = peer_pan;
    mh.destination[0] = 0x78; mh.destination[1] = 0x56;
    if (peer_out.nwk.destination >= 0xfffbu) mh.destination[0] = mh.destination[1] = 255;
    CHECK(mac_frame_encode(&mh, npdu, npdu_length, mac_body, sizeof(mac_body), &mac_length) == MAC_CODEC_OK);
    pending = 1;
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

static void observe_transmission(void)
{
    uint8_t body[125], length, aps_secured;
    mac_frame_info_t mh;
    mac_command_t command;
    zdo_node_response_t descriptor;
    zigbee_mmo_info_t hash_info;
    CHECK(mac_tx_copy(&transmitter, body, sizeof(body), &length) == MAC_TX_OK);
    CHECK(mac_frame_decode(body, length, &mh) == MAC_CODEC_OK);
    transmissions++;
    if (mh.header.type == MAC_FRAME_COMMAND) {
        CHECK(mac_command_decode(body+mh.payload_offset, mh.payload_length, &command) == MAC_CODEC_OK);
        if (command.identifier == MAC_COMMAND_ASSOCIATION_REQUEST) {
            CHECK(command.capability == 0x88 && !memcmp(mh.header.source, identity.own_ieee, 8));
            associated_request = 1;
        } else if (command.identifier == MAC_COMMAND_DATA_REQUEST) {
            CHECK(associated_request && !memcmp(mh.header.source, identity.own_ieee, 8));
            extracted = 1;
        } else CHECK(command.identifier == MAC_COMMAND_BEACON_REQUEST);
        return;
    }
    CHECK(!pending);
    open_device(body, length, &aps_secured);
    CHECK(peer_in.nwk.source == 0x5678);
    CHECK(!mh.header.destination[0] && !mh.header.destination[1]);
    CHECK(!!(mh.header.flags & MAC_FLAG_ACK_REQUEST) == (peer_in.nwk.destination < 0xfffbu));
    if (peer_in.nwk.type) {
        if (peer_in.payload[0] == 4) {
            CHECK(peer_in.length == 2 && !peer_in.payload[1] &&
                peer_in.nwk.destination == 0xfffdu && peer_in.nwk.radius == 1);
            CHECK(peer_in.nwk.flags & NWK_FLAG_SOURCE_IEEE);
            CHECK(security_keys_status(&status) == SECURITY_KEYS_OK && status.phase == SECURITY_KEYS_LEFT);
            CHECK(!device.transport.ready); leave_sent = 1;
            return;
        }
        CHECK(peer_in.length == 3 && peer_in.payload[0] == 0x0b && peer_in.payload[1] == 1 && !peer_in.payload[2]);
        CHECK(peer_in.nwk.radius == 1 &&
            (peer_in.nwk.flags & (NWK_FLAG_SOURCE_IEEE | NWK_FLAG_DESTINATION_IEEE)) ==
            (NWK_FLAG_SOURCE_IEEE | NWK_FLAG_DESTINATION_IEEE));
        CHECK(!memcmp(peer_in.nwk.source_ieee, identity.own_ieee, 8));
        CHECK(!memcmp(peer_in.nwk.destination_ieee, identity.tc_ieee, 8));
        CHECK(!!(peer_in.nwk.flags & NWK_FLAG_END_DEVICE_INITIATOR) == !!parent_set);
        if (mute_timeout) { muted++; return; }
        base_peer(1, 0, 0);
        peer_out.nwk.flags = NWK_FLAG_SOURCE_IEEE | NWK_FLAG_DESTINATION_IEEE;
        memcpy(peer_out.nwk.source_ieee, identity.tc_ieee, 8);
        memcpy(peer_out.nwk.destination_ieee, identity.own_ieee, 8);
        peer_out.length = 3; peer_out.payload[0] = 0x0c; peer_out.payload[2] = test_case == 4 ? 1 : 2;
        seal_peer(0, 0, NULL, 1);
        return;
    }
    if (peer_in.aps.type == ED_APS_ACK) return;
    if (peer_in.aps.type == ED_APS_COMMAND) {
        if (peer_in.payload[0] == 8) {
            CHECK(announced && described && aps_secured && peer_in.length == 2 && peer_in.payload[1] == 4);
            requested = 1;
            base_peer(0, ED_APS_COMMAND, 0);
            peer_out.payload[0] = 5; peer_out.payload[1] = 4; peer_out.length = 34;
            memcpy(peer_out.payload+2, link_b, 16);
            memcpy(peer_out.payload+18, identity.own_ieee, 8);
            memcpy(peer_out.payload+26, identity.tc_ieee, 8);
            CHECK(zigbee_key_hash(link_a, ZIGBEE_HASH_LOAD, effective, 1000, 1000, &hash_info) == ZIGBEE_MMO_OK);
            seal_peer(1, 3, effective, 1);
        } else {
            CHECK(requested && !aps_secured && peer_in.length == 26 &&
                peer_in.payload[0] == 15 && peer_in.payload[1] == 4);
            CHECK(!memcmp(peer_in.payload+2, identity.own_ieee, 8));
            CHECK(zigbee_key_hash(link_b, ZIGBEE_HASH_VERIFY, expected, 1000, 1000, &hash_info) == ZIGBEE_MMO_OK);
            CHECK(!memcmp(peer_in.payload+10, expected, 16));
            verified = 1;
            base_peer(0, ED_APS_COMMAND, 0);
            peer_out.length = 11; peer_out.payload[0] = 16; peer_out.payload[2] = 4;
            memcpy(peer_out.payload+3, identity.own_ieee, 8);
            seal_peer(1, 0, link_b, 1);
        }
    } else if (!peer_in.aps.profile_id && peer_in.aps.cluster_id == 0x0013u) {
        CHECK(initial_key && !requested && !verified && peer_in.nwk.destination == 0xfffdu);
        CHECK(peer_in.length == 12 && peer_in.payload[1] == 0x78 && peer_in.payload[2] == 0x56);
        CHECK(!memcmp(peer_in.payload+3, identity.own_ieee, 8) && peer_in.payload[11] == 0x88);
        CHECK(!device.transport.ready); announced = 1;
    } else if (!peer_in.aps.profile_id && peer_in.aps.cluster_id == 2) {
        CHECK(announced && !requested && peer_in.length == 3 && !peer_in.payload[1] && !peer_in.payload[2]);
        memset(&descriptor, 0, sizeof(descriptor));
        descriptor.sequence = peer_in.payload[0];
        descriptor.descriptor.frequency_band = 8; descriptor.descriptor.mac_capability = 0x8e;
        descriptor.descriptor.max_buffer = 127; descriptor.descriptor.max_incoming = 64;
        descriptor.descriptor.max_outgoing = 64; descriptor.descriptor.stack_revision = 22;
        descriptor.descriptor.manufacturer = 0x1234;
        base_peer(0, 0, 0x8002u);
        CHECK(zdo_node_rsp_encode(&descriptor, peer_out.payload, sizeof(peer_out.payload), &peer_out.length) == ZDO_NODE_OK);
        seal_peer(0, 0, NULL, 1);
    } else if (!peer_in.aps.profile_id && peer_in.aps.cluster_id == 0x0036u) {
        CHECK(verified && parent_set && peer_in.nwk.destination == 0xfffcu && peer_in.length == 3);
        CHECK(peer_in.payload[1] == 180 && peer_in.payload[2] == 1);
        CHECK(!device.transport.ready); permit = 1;
    } else if (expected_reply_cluster) {
        CHECK(peer_in.aps.cluster_id == expected_reply_cluster && !peer_in.aps.profile_id);
        CHECK(peer_in.length == expected_reply_length && peer_in.payload[1] == expected_reply_status);
        if (expected_reply_cluster == 0x8000u && !expected_reply_status) {
            CHECK(!memcmp(peer_in.payload+2, identity.own_ieee, 8));
            CHECK(peer_in.payload[10] == 0x78 && peer_in.payload[11] == 0x56);
        }
        if (expected_reply_length == 13) CHECK(!peer_in.payload[12]);
        reply_seen = 1;
    } else {
        CHECK(permit && device.phase == BDB_JOIN_READY && peer_in.aps.profile_id == 0x0104);
        if (wrap_mode) {
            CHECK(!(peer_in.aps.flags & APS_FLAG_ACK_REQUEST));
            app_counter = peer_in.aps.counter;
            return;
        }
        CHECK(peer_in.aps.flags & APS_FLAG_ACK_REQUEST);
        if (app_sends) CHECK(peer_in.aps.counter == app_counter);
        app_counter = peer_in.aps.counter; app_sends++;
        if (drop_all_acks || !dropped_ack) { dropped_ack = 1; return; }
        base_peer(0, ED_APS_ACK, peer_in.aps.cluster_id);
        peer_out.aps.counter = peer_in.aps.counter;
        peer_out.aps.profile_id = peer_in.aps.profile_id;
        peer_out.aps.destination_endpoint = peer_in.aps.source_endpoint;
        peer_out.aps.source_endpoint = peer_in.aps.destination_endpoint;
        seal_peer(aps_secured, 0, link_b, 1);
    }
}

static void source_event(mac_tx_event_t *source)
{
    memset(source, 0, sizeof(*source));
    source->generation = transmitter.generation;
    source->retry = transmitter.retries; source->nb = transmitter.nb;
    if (transmitter.phase == MAC_TX_DRAW_WAIT) source->kind = MAC_TX_EVENT_RANDOM;
    else if (transmitter.phase == MAC_TX_RADIO) {
        if (phy_busy) { source->kind = MAC_TX_EVENT_BUSY; now = transmitter.at+8u; busy_events++; }
        else {
            source->kind = MAC_TX_EVENT_SENT; now = transmitter.at+24u+2u*transmitter.length;
            if (phy_lose) lost_frames++; else observe_transmission();
        }
    } else if (transmitter.phase == MAC_TX_ACK_WAIT && phy_lose) {
        now = transmitter.tx_end+MAC_TX_ACK_SYMBOLS;
    } else if (transmitter.phase == MAC_TX_ACK_WAIT) {
        source->kind = MAC_TX_EVENT_ACK; now = transmitter.tx_end+34u;
        ack[0] = device.phase == BDB_JOIN_ASSOCIATING ? 0x12 : 2;
        ack[1] = 0; ack[2] = transmitter.frame[2];
        source->bytes = ack; source->length = 3;
    } else if (transmitter.phase == MAC_TX_STOPPING) {
        if (test_case == 5 && device.phase == BDB_JOIN_PERMIT) now = transmitter.stop_at;
        else source->kind = MAC_TX_EVENT_QUIESCED;
    }
    source->stamp = now;
}

static void drive(void)
{
    const bdb_join_event_t *input = NULL;
    memset(&event, 0, sizeof(event));
    if (device.phase == BDB_JOIN_SCANNING) {
        event.kind = BDB_JOIN_EVENT_SCAN; input = &event;
        event.scan.generation = device.scan.generation; event.scan.token = device.scan.token;
        event.scan.state = action.scan.state;
        if (action.scan.kind == MAC_SCAN_ACTION_CONFIG) event.scan.kind = MAC_SCAN_EVENT_CONFIGURED;
        else if (action.scan.kind == MAC_SCAN_ACTION_TX) {
            source_event(&event.tx);
            event.scan.kind = MAC_SCAN_EVENT_TX;
            event.scan.tx_result = mac_tx_step(&transmitter, now, event.tx.kind ? &event.tx : NULL, &granted);
        } else if (action.scan.kind == MAC_SCAN_ACTION_RECEIVE) event.scan.kind = MAC_SCAN_EVENT_OPENED;
        else if (action.scan.kind == MAC_SCAN_ACTION_RESTORE) event.scan.kind = MAC_SCAN_EVENT_RESTORED;
        else if (device.scan.phase == MAC_SCAN_RX) {
            if (!scan_beacon) {
                event.scan.kind = MAC_SCAN_EVENT_BEACON; event.scan.body = beacon;
                event.scan.length = beacon_length; event.scan.crc_valid = 1; scan_beacon = 1; now++;
            } else { event.scan.kind = MAC_SCAN_EVENT_CLOSED; now = device.scan.window_end; }
        }
        event.scan.stamp = now;
        if (!event.scan.kind) input = NULL;
    } else if (device.phase == BDB_JOIN_ASSOCIATING) {
        mac_join_event_t *j = &event.association;
        event.kind = BDB_JOIN_EVENT_ASSOCIATION; input = &event;
        j->epoch = device.epoch; j->generation = device.association.generation;
        j->token = action.association.token;
        if (action.association.kind == MAC_JOIN_ACTION_PREPARE ||
            action.association.kind == MAC_JOIN_ACTION_RECEIVE) j->kind = MAC_JOIN_PREPARED;
        else if (action.association.kind == MAC_JOIN_ACTION_TX) {
            j->kind = MAC_JOIN_TX; source_event(&j->source); j->crc_valid = 1;
            j->tx_result = mac_tx_step(&transmitter, now, j->source.kind ? &j->source : NULL, &granted);
        } else if (action.association.kind == MAC_JOIN_ACTION_RADIO || device.association.phase == MAC_JOIN_REQUEST) {
            source_event(&j->source); j->kind = j->source.kind ? MAC_JOIN_SOURCE : 0;
            j->crc_valid = 1;
        } else if (action.association.kind == MAC_JOIN_ACTION_CLOSE) {
            now += 100; j->kind = MAC_JOIN_CLOSED; j->through = now;
        } else if (action.association.kind == MAC_JOIN_ACTION_RESTORE) {
            if ((uint32_t)(now-transmitter.ready_at) >= MAC_TX_HALF) now = transmitter.ready_at;
            j->kind = MAC_JOIN_RESTORED;
        } else if (device.association.phase == MAC_JOIN_WAIT) now = device.association.wait_until;
        else if (device.association.phase == MAC_JOIN_EXTRACT &&
                 device.association.poll.control.phase == MAC_POLL_RECEIVE) {
            CHECK(extracted);
            now = device.association.poll.control.ack_end+100u;
            j->kind = MAC_JOIN_FRAME; j->serial = 1; j->crc_valid = 1;
            j->channel = 15; j->body = response; j->length = response_length;
        }
        j->stamp = now;
        if (!j->kind) input = NULL;
    } else if (action.kind == BDB_JOIN_ACTION_INSTALL) {
        event.kind = BDB_JOIN_EVENT_INSTALLED; event.epoch = action.epoch;
        event.token = action.token; event.installed = action.install; input = &event;
        CHECK(event.installed.address == 0x5678 && !device.transport.ready);
    } else if (device.transport.active) {
        source_event(&event.tx);
        if (event.tx.kind) { event.kind = BDB_JOIN_EVENT_TX; input = &event; }
    } else if (device.transport.waiting) now = device.transport.deadline;
    else if (test_case && (device.phase == BDB_JOIN_WAIT_KEY ||
        device.phase == BDB_JOIN_WAIT_TC || device.phase == BDB_JOIN_WAIT_CONFIRM)) now = device.until;
    CHECK(bdb_join_step(&device, now, input, &action) == BDB_JOIN_OK);
}

static void initial_transport(void)
{
    zigbee_mmo_info_t info;
    base_peer(0, ED_APS_COMMAND, 0);
    peer_out.length = 35; peer_out.payload[0] = 5; peer_out.payload[1] = 1;
    memcpy(peer_out.payload+2, network, 16); peer_out.payload[18] = 0;
    memcpy(peer_out.payload+19, identity.own_ieee, 8);
    memcpy(peer_out.payload+27, identity.tc_ieee, 8);
    CHECK(zigbee_key_hash(link_a, 0, effective, 1000, 1000, &info) == ZIGBEE_MMO_OK);
    seal_peer(1, 2, effective, 0);
    initial_key = 1;
}

static void deliver(void)
{
    bdb_join_result_t rc;
    uint8_t early = device.transport.active;
    if (peer_out.aps.cluster_id == 0x8002u) {
        peer_out.payload[0] ^= 1;
        seal_peer(0, 0, NULL, 1);
        CHECK(bdb_join_receive(&device, mac_body, mac_length, 1, now) == BDB_JOIN_OK);
        CHECK(bdb_join_step(&device, now, NULL, &action) == BDB_JOIN_OK);
        CHECK(device.phase == BDB_JOIN_NODE && device.zdo.query == ZDO_RUNTIME_NODE &&
            device.zdo.result == ZDO_RUNTIME_NO_EVENT && !device.transport.ready);
        peer_out.payload[0] ^= 1; peer_out.aps.counter = aps_counter++;
        peer_out.nwk.sequence = nwk_sequence++;
        seal_peer(0, 0, NULL, 1);
    } else if (peer_out.aps.type == ED_APS_COMMAND && peer_out.payload[0] == 16) {
        peer_out.payload[1] = 1;
        seal_peer(1, 0, link_b, 1);
        CHECK(bdb_join_receive(&device, mac_body, mac_length, 1, now) == BDB_JOIN_SECURITY);
        CHECK(security_keys_status(&status) == SECURITY_KEYS_OK && status.phase == SECURITY_KEYS_WAIT_CONFIRM);
        CHECK(!device.transport.ready);
        peer_out.payload[1] = 0;
        seal_peer(1, 0, link_a, 1);
        CHECK(bdb_join_receive(&device, mac_body, mac_length, 1, now) == BDB_JOIN_SECURITY);
        CHECK(security_keys_status(&status) == SECURITY_KEYS_OK && status.phase == SECURITY_KEYS_WAIT_CONFIRM);
        seal_peer(1, 0, link_b, 1);
    } else if (peer_out.aps.type == ED_APS_ACK) {
        uint8_t counter = peer_out.aps.counter;
        peer_out.aps.counter++;
        seal_peer(1, 0, link_b, 1);
        CHECK(bdb_join_receive(&device, mac_body, mac_length, 1, now) == BDB_JOIN_IGNORED);
        CHECK(!device.transport.seen_ack);
        peer_out.aps.counter = counter;
        seal_peer(1, 0, link_b, 1);
    }
    memcpy(saved, mac_body, mac_length);
    CHECK(bdb_join_receive(&device, mac_body, mac_length, 0, now) == BDB_JOIN_MALFORMED);
    mac_body[mac_length-1] ^= 0x80;
    CHECK(bdb_join_receive(&device, mac_body, mac_length, 1, now) == BDB_JOIN_SECURITY);
    memcpy(mac_body, saved, mac_length);
    rc = bdb_join_receive(&device, mac_body, mac_length, 1, now);
    CHECK(rc == BDB_JOIN_OK);
    if (peer_out.aps.cluster_id == 0x8002u) described = 1;
    if (peer_out.nwk.type && peer_out.payload[0] == 0x0c) parent_set = 1;
    pending = 0;
    CHECK(bdb_join_step(&device, now, NULL, &action) == BDB_JOIN_OK);
    if (early) CHECK(!device.application_done && transmitter.phase != MAC_TX_IDLE);
    rc = bdb_join_receive(&device, saved, mac_length, 1, now);
    if (device.phase >= BDB_JOIN_UPDATING) CHECK(rc == BDB_JOIN_STATE);
    else CHECK(rc == BDB_JOIN_SECURITY || rc == BDB_JOIN_FULL);
}

static void setup(void)
{
    mac_header_t mh;
    uint8_t payload[19], i;
    zigbee_mmo_info_t info;
    announced = described = requested = verified = parent_set = permit = 0;
    scan_beacon = associated_request = extracted = initial_key = pending = 0;
    app_sends = app_counter = dropped_ack = leave_sent = reply_seen = wrap_mode = drop_all_acks = 0;
    aps_counter = nwk_sequence = 0; expected_reply_cluster = 0;
    phy_busy = phy_lose = mute_timeout = 0; lost_frames = muted = busy_events = 0;
    aux_source = NULL;
    last_device_nwk = last_device_aps = 0; transmissions = 0;
    peer_pan = identity.pan;
    security_joint_reset(1);
    CHECK(security_keys_open() == SECURITY_KEYS_EMPTY);
    CHECK(install_code_derive(install_code, sizeof(install_code), link_a, 1000, 1000, &info) == ZIGBEE_MMO_OK);
    for (i = 0; i < 16; i++) { link_b[i] = (uint8_t)(0xa0u+i); network[i] = (uint8_t)(0x30u+i); }
    CHECK(security_keys_provision(&identity, install_code, 1, 1, &limits, 2000) == SECURITY_KEYS_OK);
    CHECK(security_keys_status(&status) == SECURITY_KEYS_OK && status.phase == SECURITY_KEYS_PROVISIONED);
    memset(&config, 0, sizeof(config));
    config.scan.channels = 1UL << 15; config.scan.lifetime = 100000; config.scan.work = 512;
    config.scan.saved.pan = 0xffff; config.scan.saved.channel = 11;
    config.association.saved.pan = 0xffff; config.association.saved.channel = 11;
    config.association.response_wait = 2; config.association.capability = 0x88;
    config.association.extraction.epoch = 1; config.association.extraction.frame_wait = 300;
    config.association.extraction.lifetime = 100000; config.association.extraction.work = 512;
    config.association.extraction.pan = identity.pan; config.association.extraction.channel = 15;
    config.association.extraction.local_mode = config.association.extraction.coordinator_mode = 3;
    memcpy(config.association.extraction.local, identity.own_ieee, 8);
    memcpy(config.association.extraction.coordinator, identity.tc_ieee, 8);
    config.crypto = limits; config.nv_polls = 2000; config.profile = 0x0104;
    config.endpoint = 1; config.link_cost = 1; config.ack_wait = NWK_APS_ACK_WAIT;
    config.broadcast_time = 1000000;
    config.descriptor.logical_type = 2; config.descriptor.frequency_band = 8;
    config.descriptor.mac_capability = 0x88; config.descriptor.max_buffer = 127;
    config.descriptor.max_incoming = config.descriptor.max_outgoing = 64;
    config.descriptor.stack_revision = 22;
    memset(&mh, 0, sizeof(mh)); mh.type = MAC_FRAME_BEACON; mh.source_mode = 3;
    mh.source_pan = identity.pan; memcpy(mh.source, identity.tc_ieee, 8);
    memset(payload, 0, sizeof(payload)); payload[0] = 0xff; payload[1] = 0xcf;
    payload[5] = 0x22; payload[6] = 0x84; memcpy(payload+7, identity.extended_pan, 8);
    payload[15] = payload[16] = payload[17] = 255;
    CHECK(mac_frame_encode(&mh, payload, sizeof(payload), beacon, sizeof(beacon), &beacon_length) == MAC_CODEC_OK);
    mh.type = MAC_FRAME_COMMAND; mh.flags = 0x60; mh.destination_mode = 3;
    mh.destination_pan = identity.pan; mh.sequence = 0x91;
    memcpy(mh.destination, identity.own_ieee, 8);
    payload[0] = 2; payload[1] = 0x78; payload[2] = 0x56; payload[3] = 0;
    CHECK(mac_frame_encode(&mh, payload, 4, response, sizeof(response), &response_length) == MAC_CODEC_OK);
    now = initial_now; peer_nwk_counter = peer_aps_counter = 1;
    CHECK(mac_tx_init(&transmitter, 255, now) == MAC_TX_OK);
    CHECK(bdb_join_init(&device, now) == BDB_JOIN_OK);
    CHECK(bdb_join_start(&device, &transmitter, &config, now) == BDB_JOIN_OK);
    memset(&action, 0, sizeof(action));
}

static void commission(void)
{
    for (iterations = 0; iterations < 512 &&
        device.phase != BDB_JOIN_READY && device.phase < BDB_JOIN_FAILED; iterations++) {
        drive();
        if (device.phase == BDB_JOIN_WAIT_KEY && !initial_key && test_case != 1) initial_transport();
        if (pending && !device.transport.active && device.phase >= BDB_JOIN_WAIT_KEY) {
            if ((test_case == 2 && peer_out.aps.type == ED_APS_COMMAND && peer_out.payload[0] == 5 &&
                peer_out.payload[1] == 4) ||
                (test_case == 3 && peer_out.aps.type == ED_APS_COMMAND && peer_out.payload[0] == 16))
                pending = 0;
            else {
                if ((test_case == 6 && peer_out.aps.type == ED_APS_COMMAND && peer_out.payload[0] == 5 &&
                     peer_out.payload[1] == 1) ||
                    (test_case == 7 && peer_out.aps.type == ED_APS_COMMAND && peer_out.payload[0] == 5 &&
                     peer_out.payload[1] == 4) ||
                    (test_case == 8 && peer_out.aps.type == ED_APS_COMMAND && peer_out.payload[0] == 16))
                    now = device.until;
                deliver();
            }
        }
    }
    CHECK(iterations < 512);
}

static void ready(void)
{
    test_case = 0; setup(); commission();
    CHECK(device.phase == BDB_JOIN_READY && device.transport.ready && device.member);
}

static void drain(void)
{
    for (iterations = 0; iterations < 256 && device.phase < BDB_JOIN_FAILED &&
        (device.transport.active || device.transport.queued || device.transport.reply ||
         device.transport.completed || device.transport.stopping || device.zdo.response_pending ||
         device.zdo.response_tx || device.phase == BDB_JOIN_ABORTING || device.phase == BDB_JOIN_LEAVING);
         iterations++) drive();
    CHECK(iterations < 256);
}

static void retained(uint8_t phase)
{
    ed_packet_t rejoin = {0};
    zigbee_security_meta_t meta;
    uint8_t output[64], written = 0xa5;
    unsigned before;
    security_joint_reset(0);
    CHECK(security_keys_open() == SECURITY_KEYS_OK);
    CHECK(security_keys_status(&status) == SECURITY_KEYS_OK && status.phase == phase);
    CHECK(mac_tx_init(&transmitter, 17, now) == MAC_TX_OK);
    CHECK(bdb_join_init(&device, now) == BDB_JOIN_OK);
    CHECK(bdb_join_start(&device, &transmitter, &config, now) == BDB_JOIN_RECOVERY_REQUIRED);
    CHECK(!device.member && !device.transport.ready);
    if (phase != SECURITY_KEYS_VERIFIED && phase != SECURITY_KEYS_LEFT) return;
    rejoin.nwk.type = ED_NWK_COMMAND; rejoin.nwk.version = 2; rejoin.nwk.radius = 1;
    rejoin.nwk.source = status.config.address;
    rejoin.nwk.flags = NWK_FLAG_SOURCE_IEEE | NWK_FLAG_DESTINATION_IEEE;
    memcpy(rejoin.nwk.source_ieee, identity.own_ieee, 8);
    memcpy(rejoin.nwk.destination_ieee, identity.tc_ieee, 8);
    rejoin.length = 2; rejoin.payload[0] = 6; rejoin.payload[1] = 0x88;
    memset(output, 0xa5, sizeof(output)); before = security_joint_flash_commands();
    if (phase == SECURITY_KEYS_VERIFIED) {
        CHECK(security_keys_send(&rejoin, 0, output, sizeof(output), &written, &limits, 2000) == SECURITY_KEYS_OK);
        CHECK(ed_wire_inspect(0, output, written, &meta) == ZIGBEE_SECURITY_OK);
        CHECK(meta.counter > last_device_nwk);
        CHECK(security_keys_status(&status) == SECURITY_KEYS_OK && status.phase == SECURITY_KEYS_REJOINING);
    } else {
        CHECK(security_keys_send(&rejoin, 0, output, sizeof(output), &written, &limits, 2000) == SECURITY_KEYS_CONTEXT);
        CHECK(written == 0xa5 && security_joint_flash_commands() == before);
        for (before = 0; before < sizeof(output); before++) CHECK(output[before] == 0xa5);
    }
}

static void address_request(uint8_t ar)
{
    base_peer(0, 0, 0);
    peer_out.aps.flags = ar ? APS_FLAG_ACK_REQUEST : 0;
    peer_out.payload[0] = 0x77; memcpy(peer_out.payload+1, identity.own_ieee, 8);
    peer_out.length = 11; seal_peer(0, 0, NULL, 1);
    expected_reply_cluster = 0x8000u; expected_reply_status = 0; expected_reply_length = 12;
}

static void timeout_response(void)
{
    base_peer(1, 0, 0);
    peer_out.nwk.flags = NWK_FLAG_SOURCE_IEEE | NWK_FLAG_DESTINATION_IEEE;
    memcpy(peer_out.nwk.source_ieee, identity.tc_ieee, 8);
    memcpy(peer_out.nwk.destination_ieee, identity.own_ieee, 8);
    peer_out.length = 3; peer_out.payload[0] = 0x0c; peer_out.payload[2] = 2;
    seal_peer(0, 0, NULL, 1);
}

static void update_network(void)
{
    base_peer(1, 0, 0);
    peer_out.nwk.destination = 0xffffu; peer_out.nwk.flags = NWK_FLAG_SOURCE_IEEE;
    memcpy(peer_out.nwk.source_ieee, identity.tc_ieee, 8);
    peer_out.payload[0] = 10; peer_out.payload[1] = 1;
    memcpy(peer_out.payload+2, identity.extended_pan, 8);
    peer_out.payload[10] = 1; peer_out.payload[11] = 0x45; peer_out.payload[12] = 0x23;
    peer_out.length = 13; seal_peer(0, 0, NULL, 1);
}

static void keepalive_sent(void)
{
    now = device.keepalive;
    for (iterations = 0; iterations < 128 &&
        !(device.zdo.query == ZDO_RUNTIME_PARENT && device.zdo.tx_done); iterations++) drive();
    CHECK(iterations < 128 && device.phase == BDB_JOIN_READY);
}

static void keepalive_success(void)
{
    uint32_t previous = device.keepalive;
    mute_timeout = phy_busy = phy_lose = 0; expected_reply_cluster = 0;
    if ((uint32_t)(now-previous) >= MAC_TX_HALF) now = previous;
    for (iterations = 0; iterations < 128 &&
        (device.keepalive == previous || device.zdo.query); iterations++) {
        drive();
        if (pending && !device.transport.active) deliver();
    }
    CHECK(iterations < 128 && device.phase == BDB_JOIN_READY && device.transport.ready);
    CHECK(!device.attempts && !leave_sent && !device.result);
}

static void operational_failures(void)
{
    uint8_t attempt, sustained;
    ready(); mute_timeout = 1; keepalive_sent();
    CHECK(muted == 1 && device.attempts == 1);
    now = device.zdo.deadline; drive();
    CHECK(device.phase == BDB_JOIN_READY && device.attempts == 2);
    keepalive_success(); retained(SECURITY_KEYS_VERIFIED);

    ready(); mute_timeout = 1;
    for (attempt = 1; attempt <= BDB_JOIN_ATTEMPTS; attempt++) {
        keepalive_sent();
        CHECK(device.attempts == attempt && muted == attempt);
        now = device.zdo.deadline; drive();
    }
    drain();
    CHECK(device.phase == BDB_JOIN_FAILED && device.result == BDB_JOIN_PARENT_FAILED);
    CHECK(!device.member && !device.transport.ready && !leave_sent && !device.cleanup_error);
    retained(SECURITY_KEYS_VERIFIED);

    for (sustained = 0; sustained < 2; sustained++) {
        ready(); phy_busy = 1; now = device.keepalive;
        for (iterations = 0; iterations < 256 && busy_events < 5u*(sustained ? BDB_JOIN_ATTEMPTS : 1u);
             iterations++) drive();
        CHECK(iterations < 256);
        if (sustained) {
            drain();
            CHECK(device.phase == BDB_JOIN_FAILED && device.result == BDB_JOIN_PARENT_FAILED && !leave_sent);
        } else keepalive_success();
        retained(SECURITY_KEYS_VERIFIED);

        ready(); address_request(1);
        CHECK(bdb_join_receive(&device, mac_body, mac_length, 1, now) == BDB_JOIN_OK);
        pending = 0; phy_busy = 1;
        for (iterations = 0; iterations < 128 && device.transport.reply_result != MAC_TX_CHANNEL_ACCESS;
             iterations++) drive();
        CHECK(iterations < 128 && busy_events == 5 && device.phase == BDB_JOIN_READY && !reply_seen);
        if (!sustained) phy_busy = 0;
        drain();
        CHECK(device.phase == BDB_JOIN_READY && device.transport.ready && !leave_sent);
        CHECK(reply_seen == !sustained);
        CHECK(device.zdo.response_result == (sustained ? NWK_APS_RADIO : NWK_APS_OK));
        keepalive_success(); retained(SECURITY_KEYS_VERIFIED);
    }

    ready(); address_request(0);
    CHECK(bdb_join_receive(&device, mac_body, mac_length, 1, now) == BDB_JOIN_OK);
    pending = 0; phy_lose = 1; drive(); drain();
    CHECK(lost_frames == 4 && !reply_seen && device.zdo.response_result == NWK_APS_RADIO);
    CHECK(device.phase == BDB_JOIN_READY && device.transport.ready && !leave_sent);
    keepalive_success(); retained(SECURITY_KEYS_VERIFIED);
}

static void update_pending_query(void)
{
    uint32_t old_deadline;
    ready(); mute_timeout = 1; keepalive_sent(); old_deadline = device.zdo.deadline;
    update_network();
    CHECK(bdb_join_receive(&device, mac_body, mac_length, 1, now) == BDB_JOIN_OK);
    timeout_response(); pending = 0;
    CHECK(bdb_join_receive(&device, mac_body, mac_length, 1, now) == BDB_JOIN_MALFORMED);
    drive();
    CHECK(bdb_join_receive(&device, mac_body, mac_length, 1, now) == BDB_JOIN_STATE);
    peer_pan = 0x2345; timeout_response(); pending = 0;
    CHECK(bdb_join_receive(&device, mac_body, mac_length, 1, now) == BDB_JOIN_STATE);
    for (iterations = 0; iterations < 16 && device.phase != BDB_JOIN_READY; iterations++) drive();
    CHECK(iterations < 16 && device.transport.ready && !device.zdo.query && !device.attempts);
    CHECK(device.zdo.result == ZDO_RUNTIME_CANCELLED && !device.cleanup_error);
    now = old_deadline; drive();
    CHECK(device.phase == BDB_JOIN_READY && !device.zdo.query && !leave_sent);
    keepalive_success(); retained(SECURITY_KEYS_VERIFIED);
}

static void response_backpressure(void)
{
    ed_packet_t application = {0};
    uint8_t ar, result;
    unsigned counter;
    application.nwk.version = 2; application.nwk.radius = 30;
    application.aps.source_endpoint = application.aps.destination_endpoint = 1;
    application.aps.profile_id = 0x0104; application.length = 1;
    for (ar = 0; ar < 2; ar++) {
        ready(); wrap_mode = 1;
        for (counter = device.transport.next_aps; counter < 256u; counter++) {
            CHECK(bdb_join_send(&device, &application, 0, now) == BDB_JOIN_OK);
            drain();
            CHECK(bdb_join_confirm(&device, &result) == BDB_JOIN_OK && result == NWK_APS_OK);
            CHECK(app_counter == (uint8_t)counter);
        }
        CHECK(device.transport.wrap_wait && !device.transport.next_aps);
        wrap_mode = 0; mute_timeout = 1; keepalive_sent();
        address_request(ar);
        CHECK(bdb_join_receive(&device, mac_body, mac_length, 1, now) == BDB_JOIN_OK);
        pending = 0; drive(); drain();
        CHECK(device.phase == BDB_JOIN_READY && !reply_seen && !device.zdo.response_pending);
        CHECK(device.zdo.response_result == NWK_APS_EXHAUSTED && device.transport.wrap_wait);
        CHECK(device.zdo.query == ZDO_RUNTIME_PARENT && !leave_sent);
        timeout_response(); deliver();
        CHECK(!device.zdo.query && device.phase == BDB_JOIN_READY && device.transport.ready);
        CHECK(device.transport.wrap_wait && !device.transport.next_aps && !device.attempts);
        retained(SECURITY_KEYS_VERIFIED);
    }

    ready(); now = device.keepalive; drive();
    CHECK(device.zdo.query_tx && device.transport.queued && !device.transport.active);
    address_request(0);
    CHECK(bdb_join_receive(&device, mac_body, mac_length, 1, now) == BDB_JOIN_OK);
    pending = 0; drive();
    CHECK(device.zdo.response_pending);
    for (iterations = 0; iterations < 64 && !pending; iterations++) drive();
    CHECK(iterations < 64 && device.transport.active && device.zdo.response_pending);
    deliver();
    CHECK(device.zdo.result == ZDO_RUNTIME_OK && device.zdo.response_pending);
    drain();
    CHECK(reply_seen && !device.zdo.query && device.phase == BDB_JOIN_READY && !leave_sent);
}

static uint8_t tc_key_pending(void)
{
    return pending && peer_out.aps.type == ED_APS_COMMAND &&
        peer_out.payload[0] == 5 && peer_out.payload[1] == 4;
}

static void work_and_deadlines(void)
{
    uint32_t start;
    unsigned poll, before;
    uint8_t which;
    for (which = 0; which < 4; which++) {
        test_case = 0; initial_now = which & 2u ? 0xffff0000UL : 100; setup();
        for (iterations = 0; iterations < 512; iterations++) {
            if ((!(which & 1u) && device.phase == BDB_JOIN_WAIT_KEY) ||
                ((which & 1u) && device.phase == BDB_JOIN_WAIT_TC && tc_key_pending() && !device.transport.active)) break;
            drive();
            if ((which & 1u) && device.phase == BDB_JOIN_WAIT_KEY && !initial_key) initial_transport();
            if (pending && !device.transport.active && device.phase >= BDB_JOIN_WAIT_KEY && !tc_key_pending()) deliver();
        }
        CHECK(iterations < 512);
        start = now;
        for (poll = 0; (uint32_t)(now+62u-start) < (which & 1u ? 281250UL : 312500UL); poll++) {
            now += 62u;
            CHECK(bdb_join_step(&device, now, NULL, &action) == BDB_JOIN_OK);
            CHECK(device.phase == (which & 1u ? BDB_JOIN_WAIT_TC : BDB_JOIN_WAIT_KEY));
        }
        CHECK(poll > BDB_JOIN_STALL_STEPS && device.steps == 1);
        now = start+(which & 1u ? 281250UL : 312500UL);
        CHECK((uint32_t)(device.until-now) < MAC_TX_HALF);
        if (!(which & 1u)) initial_transport();
        deliver(); commission();
        CHECK(device.phase == BDB_JOIN_READY && device.transport.ready && !leave_sent);
    }
    initial_now = 100;
    for (which = 0; which < 3; which++) {
        setup();
        for (iterations = 0; iterations < 512; iterations++) {
            if (which == 2 ? action.kind == BDB_JOIN_ACTION_INSTALL : device.phase == BDB_JOIN_WAIT_KEY) break;
            drive();
        }
        CHECK(iterations < 512);
        memset(&event, 0, sizeof(event)); event.kind = BDB_JOIN_EVENT_SCAN;
        before = security_joint_flash_commands();
        for (poll = device.steps; poll < BDB_JOIN_STALL_STEPS; poll++)
            CHECK(bdb_join_step(&device, now, which == 1 ? &event : NULL, &action) ==
                (which == 1 ? BDB_JOIN_IGNORED : BDB_JOIN_OK));
        CHECK(device.phase == (which == 2 ? BDB_JOIN_INSTALLING : BDB_JOIN_WAIT_KEY));
        CHECK(bdb_join_step(&device, now, which == 1 ? &event : NULL, &action) ==
            (which == 1 ? BDB_JOIN_IGNORED : BDB_JOIN_OK));
        CHECK(device.result == BDB_JOIN_WORK_LIMIT && !leave_sent && !device.transport.ready);
        CHECK(device.phase == (which == 2 ? BDB_JOIN_FAULT : BDB_JOIN_FAILED));
        CHECK(security_joint_flash_commands() == before);
        retained(SECURITY_KEYS_ASSOCIATED);
    }
}

static void stopped_transport(void)
{
    uint8_t fault;
    for (fault = 0; fault < 2; fault++) {
        ready(); address_request(1);
        CHECK(bdb_join_receive(&device, mac_body, mac_length, 1, now) == BDB_JOIN_OK);
        pending = 0; drive(); drive();
        CHECK(device.transport.active && device.transport.active_ack && transmitter.phase == MAC_TX_RADIO);
        CHECK(nwk_aps_stop(&device.transport, now) == NWK_APS_OK);
        CHECK(device.transport.active && device.transport.reply && !device.transport.ready);
        CHECK(nwk_aps_step(&device.transport, now, NULL, &granted) == NWK_APS_OK);
        CHECK(granted.kind == MAC_TX_ACTION_QUIESCE && transmitter.phase == MAC_TX_STOPPING);
        CHECK(device.transport.active && device.transport.stopping);
        if (fault) {
            now = transmitter.stop_at;
            CHECK(bdb_join_step(&device, now, NULL, &action) == BDB_JOIN_OK);
            CHECK(device.phase == BDB_JOIN_FAULT && transmitter.phase == MAC_TX_FAULT);
            CHECK(device.transport.active && device.transport.reply && device.transport.stopping);
            CHECK(mac_tx_release(&transmitter) == MAC_TX_STATE);
        } else {
            source_event(&event.tx);
            CHECK(nwk_aps_step(&device.transport, now, &event.tx, &granted) == NWK_APS_OK);
            CHECK(!device.transport.active && transmitter.phase == MAC_TX_IDLE);
            CHECK(nwk_aps_step(&device.transport, now, NULL, &granted) == NWK_APS_OK);
            CHECK(!device.transport.stopping && !device.transport.reply && !device.transport.queued);
            CHECK(device.transport.reply_result == MAC_TX_CANCELLED);
            CHECK(zdo_runtime_step(&device.zdo, &device.transport, now) == ZDO_RUNTIME_DROPPED);
            CHECK(device.zdo.response_result == NWK_APS_CANCELLED && !device.transport.completed);
        }
        CHECK(!leave_sent);
        retained(SECURITY_KEYS_VERIFIED);
    }
}

static void storage_quota(void)
{
    unsigned cycle, cleanup_start;
    ready();
    CHECK(security_joint_flash_erases(0)+security_joint_flash_erases(1) == 17);
    for (cycle = 0; cycle < 17 && device.phase == BDB_JOIN_READY; cycle++) {
        uint32_t previous = device.keepalive;
        now = previous;
        for (iterations = 0; iterations < 128 && device.phase == BDB_JOIN_READY &&
            device.keepalive == previous; iterations++) {
            drive();
            if (pending && !device.transport.active && device.phase == BDB_JOIN_READY) {
                bdb_join_result_t result;
                pending = 0; result = bdb_join_receive(&device, mac_body, mac_length, 1, now);
                CHECK(result == BDB_JOIN_OK || result == BDB_JOIN_SECURITY);
            }
        }
        CHECK(iterations < 128);
    }
    CHECK(cycle == 17 && device.phase != BDB_JOIN_READY);
    CHECK(security_joint_flash_erases(0)+security_joint_flash_erases(1) == 2u*NV_RECORD_ERASE_LIMIT);
    cleanup_start = security_joint_flash_commands(); drain();
    CHECK(device.phase == BDB_JOIN_FAILED && !leave_sent && !device.transport.ready);
    CHECK(security_joint_flash_commands() == cleanup_start);
    retained(SECURITY_KEYS_VERIFIED);
}

static void announcement(uint16_t source, uint16_t address, const uint8_t *ieee)
{
    base_peer(0, 0, 0x0013u);
    peer_out.nwk.source = source; peer_out.nwk.destination = 0xfffdu; peer_out.aps.delivery_mode = 2;
    peer_out.payload[0] = 0x55; peer_out.payload[1] = (uint8_t)address;
    peer_out.payload[2] = (uint8_t)(address >> 8);
    memcpy(peer_out.payload+3, ieee, 8); peer_out.payload[11] = 0x80; peer_out.length = 12;
    aux_source = source ? ieee : NULL;
    seal_peer(0, 0, NULL, 1); aux_source = NULL;
}

static void terminal_control(void)
{
    static const uint8_t third_ieee[8] = {0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38};
    unsigned before;
    ready(); before = security_joint_flash_commands();
    announcement(0x1111u, 0x1111u, third_ieee);
    CHECK(bdb_join_receive(&device, mac_body, mac_length, 1, now) == BDB_JOIN_SECURITY);
    CHECK(device.transport.error == SECURITY_KEYS_IDENTITY);
    announcement(0x5678u, 0x5678u, third_ieee);
    CHECK(bdb_join_receive(&device, mac_body, mac_length, 1, now) == BDB_JOIN_SECURITY);
    CHECK(device.transport.error == SECURITY_KEYS_IDENTITY);
    pending = 0; drive();
    CHECK(!device.zdo.map[0].used && !device.zdo.conflict && device.phase == BDB_JOIN_READY);
    CHECK(security_joint_flash_commands() == before);
    announcement(0, 0x5678u, third_ieee);
    CHECK(bdb_join_receive(&device, mac_body, mac_length, 1, now) == BDB_JOIN_OK);
    pending = 0; drive(); drain();
    CHECK(device.phase == BDB_JOIN_FAILED && device.result == BDB_JOIN_ADDRESS_CONFLICT && leave_sent);
    retained(SECURITY_KEYS_LEFT);

    ready(); base_peer(1, 0, 0);
    peer_out.nwk.flags = NWK_FLAG_SOURCE_IEEE;
    memcpy(peer_out.nwk.source_ieee, identity.tc_ieee, 8);
    peer_out.payload[0] = 4; peer_out.payload[1] = 0x40; peer_out.length = 2;
    seal_peer(0, 0, NULL, 1);
    CHECK(bdb_join_receive(&device, mac_body, mac_length, 1, now) == BDB_JOIN_OK);
    pending = 0; drive(); drain();
    CHECK(device.phase == BDB_JOIN_FAILED && device.result == BDB_JOIN_REMOTE_LEAVE && !leave_sent);
    retained(SECURITY_KEYS_LEFT);
}

int main(void)
{
    uint8_t result, i;
    uint32_t previous, joined_at;
    ed_packet_t application = {0};
    setup();
    CHECK(bdb_join_send(&device, &application, 0, now) == BDB_JOIN_STATE);
    commission();
    CHECK(iterations < 512 && permit && verified && device.member && device.transport.ready);
    joined_at = now;
    CHECK(transmitter.phase == MAC_TX_IDLE && transmitter.generation >= 9);
    CHECK(security_keys_status(&status) == SECURITY_KEYS_OK && status.phase == SECURITY_KEYS_VERIFIED);
    CHECK(security_joint_aes_blocks() > 100 && security_joint_flash_commands() > 100);
    memset(&application, 0, sizeof(application));
    application.nwk.version = 2; application.nwk.radius = 30;
    application.aps.source_endpoint = application.aps.destination_endpoint = 1;
    application.aps.profile_id = 0x0104; application.aps.flags = APS_FLAG_ACK_REQUEST;
    application.length = 1; application.payload[0] = 0x69;
    CHECK(bdb_join_send(&device, &application, 1, now) == BDB_JOIN_OK);
    CHECK(bdb_join_send(&device, &application, 1, now) == BDB_JOIN_FULL);
    CHECK(bdb_join_confirm(&device, &result) == BDB_JOIN_STATE);
    base_peer(0, ED_APS_ACK, 0);
    peer_out.aps.counter = device.transport.outgoing.aps.counter;
    peer_out.aps.source_endpoint = peer_out.aps.destination_endpoint = 1;
    peer_out.aps.profile_id = 0x0104;
    seal_peer(1, 0, link_b, 1);
    CHECK(bdb_join_receive(&device, mac_body, mac_length, 1, now) == BDB_JOIN_IGNORED);
    CHECK(!device.transport.seen_ack); pending = 0;
    for (iterations = 0; iterations < 128 && !device.application_done; iterations++) {
        drive();
        if (pending && (!device.transport.active || transmitter.phase == MAC_TX_STOPPING)) deliver();
    }
    CHECK(iterations < 128 && app_sends == 2);
    CHECK(bdb_join_confirm(&device, &result) == BDB_JOIN_OK && result == NWK_APS_OK);
    CHECK(device.transport.retries == 1);
    previous = device.keepalive; now = previous;
    for (iterations = 0; iterations < 128 && device.keepalive == previous; iterations++) {
        drive();
        if (pending && !device.transport.active) deliver();
    }
    CHECK(iterations < 128 && device.phase == BDB_JOIN_READY && device.zdo.parent_known);
    now = joined_at+device.transport.duplicate_time+1000;
    base_peer(0, 0, 0x0006);
    peer_out.nwk.destination = 0xfffdu; peer_out.aps.delivery_mode = 2;
    peer_out.aps.destination_endpoint = peer_out.aps.source_endpoint = 1;
    peer_out.aps.profile_id = 0x0104; peer_out.length = 1; peer_out.payload[0] = 0x42;
    seal_peer(0, 0, NULL, 1);
    CHECK(bdb_join_receive(&device, mac_body, mac_length, 1, now) == BDB_JOIN_OK);
    pending = 0;
    CHECK(bdb_join_step(&device, now, NULL, &action) == BDB_JOIN_OK);
    CHECK(zdo_runtime_take_application(&device.zdo, &application) == ZDO_RUNTIME_OK && application.payload[0] == 0x42);
    peer_out.aps.counter = aps_counter++;
    seal_peer(0, 0, NULL, 1);
    {
        unsigned before_flash = security_joint_flash_commands(), before_aes = security_joint_aes_blocks();
        CHECK(bdb_join_receive(&device, mac_body, mac_length, 1, now) == BDB_JOIN_IGNORED);
        CHECK(security_joint_flash_commands() == before_flash && security_joint_aes_blocks() == before_aes);
    }
    pending = 0;
    for (i = 0; i < 6; i++) {
        base_peer(0, 0, i == 5 ? 5 : i == 4 ? 1 : 0);
        peer_out.aps.flags = APS_FLAG_ACK_REQUEST;
        peer_out.payload[0] = (uint8_t)(0x80+i);
        peer_out.length = i == 5 ? 1 : i == 4 ? 5 : 11;
        if (i < 4) {
            memcpy(peer_out.payload+1, identity.own_ieee, 8);
            peer_out.payload[9] = i == 1 ? 1 : i == 2 ? 2 : 0;
            if (i == 3) peer_out.payload[1] ^= 0x80;
        } else if (i == 4) peer_out.payload[1] = peer_out.payload[2] = 0x11;
        expected_reply_cluster = peer_out.aps.cluster_id | 0x8000u;
        expected_reply_status = i == 2 ? 0x80 : i == 3 || i == 4 ? 0x81 : i == 5 ? 0x84 : 0;
        expected_reply_length = i == 1 ? 13 : i == 5 ? 2 : 12;
        reply_seen = 0;
        seal_peer(0, 0, NULL, 1);
        deliver();
        for (iterations = 0; iterations < 128 &&
            (!reply_seen || device.transport.active || device.transport.queued || device.zdo.response_tx); iterations++)
            drive();
        CHECK(iterations < 128 && reply_seen && device.phase == BDB_JOIN_READY);
        expected_reply_cluster = 0;
    }
    memset(&application, 0, sizeof(application));
    application.nwk.version = 2; application.nwk.radius = 30;
    application.aps.source_endpoint = application.aps.destination_endpoint = 1;
    application.aps.profile_id = 0x0104; application.length = 1;
    application.aps.flags = APS_FLAG_ACK_REQUEST;
    drop_all_acks = 1; app_sends = 0;
    CHECK(bdb_join_send(&device, &application, 1, now) == BDB_JOIN_OK);
    for (iterations = 0; iterations < 128 && !device.application_done; iterations++) drive();
    CHECK(iterations < 128 && app_sends == 4 && device.transport.retries == 3);
    CHECK(bdb_join_confirm(&device, &result) == BDB_JOIN_OK && result == NWK_APS_TIMEOUT);
    drop_all_acks = 0;
    base_peer(0, 0, 6);
    peer_out.aps.destination_endpoint = peer_out.aps.source_endpoint = 1;
    peer_out.aps.profile_id = 0x0104; peer_out.length = 1;
    seal_peer(0, 0, NULL, 1);
    CHECK(bdb_join_receive(&device, mac_body, mac_length, 1, now) == BDB_JOIN_OK);
    pending = 0;
    CHECK(bdb_join_step(&device, now, NULL, &action) == BDB_JOIN_OK);
    CHECK(zdo_runtime_take_application(&device.zdo, &peer_in) == ZDO_RUNTIME_OK);
    peer_out.aps.counter = aps_counter++; peer_out.nwk.sequence = nwk_sequence++;
    seal_peer(0, 0, NULL, 1);
    CHECK(bdb_join_receive(&device, mac_body, mac_length, 1, now) == BDB_JOIN_FULL);
    CHECK(!device.transport.receive_ready && !device.zdo.application_ready && device.transport.ready);
    pending = 0;
    base_peer(0, ED_APS_ACK, 0);
    peer_out.aps.destination_endpoint = peer_out.aps.source_endpoint = 1;
    peer_out.aps.profile_id = 0x0104; peer_out.aps.counter = app_counter;
    seal_peer(1, 0, link_b, 1);
    CHECK(bdb_join_receive(&device, mac_body, mac_length, 1, now) == BDB_JOIN_IGNORED);
    CHECK(!device.application_done && !device.transport.queued);
    pending = 0;
    application.aps.flags = 0; wrap_mode = 1;
    for (i = device.transport.next_aps; i; i++) {
        CHECK(bdb_join_send(&device, &application, 0, now) == BDB_JOIN_OK);
        for (iterations = 0; iterations < 32 && !device.application_done; iterations++) drive();
        CHECK(iterations < 32 && app_counter == i);
        CHECK(bdb_join_confirm(&device, &result) == BDB_JOIN_OK && result == NWK_APS_OK);
    }
    CHECK(device.transport.wrap_wait && !device.transport.next_aps);
    CHECK(bdb_join_send(&device, &application, 0, now) == BDB_JOIN_TRANSMIT_FAILED);
    CHECK(!device.transport.queued && device.transport.ready);
    previous = device.keepalive; now = previous;
    for (iterations = 0; iterations < 128 && device.keepalive == previous; iterations++) {
        drive();
        if (pending && !device.transport.active) deliver();
    }
    CHECK(iterations < 128 && device.phase == BDB_JOIN_READY && device.transport.wrap_wait);
    now = device.transport.counter_until;
    CHECK(bdb_join_send(&device, &application, 0, now) == BDB_JOIN_OK);
    for (iterations = 0; iterations < 32 && !device.application_done; iterations++) drive();
    CHECK(iterations < 32 && !app_counter && !device.transport.wrap_wait);
    CHECK(bdb_join_confirm(&device, &result) == BDB_JOIN_OK && result == NWK_APS_OK);
    wrap_mode = 0;
    CHECK(bdb_join_send(&device, &application, 0, now) == BDB_JOIN_OK);
    previous = transmitter.generation;
    update_network();
    CHECK(bdb_join_receive(&device, mac_body, mac_length, 1, now) == BDB_JOIN_OK);
    CHECK(!device.transport.ready);
    CHECK(bdb_join_send(&device, &application, 0, now) == BDB_JOIN_STATE);
    pending = 0; peer_pan = 0x2345;
    for (iterations = 0; iterations < 8 && device.phase != BDB_JOIN_INSTALLING; iterations++) drive();
    CHECK(iterations < 8 && transmitter.generation == previous && !device.transport.ready);
    CHECK(bdb_join_confirm(&device, &result) == BDB_JOIN_OK && result == NWK_APS_CANCELLED);
    drive();
    CHECK(action.kind == BDB_JOIN_ACTION_INSTALL && action.install.pan == peer_pan);
    {
        bdb_join_action_t issued = action;
        memset(&event, 0, sizeof(event)); event.kind = BDB_JOIN_EVENT_INSTALLED;
        event.epoch = issued.epoch; event.token = issued.token; event.installed = issued.install;
        event.installed.pan ^= 1;
        CHECK(bdb_join_step(&device, now, &event, &action) == BDB_JOIN_OK);
        CHECK(device.phase == BDB_JOIN_INSTALLING && !device.transport.ready);
        action = issued;
    }
    drive();
    CHECK(device.phase == BDB_JOIN_READY && device.transport.ready);
    CHECK(security_keys_status(&status) == SECURITY_KEYS_OK && status.config.pan == peer_pan &&
        status.config.update_id == 1 && status.parent_information == 2);
    security_joint_reset(0);
    CHECK(security_keys_open() == SECURITY_KEYS_OK);
    CHECK(security_keys_status(&status) == SECURITY_KEYS_OK && status.phase == SECURITY_KEYS_VERIFIED);
    CHECK(mac_tx_init(&transmitter, 17, now) == MAC_TX_OK);
    CHECK(bdb_join_init(&device, now) == BDB_JOIN_OK);
    CHECK(bdb_join_start(&device, &transmitter, &config, now) == BDB_JOIN_RECOVERY_REQUIRED);
    CHECK(!device.transport.ready && !device.member && device.phase == BDB_JOIN_IDLE);
    for (test_case = 1; test_case <= 8; test_case++) {
        setup(); commission();
        CHECK(!device.transport.ready && !device.member);
        CHECK(security_keys_status(&status) == SECURITY_KEYS_OK);
        if (test_case == 5) {
            CHECK(permit && device.phase == BDB_JOIN_FAULT && transmitter.phase == MAC_TX_FAULT);
            CHECK(status.phase == SECURITY_KEYS_VERIFIED && !device.transport.permit_sent);
        } else {
            CHECK(device.phase == BDB_JOIN_FAILED && transmitter.phase == MAC_TX_IDLE);
            CHECK(status.phase == SECURITY_KEYS_LEFT && !permit);
            CHECK(device.result == (test_case == 1 || test_case == 6 ? BDB_JOIN_KEY_TIMEOUT :
                test_case == 4 ? BDB_JOIN_PARENT_FAILED : BDB_JOIN_TC_FAILED));
            CHECK(test_case == 1 ? device.transport.quiet && !leave_sent : leave_sent && !device.transport.quiet);
        }
    }
    operational_failures(); update_pending_query(); response_backpressure();
    work_and_deadlines(); stopped_transport(); storage_quota(); terminal_control();
    printf("BDB join: %u checks PASS; real commissioning, operational loss, bounded work, backpressure and durable recovery boundary.\n", checks);
    return 0;
}
