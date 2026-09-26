/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 * Offline BDB -> link driver -> real adapter/radio -> MMIO -> synthetic TC.
 * No progress fields or successful device service results are injected.
 */
#include "mac_link_driver.h"
#include "mac_link_peer.h"
#include "security_joint_model.h"
/* The original independent fixtures each define their libc ownership marker.
 * Keep the radio fixture marker separate; production uses the joint marker.
 */
#define _gptrput_PARM_2 link_radio_gptr
#define MAC_ADAPTER_MAIN adapter_component_main
#define scenario component_scenario
#include "test_mac_adapter.c"
#undef scenario
#undef _gptrput_PARM_2
extern uint8_t _gptrput_PARM_2;

static bdb_join_t device;
static bdb_join_config_t join_config;
static mac_link_driver_t driver;
static bdb_join_action_t blocked_action;
static ed_packet_t application;
static host_mmio_read_hook_t crypto_read;
static host_mmio_write_hook_t crypto_write;
static host_mmio_xread_hook_t crypto_xread;
static host_mmio_xwrite_hook_t crypto_xwrite;
static host_mmio_xaddress_hook_t crypto_address;
static host_mmio_cycles_hook_t crypto_cycles;
static uint8_t sent_body[125], sent_length, sent_wait, command_wait;
static uint8_t transport_sent, corrupt_sent, response_sent, app_sent, repeated_sent;
static unsigned scenario, e2e_checks, e2e_steps, random_inputs, received_frames;
static unsigned totals, total_peer_checks, total_steps, total_random;
static uint32_t test_prng;
static uint32_t bad_serial;
static uint8_t bad_checked;
enum {
    POLL_NO_ACK = 8, POLL_BUSY, CANCEL_ARM, STOP_ARM, ACK_CANCEL_ARM,
    RETRY_CANCEL, QUERY_TIMEOUT_ARM, E2E_CASES
};
static unsigned poll_attempts, closed_checked, race_started, race_disarmed, race_completed;
static unsigned race_random, race_tx, retire_wait, timeout_started;
static uint32_t scan_watermark, race_generation;
#define CHECK(c) do { e2e_checks++; if (!(c)) { \
    fprintf(stderr, "E2E case%u step%u line%u: %s\n" \
        "BDB=%u result=%u driver=%u adapter=%u/%u MAC=%u/%u action=%u " \
        "child=%u radio=%u now=%lu join=%u poll=%u\n", scenario,e2e_steps, \
        (unsigned)__LINE__,#c,device.phase,device.result,driver.fault, \
        mac_adapter_diagnostic()->phase,mac_adapter_diagnostic()->fault, \
        adapter_tx.engine.phase,adapter_tx.engine.outcome,driver.action.kind, \
        driver.action.data.association.kind,driver.radio.control.kind, \
        (unsigned long)driver.now,device.work.association.context.phase, \
        device.work.association.context.poll.control.phase); \
    fprintf(stderr,"adapter_rc=%u radio_rc=%u lower=%u/%u\n",driver.adapter_result, \
        mac_adapter_diagnostic()->radio_result,mac_attempt_diagnostic()->phase, \
        radio_autoack_diagnostic()->result); \
    fprintf(stderr,"ready=%u held=%u goal=%u normal=%u\n",mac_adapter_diagnostic()->ready, \
        mac_adapter_diagnostic()->held,mac_adapter_diagnostic()->goal,mac_adapter_diagnostic()->normal_rx); exit(1); \
    } } while (0)

static uint16_t joint_address(const volatile void *p)
{
    if (p == &driver.config) return 0x1900;
    if (p == &driver.clock) return 0x1920;
    if (p == &driver.end) return 0x1930;
    if (p == &driver.radio) return 0x1940;
    if (p == &_gptrput_PARM_2) return helper;
    if (p == &adapter_tx || p == &adapter_action || p == &adapter_now || p == &close_at ||
        p == &mac_adapter_reserved_end || p == _mullong_PARM_2 ||
        p == &mac_adapter_receipt || p == &mac_adapter_receipt.frame ||
        p == mac_adapter_receipt.frame.body || p == &mac_adapter_live ||
        p == &mac_radio_shared_end || p == &mac_radio_reserved_end || p == &mac_attempt_reserved_end ||
        p == &mac_radio_raw || p == &mac_radio_config || p == &mac_attempt_raw ||
        p == &mac_attempt_raw.before || p == &mac_attempt_raw.armed || p == &mac_attempt_raw.tx ||
        p == &mac_attempt_raw.rx || p == &mac_attempt_raw.last || p == &mac_attempt_raw.frame ||
        p == &mac_attempt_first || p == &config.value || p == &frame.value ||
        p == &radio_autoack_reserved_end || p == __memcpy_PARM_2 ||
        p == &mac_radio_handoff_clock || p == &mac_radio_handoff_clock.before ||
        p == &mac_radio_handoff_clock.after || p == &mac_radio_handoff_clock.last)
        return adapter_address(p);
    return crypto_address(p);
}

