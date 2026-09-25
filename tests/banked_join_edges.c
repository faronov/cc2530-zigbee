/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 * Public calls only; included after the typed recorder and coordinator.
 */
static void edge_step(void)
{
    CHECK(recorded_step(&device, now, NULL, &action) == BDB_JOIN_OK);
}

static void edge_receive(uint8_t result)
{
    pending = 0;
    CHECK(recorded_receive(&device, mac_body, mac_length, 1, now) == result);
}

static void edge_application(ed_packet_t *p, uint8_t flags)
{
    memset(p, 0, sizeof(*p));
    p->nwk.version = 2; p->nwk.radius = 30;
    p->aps.source_endpoint = p->aps.destination_endpoint = 1;
    p->aps.profile_id = 0x0104; p->aps.cluster_id = 6; p->aps.flags = flags;
    p->length = 1; p->payload[0] = 0x69;
}

static void edge_confirm(uint8_t expected)
{
    uint8_t result = 0xa5;
    CHECK(recorded_confirm(&device, &result) == BDB_JOIN_OK && result == expected);
}

static void edge_take(uint8_t expected)
{
    ed_packet_t packet;
    memset(&packet, 0xa5, sizeof(packet));
    CHECK(recorded_application(&runtime.zdo, &packet) == ZDO_RUNTIME_OK);
    CHECK(packet.length == 1 && packet.payload[0] == expected);
}

static void edge_rx_queues(void)
{
    ed_packet_t first, second, reply;
    unsigned flash, blocks;
    ready();
    application_from_peer(0x61); edge_receive(BDB_JOIN_OK); edge_step();
    first = runtime.zdo.application;
    CHECK(runtime.zdo.application_ready && !runtime.transport.receive_ready);
    application_from_peer(0x62); edge_receive(BDB_JOIN_OK); edge_step();
    second = runtime.transport.incoming;
    CHECK(runtime.zdo.application_ready && runtime.transport.receive_ready);
    application_from_peer(0x63);
    flash = security_joint_flash_commands(); blocks = security_joint_aes_blocks();
    edge_receive(BDB_JOIN_FULL);
    CHECK(flash == security_joint_flash_commands() && blocks == security_joint_aes_blocks());
    CHECK(!memcmp(&first, &runtime.zdo.application, sizeof(first)));
    CHECK(!memcmp(&second, &runtime.transport.incoming, sizeof(second)));
    edge_take(0x61); edge_step(); edge_take(0x62);
    edge_receive(BDB_JOIN_OK); edge_step(); edge_take(0x63);
    /* Fresh security counters do not authorize duplicate APS delivery. */
    peer_out.nwk.sequence = nwk_sequence++; peer_out.aps.flags = APS_FLAG_ACK_REQUEST;
    seal_peer(1, 0, link_b, 1); edge_receive(BDB_JOIN_IGNORED);
    CHECK(runtime.transport.reply && !runtime.transport.receive_ready);
    reply = runtime.transport.acknowledgment;
    application_from_peer(0x64);
    flash = security_joint_flash_commands(); blocks = security_joint_aes_blocks();
    edge_receive(BDB_JOIN_FULL);
    CHECK(flash == security_joint_flash_commands() && blocks == security_joint_aes_blocks());
    CHECK(!memcmp(&reply, &runtime.transport.acknowledgment, sizeof(reply)));
    drain(); CHECK(!runtime.transport.reply && device.phase == BDB_JOIN_READY);
    application_from_peer(0x65); peer_out.aps.destination_endpoint = 2;
    seal_peer(1, 0, link_b, 1); edge_receive(BDB_JOIN_IGNORED);
    application_from_peer(0x66); peer_out.aps.profile_id ^= 1;
    seal_peer(1, 0, link_b, 1); edge_receive(BDB_JOIN_IGNORED);
    CHECK(!runtime.transport.receive_ready && !runtime.zdo.application_ready);
    base_peer(0, 0, 0); peer_out.length = 1;
    seal_peer(0, 0, NULL, 1); edge_receive(BDB_JOIN_OK); edge_step();
    CHECK(device.receive_result == ZDO_RUNTIME_FORMAT && !runtime.zdo.application_ready);
    CHECK(device.phase == BDB_JOIN_READY && runtime.transport.ready);
}

