/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 * Record real host calls and peripheral operands for the independent MCU run.
 */
#include "bdb_join.h"
#include "security_joint_model.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void recorded_reset(uint8_t erase);
static security_keys_result_t recorded_open(void);
static security_keys_result_t recorded_provision(const security_keys_config_t *c, const uint8_t *code,
    uint32_t nwk, uint32_t aps, const ccm_star_limits_t *l, uint16_t polls);
static mac_tx_result_t recorded_mac_init(mac_tx_t *m, uint8_t dsn, uint32_t stamp);
static mac_tx_result_t recorded_mac_step(mac_tx_t *m, uint32_t stamp, const mac_tx_event_t *e, mac_tx_action_t *a);
static mac_tx_result_t recorded_mac_copy(const mac_tx_t *m, uint8_t *body, uint16_t capacity, uint8_t *length);
static bdb_join_result_t recorded_init(bdb_join_t *d, uint32_t stamp);
static bdb_join_result_t recorded_start(bdb_join_t *d, mac_tx_t *m, const bdb_join_config_t *c, uint32_t stamp);
static bdb_join_result_t recorded_step(bdb_join_t *d, uint32_t stamp, const bdb_join_event_t *e, bdb_join_action_t *a);
static bdb_join_result_t recorded_receive(bdb_join_t *d, const uint8_t *body, uint16_t length, uint8_t crc, uint32_t stamp);
static bdb_join_result_t recorded_send(bdb_join_t *d, const ed_packet_t *p, uint8_t secure, uint32_t stamp);
static bdb_join_result_t recorded_confirm(bdb_join_t *d, uint8_t *result);
static zdo_runtime_result_t recorded_application(zdo_runtime_t *z, ed_packet_t *p);

#define security_joint_reset recorded_reset
#define security_keys_open recorded_open
#define security_keys_provision recorded_provision
#define mac_tx_init recorded_mac_init
#define mac_tx_step recorded_mac_step
#define mac_tx_copy recorded_mac_copy
#define bdb_join_init recorded_init
#define bdb_join_start recorded_start
#define bdb_join_step recorded_step
#define bdb_join_receive recorded_receive
#define bdb_join_send recorded_send
#define bdb_join_confirm recorded_confirm
#define zdo_runtime_take_application recorded_application
#define main original_bdb_corpus
#include "test_bdb_join.c"
#undef main
#undef runtime
#undef security_joint_reset
#undef security_keys_open
#undef security_keys_provision
#undef mac_tx_init
#undef mac_tx_step
#undef mac_tx_copy
#undef bdb_join_init
#undef bdb_join_start
#undef bdb_join_step
#undef bdb_join_receive
#undef bdb_join_send
#undef bdb_join_confirm
#undef zdo_runtime_take_application

static uint8_t packed[2048], call_input[180], call_output[125], written;
static security_keys_status_t observed_keys;
static unsigned operations;

static void put_number(unsigned offset, uint32_t value, unsigned size)
{
    unsigned i;
    assert(size && size <= 4 && offset+size <= sizeof(packed));
    assert(size == 4 || value < (1UL << (8u*size)));
    for (i = 0; i < size; i++) { packed[offset+i] = (uint8_t)value; value >>= 8; }
}

static void put_owner_pointer(unsigned offset, const void *p, unsigned size)
{
    assert(!p || p == &transmitter);
    put_number(offset, p != NULL, size);
}

static void put_frame_pointer(unsigned offset, const void *p, unsigned size)
{
    put_number(offset, p != NULL, size);
}

#include "banked_join_layout.h"
#define runtime (device.work.runtime)

static void hex(const uint8_t *p, unsigned size)
{
    unsigned i;
    for (i = 0; i < size; i++) printf("%02x", p[i]);
}

static void prepare(void)
{
    memset(call_input, 0, sizeof(call_input));
    memset(call_output, 0xa5, sizeof(call_output));
    written = 0xa5;
}

static void begin(uint8_t command, uint32_t stamp, uint8_t arg, uint16_t length)
{
    printf("CALL %u %lu %u %u %u ", command, (unsigned long)stamp, arg, length, written);
    hex(call_input, sizeof(call_input)); putchar(' ');
    hex(call_output, sizeof(call_output)); putchar('\n');
    security_joint_trace(1);
    operations++;
}

