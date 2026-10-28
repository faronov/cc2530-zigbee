/* SPDX-License-Identifier: BSD-3-Clause */
#define MAC_LINK_E2E_MAIN existing_link_corpus_main
#include "test_mac_link_e2e.c"
#define started report_owner_started
#include "../src/join_smoke.c"
#undef started

#undef CHECK
#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "report owner line %u: %s (phase=%u BDB=%u sent=%u TSN=%u)\n", \
        (unsigned)__LINE__, #c, S.phase, DEVICE.phase, \
        ZCL_SENSOR_STATE.sent, ZCL_SENSOR_STATE.tsn); exit(1); } } while (0)

/* Exercise the actual foreground owner and BDB functions, not replacements
 * returning successful queue/confirmation results. This is a component test,
 * not an assertion that the synthetic READY fixture completed admission. */
static void report_reset(void)
{
    security_keys_config_t identity;
    memset(&DEVICE, 0, sizeof(DEVICE));
    memset(&join_smoke_phase, 0, sizeof(join_smoke_phase));
    memset((void *)&S, 0, sizeof(S));
    memset(&identity, 0, sizeof(identity));
    memset(identity.own_ieee, 0x11, 8);
    memset(identity.tc_ieee, 0x22, 8);
    memcpy(DEVICE.config.association.extraction.local, identity.own_ieee, 8);
    DEVICE.config.transport.endpoint = 1;
    DEVICE.config.transport.profile = 0x0104;
    DEVICE.version = BDB_JOIN_VERSION;
    DEVICE.phase = BDB_JOIN_READY;
    DEVICE.workspace = BDB_JOIN_WORK_RUNTIME;
    DEVICE.work.runtime.zdo.version = ZDO_RUNTIME_VERSION;
    gate = JS_READY; S.phase = JS_READY;
    CHECK(zcl_sensor_init(&identity));
}

static void rejected(void)
{
    report_reset();
    /* A real BDB STATE rejection: READY's transport is not send-ready. */
    serve();
    CHECK(S.announce == (0x80u | BDB_JOIN_STATE));
    CHECK(!ZCL_SENSOR_STATE.sent && ZCL_SENSOR_STATE.basic);
    CHECK(!ZCL_SENSOR_STATE.tsn && !DEVICE.application_pending);
    CHECK(S.phase == JS_FAULT && S.reason == JS_BDB);
}

static void transmit_rejected(void)
{
    report_reset();
    DEVICE.work.runtime.transport.ready = 1;
    /* A real NWK/APS argument rejection, mapped by BDB to TRANSMIT_FAILED. */
    serve();
    CHECK(S.announce == (0x80u | BDB_JOIN_TRANSMIT_FAILED));
    CHECK(!ZCL_SENSOR_STATE.sent && ZCL_SENSOR_STATE.basic);
    CHECK(!ZCL_SENSOR_STATE.tsn && !DEVICE.application_pending);
    CHECK(S.phase == JS_FAULT && S.reason == JS_BDB);
}

static void bind_tick(void)
{
    zdo_runtime_t *zdo;
    report_reset();
    zdo = &DEVICE.work.runtime.zdo;
    DEVICE.application_pending = 1;
    ZCL_SENSOR_STATE.sent = 1;
    ZCL_SENSOR_STATE.pending = ZCL_SENSOR_BASIC;
    ZCL_SENSOR_STATE.identify = 3;
    zdo->last = ZCL_SENSOR_SECOND;
    zdo->application_ready = 1;
    zdo->application.nwk.destination = 0x4321;
    zdo->application.aps.cluster_id = ZDO_SRV_MGMT_BIND_REQUEST;
    zdo->application.length = 2;
    zdo->application.payload[0] = 7;
    serve();
    CHECK(zdo->response_pending && !zdo->application_ready);
    CHECK(!ZCL_SENSOR_STATE.sent && !ZCL_SENSOR_STATE.basic);
    CHECK(ZCL_SENSOR_STATE.tsn == 1 && ZCL_SENSOR_STATE.identify == 2);
    CHECK(ZCL_SENSOR_STATE.second == ZCL_SENSOR_SECOND);
    CHECK(ZCL_SENSOR_STATE.report[0].age == 1);
    CHECK(DEVICE.application_pending && S.phase == JS_READY);
}

static void bind_clock(void)
{
    unsigned n;
    zdo_runtime_t *zdo;
    report_reset();
    zdo = &DEVICE.work.runtime.zdo;
    DEVICE.application_pending = 1;
    ZCL_SENSOR_STATE.identify = 4;
    for (n = 1; n <= 3; n++) {
        zdo->last = n*ZCL_SENSOR_SECOND;
        zdo->response_pending = 0;
        zdo->application_ready = 1;
        zdo->application.nwk.destination = 0x4321;
        zdo->application.aps.cluster_id = ZDO_SRV_MGMT_BIND_REQUEST;
        zdo->application.length = 2;
        serve();
        CHECK(zdo->response_pending && !zdo->application_ready);
        CHECK(ZCL_SENSOR_STATE.report[0].age == n);
        CHECK(ZCL_SENSOR_STATE.identify == 4-n);
        CHECK(!ZCL_SENSOR_STATE.tsn && ZCL_SENSOR_STATE.basic);
    }
}

