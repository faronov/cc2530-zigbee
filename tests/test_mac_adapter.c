/* SPDX-License-Identifier: BSD-3-Clause
 * Original synthetic peripheral inputs; all progress uses production calls.
 */
#include "mac_adapter.h"
#define MAC_HANDOFF_MAIN handoff_component_main
#include "test_mac_handoff.c"

extern mac_attempt_record_t mac_adapter_receipt;
extern mac_epoch_stamp_t mac_adapter_live;
extern uint8_t mac_adapter_reserved_end;
uint8_t _mullong_PARM_2[4];
static mac_tx_interval_t adapter_tx;
static mac_tx_interval_action_t adapter_action;
static mac_tx_interval_event_t adapter_input;
static mac_epoch_stamp_t adapter_now, close_at;
static uint16_t adapter_end = 0x1500, adapter_owner = 0x1680, adapter_action_address = 0x1800;
static uint16_t adapter_clock_address = 0x1840, adapter_through_address = 0x1850;
static unsigned replies, wrong_dsn, rx_seen, busy_seen, closed_seen, sent_seen, retired_seen;
static unsigned later_reply;
#define ADAPTER_CASES 18u
static unsigned remaining_calls;
static const uint8_t adapter_packet[] = {
    0x61, 0x98, 0, 0x34, 0x12, 0x78, 0x56, 0xbc, 0x9a, 0xaa, 0x55, 0xcc
};
#if defined(MAC_ADAPTER_TRACE)
#include "mac_adapter_trace.h"
#endif