static void finish(uint8_t result)
{
    uint8_t metadata_result;
    security_joint_trace(0);
    metadata_result = security_keys_status(&observed_keys);
    printf("RESULT %u %u %u ", result, metadata_result, written);
    pack_metadata(&observed_keys); hex(packed, JOIN_METADATA_SIZE);
    putchar(' '); hex(call_output, sizeof(call_output)); putchar('\n');
    printf("DEVICE "); pack_device(&device); hex(packed, JOIN_DEVICE_SIZE); putchar('\n');
    printf("MAC "); pack_mac(&transmitter); hex(packed, JOIN_MAC_SIZE); putchar('\n');
    printf("NV "); hex(security_joint_nv(), 4096); putchar('\n');
}

static void recorded_reset(uint8_t erase)
{
    printf("RESET %u\n", erase);
    security_joint_reset(erase);
    /* The matching MCU reset executes CRT clearing of these caller objects.
     * This is never used as an operation handoff or recovery of a live lease. */
    memset(&device, 0, sizeof(device));
    memset(&transmitter, 0, sizeof(transmitter));
}

static security_keys_result_t recorded_open(void)
{
    security_keys_result_t result;
    prepare(); begin(0, 0, 0, 0);
    result = security_keys_open(); finish(result); return result;
}

static security_keys_result_t recorded_provision(const security_keys_config_t *c, const uint8_t *code,
    uint32_t nwk, uint32_t aps, const ccm_star_limits_t *l, uint16_t polls)
{
    security_keys_result_t result;
    prepare(); pack_identity(c); memcpy(call_input+JOIN_PROVISION_CONFIG, packed, JOIN_IDENTITY_SIZE);
    pack_limits(l); memcpy(call_input+JOIN_PROVISION_LIMITS, packed, JOIN_LIMITS_SIZE);
    put_number(JOIN_PROVISION_NWK, nwk, 4); put_number(JOIN_PROVISION_APS, aps, 4);
    put_number(JOIN_PROVISION_POLLS, polls, 2);
    memcpy(call_input+JOIN_PROVISION_NWK, packed+JOIN_PROVISION_NWK, 10);
    memcpy(call_input+JOIN_PROVISION_INSTALL, code, 18);
    begin(1, 0, 0, 0);
    result = security_keys_provision(c, code, nwk, aps, l, polls); finish(result); return result;
}

static mac_tx_result_t recorded_mac_init(mac_tx_t *m, uint8_t dsn, uint32_t stamp)
{
    mac_tx_result_t result;
    assert(m == &transmitter); prepare(); begin(2, stamp, dsn, 0);
    result = mac_tx_init(m, dsn, stamp); finish(result); return result;
}

static bdb_join_result_t recorded_init(bdb_join_t *d, uint32_t stamp)
{
    bdb_join_result_t result;
    assert(d == &device); prepare(); begin(3, stamp, 0, 0);
    result = bdb_join_init(d, stamp); finish(result); return result;
}

static bdb_join_result_t recorded_start(bdb_join_t *d, mac_tx_t *m, const bdb_join_config_t *c, uint32_t stamp)
{
    bdb_join_result_t result;
    assert(d == &device && m == &transmitter); prepare();
    pack_config(c); memcpy(call_input, packed, JOIN_CONFIG_SIZE); begin(4, stamp, 0, 0);
    result = bdb_join_start(d, m, c, stamp); finish(result); return result;
}

static void event_input(const bdb_join_event_t *e)
{
    const uint8_t *body = NULL;
    uint16_t length = 0;
    if (!e) return;
    pack_event(e); memcpy(call_input, packed, JOIN_EVENT_SIZE);
    if (e->kind == BDB_JOIN_EVENT_SCAN) {
        body = e->data.scan.body; length = e->data.scan.length;
    } else if (e->kind == BDB_JOIN_EVENT_ASSOCIATION) {
        assert(!e->data.association.body || !e->data.association.source.bytes);
        if (e->data.association.body) {
            body = e->data.association.body; length = e->data.association.length;
        } else {
            body = e->data.association.source.bytes; length = e->data.association.source.length;
        }
    } else if (e->kind == BDB_JOIN_EVENT_TX) {
        body = e->data.tx.bytes; length = e->data.tx.length;
    }
    assert(length <= 125 && (body || !length));
    if (length) memcpy(call_input+JOIN_BYTES_OFFSET, body, length);
}