static uint8_t joint_load(uint8_t a, uint8_t value)
{
    /* Actual AES transaction completion, including while foreground code
     * reads common IRQ ownership. The radio clock is shared and keeps running.
     */
    if (a == SOC_IEN0_ADDRESS) (void)crypto_read(a, value);
    return adapter_load(a, value);
}

static void joint_store(uint8_t a, uint8_t before, uint8_t value)
{
    if (a == SOC_RFST_ADDRESS && value == 0xea) {
        body_length = tx_count-1u;
        CHECK(body_length && body_length <= 125);
        memcpy(sent_body, tx_fifo+1, body_length);
        sent_length = (uint8_t)body_length; sent_wait = 1;
        response_fcf = (sent_body[0] & 7u) == MAC_FRAME_COMMAND &&
            sent_body[sent_length-1] == MAC_COMMAND_DATA_REQUEST ? 0x12 : 2;
        /* Peripheral inputs only: fail the first association's POLL, or
         * the first unicast query attempt used by the retry/cancel case. */
        replies = !((scenario == POLL_NO_ACK && device.attempts == 1 && response_fcf == 0x12) ||
                    (scenario == RETRY_CANCEL && device.phase == BDB_JOIN_NODE && !race_started));
        cca_clear = !(scenario == POLL_BUSY && device.attempts == 1 && response_fcf == 0x12);
        if ((scenario == POLL_NO_ACK || scenario == POLL_BUSY) &&
            device.attempts == 1 && response_fcf == 0x12) poll_attempts++;
    }
    if (a == 0xc7 || (a >= 0xd1 && a <= 0xd6) || a == 0xb3 || a == 0x98) {
        advance_clocks(mmio_clocks);
        crypto_write(a, before, value);
    } else adapter_store(a, before, value);
}

static uint8_t joint_xload(uint16_t a)
{
    if ((a >= 0x6270 && a <= 0x6277) || a >= 0xe800) {
        advance_clocks(mmio_clocks);
        return crypto_xread(a);
    }
    return handoff_xload(a);
}

static void joint_xstore(uint16_t a, uint8_t value)
{
    if (a >= 0x6270 && a <= 0x6273) {
        advance_clocks(mmio_clocks);
        crypto_xwrite(a, value);
    } else if (mode == 5 && ((a >= 0x6172 && a <= 0x6175) || a == 0x618f)) {
        handoff_tick(); advance_clocks(mmio_clocks);
        CHECK(xwrite_count == 1 && xwrites[0].address == a && xwrites[0].value == value);
        CHECK(!count && !packets && !XR(0x618b) && !(XR(0x6193) & 0x27) && !ack_active);
        xwrite_count = 0; logs(); XR(a) = value;
    } else handoff_xstore(a, value);
}

static void joint_cycles(uint8_t n)
{
    if (n == 9) { advance_clocks(mmio_clocks*n); crypto_cycles(n); }
    else cycles(n);
}