static void edge_wrap(void)
{
    ed_packet_t application;
    unsigned counter, flash, blocks, sent;
    uint32_t until;
    ready(); edge_application(&application, 0);
    flash = security_joint_flash_commands(); blocks = security_joint_aes_blocks(); sent = transmissions;
    /* Real queue/cancel/step/confirm calls consume sequence numbers without
     * inventing a PHY transmission, crypto return or private counter value. */
    for (counter = runtime.transport.next_aps; counter < 254; counter++) {
        now++;
        CHECK(recorded_send(&device, &application, 0, now) == BDB_JOIN_OK);
        CHECK(runtime.transport.outgoing.aps.counter == counter);
        CHECK(recorded_cancel() == NWK_APS_OK);
        edge_step(); edge_confirm(NWK_APS_CANCELLED);
        CHECK(device.phase == BDB_JOIN_READY && !runtime.transport.queued && !runtime.transport.active);
    }
    CHECK(flash == security_joint_flash_commands() && blocks == security_joint_aes_blocks());
    CHECK(sent == transmissions);
    wrap_mode = 1;
    for (; counter < 256; counter++) {
        CHECK(recorded_send(&device, &application, 0, now) == BDB_JOIN_OK);
        drain(); edge_confirm(NWK_APS_OK);
        CHECK(app_counter == counter);
    }
    CHECK(runtime.transport.wrap_wait && !runtime.transport.next_aps);
    until = runtime.transport.counter_until;
    CHECK(recorded_send(&device, &application, 0, now) == BDB_JOIN_TRANSMIT_FAILED);
    wrap_mode = 0; mute_timeout = 1; keepalive_sent();
    address_request(1); edge_receive(BDB_JOIN_OK); edge_step(); drain();
    CHECK(!reply_seen && runtime.zdo.response_result == NWK_APS_EXHAUSTED);
    CHECK(runtime.zdo.query == ZDO_RUNTIME_PARENT && device.phase == BDB_JOIN_READY);
    timeout_response(); deliver();
    CHECK(!runtime.zdo.query && runtime.transport.wrap_wait && !device.attempts);
    expected_reply_cluster = 0;
    now = until-1;
    CHECK(recorded_send(&device, &application, 0, now) == BDB_JOIN_TRANSMIT_FAILED);
    now = until; wrap_mode = 1;
    CHECK(recorded_send(&device, &application, 0, now) == BDB_JOIN_OK);
    drain(); edge_confirm(NWK_APS_OK);
    CHECK(!app_counter && !runtime.transport.wrap_wait && device.phase == BDB_JOIN_READY);
}

static void edge_ack(void)
{
    base_peer(0, ED_APS_ACK, runtime.transport.outgoing.aps.cluster_id);
    peer_out.aps.counter = runtime.transport.outgoing.aps.counter;
    peer_out.aps.profile_id = runtime.transport.outgoing.aps.profile_id;
    peer_out.aps.source_endpoint = runtime.transport.outgoing.aps.destination_endpoint;
    peer_out.aps.destination_endpoint = runtime.transport.outgoing.aps.source_endpoint;
}

static void edge_ack_correlation(void)
{
    ed_packet_t application;
    uint8_t bad, result = 0xa5;
    ready(); edge_application(&application, APS_FLAG_ACK_REQUEST); drop_all_acks = 1;
    CHECK(recorded_send(&device, &application, 1, now) == BDB_JOIN_OK);
    CHECK(recorded_send(&device, &application, 1, now) == BDB_JOIN_FULL);
    CHECK(recorded_confirm(&device, &result) == BDB_JOIN_STATE && result == 0xa5);
    edge_step();
    edge_ack(); seal_peer(1, 0, link_b, 1); edge_receive(BDB_JOIN_IGNORED);
    CHECK(!runtime.transport.sent && !runtime.transport.seen_ack);
    for (iterations = 0; iterations < 32 && !runtime.transport.sent; iterations++) drive();
    CHECK(iterations < 32 && runtime.transport.active);
    for (bad = 0; bad < 9; bad++) {
        edge_ack();
        if (!bad) peer_out.aps.counter++;
        else if (bad == 1) peer_out.aps.cluster_id++;
        else if (bad == 2) peer_out.aps.profile_id++;
        else if (bad == 3) peer_out.aps.source_endpoint++;
        else if (bad == 4) peer_out.aps.destination_endpoint++;
        else if (bad == 5) peer_out.aps.flags = APS_FLAG_ACK_FORMAT;
        else if (bad == 7) peer_out.nwk.source = 1;
        else if (bad == 8) peer_out.nwk.destination++;
        seal_peer(bad != 6, 0, link_b, 1);
        edge_receive(bad >= 7 ? BDB_JOIN_SECURITY : BDB_JOIN_IGNORED);
        CHECK(!runtime.transport.seen_ack && !device.application_done);
    }
    edge_ack(); seal_peer(1, 0, link_b, 1); edge_receive(BDB_JOIN_OK); edge_step();
    CHECK(runtime.transport.seen_ack && !device.application_done && runtime.transport.active);
    drain(); edge_confirm(NWK_APS_OK);
    edge_ack(); seal_peer(1, 0, link_b, 1); edge_receive(BDB_JOIN_IGNORED);
    CHECK(!runtime.transport.queued && device.phase == BDB_JOIN_READY);
}

