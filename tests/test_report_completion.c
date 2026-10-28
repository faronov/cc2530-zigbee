/* SPDX-License-Identifier: BSD-3-Clause */
#define MAC_LINK_E2E_MAIN existing_link_corpus_main
#include "test_mac_link_e2e.c"
#define started report_owner_started
#include "../src/join_smoke.c"
#undef started

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

int main(int argc, char **argv)
{
    CHECK(argc == 1 || argc == 2);
    if (argc == 1) { rejected(); bind_tick(); bind_clock(); }
    else if (!strcmp(argv[1], "rejected")) rejected();
    else if (!strcmp(argv[1], "bind-clock")) bind_clock();
    else { CHECK(!strcmp(argv[1], "bind-tick")); bind_tick(); }
    puts("Report foreground completion: PASS");
    return 0;
}