static void cold_start(void)
{
    memset(&driver, 0, sizeof(driver)); /* Fresh modeled device reset only. */
    security_joint_reset(1);
    crypto_read = host_mmio_read_hook; crypto_write = host_mmio_write_hook;
    crypto_xread = host_mmio_xread_hook; crypto_xwrite = host_mmio_xwrite_hook;
    crypto_address = host_mmio_xaddress_hook; crypto_cycles = host_mmio_cycles_hook;
    adapter_reset();
    SOC_ENCCS = 8; SOC_S0CON = 0xa4; SOC_IRCON = 0xbe; SOC_MEMCTR = 2;
    host_mmio_read_hook = joint_load; host_mmio_write_hook = joint_store;
    host_mmio_xread_hook = joint_xload; host_mmio_xwrite_hook = joint_xstore;
    host_mmio_xaddress_hook = joint_address; host_mmio_cycles_hook = joint_cycles;
    memcpy(config.value.ieee, link_identity.own_ieee, 8);
    config.value.pan = config.value.short_address = 0xffff; config.value.channel = 11;
    link_peer_setup(&join_config);
    CHECK(mac_adapter_init(&config.value, bound, cap) == MAC_ADAPTER_OK);
    epoch_origin = ((uint64_t)mac_radio_epoch.periods-mac_radio_epoch.symbols)*512u;
    close_receiver();
    CHECK(mac_adapter_now(bound, cap, &adapter_now) == MAC_ADAPTER_OK);
    CHECK(mac_tx_interval_init(&adapter_tx, 255, adapter_now.symbols) == MAC_TX_OK);
    CHECK(bdb_join_init(&device, adapter_now.symbols) == BDB_JOIN_OK);
    CHECK(bdb_join_start(&device, &adapter_tx, &join_config, adapter_now.symbols) == BDB_JOIN_OK);
    {
        mac_link_driver_t before = driver;
        unsigned old_accesses = accesses;
        config.value.ieee[0] ^= 0x80;
        CHECK(mac_link_driver_init(&driver, &device, &config.value, bound, cap) == MAC_LINK_DRIVER_STATE);
        config.value.ieee[0] ^= 0x80;
        CHECK(!memcmp(&before, &driver, sizeof(driver)) && accesses == old_accesses);
        device.config.association.saved.filter = 1;
        CHECK(mac_link_driver_init(&driver, &device, &config.value, bound, cap) == MAC_LINK_DRIVER_STATE);
        device.config.association.saved.filter = 0;
        CHECK(!memcmp(&before, &driver, sizeof(driver)) && accesses == old_accesses);
    }
    CHECK(mac_link_driver_init(&driver, &device, &config.value, bound, cap) == MAC_LINK_DRIVER_OK);
    {
        mac_link_driver_t before = driver;
        CHECK(mac_link_driver_init(&driver, &device, &config.value, bound, cap) == MAC_LINK_DRIVER_STATE);
        CHECK(!memcmp(&before, &driver, sizeof(driver)));
    }
    sent_wait = command_wait = transport_sent = corrupt_sent = response_sent = app_sent = repeated_sent = 0;
    random_inputs = received_frames = 0; test_prng = 0x13579bdu;
    bad_serial = 0; bad_checked = 0;
    poll_attempts = closed_checked = race_started = race_disarmed = race_completed = 0;
    race_random = race_tx = retire_wait = timeout_started = 0;
    scan_watermark = race_generation = 0;
}

/* Complete a genuine modeled PPDU. Its SFD is high during the body, sticky
 * SFD evidence remains afterwards, the RX FIFO contains PHR/body/RSSI/CRC,
 * and AUTOACK is decided from actual installed registers and the real header.
 * No FCS verification is claimed: crc is synthetic peripheral truth.
 */
static void air_frame(const uint8_t *body, uint8_t length, uint8_t crc)
{
    unsigned first = tail, i;
    mac_frame_info_t mh;
    uint8_t addressed = 0;
    CHECK(XR(0x618b) && mode == 2 && !ack_active && !packets);
    XR(0x6193) |= 0x20; SOC_RFIRQF0 |= 2;
    advance_clocks((12u+2u*(length+2u))*512u);
    XR(0x6193) &= (uint8_t)~0x20;
    enqueue((uint8_t)(length+2), crc ? 0xe9 : 0x69, 0);
    for (i = 0; i < length; i++) fifo[(first+1+i)&127] = body[i];
    if (mac_frame_decode(body, length, &mh) == MAC_CODEC_OK &&
        mh.header.destination_pan == ((uint16_t)XR(0x6172) | ((uint16_t)XR(0x6173)<<8))) {
        if (mh.header.destination_mode == MAC_ADDRESS_SHORT)
            addressed = mh.header.destination[0] == XR(0x6174) &&
                mh.header.destination[1] == XR(0x6175) &&
                !(mh.header.destination[0] == 255 && mh.header.destination[1] == 255);
        else if (mh.header.destination_mode == MAC_ADDRESS_EXTENDED) {
            addressed = 1;
            for (i = 0; i < 8; i++) if (mh.header.destination[i] != XR(0x616a+i)) addressed = 0;
        }
        if (crc && addressed && (XR(0x6189) & 0x20) && (XR(0x6180) & 1) &&
            (mh.header.flags & MAC_FLAG_ACK_REQUEST) &&
            (mh.header.type == MAC_FRAME_COMMAND || mh.header.type == MAC_FRAME_DATA))
            start_ack();
    }
    received_frames++;
}