static void edge_ack_deadlines(void)
{
    ed_packet_t application;
    uint8_t attempt, counter;
    uint32_t security, deadline;
    ready(); edge_application(&application, APS_FLAG_ACK_REQUEST); drop_all_acks = 1;
    CHECK(recorded_send(&device, &application, 1, now) == BDB_JOIN_OK);
    counter = runtime.transport.outgoing.aps.counter;
    for (attempt = 0; attempt <= NWK_APS_RETRIES; attempt++) {
        for (iterations = 0; iterations < 32 && !runtime.transport.waiting; iterations++) drive();
        CHECK(iterations < 32 && runtime.transport.retries == attempt);
        CHECK(app_counter == counter && !runtime.transport.active);
        security = last_device_nwk;
        deadline = runtime.transport.deadline;
        now = deadline;
        edge_ack(); seal_peer(1, 0, link_b, 1); edge_receive(BDB_JOIN_IGNORED);
        CHECK(!runtime.transport.seen_ack);
        edge_step();
        if (attempt < NWK_APS_RETRIES) {
            CHECK(runtime.transport.active && runtime.transport.retries == attempt+1);
            for (iterations = 0; iterations < 32 && last_device_nwk == security; iterations++) drive();
            CHECK(iterations < 32 && last_device_nwk > security);
        }
    }
    edge_confirm(NWK_APS_TIMEOUT);
    CHECK(device.phase == BDB_JOIN_READY && !runtime.transport.queued);
    /* An absolute transaction expiry cannot be replaced by a fresh ACK wait. */
    CHECK(recorded_send(&device, &application, 1, now) == BDB_JOIN_OK);
    now = runtime.transport.transaction_until;
    edge_ack(); seal_peer(1, 0, link_b, 1); edge_receive(BDB_JOIN_IGNORED);
    edge_step(); edge_confirm(NWK_APS_TIMEOUT);
    CHECK(device.phase == BDB_JOIN_READY && !runtime.transport.seen_ack);
}

static void edge_server_reply(uint16_t cluster, uint8_t status_byte, uint8_t length)
{
    uint8_t tsn = peer_out.payload[0], endpoint = peer_out.aps.source_endpoint;
    expected_reply_cluster = cluster; expected_reply_status = status_byte;
    expected_reply_length = length; reply_seen = 0;
    edge_receive(BDB_JOIN_OK); edge_step(); drain();
    CHECK(reply_seen && peer_in.payload[0] == tsn && peer_in.aps.destination_endpoint == endpoint);
    CHECK(device.phase == BDB_JOIN_READY);
    expected_reply_cluster = 0;
}