static void full(void)
{
    unsigned n;
    report_reset();
    DEVICE.work.runtime.transport.ready = 1;
    DEVICE.work.runtime.zdo.query = ZDO_RUNTIME_PARENT;
    for (n = 0; n < 3; n++) {
        DEVICE.work.runtime.zdo.last = n*ZCL_SENSOR_SECOND;
        serve();
        CHECK(!ZCL_SENSOR_STATE.sent && ZCL_SENSOR_STATE.basic);
        CHECK(!ZCL_SENSOR_STATE.tsn && !DEVICE.application_pending);
        CHECK(!DEVICE.work.runtime.zdo.application.length && S.phase == JS_READY);
        CHECK(ZCL_SENSOR_STATE.report[0].age == n);
    }
    ZCL_SENSOR_STATE.basic = 0;
    ZCL_SENSOR_STATE.report[0].last = 0;
    ZCL_SENSOR_STATE.report[0].age = 30;
    serve();
    CHECK(!ZCL_SENSOR_STATE.sent && !ZCL_SENSOR_STATE.tsn);
    CHECK(!ZCL_SENSOR_STATE.report[0].last && ZCL_SENSOR_STATE.report[0].age == 30);
}

static void wrap_wait(void)
{
    report_reset();
    DEVICE.work.runtime.transport.ready = 1;
    DEVICE.work.runtime.transport.version = NWK_APS_VERSION;
    DEVICE.work.runtime.transport.owner = &TX;
    DEVICE.work.runtime.transport.endpoint = 1;
    DEVICE.work.runtime.transport.profile = 0x0104;
    DEVICE.work.runtime.transport.wrap_wait = 1;
    DEVICE.work.runtime.transport.counter_until = ZCL_SENSOR_SECOND;
    serve();
    CHECK(!ZCL_SENSOR_STATE.sent && ZCL_SENSOR_STATE.basic);
    CHECK(!ZCL_SENSOR_STATE.tsn && !DEVICE.application_pending && S.phase == JS_READY);
    CHECK(DEVICE.work.runtime.transport.wrap_wait);
    DRIVER.now = DEVICE.work.runtime.zdo.last = ZCL_SENSOR_SECOND;
    serve();
    CHECK(DEVICE.application_pending && DEVICE.work.runtime.transport.queued);
    CHECK(ZCL_SENSOR_STATE.sent && !ZCL_SENSOR_STATE.tsn && ZCL_SENSOR_STATE.basic);
    CHECK(!DEVICE.work.runtime.transport.wrap_wait);
    CHECK(!DEVICE.work.runtime.transport.outgoing.payload[1]);
    serve();
    CHECK(!ZCL_SENSOR_STATE.sent && ZCL_SENSOR_STATE.tsn == 1 && !ZCL_SENSOR_STATE.basic);
    CHECK(DEVICE.application_pending && DEVICE.work.runtime.transport.queued);
}

static void failed_completion(void)
{
    report_reset();
    DEVICE.application_done = 1;
    DEVICE.application_result = NWK_APS_TIMEOUT;
    ZCL_SENSOR_STATE.basic = 0;
    ZCL_SENSOR_STATE.sent = 1;
    ZCL_SENSOR_STATE.pending = 0;
    ZCL_SENSOR_STATE.value = 2340;
    ZCL_SENSOR_STATE.report[0].age = 17;
    serve();
    CHECK(!DEVICE.application_done && !DEVICE.application_pending);
    CHECK(S.announce == NWK_APS_TIMEOUT && S.phase == JS_READY);
    CHECK(ZCL_SENSOR_STATE.tsn == 1 && !ZCL_SENSOR_STATE.sent);
    CHECK(ZCL_SENSOR_STATE.report[0].last == 2340 && !ZCL_SENSOR_STATE.report[0].age);
    CHECK(bdb_join_confirm(&DEVICE, &RESULT) == BDB_JOIN_STATE);
}

static void reply_full(void)
{
    ed_packet_t previous_reply;
    report_reset();
    DEVICE.application_pending = 1;
    DEVICE.work.runtime.zdo.last = ZCL_SENSOR_SECOND;
    DEVICE.work.runtime.zdo.response_pending = 1;
    memset(&DEVICE.work.runtime.zdo.response, 0x5a, sizeof(ed_packet_t));
    previous_reply = DEVICE.work.runtime.zdo.response;
    DEVICE.work.runtime.zdo.application_ready = 1;
    DEVICE.work.runtime.zdo.application.nwk.destination = 0x4321;
    DEVICE.work.runtime.zdo.application.aps.cluster_id = ZDO_SRV_MGMT_BIND_REQUEST;
    DEVICE.work.runtime.zdo.application.length = 2;
    serve();
    CHECK(DEVICE.work.runtime.zdo.response_result == NWK_APS_FULL);
    CHECK(!memcmp(&previous_reply, &DEVICE.work.runtime.zdo.response, sizeof(previous_reply)));
    CHECK(DEVICE.work.runtime.zdo.response_pending && !DEVICE.work.runtime.zdo.application_ready);
    CHECK(ZCL_SENSOR_STATE.report[0].age == 1 && DEVICE.application_pending);
}