static void peer_progress(void)
{
    mac_frame_info_t mh;
    uint32_t saved_clocks;
    if (sent_wait && !tx_remaining && (SOC_RFIRQF1 & 2)) {
        CHECK(sent_length == tx_count-1u && !memcmp(sent_body, tx_fifo+1, sent_length));
        /* Coordinator computations are an external oracle, not device CPU
         * execution time. All DUT calls keep the real timer running. */
        saved_clocks = mmio_clocks; mmio_clocks = 0;
        link_peer_transmitted(sent_body, sent_length);
        mmio_clocks = saved_clocks;
        CHECK(mac_frame_decode(sent_body, sent_length, &mh) == MAC_CODEC_OK);
        if (mh.header.type == MAC_FRAME_COMMAND)
            command_wait = sent_body[mh.payload_offset];
        sent_wait = 0;
    }
    if (!XR(0x618b) || mode != 2 || ack_active || packets || mac_adapter_diagnostic()->held)
        return;
    if (command_wait == MAC_COMMAND_BEACON_REQUEST &&
        device.phase == BDB_JOIN_SCANNING && device.work.scan.phase == MAC_SCAN_RX) {
        command_wait = 0;
        if (scenario != 1) air_frame(link_beacon, link_beacon_length, 1);
    } else if (command_wait == MAC_COMMAND_DATA_REQUEST &&
        mac_adapter_diagnostic()->normal_rx && device.phase == BDB_JOIN_ASSOCIATING &&
        device.work.association.context.poll.control.phase == MAC_POLL_RECEIVE &&
        adapter_tx.engine.phase == MAC_TX_DONE) {
        if (scenario == 4) advance_clocks(320u*512u);
        if (scenario == 2) {
            link_response[link_response_length-3] = 255;
            link_response[link_response_length-2] = 255;
            link_response[link_response_length-1] = 1;
        }
        air_frame(link_response, link_response_length, 1);
        command_wait = 0; response_sent++;
    } else if (mac_adapter_diagnostic()->normal_rx && device.phase >= BDB_JOIN_WAIT_KEY &&
               device.phase < BDB_JOIN_UPDATING && adapter_tx.engine.phase == MAC_TX_IDLE) {
        if (device.phase == BDB_JOIN_WAIT_KEY && !transport_sent && scenario != 3) {
            saved_clocks = mmio_clocks; mmio_clocks = 0;
            link_peer_transport(scenario == 7 || scenario == ACK_CANCEL_ARM);
            mmio_clocks = saved_clocks; transport_sent = 1;
        }
        if (link_peer_pending) {
            if (!corrupt_sent) {
                bad_serial = mac_adapter_diagnostic()->frames + 1u;
                air_frame(link_peer_body, link_peer_length, 0); corrupt_sent = 1;
            } else {
                air_frame(link_peer_body, link_peer_length, 1); link_peer_pending = 0;
            }
        } else if (scenario == 7 && !repeated_sent && device.work.runtime.transport.receive_ready) {
            /* A second real RX arrives while the first frame's genuine
             * priority APS ACK blocks admission. No packet slot or successful
             * admission is injected: retain this actual FIFO head. */
            air_frame(link_peer_body, link_peer_length, 1);
            repeated_sent = 1;
        }
    }
}

/* Direct cancel/stop cases mimic zdo_runtime's real callers, without writing
 * transport progress. QUERY_TIMEOUT_ARM instead exercises that actual caller.
 */
static void race_before(void)
{
    nwk_aps_t *t = &device.work.runtime.transport;
    if (device.workspace != BDB_JOIN_WORK_RUNTIME || race_started) return;
    if (scenario == QUERY_TIMEOUT_ARM && !timeout_started && device.phase == BDB_JOIN_NODE &&
        device.work.runtime.zdo.query && t->queued && !t->active) {
        advance_clocks((device.work.runtime.zdo.deadline-driver.now)*512u);
        timeout_started = 1;
    }
    if (scenario == RETRY_CANCEL && device.phase == BDB_JOIN_NODE &&
        adapter_tx.engine.phase == MAC_TX_STOPPING && adapter_tx.engine.outcome == MAC_TX_NO_ACK &&
        mac_adapter_diagnostic()->phase == MAC_ADAPTER_OFF && retire_wait == 2) {
        CHECK(nwk_aps_cancel(t, driver.now) == NWK_APS_OK);
    } else if (driver.action.kind == BDB_JOIN_ACTION_TX &&
               driver.action.data.tx.control.kind == NWK_APS_ACTION_ARM &&
               ((device.phase == BDB_JOIN_ANNOUNCING &&
                 (scenario == CANCEL_ARM || scenario == STOP_ARM || scenario == ACK_CANCEL_ARM)) ||
                (scenario == QUERY_TIMEOUT_ARM && t->cancel))) {
        CHECK(adapter_tx.engine.phase == MAC_TX_DRAW && t->queued && t->active);
        CHECK(!!t->active_ack == (scenario == ACK_CANCEL_ARM));
        if (scenario == STOP_ARM) CHECK(nwk_aps_stop(t, driver.now) == NWK_APS_OK);
        else if (scenario != QUERY_TIMEOUT_ARM) CHECK(nwk_aps_cancel(t, driver.now) == NWK_APS_OK);
        else CHECK(timeout_started && device.work.runtime.zdo.result == ZDO_RUNTIME_TIMEOUT);
    } else return;
    race_started = 1; race_generation = adapter_tx.engine.generation;
    race_random = random_inputs; race_tx = tx_started;
}