static void edge_zdo_server(void)
{
    ed_packet_t deferred;
    uint8_t which;
    unsigned sent;
    ready();
    now += runtime.transport.duplicate_time; keepalive_success();
    address_request(0); edge_server_reply(0x8000, 0, 12);
    base_peer(0, 0, 1); peer_out.aps.source_endpoint = 3;
    peer_out.length = 5; peer_out.payload[0] = 0xa2; peer_out.payload[1] = 0x78;
    peer_out.payload[2] = 0x56; peer_out.payload[3] = 1;
    seal_peer(0, 0, NULL, 1); edge_server_reply(0x8001, 0, 13);
    base_peer(0, 0, 2); peer_out.length = 3; peer_out.payload[0] = 0xa3;
    peer_out.payload[1] = 1;
    seal_peer(0, 0, NULL, 1); edge_server_reply(0x8002, ZDO_NODE_INVALID_REQUEST, 4);
    base_peer(0, 0, 0x0123); peer_out.length = 1; peer_out.payload[0] = 0xa4;
    seal_peer(0, 0, NULL, 1); edge_server_reply(0x8123, ZDO_NODE_NOT_SUPPORTED, 2);
    sent = transmissions;
    for (which = 0; which < 3; which++) {
        base_peer(0, 0, which == 0 ? ZDO_SRV_PARENT_ANNCE : which == 1 ? 0x0123 : 0);
        peer_out.length = 1; peer_out.payload[0] = 0xa5+which;
        if (which == 1) { peer_out.nwk.destination = 0xfffd; peer_out.aps.delivery_mode = 2; }
        seal_peer(0, 0, NULL, 1); edge_receive(BDB_JOIN_OK); edge_step(); drain();
        CHECK(!runtime.zdo.response_pending && !runtime.zdo.response_tx);
        CHECK(device.receive_result == (which == 2 ? ZDO_RUNTIME_FORMAT : ZDO_RUNTIME_IGNORED));
    }
    CHECK(sent == transmissions);
    now += runtime.transport.duplicate_time; keepalive_success();
    for (which = 0; which < 4; which++) {
        base_peer(0, 0, which/2);
        peer_out.payload[0] = 0xa8+which;
        if (which < 2) {
            peer_out.length = 11; memcpy(peer_out.payload+1, identity.own_ieee, 8);
            if (!which) peer_out.payload[9] = 2;
            else peer_out.payload[1] ^= 0x80;
        } else {
            peer_out.length = 5; peer_out.payload[1] = 0x78; peer_out.payload[2] = 0x56;
            if (which == 2) peer_out.payload[3] = 2;
            else peer_out.payload[1] ^= 0x80;
        }
        seal_peer(0, 0, NULL, 1);
        edge_server_reply((uint16_t)(0x8000+which/2),
            which & 1u ? ZDO_NODE_DEVICE_NOT_FOUND : ZDO_NODE_INVALID_REQUEST, 12);
    }
    base_peer(0, 0, 2); peer_out.length = 3; peer_out.payload[0] = 0xac;
    peer_out.payload[1] = 0x78; peer_out.payload[2] = 0x56;
    seal_peer(0, 0, NULL, 1); edge_server_reply(0x8002, 0, 17);
    /* A deferred server packet cannot overwrite an in-flight parent client. */
    now += runtime.transport.duplicate_time; keepalive_success();
    now = device.keepalive; edge_step();
    CHECK(runtime.zdo.query_tx && runtime.transport.queued && !runtime.transport.active);
    address_request(0); edge_receive(BDB_JOIN_OK); edge_step();
    CHECK(runtime.zdo.response_pending);
    deferred = runtime.zdo.response;
    base_peer(0, 0, 0x0123); peer_out.length = 1; peer_out.payload[0] = 0xb1;
    seal_peer(0, 0, NULL, 1); edge_receive(BDB_JOIN_OK); edge_step();
    CHECK(device.receive_result == ZDO_RUNTIME_DROPPED && runtime.zdo.response_result == NWK_APS_FULL);
    CHECK(!memcmp(&deferred, &runtime.zdo.response, sizeof(deferred)));
    for (iterations = 0; iterations < 64 && !pending; iterations++) drive();
    CHECK(iterations < 64 && runtime.transport.active);
    deliver(); drain();
    CHECK(reply_seen && !runtime.zdo.query && device.phase == BDB_JOIN_READY);
    CHECK(peer_in.payload[0] == deferred.payload[0]);
}