static bdb_join_result_t recorded_step(bdb_join_t *d, uint32_t stamp, const bdb_join_event_t *e, bdb_join_action_t *a)
{
    bdb_join_result_t result;
    assert(d == &device); prepare(); event_input(e);
    pack_action(a); memcpy(call_output, packed, JOIN_ACTION_SIZE); begin(5, stamp, e != NULL, 0);
    result = bdb_join_step(d, stamp, e, a);
    pack_action(a); memcpy(call_output, packed, JOIN_ACTION_SIZE); finish(result); return result;
}

static mac_tx_result_t recorded_mac_step(mac_tx_t *m, uint32_t stamp, const mac_tx_event_t *e, mac_tx_action_t *a)
{
    bdb_join_event_t input = {0};
    mac_tx_result_t result;
    assert(m == &transmitter); prepare();
    input.kind = BDB_JOIN_EVENT_TX;
    if (e) input.data.tx = *e;
    event_input(&input);
    pack_radio(a); memcpy(call_output, packed, JOIN_RADIO_SIZE); begin(6, stamp, e != NULL, 0);
    result = mac_tx_step(m, stamp, e, a);
    pack_radio(a); memcpy(call_output, packed, JOIN_RADIO_SIZE); finish(result); return result;
}

static bdb_join_result_t recorded_receive(bdb_join_t *d, const uint8_t *body, uint16_t length, uint8_t crc, uint32_t stamp)
{
    bdb_join_result_t result;
    assert(d == &device && body && length <= 125);
    prepare(); memcpy(call_input+JOIN_BYTES_OFFSET, body, length); begin(7, stamp, crc, length);
    result = bdb_join_receive(d, body, length, crc, stamp); finish(result); return result;
}

static bdb_join_result_t recorded_send(bdb_join_t *d, const ed_packet_t *p, uint8_t secure, uint32_t stamp)
{
    bdb_join_result_t result;
    assert(d == &device); prepare(); pack_packet(p); memcpy(call_input, packed, JOIN_PACKET_SIZE);
    begin(8, stamp, secure, 0);
    result = bdb_join_send(d, p, secure, stamp); finish(result); return result;
}

static bdb_join_result_t recorded_confirm(bdb_join_t *d, uint8_t *result)
{
    bdb_join_result_t rc;
    assert(d == &device); prepare(); call_output[0] = *result; begin(9, 0, 0, 0);
    rc = bdb_join_confirm(d, result); call_output[0] = *result; finish(rc); return rc;
}

static zdo_runtime_result_t recorded_application(zdo_runtime_t *z, ed_packet_t *p)
{
    zdo_runtime_result_t result;
    assert(z == &runtime.zdo); prepare(); pack_packet(p); memcpy(call_output, packed, JOIN_PACKET_SIZE);
    begin(10, 0, 0, 0);
    result = zdo_runtime_take_application(z, p);
    pack_packet(p); memcpy(call_output, packed, JOIN_PACKET_SIZE); finish(result); return result;
}

static mac_tx_result_t recorded_mac_copy(const mac_tx_t *m, uint8_t *body, uint16_t capacity, uint8_t *length)
{
    mac_tx_result_t result;
    assert(m == &transmitter && capacity == 125); prepare(); begin(11, 0, 0, capacity);
    result = mac_tx_copy(m, body, capacity, length);
    assert(result == MAC_TX_OK && *length <= 125);
    written = *length; memcpy(call_output, body, *length); finish(result); return result;
}

static nwk_aps_result_t recorded_cancel(void)
{
    nwk_aps_result_t result;
    prepare(); begin(12, now, 0, 0);
    result = nwk_aps_cancel(&runtime.transport, now); finish(result); return result;
}

#include "banked_join_edges.c"