static void race_after(uint8_t old_action, uint8_t old_radio, uint8_t old_phase)
{
    nwk_aps_t *t = &device.work.runtime.transport;
    if (scenario == RETRY_CANCEL && !race_started &&
        old_phase == MAC_ADAPTER_OFF && adapter_tx.engine.phase == MAC_TX_STOPPING &&
        adapter_tx.engine.outcome == MAC_TX_NO_ACK) {
        /* QUIESCE was really accepted from OFF; the next adapter call only
         * settles its retirement bound. Cancel before the following call
         * publishes RETIRED, not before an intervening NULL consumer step. */
        if (old_radio == MAC_TX_ACTION_QUIESCE) retire_wait = 1;
        else if (retire_wait == 1 && !driver.source.source.kind) retire_wait = 2;
    }
    if (!race_started || adapter_tx.engine.generation != race_generation) return;
    CHECK(!driver.fault);
    if (scenario == RETRY_CANCEL && adapter_tx.engine.phase == MAC_TX_DRAW) {
        CHECK(driver.source.source.kind == MAC_TX_EVENT_RETIRED);
        CHECK(driver.action.kind != BDB_JOIN_ACTION_TX ||
              driver.action.data.tx.control.kind != NWK_APS_ACTION_ARM);
    }
    if (old_action == NWK_APS_ACTION_DISARM && !driver.action.kind) {
        CHECK(adapter_tx.engine.phase == MAC_TX_DONE && t->disarmed);
        if (scenario != ACK_CANCEL_ARM) {
            CHECK(adapter_tx.engine.outcome == MAC_TX_CANCELLED);
            CHECK(mac_adapter_diagnostic()->phase == MAC_ADAPTER_OFF &&
                  !mac_adapter_diagnostic()->ready && !mac_adapter_diagnostic()->goal);
            CHECK(random_inputs == race_random && tx_started == race_tx);
        } else CHECK(adapter_tx.engine.outcome == MAC_TX_ACKED &&
                     t->cancel && random_inputs == race_random+1 && tx_started == race_tx+1);
        race_disarmed = 1;
    }
    if (race_disarmed && !t->active) {
        if (scenario == ACK_CANCEL_ARM && t->cancel) return;
        CHECK(t->result == NWK_APS_CANCELLED);
        if (scenario == ACK_CANCEL_ARM) CHECK(t->reply_result == MAC_TX_ACKED && !t->reply);
        race_completed = 1;
    }
}