static void edge_broadcast_table(void)
{
    uint8_t i;
    uint32_t until;
    unsigned blocks, flash;
    ready();
    now += runtime.transport.broadcast_time;
    until = now+runtime.transport.broadcast_time;
    for (i = 0; i < NWK_APS_DUPLICATES; i++) {
        base_peer(0, 0, ZDO_SRV_PARENT_ANNCE);
        peer_out.nwk.destination = 0xfffd; peer_out.nwk.sequence = (uint8_t)(252+i);
        peer_out.aps.delivery_mode = 2; peer_out.aps.counter = 0x90;
        peer_out.length = 1; peer_out.payload[0] = 0xb2;
        seal_peer(0, 0, NULL, 1);
        edge_receive(i ? BDB_JOIN_IGNORED : BDB_JOIN_OK);
        if (!i) edge_step();
    }
    CHECK(!runtime.transport.receive_ready);
    blocks = security_joint_aes_blocks(); flash = security_joint_flash_commands();
    edge_receive(BDB_JOIN_IGNORED);
    CHECK(blocks == security_joint_aes_blocks() && flash == security_joint_flash_commands());
    peer_out.nwk.sequence = 4; seal_peer(0, 0, NULL, 1);
    blocks = security_joint_aes_blocks(); flash = security_joint_flash_commands();
    edge_receive(BDB_JOIN_FULL);
    CHECK(blocks == security_joint_aes_blocks() && flash == security_joint_flash_commands());
    now = until-1; edge_receive(BDB_JOIN_FULL);
    now = until; edge_receive(BDB_JOIN_IGNORED);
    CHECK(blocks < security_joint_aes_blocks() && flash < security_joint_flash_commands());
    CHECK(device.phase == BDB_JOIN_READY && !runtime.zdo.response_tx);
    keepalive_success();
}

static void edge_address_map(void)
{
    static const uint8_t peers[4][8] = {
        {0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38},
        {0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48},
        {0x51,0x52,0x53,0x54,0x55,0x56,0x57,0x58},
        {255,255,255,255,255,255,255,255},
    };
    uint8_t i;
    zdo_runtime_address_t full[ZDO_RUNTIME_MAP_SIZE];
    ready();
    announcement(0x1111, 0x1111, peers[0]); edge_receive(BDB_JOIN_SECURITY);
    CHECK(!runtime.zdo.map[0].used);
    for (i = 0; i < 2; i++) {
        announcement(0, (uint16_t)(0x1111u*(i+1)), peers[i]);
        edge_receive(BDB_JOIN_OK); edge_step();
    }
    memcpy(full, runtime.zdo.map, sizeof(full));
    announcement(0, 0x3333, peers[2]); edge_receive(BDB_JOIN_OK); edge_step();
    CHECK(device.receive_result == ZDO_RUNTIME_FULL && !memcmp(full, runtime.zdo.map, sizeof(full)));
    announcement(0, 0x1111, peers[1]); edge_receive(BDB_JOIN_OK); edge_step();
    CHECK(!runtime.zdo.conflict && !runtime.zdo.map[3].valid && runtime.zdo.map[2].address == 0x1111);
    announcement(0, 0x4444, peers[3]); edge_receive(BDB_JOIN_OK); edge_step();
    CHECK(device.receive_result == ZDO_RUNTIME_OK && device.phase == BDB_JOIN_READY);
    now += runtime.transport.duplicate_time; keepalive_success();
    announcement(0, 0x5678, peers[2]); edge_receive(BDB_JOIN_OK); edge_step(); drain();
    CHECK(device.phase == BDB_JOIN_FAILED && device.result == BDB_JOIN_ADDRESS_CONFLICT);
    CHECK(!runtime.zdo.map[0].valid && !device.member && !runtime.transport.ready && leave_sent);
    CHECK(security_keys_status(&status) == SECURITY_KEYS_OK && status.phase == SECURITY_KEYS_LEFT);
}

static void edge_fill_duplicates(void)
{
    uint8_t i, free = 0;
    for (i = 0; i < NWK_APS_DUPLICATES; i++) if (!runtime.transport.duplicate[i].used) free++;
    for (i = 0; i < free; i++) {
        application_from_peer(0x70+i); edge_receive(BDB_JOIN_OK); edge_step(); edge_take(0x70+i);
    }
    application_from_peer(0x7f); edge_receive(BDB_JOIN_FULL);
    CHECK(!runtime.transport.receive_ready && runtime.transport.ready);
}