static uint16_t adapter_address(const volatile void *p)
{
    if (p == &mac_adapter_reserved_end) return adapter_end;
    if (p == _mullong_PARM_2) return helper+1u;
    if (p == &adapter_now) return adapter_clock_address;
    if (p == &close_at) return adapter_through_address;
#if defined(MAC_ADAPTER_TRACE)
    if (p == &mac_adapter_receipt) return TARGET_mac_adapter_receipt;
    if (p == &mac_adapter_receipt.frame) return TARGET_adapter_frame;
    if (p == mac_adapter_receipt.frame.body) return TARGET_adapter_body;
    if (p == &mac_adapter_live) return TARGET_mac_adapter_live;
#else
    if (p == &mac_adapter_receipt) return 0xc00;
    if (p == &mac_adapter_receipt.frame) return 0xc1e;
    if (p == mac_adapter_receipt.frame.body) return 0xc21;
    if (p == &mac_adapter_live) return 0xce0;
#endif
    if (p == &adapter_tx) return adapter_owner;
    if (p == &adapter_action) return adapter_action_address;
    return handoff_address(p);
}
static uint8_t adapter_load(uint8_t a, uint8_t value)
{
    if (a == SOC_RFD_ADDRESS && tx_count >= 4 && packets && remaining == 6 && fifo[head] == 5)
        fifo[(head+3)&127] = tx_fifo[3] ^ (wrong_dsn ? 1u : 0u);
    return handoff_load(a, value);
}
static void adapter_store(uint8_t a, uint8_t before, uint8_t value)
{
    if (a == SOC_RFST_ADDRESS && value == 0xea) {
        reply_after_tx = replies && (tx_fifo[1] & 0x20);
        rewrite_reply = 1;
    }
    handoff_store(a, before, value);
}
static void adapter_reset(void)
{
    normal_config = 0x1600; normal_output = 0x1880; helper = 0x1d00;
#if defined(MAC_ADAPTER_TRACE)
    normal_config = TARGET_fixture_config; helper = TARGET__gptrput_PARM_2;
    shared_address = TARGET_mac_radio_shared_end; radio_end = TARGET_mac_radio_reserved_end;
    owner_end = TARGET_mac_attempt_reserved_end; raw_address = TARGET_mac_radio_raw;
    config_staging = TARGET_mac_radio_config; attempt_raw_address = TARGET_mac_attempt_raw;
    epoch_address = TARGET_mac_attempt_first; handoff_staging_address = TARGET_mac_radio_handoff_clock;
    adapter_end = TARGET_mac_adapter_reserved_end; adapter_owner = TARGET_fixture_tx;
    adapter_action_address = TARGET_fixture_action;
    adapter_clock_address = TARGET_fixture_clock; adapter_through_address = TARGET_fixture_through;
#endif
    handoff_reset();
    memset((void *)mac_adapter_diagnostic(), 0, sizeof(mac_adapter_diagnostics_t));
    memset(&adapter_tx, 0, sizeof(adapter_tx));
    memset(&adapter_action, 0, sizeof(adapter_action));
    host_mmio_xaddress_hook = adapter_address;
    host_mmio_read_hook = adapter_load; host_mmio_write_hook = adapter_store;
    body_length = sizeof(adapter_packet);
    replies = 1; wrong_dsn = later_reply = 0;
    rx_seen = busy_seen = closed_seen = sent_seen = retired_seen = 0;
    remaining_calls = 1000; response_fcf = 0xfffa;
}
static mac_adapter_result_t adapter_tick(void)
{
    mac_adapter_result_t result;
    assert(remaining_calls--);
    handoff_started = mac_adapter_diagnostic()->phase == MAC_ADAPTER_RETIRING &&
        mac_adapter_diagnostic()->policy == MAC_ADAPTER_KEEP_AUTOACK &&
        adapter_tx.engine.outcome == MAC_TX_ACKED;
    result = mac_adapter_step(bound, cap);
    handoff_started = 0;
    return result;
}
static void consume_observation(void)
{
    assert(mac_adapter_consume(mac_adapter_observation()->token) == MAC_ADAPTER_OK);
}
static void close_receiver(void)
{
    mac_adapter_result_t result;
    assert(mac_adapter_close(NULL) == MAC_ADAPTER_OK);
    for (;;) {
        result = adapter_tick();
        assert(result == MAC_ADAPTER_WAIT || result == MAC_ADAPTER_EVENT);
        if (result != MAC_ADAPTER_EVENT) continue;
        if (mac_adapter_observation()->kind == MAC_ADAPTER_CLOSED_EVENT) {
            closed_seen++; consume_observation(); break;
        }
        assert(mac_adapter_observation()->kind == MAC_ADAPTER_RX_EVENT);
        rx_seen++; consume_observation();
    }
    assert(mac_adapter_diagnostic()->phase == MAC_ADAPTER_OFF && !XR(0x618b));
    assert(adapter_tick() == MAC_ADAPTER_WAIT);
}
static void start_receiver(void)
{
    adapter_reset();
    assert(mac_adapter_init(&config.value, bound, cap) == MAC_ADAPTER_OK);
}
static void start_adapter(void)
{
    start_receiver();
    close_receiver();
    assert(mac_adapter_now(bound, cap, &adapter_now) == MAC_ADAPTER_OK);
    assert(mac_tx_interval_init(&adapter_tx, 0x5a, adapter_now.symbols) == MAC_TX_OK);
}
static void drive_action(void)
{
    if (adapter_action.control.kind == MAC_TX_ACTION_RANDOM) {
        memset(&adapter_input, 0, sizeof(adapter_input));
        adapter_input.source.kind = MAC_TX_EVENT_RANDOM; adapter_input.source.value = 7;
        adapter_input.source.generation = adapter_tx.engine.generation;
        adapter_input.source.retry = adapter_tx.engine.retries;
        adapter_input.source.nb = adapter_tx.engine.nb;
        adapter_input.source.stamp = mac_adapter_diagnostic()->live.symbols;
        assert(mac_tx_observed_step(&adapter_tx, adapter_input.source.stamp,
                                   &adapter_input, &adapter_action) == MAC_TX_OK);
    }
    if (adapter_action.control.kind != MAC_TX_ACTION_NONE)
        assert(mac_adapter_accept(&adapter_tx, &adapter_action) == MAC_ADAPTER_OK);
}
static void arm_tx(uint8_t policy)
{
    assert(mac_adapter_prepare(&adapter_tx, policy, bound, cap) == MAC_ADAPTER_OK);
    assert(mac_adapter_now(bound, cap, &adapter_now) == MAC_ADAPTER_OK);
    assert(mac_tx_observed_step(&adapter_tx, adapter_now.symbols, NULL, &adapter_action) == MAC_TX_OK);
    drive_action();
}
static mac_adapter_result_t finish_tx(uint8_t policy)
{
    mac_adapter_result_t result;
    const mac_adapter_observation_t *view;
    while (adapter_tx.engine.phase != MAC_TX_DONE && adapter_tx.engine.phase != MAC_TX_FAULT) {
        result = adapter_tick();
        if (result != MAC_ADAPTER_WAIT && result != MAC_ADAPTER_EVENT && result != MAC_ADAPTER_EXPIRED)
            return result;
        view = mac_adapter_observation();
        if (result == MAC_ADAPTER_EVENT) {
            if (view->kind == MAC_ADAPTER_RX_EVENT) rx_seen++;
            if (view->tx.source.kind == MAC_TX_EVENT_BUSY_INTERVAL) busy_seen++;
            if (view->tx.source.kind == MAC_TX_EVENT_SENT_INTERVAL) sent_seen++;
            if (view->tx.source.kind == MAC_TX_EVENT_RETIRED) retired_seen++;
            if (later_reply && view->kind == MAC_ADAPTER_RX_EVENT && rx_seen == 1) {
                enqueue(5, 0xe9, 0x5a); later_reply = 0;
            }
        }
        assert(mac_tx_observed_step(&adapter_tx, mac_adapter_diagnostic()->live.symbols,
            result == MAC_ADAPTER_EVENT && view->tx.source.kind ? &view->tx : NULL,
            &adapter_action) == MAC_TX_OK);
        if (result == MAC_ADAPTER_EVENT) consume_observation();
        if (adapter_tx.engine.phase == MAC_TX_DRAW) {
            assert(mac_adapter_prepare(&adapter_tx, policy, bound, cap) == MAC_ADAPTER_OK);
            assert(mac_adapter_now(bound, cap, &adapter_now) == MAC_ADAPTER_OK);
            assert(mac_tx_observed_step(&adapter_tx, adapter_now.symbols, NULL, &adapter_action) == MAC_TX_OK);
        }
        drive_action();
    }
    assert(adapter_tx.engine.phase == MAC_TX_DONE);
    return MAC_ADAPTER_OK;
}
static mac_adapter_result_t drive_tx(uint8_t policy)
{
    arm_tx(policy);
    return finish_tx(policy);
}
static void submit_packet(uint8_t ack_request)
{
    uint8_t body[sizeof(adapter_packet)];
    memcpy(body, adapter_packet, sizeof(body));
    if (!ack_request) body[0] &= (uint8_t)~0x20u;
    assert(mac_tx_interval_submit(&adapter_tx, body, sizeof(body),
        mac_adapter_diagnostic()->live.symbols, 1000000, 1000) == MAC_TX_OK);
}
static void retained_fault(void)
{
    unsigned accesses_before = accesses;
    mac_attempt_record_t saved_record = *mac_adapter_record();
    mac_adapter_diagnostics_t saved_diagnostic = *mac_adapter_diagnostic();
    mac_adapter_observation_t saved_observation = *mac_adapter_observation();
    assert(saved_diagnostic.phase == MAC_ADAPTER_FAULT && saved_diagnostic.fault == MAC_ADAPTER_RADIO_ERROR);
    assert(mac_adapter_step(bound, cap) == MAC_ADAPTER_RADIO_ERROR);
    assert(mac_adapter_now(bound, cap, &adapter_now) == MAC_ADAPTER_RADIO_ERROR);
    assert(mac_adapter_consume(saved_observation.token) == MAC_ADAPTER_RADIO_ERROR);
    assert(mac_adapter_accept(&adapter_tx, &adapter_action) == MAC_ADAPTER_RADIO_ERROR);
    assert(mac_adapter_close(NULL) == MAC_ADAPTER_RADIO_ERROR);
    assert(mac_adapter_unprepare(&adapter_tx, bound, cap) == MAC_ADAPTER_RADIO_ERROR);
    assert(mac_adapter_init(&config.value, bound, cap) == MAC_ADAPTER_RADIO_ERROR);
    assert(!memcmp(&saved_record, mac_adapter_record(), sizeof(saved_record)) &&
           !memcmp(&saved_diagnostic, mac_adapter_diagnostic(), sizeof(saved_diagnostic)) &&
           !memcmp(&saved_observation, mac_adapter_observation(), sizeof(saved_observation)) &&
           accesses == accesses_before);
}
static void cancel_input(void)
{
    memset(&adapter_input, 0, sizeof(adapter_input));
    adapter_input.source.kind = MAC_TX_EVENT_CANCEL;
    adapter_input.source.generation = adapter_tx.engine.generation;
    adapter_input.source.retry = adapter_tx.engine.retries; adapter_input.source.nb = adapter_tx.engine.nb;
    adapter_input.source.stamp = mac_adapter_diagnostic()->live.symbols;
    assert(mac_tx_observed_step(&adapter_tx, adapter_input.source.stamp, &adapter_input, &adapter_action) == MAC_TX_OK);
}
static void invalid_inputs(void)
{
    unsigned accesses_before, i;
    uint16_t original, addresses[8];
    adapter_reset();
    accesses_before = accesses;
    assert(mac_adapter_now(bound, cap, &adapter_now) == MAC_ADAPTER_STATE);
    assert(mac_adapter_step(bound, cap) == MAC_ADAPTER_STATE);
    assert(mac_adapter_init(NULL, bound, cap) == MAC_ADAPTER_INVALID);
    assert(mac_adapter_init(&config.value, 0, cap) == MAC_ADAPTER_INVALID);
    assert(mac_adapter_init(&config.value, TIMEBASE_HALF_RANGE, cap) == MAC_ADAPTER_INVALID);
    assert(mac_adapter_init(&config.value, bound, 0) == MAC_ADAPTER_INVALID);
    config.value.channel = 10;
    assert(mac_adapter_init(&config.value, bound, cap) == MAC_ADAPTER_INVALID);
    config.value.channel = 11; config.value.power = 0;
    assert(mac_adapter_init(&config.value, bound, cap) == MAC_ADAPTER_INVALID);
    config.value.power = RADIO_AUTOACK_POWER_05;
    addresses[0] = 1; addresses[1] = adapter_end;
    addresses[2] = helper-24; addresses[3] = helper-11;
    addresses[4] = helper; addresses[5] = helper+1; addresses[6] = helper+3; addresses[7] = helper+4;
    original = config_address;
    for (i = 0; i < 8; i++) {
        config_address = addresses[i];
        assert(mac_adapter_init(&config.value, bound, cap) == MAC_ADAPTER_STORAGE);
    }
    config_address = 0x1df3;
    assert(mac_adapter_init(&config.value, bound, cap) == MAC_ADAPTER_RANGE);
    config_address = 0x1f00;
    assert(mac_adapter_init(&config.value, bound, cap) == MAC_ADAPTER_RANGE);
    config_address = 0xffff;
    assert(mac_adapter_init(&config.value, bound, cap) == MAC_ADAPTER_RANGE);
    config_address = original;
    assert(accesses == accesses_before);
    assert(mac_adapter_init(&config.value, bound, cap) == MAC_ADAPTER_OK);
    accesses_before = accesses;
    assert(mac_adapter_init(&config.value, bound, cap) == MAC_ADAPTER_STATE);
    assert(mac_adapter_now(bound, cap, NULL) == MAC_ADAPTER_INVALID);
    assert(mac_adapter_step(0, cap) == MAC_ADAPTER_INVALID);
    original = adapter_clock_address;
    adapter_clock_address = helper+4;
    assert(mac_adapter_now(bound, cap, &adapter_now) == MAC_ADAPTER_STORAGE);
    adapter_clock_address = 0x1dfb;
    assert(mac_adapter_now(bound, cap, &adapter_now) == MAC_ADAPTER_RANGE);
    adapter_clock_address = original;
    close_at = mac_adapter_diagnostic()->live; close_at.fine = 512;
    assert(mac_adapter_close(&close_at) == MAC_ADAPTER_INVALID);
    close_at = mac_adapter_diagnostic()->live; close_at.symbols--;
    assert(mac_adapter_close(&close_at) == MAC_ADAPTER_INVALID);
    close_at = mac_adapter_diagnostic()->live; close_at.symbols += MAC_TX_HALF;
    assert(mac_adapter_close(&close_at) == MAC_ADAPTER_INVALID);
    close_at.symbols++;
    assert(mac_adapter_close(&close_at) == MAC_ADAPTER_INVALID);
    close_at = mac_adapter_diagnostic()->live;
    if (close_at.fine) close_at.fine--;
    else { close_at.symbols--; close_at.fine = 511; }
    assert(mac_adapter_close(&close_at) == MAC_ADAPTER_INVALID);
    close_at.fine = 0; original = adapter_through_address; adapter_through_address = helper+4;
    assert(mac_adapter_close(&close_at) == MAC_ADAPTER_STORAGE);
    adapter_through_address = original;
    assert(accesses == accesses_before);
    close_receiver();
    accesses_before = accesses;
    assert(mac_adapter_prepare(NULL, MAC_ADAPTER_STOP_RX, bound, cap) == MAC_ADAPTER_INVALID);
    assert(mac_adapter_prepare(&adapter_tx, 3, bound, cap) == MAC_ADAPTER_INVALID);
    original = adapter_owner; adapter_owner = adapter_end;
    assert(mac_adapter_prepare(&adapter_tx, MAC_ADAPTER_STOP_RX, bound, cap) == MAC_ADAPTER_STORAGE);
    assert(mac_adapter_accept(&adapter_tx, &adapter_action) == MAC_ADAPTER_STORAGE);
    assert(mac_adapter_unprepare(&adapter_tx, bound, cap) == MAC_ADAPTER_STORAGE);
    adapter_owner = 0x1e00;
    assert(mac_adapter_prepare(&adapter_tx, MAC_ADAPTER_STOP_RX, bound, cap) == MAC_ADAPTER_RANGE);
    adapter_owner = original;
    original = adapter_action_address; adapter_action_address = helper+4;
    assert(mac_adapter_accept(&adapter_tx, &adapter_action) == MAC_ADAPTER_STORAGE);
    adapter_action_address = original;
    assert(mac_adapter_accept(NULL, &adapter_action) == MAC_ADAPTER_INVALID);
    assert(mac_adapter_accept(&adapter_tx, NULL) == MAC_ADAPTER_INVALID);
    assert(mac_adapter_unprepare(NULL, bound, cap) == MAC_ADAPTER_INVALID);
    assert(mac_adapter_consume(0) == MAC_ADAPTER_STATE && accesses == accesses_before);
}
static void adapter_case(unsigned selected)
{
    mac_adapter_result_t result;
    mac_adapter_observation_t saved_view;
    mac_epoch_stamp_t watermark;
    uint64_t desired, delta;
    uint8_t stopped = 0;
    unsigned i;
    if (selected == 0) {
    start_adapter(); submit_packet(1); assert(drive_tx(MAC_ADAPTER_KEEP_AUTOACK) == MAC_ADAPTER_OK);
    assert(adapter_tx.engine.outcome == MAC_TX_ACKED && adapter_tx.engine.pending &&
           mac_adapter_diagnostic()->phase == MAC_ADAPTER_RX && XR(0x618b) == 1 &&
           XR(0x6180) == 5 && XR(0x6189) == 0x60 && sent_seen == 1 && rx_seen == 1 && retired_seen == 1);
    assert(mac_tx_interval_release(&adapter_tx) == MAC_TX_OK);
    deliver_data();
    do { result = adapter_tick(); } while (result == MAC_ADAPTER_WAIT);
    assert(result == MAC_ADAPTER_EVENT && mac_adapter_observation()->kind == MAC_ADAPTER_RX_EVENT);
    memcpy(&saved_view, mac_adapter_observation(), sizeof(saved_view));
    for (i = 0; i < 10; i++) assert(adapter_tick() == MAC_ADAPTER_EVENT);
    assert(!memcmp(&saved_view, mac_adapter_observation(), sizeof(saved_view)));
    assert(mac_adapter_consume(saved_view.token+1) == MAC_ADAPTER_STATE);
    consume_observation(); close_receiver();
    assert(automatic_acks == 1 && !ack_active && tx_started == 1);
    } else if (selected == 1) {
    start_adapter(); cca_clear = 0; submit_packet(1); assert(drive_tx(MAC_ADAPTER_STOP_RX) == MAC_ADAPTER_OK);
    assert(adapter_tx.engine.outcome == MAC_TX_CHANNEL_ACCESS && busy_seen == 5 &&
           !sent_seen && !tx_started && mac_adapter_diagnostic()->phase == MAC_ADAPTER_OFF);
    } else if (selected == 2) {
    start_adapter(); replies = 0; submit_packet(1); assert(drive_tx(MAC_ADAPTER_STOP_RX) == MAC_ADAPTER_OK);
    assert(adapter_tx.engine.outcome == MAC_TX_NO_ACK && sent_seen == 4 && retired_seen == 4 &&
           tx_started == 4 && mac_adapter_diagnostic()->phase == MAC_ADAPTER_OFF);
    } else if (selected == 3) {
    start_adapter(); submit_packet(0); assert(drive_tx(MAC_ADAPTER_KEEP_RAW) == MAC_ADAPTER_OK);
    assert(adapter_tx.engine.outcome == MAC_TX_UNACKNOWLEDGED &&
           mac_adapter_diagnostic()->phase == MAC_ADAPTER_RX && XR(0x6189) == 0x40);
    close_receiver();
    } else if (selected == 4) {
        start_receiver();
        head = tail = 120; signals();
        enqueue(5, 0xe9, 0x33); enqueue(11, 0x69, 0x34); enqueue(13, 0xe9, 0x35);
        close_at = mac_adapter_diagnostic()->live; close_at.symbols += 50;
        assert(mac_adapter_close(&close_at) == MAC_ADAPTER_OK);
        stop_receive = 1;
        for (;;) {
            result = adapter_tick();
            assert(result == MAC_ADAPTER_WAIT || result == MAC_ADAPTER_EVENT);
            if (mac_adapter_diagnostic()->stop_started) {
                if (!stopped) { watermark = mac_adapter_diagnostic()->watermark; stopped = 1; }
                assert(!memcmp(&watermark, &mac_adapter_diagnostic()->watermark, sizeof(watermark)));
            }
            if (result != MAC_ADAPTER_EVENT) continue;
            if (mac_adapter_observation()->kind == MAC_ADAPTER_CLOSED_EVENT) {
                assert(stopped && rx_seen == 4 && !packets && !count &&
                       mac_adapter_diagnostic()->phase == MAC_ADAPTER_OFF &&
                       !memcmp(&watermark, &mac_adapter_observation()->through, sizeof(watermark)) &&
                       mac_adapter_diagnostic()->live.symbols > watermark.symbols);
                consume_observation(); break;
            }
            rx_seen++;
            assert(mac_adapter_observation()->kind == MAC_ADAPTER_RX_EVENT &&
                   mac_adapter_observation()->rx_serial == rx_seen &&
                   mac_adapter_observation()->frame->body[2] == (rx_seen < 4 ? 0x32+rx_seen : 0x38));
            if (rx_seen == 2) assert(!(mac_adapter_observation()->frame->crc_correlation & 0x80));
            consume_observation();
        }
    } else if (selected == 5) {
        start_receiver(); enqueue(11, 0x69, 0x20);
        assert(adapter_tick() == MAC_ADAPTER_WAIT && mac_adapter_diagnostic()->held &&
               !mac_adapter_diagnostic()->ready && !mac_adapter_diagnostic()->bound_valid);
        broken = 1;
        assert(adapter_tick() == MAC_ADAPTER_RADIO_ERROR && mac_adapter_diagnostic()->held &&
               !mac_adapter_diagnostic()->bound_valid && mac_adapter_record()->frame.length == 9 &&
               mac_adapter_record()->frame.crc_correlation == 0x69);
        retained_fault();
    } else if (selected == 6) {
        start_receiver(); head = tail = 120; signals(); enqueue(127, 0xe9, 0x20);
        do { result = adapter_tick(); } while (result == MAC_ADAPTER_WAIT);
        assert(result == MAC_ADAPTER_EVENT && mac_adapter_observation()->frame->length == 125 &&
               mac_adapter_observation()->frame->body[124] == (125u ^ 0x20u));
        saved_view = *mac_adapter_observation(); i = accesses;
        SOC_RFERRF = 4;
        assert(adapter_tick() == MAC_ADAPTER_EVENT &&
               !memcmp(&saved_view, mac_adapter_observation(), sizeof(saved_view)) && accesses == i);
        consume_observation();
        assert(adapter_tick() == MAC_ADAPTER_RADIO_ERROR);
        retained_fault();
    } else if (selected == 7 || selected == 8 || selected == 9 || selected == 17) {
        start_adapter();
        if (selected == 17) { response_fcf = 0xfff9; later_reply = 1; mmio_clocks = 16; }
        else if (selected == 9) guard_span = 512;
        else injection = selected == 7 ? 2 : 11;
        submit_packet(1);
        result = drive_tx(MAC_ADAPTER_KEEP_AUTOACK);
        if (selected == 17) {
            assert(result == MAC_ADAPTER_OK && adapter_tx.engine.outcome == MAC_TX_TIMING_UNCERTAIN &&
                   mac_adapter_diagnostic()->phase == MAC_ADAPTER_OFF && rx_seen == 2 && retired_seen == 1);
            return;
        }
        if (result != MAC_ADAPTER_RADIO_ERROR)
            fprintf(stderr, "handoff case%u result%u MAC%u/%u radio%u frames%u retired%u\n",
                    selected, result, adapter_tx.engine.phase, adapter_tx.engine.outcome,
                    mac_adapter_diagnostic()->radio_result, rx_seen, retired_seen);
        assert(result == MAC_ADAPTER_RADIO_ERROR);
        assert(adapter_tx.engine.phase == MAC_TX_STOPPING && adapter_tx.engine.outcome == MAC_TX_ACKED &&
               !retired_seen && sent_seen == 1 && rx_seen == 1);
        retained_fault();
    } else if (selected == 10 || selected == 11) {
        start_adapter(); wrong_dsn = selected == 10; bad_reply = selected == 11;
        submit_packet(1); assert(drive_tx(MAC_ADAPTER_STOP_RX) == MAC_ADAPTER_OK);
        assert(adapter_tx.engine.outcome == MAC_TX_NO_ACK && sent_seen == 4 && retired_seen == 4 && rx_seen == 4 &&
               tx_started == 4 && mac_adapter_diagnostic()->phase == MAC_ADAPTER_OFF);
    } else if (selected == 12) {
        start_adapter(); submit_packet(1);
        assert(mac_adapter_prepare(&adapter_tx, MAC_ADAPTER_KEEP_AUTOACK, bound, cap) == MAC_ADAPTER_OK);
        assert(mac_adapter_now(bound, cap, &adapter_now) == MAC_ADAPTER_OK);
        assert(mac_tx_observed_step(&adapter_tx, adapter_now.symbols, NULL, &adapter_action) == MAC_TX_OK);
        assert(adapter_action.control.kind == MAC_TX_ACTION_RANDOM);
        cancel_input();
        assert(adapter_tx.engine.phase == MAC_TX_DONE && adapter_tx.engine.outcome == MAC_TX_CANCELLED);
        assert(mac_adapter_unprepare(&adapter_tx, bound, cap) == MAC_ADAPTER_OK && !tx_started &&
               mac_adapter_diagnostic()->phase == MAC_ADAPTER_OFF);
        assert(mac_tx_interval_release(&adapter_tx) == MAC_TX_OK);
        submit_packet(0); assert(drive_tx(MAC_ADAPTER_KEEP_RAW) == MAC_ADAPTER_OK);
        assert(adapter_tx.engine.frame[2] == 0x5b && adapter_tx.engine.generation == 2);
        close_receiver();
    } else if (selected == 13) {
        start_adapter(); submit_packet(1); arm_tx(MAC_ADAPTER_STOP_RX);
        assert(mac_adapter_accept(&adapter_tx, &adapter_action) == MAC_ADAPTER_STATE);
        delta = (uint32_t)(adapter_action.control.until-mac_adapter_diagnostic()->live.symbols)+1u;
        advance_clocks((uint32_t)(delta*512u));
        assert(adapter_tick() == MAC_ADAPTER_EXPIRED && !tx_started);
        assert(finish_tx(MAC_ADAPTER_STOP_RX) == MAC_ADAPTER_OK && !tx_started &&
               adapter_tx.engine.outcome == MAC_TX_LIFETIME && retired_seen == 1 &&
               mac_adapter_diagnostic()->phase == MAC_ADAPTER_OFF);
    } else if (selected == 14) {
        start_receiver();
        desired = (UINT64_C(0xffffff)-100u)*512u;
        while (model_clocks < desired) {
            delta = desired-model_clocks;
            advance_clocks((uint32_t)(delta > UINT32_C(0x70000000) ? UINT32_C(0x70000000) : delta));
            assert(mac_adapter_now(bound, cap, &adapter_now) == MAC_ADAPTER_OK);
        }
        close_receiver();
        assert(mac_adapter_now(bound, cap, &adapter_now) == MAC_ADAPTER_OK);
        assert(mac_tx_interval_init(&adapter_tx, 0x5a, adapter_now.symbols) == MAC_TX_OK);
        submit_packet(1); assert(drive_tx(MAC_ADAPTER_KEEP_AUTOACK) == MAC_ADAPTER_OK);
        assert(adapter_tx.engine.outcome == MAC_TX_ACKED && coarse < 1000);
        close_receiver();
    } else if (selected == 15) {
        start_adapter();
        assert(mac_tx_interval_submit(&adapter_tx, adapter_packet, sizeof(adapter_packet),
            mac_adapter_diagnostic()->live.symbols, 1000000, 1) == MAC_TX_OK);
        assert(mac_adapter_prepare(&adapter_tx, MAC_ADAPTER_STOP_RX, bound, cap) == MAC_ADAPTER_OK);
        assert(mac_adapter_now(bound, cap, &adapter_now) == MAC_ADAPTER_OK);
        for (i = 0; i < 3 && adapter_tx.engine.phase != MAC_TX_DONE; i++)
            assert(mac_tx_observed_step(&adapter_tx, adapter_now.symbols, NULL, &adapter_action) == MAC_TX_OK);
        assert(adapter_tx.engine.outcome == MAC_TX_WORK_LIMIT && adapter_tx.engine.phase == MAC_TX_DONE);
        assert(mac_adapter_unprepare(&adapter_tx, bound, cap) == MAC_ADAPTER_OK && !tx_started);
    } else {
        assert(selected == 16); invalid_inputs();
    }
}
#ifndef MAC_ADAPTER_MAIN
#define MAC_ADAPTER_MAIN main
#endif
int MAC_ADAPTER_MAIN(int argc, char **argv)
{
    unsigned selected;
#if defined(MAC_ADAPTER_TRACE)
    if (argc == 3 && !strcmp(argv[1], "--vector")) {
        selected = (unsigned)strtoul(argv[2], NULL, 0);
        assert(selected < ADAPTER_CASES);
        trace_enabled = 1;
        printf("{\"case\":%u,\"steps\":[", selected);
        adapter_case(selected);
        puts("]}");
        return 0;
    }
#else
    (void)argv;
#endif
    assert(argc == 1);
    for (selected = 0; selected < ADAPTER_CASES; selected++) adapter_case(selected);
    puts("MAC adapter: real actions/closure/drain, handoff, faults, backpressure, cancellation, expiry, wrap and storage guards PASS.");
    return 0;
}