static void tick_driver(void)
{
    mac_link_driver_result_t result;
    uint32_t last = driver.now;
    uint8_t old_action = 0, old_phase = mac_adapter_diagnostic()->phase, poll_close = 0;
    uint8_t old_radio = driver.radio.control.kind;
    if (scenario >= CANCEL_ARM) race_before();
    if (scenario >= CANCEL_ARM && driver.action.kind == BDB_JOIN_ACTION_TX)
        old_action = driver.action.data.tx.control.kind;
    if (scenario == POLL_NO_ACK || scenario == POLL_BUSY) {
        if (!scan_watermark && device.phase == BDB_JOIN_ASSOCIATING)
            scan_watermark = driver.closed_through;
        poll_close = device.phase == BDB_JOIN_ASSOCIATING && device.attempts == 1 &&
            driver.action.kind == BDB_JOIN_ACTION_ASSOCIATION &&
            driver.action.data.association.kind == MAC_JOIN_ACTION_CLOSE;
    }
    if (driver.action.kind == BDB_JOIN_ACTION_TX &&
        (driver.action.data.tx.control.kind == NWK_APS_ACTION_ARM ||
         driver.action.data.tx.control.kind == NWK_APS_ACTION_DISARM)) {
        nwk_aps_t before = device.work.runtime.transport;
        mac_tx_interval_t tx_before = adapter_tx;
        uint32_t generation = driver.action.data.tx.control.generation;
        uint8_t retry = driver.action.data.tx.control.retry, nb = driver.action.data.tx.control.nb;
        if (driver.action.data.tx.control.kind == NWK_APS_ACTION_ARM) {
            CHECK(bdb_join_armed(&device, generation+1u, retry, nb) == BDB_JOIN_STATE);
            CHECK(bdb_join_armed(&device, generation, (uint8_t)(retry+1), nb) == BDB_JOIN_STATE);
            CHECK(bdb_join_armed(&device, generation, retry, (uint8_t)(nb+1)) == BDB_JOIN_STATE);
            CHECK(bdb_join_disarmed(&device, generation, retry, nb) == BDB_JOIN_STATE);
        } else {
            CHECK(bdb_join_disarmed(&device, generation+1u, retry, nb) == BDB_JOIN_STATE);
            CHECK(bdb_join_disarmed(&device, generation, (uint8_t)(retry+1), nb) == BDB_JOIN_STATE);
            CHECK(bdb_join_disarmed(&device, generation, retry, (uint8_t)(nb+1)) == BDB_JOIN_STATE);
            CHECK(bdb_join_armed(&device, generation, retry, nb) == BDB_JOIN_STATE);
        }
        CHECK(!memcmp(&before, &device.work.runtime.transport, sizeof(before)) &&
              !memcmp(&tx_before, &adapter_tx, sizeof(tx_before)));
    }
    e2e_steps++;
    /* Finite foreground latency; no consumer timestamp is supplied here.
     * Long idle security waits advance the physical Timer2 in bounded chunks. */
    advance_clocks((device.phase == BDB_JOIN_WAIT_KEY && scenario == 3 ? 1000u : 4u)*512u);
    handoff_tick();
    handoff_started = mac_adapter_diagnostic()->phase == MAC_ADAPTER_RETIRING &&
        mac_adapter_diagnostic()->policy == MAC_ADAPTER_KEEP_AUTOACK &&
        adapter_tx.engine.outcome == MAC_TX_ACKED;
    if (handoff_started) handoff_configured = 0;
    if (scenario == 7) blocked_action = driver.action;
    result = mac_link_driver_step(&driver);
    handoff_started = 0;
    if (scenario >= CANCEL_ARM) race_after(old_action, old_radio, old_phase);
    if (poll_close && driver.event.kind == BDB_JOIN_EVENT_ASSOCIATION &&
        driver.event.data.association.kind == MAC_JOIN_CLOSED) {
        const mac_poll_t *p = &device.work.association.context.poll;
        CHECK(driver.event.data.association.through == MAC_LINK_FLOOR(mac_adapter_diagnostic()->watermark));
        CHECK(driver.closed_through == MAC_LINK_FLOOR(mac_adapter_observation()->through) &&
              driver.closed_through != scan_watermark);
        CHECK((uint32_t)(p->record.stamp-driver.closed_through) < MAC_TX_HALF &&
              p->record.stamp != driver.closed_through);
        CHECK(p->record.cause == MAC_POLL_TX_RESULT &&
              p->record.protocol == (scenario == POLL_NO_ACK ? MAC_POLL_NO_ACK : MAC_POLL_CHANNEL_ACCESS));
        CHECK(p->control.closed && !p->control.reason && !p->control.cleanup_error &&
              device.work.association.context.phase != MAC_JOIN_FAULT && !driver.fault);
        closed_checked++;
    }
    CHECK((uint32_t)(driver.now-last) < MAC_TX_HALF);
    CHECK((uint64_t)driver.now*512u <= model_clocks-epoch_origin);
    CHECK(adapter_tx.engine.tx_end == 0);
    if (driver.source.source.kind >= MAC_TX_EVENT_SENT_INTERVAL &&
        mac_adapter_observation()->tx.source.kind == driver.source.source.kind)
        CHECK(!memcmp(&driver.source, &mac_adapter_observation()->tx, sizeof(driver.source)));
    if (bad_serial && driver.rx_serial == bad_serial && !bad_checked) {
        CHECK(driver.consumer_result == BDB_JOIN_MALFORMED && device.phase == BDB_JOIN_WAIT_KEY);
        bad_checked = 1;
    }
    if (result == MAC_LINK_DRIVER_RANDOM) {
        mac_link_driver_t before = driver;
        CHECK(driver.random_wait && !driver.random_ready && adapter_tx.engine.phase == MAC_TX_DRAW_WAIT);
        if (scenario == 6) {
            /* Do not supply a byte. Let actual time reach the real deadline;
             * the driver must expire/unprepare, never invent RANDOM. */
            advance_clocks((adapter_tx.engine.deadline-driver.now+1u)*512u);
            return;
        }
        CHECK(mac_link_driver_random(&driver, adapter_tx.engine.generation+1u,
            adapter_tx.engine.retries, adapter_tx.engine.nb, 0) == MAC_LINK_DRIVER_STATE);
        CHECK(!memcmp(&before, &driver, sizeof(driver)));
        /* Deterministic TEST PRNG; explicitly NOT a production entropy source. */
        test_prng ^= test_prng << 13; test_prng ^= test_prng >> 17; test_prng ^= test_prng << 5;
        CHECK(mac_link_driver_random(&driver, adapter_tx.engine.generation,
            adapter_tx.engine.retries, adapter_tx.engine.nb, (uint8_t)test_prng) == MAC_LINK_DRIVER_OK);
        CHECK(mac_link_driver_random(&driver, adapter_tx.engine.generation,
            adapter_tx.engine.retries, adapter_tx.engine.nb, 0) == MAC_LINK_DRIVER_STATE);
        random_inputs++;
    } else CHECK(result == MAC_LINK_DRIVER_WAIT || result == MAC_LINK_DRIVER_FINISHED ||
                 ((scenario == 5 || scenario == 7) && driver.fault));
    if (!driver.fault) peer_progress();
}