static void edge_update(uint8_t timeout)
{
    ed_packet_t application;
    bdb_join_event_t confirmation;
    uint8_t bad;
    uint32_t generation;
    ready();
    if (!timeout) edge_fill_duplicates();
    edge_application(&application, 0);
    CHECK(recorded_send(&device, &application, 1, now) == BDB_JOIN_OK);
    generation = transmitter.generation;
    update_network(); edge_receive(BDB_JOIN_OK);
    CHECK(!runtime.transport.ready);
    CHECK(recorded_send(&device, &application, 1, now) == BDB_JOIN_STATE);
    peer_pan = 0x2345;
    for (iterations = 0; iterations < 16 && action.kind != BDB_JOIN_ACTION_INSTALL; iterations++) drive();
    CHECK(iterations < 16 && device.phase == BDB_JOIN_INSTALLING && generation == transmitter.generation);
    edge_confirm(NWK_APS_CANCELLED);
    CHECK(security_keys_status(&status) == SECURITY_KEYS_OK && status.config.pan == peer_pan);
    memset(&confirmation, 0, sizeof(confirmation)); confirmation.kind = BDB_JOIN_EVENT_INSTALLED;
    confirmation.token = action.token; confirmation.epoch = action.epoch;
    confirmation.data.installed = action.data.install;
    for (bad = 0; bad < 6; bad++) {
        event = confirmation;
        if (bad == 0) event.token++;
        else if (bad == 1) event.epoch++;
        else if (bad == 2) event.data.installed.pan++;
        else if (bad == 3) event.data.installed.address++;
        else if (bad == 4) event.data.installed.channel++;
        else event.data.installed.own_ieee[0]++;
        CHECK(recorded_step(&device, now, &event, &action) == BDB_JOIN_OK);
        CHECK(device.phase == BDB_JOIN_INSTALLING && !runtime.transport.ready);
    }
    if (timeout) now = device.until;
    CHECK(recorded_step(&device, now, &confirmation, &action) == BDB_JOIN_OK);
    CHECK(device.phase == (timeout ? BDB_JOIN_FAULT : BDB_JOIN_READY));
    CHECK(runtime.transport.ready == !timeout);
    if (timeout) CHECK(device.result == BDB_JOIN_INSTALL_FAILED);
}

static void edge_node_start(void)
{
    test_case = 0; setup();
    for (iterations = 0; iterations < 256; iterations++) {
        drive();
        if (device.phase == BDB_JOIN_WAIT_KEY && !initial_key) initial_transport();
        if (pending && device.workspace == BDB_JOIN_WORK_RUNTIME && !runtime.transport.active) deliver();
        if (device.phase == BDB_JOIN_NODE && runtime.zdo.query && !runtime.transport.sent) break;
    }
    CHECK(iterations < 256 && !runtime.transport.ready);
}

static void edge_node_response(void)
{
    zdo_node_response_t descriptor;
    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.sequence = runtime.zdo.sequence;
    descriptor.descriptor = config.descriptor;
    descriptor.descriptor.logical_type = 0;
    base_peer(0, 0, 0x8002);
    CHECK(zdo_node_rsp_encode(&descriptor, peer_out.payload, sizeof(peer_out.payload),
        &peer_out.length) == ZDO_NODE_OK);
}

static void edge_node_sent(void)
{
    for (iterations = 0; iterations < 64 && !pending; iterations++) drive();
    CHECK(iterations < 64 && runtime.transport.sent);
    pending = 0;
    for (iterations = 0; iterations < 64 && runtime.transport.active; iterations++) drive();
    CHECK(iterations < 64 && runtime.zdo.tx_done);
}

static void edge_node_correlation(void)
{
    uint8_t bad;
    edge_node_start();
    edge_node_response(); seal_peer(0, 0, NULL, 1); edge_receive(BDB_JOIN_OK); edge_step();
    CHECK(device.phase == BDB_JOIN_NODE && runtime.zdo.result == ZDO_RUNTIME_NO_EVENT);
    edge_node_sent();
    for (bad = 0; bad < 10; bad++) {
        edge_node_response();
        if (bad == 0) peer_out.payload[0]++;
        else if (bad == 1) peer_out.aps.cluster_id = 0x8001;
        else if (bad == 2) peer_out.aps.source_endpoint = 1;
        else if (bad == 3) peer_out.aps.profile_id = 0x0105;
        else if (bad == 4) peer_out.nwk.source = 1;
        else if (bad == 5) peer_out.payload[2] = 1;
        else if (bad == 6) peer_out.length = 1;
        else if (bad == 7) peer_out.aps.delivery_mode = 2;
        else if (bad == 8) peer_out.aps.destination_endpoint = 1;
        else peer_out.nwk.destination++;
        seal_peer(0, 0, NULL, 1);
        edge_receive((bad >= 1 && bad <= 4) || bad >= 7 ? BDB_JOIN_SECURITY : BDB_JOIN_OK);
        edge_step();
        CHECK(device.phase == BDB_JOIN_NODE && runtime.zdo.result == ZDO_RUNTIME_NO_EVENT);
        CHECK(!runtime.transport.ready);
    }
    edge_node_response(); seal_peer(0, 0, NULL, 1); edge_receive(BDB_JOIN_OK);
    described = 1; edge_step(); commission();
    CHECK(device.phase == BDB_JOIN_READY && runtime.transport.ready);
}