static void configure_pending(void)
{
    static const uint8_t request[] = {
        0x00,0x70,0x06,0x00,0x00,0x00,0x29,0x05,0x00,0x0a,0x00,0x02,0x00
    };
    ed_packet_t previous_tx;
    report_reset();
    DEVICE.application_pending = 1;
    ZCL_SENSOR_STATE.basic = 0;
    ZCL_SENSOR_STATE.sent = 1;
    ZCL_SENSOR_STATE.pending = 0;
    ZCL_SENSOR_STATE.value = 2340;
    memset(&DEVICE.work.runtime.transport.outgoing, 0x5a, sizeof(ed_packet_t));
    previous_tx = DEVICE.work.runtime.transport.outgoing;
    DEVICE.work.runtime.zdo.application_ready = 1;
    DEVICE.work.runtime.zdo.application.nwk.destination = 0x4321;
    DEVICE.work.runtime.zdo.application.aps.profile_id = 0x0104;
    DEVICE.work.runtime.zdo.application.aps.destination_endpoint = 1;
    DEVICE.work.runtime.zdo.application.aps.cluster_id = 0x0402;
    DEVICE.work.runtime.zdo.application.length = sizeof(request);
    memcpy(DEVICE.work.runtime.zdo.application.payload, request, sizeof(request));
    serve();
    CHECK(DEVICE.work.runtime.zdo.response_pending && !DEVICE.work.runtime.zdo.application_ready);
    CHECK(ZCL_SENSOR_STATE.report[0].min == 5 && ZCL_SENSOR_STATE.report[0].max == 10);
    CHECK(ZCL_SENSOR_STATE.report[0].change == 2 && ZCL_SENSOR_STATE.report[0].last == 2340);
    CHECK(!ZCL_SENSOR_STATE.report[0].age && ZCL_SENSOR_STATE.tsn == 1);
    CHECK(DEVICE.application_pending && !ZCL_SENSOR_STATE.sent);
    CHECK(!memcmp(&previous_tx, &DEVICE.work.runtime.transport.outgoing, sizeof(previous_tx)));
    DEVICE.work.runtime.zdo.response_pending = 0;
    DEVICE.work.runtime.zdo.application_ready = 1;
    DEVICE.work.runtime.zdo.application.nwk.destination = 0x4321;
    DEVICE.work.runtime.zdo.application.aps.cluster_id = ZDO_SRV_UNBIND_REQUEST;
    DEVICE.work.runtime.zdo.application.length = 22;
    memset(DEVICE.work.runtime.zdo.application.payload+1, 0x11, 8);
    DEVICE.work.runtime.zdo.application.payload[0] = 0x71;
    DEVICE.work.runtime.zdo.application.payload[9] = 1;
    DEVICE.work.runtime.zdo.application.payload[10] = 0x02;
    DEVICE.work.runtime.zdo.application.payload[11] = 0x04;
    DEVICE.work.runtime.zdo.application.payload[12] = 3;
    memset(DEVICE.work.runtime.zdo.application.payload+13, 0x22, 8);
    DEVICE.work.runtime.zdo.application.payload[21] = 1;
    serve();
    CHECK(!ZCL_SENSOR_STATE.bound[0] && DEVICE.work.runtime.zdo.response_pending);
    CHECK(DEVICE.application_pending && ZCL_SENSOR_STATE.tsn == 1);
    CHECK(ZCL_SENSOR_STATE.report[0].last == 2340 && !ZCL_SENSOR_STATE.report[0].age);
    CHECK(!memcmp(&previous_tx, &DEVICE.work.runtime.transport.outgoing, sizeof(previous_tx)));
}

static void clock_wrap(void)
{
    report_reset();
    DEVICE.application_pending = 1;
    ZCL_SENSOR_STATE.second = UINT32_MAX-ZCL_SENSOR_SECOND+1u;
    ZCL_SENSOR_STATE.report[0].age = 0xffffu;
    ZCL_SENSOR_STATE.identify = 1;
    zcl_sensor_tick();
    CHECK(!ZCL_SENSOR_STATE.second && !ZCL_SENSOR_STATE.identify);
    CHECK(ZCL_SENSOR_STATE.report[0].age == 0xffffu && ZCL_SENSOR_STATE.report[1].age == 1);
}

int main(int argc, char **argv)
{
    CHECK(argc == 1 || argc == 2);
    if (argc == 1) {
        rejected(); transmit_rejected(); bind_tick(); bind_clock(); full(); wrap_wait();
        failed_completion(); reply_full(); configure_pending(); clock_wrap();
    }
    else if (!strcmp(argv[1], "rejected")) rejected();
    else if (!strcmp(argv[1], "bind-clock")) bind_clock();
    else { CHECK(!strcmp(argv[1], "bind-tick")); bind_tick(); }
    puts("Report foreground completion: PASS");
    return 0;
}