static void run(unsigned selected)
{
    unsigned faulted = 0;
    scenario = selected; e2e_checks = e2e_steps = 0;
    cold_start();
    while (device.phase != BDB_JOIN_READY && device.phase < BDB_JOIN_FAILED && !driver.fault) {
        CHECK(e2e_steps < 5000);
        if (scenario == 5 && !faulted && mac_adapter_diagnostic()->phase == MAC_ADAPTER_CLOSING) {
            SOC_RFERRF = 4; faulted = 1;
        }
        tick_driver();
    }
    if (scenario == 0 || scenario == POLL_NO_ACK || scenario == POLL_BUSY ||
        scenario == QUERY_TIMEOUT_ARM) {
        if (device.phase != BDB_JOIN_READY)
            fprintf(stderr, "scan reason=%u unscanned=%lu candidates=%u peerTX=%u cmd=%u rx=%u through=%lu\n",
                device.scan_result.reason,(unsigned long)device.scan_result.unscanned,
                device.scan_result.candidates,link_peer_tx,command_wait,received_frames,
                (unsigned long)driver.closed_through);
        if (device.phase == BDB_JOIN_FAULT)
            fprintf(stderr, "assoc reason=%u cleanup=%u pollreason=%u cleanup=%u A=%lu B=%lu rxend=%lu\n",
                device.work.association.context.record.reason,device.work.association.context.record.cleanup_error,
                device.work.association.context.poll.control.reason,device.work.association.context.poll.control.cleanup_error,
                (unsigned long)device.work.association.context.poll.control.ack_end,
                (unsigned long)device.work.association.context.poll.control.ack_upper,
                (unsigned long)device.work.association.context.poll.control.receive_end);
        if (device.result == BDB_JOIN_ASSOCIATION_FAILED)
            fprintf(stderr,"record reason=%u pollreason=%u out=%u assocrc=%u status=%u stamp=%lu\n",
                device.record.reason,device.record.poll_reason,device.record.association.outcome,
                device.record.association_rc,device.record.association.status,
                (unsigned long)device.record.poll.stamp);
        if (device.result == BDB_JOIN_ASSOCIATION_FAILED)
            fprintf(stderr,"A=%lu B=%lu accept=%lu sent=%u cphase=%u cstart=%lu\n",
                (unsigned long)device.work.association.context.poll.control.ack_end,
                (unsigned long)device.work.association.context.poll.control.ack_upper,
                (unsigned long)device.work.association.context.poll.control.accept_end,response_sent,
                device.work.association.context.association.phase,
                (unsigned long)device.work.association.context.association.last);
        CHECK(device.phase == BDB_JOIN_READY && device.member && device.work.runtime.transport.ready);
        link_peer_verify();
        link_peer_application(&application);
        CHECK(bdb_join_send(&device, &application, 1, driver.now) == BDB_JOIN_OK);
        while (!device.application_done && !driver.fault) { CHECK(e2e_steps < 6000); tick_driver(); }
        CHECK(!driver.fault && link_peer_app == 1);
        CHECK(bdb_join_confirm(&device, &app_sent) == BDB_JOIN_OK && app_sent == NWK_APS_OK);
        CHECK(response_sent == 1 && automatic_acks >= 1 &&
              random_inputs == link_peer_tx+(scenario == POLL_BUSY ? 5u : 0u) &&
              driver.gaps && bad_checked);
        /* A real periodic parent keepalive, not a fabricated parent response. */
        advance_clocks((device.keepalive-driver.now)*512u);
        while (link_peer_parent < 2 || device.work.runtime.zdo.query || link_peer_pending ||
               packets || mac_adapter_diagnostic()->held) {
            CHECK(e2e_steps < 6500); tick_driver();
        }
        CHECK(device.phase == BDB_JOIN_READY && device.member && link_peer_parent == 2);
        if (scenario == POLL_NO_ACK || scenario == POLL_BUSY)
            CHECK(closed_checked == 1 && poll_attempts == (scenario == POLL_NO_ACK ? 4u : 5u));
        if (scenario == QUERY_TIMEOUT_ARM)
            CHECK(race_started && race_disarmed && race_completed);
    } else if (scenario == 1) {
        CHECK(device.phase == BDB_JOIN_FAILED && device.result == BDB_JOIN_NO_PARENT);
        CHECK(!device.scan_result.candidates && !device.scan_result.unscanned);
    } else if (scenario == 2) {
        CHECK(device.phase == BDB_JOIN_FAILED && device.result == BDB_JOIN_ASSOCIATION_FAILED);
        CHECK(response_sent == 3 && device.record.association.status == 1 && automatic_acks == 3);
    } else if (scenario == 3) {
        CHECK(device.phase == BDB_JOIN_FAILED && device.result == BDB_JOIN_KEY_TIMEOUT);
        CHECK(!transport_sent && !device.member);
    } else if (scenario == 4) {
        CHECK(device.phase == BDB_JOIN_FAILED && device.result == BDB_JOIN_ASSOCIATION_FAILED);
        CHECK(device.record.reason == MAC_JOIN_TIMING_UNCERTAIN &&
              device.record.association.outcome != MAC_ASSOCIATION_RESPONSE &&
              !device.record.poll.protocol && !device.record.cleanup_error);
    } else if (scenario == 5) {
        mac_link_driver_t before = driver;
        CHECK(faulted && driver.fault == MAC_LINK_DRIVER_RADIO &&
              mac_adapter_diagnostic()->phase == MAC_ADAPTER_FAULT);
        CHECK(mac_link_driver_step(&driver) == MAC_LINK_DRIVER_RADIO &&
              !memcmp(&driver, &before, sizeof(driver)));
        CHECK(device.owner == &adapter_tx && device.phase != BDB_JOIN_READY);
    } else if (scenario == 6) {
        CHECK(device.phase == BDB_JOIN_FAILED && device.result == BDB_JOIN_NO_PARENT);
        CHECK(!random_inputs && !tx_started && !link_peer_tx &&
              mac_adapter_diagnostic()->phase == MAC_ADAPTER_OFF &&
              adapter_tx.engine.phase == MAC_TX_IDLE);
    } else if (scenario == 7) {
        mac_link_driver_t before = driver;
        const mac_adapter_observation_t *o = mac_adapter_observation();
        CHECK(scenario == 7 && repeated_sent && driver.fault == MAC_LINK_DRIVER_CONSUMER);
        CHECK(driver.consumer_result == BDB_JOIN_FULL && device.work.runtime.transport.reply);
        CHECK(mac_adapter_diagnostic()->ready && o->kind == MAC_ADAPTER_RX_EVENT &&
              o->rx_serial == driver.rx_serial+1u && o->frame->length == link_peer_length &&
              !memcmp(o->frame->body, link_peer_body, link_peer_length));
        CHECK(!memcmp(&blocked_action, &driver.action, sizeof(blocked_action)));
        CHECK(mac_link_driver_step(&driver) == MAC_LINK_DRIVER_CONSUMER &&
              !memcmp(&driver, &before, sizeof(driver)) && device.owner == &adapter_tx);
    } else {
        CHECK(race_started && race_disarmed && race_completed && !driver.fault);
        CHECK(device.phase == BDB_JOIN_FAILED &&
              device.result == (scenario == RETRY_CANCEL ? BDB_JOIN_TC_FAILED : BDB_JOIN_TRANSMIT_FAILED) &&
              !device.cleanup_error && !device.work.runtime.transport.active &&
              adapter_tx.engine.phase == MAC_TX_IDLE);
    }
    printf("E2E case%u: BDB=%u/%u driver=%u steps=%u checks=%u peer=%u TX=%u random=%u PASS\n",
        scenario,device.phase,device.result,driver.fault,e2e_steps,e2e_checks,
        link_peer_checks,link_peer_tx,random_inputs);
    totals += e2e_checks; total_peer_checks += link_peer_checks;
    total_steps += e2e_steps; total_random += random_inputs;
}

#ifndef MAC_LINK_E2E_MAIN
#define MAC_LINK_E2E_MAIN main
#endif
int MAC_LINK_E2E_MAIN(void)
{
    unsigned i;
    for (i = 0; i < E2E_CASES; i++) run(i);
    printf("MAC link E2E: %u cases, %u checks + %u peer checks, %u steps, %u test RANDOM inputs PASS.\n",
           E2E_CASES,totals,total_peer_checks,total_steps,total_random);
    return 0;
}