int main(int argc, char **argv)
{
    ed_packet_t application = {0}, received = {0};
    uint8_t result = 0xa5, failure;
    uint32_t old_deadline;
    if (argc != 1) {
        CHECK(argc == 2);
        edge_case(argv[1]);
        printf("DONE %u\n", operations);
        return 0;
    }
    puts("CASE joined-data-update-loss-restart");
    initial_now = 0xfffffc00UL;
    ready();
    CHECK(announced && described && requested && verified && parent_set && permit);
    application.nwk.version = 2; application.nwk.radius = 30;
    application.aps.source_endpoint = application.aps.destination_endpoint = 1;
    application.aps.profile_id = 0x0104; application.aps.flags = APS_FLAG_ACK_REQUEST;
    application.length = 3; memcpy(application.payload, "abc", 3);
    CHECK(recorded_send(&device, &application, 1, now) == BDB_JOIN_OK);
    for (iterations = 0; iterations < 256 && !device.application_done; iterations++) {
        drive();
        if (pending && !runtime.transport.active) deliver();
    }
    CHECK(iterations < 256 && app_sends == 2);
    CHECK(recorded_confirm(&device, &result) == BDB_JOIN_OK && result == NWK_APS_OK);
    application_from_peer(0x69); pending = 0;
    CHECK(recorded_receive(&device, mac_body, mac_length, 1, now) == BDB_JOIN_OK);
    drive();
    CHECK(recorded_application(&runtime.zdo, &received) == ZDO_RUNTIME_OK);
    CHECK(received.length == 1 && received.payload[0] == 0x69);
    CHECK(recorded_receive(&device, mac_body, mac_length, 1, now) == BDB_JOIN_SECURITY);
    address_request(0); deliver(); drain();
    CHECK(reply_seen && device.phase == BDB_JOIN_READY);
    expected_reply_cluster = 0x8123; expected_reply_status = ZDO_NODE_NOT_SUPPORTED;
    expected_reply_length = 2; reply_seen = 0;
    base_peer(0, 0, 0x123); peer_out.length = 1; peer_out.payload[0] = 0x72;
    seal_peer(0, 0, NULL, 1); deliver(); drain();
    CHECK(reply_seen && device.phase == BDB_JOIN_READY);
    expected_reply_cluster = 0; mute_timeout = 1; keepalive_sent(); old_deadline = runtime.zdo.deadline;
    update_network();
    CHECK(recorded_receive(&device, mac_body, mac_length, 1, now) == BDB_JOIN_OK);
    timeout_response(); pending = 0;
    CHECK(recorded_receive(&device, mac_body, mac_length, 1, now) == BDB_JOIN_MALFORMED);
    drive(); peer_pan = 0x2345; timeout_response(); pending = 0;
    CHECK(recorded_receive(&device, mac_body, mac_length, 1, now) == BDB_JOIN_STATE);
    for (iterations = 0; iterations < 16 && device.phase != BDB_JOIN_READY; iterations++) drive();
    CHECK(iterations < 16 && runtime.transport.ready && !runtime.zdo.query);
    now = old_deadline; drive(); keepalive_success();
    application.aps.flags = 0; phy_busy = 1;
    CHECK(recorded_send(&device, &application, 1, now) == BDB_JOIN_OK);
    drain();
    CHECK(device.phase == BDB_JOIN_READY && !leave_sent);
    CHECK(recorded_confirm(&device, &result) == BDB_JOIN_OK && result == NWK_APS_RADIO);
    now = device.keepalive;
    for (iterations = 0; iterations < 256 && device.phase == BDB_JOIN_READY; iterations++) drive();
    CHECK(iterations < 256);
    drain();
    CHECK(device.phase == BDB_JOIN_FAILED && !leave_sent && !device.member);
    CHECK(security_keys_status(&status) == SECURITY_KEYS_OK && status.phase == SECURITY_KEYS_VERIFIED);
    recorded_reset(0);
    CHECK(recorded_open() == SECURITY_KEYS_OK);
    CHECK(recorded_mac_init(&transmitter, 17, now) == MAC_TX_OK);
    CHECK(recorded_init(&device, now) == BDB_JOIN_OK);
    CHECK(recorded_start(&device, &transmitter, &config, now) == BDB_JOIN_RECOVERY_REQUIRED);
    CHECK(!device.member && device.workspace == BDB_JOIN_WORK_NONE);
    for (failure = 0; failure < 2; failure++) {
        puts(failure ? "CASE retained-radio-fault" : "CASE missing-network-key");
        test_case = failure ? 5 : 1; initial_now = 100; setup(); commission();
        CHECK(!runtime.transport.ready && !device.member);
        CHECK(device.phase == (failure ? BDB_JOIN_FAULT : BDB_JOIN_FAILED));
        CHECK(security_keys_status(&status) == SECURITY_KEYS_OK);
        CHECK(status.phase == (failure ? SECURITY_KEYS_VERIFIED : SECURITY_KEYS_LEFT));
    }
    printf("DONE %u\n", operations);
    return 0;
}