static void edge_node_failure(uint8_t timeout)
{
    uint8_t attempt, old_sequence = 0;
    edge_node_start();
    for (attempt = 1; attempt <= (timeout ? BDB_JOIN_ATTEMPTS : 1); attempt++) {
        edge_node_sent();
        CHECK(device.attempts == attempt);
        edge_node_response();
        if (timeout) {
            if (attempt > 1) {
                peer_out.payload[0] = old_sequence;
                seal_peer(0, 0, NULL, 1); edge_receive(BDB_JOIN_OK); edge_step();
                CHECK(runtime.zdo.result == ZDO_RUNTIME_NO_EVENT);
            }
            old_sequence = runtime.zdo.sequence;
            edge_node_response(); now = runtime.zdo.deadline;
        } else { peer_out.payload[1] = ZDO_NODE_NOT_SUPPORTED; peer_out.length = 2; }
        seal_peer(0, 0, NULL, 1); edge_receive(BDB_JOIN_OK); edge_step();
        CHECK(!runtime.transport.ready);
    }
    drain();
    CHECK(device.phase == BDB_JOIN_FAILED && device.result == BDB_JOIN_TC_FAILED && leave_sent);
    CHECK(security_keys_status(&status) == SECURITY_KEYS_OK && status.phase == SECURITY_KEYS_LEFT);
}

static void edge_commission_failure(uint8_t which)
{
    test_case = which; setup(); commission();
    CHECK(device.phase == BDB_JOIN_FAILED && transmitter.phase == MAC_TX_IDLE);
    CHECK(!runtime.transport.ready && !device.member && !permit && leave_sent);
    CHECK(device.result == (which == 6 ? BDB_JOIN_KEY_TIMEOUT :
        which == 4 ? BDB_JOIN_PARENT_FAILED : BDB_JOIN_TC_FAILED));
    CHECK(security_keys_status(&status) == SECURITY_KEYS_OK && status.phase == SECURITY_KEYS_LEFT);
}

static void edge_case(const char *name)
{
    printf("CASE %s\n", name);
    initial_now = 0xfffffc00UL;
    if (!strcmp(name, "rx-queues")) edge_rx_queues();
    else if (!strcmp(name, "wrap-quarantine")) edge_wrap();
    else if (!strcmp(name, "ack-correlation")) edge_ack_correlation();
    else if (!strcmp(name, "ack-deadlines")) edge_ack_deadlines();
    else if (!strcmp(name, "zdo-server")) edge_zdo_server();
    else if (!strcmp(name, "broadcast-table")) edge_broadcast_table();
    else if (!strcmp(name, "address-map")) edge_address_map();
    else if (!strcmp(name, "update-full")) edge_update(0);
    else if (!strcmp(name, "install-timeout")) edge_update(1);
    else if (!strcmp(name, "node-correlation")) edge_node_correlation();
    else if (!strcmp(name, "node-timeout")) edge_node_failure(1);
    else if (!strcmp(name, "node-status")) edge_node_failure(0);
    else if (!strcmp(name, "tc-key-timeout")) edge_commission_failure(2);
    else if (!strcmp(name, "tc-confirm-timeout")) edge_commission_failure(3);
    else if (!strcmp(name, "parent-status")) edge_commission_failure(4);
    else if (!strcmp(name, "network-key-late")) edge_commission_failure(6);
    else if (!strcmp(name, "tc-key-late")) edge_commission_failure(7);
    else if (!strcmp(name, "tc-confirm-late")) edge_commission_failure(8);
    else CHECK(0);
}
